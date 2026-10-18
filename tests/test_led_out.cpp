// The lamp word the firmware latches once per block: which lamps are lit in
// block `frame`, given each lamp's duty from the LED law. Pure data logic, no
// hardware type.
// Spec: docs/superpowers/specs/2026-10-18-rev-a-p6b2-mod-keys-leds-design.md
// section 4.3 (the duty pattern spreads the on-blocks rather than bunching them).
#include <doctest/doctest.h>
#include <cstdint>
#include "../shell/led_out.h"
#include "control/led_law.hpp"
#include "control/params.hpp"

namespace {

constexpr int kSteps  = spkyled::kLedSteps;
constexpr int kPeriod = kSteps - 1;   // a duty pattern repeats every steps-1 blocks

// One lamp (MODBTN_L) on bit 3, every other lamp without a bit.
struct OneLamp {
    int8_t bit[ffctl::NUM_LIGHTS];
    int    duty[ffctl::NUM_LIGHTS];
    OneLamp() {
        for (int i = 0; i < ffctl::NUM_LIGHTS; ++i) { bit[i] = -1; duty[i] = 0; }
        bit[ffctl::MODBTN_L] = 3;
    }
    bool lit(uint32_t frame) const {
        return (shell::led_word(duty, kSteps, frame, bit) >> 3) & 1u;
    }
};

} // namespace

TEST_CASE("led_out: duty 0 is never on, full is always on") {
    OneLamp l;
    l.duty[ffctl::MODBTN_L] = 0;
    for (uint32_t f = 0; f < 3u * kPeriod + 5u; ++f) CHECK_FALSE(l.lit(f));
    l.duty[ffctl::MODBTN_L] = kSteps - 1;
    for (uint32_t f = 0; f < 3u * kPeriod + 5u; ++f) CHECK(l.lit(f));
}

TEST_CASE("led_out: duty d is on d times per period") {
    OneLamp l;
    for (int d = 0; d < kSteps; ++d) {
        l.duty[ffctl::MODBTN_L] = d;
        // Any kPeriod consecutive blocks, not only the aligned ones.
        for (uint32_t start : {0u, 1u, 5u, 1000u, 123457u}) {
            int on = 0;
            for (uint32_t f = start; f < start + kPeriod; ++f) on += l.lit(f) ? 1 : 0;
            CAPTURE(d);
            CAPTURE(start);
            CHECK(on == d);
        }
    }
}

TEST_CASE("led_out: on-blocks are spread, no run longer than ceil((steps-1)/d)") {
    // The run is the distance from one on-block to the next: at most
    // ceil(period / d) blocks, so a lamp at duty d never sits dark for longer
    // than an evenly spread pattern would. Bunching the d on-blocks at the
    // start of the period fails this for every 2 <= d <= period - 2.
    OneLamp l;
    for (int d = 1; d < kSteps; ++d) {
        l.duty[ffctl::MODBTN_L] = d;
        const int bound = (kPeriod + d - 1) / d;
        int last = -1, worst = 0;
        for (int f = 0; f < 4 * kPeriod; ++f) {
            if (!l.lit(static_cast<uint32_t>(f))) continue;
            if (last >= 0 && f - last > worst) worst = f - last;
            last = f;
        }
        CAPTURE(d);
        CHECK(worst <= bound);
    }
}

TEST_CASE("led_out: a lamp without a bit sets nothing") {
    int8_t bit[ffctl::NUM_LIGHTS];
    int    duty[ffctl::NUM_LIGHTS];
    for (int i = 0; i < ffctl::NUM_LIGHTS; ++i) { bit[i] = -1; duty[i] = kSteps - 1; }
    for (uint32_t f = 0; f < 2u * kPeriod; ++f)
        CHECK(shell::led_word(duty, kSteps, f, bit) == 0u);
    // One lamp with a bit: only that bit, however many others are full.
    bit[ffctl::GATE_B_L] = 13;
    for (uint32_t f = 0; f < 2u * kPeriod; ++f)
        CHECK(shell::led_word(duty, kSteps, f, bit) == (1u << 13));
}
