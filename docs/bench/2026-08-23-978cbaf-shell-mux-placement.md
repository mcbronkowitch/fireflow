# Shell mux scan — what it costs and where it runs

**Date** 2026-08-23 · **Board** `patch_sm`, serial `3859386B3330` · **Git**
`978cbaf` (working tree carried unrelated `host/vcv/` edits; nothing under
`shell/`, `engine/` or `src/` was dirty) · **Optimization** `-O3` (fixed by
`override OPT` in `shell/Makefile`) · **Transport** USB CDC · **Runs** two per
image, second run by RESET (fresh boot, no re-flash).

Plan:
[`docs/superpowers/plans/2026-08-23-mux-scan-placement-probe.md`](../superpowers/plans/2026-08-23-mux-scan-placement-probe.md),
Task 4. Nothing is attached to the four chain pins — this prices the CPU side
only. The audio side (Task 5) needs the submodule with the 3.5 mm jacks and is
not in this capture.

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

- **Task 5**, the audio side: does a per-block chain burst move the block-rate
  tone? Needs submodule `385138563330` with the 3.5 mm jacks.
- **Phase-0 Task 6 step 5b**, the settle time per channel, and with it the 8:1
  against 16:1 choice.
