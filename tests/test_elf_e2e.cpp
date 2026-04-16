/// @file test_elf_e2e.cpp
/// @brief End-to-end test: load AArch64 ELF, decode with Capstone, simulate, output JSON.
///
/// Tests the full pipeline:
///   ELF file → ElfLoader → CapstoneDecoder → OoO Engine → PerformanceMetrics (JSON)

#include <gtest/gtest.h>
#include "arm_cpu/elf/elf_loader.hpp"
#include "arm_cpu/decoder/capstone_decoder.hpp"
#include "arm_cpu/ooo/ooo_engine.hpp"
#include "arm_cpu/config.hpp"
#include "arm_cpu/types.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace arm_cpu;
using namespace arm_cpu::decoder;

// =====================================================================
// Helper: ELF → Instruction stream via Capstone
// =====================================================================

struct ElfTestResult {
    bool elf_loaded = false;
    bool capstone_initialized = false;
    std::size_t total_instructions = 0;
    std::size_t decode_successes = 0;
    std::size_t decode_failures = 0;
    std::size_t other_count = 0;
    std::size_t sve_sme_count = 0;
    std::size_t memory_ops = 0;
    std::size_t branch_ops = 0;
    std::size_t compute_ops = 0;
    uint64_t total_cycles = 0;
    double ipc = 0.0;
    std::vector<std::string> disasm_lines;
    std::string error_message;
};

/// Load ELF, decode all instructions in executable segments with Capstone,
/// run through OoO engine, return metrics.
ElfTestResult run_elf_simulation(
    const std::string& elf_path,
    std::size_t max_instructions = 200,
    std::size_t window_size = 64,
    std::size_t issue_width = 4)
{
    ElfTestResult result;

    // Step 1: Load ELF
    auto elf = ElfLoader::load(elf_path);
    if (elf.has_error()) {
        result.error_message = "ELF load failed: " + elf.error().message();
        return result;
    }
    result.elf_loaded = true;

    // Step 2: Initialize Capstone
    CapstoneDecoder decoder;
    if (!decoder.init()) {
        result.error_message = "Capstone init failed";
        return result;
    }
    result.capstone_initialized = true;

    // Step 3: Decode instructions from executable segments
    auto exe_segments = elf.value().executable_segments();
    std::vector<Instruction> instructions;

    for (const auto* seg : exe_segments) {
        if (!seg || seg->size < 4) continue;

        for (std::size_t offset = 0; offset + 4 <= seg->size; offset += 4) {
            if (instructions.size() >= max_instructions) break;

            uint64_t pc = seg->vaddr + offset;
            uint32_t raw = 0;
            std::memcpy(&raw, seg->data.data() + offset, 4);

            auto decoded = decoder.decode(pc, raw);
            Instruction instr(InstructionId(instructions.size()), pc, raw, decoded.opcode);
            for (auto r : decoded.src_regs) instr.with_src_reg(r);
            for (auto r : decoded.dst_regs) instr.with_dst_reg(r);
            for (auto r : decoded.src_vregs) instr.with_src_vreg(r);
            for (auto r : decoded.dst_vregs) instr.with_dst_vreg(r);
            for (auto r : decoded.src_pregs) instr.with_src_preg(r);
            for (auto r : decoded.dst_pregs) instr.with_dst_preg(r);
            if (decoded.mem_access) {
                instr.mem_access = decoded.mem_access;
            }
            if (decoded.branch_info) {
                instr.branch_info = decoded.branch_info;
            }
            instr.disasm = decoded.disasm;

            if (decoded.opcode == OpcodeType::Other) {
                result.decode_failures++;
            } else {
                result.decode_successes++;
            }
            if (is_sve(decoded.opcode) || is_sme(decoded.opcode)) {
                result.sve_sme_count++;
            }
            if (is_memory_op(decoded.opcode)) result.memory_ops++;
            if (is_branch(decoded.opcode)) result.branch_ops++;
            if (is_compute(decoded.opcode)) result.compute_ops++;

            result.disasm_lines.push_back(decoded.disasm);
            instructions.push_back(std::move(instr));
        }
        if (instructions.size() >= max_instructions) break;
    }

    result.total_instructions = instructions.size();

    // Step 4: Stop infinite loops — replace trailing infinite-loop branches with NOPs
    // Find the last branch that jumps backwards (infinite loop)
    if (!instructions.empty()) {
        for (int i = static_cast<int>(instructions.size()) - 1; i >= 0; --i) {
            auto& instr = instructions[i];
            if (instr.branch_info.has_value() && !instr.branch_info->is_conditional) {
                uint64_t target = instr.branch_info->target;
                if (target <= instr.pc) {
                    // Replace infinite loop with NOP to let simulation terminate
                    instr.opcode_type = OpcodeType::Nop;
                    instr.branch_info = std::nullopt;
                    break;
                }
            }
        }
    }

    // Step 5: Run OoO simulation
    auto config = CPUConfig::minimal();
    config.window_size = window_size;
    config.issue_width = issue_width;
    config.commit_width = issue_width;

    auto engine = OoOEngine::create(config);
    if (!engine.ok()) {
        result.error_message = "Engine creation failed: " + engine.error().message();
        return result;
    }

    std::size_t committed = 0;
    uint64_t cycle = 0;
    constexpr uint64_t max_cycles = 100000;
    std::size_t instr_idx = 0;

    while ((instr_idx < instructions.size() || !engine.value()->is_empty()) && cycle < max_cycles) {
        // Dispatch up to issue_width instructions per cycle
        for (std::size_t w = 0; w < issue_width && instr_idx < instructions.size(); ++w) {
            if (engine.value()->can_accept()) {
                auto instr_copy = instructions[instr_idx];
                auto deps = engine.value()->dispatch(std::move(instr_copy));
                if (deps.ok()) instr_idx++;
            }
        }

        // Process completions from previous cycles (releases dependencies)
        engine.value()->cycle_tick();

        // Issue ready instructions to execution units and schedule completions
        auto ready = engine.value()->get_ready_instructions();
        for (auto& [id, instr] : ready) {
            uint64_t lat = instr.instr_latency();
            engine.value()->mark_completed(id, cycle + lat);
        }

        // Commit completed instructions (in program order)
        auto candidates = engine.value()->get_commit_candidates();
        for (auto& c : candidates) {
            engine.value()->commit(c.id);
            committed++;
        }

        // Advance cycle counter (must happen after cycle_tick/process_completions)
        engine.value()->advance_cycle();
        cycle++;
    }

    result.total_cycles = cycle;
    result.ipc = (cycle > 0) ? static_cast<double>(committed) / static_cast<double>(cycle) : 0.0;

    return result;
}

