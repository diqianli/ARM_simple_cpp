/// @file chi_interconnect.cpp
/// @brief ChiInterconnect and ChiSystem implementation.

#include "arm_cpu/chi/chi_interconnect.hpp"

namespace arm_cpu {

// =====================================================================
// ChiInterconnect
// =====================================================================

ChiInterconnect::ChiInterconnect(ChiInterconnectConfig config)
    : config_(std::move(config))
{}

void ChiInterconnect::send_request(const ChiRequest& req, NodeId dest) {
    InFlightMessage msg;
    msg.dest = dest;
    msg.arrival_cycle = current_cycle_ + config_.req_latency;
    msg.msg = req;
    in_flight_.push_back(std::move(msg));
    stats_.requests++;
    stats_.total_messages++;
    update_peak();
}

void ChiInterconnect::send_response(const ChiResponse& resp, NodeId dest) {
    InFlightMessage msg;
    msg.dest = dest;
    msg.arrival_cycle = current_cycle_ + config_.rsp_latency;
    msg.msg = resp;
    in_flight_.push_back(std::move(msg));
    stats_.responses++;
    stats_.total_messages++;
    update_peak();
}

void ChiInterconnect::send_data(const ChiData& data, NodeId dest) {
    InFlightMessage msg;
    msg.dest = dest;
    msg.arrival_cycle = current_cycle_ + config_.dat_latency;
    msg.msg = data;
    in_flight_.push_back(std::move(msg));
    stats_.data_msgs++;
    stats_.total_messages++;
    update_peak();
}

void ChiInterconnect::send_snoop(const ChiSnoop& snoop, NodeId dest) {
    InFlightMessage msg;
    msg.dest = dest;
    msg.arrival_cycle = current_cycle_ + config_.snp_latency;
    msg.msg = snoop;
    in_flight_.push_back(std::move(msg));
    stats_.snoops++;
    stats_.total_messages++;
    update_peak();
}

void ChiInterconnect::send_snoop_resp(const ChiSnoopResp& resp, NodeId dest) {
    InFlightMessage msg;
    msg.dest = dest;
    msg.arrival_cycle = current_cycle_ + config_.snp_latency;
    msg.msg = resp;
    in_flight_.push_back(std::move(msg));
    stats_.snoop_resps++;
    stats_.total_messages++;
    update_peak();
}

void ChiInterconnect::update_peak() {
    if (in_flight_.size() > stats_.peak_in_flight) {
        stats_.peak_in_flight = in_flight_.size();
    }
}

std::vector<std::pair<NodeId, InFlightMessage>> ChiInterconnect::get_arriving_messages() {
    std::vector<std::pair<NodeId, InFlightMessage>> arriving;

    while (!in_flight_.empty()) {
        if (in_flight_.front().arrival_cycle <= current_cycle_) {
            auto msg = std::move(in_flight_.front());
            in_flight_.pop_front();
            arriving.push_back({msg.dest, std::move(msg)});
        } else {
            break;
        }
    }

    return arriving;
}

void ChiInterconnect::advance_cycle() { ++current_cycle_; }
uint64_t ChiInterconnect::current_cycle() const { return current_cycle_; }
const InterconnectStats& ChiInterconnect::stats() const { return stats_; }
std::size_t ChiInterconnect::in_flight_count() const { return in_flight_.size(); }
void ChiInterconnect::clear() { in_flight_.clear(); }

// =====================================================================
// ChiSystem
// =====================================================================

ChiSystem::ChiSystem(std::unique_ptr<RnFNode> rn_f, std::unique_ptr<HnFNode> hn_f,
                     std::unique_ptr<SnFNode> sn_f,
                     ChiInterconnectConfig interconnect_config)
    : interconnect(std::move(interconnect_config))
    , current_cycle_(0)
{
    rn_f_nodes.push_back(std::move(rn_f));
    this->hn_f = std::move(hn_f);
    this->sn_f = std::move(sn_f);
}

ChiSystem::ChiSystem(std::vector<std::unique_ptr<RnFNode>> rn_f_nodes,
                     std::unique_ptr<HnFNode> hn_f,
                     std::unique_ptr<SnFNode> sn_f,
                     ChiInterconnectConfig interconnect_config)
    : rn_f_nodes(std::move(rn_f_nodes))
    , hn_f(std::move(hn_f))
    , sn_f(std::move(sn_f))
    , interconnect(std::move(interconnect_config))
    , current_cycle_(0)
{}

void ChiSystem::process_interconnect() {
    // Collect outgoing messages from all nodes

    // From RN-F nodes to HN-F
    for (auto& rn_f : rn_f_nodes) {
        while (auto req = rn_f->req_channel_mut().recv()) {
            interconnect.send_request(*req, hn_f->node_id);
        }
        while (auto resp = rn_f->snp_resp_channel_mut().recv()) {
            interconnect.send_snoop_resp(*resp, hn_f->node_id);
        }
    }

    // From HN-F to RN-F and SN-F
    while (auto resp = hn_f->rsp_channel_mut().recv()) {
        interconnect.send_response(*resp, resp->dest_id);
    }
    while (auto data = hn_f->dat_channel_mut().recv()) {
        interconnect.send_data(*data, data->dest_id);
    }
    while (auto snoop = hn_f->snp_channel_mut().recv()) {
        interconnect.send_snoop(*snoop, snoop->dest_id);
    }
    while (auto req = hn_f->sn_req_channel_mut().recv()) {
        interconnect.send_request(*req, sn_f->node_id);
    }

    // From SN-F to HN-F
    while (auto resp = sn_f->rsp_channel_mut().recv()) {
        interconnect.send_response(*resp, resp->dest_id);
    }
    while (auto data = sn_f->dat_channel_mut().recv()) {
        interconnect.send_data(*data, data->dest_id);
    }

    // Deliver arriving messages
    auto arriving = interconnect.get_arriving_messages();
    for (auto& [dest, msg] : arriving) {
        deliver_message(dest, msg);
    }
}

void ChiSystem::deliver_message(NodeId dest, InFlightMessage& msg) {
    std::visit([&dest, this](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_same_v<T, ChiRequest>) {
            if (dest == hn_f->node_id) {
                hn_f->req_channel_mut().send(std::move(arg));
            } else if (dest == sn_f->node_id) {
                sn_f->req_channel_mut().send(std::move(arg));
            }
        } else if constexpr (std::is_same_v<T, ChiResponse>) {
            if (auto* rn_f = find_rn_f(dest)) {
                rn_f->rsp_channel_mut().send(std::move(arg));
            } else if (dest == hn_f->node_id) {
                hn_f->rsp_channel_mut().send(std::move(arg));
            }
        } else if constexpr (std::is_same_v<T, ChiData>) {
            if (auto* rn_f = find_rn_f(arg.dest_id)) {
                rn_f->dat_channel_mut().send(std::move(arg));
            } else if (arg.dest_id == hn_f->node_id) {
                hn_f->sn_dat_channel_mut().send(std::move(arg));
            }
        } else if constexpr (std::is_same_v<T, ChiSnoop>) {
            if (auto* rn_f = find_rn_f(arg.dest_id)) {
                rn_f->snp_channel_mut().send(std::move(arg));
            }
        } else if constexpr (std::is_same_v<T, ChiSnoopResp>) {
            if (dest == hn_f->node_id) {
                hn_f->snp_resp_channel_mut().send(std::move(arg));
            }
        }
    }, std::move(msg.msg));
}

