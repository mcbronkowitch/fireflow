#include <doctest/doctest.h>
#include "instrument.h"
#include "parts/part.h"
#include "parts/bbd_engine.h"
#include "part_engine_contract.h"
#include "fx/flux.h"
#include "mod/rng.h"
#include "util/svf_bp.h"
#include "low_energy.h"
#include <algorithm>
#include <chrono>
#include <vector>
#include <cmath>
#include <vector>

using namespace spky;

static float s_bbd_l[BbdEngine::kCells];
static float s_bbd_r[BbdEngine::kCells];

TEST_CASE("bbd engine: the id is appended, never renumbered") {
    CHECK(ENGINE_BBD == 5);
    CHECK(ENGINE_COUNT == 7);
}

TEST_CASE("bbd engine: a deck set to BBD reaches the BBD, not the test tone") {
    Part p;
    p.init(48000.f, 7u);            // no line memory: a BBD deck is silent
    p.set_engine(ENGINE_BBD);
    float l, r;
    for (int i = 0; i < 500; ++i) p.process(l, r);   // let the fade complete
    CHECK(p.engine_id() == ENGINE_BBD);

    // engine_id() alone proves NOTHING about routing, and this case is named
    // for the routing: set_engine() writes _engine_id whether or not
    // _engine_for has a `case ENGINE_BBD:`, and the `default:` arm hands the
    // deck the test tone instead. Measured, by deleting that line: the two
    // assertions below go red (0.3 and "identical"), the one above does not.
    //
    // Negative leg: a deck routed to the test tone emits a 220 Hz sine at
    // LEVEL; a BBD deck handed no line memory emits exactly nothing.
    float peak = 0.f;
    for (int i = 0; i < 4800; ++i) {
        p.process(l, r);
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
    }
    CHECK(peak == 0.f);

    // Positive leg: give it memory and the deck must actually HEAR its input.
    // The test tone ignores process_in entirely (it does not even override
    // consumes_input), so for it these two renders are identical -- which is
    // also what makes this the one assertion here that would notice a
    // process_in/consumes_input pair implemented only halfway.
    auto render = [](bool with_input) {
        Part q;
        q.init(48000.f, 7u, nullptr, nullptr, nullptr, 0, s_bbd_l, s_bbd_r);
        q.set_engine(ENGINE_BBD);
        // MIX strictly below 1, so the dry path is live and the input shows
        // up immediately instead of one delay period later. Lane off, so the
        // value is exactly 0.5 and cannot be modulated back up to 1.
        q.set_target_active(LANE_LEVEL, false);
        q.set_target_base(LANE_LEVEL, 0.5f);
        std::vector<float> out;
        out.reserve(4800);
        float ql, qr, qsl, qsr;
        for (int i = 0; i < 4800; ++i) {
            const float in = with_input ? std::sin(i * 0.05f) : 0.f;
            q.process(in, in, ql, qr, qsl, qsr);
            out.push_back(ql);
        }
        return out;
    };
    CHECK(render(true) != render(false));
}

TEST_CASE("bbd engine: input reaches the output, and MIX decides how much") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(1.0f);
    float t[LANE_COUNT] = { 0.f, 0.5f, 0.5f, 0.f, 0.f };   // SRC SIZE PITCH MOT LEVEL
    auto rms = [&](float mix) {
        t[LANE_LEVEL] = mix;
        e.set_targets(t, 0.5f);
        float s = 0.f;
        for (int i = 0; i < 48000; ++i) {
            float l, r;
            e.process_in(std::sin(i * 0.05f), std::sin(i * 0.05f));
            e.process(l, r);
            if (i > 24000) s += l * l;
        }
        return std::sqrt(s / 24000.f);
    };
    const float wet = rms(1.f);
    e.reset();
    const float dry = rms(0.f);
    CHECK(dry > 1e-3f);                 // MIX 0 is the dry input, not silence
    CHECK(wet > 1e-3f);                 // MIX 1 is the delayed signal
    CHECK(std::fabs(wet - dry) > 1e-4f);   // and they are not the same thing
}

TEST_CASE("bbd engine: the delay time follows LANE_SIZE and lands on the grid") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(2.0f);                  // a 2 s phrase
    // Another Task-4 case predating the STEP/FLOW split: it changes SIZE
    // across two set_targets() calls with no fire in between and expects the
    // clock to land fresh in EACH window, which is a FLOW-mode property. In
    // STEP the clock deliberately holds across a SIZE change with no fire
    // (that is the point of the STEP-hold design), so the held clock from
    // the first window can fall outside the second, narrower one and clamp
    // -- not a bug, just not what this case is about.
    e.set_flow(true);
    float t[LANE_COUNT] = { 0.f, 0.f, 0.5f, 0.f, 1.f };
    // SIZE 1 -> div 1 -> T = 2 s; SIZE at the 1/8 rung -> T = 250 ms.
    t[LANE_SIZE] = 1.f;
    e.set_targets(t, 0.5f);
    const float long_delay = e.stages() / (2.f * e.clock_hz());
    CHECK(long_delay == doctest::Approx(2.0f).epsilon(0.01));
    t[LANE_SIZE] = 4.f / 10.f;          // rung index 4 == 1/8
    e.set_targets(t, 0.5f);
    CHECK(e.div_index() == 4);
    const float short_delay = e.stages() / (2.f * e.clock_hz());
    CHECK(short_delay == doctest::Approx(0.25f).epsilon(0.01));
}

TEST_CASE("bbd engine: LANE_PITCH moves the clock and leaves the delay alone") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(1.0f);
    // This case predates the STEP/FLOW split (Task 5) and is about the
    // continuous-follow property of the clock as PITCH moves -- a FLOW-mode
    // property now that STEP only re-derives the clock on a latched fire.
    e.set_flow(true);
    float t[LANE_COUNT] = { 0.f, 1.f, 0.f, 0.f, 1.f };
    e.set_targets(t, 0.5f);
    const float f_lo = e.clock_hz();
    const float d_lo = e.stages() / (2.f * f_lo);
    t[LANE_PITCH] = 1.f;
    e.set_targets(t, 0.5f);
    const float f_hi = e.clock_hz();
    const float d_hi = e.stages() / (2.f * f_hi);
    CHECK(f_hi > f_lo * 4.f);                       // the clock really moved
    CHECK(d_hi == doctest::Approx(d_lo).epsilon(0.01));   // the delay did not
}

TEST_CASE("bbd engine: the output stays inside its stated bound") {
    // The expander's 4x ceiling puts the raw self-oscillating return above
    // 0 dBFS. There is no per-deck limiter and the reverb send taps before the
    // master one, so the engine bounds itself.
    //
    // Measured in exactly this fixture, by deleting the fast_tanh in
    // BbdEngine::process: peak 1.387 (+2.8 dBFS), so this case does go red
    // when the bound it is named for is removed. The larger +8..+11 dBFS
    // figure quoted for the self-oscillating regime elsewhere is NOT what
    // this fixture reaches and is not asserted here.
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(0.5f);
    float t[LANE_COUNT] = { 1.f, 0.5f, 0.5f, 1.f, 1.f };   // DRIVE 1, FEEDBACK 1
    e.set_targets(t, 0.5f);
    float peak = 0.f;
    for (int i = 0; i < 48000 * 20; ++i) {
        float l, r;
        e.process_in(std::sin(i * 0.03f), std::sin(i * 0.07f));
        e.process(l, r);
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        CHECK(std::isfinite(l));
        CHECK(std::isfinite(r));
    }
    CHECK(peak <= 1.f);
}

TEST_CASE("bbd engine: a mono source through the stereo engine stays mono") {
    // Two BbdEcho is a stereo engine, so nothing about the shape of it forces
    // L and R to agree -- it is a property of what the two lines are fed and
    // seeded with, and it is a LISTENING property: at zero width a mono source
    // must arrive mono, or the deck quietly de-monoises everything sent
    // through it with nothing on the panel to explain why. Pinned here instead
    // of asserted in a comment, and pinned through Part's engine swap
    // specifically, because that is where BbdEngine::reset() runs -- the call
    // that decides which seeds the lines actually carry when the deck is
    // audible, as opposed to which ones init_buffers set once.
    Part p;
    p.init(48000.f, 3u, nullptr, nullptr, nullptr, 0, s_bbd_l, s_bbd_r);
    p.set_engine(ENGINE_BBD);
    // Width now rides COLOR (Task 7), and _color defaults to 0 already -- but
    // pin it explicitly. This test is NAMED for the zero-width case, and
    // relying on a member default to keep it there would make the assertion
    // silently mean something else the day that default changes.
    p.set_color(0.f);
    // MIX strictly below 1 and off-lane, so the dry path is live and the
    // output is loud enough to be worth comparing within this window.
    p.set_target_active(LANE_LEVEL, false);
    p.set_target_base(LANE_LEVEL, 0.5f);
    float l, r, sl, sr;
    for (int i = 0; i < 500; ++i) p.process(0.f, 0.f, l, r, sl, sr);  // swap + fade

    float peak = 0.f;
    for (int i = 0; i < 48000; ++i) {
        const float in = std::sin(i * 0.05f);   // the same sample to both ears
        p.process(in, in, l, r, sl, sr);
        REQUIRE(l == r);                        // bit-identical, not Approx
        peak = std::max(peak, std::fabs(l));
    }
    // Non-vacuity: silence is trivially mono. This must be a real signal whose
    // two channels agree, not two channels that agree about nothing.
    CHECK(peak > 1e-3f);
}

TEST_CASE("bbd engine: COLOR 0 with a mono source is bit-identical L to R") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(0.5f);
    e.set_width(0.f);
    float t[LANE_COUNT] = { 0.3f, 1.f, 0.5f, 0.6f, 1.f };
    e.set_targets(t, 0.5f);
    for (int i = 0; i < 96000; ++i) {
        float l, r;
        const float x = std::sin(i * 0.05f) * (i < 24000 ? 1.f : 0.f);
        e.process_in(x, x);
        e.process(l, r);
        // Same signal, same clock (width 0 -> r == 1 -> f_l == f_r exactly),
        // same stage count. The identity ALSO needs the same dither seed, but
        // there is nothing to arrange for that here: BbdLine::Reset (fx/bbd.h)
        // seeds every line's Rng from one constant, 0x9e3779b9u, and this
        // engine never calls SeedDither with anything else (checked: neither
        // init_buffers nor reset() do). Two identical inputs into two
        // identically-clocked, identically-seeded lines cannot diverge.
        CHECK(l == r);
    }
}

