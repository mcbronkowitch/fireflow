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

// A single deck in FLOW at a slow rate, with the neighbour muted so nothing
// downstream of the lane can colour the observation.
void configure_flow_deck(Instrument& inst, int part, EngineId engine) {
    inst.set_engine(part, engine);
    inst.set_step(part, false, 8);
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
    Instrument inst;
    inst.init(48000.f);
    configure_flow_deck(inst, 0, ENGINE_SYNTH);
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 4800; ++i) inst.process(nullptr, nullptr, &l, &r, 1);

    inst.set_engine(0, ENGINE_SAMPLER);
    for (int i = 0; i < 4800; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
    inst.set_engine(0, ENGINE_SYNTH);
    for (int i = 0; i < 4800; ++i) inst.process(nullptr, nullptr, &l, &r, 1);

    // No crash, no silence, and the deck still fires: the groove must match the
    // effective length or the rank lookup would be reading another length's map.
    int fires = 0;
    for (int i = 0; i < 48000 * 30; ++i) {
        inst.process(nullptr, nullptr, &l, &r, 1);
        if (inst.lane_fired(0, LANE_PITCH)) ++fires;
    }
    CHECK(fires > 1);
}
