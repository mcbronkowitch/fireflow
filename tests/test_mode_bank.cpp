#include "doctest/doctest.h"
#include "body/mode_bank.h"
#include <cmath>
#include <vector>

using namespace spky;

static int zero_crossings(const std::vector<float>& v) {
    int n = 0;
    for (size_t i = 1; i < v.size(); ++i)
        if ((v[i - 1] < 0.f) != (v[i] < 0.f)) ++n;
    return n;
}

static std::vector<float> strike(ModeBank& b, int samples) {
    std::vector<float> out(samples);
    out[0] = b.process(1.f);
    for (int i = 1; i < samples; ++i) out[i] = b.process(0.f);
    return out;
}

TEST_CASE("ModeBank fundamental tracks the requested pitch") {
    ModeBank b;
    b.init(48000.f);
    // stretch 0 => harmonic; the fundamental dominates.
    b.set_params(220.f, 0.f, 0.5f, 0.2f);
    const auto v = strike(b, 48000);
    // 220 Hz => ~440 crossings/s. Allow 5 % for higher modes colouring it.
    CHECK(zero_crossings(v) > 418);
    CHECK(zero_crossings(v) < 462);
}

TEST_CASE("ModeBank damping sets ring time") {
    ModeBank tight, ringing;
    tight.init(48000.f);
    ringing.init(48000.f);
    tight.set_params(220.f, 0.f, 0.f, 0.5f);
    ringing.set_params(220.f, 0.f, 1.f, 0.5f);

    auto energy_at = [](ModeBank& b, int n) {
        strike(b, n);
        float e = 0.f;
        for (int i = 0; i < 4800; ++i) { const float s = b.process(0.f); e += s * s; }
        return e;
    };
    CHECK(energy_at(ringing, 24000) > energy_at(tight, 24000));
}

TEST_CASE("ModeBank stretch makes the partials inharmonic") {
    ModeBank harmonic, stretched;
    harmonic.init(48000.f);
    stretched.init(48000.f);
    harmonic.set_params(220.f, 0.f, 0.8f, 0.8f);
    stretched.set_params(220.f, 1.f, 0.8f, 0.8f);
    // Full 8192-sample window, as in the brief. With the corrected two-
    // accumulator ladder (harmonic * stretch_factor, not one additive sum),
    // stretch scales with the harmonic index -- mode 23's stretch_factor
    // reaches ~202x at stiffness 0.4 -- so the stretched bank's partials sit
    // far enough above the harmonic series that they dominate the crossing
    // count at every window size from 32 samples out past 8192 (measured:
    // harm=1802, stretch=2183 at 8192; margin stays positive and grows
    // monotonically at every checkpoint in between). No crossover exists, so
    // no early window is needed.
    const auto a = strike(harmonic, 8192);
    const auto b = strike(stretched, 8192);
    bool differs = false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) differs = true;
    CHECK(differs);
    // Stretched partials sit higher, so the waveform crosses zero more often.
    CHECK(zero_crossings(b) > zero_crossings(a));
}

TEST_CASE("ModeBank recomputes coefficients only when parameters move") {
    ModeBank b;
    b.init(48000.f);
    b.set_params(220.f, 0.2f, 0.5f, 0.5f);
    const uint32_t after_first = b.coeff_updates();
    CHECK(after_first == 1);
    for (int i = 0; i < 50; ++i) b.set_params(220.f, 0.2f, 0.5f, 0.5f);
    CHECK(b.coeff_updates() == after_first);
    b.set_params(221.f, 0.2f, 0.5f, 0.5f);
    CHECK(b.coeff_updates() == after_first + 1);
}

TEST_CASE("ModeBank stays finite under extreme settings") {
    ModeBank b;
    b.init(48000.f);
    b.set_params(4000.f, 1.f, 1.f, 1.f);
    for (int i = 0; i < 96000; ++i) {
        const float s = b.process(i % 96 == 0 ? 1.f : 0.f);
        REQUIRE(std::isfinite(s));
    }
}

// --- signed stretch (spec 2026-07-26 body-resonator §5/§7, Task 8b) --------
//
// stretch is now the CHARACTER, -1..+1. The compressed side is new territory:
// stretch_factor descends across the 24 modes instead of climbing, and if it
// ever reaches zero the mode frequencies go negative and tan(kPi*f) stops
// meaning anything. Two separate claims below, and they are separate on
// purpose: (a) the MAPPING must not reach that, and (b) the GUARD must hold
// anyway if a caller ignores the -1..+1 contract. A guard alone would be a
// design that relies on a clamp for its sound.

