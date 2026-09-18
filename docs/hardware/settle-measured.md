# The settle time — measured on the coupon

> **This document is the bench half.** It records what the test coupon actually
> said on **2026-09-17 and 2026-09-18**, on one board, in two sessions, and
> nothing else. §4 and §5 carry both, and where the two disagree the
> disagreement is left visible rather than averaged away. Its
> companion [`settle-budget.md`](settle-budget.md) is the model — arithmetic that
> has never been on a bench — and the two are deliberately not merged: §9 below
> says which of its predictions this run confirms and which it breaks.
>
> **Every number here is either measured or derived, and each one says which.**
> Measured means a probe printed it. Derived means it was computed from
> something printed, and the computation is given. Nothing is upgraded from the
> second class to the first, and where the run characterised something without
> explaining it — §7 — it is left characterised.
>
> The instrument is `shell/settle_probe.cpp`, built behind the
> `SHELL_SETTLE_PROBE` switch, and it took ten hardware fix rounds to become
> trustworthy. Two of those rounds changed what the numbers below *mean* rather
> than how precise they are: the ADC clock stopped being assumed and started
> being measured, and the yardstick every curve is judged against stopped being
> a separately parked read. §1 carries both; `docs/gotchas.md` carries what the
> first one cost.

## 1. What the instrument is

The coupon carries six **pairs**: a source channel and a target channel on the
same multiplexer, with the target's source impedance known from the netlist
(`shell/settle_plan.cpp`, `kSettlePlan`). Group 0 is the 74HC4067 on `ADC_9`,
group 1 the 74HC4051 on `ADC_10`.

One measurement of one pair goes: park on the source channel for **20 µs**
(`kParkNs`, far past every prediction), write the target's address, wait a
commanded delay **d**, run **one** conversion. That is repeated **64 times**
(`kRepeats`) per grid point, and the grid is **65 points at 200 ns**, i.e.
d = 0…12800 ns.

Three definitions carry all the weight:

- **The settled reference** is the mean of the last 8 grid points of that pair's
  own curve — not a separately parked read. That changed late in the run, and it
  is the single most consequential fix in it: §7 is the reason.
- **The knee** is the smallest grid index whose mean is within 8 counts of that
  reference **and stays within it for every larger index**. A backward walk, so
  a curve that rings out of the band and back in cannot report its first
  crossing as a settle time. If the reference itself has not converged (its own
  spread exceeds the same 8 counts), the pair is reported as *not settled* and
  no knee is produced at all.
- **True settle = knee + that pair's offset.** The knee is a *commanded* delay;
  the ADC's aperture does not open when the delay expires. The offset is the
  probe's own pre-`ADSTART` overhead plus that pair's sampling window, because
  the sample-and-hold is acquired at the **end** of the sampling window — the
  conversion cycles after it only digitise what is already captured.

**Offsets, measured per boot and printed** (`SHELL_SETTLE_OFFSET`): **991 ns**
for the 150 Ω and 650 Ω pairs (1.5-cycle sampling rung) and **1154 ns** for the
5150 Ω pairs (2.5-cycle rung). They differ by exactly one ADC clock period,
which is the one-rung difference between them — **that** is the independent
confirmation that the arithmetic is right, because the two offsets are built
from different rungs and land exactly one rung apart.

Both are built from the same span-derived pre-`ADSTART` overhead of **747 ns**.
The same run's separately measured start-to-aperture latency
(`SHELL_SETTLE_CAL`, the 2026-09-17 capture) was **719 ns mean**, 673…777 ns.
Those two are **not** independent figures and their agreement validates
nothing: both subtract the same conversion-time estimate, derived from the same
clock measurement, from a span measured the same way. What they are is two
measurements of the same quantity minutes apart, agreeing to 28 ns — about 4 %
— which is a statement about the span's run-to-run repeatability and about
nothing else.

