# The settle-time budget — calculated

> **This document is arithmetic, not a measurement, and the probe rule applies
> to it in full.** Nothing here has been on a bench. It does not close Phase-0
> Task 6 step 5b; it turns 5b from a bisection search into a **yes/no
> confirmation with a predicted number**, which is a much cheaper experiment.
> Every number below comes out of [`tools/settle_budget.py`](../../tools/settle_budget.py),
> guarded by `tools/test_settle_budget.py`, so it is reproducible rather than
> quotable:
>
> ```
> python settle_budget.py        # from tools/
> python test_settle_budget.py
> ```
>
> Written 2026-08-30, after the question "isn't this documented somewhere?" —
> and the answer to that question is finding 0 below.

## 0. Why the number is not on the internet

It is not a property of the multiplexer. It is a property of the pot value, the
node capacitance, the ADC's sampling window and the accuracy you want, and three
of those four are ours to choose. What *is* documented everywhere is the method.

Electrosmith's own material is thinner than one would expect, and both facts are
in this repo's submodule:

- The ADC guide defers the topic entirely — "stay tuned for a guide for
  connecting a ton of controls using external multiplexer in the future"
  ([`_a4_Getting-Started-ADCs.md`](../../lib/libDaisy/doc/md/_a4_Getting-Started-ADCs.md)).
  That guide does not exist.
- `InitMux` does not wait at all. The DMA-complete callback writes the new
  address and restarts the DMA on the next line
  ([`adc.cpp:481`](../../lib/libDaisy/src/per/adc.cpp:481)), with the comment
  admitting the shortcut: *"We could set up everything to run from a timer and
  provide a few microseconds to adjust pins before resuming reading, but that
  won't work with how everything is set up right now."* The only allowance is a
  discarded conversion — `adc1_dma_buffer` is documented as *"buffer for trash
  data during mux pin changes"*.

Finding 3 below explains why that discarded conversion is there, and it is not
the reason one would guess.

## 1. Where each constant comes from

The distinction matters more than the values: two of these are read out of the
sources in this repo, two are datasheet tables, and three are estimates that the
model is deliberately built to be insensitive to.

| Constant | Value | Source | Class |
|---|---|---|---|
| ADC kernel clock | 24.58 MHz | PLL3 M=6 N=295 R=32 on 16 MHz HSE, [`system.cpp:494`](../../lib/libDaisy/src/sys/system.cpp:494), source selected at `:515` | read from this repo |
| ADC clock | 12.29 MHz | `ADC_CLOCK_ASYNC_DIV2`, [`adc.cpp:229`](../../lib/libDaisy/src/per/adc.cpp:229) | read from this repo |
| Sampling window (default) | 692 ns | `SPEED_8CYCLES_5`, [`adc.h:59`](../../lib/libDaisy/src/per/adc.h:59) | read from this repo |
| Conversion | 8.5 cycles | STM32H7 RM, 16-bit | datasheet |
| `C_COM`, 74HC4067 | 50 pF | CD74HC4067 datasheet (TI/Harris SCHS209), "Common Capacitance" | datasheet, verbatim |
| `C_COM`, 74HC4051 | 25 pF | CD74HC4051 datasheet | datasheet |
| `R_on` at 3.3 V | 150 Ω | **extrapolated.** Both datasheets stop at VCC = 4.5 V (70 Ω typ, 160 Ω max) and 6 V; neither characterises 3.3 V | estimate |
| Stray capacitance | 15 pF | **estimate.** MCU pin + PCB trace, not measured | estimate |
| `C_ADC` | 4 pF | ST, STM32H7 sample-and-hold | datasheet |
| `R_ADC` | 2 kΩ | ST community figure for slow channels, **not datasheet-verbatim** | estimate |

Two of the three estimates cannot hurt much, and that is by construction:

- **`R_on` is not a risk factor.** At a 10 kΩ pot the wiper alone contributes
  2.5 kΩ, so even 500 Ω of switch resistance would move the binding term by
  13 %. The extrapolation is pessimistic by roughly 2×, and it still does not
  matter.
- **`R_ADC` enters term B only**, and term B is never the binding term (finding
  3). Getting it wrong changes nothing. Which of `A2`/`A3`/`D8`/`D9` are ST's
  "fast" and which are "slow" channels is therefore also unchecked, on purpose.

The stray capacitance is the one estimate that does bite, linearly. See
"What the model cannot see".

## 2. Three terms, not one

| | What has to happen | Time constant |
|---|---|---|
| **A — node settle** | after the address changes, the `COM` node slews from the old channel's voltage to the new one | (R_wiper + R_on) × (C_COM + stray + C_fitted) |
| **B — acquisition** | ST's rule: the S&H cap charges through the source impedance inside the sampling window | (R_ADC + R_src) × C_ADC |
| **C — redistribution** | the S&H cap still holds the **previous** channel's charge and dumps it on the node, which then has to recover through the wiper | (R_wiper + R_on) × (C_node + C_ADC) |

