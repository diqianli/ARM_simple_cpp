/// @file visualization_state.cpp
/// @brief Visualization state implementation.

#include "arm_cpu/visualization/visualization_state.hpp"
#include "arm_cpu/ooo/ooo_engine.hpp"
#include "arm_cpu/stats/stats_collector.hpp"

#include <algorithm>

namespace arm_cpu {

VisualizationState::VisualizationState(VisualizationConfig config)
    : config_(std::move(config))
    , pipeline_tracker_(PipelineTrackerViz{})
{}

void VisualizationState::add_dependency(InstructionId from, InstructionId to, DependencyType dep_type) {
    current_dependencies_.push_back(DependencyEdge{from.value, to.value, dep_type});
}

void VisualizationState::capture_snapshot(const OoOEngine& engine, const PerformanceMetrics& metrics) {
    if (!config_.enabled) return;

    auto instructions = collect_instructions(engine);
    auto dependencies = collect_dependencies(engine);
    auto pipeline = collect_pipeline_info(engine);

    MetricsSnapshot metrics_snap;
    metrics_snap.ipc = metrics.ipc;
    metrics_snap.total_cycles = metrics.total_cycles;
    metrics_snap.total_instructions = metrics.total_instructions;
    metrics_snap.l1_hit_rate = metrics.l1_hit_rate;
    metrics_snap.l2_hit_rate = metrics.l2_hit_rate;
    metrics_snap.l1_mpki = metrics.l1_mpki;
    metrics_snap.l2_mpki = metrics.l2_mpki;
    metrics_snap.memory_instr_pct = metrics.memory_instr_pct;
    metrics_snap.avg_load_latency = metrics.avg_load_latency;

    VisualizationSnapshot snapshot;
    snapshot.cycle = current_cycle_;
    snapshot.committed_count = committed_count_;
    snapshot.instructions = std::move(instructions);
    snapshot.dependencies = std::move(dependencies);
    snapshot.metrics = metrics_snap;
    snapshot.pipeline = pipeline;

    if (snapshots_.size() >= config_.max_snapshots) {
        snapshots_.pop_front();
    }
    snapshots_.push_back(std::move(snapshot));

    capture_konata_snapshot(engine);
}

void VisualizationState::capture_konata_snapshot(const OoOEngine& engine) {
    if (!config_.enabled) return;

    auto entries = engine.get_window_entries();
    auto snapshot = pipeline_tracker_.to_snapshot(entries, current_cycle_, committed_count_);

    if (konata_snapshots_.size() >= config_.max_snapshots) {
        konata_snapshots_.pop_front();
    }
    konata_snapshots_.push_back(std::move(snapshot));
}

const VisualizationSnapshot* VisualizationState::latest_snapshot() const {
    if (snapshots_.empty()) return nullptr;
    return &snapshots_.back();
}

const VisualizationSnapshot* VisualizationState::get_snapshot(uint64_t cycle) const {
    for (const auto& s : snapshots_) {
        if (s.cycle == cycle) return &s;
    }
    return nullptr;
}

void VisualizationState::clear() {
    snapshots_.clear();
    current_cycle_ = 0;
    committed_count_ = 0;
    current_dependencies_.clear();
    pipeline_tracker_.clear();
    konata_snapshots_.clear();
}

const KonataSnapshot* VisualizationState::latest_konata_snapshot() const {
    if (konata_snapshots_.empty()) return nullptr;
    return &konata_snapshots_.back();
}

bool VisualizationState::export_konata_to_file(const std::string& path, bool pretty) const {
    const KonataSnapshot* snap = latest_konata_snapshot();
    if (!snap) return false;

    KonataExport export_data;
    export_data.total_cycles = snap->cycle;
    export_data.total_instructions = snap->committed_count;
    export_data.ops = snap->ops;
    export_data.ops_count = snap->ops.size();
    return export_data.write_to_file(path, pretty);
}

bool VisualizationState::export_all_konata_to_file(const std::string& path, bool pretty) const {
    auto ops = pipeline_tracker_.export_all_konata_ops();

    KonataExport export_data;
    export_data.total_cycles = current_cycle_;
    export_data.total_instructions = committed_count_;
    export_data.ops = std::move(ops);
    export_data.ops_count = export_data.ops.size();
    return export_data.write_to_file(path, pretty);
}

std::vector<InstructionSnapshot> VisualizationState::collect_instructions(const OoOEngine& engine) const {
    std::vector<InstructionSnapshot> instructions;
    auto entries = engine.get_window_entries();

    for (const auto* entry : entries) {
        InstructionSnapshot snap;
        snap.id = entry->instruction.id.value;
        snap.pc = entry->instruction.pc;
        snap.opcode = std::string(opcode_to_string(entry->instruction.opcode_type));
        snap.is_memory = entry->instruction.mem_access.has_value();
        snap.mem_addr = entry->instruction.mem_access ? std::optional<uint64_t>(entry->instruction.mem_access->addr) : std::nullopt;
        snap.mem_size = entry->instruction.mem_access ? std::optional<uint8_t>(entry->instruction.mem_access->size) : std::nullopt;
        snap.is_load = entry->instruction.mem_access ? std::optional<bool>(entry->instruction.mem_access->is_load) : std::nullopt;
        snap.disasm = entry->instruction.disasm;
        snap.dispatch_cycle = entry->dispatch_cycle;
        snap.issue_cycle = entry->issue_cycle;
        snap.complete_cycle = entry->complete_cycle;
        snap.pending_deps = engine.dependency_tracker().pending_count(entry->instruction.id);

        // Map InstrStatus -> VizInstructionStatus
        switch (entry->status) {
            case InstrStatus::Waiting:   snap.status = VizInstructionStatus::Waiting; break;
            case InstrStatus::Ready:     snap.status = VizInstructionStatus::Ready; break;
            case InstrStatus::Executing: snap.status = VizInstructionStatus::Executing; break;
            case InstrStatus::Completed: snap.status = VizInstructionStatus::Completed; break;
            case InstrStatus::Committed: snap.status = VizInstructionStatus::Committed; break;
        }

        for (const auto& r : entry->instruction.src_regs) snap.src_regs.push_back(r.value);
        for (const auto& r : entry->instruction.dst_regs) snap.dst_regs.push_back(r.value);

        instructions.push_back(std::move(snap));
    }

    return instructions;
}

std::vector<DependencyEdge> VisualizationState::collect_dependencies(const OoOEngine& engine) const {
    std::vector<DependencyEdge> edges;
    auto all_deps = engine.dependency_tracker().get_all_dependencies();

    for (const auto& dep : all_deps) {
        edges.push_back(DependencyEdge{
            std::get<0>(dep).value,
            std::get<1>(dep).value,
            std::get<2>(dep) ? DependencyType::Memory : DependencyType::Register
        });
    }

    return edges;
}

PipelineSnapshot VisualizationState::collect_pipeline_info(const OoOEngine& engine) const {
    PipelineSnapshot snap;
    auto stats = engine.get_stats();
    auto [waiting, ready, executing, completed] = engine.status_counts();

    snap.dispatch_count = waiting;
    snap.execute_count = ready + executing;
    snap.commit_count = completed;
    snap.window_occupancy = stats.window_occupancy;
    snap.window_capacity = stats.window_capacity;

    return snap;
}

} // namespace arm_cpu
