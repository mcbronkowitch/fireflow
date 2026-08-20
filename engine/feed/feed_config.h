#pragma once

namespace spky {
namespace feed_cfg {

// --- PHASE UNITS ----------------------------------------------------------
//
// fast_sin(p) == sin(2*pi*p) (engine/util/fast_sin.h), so EVERY quantity in
// this engine that is added to a phase is in CYCLES, not radians. One cycle is
// the 2*pi of the classical FM literature: an "index of 6" from a DX7 table is
// kIndexMaxCycles ~= 0.95 here. The three constants this applies to are
// kIndexMaxCycles, kFbBaseCycles and everything the DAMP one-pole carries,
// because it filters a cycles-valued signal.

// --- P, the one number here that is not a taste decision ------------------
//
// P is MEASURED, on a Daisy Patch Submodule over USB at -O2, AXI layout, three
// sweep points at kPairs 2 / 4 / 8. `-O3` did not enter it and cannot: the
// `system` family overflows SRAM_EXEC by 2844 B there, and did so before FEED
// existed. No Seed figure entered it either.
//
// **18 655 cycles per pair**, from a least-squares fit over the three
// `feed_pairs` points (39 110 / 77 176 / 151 190 average cycles), with about
// 2 100 cycles of fixed overhead -- so the row is very nearly pure per-pair
// cost. The two adjacent slopes are 19 033 and 18 504, a ratio of 0.972, which
// is what makes the fit worth quoting at all. The whole-engine row agrees
// independently: `inst_feed_engine_worst` fits 18 708 cycles per pair per deck.
//
// P follows from the 960 000-cycle block budget as measured on
// `inst_feed_engine_worst` -- BOTH decks on FEED, worst-case knobs -- whose
// maximum fits 39 904*P + 634 516. Measured: P=2 74.0 %, P=4 83.3 %, P=8
// 99.2 %. A ~9 % reserve puts the ceiling at 6.95 pairs, and rounding DOWN to
// a multiple of kPairsPerTone gives 6.
//
// The reserve is not a formality. The worst-case row runs with **FLUX off**
// (as the BBD row does), so none of those percentages include stereo tape,
// which is one switch away and prices at 10.3 % on its own. P=8 measured 99.2 %
// and would have had no room for it at all.
//
// CONFIRMED by its own run rather than left on the interpolation, because 6 is
// not one of the sweep points: `inst_feed_engine_worst` measures **92.51 %**
// against the fit's 91.04 %, so the linear fit runs about 1.6 % optimistic
// between its points. The reserve that actually survives is therefore **7.5 %**,
// not the 9 % the derivation aimed at -- and the number to quote is 92.51.
// FEED still sits far under the same image's `instrument_worst` (102.76 %),
// which is Task 11's gate.
//
// P=6 also decides how much of a chord is heard: the bank voices
// kPairs/kPairsPerTone tones, capped at ChordBuilder::kMaxNotes = 4. So 6 pairs
// sound 3 of the 4 tones COLOR reaches at its top -- a complete triad, with
// only the fourth note dropped at the very end of the knob. P=4 would have
// sounded two.
constexpr int kPairs = 6;

// Set 2026-08-19 by the sweep cited above.
// docs/bench/2026-08-19-f836a32-feed-axi-o2-patch_sm-usb.md   (P=2)
// docs/bench/2026-08-19-500775c-feed-axi-o2-patch_sm-usb.md   (P=4)
// docs/bench/2026-08-19-ab0a6bf-feed-axi-o2-patch_sm-usb.md   (P=8)
// The gate "feed G8" in tests/test_feed_engine.cpp failed while this was
// false, so an undecided P could not reach main.
constexpr bool kPDecided = true;

// --- structure ------------------------------------------------------------

// The control-rate raster. Must equal SynthEngine::kCtrlInterval; the
// static_assert lives in feed_engine.cpp, where both headers are visible.
constexpr int kCtrlInterval = 96;

// How many chord tones the bank voices. SPREAD detunes the pairs sharing one
// tone against each other, so a tone holding a single pair has nothing to beat
// against and SPREAD would go dead at exactly the chord size COLOR reaches
// (plan open point 4). Every voiced tone keeps a group of at least two.
constexpr int kPairsPerTone = 2;
static_assert(kPairs >= kPairsPerTone,
              "a bank must hold at least one full tone group");

// --- measured, not by ear (the regime map) --------------------------------
//
// Read off docs/engine-map.md section 9. Spec section 10 is explicit that
// these two are the map's and not the ear's: the BOND position where the
// estimated fundamental leaves the tolerance, and the tolerance itself. Do not
// retune them in a listening session -- re-run the probe.
//
// 0.7 was the last such BOND position while kFbBaseCycles was 0.08: all 80
// measured rows (4 spreads x 5 depths x 4 ratios, spread <= kSpreadKneeCt)
// still had a fundamental AND sat inside a cent of the played pitch, and at
// 0.8 the worst row jumped from +0.609 ct to +22.522 ct.
//
// RE-MEASURED 2026-08-19 after the drive went to 0.14 by ear: 0.65. This
// number tracks the drive and has to be re-run whenever kFbBaseCycles moves --
// that is what makes it a measurement rather than a setting. Nothing in the
// engine reads it; it is the DESCRIPTION the gate samples below.
//
// What the re-run actually found is worth having in front of you before
// anyone treats 0.65 as a stability limit. At pitch 0.35 the ring does not
// break anywhere on the knob any more than it did at 0.08 -- across the whole
// BOND travel and SPREAD's lower half the aperiodicity peaks at 0.0171,
// nowhere near the 0.30 that would mean "no fundamental". The drift is a
// smooth monotone creep to 3.007 ct at BOND 1.0, and it crosses
// kPitchCentreTolCt at 0.70 rather than falling off anything. So the honest
// reading is "the 2 ct bound holds to 0.65, and a 3.1 ct bound holds
// everywhere" -- the tolerance was deliberately NOT widened to keep the whole
// travel inside the gate, because loosening a bound so a test passes is how a
// gate stops being one. The cliff proper still lives at the bottom of the
// pitch range, where the attenuation is weakest; see docs/engine-map.md
// section 9.
constexpr float kBondPitchThreshold = 0.65f;

// 2.0 ct: 3.3x the worst reading below the threshold (0.609 ct) and 11x below
// the first broken row (+22.5 ct), so it discriminates with a wide margin in
// both directions. Against spec section 2.6's own calibration -- SWARM's
// withdrawn +420 ct earned "HARM almost always sounds detuned", +4.5 ct was
// accepted -- this sits comfortably inside what was accepted by ear.
constexpr float kPitchCentreTolCt = 2.0f;

// --- by ear, first try (spec section 10) ----------------------------------
// Every constant below this line is Bastian's to confirm in Task 13. No gate
// asserts any of these literals; gates derive from the names.

// The FM index at DEPTH 1 and envelope 1, in CYCLES (see PHASE UNITS above).
constexpr float kIndexMaxCycles = 0.95f;   // BY EAR, first try

// The feedback / neighbour input amount before the pitch attenuation, in
// CYCLES. This is the term spec section 3.2.2's attenuation multiplies.
//
// MEASURED DOWN from a first-try 0.30. This is still a by-ear constant and
// the listening pass may move it, but not upward past the ceiling the map found:
// at 0.30 cycles (1.885 rad) the pitch centre is already gone at BOND 0, on a
// single pair, with no coupling and no spread -- +50 ct, because 1.885 rad of
// self-feedback is far past the classical beta = 1 rad point where a
// feedback-FM operator stops being periodic at its carrier. The two-sample
// average lifts that limit but does not remove it. At 0.08 cycles (0.503 rad)
// the whole BOND travel below the threshold holds within 0.61 ct, and the
// cliff still arrives on schedule at BOND 0.8. See docs/engine-map.md
// section 9, "Where the coupling tips", for the sweep this came off.
//
// BY EAR, 2026-08-19 (Bastian, the E variant of the six-render BOND A/B). 0.14
// is deliberately ABOVE the 0.08 ceiling the paragraph above measured, so the
// deck now runs past the bank's own tipping point over the lower part of the
// pitch range -- which is the point. The brief was "ruhig hart, filtern kann
// ich selbst dahinter": the escalation is what the engine is for, and taming
// its top end is now the output FILT's job, which became a real low-pass on
// the same day. The alternative on the table was darkening the in-loop path
// (kDampFixedHz 1200 or 500 Hz, variants B/C/F) and it was rejected -- that
// removes the brightness for everyone instead of leaving it under a knob.
//
// So: the ceiling above is still the measurement it always was, and this
// value knowingly sits over it. Do not "correct" one to the other.
constexpr float kFbBaseCycles = 0.14f;     // BY EAR 2026-08-19; see above

// Pitch attenuation (spec section 3.2.2, the Braids RenderFeedbackFm recipe).
// The pair's NORMALIZED pitch is already logarithmic -- pitch_to_hz(p) is
// 110 * 8^p (synth_engine.cpp) -- so a straight line in p is an exponential
// fall in Hz, which is the shape Braids derives from a pitch offset, at zero
// cost and with no libm call on the control path.
//   atten(p) = clamp(1 - kFbPitchSlope * p, kFbAttenMin, 1)
constexpr float kFbPitchSlope = 0.75f;     // BY EAR, first try
constexpr float kFbAttenMin   = 0.18f;     // BY EAR, first try

// NEW's per-pair feedback offsets (spec section 3.4): the cliff becomes a
// gradient the ear can ride instead of an edge. Multiplicative, symmetric.
constexpr float kFbOffsetRange = 0.12f;    // BY EAR, first try

// SPREAD, in cents, at the two ends of the knob. The lower half stays in
// single digits by spec section 3.4; kSpreadKneeCt is the value at knob 0.5
// and kSpreadMaxCt the value at knob 1. The exact numbers are Task 3's regime
// probe to confirm ("audibly beating but not yet detuned").
constexpr float kSpreadKneeCt = 7.f;       // BY EAR, first try
constexpr float kSpreadMaxCt  = 45.f;      // BY EAR, first try

// RATIO (spec section 4). The lower half runs 1:1..kRatioMagnetTop through a
// monotone warp that flattens near the integers; the upper half runs
// continuously from there into the irrational.
constexpr float kRatioMagnetTop = 4.f;
constexpr float kRatioMagnetExp = 3.f;     // BY EAR, first try (>1 = flatter)
constexpr float kRatioMax       = 11.f;    // BY EAR, first try

// The one-pole inside the feedback path. It is a fixed corner, not a free
// control: it is half of this engine's anti-aliasing (the other half is the
// two-sample average in FeedPair), and there is no knob left that reaches it
// -- turning it into player travel would mean turning part of the aliasing
// guard off. It held FILT until 2026-08-19 and FILT could not be heard,
// which is the measured reason it stopped being that knob:
//
//   - it never touches the carrier. FM brightness is set by the index and the
//     ratio; damping the feedback perturbation changes how the coupling
//     wanders, not where the spectrum ends.
//   - the signal it filters is tiny. kFbBaseCycles is 0.08 cycles at the
//     bottom of the pitch range and 0.019 at the top (engine-map section 9),
//     so the one-pole is shaping a small phase wobble.
//   - its travel saturated. At k = 1 - exp(-2*pi*fc/sr) the old centre
//     detent already sat at 0.34, +0.5 of travel at 0.72 and the top at 0.98
//     -- the upper third of the knob was indistinguishable from wide open.
//
// The value is the old centre detent, so the ring's own character is exactly
// what it was at the neutral knob position. It briefly grew a bounded EDGE
// trim around this value; that trim was withdrawn 2026-08-20 (see
// docs/by-ear-decisions.md, "EDGE"), and 3200 Hz is now a fixed corner with
// no panel reach at all.
constexpr float kDampFixedHz = 3200.f;     // BY EAR, first try

// FILT: a real low-pass on the deck's OUTPUT (SvfLp, the same filter Synth,
// WAVE and the sampler run), on the same 60 Hz..14 kHz rails.
//
// Deliberately restated here rather than shared through a header: every one of
// these is a tuning literal and this file is where FEED's tuning literals
// live. If a listening pass moves them, it moves them HERE and FEED's filter
// stops matching the other decks on purpose, not by drift.
constexpr float kCutoffMinHz   = 60.f;     // same rails as the synth FILTER
constexpr float kCutoffMaxHz   = 14000.f;
constexpr float kFiltNeutral   = 0.75f;    // centre detent -> about 3.6 kHz
constexpr float kFiltLeftScale = 1.25f;    // left travel reaches past 0...
constexpr float kFiltFadeRange = 0.25f;    // ...into a fade to silence
// The right half is scaled to land exactly on kCutoffMaxHz at FILT +1, which
// is where FEED departs from the sampler's otherwise identical arithmetic.
// On a sampler deck FILT is a TRIM added to a lane that already carries the
// cutoff, so an upper travel that saturates early costs nothing -- the lane
// reaches the rest. FEED has no such lane (LANE_SIZE is SPREAD here), so FILT
// is the whole control, and with the sampler's unscaled +1 the knob clamped at
// n_raw = 1 by FILT 0.25: measured, the top 70 % of the right half returned a
// bit-identical 14 kHz and the knob was dead in the hand over most of its
// travel. That is the same defect this control was rewritten to fix, so it
// does not get to come back on the other side of the detent.
constexpr float kFiltRightScale = 1.f - kFiltNeutral;
// Fixed, because FEED's RES knob is RATIO and there is no knob left. ZERO, and
// that is a measurement rather than a preference: SvfLp::SetRes maps through
// r^0.25, so a value that reads as "a touch of resonance" is not one. At 0.15
// the damping ratio is 0.38, the filter peaks 3 dB, a drone parks a partial on
// that peak, and the deck's output measured 1.58x kSatCeil (+4.0 dB) -- the
// engine's one hard bound, broken by the thing added to tame it. At 0 the
// damping ratio is 0.91, above the 0.707 where a two-pole stops peaking at
// all. A resonant peak downstream of a saturator on a network already allowed
// to escalate is a second thing that can ring, and this is not the deck to put
// it in. G11 is the gate.
constexpr float kFiltRes       = 0.f;

// The envelope. FLOOR rides the top quarter of the FALL knob (the control map
// above; the fold SWARM's round 2 used). kFlowFloorMin is the minimum floor
// enforced in FLOW so the drone promise holds at FLOOR 0.
constexpr float kFloorFoldStart = 0.75f;   // BY EAR, first try
constexpr float kFlowFloorMin   = 0.12f;   // BY EAR, first try

// The STEP accent's two halves, deliberately equal to SynthEngineT's so a
// listening session says which one wants to differ (spec section 4).
constexpr float kAccentVelFloor = 0.3f;    // BY EAR, first try
constexpr float kAccentDecFloor = 0.3f;    // BY EAR, first try

// The tanh ceiling on the deck sum (spec section 3.3), the
// BodyVoice::kFlowSatCeil pattern and for the same stated reason.
constexpr float kSatCeil = 0.55f;          // BY EAR, first try
constexpr float kSatInv  = 1.f / kSatCeil;

// Deck output trim, applied to the bank AND to SUB just before the ceiling.
//
// MEASURED 2026-08-19, and it exists because without it the ceiling above was
// not a ceiling. `1/sqrt(kPairs)` normalizes the bank for INCOHERENT
// summation, but FM carriers on chord tones plus an uncoupled SUB are
// partially coherent: the deck wanted 1.236 peak at LEVEL 1 and the tanh
// folded it to 0.537 -- a permanent 7.24 dB of gain reduction, audible from
// about LEVEL 0.3 upward. FEED measured +11.8 dB RMS over SYNTH as a drone and
// +18.1 dB struck. So the coupling was being judged through a compressor
// before it ever reached one, which is what Bastian reported.
//
// That also broke the instrument's own stated rule (docs/by-ear-decisions.md):
// a ceiling stays only if it prevents an actual FAILURE, never merely an
// unpleasant sound. At 0.25 the deck peaks near 0.28 against SYNTH's 0.333,
// the tanh is back to catching peaks (~0.8 dB at LEVEL 1) instead of riding
// the whole signal, and the cliff it exists for is untouched.
//
// The value is a LEVEL PARITY target, not a taste setting: Bastian's brief was
// that every engine sit at the same loudness so a live engine switch does not
// jump. Re-measure it (scratchpad level probe, or any equivalent) if the
// bank's gain structure changes -- kPairs above all, since the coherence this
// compensates for grows with P.
//
// Applied at the output rather than folded into each pair's amp because
// nothing in the feedback path reads amp: the ring taps `o1`/`o2`, which are
// the pre-amp carrier outputs. Scaling the sum is therefore exactly equal to
// scaling every amp, in one place instead of two.
// RE-MEASURED at kPairs = 6, as the paragraph above demands, and the answer was
// LEAVE IT. Moving P from 4 to 6 lifted the deck by 0.6 dB: drone peak 0.343 ->
// 0.369, and the RMS distance to SYNTH is unchanged at +3.05 dB against +3.1.
// Two FEED decks reach 0.738 against the limiter's -1 dBFS knee at 0.891, so
// the headroom the parity round bought is still there. A re-measurement that
// finds nothing is still a re-measurement; recorded so the next P change does
// not skip it on the grounds that this one changed nothing.
constexpr float kDeckGain = 0.25f;

// SUB: one sine an octave below the root, not in the ring and not coupled.
constexpr float kSubMax = 0.7f;            // BY EAR, first try

// The LANE_MOTION base a FEED deck gets from the host, i.e. the DEPTH the
// player sits at before any modulation. DEPTH is the one FEED control with no
// knob of its own, so this value carries spec section 4's defensive
// requirement -- "DEPTH at 0.5 must be a good sound" -- and gate G29 is what
// makes that requirement falsifiable rather than a hope.
constexpr float kDepthBase = 0.5f;         // BY EAR, first try

}  // namespace feed_cfg
}  // namespace spky
