#include <doctest/doctest.h>
#include <cmath>
#include <vector>
#include "synth/synth_engine.h"
#include "body/body_voice.h"
#include "instrument.h"
#include "render/scenario.h"
using namespace spky;

// SynthEngine-Ebene: Kennlinien-Tests brauchen gepinnte Lane-Werte (auf
// Instrument-Ebene wandern die Lanes generativ). Muster: test_synth_engine.cpp.

static void feed(SynthEngine& e, float pitch, float filter = 1.f, float level = 1.f) {
    float t[LANE_COUNT] = { 0.f, filter, pitch, 0.f, level };
    e.set_targets(t, 0.5f);
}

// Fresh engine in "measurement" trim: pure sine, no sub, no detune.
static void fresh(SynthEngine& e, uint32_t seed = 99) {
    e.set_seed(seed);
    e.init(48000.f);
    e.set_sub(0.f);
    e.set_detune(0.f);
    e.set_cycle(1.f);
    feed(e, 0.5f);
}

static std::vector<float> render_l(SynthEngine& e, int n) {
    std::vector<float> out(n);
    for (auto& s : out) {
        float l = 0.f, r = 0.f;
        e.process(l, r);
        s = l;
    }
    return out;
}

static float rms(const std::vector<float>& v, size_t from) {
    double acc = 0.0;
    for (size_t i = from; i < v.size(); ++i) acc += (double)v[i] * v[i];
    return std::sqrt((float)(acc / (double)(v.size() - from)));
}

TEST_CASE("filt: 0 is bit-identical on the engine") {
    SynthEngine a, b;
    fresh(a);
    fresh(b);
    b.set_filt(0.f);
    a.trigger(0.5f);
    b.trigger(0.5f);
    auto va = render_l(a, 48000);
    auto vb = render_l(b, 48000);
    for (int i = 0; i < 48000; ++i) REQUIRE(va[i] == vb[i]);
}

TEST_CASE("filt: full left is silent for any lane position") {
    for (float lane : { 0.f, 0.5f, 1.f }) {
        CAPTURE(lane);
        SynthEngine e;
        fresh(e);
        feed(e, 0.5f, lane);
        e.set_filt(-1.f);
        e.trigger(0.5f);
        auto v = render_l(e, 48000);
        // 10-ms-OnePole: nach 0,2 s ist der Rest < e^-20; -80 dBFS ist grosszuegig
        CHECK(rms(v, 9600) < 1e-4f);
    }
}

TEST_CASE("filt: full right pins the cutoff fully open") {
    SynthEngine a, b;                 // A: dunkle Lane + FILT +1 ...
    fresh(a);
    feed(a, 0.5f, 0.2f);
    a.set_filt(1.f);
    fresh(b);                          // ... B: Lane offen, FILT neutral
    feed(b, 0.5f, 1.f);
    b.set_filt(0.f);
    a.trigger(0.5f);
    b.trigger(0.5f);
    auto va = render_l(a, 48000);
    auto vb = render_l(b, 48000);
    for (int i = 0; i < 48000; ++i) REQUIRE(va[i] == vb[i]);   // beide: filter_hz(1)
}

TEST_CASE("filt: bites from the first movement (no dead zone)") {
    SynthEngine a, b;
    fresh(a);
    feed(a, 0.5f, 0.5f);              // Lane mittig
    fresh(b);
    feed(b, 0.5f, 0.5f);
    b.set_filt(-0.1f);                // kleine Auslenkung muss schon wirken
    a.trigger(0.5f);
    b.trigger(0.5f);
    auto va = render_l(a, 48000);
    auto vb = render_l(b, 48000);
    float maxdiff = 0.f;
    for (int i = 0; i < 48000; ++i)
        maxdiff = std::max(maxdiff, std::abs(va[i] - vb[i]));
    CHECK(maxdiff > 1e-3f);
}

// --- BODY --------------------------------------------------------------
// On VoiceT, FILTER drives a real lowpass, so the loudness comes for free.
// BodyVoice maps the same control to brightness -- a timbre parameter -- so
// its loudness has to be produced deliberately. These two pin that.

static double body_energy(float lane, float filt) {
    BodyEngine e;
    e.set_seed(99);
    e.init(48000.f);
    e.set_sub(0.f);
    e.set_detune(0.f);
    e.set_cycle(1.f);
    float t[LANE_COUNT] = { 0.f, lane, 0.5f, 0.f, 1.f };
    e.set_targets(t, 0.5f);
    e.set_filt(filt);
    e.trigger(0.5f);
    double acc = 0.0;
    for (int i = 0; i < 2 * 48000; ++i) {
        float l = 0.f, r = 0.f;
        e.process(l, r);
        acc += (double)l * l + (double)r * r;
    }
    return acc;
}