TEST_CASE("bbd engine: COLOR opened splits the lines and keeps the grid") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(0.5f);
    e.set_width(0.6f);
    float t[LANE_COUNT] = { 0.3f, 1.f, 0.5f, 0.6f, 1.f };
    e.set_targets(t, 0.5f);
    bool differed = false;
    for (int i = 0; i < 48000; ++i) {
        float l, r;
        const float x = std::sin(i * 0.05f);
        e.process_in(x, x);
        e.process(l, r);
        if (l != r) differed = true;
    }
    CHECK(differed);
    // Both lines still land on the same delay: what differs is the stage
    // count, hence the bandwidth and grain, not the rhythm.
    //
    // The tolerance is the sub-sample stage rounding, not a tuned number:
    // stages_for rounds 2*T*f to the nearest integer, so the stored stage
    // count is 2*T*f +- 0.5, and dividing back out gives
    // delay = T +- 0.25/f. At this fixture's operating point (T = 0.5 s,
    // f_r ~= 1433 Hz, the lower of the two clocks and so the looser bound)
    // that is a relative error of at most 0.25/(1433*0.5) ~= 3.5e-4.
    // 0.001 leaves just under 3x margin over that bound without being the
    // brief's much looser 0.01, which would have passed even a materially
    // wrong split.
    CHECK(e.stages_l() / (2.f * e.clock_l())
          == doctest::Approx(e.stages_r() / (2.f * e.clock_r())).epsilon(0.001));
    // Symmetric and geometric: the geometric mean of the two clocks is the
    // un-spread one.
    CHECK(std::sqrt(e.clock_l() * e.clock_r())
          == doctest::Approx(e.clock_hz()).epsilon(0.001));
}

TEST_CASE("bbd engine: RATE-modulated COLOR does not emit channel clicks") {
    struct Metrics {
        float peak = 0.f;
        float max_delta_l = 0.f;
        float max_delta_r = 0.f;
        double stereo_delta = 0.0;
    };

    auto render = [](int width_mode) {
        BbdEngine e;
        e.init(48000.f);
        e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
        e.reset();
        e.set_cycle(0.5f);
        e.set_flow(true);
        e.set_detune(0.f);
        float targets[LANE_COUNT] = { 0.15f, 0.5f, 0.5f, 0.35f, 1.f };
        e.set_targets(targets, 0.5f);

        Metrics m;
        float previous_l = 0.f;
        float previous_r = 0.f;
        bool have_previous = false;
        const int total = 48000 * 8;
        for (int i = 0; i < total; ++i) {
            if ((i % 96) == 0) {
                float width = 0.f;
                if (width_mode == 1) width = 0.2f;
                if (width_mode == 2) {
                    width = 0.2f + 0.2f * std::sin(
                        TWO_PI * 2.f * static_cast<float>(i) / 48000.f);
                }
                e.set_width(width);
            }

            const float in = 0.5f * std::sin(
                TWO_PI * 110.f * static_cast<float>(i) / 48000.f);
            float l = 0.f, r = 0.f;
            e.process_in(in, in);
            e.process(l, r);
            REQUIRE(std::isfinite(l));
            REQUIRE(std::isfinite(r));

            if (i >= 48000) {
                m.peak = std::max(m.peak, std::max(std::fabs(l), std::fabs(r)));
                m.stereo_delta += std::fabs(l - r);
                if (have_previous) {
                    m.max_delta_l = std::max(m.max_delta_l, std::fabs(l - previous_l));
                    m.max_delta_r = std::max(m.max_delta_r, std::fabs(r - previous_r));
                }
                have_previous = true;
            }
            previous_l = l;
            previous_r = r;
        }
        return m;
    };

    const Metrics zero = render(0);
    const Metrics fixed = render(1);
    const Metrics moving = render(2);
    CAPTURE(zero.max_delta_l);
    CAPTURE(zero.max_delta_r);
    CAPTURE(fixed.max_delta_l);
    CAPTURE(fixed.max_delta_r);
    CAPTURE(moving.max_delta_l);
    CAPTURE(moving.max_delta_r);
    CAPTURE(moving.stereo_delta);

    CHECK(zero.peak > 0.05f);
    CHECK(fixed.peak > 0.05f);
    CHECK(moving.peak > 0.05f);
    CHECK(zero.stereo_delta == 0.0);
    CHECK(fixed.stereo_delta > 1.0);
    CHECK(moving.stereo_delta > 1.0);
    CHECK(zero.max_delta_l < 0.03f);
    CHECK(zero.max_delta_r < 0.03f);
    CHECK(fixed.max_delta_l < 0.03f);
    CHECK(fixed.max_delta_r < 0.03f);
    CHECK(moving.max_delta_l < 0.03f);
    CHECK(moving.max_delta_r < 0.03f);
}

TEST_CASE("bbd engine: FLOW is free, STEP is on the scale grid") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(1.0f);
    float t[LANE_COUNT] = { 0.f, 1.f, 0.5f, 0.f, 1.f };
    e.set_flow(true);
    e.set_targets(t, 0.5f);
    const float free_hz = e.clock_hz();
    e.set_flow(false);
    e.latch_clock();
    e.set_targets(t, 0.5f);
    e.latch_clock();
    const float step_hz = e.clock_hz();
    // FLOW spreads 0..1 over the whole window (up to five octaves); STEP maps
    // the quantizer's 36 semitones onto three. At lane 0.5 they cannot agree.
    CHECK(free_hz != doctest::Approx(step_hz));
    CHECK(step_hz == doctest::Approx(
        bbd_music::clock_step(bbd_music::window(1.0f), 0.5f)));
}

TEST_CASE("bbd engine: in FLOW a cycle boundary does not latch the clock") {
    // lane.cpp:447-452: "FLOW has no per-step gate so it always fires." A FLOW
    // deck fires once per master-lane cycle; un-gated, the latch would freeze
    // the clock at the top of every cycle and the continuous bend FLOW exists
    // for would not happen.
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(1.0f);
    e.set_flow(true);
    float t[LANE_COUNT] = { 0.f, 1.f, 0.2f, 0.f, 1.f };
    e.set_targets(t, 0.5f);
    e.latch_clock();                       // the fire arrives and is ignored
    const float before = e.clock_hz();
    t[LANE_PITCH] = 0.8f;
    e.set_targets(t, 0.5f);                // the plane keeps moving
    CHECK(e.clock_hz() != doctest::Approx(before));
}

TEST_CASE("bbd engine: a fire during FLOW does not leak a latch into a later STEP") {
    // The case above does not actually exercise latch_clock()'s own `!_flow`
    // guard: _recompute()'s `if (_flow) ... else if (_latched)` already takes
    // the FLOW branch whenever _flow is true, so a stray `_latched = true`
    // left behind by a guardless latch_clock() is invisible for as long as
    // the deck stays in FLOW -- the case above passes even with the guard
    // deleted. Where a leaked _latched flag DOES show up is here: switched to
    // STEP afterwards, with no STEP fire of its own, that stale flag would
    // make _recompute() take the `else if (_latched)` branch on the very
    // next set_targets() and snap the clock to the STEP grid unasked. This is
    // the assertion that actually depends on the guard.
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(1.0f);
    e.set_flow(true);
    float t[LANE_COUNT] = { 0.f, 1.f, 0.2f, 0.f, 1.f };
    e.set_targets(t, 0.5f);
    e.latch_clock();                       // a fire arrives while still in FLOW
    e.set_flow(false);                     // now switch to STEP -- no fire yet
    const float before = e.clock_hz();
    t[LANE_PITCH] = 0.9f;
    e.set_targets(t, 0.5f);                // the plane moves, but STEP has not fired
    CHECK(e.clock_hz() == doctest::Approx(before));
    e.latch_clock();                       // the real STEP fire
    CHECK(e.clock_hz() != doctest::Approx(before));
}

TEST_CASE("bbd engine: in STEP the clock holds between fires") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(1.0f);
    e.set_flow(false);
    float t[LANE_COUNT] = { 0.f, 1.f, 0.2f, 0.f, 1.f };
    e.set_targets(t, 0.5f);
    e.latch_clock();
    const float latched = e.clock_hz();
    t[LANE_PITCH] = 0.9f;
    e.set_targets(t, 0.5f);                // plane moves, no fire
    CHECK(e.clock_hz() == doctest::Approx(latched));
    e.latch_clock();                       // now it fires
    CHECK(e.clock_hz() != doctest::Approx(latched));
}

TEST_CASE("bbd engine: a freshly constructed STEP deck with no fires still reflects PITCH") {
    // Construction-time defaults used to leave a STEP deck's clock frozen at
    // the ctor literal (4000 Hz), completely disconnected from PITCH, forever
    // -- until the first real fire. Reachable any time a deck boots into (or
    // is swapped to) STEP and the player moves PITCH before ever triggering a
    // step. The fix arms _latched at construction (and at every reset()), so
    // the FIRST set_targets() call -- even with zero calls to latch_clock()
    // -- derives a real clock from whatever PITCH actually is.
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(1.0f);
    e.set_flow(false);                      // STEP -- the default, made explicit
    float t[LANE_COUNT] = { 0.f, 1.f, 0.8f, 0.f, 1.f };   // PITCH away from 0.5
    e.set_targets(t, 0.5f);                 // zero calls to latch_clock()
    CHECK(e.clock_hz() == doctest::Approx(
        bbd_music::clock_step(bbd_music::window(1.0f), 0.8f)));
}

