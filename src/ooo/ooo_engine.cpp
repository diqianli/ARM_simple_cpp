/// @file ooo_engine.cpp
/// @brief Out-of-order execution engine implementation.

#include "arm_cpu/ooo/ooo_engine.hpp"
#include <spdlog/spdlog.h>

namespace arm_cpu {

OoOEngine::OoOEngine(CPUConfig config)
    : config_(std::move(config))
    , window_(config_.window_size)
    , scheduler_(config_.issue_width, config_.commit_width)
{}

Result<std::unique_ptr<OoOEngine>> OoOEngine::create(CPUConfig config) {
    return std::unique_ptr<OoOEngine>(new OoOEngine(std::move(config)));
}

bool OoOEngine::can_accept() const { return window_.has_space(); }
std::size_t OoOEngine::free_slots() const { return window_.free_slots(); }

Result<std::vector<DependencyInfo>> OoOEngine::dispatch(Instruction instr) {
    auto id = instr.id;
    auto dependencies = dependency_tracker_.register_instruction(instr, id, current_cycle_);
    auto result = window_.insert(std::move(instr));
    if (result.has_error()) return result.error();

    if (dependency_tracker_.is_ready(id)) {
        window_.mark_ready(id);
        scheduler_.add_ready(id);
    }
    return dependencies;
}

std::vector<std::pair<InstructionId, Instruction>>
OoOEngine::get_ready_instructions() {
    return scheduler_.get_ready(window_);
}

void OoOEngine::mark_executing(InstructionId id) {
    window_.mark_executing(id);
}

void OoOEngine::mark_completed(InstructionId id, uint64_t complete_cycle) {
    window_.set_complete_cycle(id, complete_cycle);
    pending_completions_[complete_cycle].push_back(id);
}

std::size_t OoOEngine::process_completions() {
    newly_ready_.clear();

    std::vector<uint64_t> due_cycles;
    for (const auto& [cycle, _] : pending_completions_) {
        if (cycle <= current_cycle_) due_cycles.push_back(cycle);
    }

    std::size_t count = 0;
    for (auto cycle : due_cycles) {
        auto it = pending_completions_.find(cycle);
        if (it == pending_completions_.end()) continue;
        auto ids = std::move(it->second);
        pending_completions_.erase(it);

        for (auto id : ids) {
            count++;
            auto dependents = dependency_tracker_.get_dependents(id);

            if (id.value <= 10 || (id.value >= 118 && id.value <= 122)) {
                spdlog::debug("Instruction {} completing at cycle {}, has {} dependents",
                    id.value, cycle, dependents.size());
            }

            window_.set_status_completed(id);
            window_.mark_completion_processed(id);
            dependency_tracker_.release_dependencies(id);

            for (auto dep_id : dependents) {
                if (dependency_tracker_.is_ready(dep_id)) {
                    window_.mark_ready(dep_id);
                    scheduler_.add_ready(dep_id);
                    newly_ready_.push_back(dep_id);
                }
            }
        }
    }
    return count;
}

std::vector<InstructionId> OoOEngine::take_newly_ready() {
    return std::move(newly_ready_);
}

std::vector<Instruction> OoOEngine::get_commit_candidates() {
    std::vector<Instruction> candidates;
    for (std::size_t i = 0; i < config_.commit_width; i++) {
        auto id = InstructionId(next_commit_id_);
        auto* entry = window_.get_entry(id);
        if (entry && entry->status == InstrStatus::Completed && entry->completion_processed) {
            candidates.push_back(entry->instruction);
            next_commit_id_++;
        } else {
            break;
        }
    }
    return candidates;
}

void OoOEngine::commit(InstructionId id) {
    window_.remove(id);
}

void OoOEngine::advance_cycle() { current_cycle_++; }
std::size_t OoOEngine::cycle_tick() { return process_completions(); }

uint64_t OoOEngine::current_cycle() const { return current_cycle_; }
std::size_t OoOEngine::window_size() const { return window_.len(); }
bool OoOEngine::is_empty() const { return window_.is_empty(); }

OoOStats OoOEngine::get_stats() const {
    return {
        window_.len(),
        config_.window_size,
        scheduler_.ready_count(),
        current_cycle_,
        next_commit_id_,
    };
}

std::tuple<std::size_t, std::size_t, std::size_t, std::size_t>
OoOEngine::status_counts() const { return window_.status_counts(); }

const WindowEntry* OoOEngine::get_window_entry(InstructionId id) const {
    return window_.get_entry(id);
}

uint64_t OoOEngine::next_commit_id() const { return next_commit_id_; }

std::vector<const WindowEntry*> OoOEngine::get_window_entries() const {
    return window_.entries();
}

const DependencyTracker& OoOEngine::dependency_tracker() const {
    return dependency_tracker_;
}

} // namespace arm_cpu
