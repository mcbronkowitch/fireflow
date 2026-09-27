#include "wait_probe.h"

#include "coupon_expect.h"
#include "cycles.h"
#include "mux_plan.h"
#include "mux_scan.h"
#include "probe_adc.h"
#include "settle_plan.h"
#include "shell_git_hash.h"
#include "wait_plan.h"
#include "xtalk_plan.h"

namespace shell {

// wait_plan.h carries the core clock as a literal because cycles.h cannot be
// compiled on the host. This is where the copy is held to the original.
static_assert(kWaitCoreMhz == kCoreMhz,
              "wait_plan.h's kWaitCoreMhz must equal cycles.h's kCoreMhz");

namespace {

// --- The callback: arm C's codec, and nothing else ---
//
// Writes zeros and counts blocks. Round two's RunningSilent operating point,
// which is the one its silent-cadence arm ran in; no tone, no phase
// accumulator, because nothing in this probe waits on a phase. The DWT stamp
// round two's callback publishes is not needed either: G7 below counts
// blocks against elapsed foreground time, which only needs the count.
volatile uint32_t g_blocks = 0;

void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                   daisy::AudioHandle::OutputBuffer out,
                   size_t                           size)
{
    (void)in;
    for(size_t i = 0; i < size; ++i)
    {
        out[0][i] = 0.0f;
        out[1][i] = 0.0f;
    }
    ++g_blocks;
}

inline void spin_ns(uint32_t ns)
{
    const uint32_t t0 = cycles_now();
    while(cycles_now() - t0 < ns_to_cycles(ns)) { }
}

// Writes the chain word that selects (group, ch) with nothing else
// aggressing -- the same construction as tone_probe.cpp's and
// xtalk_probe.cpp's: chain_word(kCouponChain, step_pattern(...), 0).
inline void write_victim_word(MuxScan& chain, int group, int ch)
{
    chain.write_chain(chain_word(
        kCouponChain, step_pattern(kCouponChain, step_of(kCouponChain, group, ch)), 0u));
}

// --- G5's span pass, copied from tone_probe.cpp (itself copied from
// xtalk_probe.cpp). IT IS THE SAME PASS: a later change to one of the three
// copies is a defect in whichever copies did not get it, not a fork. The
// known gap tone_probe.cpp documents on read_parked() -- mean_of_repeats()
// folds a timed-out repeat's 0 into the mean uncounted -- is carried over
// unchanged for the same reason. ---

constexpr int kTieHi1 = 1, kTieHi2 = 5, kTieLo1 = 3, kTieLo2 = 7;

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

// --- The arms ---

// The sampling rung an arm reads a victim at, as an index into
// kSamplingLadderTenths: the long rung for arm L, the victim's own
// impedance-picked working rung for every other arm -- which is what
// park_victim() in both earlier probes picks.
int rung_for(WaitArm arm, const XtalkVictim& v)
{
    return arm == WaitArm::Long ? kWaitLongRung : sample_time_index_for(v.r_src_ohm);
}

// Park a victim for a case: its chain word, its ADC channel at the arm's
// rung, then the settle spin. The spin is round two's kParkNs, 20 us, far
// past every settle time settle-measured.md section 4 measured.
void park_victim(MuxScan& chain, const XtalkVictim& v, int rung)
{
    write_victim_word(chain, v.group, v.channel);
    probe_adc::select_time(probe_adc::channel_of_group(v.group),
                           probe_adc::sample_time_for_rung(rung));
    spin_ns(kParkNs);
}

// One grid point: kWaitRepeats repeats of
//
//     prime -> idle W from the prime's end -> [discard] -> measure
//
// on whatever channel and rung are already selected. Spec section 3.
//
// THE PRIME IS WHAT MAKES W MEAN SOMETHING. Without it the conversion before
// the measured one would be whatever the previous repeat, grid point or
// victim left behind, at an unknown distance in time. With it, every
// measured conversion follows one on the same channel at the same rung,
// exactly W earlier.
//
// THE WAIT RUNS WITH INTERRUPTS ENABLED; only the conversions mask them
// (probe_adc::sample_now()). Arm C's callback must keep the codec fed, and a
// 50 ms masked region would also starve USB-CDC. An interrupt landing inside
// the wait lengthens W by its own duration; that is not subtracted and not
// measured -- spec section 9 classes it as reasoned, not measured.
//
// W is timed from the moment sample_now() RETURNS, i.e. after the prime's
// EOC and DR read, which is the end of the prime conversion to within the
// return path's few cycles. W = 0 skips the wait loop entirely: back to back.
//
// A timed-out MEASURED conversion is excluded and shows as a shorter n. A
// timed-out prime or discard is NOT excluded -- the measured conversion after
// it is still taken -- and probe_adc::timeouts(), printed on
// SHELL_WAIT_HEALTH, is the lifetime count that says it happened.
Point measure_wait_point(uint32_t wait_us, bool discard, int* n_out)
{
    const uint32_t wait_cycles = us_to_cycles(wait_us);

    int64_t sum   = 0;
    int32_t lo    = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    int     valid = 0;
    for(int r = 0; r < kWaitRepeats; ++r)
    {
        (void)probe_adc::sample_now(nullptr);   // prime
        const uint32_t t_end = cycles_now();
        while(cycles_now() - t_end < wait_cycles) { }
        if(discard) (void)probe_adc::sample_now(nullptr);

        uint32_t      span = 0;
        const int32_t v    = probe_adc::sample_now(&span);
        if(span == probe_adc::kTimeoutSentinel) continue;
        sum += v;
        if(v < lo) lo = v;
        if(v > hi) hi = v;
        ++valid;
    }
    if(n_out) *n_out = valid;
    return valid > 0 ? Point{static_cast<int32_t>(sum / valid), lo, hi}
                     : Point{-1, -1, -1};
}

// Arm A's W = 0 mean per victim, which G5 judges -- the working rung, back to
// back, codec stopped: the reading closest to the level both earlier probes
// judged their address verdict on. -1/false when that point converted
// nothing at all.
int32_t g_wait_zero_mean[kXtalkVictims];
bool    g_wait_zero_seen[kXtalkVictims];

uint32_t g_block_index = 0;

} // namespace

