/// @file main.cpp
/// @brief CLI entry point for the ARM CPU emulator.
///
/// Provides command-line interface for running trace-driven CPU simulations
/// with configurable parameters.
///
/// Ported from Rust src/lib.rs usage patterns and CLI conventions.

#include "arm_cpu/cpu.hpp"
#include "arm_cpu/input/instruction_source.hpp"
#include "arm_cpu/multi_instance/manager.hpp"
#include "arm_cpu/ooo/ooo_engine.hpp"
#include "arm_cpu/simulation/simulation_engine.hpp"
#include "arm_cpu/memory/memory_subsystem.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

// =====================================================================
// CLI argument parsing
// =====================================================================

struct CliArgs {
    std::string trace_file;
    std::string trace_format = "text";       // text, binary, json, champsim, champsim_xz
    std::size_t max_instructions = 0;        // 0 = unlimited
    std::size_t skip_instructions = 0;
    uint64_t max_cycles = 1'000'000'000;
    bool use_simulation_engine = false;      // Use SimulationEngine instead of CPUEmulator
    bool enable_visualization = false;
    uint16_t viz_port = 3000;
    bool multi_instance = false;
    std::size_t num_instances = 1;
    std::size_t num_threads = 0;
    bool save_trace = false;
    std::string trace_output_file;
    bool verbose = false;
    bool json_output = false;

    // CPU config overrides
    std::size_t window_size = 0;             // 0 = use default
    std::size_t fetch_width = 0;
    std::size_t issue_width = 0;
    std::size_t commit_width = 0;
    std::size_t l1_size = 0;
    std::size_t l2_size = 0;
    std::size_t l3_size = 0;
    uint64_t frequency_mhz = 0;
};

void print_usage(const char* program_name) {
    std::fprintf(stderr,
        "ARM CPU Emulator - ESL Simulation Tool\n"
        "\n"
        "Usage: %s [OPTIONS] <trace_file>\n"
        "\n"
        "Options:\n"
        "  -f, --format <fmt>       Trace format: text, binary, json, champsim, champsim_xz (default: text)\n"
        "  -n, --max-instr <n>      Maximum instructions to simulate (default: unlimited)\n"
        "  -s, --skip <n>           Skip first N instructions\n"
        "  -c, --max-cycles <n>     Maximum cycles to simulate (default: 1000000000)\n"
        "  -e, --engine             Use SimulationEngine instead of CPUEmulator\n"
        "  -v, --verbose            Enable verbose output\n"
        "  -j, --json               Output results in JSON format\n"
        "\n"
        "CPU Configuration:\n"
        "  --window-size <n>        Instruction window size (default: 128)\n"
        "  --fetch-width <n>        Fetch width (default: 8)\n"
        "  --issue-width <n>        Issue width (default: 4)\n"
        "  --commit-width <n>       Commit width (default: 4)\n"
        "  --l1-size <kb>           L1 cache size in KB (default: 64)\n"
        "  --l2-size <kb>           L2 cache size in KB (default: 512)\n"
        "  --l3-size <kb>           L3 cache size in KB (default: 8192)\n"
        "  --frequency <mhz>        CPU frequency in MHz (default: 2000)\n"
        "\n"
        "Multi-Instance:\n"
        "  -m, --multi <n>          Run N instances in parallel\n"
        "  -t, --threads <n>        Number of threads (default: hardware concurrency)\n"
        "\n"
        "Output:\n"
        "  --save-trace <file>      Save execution trace to file\n"
        "\n"
        "General:\n"
        "  -h, --help               Show this help message\n"
        "  --version                Show version information\n"
        "\n"
        "Examples:\n"
        "  %s trace.txt\n"
        "  %s -f champsim -n 100000 trace.champsim\n"
        "  %s -m 4 -t 8 -f binary trace.bin\n"
        "  %s -e -v --window-size 256 trace.txt\n"
        "\n",
        program_name, program_name, program_name, program_name, program_name
    );
}

