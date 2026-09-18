#include "tone_plan.h"

namespace shell {

namespace {

// Spec section 3's two ladders. Frequency: capacitive coupling grows as
// C dV/dt -- linearly with f at fixed level -- and resistive or ground
// coupling does not, so three points give a slope that can come out wrong.
// 5 kHz is the top because it keeps the phase grid coarse enough for the
// callback's own timing (section 5) and nothing above it is needed to read
// the slope. Level: a linearity check, because coupling that is not linear
// in level is not coupling.
constexpr int kFreqHz[3]  = {100, 1000, 5000};
constexpr int kLevelDb[3] = {-20, -6, 0};

constexpr bool below_corner(int f_hz)
{
    return kToneCornerHz > 0 && f_hz > 0 && f_hz < kToneCornerHz;
}

} // namespace

// The full 3 x 3 cross product, frequency-major so a reader of the raw log
// sees the level ladder run inside each frequency -- which is the order the
// linearity check wants, and the order that makes a drifting board show as a
// tilt within a frequency rather than across the table.
//
// Shape: sine only. A square wave would be a stronger aggressor, but its
// harmonics fold the frequency ladder into one point; it is an optional
// fourth row once the sine slope is known, and it is out of scope here.
const ToneRow kToneRows[kToneRowCount] = {
    {kFreqHz[0], kLevelDb[0], below_corner(kFreqHz[0])},
    {kFreqHz[0], kLevelDb[1], below_corner(kFreqHz[0])},
    {kFreqHz[0], kLevelDb[2], below_corner(kFreqHz[0])},
    {kFreqHz[1], kLevelDb[0], below_corner(kFreqHz[1])},
    {kFreqHz[1], kLevelDb[1], below_corner(kFreqHz[1])},
    {kFreqHz[1], kLevelDb[2], below_corner(kFreqHz[1])},
    {kFreqHz[2], kLevelDb[0], below_corner(kFreqHz[2])},
    {kFreqHz[2], kLevelDb[1], below_corner(kFreqHz[2])},
    {kFreqHz[2], kLevelDb[2], below_corner(kFreqHz[2])},
#if SHELL_TONE_DC
    // The static row, available only because Task 1's meter said the output
    // is DC-coupled. f_hz = 0: a constant has no frequency and no phase, so
    // it is measured as a whole-block mean like the running-silent level and
    // never gets a phase grid.
    {0, -6, false},
#endif
};

// Spec section 6: the deferred sequence runs "for the round-one aggressors
// that showed a delta at all, and for MUX8_EN_N against REF_A regardless,
// because that is the case the moat crossing is most directly in".
//
// WHICH ROUND-ONE AGGRESSORS SHOWED A DELTA IS A RESULT AND NOT A PLAN. Only
// the unconditional pair is here. When round one's reader has printed its
// verdicts, add the cases whose |delta| was non-trivial -- as further entries
// with their own want_row/want_group/want_channel -- and say in the commit
// which capture named them.
//
// The two indices are REF_A's MUX8_EN_N pair in kXtalkPlan, both directions.
// They are the 11th and 12th entries of that table (row 3's first pair); the
// host test asserts what they must be rather than trusting the arithmetic.
const ToneWinCase kToneWinCases[kToneWinCaseCount] = {
    {10, 3, 0, 8},   // MUX8_EN_N low -> high, victim REF_A
    {11, 3, 0, 8},   // MUX8_EN_N high -> low, victim REF_A
};

} // namespace shell
