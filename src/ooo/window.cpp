/// @file window.cpp
/// @brief Instruction window implementation.

#include "arm_cpu/ooo/window.hpp"

namespace arm_cpu {

WindowEntry::WindowEntry(Instruction instr, uint64_t dispatch_cycle)
    : instruction(std::move(instr))
    , status(InstrStatus::Waiting)
    , dispatch_cycle(dispatch_cycle)
    , is_memory_op(this->instruction.mem_access.has_value())
    , completion_processed(false)
{}

std::optional<uint64_t> WindowEntry::execution_latency() const {
    if (issue_cycle && complete_cycle) {
        return complete_cycle.value() - issue_cycle.value();
    }
    return std::nullopt;
}

InstructionWindow::InstructionWindow(std::size_t capacity)
    : capacity_(capacity)
{
    entries_.reserve(capacity);
}

bool InstructionWindow::has_space() const { return entries_.size() < capacity_; }
std::size_t InstructionWindow::free_slots() const { return capacity_ - entries_.size(); }

Result<void> InstructionWindow::insert(Instruction instr) {
    if (!has_space()) {
        return EmulatorError::internal("Instruction window is full");
    }
    auto id = instr.id;
    entries_.emplace(id, WindowEntry(std::move(instr), 0));
    order_.push_back(id);
    return {};
}

const WindowEntry* InstructionWindow::get_entry(InstructionId id) const {
    auto it = entries_.find(id);
    return (it != entries_.end()) ? &it->second : nullptr;
}

WindowEntry* InstructionWindow::get_entry_mut(InstructionId id) {
    auto it = entries_.find(id);
    return (it != entries_.end()) ? &it->second : nullptr;
}

void InstructionWindow::mark_ready(InstructionId id) {
    auto* e = get_entry_mut(id);
    if (e) e->status = InstrStatus::Ready;
}

void InstructionWindow::mark_executing(InstructionId id) {
    auto* e = get_entry_mut(id);
    if (e) e->status = InstrStatus::Executing;
}

void InstructionWindow::mark_completed(InstructionId id, uint64_t complete_cycle) {
    auto* e = get_entry_mut(id);
    if (e) e->complete_cycle = complete_cycle;
}

void InstructionWindow::set_complete_cycle(InstructionId id, uint64_t complete_cycle) {
    auto* e = get_entry_mut(id);
    if (e) e->complete_cycle = complete_cycle;
}

void InstructionWindow::set_status_completed(InstructionId id) {
    auto* e = get_entry_mut(id);
    if (e) e->status = InstrStatus::Completed;
}

void InstructionWindow::mark_completion_processed(InstructionId id) {
    auto* e = get_entry_mut(id);
    if (e) e->completion_processed = true;
}

bool InstructionWindow::is_completion_processed(InstructionId id) const {
    auto* e = get_entry(id);
    return e ? e->completion_processed : false;
}

const WindowEntry* InstructionWindow::get_entry_debug(InstructionId id) const {
    return get_entry(id);
}

void InstructionWindow::set_issue_cycle(InstructionId id, uint64_t cycle) {
    auto* e = get_entry_mut(id);
    if (e) e->issue_cycle = cycle;
}

std::optional<WindowEntry> InstructionWindow::remove(InstructionId id) {
    auto it = entries_.find(id);
    if (it == entries_.end()) return std::nullopt;
    auto entry = std::move(it->second);
    entries_.erase(it);
    order_.erase(std::remove(order_.begin(), order_.end(), id), order_.end());
    return entry;
}

std::size_t InstructionWindow::len() const { return entries_.size(); }
bool InstructionWindow::is_empty() const { return entries_.empty(); }

std::vector<InstructionId> InstructionWindow::get_ready_ids() const {
    std::vector<InstructionId> ids;
    for (auto id : order_) {
        auto* e = get_entry(id);
        if (e && e->status == InstrStatus::Ready) ids.push_back(id);
    }
    return ids;
}

std::vector<InstructionId> InstructionWindow::get_completed_ids() const {
    std::vector<InstructionId> ids;
    for (auto id : order_) {
        auto* e = get_entry(id);
        if (e && e->status == InstrStatus::Completed) ids.push_back(id);
    }
    return ids;
}

std::optional<InstructionId> InstructionWindow::oldest() const {
    if (order_.empty()) return std::nullopt;
    return order_.front();
}

std::tuple<std::size_t, std::size_t, std::size_t, std::size_t>
InstructionWindow::status_counts() const {
    std::size_t waiting = 0, ready = 0, executing = 0, completed = 0;
    for (const auto& [_, entry] : entries_) {
        switch (entry.status) {
            case InstrStatus::Waiting: waiting++; break;
            case InstrStatus::Ready: ready++; break;
            case InstrStatus::Executing: executing++; break;
            case InstrStatus::Completed: case InstrStatus::Committed: completed++; break;
        }
    }
    return {waiting, ready, executing, completed};
}

void InstructionWindow::clear() {
    entries_.clear();
    order_.clear();
}

std::vector<const WindowEntry*> InstructionWindow::entries() const {
    std::vector<const WindowEntry*> result;
    result.reserve(entries_.size());
    for (const auto& [_, entry] : entries_) {
        result.push_back(&entry);
    }
    return result;
}

WindowStats InstructionWindow::get_stats() const {
    WindowStats stats;
    stats.capacity = capacity_;
    stats.occupancy = entries_.size();
    for (const auto& [_, entry] : entries_) {
        switch (entry.status) {
            case InstrStatus::Waiting: stats.waiting++; break;
            case InstrStatus::Ready: stats.ready++; break;
            case InstrStatus::Executing: stats.executing++; break;
            case InstrStatus::Completed: stats.completed++; break;
            case InstrStatus::Committed: stats.committed++; break;
        }
    }
    return stats;
}

} // namespace arm_cpu
