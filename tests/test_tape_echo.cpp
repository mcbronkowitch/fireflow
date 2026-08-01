#include <doctest/doctest.h>
#include "fx/tape_echo.h"
#include <algorithm>
#include <cmath>

using namespace spky;

TEST_CASE("injected delay: fractional reads interpolate host memory") {
    // Mutation caught: dropping the fractional lerp would produce one full
    // impulse instead of the independently derived half + half response.
    float mem[16] = {};
    InjectedDelayLine<float, 16> d;
    d.Init(mem);
    d.SetDelay(2.5f);
    float y[6] = {};
    for (int i = 0; i < 6; ++i) {
        y[i] = d.Read();
        d.Write(i == 0 ? 1.f : 0.f);
    }
    CHECK(y[2] == doctest::Approx(0.5f));
    CHECK(y[3] == doctest::Approx(0.5f));
}

TEST_CASE("injected delay: reset clears exactly the injected arena") {
    // Mutation caught: omitting Reset from Init leaves the caller's arena
    // dirty. Zero is independent of the implementation's clearing method.
    float mem[16];
    std::fill(std::begin(mem), std::end(mem), 1.f);
    InjectedDelayLine<float, 16> d;
    d.Init(mem);
    for (float x : mem) CHECK(x == 0.f);
}

TEST_CASE("tape time target spans four octaves without per-call libm") {
    // Mutation caught: wrong exponent span, neutral point, or input clamp.
    // The literals are the hand-derived endpoints and midpoint of x1/4..x4.
    CHECK(tape_time_mult(0.f) == doctest::Approx(0.25f));
    CHECK(tape_time_mult(0.5f) == doctest::Approx(1.f));
    CHECK(tape_time_mult(1.f) == doctest::Approx(4.f));
    CHECK(tape_time_mult(-1.f) == doctest::Approx(0.25f));
    CHECK(tape_time_mult(2.f) == doctest::Approx(4.f));
}

TEST_CASE("tape echo: feedback blooms but remains finite and bounded") {
    // Mutation caught: bypassing the saturator permits feedback > 1 to grow
    // without bound. The ceiling follows from the limiter's public |y| <= 1
    // contract, independently of the echo's feedback implementation.
    static float mem[1024];
    TapeEcho<1024> e;
    e.Init(48000.f, mem);
    e.SetFeedback(1.2f);
    float peak = 0.f;
    for (int i = 0; i < 48000; ++i) {
        const float y = e.Process(i == 0 ? 1.f : 0.f, 240.f);
        REQUIRE(std::isfinite(y));
        peak = std::max(peak, std::fabs(y));
    }
    CHECK(peak <= 1.f);
}
