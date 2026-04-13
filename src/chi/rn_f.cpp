/// @file rn_f.cpp
/// @brief RnFNode — Request Node Fully Coherent implementation.

#include "arm_cpu/chi/rn_f.hpp"

namespace arm_cpu {

// =====================================================================
// RnFNode
// =====================================================================

RnFNode::RnFNode(ChiNodeConfig config, std::unique_ptr<Cache> l1,
                 std::unique_ptr<Cache> l2, NodeId home_node_id)
    : node_id(NodeId(config.node_id))
    , home_node_id(home_node_id)
    , config_(std::move(config))
    , l1_cache_(std::move(l1))
    , l2_cache_(std::move(l2))
    , req_channel_(16, "REQ")
    , rsp_channel_(16, "RSP")
    , dat_channel_(8, "DAT")
    , snp_channel_(8, "SNP")
    , snp_resp_channel_(8, "SNP_RSP")
    , qos_manager_(16, 32, 64)
{}

Result<std::unique_ptr<RnFNode>> RnFNode::create(
    ChiNodeConfig config,
    CacheConfig l1_config,
    CacheConfig l2_config,
    NodeId home_node_id)
{
    auto l1 = Cache::create(std::move(l1_config));
    if (l1.has_error()) return l1.error();

    auto l2 = Cache::create(std::move(l2_config));
    if (l2.has_error()) return l2.error();

    return std::unique_ptr<RnFNode>(new RnFNode(
        config, std::move(l1).value(), std::move(l2).value(), home_node_id));
}

ChiTxnId RnFNode::alloc_txn_id() {
    uint16_t id = next_txn_id_;
    next_txn_id_ = static_cast<uint16_t>(next_txn_id_ + 1);
    return ChiTxnId{id};
}

std::optional<uint64_t> RnFNode::load(InstructionId instr_id, uint64_t addr, uint8_t size) {
    auto l1_result = l1_cache_->access(addr, true);
    bool l1_hit = l1_result.ok() && l1_result.value();

    if (l1_hit) {
        stats_.l1_hits++;
        return current_cycle_ + l1_cache_->hit_latency();
    }

    stats_.l1_misses++;

    auto l2_result = l2_cache_->access(addr, true);
    bool l2_hit = l2_result.ok() && l2_result.value();

    if (l2_hit) {
        stats_.l2_hits++;
        auto state = get_chi_state(addr);
        if (chi_can_read(state)) {
            l1_cache_->fill_line(addr);
            uint64_t latency = l1_cache_->hit_latency() + l2_cache_->hit_latency();
            return current_cycle_ + latency;
        }
    } else {
        stats_.l2_misses++;
    }

    return std::nullopt;
}

std::optional<uint64_t> RnFNode::store(InstructionId instr_id, uint64_t addr, uint8_t size) {
    auto l1_result = l1_cache_->access(addr, false);
    bool l1_hit = l1_result.ok() && l1_result.value();

    if (l1_hit) {
        auto state = get_chi_state(addr);
        if (chi_can_write(state)) {
            return current_cycle_ + 1;
        }
    }

    store_buffer_.push_back({
        instr_id, addr, size, current_cycle_, false
    });

    return std::nullopt;
}

std::optional<ChiTxnId> RnFNode::send_read_request(
    InstructionId instr_id, uint64_t addr, uint8_t size, bool want_unique)
{
    auto req_type = want_unique ? ChiRequestTypeExt::ReadMakeUnique
                                : ChiRequestTypeExt::ReadShared;
    return send_request(instr_id, req_type, addr, size);
}

std::optional<ChiTxnId> RnFNode::send_write_request(
    InstructionId instr_id, uint64_t addr, uint8_t size)
{
    return send_request(instr_id, ChiRequestTypeExt::MakeUnique, addr, size);
}

std::optional<ChiTxnId> RnFNode::send_request(
    InstructionId instr_id, ChiRequestTypeExt req_type,
    uint64_t addr, uint8_t size)
{
    auto pcrd_type = QosCreditManager::get_pcrd_type(req_type);
    if (!qos_manager_.has_credit(pcrd_type)) {
        PendingRequest pending;
        pending.instruction_id = instr_id;
        pending.txn_id = std::nullopt;
        pending.request_type = req_type;
        pending.addr = addr;
        pending.size = size;
        pending.first_attempt_cycle = current_cycle_;
        pending.retry_count = 0;
        pending.required_pcrd = pcrd_type;
        retry_queue_.push_back(std::move(pending));
        stats_.retries++;
        return std::nullopt;
    }

    ChiTxnId txn_id = alloc_txn_id();

    auto request = ChiRequest::new_request(
        txn_id, node_id, home_node_id, req_type, addr, size, instr_id);

    OutstandingTxn txn;
    txn.txn_id = txn_id;
    txn.instr_id = instr_id;
    txn.req_type = req_type;
    txn.addr = addr;
    txn.issue_cycle = current_cycle_;
    txn.waiting_data = chi_requires_data(req_type);
    txn.waiting_dbid = chi_is_write(req_type);
    txn.dbid = std::nullopt;
    outstanding_.emplace(txn_id, std::move(txn));

    if (req_channel_.send(std::move(request))) {
        if (chi_is_read(req_type)) {
            stats_.read_requests++;
        } else {
            stats_.write_requests++;
        }
        return txn_id;
    }

    outstanding_.erase(txn_id);
    return std::nullopt;
}

void RnFNode::handle_response(const ChiResponse& resp) {
    auto it = outstanding_.find(resp.txn_id);
    if (it == outstanding_.end()) return;

    switch (resp.resp_type) {
        case ChiResponseType::CompData:
            it->second.waiting_data = false;
            break;
        case ChiResponseType::DBIDResp:
            it->second.waiting_dbid = false;
            it->second.dbid = resp.dbid;
            break;
        case ChiResponseType::Comp:
            it->second.waiting_data = false;
            it->second.waiting_dbid = false;
            break;
        case ChiResponseType::CompAck:
            break;
        default:
            break;
    }

    // Re-find after potential map rehash is not needed since we didn't insert/erase
    auto& txn = outstanding_.at(resp.txn_id);
    if (!txn.waiting_data && !txn.waiting_dbid) {
        complete_transaction(resp.txn_id, resp.addr);
    }
}

void RnFNode::handle_data(const ChiData& data) {
    auto it = outstanding_.find(data.txn_id);
    if (it == outstanding_.end()) return;

    auto req_type = it->second.req_type;
    bool waiting_dbid = it->second.waiting_dbid;

    it->second.waiting_data = false;

    l2_cache_->fill_line(data.addr);
    l1_cache_->fill_line(data.addr);

    ChiCacheState state;
    if (data.data.dirty) {
        state = ChiCacheState::UniqueDirty;
    } else if (req_type == ChiRequestTypeExt::ReadMakeUnique) {
        state = ChiCacheState::UniqueClean;
    } else {
        state = ChiCacheState::SharedClean;
    }
    set_chi_state(data.addr, state);

    if (!waiting_dbid) {
        complete_transaction(data.txn_id, data.addr);
    }

    auto ack = ChiResponse::comp_ack(data.txn_id, node_id, home_node_id, data.addr);
    rsp_channel_.send(std::move(ack));
}

void RnFNode::handle_snoop(const ChiSnoop& snoop) {
    stats_.snoops_received++;

    auto current_state = get_chi_state(snoop.addr);
    auto coherence_resp = CoherenceStateMachine::on_snoop_request(current_state, snoop.snoop_type);

    set_chi_state(snoop.addr, coherence_resp.final_state);

    if (coherence_resp.final_state == ChiCacheState::Invalid) {
        l2_cache_->invalidate(snoop.addr);
        l1_cache_->invalidate(snoop.addr);
    }

    ChiSnoopResp snoop_resp;
    if (coherence_resp.data_valid) {
        auto desc = DataDescriptor::new_desc(64)
            .with_dirty(coherence_resp.data_dirty)
            .with_state(coherence_resp.final_state);
        snoop_resp = ChiSnoopResp::with_data(
            snoop.txn_id, node_id, snoop.src_id, snoop.addr,
            std::move(desc), coherence_resp.final_state);
    } else {
        snoop_resp = ChiSnoopResp::ack(
            snoop.txn_id, node_id, snoop.src_id, snoop.addr,
            coherence_resp.final_state);
    }

    snp_resp_channel_.send(std::move(snoop_resp));
    stats_.snoop_responses++;
}

void RnFNode::complete_transaction(ChiTxnId txn_id, uint64_t /*addr*/) {
    auto it = outstanding_.find(txn_id);
    if (it == outstanding_.end()) return;

    auto txn = std::move(it->second);
    outstanding_.erase(it);

    stats_.transactions_completed++;

    auto pcrd_type = QosCreditManager::get_pcrd_type(txn.req_type);
    qos_manager_.return_credit(pcrd_type);

    if (txn.dbid.has_value()) {
        qos_manager_.free_dbid(txn.dbid.value());
    }
}

void RnFNode::process_retries() {
    auto granted = qos_manager_.process_retries(current_cycle_);
    for (auto& pending : granted) {
        send_request(pending.instruction_id, pending.request_type,
                     pending.addr, pending.size);
    }
}

ChiCacheState RnFNode::get_chi_state(uint64_t addr) const {
    uint64_t line_size = l2_cache_->config().line_size;
    uint64_t aligned = addr & ~((static_cast<uint64_t>(line_size) - 1));
    auto it = chi_states_.find(aligned);
    return it != chi_states_.end() ? it->second : ChiCacheState::Invalid;
}

void RnFNode::set_chi_state(uint64_t addr, ChiCacheState state) {
    uint64_t line_size = l2_cache_->config().line_size;
    uint64_t aligned = addr & ~((static_cast<uint64_t>(line_size) - 1));
    if (state == ChiCacheState::Invalid) {
        chi_states_.erase(aligned);
    } else {
        chi_states_[aligned] = state;
    }
}

void RnFNode::advance_cycle() { ++current_cycle_; }
uint64_t RnFNode::current_cycle() const { return current_cycle_; }
const RnFStats& RnFNode::stats() const { return stats_; }
const CacheStats& RnFNode::l1_stats() const { return l1_cache_->stats(); }
const CacheStats& RnFNode::l2_stats() const { return l2_cache_->stats(); }
bool RnFNode::has_pending_transactions() const { return !outstanding_.empty(); }
std::size_t RnFNode::outstanding_count() const { return outstanding_.size(); }

Channel<ChiRequest>&   RnFNode::req_channel_mut()   { return req_channel_; }
Channel<ChiResponse>&  RnFNode::rsp_channel_mut()   { return rsp_channel_; }
Channel<ChiData>&      RnFNode::dat_channel_mut()   { return dat_channel_; }
Channel<ChiSnoop>&     RnFNode::snp_channel_mut()   { return snp_channel_; }
Channel<ChiSnoopResp>& RnFNode::snp_resp_channel_mut() { return snp_resp_channel_; }

const Channel<ChiRequest>&   RnFNode::req_channel() const { return req_channel_; }
const Channel<ChiResponse>&  RnFNode::rsp_channel() const { return rsp_channel_; }
const Channel<ChiData>&      RnFNode::dat_channel() const { return dat_channel_; }
const Channel<ChiSnoop>&     RnFNode::snp_channel() const { return snp_channel_; }
const Channel<ChiSnoopResp>& RnFNode::snp_resp_channel() const { return snp_resp_channel_; }

} // namespace arm_cpu
