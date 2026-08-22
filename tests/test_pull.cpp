// PULL -- chord gravity between the decks.
// Spec: docs/superpowers/specs/2026-07-19-pull-chord-gravity-design.md
// Plan: docs/superpowers/plans/2026-08-22-pull-chord-gravity.md
#include <doctest/doctest.h>
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
