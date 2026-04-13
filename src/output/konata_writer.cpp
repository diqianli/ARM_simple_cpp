/// @file konata_writer.cpp
/// @brief Implementation of Konata-compatible JSON format generator.

#include "arm_cpu/output/konata_writer.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>

namespace arm_cpu {

// =====================================================================
// KonataOp implementation
// =====================================================================

void KonataOp::add_stage(const std::string& name, uint64_t start, uint64_t end) {
    // Find or create the main lane
    if (!lanes.empty()) {
        auto& lane = lanes[0];
        lane.stages.push_back(KonataStage::make(name, start, end));
    } else {
        KonataLane lane;
        lane.name = "main";
        lane.stages.push_back(KonataStage::make(name, start, end));
        lanes.push_back(std::move(lane));
    }
}

void KonataOp::add_dependency(KonataDependency dep) {
    prods.push_back(std::move(dep));
}

void KonataOp::set_registers(std::vector<uint16_t> src, std::vector<uint16_t> dst) {
    src_regs = std::move(src);
    dst_regs = std::move(dst);
}

void KonataOp::set_memory(uint64_t addr) {
    is_memory = true;
    mem_addr = addr;
}

nlohmann::json KonataOp::to_json() const {
    nlohmann::json j;
    j["id"] = id;
    j["gid"] = gid;
    if (rid.has_value()) j["rid"] = *rid;
    j["pc"] = pc;
    j["label_name"] = label_name;

    // Lanes
    auto& lanes_arr = j["lanes"];
    lanes_arr = nlohmann::json::array();
    for (const auto& lane : lanes) {
        nlohmann::json lane_j;
        lane_j["name"] = lane.name;
        auto& stages_arr = lane_j["stages"];
        stages_arr = nlohmann::json::array();
        for (const auto& stage : lane.stages) {
            nlohmann::json stage_j;
            stage_j["name"] = stage.name;
            stage_j["start_cycle"] = stage.start_cycle;
            stage_j["end_cycle"] = stage.end_cycle;
            stages_arr.push_back(std::move(stage_j));
        }
        lanes_arr.push_back(std::move(lane_j));
    }

    // Dependencies
    if (!prods.empty()) {
        auto& prods_arr = j["prods"];
        prods_arr = nlohmann::json::array();
        for (const auto& dep : prods) {
            nlohmann::json dep_j;
            dep_j["producer_id"] = dep.producer_id;
            dep_j["type"] = dep.dep_type;
            prods_arr.push_back(std::move(dep_j));
        }
    }

    // Source registers
    if (!src_regs.empty()) {
        j["src_regs"] = src_regs;
    }

    // Destination registers
    if (!dst_regs.empty()) {
        j["dst_regs"] = dst_regs;
    }

    // Memory info
    if (is_memory.has_value() && *is_memory) {
        j["is_memory"] = true;
        if (mem_addr.has_value()) {
            j["mem_addr"] = *mem_addr;
        }
    }

    // Fetched/retired cycles
    if (fetched_cycle.has_value()) {
        j["fetched_cycle"] = *fetched_cycle;
    }
    if (retired_cycle.has_value()) {
        j["retired_cycle"] = *retired_cycle;
    }

    return j;
}

// =====================================================================
// KonataOutput implementation
// =====================================================================

void KonataOutput::add_op(KonataOp op) {
    ops.push_back(std::move(op));
    ops_count = ops.size();
}

void KonataOutput::set_summary(uint64_t cycles, uint64_t instructions) {
    total_cycles = cycles;
    total_instructions = instructions;
}

std::string KonataOutput::to_json(bool pretty) const {
    nlohmann::json j;
    j["version"] = version;
    j["total_cycles"] = total_cycles;
    j["total_instructions"] = total_instructions;
    j["ops_count"] = ops_count;

    auto& ops_arr = j["ops"];
    ops_arr = nlohmann::json::array();
    for (const auto& op : ops) {
        ops_arr.push_back(op.to_json());
    }

    if (pretty) {
        return j.dump(2);
    }
    return j.dump();
}

bool KonataOutput::write_to_file(const std::string& path, bool pretty) const {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << to_json(pretty);
    return static_cast<bool>(file);
}

// =====================================================================
// KonataWriter implementation
// =====================================================================

KonataWriter::KonataWriter()
    : config_(KonataConfig::default_config()) {}

KonataWriter::KonataWriter(KonataConfig config)
    : config_(std::move(config)) {}

void KonataWriter::handle_event(const KonataEvent& event) {
    switch (event.type) {
    case KonataEventType::InstructionFetch: {
        auto op = KonataOp::make(
            static_cast<uint64_t>(ops_.size()),
            event.id.value,
            event.instruction.pc,
            event.instruction.disasm.value_or(
                "OP_" + std::to_string(static_cast<int>(event.instruction.opcode_type)))
        );
        auto idx = ops_.size();
        id_map_[event.id.value] = idx;
        ops_.push_back(std::move(op));

        // Add fetch stage
        ops_[idx].add_stage("F", event.cycle, event.cycle + 1);
        ops_[idx].fetched_cycle = event.cycle;
        break;
    }

    case KonataEventType::InstructionDispatch: {
        auto it = id_map_.find(event.id.value);
        if (it != id_map_.end()) {
            auto& op = ops_[it->second];
            auto fetch_end = last_stage_end(it->second);
            uint64_t base = fetch_end.value_or(event.cycle);

            if (event.cycle > base) {
                if (event.cycle - base >= 3) {
                    op.add_stage("Dc", base, base + 1);
                    op.add_stage("Rn", base + 1, base + 2);
                    op.add_stage("Ds", base + 2, event.cycle);
                } else {
                    op.add_stage("Dc", base, event.cycle);
                    op.add_stage("Rn", base, event.cycle);
                    op.add_stage("Ds", event.cycle, event.cycle);
                }
            } else {
                op.add_stage("Dc", event.cycle, event.cycle);
                op.add_stage("Rn", event.cycle, event.cycle);
                op.add_stage("Ds", event.cycle, event.cycle);
            }
        }
        break;
    }

    case KonataEventType::InstructionIssue: {
        auto it = id_map_.find(event.id.value);
        if (it != id_map_.end()) {
            auto& op = ops_[it->second];
            auto dispatch_end = last_stage_end(it->second);
            uint64_t base = dispatch_end.value_or(event.cycle);
            op.add_stage("Is", base, std::max(event.cycle, base));
        }
        break;
    }

    case KonataEventType::InstructionExecuteEnd: {
        auto it = id_map_.find(event.id.value);
        if (it != id_map_.end()) {
            auto& op = ops_[it->second];
            auto issue_end = last_stage_end(it->second);
            uint64_t base = issue_end.value_or(event.cycle);
            op.add_stage("Ex", base, std::max(event.cycle, base));
        }
        break;
    }

    case KonataEventType::MemoryComplete: {
        auto it = id_map_.find(event.id.value);
        if (it != id_map_.end()) {
            auto& op = ops_[it->second];
            auto exec_end = last_stage_end(it->second);
            uint64_t base = exec_end.value_or(event.cycle);
            op.add_stage("Me", base, std::max(event.cycle, base));
        }
        break;
    }

    case KonataEventType::InstructionComplete: {
        auto it = id_map_.find(event.id.value);
        if (it != id_map_.end()) {
            ops_[it->second].add_stage("Cm", event.cycle, event.cycle);
        }
        break;
    }

    case KonataEventType::InstructionRetire: {
        auto it = id_map_.find(event.id.value);
        if (it != id_map_.end()) {
            ops_[it->second].add_stage("Rt", event.cycle, event.cycle);
            ops_[it->second].rid = event.retire_order;
            ops_[it->second].retired_cycle = event.cycle;
        }
        committed_count_ = std::max(event.retire_order, committed_count_);
        break;
    }

    case KonataEventType::MemoryAccess: {
        if (config_.include_memory) {
            auto it = id_map_.find(event.id.value);
            if (it != id_map_.end()) {
                ops_[it->second].set_memory(event.addr);
            }
        }
        break;
    }

    case KonataEventType::Dependency: {
        if (config_.include_dependencies) {
            auto consumer_it = id_map_.find(event.id.value);
            auto producer_it = id_map_.find(event.producer_id.value);
            if (consumer_it != id_map_.end() && producer_it != id_map_.end()) {
                auto dep = event.is_memory_dep
                    ? KonataDependency::memory_dep(
                        static_cast<uint64_t>(producer_it->second))
                    : KonataDependency::register_dep(
                        static_cast<uint64_t>(producer_it->second));
                ops_[consumer_it->second].add_dependency(std::move(dep));
            }
        }
        break;
    }

    case KonataEventType::CycleBoundary:
        current_cycle_ = event.cycle;
        break;

    case KonataEventType::SimulationEnd:
        current_cycle_ = event.cycle;
        break;
    }
}

KonataOutput KonataWriter::get_output() const {
    KonataOutput output;
    output.set_summary(current_cycle_, committed_count_);
    output.ops = ops_;
    output.ops_count = ops_.size();
    return output;
}

bool KonataWriter::write_to_file(const std::string& path) const {
    auto output = get_output();
    return output.write_to_file(path, config_.pretty_print);
}

void KonataWriter::clear() {
    ops_.clear();
    id_map_.clear();
    current_cycle_ = 0;
    committed_count_ = 0;
}

std::optional<uint64_t> KonataWriter::last_stage_end(std::size_t op_idx) const {
    if (op_idx >= ops_.size()) return std::nullopt;
    const auto& op = ops_[op_idx];
    if (op.lanes.empty()) return std::nullopt;
    const auto& stages = op.lanes[0].stages;
    if (stages.empty()) return std::nullopt;
    return stages.back().end_cycle;
}

} // namespace arm_cpu
