#pragma once

/// @file arithmetic.hpp
/// @brief AArch64 integer arithmetic instruction decoder declarations.
///
/// Covers: ADD/SUB (immediate, shifted, extended), MUL/DIV, conditional
/// compare, and data-processing (1-source / 2-source).

#include "arm_cpu/decoder/aarch64/encoding.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::decoder::DecodedInstruction;

/// Decode add/subtract (immediate).
Result<DecodedInstruction> decode_add_sub_imm(uint64_t pc, uint32_t raw);

/// Decode add/subtract (shifted register).
Result<DecodedInstruction> decode_add_sub_shifted(uint64_t pc, uint32_t raw);

/// Decode add/subtract (extended register).
Result<DecodedInstruction> decode_add_sub_extended(uint64_t pc, uint32_t raw);

/// Decode multiply/divide instructions (MADD, MSUB, SMULH, UMULH, SDIV, UDIV, etc.).
Result<DecodedInstruction> decode_multiply_divide(uint64_t pc, uint32_t raw);

/// Decode conditional compare (CCMP, CCMN).
Result<DecodedInstruction> decode_cond_compare(uint64_t pc, uint32_t raw);

/// Decode data-processing (2 source) -- SDIV, UDIV, LSLV, LSRV, ASRV, RORV, CRC32.
Result<DecodedInstruction> decode_data_proc_2src(uint64_t pc, uint32_t raw);

/// Decode data-processing (1 source) -- RBIT, REV16/32/64, CLZ, CLS, CTZ.
Result<DecodedInstruction> decode_data_proc_1src(uint64_t pc, uint32_t raw);

} // namespace arm_cpu::decoder::aarch64
