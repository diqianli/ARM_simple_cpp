#pragma once

/// @file stats_collector.hpp
/// @brief Statistics collector for the ARM CPU emulator.
///
/// Tracks instruction latencies, cache events, memory operations, and
/// provides the primary interface for recording simulation statistics.
///
/// Ported from Rust src/stats/mod.rs, src/stats/collector.rs, src/stats/trace_output.rs.

#include "arm_cpu/stats/performance_metrics.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace arm_cpu {

/// Hash functor for OpcodeType (needed for unordered_map)
struct OpcodeTypeHash {
    std::size_t operator()(OpcodeType op) const noexcept {
        return std::hash<uint16_t>{}(static_cast<uint16_t>(op));
    }
};

// =====================================================================
// StatsCacheInfo — internal cache statistics (for tracking during simulation)
// =====================================================================
struct StatsCacheInfo {
    uint64_t accesses = 0;
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t evictions = 0;

    double hit_rate() const;
    double miss_rate() const;
    double mpki(uint64_t total_instructions) const;
    void add_access(bool hit);
    void add_eviction();
};

// =====================================================================
// MemoryStats — memory access statistics
// =====================================================================
struct MemoryStats {
    uint64_t loads = 0;
    uint64_t stores = 0;
    uint64_t bytes_read = 0;
    uint64_t bytes_written = 0;
    double avg_load_latency = 0.0;
    double avg_store_latency = 0.0;

    void record_load(uint64_t bytes, uint64_t latency);
    void record_store(uint64_t bytes, uint64_t latency);
    uint64_t total_ops() const;
};

// =====================================================================
// PerformanceStats — aggregate performance statistics
// =====================================================================
struct PerformanceStats {
    uint64_t total_instructions = 0;
    uint64_t total_cycles = 0;
    std::unordered_map<OpcodeType, uint64_t, OpcodeTypeHash> instr_by_type;
    StatsCacheInfo l1_stats;
    StatsCacheInfo l2_stats;
    MemoryStats memory_stats;
    ExecutionMetrics exec_stats;

    double ipc() const;
    double cpi() const;
    void record_instruction(OpcodeType opcode_type);
    void record_cycles(uint64_t cycles);
    uint64_t instr_count(OpcodeType opcode_type) const;
    double instr_percentage(OpcodeType opcode_type) const;
    double memory_instr_percentage() const;
    double branch_instr_percentage() const;
    void reset();
    void merge(const PerformanceStats& other);
};

// =====================================================================
// TraceEntry — a single entry in the execution trace
// =====================================================================
struct TraceEntry {
    uint64_t id = 0;
    uint64_t pc = 0;
    std::string opcode;
    std::optional<std::string> disasm;
    uint64_t dispatch_cycle = 0;
    std::optional<uint64_t> issue_cycle;
    std::optional<uint64_t> complete_cycle;
    std::optional<uint64_t> commit_cycle;
    std::optional<uint64_t> exec_latency;
    std::optional<uint64_t> mem_addr;
    std::vector<uint16_t> src_regs;
    std::vector<uint16_t> dst_regs;

    TraceEntry() = default;
    TraceEntry(uint64_t id_, uint64_t pc_, OpcodeType opcode);

    /// Create from an Instruction
    static TraceEntry from_instruction(const Instruction& instr);

    /// Calculate total latency (dispatch to commit)
    std::optional<uint64_t> total_latency() const;
};

// =====================================================================
// TraceOutput — trace output manager
// =====================================================================
class TraceOutput {
public:
    explicit TraceOutput(std::size_t max_entries = 10000);

    /// Create a disabled trace output
    static TraceOutput disabled();

    void enable();
    void disable();
    bool is_enabled() const;

    void record_dispatch(const Instruction& instr, uint64_t cycle);
    void record_issue(InstructionId id, uint64_t cycle);
    void record_complete(InstructionId id, uint64_t cycle);
    void record_commit(InstructionId id, uint64_t cycle);

    const std::deque<TraceEntry>& entries() const;
    std::size_t len() const;
    bool is_empty() const;
    void clear();

    /// Write trace in text format to a string
    std::string write_text() const;

    /// Write trace in CSV format to a string
    std::string write_csv() const;

    /// Export trace as formatted string
    std::string to_string() const;

private:
    void add_entry(TraceEntry entry);
    TraceEntry* find_entry_mut(InstructionId id);

    std::deque<TraceEntry> entries_;
    std::size_t max_entries_;
    bool enabled_;
};

// =====================================================================
// StatsCollector — primary statistics collector
// =====================================================================
class StatsCollector {
public:
    StatsCollector();
    explicit StatsCollector(std::size_t max_history);

    /// Record instruction dispatch
    void record_dispatch(InstructionId id, uint64_t cycle);

    /// Record instruction issue
    void record_issue(InstructionId id, uint64_t cycle);

    /// Record instruction completion
    void record_complete(InstructionId id, uint64_t cycle);

    /// Record instruction commit
    void record_commit(const Instruction& instr, uint64_t cycle);

    /// Record cache access
    void record_l1_access(bool hit);
    void record_l1_eviction();
    void record_l2_access(bool hit);
    void record_l2_eviction();

    /// Record memory load/store
    void record_load(uint64_t bytes, uint64_t latency);
    void record_store(uint64_t bytes, uint64_t latency);

    /// Record cycle count
    void record_cycles(uint64_t cycles);

    /// Record IPC sample
    void record_ipc_sample(double ipc);

    /// Get current statistics
    const PerformanceStats& stats() const;
    PerformanceStats& stats_mut();

    /// Get latency statistics for an opcode type
    double avg_latency(OpcodeType opcode_type) const;
    uint64_t min_latency(OpcodeType opcode_type) const;
    uint64_t max_latency(OpcodeType opcode_type) const;

    /// IPC history
    const std::deque<double>& ipc_history() const;
    double avg_ipc() const;

    /// Get performance metrics summary
    PerformanceMetrics get_metrics() const;

    /// Reset all statistics
    void reset();

private:
    struct LatencyTracker {
        uint64_t count = 0;
        uint64_t total = 0;
        uint64_t min_val = 0;
        uint64_t max_val = 0;

        void record(uint64_t latency);
        double average() const;
    };

    struct InstrTiming {
        uint64_t dispatch_cycle = 0;
        std::optional<uint64_t> issue_cycle;
        std::optional<uint64_t> complete_cycle;
        std::optional<uint64_t> commit_cycle;

        std::optional<uint64_t> execution_latency() const;
        std::optional<uint64_t> total_latency() const;
    };

    PerformanceStats stats_;
    std::unordered_map<OpcodeType, LatencyTracker, OpcodeTypeHash> latencies_;
    std::unordered_map<InstructionId, InstrTiming, InstructionId::Hash> instr_timing_;
    std::deque<double> ipc_history_;
    std::size_t max_history_;
};

} // namespace arm_cpu
