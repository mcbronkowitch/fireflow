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

TEST_CASE("injected delay: reset leaves the arena alone and reads as cleared") {
    // Fault R2 (docs/hardware/reva-bring-up.md): FLUX's switch-off flush ran
    // Reset() inside the audio callback, and clearing 2 x 1 MB there cost
    // ~6.3 M cycles on Rev A. Reset() must not touch the arena; every cell not
    // written since must still read as zero, at every delay.
    float mem[16];
    std::fill(std::begin(mem), std::end(mem), 1.f);
    InjectedDelayLine<float, 16> d;
    d.Init(mem);
    bool untouched = true;
    for (float x : mem) untouched = untouched && (x == 1.f);
    CHECK(untouched);

    // Write a few samples, reset, and walk every delay as the line refills:
    // only what was written since the reset may come back.
    for (int i = 0; i < 40; ++i) d.Write(0.5f);
    d.Reset();
    for (int n = 0; n < 20; ++n) {
        // Up to 13.5: SetDelay clamps at MaxSize - 2 = 14 whole samples.
        for (int delay = 1; delay <= 13; ++delay) {
            d.SetDelay(static_cast<float>(delay) + 0.5f);
            // Cells delay and delay+1 back; written iff not further back than n.
            const float a = (delay <= n) ? 0.25f : 0.f;
            const float b = (delay + 1 <= n) ? 0.25f : 0.f;
            CAPTURE(n);
            CAPTURE(delay);
            CHECK(d.Read() == doctest::Approx(0.5f * (a + b)));
        }
        d.Write(0.25f);
    }
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
