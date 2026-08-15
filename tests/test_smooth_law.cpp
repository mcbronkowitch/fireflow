// The whole-instrument audio-health gates for the interval-relative SMOOTH
// law. Spec: docs/superpowers/specs/2026-08-13-shape-smooth-rework-design.md 3.
//
// G4' and G4" are deliberately DIFFERENT gates. Only the init patch is
// converted to preserve its sound (spec 2.4), so only it owes a +-3 dB band.
// The four frozen points carry SMOOTH values drawn from the deleted terrain
// generator against the OLD law; under the new one they legitimately land 5-14
// dB quieter, and asking them to hold a band would be asking the rework not to
// work. They owe continued life, which is what G4" asserts.
#include "doctest/doctest.h"
#include "param_table.h"
#include "param_impact_points.h"
#include "center/center.h"
#include "fx/flux.h"
#include "parts/bbd_engine.h"
#include <cmath>
#include <cstdio>
#include <string>

using namespace spky;

namespace {

// Pre-conversion init-patch SMOOTH values. Duplicated here rather than
// included from host/vcv/ because tests/ must not depend on the VCV host.
// Task 4 converts these together with the three VCV mirrors.
constexpr float kInitSmoothA = 0.004974f;
constexpr float kInitSmoothB = 0.026026f;

float s_sl_echo[PART_COUNT][2][Flux::kMaxSamples];
float s_sl_bbd[PART_COUNT][2][BbdEngine::kCells];
AmbientReverb s_sl_reverb;

FxMem sl_fx_mem() {
    FxMem m;
    for (int p = 0; p < PART_COUNT; ++p) {
        m.echo[p][0] = s_sl_echo[p][0];
        m.echo[p][1] = s_sl_echo[p][1];
        m.bbd[p][0]  = s_sl_bbd[p][0];
        m.bbd[p][1]  = s_sl_bbd[p][1];
    }
    m.reverb = &s_sl_reverb;
    return m;
}

constexpr float kSr = 48000.f;
constexpr int   kTextureLanes[4] = {LANE_SOURCE, LANE_SIZE, LANE_MOTION, LANE_LEVEL};

// The shipped VCV init patch, in ENGINE ParamId order. init_patch.hpp is in
// VCV PANEL order, so this is mapped by NAME, not by index -- copying it
// positionally silently pairs every value with the wrong parameter.
void apply_init_patch(Instrument& in) {
    float v[P_COUNT] = {0.f};
    v[P_ENGINE_A] = 2.f;          v[P_ENGINE_B] = 0.f;
    v[P_SCALE]    = 3.f;
    // The VCV SONG knob is a RUNG on song_ladder.h's ladder, not an engine
    // parameter: song_ladder_at() resolves it into a {form, song} pair, and
    // only that pair reaches the engine. init_patch.hpp stores SONG_A = 0
    // (rung 0 -> {0, 6}) and SONG_B = 13 (rung 13 -> {4, 5}).
    v[P_FORM_A]   = 0.f;          v[P_SONG_A]   = 6.f;
    v[P_FORM_B]   = 4.f;          v[P_SONG_B]   = 5.f;
    v[P_RATE_A]   = 0.184337318f; v[P_RATE_B]   = 0.163855359f;
    v[P_DENSITY_A]= 0.534939826f;
    v[P_SMOOTH_A] = kInitSmoothA; v[P_SMOOTH_B] = kInitSmoothB;
    v[P_DEPTH_A]  = 0.403613269f; v[P_DEPTH_B]  = 0.681928277f;
    v[P_VARIATION_A] = 0.768674195f; v[P_VARIATION_B] = 0.671083927f;
    v[P_TUNE_A]   = 0.001204819f; v[P_TUNE_B]   = 0.321686625f;
    v[P_ATTACK_A] = 1.f;          v[P_ATTACK_B] = 1.f;
    v[P_DECAY_A]  = 1.f;          v[P_DECAY_B]  = 1.f;
    v[P_RES_B]    = 0.220000312f;
    v[P_SUB_A]    = 0.738666236f;
    v[P_FILT_A]   = -0.199999928f; v[P_FILT_B]  = -0.292000026f;
    v[P_FLUXMIX_A]= 0.353333473f; v[P_FLUXMIX_B]= 0.650667071f;
    v[P_GRIT_A]   = 0.173493922f;
    v[P_COMP_A]   = 0.761333168f; v[P_COMP_B]   = 0.848000109f;
    v[P_COLOR_A]  = 0.001204819f; v[P_COLOR_B]  = 0.862999976f;
    v[P_REVMIX_A] = 0.343394309f; v[P_REVMIX_B] = 0.805333197f;
    v[P_MORPH]    = 0.495180398f; v[P_COUPLE]   = 1.0f;
    v[P_DRIFT]    = 0.791999996f;
    v[P_REV_SIZE] = 1.f;          v[P_REV_DECAY]= 0.800755024f;
    v[P_REV_TONE] = 0.905333221f; v[P_REV_DIFF] = 0.768000245f;
    // The VCV TEMPO knob stores 0.0, and Fireflow.cpp:967 maps it as
    // bpm = 40.f + knob * 200.f -- so the init patch's tempo is 40, not 50.
    // 50 is param_table.h's ParamId range FLOOR for P_TEMPO_BPM, not a
    // default; don't repeat that mix-up.
    v[P_TEMPO_BPM]= 40.f;         v[P_PACE]     = 0.5f;
    for (int p = 0; p < P_COUNT; ++p) {
        if (p == P_MODE || p == P_STEPS_A || p == P_STEPS_B) continue;
        apply_param(in, p, v[p]);
    }
    apply_mode_and_steps(in, false, 0, 0);
}

void apply_frozen(Instrument& in, const FrozenPoint& rp) {
    for (int p = 0; p < P_COUNT; ++p) {
        if (p == P_MODE || p == P_STEPS_A || p == P_STEPS_B) continue;
        apply_param(in, p, rp.v[p]);
    }
    in.set_sync(rp.step);
    in.set_step(PART_A, rp.step, rp.steps_a);
    in.set_step(PART_B, rp.step, rp.steps_b);
}

// Peak-to-peak of one lane's post-slew output, plus a NaN watch on the audio.
struct Sweep { float p2p[PART_COUNT][LANE_COUNT]; bool finite; };

Sweep sweep(Instrument& in, float seconds) {
    Sweep s{};
    s.finite = true;
    float mn[PART_COUNT][LANE_COUNT], mx[PART_COUNT][LANE_COUNT];
    for (int p = 0; p < PART_COUNT; ++p)
        for (int i = 0; i < LANE_COUNT; ++i) { mn[p][i] = 1e9f; mx[p][i] = -1e9f; }

    const int total = int(seconds * kSr);
    for (int n = 0; n < total; ++n) {
        float l = 0.f, r = 0.f;
        in.process(nullptr, nullptr, &l, &r, 1);
        if (!std::isfinite(l) || !std::isfinite(r)) s.finite = false;
        for (int p = 0; p < PART_COUNT; ++p)
            for (int i = 0; i < LANE_COUNT; ++i) {
                const float v = in.lane_value_for_test(p, i);
                if (v < mn[p][i]) mn[p][i] = v;
                if (v > mx[p][i]) mx[p][i] = v;
            }
    }
    for (int p = 0; p < PART_COUNT; ++p)
        for (int i = 0; i < LANE_COUNT; ++i) s.p2p[p][i] = mx[p][i] - mn[p][i];
    return s;
}

} // namespace

