#include <doctest/doctest.h>
#include <cmath>
#include "mod/lane.h"
using namespace spky;

// Per-sample-vs-tick equivalence harness (spec 2026-07-19 mod-plane-control-
// rate, "Testing 1"). ref is driven by 96x process(), dut by one tick();
// both start from identical seed + config, so their private RNG streams are
// the same stream. Any skipped boundary or reordered draw desyncs the
// streams and explodes the target comparison within a few cycles.
namespace {
constexpr float kSr   = 48000.f;
constexpr int   kTick = ModLane::kTickInterval;

struct TickPair {
    ModLane ref, dut;
    float ref_out = 0.f, dut_out = 0.f;
    int   ref_fires = 0;
    bool  dut_fired = false;

    void boot(uint32_t seed, void (*cfg)(ModLane&)) {
        ref.init(kSr, seed); cfg(ref);
        dut.init(kSr, seed); cfg(dut);
    }
    void advance_one_tick() {
        ref_fires = 0;
        for (int i = 0; i < kTick; ++i) {
            ref_out = ref.process();
            if (ref.fired()) ++ref_fires;
        }
        dut_out = dut.tick();
        dut_fired = dut.fired();
    }
};
} // namespace

TEST_CASE("tick: STEP S&H targets and fires match the per-sample path exactly") {
    // shape 1.0 returns the S&H operand EXACTLY (entropy-sequencer fix), so
    // the boundary target is phase-independent: bit-equal across both paths.
    TickPair tp;
    tp.boot(42u, [](ModLane& l) {
        l.set_range(1.f); l.set_shape(1.f); l.set_smooth(0.f);
        l.set_step(true, 8); l.set_rate_hz(2.3f);   // boundary every ~2609 smp
    });
    // One caveat: the two paths accumulate phase differently (96 rounded
    // adds vs one fused product), so a boundary landing within float-eps of
    // a tick edge can be detected one tick apart. That skew self-corrects on
    // the next tick and skips no RNG draw; the guard below tolerates exactly
    // that -- a real RNG desync would never re-converge and still fails.
    // Each straddle shows as TWO adjacent parity mismatches (early window +
    // missing next window), hence the doubled skew_events budget.
    int skew = 0, skew_events = 0;
    for (int t = 0; t < 400; ++t) {
        tp.advance_one_tick();
        if ((tp.ref_fires > 0) != tp.dut_fired) { skew = 1; ++skew_events; continue; }
        if (skew > 0) { --skew; continue; }
        CHECK(tp.dut.target() == tp.ref.target());
        CHECK(tp.dut_out == tp.ref_out);            // smooth 0 = passthrough
    }
    CHECK(skew_events <= 4);   // isolated float coincidences, never systematic
}

TEST_CASE("tick: GROW mutation dice stay on the same RNG stream") {
    // variation > 0 draws dice + walk deltas per boundary/wrap. 300 ticks
    // (~7 cycles) of exact target equality proves no draw was skipped.
    TickPair tp;
    tp.boot(7u, [](ModLane& l) {
        l.set_range(1.f); l.set_shape(1.f); l.set_smooth(0.f);
        l.set_step(true, 8); l.set_rate_hz(3.7f);
        l.set_variation(0.7f);
    });
    // Same tick-edge skew guard as the S&H case: seed 7 / 3.7 Hz hits one
    // straddle (~tick 250). The draw is delayed one tick, never skipped;
    // exact equality must resume immediately after.
    int skew = 0, skew_events = 0;
    for (int t = 0; t < 300; ++t) {
        tp.advance_one_tick();
        if ((tp.ref_fires > 0) != tp.dut_fired) { skew = 1; ++skew_events; continue; }
        if (skew > 0) { --skew; continue; }
        CHECK(tp.dut.target() == tp.ref.target());
    }
    CHECK(skew_events <= 4);
}

