/// @file chi_manager.cpp
/// @brief ChiManager implementation -- high-level interface combining
///        timing and interface modules for simple CHI simulation.

#include "arm_cpu/chi/chi_manager.hpp"

namespace arm_cpu {

// =====================================================================
// ChiManager
// =====================================================================

ChiManager::ChiManager(const ChiConfig& config)
    : timing_(ChiTimingConfig{
          config.chi_request_latency,
          config.chi_response_latency,
          2, // data latency
          2  // snoop latency
      })
    , interface_(config.outstanding_requests)
    , enabled_(config.enable_chi)
{}

bool ChiManager::is_enabled() const {
    return enabled_;
}

std::optional<uint64_t> ChiManager::send_read(InstructionId id, uint64_t addr, uint8_t size) {
    if (!enabled_) return 0;

    auto txn = interface_.create_read_transaction(id, addr, size);
    if (!txn.has_value()) return std::nullopt;

    auto complete_cycle = timing_.calculate_completion(txn.value());
    return complete_cycle;
}

std::optional<uint64_t> ChiManager::send_write(InstructionId id, uint64_t addr, uint8_t size) {
    if (!enabled_) return 0;

    auto txn = interface_.create_write_transaction(id, addr, size);
    if (!txn.has_value()) return std::nullopt;

    auto complete_cycle = timing_.calculate_completion(txn.value());
    return complete_cycle;
}

void ChiManager::advance_cycle() {
    timing_.advance_cycle();
    interface_.advance_cycle();
}

uint64_t ChiManager::current_cycle() const {
    return timing_.current_cycle();
}

ChiStats ChiManager::get_stats() const {
    ChiStats stats;
    stats.read_transactions = interface_.read_count();
    stats.write_transactions = interface_.write_count();
    stats.snoop_count = interface_.snoop_count();
    stats.outstanding_count = interface_.outstanding_count();
    return stats;
}

} // namespace arm_cpu
