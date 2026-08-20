#pragma once
#include <cmath>

namespace spky {

// A plain one-pole high-pass for the audio path.
//
// NOT engine/util/onepole.h: that one is a control-rate SMOOTHER and carries a
// 0.0005 deadband (`if (!_smoothing && fabs(diff) < 0.0005f) return _value;`)
// which is exactly wrong under an audio signal -- it would gate quiet passages
// to a frozen value. Same order, different job.
//
// y[n] = a * (y[n-1] + x[n] - x[n-1]), a = 1 / (1 + 2*pi*fc/sr).
//
// AT ITS BOTTOM RAIL THIS IS A BYPASS ON PAPER AND NOT IN FLOAT32, which is
// the trap for any engine whose EDGE neutral is "the corner at the bottom
// rail". set_hz(0) gives a == 1, where y[n] = y[n-1] + x[n] - x[n-1]
// telescopes to y[n] == x[n] -- in exact arithmetic. Each sample still pays
// one rounding of (y1 + x) - x1, and the error random-walks.
//
// MEASURED, not reasoned (scratchpad probe, 4.8 M samples at 48 kHz, this
// class as written): a 440.7 Hz sine comes out bit-identical on 0.48 % of
// samples, worst deviation 5.0e-6; with a 0.3 DC offset added it is 0.02 %,
// worst 2.1e-4. DC is NOT removed at this rail either -- a held step comes
// out held (1.000000 after one second), so "DC blocker" is the wrong mental
// model here. It becomes a real filter as soon as the corner leaves 0: at
// 20 Hz it takes 0.18 dB off 100 Hz, 0.01 dB off 1 kHz, and is -3.02 dB at
// its own corner.
//
// So: pin the neutral with a bit-equality test at t == 0, and when it fails,
// add an explicit branch that SKIPS process() entirely. Do not lower the
// corner until the difference hides under a tolerance -- that is a gate
// nothing can fail. FEED does not have this problem: its neutral is
// kDampFixedHz and pow(2, k*0) == 1 makes it exact by construction.
// init() LEAVES THE CORNER AT 20 Hz, not at 0. An engine whose EDGE neutral
// is "the bottom rail" and which never calls set_hz() is therefore filtering
// while believing it is neutral -- call set_hz(0.f) explicitly, or better,
// skip process() entirely at t == 0 (see the paragraph above).
class OnePoleHp {
public:
    void init(float sample_rate) {
        _sr = sample_rate > 0.f ? sample_rate : 48000.f;
        reset();
        set_hz(20.f);
    }
    void reset() { _x1 = 0.f; _y1 = 0.f; }
    void set_hz(float hz) {
        const float f = hz < 0.f ? 0.f : (hz > 0.45f * _sr ? 0.45f * _sr : hz);
        _a = 1.f / (1.f + 6.2831853f * f / _sr);
    }
    float process(float x) {
        _y1 = _a * (_y1 + x - _x1);
        _x1 = x;
        return _y1;
    }
#ifdef SPKY_TESTING
    // Test-only window onto the filter's own {x1, y1} history, guarded like
    // OnePole::coef() (engine/util/onepole.h) and the other SPKY_TESTING
    // accessors. Exists because a bit-equality comparison between two
    // engine instances that BOTH end up at EDGE == 0 cannot, by itself,
    // prove process() actually SKIPPED this filter rather than merely
    // running it at a coefficient that happens to match: two instances
    // taking the identical deterministic code path land on the identical
    // float bits regardless. Only this filter's OWN state -- read directly,
    // not inferred from a downstream comparison -- can tell "skipped" from
    // "ran once transparently" apart: it stays exactly {0, 0} only if
    // process() was never called at all.
    float x1_for_test() const { return _x1; }
    float y1_for_test() const { return _y1; }
#endif
private:
    float _sr = 48000.f, _a = 1.f, _x1 = 0.f, _y1 = 0.f;
};

} // namespace spky
