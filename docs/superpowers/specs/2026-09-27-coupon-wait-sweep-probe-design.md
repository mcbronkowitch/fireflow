# The coupon wait-sweep probe — design (round three)

**Date:** 2026-09-27
**Status:** DESIGNED and BUILT to a flashable image while Bastian was away;
**not flashed, not measured.** Every decision marked *flagged* below was taken
without him and is his to overturn before the first board session.
**Depends on:** round two
([`2026-09-18-coupon-codec-tone-probe-design.md`](2026-09-18-coupon-codec-tone-probe-design.md))
and its write-up
[`docs/hardware/codec-tone-measured.md`](../../hardware/codec-tone-measured.md),
whose §6 and §9 name this instrument as the next one.
**Closes (if it runs clean):** the one open item round two left bigger than it
found it — *what in the ADC path moves a 5150 Ω channel by up to 852 counts as
a function of how long the converter waits.*

## 1. What this is

Round two's §6 found that a settled 5150 Ω channel reads **852 counts low**
when the conversions on it are 10 ms apart, **~310** at 1 ms and **~77** at
0.2 ms, against the same channel read back to back. Its silent-cadence arm
proved that this is the **interval between conversions** and not the tone:
with the output carrying nothing the shift reproduces to within one count.
It is ordered by source impedance (5150 Ω → −852 / −671, 650 Ω → −18,
150 Ω → 0) and it drifts 60–90 counts between boots above 100 Hz while the
100 Hz value holds to four counts.

That instrument could only ever produce **three** wait values, because its
wait was a tone period. This one makes the wait the axis: a victim is
converted once, the converter then **idles for a commanded interval `W`**,
and the victim is converted again. The second conversion is the measurement.
`W` runs from back-to-back to 50 ms on a log grid.

The question is narrow and has three parts, each answered by one arm (§3):

1. **The curve.** Shift as a function of `W`, per victim: where it starts,
   whether and where it saturates, and whether 10 ms / 1 ms / 0.2 ms land on
   round two's three numbers.
2. **Is it paid off by one extra conversion?** The same grid with one
   discarded conversion immediately before the measured one.
3. **Is it paid off by a longer acquisition window?** The same grid at the
   387.5-cycle rung.

**No mechanism is proposed in this document**, and the arms are chosen so that
the result does not need one to be useful: arms 2 and 3 are the two fixes the
shipping firmware could adopt, and each either works at every `W` or it does
not. `settle-measured.md` §7's first-arrival table sits beside round two's §6
with a numerical correspondence (−866 from an AGND tie against −852 here) that
is tempting and is **not** used as an argument anywhere below.

## 2. Why a new instrument and not an arm of round two

Round two's clock is the audio callback — its phase grid waits for a phase
crossing, so its only notion of time is the tone. A wait sweep needs the
opposite: a wait that is **not** tied to any period, so that `W` and every
periodic activity on the board are independent. Three `#if` branches through
`tone_probe.cpp`'s repeat loop would have been the alternative, and that file
is already 1400 lines. It gets its own switch, `SHELL_WAIT_PROBE`, its own
`wait_probe.cpp` and `wait_plan.cpp`, and calls the same `probe_adc`
primitives both earlier rounds use, unchanged.

It reuses, rather than re-derives:

- round one's five victims (`xtalk_plan.h:kXtalkVictimTable`) and their
  per-impedance working rung (`settle_plan.h:sample_time_index_for()`);
- the clock pass, the latency pass and G2/G4 (`probe_adc::measure_clock()`,
  the `SHELL_*_CAL` loop);
- the G5 span and address pass (round one's `measure_span()` /
  `coupon_verdict()`, already copied once into `tone_probe.cpp`; copied a
  second time here, same rule: a change to one copy is a defect in the
  others).

## 3. The arms

Five victims × four arms. Every repeat of every arm has the same shape:

```
prime:    sample_now()                  -- same channel, same rung, discarded
wait:     idle until W has elapsed since the prime's EOC   (interrupts ON)
[discard: sample_now(), discarded]      -- arm B only
measure:  sample_now()                  -- the reading
```

The prime is what makes `W` mean something. Without it the "previous
conversion" would be whatever the previous grid point or the previous victim
left behind, at an unknown distance in time and on an unknown channel. With
it, every measured conversion follows a conversion **on the same channel, at
the same rung, exactly `W` earlier** — which is what round two's phase grid
did implicitly (its previous conversion was the previous repeat on the same
victim), now with `W` commanded instead of inherited from a frequency.

