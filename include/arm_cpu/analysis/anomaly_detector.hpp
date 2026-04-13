#pragma once

/// @file anomaly_detector.hpp
/// @brief Anomaly detection for performance analysis.
///
/// Identifies IPC drops, cache miss spikes, pipeline bubbles,
/// memory bottlenecks, and other performance issues.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace arm_cpu {

// Forward declaration
struct AggregatedStatistics;

// =====================================================================
// AnomalyType
// =====================================================================
enum class AnomalyType : uint8_t {
    IPCDrop,
    PipelineBubble,
    CacheMissSpike,
    MemoryBottleneck,
    BranchMispredict,
    HighLatency,
};

inline const char* anomaly_type_name(AnomalyType t) {
    switch (t) {
        case AnomalyType::IPCDrop:          return "IPC Drop";
        case AnomalyType::PipelineBubble:   return "Pipeline Bubble";
        case AnomalyType::CacheMissSpike:   return "Cache Miss Spike";
        case AnomalyType::MemoryBottleneck: return "Memory Bottleneck";
        case AnomalyType::BranchMispredict: return "Branch Mispredict";
        case AnomalyType::HighLatency:      return "High Latency";
    }
    return "Unknown";
}

// =====================================================================
// Anomaly
// =====================================================================
struct Anomaly {
    AnomalyType anomaly_type;
    uint64_t start_instr = 0;
    uint64_t end_instr = 0;
    uint64_t start_cycle = 0;
    uint64_t end_cycle = 0;
    double severity = 0.0;
    std::string description;
    std::unordered_map<std::string, double> metadata;

    static Anomaly create(AnomalyType type, uint64_t si, uint64_t ei,
                          uint64_t sc, uint64_t ec, double sev,
                          std::string desc) {
        Anomaly a;
        a.anomaly_type = type;
        a.start_instr = si;
        a.end_instr = ei;
        a.start_cycle = sc;
        a.end_cycle = ec;
        a.severity = sev;
        a.description = std::move(desc);
        return a;
    }

    Anomaly& with_metadata(const std::string& key, double value) {
        metadata[key] = value;
        return *this;
    }
};

// =====================================================================
// AnomalyDetectorConfig
// =====================================================================
struct AnomalyDetectorConfig {
    double ipc_drop_threshold = 0.5;
    double ipc_min_threshold = 0.3;
    double cache_miss_threshold = 0.3;
    uint64_t bubble_threshold = 10;
    double memory_bottleneck_threshold = 0.5;
    uint64_t latency_threshold = 50;
};

// =====================================================================
// AnomalyDetector
// =====================================================================
class AnomalyDetector {
public:
    AnomalyDetector() = default;
    explicit AnomalyDetector(AnomalyDetectorConfig config);

    /// Detect anomalies in aggregated statistics.
    std::vector<Anomaly> detect(const AggregatedStatistics& stats) const;

private:
    std::vector<Anomaly> detect_ipc_drops(const AggregatedStatistics& stats) const;
    std::vector<Anomaly> detect_cache_miss_spikes(const AggregatedStatistics& stats) const;
    std::vector<Anomaly> detect_pipeline_bubbles(const AggregatedStatistics& stats) const;
    std::vector<Anomaly> detect_memory_bottlenecks(const AggregatedStatistics& stats) const;

    AnomalyDetectorConfig config_;
};

} // namespace arm_cpu
