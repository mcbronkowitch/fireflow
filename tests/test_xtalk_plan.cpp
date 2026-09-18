// The crosstalk probe's victims, aggressors and chain words. Derived from
// the crosstalk spec sections 2 and 4 and from
// hardware/coupon/scripts/netlist.py; if the two disagree, the netlist wins
// and this file is wrong.
#include <doctest/doctest.h>
#include "../shell/xtalk_plan.h"
#include "../shell/settle_plan.h"
#include "../shell/mux_plan.h"

namespace {

// The r_src_ohm settle_plan.cpp carries for a (group, channel), or 0 if that
// channel is not a target there. Not a copy of the impedance column -- a
// lookup into the table that already passed its own host gate.
uint32_t settle_r_src(int group, int ch) {
    for(int p = 0; p < shell::kSettlePairs; ++p) {
        const shell::SettlePair& sp = shell::kSettlePlan[p];
        if(sp.group == group && sp.to_ch == ch) return sp.r_src_ohm;
    }
    return 0u;
}

int victim_index_of(const shell::XtalkCase& c) {
    for(int v = 0; v < shell::kXtalkVictims; ++v) {
        const shell::XtalkVictim& vv = shell::kXtalkVictimTable[v];
        if(vv.group == c.group && vv.channel == c.channel) return v;
    }
    return -1;
}

} // namespace

TEST_CASE("xtalk victims: every victim exists and carries the settle plan's impedance") {
    for(int v = 0; v < shell::kXtalkVictims; ++v) {
        const shell::XtalkVictim& vv = shell::kXtalkVictimTable[v];
        CAPTURE(v);
        REQUIRE((vv.group == 0 || vv.group == 1));
        CHECK(vv.channel >= 0);
        CHECK(vv.channel < shell::kCouponChain.channels[vv.group]);
        // The cross-check that makes the two tables one table.
        const uint32_t r = settle_r_src(vv.group, vv.channel);
        REQUIRE(r != 0u);
        CHECK(vv.r_src_ohm == r);
    }
}

TEST_CASE("xtalk victims: the impedance ladder is the attribution axis") {
    // Spec section 2: two 5150 ohm dividers (one per mux), one 650 ohm
    // divider as the impedance control, and two 0 ohm ties as the
    // instrument's zero -- one per mux. Without one zero per mux a delta on
    // the 4051 has nothing to be judged against.
    int at_5150 = 0, at_650 = 0, ties = 0, ties_g0 = 0, ties_g1 = 0;
    for(int v = 0; v < shell::kXtalkVictims; ++v) {
        const shell::XtalkVictim& vv = shell::kXtalkVictimTable[v];
        if(vv.r_src_ohm == 5150u) ++at_5150;
        else if(vv.r_src_ohm == 650u) ++at_650;
        else if(vv.r_src_ohm == 150u) {
            ++ties;
            if(vv.group == 0) ++ties_g0; else ++ties_g1;
        }
    }
    CHECK(at_5150 == 2);
    CHECK(at_650 == 1);
    CHECK(ties == 2);
    CHECK(ties_g0 == 1);
    CHECK(ties_g1 == 1);
}

TEST_CASE("xtalk words: a word selects its victim and disables the other mux by default") {
    for(int v = 0; v < shell::kXtalkVictims; ++v) {
        const shell::XtalkVictim& vv = shell::kXtalkVictimTable[v];
        CAPTURE(v);
        const uint32_t w = shell::xtalk_word(vv, shell::other_enable_mask(vv.group));
        // The victim's own enable is LOW: enables are active low, so the
        // victim's mux is the one that is on.
        CHECK((w & shell::victim_enable_mask(vv.group)) == 0u);
        // The other mux's enable is HIGH: off.
        CHECK((w & shell::other_enable_mask(vv.group)) != 0u);
        // The address bits the victim's mux actually reads carry its channel.
        const uint32_t addr = (w & shell::victim_address_mask(vv.group))
                              >> shell::kCouponChain.addr_shift;
        CHECK(addr == static_cast<uint32_t>(
                  vv.channel & ((1 << shell::address_bits_of_group(vv.group)) - 1)));
    }
}

