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
