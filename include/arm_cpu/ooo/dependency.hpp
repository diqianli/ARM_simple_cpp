#pragma once

/// @file dependency.hpp
/// @brief Dependency tracking for out-of-order execution.

#include "arm_cpu/types.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace arm_cpu {

struct DependencyInfo {
    InstructionId producer;
    bool is_memory;
};

struct DependencyStats {
    std::size_t register_producers = 0;
    std::size_t pending_instructions = 0;
    std::size_t total_dependents = 0;
};

class DependencyTracker {
public:
    DependencyTracker() = default;

    /// Register an instruction and set up dependencies. Returns dependency info for visualization.
    std::vector<DependencyInfo> register_instruction(const Instruction& instr, InstructionId id, uint64_t current_cycle);

    /// Check if an instruction is ready (all dependencies resolved)
    bool is_ready(InstructionId id) const;

    /// Release dependencies when an instruction completes
    void release_dependencies(InstructionId id);

    /// Get all instructions that depend on the given instruction
    std::vector<InstructionId> get_dependents(InstructionId id) const;

    /// Get pending dependency count
    std::size_t pending_count(InstructionId id) const;

    /// Clear all tracking state
    void clear();

    /// Get statistics
    DependencyStats get_stats() const;

    /// Get all current dependencies for visualization
    std::vector<std::tuple<InstructionId, InstructionId, bool>> get_all_dependencies() const;

private:
    void add_dependency(InstructionId producer, InstructionId consumer);
    void add_memory_dependency(InstructionId producer, InstructionId consumer);

    std::unordered_map<Reg, InstructionId, Reg::Hash> register_producers_;
    std::unordered_map<InstructionId, std::size_t, InstructionId::Hash> pending_dependencies_;
    std::unordered_map<InstructionId, std::vector<InstructionId>, InstructionId::Hash> dependents_;
    std::unordered_map<uint64_t, InstructionId> memory_producers_;
    std::unordered_map<InstructionId, std::vector<InstructionId>, InstructionId::Hash> memory_dependents_;
    std::unordered_set<InstructionId, InstructionId::Hash> completed_instructions_;
};

} // namespace arm_cpu
