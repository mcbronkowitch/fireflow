# The scan budget — arithmetic on measured constants

> **This is the scan-budget pass that `io-budget.md` §6 and
> `pots-measured.md` §11 left open.** It puts the coupon's measured constants
> against the audio block and the CPU reserve, for every placement the panel
> scan could take. No board was used; every input was measured on one, except
> where a row says otherwise.
>
> **The answer, in one line:** the pattern the shell already runs — libDaisy's
> free-running DMA over twelve channels at `SPEED_16CYCLES_5` and `OVS_32`,
> one mux step written per audio block and read back one block later — fits
> the block with **~300 µs to spare**, costs **no measurable CPU**, and at
> pot impedance reads within **2 counts** of the 387.5-cycle rung. **The long
> rung is not needed, because this pattern never idles**, and the wait effect
> only happens after an idle. A full sweep takes **64 ms on the 16:1 (15.6 Hz per
> channel), 48 ms on the 8:1 (20.8 Hz)**.
>
> One input carries the verdict and is **documented, not measured**: that
> oversampling runs one channel's 32 conversions back to back (§3). It is the
> first thing the panel scan's bring-up has to confirm on the coupon (§7).
>
> *(Done 2026-09-28: [`scan-measured.md`](scan-measured.md). One block after
> the write, every coupon channel reads its parked value to within 0.61
> counts, with the engine running at the fixed operating point.)*
>
> Every number comes out of [`tools/scan_budget.py`](../../tools/scan_budget.py),
> guarded by `tools/test_scan_budget.py`, which ctest runs as
> `scan_budget_guard`:
>
> ```
> python tools/scan_budget.py
> python tools/test_scan_budget.py
> ```

## 1. The question

Rounds one to four measured how long a mux step takes to settle (≤ 4.8 µs,
`pots-measured.md` §11) and what a read costs in accuracy when the converter
has idled before it (a 20 k pot at mid travel reads ~650 counts low after a
2 ms idle, `pots-measured.md` §5). Both reports then said the same thing: the
long 387.5-cycle rung removes the error, but nobody had asked what a long
conversion costs "times every channel the panel reads" (`wait-measured.md` §9).

That is this document. It asks three things per placement: does a clean value
arrive in time, what does the callback pay for it, and does the coupon say the
value is right.

## 2. The inputs, and what each one is

| input | value | class |
|---|---:|---|
| ADC conversion clock | 6.146 MHz | **measured**, 2026-09-17 (`settle_budget.py`) |
| one conversion at 16.5 / 387.5 cycles | 4.07 / 64.4 µs | **derived**: (rung + 8.5) / clock |
| settle after an address step, worst seen | 4.8 µs (20 k pot, 4067) | **measured**, `pots-measured.md` §11 |
| audio block | 2000 µs (96 @ 48 kHz) | shell configuration |
| CPU reserve, worst-case instrument | 2.9 points = 58 µs per block | **measured**, 2026-08-19 bench |
| one step per block in the callback (32 chain bits, four DMA-buffer reads) | < 0.2 points | **measured**, 2026-08-23 bench, below resolution |
| libDaisy's sequence: 12 channels, `OVS_32`, circular DMA | — | **read** from the source (`daisy_patch_sm.cpp:301-322`, `adc.h:118`, `adc.cpp:240-246`) |
| the shell's rung, all 12 channels | 16.5 cycles | **read**, `shell/main.cpp`, `adc_use_measured_sampling_time()` |
| oversampling order: all 32 of one channel, then the next | — | **documented**, `stm32h7xx_hal_adc.h:848` — *not measured* |
| pot positions on the plate | 73 | **run**, `gen_hw_panel.py` (§5) |

The 387.5-cycle rung is "63 µs of acquisition" in `wait-measured.md` §9; with
the 8.5 conversion cycles on top, a whole conversion is 64.4 µs.

## 3. The shipping pattern fits one step per block

