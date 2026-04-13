/// @file trace_input.cpp
/// @brief Implementation of in-memory TraceInput and helper functions.

#include "arm_cpu/input/trace_input.hpp"
#include "arm_cpu/types.hpp"

namespace arm_cpu {

// =====================================================================
// helpers
// =====================================================================

namespace helpers {

Instruction compute(uint64_t pc, OpcodeType opcode_type,
                    const std::vector<Reg>& srcs, Reg dst) {
    Instruction instr(InstructionId(0), pc, 0, opcode_type);
    for (const auto& src : srcs) {
        instr.src_regs.push_back(src);
    }
    instr.dst_regs.push_back(dst);
    return instr;
}

Instruction load(uint64_t pc, uint64_t addr, Reg dst, uint8_t size) {
    return Instruction(InstructionId(0), pc, 0, OpcodeType::Load)
        .with_dst_reg(dst)
        .with_mem_access(addr, size, true);
}

Instruction store(uint64_t pc, uint64_t addr, Reg src, uint8_t size) {
    return Instruction(InstructionId(0), pc, 0, OpcodeType::Store)
        .with_src_reg(src)
        .with_mem_access(addr, size, false);
}

Instruction branch(uint64_t pc, uint64_t target) {
    return Instruction(InstructionId(0), pc, 0, OpcodeType::Branch)
        .with_branch(target, false, true);
}

Instruction branch_cond(uint64_t pc, uint64_t target, bool taken) {
    return Instruction(InstructionId(0), pc, 0, OpcodeType::BranchCond)
        .with_branch(target, true, taken);
}

Instruction nop(uint64_t pc) {
    return Instruction(InstructionId(0), pc, 0xD503201F, OpcodeType::Nop);
}

} // namespace helpers

} // namespace arm_cpu
