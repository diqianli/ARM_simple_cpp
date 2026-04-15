/// @file test_simulation.cpp
/// @brief Simulation integration tests using CPUEmulator API with text traces.
///
/// These tests use text trace files (no cross-compilation needed) to verify
/// the full pipeline: trace parsing → OoO execution → performance metrics.

#include <gtest/gtest.h>
#include "arm_cpu/cpu.hpp"
#include "arm_cpu/input/instruction_source.hpp"
#include "arm_cpu/config.hpp"
#include "arm_cpu/types.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace arm_cpu;

// =====================================================================
// Helper: find test data file
// =====================================================================

std::string find_test_data(const std::string& filename) {
    std::vector<std::string> candidates = {
        "tests/data/" + filename,
        "../tests/data/" + filename,
        "arm_cpu_emulator_cpp/tests/data/" + filename,
    };
    for (auto& c : candidates) {
        if (std::ifstream f(c); f.good()) return c;
    }
    return "";
}

// =====================================================================
// Helper: run a text trace simulation and return metrics
// =====================================================================

struct SimResult {
    bool ok = false;
    std::string error;
    PerformanceMetrics metrics;
};

SimResult run_text_trace(
    const std::string& trace_path,
    CPUConfig config = CPUConfig::minimal(),
    uint64_t max_cycles = 100000)
{
    SimResult result;

    auto cpu = CPUEmulator::create(config);
    if (!cpu.ok()) {
        result.error = "CPUEmulator::create failed: " + cpu.error().message();
        return result;
    }

    TraceInputConfig trace_cfg;
    trace_cfg.file_path = trace_path;
    trace_cfg.format = TraceFormat::Text;

    auto source = create_source(trace_cfg);
    if (!source) {
        result.error = "create_source failed for: " + trace_path;
        return result;
    }

    auto next_instr = [&source]() -> std::optional<Result<Instruction>> {
        auto r = source->next();
        if (r.has_error()) return std::nullopt;
        auto& opt = r.value();
        if (!opt.has_value()) return std::nullopt;
        return std::move(*opt);
    };

    auto metrics = (*cpu)->run_with_limit(next_instr, max_cycles);
    if (!metrics.ok()) {
        result.error = "run_with_limit failed: " + metrics.error().message();
        return result;
    }

    result.ok = true;
    result.metrics = metrics.value();
    return result;
}

// =====================================================================
// Tests: Basic text trace
// =====================================================================

TEST(Simulation, TextTraceBasic) {
    auto path = find_test_data("text_trace_basic.txt");
    ASSERT_FALSE(path.empty()) << "text_trace_basic.txt not found";

    auto result = run_text_trace(path);
    ASSERT_TRUE(result.ok) << result.error;

    EXPECT_GT(result.metrics.total_instructions, 0);
    EXPECT_GT(result.metrics.total_cycles, 0);
    EXPECT_GT(result.metrics.ipc, 0.0);
    EXPECT_LE(result.metrics.ipc, 10.0) << "IPC unreasonably high";
}

TEST(Simulation, TextTraceBasicMetricsSanity) {
    auto path = find_test_data("text_trace_basic.txt");
    ASSERT_FALSE(path.empty());

    auto result = run_text_trace(path);
    ASSERT_TRUE(result.ok) << result.error;

    // Basic trace should have some compute and memory instructions
    EXPECT_GT(result.metrics.total_instructions, 15);
    // IPC should be reasonable for small traces
    EXPECT_GT(result.metrics.ipc, 0.01);
    EXPECT_LT(result.metrics.ipc, 4.0);
}

// =====================================================================
// Tests: Memory-intensive trace
// =====================================================================

TEST(Simulation, TextTraceMemory) {
    auto path = find_test_data("text_trace_memory.txt");
    ASSERT_FALSE(path.empty()) << "text_trace_memory.txt not found";

    auto result = run_text_trace(path);
    ASSERT_TRUE(result.ok) << result.error;

    EXPECT_GT(result.metrics.total_instructions, 40);
    EXPECT_GT(result.metrics.total_cycles, 0);
    EXPECT_GT(result.metrics.memory_instr_pct, 0.0)
        << "Memory trace should have memory instructions";
}

