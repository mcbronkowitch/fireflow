#include "xtalk_probe.h"

#include "coupon_expect.h"
#include "cycles.h"
#include "mux_scan.h"
#include "probe_adc.h"
#include "settle_plan.h"
#include "shell_git_hash.h"
// SHELL_XTALK_RV4 is read below and lives in the generated switch
// header beside SHELL_XTALK_PROBE, so this translation unit needs it
// directly -- main.cpp's include of it does not reach here. The
// Makefile carries the matching edge on xtalk_probe.o.
#include "shell_xtalk_probe.h"
#include "xtalk_plan.h"

namespace shell {

namespace {

// --- Step 3: the span, and G5's yardstick ---

// The four 0 R tie channels on the 4067, from netlist.py's NEIGHBOURS and
// the spare-channel loop: R_HI1 and R_HI2 to A+3V3, R_LO1 and R_LO2 to AGND.
// Two of each, because a single tie cannot tell "the rail" from "an open
// tie" -- the pair spread is the check coupon_expect.h's kTieSpread exists
// for, and reading only one would adopt a collapsed supply as the reference
// and then pass every victim against it.
constexpr int kTieHi1 = 1, kTieHi2 = 5, kTieLo1 = 3, kTieLo2 = 7;

// How long the board is left completely alone before a Silent grid starts.
//
// NOT IN THE TASK BRIEF, and here on purpose. A Silent case's whole claim is
// that nothing else on the board moved during its grid, and the case before
// it has just handed ~4.7 kB to USB-CDC. libDaisy's Transmit() queues and
// returns; the transfer drains under DMA afterwards, so without a quiet
// window the first grid points of a "silent" case would be measured with the
// previous case's traffic still in flight -- which is precisely the candidate
// settle-measured.md section 5 names and this block exists to isolate.
//
// Applied to EVERY Silent case, row 1 and row 10 alike, so the row 10 minus
// row 1 comparison is not distorted by it: it moves the starting line for
// both, and row 10 then puts its own traffic back inside the grid.
//
// 50 ms is derived, not measured: a block's widest per-case burst is about
// 4.7 kB and USB-CDC on this machine has never been measured below the
// ~100 kB/s that would make 50 ms enough. Whoever measures the drain rate
// should replace this comment with the number.
constexpr int kQuietMs = 50;

// Park on (group, ch) and take kRepeats conversions. The park is kParkNs,
// the same 20 us the settle probe uses, which is far past every prediction
// for these channels.
//
// It does NOT select the ADC channel -- the caller does, exactly as
// settle_probe.cpp's park() leaves that to run_settle_probe(). Every call
// site below says which channel is selected and when.
int32_t read_parked(MuxScan& chain, int group, int ch)
{
    chain.write_chain(chain_word(
        kCouponChain, step_pattern(kCouponChain, step_of(kCouponChain, group, ch)), 0u));
    const uint32_t t0 = cycles_now();
    while(cycles_now() - t0 < ns_to_cycles(kParkNs)) { }
    return probe_adc::mean_of_repeats(kRepeats);
}

// coupon_span()'s semantics from four reads instead of twenty-four, plus the
// two tie spreads the verdict turns on. The spreads ride in the return value
// rather than being recomputed at the call site: a G5 failure has five
// possible causes and a reader must be able to see which one gave way
// without re-deriving it from numbers that are not printed.
struct SpanRead
{
    Span    span;
    int32_t hi_spread;
    int32_t lo_spread;
};

SpanRead measure_span(MuxScan& chain)
{
    // All four ties sit on the 4067, so this one select() covers the whole
    // pass. It is the WORKING rung (16.5 cycles), not each tie's own rung
    // from sample_time_index_for().
    //
    // THE ARGUMENT IS THE PHYSICS, NOT A CITATION. The rung sets the
    // acquisition window, and what that window has to charge is the
    // sample-and-hold through the source impedance: tau_acq = (R_src +
    // R_ADC) * C_ADC. These four channels are 0 R links to A+3V3 and AGND
    // through the mux switch alone, so their tau is the shortest on the
    // board and the shortest rung already covers it many times over --
    // sample_time_index_for() itself returns the bottom rung for them, and
    // the working rung is longer still. A longer window on a node that is
    // already charged changes nothing. The dividers are the channels a rung
    // choice moves (settle-measured.md section 7: about 200 counts at
    // 5150 R), and no divider is read in this pass.
    //
    // Section 7's libDaisy ladder table is NOT the support for this. That
    // table was taken through libDaisy's own ADC configuration, and this
    // probe's SHELL_XTALK_SPAN reads the same rail tie about 2000 counts
    // higher -- two paths that disagree on the level cannot vouch for each
    // other's rung behaviour.
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
    // ALL FIVE of coupon_span()'s validity conditions, in the same order it
    // applies them (coupon_expect.cpp:64-71), and each one has a failure
    // behind it:
    //   ties agree   -- the two ties of a kind must fall inside kTieSpread,
    //                   or one open tie averages into a plausible mean
    //   rail floor   -- a collapsed supply otherwise gets adopted AS the
    //                   rail and then passes every victim judged against it
    //   zero ceiling -- a lifted AGND shifts the whole scale with it. This
    //                   one was MISSING here until the Task 5 review found
    //                   it, and its absence was not cosmetic: coupon_verdict
    //                   judges Low as `v <= span.zero + kRailMargin`, so an
    //                   inflated zero widens the very band the two 0 R
    //                   victims are tested against, and G5 self-calibrates
    //                   into a pass. It did not fire on the 2026-09-18
    //                   capture (zero=0, lo_spread=0 in every block) -- it
    //                   is here so it cannot fire unseen later.
    //   ordered      -- the rail must sit above the zero, or swapped nets
    //                   would still span, just backwards
    r.span.valid = r.hi_spread <= static_cast<int32_t>(kTieSpread)
                   && r.lo_spread <= static_cast<int32_t>(kTieSpread)
                   && rail >= static_cast<int32_t>(kRailFloor)
                   && zero <= static_cast<int32_t>(kRailMargin)
                   && rail > zero;
    return r;
}

// --- Step 5: the silent grid point ---

// A grid point plus the count of repeats that actually produced a
// conversion. Point itself (settle_plan.h) carries no such field and is not
// getting one: it is host-compiled, host-tested and shared with the settle
// probe, so the count rides beside it here instead.
struct SilentPoint
{
    Point p;
    int   valid;   // kRepeats unless a conversion timed out; 0 -> p is -1/-1/-1
};

// One grid point of a Silent case. There is no latch, so there is no t0 from
// the chain -- the repeat takes its own, spins the SAME park + d it would
// have spun in a Latch case, and converts. The cadence is identical to a
// Latch repeat minus the chain traffic, which is exactly the comparison
// wanted: the difference between this curve and the control curve is the
// shift and the pulse, and nothing else.
//
// TIMED-OUT REPEATS ARE EXCLUDED, and the span is requested for no other
// reason. probe_adc::sample_now() returns 0 on a timeout (probe_adc.cpp:236)
// and with `nullptr` passed for the span the caller cannot tell that from a
// channel reading zero -- so one timeout on a 32500-count divider would pull
// this point's mean down by about 508 counts and set its `min` to 0, and
// nothing printed would say so. probe_adc::timeouts() is a LIFETIME counter
// printed on the next block's CAL line, which leaves the last complete block
// of any capture covered by no printed counter at all. The latency pass in
// run_xtalk_probe() has always done this correctly; the grid now does too,
// and `valid` reaches the point line's own n= field so the exclusion is
// visible in the capture rather than inferred from it.
SilentPoint measure_silent_point(uint32_t d_ns)
{
    const uint32_t park_cycles = ns_to_cycles(kParkNs);
    const uint32_t d_cycles    = ns_to_cycles(d_ns);

    int64_t sum = 0;
    int32_t lo = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    int     valid_n = 0;
    for(int r = 0; r < kRepeats; ++r)
    {
        // cycles_now() - t0 on uint32_t wraps correctly with no special
        // case: the DWT counter is free-running and unsigned specifically so
        // this subtraction is always valid, even across a wrap. Do not "fix"
        // it.
        const uint32_t t0 = cycles_now();
        while(cycles_now() - t0 < park_cycles + d_cycles) { }
        uint32_t      span = 0;
        const int32_t v    = probe_adc::sample_now(&span);
        if(span == probe_adc::kTimeoutSentinel) continue;
        sum += v;
        if(v < lo) lo = v;
        if(v > hi) hi = v;
        ++valid_n;
    }
    // -1 rather than a divide by zero or the 0x7FFFFFFF init values leaking
    // into print, and valid_n dividing the sum rather than kRepeats -- the
    // same shape as the latency pass, for the same reason.
    if(valid_n == 0) return SilentPoint{Point{-1, -1, -1}, 0};
    return SilentPoint{
        Point{static_cast<int32_t>(sum / valid_n), lo, hi}, valid_n};
}

// Which victim a case names. Matched on (group, channel) rather than trusted
// from the table's running order: kXtalkPlan is 58 entries and row 1's five
// happen to arrive in victim order today, which is not a property anything
// enforces. -1 for a case that names no victim in the table, which would
// itself be a defect worth seeing rather than an array index.
int victim_index_of(const XtalkCase& c)
{
    for(int v = 0; v < kXtalkVictims; ++v)
    {
        if(kXtalkVictimTable[v].group == c.group
           && kXtalkVictimTable[v].channel == c.channel)
            return v;
    }
    return -1;
}

// Row 1's five curves, held for the whole block.
//
// Row 1 prints AFTER its grid, so its points have to live somewhere until
// then -- and holding all five rather than one is what Task 6's G6 needs:
// |mean_control(d) - mean_silent(d)| at every grid point is a difference
// against row 1, per victim, and row 2 runs after all five of row 1.
//
// At namespace scope and not on the stack: 5 * 65 * sizeof(SilentPoint) is
// about 5.2 kB, which is a large fraction of the probe's stack and none of
// the .bss it sits in here.
SilentPoint g_silent_pts[kXtalkVictims][kGridPoints];
int32_t g_silent_mean[kXtalkVictims];
bool    g_silent_seen[kXtalkVictims];

} // namespace

void run_xtalk_probe(bench::Board& hw)
{
    cycles_init();
    probe_adc::init(hw);
    hw.StartLog(false);

    // Read, not assumed: the CPU probe's own comment in main.cpp records the
    // day a block size was inferred from phase durations (48) and was in
    // fact 96. Both go into scan_settle_ns(), and both are printed so the
    // derivation is checkable from the capture alone.
    const int block_size = static_cast<int>(hw.AudioBlockSize());
    const int sr_hz      = static_cast<int>(hw.AudioSampleRate());

    // --- Step 4: the clock and the latency pass ---

    MuxScan chain;
    chain.init();

    // R_SP10: a 0 R link to AGND, so there is nothing on it to settle. Its
    // r_src_ohm reads 150 and not 0 because that field is "switch Ron plus
    // what the netlist wires" (xtalk_plan.h) -- the 150 R is the 4067's own
    // on-resistance, and the tie itself contributes nothing. "0 R tie"
    // throughout this file means the wiring, not the field.
    const XtalkVictim& v0 = kXtalkVictimTable[3];
    probe_adc::select(probe_adc::channel_of_group(v0.group));

    // A probe that cannot start must say it cannot start. THIS LINE CANNOT
    // BE THE ONLY PLACE THE THREE HAL FLAGS APPEAR, and it is not: all three
    // ride on every block's SHELL_XTALK_GATES. StartLog(false) does not wait
    // for a host, so everything printed before the forever loop goes out
    // milliseconds after reset -- settle_probe.cpp's own comment records the
    // 2026-09-18 capture in which the equivalent line did not arrive once in
    // 2730 lines. The line is kept for SWD and for a logic analyser.
    const bool warm_ok = probe_adc::warm_up();
    hw.PrintLine("SHELL_XTALK_WARMUP ok=%d init_ok=%d cal_ok=%d cfg_ok=%d",
                 warm_ok ? 1 : 0, probe_adc::init_ok() ? 1 : 0,
                 probe_adc::cal_ok() ? 1 : 0, probe_adc::cfg_ok() ? 1 : 0);

    (void)read_parked(chain, v0.group, v0.channel);   // park for the clock pass

    // The 0 R AGND tie and not a divider, for the same reason the settle
    // probe used P0: the clock pass measures spans, and a channel with
    // nothing to settle is the one whose spans carry the least of anything
    // else. Measured once, here -- only the PRINT of it is in the loop,
    // because a one-shot print goes out before Windows has enumerated the
    // CDC device.
    const probe_adc::Clock clk
        = probe_adc::measure_clock(kRepeats, probe_adc::channel_of_group(v0.group));

    while(1)
    {
        // The configuration line comes FIRST and the end marker always
        // arrives, even on a pass that measured nothing. A reader that can
        // only recognise a complete block is the point: a truncated one must
        // be discarded rather than half-believed.
        //
        // BYTE BUDGET. libDaisy's log buffer is 128 bytes
        // (lib/libDaisy/src/hid/logger.h:29). MEASURED on the board
        // 2026-09-18 with adc_khz="-1": this line was 115 characters, 117
        // with CRLF. MEASURED again on this image the same day, with the
        // real 4-digit adc_khz=6146 in it: 117 characters, 119 with CRLF,
        // 9 bytes left -- the derivation that predicted exactly that is
        // kept below so the next one can be trusted the same way. A
        // 5-digit reading would cost a tenth byte. Do not add a field to
        // it; SHELL_XTALK_GATES (76 characters measured) has room.
        //
        // AND THE LIMIT IS NOT PER LINE, which is the part that misleads.
        // logger.cpp:78-87: when impl_.Transmit() returns false because the
        // host is not draining, tx_ptr_ is deliberately NOT reset and the
        // next PrintLine's vsnprintf appends into the same 128-byte buffer.
        // The "$$" at tx_buff_[126..127] is therefore an ACCUMULATION
        // overflow across lines, not a single line that grew too long. This
        // block is tens of kilobytes, so a stalled host truncates and fuses
        // lines ANYWHERE in it; shortening this line does not buy immunity,
        // it only moves where the damage lands. That is why the reader has
        // to drop an incomplete block rather than repair one.
        hw.PrintLine("SHELL_XTALK_CFG adc_khz=%d repeats=%d grid_ns=%d "
                     "points=%d park_ns=%d scan_settle_ns=%d rv4=%d git=%s",
                     clk.measured_adc_khz, kRepeats, kGridStepNs, kGridPoints,
                     static_cast<int>(kParkNs),
                     static_cast<int>(scan_settle_ns(block_size, sr_hz)),
                     SHELL_XTALK_RV4, SHELL_GIT_HASH);
        // block_size and sr are the inputs to the field above. They ride on
        // their own line rather than in CFG because CFG has no room left,
        // and a derived duration whose inputs are not in the capture cannot
        // be checked by anyone reading it later.
        hw.PrintLine("SHELL_XTALK_RATE block_size=%d sr_hz=%d cases=%d victims=%d",
                     block_size, sr_hz, kXtalkCases, kXtalkVictims);

        // The two raw spans come back unconverted as well as converted, and
        // are printed that way every block, so the ADC-clock derivation
        // stays independently checkable off the log alone. smp_*_tenths are
        // the two rungs the clock pass ran at: the working 16.5 and the long
        // 387.5, which differ by exactly the 371 ADC cycles the difference
        // is built on.
        hw.PrintLine("SHELL_XTALK_CLK span_short_cyc=%d span_long_cyc=%d "
                     "smp_short_tenths=%d smp_long_tenths=%d",
                     clk.span_short_cyc, clk.span_long_cyc, 165, 3875);

        // The instrument measuring itself, on the same parked 0 R tie. A
        // timed-out repeat (span == kTimeoutSentinel) is excluded from every
        // reduction rather than folded in: it is not a measurement of the
        // instrument's latency, it is the instrument failing to respond, and
        // averaging it in would widen lat_max_ns into something that LOOKS
        // like jitter instead of reading as the fault it is. valid_n divides
        // the sums, not kRepeats.
        //
        // clk.ok guards the whole pass: without a measured clock there is
        // nothing correct to subtract at all, not a wrong-but-present number.
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
        // -1 rather than a divide-by-zero, a silently zeroed mean, or the
        // 0x7FFFFFFF/-0x7FFFFFFF init values leaking into print, if every
        // repeat in this block timed out (or clk.ok was false). None of
        // these fields may look like a number when it is not one.
        const bool    all_timed_out = valid_n == 0;
        const int32_t lat_mean
            = all_timed_out ? -1 : static_cast<int32_t>(lat_sum / valid_n);
        if(all_timed_out) { lat_min = -1; lat_max = -1; }
        const int32_t b0 = all_timed_out ? -1 : (val_max - val_min);  // G2's floor

        hw.PrintLine("SHELL_XTALK_CAL lat_mean_ns=%d lat_min_ns=%d "
                     "lat_max_ns=%d b0=%d timeouts=%d",
                     lat_mean, lat_min, lat_max, b0,
                     static_cast<int>(probe_adc::timeouts()));

        // --- Step 5: rows 1 and 10, the silent block ---

        for(int v = 0; v < kXtalkVictims; ++v)
        {
            g_silent_mean[v] = 0;
            g_silent_seen[v] = false;
        }

        for(int i = 0; i < kXtalkCases; ++i)
        {
            const XtalkCase& c = kXtalkPlan[i];

            // THIS TASK MEASURES THE SILENT ROWS ONLY. Rows 2 to 9 are
            // Latch, ShiftOnly and Static cases and belong to Task 6; they
            // are passed over here without a line of their own, so a capture
            // from this image contains nothing that could be mistaken for a
            // measured aggressor. The `row` field on SHELL_XTALK_CASE is
            // what tells rows 1 and 10 apart -- the point lines are
            // deliberately identical.
            if(c.kind != XtalkKind::Silent) continue;

            const int v = victim_index_of(c);
            if(v < 0) continue;   // a case naming no victim: see victim_index_of()

            // Always 0 here: row 6 is the only rv4-dependent row and every
            // one of its cases is a Latch. Computed rather than hardcoded so
            // Task 6 inherits the field already wired to its own switch.
            const bool skipped = c.needs_rv4 && !SHELL_XTALK_RV4;

            // The board left alone before the grid -- see kQuietMs.
            hw.Delay(kQuietMs);

            // Park once on word_a, select the victim's channel and the rung
            // sample_time_index_for() picks for its impedance. That rung
            // reads about 200 counts low at 5150 R in steady state
            // (settle-measured.md section 7), so no ABSOLUTE level from this
            // curve may be quoted without that qualification; every delta
            // Task 6 computes is a difference at the same rung, where the
            // bias cancels.
            chain.write_chain(c.word_a);
            probe_adc::select_time(
                probe_adc::channel_of_group(c.group),
                probe_adc::sample_time_for_rung(sample_time_index_for(c.r_src_ohm)));
            {
                const uint32_t t0 = cycles_now();
                while(cycles_now() - t0 < ns_to_cycles(kParkNs)) { }
            }

            // FROM HERE TO THE END OF THE GRID: no chain access at all, and
            // for row 1 no print either. That is the only way to read a
            // settled channel with neither the 595's traffic nor USB-CDC's
            // in the measurement, and it is the case the whole attribution
            // turns on. Row 10 is the same grid with the point line moved
            // inside it -- the difference between the two IS the USB-CDC/DMA
            // candidate settle-measured.md section 5 names.
            // BYTE BUDGET, and this is why the spec's single SHELL_XTALK
            // line is split in two here. libDaisy's log buffer is 128 bytes
            // (lib/libDaisy/src/hid/logger.h:29); the spec's combined line
            // runs about 150 and would be truncated and stamped "$$", which
            // is how settle_probe.cpp lost two fields before anyone noticed.
            // MEASURED on the board 2026-09-18 across three whole blocks:
            // this line's widest is 106 characters, 108 with CRLF, and the
            // point line below runs 66 / 68. (The plan estimated 114 and 67;
            // both were safe, and the real numbers are the ones to quote.)
            if(c.prints_inline)
            {
                hw.PrintLine("SHELL_XTALK_CASE case=%d row=%d kind=%d victim_group=%d "
                             "victim_ch=%d r_src=%d word_a=%d word_b=%d skipped=%d",
                             i, static_cast<int>(c.row), static_cast<int>(c.kind),
                             c.group, c.channel, static_cast<int>(c.r_src_ohm),
                             static_cast<int>(c.word_a), static_cast<int>(c.word_b),
                             skipped ? 1 : 0);
            }

            int64_t mean_sum      = 0;
            int32_t mean_lo       = 0x7FFFFFFF;
            int32_t mean_hi       = -0x7FFFFFFF;
            int32_t widest_band   = -1;
            int32_t widest_band_d_ns = -1;
            // Grid points that produced at least one conversion. A point
            // with none of them contributes to no statistic at all rather
            // than folding a -1 into a spread: see measure_silent_point().
            int     usable_points = 0;

            for(int k = 0; k < kGridPoints; ++k)
            {
                const SilentPoint sp = measure_silent_point(grid_ns(k));

                // Row 1's curve is held for the whole block (Task 6's G6);
                // row 10's is not, because everything it feeds is reduced on
                // the fly right here.
                if(!c.prints_inline) g_silent_pts[v][k] = sp;

                if(sp.valid > 0)
                {
                    mean_sum += sp.p.mean;
                    if(sp.p.mean < mean_lo) mean_lo = sp.p.mean;
                    if(sp.p.mean > mean_hi) mean_hi = sp.p.mean;
                    const int32_t band = sp.p.max - sp.p.min;
                    if(band > widest_band)
                    {
                        widest_band      = band;
                        widest_band_d_ns = static_cast<int32_t>(grid_ns(k));
                    }
                    ++usable_points;
                }

                if(c.prints_inline)
                {
                    // n= carries the repeats that CONVERTED, not kRepeats.
                    // It reads 64 on a clean grid and is the only place a
                    // timed-out repeat becomes visible in the capture.
                    hw.PrintLine("SHELL_XTALK case=%d d_ns=%d n=%d mean=%d min=%d max=%d",
                                 i, static_cast<int>(grid_ns(k)), sp.valid,
                                 sp.p.mean, sp.p.min, sp.p.max);
                }
            }

            // Row 1's lines, all of them, after the grid has finished. The
            // CASE line comes before the points in BOTH rows, so a reader
            // cannot tell the two apart from the line order either -- the
            // `row` field is the record.
            if(!c.prints_inline)
            {
                hw.PrintLine("SHELL_XTALK_CASE case=%d row=%d kind=%d victim_group=%d "
                             "victim_ch=%d r_src=%d word_a=%d word_b=%d skipped=%d",
                             i, static_cast<int>(c.row), static_cast<int>(c.kind),
                             c.group, c.channel, static_cast<int>(c.r_src_ohm),
                             static_cast<int>(c.word_a), static_cast<int>(c.word_b),
                             skipped ? 1 : 0);
                for(int k = 0; k < kGridPoints; ++k)
                {
                    hw.PrintLine("SHELL_XTALK case=%d d_ns=%d n=%d mean=%d min=%d max=%d",
                                 i, static_cast<int>(grid_ns(k)),
                                 g_silent_pts[v][k].valid,
                                 g_silent_pts[v][k].p.mean, g_silent_pts[v][k].p.min,
                                 g_silent_pts[v][k].p.max);
                }

                // G5 judges the floor row, not row 10: row 10 reads the same
                // channel with its own traffic on the bus, and the address
                // question deserves the quietest reading the block has.
                //
                // Divided by the points that produced a reading, not by
                // kGridPoints. A curve with no usable point at all leaves
                // g_silent_seen false, and G5 then fails that victim rather
                // than judging it against a fabricated mean.
                if(usable_points > 0)
                {
                    g_silent_mean[v] = static_cast<int32_t>(mean_sum / usable_points);
                    g_silent_seen[v] = true;
                }
            }

            // Both of settle-measured.md section 5's own statistics.
            // Reported, neither gated: spec section 7 is explicit that the
            // settled region's width is a quantity in this instrument and
            // not a gate, because gating it would refuse the very run that
            // answers the question.
            // -1, not a spread computed from the 0x7FFFFFFF init values, if
            // the whole grid came back unusable.
            const int32_t mean_spread
                = (usable_points > 0) ? (mean_hi - mean_lo) : -1;
            hw.PrintLine("SHELL_XTALK_STAT case=%d settled_mean_spread=%d "
                         "widest_sample_band=%d at_d_ns=%d",
                         i, mean_spread, widest_band, widest_band_d_ns);
        }

        // --- Step 3's verdict: the span and G5 ---
        //
        // Measured after the cases, not before: it parks the chain on four
        // tie channels, which is exactly the traffic a Silent grid must not
        // have in it.
        const SpanRead sr = measure_span(chain);
        hw.PrintLine("SHELL_XTALK_SPAN zero=%d rail=%d hi_spread=%d "
                     "lo_spread=%d valid=%d",
                     static_cast<int>(sr.span.zero),
                     static_cast<int>(sr.span.rail),
                     sr.hi_spread, sr.lo_spread, sr.span.valid ? 1 : 0);

        bool address_ok = true;
        for(int v = 0; v < kXtalkVictims; ++v)
        {
            const XtalkVictim& vv = kXtalkVictimTable[v];
            const Expect e = coupon_expect(step_of(kCouponChain, vv.group, vv.channel));
            // g_silent_seen[v] is the "no floor curve for this victim at
            // all" case. It cannot happen from the table as it stands, and
            // it must not read as a pass if the table ever changes.
            const bool ok = g_silent_seen[v] && sr.span.valid
                            && coupon_verdict(e, static_cast<uint16_t>(g_silent_mean[v]),
                                              sr.span);
            if(!ok) address_ok = false;
            hw.PrintLine("SHELL_XTALK_G5 victim_group=%d victim_ch=%d "
                         "expect=%d mean=%d ok=%d",
                         vv.group, vv.channel, static_cast<int>(e),
                         g_silent_mean[v], ok ? 1 : 0);
        }

        XtalkSummary summary{};
        summary.b0          = b0;
        summary.lat_min_ns  = lat_min;
        summary.lat_max_ns  = lat_max;
        summary.lat_mean_ns = lat_mean;
        summary.address_ok  = address_ok;
        // G6 PASSES VACUOUSLY IN THIS IMAGE AND ITS g6=1 IS NOT A VERDICT
        // ABOUT A CONTROL CURVE. This block takes no control curve at all --
        // row 2 is a Latch case and belongs to Task 6 -- so there is no
        // |mean_control(d) - mean_silent(d)| to reduce and the field is 0,
        // which xtalk_gates() reads as "inside the criterion". The gate
        // itself is not broken: its RED was proved on the host in Task 3.
        // Nobody may quote this image's g6 as evidence about the control.
        summary.worst_control_delta = 0;

        const XtalkGates gates = xtalk_gates(summary);

        hw.PrintLine("SHELL_XTALK_GATES g2=%d g4=%d g5=%d g6=%d init_ok=%d "
                     "cal_ok=%d cfg_ok=%d gates_ok=%d",
                     gates.g2_floor ? 1 : 0, gates.g4_jitter ? 1 : 0,
                     gates.g5_address ? 1 : 0, gates.g6_control ? 1 : 0,
                     probe_adc::init_ok() ? 1 : 0, probe_adc::cal_ok() ? 1 : 0,
                     probe_adc::cfg_ok() ? 1 : 0, gates.ok() ? 1 : 0);
        hw.PrintLine("SHELL_XTALK_END");
        // No inter-block delay. Task 4's skeleton ended its loop with
        // hw.Delay(1000), which existed only to keep an empty block from
        // spinning at full speed; this block measures for over a second and
        // that delay is gone. The cadence is now set by the work itself --
        // ten grids of 65 points x 64 repeats plus the ten kQuietMs windows,
        // MEASURED 2026-09-18 at about 1.8 s per block (2500 lines, 3.6
        // blocks, 6.6 s of capture). Do not add one back to "settle" the
        // host: the reader accumulates to SHELL_XTALK_END and discards an
        // incomplete block, which is the mechanism that handles a slow host.
    }
}

} // namespace shell
