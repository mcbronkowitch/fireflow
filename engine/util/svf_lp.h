#pragma once
#include <cmath>

namespace spky {

// Low-pass-only Chamberlin SVF: daisysp::Svf with everything the engine never
// reads taken out. `Low()` is BIT-IDENTICAL to daisysp::Svf::Low() for every
// call sequence this engine makes; nothing here re-tunes, re-scales or
// re-shapes the filter.
//
// Why it exists (2026-07-22 CPU hunt, measured on the Daisy Seed). Both users
// of the old filter -- Voice (synth/voice.h) and SamplerEngine's stereo pair
// (sampler/sampler_engine.h) -- call SetDrive(0.f) once at init and then read
// ONLY Low(). Counted exactly on the desktop against bench setup_inst_worst,
// the two-part instrument runs 768 Svf::Process calls per 96-sample block, and
// each of those was computing five outputs and a cubic drive term to throw
// four and a half of them away:
//
//   - out_high_/out_band_/out_peak_/out_notch_ are dead stores. Nothing in
//     engine/**, host/** or tests/** ever calls High(), Band(), Peak() or
//     Notch(). Two passes x four outputs = eight multiplies, one subtract and
//     eight member stores per sample per filter, all unread.
//   - `- drive_ * band_ * band_ * band_` is dead arithmetic. drive_ is
//     pre_drive_ * res_, pre_drive_ comes only from SetDrive, and both call
//     sites pass 0.f exactly once and never again -- so drive_ is identically
//     0.f for the whole run and the term subtracts exactly zero. Three
//     multiplies and a subtract per pass, twice per sample.
//   - powf(res_, 0.25f) was recomputed on EVERY SetFreq, although it depends
//     on res_ alone. The synth pushes a modulated cutoff and a static
//     resonance at every control tick, so that powf ran 8x per block purely to
//     produce the number it produced last tick. It is cached here instead.
//     (SetRes still recomputes it -- that is where res_ actually changes.)
//
// The recurrence itself, the coefficient formulas, the two-pass double
// sampling and the 0.5x averaging are copied unchanged from
// lib/DaisySP/Source/Filters/svf.cpp (Copyright 2020 Electrosmith / Emilie
// Gillet, MIT -- see THIRD_PARTY.md). Keep them that way: the point of this
// class is that it is the same filter, not a better one.
//
// If a future feature needs the band-pass, high-pass, notch or peak output, or
// a non-zero drive, do NOT add it back here one member at a time -- go back to
// daisysp::Svf, which is still vendored, and pay for what you use.
class SvfLp {
public:
    void Init(float sample_rate) {
        _sr    = sample_rate;
        _fc    = 200.0f;
        _res   = 0.5f;
        _freq  = 0.25f;
        _damp  = 0.0f;
        _low   = 0.0f;
        _band  = 0.0f;
        _out_low = 0.0f;
        _fc_max  = _sr / 3.f;
        _res_damp = 2.0f * (1.0f - powf(_res, 0.25f));   // cache; see SetFreq
        _fc_req  = -1.f;   // guards: force the first Set* through
        _res_req = -1.f;
#if defined(SPKY_PROBE_TOGGLES) || SPKY_LEAN_SVF1
        _single = false;   // two passes until SetSinglePass(true)
#endif
    }

    void SetFreq(float f) {
        if (f == _fc_req) return;   // exact guard; sinf is not cheap
        _fc_req = f;
        _fc = fclamp_(f, 1.0e-6f, _fc_max);
        // fs*2 because double sampled -- daisysp::Svf::SetFreq, verbatim
        _freq = 2.0f * sinf(PI_F_ * min_(0.25f, _fc / (_sr * 2.0f)));
        _recalc_damp();
    }

    void SetRes(float r) {
        // The synth pushes one static RESONANCE value at every voice at every
        // control tick (8 calls per block on the two-part instrument), so this
        // guard removes 8 powf per block outright. Exact: same input, same damp.
        if (r == _res_req) return;
        _res_req = r;
        const float res = fclamp_(r, 0.f, 1.f);
        _res = res;
        _res_damp = 2.0f * (1.0f - powf(_res, 0.25f));
        _recalc_damp();
    }

    void Process(float in) {
        // first pass
        float notch = in - _damp * _band;
        _low  = _low + _freq * _band;
        float high = notch - _low;
        _band = _freq * high + _band;
        _out_low = 0.5f * _low;
        // second pass
        notch = in - _damp * _band;
        _low  = _low + _freq * _band;
        high  = notch - _low;
        _band = _freq * high + _band;
        _out_low += 0.5f * _low;
    }

