#pragma once

#include <cmath>

#include "synth/wt_bank.h"
#include "util/math.h"

namespace spky {

class WtOsc {
public:
    static constexpr int kRampSamples = 96;

    void init(float sample_rate) {
        _sr = sample_rate;
        _phase = 0.f;
        _freq = 220.f;
        _ratio = 1.f;
        _ct = 0.f;
        _inc = _freq / _sr;
        _position = 0.f;
        _position_target = 0.f;
        _position_step = 0.f;
        _position_remaining = 0;
        _mip = _select_mip(_freq);
        _old_mip = _mip;
        _mip_xfade_remaining = 0;
        _set_mip_source(_mip);
    }

    void set_freq(float hz) {
        const float max_hz = 0.45f * _sr;
        _freq = clampf(hz, 0.f, max_hz);
        const float actual_hz = clampf(_freq * _ratio, 0.f, max_hz);
        _inc = actual_hz / _sr;

        const int next_mip = _select_mip(actual_hz);
        if (next_mip != _mip) {
            if (_mip_xfade_remaining > 0) {
                const float completed = static_cast<float>(kRampSamples - _mip_xfade_remaining)
                    / kRampSamples;
                for (int mip = 0; mip < wt::kMipCount; ++mip)
                    _mip_source_weight[mip] *= 1.f - completed;
                _mip_source_weight[_mip] += completed;
            } else {
                _set_mip_source(_mip);
            }
            _old_mip = _mip;
            _mip = next_mip;
            _mip_xfade_remaining = kRampSamples;
        }
    }

    void set_detune_cents(float ct) { // control rate
        if (ct == _ct) return;
        _ct = ct;
        _ratio = std::pow(2.f, ct * (1.f / 1200.f));
        set_freq(_freq);
    }

    void set_morph(float m) {
        const float target = clampf(m, 0.f, 1.f) * 15.f;
        if (target == _position_target) return;
        _position_target = target;
        _position_step = (_position_target - _position) / kRampSamples;
        _position_remaining = kRampSamples;
    }

    void reset_phase(float ph = 0.f) { _phase = clampf(ph, 0.f, 0.999999f); }

    float process() {
        _phase += _inc;
        if (_phase >= 1.f) _phase -= 1.f;

        if (_position_remaining > 0) {
            _position += _position_step;
            --_position_remaining;
            if (_position_remaining == 0) _position = _position_target;
        }

        const float current = _read_mip(_mip);
        if (_mip_xfade_remaining == 0) return current;

        const float old = _read_mip_source();
        const float mix = static_cast<float>(kRampSamples - _mip_xfade_remaining + 1)
            / kRampSamples;
        const float output = lerpf(old, current, mix);
        --_mip_xfade_remaining;
        if (_mip_xfade_remaining == 0) {
            _old_mip = _mip;
            _set_mip_source(_mip);
        }
        return output;
    }

    float position() const { return _position; }
    float target_position() const { return _position_target; }
    int mip_level() const { return _mip; }
    bool mip_crossfading() const { return _mip_xfade_remaining > 0; }

private:
    static constexpr float kI16Scale = 1.f / 32112.f;

    int _select_mip(float hz) const {
        float base_hz = _sr / 1024.f;
        int mip = 0;
        while (hz >= base_hz * 2.f && mip < wt::kMipCount - 1) {
            base_hz *= 2.f;
            ++mip;
        }
        return mip;
    }

    float _read_mip(int mip) const {
        const int frame0 = static_cast<int>(_position);
        const int frame1 = frame0 < wt::kFrameCount - 1 ? frame0 + 1 : frame0;
        const float blend = _position - frame0;
        return lerpf(_read_frame(frame0, mip), _read_frame(frame1, mip), blend);
    }

    float _read_frame(int frame, int mip) const {
        const int length = wt::kMipLength[mip];
        const float index = _phase * length;
        const int index0 = static_cast<int>(index);
        const int index1 = index0 + 1 < length ? index0 + 1 : 0;
        const float blend = index - index0;
        const int16_t* const samples = wt::table(frame, mip);
        return lerpf(samples[index0] * kI16Scale, samples[index1] * kI16Scale, blend);
    }

    void _set_mip_source(int mip) {
        for (int i = 0; i < wt::kMipCount; ++i) _mip_source_weight[i] = 0.f;
        _mip_source_weight[mip] = 1.f;
    }

    float _read_mip_source() const {
        float value = 0.f;
        for (int mip = 0; mip < wt::kMipCount; ++mip) {
            if (_mip_source_weight[mip] != 0.f)
                value += _mip_source_weight[mip] * _read_mip(mip);
        }
        return value;
    }

    float _sr = 48000.f;
    float _phase = 0.f;
    float _freq = 220.f;
    float _ratio = 1.f;
    float _ct = 0.f;
    float _inc = 0.f;
    float _position = 0.f;
    float _position_target = 0.f;
    float _position_step = 0.f;
    int _position_remaining = 0;
    int _mip = 0;
    int _old_mip = 0; // Snap-state contract; active sources live in the weight array.
    int _mip_xfade_remaining = 0;
    float _mip_source_weight[wt::kMipCount] = {};
};

} // namespace spky
