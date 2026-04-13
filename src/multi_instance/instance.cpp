/// @file instance.cpp
/// @brief Simulation instance implementation.
///
/// Ported from Rust src/multi_instance/instance.rs.

#include "arm_cpu/multi_instance/instance.hpp"
#include "arm_cpu/cpu.hpp"

#include <chrono>
#include <format>
#include <memory>
#include <stdexcept>

namespace arm_cpu {

// =====================================================================
// InstanceId
// =====================================================================

std::string InstanceId::to_string() const {
    return std::format("instance-{}", value);
}

// =====================================================================
// SimulationInstance
// =====================================================================

SimulationInstance::SimulationInstance(InstanceId id, CPUConfig config,
                                       std::unique_ptr<CPUEmulator> emulator)
    : id_(id)
    , config_(std::move(config))
    , emulator_(std::move(emulator))
    , state_(InstanceState::Idle)
    , stats_()
    , instruction_counter_(0)
{}

std::unique_ptr<SimulationInstance> SimulationInstance::create(InstanceId id, CPUConfig config) {
    auto emulator = CPUEmulator::create(config);
    if (!emulator.ok()) {
        throw std::runtime_error("Failed to create CPU emulator: " + emulator.error().message());
    }
    return std::unique_ptr<SimulationInstance>(new SimulationInstance(id, std::move(config), std::move(emulator.value())));
}

// Destructor defined here where CPUEmulator is a complete type
SimulationInstance::~SimulationInstance() = default;

uint64_t SimulationInstance::next_instruction_id() {
    return instruction_counter_.fetch_add(1, std::memory_order_seq_cst);
}

Result<InstanceResult> SimulationInstance::run_cycles(uint64_t max_cycles) {
    state_ = InstanceState::Running;

    auto start_time = std::chrono::steady_clock::now();

    // Run for the specified number of cycles
    emulator_->run_cycles(max_cycles);

    // Get metrics from emulator
    PerformanceMetrics perf = emulator_->get_metrics();
    stats_.cycles = emulator_->current_cycle();
    stats_.instructions_retired = emulator_->committed_count();

    auto end_time = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();

    InstanceResult result;
    result.instance_id = id_;
    result.metrics.perf = perf;
    result.metrics.execution_time_ms = static_cast<uint64_t>(duration_ms);
    result.stats = stats_;
    result.trace_path = std::nullopt;
    result.error = std::nullopt;

    state_ = InstanceState::Completed;
    return result;
}

void SimulationInstance::reset() {
    state_ = InstanceState::Idle;
    stats_ = InstanceStats{};
    emulator_->reset();
    instruction_counter_.store(0, std::memory_order_seq_cst);
}

} // namespace arm_cpu