| arm | codec | rung | grid | what it answers |
|---|---|---|---|---|
| **A** `wait` | stopped | victim's working rung | full grid, 15 points | the curve |
| **B** `discard` | stopped | victim's working rung | full grid | whether one extra conversion pays it off, at every `W` |
| **L** `long` | stopped | 387.5 cycles | full grid | whether a long acquisition window pays it off, at every `W` |
| **C** `codec` | running, callback writes zeros | victim's working rung | 200 µs, 1 ms, 10 ms only | the bridge to round two (§5, G9) |

**The grid, in µs:** 0, 2, 5, 10, 20, 50, 100, 200, 500, 1 000, 2 000,
5 000, 10 000, 20 000, 50 000. Roughly three points per decade, and round
two's three cadences (200, 1 000, 10 000 µs) are **on** the grid, so arm A
meets them without interpolation. `W = 0` is back to back: the measured
conversion starts as soon as the prime's `EOC` is seen, and it is every arm's
own reference.

*Flagged:* the top of the grid. Round two never waited longer than 10 ms, so
anything past 10 ms is new ground, and 50 ms is where the block cost starts
to dominate (§6). If arm A has not saturated by 50 ms, the next round extends
the grid; it does not stretch this one.

**Codec state.** Arms A, B and L run with the codec **stopped**, so no
periodic interrupt competes with the idle and the only interrupts left are
USB-CDC and SysTick. Arm C runs with the codec started and the callback
writing zeros — round two's `RunningSilent` operating point, the one its §6
silent-cadence arm ran in. Round two measured `Stopped` against
`RunningSilent` at ±1 count on every victim, so the two are not expected to
differ; arm C exists so that the bridge in §5 is like-for-like rather than
resting on that expectation.

**The wait runs with interrupts enabled; the conversions are masked** — the
same split round two uses, for the same reason: arm C's callback must keep the
codec fed. Masking a 50 ms wait would also starve USB-CDC for the length of a
wait, and a 50 ms interrupt-masked region is a failure mode this project has
no history with and does not need. An interrupt landing during the wait
lengthens `W` by that interrupt's duration, microseconds against a grid
whose smallest non-zero step is 2 µs and whose interesting region is
milliseconds. It is **not** subtracted and **not** measured; §9 carries it.

**Why these victims.** The same five as rounds one and two, for the same
reason: two 5150 Ω dividers (one per mux), the 650 Ω divider as impedance
control, and two 150 Ω ties as the attribution axis — round two measured the
shift at exactly 0 on both ties, so a tie that moves here is an instrument
fault, not a finding.

## 4. The reported quantity

Per `(arm, victim, W)`: `n`, `mean`, `min`, `max` of the 64 measured
conversions. The prime and the discard are never folded into anything.

The result is the **shift**

```
shift(arm, victim, W) = mean(arm, victim, W) − mean(arm, victim, W = 0)
```

computed on the host, **same arm, same victim, same block**, so a rung bias
(`settle-measured.md` §7: 180–245 counts at 5150 Ω between the working rung
and 387.5 cycles) cancels inside each arm and never enters a comparison
across arms. Arm L's absolute means are printed as well and are the only
absolute levels this document allows quoting, because 387.5 cycles is the
rung `settle-measured.md` §7 measured landing on the divider's true value.

Nothing here is a pass/fail criterion on the board. This is a
characterisation; the gates in §5 decide whether a block may be read at all,
and nothing decides what the curve is allowed to look like.

## 5. Gates

Round one's G2, G4 and G5, unchanged in bound and in code (copied, §2), plus:

| gate | bound | what it refuses |
|---|---|---|
| **G2** floor | `b0` in 0…64 | a run whose conversion noise makes a mean of 64 no longer decisively inside an 8-count band |
| **G4** jitter | `lat_max − lat_min` ≤ 200 ns, mean not negative | a run whose aperture jitter is coarser than the ADC's own clock period allows |
| **G5** address | every victim inside its `coupon_expect` band, judged on arm A's `W = 0` mean | a victim that is not where the table says it is |
| **G7** callback health | zero missed blocks across arm C | a bridge measured while the callback starved |
| **G9** bridge | arm C, `REF_A` (group 0, ch 8), `shift` at `W = 10 000 µs` inside **[−900, −800]** | an image that does not measure what round two measured |

