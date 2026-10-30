#pragma once

// Halfband sample-rate converters for the half-rate reverb (toggle bit 9,
// class 2): 48 kHz -> 24 kHz in front of the tank, 24 kHz -> 48 kHz behind it.
//
// One linear-phase halfband lowpass, 47 taps, Kaiser window (beta 6), gain 1 at
// DC. Designed offline (scratchpad hb_design.py; the numbers are in
// .superpowers/sdd/2026-10-29-cpu-hunt-remote/task-10-class2-report.md):
//   passband 0..10 kHz   ripple +0.007 / -0.006 dB
//   12 kHz               -6.02 dB (every halfband is -6 dB at fs/4)
//   stopband 14..24 kHz  <= -62 dB
//   group delay          23 samples at 48 kHz, per converter
// Taps h[n], n = -23..23: h[0] = kC0, h[even != 0] = 0, h[+-d] = kHH[(d-1)/2]
// for odd d. Only the odd taps need multiplies; 12 distinct coefficients, and
// the symmetry halves the multiplies again: 12 per output sample.
#include <cstdint>

namespace spky {

namespace halfband {
constexpr float kC0 = 0.500020805f;
constexpr float kHH[12] = {
    0.316679666f,   -0.10126177f,   0.0558614725f, -0.0350971579f,
    0.0229021011f,  -0.0149255036f, 0.00948454704f, -0.00575812918f,
    0.00326219832f, -0.00166441724f, 0.00071243965f, -0.00020584901f };
constexpr int kDelay = 23;   // group delay in 48 kHz samples (per converter)
}

// Two consecutive 48 kHz samples in, one 24 kHz sample out.
class HalfbandDown {
public:
    void reset() { for (float& v : _h) v = 0.f; _w = 0; }

    float process(float a, float b) {
        _h[_w & 63u] = a; ++_w;
        _h[_w & 63u] = b; ++_w;
        // The newest sample is index _w - 1; the filter's centre is 23 older.
        const uint32_t c = _w - 1u - 23u;
        float y = halfband::kC0 * _h[c & 63u];
        for (uint32_t i = 0; i < 12; ++i) {
            const uint32_t d = 2u * i + 1u;
            y += halfband::kHH[i] * (_h[(c - d) & 63u] + _h[(c + d) & 63u]);
        }
        return y;
    }

private:
    float _h[64] = {};
    uint32_t _w = 0;
};

// One 24 kHz sample in, two 48 kHz samples out (zero-stuffing, the same
// lowpass at gain 2).
class HalfbandUp {
public:
    void reset() { for (float& v : _u) v = 0.f; _w = 0; }

    void process(float x, float& first, float& second) {
        _u[_w & 31u] = x; ++_w;
        const uint32_t m = _w - 1u;     // newest
        float y = 0.f;
        for (uint32_t i = 0; i < 12; ++i) {
            const uint32_t d = 2u * i + 1u;
            y += halfband::kHH[i] * (_u[(m - (23u - d) / 2u) & 31u]
                                     + _u[(m - (23u + d) / 2u) & 31u]);
        }
        first  = 2.f * y;                                         // y[2m]
        second = 2.f * halfband::kC0 * _u[(m - 11u) & 31u];       // y[2m + 1]
    }

private:
    float _u[32] = {};
    uint32_t _w = 0;
};

} // namespace spky
