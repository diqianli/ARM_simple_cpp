#pragma once

/// @file chi_timing.hpp
/// @brief CHI timing model: timing configuration, event scheduling, latency
///        calculation, and bandwidth modeling.

#include "arm_cpu/chi/chi_protocol.hpp"
#include "arm_cpu/chi/chi_interface.hpp"

#include <cstdint>
#include <vector>

namespace arm_cpu {

// =====================================================================
// ChiTimingConfig
// =====================================================================
struct ChiTimingConfig {
    uint64_t request_latency  = 2;
    uint64_t response_latency = 2;
    uint64_t data_latency     = 2;
    uint64_t snoop_latency    = 2;
};

// =====================================================================
// TimingEventType
// =====================================================================
enum class TimingEventType : uint8_t {
    RequestSent,
    ResponseReceived,
    DataReceived,
    SnoopSent,
    SnoopResponseReceived,
};

// =====================================================================
// ChiTimingModel
// =====================================================================
class ChiTimingModel {
public:
    explicit ChiTimingModel(ChiTimingConfig config);

    uint64_t calculate_completion(const ChiTransaction& txn);
    std::vector<std::pair<ChiTxnId, TimingEventType>> process_events();

    void advance_cycle();
    uint64_t current_cycle() const;
    std::size_t pending_event_count() const;
    void clear();

private:
    ChiTimingConfig config_;
    uint64_t current_cycle_ = 0;
    struct TimingEvent {
        ChiTxnId       txn_id;
        TimingEventType event_type;
        uint64_t       cycle;
    };
    std::vector<TimingEvent> events_;
};

// =====================================================================
// ChiLatencyCalculator
// =====================================================================
class ChiLatencyCalculator {
public:
    ChiLatencyCalculator(uint64_t base_memory_latency, uint64_t hop_latency, uint64_t max_hops);

    uint64_t read_latency(uint64_t hops) const;
    uint64_t write_latency(uint64_t hops) const;
    uint64_t snoop_latency(uint64_t hops) const;

    static ChiLatencyCalculator typical() {
        return ChiLatencyCalculator(50, 2, 4);
    }

private:
    uint64_t base_memory_latency_;
    uint64_t hop_latency_;
    uint64_t max_hops_;
};

// =====================================================================
// ChiBandwidthModel
// =====================================================================
class ChiBandwidthModel {
public:
    ChiBandwidthModel(uint64_t max_bandwidth, std::size_t history_length);

    void record(uint64_t bytes);
    void end_cycle();
    bool has_bandwidth(uint64_t bytes) const;

    double average_bandwidth() const;
    double utilization() const;

    void clear();

private:
    uint64_t             max_bandwidth_;
    uint64_t             current_usage_ = 0;
    std::vector<uint64_t> history_;
    std::size_t          history_length_;
};

} // namespace arm_cpu
