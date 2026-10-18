# P6b-2 on the coupon: lamp step count and pot-touch threshold

## Session 1 - the probe image

Measured on **2026-10-18**, one session, one coupon board, with the probe image
built from commit **`356458f5`** (Task 1's dim demo plus `SHELL_SPREAD`).
Spec: [`2026-10-18-rev-a-p6b2-mod-keys-leds-design.md`](../superpowers/specs/2026-10-18-rev-a-p6b2-mod-keys-leds-design.md).
Both numbers below were read off the board; neither is derived from a model.

**Result:** `kLedSteps = 8` (`control/led_law.hpp`), `kPotTouch = 144`
(`shell/pot_owner.h`).

### 1. LED step count

**Procedure.** LED_1 held at duty 1 of N (the dimmest non-zero level of an
N-step raster), with N cycled through the probe's presets. Bastian looked at the
lamp directly and in peripheral vision, where flicker shows first.

| N | verdict |
|---:|---|
| 4 | no visible flicker |
| 6 | no visible flicker |
| 8 | no visible flicker |
| 16 (the darkest preset) | flickers |

**Rule:** `kLedSteps` is the largest tested N (4, 6, 8, 16) without visible flicker. That is **8**,
the same as the provisional value the code carried; the measurement confirms it.
The verdict is by eye, on one lamp at its dimmest level.

### 2. Pot-touch threshold

**Procedure.** RV2, RV4 and RV6 were left untouched for 20 minutes (1201
one-second windows) while LED_1 switched at duty 1 of 4, so the lamp switching
current was present on the board the whole time. For each window the probe
reported the raw word's max minus min per pot.

| pot | position (normalised, x1000) | worst max-min in a window |
|---|---:|---:|
| RV2 | 573-574 | 60 counts |
| RV4 | 392-393 | 67 counts |
| RV6 | 206 | 29 counts |

**Rule** (fixed in the plan before the measurement):
`kPotTouch = max(2 x worst spread, 3 x kPotHysteresis)`, rounded up to a
multiple of 16.

- 2 x 67 = 134
- 3 x `kPotHysteresis` (16) = 48
- max(134, 48) = 134, rounded up to a multiple of 16 = **144**

The worst spread is the worst pot, RV4, over 1201 windows. A pot on the real
Rev A board may be noisier or quieter; P7 sees all 73 pots.

## Session 2 - the P6b-2 image

Measured on **2026-10-18**, the same day, on the coupon with the playing image
built at **`7aa0daad`** (`SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1`), 12 V
supply on. Checks by Bastian, by ear and by eye.

**Rig gotcha.** The first listening attempt distorted heavily on every image
(P6b-2, the probe, and P6b-1 from main `40c83e92`). The coupon's 12 V supply
was off and the board ran on USB alone: USB alone powers the board but the
audio output distorts. With 12 V on the output was clean. Switch the supply on
before judging any sound.

### 3. The checks

| # | check | verdict |
|---:|---|---|
| 1 | Boot with RV2, RV4 and RV6 away from their init positions plays the init patch | PASS |
| 2 | A nudge below the threshold changes nothing; a real turn jumps and then follows | PASS |
| 3 | The key latches MOD, LED_1 double-pulses, RV6 (FILT_A) changes the filter's modulation depth audibly while the base stays | PASS |
| 4 | Unlatching: no jump until a pot moves | PASS |
| 5 | LED_1 outside MOD (limiter display) stayed dark | not a failure |
| 6 | Clicks | about one every few minutes |

Check 5: the init patch does not drive the limiter, so a dark lamp is consistent
with the law. The lamp path itself is proven by check 3. The limiter display
is unobserved at the board.

### 4. Cycle counters

From the log (`SHELL_PLAY_LAW`), about ten minutes of playing, pots untouched
apart from the checks:

| counter | value |
|---|---|
| `cyc_max` (the law) | 14 976 |
| `cyc_last` | about 11 900-12 600 |
| `led_max` | 1 580 |
| `led_last` | about 1 140-1 280 |
| `cb_last` (the whole callback), p50 | 889 912 |
| `cb_last`, p99 | 916 730 |
| `cb_last`, largest seen | 922 220 (of 960 000) |

`cb_max` climbed 945 665, 982 104, 985 308, 1 020 977: **at least three
overruns past 960 000.**

### 5. The clicks predate P6b-2

The P6b-1 image (main `40c83e92`) under the same conditions, five minutes,
pots untouched: **7 clicks** by ear; its `cyc_max` was 13 905. So the clicks are
not new with this branch.

- P6b-1's Plan B trigger ("clicks, or `cyc_max` above 2.9 points on the Rev A
  board", P6b-1 spec §6) is met. It is a separate round (the CPU headroom
  round), not part of P6b-2.
- Spec §6's own rule for this branch does not fire: 14 976 + 1 580 = 16 556,
  under the reserve of about 28 000, so the LED law keeps running every block.

### 6. P7 re-check: the LED step count on Rev A's LEDs

Rev A carries different LEDs than the coupon, and the step count (8) was judged
on the coupon's LED. The flicker threshold rises with brightness, so the darkest
step (1 of 7 blocks, about 71 Hz) must be re-checked by eye on the Rev A LEDs
at P7. If it flickers, lower `kLedSteps` (one constant in `control/led_law.hpp`,
both hosts).