The shell does not convert on demand. libDaisy runs the ADC continuously over
all twelve channels, each channel as a group of 32 conversions averaged in
hardware, and DMA writes each group's result into a buffer the callback reads.
The scan therefore only has to write the next mux address; the ADC delivers
the value on its own.

A value is clean if the group it came from **started after** the address was
written and settled. Written at the start of block *N* and read at the start of
block *N + 1*, the newest completed group for a pin is at most one rotation
old, and it started one group before it completed. So:

    rotation + group  ≤  block − settle
    1562 µs + 130 µs  ≤  2000 µs − 4.8 µs        slack ≈ 303 µs

**16.5 cycles is the longest rung that fits at `OVS_32`.** 32.5 cycles rotates
in 2562 µs and does not fit (guarded as an iff over the whole ladder).

**The verdict rests on the oversampling order.** If the hardware interleaved
the channels inside an oversampling run instead of finishing one channel
first, a group would span a whole rotation and the inequality would become
2 × 1562 µs, which does not fit. The HAL documents the channel-at-a-time order
("all conversions of oversampling ratio are done from 1 trigger"); nobody has
timed it on this board.

## 4. Every placement, side by side

At pot impedance (5150 Ω, a 20 k pot at mid travel), one step per block:

| | placement | rung | ovs | fits / callback | accuracy |
|---|---|---:|---:|---|---|
| **F1** | free-running (the shell today) | 16.5 | 32 | **fits, 303 µs slack; < 0.2 points** | **measured**: 31736 against 31738 at 387.5 (`settle-measured.md` §7) |
| F2 | free-running | 387.5 | 32 | rotation 24.7 ms — **no fit** | measured, 31738 |
| F3 | free-running | 387.5 | 1 | fits, 1158 µs slack | **unmeasured** in this pattern; single raw conversions |
| B1 | blocking, in the callback | 16.5 | 1 | 1.1 points — inside the reserve | **unmeasured**; the nearest measured case, 2.5 cycles after a 2 ms idle, read 440–650 counts low |
| B2 | blocking, in the callback | 387.5 | 1 | **13.1 points** — 4.5× the reserve | measured: flat at every wait (`wait-measured.md` §5, `pots-measured.md` §7) |
| B3 | blocking, in the callback | 16.5 | 32 | **26.3 points** | unmeasured |

