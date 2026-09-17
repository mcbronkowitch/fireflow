# The coupon settle probe — design

**Date:** 2026-09-17
**Status:** design, unbuilt
**Closes:** the instrument half of Phase-0 Task 6 step 5b, on the test coupon

## 1. What this is

A firmware probe that measures how long after a mux address change the
Patch Submodule's ADC reads a settled value, on the test coupon
(`hardware/coupon/`). It answers the prediction in
[`docs/hardware/settle-budget.md` §6](../../hardware/settle-budget.md):

> At a 10 kΩ pot, nothing fitted at `COM`, and `SPEED_16CYCLES_5`, a channel
> change must read clean **1.6 µs** after the address is written, with the two
> neighbouring channels held at opposite extremes.

It is **not** a panel scan, not a bring-up test, and not shipping code. It
produces one CSV over USB-CDC and nothing else.

**It depends on the coupon bring-up probe having passed first.** A settling
curve read off a board whose 595 chain has not been proven to latch is
uninterpretable — a firmware bug and a layout bug look alike, which is the
same argument `settle-budget.md` §6 makes for not measuring this on a
breadboard. The bring-up probe is a separate, smaller piece of work; this
spec assumes it exists and has printed a clean channel table.

## 2. Why through the ADC and not at the node

The coupon carries `TP_COM16`/`TP_COM8` precisely so an oscilloscope can watch
the node (requirement 3). There is no oscilloscope on this bench, so that path
is unavailable.

That turns out to cost less than it looks. The §6 prediction says the change
must **read** clean — read by the ADC. Measuring through the ADC therefore
measures the production question directly, with the sample-and-hold's own
charge redistribution (`settle-budget.md` §2, term C — the binding term at
every pot value in the table) inside the measurement rather than beside it. A
scope would have measured the node, which is the input to the question and not
the question.

What is lost: the node's own waveform, so a failure cannot be decomposed into
"the node was late" versus "the acquisition was short". §7 recovers part of
that with an on-board zero reference.

## 3. Why libDaisy's ADC cannot be used as-is

Three facts, all read from this repo:

| Fact | Source | Consequence |
|---|---|---|
| The patch_sm init runs all 12 channels as one free-running DMA scan | [`daisy_patch_sm.cpp:322`](../../../lib/libDaisy/src/daisy_patch_sm.cpp) calls `adc.Init(adc_config, ADC_LAST)` | `GetAdcValue()` has no defined time relation to the address write |
| That init takes the default `OVS_32` | [`adc.h:113`](../../../lib/libDaisy/src/per/adc.h) — `Init(..., OverSampling ovs = OVS_32)` | every reported value is 32 accumulated conversions; nothing at 1.6 µs resolution survives it |
| `AdcHandle::Start()` recalibrates every time | [`adc.cpp:385`](../../../lib/libDaisy/src/per/adc.cpp) — `HAL_ADCEx_Calibration_Start()` before `HAL_ADC_Start_DMA()` | a "stop, latch, wait `d`, start" loop puts a calibration between the latch and the aperture, orders of magnitude longer than the `d` being resolved |

The third one closed the cheap route. There is no way to get a single
conversion at a chosen delay through the public `AdcHandle` API.

## 4. The instrument

**The probe owns ADC1; libDaisy still owns the pins.** Sequence at startup:

1. `hw.Init()` as usual. This configures A2/A3 as analog inputs and brings up
   the ADC clock tree through libDaisy's `MspInit`. None of that is duplicated.
2. `hw.StopAdc()` (public on `DaisyPatchSM`). ADC1 is now free.
3. The probe configures its own `ADC_HandleTypeDef` on ADC1: one channel at a
   time, `ADC_SOFTWARE_START`, **no DMA**, **no oversampling**, 16-bit,
   `ADC_CLOCK_ASYNC_DIV2` (unchanged, so the 12.29 MHz of `settle-budget.md`
   §1 still holds), sampling time `ADC_SAMPLETIME_16CYCLES_5` — the setting the
   §6 prediction names.
