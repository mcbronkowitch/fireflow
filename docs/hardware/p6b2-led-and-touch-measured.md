# P6b-2 on the coupon: lamp step count and pot-touch threshold

Measured on **2026-10-18**, one session, one coupon board, with the probe image
built from commit **`356458f5`** (Task 1's dim demo plus `SHELL_SPREAD`).
Spec: [`2026-10-18-rev-a-p6b2-mod-keys-leds-design.md`](../superpowers/specs/2026-10-18-rev-a-p6b2-mod-keys-leds-design.md).
Both numbers below were read off the board; neither is derived from a model.

**Result:** `kLedSteps = 8` (`control/led_law.hpp`), `kPotTouch = 144`
(`shell/pot_owner.h`).

## 1. LED step count

**Procedure.** LED_1 held at duty 1 of N (the dimmest non-zero level of an
N-step raster), with N cycled through the probe's presets. Bastian looked at the
lamp directly and in peripheral vision, where flicker shows first.

| N | verdict |
|---:|---|
| 4 | no visible flicker |
| 6 | no visible flicker |
| 8 | no visible flicker |
| 16 (the darkest preset) | flickers |

**Rule:** `kLedSteps` is the largest N without visible flicker. That is **8**,
the same as the provisional value the code carried; the measurement confirms it.
The verdict is by eye, on one lamp at its dimmest level.

## 2. Pot-touch threshold

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
