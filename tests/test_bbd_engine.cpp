#include <doctest/doctest.h>
#include "parts/part.h"
#include "parts/bbd_engine.h"
#include "fx/flux.h"
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
