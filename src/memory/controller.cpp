/// @file controller.cpp
/// @brief Legacy memory controller implementation.

#include "arm_cpu/memory/controller.hpp"

namespace arm_cpu {

MemoryController::MemoryController(uint64_t latency, std::size_t max_outstanding)
    : latency_(latency), max_outstanding_(max_outstanding) {}

bool MemoryController::can_accept() const { return pending_.size() < max_outstanding_; }

std::optional<uint64_t> MemoryController::read(InstructionId id, uint64_t addr, uint8_t size) {
    if (!can_accept()) return std::nullopt;
    auto complete_cycle = current_cycle_ + latency_;
    pending_.push_back({id, addr, size, true, current_cycle_, complete_cycle});
    total_requests_++;
    total_bytes_ += size;
    return complete_cycle;
}

std::optional<uint64_t> MemoryController::write(InstructionId id, uint64_t addr, uint8_t size) {
    if (!can_accept()) return std::nullopt;
    auto complete_cycle = current_cycle_ + latency_;
    pending_.push_back({id, addr, size, false, current_cycle_, complete_cycle});
    total_requests_++;
    total_bytes_ += size;
    return complete_cycle;
}

std::vector<MemoryControllerRequest> MemoryController::poll_completed(uint64_t cycle) {
    std::vector<MemoryControllerRequest> completed;
    while (!pending_.empty() && pending_.front().complete_cycle <= cycle) {
        completed.push_back(pending_.front());
        pending_.pop_front();
    }
    return completed;
}

std::size_t MemoryController::pending_count() const { return pending_.size(); }
void MemoryController::advance_cycle() { current_cycle_++; }
uint64_t MemoryController::current_cycle() const { return current_cycle_; }

MemoryControllerStats MemoryController::get_stats() const {
    return {pending_.size(), total_requests_, total_bytes_, total_requests_ > 0 ? latency_ : 0};
}

void MemoryController::reset_stats() { total_requests_ = 0; total_bytes_ = 0; }
void MemoryController::clear() { pending_.clear(); }

// --- BandwidthTracker ---

BandwidthTracker::BandwidthTracker(std::size_t samples_per_window, std::size_t max_history)
    : samples_per_window_(samples_per_window), max_history_(max_history) {}

void BandwidthTracker::record(uint64_t bytes) { current_bytes_ += bytes; }
void BandwidthTracker::advance_window() {
    history_.push_back(current_bytes_);
    if (history_.size() > max_history_) history_.pop_front();
    current_bytes_ = 0;
}

double BandwidthTracker::average_bandwidth() const {
    if (history_.empty()) return 0.0;
    uint64_t total = 0;
    for (auto v : history_) total += v;
    return static_cast<double>(total) / static_cast<double>(history_.size());
}

uint64_t BandwidthTracker::peak_bandwidth() const {
    uint64_t peak = 0;
    for (auto v : history_) if (v > peak) peak = v;
    return peak;
}

void BandwidthTracker::clear() { current_bytes_ = 0; history_.clear(); }

} // namespace arm_cpu
