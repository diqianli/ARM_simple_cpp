#pragma once

/// @file chi_node.hpp
/// @brief CHI node base structures: message types (request, response, data, snoop),
///        data descriptors, generic channels, and node configuration.

#include "arm_cpu/chi/chi_protocol.hpp"
#include "arm_cpu/chi/chi_coherence.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <deque>
#include <string>

namespace arm_cpu {

// =====================================================================
// ChiRequest — CHI request message
// =====================================================================
struct ChiRequest {
    ChiMessageHeader header;
    ChiRequestTypeExt req_type;
    InstructionId    instr_id;
    bool             expect_data = false;
    std::optional<uint16_t> dbid;

    static ChiRequest new_request(
        ChiTxnId txn_id, NodeId src_id, NodeId dest_id,
        ChiRequestTypeExt req_type, uint64_t addr,
        uint8_t size, InstructionId instr_id);
};

// =====================================================================
// ChiResponse — CHI response message
// =====================================================================
struct ChiResponse {
    ChiTxnId      txn_id;
    ChiResponseType resp_type;
    NodeId        src_id;
    NodeId        dest_id;
    uint64_t      addr = 0;
    std::optional<uint16_t> dbid;
    bool          is_ack = false;

    static ChiResponse new_response(
        ChiTxnId txn_id, ChiResponseType resp_type,
        NodeId src_id, NodeId dest_id, uint64_t addr);

    static ChiResponse dbid_response(
        ChiTxnId txn_id, NodeId src_id, NodeId dest_id, uint16_t dbid);

    static ChiResponse comp_ack(
        ChiTxnId txn_id, NodeId src_id, NodeId dest_id, uint64_t addr);
};

// =====================================================================
// DataDescriptor — abstract data representation
// =====================================================================
struct DataDescriptor {
    uint8_t        size  = 0;
    bool           valid = false;
    bool           dirty = false;
    ChiCacheState  cache_state = ChiCacheState::Invalid;
    bool           error = false;

    static DataDescriptor new_desc(uint8_t size);
    static DataDescriptor empty();
    static DataDescriptor error_desc();

    DataDescriptor with_dirty(bool d);
    DataDescriptor with_state(ChiCacheState s);
};

// =====================================================================
// ChiData — CHI data message
// =====================================================================
struct ChiData {
    ChiTxnId        txn_id;
    ChiResponseType resp_type;
    NodeId          src_id;
    NodeId          dest_id;
    uint64_t        addr = 0;
    DataDescriptor  data;

    static ChiData new_data(
        ChiTxnId txn_id, ChiResponseType resp_type,
        NodeId src_id, NodeId dest_id, uint64_t addr, DataDescriptor data);

    static ChiData comp_data(
        ChiTxnId txn_id, NodeId src_id, NodeId dest_id,
        uint64_t addr, DataDescriptor data);
};

// =====================================================================
// ChiSnoop — CHI snoop message
// =====================================================================
struct ChiSnoop {
    ChiSnoopType snoop_type;
    ChiTxnId     txn_id;
    NodeId       src_id;
    NodeId       dest_id;
    uint64_t     addr = 0;
    bool         data_requested = false;

    static ChiSnoop new_snoop(
        ChiSnoopType snoop_type, ChiTxnId txn_id,
        NodeId src_id, NodeId dest_id, uint64_t addr);
};

// =====================================================================
// ChiSnoopResp — CHI snoop response
// =====================================================================
struct ChiSnoopResp {
    ChiTxnId                txn_id;
    NodeId                  src_id;
    NodeId                  dest_id;
    uint64_t                addr = 0;
    bool                    data_valid = false;
    std::optional<DataDescriptor> data;
    ChiCacheState           state = ChiCacheState::Invalid;

    static ChiSnoopResp ack(ChiTxnId txn_id, NodeId src_id, NodeId dest_id,
                            uint64_t addr, ChiCacheState state);

    static ChiSnoopResp with_data(ChiTxnId txn_id, NodeId src_id, NodeId dest_id,
                                  uint64_t addr, DataDescriptor data, ChiCacheState state);
};

// =====================================================================
// ChannelStats
// =====================================================================
struct ChannelStats {
    uint64_t    sent = 0;
    uint64_t    received = 0;
    uint64_t    dropped = 0;
    std::size_t peak_occupancy = 0;
};

// =====================================================================
// Channel<T> — generic buffered channel for CHI messages
// =====================================================================
template<typename T>
class Channel {
public:
    Channel(std::size_t capacity, const char* name)
        : capacity_(capacity), name_(name) {}

    bool send(T msg) {
        if (buffer_.size() >= capacity_) {
            stats_.dropped++;
            return false;
        }
        buffer_.push_back(std::move(msg));
        stats_.sent++;
        if (buffer_.size() > stats_.peak_occupancy) {
            stats_.peak_occupancy = buffer_.size();
        }
        return true;
    }

    std::optional<T> recv() {
        if (buffer_.empty()) return std::nullopt;
        T msg = std::move(buffer_.front());
        buffer_.pop_front();
        stats_.received++;
        return msg;
    }

    const T* peek() const {
        if (buffer_.empty()) return nullptr;
        return &buffer_.front();
    }

    bool        is_empty() const { return buffer_.empty(); }
    bool        is_full()  const { return buffer_.size() >= capacity_; }
    std::size_t size()     const { return buffer_.size(); }
    std::size_t capacity() const { return capacity_; }

    const ChannelStats& stats() const { return stats_; }
    void clear() { buffer_.clear(); }

private:
    std::deque<T> buffer_;
    std::size_t   capacity_;
    const char*   name_;
    ChannelStats  stats_;
};

// =====================================================================
// ChiNodeConfig
// =====================================================================
struct ChiNodeConfig {
    uint8_t     node_id = 0;
    ChiNodeType node_type = ChiNodeType::RnF;
    std::size_t req_channel_capacity = 16;
    std::size_t rsp_channel_capacity = 16;
    std::size_t dat_channel_capacity = 8;
    std::size_t snp_channel_capacity = 8;
};

} // namespace arm_cpu
