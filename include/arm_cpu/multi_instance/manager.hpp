#pragma once

/// @file manager.hpp
/// @brief Management of multiple simulation instances.
///
/// Provides parallel execution and result aggregation across multiple
/// isolated simulation instances.
///
/// Ported from Rust src/multi_instance/manager.rs.

#include "arm_cpu/config.hpp"
#include "arm_cpu/multi_instance/instance.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace arm_cpu {

// =====================================================================
// MultiRunConfig -- configuration for running multiple instances
// =====================================================================
struct MultiRunConfig {
    uint64_t max_cycles = 1'000'000;
    uint64_t max_instructions = 1'000'000;
    bool parallel = true;
    std::size_t num_threads = 0;  // 0 = auto
    bool save_traces = false;
    std::optional<std::string> trace_output_dir;

    static MultiRunConfig default_config() { return MultiRunConfig{}; }
};

// =====================================================================
// AggregatedResults -- aggregated results from multiple instances
// =====================================================================
struct AggregatedResults {
    std::size_t total_instances = 0;
    std::size_t successful_instances = 0;
    std::size_t failed_instances = 0;
    double avg_ipc = 0.0;
    double min_ipc = 0.0;
    double max_ipc = 0.0;
    double avg_cache_hit_rate = 0.0;
    uint64_t total_execution_time_ms = 0;
    std::vector<InstanceResult> instance_results;

    /// Calculate aggregated statistics from individual results.
    static AggregatedResults from_results(std::vector<InstanceResult> results);

    /// Get a summary string.
    std::string summary() const;
};

// =====================================================================
// InstanceManager -- manager for multiple simulation instances
// =====================================================================
class InstanceManager {
public:
    /// Create a new instance manager with default run configuration.
    explicit InstanceManager(CPUConfig config_template);

    /// Create an instance manager with custom run configuration.
    InstanceManager(CPUConfig config_template, MultiRunConfig run_config);

    /// Create a new simulation instance. Returns the new instance ID.
    InstanceId create_instance();

    /// Get an instance by ID (returns nullptr if not found).
    std::unique_ptr<SimulationInstance> get_instance(InstanceId id) const;

    /// Remove an instance. Returns the removed instance or nullptr.
    std::unique_ptr<SimulationInstance> remove_instance(InstanceId id);

    /// Run a single instance.
    Result<InstanceResult> run_instance(InstanceId id);

    /// Run all instances in parallel.
    Result<AggregatedResults> run_all_parallel();

    /// Cancel all running instances.
    void cancel();

    /// Check if cancelled.
    bool is_cancelled() const;

    /// Get all completed results.
    std::vector<InstanceResult> get_results() const;

    /// Clear all results.
    void clear_results();

    /// Get number of active instances.
    std::size_t instance_count() const;

    /// Get the run configuration.
    const MultiRunConfig& run_config() const { return run_config_; }

    /// Set the run configuration.
    void set_run_config(MultiRunConfig config) { run_config_ = std::move(config); }

private:
    static InstanceId generate_instance_id();

    CPUConfig config_template_;
    MultiRunConfig run_config_;
    mutable std::mutex instances_mutex_;
    std::unordered_map<InstanceId, std::unique_ptr<SimulationInstance>, InstanceId::Hash> instances_;
    mutable std::mutex results_mutex_;
    std::vector<InstanceResult> results_;
    std::atomic<bool> cancelled_{false};
};

/// Generate a new unique instance ID (global counter).
InstanceId generate_instance_id();

} // namespace arm_cpu