**The ADC conversion clock is measured, not assumed, every boot.** It came out
at **6.146 MHz**, half what `settle-budget.md` §1 derived from PLL3's dividers,
and the correction is the reason this probe produces a usable number at all.
The measurement, what it does and does not constrain, and what the wrong
constant cost are in [`settle-budget.md`](settle-budget.md) §1 and in
[`docs/gotchas.md`](../gotchas.md).

## 2. What the instrument cannot see

Two floors, both measured, both hard, and one design choice that is not a floor
but belongs here:

- **Its own offset.** A pair whose true settle is at or below its offset
  (991 or 1154 ns) collapses onto grid point zero. The firmware prints
  `d_settle_ns=-1` with `at_or_below_offset=1` for that case, and the *same*
  −1 with the flag clear for "never settled in the whole grid". These are
  different findings and must never be collapsed; `shell/read_settle.py`
  refuses to collapse them. Nothing faster than ~1 µs can be resolved by this
  instrument at all, which is precisely why two of the six pairs are 0 Ω
  reference steps: their job is to prove the floor is where it is claimed to be.
- **One ADC clock period of aperture jitter.** Measured 138 ns
  (`lat_max − lat_min`) against a clock period of 162.7 ns (derived from the
  measured clock). The DWT counts core cycles while the conversion starts on
  the ADC's own edge, so this is clock-domain-crossing quantisation, not
  software residue. It sets a hard floor under the grid step, which is why the
  grid is 200 ns and not the 100 ns originally specified. A finer grid needs a
  faster ADC clock, not tighter code.

  **Which capture each jitter figure comes from matters, because three of them
  are quoted in this document and they are not the same measurement.** The
  138 ns is the fix-round-4 calibration pass of 2026-09-17, the one the grid
  step was pinned against, and it is the largest — which is why it is the one
  the grid is sized on. The 673…777 ns latency range in §1 is that same run's
  `SHELL_SETTLE_CAL`, a spread of 104 ns. The 2026-09-18 repeat (§4) measured
  `lat_max − lat_min` of **42…98 ns** across its six blocks, i.e. below both.
  No capture has ever read above 138 ns, but none of these is a bound: they are
  six-, one- and one-block samples of the same quantity.

And the design choice, a **read from the source, not a runtime claim**:

