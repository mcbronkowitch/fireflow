#include "doctest/doctest.h"

#include <cmath>
#include <cstdint>

#include "synth/wt_osc.h"

using namespace spky;

// The SD probe's bank reload (spec 2026-10-31-sd-use-cases-tests-design.md
// section 5, D1; shell/sd_probe.cpp, SHELL_SD_PROBE=bank) swaps the bank
// pointer at the top of a block while WAVE plays. That is safe only while no
// pointer into the old bank outlives the swap: WtOsc must read the bank
// through wt::table() on every sample. If any read caches a table pointer,
// the oscillator keeps reading the old bank after set_bank() and this fails.
TEST_CASE("wt_bank swap: bank swap takes effect on the next read") {
    static int16_t negated[wt::kTotalSamples];
    for (int i = 0; i < wt::kTotalSamples; ++i)
        negated[i] = static_cast<int16_t>(-wt::kBankSamples[i]);

    // Two identical oscillators, a frequency that walks across the table and
    // a morph that blends two frames, so every sample reads real data.
    WtOsc a, b;
    WtOsc* const both[] = {&a, &b};
    for (WtOsc* o : both) {
        o->init(48000.f);
        o->set_freq(331.f);
        o->set_morph(0.37f);
    }

    wt::set_bank(nullptr);
    for (int i = 0; i < 64; ++i) {
        const float va = a.process();
        const float vb = b.process();
        REQUIRE(va == vb);
    }
    const float factory_65th = b.process();   // b's 65th sample, factory bank
    REQUIRE(factory_65th != 0.f);

    wt::set_bank(negated);
    const float swapped_65th = a.process();   // a's 65th sample, negated bank
    wt::set_bank(nullptr);

    // An absolute bound, not doctest::Approx (its epsilon adds a scale of 1).
    CHECK(std::fabs(swapped_65th + factory_65th) <= 1e-6f);
    CHECK(wt::bank() == wt::kBankSamples);
}