bool parse_args(int argc, char* argv[], CliArgs& args) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        }
        else if (arg == "--version") {
            std::printf("ARM CPU Emulator v0.1.0\n");
            std::exit(0);
        }
        else if (arg == "-f" || arg == "--format") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.trace_format = argv[++i];
        }
        else if (arg == "-n" || arg == "--max-instr") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.max_instructions = std::stoull(argv[++i]);
        }
        else if (arg == "-s" || arg == "--skip") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.skip_instructions = std::stoull(argv[++i]);
        }
        else if (arg == "-c" || arg == "--max-cycles") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.max_cycles = std::stoull(argv[++i]);
        }
        else if (arg == "-e" || arg == "--engine") {
            args.use_simulation_engine = true;
        }
        else if (arg == "-v" || arg == "--verbose") {
            args.verbose = true;
        }
        else if (arg == "-j" || arg == "--json") {
            args.json_output = true;
        }
        else if (arg == "--window-size") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.window_size = std::stoull(argv[++i]);
        }
        else if (arg == "--fetch-width") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.fetch_width = std::stoull(argv[++i]);
        }
        else if (arg == "--issue-width") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.issue_width = std::stoull(argv[++i]);
        }
        else if (arg == "--commit-width") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.commit_width = std::stoull(argv[++i]);
        }
        else if (arg == "--l1-size") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.l1_size = std::stoull(argv[++i]) * 1024;
        }
        else if (arg == "--l2-size") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.l2_size = std::stoull(argv[++i]) * 1024;
        }
        else if (arg == "--l3-size") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.l3_size = std::stoull(argv[++i]) * 1024;
        }
        else if (arg == "--frequency") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.frequency_mhz = std::stoull(argv[++i]);
        }
        else if (arg == "-m" || arg == "--multi") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.multi_instance = true;
            args.num_instances = std::stoull(argv[++i]);
        }
        else if (arg == "-t" || arg == "--threads") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.num_threads = std::stoull(argv[++i]);
        }
        else if (arg == "--save-trace") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.save_trace = true;
            args.trace_output_file = argv[++i];
        }
        else if (arg[0] == '-') {
            std::fprintf(stderr, "Error: unknown option: %s\n", arg.data());
            return false;
        }
        else {
            if (args.trace_file.empty()) {
                args.trace_file = arg;
            } else {
                std::fprintf(stderr, "Error: multiple trace files specified\n");
                return false;
            }
        }
    }

    return true;
}

arm_cpu::TraceFormat parse_trace_format(std::string_view fmt) {
    if (fmt == "text") return arm_cpu::TraceFormat::Text;
    if (fmt == "binary") return arm_cpu::TraceFormat::Binary;
    if (fmt == "json") return arm_cpu::TraceFormat::Json;
    if (fmt == "champsim") return arm_cpu::TraceFormat::ChampSim;
    if (fmt == "champsim_xz" || fmt == "champsimxz") return arm_cpu::TraceFormat::ChampSimXz;
    std::fprintf(stderr, "Warning: unknown trace format '%.*s', defaulting to text\n",
                 static_cast<int>(fmt.size()), fmt.data());
    return arm_cpu::TraceFormat::Text;
}

arm_cpu::CPUConfig build_config(const CliArgs& args) {
    auto config = arm_cpu::CPUConfig::default_config();

    if (args.window_size > 0) config.window_size = args.window_size;
    if (args.fetch_width > 0) config.fetch_width = args.fetch_width;
    if (args.issue_width > 0) config.issue_width = args.issue_width;
    if (args.commit_width > 0) config.commit_width = args.commit_width;
    if (args.l1_size > 0) config.l1_size = args.l1_size;
    if (args.l2_size > 0) config.l2_size = args.l2_size;
    if (args.l3_size > 0) config.l3_size = args.l3_size;
    if (args.frequency_mhz > 0) config.frequency_mhz = args.frequency_mhz;

    if (args.save_trace) {
        config.enable_trace_output = true;
        config.max_trace_output = 100000;
    }

    return config;
}

void print_json_metrics(const arm_cpu::PerformanceMetrics& m) {
    std::printf(
        "{\n"
        "  \"total_instructions\": %llu,\n"
        "  \"total_cycles\": %llu,\n"
        "  \"ipc\": %.6f,\n"
        "  \"cpi\": %.6f,\n"
        "  \"l1_hit_rate\": %.6f,\n"
        "  \"l2_hit_rate\": %.6f,\n"
        "  \"l1_mpki\": %.6f,\n"
        "  \"l2_mpki\": %.6f,\n"
        "  \"memory_instr_pct\": %.6f,\n"
        "  \"branch_instr_pct\": %.6f,\n"
        "  \"avg_load_latency\": %.6f,\n"
        "  \"avg_store_latency\": %.6f\n"
        "}\n",
        static_cast<unsigned long long>(m.total_instructions),
        static_cast<unsigned long long>(m.total_cycles),
        m.ipc,
        m.cpi,
        m.l1_hit_rate,
        m.l2_hit_rate,
        m.l1_mpki,
        m.l2_mpki,
        m.memory_instr_pct,
        m.branch_instr_pct,
        m.avg_load_latency,
        m.avg_store_latency
    );
}

