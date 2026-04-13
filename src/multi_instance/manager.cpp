/// @file manager.cpp
/// @brief Instance manager implementation.
///
/// Ported from Rust src/multi_instance/manager.rs.

#include "arm_cpu/multi_instance/manager.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <format>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace arm_cpu {

// =====================================================================
// Global instance ID counter
// =====================================================================

namespace {
    std::atomic<uint64_t> g_instance_id_counter{1};
}

InstanceId generate_instance_id() {
    return InstanceId(g_instance_id_counter.fetch_add(1, std::memory_order_seq_cst));
}

InstanceId InstanceManager::generate_instance_id() {
    return arm_cpu::generate_instance_id();
}

// =====================================================================
// AggregatedResults
// =====================================================================

AggregatedResults AggregatedResults::from_results(std::vector<InstanceResult> results) {
    AggregatedResults agg;
    agg.total_instances = results.size();

    std::size_t successful = 0;
    double ipc_sum = 0.0;
    double min_ipc = std::numeric_limits<double>::infinity();
    double max_ipc = -std::numeric_limits<double>::infinity();
    double cache_sum = 0.0;
    uint64_t total_time_ms = 0;

    for (const auto& r : results) {
        if (!r.error.has_value()) {
            successful++;
            ipc_sum += r.metrics.perf.ipc;
            min_ipc = std::min(min_ipc, r.metrics.perf.ipc);
            max_ipc = std::max(max_ipc, r.metrics.perf.ipc);
            cache_sum += r.metrics.perf.l1_hit_rate;
        }
        total_time_ms += r.metrics.execution_time_ms;
    }

    agg.successful_instances = successful;
    agg.failed_instances = agg.total_instances - successful;
    agg.avg_ipc = (successful > 0) ? (ipc_sum / static_cast<double>(successful)) : 0.0;
    agg.min_ipc = (successful > 0) ? min_ipc : 0.0;
    agg.max_ipc = (successful > 0) ? max_ipc : 0.0;
    agg.avg_cache_hit_rate = (successful > 0) ? (cache_sum / static_cast<double>(successful)) : 1.0;
    agg.total_execution_time_ms = total_time_ms;
    agg.instance_results = std::move(results);

    return agg;
}

std::string AggregatedResults::summary() const {
    return std::format(
        "Aggregated Results:\n"
        "==================\n"
        "Total instances: {}\n"
        "Successful: {}\n"
        "Failed: {}\n"
        "\n"
        "IPC Statistics:\n"
        "  Average: {:.3}\n"
        "  Min: {:.3}\n"
        "  Max: {:.3}\n"
        "\n"
        "Cache Hit Rate: {:.2}%\n"
        "Total execution time: {} ms",
        total_instances,
        successful_instances,
        failed_instances,
        avg_ipc,
        min_ipc,
        max_ipc,
        avg_cache_hit_rate * 100.0,
        total_execution_time_ms
    );
}

// =====================================================================
// InstanceManager
// =====================================================================

InstanceManager::InstanceManager(CPUConfig config_template)
    : config_template_(std::move(config_template))
    , run_config_(MultiRunConfig::default_config())
{}

InstanceManager::InstanceManager(CPUConfig config_template, MultiRunConfig run_config)
    : config_template_(std::move(config_template))
    , run_config_(std::move(run_config))
{}

InstanceId InstanceManager::create_instance() {
    InstanceId id = generate_instance_id();
    auto instance = SimulationInstance::create(id, config_template_);

    std::lock_guard<std::mutex> lock(instances_mutex_);
    instances_[id] = std::move(instance);

    return id;
}

std::unique_ptr<SimulationInstance> InstanceManager::get_instance(InstanceId id) const {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    auto it = instances_.find(id);
    if (it == instances_.end()) return nullptr;

    // Return a clone-like copy by creating a new instance with the same config
    return SimulationInstance::create(id, config_template_);
}

std::unique_ptr<SimulationInstance> InstanceManager::remove_instance(InstanceId id) {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    auto it = instances_.find(id);
    if (it == instances_.end()) return nullptr;

    auto instance = std::move(it->second);
    instances_.erase(it);
    return instance;
}

Result<InstanceResult> InstanceManager::run_instance(InstanceId id) {
    std::lock_guard<std::mutex> lock(instances_mutex_);

    auto it = instances_.find(id);
    if (it == instances_.end()) {
        return EmulatorError::internal("Instance not found");
    }

    auto result = it->second->run_cycles(run_config_.max_cycles);
    if (result.ok()) {
        std::lock_guard<std::mutex> results_lock(results_mutex_);
        results_.push_back(result.value());
    }

    return result;
}

Result<AggregatedResults> InstanceManager::run_all_parallel() {
    // Collect all instance IDs
    std::vector<InstanceId> ids;
    {
        std::lock_guard<std::mutex> lock(instances_mutex_);
        ids.reserve(instances_.size());
        for (const auto& [id, _] : instances_) {
            ids.push_back(id);
        }
    }

    if (ids.empty()) {
        return AggregatedResults{};
    }

    // Determine thread count
    std::size_t num_threads = run_config_.num_threads;
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
    }

    uint64_t max_cycles = run_config_.max_cycles;
    CPUConfig config_copy = config_template_;

    // Run instances in parallel using std::async
    std::vector<std::future<InstanceResult>> futures;
    futures.reserve(ids.size());

    for (auto id : ids) {
        futures.push_back(std::async(std::launch::async, [id, config_copy, max_cycles]() {
            try {
                auto instance = SimulationInstance::create(id, config_copy);
                auto result = instance->run_cycles(max_cycles);
                if (result.ok()) {
                    return result.value();
                }
                InstanceResult ir;
                ir.instance_id = id;
                ir.metrics = InstanceMetrics{};
                ir.stats = InstanceStats{};
                ir.error = "Error: " + result.error().message();
                return ir;
            } catch (const std::exception& e) {
                InstanceResult ir;
                ir.instance_id = id;
                ir.metrics = InstanceMetrics{};
                ir.stats = InstanceStats{};
                ir.error = std::string("Error: ") + e.what();
                return ir;
            }
        }));
    }

    // Collect results
    std::vector<InstanceResult> results;
    results.reserve(futures.size());
    for (auto& f : futures) {
        results.push_back(f.get());
    }

    // Update stored results
    {
        std::lock_guard<std::mutex> lock(results_mutex_);
        results_.insert(results_.end(), results.begin(), results.end());
    }

    return AggregatedResults::from_results(std::move(results));
}

void InstanceManager::cancel() {
    cancelled_.store(true, std::memory_order_seq_cst);
}

bool InstanceManager::is_cancelled() const {
    return cancelled_.load(std::memory_order_seq_cst);
}

std::vector<InstanceResult> InstanceManager::get_results() const {
    std::lock_guard<std::mutex> lock(results_mutex_);
    return results_;
}

void InstanceManager::clear_results() {
    std::lock_guard<std::mutex> lock(results_mutex_);
    results_.clear();
}

std::size_t InstanceManager::instance_count() const {
    std::lock_guard<std::mutex> lock(instances_mutex_);
    return instances_.size();
}

} // namespace arm_cpu
