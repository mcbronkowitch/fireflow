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

// --- The one reducer every conversion in this file goes through ---
//
// MOVED TO THE TOP OF THE FILE by the Task 7 round-one fixes, from below
// measure_span(), for one reason: read_parked() now returns a CountedPoint.
// Nothing about these three definitions changed in the move.

// A grid point plus the count of repeats that actually produced a
// conversion. Point itself (settle_plan.h) carries no such field and is not
// getting one: it is host-compiled, host-tested and shared with the settle
// probe, so the count rides beside it here instead.
//
// Named for what it is rather than for the row that first needed it. Task 5
// called this SilentPoint when Silent was the only kind this file measured;
// Latch, ShiftOnly and Static all carry the same count now, for the same
// reason -- see measure_silent_point().
struct CountedPoint
{
    Point p;
    int   valid;   // kRepeats unless a conversion timed out; 0 -> p is -1/-1/-1
};

// The reduction of kRepeats conversions into one point, written ONCE because
// Task 6 gives it four call sites -- five since Task 7 routed read_parked()
// through it too. "A timed-out repeat is excluded and the survivors are
// counted" has to hold identically at all of them, or the n= field stops
// meaning the same thing from one printed line to the next -- and n= is the
// only place in the capture where a timeout becomes visible.
struct RepeatAccum
{
    int64_t sum = 0;
    int32_t lo  = 0x7FFFFFFF;
    int32_t hi  = -0x7FFFFFFF;
    int     n   = 0;

    CountedPoint finish() const
    {
        // -1 rather than a divide by zero or the 0x7FFFFFFF init values
        // leaking into print, and n dividing the sum rather than kRepeats --
        // the same shape as the latency pass, for the same reason.
        if(n == 0) return CountedPoint{Point{-1, -1, -1}, 0};
        return CountedPoint{Point{static_cast<int32_t>(sum / n), lo, hi}, n};
    }
};

// One conversion, folded in unless it timed out.
//
// probe_adc::sample_now() returns 0 on a timeout (probe_adc.cpp:236) and with
// `nullptr` passed for the span the caller cannot tell that from a channel
// reading zero -- so one timeout on a 32500-count divider would pull a point's
// mean down by about 508 counts and set its `min` to 0, with nothing printed
// to say so. probe_adc::timeouts() is a LIFETIME counter printed on the NEXT
// block's CAL line, which leaves the last complete block of any capture
// covered by no printed counter at all.
void take_one(RepeatAccum& acc)
{
    uint32_t      span = 0;
    const int32_t v    = probe_adc::sample_now(&span);
    if(span == probe_adc::kTimeoutSentinel) return;
    acc.sum += v;
    if(v < acc.lo) acc.lo = v;
    if(v > acc.hi) acc.hi = v;
    ++acc.n;
}

// --- Step 3: the span, and G5's yardstick ---

// The four 0 R tie channels on the 4067, from netlist.py's NEIGHBOURS and
// the spare-channel loop: R_HI1 and R_HI2 to A+3V3, R_LO1 and R_LO2 to AGND.
// Two of each, because a single tie cannot tell "the rail" from "an open
// tie" -- the pair spread is the check coupon_expect.h's kTieSpread exists
// for, and reading only one would adopt a collapsed supply as the reference
// and then pass every victim against it.
constexpr int kTieHi1 = 1, kTieHi2 = 5, kTieLo1 = 3, kTieLo2 = 7;

// How long the board is left completely alone before a case starts.
//
// NOT IN THE TASK BRIEF, and here on purpose. A Silent case's whole claim is
// that nothing else on the board moved during its grid, and the case before
// it has just handed ~4.7 kB to USB-CDC. libDaisy's Transmit() queues and
// returns; the transfer drains under DMA afterwards, so without a quiet
// window the first grid points of a "silent" case would be measured with the
// previous case's traffic still in flight -- which is precisely the candidate
// settle-measured.md section 5 names and this block exists to isolate.
//
// APPLIED TO EVERY CASE IN TASK 6, not only the Silent ones it was introduced
// for, and the reason is G6. G6 is |mean_control(d) - mean_silent(d)|, i.e. a
// difference between a Latch case (row 2) and a Silent case (row 1) that is
// supposed to isolate the shift and the pulse. If row 1 started from a quiet
// board and row 2 started with the previous case's USB burst still draining,
// that difference would carry the pre-roll as well as the event, and G6 would
// be gating something it does not name. The same argument makes every
// aggressor row's delta against row 2 a difference between two identical
// starting conditions. Cost: 54 * 50 ms = 2.7 s per block -- 54 and not 58,
// because a skipped case takes its `continue` before any hw.Delay() -- out of
// a block MEASURED 2026-09-18 at 9.07 s.
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
//
// TASK 7 CLOSED THE KNOWN GAP THAT STOOD HERE. This used to be the one
// conversion path in this file that went through
// probe_adc::mean_of_repeats(), which folds a timed-out repeat's 0 into the
// sum and divides by `repeats` like any other reading, with nothing in the
// return value to say a repeat was lost (probe_adc.h:53-56). It now runs the
// same RepeatAccum/take_one() reduction as every other conversion here, so a
// timeout is excluded from the mean and counted in `valid`, and the count
// reaches the caller.
//
// probe_adc::mean_of_repeats() itself is deliberately NOT changed: it is used
// elsewhere and its documented behaviour is relied on. The header is the
// authority, not the name -- and note that mean_span_of_repeats() beside it
// DOES exclude timeouts, which is precisely why reading the names is not a
// substitute for reading the header.
CountedPoint read_parked(MuxScan& chain, int group, int ch)
{
    chain.write_chain(chain_word(
        kCouponChain, step_pattern(kCouponChain, step_of(kCouponChain, group, ch)), 0u));
    const uint32_t t0 = cycles_now();
    while(cycles_now() - t0 < ns_to_cycles(kParkNs)) { }
    RepeatAccum acc;
    for(int r = 0; r < kRepeats; ++r) take_one(acc);
    return acc.finish();
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
    // Task 7: what the four tie reads lost, so a loss is a printed number
    // rather than an absence. `lost` is the total repeats excluded across all
    // four ties (0 on a clean pass, 4 * kRepeats if the ADC answered nothing
    // at all); `worst_valid` is the smallest surviving count on any ONE tie,
    // because "four lost, one from each tie" and "four lost, all from the
    // same tie" are different events and the total cannot tell them apart.
    int32_t lost;
    int32_t worst_valid;
};

