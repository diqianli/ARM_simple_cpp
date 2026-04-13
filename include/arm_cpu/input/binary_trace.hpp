#pragma once

/// @file binary_trace.hpp
/// @brief Binary trace format parser for instruction traces.
///
/// Binary format specification:
///   Header:  8 bytes magic + 8 bytes version + 8 bytes count  (24 bytes total)
///   Each instruction:
///     8 bytes: PC (u64)
///     4 bytes: raw opcode (u32)
///     4 bytes: opcode type (u32)
///     1 byte:  src_reg_count
///     src_reg_count bytes: src registers (u8 each)
///     1 byte:  dst_reg_count
///     dst_reg_count bytes: dst registers (u8 each)
///     1 byte:  flags (bit 0: has_mem, bit 1: has_branch)
///     if has_mem:   8 bytes addr + 1 byte size + 1 byte is_load
///     if has_branch: 8 bytes target + 1 byte is_conditional + 1 byte is_taken
///
/// Analogous to Rust's `input::binary_trace::BinaryTraceParser`.

#include "arm_cpu/input/instruction_source.hpp"
#include "arm_cpu/types.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace arm_cpu {

/// Binary trace file header.
struct TraceHeader {
    uint64_t magic;
    uint64_t version;
    uint64_t instruction_count;
};

/// Binary trace parser.
class BinaryTraceParser final : public InstructionSource {
public:
    static constexpr uint64_t MAGIC = 0x41524D5F54524143ULL; // "ARM_TRAC"
    static constexpr uint64_t VERSION = 1;
    static constexpr std::size_t HEADER_SIZE = 24;

    /// Open and validate a binary trace file.
    static Result<BinaryTraceParser> from_file(const std::string& path);

    BinaryTraceParser(BinaryTraceParser&&) noexcept = default;
    BinaryTraceParser& operator=(BinaryTraceParser&&) noexcept = default;

    BinaryTraceParser(const BinaryTraceParser&) = delete;
    BinaryTraceParser& operator=(const BinaryTraceParser&) = delete;

    /// Total instruction count from the file header.
    std::optional<std::size_t> total_count() const override {
        return static_cast<std::size_t>(header_.instruction_count);
    }

    Result<void> reset() override;

protected:
    Result<std::optional<Instruction>> next_impl() override;

private:
    BinaryTraceParser(std::ifstream stream, TraceHeader header, std::string file_path)
        : reader_(std::move(stream))
        , header_(header)
        , current_id_(0)
        , instructions_read_(0)
        , file_path_(std::move(file_path)) {}

    /// Read and validate the 24-byte trace header.
    static Result<TraceHeader> read_header(std::ifstream& reader);

    /// Read a single instruction from the binary stream.
    Result<std::optional<Instruction>> read_instruction();

    /// Convert u32 encoding to OpcodeType.
    static OpcodeType u32_to_opcode_type(uint32_t v);

    /// Read exactly `n` bytes from the stream.
    Result<void> read_bytes(void* buf, std::size_t n);

    std::ifstream reader_;
    TraceHeader header_;
    uint64_t current_id_;
    uint64_t instructions_read_;
    std::string file_path_;
};

} // namespace arm_cpu
