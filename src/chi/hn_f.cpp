/// @file hn_f.cpp
/// @brief HnFNode — Home Node Fully Coherent implementation.

#include "arm_cpu/chi/hn_f.hpp"

namespace arm_cpu {

// =====================================================================
// HnFNode
// =====================================================================

HnFNode::HnFNode(ChiNodeConfig config, std::size_t directory_size,
                 std::size_t cache_line_size, NodeId sn_node_id,
                 uint64_t memory_latency)
    : node_id(NodeId(config.node_id))
    , sn_node_id(sn_node_id)
    , config_(std::move(config))
    , directory_(cache_line_size, directory_size)
    , req_channel_(32, "REQ")
    , rsp_channel_(32, "RSP")
    , dat_channel_(16, "DAT")
    , snp_channel_(16, "SNP")
    , snp_resp_channel_(16, "SNP_RSP")
    , sn_req_channel_(16, "SN_REQ")
    , sn_rsp_channel_(16, "SN_RSP")
    , sn_dat_channel_(16, "SN_DAT")
    , dbid_allocator_(64)
    , qos_manager_(32, 64, 128)
    , memory_latency_(memory_latency)
{}

void HnFNode::process_requests() {
    while (auto req = req_channel_.recv()) {
        handle_request(std::move(*req));
    }
}

void HnFNode::handle_request(ChiRequest req) {
    stats_.requests_received++;

    if (chi_is_read(req.req_type)) {
        stats_.read_requests++;
    } else if (chi_is_write(req.req_type)) {
        stats_.write_requests++;
    }

    HnfTransaction txn;
    txn.txn_id = req.header.txn_id;
    txn.request = req;
    txn.src_node = NodeId(req.header.src_id);
    txn.state = HnfTxnState::Received;
    txn.pending_snoop = std::nullopt;
    txn.dbid = std::nullopt;
    txn.receive_cycle = current_cycle_;

    transactions_.emplace(req.header.txn_id, std::move(txn));

    directory_.start_transaction(req.header.addr, req.header.txn_id);

    switch (req.req_type) {
        case ChiRequestTypeExt::ReadShared:
        case ChiRequestTypeExt::ReadNotSharedDirty:
            handle_read_request(req, false);
            break;
        case ChiRequestTypeExt::ReadMakeUnique:
        case ChiRequestTypeExt::ReadOnce:
        case ChiRequestTypeExt::ReadNoSnoop:
            handle_read_request(req, true);
            break;
        case ChiRequestTypeExt::MakeUnique:
        case ChiRequestTypeExt::CleanUnique:
            handle_unique_request(req);
            break;
        case ChiRequestTypeExt::WriteUnique:
        case ChiRequestTypeExt::WriteNoSnoop:
            handle_write_request(req);
            break;
        case ChiRequestTypeExt::Evict:
            handle_evict_request(req);
            break;
        case ChiRequestTypeExt::CleanShared:
        case ChiRequestTypeExt::CleanInvalid:
        case ChiRequestTypeExt::MakeInvalid:
            handle_clean_request(req);
            break;
        default:
            break;
    }
}

void HnFNode::handle_read_request(const ChiRequest& req, bool want_unique) {
    ChiTxnId txn_id = req.header.txn_id;
    uint64_t addr = req.header.addr;
    NodeId src_id(req.header.src_id);

    auto sharers = directory_.get_sharers(addr);

    if (sharers.empty()) {
        stats_.dir_misses++;
        request_from_memory(txn_id, addr, src_id, want_unique);
    } else if (sharers.size() == 1 && sharers[0] == src_id.value) {
        stats_.dir_hits++;
        upgrade_state(txn_id, addr, src_id, want_unique);
    } else {
        stats_.dir_hits++;
        send_snoops(txn_id, addr, src_id, want_unique, sharers);
    }
}

void HnFNode::handle_unique_request(const ChiRequest& req) {
    ChiTxnId txn_id = req.header.txn_id;
    uint64_t addr = req.header.addr;
    NodeId src_id(req.header.src_id);

    auto sharers = directory_.get_snoop_targets(addr, src_id.value);

    if (sharers.empty()) {
        grant_unique(txn_id, addr, src_id);
    } else {
        send_invalidate_snoops(txn_id, addr, src_id, sharers);
    }
}

void HnFNode::handle_write_request(const ChiRequest& req) {
    ChiTxnId txn_id = req.header.txn_id;
    uint64_t addr = req.header.addr;
    NodeId src_id(req.header.src_id);

    auto dbid = dbid_allocator_.allocate();

    if (dbid.has_value()) {
        auto sharers = directory_.get_snoop_targets(addr, src_id.value);

        if (sharers.empty()) {
            send_dbid_response(txn_id, addr, src_id, dbid.value());
        } else {
            auto it = transactions_.find(txn_id);
            if (it != transactions_.end()) {
                it->second.dbid = dbid;
            }
            send_invalidate_snoops(txn_id, addr, src_id, sharers);
        }
    }
    // If no DBID available, retry later (simplified)
}

void HnFNode::handle_evict_request(const ChiRequest& req) {
    uint64_t addr = req.header.addr;
    NodeId src_id(req.header.src_id);

    bool dirty = directory_.is_dirty(addr);
    directory_.on_evict(addr, src_id.value, dirty);

    auto resp = ChiResponse::new_response(
        req.header.txn_id, ChiResponseType::Comp, node_id, src_id, addr);
    rsp_channel_.send(std::move(resp));

    transactions_.erase(req.header.txn_id);
    directory_.complete_transaction(addr);
    stats_.transactions_completed++;
}

void HnFNode::handle_clean_request(const ChiRequest& req) {
    ChiTxnId txn_id = req.header.txn_id;
    uint64_t addr = req.header.addr;
    NodeId src_id(req.header.src_id);

    auto sharers = directory_.get_snoop_targets(addr, src_id.value);

    if (sharers.empty()) {
        auto resp = ChiResponse::new_response(
            txn_id, ChiResponseType::Comp, node_id, src_id, addr);
        rsp_channel_.send(std::move(resp));
        transactions_.erase(txn_id);
        directory_.complete_transaction(addr);
        stats_.transactions_completed++;
    } else {
        send_clean_snoops(txn_id, addr, src_id, sharers, req.req_type);
    }
}

void HnFNode::request_from_memory(ChiTxnId txn_id, uint64_t addr, NodeId src_id, bool /*want_unique*/) {
    auto it = transactions_.find(txn_id);
    if (it != transactions_.end()) {
        it->second.state = HnfTxnState::MemoryPending;
    }

    auto mem_req = ChiRequest::new_request(
        txn_id, node_id, sn_node_id,
        ChiRequestTypeExt::ReadNoSnoop, addr, 64, InstructionId{0});

    sn_req_channel_.send(std::move(mem_req));
    stats_.memory_requests++;
}

void HnFNode::send_snoops(ChiTxnId txn_id, uint64_t addr, NodeId src_id,
                           bool want_unique, const std::vector<uint8_t>& sharers)
{
    auto snoop_type = want_unique ? ChiSnoopType::SnpClean : ChiSnoopType::SnpShared;

    PendingSnoop pending;
    pending.txn_id = txn_id;
    auto it = transactions_.find(txn_id);
    pending.request = it->second.request;
    pending.responses_expected = static_cast<uint32_t>(sharers.size());
    pending.responses_received = 0;
    pending.data_received = false;
    pending.snoop_data = std::nullopt;
    pending.snoop_issue_cycle = current_cycle_;

    if (it != transactions_.end()) {
        it->second.state = HnfTxnState::SnoopPending;
        it->second.pending_snoop = std::move(pending);
    }

    for (auto sharer_id : sharers) {
        auto snoop = ChiSnoop::new_snoop(
            snoop_type, txn_id, node_id, NodeId(sharer_id), addr);
        snp_channel_.send(std::move(snoop));
        stats_.snoops_sent++;
    }
}

void HnFNode::send_invalidate_snoops(ChiTxnId txn_id, uint64_t addr,
                                      NodeId src_id, const std::vector<uint8_t>& sharers)
{
    PendingSnoop pending;
    pending.txn_id = txn_id;
    auto it = transactions_.find(txn_id);
    pending.request = it->second.request;
    pending.responses_expected = static_cast<uint32_t>(sharers.size());
    pending.responses_received = 0;
    pending.data_received = false;
    pending.snoop_data = std::nullopt;
    pending.snoop_issue_cycle = current_cycle_;

    if (it != transactions_.end()) {
        it->second.state = HnfTxnState::SnoopPending;
        it->second.pending_snoop = std::move(pending);
    }

    for (auto sharer_id : sharers) {
        auto snoop = ChiSnoop::new_snoop(
            ChiSnoopType::SnpMakeInvalid, txn_id, node_id, NodeId(sharer_id), addr);
        snp_channel_.send(std::move(snoop));
        stats_.snoops_sent++;
    }
}

void HnFNode::send_clean_snoops(ChiTxnId txn_id, uint64_t addr, NodeId src_id,
                                const std::vector<uint8_t>& sharers, ChiRequestTypeExt req_type)
{
    auto snoop_type = (req_type == ChiRequestTypeExt::CleanInvalid ||
                       req_type == ChiRequestTypeExt::MakeInvalid)
                      ? ChiSnoopType::SnpMakeInvalid
                      : ChiSnoopType::SnpCleanShared;

    PendingSnoop pending;
    pending.txn_id = txn_id;
    auto it = transactions_.find(txn_id);
    pending.request = it->second.request;
    pending.responses_expected = static_cast<uint32_t>(sharers.size());
    pending.responses_received = 0;
    pending.data_received = false;
    pending.snoop_data = std::nullopt;
    pending.snoop_issue_cycle = current_cycle_;

    if (it != transactions_.end()) {
        it->second.state = HnfTxnState::SnoopPending;
        it->second.pending_snoop = std::move(pending);
    }

    for (auto sharer_id : sharers) {
        auto snoop = ChiSnoop::new_snoop(
            snoop_type, txn_id, node_id, NodeId(sharer_id), addr);
        snp_channel_.send(std::move(snoop));
        stats_.snoops_sent++;
    }
}

void HnFNode::grant_unique(ChiTxnId txn_id, uint64_t addr, NodeId src_id) {
    directory_.set_owner(addr, src_id.value);

    auto data = DataDescriptor::new_desc(64).with_state(ChiCacheState::UniqueClean);
    auto data_msg = ChiData::comp_data(txn_id, node_id, src_id, addr, std::move(data));
    dat_channel_.send(std::move(data_msg));

    directory_.complete_transaction(addr);
    transactions_.erase(txn_id);
    stats_.transactions_completed++;
}

void HnFNode::upgrade_state(ChiTxnId txn_id, uint64_t addr, NodeId src_id, bool want_unique) {
    auto state = want_unique ? ChiCacheState::UniqueClean : ChiCacheState::SharedClean;
    auto data = DataDescriptor::new_desc(64).with_state(state);
    auto data_msg = ChiData::comp_data(txn_id, node_id, src_id, addr, std::move(data));
    dat_channel_.send(std::move(data_msg));

    directory_.complete_transaction(addr);
    transactions_.erase(txn_id);
    stats_.transactions_completed++;
}

void HnFNode::send_dbid_response(ChiTxnId txn_id, uint64_t addr, NodeId src_id, uint16_t dbid) {
    directory_.set_owner(addr, src_id.value);
    directory_.set_dirty(addr, true);

    auto resp = ChiResponse::dbid_response(txn_id, node_id, src_id, dbid);
    rsp_channel_.send(std::move(resp));

    auto it = transactions_.find(txn_id);
    if (it != transactions_.end()) {
        it->second.state = HnfTxnState::WaitingCompAck;
    }
}

void HnFNode::process_snoop_responses() {
    while (auto resp = snp_resp_channel_.recv()) {
        handle_snoop_response(std::move(*resp));
    }
}

void HnFNode::handle_snoop_response(const ChiSnoopResp& resp) {
    stats_.snoop_responses++;

    auto it = transactions_.find(resp.txn_id);
    if (it == transactions_.end()) return;

    auto& txn = it->second;
    if (!txn.pending_snoop.has_value()) return;

    auto& pending = txn.pending_snoop.value();
    pending.responses_received++;

    if (resp.data_valid) {
        pending.data_received = true;
        pending.snoop_data = resp.data;
    }

    directory_.remove_sharer(resp.addr, resp.src_id.value);

    if (pending.responses_received >= pending.responses_expected) {
        complete_snoop_phase(resp.txn_id, resp.addr);
    }
}

void HnFNode::complete_snoop_phase(ChiTxnId txn_id, uint64_t addr) {
    auto it = transactions_.find(txn_id);
    if (it == transactions_.end()) return;

    auto src_id = it->second.src_node;
    auto snoop_data = it->second.pending_snoop.has_value()
                      ? it->second.pending_snoop->snoop_data : std::nullopt;
    auto dbid = it->second.dbid;

    if (snoop_data.has_value()) {
        auto data_msg = ChiData::comp_data(txn_id, node_id, src_id, addr, snoop_data.value());
        dat_channel_.send(std::move(data_msg));

        directory_.add_sharer(addr, src_id.value);
        directory_.complete_transaction(addr);

        transactions_.erase(txn_id);
        stats_.transactions_completed++;
    } else if (dbid.has_value()) {
        send_dbid_response(txn_id, addr, src_id, dbid.value());
    } else {
        bool want_unique = (it->second.request.req_type == ChiRequestTypeExt::ReadMakeUnique ||
                            it->second.request.req_type == ChiRequestTypeExt::MakeUnique);
        request_from_memory(txn_id, addr, src_id, want_unique);
    }
}

void HnFNode::process_memory_responses() {
    while (auto data = sn_dat_channel_.recv()) {
        handle_memory_data(std::move(*data));
    }
}

void HnFNode::handle_memory_data(const ChiData& data) {
    auto it = transactions_.find(data.txn_id);
    if (it == transactions_.end()) return;

    auto src_id = it->second.src_node;
    auto addr = data.addr;

    bool want_unique = (it->second.request.req_type == ChiRequestTypeExt::ReadMakeUnique ||
                        it->second.request.req_type == ChiRequestTypeExt::ReadOnce);

    auto state = want_unique ? ChiCacheState::UniqueClean : ChiCacheState::SharedClean;
    auto resp_data = DataDescriptor::new_desc(64).with_state(state);
    auto data_msg = ChiData::comp_data(data.txn_id, node_id, src_id, addr, std::move(resp_data));
    dat_channel_.send(std::move(data_msg));

    directory_.add_sharer(addr, src_id.value);
    directory_.complete_transaction(addr);

    transactions_.erase(data.txn_id);
    stats_.transactions_completed++;
}

void HnFNode::advance_cycle() { ++current_cycle_; }
uint64_t HnFNode::current_cycle() const { return current_cycle_; }
const HnFStats& HnFNode::stats() const { return stats_; }
const DirectoryStats& HnFNode::dir_stats() const { return directory_.stats(); }
bool HnFNode::has_pending_transactions() const { return !transactions_.empty(); }
std::size_t HnFNode::pending_count() const { return transactions_.size(); }

Channel<ChiRequest>&   HnFNode::req_channel_mut()   { return req_channel_; }
Channel<ChiResponse>&  HnFNode::rsp_channel_mut()   { return rsp_channel_; }
Channel<ChiData>&      HnFNode::dat_channel_mut()   { return dat_channel_; }
Channel<ChiSnoop>&     HnFNode::snp_channel_mut()   { return snp_channel_; }
Channel<ChiSnoopResp>& HnFNode::snp_resp_channel_mut() { return snp_resp_channel_; }
Channel<ChiRequest>&   HnFNode::sn_req_channel_mut() { return sn_req_channel_; }
Channel<ChiData>&      HnFNode::sn_dat_channel_mut() { return sn_dat_channel_; }

const Channel<ChiResponse>& HnFNode::rsp_channel() const { return rsp_channel_; }
const Channel<ChiData>&     HnFNode::dat_channel() const { return dat_channel_; }
const Channel<ChiSnoop>&    HnFNode::snp_channel() const { return snp_channel_; }
const Channel<ChiRequest>&  HnFNode::sn_req_channel() const { return sn_req_channel_; }

} // namespace arm_cpu