4. `HAL_ADCEx_Calibration_Start()` **once**, here, and never again inside the
   measurement loop. This is the whole reason the public API was unusable.

`HAL_ADC_ConfigChannel()` runs when the sense pin changes — once per channel
pair, never inside the timed path.

**Timing.** The DWT cycle counter at 480 MHz, 2.08 ns per tick. `bench/cycles.h`
already has it including the Cortex-M7 `LAR` unlock, which is the non-obvious
half. `shell/` gets its own copy rather than including across trees:
`shell/README.md` is explicit that exactly one file is shared with `bench/`
(`src/hw/board.h`) and that this is deliberate. The copy carries a comment
naming `bench/cycles.h` as its origin so the trap is not re-learned.

**t = 0 is the latch edge**, not the start of the bit-bang. `write_chain()`
clocks 16 bits before the address reaches the mux at all; the address becomes
visible at the 595s' `RCLK` rising edge. The timestamp is taken immediately
after `latch_.Write(true)`.

One measurement:

```
park:    write_chain(step for from_ch); wait kParkNs   (fully settled)
latch:   write_chain(step for to_ch);  t0 = cycles()   (t0 after RCLK rises)
wait:    spin until cycles() - t0 >= d_cycles
sample:  HAL_ADC_Start(); poll EOC; value = HAL_ADC_GetValue()
```

The spin loop's granularity is a few cycles, i.e. ~10 ns against a `d` grid in
100 ns steps. Polling happens *after* the aperture opens and so costs nothing
but wall-clock.

## 5. The board profile

`shell/mux_plan.h` describes the **production** panel: four 595s
(`kChainBits = 32`), 19 LEDs from bit 8, eight mux chips in two groups. The
coupon is a different board: two 595s = 16 bits, eight LEDs, one 4067 and one
4051. Overwriting the production numbers would silently invalidate the CPU-cost
measurement that `SHELL_MUX_PROBE` exists to make.

So the chain layout becomes a `constexpr` **profile struct**, with two
instances — `kPanelChain` and `kCouponChain` — and `step_pattern()` /
`chain_word()` take the profile as an argument. The firmware picks one at
compile time; `tests/test_mux_plan.cpp` exercises both. No `#ifdef` enters the
logic.

The coupon profile, derived from [`netlist.py:268`](../../../hardware/coupon/scripts/netlist.py)
and the 595 shift order (`U_SR1.QH'` feeds `U_SR2.SER`, so the first bit
clocked travels furthest, and `write_chain()` clocks MSB first):

| bit | lands on | net |
|---:|---|---|
| 0–3 | `U_SR1` QA–QD | `MUX_A0..MUX_A3` |
| 4 | `U_SR1` QE | `MUX16_EN_N` (group 0) |
| 5 | `U_SR1` QF | `MUX8_EN_N` (group 1) |
| 6–7 | `U_SR1` QG–QH | `LED_1`, `LED_2` |
| 8–13 | `U_SR2` QA–QF | `LED_3`..`LED_8` |
| 14–15 | `U_SR2` QG–QH | open |

Address at 0–3 and enables at 4–5 are the same shifts the panel profile
already uses, so only `kChainBits` (32 → 16) and the LED field (19 bits at
shift 8 → 8 bits at shift 6) actually differ. **This mapping is derived, not
measured** — see §10.

Two further coupon facts the profile carries: only two sense pins are
populated (`ADC_9` = `SENSE_16` on the 4067, `ADC_10` = `SENSE_8` on the 4051;
`ADC_11`/`ADC_12` reach test points only), and the 4051 ignores `A3`, so its
steps 8–15 alias onto 0–7 and are not scanned.

## 6. What gets measured

Six channel pairs. Each is a step from a fully settled source channel to a
target channel, chosen so the target's source impedance is known from the
netlist and nothing depends on a part that is not fitted.

