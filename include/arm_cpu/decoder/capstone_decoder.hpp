#pragma once

/// @file capstone_decoder.hpp
/// @brief Capstone-based AArch64 instruction decoder with SVE/SME support.
///
/// Uses the Capstone disassembly engine (v5) to decode ARM64 instructions
/// into the emulator's internal Instruction format. Supports ARMv8.0 through
/// ARMv8.7-A, SVE, SVE2, and SME extensions.

#include "arm_cpu/types.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"
#include "arm_cpu/error.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

struct cs_insn;

namespace arm_cpu::decoder {

/// Pre-computed mnemonic→OpcodeType lookup table.
struct MnemonicLookup {
    std::unordered_map<std::string_view, OpcodeType> table;

    MnemonicLookup();

    /// Look up mnemonic, return OpcodeType::Other if not found.
    OpcodeType lookup(std::string_view mnemonic) const;
};

/// Capstone-based AArch64 decoder.
///
/// Decodes raw 32-bit ARM64 instructions into DecodedInstruction using
/// the Capstone engine. Maps Capstone's mnemonic + operand details to
/// the emulator's OpcodeType classification and register operands.
///
/// Usage:
/// @code
///   CapstoneDecoder decoder;
///   if (!decoder.init()) { ... }
///   auto result = decoder.decode(pc, raw_instruction);
///   auto instr = result.to_instruction(id);
/// @endcode
class CapstoneDecoder {
public:
    CapstoneDecoder();
    ~CapstoneDecoder();

    // Non-copyable (owns Capstone handle)
    CapstoneDecoder(const CapstoneDecoder&) = delete;
    CapstoneDecoder& operator=(const CapstoneDecoder&) = delete;
    CapstoneDecoder(CapstoneDecoder&&) noexcept;
    CapstoneDecoder& operator=(CapstoneDecoder&&) noexcept;

    /// Initialize the Capstone engine for AArch64 + SVE/SME.
    /// Returns true on success.
    bool init();

    /// Decode a single 32-bit instruction at the given PC.
    /// Returns DecodedInstruction with OpcodeType::Other if decoding fails.
    [[nodiscard]] DecodedInstruction decode(uint64_t pc, uint32_t raw) const;

    /// Check if the decoder is initialized and ready.
    [[nodiscard]] bool is_initialized() const { return handle_ != 0; }

private:
    size_t handle_ = 0;  // csh handle (size_t in Capstone v5)

    /// Mnemonics that fell through to OpcodeType::Other (for diagnostics).
    mutable std::unordered_set<std::string> reported_undefined_;

    static const MnemonicLookup lookup_;

    /// Map a Capstone mnemonic string to an OpcodeType.
    [[nodiscard]] OpcodeType map_mnemonic(std::string_view mnemonic) const;

    /// Extract register operands from a decoded Capstone instruction.
    void extract_operands(const cs_insn* insn, DecodedInstruction& out) const;

    /// Determine if a decoded instruction is a conditional branch.
    [[nodiscard]] bool is_conditional_branch(std::string_view mnemonic) const;
};

} // namespace arm_cpu::decoder
