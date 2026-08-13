// tests/test_flow_melody.cpp
//
// The FLOW melody engine (spec 2026-08-13 flow-melody-engine-design). In the
// free mode the melodic lane stops being a continuous LFO and becomes a slot
// sequencer without rhythm: one cycle is one phrase pass, DENSITY selects k of
// L slots through the groove ranking, and the slots it skips HOLD the previous
// note.
#include <cmath>
#include <doctest/doctest.h>
#include "mod/lane.h"

using namespace spky;

namespace {

// A melodic lane in FLOW melody mode: 8 slots per cycle, one cycle per second.
//
// 1 Hz is deliberate, not arbitrary. At 48 kHz a cycle is 48000 samples and a
// slot 6000 (125 ms), comfortably above the 60 ms note-rate floor Task 6 adds,
// so every case in this file keeps its meaning once that floor exists. A faster
// lane would start colliding with the floor and the fire counts below would
// silently become "whatever the floor allows".
//
// set_step() and set_form()/set_song() run BEFORE init(), mirroring
// tests/test_song_lane.cpp: init() generates the phrase and reads that state.
ModLane make_flow_melody_lane(uint32_t seed, float hz = 1.f, int steps = 8) {
    ModLane lane;
    lane.set_melodic(true);
    lane.set_step(false, steps);
    lane.set_form(Principle::Hierarchical);
    lane.set_song(SongMode::AAAB);
    lane.init(48000.f, seed);
    lane.set_flow_melody(true);
    lane.set_rate_hz(hz);
    lane.set_density(1.f);
    lane.set_variation(0.f);
    lane.set_smooth(0.f);
    return lane;
}

// Safety bound for every drive loop here: 4 M samples is ~83 s at 48 kHz, far
// past any cycle this file configures.
constexpr int kDriveGuard = 4000000;

void drive_to_wrap(ModLane& lane) {
    for (int i = 0; i < kDriveGuard; ++i) {
        lane.process();
        if (lane.wrapped()) return;
    }
    FAIL("lane did not wrap within the safety bound");
}

// Count fires over `cycles` whole cycles, starting from a wrap.
//
// Starting on a wrap is what makes the count exact: each cycle contains exactly
// one entry per slot, so a window that runs wrap-to-wrap has no partial slot at
// either end and the answer is (fires per cycle) * cycles.
int fires_over_cycles(ModLane& lane, int cycles) {
    drive_to_wrap(lane);
    int fires = 0, wraps = 0;
    for (int i = 0; i < kDriveGuard && wraps < cycles; ++i) {
        lane.process();
        if (lane.fired()) ++fires;
        if (lane.wrapped()) ++wraps;
    }
    REQUIRE(wraps == cycles);
    return fires;
}

} // namespace

TEST_CASE("FLOW melody: the lane fires once per slot, not once per cycle") {
    // 0xF10Eu, not 0xF10Wu: "FLOW" cannot be spelled in hex (W is not a hex
    // digit) the way tests/test_flow_terrain.cpp spells ALICE/COFFEE/etc., and
    // the straight pun (0xF10Wu) is an invalid integer-literal suffix -- it
    // fails to compile, not fails the assertion. Any seed works here; this one
    // stays close to the intended pun.
    ModLane lane = make_flow_melody_lane(0xF10Eu);
    // Two cycles so the count cannot be satisfied by a single accidental edge.
    CHECK(fires_over_cycles(lane, 2) == 16);
}

TEST_CASE("FLOW melody: the emitted value is the phrase's note, held") {
    ModLane lane = make_flow_melody_lane(0xF10Eu);
    drive_to_wrap(lane);

    // Walk one cycle and check that at every sample the pre-slew target is the
    // active pattern's pitch for the slot the phase is in. This is the whole
    // contract of the new state in one assertion: the value comes from the
    // phrase, and it does not move between boundaries.
    const uint8_t active = lane.active_pattern();
    const MelodyPattern& pattern = lane.pattern_for_test(active);
    for (int i = 0; i < 48000; ++i) {
        lane.process();
        if (lane.wrapped()) break;
        const int slot = ModLane::step_index(lane.phase(), 8);
        CHECK(lane.target() == doctest::Approx(pattern.pitch[slot]));
    }
}

