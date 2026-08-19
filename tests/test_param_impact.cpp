// tests/test_param_impact.cpp
//
// The gate that would have caught the DIRT macro. DIRT aimed two of its four
// targets at GRIT, the wet/dry of a block nothing ever switches on, and nobody
// noticed until someone rendered it: the macro layer pushed the values, the
// setters accepted them, every unit test stayed green, and the knob did
// nothing. Nine more parameters were in the same state when this file was
// written (docs/attic/2026-08-13-glow-macro-audit.md).
//
// So this is not a unit test of any one component. It renders audio and asks
// the only question those components cannot answer about themselves: does
// moving this parameter change what comes out?
//
// TWO GATES, both exact-set comparisons rather than one-way checks, so neither
// list can rot:
//
//   1. Every ParamId moves audio at at least one of the frozen operating
//      points, except a named set that provably cannot. A parameter that goes
//      dead reddens this; so does one that comes back to life while still on
//      the list.
//   2. Every ParamId that moves audio moves it in BOTH operating modes, except
//      a named set of mode-exclusive ones. This is the gate the FLOW melody
//      engine will change on purpose -- when it lands, the expected set here
//      shrinks and this test tells you exactly by how much.
//
// FxMem: echo + BBD line memory is static here, the idiom
// tests/test_bbd_engine.cpp already uses. Sampler record buffers are
// deliberately left null (two 42 s/part heap buffers is not a price worth
// paying to prove a gate), so a Sampler deck runs silent -- which is why none
// of the four frozen points puts a Sampler on either deck.
#include "doctest/doctest.h"
#include "param_table.h"
#include "param_impact_points.h"
#include "center/center.h"
#include "fx/flux.h"
#include "parts/bbd_engine.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

using namespace spky;

