#pragma once

/// @file system.hpp
/// @brief AArch64 system instruction decoder declarations.
///
/// Covers: MRS, MSR, SYS, SYSL, HINT (NOP, YIELD, WFE, WFI, SEV),
/// DMB, DSB, ISB (barriers), DC/IC (cache maintenance),
/// ERET, SVC, HVC, SMC, BRK.

#include "arm_cpu/decoder/aarch64/encoding.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::decoder::DecodedInstruction;

/// Decode system register access (MRS / MSR).
Result<DecodedInstruction> decode_sys_reg(uint64_t pc, uint32_t raw);

/// Decode system instruction (SYS / SYSL).
Result<DecodedInstruction> decode_sys_instr(uint64_t pc, uint32_t raw);

/// Decode HINT instructions (NOP, YIELD, WFE, WFI, SEV, SEVL).
Result<DecodedInstruction> decode_hint(uint64_t pc, uint32_t raw);

/// Decode memory barrier instructions (DMB, DSB, ISB).
Result<DecodedInstruction> decode_barrier(uint64_t pc, uint32_t raw);

/// Decode data cache maintenance instructions (DC CIVAC, DC CVAC, DC ZVA, etc.).
Result<DecodedInstruction> decode_dc(uint64_t pc, uint32_t raw);

/// Decode instruction cache maintenance instructions (IC IVAU, IC IALLU, IC IALLUIS).
Result<DecodedInstruction> decode_ic(uint64_t pc, uint32_t raw);

/// Decode exception return (ERET).
Result<DecodedInstruction> decode_exception_return(uint64_t pc, uint32_t raw);

/// Decode exception generating instructions (SVC, HVC, SMC, BRK).
Result<DecodedInstruction> decode_exception_gen(uint64_t pc, uint32_t raw);

} // namespace arm_cpu::decoder::aarch64
