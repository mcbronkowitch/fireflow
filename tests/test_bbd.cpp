#include <doctest/doctest.h>
#include <cmath>
#include "fx/bbd.h"
using namespace spky;

TEST_CASE("bbd_clock_hz: f_clk = stages / (2 * t_d)") {
    // 8192 stages at 250 ms -> 16384 Hz. The DMM's own numbers: 8192 stages
    // at 550 ms -> 7447 Hz, against the EH-7850 factory calibration document's
    // measured clock period of 120-140 us (7143-8333 Hz) at maximum delay.
    CHECK(bbd_clock_hz(0.25f, 8192) == doctest::Approx(16384.f));
    CHECK(bbd_clock_hz(0.550f, 8192) == doctest::Approx(7447.27f).epsilon(0.001));
    CHECK(bbd_clock_hz(1.0f, 4096) == doctest::Approx(2048.f));
}

TEST_CASE("bbd_clock_hz: the 32 kHz ceiling is hard") {
    // At 8192 stages the ceiling engages below 128 ms -- exactly where the
    // fixed post-BBD filter chain dominates anyway (spec "The clock law").
    CHECK(bbd_clock_hz(0.128f, 8192) == doctest::Approx(32000.f));
    CHECK(bbd_clock_hz(0.001f, 16384) == bbd_tuning::kClockMaxHz);
    CHECK(bbd_clock_hz(1e-9f, 16384) == bbd_tuning::kClockMaxHz);
}

TEST_CASE("bbd_clock_hz: there is no floor, and no way to return garbage") {
    // The mud at the long end is the point -- a 10 s delay at 512 stages runs
    // the line at 25.6 Hz and that is allowed. What is NOT allowed is a
    // non-finite or negative clock reaching BbdLine.
    CHECK(bbd_clock_hz(10.f, 512) == doctest::Approx(25.6f));
    CHECK(bbd_clock_hz(0.f, 8192) == bbd_tuning::kClockMaxHz);
    CHECK(bbd_clock_hz(-1.f, 8192) == bbd_tuning::kClockMaxHz);
    CHECK(std::isfinite(bbd_clock_hz(NAN, 8192)));
}

TEST_CASE("bbd_tuning: the ceiling buys twice the bandwidth the chain needs") {
    // 32 kHz / 4 = 8 kHz of BBD bandwidth against a fixed filter chain at
    // ~3.6 kHz: inaudible by construction, and it bounds the worst case at
    // 2 * 32000 / 48000 = 1.33 ticks per sample.
    CHECK(bbd_tuning::kClockMaxHz * 0.25f > 2.f * bbd_tuning::kFilterHz);
    CHECK(2.f * bbd_tuning::kClockMaxHz / 48000.f < 1.5f);
}

// --- the fixed filter chain -------------------------------------------------
// The analog spec is H(s) = sum_m R[m] / (s - P[m]). These tests pin that it
// really is a 3rd-order Butterworth at kFilterHz, because every claim the
// model makes downstream ("the filters do not move", "long delays are dark
// because of the CLOCK, not the filters") rests on this chain being the one
// the DMM has and staying where it is put.

static float analog_mag_db(bool out_kind, float hz) {
    Cf poles[bbd_tuning::kFiltOrder], res[bbd_tuning::kFiltOrder];
    bbd_analog_spec(out_kind, poles, res);
    const Cf s{ 0.f, TWO_PI * hz };
    Cf h{ 0.f, 0.f };
    for (int m = 0; m < bbd_tuning::kFiltOrder; ++m)
        h = cf_add(h, cf_div(res[m], Cf{ s.re - poles[m].re, s.im - poles[m].im }));
    return 20.f * std::log10(std::sqrt(h.re * h.re + h.im * h.im));
}

TEST_CASE("bbd filter: DC gain is exactly unity") {
    // H(0) = sum(-R/P) == 1. If this drifts, every level in the chain drifts
    // with it and the compander's reference stops meaning anything.
    CHECK(analog_mag_db(false, 0.f) == doctest::Approx(0.f).epsilon(0.001));
    CHECK(analog_mag_db(true,  0.f) == doctest::Approx(0.f).epsilon(0.001));
}

