/// @file fp.cpp
/// @brief AArch64 floating-point instruction decoder implementations.

#include "arm_cpu/decoder/aarch64/fp.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

#include <cstdio>
#include <string>

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::OpcodeType;
using arm_cpu::VReg;

// =====================================================================
// decode_fp_arith
// =====================================================================

Result<DecodedInstruction> decode_fp_arith(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint32_t ftype = (raw >> 22) & 0x3;
    uint8_t rd = decode_rd(raw);
    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);
    uint32_t opcode = (raw >> 12) & 0xF;

    d.opcode = (opcode == 0x0) ? OpcodeType::Fadd :
               (opcode == 0x1) ? OpcodeType::Fsub :
               (opcode == 0x2) ? OpcodeType::Fmul :
               (opcode == 0x3) ? OpcodeType::Fdiv :
               (opcode == 0x4) ? OpcodeType::Fmadd :
               (opcode == 0x5) ? OpcodeType::Fmsub :
               (opcode == 0x6) ? OpcodeType::Fnmadd :
               (opcode == 0x7) ? OpcodeType::Fnmsub :
               OpcodeType::Fadd;

    d.dst_vregs.push_back(VReg(rd));
    d.src_vregs.push_back(VReg(rn));
    d.src_vregs.push_back(VReg(rm));

    const char* mnemonic = (opcode == 0x0) ? "fadd" : (opcode == 0x1) ? "fsub" :
                           (opcode == 0x2) ? "fmul" : (opcode == 0x3) ? "fdiv" :
                           (opcode == 0x4) ? "fmadd" : (opcode == 0x5) ? "fmsub" :
                           (opcode == 0x6) ? "fnmadd" : (opcode == 0x7) ? "fnmsub" :
                           (opcode == 0x8) ? "faddp" : "fp";

    const char* type_suffix = (ftype == 0) ? "s" : (ftype == 1) ? "d" :
                              (ftype == 2) ? "h" : "s";

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s %s%u, %s%u, %s%u",
                  mnemonic, type_suffix, rd, type_suffix, rn, type_suffix, rm);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_fp_compare
// =====================================================================

Result<DecodedInstruction> decode_fp_compare(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint32_t ftype = (raw >> 22) & 0x3;
    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);
    uint32_t opcode = (raw >> 3) & 0x3;

    d.opcode = OpcodeType::Cmp;
    d.src_vregs.push_back(VReg(rn));
    d.src_vregs.push_back(VReg(rm));

    const char* mnemonic = (opcode == 0 || opcode == 2) ? "fcmp" : "fcmpe";
    const char* type_suffix = (ftype == 0) ? "s" : (ftype == 1) ? "d" :
                              (ftype == 2) ? "h" : "s";

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %s%u, %s%u",
                  mnemonic, type_suffix, rn, type_suffix, rm);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_fp_1src
// =====================================================================

Result<DecodedInstruction> decode_fp_1src(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint32_t ftype = (raw >> 22) & 0x3;
    uint8_t rd = decode_rd(raw);
    uint8_t rn = decode_rn(raw);
    uint32_t opcode = (raw >> 15) & 0x3F;

    d.opcode = (opcode == 0x00) ? OpcodeType::Mov :  // FMOV
               (opcode == 0x01) ? OpcodeType::And :  // FABS
               (opcode == 0x02) ? OpcodeType::Eor :  // FNEG
               (opcode == 0x03) ? OpcodeType::Shift : // FSQRT
               OpcodeType::Fcvt;                      // FCVT variants

    d.dst_vregs.push_back(VReg(rd));
    d.src_vregs.push_back(VReg(rn));

    const char* mnemonic = (opcode == 0x00) ? "fmov" : (opcode == 0x01) ? "fabs" :
                           (opcode == 0x02) ? "fneg" : (opcode == 0x03) ? "fsqrt" :
                           (opcode >= 0x04 && opcode <= 0x0F) ? "fcvt" : "fp";

    const char* type_suffix = (ftype == 0) ? "s" : (ftype == 1) ? "d" :
                              (ftype == 2) ? "h" : "s";

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %s%u, %s%u",
                  mnemonic, type_suffix, rd, type_suffix, rn);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_fp_cond_compare
// =====================================================================

Result<DecodedInstruction> decode_fp_cond_compare(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint32_t ftype = (raw >> 22) & 0x3;
    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);
    uint8_t cond = decode_condition(raw);

    d.opcode = OpcodeType::Cmp;
    d.src_vregs.push_back(VReg(rn));
    d.src_vregs.push_back(VReg(rm));

    const char* type_suffix = (ftype == 0) ? "s" : (ftype == 1) ? "d" :
                              (ftype == 2) ? "h" : "s";

    char buf[64];
    std::snprintf(buf, sizeof(buf), "fccmp %s%u, %s%u, #%s",
                  type_suffix, rn, type_suffix, rm,
                  condition_name(cond).data());
    d.disasm = buf;

    return d;
}

} // namespace arm_cpu::decoder::aarch64