TEST_CASE("tick: RENEW walk regen stays on the same RNG stream") {
    TickPair tp;
    tp.boot(11u, [](ModLane& l) {
        l.set_range(1.f); l.set_shape(1.f); l.set_smooth(0.f);
        l.set_step(true, 8); l.set_rate_hz(3.1f);
        l.set_variation(-0.8f);
    });
    int skew = 0, skew_events = 0;
    for (int t = 0; t < 300; ++t) {
        tp.advance_one_tick();
        if ((tp.ref_fires > 0) != tp.dut_fired) { skew = 1; ++skew_events; continue; }
        if (skew > 0) { --skew; continue; }
        CHECK(tp.dut.target() == tp.ref.target());
    }
    CHECK(skew_events <= 4);
}

TEST_CASE("tick: FLOW output tracks the per-sample path") {
    // Continuous FLOW: same end phase modulo float accumulation -- the tick
    // path adds one fused product where the reference adds 96 rounded
    // increments. Loose epsilon, wrap-fire parity exact.
    TickPair tp;
    tp.boot(3u, [](ModLane& l) {
        l.set_range(1.f); l.set_shape(0.3f); l.set_smooth(0.f);
        l.set_rate_hz(1.9f);
    });
    for (int t = 0; t < 500; ++t) {
        tp.advance_one_tick();
        CHECK((tp.ref_fires > 0) == tp.dut_fired);
        CHECK(tp.dut_out == doctest::Approx(tp.ref_out).epsilon(0.01));
    }
}

TEST_CASE("tick: SMOOTH slew matches outside a post-boundary blackout") {
    // The tick coefficient is the exact 96-sample compound of the per-sample
    // coefficient, so held segments converge identically. A boundary lands
    // mid-interval for the reference but takes effect at the tick edge for
    // the dut -- allow a 2-tick blackout after each fire, then require the
    // paths to agree again.
    TickPair tp;
    tp.boot(9u, [](ModLane& l) {
        l.set_range(1.f); l.set_shape(1.f); l.set_smooth(0.5f);
        l.set_step(true, 8); l.set_rate_hz(2.3f);
    });
    int blackout = 2;
    for (int t = 0; t < 400; ++t) {
        tp.advance_one_tick();
        if (tp.ref_fires > 0 || tp.dut_fired) { blackout = 2; continue; }
        if (blackout > 0) { --blackout; continue; }
        CHECK(tp.dut_out == doctest::Approx(tp.ref_out).epsilon(0.02));
    }
}

TEST_CASE("tick: multiple boundaries inside one interval are replayed in order") {
    // 500 Hz at 8 steps = one boundary every 12 samples = 8 per tick. With
    // GROW dice active, a single skipped or reordered boundary desyncs the
    // RNG stream and the exact target comparison fails within a few ticks.
    TickPair tp;
    tp.boot(21u, [](ModLane& l) {
        l.set_range(1.f); l.set_shape(1.f); l.set_smooth(0.f);
        l.set_step(true, 8); l.set_rate_hz(500.f);
        l.set_variation(0.6f);
    });
    // A boundary landing within float-eps of a tick edge shifts that one
    // boundary into the neighbouring window: the straddle tick compares
    // different "last boundary" targets, then equality resumes. Tolerate
    // isolated straddle ticks, never sustained divergence -- a skipped or
    // reordered boundary desyncs the RNG stream permanently and blows the
    // mismatch budget.
    int mismatch = 0;
    for (int t = 0; t < 200; ++t) {
        tp.advance_one_tick();
        if (tp.dut.target() != tp.ref.target()) { ++mismatch; continue; }
    }
    CHECK(mismatch <= 2);                          // isolated straddles only
    CHECK(tp.dut.target() == tp.ref.target());     // re-converged at the end
}

