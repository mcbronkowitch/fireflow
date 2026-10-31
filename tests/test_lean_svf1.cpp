// CPU round 2, candidate V6 (class 2, spec 2026-10-30-cpu-engine-kernels-
// design.md section 6): the voice's low-pass in ONE pass per sample
// (SvfLp::ProcessSingle) instead of the two-pass, 2x-oversampled Chamberlin
// recurrence (SvfLp::Process). Built into spky_lean_tests only, the desktop
// target compiled with SPKY_LEAN_SVF1=1 as a SHELL_LEAN_SVF1=1 firmware is;
// the default build has no ProcessSingle, so these cases could not run there.
//
// The reference is the two-pass filter itself, measured here with the same
// 8 192-sample sine sweeps as the candidate; no curve below is assumed.
// SPKY_V6_PRINT=1 in the environment prints the measured table.
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "util/svf_lp.h"
#include "synth/voice.h"

namespace {

constexpr float  kSr    = 48000.f;
constexpr int    kSweep = 8192;
constexpr double kPi    = 3.14159265358979323846;
// The sweep's top: the two-pass filter's response floors at Nyquist (its
// zero-order-held input and averaged output keep it from reaching zero), so
// near its top cutoff with some resonance it never falls 3 dB below DC. Its
// cutoff then reads as kTop, and the candidate must read kTop too.
constexpr double kTop   = 23900.0;

bool print_on() {
    static const bool on = std::getenv("SPKY_V6_PRINT") != nullptr;
    return on;
}

// Gain at `hz` of a filter set to (fc, res): an 8 192-sample unit sine from
// rest, read as one DFT bin over the last 4 096 samples under a Hann window.
// The start-up transient is the same for both filters (same poles, same
// input); the window keeps most of it out of the bin.
double gain_at(float fc, float res, bool single, double hz) {
    constexpr int half = kSweep / 2;
    static const std::vector<double> win = [] {
        std::vector<double> v(half);
        for (int j = 0; j < half; ++j) v[j] = 0.5 - 0.5 * std::cos(2.0 * kPi * j / half);
        return v;
    }();
    spky::SvfLp f;
    f.Init(kSr);
    f.SetFreq(fc);
    f.SetRes(res);
    if (single) f.SetSinglePass(true);
    const std::complex<double> rot = std::polar(1.0, 2.0 * kPi * hz / kSr);
    std::complex<double> ph = 1.0;   // e^{j w i}; the input is its imaginary part
    std::complex<double> acc = 0.0;
    double wsum = 0.0;
    for (int i = 0; i < kSweep; ++i) {
        const float x = static_cast<float>(ph.imag());
        if (single) f.ProcessSingle(x); else f.Process(x);
        if (i >= half) {
            acc += win[i - half] * static_cast<double>(f.Low()) * std::conj(ph);
            wsum += win[i - half];
        }
        ph *= rot;
    }
    return 2.0 * std::abs(acc) / wsum;
}

struct Response {
    double peak_db;   // highest gain, dB re the input (both have unity DC gain)
    double cut_hz;    // -3 dB cutoff above the peak, kTop if none below kTop
};

// Peak: a 1/24-octave grid from fc/4 to 4 fc, the best grid point refined by
// golden-section search between its neighbours. Cutoff: the first grid step
// above the peak whose gain falls below 1/sqrt(2), refined by bisection.
Response measure(float fc, float res, bool single) {
    const double step = std::pow(2.0, 1.0 / 24.0);
    const double lo = std::max(40.0, fc / 4.0);
    std::vector<double> hz, g;
    for (double h = lo; h < kTop && (h <= 4.0 * fc || g.empty() || g.back() >= std::sqrt(0.5)); h *= step) {
        hz.push_back(h);
        g.push_back(gain_at(fc, res, single, h));
    }
    size_t ip = 0;
    for (size_t i = 1; i < g.size(); ++i)
        if (g[i] > g[ip]) ip = i;
    double peak = g[ip];
    if (ip > 0 && ip + 1 < g.size()) {
        double a = hz[ip - 1], b = hz[ip + 1];
        const double r = 0.5 * (std::sqrt(5.0) - 1.0);
        for (int it = 0; it < 24; ++it) {
            const double c = b - r * (b - a), d = a + r * (b - a);
            const double gc = gain_at(fc, res, single, c), gd = gain_at(fc, res, single, d);
            if (gc > gd) b = d; else a = c;
            peak = std::max(peak, std::max(gc, gd));
        }
    }
    const double thr = std::sqrt(0.5);
    double cut = kTop;
    for (size_t i = ip; i + 1 < g.size(); ++i) {
        if (g[i] >= thr && g[i + 1] < thr) {
            double a = hz[i], b = hz[i + 1];
            for (int it = 0; it < 24; ++it) {
                const double m = std::sqrt(a * b);
                if (gain_at(fc, res, single, m) >= thr) a = m; else b = m;
            }
            cut = std::sqrt(a * b);
            break;
        }
    }
    return { 20.0 * std::log10(peak), cut };
}

const float kRes[] = { 0.f, 0.5f, 0.95f };
const float kFc[]  = { 100.f, 200.f, 500.f, 1000.f, 2000.f, 4000.f, 6000.f,
                       8000.f, 10000.f, 12000.f, 13000.f, 14000.f };

struct Point { float res, fc; Response two, one; };

// Measured once, read by both the cutoff and the peak case.
const std::vector<Point>& table() {
    static const std::vector<Point> t = [] {
        std::vector<Point> v;
        for (float res : kRes)
            for (float fc : kFc)
                v.push_back({ res, fc, measure(fc, res, false), measure(fc, res, true) });
        if (print_on())
            for (const Point& p : v)
                std::printf("V6 res %.2f fc %6.0f | two-pass peak %7.3f dB cut %8.1f Hz"
                            " | one-pass peak %7.3f dB cut %8.1f Hz | dpeak %+.4f dB dcut %+.4f %%\n",
                            p.res, p.fc, p.two.peak_db, p.two.cut_hz, p.one.peak_db, p.one.cut_hz,
                            p.one.peak_db - p.two.peak_db, 100.0 * (p.one.cut_hz / p.two.cut_hz - 1.0));
        return v;
    }();
    return t;
}

uint32_t xorshift(uint32_t& s) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}
float noise(uint32_t& s) {
    return static_cast<float>(static_cast<int32_t>(xorshift(s))) * (1.f / 2147483648.f);
}

} // namespace

