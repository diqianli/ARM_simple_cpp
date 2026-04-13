/// @file simulation_engine.cpp
/// @brief Core simulation engine implementation.
///
/// Ported from Rust src/simulation/engine.rs and src/simulation/event.rs.

#include "arm_cpu/simulation/simulation_engine.hpp"
#include "arm_cpu/simulation/pipeline_tracker.hpp"
#include "arm_cpu/memory/memory_subsystem.hpp"
#include "arm_cpu/ooo/ooo_engine.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

namespace arm_cpu {

// =====================================================================
// SimExecutionUnit
// =====================================================================

SimExecutionUnit sim_execution_unit_from_opcode(OpcodeType opcode) {
    switch (opcode) {
        // Integer arithmetic -> IntAlu
        case OpcodeType::Add: case OpcodeType::Sub: case OpcodeType::And: case OpcodeType::Orr:
        case OpcodeType::Eor: case OpcodeType::Lsl: case OpcodeType::Lsr: case OpcodeType::Asr:
        case OpcodeType::Mov: case OpcodeType::Cmp: case OpcodeType::Shift:
        case OpcodeType::Vmov: case OpcodeType::Vdup: case OpcodeType::Nop:
            return SimExecutionUnit::IntAlu;

        // Integer multiply/divide
        case OpcodeType::Mul: return SimExecutionUnit::IntMul;
        case OpcodeType::Div: return SimExecutionUnit::IntDiv;

        // Load operations
        case OpcodeType::Load: case OpcodeType::LoadPair: case OpcodeType::Vld:
            return SimExecutionUnit::Load;

        // Store operations
        case OpcodeType::Store: case OpcodeType::StorePair: case OpcodeType::Vst:
            return SimExecutionUnit::Store;

        // Branch operations
        case OpcodeType::Branch: case OpcodeType::BranchCond: case OpcodeType::BranchReg:
            return SimExecutionUnit::Branch;

        // Floating-point operations
        case OpcodeType::Fadd: case OpcodeType::Fsub: case OpcodeType::Fmul: case OpcodeType::Fdiv:
        case OpcodeType::Fmadd: case OpcodeType::Fmsub: case OpcodeType::Fnmadd: case OpcodeType::Fnmsub:
            return SimExecutionUnit::Fp;

        // SIMD operations
        case OpcodeType::Vadd: case OpcodeType::Vsub: case OpcodeType::Vmul:
        case OpcodeType::Vmla: case OpcodeType::Vmls:
            return SimExecutionUnit::Simd;

        // Crypto operations
        case OpcodeType::Aesd: case OpcodeType::Aese: case OpcodeType::Aesimc: case OpcodeType::Aesmc:
        case OpcodeType::Sha1H: case OpcodeType::Sha256H: case OpcodeType::Sha512H:
            return SimExecutionUnit::Crypto;

        // Cache maintenance and system
        case OpcodeType::DcZva: case OpcodeType::DcCivac: case OpcodeType::DcCvac: case OpcodeType::DcCsw:
        case OpcodeType::IcIvau: case OpcodeType::IcIallu: case OpcodeType::IcIalluis:
        case OpcodeType::Msr: case OpcodeType::Mrs: case OpcodeType::Sys:
            return SimExecutionUnit::System;

        default:
            return SimExecutionUnit::IntAlu;
    }
}

const char* sim_execution_unit_name(SimExecutionUnit unit) {
    switch (unit) {
        case SimExecutionUnit::IntAlu:  return "IntAlu";
        case SimExecutionUnit::IntMul:  return "IntMul";
        case SimExecutionUnit::IntDiv:  return "IntDiv";
        case SimExecutionUnit::Load:    return "Load";
        case SimExecutionUnit::Store:   return "Store";
        case SimExecutionUnit::Branch:  return "Branch";
        case SimExecutionUnit::Fp:      return "Fp";
        case SimExecutionUnit::Simd:    return "Simd";
        case SimExecutionUnit::Crypto:  return "Crypto";
        case SimExecutionUnit::System:  return "System";
    }
    return "Unknown";
}

// =====================================================================
// SimulationEvent
// =====================================================================

SimulationEvent SimulationEvent::instruction_fetch(const Instruction& instr, uint64_t cycle) {
    SimulationEvent e;
    e.kind = Kind::InstructionFetch;
    e.instr = instr;
    e.fetch_cycle = cycle;
    return e;
}

SimulationEvent SimulationEvent::instruction_dispatch(InstructionId id, uint64_t cycle) {
    SimulationEvent e;
    e.kind = Kind::InstructionDispatch;
    e.event_id = id;
    e.event_cycle = cycle;
    return e;
}

SimulationEvent SimulationEvent::instruction_decode(InstructionId id, uint64_t cycle) {
    SimulationEvent e;
    e.kind = Kind::InstructionDecode;
    e.event_id = id;
    e.event_cycle = cycle;
    return e;
}

SimulationEvent SimulationEvent::instruction_rename(InstructionId id, uint64_t cycle) {
    SimulationEvent e;
    e.kind = Kind::InstructionRename;
    e.event_id = id;
    e.event_cycle = cycle;
    return e;
}

SimulationEvent SimulationEvent::instruction_issue(InstructionId id, uint64_t cycle, SimExecutionUnit unit) {
    SimulationEvent e;
    e.kind = Kind::InstructionIssue;
    e.event_id = id;
    e.event_cycle = cycle;
    e.issue_unit = unit;
    return e;
}

SimulationEvent SimulationEvent::instruction_execute_start(InstructionId id, uint64_t cycle) {
    SimulationEvent e;
    e.kind = Kind::InstructionExecuteStart;
    e.event_id = id;
    e.event_cycle = cycle;
    return e;
}

SimulationEvent SimulationEvent::instruction_execute_end(InstructionId id, uint64_t cycle) {
    SimulationEvent e;
    e.kind = Kind::InstructionExecuteEnd;
    e.event_id = id;
    e.event_cycle = cycle;
    return e;
}

SimulationEvent SimulationEvent::memory_access(InstructionId id, uint64_t addr, uint8_t size,
                                                bool is_load, uint64_t latency, uint8_t hit_level) {
    SimulationEvent e;
    e.kind = Kind::MemoryAccess;
    e.event_id = id;
    e.mem_addr = addr;
    e.mem_size = size;
    e.mem_is_load = is_load;
    e.mem_latency = latency;
    e.mem_hit_level = hit_level;
    return e;
}

SimulationEvent SimulationEvent::memory_complete(InstructionId id, uint64_t cycle) {
    SimulationEvent e;
    e.kind = Kind::MemoryComplete;
    e.event_id = id;
    e.event_cycle = cycle;
    return e;
}

SimulationEvent SimulationEvent::instruction_complete(InstructionId id, uint64_t cycle) {
    SimulationEvent e;
    e.kind = Kind::InstructionComplete;
    e.event_id = id;
    e.event_cycle = cycle;
    return e;
}

SimulationEvent SimulationEvent::instruction_retire(InstructionId id, uint64_t cycle, uint64_t retire_order) {
    SimulationEvent e;
    e.kind = Kind::InstructionRetire;
    e.event_id = id;
    e.event_cycle = cycle;
    e.retire_order = retire_order;
    return e;
}

SimulationEvent SimulationEvent::dependency(InstructionId consumer, InstructionId producer, bool is_memory) {
    SimulationEvent e;
    e.kind = Kind::Dependency;
    e.event_id = consumer;
    e.dep_producer = producer;
    e.dep_is_memory = is_memory;
    return e;
}

SimulationEvent SimulationEvent::branch_prediction(InstructionId id, uint64_t target, bool correct) {
    SimulationEvent e;
    e.kind = Kind::BranchPrediction;
    e.event_id = id;
    e.predicted_target = target;
    e.prediction_correct = correct;
    return e;
}

SimulationEvent SimulationEvent::cycle_boundary(uint64_t cycle, uint64_t committed_count) {
    SimulationEvent e;
    e.kind = Kind::CycleBoundary;
    e.event_cycle = cycle;
    e.boundary_committed_count = committed_count;
    return e;
}

SimulationEvent SimulationEvent::simulation_start(uint64_t start_cycle) {
    SimulationEvent e;
    e.kind = Kind::SimulationStart;
    e.event_cycle = start_cycle;
    return e;
}

SimulationEvent SimulationEvent::simulation_end(uint64_t end_cycle, uint64_t total_committed) {
    SimulationEvent e;
    e.kind = Kind::SimulationEnd;
    e.event_cycle = end_cycle;
    e.end_total_committed = total_committed;
    return e;
}

const char* SimulationEvent::kind_name() const {
    switch (kind) {
        case Kind::InstructionFetch:    return "InstructionFetch";
        case Kind::InstructionDispatch: return "InstructionDispatch";
        case Kind::InstructionDecode:   return "InstructionDecode";
        case Kind::InstructionRename:   return "InstructionRename";
        case Kind::InstructionIssue:    return "InstructionIssue";
        case Kind::InstructionExecuteStart: return "InstructionExecuteStart";
        case Kind::InstructionExecuteEnd:   return "InstructionExecuteEnd";
        case Kind::MemoryAccess:        return "MemoryAccess";
        case Kind::MemoryComplete:      return "MemoryComplete";
        case Kind::InstructionComplete: return "InstructionComplete";
        case Kind::InstructionRetire:   return "InstructionRetire";
        case Kind::Dependency:          return "Dependency";
        case Kind::BranchPrediction:    return "BranchPrediction";
        case Kind::CycleBoundary:       return "CycleBoundary";
        case Kind::SimulationStart:     return "SimulationStart";
        case Kind::SimulationEnd:       return "SimulationEnd";
    }
    return "Unknown";
}

// =====================================================================
// EventLogger
// =====================================================================

EventLogger::EventLogger(bool verbose) : verbose_(verbose) {}

void EventLogger::on_event(const SimulationEvent& event) {
    event_count_++;
    if (verbose_) {
        std::fprintf(stderr, "Event #%llu: %s\n",
                     static_cast<unsigned long long>(event_count_),
                     event.kind_name());
    }
}

// =====================================================================
// MultiSink
// =====================================================================

void MultiSink::add_sink(std::shared_ptr<std::mutex> mtx, std::shared_ptr<SimulationEventSink> sink) {
    sinks_.push_back({std::move(mtx), std::move(sink)});
}

void MultiSink::dispatch(const SimulationEvent& event) {
    for (auto& entry : sinks_) {
        std::lock_guard<std::mutex> lock(*entry.mutex);
        entry.sink->on_event(event);
    }
}

void MultiSink::flush_all() {
    for (auto& entry : sinks_) {
        std::lock_guard<std::mutex> lock(*entry.mutex);
        entry.sink->flush();
    }
}

// =====================================================================
// SimulationEngine
// =====================================================================

SimulationEngine::SimulationEngine(CPUConfig config,
                                   std::unique_ptr<OoOEngine> ooo_engine,
                                   std::unique_ptr<MemorySubsystem> memory,
                                   StatsCollector stats,
                                   TraceOutput trace,
                                   PipelineTracker tracker)
    : config_(std::move(config))
    , ooo_engine_(std::move(ooo_engine))
    , memory_(std::move(memory))
    , pipeline_tracker_(std::make_unique<PipelineTracker>(std::move(tracker)))
    , stats_(std::move(stats))
    , trace_(std::move(trace))
    , current_cycle_(0)
    , committed_count_(0)
    , running_(false)
{}

Result<std::unique_ptr<SimulationEngine>> SimulationEngine::create(CPUConfig config) {
    auto validate_result = config.validate();
    if (validate_result.has_error()) return validate_result.error();

    auto ooo_engine_result = OoOEngine::create(config);
    if (ooo_engine_result.has_error()) return ooo_engine_result.error();
    auto ooo_engine = std::move(ooo_engine_result.value());

    auto memory_result = MemorySubsystem::create(config);
    if (memory_result.has_error()) return memory_result.error();
    auto memory = std::move(memory_result.value());

    StatsCollector stats;
    TraceOutput trace;
    if (config.enable_trace_output) {
        trace = TraceOutput(config.max_trace_output > 0 ? config.max_trace_output : 10000);
    } else {
        trace = TraceOutput::disabled();
    }

    PipelineTracker tracker;

    // Use raw pointer construction since the constructor is private
    return std::unique_ptr<SimulationEngine>(new SimulationEngine(
        std::move(config),
        std::move(ooo_engine),
        std::move(memory),
        std::move(stats),
        std::move(trace),
        std::move(tracker)
    ));
}

void SimulationEngine::add_event_sink(std::shared_ptr<std::mutex> mtx,
                                       std::shared_ptr<SimulationEventSink> sink) {
    event_sinks_.add_sink(std::move(mtx), std::move(sink));
}

void SimulationEngine::clear_event_sinks() {
    event_sinks_ = MultiSink{};
}

void SimulationEngine::emit_event(const SimulationEvent& event) {
    event_sinks_.dispatch(event);
}

const CPUConfig& SimulationEngine::config() const { return config_; }
uint64_t SimulationEngine::current_cycle() const { return current_cycle_; }
uint64_t SimulationEngine::committed_count() const { return committed_count_; }

const PipelineTracker& SimulationEngine::pipeline_tracker() const { return *pipeline_tracker_; }
PipelineTracker& SimulationEngine::pipeline_tracker_mut() { return *pipeline_tracker_; }

Result<void> SimulationEngine::dispatch(Instruction instr) {
    if (!ooo_engine_->can_accept()) {
        return EmulatorError::internal("Instruction window full");
    }

    InstructionId instr_id = instr.id;

    emit_event(SimulationEvent::instruction_fetch(instr, current_cycle_));
    pipeline_tracker_->record_fetch(instr, current_cycle_);
    stats_.record_dispatch(instr.id, current_cycle_);
    trace_.record_dispatch(instr, current_cycle_);
    pipeline_tracker_->record_dispatch(instr_id, current_cycle_);

    auto dep_result = ooo_engine_->dispatch(std::move(instr));
    if (dep_result.has_error()) return dep_result.error();
    auto dependencies = std::move(dep_result.value());

    for (const auto& dep : dependencies) {
        pipeline_tracker_->add_dependency(instr_id, dep.producer, dep.is_memory);
        emit_event(SimulationEvent::dependency(instr_id, dep.producer, dep.is_memory));
    }

    return {};
}

Result<PerformanceMetrics> SimulationEngine::run(
    std::function<std::optional<Result<Instruction>>()> next_instr)
{
    return run_with_limit(std::move(next_instr), 1'000'000'000ULL);
}

Result<PerformanceMetrics> SimulationEngine::run_with_limit(
    std::function<std::optional<Result<Instruction>>()> next_instr,
    uint64_t max_cycles)
{
    running_ = true;
    uint64_t start_cycle = current_cycle_;
    uint64_t stall_cycles = 0;

    emit_event(SimulationEvent::simulation_start(current_cycle_));

    while (running_) {
        uint64_t committed_before = committed_count_;

        emit_event(SimulationEvent::cycle_boundary(current_cycle_, committed_count_));

        fetch_dispatch(next_instr);
        ooo_engine_->cycle_tick();
        { auto r = execute(); if (r.has_error()) { running_ = false; return r.error(); } }
        { auto r = complete_memory(); if (r.has_error()) { running_ = false; return r.error(); } }
        { auto r = commit(); if (r.has_error()) { running_ = false; return r.error(); } }
        advance_cycle();

        if (should_stop()) {
            running_ = false;
        }

        // Track stall cycles
        if (committed_count_ == committed_before) {
            stall_cycles++;
        } else {
            stall_cycles = 0;
        }

        // Safety check for infinite loops
        if (stall_cycles > 10000) {
            std::fprintf(stderr, "Warning: No progress for %llu cycles, stopping\n",
                         static_cast<unsigned long long>(stall_cycles));
            running_ = false;
        }

        // Safety check for absolute cycle limit
        if (current_cycle_ - start_cycle >= max_cycles) {
            std::fprintf(stderr, "Warning: Cycle limit reached (%llu)\n",
                         static_cast<unsigned long long>(max_cycles));
            running_ = false;
        }
    }

    emit_event(SimulationEvent::simulation_end(current_cycle_, committed_count_));

    return get_metrics();
}

void SimulationEngine::fetch_dispatch(
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

Result<void> SimulationEngine::execute() {
    auto ready = ooo_engine_->get_ready_instructions();

    for (auto& [id, instr] : ready) {
        ooo_engine_->mark_executing(id);
        stats_.record_issue(id, current_cycle_);
        trace_.record_issue(id, current_cycle_);

        SimExecutionUnit unit = sim_execution_unit_from_opcode(instr.opcode_type);
        emit_event(SimulationEvent::instruction_issue(id, current_cycle_, unit));
        pipeline_tracker_->record_issue(id, current_cycle_);
        emit_event(SimulationEvent::instruction_execute_start(id, current_cycle_));

        if (is_memory_op(instr.opcode_type)) {
            if (instr.mem_access.has_value()) {
                auto mem_result = handle_memory_op(id, instr.mem_access.value());
                if (mem_result.has_error()) return mem_result.error();
            } else {
                uint64_t complete_cycle = current_cycle_ + 1;
                ooo_engine_->mark_completed(id, complete_cycle);
                pipeline_tracker_->record_complete(id, complete_cycle);
            }
        } else {
            // Compute instruction - complete after latency
            uint64_t lat = instr.instr_latency();
            uint64_t complete_cycle = current_cycle_ + lat;
            ooo_engine_->mark_completed(id, complete_cycle);

            pipeline_tracker_->record_execute(id, current_cycle_, complete_cycle);
            pipeline_tracker_->record_complete(id, complete_cycle);
            emit_event(SimulationEvent::instruction_execute_end(id, complete_cycle));
        }
    }

    return {};
}

Result<void> SimulationEngine::handle_memory_op(InstructionId id, const MemAccess& access) {
    MemoryRequest request;
    if (access.is_load) {
        request = memory_->load(id, access);
    } else {
        request = memory_->store(id, access);
    }

    uint64_t complete_cycle = request.complete_cycle.value_or(current_cycle_ + 1);
    ooo_engine_->mark_completed(id, complete_cycle);

    uint64_t lat = complete_cycle - current_cycle_;

    pipeline_tracker_->record_memory(id, current_cycle_, complete_cycle);
    pipeline_tracker_->record_complete(id, complete_cycle);

    uint8_t hit_level = 0;
    if (lat <= 4) hit_level = 1;
    else if (lat <= 12) hit_level = 2;

    emit_event(SimulationEvent::memory_access(id, access.addr, access.size,
                                               access.is_load, lat, hit_level));
    emit_event(SimulationEvent::memory_complete(id, complete_cycle));

    if (access.is_load) {
        stats_.record_load(access.size, lat);
        stats_.record_l1_access(true);
    } else {
        stats_.record_store(access.size, 1);
    }

    return {};
}

Result<void> SimulationEngine::complete_memory() {
    // In this simplified model, all memory operations complete immediately
    return {};
}

Result<void> SimulationEngine::commit() {
    auto commit_candidates = ooo_engine_->get_commit_candidates();

    for (const auto& instr : commit_candidates) {
        InstructionId id = instr.id;
        ooo_engine_->commit(id);
        committed_count_++;

        pipeline_tracker_->record_retire(id, current_cycle_);
        emit_event(SimulationEvent::instruction_retire(id, current_cycle_, committed_count_));

        stats_.record_commit(instr, current_cycle_);
        trace_.record_commit(id, current_cycle_);
    }

    return {};
}

void SimulationEngine::advance_cycle() {
    current_cycle_++;
    ooo_engine_->advance_cycle();
    memory_->advance_cycle();
    stats_.record_cycles(1);
}

bool SimulationEngine::should_stop() const {
    return ooo_engine_->is_empty();
}

void SimulationEngine::stop() {
    running_ = false;
}

Result<void> SimulationEngine::reset() {
    auto ooo_result = OoOEngine::create(config_);
    if (ooo_result.has_error()) return ooo_result.error();
    ooo_engine_ = std::move(ooo_result.value());

    auto mem_result = MemorySubsystem::create(config_);
    if (mem_result.has_error()) return mem_result.error();
    memory_ = std::move(mem_result.value());
    stats_.reset();
    trace_.clear();
    pipeline_tracker_->clear();
    current_cycle_ = 0;
    committed_count_ = 0;
    running_ = false;
    return {};
}

PerformanceMetrics SimulationEngine::get_metrics() const {
    return stats_.get_metrics();
}

const StatsCollector& SimulationEngine::stats() const { return stats_; }
StatsCollector& SimulationEngine::stats_mut() { return stats_; }

const TraceOutput& SimulationEngine::trace() const { return trace_; }
TraceOutput& SimulationEngine::trace_mut() { return trace_; }

const MemorySubsystem& SimulationEngine::memory() const { return *memory_; }

const OoOEngine& SimulationEngine::ooo_engine() const { return *ooo_engine_; }

void SimulationEngine::print_summary() const {
    auto metrics = get_metrics();
    std::cout << metrics.summary() << std::endl;
}

} // namespace arm_cpu
