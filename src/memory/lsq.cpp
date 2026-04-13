/// @file lsq.cpp
/// @brief Load/Store Queue implementation.

#include "arm_cpu/memory/lsq.hpp"

namespace arm_cpu {

LsqEntry LsqEntry::new_load(InstructionId id, uint64_t addr, uint8_t size, uint64_t issue_cycle) {
    return {id, addr, size, true, false, issue_cycle, std::nullopt};
}

LsqEntry LsqEntry::new_store(InstructionId id, uint64_t addr, uint8_t size, uint64_t issue_cycle) {
    return {id, addr, size, false, false, issue_cycle, std::nullopt};
}

LoadStoreQueue::LoadStoreQueue(std::size_t capacity, std::size_t load_pipelines, std::size_t store_pipelines)
    : capacity_(capacity), load_pipelines_(load_pipelines), store_pipelines_(store_pipelines) {}

bool LoadStoreQueue::has_space() const { return entries_.size() < capacity_; }
std::size_t LoadStoreQueue::free_slots() const { return capacity_ - entries_.size(); }

LsqHandle LoadStoreQueue::add_load(InstructionId id, uint64_t addr, uint8_t size) {
    entries_.push_back(LsqEntry::new_load(id, addr, size, current_cycle_));
    LsqHandle h{entries_.size() - 1};
    id_to_entry_[id] = h.index;
    return h;
}

LsqHandle LoadStoreQueue::add_store(InstructionId id, uint64_t addr, uint8_t size) {
    entries_.push_back(LsqEntry::new_store(id, addr, size, current_cycle_));
    LsqHandle h{entries_.size() - 1};
    id_to_entry_[id] = h.index;
    return h;
}

void LoadStoreQueue::complete(LsqHandle handle) {
    if (handle.index < entries_.size()) {
        auto& entry = entries_[handle.index];
        entry.completed = true;
        entry.complete_cycle = current_cycle_;
        if (entry.is_load) active_loads_ = active_loads_ > 0 ? active_loads_ - 1 : 0;
        else active_stores_ = active_stores_ > 0 ? active_stores_ - 1 : 0;
    }
}

std::optional<InstructionId> LoadStoreQueue::check_store_conflict(uint64_t addr, uint8_t size) const {
    auto end_addr = addr + size;
    for (const auto& entry : entries_) {
        if (!entry.is_load && !entry.completed) {
            auto entry_end = entry.addr + entry.size;
            if (addr < entry_end && end_addr > entry.addr) {
                return entry.instruction_id;
            }
        }
    }
    return std::nullopt;
}

std::vector<std::tuple<LsqHandle, InstructionId, uint64_t, bool>> LoadStoreQueue::get_ready() {
    std::vector<std::tuple<LsqHandle, InstructionId, uint64_t, bool>> ready;
    std::size_t loads_issued = 0, stores_issued = 0;

    for (std::size_t i = 0; i < entries_.size(); i++) {
        const auto& entry = entries_[i];
        if (entry.completed) continue;

        bool can_issue = entry.is_load
            ? (loads_issued < load_pipelines_ && active_loads_ < load_pipelines_)
            : (stores_issued < store_pipelines_ && active_stores_ < store_pipelines_);

        if (can_issue) {
            ready.emplace_back(LsqHandle{i}, entry.instruction_id, entry.addr, entry.is_load);
            if (entry.is_load) { loads_issued++; active_loads_++; }
            else { stores_issued++; active_stores_++; }
        }

        if (loads_issued >= load_pipelines_ && stores_issued >= store_pipelines_) break;
    }
    return ready;
}

std::vector<LsqEntry> LoadStoreQueue::retire_completed() {
    std::vector<LsqEntry> retired;
    while (!entries_.empty() && entries_.front().completed) {
        retired.push_back(entries_.front());
        id_to_entry_.erase(retired.back().instruction_id);
        entries_.pop_front();
    }
    // Rebuild index
    id_to_entry_.clear();
    for (std::size_t i = 0; i < entries_.size(); i++) {
        id_to_entry_[entries_[i].instruction_id] = i;
    }
    return retired;
}

std::size_t LoadStoreQueue::occupancy() const { return entries_.size(); }
bool LoadStoreQueue::is_empty() const { return entries_.empty(); }
void LoadStoreQueue::advance_cycle() { current_cycle_++; }
uint64_t LoadStoreQueue::current_cycle() const { return current_cycle_; }
void LoadStoreQueue::clear() { entries_.clear(); id_to_entry_.clear(); active_loads_ = 0; active_stores_ = 0; }

LsqStats LoadStoreQueue::get_stats() const {
    LsqStats stats;
    stats.capacity = capacity_;
    stats.occupancy = entries_.size();
    for (const auto& e : entries_) {
        if (e.is_load) stats.loads++; else stats.stores++;
        if (e.completed) stats.completed++;
    }
    stats.active_loads = active_loads_;
    stats.active_stores = active_stores_;
    return stats;
}

} // namespace arm_cpu
