/// @file elf_trace.cpp
/// @brief Implementation of the ELF trace parser.

#include "arm_cpu/input/elf_trace.hpp"
#include "arm_cpu/elf/elf_loader.hpp"
#include "arm_cpu/decoder/capstone_decoder.hpp"

#include <cstring>

namespace arm_cpu {

// =====================================================================
// ElfTraceParser::from_file
// =====================================================================

Result<ElfTraceParser> ElfTraceParser::from_file(const std::string& path) {
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

    // Step 2: Initialize Capstone
    decoder::CapstoneDecoder capstone;
    if (!capstone.init()) {
        return Err(EmulatorError::trace_parse("Capstone initialization failed"));
    }

    // Step 3: Decode instructions from executable segments
    auto exe_segments = elf.executable_segments();
    std::vector<Instruction> instructions;

    for (const auto* seg : exe_segments) {
        if (!seg || seg->size < 4) continue;

        for (std::size_t offset = 0; offset + 4 <= seg->size; offset += 4) {
            uint64_t pc = seg->vaddr + offset;
            // Byte-by-byte read to avoid alignment/aliasing issues
            const uint8_t* p = seg->data.data() + offset;
            uint32_t raw = static_cast<uint32_t>(p[0])
                         | (static_cast<uint32_t>(p[1]) << 8)
                         | (static_cast<uint32_t>(p[2]) << 16)
                         | (static_cast<uint32_t>(p[3]) << 24);

            auto decoded = capstone.decode(pc, raw);
            auto instr = decoded.to_instruction(InstructionId(instructions.size()));
            instructions.push_back(std::move(instr));
        }
    }

    if (instructions.empty()) {
        return Err(EmulatorError::trace_parse(
            "No instructions found in ELF executable segments"));
    }

    // Step 4: Replace trailing infinite-loop branches with NOPs so simulation
    // can terminate. Scan backwards for unconditional backward branches.
    for (int i = static_cast<int>(instructions.size()) - 1; i >= 0; --i) {
        auto& instr = instructions[i];
        if (instr.branch_info.has_value() && !instr.branch_info->is_conditional) {
            uint64_t target = instr.branch_info->target;
            if (target <= instr.pc) {
                instr.opcode_type = OpcodeType::Nop;
                instr.branch_info = std::nullopt;
                break;
            }
        }
    }

    return Ok(ElfTraceParser(std::move(instructions), path));
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
        return Ok(std::optional<Instruction>{std::nullopt});
    }
    return Ok(std::optional<Instruction>{instructions_[cursor_++]});
}

} // namespace arm_cpu
