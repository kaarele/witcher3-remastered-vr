#pragma once

#include <cstdint>

namespace w3vr::shadow_cascade {

enum class AuthorityAction : uint8_t {
    Seed,
    Reuse,
    SameEye,
};

constexpr int caller_kind(uintptr_t return_rva) {
    switch (return_rva) {
    case 0x01E0AB0F:
        return 0;
    case 0x01E0ADDC:
        return 1;
    default:
        return -1;
    }
}

constexpr AuthorityAction decide_authority(
    bool slot_valid,
    uint64_t slot_pair,
    uint32_t slot_generation,
    uint32_t authority_eye,
    uint64_t incoming_pair,
    uint32_t incoming_generation,
    uint32_t incoming_eye) {
    if (!slot_valid || slot_pair != incoming_pair ||
        slot_generation != incoming_generation) {
        return AuthorityAction::Seed;
    }
    return authority_eye == incoming_eye
        ? AuthorityAction::SameEye
        : AuthorityAction::Reuse;
}

// Pair-reuse fail-safe (5.01 remaster_shadow_pair_reuse): one call per reuse
// attempt. Trips after |consecutive_limit| mismatching attempts in a row, or
// after more than |window_limit| mismatches within one window of
// |window_size| attempts.
struct ReuseFailSafe {
    uint32_t consecutive{};
    uint32_t window_attempts{};
    uint32_t window_mismatches{};
};

constexpr bool note_reuse_attempt(
    ReuseFailSafe& state,
    bool mismatch,
    uint32_t consecutive_limit,
    uint32_t window_size,
    uint32_t window_limit) {
    state.consecutive = mismatch ? state.consecutive + 1 : 0;
    ++state.window_attempts;
    if (mismatch) {
        ++state.window_mismatches;
    }
    const bool trip = state.consecutive >= consecutive_limit ||
        state.window_mismatches > window_limit;
    if (state.window_attempts >= window_size) {
        state.window_attempts = 0;
        state.window_mismatches = 0;
    }
    return trip;
}

}  // namespace w3vr::shadow_cascade
