# CPU headroom on the coupon: the clicks, the levers, the margin

Spec: [`2026-10-18-cpu-headroom-design.md`](../superpowers/specs/2026-10-18-cpu-headroom-design.md);
plan: [`2026-10-18-cpu-headroom.md`](../superpowers/plans/2026-10-18-cpu-headroom.md).
The measurements that found the problem (the click points, the per-deck split,
the wavetable bank's location) are the spec's §2 and are not repeated here.

Every figure below: **2026-10-18**, one coupon board, 12 V on, pots untouched,
DWT cycles of the 480 MHz core, block 960 000 cycles. `cb` is the
`SHELL_PLAY_LAW` window (`panel_scan_tick()` through `inst.process()`).
Clicks counted with `tools/click_detect.py` on a recording of the coupon's
output (Apollo Twin, input 1, 48 kHz), and judged against the log (§3).

## 1. The session: ITCM on and off, and the stress patches

All five images built from **`a2075feb`**, each on a clean `shell/build`,
`SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1`, pairwise different (`cmp`). The
wavetable bank is in D2 SRAM in all of them (spec §4.1). "Probe" =
`C_USER_FLAGS=-DSPKY_PROBE_MARKS=1`. Each probe image ran ~5.4 min
(~161 000–164 000 warm blocks).

| image | ITCM | patch | cb mean | cb max | deck A | deck B | mix | blocks > 930 k | clicks |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| A (probe) | off | init (A FEED, B WAVE) | 861 737 | 958 542 | 273 760 | 409 325 | 159 103 | 7 | **3** |
| B (probe) | **on** | init | **780 374** | **866 712** | 253 317 | 357 196 | 151 364 | 0 | **0** |
| C (probe) | on | **W2** (both WAVE) | 781 168 | 894 446 | 251 677 | 359 426 | 151 589 | 0 | **0** |
| D (probe) | on | **F2** (both FEED) | 780 254 | 810 105 | 252 966 | 359 057 | 150 916 | 0 | **0** |
| E (shipping) | on | init | p50 793 402 | **873 150** | — | — | — | — | **0** |

`CBP_BOOT` confirmed the engines of every image (A, B: 6/3; C: 3/3; D: 6/6 —
`EngineId`, 3 = WAVE, 6 = FEED).

**The ITCM verdict (spec §4's rule).** A against B, the switch the only
difference: cb mean **−81 k**, cb max **−92 k**; deck A −20 k, deck B −52 k,
mix −8 k. Kept (≥ 20 k on cb), and above 40 k, so no second run was needed.
Image B: all but 2 of 163 399 warm blocks under 850 000.

**The stress patches are not what the spec estimated.** The spec's §4.6
guessed W2 at ~994 k from "2 × deck B". Measured, W2 and F2 cost the same as
the init patch: a deck's cost follows that deck's own settings (density,
voices, FX), not the engine name. Deck A as WAVE costs 252 k, deck B as FEED
359 k. A heavier stress patch would have to change a deck's knobs, not its
engine.

**Not run, by judgement:** W2 and F2 as shipping images (plan Task 9 step 2).
The probe images already include the probe's cost and stayed 36 k (W2) and
120 k (F2) under their 930 k target; the shipping images can only be lower.

## 2. The shipping image

Image E (no probe, ITCM on, init patch), **10 minutes**: `cb_max`
**873 150** (target ≤ 900 000), `cb_last` sampled twice a second p50 793 402,
p99 806 657. No click (§3): the detector's five hits score 12–26, all at the
recurring musical moments below, none on a block anywhere near the deadline.
**Spec §3.1 is met on the shipping image.**

## 3. What counts as a click, and where the deadline really is

**Weak detector hits are music.** Every recording of this patch has the same
two weak hits (score 13–25) at the same musical moments (~292 s and ~310 s
into the recording in A, B and C, which all started ~4 s after the flash). The 300 s desktop render of the same patch, where no
block can be late, has hits of the same size (13.7, 19.7), one of them with
two discontinuities 95 samples apart — the control raster stepping. Real
dropped blocks scored 350–2 600. Those moments are the pattern points where the board
used to click: the desktop render's two hits sit at pattern phase 24.23 s and
6.13 s, i.e. the +24.19 s and +6.05 s points of spec §2.1 — the parameters
jump there, the engine's own output steps a little, and the CPU peaks at the
same time. So a detector hit is a click only if it lands on a logged block
above the deadline; on that rule images B, C, D, E have none. The detector ignores the recording's first and last 100 ms (the
capture start produced a hit at t = 0 in every recording).

**The deadline is not one number** (spec §3.5's re-check). Image A's
recording, aligned to its log by the 48.305 s pattern (offset 28.0 s):

| block | cb | clicked |
|---:|---:|---|
| 132 855 | 958 542 | yes (score 351) |
| 144 935 | 958 116 | yes (score 392) |
| 157 025 | **932 214** | **yes (score 1 197)** |
| 120 785 | 944 838 | no |
| 27 165 | 934 262 | no |
| 51 334 | 934 156 | no |
| 72 476 | 930 134 | no |

The first session (spec §2.1) saw 945 388 click and 947 140 not. Together: a
block of the measured window clicks somewhere between **~930 k and ~960 k**,
depending on what runs outside the window at that moment. The spec's
targets (900 k init, 930 k stress) stand; the stress target has no margin
below the lowest click seen (932 k), so it is a ceiling, not a comfortable
place.

## 4. Memory

`SRAM_EXEC` with ITCM (`a2075feb`): Rev A image 202 224 B used, **60 656 B
free** (11 384 B without ITCM); coupon probe image 203 100 B. `ITCM_HOT`
49 952 B of 65 280 B. `RAM_D2CACHE` 65 024 B (the bank).

## 5. By ear

**Bastian, 2026-10-18, shipping image E on the coupon:** "klingt sauber" —
clean over several pattern cycles — and the P6b-2 checks (ownership, MOD
latch with LED_1, unlatch) pass.

## 6. Open

- **VCV** is unaffected: the only engine change, `wt::set_bank()`, is never
  called there, and `table()` reads `kBankSamples` by default (desktop test
  "wt_bank: table() follows set_bank()"; render hashes unchanged). No Rack
  install was needed for this round.
- **Rev A (P7)**: re-run the init and stress images with the probe on the Rev
  A board; the full panel scan's cost is unmeasured (spec §6).
- Tasks 7 (WAVE's transition cost) and 8 (Plan B) were not needed: every
  target was met after the bank move and ITCM.
