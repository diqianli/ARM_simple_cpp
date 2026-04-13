#pragma once

/// @file chi_interface.hpp
/// @brief CHI transaction management: transaction lifecycle, state tracking,
///        and the ChiInterface that orchestrates pending/in-flight/completed transactions.

#include "arm_cpu/chi/chi_protocol.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace arm_cpu {

// =====================================================================
// ChiTransactionState
// =====================================================================
enum class ChiTransactionState : uint8_t {
    Pending,
    InFlight,
    WaitingData,
    Complete,
    Failed,
};

// =====================================================================
// ChiTransaction
// =====================================================================
struct ChiTransaction {
    ChiTxnId           txn_id;
    InstructionId      instruction_id;
    ChiRequestTypeExt  request_type;
    uint64_t           addr = 0;
    uint8_t            size = 0;
    ChiTransactionState state = ChiTransactionState::Pending;
    uint64_t           issue_cycle = 0;
    std::optional<uint64_t> response_cycle;
    std::optional<uint64_t> complete_cycle;
    std::optional<std::vector<uint8_t>> data;
};

ChiTransaction make_chi_transaction(
    ChiTxnId txn_id,
    InstructionId instruction_id,
    ChiRequestTypeExt request_type,
    uint64_t addr,
    uint8_t size);

ChiResponseType chi_expected_response(const ChiTransaction& txn);
bool chi_is_complete(const ChiTransaction& txn);

// =====================================================================
// ChiInterfaceStats
// =====================================================================
struct ChiInterfaceStats {
    std::size_t pending_count   = 0;
    std::size_t in_flight_count = 0;
    std::size_t completed_count = 0;
    uint64_t    read_count      = 0;
    uint64_t    write_count     = 0;
    uint64_t    snoop_count     = 0;
};

// =====================================================================
// ChiInterface — transaction lifecycle manager
// =====================================================================
class ChiInterface {
public:
    explicit ChiInterface(std::size_t max_outstanding);

    bool can_accept() const;

    std::optional<ChiTransaction> create_read_transaction(
        InstructionId id, uint64_t addr, uint8_t size);

    std::optional<ChiTransaction> create_write_transaction(
        InstructionId id, uint64_t addr, uint8_t size);

    void issue(ChiTransaction txn);

    void receive_response(ChiTxnId txn_id, ChiResponseType response_type);
    void receive_data(ChiTxnId txn_id, std::vector<uint8_t> data);

    std::vector<ChiTransaction> get_completed();

    const ChiTransaction* get_transaction(ChiTxnId txn_id) const;

    void advance_cycle();
    uint64_t current_cycle() const;

    std::size_t outstanding_count() const;
    uint64_t    read_count() const;
    uint64_t    write_count() const;
    uint64_t    snoop_count() const;

    ChiInterfaceStats get_stats() const;
    void clear();

private:
    std::size_t                     max_outstanding_;
    ChiTxnId                        next_txn_id_;
    std::deque<ChiTransaction>      pending_;
    std::deque<ChiTransaction>      in_flight_;
    std::deque<ChiTransaction>      completed_;
    uint64_t                        current_cycle_ = 0;
    uint64_t                        read_count_  = 0;
    uint64_t                        write_count_ = 0;
    uint64_t                        snoop_count_ = 0;
};

} // namespace arm_cpu
