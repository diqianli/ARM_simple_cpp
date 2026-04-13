#pragma once

/// @file chi_directory.hpp
/// @brief Directory-based snoop filter for CHI protocol.
///        Tracks cache line sharing information across RN-F nodes.

#include "arm_cpu/chi/chi_protocol.hpp"

#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace arm_cpu {

// =====================================================================
// DirectoryState
// =====================================================================
enum class DirectoryState : uint8_t {
    NotPresent,
    Present,
    Processing,
};

// =====================================================================
// DirectoryEntry — tracking a cache line's sharing information
// =====================================================================
struct DirectoryEntry {
    uint64_t       addr = 0;
    std::unordered_set<uint8_t> sharers;
    DirectoryState state = DirectoryState::NotPresent;
    std::optional<ChiTxnId> pending_txn;
    std::optional<uint8_t>  owner;
    bool           dirty = false;

    explicit DirectoryEntry(uint64_t addr);

    bool       is_sharer(uint8_t node_id) const;
    void       add_sharer(uint8_t node_id);
    void       remove_sharer(uint8_t node_id);
    std::size_t sharer_count() const;
    bool       is_shared() const;
    bool       is_unique() const;
    void       set_owner(uint8_t node_id);
    std::vector<uint8_t> other_sharers(uint8_t exclude) const;
    void       start_transaction(ChiTxnId txn_id);
    void       complete_transaction();
    void       invalidate_all();
};

// =====================================================================
// DirectoryStats
// =====================================================================
struct DirectoryStats {
    uint64_t    lookups              = 0;
    uint64_t    hits                 = 0;
    uint64_t    misses               = 0;
    uint64_t    inserts              = 0;
    uint64_t    evictions            = 0;
    uint64_t    snoop_broadcasts     = 0;
    uint64_t    single_sharer_snops  = 0;
};

// =====================================================================
// Directory — snoop filter
// =====================================================================
class Directory {
public:
    Directory(std::size_t line_size, std::size_t max_entries);

    const DirectoryEntry* lookup(uint64_t addr) const;
    DirectoryEntry*       lookup_mut(uint64_t addr);
    DirectoryEntry*       get_or_create(uint64_t addr);

    bool              is_present(uint64_t addr) const;
    std::vector<uint8_t> get_sharers(uint64_t addr) const;
    void              add_sharer(uint64_t addr, uint8_t node_id);
    void              remove_sharer(uint64_t addr, uint8_t node_id);
    std::vector<uint8_t> get_snoop_targets(uint64_t addr, std::optional<uint8_t> exclude);

    void   set_owner(uint64_t addr, uint8_t node_id);
    std::optional<uint8_t> get_owner(uint64_t addr) const;
    void   set_dirty(uint64_t addr, bool dirty);
    bool   is_dirty(uint64_t addr) const;

    void   start_transaction(uint64_t addr, ChiTxnId txn_id);
    void   complete_transaction(uint64_t addr);
    void   invalidate_all(uint64_t addr);
    void   on_evict(uint64_t addr, uint8_t node_id, bool is_dirty);

    const DirectoryStats& stats() const;
    void reset_stats();
    void   clear();
    std::size_t entry_count() const;

private:
    uint64_t align_addr(uint64_t addr) const;
    void     evict_oldest();

    std::unordered_map<uint64_t, DirectoryEntry> entries_;
    std::size_t line_size_;
    std::size_t max_entries_;
    DirectoryStats stats_;
};

/// SnoopFilter is an alias for Directory.
using SnoopFilter = Directory;

} // namespace arm_cpu
