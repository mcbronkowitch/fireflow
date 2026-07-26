#include "doctest/doctest.h"
#include "body/exciter.h"
#include <cmath>

using namespace spky;

static float energy(Exciter& e, int n) {
    float sum = 0.f;
    for (int i = 0; i < n; ++i) { const float s = e.process(); sum += s * s; }
    return sum;
}

TEST_CASE("Exciter is silent until struck and decays after") {
    Exciter e;
    e.init(7, 48000.f);
    e.set_character(0.f);
    e.set_length(0.005f);
    e.set_freq(220.f);
    CHECK(energy(e, 480) == 0.f);
    e.strike(1.f);
    const float during = energy(e, 240);   // 5 ms
    const float after  = energy(e, 4800);  // 100 ms later
    CHECK(during > 0.f);
    CHECK(after < during * 0.01f);
}

TEST_CASE("Exciter zones produce different signals from the same seed") {
    float sig[4][512];
    for (int z = 0; z < 4; ++z) {
        Exciter e;
        e.init(7, 48000.f);
        e.set_character(z / 3.f);
        e.set_length(0.005f);
        e.set_freq(220.f);
        e.strike(1.f);
        for (int i = 0; i < 512; ++i) sig[z][i] = e.process();
    }
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b) {
            bool differs = false;
            for (int i = 0; i < 512; ++i) if (sig[a][i] != sig[b][i]) differs = true;
            CHECK(differs);
        }
}

TEST_CASE("Exciter is deterministic for a given seed") {
    float a[512], b[512];
    for (int pass = 0; pass < 2; ++pass) {
        Exciter e;
        e.init(1234, 48000.f);
        e.set_character(0.5f);
        e.set_length(0.01f);
        e.set_freq(330.f);
        e.strike(0.8f);
        for (int i = 0; i < 512; ++i) (pass ? b : a)[i] = e.process();
    }
    for (int i = 0; i < 512; ++i) CHECK(a[i] == b[i]);
}

TEST_CASE("Exciter in continuous mode does not decay to silence, in any zone") {
    // Fix round 1: character 0 (click) was silent after the first impulse
    // decayed, because _fresh never re-armed under set_continuous(true) --
    // only zone 0.4 (noise burst) was covered by the original single-zone
    // test, which is exactly the gap that let it through. All four zone
    // anchor points (spec 2026-07-26-body-resonator-engine §2) must sustain.
    const float chars[4] = {0.f, 1.f / 3.f, 2.f / 3.f, 1.f};
    for (float c : chars) {
        CAPTURE(c);
        Exciter e;
        e.init(7, 48000.f);
        e.set_character(c);
        e.set_length(0.05f);
        e.set_freq(220.f);
        e.set_continuous(true);
        e.strike(1.f);
        energy(e, 48000);   // 1 s warmup, well past any single strike's decay
        CHECK(energy(e, 4800) > 0.f);
    }
}

TEST_CASE("Exciter stops decaying to near-silence in every zone when not continuous") {
    // Mirror of the bug above: a zone that keeps sustaining once continuous
    // mode is off, after its strike has run its length, would be the same
    // defect the other way round. Generalizes the brief's single click-only
    // "decays after" test to all four zone anchor points.
    //
    // Windows are equal-sized (240 samples each), separated by a discarded
    // gap, rather than reusing the brief's 240-vs-4800 asymmetric windows.
    // Click's own filter memory dies out in ~4 samples independent of the
    // envelope, so the brief's mismatched window sizes happen to work for
    // zone 0 alone; noise/sputter/ping have no such self-decay -- their
    // level tracks the envelope only -- so a 20x-longer trailing window
    // sampled immediately after "during" can integrate *more* total energy
    // even while genuinely decaying. Equal windows placed ~100 ms apart
    // isolate the thing actually under test (does the envelope reach
    // near-silence) from that window-size artifact.
    const float chars[4] = {0.f, 1.f / 3.f, 2.f / 3.f, 1.f};
    for (float c : chars) {
        CAPTURE(c);
        Exciter e;
        e.init(7, 48000.f);
        e.set_character(c);
        e.set_length(0.005f);
        e.set_freq(220.f);
        e.strike(1.f);
        const float during = energy(e, 240);   // 5 ms right after the strike
        energy(e, 4560);                        // discard: advance to ~100 ms
        const float after = energy(e, 240);     // 5 ms window, same size
        CHECK(during > 0.f);
        CHECK(after < during * 0.01f);
    }
}

// -- Task 6 mutation-tested additions: each zone must be characteristically
// distinguishable, not merely bit-different (four different RNG draws would
// satisfy the test above even if they sounded identical).

