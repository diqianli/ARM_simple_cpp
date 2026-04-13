#pragma once

/// @file instance.hpp
/// @brief Isolated simulation instance that can run independently.
///
/// Ported from Rust src/multi_instance/instance.rs.

#include "arm_cpu/config.hpp"
#include "arm_cpu/stats/performance_metrics.hpp"
#include "arm_cpu/types.hpp"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

namespace arm_cpu {

// Forward declarations
class CPUEmulator;

// =====================================================================
// InstanceId -- unique identifier for a simulation instance
// =====================================================================
struct InstanceId {
    uint64_t value;

    constexpr InstanceId() : value(0) {}
    constexpr explicit InstanceId(uint64_t v) : value(v) {}

    bool operator==(const InstanceId& o) const { return value == o.value; }
    bool operator!=(const InstanceId& o) const { return value != o.value; }
    bool operator<(const InstanceId& o) const { return value < o.value; }

    struct Hash {
        std::size_t operator()(const InstanceId& id) const noexcept {
            return std::hash<uint64_t>{}(id.value);
        }
    };

    /// Format as string: "instance-<value>"
    std::string to_string() const;
};

// =====================================================================
// InstanceState -- lifecycle state of a simulation instance
// =====================================================================
enum class InstanceState : uint8_t {
    Idle,
    Running,
    Paused,
    Completed,
    Error,
};

// =====================================================================
// InstanceStats -- statistics for a simulation instance
// =====================================================================
struct InstanceStats {
    uint64_t cycles = 0;
    uint64_t instructions_retired = 0;
    uint64_t cache_hits = 0;
    uint64_t cache_misses = 0;
    uint64_t branch_mispredictions = 0;
};

// =====================================================================
// InstanceMetrics -- instance-specific metrics with execution time
// =====================================================================
struct InstanceMetrics {
    PerformanceMetrics perf;
    uint64_t execution_time_ms = 0;

    double cache_hit_rate() const { return perf.l1_hit_rate; }
};

// =====================================================================
// InstanceResult -- result of a completed simulation instance
// =====================================================================
struct InstanceResult {
    InstanceId instance_id;
    InstanceMetrics metrics;
    InstanceStats stats;
    std::optional<std::string> trace_path;
    std::optional<std::string> error;
};

// =====================================================================
// SimulationInstance -- a single isolated simulation instance
// =====================================================================
class SimulationInstance {
public:
    /// Create a new simulation instance.
    static std::unique_ptr<SimulationInstance> create(InstanceId id, CPUConfig config);

    /// Get the instance ID.
    InstanceId id() const { return id_; }

    /// Get the current state.
    InstanceState state() const { return state_; }

    /// Get the current statistics.
    const InstanceStats& stats() const { return stats_; }

    /// Get mutable statistics (for updating).
    InstanceStats& stats_mut() { return stats_; }

    /// Get the next instruction ID (thread-safe).
    uint64_t next_instruction_id();

    /// Run the simulation for a specified number of cycles.
    Result<InstanceResult> run_cycles(uint64_t max_cycles);

    /// Reset the instance to initial state.
    void reset();

    ~SimulationInstance(); // Defined in instance.cpp (needs complete type for unique_ptr<CPUEmulator>)

private:
    SimulationInstance(InstanceId id, CPUConfig config,
                       std::unique_ptr<CPUEmulator> emulator);

    InstanceId id_;
    CPUConfig config_;
    std::unique_ptr<CPUEmulator> emulator_;
    InstanceState state_ = InstanceState::Idle;
    InstanceStats stats_;
    std::atomic<uint64_t> instruction_counter_{0};
};

} // namespace arm_cpu
