/// @file arithmetic.cpp
/// @brief AArch64 integer arithmetic instruction decoder implementations.

#include "arm_cpu/decoder/aarch64/arithmetic.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

#include <cstdio>
#include <string>

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::OpcodeType;
using arm_cpu::Reg;

// =====================================================================
// decode_add_sub_imm
// =====================================================================

Result<DecodedInstruction> decode_add_sub_imm(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool sf = is_64bit(raw);
    uint32_t opc = (raw >> 29) & 0x3;
    uint8_t shift = decode_shift(raw);
    uint16_t imm12 = decode_imm12(raw);
    uint8_t rn = decode_rn(raw);
    uint8_t rd = decode_rd(raw);

    d.opcode = (opc == 0 || opc == 1) ? OpcodeType::Add : OpcodeType::Sub;

    if (rd != 31) d.dst_regs.push_back(Reg(rd));
    if (rn != 31) d.src_regs.push_back(Reg(rn));

    uint64_t imm = (shift == 1) ? static_cast<uint64_t>(imm12) << 12
                                : static_cast<uint64_t>(imm12);
    d.immediate = static_cast<int64_t>(imm);

    char prefix = sf ? 'x' : 'w';
    const char* mnemonic = (opc == 0) ? "add" : (opc == 1) ? "adds" :
                           (opc == 2) ? "sub" : "subs";
    const char* shift_str = (shift == 1) ? ", lsl #12" : "";

    char rd_buf[16], rn_buf[16];
    const char* rd_name;
    const char* rn_name;
    if (rd == 31) { rd_name = "sp"; } else { std::snprintf(rd_buf, sizeof(rd_buf), "%c%u", prefix, rd); rd_name = rd_buf; }
    if (rn == 31) { rn_name = "sp"; } else { std::snprintf(rn_buf, sizeof(rn_buf), "%c%u", prefix, rn); rn_name = rn_buf; }

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s %s, %s, #%u%s", mnemonic, rd_name, rn_name, imm12, shift_str);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_add_sub_shifted
// =====================================================================

Result<DecodedInstruction> decode_add_sub_shifted(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool sf = is_64bit(raw);
    uint32_t opc = (raw >> 29) & 0x3;
    uint8_t shift = static_cast<uint8_t>((raw >> 22) & 0x3);
    uint8_t rm = decode_rm(raw);
    uint8_t imm6 = static_cast<uint8_t>((raw >> 10) & 0x3F);
    uint8_t rn = decode_rn(raw);
    uint8_t rd = decode_rd(raw);

    d.opcode = (opc == 0 || opc == 1) ? OpcodeType::Add : OpcodeType::Sub;

    if (rd != 31) d.dst_regs.push_back(Reg(rd));
    if (rn != 31) d.src_regs.push_back(Reg(rn));
    if (rm != 31) d.src_regs.push_back(Reg(rm));

    char prefix = sf ? 'x' : 'w';
    const char* mnemonic = (opc == 0) ? "add" : (opc == 1) ? "adds" :
                           (opc == 2) ? "sub" : "subs";

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

// =====================================================================
// decode_add_sub_extended
// =====================================================================

Result<DecodedInstruction> decode_add_sub_extended(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool sf = is_64bit(raw);
    uint32_t opc = (raw >> 29) & 0x3;
    uint8_t option = decode_option(raw);
    uint8_t imm3 = static_cast<uint8_t>((raw >> 10) & 0x7);
    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);
    uint8_t rd = decode_rd(raw);

    d.opcode = (opc == 0 || opc == 1) ? OpcodeType::Add : OpcodeType::Sub;

    if (rd != 31) d.dst_regs.push_back(Reg(rd));
    if (rn != 31) d.src_regs.push_back(Reg(rn));
    if (rm != 31) d.src_regs.push_back(Reg(rm));

    char prefix = sf ? 'x' : 'w';
    const char* mnemonic = (opc == 0) ? "add" : (opc == 1) ? "adds" :
                           (opc == 2) ? "sub" : "subs";

    char buf[128];
    if (imm3 > 0) {
        std::snprintf(buf, sizeof(buf), "%s %c%u, %c%u, %c%u %s, #%u",
                      mnemonic, prefix, rd, prefix, rn, prefix, rm,
                      extend_name(option).data(), imm3);
    } else {
        std::snprintf(buf, sizeof(buf), "%s %c%u, %c%u, %c%u %s",
                      mnemonic, prefix, rd, prefix, rn, prefix, rm,
                      extend_name(option).data());
    }
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_multiply_divide
// =====================================================================

Result<DecodedInstruction> decode_multiply_divide(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool sf = is_64bit(raw);
    uint8_t rm = decode_rm(raw);
    uint8_t ra = static_cast<uint8_t>((raw >> 10) & 0x1F);
    uint8_t rn = decode_rn(raw);
    uint8_t rd = decode_rd(raw);
    uint32_t op = (raw >> 15) & 0x7;

    d.opcode = (op == 0 || op == 1 || op == 6) ? OpcodeType::Mul : OpcodeType::Div;

    if (rd != 31) d.dst_regs.push_back(Reg(rd));
    if (rn != 31) d.src_regs.push_back(Reg(rn));
    if (rm != 31) d.src_regs.push_back(Reg(rm));
    if (ra != 31 && (op == 0 || op == 1)) d.src_regs.push_back(Reg(ra));

    char prefix = sf ? 'x' : 'w';
    const char* mnemonic = (op == 0) ? "madd" : (op == 1) ? "msub" :
                           (op == 2) ? "smulh" : (op == 3) ? "umulh" :
                           (op == 4) ? "smull" : (op == 5) ? "umull" :
                           (op == 6) ? "smull" :
                           (((raw >> 10) & 1) ? "udiv" : "sdiv");

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %c%u, %c%u, %c%u",
                  mnemonic, prefix, rd, prefix, rn, prefix, rm);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_cond_compare
// =====================================================================

Result<DecodedInstruction> decode_cond_compare(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);
    uint8_t cond = decode_condition(raw);

    d.opcode = OpcodeType::Cmp;
    d.src_regs.push_back(Reg(rn));
    if (rm != 31) d.src_regs.push_back(Reg(rm));

    char buf[64];
    std::snprintf(buf, sizeof(buf), "ccmp x%u, x%u, #%s",
                  rn, rm, condition_name(cond).data());
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_data_proc_2src
// =====================================================================

Result<DecodedInstruction> decode_data_proc_2src(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool sf = is_64bit(raw);
    uint8_t rm = decode_rm(raw);
    uint32_t opcode = (raw >> 10) & 0x3F;
    uint8_t rn = decode_rn(raw);
    uint8_t rd = decode_rd(raw);

    d.opcode = (opcode == 0x00 || opcode == 0x01) ? OpcodeType::Div :
               (opcode == 0x02) ? OpcodeType::Lsl :
               (opcode == 0x03) ? OpcodeType::Lsr :
               (opcode == 0x04 || opcode == 0x05) ? OpcodeType::Asr :
               OpcodeType::Shift; // CRC32 etc.

    if (rd != 31) d.dst_regs.push_back(Reg(rd));
    if (rn != 31) d.src_regs.push_back(Reg(rn));
    if (rm != 31) d.src_regs.push_back(Reg(rm));

    char prefix = sf ? 'x' : 'w';
    const char* name = (opcode == 0x00) ? "sdiv" : (opcode == 0x01) ? "udiv" :
                       (opcode == 0x02) ? "lslv" : (opcode == 0x03) ? "lsrv" :
                       (opcode == 0x04) ? "asrv" : (opcode == 0x05) ? "rorv" :
                       (opcode == 0x06) ? "crc32x" : (opcode == 0x07) ? "crc32w" :
                       "data_proc_2src";

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %c%u, %c%u, %c%u", name, prefix, rd, prefix, rn, prefix, rm);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_data_proc_1src
// =====================================================================

Result<DecodedInstruction> decode_data_proc_1src(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool sf = is_64bit(raw);
    uint32_t opcode = (raw >> 10) & 0x3F;
    uint8_t rn = decode_rn(raw);
    uint8_t rd = decode_rd(raw);

    d.opcode = (opcode == 0x00) ? OpcodeType::Mov :   // RBIT
               (opcode == 0x04) ? OpcodeType::Mov :   // CLZ
               (opcode == 0x05 || opcode == 0x06) ? OpcodeType::Cmp :
               OpcodeType::Shift; // REV16, REV32, REV64

    if (rd != 31) d.dst_regs.push_back(Reg(rd));
    if (rn != 31) d.src_regs.push_back(Reg(rn));

    char prefix = sf ? 'x' : 'w';
    const char* name = (opcode == 0x00) ? "rbit" : (opcode == 0x01) ? "rev16" :
                       (opcode == 0x02) ? "rev32" : (opcode == 0x03) ? "rev64" :
                       (opcode == 0x04) ? "clz" : (opcode == 0x05) ? "cls" :
                       (opcode == 0x06) ? "ctz" : "data_proc_1src";

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %c%u, %c%u", name, prefix, rd, prefix, rn);
    d.disasm = buf;

    return d;
}

} // namespace arm_cpu::decoder::aarch64
