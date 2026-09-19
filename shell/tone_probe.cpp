#include "tone_probe.h"

#include <cmath>

#include "coupon_expect.h"
#include "cycles.h"
#include "mux_plan.h"
#include "mux_scan.h"
#include "probe_adc.h"
#include "settle_plan.h"
#include "shell_git_hash.h"
// SHELL_TONE_DC is read below (tone_plan.h) and lives in the generated
// switch header beside SHELL_TONE_PROBE, so this translation unit needs it
// directly -- main.cpp's include of it does not reach here. Same reason
// xtalk_probe.cpp includes shell_xtalk_probe.h directly. The Makefile
// carries the matching edge on tone_probe.o.
#include "shell_tone_probe.h"
#include "tone_plan.h"
#include "xtalk_plan.h"

// Raw HAL, not libDaisy's AdcHandle -- the same note probe_adc.cpp carries
// above its own include of this header. Needed here for ADC1,
// ADC_CR_ADSTART and ADC_ISR_EOC, used by measure_window_point() below and
// by nothing else in this file: that function is the one place in either
// probe that drives ADC1's registers outside probe_adc, and its own comment
// says why it cannot go through probe_adc::sample_now().
#include <stm32h7xx_hal.h>

namespace shell {

namespace {

// --- Step 1: the callback ---

// What the callback publishes to the foreground. volatile, and read in the
// order written: dwt first, then phase, then blocks. The foreground only
// ever needs a CONSISTENT pair (dwt_at_block_start, phase_at_block_start),
// and it gets one by reading blocks, then the pair, then blocks again, and
// retrying if the count moved -- a seqlock, and the cheapest correct thing
// on a single-writer single-reader pair this small.
volatile uint32_t g_blocks             = 0;
volatile uint32_t g_dwt_at_block_start = 0;
volatile uint32_t g_phase_at_block     = 0;

// The row the callback is currently emitting. Written by the foreground
// between cases, never during one.
volatile uint32_t g_phase_step = 0;
volatile float    g_amplitude  = 0.0f;

void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                   daisy::AudioHandle::OutputBuffer out,
                   size_t                           size)
{
    (void)in;
    const uint32_t t0 = cycles_now();

    uint32_t    phase = g_phase_at_block;
    const uint32_t st = g_phase_step;
    const float    a  = g_amplitude;

    for(size_t i = 0; i < size; ++i)
    {
        // st == 0 covers two cases: RunningSilent (a == 0, so which branch
        // runs makes no audible difference) and Task 4's static row (a !=
        // 0, f_hz == 0). phase_step_per_sample() returns 0 for f_hz == 0
        // (tone_plan.h), which has no phase to modulate -- writing the raw
        // amplitude directly is the same constant spec section 9's bench
        // check wrote to measure DC coupling (0.5011872f at -6 dBFS), not
        // amp * sin(whatever phase the previous row happened to leave
        // behind in the accumulator).
        float v;
        if(st == 0)
        {
            v = a;
        }
        else
        {
            // sinf() and not a table: this callback has 96 samples of a
            // 2 ms block to fill and nothing else to do, so the cost is
            // irrelevant -- and a table would put its own interpolation
            // error into the aggressor, which is the one signal in this
            // measurement that has to be what it says it is.
            const float s = sinf(6.2831853f * (static_cast<float>(phase)
                                               / static_cast<float>(kTonePhaseScale)));
            v = a * s;
        }
        out[0][i] = v;
        out[1][i] = v;
        phase = phase_advance(phase, st, 1);
    }

    // Published AFTER the buffer is filled but describing the block's START:
    // t0 was taken at entry, and phase_at_block is the phase the block began
    // with, which is what the foreground's phase(t) formula needs.
    g_dwt_at_block_start = t0;
    g_phase_at_block     = phase;   // the NEXT block's starting phase
    ++g_blocks;
}

// --- Step 2: the foreground's phase clock ---

// The phase at DWT count `t`, from the last block boundary the callback
// published. Spec section 5:
//   phi(t) = phase_at_block_start + f * (t - dwt_at_block_start) / f_core
// expressed in the fixed-point phase units, with the multiply done in 64 bit
// because a 2 ms block at 480 MHz is ~960 000 core cycles and the step is up
// to 2^24 / 9.6.
//
// ANY SPIN BUILT ON TOP OF THIS (a future caller waiting for a target phase,
// Task 4's measure_phase_point) MUST run with interrupts enabled, so the
// callback keeps the codec fed; only probe_adc::sample_now() masks them, and
// only across its own conversion (~2 us at the working rung, far inside a
// 2 ms block). The two are easy to conflate, and getting them the wrong way
// round starves the callback.
uint32_t phase_now(uint32_t step_per_sample, int sr_hz)
{
    uint32_t blocks0, dwt, phase, blocks1;
    do
    {
        blocks0 = g_blocks;
        dwt     = g_dwt_at_block_start;
        phase   = g_phase_at_block;
        blocks1 = g_blocks;
    } while(blocks0 != blocks1);

    const uint32_t elapsed = cycles_now() - dwt;   // unsigned, wraps correctly
    // core cycles -> output samples -> phase.
    const uint64_t samples
        = (static_cast<uint64_t>(elapsed) * static_cast<uint64_t>(sr_hz))
          / (static_cast<uint64_t>(kCoreMhz) * 1000000ull);
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(phase)
         + static_cast<uint64_t>(step_per_sample) * samples)
        & (kTonePhaseScale - 1u));
}

// --- Task 4: the phase-point measurement (spec section 5) ---

// Two periods of the LOWEST frequency in the ladder, in core cycles: at
// 100 Hz a period is 10 ms, so two are 20 ms, and at 480 MHz that is
// 9 600 000 cycles. WORTH FLAGGING: the task brief's own comment for this
// constant calls it "derived from kFreqHz[0] rather than written as a
// literal, so a ladder that gains a lower frequency does not silently
// shorten its own patience" -- but kFreqHz has internal linkage in
// tone_plan.cpp (anonymous namespace) and is not visible here, so there is
// no expression this file could actually write that tracks a ladder
// change. This IS a literal, correct for TODAY's ladder (100/1000/5000 Hz)
// only; a future row below 100 Hz needs this constant updated by hand
// alongside it. Said plainly rather than left implied by a comment that
// overclaims what the code does.
constexpr uint32_t kPhaseWaitTimeoutCycles = 9600000u;

// Repeats that never saw their target phase. Printed, never folded into an
// average: a run that lost repeats must say so rather than report a
// quieter n that looks like a cleaner measurement. Lifetime count, like
// probe_adc::timeouts() -- not reset per block or per case -- and printed
// on SHELL_TONE_HEALTH below. A Task 4 addition, it was appended to
// SHELL_TONE_GATES at the time for the same reason audio_virgin was
// appended to SHELL_TONE_LEVEL/STAT in Task 3 (see print_level_lines()'s
// comment); Task 5's fix round moved it, with the three other counters,
// onto its own line -- see the byte-budget note beside the two PrintLine
// calls at the end of the block.
uint32_t g_phase_timeouts = 0;

// Repeats of the window sweep whose EOC poll never saw the conversion end.
// Printed, never folded into an average: a run that lost repeats must say so
// rather than report a quieter n that looks like a cleaner measurement.
// Lifetime count, like g_phase_timeouts above and probe_adc::timeouts() --
// not reset per block or per case -- and printed on SHELL_TONE_HEALTH
// below, beside phase_timeouts. It has to be its own counter and cannot be
// probe_adc::timeouts(): measure_window_point() polls EOC itself and never
// calls sample_now(), so that counter cannot see these.
uint32_t g_win_timeouts = 0;

