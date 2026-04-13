#pragma once

/// @file simd_neon.hpp
/// @brief AArch64 SIMD/NEON instruction decoder declarations.
///
/// Covers: vector arithmetic (ADD, SUB, MUL), multiply-accumulate (MLA, MLS),
/// load/store multiple structures (LD1-4, ST1-4), permutation (ZIP, UZP, TRN, EXT),
/// saturating operations, and pairwise operations.

#include "arm_cpu/decoder/aarch64/encoding.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::decoder::DecodedInstruction;

/// Decode SIMD vector arithmetic (ADD, SUB, MUL, MLA, MLS).
Result<DecodedInstruction> decode_simd_arith(uint64_t pc, uint32_t raw);

/// Decode SIMD load/store multiple structures (LD1-4, ST1-4).
Result<DecodedInstruction> decode_simd_load_store(uint64_t pc, uint32_t raw);

/// Decode SIMD permutation (ZIP, UZP, TRN, EXT).
Result<DecodedInstruction> decode_simd_permute(uint64_t pc, uint32_t raw);

/// Decode SIMD saturating arithmetic (SQADD, SQSUB, etc.).
Result<DecodedInstruction> decode_simd_saturating(uint64_t pc, uint32_t raw);

/// Decode SIMD pairwise operations (ADDP, etc.).
Result<DecodedInstruction> decode_simd_pairwise(uint64_t pc, uint32_t raw);

} // namespace arm_cpu::decoder::aarch64
