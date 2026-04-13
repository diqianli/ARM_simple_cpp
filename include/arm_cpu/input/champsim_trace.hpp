#pragma once

/// @file champsim_trace.hpp
/// @brief ChampSim trace format parser for SPEC CPU 2017 validation.
///
/// ChampSim trace format is a binary format used by the ChampSim simulator.
/// Each instruction is stored as 64 bytes:
///   8 bytes: instruction pointer (PC)
///   1 byte:  is_branch
///   1 byte:  branch_taken
///   2 bytes: destination_registers[2]
///   4 bytes: source_registers[4]
///  16 bytes: destination_memory[2] (8 bytes each)
///  32 bytes: source_memory[4] (8 bytes each)
///
/// Analogous to Rust's `input::champsim_trace::{ChampSimTraceParser, ChampSimXzTraceParser}`.

#include "arm_cpu/input/instruction_source.hpp"
#include "arm_cpu/types.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <string>

namespace arm_cpu {

/// Parsed ChampSim instruction (unpacked from the 64-byte record).
struct ChampSimInstr {
    uint64_t ip = 0;
    bool is_branch = false;
    bool branch_taken = false;
    std::array<uint8_t, 2> destination_registers{};
    std::array<uint8_t, 4> source_registers{};
    std::array<uint64_t, 2> destination_memory{};
    std::array<uint64_t, 4> source_memory{};

    /// Parse from a 64-byte buffer.
    static ChampSimInstr from_bytes(const std::array<uint8_t, 64>& buf);

    /// Check if this instruction has any memory access.
    bool has_memory_access() const;
};

/// ChampSim trace parser (uncompressed).
class ChampSimTraceParser final : public InstructionSource {
public:
    static constexpr std::size_t INSTR_SIZE = 64;

    /// Open a ChampSim trace file.
    static Result<ChampSimTraceParser> from_file(const std::string& path);

    ChampSimTraceParser(ChampSimTraceParser&&) noexcept = default;
    ChampSimTraceParser& operator=(ChampSimTraceParser&&) noexcept = default;

    ChampSimTraceParser(const ChampSimTraceParser&) = delete;
    ChampSimTraceParser& operator=(const ChampSimTraceParser&) = delete;

    /// Number of instructions read so far.
    uint64_t instructions_read() const noexcept { return instructions_read_; }

    std::optional<std::size_t> total_count() const override { return std::nullopt; }

    Result<void> reset() override;

protected:
    Result<std::optional<Instruction>> next_impl() override;

private:
    ChampSimTraceParser(std::ifstream stream, std::string file_path)
        : reader_(std::move(stream))
        , current_id_(0)
        , instructions_read_(0)
        , file_path_(std::move(file_path)) {}

    Result<std::optional<ChampSimInstr>> read_instruction();

    /// Convert ChampSim instruction to our Instruction type.
    Instruction convert_instruction(const ChampSimInstr& cs_instr);

    std::ifstream reader_;
    uint64_t current_id_;
    uint64_t instructions_read_;
    std::string file_path_;
    std::array<uint8_t, INSTR_SIZE> buffer_{};
};

/// XZ-compressed ChampSim trace parser.
///
/// Note: In C++ this requires liblzma (commonly available on Linux/macOS).
/// The implementation conditionally uses `popen("xz -dc")` as a portable
/// fallback when liblzma headers are not available.
class ChampSimXzTraceParser final : public InstructionSource {
public:
    static constexpr std::size_t INSTR_SIZE = 64;

    /// Open an XZ-compressed ChampSim trace file.
    static Result<ChampSimXzTraceParser> from_file(const std::string& path);

    ChampSimXzTraceParser(ChampSimXzTraceParser&&) noexcept = default;
    ChampSimXzTraceParser& operator=(ChampSimXzTraceParser&&) noexcept = default;

    ChampSimXzTraceParser(const ChampSimXzTraceParser&) = delete;
    ChampSimXzTraceParser& operator=(const ChampSimXzTraceParser&) = delete;

    /// Number of instructions read so far.
    uint64_t instructions_read() const noexcept { return instructions_read_; }

    std::optional<std::size_t> total_count() const override { return std::nullopt; }

    Result<void> reset() override;

protected:
    Result<std::optional<Instruction>> next_impl() override;

private:
    ChampSimXzTraceParser(std::string file_path)
        : current_id_(0)
        , instructions_read_(0)
        , file_path_(std::move(file_path))
        , pipe_(nullptr) {}

    Result<std::optional<ChampSimInstr>> read_instruction();

    /// Convert ChampSim instruction to our Instruction type.
    Instruction convert_instruction(const ChampSimInstr& cs_instr);

    uint64_t current_id_;
    uint64_t instructions_read_;
    std::string file_path_;
    void* pipe_; // FILE* from popen, stored as void* to avoid stdio in header
};

} // namespace arm_cpu
