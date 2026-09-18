// The codec-tone probe's frequency and level ladder, its phase arithmetic
// and the round-one aggressors it fires inside a conversion window. Derived
// from the codec-tone spec sections 3, 5 and 6.
#include <doctest/doctest.h>
#include "../shell/tone_plan.h"
#include "../shell/xtalk_plan.h"
#include "../shell/settle_plan.h"

TEST_CASE("tone rows: every frequency is below half the sample rate") {
    // Spec section 10's named red. A row above Nyquist does not produce the
    // tone it claims -- it produces an alias at sr - f, at a frequency
    // nothing in the table names, and the capacitive slope read off three
    // such rows would be a slope through a fiction.
    for(int i = 0; i < shell::kToneRowCount; ++i) {
        CAPTURE(i);
        // The static row is skipped here: f_hz == 0 is a constant, not a
        // frequency, and a constant has no Nyquist limit -- it cannot alias.
        if(shell::kToneRows[i].f_hz == 0) continue;
        CHECK(shell::kToneRows[i].f_hz > 0);
        CHECK(shell::kToneRows[i].f_hz * 2 < shell::kToneSampleRateHz);
    }
}

TEST_CASE("tone rows: no row asks for more than full scale") {
    for(int i = 0; i < shell::kToneRowCount; ++i) {
        CAPTURE(i);
        CHECK(shell::kToneRows[i].dbfs <= 0);
        CHECK(shell::kToneRows[i].dbfs >= shell::kToneDbfsFloor);
    }
}

TEST_CASE("tone rows: the frequency ladder spans a decade and a half, at three points") {
    // Spec section 3: capacitive coupling grows as C dV/dt, i.e. linearly
    // with f at fixed level; resistive or ground coupling does not. Two
    // points give a line through anything; three give a slope that can be
    // wrong. Fewer than three distinct frequencies would make the whole
    // frequency argument unfalsifiable.
    //
    // The static row is skipped: it is not a rung of the frequency ladder
    // whose slope these three points establish, it is a fourth, unrelated
    // measurement (spec section 9's DC-coupled branch), so it is excluded
    // from the distinct-frequency count taken over the ladder proper.
    int distinct = 0;
    for(int i = 0; i < shell::kToneRowCount; ++i) {
        if(shell::kToneRows[i].f_hz == 0) continue;
        bool seen = false;
        for(int j = 0; j < i; ++j) {
            if(shell::kToneRows[j].f_hz == 0) continue;
            if(shell::kToneRows[j].f_hz == shell::kToneRows[i].f_hz) seen = true;
        }
        if(!seen) ++distinct;
    }
    CHECK(distinct == 3);
}

TEST_CASE("tone rows: the level ladder is a linearity check, at three points") {
    int distinct = 0;
    for(int i = 0; i < shell::kToneRowCount; ++i) {
        bool seen = false;
        for(int j = 0; j < i; ++j)
            if(shell::kToneRows[j].dbfs == shell::kToneRows[i].dbfs) seen = true;
        if(!seen) ++distinct;
    }
    CHECK(distinct == 3);
}

TEST_CASE("tone rows: the table is the full cross product, plus the static row when it exists") {
    // Nine rows of 3 x 3, and one static row iff the output is DC-coupled --
    // which Task 1 measured and SHELL_TONE_DC carries.
    const int expected = 9 + (shell::kToneHasStaticRow ? 1 : 0);
    CHECK(shell::kToneRowCount == expected);
}

TEST_CASE("tone rows: a row below the coupling corner is flagged, not deleted") {
    // Spec section 9's other branch. A row below an AC-coupled output's
    // corner still runs -- its result is a real measurement of what that
    // output does at that frequency -- but the reader must not read its
    // level as part of the capacitive slope, because the attenuation is the
    // coupling network's and not the board's.
    for(int i = 0; i < shell::kToneRowCount; ++i) {
        CAPTURE(i);
        const int f = shell::kToneRows[i].f_hz;
        // f > 0 in the condition, and it is not redundant: the static row is
        // f_hz = 0, and a bare `f < corner` would flag a constant as being
        // below the corner of a network a constant does not pass through at
        // all. A DC-coupled output is the only reason that row exists.
        CHECK(shell::kToneRows[i].below_corner
              == (shell::kToneCornerHz > 0 && f > 0 && f < shell::kToneCornerHz));
    }
}