TEST_CASE("V6: single-pass cutoff within 3 % of two-pass") {
    for (const Point& p : table()) {
        CAPTURE(p.res);
        CAPTURE(p.fc);
        CAPTURE(p.two.cut_hz);
        CAPTURE(p.one.cut_hz);
        if (p.two.cut_hz >= kTop)
            CHECK(p.one.cut_hz >= kTop);
        else
            CHECK(std::fabs(p.one.cut_hz / p.two.cut_hz - 1.0) <= 0.03);
    }
    // Not vacuous: the grid holds cutoffs from the bottom to the top, and the
    // resonant ones sit above their fc while res 0 sits below it.
    int below_top = 0;
    for (const Point& p : table()) below_top += p.two.cut_hz < kTop ? 1 : 0;
    CHECK(below_top >= 30);
}

TEST_CASE("V6: single-pass resonant peak within 1 dB of two-pass") {
    for (const Point& p : table()) {
        CAPTURE(p.res);
        CAPTURE(p.fc);
        CAPTURE(p.two.peak_db);
        CAPTURE(p.one.peak_db);
        CHECK(std::fabs(p.one.peak_db - p.two.peak_db) <= 1.0);
    }
    // Not vacuous: at res 0.95 the reference does resonate (well over 20 dB).
    for (const Point& p : table())
        if (p.res == 0.95f && p.fc >= 1000.f) CHECK(p.two.peak_db > 20.0);
}

