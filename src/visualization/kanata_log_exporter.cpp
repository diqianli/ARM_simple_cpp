/// @file kanata_log_exporter.cpp
/// @brief Kanata log format exporter implementation.
///
/// Uses an event-based sorting approach:
/// 1. Build a flat list of events from all KonataOp entries
/// 2. Sort events by (cycle, priority, op_id)
/// 3. Emit Kanata commands sequentially with cycle deltas

#include "arm_cpu/visualization/kanata_log_exporter.hpp"

#include <algorithm>
#include <cstdio>
#include <format>
#include <fstream>

namespace arm_cpu {

namespace {

// =====================================================================
// Event types and priority ordering
// =====================================================================

enum class EventType : uint8_t {
    Insn,        // 0 — instruction introduction
    Label,       // 0 — disassembly label
    StageStart,  // 1 — stage begin
    StageEnd,    // 2 — stage end
    Retire,      // 3 — retire
    WakeUp,      // 4 — dependency wake-up
};

inline int event_priority(EventType t) {
    switch (t) {
        case EventType::Insn:       return 0;
        case EventType::Label:      return 0;
        case EventType::StageStart: return 1;
        case EventType::StageEnd:   return 2;
        case EventType::Retire:     return 3;
        case EventType::WakeUp:     return 4;
    }
    return 5;
}

struct Event {
    uint64_t cycle;
    uint64_t op_id;       // viz_id (primary sort within same priority)
    int priority;         // from event_priority()
    EventType type;
    std::string text;     // stage name / label text
    uint64_t target_id;   // for WakeUp: producer viz_id; for Retire: rid
};

// =====================================================================
// Build event list from all KonataOps
// =====================================================================

std::vector<Event> build_events(const std::vector<KonataOp>& ops) {
    std::vector<Event> events;
    events.reserve(ops.size() * 12);  // rough estimate

    for (const auto& op : ops) {
        uint64_t id = op.id;
        uint64_t fetch_cycle = op.fetched_cycle;

        // Instruction introduction — at fetched_cycle
        events.push_back(Event{fetch_cycle, id, event_priority(EventType::Insn),
                               EventType::Insn, {}, 0});

        // Label — same cycle as instruction, right after
        std::string label_text = std::format("0x{:x}: {}", op.pc, op.label_name);
        events.push_back(Event{fetch_cycle, id, event_priority(EventType::Label),
                               EventType::Label, std::move(label_text), 0});

        // Stages
        for (const auto& [lane_key, lane] : op.lanes) {
            for (const auto& stage : lane.stages) {
                // Stage start
                events.push_back(Event{stage.start_cycle, id,
                                       event_priority(EventType::StageStart),
                                       EventType::StageStart, stage.name, 0});
                // Stage end
                events.push_back(Event{stage.end_cycle, id,
                                       event_priority(EventType::StageEnd),
                                       EventType::StageEnd, stage.name, 0});
            }
        }

        // Retire
        if (op.retired_cycle.has_value()) {
            uint64_t rid = op.rid.value_or(0);
            events.push_back(Event{*op.retired_cycle, id,
                                   event_priority(EventType::Retire),
                                   EventType::Retire, {}, rid});
        }

        // Wake-up dependencies
        for (const auto& dep : op.prods) {
            events.push_back(Event{fetch_cycle, id,
                                   event_priority(EventType::WakeUp),
                                   EventType::WakeUp, {}, dep.producer_id});
        }
    }

    return events;
}

} // anonymous namespace

// =====================================================================
// KanataLogExporter::export_to_string
// =====================================================================

std::string KanataLogExporter::export_to_string(
    const std::vector<KonataOp>& ops,
    uint64_t /*total_cycles*/,
    uint64_t /*total_instructions*/)
{
    if (ops.empty()) return {};

    auto events = build_events(ops);

    // Sort by (cycle, priority, op_id)
    std::sort(events.begin(), events.end(),
              [](const Event& a, const Event& b) {
                  if (a.cycle != b.cycle) return a.cycle < b.cycle;
                  if (a.priority != b.priority) return a.priority < b.priority;
                  return a.op_id < b.op_id;
              });

    std::string result;
    result.reserve(events.size() * 40);

    // Header
    result += "Kanata\t0004\n";

    // Set initial cycle
    if (!events.empty()) {
        result += std::format("C=\t{}\n", events[0].cycle);
    }

    uint64_t prev_cycle = events.empty() ? 0 : events[0].cycle;

    for (const auto& ev : events) {
        // Emit cycle delta if cycle changed
        if (ev.cycle != prev_cycle) {
            uint64_t delta = ev.cycle - prev_cycle;
            result += std::format("C\t{}\n", delta);
            prev_cycle = ev.cycle;
        }

        switch (ev.type) {
            case EventType::Insn:
                result += std::format("I\t{}\t{}\t0\n", ev.op_id, ev.op_id);
                break;
            case EventType::Label:
                result += std::format("L\t{}\t0\t{}\n", ev.op_id, ev.text);
                break;
            case EventType::StageStart:
                result += std::format("S\t{}\t0\t{}\n", ev.op_id, ev.text);
                break;
            case EventType::StageEnd:
                result += std::format("E\t{}\t0\t{}\n", ev.op_id, ev.text);
                break;
            case EventType::Retire:
                result += std::format("R\t{}\t{}\t0\n", ev.op_id, ev.target_id);
                break;
            case EventType::WakeUp:
                result += std::format("W\t{}\t{}\t0\n", ev.op_id, ev.target_id);
                break;
        }
    }

    return result;
}

// =====================================================================
// KanataLogExporter::export_to_file
// =====================================================================

bool KanataLogExporter::export_to_file(
    const std::string& path,
    const std::vector<KonataOp>& ops,
    uint64_t total_cycles,
    uint64_t total_instructions)
{
    auto content = export_to_string(ops, total_cycles, total_instructions);
    if (content.empty()) return false;

    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << content;
    return out.good();
}

} // namespace arm_cpu
