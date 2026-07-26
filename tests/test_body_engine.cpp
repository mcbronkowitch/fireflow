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

#include <cmath>
#include <vector>

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

// --- COLOR is the material's CHARACTER, DETUNE is its AMOUNT ---------------
//
// Spec §7, in as many words: "COLOR chooses WHICH WAY the partials are
// stretched, DETUNE chooses HOW FAR ... so at DETUNE = 0 a BODY deck is
// harmonic and COLOR is inaudible". Both halves of that are Bastian's
// contract, not this implementation's taste, so both are pinned here. The
// VALUE the mapping gives any particular chord is tuning material and is
// asserted nowhere.
namespace {

// Same root, different quality: only what COLOR reads can differ.
// Slot ladder (engine/pitch/chord.h): root, fifth an octave down, third.
constexpr float kSemi = 1.f / 36.f;
constexpr float kRoot = 0.4f;
const float kMajorTriad[3] = { kRoot, kRoot - 5.f * kSemi, kRoot + 4.f * kSemi };
const float kMinorTriad[3] = { kRoot, kRoot - 5.f * kSemi, kRoot + 3.f * kSemi };

std::vector<float> render_chord(const float* chord, float detune, int n_samples) {
    BodyEngine e;
    e.set_seed(11u);
    e.init(48000.f);
    e.set_detune(detune);
    float t[LANE_COUNT] = { 0.6f, 0.7f, 0.45f, 0.f, 1.f };
    e.set_targets(t, 0.f);
    e.set_chord(chord, 3);
    e.trigger_chord(chord, 3);
    std::vector<float> out(n_samples);
    for (int i = 0; i < n_samples; ++i) {
        e.set_chord(chord, 3);          // Part re-pushes the surface per sample
        float l = 0.f, r = 0.f;
        e.process(l, r);
        out[i] = l + r;
    }
    return out;
}

} // namespace

TEST_CASE("body engine: DETUNE = 0 makes COLOR inaudible") {
    // Bit-identical, not a tolerance: the promise is that the amount MULTIPLIES
    // the character, so at amount 0 there is nothing left of it to hear.
    const auto maj = render_chord(kMajorTriad, 0.f, 12000);
    const auto min = render_chord(kMinorTriad, 0.f, 12000);
    REQUIRE(maj.size() == min.size());
    int differing = 0;
    for (size_t i = 0; i < maj.size(); ++i) if (maj[i] != min[i]) ++differing;
    CHECK(differing == 0);

    // ...and the deck is not simply silent, which would satisfy the above for
    // the wrong reason.
    float peak = 0.f;
    for (float s : maj) peak = std::max(peak, std::fabs(s));
    CHECK(peak > 0.001f);
}

TEST_CASE("body engine: DETUNE > 0 makes COLOR audible") {
    const auto maj = render_chord(kMajorTriad, 1.f, 12000);
    const auto min = render_chord(kMinorTriad, 1.f, 12000);
    REQUIRE(maj.size() == min.size());
    bool differs = false;
    for (size_t i = 0; i < maj.size(); ++i) if (maj[i] != min[i]) differs = true;
    CHECK(differs);
}

TEST_CASE("body engine: DETUNE reaches the bank with COLOR at minimum") {
    // The assertion whose absence let a dead knob through review. COLOR at
    // minimum is a one-note chord, whose character is exactly 0; at MATL = 1
    // the strings are mixed out entirely, so the mode bank is the ONLY thing
    // that can carry DETUNE. If the bank's stretch were nothing but
    // amount * character, this corner of the panel would do nothing at all --
    // which the fork's "every control must carry" rule does not allow.
    //
    // What is pinned is the property (DETUNE moves the bank), not the base
    // value that makes it true: kBaseStretch is tuning material.
    auto run = [](float detune) {
        BodyEngine e;
        e.set_seed(7u);
        e.init(48000.f);
        e.set_detune(detune);
        float t[LANE_COUNT] = { 1.f, 0.7f, 0.45f, 0.f, 1.f };   // MATL = 1
        e.set_targets(t, 0.f);
        e.trigger(0.35f);                                       // COLOR at 0
        std::vector<float> out(8192);
        for (int i = 0; i < 8192; ++i) {
            float l = 0.f, r = 0.f;
            e.process(l, r);
            out[i] = l + r;
        }
        return out;
    };
    const auto dry = run(0.f);
    const auto wide = run(1.f);
    bool differs = false;
    for (size_t i = 0; i < dry.size(); ++i) if (dry[i] != wide[i]) differs = true;
    CHECK(differs);

    // ...and the deck is audible, so the claim cannot be satisfied by silence.
    float peak = 0.f;
    for (float s : wide) peak = std::max(peak, std::fabs(s));
    CHECK(peak > 0.001f);
}

TEST_CASE("body engine: a chord struck between control ticks gets its material now") {
    // Triggers do not land on control-tick boundaries -- Part fires them from
    // the step clock. The material therefore has to reach the voice on the
    // TRIGGER path too, not only on the tick: otherwise the strike, which is
    // the loudest part of a struck body, rings with the previous chord's body
    // for up to kCtrlInterval samples.
    //
    // The window below is deliberately shorter than one control interval, and
    // nothing calls set_chord, so the tick's own derivation cannot rescue it.
    auto run = [](const float* chord) {
        BodyEngine e;
        e.set_seed(9u);
        e.init(48000.f);
        e.set_detune(1.f);
        float t[LANE_COUNT] = { 0.6f, 0.7f, 0.45f, 0.f, 1.f };
        e.set_targets(t, 0.f);
        std::vector<float> out(64);
        float l = 0.f, r = 0.f;
        for (int i = 0; i < 10; ++i) e.process(l, r);   // land mid-interval
        e.trigger_chord(chord, 3);
        for (int i = 0; i < 64; ++i) { e.process(l, r); out[i] = l + r; }
        return out;
    };
    const auto maj = run(kMajorTriad);
    const auto min = run(kMinorTriad);
    bool differs = false;
    for (size_t i = 0; i < maj.size(); ++i) if (maj[i] != min[i]) differs = true;
    CHECK(differs);
}

TEST_CASE("body engine: COLOR reaches the material without a retrigger") {
    // A FLOW deck holds one sustained voice and Part re-pushes the surface
    // every sample, so a live COLOR sweep changes the chord's quality WITHOUT
    // firing a new note. The material has to follow on the control tick; if
    // only the trigger path pushed it, the deck would keep the old body until
    // the next step.
    auto run = [](bool switch_to_minor) {
        BodyEngine e;
        e.set_seed(5u);
        e.init(48000.f);
        e.set_detune(1.f);
        float t[LANE_COUNT] = { 0.6f, 0.7f, 0.45f, 0.f, 1.f };
        e.set_targets(t, 0.f);
        e.set_flow(true);
        std::vector<float> out(16000);
        for (int i = 0; i < 16000; ++i) {
            const bool minor = switch_to_minor && i >= 4000;
            e.set_chord(minor ? kMinorTriad : kMajorTriad, 3);
            float l = 0.f, r = 0.f;
            e.process(l, r);
            out[i] = l + r;
        }
        return out;
    };
    const auto held = run(false);
    const auto swept = run(true);
    // Identical before the sweep -- nothing else may differ between the runs.
    for (int i = 0; i < 4000; ++i) REQUIRE(held[i] == swept[i]);
    bool differs = false;
    for (size_t i = 4000; i < held.size(); ++i) if (held[i] != swept[i]) differs = true;
    CHECK(differs);
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
