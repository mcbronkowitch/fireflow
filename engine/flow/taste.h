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
// For kDivisionCount, the length of the rate-rung weight table below.
#include "mod/divisions.h"

namespace spky { namespace flow {

// Span itself lives in flow_params.h now (moved 2026-08-06, final review) --
// see that header for why. Still visible here unqualified via the
// flow_params.h include above.
struct BaseRule { int param; Span per_arch[ARCH_COUNT]; };
struct CurveRule { int param; Span bp[5]; };         // per-breakpoint draw spans
struct StoryVariant {
    Macro macro; const char* name;
    int n_targets; CurveRule targets[6];             // max 6 targets per macro
    // Where each archetype reads this story (spec 2026-08-06 §4). The knob
    // still sweeps its full physical travel; only the sampling position is
    // remapped, so a narrower window means the macro covers a smaller part of
    // the story and never reaches the rest. Default is the whole curve, and
    // this member is LAST with a default initialiser so the existing
    // positional entries in kStories need no edit.
    Span arch_window[ARCH_COUNT] = {{0.f,1.f},{0.f,1.f},{0.f,1.f},{0.f,1.f}};
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
// RE-MEASURED 2026-08-06 (Task 7, the taste tables). THE 0x707 BREACH THAT
// MADE THIS GATE RED IS GONE, and the constant did not move to make that
// happen. 0x707's calm corner now renders at rms 4.25e-04, 43.0 dB under this
// ceiling; that is a 45.75 dB REDUCTION from the 8.24e-02 it rendered before
// the branch, which is a different quantity from the margin.
//
// WHICH CHANGE RETIRED IT, measured rather than guessed. The calm-corner
// render (macros 0, 10 s, first 3 s skipped -- the gate's own shape) was run
// at every commit of this branch from a worktree at the branch point 4ec5be0:
//
//   4ec5be0 .. ab76a97   0x707 rms 0.0824 -> 0.0787   OVER
//   3435c31 (base rules) 0x707 rms 0.00665            under
//   ...
//   89eb461 (Task 7 measurement point)       0x707 rms 4.25e-04           under
//
// So the ceiling breach stopped reproducing at 3435c31. Inside that commit it
// is specifically the DRONE SHAPE CAP (P_SHAPE_A/B drone span {0,1} ->
// {0,.25}, below). Isolated by reverting each of that commit's three table
// edits in turn, at that commit, and re-rendering -- 0x404 is carried in the
// same run because kCalmCornerRmsMin's comment below rests on it:
//
//   3435c31 as shipped            0x707 6.645e-03  0x404 7.00e-08
//   drone SHAPE cap reverted      0x707 9.920e-02  0x404 1.35e-03
//   DIFF narrowing reverted       0x707 6.689e-03  0x404 6.93e-08
//   drone STEPS_B widening rev.   0x707 2.561e-02  0x404 7.00e-08
//
// Reverting the SHAPE cap alone puts 0x707 back OVER this ceiling; reverting
// either other edit leaves it well under. One span, isolated by reversion --
// and the same one span is what silences 0x404.
//
// THE §7.8 FINDING FROM THE MODE WORK STILL STANDS, and it got smaller. The
// same masters 1..2000 scan (1 566 non-Sampler terrains) now breaches on 8,
// 0.51 %, worst rms 0.139; at 4ec5be0 the same scan breaches on 21, 1.34 %,
// worst 0.180. This ceiling is a property the generator does not GUARANTEE --
// it holds for ~99.5 % of terrains, not all of them.
//
// RULED BY THE OWNER 2026-08-07, THE SAME RULING THE BLEND GATE TOOK: the
// fraction is ACCEPTED, §7.8 states it (kCalmLoudFracMax below), and the gate
// asserts a rate over a population instead of resting on ten fixed seeds that
// happened to miss the breaching half-percent. THIS CONSTANT IS UNCHANGED and
// still means what it always meant -- do not "fix" it by moving the number.
// The per-seed ceiling check stays as well, as a canary on the ten: a fixed
// seed crossing 0.06 is worth a look even under an accepted rate.
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
//
// RE-MEASURED 2026-08-06 (Task 7), and THE FLOOR SIDE IS THE WORSE FINDING OF
// THE TWO -- worse than the ceiling above, by an order of magnitude, and it
// predates this branch. Same masters 1..2000 calm-corner scan, 1 566
// non-Sampler terrains:
//
//                            at or below this floor    also below 1e-4
//   4ec5be0 (branch point)      193  (12.3 %)               53
//   89eb461 (Task 7 measurement point)              103  ( 6.6 %)               70
//
// So roughly one drawn terrain in fifteen currently renders FUNCTIONALLY MUTE
// at its calm corner, and one in eight did before this branch. That is a NEW
// press producing silence -- arguably a worse defect than one that is too
// loud -- and this gate reads green purely because none of the ten fixed
// candidate seeds happens to sit in that fraction. The quietest terrain in
// the HEAD scan is master 0x704 at rms 1.5e-10; at 4ec5be0 it was 0x2B7 at
// 9.9e-12.
//
// The branch nearly halved the rate rather than causing it, but it did move
// individual seeds through it, and that movement is measured: master 1028
// (0x404) rendered 1.60e-03 at 4ec5be0 and fell to 7.00e-08 at 3435c31.
//
// THE ONSET IS ISOLATED TO ONE SPAN, THE CURE ONLY TO A COMMIT, and the two
// claims are not equally strong -- read them as stated:
//
//  - ONSET, isolated WITHIN its commit by reversion (the three-way table in
//    kCalmCornerRmsMax's comment above carries 0x404's own rows): reverting
//    the drone SHAPE cap alone at 3435c31 puts 0x404 back at 1.35e-03, while
//    reverting the DIFF narrowing (6.93e-08) or the drone STEPS_B widening
//    (7.00e-08) leaves it mute. Same one span that retired the 0x707 breach.
//  - CURE, COMMIT-GRANULARITY ONLY: 0x404 stayed mute through 46cd3e8 and
//    reads 8.03e-05 at c945866, the commit that introduced the per-domain
//    adventure draw. That is a per-commit bisect, not a within-commit
//    isolation, and c945866 was followed by 019901c ("the nerve goes per
//    domain") correcting the same mechanism -- so "the adventure draw did it"
//    is the commit's headline, not a measured attribution to a single edit.
//
// It renders 1.16e-04 at HEAD: above this floor, but still about -78.7 dBFS.
// It is a survivor of the mute population, not a terrain that is comfortably
// audible.
//
// RULED BY THE OWNER 2026-08-07: THE MUTE FRACTION IS ACCEPTED, DELIBERATELY,
// and §7.8 now states it (kCalmMuteFracMax below). This closes the question,
// it does not answer it favourably -- roughly one drawn terrain in fifteen
// waking functionally mute at its calm corner is a KNOWN AND ACCEPTED
// PROPERTY of this generator, not a defect being tracked. Whether that is the
// right instrument is a listening judgement the owner has made; whether it
// stays true is what the gate now measures.
//
// TWO THINGS THAT DID NOT CHANGE WITH THE RULING. This constant is unchanged
// at 1e-5, and must still never be lowered to cover a mute terrain -- it is a
// silence detector, and a terrain that trips it is exactly what it is for.
// And the PER-SEED floor check is GONE, unlike the ceiling's: once the mute
// fraction is accepted, a fixed seed drifting into it is the accepted event
// happening, not news, and a red test for it would be noise. The rate is the
// whole claim on this side.
constexpr float kCalmCornerRmsMin = 1e-5f;          // -100 dBFS, silence floor

// Population for the calm-corner rate checks (spec §7.8 as ruled 2026-08-07).
// SAME SHAPE AS THE BLEND POPULATION BELOW, ONE DIFFERENCE: no engine-switch
// filter, because nothing is pressed here -- the calm corner is a static
// render, so the only terrain that has to be excluded is one with a Sampler
// deck, which this rig renders silent by construction (see the test file's
// header) and which would trip the floor for a reason that is not the flow
// layer's.
//
// WHY A STRIDE AND NOT THE WHOLE RANGE, stated plainly because it is the one
// compromise in this gate: the full population is 1 566 terrains and rendering
// it the way the gate measures (10 s each, first 3 s skipped) takes 115 s,
// which is more than the entire rest of the suite. The stride samples that
// same range evenly -- evenly rather than a 1..N prefix, so no locality in
// master space can bias it -- and the tolerated fractions below were set from
// the FULL 1 566-terrain measurement, not from the subsample. The subsample's
// own rate is asserted; the full population's rate is what the bound was
// chosen against, and the two are recorded together in the test.
constexpr uint32_t kCalmPopStride = 12u;            // every Nth master of
// 1..kBlendPopScanMax. 12 yields 137 terrains and ~10 s (measured), chosen as
// the coarsest stride that still leaves the mute check real teeth: it reads 8
// mute (5.84 %) against the full population's 103 (6.58 %), so a doubling is
// unmistakable. Median 1.51e-03 against the full population's 1.49e-03 -- the
// subsample tracks the whole on the quantity the median check asserts.
constexpr int kCalmPopMin = 90;                     // non-vacuity floor, same
// role as kBlendPopMin: a filter change that starved the set would otherwise
// make both rate checks trivially green.
constexpr float kCalmMuteFracMax = 0.10f;           // ACCEPTED mute fraction.
// THE NUMBER THE OWNER RULED. Full population measures 6.58 % (103/1566) at
// HEAD and 12.3 % at the branch point 4ec5be0, so 0.10 sits above today and
// below the rate the taste tables inherited -- it accepts what the generator
// does now and still goes red if the mute population drifts back toward where
// it came from. It is an acceptance and a regression bound at once; it is NOT
// a claim that 10 % would be fine musically.
constexpr float kCalmLoudFracMax = 0.05f;           // ACCEPTED loud fraction.
// Full population measures 0.51 % (8/1566) at HEAD, 1.34 % at 4ec5be0. The
// bound is deliberately loose relative to that, and the subsample shows why:
// on 137 terrains an 0.51 % rate is well under one expected breach, and the
// stride happens to catch 2 (1.46 %) -- nearly triple the population rate,
// purely from which terrains the stride lands on. A tight fraction here would
// be asserting on that accident. The SENSITIVE ceiling check on this side is
// the per-seed one, which stays; this rate exists to catch the ceiling
// becoming a COMMON event, which is the failure a per-seed canary cannot see.
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
// RULED BY THE OWNER 2026-08-07: THE SECOND OPTION. This ceiling is NOT a
// property the generator has (the distribution below), and it is not being
// made into one. §7.8 now states the fraction it tolerates, and the gate
// asserts a RATE OVER A POPULATION instead of all-of-four-fixed-seeds -- see
// kBlendSpikeBreachFracMax below. THIS CONSTANT IS UNCHANGED at 6 dB and
// keeps its original meaning: it is the spec's claim about what a crossfade
// may do, and it is what the median terrain is still held to.
//
// The red that prompted the ruling (Task 7, the taste tables): two of the
// four asserted seeds breached, 0xD0D at +6.49 dB and 0xC0C0 at +6.36 dB,
// both in window 3 (0.75-1.00 s). Pre-branch the worst was +3.02 dB. The
// bound was NOT raised to cover them, and they were NOT dropped from
// kCandidateMasters -- both of those would have been fitting the gate to the
// failure. What changed is what the gate claims, not what it tolerates
// per seed.
//
// RULED ON A DISTRIBUTION, NOT ON A SEED, because every earlier report of
// this gate quoted one seed's before/after and the gate kept changing
// character (a -11.68 dB drop, then a +12.68 dB spike, now a +6.5 dB spike).
// The differential press-vs-control comparison was run over masters 1..2000,
// keeping the 85 that are non-Sampler AND whose new_full() switches no
// engine -- exactly the population this gate asserts on -- and the per-seed
// worst in-gate spike distributes like this:
//
//                       min    p50    p90    p95    p99    max   over 6 dB
//   4ec5be0            -6.71  +2.91 +18.73 +33.74 +58.86 +59.53  31/85 (36.5%)
//   89eb461 (Task 7 measurement point)     -5.90  +2.18 +23.29 +34.98 +58.62 +59.40  24/85 (28.2%)
//
// SO THE 6 dB CEILING IS NOT A PROPERTY THE GENERATOR HAS, AND NEVER WAS.
// More than a quarter of eligible terrains breach it, before and after this
// branch -- the branch made it slightly BETTER (36.5 % -> 28.2 %), not worse.
// The gate read green until now purely because none of the four asserted
// fixed seeds sat in the breaching third; the taste tables moved two of them
// into it, which is a sampling change, not a regression in the blend.
//
// RE-MEASURED AT HEAD 2026-08-07, before the ruling was implemented, so the
// number encoded below is HEAD's and not a figure inherited from four commits
// back: n=85, min -5.90, p50 +2.18, p75 +7.32, p90 +23.29, p95 +34.98,
// max +59.40, 24/85 (28.2 %) over 6 dB. Identical to the 89eb461 row above --
// the four commits since did not move this metric at all.
//
// WHY THE OTHER THREE EXITS WERE REFUSED, recorded so nobody re-proposes one:
// raising this to cover 6.49 would be retuning a SPEC number (§5) to fit the
// generator; raising it to cover the population would mean roughly +60 dB,
// which asserts nothing; dropping 0xD0D/0xC0C0 from kCandidateMasters would be
// carving around the failure, and they are not special, they are two of ~28 %.
// The ruling took the remaining exit, the one this comment has asked for since
// Task 7 -- and it is the same exit kCalmCornerRmsMax still needs.
//
// (Still true from the mode work, and re-confirmed: two asserted seeds change
// mode on their press, so the carrier's clocking-flip duck path inside the
// window is covered non-vacuously.)
constexpr float kBlendDropDb = 10.f;                // press vs. control,
// drop floor: symmetric case -- more than this many dB QUIETER than the
// control is the deck audibly leaving the mix, which a crossfade must not
// do either. Not the same magnitude as the spike ceiling: a NEW blend
// legitimately ducks the outgoing deck's dry leg by design (kDuckDepth),
// so some asymmetric headroom on the downside is expected even in a
// healthy blend.
//
// RE-MEASURED 2026-08-06 (Task 7), same run as kBlendSpikeDb above. STILL
// GREEN, BUT THE HEADROOM IS NEARLY GONE: worst in-gate drop is now -9.20 dB
// (master 0x404, window 3) against this 10 dB floor, i.e. 0.80 dB of room,
// where the mode work left 4.06 dB (-5.94 dB, master 0x808 window 0). Bound
// holds unchanged and is NOT widened -- but it is now the closest of the four
// audio constants to falling over, and the same 85-seed scan puts 6 of 85
// (7.1 %) of eligible terrains past it (5 of 85 at 4ec5be0). Read that
// alongside kBlendSpikeDb's finding: the drop side is heading the same way,
// just more slowly.
//
// SO IT MOVES TO THE SAME DISTRIBUTION RULING (owner, 2026-08-07), even though
// it is not the side that went red. Leaving the drop side as an all-of-four-
// fixed-seeds check while the spike side asserts a rate would mean the two
// halves of one gate claim different things about the same population -- and
// with 0.80 dB of headroom the per-seed form was going to fail on the next
// taste-table change anyway, for exactly the sampling reason the spike side
// just failed for. Re-measured at HEAD 2026-08-07: p50 2.06 dB, p90 7.57 dB,
// max 14.44 dB, 6/85 (7.1 %) past the floor. Bound UNCHANGED at 10 dB.

// Population for the two rate checks below (spec §7.8 as ruled 2026-08-07).
// The population is DEFINED, not enumerated: every master in 1..kBlendPopScanMax
// whose terrain rolls no Sampler deck and whose new_full() switches no engine --
// the exact set the level comparison is meaningful on. It is computed at test
// time, so it cannot be quietly trimmed to dodge a failure the way a hardcoded
// seed list can; kBlendPopMin asserts it did not collapse.
constexpr uint32_t kBlendPopScanMax = 2000u;        // scan 1..this for the
// population. 2000 is the range every distribution figure in this file was
// measured over, kept identical so the encoded rates and the gate's own
// measurement describe the same set. It yields 85 eligible masters at HEAD.
constexpr int kBlendPopMin = 60;                    // non-vacuity floor on the
// population size: a filter change that starved the set would otherwise make
// both rate checks trivially green (0 of 3 breaching passes any fraction).
// Set well below today's 85 so ordinary generator drift does not trip it, and
// well above the handful that would make a rate meaningless.
constexpr float kBlendSpikeBreachFracMax = 0.33f;   // tolerated fraction of the
// population allowed past kBlendSpikeDb. THIS IS THE NUMBER THE OWNER RULED,
// and it is a REGRESSION bound, not a quality target: HEAD sits at 28.2 %
// (24/85) and the branch point 4ec5be0 sat at 36.5 %, so 0.33 admits four more
// breaching terrains than today and still goes red before the blend drifts back
// to where the taste tables found it. It does NOT say 28 % is acceptable
// musically -- kBlendSpikeDb still says what a crossfade should do, and the
// median check enforces it on the typical terrain. This bound only says the
// tail must not grow.
constexpr float kBlendDropBreachFracMax = 0.12f;    // same, for kBlendDropDb.
// HEAD sits at 7.1 % (6/85), 4ec5be0 at 5.9 % (5/85); 0.12 admits four more,
// the same four-terrain margin the spike side gets, chosen for that symmetry
// rather than measured separately.
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
// gate's scope window by window against the generator at that time, the worst
// spike/drop over the no-switch seeds read 1.00 s: +3.02/-5.94 (green),
// 1.25 s: +4.16/-7.00 (green), 1.50 s: +13.34/-10.03 (RED). It went red on
// the window that CONTAINS kCarrierStaggerFrac * kBlendS = 1.5 s -- the
// carrier deck's own engine/scale switch, which is where the excursion
// lives. So 1.0 s sat 0.50 s inside the boundary, not 0.75 s.
//
// RE-MEASURED AGAIN 2026-08-06 (Task 7, the taste tables): THE BOUNDARY HAS
// REACHED THE WINDOW. Same scope sweep against the current generator reads
// 0.75 s: +0.62/-5.78 (green), 1.00 s: +6.49/-9.20 (RED), 1.25 s:
// +7.39/-10.30 (RED). The gate is now red AT its own window rather than
// inside a margin -- and it goes red on the spike, in window 3, not on the
// carrier's 1.5 s stagger duck that used to be the boundary. See
// kBlendSpikeDb: the 6 dB ceiling is not a property the generator holds, so
// this is not a margin that can be recovered by moving this window. THE
// WINDOW IS STILL NOT MOVED -- narrowing it to 0.75 s would make the gate
// green by shrinking what it claims, which is fitting, and is exactly the
// move this constant's own text has refused twice already.
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
//
// RE-MEASURED 2026-08-06 (Task 7, the taste tables), by the same measurement,
// with a PER-SEED before/after this time: the same 10 s / macros-0.5 render
// was run at every commit of this branch out of a worktree seeded at the
// branch point 4ec5be0. Band is now 0.0306..0.1211. BOTH CONSTANTS HOLD
// UNCHANGED, margins 29.7 dB below the quietest terrain and 12.3 dB above
// the loudest (were 25.2 dB and 13.9 dB).
//
// THE OWNER'S +6 dB, WHAT ACTUALLY REACHES HIM. Mean per-seed change across
// the whole branch is +2.58 dB, range -0.45 to +6.21 dB -- NOT the +4.78 dB
// the COMP band work reported. Both numbers are right and they measure
// different things: the COMP report's baseline was the commit COMP landed on
// (3e7944f), which was already 3.67 dB BELOW the branch point. COMP itself is
// worth +4.77 dB (+6.38 at the 0.70 round, -1.61 pulling back to 0.60); three
// earlier table commits had spent most of that before it arrived. Mean
// per-commit, in dB against the previous commit:
//
//   9f6daf6 veto table + four redrawn curves     -2.12
//   3435c31 base rule edits                      +0.16
//   50ad085 DENSITY archetype window             -0.11
//   4624822 musical weights (rungs/steps/skew)   -1.60
//   d1a9416 COMP 0.10-0.50 -> 0.40-0.70          +6.38
//   46cd3e8 COMP ceiling -> 0.60 (by ear)        -1.61
//   c945866 the per-domain adventure draw        +1.46
//   019901c adventure per domain, corrected      +0.01
//                                          NET   +2.58
//
// THE SPREAD, and what could and could not be established about it. The four
// seeds that gain least across the branch are 0x303 (-0.45 dB), 0x606
// (+1.10), 0x101 (+1.57) and 0x505 (+1.85). They do not share an archetype
// (drone, arp, drone, fragment). Two mechanisms were measured:
//
//  - COMP's own lift is smallest on the seed that was already loudest. Across
//    the ten, correlation between a seed's pre-COMP level in dBFS and the dB
//    it gained from the COMP move is -0.66; 0x101, 6 dB louder than any other
//    seed at -19.6 dBFS, gained only +1.50 dB where the other nine gained
//    +3.55 to +5.78. That is a correlation over ten points. THE COMPRESSOR
//    MECHANISM BEHIND IT WAS NOT MEASURED and is deliberately not named here.
//  - 0x303, 0x606 and 0x505 are not COMP cases at all -- their COMP gains
//    (+5.39/+5.13/+3.55) sit near the mean. What they lost was the musical
//    weights commit 4624822: -1.80, -1.62 and -1.19 dB, against its own
//    -1.60 dB mean but a per-seed range of -0.06 to -9.56 dB. That commit
//    changes WHICH rung and step count a terrain draws, so a seed's level
//    after it is a different terrain's level, not the same terrain turned
//    down. No further mechanism is claimed.
//
// WHAT REMAINS OPEN: about 3.4 dB of the owner's reported +6 dB is still
// unaccounted for on the average seed, and the per-seed range is wide enough
// that some terrains gained essentially nothing. Do NOT chase it by raising
// the COMP ceiling back to 0.70 -- the owner heard 0.70 and ruled it
// over-compressed (see kVetos). It is a by-ear question about where else in
// the tables the level should come from.
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
//
// RE-MEASURED 2026-08-06 (Task 7), because the weighted discretes (P_STEPS_B
// and the rate rungs) and P_MODE can now change independently of each other.
// They do not churn. Same 60 s static-macro window at 500 Hz: worst storied
// churn 1 (P_STEPS_A), worst non-storied 0, P_MODE 0, on all ten candidates.
// Widened past the ten for the first time -- masters 1..600, 477 non-Sampler
// terrains -- the per-terrain maximum storied churn is 0 on 308 and 1 on 169,
// and NEVER 2 or more. So the bound has never been reached at any sample size
// measured; it holds with a full change of headroom and stays non-vacuous
// only through its RED proof (see the gate in tests/test_flow_audio.cpp), not
// through observed churn.
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
    // RE-DRAWN 2026-08-06 (owner request): the owner always plays per-deck
    // COMP at >= 0.5 by ear, and Glow's old 0.10-0.50 band never let it draw
    // that high -- COMP carries the makeup gain, and a table that undershoots
    // where the owner actually sets the knob is why Glow read quiet. First
    // moved to 0.40-0.70 (same width discipline as before: never
    // uncompressed, never squashed), then the owner LISTENED to 0.70 and
    // ruled it back down to 0.60 ("Ja passt eher 0.6"): 0.70 read as
    // over-compressed by ear. That is a by-ear ceiling, not a correction of
    // the band's location -- 0.40 lo and the "COMP carries makeup gain"
    // rationale both still hold. Do NOT "restore" 0.70 to claw back level;
    // the level gap left at 0.60 has to be found somewhere else in the
    // table, not by re-widening this band past where the owner capped it.
    { P_COMP_A,   0.40f, 0.60f },
    { P_COMP_B,   0.40f, 0.60f },
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
// Musical weights (spec 2026-08-06 §6). These are WEIGHTS, not vetoes: the
// unlikely values stay reachable and simply come up rarely. Spec §7's
// adventure draw flattens them per domain: terrain.cpp raises every weight
// below to the power (1 - a^kAdventureExp), so these numbers are the shape at
// adventure 0 and the table reads uniform at adventure 1. With kAdventureExp
// at 2 the mean terrain (a = 0.25) sits at w^0.94 and the median one
// (a = 0.21) at w^0.96, so these numbers are essentially what a terrain
// actually plays -- exponent 1 put the typical draw at w^0.75 and broke §6's
// own "straight rungs win four draws out of five". See kAdventureExp above.
//
// Rung preference on kDivisions (mod/divisions.h), which is speed-sorted, so
// the dotted and triplet rungs sit between the straight ones. Straight rungs
// weigh 1, dotted 0.20, triplet 0.15.
inline constexpr float kRateRungW[kDivisionCount] = {
//  8bar 4bar 2bar 1bar  1/2.  1/2  1/4.  1/2T  1/4  1/8.  1/4T  1/8
    1.0f,1.0f,1.0f,1.0f, .20f,1.0f, .20f, .15f,1.0f, .20f, .15f,1.0f,
//  1/16. 1/8T 1/16  1/16T 1/32
     .20f,.15f,1.0f,  .15f, 1.0f,
};
// Step counts 2..16 (index = count - 2). 8 and 16 are the counts actually
// played; 4 and 12 are usable; the rest exist for the rare terrain.
//
// A SECOND rule is encoded in the same row and would otherwise go unnamed:
// EVEN counts beat odd ones across the board. Every odd count weighs .05,
// while the even leftovers 2/6/10/14 weigh .15/.20/.15/.10 -- a 2-4x
// preference. A phrase whose length does not halve reads as a mistake against
// everything else on the clock, so odd counts stay reachable but rare.
inline constexpr int kStepsWCount = 15;             // step counts 2..16
inline constexpr float kStepsW[kStepsWCount] = {
//  2    3    4    5    6    7    8    9   10   11   12   13   14   15   16
   .15f,.05f,.50f,.05f,.20f,.05f,1.0f,.05f,.15f,.05f,.50f,.05f,.10f,.05f,1.0f,
};
// SHUFFLE has no rungs to weight, so its bias is a skew inside the drawn span:
// v = lo + (hi-lo) * u^kShuffleSkew. Above 1 pulls toward the low end; a heavy
// -shuffle fragment stays reachable, which a narrowed span would have killed.
inline constexpr float kShuffleSkew = 2.5f;

// The adventure draw (spec 2026-08-06 §7). Not a control -- a property of the
// DRAW, so NEW occasionally surprises and the panel gains no knob. At a=0 a
// span is sampled only in its middle kAdventureNarrow; at a=1 in full, which
// is the no-op. The (1-x)^3 shape of the draw itself lives in terrain.cpp, as
// does the per-domain split of WHICH a applies where (Terrain::adventure).
constexpr float kAdventureNarrow = 0.40f;
// How hard the adventure level flattens the musical weights and the SHUFFLE
// skew: both are raised to the power (1 - a^kAdventureExp). MOVED HERE
// 2026-08-06 (Task 7) from terrain.cpp, where it was an `adv * adv` literal --
// it is a tuning value, and this file is where those live. It is also a LEVER
// the owner has already been asked to rule on once, so it needs a name.
//
// 2 rather than 1 is the owner's ruling (2026-08-06). E[a] is 0.25, so a plain
// w^(1-a) puts the TYPICAL terrain at w^0.75, which lifts a 0.15 triplet weight
// to 0.24; squaring puts the MEAN terrain (a = 0.25) at w^0.94 and the MEDIAN
// terrain (a = 1 - 0.5^(1/3) = 0.21) at w^0.96 -- essentially the tables as
// written -- and lets the flattening bite only on the rare brave draw (a=0.5
// gives w^0.75, a=0.9 gives w^0.19). "Chaos when NEW is pressed, aber eben
// seltener." (Mean and median are not the same terrain here: a = 1 - u^(1/3)
// is skewed. An earlier version of this line called a = 0.25 the median and
// attached the median's w^0.96 to it; corrected 2026-08-06, review round 1.)
//
// THE MEASURED ALTERNATIVES, on the crooked synced-rate share over 4 000
// masters (tests/test_flow_terrain.cpp's own quantity, aggregated over all
// adventure levels), so a later session does not have to re-derive them:
//
//   no tempering at all  0.1891      exponent 2 (shipped)  0.2134
//   exponent 1           0.2549      exponent 3            0.2032
//                                    exponent 4            0.1961
//
// Higher exponents temper less. Anything above ~2 buys crooked-rung share back
// at the cost of the feature the draw exists for -- the brave/calm separation
// that tests/test_flow_terrain.cpp now asserts on the two populations
// separately. Do not raise it to make an aggregate bound fit; that bound was
// retired for measuring a mixture.
constexpr float kAdventureExp = 2.f;

// The adventure draw's own SHAPE (spec §7: "a = 1 - u^(1/3) ... the (1-x)^3
// shape is a first guess, tunable later"). MOVED HERE 2026-08-06 (final
// review) from terrain.cpp, where it was a bare `1.f / 3.f` literal at both
// draw sites (t.adventure_base and t.adventure[m]) -- the same argument
// Task 7 used to move kAdventureExp off an `adv * adv` literal applies here:
// it is a tuning lever the spec itself calls tunable, so it belongs in this
// file, not duplicated in terrain.cpp. a = 1 - u^(1/kAdventureShape) gives
// P(a > x) = (1 - x)^kAdventureShape. Value unchanged at 3 -- this is a
// relocation, not a retune; terrain.cpp's draw_adventure() is now the one
// place that reads it.
constexpr float kAdventureShape = 3.f;

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
// DENSITY rate-led (§3 row 2a): events carry the sweep. Drone reads only the
// sparse part of it -- a drone at full DENSITY lands where an arp sits at
// half, and STEPS_A comes down with it because it lives in the same story.
{ M_DENSITY, "rate", 3, {
  { P_DENSITY_A, {{.02f,.08f},{.1f,.2f},{.3f,.5f},{.5f,.7f},{.7f,.95f}} },
  { P_DENSITY_B, {{.02f,.08f},{.08f,.18f},{.25f,.45f},{.45f,.65f},{.65f,.9f}} },
  { P_STEPS_A,   {{2.f,4.f},{4.f,6.f},{6.f,10.f},{10.f,13.f},{13.f,16.f}} } },
  {{0.f,.45f},{0.f,1.f},{0.f,1.f},{0.f,1.f}} },
// DENSITY thickness-led (§3 row 2b): chords/pad carry it.
{ M_DENSITY, "thick", 3, {
  { P_COLOR_A,   {{0.f,.1f},{.15f,.3f},{.35f,.55f},{.55f,.75f},{.75f,1.f}} },
  { P_COLOR_B,   {{0.f,.1f},{.1f,.25f},{.3f,.5f},{.5f,.7f},{.7f,.95f}} },
  { P_SUB_A,     {{.1f,.2f},{.2f,.35f},{.35f,.5f},{.5f,.65f},{.6f,.8f}} } } },
// BRIGHT "ember -> sweep -> open -> air" (§3 row 1). Q1 dips the dry leg
// via REVMIX (the spec-named level mechanism) and blooms REV_DECAY --
// REV_DECAY's curve here runs HIGH at bp0 and settles by bp1: monotone
// falling, active only in Q1. REVMIX likewise falls Q1-only -- settling at
// bp1-bp4 well above dry silence, never toward the veto floor itself (kVetos:
// P_REVMIX_A/B >= 0.08) but bounded by the same principle -- the reverb send
// is never allowed fully dry.
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
  // COMP rescaled into 0.40-0.60 (2026-08-06, owner heard 0.70 and ruled it
  // over-compressed -- "Ja passt eher 0.6"), relative shape kept: each
  // breakpoint's position inside the 0.40-0.70 band maps linearly to the
  // same position inside the narrower 0.40-0.60 band.
  { P_COMP_A,    {{.47f,.54f},{.47f,.54f},{.49f,.56f},{.51f,.58f},{.53f,.60f}} },
  // PUSH joins in Q4 only (the threshold rule), inside the veto band. bp4 hi
  // lands exactly on the veto ceiling (0.40) on purpose: the loudest quarter
  // sits right at the limit, not a rounding accident.
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
// Drones normally have STEP off entirely (kModeW), but a drone that does draw
// the step mode gets the same preferred counts as everything else, so the
// 8/16 weight has something to bite on.
{ P_STEPS_B,  {{2.f,16.f},{4.f,10.f},{8.f,16.f},{4.f,12.f}} }, // arp = many
{ P_RATE_A,   {{0.f,.25f},{.3f,.6f},{.55f,.9f},{.3f,.7f}} },   // drone = slow
{ P_RATE_B,   {{0.f,.25f},{.3f,.6f},{.55f,.9f},{.3f,.7f}} },   // drone = slow
// -- pitch ----------------------------------------------------------------
{ P_TUNE_A,   {{.25f,.75f},{.25f,.75f},{.25f,.75f},{.25f,.75f}} }, // neutral
{ P_TUNE_B,   {{.25f,.75f},{.25f,.75f},{.25f,.75f},{.25f,.75f}} }, // neutral
{ P_RANGE_A,  {{.1f,.4f},{.2f,.5f},{.4f,.8f},{.3f,.7f}} },     // arp = wide
{ P_RANGE_B,  {{.1f,.4f},{.2f,.5f},{.4f,.8f},{.3f,.7f}} },     // arp = wide
// -- timbre wildcards -----------------------------------------------------
// SHAPE morphs sine(0) -> tri(.25) -> ramp(.5) -> pulse(.75) -> S&H(1)
// (mod/waveforms.h). A drone gets the round quarter only: from the ramp up the
// lane emits a per-cycle discontinuity and the drone reads as rhythmic. The
// other archetypes keep the full wildcard.
{ P_SHAPE_A,  {{0.f,.25f},{0.f,1.f},{0.f,1.f},{0.f,1.f}} },
{ P_SHAPE_B,  {{0.f,.25f},{0.f,1.f},{0.f,1.f},{0.f,1.f}} },
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
// Rescaled into 0.40-0.60 (2026-08-06, owner ruled 0.70 over-compressed),
// same linear map as DIRT "heat"'s P_COMP_A above (0.40-0.70 position ->
// same position in the narrower 0.40-0.60 band): old {.55f,.70f} -> {.50f,.60f}.
{ P_COMP_B,   {{.50f,.60f},{.50f,.60f},{.50f,.60f},{.50f,.60f}} },     // gentle glue
{ P_LINK_A,   {{0.f,.6f},{0.f,.6f},{0.f,.6f},{0.f,.6f}} },     // unipolar 0..1
{ P_LINK_B,   {{0.f,.6f},{0.f,.6f},{0.f,.6f},{0.f,.6f}} },     // unipolar 0..1
// -- global modulation / mix ---------------------------------------------
{ P_MORPH,    {{.2f,.8f},{.2f,.8f},{.2f,.8f},{.2f,.8f}} },     // neutral
{ P_COUPLE,   {{0.f,.5f},{0.f,.5f},{0.f,.5f},{0.f,.5f}} },     // neutral
{ P_CHOKE,    {{-.25f,.25f},{-.25f,.25f},{-.25f,.25f},{-.25f,.25f}} }, // near center (by-ear states)
{ P_SHUFFLE,  {{0.f,.1f},{0.f,.35f},{0.f,.3f},{.1f,.5f}} },    // fragment = loose
// -- reverb character (DIFF = density, per the reverb mod split) ----------
// DIFF: 0.4-0.6 is simply not wanted, so this is a span narrowing rather than
// a weight -- the value is meant to be unreachable.
{ P_REV_DIFF, {{.6f,.8f},{.6f,.8f},{.6f,.8f},{.6f,.8f}} },
// -- clock ----------------------------------------------------------------
{ P_TEMPO_BPM, {{55.f,75.f},{80.f,110.f},{90.f,130.f},{70.f,110.f}} }, // drone = slow
};
inline const int kBaseRuleCount = int(sizeof(kBaseRules) / sizeof(kBaseRules[0]));

} } // namespace spky::flow
