#include <cmath>
#include <vector>

#include <doctest/doctest.h>

#include "synth/synth_engine.h"
#include "synth_engine_contract.h"
#include "synth_voicet_contract.h"
#include "instrument.h"

using namespace spky;

namespace {

void feed(SynthEngine& e, float pitch) {
    float t[LANE_COUNT] = { 0.f, 1.f, pitch, 0.f, 1.f };
    e.set_targets(t, 0.5f);
}

void fresh(SynthEngine& e) {
    e.set_seed(99);
    e.init(48000.f);
    e.set_sub(0.f);
    e.set_detune(0.f);
    e.set_cycle(1.f);
    feed(e, 0.5f);
}

std::vector<float> render_l(SynthEngine& e, int n) {
    std::vector<float> out(n);
    for (float& s : out) {
        float l = 0.f, r = 0.f;
        e.process(l, r);
        s = l;
    }
    return out;
}

int crossings(const std::vector<float>& v, size_t from = 0) {
    int n = 0;
    for (size_t i = from + 1; i < v.size(); ++i)
        if (v[i - 1] <= 0.f && v[i] > 0.f) ++n;
    return n;
}

} // namespace

TEST_CASE("synth: pitch contract - trigger(p) sounds at 110*8^p Hz, latched in STEP") {
    SynthEngine e;
    fresh(e);
    e.trigger(0.5f);
    auto v1 = render_l(e, 48000);
    const int n1 = crossings(v1, 4800);
    CHECK(n1 >= 275);
    CHECK(n1 <= 285);

    SynthEngine e2;
    fresh(e2);
    e2.trigger(0.5f);
    feed(e2, 0.9f);
    auto v2 = render_l(e2, 48000);
    CHECK(crossings(v2, 4800) == n1);
}

TEST_CASE("synth: FLOW drone pitch tracks") {
    SynthEngine e;
    fresh(e);
    feed(e, 0.25f);
    e.set_flow(true);
    auto v = render_l(e, 48000);
    const int n = crossings(v, 4800);
    CHECK(n >= 160);
    CHECK(n <= 172);

    feed(e, 0.75f);
    render_l(e, 9600);
    auto v2 = render_l(e, 48000);
    const int n2 = crossings(v2);
    CHECK(n2 >= 515);
    CHECK(n2 <= 532);
}

TEST_CASE("synth engine satisfies the shared part-engine contract") {
    contract_round_robin_and_steal<SynthEngine>();
    contract_flow_drone_and_surface<SynthEngine>();
    contract_chord_surface_and_hold<SynthEngine>();
    contract_deterministic_seed<SynthEngine>();
    contract_detune_is_independent_of_source<SynthEngine>();
}

// The half of the old single contract that is about VoiceT's AD envelope and
// its four voices rather than about SynthEngineT. Split out in Task 8 (spec
// 2026-07-26 body-resonator) so BODY could run the machine contract without
// any of these being softened -- see the header for what qualifies.
TEST_CASE("synth engine satisfies the AD-envelope voice contract") {
    voicet_steal_retriggers_from_level<SynthEngine>();
    voicet_cycle_scaled_ad_envelope<SynthEngine>();
    voicet_flow_plateau_and_handover<SynthEngine>();
}

TEST_CASE("detune reaches 105 cents at full for the synth engines") {
    SynthEngine e;
    fresh(e);
    // set_detune() only writes the raw _detune_spread_ct; applied_detune_ct()
    // reads the VOICE's cached copy, which _update_control() refreshes once
    // per kCtrlInterval samples (see contract_detune_is_independent_of_source
    // in synth_engine_contract.h). A render past one control tick is required
    // for the new ceiling to actually reach the voice.
    e.set_detune(1.f);
    render_l(e, SynthEngine::kCtrlInterval + 1);
    CHECK(e.applied_detune_ct() == doctest::Approx(105.f).epsilon(0.001));
    e.set_detune(0.f);
    render_l(e, SynthEngine::kCtrlInterval + 1);
    CHECK(e.applied_detune_ct() == doctest::Approx(0.f).epsilon(0.001));
}

namespace {
void render_instrument(Instrument& inst, float* l, float* r, int n) {
    float in[64] = {};
    for (int i = 0; i < n; i += 64) inst.process(in, in, l + i, r + i, 64);
}
}  // namespace

// EDGE's output high-pass (spec 2026-08-19 voice-knobs-dpth-edge, 4.1/4.3):
// the whole reason it is a TRIM rather than an absolute corner is that five
// engines' factory sound has to survive the knob's arrival untouched.
// Silence is enough for this half of the claim (see
// tests/test_voice_edge_broadcast.cpp's own note on the NEUTRAL half vs. the
// REACH half): a bit-exact comparison of two silences still catches a HP that
// runs at t == 0 instead of bypassing, because engine/util/onepole_hp.h's own
// warning is that its bottom rail is not a bit-exact bypass in float32.
TEST_CASE("synth: EDGE at 0 is bit-identical to no EDGE at all") {
    static float a[9600], b[9600], c[9600], d[9600];
    Instrument i1, i2;
    i1.init(48000.f);
    i2.init(48000.f);
    // i1 never calls set_voice_edge; i2 calls it with 0.
    i2.set_voice_edge(PART_A, 0.f);
    render_instrument(i1, a, b, 9600);
    render_instrument(i2, c, d, 9600);
    for (int n = 0; n < 9600; ++n) {
        CHECK(a[n] == c[n]);
        CHECK(b[n] == d[n]);
    }
}
