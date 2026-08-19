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
// AT ITS BOTTOM RAIL THIS IS A DC BLOCKER, NOT A BYPASS. set_hz(0) gives
// a == 1, and y[n] = y[n-1] + x[n] - x[n-1] still removes DC and still costs
// the signal its lowest partials' phase. Any engine whose EDGE neutral is
// "the corner at the bottom rail" is therefore claiming that its own DC
// blocker already does this job -- check what Part/PartFx removes before
// claiming it, and pin the claim with a bit-equality test at t == 0. If that
// test cannot go green, add an explicit bypass branch; do not lower the
// corner until the difference hides under a tolerance (that is a gate
// nothing can fail). FEED does not have this problem: its neutral is
// kDampFixedHz and pow(2, k*0) == 1 makes it exact by construction.
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
private:
    float _sr = 48000.f, _a = 1.f, _x1 = 0.f, _y1 = 0.f;
};

} // namespace spky