TEST_CASE("bbd engine: a BBD deck swapped into an already-running STEP transport "
          "reflects PITCH before its first fire") {
    // The same cold-start defect, reached the way a real deck reaches it:
    // Part::_engine_swap() calls BbdEngine::reset() (re-arms the latch), then
    // -- because the master lane's rate is already established the moment a
    // SECOND engine ever activates -- pushes set_cycle() into the freshly
    // swapped-in engine before that engine's own first set_targets(). If
    // set_cycle() (or init()/init_buffers()) consumed the arm, this would
    // still fail even after the ctor-default fix.
    //
    // Two SEPARATE decks, each cold-started at a different PITCH and read
    // after zero fires -- not one deck whose PITCH is changed twice: STEP is
    // supposed to hold the clock across a plane move with no fire in between
    // (see "in STEP the clock holds between fires"), so asking a single
    // cold-started deck to keep tracking a SECOND change would be asserting
    // the wrong contract. The cold-start fix only owns the FIRST derivation.
    //
    // Asserted as a differential (two PITCH values, correctly ordered clock
    // measurements) rather than against a hand-rebuilt window(): the window
    // depends on Part's own cycle and its SIZE-lane-driven division index
    // (LANE_SIZE defaults to 0.5 -> rung 5, not the tidy 1.0 the raw-engine
    // tests pin explicitly), which is Part bookkeeping this test should not
    // have to re-derive to prove the point.
    auto cold_start_hz = [](float pitch) {
        Part p;
        p.init(48000.f, 21u, nullptr, nullptr, nullptr, 0, s_bbd_l, s_bbd_r);
        p.set_step(true, 8);                 // STEP, engaged before BBD exists
        p.set_target_active(LANE_PITCH, false);
        p.set_tune(0.5f);
        p.set_target_base(LANE_PITCH, pitch);
        p.set_engine(ENGINE_BBD);
        float l, r;
        // Enough samples to complete the swap fade and let several control
        // ticks push real targets into the freshly-active engine; nowhere
        // near the ~48000-sample master-lane period (master_hz defaults to
        // 1) a lane fire would need -- checked below, not just assumed from
        // the sample count.
        for (int i = 0; i < 700; ++i) p.process(l, r);
        CHECK_FALSE(p.lane_fired(LANE_PITCH));
        return p.bbd().clock_hz();
    };
    const float low_hz = cold_start_hz(0.1f);
    const float high_hz = cold_start_hz(0.95f);
    CHECK(low_hz != doctest::Approx(4000.f));    // not the raw ctor literal
    CHECK(high_hz != doctest::Approx(4000.f));
    CHECK(high_hz > low_hz);                     // derived from PITCH, not a shared constant
}

TEST_CASE("bbd engine: PITCH is inaudible at FEEDBACK 0, and that is the design") {
    // The wet output at FEEDBACK 0 is the first pass only, which is always at
    // unity pitch: a BBD writes and reads at the same clock. MOTION is the
    // switch that turns PITCH on. Asserted rather than discovered.
    auto tail = [](float motion, float pitch_a, float pitch_b, float* out, int n) {
        BbdEngine e;
        e.init(48000.f);
        e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
        e.set_cycle(0.5f);
        e.set_flow(true);
        float t[LANE_COUNT] = { 0.f, 1.f, pitch_a, motion, 1.f };
        e.set_targets(t, 0.5f);
        for (int i = 0; i < 24000; ++i) {       // fill the line
            float l, r;
            e.process_in(std::sin(i * 0.06f), std::sin(i * 0.06f));
            e.process(l, r);
        }
        t[LANE_PITCH] = pitch_b;
        e.set_targets(t, 0.5f);                 // bend, input now silent
        for (int i = 0; i < n; ++i) {
            float l, r;
            e.process_in(0.f, 0.f);
            e.process(l, r);
            out[i] = l;
        }
    };
    static float with_fb[48000], without_fb[48000];
    // Contract: expansion recalls retained full-ring history and would test
    // first-pass history, not feedback separation.
    tail(0.9f, 0.7f, 0.3f, with_fb, 48000);
    tail(0.0f, 0.7f, 0.3f, without_fb, 48000);
    auto energy = [](const float* x, int n) {
        float s = 0.f;
        for (int i = n / 2; i < n; ++i) s += x[i] * x[i];
        return s;
    };
    // With feedback there is still a bent tail long after the input stopped;
    // without it, the first pass has run out and there is nothing to bend.
    CHECK(energy(with_fb, 48000) > 100.f * energy(without_fb, 48000));
}

TEST_CASE("bbd engine: a BBD deck in FLOW gets the raw pitch, in STEP the scale") {
    Part p;
    p.init(48000.f, 13u, nullptr, nullptr, nullptr, 0, s_bbd_l, s_bbd_r);
    p.set_engine(ENGINE_BBD);
    float l, r;
    for (int i = 0; i < 500; ++i) p.process(l, r);
    p.set_target_base(LANE_PITCH, 0.37f);
    p.set_target_active(LANE_PITCH, false);   // base + TUNE only
    p.set_tune(0.5f);
    p.set_step(false, 8);                     // FLOW
    for (int i = 0; i < 200; ++i) p.process(l, r);
    const float flow_v = p.target_value(LANE_PITCH);
    p.set_step(true, 8);                      // STEP
    for (int i = 0; i < 200; ++i) p.process(l, r);
    const float step_v = p.target_value(LANE_PITCH);
    // FLOW hands the engine the unquantized value; STEP hands it the scale.
    CHECK(flow_v == doctest::Approx(p.pitch_pre_quant()).epsilon(0.001));
    CHECK(step_v != doctest::Approx(flow_v));
}

TEST_CASE("bbd engine: switching away and back returns silence, not old charge") {
    Part p;
    p.init(48000.f, 11u, nullptr, nullptr, nullptr, 0, s_bbd_l, s_bbd_r);
    p.set_engine(ENGINE_BBD);
    p.set_target_base(LANE_MOTION, 0.9f);    // FEEDBACK up: charge circulates
    // The input-carrying form is the six-argument one -- Part has no
    // (in, in, out, out) overload, and the four-argument one is
    // (out, out, send, send), whose float& parameters a std::sin() prvalue
    // cannot bind to.
    float l, r, sl, sr;
    for (int i = 0; i < 48000; ++i) {
        const float in = std::sin(i * 0.05f);
        p.process(in, in, l, r, sl, sr);
    }
    p.set_engine(ENGINE_SYNTH);
    for (int i = 0; i < 2000; ++i) p.process(l, r);
    p.set_engine(ENGINE_BBD);
    for (int i = 0; i < 500; ++i) p.process(l, r);   // complete the fade
    float peak = 0.f;
    for (int i = 0; i < 24000; ++i) {
        p.process(l, r);
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
    }
    CHECK(peak < 1e-4f);
}

TEST_CASE("bbd engine: the freeze holds a broadband burst per octave") {
    // Rev. 1's scalar-k scheme measured the one frequency it tuned. The
    // compander's round trip is L^2 above about -40 dBFS and L below it, and L
    // is a lowpass -- at 8192 stages with k tuned for 1 kHz, ten circulations
    // left 110 Hz at +7.2 dB and 2.5 kHz at -48.8 dB. So: broadband material,
    // per-octave criterion, DRIVE swept during the hold.
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(1.0f);
    e.set_flow(false);
    e.set_decay(1.f);                      // DECAY max: no trim below k0
    // The operating point is chosen by ONE requirement: every probe must be
    // below the line's own Nyquist with real headroom. A BBD samples at f_clk,
    // so a probe above f_clk/2 measures the staircase's fold-around, not held
    // content -- and a "spread" computed across a mixture of real and aliased
    // bands is not a spectrum-flatness figure at all. This case first shipped
    // at div 1/2 / PITCH 0.5, i.e. f_clk = 1448 Hz, where 880, 1760 and 3520
    // were ALL above f_clk/2 = 724 Hz; that instrument is what produced the
    // un-fittable low/HIGH/ok/low/ok/ok residue.
    //
    // div 1/8 at a 1 s cycle -> T = 125 ms, PITCH 0.9 on the STEP grid ->
    // f_lo = 2048 Hz, f_clk = 2048 * 2^2.7 = 13308 Hz (measured 13307.9,
    // 3327 stages). Nyquist 6654 Hz against a top probe of 3520 Hz: 1.89x, and
    // 1.48x even against that probe's upper -3 dB skirt at 4508 Hz.
    //
    // STEP is what bounds this: clock_step spans 36 semitones, so f_clk can
    // never exceed 8 * f_lo = 2048/T, i.e. Nyquist <= 1024/T. Holding 3520 Hz
    // clear by ~2x therefore REQUIRES T <= ~145 ms whatever the PITCH -- the
    // fixture cannot be both long-tailed and validly probed, and the freeze is
    // only reachable in STEP, so FLOW's wider window is not available.
    float t[LANE_COUNT] = { 0.f, 4.f / 10.f, 0.9f, 0.5f, 1.f };
    e.set_targets(t, 0.5f);
    e.latch_clock();

    // A noise burst in, one delay period long.
    Rng rng; rng.seed(0xfeedu);
    const int period = static_cast<int>(0.125f * 48000.f);
    for (int i = 0; i < period; ++i) {
        float l, r;
        const float n = rng.next_bipolar() * 0.3f;
        e.process_in(n, n);
        e.process(l, r);
    }
    e.set_gate(true);                       // freeze engages
    CHECK(e.frozen());

    // Six one-octave probes at 110, 220, 440, 880, 1760, 3520 Hz. SvfBp<N>
    // SUMS its N bands, so use six SvfBp<1> and read them one at a time.
    constexpr float kProbeHz[6] = { 110.f, 220.f, 440.f, 880.f, 1760.f, 3520.f };
    SvfBp<1> probe[6];
    auto arm_probes = [&] {
        for (int b = 0; b < 6; ++b) {
            probe[b].reset();
            const float g = std::tan(3.14159265f * kProbeHz[b] / 48000.f);
            const float q = 2.f;                       // ~half an octave wide
            probe[b].set_coeffs(0, g, 1.f / q + g, 1.f / (1.f + g / q + g * g));
        }
    };

    // Band energy over the LAST circulation of a `circulations`-long hold, with
    // DRIVE swept 0 -> 1 across the whole run -- the term k = k0/bbd_drive_gain
    // exists precisely to keep that sweep from moving the loop gain.
    auto octaves = [&](int circulations, float* bands) {
        for (int b = 0; b < 6; ++b) bands[b] = 0.f;
        arm_probes();
        const int total = circulations * period;
        const float one = 1.f;
        for (int i = 0; i < total; ++i) {
            t[LANE_SOURCE] = static_cast<float>(i) / static_cast<float>(total);
            if ((i % 96) == 0) e.set_targets(t, 0.5f);   // control raster
            float l, r;
            e.process_in(0.f, 0.f);
            e.process(l, r);
            if (i >= total - period)
                for (int b = 0; b < 6; ++b) {
                    const float y = probe[b].process(&one, l);
                    bands[b] += y * y;
                }
            else
                for (int b = 0; b < 6; ++b) probe[b].process(&one, l);
        }
    };
    float a[6], b[6];
    octaves(1, a);
    // The engine is NOT re-primed between the two calls: the second run
    // continues the same frozen loop, so `b` really is ten circulations later
    // than `a` rather than a second first circulation.
    octaves(9, b);

    // +-2.5 dB per octave, and it is the PHYSICS' figure, not an aspiration.
    // Spec 5.6 asked for +-1 dB by inference and it was never measured; this
    // case is the measurement, and the owner took the measured number
    // (2026-07-31). What bounds it: the fixed 3600 Hz Butterworth chain costs
    // 5.45 dB per pass at 3520 Hz against 0.12 dB at 1760 Hz, it is constexpr,
    // and unlike the loss pole it does NOT move with the clock -- so with the
    // compander's L^2 round trip the feedback shelf would have to supply
    // +16.5 dB at 3520 Hz, where a one-pole shelf cornered at f_clk/4 delivers
    // +4.3. One zero against one pole plus six. A higher-order shelf was
    // refused on the same CPU grounds that keep kFiltOrder at 3.
    //
    // Measured worst band at the shipped constants: 2.26 dB (the floor is
    // spread/2 = 2.25, so the gain is centred on the MIDRANGE, not the mean --
    // centring on the mean left 0.6 dB on the table).
    //
    // CONTENT DEPENDENCE, so nobody mis-trusts this the first time they change
    // the material: the criterion holds at this case's fixed seed. Across six
    // seeds at the shipped constants the whole set slides by a mean offset of
    // roughly +-3 dB (-2.59 .. +2.40) and the spread ranges 3.84 .. 8.00 --
    //
    //   seed     110    220    440    880   1760   3520  spread   mean
    //   0xfeed  -0.60  -1.39  -2.26  +0.80  +2.24  -2.26   4.50   -0.58
    //   0x1234  -1.36  -1.51  +0.24  +2.01  +2.73  -2.65   5.38   -0.09
    //   0xbeef  +0.07  +0.30  +0.61  +3.65  +4.12  -0.73   4.85   +1.34
    //   0x55aa  -2.34  -0.82  +1.13  +3.04  +3.32  -1.73   5.66   +0.44
    //   0x0001  -3.89  -3.09  -2.68  -0.78  -0.64  -4.48   3.84   -2.59
    //   0xabcd  -1.28  -0.82  +2.86  +5.46  +6.72  +1.47   8.00   +2.40
    //
    // -- so k0 is not a content-independent constant. That is a stated
    // property of the freeze, not a defect the fixed seed is hiding: the loop
    // disperses around unity rather than diverging from it (an earlier
    // revision of this case, whose top three probes were above the line's
    // Nyquist, appeared to show every realisation blooming monotonically; it
    // did not).
    float worst = 0.f, hi = 0.f, lo = 0.f;
    for (int i = 0; i < 6; ++i) {
        // 10*log10, not 20: bands[] accumulates y*y, so this is an ENERGY
        // ratio, and the criterion is written in LEVEL. 20*log10 of an energy
        // ratio is twice the level figure.
        const float db = 10.f * std::log10((b[i] + 1e-12f) / (a[i] + 1e-12f));
        CAPTURE(i);
        CAPTURE(db);
        CHECK(std::fabs(db) <= 2.5f);
        if (i == 0 || db > hi) hi = db;
        if (i == 0 || db < lo) lo = db;
        worst = std::max(worst, std::fabs(db));
    }
    // The spread is the quantity kFreezeTilt actually controls, and it is the
    // seed-robust half of the result: re-centring the gain moves every band
    // together and can hide a shape regression inside the per-band figures
    // above, but it cannot move max - min. Measured 4.50 here; 5.0 leaves half
    // a dB and still catches a tilt that has stopped inverting the line.
    CAPTURE(worst);
    CHECK((hi - lo) <= 5.0f);
}

