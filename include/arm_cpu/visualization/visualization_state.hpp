#pragma once

/// @file visualization_state.hpp
/// @brief Visualization state management for CPU pipeline visualization.

#include "arm_cpu/visualization/pipeline_tracker_viz.hpp"
#include "arm_cpu/simulation/pipeline_tracker.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace arm_cpu {

// Forward declarations
class OoOEngine;
struct PerformanceMetrics;

// =====================================================================
// InstructionStatus - Visualization status enum
// =====================================================================
enum class VizInstructionStatus : uint8_t {
    Waiting,
    Ready,
    Executing,
    Completed,
    Committed,
};

// =====================================================================
// DependencyEdge (uses DependencyType from simulation/pipeline_tracker.hpp)
// =====================================================================
struct DependencyEdge {
    uint64_t from = 0;
    uint64_t to = 0;
    DependencyType dep_type = DependencyType::Register;
};

// =====================================================================
// InstructionSnapshot
// =====================================================================
struct InstructionSnapshot {
    uint64_t id = 0;
    uint64_t pc = 0;
    std::string opcode;
    std::optional<std::string> disasm;
    VizInstructionStatus status = VizInstructionStatus::Waiting;
    std::vector<uint16_t> src_regs;
    std::vector<uint16_t> dst_regs;
    bool is_memory = false;
    std::optional<uint64_t> mem_addr;
    std::optional<uint8_t> mem_size;
    std::optional<bool> is_load;
    std::optional<uint64_t> dispatch_cycle;
    std::optional<uint64_t> issue_cycle;
    std::optional<uint64_t> complete_cycle;
    std::size_t pending_deps = 0;
};

// =====================================================================
// MetricsSnapshot
// =====================================================================
struct MetricsSnapshot {
    double ipc = 0.0;
    uint64_t total_cycles = 0;
    uint64_t total_instructions = 0;
    double l1_hit_rate = 0.0;
    double l2_hit_rate = 0.0;
    double l1_mpki = 0.0;
    double l2_mpki = 0.0;
    double memory_instr_pct = 0.0;
    double avg_load_latency = 0.0;
};

// =====================================================================
// PipelineSnapshot
// =====================================================================
struct PipelineSnapshot {
    std::size_t fetch_count = 0;
    std::size_t dispatch_count = 0;
    std::size_t execute_count = 0;
    std::size_t memory_count = 0;
    std::size_t commit_count = 0;
    std::size_t window_occupancy = 0;
    std::size_t window_capacity = 0;
};

// =====================================================================
// VisualizationSnapshot
// =====================================================================
struct VisualizationSnapshot {
    uint64_t cycle = 0;
    uint64_t committed_count = 0;
    std::vector<InstructionSnapshot> instructions;
    std::vector<DependencyEdge> dependencies;
    MetricsSnapshot metrics;
    PipelineSnapshot pipeline;
};

// =====================================================================
// VisualizationConfig
// =====================================================================
struct VisualizationConfig {
    bool enabled = false;
    uint16_t port = 3000;
    std::size_t max_snapshots = 10000;
    uint32_t animation_speed = 10;

    static VisualizationConfig enabled_config() {
        VisualizationConfig c;
        c.enabled = true;
        return c;
    }

    static VisualizationConfig with_port(uint16_t p) {
        VisualizationConfig c;
        c.enabled = true;
        c.port = p;
        return c;
    }
};

// =====================================================================
// VisualizationState
// =====================================================================
class VisualizationState {
public:
    explicit VisualizationState(VisualizationConfig config);

    bool is_enabled() const { return config_.enabled; }
    uint16_t port() const { return config_.port; }
    void set_cycle(uint64_t cycle) { current_cycle_ = cycle; }
    void set_committed_count(uint64_t count) { committed_count_ = count; }
    void add_dependency(InstructionId from, InstructionId to, DependencyType dep_type);
    void clear_dependencies() { current_dependencies_.clear(); }

    void capture_snapshot(const OoOEngine& engine, const PerformanceMetrics& metrics);
    void capture_konata_snapshot(const OoOEngine& engine);

    const VisualizationSnapshot* latest_snapshot() const;
    const std::deque<VisualizationSnapshot>& snapshots() const { return snapshots_; }
    const VisualizationSnapshot* get_snapshot(uint64_t cycle) const;

    void clear();
    std::size_t snapshot_count() const { return snapshots_.size(); }
    bool has_snapshots() const { return !snapshots_.empty(); }

    PipelineTrackerViz& pipeline_tracker() { return pipeline_tracker_; }
    const PipelineTrackerViz& pipeline_tracker() const { return pipeline_tracker_; }

    const KonataSnapshot* latest_konata_snapshot() const;
    const std::deque<KonataSnapshot>& konata_snapshots() const { return konata_snapshots_; }

    /// Export latest Konata snapshot to a JSON file.
    bool export_konata_to_file(const std::string& path, bool pretty = false) const;

    /// Export all tracked Konata ops to a JSON file.
    bool export_all_konata_to_file(const std::string& path, bool pretty = false) const;

    uint64_t current_cycle() const { return current_cycle_; }
    uint64_t committed_count() const { return committed_count_; }

private:
    std::vector<InstructionSnapshot> collect_instructions(const OoOEngine& engine) const;
    std::vector<DependencyEdge> collect_dependencies(const OoOEngine& engine) const;
    PipelineSnapshot collect_pipeline_info(const OoOEngine& engine) const;

    VisualizationConfig config_;
    std::deque<VisualizationSnapshot> snapshots_;
    uint64_t current_cycle_ = 0;
    uint64_t committed_count_ = 0;
    std::vector<DependencyEdge> current_dependencies_;
    PipelineTrackerViz pipeline_tracker_;
    std::deque<KonataSnapshot> konata_snapshots_;
};

} // namespace arm_cpu
