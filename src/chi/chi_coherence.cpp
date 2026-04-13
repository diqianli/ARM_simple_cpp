/// @file chi_coherence.cpp
/// @brief ChiCacheState property queries, CoherenceStateMachine, and
///        CacheLineState <-> ChiCacheState conversions.

#include "arm_cpu/chi/chi_coherence.hpp"

#include <optional>

namespace arm_cpu {

// =====================================================================
// ChiCacheState property queries
// =====================================================================

bool chi_is_stable(ChiCacheState s) {
    switch (s) {
        case ChiCacheState::Invalid:
        case ChiCacheState::UniqueClean:
        case ChiCacheState::UniqueDirty:
        case ChiCacheState::SharedClean:
        case ChiCacheState::SharedDirty:
            return true;
        default: return false;
    }
}

bool chi_is_valid(ChiCacheState s) {
    return s != ChiCacheState::Invalid;
}

bool chi_is_unique(ChiCacheState s) {
    switch (s) {
        case ChiCacheState::UniqueClean:
        case ChiCacheState::UniqueDirty:
        case ChiCacheState::UcToI:
        case ChiCacheState::UdToI:
        case ChiCacheState::IToUc:
        case ChiCacheState::IToUd:
            return true;
        default: return false;
    }
}

bool chi_is_dirty(ChiCacheState s) {
    switch (s) {
        case ChiCacheState::UniqueDirty:
        case ChiCacheState::SharedDirty:
        case ChiCacheState::UdToI:
        case ChiCacheState::SdToI:
        case ChiCacheState::IToUd:
        case ChiCacheState::IToSd:
        case ChiCacheState::UdToSd:
            return true;
        default: return false;
    }
}

bool chi_can_provide_data(ChiCacheState s) {
    switch (s) {
        case ChiCacheState::UniqueClean:
        case ChiCacheState::UniqueDirty:
        case ChiCacheState::SharedDirty:
        case ChiCacheState::UcToI:
        case ChiCacheState::UdToI:
        case ChiCacheState::SdToI:
            return true;
        default: return false;
    }
}

bool chi_can_read(ChiCacheState s) {
    return chi_is_valid(s);
}

bool chi_can_write(ChiCacheState s) {
    switch (s) {
        case ChiCacheState::UniqueClean:
        case ChiCacheState::UniqueDirty:
        case ChiCacheState::IToUc:
        case ChiCacheState::IToUd:
            return true;
        default: return false;
    }
}

std::optional<ChiCacheState> chi_stable_state(ChiCacheState s) {
    switch (s) {
        case ChiCacheState::UcToI:
        case ChiCacheState::UdToI:
        case ChiCacheState::ScToI:
        case ChiCacheState::SdToI:
            return ChiCacheState::Invalid;
        case ChiCacheState::IToUc: return ChiCacheState::UniqueClean;
        case ChiCacheState::IToUd: return ChiCacheState::UniqueDirty;
        case ChiCacheState::IToSc: return ChiCacheState::SharedClean;
        case ChiCacheState::IToSd: return ChiCacheState::SharedDirty;
        case ChiCacheState::UcToSc: return ChiCacheState::SharedClean;
        case ChiCacheState::UdToSd: return ChiCacheState::SharedDirty;
        default: return std::nullopt; // Already stable
    }
}

const char* chi_state_str(ChiCacheState s) {
    switch (s) {
        case ChiCacheState::Invalid:      return "I";
        case ChiCacheState::UniqueClean:  return "UC";
        case ChiCacheState::UniqueDirty:  return "UD";
        case ChiCacheState::SharedClean:  return "SC";
        case ChiCacheState::SharedDirty:  return "SD";
        case ChiCacheState::UcToI:        return "UC->I";
        case ChiCacheState::UdToI:        return "UD->I";
        case ChiCacheState::ScToI:        return "SC->I";
        case ChiCacheState::SdToI:        return "SD->I";
        case ChiCacheState::IToUc:        return "I->UC";
        case ChiCacheState::IToUd:        return "I->UD";
        case ChiCacheState::IToSc:        return "I->SC";
        case ChiCacheState::IToSd:        return "I->SD";
        case ChiCacheState::UcToSc:       return "UC->SC";
        case ChiCacheState::UdToSd:       return "UD->SD";
    }
    return "Unknown";
}

// =====================================================================
// CoherenceResponse
// =====================================================================

CoherenceResponse CoherenceResponse::with_data(bool dirty, ChiCacheState final_state) {
    return {true, dirty, final_state, false};
}

CoherenceResponse CoherenceResponse::ack(ChiCacheState final_state) {
    return {false, false, final_state, true};
}

// =====================================================================
// CoherenceStateMachine
// =====================================================================

StateTransition CoherenceStateMachine::on_read_request(ChiCacheState current, bool want_unique) {
    switch (current) {
        case ChiCacheState::Invalid:
            return {StateTransitionResult::Pending,
                    want_unique ? ChiCacheState::IToUc : ChiCacheState::IToSc};
        case ChiCacheState::UniqueClean:
        case ChiCacheState::UniqueDirty:
            return {StateTransitionResult::Complete, current};
        case ChiCacheState::SharedClean:
        case ChiCacheState::SharedDirty:
            if (want_unique) {
                return {StateTransitionResult::Pending, current};
            }
            return {StateTransitionResult::Complete, current};
        default:
            return {StateTransitionResult::Blocked, current};
    }
}

StateTransition CoherenceStateMachine::on_write_request(ChiCacheState current) {
    switch (current) {
        case ChiCacheState::Invalid:
            return {StateTransitionResult::Pending, ChiCacheState::IToUd};
        case ChiCacheState::UniqueClean:
            return {StateTransitionResult::Complete, ChiCacheState::UniqueDirty};
        case ChiCacheState::UniqueDirty:
            return {StateTransitionResult::Complete, ChiCacheState::UniqueDirty};
        case ChiCacheState::SharedClean:
        case ChiCacheState::SharedDirty:
            return {StateTransitionResult::Pending, current};
        default:
            return {StateTransitionResult::Blocked, current};
    }
}

CoherenceResponse CoherenceStateMachine::on_snoop_request(
    ChiCacheState current, ChiSnoopType snoop_type)
{
    switch (snoop_type) {
        case ChiSnoopType::SnpOnce:
            if (chi_can_provide_data(current)) {
                return CoherenceResponse::with_data(chi_is_dirty(current), current);
            }
            return CoherenceResponse::ack(current);

        case ChiSnoopType::SnpShared:
            switch (current) {
                case ChiCacheState::UniqueClean:
                    return CoherenceResponse::with_data(false, ChiCacheState::SharedClean);
                case ChiCacheState::UniqueDirty:
                    return CoherenceResponse::with_data(true, ChiCacheState::SharedDirty);
                case ChiCacheState::SharedClean:
                case ChiCacheState::SharedDirty:
                    return CoherenceResponse::ack(current);
                default:
                    return CoherenceResponse::ack(ChiCacheState::Invalid);
            }

        case ChiSnoopType::SnpClean:
        case ChiSnoopType::SnpData:
            if (chi_can_provide_data(current)) {
                ChiCacheState final_state;
                if (chi_is_dirty(current)) {
                    final_state = ChiCacheState::SharedDirty;
                } else if (chi_is_unique(current)) {
                    final_state = ChiCacheState::SharedClean;
                } else {
                    final_state = current;
                }
                return CoherenceResponse::with_data(chi_is_dirty(current), final_state);
            }
            return CoherenceResponse::ack(current);

        case ChiSnoopType::SnpCleanShared:
            switch (current) {
                case ChiCacheState::UniqueDirty:
                case ChiCacheState::SharedDirty:
                    return CoherenceResponse::with_data(true, ChiCacheState::SharedClean);
                case ChiCacheState::UniqueClean:
                    return CoherenceResponse::with_data(false, ChiCacheState::SharedClean);
                case ChiCacheState::SharedClean:
                    return CoherenceResponse::ack(current);
                default:
                    return CoherenceResponse::ack(ChiCacheState::Invalid);
            }

        case ChiSnoopType::SnpCleanInvalid:
        case ChiSnoopType::SnpMakeInvalid:
            switch (current) {
                case ChiCacheState::UniqueDirty:
                case ChiCacheState::SharedDirty:
                    return CoherenceResponse::with_data(true, ChiCacheState::Invalid);
                case ChiCacheState::UniqueClean:
                    return CoherenceResponse::with_data(false, ChiCacheState::Invalid);
                case ChiCacheState::SharedClean:
                    return CoherenceResponse::ack(ChiCacheState::Invalid);
                default:
                    return CoherenceResponse::ack(ChiCacheState::Invalid);
            }

        case ChiSnoopType::SnpStashUnique:
        case ChiSnoopType::SnpStashShared:
            return CoherenceResponse::ack(current);
    }
    return CoherenceResponse::ack(current);
}

ChiCacheState CoherenceStateMachine::complete_transition(
    ChiCacheState current, bool got_unique, bool got_dirty)
{
    switch (current) {
        case ChiCacheState::IToUc: return ChiCacheState::UniqueClean;
        case ChiCacheState::IToUd: return ChiCacheState::UniqueDirty;
        case ChiCacheState::IToSc: return ChiCacheState::SharedClean;
        case ChiCacheState::IToSd: return ChiCacheState::SharedDirty;
        case ChiCacheState::UcToSc: return ChiCacheState::SharedClean;
        case ChiCacheState::UdToSd: return ChiCacheState::SharedDirty;
        case ChiCacheState::UcToI:
        case ChiCacheState::UdToI:
        case ChiCacheState::ScToI:
        case ChiCacheState::SdToI:
            return ChiCacheState::Invalid;
        case ChiCacheState::SharedClean:
        case ChiCacheState::SharedDirty:
            if (got_unique) {
                return got_dirty ? ChiCacheState::UniqueDirty : ChiCacheState::UniqueClean;
            }
            return current;
        default:
            return current;
    }
}

std::optional<std::pair<ChiCacheState, bool>> CoherenceStateMachine::on_evict(ChiCacheState current) {
    switch (current) {
        case ChiCacheState::UniqueDirty:
        case ChiCacheState::SharedDirty:
            return {{ChiCacheState::Invalid, true}};
        case ChiCacheState::UniqueClean:
        case ChiCacheState::SharedClean:
            return {{ChiCacheState::Invalid, false}};
        case ChiCacheState::Invalid:
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

// =====================================================================
// CacheLineState <-> ChiCacheState conversions
// =====================================================================

ChiCacheState to_chi_state(CacheLineState s) {
    switch (s) {
        case CacheLineState::Invalid:    return ChiCacheState::Invalid;
        case CacheLineState::Shared:     return ChiCacheState::SharedClean;
        case CacheLineState::Exclusive:  return ChiCacheState::UniqueClean;
        case CacheLineState::Modified:   return ChiCacheState::UniqueDirty;
        case CacheLineState::Unique:     return ChiCacheState::UniqueClean;
    }
    return ChiCacheState::Invalid;
}

CacheLineState to_cache_line_state(ChiCacheState s) {
    switch (s) {
        case ChiCacheState::Invalid:     return CacheLineState::Invalid;
        case ChiCacheState::UniqueClean:
        case ChiCacheState::IToUc:
        case ChiCacheState::UcToSc:
        case ChiCacheState::UcToI:
            return CacheLineState::Unique;
        case ChiCacheState::UniqueDirty:
        case ChiCacheState::IToUd:
        case ChiCacheState::UdToSd:
        case ChiCacheState::UdToI:
            return CacheLineState::Modified;
        case ChiCacheState::SharedClean:
        case ChiCacheState::IToSc:
        case ChiCacheState::ScToI:
        case ChiCacheState::SdToI:
            return CacheLineState::Shared;
        case ChiCacheState::SharedDirty:
        case ChiCacheState::IToSd:
            return CacheLineState::Modified;
    }
    return CacheLineState::Invalid;
}

} // namespace arm_cpu