TEST_CASE("bbd engine: a frozen loop shows no DC growth over 60 s") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(1.0f);
    e.set_flow(false);
    e.set_decay(1.f);
    float t[LANE_COUNT] = { 0.f, 8.f / 10.f, 0.5f, 0.5f, 1.f };
    e.set_targets(t, 0.5f);
    e.latch_clock();
    for (int i = 0; i < 24000; ++i) {
        float l, r;
        e.process_in(0.4f, 0.4f);           // a deliberate DC offset
        e.process(l, r);
    }
    e.set_gate(true);
    // The DC already IN the line at freeze time is not what this case is
    // about, and it dominates a mean taken from sample 0: the stored offset
    // reads out unblocked once, and the blocker's own state starts from zero
    // the moment it is switched in. Measured per 5 s block at the settled
    // constants: block 1 = +3.56e-2, blocks 2..12 = +3.2e-4 .. -5.0e-4, flat
    // and not growing. So skip a settle window and measure the rest -- the
    // THRESHOLD is untouched, only the transient is excluded. Step 7 proves
    // the remaining assertion still goes red without the blocker.
    double mean = 0.0;
    const int n = 48000 * 60;
    const int settle = 48000 * 10;
    float peak = 0.f;
    for (int i = 0; i < n; ++i) {
        float l, r;
        e.process_in(0.f, 0.f);
        e.process(l, r);
        if (i >= settle) mean += l;
        if (i > n - 48000) peak = std::max(peak, std::fabs(l));
    }
    CHECK(std::fabs(mean / (n - settle)) < 1e-3);
    CHECK(peak < 1.f);                      // it has not parked the saturator
}

TEST_CASE("bbd engine: DECAY trims below k0, ATTACK sets the ramp") {
    auto tail_after = [](float decay, float seconds) {
        BbdEngine e;
        e.init(48000.f);
        e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
        e.set_cycle(1.0f);
        e.set_flow(false);
        e.set_attack(0.f);
        e.set_decay(decay);
        float t[LANE_COUNT] = { 0.f, 8.f / 10.f, 0.5f, 0.5f, 1.f };
        e.set_targets(t, 0.5f);
        e.latch_clock();
        for (int i = 0; i < 24000; ++i) {
            float l, r;
            e.process_in(std::sin(i * 0.05f) * 0.5f, 0.f);
            e.process(l, r);
        }
        e.set_gate(true);
        float s = 0.f;
        const int n = static_cast<int>(seconds * 48000.f);
        for (int i = 0; i < n; ++i) {
            float l, r;
            e.process_in(0.f, 0.f);
            e.process(l, r);
            if (i > n - 24000) s += l * l;
        }
        return s;
    };
    CHECK(tail_after(0.2f, 8.f) < 0.05f * tail_after(1.f, 8.f));

    // ...and DECAY has to act DURING a hold, not only before one. This leg is
    // separate from the one above because the one above cannot see the
    // difference: it calls set_decay() and then set_targets(), and
    // set_targets -> _recompute -> _apply_freeze refreshes the cached loop
    // gain regardless of whether set_decay did. Verified by measurement, not
    // assumed -- with set_decay()'s own _apply_freeze() deleted, the case
    // above still passes.
    //
    // Here nothing follows the knob: the deck is already frozen, the ramp has
    // settled so process() pushes nothing, and _freeze_k would keep the value
    // it was cached with. Turning DECAY down mid-hold is exactly what a player
    // does to end a freeze, so a stale cache here is a dead knob.
    auto tail_with_late_decay = [](float late_decay) {
        BbdEngine e;
        e.init(48000.f);
        e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
        e.set_cycle(1.0f);
        e.set_flow(false);
        e.set_attack(0.f);
        e.set_decay(1.f);
        float t[LANE_COUNT] = { 0.f, 8.f / 10.f, 0.5f, 0.5f, 1.f };
        e.set_targets(t, 0.5f);
        e.latch_clock();
        for (int i = 0; i < 24000; ++i) {
            float l, r;
            e.process_in(std::sin(i * 0.05f) * 0.5f, 0.f);
            e.process(l, r);
        }
        e.set_gate(true);
        for (int i = 0; i < 24000; ++i) {       // hold at k0 for half a second
            float l, r;
            e.process_in(0.f, 0.f);
            e.process(l, r);
        }
        e.set_decay(late_decay);                // the knob moves mid-hold, and
                                                // NOTHING else is pushed after
        float acc = 0.f;
        const int n = 8 * 48000;
        for (int i = 0; i < n; ++i) {
            float l, r;
            e.process_in(0.f, 0.f);
            e.process(l, r);
            if (i > n - 24000) acc += l * l;
        }
        return acc;
    };
    CHECK(tail_with_late_decay(0.2f) < 0.05f * tail_with_late_decay(1.f));
}

TEST_CASE("bbd engine: reset() leaves the lines holding no freeze, not just no charge") {
    // BbdEcho::Reset() clears STATE and not COEFFICIENTS -- feedback_, tilt_,
    // tilt_c_ and dc_on_ all survive it -- so a deck reset out of a freeze
    // would otherwise carry k0-ish feedback, kFreezeTilt and the DC blocker
    // into its next life with the input gate reopened, and process() could
    // never notice: _freeze == _freeze_last means nothing has moved, so the
    // per-sample push never fires. The swap path does not rescue it either
    // (Part::_engine_swap in STEP: reset -> set_flow(false) -> set_hold ->
    // set_gate -> set_cycle, none of which recompute).
    //
    // Asserted through the SIGNAL rather than through an observer, because the
    // defect is precisely that the observers say the freeze is off while the
    // lines behave as though it is on. A reset deck at FEEDBACK 0 must take
    // its input and let it die; one still holding the freeze's loop gain
    // sustains it instead.
    static float rl[BbdEngine::kCells], rr[BbdEngine::kCells];
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(rl, rr, BbdEngine::kCells);
    e.set_cycle(1.0f);
    e.set_flow(false);
    e.set_decay(1.f);
    // MOTION 0 -> the lane asks for NO feedback at all. Anything circulating
    // after the reset below can only be the freeze's own coefficients.
    float t[LANE_COUNT] = { 0.f, 4.f / 10.f, 0.9f, 0.f, 1.f };
    e.set_targets(t, 0.5f);
    e.latch_clock();
    for (int i = 0; i < 12000; ++i) {
        float l, r;
        e.process_in(std::sin(i * 0.05f) * 0.5f, 0.f);
        e.process(l, r);
    }
    e.set_gate(true);                       // freeze: loop gain goes to k0
    for (int i = 0; i < 24000; ++i) {
        float l, r;
        e.process_in(0.f, 0.f);
        e.process(l, r);
    }
    REQUIRE(e.freeze_amount() == 1.f);      // it really is frozen
    e.reset();                              // ...and now it is not
    CHECK(e.freeze_amount() == 0.f);
    CHECK_FALSE(e.frozen());

    // A short burst in, then silence. With the lane at MOTION 0 the burst must
    // run out; with the freeze's coefficients still in the lines it circulates.
    for (int i = 0; i < 2400; ++i) {
        float l, r;
        e.process_in(std::sin(i * 0.05f) * 0.5f, 0.f);
        e.process(l, r);
    }
    float early = 0.f, late = 0.f;
    for (int i = 0; i < 48000; ++i) {
        float l, r;
        e.process_in(0.f, 0.f);
        e.process(l, r);
        if (i < 6000) early = std::max(early, std::fabs(l));
        if (i > 42000) late = std::max(late, std::fabs(l));
    }
    CAPTURE(early);
    CAPTURE(late);
    CHECK(early > 1e-3f);                   // non-vacuity: there WAS a signal
    CHECK(late < 0.05f * early);            // and at FEEDBACK 0 it died
}

