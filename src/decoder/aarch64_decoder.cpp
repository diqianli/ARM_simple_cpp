/// @file aarch64_decoder.cpp
/// @brief Implementation of the main AArch64 decoder (dispatch logic).

#include "arm_cpu/decoder/aarch64_decoder.hpp"
#include "arm_cpu/decoder/aarch64/encoding.hpp"

#include <cinttypes>
#include <cstdio>
#include <optional>
#include <string>

namespace arm_cpu::decoder {

using namespace ::arm_cpu; // Reg, VReg, OpcodeType, etc.
namespace enc = ::arm_cpu::decoder::aarch64; // encoding helpers

// =====================================================================
// AArch64Decoder -- public
// =====================================================================

Result<DecodedInstruction> AArch64Decoder::decode(uint64_t pc, uint32_t raw) const {
    // Try each decoder category in order of expected frequency.

    if (auto op = try_data_proc_imm(pc, raw)) return *op;
    if (auto op = try_branch(pc, raw))         return *op;
    if (auto op = try_load_store(pc, raw))     return *op;
    if (auto op = try_data_proc_reg(pc, raw))  return *op;
    if (auto op = try_simd_fp(pc, raw))        return *op;
    if (auto op = try_system(pc, raw))         return *op;

    // Fallback: unknown instruction
    DecodedInstruction decoded(pc, raw);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "unknown %08x", raw);
    decoded.disasm = buf;
    return decoded;
}

// =====================================================================
// AArch64Decoder -- internal dispatch
// =====================================================================

std::optional<DecodedInstruction>
AArch64Decoder::try_data_proc_imm(uint64_t pc, uint32_t raw) const {
    uint32_t op = (raw >> 25) & 0x1;
    if (op == 0) {
        return decode_pc_rel(pc, raw);
    }
    // Add/subtract (immediate), logical (imm), move wide, bitfield, extract
    if (auto d = decode_add_sub_imm(pc, raw))  return d;
    if (auto d = decode_logical_imm(pc, raw))  return d;
    if (auto d = decode_move_wide(pc, raw))    return d;
    if (auto d = decode_bitfield(pc, raw))     return d;
    if (auto d = decode_extract(pc, raw))      return d;
    return std::nullopt;
}

std::optional<DecodedInstruction>
AArch64Decoder::try_branch(uint64_t pc, uint32_t raw) const {
    uint32_t top = (raw >> 26) & 0x3F;
    switch (top) {
        case 0x05: {
            // Conditional branch (immediate)
            DecodedInstruction d(pc, raw);
            d.opcode = OpcodeType::BranchCond;
            uint32_t imm19 = (raw >> 5) & 0x7FFFF;
            int32_t offset = static_cast<int32_t>(static_cast<int64_t>(enc::sign_extend(imm19, 19)) << 2);
            uint64_t target = pc + static_cast<uint64_t>(static_cast<int64_t>(offset) * 4);
            d.branch_info = BranchInfo{true, target, true};
            char buf[64];
            std::snprintf(buf, sizeof(buf), "b.cond %#" PRIx64, target);
            d.disasm = buf;
            return d;
        }
        case 0x04: {
            // Unconditional branch (immediate)
            DecodedInstruction d(pc, raw);
            d.opcode = OpcodeType::Branch;
            uint32_t imm26 = raw & 0x3FFFFFF;
            int64_t offset = enc::sign_extend(imm26, 26) << 2;
            uint64_t target = pc + static_cast<uint64_t>(offset);
            d.branch_info = BranchInfo{false, target, true};
            char buf[64];
            std::snprintf(buf, sizeof(buf), "b %#" PRIx64, target);
            d.disasm = buf;
            return d;
        }
        case 0x06:
        case 0x07: {
            // Unconditional branch (register)
            DecodedInstruction d(pc, raw);
            d.opcode = OpcodeType::BranchReg;
            uint32_t rn = (raw >> 5) & 0x1F;
            if (rn != 31) d.src_regs.push_back(Reg(static_cast<uint8_t>(rn)));
            char buf[32];
            std::snprintf(buf, sizeof(buf), "br x%u", rn);
            d.disasm = buf;
            return d;
        }
        case 0x25:
        case 0x27: {
            // Compare and branch (CBZ / CBNZ)
            DecodedInstruction d(pc, raw);
            d.opcode = OpcodeType::BranchCond;
            uint32_t rt = raw & 0x1F;
            uint32_t sf = (raw >> 31) & 0x1;
            if (rt != 31) d.src_regs.push_back(Reg(static_cast<uint8_t>(rt)));
            uint32_t imm19 = (raw >> 5) & 0x7FFFF;
            int64_t offset = enc::sign_extend(imm19, 19) << 2;
            uint64_t target = pc + static_cast<uint64_t>(offset);
            d.branch_info = BranchInfo{true, target, true};
            char buf[64];
            std::snprintf(buf, sizeof(buf), "cbz %cx%u, %#" PRIx64, sf ? 'x' : 'w', rt, target);
            d.disasm = buf;
            return d;
        }
        default:
            return std::nullopt;
    }
}

