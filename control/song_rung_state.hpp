#pragma once

#include "mod/song_ladder.h"

// The SONG-knob rung tracker used by Fireflow::pushParams() (spec 2026-08-09
// hw-control-reduction, Task 3 review). Kept dependency-free (no Rack, no
// engine/** beyond the header-only song_ladder.h) so it can be exercised
// directly by ctest as well as compiled into the VCV plugin -- see
// tests/test_song_rung_state.cpp. Same seeded/rearm shape as BbdEdgeState
// (bbd_edge_state.hpp) and for the same reason.
//
// Invariant: a genuine turn of the SONG pot fires a re-roll exactly on the
// rung it lands on; a rung ARRIVING BY RESTORE -- fresh module add,
// whole-patch open, Ctrl+D duplicate, an already-live preset Load/paste, or
// Initialize -- must never fire, because the restored/reset rung is exactly
// what the player saved (or the factory default), not a gesture asking for a
// new melody.
//
// Review round 1 (Task 3) shipped without this: `songRung` was a bare
// `int[PART_COUNT]` that always started seeded at 0, so any loaded rung != 0
// looked like a giant turn of the knob on the very first control tick and
// fired new_phrase() (and sampler_punch() on a Sampler deck) before the
// player ever heard what they saved.
namespace spkyvcv {

struct SongRungState {
    int rung = 0;
    bool seeded = false;

    // Call once per control tick with this tick's normalized SONG pot
    // position (0..1) and the ladder's rung count. Returns true exactly on a
    // genuine rung change observed across two calls on an already-seeded
    // instance -- never on the first call (or the first call after
    // rearm()), which instead adopts the pot's CURRENT position as the
    // baseline.
    //
    // That first-call adoption deliberately bypasses spky::hyst_step's
    // hysteresis: hyst_step(-1000, ...) always fails its hold guard (its
    // `cur` sits far outside the ladder's valid range, so the "did this move
    // more than one step" check is unconditionally true), so it always
    // returns its `nearest` computation -- the exact rung the restored/reset
    // pot position snaps to, not whatever a stale or construction-default
    // `rung` happens to hold. A restore must land exactly where it was
    // saved, never wherever hysteresis would have held a live pot.
    bool tick(float norm, int count) {
        if (!seeded) {
            rung = spky::hyst_step(-1000, norm, count);
            seeded = true;
            return false;
        }
        const int next = spky::hyst_step(rung, norm, count);
        const bool changed = next != rung;
        rung = next;
        return changed;
    }

    // Call when a rung-bearing param is about to be (or was just) restored
    // into an ALREADY-TICKING instance: a JSON restore landing on an
    // already-live module (Fireflow's dataFromJson(), `curSr > 0.f` branch)
    // or Initialize (onReset() -- Rack resets params to their default before
    // calling it). Re-arms so the very next tick() treats whatever rung that
    // restore/reset just set as a fresh baseline instead of comparing it
    // against the stale pre-restore rung.
    //
    // Deliberately NOT needed for a fresh module add / whole-patch open /
    // Ctrl+D duplicate: those restore into an instance whose tick() has
    // never yet been called, so `seeded` is still at its construction-time
    // false and the ordinary first-call baseline above already applies.
    void rearm() { seeded = false; }
};

} // namespace spkyvcv
