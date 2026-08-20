// tests/test_onepole_hp.cpp
//
// The audio-path high-pass Tasks 4-7 of the DPTH/EDGE plan build their cells
// on. Until one of them lands nothing includes engine/util/onepole_hp.h, so
// without this file the header is not compiled by the repo build at all and a
// syntax error in it would ship.
//
// What is pinned here is what the header CLAIMS, measured rather than
// asserted: the corner is a real -3 dB point, a corner above 0 removes DC,
// and the bottom rail does not (it is a bypass on paper -- see the header for
// why that is not a bit-exact bypass in float32).
#include <doctest/doctest.h>
#include "util/onepole_hp.h"
#include <cmath>
#include <initializer_list>   // this toolchain does not get it transitively

using namespace spky;

namespace {

constexpr double kSr  = 48000.0;
constexpr double kTwoPi = 6.283185307179586;
// RMS of a unit-amplitude sine, i.e. the reading a transparent filter gives.
constexpr double kRef = 0.7071067811865476;

// Steady-state RMS of a sine at `f` through a fresh filter cornered at `hz`.
// The first half of the window is discarded so the filter's own settling does
// not enter the reading.
double rms_at(float hz, double f) {
    OnePoleHp hp;
    hp.init(float(kSr));
    hp.set_hz(hz);
    const int n = 48000;
    double acc = 0.0;
    for (int i = 0; i < n; ++i) {
        const float y = hp.process(float(std::sin(kTwoPi * f * i / kSr)));
        if (i >= n / 2) acc += double(y) * y;
    }
    return std::sqrt(acc / (n / 2));
}

double db(double x) { return 20.0 * std::log10(x / kRef); }

// What is left of a held DC step after one second.
double dc_after_1s(float hz) {
    OnePoleHp hp;
    hp.init(float(kSr));
    hp.set_hz(hz);
    float y = 0.f;
    for (int i = 0; i < int(kSr); ++i) y = hp.process(1.f);
    return double(y);
}

}  // namespace

TEST_CASE("onepole hp: the corner is a real -3 dB point") {
    // Two corners two decades apart, so this cannot pass on one coincidence.
    // +/- 0.2 dB is tight enough to fail on a coefficient that is off by a
    // factor of 2*pi -- the classic way to get this formula wrong -- which
    // would read -0.08 dB at 20 Hz instead of -3.
    for (float hz : { 20.f, 200.f }) {
        CAPTURE(hz);
        CHECK(db(rms_at(hz, double(hz))) == doctest::Approx(-3.0).epsilon(0.07));
    }
}

TEST_CASE("onepole hp: it passes what is above the corner and cuts what is below") {
    // At a 20 Hz corner an audio band is essentially untouched...
    CHECK(db(rms_at(20.f, 1000.0)) > -0.05);
    CHECK(db(rms_at(20.f, 100.0))  > -0.5);
    // ...and moving the corner up puts the same 100 Hz well down.
    CHECK(db(rms_at(200.f, 100.0)) < -6.0);
    CHECK(db(rms_at(2000.f, 100.0)) < -20.0);
    // Monotone in the corner, which a sign error in the coefficient breaks.
    CHECK(rms_at(2000.f, 100.0) < rms_at(200.f, 100.0));
    CHECK(rms_at(200.f, 100.0)  < rms_at(20.f, 100.0));
}

TEST_CASE("onepole hp: a corner above 0 removes DC, and the bottom rail does not") {
    // This is the trap the header warns Tasks 4-7 about. At set_hz(0) the
    // recursion telescopes to y == x, so a held step comes out HELD: the
    // bottom rail is not a DC blocker, and an engine that wants a true
    // neutral must bypass process() rather than lower the corner.
    CHECK(dc_after_1s(0.f) == doctest::Approx(1.0));
    CHECK(std::fabs(dc_after_1s(20.f)) < 1e-3);
    CHECK(std::fabs(dc_after_1s(200.f)) < 1e-3);
}

TEST_CASE("onepole hp: init leaves the corner at 20 Hz, not at 0") {
    // Documented in the header because it is a silent trap: an engine whose
    // neutral is "the bottom rail" and which never calls set_hz still filters.
    OnePoleHp hp;
    hp.init(float(kSr));
    float y = 0.f;
    for (int i = 0; i < int(kSr); ++i) y = hp.process(1.f);
    CHECK(std::fabs(double(y)) < 1e-3);        // DC gone => the corner is not 0
}