TEST_CASE("bbd engine: ATTACK sets the freeze ramp, and the ramp lands exactly") {
    // The sibling DECAY case is named for ATTACK but sets it to 0 in both
    // branches and asserts only the DECAY ratio, so until this case existed the
    // geometric map, _freeze_step and the landing had no coverage at all -- and
    // the ramp being LINEAR rather than the one-pole it was first written as
    // rests entirely on an arithmetic claim that nothing witnessed. frozen()
    // cannot witness it either: it reports the gate's decision, not the
    // crossfade.
    static float prime_l[BbdEngine::kCells], prime_r[BbdEngine::kCells];

    // 1. The geometric map, 2 ms .. 2 s, read back through the observer.
    {
        BbdEngine e;
        e.init(48000.f);
        e.set_attack(0.f);
        CHECK(e.freeze_ramp_s() == doctest::Approx(0.002f));
        e.set_attack(1.f);
        CHECK(e.freeze_ramp_s() == doctest::Approx(2.0f));
        // Geometric, so the midpoint is the GEOMETRIC mean sqrt(0.002 * 2) =
        // 0.0632 s, not the arithmetic 1.001 s. A linear map would pass the
        // two endpoints above and fail here.
        e.set_attack(0.5f);
        CHECK(e.freeze_ramp_s() == doctest::Approx(0.063246f).epsilon(0.001));
    }

    // A frozen deck at a known ATTACK, run for `n` samples after the gate.
    auto ramp_after = [](float attack, int n, float* freeze_out) {
        BbdEngine e;
        e.init(48000.f);
        e.init_buffers(prime_l, prime_r, BbdEngine::kCells);
        e.set_cycle(1.0f);
        e.set_flow(false);
        e.set_attack(attack);
        e.set_decay(1.f);
        float t[LANE_COUNT] = { 0.f, 4.f / 10.f, 0.9f, 0.5f, 1.f };
        e.set_targets(t, 0.5f);
        e.latch_clock();
        for (int i = 0; i < 12000; ++i) {          // fill the line
            float l, r;
            e.process_in(std::sin(i * 0.05f) * 0.5f, 0.f);
            e.process(l, r);
        }
        CHECK(e.freeze_amount() == 0.f);           // nothing has engaged yet
        e.set_gate(true);
        for (int i = 0; i < n; ++i) {
            float l, r;
            e.process_in(0.f, 0.f);
            e.process(l, r);
        }
        *freeze_out = e.freeze_amount();
    };

    // 2. ATTACK max is a 2 s ramp, so one second in it is genuinely PART WAY --
    //    neither still open nor already held. This is the assertion that fails
    //    if _freeze_step ignores ATTACK.
    float half = -1.f;
    ramp_after(1.f, 48000, &half);
    CAPTURE(half);
    CHECK(half > 0.45f);
    CHECK(half < 0.55f);

    // 3. ...and the same elapsed time at ATTACK min has long since landed, so
    //    the knob is demonstrably doing the work rather than the ramp being
    //    some fixed length.
    float fast = -1.f;
    ramp_after(0.f, 48000, &fast);
    CHECK(fast == 1.f);

    // 4. THE LANDING, and this is the claim the linear ramp exists for. A
    //    one-pole `_freeze += (want - _freeze) * coef` never lands: at ATTACK
    //    max the increment (1 - _freeze)/96000 drops below one float ulp while
    //    _freeze is still ~3e-3 short of 1, so it sticks there forever, leaving
    //    the input permanently ~0.3 % open and the loop that far under k0. The
    //    denormal floor cannot see a 3e-3 residue. `== 1.f` is the whole point:
    //    Approx would pass on exactly the arithmetic this asserts against.
    float landed = -1.f;
    ramp_after(1.f, 96000 + 4800, &landed);       // the full 2 s plus a margin
    CHECK(landed == 1.f);

    // 5. And the behavioural consequence of landing exactly, so the claim is
    //    not only an assertion about a private float: once the ramp is home the
    //    input must be COMPLETELY gone, not 99.7 % gone. Two identical frozen
    //    decks, one fed full-scale audio after the landing and one fed silence
    //    -- their outputs must agree. The residual tolerance is float rounding
    //    in `_in_l + _mix * (wl - _in_l)`, which does not cancel exactly at
    //    MIX 1; a 3e-3 leak through the line is orders above it.
    auto after_landing = [](bool with_input, std::vector<float>* out) {
        BbdEngine e;
        e.init(48000.f);
        e.init_buffers(prime_l, prime_r, BbdEngine::kCells);
        e.set_cycle(1.0f);
        e.set_flow(false);
        e.set_attack(1.f);                        // the ramp that sticks
        e.set_decay(1.f);
        float t[LANE_COUNT] = { 0.f, 4.f / 10.f, 0.9f, 0.5f, 1.f };
        e.set_targets(t, 0.5f);
        e.latch_clock();
        for (int i = 0; i < 12000; ++i) {
            float l, r;
            e.process_in(std::sin(i * 0.05f) * 0.5f, 0.f);
            e.process(l, r);
        }
        e.set_gate(true);
        for (int i = 0; i < 96000 + 4800; ++i) {  // let the ramp land
            float l, r;
            e.process_in(0.f, 0.f);
            e.process(l, r);
        }
        // CHECK, not REQUIRE: if the ramp ever stops landing, the leak
        // figure below is the interesting half of the diagnosis and aborting
        // the case would hide it.
        CHECK(e.freeze_amount() == 1.f);
        out->clear();
        for (int i = 0; i < 24000; ++i) {
            float l, r;
            const float in = with_input ? std::sin(i * 0.07f) : 0.f;
            e.process_in(in, in);
            e.process(l, r);
            out->push_back(l);
        }
    };
    std::vector<float> fed, quiet;
    after_landing(true, &fed);
    after_landing(false, &quiet);
    float worst_leak = 0.f;
    for (size_t i = 0; i < fed.size(); ++i)
        worst_leak = std::max(worst_leak, std::fabs(fed[i] - quiet[i]));
    // Measured with the one-pole restored and given 20 s to converge: it
    // settles at 0.997139 -- a 2.86e-3 residue, which is the arithmetic in
    // process()'s comment confirmed to three figures -- and leaks 9.7e-3 here,
    // 97x this threshold. Float rounding in the MIX-1 dry cancellation is
    // ~1e-7, so 1e-4 sits three decades clear of the noise and two below the
    // defect.
    CAPTURE(worst_leak);
    CHECK(worst_leak < 1e-4f);
}

TEST_CASE("bbd engine: FLOW ignores the gate, so the freeze is unreachable there") {
    // A FLOW deck's gate is effectively always on. Without this rule a FLOW BBD
    // would be permanently frozen. Consequence: ATTACK and DECAY are inert in
    // FLOW -- a mode-dependent dead knob, accepted, and it belongs in the manual.
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_flow(true);
    e.set_gate(true);
    CHECK(!e.frozen());
}

TEST_CASE("bbd engine: CHOKE closes the input and lets the tail run out") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(0.5f);
    e.set_flow(true);
    float t[LANE_COUNT] = { 0.f, 1.f, 0.5f, 0.6f, 1.f };
    e.set_targets(t, 0.5f);
    for (int i = 0; i < 24000; ++i) {
        float l, r;
        e.process_in(std::sin(i * 0.05f), 0.f);
        e.process(l, r);
    }
    e.set_hold(true);
    float first = 0.f, later = 0.f;
    for (int i = 0; i < 48000; ++i) {
        float l, r;
        e.process_in(std::sin(i * 0.05f), 0.f);   // still arriving, ignored
        e.process(l, r);
        if (i < 4800) first = std::max(first, std::fabs(l));
        if (i > 43200) later = std::max(later, std::fabs(l));
    }
    CHECK(later < 0.5f * first);    // it ran out
    CHECK(first > 1e-3f);           // and it was not cut dead
}

TEST_CASE("bbd engine: every VOICE knob reaches the BBD") {
    // Part::set_voice_* is six hand-written lines. An engine missing from one
    // of them is a dead knob with no diagnostic, so this checks all six by
    // observing a consequence, not by reading the source.
    Part p;
    p.init(48000.f, 17u, nullptr, nullptr, nullptr, 0, s_bbd_l, s_bbd_r);
    p.set_engine(ENGINE_BBD);
    // Every lane defaults ACTIVE (part.h:637), i.e. modulated continuously by
    // the mod plane -- including LANE_PITCH, which drives the clock in FLOW.
    // The brief's snippet left all five live, and over the ~100k samples this
    // case runs (two 48k-sample renders back to back) the plane walks the
    // clock by more than an OCTAVE between the two hf() calls -- measured,
    // clock_hz went from 297 Hz at FILT -1 to 8090 Hz at FILT +1, so the
    // "less HF energy at FILT -1" result was actually "a lower clock aliases
    // a fixed 3820 Hz probe tone harder", not FILT. Pin every lane inactive so
    // the only thing that differs between the two calls is FILT itself --
    // this is Task 8's own instruction #3: a RED or GREEN whose assertion
    // does not depend on the changed line has to be strengthened, not trusted.
    for (int lane = 0; lane < LANE_COUNT; ++lane) p.set_target_active(lane, false);
    p.set_target_base(LANE_SOURCE, 0.f);
    p.set_target_base(LANE_SIZE, 0.5f);
    p.set_target_base(LANE_PITCH, 0.5f);
    p.set_target_base(LANE_MOTION, 0.f);
    p.set_target_base(LANE_LEVEL, 0.8f);
    float l, r, sl, sr;
    for (int i = 0; i < 500; ++i) p.process(l, r);
    // FILT: the loss-pole corner. Dark and bright must differ in HF energy.
    auto hf = [&](float filt) {
        p.set_voice_filt(filt);
        p.bbd().reset();
        float s = 0.f;
        for (int i = 0; i < 48000; ++i) {
            const float x = std::sin(i * 0.5f);
            p.process(x, x, l, r, sl, sr);
            if (i > 24000) s += l * l;
        }
        return s;
    };
    CHECK(hf(-1.f) < hf(1.f));
    // FILT centre is exactly kLossCoef -- the neutral position, so a knob left
    // alone changes nothing.
    p.set_voice_filt(0.f);
    CHECK(p.bbd().loss_coef() == doctest::Approx(bbd_tuning::kLossCoef));
    // RESONANCE plays the feedback tilt.
    p.set_voice_resonance(0.f);
    const float lo = p.bbd().resonance_tilt();
    p.set_voice_resonance(1.f);
    CHECK(p.bbd().resonance_tilt() > lo);
    // SUB is the input level.
    p.set_voice_sub(0.f);
    CHECK(p.bbd().input_gain() == doctest::Approx(0.f));
    p.set_voice_sub(1.f);
    CHECK(p.bbd().input_gain() == doctest::Approx(1.f));
    // DETUNE (menu-only) is the slew time.
    p.set_voice_detune(0.f);
    const float fast = p.bbd().slew_seconds();
    p.set_voice_detune(1.f);
    CHECK(p.bbd().slew_seconds() > fast);
    // ATTACK and DECAY reached it in the freeze task; assert they still do.
    // freeze_ramp_s(), not the brief's freeze_ramp_seconds() -- the freeze
    // task already named this observer, and Task 8's own instructions say to
    // extend the existing observer set rather than invent a parallel one.
    p.set_voice_attack(1.f);
    p.set_voice_decay(0.3f);
    CHECK(p.bbd().freeze_ramp_s() > 0.5f);
    CHECK(p.bbd().decay_norm() == doctest::Approx(0.3f));
}

