#pragma once
#include <cmath>

namespace spky {

// 2^x for the detune ratio (CPU round 2, candidate V5; MorphOsc and WtOsc,
// set_detune_cents). The argument there is cents / 1200: a spread of at most
// +-52.5 ct (kDetuneCeilCt 105 split across the two oscillators) plus the
// +-3 ct drift, so |x| <= 0.0463, and the polynomial is built for the wider
// [-0.1, 0.1] (+-120 ct).
//
// 2^x = e^(x ln 2), expanded to fourth order in x. The first omitted term is
// (x ln 2)^5 / 120, 1.3e-8 of the result at the ends of the range; the
// measured worst error over the whole range is 7.2e-8 relative, 0.00013 ct
// (tests/test_lean_exp2.cpp), against the 5.8e-6 (0.01 ct) the detune needs.
// x == 0 gives exactly 1.f (the Horner chain vanishes).
//
// Outside [-0.1, 0.1] it falls back to std::pow(2.f, x) rather than clamping,
// so a larger argument still gets the right ratio, at the old cost. A NaN
// passes both comparisons and comes out of the polynomial as NaN.
inline float fast_exp2(float x) {
    if (x < -0.1f || x > 0.1f) return std::pow(2.f, x);
    return 1.f + x * (0.693147181f
                 + x * (0.240226507f
                 + x * (0.0555041087f
                 + x * 0.00961812911f)));
}

} // namespace spky