**G9 is host-side**, like round two's G8, because its bound comes from a
published measurement and baking that into the firmware turns a measurement
into a literal nobody re-measures. The firmware prints `g9=-1`;
`read_wait.py` computes it and folds it into its exit code.

*Why that bound.* Round two measured this quantity four times across four
boots: −849.7, −850.7, −851.4, −854.0 (`codec-tone-measured.md` §6). That is
a 4.3-count range, and a gate at `[min − 4, max + 4]` in the style of round
two's G8(b) would be the natural choice — **if the instrument were the same**.
It is not: round two's wait was a phase crossing, measured from the previous
conversion's start, with the codec writing a tone or zeros; this one's is a
commanded idle from the previous conversion's `EOC`. Those differ by a
conversion time (~4 µs) in 10 ms, which should not matter, but *should* is
not a measurement. So the bound is set to refuse the failures that would make
the curve unrelatable to round two — a shift near 0, or near half the size —
and to admit instrument differences of a few percent. **±50 around 850 is a
judgement, not a derivation. Flagged.** A tighter bound is a one-line change
in `read_wait.py` once the first block has been read.

G9 is the one to prove red deliberately: `−799` and `−901` must fail, `−800`
and `−900` must pass, and all four are pinned in `shell/test_read_wait.py`.

**What G9 does not gate: 1 kHz and 5 kHz.** Round two's own §6 shows those
wander 91 and 23 counts between boots while 100 Hz holds to four. A gate on
them would be a gate on the boot, so arm C's 200 µs and 1 ms points are
printed and reported beside round two's four captures and never judged.

`gates_ok` in the firmware is the AND of G2, G4, G5 and G7 — the four it can
compute. `read_wait.py` exits 1 when `gates_ok` is 0 **or** G9 fails, and says
which.

## 6. Cost

Derived, not measured: one repeat of a grid point costs `W` plus three
working-rung conversions at most (~2.7 µs each at the measured 6.146 MHz,
arm B) or two long-rung ones (~65 µs each, arm L). The grid sums to
88 887 µs, so a full grid of 64 repeats is **~5.7 s** per victim per arm.
Arms A, B and L over five victims: **~85 s**. Arm C: 11.2 ms × 64 × 5 =
**~3.6 s**. With calibration, span and printing, a block is estimated at
**~90 s**, against round two's measured 170–227 s. `block_ms` is printed
(§7) and is the measurement; this paragraph is the estimate it replaces.

Output volume: 4 arms × 5 victims = 20 case lines, 15 × 15 + 3 × 5 = 240
point lines, plus about twenty calibration and verdict lines — roughly 280
lines per block, about a third of round two's 821. The accumulation overflow
(`logger.cpp:78-87`, `docs/gotchas.md`) is a volume problem, and this is less
volume.

## 7. Output

Every line under 125 bytes at data-widest values; the widest, `SHELL_WAIT_CASE`,
is ~105.

```
SHELL_WAIT_CFG    adc_khz=%d repeats=%d points=%d block_size=%d sr=%d sweep_dir=%d git=%s
SHELL_WAIT_CLK    span_short_cyc=%d span_long_cyc=%d smp_short_tenths=%d smp_long_tenths=%d
SHELL_WAIT_CAL    lat_mean_ns=%d lat_min_ns=%d lat_max_ns=%d b0=%d timeouts=%d
SHELL_WAIT_CASE   case=%d arm=%d victim_group=%d victim_ch=%d r_src=%d rung_tenths=%d codec=%d
SHELL_WAIT        case=%d w_us=%d n=%d mean=%d min=%d max=%d
SHELL_WAIT_SPAN   zero=%d rail=%d hi_spread=%d lo_spread=%d valid=%d
SHELL_WAIT_G5     victim_group=%d victim_ch=%d expect=%d mean=%d ok=%d
SHELL_WAIT_GATES  g2=%d g4=%d g5=%d g7=%d g9=%d gates_ok=%d
SHELL_WAIT_HEALTH missed_blocks=%d timeouts=%d block_ms=%d
SHELL_WAIT_END
```

- `arm`: 0 = A `wait`, 1 = B `discard`, 2 = L `long`, 3 = C `codec`.
- `case` numbers the 20 `(arm, victim)` pairs of a block from 0, arm-major,
  and is the key `SHELL_WAIT` lines carry. A reader keys on it and never on
  position.
