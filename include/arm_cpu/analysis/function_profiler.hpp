#pragma once

/// @file function_profiler.hpp
/// @brief Function-level profiling for hotspot analysis.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace arm_cpu {

// =====================================================================
// FunctionStats
// =====================================================================
struct FunctionStats {
    std::string name;
    uint64_t start_pc = 0;
    uint64_t end_pc = 0;
    uint64_t instruction_count = 0;
    uint64_t cycle_count = 0;
    double ipc = 0.0;
    double cache_miss_rate = 0.0;

    /// Hotspot score: higher = more potential for optimization.
    double hotspot_score() const {
        if (cycle_count == 0) return 0.0;
        double wasted_cycles = static_cast<double>(cycle_count) * (1.0 - std::min(ipc, 1.0));
        double cache_penalty = cache_miss_rate * 10.0;
        return wasted_cycles + cache_penalty * static_cast<double>(instruction_count);
    }
};

// =====================================================================
// FlameGraphNode - For call stack visualization
// =====================================================================
struct FlameGraphNode {
    std::string name;
    uint64_t value = 0;
    uint64_t self_value = 0;
    std::vector<FlameGraphNode> children;

    static FlameGraphNode create(const std::string& n, uint64_t v) {
        return FlameGraphNode{n, v, v, {}};
    }

    void add_child(FlameGraphNode child) {
        value += child.value;
        children.push_back(std::move(child));
    }

    FlameGraphNode* find_or_create_child(const std::string& n) {
        for (auto& c : children) {
            if (c.name == n) return &c;
        }
        children.push_back(create(n, 0));
        return &children.back();
    }
};

// =====================================================================
// CallStackTracker - For flame graph generation
// =====================================================================
class CallStackTracker {
public:
    CallStackTracker() = default;

    void push(const std::string& function);
    void pop();
    void record_samples(uint64_t samples);
    FlameGraphNode generate_flame_graph() const;

private:
    FlameGraphNode root_{"root", 0, 0, {}};
    std::vector<std::string> stack_;
    std::unordered_map<std::string, uint64_t> stack_values_;
};

// =====================================================================
// FunctionProfiler
// =====================================================================
class FunctionProfiler {
public:
    FunctionProfiler() = default;

    /// Register a function with its address range.
    void register_function(const std::string& name, uint64_t start_pc, uint64_t end_pc);

    /// Record an instruction execution.
    void record_instruction(uint64_t pc, uint64_t cycles, bool is_memory, bool is_cache_miss);

    /// Get all function statistics.
    std::vector<FunctionStats> get_stats() const;

    /// Get hotspots sorted by optimization potential.
    std::vector<const FunctionStats*> get_hotspots(std::size_t limit) const;

    /// Get functions with lowest IPC.
    std::vector<const FunctionStats*> get_low_ipc_functions(double threshold, std::size_t limit) const;

    /// Get functions with highest cache miss rates.
    std::vector<const FunctionStats*> get_cache_bound_functions(double threshold, std::size_t limit) const;

    /// Finalize profiling and return all stats.
    std::vector<FunctionStats> finalize();

private:
    void finalize_function_stats(uint64_t func_pc);

    std::unordered_map<uint64_t, FunctionStats> functions_;
    std::unordered_map<uint64_t, uint64_t> pc_to_function_;
    std::optional<uint64_t> current_function_;
    uint64_t current_cache_misses_ = 0;
    uint64_t current_mem_ops_ = 0;
};

} // namespace arm_cpu