**Why F1 is right and B1 is suspect, from the same rung:** the wait effect is a
function of the idle before a conversion (`wait-measured.md` §3). F1 never
idles: its conversions run back to back, each group's first one arriving from a
different channel. That arrival case was measured in libDaisy's own rotation
on 2026-09-18 (`settle-measured.md` §7's second table), and at 16.5 cycles it
reads what the long rung reads. B1 is the pattern the wait probe measured:
convert, idle a block, convert.

**B2 is what the pot reports pointed at, and the callback cannot pay for it.**
Four long conversions per step, waited for, cost 262 µs of a 2000 µs block
against a reserve of 58 µs.

**F2 is the long rung in libDaisy's pattern**, and it is too slow by an order
of magnitude: at `OVS_32` a rotation takes 24.7 ms, twelve blocks. **F3 fits**
by switching oversampling off, but it gives up the 32-fold average for single
raw conversions, whose noise at this rung nobody has measured; at the settle
probe's short rungs single raw conversions scattered over bands of 10…197
counts (`settle-measured.md` §5).

## 5. The panel: 73 channels, 32 or 24 steps

`io-budget.md` §3 carries **65 mux channels**. That was 67 positions minus the
two `REC` pads, before 2026-08-30. The plate had **70 pot positions** from then
until the 9 mm raster (`gen_hw_panel.py`: 72 pot-class params, `STAGES_A/B`
sharing `ATTACK_A/B`'s knob; `REC_A/B` and `MODBTN` are keycaps). It now has
**73**: the raster adds three reserved pots, `ROOT_A`, `ROOT_B` and `REV_MOD`
(`HW_ONLY` entries of class S). They send nothing, but they are read through a
mux channel like every pot (spec 2026-10-07 §4 and §8), so the count includes
them. The step counts do not move, because the address lines are common and a
sweep is as long as the busiest sense pin:

| chip | chips for 73 | spare channels | steps | one step per block |
|---|---:|---:|---:|---|
| 74HC4067 (16:1) | 5 | 7 | 32 | **64 ms, 15.6 Hz per channel** |
| 74HC4051 (8:1) | 10 | 7 | 24 | **48 ms, 20.8 Hz per channel** |

`settle-budget.md`'s sweep figures, computed at 65, stand for the same reason.

The 8:1 still scans a quarter faster, as `settle-budget.md` finding 5 found. The
recount cost it a chip (ten, not nine); both options now leave seven spare
channels. Rev A (ten 8:1 chips) spends two of those on the calibration
channels, which leaves five (`hardware/reva/panel-map.json`).

## 6. Going faster

**Two steps per block are arithmetic, not a plan.** Inside the free-running
pattern they need the rotation halved: `OVS_16` at 16.5 cycles fits
(781 + 65 µs against a 995 µs half-block), `OVS_32` does not. That would give
32 ms / 31 Hz on the 16:1 and 24 ms / 42 Hz on the 8:1, at half the hardware
averaging. It also needs the second address written in the middle of a block,
which the audio callback cannot do. Something else would have to write it, a
timer interrupt for instance. That is neither of the two placements measured
on 2026-08-23, and the foreground one raised the block-rate artifact by 7.3 dB
(`io-budget.md` §6). Unmeasured, and not proposed.

**`settle-budget.md`'s "~500 Hz per channel" is ADC time, not a placement.** A
whole sweep does take 0.16 of a block at the model's settle. But a sweep
converted on demand is a blocking placement (§4, B-rows) and costs its full
duration in the callback. The free-running pattern is limited by its rotation,
not by the settle.

Whether 64 ms, or 48, is fast enough is not an ADC question. It is a question
of feel for the panel and of the parameter smoothing behind it, and nothing
here measures either.

## 7. What this rests on, and what to measure first

| claim | class |
|---|---|
| F1 fits, 303 µs slack | **derived** from the measured clock and settle, and the documented oversampling order |
| F1's callback cost | **measured** (2026-08-23), for the same per-block work |
| F1 reads at 16.5 what 387.5 reads | **measured** on `REF_A` (a 5150 Ω divider) in libDaisy's rotation — **not on a pot**, and not with the mux stepping |
| B2's cost, F2's rotation | **derived** |
| B1, B3, F3 accuracy | **unmeasured** |
| 73 channels, 32/24 steps | **run** (`gen_hw_panel.py`) and **derived** |

**First, when the panel scan is written:** step the coupon's mux once per block in
the shell's own pattern and compare each channel with its long-rung reading
from `pots-measured.md`. That one run tests the oversampling order, the slack
and F1's accuracy on real pots under a stepping mux, the three things this
document assumes. Until then, the 303 µs slack is 15 % of a block and the only
margin there is.

*Done 2026-09-28, in [`scan-measured.md`](scan-measured.md):* on all 24
coupon steps, five run blocks, the shipping pattern reads within 0.61 counts
of the same channel parked, with the engine playing at the shell's fixed
operating point — the outcome the documented oversampling order predicts. The
comparison is against a parked read in the same image rather than the
long-rung readings, which come from a different path and scale. The slack's
size stays derived, and other engine loads are reasoned, not measured.

Also on record, from outside this calculation: everything read through
libDaisy sits about 3.1 % low at full scale (the rail reads 63485,
`settle-measured.md` §7; cause open, `docs/gotchas.md`). That is a gain
the scan maps out, not a timing question.

## 8. How to repeat it

    python tools/scan_budget.py        # prints §2–§6
    python tools/test_scan_budget.py   # every claim above, as a check

The guard reads the pot count from the panel generator, so it goes red when
the plate changes. That is deliberate. Re-read §5 and `io-budget.md` §3 in
the same commit.