namespace {

// Two slots: the two Instruments of a comparison run CONCURRENTLY and must not
// share echo/BBD memory, or each would hear the other's tail.
constexpr int kSlots = 2;
float s_pi_echo[kSlots][PART_COUNT][2][Flux::kMaxSamples];
float s_pi_bbd[kSlots][PART_COUNT][2][BbdEngine::kCells];
AmbientReverb s_pi_reverb[kSlots];

FxMem pi_fx_mem(int slot) {
    FxMem m;
    for (int p = 0; p < PART_COUNT; ++p) {
        m.echo[p][0] = s_pi_echo[slot][p][0];
        m.echo[p][1] = s_pi_echo[slot][p][1];
        m.bbd[p][0]  = s_pi_bbd[slot][p][0];
        m.bbd[p][1]  = s_pi_bbd[slot][p][1];
    }
    m.reverb = &s_pi_reverb[slot];
    return m;
}

constexpr float  kSr    = 48000.f;
constexpr int    kBlock = Center::kCtrlInterval;      // 96
constexpr float  kCtrlHz = kSr / float(kBlock);       // 500 Hz, as the hosts run it
constexpr double kDur   = 4.0;   // render window, seconds
constexpr double kSkip  = 0.5;   // discarded attack/settle head

// A parameter counts as moving audio at this much relative difference. The
// measured population is bimodal by orders of magnitude -- dead parameters read
// exactly 0.0 (bit-identical renders, not "small"), live ones read 0.03 and up
// -- so this threshold sits in an empty region and is not a tuning knob. It is
// deliberately NOT zero: a parameter whose only effect is denormal-level dither
// would pass a `> 0` test while being inaudible, and that is the failure this
// file exists to catch.
constexpr double kMoved = 1e-6;

// How much of the mix a deck must carry before its parameters may be judged at
// a given point. See the operating-point note below for why this check exists
// at all.
constexpr double kDeckAudible = 0.05;

// Excluded from both gates: P_MODE IS the axis the second gate splits on.
// Sweeping it would compare a FLOW render against a STEP one, which says
// nothing about whether the parameter is wired -- the two modes are
// different instruments by design. The mode itself is exercised by the lane
// tests (tests/test_step.cpp, tests/test_flow_melody.cpp), not here.
//
// FORM and SONG used to be excluded here too, for the same "unstable sample"
// reason DENSITY left the mode-exclusive gate under: ModLane::_compute_raw()
// blended the melody pattern through shape_value()'s S&H segment
// (waveforms.h), so whether FORM/SONG moved anything depended on a SHAPE
// value the operating point happened to carry. Spec 2026-08-13
// flow-melody-engine Task 8 wired set_flow_melody(true) into every note
// engine's PITCH lane and removed that dependence in FLOW; spec 2026-08-14
// melody-reachable removed it in STEP as
// well, so _compute_raw() now returns the phrase pitch directly whenever the
// deck runs a note engine (_note_lane()) -- no SHAPE blend, no S&H gate, in
// either mode. That removes the instability the exclusion existed for; see
// apply_patch() below for how this gate additionally controls for DEPTH and
// _active on the FORM/SONG cases specifically
// (docs/attic/2026-08-13-glow-macro-audit.md, "the second FORM/SONG gate").
bool is_excluded(int p) {
    return p == P_MODE;
}

// The operating points every comparison runs at were captured from the deleted
// terrain layer with all six macros at their CENTRE, settled -- that is what
// param_impact_points.h holds and why it holds those numbers.
//
// NOT the layer's own base[] array. That was this rig's first version and it
// measured the wrong instrument: for a story-owned parameter the generator's
// last stage wrote base[p] = bp[0], the curve's calm floor, so rendering from
// base[] put every macro at zero at once. Measured at that corner, BRIGHT's
// floor held FILT_A/B at about -0.5 everywhere, both decks sat near silence,
// and one of them usually fell off the filter cliff entirely and rendered
// exact zeros -- which then reported every _B parameter as dead. The bug was
// in the rig, not the instrument. A centred macro vector is an operating point
// somebody might actually listen to, which is why these four are the ones that
// were frozen.

// Push a reference patch onto an Instrument, optionally overriding one
// parameter. `param < 0` means "no override".
void apply_patch(Instrument& in, const FrozenPoint& rp, int param, float v) {
    for (int p = 0; p < P_COUNT; ++p) {
        // set_step() takes mode and count together and set_sync() is global, so
        // these three cannot go through the per-parameter apply_param() -- the
        // same reason apply_mode_and_steps() (param_table.h) exists. Issued as
        // one unit below.
        if (p == P_MODE || p == P_STEPS_A || p == P_STEPS_B) continue;
        apply_param(in, p, p == param ? v : rp.v[p]);
    }
    const int sa = param == P_STEPS_A
        ? int(clamp_to(kParams[P_STEPS_A], v) + 0.5f) : rp.steps_a;
    const int sb = param == P_STEPS_B
        ? int(clamp_to(kParams[P_STEPS_B], v) + 0.5f) : rp.steps_b;
    in.set_sync(rp.step);
    in.set_step(PART_A, rp.step, sa);
    in.set_step(PART_B, rp.step, sb);

    // FORM/SONG reach audio only through LANE_PITCH's modulation output
    // (part.cpp's target_raw). A point that carries a low DEPTH, or a future
    // caller that left LANE_PITCH inactive, could mask the melody
    // independently of whether FORM/SONG themselves are wired -- decided in
    // advance (task-10 brief) to hold both out of the way for exactly these
    // two parameters, so a measured zero below can only be the melody's own
    // reach, not a downstream attenuator. Applied identically to both the
    // lo and hi render of a compare() (same `param`, same forced state), so
    // it cannot itself be the thing that differs between them.
    if (param == P_FORM_A || param == P_SONG_A) {
        apply_param(in, P_DEPTH_A, kParams[P_DEPTH_A].hi);
        in.set_target_active(PART_A, LANE_PITCH, true);
    }
    if (param == P_FORM_B || param == P_SONG_B) {
        apply_param(in, P_DEPTH_B, kParams[P_DEPTH_B].hi);
        in.set_target_active(PART_B, LANE_PITCH, true);
    }
}

// Render two Instruments in lockstep from the same reference patch, differing
// in exactly one thing, and return the relative RMS difference. 0.0 means the
// two renders are bit-identical.
//
// The frozen points are pushed once and then held: nothing re-pushes a
// parameter during the render, so the single override survives the whole
// window. (When the terrain layer still existed this was a live hazard -- it
// re-pushed every parameter each control tick, so an override was erased on the
// next one, and the rig deliberately never rendered through it.)
double compare(const FrozenPoint& rp, int param, float v_lo, float v_hi,
               int mute_deck = -1) {
    Instrument a, b;
    a.init(kSr, pi_fx_mem(0));
    b.init(kSr, pi_fx_mem(1));
    apply_patch(a, rp, param, v_lo);
    apply_patch(b, rp, param, v_hi);
    if (mute_deck >= 0) b.set_part_level(mute_deck, 0.f);

    const size_t total = size_t(kDur * kSr), skip = size_t(kSkip * kSr);
    double sum_d = 0.0, sum_a = 0.0;
    for (size_t i = 0; i < total; ++i) {
        float al = 0.f, ar = 0.f, bl = 0.f, br = 0.f;
        a.process(nullptr, nullptr, &al, &ar, 1);
        b.process(nullptr, nullptr, &bl, &br, 1);
        if (i < skip) continue;
        const double dl = double(al) - double(bl), dr = double(ar) - double(br);
        sum_d += dl * dl + dr * dr;
        sum_a += double(al) * al + double(ar) * ar;
    }
    // A silent reference cannot answer the question either way. Reported as
    // "did not move" rather than as a pass, so a rig that accidentally renders
    // silence fails loudly instead of passing everything.
    if (sum_a <= 0.0) return 0.0;
    return std::sqrt(sum_d / sum_a);
}

// Does `deck` carry enough of the mix for its parameters to be judgeable here?
// An operating point can leave one deck near-silent even with the macros
// centred -- an engine whose excitation never arrives, an envelope that never
// opens -- and at such a point every parameter of that deck renders
// bit-identical. Without this check the gate reports those as defects.
bool deck_audible(const FrozenPoint& rp, int deck) {
    return compare(rp, -1, 0.f, 0.f, deck) > kDeckAudible;
}

struct Terrains { FrozenPoint flow[kPer], step[kPer]; };

// The generator that drew these is gone (removal spec
// docs/superpowers/specs/2026-08-14-flow-glow-removal-design.md, 4.3). What it did on
// every build and this cannot is re-check that both decks still carry the
// mix -- so that check runs here instead, live, on each frozen point. A
// change that leaves a deck near-silent at one of these points would
// otherwise report that deck's whole parameter set as dead, which is the
// mistake this rig already made once (see the operating-point note above).
Terrains load_points() {
    Terrains out{};
    for (int i = 0; i < kPer; ++i) {
        out.flow[i] = kFlowPoints[i];
        out.step[i] = kStepPoints[i];
    }
    for (int i = 0; i < kPer; ++i) {
        REQUIRE_MESSAGE(deck_audible(out.flow[i], PART_A), std::string(out.flow[i].origin));
        REQUIRE_MESSAGE(deck_audible(out.flow[i], PART_B), std::string(out.flow[i].origin));
        REQUIRE_MESSAGE(deck_audible(out.step[i], PART_A), std::string(out.step[i].origin));
        REQUIRE_MESSAGE(deck_audible(out.step[i], PART_B), std::string(out.step[i].origin));
    }
    return out;
}

// Does `param` move audio at any of these points? Stops at the first point
// that says yes -- the gate asks a yes/no question, and paying for the
// remaining renders would only refine a number nobody reads.
bool moves_audio(const FrozenPoint* rps, int param) {
    for (int i = 0; i < kPer; ++i)
        if (compare(rps[i], param, kParams[param].lo, kParams[param].hi) > kMoved)
            return true;
    return false;
}

std::string name_of(int p) { return std::string(kParams[p].name + 2); }

// Sorted, comma-joined, so a failure names the difference instead of making the
// reader diff two unordered dumps.
std::string join(const bool* set) {
    std::string s;
    for (int p = 0; p < P_COUNT; ++p)
        if (set[p]) { if (!s.empty()) s += ", "; s += name_of(p); }
    return s.empty() ? "(none)" : s;
}

void report(const bool* actual, const bool* expected, const char* what) {
    for (int p = 0; p < P_COUNT; ++p) {
        if (is_excluded(p)) continue;
        INFO("parameter " << name_of(p));
        CHECK(actual[p] == expected[p]);
    }
    if (join(actual) != join(expected))
        std::printf("  %s now:      %s\n  %s expected: %s\n",
                    what, join(actual).c_str(), what, join(expected).c_str());
}

} // namespace

