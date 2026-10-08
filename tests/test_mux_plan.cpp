// The write side of the panel scan is data logic and belongs on the host,
// exactly like shell/controls.h. On the board a wrong address pattern shows
// up as "the wrong knob moved", which is expensive to find; here it is a
// line. Nothing in this file may include a hardware header.
#include <doctest/doctest.h>
#include <set>
#include <utility>
#include <vector>
#include "../shell/generated_panel_map.h"
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
    // netlist.py:267 wires U_SR1 QA..QH = A0,A1,A2,A3,EN16,EN8,LED_1,LED_2
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
    // U_IN1 is a 74HC165 with Q7 on SR_DATA_IN (netlist.py:283). Q7 is the
    // LAST parallel stage, so after ~PL the first bit read is D7 and D0
    // arrives eighth. netlist.py:285 puts BTN_1 on D0, and :286-287 tie
    // D1..D7 to GND, so bit 7 counting from the first bit read is the only
    // one that can move.
    CHECK(shell::button_bit(shell::kCouponChain) == 7);
    // The shipping panel has no single button on the chain yet.
    CHECK(shell::button_bit(shell::kPanelChain) == -1);
}

TEST_CASE("mux plan: step_of is the inverse of group_of_step") {
    // Not a restatement of the implementation: this walks every step of both
    // profiles, asks which group and channel it is, and requires step_of()
    // to hand the same step back. A sign error or an off-by-one in either
    // direction breaks the round trip.
    for(const shell::ChainProfile* p : {&shell::kPanelChain, &shell::kCouponChain}) {
        for(int s = 0; s < shell::scan_steps(*p); ++s) {
            const int g = shell::group_of_step(*p, s);
            REQUIRE(g >= 0);
            int ch = s;
            for(int i = 0; i < g; ++i) ch -= p->channels[i];
            CAPTURE(s);
            CHECK(shell::step_of(*p, g, ch) == s);
        }
    }
}

TEST_CASE("mux plan: step_of answers out of range instead of assuming") {
    CHECK(shell::step_of(shell::kCouponChain, -1, 0) == -1);
    CHECK(shell::step_of(shell::kCouponChain, 2, 0) == -1);
    CHECK(shell::step_of(shell::kCouponChain, 0, -1) == -1);
    // The coupon's two groups are 16 and 8 channels, so channel 8 exists on
    // group 0 and does not exist on group 1. A shared bound would pass on
    // group 0 and hand back step 24 on group 1, past the end of the step
    // space.
    CHECK(shell::step_of(shell::kCouponChain, 0, 8) == 8);
    CHECK(shell::step_of(shell::kCouponChain, 1, 8) == -1);
    CHECK(shell::step_of(shell::kCouponChain, 1, 7) == 23);
}

TEST_CASE("mux plan: on the panel every sense pin is live on every step") {
    const shell::ChainProfile& p = shell::kPanelChain;
    for(int s = 0; s < shell::scan_steps(p); ++s)
        for(int pin = 0; pin < p.sense_pins; ++pin)
            CHECK(shell::sense_live(p, s, pin));
}

TEST_CASE("mux plan: on the coupon only the group's own sense pin is live") {
    // Group 0 (the 4067) is wired to ADC_9 only, group 1 (the 4051) to
    // ADC_10 only. The other pin's mux is disabled during the step, so its
    // node floats, and a scan that stored it would store noise under a real
    // channel's index.
    const shell::ChainProfile& p = shell::kCouponChain;
    for(int s = 0; s < 16; ++s)
    {
        CHECK(shell::sense_live(p, s, 0));
        CHECK_FALSE(shell::sense_live(p, s, 1));
    }
    for(int s = 16; s < 24; ++s)
    {
        CHECK_FALSE(shell::sense_live(p, s, 0));
        CHECK(shell::sense_live(p, s, 1));
    }
}

TEST_CASE("mux plan: sense_live refuses what does not exist") {
    for(const auto& p : kProfiles)
    {
        CHECK_FALSE(shell::sense_live(p, -1, 0));
        CHECK_FALSE(shell::sense_live(p, shell::scan_steps(p), 0));
        CHECK_FALSE(shell::sense_live(p, 0, -1));
        CHECK_FALSE(shell::sense_live(p, 0, p.sense_pins));
    }
}

