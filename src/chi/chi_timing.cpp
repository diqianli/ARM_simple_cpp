/// @file chi_timing.cpp
/// @brief ChiTimingModel, ChiLatencyCalculator, ChiBandwidthModel implementation.

#include "arm_cpu/chi/chi_timing.hpp"

#include <algorithm>
#include <numeric>

namespace arm_cpu {

// =====================================================================
// ChiTimingModel
// =====================================================================

ChiTimingModel::ChiTimingModel(ChiTimingConfig config)
    : config_(std::move(config))
{}

uint64_t ChiTimingModel::calculate_completion(const ChiTransaction& txn) {
    uint64_t request_sent = current_cycle_ + config_.request_latency;
    uint64_t response_received = request_sent + config_.response_latency;

    uint64_t complete_cycle;
    if (chi_requires_data(txn.request_type)) {
        complete_cycle = response_received + config_.data_latency;
    } else {
        complete_cycle = response_received;
    }

    events_.push_back({txn.txn_id, TimingEventType::RequestSent, request_sent});
    events_.push_back({txn.txn_id, TimingEventType::ResponseReceived, response_received});

    if (chi_requires_data(txn.request_type)) {
        events_.push_back({txn.txn_id, TimingEventType::DataReceived, complete_cycle});
    }

    return complete_cycle;
}

std::vector<std::pair<ChiTxnId, TimingEventType>> ChiTimingModel::process_events() {
    std::vector<std::pair<ChiTxnId, TimingEventType>> ready;
    std::vector<TimingEvent> pending;

    for (auto& e : events_) {
        if (e.cycle <= current_cycle_) {
            ready.emplace_back(e.txn_id, e.event_type);
        } else {
            pending.push_back(std::move(e));
        }
    }

    events_ = std::move(pending);
    return ready;
}

void ChiTimingModel::advance_cycle() { ++current_cycle_; }
uint64_t ChiTimingModel::current_cycle() const { return current_cycle_; }
std::size_t ChiTimingModel::pending_event_count() const { return events_.size(); }
void ChiTimingModel::clear() { events_.clear(); }

// =====================================================================
// ChiLatencyCalculator
// =====================================================================

ChiLatencyCalculator::ChiLatencyCalculator(
    uint64_t base_memory_latency, uint64_t hop_latency, uint64_t max_hops)
    : base_memory_latency_(base_memory_latency)
    , hop_latency_(hop_latency)
    , max_hops_(max_hops)
{}

uint64_t ChiLatencyCalculator::read_latency(uint64_t hops) const {
    hops = std::min(hops, max_hops_);
    return base_memory_latency_ + hops * hop_latency_;
}

uint64_t ChiLatencyCalculator::write_latency(uint64_t hops) const {
    hops = std::min(hops, max_hops_);
    return base_memory_latency_ + hops * hop_latency_;
}

uint64_t ChiLatencyCalculator::snoop_latency(uint64_t hops) const {
    hops = std::min(hops, max_hops_);
    return hops * hop_latency_;
}

// =====================================================================
// ChiBandwidthModel
// =====================================================================

ChiBandwidthModel::ChiBandwidthModel(uint64_t max_bandwidth, std::size_t history_length)
    : max_bandwidth_(max_bandwidth)
    , history_length_(history_length)
{
    history_.reserve(history_length);
}

void ChiBandwidthModel::record(uint64_t bytes) {
    current_usage_ += bytes;
}

void ChiBandwidthModel::end_cycle() {
    history_.push_back(current_usage_);
    if (history_.size() > history_length_) {
        history_.erase(history_.begin());
    }
    current_usage_ = 0;
}

bool ChiBandwidthModel::has_bandwidth(uint64_t bytes) const {
    return current_usage_ + bytes <= max_bandwidth_;
}

double ChiBandwidthModel::average_bandwidth() const {
    if (history_.empty()) return 0.0;
    uint64_t total = std::accumulate(history_.begin(), history_.end(), uint64_t{0});
    return static_cast<double>(total) / static_cast<double>(history_.size());
}

double ChiBandwidthModel::utilization() const {
    if (max_bandwidth_ == 0) return 0.0;
    return average_bandwidth() / static_cast<double>(max_bandwidth_) * 100.0;
}

void ChiBandwidthModel::clear() {
    current_usage_ = 0;
    history_.clear();
}

} // namespace arm_cpu
