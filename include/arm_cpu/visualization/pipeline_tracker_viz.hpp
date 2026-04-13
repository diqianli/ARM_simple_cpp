#pragma once

/// @file pipeline_tracker_viz.hpp
/// @brief Pipeline stage tracker for Konata visualization.
///
/// Tracks detailed pipeline stage timing for each instruction,
/// enabling generation of Konata-compatible visualization data.

#include "arm_cpu/visualization/konata_format.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

namespace arm_cpu {

// Forward declarations
struct WindowEntry;

/// Dependency info for pipeline tracker
struct TrackerDependencyInfo {
    uint64_t producer_id = 0;
    KonataDependencyType dep_type = KonataDependencyType::Register;
};

/// Pipeline stage timing for a single instruction
struct StageTimingViz {
    std::optional<uint64_t> fetch_start;
    std::optional<uint64_t> fetch_end;
    std::optional<uint64_t> decode_start;
    std::optional<uint64_t> decode_end;
    std::optional<uint64_t> rename_start;
    std::optional<uint64_t> rename_end;
    std::optional<uint64_t> dispatch_start;
    std::optional<uint64_t> dispatch_end;
    std::optional<uint64_t> issue_start;
    std::optional<uint64_t> issue_end;
    std::optional<uint64_t> ready_cycle;
    std::optional<uint64_t> execute_start;
    std::optional<uint64_t> execute_end;
    std::optional<uint64_t> memory_start;
    std::optional<uint64_t> memory_end;
    std::optional<uint64_t> complete_cycle;
    std::optional<uint64_t> retire_cycle;
    std::optional<CacheAccessInfoViz> cache_access;

    /// Convert to sequential Konata stages with overlap prevention and DI gap annotations.
    std::vector<KonataStage> to_stages() const;
};

// =====================================================================
// PipelineTrackerViz
// =====================================================================
class PipelineTrackerViz {
public:
    PipelineTrackerViz() = default;
    explicit PipelineTrackerViz(std::size_t fetch_width);

    /// Record an instruction being fetched.
    void record_fetch(const Instruction& instr, uint64_t cycle);

    /// Record decode stage.
    void record_decode(InstructionId id, uint64_t start_cycle, uint64_t end_cycle);

    /// Record rename stage.
    void record_rename(InstructionId id, uint64_t start_cycle, uint64_t end_cycle);

    /// Record dispatch stage (also records decode/rename if needed).
    void record_dispatch(InstructionId id, uint64_t cycle);

    /// Record ready (all operands available).
    void record_ready(InstructionId id, uint64_t cycle);

    /// Record issue stage.
    void record_issue(InstructionId id, uint64_t cycle);

    /// Record execute start.
    void record_execute_start(InstructionId id, uint64_t cycle);

    /// Record execute end.
    void record_execute_end(InstructionId id, uint64_t cycle);

    /// Record memory operation.
    void record_memory(InstructionId id, uint64_t start_cycle, uint64_t end_cycle);

    /// Record cache access info.
    void record_cache_access(InstructionId id, const CacheAccessInfoViz& info);

    /// Record completion.
    void record_complete(InstructionId id, uint64_t cycle);

    /// Record retire/commit.
    void record_retire(InstructionId id, uint64_t cycle);

    /// Add a dependency between instructions.
    void add_dependency(InstructionId consumer, InstructionId producer, bool is_memory);

    /// Convert tracked data to Konata operations.
    std::vector<KonataOp> to_konata_ops(
        const std::vector<const WindowEntry*>& entries, uint64_t current_cycle) const;

    /// Generate a complete Konata snapshot.
    KonataSnapshot to_snapshot(
        const std::vector<const WindowEntry*>& entries,
        uint64_t cycle, uint64_t committed_count) const;

    /// Export all tracked instructions as Konata ops.
    std::vector<KonataOp> export_all_konata_ops() const;

    /// Clear all tracking data.
    void clear();

    std::size_t size() const { return timings_.size(); }
    bool empty() const { return timings_.empty(); }

    const StageTimingViz* get_timing(InstructionId id) const;
    const std::vector<TrackerDependencyInfo>* get_dependencies(InstructionId id) const;
    const CacheAccessInfoViz* get_cache_access(InstructionId id) const;

private:
    uint64_t get_or_assign_viz_id(InstructionId id);

    std::unordered_map<InstructionId, StageTimingViz, InstructionId::Hash> timings_;
    std::deque<InstructionId> order_;
    std::unordered_map<InstructionId, uint64_t, InstructionId::Hash> viz_id_map_;
    uint64_t next_viz_id_ = 0;
    uint64_t retire_counter_ = 0;

    std::deque<InstructionId> completed_;
    std::size_t max_completed_ = 1000;

    std::unordered_map<InstructionId, std::vector<TrackerDependencyInfo>, InstructionId::Hash> dependencies_;
    std::unordered_map<InstructionId, std::string, InstructionId::Hash> disasm_map_;
    std::unordered_map<InstructionId, std::vector<uint16_t>, InstructionId::Hash> src_regs_map_;
    std::unordered_map<InstructionId, std::vector<uint16_t>, InstructionId::Hash> dst_regs_map_;
    std::unordered_map<InstructionId, std::tuple<uint64_t, uint8_t, bool>, InstructionId::Hash> mem_access_map_;
    std::unordered_map<InstructionId, uint64_t, InstructionId::Hash> retire_order_map_;

    std::size_t fetch_width_ = 8;
    std::size_t fetch_count_in_cycle_ = 0;
    uint64_t current_fetch_cycle_ = 0;
};

} // namespace arm_cpu
