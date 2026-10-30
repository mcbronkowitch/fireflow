# CPU hunt on Rev A: board measurements

Rev A #1, branch `feat/cpu-hunt-remote`. Spec:
[`2026-10-29-cpu-hunt-remote-design.md`](../superpowers/specs/2026-10-29-cpu-hunt-remote-design.md),
plan [`2026-10-29-cpu-hunt-remote.md`](../superpowers/plans/2026-10-29-cpu-hunt-remote.md).
Every number below was printed by the board; captures in [`captures/`](captures/)
as `cpu-hunt-2026-10-29-*.txt`.

## 1. Setup visit (plan Task 7), 2026-10-29

Bastian at the board. Cabling: board outputs on Twin inputs 1 + 2; the board's
audio input is unconnected. Images: the measurement image
(`SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1 SHELL_REMOTE=1 SHELL_REMOTE_MEASURE=1`,
clean build), first `f53409a`, then `80b37ce` (adds workload `bastian`). Every
flash was a pair, QSPI first. The persisted patch survived all of them
(`SHELL_PERSIST src=flash seq=579` before and after).

| Step | Verdict |
|---|---|
| 1, build and first flash | **pass**: Bastian put the board in DFU; the runner flashed the pair and confirmed `build=f53409a` |
| 3, PING and Bastian's vector | **pass**: `pong`; `SHELL_PLAY_V` identical over all 39 frames of 20 s; stored as workload `bastian` (id 145) |
| 4, remote flash rehearsal | **pass**: `boot` → DFU → `flash` → `build=80b37ce` confirmed, no hands |
| 5, entry-to-entry period | 959 826 - 960 094 cycles on quiet frames: 960 000 minus a crystal offset of ~100, jitter under 300. The 965 000 period threshold stands |
| 5a, guard under a full burn | **pass** (on `r4_noon`): `guard tripped by=cb`, `over=8`, the log kept coming, the burn stopped (`cb` ~30 k); `TOG 30 0` then `LOAD` → audio back |
| 5b, BOOT near saturation | **pass**: with `LATE 36` the report frames took ~4x as long (`n=1045` blocks per frame instead of ~250), `over=851`, `gap` up to 982 703; `BOOT` put the board in DFU within 1 s |
| 5c, full saturation, guard off | **wedges, as documented**: happened by accident (below); no serial for 20 s, RESET by hand brought it back |
| 6, self-recovery | **pass for this pairing**: `80b37ce` SRAM over `f53409a` QSPI → DFU device present for all 40 s watched, no CDC port; the right pair then flashed remotely |
| 6b, IWDG across a bootloader reset | not run (needs a probe image; optional) |
| 7, power cycle by plug | skipped: no plug (spec §9) |

### Bastian's patch trips the overrun guard

On the persisted patch (WAVE + SYNTH, no excite) the guard trips by itself:
8 callbacks in a row over 950 000 cycles. After both boots it tripped at block
8788 and 8789 (~17.6 s), with `cb_max` 965 790 and 968 494. After an `RST` it
tripped again about 20 s later (block 18 801, `cb_max` 959 181). Until the next
`RST`, `LOAD` or `HOME` the output is silent, so on this patch the board is
mostly silent, and runs of workload `bastian` will come back `tripped=1`.
Quiet frames run at `cb_last` ~873 000 - 912 000, `busy_p50` ~890 000.
Captures: `…-bastian-patch.txt`, `…-bastian-retrip.txt`.

**After the cb limit went to 965 000 (`7dc7023`)** the patch still trips, now
`by=period`, and again at block 8789 after boot: the same block on three
boots. The frame before the trip has the worst block at `cb` 971 066, part A
(WAVE) at 404 752 against its usual ~362 000, and `gap` 986 574: the callbacks
ran back to back. On quiet frames the patch runs at `busy_p50` ~939 000 and
`busy_max` ~955 000, so about 5 000 cycles from the period. The trip at block
8789 is a real overrun at a fixed time after boot, not a guard set too
tight. Capture: `…-bastian-965.txt`.

### Pot takes during remote runs

