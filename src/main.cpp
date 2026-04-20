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
#include <chrono>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <iomanip>
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
    std::string output_file;                   // Konata JSON output path
    std::string viz_format = "json";            // "json" or "kanata"
    std::string preset;                        // "default", "high_performance", "minimal", "gem5_o3_arm_v7a"
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
        "  --preset <name>          Config preset: default, high_performance, minimal, gem5_o3_arm_v7a\n"
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
        "  -o, --output <file>      Save Konata visualization JSON to file (default: output/<stem>_YYYYMMDD_HHMM.json)\n"
        "  --viz-format <fmt>       Visualization format: json, kanata (default: json)\n"
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
        else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.output_file = argv[++i];
        }
        else if (arg == "--viz-format") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.viz_format = argv[++i];
        }
        else if (arg == "--preset") {
            if (i + 1 >= argc) { std::fprintf(stderr, "Error: missing argument for %s\n", arg.data()); return false; }
            args.preset = argv[++i];
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
    if (fmt == "elf") return arm_cpu::TraceFormat::Elf;
    std::fprintf(stderr, "Warning: unknown trace format '%.*s', defaulting to text\n",
                 static_cast<int>(fmt.size()), fmt.data());
    return arm_cpu::TraceFormat::Text;
}

arm_cpu::CPUConfig build_config(const CliArgs& args) {
    arm_cpu::CPUConfig config;

    if (args.preset == "high_performance") {
        config = arm_cpu::CPUConfig::high_performance();
    } else if (args.preset == "minimal") {
        config = arm_cpu::CPUConfig::minimal();
    } else if (args.preset == "gem5_o3_arm_v7a") {
        config = arm_cpu::CPUConfig::gem5_o3_arm_v7a();
    } else {
        config = arm_cpu::CPUConfig::default_config();
    }

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

/// Build the performance metrics JSON string (without the trailing closing brace).
/// Returns the JSON text for all metric sections.
std::string build_json_metrics_string(const arm_cpu::PerformanceMetrics& m,
                                     const std::vector<arm_cpu::IntervalSample>& time_series = {},
                                     const std::vector<arm_cpu::WallTimeSample>& wall_time_series = {}) {
    std::string json;
    json.reserve(4096);

    auto fmt_u64 = [](unsigned long long v) -> std::string {
        return std::to_string(v);
    };
    auto fmt_f = [](double v, int prec = 6) -> std::string {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*f", prec, v);
        return buf;
    };

    json += "{\n"
        "  \"total_instructions\": " + fmt_u64(m.total_instructions) + ",\n"
        "  \"total_cycles\": " + fmt_u64(m.total_cycles) + ",\n"
        "  \"ipc\": " + fmt_f(m.ipc) + ",\n"
        "  \"l1_hit_rate\": " + fmt_f(m.l1_hit_rate) + ",\n"
        "  \"l2_hit_rate\": " + fmt_f(m.l2_hit_rate) + ",\n"
        "  \"l1_mpki\": " + fmt_f(m.l1_mpki) + ",\n"
        "  \"l2_mpki\": " + fmt_f(m.l2_mpki) + ",\n"
        "  \"memory_instr_pct\": " + fmt_f(m.memory_instr_pct) + ",\n"
        "  \"branch_instr_pct\": " + fmt_f(m.branch_instr_pct) + ",\n"
        "  \"avg_load_latency\": " + fmt_f(m.avg_load_latency) + ",\n"
        "  \"avg_store_latency\": " + fmt_f(m.avg_store_latency) + ",\n";

    // Instruction breakdown by opcode type
    json += "  \"instructions\": {\n";
    bool first = true;
    for (const auto& [opcode, count] : m.instr_by_type) {
        if (count == 0) continue;
        if (!first) json += ",\n";
        first = false;
        double pct = (m.total_instructions > 0)
            ? static_cast<double>(count) / static_cast<double>(m.total_instructions) * 100.0
            : 0.0;
        json += "    \"" + std::string(arm_cpu::opcode_to_string(opcode)) +
                "\": { \"count\": " + fmt_u64(count) +
                ", \"pct\": " + fmt_f(pct, 2) + " }";
    }
    if (!first) json += "\n";
    json += "  },\n";

    // Branch predictor metrics
    const auto& bs = m.branch_stats;
    json +=
        "  \"branch_predictor\": {\n"
        "    \"total_branches\": " + fmt_u64(bs.branches) + ",\n"
        "    \"taken\": " + fmt_u64(bs.taken) + ",\n"
        "    \"not_taken\": " + fmt_u64(bs.not_taken) + ",\n"
        "    \"correct\": " + fmt_u64(bs.correct_predictions) + ",\n"
        "    \"mispredictions\": " + fmt_u64(bs.mispredictions) + ",\n"
        "    \"accuracy\": " + fmt_f(bs.accuracy() * 100.0, 2) + ",\n"
        "    \"btb_hits\": " + fmt_u64(bs.btb_hits) + ",\n"
        "    \"btb_misses\": " + fmt_u64(bs.btb_misses) + ",\n"
        "    \"btb_hit_rate\": " + fmt_f(bs.btb_hit_rate() * 100.0, 2) + ",\n"
        "    \"ras_hits\": " + fmt_u64(bs.ras_hits) + ",\n"
        "    \"ras_misses\": " + fmt_u64(bs.ras_misses) + ",\n"
        "    \"squashes\": " + fmt_u64(bs.squashes) + ",\n"
        "    \"branch_mpki\": " + fmt_f(bs.mpki(m.total_instructions), 2) + "\n"
        "  },\n";

    // FU utilization
    const auto& fu = m.fu_stats;
    json +=
        "  \"fu_utilization\": {\n"
        "    \"int_alu_busy_cycles\": " + fmt_u64(fu.int_alu_cycles) + ",\n"
        "    \"int_alu_issued\": " + fmt_u64(fu.int_alu_issued) + ",\n"
        "    \"int_mul_busy_cycles\": " + fmt_u64(fu.int_mul_cycles) + ",\n"
        "    \"load_busy_cycles\": " + fmt_u64(fu.load_cycles) + ",\n"
        "    \"load_issued\": " + fmt_u64(fu.load_issued) + ",\n"
        "    \"store_busy_cycles\": " + fmt_u64(fu.store_cycles) + ",\n"
        "    \"store_issued\": " + fmt_u64(fu.store_issued) + ",\n"
        "    \"branch_busy_cycles\": " + fmt_u64(fu.branch_cycles) + ",\n"
        "    \"branch_issued\": " + fmt_u64(fu.branch_issued) + ",\n"
        "    \"fp_simd_busy_cycles\": " + fmt_u64(fu.fp_simd_cycles) + ",\n"
        "    \"fp_simd_issued\": " + fmt_u64(fu.fp_simd_issued) + ",\n"
        "    \"total_cycles\": " + fmt_u64(fu.total_available_cycles) + "\n"
        "  },\n";

    // Pipeline stalls
    const auto& st = m.stall_stats;
    json +=
        "  \"pipeline_stalls\": {\n"
        "    \"rob_full\": " + fmt_u64(st.rob_full_stalls) + ",\n"
        "    \"iq_full\": " + fmt_u64(st.iq_full_stalls) + ",\n"
        "    \"lsq_full\": " + fmt_u64(st.lsq_full_stalls) + ",\n"
        "    \"cache_miss\": " + fmt_u64(st.cache_miss_stalls) + ",\n"
        "    \"branch_mispredict\": " + fmt_u64(st.branch_mispredict_stalls) + ",\n"
        "    \"total_stall_cycles\": " + fmt_u64(st.total_stall_cycles) + ",\n"
        "    \"issue_width_dist\": { \"0\": " + fmt_u64(st.issue_width_dist[0]) +
        ", \"1\": " + fmt_u64(st.issue_width_dist[1]) +
        ", \"2\": " + fmt_u64(st.issue_width_dist[2]) +
        ", \"3\": " + fmt_u64(st.issue_width_dist[3]) +
        ", \"4_plus\": " + fmt_u64(st.issue_width_dist[4]) + " }\n"
        "  },\n";

    // Detailed cache metrics
    const auto& cd = m.cache_detail;
    json +=
        "  \"cache_detail\": {\n"
        "    \"l1\": { \"reads\": " + fmt_u64(cd.l1_reads) +
        ", \"writes\": " + fmt_u64(cd.l1_writes) +
        ", \"read_misses\": " + fmt_u64(cd.l1_read_misses) +
        ", \"write_misses\": " + fmt_u64(cd.l1_write_misses) +
        ", \"writebacks\": " + fmt_u64(cd.l1_writebacks) +
        ", \"evictions\": " + fmt_u64(cd.l1_evictions) +
        ", \"avg_miss_latency\": " + fmt_f(cd.l1_avg_miss_latency, 2) + " },\n"
        "    \"l2\": { \"reads\": " + fmt_u64(cd.l2_reads) +
        ", \"writes\": " + fmt_u64(cd.l2_writes) +
        ", \"read_misses\": " + fmt_u64(cd.l2_read_misses) +
        ", \"write_misses\": " + fmt_u64(cd.l2_write_misses) +
        ", \"writebacks\": " + fmt_u64(cd.l2_writebacks) +
        ", \"evictions\": " + fmt_u64(cd.l2_evictions) +
        ", \"avg_miss_latency\": " + fmt_f(cd.l2_avg_miss_latency, 2) + " }\n"
        "  }\n";

    // Time series data
    json += "  ,\n  \"time_series\": {\n"
        "    \"interval\": " + fmt_u64(arm_cpu::StatsCollector::kSampleInterval) + ",\n"
        "    \"samples\": [\n";
    for (std::size_t i = 0; i < time_series.size(); ++i) {
        const auto& s = time_series[i];
        json += "      {"
            "\"cycle_start\": " + fmt_u64(s.cycle_start) + ", "
            "\"cycle_end\": " + fmt_u64(s.cycle_end) + ", "
            "\"ipc\": " + fmt_f(s.ipc) + ", "
            "\"cache_miss_rate\": " + fmt_f(s.cache_miss_rate) + ", "
            "\"branch_mispred_rate\": " + fmt_f(s.branch_mispred_rate) + ", "
            "\"stall_rate\": " + fmt_f(s.stall_rate) +
            "}";
        if (i + 1 < time_series.size()) json += ",";
        json += "\n";
    }
    json += "    ]\n"
        "  }";

    // Wall time series data
    if (!wall_time_series.empty()) {
        json += ",\n  \"wall_time_series\": {\n"
            "    \"sample_interval_sec\": 1.0,\n"
            "    \"samples\": [\n";
        for (std::size_t i = 0; i < wall_time_series.size(); ++i) {
            const auto& s = wall_time_series[i];
            json += "      {"
                "\"wall_time_sec\": " + fmt_f(s.wall_time_sec, 1) + ", "
                "\"total_instructions\": " + fmt_u64(s.total_instructions) + ", "
                "\"total_cycles\": " + fmt_u64(s.total_cycles) + ", "
                "\"instr_per_sec\": " + fmt_f(s.instr_per_sec, 0) +
                "}";
            if (i + 1 < wall_time_series.size()) json += ",";
            json += "\n";
        }
        json += "    ]\n  }";
    }

    return json;
}

/// Helper: write profiling JSON to file. Returns the file path on success, empty on failure.
std::string write_profiling_json(const std::string& stem,
                                  const std::string& metrics_body,
                                  double elapsed_ms,
                                  double instr_per_sec,
                                  double cycles_per_sec) {
    // Determine output directory: output/profiling/
    auto output_dir = std::filesystem::current_path() / "output" / "profiling";
    if (!std::filesystem::exists(std::filesystem::current_path() / "output")) {
        output_dir = std::filesystem::current_path() / ".." / "output" / "profiling";
    }
    std::filesystem::create_directories(output_dir);

    // Timestamp: YYYYMMDD_HHMM
    std::time_t now = std::time(nullptr);
    std::tm local_tm;
    localtime_r(&now, &local_tm);
    char ts[20];
    std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M", &local_tm);

    auto path = output_dir / (stem + "_" + ts + "_perf.json");
    std::ofstream out(path);
    if (!out.is_open()) return "";

    out << metrics_body;
    out << ",\n  \"wall_time_ms\": " << std::fixed << std::setprecision(3) << elapsed_ms << ",\n";
    out << "  \"instr_per_sec\": " << std::fixed << std::setprecision(0) << instr_per_sec << ",\n";
    out << "  \"cycles_per_sec\": " << cycles_per_sec << "\n";
    out << "}\n";

    return path.string();
}

int run_single(const CliArgs& args, int argc, char* argv[]) {
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
        std::size_t fetched_count = 0;
        auto next_instr = [&source, &fetched_count, max_instr = args.max_instructions]() -> std::optional<Result<Instruction>> {
            if (max_instr > 0 && fetched_count >= max_instr) {
                return std::nullopt;  // 达到指令上限，停止获取
            }
            auto result = source->next();
            if (result.has_error()) return std::nullopt;
            auto opt = result.value();
            if (!opt.has_value()) return std::nullopt;
            fetched_count++;
            return std::move(*opt);
        };

        auto sim_start = std::chrono::steady_clock::now();
        auto metrics = engine.value()->run_with_limit(next_instr, args.max_cycles);
        auto sim_end = std::chrono::steady_clock::now();
        if (!metrics.ok()) {
            std::fprintf(stderr, "Error during simulation: %s\n",
                         metrics.error().message().c_str());
            return 1;
        }

        auto elapsed_ms = std::chrono::duration<double, std::milli>(sim_end - sim_start).count();
        const auto& m = metrics.value();
        double instr_per_sec = (elapsed_ms > 0) ? static_cast<double>(m.total_instructions) / (elapsed_ms / 1000.0) : 0.0;
        double cycles_per_sec = (elapsed_ms > 0) ? static_cast<double>(m.total_cycles) / (elapsed_ms / 1000.0) : 0.0;

        if (args.json_output) {
            auto metrics_body = build_json_metrics_string(m);
            std::printf("%s", metrics_body.c_str());
            std::printf(",\n");
            std::printf("  \"wall_time_ms\": %.3f,\n", elapsed_ms);
            std::printf("  \"instr_per_sec\": %.0f,\n", instr_per_sec);
            std::printf("  \"cycles_per_sec\": %.0f\n", cycles_per_sec);
            std::printf("}\n");

            // Write profiling JSON to file
            auto stem = std::filesystem::path(args.trace_file).stem().string();
            auto perf_path = write_profiling_json(stem, metrics_body, elapsed_ms, instr_per_sec, cycles_per_sec);
            if (!perf_path.empty()) {
                std::fprintf(stderr, "  Profiling: %s\n", perf_path.c_str());
            }
        } else {
            std::cout << m.summary() << std::endl;
            std::fprintf(stderr, "\n--- Timing ---\n");
            std::fprintf(stderr, "  Wall time:    %.3f ms\n", elapsed_ms);
            std::fprintf(stderr, "  Instructions: %llu (%.0f instr/s)\n",
                         static_cast<unsigned long long>(m.total_instructions), instr_per_sec);
            std::fprintf(stderr, "  Cycles:       %llu (%.0f cycles/s)\n",
                         static_cast<unsigned long long>(m.total_cycles), cycles_per_sec);
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

        // SimulationEngine has no Konata export
        std::fprintf(stderr, "\n========================================\n");
        std::fprintf(stderr, "  Input:  %s\n", args.trace_file.c_str());
        std::fprintf(stderr, "  Output: (SimulationEngine has no Konata export)\n");
        std::fprintf(stderr, "========================================\n\n");
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
        std::size_t fetched_count = 0;
        auto next_instr = [&source, &fetched_count, max_instr = args.max_instructions]() -> std::optional<Result<Instruction>> {
            if (max_instr > 0 && fetched_count >= max_instr) {
                return std::nullopt;  // 达到指令上限，停止获取
            }
            auto result = source->next();
            if (result.has_error()) return std::nullopt;
            auto opt = result.value();
            if (!opt.has_value()) return std::nullopt;
            fetched_count++;
            return std::move(*opt);
        };

        auto sim_start = std::chrono::steady_clock::now();
        auto metrics = cpu->run_with_limit(next_instr, args.max_cycles);
        auto sim_end = std::chrono::steady_clock::now();
        if (!metrics.ok()) {
            std::fprintf(stderr, "Error during simulation: %s\n",
                         metrics.error().message().c_str());
            return 1;
        }

        auto elapsed_ms = std::chrono::duration<double, std::milli>(sim_end - sim_start).count();
        const auto& m = metrics.value();
        double instr_per_sec = (elapsed_ms > 0) ? static_cast<double>(m.total_instructions) / (elapsed_ms / 1000.0) : 0.0;
        double cycles_per_sec = (elapsed_ms > 0) ? static_cast<double>(m.total_cycles) / (elapsed_ms / 1000.0) : 0.0;

        if (args.json_output) {
            auto metrics_body = build_json_metrics_string(m, cpu->stats().interval_samples(), cpu->stats().wall_time_samples());
            std::printf("%s", metrics_body.c_str());
            std::printf(",\n");
            std::printf("  \"wall_time_ms\": %.3f,\n", elapsed_ms);
            std::printf("  \"instr_per_sec\": %.0f,\n", instr_per_sec);
            std::printf("  \"cycles_per_sec\": %.0f\n", cycles_per_sec);
            std::printf("}\n");

            // Write profiling JSON to file
            auto stem = std::filesystem::path(args.trace_file).stem().string();
            auto perf_path = write_profiling_json(stem, metrics_body, elapsed_ms, instr_per_sec, cycles_per_sec);
            if (!perf_path.empty()) {
                std::fprintf(stderr, "  Profiling: %s\n", perf_path.c_str());
            }
        } else {
            std::cout << m.summary() << std::endl;
            std::fprintf(stderr, "\n--- Timing ---\n");
            std::fprintf(stderr, "  Wall time:    %.3f ms\n", elapsed_ms);
            std::fprintf(stderr, "  Instructions: %llu (%.0f instr/s)\n",
                         static_cast<unsigned long long>(m.total_instructions), instr_per_sec);
            std::fprintf(stderr, "  Cycles:       %llu (%.0f cycles/s)\n",
                         static_cast<unsigned long long>(m.total_cycles), cycles_per_sec);
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

        // Export Konata JSON — fixed output directory with timestamped filename
        std::string output_path;
        if (args.output_file.empty()) {
            // Fixed output directory: <project_root>/output/
            auto output_dir = std::filesystem::current_path() / "output";
            if (!std::filesystem::exists(std::filesystem::current_path() / "output")) {
                output_dir = std::filesystem::current_path() / ".." / "output";
            }
            if (!std::filesystem::exists(output_dir)) {
                output_dir = std::filesystem::path(argv[0]).parent_path() / ".." / "output";
            }
            std::filesystem::create_directories(output_dir);

            // Derive base name from input file (stem)
            auto stem = std::filesystem::path(args.trace_file).stem().string();

            // Timestamp: YYYYMMDD_HHMM
            std::time_t now = std::time(nullptr);
            std::tm local_tm;
            localtime_r(&now, &local_tm);
            char ts[20];
            std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M", &local_tm);

            output_path = (output_dir / (stem + "_" + ts + (args.viz_format == "kanata" ? ".knata" : ".json"))).string();
        } else {
            output_path = args.output_file;
        }

        bool exported;
        if (args.viz_format == "kanata") {
            exported = cpu->visualization_mut().export_kanata_log_to_file(output_path);
        } else {
            exported = cpu->visualization_mut().export_all_konata_to_file(output_path, true);
        }

        std::fprintf(stderr, "\n========================================\n");
        std::fprintf(stderr, "  Input:  %s\n", args.trace_file.c_str());
        if (exported) {
            std::fprintf(stderr, "  Output: %s\n", output_path.c_str());
        } else {
            std::fprintf(stderr, "  Output: (no visualization data to export)\n");
        }
        std::fprintf(stderr, "========================================\n\n");
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
        return run_single(args, argc, argv);
    }
}
