// CPU round 2, candidate V5 (class 2, spec 2026-10-30-cpu-engine-kernels-
// design.md section 6): the detune ratio 2^(ct / 1200) in MorphOsc and WtOsc
// (set_detune_cents) from util/fast_exp2.h instead of std::pow. Built into
// spky_lean_tests only, the desktop target compiled with SPKY_LEAN_EXP2=1 as a
// SHELL_LEAN_EXP2=1 firmware is. The old path's reference here is std::pow
// itself, called by the test; the engine-level old-against-new comparison
// needs both paths in one binary and lives in tests/test_cpu_round2_probes.cpp
// (toggle bit 25). The measured numbers ride along as CAPTUREs.
#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "synth/morph_osc.h"
#include "synth/voice.h"
#include "synth/wt_osc.h"
#include "util/fast_exp2.h"

namespace {

constexpr float kSr = 48000.f;

// The error of a ratio r against its reference, in cents.
double cents_off(double r, double ref) { return 1200.0 * std::log2(r / ref); }

// The oscillator's rising zero crossings over one second.
template <class Osc>
int rising_crossings(Osc& o) {
    int n = 0;
    float prev = 0.f;
    bool finite = true;
    for (int i = 0; i < 48000; ++i) {
        const float y = o.process();
        finite = finite && std::isfinite(y);
        if (i > 0 && prev <= 0.f && y > 0.f) ++n;
        prev = y;
    }
    CHECK(finite);
    return n;
}

// The wraps of a phasor stepping f * ratio / sr, with the ratio from
// std::pow(2.f, ct / 1200.f) as the old set_detune_cents computed it: the
// rising crossings the oscillator should show, whichever ratio it used. The
// first sample is not counted, as in rising_crossings.
int reference_crossings(float f, float ct) {
    const float ratio = std::pow(2.f, ct * (1.f / 1200.f));
    const float inc = f * ratio / kSr;
    float phase = 0.f;
    int n = 0;
    for (int i = 0; i < 48000; ++i) {
        phase += inc;
        if (phase >= 1.f) { phase -= 1.f; if (i > 0) ++n; }
    }
    return n;
}

} // namespace

TEST_CASE("V5: fast_exp2 within 0.01 ct of std::pow(2, x)") {
    // Every float step of 1e-6 over [-0.1, 0.1], both ends included, against
    // the double-precision 2^x of the same float argument.
    double worst_rel = 0.0, worst_ct = 0.0, worst_old_ct = 0.0;
    float worst_at = 0.f;
    auto probe = [&](float x) {
        const double ref = std::pow(2.0, double(x));
        const double got = double(spky::fast_exp2(x));
        const double rel = std::fabs(got / ref - 1.0);
        if (rel > worst_rel) { worst_rel = rel; worst_at = x; }
        worst_ct = std::fmax(worst_ct, std::fabs(cents_off(got, ref)));
        // And against the old path's own float value.
        worst_old_ct = std::fmax(worst_old_ct,
                                 std::fabs(cents_off(got, double(std::pow(2.f, x)))));
    };
    probe(-0.1f);
    probe(0.1f);
    for (int i = -100000; i <= 100000; ++i) probe(float(i) * 1e-6f);
    CAPTURE(worst_rel);
    CAPTURE(worst_ct);
    CAPTURE(worst_at);
    CAPTURE(worst_old_ct);
    CHECK(worst_rel <= 5.8e-6);      // 0.01 ct
    CHECK(worst_ct <= 0.01);
    CHECK(worst_old_ct <= 0.01);
    // The ratio at a detune of zero is 1 exactly, as std::pow(2, 0) is.
    CHECK(spky::fast_exp2(0.f) == 1.f);
    CHECK(spky::fast_exp2(-0.f) == 1.f);
}

