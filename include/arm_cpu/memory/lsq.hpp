#pragma once

/// @file lsq.hpp
/// @brief Load/Store Queue implementation.

#include "arm_cpu/types.hpp"

#include <cstdint>
#include <cstddef>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

namespace arm_cpu {

struct LsqEntry {
    InstructionId instruction_id;
    uint64_t addr;
    uint8_t size;
    bool is_load;
    bool completed = false;
    uint64_t issue_cycle;
    std::optional<uint64_t> complete_cycle;

    static LsqEntry new_load(InstructionId id, uint64_t addr, uint8_t size, uint64_t issue_cycle);
    static LsqEntry new_store(InstructionId id, uint64_t addr, uint8_t size, uint64_t issue_cycle);
};

struct LsqHandle { std::size_t index; };

struct LsqStats {
    std::size_t capacity = 0;
    std::size_t occupancy = 0;
    std::size_t loads = 0;
    std::size_t stores = 0;
    std::size_t completed = 0;
    std::size_t active_loads = 0;
    std::size_t active_stores = 0;
};

class LoadStoreQueue {
public:
    LoadStoreQueue(std::size_t capacity, std::size_t load_pipelines, std::size_t store_pipelines);

    bool has_space() const;
    std::size_t free_slots() const;
    LsqHandle add_load(InstructionId id, uint64_t addr, uint8_t size);
    LsqHandle add_store(InstructionId id, uint64_t addr, uint8_t size);
    void complete(LsqHandle handle);
    std::optional<InstructionId> check_store_conflict(uint64_t addr, uint8_t size) const;
    std::vector<std::tuple<LsqHandle, InstructionId, uint64_t, bool>> get_ready();
    std::vector<LsqEntry> retire_completed();
    std::size_t occupancy() const;
    bool is_empty() const;
    void advance_cycle();
    uint64_t current_cycle() const;
    void clear();
    LsqStats get_stats() const;

private:
    std::size_t capacity_;
    std::size_t load_pipelines_;
    std::size_t store_pipelines_;
    std::deque<LsqEntry> entries_;
    std::unordered_map<InstructionId, std::size_t, InstructionId::Hash> id_to_entry_;
    std::size_t active_loads_ = 0;
    std::size_t active_stores_ = 0;
    uint64_t current_cycle_ = 0;
};

} // namespace arm_cpu