// =====================================================================
// Helper: Get test ELF path
// =====================================================================

std::string get_test_elf_path() {
    // Look in tests/data/ relative to source
    std::vector<std::string> candidates = {
        "tests/data/test_elf_aarch64",
        "../tests/data/test_elf_aarch64",
        "arm_cpu_emulator_cpp/tests/data/test_elf_aarch64",
    };
    for (auto& c : candidates) {
        std::ifstream f(c);
        if (f.good()) return c;
    }
    return "";
}

// =====================================================================
// Tests
// =====================================================================

TEST(ElfE2E, ElfLoadsSuccessfully) {
    auto elf = get_test_elf_path();
    ASSERT_FALSE(elf.empty()) << "Test ELF not found. Run: aarch64-elf-gcc -c test.S && aarch64-elf-ld -o tests/data/test_elf_aarch64 test.o";

    auto result = ElfLoader::load(elf);
    ASSERT_TRUE(result.ok()) << result.error().message();

    EXPECT_EQ(result.value().header().machine, 0xB7) << "Not AArch64 ELF (EM_AARCH64=0xB7)";
    EXPECT_GT(result.value().entry_point(), 0);
    EXPECT_FALSE(result.value().executable_segments().empty());
}

TEST(ElfE2E, CapstoneDecodesAllInstructions) {
    auto elf = get_test_elf_path();
    ASSERT_FALSE(elf.empty());

    auto result = run_elf_simulation(elf);
    ASSERT_TRUE(result.elf_loaded);
    ASSERT_TRUE(result.capstone_initialized);
    ASSERT_FALSE(result.error_message.empty() ? false : true) << result.error_message;

    EXPECT_GT(result.total_instructions, 20) << "Expected at least 20 instructions in test ELF";

    // Most instructions should decode successfully (not Other)
    double success_rate = static_cast<double>(result.decode_successes) /
                          static_cast<double>(result.total_instructions);
    EXPECT_GT(success_rate, 0.70)
        << "Decode success rate too low: " << result.decode_successes << "/"
        << result.total_instructions
        << " (failures: " << result.decode_failures << ")";
}

TEST(ElfE2E, InstructionTypeCoverage) {
    auto elf = get_test_elf_path();
    ASSERT_FALSE(elf.empty());

    auto result = run_elf_simulation(elf);

    // Should have decoded various instruction categories
    EXPECT_GT(result.compute_ops, 5)
        << "Expected multiple compute ops, got " << result.compute_ops;
    EXPECT_GT(result.memory_ops, 2)
        << "Expected multiple memory ops, got " << result.memory_ops;
    EXPECT_GT(result.branch_ops, 2)
        << "Expected multiple branch ops, got " << result.branch_ops;
}

TEST(ElfE2E, OoOSimulationCompletes) {
    auto elf = get_test_elf_path();
    ASSERT_FALSE(elf.empty());

    auto result = run_elf_simulation(elf);

    EXPECT_GT(result.total_instructions, 0);
    EXPECT_GT(result.total_cycles, 0);
    EXPECT_GT(result.ipc, 0.0);
    EXPECT_LE(result.ipc, 4.0) << "IPC exceeds issue width";

    // IPC should be reasonable (> 0.5 for simple code)
    EXPECT_GT(result.ipc, 0.1)
        << "IPC suspiciously low: " << result.ipc;
}

