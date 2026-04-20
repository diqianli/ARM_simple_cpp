#pragma once

/// @file config.hpp
/// @brief CPU configuration, trace input config, and validation.

#include "arm_cpu/error.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <string>
#include <chrono>

namespace arm_cpu {

/// Main CPU configuration.
struct CPUConfig {
    // Out-of-Order Engine
    std::size_t window_size = 128;
    std::size_t fetch_width = 8;
    std::size_t issue_width = 4;
    std::size_t commit_width = 4;

    // Memory Subsystem
    std::size_t lsq_size = 64;
    std::size_t load_pipeline_count = 2;
    std::size_t store_pipeline_count = 1;
    std::size_t outstanding_requests = 16;

    // L1 Data Cache
    std::size_t l1_size = 64 * 1024;
    std::size_t l1_associativity = 4;
    std::size_t l1_line_size = 64;
    uint64_t l1_hit_latency = 4;

    // L2 Cache
    std::size_t l2_size = 512 * 1024;
    std::size_t l2_associativity = 8;
    std::size_t l2_line_size = 64;
    uint64_t l2_hit_latency = 12;

    // L3 Cache
    std::size_t l3_size = 8 * 1024 * 1024;
    std::size_t l3_associativity = 16;
    std::size_t l3_line_size = 64;
    uint64_t l3_hit_latency = 40;

    // DDR Memory Controller
    uint64_t ddr_base_latency = 150;
    uint64_t ddr_row_buffer_hit_bonus = 30;
    uint64_t ddr_bank_conflict_penalty = 20;
    std::size_t ddr_num_banks = 8;

    // External Memory (deprecated - use DDR config)
    uint64_t l2_miss_latency = 100;

    // CPU
    uint64_t frequency_mhz = 2000;

    // CHI Interface
    bool enable_chi = false;
    uint64_t chi_request_latency = 2;
    uint64_t chi_response_latency = 2;
    uint64_t chi_data_latency = 4;
    uint64_t chi_snoop_latency = 2;

    // CHI Node Configuration
    uint8_t chi_rnf_node_id = 0;
    uint8_t chi_hnf_node_id = 1;
    uint8_t chi_snf_node_id = 2;

    // CHI QoS Configuration
    uint16_t chi_max_pcrd_credits = 16;
    uint16_t chi_max_outstanding_dbid = 32;
    std::size_t chi_max_retry_queue_size = 64;

    // CHI Directory Configuration
    std::size_t chi_directory_size = 4096;

    // Statistics
    bool enable_trace_output = false;
    std::size_t max_trace_output = 0;

    /// Default configuration
    static CPUConfig default_config() { return CPUConfig{}; }

    /// High-performance configuration
    static CPUConfig high_performance() {
        auto c = default_config();
        c.window_size = 256;
        c.issue_width = 6;
        c.commit_width = 6;
        c.lsq_size = 128;
        c.load_pipeline_count = 4;
        c.store_pipeline_count = 2;
        c.outstanding_requests = 32;
        return c;
    }

    /// Minimal configuration (for testing)
    static CPUConfig minimal() {
        auto c = default_config();
        c.window_size = 16;
        c.issue_width = 2;
        c.commit_width = 2;
        c.lsq_size = 8;
        c.load_pipeline_count = 1;
        c.store_pipeline_count = 1;
        c.outstanding_requests = 4;
        c.l1_size = 4 * 1024;
        c.l1_associativity = 2;
        c.l2_size = 16 * 1024;
        c.l2_associativity = 2;
        return c;
    }

    /// GEM5 O3_ARM_v7a matching configuration
    /// Matches configs/common/cores/arm/O3_ARM_v7a.py defaults:
    ///   ROB=40, IQ=32, LQ=16, SQ=16, fetchWidth=3, issueWidth=8,
    ///   commitWidth=8, ICache=32KB 2-way, DCache=32KB 2-way, L2=1MB 16-way
    static CPUConfig gem5_o3_arm_v7a() {
        auto c = default_config();
        c.window_size = 40;        // ROB size
        c.fetch_width = 3;         // fetchWidth
        c.issue_width = 8;         // issueWidth
        c.commit_width = 8;        // commitWidth
        c.lsq_size = 32;           // LQ=16 + SQ=16
        c.load_pipeline_count = 2;
        c.store_pipeline_count = 2;
        c.outstanding_requests = 16;
        c.l1_size = 32 * 1024;     // 32 KB
        c.l1_associativity = 2;    // 2-way
        c.l1_hit_latency = 2;
        c.l2_size = 1024 * 1024;   // 1 MB
        c.l2_associativity = 16;   // 16-way
        c.l2_hit_latency = 20;
        // Note: GEM5 O3_ARM_v7a has no L3; our simulator requires L3, kept as default
        return c;
    }

    /// Validate configuration parameters
    Result<void> validate() const;

    std::size_t l1_sets() const { return l1_size / (l1_associativity * l1_line_size); }
    std::size_t l2_sets() const { return l2_size / (l2_associativity * l2_line_size); }
    std::size_t l3_sets() const { return l3_size / (l3_associativity * l3_line_size); }

    double cycle_period_ns() const { return 1000.0 / static_cast<double>(frequency_mhz); }

    std::chrono::nanoseconds cycles_to_duration(uint64_t cycles) const {
        auto ns = static_cast<uint64_t>(static_cast<double>(cycles) * cycle_period_ns());
        return std::chrono::nanoseconds(ns);
    }
};

/// Supported trace file formats
enum class TraceFormat : uint8_t {
    Text,
    Binary,
    Json,
    ChampSim,
    ChampSimXz,
    Elf,
};

/// Trace input configuration
struct TraceInputConfig {
    std::string file_path;
    TraceFormat format = TraceFormat::Text;
    std::size_t max_instructions = 0;
    std::size_t skip_instructions = 0;
};

} // namespace arm_cpu
