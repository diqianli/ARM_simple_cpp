#pragma once

/// @file hn_f.hpp
/// @brief Home Node - Fully coherent (HN-F) implementation.
///        The protocol controller managing cache coherence through a directory.

#include "arm_cpu/chi/chi_node.hpp"
#include "arm_cpu/chi/chi_directory.hpp"
#include "arm_cpu/chi/chi_qos.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace arm_cpu {

// =====================================================================
// PendingSnoop
// =====================================================================
struct PendingSnoop {
    ChiTxnId      txn_id;
    ChiRequest    request;
    uint32_t      responses_expected = 0;
    uint32_t      responses_received = 0;
    bool          data_received = false;
    std::optional<DataDescriptor> snoop_data;
    uint64_t      snoop_issue_cycle = 0;
};

// =====================================================================
// HnfTxnState
// =====================================================================
enum class HnfTxnState : uint8_t {
    Received,
    SnoopPending,
    MemoryPending,
    ReadyToRespond,
    WaitingCompAck,
    Complete,
};

// =====================================================================
// HnfTransaction
// =====================================================================
struct HnfTransaction {
    ChiTxnId                txn_id;
    ChiRequest              request;
    NodeId                  src_node;
    HnfTxnState             state = HnfTxnState::Received;
    std::optional<PendingSnoop> pending_snoop;
    std::optional<uint16_t> dbid;
    uint64_t                receive_cycle = 0;
};

// =====================================================================
// HnFStats
// =====================================================================
struct HnFStats {
    uint64_t requests_received      = 0;
    uint64_t read_requests          = 0;
    uint64_t write_requests         = 0;
    uint64_t snoops_sent            = 0;
    uint64_t snoop_responses        = 0;
    uint64_t memory_requests        = 0;
    uint64_t transactions_completed = 0;
    uint64_t dir_hits               = 0;
    uint64_t dir_misses             = 0;
};

// =====================================================================
// HnFNode — Home Node Fully Coherent
// =====================================================================
class HnFNode {
public:
    HnFNode(ChiNodeConfig config, std::size_t directory_size,
            std::size_t cache_line_size, NodeId sn_node_id,
            uint64_t memory_latency);

    void process_requests();
    void process_snoop_responses();
    void process_memory_responses();

    void advance_cycle();
    uint64_t current_cycle() const;

    const HnFStats&     stats() const;
    const DirectoryStats& dir_stats() const;

    bool        has_pending_transactions() const;
    std::size_t pending_count() const;

    // Channel accessors for interconnect
    Channel<ChiRequest>&   req_channel_mut();
    Channel<ChiResponse>&  rsp_channel_mut();
    Channel<ChiData>&      dat_channel_mut();
    Channel<ChiSnoop>&     snp_channel_mut();
    Channel<ChiSnoopResp>& snp_resp_channel_mut();
    Channel<ChiRequest>&   sn_req_channel_mut();
    Channel<ChiData>&      sn_dat_channel_mut();

    const Channel<ChiResponse>&  rsp_channel() const;
    const Channel<ChiData>&      dat_channel() const;
    const Channel<ChiSnoop>&     snp_channel() const;
    const Channel<ChiRequest>&   sn_req_channel() const;

    NodeId node_id;
    NodeId sn_node_id;

private:
    void handle_request(ChiRequest req);
    void handle_read_request(const ChiRequest& req, bool want_unique);
    void handle_unique_request(const ChiRequest& req);
    void handle_write_request(const ChiRequest& req);
    void handle_evict_request(const ChiRequest& req);
    void handle_clean_request(const ChiRequest& req);

    void request_from_memory(ChiTxnId txn_id, uint64_t addr, NodeId src_id, bool want_unique);
    void send_snoops(ChiTxnId txn_id, uint64_t addr, NodeId src_id,
                     bool want_unique, const std::vector<uint8_t>& sharers);
    void send_invalidate_snoops(ChiTxnId txn_id, uint64_t addr,
                                NodeId src_id, const std::vector<uint8_t>& sharers);
    void send_clean_snoops(ChiTxnId txn_id, uint64_t addr, NodeId src_id,
                           const std::vector<uint8_t>& sharers, ChiRequestTypeExt req_type);

    void grant_unique(ChiTxnId txn_id, uint64_t addr, NodeId src_id);
    void upgrade_state(ChiTxnId txn_id, uint64_t addr, NodeId src_id, bool want_unique);
    void send_dbid_response(ChiTxnId txn_id, uint64_t addr, NodeId src_id, uint16_t dbid);

    void handle_snoop_response(const ChiSnoopResp& resp);
    void complete_snoop_phase(ChiTxnId txn_id, uint64_t addr);
    void handle_memory_data(const ChiData& data);

    ChiNodeConfig config_;
    Directory     directory_;

    Channel<ChiRequest>   req_channel_;
    Channel<ChiResponse>  rsp_channel_;
    Channel<ChiData>      dat_channel_;
    Channel<ChiSnoop>     snp_channel_;
    Channel<ChiSnoopResp> snp_resp_channel_;
    Channel<ChiRequest>   sn_req_channel_;
    Channel<ChiResponse>  sn_rsp_channel_;
    Channel<ChiData>      sn_dat_channel_;

    std::unordered_map<ChiTxnId, HnfTransaction, ChiTxnId::Hash> transactions_;
    DbidAllocator  dbid_allocator_;
    QosCreditManager qos_manager_;

    uint64_t current_cycle_ = 0;
    uint64_t memory_latency_;
    HnFStats stats_;
};

} // namespace arm_cpu
