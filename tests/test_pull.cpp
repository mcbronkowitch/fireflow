// PULL -- chord gravity between the decks.
// Spec: docs/superpowers/specs/2026-07-19-pull-chord-gravity-design.md
// Plan: docs/superpowers/plans/2026-08-22-pull-chord-gravity.md
#include <doctest/doctest.h>
#include <vector>
#include "parts/part.h"
#include "pitch/chord.h"
using namespace spky;

namespace {
int popcount12(uint16_t m) { int k = 0; while (m) { k += m & 1; m >>= 1; } return k; }

// Settle a part on a fixed pitch with the quantizer in scale mode.
void settle(Part& p, EngineId e, float color) {
    p.init(48000.f, 11);
    p.set_engine(e);
    p.set_step(false, 8);
    p.set_target_active(LANE_PITCH, true);
    p.set_target_base(LANE_PITCH, 0.5f);
    p.set_target_depth(LANE_PITCH, 0.f);
    // COLOR is one of MOTION's destinations (part.cpp:343-347, spec
    // 2026-07-18 color-motion-target): _color_eff = clampf(_color +
    // lane_output(LANE_MOTION) * _depth * kColorMod * cgate, 0, 1). This
    // test asserts what COLOR alone publishes -- the 1/2/3/4 table was
    // measured on ChordBuilder directly, which never sees that coupling --
    // so the lane that also writes COLOR is switched off here. Left on, the
    // expected count would depend on the modulator's phase at sample 20000,
    // which is not what "COLOR sets how many pitch classes" is testing.
    p.set_target_active(LANE_MOTION, false);
    p.set_color(color);
    p.quant().set_mode(QuantMode::Scale);
    p.quant().set_scale(SCALE_MASKS[SCALE_DORIAN]);
    float l, r;
    for (int i = 0; i < 20000; ++i) p.process(l, r);
}
} // namespace

TEST_CASE("pc_mask12: absolute pitch classes, nearest semitone") {
    const float notes[3] = { 12.f / 36.f, 18.f / 36.f, 25.f / 36.f };
    CHECK(pc_mask12(notes, 3) == (uint16_t)((1u << 0) | (1u << 6) | (1u << 1)));
    CHECK(pc_mask12(notes, 0) == 0u);

    // Round-to-nearest, not truncate-toward-zero: -1.9 semitones is nearer
    // -2 than -1 (pc 10, not pc 11). A cast that truncates instead of floors
    // gets this one semitone wrong for every negative input.
    const float neg[1] = { -1.9f / Quantizer::SPAN_SEMIS };
    CHECK(pc_mask12(neg, 1) == (uint16_t)(1u << 10));
}

TEST_CASE("leader: COLOR sets how many pitch classes the deck publishes") {
    // Measured 2026-08-22 on ChordBuilder directly: 1/2/3/4 classes at
    // COLOR 0 / .25 / .5 / .75. The Part path has to reproduce that.
    const float colors[4] = { 0.f, 0.25f, 0.5f, 0.75f };
    for (int i = 0; i < 4; ++i) {
        Part p; settle(p, ENGINE_SYNTH, colors[i]);
        CAPTURE(colors[i]);
        CHECK(popcount12(p.chord_pc_mask()) == i + 1);
    }
}

TEST_CASE("leader: a sampler or BBD deck publishes no harmony") {
    // On those two the PITCH lane is a read position and a clock bend, not a
    // note -- the same two engines set_flow_melody and the quantizer bypass
    // name. Measured 2026-08-22: the quantizer never reaches the sounding
    // pitch on SAMPLER, and on BBD only in STEP.
    Part s; settle(s, ENGINE_SAMPLER, 0.75f);
    CHECK(s.chord_pc_mask() == 0u);
    Part b; settle(b, ENGINE_BBD, 0.75f);
    CHECK(b.chord_pc_mask() == 0u);
    Part f; settle(f, ENGINE_FEED, 0.75f);
    CHECK(f.chord_pc_mask() != 0u);      // FEED is a note deck and does lead
}

