/// @file stats_collector.cpp
/// @brief Statistics collector implementation.
///
/// Ported from Rust src/stats/mod.rs, src/stats/collector.rs, src/stats/trace_output.rs.

#include "arm_cpu/stats/stats_collector.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>

namespace arm_cpu {

// =====================================================================
// StatsCacheInfo
// =====================================================================

double StatsCacheInfo::hit_rate() const {
    if (accesses == 0) return 0.0;
    return static_cast<double>(hits) / static_cast<double>(accesses);
}

double StatsCacheInfo::miss_rate() const {
    if (accesses == 0) return 0.0;
    return static_cast<double>(misses) / static_cast<double>(accesses);
}

double StatsCacheInfo::mpki(uint64_t total_instructions) const {
    if (total_instructions == 0) return 0.0;
    return (static_cast<double>(misses) / static_cast<double>(total_instructions)) * 1000.0;
}

void StatsCacheInfo::add_access(bool hit) {
    accesses++;
    if (hit) {
        hits++;
    } else {
        misses++;
    }
}

void StatsCacheInfo::add_eviction() {
    evictions++;
}

// =====================================================================
// MemoryStats
// =====================================================================

void MemoryStats::record_load(uint64_t bytes, uint64_t latency) {
    loads++;
    bytes_read += bytes;
    // Running average
    double n = static_cast<double>(loads);
    avg_load_latency = avg_load_latency * (n - 1.0) / n + static_cast<double>(latency) / n;
}

void MemoryStats::record_store(uint64_t bytes, uint64_t latency) {
    stores++;
    bytes_written += bytes;
    // Running average
    double n = static_cast<double>(stores);
    avg_store_latency = avg_store_latency * (n - 1.0) / n + static_cast<double>(latency) / n;
}

uint64_t MemoryStats::total_ops() const {
    return loads + stores;
}

// =====================================================================
// PerformanceStats
// =====================================================================

double PerformanceStats::ipc() const {
    if (total_cycles == 0) return 0.0;
    return static_cast<double>(total_instructions) / static_cast<double>(total_cycles);
}

double PerformanceStats::cpi() const {
    if (total_instructions == 0) return 0.0;
    return static_cast<double>(total_cycles) / static_cast<double>(total_instructions);
}

void PerformanceStats::record_instruction(OpcodeType opcode_type) {
    total_instructions++;
    instr_by_type[opcode_type]++;
}

void PerformanceStats::record_cycles(uint64_t cycles) {
    total_cycles += cycles;
}

uint64_t PerformanceStats::instr_count(OpcodeType opcode_type) const {
    auto it = instr_by_type.find(opcode_type);
    if (it != instr_by_type.end()) return it->second;
    return 0;
}

double PerformanceStats::instr_percentage(OpcodeType opcode_type) const {
    if (total_instructions == 0) return 0.0;
    return static_cast<double>(instr_count(opcode_type)) / static_cast<double>(total_instructions) * 100.0;
}

double PerformanceStats::memory_instr_percentage() const {
    uint64_t mem_instrs = instr_count(OpcodeType::Load)
        + instr_count(OpcodeType::Store)
        + instr_count(OpcodeType::LoadPair)
        + instr_count(OpcodeType::StorePair);
    if (total_instructions == 0) return 0.0;
    return static_cast<double>(mem_instrs) / static_cast<double>(total_instructions) * 100.0;
}

double PerformanceStats::branch_instr_percentage() const {
    uint64_t branch_instrs = instr_count(OpcodeType::Branch)
        + instr_count(OpcodeType::BranchCond)
        + instr_count(OpcodeType::BranchReg);
    if (total_instructions == 0) return 0.0;
    return static_cast<double>(branch_instrs) / static_cast<double>(total_instructions) * 100.0;
}

void PerformanceStats::reset() {
    total_instructions = 0;
    total_cycles = 0;
    instr_by_type.clear();
    l1_stats = StatsCacheInfo{};
    l2_stats = StatsCacheInfo{};
    memory_stats = MemoryStats{};
    exec_stats = ExecutionMetrics{};
}

void PerformanceStats::merge(const PerformanceStats& other) {
    total_instructions += other.total_instructions;
    total_cycles += other.total_cycles;

    for (const auto& [opcode, count] : other.instr_by_type) {
        instr_by_type[opcode] += count;
    }

    l1_stats.accesses += other.l1_stats.accesses;
    l1_stats.hits += other.l1_stats.hits;
    l1_stats.misses += other.l1_stats.misses;
    l1_stats.evictions += other.l1_stats.evictions;

    l2_stats.accesses += other.l2_stats.accesses;
    l2_stats.hits += other.l2_stats.hits;
    l2_stats.misses += other.l2_stats.misses;
    l2_stats.evictions += other.l2_stats.evictions;
}

// =====================================================================
// TraceEntry
// =====================================================================

TraceEntry::TraceEntry(uint64_t id_, uint64_t pc_, OpcodeType opcode)
    : id(id_)
    , pc(pc_)
{
    // Convert opcode to string name
    switch (opcode) {
        #define CASE(x) case OpcodeType::x: this->opcode = #x; break
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
}

TraceEntry TraceEntry::from_instruction(const Instruction& instr) {
    TraceEntry entry(instr.id.value, instr.pc, instr.opcode_type);
    entry.disasm = instr.disasm;
    entry.mem_addr = instr.mem_access.has_value() ? std::optional<uint64_t>(instr.mem_access->addr) : std::nullopt;
    for (const auto& r : instr.src_regs) entry.src_regs.push_back(r.value);
    for (const auto& r : instr.dst_regs) entry.dst_regs.push_back(r.value);
    return entry;
}

std::optional<uint64_t> TraceEntry::total_latency() const {
    if (commit_cycle.has_value()) {
        return commit_cycle.value() - dispatch_cycle;
    }
    return {};
}

// =====================================================================
// TraceOutput
// =====================================================================

TraceOutput::TraceOutput(std::size_t max_entries)
    : max_entries_(max_entries)
    , enabled_(true)
{}

TraceOutput TraceOutput::disabled() {
    return TraceOutput(0);
}

void TraceOutput::enable() { enabled_ = true; }
void TraceOutput::disable() { enabled_ = false; }
bool TraceOutput::is_enabled() const { return enabled_; }

void TraceOutput::record_dispatch(const Instruction& instr, uint64_t cycle) {
    if (!enabled_) return;
    auto entry = TraceEntry::from_instruction(instr);
    entry.dispatch_cycle = cycle;
    add_entry(std::move(entry));
}

void TraceOutput::record_issue(InstructionId id, uint64_t cycle) {
    if (!enabled_) return;
    auto* entry = find_entry_mut(id);
    if (entry) entry->issue_cycle = cycle;
}

void TraceOutput::record_complete(InstructionId id, uint64_t cycle) {
    if (!enabled_) return;
    auto* entry = find_entry_mut(id);
    if (entry) {
        entry->complete_cycle = cycle;
        if (entry->issue_cycle.has_value()) {
            entry->exec_latency = cycle - entry->issue_cycle.value();
        }
    }
}

void TraceOutput::record_commit(InstructionId id, uint64_t cycle) {
    if (!enabled_) return;
    auto* entry = find_entry_mut(id);
    if (entry) entry->commit_cycle = cycle;
}

void TraceOutput::add_entry(TraceEntry entry) {
    if (max_entries_ > 0 && entries_.size() >= max_entries_) {
        entries_.pop_front();
    }
    entries_.push_back(std::move(entry));
}

TraceEntry* TraceOutput::find_entry_mut(InstructionId id) {
    // Search from back (most recent first)
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        if (it->id == id.value) {
            return &(*it);
        }
    }
    return nullptr;
}

const std::deque<TraceEntry>& TraceOutput::entries() const { return entries_; }
std::size_t TraceOutput::len() const { return entries_.size(); }
bool TraceOutput::is_empty() const { return entries_.empty(); }

void TraceOutput::clear() { entries_.clear(); }

std::string TraceOutput::write_text() const {
    std::string out;
    out += "ID\tPC\tOpcode\tDispatch\tIssue\tComplete\tCommit\tExecLat\tMemAddr\n";

    for (const auto& e : entries_) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%llu\t0x%llx\t%s\t%llu\t%s\t%s\t%s\t%s\t%s\n",
            static_cast<unsigned long long>(e.id),
            static_cast<unsigned long long>(e.pc),
            e.opcode.c_str(),
            static_cast<unsigned long long>(e.dispatch_cycle),
            e.issue_cycle ? std::to_string(e.issue_cycle.value()).c_str() : "-",
            e.complete_cycle ? std::to_string(e.complete_cycle.value()).c_str() : "-",
            e.commit_cycle ? std::to_string(e.commit_cycle.value()).c_str() : "-",
            e.exec_latency ? std::to_string(e.exec_latency.value()).c_str() : "-",
            e.mem_addr ? ("0x" + [&]() {
                char h[32]; std::snprintf(h, sizeof(h), "%llx", static_cast<unsigned long long>(*e.mem_addr));
                return std::string(h);
            }()).c_str() : "-");
        out += buf;
    }
    return out;
}

