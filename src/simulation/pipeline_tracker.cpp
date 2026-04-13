/// @file pipeline_tracker.cpp
/// @brief Pipeline stage tracker implementation.
///
/// Ported from Rust src/simulation/tracker.rs.

#include "arm_cpu/simulation/pipeline_tracker.hpp"

#include <algorithm>

namespace arm_cpu {

// =====================================================================
// StageTiming
// =====================================================================

void StageTiming::record_fetch(uint64_t start, uint64_t end) {
    fetch_start = start;
    fetch_end = std::max(end, start);
}

void StageTiming::record_decode(uint64_t start, uint64_t end) {
    decode_start = start;
    decode_end = std::max(end, start);
}

void StageTiming::record_rename(uint64_t start, uint64_t end) {
    rename_start = start;
    rename_end = std::max(end, start);
}

void StageTiming::record_dispatch(uint64_t start, uint64_t end) {
    dispatch_start = start;
    dispatch_end = std::max(end, start);
}

void StageTiming::record_issue(uint64_t start, uint64_t end) {
    issue_start = start;
    issue_end = std::max(end, start);
}

void StageTiming::record_execute(uint64_t start, uint64_t end) {
    execute_start = start;
    execute_end = std::max(end, start);
}

void StageTiming::record_memory(uint64_t start, uint64_t end) {
    memory_start = start;
    memory_end = std::max(end, start);
}

void StageTiming::record_complete(uint64_t cycle) {
    complete_cycle = cycle;
}

void StageTiming::record_retire(uint64_t cycle) {
    retire_cycle = cycle;
}

std::vector<StageInfo> StageTiming::to_stages() const {
    std::vector<StageInfo> stages;

    if (fetch_start.has_value() && fetch_end.has_value()) {
        stages.push_back({"F", *fetch_start, *fetch_end});
    }
    if (decode_start.has_value() && decode_end.has_value()) {
        stages.push_back({"Dc", *decode_start, *decode_end});
    }
    if (rename_start.has_value() && rename_end.has_value()) {
        stages.push_back({"Rn", *rename_start, *rename_end});
    }
    if (dispatch_start.has_value() && dispatch_end.has_value()) {
        stages.push_back({"Ds", *dispatch_start, *dispatch_end});
    }
    if (issue_start.has_value() && issue_end.has_value()) {
        stages.push_back({"Is", *issue_start, *issue_end});
    }

    // Execute or Memory stage (mutually exclusive for most instructions)
    if (memory_start.has_value() && memory_end.has_value()) {
        stages.push_back({"Me", *memory_start, *memory_end});
    } else if (execute_start.has_value() && execute_end.has_value()) {
        stages.push_back({"Ex", *execute_start, *execute_end});
    }

    if (complete_cycle.has_value()) {
        stages.push_back({"Cm", *complete_cycle, *complete_cycle});
    }
    if (retire_cycle.has_value()) {
        stages.push_back({"Rt", *retire_cycle, *retire_cycle});
    }

    return stages;
}

// =====================================================================
// PipelineTracker
// =====================================================================

PipelineTracker::PipelineTracker()
    : max_completed_(10000)
{}

PipelineTracker::PipelineTracker(std::size_t max_completed)
    : max_completed_(max_completed)
{}

uint64_t PipelineTracker::get_or_assign_viz_id(InstructionId id) {
    auto it = viz_id_map_.find(id);
    if (it != viz_id_map_.end()) {
        return it->second;
    }
    uint64_t viz_id = next_viz_id_++;
    viz_id_map_[id] = viz_id;
    return viz_id;
}

void PipelineTracker::record_fetch(const Instruction& instr, uint64_t cycle) {
    InstructionId id = instr.id;
    uint64_t viz_id = get_or_assign_viz_id(id);

    // Initialize timing
    auto& timing = timings_[id];
    timing.record_fetch(cycle, cycle + 1);

    // Store instruction info
    if (instructions_.find(id) == instructions_.end()) {
        TrackedInstruction tracked;
        tracked.viz_id = viz_id;
        tracked.program_id = id.value;
        tracked.pc = instr.pc;
        tracked.disasm = instr.disasm.value_or(
            [&]() {
                switch (instr.opcode_type) {
                    #define CASE(x) case OpcodeType::x: return #x
                    CASE(Add); CASE(Sub); CASE(Mul); CASE(Div);
                    CASE(And); CASE(Orr); CASE(Eor); CASE(Lsl); CASE(Lsr); CASE(Asr);
                    CASE(Mov); CASE(Cmp); CASE(Shift);
                    CASE(Load); CASE(Store); CASE(LoadPair); CASE(StorePair);
                    CASE(Branch); CASE(BranchCond); CASE(BranchReg);
                    CASE(Msr); CASE(Mrs); CASE(Sys); CASE(Nop);
                    CASE(Fadd); CASE(Fsub); CASE(Fmul); CASE(Fdiv);
                    CASE(DcZva); CASE(DcCivac); CASE(DcCvac); CASE(DcCsw);
                    CASE(IcIvau); CASE(IcIallu); CASE(IcIalluis);
                    CASE(Aesd); CASE(Aese); CASE(Aesimc); CASE(Aesmc);
                    CASE(Sha1H); CASE(Sha256H); CASE(Sha512H);
                    CASE(Vadd); CASE(Vsub); CASE(Vmul); CASE(Vmla); CASE(Vmls);
                    CASE(Vld); CASE(Vst); CASE(Vdup); CASE(Vmov);
                    CASE(Fmadd); CASE(Fmsub); CASE(Fnmadd); CASE(Fnmsub); CASE(Fcvt);
                    CASE(Dmb); CASE(Dsb); CASE(Isb);
                    CASE(Eret); CASE(Yield); CASE(Adr); CASE(Pmull);
                    CASE(Other);
                    #undef CASE
                }
                return "Unknown";
            }()
        );
        tracked.timing = StageTiming{};
        for (const auto& r : instr.src_regs) tracked.src_regs.push_back(r.value);
        for (const auto& r : instr.dst_regs) tracked.dst_regs.push_back(r.value);
        tracked.is_memory = instr.mem_access.has_value();
        tracked.mem_addr = instr.mem_access.has_value() ? std::optional<uint64_t>(instr.mem_access->addr) : std::nullopt;
        tracked.dependencies = {};
        instructions_[id] = std::move(tracked);
    }

    // Add to order if new
    if (std::find(order_.begin(), order_.end(), id) == order_.end()) {
        order_.push_back(id);
    }
}

void PipelineTracker::record_decode(InstructionId id, uint64_t start, uint64_t end) {
    timings_[id].record_decode(start, end);
}

void PipelineTracker::record_rename(InstructionId id, uint64_t start, uint64_t end) {
    timings_[id].record_rename(start, end);
}

void PipelineTracker::record_dispatch(InstructionId id, uint64_t cycle) {
    auto& timing = timings_[id];

    // Infer decode/rename if not set
    uint64_t fetch_end_val = timing.fetch_end.value_or(cycle);
    if (!timing.decode_start.has_value()) {
        if (cycle > fetch_end_val) {
            uint64_t available = cycle - fetch_end_val;
            if (available >= 2) {
                timing.record_decode(fetch_end_val, fetch_end_val + 1);
                timing.record_rename(fetch_end_val + 1, fetch_end_val + 2);
            } else {
                timing.record_decode(fetch_end_val, cycle);
                timing.record_rename(fetch_end_val, cycle);
            }
        } else {
            timing.record_decode(cycle, cycle);
            timing.record_rename(cycle, cycle);
        }
    }
    timing.record_dispatch(cycle, cycle);
}

void PipelineTracker::record_issue(InstructionId id, uint64_t cycle) {
    auto& timing = timings_[id];
    uint64_t dispatch_end_val = timing.dispatch_end.value_or(cycle);
    timing.record_issue(dispatch_end_val, std::max(cycle, dispatch_end_val));
}

void PipelineTracker::record_execute(InstructionId id, uint64_t start, uint64_t end) {
    auto& timing = timings_[id];
    uint64_t issue_end_val = timing.issue_end.value_or(start);
    uint64_t eff_start = std::max(start, issue_end_val);
    timing.record_execute(eff_start, std::max(end, eff_start));
}

void PipelineTracker::record_memory(InstructionId id, uint64_t start, uint64_t end) {
    auto& timing = timings_[id];
    uint64_t issue_end_val = timing.issue_end.value_or(start);
    uint64_t eff_start = std::max(start, issue_end_val);
    timing.record_memory(eff_start, std::max(end, eff_start));
}

void PipelineTracker::record_complete(InstructionId id, uint64_t cycle) {
    timings_[id].record_complete(cycle);

    // Track completed
    auto it = std::find(completed_.begin(), completed_.end(), id);
    if (it == completed_.end()) {
        completed_.push_back(id);
        if (completed_.size() > max_completed_) {
            completed_.pop_front();
        }
    }
}

void PipelineTracker::record_retire(InstructionId id, uint64_t cycle) {
    timings_[id].record_retire(cycle);
    retire_counter_++;
}

void PipelineTracker::add_dependency(InstructionId consumer, InstructionId producer, bool is_memory_dep) {
    uint64_t viz_producer_id = get_or_assign_viz_id(producer);
    DependencyType dep_type = is_memory_dep ? DependencyType::Memory : DependencyType::Register;

    auto& deps = dependencies_[consumer];
    // Check for duplicates
    bool exists = false;
    for (const auto& d : deps) {
        if (d.producer_id == viz_producer_id && d.dep_type == dep_type) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        deps.push_back({viz_producer_id, dep_type});
    }
}

const StageTiming* PipelineTracker::get_timing(InstructionId id) const {
    auto it = timings_.find(id);
    if (it != timings_.end()) return &it->second;
    return nullptr;
}

std::optional<uint64_t> PipelineTracker::get_viz_id(InstructionId id) const {
    auto it = viz_id_map_.find(id);
    if (it != viz_id_map_.end()) return it->second;
    return {};
}

uint64_t PipelineTracker::retire_count() const {
    return retire_counter_;
}

std::size_t PipelineTracker::len() const {
    return timings_.size();
}

bool PipelineTracker::is_empty() const {
    return timings_.empty();
}

void PipelineTracker::clear() {
    timings_.clear();
    instructions_.clear();
    order_.clear();
    viz_id_map_.clear();
    next_viz_id_ = 0;
    retire_counter_ = 0;
    completed_.clear();
    dependencies_.clear();
}

std::vector<TrackedInstruction> PipelineTracker::export_instructions() const {
    std::vector<TrackedInstruction> result;

    for (const auto& id : order_) {
        auto timing_it = timings_.find(id);
        auto viz_it = viz_id_map_.find(id);
        if (timing_it == timings_.end() || viz_it == viz_id_map_.end()) continue;

        auto instr_it = instructions_.find(id);
        if (instr_it == instructions_.end()) continue;

        TrackedInstruction tracked = instr_it->second;
        tracked.timing = timing_it->second;

        auto dep_it = dependencies_.find(id);
        if (dep_it != dependencies_.end()) {
            tracked.dependencies = dep_it->second;
        }

        result.push_back(std::move(tracked));
    }

    return result;
}

} // namespace arm_cpu
