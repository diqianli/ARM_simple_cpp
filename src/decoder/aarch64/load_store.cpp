/// @file load_store.cpp
/// @brief AArch64 load/store instruction decoder implementations.

#include "arm_cpu/decoder/aarch64/load_store.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

#include <cinttypes>
#include <cstdio>
#include <string>

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::OpcodeType;
using arm_cpu::Reg;
using arm_cpu::MemAccess;

// =====================================================================
// decode_load_store_imm
// =====================================================================

Result<DecodedInstruction> decode_load_store_imm(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint8_t size = decode_size(raw);
    bool sf = is_64bit(raw);
    bool is_load = ((raw >> 22) & 0x1) == 1;
    uint8_t rt = decode_rt(raw);
    uint8_t rn = decode_rn(raw);
    uint16_t imm12 = decode_imm12(raw);

    uint8_t access_size = (size == 0) ? 1 : (size == 1) ? 2 : (size == 2) ? 4 : 8;

    d.opcode = is_load ? OpcodeType::Load : OpcodeType::Store;

    if (rt != 31) d.dst_regs.push_back(Reg(rt));
    if (rn != 31) d.src_regs.push_back(Reg(rn));

    d.mem_access = MemAccess{0, access_size, is_load};

    char prefix = sf ? 'x' : 'w';
    const char* mnemonic = is_load ? "ldr" : "str";
    uint64_t offset = static_cast<uint64_t>(imm12) * access_size;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %c%u, [x%u, #%" PRIu64 "]",
                  mnemonic, prefix, rt, rn, offset);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_load_store_reg
// =====================================================================

Result<DecodedInstruction> decode_load_store_reg(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint8_t size = decode_size(raw);
    bool sf = is_64bit(raw);
    bool is_load = ((raw >> 22) & 0x1) == 1;
    uint8_t rt = decode_rt(raw);
    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);

    uint8_t access_size = 1u << size;

    d.opcode = is_load ? OpcodeType::Load : OpcodeType::Store;

    if (rt != 31) d.dst_regs.push_back(Reg(rt));
    if (rn != 31) d.src_regs.push_back(Reg(rn));
    if (rm != 31) d.src_regs.push_back(Reg(rm));

    d.mem_access = MemAccess{0, access_size, is_load};

    char prefix = sf ? 'x' : 'w';
    const char* mnemonic = is_load ? "ldr" : "str";

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %c%u, [x%u, x%u]",
                  mnemonic, prefix, rt, rn, rm);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_load_store_pair
// =====================================================================

Result<DecodedInstruction> decode_load_store_pair(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    bool sf = is_64bit(raw);
    bool l = ((raw >> 22) & 0x1) == 1;
    uint8_t rt = decode_rt(raw);
    uint8_t rt2 = static_cast<uint8_t>((raw >> 10) & 0x1F);
    uint8_t rn = decode_rn(raw);
    int8_t imm7 = static_cast<int8_t>((raw >> 15) & 0x7F);

    d.opcode = l ? OpcodeType::LoadPair : OpcodeType::StorePair;

    if (rt != 31) d.dst_regs.push_back(Reg(rt));
    if (l && rt2 != 31) d.dst_regs.push_back(Reg(rt2));
    if (rn != 31) d.src_regs.push_back(Reg(rn));

    uint8_t access_size = sf ? 8 : 4;
    d.mem_access = MemAccess{0, static_cast<uint8_t>(access_size * 2), l};

    char prefix = sf ? 'x' : 'w';
    const char* mnemonic = l ? "ldp" : "stp";
    int64_t offset = static_cast<int64_t>(imm7) * access_size;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %c%u, %c%u, [x%u, #%" PRId64 "]",
                  mnemonic, prefix, rt, prefix, rt2, rn, offset);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_atomic
// =====================================================================

Result<DecodedInstruction> decode_atomic(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint8_t size = decode_size(raw);
    bool sf = is_64bit(raw);
    uint8_t rt = decode_rt(raw);
    uint8_t rn = decode_rn(raw);
    uint8_t rm = decode_rm(raw);

    d.opcode = OpcodeType::Load;

    if (rt != 31) d.dst_regs.push_back(Reg(rt));
    if (rn != 31) d.src_regs.push_back(Reg(rn));
    if (rm != 31) d.src_regs.push_back(Reg(rm));

    uint8_t access_size = 1u << size;
    d.mem_access = MemAccess{0, access_size, true};

    char prefix = sf ? 'x' : 'w';
    char buf[64];
    std::snprintf(buf, sizeof(buf), "atomic %c%u, %c%u, [x%u]",
                  prefix, rt, prefix, rm, rn);
    d.disasm = buf;

    return d;
}

// =====================================================================
// decode_exclusive
// =====================================================================

Result<DecodedInstruction> decode_exclusive(uint64_t pc, uint32_t raw) {
    DecodedInstruction d(pc, raw);

    uint8_t size = decode_size(raw);
    bool sf = is_64bit(raw);
    bool is_load = ((raw >> 22) & 0x1) == 1;
    uint8_t rt = decode_rt(raw);
    uint8_t rn = decode_rn(raw);
    uint8_t rs = static_cast<uint8_t>((raw >> 16) & 0x1F);

    d.opcode = is_load ? OpcodeType::Load : OpcodeType::Store;

    if (rt != 31) d.dst_regs.push_back(Reg(rt));
    if (rn != 31) d.src_regs.push_back(Reg(rn));
    if (!is_load && rs != 31) d.src_regs.push_back(Reg(rs));

    uint8_t access_size = 1u << size;
    d.mem_access = MemAccess{0, access_size, is_load};

    char prefix = sf ? 'x' : 'w';
    const char* mnemonic = is_load ? "ldaxr" : "stlxr";

    char buf[64];
    if (is_load) {
        std::snprintf(buf, sizeof(buf), "%s %c%u, [x%u]", mnemonic, prefix, rt, rn);
    } else {
        std::snprintf(buf, sizeof(buf), "%s w%u, %c%u, [x%u]", mnemonic, rs, prefix, rt, rn);
    }
    d.disasm = buf;

    return d;
}

} // namespace arm_cpu::decoder::aarch64