TEST_CASE("xtalk words: the 4051 reads three address lines, the 4067 four") {
    // netlist.py:163, "the 8:1 uses three of the four". This is the whole
    // reason MUX_A3 can be an aggressor against a victim on the 4051 and
    // against nothing else, so it is asserted rather than assumed.
    CHECK(shell::address_bits_of_group(0) == 4);
    CHECK(shell::address_bits_of_group(1) == 3);
    CHECK(shell::victim_address_mask(0) == (0x0Fu << shell::kCouponChain.addr_shift));
    CHECK(shell::victim_address_mask(1) == (0x07u << shell::kCouponChain.addr_shift));
    CHECK(shell::victim_enable_mask(0) == (1u << shell::kCouponChain.enable_shift));
    CHECK(shell::victim_enable_mask(1) == (2u << shell::kCouponChain.enable_shift));
    CHECK(shell::other_enable_mask(0) == shell::victim_enable_mask(1));
    CHECK(shell::other_enable_mask(1) == shell::victim_enable_mask(0));
}

TEST_CASE("xtalk plan: every case names a real victim and a real channel") {
    for(int i = 0; i < shell::kXtalkCases; ++i) {
        const shell::XtalkCase& c = shell::kXtalkPlan[i];
        CAPTURE(i);
        REQUIRE((c.group == 0 || c.group == 1));
        CHECK(c.channel >= 0);
        CHECK(c.channel < shell::kCouponChain.channels[c.group]);
        CHECK(victim_index_of(c) >= 0);
        CHECK(c.r_src_ohm == settle_r_src(c.group, c.channel));
        CHECK(c.row >= 1);
        CHECK(c.row <= 10);
    }
}

TEST_CASE("xtalk plan: both words hold the victim still") {
    // The measurement's entire premise (spec section 3): the victim never
    // changes channel, so the sample-and-hold's channel-change transient is
    // not in the reading at all. A case whose two words moved the victim's
    // address or its enable would be measuring settle time again, under a
    // name that says crosstalk.
    for(int i = 0; i < shell::kXtalkCases; ++i) {
        const shell::XtalkCase& c = shell::kXtalkPlan[i];
        CAPTURE(i);
        const uint32_t hold = shell::victim_address_mask(c.group)
                              | shell::victim_enable_mask(c.group);
        CHECK((c.word_a & hold) == (c.word_b & hold));
        CHECK((c.word_a & shell::victim_enable_mask(c.group)) == 0u);
    }
}

TEST_CASE("xtalk plan: a Latch case changes something and a control changes nothing") {
    int controls = 0, latch_aggressors = 0;
    for(int i = 0; i < shell::kXtalkCases; ++i) {
        const shell::XtalkCase& c = shell::kXtalkPlan[i];
        CAPTURE(i);
        if(c.kind == shell::XtalkKind::Latch) {
            if(c.row == 2) { CHECK(c.word_a == c.word_b); ++controls; }
            else           { CHECK(c.word_a != c.word_b); ++latch_aggressors; }
        } else {
            // Silent, ShiftOnly and Static have no second word: nothing is
            // latched, so a differing word_b would be a field nobody reads
            // pretending to be an event.
            CHECK(c.word_a == c.word_b);
        }
    }
    CHECK(controls == shell::kXtalkVictims);
    CHECK(latch_aggressors > 0);
}

TEST_CASE("xtalk plan: every victim has a silent case and a control case") {
    // Without both, that victim's aggressor cases have no reference and
    // every delta computed for it is a difference against nothing.
    for(int v = 0; v < shell::kXtalkVictims; ++v) {
        const shell::XtalkVictim& vv = shell::kXtalkVictimTable[v];
        CAPTURE(v);
        int silent = 0, control = 0, silent_printing = 0;
        for(int i = 0; i < shell::kXtalkCases; ++i) {
            const shell::XtalkCase& c = shell::kXtalkPlan[i];
            if(c.group != vv.group || c.channel != vv.channel) continue;
            if(c.row == 1)  ++silent;
            if(c.row == 2)  ++control;
            if(c.row == 10) ++silent_printing;
        }
        CHECK(silent == 1);
        CHECK(control == 1);
        CHECK(silent_printing == 1);
    }
}