- `sweep_dir`: 0 walks each case's grid ascending in `W`, 1 descending. It
  alternates every block, for the reason `settle-measured.md` §6 gives: in
  one direction elapsed time and the axis increase together, and a drift
  across a 5.7 s sweep would be indistinguishable from a slow tail. Arm C's
  three points follow the same direction.
- `rung_tenths`: the sampling rung in tenths of an ADC cycle (25 for
  2.5 cycles, 3875 for 387.5) — printed because arm L's rung differs from
  every other arm's and a reader must not have to infer it.
- `codec`: 1 for arm C, 0 otherwise.
- `timeouts` on `SHELL_WAIT_HEALTH` is `probe_adc::timeouts()`, lifetime. A
  timed-out measured conversion is excluded from its point's statistics and
  shows as a shorter `n`; a timed-out prime or discard is not excluded — the
  measured conversion after it is still taken, and the count says it
  happened.

## 8. How this can go red

- **`tests/test_wait_plan.cpp`**, host, doctest:
  - the grid starts at 0, is strictly increasing, and has 15 points;
  - each of arm C's three points is on the grid (so arm A meets them exactly);
  - the µs-to-core-cycle conversion of the **largest** grid point does not
    overflow 32 bits — the trap is real: `cycles.h:ns_to_cycles()` computes
    `ns * 480` in 32 bits and wraps above ~8.9 ms, so 10 ms passed through it
    in nanoseconds silently becomes ~1 ms. The test pins that the wait
    conversion used here is exact at 50 000 µs, and a deliberate route through
    `ns_to_cycles()` is how its red is proved;
  - the sweep-direction index map is a permutation in both directions;
  - the derived block estimate stays under a bound (so a grid edit that
    triples the block does not go unnoticed).
- **`shell/test_read_wait.py`**, CTest `read_wait_guard`: G9 at its four
  boundaries (§5), a fixture with `gates_ok=0` exiting 1, a fixture whose tie
  shift is non-zero being reported (not gated), and a truncated block
  refused rather than half-read.
- **On the board**, the two 150 Ω ties are the zero: round two measured their
  shift at exactly 0 at every cadence. A tie with a shift is an instrument
  fault.

## 9. What is read, what is derived, what is unmeasured

| claim | class |
|---|---|
| The 852 / 310 / 77 counts at 10 / 1 / 0.2 ms, codec running | measured — `codec-tone-measured.md` §6 |
| That they follow the interval and not the tone | measured — same, silent-cadence arm |
| That `Stopped` and `RunningSilent` differ by ≤1 count | measured — same, §5 |
| The ADC clock (6.146 MHz) and the conversion costs derived from it | measured clock, derived costs; re-measured every boot and printed |
| `ns_to_cycles()` overflowing above ~8.9 ms | read — `cycles.h:38-41`, arithmetic; pinned by a host test |
| The ~90 s block | **estimate** (§6); `block_ms` is the measurement |
| That interrupts during the wait lengthen `W` by microseconds only | **reasoned**, not measured; the probe does not time its own waits |
| What the curve looks like, where it saturates, whether arms B and L collapse it | **unmeasured — that is the run** |
| Any mechanism for any of it | **none is offered** |

## 10. What the result would license

Stated in advance so the write-up cannot choose it afterwards:

- **Arm B flat at every `W`** (shift within the arm's own `W = 0` noise, i.e.
  a few counts): one discarded conversion before every pot read pays the
  whole effect off, and the shipping firmware's cheapest fix is known.
- **Arm L flat at every `W`**: a long acquisition window pays it off. Costlier
  per read than a discard, and `settle-measured.md` §7 already found the
  shipping rung too short at pot impedance for a different reason.
- **Neither flat**: the effect is not paid off by anything this instrument can
  do, and the next round is about the converter, not about the scan.
- **Arm A saturating before 50 ms**: the saturation value and the `W` at which
  it is reached are the numbers the panel firmware's scan cadence has to be
  designed against. Not saturating: the grid is too short and the next round
  extends it.

What it does **not** license: anything about the shipping firmware's
free-running DMA, whose conversions follow each other with no idle and on
different channels — that is `settle-measured.md` §7's first-arrival
question, already measured, and not this one.

## 11. Out of scope

- Any mechanism experiment. If the curve names a time constant, what the time
  constant belongs to is the round after this.
- An interrupt-masked wait variant. Considered and left out (§3); if the curve
  shows structure at the microsecond end that interrupts could explain, a
  masked variant restricted to `W ≤ 1 ms` is the follow-up.
- Pots, a second board, a second submodule — the same list every round has
  carried.