TEST_CASE("FLOW LFO mode is untouched when the flag is off") {
    ModLane lane = make_flow_melody_lane(0xF10Eu);
    lane.set_flow_melody(false);
    // The legacy free lane fires exactly once per cycle, at the wrap.
    CHECK(fires_over_cycles(lane, 3) == 3);
}

TEST_CASE("FLOW melody: the phrase length is a constant, not STEPS") {
    // A deck whose STEPS says 3 still gets an 8-slot FLOW phrase. STEPS means
    // different things on the two hosts in FLOW -- Fireflow spends STEPS == 0
    // on the mode switch itself, which set_step clamps to 1, while Glow pushes
    // 2..16 in both modes -- so the free mode owns its own length and both
    // hosts produce the same phrase from the same terrain.
    ModLane lane = make_flow_melody_lane(0xF10Eu, 1.f, /*steps=*/3);
    CHECK(fires_over_cycles(lane, 2) == 16);
}

TEST_CASE("STEP is unaffected by the phrase-length constant") {
    // The same lane in STEP still follows STEPS exactly. _effective_length()
    // must not leak into the stepped world.
    ModLane lane;
    lane.set_melodic(true);
    lane.set_step(true, 3);
    lane.set_form(Principle::Hierarchical);
    lane.set_song(SongMode::AAAB);
    lane.init(48000.f, 0xF10Eu);
    lane.set_rate_hz(1.f);
    lane.set_density(1.f);
    lane.set_variation(0.f);
    CHECK(lane.steps() == 3);
    // A STEP lane's clock_scale is 8/steps, so one cycle is still one phrase:
    // three slot entries, three fires at full density.
    CHECK(fires_over_cycles(lane, 2) == 6);
}

TEST_CASE("FLOW melody: DENSITY selects how many notes the drone uses") {
    SUBCASE("k == 1 is one standing note") {
        ModLane lane = make_flow_melody_lane(0xF10Eu);
        lane.set_density(0.f);           // lround(0 * L) == 0, clamped to 1
        // Exactly one fire per cycle, and the value never moves between them.
        drive_to_wrap(lane);
        const float held = lane.target();
        int fires = 0, wraps = 0;
        for (int i = 0; i < kDriveGuard && wraps < 3; ++i) {
            lane.process();
            if (lane.fired()) ++fires;
            if (lane.wrapped()) ++wraps;
            CHECK(lane.target() == doctest::Approx(held));
        }
        CHECK(fires == 3);
        // The lane really ran -- constancy alone would also pass on a stalled
        // phase accumulator, which is the vacuity the PACE spec recorded.
        CHECK(lane.wrap_count_for_test() >= 3u);
    }

    SUBCASE("k == L is a note per slot") {
        ModLane lane = make_flow_melody_lane(0xF10Eu);
        lane.set_density(1.f);
        CHECK(fires_over_cycles(lane, 2) == 16);
    }

    SUBCASE("a closed slot holds the previous note") {
        ModLane lane = make_flow_melody_lane(0xF10Eu);
        lane.set_density(0.5f);          // k == 4 of 8
        drive_to_wrap(lane);
        float last_fired = lane.target();
        int fires = 0;
        for (int i = 0; i < 48000; ++i) {
            lane.process();
            if (lane.fired()) { last_fired = lane.target(); ++fires; }
            // Between fires the target must equal the last fired value, never
            // a fresh one: the skipped slots HOLD, they do not rest.
            CHECK(lane.target() == doctest::Approx(last_fired));
            // Slot 0 (the unmaskable anchor, see _groove_k) always fires on the
            // same sample its entry wraps the cycle -- checking wrapped() before
            // fired() (as the original draft did) would silently drop that one
            // fire from the tally below. Break AFTER the fired-check, matching
            // the "k == 1" subcase above, so this closes exactly one full cycle.
            if (lane.wrapped()) break;
        }
        CHECK(fires == 4);
    }
}

