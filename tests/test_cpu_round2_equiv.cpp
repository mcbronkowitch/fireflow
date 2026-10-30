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
        if (seg <= 1.f) return lerpf(fast_sin(t), _tri(t), seg);
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
