# Crosstalk — measured on the coupon

> **This document is the bench half.** It records what the test coupon actually
> said on **2026-09-18**, on one board, in one session, and nothing else. Its
> question is narrow: when the board's own digital side fires one controlled
> event just before the ADC's aperture, how far does a settled multiplexer
> channel move? The answer is in §4, and it names one aggressor out of nine.
>
> Its predecessor is [`settle-measured.md`](settle-measured.md), whose §5 left
> two observations open. This run closes both — §6 below — and it is the reason
> the instrument exists at all. Read `settle-measured.md` §1–§3 first if you
> want to know what the ADC path is; this probe inherits it unchanged and
> re-confirms it in §3.
>
> **Every number here is either measured or derived, and each one says which.**
> Measured means a probe printed it. Derived means it was computed from
> something printed, and the computation is given. Nothing is upgraded from the
> second class to the first, and where the run characterised something without
> explaining it — §8 — it is left characterised. **No mechanism is asserted for
> §4's finding.**
>
> The instrument is `shell/xtalk_probe.cpp`, built behind the
> `SHELL_XTALK_PROBE` switch. Its ADC primitives are the settle probe's, moved
> verbatim into `shell/probe_adc.cpp` and **proven behaviour-identical on this
> board** before anything was built on them: the refactored image reproduced a
> baseline capture with `adc_khz`, all six offset pairs and all four gates
> identical, and the two clock spans bit-identical. That proof is the reason the
> numbers below can be compared to `settle-measured.md`'s at all.

## 1. What the instrument is

The settle probe answers *how long does a channel take to arrive*. This one
holds a channel that has already arrived and asks *what moves it*.

Five **victims**, each read at a fixed address and enable for the whole of its
case, so the sample-and-hold's channel-change transient is not in the
measurement at all (`shell/xtalk_plan.cpp`, `kXtalkVictimTable`):

| victim | channel | mux | `r_src_ohm` | what it is |
|---|---|---|---:|---|
| `REF_A` | group 0, ch 8 | 74HC4067 on `ADC_9` | 5150 Ω | divider |
| `REF_B` | group 0, ch 9 | 74HC4067 on `ADC_9` | 650 Ω | divider |
| `R_SP10` | group 0, ch 10 | 74HC4067 on `ADC_9` | 150 Ω | 0 Ω link to `AGND` |
| `REF_C` | group 1, ch 6 | 74HC4051 on `ADC_10` | 5150 Ω | divider |
| `R_LO3` | group 1, ch 3 | 74HC4051 on `ADC_10` | 150 Ω | 0 Ω link to `AGND` |

**A naming discrepancy worth knowing before you read any table.** Spec §2 calls
`R_SP10` and `R_LO3` "0 Ω to `AGND`", and in the netlist they are exactly that.
`XtalkVictim::r_src_ohm` is "switch `Ron` plus what the netlist wires", so the
plan table enters them at **150 Ω** and every line the instrument prints says
`r_src=150`. This document says 150 Ω, because that is what was printed.
Neither is wrong — they measure different things — and the attribution argument
in §5 is unaffected either way: 150 Ω is 34× below the 5150 Ω dividers and 4.3×
below the 650 Ω one, so it is still the bottom rung by a wide margin. **The spec
should say so.**

A **case** is one victim, one aggressor, and two 16-bit chain words. `word_a` is
parked before the grid; `word_b` is what the event latches. Both carry the
victim's own address and enable bits, and a host test asserts that they agree
there and differ somewhere else — so a crosstalk case can never silently become
a settle-time case. 58 cases in ten rows; the four `row=6` cases need `RV4`
fitted and are **printed as skipped**, never omitted.

The grid is `settle_plan.h`'s: **65 points, 200 ns apart, 0 … 12 800 ns, 64
repeats each**. `d` is the commanded delay between the event and the
conversion's start. The criterion is `|delta(d)| <= 8` counts — half an LSB of
12 bit, expressed in the 16-bit left-aligned counts the ADC returns.

`delta(d)` is always a **difference of two curves on the same victim at the same
sampling rung**: the aggressor case's per-point mean minus its own row-2
control's. The control is a latch pulse with no bit change, so the difference
isolates the bit change from the pulse and the shift that precede it.

