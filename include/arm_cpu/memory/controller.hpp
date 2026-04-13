#pragma once

/// @file controller.hpp
/// @brief Legacy memory controller for managing requests.

#include "arm_cpu/types.hpp"

#include <cstdint>
#include <cstddef>
#include <deque>
#include <vector>

namespace arm_cpu {

struct MemoryControllerRequest {
    InstructionId instruction_id;
    uint64_t addr;
    uint8_t size;
    bool is_read;
    uint64_t issue_cycle;
    uint64_t complete_cycle;
};

struct MemoryControllerStats {
    std::size_t pending_requests = 0;
    uint64_t total_requests = 0;
    uint64_t total_bytes = 0;
    uint64_t average_latency = 0;
};

class MemoryController {
public:
    MemoryController(uint64_t latency, std::size_t max_outstanding);

    bool can_accept() const;
    std::optional<uint64_t> read(InstructionId id, uint64_t addr, uint8_t size);
    std::optional<uint64_t> write(InstructionId id, uint64_t addr, uint8_t size);
    std::vector<MemoryControllerRequest> poll_completed(uint64_t cycle);
    std::size_t pending_count() const;
    void advance_cycle();
    uint64_t current_cycle() const;
    MemoryControllerStats get_stats() const;
    void reset_stats();
    void clear();

private:
    uint64_t latency_;
    std::size_t max_outstanding_;
    std::deque<MemoryControllerRequest> pending_;
    uint64_t current_cycle_ = 0;
    uint64_t total_requests_ = 0;
    uint64_t total_bytes_ = 0;
};

class BandwidthTracker {
public:
    BandwidthTracker(std::size_t samples_per_window, std::size_t max_history);

    void record(uint64_t bytes);
    void advance_window();
    double average_bandwidth() const;
    uint64_t peak_bandwidth() const;
    void clear();

private:
    std::size_t samples_per_window_;
    uint64_t current_bytes_ = 0;
    std::deque<uint64_t> history_;
    std::size_t max_history_;
};

} // namespace arm_cpu
