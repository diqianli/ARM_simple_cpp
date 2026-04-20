#pragma once

/// @file elf_trace.hpp
/// @brief ELF trace parser: loads ARM64 ELF, decodes instructions via Capstone.
///
/// Uses ElfLoader to parse the ELF file and CapstoneDecoder to decode each
/// 32-bit instruction in executable segments into the emulator's Instruction
/// format. Suitable for direct ELF input (-f elf).

#include "arm_cpu/input/instruction_source.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace arm_cpu {

class ElfLoader;
namespace decoder { class CapstoneDecoder; }

/// Instruction source backed by an ARM64 ELF file.
///
/// On construction, the ELF is loaded and all instructions in executable
/// segments are decoded into a flat vector. `next()` then streams them out.
class ElfTraceParser : public InstructionSource {
public:
    /// Load and decode an ELF file.
    /// Returns an error if the file cannot be opened or is not a valid ARM64 ELF.
    /// When max_instructions > 0 and the trace is shorter, the trace will loop
    /// to provide enough instructions for the simulation.
    static Result<ElfTraceParser> from_file(
        const std::string& path,
        std::size_t max_instructions = 0);

    /// Reset to the beginning of the instruction stream.
    Result<void> reset() override;

    /// Total number of decoded instructions.
    std::optional<std::size_t> total_count() const override;

private:
    ElfTraceParser(std::vector<Instruction> instructions, std::string path);

    Result<std::optional<Instruction>> next_impl() override;

    std::vector<Instruction> instructions_;
    std::size_t cursor_ = 0;
    std::string file_path_;
    bool loop_ = false;  // 是否循环回放 trace
    uint64_t next_unique_id_ = 0;  // 循环回放时分配全局唯一 ID
    std::size_t loop_count_ = 0;   // 已循环次数（0 = 首次遍历）
};

} // namespace arm_cpu