    float Low() const { return _out_low; }

#if defined(SPKY_PROBE_TOGGLES) || SPKY_LEAN_SVF1
    // ---- One pass per sample (CPU round 2, candidate V6; class 2) ----------
    // Compiled only in the measurement image (SPKY_PROBE_TOGGLES, toggle bit
    // 19) and with the firmware switch SHELL_LEAN_SVF1=1 (SPKY_LEAN_SVF1).
    // Every other build -- desktop, VCV, and a firmware built with
    // SHELL_LEAN_SVF1=0 -- has none of it, and Process() above is the same in
    // all of them (the default firmware has it: the switch is on). Only the
    // voice switches it on (synth/voice.cpp); SamplerEngine's and FeedEngine's
    // filters stay two-pass in every build.
    //
    // It is the same filter, not a better one: Process()'s two steps folded
    // into one. One Chamberlin step at 2*sr maps the state x = (_low, _band)
    // to A x + b in, with
    //     A = [ 1  f ; -f  e ],   b = (0, f),   e = 1 - qf - f^2
    // (f = _freq, q = _damp). Two steps on the held input, and the average of
    // the two lows, are therefore exactly
    //     x' = A^2 x + (A + I) b in,   out = C x + (f^2/2) in,
    //     C  = (1 - f^2/2, f (1 + s)/2),   s = 1 + e = 2 - qf - f^2
    // which, written out with d = in - _low, is the four lines below:
    //     out   = _low + (f^2/2) d + (f (1 + s)/2) _band
    //     _low  = _low + f^2 d + f s _band
    //     _band = (e^2 - f^2) _band + f s d
    // Same state, same coordinates, same coefficients' inputs: the output is
    // Process()'s up to float rounding, under modulation too (the coefficients
    // change between calls in both), and the two can be swapped mid-stream
    // without a transient. Stable wherever Process() is (A^2's eigenvalues
    // are the squares of A's). tests/test_lean_svf1.cpp measures both with
    // sine sweeps. The coefficients are a few products at control rate, after
    // the sinf SetFreq already runs; no division, no tan.
    void SetSinglePass(bool on) {
        if (on == _single) return;
        _single = on;
        if (on) _recalc_single();
    }
    bool single_pass() const { return _single; }

    void ProcessSingle(float in) {
        const float d = in - _low;
        _out_low = _low + _s_h * d + _s_c1 * _band;
        _low  = _low + _s_f2 * d + _s_fs * _band;
        _band = _s_m11 * _band + _s_fs * d;
    }
#endif

    // Zero the state once it has decayed below audibility. A two-pole fed
    // exact zeros decays GEOMETRICALLY: it approaches zero without reaching
    // it, lands in the subnormal range and stays there for as long as the
    // input is silent. Measured on FEED 2026-08-19, where the deck's own
    // amplitude glide had just been fixed and the filter inherited the whole
    // tax: an idle deck still cost 1.17x a sounding one with nothing but this
    // left (docs/engine-map.md section 9).
    //
    // NOT called by Process -- this is opt-in, at the caller's control rate,
    // so Voice and SamplerEngine are bit-for-bit unaffected and the equality
    // against daisysp::Svf in tests/test_svf_lp.cpp still pins the algebra.
    //
    // 1e-15 is about -300 dBFS and sits 23 orders of magnitude above the
    // subnormal range.
    //
    // Both state variables are in the condition, but only _low is PROVABLY
    // load-bearing, and the comment says so rather than implying otherwise:
    // at this threshold a _low-only guard is not distinguishable by any test
    // written against it. A decaying two-pole whose _low is within 1e-15 of
    // zero has a _band that is tiny as well, and the zero crossings of a
    // still-audible ring never land inside a window that narrow. _band is
    // kept because kFlushEps is exactly the kind of constant somebody raises
    // later, and at a wider threshold the crossing does become reachable --
    // _low passes through zero on every cycle of a ring that _band is still
    // carrying at full size. What the gate in tests/test_svf_lp.cpp proves is
    // that the flush is CONDITIONAL at all: an unconditional one truncates a
    // 60 Hz ring to nothing 5 ms in.
    void FlushDenormals() {
        constexpr float kFlushEps = 1e-15f;
        if (fabsf(_low) < kFlushEps && fabsf(_band) < kFlushEps) {
            _low = 0.f;
            _band = 0.f;
            _out_low = 0.f;
        }
    }

private:
    static constexpr float PI_F_ = 3.1415927410125732421875f;   // == DaisySP PI_F
    static float min_(float a, float b) { return a < b ? a : b; }
    static float fclamp_(float in, float lo, float hi) {
        return fminf(fmaxf(in, lo), hi);
    }

    void _recalc_damp() {
        _damp = min_(_res_damp, min_(2.0f, 2.0f / _freq - _freq * 0.5f));
#if defined(SPKY_PROBE_TOGGLES) || SPKY_LEAN_SVF1
        if (_single) _recalc_single();
#endif
    }

#if defined(SPKY_PROBE_TOGGLES) || SPKY_LEAN_SVF1
    // The one-pass coefficients from _freq and _damp (see ProcessSingle).
    void _recalc_single() {
        const float f  = _freq;
        const float ff = f * f;
        const float e  = 1.0f - _damp * f - ff;
        const float s  = 1.0f + e;
        _s_f2  = ff;
        _s_h   = 0.5f * ff;
        _s_fs  = f * s;
        _s_c1  = 0.5f * f * (1.0f + s);
        _s_m11 = e * e - ff;
    }
#endif

    float _sr = 48000.f;
    float _fc = 200.f;
    float _fc_max = 16000.f;
    float _res = 0.5f;
    float _fc_req = -1.f;    // last SetFreq/SetRes argument (change guards)
    float _res_req = -1.f;
    float _res_damp = 0.f;
    float _freq = 0.25f;
    float _damp = 0.f;
    float _low = 0.f;
    float _band = 0.f;
    float _out_low = 0.f;
#if defined(SPKY_PROBE_TOGGLES) || SPKY_LEAN_SVF1
    bool  _single = false;   // ProcessSingle's coefficients are kept current
    float _s_f2 = 0.f;       // f^2
    float _s_h = 0.f;        // f^2 / 2
    float _s_fs = 0.f;       // f s
    float _s_c1 = 0.f;       // f (1 + s) / 2
    float _s_m11 = 1.f;      // e^2 - f^2
#endif
};

}  // namespace spky
