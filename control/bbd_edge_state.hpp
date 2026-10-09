#pragma once

// The ENG-switch-into-BBD edge detector used by Fireflow::pushParams() (spec
// 2026-07-31 bbd-part-engine, Task 10). Kept dependency-free (no Rack, no
// engine/**) so it can be exercised directly by ctest as well as compiled
// into the VCV plugin -- see tests/test_bbd_edge_state.cpp.
//
// Invariant: entering BBD by PLAYER ACTION applies the FLUX-off/
// exciteOtherDeck-on defaults exactly once, on the transition; arriving on
// BBD because state was RESTORED -- fresh module add, whole-patch open,
// Ctrl+D duplicate, or an already-live preset Load/module paste -- must
// never apply them, because the restored state (including a deliberately
// re-enabled FLUX) is exactly what the player saved.
//
// This took two review rounds to get right, which is the whole reason it now
// lives in its own tested unit rather than as inline bools in Fireflow.cpp:
//   - Round 1 missed that ANY restore into a freshly-constructed Module
//     (fresh add / whole-patch open / duplicate) must treat its first
//     observed ENG state as a baseline, not a transition -- fixed by
//     `seeded`/tick()'s first-call behaviour.
//   - Round 2 missed that dataFromJson() is ALSO called on an ALREADY-LIVE
//     module (right-click Load preset, module paste -- Fireflow.cpp's own
//     `curSr > 0.f` branch is exactly that case): a second restore into the
//     same instance leaves `seeded` already true and `wasBbd` stale from
//     BEFORE the restore, so it needs an explicit re-arm -- rearm().
namespace spkyvcv {

struct BbdEdgeState {
    bool wasBbd = false;
    bool seeded = false;

    // Call once per control tick with this tick's "is this part on the BBD
    // engine right now" state. Returns true exactly on a genuine
    // false -> true transition observed across two calls to tick() on an
    // already-seeded instance -- never on the very first call, which instead
    // establishes the baseline.
    bool tick(bool bbdPart) {
        if (!seeded) {
            wasBbd = bbdPart;
            seeded = true;
        }
        const bool entered = bbdPart && !wasBbd;
        wasBbd = bbdPart;
        return entered;
    }

    // Call when a JSON restore lands on an ALREADY-LIVE instance (Fireflow's
    // dataFromJson(), `curSr > 0.f` branch). Re-arms so the very next tick()
    // treats whatever ENG the restore just set as a fresh baseline instead
    // of a transition from the stale pre-restore state.
    //
    // Deliberately NOT needed for a fresh module add / whole-patch open /
    // Ctrl+D duplicate: those restore into an instance whose tick() has
    // never yet been called, so `seeded` is still at its construction-time
    // false and the ordinary first-call baseline in tick() already applies.
    void rearm() { seeded = false; }
};

} // namespace spkyvcv
