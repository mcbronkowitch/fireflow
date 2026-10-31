// CPU round 2, candidate G2 (class 2, spec 2026-10-30-cpu-engine-kernels-
// design.md section 6): a grain's Hann window multiplies by 1/atk, 1/dec and
// 1/fade_len latched at spawn and at release instead of dividing per sample.
// Built into spky_lean_tests only, the desktop target compiled with
// SPKY_LEAN_GRAIN_WIN=1 as a SHELL_LEAN_GRAIN_WIN=1 firmware is; the default
// build keeps the dividing window, so these cases could not run there.
//
// The reference is the grain as it was, copied below with its dividing window.
// Both play the same constant buffer at ratio 0, so a grain's output is its
// window times its two pan gains: the cases compare outputs, which bounds the
// window difference from above (the gains are at most 1).
// The measured maxima ride along as CAPTUREs.
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>
#include "sampler/grain.h"
#include "sampler/sample_buffer.h"

using namespace spky;

namespace {

// Grain before G2: the window divides by atk, dec and the fade length on
// every sample. Copied from engine/sampler/grain.h (comments dropped).
class OldGrain {
public:
    void spawn(float start, float ratio, float pan, int len,
               int atk, int dec, bool reverse, float gain = 1.f) {
        _len     = len < 2 ? 2 : len;
        if (atk < 1) atk = 1;
        if (dec < 1) dec = 1;
        if (atk + dec > _len) {
            const long long total = static_cast<long long>(atk) + dec;
            atk = static_cast<int>((static_cast<long long>(atk) * _len) / total);
            dec = _len - atk;
            if (atk < 1) { atk = 1; dec = _len - 1; }
            if (dec < 1) { dec = 1; atk = _len - 1; }
        }
        _atk = atk; _dec = dec; _start = start; _off = 0.f; _ratio = ratio;
        _reverse = reverse; _i = 0; _rel_len = 0; _rel_ctr = 0; _active = true;
        const float a = (clampf(pan, -1.f, 1.f) + 1.f) * 0.125f;
        const float g = gain < 0.f ? 0.f : gain;
        _gr = fast_sin(a) * g;
        _gl = fast_sin(a + 0.25f) * g;
    }
    bool active() const { return _active; }
    void release(int fade_len) {
        if (!_active || _rel_len > 0) return;
        _rel_from = _level();
        _rel_len = fade_len < 1 ? 1 : fade_len;
        _rel_ctr = _rel_len;
    }
    void trim_total(int total, int min_fade) {
        if (!_active) return;
        if (min_fade < 1) min_fade = 1;
        int want = total - _i;
        if (want < min_fade) want = min_fade;
        const int left = _rel_len > 0 ? _rel_ctr : _len - _i;
        if (want >= left) return;
        _rel_from = _level();
        _rel_len  = want;
        _rel_ctr  = want;
    }
    void process(const SampleBuffer& buf, float& outL, float& outR) {
        if (!_active) { outL = 0.f; outR = 0.f; return; }
        float l = 0.f, r = 0.f;
        buf.read_linear(_start + _off, l, r);
        float w;
        if (_rel_len > 0) {
            --_rel_ctr;
            w = _rel_from * hann_value_at(static_cast<float>(_rel_ctr)
                                          / static_cast<float>(_rel_len));
            if (_rel_ctr <= 0) _active = false;
        } else {
            w = _window();
        }
        outL = l * w * _gl;
        outR = r * w * _gr;
        _off += _reverse ? -_ratio : _ratio;
        ++_i;
        if (_rel_len == 0 && _i >= _len) _active = false;
    }

private:
    float _level() const {
        if (_rel_len <= 0) return _window();
        return _rel_from * hann_value_at(static_cast<float>(_rel_ctr)
                                         / static_cast<float>(_rel_len));
    }
    float _window() const {
        if (_i < _atk)
            return hann_value_at(static_cast<float>(_i) / static_cast<float>(_atk));
        const int from_end = _len - 1 - _i;
        if (from_end < _dec)
            return hann_value_at(static_cast<float>(from_end) / static_cast<float>(_dec));
        return 1.f;
    }
    float _start = 0.f, _off = 0.f, _ratio = 1.f, _gl = 0.7071f, _gr = 0.7071f;
    float _rel_from = 0.f;
    int   _len = 0, _i = 0, _atk = 1, _dec = 1, _rel_len = 0, _rel_ctr = 0;
    bool  _reverse = false, _active = false;
};

constexpr size_t kCap = 48000;
constexpr float  kTol = 1e-6f;
constexpr int    kSizes[] = { 1, 2, 3, 96, 4801, 96000 };

// L a constant 1.0 and R a constant -1.0; with ratio 0 and a start inside the
// content every read returns exactly those, so a grain's output is its window
// times its pan gains.
struct FlatBuf {
    std::vector<SampleBuffer::Frame> mem{kCap};
    SampleBuffer buf;
    FlatBuf() {
        buf.init(mem.data(), kCap, 48000.f);       // init() clears: fill after
        for (size_t i = 0; i < kCap; ++i) { mem[i].l = 1.f; mem[i].r = -1.f; }
        buf.set_rec_size(kCap);
    }
};

struct Pair {
    Grain    n;      // the candidate (SPKY_LEAN_GRAIN_WIN=1 in this target)
    OldGrain o;
    float    worst = 0.f;
    bool     same_active = true;
    long     steps = 0;