TEST_CASE("tick: wrap events land before the new cycle's step 0") {
    // variation -1 makes the RENEW walk-regen dice certain (v^2 = 1), so the
    // whole _seq walk regenerates at EVERY wrap. Step 0's target right after
    // the seam must sample the NEW walk -- if tick() ran the step-0 boundary
    // before _wrap_events(), it would sample the old walk and diverge from
    // the per-sample reference immediately.
    TickPair tp;
    tp.boot(33u, [](ModLane& l) {
        l.set_range(1.f); l.set_shape(1.f); l.set_smooth(0.f);
        l.set_step(true, 8); l.set_rate_hz(4.3f);
        l.set_variation(-1.f);
    });
    int skew = 0, skew_events = 0;
    for (int t = 0; t < 300; ++t) {
        tp.advance_one_tick();
        if ((tp.ref_fires > 0) != tp.dut_fired) { skew = 1; ++skew_events; continue; }
        if (skew > 0) { --skew; continue; }
        CHECK(tp.dut.target() == tp.ref.target());
    }
    CHECK(skew_events <= 4);
}

TEST_CASE("tick: SPOT kick equivalence at tick granularity") {
    // Kick applied to both paths at a tick edge (the only place Center can
    // apply it in production -- SPOT runs on the control tick).
    TickPair tp;
    tp.boot(5u, [](ModLane& l) {
        l.set_range(1.f); l.set_shape(0.4f); l.set_smooth(0.f);
        l.set_step(true, 8); l.set_rate_hz(2.9f);
    });
    for (int t = 0; t < 50; ++t) tp.advance_one_tick();
    tp.ref.kick(0.3f, 0.2f);
    tp.dut.kick(0.3f, 0.2f);
    int blackout = 0;
    for (int t = 0; t < 400; ++t) {
        tp.advance_one_tick();
        if (tp.ref_fires > 0 || tp.dut_fired) { blackout = 2; continue; }
        if (blackout > 0) { --blackout; continue; }
        // shape 0.4 is phase-dependent: boundary targets differ by the
        // detection-overshoot phase (< 1 sample) -- loose but real bound.
        CHECK(tp.dut_out == doctest::Approx(tp.ref_out).epsilon(0.05));
    }
}

TEST_CASE("tick: SETTLE glides the audible phase the same way") {
    // Build up EVOLVE walks first (variation > 0), then settle both paths and
    // compare the audible phase while the glide runs (tau 0.3 s, ctr 1 s).
    TickPair tp;
    tp.boot(13u, [](ModLane& l) {
        l.set_range(1.f); l.set_shape(0.5f); l.set_smooth(0.f);
        l.set_rate_hz(2.f);
        l.set_variation(0.8f);
    });
    for (int t = 0; t < 1500; ++t) tp.advance_one_tick();   // ~3 s of walk
    tp.ref.settle();
    tp.dut.settle();
    for (int t = 0; t < 600; ++t) {                          // ~1.2 s glide
        tp.advance_one_tick();
        // circular distance: phases straddling the 1.0 wrap must not read
        // as a full-cycle disagreement (0.999 vs 0.001 is 0.002 apart)
        float d = std::fabs(tp.dut.phase_eff() - tp.ref.phase_eff());
        if (d > 0.5f) d = 1.f - d;
        CHECK(d < 0.01f);
    }
}

