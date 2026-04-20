/// @file elf_trace.cpp
/// @brief Implementation of the ELF trace parser.
///
/// Uses FunctionalSim to perform a PC-based dynamic trace generation
/// with perfect branch prediction, instead of linearly scanning all
/// executable segments.

#include "arm_cpu/input/elf_trace.hpp"
#include "arm_cpu/elf/elf_loader.hpp"
#include "arm_cpu/input/functional_sim.hpp"

#include <cstring>

namespace arm_cpu {

// =====================================================================
// ElfTraceParser::from_file
// =====================================================================

Result<ElfTraceParser> ElfTraceParser::from_file(
    const std::string& path, std::size_t max_instructions)
{
    // Step 1: Load ELF
    auto elf_result = ElfLoader::load(path);
    if (elf_result.has_error()) {
        return Err(EmulatorError::trace_parse(
            "Failed to load ELF: " + elf_result.error().message()));
    }
    auto& elf = elf_result.value();

    // Verify it's ARM64
    if (elf.header().machine != 0xB7) {
        return Err(EmulatorError::trace_parse(
            "Not an AArch64 ELF (EM_AARCH64=0xB7, got 0x" +
            std::to_string(elf.header().machine) + ")"));
    }

    // Step 2: Run functional simulation to generate dynamic trace
    // This performs PC-based execution with perfect branch prediction.
    FunctionalSim::Config sim_config;
    if (max_instructions > 0) {
        sim_config.max_instructions = std::max(
            sim_config.max_instructions,
            static_cast<uint64_t>(max_instructions));
    }
    auto trace_result = FunctionalSim::run(elf, sim_config);
    if (trace_result.has_error()) {
        return Err(EmulatorError::trace_parse(
            "Functional simulation failed: " + trace_result.error().message()));
    }
    auto instructions = std::move(trace_result.value());

    if (instructions.empty()) {
        return Err(EmulatorError::trace_parse(
            "No instructions executed from entry point 0x" +
            std::to_string(elf.entry_point())));
    }

    auto parser = ElfTraceParser(std::move(instructions), path);
    // 如果用户要求更多指令但 trace 不足，启用循环回放
    if (max_instructions > 0 && parser.instructions_.size() < max_instructions) {
        parser.loop_ = true;
        parser.next_unique_id_ = static_cast<uint64_t>(parser.instructions_.size());
    }
    return Ok(std::move(parser));
}

// =====================================================================
// ElfTraceParser::ElfTraceParser (private)
// =====================================================================

ElfTraceParser::ElfTraceParser(std::vector<Instruction> instructions, std::string path)
    : instructions_(std::move(instructions)), file_path_(std::move(path)) {}

// =====================================================================
// ElfTraceParser::reset
// =====================================================================

Result<void> ElfTraceParser::reset() {
    cursor_ = 0;
    return Ok();
}

// =====================================================================
// ElfTraceParser::total_count
// =====================================================================

std::optional<std::size_t> ElfTraceParser::total_count() const {
    return instructions_.size();
}

// =====================================================================
// ElfTraceParser::next_impl
// =====================================================================

Result<std::optional<Instruction>> ElfTraceParser::next_impl() {
    if (cursor_ >= instructions_.size()) {
        if (loop_ && !instructions_.empty()) {
            cursor_ = 0;  // 循环回放
            loop_count_++;
        } else {
            return Ok(std::optional<Instruction>{std::nullopt});
        }
    }
    auto instr = instructions_[cursor_++];
    if (loop_count_ > 0) {
        // 循环回放时分配全局唯一 ID，避免与已提交指令的 ID 冲突
        instr.id = InstructionId(next_unique_id_++);
    }
    return Ok(std::optional<Instruction>{std::move(instr)});
}

} // namespace arm_cpu
