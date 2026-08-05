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
constexpr float kDistanceMin = 0.18f;               // NEW rejection threshold
constexpr float kCalmCornerRmsMax = 0.06f;          // §7.8 ceiling, lin FS
constexpr float kBodyFiltFloor = -0.3f;             // BODY FILT cliff margin
constexpr float kSpaceSlewS = 2.5f;                 // lazy SIZE/DECAY follower
constexpr float kHysteresisFrac = 0.5f;             // half a discrete step

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
  { P_REV_SMEAR, {{0.f,.05f},{.05f,.15f},{.15f,.3f},{.3f,.5f},{.5f,.8f}} },
  { P_REV_MOD,   {{0.f,.05f},{.05f,.1f},{.1f,.25f},{.25f,.45f},{.45f,.85f}} } } },
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
// falling, active only in Q1. REVMIX likewise falls Q1-only.
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
  { P_COMP_A,    {{.3f,.5f},{.3f,.5f},{.35f,.55f},{.4f,.6f},{.5f,.75f}} },
  { P_DRIVE,     {{0.f,0.f},{0.f,0.f},{0.f,0.f},{0.f,.05f},{.3f,.7f}} } } },
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
  { P_REVMIX_A,  {{.02f,.1f},{.15f,.3f},{.35f,.5f},{.5f,.7f},{.75f,.95f}} },
  { P_REVMIX_B,  {{.02f,.1f},{.15f,.3f},{.35f,.5f},{.5f,.7f},{.75f,.95f}} },
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
{ P_COMP_B,   {{.3f,.6f},{.3f,.6f},{.3f,.6f},{.3f,.6f}} },     // gentle glue
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
