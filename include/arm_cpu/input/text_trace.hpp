#pragma once

/// @file text_trace.hpp
/// @brief Text trace format parser for instruction traces.
///
/// Parses human-readable text traces with two supported formats:
///   Format 1 (structured): PC OPCODE_TYPE [src_regs] [dst_regs] [mem_addr] [branch_info]
///   Format 2 (disassembly): PC: DISASSEMBLY
///
/// Analogous to Rust's `input::text_trace::TextTraceParser`.

#include "arm_cpu/input/instruction_source.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace arm_cpu {

/// Text trace format parser.
///
/// Reads text trace files line-by-line and parses each line into an
/// Instruction. Supports both structured and disassembly formats.
class TextTraceParser final : public InstructionSource {
public:
    /// Open and parse a text trace file.
    static Result<TextTraceParser> from_file(const std::string& path);

    TextTraceParser(TextTraceParser&&) noexcept = default;
    TextTraceParser& operator=(TextTraceParser&&) noexcept = default;

    // Non-copyable
    TextTraceParser(const TextTraceParser&) = delete;
    TextTraceParser& operator=(const TextTraceParser&) = delete;

    std::optional<std::size_t> total_count() const override { return std::nullopt; }

    Result<void> reset() override;

protected:
    Result<std::optional<Instruction>> next_impl() override;

private:
    TextTraceParser(std::ifstream stream, std::string file_path)
        : reader_(std::move(stream))
        , current_id_(0)
        , line_buffer_()
        , file_path_(std::move(file_path)) {}

    /// Parse a single line into an instruction (may return nullopt for blanks/comments).
    Result<std::optional<Instruction>> parse_line(std::string_view line);

    /// Parse disassembly format (PC: MNEMONIC operands...)
    Result<Instruction> parse_disassembly(uint64_t pc, std::string_view disasm);

    /// Parse a hexadecimal string to u64.
    static Result<uint64_t> parse_hex(std::string_view s);

    /// Parse opcode type from a string like "ADD", "LDR", "B.EQ", etc.
    static Result<OpcodeType> parse_opcode_type(std::string_view s);

    /// Convert a mnemonic to an OpcodeType (ignoring conditional suffixes).
    static OpcodeType mnemonic_to_opcode(std::string_view mnemonic);

    /// Parse a register string like "X0", "W15", "R30" into a Reg.
    static std::optional<Reg> parse_reg(std::string_view s);

    /// Extract a memory operand pattern like [Xn, #offset] from a string.
    static std::optional<std::string> extract_memory_operand(std::string_view s);

    std::ifstream reader_;
    uint64_t current_id_;
    std::string line_buffer_;
    std::string file_path_;
};

} // namespace arm_cpu
