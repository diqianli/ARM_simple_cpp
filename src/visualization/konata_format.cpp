/// @file konata_format.cpp
/// @brief Konata format JSON serialization implementation.

#include "arm_cpu/visualization/konata_format.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace arm_cpu {

// =====================================================================
// KonataStage
// =====================================================================

nlohmann::json KonataStage::to_json() const {
    nlohmann::json j;
    j["name"] = name;
    j["start_cycle"] = start_cycle;
    j["end_cycle"] = end_cycle;
    return j;
}

// =====================================================================
// KonataLane
// =====================================================================

nlohmann::json KonataLane::to_json() const {
    nlohmann::json j;
    j["name"] = name;
    j["stages"] = nlohmann::json::array();
    for (const auto& stage : stages) {
        j["stages"].push_back(stage.to_json());
    }
    return j;
}

// =====================================================================
// KonataOp
// =====================================================================

nlohmann::json KonataOp::to_json() const {
    nlohmann::json j;
    j["id"] = id;
    j["gid"] = gid;
    if (rid.has_value()) j["rid"] = *rid;
    j["pc"] = pc;
    j["label_name"] = label_name;

    // Lanes as object (unordered_map -> JSON object)
    j["lanes"] = nlohmann::json::object();
    for (const auto& [key, lane] : lanes) {
        j["lanes"][key] = lane.to_json();
    }

    // Dependencies
    if (!prods.empty()) {
        j["prods"] = nlohmann::json::array();
        for (const auto& dep : prods) {
            nlohmann::json dep_j;
            dep_j["producer_id"] = dep.producer_id;
            dep_j["type"] = (dep.dep_type == KonataDependencyType::Memory) ? "memory" : "register";
            j["prods"].push_back(std::move(dep_j));
        }
    }

    // Registers
    if (!src_regs.empty()) {
        j["src_regs"] = src_regs;
    }
    if (!dst_regs.empty()) {
        j["dst_regs"] = dst_regs;
    }

    // Memory info
    if (is_memory) {
        j["is_memory"] = true;
        if (mem_addr.has_value()) {
            j["mem_addr"] = *mem_addr;
        }
    }

    // Fetched/retired cycles
    j["fetched_cycle"] = fetched_cycle;
    if (retired_cycle.has_value()) {
        j["retired_cycle"] = *retired_cycle;
    }

    return j;
}

// =====================================================================
// KonataSnapshot
// =====================================================================

nlohmann::json KonataSnapshot::to_json() const {
    nlohmann::json j;
    j["ops"] = nlohmann::json::array();
    for (const auto& op : ops) {
        j["ops"].push_back(op.to_json());
    }
    j["cycle"] = cycle;
    j["committed_count"] = committed_count;

    // Metadata
    nlohmann::json meta = nlohmann::json::object();
    if (metadata.config.has_value()) {
        nlohmann::json cfg;
        cfg["window_size"] = metadata.config->window_size;
        cfg["issue_width"] = metadata.config->issue_width;
        cfg["commit_width"] = metadata.config->commit_width;
        meta["config"] = std::move(cfg);
    }
    if (metadata.timestamp.has_value()) {
        meta["timestamp"] = *metadata.timestamp;
    }
    j["metadata"] = std::move(meta);

    return j;
}

std::string KonataSnapshot::to_json_string(bool pretty) const {
    auto j = to_json();
    return pretty ? j.dump(2) : j.dump();
}

bool KonataSnapshot::write_to_file(const std::string& path, bool pretty) const {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << to_json_string(pretty);
    return static_cast<bool>(file);
}

// =====================================================================
// KonataExport
// =====================================================================

nlohmann::json KonataExport::to_json() const {
    nlohmann::json j;
    j["version"] = version;
    j["total_cycles"] = total_cycles;
    j["total_instructions"] = total_instructions;
    j["ops_count"] = ops_count;
    j["ops"] = nlohmann::json::array();
    for (const auto& op : ops) {
        j["ops"].push_back(op.to_json());
    }
    return j;
}

bool KonataExport::write_to_file(const std::string& path, bool pretty) const {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    auto j = to_json();
    file << (pretty ? j.dump(2) : j.dump());
    return static_cast<bool>(file);
}

} // namespace arm_cpu
