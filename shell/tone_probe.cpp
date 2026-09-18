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
// on SHELL_TONE_GATES below: a Task 4 addition, appended after the design
// spec's own field list for the same reason audio_virgin was appended to
// SHELL_TONE_LEVEL/STAT in Task 3 (see print_level_lines()'s comment) -- a
// reader keyed on the spec's names and positions is unaffected by a field
// added at the end.
uint32_t g_phase_timeouts = 0;

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
// ITS SHAPE IS ROUND ONE'S SILENT CURVE AND NOT AN ARBITRARY RUN, and that
// is G8's whole premise. Round one's settled_mean_spread is the peak-to-peak
// of 65 means of 64 conversions each; this takes exactly the same -- 65
// sub-measurements of kToneRepeats conversions -- so the two are the same
// statistic and the 4-count bound compares like with like. A shorter run
// here would make G8 a comparison between two differently-shaped spreads,
// which is a bound with no meaning behind it.
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

                if(row.f_hz == 0)
                {
                    // The static row: a tone row by table position (f_hz ==
                    // 0, present only when Task 1 measured a DC-coupled
                    // output, kToneHasStaticRow) but a LEVEL row by SHAPE:
                    // it has no phase, so it is never walked as a 16-point
                    // grid. It takes the same whole-block measurement the
                    // Stopped/RunningSilent levels do -- measure_level(),
                    // which parks the victim itself -- printed as
                    // SHELL_TONE_LEVEL/SHELL_TONE_STAT with level=2 (Tone).
                    // A reader that assumed every level=2 row carried a
                    // phase grid would refuse this block on the one row
                    // that never has one. missed_blocks is tracked here
                    // (not nullptr): audio is running, same as
                    // RunningSilent, so G7 applies -- see measure_level()'s
                    // own comment on this parameter, "and Tone in Task 4".
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

                // BYTE BUDGET, and the reason the spec's single SHELL_TONE
                // line is split in two here: libDaisy's log buffer is 128
                // bytes (lib/libDaisy/src/hid/logger.h:29) and the spec's
                // combined line runs about 130 at its widest values --
                // truncated, and stamped "$$". Same split and same reason
                // as the crosstalk probe's SHELL_XTALK_CASE. This line runs
                // 108 bytes; the point line below runs 62.
                hw.PrintLine("SHELL_TONE_CASE case=%d level=%d f_hz=%d dbfs=%d "
                             "victim_group=%d victim_ch=%d r_src=%d below_corner=%d",
                             case_idx, static_cast<int>(ToneLevel::Tone), row.f_hz,
                             row.dbfs, vv.group, vv.channel,
                             static_cast<int>(vv.r_src_ohm), row.below_corner ? 1 : 0);

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

        // g8=-1 means NOT EVALUATED HERE, and it is not a failure. G8
        // compares this image's floor against round one's silent block, and
        // round one's numbers are in xtalk.csv.meta.csv, not in this
        // firmware. Baking them in as constants would turn a measurement
        // into a literal nobody re-measures -- which is exactly what the
        // deleted kConversionNs was. read_tone.py computes G8 from the two
        // files and folds it into its exit code; gates_ok below excludes it.
        // phase_timeouts=%d is a Task 4 addition, appended after the design
        // spec's field list (section 8 predates the phase grid) rather than
        // inserted between existing fields -- same convention as
        // audio_virgin on SHELL_TONE_LEVEL/STAT. Lifetime count from
        // g_phase_timeouts, not reset per block; not folded into gates_ok,
        // because a lost repeat already shows up as a shorter n on its own
        // SHELL_TONE line and is not itself a gate.
        hw.PrintLine("SHELL_TONE_GATES g2=%d g4=%d g5=%d g7=%d g8=%d "
                     "missed_blocks=%d gates_ok=%d phase_timeouts=%d",
                     gates.g2_floor ? 1 : 0, gates.g4_jitter ? 1 : 0,
                     gates.g5_address ? 1 : 0, missed_blocks == 0 ? 1 : 0,
                     -1, static_cast<int>(missed_blocks),
                     (gates.g2_floor && gates.g4_jitter && gates.g5_address
                      && missed_blocks == 0) ? 1 : 0,
                     static_cast<int>(g_phase_timeouts));
        hw.PrintLine("SHELL_TONE_END");
    }
}

} // namespace shell
