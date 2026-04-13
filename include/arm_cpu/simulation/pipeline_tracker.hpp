#pragma once

/// @file pipeline_tracker.hpp
/// @brief Pipeline stage tracker for simulation events.
///
/// Records pipeline stage timing for each instruction based on
/// simulation events, producing Konata-compatible visualization data.
///
/// Ported from Rust src/simulation/tracker.rs.

#include "arm_cpu/types.hpp"

#include <cstdint>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace arm_cpu {

// Forward declarations
struct StageInfo;

// =====================================================================
// StageTiming — pipeline stage timing for a single instruction
// =====================================================================
struct StageTiming {
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
    std::optional<uint64_t> execute_start;
    std::optional<uint64_t> execute_end;
    std::optional<uint64_t> memory_start;
    std::optional<uint64_t> memory_end;
    std::optional<uint64_t> complete_cycle;
    std::optional<uint64_t> retire_cycle;

    StageTiming() = default;

    void record_fetch(uint64_t start, uint64_t end);
    void record_decode(uint64_t start, uint64_t end);
    void record_rename(uint64_t start, uint64_t end);
    void record_dispatch(uint64_t start, uint64_t end);
    void record_issue(uint64_t start, uint64_t end);
    void record_execute(uint64_t start, uint64_t end);
    void record_memory(uint64_t start, uint64_t end);
    void record_complete(uint64_t cycle);
    void record_retire(uint64_t cycle);

    /// Convert to Konata-style stage list
    std::vector<StageInfo> to_stages() const;
};

// =====================================================================
// StageInfo — information about a single pipeline stage
// =====================================================================
struct StageInfo {
    std::string name;  // F, Dc, Rn, Ds, Is, Ex, Me, Cm, Rt
    uint64_t start_cycle = 0;
    uint64_t end_cycle = 0;
};

// Forward declaration -- DependencyType is defined in visualization_state.hpp
// or can be defined here if visualization_state.hpp is not included.
#ifndef ARM_CPU_DEPENDENCY_TYPE_DEFINED
enum class DependencyType : uint8_t {
    Register,
    Memory,
};
#define ARM_CPU_DEPENDENCY_TYPE_DEFINED 1
#endif

// =====================================================================
// DependencyRef — dependency reference for Konata output
// =====================================================================
struct DependencyRef {
    uint64_t producer_id = 0;
    DependencyType dep_type = DependencyType::Register;
};

// =====================================================================
// TrackedInstruction — instruction info for output
// =====================================================================
struct TrackedInstruction {
    uint64_t viz_id = 0;
    uint64_t program_id = 0;
    uint64_t pc = 0;
    std::string disasm;
    StageTiming timing;
    std::vector<uint16_t> src_regs;
    std::vector<uint16_t> dst_regs;
    bool is_memory = false;
    std::optional<uint64_t> mem_addr;
    std::vector<DependencyRef> dependencies;
};

// =====================================================================
// PipelineTracker — records instruction timing from simulation events
// =====================================================================
class PipelineTracker {
public:
    PipelineTracker();
    explicit PipelineTracker(std::size_t max_completed);

    /// Record instruction fetch
    void record_fetch(const Instruction& instr, uint64_t cycle);

    /// Record instruction decode
    void record_decode(InstructionId id, uint64_t start, uint64_t end);

    /// Record instruction rename
    void record_rename(InstructionId id, uint64_t start, uint64_t end);

    /// Record instruction dispatch
    void record_dispatch(InstructionId id, uint64_t cycle);

    /// Record instruction issue
    void record_issue(InstructionId id, uint64_t cycle);

    /// Record instruction execute
    void record_execute(InstructionId id, uint64_t start, uint64_t end);

    /// Record memory access
    void record_memory(InstructionId id, uint64_t start, uint64_t end);

    /// Record instruction complete
    void record_complete(InstructionId id, uint64_t cycle);

    /// Record instruction retire
    void record_retire(InstructionId id, uint64_t cycle);

    /// Add a dependency
    void add_dependency(InstructionId consumer, InstructionId producer, bool is_memory);

    /// Get timing for an instruction
    const StageTiming* get_timing(InstructionId id) const;

    /// Get visualization ID for an instruction
    std::optional<uint64_t> get_viz_id(InstructionId id) const;

    /// Get the retire counter
    uint64_t retire_count() const;

    /// Get number of tracked instructions
    std::size_t len() const;

    /// Check if empty
    bool is_empty() const;

    /// Clear all tracking data
    void clear();

    /// Export all tracked instructions with their stages
    std::vector<TrackedInstruction> export_instructions() const;

private:
    uint64_t get_or_assign_viz_id(InstructionId id);

    std::unordered_map<InstructionId, StageTiming, InstructionId::Hash> timings_;
    std::unordered_map<InstructionId, TrackedInstruction, InstructionId::Hash> instructions_;
    std::deque<InstructionId> order_;
    std::unordered_map<InstructionId, uint64_t, InstructionId::Hash> viz_id_map_;
    uint64_t next_viz_id_ = 0;
    uint64_t retire_counter_ = 0;
    std::size_t max_completed_;
    std::deque<InstructionId> completed_;
    std::unordered_map<InstructionId, std::vector<DependencyRef>, InstructionId::Hash> dependencies_;
};

} // namespace arm_cpu