A is paid once per address step. B and C both happen inside the sampling window,
so the window has to cover `max(B, C)`. A linear pot at mid travel is taken as
the worst wiper source impedance, R/4.

For the 74HC4067 with nothing fitted at `COM`, settled to half an LSB of 12 bit:

| pot | R_src | C_node | A settle | B acq | C redist |
|---|---:|---:|---:|---:|---:|
| 10 k | 2.65 kΩ | 65 pF | 1552 ns | 168 ns | **1127 ns** |
| 20 k | 5.15 kΩ | 65 pF | 3016 ns | 258 ns | **2190 ns** |
| 50 k | 12.65 kΩ | 65 pF | 7409 ns | 528 ns | **5379 ns** |
| 100 k | 25.15 kΩ | 65 pF | 14731 ns | 979 ns | **10695 ns** |

## 3. The full sweep

The address lines are shared across every mux — they ride the 595 chain
(io-budget §3) — so a full sweep costs as long as the **busiest sense pin**.
With 65 channels over 4 raw ADC pins that is 9 chips at 8:1 (3 on the busiest
pin, 24 steps) or 5 chips at 16:1 (2 on the busiest pin, 32 steps). One ADC
serves all four pins, so the four conversions per step are sequential while the
address settle is paid once.

Sampling window auto-picked as the smallest libDaisy `SPEED_*` that covers
`max(B, C)`. **Nothing fitted at `COM`:**

| pot | 74HC4051 (8:1, 24 steps) | 74HC4067 (16:1, 32 steps) |
|---|---|---|
| **10 k** | 16.5 cy · **218 µs** · 0.11 block | 16.5 cy · **310 µs** · 0.16 block |
| **20 k** | 32.5 cy · 365 µs · 0.18 block | 32.5 cy · 524 µs · 0.26 block |
| 50 k | 64.5 cy · 680 µs · 0.34 block | 387.5 cy · 4361 µs · **2.18 blocks** |
| 100 k | 387.5 cy · 3310 µs · 1.66 blocks | 387.5 cy · 4595 µs · **2.30 blocks** |

What a capacitor at `COM` costs, at 10 kΩ:

| C at COM | 74HC4051 | 74HC4067 |
|---|---|---|
| none | 218 µs | 310 µs |
| 100 pF | 401 µs | 553 µs |
| 1 nF | 3689 µs | 4938 µs |
| 1 nF at a 100 k pot | *no libDaisy sampling window is long enough* | *same* |

## 4. What it says

**1. The capacitor at `COM` goes away entirely — it does not get smaller.**
Every configuration with a capacitor is worse than the same configuration
without one. The envelope spec says "≤ 1 nF or gone"
([`2026-08-08-fireflow-hardware-envelope-design.md`](../superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md) §5);
this resolves it to **gone**. The intuition it was fighting — a local charge
reservoir helps the S&H — is real but loses: the reservoir's recovery time grows
linearly with the capacitance while the dip it suppresses shrinks only
logarithmically.

**2. The pot value is a real design decision with a hard ceiling, and it lands
on 10 k.** At 50 k the 16:1 falls off a cliff, because term C forces the
sampling window from 64.5 to 387.5 cycles and the step cost multiplies. The
price of 10 k is current: 65 pots at 3.3 V/10 k is **~21 mA** standing on the
3V3 rail (20 k → ~11 mA) — which was the only argument for 20 k, and it does not
hold. The Patch SM datasheet (v1.0.5, Table 1, *Absolute Maximum Ratings*) gives
**3V3 Output = 500 mA**, so the pots are 4 % of it. Two caveats for honesty: that
is an absolute-maximum figure rather than an operating one, and the module's own
consumption comes out of the same budget and is **stated nowhere** — the
footnote only says "Maximum output current is firmware dependent". The
defensible claim is that 21 mA is not the problem, not that 479 mA are free.

Two side findings from the same datasheet, both of which bear on the design.
Electrosmith's own potentiometer application example names an **Alpha 9 mm
Linear 10K** (`RD901F-40-15F-B10K-00D70`) — the vendor's reference design lands
on the same value this calculation does, independently. And a note beside it
closes an option that would otherwise look attractive: *"When using ADC_9 to
ADC_12, use +3V3 OUT (A10) instead of +5V OUT (A6)."* `ADC_9`–`ADC_12` are
exactly the four sense pins. Giving the pots their **own** 3.3 V regulator on the
control PCB to spare the module's is therefore wrong: the conversion is
ratiometric to the ADC reference, and a second regulator's difference from it
appears as a gain error and as noise in every reading. The pots hang on `A10`.
(libDaisy never configures `VREFBUF` — checked; how the board ties `VREF+` is not
verifiable from here, but the instruction stands on its own.)

**3. It is charge redistribution that sets the sampling window, never the
acquisition rule.** Term C beats term B in every configuration considered — the
guard `test_redistribution_is_the_binding_term` exists to notice if that ever
stops being true. This is also the explanation for libDaisy's discarded
conversion: the S&H cap carrying the previous channel's charge is the actual
problem, not the ADC's charging time.