TEST_CASE("Exciter click zone concentrates energy at the strike") {
    // Click (RESO 0): filtered impulse. Almost all of its energy should land
    // in the first millisecond; a noise burst or sputter would still be
    // producing comparable energy well after the strike.
    Exciter e;
    e.init(7, 48000.f);
    e.set_character(0.f);
    e.set_length(0.05f);        // long strike length so envelope decay alone
                                 // does not explain a concentrated attack
    e.set_freq(220.f);
    e.strike(1.f);
    const float first_ms = energy(e, 48);      // 1 ms
    const float next_ms  = energy(e, 48);       // the following ms
    CHECK(first_ms > 0.f);
    CHECK(next_ms < first_ms * 0.25f);
}

TEST_CASE("Exciter noise zone is broadband, unlike the tonal ping zone") {
    // A crude zero-crossing-rate check: broadband noise crosses zero far
    // more often per unit time than a sine at a low fundamental. This is the
    // assertion that would fail if the noise zone were replaced by the ping
    // zone (or vice versa).
    auto zero_crossings = [](Exciter& e, int n) {
        int crossings = 0;
        float prev = e.process();
        for (int i = 1; i < n; ++i) {
            const float s = e.process();
            if ((prev < 0.f) != (s < 0.f)) ++crossings;
            prev = s;
        }
        return crossings;
    };

    Exciter noise_e;
    noise_e.init(7, 48000.f);
    noise_e.set_character(0.5f);   // zone 1: noise burst
    noise_e.set_length(0.05f);
    noise_e.set_freq(220.f);
    noise_e.strike(1.f);
    const int noise_crossings = zero_crossings(noise_e, 1000);

    Exciter ping_e;
    ping_e.init(7, 48000.f);
    ping_e.set_character(1.f);     // zone 3: pure tonal ping
    ping_e.set_length(0.05f);
    ping_e.set_freq(220.f);
    ping_e.strike(1.f);
    const int ping_crossings = zero_crossings(ping_e, 1000);

    // 220 Hz over 1000 samples at 48 kHz is ~9 cycles -> ~18 crossings.
    // Filtered noise crosses zero far more often.
    CHECK(noise_crossings > ping_crossings * 3);
}

TEST_CASE("Exciter sputter zone gates its noise, unlike the noise zone") {
    // Sputter (RESO 2/3, pure) rng-gates its bursts, so it spends stretches
    // of consecutive samples at exactly zero. The noise-burst zone (RESO
    // 1/3..2/3) runs a filtered noise source through every sample and does
    // not produce runs of exact zero. This is the assertion that would fail
    // if the sputter zone were replaced by the noise zone.
    Exciter e;
    e.init(7, 48000.f);
    e.set_character(2.f / 3.f);   // zone 2, t = 0: pure sputter, no ping mixed in
    e.set_length(0.05f);
    e.set_freq(220.f);
    e.strike(1.f);

    int zero_samples = 0;
    for (int i = 0; i < 2000; ++i) if (e.process() == 0.f) ++zero_samples;
    CHECK(zero_samples > 0);

    Exciter noise_e;
    noise_e.init(7, 48000.f);
    noise_e.set_character(0.5f);  // zone 1: noise burst, no gating
    noise_e.set_length(0.05f);
    noise_e.set_freq(220.f);
    noise_e.strike(1.f);

    int noise_zero_samples = 0;
    for (int i = 0; i < 2000; ++i) if (noise_e.process() == 0.f) ++noise_zero_samples;
    CHECK(noise_zero_samples == 0);
}

TEST_CASE("Exciter ping zone tracks the fundamental it was set to") {
    // Pure tonal ping (RESO 1.0) should ring near set_freq, not at some
    // unrelated rate -- distinguishing it from every other zone, none of
    // which are frequency-locked to set_freq at all.
    Exciter e;
    e.init(7, 48000.f);
    e.set_character(1.f);
    e.set_length(0.05f);
    e.set_freq(220.f);
    e.strike(1.f);

    int crossings = 0;
    float prev = e.process();
    const int n = 4800; // 100 ms
    for (int i = 1; i < n; ++i) {
        const float s = e.process();
        if ((prev < 0.f) != (s < 0.f)) ++crossings;
        prev = s;
    }
    // 220 Hz over 100 ms -> 22 cycles -> 44 zero crossings. Allow slack for
    // the fast_sin approximation and envelope decay near the tail.
    CHECK(crossings > 30);
    CHECK(crossings < 60);
}