TEST_CASE("ModeBank compresses the partials without collapsing the ladder") {
    ModeBank b;
    b.init(48000.f);
    for (float stretch : { -1.f, -0.75f, -0.5f, -0.25f, -0.05f }) {
        CAPTURE(stretch);
        b.set_params(220.f, stretch, 0.8f, 0.8f);
        for (int i = 0; i < ModeBank::kModes; ++i) {
            const float f = b.mode_freq(i);
            CAPTURE(i);
            REQUIRE(std::isfinite(f));
            // STRICTLY greater than the floor, not merely non-negative: an f
            // exactly equal to kFMin is the guard having fired, and the whole
            // point of kCompressScale is that the knob never gets there.
            CHECK(f > ModeBank::kFMin);
            CHECK(f <= 0.499f);
        }
        // Margin, stated as something audible rather than as a number copied
        // out of a measurement: 24 partials that no longer span an octave are
        // not a body any more, they are a lump at the fundamental. The top
        // mode sits at 24 * f0 * min_stretch_factor, so this claim fails at a
        // compression scale of about 0.079 -- before the ladder reaches zero
        // at 0.087, and well before anything divides by it.
        CHECK(b.mode_freq(ModeBank::kModes - 1) > 2.f * b.mode_freq(0));
    }
}

TEST_CASE("ModeBank's floor guard holds when the caller ignores the contract") {
    ModeBank b;
    b.init(48000.f);
    // Far past -1: stiffness runs the ladder through zero and out the other
    // side, and at the most absurd values NthHarmonicCompensation's own
    // denominator changes sign, so f0 itself arrives negative or infinite.
    for (float stretch : { -2.f, -5.f, -20.f, -1e6f }) {
        CAPTURE(stretch);
        b.set_params(220.f, stretch, 0.8f, 0.8f);
        for (int i = 0; i < ModeBank::kModes; ++i) {
            const float f = b.mode_freq(i);
            CAPTURE(i);
            REQUIRE(std::isfinite(f));
            CHECK(f >= ModeBank::kFMin);      // >= : here the guard IS the answer
            CHECK(f <= 0.499f);
        }
        b.reset();
        for (int i = 0; i < 24000; ++i) {
            const float s = b.process(i == 0 ? 1.f : 0.f);
            REQUIRE(std::isfinite(s));
        }
    }
}

TEST_CASE("ModeBank's brightness roll-off follows the AMOUNT of stretch") {
    // "Stretch dulls the top" is a roll-off that tracks how inharmonic the
    // body is, not which way. So neither direction may come out BRIGHTER than
    // the harmonic bank at the same FILTER setting.
    //
    // q_loss is not exposed, but it is recoverable: q_i = 1 + f_i * q_base *
    // loss_i with q_base a function of damping alone, so (q_i - 1) / f_i is
    // q_base * loss_i, and the ratio between two banks at equal damping is the
    // ratio of their loss. damping 0 keeps q small, which keeps the r = rg - g
    // recovery well conditioned.
    ModeBank harmonic, compressed, stretched;
    harmonic.init(48000.f);
    compressed.init(48000.f);
    stretched.init(48000.f);
    harmonic.set_params(220.f, 0.f, 0.f, 0.8f);
    compressed.set_params(220.f, -1.f, 0.f, 0.8f);
    stretched.set_params(220.f, 1.f, 0.f, 0.8f);

    auto loss = [](const ModeBank& b, int i) {
        return (b.mode_q(i) - 1.f) / b.mode_freq(i);
    };
    for (int i = 1; i < ModeBank::kModes; ++i) {
        CAPTURE(i);
        const float lh = loss(harmonic, i);
        REQUIRE(lh > 0.f);
        CHECK(loss(compressed, i) <= lh * 1.01f);
        CHECK(loss(stretched,  i) <= lh * 1.01f);
    }
}

TEST_CASE("ModeBank: the two directions are different materials") {
    ModeBank compressed, harmonic, stretched;
    compressed.init(48000.f);
    harmonic.init(48000.f);
    stretched.init(48000.f);
    compressed.set_params(220.f, -1.f, 0.8f, 0.8f);
    harmonic.set_params(220.f, 0.f, 0.8f, 0.8f);
    stretched.set_params(220.f, 1.f, 0.8f, 0.8f);

    // Sign is direction, and direction is what COLOR chooses: the top partial
    // must sit BELOW the harmonic series on one side and ABOVE it on the
    // other. This is the whole claim of "which way the partials stretch".
    const int top = ModeBank::kModes - 1;
    CHECK(compressed.mode_freq(top) < harmonic.mode_freq(top));
    CHECK(stretched.mode_freq(top)  > harmonic.mode_freq(top));

    const auto a = strike(compressed, 4096);
    const auto b = strike(stretched, 4096);
    bool differs = false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) differs = true;
    CHECK(differs);
}
