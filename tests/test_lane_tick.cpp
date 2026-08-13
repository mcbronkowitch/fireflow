#include <doctest/doctest.h>
#include <cmath>
#include <cstring>
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
    bool  ref_wrapped = false;
    bool  dut_wrapped = false;

    void boot(uint32_t seed, void (*cfg)(ModLane&)) {
        ref.init(kSr, seed); cfg(ref);
        dut.init(kSr, seed); cfg(dut);
    }
    void advance_one_tick() {
        ref_fires = 0;
        ref_wrapped = false;
        for (int i = 0; i < kTick; ++i) {
            ref_out = ref.process();
            if (ref.fired()) ++ref_fires;
            if (ref.wrapped()) ref_wrapped = true;
        }
        dut_out = dut.tick();
        dut_fired = dut.fired();
        dut_wrapped = dut.wrapped();
    }
    void boot_song(uint32_t seed) {
        auto prepare = [seed](ModLane& lane) {
            lane.set_melodic(true);
            lane.set_step(true, 8);
            lane.set_form(Principle::Hierarchical);
            lane.set_song(SongMode::AAAB);
            lane.init(kSr, seed);
            lane.set_range(1.f);
            lane.set_shape(1.f);
            lane.set_smooth(0.f);
            lane.set_density(1.f);
            lane.set_rate_hz(7.3f);
        };
        prepare(ref);
        prepare(dut);
    }
    // FLOW melody-mode config, mirroring tests/test_flow_melody.cpp's
    // make_flow_melody_lane: set_step(false, steps) + set_flow_melody(true)
    // AFTER init(), unlike boot_song's STEP config above.
    void boot_flow_melody(uint32_t seed, float hz = 1.f, float variation = 0.f) {
        auto prepare = [seed, hz, variation](ModLane& lane) {
            lane.set_melodic(true);
            lane.set_step(false, 8);
            lane.set_form(Principle::Hierarchical);
            lane.set_song(SongMode::AAAB);
            lane.init(kSr, seed);
            lane.set_flow_melody(true);
            lane.set_range(1.f);
            lane.set_shape(1.f);
            lane.set_smooth(0.f);
            lane.set_density(1.f);
            lane.set_variation(variation);
            lane.set_rate_hz(hz);
        };
        prepare(ref);
        prepare(dut);
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
    // The two paths still accumulate phase differently (96 rounded adds vs one
    // fused product), but both now accumulate in DOUBLE (spec 2026-08-12
    // modulation-pace), so the window in which a boundary can land on
    // different sides of a tick edge for the two paths shrank by about eight
    // orders of magnitude. Measured over these 400 ticks: 0 straddles, where
    // the float accumulator produced 0 here and 2 in the GROW case below.
    //
    // The guard is kept, not the budget: if a straddle ever returns it is
    // reported as one number instead of an avalanche of target mismatches, and
    // it self-corrects on the next tick without skipping an RNG draw. But the
    // gate asserts the measured value, which is zero -- a real RNG desync
    // never re-converges and would blow this instantly.
    int skew = 0, skew_events = 0;
    for (int t = 0; t < 400; ++t) {
        tp.advance_one_tick();
        if ((tp.ref_fires > 0) != tp.dut_fired) { skew = 1; ++skew_events; continue; }
        if (skew > 0) { --skew; continue; }
        CHECK(tp.dut.target() == tp.ref.target());
        CHECK(tp.dut_out == tp.ref_out);            // smooth 0 = passthrough
    }
    CHECK(skew_events == 0);   // measured, double accumulation (was <= 4)
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
    // Same tick-edge skew guard as the S&H case. In float, seed 7 / 3.7 Hz was
    // the one case in this file that actually straddled: 2 skew events around
    // tick 250. Double accumulation removes them -- measured 0 -- so this case
    // is now the direct evidence that the straddles were the accumulator's
    // rounding and not a boundary the tick path handles differently.
    int skew = 0, skew_events = 0;
    for (int t = 0; t < 300; ++t) {
        tp.advance_one_tick();
        if ((tp.ref_fires > 0) != tp.dut_fired) { skew = 1; ++skew_events; continue; }
        if (skew > 0) { --skew; continue; }
        CHECK(tp.dut.target() == tp.ref.target());
    }
    CHECK(skew_events == 0);   // measured: 2 in float, 0 in double
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
    CHECK(skew_events == 0);   // measured, double accumulation (was <= 4)
}

