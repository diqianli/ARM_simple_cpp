/// @file pipeline_tracker_viz.cpp
/// @brief Pipeline tracker visualization implementation.

#include "arm_cpu/visualization/pipeline_tracker_viz.hpp"
#include "arm_cpu/ooo/ooo_engine.hpp"

#include <algorithm>
#include <format>
#include <cstring>

namespace arm_cpu {

// =====================================================================
// StageTimingViz::to_stages()
// =====================================================================

std::vector<KonataStage> StageTimingViz::to_stages() const {
    std::vector<KonataStage> stages;
    uint64_t last_end = 0;
    std::optional<uint64_t> exec_mem_end;

    // Helper: add a stage ensuring sequential (non-overlapping) visualization
    auto add_stage_sequential = [&](const std::string& name, uint64_t start, uint64_t end) {
        uint64_t adjusted_end = std::max(end, start + 1);
        uint64_t visual_start = std::max(start, last_end);
        uint64_t visual_end = std::max(adjusted_end, visual_start + 1);
        stages.push_back(KonataStage::create(name, visual_start, visual_end));
        last_end = visual_end;
    };

    if (fetch_start && fetch_end)
        add_stage_sequential("IF", *fetch_start, *fetch_end);
    if (decode_start && decode_end)
        add_stage_sequential("DE", *decode_start, *decode_end);
    if (rename_start && rename_end)
        add_stage_sequential("RN", *rename_start, *rename_end);
    if (dispatch_start && dispatch_end)
        add_stage_sequential("DI", *dispatch_start, *dispatch_end);

    if (issue_start && issue_end) {
        // Annotate DI waiting gap if there's a delay between DI end and IS start
        if (*issue_start > last_end) {
            std::string di_with_wait = std::format("DI:{}-{}", last_end, *issue_start - 1);
            add_stage_sequential(di_with_wait, last_end, *issue_start);
        }
        add_stage_sequential("IS", *issue_start, *issue_end);
    }

    if (memory_start && memory_end) {
        exec_mem_end = *memory_end;

        // Use cache hierarchy sub-stages if available
        if (cache_access.has_value() && !cache_access->level_timing.empty()) {
            for (const auto& lt : cache_access->level_timing) {
                std::string stage_name = std::format("ME:{}", cache_level_name(lt.level));
                add_stage_sequential(stage_name, lt.start_cycle, lt.end_cycle);
            }
        } else {
            add_stage_sequential("ME", *memory_start, *memory_end);
        }
    } else if (execute_start && execute_end) {
        add_stage_sequential("EX", *execute_start, *execute_end);
        exec_mem_end = execute_end;
    }

    // Writeback: from Execute/Memory end to complete
    if (complete_cycle.has_value() && exec_mem_end.has_value()) {
        uint64_t wb_start = std::max(*exec_mem_end, last_end);
        uint64_t wb_end = std::max(*complete_cycle, wb_start + 1);
        stages.push_back(KonataStage::create("WB", wb_start, wb_end));
        last_end = wb_end;
    }

    // Retire
    if (retire_cycle.has_value()) {
        uint64_t rr_end = std::max(*retire_cycle, last_end + 1);
        stages.push_back(KonataStage::create("RR", last_end, rr_end));
    }

    return stages;
}

// =====================================================================
// PipelineTrackerViz
// =====================================================================

PipelineTrackerViz::PipelineTrackerViz(std::size_t fetch_width)
    : fetch_width_(fetch_width)
{}

void PipelineTrackerViz::record_fetch(const Instruction& instr, uint64_t cycle) {
    auto id = instr.id;
    get_or_assign_viz_id(id);

    if (fetch_count_in_cycle_ >= fetch_width_) {
        ++current_fetch_cycle_;
        fetch_count_in_cycle_ = 0;
    } else if (fetch_count_in_cycle_ == 0) {
        current_fetch_cycle_ = 0;
    }

    uint64_t adjusted_cycle = std::max(current_fetch_cycle_, cycle);
    ++fetch_count_in_cycle_;

    auto& timing = timings_[id];
    timing.fetch_start = adjusted_cycle;
    timing.fetch_end = adjusted_cycle + 1;

    bool already_ordered = false;
    for (const auto& existing : order_) {
        if (existing == id) { already_ordered = true; break; }
    }
    if (!already_ordered) {
        order_.push_back(id);
    }

    if (instr.disasm.has_value()) {
        disasm_map_[id] = *instr.disasm;
    }
    if (!instr.src_regs.empty()) {
        std::vector<uint16_t> regs;
        for (const auto& r : instr.src_regs) regs.push_back(r.value);
        src_regs_map_[id] = std::move(regs);
    }
    if (!instr.dst_regs.empty()) {
        std::vector<uint16_t> regs;
        for (const auto& r : instr.dst_regs) regs.push_back(r.value);
        dst_regs_map_[id] = std::move(regs);
    }
    if (instr.mem_access.has_value()) {
        mem_access_map_[id] = {instr.mem_access->addr, instr.mem_access->size, instr.mem_access->is_load};
    }
}

void PipelineTrackerViz::record_decode(InstructionId id, uint64_t start_cycle, uint64_t end_cycle) {
    auto& timing = timings_[id];
    timing.decode_start = start_cycle;
    timing.decode_end = end_cycle;
}

void PipelineTrackerViz::record_rename(InstructionId id, uint64_t start_cycle, uint64_t end_cycle) {
    auto& timing = timings_[id];
    timing.rename_start = start_cycle;
    timing.rename_end = end_cycle;
}

void PipelineTrackerViz::record_dispatch(InstructionId id, uint64_t cycle) {
    auto& timing = timings_[id];

    uint64_t fetch_end = timing.fetch_end.value_or(cycle);
    uint64_t decode_start = timing.fetch_start.value_or(fetch_end);
    uint64_t decode_end = cycle <= fetch_end
        ? std::max(cycle, decode_start)
        : std::min(fetch_end + 1, cycle);

    if (!timing.decode_start.has_value()) {
        timing.decode_start = decode_start;
        timing.decode_end = decode_end;
    }

    uint64_t rename_start = timing.decode_end.value_or(decode_end);
    uint64_t rename_end = cycle <= rename_start
        ? std::max(cycle, rename_start)
        : std::min(rename_start + 1, cycle);

    if (!timing.rename_start.has_value()) {
        timing.rename_start = rename_start;
        timing.rename_end = rename_end;
    }

    uint64_t dispatch_start = timing.rename_end.value_or(rename_end);
    uint64_t dispatch_end = std::max(cycle, dispatch_start);
    timing.dispatch_start = dispatch_start;
    timing.dispatch_end = dispatch_end;
}

void PipelineTrackerViz::record_ready(InstructionId id, uint64_t cycle) {
    timings_[id].ready_cycle = cycle;
}

void PipelineTrackerViz::record_issue(InstructionId id, uint64_t cycle) {
    auto& timing = timings_[id];
    uint64_t issue_start = timing.ready_cycle
        .value_or(timing.dispatch_end.value_or(cycle));
    uint64_t issue_end = std::max(issue_start, cycle);
    timing.issue_start = issue_start;
    timing.issue_end = issue_end;
}

void PipelineTrackerViz::record_execute_start(InstructionId id, uint64_t cycle) {
    auto& timing = timings_[id];
    uint64_t start = timing.issue_start.value_or(cycle);
    timing.execute_start = start;
}

void PipelineTrackerViz::record_execute_end(InstructionId id, uint64_t cycle) {
    auto& timing = timings_[id];
    uint64_t exec_start = timing.issue_end.value_or(cycle);
    uint64_t exec_end = std::max(exec_start, cycle);
    timing.execute_start = exec_start;
    timing.execute_end = exec_end;
}

void PipelineTrackerViz::record_memory(InstructionId id, uint64_t start_cycle, uint64_t end_cycle) {
    auto& timing = timings_[id];
    uint64_t mem_start = timing.issue_end.value_or(start_cycle);
    uint64_t mem_end = std::max(mem_start, end_cycle);
    timing.memory_start = mem_start;
    timing.memory_end = mem_end;
}

void PipelineTrackerViz::record_cache_access(InstructionId id, const CacheAccessInfoViz& info) {
    timings_[id].cache_access = info;
}

void PipelineTrackerViz::record_complete(InstructionId id, uint64_t cycle) {
    auto& timing = timings_[id];

    if (timing.memory_start.has_value() && !timing.memory_end.has_value()) {
        timing.memory_end = cycle;
    } else if (timing.execute_start.has_value() && !timing.execute_end.has_value()) {
        timing.execute_end = cycle;
    }

    if (!timing.complete_cycle.has_value()) {
        timing.complete_cycle = cycle;
    }

    bool already_completed = false;
    for (const auto& c : completed_) {
        if (c == id) { already_completed = true; break; }
    }
    if (!already_completed) {
        completed_.push_back(id);
        if (completed_.size() > max_completed_) {
            completed_.pop_front();
        }
    }
}

void PipelineTrackerViz::record_retire(InstructionId id, uint64_t cycle) {
    auto& timing = timings_[id];
    if (!timing.retire_cycle.has_value()) {
        timing.retire_cycle = cycle;
        retire_order_map_[id] = ++retire_counter_;
    }
}

void PipelineTrackerViz::add_dependency(InstructionId consumer, InstructionId producer, bool is_memory) {
    auto viz_producer_id = get_or_assign_viz_id(producer);
    auto& deps = dependencies_[consumer];

    bool exists = false;
    for (const auto& d : deps) {
        if (d.producer_id == viz_producer_id && d.dep_type == (is_memory ? KonataDependencyType::Memory : KonataDependencyType::Register)) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        deps.push_back(TrackerDependencyInfo{
            viz_producer_id,
            is_memory ? KonataDependencyType::Memory : KonataDependencyType::Register
        });
    }
}

uint64_t PipelineTrackerViz::get_or_assign_viz_id(InstructionId id) {
    auto it = viz_id_map_.find(id);
    if (it != viz_id_map_.end()) return it->second;
    auto vid = next_viz_id_++;
    viz_id_map_[id] = vid;
    return vid;
}

std::vector<KonataOp> PipelineTrackerViz::to_konata_ops(
    const std::vector<const WindowEntry*>& entries, uint64_t current_cycle) const
{
    std::vector<KonataOp> ops;

    for (const auto* entry : entries) {
        auto id = entry->instruction.id;
        auto viz_it = viz_id_map_.find(id);
        if (viz_it == viz_id_map_.end()) continue;
        uint64_t viz_id = viz_it->second;

        auto timing = timings_;
        auto timing_it = timing.find(id);
        if (timing_it == timing.end()) continue;
        auto mut_timing = timing_it->second;

        // For waiting instructions, add in-progress issue stage
        if (!mut_timing.issue_start.has_value() && mut_timing.dispatch_end.has_value()) {
            uint64_t dispatch_end = *mut_timing.dispatch_end;
            mut_timing.issue_start = dispatch_end;
            mut_timing.issue_end = current_cycle;
        }

        // For issued but not completed, add in-progress execute
        if (!mut_timing.execute_start.has_value() && mut_timing.issue_end.has_value() && !mut_timing.complete_cycle.has_value()) {
            uint64_t issue_end = *mut_timing.issue_end;
            mut_timing.execute_start = issue_end;
            mut_timing.execute_end = current_cycle;
        }

        std::string label = entry->instruction.disasm.value_or(
            std::format("Instr {}", id.value));

        KonataOp op(viz_id, id.value, entry->instruction.pc, std::move(label));
        op.fetched_cycle = mut_timing.fetch_start.value_or(0);
        op.retired_cycle = mut_timing.retire_cycle;

        // Set retire order ID if instruction has been retired
        auto retire_it = retire_order_map_.find(id);
        if (retire_it != retire_order_map_.end()) {
            op.rid = retire_it->second;
        }

        // Add stages via to_stages() for sequential, non-overlapping visualization
        for (const auto& stage : mut_timing.to_stages()) {
            const auto& name = stage.name;
            if (name == "IF") op.add_stage(StageId::IF, stage.start_cycle, stage.end_cycle);
            else if (name == "DE") op.add_stage(StageId::DE, stage.start_cycle, stage.end_cycle);
            else if (name == "RN") op.add_stage(StageId::RN, stage.start_cycle, stage.end_cycle);
            else if (name == "DI") op.add_stage(StageId::DI, stage.start_cycle, stage.end_cycle);
            else if (name == "IS") op.add_stage(StageId::IS, stage.start_cycle, stage.end_cycle);
            else if (name == "EX") op.add_stage(StageId::EX, stage.start_cycle, stage.end_cycle);
            else if (name == "ME") op.add_stage(StageId::ME, stage.start_cycle, stage.end_cycle);
            else if (name == "WB") op.add_stage(StageId::WB, stage.start_cycle, stage.end_cycle);
            else if (name == "RR") op.add_stage(StageId::RR, stage.start_cycle, stage.end_cycle);
            else op.add_stage_with_name(name, stage.start_cycle, stage.end_cycle);
        }

        // Add registers
        for (const auto& r : entry->instruction.src_regs) op.src_regs.push_back(r.value);
        for (const auto& r : entry->instruction.dst_regs) op.dst_regs.push_back(r.value);

        // Add memory info
        if (entry->instruction.mem_access.has_value()) {
            op.is_memory = true;
            op.mem_addr = entry->instruction.mem_access->addr;
        }

        // Add dependencies
        auto dep_it = dependencies_.find(id);
        if (dep_it != dependencies_.end()) {
            for (const auto& dep : dep_it->second) {
                op.prods.push_back(KonataDependencyRef{dep.producer_id, dep.dep_type});
            }
        }

        ops.push_back(std::move(op));
    }

    std::sort(ops.begin(), ops.end(),
              [](const KonataOp& a, const KonataOp& b) { return a.id < b.id; });

    return ops;
}

KonataSnapshot PipelineTrackerViz::to_snapshot(
    const std::vector<const WindowEntry*>& entries,
    uint64_t cycle, uint64_t committed_count) const
{
    auto ops = to_konata_ops(entries, cycle);
    KonataSnapshot snapshot{{}, cycle, committed_count, KonataMetadata{}};
    for (auto& op : ops) {
        snapshot.ops.push_back(std::move(op));
    }
    return snapshot;
}

std::vector<KonataOp> PipelineTrackerViz::export_all_konata_ops() const {
    std::vector<KonataOp> ops;

    for (const auto& [id, viz_id] : viz_id_map_) {
        auto timing_it = timings_.find(id);
        if (timing_it == timings_.end()) continue;
        const auto& timing = timing_it->second;

        std::string disasm = "Instr " + std::to_string(id.value);
        auto disasm_it = disasm_map_.find(id);
        if (disasm_it != disasm_map_.end()) disasm = disasm_it->second;

        KonataOp op(viz_id, id.value, 0, disasm);
        op.fetched_cycle = timing.fetch_start.value_or(0);
        op.retired_cycle = timing.retire_cycle;

        // Set retire order ID if instruction has been retired
        auto retire_it = retire_order_map_.find(id);
        if (retire_it != retire_order_map_.end()) {
            op.rid = retire_it->second;
        }

        // Add stages via to_stages() for sequential, non-overlapping visualization
        for (const auto& stage : timing.to_stages()) {
            const auto& name = stage.name;
            if (name == "IF") op.add_stage(StageId::IF, stage.start_cycle, stage.end_cycle);
            else if (name == "DE") op.add_stage(StageId::DE, stage.start_cycle, stage.end_cycle);
            else if (name == "RN") op.add_stage(StageId::RN, stage.start_cycle, stage.end_cycle);
            else if (name == "DI") op.add_stage(StageId::DI, stage.start_cycle, stage.end_cycle);
            else if (name == "IS") op.add_stage(StageId::IS, stage.start_cycle, stage.end_cycle);
            else if (name == "EX") op.add_stage(StageId::EX, stage.start_cycle, stage.end_cycle);
            else if (name == "ME") op.add_stage(StageId::ME, stage.start_cycle, stage.end_cycle);
            else if (name == "WB") op.add_stage(StageId::WB, stage.start_cycle, stage.end_cycle);
            else if (name == "RR") op.add_stage(StageId::RR, stage.start_cycle, stage.end_cycle);
            else op.add_stage_with_name(name, stage.start_cycle, stage.end_cycle);
        }

        auto src_it = src_regs_map_.find(id);
        if (src_it != src_regs_map_.end()) op.src_regs = src_it->second;
        auto dst_it = dst_regs_map_.find(id);
        if (dst_it != dst_regs_map_.end()) op.dst_regs = dst_it->second;

        auto mem_it = mem_access_map_.find(id);
        if (mem_it != mem_access_map_.end()) {
            op.is_memory = true;
            op.mem_addr = std::get<0>(mem_it->second);
        }

        auto dep_it = dependencies_.find(id);
        if (dep_it != dependencies_.end()) {
            for (const auto& dep : dep_it->second) {
                op.prods.push_back(KonataDependencyRef{dep.producer_id, dep.dep_type});
            }
        }

        ops.push_back(std::move(op));
    }

    std::sort(ops.begin(), ops.end(),
              [](const KonataOp& a, const KonataOp& b) { return a.id < b.id; });

    return ops;
}

void PipelineTrackerViz::clear() {
    timings_.clear();
    order_.clear();
    viz_id_map_.clear();
    next_viz_id_ = 0;
    retire_counter_ = 0;
    completed_.clear();
    dependencies_.clear();
    disasm_map_.clear();
    src_regs_map_.clear();
    dst_regs_map_.clear();
    mem_access_map_.clear();
    retire_order_map_.clear();
}

const StageTimingViz* PipelineTrackerViz::get_timing(InstructionId id) const {
    auto it = timings_.find(id);
    return it != timings_.end() ? &it->second : nullptr;
}

const std::vector<TrackerDependencyInfo>* PipelineTrackerViz::get_dependencies(InstructionId id) const {
    auto it = dependencies_.find(id);
    return it != dependencies_.end() ? &it->second : nullptr;
}

const CacheAccessInfoViz* PipelineTrackerViz::get_cache_access(InstructionId id) const {
    auto it = timings_.find(id);
    if (it == timings_.end() || !it->second.cache_access.has_value()) return nullptr;
    return &*it->second.cache_access;
}

} // namespace arm_cpu