std::optional<DecodedInstruction>
AArch64Decoder::try_load_store(uint64_t pc, uint32_t raw) const {
    uint32_t op0 = (raw >> 28) & 0x7;
    switch (op0) {
        case 0x4: case 0x5: case 0x6: case 0x7:
            return decode_load_store_reg(pc, raw);
        case 0x0: case 0x1: case 0x2: case 0x3:
            return decode_load_store_pair_or_excl(pc, raw);
        default:
            return std::nullopt;
    }
}

std::optional<DecodedInstruction>
AArch64Decoder::try_data_proc_reg(uint64_t pc, uint32_t raw) const {
    uint32_t top = (raw >> 24) & 0x1F;
    switch (top) {
        case 0x0A: case 0x0B:
            return decode_data_proc_reg_op(pc, raw);
        case 0x08: case 0x09:
            return decode_add_sub_ext(pc, raw);
        default:
            return std::nullopt;
    }
}

std::optional<DecodedInstruction>
AArch64Decoder::try_simd_fp(uint64_t pc, uint32_t raw) const {
    uint32_t top = (raw >> 24) & 0x1F;
    if (top != 0x0E && top != 0x0F) return std::nullopt;

    DecodedInstruction d(pc, raw);
    uint32_t is_fp = (raw >> 28) & 0x1;
    uint32_t opc = (raw >> 12) & 0xF;

    if (is_fp) {
        d.opcode = (opc == 0x0) ? OpcodeType::Fadd :
                   (opc == 0x2) ? OpcodeType::Fsub :
                   (opc == 0x8) ? OpcodeType::Fmul :
                   (opc == 0xA) ? OpcodeType::Fdiv :
                   (opc == 0x1 || opc == 0x5) ? OpcodeType::Fmadd :
                   OpcodeType::Fadd;
    } else {
        d.opcode = (opc == 0x0) ? OpcodeType::Vadd :
                   (opc == 0x2) ? OpcodeType::Vsub :
                   (opc == 0x8) ? OpcodeType::Vmul :
                   (opc == 0xA) ? OpcodeType::Vmla :
                   OpcodeType::Vadd;
    }

    uint32_t rd = raw & 0x1F;
    uint32_t rn = (raw >> 5) & 0x1F;
    d.dst_vregs.push_back(VReg(static_cast<uint8_t>(rd)));
    d.src_vregs.push_back(VReg(static_cast<uint8_t>(rn)));

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s v%u, v%u",
                  is_fp ? "fp" : "simd", rd, rn);
    d.disasm = buf;
    return d;
}