TEST_CASE("param impact: every parameter moves audio somewhere") {
    // Two groups, kept in SEPARATE arrays on purpose -- see the header
    // comment on is_excluded() for why an undifferentiated expected[] would
    // be dishonest here: this file must never assert an untraced anomaly is
    // correct, only that it is what was measured. Measured 2026-08-13; full
    // method in docs/attic/2026-08-13-glow-macro-audit.md.
    //
    // PROVEN: the parameters that provably cannot move audio, each with the
    // mechanism that stops them.
    //
    // GRIT / FLUXMIX / LINK: apply_param() has no set_fx_on(), so neither FX
    //   block is ever switched on and all six are wet/dry controls of silence.
    //   This does not depend on what an operating point happens to carry -- no
    //   parameter value can switch a block on -- so the answer is the same for
    //   every sample set.
    //
    // REMOVE a PROVEN entry when its cause is fixed; this case fails on a
    // listed parameter coming back to life just as it does on a new death,
    // so the list cannot silently outlive the defect.
    bool expected_proven[P_COUNT] = {};
    for (int p : { P_GRIT_A, P_GRIT_B, P_FLUXMIX_A, P_FLUXMIX_B,
                   P_LINK_A, P_LINK_B })
        expected_proven[p] = true;

    // UNTRACED: measured dead, mechanism not established. Being listed here
    // pins CURRENT behaviour so the gate stays green and useful for
    // everything else -- it does NOT claim the deadness is correct or
    // intended. Do not move an entry to PROVEN without an actual traced
    // mechanism in its comment; do not delete an entry, narrow the point
    // set, or loosen the threshold to make one go away instead.
    //
    // SONG_B: dead on both modes even with DEPTH_B forced to its max and
    //   LANE_PITCH forced active (apply_patch()'s FORM/SONG control, task-10,
    //   2026-08-13) -- so this is not the DEPTH/_active masking that
    //   motivated that control. FORM_A, FORM_B and SONG_A all came alive
    //   under the same control (mode-exclusive gate below); SONG_B alone did
    //   not. No mechanism traced; open thread in
    //   docs/attic/2026-08-13-glow-macro-audit.md ("FORM/SONG re-measured under
    //   task 10").
    bool expected_untraced[P_COUNT] = {};
    for (int p : { P_SONG_B })
        expected_untraced[p] = true;

    // STUBBED: dead because the engine's cell is not written YET, with a task
    // that writes it. This group exists to be EMPTIED, not maintained -- it is
    // a third array rather than an entry in either list above because "not
    // built yet" is a different claim from "cannot" and from "nobody knows".
    //
    // EDGE_A / EDGE_B: the broadcast line Part::set_voice_edge reaches all six
    //   engines and FEED implements it (tests/test_voice_edge_broadcast.cpp),
    //   but SYNTH, WAVE, BODY, the sampler and the BBD carry one-line stubs
    //   that store the trim and nothing else, and NONE of the four frozen
    //   points runs FEED -- they run BODY, WAVE and SYNTH. So this reads dead
    //   here while being demonstrably live in the engine it is finished on.
    //   Tasks 4-7 of spec 2026-08-19 voice-knobs-dpth-edge replace the stubs;
    //   the FIRST of Task 4 (BODY) or Task 5 (SYNTH/WAVE) to land will redden
    //   this case, and the fix then is to DELETE this group, not to extend it.
    bool expected_stubbed[P_COUNT] = {};
    for (int p : { P_EDGE_A, P_EDGE_B })
        expected_stubbed[p] = true;

    bool expected[P_COUNT] = {};
    for (int p = 0; p < P_COUNT; ++p)
        expected[p] = expected_proven[p] || expected_untraced[p]
                   || expected_stubbed[p];

    const Terrains ter = load_points();
    bool actual[P_COUNT] = {};
    for (int p = 0; p < P_COUNT; ++p) {
        if (is_excluded(p)) continue;
        actual[p] = !moves_audio(ter.flow, p) && !moves_audio(ter.step, p);
    }
    report(actual, expected, "dead");
}