TEST_CASE("FLOW melody: the gate is value-dependent, not merely executed") {
    // Two lanes identical but for DENSITY must emit different sequences. A
    // counter proving the branch ran would be shape 4 in the vacuous-gates
    // taxonomy -- it asserts the code executed, not that it did anything.
    ModLane sparse = make_flow_melody_lane(0xF10Eu);
    ModLane dense  = make_flow_melody_lane(0xF10Eu);
    sparse.set_density(0.f);
    dense.set_density(1.f);
    bool differed = false;
    for (int i = 0; i < 48000; ++i) {
        sparse.process();
        dense.process();
        if (sparse.target() != dense.target()) differed = true;
    }
    CHECK(differed);
}

#include <cstring>

TEST_CASE("FLOW melody: SONG advances one phrase per cycle") {
    ModLane lane = make_flow_melody_lane(0xF10Eu);
    drive_to_wrap(lane);
    const uint32_t start = lane.song_position();
    int wraps = 0;
    for (int i = 0; i < kDriveGuard && wraps < 3; ++i) {
        lane.process();
        if (lane.wrapped()) ++wraps;
    }
    CHECK(lane.song_position() == start + 3u);
}

TEST_CASE("FLOW melody: a FORM change reaches the phrase") {
    ModLane lane = make_flow_melody_lane(0xF10Eu);
    drive_to_wrap(lane);
    MelodyPattern before = lane.pattern_for_test(0);

    lane.set_form(Principle::CallResponse);
    int wraps = 0;
    for (int i = 0; i < kDriveGuard && wraps < 1; ++i) {
        lane.process();
        if (lane.wrapped()) ++wraps;
    }
    CHECK(lane.form() == Principle::CallResponse);
    CHECK(std::memcmp(&before, &lane.pattern_for_test(0), sizeof(before)) != 0);
}

TEST_CASE("FLOW melody: VARIATION moves the standing note two ways") {
    // At k == 1 the open slot is rank 0. VARIATION walks its pitch
    // (_mutate_slot) AND swaps groove ranks (mutate_pattern_groove), so the
    // open slot itself migrates and starts reading a different pitch[] entry.
    // Both are wanted; a gate on the pitch walk alone would miss half of it.
    SUBCASE("VARIATION 0 is loop-stable") {
        ModLane lane = make_flow_melody_lane(0xF10Eu);
        lane.set_density(0.f);
        lane.set_variation(0.f);
        drive_to_wrap(lane);
        const MelodyPattern before = lane.pattern_for_test(lane.active_pattern());
        int wraps = 0;
        for (int i = 0; i < kDriveGuard && wraps < 4; ++i) {
            lane.process();
            if (lane.wrapped()) ++wraps;
        }
        const MelodyPattern after = lane.pattern_for_test(lane.active_pattern());
        CHECK(std::memcmp(&before, &after, sizeof(before)) == 0);
    }

    SUBCASE("VARIATION 1 moves both pitch and groove") {
        ModLane lane = make_flow_melody_lane(0xF10Eu);
        lane.set_density(0.f);
        lane.set_variation(1.f);
        drive_to_wrap(lane);
        const MelodyPattern before = lane.pattern_for_test(lane.active_pattern());
        int wraps = 0;
        for (int i = 0; i < kDriveGuard && wraps < 8; ++i) {
            lane.process();
            if (lane.wrapped()) ++wraps;
        }
        const MelodyPattern after = lane.pattern_for_test(lane.active_pattern());
        bool pitch_moved = false, groove_moved = false;
        for (int s = 0; s < 32; ++s) {
            if (before.pitch[s] != after.pitch[s]) pitch_moved = true;
            if (before.pattern_groove.rank_of_slot[s] !=
                after.pattern_groove.rank_of_slot[s]) groove_moved = true;
        }
        CHECK(pitch_moved);
        CHECK(groove_moved);
    }
}

TEST_CASE("FLOW melody: pending work applies before the first slot") {
    // A lane whose STEPS disagrees with kFlowPhraseSlots has its phrase
    // generated at the wrong length by init(); set_flow_melody raises
    // length_pending and _apply_preroll_work must consume it on the first
    // process() call, while _cur_step is still -1. Otherwise the phrase would
    // stay wrong until the next wrap -- up to 50 s at kRateFreeMin.
    ModLane lane = make_flow_melody_lane(0xF10Eu, 1.f, /*steps=*/3);
    lane.process();
    CHECK(lane.pattern_for_test(lane.active_pattern()).pattern_groove.len == 8);
}

