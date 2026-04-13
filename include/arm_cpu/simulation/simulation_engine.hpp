#pragma once

/// @file simulation_engine.hpp
/// @brief Core simulation engine with event-based output.
///
/// The simulation engine executes instructions and emits events that can
/// be consumed by various output sinks (Konata, custom formats, etc.).
///
/// Ported from Rust src/simulation/engine.rs and src/simulation/event.rs.

#include "arm_cpu/config.hpp"
#include "arm_cpu/simulation/pipeline_tracker.hpp"
#include "arm_cpu/stats/stats_collector.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace arm_cpu {

// Forward declarations
class OoOEngine;
class MemorySubsystem;
class PipelineTracker;

// =====================================================================
// SimExecutionUnit — execution unit types for simulation events
// =====================================================================
enum class SimExecutionUnit : uint8_t {
    IntAlu,
    IntMul,
    IntDiv,
    Load,
    Store,
    Branch,
    Fp,
    Simd,
    Crypto,
    System,
};

/// Determine the execution unit for an instruction
SimExecutionUnit sim_execution_unit_from_opcode(OpcodeType opcode);

/// Get a human-readable name for an execution unit
const char* sim_execution_unit_name(SimExecutionUnit unit);

// =====================================================================
// SimulationEvent — events emitted during simulation
// =====================================================================
struct SimulationEvent {
    enum class Kind : uint8_t {
        InstructionFetch,
        InstructionDispatch,
        InstructionDecode,
        InstructionRename,
        InstructionIssue,
        InstructionExecuteStart,
        InstructionExecuteEnd,
        MemoryAccess,
        MemoryComplete,
        InstructionComplete,
        InstructionRetire,
        Dependency,
        BranchPrediction,
        CycleBoundary,
        SimulationStart,
        SimulationEnd,
    };

    Kind kind;

    // InstructionFetch
    Instruction instr;
    uint64_t fetch_cycle = 0;

    // InstructionDispatch / Decode / Rename / Issue / ExecuteStart / ExecuteEnd
    InstructionId event_id;
    uint64_t event_cycle = 0;

    // InstructionIssue
    SimExecutionUnit issue_unit = SimExecutionUnit::IntAlu;

    // MemoryAccess
    uint64_t mem_addr = 0;
    uint8_t mem_size = 0;
    bool mem_is_load = false;
    uint64_t mem_latency = 0;
    uint8_t mem_hit_level = 0;

    // InstructionRetire
    uint64_t retire_order = 0;

    // Dependency
    InstructionId dep_producer;
    bool dep_is_memory = false;

    // BranchPrediction
    uint64_t predicted_target = 0;
    bool prediction_correct = false;

    // CycleBoundary
    uint64_t boundary_committed_count = 0;

    // SimulationEnd
    uint64_t end_total_committed = 0;

    // Factory methods
    static SimulationEvent instruction_fetch(const Instruction& instr, uint64_t cycle);
    static SimulationEvent instruction_dispatch(InstructionId id, uint64_t cycle);
    static SimulationEvent instruction_decode(InstructionId id, uint64_t cycle);
    static SimulationEvent instruction_rename(InstructionId id, uint64_t cycle);
    static SimulationEvent instruction_issue(InstructionId id, uint64_t cycle, SimExecutionUnit unit);
    static SimulationEvent instruction_execute_start(InstructionId id, uint64_t cycle);
    static SimulationEvent instruction_execute_end(InstructionId id, uint64_t cycle);
    static SimulationEvent memory_access(InstructionId id, uint64_t addr, uint8_t size,
                                         bool is_load, uint64_t latency, uint8_t hit_level);
    static SimulationEvent memory_complete(InstructionId id, uint64_t cycle);
    static SimulationEvent instruction_complete(InstructionId id, uint64_t cycle);
    static SimulationEvent instruction_retire(InstructionId id, uint64_t cycle, uint64_t retire_order);
    static SimulationEvent dependency(InstructionId consumer, InstructionId producer, bool is_memory);
    static SimulationEvent branch_prediction(InstructionId id, uint64_t target, bool correct);
    static SimulationEvent cycle_boundary(uint64_t cycle, uint64_t committed_count);
    static SimulationEvent simulation_start(uint64_t start_cycle);
    static SimulationEvent simulation_end(uint64_t end_cycle, uint64_t total_committed);

    /// Get a human-readable name for the event kind
    const char* kind_name() const;
};

