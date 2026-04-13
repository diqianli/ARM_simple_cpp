/// @file simd_neon.cpp
/// @brief AArch64 SIMD/NEON instruction decoder implementations.

#include "arm_cpu/decoder/aarch64/simd_neon.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

#include <cstdio>
#include <string>

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::OpcodeType;
using arm_cpu::VReg;

// =====================================================================
// decode_simd_arith
// =====================================================================

Result<DecodedInstruction> decode_simd_arith(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool q = decode_q_bit(raw);
    uint8_t rd = decode_rd(raw);
    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);
    uint32_t opcode = (raw >> 11) & 0x1F;

    d.opcode = (opcode == 0x00 || opcode == 0x01 || opcode == 0x0E || opcode == 0x0F) ? OpcodeType::Vadd :
               (opcode == 0x02 || opcode == 0x03) ? OpcodeType::Vmul :
               (opcode == 0x04 || opcode == 0x05 || opcode == 0x0C || opcode == 0x0D) ? OpcodeType::Vmla :
               (opcode == 0x06 || opcode == 0x07) ? OpcodeType::Vmls :
               (opcode == 0x08 || opcode == 0x09) ? OpcodeType::Vsub :
               OpcodeType::Vadd;

    d.dst_vregs.push_back(VReg(rd));
    d.src_vregs.push_back(VReg(rn));
    d.src_vregs.push_back(VReg(rm));

    const char* reg_size = q ? "16b" : "8b";
    char buf[128];
    std::snprintf(buf, sizeof(buf), "simd v%u.%s, v%u.%s, v%u.%s",
                  rd, reg_size, rn, reg_size, rm, reg_size);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_simd_load_store
// =====================================================================

Result<DecodedInstruction> decode_simd_load_store(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool l = ((raw >> 22) & 0x1) == 1;
    uint8_t rt = decode_rt(raw);
    uint8_t rn = decode_rn(raw);
    uint32_t opcode = (raw >> 12) & 0xF;

    d.opcode = l ? OpcodeType::Vld : OpcodeType::Vst;
    d.dst_vregs.push_back(VReg(rt));

    const char* mnemonic;
    switch (opcode) {
        case 0x0: case 0x2: case 0x4: case 0x6: case 0x7:
            mnemonic = l ? "ld1" : "st1"; break;
        case 0x1: case 0x3: case 0x5:
            mnemonic = l ? "ld2" : "st2"; break;
        case 0x8:
            mnemonic = l ? "ld3" : "st3"; break;
        case 0x9:
            mnemonic = l ? "ld4" : "st4"; break;
        default:
            mnemonic = l ? "ld1" : "st1"; break;
    }

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s { v%u.16b }, [x%u]", mnemonic, rt, rn);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_simd_permute
// =====================================================================

Result<DecodedInstruction> decode_simd_permute(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool q = decode_q_bit(raw);
    uint8_t rd = decode_rd(raw);
    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);
    uint32_t opcode = (raw >> 12) & 0x3F;

    (void)opcode; // all map to Vmov
    d.opcode = OpcodeType::Vmov;

    d.dst_vregs.push_back(VReg(rd));
    d.src_vregs.push_back(VReg(rn));
    d.src_vregs.push_back(VReg(rm));

    const char* reg_size = q ? "16b" : "8b";
    char buf[128];
    std::snprintf(buf, sizeof(buf), "perm v%u.%s, v%u.%s, v%u.%s",
                  rd, reg_size, rn, reg_size, rm, reg_size);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_simd_saturating
// =====================================================================

Result<DecodedInstruction> decode_simd_saturating(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool q = decode_q_bit(raw);
    uint8_t rd = decode_rd(raw);
    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);
    uint32_t opcode = (raw >> 11) & 0x1F;

    d.opcode = (opcode == 0x00 || opcode == 0x01 || opcode == 0x05) ? OpcodeType::Vadd :
               (opcode == 0x02 || opcode == 0x03 || opcode == 0x04) ? OpcodeType::Vsub :
               OpcodeType::Vadd;

    d.dst_vregs.push_back(VReg(rd));
    d.src_vregs.push_back(VReg(rn));
    d.src_vregs.push_back(VReg(rm));

    const char* reg_size = q ? "16b" : "8b";
    char buf[128];
    std::snprintf(buf, sizeof(buf), "qop v%u.%s, v%u.%s, v%u.%s",
                  rd, reg_size, rn, reg_size, rm, reg_size);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_simd_pairwise
// =====================================================================

Result<DecodedInstruction> decode_simd_pairwise(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool q = decode_q_bit(raw);
    uint8_t rd = decode_rd(raw);
    uint8_t rn = decode_rn(raw);
    uint32_t opcode = (raw >> 12) & 0x1F;

    d.opcode = (opcode == 0x0A || opcode == 0x0B) ? OpcodeType::Vmla : OpcodeType::Vadd;

    d.dst_vregs.push_back(VReg(rd));
    d.src_vregs.push_back(VReg(rn));

    const char* reg_size = q ? "16b" : "8b";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "addp v%u.%s, v%u.%s", rd, reg_size, rn, reg_size);
    d.disasm = buf;

    return d;
}

} // namespace arm_cpu::decoder::aarch64
