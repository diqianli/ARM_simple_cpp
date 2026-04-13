#pragma once

/// @file cache.hpp
/// @brief Cache implementation with LRU replacement.

#include "arm_cpu/types.hpp"

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace arm_cpu {

enum class CacheLevel : uint8_t { L1, L2, L3, Memory };

inline const char* cache_level_name(CacheLevel level) {
    switch (level) {
        case CacheLevel::L1: return "L1";
        case CacheLevel::L2: return "L2";
        case CacheLevel::L3: return "L3";
        case CacheLevel::Memory: return "MEM";
    }
    return "Unknown";
}

struct CacheLevelTiming {
    CacheLevel level;
    uint64_t start_cycle;
    uint64_t end_cycle;
    uint64_t duration() const { return end_cycle - start_cycle; }
};

struct CacheAccessInfo {
    CacheLevel servicing_level;
    uint64_t total_latency;
    std::vector<CacheLevelTiming> level_timing;
    bool ddr_row_hit = false;
    std::optional<std::size_t> ddr_bank;

    static CacheAccessInfo l1_hit(uint64_t start_cycle, uint64_t latency);
    static CacheAccessInfo l2_hit(uint64_t start_cycle, uint64_t l1_latency, uint64_t l2_latency);
    static CacheAccessInfo l3_hit(uint64_t start_cycle, uint64_t l1_latency, uint64_t l2_latency, uint64_t l3_latency);
    static CacheAccessInfo memory_access(uint64_t start_cycle, uint64_t l1_latency, uint64_t l2_latency,
        uint64_t l3_latency, uint64_t ddr_latency, bool ddr_row_hit, std::size_t ddr_bank);

    bool is_l1_hit() const { return servicing_level == CacheLevel::L1; }
    bool is_l2_hit() const { return servicing_level == CacheLevel::L2; }
    bool is_l3_hit() const { return servicing_level == CacheLevel::L3; }
    bool is_memory_access() const { return servicing_level == CacheLevel::Memory; }
};

struct CacheConfig {
    std::size_t size;
    std::size_t associativity;
    std::size_t line_size;
    uint64_t hit_latency;
    std::string name;

    std::size_t num_sets() const { return size / (associativity * line_size); }
    std::size_t get_set(uint64_t addr) const;
    uint64_t get_tag(uint64_t addr) const;
};

struct CacheStats {
    std::string name;
    uint64_t accesses = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t reads = 0;
    uint64_t writes = 0;
    uint64_t read_misses = 0;
    uint64_t write_misses = 0;
    uint64_t evictions = 0;

    double hit_rate() const;
    double miss_rate() const;
    double mpki(uint64_t instructions) const;
    double avg_latency(uint64_t hit_latency, uint64_t miss_latency) const;
};

struct CacheLine {
    uint64_t tag = 0;
    CacheLineState state = CacheLineState::Invalid;
    bool valid = false;
    uint32_t lru = 0;
    bool dirty = false;
};

class CacheSet {
public:
    explicit CacheSet(std::size_t associativity);

    std::optional<std::size_t> find(uint64_t tag) const;
    std::optional<std::size_t> find_victim() const;
    void update_lru(std::size_t accessed_way);
    CacheLine& get_way(std::size_t way);
    const CacheLine& get_way(std::size_t way) const;

private:
    std::vector<CacheLine> ways_;
    std::size_t associativity_;
};

class Cache {
public:
    static Result<std::unique_ptr<Cache>> create(CacheConfig config);

    Result<bool> access(uint64_t addr, bool is_read);
    std::optional<uint64_t> fill_line(uint64_t addr);
    bool invalidate(uint64_t addr);

    uint64_t hit_latency() const;
    const CacheStats& stats() const;
    void reset_stats();
    const CacheConfig& config() const;
    void flush();

private:
    explicit Cache(CacheConfig config);

    CacheConfig config_;
    std::vector<CacheSet> sets_;
    CacheStats stats_;
};

} // namespace arm_cpu