TEST_CASE("V6: single-pass stable at res 0.95, cutoff 14 kHz") {
    spky::SvfLp two, one;
    two.Init(kSr); one.Init(kSr);
    two.SetFreq(14000.f); one.SetFreq(14000.f);
    two.SetRes(0.95f);    one.SetRes(0.95f);
    one.SetSinglePass(true);
    uint32_t s = 2463534242u;
    double max_two = 0.0, max_one = 0.0;
    bool finite = true;
    for (int i = 0; i < 10 * 48000; ++i) {   // 10 s of full-scale white noise
        const float x = noise(s);
        two.Process(x);
        one.ProcessSingle(x);
        finite = finite && std::isfinite(one.Low());
        max_two = std::max(max_two, (double)std::fabs(two.Low()));
        max_one = std::max(max_one, (double)std::fabs(one.Low()));
    }
    CHECK(finite);
    CAPTURE(max_two);
    CAPTURE(max_one);
    CHECK(max_one <= 1.25 * max_two);
    // ... and it rings out: 1 s of silence later it is gone.
    double tail = 0.0;
    for (int i = 0; i < 48000; ++i) {
        one.ProcessSingle(0.f);
        if (i >= 47520) tail = std::max(tail, (double)std::fabs(one.Low()));
    }
    CHECK(tail < 1e-6);
}

TEST_CASE("V6: single-pass stable under control-rate cutoff modulation") {
    // The voice's ranges (synth/voice.cpp): cutoff 20 Hz .. 0.3 sr, resonance
    // 0 .. 0.95, both pushed once per 96-sample tick. One tick in four sets
    // resonance exactly 0: above about 13 kHz that is where the two-pass
    // filter's damping clamp makes one of its modes unobservable, the case
    // the one-pass coefficients have to survive.
    spky::SvfLp two, one;
    two.Init(kSr); one.Init(kSr);
    one.SetSinglePass(true);
    uint32_t s = 0x9e3779b9u, m = 0x7f4a7c15u;
    double max_two = 0.0, max_one = 0.0;
    double sig = 0.0, diff = 0.0;   // energies of the two-pass output and of the gap
    bool finite = true;
    for (int i = 0; i < 20 * 48000; ++i) {
        if (i % 96 == 0) {
            const float u = 0.5f + 0.5f * noise(m);
            const float fc = 20.f * std::pow(0.3f * kSr / 20.f, u);
            const float res = (xorshift(m) & 3u) == 0u ? 0.f : 0.475f + 0.475f * noise(m);
            two.SetFreq(fc); one.SetFreq(fc);
            two.SetRes(res); one.SetRes(res);
        }
        const float x = noise(s);
        two.Process(x);
        one.ProcessSingle(x);
        finite = finite && std::isfinite(one.Low());
        max_two = std::max(max_two, (double)std::fabs(two.Low()));
        max_one = std::max(max_one, (double)std::fabs(one.Low()));
        const double a = two.Low(), b = one.Low();
        sig += a * a;
        diff += (a - b) * (a - b);
    }
    CHECK(finite);
    CAPTURE(max_two);
    CAPTURE(max_one);
    CHECK(max_one <= 1.5 * max_two);
    // The one-pass step is the two-pass filter's own recurrence on the same
    // state, so under modulation too only rounding separates them: the
    // difference stays below -80 dB of the signal.
    CAPTURE(sig);
    CAPTURE(diff);
    CHECK(sig > 1.0);
    CHECK(diff < 1e-8 * sig);
    // Parked in that degenerate corner (res 0, 14 kHz) and fed silence, it
    // must not hold a value: whatever it carried decays away.
    one.SetFreq(14000.f);
    one.SetRes(0.f);
    double tail = 0.0;
    for (int i = 0; i < 24000; ++i) {
        one.ProcessSingle(0.f);
        if (i >= 23904) tail = std::max(tail, (double)std::fabs(one.Low()));
    }
    CHECK(tail < 1e-6);
}

TEST_CASE("V6: single-pass output finite at the parameter extremes") {
    const float fcs[]  = { 0.f, 1e-6f, 20.f, 0.3f * kSr, kSr / 3.f, 1e9f };
    const float ress[] = { 0.f, 0.95f, 1.f };
    for (float fc : fcs)
        for (float res : ress) {
            CAPTURE(fc);
            CAPTURE(res);
            spky::SvfLp one;
            one.Init(kSr);
            one.SetSinglePass(true);
            one.SetFreq(fc);
            one.SetRes(res);
            uint32_t s = 12345u;
            bool finite = true;
            for (int i = 0; i < 48000; ++i) {
                one.ProcessSingle(noise(s));
                finite = finite && std::isfinite(one.Low());
            }
            CHECK(finite);
        }
}

TEST_CASE("V6: a SHELL_LEAN_SVF1 voice filters in one pass") {
    spky::Voice v;
    v.init(kSr, 7u);
    CHECK(v.svf_single_pass());
}
