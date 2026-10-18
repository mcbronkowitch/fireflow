# CPU headroom — removing the clicks on the Daisy firmware

**Date:** 2026-10-18
**Phase:** between P6b-2 and bring-up (P7). This is the "CPU headroom round"
that P6b-1's Plan B trigger opened (P6b-1 spec §6; roadmap, 2026-10-18 P6b-2
entry: "Next: the CPU headroom round").

**Inputs:** the brainstorming of 2026-10-18 with Bastian (his decisions are
marked *Bastian*), five probe sessions on the coupon the same evening (§2), the
July bench evidence for ITCM (`docs/bench/2026-07-30-d570e47-system-{axi,itcm-hot}`
and the roadmap's 2026-07-30 entries), the code at main `8c1a37f1` plus the
probe commits `20c46513` … `80ece588` on branch `claude/objective-dhawan-c68b8d`.

## 1. Intent

The firmware clicks: about one click a minute in the init patch, sometimes two
within a few seconds (*Bastian*: "kurz bevor das Muster wechselt", "ab und zu
2x relativ nah beieinander, 5–10 s später nochmal"). The P6b-1 image clicked
too, so the clicks are not new with P6b-2. The Rev A boards are on their way,
and Bastian wants firmware that plays cleanly when one is on the table.

This round removes the clicks by giving the audio callback headroom, not by
hiding the symptom. Changed sound is acceptable (*Bastian*: "kann ruhig etwas
anders klingen, ist gar kein Problem"). The goal is the worst case, as far as
it can be reached, not only the init patch (*Bastian*: "worst case nach
Möglichkeit").

## 2. What was measured

All figures: coupon, 12 V on, pots untouched (so the init patch plays, P6b-2
§2), init patch, DWT cycles of the 480 MHz core, block = 96 samples = 960 000
cycles. "cb" is the existing `SHELL_PLAY_LAW` window: `panel_scan_tick()`
through `inst.process()` (`shell/main.cpp:179`). Audio was recorded off the
coupon's output (Apollo Twin, input 1) with ffmpeg and searched for clicks by a
second-difference detector (scratch tool, method in §2.6).

### 2.1 The clicks are dropped blocks at fixed points of the pattern

- **Shape:** every click is two discontinuities 85–95 samples apart, which is
  one block: a block whose output was not ready in time.
- **The desktop render of the same patch** (the shared law ticked per block
  over `kInitParamDefaults`, 300 s) is clean: no clicks.
- **Strictly periodic** at the pattern period, **48.305 s**, at three fixed
  points of the cycle: the PITCH phrase wrap (phase 0), **+6.05 s**, and
  **+24.19 s** — the last is the moment deck B's SOURCE, SIZE, MOTION and LEVEL
  lanes all step in the same block. 0 and +6.05 s are Bastian's "two clicks
  close together".
- **Every recorded click matched a logged block** once the recording and the
  log were aligned (offset 27.99 s): the clicked blocks had cb 945 388 …
  999 294. Blocks at cb ≤ 947 140 did not click, one at 951 506 clicked
  faintly.

**So the measured window's real deadline is about 945 000 cycles, not
960 000.** Roughly 12–15 k cycles of every block are spent outside the window
(interrupt entry, libDaisy's sample conversion, the other priority-0
interrupts). No callback-cost target in this round is quoted against 960 000.

### 2.2 Where the block goes

Probe `cbc0545c` (per-sample stamps into a stack array inside
`Instrument::process`, deck boundaries included; §2.6), 168 023 warm blocks
(~5.6 min):

| part of the callback | mean | in the hot blocks |
|---|---:|---:|
| deck A (FEED) | 273 k | 270–290 k, flat |
| deck B (WAVE, 4 voices sounding) | 432 k | **480 k in dense passages, 505–561 k at the click points** |
| mix: CHOKE, pan, reverb, limiter | 159 k | 158–161 k, flat |
| control law (`cyc_last`) | 12 k | 12–12.8 k |
| LED law + lamp word | 1.1 k | 1.1–1.3 k |
| scan, keys, pot rows, rest | 5–7 k | 5–7 k |
| **cb** | **883 k** | **930 k … 1 011 k** |

- **The peaks are deck B, and only deck B.** Deck A and the mix do not move.
- **They are not a single expensive sample.** The most expensive sample of a
  block is ~28–33 k in every block (the control-rate sample); in a hot block
  the other 95 samples each cost ~0.6–1 k more (mean 9.1 k → 9.7–10.1 k). The
  excess lasts about one block after the lane step or note.
- **Voice count does not change:** deck B reports 4 active voices in every hot
  block. The cost per voice-sample changes.
- **The headroom is thin even without the peaks:** in dense passages cb sits at
  930–940 k, within 5–15 k of the deadline.

### 2.3 The wavetable bank's location (ablation, sound unchanged)

Probe `80ece588`: the 65 024-byte WAVE bank, normally read in place from QSPI
(`.qspiflash_data`), copied into the cacheable part of D2 SRAM
(`RAM_D2CACHE`, 256 KB, 0 % used before) at boot. Same patch, ~5.4 min:

| | bank in QSPI (`cbc0545c`) | bank in D2 SRAM (`80ece588`) |
|---|---:|---:|
| deck B mean | 432 k | **408 k** |
| deck B, worst hot block | 561 k | **503 k** |
| cb mean | 883 k | **857 k** |
| cb max | 1 011 k | **955 k** |
| blocks above 945 k | 15 | **2** |
| deck A / mix mean | 273 k / 159 k | 271 k / 158 k |

Deck A and the mix stayed put, so the gain is the bank's location, not a code
layout effect. The peaks gain more than the mean (−58 k against −24 k): the
click points are where WAVE reads table regions that are not in the cache. The
mechanism inside the oscillator (the 96-sample mip crossfade in
`engine/synth/wt_osc.h`, which reads a second mip table for one block after a
note crosses a mip boundary) fits the one-block shape but **is not measured**.

### 2.4 Code layout moves the baseline by tens of k

The same patch on the same board ran at cb ≈ 890 k (p50, P6b-2 image
`7aa0daad`) and ≈ 848 k (first 32 s of probe `74393b61`). Different images
differ by tens of k through code placement alone. Two consequences: every
comparison in this round is between images built from one tree with one
switch flipped, and a lever's gain is believed only when it is clearly larger
than that spread or shows in the per-deck split (§2.3 is such a case).

### 2.5 Prior evidence, not re-measured: ITCM

July 2026, bench on the Daisy Seed, engine of that time: moving 41 984 B of the
per-sample audio path into ITCM saved **≥ 6.01 points average, 6.09 max**
(~58 k cycles), output bit-exact; a function at address 0 produced silence, so
the first 256 B of ITCM are reserved (`bench/itcm_hot.lds`). The shell never
got this: the submodule has no SWD, so nothing loads an ITCM section, and
`ITCMRAM` reads 0 %. The engine has grown since (FEED, BODY, the WAVE
engine as it is now), so the 6 points are a reason to try, not a figure for
this board.

### 2.6 How it was measured (for whoever re-measures)

- **The probe that works** (`cbc0545c`, kept, §4.5): `Instrument::process`
  stamps the cycle counter into a stack array at the top of every sample and
  after each deck, and hands the array to the shell once per block; the shell
  reads each deck's lane step and phrase index once per block.
- **Two designs that do not work**, kept here so nobody rebuilds them: a call
  into the shell at every segment boundary (`20c46513`, ~1 500 calls a block),
  and inline marks that write global counters inside the sample loop
  (`37830e2d`). Both overran on every block and starved the USB log. A global
  store in the hot loop costs far more than its instructions.
- **Click detector:** second difference of the recording against its local
  median in 20 ms windows, a click is a window above 12× its neighbourhood.
  Recording-to-log alignment by the 48.305 s period and the click phases.

## 3. Goal and done

**Done when, on the coupon:**

1. **Init patch:** worst cb over 10 minutes **≤ 900 000** (45 k under the
   measured deadline), read from the shipping image's own `SHELL_PLAY_LAW`
   `cb_max`, and a 10-minute recording with **no click** by the detector. Then
   Bastian listens (by ear, his verdict).
2. **Stress patches** (§4.6), each 10 minutes: no block above **930 000** (the
   deadline minus the 15 k the window cannot see), no click in the recording.
   Where a stress patch cannot get there with §4.1–§4.4, §4.5's rule decides.
3. The P6b-2 checks still pass by ear (ownership, MOD latch, lamps).
4. `SRAM_EXEC` on the Rev A image keeps at least the 8 KB floor of P6b-1 §5.

5. The final session re-checks §2.1's deadline the same way it was found:
   the recording aligned to the probe log, the cb of every clicked and of the
   largest unclicked block reported. If it moved, the targets move with it and
   the report says so.

**The click detector** becomes a committed tool (`tools/`, name the builder's
call) with the 2026-10-18 method and threshold (§2.6). Its test synthesises a
tone with one dropped 96-sample block and a clean tone: one click found, none
found; proven red once. It runs as a plain script, like the other `tools/`
guards (they have no runner).

The figures and the outcome of every decision rule go into the roadmap's entry
for this round and into `shell/README.md`, with the shell's real deadline
(§2.1) stated there.

## 4. The levers, in order

Each lever is its own task, measured on the coupon before the next starts.
The order is by expected gain per risk.

**How a lever is judged.** Two probe images from one tree, the lever's switch
the only difference, one run of ≥ 5 minutes each (init patch, recorded). The
probe's per-segment means (deck A, deck B, mix, law) are the evidence, because
§2.4's layout spread is tens of k and a single cb_max is one extreme value.

- **Kept** when the segments the lever touches drop by **≥ 10 k mean** or the
  worst block drops by **≥ 20 k**, while a segment it does not touch moves by
  **< 5 k** (that segment is the layout control, as deck A and the mix were in
  §2.3), and nothing sounds wrong to Bastian.
- A lever that touches every segment (ITCM) has no control segment: it is kept
  at **≥ 20 k mean** on cb, and confirmed by a second run of both images if the
  first shows less than 40 k.
- Anything in between is **inconclusive**: one more run of both images, then
  the rule above on the two runs' mean. Never more than two runs per image.

### 4.1 The wavetable bank in D2 SRAM (measured, §2.3)

Productise the probe: the shell copies the bank from QSPI into `RAM_D2CACHE`
before `Instrument::init`, and `wt::table()` reads the copy. The bank stays
in QSPI as the source (the flashing procedure does not change). On the
desktop and in VCV nothing changes. The engine keeps no hardware knowledge:
it reads through a pointer the host sets (as in the probe); the name and the
exact seam are the builder's call.

### 4.2 The per-sample audio path in ITCM

Move the hot audio code into ITCM, loaded at boot, never executed from
anywhere else.

- **Where the code comes from at boot:** from QSPI, like the cold code
  (`.qspi_cold`, P6b-1 §5), so the ITCM image does not also occupy
  `SRAM_EXEC`. A copy in `SRAM_EXEC` instead is allowed only if QSPI cannot
  work; then §3.4's floor decides.
- **When:** the copy runs before anything that can reach an ITCM function —
  before `Instrument::init` at the latest, since init paths call into the
  same objects.
- **Scope:** engine code and the shell's own callback code only. libDaisy's
  audio interrupt and sample conversion stay where they are, so the ~945 k
  deadline of §2.1 stays valid for the targets.
- **What goes in:** chosen by function, not by object file: the July list
  (`bench/itcm_hot.lds`) is by object and today's objects do not fit 64 KB
  (instrument, part, the FX, synth_engine, voice, feed_engine and lane
  together are ~105 KB of `.text`). The deck B and deck A paths of §2.2 first.
  The first 256 B stay empty (§2.5).
- **Guard:** a post-link check in the spirit of `shell/qspi_placement.py` —
  no function that runs before the copy can reach ITCM, the section is not
  empty, it fits — wired into ctest and proven red once, as every guard here
  is.

### 4.3 WAVE's transition cost (sound may change)

Only if §4.1–§4.2 leave the init patch above §3's target, or a stress patch
needs it. First an ablation that names the mechanism (e.g. the mip crossfade
shortened or switched off, everything else equal), then a change Bastian
judges by ear. Candidates, not decisions: a shorter crossfade, a crossfade
that reads one table instead of a weighted sum over all mips, fewer table
reads per oscillator.

### 4.4 Plan B: the control law across two blocks

P6b-1 §6's split (deck A and the globals in one block, deck B, the depth loop
and the tail in the other). It is a firmware schedule: the law gains a way to
run one half per call, the shell alternates the halves, and VCV keeps calling
the whole tick every 16 samples, unchanged. The law's tests cover both the
whole tick and the alternating halves (end state equal after an even number of
calls). It is worth about 6 k per block (§2.2), so it
comes after the levers that are worth tens of k, and only if a target is still
missed or the Rev A panel's measurement (§6) asks for it.

### 4.5 If the stress patches still do not fit

Rule, fixed before the measurement: if after §4.1–§4.4 a stress patch still
crosses 930 k, the round does **not** keep going down the list below on its
own. It reports the figures and Bastian decides between: fewer voices or a
cheaper voice on the engine that crosses (sound changes, which he has
allowed), `-ffast-math` as a measured round of its own (gotcha: it can fold
away the isfinite/NaN guards, P6b-1 entry), or a larger audio block (192
samples: halves the per-block costs but doubles the latency and the mux sweep
and breaks the LED law's ~71 Hz darkest step, P6b-2 §4.3 — it would need its
own LED decision).

### 4.6 The stress patches

Measured with a probe switch that boots a variant of the init patch instead of
the init patch itself (the coupon's pots stay unowned, so the variant holds):

- **W2:** both decks on WAVE (init patch, `ENGINE_A` set like `ENGINE_B`).
  Estimate from §2.3, not measured: 2 × 408 k + 158 k + ~20 k ≈ 994 k — over
  the deadline today; this is the patch the round is most likely to need §4.3
  or §4.5 for.
- **F2:** both decks on FEED (the bench's 2026-08-19 worst case, ~2.9 points of
  reserve then, engine only).

The builder may add a third if a probe shows a heavier one; the list is a
floor.

### 4.7 The probe stays, switched off

`engine/util/probe_mark.h` and `shell/cb_profile.{h,cpp}` (the `cbc0545c`
design) stay in the tree, off unless the build defines `SPKY_PROBE_MARKS`, as
the tool every later CPU question uses — including P7 on Rev A. Off, they
compile to nothing in every image, the desktop and VCV. The probe's lane
observers on `Instrument` and `SuperModulator` stay with them. `shell/README.md`'s
probe table gets the row. The failed designs of §2.6 go; their commits are the
record.

## 5. Constraints

- `engine/` carries no hardware type (CLAUDE.md). Every placement decision
  (D2, ITCM, QSPI) lives in the shell and the linker scripts.
- The shell's DFU-only flashing stays: `shell-qspi.bin` then `shell-sram.bin`,
  a matched pair (`shell/README.md`). Whatever the ITCM copy reads from QSPI is
  part of `shell-qspi.bin`.
- No bit-exactness gates (CLAUDE.md). §4.1 and §4.2 should leave the output
  identical; that is a sanity check, not a gate.
- `bench/` is not touched; `alt_sram.lds` is shared with it, so a change there
  is additive and leaves bench's sections where they are.
- Clean builds between switch positions, and `cmp` two positions before
  flashing (the one-second-mtime trap, `shell/README.md`).

## 6. Rev A

The Rev A image scans 73 pots, not 3, and runs the same law over a full knob
vector. Its shell cost is not measured. P7's bring-up re-runs §3.1 and §3.2 on
the Rev A board with the probe; if the margin is gone there, §4.4 is the
first lever, and the outcome goes into the P7 record.

## 7. Calls I made (not decided by Bastian)

- The targets: 900 k for the init patch, 930 k for the stress patches, both
  against the measured ~945 k deadline.
- The lever order (§4) and the judging rule (§4: one 5-minute run per image,
  per-segment means against a control segment, at most two runs).
- Plan B as a firmware-only schedule; VCV keeps the whole tick.
- libDaisy's interrupt code out of the ITCM scope.
- The click detector committed with a synthetic test.
- ITCM loaded from QSPI rather than from `SRAM_EXEC`.
- W2 and F2 as the stress patches.
- Keeping the `cbc0545c` probe in the tree, off by default.
- Stopping at §4.5 for Bastian's decision instead of reducing voices on my own.

## 8. Out of this round

- Saving and loading patches, CV, clock — unchanged.
- The bench and its ITCM profiles.
- Rev A bring-up itself (P7).
