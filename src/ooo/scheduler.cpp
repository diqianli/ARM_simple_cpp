/// @file scheduler.cpp
/// @brief Instruction scheduler implementation.

#include "arm_cpu/ooo/scheduler.hpp"

namespace arm_cpu {

ExecutionUnit execution_unit_for(const Instruction& instr) {
    switch (instr.opcode_type) {
        case OpcodeType::Add: case OpcodeType::Sub:
        case OpcodeType::And: case OpcodeType::Orr: case OpcodeType::Eor:
        case OpcodeType::Lsl: case OpcodeType::Lsr: case OpcodeType::Asr:
        case OpcodeType::Mov: case OpcodeType::Cmp: case OpcodeType::Shift:
        case OpcodeType::Other: case OpcodeType::Nop:
        case OpcodeType::Msr: case OpcodeType::Mrs: case OpcodeType::Sys:
        case OpcodeType::Dmb: case OpcodeType::Dsb: case OpcodeType::Isb:
        case OpcodeType::Eret: case OpcodeType::Yield: case OpcodeType::Adr:
            return ExecutionUnit::IntAlu;

        case OpcodeType::Mul: case OpcodeType::Div:
            return ExecutionUnit::IntMul;

        case OpcodeType::Load: case OpcodeType::LoadPair: case OpcodeType::Vld:
            return ExecutionUnit::Load;

        case OpcodeType::Store: case OpcodeType::StorePair: case OpcodeType::Vst:
            return ExecutionUnit::Store;

        case OpcodeType::Branch: case OpcodeType::BranchCond: case OpcodeType::BranchReg:
            return ExecutionUnit::Branch;

        case OpcodeType::Fadd: case OpcodeType::Fsub: case OpcodeType::Fmul: case OpcodeType::Fdiv:
        case OpcodeType::Vadd: case OpcodeType::Vsub: case OpcodeType::Vmul:
        case OpcodeType::Vmla: case OpcodeType::Vmls: case OpcodeType::Vdup: case OpcodeType::Vmov:
        case OpcodeType::Fmadd: case OpcodeType::Fmsub: case OpcodeType::Fnmadd: case OpcodeType::Fnmsub:
        case OpcodeType::Fcvt:
            return ExecutionUnit::FpSimd;

        case OpcodeType::DcZva: case OpcodeType::DcCivac: case OpcodeType::DcCvac:
        case OpcodeType::DcCsw: case OpcodeType::IcIvau: case OpcodeType::IcIallu:
        case OpcodeType::IcIalluis:
            return ExecutionUnit::System;

        case OpcodeType::Aesd: case OpcodeType::Aese: case OpcodeType::Aesimc: case OpcodeType::Aesmc:
        case OpcodeType::Sha1H: case OpcodeType::Sha256H: case OpcodeType::Sha512H:
        case OpcodeType::Pmull:
            return ExecutionUnit::Crypto;
    }
    return ExecutionUnit::IntAlu;
}

std::size_t execution_unit_count(ExecutionUnit unit) {
    switch (unit) {
        case ExecutionUnit::IntAlu: return 4;
        case ExecutionUnit::IntMul: return 1;
        case ExecutionUnit::Load: return 2;
        case ExecutionUnit::Store: return 1;
        case ExecutionUnit::Branch: return 1;
        case ExecutionUnit::FpSimd: return 2;
        case ExecutionUnit::System: return 1;
        case ExecutionUnit::Crypto: return 1;
    }
    return 1;
}

// --- ExecutionPipeline ---

ExecutionPipeline::ExecutionPipeline(ExecutionUnit unit)
    : unit_type(unit), unit_count(execution_unit_count(unit)) {}

bool ExecutionPipeline::has_capacity() const { return executing.size() < unit_count; }

bool ExecutionPipeline::issue(InstructionId id, uint64_t complete_cycle) {
    if (!has_capacity()) return false;
    executing.emplace_back(id, complete_cycle);
    return true;
}

std::vector<InstructionId> ExecutionPipeline::complete_by(uint64_t cycle) {
    std::vector<InstructionId> completed;
    std::vector<std::pair<InstructionId, uint64_t>> remaining;
    for (auto& [id, complete] : executing) {
        if (complete <= cycle) {
            completed.push_back(id);
        } else {
            remaining.emplace_back(id, complete);
        }
    }
    executing = std::move(remaining);
    return completed;
}

std::size_t ExecutionPipeline::executing_count() const { return executing.size(); }

void ExecutionPipeline::clear() { executing.clear(); }

// --- Scheduler ---

Scheduler::Scheduler(std::size_t issue_width, std::size_t commit_width)
    : issue_width_(issue_width), commit_width_(commit_width) {}

void Scheduler::add_ready(InstructionId id) {
    for (auto existing : ready_queue_) {
        if (existing == id) return;
    }
    ready_queue_.push_back(id);
}

std::vector<std::pair<InstructionId, Instruction>>
Scheduler::get_ready(InstructionWindow& window) {
    std::vector<std::pair<InstructionId, Instruction>> result;
    result.reserve(issue_width_);
    std::size_t issued = 0;

    while (issued < issue_width_ && !ready_queue_.empty()) {
        auto id = ready_queue_.front();
        ready_queue_.pop_front();

        auto* entry = window.get_entry(id);
        if (entry && entry->status == InstrStatus::Ready) {
            result.emplace_back(id, entry->instruction);
            window.mark_executing(id);
            issued++;
        }
    }
    return result;
}

std::size_t Scheduler::ready_count() const { return ready_queue_.size(); }
bool Scheduler::has_ready() const { return !ready_queue_.empty(); }
void Scheduler::clear() { ready_queue_.clear(); }

std::vector<InstructionId>
Scheduler::get_commit_candidates(const InstructionWindow& window, uint64_t next_commit_id) const {
    std::vector<InstructionId> candidates;
    for (std::size_t i = 0; i < commit_width_; i++) {
        auto id = InstructionId(next_commit_id + i);
        auto* entry = window.get_entry(id);
        if (entry && entry->status == InstrStatus::Completed) {
            candidates.push_back(id);
        } else {
            break;
        }
    }
    return candidates;
}

} // namespace arm_cpu
