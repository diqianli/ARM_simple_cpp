/// @file region_selector.cpp
/// @brief Region selection implementation.

#include "arm_cpu/analysis/region_selector.hpp"
#include "arm_cpu/analysis/aggregator.hpp"
#include "arm_cpu/analysis/anomaly_detector.hpp"

#include <algorithm>
#include <format>

namespace arm_cpu {

void RegionSelector::add_region(RegionOfInterest region) {
    // Check minimum size
    if (region.end_instr < region.start_instr + min_region_size_) return;

    // Remove overlapping regions with lower importance
    for (std::size_t i = regions_.size(); i-- > 0;) {
        auto& r = regions_[i];
        if (r.start_instr <= region.end_instr && r.end_instr >= region.start_instr) {
            if (r.importance < region.importance) {
                regions_.erase(regions_.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
    }

    regions_.push_back(std::move(region));

    // Sort by importance descending, trim to max
    std::sort(regions_.begin(), regions_.end(),
              [](const RegionOfInterest& a, const RegionOfInterest& b) {
                  return a.importance > b.importance;
              });
    if (regions_.size() > max_regions_) {
        regions_.resize(max_regions_);
    }
}

void RegionSelector::add_anomaly_region(uint64_t si, uint64_t ei, uint64_t sc, uint64_t ec,
                                         const std::string& desc, double severity) {
    RegionOfInterest region;
    region.id = static_cast<uint64_t>(regions_.size());
    region.name = desc;
    region.start_instr = si;
    region.end_instr = ei;
    region.start_cycle = sc;
    region.end_cycle = ec;
    region.region_type = RegionType::Anomaly;
    region.importance = severity;
    add_region(std::move(region));
}

void RegionSelector::add_hotspot_region(uint64_t si, uint64_t ei, uint64_t sc, uint64_t ec,
                                         const std::string& name, double score) {
    RegionOfInterest region;
    region.id = static_cast<uint64_t>(regions_.size());
    region.name = std::format("Hotspot: {}", name);
    region.start_instr = si;
    region.end_instr = ei;
    region.start_cycle = sc;
    region.end_cycle = ec;
    region.region_type = RegionType::Hotspot;
    region.importance = std::min(score, 1.0);
    add_region(std::move(region));
}

void RegionSelector::add_user_region(uint64_t si, uint64_t ei, uint64_t sc, uint64_t ec,
                                      const std::string& name) {
    RegionOfInterest region;
    region.id = static_cast<uint64_t>(regions_.size());
    region.name = name;
    region.start_instr = si;
    region.end_instr = ei;
    region.start_cycle = sc;
    region.end_cycle = ec;
    region.region_type = RegionType::UserSelected;
    region.importance = 1.0;
    add_region(std::move(region));
}

std::vector<const RegionOfInterest*> RegionSelector::get_regions_in_range(uint64_t start, uint64_t end) const {
    std::vector<const RegionOfInterest*> result;
    for (const auto& r : regions_) {
        if (r.start_instr <= end && r.end_instr >= start) {
            result.push_back(&r);
        }
    }
    return result;
}

const RegionOfInterest* RegionSelector::get_region_at(uint64_t instr) const {
    for (const auto& r : regions_) {
        if (r.start_instr <= instr && r.end_instr >= instr) {
            return &r;
        }
    }
    return nullptr;
}

std::vector<const RegionOfInterest*> RegionSelector::get_regions_by_type(RegionType type) const {
    std::vector<const RegionOfInterest*> result;
    for (const auto& r : regions_) {
        if (r.region_type == type) {
            result.push_back(&r);
        }
    }
    return result;
}

void RegionSelector::auto_detect_regions(const AggregatedStatistics& stats,
                                          const std::vector<Anomaly>& anomalies) {
    for (const auto& anomaly : anomalies) {
        add_anomaly_region(
            anomaly.start_instr, anomaly.end_instr,
            anomaly.start_cycle, anomaly.end_cycle,
            anomaly.description, anomaly.severity
        );
    }

    detect_low_ipc_regions(stats);
    detect_cache_bound_regions(stats);
}

void RegionSelector::detect_low_ipc_regions(const AggregatedStatistics& stats) {
    double threshold = stats.ipc * 0.5;
    std::optional<std::size_t> region_start;

    for (std::size_t i = 0; i < stats.ipc_timeline.size(); ++i) {
        const auto& point = stats.ipc_timeline[i];

        if (point.value < threshold) {
            if (!region_start.has_value()) {
                region_start = i;
            }
        } else if (region_start.has_value()) {
            std::size_t start_idx = *region_start;
            const auto& start_point = stats.ipc_timeline[start_idx];
            const auto& end_point = stats.ipc_timeline[i > 0 ? i - 1 : 0];

            double importance = 1.0 - (point.value / stats.ipc);

            RegionOfInterest region;
            region.id = static_cast<uint64_t>(regions_.size());
            region.name = std::format("Low IPC region ({:.2})", stats.ipc_timeline[start_idx].value);
            region.start_instr = start_point.instr;
            region.end_instr = end_point.instr;
            region.start_cycle = start_point.cycle;
            region.end_cycle = end_point.cycle;
            region.region_type = RegionType::AutoDetected;
            region.importance = importance;
            add_region(std::move(region));

            region_start = std::nullopt;
        }
    }
}

void RegionSelector::detect_cache_bound_regions(const AggregatedStatistics& stats) {
    double threshold = 0.2;
    std::optional<std::size_t> region_start;

    for (std::size_t i = 0; i < stats.l1_miss_timeline.size(); ++i) {
        const auto& point = stats.l1_miss_timeline[i];

        if (point.value > threshold) {
            if (!region_start.has_value()) {
                region_start = i;
            }
        } else if (region_start.has_value()) {
            std::size_t start_idx = *region_start;
            const auto& start_point = stats.l1_miss_timeline[start_idx];
            const auto& end_point = stats.l1_miss_timeline[i > 0 ? i - 1 : 0];

            double importance = stats.l1_miss_timeline[start_idx].value;

            RegionOfInterest region;
            region.id = static_cast<uint64_t>(regions_.size());
            region.name = std::format("Cache bound region ({:.0}% miss)",
                                         stats.l1_miss_timeline[start_idx].value * 100.0);
            region.start_instr = start_point.instr;
            region.end_instr = end_point.instr;
            region.start_cycle = start_point.cycle;
            region.end_cycle = end_point.cycle;
            region.region_type = RegionType::AutoDetected;
            region.importance = importance;
            add_region(std::move(region));

            region_start = std::nullopt;
        }
    }
}

} // namespace arm_cpu
