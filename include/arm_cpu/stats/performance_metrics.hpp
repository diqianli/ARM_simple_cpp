#pragma once

/// @file performance_metrics.hpp
/// @brief Performance metrics definitions for the ARM CPU emulator.
///
/// Provides snapshot metrics structures and summary/reporting utilities.
/// Ported from Rust src/stats/metrics.rs.

#include "arm_cpu/types.hpp"

#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace arm_cpu {

// OpcodeTypeHash is defined in arm_cpu/types.hpp

// =====================================================================
// BranchPredictorMetrics — branch prediction statistics
// =====================================================================
struct BranchPredictorMetrics {
    uint64_t branches = 0;
    uint64_t taken = 0;
    uint64_t not_taken = 0;
    uint64_t correct_predictions = 0;
    uint64_t mispredictions = 0;
    uint64_t btb_hits = 0;
    uint64_t btb_misses = 0;
    uint64_t ras_hits = 0;
    uint64_t ras_misses = 0;
    uint64_t squashes = 0;

    double accuracy() const {
        return (branches > 0) ? static_cast<double>(correct_predictions) / static_cast<double>(branches) : 0.0;
    }
    double btb_hit_rate() const {
        return (btb_hits + btb_misses > 0) ? static_cast<double>(btb_hits) / static_cast<double>(btb_hits + btb_misses) : 0.0;
    }
    double mpki(uint64_t total_insts) const {
        return (total_insts > 0) ? (static_cast<double>(mispredictions) / static_cast<double>(total_insts)) * 1000.0 : 0.0;
    }
};

// =====================================================================
// FUUtilization — functional unit utilization statistics
// =====================================================================
struct FUUtilization {
    uint64_t int_alu_cycles = 0;
    uint64_t int_mul_cycles = 0;
    uint64_t load_cycles = 0;
    uint64_t store_cycles = 0;
    uint64_t branch_cycles = 0;
    uint64_t fp_simd_cycles = 0;
    uint64_t total_available_cycles = 0;
    uint64_t int_alu_issued = 0;
    uint64_t load_issued = 0;
    uint64_t store_issued = 0;
    uint64_t branch_issued = 0;
    uint64_t fp_simd_issued = 0;
};

// =====================================================================
// PipelineStallMetrics — pipeline stall breakdown
// =====================================================================
struct PipelineStallMetrics {
    uint64_t rob_full_stalls = 0;
    uint64_t iq_full_stalls = 0;
    uint64_t lsq_full_stalls = 0;
    uint64_t cache_miss_stalls = 0;
    uint64_t branch_mispredict_stalls = 0;
    uint64_t total_stall_cycles = 0;
    uint64_t issue_width_dist[5] = {};  // [0]=0 issued, [1]=1, [2]=2, [3]=3, [4]=4+
};

// =====================================================================
// DetailedCacheMetrics — detailed per-level cache statistics
// =====================================================================
struct DetailedCacheMetrics {
    uint64_t l1_reads = 0;
    uint64_t l1_writes = 0;
    uint64_t l1_read_misses = 0;
    uint64_t l1_write_misses = 0;
    uint64_t l1_writebacks = 0;
    uint64_t l1_evictions = 0;
    double l1_avg_miss_latency = 0.0;
    uint64_t l2_reads = 0;
    uint64_t l2_writes = 0;
    uint64_t l2_read_misses = 0;
    uint64_t l2_write_misses = 0;
    uint64_t l2_writebacks = 0;
    uint64_t l2_evictions = 0;
    double l2_avg_miss_latency = 0.0;
};

// =====================================================================
// IntervalSample — per-interval performance sample for time series
// =====================================================================
struct IntervalSample {
    uint64_t cycle_start = 0;
    uint64_t cycle_end = 0;
    uint64_t instructions = 0;
    uint64_t cycles = 0;
    double ipc = 0.0;

    uint64_t cache_accesses = 0;
    uint64_t cache_misses = 0;
    double cache_miss_rate = 0.0;

    uint64_t branches = 0;
    uint64_t mispredictions = 0;
    double branch_mispred_rate = 0.0;

    uint64_t stall_cycles = 0;
    double stall_rate = 0.0;
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

// =====================================================================
// PerformanceMetrics — overall performance snapshot
// =====================================================================
struct PerformanceMetrics {
    uint64_t total_instructions = 0;
    uint64_t total_cycles = 0;
    double ipc = 0.0;
    double l1_hit_rate = 0.0;
    double l2_hit_rate = 0.0;
    double l1_mpki = 0.0;
    double l2_mpki = 0.0;
    double memory_instr_pct = 0.0;
    double branch_instr_pct = 0.0;
    double avg_load_latency = 0.0;
    double avg_store_latency = 0.0;

    /// Per-opcode-type instruction counts (e.g., {Add: 1200, Load: 800, ...})
    std::unordered_map<OpcodeType, uint64_t, OpcodeTypeHash> instr_by_type;

    /// GEM5-like detailed profiling dimensions
    BranchPredictorMetrics branch_stats;
    FUUtilization fu_stats;
    PipelineStallMetrics stall_stats;
    DetailedCacheMetrics cache_detail;

    /// Calculate execution time in nanoseconds given frequency in MHz
    uint64_t execution_time_ns(uint64_t frequency_mhz) const;

    /// Calculate throughput in MIPS (Million Instructions Per Second)
    double throughput_mips(uint64_t frequency_mhz) const;

    /// Format as a summary string
    std::string summary() const;
};

} // namespace arm_cpu
