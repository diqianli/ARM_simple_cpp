#pragma once

/// @file memory_subsystem.hpp
/// @brief Memory subsystem combining LSQ, caches, and controller.

#include "arm_cpu/config.hpp"
#include "arm_cpu/memory/cache.hpp"
#include "arm_cpu/memory/lsq.hpp"
#include "arm_cpu/memory/ddr_controller.hpp"
#include "arm_cpu/memory/controller.hpp"

namespace arm_cpu {

enum class MemoryRequestState : uint8_t { Pending, Completed };

struct MemoryRequest {
    InstructionId instruction_id;
    MemoryRequestState state = MemoryRequestState::Pending;
    std::optional<uint64_t> complete_cycle;
    std::optional<CacheAccessInfo> cache_info;

    static MemoryRequest completed(InstructionId id, uint64_t cycle);
    static MemoryRequest completed_with_cache_info(InstructionId id, uint64_t cycle, CacheAccessInfo info);
    static MemoryRequest pending(InstructionId id);
    bool is_completed() const;
};

struct CacheHierarchyStats {
    CacheStats l1_stats;
    CacheStats l2_stats;
    CacheStats l3_stats;
    DdrStats ddr_stats;
    std::size_t lsq_occupancy = 0;
    std::size_t lsq_capacity = 0;
    uint64_t outstanding_requests = 0;
};

class MemorySubsystem {
public:
    static Result<std::unique_ptr<MemorySubsystem>> create(CPUConfig config);

    MemoryRequest load(InstructionId id, const MemAccess& access);
    MemoryRequest store(InstructionId id, const MemAccess& access);
    void advance_cycle();
    uint64_t current_cycle() const;
    const CacheStats& l1_stats() const;
    const CacheStats& l2_stats() const;
    const CacheStats& l3_stats() const;
    const DdrStats& ddr_stats() const;
    uint64_t outstanding_count() const;
    void reset_stats();
    CacheHierarchyStats get_stats() const;

    Cache& l1_cache() { return l1_cache_; }
    Cache& l2_cache() { return l2_cache_; }
    Cache& l3_cache() { return l3_cache_; }
    LoadStoreQueue& lsq() { return lsq_; }

private:
    MemorySubsystem(CPUConfig config, LoadStoreQueue lsq,
                    Cache l1, Cache l2, Cache l3,
                    DdrController ddr, MemoryController controller);

    LoadStoreQueue lsq_;
    Cache l1_cache_;
    Cache l2_cache_;
    Cache l3_cache_;
    DdrController ddr_controller_;
    MemoryController controller_;
    CPUConfig config_;
    uint64_t current_cycle_ = 0;
    uint64_t outstanding_requests_ = 0;
};

} // namespace arm_cpu
