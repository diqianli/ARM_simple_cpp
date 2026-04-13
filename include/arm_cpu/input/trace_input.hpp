#pragma once

/// @file trace_input.hpp
/// @brief In-memory instruction input for programmatic use (API interface).
///
/// Analogous to Rust's `input::api::TraceInput` and `InstructionBuilder`.

#include "arm_cpu/input/instruction_source.hpp"
#include "arm_cpu/types.hpp"

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace arm_cpu {

/// In-memory trace input for programmatic use.
///
/// Implements InstructionSource so it can be used interchangeably with
/// file-based parsers.
class TraceInput final : public InstructionSource {
public:
    TraceInput() = default;

    /// Create with pre-allocated capacity.
    explicit TraceInput(std::size_t /*capacity*/) {
        // std::deque does not have reserve; capacity hint is ignored.
    }

    /// Create from an existing vector of instructions.
    explicit TraceInput(std::vector<Instruction> instructions)
        : instructions_(std::make_move_iterator(instructions.begin()),
                        std::make_move_iterator(instructions.end()))
        , total_count_(instructions_.size()) {}

    /// Add a single instruction.
    void push(Instruction instr) {
        instructions_.push_back(std::move(instr));
        ++total_count_;
    }

    /// Add multiple instructions.
    void extend(std::vector<Instruction> instrs) {
        instructions_.insert(instructions_.end(),
                             std::make_move_iterator(instrs.begin()),
                             std::make_move_iterator(instrs.end()));
        total_count_ = instructions_.size();
    }

    /// Number of remaining instructions.
    std::size_t remaining() const noexcept { return instructions_.size(); }

    /// Check if empty.
    bool is_empty() const noexcept { return instructions_.empty(); }

    /// Clear all instructions.
    void clear() {
        instructions_.clear();
        current_id_ = 0;
        total_count_ = 0;
    }

    /// Peek at the next instruction without consuming it.
    const Instruction* peek() const {
        return instructions_.empty() ? nullptr : &instructions_.front();
    }

    // -- InstructionSource interface --

    std::optional<std::size_t> total_count() const override {
        return total_count_;
    }

    Result<void> reset() override {
        return Result<void>(EmulatorError::internal(
            "Cannot reset in-memory trace input"));
    }

protected:
    Result<std::optional<Instruction>> next_impl() override {
        if (instructions_.empty()) {
            return Result<std::optional<Instruction>>(std::optional<Instruction>{std::nullopt});
        }
        auto instr = std::move(instructions_.front());
        instructions_.pop_front();
        return Result<std::optional<Instruction>>(std::optional<Instruction>{std::move(instr)});
    }

private:
    std::deque<Instruction> instructions_;
    uint64_t current_id_ = 0;
    std::size_t total_count_ = 0;
};

/// Builder for creating instructions and pushing them into a TraceInput.
class InstructionBuilder {
public:
    InstructionBuilder(TraceInput& input, uint64_t pc, OpcodeType opcode_type)
        : input_(&input)
        , pc_(pc)
        , raw_opcode_(0)
        , opcode_type_(opcode_type) {}

    /// Set the raw opcode encoding.
    InstructionBuilder& raw_opcode(uint32_t raw) {
        raw_opcode_ = raw;
        return *this;
    }

    /// Add a source register.
    InstructionBuilder& src_reg(Reg reg) {
        if (!contains(src_regs_, reg)) {
            src_regs_.push_back(reg);
        }
        return *this;
    }

    /// Add a destination register.
    InstructionBuilder& dst_reg(Reg reg) {
        if (!contains(dst_regs_, reg)) {
            dst_regs_.push_back(reg);
        }
        return *this;
    }

    /// Set memory access info.
    InstructionBuilder& mem_access(uint64_t addr, uint8_t size, bool is_load) {
        mem_access_ = MemAccess{addr, size, is_load};
        return *this;
    }

    /// Set branch info.
    InstructionBuilder& branch(uint64_t target, bool is_conditional, bool is_taken) {
        branch_info_ = BranchInfo{is_conditional, target, is_taken};
        return *this;
    }

    /// Set disassembly text.
    InstructionBuilder& disasm(std::string text) {
        disasm_ = std::move(text);
        return *this;
    }

    /// Build and add the instruction to the trace. Returns the assigned ID.
    InstructionId build() {
        InstructionId id(next_id_);
        ++next_id_;

        Instruction instr(id, pc_, raw_opcode_, opcode_type_);
        for (const auto& r : src_regs_) {
            instr.src_regs.push_back(r);
        }
        for (const auto& r : dst_regs_) {
            instr.dst_regs.push_back(r);
        }
        instr.mem_access = mem_access_;
        instr.branch_info = branch_info_;
        instr.disasm = disasm_;

        input_->push(std::move(instr));
        return id;
    }

private:
    static bool contains(const std::vector<Reg>& regs, Reg r) {
        for (const auto& reg : regs) {
            if (reg == r) return true;
        }
        return false;
    }

    TraceInput* input_;
    uint64_t pc_;
    uint32_t raw_opcode_;
    OpcodeType opcode_type_;
    std::vector<Reg> src_regs_;
    std::vector<Reg> dst_regs_;
    std::optional<MemAccess> mem_access_;
    std::optional<BranchInfo> branch_info_;
    std::optional<std::string> disasm_;
    uint64_t next_id_ = 0;
};

/// Helper functions for creating common instruction patterns.
/// Mirrors Rust's `input::api::helpers` module.
namespace helpers {

/// Create a simple compute instruction.
Instruction compute(uint64_t pc, OpcodeType opcode_type,
                    const std::vector<Reg>& srcs, Reg dst);

/// Create a load instruction.
Instruction load(uint64_t pc, uint64_t addr, Reg dst, uint8_t size);

/// Create a store instruction.
Instruction store(uint64_t pc, uint64_t addr, Reg src, uint8_t size);

/// Create an unconditional branch.
Instruction branch(uint64_t pc, uint64_t target);

/// Create a conditional branch.
Instruction branch_cond(uint64_t pc, uint64_t target, bool taken);

/// Create a NOP instruction.
Instruction nop(uint64_t pc);

} // namespace helpers

} // namespace arm_cpu
