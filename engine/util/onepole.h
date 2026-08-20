#pragma once
#include <cmath>

namespace spky {

// Portable one-pole smoother (same math as the firmware's OnePoleSmoother,
// but with no libDaisy dependency).
class OnePole {
public:
    // Note: ModLane::_update_slew (engine/mod/lane.cpp) derives its own tick-
    // rate coefficient by mirroring this exact `k` formula + clamp, then
    // compounding it kTickInterval times, so its per-sample and tick slews
    // agree. If this formula changes, that derivation must change with it.
    void init(float sample_rate, float time_s = 0.001f) {
        if (time_s <= 0.f || sample_rate <= 0.f) { _kof = 1.f; return; }
        float k = 1.f / (time_s * sample_rate);
        _kof = k > 1.f ? 1.f : k;
    }

    // Direct coefficient override -- used by ModLane's tick-rate slew twin,
    // whose exact coefficient (1 - (1-k)^N) has no time_s equivalent.
    void set_coef(float k) { _kof = k < 0.f ? 0.f : (k > 1.f ? 1.f : k); }

    float process(float target) {
        float diff = target - _value;
        if (!_smoothing && std::fabs(diff) < 0.0005f) return _value;
        _smoothing = true;
        _value += _kof * diff;
        if (std::fabs(target - _value) < 0.0005f) {
            _value = target;
            _smoothing = false;
        }
        return _value;
    }

    void reset(float value = 0.f) { _value = value; _smoothing = false; }
    float value() const { return _value; }

#ifdef SPKY_TESTING
    // Test-only window onto the current coefficient, added so
    // Exciter::coef_for_test() (engine/body/exciter.h) can read what RESO and
    // EDGE together produced without OnePole growing a permanent public
    // accessor for it. Guarded like the SPKY_TESTING accessors in
    // body_voice.h, synth_engine.h, instrument.h, mod/lane.h and
    // mod/super_modulator.h -- this header backs BODY's exciter filter,
    // SYNTH/WAVE's smoothing and the master LEVEL gain, so it does not ship
    // an unguarded reader in every build for one caller's sake.
    float coef() const { return _kof; }
#endif

private:
    float _kof = 1.f;
    float _value = 0.f;
    bool  _smoothing = false;
};

} // namespace spky
