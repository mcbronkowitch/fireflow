#include <doctest/doctest.h>

#include "vcv/src/song_rung_state.hpp"


// Covers the defect a Task 3 review round found (spec 2026-08-09
// hw-control-reduction): a rung ARRIVING BY RESTORE -- fresh module add,
// whole-patch open, Ctrl+D duplicate, an already-live preset Load/paste, or
// Initialize -- must never fire a re-roll, only a genuine turn of the SONG
// pot may. hyst_step's own hysteresis math is already covered end-to-end by
// tests/test_song_ladder.cpp; these cases are specifically about the
// seeded/rearm baseline this state adds on top of it.

TEST_CASE("SongRungState: a genuine pot turn fires on each rung change")
{
    spkyvcv::SongRungState state;
    const int n = 14;

    CHECK(state.tick(0.f, n) == false);          // baseline (first-ever call)
    CHECK(state.rung == 0);

    CHECK(state.tick(5.f / 13.f, n) == true);    // player turns to rung 5
    CHECK(state.rung == 5);
    CHECK(state.tick(5.f / 13.f, n) == false);   // still rung 5 -- must NOT re-fire

    CHECK(state.tick(10.f / 13.f, n) == true);   // player turns to rung 10
    CHECK(state.rung == 10);
}

TEST_CASE("SongRungState: fresh construction landing on a nonzero rung does "
          "not fire (fresh module add / whole-patch open / Ctrl+D duplicate)")
{
    // Models a saved patch whose SONG_A sits at rung 9 loading into a
    // brand-new module instance: params are restored before the first
    // control tick ever runs, so the first tick() call must adopt that rung
    // as the baseline, not treat it as a turn from the construction-time
    // default of 0.
    spkyvcv::SongRungState state;
    const int n = 14;

    CHECK(state.tick(9.f / 13.f, n) == false);   // first-ever observation: baseline
    CHECK(state.rung == 9);
    CHECK(state.tick(9.f / 13.f, n) == false);   // still rung 9
}

TEST_CASE("SongRungState: already-live restore (rearm) does not fire, but a "
          "later genuine turn still does")
{
    // Models the case Fireflow's dataFromJson() `curSr > 0.f` branch (an
    // already-live module receiving a right-click preset Load or module
    // paste) and onReset() (Initialize, which Rack resets params for before
    // calling) both hit: the instance is already seeded from ticks before
    // the restore, so without an explicit rearm() the stale pre-restore
    // rung would make the restored value look like a giant turn of the knob
    // and fire new_phrase()/sampler_punch() before the player ever hears
    // what they loaded.
    spkyvcv::SongRungState state;
    const int n = 14;

    CHECK(state.tick(0.f, n) == false);          // live module, rung 0
    CHECK(state.tick(0.f, n) == false);          // still rung 0

    state.rearm();                                // dataFromJson()/onReset()
    // The restore just set the pot to rung 9; the next tick() must treat
    // that as a baseline, not a transition.
    CHECK(state.tick(9.f / 13.f, n) == false);   // post-restore: must NOT fire
    CHECK(state.rung == 9);
    CHECK(state.tick(9.f / 13.f, n) == false);   // still rung 9

    // A later GENUINE player-driven turn must still fire after a rearm() --
    // the fix must not have disabled real change detection.
    CHECK(state.tick(3.f / 13.f, n) == true);    // player turns to rung 3
    CHECK(state.rung == 3);
}

TEST_CASE("SongRungState: a single-detent click fires, walking the whole "
          "ladder one rung at a time")
{
    // CRITICAL 1 (2026-08-09 hw-control-reduction final review): SONG is a
    // Rack configSwitch, so every real turn this state ever sees is exactly
    // one detent away from the last -- never the multi-rung jumps the other
    // cases above exercise. hyst_step's own boundary math is pinned in
    // tests/test_song_ladder.cpp; this walks it through the actual host
    // entry point (tick(), with the host's norm = rung / (count - 1)) to
    // prove a click-by-click player gesture is never swallowed.
    spkyvcv::SongRungState state;
    const int n = 14;

    CHECK(state.tick(0.f, n) == false);   // baseline
    for (int rung = 1; rung < n; ++rung) {
        CHECK(state.tick(float(rung) / 13.f, n) == true);
        CHECK(state.rung == rung);
    }
    for (int rung = n - 2; rung >= 0; --rung) {
        CHECK(state.tick(float(rung) / 13.f, n) == true);
        CHECK(state.rung == rung);
    }
}