TEST_CASE("tone phase: sixteen points span exactly one period") {
    CHECK(shell::kTonePhasePoints == 16);
    CHECK(shell::phase_target(0) == 0u);
    // The last point must be one step short of a full turn, not at it: a
    // point at a full turn is the same instant as point 0 and the grid would
    // measure fifteen phases while reporting sixteen.
    CHECK(shell::phase_target(shell::kTonePhasePoints)
          == shell::kTonePhaseScale);
    for(int i = 1; i < shell::kTonePhasePoints; ++i) {
        CAPTURE(i);
        CHECK(shell::phase_target(i) > shell::phase_target(i - 1));
        CHECK(shell::phase_target(i) - shell::phase_target(i - 1)
              == shell::kTonePhaseScale / shell::kTonePhasePoints);
    }
}

TEST_CASE("tone phase: one second of steps comes back to where it started") {
    // The accumulator is what the callback advances per sample. If it does
    // not come back, the phase the foreground computes drifts against the
    // tone the codec is emitting and every delta(phi) is smeared across the
    // grid.
    //
    // ONE SECOND and not one period, and the difference matters: 5 kHz does
    // not divide 48 kHz into a whole number of samples (9.6 of them), so
    // "one period of samples" is not a thing the integer accumulator can be
    // asked about. One second is exactly f_hz periods for any integer f, and
    // exactly sr samples.
    for(int i = 0; i < shell::kToneRowCount; ++i) {
        const shell::ToneRow& r = shell::kToneRows[i];
        if(r.f_hz == 0) continue;                 // the static row has no phase
        CAPTURE(i);
        const uint32_t step = shell::phase_step_per_sample(r.f_hz,
                                                           shell::kToneSampleRateHz);
        const uint32_t after
            = shell::phase_advance(0u, step, shell::kToneSampleRateHz);
        // Not exactly zero: phase_step_per_sample truncates, by less than one
        // phase unit per sample, so a second accumulates less than sr units
        // of error. That bound is derived from the truncation itself and not
        // chosen. Distance to the nearest turn, because "just short of a full
        // turn" and "just past zero" are the same place.
        const uint32_t err = (after < shell::kTonePhaseScale - after)
                                 ? after
                                 : (shell::kTonePhaseScale - after);
        CHECK(err < static_cast<uint32_t>(shell::kToneSampleRateHz));
        // And that error must stay far inside one grid point, or a phase
        // point measured at the start of a case is a different phase by the
        // end of it.
        CHECK(err < shell::kTonePhaseScale / shell::kTonePhasePoints / 4u);
    }
}

TEST_CASE("tone phase: the crossing detector fires once per period, not once per sample") {
    // The failure this catches is a detector written as `now >= target`: it
    // fires on EVERY sample after the first crossing, so the spin returns
    // immediately with whatever phase it happens to be at, and the phase
    // grid measures one phase sixteen times. Over ten periods that detector
    // fires hundreds of times and a correct one fires ten.
    //
    // Nine to eleven rather than exactly ten: phase_step_per_sample
    // truncates, so a period is a fraction of a sample longer than
    // sr / f_hz and ten of them can straddle a boundary either way.
    const uint32_t step = shell::phase_step_per_sample(1000,
                                                       shell::kToneSampleRateHz);
    const int per_period = shell::kToneSampleRateHz / 1000;
    for(int idx = 0; idx < shell::kTonePhasePoints; ++idx) {
        const uint32_t target = shell::phase_target(idx);
        CAPTURE(idx);
        int      hits = 0;
        uint32_t now  = 0u;
        for(int s = 0; s < 10 * per_period; ++s) {
            const uint32_t prev = now;
            now = shell::phase_advance(now, step, 1);
            if(shell::phase_reached(prev, now, target)) ++hits;
        }
        CHECK(hits >= 9);
        CHECK(hits <= 11);
    }
}