| # | mux / sense | from | to | R_src at target | derived 9.01 τ |
|---|---|---|---|---:|---:|
| P0 | 4067 / `ADC_9` | ch1 `R_HI1` (A+3V3) | ch10 `R_SP10` (AGND) | 150 Ω | 88 ns |
| P1 | 4067 / `ADC_9` | ch7 `R_LO2` (AGND) | ch8 `REF_A` | 5150 Ω | 3016 ns |
| P2 | 4067 / `ADC_9` | ch5 `R_HI2` (A+3V3) | ch8 `REF_A` | 5150 Ω | 3016 ns |
| P3 | 4067 / `ADC_9` | ch7 `R_LO2` (AGND) | ch9 `REF_B` | 650 Ω | 381 ns |
| P4 | 4051 / `ADC_10` | ch7 `R_LO4` (AGND) | ch6 `REF_C` | 5150 Ω | 1856 ns |
| P5 | 4051 / `ADC_10` | ch5 `R_HI4` (A+3V3) | ch3 `R_LO3` (AGND) | 150 Ω | 54 ns |

`9.01 τ` is `ln(8192)`, the model's own half-LSB-of-12-bit criterion, with
`C_node` = 65 pF on the 4067 and 40 pF on the 4051 (`settle-budget.md` §1:
50 pF and 25 pF of `C_COM` plus 15 pF of stray). These are **predictions from
the model, not measurements** — the point of the run is to break or confirm
them. Note that `REF_A` at 5.15 kΩ sits exactly on the model's 20 kΩ-pot row,
and `REF_B` an order of magnitude below it, so the two reference channels
bracket the pot range the panel will actually use.

**Delay grid:** 0 to 6400 ns in 100 ns steps (65 points). Plus one settled
reference read per pair at `kParkNs` after the latch, which is the value the
curve is compared against — the target is measured, not assumed.

**Repeats:** 64 per point, reported as mean, min and max. `min`/`max` are not
decoration: they are where jitter in the start-to-aperture latency would show
up, and a curve with a wide band is a curve that must not be read as a time.

Run cost is roughly 6 × 65 × 64 × (park + convert) ≈ 3 s.

**The reported result per pair** is `d_settle`: the smallest grid point whose
mean is within half an LSB of 12 bit (8 counts at 16-bit) of that pair's
settled reference, and stays within it for every larger `d`. That is the number
that meets or breaks §6.

## 7. The zero point is measured, not assumed

`HAL_ADC_Start()` does not open the aperture instantly; there is a fixed
latency of a few ADC clock cycles (81 ns each at 12.29 MHz) plus whatever the
HAL call itself costs. Left unexamined, that latency is a systematic offset
sitting on top of every reported time.

Pairs **P0 and P5 remove it**. Both step between channels tied hard to a rail
through 0 Ω — `R_HI1`/`R_SP10` on the 4067, `R_HI4`/`R_LO3` on the 4051 — so
the only source impedance is the switch itself and the derived settle is 88 ns
and 54 ns, at or below one grid step. A curve there that rises over hundreds of
nanoseconds is not the board; it is the instrument. `d_settle(P0)` is therefore
subtracted from the other 4067 pairs and `d_settle(P5)` from the 4051 pair, and
both are reported raw as well.

This also gives the run its honesty check. If P0's band is wide or its knee
sits far out, the instrument is not good enough to make the claim, and the run
says so instead of quietly reporting a board defect.

## 8. Output

One line per pair and grid point on USB-CDC, integers only — `PrintLine()` is
the lightweight printf and the existing probes stay on `%d` for that reason:

```
SHELL_SETTLE pair=%d sense=%d from=%d to=%d d_ns=%d n=%d mean=%d min=%d max=%d
```

Preceded by one configuration line (sampling time in ADC cycles, ADC clock in
kHz, repeats, grid step and count, park time, firmware git hash) and closed by
`SHELL_SETTLE_END`, so a reader knows it has the whole set rather than
guessing. The block repeats forever with a delay, like `SHELL_CPU` does, since
there is no handshake and the host may open the port late.

`shell/read_settle.py` collects one complete block and writes it as CSV.
`pyserial 3.5` is installed on this machine (checked 2026-09-17), so it follows
`read_probe.py` directly; unlike that script it must accumulate until
`SHELL_SETTLE_END` rather than return on the first matching line.