TEST_CASE("G4': the init patch's texture lanes keep their movement") {
    // Baseline captured on commit b328984 (accessor present, OLD law still in
    // place -- the only tree where this measurement is meaningful), init
    // patch, 40 s, SMOOTH 0.836144507 / 1.0, audio finite.
    // Per lane, per deck, in ParamId lane order SOURCE/SIZE/MOTION/LEVEL.
    // A +-3 dB band: the conversion in gen_panel.py exists to hold this.
    // 2026-08-15: apply_init_patch's SONG_A/B, FORM_A/B and TEMPO_BPM were
    // corrected after this baseline was captured (review findings -- SONG
    // is a song_ladder.h rung, not an engine param, and TEMPO's init is 40,
    // not param_table.h's range floor of 50). Re-measured post-fix: all
    // eight texture-lane p2p values are bit-identical to the pre-fix run
    // (delta +0.000000000 everywhere), so the baseline below did not need
    // to move.
    static const float kBaseline[PART_COUNT][4] = {
        {1.955478f, 0.972017f, 0.989842f, 1.960271f},   // deck A
        {1.975470f, 0.999991f, 0.999896f, 1.968115f},   // deck B
    };
    Instrument in;
    in.init(kSr, sl_fx_mem());
    apply_init_patch(in);
    // The init patch's slowest texture lane cycles in ~153 s; 8 cycles is not
    // affordable in a unit test, so this measures 40 s -- long enough for the
    // two fast lanes to complete several cycles and for the slow ones to
    // traverse most of one, which is what a p2p comparison needs.
    const Sweep s = sweep(in, 40.f);
    CHECK_MESSAGE(s.finite, "G5: non-finite audio at the init patch");
    for (int p = 0; p < PART_COUNT; ++p)
        for (int k = 0; k < 4; ++k) {
            const float got = s.p2p[p][kTextureLanes[k]];
            const float want = kBaseline[p][k];
            const float db = 20.f * std::log10((got + 1e-9f) / (want + 1e-9f));
            CHECK_MESSAGE(std::fabs(db) <= 3.f,
                          "deck " << p << " lane " << kTextureLanes[k]
                                  << " moved " << db << " dB");
        }
}

