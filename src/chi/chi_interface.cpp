/// @file chi_interface.cpp
/// @brief ChiInterface and ChiTransaction implementation.

#include "arm_cpu/chi/chi_interface.hpp"

namespace arm_cpu {

// =====================================================================
// ChiTransaction
// =====================================================================

ChiTransaction make_chi_transaction(
    ChiTxnId txn_id,
    InstructionId instruction_id,
    ChiRequestTypeExt request_type,
    uint64_t addr,
    uint8_t size)
{
    ChiTransaction txn;
    txn.txn_id = txn_id;
    txn.instruction_id = instruction_id;
    txn.request_type = request_type;
    txn.addr = addr;
    txn.size = size;
    txn.state = ChiTransactionState::Pending;
    txn.issue_cycle = 0;
    txn.response_cycle = std::nullopt;
    txn.complete_cycle = std::nullopt;
    txn.data = std::nullopt;
    return txn;
}

ChiResponseType chi_expected_response(const ChiTransaction& txn) {
    if (chi_requires_data(txn.request_type)) {
        return ChiResponseType::CompData;
    }
    return ChiResponseType::DBIDResp;
}

bool chi_is_complete(const ChiTransaction& txn) {
    return txn.state == ChiTransactionState::Complete ||
           txn.state == ChiTransactionState::Failed;
}

// =====================================================================
// ChiInterface
// =====================================================================

ChiInterface::ChiInterface(std::size_t max_outstanding)
    : max_outstanding_(max_outstanding)
{}

bool ChiInterface::can_accept() const {
    return pending_.size() + in_flight_.size() < max_outstanding_;
}

std::optional<ChiTransaction> ChiInterface::create_read_transaction(
    InstructionId id, uint64_t addr, uint8_t size)
{
    if (!can_accept()) return std::nullopt;
    auto txn = make_chi_transaction(next_txn_id_.next(), id,
                                    ChiRequestTypeExt::ReadNoSnoop, addr, size);
    read_count_++;
    return txn;
}

std::optional<ChiTransaction> ChiInterface::create_write_transaction(
    InstructionId id, uint64_t addr, uint8_t size)
{
    if (!can_accept()) return std::nullopt;
    auto txn = make_chi_transaction(next_txn_id_.next(), id,
                                    ChiRequestTypeExt::WriteNoSnoop, addr, size);
    write_count_++;
    return txn;
}

void ChiInterface::issue(ChiTransaction txn) {
    txn.state = ChiTransactionState::InFlight;
    txn.issue_cycle = current_cycle_;
    in_flight_.push_back(std::move(txn));
}

void ChiInterface::receive_response(ChiTxnId txn_id, ChiResponseType response_type) {
    for (auto it = in_flight_.begin(); it != in_flight_.end(); ++it) {
        if (it->txn_id == txn_id) {
            auto txn = std::move(*it);
            in_flight_.erase(it);

            if (response_type == chi_expected_response(txn)) {
                if (chi_has_data(response_type)) {
                    txn.state = ChiTransactionState::WaitingData;
                    txn.response_cycle = current_cycle_;
                    in_flight_.push_back(std::move(txn));
                } else {
                    txn.state = ChiTransactionState::Complete;
                    txn.response_cycle = current_cycle_;
                    txn.complete_cycle = current_cycle_;
                    completed_.push_back(std::move(txn));
                }
            } else {
                txn.state = ChiTransactionState::Failed;
                completed_.push_back(std::move(txn));
            }
            return;
        }
    }
}

void ChiInterface::receive_data(ChiTxnId txn_id, std::vector<uint8_t> data) {
    for (auto it = in_flight_.begin(); it != in_flight_.end(); ++it) {
        if (it->txn_id == txn_id) {
            auto txn = std::move(*it);
            in_flight_.erase(it);
            txn.data = std::move(data);
            txn.state = ChiTransactionState::Complete;
            txn.response_cycle = current_cycle_;
            txn.complete_cycle = current_cycle_;
            completed_.push_back(std::move(txn));
            return;
        }
    }
}

std::vector<ChiTransaction> ChiInterface::get_completed() {
    std::vector<ChiTransaction> result;
    result.reserve(completed_.size());
    while (!completed_.empty()) {
        result.push_back(std::move(completed_.front()));
        completed_.pop_front();
    }
    return result;
}

const ChiTransaction* ChiInterface::get_transaction(ChiTxnId txn_id) const {
    for (const auto& t : pending_) {
        if (t.txn_id == txn_id) return &t;
    }
    for (const auto& t : in_flight_) {
        if (t.txn_id == txn_id) return &t;
    }
    for (const auto& t : completed_) {
        if (t.txn_id == txn_id) return &t;
    }
    return nullptr;
}

void ChiInterface::advance_cycle() { ++current_cycle_; }
uint64_t ChiInterface::current_cycle() const { return current_cycle_; }
std::size_t ChiInterface::outstanding_count() const { return pending_.size() + in_flight_.size(); }
uint64_t ChiInterface::read_count() const { return read_count_; }
uint64_t ChiInterface::write_count() const { return write_count_; }
uint64_t ChiInterface::snoop_count() const { return snoop_count_; }

ChiInterfaceStats ChiInterface::get_stats() const {
    return {
        pending_.size(),
        in_flight_.size(),
        completed_.size(),
        read_count_,
        write_count_,
        snoop_count_,
    };
}

void ChiInterface::clear() {
    pending_.clear();
    in_flight_.clear();
    completed_.clear();
}

} // namespace arm_cpu