## 9. How this can go red

The firmware's timing cannot be tested on the host. Everything around it can,
and that is where the RED proof lives:

- **`tests/test_mux_plan.cpp` already exists** and is wired into
  `CMakeLists.txt:93`. Introducing the profile struct breaks it — it asserts
  against 19 LED bits (`0x7FFFFu`) today. It gets extended to run every
  existing assertion against both profiles, and gains the coupon bit table of
  §5 as explicit expectations. Prove the red once by asserting the coupon
  profile's LED mask and watching it fail before the profile exists.
- **The delay grid and pair table are pure data** and get host assertions:
  monotonic grid, every channel in the pair table inside its mux's range, every
  target's expected source impedance matching what `netlist.py` wires.
- **On the board**, §7's P0/P5 pairs are the self-check. They are a gate, not a
  courtesy: if the instrument response does not collapse to the first grid
  step, no number from this run may be quoted.

## 10. What is read, what is derived, what is unmeasured

Following `settle-budget.md` §1's discipline, because the whole point of this
probe is that nobody has measured any of it yet.

| Claim | Class |
|---|---|
| Sense pins are `ADC_9`/`ADC_10` = A2/A3 | read — `netlist.py`, `daisy_patch_sm.cpp:18`, io-budget §3 |
| libDaisy's scan is free-running, `OVS_32`, recalibrating on `Start()` | read — `adc.cpp`, `adc.h` |
| The coupon's 595 outputs carry address/enable/LED as tabulated | read — `netlist.py:268` |
| **Which chain bit lands on which 595 output** | **derived** from MSB-first clocking and `QH'`→`SER` order. The eight LEDs are the free proof at bring-up: a wrong order lights a wrong pattern |
| 9.01 τ settle predictions in §6 | derived from `settle-budget.md`'s model, which is itself three estimates deep |
| `HAL_ADC_Start()` latency is fixed and small | **unmeasured** — this is what §7 exists to find out, and the run is void if it is false |
| That the 595s accept `write_chain()`'s undelayed clock edges at 3V3 | **unmeasured** — `mux_scan.cpp` flags it; the bring-up probe answers it, not this one |

## 11. Out of scope

- Requirement 8 (split versus joined supply planes) **cannot be met on this
  board at all**, and a second copy does not recover it. `AGND` and `A+3V3`
  reach the rest of the world through `JP_GND` and `JP_3V3` and through
  nothing else: the Eurorack header grounds pins 3..6 to `GND`
  (`netlist.py:139`) and the submodule's A10 feeds `+3V3` (`:97`). Open both
  jumpers and the analog island is unpowered and unreferenced — both muxes
  lose VCC. The "without" half of the A/B is not a second topology, it is an
  off state. An earlier draft of this section said a second board with the
  jumpers open would do it; that was wrong.

  What the open state is still worth, and only before the jumpers are ever
  bridged: meter `TP_AGND` against `TP_GND` and `TP_A3V3` against `TP_3V3`.
  Both must read open. Continuity there means copper crosses the 1.0 mm moat
  somewhere it should not, which is a defect in exactly the separation
  requirement 8 is about — and it is invisible for good once the jumpers are
  soldered. Joining the two planes a second time by wire is not an option
  either: the only two places both are probeable are `TP_AGND` at (10.00,
  66.00) and `TP_GND` at (94.00, 3.00), 105.0 mm apart — the board's full
  diagonal, and a wire that long is a component, not a connection.
- Requirement 2's "with a capacitor at `COM`" case needs `C_COM16`/`C_COM8`
  fitted, i.e. also a second copy.
- Crosstalk with a pot as the measured channel (the literal §6 wording, with
  neighbours at opposite rails) is the same probe with a different pair table,
  once the pots are fitted. The pair table is data; adding those rows is not a
  second piece of firmware, and this spec deliberately makes that possible
  rather than hard-wiring the reference channels.
- The audio artifact of `2026-08-23-978cbaf-artifact-triage.md`. Unrelated.
