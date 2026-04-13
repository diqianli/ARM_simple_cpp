#pragma once

/// @file parallel.hpp
/// @brief Parallel batch simulation support.

#include "arm_cpu/config.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <cstddef>
#include <vector>

namespace arm_cpu {

struct InstructionBatch {
    std::vector<Instruction> instructions;
    uint64_t batch_id = 0;
    uint64_t start_id = 0;

    InstructionBatch() = default;
    explicit InstructionBatch(uint64_t batch_id, std::size_t capacity);

    void push(Instruction instr);
    std::size_t len() const { return instructions.size(); }
    bool is_empty() const { return instructions.empty(); }
    bool is_full(std::size_t max_size) const { return instructions.size() >= max_size; }
    void clear() { instructions.clear(); }
};

struct BatchResult {
    uint64_t batch_id = 0;
    std::size_t instr_count = 0;
    uint64_t cycles = 0;
    double ipc = 0.0;
    uint64_t cache_misses = 0;
    uint64_t mem_ops = 0;
    uint64_t branch_ops = 0;
};

struct ParallelConfig {
    std::size_t num_workers = 1;
    std::size_t batch_size = 10000;
    bool parallel_deps = true;
    uint64_t stats_interval = 100000;

    static ParallelConfig default_config();
};

struct DependencyAnalysis {
    uint64_t instr_id = 0;
    std::vector<uint64_t> producers;
    bool is_memory_dep = false;
};

/// Analyze dependencies in a batch of instructions
std::vector<DependencyAnalysis> analyze_dependencies_batch(const std::vector<Instruction>& instructions);

/// Create batches from instruction vector
std::vector<InstructionBatch> create_batches(const std::vector<Instruction>& instructions, std::size_t batch_size);

class BatchSimulator {
public:
    explicit BatchSimulator(CPUConfig config);
    void set_parallel_config(ParallelConfig config);
    BatchResult process_batch(const InstructionBatch& batch);
    std::vector<BatchResult> process_batches(const std::vector<InstructionBatch>& batches);
    uint64_t total_cycles() const;
    uint64_t committed_count() const;
    double overall_ipc() const;

private:
    CPUConfig config_;
    ParallelConfig parallel_config_;
    uint64_t current_cycle_ = 0;
    uint64_t committed_count_ = 0;
};

} // namespace arm_cpu