// G4" and G5 share one render per frozen point. Kept as one TEST_CASE rather
// than two on purpose: `sweep` already returns both answers, and rendering the
// four points twice would double this file's contribution to suite runtime for
// nothing. The two CHECKs stay separately worded so a failure still names which
// gate fell over.
TEST_CASE("G4\"/G5: the frozen points still move, and stay finite") {
    for (int i = 0; i < kPer; ++i) {
        for (const FrozenPoint* rp : {&kFlowPoints[i], &kStepPoints[i]}) {
            Instrument in;
            in.init(kSr, sl_fx_mem());
            apply_frozen(in, *rp);
            const Sweep s = sweep(in, 20.f);
            CHECK_MESSAGE(s.finite, "G5: non-finite audio at "
                                        << std::string(rp->origin));
            for (int p = 0; p < PART_COUNT; ++p)
                for (int k = 0; k < 4; ++k)
                    CHECK_MESSAGE(s.p2p[p][kTextureLanes[k]] > 0.05f,
                                  "G4\": " << std::string(rp->origin) << " deck "
                                           << p << " lane " << kTextureLanes[k]);
        }
    }
}

// G6 exists because the interval-relative law made a failure mode reachable
// that the absolute one could not produce. tau is now proportional to the lane
// cycle, so at the slowest panel position -- RATE 0 (0.02 Hz), PACE 0 (x1/32),
// TIDE 0 (x1/4) -- tau reaches hundreds of seconds and the per-tick coefficient
// k = 1/(tau*sr) falls to ~1e-8. The tick twin's coefficient is 1 - (1-k)^96,
// and in FLOAT `1.f - k` rounds to exactly 1.0f below half an ulp, so the
// coefficient QUANTISES and eventually reaches zero. Under the old absolute law
// tau was capped at 0.5 s and k never fell below 4e-5, so none of this was
// reachable -- this is a defect the rework introduced.
//
// What the gate asserts, and why it is this and not "the lane still moves":
// driving ModLane directly at 0.0003125 Hz does freeze it outright (p2p exactly
// 0.000000000 over 60 s at SMOOTH 0.50/0.70/1.00, measured). Through the whole
// instrument it does NOT -- no lane reaches p2p 0 at any panel position, so a
// "> 0" assertion cannot go red and would be vacuous. What survives at the
// instrument level is the quantisation: the SMOOTH knob stops resolving.
// Measured on deck A's SOURCE lane, TIDE 0, 20 s, unfixed vs fixed:
//
//   SMOOTH   0.20        0.60        1.00
//   float    0.00352925  0.00240338  0.00240338   <- 0.60 and 1.00 identical
//   double   0.00398457  0.00293958  0.00272623   <- strictly decreasing
//
// So the gate is monotonicity: turning SMOOTH up must reduce the lane's
// movement. That is the rework's whole promise ("the knob has the same reach at
// every rate") stated at the one setpoint where float arithmetic breaks it.
// It is checked on SOURCE because at TIDE 0 the other three texture lanes cycle
// so slowly that a test-affordable window shows almost nothing of them -- their
// p2p is window-limited, not slew-limited, and it does not move with SMOOTH
// under either arithmetic. Three renders rather than five: the 0.60/1.00 pair
// alone carries the RED, and this file already costs ~1 s.
TEST_CASE("G6: SMOOTH still resolves at the slowest reachable setting") {
    const float kSmooth[3] = {0.20f, 0.60f, 1.00f};
    float p2p[3] = {0.f, 0.f, 0.f};

    for (int i = 0; i < 3; ++i) {
        Instrument in;
        in.init(kSr, sl_fx_mem());
        apply_init_patch(in);
        // The knobs that reach the underflow, pushed to their slow extremes.
        apply_param(in, P_RATE_A, 0.f);
        apply_param(in, P_RATE_B, 0.f);
        apply_param(in, P_PACE,   0.f);
        apply_param(in, P_TIDE,   0.f);
        apply_param(in, P_SMOOTH_A, kSmooth[i]);
        apply_param(in, P_SMOOTH_B, kSmooth[i]);
        const Sweep s = sweep(in, 20.f);
        CHECK_MESSAGE(s.finite, "G5: non-finite audio at SMOOTH " << kSmooth[i]);
        p2p[i] = s.p2p[PART_A][LANE_SOURCE];
    }

    for (int i = 1; i < 3; ++i)
        CHECK_MESSAGE(p2p[i] < p2p[i - 1],
                      "G6: SMOOTH " << kSmooth[i] << " did not smooth more than "
                                    << kSmooth[i - 1] << " (p2p " << p2p[i]
                                    << " vs " << p2p[i - 1]
                                    << ") -- the coefficient has quantised");
}
