#pragma once

/// @file encoding.hpp
/// @brief AArch64 instruction encoding helpers: bit extraction, register decoding,
///        sign extension, condition/shift/extend name tables.

#include <cstdint>
#include <array>
#include <string_view>

namespace arm_cpu::decoder::aarch64 {

// =====================================================================
// Bit-field extraction
// =====================================================================

/// Extract bits [msb:lsb] from a 32-bit word.
[[nodiscard]] inline constexpr uint32_t extract_bits(uint32_t raw, uint32_t msb, uint32_t lsb) {
    uint32_t mask = (msb == 31) ? 0xFFFFFFFFu : ((1u << (msb + 1)) - 1u);
    return (raw >> lsb) & (mask >> lsb);
}

/// Sign-extend a `bits`-wide unsigned value to int64.
[[nodiscard]] inline constexpr int64_t sign_extend(uint32_t value, uint32_t bits) {
    uint32_t shift = 64 - bits;
    return static_cast<int64_t>(static_cast<uint64_t>(value) << shift) >> shift;
}

[[nodiscard]] inline constexpr int64_t sign_extend_19(uint32_t value) {
    return sign_extend(value, 19);
}

[[nodiscard]] inline constexpr int64_t sign_extend_26(uint32_t value) {
    return sign_extend(value, 26);
}

[[nodiscard]] inline constexpr int64_t sign_extend_19_shift(uint32_t value, uint32_t shift) {
    return sign_extend(value, 19) << static_cast<int64_t>(shift);
}

// =====================================================================
// Register field decoding
// =====================================================================

[[nodiscard]] inline constexpr uint8_t decode_rd(uint32_t raw) {
    return static_cast<uint8_t>(raw & 0x1F);
}

/// Alias for Rd, used for Rt (load/store target).
[[nodiscard]] inline constexpr uint8_t decode_rt(uint32_t raw) {
    return static_cast<uint8_t>(raw & 0x1F);
}

[[nodiscard]] inline constexpr uint8_t decode_rn(uint32_t raw) {
    return static_cast<uint8_t>((raw >> 5) & 0x1F);
}

[[nodiscard]] inline constexpr uint8_t decode_rm(uint32_t raw) {
    return static_cast<uint8_t>((raw >> 16) & 0x1F);
}

[[nodiscard]] inline constexpr uint8_t decode_ra(uint32_t raw) {
    return static_cast<uint8_t>((raw >> 10) & 0x1F);
}

// =====================================================================
// Immediate field decoding
// =====================================================================

[[nodiscard]] inline constexpr uint16_t decode_imm12(uint32_t raw) {
    return static_cast<uint16_t>((raw >> 10) & 0xFFF);
}

[[nodiscard]] inline constexpr uint32_t decode_imm19(uint32_t raw) {
    return (raw >> 5) & 0x7FFFF;
}

[[nodiscard]] inline constexpr uint32_t decode_imm26(uint32_t raw) {
    return raw & 0x3FFFFFF;
}

// =====================================================================
// Size / shift / option / condition
// =====================================================================

[[nodiscard]] inline constexpr bool is_64bit(uint32_t raw) {
    return ((raw >> 31) & 0x1) == 1;
}

[[nodiscard]] inline constexpr bool is_32bit(uint32_t raw) {
    return !is_64bit(raw);
}

[[nodiscard]] inline constexpr uint8_t decode_shift(uint32_t raw) {
    return static_cast<uint8_t>((raw >> 22) & 0x3);
}

[[nodiscard]] inline constexpr uint8_t decode_option(uint32_t raw) {
    return static_cast<uint8_t>((raw >> 22) & 0x3);
}

[[nodiscard]] inline constexpr uint8_t decode_size(uint32_t raw) {
    return static_cast<uint8_t>((raw >> 30) & 0x3);
}

[[nodiscard]] inline constexpr uint8_t decode_size2(uint32_t raw) {
    return static_cast<uint8_t>((raw >> 22) & 0x3);
}

[[nodiscard]] inline constexpr uint8_t decode_condition(uint32_t raw) {
    return static_cast<uint8_t>(raw & 0xF);
}

[[nodiscard]] inline constexpr uint8_t decode_extend(uint32_t raw) {
    return static_cast<uint8_t>((raw >> 20) & 0x7);
}

// =====================================================================
// SIMD helpers
// =====================================================================

[[nodiscard]] inline constexpr uint32_t decode_esize(uint32_t raw) {
    return 1u << decode_size(raw);
}

[[nodiscard]] inline constexpr uint8_t decode_vreg(uint32_t raw, uint32_t offset) {
    return static_cast<uint8_t>((raw >> offset) & 0x1F);
}

[[nodiscard]] inline constexpr bool decode_q_bit(uint32_t raw) {
    return ((raw >> 30) & 0x1) == 1;
}

[[nodiscard]] inline constexpr bool decode_scalar(uint32_t raw) {
    return ((raw >> 28) & 0x1) == 1;
}

// =====================================================================
// System register fields
// =====================================================================

[[nodiscard]] inline constexpr uint8_t decode_crm(uint32_t raw) {
    return static_cast<uint8_t>((raw >> 8) & 0xF);
}

[[nodiscard]] inline constexpr uint8_t decode_crn(uint32_t raw) {
    return static_cast<uint8_t>((raw >> 12) & 0xF);
}

[[nodiscard]] inline constexpr uint8_t decode_op1(uint32_t raw) {
    return static_cast<uint8_t>((raw >> 16) & 0x7);
}

[[nodiscard]] inline constexpr uint8_t decode_op2(uint32_t raw) {
    return static_cast<uint8_t>((raw >> 5) & 0x7);
}

// =====================================================================
// Name tables
// =====================================================================

inline constexpr std::array<std::string_view, 16> CONDITION_NAMES = {
    "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
    "hi", "ls", "ge", "lt", "gt", "le", "al", "nv",
};

[[nodiscard]] inline constexpr std::string_view condition_name(uint8_t cond) {
    return CONDITION_NAMES[cond];
}

inline constexpr std::array<std::string_view, 8> EXTEND_NAMES = {
    "uxtb", "uxth", "uxtw", "uxtx", "sxtb", "sxth", "sxtw", "sxtx",
};

[[nodiscard]] inline constexpr std::string_view extend_name(uint8_t ext) {
    return EXTEND_NAMES[ext];
}

inline constexpr std::array<std::string_view, 4> SHIFT_NAMES = {
    "lsl", "lsr", "asr", "ror",
};

[[nodiscard]] inline constexpr std::string_view shift_name(uint8_t shift) {
    return SHIFT_NAMES[shift];
}

} // namespace arm_cpu::decoder::aarch64