`void=1` showed up in runs nobody was meant to touch: rows 35 `COMP_B` and 72
`REV_MIX_B` during a run, then rows 6, 26, 50 and 64 right after a `LOAD` while
a 1 000 000-cycle burn was running. No hand was on the panel (Bastian). Both
bursts came in the first blocks of a burn longer than the block period (bit 30
repeating `LATE 1000`), right before the guard tripped. No take appeared in
any other capture of the visit: Bastian's patch, `r4_noon` at ~910 000 cycles,
and the near-saturated 5b runs at `LATE 30` and `LATE 36`, where callbacks
still ended inside the period. So far takes have come only with callbacks
running past the period; the mechanism is not measured.

### The 5c accident and the 5b order

Bit 30 repeats the *last* `LATE`. Step 5b as written (`TOG 31 1`, `TOG 30 1`,
`LATE k`) therefore repeats the previous step's `LATE 1000` between the second
and third command, with the guard already off: that is 5c. The board wedged
and needed RESET by hand. The working order is `TOG 31 1`, `LATE k`, then
`TOG 30 1`; the plan text is corrected.

## 2. Baseline, A/A floor, outside the window (plan Task 8 steps 1-3), 2026-10-29

Claude alone, hands off the panel. Clean builds of `50250a0` (the guard
re-arms itself): the listener image (`SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1
SHELL_REMOTE=1`) and the measurement image (the same plus
`SHELL_REMOTE_MEASURE=1`). Every flash a pair, QSPI first, remotely; the
persisted patch read `SHELL_PERSIST src=flash seq=579` after every boot.

**Which image judges Bastian's patch.** On the measurement image the patch
overruns by itself (§1), and after a `LOAD` it overruns at once, so a 600 s
measurement-image run of `bastian` measured nothing. On the listener image it
never trips. Ruling: verdicts on `bastian` (baseline, A/A, candidates) run on
the listener image; the measurement image is used for the section split and
for the injected late blocks only.

**Free memory** (measurement image `7dc7023`, from the section ends in the
map): `ITCM_HOT` 13 376 B, `SRAM_EXEC` 29 424 B.

**Baseline, `bastian` for 600 s** (listener, `--record`): 636 counted frames,
0 trips, no take. `cb_last` p50 904 427, p99 924 413, max 929 583. The worst
block per frame (`frame_max`, the frame's callback window) p50 923 741,
p99 943 269, max 951 197; `gap_max` 961 830. 133 frames have a block over
930 000. Capture `…-baseline-600.txt`.

**Section split** (measurement image, fresh boot, the 21 frames before the
patch's own overrun at block 8789; `…-boot-measure.txt`), medians per frame's
worst block: deck A 334 813, deck B 361 765, mix 150 181, scan 15 932, law
14 148, LED 1 441, gate 92, persist 14; the worst block itself 907 011.

**A/A floor.** Two 300 s runs each, listener image, interleaved with nothing
else. A 300 s run has ~320-380 frames, so its p99.9 is its max.

| Workload | run | `cb_last` p50 / p99 / max | `frame_max` p50 / p99 / max | floor `frame_max` p99 / max |
|---|---|---|---|---|
| `bastian` (145) | 1 | 903 305 / 923 459 / 927 953 | 923 171 / 944 779 / 953 237 | **2 698 / 1 758** |
| | 2 | 903 713 / 925 179 / 930 147 | 924 217 / 942 081 / 954 995 | |
| `BBD_SYNTH_corner_A0` (14) | 1 | 800 409 / 820 963 / 829 391 | 823 713 / 835 945 / 837 315 | **2 270 / 1 538** |
| | 2 | 800 047 / 822 337 / 827 837 | 824 541 / 833 675 / 835 777 | |

Workload 14 is the corner with the highest desktop p99 (31.1 µs). The floor
on `cb_last` is p99 1 720 / max 2 194 (`bastian`) and 1 374 / 1 554 (14). So a
candidate has to move p99 and max by more than ~2.7 k / ~2.2 k cycles to
count. The plan's stop criterion ("the shortest duration at which two A/A
runs' p99.9 agree within the floor") is circular when the floor comes from
the same pair; 300 s is the duration used, and it gave 0 trips and the
floors above. Captures `…-aa-bastian-{1,2}.txt`, `…-aa-corner14-{1,2}.txt`.

**Measurement image against listener image.** Both images booted fresh on
the persisted patch, 16 s each, logged hands off (`…-boot-listener.txt`,
`…-boot-measure.txt`). Aligned by the best-fitting time shift (the patch
plays the same from boot): the measurement image's callback window is
**+55 k cycles** above the listener's (median over 16 frames, sd 8 k). That
is why the patch overruns there and not on the listener image.

**Outside the callback window** (measurement image, `SHELL_OUT`, the same
fresh boot, quiet frames): `out_max` median 18 443, `ext_max` 14 461,
`tail_max` 4 129, `tail_p50` 3 909; `busy_p50` 891 884. A block whose window
reads ~945 000 therefore ends ~15-20 k later, at the period.

Not measured: the listener image against an image without `SHELL_REMOTE` (a
play image has no `BOOT`, so getting back from it needs a hand on the board).

## 3. The clicks (plan Task 8 step 4-5), 2026-10-29

**Calibration: an injected late block.** The capture runs while the board
is sent to DFU (the output goes silent) and flashed with the measurement
image; the audio onset in the recording is then a fixed point of the
board's block counter, with no host clock in between. Ten `LATE k` (k = 100,
200, 400 kcycles) at ~1.1 s spacing; each ack names the block it burnt in
(`…-late-calib.txt`, click lists in `…-clicks.txt`).

- Every injected block whose frame went over the period (8 of 10, windows
  973 765 - 1 276 963) gives a click on **both channels**, at the onset
  + block time + **0.26 s, the same within ±4 ms for all eight**.
- The two that did not click are the two `k=100` whose frame stayed under
  the period (early after boot, the patch still light).
- The click's score (the second-difference detector, `tools/click_detect.py`,
  k = 12) is 94 - 2 672 and does not grow with the lateness (k = 100 gave
  1 436, k = 400 gave 535).

