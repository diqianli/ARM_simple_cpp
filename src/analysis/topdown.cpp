/// @file topdown.cpp
/// @brief TopDown performance analysis implementation.

#include "arm_cpu/analysis/topdown.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>

namespace arm_cpu {

void TopDownAnalyzer::record_cycle(uint64_t issue_count, uint64_t issue_width) {
    ++total_cycles_;

    if (issue_count == 0) {
        ++no_issue_cycles_;
    } else if (issue_count == issue_width) {
        ++full_issue_cycles_;
    } else {
        ++partial_issue_cycles_;
    }
}

void TopDownAnalyzer::record_instruction(uint64_t pc, bool is_memory, bool is_branch, uint64_t cycles) {
    ++total_instructions_;

    pc_histogram_[pc] += 1;
    pc_cycles_[pc] += cycles;

    if (is_memory) {
        ++memory_instructions_;
    } else if (is_branch) {
        ++branch_instructions_;
    } else {
        ++alu_instructions_;
    }
}

void TopDownAnalyzer::record_memory_access(bool l1_hit, bool l2_hit, bool l3_hit, uint64_t latency) {
    ++total_memory_accesses_;
    total_memory_latency_ += latency;

    if (!l1_hit) {
        ++l1_misses_;
        if (!l2_hit) {
            ++l2_misses_;
            if (!l3_hit) {
                ++l3_misses_;
            }
        }
    }
}

void TopDownAnalyzer::record_branch_prediction(bool mispredicted) {
    ++branch_predictions_;
    if (mispredicted) {
        ++branch_mispredictions_;
    }
}

TopDownReport TopDownAnalyzer::generate_report(const StageUtilization& stage_util) const {
    TopDownReport report;

    report.total_cycles = total_cycles_;
    report.total_instructions = total_instructions_;
    report.stage_utilization = stage_util;

    auto total = std::max(total_cycles_, uint64_t(1));

    // TopDown Level 1 metrics (simplified approximations)
    report.topdown.retiring_pct = total_instructions_ > 0
        ? std::min(static_cast<double>(total_instructions_) / static_cast<double>(total) * 100.0, 100.0)
        : 0.0;

    report.topdown.bad_speculation_pct = branch_predictions_ > 0
        ? (static_cast<double>(branch_mispredictions_) / static_cast<double>(branch_predictions_)) * 15.0
        : 0.0;

    report.topdown.frontend_bound_pct = no_issue_cycles_ > 0
        ? (static_cast<double>(no_issue_cycles_) / static_cast<double>(total)) * 30.0
        : 0.0;

    report.topdown.backend_bound_pct = std::max(100.0
        - report.topdown.retiring_pct
        - report.topdown.bad_speculation_pct
        - report.topdown.frontend_bound_pct, 0.0);

    // Frontend bound details
    report.frontend_bound.fetch_latency_pct = report.topdown.frontend_bound_pct * 0.6;
    report.frontend_bound.fetch_bandwidth_pct = report.topdown.frontend_bound_pct * 0.4;
    report.frontend_bound.icache_miss_rate = total_memory_accesses_ > 0
        ? (static_cast<double>(l1_misses_) / static_cast<double>(total_memory_accesses_)) * 100.0
        : 0.0;
    report.frontend_bound.itlb_miss_rate = 0.0;

    // Backend bound details
    report.backend_bound.memory_bound_pct = report.topdown.backend_bound_pct * 0.7;
    report.backend_bound.core_bound_pct = report.topdown.backend_bound_pct * 0.3;
    report.backend_bound.l1_dcache_miss_rate = total_memory_accesses_ > 0
        ? (static_cast<double>(l1_misses_) / static_cast<double>(total_memory_accesses_)) * 100.0
        : 0.0;
    report.backend_bound.l2_cache_miss_rate = l1_misses_ > 0
        ? (static_cast<double>(l2_misses_) / static_cast<double>(l1_misses_)) * 100.0
        : 0.0;
    report.backend_bound.l3_cache_miss_rate = l2_misses_ > 0
        ? (static_cast<double>(l3_misses_) / static_cast<double>(l2_misses_)) * 100.0
        : 0.0;
    report.backend_bound.avg_mem_latency = total_memory_accesses_ > 0
        ? static_cast<double>(total_memory_latency_) / static_cast<double>(total_memory_accesses_)
        : 0.0;

    // Bad speculation details
    report.bad_speculation.branch_mispred_rate = branch_predictions_ > 0
        ? (static_cast<double>(branch_mispredictions_) / static_cast<double>(branch_predictions_)) * 100.0
        : 0.0;
    report.bad_speculation.wasted_instructions_pct = report.topdown.bad_speculation_pct * 0.5;

    // Retiring details
    report.retiring.alu_ops_pct = total_instructions_ > 0
        ? (static_cast<double>(alu_instructions_) / static_cast<double>(total_instructions_)) * 100.0
        : 0.0;
    report.retiring.memory_ops_pct = total_instructions_ > 0
        ? (static_cast<double>(memory_instructions_) / static_cast<double>(total_instructions_)) * 100.0
        : 0.0;
    report.retiring.branch_ops_pct = total_instructions_ > 0
        ? (static_cast<double>(branch_instructions_) / static_cast<double>(total_instructions_)) * 100.0
        : 0.0;
    report.retiring.simd_ops_pct = total_instructions_ > 0
        ? (static_cast<double>(simd_instructions_) / static_cast<double>(total_instructions_)) * 100.0
        : 0.0;

    // Hotspots
    for (const auto& [pc, count] : pc_histogram_) {
        auto cycles_it = pc_cycles_.find(pc);
        uint64_t cycles = cycles_it != pc_cycles_.end() ? cycles_it->second : 0;

        Hotspot h;
        h.name = std::format("PC_{:08X}", pc);
        h.start_pc = pc;
        h.end_pc = pc + 4;
        h.instruction_count = count;
        h.cycle_count = cycles;
        h.cycle_pct = (static_cast<double>(cycles) / static_cast<double>(total)) * 100.0;
        h.ipc = cycles > 0 ? static_cast<double>(count) / static_cast<double>(cycles) : 0.0;
        report.hotspots.push_back(std::move(h));
    }

    // Sort by cycle count descending, keep top 20
    std::partial_sort(report.hotspots.begin(),
                      report.hotspots.begin() + std::min(report.hotspots.size(), std::size_t(20)),
                      report.hotspots.end(),
                      [](const Hotspot& a, const Hotspot& b) { return a.cycle_count > b.cycle_count; });
    if (report.hotspots.size() > 20) {
        report.hotspots.resize(20);
    }

    // Cycle distribution
    report.cycle_distribution.full_issue_cycles = full_issue_cycles_;
    report.cycle_distribution.partial_issue_cycles = partial_issue_cycles_;
    report.cycle_distribution.stall_cycles = no_issue_cycles_;
    report.cycle_distribution.memory_stall_cycles = static_cast<uint64_t>(static_cast<double>(no_issue_cycles_) * 0.4);
    report.cycle_distribution.dependency_stall_cycles = static_cast<uint64_t>(static_cast<double>(no_issue_cycles_) * 0.3);

    // IPC
    report.ipc = total_cycles_ > 0
        ? static_cast<double>(total_instructions_) / static_cast<double>(total_cycles_)
        : 0.0;

    return report;
}

} // namespace arm_cpu