## 2. What the instrument cannot see, and what it refuses to say

**The verdict has no grid point to stand on, and the reader says so.** The
firmware printed `scan_settle_ns=2000000` — the audio block period, because
`MuxScan::step()` reads the sense pins for the step it wrote *last* time and
only then clocks out the next address (`shell/mux_scan.cpp:127-150`), so the
block period *is* the settle window. At the 96 samples / 48 kHz `src/hw/board.h`
sets, that is 2 ms, and the grid ends at 12.8 µs — **156× short of it**.

Spec §5's criterion applies at or past that boundary. No grid point reaches it.
A reader applying §5 literally would compute a verdict over an empty set and
print "pass"; `shell/read_xtalk.py` refuses to, and takes the **envelope** path
instead: `max |delta(d)|` over the whole grid, labelled `verdict_basis=envelope`
on every row. That is the more pessimistic of the two and it is what spec §6
licenses — the shipping ADC free-runs a circular DMA, so a pot read lands at an
arbitrary phase relative to any scan event and the envelope is the worst case it
can meet. **It is not a measurement at 2 ms and nothing here labels it as one.**

**Absolute levels are biased and may not be quoted as the node's voltage.** Each
victim is read at the rung `sample_time_index_for()` picks for its impedance,
and `settle-measured.md` §7 measured that rung reading **180–245 counts low in
steady state at 5150 Ω**. Every `delta`, every excursion and every `lit − dark`
below is a difference of two readings at the same rung, where that bias cancels.
The 150 Ω ties are unaffected — §7's ladder is flat for them.

**What was not measured.** The reverse-order sequence (aggressor before victim
park) is round two's. No second board, no second submodule, no rework. The audio
SAI DMA is not running in this image and neither is the shipping firmware's
free-running ADC DMA, which bounds what §6's row-10 result rules out.

## 3. The gates, and the one run they refused

Four gates, printed per block, folded into `gates_ok`. G2 and G4 are the settle
probe's own, reusing its constants — two instruments judging the same board by
two different thresholds is the failure that avoids.

| gate | bound | what it refuses |
|---|---|---|
| **G2** floor | the spread of the reference channel's 64 settled conversions, `b0`, must be 0…64 counts | a run whose conversion noise is large enough that a mean of 64 repeats is no longer decisively inside the 8-count criterion |
| **G4** jitter | `lat_max − lat_min` ≤ one grid step (200 ns), latency mean not negative | a run whose aperture jitter is coarser than the grid it reads |
| **G5** address | every victim inside its `coupon_expect` band against a valid span | a victim that is not where the table says it is — at which point *nothing* in the block means anything |
| **G6** control | `max_d \|mean_control(d) − mean_silent(d)\|` ≤ 8 counts on every victim | a run whose control curve is already past the criterion, against which every aggressor difference is uninterpretable while still looking like a plausible integer |

**Measured, four complete blocks:** `gates_ok=1` in all four.
`b0` = 3, 5, 10, 2 (bound 64). `lat_max − lat_min` = 133, 133, 133, 136 ns
(bound 200). `lat_mean` = 701–704 ns; `lat_min` = **673 ns in all four, and in
every block the previous image captured** — the instrument reproduces across a
reflash. `timeouts=0` throughout. `adc_khz=6146`, matching
`settle-measured.md` §1's independently measured 6.146 MHz in a different image.
G5: all five victims in band, `zero=0`, `rail=65531`, spreads 0–1, `valid=1`.

**G2 refused one run, and that is the gate working.** In five later reads taken
for the reader's bring-up, the **first** read after the port had sat unattached
came back `b0=85` against a bound of 64, `gates_ok=0`, and the reader exited 1
with *"GATES FAILED (instrument floor) — no crosstalk verdict from this run may
be quoted"*. The four reads after it gave `b0` = 8, 6, 4, 5. **Whether that
means "discard the first block after connecting" is a hypothesis, not a finding**
— it is one occurrence and it needs a probe.

**G6 is near-marginal by construction, not by luck, and that matters.** The
firmware prints only the bit, so the reader recomputes the gate's own quantity
per victim from the row-1 and row-2 curves every block already prints:

