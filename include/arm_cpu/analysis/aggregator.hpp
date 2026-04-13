#pragma once

/// @file aggregator.hpp
/// @brief Statistics aggregation for large-scale simulation data.
///
/// Provides binned timeline statistics for visualization and analysis.

#include "arm_cpu/analysis/function_profiler.hpp"
#include "arm_cpu/analysis/anomaly_detector.hpp"

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>

namespace arm_cpu {

// =====================================================================
// TimelinePoint - A point on the timeline
// =====================================================================
struct TimelinePoint {
    uint64_t cycle = 0;
    uint64_t instr = 0;
    double value = 0.0;

    static TimelinePoint create(uint64_t c, uint64_t i, double v) {
        return TimelinePoint{c, i, v};
    }
};

// =====================================================================
// StatsBin - Statistics bin for aggregated data
// =====================================================================
struct StatsBin {
    uint64_t start_instr = 0;
    uint64_t end_instr = 0;
    uint64_t start_cycle = 0;
    uint64_t end_cycle = 0;
    uint64_t instr_count = 0;
    uint64_t cycle_count = 0;
    double ipc = 0.0;
    uint64_t mem_ops = 0;
    uint64_t branch_ops = 0;
    uint64_t l1_misses = 0;
    uint64_t l2_misses = 0;
    uint64_t bubbles = 0;
    double avg_latency = 0.0;
};

// =====================================================================
// CacheStatistics
// =====================================================================
struct CacheStatistics {
    uint64_t l1_d_hits = 0;
    uint64_t l1_d_misses = 0;
    uint64_t l1_i_hits = 0;
    uint64_t l1_i_misses = 0;
    uint64_t l2_hits = 0;
    uint64_t l2_misses = 0;
    double l1_miss_rate = 0.0;
    double l2_miss_rate = 0.0;
};

// =====================================================================
// PipelineUtilization
// =====================================================================
struct PipelineUtilization {
    double window_utilization = 0.0;
    double peak_window_utilization = 0.0;
    double lsq_utilization = 0.0;
    double peak_lsq_utilization = 0.0;
    double issue_utilization = 0.0;
    double commit_utilization = 0.0;
};

// =====================================================================
// AggregatedStatistics
// =====================================================================
struct AggregatedStatistics {
    uint64_t total_instructions = 0;
    uint64_t total_cycles = 0;
    double ipc = 0.0;
    uint64_t bin_size = 10000;
    uint64_t bin_count = 0;

    std::vector<TimelinePoint> ipc_timeline;
    std::vector<TimelinePoint> throughput_timeline;
    std::vector<TimelinePoint> l1_miss_timeline;
    std::vector<TimelinePoint> l2_miss_timeline;

    std::vector<StatsBin> bins;
    std::vector<FunctionStats> function_stats;
    CacheStatistics cache_stats;
    PipelineUtilization pipeline_utilization;
    std::vector<Anomaly> anomalies;
};

// =====================================================================
// StatsAggregator
// =====================================================================
class StatsAggregator {
public:
    explicit StatsAggregator(uint64_t bin_size);

    /// Record an instruction completion.
    void record_instruction(uint64_t instr_id, uint64_t start_cycle, uint64_t end_cycle,
                            bool is_memory, bool is_branch, bool l1_miss, bool l2_miss);

    /// Record a pipeline bubble.
    void record_bubble(uint64_t cycle);

    /// Register a function for profiling.
    void register_function(uint64_t start_pc, uint64_t end_pc, const std::string& name);

    /// Record instruction for function profiling.
    void record_function_instr(uint64_t pc, uint64_t cycles);

    /// Finalize aggregation and return statistics.
    AggregatedStatistics finalize();

private:
    void finalize_current_bin();

    uint64_t bin_size_;
    StatsBin current_bin_;
    std::vector<StatsBin> bins_;
    std::unordered_map<uint64_t, FunctionStats> function_stats_;
    std::unordered_map<uint64_t, uint64_t> function_map_;

    uint64_t total_instructions_ = 0;
    uint64_t total_cycles_ = 0;
    uint64_t total_l1_misses_ = 0;
    uint64_t total_l2_misses_ = 0;
    uint64_t total_mem_ops_ = 0;
    uint64_t total_branch_ops_ = 0;
    double total_latency_ = 0.0;
    uint64_t latency_count_ = 0;
};

} // namespace arm_cpu
