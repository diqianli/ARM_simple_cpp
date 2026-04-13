#pragma once

/// @file region_selector.hpp
/// @brief Region selection for focused visualization.

#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

namespace arm_cpu {

// Forward declarations
struct AggregatedStatistics;
struct Anomaly;

// =====================================================================
// RegionType
// =====================================================================
enum class RegionType : uint8_t {
    Anomaly,
    Hotspot,
    UserSelected,
    AutoDetected,
    Benchmark,
};

// =====================================================================
// RegionOfInterest
// =====================================================================
struct RegionOfInterest {
    uint64_t id = 0;
    std::string name;
    uint64_t start_instr = 0;
    uint64_t end_instr = 0;
    uint64_t start_cycle = 0;
    uint64_t end_cycle = 0;
    RegionType region_type = RegionType::AutoDetected;
    double importance = 0.0;
};

// =====================================================================
// RegionSelector
// =====================================================================
class RegionSelector {
public:
    RegionSelector() = default;

    RegionSelector& with_max_regions(std::size_t max) { max_regions_ = max; return *this; }
    RegionSelector& with_min_region_size(uint64_t min) { min_region_size_ = min; return *this; }

    /// Add a region of interest.
    void add_region(RegionOfInterest region);

    /// Add an anomaly region.
    void add_anomaly_region(uint64_t si, uint64_t ei, uint64_t sc, uint64_t ec,
                            const std::string& desc, double severity);

    /// Add a hotspot region.
    void add_hotspot_region(uint64_t si, uint64_t ei, uint64_t sc, uint64_t ec,
                            const std::string& name, double score);

    /// Add a user-selected region.
    void add_user_region(uint64_t si, uint64_t ei, uint64_t sc, uint64_t ec,
                         const std::string& name);

    /// Get all regions.
    const std::vector<RegionOfInterest>& get_regions() const { return regions_; }

    /// Get regions overlapping with a given range.
    std::vector<const RegionOfInterest*> get_regions_in_range(uint64_t start, uint64_t end) const;

    /// Get region containing a specific instruction.
    const RegionOfInterest* get_region_at(uint64_t instr) const;

    /// Get regions by type.
    std::vector<const RegionOfInterest*> get_regions_by_type(RegionType type) const;

    /// Auto-detect regions from statistics.
    void auto_detect_regions(const AggregatedStatistics& stats, const std::vector<Anomaly>& anomalies);

    /// Clear all regions.
    void clear() { regions_.clear(); }

private:
    void detect_low_ipc_regions(const AggregatedStatistics& stats);
    void detect_cache_bound_regions(const AggregatedStatistics& stats);

    std::vector<RegionOfInterest> regions_;
    std::size_t max_regions_ = 100;
    uint64_t min_region_size_ = 100;
};

} // namespace arm_cpu