TEST_CASE("filt: BODY loses most of its loudness before the fade begins") {
    // At lane 0.5, FILT -0.4 puts n_raw at exactly 0, where the fade
    // (1 + n_raw/kFiltFadeRange) still equals exactly 1. Everything measured
    // here is therefore the engine's own response, with the fade uninvolved.
    //
    // Before BodyVoice had a loudness tilt this read -5.7 dB, so the fade was
    // left to erase the remaining 100 % inside 0.2 of knob travel -- the cliff
    // that made FILT below -0.5 unusable on this engine. SYNTH reaches
    // -28.6 dB at the same point.
    const double ref  = body_energy(0.5f, 0.f);
    const double dark = body_energy(0.5f, -0.4f);
    const float  drop = 10.f * std::log10((float)(dark / ref));
    CAPTURE(drop);
    CHECK(drop < -15.f);
}

TEST_CASE("filt: BODY's left half only ever gets quieter") {
    // The tilt must not introduce a bump: every step left is a step down, so
    // the control reads as one continuous move rather than a swell.
    double prev = body_energy(0.5f, 0.f);
    for (float filt = -0.1f; filt >= -0.51f; filt -= 0.1f) {
        CAPTURE(filt);
        const double e = body_energy(0.5f, filt);
        CHECK(e < prev);
        prev = e;
    }
}

TEST_CASE("filt: sweep through the whole range is click-free") {
    SynthEngine e;
    fresh(e);
    feed(e, 0.f, 0.5f);               // 110 Hz: kleines natuerliches Sample-Delta
    e.trigger(0.f);
    render_l(e, 4800);                // Attack ausklingen lassen
    float maxstep = 0.f, prev = 0.f;
    const int blocks = 1000;          // 2 s Sweep in Control-Block-Schritten
    for (int bi = 0; bi <= blocks; ++bi) {
        e.set_filt(-1.f + 2.f * bi / blocks);
        for (int i = 0; i < SynthEngine::kCtrlInterval; ++i) {
            float l = 0.f, r = 0.f;
            e.process(l, r);
            if (bi > 0 || i > 0) maxstep = std::max(maxstep, std::abs(l - prev));
            prev = l;
        }
    }
    CHECK(maxstep < 0.05f);           // Klicks waeren Spruenge >> Signal-Delta
}

// ---- Instrument-Ebene: Regression + Verkabelung (Muster: test_choke.cpp) ----

TEST_CASE("filt: 0 is bit-identical to an untouched instrument") {
    Instrument a, b;
    a.init(48000.f);
    b.init(48000.f);
    b.set_voice_filt(0, 0.f);
    b.set_voice_filt(1, 0.f);
    std::vector<float> al(1), ar(1), bl(1), br(1);
    for (int i = 0; i < 48000; ++i) {
        a.process(nullptr, nullptr, al.data(), ar.data(), 1);
        b.process(nullptr, nullptr, bl.data(), br.data(), 1);
        REQUIRE(al[0] == bl[0]);
        REQUIRE(ar[0] == br[0]);
    }
}

static float inst_rms(Instrument& inst, int samples, int skip) {
    std::vector<float> l(1), r(1);
    double acc = 0.0;
    for (int i = 0; i < samples; ++i) {
        inst.process(nullptr, nullptr, l.data(), r.data(), 1);
        if (i >= skip) acc += (double)l[0] * l[0] + (double)r[0] * r[0];
    }
    return std::sqrt((float)(acc / (double)(samples - skip)));
}

TEST_CASE("filt: per-part plumbing - one part fades, the other keeps playing") {
    Instrument inst;
    inst.init(48000.f);
    for (int p = 0; p < 2; ++p) {      // beide Decks sicher hoerbar machen
        inst.set_rate(p, p == 0 ? 0.8f : 0.9f);
        inst.set_density(p, 1.f);
    }
    CHECK(inst_rms(inst, 96000, 0) > 1e-3f);       // sanity: es klingt

    inst.set_voice_filt(0, -1.f);                   // A weg, B bleibt
    CHECK(inst_rms(inst, 96000, 9600) > 1e-3f);

    inst.set_voice_filt(1, -1.f);                   // beide weg -> Stille
    CHECK(inst_rms(inst, 96000, 48000) < 1e-4f);
}

TEST_CASE("filt: scenario action reaches the instrument") {
    Instrument inst;
    inst.init(48000.f);
    for (int p = 0; p < 2; ++p) {
        inst.set_rate(p, 0.8f);
        inst.set_density(p, 1.f);
        Event e;
        e.action = "set_voice_filt";
        e.part = p;
        e.value = -1.f;
        apply_event(inst, e);
    }
    CHECK(inst_rms(inst, 96000, 48000) < 1e-4f);    // dispatch beweist sich als Stille
}