| victim | R_src | four blocks | five later reads | bound |
|---|---:|---|---|---:|
| `REF_A` | 5150 Ω | 6, 7, 7, 7 | 7, 6, 7, 6, 7 | ≤ 8 |
| `REF_C` | 5150 Ω | 6, 7, 6, 6 | 5, 6, 5, 5, 5 | ≤ 8 |
| `REF_B` | 650 Ω | 5, 5, 6, 5 | 4, 5, 4, 4, 5 | ≤ 8 |
| `R_SP10` | 150 Ω | 1, 1, 1, 1 | 1, 1, 0, 1, 1 | ≤ 8 |
| `R_LO3` | 150 Ω | 0, 0, 0, 0 | 0, 0, 0, 0, 0 | ≤ 8 |

**One count of margin on the worst victim — and part of that margin is the
control event itself, which is exactly what G6 exists to catch.** The
whole-curve mean of `mean_silent(d) − mean_control(d)`, over all 65 points and
all eight Silent curves per victim, is **+1.68…+2.60 at 5150 Ω, +0.29…+0.88 at
650 Ω and +0.00…+0.06 at 150 Ω**. Twenty-four of twenty-four divider offsets are
positive, none negative, and the offset **orders by source impedance**. Wander
does not do that.

So roughly 2 of G6's 6–7 counts on a 5150 Ω victim are a reproducible effect of
the latch pulse and the shift preceding it, and only the remainder is the node's
wander (§6: ~8 counts over the same points). Two consequences:

- **A G6 failure at 7–9 counts would be partly correct, not a misattribution.**
- **The bound must not be widened without measuring it.** G6's null
  distribution is about twice the node's within-curve spread at 5150 Ω, because
  it is a max over 65 points of a difference of two curves each wandering ~8.
  It is designed to sit near its bound on this board.

## 4. The result: the LED word is the only aggressor that breaks the criterion

Largest per-point excursion of a row's curve against **that victim's own
control**, over the whole 65-point grid, worst of the row's cases —
`max_d |mean_row(d) − mean_control(d)|` in 16-bit counts, four complete blocks:

