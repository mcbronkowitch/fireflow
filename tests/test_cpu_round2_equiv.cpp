// CPU round 2's class-1 candidates (engine/util/probe_toggles.h, bits from
// 17; spec docs/superpowers/specs/2026-10-30-cpu-engine-kernels-design.md):
// each is compared bit for bit with a test-local copy of the arithmetic it
// replaces, as tests/test_cpu_hunt_equiv.cpp does for round 1. The default
// build compiles the new path only, so this is what keeps it honest there.
#include <doctest/doctest.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "synth/morph_osc.h"
#include "util/fast_sin.h"
#include "util/math.h"

using namespace spky;

namespace {

// xorshift32 noise, -1..1 (the seed is already well mixed).
struct Noise {
    uint32_t s = 2463534242u;
    float next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float)(int32_t)s * (1.f / 2147483648.f);
    }
};
uint32_t bits_of(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

// fast_sin as it was before V3 (util/fast_sin.h): the floor wrap inline, then
// the polynomial. Kept here so the references below do not share a body with
// the code under test.
float fast_sin_ref(float phase) {
    float p = phase - std::floor(phase);
    float q = p < 0.5f ? p : p - 1.f;
    float aq = q < 0.f ? -q : q;
    float y = 8.f * q * (1.f - 2.f * aq);
    float ay = y < 0.f ? -y : y;
    return 0.225f * (y * ay - y) + y;
}

// ---- V1: MorphOsc's per-sample invariants --------------------------------

// MorphOsc as it was before V1: dt, the segment and its weight recomputed on
// every sample from _inc and _morph.
struct MorphOscRef {
    float _sr = 48000.f, _phase = 0.f, _freq = 220.f, _ratio = 1.f, _ct = 0.f;
    float _inc = 0.f, _morph = 0.f;

    void init(float sample_rate) {
        _sr = sample_rate;
        _phase = 0.f;
        _ratio = 1.f;
        set_freq(220.f);
    }
    void set_freq(float hz) {
        _freq = hz < 0.f ? 0.f : hz;
        _inc = _freq * _ratio / _sr;
    }
    void set_detune_cents(float ct) {
        if (ct == _ct) return;
        _ct = ct;
        _ratio = std::pow(2.f, ct * (1.f / 1200.f));
        _inc = _freq * _ratio / _sr;
    }
    void set_morph(float m) { _morph = clampf(m, 0.f, 1.f); }
    void reset_phase(float ph = 0.f) { _phase = clampf(ph, 0.f, 0.999999f); }

    float process() {
        _phase += _inc;
        if (_phase >= 1.f) _phase -= 1.f;
        const float t = _phase;
        const float dt = _inc > 1e-6f ? _inc : 1e-6f;
        const float seg = _morph * 3.f;
        if (seg <= 1.f) return lerpf(fast_sin_ref(t), _tri(t), seg);
        if (seg <= 2.f) return lerpf(_tri(t), _saw(t, dt), seg - 1.f);
        return lerpf(_saw(t, dt), _pulse(t, dt), seg - 2.f);
    }

    static float _tri(float t) {
        if (t < 0.25f) return 4.f * t;
        if (t < 0.75f) return 2.f - 4.f * t;
        return 4.f * t - 4.f;
    }
    static float _polyblep(float t, float dt) {
        if (t < dt) {
            float x = t / dt;
            return x + x - x * x - 1.f;
        }
        if (t > 1.f - dt) {
            float x = (t - 1.f) / dt;
            return x * x + x + x + 1.f;
        }
        return 0.f;
    }
    static float _saw(float t, float dt) { return 2.f * t - 1.f - _polyblep(t, dt); }
    static float _pulse(float t, float dt) {
        float t2 = t + 0.5f;
        if (t2 >= 1.f) t2 -= 1.f;
        float v = t < 0.5f ? 1.f : -1.f;
        return v + _polyblep(t, dt) - _polyblep(t2, dt);
    }
};

// One case: 48 000 samples at a fixed morph and base frequency, the detune
// moving every 96 samples as Voice::_apply_freq moves it (+-25 ct; every
// fourth tick repeats the last value, so set_detune_cents' guard is crossed
// too), set_morph re-pushed on every tick as the voice does. Returns the
// count of samples whose bits differ.
int morph_mismatches(float morph, float freq) {
    MorphOsc osc;
    MorphOscRef ref;
    osc.init(48000.f);
    ref.init(48000.f);
    osc.set_freq(freq);
    ref.set_freq(freq);
    Noise n;
    float ct = 0.f;
    int bad = 0;
    for (int i = 0; i < 48000; ++i) {
        if (i % 96 == 0) {
            if ((i / 96) % 4 != 3) ct = 25.f * n.next();
            osc.set_detune_cents(ct);
            ref.set_detune_cents(ct);
            osc.set_morph(morph);
            ref.set_morph(morph);
        }
        if (bits_of(osc.process()) != bits_of(ref.process())) ++bad;
    }
    return bad;
}

} // namespace

