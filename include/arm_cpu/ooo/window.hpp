#pragma once

/// @file window.hpp
/// @brief Instruction window for out-of-order execution.

#include "arm_cpu/types.hpp"

#include <cstdint>
#include <cstddef>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

namespace arm_cpu {

struct WindowEntry {
    Instruction instruction;
    InstrStatus status = InstrStatus::Waiting;
    uint64_t dispatch_cycle = 0;
    std::optional<uint64_t> issue_cycle;
    std::optional<uint64_t> complete_cycle;
    std::optional<uint64_t> decode_cycle;
    std::optional<uint64_t> rename_cycle;
    std::optional<uint64_t> retire_cycle;
    bool is_memory_op = false;
    bool completion_processed = false;

    WindowEntry() = default;
    WindowEntry(Instruction instr, uint64_t dispatch_cycle);

    const Instruction& instr() const { return instruction; }
    std::optional<uint64_t> execution_latency() const;
    void set_decode_cycle(uint64_t cycle) { decode_cycle = cycle; }
    void set_rename_cycle(uint64_t cycle) { rename_cycle = cycle; }
    void set_retire_cycle(uint64_t cycle) { retire_cycle = cycle; }
};

struct WindowStats {
    std::size_t capacity = 0;
    std::size_t occupancy = 0;
    std::size_t waiting = 0;
    std::size_t ready = 0;
    std::size_t executing = 0;
    std::size_t completed = 0;
    std::size_t committed = 0;
};

class InstructionWindow {
public:
    explicit InstructionWindow(std::size_t capacity);

    bool has_space() const;
    std::size_t free_slots() const;

    Result<void> insert(Instruction instr);
    const WindowEntry* get_entry(InstructionId id) const;
    WindowEntry* get_entry_mut(InstructionId id);

    void mark_ready(InstructionId id);
    void mark_executing(InstructionId id);
    void mark_completed(InstructionId id, uint64_t complete_cycle);
    void set_complete_cycle(InstructionId id, uint64_t complete_cycle);
    void set_status_completed(InstructionId id);
    void mark_completion_processed(InstructionId id);
    bool is_completion_processed(InstructionId id) const;

    const WindowEntry* get_entry_debug(InstructionId id) const;
    void set_issue_cycle(InstructionId id, uint64_t cycle);

    std::optional<WindowEntry> remove(InstructionId id);
    std::size_t len() const;
    bool is_empty() const;

    std::vector<InstructionId> get_ready_ids() const;
    std::vector<InstructionId> get_completed_ids() const;
    std::optional<InstructionId> oldest() const;

    std::tuple<std::size_t, std::size_t, std::size_t, std::size_t> status_counts() const;
    void clear();

    std::vector<const WindowEntry*> entries() const;
    WindowStats get_stats() const;

private:
    std::size_t capacity_;
    std::unordered_map<InstructionId, WindowEntry, InstructionId::Hash> entries_;
    std::deque<InstructionId> order_;
};

} // namespace arm_cpu
