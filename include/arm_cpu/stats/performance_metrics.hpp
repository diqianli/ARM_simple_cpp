#pragma once

/// @file performance_metrics.hpp
/// @brief Performance metrics definitions for the ARM CPU emulator.
///
/// Provides snapshot metrics structures and summary/reporting utilities.
/// Ported from Rust src/stats/metrics.rs.

#include <cstdint>
#include <cstddef>
#include <string>

namespace arm_cpu {

// =====================================================================
// PerformanceMetrics — overall performance snapshot
// =====================================================================
struct PerformanceMetrics {
    uint64_t total_instructions = 0;
    uint64_t total_cycles = 0;
    double ipc = 0.0;
    double cpi = 0.0;
    double l1_hit_rate = 0.0;
    double l2_hit_rate = 0.0;
    double l1_mpki = 0.0;
    double l2_mpki = 0.0;
    double memory_instr_pct = 0.0;
    double branch_instr_pct = 0.0;
    double avg_load_latency = 0.0;
    double avg_store_latency = 0.0;

    /// Calculate execution time in nanoseconds given frequency in MHz
    uint64_t execution_time_ns(uint64_t frequency_mhz) const;

    /// Calculate throughput in MIPS (Million Instructions Per Second)
    double throughput_mips(uint64_t frequency_mhz) const;

    /// Format as a summary string
    std::string summary() const;
};

// =====================================================================
// CacheMetrics — cache performance snapshot
// =====================================================================
struct CacheMetrics {
    uint64_t accesses = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;
    double hit_rate = 0.0;
    double miss_rate = 0.0;
    double mpki = 0.0;
};

// =====================================================================
// ExecutionMetrics — pipeline execution snapshot
// =====================================================================
struct ExecutionMetrics {
    uint64_t dispatched = 0;
    uint64_t issued = 0;
    uint64_t completed = 0;
    uint64_t committed = 0;
    double avg_dispatch_issue_latency = 0.0;
    double avg_issue_complete_latency = 0.0;
    double avg_complete_commit_latency = 0.0;
    double avg_window_occupancy = 0.0;
    std::size_t peak_window_occupancy = 0;
};

// =====================================================================
// MemoryMetrics — memory subsystem performance snapshot
// =====================================================================
struct MemoryMetrics {
    uint64_t loads = 0;
    uint64_t stores = 0;
    uint64_t bytes_read = 0;
    uint64_t bytes_written = 0;
    double avg_load_latency = 0.0;
    double avg_store_latency = 0.0;
    double bandwidth = 0.0;
};

} // namespace arm_cpu
