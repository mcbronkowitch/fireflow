# Shell mux scan — what it costs and where it runs

**Date** 2026-08-23 · **Board** `patch_sm`, serial `3859386B3330` · **Git**
`978cbaf` (working tree carried unrelated `host/vcv/` edits; nothing under
`shell/`, `engine/` or `src/` was dirty) · **Optimization** `-O3` (fixed by
`override OPT` in `shell/Makefile`) · **Transport** USB CDC · **Runs** two per
image, second run by RESET (fresh boot, no re-flash).

Plan:
[`docs/superpowers/plans/2026-08-23-mux-scan-placement-probe.md`](../superpowers/plans/2026-08-23-mux-scan-placement-probe.md),
Tasks 4 and 5. Nothing is attached to the four chain pins in either half. The
CPU half below runs on `3859386B3330`; the audio half runs later the same day on
the submodule with the 3.5 mm jacks, `385138563330`.

## What ran

`shell/` built with `SHELL_CPU_PROBE=1` in three positions of
`SHELL_MUX_PROBE`: 0 off, 1 the scan inside the audio callback, 2 the scan in
the foreground paced at one step per audio block. One step = 32 bit-banged
chain bits (four 74HC595: 19 LEDs, four address lines, two enables) plus four
`GetAdcValue` reads. The values are stored to a volatile array and **not**
pushed into the engine, so all three images run the identical operating point.

The three images were verified byte-distinct before flashing — see "The build
was lying" below.

## The numbers

| image | mux | avg % | max % | min % | steps | blocks |
|---|---:|---:|---:|---:|---:|---:|
| baseline, run 1 | 0 | 74.33 | 76.70 | 55.54 | 0 | 2500 |
| baseline, run 2 | 0 | 74.33 | 76.67 | 55.57 | 0 | 2500 |
| callback, run 1 | 1 | 74.14 | 76.58 | 55.38 | 2500 | 2500 |
| callback, run 2 | 1 | 74.15 | 76.60 | 55.42 | 2500 | 2500 |
| foreground, run 1 | 2 | 74.35 | 76.64 | 55.62 | 2500 | 2500 |
| foreground, run 2 | 2 | 74.36 | 76.63 | 55.55 | 2500 | 2500 |

`sr=48000`, `block=96`, 2500 blocks (5 s) per run, load in hundredths of a
percent of the block budget as reported by the board itself.

Run-to-run spread inside one image: **0.03 points** on `max` at worst. That is
the resolution of this measurement, and it is the number every delta below has
to be read against.

| image | Δ avg vs baseline | Δ max vs baseline |
|---|---:|---:|
| callback | **−0.19** | **−0.10** |
| foreground | **+0.03** | **−0.05** |

## What it says

**1. The scan's cost is below what this measurement can resolve.** The callback
variant does 32 chain bits and four ADC reads every single block and comes out
*under* the baseline. A negative surcharge is not physical, so the honest
statement is: at one step per block the scan costs less than ~0.2 points, and
at that size the difference between the two images' code layout is bigger than
the work itself. What is ruled out is the fear io-budget §6 point 1 named —
that a per-block chain write "lands right in" the 2.9-point reserve. It does
not, by more than an order of magnitude.

The transfer to the bench's worst case is legitimate because the scan's work is
fixed per block and independent of what the engine is doing: 32 bits and four
reads cost the same under `instrument_worst_bbd_dtcm` as they do here. What may
not be transferred is the baseline itself — 76.7 % is this shell's one
operating point on SYNTH, not the bench's worst-case instrument at 97.0–97.2 %.

**2. The foreground keeps up completely.** `steps=2500` against `blocks=2500`
in both runs: not one step opportunity was missed, and the callback load sits
on the baseline (avg +0.03, inside the noise). The placement question — the one
nobody had asked — therefore has an answer: **the scan does not have to be in
the audio callback**, and if it is not, it does not enter the audio budget at
all. The CPU reserve is not the constraint on this problem.

**3. The settle time stops being a cost item at this rate.** One step per block
means the address is written at the end of a block and sampled at the start of
the next, so the block period *is* the settle window — 2 ms. A full sweep of 32
steps takes 64 ms, i.e. ~15.6 Hz per channel with all eight chips populated,
~31 Hz with four. That is arithmetic, not a measurement, and it is the rate the
numbers above belong to. Whether 2 ms is actually enough for a 74HC4067 into
the real node is still Phase-0 Task 6 step 5b's question and still needs parts
on the table.

## Honest limits

- **Nothing is attached to the four pins.** The drive current is lower than
  production, where each edge charges a real chain's input capacitance. For the
  CPU number this barely matters — a `BSRR` write costs what it costs — but for
  the audio question in Task 5 it means a null result is a lower bound, not an
  acquittal.
