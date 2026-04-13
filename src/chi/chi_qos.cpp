/// @file chi_qos.cpp
/// @brief DbidAllocator, QosCreditManager, ChannelCredits, NodeChannelCredits implementation.

#include "arm_cpu/chi/chi_qos.hpp"

namespace arm_cpu {

// =====================================================================
// DbidAllocator
// =====================================================================

DbidAllocator::DbidAllocator(uint16_t max_dbid)
    : next_dbid_(0), max_dbid_(max_dbid)
    , free_list_(max_dbid)
{}

std::optional<uint16_t> DbidAllocator::allocate() {
    if (!free_list_.empty()) {
        uint16_t dbid = free_list_.front();
        free_list_.pop_front();
        in_use_++;
        return dbid;
    }
    if (next_dbid_ < max_dbid_) {
        uint16_t dbid = next_dbid_++;
        in_use_++;
        return dbid;
    }
    return std::nullopt;
}

void DbidAllocator::free(uint16_t dbid) {
    if (dbid < max_dbid_) {
        free_list_.push_back(dbid);
        if (in_use_ > 0) in_use_--;
    }
}

uint16_t DbidAllocator::available() const { return max_dbid_ - in_use_; }
uint16_t DbidAllocator::in_use() const { return in_use_; }

void DbidAllocator::reset() {
    next_dbid_ = 0;
    free_list_.clear();
    in_use_ = 0;
}

// =====================================================================
// QosCreditManager
// =====================================================================

QosCreditManager::QosCreditManager(
    uint16_t max_pcrd_credits, uint16_t max_dbid, std::size_t max_retry_queue_size)
    : dbid_allocator_(max_dbid)
    , max_retry_queue_size_(max_retry_queue_size)
{
    for (int i = 0; i < 3; ++i) {
        pcrd_available_[i] = max_pcrd_credits;
        pcrd_max_[i] = max_pcrd_credits;
    }
}

PcrdType QosCreditManager::get_pcrd_type(ChiRequestTypeExt request_type) {
    switch (request_type) {
        case ChiRequestTypeExt::ReadNoSnoop:
        case ChiRequestTypeExt::ReadNotSharedDirty:
        case ChiRequestTypeExt::ReadShared:
        case ChiRequestTypeExt::ReadMakeUnique:
        case ChiRequestTypeExt::ReadOnce:
        case ChiRequestTypeExt::ReadOnceCleanInvalid:
        case ChiRequestTypeExt::ReadOnceMakeInvalid:
            return PcrdType::Type0;

        case ChiRequestTypeExt::WriteNoSnoop:
        case ChiRequestTypeExt::WriteUnique:
        case ChiRequestTypeExt::WriteUniquePtl:
        case ChiRequestTypeExt::WriteUniqueFull:
        case ChiRequestTypeExt::WriteEvictFull:
        case ChiRequestTypeExt::WriteEvictPtl:
            return PcrdType::Type1;

        case ChiRequestTypeExt::CleanUnique:
        case ChiRequestTypeExt::MakeUnique:
        case ChiRequestTypeExt::Evict:
        case ChiRequestTypeExt::CleanShared:
        case ChiRequestTypeExt::CleanInvalid:
        case ChiRequestTypeExt::MakeInvalid:
        case ChiRequestTypeExt::DVMOp:
        case ChiRequestTypeExt::PCrdReturn:
            return PcrdType::Type2;
    }
    return PcrdType::Type2;
}

bool QosCreditManager::has_credit(PcrdType pcrd_type) const {
    return pcrd_available_[pcrd_index(pcrd_type)] > 0;
}

PcrdResponse QosCreditManager::request_credit(PcrdType pcrd_type) {
    std::size_t idx = pcrd_index(pcrd_type);
    if (pcrd_available_[idx] > 0) {
        pcrd_available_[idx]--;
        pcrd_in_use_++;
        return PcrdResponse::grant(pcrd_type);
    }
    return PcrdResponse::deny();
}

void QosCreditManager::return_credit(PcrdType pcrd_type) {
    std::size_t idx = pcrd_index(pcrd_type);
    if (pcrd_available_[idx] < pcrd_max_[idx]) {
        pcrd_available_[idx]++;
        if (pcrd_in_use_ > 0) pcrd_in_use_--;
    }
}

std::optional<uint16_t> QosCreditManager::allocate_dbid() {
    auto dbid = dbid_allocator_.allocate();
    if (dbid.has_value()) stats_.dbids_allocated++;
    return dbid;
}

void QosCreditManager::free_dbid(uint16_t dbid) {
    dbid_allocator_.free(dbid);
    stats_.dbids_freed++;
}

std::variant<ChiTxnId, PendingRequest> QosCreditManager::process_request(
    InstructionId instruction_id,
    ChiRequestTypeExt request_type,
    uint64_t addr,
    uint8_t size,
    uint64_t current_cycle)
{
    stats_.total_requests++;
    PcrdType pcrd_type = get_pcrd_type(request_type);

    auto resp = request_credit(pcrd_type);
    if (resp.granted) {
        stats_.immediate_grants++;
        return ChiTxnId{0};
    }

    stats_.denials++;

    PendingRequest pending;
    pending.instruction_id = instruction_id;
    pending.txn_id = std::nullopt;
    pending.request_type = request_type;
    pending.addr = addr;
    pending.size = size;
    pending.first_attempt_cycle = current_cycle;
    pending.retry_count = 0;
    pending.required_pcrd = pcrd_type;

    if (retry_queue_.size() < max_retry_queue_size_) {
        retry_queue_.push_back(pending);
        if (retry_queue_.size() > stats_.peak_retry_queue_size) {
            stats_.peak_retry_queue_size = retry_queue_.size();
        }
    }

    return pending;
}

std::vector<PendingRequest> QosCreditManager::process_retries(uint64_t /*current_cycle*/) {
    std::vector<PendingRequest> granted;
    std::deque<PendingRequest> remaining;

    while (!retry_queue_.empty()) {
        auto pending = std::move(retry_queue_.front());
        retry_queue_.pop_front();

        auto resp = request_credit(pending.required_pcrd);
        if (resp.granted) {
            pending.retry_count++;
            stats_.successful_retries++;
            granted.push_back(std::move(pending));
        } else {
            remaining.push_back(std::move(pending));
        }
    }

    retry_queue_ = std::move(remaining);
    return granted;
}

std::size_t QosCreditManager::retry_queue_size() const { return retry_queue_.size(); }
uint16_t QosCreditManager::available_credits(PcrdType pcrd_type) const {
    return pcrd_available_[pcrd_index(pcrd_type)];
}
uint16_t QosCreditManager::credits_in_use() const { return pcrd_in_use_; }
uint16_t QosCreditManager::available_dbids() const { return dbid_allocator_.available(); }
const QosStats& QosCreditManager::stats() const { return stats_; }

void QosCreditManager::reset_stats() { stats_ = QosStats{}; }

void QosCreditManager::reset() {
    for (int i = 0; i < 3; ++i) {
        pcrd_available_[i] = pcrd_max_[i];
    }
    pcrd_in_use_ = 0;
    dbid_allocator_.reset();
    retry_queue_.clear();
    stats_ = QosStats{};
}

// =====================================================================
// ChannelCredits
// =====================================================================

ChannelCredits::ChannelCredits(uint16_t max_credits, const char* name)
    : available_(max_credits), max_credits_(max_credits), name_(name)
{}

bool ChannelCredits::has_credit() const { return available_ > 0; }

bool ChannelCredits::use_credit() {
    if (available_ > 0) { available_--; return true; }
    return false;
}

void ChannelCredits::return_credit() {
    if (available_ < max_credits_) available_++;
}

uint16_t ChannelCredits::available() const { return available_; }
uint16_t ChannelCredits::in_use() const { return max_credits_ - available_; }

void ChannelCredits::reset() { available_ = max_credits_; }

// =====================================================================
// NodeChannelCredits
// =====================================================================

NodeChannelCredits::NodeChannelCredits(uint16_t req_c, uint16_t rsp_c, uint16_t dat_c, uint16_t snp_c)
    : req(req_c, "REQ")
    , rsp(rsp_c, "RSP")
    , dat(dat_c, "DAT")
    , snp(snp_c, "SNP")
{}

NodeChannelCredits NodeChannelCredits::typical() {
    return NodeChannelCredits(16, 16, 8, 8);
}

void NodeChannelCredits::reset() {
    req.reset(); rsp.reset(); dat.reset(); snp.reset();
}

} // namespace arm_cpu
