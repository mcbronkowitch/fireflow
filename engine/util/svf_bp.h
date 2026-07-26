#pragma once

namespace spky {

// Band-pass-only batched SVF for the BODY mode bank.
//
// The recurrence, the two-integrator topology and the h/g/r_plus_g formulation
// are copied unchanged from daisysp::ResonatorSvf<N>::Process
// (lib/DaisySP/Source/PhysicalModeling/resonator.h, Copyright 2020
// Electrosmith / Emilie Gillet, MIT -- see THIRD_PARTY.md). One thing is
// different, and it is the reason this file exists:
//
//   ResonatorSvf::Process computes g = fasttan(f), r = 1/q and
//   h = 1/(1 + r*g + g*g) INSIDE the per-sample call, for every mode. That is
//   a polynomial tangent and two divisions per mode per sample. Those three
//   values depend only on frequency and Q, and in this engine both change once
//   per 96-sample control tick. So they are pushed in from outside via
//   set_coeffs() and the per-sample path does arithmetic only.
//
// Low-pass, high-pass, notch and peak outputs are not computed: nothing reads
// them (same reasoning as util/svf_lp.h). The low-pass intermediate `lp` stays
// because state_2's update needs it.
template <int N>
class SvfBp {
public:
    void reset() {
        for (int i = 0; i < N; ++i) { _s1[i] = 0.f; _s2[i] = 0.f; }
    }

    // Control-rate feed. g = tan(pi * f_normalized), r_plus_g = 1/q + g,
    // h = 1 / (1 + g/q + g*g).
    void set_coeffs(int i, float g, float r_plus_g, float h) {
        _g[i] = g; _rg[i] = r_plus_g; _h[i] = h;
    }

    // Per-sample. Returns sum(gain[i] * bandpass_i(in)).
    float process(const float* gain, float in) {
        float out = 0.f;
        for (int i = 0; i < N; ++i) {
            const float hp = (in - _rg[i] * _s1[i] - _s2[i]) * _h[i];
            const float bp = _g[i] * hp + _s1[i];
            _s1[i] = _g[i] * hp + bp;
            const float lp = _g[i] * bp + _s2[i];
            _s2[i] = _g[i] * bp + lp;
            out += gain[i] * bp;
        }
        return out;
    }

private:
    float _g[N]  = {};
    float _rg[N] = {};
    float _h[N]  = {};
    float _s1[N] = {};
    float _s2[N] = {};
};

} // namespace spky