namespace {
// A Rev A-shaped profile written out by hand from P2 sections 3 and 4: ten
// 8-channel muxes, 3/3/2/2 on four sense pins, a 40-bit chain with three
// address lines, ten enables and fifteen LEDs. Task 3 holds the generated
// kRevaChain to these same numbers.
constexpr shell::ChainProfile kRevaShape{
    4, shell::kSenseAdcBase, 10,
    {8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
    {0, 0, 0, 1, 1, 1, 2, 2, 3, 3},
    40, 0, 3, 13, 15, -1, 3, true};

const std::vector<shell::ChainProfile> kParallelProfiles
    = {kRevaShape, shell::kCouponPlayChain};

int total_channels(const shell::ChainProfile& p)
{
    int n = 0;
    for(int g = 0; g < p.groups; ++g) n += p.channels[g];
    return n;
}
}

TEST_CASE("parallel: a group is enabled exactly when its sense pin reads it") {
    // P2 section 3: one mux per sense pin per step. Two muxes of one pin on
    // at once short two wipers together; a mux on with nothing reading it is
    // harmless but means the model and the chain disagree.
    for(const auto& p : kParallelProfiles)
        for(int step = 0; step < shell::scan_steps(p); ++step)
        {
            const shell::StepPattern sp = shell::step_pattern(p, step);
            for(int g = 0; g < p.groups; ++g)
            {
                const bool on = ((sp.enable_mask >> g) & 1u) == 0u;
                CAPTURE(step);
                CAPTURE(g);
                CHECK(on == (shell::group_at(p, step, p.sense_of_group[g]) == g));
            }
        }
}

TEST_CASE("parallel: every enabled group sees the step's shared address") {
    // The address lines are common to every mux, so a step is only coherent
    // if each sense pin's channel IS the address on the chain.
    for(const auto& p : kParallelProfiles)
        for(int step = 0; step < shell::scan_steps(p); ++step)
        {
            const shell::StepPattern sp = shell::step_pattern(p, step);
            for(int s = 0; s < p.sense_pins; ++s)
                if(shell::group_at(p, step, s) >= 0)
                    CHECK(shell::channel_at(p, step, s) == sp.address);
        }
}

TEST_CASE("parallel: every (group, channel) is read exactly once per sweep") {
    for(const auto& p : kParallelProfiles)
    {
        std::set<std::pair<int, int>> seen;
        int reads = 0;
        for(int step = 0; step < shell::scan_steps(p); ++step)
            for(int s = 0; s < p.sense_pins; ++s)
            {
                const int g = shell::group_at(p, step, s);
                if(g < 0) continue;
                ++reads;
                seen.insert({g, shell::channel_at(p, step, s)});
            }
        CHECK(reads == total_channels(p));
        CHECK(static_cast<int>(seen.size()) == total_channels(p));
    }
}

TEST_CASE("parallel: a sense pin whose channels are exhausted is fully off") {
    // Its node floats then. Storing it would put noise under a real
    // channel's index (Review Focus 1).
    for(int step = 16; step < 24; ++step)
    {
        const shell::StepPattern sp = shell::step_pattern(kRevaShape, step);
        for(int g = 6; g < 10; ++g) CHECK(((sp.enable_mask >> g) & 1u) == 1u);
        CHECK_FALSE(shell::sense_live(kRevaShape, step, 2));
        CHECK_FALSE(shell::sense_live(kRevaShape, step, 3));
        CHECK(shell::sense_live(kRevaShape, step, 0));
        CHECK(shell::sense_live(kRevaShape, step, 1));
    }
    for(int step = 8; step < 16; ++step)
    {
        CHECK_FALSE(shell::sense_live(shell::kCouponPlayChain, step, 1));
        CHECK(shell::step_pattern(shell::kCouponPlayChain, step).enable_mask == 0x2u);
    }
}

TEST_CASE("parallel: step_of finds each channel where the scan reads it") {
    for(const auto& p : kParallelProfiles)
        for(int g = 0; g < p.groups; ++g)
            for(int ch = 0; ch < p.channels[g]; ++ch)
            {
                const int s = shell::step_of(p, g, ch);
                REQUIRE(s >= 0);
                CHECK(shell::group_at(p, s, p.sense_of_group[g]) == g);
                CHECK(shell::channel_at(p, s, p.sense_of_group[g]) == ch);
            }
}

TEST_CASE("parallel: step counts") {
    CHECK(shell::scan_steps(kRevaShape) == 24);
    CHECK(shell::mux_total(kRevaShape) == 96);
    CHECK(shell::sense_channels(kRevaShape, 0) == 24);
    CHECK(shell::sense_channels(kRevaShape, 3) == 16);
    CHECK(shell::scan_steps(shell::kCouponPlayChain) == 16);
    CHECK(shell::mux_total(shell::kCouponPlayChain) == 32);
}

TEST_CASE("parallel: steps that do not exist enable nothing and read nothing") {
    for(const auto& p : kParallelProfiles)
        for(int step : {-1, shell::scan_steps(p), shell::scan_steps(p) + 5})
        {
            const shell::StepPattern sp = shell::step_pattern(p, step);
            CHECK(sp.enable_mask == static_cast<uint16_t>((1u << p.groups) - 1u));
            for(int s = 0; s < p.sense_pins; ++s)
            {
                CHECK(shell::group_at(p, step, s) == -1);
                CHECK(shell::channel_at(p, step, s) == -1);
                CHECK_FALSE(shell::sense_live(p, step, s));
            }
            CHECK(shell::group_of_step(p, 0) == -1);   // sequential only
        }
}

TEST_CASE("chain word: Rev A's address cannot reach the enable field") {
    // Three address lines sit directly below EN0 (Review Focus 4). With
    // every enable ON (mask 0), an unmasked 4-bit address 0x0F would set
    // bit 3 and switch mux 0 off.
    const uint64_t w = shell::chain_word(kRevaShape, shell::StepPattern{0x0F, 0}, 0u);
    CHECK((w & 0x7u) == 0x7u);
    CHECK(((w >> 3) & 0x3FFu) == 0u);
}

TEST_CASE("chain word: a 40-bit word puts LEDs at 13..27 and nothing above") {
    const uint64_t lit = shell::chain_word(kRevaShape, shell::StepPattern{0, 0}, 0xFFFFFFFFu);
    CHECK(((lit >> 13) & 0x7FFFu) == 0x7FFFu);
    CHECK((lit & 0x1FFFu) == 0u);
    CHECK((lit >> 28) == 0u);
    const uint64_t off = shell::chain_word(kRevaShape, shell::step_pattern(kRevaShape, -1), 0u);
    CHECK(off == (uint64_t{0x3FF} << 3));
}

TEST_CASE("coupon chain: words are bit for bit what part 1 clocked") {
    // Every coupon probe was measured on these words. Recomputed with part
    // 1's formula, independent of chain_word(): 4 address bits, 2 enables at
    // 4, 8 LEDs at 6.
    for(int step = 0; step < 24; ++step)
        for(uint32_t leds : {0u, 0xA5u, 0xFFu})
        {
            const shell::StepPattern sp = shell::step_pattern(shell::kCouponChain, step);
            const uint64_t legacy = (uint64_t{sp.address} & 0x0Fu)
                                    | ((uint64_t{sp.enable_mask} & 0x3u) << 4)
                                    | ((uint64_t{leds} & 0xFFu) << 6);
            CAPTURE(step);
            CHECK(shell::chain_word(shell::kCouponChain, sp, leds) == legacy);
        }
    CHECK(shell::step_pattern(shell::kCouponChain, 0).enable_mask == 0x2u);
    CHECK(shell::step_pattern(shell::kCouponChain, 16).enable_mask == 0x1u);
    CHECK(shell::step_pattern(shell::kCouponChain, 23).address == 7);
    CHECK_FALSE(shell::kCouponChain.parallel_sense);
    CHECK(shell::kCouponChain.addr_bits == 4);
}

TEST_CASE("coupon play chain: the coupon's wiring, run in parallel") {
    const shell::ChainProfile& c = shell::kCouponChain;
    const shell::ChainProfile& p = shell::kCouponPlayChain;
    CHECK(p.parallel_sense);
    CHECK(p.sense_pins == c.sense_pins);
    CHECK(p.groups == c.groups);
    CHECK(p.channels[0] == c.channels[0]);
    CHECK(p.channels[1] == c.channels[1]);
    CHECK(p.sense_of_group[0] == c.sense_of_group[0]);
    CHECK(p.sense_of_group[1] == c.sense_of_group[1]);
    CHECK(p.chain_bits == c.chain_bits);
    CHECK(p.addr_shift == c.addr_shift);
    CHECK(p.enable_shift == c.enable_shift);
    CHECK(p.led_shift == c.led_shift);
    CHECK(p.led_bits == c.led_bits);
    CHECK(p.button_bit == c.button_bit);
    CHECK(p.addr_bits == c.addr_bits);
    for(int step = 0; step < 8; ++step)
    {
        CHECK(shell::step_pattern(p, step).enable_mask == 0x0u);   // both on
        CHECK(shell::step_pattern(p, step).address == step);
    }
    for(int step = 8; step < 16; ++step)
        CHECK(shell::step_pattern(p, step).address == step);       // the 4067's upper half
}

TEST_CASE("mux plan: the generated Rev A chain is P2's, field for field") {
    const shell::ChainProfile& p = shell::kRevaChain;
    const shell::ChainProfile& e = kRevaShape;
    CHECK(p.sense_pins == e.sense_pins);
    CHECK(p.sense_adc_base == e.sense_adc_base);
    CHECK(p.groups == e.groups);
    for(int g = 0; g < shell::kMaxGroups; ++g)
    {
        CHECK(p.channels[g] == e.channels[g]);
        CHECK(p.sense_of_group[g] == e.sense_of_group[g]);
    }
    CHECK(p.chain_bits == e.chain_bits);
    CHECK(p.addr_shift == e.addr_shift);
    CHECK(p.enable_shift == e.enable_shift);
    CHECK(p.led_shift == e.led_shift);
    CHECK(p.led_bits == e.led_bits);
    CHECK(p.button_bit == e.button_bit);
    CHECK(p.addr_bits == e.addr_bits);
    CHECK(p.parallel_sense == e.parallel_sense);
    CHECK(shell::scan_steps(p) == 24);
}

TEST_CASE("mux plan: Rev A's keys are D0..D3 of the 165") {
    REQUIRE(shell::kRevaKeys.count == 4);
    CHECK(shell::kRevaKeys.bit[0] == 7);
    CHECK(shell::kRevaKeys.bit[1] == 6);
    CHECK(shell::kRevaKeys.bit[2] == 5);
    CHECK(shell::kRevaKeys.bit[3] == 4);
}