namespace {
// A stepped note deck that fires often, for counting draws.
void stepped(Part& p, uint32_t seed) {
    p.init(48000.f, seed);
    p.set_engine(ENGINE_SYNTH);
    p.set_step(true, 8);
    p.set_target_active(LANE_PITCH, true);
    p.set_target_base(LANE_PITCH, 0.5f);
    p.set_target_depth(LANE_PITCH, 1.f);
    p.set_depth(1.f);
    p.mod().set_range(1.f);
    p.quant().set_mode(QuantMode::Scale);
    p.quant().set_scale(SCALE_MASKS[SCALE_DORIAN]);
}
// Count fires and how many of them were bound, over `sec` seconds.
void count(Part& p, float sec, int& fires, int& bound) {
    fires = 0; bound = 0;
    bool prev = false; float l, r;
    const int n = static_cast<int>(48000.f * sec);
    for (int i = 0; i < n; ++i) {
        p.process(l, r);
        const bool f = p.mod().lane_fired(LANE_PITCH);
        if (f && !prev) { ++fires; if (p.gravity_bound()) ++bound; }
        prev = f;
    }
}
} // namespace

TEST_CASE("follower: probability 0 binds nothing, probability 1 binds every note") {
    int fires = 0, bound = 0;
    { Part p; stepped(p, 21); p.set_gravity(0x0044, 0.f);
      count(p, 8.f, fires, bound);
      CHECK(fires > 20); CHECK(bound == 0); }
    { Part p; stepped(p, 21); p.set_gravity(0x0044, 1.f);
      count(p, 8.f, fires, bound);
      CHECK(fires > 20); CHECK(bound == fires); }
}

TEST_CASE("follower: half probability binds roughly half, reproducibly") {
    // Deterministic seed -> the count is exact and repeatable, so this is a
    // regression gate on the stream as well as a statistics check.
    int f1 = 0, b1 = 0, f2 = 0, b2 = 0;
    Part a; stepped(a, 21); a.set_gravity(0x0044, 0.5f); count(a, 30.f, f1, b1);
    Part b; stepped(b, 21); b.set_gravity(0x0044, 0.5f); count(b, 30.f, f2, b2);
    CHECK(f1 == f2);
    CHECK(b1 == b2);                       // same seed, same stream
    CHECK(b1 > f1 / 4);                    // not stuck off
    CHECK(b1 < (3 * f1) / 4);              // not stuck on
}

TEST_CASE("follower: a bound note sounds a pitch class of the mask") {
    Part p; stepped(p, 21);
    p.set_gravity(0x0044, 1.f);            // classes 2 and 6 only
    float l, r;
    bool prev = false; int checked = 0;
    for (int i = 0; i < 48000 * 20 && checked < 8; ++i) {
        p.process(l, r);
        const bool f = p.mod().lane_fired(LANE_PITCH);
        if (f && !prev) {
            // Ride the 40 ms change slew out before reading the pitch: the
            // glide is the feature, so the value at the fire sample is not
            // the destination (measured 2026-08-22: 20 control ticks).
            for (int k = 0; k < 4000; ++k) p.process(l, r);
            const int semis =
                static_cast<int>(p.pitch_cv() * Quantizer::SPAN_SEMIS + 0.5f);
            CAPTURE(semis);
            CHECK(((1u << (semis % 12)) & 0x0044u) != 0u);
            ++checked;
        }
        prev = f;
    }
    CHECK(checked == 8);
}

TEST_CASE("follower: gravity off releases a bound note") {
    Part p; stepped(p, 21);
    p.set_gravity(0x0044, 1.f);
    float l, r;
    for (int i = 0; i < 48000 * 4; ++i) p.process(l, r);
    CHECK(p.gravity_bound());
    p.set_gravity(0, 0.f);
    CHECK_FALSE(p.gravity_bound());
    for (int i = 0; i < 48000; ++i) p.process(l, r);
    CHECK_FALSE(p.quant().gravity_on());
}

#include "instrument.h"

namespace {
// Two note decks, both stepped, A voiced as a chord.
void two_decks(Instrument& in) {
    in.init(48000.f);
    for (int d = 0; d < 2; ++d) {
        in.set_engine(d, ENGINE_SYNTH);
        in.set_step(d, true, 8);
        in.set_target_active(d, LANE_PITCH, true);
        in.set_target_base(d, LANE_PITCH, 0.5f);
        in.set_target_depth(d, LANE_PITCH, 1.f);
        in.set_quant_mode(d, QuantMode::Scale);
    }
    in.set_scale(SCALE_DORIAN);
    in.set_color(PART_A, 0.75f);      // A leads with four tones
    in.set_color(PART_B, 0.f);
}
void run(Instrument& in, float sec) {
    const int n = static_cast<int>(48000.f * sec);
    std::vector<float> l(64), r(64);
    for (int i = 0; i < n; i += 64) in.process(nullptr, nullptr, l.data(), r.data(), 64);
}
} // namespace