int run_single(const CliArgs& args) {
    using namespace arm_cpu;

    auto config = build_config(args);

    if (args.verbose) {
        std::fprintf(stderr, "Configuration:\n");
        std::fprintf(stderr, "  Window size: %zu\n", config.window_size);
        std::fprintf(stderr, "  Fetch width: %zu\n", config.fetch_width);
        std::fprintf(stderr, "  Issue width: %zu\n", config.issue_width);
        std::fprintf(stderr, "  Commit width: %zu\n", config.commit_width);
        std::fprintf(stderr, "  L1 size: %zu KB\n", config.l1_size / 1024);
        std::fprintf(stderr, "  L2 size: %zu KB\n", config.l2_size / 1024);
        std::fprintf(stderr, "  L3 size: %zu KB\n", config.l3_size / 1024);
        std::fprintf(stderr, "  Frequency: %llu MHz\n", static_cast<unsigned long long>(config.frequency_mhz));
    }

    if (args.use_simulation_engine) {
        // Use SimulationEngine path
        auto engine = SimulationEngine::create(config);
        if (!engine.ok()) {
            std::fprintf(stderr, "Error creating simulation engine: %s\n",
                         engine.error().message().c_str());
            return 1;
        }

        // Create instruction source from trace file
        TraceInputConfig trace_cfg;
        trace_cfg.file_path = args.trace_file;
        trace_cfg.format = parse_trace_format(args.trace_format);
        trace_cfg.max_instructions = args.max_instructions;
        trace_cfg.skip_instructions = args.skip_instructions;

        auto source = create_source(trace_cfg);
        if (!source) {
            std::fprintf(stderr, "Error: could not create instruction source for '%s'\n",
                         args.trace_file.c_str());
            return 1;
        }

        if (args.verbose) {
            std::fprintf(stderr, "Running with SimulationEngine...\n");
        }

        // Wrap the InstructionSource in a functor for the engine
        auto next_instr = [&source]() -> std::optional<Result<Instruction>> {
            auto result = source->next();
            if (result.has_error()) return std::nullopt;
            auto opt = result.value();
            if (!opt.has_value()) return std::nullopt;
            return std::move(*opt);
        };

        auto metrics = engine.value()->run_with_limit(next_instr, args.max_cycles);
        if (!metrics.ok()) {
            std::fprintf(stderr, "Error during simulation: %s\n",
                         metrics.error().message().c_str());
            return 1;
        }

        if (args.json_output) {
            print_json_metrics(metrics.value());
        } else {
            std::cout << metrics.value().summary() << std::endl;
        }

        // Save trace if requested
        if (args.save_trace) {
            auto trace_text = engine.value()->trace().write_text();
            std::ofstream out(args.trace_output_file);
            if (out.is_open()) {
                out << trace_text;
                std::fprintf(stderr, "Trace saved to %s\n", args.trace_output_file.c_str());
            } else {
                std::fprintf(stderr, "Warning: could not open trace output file '%s'\n",
                             args.trace_output_file.c_str());
            }
        }
    } else {
        // Use CPUEmulator path
        std::unique_ptr<CPUEmulator> cpu;
        if (args.enable_visualization) {
            VisualizationConfig viz_cfg;
            viz_cfg.enabled = true;
            viz_cfg.port = args.viz_port;
            auto result = CPUEmulator::create_with_visualization(config, viz_cfg);
            if (!result.ok()) {
                std::fprintf(stderr, "Error creating CPU emulator: %s\n",
                             result.error().message().c_str());
                return 1;
            }
            cpu = std::move(result.value());
        } else {
            auto result = CPUEmulator::create(config);
            if (!result.ok()) {
                std::fprintf(stderr, "Error creating CPU emulator: %s\n",
                             result.error().message().c_str());
                return 1;
            }
            cpu = std::move(result.value());
        }

        // Create instruction source from trace file
        TraceInputConfig trace_cfg;
        trace_cfg.file_path = args.trace_file;
        trace_cfg.format = parse_trace_format(args.trace_format);
        trace_cfg.max_instructions = args.max_instructions;
        trace_cfg.skip_instructions = args.skip_instructions;

        auto source = create_source(trace_cfg);
        if (!source) {
            std::fprintf(stderr, "Error: could not create instruction source for '%s'\n",
                         args.trace_file.c_str());
            return 1;
        }

        if (args.verbose) {
            std::fprintf(stderr, "Running with CPUEmulator...\n");
        }

        // Wrap the InstructionSource in a functor for the emulator
        auto next_instr = [&source]() -> std::optional<Result<Instruction>> {
            auto result = source->next();
            if (result.has_error()) return std::nullopt;
            auto opt = result.value();
            if (!opt.has_value()) return std::nullopt;
            return std::move(*opt);
        };

        auto metrics = cpu->run_with_limit(next_instr, args.max_cycles);
        if (!metrics.ok()) {
            std::fprintf(stderr, "Error during simulation: %s\n",
                         metrics.error().message().c_str());
            return 1;
        }

        if (args.json_output) {
            print_json_metrics(metrics.value());
        } else {
            std::cout << metrics.value().summary() << std::endl;
        }

        // Save trace if requested
        if (args.save_trace) {
            auto trace_text = cpu->trace().write_text();
            std::ofstream out(args.trace_output_file);
            if (out.is_open()) {
                out << trace_text;
                std::fprintf(stderr, "Trace saved to %s\n", args.trace_output_file.c_str());
            } else {
                std::fprintf(stderr, "Warning: could not open trace output file '%s'\n",
                             args.trace_output_file.c_str());
            }
        }
    }

    return 0;
}

