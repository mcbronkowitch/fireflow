// The write side of the panel scan is data logic and belongs on the host,
// exactly like shell/controls.h. On the board a wrong address pattern shows
// up as "the wrong knob moved", which is expensive to find; here it is a
// line. Nothing in this file may include a hardware header.
#include <doctest/doctest.h>
#include <set>
#include <vector>
#include "../shell/mux_plan.h"

namespace {
const std::vector<shell::ChainProfile> kProfiles
    = {shell::kPanelChain, shell::kCouponChain};
}

TEST_CASE("mux plan: the address walks each group's channels once") {
    for(const auto& p : kProfiles)
    {
        int step = 0;
        for(int g = 0; g < p.groups; ++g)
            for(int a = 0; a < p.channels[g]; ++a, ++step)
            {
                const shell::StepPattern s = shell::step_pattern(p, step);
                CHECK(static_cast<int>(s.address) == a);
                CHECK(shell::group_of_step(p, step) == g);
            }
        CHECK(step == shell::scan_steps(p));
    }
}

TEST_CASE("mux plan: exactly one group is enabled per step") {
    // Enables are active low. Two groups on at once shorts two mux outputs
    // onto one sense pin -- silently, and the reading looks plausible.
    for(const auto& p : kProfiles)
        for(int s = 0; s < shell::scan_steps(p); ++s)
        {
            const shell::StepPattern sp = shell::step_pattern(p, s);
            int low = 0;
            for(int g = 0; g < p.groups; ++g)
                if(((sp.enable_mask >> g) & 1u) == 0u) ++low;
            CHECK(low == 1);
            CHECK(((sp.enable_mask >> shell::group_of_step(p, s)) & 1u) == 0u);
        }
}

TEST_CASE("mux plan: a step that does not exist enables nothing") {
    // The safe answer, and not an obvious one: an out-of-range ADDRESS
    // would still select some channel and return a foreign knob's voltage.
    for(const auto& p : kProfiles)
        for(int s : {-1, shell::scan_steps(p), shell::scan_steps(p) + 7})
        {
            const shell::StepPattern sp = shell::step_pattern(p, s);
            for(int g = 0; g < p.groups; ++g)
                CHECK(((sp.enable_mask >> g) & 1u) == 1u);
            CHECK(shell::group_of_step(p, s) == -1);
        }
}

TEST_CASE("mux plan: every channel is reached exactly once per sweep") {
    for(const auto& p : kProfiles)
    {
        std::set<int> seen;
        for(int s = 0; s < shell::scan_steps(p); ++s)
            for(int i = 0; i < p.sense_pins; ++i)
            {
                const int ch = shell::mux_channel(p, s, i);
                CHECK(ch >= 0);
                CHECK(ch < shell::mux_total(p));
                seen.insert(ch);
            }
        CHECK(static_cast<int>(seen.size()) == shell::mux_total(p));
    }
}

TEST_CASE("mux plan: an index that does not exist is answered, not assumed") {
    for(const auto& p : kProfiles)
    {
        CHECK(shell::mux_channel(p, -1, 0) == -1);
        CHECK(shell::mux_channel(p, 0, -1) == -1);
        CHECK(shell::mux_channel(p, shell::scan_steps(p), 0) == -1);
        CHECK(shell::mux_channel(p, 0, p.sense_pins) == -1);
    }
}