An earlier injection run on the same image, aligned by the host clock
(`audio.wav.start`) instead of the onset, put its clicks 0.75 s *after* the
send times; the reference run below put its `LOAD` ~1 s *before* it. So the
`.start` stamp does not pin a recording to host time to better than ±1 s:
ffmpeg's start-up latency varies by capture. Alignment needs a reference
inside the audio.

**The reference run** (lightest corner, `SAMPLER_WAVE_corner_B0`, id 106,
300 s, listener, `…-ref-corner106.txt`) is no click-free control: the
detector reports 832 / 447 hits up to score 25 786 on it. The corners play
noise through percussive engines, and the detector cannot tell those
transients from a click. Only Bastian's patch is quiet enough for it: over
600 s its natural hits stay at score ≤ 16.

**The baseline run's clicks.** The 600 s `bastian` recording has 116 hits
(both channels). Its frames last ~1 s (n ≈ 512 blocks) on the listener
image. With the recording shifted by the host-to-audio offset, every one of
the four frames whose window reached ≥ 945 000 holds a click of score
1 145 - 4 638 (frames at 82.8, 89.5, 108.0, 424.7 s; `win` 951 197,
946 487, 946 751, 946 177). Every other hit, in the other 636 of its 640 frames,
scores ≤ 16, the material's own floor. The 18 frames at 940 000 - 945 000
hold none. This holds for any offset from -0.75 to -0.51 s. The offset is
fitted here, not measured, but it sits inside the range the calibration's
host clock allows (-0.67 to -0.17 s). Four clicks landing by chance in the
four hottest of 640 one-second frames is not a plausible alternative.

**Verdict: yes.** The 2026-10-28 clicks on Bastian's patch are late blocks.
A block whose callback window reaches ~945 000 cycles (plus ~15-20 k outside
the window, i.e. at the period) is a click on both channels; at
940 000 - 945 000 there was none in 600 s. On the listener image this patch
crosses that line about four times in ten minutes. So the target for the
candidates is the worst block of `bastian`, not its mean: the A/A floor
above says a candidate must take `frame_max` p99 and max down by more than
~2.7 k to count, and a click-free 10 minutes needs its max under ~945 000
with margin.

## 4. Worst cases per engine and the candidate list (plan Task 9), 2026-10-29/30

### Which image these numbers come from

The 154 runs of step 1 (145 workloads x 60 s, 9 gestures x 20 s) ran on the
**measurement image** 50250a0, not on the listener as intended: Task 8's
last flash was the calibration's measurement image. The 180 random vectors
of step 2 ran on the measurement image 117c6ac (the same code plus the
longer table). That is the image with the section splits (`SHELL_SEC`,
`CBP_REC*`, `CBP_WIN*`), so the ranking and the breakdown below are
measurement-image numbers. The absolute worst cases were re-measured on the
listener 117c6ac (the 20 highest by `frame_max` p99, 60 s each). Measure
minus listener on those 20 is +72 k median, with a spread of -12 k to +135 k:
the +55 k of §2 holds for `bastian`, not as a constant for every workload.

