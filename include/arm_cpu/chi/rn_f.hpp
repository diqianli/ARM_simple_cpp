#pragma once

/// @file rn_f.hpp
/// @brief Request Node - Fully coherent (RN-F) implementation.
///        Represents a CPU core with L1/L2 caches participating in CHI.

#include "arm_cpu/chi/chi_node.hpp"
#include "arm_cpu/chi/chi_coherence.hpp"
#include "arm_cpu/chi/chi_qos.hpp"
#include "arm_cpu/memory/cache.hpp"

#include <cstdint>
#include <deque>
#include <unordered_map>

namespace arm_cpu {

// =====================================================================
// OutstandingTxn
// =====================================================================
struct OutstandingTxn {
    ChiTxnId          txn_id;
    InstructionId     instr_id;
    ChiRequestTypeExt req_type;
    uint64_t          addr = 0;
    uint64_t          issue_cycle = 0;
    bool              waiting_data = false;
    bool              waiting_dbid = false;
    std::optional<uint16_t> dbid;
};

// =====================================================================
// StoreBufferEntry
// =====================================================================
struct StoreBufferEntry {
    InstructionId instr_id;
    uint64_t      addr = 0;
    uint8_t       size = 0;
    uint64_t      issue_cycle = 0;
    bool          committed = false;
};

// =====================================================================
// RnFStats
// =====================================================================
struct RnFStats {
    uint64_t read_requests          = 0;
    uint64_t write_requests         = 0;
    uint64_t snoops_received        = 0;
    uint64_t snoop_responses        = 0;
    uint64_t l1_hits                = 0;
    uint64_t l1_misses              = 0;
    uint64_t l2_hits                = 0;
    uint64_t l2_misses              = 0;
    uint64_t transactions_completed = 0;
    uint64_t retries                = 0;
};

// =====================================================================
// RnFNode — Request Node Fully Coherent
// =====================================================================
class RnFNode {
public:
    static Result<std::unique_ptr<RnFNode>> create(
        ChiNodeConfig config,
        CacheConfig l1_config,
        CacheConfig l2_config,
        NodeId home_node_id);

    std::optional<uint64_t> load(InstructionId instr_id, uint64_t addr, uint8_t size);
    std::optional<uint64_t> store(InstructionId instr_id, uint64_t addr, uint8_t size);

    std::optional<ChiTxnId> send_read_request(
        InstructionId instr_id, uint64_t addr, uint8_t size, bool want_unique);

    std::optional<ChiTxnId> send_write_request(
        InstructionId instr_id, uint64_t addr, uint8_t size);

    void handle_response(const ChiResponse& resp);
    void handle_data(const ChiData& data);
    void handle_snoop(const ChiSnoop& snoop);
    void process_retries();

    void advance_cycle();
    uint64_t current_cycle() const;

    const RnFStats&    stats() const;
    const CacheStats&  l1_stats() const;
    const CacheStats&  l2_stats() const;

    bool        has_pending_transactions() const;
    std::size_t outstanding_count() const;

    // Channel accessors for interconnect
    Channel<ChiRequest>&   req_channel_mut();
    Channel<ChiResponse>&  rsp_channel_mut();
    Channel<ChiData>&      dat_channel_mut();
    Channel<ChiSnoop>&     snp_channel_mut();
    Channel<ChiSnoopResp>& snp_resp_channel_mut();

    const Channel<ChiRequest>&   req_channel() const;
    const Channel<ChiResponse>&  rsp_channel() const;
    const Channel<ChiData>&      dat_channel() const;
    const Channel<ChiSnoop>&     snp_channel() const;
    const Channel<ChiSnoopResp>& snp_resp_channel() const;

    NodeId node_id;
    NodeId home_node_id;

private:
    RnFNode(ChiNodeConfig config, std::unique_ptr<Cache> l1,
            std::unique_ptr<Cache> l2, NodeId home_node_id);

    ChiTxnId alloc_txn_id();
    std::optional<ChiTxnId> send_request(
        InstructionId instr_id, ChiRequestTypeExt req_type,
        uint64_t addr, uint8_t size);
    void complete_transaction(ChiTxnId txn_id, uint64_t addr);
    ChiCacheState get_chi_state(uint64_t addr) const;
    void          set_chi_state(uint64_t addr, ChiCacheState state);

    ChiNodeConfig                        config_;
    std::unique_ptr<Cache>               l1_cache_;
    std::unique_ptr<Cache>               l2_cache_;
    std::unordered_map<uint64_t, ChiCacheState> chi_states_;

    Channel<ChiRequest>   req_channel_;
    Channel<ChiResponse>  rsp_channel_;
    Channel<ChiData>      dat_channel_;
    Channel<ChiSnoop>     snp_channel_;
    Channel<ChiSnoopResp> snp_resp_channel_;

    std::unordered_map<ChiTxnId, OutstandingTxn, ChiTxnId::Hash> outstanding_;
    uint16_t              next_txn_id_ = 0;

    QosCreditManager                   qos_manager_;
    std::deque<PendingRequest>         retry_queue_;
    std::deque<StoreBufferEntry>       store_buffer_;

    uint64_t current_cycle_ = 0;
    RnFStats stats_;
};

} // namespace arm_cpu
