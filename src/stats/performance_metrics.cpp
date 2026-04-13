/// @file performance_metrics.cpp
/// @brief Performance metrics implementation.
///
/// Ported from Rust src/stats/metrics.rs.

#include "arm_cpu/stats/performance_metrics.hpp"

#include <cstdio>
#include <format>

namespace arm_cpu {

// =====================================================================
// PerformanceMetrics
// =====================================================================

uint64_t PerformanceMetrics::execution_time_ns(uint64_t frequency_mhz) const {
    if (frequency_mhz == 0) return 0;
    double cycle_ns = 1000.0 / static_cast<double>(frequency_mhz);
    return static_cast<uint64_t>(static_cast<double>(total_cycles) * cycle_ns);
}

double PerformanceMetrics::throughput_mips(uint64_t frequency_mhz) const {
    if (frequency_mhz == 0 || total_cycles == 0) return 0.0;
    double seconds = static_cast<double>(execution_time_ns(frequency_mhz)) / 1e9;
    if (seconds == 0.0) return 0.0;
    return static_cast<double>(total_instructions) / seconds / 1e6;
}

std::string PerformanceMetrics::summary() const {
    return std::format(
        "Performance Metrics:\n"
        "====================\n"
        "Instructions: {}\n"
        "Cycles: {}\n"
        "IPC: {:.3}\n"
        "CPI: {:.3}\n"
        "\n"
        "L1 Cache:\n"
        "  Hit Rate: {:.2}%\n"
        "  MPKI: {:.2}\n"
        "\n"
        "L2 Cache:\n"
        "  Hit Rate: {:.2}%\n"
        "  MPKI: {:.2}\n"
        "\n"
        "Memory: {:.2}%\n"
        "Branch: {:.2}%\n"
        "\n"
        "Avg Load Latency: {:.2}\n"
        "Avg Store Latency: {:.2}",
        total_instructions,
        total_cycles,
        ipc,
        cpi,
        l1_hit_rate * 100.0,
        l1_mpki,
        l2_hit_rate * 100.0,
        l2_mpki,
        memory_instr_pct,
        branch_instr_pct,
        avg_load_latency,
        avg_store_latency
    );
}

} // namespace arm_cpu
