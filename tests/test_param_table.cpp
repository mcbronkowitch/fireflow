// tests/test_param_table.cpp
#include "doctest/doctest.h"
#include "param_table.h"
#include "instrument.h"
using namespace spky;

TEST_CASE("param table: table is sane") {
    for (int p = 0; p < P_COUNT; ++p) {
        CAPTURE(kParams[p].name);
        CHECK(kParams[p].lo < kParams[p].hi);
        if (kParams[p].steps > 0) CHECK(kParams[p].steps >= 2);
    }
}

TEST_CASE("param table: inventory marker -- P_MODE, P_PACE, P_COUNT") {
    // Inherited from the deleted tests/test_flow_mode.cpp, whose own comment
    // put it best: "The first two CHECKs are the invariant. The third is
    // NOT -- it is a deliberate INVENTORY MARKER, and appending a parameter
    // WILL redden it. That is the intent: appending is free ... but it is
    // not free for the reader, and the one edit it should cost is bumping
    // this line while re-reading the paragraph above."
    //
    // The removal spec (docs/superpowers/specs/2026-08-14-flow-glow-removal-
    // design.md, 4.4) judged the original's first two CHECKs moot -- they
    // pinned P_MODE's absolute index because the deleted terrain generator
    // keyed its per-parameter RNG streams by index, and nothing keys a
    // stream by index any more. That judgment was correct and this case does
    // NOT restate those two.
    //
    // But the third CHECK was never about terrain streams -- it is the only
    // thing in this suite that pins P_COUNT, i.e. the only guard against a
    // parameter being inserted anywhere in the enum without a human noticing.
    // That matters more now than it did before this branch: commit 1 added
    // tests/param_impact_points.h, whose frozen `float v[P_COUNT]` vectors
    // are bound BY POSITION. Insert a ParamId anywhere before the end and
    // every later row silently shifts one slot -- C++ zero-fills the tail of
    // a short aggregate initializer without a warning, so the file still
    // compiles. The parameter-impact gate then goes red, but it reports
    // "these parameters died", not "your frozen patches are stale": the
    // diagnosis cost is high and the failure actively misleads. Appending
    // P_COUNT - 1 restores.
    //
    // So: if this CHECK reddens because you inserted a parameter, do not
    // treat it as a terrain-stream problem (there is no terrain layer left
    // to reseed) -- treat it as a instruction to go re-check
    // tests/param_impact_points.h's comment for what an insertion does to
    // those vectors, then bump P_MODE and P_PACE below to match the enum as
    // it now stands and re-read this paragraph again before moving on.
    // 62 -> 64 on 2026-08-19: P_EDGE_A/P_EDGE_B were INSERTED after P_FILT_B
    // (spec voice-knobs-dpth-edge, 4.4), not appended, so this case reddened
    // exactly as the paragraph above says it should.
    // 64 -> 62 on 2026-08-20: P_EDGE_A/P_EDGE_B were REMOVED (spec
    // edge-knob-removal, task 2) as part of taking the EDGE knob out of the
    // instrument entirely. A removal is a hard compile error in
    // tests/param_impact_points.h's frozen vectors -- each `float
    // v[P_COUNT]` became two elements too long -- so this case's own red was
    // proved only after that first, stronger tripwire had already fired and
    // been fixed. The frozen vectors were un-shifted by hand in the same
    // commit; see that file's header for the record of both moves.
    CHECK(P_MODE == 62);
    CHECK(P_PACE == P_MODE + 1);
    CHECK(P_PACE == P_COUNT - 1);      // inventory marker: bump on append
}

TEST_CASE("param table: the two rows other code reads by hand") {
    // P_MODE is discrete and binary; P_PACE is continuous 0..1 with 0.5 = x1.
    // Inherited from tests/test_flow_mode.cpp (removal spec
    // docs/superpowers/specs/2026-08-14-flow-glow-removal-design.md, 4.4).
    CHECK(kParams[P_MODE].steps == 2);
    CHECK(kParams[P_MODE].lo == doctest::Approx(0.f));
    CHECK(kParams[P_MODE].hi == doctest::Approx(1.f));
    CHECK(kParams[P_PACE].steps == 0);
    CHECK(kParams[P_PACE].lo == doctest::Approx(0.f));
    CHECK(kParams[P_PACE].hi == doctest::Approx(1.f));
}