TEST_CASE("bbd engine: DETUNE's slew decides how far a modulated bend travels") {
    auto travel = [](float detune) {
        BbdEngine e;
        e.init(48000.f);
        e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
        e.set_cycle(1.0f);
        e.set_flow(true);
        e.set_detune(detune);
        float t[LANE_COUNT] = { 0.f, 1.f, 0.1f, 0.5f, 1.f };
        e.set_targets(t, 0.5f);
        for (int i = 0; i < 4800; ++i) { float l, r; e.process_in(0.f,0.f); e.process(l, r); }
        const float start = e.clock_now();
        t[LANE_PITCH] = 0.9f;                 // a step the slew must chase
        e.set_targets(t, 0.5f);
        for (int i = 0; i < 2400; ++i) { float l, r; e.process_in(0.f,0.f); e.process(l, r); }
        return e.clock_now() / start;
    };
    // clock_now(), not clock_hz(): clock_hz() is the TARGET the lane asks for
    // and moves instantly. What DETUNE decides is how fast the line follows it.
    CHECK(travel(0.f) > travel(1.f));    // a short slew gets further in 50 ms
}

TEST_CASE("bbd engine: DETUNE's glide crosses equal clock RATIOS in equal time") {
    // Code review finding, 2026-07-31: the shipped glide,
    // next = now*(1 + c*(target/now - 1)), is algebraically an exact
    // next = now + c*(target-now) -- a plain one-pole applied directly to
    // Hz, not the geometric ("equal ratios, equal time") shape spec 5.8
    // asks for. It reaches the pathology by a different route than a linear
    // ramp but lands in the same place: the first octave of a big jump
    // moves fast and the last one crawls, because the raw-Hz GAP shrinks
    // exponentially regardless of how many octaves are left to cross.
    //
    // The discriminating measurement: time an octave-doubling from f0 to
    // 2*f0, then time the NEXT doubling from 2*f0 to 4*f0, inside one
    // continuous glide toward a target far beyond both. A geometric glide
    // spends the same time on every octave; a one-pole-in-Hz spends much
    // less on the second (the arithmetic gap it is chasing has already
    // shrunk by the time it gets there).
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(1.0f);
    e.set_flow(true);
    e.set_detune(1.f);                    // the slowest setting, easiest to resolve
    // SIZE 1 -> div 1 -> T = cycle = 1 s -> window(1.0) gives f_lo = 256 Hz,
    // f_hi = 8192 Hz (kMinStages*0.5/1, kMaxStages*0.5/1). PITCH 0 -> the
    // target clock settles at f_lo = 256 Hz.
    float t[LANE_COUNT] = { 0.f, 1.f, 0.f, 0.5f, 1.f };
    e.set_targets(t, 0.5f);
    // Settle well past DETUNE max's own time constant before measuring f0:
    // the shipped glide's coefficient at DETUNE 1 gives a ~24000-sample (0.5 s)
    // time constant, so 24000 samples is only ~63 % converged, not settled.
    // 250000 samples is >10 time constants (<0.01 % residual) under either
    // the shipped shape or the fixed one, so f0 is a fair reading either way.
    for (int i = 0; i < 250000; ++i) { float l, r; e.process_in(0.f, 0.f); e.process(l, r); }
    const float f0 = e.clock_now();
    REQUIRE(f0 == doctest::Approx(256.f).epsilon(0.02));
    // PITCH 1 -> target jumps to f_hi = 8192 Hz, 32x f0 and therefore far
    // beyond the two octaves (4x) this case actually measures -- the glide
    // is nowhere near arriving while it crosses them.
    t[LANE_PITCH] = 1.f;
    e.set_targets(t, 0.5f);
    int i_2x = -1, i_4x = -1;
    const int n = 200000;
    for (int i = 0; i < n; ++i) {
        float l, r;
        e.process_in(0.f, 0.f);
        e.process(l, r);
        const float c = e.clock_now();
        if (i_2x < 0 && c >= 2.f * f0) i_2x = i;
        if (i_4x < 0 && c >= 4.f * f0) { i_4x = i; break; }
    }
    REQUIRE(i_2x > 0);
    REQUIRE(i_4x > i_2x);
    const float samples_f0_to_2f0 = static_cast<float>(i_2x);
    const float samples_2f0_to_4f0 = static_cast<float>(i_4x - i_2x);
    CAPTURE(samples_f0_to_2f0);
    CAPTURE(samples_2f0_to_4f0);
    // 10 % -- generous against measurement/quantisation noise, but nowhere
    // near the ~2x the broken one-pole-in-Hz shape actually produces
    // (verified: 787 vs 1656 samples at this fixture's operating point).
    CHECK(samples_2f0_to_4f0 == doctest::Approx(samples_f0_to_2f0).epsilon(0.1));
}

TEST_CASE("bbd engine: FILT's positive half is not dead over most of its travel") {
    // Code review finding, 2026-07-31: with a shared +-2-octave span, the
    // positive endpoint asked for kLossCoef*4 = 2.928, saturating
    // BbdLine::SetLossCoef's 0.999 ceiling at t ~= 0.22 -- so FILT +0.3
    // (comfortably inside the knob's positive half) was already indistinguishable
    // from FILT +1, and the whole top three quarters of that half did nothing.
    // A knob dead over most of one half of its travel is not a knob.
    BbdEngine e;
    e.init(48000.f);
    e.set_filt(0.3f);
    const float mid = e.loss_coef();
    e.set_filt(1.f);
    const float top = e.loss_coef();
    CAPTURE(mid);
    CAPTURE(top);
    // FILT +0.3 must not already be sitting at the same value as FILT +1 --
    // there has to be real travel left between them.
    CHECK(mid < top);
    // And FILT +1 should land at (not far past) the physical ceiling -- the
    // whole point of re-deriving the positive span was to spend the knob's
    // full travel getting there, not to overshoot into more clamped dead zone
    // the other way.
    CHECK(top == doctest::Approx(0.999f).epsilon(0.001));
}


TEST_CASE("bbd engine: satisfies the part-engine contract") {
    // tests/part_engine_contract.h, not tests/synth_engine_contract.h: WAVE
    // and BODY both entered through the latter, which is about voices,
    // envelopes and note allocation. A voiceless input-consuming engine
    // cannot satisfy it, so the universal half was split out. See that header
    // for why each of its three blocks is engine-agnostic.
    check_part_engine_contract<BbdEngine>([](BbdEngine& e) {
        e.init(48000.f);
        e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
        e.set_cycle(0.5f);
    });
}

TEST_CASE("bbd engine: with no input and FEEDBACK high it blooms from the dither floor") {
    // Spec 5.13: "With no input connected and FEEDBACK high, the engine
    // self-oscillates from the dither floor rather than outputting silence."
    // A voiceless engine with nothing patched in is a real playing state --
    // it is what a BBD deck IS before the player routes anything to it -- and
    // the difference between "a delay that blooms" and "a delay that is
    // silent" is the whole reason kDither exists (bbd_engine.cpp:9).
    //
    // reset() FIRST, and it is load-bearing: s_bbd_l/s_bbd_r are file-scope
    // and shared with every other case here, so without the memset in
    // BbdLine::Reset the loop could be seeded by a previous case's charge and
    // the phrase "from the dither floor" would be false while the assertion
    // still passed.
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.reset();
    e.set_cycle(0.5f);
    // DRIVE 0.5, SIZE mid, PITCH mid, FEEDBACK 1, MIX 1. The loop gain is
    // MOTION * 1.2 by construction (set_targets divides bbd_drive_gain back
    // out), so FEEDBACK at the top is 1.2 -- above unity, which is what "high"
    // has to mean for this bullet to be about anything.
    float t[LANE_COUNT] = { 0.5f, 0.5f, 0.5f, 1.f, 1.f };
    e.set_targets(t, 0.5f);

    // process_in is fed exactly zero for the whole run: nothing is connected.
    float early = 0.f, late = 0.f;
    const int n = 48000 * 20;
    for (int i = 0; i < n; ++i) {
        float l, r;
        e.process_in(0.f, 0.f);
        e.process(l, r);
        const float a = std::max(std::fabs(l), std::fabs(r));
        if (i < 4800) early = std::max(early, a);            // first 100 ms
        if (i > n - 48000) late = std::max(late, a);         // last second
    }
    CAPTURE(early);
    CAPTURE(late);
    // It started at the noise floor...
    CHECK(early < 1e-2f);
    // ...and it is emphatically not silent twenty seconds later.
    CHECK(late > 0.1f);
    CHECK(late < 1.f);                       // and still inside the bound

    // The control leg, and the reason this case can go red for the right
    // reason. Identical fixture with FEEDBACK at 0: the dither is still
    // injected at every write tick, so if the growth above came from anything
    // other than the loop -- a DC offset, an unstable filter, a seeded buffer
    // -- this leg would grow too.
    BbdEngine q;
    q.init(48000.f);
    q.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    q.reset();
    q.set_cycle(0.5f);
    float t0[LANE_COUNT] = { 0.5f, 0.5f, 0.5f, 0.f, 1.f };   // FEEDBACK 0
    q.set_targets(t0, 0.5f);
    float quiet = 0.f;
    for (int i = 0; i < n; ++i) {
        float l, r;
        q.process_in(0.f, 0.f);
        q.process(l, r);
        quiet = std::max(quiet, std::max(std::fabs(l), std::fabs(r)));
    }
    CAPTURE(quiet);
    CHECK(quiet < 1e-2f);
}