TEST_CASE("mux plan: the LED field cannot collide with address or enable") {
    // One chain carries both, which is why LEDs cost no extra CPU. It is
    // also why an overlap would make a lit LED move a knob.
    for(const auto& p : kProfiles)
    {
        const shell::StepPattern sp = shell::step_pattern(p, 5);
        const uint32_t all_leds = (1u << p.led_bits) - 1u;
        const uint32_t dark     = shell::chain_word(p, sp, 0u);
        const uint32_t lit      = shell::chain_word(p, sp, all_leds);
        CHECK(((dark >> p.addr_shift) & 0x0Fu) == sp.address);
        CHECK(((dark >> p.enable_shift) & 0x03u) == sp.enable_mask);
        // Lighting every LED changes nothing below the LED field.
        const uint32_t below = (1u << p.led_shift) - 1u;
        CHECK((lit & below) == (dark & below));
        // And the whole word still fits the chain. Widened before the shift:
        // the panel profile's chain_bits is 32, and shifting a 32-bit value
        // by its own width is undefined behaviour (on x86 the shift count is
        // masked mod 32, so `lit >> 32` silently returns `lit` unchanged and
        // the check would pass for the wrong reason -- or fail outright, as
        // measured here before this line was widened).
        CHECK((static_cast<uint64_t>(lit) >> p.chain_bits) == 0u);
    }
}

TEST_CASE("mux plan: the coupon profile matches the coupon's 595 wiring") {
    // netlist.py:268 wires U_SR1 QA..QH = A0,A1,A2,A3,EN16,EN8,LED_1,LED_2
    // and U_SR2 QA..QF = LED_3..LED_8, with QG/QH open. write_chain() clocks
    // MSB first and U_SR1.QH' feeds U_SR2.SER, so the bit clocked LAST sits
    // nearest the input, at U_SR1.QA. That makes bit 0 the first address
    // line and bit 13 the last LED.
    const shell::ChainProfile& p = shell::kCouponChain;
    CHECK(p.chain_bits == 16);
    CHECK(p.addr_shift == 0);
    CHECK(p.enable_shift == 4);
    CHECK(p.led_shift == 6);
    CHECK(p.led_bits == 8);
    CHECK(p.groups == 2);
    CHECK(p.channels[0] == 16);   // CD74HC4067 on ADC_9
    CHECK(p.channels[1] == 8);    // CD74HC4051 on ADC_10
    CHECK(p.sense_pins == 2);     // ADC_11/ADC_12 reach test points only
    CHECK(p.sense_of_group[0] == 0);
    CHECK(p.sense_of_group[1] == 1);
    CHECK(shell::scan_steps(p) == 24);
}

TEST_CASE("mux plan: the panel profile still describes the shipping panel") {
    // These are the numbers SHELL_MUX_PROBE's CPU cost was measured against
    // (docs/bench/2026-08-23-978cbaf-shell-mux-placement.md). If a coupon
    // change moves them, that measurement silently stops meaning anything.
    const shell::ChainProfile& p = shell::kPanelChain;
    CHECK(p.chain_bits == 32);
    CHECK(p.led_bits == 19);
    CHECK(p.led_shift == 8);
    CHECK(p.sense_pins == 4);
    CHECK(shell::scan_steps(p) == 32);
    CHECK(shell::mux_total(p) == 128);
}

TEST_CASE("mux plan: the sense pins are the raw ADC inputs, not the CV pins") {
    // libDaisy's patch_sm enum runs CV_1..CV_8 = 0..7 and then ADC_9 = 8
    // (daisy_patch_sm.h:20-28). io-budget section 3 spends A2/A3/D8/D9 =
    // ADC_9..ADC_12 on raw pot sense and explicitly rejects the CV pins for
    // it, because those are conditioned bipolar inputs (InitBipolarCv:
    // +-5 V, inverted, 2 ms slew). Reading CV_1 on the coupon returns a pin
    // nothing on the board drives.
    CHECK(shell::kSenseAdcBase == 8);
}

TEST_CASE("mux plan: the coupon's button is the eighth bit shifted out") {
    // U_IN1 is a 74HC165 with Q7 on SR_DATA_IN. Q7 is the LAST parallel
    // stage, so after ~PL the first bit read is D7 and D0 arrives eighth.
    // netlist.py:283 puts BTN_1 on D0 and ties D1..D7 to GND, so bit 7
    // counting from the first bit read is the only one that can move.
    CHECK(shell::button_bit(shell::kCouponChain) == 7);
    // The shipping panel has no single button on the chain yet.
    CHECK(shell::button_bit(shell::kPanelChain) == -1);
}
