/// @file branch.cpp
/// @brief AArch64 branch and control-flow instruction decoder implementations.

#include "arm_cpu/decoder/aarch64/branch.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

#include <cinttypes>
#include <cstdio>
#include <string>

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::OpcodeType;
using arm_cpu::Reg;
using arm_cpu::BranchInfo;

// =====================================================================
// decode_branch_imm
// =====================================================================

Result<DecodedInstruction> decode_branch_imm(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool is_call = ((raw >> 31) & 0x1) == 1;
    uint32_t imm26 = decode_imm26(raw);
    int64_t offset = sign_extend_26(imm26) << 2;
    uint64_t target = pc + static_cast<uint64_t>(offset);

    d.opcode = OpcodeType::Branch; // both B and BL
    d.branch_info = BranchInfo{false, target, true};

    const char* mnemonic = is_call ? "bl" : "b";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %#" PRIx64, mnemonic, target);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_branch_cond
// =====================================================================

Result<DecodedInstruction> decode_branch_cond(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint32_t imm19 = decode_imm19(raw);
    uint8_t cond = decode_condition(raw);
    int64_t offset = sign_extend_19(imm19) << 2;
    uint64_t target = pc + static_cast<uint64_t>(offset);

    d.opcode = OpcodeType::BranchCond;
    d.branch_info = BranchInfo{true, target, false};

    char buf[64];
    std::snprintf(buf, sizeof(buf), "b.%s %#" PRIx64,
                  condition_name(cond).data(), target);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_branch_reg
// =====================================================================

Result<DecodedInstruction> decode_branch_reg(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint8_t rn = decode_rn(raw);
    uint32_t opc = (raw >> 21) & 0xF;

    d.opcode = (opc == 0 || opc == 1 || opc == 2) ? OpcodeType::BranchReg :
               OpcodeType::Eret;

    if (rn != 31) d.src_regs.push_back(Reg(rn));

    const char* mnemonic = (opc == 0) ? "br" : (opc == 1) ? "blr" :
                           (opc == 2) ? "ret" : (opc == 4) ? "eret" :
                           (opc == 5) ? "drps" : "br";

    char buf[64];
    if (opc == 2) {
        std::snprintf(buf, sizeof(buf), "%s", mnemonic);
    } else {
        std::snprintf(buf, sizeof(buf), "%s x%u", mnemonic, rn);
    }
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_compare_branch
// =====================================================================

Result<DecodedInstruction> decode_compare_branch(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool sf = is_64bit(raw);
    bool is_nz = ((raw >> 24) & 0x1) == 1;
    uint8_t rt = decode_rt(raw);
    uint32_t imm19 = decode_imm19(raw);
    int64_t offset = sign_extend_19(imm19) << 2;
    uint64_t target = pc + static_cast<uint64_t>(offset);

    d.opcode = OpcodeType::BranchCond;
    d.branch_info = BranchInfo{true, target, false};

    if (rt != 31) d.src_regs.push_back(Reg(rt));

    char prefix = sf ? 'x' : 'w';
    const char* mnemonic = is_nz ? "cbnz" : "cbz";

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %c%u, %#" PRIx64,
                  mnemonic, prefix, rt, target);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_test_branch
// =====================================================================

Result<DecodedInstruction> decode_test_branch(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool sf = is_64bit(raw);
    bool is_nz = ((raw >> 24) & 0x1) == 1;
    uint8_t rt = decode_rt(raw);
    uint32_t imm14 = (raw >> 5) & 0x3FFF;
    uint8_t bit = static_cast<uint8_t>((raw >> 19) & 0x1F);
    int64_t offset = sign_extend(imm14, 14) << 2;
    uint64_t target = pc + static_cast<uint64_t>(offset);

    d.opcode = OpcodeType::BranchCond;
    d.branch_info = BranchInfo{true, target, false};

    if (rt != 31) d.src_regs.push_back(Reg(rt));

    const char* mnemonic = is_nz ? "tbnz" : "tbz";
    uint8_t full_bit = sf ? bit : (bit & 0x1F);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %c%u, #%u, %#" PRIx64,
                  mnemonic, sf ? 'x' : 'w', rt, full_bit,
                  target);
    d.disasm = buf;

    return d;
}

} // namespace arm_cpu::decoder::aarch64