TEST_CASE("bbd engine: 60 s of silence at the input costs no denormal stall"
          * doctest::timeout(600.0)) {
    // Spec 5.13: "After 60 s of silence at the input, no denormal stall is
    // measurable on x86." Every state in the line decays geometrically, so
    // during a long silence they all approach zero without reaching it, and
    // the result is arithmetically correct and -- on a CPU that traps
    // subnormals into microcode -- catastrophically slow.
    //
    // TWO legs, because the wall-clock one alone would be a test that cannot
    // fail on this machine:
    //
    //   * The CONDITION, measured numerically: after 60 s of silence, no
    //     output sample is subnormal. That parked-between-zero-and-FLT_MIN
    //     state is what a stall IS, and classifying it is machine-independent.
    //     It goes red: with kDither set to 0 AND the four denormal flushes in
    //     fx/bbd.h deleted (BbdLine::Process's loss_z_ and ybbd_old_ guards
    //     and BbdEcho::fb_path's two), all 47999 of the last second's samples
    //     come back FP_SUBNORMAL. Unmodified, none do, and the smallest normal
    //     sample is 6.9e-11 -- twenty-seven orders of magnitude clear of the
    //     subnormal range. The primary defence is kDither: a normal-magnitude
    //     noise floor written into the line at every write tick means the line
    //     never has a true silence to decay into.
    //
    //   * The CONSEQUENCE, measured as wall clock, which is the spec's own
    //     wording. Against the same engine doing the same work on live
    //     material in the same process, so it prices the machine rather than
    //     assuming a budget. HONEST LIMIT: this leg did NOT go red on the
    //     machine this was written on. With both defences removed as above the
    //     silent run measured 1.19x the busy run, and a standalone one-pole
    //     microbenchmark on the same CPU showed the UNFLUSHED loop running
    //     faster than the flushed one (0.67x) -- i.e. this CPU has no
    //     measurable subnormal penalty for these operations, so no bound
    //     placed here could separate the two. The 2x bound is kept because the
    //     bullet is about x86 in general and this leg will bite on a CPU that
    //     does penalise; it is not what makes this case a gate today. The
    //     subnormal leg is.
    auto run = [](bool silent, long* subnormals) {
        BbdEngine e;
        e.init(48000.f);
        e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
        e.reset();
        e.set_cycle(0.5f);
        // FEEDBACK below unity on purpose: a blooming loop never decays, so
        // its states never approach the denormal range and the case would
        // measure nothing. What this bullet is about is a line left to die.
        float t[LANE_COUNT] = { 0.5f, 0.5f, 0.5f, 0.3f, 1.f };
        e.set_targets(t, 0.5f);
        float l, r;
        // A burst first, so there is something in the line to decay AWAY.
        for (int i = 0; i < 4800; ++i) {
            e.process_in(std::sin(i * 0.05f), std::sin(i * 0.05f));
            e.process(l, r);
        }
        const auto t0 = std::chrono::steady_clock::now();
        double acc = 0.0;                    // keeps the loop from vanishing
        const int n = 48000 * 60;
        for (int i = 0; i < n; ++i) {
            const float x = silent ? 0.f : std::sin(i * 0.05f) * 0.25f;
            e.process_in(x, x);
            e.process(l, r);
            acc += l;
            // The last second only: the decay has had 59 s to get there, and
            // classifying all 5.76 M samples would price the measurement
            // itself into the wall-clock leg running beside it.
            if (i > n - 48000) {
                if (std::fpclassify(l) == FP_SUBNORMAL) ++*subnormals;
                if (std::fpclassify(r) == FP_SUBNORMAL) ++*subnormals;
            }
        }
        const auto t1 = std::chrono::steady_clock::now();
        CHECK(std::isfinite(static_cast<float>(acc)));
        return std::chrono::duration<double>(t1 - t0).count();
    };
    long busy_sub = 0, silent_sub = 0;
    const double busy_s = run(false, &busy_sub);
    const double silent_s = run(true, &silent_sub);
    CAPTURE(busy_s);
    CAPTURE(silent_s);
    CAPTURE(silent_sub);
    CAPTURE(busy_sub);
    // The condition.
    CHECK(silent_sub == 0);
    CHECK(busy_sub == 0);
    // The consequence -- see the honest limit in the header comment.
    CHECK(busy_s > 0.0);
    CHECK(silent_s < busy_s * 2.0);
}

// Instrument-level line memory for the two-deck case below. Four lines, one
// stereo pair per deck -- deliberately NOT the s_bbd_l/s_bbd_r pair the
// engine-level cases share, because two decks running at once must not be
// writing into the same cells.
static float s_inst_bbd[PART_COUNT][2][BbdEngine::kCells];
static std::vector<float> s_inst_echo[PART_COUNT][2] = {
    {std::vector<float>(Flux::kMaxSamples), std::vector<float>(Flux::kMaxSamples)},
    {std::vector<float>(Flux::kMaxSamples), std::vector<float>(Flux::kMaxSamples)},
};

TEST_CASE("bbd engine: the output stays inside its stated bound with BOTH decks blooming") {
    // Spec 5.13: "The engine's output stays within its stated bound with both
    // decks blooming." The single-engine case above ("the output stays inside
    // its stated bound") is not this bullet: it runs ONE BbdEngine in
    // isolation. What this one adds is the configuration that can actually
    // diverge -- two blooming loops routed INTO EACH OTHER through the
    // cross-deck bus, so each deck's self-oscillating output is the other's
    // input, on top of its own feedback. The bound is per deck (fast_tanh in
    // BbdEngine::process), so it is read at deck_tap, before the summing and
    // before the master limiter -- reading the master output instead would
    // prove the limiter works, not the engine.
    FxMem mem;
    for (int p = 0; p < PART_COUNT; ++p) {
        mem.echo[p][0] = s_inst_echo[p][0].data();
        mem.echo[p][1] = s_inst_echo[p][1].data();
        mem.bbd[p][0] = s_inst_bbd[p][0];
        mem.bbd[p][1] = s_inst_bbd[p][1];
    }
    Instrument inst;
    inst.init(48000.f, mem);
    inst.set_tempo_bpm(120.f);
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_engine(p, ENGINE_BBD);
        // Mutual routing plus the audio input: every source a deck has.
        inst.set_excitation_sources(p, /*tape=*/true, /*other_deck=*/true,
                                    /*audio_in=*/true);
        // FEEDBACK (LANE_MOTION) and MIX (LANE_LEVEL) at the top, DRIVE
        // (LANE_SOURCE) high. Loop gain is MOTION * 1.2, i.e. blooming.
        inst.set_target_active(p, LANE_MOTION, false);
        inst.set_target_base(p, LANE_MOTION, 1.f);
        inst.set_target_active(p, LANE_LEVEL, false);
        inst.set_target_base(p, LANE_LEVEL, 1.f);
        inst.set_target_active(p, LANE_SOURCE, false);
        inst.set_target_base(p, LANE_SOURCE, 1.f);
        inst.set_voice_sub(p, 1.f);        // SUB: the input arrives at full level
        inst.set_voice_resonance(p, 1.f);  // the feedback-path tilt, live
    }
    float l, r;
    float peak_a = 0.f, peak_b = 0.f, peak_out = 0.f;
    for (int i = 0; i < 48000 * 20; ++i) {
        const float x = std::sin(i * 0.031f) * 0.5f;
        inst.process(&x, &x, &l, &r, 1);
        for (int c = 0; c < 2; ++c) {
            peak_a = std::max(peak_a, std::fabs(inst.deck_tap(PART_A, c)));
            peak_b = std::max(peak_b, std::fabs(inst.deck_tap(PART_B, c)));
        }
        peak_out = std::max(peak_out, std::max(std::fabs(l), std::fabs(r)));
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
    }
    CAPTURE(peak_a);
    CAPTURE(peak_b);
    CAPTURE(peak_out);
    // Non-vacuity FIRST: both decks must actually have bloomed, or the two
    // bounds below are satisfied by a pair of silent decks. This is the exact
    // failure mode movement 1's bench row shipped in another form -- a
    // configuration that measured nothing and looked correct.
    CHECK(peak_a > 0.1f);
    CHECK(peak_b > 0.1f);
    // The bound itself, per deck, before the sum and before the limiter.
    CHECK(peak_a <= 1.f);
    CHECK(peak_b <= 1.f);
}

// EDGE (Task 7, spec 2026-08-19 voice-knobs-dpth-edge, 4.5): a pre-emphasis
// high-pass applied in process_in(), ahead of SUB's _in_gain and the line --
// see bbd_engine.cpp's process_in() and set_edge() for the wiring.

TEST_CASE("bbd engine: EDGE at 0 leaves the line exactly as it was") {
    // Bit equality between a deck that never hears set_edge and one that
    // hears set_edge(0.f) -- same rig, same input, same seed (both engines
    // use the SAME s_bbd_l/s_bbd_r buffers in turn, and init_buffers()'s own
    // comment is that BbdLine::Reset seeds the dither from one fixed
    // constant, so two freshly-init'd engines fed identical input agree
    // regardless).
    //
    // This loop is a REGRESSION guard, not the bypass proof: two engines
    // that BOTH land on _edge == 0 take the identical deterministic code
    // path on identical content and therefore land on identical bits EITHER
    // WAY, whether or not process_in() actually skips _hp_l/_hp_r (same
    // reasoning as tests/test_sampler_engine.cpp's "sampler: EDGE at 0 is
    // bit-identical to no EDGE at all"). The case below this one is what
    // actually pins the skip.
    auto render = [](bool touch_edge) {
        BbdEngine e;
        e.init(48000.f);
        e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
        e.set_cycle(0.5f);
        float t[LANE_COUNT] = { 0.3f, 1.f, 0.5f, 0.6f, 1.f };
        e.set_targets(t, 0.5f);
        if (touch_edge) e.set_edge(0.f);
        std::vector<float> out;
        out.reserve(24000);
        for (int i = 0; i < 24000; ++i) {
            float l, r;
            const float x = std::sin(i * 0.05f) * 0.5f;
            e.process_in(x, x);
            e.process(l, r);
            out.push_back(l);
        }
        return out;
    };
    const auto untouched = render(false);
    const auto neutral = render(true);
    REQUIRE(untouched.size() == neutral.size());
    int mismatch = 0;
    for (size_t i = 0; i < untouched.size(); ++i)
        if (untouched[i] != neutral[i]) ++mismatch;
    CHECK(mismatch == 0);
}