TEST_CASE("tick: shuffled odd grid and mid-pair target change match process") {
    TickPair tp;
    tp.boot(17u, [](ModLane& l) {
        l.set_range(1.f); l.set_shape(1.f); l.set_smooth(0.f);
        l.set_step(true, 5); l.set_rate_hz(47.f);
        l.set_shuffle(0.6f);
    });

    int skew_events = 0;
    int full_state_checks = 0;
    bool reconverge_next = false;
    for (int t = 0; t < 80; ++t) {
        tp.advance_one_tick();
        if (t == 1) {
            REQUIRE(tp.ref.cur_step() == 1);
            REQUIRE(tp.dut.cur_step() == 1);
            tp.ref.set_shuffle(1.f);
            tp.dut.set_shuffle(1.f);
        }

        const bool fire_mismatch = (tp.ref_fires > 0) != tp.dut_fired;
        if (reconverge_next) {
            INFO("reconvergence t=", t);
            CHECK(tp.dut.cur_step() == tp.ref.cur_step());
            CHECK(tp.dut.target() == tp.ref.target());
            CHECK(tp.dut_out == tp.ref_out);
            float d = std::fabs(tp.dut.phase_eff() - tp.ref.phase_eff());
            if (d > 0.5f) d = 1.f - d;
            CHECK(d < 0.01f);
            reconverge_next = false;
            ++full_state_checks;
            continue;
        }

        if (fire_mismatch) {
            ++skew_events;
            CHECK(skew_events <= 2);
            reconverge_next = true;
            continue;
        }

        INFO("t=", t, " ref_step=", tp.ref.cur_step(),
             " dut_step=", tp.dut.cur_step(),
             " ref_fires=", tp.ref_fires, " dut_fired=", tp.dut_fired,
             " ref_phase=", tp.ref.phase(), " dut_phase=", tp.dut.phase());
        CHECK(tp.dut.cur_step() == tp.ref.cur_step());
        CHECK(tp.dut.target() == tp.ref.target());
        CHECK(tp.dut_out == tp.ref_out);
        float d = std::fabs(tp.dut.phase_eff() - tp.ref.phase_eff());
        if (d > 0.5f) d = 1.f - d;
        CHECK(d < 0.01f);
        ++full_state_checks;
    }
    CHECK(skew_events <= 2);
    CHECK_FALSE(reconverge_next);
    CHECK(full_state_checks >= 78);
}

TEST_CASE("tick: exact-endpoint live shuffle latches the same even-step pair") {
    TickPair tp;
    tp.boot(23u, [](ModLane& l) {
        l.set_range(1.f); l.set_shape(1.f); l.set_smooth(0.f);
        l.set_step(true, 5); l.set_rate_hz(125.f); // step = 48 samples
        l.set_shuffle(0.f);
    });

    // Step 2 is mathematically at sample 96, but repeated float addition lands
    // a hair below phase 0.4. Both paths therefore still report odd step 1
    // when the control update is applied at that observation boundary.
    tp.advance_one_tick();
    REQUIRE(tp.ref.cur_step() == 1);
    REQUIRE(tp.dut.cur_step() == 1);

    tp.ref.set_shuffle(1.f);
    tp.dut.set_shuffle(1.f);

    // Both enter even step 2 with the new target. Probe phase 0.63 proves
    // that both paths latched full shuffle (step 2 rather than straight 3).
    tp.advance_one_tick();
    CHECK((tp.ref_fires > 0) == tp.dut_fired);
    CHECK(tp.ref.cur_step() == 3);
    CHECK(tp.dut.cur_step() == 3);
    CHECK(tp.ref.step_at_phase(0.63f) == 2);
    CHECK(tp.dut.step_at_phase(0.63f) == 2);
    CHECK(tp.dut.target() == tp.ref.target());
    CHECK(tp.dut_out == tp.ref_out);

    // Both then traverse the same odd boundary, final straight step and wrap.
    tp.advance_one_tick();
    CHECK((tp.ref_fires > 0) == tp.dut_fired);
    CHECK(tp.ref.cur_step() == 0);
    CHECK(tp.dut.cur_step() == 0);
    CHECK(tp.dut.target() == tp.ref.target());
    CHECK(tp.dut_out == tp.ref_out);
}

