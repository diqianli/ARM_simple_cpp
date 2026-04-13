/// @file parallel.cpp
/// @brief Parallel batch simulation implementation.

#include "arm_cpu/ooo/parallel.hpp"

#include <algorithm>
#include <thread>

namespace arm_cpu {

InstructionBatch::InstructionBatch(uint64_t batch_id, std::size_t capacity)
    : batch_id(batch_id)
{
    instructions.reserve(capacity);
}

void InstructionBatch::push(Instruction instr) {
    if (instructions.empty()) start_id = instr.id.value;
    instructions.push_back(std::move(instr));
}

ParallelConfig ParallelConfig::default_config() {
    return {
        std::max(1u, std::thread::hardware_concurrency()),
        10000,
        true,
        100000,
    };
}

std::vector<DependencyAnalysis>
analyze_dependencies_batch(const std::vector<Instruction>& instructions) {
    std::unordered_map<Reg, uint64_t, Reg::Hash> last_writer;
    std::optional<uint64_t> last_mem_writer;
    std::vector<DependencyAnalysis> analyses;
    analyses.reserve(instructions.size());

    for (const auto& instr : instructions) {
        DependencyAnalysis analysis;
        analysis.instr_id = instr.id.value;

        for (const auto& reg : instr.src_regs) {
            auto it = last_writer.find(reg);
            if (it != last_writer.end()) {
                analysis.producers.push_back(it->second);
            }
        }

        if (instr.mem_access.has_value()) {
            analysis.is_memory_dep = true;
            if (last_mem_writer.has_value()) {
                if (std::find(analysis.producers.begin(), analysis.producers.end(), *last_mem_writer)
                    == analysis.producers.end()) {
                    analysis.producers.push_back(*last_mem_writer);
                }
            }
        }

        for (const auto& reg : instr.dst_regs) {
            last_writer[reg] = instr.id.value;
        }

        if (instr.mem_access.has_value() && !instr.mem_access->is_load) {
            last_mem_writer = instr.id.value;
        }

        analyses.push_back(std::move(analysis));
    }
    return analyses;
}

std::vector<InstructionBatch>
create_batches(const std::vector<Instruction>& instructions, std::size_t batch_size) {
    std::vector<InstructionBatch> batches;
    uint64_t batch_id = 0;
    InstructionBatch current(batch_id, batch_size);

    for (const auto& instr : instructions) {
        current.push(instr);
        if (current.len() >= batch_size) {
            batches.push_back(std::move(current));
            batch_id++;
            current = InstructionBatch(batch_id, batch_size);
        }
    }
    if (!current.is_empty()) {
        batches.push_back(std::move(current));
    }
    return batches;
}

BatchSimulator::BatchSimulator(CPUConfig config)
    : config_(std::move(config))
    , parallel_config_(ParallelConfig::default_config())
{}

void BatchSimulator::set_parallel_config(ParallelConfig config) {
    parallel_config_ = std::move(config);
}

BatchResult BatchSimulator::process_batch(const InstructionBatch& batch) {
    BatchResult result;
    result.batch_id = batch.batch_id;

    auto deps = analyze_dependencies_batch(batch.instructions);
    auto issue_width = config_.issue_width;
    std::size_t issued_this_cycle = 0;

    for (std::size_t i = 0; i < batch.instructions.size(); i++) {
        const auto& instr = batch.instructions[i];
        const auto& dep = deps[i];

        result.instr_count++;
        if (is_memory_op(instr.opcode_type)) result.mem_ops++;
        if (is_branch(instr.opcode_type)) result.branch_ops++;

        auto base_latency = instr.instr_latency();
        auto dep_stall = dep.producers.empty() ? 0u : dep.producers.size() * 2u;

        if (issued_this_cycle >= issue_width) {
            result.cycles++;
            issued_this_cycle = 0;
        }
        issued_this_cycle++;

        result.cycles += base_latency + dep_stall;
    }

    if (result.cycles > 0) {
        result.ipc = static_cast<double>(result.instr_count) / static_cast<double>(result.cycles);
    }

    current_cycle_ += result.cycles;
    committed_count_ += result.instr_count;

    return result;
}

std::vector<BatchResult>
BatchSimulator::process_batches(const std::vector<InstructionBatch>& batches) {
    std::vector<BatchResult> results;
    results.reserve(batches.size());
    for (const auto& batch : batches) {
        results.push_back(process_batch(batch));
    }
    return results;
}

uint64_t BatchSimulator::total_cycles() const { return current_cycle_; }
uint64_t BatchSimulator::committed_count() const { return committed_count_; }

double BatchSimulator::overall_ipc() const {
    if (current_cycle_ > 0) return static_cast<double>(committed_count_) / static_cast<double>(current_cycle_);
    return 0.0;
}

} // namespace arm_cpu