// How many repeats a tie read may lose and still be used. ZERO -- the span is
// refused unless all four ties converted all kRepeats.
//
// THIS IS NOT AN ACCURACY ARGUMENT. take_one() excludes a timed-out repeat
// rather than folding its 0 in, so a mean over the survivors is unbiased and
// 63 of 64 repeats would be plenty of precision against kTieSpread (328
// counts). The argument is about what a timeout on THESE four channels means.
// They are 0 R links to A+3V3 and AGND through the mux switch alone -- the
// lowest-impedance, quietest nodes this instrument ever reads, and the ones
// it reads last, after the whole table has run and the board has been left
// alone for kQuietMs. An ADC that cannot complete a conversion there is not
// meeting a difficult channel; it is misbehaving, and the span is the
// yardstick every victim in the block is then judged against.
//
// The asymmetry is what settles it. G5 is judged directly against this span
// and, unlike G6, read_xtalk.py does not recompute it -- nothing downstream
// catches a span that was wrong. Meanwhile the block repeats forever with no
// inter-block delay, so refusing a span costs ONE block (MEASURED 9.07 s,
// 2026-09-18) and prints lost= and n_min= saying exactly why.
//
// COST IF THIS IS WRONG, i.e. if it is too strict: a single spurious timeout
// anywhere in the 4 * kRepeats conversions of the span pass invalidates the
// span and fails G5 for all five victims, throwing away an otherwise good
// block. That failure is loud, correctly labelled and self-explaining on the
// SPAN line, and the next block is 9 s away. The opposite error -- tolerating
// losses -- yields a plausible span and a silent G5 pass, which is the shape
// this project treats as worst because it does not look like an error at all.
// Refusing too much is visible; refusing too little is not.
//
// Whoever loosens this must keep the invariant below it: a tie that lost
// EVERY repeat has no mean at all and must never reach the rail/zero
// arithmetic, whatever the threshold is.
constexpr int kMaxTieLoss = 0;

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

    // TASK 7 CLOSED THE KNOWN GAP THAT STOOD HERE. These four reads used to
    // go through probe_adc::mean_of_repeats(), the one conversion path in
    // this file that never got Task 5's timeout exclusion: it folds a
    // timed-out repeat's 0 into the mean with no way for the caller to
    // detect it (probe_adc.h:53-56). read_parked() now runs the same
    // RepeatAccum/take_one() reduction as every other conversion here.
    //
    // WHY IT MATTERED, kept because it is the argument for the refusal
    // below and not a historical note. The failure was asymmetric. A timeout
    // on a HI tie pulled that tie's mean down by ~1024 counts per lost
    // repeat, which mostly failed closed: it either blew hi_spread past
    // kTieSpread or dragged rail below kRailFloor, and the span came back
    // invalid. A timeout on an AGND (LO) tie was invisible instead -- the
    // true reading is already ~0, and a zeroed repeat looks the same as a
    // good one, so it could pass a genuinely bad AGND tie as valid.
    //
    // Excluding alone would not have been a fix. It removes the bias and
    // leaves the loss unsaid, which moves the silence rather than removing
    // it -- so the span is REFUSED when a tie lost more than kMaxTieLoss
    // repeats (see there for the threshold and what it costs), and lost= and
    // n_min= ride on the SHELL_XTALK_SPAN line every block so a clean pass
    // is distinguishable from a lucky one.
    //
    // G5 is judged directly against this span, and unlike G6 (which the
    // reader recomputes independently in read_xtalk.py) there is no
    // host-side check on G5 -- if this pass is wrong, nothing downstream
    // catches it. That is why this one refuses rather than reports.
    //
    // Not exercised on the 2026-09-18 capture: timeouts=0 on all five CAL
    // lines. So the refusal path is UNMEASURED on hardware -- the numbers
    // above are the old path's arithmetic, not a reading of the new one.
    const CountedPoint hi1 = read_parked(chain, 0, kTieHi1);
    const CountedPoint hi2 = read_parked(chain, 0, kTieHi2);
    const CountedPoint lo1 = read_parked(chain, 0, kTieLo1);
    const CountedPoint lo2 = read_parked(chain, 0, kTieLo2);

    SpanRead r{};
    r.lost = 4 * kRepeats
             - (hi1.valid + hi2.valid + lo1.valid + lo2.valid);
    r.worst_valid = hi1.valid;
    if(hi2.valid < r.worst_valid) r.worst_valid = hi2.valid;
    if(lo1.valid < r.worst_valid) r.worst_valid = lo1.valid;
    if(lo2.valid < r.worst_valid) r.worst_valid = lo2.valid;

    // A tie that lost EVERY repeat has no mean at all: finish() returns -1,
    // which is not a level and must never enter the arithmetic below. Its
    // spread would be a difference of a sentinel and its rail a plausible
    // half of one, and the whole defect this fix closes is a tie read that
    // comes back looking like a measurement. So this returns before any of
    // it happens, with zero/rail at 0 and both spreads at -1: nothing in the
    // printed line is then a number that could be read as a reading.
    //
    // Unreachable while kMaxTieLoss is 0 -- the threshold below already
    // refuses far less than this. It is here because the threshold is a
    // judgement someone may revisit and this invariant is not.
    if(r.worst_valid == 0)
    {
        r.hi_spread  = -1;
        r.lo_spread  = -1;
        r.span.valid = false;
        return r;
    }

    r.hi_spread = (hi1.p.mean > hi2.p.mean) ? (hi1.p.mean - hi2.p.mean)
                                            : (hi2.p.mean - hi1.p.mean);
    r.lo_spread = (lo1.p.mean > lo2.p.mean) ? (lo1.p.mean - lo2.p.mean)
                                            : (lo2.p.mean - lo1.p.mean);

    const int32_t rail = (hi1.p.mean + hi2.p.mean) / 2;
    const int32_t zero = (lo1.p.mean + lo2.p.mean) / 2;
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
    //
    // AND A SIXTH, WHICH IS NOT coupon_span()'s: no tie may have lost more
    // than kMaxTieLoss repeats. It is first in the expression because it is
    // the condition on whether the other five were computed from four whole
    // readings at all, and last in coupon_span()'s list because coupon_span()
    // is handed raw values and never sees a repeat.
    r.span.valid = r.lost <= kMaxTieLoss
                   && r.hi_spread <= static_cast<int32_t>(kTieSpread)
                   && r.lo_spread <= static_cast<int32_t>(kTieSpread)
                   && rail >= static_cast<int32_t>(kRailFloor)
                   && zero <= static_cast<int32_t>(kRailMargin)
                   && rail > zero;
    return r;
}