TEST_CASE("tick: exactly representable endpoint latches before live shuffle target") {
    TickPair tp;
    tp.boot(29u, [](ModLane& l) {
        l.set_range(1.f); l.set_shape(1.f); l.set_smooth(0.f);
        l.set_step(true, 8); l.set_rate_hz(375.f); // phase_inc = 1/128
        l.set_shuffle(0.f);
    });

    // After 96 samples phase is exactly 0.75, the even step-6 boundary.
    tp.advance_one_tick();
    REQUIRE(tp.ref.phase() == 0.75f);
    REQUIRE(tp.dut.phase() == 0.75f);
    REQUIRE(tp.ref.cur_step() == 6);
    REQUIRE(tp.dut.cur_step() == 6);

    tp.ref.set_shuffle(1.f);
    tp.dut.set_shuffle(1.f);

    // Step 6 already latched straight timing, so a probe between the straight
    // and shuffled odd boundaries remains in step 7 for both paths.
    CHECK(tp.ref.step_at_phase(0.89f) == 7);
    CHECK(tp.dut.step_at_phase(0.89f) == 7);

    // The next even step is cycle step 0, which latches full shuffle. Both
    // paths traverse the same wrap and subsequent pair sequence.
    tp.advance_one_tick();
    CHECK((tp.ref_fires > 0) == tp.dut_fired);
    CHECK(tp.ref.cur_step() == 4);
    CHECK(tp.dut.cur_step() == 4);
    CHECK(tp.ref.step_at_phase(0.89f) == 6);
    CHECK(tp.dut.step_at_phase(0.89f) == 6);
    CHECK(tp.dut.target() == tp.ref.target());
    CHECK(tp.dut_out == tp.ref_out);
}

TEST_CASE("tick: warped multi-edge wrap endpoint keeps live shuffle pair aligned") {
    TickPair tp;
    tp.boot(31u, [](ModLane& l) {
        l.set_range(1.f); l.set_shape(1.f); l.set_smooth(0.f);
        l.set_step(true, 2); l.set_rate_hz(250.f);
        l.set_shuffle(0.2f);
    });

    // One tick is mathematically two cycles. The warped odd edges occur at
    // fractional sample 25.6 in each cycle, and repeated process additions
    // finish just below the second wrap on step 1.
    tp.advance_one_tick();
    REQUIRE(tp.ref.phase() < 1.f);
    REQUIRE(tp.ref.phase() > 0.999f);
    REQUIRE(tp.dut.phase() >= 0.f);
    REQUIRE(tp.dut.phase() < 1.f);
    REQUIRE(tp.dut.phase() > 0.999f);
    CHECK(std::fabs(tp.dut.phase() - tp.ref.phase()) < 0.01f);
    REQUIRE(tp.ref.cur_step() == 1);
    REQUIRE(tp.dut.cur_step() == 1);

    tp.ref.set_shuffle(1.f);
    tp.dut.set_shuffle(1.f);

    // Both enter the pending wrap after the update and latch the same full
    // shuffle value for step 0. Repeated whole-window comparisons prove the
    // step/RNG/target sequence remains aligned across subsequent wraps.
    for (int t = 0; t < 3; ++t) {
        tp.advance_one_tick();
        INFO("t=", t, " ref_step=", tp.ref.cur_step(),
             " dut_step=", tp.dut.cur_step(),
             " ref_phase=", tp.ref.phase(), " dut_phase=", tp.dut.phase());
        CHECK((tp.ref_fires > 0) == tp.dut_fired);
        CHECK(tp.dut.phase() >= 0.f);
        CHECK(tp.dut.phase() < 1.f);
        CHECK(tp.ref.phase() > 0.999f);
        CHECK(tp.dut.phase() > 0.999f);
        CHECK(std::fabs(tp.dut.phase() - tp.ref.phase()) < 0.01f);
        CHECK(tp.ref.cur_step() == 1);
        CHECK(tp.dut.cur_step() == 1);
        CHECK(tp.ref.step_at_phase(0.6f) == 0);
        CHECK(tp.dut.step_at_phase(0.6f) == 0);
        CHECK(tp.dut.target() == tp.ref.target());
        CHECK(tp.dut_out == tp.ref_out);
    }
}