**4. The libDaisy default of 8.5 cycles is too short in every case.** The
hand-written scan needs at least `SPEED_16CYCLES_5`, and 32.5 at 20 kΩ. This is
a concrete firmware setting that came out of the paper round rather than out of
bring-up.

**5. Timing does not decide 8:1 against 16:1.** The 4051 wins twice — half the
`C_COM`, *and* fewer steps per sweep, because nine chips distribute over four
sense pins better than five do (24 against 32) — but at 10 k and 20 k both are
comfortably inside one audio block. The choice therefore stays where io-budget
§6 put it: availability, assembly price, and 595 outputs (28 against 31 of 32).
It only becomes a timing question at 50 kΩ and above.

**6. One arithmetic claim elsewhere is far too pessimistic, and it is worth
correcting.** [`docs/bench/2026-08-23-978cbaf-shell-mux-placement.md`](../bench/2026-08-23-978cbaf-shell-mux-placement.md)
computes "~15.6 Hz per channel" for a full sweep. That figure is correct for
what was measured — the probe deliberately paced **one step per audio block**,
and it is labelled as arithmetic there, not as a measurement. But it is not a
property of the design: at 10 kΩ a **complete** sweep of all 65 channels fits in
0.16 of one block, so the ceiling is the block rate itself, **~500 Hz per
channel**, about 32× the figure that has been carried around. The capture is
left as written; it is an honest record of its own experiment.

**The model reproduces the only settle number that already existed in the
repo.** Phase-0 Task 6 step 5 says a 10 nF capacitor at `COM` makes τ ≈ 26 µs;
the model gives 26.7 µs, which also reveals that the plan was silently assuming a
10 kΩ pot. That cross-check is a guard
(`test_model_reproduces_the_plans_own_ten_nanofarad_figure`), not a remark.

## 5. What the model cannot see

Single-pole RC, and nothing else. It carries none of the following, and 5b on
the bench is what covers them:

- **Crosstalk between adjacent channels.** This is precisely what the plan's
  test rig probes by pulling the neighbours of the measured channel to opposite
  extremes — and it is the failure the model is blindest to.
- **Charge injection** from the address switching itself.
- **The stray capacitance**, at 15 pF an estimate and the one estimate that
  scales the result linearly. At 10 kΩ there is a factor of 6 of headroom inside
  a block, so the layout would have to be far worse than assumed before the
  topology is in danger — but the *number* would move.
- **Anything about the audio artifact.** The 500 Hz series
  ([`2026-08-23-978cbaf-artifact-triage.md`](../bench/2026-08-23-978cbaf-artifact-triage.md))
  is a separate question and this document says nothing about it.
- **Leakage**, and the temperature dependence of `R_on` (the datasheet's max
  rises from 160 Ω at 25 °C to 200 Ω at 85 °C — inside the insensitivity margin
  above, but not zero).

## 6. What step 5b becomes

Not a bisection search from 50 µs downward, but a prediction to confirm or
break:

> At a 10 kΩ pot, nothing fitted at `COM`, and `SPEED_16CYCLES_5`, a channel
> change must read clean **1.6 µs** after the address is written, with the two
> neighbouring channels held at opposite extremes.

If it does, the topology is settled and the remaining question is only 8:1
against 16:1 on price. If it does not, the gap between prediction and
measurement names which of the three estimates in §1 was wrong — which is worth
more than a bare number.

**Where it gets confirmed is the test coupon, not a breadboard**, decided the
same day this was written. The reason is the model itself: it is **linear in the
node capacitance**, and a jumper-wire node is an unknown multiple of the 65 pF
assumed here, so the 1.6 µs above has no defined value on a breadboard at all.
For the settling term a breadboard pass would at least be conservative — more
capacitance, slower — but for crosstalk and for noise beside the audio path even
that fails, because the coupling paths differ in kind rather than in degree: a
pass proves nothing and a failure explains nothing. The coupon was always where
the envelope spec put this measurement; what changes is that the paper round has
removed the three layout decisions that used to be blocked behind it.

What the coupon has to carry to make the confirmation possible is an eight-point
list in
[`2026-08-08-fireflow-hardware-envelope-design.md`](../superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md) §5
(addendum of 2026-08-30) — among them a probe point on `COM`, both chip
footprints, 10 k and 20 k side by side on the same mux, fixed-divider reference
channels that separate a noisy scan from a noisy pot, and the real 595 chain on
`B7`/`B8`/`D1`/`D10` so the callback measurement of 2026-08-23 is repeated with
something actually hanging on those pins.

The breadboard build in Phase-0 Task 6 step 5 keeps its place — as the bring-up
rig for the scan firmware, which does not exist yet and should not first be
debugged on a freshly routed board, where a firmware bug and a layout bug look
alike. It is not a measuring rig, and no number in this document may be closed
against it.