// --- The grid point ---

// One grid point of a Silent case. There is no latch, so there is no t0 from
// the chain -- the repeat takes its own, spins the SAME park + d it would
// have spun in a Latch case, and converts. The cadence is identical to a
// Latch repeat minus the chain traffic, which is exactly the comparison
// wanted: the difference between this curve and the control curve is the
// shift and the pulse, and nothing else.
//
// TIMED-OUT REPEATS ARE EXCLUDED -- take_one() is where and why -- and
// `valid` reaches the point line's own n= field so the exclusion is visible in
// the capture rather than inferred from it.
CountedPoint measure_silent_point(uint32_t d_ns)
{
    const uint32_t park_cycles = ns_to_cycles(kParkNs);
    const uint32_t d_cycles    = ns_to_cycles(d_ns);

    RepeatAccum acc;
    for(int r = 0; r < kRepeats; ++r)
    {
        // cycles_now() - t0 on uint32_t wraps correctly with no special
        // case: the DWT counter is free-running and unsigned specifically so
        // this subtraction is always valid, even across a wrap. Do not "fix"
        // it.
        const uint32_t t0 = cycles_now();
        while(cycles_now() - t0 < park_cycles + d_cycles) { }
        take_one(acc);
    }
    return acc.finish();
}

// --- Step 1: the Latch and ShiftOnly grid point ---

// One grid point of a Latch or ShiftOnly case.
//
// The victim's address and enable are the same in both words (xtalk_plan.h's
// word builder guarantees it and test_xtalk_plan.cpp asserts it), so the
// victim never changes channel and the sample-and-hold's charge
// redistribution -- settle-measured.md section 7's 845-count first arrival --
// is not in this measurement at all.
//
// ONE DEVIATION FROM THE TASK BRIEF'S BLOCK, and it is the semantic Task 5's
// review installed: the brief passes `nullptr` for the span and divides by
// kRepeats. That is the exact path the review found and closed for the silent
// grid, and it would have come straight back in here for three more kinds. The
// reduction goes through RepeatAccum/take_one instead, so a point whose mean is
// over fewer real repeats prints its own n= and is the fix working.
CountedPoint measure_event_point(MuxScan& chain, const XtalkCase& c, uint32_t d_ns)
{
    const uint32_t d_cycles    = ns_to_cycles(d_ns);
    const uint32_t park_cycles = ns_to_cycles(kParkNs);

    RepeatAccum acc;
    for(int r = 0; r < kRepeats; ++r)
    {
        // The park establishes BOTH things the event needs: a settled victim
        // and a defined starting state for the aggressor bits.
        chain.write_chain(c.word_a);
        const uint32_t park_t0 = cycles_now();
        while(cycles_now() - park_t0 < park_cycles) { }

        // t = 0 is the 595's own RCLK edge for a Latch case, and the last
        // SRCLK falling edge for a ShiftOnly case. Neither is the call site:
        // 16 clocked bits in front of the event would fold into every
        // reading as a constant nobody measured.
        const uint32_t t0 = (c.kind == XtalkKind::Latch)
                                ? chain.write_chain_timed(c.word_b)
                                : chain.shift_chain_timed(c.word_a);
        while(cycles_now() - t0 < d_cycles) { }

        take_one(acc);
    }
    return acc.finish();
}