std::optional<DecodedInstruction>
AArch64Decoder::try_system(uint64_t pc, uint32_t raw) const {
    uint32_t top = (raw >> 24) & 0x1F;

    if (top == 0x14 || top == 0x15 || top == 0x16 || top == 0x17) {
        DecodedInstruction d(pc, raw);
        uint32_t crm = (raw >> 8) & 0xF;
        uint32_t op1 = (raw >> 16) & 0x7;
        uint32_t op2 = (raw >> 5) & 0x7;

        // HINT instructions
        if (op1 == 0 && op2 == 0) {
            d.opcode = (crm == 1) ? OpcodeType::Yield : OpcodeType::Nop;
            d.disasm = (crm == 0) ? "nop" : (crm == 1) ? "yield" : "hint";
            return d;
        }

        // Instruction cache maintenance
        if (op1 == 3) {
            d.opcode = (crm == 1) ? OpcodeType::IcIvau :
                       (crm == 2) ? OpcodeType::IcIallu :
                       OpcodeType::Sys;
            d.disasm = (crm == 1) ? "ic ivau" : (crm == 2) ? "ic iallu" : "ic";
            return d;
        }

        // Data cache maintenance
        if (op1 == 7) {
            d.opcode = (crm == 1) ? OpcodeType::DcCivac :
                       (crm == 2) ? OpcodeType::DcCvac :
                       (crm == 4) ? OpcodeType::DcZva :
                       OpcodeType::Sys;
            d.disasm = (crm == 1) ? "dc civac" : (crm == 2) ? "dc cvac" :
                       (crm == 4) ? "dc zva" : "dc";
            return d;
        }

        d.opcode = OpcodeType::Sys;
        d.disasm = "sys";
        return d;
    }

    if (top == 0x18 || top == 0x19) {
        // MSR / MRS
        DecodedInstruction d(pc, raw);
        uint32_t l = (raw >> 21) & 0x1;
        d.opcode = l ? OpcodeType::Mrs : OpcodeType::Msr;
        uint32_t rt = raw & 0x1F;
        if (rt != 31) d.dst_regs.push_back(Reg(static_cast<uint8_t>(rt)));
        d.disasm = l ? "mrs" : "msr";
        return d;
    }

    return std::nullopt;
}

// =====================================================================
// AArch64Decoder -- data-processing immediate sub-decoders
// =====================================================================

std::optional<DecodedInstruction>
AArch64Decoder::decode_pc_rel(uint64_t pc, uint32_t raw) const {
    uint32_t op = (raw >> 24) & 0x1;
    DecodedInstruction d(pc, raw);

    d.opcode = OpcodeType::Adr;
    uint32_t rd = raw & 0x1F;
    d.dst_regs.push_back(Reg(static_cast<uint8_t>(rd)));

    if (op == 0) {
        // ADRP
        uint32_t immlo = (raw >> 29) & 0x3;
        uint32_t immhi = (raw >> 5) & 0x7FFFF;
        int64_t imm = static_cast<int64_t>((immhi << 2) | immlo);
        uint64_t target = (pc & 0xFFFF'FFFF'FFFF'F000ULL) + static_cast<uint64_t>(imm << 12);
        d.immediate = static_cast<int64_t>(target);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "adrp x%u, %#" PRIx64, rd, target);
        d.disasm = buf;
    } else {
        d.disasm = "adr";
    }

    return d;
}

std::optional<DecodedInstruction>
AArch64Decoder::decode_add_sub_imm(uint64_t pc, uint32_t raw) const {
    uint32_t top = (raw >> 24) & 0x1F;
    if (top != 0x11 && top != 0x12 && top != 0x13) return std::nullopt;

    DecodedInstruction d(pc, raw);
    uint32_t is_sub = (raw >> 30) & 0x1;
    uint32_t is_64bit = (raw >> 31) & 0x1;
    uint32_t rd = raw & 0x1F;
    uint32_t rn = (raw >> 5) & 0x1F;
    uint16_t imm = static_cast<uint16_t>((raw >> 10) & 0xFFF);
    uint32_t shift = (raw >> 22) & 0x3;

    d.opcode = is_sub ? OpcodeType::Sub : OpcodeType::Add;

    if (rd != 31) d.dst_regs.push_back(Reg(static_cast<uint8_t>(rd)));
    if (rn != 31) d.src_regs.push_back(Reg(static_cast<uint8_t>(rn)));

    char prefix = is_64bit ? 'x' : 'w';
    const char* shift_str = (shift == 1) ? ", lsl #12" : "";
    char rd_name[8], rn_name[8];
    std::snprintf(rd_name, sizeof(rd_name), "%c%u%s", prefix, rd, (rd == 31) ? "sp" : "");
    std::snprintf(rn_name, sizeof(rn_name), "%c%u%s", prefix, rn, (rn == 31) ? "sp" : "");
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s %s, %s, #%u%s",
                  is_sub ? "sub" : "add",
                  (rd == 31) ? "sp" : rd_name,
                  (rn == 31) ? "sp" : rn_name,
                  imm, shift_str);
    d.disasm = buf;
    return d;
}