A run is `void` when the overrun guard tripped. Most voids are a workload
that overruns on every block after each re-arm. Their `frame_max` is then
taken over all frames: the re-armed block is a real full block. 39 of 59
voided again on the one repeat. The random vectors got no repeat.

Tables: `captures/cpu-hunt-2026-10-30-worst-cases.csv` (every run; desk ns,
measurement and listener `frame_max`, trips, void),
`…-rank-correlation.json`, `…-section-means.txt` (window means per section,
per run the max over its windows), `…-worst-block-split.txt` (each run's
hottest block, per deck).

### It is not one patch: half the vectors overrun

On the measurement image 171 of 325 static workloads have a block ≥ 960 000
cycles. On the listener, all 20 of the top 20 still reach ≥ 945 000 (the
lowest 946 559), the click line of §3. The worst on the listener:

| workload | listener `frame_max` max | trips / 60 s |
|---|---|---|
| SYNTH_WAVE_top1 | 1 057 583 | 0 |
| WAVE_SYNTH_top2 | 1 049 539 | 0 |
| WAVE_WAVE_rnd8 | 1 011 799 | 0 |
| SAMPLER_WAVE_top1 | 1 011 029 | 55 |
| WAVE_WAVE_rnd9 | 1 001 041 | 2 |
| BBD_SYNTH_rnd4 | 1 000 878 | 57 |
| SAMPLER_SYNTH_top3 | 994 167 | 44 |
| BODY_WAVE_rnd1 | 991 575 | 0 |

Zero trips at ~1.05 M looks contradictory. Those runs report few frames
(49 in 60 s for SYNTH_WAVE_top1, against ~60 for a listener run at the
period, e.g. 61 for BBD_WAVE_top4):
the main loop starves and reports late. Why the guard does not trip there
is not examined. Worst per engine on the measurement image (any pairing it
is in):

| engine | worst workload | measurement `frame_max` max |
|---|---|---|
| SAMPLER / SYNTH | SAMPLER_SYNTH_top2 | 1 105 301 |
| WAVE | WAVE_SYNTH_top2 | 1 091 000 |
| FEED | FEED_SYNTH_top2 | 1 070 673 |
| BBD | BBD_SYNTH_top2 | 1 068 055 |
| BODY | BODY_WAVE_rnd1 | 1 053 586 |

The gestures stay under the period on the measurement image: the worst is
`bbd_swap_A` at 962 362, the next `morph_sweep` at 955 051. Their base is
`r4_noon` (927 870 static).

The plan's stop criterion is every worst case ≤ 880 000. On the listener
that is ~180 k off SYNTH_WAVE_top1, i.e. 19 % of the block.

### The desktop does not rank the board

Spearman between desk `max_ns` and the board's `frame_max` max per pairing,
step 1 (n = 9 per pairing: 5 tops + 4 corners): 9 of 16 below 0.5. With the
20 random vectors per weak pairing (seed 1, sweep pass 1; 117c6ac9), still
below 0.5 in BBD_BBD (-0.53), BBD_SYNTH 0.27, BBD_WAVE 0.46, BODY_SYNTH
0.20, BODY_WAVE 0.32 and FEED_FEED 0.35. Over all 323 static vectors the
correlation is 0.37. Random vectors became the worst of their pairing in
BBD_BBD, BODY_SYNTH and BODY_WAVE. One pattern is systematic. The
`corner_*1000` vectors (every pot at the right stop) are among the
lightest on the desktop (BBD_BBD: 26.7 µs against 45.4 µs for top1) and
among the heaviest on the board (985 k against 912 k). The mechanism is
not measured. A guess: long delay lines in SDRAM, cheap in the desktop's
cache, missing in the board's 16 KB D-cache.

**Ruling:** no further vector rounds. The desktop sweep is a pre-filter at
best. Candidates are judged on the board's own worst cases, i.e. the table
above.

### Where a hot block's cycles go

Window means over all 325 static runs, per block, measurement image (per
run the max over its windows, then the median over runs; p90 in brackets):