TEST_CASE("bbd engine: EDGE at 0 provably skips the pre-emphasis filter") {
    // The white-box half the case above cannot provide (see its own
    // comment): reads _hp_l's OWN {x1, y1} history straight off a real
    // render at _edge == 0, which stays at its post-init() {0, 0} only if
    // process_in() never called the filter at all -- removing the skip in
    // process_in() turns this into a hard fail (same idiom as
    // tests/test_sampler_engine.cpp's SPKY_TESTING assertions, Task 6).
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
    e.set_cycle(0.5f);
    float t[LANE_COUNT] = { 0.3f, 1.f, 0.5f, 0.6f, 1.f };
    e.set_targets(t, 0.5f);
    e.set_edge(0.f);
    for (int i = 0; i < 4800; ++i) {
        float l, r;
        const float x = std::sin(i * 0.05f) * 0.5f;
        e.process_in(x, x);
        e.process(l, r);
    }
    CHECK(e.edge_hp_x1_for_test() == 0.f);
    CHECK(e.edge_hp_y1_for_test() == 0.f);
}

TEST_CASE("bbd engine: EDGE shapes what ARRIVES, not how it decays") {
    // The distinguishing test, and the reason this cell is not redundant
    // with FILT (the loss pole, INSIDE the loop, applied every circulation)
    // or RES (the feedback-path tilt, also every circulation): EDGE must
    // change the FIRST pass, because it acts once, at process_in(), on
    // whatever arrives from outside -- never on what is already circulating.
    //
    // Fixture: FLOW at PITCH 0.5, cycle 0.1 s, SIZE at the top rung (div 1,
    // T == cycle) -> delay ~= 0.1 s. LEVEL 0.5 (not 1) is deliberate: at
    // MIX 1 the very first output segment is pure WET and the line has not
    // had time to return anything yet, which would make "pass 1" compare
    // two near-silences -- vacuous. At MIX 0.5 pass 1 is dominated by the
    // live dry tap, which is exactly what "shapes what ARRIVES" means:
    // EDGE's effect on the input is audible on the very first thing that
    // reaches the output, not only three delay periods in.
    //
    // THE PASS-0 CHECK BELOW (CHECK(e1_p0 < 0.8f * e0_p0)) IS THE
    // FIXTURE-INDEPENDENT PROOF of non-redundancy, not the ratio trend
    // after it. Pass 0 spans [0, d), before the first wet return can even
    // arrive, so at MIX 0.5 it is essentially the dry tap,
    // fast_tanh(0.5*_in_l). FILT (the loss pole) and RES (the feedback
    // tilt) live entirely INSIDE BbdLine and structurally cannot touch the
    // dry tap at ANY DRIVE/FEEDBACK setting -- confirmed, not assumed:
    // e0_p0/e1_p0 read IDENTICAL to six decimal digits (0.009827/0.007336)
    // whether DRIVE is 0 or 0.7 and MOTION is 0.5 or 1.0 (both probed
    // below). So the pass-0 check alone already proves EDGE cannot be
    // redundant with either -- no operating point matters for THIS
    // assertion, and if it is ever true that r1 == r4 at some fixture, that
    // is a fact about the SECONDARY metric's resolution, not a reason to
    // doubt this one.
    //
    // The ratio trend (r1 vs r4, below) is that secondary,
    // resolution-limited metric, and DRIVE 0.7 / MOTION 1.0 is what IT
    // needs, not what the non-redundancy claim needs. Fix-round-1 review
    // corrected two things this comment had wrong about why:
    //  1. It is NOT about compander nonlinearity. BbdEcho::SetDrive
    //     (engine/fx/bbd.h) feeds only the fast_tanh SATURATOR
    //     (sat_in_ = g/kSatCeil); Compander (bbd.h) takes no DRIVE input at
    //     all and runs identically regardless of it. "DRIVE for real
    //     compander nonlinearity" was wrong -- DRIVE does not touch the
    //     compander.
    //  2. The falling r1..r4 trend needs no nonlinearity to explain: FILT's
    //     kLossCoef is a one-pole applied every circulation and
    //     low_energy() is a FIXED 200 Hz weighting, so as the loss pole
    //     darkens what circulates, the meter's weight migrates into
    //     exactly the band a high-pass removes hardest -- a purely LINEAR
    //     cascade already predicts a monotonically falling r_n. What the
    //     rejected fixture (DRIVE 0, MOTION 0.5) actually got wrong was
    //     FEEDBACK, not DRIVE: at MOTION 0.5, e0_p3 sat 14.2x below e0_p0
    //     (probed: e0_p0 = 0.009827, e0_p3 = 0.000694) -- close enough to
    //     BbdLine's own injected dither floor (kDither, bbd_engine.cpp)
    //     that r2/r3/r4 came out non-monotonic (probed: r1 = 0.746571,
    //     r2 = 0.673597, r3 = 0.699009, r4 = 0.706663) -- noise, not a real
    //     trend, and r1 vs r4 landed inside the epsilon by chance, not
    //     because EDGE stopped doing anything distinct from FILT/RES.
    //     MOTION 1.0 raises e0_p3 6.8x (0.000694 -> 0.004704, DRIVE also at
    //     0.7 for the fixture actually used below) clear of that floor
    //     (e0_p0/e0_p3 drops from 14.2x to 2.1x), and the trend becomes
    //     what the linear loss-pole story alone predicts: r1 = 0.746571,
    //     r2 = 0.680201, r3 = 0.659572, r4 = 0.635173 -- monotonic,
    //     comfortably outside the epsilon. DRIVE 0.7 is kept in the
    //     fixture below but is not load-bearing for this trend; MOTION is.
    // Do not "fix" a future near-equal r1/r4 reading here by loosening the
    // epsilon or by reaching for more DRIVE -- raise MOTION until e0_p3
    // clears the non-vacuity floor below, and only if it STILL lands
    // inside the epsilon does this become a design finding to report.
    // This is a test-methodology fact about this metric's resolution, not
    // a fact about the shipped instrument -- it does not belong in
    // docs/by-ear-decisions.md next to BODY's zone-2 blind spot.
    auto delay_samples = [](BbdEngine& e, float sr) {
        return static_cast<int>(e.stages() / (2.f * e.clock_hz()) * sr + 0.5f);
    };
    auto render_burst = [&](float edge, int total) {
        BbdEngine e;
        e.init(48000.f);
        e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
        e.set_cycle(0.1f);
        e.set_flow(true);
        float t[LANE_COUNT] = { 0.7f, 1.f, 0.5f, 1.f, 0.5f };   // SRC SIZE PITCH MOT LEVEL
        e.set_targets(t, 0.5f);
        e.set_edge(edge);
        const int d = delay_samples(e, 48000.f);
        REQUIRE(d > 0);
        REQUIRE(total >= 4 * d);
        Rng rng; rng.seed(0xfeedu);
        std::vector<float> out(static_cast<size_t>(total));
        for (int i = 0; i < total; ++i) {
            const float in = i < d ? rng.next_bipolar() * 0.3f : 0.f;
            float l, r;
            e.process_in(in, in);
            e.process(l, r);
            out[static_cast<size_t>(i)] = l;
        }
        return std::make_pair(out, d);
    };
    auto pass = [&](float edge, int n) {
        const auto [v, d] = render_burst(edge, 24000);
        const size_t lo = static_cast<size_t>(n) * static_cast<size_t>(d);
        const size_t hi = lo + static_cast<size_t>(d);
        REQUIRE(hi <= v.size());
        return low_energy({v.begin() + static_cast<long>(lo),
                            v.begin() + static_cast<long>(hi)});
    };
    const float e0_p0 = pass(0.f, 0), e1_p0 = pass(1.f, 0);
    const float e0_p3 = pass(0.f, 3), e1_p3 = pass(1.f, 3);
    CAPTURE(e0_p0);
    CAPTURE(e1_p0);
    CAPTURE(e0_p3);
    CAPTURE(e1_p3);
    // Pass 1 must move: that is the claim, and it is the fixture-independent
    // proof (see the comment above) -- FILT and RES cannot reach the dry
    // tap pass 0 dominates, at any operating point.
    CHECK(e1_p0 < 0.8f * e0_p0);
    // Non-vacuity FIRST (same idiom as "the output stays inside its stated
    // bound with BOTH decks"): pass 3 must be real circulating signal, not
    // BbdLine's own injected dither floor, before the ratio built from it
    // means anything. Measured (fix-round-1 review): at the fixture this
    // case used to use (DRIVE 0, MOTION 0.5), e0_p3 read 0.000694, only
    // 14.2x below e0_p0 -- close enough to the dither floor that the ratio
    // trend came out non-monotonic and noisy. At the fixture below, e0_p3
    // reads 0.004704, 2.1x below e0_p0. 2e-3 sits between the two: it fails
    // on the rejected fixture and passes on this one, so it is a real
    // discriminator, not a floor nothing can miss.
    CHECK(e0_p3 > 2e-3f);
    // And it must not move merely because the whole line got quieter -- if
    // the ratio at pass 4 equals the ratio at pass 1, EDGE would be
    // behaving like a fixed-gain trim rather than genuine pre-emphasis on
    // THIS secondary metric. That is a reason to raise MOTION further (see
    // the comment above), not a threshold to widen -- and only a reading
    // that stays near-equal after e0_p3 clearly clears the floor above is
    // the design failure worth reporting.
    const float r1 = e1_p0 / e0_p0;
    const float r4 = e1_p3 / e0_p3;
    CAPTURE(r1);
    CAPTURE(r4);
    CHECK(r1 != doctest::Approx(r4).epsilon(0.05));
}