// =====================================================================
// Tests: Branch-intensive trace
// =====================================================================

TEST(Simulation, TextTraceBranches) {
    auto path = find_test_data("text_trace_branches.txt");
    ASSERT_FALSE(path.empty()) << "text_trace_branches.txt not found";

    auto result = run_text_trace(path);
    ASSERT_TRUE(result.ok) << result.error;

    EXPECT_GT(result.metrics.total_instructions, 20);
    EXPECT_GT(result.metrics.branch_instr_pct, 0.0)
        << "Branch trace should have branch instructions";
}

// =====================================================================
// Tests: Long trace (stress test)
// =====================================================================

TEST(Simulation, TextTraceLong) {
    auto path = find_test_data("text_trace_long.txt");
    ASSERT_FALSE(path.empty()) << "text_trace_long.txt not found";

    auto result = run_text_trace(path, CPUConfig::minimal(), 500000);
    ASSERT_TRUE(result.ok) << result.error;

    EXPECT_GT(result.metrics.total_instructions, 150);
    EXPECT_GT(result.metrics.total_cycles, 0);
    EXPECT_GT(result.metrics.ipc, 0.0);
}

// =====================================================================
// Tests: Config variants
// =====================================================================

TEST(Simulation, ConfigVariants) {
    auto path = find_test_data("text_trace_basic.txt");
    ASSERT_FALSE(path.empty());

    // Test with default config
    {
        auto result = run_text_trace(path, CPUConfig::default_config());
        ASSERT_TRUE(result.ok) << result.error;
        EXPECT_GT(result.metrics.total_instructions, 0);
    }

    // Test with minimal config
    {
        auto result = run_text_trace(path, CPUConfig::minimal());
        ASSERT_TRUE(result.ok) << result.error;
        EXPECT_GT(result.metrics.total_instructions, 0);
    }

    // Test with high-performance config
    {
        auto result = run_text_trace(path, CPUConfig::high_performance());
        ASSERT_TRUE(result.ok) << result.error;
        EXPECT_GT(result.metrics.total_instructions, 0);
    }

    // Test with tiny window
    {
        auto config = CPUConfig::minimal();
        config.window_size = 4;
        config.issue_width = 1;
        config.commit_width = 1;
        auto result = run_text_trace(path, config);
        ASSERT_TRUE(result.ok) << result.error;
        EXPECT_GT(result.metrics.total_instructions, 0);
    }
}

// =====================================================================
// Tests: Empty and edge cases
// =====================================================================

TEST(Simulation, EmptyTrace) {
    // Create a temporary empty trace file
    auto tmp = std::filesystem::temp_directory_path() / "empty_trace.txt";
    {
        std::ofstream f(tmp);
        f << "# only comments\n";
        f << "\n";
        f << "\n";
    }

    auto result = run_text_trace(tmp.string());
    // Empty trace should not crash — may return 0 instructions
    EXPECT_TRUE(result.ok) << result.error;

    std::filesystem::remove(tmp);
}

TEST(Simulation, SingleInstruction) {
    // Create a temporary trace with just one instruction
    auto tmp = std::filesystem::temp_directory_path() / "single_instr.txt";
    {
        std::ofstream f(tmp);
        f << "0x400000: ADD X0, X1, X2\n";
    }

    auto result = run_text_trace(tmp.string());
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_GE(result.metrics.total_instructions, 1);
    EXPECT_GT(result.metrics.total_cycles, 0);

    std::filesystem::remove(tmp);
}

TEST(Simulation, CommentOnlyTrace) {
    auto tmp = std::filesystem::temp_directory_path() / "comment_only.txt";
    {
        std::ofstream f(tmp);
        f << "# This is a comment\n";
        f << "# Another comment\n";
    }

    auto result = run_text_trace(tmp.string());
    EXPECT_TRUE(result.ok) << result.error;

    std::filesystem::remove(tmp);
}
