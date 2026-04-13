#pragma once

/// @file crypto.hpp
/// @brief AArch64 cryptography extension instruction decoder declarations.
///
/// Covers: AES (AESE, AESD, AESMC, AESIMC), SHA-1, SHA-256, SHA-512,
/// SHA-3 (EOR3, RAX1, XAR, BCAX), CRC32, PMULL.

#include "arm_cpu/decoder/aarch64/encoding.hpp"
#include "arm_cpu/decoder/aarch64_decoder.hpp"

namespace arm_cpu::decoder::aarch64 {

using arm_cpu::decoder::DecodedInstruction;

/// Decode AES instructions (AESE, AESD, AESMC, AESIMC).
Result<DecodedInstruction> decode_aes(uint64_t pc, uint32_t raw);

/// Decode SHA-1 instructions (SHA1C, SHA1P, SHA1M, SHA1H, SHA1SU0, SHA1SU1).
Result<DecodedInstruction> decode_sha1(uint64_t pc, uint32_t raw);

/// Decode SHA-256 instructions (SHA256H, SHA256H2, SHA256SU0, SHA256SU1).
Result<DecodedInstruction> decode_sha256(uint64_t pc, uint32_t raw);

/// Decode SHA-512 instructions (SHA512H, SHA512H2, SHA512SU0, SHA512SU1).
Result<DecodedInstruction> decode_sha512(uint64_t pc, uint32_t raw);

/// Decode SHA-3 three-register XOR instructions (EOR3, RAX1, XAR, BCAX).
Result<DecodedInstruction> decode_sha3(uint64_t pc, uint32_t raw);

/// Decode CRC32 instructions (CRC32B, CRC32H, CRC32W, CRC32X, CRC32CB, etc.).
Result<DecodedInstruction> decode_crc32(uint64_t pc, uint32_t raw);

/// Decode polynomial multiply instructions (PMULL, PMULL2).
Result<DecodedInstruction> decode_pmull(uint64_t pc, uint32_t raw);

} // namespace arm_cpu::decoder::aarch64
