#pragma once

/// @file chi_interconnect.hpp
/// @brief CHI Interconnect — connects RN-F, HN-F, and SN-F nodes with
///        configurable per-channel latencies and message routing.

#include "arm_cpu/chi/chi_node.hpp"
#include "arm_cpu/chi/rn_f.hpp"
#include "arm_cpu/chi/hn_f.hpp"
#include "arm_cpu/chi/sn_f.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <variant>
#include <vector>

namespace arm_cpu {

// =====================================================================
// ChiInterconnectConfig
// =====================================================================
struct ChiInterconnectConfig {
    uint64_t req_latency = 2;
    uint64_t rsp_latency = 2;
    uint64_t dat_latency = 4;
    uint64_t snp_latency = 2;

    static ChiInterconnectConfig typical() { return {}; }
};

// =====================================================================
// InFlightMessageType — tagged union for in-flight messages
// =====================================================================
struct InFlightMessage {
    NodeId  dest;
    uint64_t arrival_cycle = 0;

    std::variant<ChiRequest, ChiResponse, ChiData, ChiSnoop, ChiSnoopResp> msg;
};

// =====================================================================
// InterconnectStats
// =====================================================================
struct InterconnectStats {
    uint64_t    requests      = 0;
    uint64_t    responses     = 0;
    uint64_t    data_msgs     = 0;
    uint64_t    snoops        = 0;
    uint64_t    snoop_resps   = 0;
    uint64_t    total_messages = 0;
    std::size_t peak_in_flight = 0;
};

// =====================================================================
// ChiInterconnect
// =====================================================================
class ChiInterconnect {
public:
    explicit ChiInterconnect(ChiInterconnectConfig config);

    void send_request(const ChiRequest& req, NodeId dest);
    void send_response(const ChiResponse& resp, NodeId dest);
    void send_data(const ChiData& data, NodeId dest);
    void send_snoop(const ChiSnoop& snoop, NodeId dest);
    void send_snoop_resp(const ChiSnoopResp& resp, NodeId dest);

    std::vector<std::pair<NodeId, InFlightMessage>> get_arriving_messages();

    void     advance_cycle();
    uint64_t current_cycle() const;

    const InterconnectStats& stats() const;
    std::size_t in_flight_count() const;
    void clear();

private:
    void update_peak();

    ChiInterconnectConfig config_;
    std::deque<InFlightMessage> in_flight_;
    uint64_t current_cycle_ = 0;
    InterconnectStats stats_;
};

// =====================================================================
// ChiSystem — complete CHI system with all nodes
// =====================================================================
class ChiSystem {
public:
    ChiSystem(std::unique_ptr<RnFNode> rn_f, std::unique_ptr<HnFNode> hn_f,
              std::unique_ptr<SnFNode> sn_f, ChiInterconnectConfig interconnect_config);

    ChiSystem(std::vector<std::unique_ptr<RnFNode>> rn_f_nodes,
              std::unique_ptr<HnFNode> hn_f,
              std::unique_ptr<SnFNode> sn_f,
              ChiInterconnectConfig interconnect_config);

    void process_interconnect();
    void process_nodes();

    void step();

    uint64_t current_cycle() const;

    RnFNode* primary_rn_f();
    const RnFNode* primary_rn_f() const;

    bool has_pending_transactions() const;

    std::vector<std::unique_ptr<RnFNode>> rn_f_nodes;
    std::unique_ptr<HnFNode> hn_f;
    std::unique_ptr<SnFNode> sn_f;
    ChiInterconnect interconnect;

private:
    void deliver_message(NodeId dest, InFlightMessage& msg);
    RnFNode* find_rn_f(NodeId node_id);

    uint64_t current_cycle_ = 0;
};

} // namespace arm_cpu
