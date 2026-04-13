/// @file chi_node.cpp
/// @brief ChiRequest, ChiResponse, DataDescriptor, ChiData, ChiSnoop, ChiSnoopResp,
///        and ChiNodeConfig implementation.

#include "arm_cpu/chi/chi_node.hpp"

namespace arm_cpu {

// =====================================================================
// ChiRequest
// =====================================================================

ChiRequest ChiRequest::new_request(
    ChiTxnId txn_id, NodeId src_id, NodeId dest_id,
    ChiRequestTypeExt req_type, uint64_t addr,
    uint8_t size, InstructionId instr_id)
{
    ChiRequest req;
    req.header.opcode = ChiOpcode::request(req_type);
    req.header.txn_id = txn_id;
    req.header.src_id = src_id.value;
    req.header.dest_id = dest_id.value;
    req.header.addr = addr;
    req.header.size = size;
    req.header.allow_retry = true;
    req.header.order = ChiOrder::None;
    req.req_type = req_type;
    req.instr_id = instr_id;
    req.expect_data = chi_requires_data(req_type);
    req.dbid = std::nullopt;
    return req;
}

// =====================================================================
// ChiResponse
// =====================================================================

ChiResponse ChiResponse::new_response(
    ChiTxnId txn_id, ChiResponseType resp_type,
    NodeId src_id, NodeId dest_id, uint64_t addr)
{
    ChiResponse resp;
    resp.txn_id = txn_id;
    resp.resp_type = resp_type;
    resp.src_id = src_id;
    resp.dest_id = dest_id;
    resp.addr = addr;
    resp.dbid = std::nullopt;
    resp.is_ack = false;
    return resp;
}

ChiResponse ChiResponse::dbid_response(
    ChiTxnId txn_id, NodeId src_id, NodeId dest_id, uint16_t dbid)
{
    ChiResponse resp;
    resp.txn_id = txn_id;
    resp.resp_type = ChiResponseType::DBIDResp;
    resp.src_id = src_id;
    resp.dest_id = dest_id;
    resp.addr = 0;
    resp.dbid = dbid;
    resp.is_ack = false;
    return resp;
}

ChiResponse ChiResponse::comp_ack(
    ChiTxnId txn_id, NodeId src_id, NodeId dest_id, uint64_t addr)
{
    ChiResponse resp;
    resp.txn_id = txn_id;
    resp.resp_type = ChiResponseType::CompAck;
    resp.src_id = src_id;
    resp.dest_id = dest_id;
    resp.addr = addr;
    resp.dbid = std::nullopt;
    resp.is_ack = true;
    return resp;
}

// =====================================================================
// DataDescriptor
// =====================================================================

DataDescriptor DataDescriptor::new_desc(uint8_t size) {
    return {size, true, false, ChiCacheState::SharedClean, false};
}

DataDescriptor DataDescriptor::empty() {
    return {0, false, false, ChiCacheState::Invalid, false};
}

DataDescriptor DataDescriptor::error_desc() {
    return {0, false, false, ChiCacheState::Invalid, true};
}

DataDescriptor DataDescriptor::with_dirty(bool d) {
    DataDescriptor copy = *this;
    copy.dirty = d;
    return copy;
}

DataDescriptor DataDescriptor::with_state(ChiCacheState s) {
    DataDescriptor copy = *this;
    copy.cache_state = s;
    return copy;
}

// =====================================================================
// ChiData
// =====================================================================

ChiData ChiData::new_data(
    ChiTxnId txn_id, ChiResponseType resp_type,
    NodeId src_id, NodeId dest_id, uint64_t addr, DataDescriptor data)
{
    ChiData d;
    d.txn_id = txn_id;
    d.resp_type = resp_type;
    d.src_id = src_id;
    d.dest_id = dest_id;
    d.addr = addr;
    d.data = std::move(data);
    return d;
}

ChiData ChiData::comp_data(
    ChiTxnId txn_id, NodeId src_id, NodeId dest_id,
    uint64_t addr, DataDescriptor data)
{
    return new_data(txn_id, ChiResponseType::CompData, src_id, dest_id, addr, std::move(data));
}

// =====================================================================
// ChiSnoop
// =====================================================================

ChiSnoop ChiSnoop::new_snoop(
    ChiSnoopType snoop_type, ChiTxnId txn_id,
    NodeId src_id, NodeId dest_id, uint64_t addr)
{
    ChiSnoop s;
    s.snoop_type = snoop_type;
    s.txn_id = txn_id;
    s.src_id = src_id;
    s.dest_id = dest_id;
    s.addr = addr;
    s.data_requested = chi_snoop_requires_data(snoop_type);
    return s;
}

// =====================================================================
// ChiSnoopResp
// =====================================================================

ChiSnoopResp ChiSnoopResp::ack(
    ChiTxnId txn_id, NodeId src_id, NodeId dest_id,
    uint64_t addr, ChiCacheState state)
{
    ChiSnoopResp resp;
    resp.txn_id = txn_id;
    resp.src_id = src_id;
    resp.dest_id = dest_id;
    resp.addr = addr;
    resp.data_valid = false;
    resp.data = std::nullopt;
    resp.state = state;
    return resp;
}

ChiSnoopResp ChiSnoopResp::with_data(
    ChiTxnId txn_id, NodeId src_id, NodeId dest_id,
    uint64_t addr, DataDescriptor data, ChiCacheState state)
{
    ChiSnoopResp resp;
    resp.txn_id = txn_id;
    resp.src_id = src_id;
    resp.dest_id = dest_id;
    resp.addr = addr;
    resp.data_valid = true;
    resp.data = std::move(data);
    resp.state = state;
    return resp;
}

} // namespace arm_cpu
