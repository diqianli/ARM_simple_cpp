/// @file logical.cpp
/// @brief AArch64 logical/bitwise instruction decoder implementations.

#include "arm_cpu/decoder/aarch64/logical.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

#include <cstdio>
#include <string>

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::OpcodeType;
using arm_cpu::Reg;

// =====================================================================
// decode_logical_imm
// =====================================================================

Result<DecodedInstruction> decode_logical_imm(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool sf = is_64bit(raw);
    uint32_t opc = (raw >> 29) & 0x3;
    uint8_t rn = decode_rn(raw);
    uint8_t rd = decode_rd(raw);
    uint8_t immr = static_cast<uint8_t>((raw >> 16) & 0x3F);
    uint8_t imms = static_cast<uint8_t>((raw >> 10) & 0x3F);

    d.opcode = (opc == 0) ? OpcodeType::And :   // AND
               (opc == 1) ? OpcodeType::Orr :   // ORR
               (opc == 2) ? OpcodeType::Eor :   // EOR
               OpcodeType::And;                  // ANDS

    if (rd != 31) d.dst_regs.push_back(Reg(rd));
    if (rn != 31) d.src_regs.push_back(Reg(rn));

    char prefix = sf ? 'x' : 'w';
    const char* mnemonic = (opc == 0) ? "and" : (opc == 1) ? "orr" :
                           (opc == 2) ? "eor" : "ands";

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s %c%u, %c%u, #0x%04x",
                  mnemonic, prefix, rd, prefix, rn,
                  (static_cast<uint16_t>(immr) << 8) | static_cast<uint16_t>(imms));
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_logical_shifted
// =====================================================================

Result<DecodedInstruction> decode_logical_shifted(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool sf = is_64bit(raw);
    uint32_t opc = (raw >> 29) & 0x3;
    uint8_t shift = static_cast<uint8_t>((raw >> 22) & 0x3);
    uint8_t rm = decode_rm(raw);
    uint8_t imm6 = static_cast<uint8_t>((raw >> 10) & 0x3F);
    uint8_t rn = decode_rn(raw);
    uint8_t rd = decode_rd(raw);

    d.opcode = (opc == 0) ? OpcodeType::And :   // AND
               (opc == 1) ? OpcodeType::Orr :   // ORR
               (opc == 2) ? OpcodeType::Eor :   // EOR
               OpcodeType::And;                  // ANDS/TST

    if (rd != 31) d.dst_regs.push_back(Reg(rd));
    if (rn != 31) d.src_regs.push_back(Reg(rn));
    if (rm != 31) d.src_regs.push_back(Reg(rm));

    char prefix = sf ? 'x' : 'w';
    const char* mnemonic = (opc == 0) ? "and" : (opc == 1) ? "orr" :
                           (opc == 2) ? "eor" : "ands";

    char buf[128];
    if (imm6 > 0) {
        std::snprintf(buf, sizeof(buf), "%s %c%u, %c%u, %c%u, %s #%u",
                      mnemonic, prefix, rd, prefix, rn, prefix, rm,
                      shift_name(shift).data(), imm6);
    } else {
        std::snprintf(buf, sizeof(buf), "%s %c%u, %c%u, %c%u",
                      mnemonic, prefix, rd, prefix, rn, prefix, rm);
    }
    d.disasm = buf;

    return d;
}

} // namespace arm_cpu::decoder::aarch64
