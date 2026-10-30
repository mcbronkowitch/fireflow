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

    // Leaves the arena as it is: a cell not written since the last Reset reads
    // as zero (Read), which is what clearing it would give. Clearing it here
    // put 2 x 1 MB through memset inside the audio callback whenever FLUX
    // switched off -- ~6.3 M cycles on Rev A (fault R2,
    // docs/hardware/reva-bring-up.md).
    void Reset() {
        _filled = 0;
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
        // A cell k writes back exists only if k <= _filled.
        const T a = _delay <= _filled ? _line[(_write + _delay) & kMask] : T{};
        const T b = _delay < _filled ? _line[(_write + _delay + 1) & kMask] : T{};
        return a + (b - a) * _frac;
    }

    void Write(T x) {
        _line[_write] = x;
        _write = (_write - 1) & kMask;
        if (_filled < kMask) ++_filled;
    }

    // ---- Two lines in lock step (FLUX's L and R; the only path since toggle bit 4 was retired) ----
    // Both lines are Init-ed, Reset, SetDelay-ed and Write-n together and
    // nowhere else, so their _write, _filled, _delay and _frac are always
    // equal; only the memory differs. These do the shared part (SetDelay's
    // clamp / whole / frac, the read indices, the _filled tests, the write
    // index and the _filled count) once and then do each line's own cell
    // reads and writes with exactly the single-line arithmetic. Precondition:
    // `a` and `b` are in the same state.
    static void SetDelayPair(InjectedDelayLine& a, InjectedDelayLine& b,
                             float samples) {
        a.SetDelay(samples);
        b._delay = a._delay;
        b._frac  = a._frac;
    }
    static void ReadPair(const InjectedDelayLine& a, const InjectedDelayLine& b,
                         T& ra, T& rb) {
        const int32_t d = a._delay, w = a._write, f = a._filled;
        const bool have_a = d <= f, have_b = d < f;
        const int32_t ia = (w + d) & kMask, ib = (w + d + 1) & kMask;
        const T a0 = have_a ? a._line[ia] : T{};
        const T a1 = have_b ? a._line[ib] : T{};
        const T b0 = have_a ? b._line[ia] : T{};
        const T b1 = have_b ? b._line[ib] : T{};
        ra = a0 + (a1 - a0) * a._frac;
        rb = b0 + (b1 - b0) * a._frac;
    }
    static void WritePair(InjectedDelayLine& a, InjectedDelayLine& b, T xa, T xb) {
        const int32_t w = a._write;
        a._line[w] = xa;
        b._line[w] = xb;
        const int32_t nw = (w - 1) & kMask;
        a._write = b._write = nw;
        int32_t nf = a._filled;
        if (nf < kMask) ++nf;
        a._filled = b._filled = nf;
    }

private:
    T* _line = nullptr;
    float _frac = 0.f;
    int32_t _write = 0;
    int32_t _delay = 1;
    int32_t _filled = 0;   // cells written since Reset, saturating below MaxSize
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

    // Process() for two echoes that have always been driven together (same
    // delay, same state; see InjectedDelayLine::ReadPair): the same filter,
    // saturator and feedback arithmetic per channel, the line bookkeeping
    // once. Bit-identical to a.Process(in_a, d) and b.Process(in_b, d).
    static void ProcessPair(TapeEcho& a, TapeEcho& b, float in_a, float in_b,
                            float delay_samples, float& out_a, float& out_b) {
        InjectedDelayLine<float, MaxSize>::SetDelayPair(a._line, b._line,
                                                        delay_samples);
        float la, lb;
        InjectedDelayLine<float, MaxSize>::ReadPair(a._line, b._line, la, lb);
        float oa = a._bpf.Process(la);
        float ob = b._bpf.Process(lb);
        oa = fast_tanh(oa);
        ob = fast_tanh(ob);
        InjectedDelayLine<float, MaxSize>::WritePair(
            a._line, b._line, oa * a._feedback + in_a, ob * b._feedback + in_b);
        out_a = oa;
        out_b = ob;
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