RnFNode* ChiSystem::find_rn_f(NodeId node_id) {
    for (auto& rn_f : rn_f_nodes) {
        if (rn_f->node_id == node_id) return rn_f.get();
    }
    return nullptr;
}

void ChiSystem::process_nodes() {
    // Process RN-F nodes
    for (auto& rn_f : rn_f_nodes) {
        while (auto snoop = rn_f->snp_channel_mut().recv()) {
            rn_f->handle_snoop(*snoop);
        }
        while (auto resp = rn_f->rsp_channel_mut().recv()) {
            rn_f->handle_response(*resp);
        }
        while (auto data = rn_f->dat_channel_mut().recv()) {
            rn_f->handle_data(*data);
        }
        rn_f->process_retries();
    }

    // Process HN-F
    hn_f->process_requests();
    hn_f->process_snoop_responses();
    hn_f->process_memory_responses();

    // Process SN-F
    sn_f->process_requests();
    sn_f->process_completions();
}

void ChiSystem::step() {
    process_nodes();
    process_interconnect();
    ++current_cycle_;
    for (auto& rn_f : rn_f_nodes) rn_f->advance_cycle();
    hn_f->advance_cycle();
    sn_f->advance_cycle();
    interconnect.advance_cycle();
}

uint64_t ChiSystem::current_cycle() const { return current_cycle_; }

RnFNode* ChiSystem::primary_rn_f() {
    return rn_f_nodes.empty() ? nullptr : rn_f_nodes[0].get();
}

const RnFNode* ChiSystem::primary_rn_f() const {
    return rn_f_nodes.empty() ? nullptr : rn_f_nodes[0].get();
}

bool ChiSystem::has_pending_transactions() const {
    for (const auto& rn_f : rn_f_nodes) {
        if (rn_f->has_pending_transactions()) return true;
    }
    if (hn_f->has_pending_transactions()) return true;
    if (sn_f->has_pending_requests()) return true;
    if (interconnect.in_flight_count() > 0) return true;
    return false;
}

} // namespace arm_cpu
