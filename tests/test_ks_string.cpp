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
//
// Every case here sits at or below nonlinearity 0.75, where recompute() sets
// _noise_amount to exactly zero and the dispersion noise is multiplied out of
// the delay. That is what keeps them bit-exact after the port stopped drawing
// from libc rand() (engine/body/ks_string.h). The 0.75 case is deliberately
// AT the threshold rather than safely below it: it drives the dispersion
// parameter block -- stretch point, allpass gain, noise filter -- to the far
// end of its range while still pinning the result to the last bit. Above the
// threshold the two diverge on purpose; that case is its own test below.
const Params kCases[] = {
    { "curved bridge (nonlinearity 0)",   220.f, 0.7f, 0.7f,  0.0f  },
    { "dispersion (nonlinearity 0.4)",    220.f, 0.7f, 0.7f,  0.4f  },
    { "dispersion at the noise threshold", 110.f, 0.4f, 0.5f, 0.75f },
    { "infinite decay (damping 0.97)",    440.f, 0.6f, 0.97f, 0.2f  },
};

// Sum |x| over a half-open sample range.
double energy_of(const std::vector<float>& v, int from, int to)
{
    double e = 0.0;
    for (int i = from; i < to; ++i) e += std::abs(static_cast<double>(v[static_cast<size_t>(i)]));
    return e;
}

std::vector<float> run_port(uint32_t seed, const Params& p, const std::vector<float>& in)
{
    KsString s;
    s.init(kSr, seed);
    s.set_params(p.freq_hz, p.brightness, p.damping, p.nonlinearity);
    std::vector<float> out(in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = s.process(in[i]);
    return out;
}

// Above the 0.75 threshold, where the noise actually reaches the delay.
const Params kNoisy = { "dispersion with noise (0.9)", 110.f, 0.4f, 0.5f, 0.9f };

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

        // The reference still draws its dispersion noise from libc rand();
        // the port draws from an owned spky::Rng. At these parameters neither
        // draw reaches the output -- _noise_amount is exactly zero at and
        // below nonlinearity 0.75 -- which is why they can still be compared
        // bit for bit. Reseeding pins the reference anyway, so a failure here
        // is never a different roll of the dice.
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

        const std::vector<float> got = run_port(0xB0D1u, p, in);

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
    s.init(kSr, 0xB0D1u);
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
    s.init(kSr, 0xB0D1u);

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
    s.init(kSr, 0xB0D1u);

    double energy = 0.0;
    for (int i = 0; i < 4096; ++i)
        energy += std::abs(static_cast<double>(s.process(i < 32 ? 0.5f : 0.f)));

    CHECK(energy > 1.0);
}

// --- the deliberate divergence -------------------------------------------
//
// Above nonlinearity 0.75 the dispersion noise reaches the delay, and there
// the port is NOT the reference: it draws from its own spky::Rng instead of
// libc rand(). The fork's determinism rule (engine/mod/rng.h) and fidelity to
// the reference cannot both hold at this one line, and the rule wins. The
// tests below say where that leaves us instead of quietly widening the
// equality above into a tolerance.
//
// What is still pinned exactly: everything in recompute() except the noise
// amplitude itself, by the 0.75-threshold case in kCases -- that case drives
// the same dispersion branch, the same stretch point, allpass gain and noise
// filter, and matches the reference to the last bit.

TEST_CASE("KsString above the noise threshold: diverges from the reference, "
          "still rings like the same string")
{
    constexpr int n  = 8192;
    const auto    in = exciter(n);

    std::srand(20260726u);
    daisysp::String reference;
    reference.Init(kSr);
    reference.SetFreq(kNoisy.freq_hz);
    reference.SetBrightness(kNoisy.brightness);
    reference.SetDamping(kNoisy.damping);
    reference.SetNonLinearity(kNoisy.nonlinearity);
    std::vector<float> want(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        want[static_cast<size_t>(i)] = reference.Process(in[static_cast<size_t>(i)]);

    const std::vector<float> got = run_port(0xB0D1u, kNoisy, in);

    // The divergence is real, not a rounding artefact we could have avoided.
    //
    // Note what this does NOT guard: zeroing the port's _noise_amount makes
    // the two differ MORE, so this check survives it (verified by mutation).
    // "the noise still reaches the delay" is pinned by the seeding test
    // below, where two seeds must give two different strings -- that one
    // fails the moment the noise stops mattering.
    int mismatches = 0;
    for (int i = 0; i < n; ++i)
        if (want[static_cast<size_t>(i)] != got[static_cast<size_t>(i)]) ++mismatches;
    CHECK(mismatches > 0);

    // What survives the divergence: same string, same decay. The band is
    // wide on purpose -- it states "both ring, comparably" and nothing about
    // today's numbers, because the only thing that legitimately differs here
    // is which noise sequence jitters the delay.
    const double want_e = energy_of(want, 0, n);
    const double got_e  = energy_of(got, 0, n);
    CAPTURE(want_e);
    CAPTURE(got_e);
    CHECK(want_e > 1.0);
    CHECK(got_e > 0.5 * want_e);
    CHECK(got_e < 2.0 * want_e);

    // And it decays rather than sustaining or blowing up: damping is 0.5, so
    // the second half must be quieter than the first.
    CHECK(energy_of(got, n / 2, n) < energy_of(got, 0, n / 2));
    for (int i = 0; i < n; ++i) REQUIRE(std::isfinite(got[static_cast<size_t>(i)]));
}

// The property libc rand() could not give us, and the reason for the whole
// change: one seed, one render, on every platform.
TEST_CASE("KsString dispersion noise is seeded, reproducible and per-instance")
{
    constexpr int n  = 4096;
    const auto    in = exciter(n);

    const std::vector<float> a1 = run_port(0xB0D1u, kNoisy, in);
    const std::vector<float> a2 = run_port(0xB0D1u, kNoisy, in);
    const std::vector<float> b  = run_port(0x1234u, kNoisy, in);

    // Same seed, same string, to the last bit -- twice in the same process,
    // where libc rand() would have carried its state across from the first run.
    CHECK(a1 == a2);

    // A different seed is a different string. Without this, an init() that
    // dropped its seed argument and always used a constant would pass the
    // check above.
    CHECK(a1 != b);
    CHECK(energy_of(b, 0, n) > 1.0);
}

// reset() rewinds the noise as well as the delay line: a retriggered string
// has to ring the same way twice, or a render stops being reproducible the
// moment a note repeats.
TEST_CASE("KsString reset rewinds the noise sequence")
{
    constexpr int n  = 2048;
    const auto    in = exciter(n);

    KsString s;
    s.init(kSr, 0xB0D1u);
    s.set_params(kNoisy.freq_hz, kNoisy.brightness, kNoisy.damping, kNoisy.nonlinearity);

    std::vector<float> first(static_cast<size_t>(n)), second(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) first[static_cast<size_t>(i)] = s.process(in[static_cast<size_t>(i)]);
    s.reset();
    for (int i = 0; i < n; ++i) second[static_cast<size_t>(i)] = s.process(in[static_cast<size_t>(i)]);

    CHECK(first == second);
    CHECK(energy_of(first, 0, n) > 1.0);
}