| row | aggressor | `REF_A` 5150 Ω | `REF_C` 5150 Ω | `REF_B` 650 Ω | `R_SP10` 150 Ω | `R_LO3` 150 Ω |
|---|---|---:|---:|---:|---:|---:|
| 1 | Silent (= G6's floor) | 6 / 7 / 7 / 7 | 6 / 7 / 6 / 6 | 5 / 5 / 6 / 5 | 1 | 0 |
| 3 | `MUX8_EN_N` | 5 / 7 / 8 / 6 | — | 5 / 6 / 4 / 4 | 1 | — |
| 4 | `MUX16_EN_N` | — | 4 / 3 / 2 / 5 | — | — | 0 |
| 5 | `MUX_A3` bare | — | 3 / 4 / 3 / 3 | — | — | 0 |
| 6 | `MUX_A3` + `EN16` | skipped | skipped | — | — | skipped |
| 7 | **LED word** | **13 / 15 / 15 / 13** | **9 / 9 / 9 / 9** | **12 / 13 / 12 / 11** | 1 | 0 |
| 9 | ShiftOnly | 6 / 6 / 7 / 5 | 2 / 3 / 3 / 3 | 6 / 7 / 4 / 5 | 0–1 | 0 |
| 10 | Silent, printing | 7 / 7 / 7 / 9 | 5 / 7 / 6 / 6 | 5 / 5 / 6 / 5 | 0–1 | 0 |

**Read the row-1 line first: it is the floor.** Row 1 is Silent, so its
"excursion against the control" is the difference between two curves in which
nothing was deliberately done differently. Rows 3, 4, 5, 9 and 10 sit at that
floor, with one 1-count exception not rounded away: row 3 on `REF_A` reads 8 in
block 3 against a floor of 7 in the same block.

**But a max-over-65-points statistic is weak, and the honest floor is lower than
it looks.** Two curves each wandering ~8 counts will differ somewhere by around
16 by chance alone, so "13–15 against a floor of 7" is thinner than it appears.
The whole-curve statistic is what settles it, and it was sitting in the same
capture:

| victim | direction | points positive of 65 | points negative | whole-curve mean `delta` |
|---|---|---|---|---|
| `REF_A` | dark→lit | 0 / 2 / 6 / 3 | 63 / 60 / 57 / 57 | **−6.77 / −6.80 / −6.26 / −5.89** |
| `REF_A` | lit→dark | 45 / 50 / 50 / 47 | 10 / 10 / 11 / 8 | **+2.20 / +2.06 / +2.38 / +2.49** |
| `REF_C` | dark→lit | 3 / 9 / 3 / 5 | 58 / 54 / 54 / 56 | **−4.09 / −3.71 / −3.92 / −4.23** |
| `REF_C` | lit→dark | 51 / 49 / 51 / 47 | 7 / 9 / 7 / 11 | **+2.72 / +2.75 / +2.75 / +2.23** |
| `REF_B` | dark→lit | 6 / 9 / 7 / 1 | 56 / 55 / 55 / 57 | **−4.88 / −5.03 / −5.49 / −5.22** |
| `REF_B` | lit→dark | 50 / 53 / 52 / 49 | 5 / 6 / 7 / 13 | **+2.69 / +2.55 / +2.72 / +2.62** |
| `R_SP10` | both | 0–3 | 0–1 | −0.02 … +0.03 |
| `R_LO3` | both | 0 | 0 | **+0.00 in all eight cases** |

**Nearly every point of the dark→lit curve is below its control and nearly every
point of the lit→dark curve is above it — on all three dividers, in all four
blocks — and the two directions of the same aggressor, at the same victim,
against the same control, are separated by about 9 counts of mean.** That is not
a peak that could be an outlier; it is 65 points agreeing.

**The null for that statistic is in the same capture.** Rows 3, 4, 5 and 9 — the
aggressors that do nothing — measured the same way give `|mean| ≤ 0.62` on every
one, with the signs roughly balanced. Row 7 is the only row in the table that is
not in that population.

**Spec §4's own discriminator says the same thing:** *"a disturbance that flips
with the edge is coupling, one that does not is the pulse itself."* It flips, on
all three dividers, in all four blocks.

**The shape**, measured over all three dividers and all four blocks: near zero
at `d = 0`, peaking at **3 400–5 600 ns in the dark→lit direction and
1 400–5 200 ns in the lit→dark direction**, decayed back into the noise by the
end of the grid (tail over 10–12.8 µs: `REF_A` −6…+4, `REF_C` −3…+2, `REF_B`
−5…+5). **The two directions are not symmetric** — dark→lit reaches −13…−15
where lit→dark reaches only +7…+10 on the same victim. Printed, not explained.

**Does it scale with source impedance? Not among the dividers.** 13–15 at
5150 Ω (`REF_A`), 11–13 at 650 Ω (`REF_B`), 9 at 5150 Ω (`REF_C`) — so the
650 Ω channel is nearly as disturbed as one 5150 Ω channel and more disturbed
than the other. `REF_C` is the victim on the 4051 and is the smallest of the
three, which is a per-mux observation, not an impedance one. **No mechanism is
asserted for either fact.**

### The verdict, as the reader computes it

`verdict_basis=envelope` on every row of every block, for the reason in §2.

| row | aggressor | `REF_A` 5150 Ω | `REF_C` 5150 Ω | `REF_B` 650 Ω | `R_SP10` 150 Ω | `R_LO3` 150 Ω |
|---|---|---|---|---|---|---|
| 3 | `MUX8_EN_N` | HELD 6–7 | — | HELD 4–6 | HELD 0–1 | — |
| 4 | `MUX16_EN_N` | — | HELD 3–4 | — | — | HELD 0 |
| 5 | `MUX_A3` bare | — | HELD 2–4 | — | — | HELD 0 |
| 6 | `MUX_A3` + `EN16` | skipped | skipped | — | — | skipped |
| 7 | **LED word** | **BROKE, 13–14** | **BROKE, 8–10** | **BROKE, 11–14** | HELD 1 | HELD 0 |
| 9 | ShiftOnly | HELD 4–6 | HELD 2–4 | HELD 4–6 | HELD 0–1 | HELD 0 |
| 10 | Silent, printing | HELD 6–7 | HELD 5–7 | HELD 4–6 | HELD 0–1 | HELD 0 |

**`REF_C`'s row-7 envelope straddles the criterion** — 8 in one read (a pass), 9
in another, 10 in two more. Three of four fail it; one does not. That is a
marginal result and it is reported as one, not rounded to either side.

## 5. The R-ladder, which speaks before any curve is read

Spec §9 sets the test in advance: *if the 150 Ω victims show the same `delta` as
the 5150 Ω ones, the coupling is not into the node.*

**They show nothing.** Row 7's excursion is 9 to 15 counts on all three divider
victims and **0 or ±1 count on both 150 Ω victims, at every one of the 65 grid
points, in both edge directions, in all four blocks.** `R_LO3` reads `+0` at
every point of every row-7 case in the capture.

**And the obvious objection is answered by the data rather than argued away.**
Both 150 Ω victims are tied to `AGND` and read `mean=0`, the bottom of an
unsigned converter, so a *negative* excursion on them could in principle be
clipped and invisible. But the dividers move **positive** in the lit→dark
direction (+7…+10), and a tie would move positive too — where nothing can clip.
It reads `+0` and `+1`. **The absence is measured in the direction where it
cannot be hidden.**

The same separation holds for every other row: rows 3, 4, 5, 9 and 10 give the
two ties 0 or 1 count against 2–8 on the dividers.

So: **the coupling arrives at the node, not in the ground, the reference or the
ADC.** That is the one structural conclusion this run supports, and it is
supported by the ladder rather than by a model.

## 6. What the silent block settled — `settle-measured.md` §5, both halves

§5 of the predecessor left two questions open. This instrument was built to
answer them and it answers both.

### The 8–12 count wander survives total silence

Row 1 takes the whole grid with **no chain access and no print inside it**, and
50 ms of quiet before it. The board is doing nothing at all.
`settled_mean_spread`, three consecutive blocks:

| victim | R_src | whole grid | `d=0` dropped | §5's measured |
|---|---:|---|---|---|
| `REF_A` | 5150 Ω | 15 / 16 / 17 | 10 / 11 / 12 | 8–12 (dividers) |
| `REF_C` | 5150 Ω | 10 / 11 / 12 | 6 / 6 / 6 | 8–12 |
| `REF_B` | 650 Ω | 6 / 6 / 7 | 6 / 6 / 7 | 9–11 (its own pair, P3) |
| `R_SP10` | 150 Ω | 0 / 0 / 1 | 0 / 0 / 1 | 0–1 (ties) |
| `R_LO3` | 150 Ω | 0 / 0 / 0 | 0 / 0 / 0 | 0–1 |

**The wander is there with the board idle.** It is therefore neither the settle
probe's own 595 clocking nor its USB traffic — it is the node or the reference,
and no later row can be blamed for it.

It does not land squarely on §5's 8–12: `REF_A` sits above it on the whole grid
and `REF_C`/`REF_B` below it. **A caveat has to travel with that comparison:**
this is not byte-for-byte §5's statistic. §5 computed the spread from each
pair's *knee* to the end of the grid; this instrument has no knee and computes
it over the whole grid, which includes the one point §8's effect lands on. The
`d=0`-dropped column is how the two reconcile, and it is mechanism-selective
rather than cosmetic — dropping that point moves the two 5150 Ω victims and
leaves the 650 Ω one and both ties untouched, which is §7's impedance signature
with no free parameter. **~8 counts is the honest idle wander on this node.**

### Printing inside the grid is not the cause

Row 10 is row 1 with a `PrintLine` after every grid point, as the settle probe
does. `settled_mean_spread`, row 10 minus row 1, per block:

| victim | b1 | b2 | b3 |
|---|---:|---:|---:|
| `REF_A` | 0 | +2 | −3 |
| `REF_C` | +5 | 0 | −1 |
| `REF_B` | +1 | −1 | −1 |
| `R_SP10` | +1 | 0 | 0 |
| `R_LO3` | +1 | 0 | 0 |

**The differences run −3 to +5 counts against a block-to-block scatter of the
same size within either row alone**, with no consistent sign for any victim, and
0 or 1 count on the two ties. §5's USB-CDC/DMA candidate is **not** the cause.

One qualification on what that rules out. Row 10 prints 65 lines across roughly
110 ms of grid, one per ~1.7 ms — USB-CDC traffic *concurrent with* the
measurement, which is the condition §5 named. It is not every DMA condition: the
audio SAI DMA is not running in this image and neither is the shipping ADC DMA.
**What is eliminated is specifically the print traffic the settle probe itself
generates.**

### `widest_sample_band`, per pair

§5's single-point bands survive silence too. Compared per pair rather than
against one aggregate band (`settle_plan.cpp:12-17` maps pair to channel):
`R_LO3`'s 20–49 is **inside** its own pair's 10…56, and `REF_B`'s 198/194/201 is
**marginally above** P3's 53…195 in two of three blocks. Neither reverses the
finding; the aggregate band was hiding both.

## 7. The rest of the table

### Row 9 — the datasheet claim, measured for the first time

Spec §10 marks *"a latch-less shift leaves the 595 outputs untouched"* as read
from the 74HC595 datasheet and **unmeasured on this board**.
`MuxScan::shift_chain_timed()` — 16 bits clocked, `RCLK` never pulsed — was
built for exactly this and had never run on hardware before this capture.

| victim | R_src | worst \|delta\|, four blocks | grid mean, control vs row 9 |
|---|---:|---:|---|
| `REF_A` | 5150 Ω | 6 / 6 / 7 / 5 | 32495 vs 32495 |
| `REF_C` | 5150 Ω | 2 / 3 / 3 / 3 | 32577 vs 32577 |
| `REF_B` | 650 Ω | 6 / 7 / 4 / 5 | 32754 vs 32754 |
| `R_SP10` | 150 Ω | 0 / 1 / 1 / 1 | 0 vs 0 |
| `R_LO3` | 150 Ω | 0 / 0 / 0 / 0 | 0 vs 0 |

**On both 150 Ω victims row 9 matches its control to 0 or 1 count** — which is
the condition spec §10 names. On the dividers its 2–7 counts sit inside row 1's
own 5–7 count floor, the worst excursion lands at a different `d` in every block
with no consistent sign, and the grid means are identical to the control's.

**At this instrument's resolution, a latch-less shift does nothing measurable to
the 595 outputs or to any victim.** The bound is the resolution — about 1 count
on the ties and about 7 on the dividers — not zero, and the statement is about
this board at these delays.

### Row 8 — the DC shift under eight LEDs

`SHELL_XTALK_STATIC`: the LED word held at `0x00`, then at `0xFF`, 64
conversions each, no grid. `lit − dark`, four blocks:

| victim | R_src | dark means | lit − dark |
|---|---:|---|---|
| `REF_A` | 5150 Ω | 32500, 32499, 32500, 32500 | 0, +2, −2, −2 |
| `REF_C` | 5150 Ω | 32599, 32601, 32600, 32600 | +1, −2, 0, −2 |
| `REF_B` | 650 Ω | 32763, 32764, 32763, 32762 | −1, −2, −1, 0 |
| `R_SP10` | 150 Ω | 0, 0, 0, 0 | 0, 0, 0, 0 |
| `R_LO3` | 150 Ω | 0, 0, 0, 0 | 0, 0, 0, 0 |

**At most 2 counts on a divider and exactly 0 on both ties, with no consistent
sign on two of the three dividers.** The dark mean's own block-to-block scatter
is 1–2 counts, so `lit − dark` is inside the reproducibility of the measurement
it is drawn from. Only `REF_B` is weakly one-sided across four blocks, which is
one count and not a finding.

**The LED current is derived, not measured:** (3.3 V − V_f) / 1 kΩ ≈ **1.3 mA**
per output at a green LED's ~2 V forward drop, so ~10.4 mA total through the
single ground join. Spec §4's "~2 mA" is an upper bound on the same derivation.
Nothing in this run measures current.

**Rows 7 and 8 are consistent with each other.** Row 7 says the LED *edge* moves
a divider by up to 15 counts and has decayed by 12.8 µs; row 8 says the LED *DC
state* moves it by at most 2 counts after a 20 µs park. A transient that recovers
to a DC level which did not change is what both measure.

## 8. The first-arrival dip, reproduced from a different code path

`settle-measured.md` §7's ~845-count first-arrival dip appears in this
instrument too, and this capture locates it precisely: **it is the first
conversion of each case, the one that follows `select_time()`.**

On the 5150 Ω dividers every case's `d = 0` point carries a raw `min` around
**31 640–31 660** against 32 460+ at every other point, and every Static case's
`min` shows the same. `settle-measured.md` §7's 2026-09-18 repeat measured P1's
`settled_raw_pre` at **31 647…31 655** on the same node — **this is a different
image, a different code path and a different instrument reproducing it to within
a handful of counts.** `REF_B` at 650 Ω shows none of it, which is §7's own
finding that the 650 Ω channel is immune.

One depressed repeat out of 64 moves that point's mean by ~13 counts, which
matches what is printed. That is §7's mechanism arriving from the *previous
case's victim channel*, not from a channel change inside the case — a Latch
case's two words hold the victim's address and enable identical, and the effect
is absent from every point but the first.

**It does not affect any difference in this document**, because control and
aggressor curves carry it equally at `d = 0` and it cancels — `REF_A`'s row-7
`delta` at `d = 0` is −1. It does inflate the printed `settled_mean_spread`,
which is why §6 carries the `d=0`-dropped column.

## 9. Open, characterised, and deliberately unexplained

1. **No mechanism for row 7.** The run says the LED word's edge moves a divider
   by 9–15 counts, that it flips sign with the edge direction, that it peaks in
   the low microseconds and decays by 12.8 µs, and that it does not reach a
   150 Ω tie. It does not say whether that is di/dt through the star point,
   capacitive coupling into the divider node, or something else. **Nothing here
   should be quoted as a mechanism.**
2. **Row 7 does not order by source impedance among the dividers.** 650 Ω is
   more disturbed than one of the two 5150 Ω victims. Unexplained.
3. **`REF_C` straddles the criterion.** 8 / 9 / 10 / 10 across four reads. More
   blocks would say whether it is a pass or a fail; four do not.
4. **The one G2 failure** (`b0=85`, first read after an unattached port) is one
   occurrence. "Discard the first block after connecting" is a hypothesis and
   needs a probe.
5. **`measure_span()`'s four tie reads never got the timeout exclusion** the
   rest of the instrument has (`shell/xtalk_probe.cpp`, `read_parked()` →
   `probe_adc::mean_of_repeats()`). A timeout on a rail tie mostly fails closed;
   **on an `AGND` tie it is invisible**, because the true reading is ~0 and a
   zeroed repeat is indistinguishable. G5 is judged against that span and,
   unlike G6, the reader does not recompute it independently. Not exercised —
   `timeouts=0` on every CAL line of every capture — and recorded in the source.
6. **G6's `controls_compared` counts victims, not points.** A control curve
   whose every point pair was excluded would leave `worst = 0` and print `g6=1`.
   The reader refuses it (`points_compared == 0` → disagreement → exit 1), so the
   deliverable cannot pass on it, but the firmware bit can be wrong.
7. **The 3.1 % `hw.adc.Get()` discrepancy of `docs/gotchas.md` is localised, not
   explained.** The rail tie reads **65 531–65 533 through this probe against
   63 485 through libDaisy** on the same net. That puts the difference in the
   libDaisy path rather than in the board. Observed in passing; not chased.

## 10. What this rests on

One board. One coupon, one populated copy, one Patch Submodule. One session,
**2026-09-18**, five firmware images flashed in sequence: the settle probe
unmodified and then refactored (the two halves of the `probe_adc` proof), the
crosstalk probe's empty skeleton, the silent block, and the full sweep. The
numbers in this document come from the last two — a 3-block silent run, a
4-block full sweep, and five later reads during the reader's bring-up, one of
which G2 refused.

58 cases, of which 44 take a grid; four skipped for want of `RV4`. Five victims:
three dividers spanning 650 Ω to 5150 Ω, and two 150 Ω ties whose job is to read
nothing.

One block is **9.07 s** measured (9.074 / 9.066 / 9.067 / 9.068, `_END` to
`_END`), which is what the reader's default timeout is set from.

What would strengthen it, roughly in order of what each buys:

1. **A second board.** Five bare boards were fabricated; populating a second one
   separates this coupon from the design. Row 7 is the claim most in need of it.
2. **More blocks.** Four put `REF_C` across the criterion boundary and leave it
   there. Whatever uncertainty gets quoted on row 7 has to come out of a count
   larger than four.
3. **A mechanism experiment for row 7.** The sign flip and the 150 Ω absence
   constrain it; nothing here distinguishes the candidates.
4. **`RV4` fitted**, which turns the four skipped row-6 cases into the one
   variant spec §2 flags — the 16:1 switching channel into `ADC_9` while
   `ADC_10` is read.
5. **A second submodule**, which separates the board from the MCU. §9's item 7
   is a reminder that ADC behaviour here has already turned out to be
   configuration rather than silicon.

## 11. What it means for the design

**The LED word is the finding, and it is an actionable one.** Eight simultaneous
current edges through the star point move a 5150 Ω divider by up to 15 counts —
roughly twice the half-LSB criterion — for about 5 µs. The shipping firmware
free-runs its ADC on a circular DMA, so a pot read lands at an arbitrary phase
relative to any LED update, and the envelope is what it can meet.

Three things follow, and none of them is a redesign:

- **The coupling is into the node, not the ground or the reference** (§5). That
  points at the divider side rather than at the star point's topology.
- **The disturbance is transient and recovers.** Row 8 measures the DC shift at
  ≤ 2 counts. Anything that separates an LED update from a conversion by more
  than ~13 µs removes it entirely.
- **Nothing else on the chain registers.** Mux enables, a bare address edge
  across the moat, and a latch-less shift all sit at the control floor.

What this does **not** license is a number for the shipping panel. The coupon's
divider impedances were chosen to span an attribution axis, not to match the
panel's pots, and §2's envelope basis is deliberately the pessimistic reading.
`io-budget.md` should be re-read against §4 rather than updated from it.

## 12. How to repeat it

Two toolchains, and they must not be mixed — the probe is firmware, the reader
is host Python.

```
# firmware, from shell/ (ARM GCC, never source env.sh in this shell)
make -j8 images SHELL_COUPON_PROBE=1 SHELL_XTALK_PROBE=1
dfu-util -a 0 -s 0x90040000:leave -D build/shell-sram.bin

# host, also from shell/: capture one complete block and judge it
python read_xtalk.py COM4 xtalk.csv
```

`SHELL_XTALK_RV4=1` un-skips row 6 once the pot is fitted. Both switches ride in
a generated header rather than a bare `-D`, because a bare `-D` is invisible to
make's dependency graph and this machine has twice produced two byte-identical
images for two different switch positions. **`cmp` the image against the one it
replaces before flashing**, every time.

That writes **two** files and both are needed. `xtalk.csv` carries the grid
points, one row per measured point, with the reader's computed `delta`.
`xtalk.csv.meta.csv` carries everything else the block said — configuration,
clock, calibration, span, the gate verdicts, each victim's G5 band and each
victim's recomputed G6 magnitude — as `scope,key,value` rows. §4's verdict table
cannot be rebuilt from the grid points alone.

The reader accumulates to the end marker and discards an incomplete block. It
exits 1 when a gate failed **or** when a verdict failed, and it distinguishes the
two in words — a gate failure prints *"no crosstalk verdict from this run may be
quoted"* and stops. Its guard is `shell/test_read_xtalk.py`, registered in CTest
as `read_xtalk_guard`; it needs no board and no pyserial.

**A block is 9.07 s and prints ~2 984 lines.** A fused or truncated line can
appear anywhere in it — `$$` is libDaisy's marker, and the mechanism is an
accumulation overflow when the host is not draining (`logger.cpp:78-87`), not a
single line being too long. The completeness rules are what protect the verdict:
five of them, each refusing a corruption the other four cannot see.

**Do not expect `SHELL_XTALK_WARMUP` to arrive.** It is printed once before the
forever loop and `StartLog(false)` does not wait for a host. Everything on it
that matters is reprinted inside the loop on `SHELL_XTALK_GATES`.