- **The bit-bang clocks with no delay between edges**, so at 480 MHz two
  register writes are nanoseconds apart. If a real 74HC595 chain needs a delay,
  the cost measured here is optimistic. Given how far under the resolution it
  came out, there is room for that delay.
- **The apply side is not priced.** The images deliberately do not push values
  into the engine, so `map_control` and any smoothing or dead-band per channel
  are not in these numbers. That is a switch statement over 67 channels at
  ~15.6 Hz; it is not where the risk was.
- **The reserve row is an `-O2` bench image** (`instrument_worst_bbd_dtcm`,
  97.02–97.16 % `pct_max`,
  `docs/bench/2026-08-19-3def5d5-feed-axi-o2-patch_sm-usb.md`) while the shell
  is `-O3`. The primary result here is shell-against-shell at identical
  optimization; the reserve only sizes the verdict.

## The audio side

**Board** `patch_sm`, serial `385138563330` — the submodule with the 3.5 mm
jacks, not the board of the CPU half · **Images** `shell-audio-mux{0,1,2}.bin`,
the same three switch positions built with `SHELL_CPU_PROBE=0`, md5-verified
distinct before flashing · **Capture** `ffmpeg -f dshow -i audio="Line
(Universal Audio Twin USB)" -t 10 -ac 2 -ar 48000`, interface gain set once and
untouched across every run below · **Analysis** `python tools/blockrate_fft.py
<wav> --block-rate 500` — RMS in a ±5 Hz band around the block rate, reported
relative to total RMS so the unknown DAC-to-interface damping cancels.

| image | placement | total RMS | 500 Hz band | excess |
|---|---|---:|---:|---:|
| mux0, run 1 | scan off | −55.14 | −62.93 | **−7.79** |
| mux0, run 2 | scan off | −55.31 | −62.96 | **−7.65** |
| mux1, run 1 | audio callback | −54.92 | −63.17 | **−8.26** |
| mux1, run 2 | audio callback | −55.02 | −63.19 | **−8.18** |
| mux2, run 1 | foreground | −51.75 | −55.73 | **−3.98** |
| mux2, run 2 | foreground | −51.61 | −55.69 | **−4.08** |
| mux2, run 3 | foreground | −51.52 | −55.64 | **−4.12** |

All figures dBFS; `excess` is band minus total, in dB. Run-to-run spread within
one image is **0.15 dB** on `excess` at worst — the resolution of this half, and
the number the deltas have to be read against.

**4. The callback adds no block-rate artifact.** `mux1` sits 0.5 dB *below* the
baseline. That is outside the spread, but it points the wrong way for an
artifact: switching the chain from inside the audio callback does not put energy
on the block rate.

**5. The foreground is the dirty placement, by a lot.** `mux2` runs **+3.6 dB**
`excess` against the baseline and **+4.2 dB** against the callback, at 0.15 dB
resolution.

**5b. What it raises is the artifact alone, not the floor.** Splitting each
capture into the 500 Hz harmonic series (40 harmonics, ±5 Hz each) and
everything else:

| image | total | 500 Hz series | residual |
|---|---:|---:|---:|
| mux0, runs 1 / 2 | −55.14 / −55.31 | −60.57 / −60.58 | −56.60 / −56.84 |
| mux1, runs 1 / 2 | −54.92 / −55.02 | −60.74 / −60.76 | −56.23 / −56.37 |
| mux2, runs 1 / 3 | −51.75 / −51.52 | **−53.33 / −53.23** | −56.92 / −56.40 |

The residual is the same in all three images — 0.7 dB of spread, no trend, and
that is the comparison this table is for; its absolute value is a function of
which stretch of a wandering program got captured (see 5c). The
foreground's entire 3.6 dB of extra total RMS is the harmonic series rising
7.3 dB. The callback's series is within 0.2 dB of the baseline's, which is the
same null as finding 4 seen a second way and is the stronger statement of it:
the callback does not touch the artifact at all.

**5c. The artifact was already there, and it is audible.** In the baseline —
scan off — the 500 Hz series sits at −60.6 dBFS: a pulse train with a full
harmonic series, and Bastian hears it as a standing tone with the synth signal
distorting oddly on top. That report was volunteered while `mux2` was running,
i.e. against the worst of the three. Nothing clips: peak sample 0.008 of full
scale, crest factor 19 dB. This is the 8 Aug tone, undiminished, and it is not
the mux's doing — it is present with the scan switched off. It belongs to its
own session.