// One phase point of one tone row against one victim.
//
// Every repeat waits for the NEXT crossing of its target phase, so repeats
// are one or more periods apart and never share a block's interrupt jitter
// -- which is the whole reason this is a phase grid and not 64 conversions
// in a row. The wait runs with interrupts ENABLED; only the conversion
// masks them.
Point measure_phase_point(uint32_t step, int sr_hz, int phase_idx, int* n_out)
{
    const uint32_t target = phase_target(phase_idx);

    int64_t sum   = 0;
    int32_t lo    = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    int     valid = 0;
    for(int r = 0; r < kToneRepeats; ++r)
    {
        uint32_t prev = phase_now(step, sr_hz);
        // Bounded, like every other wait in these two probes: a tone that
        // stopped -- a codec that lost its clock, a callback that died --
        // must leave a flag and a shorter n, not a board that is silent with
        // no clue why. Two periods at the LOWEST frequency in the ladder,
        // expressed in core cycles.
        const uint32_t t0 = cycles_now();
        bool reached = false;
        while(cycles_now() - t0 < kPhaseWaitTimeoutCycles)
        {
            const uint32_t now = phase_now(step, sr_hz);
            if(phase_reached(prev, now, target)) { reached = true; break; }
            prev = now;
        }
        if(!reached) { ++g_phase_timeouts; continue; }

        // KNOWN GAP, same shape as read_parked()'s below: sample_now(nullptr)
        // cannot see its own EOC-poll timeout -- the span pointer that would
        // carry probe_adc::kTimeoutSentinel is null here -- so a conversion
        // that times out on this one repeat folds a stale/garbage reading
        // into sum/lo/hi uncounted. Same non-exclusion mean_of_repeats() has,
        // not mean_span_of_repeats()'s exclusion. Verbatim from the task
        // brief; the phase-wait's own bound above is the exclusion this
        // function relies on for a dead callback, and probe_adc::timeouts()
        // (printed on SHELL_TONE_CAL every block) is the lifetime count of
        // any ADC-level timeout on this board, phase grid included.
        const int32_t v = probe_adc::sample_now(nullptr);
        sum += v;
        if(v < lo) lo = v;
        if(v > hi) hi = v;
        ++valid;
    }
    if(n_out) *n_out = valid;
    return valid > 0 ? Point{static_cast<int32_t>(sum / valid), lo, hi}
                     : Point{-1, -1, -1};
}

// --- A small shared spin, used by both the calibration passes below and
// measure_level()'s park. Not in the brief as a named helper; factored out
// because the same "spin for N ns on the DWT counter" body would otherwise
// appear four times in this file. ---
inline void spin_ns(uint32_t ns)
{
    const uint32_t t0 = cycles_now();
    while(cycles_now() - t0 < ns_to_cycles(ns)) { }
}

// Writes the chain word that selects (group, ch) with nothing else
// aggressing -- the same construction as xtalk_probe.cpp's read_parked()
// and park_victim(): chain_word(kCouponChain, step_pattern(...), 0).
inline void write_victim_word(MuxScan& chain, int group, int ch)
{
    chain.write_chain(chain_word(
        kCouponChain, step_pattern(kCouponChain, step_of(kCouponChain, group, ch)), 0u));
}

// --- Step 4: the calibration passes, and G2/G4/G5 ---
//
// This whole section -- kTieHi1..kTieLo2, SpanRead, measure_span() and
// read_parked() -- is round one's G5 pass from xtalk_probe.cpp, copied
// rather than reinvented (the brief is explicit about that), with only the
// print-line prefix changed from SHELL_XTALK_ to SHELL_TONE_. IT IS THE SAME
// PASS: a later change to one of the two copies is a divergence from the
// other and should be treated as a defect in whichever copy did not get it,
// not as an intentional fork.

// The four 0 R tie channels on the 4067 -- see xtalk_probe.cpp's own comment
// for the derivation (netlist.py's NEIGHBOURS and the spare-channel loop).
constexpr int kTieHi1 = 1, kTieHi2 = 5, kTieLo1 = 3, kTieLo2 = 7;

// Park on (group, ch) and take kRepeats conversions.
//
// KNOWN GAP, carried over unchanged from xtalk_probe.cpp: this goes through
// probe_adc::mean_of_repeats(), which does NOT exclude a timed-out repeat --
// its 0 folds into the sum and divides by `repeats` like any other reading,
// with nothing in the return value to say a repeat was lost (probe_adc.h).
// "Unchanged in substance" is the brief's own instruction for this pass; the
// fix (routing through the RepeatAccum/take_one-style exclusion that
// measure_level() below uses instead) is xtalk_probe.cpp's to make, not
// this file's, or the two copies stop being the same pass.
int32_t read_parked(MuxScan& chain, int group, int ch)
{
    write_victim_word(chain, group, ch);
    spin_ns(kParkNs);
    return probe_adc::mean_of_repeats(kRepeats);
}

struct SpanRead
{
    Span    span;
    int32_t hi_spread;
    int32_t lo_spread;
};

SpanRead measure_span(MuxScan& chain)
{
    // All four ties sit on the 4067; the working rung already covers their
    // tau many times over. See xtalk_probe.cpp's measure_span() for the
    // physics argument -- it is not repeated here because it is the same
    // pass and would drift the moment only one copy carried the reasoning.
    probe_adc::select(probe_adc::channel_of_group(0));

    const int32_t hi1 = read_parked(chain, 0, kTieHi1);
    const int32_t hi2 = read_parked(chain, 0, kTieHi2);
    const int32_t lo1 = read_parked(chain, 0, kTieLo1);
    const int32_t lo2 = read_parked(chain, 0, kTieLo2);

    SpanRead r{};
    r.hi_spread = (hi1 > hi2) ? (hi1 - hi2) : (hi2 - hi1);
    r.lo_spread = (lo1 > lo2) ? (lo1 - lo2) : (lo2 - lo1);

    const int32_t rail = (hi1 + hi2) / 2;
    const int32_t zero = (lo1 + lo2) / 2;
    r.span.rail  = static_cast<uint16_t>(rail);
    r.span.zero  = static_cast<uint16_t>(zero);
    r.span.valid = r.hi_spread <= static_cast<int32_t>(kTieSpread)
                   && r.lo_spread <= static_cast<int32_t>(kTieSpread)
                   && rail >= static_cast<int32_t>(kRailFloor)
                   && zero <= static_cast<int32_t>(kRailMargin)
                   && rail > zero;
    return r;
}

// Park a victim for a real measurement: its chain word, its ADC channel and
// the rung its impedance picks, then the settle spin. Same shape as
// xtalk_probe.cpp's park_victim(), a separate definition because this file
// cannot reach into that one's anonymous namespace.
void park_victim(MuxScan& chain, const XtalkVictim& v)
{
    write_victim_word(chain, v.group, v.channel);
    probe_adc::select_time(
        probe_adc::channel_of_group(v.group),
        probe_adc::sample_time_for_rung(sample_time_index_for(v.r_src_ohm)));
    spin_ns(kParkNs);
}

// --- Task 5: the edge inside the sampling window (spec section 6) ---

// The EOC-poll bound for the window sweep. NOT a second number invented
// here: it is probe_adc's own kPollTimeoutCycles (probe_adc.cpp:211),
// 300 us at 480 MHz, sized there to clear 396 ADC cycles even if the real
// ADC clock turned out as slow as 3 MHz -- and the long, 387.5-cycle rung
// this sweep runs at is exactly the case that sizing was chosen for. It is
// carried as a literal because that constant has internal linkage in
// probe_adc.cpp and there is no expression this file could write that tracks
// it; a change there needs this one changed by hand alongside it, and this
// comment is the citation rather than a claim that the two are linked.
constexpr uint32_t kWinPollTimeoutCycles = 144000u;  // 300 us at 480 MHz

