#pragma once

/// @file cpu.hpp
/// @brief Top-level CPU emulator integration.
///
/// Combines out-of-order execution, memory subsystem, CHI interface,
/// statistics collection, and visualization into a single simulation
/// entry point.
///
/// Ported from Rust src/cpu.rs.

#include "arm_cpu/config.hpp"
#include "arm_cpu/stats/stats_collector.hpp"
#include "arm_cpu/visualization/visualization_state.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace arm_cpu {

// Forward declarations
class OoOEngine;
class MemorySubsystem;
class ChiManager;
class PipelineTracker;
class VisualizationState;
class PipelineTrackerViz;

/// Main CPU emulator.
///
/// Orchestrates the out-of-order engine, memory subsystem, CHI manager,
/// statistics collector, trace output, and visualization for a single
/// CPU simulation.
class CPUEmulator {
public:
    /// Create a new CPU emulator with the given configuration.
    static Result<std::unique_ptr<CPUEmulator>> create(CPUConfig config);

    /// Create a new CPU emulator with visualization enabled.
    static Result<std::unique_ptr<CPUEmulator>> create_with_visualization(
        CPUConfig config, VisualizationConfig viz_config);

    /// Create with default configuration.
    static Result<std::unique_ptr<CPUEmulator>> create_with_defaults();

    /// Get configuration.
    const CPUConfig& config() const { return config_; }

    /// Get current cycle.
    uint64_t current_cycle() const { return current_cycle_; }

    /// Get committed instruction count.
    uint64_t committed_count() const { return committed_count_; }

    /// Run simulation with an instruction source functor.
    Result<PerformanceMetrics> run(
        std::function<std::optional<Result<Instruction>>()> next_instr);

    /// Run simulation with a cycle limit (to prevent infinite loops).
    Result<PerformanceMetrics> run_with_limit(
        std::function<std::optional<Result<Instruction>>()> next_instr,
        uint64_t max_cycles);

    /// Run for a specific number of cycles (no instruction source needed).
    void run_cycles(uint64_t cycles);

    /// Single step the simulation.
    void step();

    /// Dispatch an instruction directly.
    Result<void> dispatch(Instruction instr);

    /// Stop the simulation.
    void stop();

    /// Reset the emulator to initial state.
    void reset();

    /// Get performance metrics.
    PerformanceMetrics get_metrics() const;

    /// Get statistics collector (const).
    const StatsCollector& stats() const { return stats_; }

    /// Get mutable statistics collector.
    StatsCollector& stats_mut() { return stats_; }

    /// Get trace output (const).
    const TraceOutput& trace() const { return trace_; }

    /// Get mutable trace output.
    TraceOutput& trace_mut() { return trace_; }

    /// Get memory subsystem (const).
    const MemorySubsystem& memory() const { return *memory_; }

    /// Get OoO engine (const).
    const OoOEngine& ooo_engine() const { return *ooo_engine_; }

    /// Print summary to stdout.
    void print_summary() const;

    /// Get visualization state (const).
    const VisualizationState& visualization() const { return *visualization_; }

    /// Get mutable visualization state.
    VisualizationState& visualization_mut() { return *visualization_; }

    /// Get pipeline tracker for Konata visualization (const).
    const PipelineTracker& pipeline_tracker() const { return *pipeline_tracker_; }

    /// Get mutable pipeline tracker.
    PipelineTracker& pipeline_tracker_mut() { return *pipeline_tracker_; }

    ~CPUEmulator(); // Defined in cpu.cpp (needs complete types for unique_ptr members)

private:
    CPUEmulator(CPUConfig config,
                std::unique_ptr<OoOEngine> ooo_engine,
                std::unique_ptr<MemorySubsystem> memory,
                std::unique_ptr<ChiManager> chi_manager,
                StatsCollector stats,
                TraceOutput trace,
                std::unique_ptr<VisualizationState> visualization,
                std::unique_ptr<PipelineTracker> pipeline_tracker);

    void fetch_dispatch(
        std::function<std::optional<Result<Instruction>>()> next_instr);
    Result<void> execute();
    Result<void> handle_memory_op(InstructionId id, const MemAccess& access);
    Result<void> complete_memory();
    Result<void> commit();
    void advance_cycle();
    bool should_stop() const;

    CPUConfig config_;
    std::unique_ptr<OoOEngine> ooo_engine_;
    std::unique_ptr<MemorySubsystem> memory_;
    std::unique_ptr<ChiManager> chi_manager_;
    StatsCollector stats_;
    TraceOutput trace_;
    std::unique_ptr<VisualizationState> visualization_;
    std::unique_ptr<PipelineTracker> pipeline_tracker_;
    uint64_t current_cycle_ = 0;
    uint64_t committed_count_ = 0;
    bool running_ = false;
};

} // namespace arm_cpu
