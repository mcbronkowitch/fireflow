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

TEST_CASE("tape bpf: an impulse tail never enters the subnormal range") {
    // Mutation caught: removing either recursive-state denormal floor lets
    // the impulse response decay into FP_SUBNORMAL instead of exact zero.
    // A nonzero response plus zero subnormals is derived from the filter and
    // real-time contracts, not from its coefficient or state implementation.
    TapeBpf bpf;
    bpf.Init(48000.f);
    float peak = 0.f;
    int subnormals = 0;
    for (int i = 0; i < 20000; ++i) {
        const float y = bpf.Process(i == 0 ? 1.f : 0.f);
        peak = std::max(peak, std::fabs(y));
        if (std::fpclassify(y) == FP_SUBNORMAL) ++subnormals;
    }
    CHECK(peak > 0.f);
    CHECK(subnormals == 0);
}

TEST_CASE("tape echo: feedback blooms but remains finite and bounded") {
    // Mutations caught: bypassing the saturator breaks the independent
    // fast_tanh |y| <= 1 ceiling; omitting the feedback write makes the later
    // energy bit-identical to the zero-feedback control.
    static float feedback_mem[2048];
    static float zero_mem[2048];
    TapeEcho<2048> feedback;
    TapeEcho<2048> zero;
    feedback.Init(48000.f, feedback_mem);
    zero.Init(48000.f, zero_mem);
    feedback.SetFeedback(1.2f);
    zero.SetFeedback(0.f);
    float peak = 0.f;
    double feedback_tail_energy = 0.0;
    double zero_tail_energy = 0.0;
    for (int i = 0; i < 48000; ++i) {
        const float in = i == 0 ? 1.f : 0.f;
        const float feedback_y = feedback.Process(in, 512.f);
        const float zero_y = zero.Process(in, 512.f);
        REQUIRE(std::isfinite(feedback_y));
        REQUIRE(std::isfinite(zero_y));
        peak = std::max(peak, std::fabs(feedback_y));
        // The input's first delayed response starts at sample 512. Starting
        // this window at 2 * 512 isolates the first possible recirculation;
        // the zero-feedback echo supplies the independently generated control.
        if (i >= 1024 && i < 2048) {
            feedback_tail_energy += static_cast<double>(feedback_y) * feedback_y;
            zero_tail_energy += static_cast<double>(zero_y) * zero_y;
        }
    }
    CHECK(peak <= 1.f);
    CHECK(feedback_tail_energy > zero_tail_energy);
}