std::string TraceOutput::write_csv() const {
    std::string out;
    out += "id,pc,opcode,dispatch_cycle,issue_cycle,complete_cycle,commit_cycle,exec_latency,mem_addr\n";

    for (const auto& e : entries_) {
        out += std::to_string(e.id) + ","
            + std::to_string(e.pc) + ","
            + e.opcode + ","
            + std::to_string(e.dispatch_cycle) + ","
            + std::to_string(e.issue_cycle.value_or(0)) + ","
            + std::to_string(e.complete_cycle.value_or(0)) + ","
            + std::to_string(e.commit_cycle.value_or(0)) + ","
            + std::to_string(e.exec_latency.value_or(0)) + ","
            + std::to_string(e.mem_addr.value_or(0)) + "\n";
    }
    return out;
}

std::string TraceOutput::to_string() const {
    std::string out;
    for (const auto& e : entries_) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "[%llu] 0x%llx %s dispatch=%llu issue=%s complete=%s commit=%s\n",
            static_cast<unsigned long long>(e.id),
            static_cast<unsigned long long>(e.pc),
            e.opcode.c_str(),
            static_cast<unsigned long long>(e.dispatch_cycle),
            e.issue_cycle ? std::to_string(e.issue_cycle.value()).c_str() : "none",
            e.complete_cycle ? std::to_string(e.complete_cycle.value()).c_str() : "none",
            e.commit_cycle ? std::to_string(e.commit_cycle.value()).c_str() : "none");
        out += buf;
    }
    return out;
}