    // pan -1 puts the whole signal on L at gain ~1: the L output IS the window.
    void spawn(int len, int atk, int dec, float pan = -1.f, float gain = 1.f) {
        n.spawn(100.f, 0.f, pan, len, atk, dec, false, gain);
        o.spawn(100.f, 0.f, pan, len, atk, dec, false, gain);
    }
    // One sample of both; returns the new grain's output.
    void step(const SampleBuffer& b, float* nl = nullptr, float* ol = nullptr) {
        float al = 0.f, ar = 0.f, bl = 0.f, br = 0.f;
        n.process(b, al, ar);
        o.process(b, bl, br);
        worst = std::fmax(worst, std::fmax(std::fabs(al - bl), std::fabs(ar - br)));
        if (n.active() != o.active()) same_active = false;
        if (nl) *nl = al;
        if (ol) *ol = bl;
        ++steps;
    }
};

} // namespace

TEST_CASE("G2: window within 1e-6 of the dividing window") {
    FlatBuf f;
    double grid_dev = 0.0;      // the deviation summed over the grid
    // Every _i of a grain, for atk and dec over the whole grid; 7 samples of
    // plateau between the halves.
    for (int atk : kSizes) {
        for (int dec : kSizes) {
            const int len = atk + dec + 7;
            Pair p;
            p.spawn(len, atk, dec);
            for (int i = 0; i < len; ++i) p.step(f.buf);
            CAPTURE(atk); CAPTURE(dec);
            CHECK(p.worst <= kTol);
            CHECK(p.same_active);
            CHECK_FALSE(p.n.active());          // the same lifetime
            grid_dev += p.worst;
        }
    }
    // The lean arm is the one in use: the reciprocal windows are not the
    // dividing ones bit for bit, so across the grid some sample differs (by at
    // most the 1e-6 above). A grain that still divided would read exactly 0.
    CAPTURE(grid_dev);
    CHECK(grid_dev > 0.0);
    // Spawns whose halves do not fit (scaled down at spawn), and a grain with
    // no plateau at all.
    const int odd[][3] = { {50, 30, 40}, {2, 9, 9}, {1000, 1000, 1000}, {96000, 96000, 96000},
                           {3, 1, 1}, {5, 4801, 3}, {4801, 96000, 1}, {4801, 1, 96000} };
    for (const auto& c : odd) {
        Pair p;
        p.spawn(c[0], c[1], c[2]);
        for (int i = 0; i < c[0]; ++i) p.step(f.buf);
        CAPTURE(c[0]); CAPTURE(c[1]); CAPTURE(c[2]);
        CHECK(p.worst <= kTol);
        CHECK(p.same_active);
        CHECK_FALSE(p.n.active());
    }
}