// One point of the window sweep.
//
// The order is the reverse of every other measurement in these two probes:
// the conversion starts FIRST and the aggressor edge lands inside its
// acquisition window. The sample-and-hold tracks the node through the whole
// window and the aperture closes at its end, so a transient whose remainder
// at the aperture is still above the criterion shows as a function of how
// long before the end it was fired.
//
// t0 here is the CONVERSION START, not a latch edge. That is the difference
// from round one, and it is why this cannot be a case in round one's table.
// It deliberately does NOT call probe_adc::sample_now(): that function owns
// the whole start-to-read span, and this sequence has to act in the middle
// of it. Do not "unify" the two -- doing so removes the measurement.
Point measure_window_point(MuxScan& chain, const XtalkCase& c,
                           uint32_t window_cycles, uint32_t d_before_end_ns,
                           int* n_out)
{
    const uint32_t d_cycles = ns_to_cycles(d_before_end_ns);
    // The edge is fired when this much of the window has already elapsed.
    // A d_before_end longer than the window would mean firing before the
    // conversion started, which is round one's measurement at an
    // uncommanded delay; the caller has already refused that, and this is
    // the belt.
    if(d_cycles >= window_cycles) return Point{-1, -1, -1};
    const uint32_t fire_at = window_cycles - d_cycles;

    int64_t sum   = 0;
    int32_t lo    = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    int     valid = 0;
    for(int r = 0; r < kToneRepeats; ++r)
    {
        chain.write_chain(c.word_a);
        // The file's shared park spin. It is NOT part of the measurement --
        // it only lets word_a settle before the conversion starts -- which
        // is why it may be a helper that re-reads the clock, and why the
        // in-window wait below deliberately is not.
        spin_ns(kParkNs);

        // Interrupts masked across the whole sequence, not just the
        // conversion: the edge's position INSIDE the window is the
        // measurement, and a block interrupt landing between the start and
        // the latch would move it by microseconds. The mask is saved and
        // restored rather than cleared and set, so this cannot turn
        // interrupts on if it was called with them off -- same discipline as
        // probe_adc::sample_now().
        //
        // WHAT THE MASK COSTS, AND WHAT DOES NOT WATCH IT. The task brief's
        // own sentence here read "it costs the callback one window, ~63 us,
        // well inside a 2 ms block, and G7 is what notices if that ever
        // stops being true". That is false for this sweep as it is actually
        // called, and the thing that falsified it is the StopAudio() at the
        // call site: during the sweep there is no callback at all, so there
        // is nothing to cost a window and nothing to starve. G7 could not
        // notice either way -- missed_blocks is accumulated only inside
        // measure_level(), which this function never calls, and g_blocks is
        // frozen while the codec is stopped. Stated plainly rather than left
        // as a guard that cannot go red.
        //
        // The conditional half of it is still worth keeping: IF this
        // function were ever called with audio running, the masked region is
        // ~63 us of a 2 ms block and G7 would then be the thing that sees a
        // starved callback -- but only because measure_level() runs in that
        // arrangement, not because of anything here.
        const uint32_t primask = __get_PRIMASK();
        __disable_irq();

        const uint32_t t0 = cycles_now();
        ADC1->CR |= ADC_CR_ADSTART;    // the same direct start probe_adc uses

        // Timed against the conversion's OWN t0 and written out here rather
        // than routed through spin_ns(): that helper takes its own clock
        // reading, which would move the edge off the conversion start by
        // however long the call and the second read take. This loop IS the
        // measurement.
        while(cycles_now() - t0 < fire_at) { }
        (void)chain.write_chain_timed(c.word_b);

        // KNOWN, NOT FIXED HERE: the timeout path does not read ADC1->DR, so
        // an EOC arriving just after kWinPollTimeoutCycles expires leaves the
        // flag set into the next repeat, which would then see it immediately
        // and read a stale DR. This is the same shape probe_adc::sample_now()
        // has (probe_adc.cpp:218-243) -- a replicated pre-existing pattern,
        // not something this sequence introduced -- and a fix belongs in both
        // copies at once or they stop being the same construction.
        // win_timeouts was 0 in both complete blocks of
        // task-5-board-capture.txt, so no repeat on record took that path.
        bool timed_out = true;
        while((cycles_now() - t0) < kWinPollTimeoutCycles)
            if((ADC1->ISR & ADC_ISR_EOC) != 0u) { timed_out = false; break; }
        const int32_t v = timed_out ? -1 : static_cast<int32_t>(ADC1->DR);

        __set_PRIMASK(primask);

        if(timed_out) { ++g_win_timeouts; continue; }
        sum += v;
        if(v < lo) lo = v;
        if(v > hi) hi = v;
        ++valid;
    }
    if(n_out) *n_out = valid;
    return valid > 0 ? Point{static_cast<int32_t>(sum / valid), lo, hi}
                     : Point{-1, -1, -1};
}

// --- Step 4: the two silent levels ---

// A single conversion's contribution to a running mean/min/max, folding in
// exactly one value -- used for both the sub-measurement (64 conversions)
// and the whole-level total (65 x 64) in measure_level() below, so one
// sample_now() call updates both without taking the conversion twice.
//
// SAME SHAPE as xtalk_probe.cpp's RepeatAccum/take_one, and for the same
// reason: probe_adc::sample_now() with the span checked against
// kTimeoutSentinel EXCLUDES a timed-out repeat, unlike
// probe_adc::mean_of_repeats() above. This is a controller decision and not
// a brief instruction -- the brief's measure_level() sketch names no
// reducer -- and it is the one that keeps this level's statistic comparable
// to round one's silent curve, which is built the same way in
// xtalk_probe.cpp's measure_silent_point().
struct RepeatAccum
{
    int64_t sum = 0;
    int32_t lo  = 0x7FFFFFFF;
    int32_t hi  = -0x7FFFFFFF;
    int     n   = 0;

    void fold(int32_t v)
    {
        sum += v;
        if(v < lo) lo = v;
        if(v > hi) hi = v;
        ++n;
    }
};

// A whole-block measurement: no phase, because there is no phase in
// silence.
//
// ITS SHAPE WAS MEANT TO BE ROUND ONE'S SILENT CURVE, and that was G8's
// premise. Round one's settled_mean_spread is the peak-to-peak of 65 means
// of 64 conversions each; this takes 65 sub-measurements of kToneRepeats
// conversions, which is the same COUNT.
//
// IT IS NOT THE SAME STATISTIC, and the original sentence here said it was.
// Round one's measure_silent_point() spins park + d before EVERY conversion
// and its 65 grid points carry 65 DIFFERENT d values, 0 to 12800 ns, so its
// spread is a peak-to-peak across 65 different pre-conversion delays.
// measure_level() below spins nothing and varies nothing across its 65
// points: its spread is repeat-to-repeat noise on the mean. Same count,
// different content -- a statement about what two pieces of code compute,
// checkable from their source, and no claim about what causes the
// difference. read_tone.py's g8() docstring is the authority and carries
// the line numbers; G8 was split there into a report-only round-one
// comparison and a real gate against this campaign's own boot-virgin
// floors.
//
// So DO NOT TRIM the 65 for block time on the grounds that it buys
// like-for-like with round one. It never did. What it does buy is a spread
// of the same count as the campaign's four recorded boot-virgin floors,
// which IS what G8 gates on.
//
// It costs 65 x 64 conversions at about 2.5 us each, roughly 10 ms per level
// per victim: nothing against a block that takes minutes.
struct LevelResult
{
    Point   p;
    int32_t settled_mean_spread;
    int32_t widest_sample_band;
};

// The two printed lines for one LevelResult, factored out of measure_level()
// below so fix round 2 can re-emit a CACHED boot-virgin result inside every
// block without a second, drifting copy of the format strings. Same field
// order, same line names, for a freshly measured result or a cached one --
// a reader cannot tell which call site produced a line from its shape, only
// from `audio_virgin` and (for the boot-virgin lines) `case`'s sign; see the
// re-emission loop in run_tone_probe() for why that is enough.
//
// n= carries kToneRepeats, the per-SUB-MEASUREMENT repeat count -- the
// brief's own line for this, matching the field settled_mean_spread's
// peak-to-peak is built from. mean/min/max are the whole level's, over all
// 65 x 64 conversions -- a different scope from n=, exactly as SHELL_XTALK's
// point line and SHELL_XTALK_STAT's spread differ in scope in round one.
//
// audio_virgin=%d is appended after the design spec's own field list
// (section 8 does not carry it) rather than inserted between existing
// fields, so a reader keyed on the spec's names and positions is unaffected
// by its presence. Fixed position, last field: Task 6's reader keys G8 off
// this and only this.
void print_level_lines(bench::Board& hw, int case_index, ToneLevel level,
                       const XtalkVictim& v, const LevelResult& result,
                       bool audio_virgin)
{
    hw.PrintLine("SHELL_TONE_LEVEL case=%d level=%d victim_group=%d "
                 "victim_ch=%d r_src=%d n=%d mean=%d min=%d max=%d "
                 "audio_virgin=%d",
                 case_index, static_cast<int>(level), v.group, v.channel,
                 static_cast<int>(v.r_src_ohm), kToneRepeats,
                 result.p.mean, result.p.min, result.p.max,
                 audio_virgin ? 1 : 0);
    hw.PrintLine("SHELL_TONE_STAT case=%d settled_mean_spread=%d "
                 "widest_sample_band=%d",
                 case_index, result.settled_mean_spread, result.widest_sample_band);
}

