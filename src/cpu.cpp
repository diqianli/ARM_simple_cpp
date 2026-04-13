/// @file cpu.cpp
/// @brief Top-level CPUEmulator implementation.
///
/// Ported from Rust src/cpu.rs.

#include "arm_cpu/cpu.hpp"
#include "arm_cpu/chi/chi_manager.hpp"
#include "arm_cpu/memory/memory_subsystem.hpp"
#include "arm_cpu/ooo/ooo_engine.hpp"
#include "arm_cpu/simulation/pipeline_tracker.hpp"
#include "arm_cpu/visualization/visualization_state.hpp"

#include <algorithm>
#include <cstdio>
#include <format>
#include <iostream>
#include <memory>
#include <utility>

namespace arm_cpu {

// =====================================================================
// CPUEmulator -- private constructor
// =====================================================================

CPUEmulator::CPUEmulator(CPUConfig config,
                         std::unique_ptr<OoOEngine> ooo_engine,
                         std::unique_ptr<MemorySubsystem> memory,
                         std::unique_ptr<ChiManager> chi_manager,
                         StatsCollector stats,
                         TraceOutput trace,
                         std::unique_ptr<VisualizationState> visualization,
                         std::unique_ptr<PipelineTracker> pipeline_tracker)
    : config_(std::move(config))
    , ooo_engine_(std::move(ooo_engine))
    , memory_(std::move(memory))
    , chi_manager_(std::move(chi_manager))
    , stats_(std::move(stats))
    , trace_(std::move(trace))
    , visualization_(std::move(visualization))
    , pipeline_tracker_(std::move(pipeline_tracker))
    , current_cycle_(0)
    , committed_count_(0)
    , running_(false)
{}

// =====================================================================
// CPUEmulator -- destructor (needs complete types for unique_ptr)
// =====================================================================

CPUEmulator::~CPUEmulator() = default;

// =====================================================================
// CPUEmulator -- factory methods
// =====================================================================

Result<std::unique_ptr<CPUEmulator>> CPUEmulator::create(CPUConfig config) {
    TRY(config.validate());

    auto ooo_engine = TRY(OoOEngine::create(config));
    auto memory = TRY(MemorySubsystem::create(config));

    ChiConfig chi_cfg;
    chi_cfg.enable_chi = config.enable_chi;
    chi_cfg.chi_request_latency = config.chi_request_latency;
    chi_cfg.chi_response_latency = config.chi_response_latency;
    chi_cfg.outstanding_requests = config.outstanding_requests;
    auto chi_manager = std::make_unique<ChiManager>(chi_cfg);

    StatsCollector stats;
    TraceOutput trace;
    if (config.enable_trace_output) {
        trace = TraceOutput(config.max_trace_output > 0 ? config.max_trace_output : 10000);
    } else {
        trace = TraceOutput::disabled();
    }

    auto visualization = std::make_unique<VisualizationState>(VisualizationConfig{});
    auto pipeline_tracker = std::make_unique<PipelineTracker>();

    return std::unique_ptr<CPUEmulator>(new CPUEmulator(
        std::move(config),
        std::move(ooo_engine),
        std::move(memory),
        std::move(chi_manager),
        std::move(stats),
        std::move(trace),
        std::move(visualization),
        std::move(pipeline_tracker)
    ));
}

Result<std::unique_ptr<CPUEmulator>> CPUEmulator::create_with_visualization(
    CPUConfig config, VisualizationConfig viz_config)
{
    TRY(config.validate());

    auto ooo_engine = TRY(OoOEngine::create(config));
    auto memory = TRY(MemorySubsystem::create(config));

    ChiConfig chi_cfg;
    chi_cfg.enable_chi = config.enable_chi;
    chi_cfg.chi_request_latency = config.chi_request_latency;
    chi_cfg.chi_response_latency = config.chi_response_latency;
    chi_cfg.outstanding_requests = config.outstanding_requests;
    auto chi_manager = std::make_unique<ChiManager>(chi_cfg);

    StatsCollector stats;
    TraceOutput trace;
    if (config.enable_trace_output) {
        trace = TraceOutput(config.max_trace_output > 0 ? config.max_trace_output : 10000);
    } else {
        trace = TraceOutput::disabled();
    }

    auto visualization = std::make_unique<VisualizationState>(std::move(viz_config));
    auto pipeline_tracker = std::make_unique<PipelineTracker>();

    return std::unique_ptr<CPUEmulator>(new CPUEmulator(
        std::move(config),
        std::move(ooo_engine),
        std::move(memory),
        std::move(chi_manager),
        std::move(stats),
        std::move(trace),
        std::move(visualization),
        std::move(pipeline_tracker)
    ));
}

Result<std::unique_ptr<CPUEmulator>> CPUEmulator::create_with_defaults() {
    return create(CPUConfig::default_config());
}

// =====================================================================
// CPUEmulator -- run / step
// =====================================================================

Result<PerformanceMetrics> CPUEmulator::run(
    std::function<std::optional<Result<Instruction>>()> next_instr)
{
    return run_with_limit(std::move(next_instr), 1'000'000'000ULL);
}

Result<PerformanceMetrics> CPUEmulator::run_with_limit(
    std::function<std::optional<Result<Instruction>>()> next_instr,
    uint64_t max_cycles)
{
    running_ = true;
    uint64_t start_cycle = current_cycle_;
    uint64_t stall_cycles = 0;

    while (running_) {
        uint64_t committed_before = committed_count_;

        // Capture pre-execute snapshot for visualization
        visualization_->set_cycle(current_cycle_);
        visualization_->set_committed_count(committed_count_);

        // Fetch and dispatch instructions
        fetch_dispatch(next_instr);

        // Process completions for this cycle FIRST (releases dependencies)
        auto completions_processed = ooo_engine_->cycle_tick();

        if (completions_processed > 0) {
            // Process newly ready instructions
            auto newly_ready = ooo_engine_->take_newly_ready();
            for (auto ready_id : newly_ready) {
                pipeline_tracker_->record_issue(ready_id, current_cycle_);
                visualization_->pipeline_tracker().record_ready(ready_id, current_cycle_);
            }
        }

        // Execute ready instructions
        TRY(execute());

        // Complete memory operations
        TRY(complete_memory());

        // Commit completed instructions
        TRY(commit());

        // Capture post-execute snapshot for visualization
        // (visualization captures happen in the pipeline tracker)

        // Advance cycle
        advance_cycle();

        // Check termination
        if (should_stop()) {
            running_ = false;
        }

        // Track stall cycles for debugging
        if (committed_count_ == committed_before) {
            stall_cycles++;
        } else {
            stall_cycles = 0;
        }

        // Safety check for infinite loops
        if (stall_cycles > 10000) {
            auto [waiting, ready, executing, completed] = ooo_engine_->status_counts();
            std::fprintf(stderr,
                "Warning: No progress for %llu cycles, stopping. "
                "Window: waiting=%zu, ready=%zu, executing=%zu, completed=%zu, committed=%llu\n",
                static_cast<unsigned long long>(stall_cycles),
                waiting, ready, executing, completed,
                static_cast<unsigned long long>(committed_count_));
            running_ = false;
        }

        // Safety check for absolute cycle limit
        if (current_cycle_ - start_cycle >= max_cycles) {
            auto [waiting, ready, executing, completed] = ooo_engine_->status_counts();
            std::fprintf(stderr,
                "Warning: Cycle limit reached (%llu), stopping. "
                "Window: waiting=%zu, ready=%zu, executing=%zu, completed=%zu, committed=%llu\n",
                static_cast<unsigned long long>(max_cycles),
                waiting, ready, executing, completed,
                static_cast<unsigned long long>(committed_count_));
            running_ = false;
        }
    }

    return get_metrics();
}

void CPUEmulator::run_cycles(uint64_t cycles) {
    for (uint64_t i = 0; i < cycles; ++i) {
        step();
    }
}

void CPUEmulator::step() {
    // Capture pre-execute snapshot
    visualization_->set_cycle(current_cycle_);
    visualization_->set_committed_count(committed_count_);

    // Process completions for this cycle FIRST (releases dependencies)
    ooo_engine_->cycle_tick();

    // Record ready cycle for newly ready instructions
    auto newly_ready = ooo_engine_->take_newly_ready();
    for (auto ready_id : newly_ready) {
        pipeline_tracker_->record_issue(ready_id, current_cycle_);
        visualization_->pipeline_tracker().record_ready(ready_id, current_cycle_);
    }

    // Execute ready instructions (including newly woken up dependents)
    (void)execute();

    // Complete memory operations
    (void)complete_memory();

    // Commit
    (void)commit();

    // Advance cycle
    advance_cycle();
}

// =====================================================================
// CPUEmulator -- dispatch
// =====================================================================

Result<void> CPUEmulator::dispatch(Instruction instr) {
    if (!ooo_engine_->can_accept()) {
        return EmulatorError::internal("Instruction window full");
    }

    InstructionId instr_id = instr.id;

    stats_.record_dispatch(instr.id, current_cycle_);
    trace_.record_dispatch(instr, current_cycle_);

    // Track pipeline stages for visualization
    pipeline_tracker_->record_fetch(instr, current_cycle_);
    pipeline_tracker_->record_dispatch(instr_id, current_cycle_);
    visualization_->pipeline_tracker().record_fetch(instr, current_cycle_);
    visualization_->pipeline_tracker().record_dispatch(instr_id, current_cycle_);

    // Dispatch to OoO engine and get dependencies
    auto dependencies = TRY(ooo_engine_->dispatch(std::move(instr)));

    // Record dependencies for visualization
    for (const auto& dep : dependencies) {
        pipeline_tracker_->add_dependency(instr_id, dep.producer, dep.is_memory);
        visualization_->pipeline_tracker().add_dependency(instr_id, dep.producer, dep.is_memory);
    }

    return {};
}

// =====================================================================
// CPUEmulator -- internal pipeline stages
// =====================================================================

void CPUEmulator::fetch_dispatch(
    std::function<std::optional<Result<Instruction>>()> next_instr)
{
    std::size_t free_slots = ooo_engine_->free_slots();
    std::size_t fetch_limit = std::min(free_slots, config_.fetch_width);
    std::size_t dispatched = 0;
    std::size_t failed = 0;

    while (dispatched < fetch_limit) {
        auto result = next_instr();
        if (!result.has_value()) break;

        if (result->has_error()) {
            break;
        }

        auto instr = std::move(result->value());
        auto dispatch_result = dispatch(std::move(instr));
        if (dispatch_result.ok()) {
            dispatched++;
        } else {
            failed++;
            if (failed > 10) break;
        }
    }
}

Result<void> CPUEmulator::execute() {
    auto ready = ooo_engine_->get_ready_instructions();

    for (auto& [id, instr] : ready) {
        ooo_engine_->mark_executing(id);
        stats_.record_issue(id, current_cycle_);
        trace_.record_issue(id, current_cycle_);

        // Track issue stage for visualization
        pipeline_tracker_->record_issue(id, current_cycle_);
        visualization_->pipeline_tracker().record_issue(id, current_cycle_);

        if (is_memory_op(instr.opcode_type)) {
            // Handle memory operation
            if (instr.mem_access.has_value()) {
                TRY(handle_memory_op(id, instr.mem_access.value()));
            } else {
                // Memory op without address - complete immediately
                uint64_t complete_cycle = current_cycle_ + 1;
                ooo_engine_->mark_completed(id, complete_cycle);

                pipeline_tracker_->record_memory(id, current_cycle_, complete_cycle);
                pipeline_tracker_->record_complete(id, complete_cycle);
                visualization_->pipeline_tracker().record_memory(id, current_cycle_, complete_cycle);
                visualization_->pipeline_tracker().record_complete(id, complete_cycle);
            }
        } else {
            // Compute instruction - complete after latency
            uint64_t lat = instr.instr_latency();
            uint64_t complete_cycle = current_cycle_ + lat;
            ooo_engine_->mark_completed(id, complete_cycle);

            // Track execute and complete for visualization
            pipeline_tracker_->record_execute(id, current_cycle_, complete_cycle);
            pipeline_tracker_->record_complete(id, complete_cycle);
            visualization_->pipeline_tracker().record_execute_start(id, current_cycle_);
            visualization_->pipeline_tracker().record_execute_end(id, complete_cycle);
            visualization_->pipeline_tracker().record_complete(id, complete_cycle);
        }
    }

    return {};
}

Result<void> CPUEmulator::handle_memory_op(InstructionId id, const MemAccess& access) {
    MemoryRequest request;
    if (access.is_load) {
        request = memory_->load(id, access);
    } else {
        request = memory_->store(id, access);
    }

    // Memory requests now always complete (no pending state)
    uint64_t complete_cycle = request.complete_cycle.value_or(current_cycle_ + 1);
    ooo_engine_->mark_completed(id, complete_cycle);

    // Track memory stage for visualization
    pipeline_tracker_->record_memory(id, current_cycle_, complete_cycle);
    pipeline_tracker_->record_complete(id, complete_cycle);
    visualization_->pipeline_tracker().record_memory(id, current_cycle_, complete_cycle);
    visualization_->pipeline_tracker().record_complete(id, complete_cycle);

    // Record cache hierarchy info for visualization
    if (request.cache_info.has_value()) {
        CacheAccessInfoViz viz_info;
        viz_info.servicing_level = request.cache_info->servicing_level;
        viz_info.total_latency = request.cache_info->total_latency;
        viz_info.level_timing = request.cache_info->level_timing;
        visualization_->pipeline_tracker().record_cache_access(id, viz_info);
    }

    // Record stats
    if (access.is_load) {
        uint64_t lat = complete_cycle - current_cycle_;
        stats_.record_load(access.size, lat);
        stats_.record_l1_access(true); // Simplified
    } else {
        stats_.record_store(access.size, 1);
    }

    return {};
}

Result<void> CPUEmulator::complete_memory() {
    // In this simplified model, all memory operations complete immediately
    // when issued (no asynchronous completion)
    return {};
}

Result<void> CPUEmulator::commit() {
    auto commit_candidates = ooo_engine_->get_commit_candidates();

    for (const auto& instr : commit_candidates) {
        InstructionId id = instr.id;

        ooo_engine_->commit(id);
        committed_count_++;

        // Track retire stage for visualization
        pipeline_tracker_->record_retire(id, current_cycle_);
        visualization_->pipeline_tracker().record_retire(id, current_cycle_);

        stats_.record_commit(instr, current_cycle_);
        trace_.record_commit(id, current_cycle_);
    }

    return {};
}

void CPUEmulator::advance_cycle() {
    current_cycle_++;
    ooo_engine_->advance_cycle();
    memory_->advance_cycle();
    chi_manager_->advance_cycle();
    stats_.record_cycles(1);

    // Update visualization cycle counter
    visualization_->set_cycle(current_cycle_);
    visualization_->set_committed_count(committed_count_);
}

bool CPUEmulator::should_stop() const {
    // Stop if window is empty and source is exhausted
    return ooo_engine_->is_empty();
}

// =====================================================================
// CPUEmulator -- control methods
// =====================================================================

void CPUEmulator::stop() {
    running_ = false;
}

void CPUEmulator::reset() {
    auto ooo = OoOEngine::create(config_);
    if (!ooo.ok()) return;
    auto mem = MemorySubsystem::create(config_);
    if (!mem.ok()) return;

    ooo_engine_ = std::move(ooo.value());
    memory_ = std::move(mem.value());

    ChiConfig chi_cfg;
    chi_cfg.enable_chi = config_.enable_chi;
    chi_cfg.chi_request_latency = config_.chi_request_latency;
    chi_cfg.chi_response_latency = config_.chi_response_latency;
    chi_cfg.outstanding_requests = config_.outstanding_requests;
    chi_manager_ = std::make_unique<ChiManager>(chi_cfg);

    stats_.reset();
    trace_.clear();
    visualization_->clear();
    pipeline_tracker_->clear();
    current_cycle_ = 0;
    committed_count_ = 0;
    running_ = false;
}

// =====================================================================
// CPUEmulator -- metrics
// =====================================================================

PerformanceMetrics CPUEmulator::get_metrics() const {
    return stats_.get_metrics();
}

void CPUEmulator::print_summary() const {
    auto metrics = get_metrics();
    std::cout << metrics.summary() << std::endl;
}

} // namespace arm_cpu
