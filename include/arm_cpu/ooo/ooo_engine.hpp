#pragma once

/// @file ooo_engine.hpp
/// @brief Out-of-order execution engine.

#include "arm_cpu/config.hpp"
#include "arm_cpu/ooo/dependency.hpp"
#include "arm_cpu/ooo/scheduler.hpp"
#include "arm_cpu/ooo/window.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace arm_cpu {

struct OoOStats {
    std::size_t window_occupancy = 0;
    std::size_t window_capacity = 0;
    std::size_t ready_count = 0;
    uint64_t current_cycle = 0;
    uint64_t next_commit_id = 0;
};

class OoOEngine {
public:
    static Result<std::unique_ptr<OoOEngine>> create(CPUConfig config);

    bool can_accept() const;
    std::size_t free_slots() const;

    Result<std::vector<DependencyInfo>> dispatch(Instruction instr);
    std::vector<std::pair<InstructionId, Instruction>> get_ready_instructions();
    void mark_executing(InstructionId id);
    void mark_completed(InstructionId id, uint64_t complete_cycle);

    std::size_t process_completions();
    std::vector<InstructionId> take_newly_ready();

    std::vector<Instruction> get_commit_candidates();
    void commit(InstructionId id);

    void advance_cycle();
    std::size_t cycle_tick();

    uint64_t current_cycle() const;
    std::size_t window_size() const;
    bool is_empty() const;

    OoOStats get_stats() const;
    std::tuple<std::size_t, std::size_t, std::size_t, std::size_t> status_counts() const;
    const WindowEntry* get_window_entry(InstructionId id) const;
    uint64_t next_commit_id() const;
    std::vector<const WindowEntry*> get_window_entries() const;
    const DependencyTracker& dependency_tracker() const;

private:
    OoOEngine(CPUConfig config);

    CPUConfig config_;
    InstructionWindow window_;
    DependencyTracker dependency_tracker_;
    Scheduler scheduler_;
    uint64_t current_cycle_ = 0;
    uint64_t next_commit_id_ = 0;
    std::map<uint64_t, std::vector<InstructionId>> pending_completions_;
    std::vector<InstructionId> newly_ready_;
};

} // namespace arm_cpu