// =====================================================================
// StatsCollector — LatencyTracker
// =====================================================================

void StatsCollector::LatencyTracker::record(uint64_t lat) {
    count++;
    total += lat;
    min_val = (min_val == 0) ? lat : std::min(min_val, lat);
    max_val = std::max(max_val, lat);
}

double StatsCollector::LatencyTracker::average() const {
    if (count == 0) return 0.0;
    return static_cast<double>(total) / static_cast<double>(count);
}

// =====================================================================
// StatsCollector — InstrTiming
// =====================================================================

std::optional<uint64_t> StatsCollector::InstrTiming::execution_latency() const {
    if (issue_cycle.has_value() && complete_cycle.has_value()) {
        return complete_cycle.value() - issue_cycle.value();
    }
    return {};
}

std::optional<uint64_t> StatsCollector::InstrTiming::total_latency() const {
    if (commit_cycle.has_value()) {
        return commit_cycle.value() - dispatch_cycle;
    }
    return {};
}

// =====================================================================
// StatsCollector
// =====================================================================

StatsCollector::StatsCollector()
    : max_history_(1000)
{}

StatsCollector::StatsCollector(std::size_t max_history)
    : ipc_history_()
    , max_history_(max_history)
{}

void StatsCollector::record_dispatch(InstructionId id, uint64_t cycle) {
    InstrTiming timing;
    timing.dispatch_cycle = cycle;
    instr_timing_[id] = std::move(timing);
}