TEST_CASE("xtalk plan: the MUX_A3 cases move bit 3 and nothing else") {
    // Spec section 9's named red. Rows 5 and 6 are the bare address edge
    // across the moat: A3 is the ONLY bit that may differ, because the 4051
    // ignores it and the 4067's response is what row 6 separates from row 5.
    // A case that also moved an LED bit or an enable would be a different
    // experiment wearing this one's label.
    const uint32_t a3 = 8u << shell::kCouponChain.addr_shift;
    int seen = 0;
    for(int i = 0; i < shell::kXtalkCases; ++i) {
        const shell::XtalkCase& c = shell::kXtalkPlan[i];
        if(c.row != 5 && c.row != 6) continue;
        CAPTURE(i);
        CHECK((c.word_a ^ c.word_b) == a3);
        // Row 5 holds the 16:1 disabled, row 6 enables it. That is the whole
        // difference between "a bare edge crossing the moat" and "the 16:1
        // switching channel into ADC_9 while ADC_10 is read".
        const uint32_t en16 = shell::victim_enable_mask(0);
        CHECK(((c.word_a & en16) != 0u) == (c.row == 5));
        CHECK(c.needs_rv4 == (c.row == 6));
        ++seen;
    }
    CHECK(seen == 8);
}

TEST_CASE("xtalk plan: the LED cases move the whole LED field and only it") {
    const uint32_t leds = ((1u << shell::kCouponChain.led_bits) - 1u)
                          << shell::kCouponChain.led_shift;
    int seen = 0;
    for(int i = 0; i < shell::kXtalkCases; ++i) {
        const shell::XtalkCase& c = shell::kXtalkPlan[i];
        if(c.row != 7) continue;
        CAPTURE(i);
        CHECK((c.word_a ^ c.word_b) == leds);
        ++seen;
    }
    CHECK(seen == 2 * shell::kXtalkVictims);
}

TEST_CASE("xtalk plan: every Latch aggressor runs in both directions") {
    // Charge injection has a sign and di/dt has a sign. A disturbance that
    // flips with the edge is coupling; one that does not is the pulse
    // itself, and only both directions can tell them apart. So for every
    // aggressor case there must be a mirror with word_a and word_b swapped.
    for(int i = 0; i < shell::kXtalkCases; ++i) {
        const shell::XtalkCase& c = shell::kXtalkPlan[i];
        if(c.kind != shell::XtalkKind::Latch || c.row == 2) continue;
        CAPTURE(i);
        bool mirrored = false;
        for(int j = 0; j < shell::kXtalkCases; ++j) {
            const shell::XtalkCase& d = shell::kXtalkPlan[j];
            if(j != i && d.row == c.row && d.group == c.group
               && d.channel == c.channel && d.word_a == c.word_b
               && d.word_b == c.word_a)
                mirrored = true;
        }
        CHECK(mirrored);
    }
}

TEST_CASE("xtalk plan: the Static cases come in pairs, dark and lit") {
    for(int v = 0; v < shell::kXtalkVictims; ++v) {
        const shell::XtalkVictim& vv = shell::kXtalkVictimTable[v];
        CAPTURE(v);
        const uint32_t leds = ((1u << shell::kCouponChain.led_bits) - 1u)
                              << shell::kCouponChain.led_shift;
        int dark = 0, lit = 0;
        for(int i = 0; i < shell::kXtalkCases; ++i) {
            const shell::XtalkCase& c = shell::kXtalkPlan[i];
            if(c.kind != shell::XtalkKind::Static) continue;
            if(c.group != vv.group || c.channel != vv.channel) continue;
            if((c.word_a & leds) == 0u)    ++dark;
            if((c.word_a & leds) == leds)  ++lit;
        }
        CHECK(dark == 1);
        CHECK(lit == 1);
    }
}

TEST_CASE("xtalk plan: the table is the size the spec's ten rows add up to") {
    // 5 silent + 5 control + 6 MUX8_EN_N + 4 MUX16_EN_N + 4 A3-high
    // + 4 A3-low + 10 LED + 10 static + 5 shift-only + 5 silent-printing.
    CHECK(shell::kXtalkCases == 58);
    int by_row[11] = {};
    for(int i = 0; i < shell::kXtalkCases; ++i) ++by_row[shell::kXtalkPlan[i].row];
    CHECK(by_row[1] == 5);
    CHECK(by_row[2] == 5);
    CHECK(by_row[3] == 6);
    CHECK(by_row[4] == 4);
    CHECK(by_row[5] == 4);
    CHECK(by_row[6] == 4);
    CHECK(by_row[7] == 10);
    CHECK(by_row[8] == 10);
    CHECK(by_row[9] == 5);
    CHECK(by_row[10] == 5);
}

