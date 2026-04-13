#pragma once

/// @file chi_protocol.hpp
/// @brief CHI Issue B protocol definitions: request/response/snoop types, opcodes,
///        channels, transaction IDs, message headers, and related structures.

#include "arm_cpu/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace arm_cpu {

// =====================================================================
// ChiRequestType — CHI request opcodes (extended set, replaces types.hpp stub)
// =====================================================================
enum class ChiRequestTypeExt : uint8_t {
    // Read requests
    ReadNoSnoop,
    ReadNotSharedDirty,
    ReadShared,
    ReadMakeUnique,
    ReadOnce,
    ReadOnceCleanInvalid,
    ReadOnceMakeInvalid,

    // Write requests
    WriteNoSnoop,
    WriteUnique,
    WriteUniquePtl,
    WriteUniqueFull,
    WriteEvictFull,
    WriteEvictPtl,

    // Coherence requests
    CleanUnique,
    MakeUnique,
    Evict,
    CleanShared,
    CleanInvalid,
    MakeInvalid,

    // Dataless requests
    DVMOp,
    PCrdReturn,
};

/// Check if a request type is a read.
inline bool chi_is_read(ChiRequestTypeExt r) {
    switch (r) {
        case ChiRequestTypeExt::ReadNoSnoop:
        case ChiRequestTypeExt::ReadNotSharedDirty:
        case ChiRequestTypeExt::ReadShared:
        case ChiRequestTypeExt::ReadMakeUnique:
        case ChiRequestTypeExt::ReadOnce:
        case ChiRequestTypeExt::ReadOnceCleanInvalid:
        case ChiRequestTypeExt::ReadOnceMakeInvalid:
            return true;
        default: return false;
    }
}

/// Check if a request type is a write.
inline bool chi_is_write(ChiRequestTypeExt r) {
    switch (r) {
        case ChiRequestTypeExt::WriteNoSnoop:
        case ChiRequestTypeExt::WriteUnique:
        case ChiRequestTypeExt::WriteUniquePtl:
        case ChiRequestTypeExt::WriteUniqueFull:
        case ChiRequestTypeExt::WriteEvictFull:
        case ChiRequestTypeExt::WriteEvictPtl:
            return true;
        default: return false;
    }
}

/// Check if a request type requires data response.
inline bool chi_requires_data(ChiRequestTypeExt r) {
    return chi_is_read(r);
}

// =====================================================================
// ChiResponseType — CHI response types
// =====================================================================
enum class ChiResponseType : uint8_t {
    // Data responses
    CompData,
    DataSepResp,
    NonCopyBackWrData,
    CopyBackWrData,

    // Acknowledgments
    CompAck,
    DBIDResp,
    RespSepData,

    // Combined responses
    Comp,
    CompCMO,
};

/// Check if a response type carries data.
inline bool chi_has_data(ChiResponseType r) {
    switch (r) {
        case ChiResponseType::CompData:
        case ChiResponseType::DataSepResp:
        case ChiResponseType::NonCopyBackWrData:
        case ChiResponseType::CopyBackWrData:
            return true;
        default: return false;
    }
}

/// Check if a response type is an acknowledgment.
inline bool chi_is_ack(ChiResponseType r) {
    switch (r) {
        case ChiResponseType::CompAck:
        case ChiResponseType::DBIDResp:
        case ChiResponseType::RespSepData:
        case ChiResponseType::Comp:
            return true;
        default: return false;
    }
}

// =====================================================================
// ChiSnoopType — CHI snoop types
// =====================================================================
enum class ChiSnoopType : uint8_t {
    SnpOnce,
    SnpShared,
    SnpClean,
    SnpData,
    SnpCleanShared,
    SnpCleanInvalid,
    SnpMakeInvalid,
    SnpStashUnique,
    SnpStashShared,
};

/// Check if a snoop type requires data response.
inline bool chi_snoop_requires_data(ChiSnoopType s) {
    switch (s) {
        case ChiSnoopType::SnpData:
        case ChiSnoopType::SnpClean:
        case ChiSnoopType::SnpStashUnique:
        case ChiSnoopType::SnpStashShared:
            return true;
        default: return false;
    }
}

