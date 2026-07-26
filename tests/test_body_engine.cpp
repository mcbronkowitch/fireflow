// BODY as a part engine (spec 2026-07-26 body-resonator, Task 8).
//
// BodyEngine is SynthEngineT<BodyVoice>, so allocation, FLOW, CHOKE, the
// chord surface, steal order and determinism all come from the same machine
// SYNTH and WAVE use -- and it runs the shared contract to prove it. It does
// NOT run synth_voicet_contract.h: BodyVoice::env_value() is an energy
// follower rather than an AD envelope, and it has one voice, not four. The
// header comments on both contract files say which claim belongs where.
//
// This file replaces tests/test_synth_engine_body_voice_proof.cpp, which
// existed only to show SynthEngineT<BodyVoice> compiled and ran before a
// production alias existed.
#include "doctest/doctest.h"

#include "synth/synth_engine.h"
#include "synth_engine_contract.h"

using namespace spky;

TEST_CASE("body engine satisfies the shared part-engine contract") {
    contract_round_robin_and_steal<BodyEngine>();
    contract_flow_drone_and_surface<BodyEngine>();
    contract_chord_surface_and_hold<BodyEngine>();
    contract_deterministic_seed<BodyEngine>();
    contract_detune_is_independent_of_source<BodyEngine>();
}

TEST_CASE("body engine is one voice, and the surface never asks for more") {
    static_assert(BodyEngine::kVoices == 1,
                  "kVoices must come from BodyVoice::kEngineVoices");

    BodyEngine e;
    e.set_seed(3u);
    e.init(48000.f);
    const float chord[4] = { 0.3f, 0.36f, 0.42f, 0.5f };
    e.set_flow(true);
    float peak_1 = 0.f;
    for (int i = 0; i < 48000; ++i) {
        e.set_chord(chord, 1);
        float l = 0.f, r = 0.f;
        e.process(l, r);
        peak_1 = std::max(peak_1, std::fabs(l));
    }
    CHECK(e.sustain_count() == 1);

    // A four-note surface must sound the SAME as a one-note surface here: the
    // deck holds one note either way, so it must not also be attenuated by an
    // equal-power compensation for three notes it cannot play. Before Task 8's
    // clamp this ran ~6 dB quieter and re-struck the resonator every control
    // tick (see tests/test_synth_engine_voice_count.cpp).
    BodyEngine f;
    f.set_seed(3u);
    f.init(48000.f);
    f.set_flow(true);
    float peak_4 = 0.f;
    for (int i = 0; i < 48000; ++i) {
        f.set_chord(chord, 4);
        float l = 0.f, r = 0.f;
        f.process(l, r);
        peak_4 = std::max(peak_4, std::fabs(l));
    }
    CHECK(f.sustain_count() == 1);
    CHECK(peak_4 == doctest::Approx(peak_1));
}

TEST_CASE("body engine SOURCE moves the material, not an oscillator shape") {
    BodyEngine str, bell;
    str.init(48000.f);
    bell.init(48000.f);
    str.set_detune(0.f);
    bell.set_detune(0.f);
    float ts[LANE_COUNT] = {0.f, 1.f, 0.45f, 0.f, 1.f};
    float tb[LANE_COUNT] = {1.f, 1.f, 0.45f, 0.f, 1.f};
    str.set_targets(ts, 0.5f);
    bell.set_targets(tb, 0.5f);
    str.trigger(0.35f);
    bell.trigger(0.35f);
    bool differs = false;
    for (int i = 0; i < 4096; ++i) {
        float sl, sr, bl, br;
        str.process(sl, sr);
        bell.process(bl, br);
        if (sl != bl || sr != br) differs = true;
    }
    CHECK(differs);
}
