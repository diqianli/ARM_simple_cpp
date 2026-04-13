/// @file ddr_controller.cpp
/// @brief DDR memory controller implementation.

#include "arm_cpu/memory/ddr_controller.hpp"

namespace arm_cpu {

DdrController::DdrController(uint64_t base_latency, uint64_t row_buffer_hit_bonus,
    uint64_t bank_conflict_penalty, std::size_t num_banks)
    : base_latency_(base_latency)
    , row_buffer_hit_bonus_(row_buffer_hit_bonus)
    , bank_conflict_penalty_(bank_conflict_penalty)
    , num_banks_(num_banks)
    , open_rows_(num_banks)
{}

void DdrController::set_cycle(uint64_t cycle) { current_cycle_ = cycle; }
uint64_t DdrController::current_cycle() const { return current_cycle_; }

DdrAccessResult DdrController::access(uint64_t addr) {
    auto bank = static_cast<std::size_t>((addr >> 6) & ((num_banks_ - 1)));
    auto row_addr = addr & ~(ROW_SIZE - 1);

    bool row_hit = open_rows_[bank].has_value() && open_rows_[bank].value() == row_addr;

    auto access_latency = row_hit
        ? (base_latency_ > row_buffer_hit_bonus_ ? base_latency_ - row_buffer_hit_bonus_ : uint64_t{0})
        : base_latency_;

    open_rows_[bank] = row_addr;

    stats_.total_accesses++;
    stats_.total_latency += access_latency;
    if (row_hit) stats_.row_buffer_hits++; else stats_.row_buffer_misses++;

    return {current_cycle_ + access_latency, bank, row_hit, access_latency};
}

const DdrStats& DdrController::stats() const { return stats_; }
void DdrController::reset_stats() { stats_ = DdrStats{}; }
void DdrController::reset_row_buffers() { open_rows_.assign(num_banks_, std::nullopt); }
std::size_t DdrController::num_banks() const { return num_banks_; }
uint64_t DdrController::base_latency() const { return base_latency_; }

double DdrStats::hit_rate() const {
    return total_accesses > 0 ? static_cast<double>(row_buffer_hits) / static_cast<double>(total_accesses) : 0.0;
}
double DdrStats::avg_latency() const {
    return total_accesses > 0 ? static_cast<double>(total_latency) / static_cast<double>(total_accesses) : 0.0;
}

} // namespace arm_cpu