// =====================================================================
// ChiOpcode — tagged union of Request / Response / Snoop
// =====================================================================
struct ChiOpcode {
    std::variant<ChiRequestTypeExt, ChiResponseType, ChiSnoopType> value;

    static ChiOpcode request(ChiRequestTypeExt r) { return ChiOpcode{r}; }
    static ChiOpcode response(ChiResponseType r) { return ChiOpcode{r}; }
    static ChiOpcode snoop(ChiSnoopType s) { return ChiOpcode{s}; }

    bool is_request() const { return std::holds_alternative<ChiRequestTypeExt>(value); }
    bool is_response() const { return std::holds_alternative<ChiResponseType>(value); }
    bool is_snoop() const { return std::holds_alternative<ChiSnoopType>(value); }

    ChiRequestTypeExt  as_request()  const { return std::get<ChiRequestTypeExt>(value); }
    ChiResponseType    as_response() const { return std::get<ChiResponseType>(value); }
    ChiSnoopType       as_snoop()    const { return std::get<ChiSnoopType>(value); }
};

// =====================================================================
// ChiChannel — channel identifiers
// =====================================================================
enum class ChiChannel : uint8_t {
    Request,
    Response,
    Data,
    Snoop,
};

// =====================================================================
// ChiOrder — ordering requirements
// =====================================================================
enum class ChiOrder : uint8_t {
    None,
    Endpoint,
    Global,
};

// =====================================================================
// ChiTxnId — CHI transaction identifier
// =====================================================================
struct ChiTxnId {
    uint16_t value = 0;

    ChiTxnId() = default;
    explicit ChiTxnId(uint16_t v) : value(v) {}

    ChiTxnId next() {
        uint16_t current = value;
        value = static_cast<uint16_t>(value + 1);
        return ChiTxnId{current};
    }

    bool operator==(const ChiTxnId& o) const { return value == o.value; }
    bool operator!=(const ChiTxnId& o) const { return value != o.value; }

    struct Hash {
        std::size_t operator()(const ChiTxnId& id) const noexcept {
            return std::hash<uint16_t>{}(id.value);
        }
    };
};

// =====================================================================
// ChiMessageHeader — header for all CHI messages
// =====================================================================
struct ChiMessageHeader {
    ChiOpcode opcode;
    ChiTxnId  txn_id;
    uint8_t   src_id  = 0;
    uint8_t   dest_id = 0;
    uint64_t  addr    = 0;
    uint8_t   size    = 0;
    bool      allow_retry = true;
    ChiOrder  order   = ChiOrder::None;
};

// =====================================================================
// ChiDataResponse — data response payload
// =====================================================================
struct ChiDataResponse {
    ChiTxnId      txn_id;
    ChiResponseType resp_type;
    std::vector<uint8_t> data;
    bool           error  = false;
    bool           poison = false;
};

// =====================================================================
// ChiSnoopRequest — snoop request payload
// =====================================================================
struct ChiSnoopRequest {
    ChiSnoopType snoop_type;
    ChiTxnId     txn_id;
    uint64_t     addr = 0;
};

// =====================================================================
// ChiSnoopResponse — snoop response payload
// =====================================================================
struct ChiSnoopResponse {
    ChiTxnId          txn_id;
    std::vector<uint8_t> data;
    bool              data_present = false;
    CacheLineState    state = CacheLineState::Invalid;
};

// =====================================================================
// ChiNodeType — node type in CHI topology
// =====================================================================
enum class ChiNodeType : uint8_t {
    RnF,  // Request Node - Fully coherent
    HnF,  // Home Node - Protocol controller
    SnF,  // Subordinate Node - Memory controller
};

// =====================================================================
// NodeId — CHI node identifier
// =====================================================================
struct NodeId {
    uint8_t value = 0;

    NodeId() = default;
    explicit NodeId(uint8_t v) : value(v) {}

    bool operator==(const NodeId& o) const { return value == o.value; }
    bool operator!=(const NodeId& o) const { return value != o.value; }
    bool operator<(const NodeId& o)  const { return value < o.value; }

    struct Hash {
        std::size_t operator()(const NodeId& n) const noexcept {
            return std::hash<uint8_t>{}(n.value);
        }
    };
};

} // namespace arm_cpu
