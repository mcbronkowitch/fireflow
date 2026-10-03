# The panel scan — measured on the coupon

> **This document is board session 1 of the panel scan, part 1.** It records
> what the test coupon said on **2026-09-28**, on one board, in one session:
> five complete run blocks of the scan-check image, with the engine playing
> and the codec running. Its question is the one
> [`scan-budget.md`](scan-budget.md) §7 left for the bring-up: in the shell's
> own ADC pattern, one mux step written per audio block and read back one
> block later, does every channel come out clean?
>
> **The answer, in one line:** **yes.** On all 24 coupon steps, in all five
> blocks, the value read one block after its address was written matches
> the value read with the address parked to within **0.61 counts**, against a
> criterion of 8. That includes the **15 steps whose predecessor sits
> 14 844–62 605 counts away**, and on every one of them the control arm,
> which reads in the same block as the write, misses by **14 843 counts or
> more**. The pots' noise sets the hysteresis at **H = 16**.
>
> **Every number here is either measured or derived, and each one says
> which.** No mechanism is asserted. The instrument is
> `shell/scan_check.cpp`, built behind `SHELL_SCAN_CHECK`, spec
> [`2026-09-28-coupon-panel-scan-design.md`](../superpowers/specs/2026-09-28-coupon-panel-scan-design.md)
> §4. The image that produced every figure below is commit **`c04ba77`**,
> and every `SHELL_SCAN_CFG` line in the capture carries that stamp.

## 1. What the instrument is

The coupon's 24 mux steps (the 4067's 16, then the 4051's 8) are read in
three arms, back to back, in one run block of 4729 audio blocks. Each arm reads
every step 64 times and keeps `n`, `sum`, `min` and `max` of the raw 16-bit
DMA word of the step's live sense pin (`hw.adc.Get()`, not libDaisy's
`AnalogControl`, spec §2).

| arm | per audio block | what it asks |
|---|---|---|
| **S**, the shipping pattern | read the step selected one block earlier, then select the next; 64 full sweeps | the value the playing image will see |
| **P**, parked | select a step, then read it in blocks 5…68 after the select | the reference: the same channel, long settled |
| **0**, lag zero | select a step and read it in the same callback | the control: a read that has had no time |

The quantity is `S − P` per step, within one run block. The criterion is
**|S − P| ≤ 8 counts**, half an LSB of 12 bit, as in every earlier round.
Arm 0 exists so that a clean S means something: gate G3 (§2) requires it to
miss P on every step where missing is possible.

The engine runs at the shell's fixed operating point (`set_tempo_bpm(96)`,
`RATE_A` 0.4, `DENSITY_A` 0.6), audio is started, and nothing the scan reads
reaches the engine. **The scan's tick runs first in the callback, before
`process()`** (spec §2). Derived from the plan's block counts: arm S's 64
reads of a step spread over 3.07 s (64 sweeps of 24 blocks), arm P's over
128 ms, and a run block lasts 9.46 s at 2 ms.

## 2. The run, and its gates

**Measured**, every complete block (`SHELL_SCAN_CFG`, `_HEALTH`, and the
gates `read_scan_check.py` computes):

