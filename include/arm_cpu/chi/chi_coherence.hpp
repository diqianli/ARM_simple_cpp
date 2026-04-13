#pragma once

/// @file chi_coherence.hpp
/// @brief CHI coherence state machine with extended MOESI states.
///        Includes stable states (I, UC, UD, SC, SD), intermediate transition
///        states, and the CoherenceStateMachine that governs transitions.

#include "arm_cpu/chi/chi_protocol.hpp"
#include "arm_cpu/types.hpp"

#include <cstdint>
#include <optional>

namespace arm_cpu {

// =====================================================================
// ChiCacheState — extended MOESI cache line states for CHI
// =====================================================================
enum class ChiCacheState : uint8_t {
    // Stable states
    Invalid,        // I  - not present in cache
    UniqueClean,    // UC - exclusive, clean
    UniqueDirty,    // UD - exclusive, dirty/modified
    SharedClean,    // SC - shared, may be in multiple caches
    SharedDirty,    // SD - shared, this copy may have dirty data

    // Intermediate states (transaction in progress)
    UcToI,  // UC -> I transition
    UdToI,  // UD -> I transition
    ScToI,  // SC -> I transition
    SdToI,  // SD -> I transition
    IToUc,  // I -> UC transition
    IToUd,  // I -> UD transition
    IToSc,  // I -> SC transition
    IToSd,  // I -> SD transition
    UcToSc, // UC -> SC transition
    UdToSd, // UD -> SD transition
};

// =====================================================================
// ChiCacheState property queries
// =====================================================================
bool chi_is_stable(ChiCacheState s);
bool chi_is_valid(ChiCacheState s);
bool chi_is_unique(ChiCacheState s);
bool chi_is_dirty(ChiCacheState s);
bool chi_can_provide_data(ChiCacheState s);
bool chi_can_read(ChiCacheState s);
bool chi_can_write(ChiCacheState s);
std::optional<ChiCacheState> chi_stable_state(ChiCacheState s);
const char* chi_state_str(ChiCacheState s);

// =====================================================================
// CoherenceRequest
// =====================================================================
enum class CoherenceRequest : uint8_t {
    ReadShared,
    ReadUnique,
    MakeUnique,
    CleanUnique,
    Evict,
    CleanShared,
    CleanInvalid,
    MakeInvalid,
};

// =====================================================================
// CoherenceResponse
// =====================================================================
struct CoherenceResponse {
    bool          data_valid = false;
    bool          data_dirty = false;
    ChiCacheState final_state = ChiCacheState::Invalid;
    bool          ack_only   = false;

    static CoherenceResponse with_data(bool dirty, ChiCacheState final_state);
    static CoherenceResponse ack(ChiCacheState final_state);
};

// =====================================================================
// StateTransitionResult
// =====================================================================
enum class StateTransitionResult : uint8_t {
    Complete, // completed successfully (state embedded in variant below)
    Pending,  // in progress, waiting for response
    Invalid,  // invalid transition
    Blocked,  // need to wait for ongoing transaction
};

struct StateTransition {
    StateTransitionResult result;
    ChiCacheState         state; // meaningful for Complete/Pending
};

// =====================================================================
// CoherenceStateMachine — static state machine for CHI coherence
// =====================================================================
class CoherenceStateMachine {
public:
    static StateTransition on_read_request(ChiCacheState current, bool want_unique);
    static StateTransition on_write_request(ChiCacheState current);
    static CoherenceResponse on_snoop_request(ChiCacheState current, ChiSnoopType snoop_type);
    static ChiCacheState complete_transition(ChiCacheState current, bool got_unique, bool got_dirty);

    /// Returns {final_state, needs_writeback} or std::nullopt if eviction not allowed.
    static std::optional<std::pair<ChiCacheState, bool>> on_evict(ChiCacheState current);
};

// =====================================================================
// Conversions between CacheLineState and ChiCacheState
// =====================================================================
ChiCacheState  to_chi_state(CacheLineState s);
CacheLineState to_cache_line_state(ChiCacheState s);

} // namespace arm_cpu
