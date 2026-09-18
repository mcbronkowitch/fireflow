#pragma once

// The codec-tone probe's ladder, its phase arithmetic and the round-one
// aggressors it fires inside a conversion window. Data and pure arithmetic
// with no hardware type in it, for the same reason as xtalk_plan.h.
//
// It defines NO victim table: the victims are round one's five, used
// directly from xtalk_plan.h, and a second copy of them would be two tables
// to keep in step for one board.
//
// Spec: ../docs/superpowers/specs/2026-09-18-coupon-codec-tone-probe-design.md
#include <cstdint>

#include "settle_plan.h"
#include "shell_tone_probe.h"   // SHELL_TONE_DC, from Task 1's bench reading
#include "xtalk_plan.h"

namespace shell {

// Which of the three levels a printed row was taken at. Printed as level=%d,
// so the numbering is part of the output format and may not be reordered.
enum class ToneLevel : uint8_t
{
    Stopped       = 0,   // StopAudio() before the block; the codec is idle
    RunningSilent = 1,   // audio started, callback writes zeros
    Tone          = 2,   // one row of the frequency x level table
};

// The sample rate the ladder is checked against. Read from src/hw/board.h's
// SetAudioSampleRate(SAI_48KHZ); the firmware asks the board at runtime and
// prints the answer, and a board that disagrees with this constant is a
// finding, not a silent retune.
inline constexpr int kToneSampleRateHz = 48000;

// The quietest row. -20 dBFS is a tenth of full scale in amplitude, which is
// two decades above the ADC's own floor and still far enough below 0 dBFS
// for a linearity check to mean something.
inline constexpr int kToneDbfsFloor = -20;

// Task 1's bench reading, carried through the switch header. 1 = the audio
// output is DC-coupled at B1/B2 and the static row is available.
inline constexpr bool kToneHasStaticRow = (SHELL_TONE_DC != 0);

// The coupling network's corner, in Hz. 0 -- MEASURED, not a default: Task 1
// read the Patch Submodule's audio output on the bench 2026-09-18 (handheld
// multimeter, DC volts, black probe on TP_AGND, red on TP_AUDIO_L) and found
// it DC-coupled at B1/B2, no coupling capacitor. Spec section 9. A
// DC-coupled output has no corner, so this is 0 and below_corner is false
// everywhere: every row's level may be read as part of the capacitive
// slope. If a future board measures AC coupling, this carries that corner
// and the spec's section 9 other branch applies.
inline constexpr int kToneCornerHz = 0;

struct ToneRow
{
    int  f_hz;           // 0 on the static row: a constant has no frequency
    int  dbfs;           // negative, or 0 for full scale
    bool below_corner;   // the coupling network attenuates this row; its
                         // level may not be read as part of the slope
};

inline constexpr int kToneRowCount = 9 + (kToneHasStaticRow ? 1 : 0);
extern const ToneRow kToneRows[kToneRowCount];

// --- The phase grid (spec section 5) ---
//
// Phase is a fixed-point fraction of a period, 0 .. kTonePhaseScale. A power
// of two so the wrap is free and the arithmetic is integer: this runs beside
// a timed conversion and a float divide there would be measuring the
// measurement, the same argument cycles.h makes for ns_to_cycles().
inline constexpr uint32_t kTonePhaseScale  = 1u << 24;
inline constexpr int      kTonePhasePoints = 16;

// Sixteen points per period, 64 repeats each. Sixteen is enough because the
// quantity is a PEAK-TO-PEAK and a sine's extremes are broad -- a finer grid
// would cost time to resolve a maximum that is already flat where it matters.
inline constexpr int kToneRepeats = kRepeats;

// How many sub-measurements the Stopped and RunningSilent levels take, each
// of kToneRepeats conversions. It is round one's grid point count, and that
// is G8's whole premise: round one's settled_mean_spread is the peak-to-peak
// of 65 means of 64 conversions, so this level's has to be the peak-to-peak
// of 65 means of 64 conversions or the 4-count bound compares two
// differently-shaped spreads and means nothing.
inline constexpr int kToneLevelPoints = kGridPoints;

// How far the accumulator moves per output sample.
constexpr uint32_t phase_step_per_sample(int f_hz, int sr_hz)
{
    if(f_hz <= 0 || sr_hz <= 0) return 0u;
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(f_hz) * kTonePhaseScale)
        / static_cast<uint64_t>(sr_hz));
}

// Grid point `idx`'s phase. Defined for idx == kTonePhasePoints too, which
// is one full turn -- the same instant as point 0, and the host test asserts
// that the last GRID point is one step short of it rather than at it.
constexpr uint32_t phase_target(int idx)
{
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(idx) * kTonePhaseScale)
        / static_cast<uint64_t>(kTonePhasePoints));
}

// `n` samples of advance, wrapping. uint32 arithmetic wraps at 2^32, not at
// kTonePhaseScale, so the mask is explicit.
constexpr uint32_t phase_advance(uint32_t phase, uint32_t step, int n)
{
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(phase) + static_cast<uint64_t>(step) * n)
        & (kTonePhaseScale - 1u));
}

// Whether the advance from `prev` to `now` crossed `target`.
//
// THE WRAP IS THE WHOLE POINT. A magnitude comparison -- now >= target --
// fires on every sample after the first crossing and never fires at all for
// a target the accumulator steps over. The crossing test has to handle the
// step that wraps from near the top of the range to near the bottom, or a
// target just above the wrap is never reached and the foreground spins
// forever.
constexpr bool phase_reached(uint32_t prev, uint32_t now, uint32_t target)
{
    if(now >= prev)                       // no wrap in this step
        return prev < target && target <= now;
    return target > prev || target <= now;   // wrapped
}

// --- The edge inside the sampling window (spec section 6) ---
//
// The rung: 387.5 ADC cycles, index 6 of settle_plan.h's
// kSamplingLadderTenths, which settle-measured.md section 7 measured landing
// on the divider's true value. The sample-and-hold tracks the node through
// the whole window and the aperture closes at its end, so an edge fired at a
// chosen time BEFORE that end shows its remainder at the aperture.
inline constexpr int kToneWinRung = 6;

// 387.5 / 6.146 MHz = 63.05 us. DERIVED from the measured clock, for the
// host assertion that the grid fits inside the window; the firmware computes
// the real window from THIS boot's probe_adc::Clock and prints it, and a
// disagreement is a finding rather than a silent retune.
inline constexpr uint32_t kToneWinWindowNsNominal = 63050;

// The same 65 points at 200 ns as round one's grid, counted backwards from
// the end of the window, so the two instruments' curves line up end to end:
// round one's d after the edge continues where this one's d_before_end
// stops.
inline constexpr int kToneWinPoints = kGridPoints;
inline constexpr int kToneWinStepNs = kGridStepNs;

constexpr uint32_t tone_win_ns(int i)
{
    return static_cast<uint32_t>(i) * static_cast<uint32_t>(kToneWinStepNs);
}

// A round-one aggressor, fired inside a conversion window.
//
// The index is into kXtalkPlan and the three want_* fields are what that
// index must be. A row added to kXtalkPlan would silently repoint the index;
// the host assertion on want_row/want_group/want_channel is what turns that
// into a failure instead of a measurement of a different aggressor under the
// right label.
struct ToneWinCase
{
    int     xtalk_case;
    uint8_t want_row;
    int     want_group;
    int     want_channel;
};

inline constexpr int kToneWinCaseCount = 2;
extern const ToneWinCase kToneWinCases[kToneWinCaseCount];

} // namespace shell