std::optional<DecodedInstruction>
AArch64Decoder::decode_logical_imm(uint64_t pc, uint32_t raw) const {
    uint32_t top = (raw >> 23) & 0x3F;
    if ((top >> 1) != 0x10) return std::nullopt;

    DecodedInstruction d(pc, raw);
    uint32_t opc = (raw >> 29) & 0x3;
    uint32_t rn = (raw >> 5) & 0x1F;
    uint32_t rd = raw & 0x1F;

    d.opcode = (opc == 0) ? OpcodeType::And :
               (opc == 1) ? OpcodeType::Orr :
               (opc == 2) ? OpcodeType::Eor :
               OpcodeType::And; // ANDS

    if (rd != 31) d.dst_regs.push_back(Reg(static_cast<uint8_t>(rd)));
    if (rn != 31) d.src_regs.push_back(Reg(static_cast<uint8_t>(rn)));

    return d;
}

std::optional<DecodedInstruction>
AArch64Decoder::decode_move_wide(uint64_t pc, uint32_t raw) const {
    uint32_t top = (raw >> 23) & 0x3F;
    if (top != 0x12 && top != 0x13) return std::nullopt;

    DecodedInstruction d(pc, raw);
    uint32_t opc = (raw >> 29) & 0x3;
    uint32_t hw = (raw >> 21) & 0x3;
    uint16_t imm = static_cast<uint16_t>((raw >> 5) & 0xFFFF);
    uint32_t rd = raw & 0x1F;

    d.opcode = (opc == 0) ? OpcodeType::Nop : // MOVN
               (opc == 2) ? OpcodeType::Mov : // MOVZ
               (opc == 3) ? OpcodeType::Mov : // MOVK
               OpcodeType::Mov;

    if (rd != 31) d.dst_regs.push_back(Reg(static_cast<uint8_t>(rd)));
    d.immediate = static_cast<int64_t>(imm) << (hw * 16);

    char buf[64];
    std::snprintf(buf, sizeof(buf), "mov x%u, #%u", rd, imm);
    d.disasm = buf;
    return d;
}

std::optional<DecodedInstruction>
AArch64Decoder::decode_bitfield(uint64_t pc, uint32_t raw) const {
    uint32_t top = (raw >> 23) & 0x3F;
    if (top != 0x14) return std::nullopt;

    DecodedInstruction d(pc, raw);
    uint32_t opc = (raw >> 29) & 0x3;
    uint32_t rn = (raw >> 5) & 0x1F;
    uint32_t rd = raw & 0x1F;

    d.opcode = (opc == 0) ? OpcodeType::Lsl :  // SBFM
               (opc == 1) ? OpcodeType::Lsr :  // BFM
               (opc == 2) ? OpcodeType::Asr :  // UBFM
               OpcodeType::Shift;

    if (rd != 31) d.dst_regs.push_back(Reg(static_cast<uint8_t>(rd)));
    if (rn != 31) d.src_regs.push_back(Reg(static_cast<uint8_t>(rn)));
    return d;
}

std::optional<DecodedInstruction>
AArch64Decoder::decode_extract(uint64_t pc, uint32_t raw) const {
    uint32_t top = (raw >> 23) & 0x3F;
    if (top != 0x15) return std::nullopt;

    DecodedInstruction d(pc, raw);
    uint32_t rm = (raw >> 16) & 0x1F;
    uint32_t rn = (raw >> 5) & 0x1F;
    uint32_t rd = raw & 0x1F;

    d.opcode = OpcodeType::Shift; // EXTR / DEPR

    if (rd != 31) d.dst_regs.push_back(Reg(static_cast<uint8_t>(rd)));
    if (rn != 31) d.src_regs.push_back(Reg(static_cast<uint8_t>(rn)));
    if (rm != 31) d.src_regs.push_back(Reg(static_cast<uint8_t>(rm)));
    return d;
}

// =====================================================================
// AArch64Decoder -- load/store sub-decoders
// =====================================================================

std::optional<DecodedInstruction>
AArch64Decoder::decode_load_store_reg(uint64_t pc, uint32_t raw) const {
    DecodedInstruction d(pc, raw);
    uint32_t size = (raw >> 30) & 0x3;
    bool is_load = ((raw >> 22) & 0x1) == 1;
    uint32_t rt = raw & 0x1F;
    uint32_t rn = (raw >> 5) & 0x1F;

    d.opcode = is_load ? OpcodeType::Load : OpcodeType::Store;
    if (rt != 31) d.dst_regs.push_back(Reg(static_cast<uint8_t>(rt)));
    if (rn != 31) d.src_regs.push_back(Reg(static_cast<uint8_t>(rn)));

    uint16_t imm = static_cast<uint16_t>((raw >> 10) & 0xFFF);
    uint8_t access_size = 1u << size;
    d.mem_access = MemAccess{0, access_size, is_load};

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s x%u, [x%u, #%u]",
                  is_load ? "ldr" : "str", rt, rn, imm);
    d.disasm = buf;
    return d;
}

