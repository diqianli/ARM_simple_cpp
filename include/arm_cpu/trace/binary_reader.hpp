#pragma once

/// @file binary_reader.hpp
/// @brief Binary trace reader with streaming support for large files.

#include "arm_cpu/trace/binary_format.hpp"
#include "arm_cpu/types.hpp"
#include "arm_cpu/error.hpp"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace arm_cpu {

// =====================================================================
// BinaryTraceReader
// =====================================================================
class BinaryTraceReader {
public:
    /// Open a trace file.
    static Result<BinaryTraceReader> open(const std::string& path);

    /// Get the file header.
    const FileHeader& header() const { return header_; }

    /// Get total instruction count.
    uint64_t instr_count() const { return header_.instr_count; }

    /// Check if random access is available.
    bool has_index() const { return !index_.empty(); }

    /// Seek to a specific instruction by ID (requires index).
    bool seek_to_instruction(uint64_t instr_id);

    /// Read the next instruction.
    Result<std::optional<Instruction>> read_next();

    /// Read a range of instructions.
    Result<std::vector<Instruction>> read_range(uint64_t start, uint64_t end);

    /// Stream all remaining instructions.
    std::vector<Instruction> stream_all();

    /// Get all string table entries.
    const std::vector<std::string>& string_table() const { return string_table_; }

private:
    BinaryTraceReader(FILE* file, FileHeader header,
                      std::vector<std::string> strings,
                      std::vector<IndexEntry> index);

    FILE* file_ = nullptr;
    FileHeader header_;
    std::vector<std::string> string_table_;
    std::vector<IndexEntry> index_;
    uint64_t current_position_ = 0;
    uint64_t instructions_read_ = 0;
    uint64_t last_pc_ = 0;

    Result<uint8_t> read_byte();
    Result<uint16_t> read_u16_le();
    Result<uint32_t> read_u32_le();
    Result<uint64_t> read_u64_le();
    Result<uint64_t> read_varint();
    Result<int64_t> read_signed_varint();
};

} // namespace arm_cpu