void run_wait_probe(bench::Board& hw)
{
    cycles_init();
    probe_adc::init(hw);
    hw.StartLog(false);

    // Read, not assumed -- main.cpp's CPU probe carries the comment about the
    // day a block size was inferred as 48 and was in fact 96.
    const int block_size = static_cast<int>(hw.AudioBlockSize());
    const int sr_hz      = static_cast<int>(hw.AudioSampleRate());

    MuxScan chain;
    chain.init();

    // The clock and latency reference channel: kXtalkVictimTable[3], R_SP10,
    // the 0 R tie to AGND on the 4067 -- both earlier probes' choice, for the
    // same reason: a channel with nothing to settle carries the least of
    // anything else into the spans.
    const XtalkVictim& v0 = kXtalkVictimTable[3];
    probe_adc::select(probe_adc::channel_of_group(v0.group));
    (void)probe_adc::warm_up();
    (void)read_parked(chain, v0.group, v0.channel);

    const probe_adc::Clock clk
        = probe_adc::measure_clock(kRepeats, probe_adc::channel_of_group(v0.group));

    while(1)
    {
        // Wall-clock ms from the 1 ms SysTick, not the DWT counter: a block
        // runs ~90 s and the 32-bit DWT count wraps every ~8.9 s.
        const uint32_t block_start_ms = daisy::System::GetNow();
        const int      sweep_dir      = static_cast<int>(g_block_index & 1u);

        hw.PrintLine("SHELL_WAIT_CFG adc_khz=%d repeats=%d points=%d "
                     "block_size=%d sr=%d sweep_dir=%d git=%s",
                     clk.measured_adc_khz, kWaitRepeats, kWaitPoints,
                     block_size, sr_hz, sweep_dir, SHELL_GIT_HASH);
        hw.PrintLine("SHELL_WAIT_CLK span_short_cyc=%d span_long_cyc=%d "
                     "smp_short_tenths=%d smp_long_tenths=%d",
                     clk.span_short_cyc, clk.span_long_cyc, 165, 3875);

        // --- The latency pass: tone_probe.cpp's, unchanged ---
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

        hw.PrintLine("SHELL_WAIT_CAL lat_mean_ns=%d lat_min_ns=%d "
                     "lat_max_ns=%d b0=%d timeouts=%d",
                     lat_mean, lat_min, lat_max, b0,
                     static_cast<int>(probe_adc::timeouts()));

        for(int v = 0; v < kXtalkVictims; ++v)
        {
            g_wait_zero_mean[v] = -1;
            g_wait_zero_seen[v] = false;
        }
        uint32_t missed_blocks = 0;

        // --- The four arms, arm-major. case = arm * kXtalkVictims + victim.
        for(int a = 0; a < kWaitArmCount; ++a)
        {
            const WaitArm arm   = static_cast<WaitArm>(a);
            const bool    codec = arm == WaitArm::Codec;

            // Arm C is the only arm with the codec running. Started once for
            // the arm, not per victim; hw.Delay(20) lets it settle -- ten
            // blocks at 96/48 kHz, the same margin round two gives a new row.
            if(codec)
            {
                hw.StartAudio(AudioCallback);
                hw.Delay(20);
            }

            for(int v = 0; v < kXtalkVictims; ++v)
            {
                const XtalkVictim& vv       = kXtalkVictimTable[v];
                const int          case_idx = a * kXtalkVictims + v;
                const int          rung     = rung_for(arm, vv);

                // BYTE BUDGET: ~105 bytes at data-widest values, against
                // libDaisy's 125-byte payload (logger.h:29). The point line
                // below runs ~60. Split the same way as round two's
                // SHELL_TONE_CASE/SHELL_TONE, for the same buffer.
                hw.PrintLine("SHELL_WAIT_CASE case=%d arm=%d victim_group=%d "
                             "victim_ch=%d r_src=%d rung_tenths=%d codec=%d",
                             case_idx, a, vv.group, vv.channel,
                             static_cast<int>(vv.r_src_ohm),
                             kSamplingLadderTenths[rung], codec ? 1 : 0);

                park_victim(chain, vv, rung);

                const int      n_points  = codec ? kWaitCodecPoints : kWaitPoints;
                const uint32_t case_t0   = cycles_now();
                const uint32_t blocks_t0 = g_blocks;

                for(int k = 0; k < n_points; ++k)
                {
                    const int      idx = wait_grid_order(k, n_points, sweep_dir);
                    const uint32_t w   = codec ? kWaitCodecGridUs[idx] : kWaitGridUs[idx];
                    int            n   = 0;
                    const Point    p   = measure_wait_point(w, arm == WaitArm::Discard, &n);
                    hw.PrintLine("SHELL_WAIT case=%d w_us=%d n=%d mean=%d min=%d max=%d",
                                 case_idx, static_cast<int>(w), n, p.mean, p.min, p.max);

                    if(arm == WaitArm::Wait && w == 0u && p.mean >= 0)
                    {
                        g_wait_zero_mean[v] = p.mean;
                        g_wait_zero_seen[v] = true;
                    }
                }

                // --- G7, the callback health gate, arm C only ---
                //
                // The callback's own count against what the elapsed DWT time
                // and the block size say it should have been, with a
                // tolerance of one block -- round two's rule, because the two
                // clocks are read at slightly different instants. A case is
                // 3 points x 64 repeats x at most ~10 ms, ~0.72 s, well
                // inside the DWT's ~8.9 s wrap. The PrintLine calls inside
                // the case are counted in the elapsed time too, and the
                // callback keeps running through them, so they cost nothing
                // here.
                if(codec)
                {
                    const uint32_t expected
                        = static_cast<uint32_t>((static_cast<uint64_t>(cycles_now() - case_t0)
                                                 * static_cast<uint64_t>(sr_hz))
                                                / (static_cast<uint64_t>(kCoreMhz) * 1000000ull
                                                   * static_cast<uint64_t>(block_size)));
                    const uint32_t seen   = g_blocks - blocks_t0;
                    const int32_t  missed = static_cast<int32_t>(expected)
                                           - static_cast<int32_t>(seen);
                    if(missed > 1) missed_blocks += static_cast<uint32_t>(missed);
                }
            }

            if(codec) hw.StopAudio();
        }

        // --- G5's span and per-victim verdict, codec stopped ---
        const SpanRead sp = measure_span(chain);
        hw.PrintLine("SHELL_WAIT_SPAN zero=%d rail=%d hi_spread=%d "
                     "lo_spread=%d valid=%d",
                     static_cast<int>(sp.span.zero), static_cast<int>(sp.span.rail),
                     sp.hi_spread, sp.lo_spread, sp.span.valid ? 1 : 0);

        bool address_ok = true;
        for(int v = 0; v < kXtalkVictims; ++v)
        {
            const XtalkVictim& vv = kXtalkVictimTable[v];
            const Expect e = coupon_expect(step_of(kCouponChain, vv.group, vv.channel));
            const bool ok = g_wait_zero_seen[v] && sp.span.valid
                            && coupon_verdict(e, static_cast<uint16_t>(g_wait_zero_mean[v]),
                                              sp.span);
            if(!ok) address_ok = false;
            hw.PrintLine("SHELL_WAIT_G5 victim_group=%d victim_ch=%d "
                         "expect=%d mean=%d ok=%d",
                         vv.group, vv.channel, static_cast<int>(e),
                         g_wait_zero_mean[v], ok ? 1 : 0);
        }

        XtalkSummary summary{};
        summary.b0          = b0;
        summary.lat_min_ns  = lat_min;
        summary.lat_max_ns  = lat_max;
        summary.lat_mean_ns = lat_mean;
        summary.address_ok  = address_ok;
        // 0 is NOT a measured control: there is no control curve in this
        // probe, so G6 does not apply and is never printed. Same note as
        // tone_probe.cpp -- it only keeps the shared reducer satisfied for
        // the three gates (G2, G4, G5) used here.
        summary.worst_control_delta = 0;
        const XtalkGates gates = xtalk_gates(summary);

        // g9=-1 means NOT EVALUATED HERE, not a failure: G9's bound comes
        // from round two's published captures, and read_wait.py computes it.
        hw.PrintLine("SHELL_WAIT_GATES g2=%d g4=%d g5=%d g7=%d g9=%d gates_ok=%d",
                     gates.g2_floor ? 1 : 0, gates.g4_jitter ? 1 : 0,
                     gates.g5_address ? 1 : 0, missed_blocks == 0 ? 1 : 0, -1,
                     (gates.g2_floor && gates.g4_jitter && gates.g5_address
                      && missed_blocks == 0) ? 1 : 0);
        hw.PrintLine("SHELL_WAIT_HEALTH missed_blocks=%d timeouts=%d block_ms=%d",
                     static_cast<int>(missed_blocks),
                     static_cast<int>(probe_adc::timeouts()),
                     static_cast<int>(daisy::System::GetNow() - block_start_ms));
        hw.PrintLine("SHELL_WAIT_END");

        ++g_block_index;
    }
}

} // namespace shell
