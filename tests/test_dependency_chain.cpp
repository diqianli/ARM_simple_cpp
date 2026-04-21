/// @file test_dependency_chain.cpp
/// @brief Dependency chain violation detection tests.
///
/// Verifies OoO execution correctness by checking that for every
/// (consumer, producer) dependency edge, the consumer finishes execution
/// no earlier than the producer, and retires no earlier than the producer.

#include <gtest/gtest.h>

#include "arm_cpu/cpu.hpp"
#include "arm_cpu/input/instruction_source.hpp"
#include "arm_cpu/config.hpp"
#include "arm_cpu/types.hpp"
#include "arm_cpu/visualization/konata_format.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>
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
// Helper: extract the end_cycle of a named stage from an op's lanes
// =====================================================================

std::optional<uint64_t> get_stage_end_cycle(
    const KonataOp& op, const std::string& stage_name)
{
    for (const auto& [lane_name, lane] : op.lanes) {
        for (const auto& stage : lane.stages) {
            if (stage.name == stage_name) {
                return stage.end_cycle;
            }
        }
    }
    return std::nullopt;
}

// =====================================================================
// Helper: get the effective execution/memory end cycle for an op
// =====================================================================

/// For compute ops this is the EX stage end; for memory ops this is the
/// last ME (or ME:L1/L2/L3) stage end.  Returns nullopt if none found.
std::optional<uint64_t> get_execution_end_cycle(const KonataOp& op) {
    // Prefer explicit EX stage (compute ops)
    auto ex_end = get_stage_end_cycle(op, "EX");
    if (ex_end.has_value()) return ex_end;

    // Memory ops use ME or ME:level sub-stages — take the latest one
    std::optional<uint64_t> latest_me;
    for (const auto& [lane_name, lane] : op.lanes) {
        for (const auto& stage : lane.stages) {
            if (stage.name == "ME" || stage.name.starts_with("ME:")) {
                if (!latest_me.has_value() || stage.end_cycle > *latest_me) {
                    latest_me = stage.end_cycle;
                }
            }
        }
    }
    return latest_me;
}

// =====================================================================
// Core: check dependency chain correctness
// =====================================================================

/// Check all dependency edges in exported KonataOps.
/// Returns a vector of violation descriptions (empty = no violations).
std::vector<std::string> check_dependency_chain(
    const std::vector<KonataOp>& ops)
{
    // Build id -> op lookup
    std::unordered_map<uint64_t, const KonataOp*> id_map;
    id_map.reserve(ops.size() * 2);
    for (const auto& op : ops) {
        id_map[op.id] = &op;
    }

    std::vector<std::string> violations;

    for (const auto& consumer : ops) {
        for (const auto& dep : consumer.prods) {
            auto prod_it = id_map.find(dep.producer_id);
            if (prod_it == id_map.end()) {
                // Producer not in ops — skip (may have been evicted from tracker)
                continue;
            }
            const auto& producer = *prod_it->second;

            const char* dep_type_str =
                dep.dep_type == KonataDependencyType::Register ? "Register" : "Memory";

            // --- Execution timing check (covers both EX and ME stages) ---
            auto consumer_end = get_execution_end_cycle(consumer);
            auto producer_end = get_execution_end_cycle(producer);

            if (consumer_end.has_value() && producer_end.has_value()) {
                if (*consumer_end < *producer_end) {
                    violations.push_back(std::format(
                        "Consumer [id={}] 0x{:x} {} executed before Producer [id={}] 0x{:x} {} "
                        "(dep={}, consumer_end={}, producer_end={})",
                        consumer.id, consumer.pc, consumer.label_name,
                        producer.id, producer.pc, producer.label_name,
                        dep_type_str, *consumer_end, *producer_end));
                }
            }

            // --- Retire timing check ---
            if (consumer.retired_cycle.has_value() && producer.retired_cycle.has_value()) {
                if (*consumer.retired_cycle < *producer.retired_cycle) {
                    violations.push_back(std::format(
                        "Consumer [id={}] 0x{:x} {} retired before Producer [id={}] 0x{:x} {} "
                        "(dep={}, consumer_retired={}, producer_retired={})",
                        consumer.id, consumer.pc, consumer.label_name,
                        producer.id, producer.pc, producer.label_name,
                        dep_type_str, *consumer.retired_cycle, *producer.retired_cycle));
                }
            }
        }
    }

    return violations;
}

// =====================================================================
// Helper: run a text trace simulation with visualization enabled
// =====================================================================

struct VizSimResult {
    bool ok = false;
    std::string error;
    std::vector<KonataOp> ops;
    PerformanceMetrics metrics;
};

