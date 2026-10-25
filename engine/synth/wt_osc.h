#pragma once

#include <cmath>

#include "synth/wt_bank.h"
#include "util/math.h"

namespace spky {

namespace detail {
// A mip fade's mix on its i-th sample, (i + 1) / N: the same correctly
// rounded quotient the division gave at run time (WtOsc::kXfadeMix).
template <int N>
struct XfadeMix {
    float v[N];
    constexpr XfadeMix() : v() {
        for (int i = 0; i < N; ++i) v[i] = static_cast<float>(i + 1) / N;
    }
    constexpr float operator[](int i) const { return v[i]; }
};
} // namespace detail

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
                _src_mip = -1;                     // a weighted mix now
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

    // always_inline: with the fade's lone-source read below, arm-none-eabi-g++
    // -O3 stopped inlining this into VoiceT<WtOsc>::process and `bl`-called a
    // 764-byte copy twice per voice sample (fault R4, read off the linked
    // ELF); engine/parts/part.h's Part::process has the same attribute and
    // the caveats that come with it.
    __attribute__((always_inline)) inline
    float process() {
        _phase += _inc;
        if (_phase >= 1.f) _phase -= 1.f;

        if (_position_remaining > 0) {
            _position += _position_step;
            --_position_remaining;
            if (_position_remaining == 0) _position = _position_target;
        }

        const Frames f = _frames();
        const float current = _read_mip(_mip, f);
        if (_mip_xfade_remaining == 0) return current;

        // The fade's own sample doubled a WAVE voice's cost on the board
        // (fault R4, docs/hardware/reva-bring-up.md): the source read through
        // the seven-weight loop and a division per sample. Both shortcuts
        // below give the same bits: a lone source of weight 1 reads as
        // 0 + 1 * x == 0 + x, and kXfadeMix holds the division's quotients.
        const float old = _src_mip >= 0 ? 0.f + _read_mip(_src_mip, f) : _read_mip_source(f);
        const float mix = kXfadeMix[kRampSamples - _mip_xfade_remaining];
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

    static constexpr detail::XfadeMix<kRampSamples> kXfadeMix{};

    // The two frames a read blends and their weight, shared by the current
    // mip and the fade's source on the same sample.
    struct Frames { int f0, f1; float blend; };
    Frames _frames() const {
        const int frame0 = static_cast<int>(_position);
        const int frame1 = frame0 < wt::kFrameCount - 1 ? frame0 + 1 : frame0;
        return {frame0, frame1, _position - frame0};
    }

    int _select_mip(float hz) const {
        float base_hz = _sr / 1024.f;
        int mip = 0;
        while (hz >= base_hz * 2.f && mip < wt::kMipCount - 1) {
            base_hz *= 2.f;
            ++mip;
        }
        return mip;
    }

    float _read_mip(int mip, const Frames& f) const {
        return lerpf(_read_frame(f.f0, mip), _read_frame(f.f1, mip), f.blend);
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
        _src_mip = mip;
    }

    float _read_mip_source(const Frames& f) const {
        float value = 0.f;
        for (int mip = 0; mip < wt::kMipCount; ++mip) {
            if (_mip_source_weight[mip] != 0.f)
                value += _mip_source_weight[mip] * _read_mip(mip, f);
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
    int _src_mip = 0;   // the lone weight-1 source, or -1 for a weighted mix
};

} // namespace spky
