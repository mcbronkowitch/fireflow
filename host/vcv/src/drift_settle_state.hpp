#pragma once

// The DRIFT knob's left stop, spec 2026-08-09 hw-control-reduction Task 8:
// SETL used to be its own momentary pad calling Instrument::settle() (drift
// target to zero, plus a ~1 s glide of EVOLVE and kick back to rest). DRIFT's
// own axis already ends at zero, so the pad's job now belongs to the knob's
// lower kDriftSettleZone -- but settle() must fire once on the ENTRY into
// that zone, not on every control tick a parked knob happens to sit there
// (Center::settle()/ModLane::settle() restart their glide countdown on every
// call, so a re-fired settle() every tick would never let the glide finish
// and the modulation resume).
//
// Same seeded/rearm shape as bbd_edge_state.hpp and song_rung_state.hpp, for
// the identical reason those two exist as their own tested types (spec
// 2026-08-09 hw-control-reduction Task 3's review found this exact bug
// class): a patch that RESTORES with DRIFT already parked at the stop --
// fresh module add, whole-patch open, Ctrl+D duplicate, an already-live
// preset Load/paste, or an Initialize -- must not panic a freshly-loaded (or
// freshly-reset) deck's weather/EVOLVE/kick down to zero on the very next
// control tick, merely because the restored value happens to sit in the
// zone. Only a genuine false -> true transition observed across two
// already-seeded ticks counts as an entry.
namespace spkyvcv {

struct DriftSettleState {
    bool inZone = false;
    bool seeded = false;

    // Call once per control tick with this tick's "is the knob at or below
    // the settle zone right now" state. Returns true exactly on a genuine
    // false -> true transition observed on an already-seeded instance --
    // never on the very first call, which instead establishes the baseline.
    bool tick(bool zoneNow) {
        if (!seeded) {
            inZone = zoneNow;
            seeded = true;
            return false;
        }
        const bool entered = zoneNow && !inZone;
        inZone = zoneNow;
        return entered;
    }

    // Call when a JSON restore lands on an ALREADY-LIVE instance (Fireflow's
    // dataFromJson(), curSr>0.f branch), or when Rack's Initialize resets
    // params to default on an already-ticking module (onReset() -- Rack
    // resets the DRIFT param BEFORE calling onReset()). Re-arms so the very
    // next tick() treats whatever DRIFT position the restore/reset just set
    // as a fresh baseline instead of a transition from the stale pre-restore
    // state. See BbdEdgeState::rearm()/SongRungState::rearm() for the
    // identical reasoning.
    //
    // Deliberately NOT needed for a fresh module add / whole-patch open /
    // Ctrl+D duplicate: those restore into an instance whose tick() has
    // never yet been called, so `seeded` is still at its construction-time
    // false and the ordinary first-call baseline in tick() already applies.
    void rearm() { seeded = false; }
};

} // namespace spkyvcv