TEST_CASE("V5: fast_exp2 outside [-0.1, 0.1] falls back to std::pow, exactly") {
    // Drift never takes the argument there (|x| <= 0.0463 in the engine), but
    // if it ever did the pitch must stay right, not clamp.
    const float xs[] = { 0.1000001f, -0.1000001f, 0.2f, -0.2f, 0.5f, -1.f, 1.f, 3.7f, -10.f };
    for (float x : xs) {
        CHECK(spky::fast_exp2(x) == std::pow(2.f, x));
    }
    // The ends themselves are the polynomial's (inside the range).
    CHECK(std::fabs(cents_off(double(spky::fast_exp2(0.1f)), std::pow(2.0, double(0.1f)))) <= 0.01);
    // NaN stays NaN, and the infinities go where std::pow sends them.
    CHECK(std::isnan(spky::fast_exp2(std::numeric_limits<float>::quiet_NaN())));
    CHECK(std::isinf(spky::fast_exp2(std::numeric_limits<float>::infinity())));
    CHECK(spky::fast_exp2(-std::numeric_limits<float>::infinity()) == 0.f);
}

TEST_CASE("V5: the lean arm is the one in use, on both oscillators") {
    // set_detune_cents stores fast_exp2(ct / 1200) bit for bit, and across the
    // engine's detune range some ct gives a ratio std::pow would round
    // differently, so a build that still called std::pow could not pass.
    int differs = 0;
    for (int i = -150; i <= 150; ++i) {
        const float ct = float(i) * 0.37f;
        const float x = ct * (1.f / 1200.f);
        spky::MorphOsc m;
        m.init(kSr);
        m.set_detune_cents(ct);
        spky::WtOsc w;
        w.init(kSr);
        w.set_detune_cents(ct);
        if (ct == 0.f) continue;       // the guard returns early: _ct starts at 0
        CHECK(m.ratio() == spky::fast_exp2(x));
        CHECK(w.ratio() == spky::fast_exp2(x));
        if (m.ratio() != std::pow(2.f, x)) ++differs;
    }
    CHECK(differs > 0);
}

TEST_CASE("V5: both oscillators keep their pitch, zero-crossing count over 1 s") {
    // The lean oscillators against the crossings of a phasor with the OLD
    // path's ratio, at the detune ends the engine reaches (+-55.5 ct: half the
    // spread plus the drift) and in between, across octaves.
    const float cts[] = { -55.5f, -30.f, -3.f, 0.f, 3.f, 17.f, 55.5f, 105.f };
    const float fs[]  = { 110.f, 220.f, 311.13f, 700.f, 1000.f };
    for (float f : fs) {
        for (float ct : cts) {
            spky::MorphOsc m;                    // morph 0: a pure sine
            m.init(kSr);
            m.set_detune_cents(ct);
            m.set_freq(f);
            const int want = reference_crossings(f, ct);
            const int got_m = rising_crossings(m);
            CAPTURE(f); CAPTURE(ct); CAPTURE(got_m); CAPTURE(want);
            CHECK(got_m == want);

            // WtOsc frame 0 at position 0: set_morph(0) is the default; its
            // fundamental is the only zero crossing pair per cycle.
            spky::WtOsc w;
            w.init(kSr);
            w.set_detune_cents(ct);
            w.set_freq(f);
            const int got_w = rising_crossings(w);
            CAPTURE(got_w);
            CHECK(got_w == want);
        }
    }
}

namespace {

// A voice at a sustained note, DETUNE and the drift LFO on, the control tick
// every 96 samples as the engine runs it. Output finite, bounded, sounding.
template <class Osc>
void check_voice_output() {
    spky::VoiceT<Osc> v;
    v.init(kSr, 7u);
    v.set_env_times(0.002f, 5.f);
    v.set_morph(0.4f);
    v.set_detune_cents(105.f);
    v.set_sub_level(0.3f);
    v.set_cutoff_hz(8000.f);
    v.set_resonance(0.3f);
    v.set_drift_amount(1.f);
    v.set_sustaining(true);
    v.trigger(311.13f);
    double sig = 0.0;
    float peak = 0.f;
    for (int t = 0; t < 500; ++t) {
        v.update_control(96.f / kSr);
        for (int i = 0; i < 96; ++i) {
            float l = 0.f, r = 0.f;
            v.process(l, r);
            CHECK(std::isfinite(l));
            CHECK(std::isfinite(r));
            sig += double(l) * l;
            peak = std::fmax(peak, std::fabs(l));
        }
    }
    CHECK(sig > 1.0);        // the voice sounded
    CHECK(peak < 2.f);
}

} // namespace

TEST_CASE("V5: a SYNTH and a WAVE voice stay finite and bounded with the lean ratio") {
    check_voice_output<spky::MorphOsc>();
    check_voice_output<spky::WtOsc>();
}