// The original single-case version of this test tried to gate two hazards
// through the SAME landing slot's gate decision: the first half needs that
// slot open (so the boundary fires), the second half is only a real gate on
// _frozen if that slot is closed (so a stale freeze would carry over). No
// DENSITY value makes both halves real at once, so the case is split in two
// -- each with its own density and its own single job.

TEST_CASE("FLOW melody: mode entry fires the first sample, not a stale index") {
    // A _cur_step left from the other mode means no boundary fires until the
    // slot index happens to differ, so the phrase's first note can be skipped
    // or arrive a slot late. set_step clears _cur_step to -1 on any mode
    // change so the very next sample re-evaluates the boundary.
    ModLane lane = make_flow_melody_lane(0xF10Eu);
    lane.set_density(0.5f);           // k == 4 of 8: landing slot 3 (rank 2) is open
    drive_to_wrap(lane);
    for (int i = 0; i < 20000; ++i) lane.process();   // land mid-phrase, slot 3

    lane.set_step(true, 8);
    // The first sample in the new mode must fire, not wait for an index change.
    lane.process();
    CHECK(lane.fired());
}

TEST_CASE("FLOW melody: mode entry clears a stale freeze before the LFO resumes") {
    // A _frozen left true from a CLOSED slot follows the lane into the FLOW
    // LFO path, whose branch is `if (!_frozen) _target = _compute_raw()` --
    // the "LFO" would then hold a constant for up to a full cycle.
    ModLane lane = make_flow_melody_lane(0xF10Eu);
    lane.set_density(0.25f);          // k == 2 of 8: landing slot 3 (rank 2) is closed
    drive_to_wrap(lane);
    for (int i = 0; i < 20000; ++i) lane.process();   // land mid-phrase, slot 3, held
    REQUIRE(lane.frozen());           // precondition: the hazard this case gates

    lane.set_flow_melody(false);
    // Back on the LFO path the lane must move again immediately.
    const float first = lane.target();
    for (int i = 0; i < 200; ++i) lane.process();
    CHECK(lane.target() != doctest::Approx(first));
}

TEST_CASE("FLOW melody: RST clears a stale freeze immediately, "
          "before the next process()/tick() call") {
    // Originally titled "...for the tick() path": process() re-evaluated the
    // boundary on its very next call after RST (_cur_step == -1 forces slot
    // 0, always open, so _frozen recomputes to false on its own -- see the
    // mode-entry case above), and tick() did not, so driving through
    // reset()+tick() and checking the target moved was the one place
    // reset()'s own `_frozen = false;` line was observable.
    //
    // Task 11 (2026-08-13, FLOW melody engine plan) gave tick() the same
    // immediate pending-mismatch re-check process() already had, so BOTH
    // paths now force-evaluate slot 0 -- and recompute _frozen fresh -- on
    // their very next call after reset(). Re-running this case with
    // reset()'s clear removed (Task 11's RED re-proof) still passed: the
    // gate had gone vacuous. Re-homed here to check the ONE place reset()'s
    // line is still observable -- frozen() queried directly between reset()
    // and the next process()/tick() call, before either path's own
    // recompute has run. See reset()'s comment.
    ModLane lane = make_flow_melody_lane(0xF10Eu);
    lane.set_density(0.5f);           // k == 4 of 8: opens are slots 0, 3, 5, 7
    drive_to_wrap(lane);
    for (int i = 0; i < 26000; ++i) lane.process();   // land in slot 4's window, closed
    REQUIRE(lane.frozen());           // precondition: the hazard this case gates

    lane.reset();
    CHECK_FALSE(lane.frozen());
}

TEST_CASE("FLOW melody: RST restarts the phrase at slot 0") {
    ModLane lane = make_flow_melody_lane(0xF10Eu);
    drive_to_wrap(lane);
    for (int i = 0; i < 20000; ++i) lane.process();

    lane.reset();
    lane.process();
    CHECK(lane.fired());
    CHECK(lane.target() ==
          doctest::Approx(lane.pattern_for_test(lane.active_pattern()).pitch[0]));
}

