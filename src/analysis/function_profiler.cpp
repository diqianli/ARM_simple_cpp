/// @file function_profiler.cpp
/// @brief Function-level profiling implementation.

#include "arm_cpu/analysis/function_profiler.hpp"

#include <algorithm>
#include <functional>

namespace arm_cpu {

// =====================================================================
// CallStackTracker
// =====================================================================

void CallStackTracker::push(const std::string& function) {
    stack_.push_back(function);
}

void CallStackTracker::pop() {
    if (!stack_.empty()) {
        stack_.pop_back();
    }
}

void CallStackTracker::record_samples(uint64_t samples) {
    if (stack_.empty()) {
        root_.value += samples;
        return;
    }

    // Build key from stack
    std::string key;
    for (std::size_t i = 0; i < stack_.size(); ++i) {
        if (i > 0) key += '|';
        key += stack_[i];
    }
    stack_values_[key] += samples;
}

FlameGraphNode CallStackTracker::generate_flame_graph() const {
    FlameGraphNode root = FlameGraphNode::create("all", 0);

    for (const auto& [stack_key, value] : stack_values_) {
        FlameGraphNode* current = &root;

        // Parse stack from key
        std::string func;
        for (std::size_t i = 0; i < stack_key.size(); ++i) {
            if (stack_key[i] == '|') {
                current = current->find_or_create_child(func);
                func.clear();
            } else {
                func += stack_key[i];
            }
        }
        if (!func.empty()) {
            current = current->find_or_create_child(func);
        }

        current->value += value;
        current->self_value += value;
    }

    // Sort children by value descending (recursive lambda)
    std::function<void(FlameGraphNode&)> sort_children = [&](FlameGraphNode& node) {
        std::sort(node.children.begin(), node.children.end(),
                  [](const FlameGraphNode& a, const FlameGraphNode& b) { return a.value > b.value; });
        for (auto& child : node.children) {
            sort_children(child);
        }
    };
    sort_children(root);

    return root;
}

// =====================================================================
// FunctionProfiler
// =====================================================================

void FunctionProfiler::register_function(const std::string& name, uint64_t start_pc, uint64_t end_pc) {
    FunctionStats stats;
    stats.name = name;
    stats.start_pc = start_pc;
    stats.end_pc = end_pc;

    for (uint64_t pc = start_pc; pc <= end_pc; ++pc) {
        pc_to_function_[pc] = start_pc;
    }
    functions_[start_pc] = std::move(stats);
}

void FunctionProfiler::record_instruction(uint64_t pc, uint64_t cycles, bool is_memory, bool is_cache_miss) {
    auto it = pc_to_function_.find(pc);
    if (it == pc_to_function_.end()) return;

    auto stats_it = functions_.find(it->second);
    if (stats_it == functions_.end()) return;

    ++stats_it->second.instruction_count;
    stats_it->second.cycle_count += cycles;

    if (is_memory) ++current_mem_ops_;
    if (is_cache_miss) ++current_cache_misses_;

    if (current_function_ != it->second) {
        if (current_function_.has_value()) {
            finalize_function_stats(*current_function_);
        }
        current_function_ = it->second;
        current_cache_misses_ = is_cache_miss ? 1 : 0;
        current_mem_ops_ = is_memory ? 1 : 0;
    }
}

void FunctionProfiler::finalize_function_stats(uint64_t func_pc) {
    auto it = functions_.find(func_pc);
    if (it == functions_.end()) return;

    if (it->second.cycle_count > 0) {
        it->second.ipc = static_cast<double>(it->second.instruction_count)
                       / static_cast<double>(it->second.cycle_count);
    }
    if (current_mem_ops_ > 0) {
        it->second.cache_miss_rate = static_cast<double>(current_cache_misses_)
                                   / static_cast<double>(current_mem_ops_);
    }
}

std::vector<FunctionStats> FunctionProfiler::get_stats() const {
    std::vector<FunctionStats> result;
    result.reserve(functions_.size());
    for (const auto& [pc, stats] : functions_) {
        result.push_back(stats);
    }
    return result;
}

std::vector<const FunctionStats*> FunctionProfiler::get_hotspots(std::size_t limit) const {
    std::vector<const FunctionStats*> result;
    result.reserve(functions_.size());
    for (const auto& [pc, stats] : functions_) {
        result.push_back(&stats);
    }
    std::partial_sort(result.begin(),
                      result.begin() + std::min(result.size(), limit),
                      result.end(),
                      [](const FunctionStats* a, const FunctionStats* b) {
                          return a->hotspot_score() > b->hotspot_score();
                      });
    if (result.size() > limit) result.resize(limit);
    return result;
}

std::vector<const FunctionStats*> FunctionProfiler::get_low_ipc_functions(double threshold, std::size_t limit) const {
    std::vector<const FunctionStats*> result;
    for (const auto& [pc, stats] : functions_) {
        if (stats.ipc < threshold && stats.instruction_count > 100) {
            result.push_back(&stats);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const FunctionStats* a, const FunctionStats* b) { return a->ipc < b->ipc; });
    if (result.size() > limit) result.resize(limit);
    return result;
}

std::vector<const FunctionStats*> FunctionProfiler::get_cache_bound_functions(double threshold, std::size_t limit) const {
    std::vector<const FunctionStats*> result;
    for (const auto& [pc, stats] : functions_) {
        if (stats.cache_miss_rate > threshold && stats.instruction_count > 100) {
            result.push_back(&stats);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const FunctionStats* a, const FunctionStats* b) { return a->cache_miss_rate > b->cache_miss_rate; });
    if (result.size() > limit) result.resize(limit);
    return result;
}

std::vector<FunctionStats> FunctionProfiler::finalize() {
    if (current_function_.has_value()) {
        finalize_function_stats(*current_function_);
    }

    std::vector<FunctionStats> result;
    result.reserve(functions_.size());
    for (auto& [pc, stats] : functions_) {
        result.push_back(std::move(stats));
    }
    return result;
}

} // namespace arm_cpu
