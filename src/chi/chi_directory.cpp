/// @file chi_directory.cpp
/// @brief Directory and DirectoryEntry implementation.

#include "arm_cpu/chi/chi_directory.hpp"

namespace arm_cpu {

// =====================================================================
// DirectoryEntry
// =====================================================================

DirectoryEntry::DirectoryEntry(uint64_t addr)
    : addr(addr)
{}

bool DirectoryEntry::is_sharer(uint8_t node_id) const {
    return sharers.count(node_id) > 0;
}

void DirectoryEntry::add_sharer(uint8_t node_id) {
    sharers.insert(node_id);
    state = DirectoryState::Present;
}

void DirectoryEntry::remove_sharer(uint8_t node_id) {
    sharers.erase(node_id);
    if (sharers.empty()) {
        state = DirectoryState::NotPresent;
        owner = std::nullopt;
        dirty = false;
    } else if (owner.has_value() && owner.value() == node_id) {
        owner = std::nullopt;
    }
}

std::size_t DirectoryEntry::sharer_count() const {
    return sharers.size();
}

bool DirectoryEntry::is_shared() const {
    return sharers.size() > 1;
}

bool DirectoryEntry::is_unique() const {
    return sharers.size() == 1;
}

void DirectoryEntry::set_owner(uint8_t node_id) {
    owner = node_id;
    add_sharer(node_id);
}

std::vector<uint8_t> DirectoryEntry::other_sharers(uint8_t exclude) const {
    std::vector<uint8_t> result;
    for (auto id : sharers) {
        if (id != exclude) result.push_back(id);
    }
    return result;
}

void DirectoryEntry::start_transaction(ChiTxnId txn_id) {
    pending_txn = txn_id;
    state = DirectoryState::Processing;
}

void DirectoryEntry::complete_transaction() {
    pending_txn = std::nullopt;
    if (sharers.empty()) {
        state = DirectoryState::NotPresent;
    } else {
        state = DirectoryState::Present;
    }
}

void DirectoryEntry::invalidate_all() {
    sharers.clear();
    owner = std::nullopt;
    dirty = false;
    state = DirectoryState::NotPresent;
}

// =====================================================================
// Directory
// =====================================================================

Directory::Directory(std::size_t line_size, std::size_t max_entries)
    : line_size_(line_size), max_entries_(max_entries)
{}

uint64_t Directory::align_addr(uint64_t addr) const {
    uint64_t mask = ~(static_cast<uint64_t>(line_size_) - 1);
    return addr & mask;
}

const DirectoryEntry* Directory::lookup(uint64_t addr) const {
    auto it = entries_.find(align_addr(addr));
    return it != entries_.end() ? &it->second : nullptr;
}

DirectoryEntry* Directory::lookup_mut(uint64_t addr) {
    auto it = entries_.find(align_addr(addr));
    return it != entries_.end() ? &it->second : nullptr;
}

DirectoryEntry* Directory::get_or_create(uint64_t addr) {
    uint64_t aligned = align_addr(addr);
    stats_.lookups++;

    auto it = entries_.find(aligned);
    if (it == entries_.end()) {
        stats_.misses++;
        stats_.inserts++;
        if (entries_.size() >= max_entries_) {
            evict_oldest();
        }
        auto [ins_it, _] = entries_.emplace(aligned, DirectoryEntry(aligned));
        return &ins_it->second;
    }

    stats_.hits++;
    return &it->second;
}

void Directory::evict_oldest() {
    if (!entries_.empty()) {
        entries_.erase(entries_.begin());
        stats_.evictions++;
    }
}

bool Directory::is_present(uint64_t addr) const {
    auto it = entries_.find(align_addr(addr));
    return it != entries_.end() && !it->second.sharers.empty();
}

std::vector<uint8_t> Directory::get_sharers(uint64_t addr) const {
    auto it = entries_.find(align_addr(addr));
    if (it == entries_.end()) return {};
    return std::vector<uint8_t>(it->second.sharers.begin(), it->second.sharers.end());
}

void Directory::add_sharer(uint64_t addr, uint8_t node_id) {
    get_or_create(addr)->add_sharer(node_id);
}

void Directory::remove_sharer(uint64_t addr, uint8_t node_id) {
    uint64_t aligned = align_addr(addr);
    auto it = entries_.find(aligned);
    if (it != entries_.end()) {
        it->second.remove_sharer(node_id);
    }
}

std::vector<uint8_t> Directory::get_snoop_targets(uint64_t addr, std::optional<uint8_t> exclude) {
    uint64_t aligned = align_addr(addr);
    auto it = entries_.find(aligned);
    if (it == entries_.end()) return {};

    std::vector<uint8_t> targets;
    for (auto id : it->second.sharers) {
        if (exclude.has_value() && id == exclude.value()) continue;
        targets.push_back(id);
    }

    if (targets.size() > 1) {
        stats_.snoop_broadcasts++;
    } else if (targets.size() == 1) {
        stats_.single_sharer_snops++;
    }

    return targets;
}

void Directory::set_owner(uint64_t addr, uint8_t node_id) {
    get_or_create(addr)->set_owner(node_id);
}

std::optional<uint8_t> Directory::get_owner(uint64_t addr) const {
    auto it = entries_.find(align_addr(addr));
    return it != entries_.end() ? it->second.owner : std::nullopt;
}

void Directory::set_dirty(uint64_t addr, bool dirty) {
    auto* entry = lookup_mut(addr);
    if (entry) entry->dirty = dirty;
}

bool Directory::is_dirty(uint64_t addr) const {
    auto* entry = lookup(addr);
    return entry ? entry->dirty : false;
}

void Directory::start_transaction(uint64_t addr, ChiTxnId txn_id) {
    get_or_create(addr)->start_transaction(txn_id);
}

void Directory::complete_transaction(uint64_t addr) {
    uint64_t aligned = align_addr(addr);
    auto it = entries_.find(aligned);
    if (it != entries_.end()) {
        it->second.complete_transaction();
    }
}

void Directory::invalidate_all(uint64_t addr) {
    uint64_t aligned = align_addr(addr);
    auto it = entries_.find(aligned);
    if (it != entries_.end()) {
        it->second.invalidate_all();
    }
}

void Directory::on_evict(uint64_t addr, uint8_t node_id, bool is_dirty) {
    uint64_t aligned = align_addr(addr);
    auto it = entries_.find(aligned);
    if (it != entries_.end()) {
        it->second.remove_sharer(node_id);
        if (is_dirty) it->second.dirty = false;
        if (it->second.sharers.empty()) {
            it->second.state = DirectoryState::NotPresent;
            it->second.owner = std::nullopt;
        }
    }
}

const DirectoryStats& Directory::stats() const { return stats_; }
void Directory::reset_stats() { stats_ = DirectoryStats{}; }
void Directory::clear() { entries_.clear(); }
std::size_t Directory::entry_count() const { return entries_.size(); }

} // namespace arm_cpu