| section | per deck | both decks / once |
|---|---|---|
| engine | 161 k (194 k) A, 180 k (194 k) B | |
| FX chain | 148 k (170 k) | ~295 k |
| - FLUX | 64-68 k (81-85 k) | ~132 k |
| - GRIT | 57 k (58 k), flat | ~114 k |
| -- stage 1 (Decimator) / 2 (SR reducer) / x-fade | 21.0 k / 24.5 k / 6.3 k | |
| - tap + FX mix + COMP | 24-26 k | ~50 k |
| - send law | 8.6 k | 17 k |
| modulation + control | 69 k (82 k) | ~138 k |
| - lanes | 32-33 k (36 k) | ~65 k |
| - before the deck | 20 k (27 k) | 40 k |
| mix | | 152 k (154 k) |
| - reverb | | 102 k |
| - limiter / blend + duck / observers / return | | 17 k / 17 k / 12 k / 3 k |
| law (`control_tick`) | | 12 k mean; 44 k p50, 72 k max in the run's hottest block |
| scan / led / gate / persist (§2) | | 16 k / 1.4 k / 0.1 k / 0.01 k |

About 600 k of the ~900 k mean block (FX ×2, mix, mod ×2) does not depend
on which engine a deck runs. In each run's hottest block, the engine, FX
and mod sit within 2-10 k of their window means. Two things lift a block
over the mean. One is the law (12 k mean → 44 k p50). The other is the
block's first sample: 60 % of the 413 258 hot-block records have their
slowest sample at index 0, i.e. the control-rate work (in the example of
§2, `maxs` 26.7 k against a 9.4 k median sample).

### The candidate list

Expected cycles are estimates from the split above, or from reading the
code. None is measured yet; Task 10 measures each. Highest first. "Toggle"
means a `probe_toggles.h` bit on the measurement image, "build" a compile
switch.

| # | candidate | expected | class | applies to | switch |
|---|---|---|---|---|---|
| 1 | GRIT REDUCE: the doubled Decimator step bitcrushes and divides on the step whose output is discarded (`engine/fx/grit.cpp:73-80`, `Decimator::Process`). Step the state only on the first call; replace the two `/ (65536 * k)` with a multiply by a reciprocal set in `apply()` | 15-25 k (both decks; stage 1 is 21 k per deck) | 1 | every workload with GRIT engaged in REDUCE (the stage costs are flat over all 325, so all of them) | toggle |
| 2 | FLUX echo reads: probe first whether the 64-104 k per deck is SDRAM misses (the line in a short SRAM buffer); if so, batch or prefetch the read per block | 0-60 k, unknown until probed | 1 | every workload, most on `corner_*1000` / long FLUX time | toggle |
| 3 | `-ffast-math` (incl. `-ffinite-math-only`) on `engine/` | unknown, one build measures it | 2 (NaN guards, spec §4) | all | build |
| 4 | Reverb at half rate (decimate in, interpolate out) | ~45-50 k of 102 k | 2 | all | toggle |
| 5 | Law spike sources (per-block setter audit, read not measured). `BbdEngine::set_flow` (`engine/parts/bbd_engine.h:49`) runs `_recompute()` every block for a BBD deck in FLOW, with no unchanged check (pow ×3, exp ×2, `SetStages` ×2). `Instrument::set_tempo_bpm` (`engine/instrument.cpp:219-248`) always runs `_apply_tempo()` → FLUX `update_thin_pattern()`. Each needs an unchanged-value guard; `Part::_engine_swap` relies on `set_flow`, so that use must keep working | 3-10 k per BBD deck in FLOW; ~1-2 k | 1 | BBD decks in FLOW; all | toggle |
| 6 | Sample-0 control work: `Center::update` computes cos/sin for MORPH and pow ×2 for `rate_drift` even at `w == 0` (`engine/center/center.cpp:140-222`), then `set_rate_scale` → `_apply_rate` on 10 lanes | 2-8 k | 1 | all | toggle |
| 7 | `control_tick` at `-O3` (today `-O2` because -O3 cost 14.3 KB; ITCM_HOT has 13 376 B free, §2) | 1-4 k of the 12-72 k law | 1 | all | build |
| 8 | COMP `update_curve` → `compute_gain` log10 + pow(10, …) on every control tick while the amount glides (`engine/fx/comp.cpp:63-73`) | 1-3 k | 1 | decks with LVL > 0.6 | toggle |
| 9 | Mix: limiter 17 k, blend + duck 17 k, observers 12 k; block rate where a per-sample value is block-constant | 5-15 k | 1 | all | toggle |
| 10 | Scan path 16 k; `mux_plan` at `-Os` (-O3 costs ~2.6 KB of SRAM_EXEC, which has 29 424 B free) | 1-3 k | 1 | all | build |
| 11 | The engines: SYNTH 188 k, WAVE 179 k (max 291 k), SAMPLER 124 k p50 (max 385 k) per deck in the hottest block; per-engine kernel work, sized only after rows 1-10 | open | 1-2 | per engine | toggle |

