#pragma once

/// @file load_store.hpp
/// @brief AArch64 load/store instruction decoder declarations.
///
/// Covers: LDR/STR (immediate, register offset), LDP/STP (pair),
/// atomic operations, exclusive load/store (LDAXR, STLXR).

#include "arm_cpu/decoder/aarch64/encoding.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::decoder::DecodedInstruction;

/// Decode load/store register (unsigned immediate).
Result<DecodedInstruction> decode_load_store_imm(uint64_t pc, uint32_t raw);

/// Decode load/store register (register offset).
Result<DecodedInstruction> decode_load_store_reg(uint64_t pc, uint32_t raw);

/// Decode load/store pair (LDP/STP).
Result<DecodedInstruction> decode_load_store_pair(uint64_t pc, uint32_t raw);

/// Decode atomic memory operations (LDADD, LDCLR, LDEOR, LDSET, LDSWP, CAS).
Result<DecodedInstruction> decode_atomic(uint64_t pc, uint32_t raw);

/// Decode exclusive load/store (LDAXR, STLXR).
Result<DecodedInstruction> decode_exclusive(uint64_t pc, uint32_t raw);

} // namespace arm_cpu::decoder::aarch64
