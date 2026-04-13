#pragma once

/// @file ddr_controller.hpp
/// @brief DDR memory controller with row buffer tracking.

#include <cstdint>
#include <cstddef>
#include <vector>
#include <optional>

namespace arm_cpu {

struct DdrAccessResult {
    uint64_t complete_cycle;
    std::size_t bank;
    bool row_hit;
    uint64_t latency;
};

struct DdrStats {
    uint64_t total_accesses = 0;
    uint64_t row_buffer_hits = 0;
    uint64_t row_buffer_misses = 0;
    uint64_t total_latency = 0;

    double hit_rate() const;
    double avg_latency() const;
};

class DdrController {
public:
    DdrController(uint64_t base_latency, uint64_t row_buffer_hit_bonus,
                  uint64_t bank_conflict_penalty, std::size_t num_banks);

    void set_cycle(uint64_t cycle);
    uint64_t current_cycle() const;
    DdrAccessResult access(uint64_t addr);
    const DdrStats& stats() const;
    void reset_stats();
    void reset_row_buffers();
    std::size_t num_banks() const;
    uint64_t base_latency() const;

private:
    uint64_t base_latency_;
    uint64_t row_buffer_hit_bonus_;
    uint64_t bank_conflict_penalty_;
    std::size_t num_banks_;
    std::vector<std::optional<uint64_t>> open_rows_;
    uint64_t current_cycle_ = 0;
    DdrStats stats_;
    static constexpr uint64_t ROW_SIZE = 8 * 1024; // 8KB rows
};

} // namespace arm_cpu
