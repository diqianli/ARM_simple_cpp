/// @file memory_subsystem.cpp
/// @brief Memory subsystem implementation.

#include "arm_cpu/memory/memory_subsystem.hpp"

namespace arm_cpu {

// --- MemoryRequest ---

MemoryRequest MemoryRequest::completed(InstructionId id, uint64_t cycle) {
    return {id, MemoryRequestState::Completed, cycle, std::nullopt};
}

MemoryRequest MemoryRequest::completed_with_cache_info(InstructionId id, uint64_t cycle, CacheAccessInfo info) {
    return {id, MemoryRequestState::Completed, cycle, std::move(info)};
}

MemoryRequest MemoryRequest::pending(InstructionId id) {
    return {id, MemoryRequestState::Pending, std::nullopt, std::nullopt};
}

bool MemoryRequest::is_completed() const { return state == MemoryRequestState::Completed; }

// --- MemorySubsystem ---

MemorySubsystem::MemorySubsystem(CPUConfig config, LoadStoreQueue lsq,
    Cache l1, Cache l2, Cache l3, DdrController ddr, MemoryController controller)
    : lsq_(std::move(lsq)), l1_cache_(std::move(l1)), l2_cache_(std::move(l2))
    , l3_cache_(std::move(l3)), ddr_controller_(std::move(ddr))
    , controller_(std::move(controller)), config_(std::move(config)) {}

Result<std::unique_ptr<MemorySubsystem>> MemorySubsystem::create(CPUConfig config) {
    auto lsq = LoadStoreQueue(config.lsq_size, config.load_pipeline_count, config.store_pipeline_count);

    CacheConfig l1_cfg{config.l1_size, config.l1_associativity, config.l1_line_size, config.l1_hit_latency, "L1"};
    auto l1 = Cache::create(l1_cfg);
    if (l1.has_error()) return l1.error();
    CacheConfig l2_cfg{config.l2_size, config.l2_associativity, config.l2_line_size, config.l2_hit_latency, "L2"};
    auto l2 = Cache::create(l2_cfg);
    if (l2.has_error()) return l2.error();
    CacheConfig l3_cfg{config.l3_size, config.l3_associativity, config.l3_line_size, config.l3_hit_latency, "L3"};
    auto l3 = Cache::create(l3_cfg);
    if (l3.has_error()) return l3.error();

    auto ddr = DdrController(config.ddr_base_latency, config.ddr_row_buffer_hit_bonus,
        config.ddr_bank_conflict_penalty, config.ddr_num_banks);
    auto ctrl = MemoryController(config.l2_miss_latency, config.outstanding_requests);

    return std::unique_ptr<MemorySubsystem>(new MemorySubsystem(config, std::move(lsq),
        std::move(*l1.value()), std::move(*l2.value()), std::move(*l3.value()), std::move(ddr), std::move(ctrl)));
}

MemoryRequest MemorySubsystem::load(InstructionId id, const MemAccess& access) {
    auto l1_hit = l1_cache_.access(access.addr, true);
    if (l1_hit.ok() && l1_hit.value()) {
        auto handle = lsq_.add_load(id, access.addr, access.size);
        auto complete_cycle = current_cycle_ + config_.l1_hit_latency;
        lsq_.complete(handle);
        return MemoryRequest::completed_with_cache_info(id, complete_cycle,
            CacheAccessInfo::l1_hit(current_cycle_, config_.l1_hit_latency));
    }

    auto l2_hit = l2_cache_.access(access.addr, true);
    if (l2_hit.ok() && l2_hit.value()) {
        auto handle = lsq_.add_load(id, access.addr, access.size);
        auto complete_cycle = current_cycle_ + config_.l1_hit_latency + config_.l2_hit_latency;
        l1_cache_.fill_line(access.addr);
        lsq_.complete(handle);
        return MemoryRequest::completed_with_cache_info(id, complete_cycle,
            CacheAccessInfo::l2_hit(current_cycle_, config_.l1_hit_latency, config_.l2_hit_latency));
    }

    auto l3_hit = l3_cache_.access(access.addr, true);
    if (l3_hit.ok() && l3_hit.value()) {
        auto handle = lsq_.add_load(id, access.addr, access.size);
        auto complete_cycle = current_cycle_ + config_.l1_hit_latency + config_.l2_hit_latency + config_.l3_hit_latency;
        l2_cache_.fill_line(access.addr);
        l1_cache_.fill_line(access.addr);
        lsq_.complete(handle);
        return MemoryRequest::completed_with_cache_info(id, complete_cycle,
            CacheAccessInfo::l3_hit(current_cycle_, config_.l1_hit_latency, config_.l2_hit_latency, config_.l3_hit_latency));
    }

    // L3 miss -> DDR
    auto handle = lsq_.add_load(id, access.addr, access.size);
    ddr_controller_.set_cycle(current_cycle_ + config_.l1_hit_latency + config_.l2_hit_latency + config_.l3_hit_latency);
    auto ddr_result = ddr_controller_.access(access.addr);
    l3_cache_.fill_line(access.addr);
    l2_cache_.fill_line(access.addr);
    l1_cache_.fill_line(access.addr);
    lsq_.complete(handle);

    auto cache_info = CacheAccessInfo::memory_access(current_cycle_,
        config_.l1_hit_latency, config_.l2_hit_latency, config_.l3_hit_latency,
        ddr_result.latency, ddr_result.row_hit, ddr_result.bank);

    return MemoryRequest::completed_with_cache_info(id, ddr_result.complete_cycle, std::move(cache_info));
}

MemoryRequest MemorySubsystem::store(InstructionId id, const MemAccess& access) {
    auto handle = lsq_.add_store(id, access.addr, access.size);
    auto complete_cycle = current_cycle_ + config_.l1_hit_latency;
    lsq_.complete(handle);
    l1_cache_.access(access.addr, false);
    return MemoryRequest::completed_with_cache_info(id, complete_cycle,
        CacheAccessInfo::l1_hit(current_cycle_, config_.l1_hit_latency));
}

void MemorySubsystem::advance_cycle() {
    current_cycle_++;
    auto completed = controller_.poll_completed(current_cycle_);
    for (auto& _ : completed) outstanding_requests_ = outstanding_requests_ > 0 ? outstanding_requests_ - 1 : 0;
}

uint64_t MemorySubsystem::current_cycle() const { return current_cycle_; }
const CacheStats& MemorySubsystem::l1_stats() const { return l1_cache_.stats(); }
const CacheStats& MemorySubsystem::l2_stats() const { return l2_cache_.stats(); }
const CacheStats& MemorySubsystem::l3_stats() const { return l3_cache_.stats(); }
const DdrStats& MemorySubsystem::ddr_stats() const { return ddr_controller_.stats(); }
uint64_t MemorySubsystem::outstanding_count() const { return outstanding_requests_; }

void MemorySubsystem::reset_stats() {
    l1_cache_.reset_stats(); l2_cache_.reset_stats(); l3_cache_.reset_stats();
    ddr_controller_.reset_stats();
}

CacheHierarchyStats MemorySubsystem::get_stats() const {
    return {l1_cache_.stats(), l2_cache_.stats(), l3_cache_.stats(),
        ddr_controller_.stats(), lsq_.occupancy(), config_.lsq_size, outstanding_requests_};
}

} // namespace arm_cpu