TEST_CASE("tone phase: the crossing detector never makes the spin wait two periods") {
    // Spec section 10: "the crossing detector never waits more than two
    // periods". Starting from every phase in the grid, the next crossing of
    // every target must arrive inside one turn of the accumulator.
    //
    // The bound is per_period + 2 and not + 1: a turn takes
    // ceil(kTonePhaseScale / step) samples, and because step is truncated
    // downward that is up to one sample more than sr / f_hz. At 1 kHz it is
    // 49 against a per_period of 48.
    //
    // This is also the test that catches the OTHER wrap failure -- a
    // detector that misses the crossing which happens across the wrap never
    // fires for a target just above it, and this loop then runs out rather
    // than breaking.
    const uint32_t step = shell::phase_step_per_sample(1000,
                                                       shell::kToneSampleRateHz);
    const int per_period = shell::kToneSampleRateHz / 1000;
    for(int from = 0; from < shell::kTonePhasePoints; ++from) {
        for(int idx = 0; idx < shell::kTonePhasePoints; ++idx) {
            CAPTURE(from);
            CAPTURE(idx);
            const uint32_t target = shell::phase_target(idx);
            uint32_t now    = shell::phase_target(from);
            bool     fired  = false;
            int      waited = 0;
            for(; waited <= per_period + 2; ++waited) {
                const uint32_t prev = now;
                now = shell::phase_advance(now, step, 1);
                if(shell::phase_reached(prev, now, target)) { fired = true; break; }
            }
            CHECK(fired);
            CHECK(waited <= per_period + 2);
        }
    }
}

TEST_CASE("tone WIN cases: every one points at a real round-one aggressor") {
    // The index is into kXtalkPlan, so a row added there would silently
    // repoint it. The table carries what it expects to find and this
    // assertion is what makes a shifted index a failure rather than a
    // measurement of a different aggressor under the right label.
    for(int i = 0; i < shell::kToneWinCaseCount; ++i) {
        const shell::ToneWinCase& w = shell::kToneWinCases[i];
        CAPTURE(i);
        REQUIRE(w.xtalk_case >= 0);
        REQUIRE(w.xtalk_case < shell::kXtalkCases);
        const shell::XtalkCase& c = shell::kXtalkPlan[w.xtalk_case];
        CHECK(c.row     == w.want_row);
        CHECK(c.group   == w.want_group);
        CHECK(c.channel == w.want_channel);
        // It must be an EVENT: a Silent or Static case has nothing to fire
        // inside a sampling window.
        CHECK(c.kind == shell::XtalkKind::Latch);
        CHECK(c.word_a != c.word_b);
        // And it must still hold the victim still -- round one's assertion,
        // reused, because this sequence changes when the edge lands and
        // nothing else.
        const uint32_t hold = shell::victim_address_mask(c.group)
                              | shell::victim_enable_mask(c.group);
        CHECK((c.word_a & hold) == (c.word_b & hold));
    }
}

TEST_CASE("tone WIN cases: MUX8_EN_N against REF_A is there in both directions") {
    // Spec section 6: it runs "for MUX8_EN_N against REF_A regardless",
    // because that is the case the moat crossing is most directly in. Both
    // directions, because charge injection has a sign.
    int found = 0;
    for(int i = 0; i < shell::kToneWinCaseCount; ++i) {
        const shell::ToneWinCase& w = shell::kToneWinCases[i];
        if(w.want_row == 3 && w.want_group == 0 && w.want_channel == 8) ++found;
    }
    CHECK(found == 2);
}

TEST_CASE("tone WIN cases: the window grid meets round one's grid end to end") {
    // Spec section 6: d_before_end runs 0..12800 ns in 200 ns steps, the
    // same 65 points, so round one's d AFTER the edge continues where this
    // one's d_before_end stops. A different step or count would make the two
    // curves uncomparable, which is the only reason to run this sequence on
    // the same aggressors at all.
    CHECK(shell::kToneWinPoints == shell::kGridPoints);
    CHECK(shell::kToneWinStepNs == shell::kGridStepNs);
    CHECK(shell::tone_win_ns(0) == 0u);
    CHECK(shell::tone_win_ns(shell::kToneWinPoints - 1) == 12800u);
}

TEST_CASE("tone WIN cases: the window is long enough to hold the whole grid") {
    // The 387.5-cycle rung gives about 63 us of acquisition at the measured
    // 6.146 MHz (settle-measured.md section 7). The grid reaches 12.8 us
    // back from the end of it, so the edge always lands inside the window --
    // an edge fired before the window opened would be round one's
    // measurement again, at a delay nobody commanded.
    CHECK(shell::kToneWinRung == 6);                    // 387.5 cycles
    CHECK(shell::kSamplingLadderTenths[shell::kToneWinRung] == 3875);
    // Derived, not measured, and the firmware checks the real number against
    // it at runtime: 387.5 / 6.146 MHz = 63.05 us.
    CHECK(shell::kToneWinWindowNsNominal > 12800u * 4u);
}
