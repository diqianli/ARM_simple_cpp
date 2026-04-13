#pragma once

/// @file branch.hpp
/// @brief AArch64 branch and control-flow instruction decoder declarations.
///
/// Covers: B, BL, B.cond, BR, BLR, RET, CBZ, CBNZ, TBZ, TBNZ.

#include "arm_cpu/decoder/aarch64/encoding.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::decoder::DecodedInstruction;

/// Decode unconditional branch immediate (B / BL).
Result<DecodedInstruction> decode_branch_imm(uint64_t pc, uint32_t raw);

/// Decode conditional branch immediate (B.cond).
Result<DecodedInstruction> decode_branch_cond(uint64_t pc, uint32_t raw);

/// Decode branch register (BR, BLR, RET, ERET, DRPS).
Result<DecodedInstruction> decode_branch_reg(uint64_t pc, uint32_t raw);

/// Decode compare-and-branch (CBZ / CBNZ).
Result<DecodedInstruction> decode_compare_branch(uint64_t pc, uint32_t raw);

/// Decode test-bit-and-branch (TBZ / TBNZ).
Result<DecodedInstruction> decode_test_branch(uint64_t pc, uint32_t raw);

} // namespace arm_cpu::decoder::aarch64
