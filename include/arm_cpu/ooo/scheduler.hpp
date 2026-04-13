#pragma once

/// @file scheduler.hpp
/// @brief Instruction scheduler for out-of-order execution.

#include "arm_cpu/ooo/window.hpp"
#include "arm_cpu/types.hpp"

#include <cstddef>
#include <deque>
#include <vector>

namespace arm_cpu {

enum class ExecutionUnit : uint8_t {
    IntAlu, IntMul, Load, Store, Branch, FpSimd, System, Crypto,
};

/// Determine execution unit for an instruction
ExecutionUnit execution_unit_for(const Instruction& instr);

/// Get number of parallel units of a given type
std::size_t execution_unit_count(ExecutionUnit unit);

struct ExecutionPipeline {
    ExecutionUnit unit_type;
    std::size_t unit_count;
    std::vector<std::pair<InstructionId, uint64_t>> executing;

    explicit ExecutionPipeline(ExecutionUnit unit);

    bool has_capacity() const;
    bool issue(InstructionId id, uint64_t complete_cycle);
    std::vector<InstructionId> complete_by(uint64_t cycle);
    std::size_t executing_count() const;
    void clear();
};

class Scheduler {
public:
    Scheduler(std::size_t issue_width, std::size_t commit_width);

    void add_ready(InstructionId id);
    std::vector<std::pair<InstructionId, Instruction>> get_ready(InstructionWindow& window);
    std::size_t ready_count() const;
    bool has_ready() const;
    void clear();

    std::vector<InstructionId> get_commit_candidates(const InstructionWindow& window, uint64_t next_commit_id) const;

private:
    std::size_t issue_width_;
    std::size_t commit_width_;
    std::deque<InstructionId> ready_queue_;
};

} // namespace arm_cpu