// =====================================================================
// SimulationEventSink — trait for consuming simulation events
// =====================================================================
class SimulationEventSink {
public:
    virtual ~SimulationEventSink() = default;

    /// Handle a simulation event
    virtual void on_event(const SimulationEvent& event) = 0;

    /// Flush any buffered output
    virtual void flush() {}

    /// Get the name of this sink for debugging
    virtual const char* name() const = 0;
};

// =====================================================================
// EventLogger — simple event logger for debugging
// =====================================================================
class EventLogger : public SimulationEventSink {
public:
    explicit EventLogger(bool verbose = false);

    void on_event(const SimulationEvent& event) override;
    void flush() override {}
    const char* name() const override { return "EventLogger"; }

    uint64_t event_count() const { return event_count_; }

private:
    bool verbose_;
    uint64_t event_count_ = 0;
};

// =====================================================================
// NullSink — discards all events
// =====================================================================
class NullSink : public SimulationEventSink {
public:
    void on_event(const SimulationEvent& event) override { (void)event; }
    const char* name() const override { return "NullSink"; }
};

// =====================================================================
// MultiSink — dispatches events to multiple sinks
// =====================================================================
class MultiSink {
public:
    MultiSink() = default;

    void add_sink(std::shared_ptr<std::mutex> mtx, std::shared_ptr<SimulationEventSink> sink);
    void dispatch(const SimulationEvent& event);
    void flush_all();

private:
    struct SinkEntry {
        std::shared_ptr<std::mutex> mutex;
        std::shared_ptr<SimulationEventSink> sink;
    };
    std::vector<SinkEntry> sinks_;
};

// =====================================================================
// SimulationEngine
// =====================================================================
class SimulationEngine {
public:
    /// Create a new simulation engine
    static Result<std::unique_ptr<SimulationEngine>> create(CPUConfig config);

    /// Add an event sink
    void add_event_sink(std::shared_ptr<std::mutex> mtx, std::shared_ptr<SimulationEventSink> sink);

    /// Remove all event sinks
    void clear_event_sinks();

    /// Get configuration
    const CPUConfig& config() const;

    /// Get current cycle
    uint64_t current_cycle() const;

    /// Get committed instruction count
    uint64_t committed_count() const;

    /// Get the pipeline tracker
    const PipelineTracker& pipeline_tracker() const;

    /// Get mutable pipeline tracker
    PipelineTracker& pipeline_tracker_mut();

    /// Dispatch an instruction directly
    Result<void> dispatch(Instruction instr);

    /// Run simulation until source is exhausted (uses a functor)
    Result<PerformanceMetrics> run(std::function<std::optional<Result<Instruction>>()> next_instr);

    /// Run simulation with a cycle limit
    Result<PerformanceMetrics> run_with_limit(std::function<std::optional<Result<Instruction>>()> next_instr,
                                               uint64_t max_cycles);

    /// Stop the simulation
    void stop();

    /// Reset the emulator
    Result<void> reset();

    /// Get performance metrics
    PerformanceMetrics get_metrics() const;

    /// Get statistics collector
    const StatsCollector& stats() const;
    StatsCollector& stats_mut();

    /// Get trace output
    const TraceOutput& trace() const;
    TraceOutput& trace_mut();

    /// Get memory subsystem
    const MemorySubsystem& memory() const;

    /// Get OoO engine
    const OoOEngine& ooo_engine() const;

    /// Print summary to stdout
    void print_summary() const;

private:
    SimulationEngine(CPUConfig config,
                     std::unique_ptr<OoOEngine> ooo_engine,
                     std::unique_ptr<MemorySubsystem> memory,
                     StatsCollector stats,
                     TraceOutput trace,
                     PipelineTracker tracker);

    void emit_event(const SimulationEvent& event);
    void fetch_dispatch(std::function<std::optional<Result<Instruction>>()> next_instr);
    Result<void> execute();
    Result<void> handle_memory_op(InstructionId id, const MemAccess& access);
    Result<void> complete_memory();
    Result<void> commit();
    void advance_cycle();
    bool should_stop() const;

    CPUConfig config_;
    std::unique_ptr<OoOEngine> ooo_engine_;
    std::unique_ptr<MemorySubsystem> memory_;
    std::unique_ptr<PipelineTracker> pipeline_tracker_;
    StatsCollector stats_;
    TraceOutput trace_;
    MultiSink event_sinks_;
    uint64_t current_cycle_ = 0;
    uint64_t committed_count_ = 0;
    bool running_ = false;
};

} // namespace arm_cpu