int run_multi_instance(const CliArgs& args) {
    using namespace arm_cpu;

    auto config = build_config(args);

    MultiRunConfig run_cfg;
    run_cfg.max_cycles = args.max_cycles;
    run_cfg.max_instructions = args.max_instructions;
    run_cfg.parallel = true;
    run_cfg.num_threads = args.num_threads;

    InstanceManager manager(config, run_cfg);

    if (args.verbose) {
        std::fprintf(stderr, "Creating %zu instances...\n", args.num_instances);
    }

    // Create instances
    for (std::size_t i = 0; i < args.num_instances; ++i) {
        auto id = manager.create_instance();
        if (args.verbose) {
            std::fprintf(stderr, "  Created %s\n", id.to_string().c_str());
        }
    }

    // Run all instances in parallel
    if (args.verbose) {
        std::fprintf(stderr, "Running %zu instances in parallel...\n", args.num_instances);
    }

    auto results = manager.run_all_parallel();
    if (!results.ok()) {
        std::fprintf(stderr, "Error during multi-instance simulation: %s\n",
                     results.error().message().c_str());
        return 1;
    }

    if (args.json_output) {
        const auto& agg = results.value();
        std::printf(
            "{\n"
            "  \"total_instances\": %zu,\n"
            "  \"successful_instances\": %zu,\n"
            "  \"failed_instances\": %zu,\n"
            "  \"avg_ipc\": %.6f,\n"
            "  \"min_ipc\": %.6f,\n"
            "  \"max_ipc\": %.6f,\n"
            "  \"avg_cache_hit_rate\": %.6f,\n"
            "  \"total_execution_time_ms\": %llu\n"
            "}\n",
            agg.total_instances,
            agg.successful_instances,
            agg.failed_instances,
            agg.avg_ipc,
            agg.min_ipc,
            agg.max_ipc,
            agg.avg_cache_hit_rate,
            static_cast<unsigned long long>(agg.total_execution_time_ms)
        );
    } else {
        std::cout << results.value().summary() << std::endl;
    }

    return 0;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    using namespace arm_cpu;

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    CliArgs args;
    if (!parse_args(argc, argv, args)) {
        print_usage(argv[0]);
        return 1;
    }

    if (args.trace_file.empty() && !args.multi_instance) {
        std::fprintf(stderr, "Error: no trace file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    if (args.multi_instance) {
        return run_multi_instance(args);
    } else {
        return run_single(args);
    }
}
