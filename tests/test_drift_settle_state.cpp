#include <doctest/doctest.h>

#include "vcv/src/drift_settle_state.hpp"


// The DRIFT knob's left stop swallowed the SETL pad (spec 2026-08-09
// hw-control-reduction Task 8): Fireflow.cpp lives inside a Rack Module,
// unreachable from this suite, so DriftSettleState -- the dependency-free
// edge detector that decides when Instrument::settle() actually fires --
// is exercised directly here, the same way bbd_edge_state.hpp and
// song_rung_state.hpp are covered by their own tests. This is the "settle()
// fires once on entry, not on every tick a parked knob sits there" proof.

TEST_CASE("DriftSettleState: genuine player entry into the zone fires exactly once")
{
    spkyvcv::DriftSettleState s;

    CHECK(s.tick(false) == false);   // knob well off the stop
    CHECK(s.tick(false) == false);   // still off the stop
    CHECK(s.tick(true)  == true);    // player parks it at the stop -- must fire
    CHECK(s.tick(true)  == false);   // parked -- must NOT re-fire
    CHECK(s.tick(true)  == false);   // parked, again -- still must NOT re-fire
}

TEST_CASE("DriftSettleState: leaving the zone and genuinely returning fires again")
{
    spkyvcv::DriftSettleState s;

    CHECK(s.tick(false) == false);   // baseline (first-ever call): off the stop
    CHECK(s.tick(true)  == true);    // player enters the zone -- must fire
    CHECK(s.tick(false) == false);   // player backs off the stop
    CHECK(s.tick(true)  == true);    // a real second entry -- must fire again
}

TEST_CASE("DriftSettleState: fresh construction already parked at the stop never fires "
          "(fresh add / whole-patch open / Ctrl+D duplicate)")
{
    // Models pushParams()'s very first tick on a freshly-constructed module
    // whose DRIFT was already in the zone when the JSON (or the factory
    // default) landed -- a patch saved hard left must not panic the instant
    // it loads. The very first tick() call establishes the baseline instead
    // of a transition.
    spkyvcv::DriftSettleState s;

    CHECK(s.tick(true) == false);    // first-ever observation: baseline, not a fire
    CHECK(s.tick(true) == false);    // still parked
}

TEST_CASE("DriftSettleState: already-live restore (preset Load/paste, or Initialize) "
          "landing in the zone does not fire, but a later genuine entry still does")
{
    // Models the case dataFromJson()'s curSr>0.f branch (and onReset(),
    // since Rack resets params before calling it) handles: a module already
    // live and ticking with DRIFT off the stop receives a restore that lands
    // DRIFT in the zone. Without rearm(), the instance is already seeded
    // from before the restore and the stale inZone==false would make this
    // look like a genuine transition and fire.
    spkyvcv::DriftSettleState s;

    CHECK(s.tick(false) == false);   // live module, DRIFT off the stop
    CHECK(s.tick(false) == false);   // still off

    s.rearm();                        // dataFromJson()'s curSr>0.f branch, or onReset()
    // The restore just set DRIFT into the zone on the param the caller
    // reads; the next tick() must treat that as a baseline, not a
    // transition, matching whatever the preset saved (no panic glide the
    // player never asked for).
    CHECK(s.tick(true) == false);    // post-restore: must NOT fire
    CHECK(s.tick(true) == false);    // still parked

    // A later GENUINE player-driven leave-and-return must still work after
    // a rearm() -- the fix must not have disabled real edge detection.
    CHECK(s.tick(false) == false);   // player backs off the stop
    CHECK(s.tick(true)  == true);    // player returns -- must fire
}
