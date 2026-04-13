/// @file enhanced.cpp
/// @brief Enhanced cache features implementation.

#include "arm_cpu/memory/enhanced.hpp"

namespace arm_cpu {

// --- Mshr ---

Mshr::Mshr(std::size_t capacity) : capacity_(capacity) { entries_.reserve(capacity); }

bool Mshr::has_space() const { return entries_.size() < capacity_; }

std::optional<std::size_t> Mshr::allocate(const MshrEntry& entry) {
    if (!has_space()) return std::nullopt;
    // Check for MSHR merge
    for (std::size_t i = 0; i < entries_.size(); i++) {
        if (entries_[i].pending && entries_[i].addr == entry.addr) {
            stats_.mshr_hits++;
            return i; // Merge into existing entry
        }
    }
    entries_.push_back(entry);
    stats_.total_misses++;
    if (entry.miss_type == MissType::Read) stats_.read_misses++;
    else stats_.write_misses++;
    return entries_.size() - 1;
}

bool Mshr::is_pending(uint64_t addr) const {
    for (const auto& e : entries_) {
        if (e.pending && e.addr == addr) return true;
    }
    return false;
}

void Mshr::mark_complete(std::size_t index) {
    if (index < entries_.size()) {
        entries_[index].pending = false;
        stats_.total_latency += entries_[index].issue_cycle; // simplified
    }
}

const MshrEntry* Mshr::get(std::size_t index) const {
    return index < entries_.size() ? &entries_[index] : nullptr;
}

std::size_t Mshr::size() const { return entries_.size(); }

MshrStats Mshr::stats() const { return stats_; }

void Mshr::clear() { entries_.clear(); stats_ = MshrStats{}; }

// --- NextLinePrefetcher ---

NextLinePrefetcher::NextLinePrefetcher(std::size_t line_size, std::size_t max_outstanding)
    : line_size_(line_size), max_outstanding_(max_outstanding) {}

std::optional<PrefetchRequest> NextLinePrefetcher::on_access(uint64_t addr, bool is_load) {
    (void)is_load;
    stats_.total_prefetches++;
    return PrefetchRequest{addr + line_size_, 0, 0};
}

std::optional<PrefetchRequest> NextLinePrefetcher::on_miss(uint64_t addr, bool is_load) {
    return on_access(addr, is_load);
}

PrefetcherStats NextLinePrefetcher::stats() const { return stats_; }
void NextLinePrefetcher::reset_stats() { stats_ = PrefetcherStats{}; }

// --- StridePrefetcher ---

StridePrefetcher::StridePrefetcher(std::size_t line_size, std::size_t max_outstanding)
    : line_size_(line_size), max_outstanding_(max_outstanding) {}

std::optional<PrefetchRequest> StridePrefetcher::on_access(uint64_t addr, bool is_load) {
    (void)is_load;
    // Simple stride detection: if same stride seen 3+ times, prefetch next
    for (auto& [prev_addr, stride] : stride_table_) {
        if (prev_addr == addr && stride != 0) {
            stats_.total_prefetches++;
            return PrefetchRequest{addr + static_cast<uint64_t>(stride), 0, 1};
        }
    }
    // Track this address
    stride_table_.push_back({addr, 0});
    if (stride_table_.size() > 64) stride_table_.erase(stride_table_.begin());
    return std::nullopt;
}

std::optional<PrefetchRequest> StridePrefetcher::on_miss(uint64_t addr, bool is_load) {
    return on_access(addr, is_load);
}

PrefetcherStats StridePrefetcher::stats() const { return stats_; }
void StridePrefetcher::reset_stats() { stats_ = PrefetcherStats{}; }

// --- EnhancedCache ---

EnhancedCache::EnhancedCache(std::unique_ptr<Cache> cache, Mshr mshr, std::unique_ptr<Prefetcher> prefetcher)
    : cache_(std::move(cache)), mshr_(std::move(mshr)), prefetcher_(std::move(prefetcher))
{
    stats_.cache_stats.name = cache_->config().name;
}

Result<std::unique_ptr<EnhancedCache>>
EnhancedCache::create(CacheConfig config, std::size_t mshr_capacity, std::unique_ptr<Prefetcher> prefetcher) {
    auto cache = Cache::create(config);
    if (cache.has_error()) return cache.error();
    auto mshr = Mshr(mshr_capacity);
    return std::unique_ptr<EnhancedCache>(new EnhancedCache(std::move(*cache), std::move(mshr), std::move(prefetcher)));
}

Result<bool> EnhancedCache::access(uint64_t addr, bool is_read) {
    // Check prefetcher
    if (prefetcher_) {
        auto pf = prefetcher_->on_access(addr, is_read);
        if (pf.has_value()) {
            cache_->access(pf->addr, true); // speculative prefetch
        }
    }

    // Check MSHR
    if (mshr_.is_pending(addr)) {
        // Already pending - MSHR hit
        return false; // miss
    }

    // Normal cache access
    auto result = cache_->access(addr, is_read);
    if (result.ok() && result.value()) return true; // hit

    // Miss - allocate MSHR
    mshr_.allocate({addr, InstructionId(0), is_read ? MissType::Read : MissType::Write, 0});
    return false;
}

std::optional<uint64_t> EnhancedCache::fill_line(uint64_t addr) {
    return cache_->fill_line(addr);
}

const EnhancedCacheStats& EnhancedCache::stats() const {
    stats_.cache_stats = cache_->stats();
    stats_.mshr_stats = mshr_.stats();
    if (prefetcher_) stats_.prefetcher_stats = prefetcher_->stats();
    return stats_;
}

void EnhancedCache::reset_stats() {
    cache_->reset_stats();
    mshr_.clear();
    if (prefetcher_) prefetcher_->reset_stats();
}

} // namespace arm_cpu
