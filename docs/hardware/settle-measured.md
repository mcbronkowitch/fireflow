# The settle time — measured on the coupon

> **This document is the bench half.** It records what the test coupon actually
> said on **2026-09-17**, on one board, in one session, and nothing else. Its
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
which is the one-rung difference between them. Both are built from the same
span-derived pre-`ADSTART` overhead of **747 ns**; the same run's separately
measured start-to-aperture latency (`SHELL_SETTLE_CAL`) was **719 ns mean**,
673…777 ns, and the agreement between those two independent figures is the
cross-check that the offset arithmetic is right.

**The ADC conversion clock is measured, not assumed, every boot.** It came out
at **6.146 MHz**, half what `settle-budget.md` §1 derived from PLL3's dividers,
and the correction is the reason this probe produces a usable number at all.
The measurement, what it does and does not constrain, and what the wrong
constant cost are in [`settle-budget.md`](settle-budget.md) §1 and in
[`docs/gotchas.md`](../gotchas.md).

## 2. What the instrument cannot see

Two floors, both measured, both hard:

- **Its own offset.** A pair whose true settle is at or below its offset
  (991 or 1154 ns) collapses onto grid point zero. The firmware prints
  `d_settle_ns=-1` with `at_or_below_offset=1` for that case, and the *same*
  −1 with the flag clear for "never settled in the whole grid". These are
  different findings and must never be collapsed; `shell/read_settle.py`
  refuses to collapse them. Nothing faster than ~1 µs can be resolved by this
  instrument at all, which is precisely why two of the six pairs are 0 Ω
  reference steps: their job is to prove the floor is where it is claimed to be.
- **One ADC clock period of aperture jitter.** Measured 138 ns
  (`lat_max − lat_min`, on the calibration pass that pinned the grid step)
  against a clock period of 162.7 ns
  (derived from the measured clock). The DWT counts core cycles while the
  conversion starts on the ADC's own edge, so this is clock-domain-crossing
  quantisation, not software residue. It sets a hard floor under the grid step,
  which is why the grid is 200 ns and not the 100 ns originally specified. A
  finer grid needs a faster ADC clock, not tighter code.

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

G1, G2 and G4 pass. **G3 fails, and its failure is the result** — §5.

## 4. The settle times, against the model

Two consecutive blocks, swept in opposite directions (§6). Knee and true settle
are measured; the model column is `settle-budget.md`'s prediction as carried in
`kSettlePlan`; the ratio is derived from the two.

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
a model term that is too small, not of measurement noise.

Two qualifications that travel with those numbers and may not be dropped:

- **These are settle-to-~12-counts, not settle-to-8-counts.** The criterion the
  knee is nominally decided against is half an LSB of 12 bit = 8 counts, and
  §5 is the reason the board does not deliver it. Every quotation of a knee has
  to carry this.
- **The run's gates did not all pass**, so the firmware's own rule
  ("a run that fails a gate may not have its settle times quoted") applies to it
  literally. That rule was written for an instrument that *cannot produce* a
  time. This one produces reproducible times; what it cannot do is reach 8-count
  stability, and those are different claims. The ruling at the end of the probe
  was to quote the knees **with their precision stated**, rather than discard a
  good measurement to honour a rule aimed at a different failure. *Cost if that
  ruling is wrong:* a reader takes 1.3–1.5× as settled fact when it rests on one
  board, one session, and a criterion the board does not meet.

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

**On this board a multiplexer channel cannot be read to half an LSB of 12 bit at
any delay.**

Measured: the settled-region mean spread — the *same* statistic the knee is
decided on, taken from each pair's knee to the end of the grid — is **9 to 12
counts** on every divider pair, in both sweep directions (P1 12/9, P2 12/9,
P3 9/12, P4 9/11). The criterion is 8. That includes P3, whose curve is flat
from the very first grid point because it settles long before the instrument can
see it: the wander is not a property of the transient.

Derived: about **4 counts** of that is conversion noise. The reference pair's
own 64-repeat spread `b0` runs around 31 counts peak to peak, which puts the
spread of a 64-sample mean near 0.8 counts, so roughly 4 counts of extreme
accumulate across 65 grid points. The rest is systematic and reproduces in both
directions.

**State it plainly: the limit this board demonstrates is repeatability, not
settling time.** A longer delay does not fix it, because the residue is there
after the node has stopped moving. That is a real answer to the question the
coupon was built to ask, and a more useful one than a settle time would have
been.

*Cost if wrong:* if the ~10 counts is an artifact of something unexamined —
supply ripple, USB DMA activity, the probe's own 595 clocking — then the
criterion is achievable and this conclusion is too pessimistic. The
sweep-direction test (§6) rules out sweep-order drift **and nothing else**.

One honest wrinkle in the record: the same statistic read **2–4 counts** on the
capture the gate was introduced against, and 9–12 counts on the two captures
that closed the probe. What differs between those captures is not established. This
document takes the larger, more conservative figure, and the disagreement is
itself a reason to repeat the run before treating either as a property of the
design.

A separate observation, printed but not gated: single-point **raw** sample bands
of 159, 169 and 146 counts appear in otherwise clean settled regions, and a
106-count excursion appears on P0 — a channel tied to AGND through 0 Ω, with
nothing to settle. Those move a 64-sample mean by at most ~2.6 counts, so they
do not block a knee, but a channel with nothing to settle showing 106 counts of
excursion is exactly the crosstalk question this coupon exists to ask, and it is
not answered here.

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
bookkeeping error. **No mechanism is offered here.** Nothing in this session
measured one, and this repo's rule is that a runtime claim needs a probe before
it enters a document. It needs its own probe.

It is not a curiosity: it is why the settled reference is now the tail of the
curve rather than a parked read (§1). Every knee was being judged against that
845-count-wrong reference until the round that found this replaced it.

## 8. What this rests on

One board. One session, one evening. One coupon, one populated copy, one Patch
Submodule, one firmware image. Six pairs: three resolvable above the
instrument's offset floor, two 0 Ω reference pairs whose job is to be
unresolvable, and one (P3) that falls below the floor and can only be bounded.
The direction test is two consecutive blocks.

What would strengthen it, roughly in order of what each buys:

1. **A second board.** Five bare boards were fabricated; populating a second one
   separates this coupon from the design. The 9–12 count wander in §5 is the
   claim most in need of it.
2. **A second session**, cold, on another day — the cheapest test of whether
   §5's 2–4 versus 9–12 disagreement is run-to-run or configuration.
3. **A probe for §7.** It is the only finding here with no explanation at all.
4. **More blocks per direction**, to put an uncertainty on the knees rather than
   "agree to within one grid step".
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
  clean" — settled to half an LSB of 12 bit — at a stated delay. On this board
  that state does not exist at any delay (§5). The prediction was answered by a
  limit it did not anticipate.
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

The reader accumulates to the end marker and discards an incomplete block,
because USB-CDC on this machine is not byte-perfect (truncated and spliced lines
were observed, marked `$$`). It exits 1 when the gates did not pass, and it
distinguishes the two meanings of `d_settle_ns=-1` in words. Its guard is
`shell/test_read_settle.py`, registered in CTest as `read_settle_guard`.

The board repeats a block roughly every 0.6 s and alternates sweep direction, so
two consecutive blocks are one direction test.