TEST_CASE("bbd filter: -3 dB at kFilterHz, -18 dB/oct above it") {
    CHECK(analog_mag_db(false, bbd_tuning::kFilterHz)
          == doctest::Approx(-3.0103f).epsilon(0.01));
    // Butterworth order 3: one octave up is -18 dB, two octaves -36 dB
    // (asymptotically). Generous windows -- the shape is what is pinned.
    const float oct1 = analog_mag_db(false, 2.f * bbd_tuning::kFilterHz);
    const float oct2 = analog_mag_db(false, 4.f * bbd_tuning::kFilterHz);
    CHECK(oct1 < -16.f);
    CHECK(oct1 > -22.f);
    CHECK(oct2 - oct1 < -16.f);
    CHECK(oct2 - oct1 > -20.f);
}

TEST_CASE("bbd filter: every pole is in the left half plane") {
    // A pole with Re >= 0 makes P[m] = exp(ts*p) leave the unit disc and the
    // parallel branches diverge -- silently, over seconds. This is the guard
    // that a hand-edited kFilterHz or kFiltOrder cannot get past.
    for (bool out_kind : { false, true }) {
        Cf poles[bbd_tuning::kFiltOrder], res[bbd_tuning::kFiltOrder];
        bbd_analog_spec(out_kind, poles, res);
        for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
            CHECK(poles[m].re < 0.f);
            CHECK(std::isfinite(res[m].re));
            CHECK(std::isfinite(res[m].im));
        }
    }
}

TEST_CASE("bbd filter: the discretised poles sit strictly inside the unit disc") {
    const BbdFilterCoef& fin = bbd_filter_in(48000.f);
    const BbdFilterCoef& fout = bbd_filter_out(48000.f);
    for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
        const float rin = std::sqrt(fin.P[m].re * fin.P[m].re + fin.P[m].im * fin.P[m].im);
        const float rout = std::sqrt(fout.P[m].re * fout.P[m].re + fout.P[m].im * fout.P[m].im);
        CHECK(rin < 1.f);
        CHECK(rout < 1.f);
        CHECK(rin > 0.f);
    }
    // H is the output filter's DC feed-through term, sum(-R/P) == H(0) == 1.
    CHECK(fout.H == doctest::Approx(1.f).epsilon(0.001));
}

TEST_CASE("bbd filter: interpolate_g matches the table at both endpoints") {
    // d == 0 and d == 1 must hit rows 0 and N-1 exactly, not one row short.
    // An off-by-one here shows up as a faint, clock-rate-dependent whine that
    // is very hard to attribute later.
    const BbdFilterCoef& f = bbd_filter_in(48000.f);
    Cf g[bbd_tuning::kFiltOrder];
    f.interpolate_g(0.f, g);
    for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
        CHECK(g[m].re == doctest::Approx(f.G[0][m].re));
        CHECK(g[m].im == doctest::Approx(f.G[0][m].im));
    }
    f.interpolate_g(1.f, g);
    for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
        CHECK(g[m].re == doctest::Approx(f.G[bbd_tuning::kInterpSteps - 1][m].re));
        CHECK(g[m].im == doctest::Approx(f.G[bbd_tuning::kInterpSteps - 1][m].im));
    }
    // And it must not read past the table for d slightly out of range.
    f.interpolate_g(1.0001f, g);
    CHECK(std::isfinite(g[0].re));
}

TEST_CASE("bbd filter: the table is built once per sample rate") {
    // Four lines call this at init. Rebuilding the 3 kB of tables four times
    // would be harmless but silly; rebuilding them from a DIFFERENT sample
    // rate and handing the stale result to the other lines would not be.
    const BbdFilterCoef& a = bbd_filter_in(48000.f);
    const BbdFilterCoef& b = bbd_filter_in(48000.f);
    CHECK(&a == &b);
    const BbdFilterCoef& c = bbd_filter_in(44100.f);
    CHECK(&c == &a);                       // same storage, rebuilt in place
    CHECK(c.P[0].re != doctest::Approx(0.f));
    // Put it back so later cases in this file see 48 kHz tables.
    bbd_filter_in(48000.f);
    bbd_filter_out(48000.f);
}
