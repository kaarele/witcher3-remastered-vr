#include "shadow_cascade_authority_policy.h"

#include <iostream>

int main() {
    using w3vr::shadow_cascade::AuthorityAction;
    using w3vr::shadow_cascade::caller_kind;
    using w3vr::shadow_cascade::decide_authority;

    bool ok = true;
    ok &= caller_kind(0x01E0AB0F) == 0;
    ok &= caller_kind(0x01E0ADDC) == 1;
    ok &= caller_kind(0x01E0AB10) == -1;

    ok &= decide_authority(false, 0, 0, 0, 1604, 7, 0) ==
        AuthorityAction::Seed;
    ok &= decide_authority(true, 1603, 7, 0, 1604, 7, 1) ==
        AuthorityAction::Seed;
    ok &= decide_authority(true, 1604, 6, 0, 1604, 7, 1) ==
        AuthorityAction::Seed;
    ok &= decide_authority(true, 1604, 7, 0, 1604, 7, 0) ==
        AuthorityAction::SameEye;
    ok &= decide_authority(true, 1604, 7, 0, 1604, 7, 1) ==
        AuthorityAction::Reuse;

    using w3vr::shadow_cascade::ReuseFailSafe;
    using w3vr::shadow_cascade::note_reuse_attempt;
    {
        // Consecutive mismatches trip at the limit; a match resets the run.
        ReuseFailSafe state{};
        for (int i = 0; i < 3; ++i) {
            ok &= !note_reuse_attempt(state, true, 4, 256, 32);
        }
        ok &= !note_reuse_attempt(state, false, 4, 256, 32);
        for (int i = 0; i < 3; ++i) {
            ok &= !note_reuse_attempt(state, true, 4, 256, 32);
        }
        ok &= note_reuse_attempt(state, true, 4, 256, 32);
    }
    {
        // Scattered mismatches trip once the window holds more than the limit.
        ReuseFailSafe state{};
        bool tripped = false;
        int attempts = 0;
        while (!tripped && attempts < 64) {
            tripped = note_reuse_attempt(state, attempts % 2 == 0, 8, 64, 4);
            ++attempts;
        }
        ok &= tripped && attempts == 9;
    }
    {
        // The window restarts: the same scattered rate below the limit per
        // window never trips.
        ReuseFailSafe state{};
        bool tripped = false;
        for (int i = 0; i < 1024; ++i) {
            tripped |= note_reuse_attempt(state, i % 64 == 0, 8, 64, 4);
        }
        ok &= !tripped;
    }

    if (!ok) {
        std::cerr << "shadow cascade authority policy tests failed\n";
        return 1;
    }
    return 0;
}
