#pragma once

/// @file aarch64_decoder.hpp
/// @brief Top-level AArch64 instruction decoder.
///
/// Provides `DecodedInstruction` (the result of decoding) and
/// `AArch64Decoder` (the main decoder class).
///
/// The decoder dispatches to specialized sub-decoders:
/// - arithmetic  -- integer add/sub/mul/div
/// - logical     -- AND, ORR, EOR
/// - load_store  -- LDR, STR, LDP, STP, atomic, exclusive
/// - branch      -- B, BL, B.cond, BR, BLR, RET, CBZ, TBZ
/// - simd_neon   -- vector arithmetic, load/store, permutation
/// - fp          -- floating-point arithmetic, compare, convert
/// - crypto      -- AES, SHA, CRC32, PMULL
/// - system      -- MSR, MRS, barriers, cache maintenance

#include "arm_cpu/types.hpp"
#include "arm_cpu/error.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace arm_cpu::decoder {

// =====================================================================
// DecodedInstruction
// =====================================================================

/// Result of decoding a single AArch64 instruction.
struct DecodedInstruction {
    uint64_t                  pc{};
    uint32_t                  raw{};
    OpcodeType                opcode{OpcodeType::Other};
    std::vector<Reg>          src_regs;
    std::vector<Reg>          dst_regs;
    std::vector<VReg>         src_vregs;
    std::vector<VReg>         dst_vregs;
    std::optional<int64_t>    immediate;
    std::optional<MemAccess>  mem_access;
    std::optional<BranchInfo> branch_info;
    std::string               disasm;

    /// Default constructor.
    DecodedInstruction() = default;

    /// Create with PC and raw encoding.
    DecodedInstruction(uint64_t pc_, uint32_t raw_)
        : pc(pc_), raw(raw_) {}

    /// Convert to an Instruction suitable for the emulator pipeline.
    [[nodiscard]] Instruction to_instruction(InstructionId id) const {
        Instruction instr(id, pc, raw, opcode);
        for (auto r : src_regs)  instr.with_src_reg(r);
        for (auto r : dst_regs)  instr.with_dst_reg(r);
        for (auto r : src_vregs) instr.with_src_vreg(r);
        for (auto r : dst_vregs) instr.with_dst_vreg(r);
        if (mem_access) {
            instr.with_mem_access(mem_access->addr, mem_access->size, mem_access->is_load);
        }
        if (branch_info) {
            instr.with_branch(branch_info->target,
                              branch_info->is_conditional,
                              branch_info->is_taken);
        }
        instr.with_disasm(disasm);
        return instr;
    }
};

// =====================================================================
// AArch64Decoder
// =====================================================================

/// Main AArch64 instruction decoder.
///
/// Usage:
/// @code
///   AArch64Decoder decoder;
///   auto result = decoder.decode(pc, raw_instruction);
///   if (result.ok()) { ... }
/// @endcode
class AArch64Decoder {
public:
    /// Create a detailed decoder (full disassembly).
    AArch64Decoder() : detailed_(true) {}

    /// Create a fast decoder that only extracts essential info.
    static AArch64Decoder fast() { return AArch64Decoder(false); }

    /// Decode a single 32-bit instruction at the given PC.
    ///
    /// On success returns `DecodedInstruction`.
    /// If the encoding is completely unknown, still returns a decoded
    /// instruction with `OpcodeType::Other`.
    [[nodiscard]] Result<DecodedInstruction> decode(uint64_t pc, uint32_t raw) const;

private:
    explicit AArch64Decoder(bool detailed) : detailed_(detailed) {}

    bool detailed_;

    // --- internal dispatch helpers ---
    [[nodiscard]] std::optional<DecodedInstruction>
        try_data_proc_imm(uint64_t pc, uint32_t raw) const;

    [[nodiscard]] std::optional<DecodedInstruction>
        try_branch(uint64_t pc, uint32_t raw) const;

    [[nodiscard]] std::optional<DecodedInstruction>
        try_load_store(uint64_t pc, uint32_t raw) const;

    [[nodiscard]] std::optional<DecodedInstruction>
        try_data_proc_reg(uint64_t pc, uint32_t raw) const;

    [[nodiscard]] std::optional<DecodedInstruction>
        try_simd_fp(uint64_t pc, uint32_t raw) const;

    [[nodiscard]] std::optional<DecodedInstruction>
        try_system(uint64_t pc, uint32_t raw) const;

    // --- sub-decoders called from dispatch ---
    [[nodiscard]] std::optional<DecodedInstruction>
        decode_pc_rel(uint64_t pc, uint32_t raw) const;

    [[nodiscard]] std::optional<DecodedInstruction>
        decode_add_sub_imm(uint64_t pc, uint32_t raw) const;

    [[nodiscard]] std::optional<DecodedInstruction>
        decode_logical_imm(uint64_t pc, uint32_t raw) const;

    [[nodiscard]] std::optional<DecodedInstruction>
        decode_move_wide(uint64_t pc, uint32_t raw) const;

    [[nodiscard]] std::optional<DecodedInstruction>
        decode_bitfield(uint64_t pc, uint32_t raw) const;

    [[nodiscard]] std::optional<DecodedInstruction>
        decode_extract(uint64_t pc, uint32_t raw) const;

    [[nodiscard]] std::optional<DecodedInstruction>
        decode_load_store_reg(uint64_t pc, uint32_t raw) const;

    [[nodiscard]] std::optional<DecodedInstruction>
        decode_load_store_pair_or_excl(uint64_t pc, uint32_t raw) const;

    [[nodiscard]] std::optional<DecodedInstruction>
        decode_data_proc_reg_op(uint64_t pc, uint32_t raw) const;

    [[nodiscard]] std::optional<DecodedInstruction>
        decode_add_sub_ext(uint64_t pc, uint32_t raw) const;
};

} // namespace arm_cpu::decoder
