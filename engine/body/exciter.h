#pragma once
#include <cmath>
#include <cstdint>
#include "mod/rng.h"
#include "util/fast_sin.h"
#include "util/math.h"
#include "util/onepole.h"

namespace spky {

// The playable strike for BodyVoice (spec 2026-07-26-body-resonator-engine
// §2). Four characters across RESO; in FLOW the same character becomes
// continuous excitation -- the bow.
//
//   zone 0  0.00 <= c < 0.33   click        -- filtered impulse
//   zone 1  0.33 <= c < 0.67   noise burst  -- filtered noise
//   zone 2  0.67 <= c <= 1.00  sputter/ping -- rng-gated micro-bursts
//                                              crossfading to a fast_sin
//                                              blip at the fundamental
//
// Zone boundaries and the crossfade width are TUNING MATERIAL: the contract
// is "four distinguishable characters that all decay with the strike" (spec
// 2, DUST-zone precedent), not these particular numbers.
//
// The bow (FLOW). A click is a single impulse through the one-pole; a bowed
// click is a *stream* of them, so continuous mode re-fires the impulse rather
// than switching to a different signal -- switching would collapse zone 0
// into zone 1, which the zone-distinctness tests guard against.
//
// The re-arm runs at the FUNDAMENTAL. It used to run on a fixed 5 ms timer,
// which is exactly 200 Hz at 48 kHz, so a bowed click droned at 200 Hz
// whatever was played: a FLOW recording from 2026-07-28 measured an exact
// 200 Hz harmonic series with the played note nowhere in it. Driving the
// resonator at its own period is also the physically right answer -- every
// impulse then arrives in phase with the wave already circulating in the
// string, which is what a bow does.
//
// One coupling to know before changing it: the filter is not reset between
// re-arms, so a shorter period raises per-impulse amplitude as well as rate.
// Measured steady-state peak: flat at 0.230 for periods down to ~60 samples,
// then 0.248 at 20, 0.316 at 10, 0.565 at 5, saturating at 1.0 by 1. The
// pitch contract spans 110-880 Hz, i.e. 436 down to 55 samples at 48 kHz --
// the whole range sits in the flat part, so tracking f0 does not tilt the
// excitation level across the keyboard. Anyone extending the contract upward
// leaves the flat region and should re-measure.
//
// Everything derived from a parameter (the click/noise filter cutoff, the
// decay-per-sample coefficient, the ping phase increment) is computed in a
// setter, on the engine's 96-sample control tick -- never in process(). That
// is the fix this project has had to make three times already (see
// engine/body/ks_string.h, engine/body/mode_bank.h); process() below does
// table lookups and multiplies only, no libm.
class Exciter {
public:
    void init(uint32_t seed, float sample_rate) {
        _rng.seed(seed);
        _sr = sample_rate > 0.f ? sample_rate : 48000.f;
        _lp.reset();
        _char = 0.f;
        _decay = 0.f;
        _inc = 0.f;
        _env = 0.f;
        _phase = 0.f;
        _gate = 0.f;
        _burst = 0;
        _continuous = false;
        _fresh = false;
        _click_phase = 0.f;
        _recompute_filter();
    }

    // Control rate. 0..1, four zones (RESO) -- see the class comment.
    void set_character(float c) {
        _char = c < 0.f ? 0.f : (c > 1.f ? 1.f : c);
        _recompute_filter();
    }

    // Control rate. Strike length (ATTACK): 2 ms click up to a bowed swell.
    void set_length(float seconds) {
        const float n = seconds * _sr;
        _decay = n > 1.f ? (1.f - 1.f / n) : 0.f;
    }

    // Control rate. Fundamental for the ping zone AND for the click zone's
    // bow -- one increment, two consumers. At 0 Hz neither advances, which is
    // the same "no pitch, no excitation" the ping zone already had; the pitch
    // contract floors at 110 Hz, so nothing reaches it in practice.
    void set_freq(float hz) { _inc = hz > 0.f ? hz / _sr : 0.f; }