TEST_CASE("G2: the windows still reach exactly 0 and 1 at their ends") {
    FlatBuf f;
    for (int atk : kSizes) {
        for (int dec : kSizes) {
            const int len = atk + dec + 7;
            Pair p;
            p.spawn(len, atk, dec);
            float first = 1.f, last = 1.f;
            for (int i = 0; i < len; ++i) {
                float nl = 0.f, ol = 0.f;
                p.step(f.buf, &nl, &ol);
                if (i == 0)       first = nl;
                if (i == len - 1) last  = nl;
                // The plateau: window exactly 1, so the output is the gain
                // alone, bit for bit what the dividing grain emits.
                if (i >= atk && i <= len - 1 - dec) {
                    CHECK(nl == ol);
                    CHECK(nl > 0.f);
                }
            }
            CAPTURE(atk); CAPTURE(dec);
            CHECK(first == 0.f);                 // the attack starts at 0
            CHECK(last == 0.f);                  // the decay ends at 0
        }
    }
    // A release fade ends at exactly 0 as well: the grain retires on the
    // sample whose multiplier is hann(0) * frozen level.
    for (int fade : kSizes) {
        Pair p;
        p.spawn(4000, 96, 96);
        for (int i = 0; i < 1500; ++i) p.step(f.buf);      // on the plateau
        p.n.release(fade);
        p.o.release(fade);
        float nl = 1.f;
        for (int i = 0; i < fade; ++i) p.step(f.buf, &nl);
        CAPTURE(fade);
        CHECK(nl == 0.f);
        CHECK_FALSE(p.n.active());
        CHECK_FALSE(p.o.active());
        CHECK(p.worst <= kTol);
    }
}

TEST_CASE("G2: releases and trims at random points stay within 1e-6 and retire together") {
    FlatBuf f;
    std::mt19937 rng(20261031u);
    auto pick = [&rng](int lo, int hi) {                    // inclusive
        return lo + static_cast<int>(rng() % static_cast<uint32_t>(hi - lo + 1));
    };
    const int kFades[] = { 1, 2, 3, 96, 4801, 96000 };
    Pair all;
    for (int trial = 0; trial < 600; ++trial) {
        const int atk = kSizes[pick(0, 4) ], dec = kSizes[pick(0, 4)];
        const int len = atk + dec + pick(0, 3000);
        Pair p;
        p.spawn(len, atk, dec, (pick(0, 200) - 100) / 100.f, pick(0, 150) / 100.f);
        // Run to a random point (including the very first sample and past
        // the end of the attack), then fade from wherever the window is.
        const int at = pick(0, len - 1);
        for (int i = 0; i < at; ++i) p.step(f.buf);
        const int fade = (trial % 3 == 0) ? pick(1, 20000) : kFades[pick(0, 5)];
        p.n.release(fade);
        p.o.release(fade);
        // Sometimes a trim: while fading (shortens the fade) or, on a trial
        // that has not faded yet, on the plateau/window.
        const int later = pick(0, 40);
        for (int i = 0; i < later; ++i) p.step(f.buf);
        if (trial % 2 == 0) {
            const int total = pick(0, len + fade);
            const int minf  = kFades[pick(0, 5)];
            p.n.trim_total(total, minf);
            p.o.trim_total(total, minf);
        }
        int guard = 0;
        while ((p.n.active() || p.o.active()) && guard++ < len + fade + 200000)
            p.step(f.buf);
        CAPTURE(trial); CAPTURE(atk); CAPTURE(dec); CAPTURE(len); CAPTURE(at); CAPTURE(fade);
        REQUIRE(p.same_active);
        REQUIRE_FALSE(p.n.active());
        REQUIRE(p.worst <= kTol);
        all.worst = std::fmax(all.worst, p.worst);
        all.steps += p.steps;
    }
    // Trims alone, on grains that never released: the cap shortens the life
    // and the fade length it creates is a new divisor.
    for (int trial = 0; trial < 300; ++trial) {
        const int atk = kSizes[pick(0, 4)], dec = kSizes[pick(0, 4)];
        const int len = atk + dec + pick(0, 3000);
        Pair p;
        p.spawn(len, atk, dec);
        const int at = pick(0, len - 1);
        for (int i = 0; i < at; ++i) p.step(f.buf);
        const int total = pick(0, len);
        const int minf  = kFades[pick(0, 5)];
        p.n.trim_total(total, minf);
        p.o.trim_total(total, minf);
        int guard = 0;
        while ((p.n.active() || p.o.active()) && guard++ < len + 200000)
            p.step(f.buf);
        CAPTURE(trial); CAPTURE(len); CAPTURE(at); CAPTURE(total); CAPTURE(minf);
        REQUIRE(p.same_active);
        REQUIRE_FALSE(p.n.active());
        REQUIRE(p.worst <= kTol);
        all.worst = std::fmax(all.worst, p.worst);
        all.steps += p.steps;
    }
    // Releases and trims reach the new divisors too: the reciprocal fade also
    // differs from the dividing one somewhere.
    CAPTURE(all.worst);
    CHECK(all.worst > 0.f);
}
