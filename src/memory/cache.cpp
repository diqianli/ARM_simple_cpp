/// @file cache.cpp
/// @brief Cache implementation.

#include "arm_cpu/memory/cache.hpp"
#include <cmath>
#include <algorithm>

namespace arm_cpu {

// --- CacheAccessInfo ---

CacheAccessInfo CacheAccessInfo::l1_hit(uint64_t start_cycle, uint64_t latency) {
    CacheAccessInfo info;
    info.servicing_level = CacheLevel::L1;
    info.total_latency = latency;
    info.level_timing.push_back({CacheLevel::L1, start_cycle, start_cycle + latency});
    return info;
}

CacheAccessInfo CacheAccessInfo::l2_hit(uint64_t start_cycle, uint64_t l1_latency, uint64_t l2_latency) {
    CacheAccessInfo info;
    auto l1_end = start_cycle + l1_latency;
    auto l2_end = l1_end + l2_latency;
    info.servicing_level = CacheLevel::L2;
    info.total_latency = l1_latency + l2_latency;
    info.level_timing = {{CacheLevel::L1, start_cycle, l1_end}, {CacheLevel::L2, l1_end, l2_end}};
    return info;
}

CacheAccessInfo CacheAccessInfo::l3_hit(uint64_t start_cycle, uint64_t l1_latency, uint64_t l2_latency, uint64_t l3_latency) {
    CacheAccessInfo info;
    auto l1_end = start_cycle + l1_latency;
    auto l2_end = l1_end + l2_latency;
    auto l3_end = l2_end + l3_latency;
    info.servicing_level = CacheLevel::L3;
    info.total_latency = l1_latency + l2_latency + l3_latency;
    info.level_timing = {{CacheLevel::L1, start_cycle, l1_end}, {CacheLevel::L2, l1_end, l2_end}, {CacheLevel::L3, l2_end, l3_end}};
    return info;
}

CacheAccessInfo CacheAccessInfo::memory_access(uint64_t start_cycle, uint64_t l1_latency, uint64_t l2_latency,
    uint64_t l3_latency, uint64_t ddr_latency, bool ddr_row_hit, std::size_t ddr_bank) {
    CacheAccessInfo info;
    auto l1_end = start_cycle + l1_latency;
    auto l2_end = l1_end + l2_latency;
    auto l3_end = l2_end + l3_latency;
    auto mem_end = l3_end + ddr_latency;
    info.servicing_level = CacheLevel::Memory;
    info.total_latency = l1_latency + l2_latency + l3_latency + ddr_latency;
    info.level_timing = {{CacheLevel::L1, start_cycle, l1_end}, {CacheLevel::L2, l1_end, l2_end},
        {CacheLevel::L3, l2_end, l3_end}, {CacheLevel::Memory, l3_end, mem_end}};
    info.ddr_row_hit = ddr_row_hit;
    info.ddr_bank = ddr_bank;
    return info;
}

// --- CacheConfig ---

std::size_t CacheConfig::get_set(uint64_t addr) const {
    auto line_bits = static_cast<uint32_t>(std::log2(line_size));
    auto set_mask = static_cast<uint64_t>(num_sets() - 1);
    return static_cast<std::size_t>((addr >> line_bits) & set_mask);
}

uint64_t CacheConfig::get_tag(uint64_t addr) const {
    auto set_bits = static_cast<uint32_t>(std::log2(num_sets()));
    auto line_bits = static_cast<uint32_t>(std::log2(line_size));
    return addr >> (set_bits + line_bits);
}

// --- CacheStats ---

double CacheStats::hit_rate() const {
    return accesses > 0 ? static_cast<double>(hits) / static_cast<double>(accesses) : 0.0;
}
double CacheStats::miss_rate() const {
    return accesses > 0 ? static_cast<double>(misses) / static_cast<double>(accesses) : 0.0;
}
double CacheStats::mpki(uint64_t instructions) const {
    return instructions > 0 ? (static_cast<double>(misses) / static_cast<double>(instructions)) * 1000.0 : 0.0;
}
double CacheStats::avg_latency(uint64_t hit_latency, uint64_t miss_latency) const {
    if (accesses == 0) return 0.0;
    auto hit_time = static_cast<double>(hits) * hit_latency;
    auto miss_time = static_cast<double>(misses) * miss_latency;
    return (hit_time + miss_time) / static_cast<double>(accesses);
}

// --- CacheSet ---

CacheSet::CacheSet(std::size_t associativity) : associativity_(associativity) {
    ways_.resize(associativity);
}

std::optional<std::size_t> CacheSet::find(uint64_t tag) const {
    for (std::size_t i = 0; i < ways_.size(); i++) {
        if (ways_[i].valid && ways_[i].tag == tag) return i;
    }
    return std::nullopt;
}

std::optional<std::size_t> CacheSet::find_victim() const {
    // Prefer invalid way
    for (std::size_t i = 0; i < ways_.size(); i++) {
        if (!ways_[i].valid) return i;
    }
    // Fall back to LRU
    std::size_t min_idx = 0;
    uint32_t min_lru = ways_[0].lru;
    for (std::size_t i = 1; i < ways_.size(); i++) {
        if (ways_[i].lru < min_lru) { min_lru = ways_[i].lru; min_idx = i; }
    }
    return min_idx;
}

void CacheSet::update_lru(std::size_t accessed_way) {
    for (std::size_t i = 0; i < ways_.size(); i++) {
        if (i == accessed_way) ways_[i].lru = static_cast<uint32_t>(associativity_);
        else if (ways_[i].lru > 0) ways_[i].lru--;
    }
}

CacheLine& CacheSet::get_way(std::size_t way) { return ways_[way]; }
const CacheLine& CacheSet::get_way(std::size_t way) const { return ways_[way]; }

// --- Cache ---

Cache::Cache(CacheConfig config)
    : config_(std::move(config)), stats_{config_.name} {}

Result<std::unique_ptr<Cache>> Cache::create(CacheConfig config) {
    auto num_sets = config.num_sets();
    if (num_sets == 0) return EmulatorError::config("Invalid cache configuration: zero sets");
    if ((num_sets & (num_sets - 1)) != 0) return EmulatorError::config("Number of sets must be a power of 2");

    auto cache = std::unique_ptr<Cache>(new Cache(std::move(config)));
    cache->sets_.reserve(num_sets);
    for (std::size_t i = 0; i < num_sets; i++) {
        cache->sets_.emplace_back(cache->config_.associativity);
    }
    return cache;
}

Result<bool> Cache::access(uint64_t addr, bool is_read) {
    auto set_idx = config_.get_set(addr);
    auto tag = config_.get_tag(addr);
    stats_.accesses++;
    if (is_read) stats_.reads++; else stats_.writes++;

    auto hit_way = sets_[set_idx].find(tag);
    if (hit_way.has_value()) {
        sets_[set_idx].update_lru(*hit_way);
        auto& line = sets_[set_idx].get_way(*hit_way);
        line.lru = static_cast<uint32_t>(config_.associativity);
        if (!is_read) line.dirty = true;
        stats_.hits++;
        return true;
    }

    stats_.misses++;
    if (is_read) stats_.read_misses++; else stats_.write_misses++;
    return false;
}

std::optional<uint64_t> Cache::fill_line(uint64_t addr) {
    auto set_idx = config_.get_set(addr);
    auto tag = config_.get_tag(addr);
    auto num_sets = sets_.size();
    auto set_bits = static_cast<uint32_t>(std::log2(num_sets));
    auto line_bits = static_cast<uint32_t>(std::log2(config_.line_size));

    auto& set = sets_[set_idx];
    if (set.find(tag).has_value()) {
        set.update_lru(*set.find(tag));
        return std::nullopt;
    }

    auto victim_way = set.find_victim();
    auto& victim = set.get_way(*victim_way);

    std::optional<uint64_t> evicted_addr;
    if (victim.valid && victim.dirty) {
        evicted_addr = (victim.tag << (set_bits + line_bits)) | (static_cast<uint64_t>(set_idx) << line_bits);
        stats_.evictions++;
    }

    victim.tag = tag;
    victim.valid = true;
    victim.dirty = false;
    victim.state = CacheLineState::Exclusive;
    set.update_lru(*victim_way);

    return evicted_addr;
}

bool Cache::invalidate(uint64_t addr) {
    auto set_idx = config_.get_set(addr);
    auto tag = config_.get_tag(addr);
    auto way = sets_[set_idx].find(tag);
    if (way.has_value()) {
        auto& line = sets_[set_idx].get_way(*way);
        line.valid = false;
        line.state = CacheLineState::Invalid;
        return true;
    }
    return false;
}

uint64_t Cache::hit_latency() const { return config_.hit_latency; }
const CacheStats& Cache::stats() const { return stats_; }
void Cache::reset_stats() { stats_ = CacheStats{config_.name}; }
const CacheConfig& Cache::config() const { return config_; }
void Cache::flush() {
    for (auto& set : sets_) {
        for (std::size_t i = 0; i < config_.associativity; i++) {
            auto& line = set.get_way(i);
            line.valid = false; line.dirty = false; line.state = CacheLineState::Invalid;
        }
    }
}

} // namespace arm_cpu
