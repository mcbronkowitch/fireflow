#pragma once
#include <cmath>
#include <cstdint>
#include "mod/rng.h"
#include "util/fast_sin.h"
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
        // Re-arm interval for the click zone's bowed form (spec 2: "in FLOW
        // the same character becomes continuous excitation"). A click is a
        // single impulse through the one-pole; a bowed click is a *stream*
        // of them, so continuous mode re-fires the impulse on a timer rather
        // than switching to a different signal (that would collapse into
        // the noise-burst zone, which is exactly what the zone-distinctness
        // tests guard against). The interval is TUNING MATERIAL, like every
        // other zone constant here -- 5 ms is a placeholder for "audibly a
        // rapid rattle, not a buzz."
        _click_interval = static_cast<int>(0.005f * _sr);
        if (_click_interval < 1) _click_interval = 1;
        _click_counter = 0;
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

    // Control rate. Fundamental for the ping zone.
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
        _click_counter = 0;   // next continuous-mode re-arm check fires immediately
        _lp.reset();   // clean impulse response per strike, no bleed-through
    }

    float process() {
        if (_env <= 0.f && !_continuous) return 0.f;

        const float z = _char * 3.f;
        float s = 0.f;

        if (z < 1.f) {                              // zone 0: click
            bool fire = _fresh;
            if (_continuous) {                      // bow: re-arm the impulse on a timer
                if (_click_counter <= 0) {
                    fire = true;
                    _click_counter = _click_interval;
                }
                --_click_counter;
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
    int   _click_counter = 0, _click_interval = 240;   // see init(): 5 ms at 48 kHz
    bool  _continuous = false, _fresh = false;
};

} // namespace spky
