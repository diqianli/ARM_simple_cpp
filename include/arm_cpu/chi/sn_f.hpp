#pragma once

/// @file sn_f.hpp
/// @brief Subordinate Node - Fully coherent (SN-F) implementation.
///        Represents memory or downstream devices responding to HN-F requests.

#include "arm_cpu/chi/chi_node.hpp"

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

namespace arm_cpu {

// =====================================================================
// MemoryRequest
// =====================================================================
struct MemoryRequest {
    ChiTxnId         txn_id;
    ChiRequestTypeExt req_type;
    uint64_t         addr = 0;
    uint8_t          size = 0;
    NodeId           src_node;
    uint64_t         issue_cycle = 0;
    uint64_t         complete_cycle = 0;
};

// =====================================================================
// SnFStats
// =====================================================================
struct SnFStats {
    uint64_t requests_received    = 0;
    uint64_t read_requests        = 0;
    uint64_t write_requests       = 0;
    uint64_t responses_sent       = 0;
    uint64_t data_responses_sent  = 0;
    uint64_t bytes_transferred    = 0;
    uint64_t total_latency        = 0;
    uint64_t bandwidth_stalls     = 0;
};

// =====================================================================
// SnFNode — Subordinate Node Fully Coherent
// =====================================================================
class SnFNode {
public:
    SnFNode(ChiNodeConfig config, uint64_t memory_latency, uint64_t memory_bandwidth);

    void process_requests();
    void process_completions();

    void advance_cycle();
    uint64_t current_cycle() const;

    const SnFStats& stats() const;
    double average_latency() const;

    bool        has_pending_requests() const;
    std::size_t pending_count() const;
    std::size_t queue_length() const;

    // Channel accessors for interconnect
    Channel<ChiRequest>&  req_channel_mut();
    Channel<ChiResponse>& rsp_channel_mut();
    Channel<ChiData>&     dat_channel_mut();

    const Channel<ChiResponse>& rsp_channel() const;
    const Channel<ChiData>&     dat_channel() const;

    NodeId node_id;

private:
    void handle_request(ChiRequest req);
    void process_memory_request(const ChiRequest& req);
    void process_queued_requests();
    void send_response(const MemoryRequest& req);

    ChiNodeConfig config_;

    Channel<ChiRequest>  req_channel_;
    Channel<ChiResponse> rsp_channel_;
    Channel<ChiData>     dat_channel_;

    std::unordered_map<ChiTxnId, MemoryRequest, ChiTxnId::Hash> pending_requests_;
    std::deque<MemoryRequest> request_queue_;

    uint64_t current_cycle_ = 0;
    uint64_t memory_latency_;
    uint64_t memory_bandwidth_;
    uint64_t bandwidth_usage_ = 0;
    SnFStats stats_;
};

// =====================================================================
// MemoryModel — simple banked memory model for SN-F
// =====================================================================
class MemoryModel {
public:
    MemoryModel(uint64_t size, uint64_t latency, uint8_t banks, uint64_t bank_conflict_latency);

    uint64_t access_latency(uint64_t addr, uint64_t cycle);
    void     reset();

private:
    uint8_t get_bank(uint64_t addr) const;

    uint64_t           size_;
    uint64_t           latency_;
    uint8_t            banks_;
    uint64_t           bank_conflict_latency_;
    std::vector<uint64_t> bank_last_access_;
};

} // namespace arm_cpu
