#include <doctest/doctest.h>
#include "parts/part.h"
#include "parts/bbd_engine.h"
#include "fx/flux.h"
#include "mod/rng.h"
#include "util/svf_bp.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace spky;

static float s_bbd_l[Flux::kMaxSamples];
static float s_bbd_r[Flux::kMaxSamples];

TEST_CASE("bbd engine: the id is appended, never renumbered") {
    CHECK(ENGINE_BBD == 5);
    CHECK(ENGINE_COUNT == 6);
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
        q.init(48000.f, 7u, nullptr, nullptr, 0, s_bbd_l, s_bbd_r);
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
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
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
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
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
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
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
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
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
    p.init(48000.f, 3u, nullptr, nullptr, 0, s_bbd_l, s_bbd_r);
    p.set_engine(ENGINE_BBD);
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

TEST_CASE("bbd engine: FLOW is free, STEP is on the scale grid") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
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
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
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
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
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
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
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
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
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
        p.init(48000.f, 21u, nullptr, nullptr, 0, s_bbd_l, s_bbd_r);
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
        e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
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
    tail(0.9f, 0.3f, 0.7f, with_fb, 48000);
    tail(0.0f, 0.3f, 0.7f, without_fb, 48000);
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
    p.init(48000.f, 13u, nullptr, nullptr, 0, s_bbd_l, s_bbd_r);
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
    p.init(48000.f, 11u, nullptr, nullptr, 0, s_bbd_l, s_bbd_r);
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
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
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
    for (int i = 0; i < 6; ++i) {
        // 10*log10, not 20: bands[] accumulates y*y, so this is an ENERGY
        // ratio, and +-1 dB is written in LEVEL. 20*log10 of an energy ratio
        // is twice the level figure and makes the criterion silently 2x
        // stricter than spec 5.6 asks for.
        const float db = 10.f * std::log10((b[i] + 1e-12f) / (a[i] + 1e-12f));
        CAPTURE(i);
        CAPTURE(db);
        CHECK(std::fabs(db) <= 1.0f);
    }
}

TEST_CASE("bbd engine: a frozen loop shows no DC growth over 60 s") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
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
        e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
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
}

TEST_CASE("bbd engine: FLOW ignores the gate, so the freeze is unreachable there") {
    // A FLOW deck's gate is effectively always on. Without this rule a FLOW BBD
    // would be permanently frozen. Consequence: ATTACK and DECAY are inert in
    // FLOW -- a mode-dependent dead knob, accepted, and it belongs in the manual.
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
    e.set_flow(true);
    e.set_gate(true);
    CHECK(!e.frozen());
}

TEST_CASE("bbd engine: CHOKE closes the input and lets the tail run out") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
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
