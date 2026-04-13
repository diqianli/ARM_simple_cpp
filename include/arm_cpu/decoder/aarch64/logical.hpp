#pragma once

/// @file logical.hpp
/// @brief AArch64 logical/bitwise instruction decoder declarations.
///
/// Covers: AND, ORR, EOR, ANDS, BIC, ORN (both immediate and shifted-register).

#include "arm_cpu/decoder/aarch64/encoding.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::decoder::DecodedInstruction;

/// Decode logical (immediate) -- AND, ORR, EOR, ANDS.
Result<DecodedInstruction> decode_logical_imm(uint64_t pc, uint32_t raw);

/// Decode logical (shifted register) -- AND, ORR, EOR, ANDS, BIC, ORN.
Result<DecodedInstruction> decode_logical_shifted(uint64_t pc, uint32_t raw);

} // namespace arm_cpu::decoder::aarch64
