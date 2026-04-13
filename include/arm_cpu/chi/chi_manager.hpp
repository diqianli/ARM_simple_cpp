#pragma once

/// @file chi_manager.hpp
/// @brief ChiManager — high-level interface that combines timing and
///        interface modules for simple CHI simulation.

#include "arm_cpu/chi/chi_timing.hpp"
#include "arm_cpu/chi/chi_interface.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <optional>

namespace arm_cpu {

// =====================================================================
// ChiStats
// =====================================================================
struct ChiStats {
    uint64_t    read_transactions  = 0;
    uint64_t    write_transactions = 0;
    uint64_t    snoop_count        = 0;
    std::size_t outstanding_count  = 0;
};

// =====================================================================
// ChiConfig — configuration parameters for ChiManager
// =====================================================================
struct ChiConfig {
    bool     enable_chi          = true;
    uint64_t chi_request_latency = 2;
    uint64_t chi_response_latency = 2;
    std::size_t outstanding_requests = 16;
};

// =====================================================================
// ChiManager
// =====================================================================
class ChiManager {
public:
    explicit ChiManager(const ChiConfig& config);

    bool is_enabled() const;

    std::optional<uint64_t> send_read(InstructionId id, uint64_t addr, uint8_t size);
    std::optional<uint64_t> send_write(InstructionId id, uint64_t addr, uint8_t size);

    void     advance_cycle();
    uint64_t current_cycle() const;

    ChiStats get_stats() const;

private:
    ChiTimingModel timing_;
    ChiInterface   interface_;
    bool           enabled_;
};

} // namespace arm_cpu