- **The probe's own sampling window is far shorter than the window the model
  says a channel change needs.** `sample_time_index_for()`
  (`shell/settle_plan.cpp`) picks each pair's rung from `(R_src + R_ADC) ·
  C_ADC` — ST's acquisition rule, **term B alone**. That gives the 150 Ω and
  650 Ω pairs the 1.5-cycle rung and the 5150 Ω pairs the 2.5-cycle rung
  (printed as `smp_tenths=15` and `25`), which at the measured 6.146 MHz are
  244 ns and 407 ns of acquisition (derived: 1.5 / 6.146 MHz and
  2.5 / 6.146 MHz). But [`settle-budget.md`](settle-budget.md) finding 3 —
  guarded by `test_redistribution_is_the_binding_term` — says term B is
  **never** the binding term, and its own §2 table puts term C at 5150 Ω with
  65 pF at about **2190 ns**, roughly 5× the window the probe actually gives
  itself.

  Inside a repeat loop this is self-cancelling and the curves are unaffected:
  every repeat re-parks and re-steps the same way, so the sample-and-hold
  arrives at each conversion carrying the previous repeat's charge from the
  same channel at the same delay. It is **not** self-cancelling for the first
  arrival on a channel after a long absence — which is exactly and only what
  §7 describes. §7 says what follows from that, and what does not.

## 3. The four gates, and what each refuses

The probe judges itself before it names anything. A run that fails a gate prints
every number and **refuses to name a settle time**; `read_settle.py` exits 1 on
such a run. Failing a gate is never a statement that the board is defective —
that distinction is the point of having gates at all.

| Gate | Bound | What it refuses |
|---|---|---|
| **G1** reference pairs | both 0 Ω pairs must settle by the first grid step or the next (≤ 200 ns commanded) | a run in which the instrument's own zero is not fast — if a 0 Ω step takes measurable time, the offset subtraction in §1 is a guess and every other knee is built on it |
| **G2** floor | the spread of the reference pair's 64 settled conversions, `b0`, must be 0…64 counts | a run whose conversion noise is large enough that the *mean* of 64 repeats is no longer decisively inside the 8-count criterion |
| **G3** settled-region agreement | max − min of the per-point **means**, from each pair's own knee onward, ≤ 8 counts | a curve whose settled region does not actually hold still to the precision the knee claims. It is evaluated only *after* the knee: before the knee a wide band is physics, because aperture jitter times a moving node is voltage |
| **G4** instrument jitter | `lat_max − lat_min` ≤ one grid step (200 ns), and the latency mean not negative | a run whose aperture jitter is coarser than the grid it is reading, and a run whose conversion-time subtraction came out larger than the span it was subtracted from |

G1, G2 and G4 pass. **G3 fails** — and what that does and does not say is §5.

**G3 is about 2× stricter than the criterion the knee is decided on, and the
table above cannot show you that.** The two are different statistics over the
same inputs:

- the knee rule bounds each settled point's **deviation from the reference**,
  `|mean[i] − tail_ref| ≤ 8` for every `i` at or past the knee. That is the
  spec's §6 criterion, half an LSB of 12 bit.
- G3 bounds the settled region's **peak-to-peak** spread, `max − min` of the
  same means, with no reference in it.

Finding a knee at all therefore already bounds the peak-to-peak spread at
**16** counts, by construction: two points can sit at most one full band apart,
one at +8 and one at −8. Bounding it at 8 asks for a settled region half as
wide as the criterion allows. So a failed G3 is not "the board misses §6's
criterion" — a run that produced a knee has met §6 at every settled point, by
definition of having produced one.

**This is a defect in the spec, not in the code.**
[`docs/superpowers/specs/2026-09-17-coupon-settle-probe-design.md`](../superpowers/specs/2026-09-17-coupon-settle-probe-design.md)
§7a defined G3 without ever relating it to its own §6, so nothing in the
instrument or in this document could tell the two apart, and for a day the
branch read G3's failure as the board failing the criterion. The gate is worth
keeping as it stands — a settled region twice as tight as the criterion is a
reasonable thing to ask of a board, and it is the only statistic here that
noticed the wander at all — but it must be quoted as what it is.

**Two of the four bounds moved as a side effect of the grid step, and that is
worth a line each.** `kKneeMaxNs` and `kJitterMaxNs` are both defined as
`kGridStepNs`, so when fix round 5 widened the grid from 100 ns to 200 ns to
clear the measured 138 ns of jitter, G1 and G4 widened with it:

- **G1's absolute bound went from 100 ns to 200 ns.** It rescued nothing — both
  reference pairs report grid index 0 either way, in every capture — and
  deriving the bound from the grid step rather than repeating a literal is
  right, because a gate that admits a knee finer than the grid can resolve is
  meaningless. But a gate that loosened 2× as a consequence of another decision
  should say so rather than be found later.
- **G4's bound moved with the grid too**, and the honest way to put it is not
  "G4 was not widened to admit the jitter". The grid could not stay at 100 ns:
  the measured 138 ns of aperture jitter is larger than a 100 ns grid step, so
  a 100 ns grid claims resolution the instrument does not have, and widening G4
  alone to admit the jitter would have been exactly the move this probe exists
  to refuse. The consequence travels with every knee in §4: **138 ns is 69 % of
  a grid step, so adjacent grid points are not independent**, and one grid step
  is the finest precision a knee from this instrument could claim even in the
  best case. §4's repeat shows that on one pair the actual block-to-block
  scatter is four times that, so the grid is not the limiting term there —
  something else is, and it is unidentified.

## 4. The settle times, against the model

**2026-09-17**, two consecutive blocks, swept in opposite directions (§6). The
knees are measured; true settle is derived as knee + that pair's printed
offset; the model column is `settle-budget.md`'s prediction as carried in
`kSettlePlan`; the ratio is derived from the two. A six-block repeat a day
later follows below and does not replace this table.

| pair | step | R_src | offset | knee, asc / desc | **true settle** | model | ratio |
|---|---|---:|---:|---:|---:|---:|---:|
| P1 | `R_LO2` (AGND) → `REF_A`, 4067 | 5150 Ω | 1154 ns | 2800 / 3000 ns | **3954 / 4154 ns** | 3016 ns | 1.31 / 1.38 |
| P2 | `R_HI2` (A+3V3) → `REF_A`, 4067 | 5150 Ω | 1154 ns | 3000 / 2800 ns | **4154 / 3954 ns** | 3016 ns | 1.38 / 1.31 |
| P4 | `R_LO4` (AGND) → `REF_C`, 4051 | 5150 Ω | 1154 ns | 1600 / 1600 ns | **2754 ns** | 1856 ns | 1.48 |
| P3 | `R_LO2` (AGND) → `REF_B`, 4067 | 650 Ω | 991 ns | at or below offset | ≤ 991 ns | 381 ns | consistent |
| P0 | `R_HI1` → `R_SP10`, 4067 (0 Ω) | 150 Ω | 991 ns | at or below offset | ≤ 991 ns | 88 ns | reference |
| P5 | `R_HI4` → `R_LO3`, 4051 (0 Ω) | 150 Ω | 991 ns | at or below offset | ≤ 991 ns | 54 ns | reference |

**The headline: across three independent pairs the true settle is 1.3–1.5× the
model's prediction — coherently, not as scatter.** Three pairs on two different
multiplexer types, two of them approaching the same target from opposite
directions, all land on the same side by a similar factor. That is the shape of
a model term that is too small, not of measurement noise. The *direction* of
that finding survives the repeat below; the precision of the individual factors
does not, so quote the band and not a pair's number.

Two qualifications that travel with those numbers and may not be dropped:

- **Every one of these is a settle to within 8 counts of that pair's own
  reference** — that is what finding a knee means, and it is exactly the spec's
  §6 criterion, half an LSB of 12 bit. What the knees do not come with is a
  settled region *tighter* than that: from the knee to the end of the grid the
  means wander 8–12 counts peak to peak (§5), against the 8 that G3 asks for.
  A quotation of a knee has to carry the width of the region it opens, not a
  claim that the criterion was missed.
- **The run's gates did not all pass, and quoting the knees anyway is not a
  bending of the rule.** The firmware's rule — "a run that fails a gate may not
  have its settle times quoted" — exists so that an instrument which cannot
  produce a trustworthy time does not produce one anyway. The only failing gate
  here is G3, and G3 is about 2× stricter than §6's criterion (§3). The
  criterion the times are decided against was met at every settled point, in
  every capture. So the times stand on their own terms; what has to travel with
  them is the width of the settled region, which is the thing G3 actually
  measured. *Cost if this reading is wrong:* a reader takes 1.3–1.5× as settled
  fact when it rests on one board and two sessions — and the repeat below shows
  the scatter is wider than one session suggested.

**A same-instrument repeat, 2026-09-18.** Six consecutive blocks, alternating
direction, on the same board after a reflash and a power cycle. Measured
(`SHELL_SETTLE_KNEE`, `SHELL_SETTLE_OFFSET`), with true settle derived as
knee + offset:

| pair | offset | knees, six blocks | **true settle** | model |
|---|---:|---|---:|---:|
| P1 | 1154 ns | 3400, 3200, 4000, 3600, 3600, 3200 ns | **4354…5154 ns** | 3016 ns |
| P2 | 1154 ns | 2600, 2600, 2800, 2800, 2800, 2600 ns | **3754…3954 ns** | 3016 ns |
| P4 | 1154 ns | 1800, 1800, 1600, 1600, 2000, 1800 ns | **2754…3154 ns** | 1856 ns |
| P3 | 991 ns | at or below offset in four blocks; 400 and 200 ns in the other two | **≤ 1391 ns** | 381 ns |
| P0, P5 | 991 ns | at or below offset in all six | **≤ 991 ns** | 88 / 54 ns |

The direction of the result holds: every resolvable pair is still slower than
the model, by a derived factor of 1.2–1.7×. **The scatter is the news, and it
is wider than the 2026-09-17 run implied.** Across six blocks P2 moves by one
grid step and P4 by two, which is about what §3 leads one to expect. P1 moves
by **four** —
3200 to 4000 ns — and its whole range sits above the 2800/3000 the 2026-09-17
run reported for it. A third capture, taken earlier the same day while Task 6's
reader was being tested, put P1's knee at 2800 again. So P1's knee is not
reproducible to a grid step across sessions, and no average of these should be
quoted as if it were: the honest statement is that P1's knee has been seen
anywhere from 2800 to 4000 ns on the same board with the same instrument, and
that nothing here explains the difference.

P3 is the other thing that moved. On 2026-09-17 it was at or below its offset
in both blocks; on 2026-09-18 two of six blocks put its knee one or two grid
points above zero. That is the floor doing exactly what §2 says it does — P3's
true settle sits within a grid step or two of the 991 ns the instrument cannot
see past — and not a change in the board.

What is *not* claimed: which model term carries the factor. The model is linear
in R·C, so a 1.3–1.5× time is a 1.3–1.5× R·C — arithmetic, not an attribution.
Node capacitance is the term the model itself flags as an unmeasured estimate
(`settle-budget.md` §1, §5), but nothing in this run measures it, and a slow
tail the single-pole model has no term for would look identical from here.

**The curves are real, not fitted.** From the first full capture of the sweep,
on the shorter 33-point grid it started with: P1
rises 29297 → 32499, P2 falls 35349 → 32502, P4 rises 31882 → 32581 — three
settling curves converging on the same divider value from opposite directions.
P0 and P5 sit flat.

## 5. The result worth more than a settle time

**On this board a multiplexer channel settles to half an LSB of 12 bit — and
then keeps wandering across about that same band for as long as you keep
looking.**

Measured (`SHELL_SETTLE_BAND`, field `settled_mean_spread`, six consecutive
blocks on 2026-09-18, both sweep directions): the settled-region mean spread —
peak to peak across the per-point means, from each pair's knee to the end of
the grid — is **8 to 12 counts** on all four divider pairs and **0 to 1** on
the two 0 Ω reference pairs.

| block | sweep_dir | P0 | P1 | P2 | P3 | P4 | P5 |
|---|---|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 1 | 10 | 11 | 9 | 10 | 0 |
| 2 | 0 | 1 | 12 | 10 | 10 | 10 | 0 |
| 3 | 1 | 1 | 12 | 11 | 9 | 9 | 0 |
| 4 | 0 | 0 | 10 | 9 | 9 | 10 | 0 |
| 5 | 1 | 1 | 10 | 9 | 11 | 8 | 0 |
| 6 | 0 | 1 | 12 | 10 | 10 | 9 | 0 |

**Read that against the right number.** G3's bound is 8, and 8–12 fails it —
but G3 is a peak-to-peak bound and the spec's criterion is a deviation from a
reference (§3). Having a knee at all means every one of those means was within
8 counts of that pair's own reference; §6's criterion was met at every settled
point of every pair in every block. What this board does **not** deliver is a
settled region *tighter than half* the band the criterion allows, which is what
G3 asks for. So the finding is not "the channel cannot be read to half an LSB";
it is that the settled state is not still — it occupies most of the criterion's
own band rather than sitting quietly inside it.

The reference pairs reading 0–1 is what makes that a statement about the
channels rather than about the instrument: the same instrument, the same 65
grid points, the same 64 repeats, on a channel tied to a rail through 0 Ω,
produces a settled region one count wide. Two earlier captures, before the
figure was printed, put the divider pairs at 9–12 (2026-09-17, hand-recomputed)
and at 2–4 (the capture G3 was introduced against). The 2–4 remains
unexplained; six printed blocks a day later agree with the 9–12 to within one
count at the bottom of the range.

P3 is the cleanest read of the four. It settles below the instrument's floor,
so in most blocks its curve is flat from the first grid point and its whole
grid *is* the settled region — 9 to 11 counts of wander with no transient in it
at all. The wander is not the tail of a settling curve.

Derived: about **4 counts** of it is conversion noise. The reference pair's own
64-repeat spread `b0` ran 27…40 counts peak to peak across these six blocks,
which puts the spread of a 64-sample mean near 1 count, so roughly 4 counts of
extreme accumulate across 65 grid points. The rest is systematic and reproduces
in both sweep directions and across a reflash.

**What the run does not answer: whether a single conversion reaches the
criterion.** Every number above is the mean of 64 conversions, and the probe
runs `OVS_NONE` — one raw conversion per repeat, no hardware oversampling. The
shipping firmware reads its pots through libDaisy, which runs `OVS_32`. Those
are three different quantities: a raw single conversion, a 32-fold hardware
average, and this probe's 64-fold software mean. Nothing here measures the
first two, and no verdict for either is computed here. The raw per-sample bands
below are the closest thing in this document to the first, and they are single
points, not a distribution.

*Cost if wrong:* if the 8–12 counts is an artifact of something unexamined —
supply ripple, USB DMA activity, the probe's own 595 clocking — then a settled
region inside 8 counts is achievable and this is too pessimistic. The
sweep-direction test (§6) rules out sweep-order drift **and nothing else**.

A separate observation, printed but not gated: single-point **raw** sample bands
of 159, 169 and 146 counts appear in otherwise clean settled regions, and a
106-count excursion appears on P0 — a channel tied to AGND through 0 Ω, with
nothing to settle. Those move a 64-sample mean by at most ~2.6 counts, so they
do not block a knee, but a channel with nothing to settle showing 106 counts of
excursion is exactly the crosstalk question this coupon exists to ask, and it is
not answered here.

The 2026-09-18 capture prints the same observation per pair and per block
(`widest_sample_band_counts`), and it reproduces: the widest single-point raw
band per block runs 44…103 counts on P0, 56…197 on P1, 61…184 on P2, 53…195 on
P3, 47…92 on P4 and 10…56 on P5. Both 0 Ω reference pairs show it, so it is not
a property of the divider channels — and it is not visible in their means,
which hold to 0–1 counts. Whatever this is, it is fast, rare, and it averages
out; nothing in either capture identifies it.

## 6. The sweep-direction confound, and what ruling it out covers

The grid is swept in one direction per block, so within a block elapsed time and
commanded delay increase together: a drift across the ~0.6 s sweep would be
indistinguishable from a slow settling tail. The parked pre/post reads proved
drift **exists** (P2 moved 29 counts across one sweep) but not how much of the
curve it explains.

The probe therefore alternates direction every block. Measured, two consecutive
blocks:

- ascending: P1 2800, P2 3000, P4 1600 ns
- descending: P1 3000, P2 2800, P4 1600 ns
- P0, P3, P5 at or below their offset in **both** directions

**Both directions agree to within one grid step.** The curve is a function of
the commanded delay and not of wall-clock time; the drift the pre/post pair
detected is a small additive term, not the shape. The settling is real.

**The 2026-09-18 repeat keeps that conclusion and takes something else away.**
Its six blocks alternate direction too, so each direction has three. P1's knee
scatters by four grid steps across the six — and it scatters *within* each
direction (3400, 4000, 3600 ascending; 3200, 3600, 3200 descending), with the
two directions' ranges overlapping. So the scatter is not direction-dependent
and this section's ruling stands. What no longer stands is reading "both
directions agree to within one grid step" as a statement about the knee's
precision: two blocks agreeing was, in hindsight, two samples of something that
spreads wider than that (§4).

What the test does **not** cover: anything that tracks the commanded delay
itself, or the grid index, identically in both directions; drift *between*
blocks; crosstalk; and temperature. It is one confound removed, not a clean bill
of health.

## 7. Open, characterised, and deliberately unexplained

**The first parked arrival on a channel after a long absence reads about 845
counts low.** Measured: P1's parked pre-read was 31649 against its own curve's
tail of 32494, while P2 — visiting the same channel immediately afterwards —
agreed with the tail to within 6 counts. Every later read of that channel agrees
within a few counts.

The order of operations was checked and found correct, so it is not the obvious
bookkeeping error. **No mechanism is asserted here.** Nothing measured one, and
this repo's rule is that a runtime claim needs a probe before it enters a
document.

**It reproduces across a reflash and a power cycle.** Measured on 2026-09-18,
six consecutive blocks: P1's `settled_raw_pre` ran 31647…31655 against its own
`tail_ref` of 32492…32494, i.e. **838 to 847 counts low**, every block. P2,
visiting the same channel immediately afterwards, agreed with its own tail to
within 15 counts in every block. So this is stable, not an artifact of one
capture, and it is the same size as the 845 recorded a day earlier.

**An untested candidate, named as one: charge redistribution.** §2 records a
read from the source — the probe picks each pair's sampling rung from term B
alone, so the 5150 Ω pairs get 407 ns of acquisition where
[`settle-budget.md`](settle-budget.md) §2 puts term C at about 2190 ns. Inside
a repeat loop that cancels; for a first arrival on a channel it does not, and a
first arrival is exactly what the parked pre-read is. The mechanism would
therefore predict a deficit on precisely that read and on no other, which is
the shape of what is observed.

It is **not** offered as the explanation, and the arithmetic below is a bound,
not a prediction. Derived from `settle-budget.md`'s own constants: the
sample-and-hold can pull at most `C_ADC / (C_node + C_ADC)` = 4 pF / 69 pF ≈
**5.8 %** off the node at first contact, which is about **3800 counts** of full
scale if the cap arrives holding a voltage at the opposite end of the range.
The observed 845 is roughly a fifth of that. So the mechanism is *capable* of a
deficit this large; that is all the arithmetic says, and it would say the same
about several other mechanisms.

**The experiment that would settle it, in one round:** take the parked pre-read
either after a discarded conversion on the same channel, or at the 387.5-cycle
rung (63 µs of acquisition, far past term C). If the deficit is charge
redistribution it collapses in both cases. If it survives either, it is not —
and that is worth as much as a confirmation.

It is not a curiosity: it is why the settled reference is now the tail of the
curve rather than a parked read (§1). Every knee was being judged against that
845-count-wrong reference until the round that found this replaced it.

## 8. What this rests on

One board. One coupon, one populated copy, one Patch Submodule. Two sessions a
day apart, three firmware images, three captures — the 2026-09-17 evening run
(§4's table), an intermediate one during the reader's bring-up, and the
2026-09-18 six-block run (§4's repeat, §5's table, §7's reproduction). Six
pairs: three resolvable above the instrument's offset floor, two 0 Ω reference
pairs whose job is to be unresolvable, and one (P3) that sits on the floor and
can only be bounded. The direction test is a pair of consecutive blocks, done
twice.

What would strengthen it, roughly in order of what each buys:

1. **A second board.** Five bare boards were fabricated; populating a second one
   separates this coupon from the design. The 8–12 count wander in §5 is the
   claim most in need of it.
2. **A cold second machine or a cold boot hours apart.** The second session
   settled part of what it was asked to: §5's spread reproduced at 8–12 across
   six blocks and a reflash, so the 2–4 of the gate's introduction capture is
   the odd one out rather than the 9–12. What it did *not* settle is P1's knee,
   which moved four grid steps between sessions (§4) with no explanation.
3. **A probe for §7**, and the experiment is now written down in §7 rather than
   left as "it needs one".
4. **More blocks per direction.** Six blocks put P2 and P4 inside one grid step
   and P1 across four. Whatever uncertainty gets quoted on a knee has to come
   out of a count like that, not out of a single pair of blocks.
5. **A second submodule**, which separates the board from the MCU — the 3.1 %
   `hw.adc.Get()` deficit in `docs/gotchas.md` is a reminder that ADC behaviour
   here has already turned out to be configuration, not silicon.

## 9. What it says about `settle-budget.md`

**Confirmed:**

- Multiplexed channel settling is real, resolvable, and lands in the
  microsecond range the model puts it in. The model's *relative* ordering holds:
  the 4051 pair (predicted 1856 ns) settles measurably faster than the 4067
  pairs (predicted 3016 ns), and the low-impedance pairs fall below the
  instrument's floor as the model says they should.
- P3 (650 Ω, predicted 381 ns) and both 0 Ω reference pairs (predicted 88 and
  54 ns) are at or below the 991 ns floor — consistent with the model, though
  bounded rather than measured.

**Broken, or qualified:**

- **The magnitude.** True settle is 1.3–1.5× the prediction on all three
  resolvable pairs, i.e. the model underestimates by roughly 40 %. Coherent, not
  scatter. Which term is short is not measured (§4).
- **The criterion.** §6 of the budget document predicts a channel that "reads
  clean" — settled to half an LSB of 12 bit — at a stated delay. That much
  holds: past the knee, every per-point mean on every pair is inside 8 counts
  of its own reference. What the prediction did not anticipate is that "clean"
  would turn out not to mean *still*: the settled region wanders 8–12 counts
  peak to peak and keeps doing so however long you wait (§5). A model phrased
  as "after time t the channel is settled" has no term for a settled state that
  is not quiet.
- **The ADC clock**, which the model derived from PLL3's dividers and this probe
  measured at half that value. Corrected in `settle-budget.md` §1 on 2026-09-18;
  it moved every sampling window and every sweep duration in that document, and
  rewrote its findings 2 and 4.

**Untouched:** crosstalk between adjacent channels, charge injection, node
capacitance as a measured quantity, and anything about the audio artifact. This
run removed no item from the budget document's "What the model cannot see"; it
answered the settling time in front of that list, not the list itself.

## 10. How to repeat it

Two toolchains, and they must not be mixed — the probe is firmware, the reader
is host Python.

```
# firmware, from shell/ (ARM GCC, never source env.sh in this shell)
make -j8 images SHELL_COUPON_PROBE=1 SHELL_SETTLE_PROBE=1
dfu-util -a 0 -s 0x90040000:leave -D build/shell-sram.bin

