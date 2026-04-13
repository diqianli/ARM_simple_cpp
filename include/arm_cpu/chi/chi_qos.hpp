#pragma once

/// @file chi_qos.hpp
/// @brief QoS and retry mechanism for CHI protocol.
///        Includes PCrd (Protocol Credit) management, DBID allocation,
///        retry queues, and channel credit tracking.

#include "arm_cpu/chi/chi_protocol.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace arm_cpu {

// =====================================================================
// PcrdType — Protocol Credit type
// =====================================================================
enum class PcrdType : uint8_t {
    Type0,
    Type1,
    Type2,
};

inline std::size_t pcrd_index(PcrdType t) { return static_cast<std::size_t>(t); }

// =====================================================================
// PcrdResponse
// =====================================================================
struct PcrdResponse {
    bool     granted = false;
    PcrdType type    = PcrdType::Type0;

    static PcrdResponse grant(PcrdType t) { return {true, t}; }
    static PcrdResponse deny()             { return {false, PcrdType::Type0}; }
};

// =====================================================================
// PendingRequest — request waiting for retry
// =====================================================================
struct PendingRequest {
    InstructionId      instruction_id;
    std::optional<ChiTxnId> txn_id;
    ChiRequestTypeExt  request_type;
    uint64_t           addr = 0;
    uint8_t            size = 0;
    uint64_t           first_attempt_cycle = 0;
    uint32_t           retry_count = 0;
    PcrdType           required_pcrd = PcrdType::Type0;
};

// =====================================================================
// QosStats
// =====================================================================
struct QosStats {
    uint64_t    total_requests         = 0;
    uint64_t    immediate_grants       = 0;
    uint64_t    denials                = 0;
    uint64_t    successful_retries     = 0;
    uint64_t    dbids_allocated        = 0;
    uint64_t    dbids_freed            = 0;
    std::size_t peak_retry_queue_size  = 0;
};

// =====================================================================
// DbidAllocator — Data Buffer ID allocator
// =====================================================================
class DbidAllocator {
public:
    explicit DbidAllocator(uint16_t max_dbid);

    std::optional<uint16_t> allocate();
    void                    free(uint16_t dbid);
    uint16_t                available() const;
    uint16_t                in_use() const;
    void                    reset();

private:
    uint16_t              next_dbid_ = 0;
    uint16_t              max_dbid_;
    std::deque<uint16_t>  free_list_;
    uint16_t              in_use_ = 0;
};

// =====================================================================
// QosCreditManager
// =====================================================================
class QosCreditManager {
public:
    QosCreditManager(uint16_t max_pcrd_credits, uint16_t max_dbid, std::size_t max_retry_queue_size);

    static PcrdType get_pcrd_type(ChiRequestTypeExt request_type);

    bool          has_credit(PcrdType pcrd_type) const;
    PcrdResponse  request_credit(PcrdType pcrd_type);
    void          return_credit(PcrdType pcrd_type);

    std::optional<uint16_t> allocate_dbid();
    void                    free_dbid(uint16_t dbid);

    /// Process a request; returns ChiTxnId on success, or PendingRequest if denied.
    std::variant<ChiTxnId, PendingRequest> process_request(
        InstructionId instruction_id,
        ChiRequestTypeExt request_type,
        uint64_t addr,
        uint8_t size,
        uint64_t current_cycle);

    std::vector<PendingRequest> process_retries(uint64_t current_cycle);

    std::size_t retry_queue_size() const;
    uint16_t    available_credits(PcrdType pcrd_type) const;
    uint16_t    credits_in_use() const;
    uint16_t    available_dbids() const;

    const QosStats& stats() const;
    void            reset_stats();
    void            reset();

private:
    uint16_t                    pcrd_available_[3];
    uint16_t                    pcrd_max_[3];
    uint16_t                    pcrd_in_use_ = 0;
    DbidAllocator               dbid_allocator_;
    std::deque<PendingRequest>  retry_queue_;
    std::size_t                 max_retry_queue_size_;
    QosStats                    stats_;
};

// =====================================================================
// ChannelCredits
// =====================================================================
class ChannelCredits {
public:
    ChannelCredits(uint16_t max_credits, const char* name);

    bool  has_credit() const;
    bool  use_credit();
    void  return_credit();
    uint16_t available() const;
    uint16_t in_use() const;
    void     reset();

private:
    uint16_t  available_;
    uint16_t  max_credits_;
    const char* name_;
};

// =====================================================================
// NodeChannelCredits
// =====================================================================
struct NodeChannelCredits {
    ChannelCredits req;
    ChannelCredits rsp;
    ChannelCredits dat;
    ChannelCredits snp;

    NodeChannelCredits(uint16_t req_c, uint16_t rsp_c, uint16_t dat_c, uint16_t snp_c);
    static NodeChannelCredits typical();
    void reset();
};

} // namespace arm_cpu
