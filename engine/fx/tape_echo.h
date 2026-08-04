#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "util/fast_tanh.h"
#include "util/math.h"

namespace spky {

// Interpolating delay line over caller-owned memory. MaxSize is a power of
// two so every audio-rate wrap is an AND mask rather than integer division.
template <typename T, size_t MaxSize>
class InjectedDelayLine {
    static_assert(MaxSize > 1 && (MaxSize & (MaxSize - 1)) == 0,
                  "MaxSize must be a power of two");
    static constexpr int32_t kMask = static_cast<int32_t>(MaxSize) - 1;

public:
    InjectedDelayLine() = default;
    InjectedDelayLine(const InjectedDelayLine&) = delete;
    InjectedDelayLine& operator=(const InjectedDelayLine&) = delete;

    void Init(T* memory) {
        _line = memory;
        Reset();
    }

    void Reset() {
        std::memset(_line, 0, MaxSize * sizeof(T));
        _write = 0;
        _delay = 1;
        _frac = 0.f;
    }

    void SetDelay(float samples) {
        if (samples < 1.f) samples = 1.f;
        if (samples > static_cast<float>(MaxSize - 2))
            samples = static_cast<float>(MaxSize - 2);
        const int32_t whole = static_cast<int32_t>(samples);
        _delay = whole & kMask;
        _frac = samples - static_cast<float>(whole);
    }

    T Read() const {
        const T a = _line[(_write + _delay) & kMask];
        const T b = _line[(_write + _delay + 1) & kMask];
        return a + (b - a) * _frac;
    }

    void Write(T x) {
        _line[_write] = x;
        _write = (_write - 1) & kMask;
    }

private:
    T* _line = nullptr;
    float _frac = 0.f;
    int32_t _write = 0;
    int32_t _delay = 1;
};

// The historical tape echo's single 800 Hz band-pass section, with the
// effective Q of 0.1 produced by the original SetParams(800, 0).
class TapeBpf {
public:
    void Init(float sample_rate) {
        constexpr float pi = 3.14159265358979f;
        constexpr float cutoff_hz = 800.f;
        constexpr float q = 0.1f;
        const float k = std::tan(pi * cutoff_hz / sample_rate);
        const float ksq = k * k;
        const float norm = 1.f / (1.f + (k / q) + ksq);
        _b0 = (k / q) * norm;
        _a1 = 2.f * (ksq - 1.f) * norm;
        _a2 = (1.f - (k / q) + ksq) * norm;
        _s1 = _s2 = 0.f;
    }

    float Process(float in) {
        const float y = _b0 * in + _s1;
        _s1 = _s2 - _a1 * y;
        _s2 = -_b0 * in - _a2 * y;
        if (_s1 < 1e-9f && _s1 > -1e-9f) _s1 = 0.f;
        if (_s2 < 1e-9f && _s2 > -1e-9f) _s2 = 0.f;
        return y;
    }

    void Reset() { _s1 = _s2 = 0.f; }

private:
    float _b0 = 0.f, _a1 = 0.f, _a2 = 0.f;
    float _s1 = 0.f, _s2 = 0.f;
};

// Tape-style, full-wet feedback echo. Delay-time smoothing belongs to its
// owner; this primitive accepts an already-smoothed delay in samples.
template <size_t MaxSize>
class TapeEcho {
public:
    TapeEcho() = default;
    TapeEcho(const TapeEcho&) = delete;
    TapeEcho& operator=(const TapeEcho&) = delete;

    void Init(float sample_rate, float* memory) {
        _line.Init(memory);
        _bpf.Init(sample_rate);
        _feedback = 0.f;
    }

    void Reset() {
        _line.Reset();
        _bpf.Reset();
    }

    void SetFeedback(float feedback) { _feedback = feedback; }
    float Feedback() const { return _feedback; }

    float Process(float in, float delay_samples) {
        _line.SetDelay(delay_samples);
        float out = _bpf.Process(_line.Read());
        out = fast_tanh(out);
        _line.Write(out * _feedback + in);
        return out;
    }

private:
    float _feedback = 0.f;
    InjectedDelayLine<float, MaxSize> _line;
    TapeBpf _bpf;
};

// FXT_FLUX_TIME's geometric depth map: 0 -> x1/4, 0.5 -> x1, 1 -> x4.
// The table's one-time pow construction is forced by Flux::init; subsequent
// audio-rate calls are clamp, index and linear interpolation only.
inline float tape_time_mult(float norm) {
    static const std::array<float, 65> table = [] {
        std::array<float, 65> t{};
        for (size_t i = 0; i < t.size(); ++i) {
            const float n = static_cast<float>(i) /
                            static_cast<float>(t.size() - 1);
            t[i] = std::pow(2.f, 4.f * (n - 0.5f));
        }
        return t;
    }();
    const float p = clampf(norm, 0.f, 1.f) * 64.f;
    int i = static_cast<int>(p);
    if (i > 64) i = 64;
    const int j = (i < 64) ? i + 1 : 64;
    return table[i] + (table[j] - table[i]) *
                          (p - static_cast<float>(i));
}

} // namespace spky