# host, also from shell/: capture one complete block and judge it
python read_settle.py COM4 settle.csv
```

That writes **two** files and both are needed. `settle.csv` carries the grid
points, one row per measured point. `settle.csv.meta.csv` carries everything
else the block said — the configuration, the clock spans, the calibration pass,
the gate verdicts and each pair's offset, knee, reference and band — as
`scope,pair,key,value` rows. §4's table cannot be rebuilt from the grid points
alone: true settle is knee + offset, and both live in the metadata file, as
does the verdict that says whether a settle time from that run may be quoted.

The reader accumulates to the end marker and discards an incomplete block,
because USB-CDC on this machine is not byte-perfect (truncated and spliced lines
were observed, marked `$$`). It exits 1 when the gates did not pass, and it
distinguishes the two meanings of `d_settle_ns=-1` in words. Its guard is
`shell/test_read_settle.py`, registered in CTest as `read_settle_guard`.

**Do not expect `SHELL_SETTLE_WARMUP` to arrive.** It is printed once, before
the forever loop, and `StartLog(false)` does not wait for a host — so it goes
out milliseconds after reset, long before Windows finishes enumerating the CDC
device. It did not appear once in 2730 captured lines on 2026-09-18. Everything
on it that matters is reprinted inside the loop on `SHELL_SETTLE_GATES`
(`cfg_ok`, `init_ok`, `cal_ok`); the boot line is kept for SWD and for a logic
analyser.

The board repeats a block roughly every 0.6 s and alternates sweep direction, so
two consecutive blocks are one direction test.