The hot-path libm scan (`hotpath_libm.py`: 61 hot functions, 132 libm call
sites) shows control-rate setters (rows 5, 6, 8), not per-sample work. Its
`daisysp::CrossFade::Process` `sinf`/`cosf` is the CPOW branch; the one
user, `KsString` (BODY), runs LIN, so the branch never runs.

Even the upper estimates of the class-1 rows 1, 2 and 5-10 add up to about
130 k. That leaves the worst listener case (1 057 583) near 930 k. The
880 000 stop criterion cannot be reached without class 2 (rows 3-4) or
engine work (row 11), whose size is open. Task 10 starts with row 1, the
only row with a measured section and a known mechanism. Row 2 gets its
probe before anything is built.

## 5. The candidate loop (plan Task 10), 2026-10-30

### How a verdict is reached here

The plan's verdict compares `frame_max` p99 / max of interleaved base and
candidate runs against the A/A floor. On the static workloads of §4 that
statistic does not hold still over 60 s. Two base runs of the same
workload, a minute apart on the same image, differ by up to 135 k
(BBD_SYNTH_rnd4: 844 182 / 980 921). One candidate run of WAVE_SYNTH_rnd19
reads 947 599 and the next 828 179. SAMPLER_SAMPLER_rnd17 trips 5 times in
one minute and 38 in the next. The workloads' modulation keeps moving, so
the worst minute is not the same minute twice. The §2 floors (2-3 k, from
300 s of `bastian` and corner 14) do not transfer to them.

**Ruling:** a class-1 candidate whose output is bit-identical (proven by a
test against the old code) is judged on its own section's window means.
That means the same measurement image, base and candidate interleaved on
several workloads it applies to. Such a candidate cannot cost: the path
strictly does less for the same result. Its saving is the section delta.
Whether the worst cases then reach 880 000 is checked on the listener once
the class-1 rows are in, not per candidate. Cost if wrong: a section saving
that never reaches a worst block gets counted.

### Candidate 1: GRIT REDUCE, the discarded Decimator step — adopted

`GritDecimator` (`engine/fx/grit.h`) replaces `daisysp::Decimator` with the
same arithmetic. `step()` is the state alone; `process()` is the state and
the bitcrush. `GritReduce::process` used to run the full Decimator twice
per frame and throw away the first result. It now calls `step()` and then
`process()`. Tests: `GritDecimator` equals `daisysp::Decimator` bit for bit
(5 intensities, 3 crush factors). `GritReduce` equals an inline reference
built from daisysp parts as the old code was, bit for bit, at 5
intensity/mix settings; RED shown by skipping one `step()` (3 583
mismatches). Desktop `ctest -E "^reva_"` 43/43 (controller-run). Commits
b564e7c9 (behind toggle bit 0), eb439537 (the only path; bit 0 retired).

| run (measurement image) | GRIT stage 1 per REDUCE deck | GRIT total per REDUCE deck |
|---|---|---|
| base, 843baa3 bit 0 off (w313, w169, w117, w304, w282) | 21.7-22.1 k | 58.1-58.7 k |
| toggle, 843baa3 bit 0 on (same five) | 13.7-14.4 k | 50.5-50.9 k |
| final, eb43953 (w313, w304) | 7.1-7.3 k | 43.0-43.4 k |

**−15.2 k per REDUCE deck per block.** The worst workloads have one or two
REDUCE decks (§4), so −15 k to −30 k. The toggle build measured half of
that. There the decimator stayed out of line in ITCM, called from
`GritReduce::process` in SRAM_EXEC through a veneer. In the final build
the step and the bitcrush inline into `GritReduce::process`, and the
decimator's ITCM entry is gone (the in-build guard requires that).
