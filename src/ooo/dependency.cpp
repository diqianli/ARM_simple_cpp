/// @file dependency.cpp
/// @brief Dependency tracking implementation.

#include "arm_cpu/ooo/dependency.hpp"
#include <spdlog/spdlog.h>

namespace arm_cpu {

std::vector<DependencyInfo> DependencyTracker::register_instruction(
    const Instruction& instr, InstructionId id, uint64_t current_cycle)
{
    (void)current_cycle;
    std::size_t deps_count = 0;
    std::vector<DependencyInfo> dependencies;

    // Register dependencies (RAW - Read After Write)
    for (const auto& src_reg : instr.src_regs) {
        auto it = register_producers_.find(src_reg);
        if (it != register_producers_.end()) {
            auto producer_id = it->second;
            if (producer_id != id && completed_instructions_.find(producer_id) == completed_instructions_.end()) {
                add_dependency(producer_id, id);
                deps_count++;
                dependencies.push_back({producer_id, false});
            }
        }
    }

    // Memory dependencies
    if (instr.mem_access.has_value()) {
        uint64_t addr = instr.mem_access->addr;
        auto it = memory_producers_.find(addr);
        if (it != memory_producers_.end()) {
            auto producer_id = it->second;
            if (producer_id != id && completed_instructions_.find(producer_id) == completed_instructions_.end()) {
                add_memory_dependency(producer_id, id);
                deps_count++;
                dependencies.push_back({producer_id, true});
            }
        }
    }

    // Update register producers
    for (const auto& dst_reg : instr.dst_regs) {
        register_producers_[dst_reg] = id;
    }

    // Update memory producer for stores
    if (instr.mem_access.has_value() && !instr.mem_access->is_load) {
        memory_producers_[instr.mem_access->addr] = id;
    }

    pending_dependencies_[id] = deps_count;

    if (id.value <= 10) {
        spdlog::debug("Instruction {} registered with {} pending dependencies, is_ready: {}",
            id.value, deps_count, deps_count == 0);
    }

    return dependencies;
}

bool DependencyTracker::is_ready(InstructionId id) const {
    auto it = pending_dependencies_.find(id);
    auto pending = (it != pending_dependencies_.end()) ? it->second : 0;
    return pending == 0;
}

void DependencyTracker::release_dependencies(InstructionId id) {
    completed_instructions_.insert(id);

    auto dep_it = dependents_.find(id);
    if (dep_it != dependents_.end()) {
        for (auto dep_id : dep_it->second) {
            auto it = pending_dependencies_.find(dep_id);
            if (it != pending_dependencies_.end() && it->second > 0) {
                it->second--;
            }
        }
        dependents_.erase(dep_it);
    }

    memory_dependents_.erase(id);
}

std::vector<InstructionId> DependencyTracker::get_dependents(InstructionId id) const {
    auto it = dependents_.find(id);
    if (it != dependents_.end()) return it->second;
    return {};
}

std::size_t DependencyTracker::pending_count(InstructionId id) const {
    auto it = pending_dependencies_.find(id);
    return (it != pending_dependencies_.end()) ? it->second : 0;
}

void DependencyTracker::clear() {
    register_producers_.clear();
    pending_dependencies_.clear();
    dependents_.clear();
    memory_producers_.clear();
    memory_dependents_.clear();
    completed_instructions_.clear();
}

DependencyStats DependencyTracker::get_stats() const {
    DependencyStats stats;
    stats.register_producers = register_producers_.size();
    stats.pending_instructions = pending_dependencies_.size();
    for (const auto& [_, deps] : dependents_) {
        stats.total_dependents += deps.size();
    }
    return stats;
}

std::vector<std::tuple<InstructionId, InstructionId, bool>>
DependencyTracker::get_all_dependencies() const {
    std::vector<std::tuple<InstructionId, InstructionId, bool>> deps;
    for (const auto& [producer, consumers] : dependents_) {
        for (auto consumer : consumers) {
            bool is_mem = false;
            auto mem_it = memory_dependents_.find(producer);
            if (mem_it != memory_dependents_.end()) {
                for (auto mc : mem_it->second) {
                    if (mc == consumer) { is_mem = true; break; }
                }
            }
            deps.emplace_back(producer, consumer, is_mem);
        }
    }
    return deps;
}

void DependencyTracker::add_dependency(InstructionId producer, InstructionId consumer) {
    if (completed_instructions_.find(producer) != completed_instructions_.end()) return;
    dependents_[producer].push_back(consumer);
}

void DependencyTracker::add_memory_dependency(InstructionId producer, InstructionId consumer) {
    memory_dependents_[producer].push_back(consumer);
    add_dependency(producer, consumer);
}

} // namespace arm_cpu
