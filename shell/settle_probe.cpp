#include "settle_probe.h"

#include "cycles.h"
#include "mux_scan.h"
#include "probe_adc.h"
#include "settle_plan.h"

namespace shell {

namespace {

// Task 5 fix round 3, item 2: how many of the LAST grid points are averaged
// into the settled reference d_settle_index() is judged against. Value is 8,
// same as settle_plan.h's kSettleCounts -- but the two are NOT the same
// constant wearing two names: kSettleCounts is a THRESHOLD in raw ADC
// counts (half an LSB of 12 bit), kTailWindowPoints is a WINDOW SIZE in
// grid points. They happen to share a numeral today; a future change to
// either must not assume the other moves with it, so they stay two
// separate names.
constexpr int kTailWindowPoints = 8;

// Fix round 4, item 1: there used to be a kConversionNs = 2034 here --
// "16.5 sampling + 8.5 conversion = 25 ADC cycles, and at 12.29 MHz that is
// 2034 ns." SHELL_SETTLE_CLK (below) measured the real ADC clock at
// 6.146 MHz, exactly half of that assumption -- one prescaler step -- so
// 2034 ns under-subtracted by 2034 ns, and that shortfall was the entire
// "latency" fix rounds 1-3 spent three rounds chasing out of software that
// was never the cause. A number this probe can measure must not also exist
// as a literal: run_settle_probe() below now computes the working sampling
// pass's conversion time (and every pair's offset) from THIS boot's
// measured core-cycles-per-ADC-cycle ratio instead. See task-4-report.md's
// fix-round-4 section for the derivation and the board numbers.

void park(MuxScan& chain, bench::Board& hw, int group, int ch)
{
    chain.write_chain(chain_word(kCouponChain,
                                 step_pattern(kCouponChain, step_of(kCouponChain, group, ch)),
                                 0u));
    hw.Delay(1);
}

// --- Task 5: the sweep ---
//
// One grid point: park on the pair's from_ch for kParkNs, latch to to_ch
// taking t0 from the 595 chain's own latch edge, spin until d_ns has
// elapsed, convert. Repeated kRepeats times; mean/min/max come back for
// d_settle_index() and for G2/G3's floor and band.
//
// No bench::Board& parameter (brief's step 1 pseudocode carried one and
// never used it) -- every wait in here spins on the DWT counter via
// cycles_now(), not on anything the board handle owns.
Point measure_point(MuxScan& chain, const SettlePair& sp, uint32_t d_ns)
{
    const uint32_t park_word
        = chain_word(kCouponChain,
                     step_pattern(kCouponChain, step_of(kCouponChain, sp.group, sp.from_ch)), 0u);
    const uint32_t test_word
        = chain_word(kCouponChain,
                     step_pattern(kCouponChain, step_of(kCouponChain, sp.group, sp.to_ch)), 0u);
    const uint32_t d_cycles = ns_to_cycles(d_ns);
    const uint32_t park_cycles = ns_to_cycles(kParkNs);

    int64_t sum = 0;
    int32_t lo = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    for(int r = 0; r < kRepeats; ++r)
    {
        chain.write_chain(park_word);
        // Named park_t0, not p0: run_settle_probe() below has its own p0 --
        // kSettlePlan[0], a SettlePair -- and this function must not read as
        // sharing it even though the two never actually collide (separate
        // function scopes, no shadowing).
        const uint32_t park_t0 = cycles_now();
        while(cycles_now() - park_t0 < park_cycles) { }

        const uint32_t t0 = chain.write_chain_timed(test_word);
        // t = 0 is the 595 latch edge write_chain_timed() hands back, not the
        // call site -- see its own doc comment. `cycles_now() - t0` on
        // uint32_t wraps correctly with no special case needed: the DWT
        // counter is free-running and unsigned specifically so this
        // subtraction is always valid, even across a wrap. Do not "fix" it.
        while(cycles_now() - t0 < d_cycles) { }

        // nullptr: nothing in this function reads the per-repeat span (that
        // came from the brief's own pseudocode, not a defect introduced
        // here) -- the aperture-jitter measurement that DOES need spans is
        // the lat pass in run_settle_probe(), on the parked reference.
        const int32_t v = probe_adc::sample_now(nullptr);
        sum += v;
        if(v < lo) lo = v;
        if(v > hi) hi = v;
    }
    return Point{static_cast<int32_t>(sum / kRepeats), lo, hi};
}

} // namespace

void run_settle_probe(bench::Board& hw)
{
    cycles_init();
    probe_adc::init(hw);
    hw.StartLog(false);

    MuxScan chain;
    chain.init();

    const SettlePair& p0 = kSettlePlan[0];
    probe_adc::select(probe_adc::channel_of_group(p0.group));

    // A probe that cannot start must say it cannot start. This is the first
    // line printed after "Daisy is online" (StartLog's own banner), before
    // the calibration or the forever loop, so a warm-up stall is visible
    // even though it would otherwise leave the board silent -- fix round 2,
    // item 1. ok=0 does not stop the rest of this function: every later ADC
    // access is bounded the same way, so the run continues and reports
    // whatever it can (see probe_adc::timeouts() in every SHELL_SETTLE_CAL
    // line) rather than reaching a second unbounded wait.
    //
    // init_ok/cal_ok/cfg_ok are the three discarded HAL statuses (see the
    // flags' own comment above probe_adc::init()).
    //
    // THIS LINE CANNOT BE THE ONLY PLACE THEY APPEAR, and it is not: all
    // three are reprinted on every block's SHELL_SETTLE_GATES line. The
    // 2026-09-18 capture is why -- SHELL_SETTLE_WARMUP did not arrive once
    // in 2730 lines of USB-CDC. StartLog(false) does not wait for a host, so
    // everything printed before the forever loop goes out milliseconds after
    // reset, long before Windows finishes enumerating the CDC device; this is
    // the same trap that moved SHELL_SETTLE_CLK's print into the loop in fix
    // round 3, and resetting the board to catch it does not work either,
    // because the reset re-enumerates the device and kills the open handle.
    // The line is kept anyway: over SWD or a logic analyser it is the
    // earliest signal the probe gives, and it costs one PrintLine.
    //
    // cfg_ok here also covers only the configuration calls made BEFORE this
    // line -- the single probe_adc::select() just above -- because the
    // per-pair rung selections happen inside the sweep. That is a second,
    // independent reason the in-loop copy is the one to read.
    const bool adc_warm_ok = probe_adc::warm_up();
    hw.PrintLine("SHELL_SETTLE_WARMUP ok=%d init_ok=%d cal_ok=%d cfg_ok=%d",
                 adc_warm_ok ? 1 : 0, probe_adc::init_ok() ? 1 : 0,
                 probe_adc::cal_ok() ? 1 : 0, probe_adc::cfg_ok() ? 1 : 0);

    park(chain, hw, p0.group, p0.from_ch);

    // The ADC-clock calibration pass. It measured 6.146 MHz where
    // settle-budget.md derived 12.29 MHz, and everything this file prints in
    // nanoseconds is built on it -- see probe_adc.h, which carries the pass
    // itself and the argument for every field it returns.
    //
    // Measured exactly once, here -- re-running the 387.5-cycle pass every
    // block would cost real time to keep reprinting a constant. Only the
    // PRINT of the spans moves into the forever loop below (fix round 3):
    // hw.StartLog(false) does not wait for a host, so a one-shot print here
    // goes out within milliseconds of boot, long before Windows finishes
    // re-enumerating the USB-CDC device after flashing -- the line existed
    // and was correct but nobody could ever read it, the same failure shape
    // as a probe that scans once and reprints a frozen buffer forever. Do
    // not move this back out.
    const probe_adc::Clock clk
        = probe_adc::measure_clock(kRepeats, probe_adc::channel_of_group(p0.group));
    const bool    has_clk               = clk.ok;
    const int32_t working_conversion_ns = clk.working_conversion_ns;
    const int32_t measured_adc_khz      = clk.measured_adc_khz;

    // Task 5's per-pair sweep, below, reuses rung_idx[]/offset_ns[] computed
    // right here rather than recomputing them: "true settle = d + offset"
    // needs this pair's offset, and offset_ns is printed on its own, beside
    // SHELL_SETTLE_CAL, so a reader can check the arithmetic without
    // trusting SHELL_SETTLE_KNEE's at_or_below_offset flag blind. The rung
    // comes from sample_time_index_for() (settle_plan.h/.cpp), the same
    // pure, host-tested function the sweep uses to select each pair's
    // sampling time; -1 comes back for every pair when has_clk is false.
    int32_t rung_idx[kSettlePairs];
    int32_t offset_ns[kSettlePairs];
    for(int p = 0; p < kSettlePairs; ++p)
    {
        rung_idx[p]  = sample_time_index_for(kSettlePlan[p].r_src_ohm);
        offset_ns[p] = probe_adc::offset_ns_for_rung(clk, rung_idx[p]);
    }

    // Calibration (scope change from the brief's step 7): park permanently on
    // BOTH ends of P0's step and take kRepeats conversions at each, instead
    // of temporarily parking on to_ch, reflashing, reading and restoring.
    // from_ch is tied to A+3V3 through 0 ohms, to_ch to AGND through 0 ohms,
    // so this alone answers step 1's channel-number question: the right
    // constants put cal_from near full scale and cal_to near zero; wrong
    // ones put them equal, or both at some implausible value.
    //
    // Already parked on from_ch above (for the clock-calibration pass), so
    // cal_from's park is not repeated here.
    const int32_t cal_from = probe_adc::mean_of_repeats(kRepeats);

    park(chain, hw, p0.group, p0.to_ch);
    const int32_t cal_to = probe_adc::mean_of_repeats(kRepeats);

    // Park back on from_ch -- P0's settled reference -- for the latency pass
    // that follows, forever.
    park(chain, hw, p0.group, p0.from_ch);

    // Fix round 5: alternates the per-pair sweep's own visit order every
    // block, so two consecutive blocks can answer whether a slow tail is
    // settling or wall-clock drift (round 2's pre/post pair proved drift
    // EXISTS -- pair 2 moved 29 counts between reads seconds apart -- but
    // not how much of any one curve's shape it accounts for). Declared
    // outside while(1) because the alternation has to survive across
    // iterations, not reset every block; see SHELL_SETTLE_CFG's sweep_dir
    // field and the per-pair sweep below for where it is read and flipped.
    bool sweep_ascending = true;

    while(1)
    {
        // The sweep below (Task 5) reconfigures the ADC's channel and
        // sampling time per pair and leaves the mux parked on whichever
        // grid point it measured last. Put both back to P0's own from_ch at
        // the working sampling time before anything else this iteration
        // does -- the instrument-measuring-itself pass a few lines down has
        // to see the exact same node every single iteration, or lat_mean_ns
        // stops meaning "the instrument's own latency" and starts meaning
        // "whatever the previous sweep happened to visit last". A no-op on
        // the very first iteration (already parked there by the setup
        // above); cheap (hw.Delay(1) against a sweep costing ~0.3 s) on
        // every iteration after.
        probe_adc::select(probe_adc::channel_of_group(p0.group));
        park(chain, hw, p0.group, p0.from_ch);

        // Fix round 5: this block's own sweep direction, captured into a
        // const so the CFG line below, the per-pair sweep further down, and
        // anyone reasoning about this block later all agree on one value --
        // flipped immediately after capture so alternation applies to the
        // NEXT block, not this one.
        const bool ascending_this_block = sweep_ascending;
        sweep_ascending                 = !sweep_ascending;

        // The configuration line comes FIRST and the end marker always
        // arrives, even on a pass that measured nothing. A reader that can
        // only recognise a complete block is the point: a truncated one must
        // be discarded rather than half-believed.
        //
        // sample_cycles=165 is ADC_SAMPLETIME_16CYCLES_5 expressed in tenths,
        // so it stays an integer for %d -- PrintLine() is the lightweight
        // printf and the existing probes stay on %d for that reason.
        //
        // adc_khz now prints the MEASURED value (fix round 4, item 4) --
        // computed once at startup from SHELL_SETTLE_CLK's own two spans,
        // the same measurement that overturned the previously assumed
        // 12.29 MHz (this line printed a bare "12290" literal through fix
        // round 3; the real clock is 6.146 MHz, exactly half). -1 means
        // has_clk was false -- the clock-calibration pass itself came back
        // invalid and nothing downstream of it should be trusted either.
        //
        // sweep_dir (fix round 5): 0 = ascending (d small -> large, the only
        // order this probe has ever swept in), 1 = descending (d large ->
        // small). Two consecutive blocks, one of each, are what let a
        // reader tell settling from wall-clock drift apart -- see the
        // per-pair sweep below for how the reversed order is made exactly
        // as fresh a measurement as the forward one.
        hw.PrintLine("SHELL_SETTLE_CFG sample_cycles=165 adc_khz=%d "
                     "repeats=%d grid_step_ns=%d grid_points=%d park_ns=%d "
                     "sweep_dir=%d",
                     measured_adc_khz, kRepeats, kGridStepNs, kGridPoints,
                     static_cast<int>(kParkNs), ascending_this_block ? 0 : 1);

        // Printed every pass, beside SHELL_SETTLE_CAL, even though
        // clk.span_short_cyc/clk.span_long_cyc were measured once at
        // startup and never change (fix round 3): hw.StartLog(false) does
        // not wait for a host, so the one-shot print this replaced went out
        // within milliseconds of boot and no reader could ever open the port
        // in time -- confirmed absent from 223 captured lines across a normal
        // run and 110 seconds across a deliberate RESET. A measurement
        // nobody can observe is not a measurement. Do not move this back to
        // a one-shot print before the loop.
        hw.PrintLine("SHELL_SETTLE_CLK span_short_cyc=%d span_long_cyc=%d "
                     "smp_short_tenths=%d smp_long_tenths=%d",
                     clk.span_short_cyc, clk.span_long_cyc, 165, 3875);

        // Fix round 4, item 3: printed every pass, beside SHELL_SETTLE_CAL,
        // for the same observability reason SHELL_SETTLE_CLK moved into the
        // loop in fix round 3 -- offset_ns[]/rung_idx[] were computed once,
        // above, since none of it changes boot to boot. This line establishes
        // the offset half of "true settle = d + offset"; the sweep below
        // (Task 5) prints the other half in SHELL_SETTLE_KNEE, with its own
        // at_or_below_offset flag for the pairs whose true settle falls
        // below what's printed here.
        for(int p = 0; p < kSettlePairs; ++p)
        {
            hw.PrintLine("SHELL_SETTLE_OFFSET pair=%d offset_ns=%d smp_tenths=%d",
                         p, offset_ns[p], kSamplingLadderTenths[rung_idx[p]]);
        }

        // The instrument measuring itself. Parked and fully settled, so the
        // only thing that varies between these conversions is the
        // instrument.
        //
        // A timed-out repeat (span == probe_adc::kTimeoutSentinel) is
        // excluded from every one of these reductions rather than folded in:
        // it is not a measurement of the instrument's latency, it is the
        // instrument failing to respond, and averaging that in would silently
        // widen lat_max_ns into something that LOOKS like jitter instead of
        // reading as the fault it is. valid_n divides the sums, not kRepeats,
        // for the same reason.
        int32_t lat_min = 0x7FFFFFFF, lat_max = -0x7FFFFFFF;
        int64_t lat_sum = 0;
        int32_t val_min = 0x7FFFFFFF, val_max = -0x7FFFFFFF;
        int     valid_n = 0;
        // has_clk guards the whole pass: kConversionNs is gone (fix round 4,
        // item 1), so a bad clock measurement now means there is nothing
        // correct to subtract at all, not a wrong-but-present number. The
        // valid_n == 0 path below already prints -1 for exactly this shape
        // of "nothing usable this block".
        if(has_clk)
        {
            for(int i = 0; i < kRepeats; ++i)
            {
                uint32_t span = 0;
                const int32_t v = probe_adc::sample_now(&span);
                if(span == probe_adc::kTimeoutSentinel) continue;
                const int32_t l =
                    static_cast<int32_t>(cycles_to_ns(span)) - working_conversion_ns;
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
        // repeat in this block timed out (or has_clk was false). gates_ok
        // comes from the sweep below now (Task 5), not a hardcoded 0 -- but
        // none of these fields may look like a number when they are not one.
        const bool    all_timed_out = valid_n == 0;
        const int32_t lat_mean = all_timed_out ? -1 : static_cast<int32_t>(lat_sum / valid_n);
        if(all_timed_out) { lat_min = -1; lat_max = -1; }
        const int32_t b0 = all_timed_out ? -1 : (val_max - val_min);  // G2's measured noise floor

        // --- Task 5: the sweep ---
        //
        // Streamed, not buffered (Task 5 fix round 1, item 1): each
        // SHELL_SETTLE line prints the instant its own point is measured,
        // inside the loop below. Only the CURRENT pair's kGridPoints points
        // (a few hundred bytes) are ever kept at once -- d_settle_index()
        // needs the whole curve to find "the start of the final settled
        // run" (a forward scan alone cannot do that), but never more than
        // one pair's worth. This task's first round buffered all 6 pairs'
        // points so SHELL_SETTLE_CAL's gates_ok could print before the
        // per-point lines; that print order was itself the bug (a dispatch
        // error, not the brief), so the fix is printing CAL, the knees, the
        // gates and the end marker only AFTER the sweep below, matching the
        // brief -- not carrying the whole sweep in memory to satisfy a print
        // order that should not have existed.
        RunSummary summary{};
        summary.b0                  = b0;
        summary.lat_min_ns          = lat_min;
        summary.lat_max_ns          = lat_max;
        summary.lat_mean_ns         = lat_mean;
        summary.settled_mean_spread = 0;

        int32_t d_settle_ns_print[kSettlePairs];
        bool    at_or_below_offset[kSettlePairs];
        // Fix round 3, item 1: the parked reference is demoted to a printed
        // cross-check (item 2 below explains why) but is still kept, pre
        // and post, so a reader can see it disagree with the curve's own
        // tail when it does.
        int32_t settled_raw_pre[kSettlePairs];
        int32_t settled_raw_post[kSettlePairs];
        // The actual yardstick now: mean of the last kTailWindowPoints grid
        // points (fix round 3, item 2), and their own spread -- see below.
        int32_t tail_ref[kSettlePairs];
        int32_t tail_spread[kSettlePairs];
        // Fix round 4: the raw per-sample band no longer gates anything
        // (see the G3 comment below), but it is not thrown away -- kept per
        // pair, with the grid position of its widest point, purely as a
        // printed observation (SHELL_SETTLE_BAND). -1 where the pair has no
        // settled region to observe (idx < 0).
        int32_t widest_band_counts[kSettlePairs];
        int32_t widest_band_d_ns[kSettlePairs];
        // Final-review I2: the per-pair settled-region mean spread -- the
        // statistic G3 is decided on. summary.settled_mean_spread is the MAX
        // across pairs and is consumed by settle_gates() without ever being
        // printed, so the only way to say which pair carried it was to
        // recompute it by hand from the SHELL_SETTLE rows. A number a
        // document wants to quote has to leave the board printed. -1 where
        // the pair has no settled region (idx < 0), same convention as
        // widest_band_* above.
        int32_t settled_mean_spread[kSettlePairs];

        for(int p = 0; p < kSettlePairs; ++p)
        {
            const SettlePair& sp = kSettlePlan[p];

            // This pair's OWN rung (amendment 4), not the working sampling
            // time.
            // rung_idx[p] came from sample_time_index_for() at startup and is
            // reused here, not recomputed (amendment 4).
            probe_adc::select_time(probe_adc::channel_of_group(sp.group),
                                   probe_adc::sample_time_for_rung(rung_idx[p]));

            // The PARKED reference for THIS pair (PRE), read at the end of a
            // long park on to_ch. Fix round 3, item 2: this is no longer
            // what d_settle_index() is judged against -- round 2 printed it
            // and round 3's board data showed it moving run to run (pair 2's
            // knee: 2600, then 11600, then 2800) while the curves themselves
            // stayed reproducible. The yardstick was the noisy part. Kept
            // here, and printed on SHELL_SETTLE_REF below, purely as a
            // cross-check: a large disagreement between this parked read and
            // the curve's own tail (see tail_ref below) is itself a finding
            // a reader must be able to see, which is why it is not deleted.
            chain.write_chain(chain_word(
                kCouponChain,
                step_pattern(kCouponChain, step_of(kCouponChain, sp.group, sp.to_ch)), 0u));
            const uint32_t s0_pre = cycles_now();
            while(cycles_now() - s0_pre < ns_to_cycles(kParkNs)) { }
            const int32_t settled_pre = probe_adc::sample_now(nullptr);

            // This pair only, overwritten by the next one -- see the
            // "streamed, not buffered" comment above.
            //
            // Fix round 5: visited ascending or descending depending on
            // ascending_this_block, but always stored (and printed) at its
            // TRUE grid index `i` -- d_settle_index(), the tail-window mean
            // and the settled-region band all depend on index order
            // matching commanded-delay order, not visit order, and none of
            // that changes with this round.
            //
            // No extra guarantee is needed to make the reversed pass "as
            // fresh" as the forward one: measure_point() itself re-parks on
            // from_ch and re-transitions to to_ch from scratch on EVERY
            // SINGLE REPEAT (see its own doc comment), independent of
            // whatever point was measured immediately before it. Calling it
            // with d_ns=12800 first and d_ns=200 last leaves no more shared
            // state between those two calls than the forward order does --
            // there is nothing here that could leak.
            Point pts[kGridPoints];
            for(int k = 0; k < kGridPoints; ++k)
            {
                const int i = ascending_this_block ? k : (kGridPoints - 1 - k);
                pts[i]      = measure_point(chain, sp, grid_ns(i));
                hw.PrintLine("SHELL_SETTLE pair=%d sense=%d from=%d to=%d d_ns=%d "
                             "n=%d mean=%d min=%d max=%d",
                             p, sp.group, sp.from_ch, sp.to_ch,
                             static_cast<int>(grid_ns(i)), kRepeats,
                             pts[i].mean, pts[i].min, pts[i].max);
            }

            // A second parked read (POST), after the sweep -- kept from fix
            // round 2 as a cross-check too, same reasoning as PRE above.
            chain.write_chain(chain_word(
                kCouponChain,
                step_pattern(kCouponChain, step_of(kCouponChain, sp.group, sp.to_ch)), 0u));
            const uint32_t s0_post = cycles_now();
            while(cycles_now() - s0_post < ns_to_cycles(kParkNs)) { }
            const int32_t settled_post = probe_adc::sample_now(nullptr);

            settled_raw_pre[p]  = settled_pre;
            settled_raw_post[p] = settled_post;

            // The settled reference itself (fix round 3, item 2): the mean
            // of the last kTailWindowPoints grid points, not a separate
            // parked read. It is measured (satisfies the spec's "the target
            // is measured, not assumed"), and it comes from the SAME sweep
            // under the SAME conditions every other point in the curve was
            // taken under -- which the parked read, arriving from whatever
            // the previous pair left the mux on and read under a single
            // fixed 20 us park, does not share.
            int32_t tail_min = 0x7FFFFFFF, tail_max = -0x7FFFFFFF;
            int64_t tail_sum = 0;
            for(int i = kGridPoints - kTailWindowPoints; i < kGridPoints; ++i)
            {
                tail_sum += pts[i].mean;
                if(pts[i].mean < tail_min) tail_min = pts[i].mean;
                if(pts[i].mean > tail_max) tail_max = pts[i].mean;
            }
            tail_ref[p]    = static_cast<int32_t>(tail_sum / kTailWindowPoints);
            tail_spread[p] = tail_max - tail_min;

            // The guard fix round 3, item 2 asks for: if the tail itself
            // has not converged (its own spread exceeds the same 8-count
            // criterion d_settle_index() uses), the pair has not settled by
            // the last grid point, full stop -- regardless of what
            // d_settle_index() returns when compared against a reference
            // computed from that same unconverged tail. Without this, a
            // curve that is still moving at the end of the grid could
            // average its own moving tail into a "settled" answer nothing
            // ever measured. tail_settled gates idx to -1 in that case; it
            // does NOT touch summary.settled_mean_spread's own idx>=0 guard
            // below, which independently already excludes an unsettled
            // pair's transient from G3.
            const bool tail_settled = tail_spread[p] <= kSettleCounts;
            const int  idx_raw      = d_settle_index(pts, kGridPoints, tail_ref[p]);
            const int  idx          = tail_settled ? idx_raw : -1;
            summary.knee_ns[p] = (idx < 0) ? -1 : static_cast<int32_t>(grid_ns(idx));

            // idx == 0 means the FIRST grid point (0 ns commanded delay) was
            // already inside the band: the true settle instant -- d + this
            // pair's own offset_ns, the SHELL_SETTLE_OFFSET line above -- is
            // at or below the offset, i.e. faster than this instrument's own
            // zero point can resolve. That is not "0 ns settle time"; the
            // node did not settle instantaneously, the instrument simply
            // cannot see anything faster than its own aperture closing.
            // d_settle_ns prints -1 (impossible as a real duration, the same
            // sentinel already used for "never settled") whenever idx <= 0,
            // and at_or_below_offset is the only field that then tells the
            // two apart: idx == 0 is a PASS (that collapse to grid point
            // zero is G1's entire job, section 7), idx < 0 across the WHOLE
            // grid means the pair never settled at all. A reader who
            // ignores the flag still cannot mistake either -1 for a
            // measured time, and one who reads it cannot conflate the two
            // very different reasons behind it. Confirmed against the
            // coupon board (fix round 1): pair 0 and pair 5, the two
            // reference pairs, both came back idx == 0.
            at_or_below_offset[p] = (idx == 0);
            d_settle_ns_print[p]  = (idx > 0) ? static_cast<int32_t>(grid_ns(idx)) : -1;

            // G3 (fix round 4): counted only from this pair's own knee
            // onward, never the transient before it -- same reason as
            // always (a pair with no knee, idx < 0, contributes nothing;
            // its failure to settle already shows up in knee_ns). What
            // changed is WHAT gets measured over that region.
            //
            // settled_mean_spread is max-min of the per-point MEANS -- the
            // statistic d_settle_index() actually decided the knee on. See
            // RunSummary::settled_mean_spread's comment in settle_plan.h
            // for why this replaced a raw-per-sample-band gate that was
            // refusing perfectly good knees on single-point outliers whose
            // means barely moved.
            //
            // widest_band_counts/widest_band_d_ns are that SAME raw
            // per-sample band this gate used to bound, computed the same
            // way (max - min of the 64 individual conversions at a point),
            // kept as a printed observation with its grid position rather
            // than deleted -- a wide single-point sample spread is still
            // informative (pair 0, a 0 ohm AGND tie with nothing to settle,
            // showed a 106-count one: real interference, not settling),
            // it is just not a reason to block a knee the mean already
            // cleared.
            if(idx >= 0)
            {
                int32_t mean_min = 0x7FFFFFFF, mean_max = -0x7FFFFFFF;
                int32_t widest_sample_band    = 0;
                int32_t widest_sample_band_ns = -1;
                for(int i = idx; i < kGridPoints; ++i)
                {
                    if(pts[i].mean < mean_min) mean_min = pts[i].mean;
                    if(pts[i].mean > mean_max) mean_max = pts[i].mean;

                    const int32_t band = pts[i].max - pts[i].min;
                    if(band > widest_sample_band)
                    {
                        widest_sample_band    = band;
                        widest_sample_band_ns = static_cast<int32_t>(grid_ns(i));
                    }
                }
                const int32_t mean_spread = mean_max - mean_min;
                if(mean_spread > summary.settled_mean_spread)
                    summary.settled_mean_spread = mean_spread;

                settled_mean_spread[p] = mean_spread;
                widest_band_counts[p]  = widest_sample_band;
                widest_band_d_ns[p]    = widest_sample_band_ns;
            }
            else
            {
                settled_mean_spread[p] = -1;
                widest_band_counts[p]  = -1;
                widest_band_d_ns[p]    = -1;
            }
        }

        const Gates gates = settle_gates(summary);

        hw.PrintLine("SHELL_SETTLE_CAL lat_mean_ns=%d lat_min_ns=%d "
                     "lat_max_ns=%d b0=%d cal_from=%d cal_to=%d timeouts=%d "
                     "gates_ok=%d",
                     lat_mean, lat_min, lat_max, b0, cal_from, cal_to,
                     static_cast<int>(probe_adc::timeouts()), gates.ok() ? 1 : 0);

        // The block prints the knees whether or not the gates passed, beside
        // the gates -- it does not suppress numbers, it labels them. A
        // reader that sees gates_ok=0 must not quote a single d_settle_ns;
        // read_settle.py (Task 6) enforces that.
        //
        // Fix round 3, item 1: this used to be ONE line carrying
        // settled_raw_pre/settled_raw_post/residual_counts too, and it
        // silently exceeded libDaisy's 128-byte log buffer
        // (lib/libDaisy/src/hid/logger.h:29, LOGGER_BUFFER) -- vsnprintf()
        // truncated it and Logger::TransmitBuf() (logger.cpp:67-69) stamped
        // the last two bytes "$$" as its own overflow marker, so
        // settled_raw_post and residual_counts never reached the board log
        // at all. Split across two lines below; neither may grow back to
        // where it needs this comment to remember why not to re-merge them.
        // A reader who sees a line end in "$$" is looking at exactly this
        // failure mode, on whichever line hit it.
        for(int p = 0; p < kSettlePairs; ++p)
        {
            hw.PrintLine("SHELL_SETTLE_KNEE pair=%d d_settle_ns=%d at_or_below_offset=%d "
                         "predicted_ns=%d reference=%d",
                         p, d_settle_ns_print[p], at_or_below_offset[p] ? 1 : 0,
                         static_cast<int>(kSettlePlan[p].tau9_ns),
                         kSettlePlan[p].is_reference ? 1 : 0);
        }

        // The cross-check line (fix round 3, item 2): tail_ref/tail_spread
        // are the actual yardstick now (tail_spread is exactly the guard's
        // own criterion value, kept next to tail_ref rather than reduced
        // further, since it IS the number the guard compares against
        // kSettleCounts). settled_raw_pre/settled_raw_post are the demoted
        // parked reads, printed so a large disagreement against tail_ref is
        // visible instead of silently discarded -- round 2's own board data
        // is the reason: pair 1's parked read sat 852 counts from its own
        // curve's tail while the curve itself was quiet to 3-4 counts.
        for(int p = 0; p < kSettlePairs; ++p)
        {
            hw.PrintLine("SHELL_SETTLE_REF pair=%d tail_ref=%d tail_spread=%d "
                         "settled_raw_pre=%d settled_raw_post=%d",
                         p, tail_ref[p], tail_spread[p],
                         settled_raw_pre[p], settled_raw_post[p]);
        }

        // Fix round 4: the raw per-sample band G3 used to gate is now an
        // observation only -- printed with the grid position (d_ns) of its
        // widest point, not folded into a single "widest of the run"
        // number, because WHICH point and WHICH pair is exactly what makes
        // an outlier like pair 0's 106-count one (a 0 ohm AGND tie that
        // should read a flat zero) interesting rather than just noise.
        // -1/-1 for a pair with no settled region to observe (idx < 0).
        //
        // settled_mean_spread rides here (final-review I2) rather than on
        // SHELL_SETTLE_REF, whose own comment above forbids growing it back:
        // it overflowed the 128-byte log buffer once already and was split
        // for it. This line is the roomy one -- literal 80 bytes and four
        // fields, so even three int32 extremes leave it inside the buffer,
        // where SHELL_SETTLE_CAL has only a handful of bytes to spare. It is
        // also the right neighbour: both fields are settled-region
        // observations over the same index range, one on the means and one
        // on the raw samples.
        for(int p = 0; p < kSettlePairs; ++p)
        {
            hw.PrintLine("SHELL_SETTLE_BAND pair=%d widest_sample_band_counts=%d "
                         "at_d_ns=%d settled_mean_spread=%d",
                         p, widest_band_counts[p], widest_band_d_ns[p],
                         settled_mean_spread[p]);
        }

        // cfg_ok, init_ok and cal_ok are the three HAL statuses of
        // final-review I5. None of them is a fifth gate: they are NOT part of
        // Gates::ok(), they do not enter settle_gates(), and they change no
        // verdict. They ride HERE, inside the loop, for the reason the
        // 2026-09-18 capture proved the hard way: all three were printed on
        // SHELL_SETTLE_WARMUP at boot, and that line did not appear once in
        // 2730 captured lines. StartLog(false) does not wait for a host, so a
        // boot-time line goes out milliseconds after reset, long before
        // Windows finishes enumerating the CDC device -- the same trap
        // SHELL_SETTLE_CLK was moved into this loop to escape in fix round 3,
        // and a deliberate reset does not help because it re-enumerates the
        // device and kills the open handle. A status nobody can read is not a
        // status. The boot line is kept (it still reaches SWD or a logic
        // analyser) but it may never be the only place these appear.
        //
        // This line and not SHELL_SETTLE_CAL: CAL is within a few bytes of
        // the 128-byte log buffer already, and this one runs about 66 bytes
        // with all seven fields.
        hw.PrintLine("SHELL_SETTLE_GATES g1=%d g2=%d g3=%d g4=%d cfg_ok=%d "
                     "init_ok=%d cal_ok=%d",
                     gates.g1_knee ? 1 : 0, gates.g2_floor ? 1 : 0,
                     gates.g3_band ? 1 : 0, gates.g4_jitter ? 1 : 0,
                     probe_adc::cfg_ok() ? 1 : 0, probe_adc::init_ok() ? 1 : 0,
                     probe_adc::cal_ok() ? 1 : 0);
        hw.PrintLine("SHELL_SETTLE_END");

        // Sweep state (last pair's channel/sampling time, mux parked on its
        // last grid point) is NOT restored here: the top of the next
        // iteration does it, right before that iteration's own lat pass
        // needs it (probe_adc::select(probe_adc::channel_of_group(p0.group))
        // + park() there).
        // Restoring in two places would only be two places to keep in sync.
        hw.Delay(1000);
    }
}

} // namespace shell