// ONE DIVERGENCE FROM THE BRIEF'S SKETCH `LevelResult measure_level(MuxScan&,
// const XtalkVictim&)`: `hw`, `block_size`, `sr_hz`, `case_index`, `level`
// and `missed_blocks` are added here because the brief's own call site
// (StopAudio()/StartAudio() around two measure_level() calls per victim) and
// its own SHELL_TONE_LEVEL/SHELL_TONE_STAT PrintLine block need them, and the
// two-argument sketch has nowhere to put them. LevelResult's shape is
// unchanged.
//
// `missed_blocks`: nullptr for the Stopped level. G7 counts blocks the
// callback should have produced against elapsed wall-clock time, and with
// the codec halted ON PURPOSE there IS no callback producing blocks -- every
// Stopped measurement would report ~100% missed against a check that does
// not apply to it. G7 only means something while the callback is expected
// to be alive, i.e. RunningSilent here and Tone in Task 4.
//
// `audio_virgin`: true for exactly one pass, taken once per boot before any
// StartAudio() call anywhere in this image (see run_tone_probe() below) --
// the only floor comparable to round one's, whose xtalk_probe.cpp never
// touches audio at all. Every per-block Stopped and RunningSilent call
// passes false: the per-block Stopped level is reached via StopAudio() on a
// codec that an EARLIER block has already started and stopped, which is a
// different history from "never started", and G8 (image floor vs round
// one's floor, at a 4-count tolerance) must not mix the two. The per-block
// Stopped level stays -- it is still the right reference for the
// running-silent DIFFERENCE measured inside the same block -- this flag
// only marks which lines G8 may read.
LevelResult measure_level(bench::Board& hw, MuxScan& chain, int block_size, int sr_hz,
                          int case_index, ToneLevel level, const XtalkVictim& v,
                          uint32_t* missed_blocks, bool audio_virgin)
{
    park_victim(chain, v);

    const uint32_t case_t0              = cycles_now();
    const uint32_t blocks_at_case_start = g_blocks;

    RepeatAccum whole;   // every valid conversion across all 65 points
    int32_t mean_lo = 0x7FFFFFFF, mean_hi = -0x7FFFFFFF;
    int32_t widest_band   = -1;
    int     usable_points = 0;

    for(int k = 0; k < kToneLevelPoints; ++k)
    {
        RepeatAccum sub;
        for(int r = 0; r < kToneRepeats; ++r)
        {
            uint32_t      span = 0;
            const int32_t s    = probe_adc::sample_now(&span);
            if(span == probe_adc::kTimeoutSentinel) continue;
            sub.fold(s);
            whole.fold(s);
        }
        // A point at which no repeat converted contributes to no statistic
        // rather than folding a divide-by-zero or a -1 into a spread.
        if(sub.n == 0) continue;
        const int32_t sub_mean = static_cast<int32_t>(sub.sum / sub.n);
        if(sub_mean < mean_lo) mean_lo = sub_mean;
        if(sub_mean > mean_hi) mean_hi = sub_mean;
        const int32_t band = sub.hi - sub.lo;
        if(band > widest_band) widest_band = band;
        ++usable_points;
    }

    // --- Step 3: G7, the callback health gate ---
    //
    // A starved callback outputs the DMA buffer's stale contents, so the
    // tone is not the tone -- and a delta_pp measured against that is
    // unreadable rather than merely wrong. The count is the callback's own
    // entries against what the elapsed DWT time and the block size say it
    // should have been.
    //
    // Tolerance of one block, and only one: the two clocks are read at
    // slightly different instants and a boundary can fall between them. Two
    // is a real miss.
    if(missed_blocks != nullptr)
    {
        const uint32_t expected_blocks
            = static_cast<uint32_t>((static_cast<uint64_t>(cycles_now() - case_t0)
                                     * static_cast<uint64_t>(sr_hz))
                                    / (static_cast<uint64_t>(kCoreMhz) * 1000000ull
                                       * static_cast<uint64_t>(block_size)));
        const uint32_t seen = g_blocks - blocks_at_case_start;
        const int32_t  missed = static_cast<int32_t>(expected_blocks)
                                - static_cast<int32_t>(seen);
        if(missed > 1) *missed_blocks += static_cast<uint32_t>(missed);
    }

    LevelResult result{};
    // -1/-1/-1, not a divide-by-zero or the 0x7FFFFFFF init values leaking
    // into print, if every repeat in the whole level timed out.
    result.p = (whole.n > 0)
                   ? Point{static_cast<int32_t>(whole.sum / whole.n), whole.lo, whole.hi}
                   : Point{-1, -1, -1};
    result.settled_mean_spread = (usable_points > 0) ? (mean_hi - mean_lo) : -1;
    result.widest_sample_band  = widest_band;   // already -1 if no point converted

    print_level_lines(hw, case_index, level, v, result, audio_virgin);

    return result;
}

// Row 1's five RunningSilent means, held for the whole block. G5 judges
// these rather than the Stopped level: RunningSilent is audio-active, which
// is the operating point every later tone row (Task 4) shares, while Stopped
// is the codec halted on purpose and says nothing about where the mux sits
// during real operation. -1/false when the level came back with no usable
// conversion at all.
int32_t g_running_silent_mean[kXtalkVictims];
bool    g_running_silent_seen[kXtalkVictims];

// The boot-virgin pass's results, one per victim, filled ONCE before
// run_tone_probe()'s while(1) loop and never touched again.
//
// Fix round 2: a host that connects after boot -- which is every host --
// cannot see a line printed once before the CDC port is realistically open,
// and StartLog(false) does not wait for one; the logger's accumulation
// overflow (logger.cpp:78-87) is at its worst at exactly that moment too,
// with nothing draining. Printing the boot-virgin pass once made its only
// output structurally unreachable, so this cache exists to let every block
// RE-EMIT it, verbatim, alongside the block's own live lines -- see the
// re-emission loop in run_tone_probe() for why re-emitting rather than
// re-measuring is the whole point.
LevelResult g_boot_virgin[kXtalkVictims];

} // namespace

