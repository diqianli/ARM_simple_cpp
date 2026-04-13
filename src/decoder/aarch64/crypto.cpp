/// @file crypto.cpp
/// @brief AArch64 cryptography extension instruction decoder implementations.

#include "arm_cpu/decoder/aarch64/crypto.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

#include <cstdio>
#include <string>

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::OpcodeType;
using arm_cpu::Reg;
using arm_cpu::VReg;

// =====================================================================
// decode_aes
// =====================================================================

Result<DecodedInstruction> decode_aes(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint8_t rd = decode_rd(raw);
    uint8_t rn = decode_rn(raw);
    uint32_t opcode = (raw >> 12) & 0x3;

    d.opcode = (opcode == 0) ? OpcodeType::Aese :
               (opcode == 1) ? OpcodeType::Aesd :
               (opcode == 2) ? OpcodeType::Aesmc :
               OpcodeType::Aesimc;

    d.dst_vregs.push_back(VReg(rd));
    d.src_vregs.push_back(VReg(rn));

    const char* mnemonic = (opcode == 0) ? "aese" : (opcode == 1) ? "aesd" :
                           (opcode == 2) ? "aesmc" : "aesimc";

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s v%u.16b, v%u.16b", mnemonic, rd, rn);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_sha1
// =====================================================================

Result<DecodedInstruction> decode_sha1(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint8_t rd = decode_rd(raw);
    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);

    d.opcode = OpcodeType::Sha1H;

    d.dst_vregs.push_back(VReg(rd));
    d.src_vregs.push_back(VReg(rn));
    d.src_vregs.push_back(VReg(rm));

    char buf[64];
    std::snprintf(buf, sizeof(buf), "sha1 v%u.4s, v%u.4s, v%u.4s", rd, rn, rm);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_sha256
// =====================================================================

Result<DecodedInstruction> decode_sha256(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint8_t rd = decode_rd(raw);
    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);

    d.opcode = OpcodeType::Sha256H;

    d.dst_vregs.push_back(VReg(rd));
    d.src_vregs.push_back(VReg(rn));
    d.src_vregs.push_back(VReg(rm));

    char buf[64];
    std::snprintf(buf, sizeof(buf), "sha256h v%u.4s, v%u.4s, v%u.4s", rd, rn, rm);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_sha512
// =====================================================================

Result<DecodedInstruction> decode_sha512(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint8_t rd = decode_rd(raw);
    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);

    d.opcode = OpcodeType::Sha512H;

    d.dst_vregs.push_back(VReg(rd));
    d.src_vregs.push_back(VReg(rn));
    d.src_vregs.push_back(VReg(rm));

    char buf[64];
    std::snprintf(buf, sizeof(buf), "sha512h v%u.2d, v%u.2d, v%u.2d", rd, rn, rm);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_sha3
// =====================================================================

Result<DecodedInstruction> decode_sha3(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint8_t rd = decode_rd(raw);
    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);
    uint8_t ra = static_cast<uint8_t>((raw >> 10) & 0x1F);

    d.opcode = OpcodeType::Eor;

    d.dst_vregs.push_back(VReg(rd));
    d.src_vregs.push_back(VReg(rn));
    d.src_vregs.push_back(VReg(rm));
    d.src_vregs.push_back(VReg(ra));

    char buf[80];
    std::snprintf(buf, sizeof(buf), "eor3 v%u.16b, v%u.16b, v%u.16b, v%u.16b",
                  rd, rn, rm, ra);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_crc32
// =====================================================================

Result<DecodedInstruction> decode_crc32(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool sf = is_64bit(raw);
    uint8_t rd = decode_rd(raw);
    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);
    uint32_t size = (raw >> 21) & 0x3;
    uint32_t c = (raw >> 10) & 0x1;

    d.opcode = OpcodeType::Other;

    d.dst_regs.push_back(Reg(rd));
    d.src_regs.push_back(Reg(rn));
    d.src_regs.push_back(Reg(rm));

    const char* mnemonic;
    switch ((c << 2) | size) {
        case 0: mnemonic = "crc32b";  break;
        case 1: mnemonic = "crc32h";  break;
        case 2: mnemonic = "crc32w";  break;
        case 3: mnemonic = "crc32x";  break;
        case 4: mnemonic = "crc32cb"; break;
        case 5: mnemonic = "crc32ch"; break;
        case 6: mnemonic = "crc32cw"; break;
        case 7: mnemonic = "crc32cx"; break;
        default: mnemonic = "crc32";   break;
    }

    char prefix = sf ? 'x' : 'w';
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %c%u, w%u, %c%u", mnemonic, prefix, rd, rn, prefix, rm);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_pmull
// =====================================================================

Result<DecodedInstruction> decode_pmull(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool q = decode_q_bit(raw);
    uint8_t rd = decode_rd(raw);
    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);
    uint32_t size = (raw >> 22) & 0x3;

    d.opcode = OpcodeType::Pmull;

    d.dst_vregs.push_back(VReg(rd));
    d.src_vregs.push_back(VReg(rn));
    d.src_vregs.push_back(VReg(rm));

    const char* mnemonic = q ? "pmull2" : "pmull";
    const char* reg_size = (size == 0) ? "8b" : "16b";

    char buf[80];
    std::snprintf(buf, sizeof(buf), "%s v%u.1q, v%u.%s, v%u.%s",
                  mnemonic, rd, rn, reg_size, rm, reg_size);
    d.disasm = buf;

    return d;
}

} // namespace arm_cpu::decoder::aarch64