    // Control rate. FLOW bows instead of striking: the envelope stops
    // decaying and the character runs as continuous excitation.
    void set_continuous(bool on) { _continuous = on; }

    void strike(float velocity) {
        _env = velocity;
        _phase = 0.f;
        _burst = 0;
        _gate = 0.f;
        _fresh = true;
        _click_phase = 1.f;   // next continuous-mode re-arm fires immediately
        _lp.reset();   // clean impulse response per strike, no bleed-through
    }

    float process() {
        if (_env <= 0.f && !_continuous) return 0.f;

        const float z = _char * 3.f;
        float s = 0.f;

        if (z < 1.f) {                              // zone 0: click
            bool fire = _fresh;
            if (_continuous) {                      // bow: re-arm at the fundamental
                _click_phase += _inc;
                if (_click_phase >= 1.f) {
                    _click_phase -= 1.f;
                    fire = true;
                }
            }
            s = _lp.process(fire ? 1.f : 0.f);
        } else if (z < 2.f) {                        // zone 1: noise burst
            s = _lp.process(_rng.next_bipolar());
        } else {                                     // zone 2: sputter -> ping
            const float t = z - 2.f;                 // 0 = pure sputter, 1 = pure ping
            if (_burst-- <= 0) {
                _burst = 8 + static_cast<int>(24.f * _rng.next_unipolar());
                _gate = _rng.next_bipolar() > 0.f ? 1.f : 0.f;
            }
            const float sputter = _rng.next_bipolar() * _gate;

            _phase += _inc;
            if (_phase >= 1.f) _phase -= 1.f;
            const float ping = fast_sin(_phase);

            s = sputter * (1.f - t) + ping * t;
        }

        _fresh = false;
        if (!_continuous) _env *= _decay;
        return s * _env;
    }

#ifdef SPKY_TESTING
    // Test-only window onto the click/noise filter's current coefficient --
    // what RESO produced -- without reaching past _lp's own encapsulation.
    // Same idiom as the SPKY_TESTING accessors in
    // body_voice.h, synth_engine.h, instrument.h, mod/lane.h and
    // mod/super_modulator.h.
    float coef_for_test() const { return _lp.coef(); }

    // Sums n samples of process() with no other transform, for a test that
    // needs the raw output over a window rather than accumulated energy
    // (this file's energy() helper squares first and would hide a
    // sign-only or purely-RNG-driven difference).
    float render_sum_for_test(int n) {
        float sum = 0.f;
        for (int i = 0; i < n; ++i) sum += process();
        return sum;
    }
#endif

private:
    static constexpr float kTwoPi = 6.2831853f;

    // Control rate. The click/noise filter cutoff depends only on _char, so
    // it is derived here and cached as a OnePole coefficient -- never as a
    // per-sample cutoff_hz-to-coefficient conversion (that would be a
    // std::exp per sample, the exact daisysp::String mistake this engine
    // keeps having to unwind).
    void _recompute_filter() {
        const float z = _char * 3.f;
        float cutoff_hz;
        if (z < 1.f) {
            cutoff_hz = 2000.f + 6000.f * z;              // click: 2-8 kHz
        } else if (z < 2.f) {
            cutoff_hz = 1000.f + 9000.f * (z - 1.f);       // noise: 1-10 kHz
        } else {
            cutoff_hz = 10000.f;                           // unused (sputter/ping zone)
        }
        const float k = 1.f - std::exp(-kTwoPi * cutoff_hz / _sr);
        _lp.set_coef(k);
    }

    Rng     _rng;
    OnePole _lp;

    float _sr = 48000.f;
    float _char = 0.f, _decay = 0.f, _inc = 0.f;
    float _env = 0.f, _phase = 0.f, _gate = 0.f;
    int   _burst = 0;
    // Bow phase for the click zone, advanced by _inc -- see the class comment.
    // Separate from _phase, which the ping zone owns: the two zones are
    // mutually exclusive per sample, but a shared accumulator would still make
    // the crossfade between them depend on which one ran last.
    float _click_phase = 0.f;
    bool  _continuous = false, _fresh = false;
};

} // namespace spky