TEST_CASE("V1: MorphOsc output bit-identical") {
    // The brief's sweep (morph at the three segments, their anchors 1/3 and
    // 2/3 and both ends; 20 Hz to 18 kHz), plus 0.03 Hz, where _inc falls
    // below dt's 1e-6 floor.
    const float morphs[] = { 0.f, 0.2f, 1.f / 3.f, 0.5f, 2.f / 3.f, 0.9f, 1.f };
    const float freqs[]  = { 20.f, 220.f, 4000.f, 18000.f, 0.03f };
    for (float m : morphs)
        for (float f : freqs) {
            CAPTURE(m);
            CAPTURE(f);
            CHECK(morph_mismatches(m, f) == 0);
        }
}

TEST_CASE("V1: MorphOsc morph and frequency moving together stay bit-identical") {
    // Both move on every tick, through every segment and across both
    // boundaries, from a phase that is not 0: the setters' cached segment
    // and dt must follow every change, in either order of the calls.
    MorphOsc osc;
    MorphOscRef ref;
    osc.init(48000.f);
    ref.init(48000.f);
    osc.reset_phase(0.37f);
    ref.reset_phase(0.37f);
    Noise n;
    int bad = 0;
    for (int i = 0; i < 96 * 2000; ++i) {
        if (i % 96 == 0) {
            const float m = 0.5f + 0.6f * n.next();          // clamps at both ends too
            const float f = 4000.f * (1.f + n.next());        // 0 .. 8 kHz
            const float ct = 30.f * n.next();
            if ((i / 96) % 2 == 0) {
                osc.set_morph(m);  ref.set_morph(m);
                osc.set_freq(f);   ref.set_freq(f);
                osc.set_detune_cents(ct); ref.set_detune_cents(ct);
            } else {
                osc.set_detune_cents(ct); ref.set_detune_cents(ct);
                osc.set_freq(f);   ref.set_freq(f);
                osc.set_morph(m);  ref.set_morph(m);
            }
        }
        if (bits_of(osc.process()) != bits_of(ref.process())) ++bad;
    }
    CHECK(bad == 0);
}

// ---- V3: sin without the redundant wrap ----------------------------------

namespace {

float float_of(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

} // namespace

TEST_CASE("V3: fast_sin_wrapped equals fast_sin on [0,1)") {
    // The sweep: every float of the 2^16 ulps just below 2^-10, the first and
    // last 4096 floats of every binade below 2^-10, +0 and the smallest
    // denormal; then 2^16 evenly spaced values from 2^-10 up, and the float
    // just below 1 (all of [0, 1) at 1 ulp would be ~1e9 floats).
    int bad = 0;
    long n = 0;
    auto check = [&](float p) {
        const float want = fast_sin_ref(p);
        if (bits_of(fast_sin_wrapped(p)) != bits_of(want)) ++bad;
        if (bits_of(fast_sin(p)) != bits_of(want)) ++bad;
        ++n;
    };
    const uint32_t k2m10 = 117u << 23;                        // 2^-10
    for (uint32_t u = k2m10 - 65536u; u < k2m10; ++u) check(float_of(u));
    for (uint32_t e = 1; e < 117u; ++e) {                     // binades 2^-126 .. 2^-11
        const uint32_t lo = e << 23, hi = (e + 1u) << 23;
        for (uint32_t i = 0; i < 4096u; ++i) { check(float_of(lo + i)); check(float_of(hi - 1u - i)); }
    }
    check(0.f);
    check(float_of(1u));                                      // smallest denormal
    for (uint32_t i = 0; i < 65536u; ++i) {
        const float p = 0.0009765625f + (1.f - 0.0009765625f) * ((float)i * (1.f / 65536.f));
        check(p < 1.f ? p : float_of(0x3F7FFFFFu));
    }
    check(float_of(0x3F7FFFFFu));
    CHECK(n > 900000);
    CHECK(bad == 0);
}

TEST_CASE("V3: the sub oscillator's wrapped phase gives the same bits") {
    // VoiceT::process' sub loop as it was: phase += inc, wrap once with >=,
    // fast_sin (which wraps again). The new loop hands the wrapped phase to
    // fast_sin_wrapped. inc is 0.5 * pitch / sr, as set_pitch_hz computes it
    // (about 5 Hz to 9 kHz).
    const float freqs[] = { 10.f, 55.f, 220.f, 1760.f, 8000.f, 18000.f, 0.03f };
    int bad = 0;
    for (float f : freqs) {
        const float inc = 0.5f * f / 48000.f;
        float ph_new = 0.f, ph_old = 0.f;
        for (int i = 0; i < 48000; ++i) {
            ph_old += inc;
            if (ph_old >= 1.f) ph_old -= 1.f;
            ph_new += inc;
            if (ph_new >= 1.f) ph_new -= 1.f;
            if (bits_of(fast_sin_ref(ph_old)) != bits_of(fast_sin_wrapped(ph_new))) ++bad;
        }
    }
    CHECK(bad == 0);
}
