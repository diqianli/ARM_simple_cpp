#pragma once

/// @file konata_writer.hpp
/// @brief Konata-compatible JSON format generator for pipeline visualization.
///
/// Generates Konata-compatible JSON output for visualizing instruction
/// pipeline execution. The output is a JSON file containing an array of
/// operations, each with pipeline stage timing, dependencies, register
/// info, and memory addresses.
///
/// Analogous to Rust's `output::konata` module.

#include "arm_cpu/output/output_sink.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace arm_cpu {

// =====================================================================
// KonataConfig
// =====================================================================

/// Configuration for Konata output.
struct KonataConfig {
    /// Include register information.
    bool include_registers = true;
    /// Include memory addresses.
    bool include_memory = true;
    /// Include dependencies.
    bool include_dependencies = true;
    /// Minimum stage duration to include (0 = all).
    uint64_t min_stage_duration = 0;
    /// Pretty-print JSON output.
    bool pretty_print = true;

    /// Default configuration with all details.
    static KonataConfig default_config() { return KonataConfig{}; }

    /// Minimal config for small output.
    static KonataConfig minimal() {
        return {
            false, false, false, 1, false
        };
    }

    /// Full config with all details.
    static KonataConfig full() {
        return KonataConfig{};
    }
};

// =====================================================================
// KonataStage
// =====================================================================

/// A single pipeline stage in Konata format.
struct KonataStage {
    /// Stage name (F, Dc, Rn, Ds, Is, Ex, Me, Cm, Rt).
    std::string name;
    /// Start cycle.
    uint64_t start_cycle = 0;
    /// End cycle.
    uint64_t end_cycle = 0;

    /// Create a new stage (ensures end >= start).
    static KonataStage make(std::string name, uint64_t start, uint64_t end) {
        return {std::move(name), start, std::max(end, start)};
    }

    /// Get the duration of this stage.
    uint64_t duration() const noexcept {
        return end_cycle >= start_cycle ? end_cycle - start_cycle : 0;
    }
};

// =====================================================================
// KonataDependency
// =====================================================================

/// Dependency reference in Konata format.
struct KonataDependency {
    /// Producer instruction ID.
    uint64_t producer_id = 0;
    /// Dependency type ("reg" or "mem").
    std::string dep_type;

    /// Create a register dependency.
    static KonataDependency register_dep(uint64_t producer_id) {
        return {producer_id, "reg"};
    }

    /// Create a memory dependency.
    static KonataDependency memory_dep(uint64_t producer_id) {
        return {producer_id, "mem"};
    }
};

// =====================================================================
// KonataLane
// =====================================================================

/// A lane containing stages.
struct KonataLane {
    /// Lane name.
    std::string name;
    /// Stages in this lane.
    std::vector<KonataStage> stages;
};

// =====================================================================
// KonataOp
// =====================================================================

/// A single operation (instruction) in Konata format.
struct KonataOp {
    /// Visualization ID (sequential).
    uint64_t id = 0;
    /// Program ID (instruction ID).
    uint64_t gid = 0;
    /// Retire ID (retire order).
    std::optional<uint64_t> rid;
    /// Program counter.
    uint64_t pc = 0;
    /// Disassembly text.
    std::string label_name;
    /// Pipeline lanes.
    std::vector<KonataLane> lanes;
    /// Dependencies.
    std::vector<KonataDependency> prods;
    /// Source registers.
    std::vector<uint16_t> src_regs;
    /// Destination registers.
    std::vector<uint16_t> dst_regs;
    /// Whether this is a memory operation.
    std::optional<bool> is_memory;
    /// Memory address (if memory op).
    std::optional<uint64_t> mem_addr;
    /// Fetched cycle.
    std::optional<uint64_t> fetched_cycle;
    /// Retired cycle.
    std::optional<uint64_t> retired_cycle;

    /// Create a new Konata operation.
    static KonataOp make(uint64_t id_, uint64_t gid_, uint64_t pc_, std::string label_name_) {
        KonataOp op;
        op.id = id_;
        op.gid = gid_;
        op.pc = pc_;
        op.label_name = std::move(label_name_);
        return op;
    }

    /// Add a stage to the main lane.
    void add_stage(const std::string& name, uint64_t start, uint64_t end);

    /// Add a dependency.
    void add_dependency(KonataDependency dep);

    /// Set register info.
    void set_registers(std::vector<uint16_t> src, std::vector<uint16_t> dst);

    /// Set memory info.
    void set_memory(uint64_t addr);

    /// Serialize to nlohmann::json.
    nlohmann::json to_json() const;
};

// =====================================================================
// KonataOutput
// =====================================================================

/// Complete Konata output file.
struct KonataOutput {
    /// Format version.
    std::string version = "1.0";
    /// Total simulation cycles.
    uint64_t total_cycles = 0;
    /// Total committed instructions.
    uint64_t total_instructions = 0;
    /// Number of operations.
    std::size_t ops_count = 0;
    /// Operations.
    std::vector<KonataOp> ops;

    /// Create a new empty output.
    static KonataOutput make() { return KonataOutput{}; }

    /// Add an operation.
    void add_op(KonataOp op);

    /// Set summary info.
    void set_summary(uint64_t cycles, uint64_t instructions);

    /// Convert to JSON string.
    std::string to_json(bool pretty) const;

    /// Write to a file.
    bool write_to_file(const std::string& path, bool pretty) const;
};

// =====================================================================
// KonataWriter
// =====================================================================

/// Event types for feeding into the KonataWriter.
/// These mirror the Rust SimulationEvent variants used by KonataWriter.
enum class KonataEventType : uint8_t {
    InstructionFetch,
    InstructionDispatch,
    InstructionIssue,
    InstructionExecuteEnd,
    MemoryComplete,
    InstructionComplete,
    InstructionRetire,
    MemoryAccess,
    Dependency,
    CycleBoundary,
    SimulationEnd,
};

/// A lightweight event structure for feeding simulation data to KonataWriter.
struct KonataEvent {
    KonataEventType type;
    uint64_t cycle = 0;
    InstructionId id;
    Instruction instruction;  // Only used for InstructionFetch
    uint64_t retire_order = 0;
    uint64_t addr = 0;
    InstructionId producer_id;
    bool is_memory_dep = false;
};

/// Konata writer that collects simulation events and produces JSON output.
///
/// Usage:
///   KonataWriter writer;
///   writer.handle_event(KonataEvent{...});
///   writer.write_to_file("output.json");
class KonataWriter {
public:
    /// Create a new Konata writer with default config.
    KonataWriter();

    /// Create with custom config.
    explicit KonataWriter(KonataConfig config);

    /// Handle a simulation event.
    void handle_event(const KonataEvent& event);

    /// Get the output structure.
    KonataOutput get_output() const;

    /// Write output to a file.
    bool write_to_file(const std::string& path) const;

    /// Clear all collected data.
    void clear();

    /// Get the number of collected operations.
    std::size_t size() const noexcept { return ops_.size(); }

    /// Check if empty.
    bool empty() const noexcept { return ops_.empty(); }

private:
    KonataConfig config_;
    std::vector<KonataOp> ops_;
    uint64_t current_cycle_ = 0;
    uint64_t committed_count_ = 0;
    std::unordered_map<uint64_t, std::size_t> id_map_;

    /// Helper to find the end cycle of the last stage in the main lane.
    std::optional<uint64_t> last_stage_end(std::size_t op_idx) const;
};

} // namespace arm_cpu