| | blk 2 | blk 3 | blk 4 | blk 5 | blk 6 |
|---|---:|---:|---:|---:|---:|
| `block`, `sr` | 96, 48000 | 96, 48000 | 96, 48000 | 96, 48000 | 96, 48000 |
| `ticks` / `expected` | 4729 / 4729 | 4729 / 4729 | 4729 / 4729 | 4729 / 4729 | 4729 / 4729 |
| G1: zero, rail (arm P's ties) | 0.1, 63483.4 | 0.1, 63483.4 | 0.1, 63483.4 | 0.1, 63483.3 | 0.1, 63483.2 |
| G1, G2 | pass, pass | pass, pass | pass, pass | pass, pass | pass, pass |
| G3: qualifying steps | 15 | 15 | 15 | 15 | 15 |
| G3: smallest \|0 − P\| on them | 14844.4 | 14844.4 | 14844.4 | 14843.5 | 14843.9 |
| criterion, largest \|S − P\| | pass, 0.48 | pass, 0.42 | pass, 0.61 | pass, 0.50 | pass, 0.53 |

The same 15 steps qualify in every block: 1–8 and 10 on the 4067, 17–20,
22 and 23 on the 4051. A step qualifies when its predecessor is on the same
sense pin and their P means differ by more than 4096 counts; on these 15 the
difference is 14 844 to 62 605 counts. The reader exits 0.

**What G2's tick half checks.** `ticks` is counted by the same callback that
closes the run block (`scan_check.cpp`), so `ticks == 4729` confirms the
double buffer's bookkeeping, not the audio clock. *Reasoned* from the source:
this image has no counter that would show a missed audio block.

**What was not used.** The host opened the port mid-stream. Lines 1–75 are a
partial opening: line 1 is a `blk=0` line, line 2 a truncated `blk=0` line
carrying the `$$` overflow marker with `blk=1`'s first `CH` line fused onto
it, and lines 3–75 the rest of `blk=1`, without its `CFG` line. All 72 of
`blk=1`'s `CH` lines are in the file, the first one fused onto line 2 behind
the stray `blk=0` text. The reader discards all of it. The complete blocks start at
lines 76, 151, 226, 301 and 376; the file ends on `blk=6`'s `END` at line 450.

## 3. The result

Per step, **measured**, the range across the five blocks. `S range` is
arm S's `max − min` over its 64 reads; `0 − P(prev)` is arm 0's mean against
the preceding step's P mean. What sits on each step is `coupon_expect.cpp`'s
table and the netlist's pot assignment
(`hardware/coupon/scripts/netlist.py`).

| step | mux, ch | what | P mean | S − P | S range | 0 − P | 0 − P(prev) | G3 |
|---:|---|---|---:|---:|---:|---:|---:|:---:|
| 0 | 4067, 0 | RV1, 10 k | 33158.3 … 33159.1 | −0.4 … +0.4 | 5–9 | −22426.1 … −22401.8 | — | |
| 1 | 4067, 1 | rail tie | 63483.0 … 63483.5 | −0.4 … +0.3 | 4–6 | −30325.1 … −30324.1 | −0.7 … +0.3 | yes |
| 2 | 4067, 2 | RV2, 10 k | 31656.7 … 31657.0 | 0.0 … +0.3 | 3–5 | +31826.2 … +31826.6 | −0.2 … +0.3 | yes |
| 3 | 4067, 3 | AGND tie | 0.1 | 0.0 … +0.1 | 1–3 | +31656.1 … +31657.0 | −0.4 … +0.2 | yes |
| 4 | 4067, 4 | RV3, 20 k | 48638.6 … 48639.0 | −0.4 … +0.5 | 5–8 | −48638.9 … −48638.5 | −0.1 … 0.0 | yes |
| 5 | 4067, 5 | rail tie | 63482.8 … 63483.2 | 0.0 … +0.5 | 4–6 | −14844.4 … −14843.5 | −0.3 … +0.7 | yes |
| 6 | 4067, 6 | RV4, 20 k | 31677.2 … 31677.8 | 0.0 … +0.5 | 4–8 | +31805.5 … +31805.9 | 0.0 … +0.7 | yes |
| 7 | 4067, 7 | AGND tie | 0.1 … 0.2 | −0.1 … +0.1 | 1–3 | +31677.0 … +31678.0 | −0.3 … +0.5 | yes |
| 8 | 4067, 8 | `REF_A`, 5150 Ω | 31734.0 … 31734.6 | −0.4 … +0.6 | 3–10 | −31734.5 … −31733.9 | −0.1 … 0.0 | yes |
| 9 | 4067, 9 | `REF_B`, 650 Ω | 31736.0 … 31736.5 | −0.1 … +0.5 | 3–8 | −2.4 … −1.3 | −0.2 … +0.5 | |
| 10 | 4067, 10 | AGND tie | 0.0 … 0.2 | 0.0 … +0.2 | 1–3 | +31735.8 … +31736.4 | −0.6 … 0.0 | yes |
| 11–15 | 4067, 11–15 | AGND ties | 0.0 … 0.2 | −0.1 … +0.1 | 1–3 | −0.1 … +0.1 | −0.1 … +0.1 | |
| 16 | 4051, 0 | RV5, 10 k | 30944.4 … 30945.2 | −0.5 … +0.3 | 3–5 | −24961.0 … −24911.9 | *other pin* | |
| 17 | 4051, 1 | rail tie | 63483.2 … 63483.5 | −0.2 … +0.5 | 4–6 | −32538.9 … −32538.6 | −0.6 … +0.2 | yes |
| 18 | 4051, 2 | RV6, 10 k | 31664.3 … 31664.6 | −0.2 … +0.4 | 3–4 | +31819.0 … +31819.6 | +0.1 … +0.5 | yes |
| 19 | 4051, 3 | AGND tie | 0.0 | 0.0 | 0 | +31664.3 … +31664.7 | 0.0 … +0.4 | yes |
| 20 | 4051, 4 | RV7, 20 k | 62605.3 … 62605.7 | −0.3 … +0.3 | 4 | −62605.7 … −62605.3 | 0.0 | yes |
| 21 | 4051, 5 | rail tie | 63483.6 … 63484.0 | −0.4 … +0.2 | 4–5 | −878.5 … −877.9 | −0.2 … +0.3 | |
| 22 | 4051, 6 | `REF_C`, 5150 Ω | 31765.5 … 31766.1 | −0.4 … +0.5 | 3–5 | +31717.5 … +31718.2 | −0.3 … 0.0 | yes |
| 23 | 4051, 7 | AGND tie | 0.0 | 0.0 | 0–1 | +31765.7 … +31766.0 | −0.2 … +0.4 | yes |

**S equals P.** The largest |S − P| anywhere is **0.61 counts** (step 8,
block 4); on the seven pots it is 0.53. *Derived:* the criterion holds with a
factor of 13 to spare. Steps 0 and 16, where the sweep changes sense pin,
are as clean as the rest.

**Arm 0 reads its predecessor.** On every step whose predecessor shares its
sense pin, arm 0 lands within **±0.7 counts** of that predecessor's P mean.
That is the reading spec §4 names as expected for a read with no time after
the write; it is reported, not gated. Step 16's column is not a predecessor
comparison: step 15 is on the 4067, step 16 on the 4051. Step 0 has no
predecessor in the table.

**Every single read was clean, not only the means.** *Derived*, assuming the
hardware average weights its 32 conversions equally: a read whose average
held even one conversion of the previous address would sit about ΔP/32 away
from the rest, at least **464 counts** on the qualifying step with the
smallest ΔP (step 5, 14 844) and **1956** on the largest (step 20, 62 605).
Two legs rule that out for all 15 × 64 × 5 = 4800 reads on those steps. Arm
S's `max − min` on them is 0–10 counts in every block, which rules out a mix
of such reads and clean ones within a step. And |S − P| is at most 0.61
counts on every step, which rules out every read of a step being shifted
alike.

## 4. What it answers for `scan-budget.md` §7

`scan-budget.md` §7 names three things its arithmetic assumed and this run
was to test.

| assumption | what this run says | class |
|---|---|---|
| **The oversampling order** — one channel's 32 conversions back to back | The outcome the documented order predicts, on every step: no read one block after the write carries the previous address (§3). The interleaved alternative puts a group across a whole rotation, which `scan-budget.md` §3's arithmetic does not fit into a block | **derived** from the measured S = P; the order itself was **not timed** |
| **The slack** — the value is clean one block after the write | Holds, with the engine playing at the fixed operating point and the codec running | **measured**, at that operating point only |
| its size, ~303 µs | Nothing here measures it; the run shows it is not negative | **not claimed** |
| **F1 on real pots under a stepping mux** | All seven pots, 10 k and 20 k, on both muxes: S within 0.53 counts of P | **measured** |

**What the placement licenses about other engine loads.** The tick runs
before `process()`, so the interval between a select and its read is the time
between two callback starts. *Reasoned:* `process()`'s duration enters that
interval only if a callback overruns its block, and the 2026-08-19 bench puts
the worst-case instrument 2.9 points inside the block (`scan-budget.md` §2).
That is an argument, not a measurement: the only engine load measured here is
the fixed operating point, and this image could not show a missed block if
there were one (§2).

**Against the long rung.** `scan-budget.md` §7 asked for a comparison with
the pots' long-rung readings from `pots-measured.md`. Those came from the wait
probe's own conversions, not from libDaisy's path, and the two paths read on
different scales (`pots-measured.md` §1). The spec replaced that comparison
with arm P, in the same image. What the two can still be set against is
position, each on its own scale. *Derived:* over this image's own span, RV2,
RV4 and RV6 sit at **x = 0.4987 / 0.4990 / 0.4988**; `pots-measured.md` §3
derived **0.4987 / 0.4992 / 0.4988** from the long rung. Stated as a
correspondence between two instruments; it cannot separate a pot that moved
by a few counts from an instrument that differs by a few.

## 5. The noise, and H

**Measured**, arm S's `max − min` over 64 reads, widest across the five
blocks: RV1 **9**, RV2 5, RV3 8, RV4 8, RV5 5, RV6 4, RV7 4 counts. The
widest in any block is RV1's 9, in block 6.

**H = 16.** Spec §4's rule: the widest pot range across all complete blocks,
rounded up to a multiple of 16, and never below 16. 9 rounds to 16, and so
does the floor, so the measurement did not set the band; the floor did.
`read_scan_check.py` prints `widest S range 9 counts over 5 block(s) -> H=16`,
and `shell/scan_value.h` carries it as `kPotHysteresis = 16`.
`read_scan_check_guard` recomputes H from the committed capture and fails if
the two differ.

**Measured**, the pots' P means across the five blocks: no pot moved more
than **0.8 counts** (RV1 and RV5). *Derived:* the P readings compared, block 2
against block 6, lie about 38 s apart (4 × 9.46 s); the five blocks together
span about 47 s.

What H covers: seven pots standing still, each read over about 3 s at a
time, in one session. It does not cover a pot being turned, a longer drift,
another board, or the panel's own muxes.

## 6. What this rests on

One board: one coupon, one populated copy, one Patch Submodule, the same one
as the pot round. One session, one image, one capture, five complete run
blocks. RV2, RV4 and RV6 were still at mid travel from the pot round; the
other four pots were wherever they had been left. The engine and codec ran
at the one fixed operating point.

Before the app was flashed, the wavetable bank `shell/build/shell-qspi.bin`
was written to `0x90100000`. Whether a bank had been there before could not
be checked. *Observed in the session, not in the capture:* a DFU upload
(`dfu-util -U`) of `0x90100000`, and one of `0x90040000` right after the app
was flashed there, returned the same bytes, matching neither image, so the
upload did not show what the flash holds. Why is not claimed. The engine ran
during the measurement, which is why the bank is recorded here.

| claim | class |
|---|---|
| Every table in §2, §3 and §5 | **measured**, `SHELL_SCAN` lines, five blocks |
| Every mean, `S − P`, `0 − P`, H | **derived**, `read_scan_check.py` |
| The factor of 13, the 3.07 s and 128 ms windows, the pots' x | **derived** |
| No single read held a conversion of the previous address | **derived**, assuming an equal-weight average of 32 |
| The oversampling order is the documented one | **derived** from the outcome; not timed |
| The slack holds with the engine running | **measured**, at the fixed operating point |
| The slack holds at other engine loads | **reasoned** (§4), not measured |
| That `ticks` would show a missed audio block | **not claimed**; it would not (§2) |
| The two DFU uploads returned the same bytes, matching neither image | **observed** in the session, not in the capture; no cause claimed |
| Any mechanism | **none** |

## 7. Open, characterised, deliberately unexplained

- **Arm 0 at steps 0 and 16.** Arm 0 reads **10 732–10 757** counts on step
  0 and **5 983–6 033** on step 16. That is neither the step's own P nor its
  table predecessor's, and it moves 25–49 counts between blocks, where every
  other arm-0 cell holds within 1.03 counts. Both steps change sense pin,
  neither qualifies for G3, and arm S is clean on both. Recorded, not
  explained.
- **The rail.** G1's rail reads 63483.2–63483.4, against the 63485 that
  `kPanelSpan` carries from `settle-measured.md` §7 (2026-09-17). 1.6–1.8
  counts, inside H. Recorded.
- **The size of the slack**, and the scan at any engine load other than the
  fixed operating point (§4).
- **A missed-block counter.** The image cannot show one (§2). If a later image
  measures other engine loads, it needs one.
- **H on a turned pot.** Board session 2 (2026-09-28, image `cbd6270`)
  turned the pots with the playing image. By ear (Bastian), `FILT_A` did not
  step audibly. *Observed, unattributed:* at the end of the second
  recording, after the turning, RV2's emitted value moved by more than 60 raw
  counts over about 10 s, beyond H, while RV4 and RV6 held still; whether the
  knob was being touched was not recorded, and no cause is claimed. RV4's low
  stop, not seen in the session, was read later the same day: all three pots
  fully left gave `rv2=0 rv4=0 rv6=0` on every `SHELL_PLAY` line. The session is recorded in the M6 entry of 2026-09-28 in
  [`docs/roadmap.md`](../roadmap.md).

## 8. How to repeat it

The capture is committed as
[`captures/scan-check-capture-c04ba77.txt`](captures/scan-check-capture-c04ba77.txt)
(450 lines: a partial opening, then five complete blocks from line 76).
`read_scan_check_guard` parses it on every `ctest` run and pins G1, G2, G3
and the criterion on every block, and `kPotHysteresis` against its H. From
the repository root:

```bash
python shell/read_scan_check.py --file docs/hardware/captures/scan-check-capture-c04ba77.txt
```

It exits 0 and prints the per-block tables of §3 and the H line of §5.

On the board: build the image with the Daisy toolchain (never in a shell that
sourced `env.sh`), and before flashing, `cmp` it against the plain
`SHELL_COUPON_PROBE=1` image as spec §6 requires:

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_SCAN_CHECK=1
```

Flash over DFU as `shell/README.md` describes: the wavetable bank
(`build/shell-qspi.bin`, to `0x90100000`) on a board that may lack it, then
the app (`build/shell-sram.bin`, to `0x90040000:leave`). Record; this
session's 60 s gave five complete blocks after the one the port opened into:

```bash
python shell/read_scan_check.py COM4 capture.txt 60
```

## P6a coupon session (2026-10-03)

The coupon play image of P6a (`SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1`,
main 757f072b, `SRAM_EXEC` 260524 B) on the coupon, flashed over the Daisy
bootloader's DFU to `0x90040000`. It scans the coupon's wiring with Rev A's
parallel model (`kCouponPlayChain`: the 4067 and the 4051 enabled together).
The serial log was captured with a timestamping pyserial reader for 147 s:
230 `SHELL_PLAY` and 229 `SHELL_PLAY_IO` lines. Checks from spec §7:

1. **Pots: pass.** RV2, RV4 and RV6 each reached both stops: `rv4=1000`,
   `rv2=1000`, `rv6=1000`, and every pot returned to 0. Every rest stretch
   repeats exactly (`rv2=0 rv4=0 rv6=0` over dozens of lines). All of those
   stretches were at the 0 stop; no rest in mid-travel was captured.
2. **Key: pass.** `presses` steps 0 → 1 → 2 → 3 → 4 → 5 over the five
   presses (71.6–74.5 s). It then reads 6 and 7 for the two holds that
   followed. `keys=1` appears in the lines written while the key was down.
   Bastian saw LED_1 lit while SW1 was held.
3. **Held key, pots untouched: pass.** During the long hold (86.5–101.3 s,
   `keys=1 presses=7`) and in every other line with `keys=1` (33 lines in
   all), `rv2`/`rv4`/`rv6` repeat exactly. Caveat: all three pots sat at the
   0 stop during the hold, so this is the weakest position for showing LED
   switching coupling into a wiper.
4. **D8/D9: not run.** No jumpers were at hand for `TP_ADC12`→`TP_AGND` and
   `TP_ADC11`→`TP_A3V3`. Unjumpered, `adc11` and `adc12` float (for example
   6018 / 6463, then 10489 / 7219 one line later), which says nothing about
   the mapping. Deferred to P7 bring-up by decision (Bastian, 2026-10-03),
   where P2 §7 lists it: a swapped mapping would show on Rev A as SENSE_2
   and SENSE_3 trading places, fixed by two entries of `ADC_OF_PIN` in
   `shell/gen_panel_map.py`.

**Also seen:**
- The span stayed valid the whole time: `zero=0`, `rail` 63482–63485.
- `sweeps` rose from 31 to 5093 in 146.5 s of host time, 34.6 sweeps/s. That
  is a 1.81 ms step against the 2 ms the README assumes. Not part of the four
  checks, and not explained here.
