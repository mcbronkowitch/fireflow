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
        // sinf() and not a table: this callback has 96 samples of a 2 ms
        // block to fill and nothing else to do, so the cost is irrelevant --
        // and a table would put its own interpolation error into the
        // aggressor, which is the one signal in this measurement that has to
        // be what it says it is.
        const float s = sinf(6.2831853f * (static_cast<float>(phase)
                                           / static_cast<float>(kTonePhaseScale)));
        const float v = a * s;
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
LevelResult measure_level(bench::Board& hw, MuxScan& chain, int block_size, int sr_hz,
                          int case_index, ToneLevel level, const XtalkVictim& v,
                          uint32_t* missed_blocks)
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

    // n= carries kToneRepeats, the per-SUB-MEASUREMENT repeat count -- the
    // brief's own line for this, matching the field settle_mean_spread's
    // peak-to-peak is built from. mean/min/max are the whole level's, over
    // all 65 x 64 conversions -- a different scope from n=, exactly as
    // SHELL_XTALK's point line and SHELL_XTALK_STAT's spread differ in
    // scope in round one.
    hw.PrintLine("SHELL_TONE_LEVEL case=%d level=%d victim_group=%d "
                 "victim_ch=%d r_src=%d n=%d mean=%d min=%d max=%d",
                 case_index, static_cast<int>(level), v.group, v.channel,
                 static_cast<int>(v.r_src_ohm), kToneRepeats,
                 result.p.mean, result.p.min, result.p.max);
    hw.PrintLine("SHELL_TONE_STAT case=%d settled_mean_spread=%d "
                 "widest_sample_band=%d",
                 case_index, result.settled_mean_spread, result.widest_sample_band);

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

    while(1)
    {
        // The configuration line: block_size and sr are READ from the board,
        // never assumed -- see the comment above. adc_khz is the measured
        // clock, not the nominal one.
        hw.PrintLine("SHELL_TONE_CFG adc_khz=%d repeats=%d block_size=%d sr=%d git=%s",
                     clk.measured_adc_khz, kToneRepeats, block_size, sr_hz,
                     SHELL_GIT_HASH);

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

            // Stopped: the codec idle. This is round one's silent block
            // repeated in this image, and it is what G8 checks -- an image
            // whose floor is not round one's floor has had something moved
            // by the refactor or the linker, and every tone result in it
            // would be plausible and against the wrong baseline.
            hw.StopAudio();
            (void)measure_level(hw, chain, block_size, sr_hz, case_idx,
                                ToneLevel::Stopped, vv, nullptr);
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
                                ToneLevel::RunningSilent, vv, &missed_blocks);
            ++case_idx;

            if(running.p.mean >= 0)   // -1 means no repeat converted at all
            {
                g_running_silent_mean[v] = running.p.mean;
                g_running_silent_seen[v] = true;
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
        hw.PrintLine("SHELL_TONE_GATES g2=%d g4=%d g5=%d g7=%d g8=%d "
                     "missed_blocks=%d gates_ok=%d",
                     gates.g2_floor ? 1 : 0, gates.g4_jitter ? 1 : 0,
                     gates.g5_address ? 1 : 0, missed_blocks == 0 ? 1 : 0,
                     -1, static_cast<int>(missed_blocks),
                     (gates.g2_floor && gates.g4_jitter && gates.g5_address
                      && missed_blocks == 0) ? 1 : 0);
        hw.PrintLine("SHELL_TONE_END");
    }
}

} // namespace shell