VizSimResult run_text_trace_viz(
    const std::string& trace_path,
    CPUConfig config = CPUConfig::minimal(),
    uint64_t max_cycles = 100000)
{
    VizSimResult result;

    auto cpu = CPUEmulator::create_with_visualization(
        config, VisualizationConfig::enabled_config());
    if (!cpu.ok()) {
        result.error = "create_with_visualization failed: " + cpu.error().message();
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

    result.ops = (*cpu)->visualization().pipeline_tracker().export_all_konata_ops();
    result.metrics = metrics.value();
    result.ok = true;
    return result;
}

// =====================================================================
// Helper: report violations on test failure
// =====================================================================

void report_violations(const std::vector<std::string>& violations) {
    for (const auto& v : violations) {
        ADD_FAILURE() << v;
    }
}

// =====================================================================
// A. Text trace tests (no cross-compilation needed)
// =====================================================================

TEST(DependencyChain, BasicTraceNoViolations) {
    auto path = find_test_data("text_trace_basic.txt");
    ASSERT_FALSE(path.empty()) << "text_trace_basic.txt not found";

    auto result = run_text_trace_viz(path);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_GT(result.ops.size(), 0u) << "No KonataOps exported";

    auto violations = check_dependency_chain(result.ops);
    report_violations(violations);
    EXPECT_TRUE(violations.empty())
        << violations.size() << " dependency chain violation(s) detected in basic trace";
}

TEST(DependencyChain, MemoryTraceNoViolations) {
    auto path = find_test_data("text_trace_memory.txt");
    ASSERT_FALSE(path.empty()) << "text_trace_memory.txt not found";

    auto result = run_text_trace_viz(path);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_GT(result.ops.size(), 0u) << "No KonataOps exported";

    auto violations = check_dependency_chain(result.ops);
    report_violations(violations);
    EXPECT_TRUE(violations.empty())
        << violations.size() << " dependency chain violation(s) detected in memory trace";
}

TEST(DependencyChain, BranchesTraceNoViolations) {
    auto path = find_test_data("text_trace_branches.txt");
    ASSERT_FALSE(path.empty()) << "text_trace_branches.txt not found";

    auto result = run_text_trace_viz(path);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_GT(result.ops.size(), 0u) << "No KonataOps exported";

    auto violations = check_dependency_chain(result.ops);
    report_violations(violations);
    EXPECT_TRUE(violations.empty())
        << violations.size() << " dependency chain violation(s) detected in branches trace";
}

TEST(DependencyChain, LongTraceNoViolations) {
    auto path = find_test_data("text_trace_long.txt");
    ASSERT_FALSE(path.empty()) << "text_trace_long.txt not found";

    auto result = run_text_trace_viz(path, CPUConfig::minimal(), 500000);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_GT(result.ops.size(), 0u) << "No KonataOps exported";

    auto violations = check_dependency_chain(result.ops);
    report_violations(violations);
    EXPECT_TRUE(violations.empty())
        << violations.size() << " dependency chain violation(s) detected in long trace";
}

// =====================================================================
// B. Constructive tests (hand-crafted dependency chains)
// =====================================================================

TEST(DependencyChain, ConstructedChainNoViolations) {
    // Build a 5-instruction ADD chain: ADD X0,X1,X2 -> ADD X3,X0,X4 ->
    // ADD X5,X3,X6 -> ADD X7,X5,X8 -> ADD X9,X7,X10
    auto tmp = std::filesystem::temp_directory_path() / "dep_chain_trace.txt";
    {
        std::ofstream f(tmp);
        f << "0x400000: ADD X0, X1, X2\n";
        f << "0x400004: ADD X3, X0, X4\n";
        f << "0x400008: ADD X5, X3, X6\n";
        f << "0x40000c: ADD X7, X5, X8\n";
        f << "0x400010: ADD X9, X7, X10\n";
    }

    auto result = run_text_trace_viz(tmp.string());
    std::filesystem::remove(tmp);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_GT(result.ops.size(), 0u) << "No KonataOps exported";

    auto violations = check_dependency_chain(result.ops);
    report_violations(violations);
    EXPECT_TRUE(violations.empty())
        << violations.size() << " dependency chain violation(s) in constructed ADD chain";
}

TEST(DependencyChain, LoadStoreDependency) {
    // LDR X0, [X1, #0] -> ADD X2, X0, X3  (load-to-use dependency)
    auto tmp = std::filesystem::temp_directory_path() / "load_dep_trace.txt";
    {
        std::ofstream f(tmp);
        f << "0x400000: LDR X0, [X1, #0]\n";
        f << "0x400004: ADD X2, X0, X3\n";
    }

    auto result = run_text_trace_viz(tmp.string());
    std::filesystem::remove(tmp);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_GT(result.ops.size(), 0u) << "No KonataOps exported";

    auto violations = check_dependency_chain(result.ops);
    report_violations(violations);
    EXPECT_TRUE(violations.empty())
        << violations.size() << " dependency chain violation(s) in load-to-use test";
}

// =====================================================================
// C. Dependency existence verification (ensure deps are actually recorded)
// =====================================================================

TEST(DependencyChain, DependenciesAreRecorded) {
    // Verify that register RAW dependencies are actually recorded in KonataOps.
    // ADD X0,X1,X2 -> ADD X3,X0,X4  must have a dependency edge.
    auto tmp = std::filesystem::temp_directory_path() / "dep_exists_trace.txt";
    {
        std::ofstream f(tmp);
        f << "0x400000: ADD X0, X1, X2\n";
        f << "0x400004: ADD X3, X0, X4\n";
    }

    auto result = run_text_trace_viz(tmp.string());
    std::filesystem::remove(tmp);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_GE(result.ops.size(), 2u) << "Expected at least 2 KonataOps";

    // The second instruction (consumer) must have at least one producer dependency
    auto& consumer_op = result.ops[1];  // sorted by id
    EXPECT_GE(consumer_op.prods.size(), 1u)
        << "Consumer ADD X3,X0,X4 should have a dependency on producer ADD X0,X1,X2, "
        << "but has " << consumer_op.prods.size() << " dependencies";
}

TEST(DependencyChain, LoadToUseDependencyRecorded) {
    // LDR X0,[X1,#0] -> ADD X2,X0,X3  must have a register dependency
    auto tmp = std::filesystem::temp_directory_path() / "load_dep_exists.txt";
    {
        std::ofstream f(tmp);
        f << "0x400000: LDR X0, [X1, #0]\n";
        f << "0x400004: ADD X2, X0, X3\n";
    }

    auto result = run_text_trace_viz(tmp.string());
    std::filesystem::remove(tmp);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_GE(result.ops.size(), 2u) << "Expected at least 2 KonataOps";

    auto& consumer_op = result.ops[1];
    bool has_dep = false;
    for (const auto& d : consumer_op.prods) {
        if (d.producer_id == result.ops[0].id) {
            has_dep = true;
            break;
        }
    }
    EXPECT_TRUE(has_dep)
        << "ADD X2,X0,X3 should depend on LDR X0,[X1,#0] via X0 register";
}

TEST(DependencyChain, DependencyTimingSanity) {
    // For a chain of ADD instructions, verify that execution is sequential
    // (each later instruction finishes no earlier than the previous).
    auto tmp = std::filesystem::temp_directory_path() / "timing_sanity.txt";
    {
        std::ofstream f(tmp);
        f << "0x400000: ADD X0, X1, X2\n";
        f << "0x400004: ADD X3, X0, X4\n";
        f << "0x400008: ADD X5, X3, X6\n";
    }

    auto result = run_text_trace_viz(tmp.string());
    std::filesystem::remove(tmp);
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_GE(result.ops.size(), 3u) << "Expected at least 3 KonataOps";

    // Each instruction's execution end should be >= previous instruction's execution end
    for (std::size_t i = 1; i < result.ops.size(); ++i) {
        auto prev_end = get_execution_end_cycle(result.ops[i - 1]);
        auto curr_end = get_execution_end_cycle(result.ops[i]);
        if (prev_end.has_value() && curr_end.has_value()) {
            EXPECT_GE(*curr_end, *prev_end)
                << "Instruction " << result.ops[i].label_name
                << " (id=" << result.ops[i].id << ") ended at cycle " << *curr_end
                << " but depends on " << result.ops[i-1].label_name
                << " (id=" << result.ops[i-1].id << ") which ended at cycle " << *prev_end;
        }
    }

    // Retire order must be sequential
    for (std::size_t i = 1; i < result.ops.size(); ++i) {
        auto prev_retire = result.ops[i - 1].retired_cycle;
        auto curr_retire = result.ops[i].retired_cycle;
        if (prev_retire.has_value() && curr_retire.has_value()) {
            EXPECT_GE(*curr_retire, *prev_retire)
                << "Instruction " << result.ops[i].label_name
                << " retired at cycle " << *curr_retire
                << " before " << result.ops[i-1].label_name
                << " retired at cycle " << *prev_retire;
        }
    }
}

// =====================================================================
// D. Multi-config tests
// =====================================================================

TEST(DependencyChain, MultipleConfigsNoViolations) {
    auto path = find_test_data("text_trace_basic.txt");
    ASSERT_FALSE(path.empty()) << "text_trace_basic.txt not found";

    std::vector<std::pair<std::string, CPUConfig>> configs = {
        {"minimal",           CPUConfig::minimal()},
        {"default",           CPUConfig::default_config()},
        {"high_performance",  CPUConfig::high_performance()},
    };

    for (const auto& [name, config] : configs) {
        SCOPED_TRACE("Config: " + name);

        auto result = run_text_trace_viz(path, config);
        ASSERT_TRUE(result.ok) << result.error;
        ASSERT_GT(result.ops.size(), 0u) << "No KonataOps exported with " << name << " config";

        auto violations = check_dependency_chain(result.ops);
        report_violations(violations);
        EXPECT_TRUE(violations.empty())
            << violations.size() << " dependency chain violation(s) with " << name << " config";
    }
}