std::optional<DecodedInstruction>
AArch64Decoder::decode_load_store_pair_or_excl(uint64_t pc, uint32_t raw) const {
    uint32_t op1 = (raw >> 23) & 0x1;
    if (op1 != 0) return std::nullopt; // exclusive -- simplified

    DecodedInstruction d(pc, raw);
    bool is_load = ((raw >> 22) & 0x1) == 1;

    d.opcode = is_load ? OpcodeType::LoadPair : OpcodeType::StorePair;

    uint32_t rt = raw & 0x1F;
    uint32_t rt2 = (raw >> 10) & 0x1F;
    uint32_t rn = (raw >> 5) & 0x1F;

    if (rt != 31) d.dst_regs.push_back(Reg(static_cast<uint8_t>(rt)));
    if (is_load && rt2 != 31) d.dst_regs.push_back(Reg(static_cast<uint8_t>(rt2)));
    if (rn != 31) d.src_regs.push_back(Reg(static_cast<uint8_t>(rn)));

    d.mem_access = MemAccess{0, 16, is_load};

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s x%u, x%u, [x%u]",
                  is_load ? "ldp" : "stp", rt, rt2, rn);
    d.disasm = buf;
    return d;
}

// =====================================================================
// AArch64Decoder -- data-processing register sub-decoders
// =====================================================================

std::optional<DecodedInstruction>
AArch64Decoder::decode_data_proc_reg_op(uint64_t pc, uint32_t raw) const {
    DecodedInstruction d(pc, raw);
    uint32_t opc = (raw >> 29) & 0x7;
    uint32_t is_64bit = (raw >> 31) & 0x1;
    uint32_t rd = raw & 0x1F;
    uint32_t rn = (raw >> 5) & 0x1F;
    uint32_t rm = (raw >> 16) & 0x1F;

    d.opcode = (opc == 0 || opc == 4) ? OpcodeType::Add :
               (opc == 1 || opc == 5) ? OpcodeType::Mul :
               (opc == 2 || opc == 6) ? OpcodeType::Sub :
               OpcodeType::Div;

    if (rd != 31) d.dst_regs.push_back(Reg(static_cast<uint8_t>(rd)));
    if (rn != 31) d.src_regs.push_back(Reg(static_cast<uint8_t>(rn)));
    if (rm != 31) d.src_regs.push_back(Reg(static_cast<uint8_t>(rm)));

    char prefix = is_64bit ? 'x' : 'w';
    char buf[64];
    const char* name = (opc == 0 || opc == 4) ? "add" :
                       (opc == 1 || opc == 5) ? "mul" :
                       (opc == 2 || opc == 6) ? "sub" : "div";
    std::snprintf(buf, sizeof(buf), "%s %c%u, %c%u, %c%u", name, prefix, rd, prefix, rn, prefix, rm);
    d.disasm = buf;
    return d;
}

std::optional<DecodedInstruction>
AArch64Decoder::decode_add_sub_ext(uint64_t pc, uint32_t raw) const {
    DecodedInstruction d(pc, raw);
    uint32_t is_sub = (raw >> 30) & 0x1;
    uint32_t is_64bit = (raw >> 31) & 0x1;
    uint32_t rd = raw & 0x1F;
    uint32_t rn = (raw >> 5) & 0x1F;
    uint32_t rm = (raw >> 16) & 0x1F;

    d.opcode = is_sub ? OpcodeType::Sub : OpcodeType::Add;

    if (rd != 31) d.dst_regs.push_back(Reg(static_cast<uint8_t>(rd)));
    if (rn != 31) d.src_regs.push_back(Reg(static_cast<uint8_t>(rn)));
    if (rm != 31) d.src_regs.push_back(Reg(static_cast<uint8_t>(rm)));

    char prefix = is_64bit ? 'x' : 'w';
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %c%u, %c%u, %c%u",
                  is_sub ? "sub" : "add", prefix, rd, prefix, rn, prefix, rm);
    d.disasm = buf;
    return d;
}

} // namespace arm_cpu::decoder