void run_tone_probe(bench::Board& hw)
{
    cycles_init();
    probe_adc::init(hw);
    hw.StartLog(false);

    // Read, not assumed. main.cpp's CPU probe carries the comment about the
    // day a block size was inferred as 48 and was in fact 96.
    const int block_size = static_cast<int>(hw.AudioBlockSize());
    const int sr_hz      = static_cast<int>(hw.AudioSampleRate());

    MuxScan chain;
    chain.init();

    // The clock and latency reference channel: kXtalkVictimTable[3], R_SP10,
    // the 0 R tie to AGND on the 4067 -- the same choice xtalk_probe.cpp
    // makes for its own clock pass, and for the same reason: a channel with
    // nothing to settle carries the least of anything else into the spans.
    const XtalkVictim& v0 = kXtalkVictimTable[3];
    probe_adc::select(probe_adc::channel_of_group(v0.group));
    (void)probe_adc::warm_up();
    (void)read_parked(chain, v0.group, v0.channel);   // park for the clock pass

    // Measured once, here -- only the PRINT of it is in the loop, because a
    // one-shot print goes out before Windows has enumerated the CDC device.
    const probe_adc::Clock clk
        = probe_adc::measure_clock(kRepeats, probe_adc::channel_of_group(v0.group));

    // --- The boot-virgin floor: G8's actual reference ---
    //
    // Taken ONCE, right here, before ANY StartAudio() call anywhere in this
    // image -- the audio subsystem has never been started, which is round
    // one's own condition (xtalk_probe.cpp never calls StartAudio at all).
    // StopAudio() is deliberately NOT called first: calling it would still
    // be the first audio-related HAL call this boot, and skipping it keeps
    // this pass provably untouched rather than "probably a no-op" (see
    // task-3-report.md's fix-round note on whether Stop() before Start() is
    // a no-op on this board -- unmeasured this session).
    //
    // Printed with audio_virgin=1 on SHELL_TONE_LEVEL/SHELL_TONE_STAT here,
    // once -- kept for whoever is watching a fresh boot over SWD or a logic
    // analyser, but THE READER MUST NEVER NEED THIS PRINT: fix round 2 found
    // it structurally unreachable by any USB-CDC host, which connects after
    // reset. The RESULT is cached into g_boot_virgin[] and re-emitted inside
    // every block below; that re-emission, not this one-shot print, is what
    // a reader actually sees. G8 is an image-vs-image comparison at a
    // 4-count tolerance and must read only audio_virgin=1 lines -- see
    // "Controller decisions" in task-3-report.md for why the per-block
    // Stopped level (StopAudio() called on a codec an earlier block already
    // started and stopped) is not the same floor and is kept for a
    // different purpose.
    for(int v = 0; v < kXtalkVictims; ++v)
    {
        g_boot_virgin[v] = measure_level(hw, chain, block_size, sr_hz, v,
                                         ToneLevel::Stopped, kXtalkVictimTable[v],
                                         nullptr, true);
    }

    while(1)
    {
        // MEASURED, per block, not derived: wall-clock milliseconds this
        // block takes, from daisy::System::GetNow() (HAL_GetTick(), a 1 ms
        // SysTick counter) rather than the DWT cycle counter this file uses
        // everywhere else -- cycles_now() is 32 bit at 480 MHz and wraps
        // roughly every 8.9 s, many times over inside a phase-grid block
        // that runs into minutes, so a naive `cycles_now() - t0` here would
        // alias to a small, wrong number. GetNow() wraps at ~49.7 days, far
        // outside anything this block can take. Printed on SHELL_TONE_HEALTH
        // below as block_ms -- the one thing only the firmware can measure,
        // and the plan names it as a decision input for Bastian.
        const uint32_t block_start_ms = daisy::System::GetNow();

        // The configuration line: block_size and sr are READ from the board,
        // never assumed -- see the comment above. adc_khz is the measured
        // clock, not the nominal one. phase_points is Task 2's kTonePhasePoints
        // -- a compile-time constant, known now even though Task 4 is what
        // walks the grid. rv4 is a literal 0, not a placeholder: this image
        // runs no rv4-dependent case (row 6 of kXtalkPlan is the only one,
        // and this plan's window cases point at row 3), the same reason
        // Task 1's image printed the same literal. Field list and order are
        // the design spec's (section 8), not the task brief's -- the brief
        // did not give this line verbatim and undercounted it; Task 6's
        // reader parses by name and a missing field is a parse it cannot do.
        hw.PrintLine("SHELL_TONE_CFG adc_khz=%d repeats=%d phase_points=%d "
                     "block_size=%d sr=%d rv4=%d git=%s",
                     clk.measured_adc_khz, kToneRepeats, kTonePhasePoints,
                     block_size, sr_hz, 0, SHELL_GIT_HASH);

        // The two raw spans, unconverted as well as converted, printed every
        // block so the ADC-clock derivation stays independently checkable
        // off the log alone -- same shape as SHELL_XTALK_CLK.
        hw.PrintLine("SHELL_TONE_CLK span_short_cyc=%d span_long_cyc=%d "
                     "smp_short_tenths=%d smp_long_tenths=%d",
                     clk.span_short_cyc, clk.span_long_cyc, 165, 3875);

        // The instrument measuring itself, on the same parked 0 R tie --
        // same shape as xtalk_probe.cpp's latency pass. clk.ok guards the
        // whole pass: without a measured clock there is nothing correct to
        // subtract at all, not a wrong-but-present number.
        probe_adc::select(probe_adc::channel_of_group(v0.group));
        (void)read_parked(chain, v0.group, v0.channel);

        int32_t lat_min = 0x7FFFFFFF, lat_max = -0x7FFFFFFF;
        int64_t lat_sum = 0;
        int32_t val_min = 0x7FFFFFFF, val_max = -0x7FFFFFFF;
        int     valid_n = 0;
        if(clk.ok)
        {
            for(int i = 0; i < kRepeats; ++i)
            {
                uint32_t      span = 0;
                const int32_t v    = probe_adc::sample_now(&span);
                if(span == probe_adc::kTimeoutSentinel) continue;
                const int32_t l = static_cast<int32_t>(cycles_to_ns(span))
                                  - clk.working_conversion_ns;
                if(l < lat_min) lat_min = l;
                if(l > lat_max) lat_max = l;
                lat_sum += l;
                if(v < val_min) val_min = v;
                if(v > val_max) val_max = v;
                ++valid_n;
            }
        }
        const bool    all_timed_out = valid_n == 0;
        const int32_t lat_mean
            = all_timed_out ? -1 : static_cast<int32_t>(lat_sum / valid_n);
        if(all_timed_out) { lat_min = -1; lat_max = -1; }
        const int32_t b0 = all_timed_out ? -1 : (val_max - val_min);   // G2's floor

        hw.PrintLine("SHELL_TONE_CAL lat_mean_ns=%d lat_min_ns=%d "
                     "lat_max_ns=%d b0=%d timeouts=%d",
                     lat_mean, lat_min, lat_max, b0,
                     static_cast<int>(probe_adc::timeouts()));

        // --- Fix round 2: re-emit the boot-virgin floor into THIS block ---
        //
        // g_boot_virgin[] was measured exactly ONCE, before the while(1)
        // loop and before any StartAudio() call this boot (see the comment
        // there). These lines print the SAME numbers every block -- NOT
        // re-measured -- because re-measuring here would destroy the one
        // property audio_virgin=1 exists to assert: "taken before any
        // StartAudio on this boot". By block N the audio subsystem has
        // already been started and stopped many times over (every earlier
        // block's five RunningSilent cases), so a fresh measurement here
        // would be exactly the per-block Stopped level a few lines below,
        // under a label that claims otherwise. Re-emission changes when a
        // reading is PRINTED, never when it was TAKEN.
        //
        // case= is NEGATIVE, -(v+1), for these five lines only. Every live
        // case in this file (this loop's 0..9, and Task 4's phase-grid cases
        // continuing the same non-negative counter) is >= 0, so a negative
        // case number can never collide with a live one, and a reader can
        // tell a cached line from a live one by the SIGN of `case` alone,
        // with no need to also check audio_virgin.
        for(int v = 0; v < kXtalkVictims; ++v)
        {
            print_level_lines(hw, -(v + 1), ToneLevel::Stopped, kXtalkVictimTable[v],
                              g_boot_virgin[v], true);
        }

        // --- Step 3/4: the two silent levels, per victim ---

        uint32_t missed_blocks = 0;
        for(int v = 0; v < kXtalkVictims; ++v)
        {
            g_running_silent_mean[v] = 0;
            g_running_silent_seen[v] = false;
        }

        int case_idx = 0;
        for(int v = 0; v < kXtalkVictims; ++v)
        {
            const XtalkVictim& vv = kXtalkVictimTable[v];

            // Stopped: the codec idle. NOT G8's floor -- see the
            // audio_virgin=1 pass above run_tone_probe()'s loop for that.
            // This is StopAudio() called on a codec that (from the second
            // victim of the first block onward) an EARLIER case has already
            // started and stopped; it is the right reference for the
            // running-silent DIFFERENCE measured a few lines below, in the
            // SAME block, and it is kept for exactly that.
            //
            // On the very first victim of the very first block, this is
            // also the first StopAudio() call this boot on a subsystem that
            // has never been Start()ed (the audio_virgin pass above does not
            // call it either). Whether that is a no-op on this board is
            // UNMEASURED -- see task-3-report.md; no mechanism is assumed
            // here.
            hw.StopAudio();
            (void)measure_level(hw, chain, block_size, sr_hz, case_idx,
                                ToneLevel::Stopped, vv, nullptr, false);
            ++case_idx;

            // Running, silent: audio started, the callback writing zeros.
            // I2S, the SAI DMA and the block interrupt as aggressor, with no
            // signal on the trace. The difference from Stopped is the DMA
            // finding.
            g_amplitude  = 0.0f;
            g_phase_step = 0u;
            hw.StartAudio(AudioCallback);
            const LevelResult running
                = measure_level(hw, chain, block_size, sr_hz, case_idx,
                                ToneLevel::RunningSilent, vv, &missed_blocks, false);
            ++case_idx;

            if(running.p.mean >= 0)   // -1 means no repeat converted at all
            {
                g_running_silent_mean[v] = running.p.mean;
                g_running_silent_seen[v] = true;
            }
        }

        // --- Task 4: the frequency x level phase grid (spec section 5) ---
        //
        // Per row: the callback's step and amplitude are set ONCE here, in
        // the foreground, between cases -- never touched by
        // measure_phase_point() itself, which only ever reads the `step`
        // argument the caller passes it -- and every victim of that row
        // reads against the SAME tone. hw.Delay(20) lets the codec and the
        // analog path settle into the new row before the first conversion
        // of the first victim: ten blocks at 96 samples/48 kHz is ~20 ms,
        // two periods of the lowest frequency in the ladder -- derived, not
        // measured, and generous on purpose because it costs 20 ms against
        // a row that costs seconds.
        //
        // case_idx CONTINUES the same non-negative counter the Stopped/
        // RunningSilent loop above left at 2 * kXtalkVictims (10): every
        // live case in this file is >= 0, matching the boot-virgin cache's
        // own negative-vs-non-negative split above.
        for(int i = 0; i < kToneRowCount; ++i)
        {
            const ToneRow& row = kToneRows[i];

            // The amplitude from dbfs, computed once per row and handed to
            // the callback between cases, never during one. powf() is fine
            // here: this runs once per row in the foreground, nowhere near a
            // timed path.
            const float amp = powf(10.0f, static_cast<float>(row.dbfs) / 20.0f);
            g_phase_step = phase_step_per_sample(row.f_hz, sr_hz);
            g_amplitude  = amp;

            hw.Delay(20);

            for(int v = 0; v < kXtalkVictims; ++v)
            {
                const XtalkVictim& vv = kXtalkVictimTable[v];

                // Fix 1 (controller ruling, post-board-capture): EVERY tone
                // case gets this CASE line, the static row included, printed
                // BEFORE branching on whether there is a grid to walk. Before
                // this fix the static row printed only SHELL_TONE_LEVEL --
                // which carries no f_hz and no dbfs field, because that line
                // shape has neither -- so Task 6's reader could only infer
                // which table row a level=2 line belonged to from there being
                // exactly one such row. Now every tone case in the block has
                // its identity on SHELL_TONE_CASE, keyed uniformly, and a
                // reader learns the static row has no grid from the simple
                // absence of SHELL_TONE point lines after it -- not from
                // counting.
                //
                // BYTE BUDGET, and the reason the spec's single SHELL_TONE
                // line is split in two here: libDaisy's log buffer is 128
                // bytes (lib/libDaisy/src/hid/logger.h:29) and the spec's
                // combined line runs about 130 at its widest values -- past
                // the buffer. Same split and same reason as the crosstalk
                // probe's SHELL_XTALK_CASE (plan decision 1, cited in spec
                // section 8, not re-argued here). This line runs 108 bytes;
                // the point line below runs 62.
                //
                // TWO DIFFERENT FAILURES, ONE VISIBLE SYMPTOM -- do not
                // conflate them. A too-long line truncates at the 128-byte
                // buffer and would ALSO stamp "$$"; that is what the split
                // above prevents. But the ONE "$$" measured in the 968-line
                // board capture of commit 438fd51 (line 2, fusing case=10's
                // phase_idx=2 into the next SHELL_TONE_CASE line -- see
                // task-4-report.md) landed on this file's 62-byte POINT
                // line, nowhere near 128 bytes. That is logger.cpp:78-87's
                // ACCUMULATION overflow: when the host does not drain fast
                // enough, tx_ptr_ is not reset and the next PrintLine's
                // vsnprintf appends into the same buffer regardless of any
                // one line's length -- xtalk_probe.cpp's SHELL_XTALK_CFG
                // comment carries the full mechanism. Shortening a line does
                // not buy immunity from this kind; it only moves where the
                // damage lands. The split fixes the first failure. Nothing
                // in this file fixes, or was meant to fix, the second.
                hw.PrintLine("SHELL_TONE_CASE case=%d level=%d f_hz=%d dbfs=%d "
                             "victim_group=%d victim_ch=%d r_src=%d below_corner=%d",
                             case_idx, static_cast<int>(ToneLevel::Tone), row.f_hz,
                             row.dbfs, vv.group, vv.channel,
                             static_cast<int>(vv.r_src_ohm), row.below_corner ? 1 : 0);

                if(row.f_hz == 0)
                {
                    // The static row: a tone row by table position (f_hz ==
                    // 0, present only when Task 1 measured a DC-coupled
                    // output, kToneHasStaticRow) but a LEVEL row by SHAPE:
                    // it has no phase, so it is never walked as a 16-point
                    // grid. It takes the same whole-block measurement the
                    // Stopped/RunningSilent levels do -- measure_level(),
                    // which parks the victim itself -- printed as
                    // SHELL_TONE_LEVEL/SHELL_TONE_STAT with level=2 (Tone),
                    // immediately after the SHELL_TONE_CASE line just
                    // printed above. missed_blocks is tracked here (not
                    // nullptr): audio is running, same as RunningSilent, so
                    // G7 applies -- see measure_level()'s own comment on
                    // this parameter, "and Tone in Task 4".
                    (void)measure_level(hw, chain, block_size, sr_hz, case_idx,
                                        ToneLevel::Tone, vv, &missed_blocks, false);
                    ++case_idx;
                    continue;
                }

                // Park the victim and select its rung -- measure_level()
                // does this internally for the two levels above;
                // measure_phase_point() below does neither, so it is done
                // here, once per victim, before the 16-point walk.
                park_victim(chain, vv);

                for(int k = 0; k < kTonePhasePoints; ++k)
                {
                    int         n = 0;
                    const Point p = measure_phase_point(g_phase_step, sr_hz, k, &n);
                    hw.PrintLine("SHELL_TONE case=%d phase_idx=%d n=%d mean=%d min=%d max=%d",
                                 case_idx, k, n, p.mean, p.min, p.max);
                }
                ++case_idx;
            }
        }

        // --- The silent-cadence arm (codec-tone-measured.md section 6) ---
        //
        // WHAT THIS EXISTS TO SEPARATE. Reading the campaign's own blocks for
        // ABSOLUTE level rather than delta_pp, a 100 Hz tone moves REF_A 852
        // counts off its boot-virgin Stopped level -- 53 LSB of 12 bit,
        // against a criterion of half an LSB -- reproducing across three
        // boots to two counts, ordered by source impedance, exactly zero on
        // both 150 ohm ties, and FLAT under a ten-fold amplitude change.
        // delta_pp is a peak-to-peak WITHIN a case and is blind to it.
        //
        // Three explanations were already excluded by lines this block
        // prints: starting the codec moves every victim by one count or less
        // (RunningSilent), a steady -4.34 V on the output moves REF_A by two
        // (the static row), and ten times the amplitude moves it by under two
        // (the three level rows of each frequency). What was left was
        // CONFOUNDED, and structurally so: measure_phase_point() waits for the
        // next phase crossing, so the interval between two conversions IS one
        // period of the tone. Frequency and measurement cadence were the same
        // variable, and the campaign's run contained no case with a long wait
        // and no tone.
        //
        // This is that case. g_amplitude goes to zero while g_phase_step keeps
        // the row's REAL value, so the callback writes a * s with a == 0 --
        // silence -- while the accumulator advances exactly as before and the
        // grid waits exactly as long. Same cadence, no aggressor, codec
        // running in both arms.
        //
        // HOW TO READ THE RESULT. If the shift survives here, it is this
        // probe's own cadence meeting something in the ADC path and the audio
        // output is exonerated. If it vanishes, the output moves a 5150 ohm
        // divider by 53 LSB of 12 bit and round two's negative result was
        // measured with a statistic that could not see it. NO MECHANISM IS
        // ASSUMED EITHER WAY; this prints the two arms and nothing more.
        //
        // Every victim, not only the three that moved: the two 150 ohm ties
        // are the attribution axis and reading zero in this arm too is the
        // check that keeps them one. They cost 21 s of the ~57 s this arm
        // adds to a ~171 s block.
        //
        // The frequencies come from kToneRows itself rather than a second
        // table -- one table to keep in step, the same argument this file
        // makes for using round one's victim table directly. The static row
        // is skipped: f_hz == 0 has no period, so it has no cadence to hold
        // constant.
        //
        // case_idx CONTINUES the same counter, so these land after the static
        // row and every case index published before this arm existed keeps
        // its number.
        for(int i = 0; i < kToneRowCount; ++i)
        {
            const int f_hz = kToneRows[i].f_hz;
            if(f_hz == 0) continue;

            bool already = false;
            for(int j = 0; j < i; ++j)
                if(kToneRows[j].f_hz == f_hz) { already = true; break; }
            if(already) continue;

            // The step is the row's; the amplitude is not. Set once here, in
            // the foreground, between cases -- same discipline as the tone
            // ladder above.
            g_phase_step = phase_step_per_sample(f_hz, sr_hz);
            g_amplitude  = 0.0f;

            hw.Delay(20);

            for(int v = 0; v < kXtalkVictims; ++v)
            {
                const XtalkVictim& vv = kXtalkVictimTable[v];

                // Same line shape as a tone case, so the block's completeness
                // rules (read_tone.py rules 5 and 6) cover this arm with no
                // new tag and no new rule: the case counter stays dense and a
                // case with f_hz != 0 still owes exactly phase_points points.
                // level=3 is what tells the two arms apart; dbfs carries
                // kToneSilentDbfs because silence has no level and this line
                // shape has a field for one.
                hw.PrintLine("SHELL_TONE_CASE case=%d level=%d f_hz=%d dbfs=%d "
                             "victim_group=%d victim_ch=%d r_src=%d below_corner=%d",
                             case_idx, static_cast<int>(ToneLevel::SilentCadence),
                             f_hz, kToneSilentDbfs, vv.group, vv.channel,
                             static_cast<int>(vv.r_src_ohm), 0);

                park_victim(chain, vv);

                for(int k = 0; k < kTonePhasePoints; ++k)
                {
                    int         n = 0;
                    const Point p = measure_phase_point(g_phase_step, sr_hz, k, &n);
                    hw.PrintLine("SHELL_TONE case=%d phase_idx=%d n=%d mean=%d min=%d max=%d",
                                 case_idx, k, n, p.mean, p.min, p.max);
                }
                ++case_idx;
            }
        }

        // RESTORE THE STATE THE TONE LADDER USED TO LEAVE BEHIND, and do it
        // before the span/G5 pass rather than after. That pass reads the two
        // HI and two LO ties and judges every victim's address against them,
        // and the comment ahead of the window sweep below records why it runs
        // "with the codec in whatever state the tone-case loop left it in":
        // changing those conditions changes two measurements neither that
        // task nor this arm is about. Without this the span and all five G5
        // verdicts would be taken in silence where every earlier block took
        // them under the last row's output, and any shift between a new
        // capture and the committed one would be unattributable.
        //
        // Read from the table's last row rather than restated, so a row added
        // or reordered there carries this with it.
        {
            const ToneRow& last = kToneRows[kToneRowCount - 1];
            g_phase_step = phase_step_per_sample(last.f_hz, sr_hz);
            g_amplitude  = powf(10.0f, static_cast<float>(last.dbfs) / 20.0f);
            hw.Delay(20);
        }

        // --- Step 4: G5's span and per-victim verdict, copied from
        // xtalk_probe.cpp's measure_span()/coupon_verdict() pass (see the
        // comment ahead of that section above) ---

        const SpanRead sr = measure_span(chain);
        hw.PrintLine("SHELL_TONE_SPAN zero=%d rail=%d hi_spread=%d "
                     "lo_spread=%d valid=%d",
                     static_cast<int>(sr.span.zero),
                     static_cast<int>(sr.span.rail),
                     sr.hi_spread, sr.lo_spread, sr.span.valid ? 1 : 0);

        bool address_ok = true;
        for(int v = 0; v < kXtalkVictims; ++v)
        {
            const XtalkVictim& vv = kXtalkVictimTable[v];
            const Expect e = coupon_expect(step_of(kCouponChain, vv.group, vv.channel));
            const bool ok = g_running_silent_seen[v] && sr.span.valid
                            && coupon_verdict(e, static_cast<uint16_t>(g_running_silent_mean[v]),
                                              sr.span);
            if(!ok) address_ok = false;
            hw.PrintLine("SHELL_TONE_G5 victim_group=%d victim_ch=%d "
                         "expect=%d mean=%d ok=%d",
                         vv.group, vv.channel, static_cast<int>(e),
                         g_running_silent_mean[v], ok ? 1 : 0);
        }

        // --- Task 5: the edge inside the sampling window (spec section 6) ---
        //
        // WHERE THIS SITS. The brief does not place the sweep; the
        // controller's range is "after the tone-case loop, before
        // SHELL_TONE_GATES", and it is put at the END of that range, after
        // the span/G5 pass rather than before it. The span calibration and
        // the per-victim G5 verdict have always run with the codec in
        // whatever state the tone-case loop left it in, and putting a
        // StopAudio() ahead of them would have changed the conditions of two
        // measurements this task is not about.
        //
        // THE CODEC IS STOPPED for this sequence, and StopAudio() is called
        // here explicitly because the tone-case loop above leaves it RUNNING
        // -- the last row's step and amplitude are still in the callback.
        // This is a round-one aggressor, a chain word pair, and the tone is
        // not part of the question. codec=0 is printed on every
        // SHELL_TONE_WINCASE line below because that is then true, and
        // because the case index on that line is a small integer that also
        // exists among the tone cases: a reader has to be told the codec
        // state differed rather than be left to infer it.
        //
        // Audio is deliberately NOT restarted afterwards. ONE CONSEQUENCE
        // WORTH STATING rather than leaving implied: the NEXT block's
        // SHELL_TONE_CLK/SHELL_TONE_CAL pass now runs with the codec
        // stopped, where before this task it ran with the codec still
        // playing the last tone row.
        //
        // What can be said without measuring is structural only. Block 1's
        // CLK/CAL has always run before any StartAudio() call this boot (the
        // clock pass sits above run_tone_probe()'s while(1) loop and the
        // boot-virgin pass calls no audio function), so every later block
        // ran under a different condition from block 1. After this change
        // every block's condition is identical to block 1's, rather than
        // block 1 being the odd one out among all the others.
        //
        // THE EFFECT ON THE NUMBERS IS UNMEASURED. No mechanism is claimed,
        // no direction is expected and no size is implied; it cannot be
        // checked against the captures on record either, because
        // task-4-board-capture-c5631f4.txt carries two blocks' GATES/END
        // lines but only one CFG/CLK/CAL -- the host attached mid-block, so
        // the one codec-never-started CAL that exists was never captured.
        // The next capture is what makes it measurable, because it will
        // carry consecutive blocks' CAL lines under the same condition.
        hw.StopAudio();

        // 387.5 sampling cycles at THIS boot's measured ADC clock. Not
        // kToneWinWindowNsNominal -- that constant exists for the host
        // assertion that the grid fits inside the window, and a firmware
        // that used it would be back to assuming a clock the probe can
        // measure, which is the mistake settle-measured.md section 1 records
        // costing three fix rounds.
        //
        // No separate clk.ok guard is needed below: measure_clock() sets
        // core_cyc_per_adc_cyc to 0.0 when the clock pass failed
        // (probe_adc.cpp:330-332), so window_cycles and window_ns come out 0
        // and the fits test refuses the sweep on its own.
        const uint32_t window_cycles = static_cast<uint32_t>(
            (static_cast<double>(kSamplingLadderTenths[kToneWinRung]) / 10.0)
            * clk.core_cyc_per_adc_cyc + 0.5);
        const uint32_t window_ns = cycles_to_ns(window_cycles);

        // A window shorter than the grid means the last points would fire
        // before the conversion started. Refuse the sweep and say so rather
        // than print points measured at a delay nobody commanded.
        //
        // THE REFUSAL IS ONE-SIDED, and that is worth saying out loud. It
        // bounds window_ns from BELOW only, which is what the brief asked
        // for. An over-measured ADC clock would make window_cycles
        // arbitrarily large, fits would still read 1, and the in-window wait
        // -- which has no bound of its own, because the conversion's own
        // start is its reference -- would spin with interrupts masked for as
        // long as that window claims to be. Not a defect against the spec;
        // simply not guarded, and not previously written down.
        hw.PrintLine("SHELL_TONE_WINDOW window_ns=%d nominal_ns=%d "
                     "grid_end_ns=%d fits=%d",
                     static_cast<int>(window_ns),
                     static_cast<int>(kToneWinWindowNsNominal),
                     static_cast<int>(tone_win_ns(kToneWinPoints - 1)),
                     (window_ns > tone_win_ns(kToneWinPoints - 1)) ? 1 : 0);

        if(window_ns > tone_win_ns(kToneWinPoints - 1))
        {
            for(int i = 0; i < kToneWinCaseCount; ++i)
            {
                const ToneWinCase& w = kToneWinCases[i];
                const XtalkCase&   c = kXtalkPlan[w.xtalk_case];

                // The LONG rung, picked by kToneWinRung and not by the
                // victim's impedance: the acquisition window is the axis
                // this sweep walks inside, so sample_time_index_for(r_src)
                // -- what park_victim() would have chosen -- is the wrong
                // rung here by construction. That is also why this does not
                // call park_victim(): measure_window_point() writes word_a
                // and does its own park on every repeat.
                probe_adc::select_time(
                    probe_adc::channel_of_group(c.group),
                    probe_adc::sample_time_for_rung(kToneWinRung));

                // BYTE BUDGET, and the reason the design spec's single
                // combined SHELL_TONE_WIN line is split in two here:
                // libDaisy's log buffer is 128 bytes
                // (lib/libDaisy/src/hid/logger.h:29) and the spec's combined
                // form -- case, victim identity, both chain words and one
                // window point together -- runs 143 characters, past the
                // buffer, truncated and stamped "$$". Same split and same
                // reason as round one's SHELL_XTALK_CASE (plan decision 1,
                // cited in spec section 8, not re-argued here) and as this
                // file's own SHELL_TONE_CASE above. This line runs 112
                // characters; the point line below runs 79.
                //
                // WHICH BOUND THOSE NUMBERS ARE, because "at its widest
                // values" does not say and two different bounds exist. They
                // are DATA-widest: the widest rendering reachable with this
                // board's own tables (xtalk_case <= 57, victim_ch <= 15,
                // r_src <= 5150, 16-bit chain words, 16-bit ADC readings,
                // d_before_end_ns <= 12800, n <= 64). The 143 is at
                // victim_ch=8; at victim_ch=15 it is 144. TYPE-widest -- every
                // %d at the 11 characters a negative int can print -- is far
                // larger (179 for this line, 225 for the spec's combined
                // form) and is unreachable from these tables. Ruling 18 holds
                // under either bound: the combined line is over the 125-byte
                // payload both ways.
                //
                // TWO DIFFERENT FAILURES, ONE VISIBLE SYMPTOM -- do not
                // conflate them; the tone-case loop's own BYTE BUDGET
                // comment above carries the full argument and is not
                // repeated. The split fixes the too-long-line failure only.
                // logger.cpp:78-87's ACCUMULATION overflow strikes a line of
                // ANY length whenever the host does not drain fast enough,
                // and shortening a line does not buy immunity from it -- it
                // only moves where the damage lands. Nothing here fixes, or
                // was meant to fix, that one. The lever it responds to is
                // total volume per run, and this sweep adds 1 + 2 + 130
                // lines to a block that MEASURED 821 lines before it
                // (task-4-board-capture-c5631f4.txt, the one complete block
                // it carries, lines 775-1595) -- about +16 %. The 968 cited
                // a few comments above is a capture FILE's length, not a
                // block's; do not reuse it as one.
                hw.PrintLine("SHELL_TONE_WINCASE case=%d xtalk_case=%d victim_group=%d "
                             "victim_ch=%d r_src=%d word_a=%d word_b=%d codec=%d",
                             i, w.xtalk_case, c.group, c.channel,
                             static_cast<int>(c.r_src_ohm),
                             static_cast<int>(c.word_a), static_cast<int>(c.word_b),
                             0);

                for(int k = 0; k < kToneWinPoints; ++k)
                {
                    int         n = 0;
                    const Point p = measure_window_point(chain, c, window_cycles,
                                                         tone_win_ns(k), &n);
                    hw.PrintLine("SHELL_TONE_WIN case=%d d_before_end_ns=%d n=%d mean=%d "
                                 "min=%d max=%d",
                                 i, static_cast<int>(tone_win_ns(k)), n,
                                 p.mean, p.min, p.max);
                }
            }
        }

        XtalkSummary summary{};
        summary.b0          = b0;
        summary.lat_min_ns  = lat_min;
        summary.lat_max_ns  = lat_max;
        summary.lat_mean_ns = lat_mean;
        summary.address_ok  = address_ok;
        // worst_control_delta = 0, and that zero is NOT a measured control:
        // there is no control curve in this probe -- no chain event happens
        // anywhere in this block -- so G6 does not apply and is never
        // printed. xtalk_gates() still reads this field to compute its own
        // g6_control, which this file discards; a 0 here must not be read as
        // "a control was taken and it passed inside the criterion". It only
        // keeps the shared reducer satisfied for the three gates (G2, G4,
        // G5) this probe actually uses.
        summary.worst_control_delta = 0;

        const XtalkGates gates = xtalk_gates(summary);

        // g8=-1 means NOT EVALUATED HERE, and it is not a failure. G8 is
        // host-side because its baseline is not in this firmware: baking
        // numbers in as constants would turn a measurement into a literal
        // nobody re-measures -- which is exactly what the deleted
        // kConversionNs was. read_tone.py computes G8 and folds it into its
        // exit code; gates_ok below excludes it.
        //
        // G8 IS TWO THINGS AND ONLY ONE OF THEM GATES. The half that gates
        // compares this image's boot-virgin floor against THIS CAMPAIGN's
        // four recorded boot-virgin floors, per victim, which is a
        // comparison of a quantity against itself. The half that compares
        // against round one's silent block -- the one in xtalk.csv.meta.csv
        // -- is REPORT ONLY and carries no bound, because the two spreads
        // have the same count and different content. See read_tone.py's
        // g8() docstring.
        // phase_timeouts and block_ms were Task 4 additions to THIS line and
        // now live on SHELL_TONE_HEALTH below -- see the byte-budget note
        // ahead of the two PrintLine calls for why they moved; their
        // semantics did not. Neither is folded into gates_ok: a lost repeat
        // already shows up as a shorter n on its own SHELL_TONE line, and
        // block duration is not a pass/fail criterion, it is the one number
        // only the firmware can measure and the plan needs from Bastian as a
        // decision input.
        // phase_timeouts is g_phase_timeouts, a lifetime count, not reset
        // per block. block_ms is MEASURED per block (daisy::System::GetNow()
        // at the top of this loop iteration, subtracted here) -- not
        // derived, and not to be confused with any arithmetic estimate.
        // win_timeouts=%d is a Task 5 addition and is g_win_timeouts: the
        // window sweep's own lifetime timeout count, separate from
        // phase_timeouts and from probe_adc::timeouts() because
        // measure_window_point() polls EOC itself. It is not folded into
        // gates_ok either -- a lost repeat already shows as a shorter n on
        // its own SHELL_TONE_WIN line.
        //
        // BYTE BUDGET, and the reason the four counters are on their own
        // SHELL_TONE_HEALTH line rather than appended to SHELL_TONE_GATES:
        // the combined form was MEASURED, not estimated, at
        //     117 bytes on a healthy run,
        //     124 bytes at missed_blocks=12 phase_timeouts=4096
        //         win_timeouts=8320 -- ordinary failure-run values,
        //     144 bytes with the three counters at ten digits and a
        //         realistic block_ms (170123), and
        //     148 bytes with the three counters AND block_ms all at ten
        //         digits -- the condition matters, so it is stated rather
        //         than folded into "at full width",
        // against libDaisy's 128-byte buffer (lib/libDaisy/src/hid/logger.h:29;
        // AppendNewLine() at logger.cpp:104 only fits its 2-byte newline
        // below 126, so the usable payload is 125). The combined line
        // therefore truncated and stamped "$$" precisely in the runs whose
        // counters were large -- which is to say precisely when these four
        // fields were the ones anyone needed. A diagnostic that hides itself
        // when things go wrong is worse than no diagnostic, because its
        // silence reads as health. Split the same way, and for the same
        // buffer, as SHELL_TONE_CASE/SHELL_TONE and
        // SHELL_TONE_WINCASE/SHELL_TONE_WIN above. Measured widths of the
        // two lines below at full uint32 values: 53 and 112 bytes.
        //
        // FIELDS MOVED, NOTHING RECOMPUTED: gates_ok is still the AND of the
        // same FOUR terms -- g2_floor, g4_jitter, g5_address and
        // missed_blocks == 0, which is what g7 is, so it is one term and not
        // two; g8 is the literal -1 and has never been in it -- the three
        // counters are still lifetime counts that are not reset per block,
        // and block_ms is still the same GetNow() subtraction. See the "two different failures" note in the
        // tone-case loop above: this split, like the others, addresses only
        // the too-long-line failure and buys no immunity from
        // logger.cpp:78-87's accumulation overflow.
        hw.PrintLine("SHELL_TONE_GATES g2=%d g4=%d g5=%d g7=%d g8=%d gates_ok=%d",
                     gates.g2_floor ? 1 : 0, gates.g4_jitter ? 1 : 0,
                     gates.g5_address ? 1 : 0, missed_blocks == 0 ? 1 : 0,
                     -1,
                     (gates.g2_floor && gates.g4_jitter && gates.g5_address
                      && missed_blocks == 0) ? 1 : 0);
        hw.PrintLine("SHELL_TONE_HEALTH missed_blocks=%d phase_timeouts=%d "
                     "win_timeouts=%d block_ms=%d",
                     static_cast<int>(missed_blocks),
                     static_cast<int>(g_phase_timeouts),
                     static_cast<int>(g_win_timeouts),
                     static_cast<int>(daisy::System::GetNow() - block_start_ms));
        hw.PrintLine("SHELL_TONE_END");
    }
}

} // namespace shell
