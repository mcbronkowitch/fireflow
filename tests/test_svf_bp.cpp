#include "doctest/doctest.h"
#include "util/svf_bp.h"
#include <cmath>

using namespace spky;

// Drive one mode with an impulse and confirm it rings at the tuned frequency.
TEST_CASE("SvfBp rings at the frequency its coefficients encode") {
    SvfBp<4> bank;
    bank.reset();

    // 1 kHz at 48 kHz, Q = 40, expressed the way ModeBank will express it.
    const float f  = 1000.f / 48000.f;
    const float q  = 40.f;
    const float g  = std::tan(3.14159265f * f);
    const float r  = 1.f / q;
    const float h  = 1.f / (1.f + r * g + g * g);
    bank.set_coeffs(0, g, r + g, h);
    for (int i = 1; i < 4; ++i) bank.set_coeffs(i, 0.f, 0.f, 1.f);

    const float gain[4] = { 1.f, 0.f, 0.f, 0.f };

    // Impulse, then count zero crossings over 48000 samples (1 s).
    int   crossings = 0;
    float prev = bank.process(gain, 1.f);
    for (int i = 1; i < 48000; ++i) {
        const float s = bank.process(gain, 0.f);
        if ((prev < 0.f) != (s < 0.f)) ++crossings;
        prev = s;
    }
    // Two crossings per cycle; allow 2 % for the ring decaying into noise.
    CHECK(crossings > 1960);
    CHECK(crossings < 2040);
}

TEST_CASE("SvfBp is silent with zero gains and stays finite") {
    SvfBp<4> bank;
    bank.reset();
    for (int i = 0; i < 4; ++i) bank.set_coeffs(i, 0.5f, 0.6f, 0.7f);
    const float gain[4] = { 0.f, 0.f, 0.f, 0.f };
    for (int i = 0; i < 1000; ++i) CHECK(bank.process(gain, 1.f) == 0.f);
}

TEST_CASE("SvfBp reset clears state") {
    SvfBp<4> bank;
    bank.reset();
    for (int i = 0; i < 4; ++i) bank.set_coeffs(i, 0.2f, 0.3f, 0.9f);
    const float gain[4] = { 1.f, 1.f, 1.f, 1.f };
    for (int i = 0; i < 100; ++i) bank.process(gain, 1.f);
    const float ringing = bank.process(gain, 0.f);
    CHECK(std::fabs(ringing) > 0.f);
    bank.reset();
    CHECK(bank.process(gain, 0.f) == 0.f);
}

TEST_CASE("SvfBp has zero DC gain (band-pass, not low-pass)") {
    SvfBp<4> bank;
    bank.reset();

    // 1 kHz at 48 kHz, Q = 40.
    const float f  = 1000.f / 48000.f;
    const float q  = 40.f;
    const float g  = std::tan(3.14159265f * f);
    const float r  = 1.f / q;
    const float h  = 1.f / (1.f + r * g + g * g);
    bank.set_coeffs(0, g, r + g, h);
    for (int i = 1; i < 4; ++i) bank.set_coeffs(i, 0.f, 0.f, 1.f);

    const float gain[4] = { 1.f, 0.f, 0.f, 0.f };

    // Feed constant DC (1.0) for 10000 samples to allow filter to settle.
    for (int i = 0; i < 10000; ++i) bank.process(gain, 1.f);

    // DC gain of band-pass is zero; output should be negligible.
    // Low-pass would have unity DC gain, so output would be ~1.0.
    const float dc_response = bank.process(gain, 1.f);
    CHECK(std::fabs(dc_response) < 0.01f);
}
