// tests/test_flow_melody_wiring.cpp
//
// Part decides whether a PITCH lane is a note at all. SYNTH, WAVE, BODY, ZAP
// and TEST_TONE get the FLOW melody engine; SAMPLER and BBD keep the
// continuous LFO, because on those decks the PITCH lane is a read position and
// a clock bend rather than a note.
#include <doctest/doctest.h>
#include "instrument.h"

using namespace spky;

namespace {

// A STEPS the melody engine's own phrase length cannot coincide with. The
// phrase is kFlowPhraseSlots == 8 slots whatever STEPS says, so a fixture at
// STEPS 8 makes "pattern_groove.len == _effective_length()" true by
// construction and cannot fail on it. Glow pushes 2..16 in FLOW as well as in
// STEP (super_modulator.cpp), so 12 is a value the hosts really produce.
constexpr int kOffGridSteps = 12;

// A single deck in FLOW at a slow rate, with the neighbour muted so nothing
// downstream of the lane can colour the observation.
void configure_flow_deck(Instrument& inst, int part, EngineId engine,
                         int steps = 8) {
    inst.set_engine(part, engine);
    inst.set_step(part, false, steps);
    inst.set_rate(part, 0.f);          // free_hz floor, ~0.02 Hz
    inst.set_density(part, 1.f);
    inst.set_variation(part, 0.f);
    inst.set_smooth(part, 0.f);
}

} // namespace

TEST_CASE("a SYNTH deck in FLOW runs the melody engine") {
    Instrument inst;
    inst.init(48000.f);
    configure_flow_deck(inst, 0, ENGINE_SYNTH);
    inst.set_density(0, 1.f);

    // The pitch lane fires more than once per cycle -- the observable
    // difference between a slot sequencer and the old one-fire-per-wrap LFO.
    int fires = 0;
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 48000 * 30; ++i) {
        inst.process(nullptr, nullptr, &l, &r, 1);
        if (inst.lane_fired(0, LANE_PITCH)) ++fires;
    }
    CHECK(fires > 1);
}

TEST_CASE("a SAMPLER deck in FLOW keeps the continuous LFO") {
    Instrument inst;
    inst.init(48000.f);
    configure_flow_deck(inst, 0, ENGINE_SAMPLER);

    // The pitch target must keep moving every sample, not hold between slots.
    float l = 0.f, r = 0.f;
    inst.process(nullptr, nullptr, &l, &r, 1);
    const float a = inst.pitch_cv(0);
    for (int i = 0; i < 2000; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    const float b = inst.pitch_cv(0);
    CHECK(a != doctest::Approx(b));
}

TEST_CASE("a BBD deck in FLOW keeps the continuous LFO") {
    Instrument inst;
    inst.init(48000.f);
    configure_flow_deck(inst, 0, ENGINE_BBD);

    float l = 0.f, r = 0.f;
    inst.process(nullptr, nullptr, &l, &r, 1);
    const float a = inst.pitch_cv(0);
    for (int i = 0; i < 2000; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    const float b = inst.pitch_cv(0);
    CHECK(a != doctest::Approx(b));
}

TEST_CASE("an engine swap in FLOW leaves the phrase length consistent") {
    // SYNTH -> Sampler -> SYNTH. The check is at the first wrap AFTER the swap
    // back, not "at every point": while the deck is a Sampler the lane is on
    // the LFO path where the melody state is legitimately not maintained.
    //
    // Asserted as the invariant itself -- pattern_groove.len against
    // _effective_length() -- not through a fire count. A fire count only says
    // the flag push happened; the length claim this case is named for
    // (spec §10 item 15) needs the two lengths compared. The deck runs at
    // STEPS 12 for the same reason (spec §10 item 14, "including where the
    // host pushed a STEPS other than 8"): at STEPS 8 the two sides coincide by
    // construction and the assertion could not fail.
    Instrument inst;
    inst.init(48000.f);
    // Start in STEP so the phrase really is generated at 12 first -- entering
    // FLOW then has to re-length it, which is the work this case watches
    // survive an engine swap.
    inst.set_engine(0, ENGINE_SYNTH);
    inst.set_step(0, true, kOffGridSteps);
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 4800; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    REQUIRE(inst.lane_pattern_groove_len_for_test(0, LANE_PITCH) ==
            kOffGridSteps);

    configure_flow_deck(inst, 0, ENGINE_SYNTH, kOffGridSteps);
    for (int i = 0; i < 4800; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    REQUIRE(inst.lane_effective_length_for_test(0, LANE_PITCH) != kOffGridSteps);

    inst.set_engine(0, ENGINE_SAMPLER);
    for (int i = 0; i < 4800; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    inst.set_engine(0, ENGINE_SYNTH);
    for (int i = 0; i < 4800; ++i) inst.process(nullptr, nullptr, &l, &r, 1);

    CHECK(inst.lane_pattern_groove_len_for_test(0, LANE_PITCH) ==
          inst.lane_effective_length_for_test(0, LANE_PITCH));

    // And it still runs: a length that agrees on a dead lane would be a
    // hollow pass.
    int fires = 0;
    for (int i = 0; i < 48000 * 30; ++i) {
        inst.process(nullptr, nullptr, &l, &r, 1);
        if (inst.lane_fired(0, LANE_PITCH)) ++fires;
    }
    CHECK(fires > 1);
}

TEST_CASE("a deck that leaves FLOW melody for STEP re-lengths its phrase") {
    // The reachable gesture the length-delta check used to miss: a note deck
    // in FLOW (phrase at kFlowPhraseSlots) is swapped to SAMPLER, which drops
    // the lane onto the LFO path and changes what _effective_length() returns,
    // and STEP is then entered at the SAME STEPS the deck already had. STEPS
    // never moves, so set_step's before/after comparison sees no change --
    // only set_flow_melody's own check can flag the phrase for regeneration.
    //
    // Fireflow cannot reach this (it spends STEPS 0 on the mode switch, so the
    // delta always fires); Glow and the render host push a real STEPS in both
    // modes and can.
    Instrument inst;
    inst.init(48000.f);
    configure_flow_deck(inst, 0, ENGINE_SYNTH, kOffGridSteps);
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 4800; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    REQUIRE(inst.lane_pattern_groove_len_for_test(0, LANE_PITCH) !=
            kOffGridSteps);

    inst.set_engine(0, ENGINE_SAMPLER);
    for (int i = 0; i < 4800; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    inst.set_step(0, true, kOffGridSteps);          // same STEPS as before
    for (int i = 0; i < 4800; ++i) inst.process(nullptr, nullptr, &l, &r, 1);

    CHECK(inst.lane_pattern_groove_len_for_test(0, LANE_PITCH) ==
          inst.lane_effective_length_for_test(0, LANE_PITCH));
}
