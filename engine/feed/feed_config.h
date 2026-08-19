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
// P is MEASURED. The feed_pairs row in bench/workloads_feed.cpp prints cycles
// per pair on the Daisy Patch Submodule, and P follows from the 960 000-cycle
// block budget (spec section 8). The literal below is a PLACEHOLDER that exists
// so the desktop tasks can build, and it carries NO CPU claim of any kind: do
// not quote it, do not size anything against it by hand, and do not let a test
// depend on its value.
constexpr int kPairs = 4;

// False until the bench has run and kPairs above is its result. The gate
// "feed G8" in tests/test_feed_engine.cpp fails while this is false, so an
// undecided P cannot reach main.
constexpr bool kPDecided = false;

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
// 0.7 is the last BOND position at which all 80 measured rows (4 spreads x 5
// depths x 4 ratios, spread <= kSpreadKneeCt) still had a fundamental AND sat
// inside a cent of the played pitch. At 0.8 the worst row jumps from +0.609 ct
// to +22.522 ct and the first row loses its fundamental entirely -- the knob's
// cliff, arriving at 70 % of its travel.
constexpr float kBondPitchThreshold = 0.7f;

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
// Task 13 may move it, but not upward past the ceiling the regime map found:
// at 0.30 cycles (1.885 rad) the pitch centre is already gone at BOND 0, on a
// single pair, with no coupling and no spread -- +50 ct, because 1.885 rad of
// self-feedback is far past the classical beta = 1 rad point where a
// feedback-FM operator stops being periodic at its carrier. The two-sample
// average lifts that limit but does not remove it. At 0.08 cycles (0.503 rad)
// the whole BOND travel below the threshold holds within 0.61 ct, and the
// cliff still arrives on schedule at BOND 0.8. See docs/engine-map.md
// section 9, "Where the coupling tips", for the sweep this came off.
constexpr float kFbBaseCycles = 0.08f;     // BY EAR, first try; ceiling MEASURED

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

// DAMP: the one-pole inside the feedback path. FILT's centre detent is
// kDampCenterHz; the bipolar travel multiplies and divides it by kDampSpan.
constexpr float kDampCenterHz = 3200.f;    // BY EAR, first try
constexpr float kDampSpan     = 9.f;       // BY EAR, first try

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