TEST_CASE("xtalk plan: only row 6 needs the pot that is not fitted") {
    for(int i = 0; i < shell::kXtalkCases; ++i) {
        const shell::XtalkCase& c = shell::kXtalkPlan[i];
        CAPTURE(i);
        CHECK(c.needs_rv4 == (c.row == 6));
    }
}

TEST_CASE("xtalk plan: kind and prints_inline follow the row, not the words") {
    // Rows 1, 9 and 10 all hold the victim at base(v) with word_a == word_b --
    // byte-identical chain words. Kind and prints_inline are the ONLY fields
    // that separate "no chain access at all" (Silent) from "16 bits shifted,
    // RCLK never pulsed" (ShiftOnly) from "as row 1, but PrintLine after every
    // grid point" (row 10's Silent + prints_inline). A kind mislabelled
    // between these three would still build a plausible-looking word.
    for(int i = 0; i < shell::kXtalkCases; ++i) {
        const shell::XtalkCase& c = shell::kXtalkPlan[i];
        CAPTURE(i);
        shell::XtalkKind expected = shell::XtalkKind::Latch;
        if(c.row == 1 || c.row == 10)      expected = shell::XtalkKind::Silent;
        else if(c.row == 8)                expected = shell::XtalkKind::Static;
        else if(c.row == 9)                expected = shell::XtalkKind::ShiftOnly;
        // rows 2..7 stay Latch
        CHECK(c.kind == expected);
        CHECK(c.prints_inline == (c.row == 10));
    }
}

TEST_CASE("xtalk plan: the word builder agrees with the shipping chain_word()") {
    // xtalk_word() has to re-implement chain_word()'s layout because
    // chain_word() is not constexpr and kXtalkPlan is built in the constant
    // evaluator -- but nothing else ties the two together, so a shift or a
    // polarity that moves in kCouponChain or in step_pattern() would move one
    // builder and not the other while every assertion that reads only
    // xtalk_plan.h's own constants kept passing. This is the one place that
    // checks against the scan's own word, not against itself.
    for(int v = 0; v < shell::kXtalkVictims; ++v) {
        const shell::XtalkVictim& vv = shell::kXtalkVictimTable[v];
        CAPTURE(v);
        const int step = shell::step_of(shell::kCouponChain, vv.group, vv.channel);
        REQUIRE(step >= 0);
        const shell::StepPattern sp = shell::step_pattern(shell::kCouponChain, step);
        const uint32_t shipping = shell::chain_word(shell::kCouponChain, sp, 0);
        const uint32_t local = shell::xtalk_word(vv, shell::other_enable_mask(vv.group));
        CHECK(local == shipping);
    }
}

TEST_CASE("xtalk plan: the scan settle delay is the audio block period") {
    // Read from shell/, not chosen here -- see the comment on
    // scan_settle_ns() and Task 5 step 2. The board's own numbers are
    // handed in at runtime; these are the arithmetic's own properties.
    CHECK(shell::scan_settle_ns(96, 48000) == 2000000u);
    CHECK(shell::scan_settle_ns(48, 48000) == 1000000u);
    CHECK(shell::scan_settle_ns(96, 96000) == 1000000u);
    // A board that answers nonsense must not produce a plausible duration.
    CHECK(shell::scan_settle_ns(96, 0) == 0u);
    CHECK(shell::scan_settle_ns(0, 48000) == 0u);
}

TEST_CASE("xtalk plan: the scan settle arithmetic does not overflow on the way") {
    // block_size * 1e9 leaves 32 bits for any block size above 4, so the
    // intermediate has to be 64 bit. With a 32-bit intermediate a 96-sample
    // block at 48 kHz comes back as 331 350 ns instead of 2 000 000 -- a
    // plausible-looking number, which is the worst kind, and one that would
    // put the criterion boundary INSIDE the grid and silently switch the
    // reader onto its other verdict branch.
    CHECK(shell::scan_settle_ns(96, 48000) == 2000000u);
    CHECK(shell::scan_settle_ns(1024, 48000) == 21333333u);
    CHECK(shell::scan_settle_ns(1, 1) == 1000000000u);
}

