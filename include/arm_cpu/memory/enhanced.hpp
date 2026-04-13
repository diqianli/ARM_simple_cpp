#pragma once

/// @file enhanced.hpp
/// @brief Enhanced cache features: MSHR, Prefetcher (stub for future expansion).

#include "arm_cpu/memory/cache.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

namespace arm_cpu {

// --- MSHR ---
enum class MissType : uint8_t { Read, Write };

struct MshrEntry {
    uint64_t addr;
    InstructionId instruction_id;
    MissType miss_type;
    uint64_t issue_cycle;
    bool pending = true;
};

struct MshrStats {
    uint64_t total_misses = 0;
    uint64_t read_misses = 0;
    uint64_t write_misses = 0;
    uint64_t mshr_hits = 0; // MSHR merge hits
    uint64_t total_latency = 0;
};

class Mshr {
public:
    explicit Mshr(std::size_t capacity);

    bool has_space() const;
    std::optional<std::size_t> allocate(const MshrEntry& entry);
    bool is_pending(uint64_t addr) const;
    void mark_complete(std::size_t index);
    const MshrEntry* get(std::size_t index) const;
    std::size_t size() const;
    MshrStats stats() const;
    void clear();

private:
    std::size_t capacity_;
    std::vector<MshrEntry> entries_;
    MshrStats stats_;
};

// --- Prefetcher ---
struct PrefetchRequest {
    uint64_t addr;
    std::size_t cache_level; // 0=L1, 1=L2, 2=L3
    uint64_t priority;
};

struct PrefetcherStats {
    uint64_t total_prefetches = 0;
    uint64_t useful_prefetches = 0;
    uint64_t useless_prefetches = 0;
    double accuracy() const { return total_prefetches > 0 ? static_cast<double>(useful_prefetches) / total_prefetches : 0.0; }
};

class Prefetcher {
public:
    virtual ~Prefetcher() = default;
    virtual std::optional<PrefetchRequest> on_access(uint64_t addr, bool is_load) = 0;
    virtual std::optional<PrefetchRequest> on_miss(uint64_t addr, bool is_load) = 0;
    virtual PrefetcherStats stats() const = 0;
    virtual void reset_stats() = 0;
};

class NextLinePrefetcher : public Prefetcher {
public:
    explicit NextLinePrefetcher(std::size_t line_size, std::size_t max_outstanding);

    std::optional<PrefetchRequest> on_access(uint64_t addr, bool is_load) override;
    std::optional<PrefetchRequest> on_miss(uint64_t addr, bool is_load) override;
    PrefetcherStats stats() const override;
    void reset_stats() override;

private:
    std::size_t line_size_;
    std::size_t max_outstanding_;
    PrefetcherStats stats_;
};

class StridePrefetcher : public Prefetcher {
public:
    explicit StridePrefetcher(std::size_t line_size, std::size_t max_outstanding);

    std::optional<PrefetchRequest> on_access(uint64_t addr, bool is_load) override;
    std::optional<PrefetchRequest> on_miss(uint64_t addr, bool is_load) override;
    PrefetcherStats stats() const override;
    void reset_stats() override;

private:
    std::size_t line_size_;
    std::size_t max_outstanding_;
    std::vector<std::pair<uint64_t, int64_t>> stride_table_;
    PrefetcherStats stats_;
};

// --- EnhancedCache ---
struct EnhancedCacheStats {
    CacheStats cache_stats;
    MshrStats mshr_stats;
    PrefetcherStats prefetcher_stats;
};

class EnhancedCache {
public:
    static Result<std::unique_ptr<EnhancedCache>> create(CacheConfig config, std::size_t mshr_capacity,
        std::unique_ptr<Prefetcher> prefetcher);

    Result<bool> access(uint64_t addr, bool is_read);
    std::optional<uint64_t> fill_line(uint64_t addr);
    const EnhancedCacheStats& stats() const;
    void reset_stats();

private:
    EnhancedCache(std::unique_ptr<Cache> cache, Mshr mshr, std::unique_ptr<Prefetcher> prefetcher);

    std::unique_ptr<Cache> cache_;
    Mshr mshr_;
    std::unique_ptr<Prefetcher> prefetcher_;
    mutable EnhancedCacheStats stats_;
};

} // namespace arm_cpu
