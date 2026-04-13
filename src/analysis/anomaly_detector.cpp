/// @file anomaly_detector.cpp
/// @brief Anomaly detection implementation.

#include "arm_cpu/analysis/anomaly_detector.hpp"
#include "arm_cpu/analysis/aggregator.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace arm_cpu {

AnomalyDetector::AnomalyDetector(AnomalyDetectorConfig config)
    : config_(std::move(config))
{}

std::vector<Anomaly> AnomalyDetector::detect(const AggregatedStatistics& stats) const {
    std::vector<Anomaly> anomalies;

    auto ipc_drops = detect_ipc_drops(stats);
    auto cache_spikes = detect_cache_miss_spikes(stats);
    auto bubbles = detect_pipeline_bubbles(stats);
    auto mem_bottlenecks = detect_memory_bottlenecks(stats);

    anomalies.reserve(ipc_drops.size() + cache_spikes.size() + bubbles.size() + mem_bottlenecks.size());
    anomalies.insert(anomalies.end(), ipc_drops.begin(), ipc_drops.end());
    anomalies.insert(anomalies.end(), cache_spikes.begin(), cache_spikes.end());
    anomalies.insert(anomalies.end(), bubbles.begin(), bubbles.end());
    anomalies.insert(anomalies.end(), mem_bottlenecks.begin(), mem_bottlenecks.end());

    // Sort by severity descending
    std::sort(anomalies.begin(), anomalies.end(),
              [](const Anomaly& a, const Anomaly& b) { return a.severity > b.severity; });

    return anomalies;
}

std::vector<Anomaly> AnomalyDetector::detect_ipc_drops(const AggregatedStatistics& stats) const {
    std::vector<Anomaly> anomalies;
    const auto& tl = stats.ipc_timeline;

    if (tl.size() < 3) return anomalies;

    uint64_t last_drop_end = 0;

    for (std::size_t i = 0; i + 2 < tl.size(); ++i) {
        double prev = tl[i].value;
        double curr = tl[i + 1].value;

        if (tl[i + 1].instr <= last_drop_end) continue;
        if (prev <= 0.0) continue;

        double drop_ratio = curr / prev;

        if (drop_ratio < config_.ipc_drop_threshold || curr < config_.ipc_min_threshold) {
            double severity = curr < config_.ipc_min_threshold ? 1.0 : 1.0 - drop_ratio;

            // Find end of drop
            std::size_t end_idx = tl.size() - 1;
            for (std::size_t j = i + 2; j < tl.size(); ++j) {
                if (tl[j].instr > tl[i + 1].instr && tl[j].value > curr * 1.5) {
                    end_idx = j > 0 ? j - 1 : 0;
                    break;
                }
            }

            last_drop_end = tl[end_idx].instr;

            anomalies.push_back(
                Anomaly::create(
                    AnomalyType::IPCDrop,
                    tl[i + 1].instr, tl[end_idx].instr,
                    tl[i + 1].cycle, tl[end_idx].cycle,
                    severity,
                    std::format("IPC dropped from {:.2} to {:.2} ({:.0}% decrease)",
                               prev, curr, (1.0 - drop_ratio) * 100.0)
                )
                .with_metadata("prev_ipc", prev)
                .with_metadata("curr_ipc", curr)
                .with_metadata("drop_ratio", drop_ratio)
            );
        }
    }

    return anomalies;
}

std::vector<Anomaly> AnomalyDetector::detect_cache_miss_spikes(const AggregatedStatistics& stats) const {
    std::vector<Anomaly> anomalies;

    // Check L1 miss timeline
    const auto& l1_tl = stats.l1_miss_timeline;
    for (std::size_t i = 0; i + 1 < l1_tl.size(); ++i) {
        double prev = l1_tl[i].value;
        double curr = l1_tl[i + 1].value;

        if (curr > config_.cache_miss_threshold && curr > prev * 2.0) {
            double severity = std::min(curr - prev, 1.0);
            anomalies.push_back(
                Anomaly::create(
                    AnomalyType::CacheMissSpike,
                    l1_tl[i + 1].instr, l1_tl[i + 1].instr + stats.bin_size,
                    l1_tl[i + 1].cycle, l1_tl[i + 1].cycle + stats.bin_size,
                    severity,
                    std::format("L1 cache miss rate spiked from {:.1}% to {:.1}%",
                               prev * 100.0, curr * 100.0)
                )
                .with_metadata("prev_rate", prev)
                .with_metadata("curr_rate", curr)
            );
        }
    }

    // Check L2 miss timeline
    const auto& l2_tl = stats.l2_miss_timeline;
    for (std::size_t i = 0; i + 1 < l2_tl.size(); ++i) {
        double prev = l2_tl[i].value;
        double curr = l2_tl[i + 1].value;

        if (curr > config_.cache_miss_threshold * 0.5 && curr > prev * 2.0) {
            double severity = std::min(curr - prev, 1.0);
            anomalies.push_back(
                Anomaly::create(
                    AnomalyType::CacheMissSpike,
                    l2_tl[i + 1].instr, l2_tl[i + 1].instr + stats.bin_size,
                    l2_tl[i + 1].cycle, l2_tl[i + 1].cycle + stats.bin_size,
                    severity,
                    std::format("L2 cache miss rate spiked from {:.1}% to {:.1}%",
                               prev * 100.0, curr * 100.0)
                )
                .with_metadata("prev_rate", prev)
                .with_metadata("curr_rate", curr)
            );
        }
    }

    return anomalies;
}

std::vector<Anomaly> AnomalyDetector::detect_pipeline_bubbles(const AggregatedStatistics& stats) const {
    std::vector<Anomaly> anomalies;

    for (const auto& bin : stats.bins) {
        if (bin.bubbles >= config_.bubble_threshold) {
            double severity = std::min(
                static_cast<double>(bin.bubbles) / static_cast<double>(config_.bubble_threshold) / 5.0,
                1.0
            );
            anomalies.push_back(
                Anomaly::create(
                    AnomalyType::PipelineBubble,
                    bin.start_instr, bin.end_instr,
                    bin.start_cycle, bin.end_cycle,
                    severity,
                    std::format("Pipeline bubble: {} consecutive cycles with no progress", bin.bubbles)
                )
                .with_metadata("bubble_count", static_cast<double>(bin.bubbles))
            );
        }
    }

    return anomalies;
}

std::vector<Anomaly> AnomalyDetector::detect_memory_bottlenecks(const AggregatedStatistics& stats) const {
    std::vector<Anomaly> anomalies;

    for (const auto& bin : stats.bins) {
        if (bin.instr_count == 0) continue;

        double mem_ratio = static_cast<double>(bin.mem_ops) / static_cast<double>(bin.instr_count);

        if (mem_ratio > config_.memory_bottleneck_threshold) {
            double severity = std::min(mem_ratio, 1.0);
            anomalies.push_back(
                Anomaly::create(
                    AnomalyType::MemoryBottleneck,
                    bin.start_instr, bin.end_instr,
                    bin.start_cycle, bin.end_cycle,
                    severity,
                    std::format("Memory bottleneck: {:.0}% of instructions are memory operations",
                               mem_ratio * 100.0)
                )
                .with_metadata("mem_ratio", mem_ratio)
                .with_metadata("mem_ops", static_cast<double>(bin.mem_ops))
            );
        }
    }

    return anomalies;
}

} // namespace arm_cpu
