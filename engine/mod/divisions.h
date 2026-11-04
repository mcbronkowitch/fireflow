#pragma once
#include <cmath>
#include "util/math.h"

namespace spky {

// The musical rate ladder for SYNC mode: strictly speed-sorted,
// dotted/straight/triplet interleaved. cpb = cycles per beat; the bar-length
// entries assume 4/4. Names appear verbatim in the VCV RATE tooltip.
//
// A name is the length of one LANE CYCLE, not of a step: a STEP deck plays 8
// steps per cycle whatever STEPS says (clock_scale() = 8/steps cancels the
// slot count), so a step is an eighth of the named length.
struct Division { float cpb; const char* name; };

inline constexpr Division kDivisions[] = {
    {1.f/256.f, "64 bars"}, {1.f/128.f, "32 bars"}, {1.f/64.f, "16 bars"},
    {1.f/32.f, "8 bars"}, {1.f/16.f, "4 bars"}, {1.f/8.f, "2 bars"},
    {1.f/4.f,  "1 bar"},  {1.f/3.f,  "1/2."},   {1.f/2.f, "1/2"},
    {2.f/3.f,  "1/4."},   {3.f/4.f,  "1/2T"},   {1.f,     "1/4"},
    {4.f/3.f,  "1/8."},   {3.f/2.f,  "1/4T"},   {2.f,     "1/8"},
    {8.f/3.f,  "1/16."},  {3.f,      "1/8T"},   {4.f,     "1/16"},
    {6.f,      "1/16T"},  {8.f,      "1/32"},
};
inline constexpr int kDivisionCount = 20;

// The lane RATE knob's window onto the ladder: entries 0 .. 11, "64 bars" ..
// "1/4", with "2 bars" at noon -- ONE STEP PER BEAT on a STEP deck, so at full
// DENS every note lands on the TEMPO lamp (Bastian, 2026-11-04). In steps:
//
//   left half, 5 rungs at 0.1:   8 bars, 4 bars, 2 bars, 1 bar, 1/2 per step
//   noon:                        1/4 (one step per beat)
//   right half, 6 rungs at 1/12: 1/8, 1/16., 1/16, 1/32., 1/16T, 1/32
//
// The left half is coarse on purpose (powers of two, ending at today's left
// stop of one step per 8 bars; slower is PACE's job), the right half keeps
// the dotted/triplet interleave and stops at 32nds -- what was noon before,
// and the fastest a melody needs. History: the window was "8 bars" .. "1/32"
// with "1/4" at noon (32nd steps at noon, 1/256 at the right stop); earlier
// on 2026-11-04 it slid three rungs to "1/2" at noon, which was still too
// fast. The entries above "1/4" stay in the table for FLUX.
inline constexpr int   kLaneRateCount = 12;
inline constexpr int   kLaneRateNoon  = 5;      // "2 bars"
inline constexpr float kLaneRateLeftStep  = 0.5f / 5.f;   // knob per rung, left half
inline constexpr float kLaneRateRightStep = 0.5f / 6.f;   // knob per rung, right half

inline int division_index(float norm) {
    const float n = clampf(norm, 0.f, 1.f);
    // Both arguments are >= 0, so truncation is floor: round to nearest
    // without a libm call. 0.5 is noon from either side.
    const int k = n <= 0.5f
        ? static_cast<int>(n / kLaneRateLeftStep + 0.5f)
        : kLaneRateNoon + static_cast<int>((n - 0.5f) / kLaneRateRightStep + 0.5f);
    return k < kLaneRateCount ? k : kLaneRateCount - 1;
}

// The RATE knob position at the centre of ladder entry `idx` (0 .. 11): the
// inverse of division_index(), so a caller names a rung instead of a knob
// number that silently moves with the window.
inline constexpr float division_knob(int idx) {
    return idx <= kLaneRateNoon
        ? static_cast<float>(idx) * kLaneRateLeftStep
        : 0.5f + static_cast<float>(idx - kLaneRateNoon) * kLaneRateRightStep;
}

// The rungs COUPLE's free-world grid gravity snaps to (nearest_division):
// "8 bars" .. "1/32", the ladder as it stood before the three slow entries
// were added, so the free world's snapping did not move with the RATE window.
inline constexpr int kGridFirst = 3;

inline float division_hz(int idx, float bpm) {
    return (bpm / 60.f) * kDivisions[idx].cpb;
}

// nearest ladder rung to a free rate, compared in log space (ratio symmetry)
inline int nearest_division(float hz, float bpm) {
    int best = kGridFirst;
    float best_d = 1e30f;
    for (int i = kGridFirst; i < kDivisionCount; ++i) {
        const float d = std::fabs(std::log(hz / division_hz(i, bpm)));
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

// FLUX synced-delay rate: a slice of kDivisions starting at "1/2" (idx 8)
// through "1/32" (idx 16) — 12 rungs, incl. dotted & triplet. Since FLUX
// became a BBD the ladder is a TONE control as much as a time control: the
// 16x span in time is roughly 8x in BBD bandwidth once the 32 kHz clock
// ceiling is applied (8 kHz at "1/32" down to 1.0 kHz at "1/2" at 120 BPM),
// and further down as the tempo drops. There is no buffer to fit inside any
// more. Names come from kDivisions[kFluxRateOffset + i].
inline constexpr int kFluxRateOffset = 8;
inline constexpr int kFluxRateCount  = 12;
static_assert(kFluxRateOffset + kFluxRateCount == kDivisionCount,
              "FLUX's slice ends on the ladder's last entry");

inline int flux_division_index(float norm) {
    return static_cast<int>(clampf(norm, 0.f, 1.f) * (kFluxRateCount - 1) + 0.5f);
}

// Free-mode rate curve. Lives here (not in super_modulator.cpp) so the VCV
// tooltip shows exactly the Hz the engine runs.
inline constexpr float kRateFreeMin = 0.02f;
inline constexpr float kRateFreeMax = 30.f;
inline float free_hz(float norm) {
    return kRateFreeMin * std::pow(kRateFreeMax / kRateFreeMin, clampf(norm, 0.f, 1.f));
}

// TIDE: texture-lane rate scale (spec 2026-07-17 mod-tide). 10 rungs since
// 2026-11-04, when x1/8 was added at the slow end only. The knob is therefore
// split like PACE: the left half holds the five rungs x1/8..x1 (0.1 apart),
// the right half the four old ones above x1 (0.125 apart, at their old
// positions). Index kTideCentre sits exactly at 0.5 and is exactly x1 -- that
// is what makes TIDE 0.5 a bit-identical no-op. Names appear verbatim in the
// VCV TIDE tooltip.
inline constexpr float kTideRatios[] =
    { 0.125f, 0.25f, 1.f/3.f, 0.5f, 2.f/3.f, 1.f, 1.5f, 2.f, 3.f, 4.f };
inline constexpr const char* kTideNames[] =
    { "x1/8", "x1/4", "x1/3", "x1/2", "x2/3", "x1", "x3/2", "x2", "x3", "x4" };
inline constexpr int kTideCount  = 10;
inline constexpr int kTideCentre = 5;   // index of x1

inline int tide_index(float norm) {
    const float n = clampf(norm, 0.f, 1.f);
    if (n <= 0.5f)
        return static_cast<int>(n * (2.f * kTideCentre) + 0.5f);
    return kTideCentre + static_cast<int>(
        (n - 0.5f) * (2.f * (kTideCount - 1 - kTideCentre)) + 0.5f);
}

// free (unsynced) curve: 1/8 .. 4, exactly 1 at centre (2^0) from both sides
inline float tide_free(float norm) {
    const float n = clampf(norm, 0.f, 1.f);
    return n <= 0.5f ? std::pow(2.f, 6.f * (n - 0.5f))
                     : std::pow(2.f, 4.f * (n - 0.5f));
}

// PACE: the global modulation time-stretch (spec 2026-08-12 modulation-pace).
// A ladder of powers of two since 2026-11-04 -- it was continuous before. FLUX's
// delay stays on the raw tempo (Instrument::_apply_tempo), so only a
// power-of-two PACE keeps a FLUX division a whole fraction of the paced beat:
// at any rung the echo period divides the TEMPO lamp's beat (or the beat
// divides it), so the two lamps meet and the repeats land on the notes' grid.
// A continuous PACE left both free to slide past each other. Continuity bought
// little: in FLOW, RATE and TIDE already run on continuous free curves.
//
// The rungs sit where the old piecewise curve (32^(2n-1) left, 4^(2n-1)
// right) passed through them, so a saved knob on a rung keeps its value:
//
//   0.0 x1/32  0.1 x1/16  0.2 x1/8  0.3 x1/4  0.4 x1/2  0.5 x1  0.75 x2  1.0 x4
//
// and a knob between two rungs takes the nearer one in log2 (the old
// curve's exponent, rounded). 0.5 is still exactly 1.0, the identity that
// makes PACE 0.5 a bit-identical no-op. No pow: a table of exact values.
//
// Lives here rather than in the host for the reason stated at the top of
// free_hz: the curve belongs where the engine reads it, so a tooltip cannot
// drift from what actually runs. The host formats the name.
inline constexpr float kPaceRungs[] =
    { 1.f / 32.f, 1.f / 16.f, 1.f / 8.f, 1.f / 4.f, 1.f / 2.f, 1.f, 2.f, 4.f };
inline constexpr int kPaceRungCount = 8;
inline constexpr int kPaceUnityRung = 5;   // kPaceRungs[5] == 1

inline float pace_mult(float norm) {
    const float n = clampf(norm, 0.f, 1.f);
    // The old curve's log2: 5(2n-1) on the left half, 2(2n-1) on the right.
    const float e = n <= 0.5f ? 10.f * n - 5.f : 4.f * n - 2.f;
    // e >= -5, so the argument is positive and truncation is floor: round
    // to nearest without a libm call.
    int k = static_cast<int>(e + 0.5f + static_cast<float>(kPaceUnityRung));
    if (k >= kPaceRungCount) k = kPaceRungCount - 1;
    return kPaceRungs[k];
}

} // namespace spky