void StatsCollector::record_issue(InstructionId id, uint64_t cycle) {
    auto it = instr_timing_.find(id);
    if (it != instr_timing_.end()) {
        it->second.issue_cycle = cycle;
    }
}

void StatsCollector::record_complete(InstructionId id, uint64_t cycle) {
    auto it = instr_timing_.find(id);
    if (it != instr_timing_.end()) {
        it->second.complete_cycle = cycle;
    }
}

void StatsCollector::record_commit(const Instruction& instr, uint64_t cycle) {
    InstructionId id = instr.id;

    auto it = instr_timing_.find(id);
    if (it != instr_timing_.end()) {
        it->second.commit_cycle = cycle;

        if (auto lat = it->second.execution_latency(); lat.has_value()) {
            latencies_[instr.opcode_type].record(lat.value());
        }
    }

    stats_.record_instruction(instr.opcode_type);
    instr_timing_.erase(id);
}

void StatsCollector::record_l1_access(bool hit) { stats_.l1_stats.add_access(hit); }
void StatsCollector::record_l1_eviction() { stats_.l1_stats.add_eviction(); }
void StatsCollector::record_l2_access(bool hit) { stats_.l2_stats.add_access(hit); }
void StatsCollector::record_l2_eviction() { stats_.l2_stats.add_eviction(); }

void StatsCollector::record_load(uint64_t bytes, uint64_t latency) {
    stats_.memory_stats.record_load(bytes, latency);
}

void StatsCollector::record_store(uint64_t bytes, uint64_t latency) {
    stats_.memory_stats.record_store(bytes, latency);
}

void StatsCollector::record_cycles(uint64_t cycles) {
    stats_.record_cycles(cycles);
}

void StatsCollector::record_ipc_sample(double ipc) {
    ipc_history_.push_back(ipc);
    if (ipc_history_.size() > max_history_) {
        ipc_history_.pop_front();
    }
}

const PerformanceStats& StatsCollector::stats() const { return stats_; }
PerformanceStats& StatsCollector::stats_mut() { return stats_; }

double StatsCollector::avg_latency(OpcodeType opcode_type) const {
    auto it = latencies_.find(opcode_type);
    if (it != latencies_.end()) return it->second.average();
    return 0.0;
}

uint64_t StatsCollector::min_latency(OpcodeType opcode_type) const {
    auto it = latencies_.find(opcode_type);
    if (it != latencies_.end()) return it->second.min_val;
    return 0;
}

uint64_t StatsCollector::max_latency(OpcodeType opcode_type) const {
    auto it = latencies_.find(opcode_type);
    if (it != latencies_.end()) return it->second.max_val;
    return 0;
}

const std::deque<double>& StatsCollector::ipc_history() const { return ipc_history_; }

double StatsCollector::avg_ipc() const {
    if (ipc_history_.empty()) return 0.0;
    double sum = 0.0;
    for (double v : ipc_history_) sum += v;
    return sum / static_cast<double>(ipc_history_.size());
}

PerformanceMetrics StatsCollector::get_metrics() const {
    PerformanceMetrics m;
    m.total_instructions = stats_.total_instructions;
    m.total_cycles = stats_.total_cycles;
    m.ipc = stats_.ipc();
    m.cpi = stats_.cpi();
    m.l1_hit_rate = stats_.l1_stats.hit_rate();
    m.l2_hit_rate = stats_.l2_stats.hit_rate();
    m.l1_mpki = stats_.l1_stats.mpki(stats_.total_instructions);
    m.l2_mpki = stats_.l2_stats.mpki(stats_.total_instructions);
    m.memory_instr_pct = stats_.memory_instr_percentage();
    m.branch_instr_pct = stats_.branch_instr_percentage();
    m.avg_load_latency = stats_.memory_stats.avg_load_latency;
    m.avg_store_latency = stats_.memory_stats.avg_store_latency;
    return m;
}

void StatsCollector::reset() {
    stats_.reset();
    latencies_.clear();
    instr_timing_.clear();
    ipc_history_.clear();
}

} // namespace arm_cpu
