/// @file aggregator.cpp
/// @brief Statistics aggregation implementation.

#include "arm_cpu/analysis/aggregator.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace arm_cpu {

StatsAggregator::StatsAggregator(uint64_t bin_size)
    : bin_size_(bin_size)
{}

void StatsAggregator::record_instruction(uint64_t instr_id, uint64_t start_cycle,
                                          uint64_t end_cycle, bool is_memory,
                                          bool is_branch, bool l1_miss, bool l2_miss) {
    if (current_bin_.instr_count == 0) {
        current_bin_.start_instr = instr_id;
        current_bin_.start_cycle = start_cycle;
    }

    current_bin_.end_instr = instr_id;
    current_bin_.end_cycle = end_cycle;
    ++current_bin_.instr_count;
    current_bin_.cycle_count = end_cycle >= current_bin_.start_cycle
        ? end_cycle - current_bin_.start_cycle + 1
        : 1;

    if (is_memory) {
        ++current_bin_.mem_ops;
        ++total_mem_ops_;
    }
    if (is_branch) {
        ++current_bin_.branch_ops;
        ++total_branch_ops_;
    }
    if (l1_miss) {
        ++current_bin_.l1_misses;
        ++total_l1_misses_;
    }
    if (l2_miss) {
        ++current_bin_.l2_misses;
        ++total_l2_misses_;
    }

    auto lat = end_cycle >= start_cycle ? end_cycle - start_cycle + 1 : 1;
    total_latency_ += static_cast<double>(lat);
    ++latency_count_;
    current_bin_.avg_latency = total_latency_ / static_cast<double>(latency_count_);

    ++total_instructions_;
    total_cycles_ = std::max(total_cycles_, end_cycle + 1);

    if (current_bin_.instr_count >= bin_size_) {
        finalize_current_bin();
    }
}

void StatsAggregator::record_bubble(uint64_t cycle) {
    ++current_bin_.bubbles;
    total_cycles_ = std::max(total_cycles_, cycle + 1);
}

void StatsAggregator::register_function(uint64_t start_pc, uint64_t end_pc, const std::string& name) {
    for (uint64_t pc = start_pc; pc <= end_pc; ++pc) {
        function_map_[pc] = start_pc;
    }
    FunctionStats stats;
    stats.name = name;
    stats.start_pc = start_pc;
    stats.end_pc = end_pc;
    function_stats_[start_pc] = std::move(stats);
}

void StatsAggregator::record_function_instr(uint64_t pc, uint64_t cycles) {
    auto it = function_map_.find(pc);
    if (it != function_map_.end()) {
        auto stats_it = function_stats_.find(it->second);
        if (stats_it != function_stats_.end()) {
            ++stats_it->second.instruction_count;
            stats_it->second.cycle_count += cycles;
        }
    }
}

void StatsAggregator::finalize_current_bin() {
    if (current_bin_.instr_count == 0) return;

    if (current_bin_.cycle_count > 0) {
        current_bin_.ipc = static_cast<double>(current_bin_.instr_count)
                         / static_cast<double>(current_bin_.cycle_count);
    }

    bins_.push_back(current_bin_);
    current_bin_ = StatsBin{};
}

AggregatedStatistics StatsAggregator::finalize() {
    finalize_current_bin();

    AggregatedStatistics result;
    result.bin_size = bin_size_;

    // Build timelines
    for (const auto& b : bins_) {
        result.ipc_timeline.push_back(TimelinePoint::create(b.start_cycle, b.start_instr, b.ipc));
        double throughput = b.cycle_count > 0
            ? static_cast<double>(b.instr_count) / static_cast<double>(b.cycle_count)
            : 0.0;
        result.throughput_timeline.push_back(TimelinePoint::create(b.start_cycle, b.start_instr, throughput));

        double l1_rate = b.mem_ops > 0
            ? static_cast<double>(b.l1_misses) / static_cast<double>(b.mem_ops)
            : 0.0;
        result.l1_miss_timeline.push_back(TimelinePoint::create(b.start_cycle, b.start_instr, l1_rate));

        double l2_rate = b.l1_misses > 0
            ? static_cast<double>(b.l2_misses) / static_cast<double>(b.l1_misses)
            : 0.0;
        result.l2_miss_timeline.push_back(TimelinePoint::create(b.start_cycle, b.start_instr, l2_rate));
    }

    result.total_instructions = total_instructions_;
    result.total_cycles = total_cycles_;
    result.bin_count = bins_.size();

    result.ipc = total_cycles_ > 0
        ? static_cast<double>(total_instructions_) / static_cast<double>(total_cycles_)
        : 0.0;

    // Function stats
    for (auto& [pc, stats] : function_stats_) {
        if (stats.cycle_count > 0) {
            stats.ipc = static_cast<double>(stats.instruction_count) / static_cast<double>(stats.cycle_count);
        }
        result.function_stats.push_back(std::move(stats));
    }

    // Cache stats
    result.cache_stats.l1_d_misses = total_l1_misses_;
    result.cache_stats.l1_d_hits = total_mem_ops_ >= total_l1_misses_ ? total_mem_ops_ - total_l1_misses_ : 0;
    result.cache_stats.l2_misses = total_l2_misses_;
    result.cache_stats.l2_hits = total_l1_misses_ >= total_l2_misses_ ? total_l1_misses_ - total_l2_misses_ : 0;
    result.cache_stats.l1_miss_rate = total_mem_ops_ > 0
        ? static_cast<double>(total_l1_misses_) / static_cast<double>(total_mem_ops_)
        : 0.0;
    result.cache_stats.l2_miss_rate = total_l1_misses_ > 0
        ? static_cast<double>(total_l2_misses_) / static_cast<double>(total_l1_misses_)
        : 0.0;

    result.bins = std::move(bins_);

    return result;
}

} // namespace arm_cpu
