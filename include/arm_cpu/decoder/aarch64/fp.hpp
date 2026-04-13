#pragma once

/// @file fp.hpp
/// @brief AArch64 floating-point instruction decoder declarations.
///
/// Covers: FADD, FSUB, FMUL, FDIV, FMA/FMS/FNMA/FNMS, FCMP, FCCMP,
/// FMOV, FABS, FNEG, FSQRT, FCVT, FRINT variants.

#include "arm_cpu/decoder/aarch64/encoding.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::decoder::DecodedInstruction;

/// Decode floating-point arithmetic (FADD, FSUB, FMUL, FDIV, FMADD, etc.).
Result<DecodedInstruction> decode_fp_arith(uint64_t pc, uint32_t raw);

/// Decode floating-point compare (FCMP, FCMPE).
Result<DecodedInstruction> decode_fp_compare(uint64_t pc, uint32_t raw);

/// Decode floating-point data-processing 1-source (FMOV, FABS, FNEG, FSQRT, FCVT).
Result<DecodedInstruction> decode_fp_1src(uint64_t pc, uint32_t raw);

/// Decode floating-point conditional compare (FCCMP).
Result<DecodedInstruction> decode_fp_cond_compare(uint64_t pc, uint32_t raw);

} // namespace arm_cpu::decoder::aarch64