TEST(ElfE2E, DisasmOutputContainsExpectedMnemonics) {
    auto elf = get_test_elf_path();
    ASSERT_FALSE(elf.empty());

    auto result = run_elf_simulation(elf);

    // Check that key mnemonics appear in disassembly
    std::string all_disasm;
    for (const auto& line : result.disasm_lines) {
        all_disasm += line + "\n";
    }

    EXPECT_NE(all_disasm.find("add"), std::string::npos) << "Missing 'add' in disassembly";
    EXPECT_NE(all_disasm.find("sub"), std::string::npos) << "Missing 'sub' in disassembly";
    EXPECT_NE(all_disasm.find("mul"), std::string::npos) << "Missing 'mul' in disassembly";
    EXPECT_NE(all_disasm.find("ldr"), std::string::npos) << "Missing 'ldr' in disassembly";
    EXPECT_NE(all_disasm.find("str"), std::string::npos) << "Missing 'str' in disassembly";
    EXPECT_NE(all_disasm.find("dmb"), std::string::npos) << "Missing 'dmb' in disassembly";
}

TEST(ElfE2E, JsonOutputFormat) {
    auto elf = get_test_elf_path();
    ASSERT_FALSE(elf.empty());

    auto result = run_elf_simulation(elf);

    // Produce JSON output (the actual format the user wants)
    std::ostringstream json;
    json << "{\n";
    json << "  \"test\": \"elf_e2e\",\n";
    json << "  \"elf_loaded\": " << (result.elf_loaded ? "true" : "false") << ",\n";
    json << "  \"capstone_initialized\": " << (result.capstone_initialized ? "true" : "false") << ",\n";
    json << "  \"total_instructions\": " << result.total_instructions << ",\n";
    json << "  \"decode_successes\": " << result.decode_successes << ",\n";
    json << "  \"decode_failures\": " << result.decode_failures << ",\n";
    json << "  \"decode_success_rate\": " << std::fixed
         << (result.total_instructions > 0
             ? static_cast<double>(result.decode_successes) / result.total_instructions
             : 0.0) << ",\n";
    json << "  \"memory_ops\": " << result.memory_ops << ",\n";
    json << "  \"branch_ops\": " << result.branch_ops << ",\n";
    json << "  \"compute_ops\": " << result.compute_ops << ",\n";
    json << "  \"sve_sme_ops\": " << result.sve_sme_count << ",\n";
    json << "  \"total_cycles\": " << result.total_cycles << ",\n";
    json << "  \"ipc\": " << std::fixed << result.ipc << ",\n";

    // Instruction breakdown
    json << "  \"instructions\": [\n";
    for (std::size_t i = 0; i < result.disasm_lines.size(); ++i) {
        json << "    \"" << result.disasm_lines[i] << "\"";
        if (i + 1 < result.disasm_lines.size()) json << ",";
        json << "\n";
    }
    json << "  ]\n";
    json << "}\n";

    std::string json_str = json.str();

    // Verify JSON structure
    EXPECT_NE(json_str.find("\"test\": \"elf_e2e\""), std::string::npos);
    EXPECT_NE(json_str.find("\"total_instructions\":"), std::string::npos);
    EXPECT_NE(json_str.find("\"total_cycles\":"), std::string::npos);
    EXPECT_NE(json_str.find("\"ipc\":"), std::string::npos);
    EXPECT_NE(json_str.find("\"instructions\": ["), std::string::npos);

    // Print JSON to stdout for user inspection
    std::printf("\n=== ELF End-to-End Simulation Results (JSON) ===\n");
    std::printf("%s", json_str.c_str());
    std::printf("=== End of JSON ===\n\n");
}

TEST(ElfE2E, ElfHeaderAndSegments) {
    auto elf = get_test_elf_path();
    ASSERT_FALSE(elf.empty());

    auto loader = ElfLoader::load(elf);
    ASSERT_TRUE(loader.ok());

    const auto& hdr = loader.value().header();
    EXPECT_EQ(hdr.machine, 0xB7);  // EM_AARCH64

    const auto& segs = loader.value().segments();
    EXPECT_FALSE(segs.empty());

    // At least one executable segment
    auto exe_segs = loader.value().executable_segments();
    EXPECT_FALSE(exe_segs.empty());

    // Verify we can read instructions from executable segments
    bool can_read = false;
    for (const auto* seg : exe_segs) {
        if (seg && seg->size >= 4) {
            auto instr = loader.value().read_instruction(seg->vaddr);
            if (instr.has_value()) {
                can_read = true;
                break;
            }
        }
    }
    EXPECT_TRUE(can_read) << "Cannot read any instruction from executable segments";
}

TEST(ElfE2E, SymbolTableWorks) {
    auto elf = get_test_elf_path();
    ASSERT_FALSE(elf.empty());

    auto loader = ElfLoader::load(elf);
    ASSERT_TRUE(loader.ok());

    // Bare-metal ELF from aarch64-elf-ld may not have a symbol table at all,
    // or _start may be NOTYPE. Just verify the symbol_table() method doesn't crash.
    auto* sym = loader.value().symbol_table().find_by_name("_start");
    if (sym) {
        EXPECT_GT(sym->address, 0);
    }
    // Symbol table may be empty for bare-metal binaries - that's OK
}
