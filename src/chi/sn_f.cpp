/// @file sn_f.cpp
/// @brief SnFNode and MemoryModel implementation.

#include "arm_cpu/chi/sn_f.hpp"

#include <algorithm>

namespace arm_cpu {

// =====================================================================
// SnFNode
// =====================================================================

SnFNode::SnFNode(ChiNodeConfig config, uint64_t memory_latency, uint64_t memory_bandwidth)
    : node_id(NodeId(config.node_id))
    , config_(std::move(config))
    , req_channel_(16, "REQ")
    , rsp_channel_(16, "RSP")
    , dat_channel_(16, "DAT")
    , memory_latency_(memory_latency)
    , memory_bandwidth_(memory_bandwidth)
{}

void SnFNode::process_requests() {
    while (auto req = req_channel_.recv()) {
        handle_request(std::move(*req));
    }
}

void SnFNode::handle_request(ChiRequest req) {
    stats_.requests_received++;

    if (chi_is_read(req.req_type)) {
        stats_.read_requests++;
    } else if (chi_is_write(req.req_type)) {
        stats_.write_requests++;
    }

    if (bandwidth_usage_ + static_cast<uint64_t>(req.header.size) > memory_bandwidth_) {
        stats_.bandwidth_stalls++;
        MemoryRequest mem_req;
        mem_req.txn_id = req.header.txn_id;
        mem_req.req_type = req.req_type;
        mem_req.addr = req.header.addr;
        mem_req.size = req.header.size;
        mem_req.src_node = NodeId(req.header.src_id);
        mem_req.issue_cycle = current_cycle_;
        mem_req.complete_cycle = current_cycle_ + memory_latency_;
        request_queue_.push_back(std::move(mem_req));
        return;
    }

    process_memory_request(req);
}

void SnFNode::process_memory_request(const ChiRequest& req) {
    bandwidth_usage_ += static_cast<uint64_t>(req.header.size);

    MemoryRequest mem_req;
    mem_req.txn_id = req.header.txn_id;
    mem_req.req_type = req.req_type;
    mem_req.addr = req.header.addr;
    mem_req.size = req.header.size;
    mem_req.src_node = NodeId(req.header.src_id);
    mem_req.issue_cycle = current_cycle_;
    mem_req.complete_cycle = current_cycle_ + memory_latency_;

    pending_requests_.emplace(req.header.txn_id, std::move(mem_req));
    stats_.bytes_transferred += static_cast<uint64_t>(req.header.size);
}

void SnFNode::process_completions() {
    std::vector<ChiTxnId> completed;
    for (const auto& [id, req] : pending_requests_) {
        if (req.complete_cycle <= current_cycle_) {
            completed.push_back(id);
        }
    }

    for (auto id : completed) {
        auto it = pending_requests_.find(id);
        if (it != pending_requests_.end()) {
            auto req = std::move(it->second);
            pending_requests_.erase(it);
            send_response(req);
        }
    }

    process_queued_requests();
}

void SnFNode::process_queued_requests() {
    while (!request_queue_.empty()) {
        auto& front = request_queue_.front();
        if (bandwidth_usage_ + static_cast<uint64_t>(front.size) > memory_bandwidth_) {
            break;
        }

        auto req = std::move(request_queue_.front());
        request_queue_.pop_front();

        bandwidth_usage_ += static_cast<uint64_t>(req.size);
        req.complete_cycle = current_cycle_ + memory_latency_;

        pending_requests_.emplace(req.txn_id, std::move(req));
    }
}

void SnFNode::send_response(const MemoryRequest& req) {
    uint64_t latency = current_cycle_ >= req.issue_cycle
        ? current_cycle_ - req.issue_cycle : 0;
    stats_.total_latency += latency;

    if (chi_is_read(req.req_type)) {
        auto data = DataDescriptor::new_desc(req.size);
        auto data_msg = ChiData::comp_data(
            req.txn_id, node_id, req.src_node, req.addr, std::move(data));
        dat_channel_.send(std::move(data_msg));
        stats_.data_responses_sent++;
    } else {
        auto resp = ChiResponse::new_response(
            req.txn_id, ChiResponseType::Comp, node_id, req.src_node, req.addr);
        rsp_channel_.send(std::move(resp));
    }

    stats_.responses_sent++;
}

void SnFNode::advance_cycle() {
    ++current_cycle_;
    bandwidth_usage_ = 0;
}

uint64_t SnFNode::current_cycle() const { return current_cycle_; }
const SnFStats& SnFNode::stats() const { return stats_; }

double SnFNode::average_latency() const {
    if (stats_.responses_sent == 0) return 0.0;
    return static_cast<double>(stats_.total_latency) / static_cast<double>(stats_.responses_sent);
}

bool SnFNode::has_pending_requests() const { return !pending_requests_.empty(); }
std::size_t SnFNode::pending_count() const { return pending_requests_.size(); }
std::size_t SnFNode::queue_length() const { return request_queue_.size(); }

Channel<ChiRequest>&  SnFNode::req_channel_mut() { return req_channel_; }
Channel<ChiResponse>& SnFNode::rsp_channel_mut() { return rsp_channel_; }
Channel<ChiData>&     SnFNode::dat_channel_mut() { return dat_channel_; }

const Channel<ChiResponse>& SnFNode::rsp_channel() const { return rsp_channel_; }
const Channel<ChiData>&     SnFNode::dat_channel() const { return dat_channel_; }

// =====================================================================
// MemoryModel
// =====================================================================

MemoryModel::MemoryModel(uint64_t size, uint64_t latency, uint8_t banks, uint64_t bank_conflict_latency)
    : size_(size), latency_(latency), banks_(banks)
    , bank_conflict_latency_(bank_conflict_latency)
    , bank_last_access_(banks, 0)
{}

uint64_t MemoryModel::access_latency(uint64_t addr, uint64_t cycle) {
    uint8_t bank = get_bank(addr);
    uint64_t last_access = bank_last_access_[bank];

    uint64_t additional_latency = 0;
    if (last_access > cycle) {
        additional_latency = bank_conflict_latency_;
    }

    bank_last_access_[bank] = cycle + latency_ + additional_latency;
    return latency_ + additional_latency;
}

uint8_t MemoryModel::get_bank(uint64_t addr) const {
    return static_cast<uint8_t>((addr >> 3) & (static_cast<uint64_t>(banks_) - 1));
}

void MemoryModel::reset() {
    std::fill(bank_last_access_.begin(), bank_last_access_.end(), uint64_t{0});
}

} // namespace arm_cpu