TEST_CASE("tick: FLOW output tracks the per-sample path") {
    // Continuous FLOW: same end phase modulo accumulation order -- the tick
    // path adds one fused product where the reference adds 96 rounded
    // increments, both in double. Loose epsilon, wrap-fire parity exact.
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
    // A boundary landing within accumulator-eps of a tick edge shifts that one
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

TEST_CASE("tick: shuffled high-rate wraps replay every intermediate boundary") {
    // Five steps plus full shuffle exercises the odd final straight step.
    // At 480 Hz the 96-sample texture tick spans more than one full cycle and
    // at least seven boundaries. GROW consumes RNG at the boundaries/wraps,
    // so a skipped or duplicated intermediate edge permanently changes the
    // exact target even when the final step index happens to match.
    TickPair tp;
    tp.boot(73u, [](ModLane& l) {
        l.set_range(1.f); l.set_shape(1.f); l.set_smooth(0.f);
        l.set_density(1.f);
        l.set_shuffle(1.f);
        l.set_step(true, 5);
        l.set_rate_hz(480.f);
        l.set_variation(0.6f);
    });

    int verified_multi_edge_windows = 0;
    for (int t = 0; t < 200; ++t) {
        tp.advance_one_tick();
        INFO("t=", t, " ref_fires=", tp.ref_fires,
             " ref_step=", tp.ref.cur_step(), " dut_step=", tp.dut.cur_step(),
             " ref_phase=", tp.ref.phase(), " dut_phase=", tp.dut.phase());
        REQUIRE(tp.ref_fires >= 7);
        CHECK(tp.dut_fired);
        CHECK(tp.dut.cur_step() == tp.ref.cur_step());
        // GROW also walks phase/shape at each wrap. The reference samples a
        // boundary after per-sample accumulation while tick() samples
        // its exact grid phase, so matching RNG state can differ by a tiny
        // waveform-evaluation epsilon. A skipped RNG mutation is orders of
        // magnitude larger and persists across later windows.
        CHECK(tp.dut.target()
              == doctest::Approx(tp.ref.target()).epsilon(0.001));
        float phase_distance = std::fabs(tp.dut.phase() - tp.ref.phase());
        if (phase_distance > 0.5f) phase_distance = 1.f - phase_distance;
        CHECK(phase_distance < 0.01f);
        ++verified_multi_edge_windows;
    }
    CHECK(verified_multi_edge_windows == 200);
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
    CHECK(skew_events == 0);   // measured, double accumulation (was <= 4)
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

    // Step 2 is mathematically at sample 96, and since the accumulator moved to
    // double (spec 2026-08-12 modulation-pace) both paths LAND on it: phase
    // 0.4, step 2, entered on the observation boundary itself. In float the
    // reference's 96 rounded adds finished a hair below 0.4 and both paths
    // still reported odd step 1 here -- that is what changed, and it is the
    // point of the case: the endpoint is now decided by the grid, not by which
    // side of it the rounding happened to fall.
    tp.advance_one_tick();
    REQUIRE(tp.ref.cur_step() == 2);
    REQUIRE(tp.dut.cur_step() == 2);

    tp.ref.set_shuffle(1.f);
    tp.dut.set_shuffle(1.f);

    // The next even step is 4, again exactly at a tick endpoint (0.8), and
    // entering it is what latches the new amount. Probe phase 0.63 proves both
    // paths latched full shuffle: its odd boundary sits at 0.6667, so 0.63 is
    // step 2 under full shuffle and step 3 on the straight grid.
    tp.advance_one_tick();
    CHECK((tp.ref_fires > 0) == tp.dut_fired);
    CHECK(tp.ref.cur_step() == 4);
    CHECK(tp.dut.cur_step() == 4);
    CHECK(tp.ref.step_at_phase(0.63f) == 2);
    CHECK(tp.dut.step_at_phase(0.63f) == 2);
    CHECK(tp.dut.target() == tp.ref.target());
    CHECK(tp.dut_out == tp.ref_out);

    // Both then traverse the same wrap and land inside the long shuffled
    // step 0 (its odd boundary moved out to 0.2667).
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
    // fractional sample 25.6 in each cycle, and since the accumulator moved to
    // double (spec 2026-08-12 modulation-pace) the reference COMPLETES the
    // second wrap inside the window instead of finishing a hair below it: both
    // paths sit on the wrap, on step 0. In float the reference stopped at
    // ~0.9999 on step 1 and carried a pending wrap into the next tick -- the
    // very off-by-one-edge this case exists to pin down.
    tp.advance_one_tick();
    REQUIRE(tp.ref.phase() >= 0.f);
    REQUIRE(tp.ref.phase() < 0.01f);
    REQUIRE(tp.dut.phase() >= 0.f);
    REQUIRE(tp.dut.phase() < 0.01f);
    CHECK(std::fabs(tp.dut.phase() - tp.ref.phase()) < 0.01f);
    REQUIRE(tp.ref.cur_step() == 0);
    REQUIRE(tp.dut.cur_step() == 0);

    tp.ref.set_shuffle(1.f);
    tp.dut.set_shuffle(1.f);

    // Both cross the next wrap into step 0 and latch the same full shuffle
    // value there. Probe phase 0.6 proves the latch: full shuffle puts the odd
    // boundary at 0.6667 (step 0), amount 0.2 put it at 0.5333 (step 1).
    // Repeated whole-window comparisons prove the step/RNG/target sequence
    // remains aligned across subsequent wraps.
    for (int t = 0; t < 3; ++t) {
        tp.advance_one_tick();
        INFO("t=", t, " ref_step=", tp.ref.cur_step(),
             " dut_step=", tp.dut.cur_step(),
             " ref_phase=", tp.ref.phase(), " dut_phase=", tp.dut.phase());
        CHECK((tp.ref_fires > 0) == tp.dut_fired);
        CHECK(tp.ref.phase() >= 0.f);
        CHECK(tp.ref.phase() < 0.01f);
        CHECK(tp.dut.phase() >= 0.f);
        CHECK(tp.dut.phase() < 0.01f);
        CHECK(std::fabs(tp.dut.phase() - tp.ref.phase()) < 0.01f);
        CHECK(tp.ref.cur_step() == 0);
        CHECK(tp.dut.cur_step() == 0);
        CHECK(tp.ref.step_at_phase(0.6f) == 0);
        CHECK(tp.dut.step_at_phase(0.6f) == 0);
        CHECK(tp.dut.target() == tp.ref.target());
        CHECK(tp.dut_out == tp.ref_out);
    }
}

TEST_CASE("tick: SONG process and tick keep form snapshots aligned") {
    TickPair tp;
    tp.boot_song(0x5150u);
    int skew_windows = 0;
    bool require_reconvergence = false;
    int full_checks = 0;

    for (int tick = 0; tick < 900; ++tick) {
        if (tick == 180) {
            tp.ref.set_variation(0.8f);
            tp.dut.set_variation(0.8f);
        }
        if (tick == 360) {
            tp.ref.set_variation(-0.8f);
            tp.dut.set_variation(-0.8f);
        }
        if (tick == 540) {
            tp.ref.new_phrase();
            tp.dut.new_phrase();
            tp.ref.set_step(true, 12);
            tp.dut.set_step(true, 12);
        }

        tp.advance_one_tick();
        const bool boundary_skew =
            tp.ref_wrapped != tp.dut_wrapped ||
            tp.ref.song_position() != tp.dut.song_position() ||
            tp.ref.active_pattern() != tp.dut.active_pattern();
        if (boundary_skew) {
            ++skew_windows;
            require_reconvergence = true;
            continue;
        }

        INFO("tick=", tick,
             " ref_form_pos=", static_cast<int>(tp.ref.song_position()),
             " dut_form_pos=", static_cast<int>(tp.dut.song_position()),
             " ref_pattern=", static_cast<int>(tp.ref.active_pattern()),
             " dut_pattern=", static_cast<int>(tp.dut.active_pattern()));
        CHECK(tp.dut.form() == tp.ref.form());
        CHECK(tp.dut.song_position() == tp.ref.song_position());
        CHECK(tp.dut.active_pattern() == tp.ref.active_pattern());
        CHECK(tp.dut.cadence_slot_for_test() ==
              tp.ref.cadence_slot_for_test());
        CHECK(tp.dut.bound_a_opening_for_test() ==
              tp.ref.bound_a_opening_for_test());
        CHECK(std::memcmp(&tp.dut.pattern_for_test(0),
                          &tp.ref.pattern_for_test(0),
                          sizeof(MelodyPattern)) == 0);
        CHECK(std::memcmp(&tp.dut.pattern_for_test(1),
                          &tp.ref.pattern_for_test(1),
                          sizeof(MelodyPattern)) == 0);
        if ((tp.ref_fires > 0) == tp.dut_fired) {
            CHECK(tp.dut.target() == tp.ref.target());
            CHECK(tp.dut_out == tp.ref_out);
        }
        require_reconvergence = false;
        ++full_checks;
    }

    CHECK(skew_windows <= 8);
    CHECK_FALSE(require_reconvergence);
    CHECK(full_checks >= 892);
}

TEST_CASE("tick: FLOW melody slot walk matches the per-sample path") {
    // 47 Hz across an 8-slot phrase (kFlowPhraseSlots) is a boundary every
    // ~128 samples -- more than one 96-sample tick apart, but close enough
    // that most windows still hold one edge and some hold two, exercising
    // the interior-slot arm as well as the wrap. Far above anything a FLOW
    // melody RATE reaches from the panel (the note-rate floor caps audible
    // fires around 14-16 Hz even when the underlying phrase cycle spins
    // faster -- see test_flow_melody.cpp's "the note rate has a floor",
    // which drives the *fire* rate this high), but this rate stresses the
    // *slot walk*, and it and the panel-reachable range both measured clean
    // (0 mismatches over thousands of ticks, several seeds) with the
    // per-edge floor advance in tick() (see the advance_floors comment
    // there). Only a pathological exact-resonance rate where the phrase
    // cycle divides kTickInterval evenly (500 Hz: 48000/500 == 96 samples,
    // matching kTickInterval exactly) still measured occasional desync --
    // far outside anything reachable here, so this case does not probe it.
    TickPair tp;
    tp.boot_flow_melody(0xF10Eu, 47.f, 0.6f);

    int mismatch = 0;
    for (int t = 0; t < 400; ++t) {
        tp.advance_one_tick();
        INFO("t=", t, " ref_step=", tp.ref.cur_step(),
             " dut_step=", tp.dut.cur_step(),
             " ref_fires=", tp.ref_fires, " dut_fired=", tp.dut_fired,
             " ref_song_pos=", tp.ref.song_position(),
             " dut_song_pos=", tp.dut.song_position());
        if ((tp.ref_fires > 0) != tp.dut_fired ||
            tp.dut.target() != tp.ref.target()) {
            ++mismatch;
            continue;
        }
        CHECK(tp.dut.cur_step() == tp.ref.cur_step());
        CHECK(tp.dut.song_position() == tp.ref.song_position());
    }
    CHECK(mismatch <= 2);                          // isolated straddles only
    CHECK(tp.dut.cur_step() == tp.ref.cur_step());  // re-converged at the end
    CHECK(tp.dut.target() == tp.ref.target());
    CHECK(tp.dut.song_position() == tp.ref.song_position());
}

TEST_CASE("tick: a kick near the note-rate floor does not grant it extra credit") {
    // Regression for the pending-mismatch entry's flow-melody arm
    // (lane.cpp, the `else if (_flow_melody_on())` block right after
    // advance_floors is defined): that call runs with NO advance_floors()
    // in front of it, unlike every other _on_boundary()/_wrap_events() call
    // in tick(). A kick() (or a FLOW<->STEP re-entry) can leave _cur_step
    // stale and land the pending-mismatch entry on a boundary it would not
    // otherwise have reached this window -- proving that entry does not
    // also wrongly backdate the floors to "as if this whole window had
    // already elapsed" the way the old single pre-loop lump did everywhere.
    //
    // density 1 (every slot gate-open) isolates the note-rate floor as the
    // only thing deciding "fired" here; variation 0 removes any RNG-draw
    // confound; rate_hz 1 keeps the next NATURAL slot boundary (6000
    // samples away) far outside this case's whole window, so only the
    // kick's forced mismatch can trigger _on_boundary() here.
    //
    // kFlowNoteMinS * 48 kHz == 2880 samples, and 29 * kTickInterval (96)
    // == 2784 -- exactly one tick short of the floor -- by construction,
    // not by measurement, so this reddens deterministically (integers, no
    // float epsilon) rather than depending on where a rate happens to land.
    TickPair tp;
    tp.boot_flow_melody(0xF10Eu, 1.f, 0.f);   // density 1.f already set here

    for (int t = 0; t < 29; ++t) tp.advance_one_tick();
    REQUIRE(tp.ref.cur_step() == 0);
    REQUIRE(tp.dut.cur_step() == 0);

    tp.ref.kick(0.5f, 0.f);
    tp.dut.kick(0.5f, 0.f);

    tp.advance_one_tick();
    INFO("ref_fires=", tp.ref_fires, " dut_fired=", tp.dut_fired,
         " ref_step=", tp.ref.cur_step(), " dut_step=", tp.dut.cur_step());
    REQUIRE(tp.ref.cur_step() == 4);          // the kick actually moved a slot
    CHECK(tp.dut.cur_step() == tp.ref.cur_step());
    // The real question: crediting a full window's worth of floor advance
    // at the mismatch entry (the wrong alternative measured separately)
    // fires here where process() does not -- one tick short of the floor,
    // a kick must not manufacture the missing 96 samples.
    CHECK((tp.ref_fires > 0) == tp.dut_fired);
    CHECK_FALSE(tp.dut_fired);
    CHECK(tp.dut.target() == tp.ref.target());
}
