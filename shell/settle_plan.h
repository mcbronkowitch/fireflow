#pragma once

// What the settle probe steps between, and how it decides. Data and pure
// arithmetic with no hardware type in it, for the same reason as mux_plan.h
// and coupon_expect.h: this is where a wrong constant is a visible line
// rather than a board that "looks slow" at the bench.
//
// Every number here is DERIVED -- from netlist.py for the channels and the
// resistors, from settle-budget.md's model for the predictions. The point of
// the run is to break or confirm them, so nothing in this file may be quoted
// as measured.
#include <cstdint>

namespace shell {

// One measured step: from a fully settled source channel to a target whose
// source impedance is known from the netlist.
struct SettlePair
{
    int      group;         // 0 = the 4067 on ADC_9, 1 = the 4051 on ADC_10
    int      from_ch;       // mux channel, parked and settled before the step
    int      to_ch;         // mux channel under test
    uint32_t r_src_ohm;     // switch Ron plus whatever the netlist wires
    uint32_t tau9_ns;       // 9.01 tau prediction, derived
    bool     is_reference;  // a 0 ohm step: the instrument's own zero (sec. 7)
};

inline constexpr int kSettlePairs = 6;
extern const SettlePair kSettlePlan[kSettlePairs];

// --- The ADC's own sampling-time ladder (fix round 4) ---
//
// The STM32H7 ADC1 HAL only offers eight sampling times, in ADC cycles x10
// (kept integer for the same %d-friendly reason as every other tenths field
// in this codebase; order matches ADC_SAMPLETIME_* in stm32h7xx_hal_adc.h):
// 1.5, 2.5, 8.5, 16.5, 32.5, 64.5, 387.5, 810.5.
inline constexpr int kSamplingLadderLen = 8;
extern const int     kSamplingLadderTenths[kSamplingLadderLen];

// The shortest rung of kSamplingLadderTenths whose acquisition window covers
// 9.01 * tau_acq for a channel whose source impedance is r_src_ohm -- ST's
// rule for term B, settle-budget.md section 2: tau_acq = (r_src_ohm + R_ADC)
// * C_ADC. Returns an index into kSamplingLadderTenths; the longest rung
// (index kSamplingLadderLen - 1) is returned as the best available answer
// for an impedance no rung covers, rather than an out-of-range index or a
// crash.
//
// Deliberately does NOT touch SettlePair or kSettlePlan: the choice follows
// from r_src_ohm, which the table already carries, so deriving it here keeps
// the table honest and gives the host test something real to check, instead
// of a ninth field that could silently drift from the impedance it claims to
// answer for.
//
// Pure and host-testable, like every other function in this file -- no
// hardware type, no runtime board measurement. See settle_plan.cpp for the
// R_ADC/C_ADC citations and for why this function's own ADC-clock constant
// is a fixed, measured-once figure rather than a live per-boot read (unlike
// shell/settle_probe.cpp's latency arithmetic, which now uses the live
// value every boot -- fix round 4's report explains the distinction).
int sample_time_index_for(uint32_t r_src_ohm);

// Delay grid step: fix round 5, not the original 100 ns.
//
// Measured on the coupon board 2026-09-17 (fix round 4's SHELL_SETTLE_CAL,
// after the instrument's own latency arithmetic was corrected to the
// measured ADC clock): lat_max_ns - lat_min_ns = 807 - 669 = 138 ns of
// aperture jitter. A 100 ns grid step claims resolution the instrument does
// not have -- G4's own 100 ns gate would then refuse every run, and
// widening THAT gate instead would be exactly the failure this whole probe
// exists to avoid (a test that cannot go red). The grid step is the one
// that has to give: it must never be finer than the instrument's own
// measured spread, so it moved to 200 ns, comfortably above the 138 ns
// floor.
//
// Whoever wants this narrower again must first RE-MEASURE the jitter (not
// assume it improved) and show the new number is smaller before touching
// this constant -- see task-4-report.md's fix-round-5 section for whether
// 138 ns looked reducible at the time this was written.
//
// Delay grid SPAN: 0..6400 ns through Task 5 fix round 1, extended to
// 0..12800 ns in that same round. On the coupon board, P1 (REF_A, the
// 3016 ns prediction) was STILL RISING at the last old grid point while its
// 20 us parked reference sat higher still -- d_settle_index() correctly
// reported "never settled", but at 6400 ns the grid could not distinguish
// "slow" from "beyond my reach", which is exactly the distinction section 7
// exists to make. kGridStepNs did not move again -- it is still pinned to
// the measured 138 ns jitter above -- only kGridPoints doubled to extend the
// reach.
inline constexpr int kGridStepNs = 200;
inline constexpr int kGridPoints = 65;

constexpr uint32_t grid_ns(int i)
{
    return static_cast<uint32_t>(i) * static_cast<uint32_t>(kGridStepNs);
}

// Repeats per grid point. Not decoration: min and max across these are where
// jitter in the start-to-aperture latency shows up, and section 7a's G2 reads
// the spread of the settled point as the instrument's noise floor.
inline constexpr int kRepeats = 64;

// Half an LSB of 12 bit on the 16-bit scale: a 12-bit LSB is 16 counts.
inline constexpr int kSettleCounts = 8;

// How long the probe parks on the source channel before stepping. The park is
// the reference the whole curve is measured against, so it is not "past the
// prediction" but far past it -- 6.6x the slowest of them.
inline constexpr uint32_t kParkNs = 20000;

// One grid point's statistics across kRepeats conversions.
struct Point
{
    int32_t mean;
    int32_t min;
    int32_t max;
};

// The smallest grid index whose mean is within kSettleCounts of `settled` AND
// stays within it for every larger index, or -1 if there is none.
//
// "And stays within it" is the whole definition. A node that rings crosses the
// band early and wanders back out; reporting the first crossing would report
// that node as the fastest one on the board.
int d_settle_index(const Point* pts, int n, int32_t settled);

// Section 7a's gate constants. Each is derived; the spec carries the
// derivation and it is not repeated here, but none of them is a taste.
//
// kKneeMaxNs and kJitterMaxNs both MEAN "one grid step" -- fix round 5
// makes that derivation explicit instead of two literal 100s that had to be
// kept equal to kGridStepNs (and to each other) by hand. A wrong constant
// used to be a defect waiting for someone to change one of the three
// numbers and not the other two; now there is only one number to change.
inline constexpr int32_t  kKneeMaxNs      = kGridStepNs;  // G1, one grid step
inline constexpr int32_t  kFloorMaxCounts = 64;           // G2
inline constexpr int32_t  kBandFactor     = 3;            // G3
inline constexpr int32_t  kJitterMaxNs    = kGridStepNs;  // G4, one grid step

// What a completed run reduces to before it is judged.
struct RunSummary
{
    int32_t knee_ns[kSettlePairs];  // -1 where the pair never settled
    int32_t b0;                     // spread of P0's settled reference read
    // Widest max-min over every SETTLED point of every pair -- i.e. only
    // points at or after that pair's own knee, never the transient before
    // it. Named widest_band through Task 5 fix round 1; renamed here because
    // that name let the field silently mean "every point", which G3 cannot
    // survive on a real curve: measured on the coupon board, pair 1's bands
    // ran 934, 581, 379, 329, 148, 81 ... ns and pair 2's ran 1140, 803, 476,
    // 275, 149 ... ns, widest at the EARLIEST points and collapsing to a
    // 30-50 floor once settled. That is not noise -- while a node is still
    // moving, the instrument's own ~140 ns aperture jitter gets converted
    // into voltage by the curve's slope, so a steep point MUST have a wide
    // band. G3's intent (a wide band must not be read as a time) applies to
    // the settled region, not the transient the knee already reports as
    // unsettled. A pair with no knee (knee_ns[p] == -1) contributes nothing
    // to this field -- its failure to settle is already visible in knee_ns
    // and must not also corrupt a band gate that was never about it. Do not
    // re-broaden this back to "every point" because that reads more
    // thorough; it fails every curve that actually settles.
    int32_t widest_settled_band;
    int32_t lat_min_ns;
    int32_t lat_max_ns;
    int32_t lat_mean_ns;
};

struct Gates
{
    bool g1_knee;
    bool g2_floor;
    bool g3_band;
    bool g4_jitter;

    bool ok() const { return g1_knee && g2_floor && g3_band && g4_jitter; }
};

// A run that fails any gate prints its numbers and refuses to name a settle
// time. It does NOT report a board defect -- the distinction is the entire
// point of section 7.
Gates settle_gates(const RunSummary& s);

} // namespace shell