**How far under the program it sits needs a longer window than these captures.**
Against the 8–10 s baselines it computes to 4 dB under; against a 30 s capture
taken later the same day it is **9.2 dB** under (program −51.67 dBFS, series
−60.82). The program is a generative ambient patch and swings 6.5 dB from one
second to the next, so a short window lands wherever it lands — the short one
caught quiet stretches. The 30 s figure is the one to quote. What does *not*
move is the series itself: across twelve captures today, three images, and both
states of the MAX11300 module, it stayed inside −60.2 to −61.2 dBFS. It tracks
the interface gain and nothing else — which is the 8 Aug finding ("not in the
samples") seen from a third angle.

**6. The placement question is closed, opposite to the plan's expectation.** The
plan treated the callback as the placement that had to prove itself and the
foreground as the safe fallback. The CPU half found the callback's cost below
resolution; the audio half finds the foreground measurably worse. **The scan
goes in the audio callback.** Nothing is left that argues for the foreground.

**The time-since-boot confound was controlled.** The `mux0` pair was recorded
minutes after its boot, the `mux1` pair immediately after theirs — so a slow
drift after reset would have masqueraded as a placement effect. `mux2 run 1`
(immediately after boot) and `mux2 run 3` (minutes later) differ by 0.14 dB,
inside the spread. This metric does not drift with time since reset.

### Honest limits, audio half

- **The mechanism is not measured and gets no name here.** One thing is ruled
  out: it is not step *rate*. Both placements clock exactly one step per audio
  block (`steps=2500` against `blocks=2500` — measured on the CPU-probe build at
  the same two switch positions, not on these audio images). Everything else is
  open.
- **Nothing is attached to the four pins**, here as in the CPU half. The real
  chain's input capacitance draws current these images never draw, so the
  callback's null is a lower bound rather than an acquittal, and the
  foreground's +3.6 dB is a lower bound too.
- **The absolute levels above are the connected channel, not a mix.** Only one
  side of the interface is patched — the rig is mono by design — and the first
  pass of this analysis averaged the live channel with the dead one, putting
  every absolute dBFS figure 6.02 dB low. That gap was briefly written up here
  as a moved operating point; it was arithmetic. Corrected the same day by
  recomputing from channel 0, and `tools/blockrate_fft.py` now drops channels
  more than 20 dB under the loudest and prints which it used. Every ratio in
  this section — `excess`, series against residual, and all deltas — was
  unaffected, because a dead channel in a mean is a constant factor.
  **Corrected, the baseline matches the 8 Aug capture** (−54.6 dBFS total)
  to half a dB, so the operating point did not move. The 8 Aug `excess` of
  −4.9 dB is still not a like-for-like partner: it came out of an ad-hoc numpy
  session, not this tool.
- **The audio images print no identity receipt** — `SHELL_CPU_PROBE=0` removes
  the line that would say which build is running. The evidence that three
  different firmwares ran is three distinct md5 sums and three separate flashes,
  not a receipt from the board.
- **10 s per run on non-stationary material.** The agreement across two and
  three runs is what bounds that, not the length of a single capture.

## The build was lying, twice, and the plan's cmp check caught it

Worth recording because it nearly produced three measurements of one firmware
under three names.

The generated-switch-header mechanism (`write_shell_*.py` + a make dependency
edge) has a **same-second hole**: the header was written 0.36 s after `main.o`
was compiled, make on this machine judged it "not newer", and the images for
`SHELL_MUX_PROBE=1` and `=2` came out **byte-identical**. Deleting the stale
object from inside the recipe did not fix it either — make has already read the
directory and does not notice the file vanish mid-run. The switches are now
resolved at **parse time** through `$(shell ...)`, before the dependency graph
exists.

The same hole sat one level up: `shell-sram.bin` could be "not newer" than the
ELF that had just replaced it, so the three `objcopy` steps now take `FORCE`.

`SHELL_CPU_PROBE` had the identical hole and Task 5 flips it, so it was fixed in
the same pass and round-tripped `1 → 0 → 1` to an identical ELF hash. Fixed in
`978cbaf`.

## Still open

- **Why the foreground places more energy on the block rate than the callback.**
  Measured, unexplained, and deliberately not named. It does not block the
  decision — the decision went to the callback — but it is the one result here
  that nobody predicted.
- **The pre-existing 500 Hz harmonic series** (finding 5c), 4 dB under the
  program with the scan off. Diagnosed on 8 Aug, still unfixed, and now with a
  measurement rig and a number attached to it. This capture only establishes
  that the mux question is independent of it.
- **Both halves ran with floating chain pins.** The whole thing wants repeating
  once a real 74HC595 chain and a 4067 hang on B7/B8/D1/D10.
- **Phase-0 Task 6 step 5b**, the settle time per channel, and with it the 8:1
  against 16:1 choice.
