#pragma once

/// @file topdown.hpp
/// @brief Intel TopDown performance analysis methodology.
///
/// Implements the TopDown hierarchy for identifying performance bottlenecks:
/// Level 1: Retiring, Bad Speculation, Frontend Bound, Backend Bound.

#include <cstdint>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace arm_cpu {

// =====================================================================
// TopDownMetrics - Level 1 breakdown
// =====================================================================
struct TopDownMetrics {
    double retiring_pct = 0.0;
    double bad_speculation_pct = 0.0;
    double frontend_bound_pct = 0.0;
    double backend_bound_pct = 0.0;
};

// =====================================================================
// FrontendBound - Detailed frontend breakdown
// =====================================================================
struct FrontendBound {
    double fetch_latency_pct = 0.0;
    double fetch_bandwidth_pct = 0.0;
    double icache_miss_rate = 0.0;
    double itlb_miss_rate = 0.0;
};

// =====================================================================
// BackendBound - Detailed backend breakdown
// =====================================================================
struct BackendBound {
    double memory_bound_pct = 0.0;
    double core_bound_pct = 0.0;
    double l1_dcache_miss_rate = 0.0;
    double l2_cache_miss_rate = 0.0;
    double l3_cache_miss_rate = 0.0;
    double avg_mem_latency = 0.0;
};

// =====================================================================
// BadSpeculation - Speculation waste breakdown
// =====================================================================
struct BadSpeculation {
    double branch_mispred_rate = 0.0;
    double wasted_instructions_pct = 0.0;
};

// =====================================================================
// Retiring - Useful work breakdown
// =====================================================================
struct Retiring {
    double alu_ops_pct = 0.0;
    double memory_ops_pct = 0.0;
    double branch_ops_pct = 0.0;
    double simd_ops_pct = 0.0;
};

// =====================================================================
// StageUtilization - Pipeline stage utilization
// =====================================================================
struct StageUtilization {
    double fetch_util = 0.0;
    double decode_util = 0.0;
    double rename_util = 0.0;
    double dispatch_util = 0.0;
    double issue_util = 0.0;
    double execute_util = 0.0;
    double memory_util = 0.0;
    double commit_util = 0.0;
};

// =====================================================================
// Hotspot - Hot function/PC range
// =====================================================================
struct Hotspot {
    std::string name;
    uint64_t start_pc = 0;
    uint64_t end_pc = 0;
    uint64_t instruction_count = 0;
    uint64_t cycle_count = 0;
    double cycle_pct = 0.0;
    double ipc = 0.0;
};

// =====================================================================
// CycleDistribution - Issue width distribution
// =====================================================================
struct CycleDistribution {
    uint64_t full_issue_cycles = 0;
    uint64_t partial_issue_cycles = 0;
    uint64_t stall_cycles = 0;
    uint64_t memory_stall_cycles = 0;
    uint64_t dependency_stall_cycles = 0;
};

// =====================================================================
// TopDownReport - Complete analysis report
// =====================================================================
struct TopDownReport {
    uint64_t total_cycles = 0;
    uint64_t total_instructions = 0;
    double ipc = 0.0;

    TopDownMetrics topdown;
    FrontendBound frontend_bound;
    BackendBound backend_bound;
    BadSpeculation bad_speculation;
    Retiring retiring;

    StageUtilization stage_utilization;
    std::vector<Hotspot> hotspots;
    CycleDistribution cycle_distribution;
};

// =====================================================================
// TopDownAnalyzer
// =====================================================================
class TopDownAnalyzer {
public:
    TopDownAnalyzer() = default;

    /// Record a cycle with the given issue count.
    void record_cycle(uint64_t issue_count, uint64_t issue_width);

    /// Record an instruction retirement.
    void record_instruction(uint64_t pc, bool is_memory, bool is_branch, uint64_t cycles);

    /// Record a memory access.
    void record_memory_access(bool l1_hit, bool l2_hit, bool l3_hit, uint64_t latency);

    /// Record a branch prediction outcome.
    void record_branch_prediction(bool mispredicted);

    /// Generate the full analysis report.
    TopDownReport generate_report(const StageUtilization& stage_util) const;

private:
    uint64_t total_cycles_ = 0;
    uint64_t total_instructions_ = 0;

    uint64_t fetch_active_cycles_ = 0;
    uint64_t decode_active_cycles_ = 0;
    uint64_t rename_active_cycles_ = 0;
    uint64_t dispatch_active_cycles_ = 0;
    uint64_t issue_active_cycles_ = 0;
    uint64_t execute_active_cycles_ = 0;
    uint64_t memory_active_cycles_ = 0;
    uint64_t commit_active_cycles_ = 0;

    uint64_t frontend_stall_cycles_ = 0;
    uint64_t backend_stall_cycles_ = 0;
    uint64_t speculation_waste_cycles_ = 0;

    uint64_t alu_instructions_ = 0;
    uint64_t memory_instructions_ = 0;
    uint64_t branch_instructions_ = 0;
    uint64_t simd_instructions_ = 0;

    uint64_t l1_misses_ = 0;
    uint64_t l2_misses_ = 0;
    uint64_t l3_misses_ = 0;
    uint64_t total_memory_accesses_ = 0;
    uint64_t total_memory_latency_ = 0;

    uint64_t branch_predictions_ = 0;
    uint64_t branch_mispredictions_ = 0;

    uint64_t full_issue_cycles_ = 0;
    uint64_t partial_issue_cycles_ = 0;
    uint64_t no_issue_cycles_ = 0;

    std::unordered_map<uint64_t, uint64_t> pc_histogram_;
    std::unordered_map<uint64_t, uint64_t> pc_cycles_;
};

} // namespace arm_cpu