TEST_CASE("param impact: a live parameter works in both operating modes") {
    // Three groups, kept in SEPARATE arrays on purpose -- see the header
    // comment on is_excluded() and the sibling case above: this file must
    // never assert an untraced anomaly is correct, only that it is what was
    // measured.
    //
    // PROVEN: mode-exclusive by construction, not by accident, each with the
    // mechanism that makes it so.
    //
    // STEPS / SHUFFLE / TEMPO_BPM are step-grid concepts with nothing to act on
    //   in the free mode.
    // COUPLE is the mirror image: it corrects a phase error between the decks
    //   that the step grid does not leave, so it is free-mode-only.
    //
    // DENSITY used to be on this list too -- in the free mode the melodic lane
    //   was a continuous LFO (one slot, no gate) that DENSITY had nothing to
    //   act on. Spec 2026-08-13 flow-melody-engine task 8 wires the FLOW
    //   melody engine into every note engine's PITCH lane (Part::init /
    //   Part::_engine_swap push set_flow_melody(true)), which walks phrase
    //   slots and consults DENSITY's k-of-8 groove ranking in FLOW exactly as
    //   STEP already did -- so DENSITY now moves audio in both modes and drops
    //   off this list, per the comment above that predicted exactly this.
    //
    // FORM_A and FORM_B used to be on this list, on a mechanism that is gone:
    //   in STEP the melody went through shape_value()'s S&H segment, so FORM's
    //   pitch content only reached audio above SHAPE 0.75. Spec 2026-08-14
    //   melody-reachable makes _compute_raw() return the phrase directly on
    //   any note deck, in STEP as in FLOW (_note_lane()), and FORM_A now moves
    //   audio in both modes -- measured 0.797 at a FLOW point and 0.143 at a
    //   STEP one -- so it drops off this list exactly as DENSITY did. FORM_B
    //   moves in both modes too; see SAMPLE-BOUND below for why this gate
    //   still reports it as mode-exclusive.
    bool expected_proven[P_COUNT] = {};
    for (int p : { P_STEPS_A, P_STEPS_B, P_SHUFFLE, P_TEMPO_BPM, P_COUPLE })
        expected_proven[p] = true;

    // UNTRACED: measured mode-exclusive, mechanism not established. Being
    // listed here pins CURRENT behaviour so the gate stays green and useful
    // for everything else -- it does NOT claim the asymmetry is correct or
    // intended. Do not move an entry to PROVEN without an actual traced
    // mechanism in its comment; do not delete an entry, narrow the point
    // set, or loosen the threshold to make one go away instead.
    //
    // SONG_A: alive only in STEP, measured under the DEPTH/_active control
    //   from apply_patch(). Neither this nor SONG_B's outright deadness
    //   (dead-parameter gate above) is explained by that control or by
    //   anything else traced so far; both are open threads in
    //   docs/attic/2026-08-13-glow-macro-audit.md ("FORM/SONG re-measured under
    //   task 10"), not something this task fixes.
    bool expected_untraced[P_COUNT] = {};
    for (int p : { P_SONG_A })
        expected_untraced[p] = true;

    // SAMPLE-BOUND: NOT mode-exclusive in the instrument -- mode-exclusive
    // only in what this gate's kPer = 2 frozen points per mode happen to carry.
    // An entry here is a statement about the sample, not about the parameter,
    // and it is the one group whose members are expected to leave the moment
    // that sample changes. Nothing may be added here without a measurement
    // over a wider sample showing the parameter alive in both modes.
    //
    // FORM_B: was PROVEN until 2026-08-14 on the S&H mechanism above, and the
    //   melody-reachable change did reach it. Measured on 2026-08-14, while the
    //   generator still existed, with this rig's own filters over masters
    //   1..59, both guards, FORM_B moving audio at a STEP
    //   point: 1 of 13 under the old guard (master 43 only), 9 of 13 under
    //   the new one (masters 11, 13, 16, 19, 31, 43, 44, 51, 58; 0.314 ..
    //   1.183). Several of those are far below the old SHAPE 0.75 gate --
    //   master 11 at SHAPE_B 0.325, 19 at 0.483, 31 at 0.468 -- and read
    //   exactly 0.0 under the old guard, which only the phrase path explains.
    //
    //   What is left is a sampling accident, not an asymmetry: the two frozen
    //   STEP points are masters 3 and 8, which are two of the four STEP draws
    //   where FORM_B read exactly 0.0. A FORM reading of exactly 0.0 at a given
    //   point is ordinary in BOTH modes -- FORM_A reads 0.0 on FLOW masters
    //   1, 5, 7 and 9 -- so "dead in STEP" here is a property of two draws.
    //   Not the drawn step counts either: forcing steps_a = steps_b = 8 on
    //   masters 3 and 8 leaves FORM_B at exactly 0.0 on both.
    //
    //   Do NOT go hunting a mechanism downstream of the lane for this. The
    //   sample is frozen (removal spec 4.3), so this entry is now permanent
    //   unless the frozen points are re-authored by hand.
    //   The asymmetry is deliberate and is the whole difference between this
    //   group and UNTRACED above: WIDENING the sample may retire an entry,
    //   NARROWING it to keep one is the abuse UNTRACED names.
    bool expected_sample_bound[P_COUNT] = {};
    for (int p : { P_FORM_B })
        expected_sample_bound[p] = true;

    bool expected[P_COUNT] = {};
    for (int p = 0; p < P_COUNT; ++p)
        expected[p] = expected_proven[p] || expected_untraced[p] ||
                      expected_sample_bound[p];

    // Anything NOT listed in any group above that works in one mode only
    // is a defect: half of everything the instrument can be set to cannot
    // reach it.
    const Terrains ter = load_points();
    bool actual[P_COUNT] = {};
    for (int p = 0; p < P_COUNT; ++p) {
        if (is_excluded(p)) continue;
        actual[p] = moves_audio(ter.flow, p) != moves_audio(ter.step, p);
    }
    report(actual, expected, "mode-exclusive");
}

