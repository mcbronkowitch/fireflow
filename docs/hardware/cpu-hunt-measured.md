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