TEST_CASE("pull: the sign picks the leader, CHOKE's convention") {
    { Instrument in; two_decks(in); in.set_pull(-1.f); run(in, 4.f);
      CHECK(in.part(PART_B).gravity_bound());        // A leads, B is pulled
      CHECK_FALSE(in.part(PART_A).gravity_bound()); }
    { Instrument in; two_decks(in); in.set_pull(+1.f); run(in, 4.f);
      CHECK(in.part(PART_A).gravity_bound());
      CHECK_FALSE(in.part(PART_B).gravity_bound()); }
}

TEST_CASE("pull: the dead zone makes noon reliably off") {
    Instrument in; two_decks(in);
    in.set_pull(0.02f);                              // inside kPullDead
    run(in, 4.f);
    CHECK_FALSE(in.part(PART_A).gravity_bound());
    CHECK_FALSE(in.part(PART_B).gravity_bound());
}

TEST_CASE("pull: at full deflection every follower note is a leader chord tone") {
    // Fix round 1 (review of 2a00245): the original version sampled
    // pitch_cv() right after a fixed 500 ms run(), unsynchronised with the
    // follower's own 40 ms change slew -- a structural race against
    // _slew_len (~1920 samples), not evidence that PULL was wrong. Hardened
    // to Task 3's idiom in this same file ("follower: a bound note sounds a
    // pitch class of the mask"): detect the PITCH-lane fire's rising edge,
    // then ride past the slew before reading.
    //
    // The edge itself needs sample accuracy, measured, not assumed: probing
    // in.part(PART_B).lane_fired(LANE_PITCH) after a 64-sample process()
    // call caught 0 of 25 true rising edges over 4 s, because Part::process
    // (called once per sample from Instrument::process's inner loop) resets
    // and recomputes _fired every single sample -- only the LAST of the 64
    // survives past the block call. Per-sample polling caught all 25. So the
    // edge is detected one sample at a time here; the post-edge ride is
    // still done in 64-sample steps, since position there genuinely doesn't
    // matter (64 samples is two orders of magnitude inside the 1920-sample
    // slew).
    Instrument in; two_decks(in);
    in.set_pull(-1.f);
    run(in, 2.f);                                 // let PULL bind in
    std::vector<float> l1(1), r1(1);
    std::vector<float> l(64), r(64);
    int checked = 0;
    bool prev = false;
    // 20 s ceiling, matching Task 3's per-sample gates in this same file --
    // ample margin for a stepped 8-step deck to produce 6 fires.
    for (int i = 0; i < 48000 * 20 && checked < 6; ++i) {
        in.process(nullptr, nullptr, l1.data(), r1.data(), 1);
        const bool f = in.part(PART_B).lane_fired(LANE_PITCH);
        if (f && !prev) {
            for (int k = 0; k < 4000; k += 64)
                in.process(nullptr, nullptr, l.data(), r.data(), 64);
            const uint16_t lead = in.part(PART_A).chord_pc_mask();
            if (lead) {
                const int semis = static_cast<int>(
                    in.part(PART_B).pitch_cv() * Quantizer::SPAN_SEMIS + 0.5f);
                CAPTURE(lead); CAPTURE(semis);
                CHECK(((1u << (semis % 12)) & lead) != 0u);
                ++checked;
            }
            // Re-arm against the post-ride state, not the pre-ride edge: the
            // ride advances thousands of samples, long enough for another
            // fire to have already happened inside it.
            prev = in.part(PART_B).lane_fired(LANE_PITCH);
            continue;
        }
        prev = f;
    }
    CHECK(checked == 6);
}

TEST_CASE("pull: sweeping through centre releases the old follower") {
    Instrument in; two_decks(in);
    in.set_pull(-1.f); run(in, 4.f);
    CHECK(in.part(PART_B).gravity_bound());
    in.set_pull(0.f);  run(in, 0.1f);
    CHECK_FALSE(in.part(PART_B).gravity_bound());
    CHECK_FALSE(in.part(PART_B).quant().gravity_on());
}

TEST_CASE("pull: a sampler leader means the knob does nothing") {
    // D2: the PITCH lane is not a note on SAMPLER or BBD, so those decks
    // publish mask 0 and gravity in that direction is simply off.
    Instrument in; two_decks(in);
    in.set_engine(PART_A, ENGINE_SAMPLER);
    in.set_pull(-1.f);
    run(in, 4.f);
    CHECK_FALSE(in.part(PART_B).gravity_bound());
}