TEST_CASE("param table: apply_mode_and_steps reaches what apply_param refuses") {
    Instrument in;
    in.init(48000.f);          // as the file's existing "apply routes to the
                               // engine" case does -- no FX chain needed
    // apply_param cannot route these three -- it is per-parameter and stateless.
    apply_param(in, P_STEPS_A, 6.f);
    CHECK(in.deck_steps(PART_A) != 6);      // the documented refusal
    apply_mode_and_steps(in, true, 6, 11);
    CHECK(in.deck_steps(PART_A) == 6);
    CHECK(in.deck_steps(PART_B) == 11);
    // The deleted test_flow_mode.cpp:59 invariant ("steps never run without a
    // grid") -- step_on() must track synced() on both decks -- was true only
    // because it was drawn from a single global P_MODE row. Now that this
    // function is the only place forcing STEP and SYNC together, the
    // invariant is asserted nowhere else; pin it here.
    CHECK(in.step_on(PART_A) == in.synced(PART_A));
    CHECK(in.step_on(PART_B) == in.synced(PART_B));
}

TEST_CASE("param table: apply routes to the engine (spot checks via observers)") {
    Instrument inst;
    inst.init(48000.f);                       // engine only, no FX chain needed
    float l, r;

    apply_param(inst, P_ENGINE_A, float(ENGINE_BODY));
    // set_engine() crossfades rather than swapping instantly (part.cpp:
    // _engine_fade.set_on(false), swap lands once the fade is idle), so
    // engine_id() only reflects it after the fade has had samples to run --
    // the same pattern tests/test_instrument.cpp uses around its own
    // set_engine() checks.
    for (int i = 0; i < 1000; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.engine_id(PART_A) == ENGINE_BODY);

    // Non-default value (Principle::Hierarchical == 2 is SongForm's own
    // default -- see engine/mod/song_form.h:52 -- so checking form() == 2
    // would pass even with a broken/missing P_FORM_A case). form_pending
    // is the same deferred mechanism as song_pending (ModLane::set_form(),
    // lane.cpp:107-114, applied by the same _apply_pending_song_work() as
    // SONG), so it needs the same STEP-entry preroll push described below.
    apply_param(inst, P_FORM_A, 1.f);              // OneMotif, not the default
    inst.set_step(PART_A, true, 4);
    inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.form(PART_A) == 1);

    // SONG is boundary-safe by design (tests/test_song_lane.cpp: "SONG-only
    // change is boundary-safe"), so song() only picks up a pending value at
    // a phrase boundary. Put the deck into STEP mode while its lane is still
    // pre-first-step (_cur_step == -1, true right after init()) so the very
    // next process() sample applies the pending work through the preroll
    // path (lane.cpp: _apply_preroll_work), instead of needing a full
    // phrase-wrap loop.
    apply_param(inst, P_SONG_B, 1.f);
    inst.set_step(PART_B, true, 4);
    inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.song(PART_B) == 1);

    // Discrete params clamp, never wrap: over-range engine id stays legal
    // AND actually lands on the clamped top of the range (99 clamps to
    // hi=6 == ENGINE_FEED). Needs the same crossfade-settling process()
    // loop as the P_ENGINE_A check above -- reading engine_id() right after
    // apply_param() would silently read the pre-change boot default and
    // pass even with a deleted/broken P_ENGINE_B case.
    apply_param(inst, P_ENGINE_B, 99.f);
    for (int i = 0; i < 1000; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    CHECK(inst.engine_id(PART_B) == ENGINE_FEED);
    CHECK(inst.engine_id(PART_B) >= 0);
    CHECK(inst.engine_id(PART_B) < ENGINE_COUNT);
}