TEST_CASE("FLOW melody: the note rate has a floor") {
    // A non-drone archetype reaches P_RATE_A {.55,.9} in FLOW, about 14 Hz.
    // At k == L == 8 that is over 100 fires a second, where Part's 5 ms gate
    // never expires so the envelope never re-attacks, and _chord.build()'s lay
    // search runs per note.
    ModLane lane = make_flow_melody_lane(0xF10Eu, /*hz=*/14.f);
    lane.set_density(1.f);
    int fires = 0;
    for (int i = 0; i < 48000; ++i) {       // one second
        lane.process();
        if (lane.fired()) ++fires;
    }
    const int ceiling = static_cast<int>(48000.f / (0.060f * 48000.f)) + 1;
    CHECK(fires <= ceiling);                // ~16/s, not ~112/s
    CHECK(fires > 0);                       // it decimates, it does not stop
}

TEST_CASE("FLOW melody: the phrase rate has a floor") {
    // The note floor bounds fires, not wraps. Without a second floor,
    // _advance_song() would run once per cycle at 14 Hz -- fourteen phrases a
    // second, and a pending FORM would drive generate_phrase at that rate.
    ModLane lane = make_flow_melody_lane(0xF10Eu, /*hz=*/14.f);
    drive_to_wrap(lane);
    const uint32_t start = lane.song_position();
    for (int i = 0; i < 48000; ++i) lane.process();
    const uint32_t ceiling =
        static_cast<uint32_t>(48000.f / (8 * 0.060f * 48000.f)) + 1u;
    CHECK(lane.song_position() - start <= ceiling);   // ~2/s, not 14/s
}

TEST_CASE("FLOW melody: the floor never swallows the first note") {
    // Primed, not zeroed. Left at 0 the counters would suppress the very first
    // note at boot, after RST, and on the first slot after mode entry.
    ModLane lane = make_flow_melody_lane(0xF10Eu);
    lane.process();
    CHECK(lane.fired());

    drive_to_wrap(lane);
    for (int i = 0; i < 20000; ++i) lane.process();
    lane.reset();
    lane.process();
    CHECK(lane.fired());
}

TEST_CASE("FLOW melody: a note arrives within its own slot at full SMOOTH") {
    // With _range at its default 1.0, apply_range is the identity
    // (range.h:18, lerpf(uni, v, 1) == v), so process()'s return value is the
    // post-slew value and can be compared against target() directly.
    ModLane lane = make_flow_melody_lane(0xF10Eu);
    lane.set_density(1.f);
    lane.set_smooth(1.f);              // ~0.5 s unclamped; a slot is 125 ms
    drive_to_wrap(lane);

    // Find a boundary where the note actually moves, then give it one slot.
    float out = 0.f;
    bool checked = false;
    for (int i = 0; i < 48000 * 3 && !checked; ++i) {
        out = lane.process();
        if (!lane.fired()) continue;
        const float goal = lane.target();
        const float gap  = goal - out;
        if (std::fabs(gap) < 0.05f) continue;      // too small to measure
        for (int s = 0; s < 6000; ++s) out = lane.process();   // one slot
        CHECK(std::fabs(goal - out) <= 0.10f * std::fabs(gap));
        checked = true;
    }
    CHECK(checked);
}

TEST_CASE("STEP's slew is unchanged by the melody clamp") {
    ModLane step_lane;
    step_lane.set_melodic(true);
    step_lane.set_step(true, 8);
    step_lane.init(48000.f, 0xF10Eu);
    step_lane.set_rate_hz(1.f);
    step_lane.set_smooth(1.f);
    // A STEP lane at SMOOTH 1 keeps the long glide: after 6000 samples it must
    // still be well short of its target, which is what the clamp must not do
    // to it.
    //
    // The bound is 0.02, not the loose 1.0 a first draft used: for this seed
    // the unclamped tau (~0.5 s) moves the output only ~0.006 over this
    // window, but a guard that leaks the melody clamp into STEP (e.g.
    // checking `_melodic` instead of `_flow_melody_on()`, so a STEP lane gets
    // clamped too) measures ~0.065 here -- an order of magnitude more. 1.0
    // would pass either way and never catch that regression; 0.02 sits
    // between the two with margin on both sides.
    float out = 0.f;
    for (int i = 0; i < 200; ++i) out = step_lane.process();
    const float early = out;
    for (int i = 0; i < 6000; ++i) out = step_lane.process();
    CHECK(std::fabs(out - early) < 0.02f);
}