// --- Step 2: the Static measurement ---

// A Static case: one word held, kRepeats conversions, no grid and no event.
// Row 8 is the DC shift of the ground join under eight LEDs, and a DC shift
// has no d -- the reader differences the dark case's mean against the lit
// case's for the same victim.
//
// Same deviation from the brief's block as measure_event_point(), for the same
// reason: the repeats go through take_one().
CountedPoint measure_static(MuxScan& chain, const XtalkCase& c)
{
    chain.write_chain(c.word_a);
    const uint32_t t0 = cycles_now();
    while(cycles_now() - t0 < ns_to_cycles(kParkNs)) { }

    RepeatAccum acc;
    for(int r = 0; r < kRepeats; ++r) take_one(acc);
    return acc.finish();
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
// then -- and holding all five rather than one is what G6 needs:
// |mean_control(d) - mean_silent(d)| at every grid point is a difference
// against row 1, per victim, and row 2 runs after all five of row 1.
//
// THE TASK BRIEF'S int32_t silent_mean_curve[5][65] IS NOT ADDED BESIDE THIS.
// It would be 1300 bytes holding a copy of the `mean` fields already here, and
// a copy that silently drops `valid` -- G6 would then difference against a
// point no repeat converted at, whose -1 mean is not a level. G6 reads
// g_silent_pts[v][k].p.mean and checks g_silent_pts[v][k].valid, which is the
// brief's intent with nothing duplicated.
//
// At namespace scope and not on the stack: 5 * 65 * sizeof(CountedPoint) is
// about 5.2 kB, which is a large fraction of the probe's stack and none of
// the .bss it sits in here.
CountedPoint g_silent_pts[kXtalkVictims][kGridPoints];
int32_t g_silent_mean[kXtalkVictims];
bool    g_silent_seen[kXtalkVictims];

// How many surviving control-vs-silent point pairs a victim needs before G6
// may call it compared. A MAJORITY OF THE GRID -- 33 of 65 -- and the reason
// is what G6 is.
//
// G6 is not a statistic over the grid, it is an ENVELOPE: the maximum of
// |mean_control(d) - mean_silent(d)| over every d, and the spec defines it
// "at every d". What it is looking for is a transient -- the 16-bit shift and
// the RCLK pulse disturbing a parked victim -- whose magnitude depends
// entirely on where in the 12.8 us grid you sample it. A handful of surviving
// pairs can all sit in a quiet stretch of that transient and report 0, which
// is the same "perfect score from almost no data" this fix exists to stop,
// only one step further along. One pair is emphatically not enough, and it is
// not chosen here merely because it is the smallest number above zero.
//
// A majority is the WEAKEST threshold under which "over the grid" is still an
// honest description of what was measured: below half, the gate is making a
// claim about a grid it saw less of than it missed. That is the whole
// argument for the number -- it is not tuned to any measurement, and it must
// not be quoted as one.
//
// It is also nearly free in practice. A pair drops out only when every one of
// kRepeats conversions timed out at that delay on one of the two curves; the
// 2026-09-18 capture reports timeouts=0 on all five CAL lines, so losing even
// ONE pair is already an abnormal run and losing 33 is a broken one. A
// threshold that never fires on a healthy board and fires loudly on a sick
// one is doing its job.
//
// COST IF THIS IS WRONG:
//  - too high: a run that lost most of a victim's grid is refused. G6 comes
//    back -1, the gates fail, no aggressor verdict may be quoted, and the
//    SHELL_XTALK_G6 line names the victim and prints pairs= so the operator
//    can see it was 20 of 65 and not a real excursion. One block, 9 s, and a
//    correctly labelled refusal.
//  - too low: G6 passes on a thin sample of an envelope, and G6 is the gate
//    that licenses EVERY aggressor delta in the run. read_xtalk.py recomputes
//    G6 per victim and would catch a magnitude the curves contradict, but it
//    cannot invent the points that were never measured -- it would agree with
//    a firmware that also looked at four pairs. Both sides therefore hold the
//    same minimum; see MIN_CONTROL_PAIRS in read_xtalk.py, which derives it
//    from the block's own `points` field rather than copying 33.
constexpr int kMinControlPairs = kGridPoints / 2 + 1;

// The scratch curve every case that is NOT row 1 measures into. One buffer and
// not fifty-three: the only curve that has to outlive its own case is row 1's,
// because that is the one G6 and G5 come back to. ~780 bytes of .bss.
CountedPoint g_case_pts[kGridPoints];

// --- The lines. ONE call site each, which is a requirement and not tidiness ---

// Spec section 8 wants rows 1 and 10 -- and in Task 6 every other row too --
// indistinguishable but for the `row` field. Task 5 kept two hand-copied
// PrintLine sites in sync by hand, and its own self-review caught a
// print-ORDER divergence of exactly that class before the build. Task 6 adds
// three more kinds, so the copies would have become four and six.
//
// BYTE BUDGET, and this is why spec section 8's single SHELL_XTALK line is
// split into a CASE line and a point line here. libDaisy's log buffer is 128
// bytes (lib/libDaisy/src/hid/logger.h:29); the spec's combined line runs
// about 150 and would be truncated and stamped "$$", which is how
// settle_probe.cpp lost two fields before anyone noticed.
//
// MEASURED on the board 2026-09-18 across three whole blocks of rows 1 and 10:
// 106 characters, 108 with CRLF. Those rows' chain words are two digits. Rows
// 7 and 8 carry the whole LED field, whose widest word is 16383 (4 address +
// 2 enable + 8 LED bits = 14 bits), so both word fields grow by three
// characters -- DERIVED 112 / 114, still 14 bytes clear, and MEASURED again in
// the Task 6 report off this image.
void print_case_line(bench::Board& hw, int i, const XtalkCase& c, bool skipped)
{
    hw.PrintLine("SHELL_XTALK_CASE case=%d row=%d kind=%d victim_group=%d "
                 "victim_ch=%d r_src=%d word_a=%d word_b=%d skipped=%d",
                 i, static_cast<int>(c.row), static_cast<int>(c.kind),
                 c.group, c.channel, static_cast<int>(c.r_src_ohm),
                 static_cast<int>(c.word_a), static_cast<int>(c.word_b),
                 skipped ? 1 : 0);
}

// The point line. n= carries the repeats that CONVERTED, not kRepeats: it
// reads 64 on a clean grid and is the only place a timed-out repeat becomes
// visible in the capture.
//
// BYTE BUDGET: MEASURED 2026-09-18 at 66 characters, 68 with CRLF.
void print_point_line(bench::Board& hw, int i, uint32_t d_ns, const CountedPoint& cp)
{
    hw.PrintLine("SHELL_XTALK case=%d d_ns=%d n=%d mean=%d min=%d max=%d",
                 i, static_cast<int>(d_ns), cp.valid,
                 cp.p.mean, cp.p.min, cp.p.max);
}

// What a 65-point curve reduces to: both of settle-measured.md section 5's own
// statistics, plus the curve mean G5 judges a floor row by.
struct Reduction
{
    int32_t mean_spread;        // peak to peak of the per-point means
    int32_t widest_band;        // widest raw min..max at any one point
    int32_t widest_band_d_ns;
    int32_t curve_mean;         // mean over the usable points' means
    int     usable_points;      // points at which at least one repeat converted
};

// Reduce one case's curve and emit its lines in the order spec section 8
// fixes: CASE, then the 65 points, then STAT -- the same order for every row,
// so the `row` field is the only record of which row it was.
//
// ONE BODY FOR EVERY GRID KIND. Silent, Latch and ShiftOnly differ in which
// measure_*() filled `pts` and in nothing else; four inline copies of this
// reduction was the wrong direction with under 10 kB of SRAM_EXEC left, and it
// is also how the three rows would have drifted apart.
//
// `lines_already_out` is row 10's case and only row 10's: it printed its CASE
// line before the grid and each point as it was taken, which is the whole
// point of that row. What it prints and in what order is identical.
Reduction reduce_and_emit(bench::Board& hw, int i, const XtalkCase& c,
                          const CountedPoint* pts, bool lines_already_out)
{
    Reduction red{};
    int64_t   mean_sum = 0;
    int32_t   mean_lo = 0x7FFFFFFF, mean_hi = -0x7FFFFFFF;
    red.widest_band      = -1;
    red.widest_band_d_ns = -1;

    for(int k = 0; k < kGridPoints; ++k)
    {
        // A point with no conversion at all contributes to no statistic
        // rather than folding a -1 into a spread: see take_one().
        if(pts[k].valid <= 0) continue;
        mean_sum += pts[k].p.mean;
        if(pts[k].p.mean < mean_lo) mean_lo = pts[k].p.mean;
        if(pts[k].p.mean > mean_hi) mean_hi = pts[k].p.mean;
        const int32_t band = pts[k].p.max - pts[k].p.min;
        if(band > red.widest_band)
        {
            red.widest_band      = band;
            red.widest_band_d_ns = static_cast<int32_t>(grid_ns(k));
        }
        ++red.usable_points;
    }
    // -1, not a spread or a mean computed from the init values, if the whole
    // grid came back unusable.
    red.mean_spread = (red.usable_points > 0) ? (mean_hi - mean_lo) : -1;
    red.curve_mean  = (red.usable_points > 0)
                          ? static_cast<int32_t>(mean_sum / red.usable_points)
                          : -1;

    if(!lines_already_out)
    {
        print_case_line(hw, i, c, false);
        for(int k = 0; k < kGridPoints; ++k) print_point_line(hw, i, grid_ns(k), pts[k]);
    }

    // Both of settle-measured.md section 5's statistics. Reported, neither
    // gated: spec section 7 is explicit that the settled region's width is a
    // quantity in this instrument and not a gate, because gating it would
    // refuse the very run that answers the question.
    hw.PrintLine("SHELL_XTALK_STAT case=%d settled_mean_spread=%d "
                 "widest_sample_band=%d at_d_ns=%d",
                 i, red.mean_spread, red.widest_band, red.widest_band_d_ns);
    return red;
}

// Park the victim: the chain word, the ADC channel, the rung its impedance
// picks, and the settle spin. Every kind starts from exactly this state, which
// is what makes one case's curve subtractable from another's.
//
// The rung reads about 180-245 counts low at 5150 R in steady state
// (settle-measured.md section 7), so no ABSOLUTE level from any curve here may
// be quoted without that qualification. Every delta the reader computes is a
// difference at the same rung, where the bias cancels.
void park_victim(MuxScan& chain, const XtalkCase& c)
{
    chain.write_chain(c.word_a);
    probe_adc::select_time(
        probe_adc::channel_of_group(c.group),
        probe_adc::sample_time_for_rung(sample_time_index_for(c.r_src_ohm)));
    const uint32_t t0 = cycles_now();
    while(cycles_now() - t0 < ns_to_cycles(kParkNs)) { }
}

// One grid case end to end: the quiet window, the park, the 65 points, the
// lines. `pts` is where the curve lands -- row 1's own slot, or the scratch.
Reduction run_grid_case(bench::Board& hw, MuxScan& chain, int i,
                        const XtalkCase& c, CountedPoint* pts)
{
    hw.Delay(kQuietMs);   // see kQuietMs
    park_victim(chain, c);

    if(c.prints_inline) print_case_line(hw, i, c, false);

    for(int k = 0; k < kGridPoints; ++k)
    {
        // FOR A SILENT CASE, FROM HERE TO THE END OF THE GRID: no chain access
        // at all, and for row 1 no print either. That is the only way to read a
        // settled channel with neither the 595's traffic nor USB-CDC's in the
        // measurement, and it is the case the whole attribution turns on. Row
        // 10 is the same grid with the point line moved inside it.
        pts[k] = (c.kind == XtalkKind::Silent)
                     ? measure_silent_point(grid_ns(k))
                     : measure_event_point(chain, c, grid_ns(k));
        if(c.prints_inline) print_point_line(hw, i, grid_ns(k), pts[k]);
    }

    return reduce_and_emit(hw, i, c, pts, c.prints_inline);
}

// One Static case end to end. No grid and no event, so no point lines and no
// STAT line -- the reader differences the two means of a victim's pair.
//
// It still gets its SHELL_XTALK_CASE line, so the completeness check is ONE
// rule for all 58 cases rather than one rule per kind.
void run_static_case(bench::Board& hw, MuxScan& chain, int i, const XtalkCase& c)
{
    hw.Delay(kQuietMs);
    // THE PARK HAPPENS TWICE HERE, and it is the one place park_victim()'s
    // "every kind starts from exactly this state" is not literally true:
    // measure_static() writes the same word_a again and spins another kParkNs
    // before its first conversion. Harmless -- the word is identical and the
    // second park is a full one, so the channel is at least as settled as
    // every other kind's -- and it is left alone deliberately, because
    // park_victim() is also what selects the ADC channel and the rung, and
    // measure_static() (which is the task brief's block) does neither. Costs
    // 10 * 20 us a block.
    park_victim(chain, c);

    const CountedPoint cp = measure_static(chain, c);

    print_case_line(hw, i, c, false);
    // BYTE BUDGET: DERIVED 112 characters at the widest (word 16383, four
    // 5-digit fields), 114 with CRLF, against the 128-byte log buffer.
    // n= carries the repeats that converted, exactly as the point line does.
    hw.PrintLine("SHELL_XTALK_STATIC case=%d victim_group=%d victim_ch=%d "
                 "r_src=%d word=%d n=%d mean=%d min=%d max=%d",
                 i, c.group, c.channel, static_cast<int>(c.r_src_ohm),
                 static_cast<int>(c.word_a), cp.valid,
                 cp.p.mean, cp.p.min, cp.p.max);
}

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

        // --- Step 4: the whole table, in the order the plan runs it ---
        //
        // ROWS 1 AND 2 RUN FIRST FOR EVERY VICTIM, and that is a property of
        // kXtalkPlan (xtalk_plan.cpp's own header comment says why it is
        // ordered that way), not of this loop. What this loop guarantees is
        // that a table which ever stopped doing it would FAIL G6 rather than
        // compare a control against zeroed .bss: g_silent_seen[] gates the
        // comparison, and controls_compared counts it.
        for(int v = 0; v < kXtalkVictims; ++v)
        {
            g_silent_mean[v] = 0;
            g_silent_seen[v] = false;
        }

        // G6's accumulator. Reset per block: a gate that could latch a pass
        // from an earlier block would stop being a statement about this one.
        int32_t worst_control     = 0;
        int     controls_compared = 0;

        for(int i = 0; i < kXtalkCases; ++i)
        {
            const XtalkCase& c = kXtalkPlan[i];

            // --- Step 3: skipped cases are lines, not absences ---
            //
            // Spec section 4: a skipped case is a LINE in the output, never a
            // silently shorter table. A reader that has to infer an absence
            // cannot tell "not run" from "lost in transit", and USB-CDC on
            // this machine does lose whole lines.
            //
            // Row 6 is the only rv4-dependent row: its four cases toggle A3
            // with the 16:1 enabled, which switches RV4's floating wiper into
            // ADC_9 until the pot is fitted.
            const bool skipped = c.needs_rv4 && (SHELL_XTALK_RV4 == 0);
            if(skipped)
            {
                print_case_line(hw, i, c, true);
                continue;   // no SHELL_XTALK points, no SHELL_XTALK_STATIC
            }

            // --- Task 7: a case naming no victim is a diagnosis, not a gap ---
            //
            // This used to be a bare `continue`. It printed nothing at all --
            // no SHELL_XTALK_CASE line, no marker -- so the case simply was
            // not in the block. read_xtalk.py caught that (its completeness
            // rule 1 counts the firmware's own `cases=` against the CASE
            // lines it saw) and discarded the whole block, which is the right
            // outcome; but the log then said only that a block was short, not
            // WHICH case went missing or why, and a reader had to diff the
            // capture against the plan table to find out.
            //
            // It gets the rv4 path's treatment four lines above -- a CASE
            // line with skipped=1, so the block stays complete and the case
            // is visible -- PLUS its own line, because an rv4-skipped case
            // and a case naming no victim are different events and a reader
            // must not have to guess which one is in front of them. The rv4
            // skip is an expected configuration; this one is a defect in
            // kXtalkPlan or kXtalkVictimTable, and victim_index_of()'s own
            // comment already calls it "a defect worth seeing rather than an
            // array index".
            //
            // The (group, channel) that matched no table row rides on the
            // line so the diagnosis is readable without joining back to the
            // CASE line above it. UNREACHABLE with the table as it stands --
            // all kXtalkCases entries name a victim -- which is exactly why
            // it must print rather than vanish if that ever stops being true.
            //
            // BYTE BUDGET: DERIVED 63 characters at the widest (case 57, a
            // two-digit row and channel), 65 with CRLF, against the 128-byte
            // log buffer. Not measured on hardware: nothing reaches it.
            const int v = victim_index_of(c);
            if(v < 0)
            {
                print_case_line(hw, i, c, true);
                hw.PrintLine("SHELL_XTALK_NOVICTIM case=%d row=%d "
                             "victim_group=%d victim_ch=%d",
                             i, static_cast<int>(c.row), c.group, c.channel);
                continue;   // no points, no _STAT, no _STATIC
            }

            if(c.kind == XtalkKind::Static)
            {
                run_static_case(hw, chain, i, c);
                continue;
            }

            // Row 1's curve is held for the whole block; every other case
            // reduces and prints from the scratch and is done with.
            const bool      floor_row = (c.row == 1);
            CountedPoint*   pts       = floor_row ? g_silent_pts[v] : g_case_pts;
            const Reduction red       = run_grid_case(hw, chain, i, c, pts);

            if(floor_row)
            {
                // G5 judges the floor row, not row 10: row 10 reads the same
                // channel with its own traffic on the bus, and the address
                // question deserves the quietest reading the block has.
                //
                // A curve with no usable point at all leaves g_silent_seen
                // false, and G5 then fails that victim rather than judging it
                // against a fabricated mean.
                if(red.usable_points > 0)
                {
                    g_silent_mean[v] = red.curve_mean;
                    g_silent_seen[v] = true;
                }
            }
            else if(c.row == 2)
            {
                // --- Step 4: G6 ---
                //
                // For each victim, max |mean_control(d) - mean_silent(d)| over
                // the whole grid. The control carries the same shift and the
                // same latch pulse as every aggressor case, so if the control
                // alone has already moved the reading past the criterion, the
                // aggressor deltas are no longer interpretable as differences
                // and the finding is G6 itself.
                //
                // A DIFFERENCE BETWEEN TWO CURVES TAKEN IN THE SAME BLOCK,
                // which is why row 1's five are held rather than reduced to a
                // remembered scalar. A curve against a memory is not the
                // quantity spec section 7 defines.
                // TASK 7: `pairs` COUNTS THE COMPARISONS, AND THE COUNT IS
                // WHAT GATES THE INCREMENT. Until this fix,
                // ++controls_compared ran whenever g_silent_seen[v] was true,
                // regardless of whether a single point pair had survived the
                // exclusion immediately below. A victim whose 65 pairs were
                // all excluded left `worst` at 0, left worst_control
                // untouched, and still counted -- so controls_compared
                // reached kXtalkVictims, the summary took worst_control
                // instead of -1, and G6 passed with a worst_control_delta of
                // 0. A perfect score out of zero comparisons: not an error a
                // reader would notice, a clean result.
                int32_t worst = 0;
                int     pairs = 0;
                if(g_silent_seen[v])
                {
                    for(int k = 0; k < kGridPoints; ++k)
                    {
                        // A point no repeat converted at is not a reading and
                        // its -1 mean is not a level. Excluded on both sides.
                        if(g_silent_pts[v][k].valid <= 0 || pts[k].valid <= 0)
                            continue;
                        ++pairs;
                        const int32_t d
                            = pts[k].p.mean - g_silent_pts[v][k].p.mean;
                        const int32_t a = (d < 0) ? -d : d;
                        if(a > worst) worst = a;
                    }
                }
                const bool compared = pairs >= kMinControlPairs;
                if(compared)
                {
                    // `worst` is kept out of the fold when the victim was not
                    // compared, rather than folded in as a conservative
                    // guess: a magnitude from a sample too thin to support
                    // the gate is not evidence for it either way, and
                    // controls_compared below already fails the gate.
                    if(worst > worst_control) worst_control = worst;
                    ++controls_compared;
                }

                // UNCONDITIONAL, and outside the g_silent_seen test on
                // purpose: one line per victim that has a control case, every
                // block, so read_xtalk.py can require exactly that many and
                // catch a lost one. A victim with no floor curve prints
                // pairs=0 compared=0, which is the honest answer and not an
                // absence.
                //
                // Here rather than beside the G5 lines because this is where
                // the numbers exist -- carrying them to that loop would need
                // two more per-victim arrays in .bss for nothing. The block's
                // line order is fixed either way.
                //
                // BYTE BUDGET: DERIVED 88 characters at the widest (case 57,
                // a five-digit worst), 90 with CRLF, against the 128-byte log
                // buffer. Not measured on hardware yet.
                hw.PrintLine("SHELL_XTALK_G6 case=%d victim_group=%d "
                             "victim_ch=%d pairs=%d worst=%d compared=%d",
                             i, c.group, c.channel, pairs, worst,
                             compared ? 1 : 0);
            }
        }

        // --- Step 3's verdict: the span and G5 ---
        //
        // Measured after the cases, not before: it parks the chain on four
        // tie channels, which is exactly the traffic a Silent grid must not
        // have in it.
        //
        // lost= and n_min= are Task 7's: the repeats the four tie reads lost
        // in total, and the smallest surviving count on any one tie. A clean
        // pass prints lost=0 n_min=64 and is the only shape that can yield
        // valid=1 while kMaxTieLoss is 0 -- which is the point. Before this,
        // a lost repeat on an AGND tie changed nothing visible in the whole
        // block.
        //
        // BYTE BUDGET: this line was 78 characters at its widest before the
        // two fields (five-digit zero, rail, hi_spread and lo_spread) and is
        // DERIVED at 95, 97 with CRLF, against the 128-byte log buffer. Not
        // measured on hardware yet -- the capture that measures it is the
        // one this image has not taken.
        const SpanRead sr = measure_span(chain);
        hw.PrintLine("SHELL_XTALK_SPAN zero=%d rail=%d hi_spread=%d "
                     "lo_spread=%d lost=%d n_min=%d valid=%d",
                     static_cast<int>(sr.span.zero),
                     static_cast<int>(sr.span.rail),
                     sr.hi_spread, sr.lo_spread, sr.lost, sr.worst_valid,
                     sr.span.valid ? 1 : 0);

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
        // G6 IS NO LONGER VACUOUS. Task 5's image had no control curve at all
        // and assigned 0 here, which xtalk_gates() reads as "inside the
        // criterion"; this image reduces
        // |mean_control(d) - mean_silent(d)| over the whole grid for all five
        // victims and the field is that maximum.
        //
        // -1 -- a FAILED G6, since xtalk_gates() requires the field to be
        // non-negative -- when any victim's control could not be compared
        // against its own floor curve. That is the only honest answer when the
        // difference the gate is DEFINED as was never computed, and it is what
        // stops a reordered plan table from turning a missing comparison into
        // a pass.
        //
        // TASK 7: controls_compared now counts COMPARISONS AND NOT VICTIMS.
        // It used to increment for any victim that had a floor curve at all,
        // so five victims whose every point pair was excluded still reached
        // kXtalkVictims here and handed the gate a worst_control of 0. See
        // the counting site above.
        summary.worst_control_delta
            = (controls_compared == kXtalkVictims) ? worst_control : -1;

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
        // spinning at full speed; this block measures for many seconds and
        // that delay is gone. The cadence is set by the work itself: 44 grids
        // of 65 points x 64 repeats, 10 static cases of 64 conversions, 4
        // skipped lines, and 54 kQuietMs windows. Task 5's silent-only image
        // MEASURED about 1.8 s per block over ten grids; the whole table is
        // measured in the Task 6 report, and THAT is the number the reader's
        // default timeout belongs to. Do not add a delay back to "settle" the
        // host: the reader accumulates to SHELL_XTALK_END and discards an
        // incomplete block, which is the mechanism that handles a slow host.
    }
}

} // namespace shell
