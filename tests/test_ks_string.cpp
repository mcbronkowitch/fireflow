#include "doctest/doctest.h"

#include "body/ks_string.h"
#include "PhysicalModeling/KarplusString.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

using namespace spky;

namespace {

constexpr float kSr = 48000.f;

// A pluck: a short burst, then silence to ring out into.
std::vector<float> exciter(int n)
{
    std::vector<float> in(static_cast<size_t>(n), 0.f);
    for (int i = 0; i < 64 && i < n; ++i) {
        in[static_cast<size_t>(i)] = (i % 2 == 0 ? 0.5f : -0.5f) * (1.f - i / 64.f);
    }
    return in;
}

struct Params {
    const char* label;
    float       freq_hz;
    float       brightness;
    float       damping;
    float       nonlinearity;
};

// Both nonlinearity branches, and the >= 0.95 damping crossfade to infinite
// decay -- the one path in recompute() that rewrites brightness and
// damping_cutoff after they have already been used once.
const Params kCases[] = {
    { "curved bridge (nonlinearity 0)", 220.f, 0.7f, 0.7f, 0.0f },
    { "dispersion (nonlinearity 0.4)",  220.f, 0.7f, 0.7f, 0.4f },
    { "dispersion with noise (0.9)",    110.f, 0.4f, 0.5f, 0.9f },
    { "infinite decay (damping 0.97)",  440.f, 0.6f, 0.97f, 0.2f },
};

} // namespace

// The whole point of the port: the same DSP on a different schedule. With the
// parameters held still there is no schedule difference left to observe, so
// the two must agree exactly. Anything less than exact equality here means the
// parameter block was not transcribed faithfully.
TEST_CASE("KsString matches daisysp::String sample for sample, parameters held")
{
    constexpr int n  = 4096;
    const auto    in = exciter(n);

    for (const Params& p : kCases) {
        INFO("case: " << std::string(p.label));

        // rand() drives the dispersion noise, and both implementations draw
        // from it once per sample. Reseed before each so they see the same
        // sequence -- otherwise this test would be comparing two different
        // noise realisations and could never pass on the dispersion cases.
        std::srand(20260726u);
        daisysp::String reference;
        reference.Init(kSr);
        reference.SetFreq(p.freq_hz);
        reference.SetBrightness(p.brightness);
        reference.SetDamping(p.damping);
        reference.SetNonLinearity(p.nonlinearity);
        std::vector<float> want(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            want[static_cast<size_t>(i)] = reference.Process(in[static_cast<size_t>(i)]);

        std::srand(20260726u);
        KsString port;
        port.init(kSr);
        port.set_params(p.freq_hz, p.brightness, p.damping, p.nonlinearity);
        std::vector<float> got(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            got[static_cast<size_t>(i)] = port.process(in[static_cast<size_t>(i)]);

        int    mismatches = 0;
        int    first      = -1;
        double energy     = 0.0;
        float  first_want = 0.f, first_got = 0.f;
        for (int i = 0; i < n; ++i) {
            energy += std::abs(static_cast<double>(want[static_cast<size_t>(i)]));
            if (want[static_cast<size_t>(i)] != got[static_cast<size_t>(i)]) {
                if (first < 0) {
                    first      = i;
                    first_want = want[static_cast<size_t>(i)];
                    first_got  = got[static_cast<size_t>(i)];
                }
                ++mismatches;
            }
        }
        CAPTURE(first);
        CAPTURE(first_want);
        CAPTURE(first_got);
        CHECK(mismatches == 0);
        // Guards the guard: a string that never rang would match trivially.
        CHECK(energy > 1.0);
    }
}

// The reason the port exists. If process() recomputed anything, the cost the
// bench measured would come straight back.
TEST_CASE("KsString recomputes only on a real parameter change")
{
    KsString s;
    s.init(kSr);
    s.set_params(220.f, 0.7f, 0.7f, 0.4f);
    const uint32_t after_setup = s.coeff_updates();

    for (int i = 0; i < 1000; ++i) s.process(0.f);
    CHECK(s.coeff_updates() == after_setup);

    // Same values again: the dirty check must swallow them.
    for (int i = 0; i < 10; ++i) s.set_params(220.f, 0.7f, 0.7f, 0.4f);
    CHECK(s.coeff_updates() == after_setup);

    // Each of the four arguments has to be able to trigger a recompute on its
    // own -- a dirty check that only watched frequency would pass a test that
    // moved frequency alone.
    s.set_params(221.f, 0.7f, 0.7f, 0.4f);
    CHECK(s.coeff_updates() == after_setup + 1);
    s.set_params(221.f, 0.8f, 0.7f, 0.4f);
    CHECK(s.coeff_updates() == after_setup + 2);
    s.set_params(221.f, 0.8f, 0.6f, 0.4f);
    CHECK(s.coeff_updates() == after_setup + 3);
    s.set_params(221.f, 0.8f, 0.6f, 0.5f);
    CHECK(s.coeff_updates() == after_setup + 4);
}

// A parameter moving every control tick is the normal case, not the corner
// case: the drift LFO and the mod lanes keep DETUNE and FILTER walking.
TEST_CASE("KsString stays bounded with parameters moving on the control tick")
{
    KsString s;
    s.init(kSr);

    float phase = 0.f;
    for (int block = 0; block < 500; ++block) {
        phase += 0.013f;
        const float wobble = std::sin(phase);
        s.set_params(220.f * (1.f + 0.02f * wobble),
                     0.5f + 0.49f * wobble,
                     0.5f + 0.45f * wobble,
                     0.5f + 0.5f * wobble);
        for (int i = 0; i < 96; ++i) {
            const float out = s.process(block == 0 && i < 32 ? 0.5f : 0.f);
            REQUIRE(std::isfinite(out));
            REQUIRE(std::abs(out) < 100.f);
        }
    }
}

// init() must leave a usable string, not one waiting for a first set_params.
TEST_CASE("KsString rings straight after init, before any set_params")
{
    KsString s;
    s.init(kSr);

    double energy = 0.0;
    for (int i = 0; i < 4096; ++i)
        energy += std::abs(static_cast<double>(s.process(i < 32 ? 0.5f : 0.f)));

    CHECK(energy > 1.0);
}