namespace {

// A summary that passes every gate, for tests that break exactly one thing.
shell::XtalkSummary clean_xtalk_summary() {
    shell::XtalkSummary s{};
    s.b0                   = 32;    // settle-measured.md section 5 measured 27..40
    s.lat_min_ns           = 700;
    s.lat_max_ns           = 800;
    s.lat_mean_ns          = 747;
    s.address_ok           = true;
    s.worst_control_delta  = 3;
    return s;
}

} // namespace

TEST_CASE("xtalk gates: a clean run passes all four") {
    const shell::XtalkGates g = shell::xtalk_gates(clean_xtalk_summary());
    CHECK(g.g2_floor);
    CHECK(g.g4_jitter);
    CHECK(g.g5_address);
    CHECK(g.g6_control);
    CHECK(g.ok());
}

TEST_CASE("xtalk gates: G2 is the settle probe's floor, unchanged") {
    shell::XtalkSummary s = clean_xtalk_summary();
    s.b0 = shell::kFloorMaxCounts;
    CHECK(shell::xtalk_gates(s).g2_floor);
    s.b0 = shell::kFloorMaxCounts + 1;
    CHECK_FALSE(shell::xtalk_gates(s).g2_floor);
    // -1 is the firmware's "every repeat timed out" sentinel, not a floor of
    // minus one count.
    s.b0 = -1;
    CHECK_FALSE(shell::xtalk_gates(s).g2_floor);
}

TEST_CASE("xtalk gates: G4 is the settle probe's jitter gate, unchanged") {
    shell::XtalkSummary s = clean_xtalk_summary();
    s.lat_min_ns = 700;
    s.lat_max_ns = 700 + shell::kJitterMaxNs;
    CHECK(shell::xtalk_gates(s).g4_jitter);
    s.lat_max_ns = 700 + shell::kJitterMaxNs + 1;
    CHECK_FALSE(shell::xtalk_gates(s).g4_jitter);
    // A negative mean means the conversion-time subtraction came out larger
    // than the span it was subtracted from, which invalidates every delay in
    // the run.
    s = clean_xtalk_summary();
    s.lat_mean_ns = -1;
    CHECK_FALSE(shell::xtalk_gates(s).g4_jitter);
}

TEST_CASE("xtalk gates: G5 refuses a run whose victims are not where the table says") {
    // A wrong word here makes every delta a measurement of nothing -- the
    // reading would be of some other channel, moving for some other reason,
    // and it would look exactly like a number.
    shell::XtalkSummary s = clean_xtalk_summary();
    s.address_ok = false;
    CHECK_FALSE(shell::xtalk_gates(s).g5_address);
    CHECK_FALSE(shell::xtalk_gates(s).ok());
}

TEST_CASE("xtalk gates: G6 refuses a control that is not a control") {
    // The bound is kSettleCounts, the same half-LSB-of-12-bit criterion the
    // aggressor verdict uses -- because if a latch pulse with NO bit change
    // already moves the reading by that much, the aggressor cases cannot be
    // read as differences at all.
    shell::XtalkSummary s = clean_xtalk_summary();
    s.worst_control_delta = shell::kSettleCounts;
    CHECK(shell::xtalk_gates(s).g6_control);
    s.worst_control_delta = shell::kSettleCounts + 1;
    CHECK_FALSE(shell::xtalk_gates(s).g6_control);
}

TEST_CASE("xtalk gates: G6 takes a magnitude, and a negative one is a fault") {
    // worst_control_delta is filled from max |mean_control(d) -
    // mean_silent(d)|, so it cannot be negative unless the firmware never
    // filled it. A gate that passed an unfilled field would pass a run in
    // which the control curve was never taken.
    shell::XtalkSummary s = clean_xtalk_summary();
    s.worst_control_delta = -1;
    CHECK_FALSE(shell::xtalk_gates(s).g6_control);
}

TEST_CASE("xtalk gates: ok() is the conjunction and nothing else") {
    // Each gate alone must be able to refuse the run. A fold that dropped
    // one would leave that gate printed and toothless, which is worse than
    // not having it.
    for(int which = 0; which < 4; ++which) {
        shell::XtalkSummary s = clean_xtalk_summary();
        if(which == 0) s.b0 = shell::kFloorMaxCounts + 1;
        if(which == 1) s.lat_mean_ns = -1;
        if(which == 2) s.address_ok = false;
        if(which == 3) s.worst_control_delta = shell::kSettleCounts + 1;
        CAPTURE(which);
        CHECK_FALSE(shell::xtalk_gates(s).ok());
    }
}
