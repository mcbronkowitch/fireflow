// engine/flow/taste.h
//
// The taste tables. ALL flow-layer tuning data lives in this one header:
// archetype weights, per-archetype base-draw spans, the v1 story library,
// weather config, gesture/blend thresholds. Data, not code -- the listening
// loop edits this file only, so every table carries a comment naming the
// spec rule it implements. All values are listening-phase first guesses.
//
// Range authority is engine/flow/flow_params.h's kParams (engine units,
// already corrected against the real engine headers: ENGINE 0..5, FORM 0..4,
// SONG 0..6, LINK unipolar 0..1). tests/test_flow_taste.cpp enforces that
// every span here stays inside those ranges and that base rules and story
// targets never share a param.
#pragma once
#include "flow/flow_ids.h"
#include "flow/flow_params.h"
// For the EngineId names in the stage-1 role tables below. Already
// transitively visible via flow_params.h -> instrument.h, so this adds no
// new edge to the include graph -- it only makes the dependency explicit.
#include "parts/engine_iface.h"

namespace spky { namespace flow {

struct Span   { float lo, hi; };                     // draw range, engine units
struct BaseRule { int param; Span per_arch[ARCH_COUNT]; };
struct CurveRule { int param; Span bp[5]; };         // per-breakpoint draw spans
struct StoryVariant {
    Macro macro; const char* name;
    int n_targets; CurveRule targets[6];             // max 6 targets per macro
};

// ---------------------------------------------------------------------------
// Scalar tuning (brief's constants, verbatim).
constexpr float kWeatherDepthMax = 0.10f, kWeatherDepthMin = 0.05f;
constexpr float kWeatherPeriodMinS = 300.f, kWeatherPeriodMaxS = 1200.f;
constexpr int   kWeatherOscMin = 2, kWeatherOscMax = 4;
constexpr float kBlendS = 6.f, kMinSpan = 0.08f;
constexpr float kTapMaxS = 0.4f, kUndoArmS = 1.5f, kLockS = 5.f;
constexpr float kMarkDelta = 0.01f;
constexpr float kRefuseFlashS = 0.25f;              // gesture.h REFUSED blink
// Spec 5, the house seed: the terrain the instrument wakes on, so the first
// sound after power-on is a decision rather than a draw. PLACEHOLDER, not a
// curated choice -- the render-based listening pass was stopped on
// 2026-08-05 (docs/superpowers/specs/2026-08-05-flow-listening-notes.md)
// because a bounced file can only judge level. This is the one opener that
// pass did measure as reasonable: its first note lands at 1.25 s, where the
// previous candidate opened with 11.4 s of silence. Re-choose it by ear once
// FireFlow Glow can actually be played.
inline constexpr char kHouseCode[] = "F1-00000020-000000000000";
constexpr float kDistanceMin = 0.18f;               // NEW rejection threshold
// §7.8 ceiling, lin FS. A SPEC NUMBER, not a measured one: it says how loud
// the calm corner is allowed to be, so it is never fitted to what the
// generator happens to produce.
//
// RE-MEASURED 2026-08-06 (mode work). tests/test_flow_audio.cpp's calm-corner
// case is RED on master 0x707 at rms 0.0824, and it is left red on purpose --
// raising this ceiling to cover it would be retuning a spec bound to fit a
// generator defect. What the measurement found:
//
//  - It is not a transient the gate's 3 s skip misses. Rendered out to 120 s,
//    0x707's calm corner converges on rms 0.0789, with a swell recurring
//    roughly every 30 s that peaks at rms 0.15 / sample peak 0.73.
//  - P_MODE is causal FOR THIS SEED: forcing kModeW to all-zero (FLOW) drops
//    0x707 to a steady rms 0.056; forcing all-one (STEP) reproduces 0.0824.
//  - But the mode work did not CREATE the problem, it reshuffled which seeds
//    hit it. Scanning masters 1..2000 (1 566 non-Sampler terrains) at the calm
//    corner: 21 breach this ceiling, 1.34 % -- 1.7 % of FLOW terrains and
//    1.0 % of STEP terrains, every one of them a drone, worst rms 0.180. The
//    SAME scan at the pre-mode commit 651ee2c breaches on 19 of 1 566, 1.2 %.
//    The rate is unchanged; only the membership moved, and 0x707 moved in.
//
// So the real finding is about the gate, not the constant: this ceiling
// samples a property the generator holds only ~98.7 % of the time, and it
// read green before purely because none of the ten fixed candidate seeds sat
// in the breaching 1.3 %. Either the generator has to guarantee a quiet calm
// corner for drone terrains, or §7.8 has to say what fraction it tolerates.
// That is an ear-and-spec decision, not a code-side one. Do not "fix" it by
// moving this number or by dropping 0x707 from the seed set.
constexpr float kCalmCornerRmsMax = 0.06f;
// §7.8 floor -- Task 10. This is a SILENCE DETECTOR, not the musical
// target: measurements at the reference terrain/seed put the calm corner
// at RMS ~= 0.00092 (-61 dBFS), about 1.5% of kCalmCornerRmsMax and, in
// practice, inaudible; a spread of terrains sampled for this task's own
// gate ranged from ~0.00006 to ~0.0035 (macros parked at 0, so archetype
// and per-terrain draws are the only source of the spread -- a sparse
// drone terrain legitimately sits much quieter than a busier one even at
// the calm corner). Whether any of these levels is the right background
// for "receding but present" (spec §3) is an open listening-loop
// question -- this constant does not answer it and must never be retuned
// to try. It exists only to catch a calm corner that has gone MUTE (a
// future taste-table or runtime change that accidentally zeroes the quiet
// decks), set here at a value comfortably below the quietest terrain this
// task measured but still well clear of digital silence (exact-zero
// output, which is what an accidental mute actually produces).
constexpr float kCalmCornerRmsMin = 1e-5f;          // -100 dBFS, silence floor
// §7.8 NEW-blend level gate -- Task 10, round 2. The original design (a raw
// window-to-window RMS ratio inside ONE render) conflated the instrument's
// own note-envelope/retrigger dynamics with anything the blend itself did,
// and failed on every seed tested (up to 136 dB on one). The fix is
// DIFFERENTIAL: render a no-press control alongside the press run on the
// same terrain/macros, and compare the two at the same window index --
// native dynamics appear in both and cancel, so what survives is what the
// blend actually changed.
constexpr float kBlendLevelFloorDb = -80.f;         // dBFS floor before the
// press-vs-control comparison: a windowed RMS is converted to dBFS and
// clamped at this floor first, so a recovery from near-silence produces a
// bounded, comparable dB delta instead of a ratio that blows up toward
// infinity (an unfloored ratio hit 136 dB on one seed purely from dividing
// by a nearly-zero window).
constexpr float kBlendSpikeDb = 6.f;                // press vs. control,
// spike ceiling: a NEW blend is specified (spec §5) as a crossfade -- the
// instrument changes WHAT it is playing, not how loud -- so the press run
// should never read more than this many dB LOUDER than the no-press
// control at the same instant.
//
// RE-MEASURED 2026-08-06 (mode work), by the differential press-vs-control
// comparison this bound is asserted with. Worst in-gate spike across the four
// no-switch seeds is now +3.02 dB (master 0x404, window 3), leaving 2.98 dB
// of headroom -- BETTER than the 1.14 dB the pre-mode measurement left, and
// the previous worst case (+4.86 dB, master 0x808 window 2) is now -1.68 dB.
// THE CONCERN RAISED WHEN THE SECOND DUCK LANDED DID NOT MATERIALIZE: a
// mode-changing press puts the carrier's clocking-flip duck inside the gate
// window for the first time, and the measured spike went DOWN, not up. Two of
// the four asserted seeds (0x404, 0x808) do change mode on their press, so
// the path is covered non-vacuously. Bound holds unchanged.
constexpr float kBlendDropDb = 10.f;                // press vs. control,
// drop floor: symmetric case -- more than this many dB QUIETER than the
// control is the deck audibly leaving the mix, which a crossfade must not
// do either. Not the same magnitude as the spike ceiling: a NEW blend
// legitimately ducks the outgoing deck's dry leg by design (kDuckDepth),
// so some asymmetric headroom on the downside is expected even in a
// healthy blend.
//
// RE-MEASURED 2026-08-06 (mode work), same run as kBlendSpikeDb above: worst
// in-gate drop is -5.94 dB (master 0x808, window 0), 4.06 dB of headroom.
// Bound holds unchanged.
// §7.8 NEW-blend level gate -- Task 10, round 4 (review round 1 found the
// round-3 comment below quantitatively wrong; this is the corrected
// version -- see task-10-report.md rounds 3-5 for the full history).
//
// The differential design (kBlendSpikeDb/kBlendDropDb above) stayed red
// past round 2 even on seeds with no engine switch, and the reason turned
// out to be the REFERENCE, not the blend: the no-press control keeps
// playing the OUTGOING terrain for the full 6 s while the press run
// settles onto a completely different, independently-drawn terrain, so
// comparing the two once the blend is mostly or fully settled compares two
// terrains' natural loudness, not anything the blend did (measured up to
// ~15.9 dB apart -- tests/test_flow_audio.cpp's fixed-seed RMS case: 0.0158
// to 0.0983 across 8 terrains at the same macro setting, no blend
// involved).
//
// kBlendGateWindowS = 1.0 s IS NOT A DERIVATION. It is a conservative
// CHOICE, checked (not fitted) against the failure boundary. Pre-mode, the
// gate stayed green unmutated out to 1.75 s and went red at 2.0 s, so 1.0 s
// sat 0.75 s inside the boundary.
//
// RE-MEASURED 2026-08-06 (mode work): THE BOUNDARY MOVED IN. Widening the
// gate's scope window by window against the current generator, the worst
// spike/drop over the no-switch seeds reads 1.00 s: +3.02/-5.94 (green),
// 1.25 s: +4.16/-7.00 (green), 1.50 s: +13.34/-10.03 (RED). It goes red on
// the window that CONTAINS kCarrierStaggerFrac * kBlendS = 1.5 s -- the
// carrier deck's own engine/scale switch, which is where the excursion
// lives. So 1.0 s now sits 0.50 s inside the boundary, not 0.75 s.
//
// (Why 1.25 s is still green even though the stagger duck's leading edge
// starts at ~1.25 s: the scope moves in whole 0.25 s RMS windows, so a
// "1.25 s" scope asserts on windows 0-4, i.e. everything BEFORE t = 1.25 s.
// The duck's leading edge and the switch itself both land in window 5
// (1.25-1.50 s), which is the first window a 1.50 s scope adds. The two
// figures agree; the granularity is the whole story.) The WINDOW IS NOT WIDENED to
// recover the old margin, and it is not narrowed either: it is still green
// where it stands, and moving it to chase a margin would be fitting.
//
// A phase-math argument was tried here first ("continuous params are
// (1-p)*old + p*new, so legitimate divergence is roughly p * the terrain
// gap, ~2.7 dB at p=1/6") and IT IS WRONG -- it omits discretes, which
// never lerp (flow.cpp: `_resid[p] = kParams[p].steps > 0 ? 0.f : ...`,
// `v = due ? cur[p] : _pushed[p]`) and switch 100% at their scheduled
// phase, not p*100%. Measured in-window divergence is therefore several
// times the phase-math prediction: at w=2 (~0.6 s) the model predicts
// ~1.6 dB, the pre-mode measured value (master 0x808) was +4.86 dB. (The
// argument is still the point; the number is superseded -- re-measured
// 2026-08-06, the worst in-gate spike is +3.02 dB and 0x808 w=2 now reads
// -1.68 dB. See kBlendSpikeDb above for the current headroom, 2.98 dB.)
//
// THE GATE'S BLIND SPOT, STATED PLAINLY -- and NARROWED on 2026-08-06 by the
// second carrier duck (spec 2026-08-06 §5, flow.cpp begin_blend):
//
//  - The texture deck's discrete switch happens at the press (phase 0),
//    inside this window. Covered, as before.
//  - On a MODE-CHANGING press the carrier deck now gets a duck AT THE PRESS
//    too, because set_sync is global and the clocking flip lands at phase 0.
//    That duck is inside this window, so for the first time the gate sees a
//    carrier-deck event. Two of the four asserted seeds (0x404, 0x808) take
//    this path, and it measures clean (see kBlendSpikeDb).
//  - The carrier deck's OWN engine/scale switch still happens at
//    kCarrierStaggerFrac * kBlendS = 0.25 * 6 = 1.5 s, and its stagger duck
//    (kDuckWindowS = 0.5 s) still spans roughly 1.25-1.75 s -- STILL
//    ENTIRELY OUTSIDE kBlendGateWindowS = 1.0 s, for every seed, by
//    construction. That is where the +13.34 dB in the scope check above
//    lives, and pre-mode it was proven by deferring a bare-step mutation to
//    t = 1.5 s: it passes this gate completely while the unasserted windows
//    read as low as -24.04 dB.
//
// So: this gate covers the texture deck's switch, the initial retarget, and
// (new) the carrier's clocking flip on a mode change. It does NOT cover the
// carrier deck's engine switch or its stagger duck -- do not describe it as
// catching "an over-attenuating duck" in general; for that event it
// structurally cannot.
constexpr float kBlendGateWindowS = 1.0f;           // seconds after the
                                                     // press the differential
                                                     // gate actually asserts on
// §7.8 fixed-seed RMS bounds (Task 10, review I-3: was inline in the test).
// 0.001 is two orders of magnitude below kCalmCornerRmsMax, so it only
// fires if the "busy" (all macros 0.5) setting is somehow quieter than the
// calm corner; 0.5 sits below full scale. Both are the brief's own numbers,
// deliberately not fitted to today's output.
//
// RE-MEASURED 2026-08-06 (mode work), by the measurement the old comment
// names: windowed RMS over a 10 s render at all macros 0.5, across the
// candidate terrains. Band is now 0.0181..0.1011. BOTH CONSTANTS HOLD
// UNCHANGED, with the same margins the old comment describes: 25.2 dB below
// the quietest terrain, 13.9 dB above the loudest (was ~24 dB and ~14 dB).
//
// THE TWO BANDS ARE OVER DIFFERENT TERRAIN SETS, so do not read the change
// as the mode draw's doing. The old band (0.0158..0.0983) covered 8
// terrains; this one covers 10, because review I-4 later added 0xD0D and
// 0xC0C0 to kCandidateMasters (tests/test_flow_audio.cpp) without restating
// this comment. The new low end, 0.0181, IS 0xC0C0 -- one of the two added
// seeds. What the mode work did NOT change is the seed set's composition:
// engine draws come from per-param streams, so all ten masters draw exactly
// the same engine pair at 651ee2c as at HEAD (checked in a worktree), and
// all ten clear the Sampler filter in both. Attributing any part of the band
// shift to the mode draw would need a per-seed before/after, which was not
// run -- these bounds hold on the measured band either way.
constexpr float kFixedSeedRmsMin = 0.001f;
constexpr float kFixedSeedRmsMax = 0.5f;
// §7.8 discrete-churn gate storied-param bound (Task 10, review I-3: was
// inline in the test as kStoriedChurnMax). Reasoning lives with the gate
// in tests/test_flow_audio.cpp (the quarter-cycle weather-monotonicity
// argument): non-storied discretes are bound at exactly 0 (structural, not
// here); storied discretes are bound at this many changes per 60 s static-
// macro window, decided before measuring and confirmed non-vacuous in both
// directions (max observed: 1).
//
// RE-MEASURED 2026-08-06 (mode work), because P_MODE is a new discrete that
// could in principle churn. It cannot: P_MODE has no story owner (its
// kBaseRules row is the placeholder below), so it is bound at exactly 0 by
// the same structural argument as every other non-storied discrete --
// measured 0 changes on all 10 candidate terrains, in both FLOW and STEP.
// Worst storied churn over 60 s is still 1 (P_STEPS_A), worst non-storied
// still 0. BOUND HOLDS UNCHANGED and is still non-vacuous.
constexpr int kDiscreteChurnMax = 2;
constexpr float kBodyFiltFloor = -0.3f;             // BODY FILT cliff margin
constexpr float kSpaceSlewS = 2.5f;                 // lazy SIZE/DECAY follower
constexpr float kHysteresisFrac = 0.5f;             // half a discrete step

// NEW gesture (spec §5). The two decks' discrete params (engine, and the
// deck-scoped FORM/SONG/STEPS) never switch together: the texture deck goes
// at the start of the blend, the carrier deck this far into it. Global
// discretes (SCALE/ROOT) ride with the carrier, so the tonality lands with
// the lead voice rather than ahead of it.
constexpr float kCarrierStaggerFrac = 0.25f;        // fraction of kBlendS
// Each switch happens under a duck of that deck's reverb send: a
// raised-cosine hump of this total width, centred on the switch instant
// (the texture deck's rising half falls before the press and is simply
// clipped, so its duck opens at full and returns smoothly). The duck is a
// MAXIMUM against the macro-computed send -- it may only add wetness.
constexpr float kDuckWindowS   = 0.5f;              // total hump width
constexpr float kDuckWetTarget = 0.95f;             // send value it aims at
constexpr float kDuckDepth     = 0.8f;              // how far it gets there

// ---------------------------------------------------------------------------
// Hard by-ear limits (spec 2026-08-06 §3). These hold under EVERY archetype,
// every macro position, every weather offset and every adventure level. A row
// here is a claim that no music in this box ever wants that value.
//
// THIS IS NOT THE COMPLETE LIST OF HARD LIMITS. Two live elsewhere on purpose:
//   - P_RES's 0.75 ceiling is in kParams (flow_params.h) because that range
//     also normalises the terrain distance metric in terrain.cpp.
//   - kBodyFiltFloor is a runtime clamp in flow.cpp because it is conditional
//     on a deck's engine, and this table is engine-independent.
struct Veto { int param; float lo, hi; };
inline const Veto kVetos[] = {
    { P_REV_MOD,  0.00f, 0.25f },  // above: the reverb tail comes apart
    { P_DRIVE,    0.00f, 0.40f },  // above: the limiter rides and DRIVE stops
                                   // controlling dirt (it only gets louder)
    { P_COMP_A,   0.10f, 0.50f },  // never uncompressed, never squashed
    { P_COMP_B,   0.10f, 0.50f },
    { P_REVMIX_A, 0.08f, 1.00f },  // never fully dry
    { P_REVMIX_B, 0.08f, 1.00f },
};
inline const int kVetoCount = int(sizeof(kVetos) / sizeof(kVetos[0]));

// ---------------------------------------------------------------------------
// Archetype draw weights (drone-heavy per the spec: this is an ambient box).
// Order: {ARCH_DRONE, ARCH_PULSE, ARCH_ARP, ARCH_FRAGMENT}.
inline const float kArchWeight[ARCH_COUNT] = { 0.5f, 0.2f, 0.15f, 0.15f };

// ---------------------------------------------------------------------------
// Stage-1 role tables (spec §4 stage 1). Carrier may only be a pitched
// sustaining engine {SYNTH, WAVE, BODY}; texture may be any of the five
// real engines. ENGINE_TEST_TONE is in neither list -- the generator must
// never roll it -- and with one carrier deck plus one texture deck the
// "loud pair" rule (never Sampler+BBD together) is structural: both loud
// engines are excluded from the carrier list. Weights are per-archetype
// listening-phase first guesses.
inline constexpr int kCarrierEngine[3] = { ENGINE_SYNTH, ENGINE_WAVE,
                                           ENGINE_BODY };
inline constexpr float kCarrierW[ARCH_COUNT][3] = {
    { 0.30f, 0.40f, 0.30f },   // drone: wavetables and bowed bodies sustain
    { 0.50f, 0.25f, 0.25f },   // pulse: the synth's envelopes pulse best
    { 0.55f, 0.30f, 0.15f },   // arp: fast retriggers favor the synth
    { 0.35f, 0.35f, 0.30f },   // fragment: anything broken works
};
inline constexpr int kTextureEngine[5] = { ENGINE_SYNTH, ENGINE_SAMPLER,
                                           ENGINE_WAVE, ENGINE_BODY,
                                           ENGINE_BBD };
inline constexpr float kTextureW[ARCH_COUNT][5] = {
    { 0.20f, 0.20f, 0.25f, 0.20f, 0.15f },   // drone
    { 0.25f, 0.20f, 0.20f, 0.15f, 0.20f },   // pulse
    { 0.30f, 0.15f, 0.25f, 0.15f, 0.15f },   // arp
    { 0.20f, 0.30f, 0.15f, 0.15f, 0.20f },   // fragment: grains and echoes
};

// P_MODE draw weights (spec 2026-08-06 §5): probability that a terrain of this
// archetype comes out STEP/synced rather than FLOW/free. A drone normally has
// no step sequencer at all; an arp is one almost by definition.
// Order: {ARCH_DRONE, ARCH_PULSE, ARCH_ARP, ARCH_FRAGMENT}.
inline constexpr float kModeW[ARCH_COUNT] = { 0.15f, 0.90f, 0.95f, 0.75f };

// ---------------------------------------------------------------------------
// Story library, one variant per macro (DENSITY gets two). Implements §3's
// table: each target is a 5-breakpoint curve of draw spans; the macro knob
// interpolates bp0..bp4 (Q1..Q4 with center at bp2). Q4 hi values may sit at
// a param's full ceiling on purpose -- that is the risk zone.
inline const StoryVariant kStories[] = {
// MOTION "still photo -> breathing -> wobble -> seasick" (§3 row 3).
// Q4 hi values deliberately sit at the params' full ceiling: the risk zone.
{ M_MOTION, "orbit", 4, {
  { P_TIDE,      {{0.f,.05f},{.1f,.2f},{.25f,.4f},{.45f,.6f},{.7f,1.f}} },
  { P_DRIFT,     {{0.f,.05f},{.05f,.15f},{.2f,.35f},{.4f,.55f},{.6f,.9f}} },
  // SMEAR is the diffuser LFO (the wash). It carries the seasick end now that
  // WOBL is capped: smear washes the reverb where MOD tears it.
  { P_REV_SMEAR, {{0.f,.05f},{.05f,.2f},{.2f,.4f},{.4f,.65f},{.65f,.95f}} },
  // WOBL, capped at the veto (kVetos: P_REV_MOD 0.00-0.25). Flattens out
  // rather than stopping dead, so the top of the knob still moves it -- just
  // inside the band that survives. bp4 lo is .20, not the ".10" a naive
  // rescale would give: test_flow_taste.cpp holds every curve's lo bounds
  // monotone ascending, and .10 sits below bp3's lo of .14.
  { P_REV_MOD,   {{0.f,.03f},{.03f,.08f},{.08f,.14f},{.14f,.20f},{.20f,.25f}} } } },
// DENSITY rate-led (§3 row 2a): events carry the sweep.
{ M_DENSITY, "rate", 3, {
  { P_DENSITY_A, {{.02f,.08f},{.1f,.2f},{.3f,.5f},{.5f,.7f},{.7f,.95f}} },
  { P_DENSITY_B, {{.02f,.08f},{.08f,.18f},{.25f,.45f},{.45f,.65f},{.65f,.9f}} },
  { P_STEPS_A,   {{2.f,4.f},{4.f,6.f},{6.f,10.f},{10.f,13.f},{13.f,16.f}} } } },
// DENSITY thickness-led (§3 row 2b): chords/pad carry it.
{ M_DENSITY, "thick", 3, {
  { P_COLOR_A,   {{0.f,.1f},{.15f,.3f},{.35f,.55f},{.55f,.75f},{.75f,1.f}} },
  { P_COLOR_B,   {{0.f,.1f},{.1f,.25f},{.3f,.5f},{.5f,.7f},{.7f,.95f}} },
  { P_SUB_A,     {{.1f,.2f},{.2f,.35f},{.35f,.5f},{.5f,.65f},{.6f,.8f}} } } },
// BRIGHT "ember -> sweep -> open -> air" (§3 row 1). Q1 dips the dry leg
// via REVMIX (the spec-named level mechanism) and blooms REV_DECAY --
// REV_DECAY's curve here runs HIGH at bp0 and settles by bp1: monotone
// falling, active only in Q1. REVMIX likewise falls Q1-only -- bottoming at
// the veto floor (kVetos: P_REVMIX_A/B >= 0.08), not toward dry silence,
// because the reverb send is never allowed fully dry.
// FILT bp0 lo (-0.55) is below kBodyFiltFloor on purpose: the BODY margin
// is a runtime clamp (Task 7), not a table limit -- other engines may dive.
{ M_BRIGHT, "dawn", 5, {
  { P_FILT_A,    {{-.55f,-.4f},{-.3f,-.1f},{0.f,.2f},{.3f,.5f},{.6f,.9f}} },
  { P_FILT_B,    {{-.55f,-.4f},{-.3f,-.1f},{0.f,.2f},{.3f,.5f},{.6f,.9f}} },
  { P_REV_TONE,  {{.1f,.2f},{.25f,.4f},{.4f,.55f},{.55f,.7f},{.7f,.9f}} },
  { P_REVMIX_A,  {{.75f,.9f},{.45f,.6f},{.4f,.55f},{.4f,.55f},{.4f,.55f}} },
  { P_REV_DECAY, {{.75f,.9f},{.5f,.65f},{.5f,.65f},{.5f,.65f},{.5f,.65f}} } } },
// DIRT "clean glue -> warmth -> grit -> risk + DRIVE threshold" (§3 row 4).
// P_DRIVE flat 0 through Q1..Q3, joins in Q4 only (the threshold rule:
// once the limiter rides, DRIVE stops controlling dirt -- so it is a Q4
// commitment, not a gradual blend).
{ M_DIRT, "heat", 4, {
  { P_GRIT_A,    {{0.f,0.f},{.05f,.15f},{.2f,.4f},{.45f,.65f},{.7f,1.f}} },
  { P_GRIT_B,    {{0.f,0.f},{.05f,.12f},{.15f,.35f},{.4f,.6f},{.65f,.95f}} },
  // COMP rescaled into 0.10-0.50, relative shape kept.
  { P_COMP_A,    {{.25f,.38f},{.25f,.38f},{.28f,.42f},{.32f,.46f},{.35f,.50f}} },
  // PUSH joins in Q4 only (the threshold rule), inside the veto band.
  { P_DRIVE,     {{0.f,0.f},{0.f,0.f},{0.f,0.f},{0.f,.05f},{.25f,.40f}} } } },
// WANDER "frozen -> fine variation -> melodic wander -> FORM/SONG churn"
// (§3 row 5). FORM/SONG are discrete: flat until Q4, hysteresis in Task 7.
// Q4 hi extended vs the plan (which stopped at 3) toward the corrected
// kParams maxima: FORM reaches 4 (Ostinato -- still generates material, not
// a disable state). SONG stops at 5 (Mirror): SongMode 6 is Off
// (song_form.h -- song_symbol_at returns constant 0, no alternation), and a
// wander knob must never disable wandering, so Off is deliberately excluded.
{ M_WANDER, "path", 4, {
  { P_VARIATION_A, {{0.f,0.f},{.05f,.15f},{.25f,.45f},{.5f,.7f},{.75f,1.f}} },
  { P_VARIATION_B, {{0.f,0.f},{.05f,.15f},{.25f,.45f},{.5f,.7f},{.75f,1.f}} },
  { P_FORM_A,    {{0.f,0.f},{0.f,0.f},{0.f,1.f},{1.f,2.f},{2.f,4.f}} },
  { P_SONG_A,    {{0.f,0.f},{0.f,0.f},{0.f,1.f},{1.f,2.f},{2.f,5.f}} } } },
// SPACE "intimate -> room -> hall -> dissolve" (§3 row 6). SIZE/DECAY get
// the lazy follower in the runtime (kSpaceSlewS); dry duck at Q4 comes from
// REVMIX riding high (equal-power: wet up = dry down).
{ M_SPACE, "bloom", 4, {
  { P_REVMIX_A,  {{.08f,.15f},{.15f,.3f},{.35f,.5f},{.5f,.7f},{.75f,.95f}} },
  { P_REVMIX_B,  {{.08f,.15f},{.15f,.3f},{.35f,.5f},{.5f,.7f},{.75f,.95f}} },
  { P_REV_SIZE,  {{.2f,.35f},{.35f,.5f},{.5f,.65f},{.65f,.8f},{.8f,.95f}} },
  { P_REV_DECAY, {{.3f,.4f},{.4f,.5f},{.5f,.65f},{.65f,.8f},{.8f,.92f}} } } },
};
inline const int kStoryCount = int(sizeof(kStories) / sizeof(kStories[0]));

// ---------------------------------------------------------------------------
// Base draw rules: one row for EVERY ParamId that no story owns (the test
// enforces both directions -- no overlap with stories, no uncovered param).
// Span order per row: {ARCH_DRONE, ARCH_PULSE, ARCH_ARP, ARCH_FRAGMENT}.
// Archetype intent (spec §4): drone = sparse/slow/long envelopes; pulse =
// mid density, short envelopes; arp = fast, many steps; fragment = broken,
// short decays, more shuffle. Params where the archetype should not matter
// get four identical spans.
// NOTE: VARIATION_A/B are owned by the WANDER story, so "fragment widens
// VARIATION" cannot live here; fragment gets wider SHUFFLE/RANGE instead.
inline const BaseRule kBaseRules[] = {
// -- discrete world picks -------------------------------------------------
// ENGINE_A/B: full legal range 0..5 (TEST_TONE..BBD per engine_iface.h).
// Stage 1's roles logic OVERRIDES these draws -- the rows exist only so the
// table has no hole; do not tune them expecting audible effect.
{ P_ENGINE_A, {{0.f,5.f},{0.f,5.f},{0.f,5.f},{0.f,5.f}} },   // stage 1 overrides
{ P_ENGINE_B, {{0.f,5.f},{0.f,5.f},{0.f,5.f},{0.f,5.f}} },   // stage 1 overrides
// P_MODE: drawn from kModeW, NOT from this span. The row exists only so the
// coverage test has no hole -- exactly like the P_ENGINE_A/B rows above. Do
// not tune it expecting audible effect.
{ P_MODE,     {{0.f,1.f},{0.f,1.f},{0.f,1.f},{0.f,1.f}} },
{ P_SCALE,    {{0.f,12.f},{0.f,12.f},{0.f,12.f},{0.f,12.f}} }, // any scale
{ P_ROOT,     {{0.f,11.f},{0.f,11.f},{0.f,11.f},{0.f,11.f}} }, // any root
{ P_FORM_B,   {{0.f,4.f},{0.f,4.f},{0.f,4.f},{0.f,4.f}} },   // any principle
{ P_SONG_B,   {{0.f,6.f},{0.f,6.f},{0.f,6.f},{0.f,6.f}} },   // any song mode
// -- event rate / step count ---------------------------------------------
{ P_STEPS_B,  {{2.f,6.f},{4.f,10.f},{8.f,16.f},{4.f,12.f}} },  // arp = many
{ P_RATE_A,   {{0.f,.25f},{.3f,.6f},{.55f,.9f},{.3f,.7f}} },   // drone = slow
{ P_RATE_B,   {{0.f,.25f},{.3f,.6f},{.55f,.9f},{.3f,.7f}} },   // drone = slow
// -- pitch ----------------------------------------------------------------
{ P_TUNE_A,   {{.25f,.75f},{.25f,.75f},{.25f,.75f},{.25f,.75f}} }, // neutral
{ P_TUNE_B,   {{.25f,.75f},{.25f,.75f},{.25f,.75f},{.25f,.75f}} }, // neutral
{ P_RANGE_A,  {{.1f,.4f},{.2f,.5f},{.4f,.8f},{.3f,.7f}} },     // arp = wide
{ P_RANGE_B,  {{.1f,.4f},{.2f,.5f},{.4f,.8f},{.3f,.7f}} },     // arp = wide
// -- timbre wildcards -----------------------------------------------------
{ P_SHAPE_A,  {{0.f,1.f},{0.f,1.f},{0.f,1.f},{0.f,1.f}} },     // wildcard
{ P_SHAPE_B,  {{0.f,1.f},{0.f,1.f},{0.f,1.f},{0.f,1.f}} },     // wildcard
{ P_SMOOTH_A, {{.5f,.9f},{.2f,.5f},{.2f,.5f},{.1f,.4f}} },     // drone = glassy
{ P_SMOOTH_B, {{.5f,.9f},{.2f,.5f},{.2f,.5f},{.1f,.4f}} },     // drone = glassy
{ P_DEPTH_A,  {{.2f,.7f},{.2f,.7f},{.2f,.7f},{.2f,.7f}} },     // neutral
{ P_DEPTH_B,  {{.2f,.7f},{.2f,.7f},{.2f,.7f},{.2f,.7f}} },     // neutral
// -- envelopes ------------------------------------------------------------
{ P_ATTACK_A, {{.5f,.95f},{0.f,.15f},{0.f,.2f},{0.f,.3f}} },   // drone = long
{ P_ATTACK_B, {{.5f,.95f},{0.f,.15f},{0.f,.2f},{0.f,.3f}} },   // drone = long
{ P_DECAY_A,  {{.6f,.95f},{.15f,.45f},{.2f,.5f},{.1f,.35f}} }, // fragment = short
{ P_DECAY_B,  {{.6f,.95f},{.15f,.45f},{.2f,.5f},{.1f,.35f}} }, // fragment = short
// -- voice color ----------------------------------------------------------
{ P_RES_A,    {{0.f,.6f},{0.f,.6f},{0.f,.6f},{0.f,.6f}} },     // under 0.75 cap
{ P_RES_B,    {{0.f,.6f},{0.f,.6f},{0.f,.6f},{0.f,.6f}} },     // under 0.75 cap
{ P_SUB_B,    {{.1f,.6f},{.1f,.6f},{.1f,.6f},{.1f,.6f}} },     // neutral
// -- fx sends -------------------------------------------------------------
{ P_FLUXMIX_A, {{0.f,.5f},{0.f,.5f},{0.f,.5f},{0.f,.5f}} },    // neutral
{ P_FLUXMIX_B, {{0.f,.5f},{0.f,.5f},{0.f,.5f},{0.f,.5f}} },    // neutral
{ P_COMP_B,   {{.3f,.5f},{.3f,.5f},{.3f,.5f},{.3f,.5f}} },     // gentle glue
{ P_LINK_A,   {{0.f,.6f},{0.f,.6f},{0.f,.6f},{0.f,.6f}} },     // unipolar 0..1
{ P_LINK_B,   {{0.f,.6f},{0.f,.6f},{0.f,.6f},{0.f,.6f}} },     // unipolar 0..1
// -- global modulation / mix ---------------------------------------------
{ P_MORPH,    {{.2f,.8f},{.2f,.8f},{.2f,.8f},{.2f,.8f}} },     // neutral
{ P_COUPLE,   {{0.f,.5f},{0.f,.5f},{0.f,.5f},{0.f,.5f}} },     // neutral
{ P_CHOKE,    {{-.25f,.25f},{-.25f,.25f},{-.25f,.25f},{-.25f,.25f}} }, // near center (by-ear states)
{ P_SHUFFLE,  {{0.f,.1f},{0.f,.35f},{0.f,.3f},{.1f,.5f}} },    // fragment = loose
// -- reverb character (DIFF = density, per the reverb mod split) ----------
{ P_REV_DIFF, {{.4f,.8f},{.4f,.8f},{.4f,.8f},{.4f,.8f}} },     // dense-ish
// -- clock ----------------------------------------------------------------
{ P_TEMPO_BPM, {{55.f,75.f},{80.f,110.f},{90.f,130.f},{70.f,110.f}} }, // drone = slow
};
inline const int kBaseRuleCount = int(sizeof(kBaseRules) / sizeof(kBaseRules[0]));

} } // namespace spky::flow
