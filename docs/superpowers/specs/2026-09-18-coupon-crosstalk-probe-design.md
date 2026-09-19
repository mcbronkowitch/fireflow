# The coupon crosstalk probe — design

**Date:** 2026-09-18
**Status:** BUILT, flashed and measured. Results in
[`docs/hardware/crosstalk-measured.md`](../../hardware/crosstalk-measured.md)
(2026-09-18 capture; §13 is a 2026-09-19 re-measurement on a later image
that fails G6 and revises none of the numbers above it)
**Closes:** the first item on `settle-budget.md` §5's list of what the model
cannot see — crosstalk from the board's own switching into a settled mux
channel — and the attribution of the two open observations in
`settle-measured.md` §5 (the 8–12 count wander, the 100–200 count raw
excursions)
**Round two, separate spec:** the codec tone as aggressor,
[`2026-09-18-coupon-codec-tone-probe-design.md`](2026-09-18-coupon-codec-tone-probe-design.md)

## 1. What this is

A firmware probe that holds one multiplexer channel still, fires one
controlled digital event on the same board at a chosen time before the ADC's
aperture, and reports how far the reading moves. It answers two questions
that [`settle-measured.md`](../../hardware/settle-measured.md) left open in so
many words:

- §5: the settled state on every divider channel wanders 8–12 counts peak to
  peak and is still doing it at the end of a 12.8 µs grid, while the 0 Ω rail
  ties hold to 0–1. *"If the 8–12 counts is an artifact of something
  unexamined — supply ripple, USB DMA activity, the probe's own 595 clocking —
  then a settled region inside 8 counts is achievable."* Nothing there tests
  that.
- §5, second observation: single raw conversions 100–200 counts off in
  otherwise clean settled regions, on the 0 Ω ties as well, *"exactly the
  crosstalk question this coupon exists to ask, and it is not answered here."*

And behind both, the panel question the coupon was built for: the shipping
scan updates a 595 chain that carries mux addresses, enables and LED bits in
one word, every audio block, on the same board as 65 pots. Does anything the
digital side does move a pot reading, at the moment the reading is taken, by
more than the criterion of half an LSB of 12 bit (8 counts)?

It is **not** a settle-time measurement — that instrument exists and its
results stand — and it is not a scan. It produces two CSV files over USB-CDC
and nothing else.

**It depends on the settle probe having run**, in the strong sense that it
reuses that probe's ADC primitives, its measured clock, its gates G2 and G4
and its grid, and it inherits their limits. Nothing here is re-derived; where
this document quotes a number from `settle-measured.md` it is quoting a
measurement, and it says so.

## 2. What this coupon can pit against what

The difficulty is not how to measure crosstalk but what there is to measure.
The channel plan (`hardware/coupon/scripts/design.py`, `MUX16_CHANNELS`) holds
the measured channels' neighbours at opposite rails through 0 Ω. That is the
right rig for the *settle* question — the step into the target is maximal —
but `hardware/coupon/README.md` already argues why it is nearly worthless for
crosstalk: a node held at a rail through 0 Ω is a static node, and a static
node is a poor aggressor. The settle probe's own transient *is* the only test a
static neighbour can give, and it has been given.

What moves on this board, from firmware, without rework, is everything the
595 chain drives plus what the submodule itself does. Read off `netlist.py`:

| Aggressor | Wired how | Path to a victim | Firmware control |
|---|---|---|---|
| `MUX8_EN_N` | `U_SR1.QF`, one of the six control nets that cross the moat on the west edge, 70–74 mm from `JP_GND` (README, second deviation) | trace coupling across the moat and along the 8:1's column, past the three longest 0 R ties (`R_HI3`, `R_HI4`, `R_LO4`); charge injection as the 4051's switches open or close, into `MUX8_COM` = `ADC_10`, the pin beside the victim's `ADC_9` | one bit of the chain word; victim on the 16:1 |
| `MUX16_EN_N` | `U_SR1.QE`, same bus | the mirror image; charge injection into `MUX16_COM` = `ADC_9` | one bit; victim on the 8:1 |
| `MUX_A3` | `U_SR1.QD`, same bus; **the 8:1 does not use it** (`netlist.py:163`, "the 8:1 uses three of the four") | with `MUX16_EN_N` high: a bare address line switching across the moat, no mux acts on it. With it low: the same edge plus the 16:1 changing channel into `ADC_9` | one bit; victim on the 8:1 |
| LED word | `U_SR1.QG/QH` and `U_SR2.QA..QF`, eight LEDs at 1 kΩ from the digital `+3V3` | current steps through the digital planes and the single ground join `JP_GND`; the DC state shifts the star point by I·R, the edge is di/dt | eight bits; any victim |
| 595/165 shift clock | `SR_CLK`/`SR_DATA_OUT` from B7/B8 into `U_SR1`, `U_IN1` | digital supply and ground activity with **no** change on any mux control line, because `RCLK` is not pulsed | a shift without a latch; any victim |
| USB-CDC, the probe's own `PrintLine` | on the submodule | DMA and interrupt activity on the MCU, named in `settle-measured.md` §5 as an unexamined candidate | print inside the block, or buffer and print after |

What **cannot** be pitted against a victim: `MUX_A0..A2` against the mux
being read, because both muxes share them — moving them moves the victim.
And the codec, which needs the audio path started and its own sequencing;
that is round two.

The victims are the same four source impedances the settle probe used, and
for the same reason: a disturbance that grows with `R_src` is charge or
current arriving at the node; one that stays flat across 0, 650 and 5150 Ω is
in the ground, the reference or the ADC. The R-ladder is the attribution axis
that decided §7 of the measured document, and it decides this.

| Victim | Group, channel | R_src | Role |
|---|---|---:|---|
| `REF_A` | 16:1, ch 8 | 5150 Ω | pot at mid travel, on the 4067 |
| `REF_C` | 8:1, ch 6 | 5150 Ω | the same on the 4051 |
| `REF_B` | 16:1, ch 9 | 650 Ω | an order of magnitude down: the impedance control |
| `R_SP10` | 16:1, ch 10 | 0 Ω to `AGND` | the instrument's zero |
| `R_LO3` | 8:1, ch 3 | 0 Ω to `AGND` | the same on the 4051 |

**The seven pots being unpopulated does not matter here.** A pot at mid
travel is a 5 kΩ static source, which `REF_A` already is. The one place it
does matter is the `MUX_A3` aggressor with the 16:1 enabled: `REF_C` sits at
address `110`, so toggling `A3` moves the 16:1 between channel 6 (`RV4`'s
wiper — floating until the pot is in) and channel 14 (`AGND`). Until `RV4` is
fitted that variant switches a floating node into `ADC_9`, which is a
different experiment. §4 handles it.

## 3. The instrument is the settle probe with a different word pair

`shell/settle_probe.cpp`'s `measure_point()` does, per repeat: latch a park
word, spin 20 µs, latch a test word taking `t0` from the 595's own `RCLK`
edge, spin until `d`, take one conversion. 64 repeats per grid point, 65
points at 200 ns. The chain word is `address | enable_mask << 4 | leds << 6`
(`chain_word()`, `kCouponChain`: `addr_shift 0`, `enable_shift 4`,
`led_shift 6`, `led_bits 8`), and `write_chain_timed()` returns the DWT count
at the latch edge.

A crosstalk measurement is that sequence with a word pair that **keeps the
victim's address and enable fixed and differs only in aggressor bits**. Two
consequences fall out for free:

- the victim never changes channel, so the sample-and-hold's charge
  redistribution (`settle-measured.md` §7, the 845-count first arrival) is
  not in the measurement at all; and
- `t0` is still the latch edge, so `d` means what it meant: time from the
  aggressor edge to the commanded conversion, with the same measured offset
  in front of the aperture.

Two primitives are missing and both are small:

- **`MuxScan::shift_chain_timed(word)`** — the shift loop of
  `write_chain_timed()` with the two `latch_` writes removed, returning the
  DWT count after the last `SRCLK` edge. The 595's outputs do not change; the
  165 is clocked too (shared `CP`), which is what the shipping scan does
  anyway.
- **A block that touches the chain zero times** — park once before the block,
  take the whole grid, and hold every result in RAM until the block is done.
  65 points × three integers is nothing. It is the only way to get a reading
  with no 595 traffic and no USB traffic inside it.

### Why a new switch and not a mode

`SHELL_XTALK_PROBE`, its own `xtalk_probe.cpp` and `xtalk_plan.cpp`, its own
reader. Not `SHELL_SETTLE_PROBE=2`: `settle_probe.cpp` is 1028 lines and its
knee search, its tail reference and its gates G1 and G3 are the settle
question's statistics, every one of which is the wrong question here. A
second mode in that file would spend more lines bypassing them than a second
file spends.

The cost is one refactor at an instrument that took ten fix rounds: the ADC
primitives — `adc_init()`, `adc_select_time()`, `sample_now()`,
`adc_warm_up()`, the clock-span measurement, the `g_adc_*_ok` flags — move
from `settle_probe.cpp` into a shared `probe_adc.cpp`/`.h`, and
`settle_probe.cpp` calls them from there. **That refactor is behaviour-
preserving by definition and must be proven so on the board before anything
else is built**: one settle block on the refactored image, its `SHELL_SETTLE_
CLK`, `_CAL` and `_GATES` lines compared against the 2026-09-18 capture. The
clock within 1 %, the offsets identical, the gates the same. Nothing in this
spec proceeds until that comparison has been printed.

The Makefile switch follows `SHELL_SETTLE_PROBE`'s pattern exactly: `0|1`,
implies `SHELL_COUPON_PROBE=1` with the same error text, dispatched from
`main.cpp` before `StartAudio` so no audio runs (round two changes that, not
this).

## 4. The plan table

`xtalk_plan.cpp`, `kXtalkPlan`, data only, host-tested. One entry is a
victim, an aggressor, and how the aggressor is applied:

```
struct XtalkCase
{
    int      group;        // victim's mux, 0 = 4067 on ADC_9, 1 = 4051 on ADC_10
    int      channel;      // victim's channel, held for the whole case
    uint32_t r_src_ohm;    // from the netlist, the attribution axis
    uint32_t word_a;       // chain word before the event
    uint32_t word_b;       // chain word the event latches (== word_a for a control)
    Kind     kind;         // Latch, ShiftOnly, Static, Silent
    bool     needs_rv4;    // true for the one variant §2 flags
};
```

Every `word_a`/`word_b` pair carries the victim's own address and enable
bits; a host test asserts that the two words agree in the victim's address
field and in the victim's enable bit, and that a `Latch` pair differs
somewhere else. `Silent` and `Static` cases have no `d`.

Round one, in the order they run:

| # | Kind | Victim | Event | What it separates |
|---|---|---|---|---|
| 1 | Silent | each of the five | none — park, then no chain access and no print for the whole grid | the floor: does the 8–12 count wander need the scan's own activity? |
| 2 | Latch, control | each of the five | `word_b == word_a`: a latch pulse with no bit change | the `RCLK` pulse and the shift preceding it, from any bit change |
| 3 | Latch | `REF_A`, `REF_B`, `R_SP10` | `MUX8_EN_N` low→high and high→low, two cases | the moat-crossing bus and the 4051's switches, into the 16:1's read |
| 4 | Latch | `REF_C`, `R_LO3` | `MUX16_EN_N` low→high and high→low | the mirror image |
| 5 | Latch | `REF_C`, `R_LO3` | `MUX_A3` 0→1 and 1→0 with `MUX16_EN_N` high | a bare address edge across the moat |
| 6 | Latch | `REF_C`, `R_LO3` | the same with `MUX16_EN_N` low; `needs_rv4` | the 16:1 switching channel into `ADC_9` while `ADC_10` is read |
| 7 | Latch | each of the five | LED word `0x00`→`0xFF` and `0xFF`→`0x00` | eight simultaneous current edges through the star point |
| 8 | Static | each of the five | LED word held at `0x00`, then at `0xFF`, 64 conversions each, no grid | the DC shift of the ground join under 8 × ~2 mA |
| 9 | ShiftOnly | each of the five | 16 bits of `word_a` shifted, no latch | digital activity with no control-line change |
| 10 | Silent, printing | each of the five | as case 1 but `PrintLine` after every grid point, as the settle probe does | USB-CDC/DMA activity, the §5 candidate |

Each Latch case runs both edge directions because charge injection has a
sign and di/dt has a sign; a disturbance that flips with the edge is
coupling, one that does not is the pulse itself.

`needs_rv4` cases are skipped and reported as skipped (`skipped=1` on their
line) until a build flag says the pot is fitted. A skipped case is a line in
the output, never a silently shorter table.

## 5. Three levels per victim, and the statistic

For every victim the block produces three curves over the same grid:
Silent (case 1), Control (case 2) and each Aggressor (cases 3–7, 9, 10). The
reported quantity for an aggressor at grid point `d` is

```
delta(d) = mean_aggressor(d) − mean_control(d)
```

in 16-bit counts, where each mean is over 64 repeats. The control, not the
silent curve, is the reference: the control carries the same shift and the
same latch pulse as the aggressor case, so the difference isolates the bit
change. The silent curve is reported beside both, and its own two numbers are
findings in their own right:

- **`settled_mean_spread`** of the silent curve, the same statistic as
  `settle-measured.md` §5's table (peak to peak of the 65 per-point means).
  If it comes back at 0–1 on the divider channels, the 8–12 count wander was
  the instrument's own traffic and the attribution goes on to which case
  brings it back. If it comes back at 8–12 with the board otherwise idle, the
  wander is the node or the reference, not the scan, and none of the cases
  below can be blamed for it.
- **`widest_sample_band`**, the raw min–max per grid point, again as §5
  prints it. The 100–200 count excursions either survive silence or they do
  not.

For Static cases the statistic is the difference of the two 64-repeat means,
with both raw bands.

**The criterion, from which the panel verdict falls:** for every aggressor
case and every victim, `|delta(d)| ≤ 8` counts at every grid point at or past
`kScanSettleNs`, the delay the shipping scan waits after an address before it
reads. That constant is **not** defined by this spec: it is whatever
`shell/` reads its pots with when this probe is built, and the plan table
prints it so the verdict carries its own premise. Everything before it on the
grid is characterisation — the shape and sign of the disturbance, which is
what names the mechanism — and not a verdict.

The verdict is computed by the reader, not the firmware, and it is a verdict
about *this board and this delay*, printed as such. The firmware prints
curves and gates.

## 6. What the instrument cannot see

Inherited from `settle-measured.md` §2, and one addition:

- **The offset floor.** `d = 0` means a conversion whose aperture opens
  991 ns (1.5-cycle rung) or 1154 ns (2.5-cycle rung) after the latch edge —
  measured per boot and printed, as before. A transient that has decayed by
  then is invisible. That is acceptable for the panel question, because the
  scan reads later than that on every channel, and the envelope over `d` is
  what an asynchronous free-running DMA read (the shipping ADC's mode) can
  worst-case land on. It is **not** acceptable for characterising charge
  injection itself, which decays in nanoseconds. Seeing the transient needs
  the reverse order — start a long conversion, land the edge *inside* its
  sampling window — which is new sequencing, not a new plan table, and is
  deferred with the codec to round two.
- **One clock period of aperture jitter**, 138 ns worst measured. Adjacent
  grid points are not independent. Unchanged.
- **The victim's own rung.** Each victim is read at the rung the settle probe
  chose for its impedance (`sample_time_index_for()`), which §7 of the
  measured document showed reads ~200 counts low in steady state at 5150 Ω.
  A `delta` is a difference of two readings at the same rung, so the bias
  cancels, and the Static cases are likewise differences. No absolute level
  from this probe may be quoted without that qualification.

## 7. Gates

Only gates whose failure indicts the instrument. A failed gate prints every
number and refuses the verdict; the reader exits 1.

| Gate | Bound | Inherited from |
|---|---|---|
| **G2** floor | the silent curve's 64-repeat spread on a 0 Ω victim, 0…64 counts | settle probe G2, unchanged |
| **G4** jitter | `lat_max − lat_min ≤ 200 ns`, latency mean not negative | settle probe G4, unchanged, same calibration pass |
| **G5** address | in its Silent case each victim reads inside its `coupon_expect` band (`Mid` for the dividers, `Low` for the ties, judged against the span the two ties give) | new — proves the mux is where the table says and is enabled; a wrong word here makes every `delta` a measurement of nothing |
| **G6** control | for each victim, `|mean_control(d) − mean_silent(d)| ≤ 8` at every `d` | new — the control must be a control: if a latch with no bit change already moves the reading past the criterion, the aggressor cases cannot be read as differences and the finding is G6 itself |

G6 is judged over **comparisons and not over victims**, and a victim counts as
compared only once a majority of the grid's control-vs-silent point pairs have
survived the exclusion — 33 of 65. G6 is an envelope over `d`, not a
statistic, so a handful of surviving pairs can all sit in a quiet stretch of
the transient and report 0 counts; below half the grid, "at every `d`" stops
being an honest description of what was measured. The firmware and
`read_xtalk.py` hold the same minimum, the reader deriving it from the block's
own `points` field rather than carrying its own 33.

G6 failing is the one that may well be a real result rather than a broken
instrument — a latch pulse alone moving a 5150 Ω channel by 8 counts would be
a finding about the board. The gate still refuses the *per-aggressor* verdict
in that case, because the differences are no longer interpretable; what it
prints is the control curve, which is then the result of the run.

G1 (reference pairs settle at once) and G3 (settled-region agreement) do not
carry over: there is no knee, and the settled-region width is a reported
quantity here, not a gate.

## 8. Output

Integers only, `PrintLine`, one block repeating forever with the same cadence
and the same `_END` marker discipline as the settle probe:

**Transcribed from `xtalk_probe.cpp`'s `PrintLine` calls on 2026-09-19, not
from this section's own history.** What stood here was the design's single
combined `SHELL_XTALK` line and nothing else; the firmware had since split it
into a `_CASE` line and a point line, and had added `_RATE`, `_STAT`,
`_SPAN` and `_G5`. Why the split: libDaisy's log buffer is 128 bytes
(`lib/libDaisy/src/hid/logger.h:29`) and the combined line
`SHELL_XTALK case= kind= victim_group= victim_ch= r_src= word_a= word_b=
d_ns= n= mean= min= max= skipped=` runs **148 characters, 150 with CRLF, at
its DATA-widest values** — `case=57`, `kind=3`, `victim_group=1`,
`victim_ch=15`, `r_src=5150`, both words at 16383 (4 address + 2 enable + 8
LED bits), `d_ns=12800`, `n=64`, the three 16-bit readings at 65535,
`skipped=1`. That is 20 bytes past the buffer, so it would truncate and be
stamped `$$` on every point line. (Type-widest, every `%d` at a full
negative `int32`, is 248/250 — the two bounds are a hundred characters
apart, which is why "widest" alone is not a figure. Neither is a board
measurement: no image ever printed this line. The **measured** figures are
the split lines', in `print_case_line()`'s comment.) `read_xtalk.py`
parses this format by field name, and three separate fix rounds in the
codec-tone plan were caused by a spec section that had drifted from the
firmware, so it is brought level here rather than appended to.

```
SHELL_XTALK_WARMUP  ok=%d init_ok=%d cal_ok=%d cfg_ok=%d      (once at boot, outside the block)
SHELL_XTALK_CFG     adc_khz=%d repeats=%d grid_ns=%d points=%d park_ns=%d scan_settle_ns=%d rv4=%d git=%s
SHELL_XTALK_RATE    block_size=%d sr_hz=%d cases=%d victims=%d
SHELL_XTALK_CLK     span_short_cyc=%d span_long_cyc=%d smp_short_tenths=%d smp_long_tenths=%d
SHELL_XTALK_CAL     lat_mean_ns=%d lat_min_ns=%d lat_max_ns=%d b0=%d timeouts=%d
SHELL_XTALK_CASE    case=%d row=%d kind=%d victim_group=%d victim_ch=%d r_src=%d word_a=%d word_b=%d skipped=%d
SHELL_XTALK         case=%d d_ns=%d n=%d mean=%d min=%d max=%d
SHELL_XTALK_STAT    case=%d settled_mean_spread=%d widest_sample_band=%d at_d_ns=%d
SHELL_XTALK_G6      case=%d victim_group=%d victim_ch=%d pairs=%d worst=%d compared=%d
SHELL_XTALK_NOVICTIM case=%d row=%d victim_group=%d victim_ch=%d
SHELL_XTALK_STATIC  case=%d victim_group=%d victim_ch=%d r_src=%d word=%d n=%d mean=%d min=%d max=%d
SHELL_XTALK_SPAN    zero=%d rail=%d hi_spread=%d lo_spread=%d lost=%d n_min=%d valid=%d
SHELL_XTALK_G5      victim_group=%d victim_ch=%d expect=%d mean=%d ok=%d
SHELL_XTALK_GATES   g2=%d g4=%d g5=%d g6=%d init_ok=%d cal_ok=%d cfg_ok=%d gates_ok=%d
SHELL_XTALK_END
```

Per case the block carries either `_CASE`, its `points` point lines and
`_STAT` for a grid case — with `_G6` behind the `_STAT` when that case is its
victim's control — or `_CASE` and `_STATIC` for a static case, or `_CASE`
alone for a skipped one, or `_CASE` and `_NOVICTIM` for a case naming no
victim. `_STAT` is emitted for **every** grid case and not only the Silent
ones; its two numbers mean different things depending on whose curve they
describe, and `read_xtalk.py` files them under separate scopes accordingly.

Silent cases are printed after their grid completes, in the same line format;
case 10 prints as it goes. The reader cannot tell the two apart from the
lines, which is deliberate — the firmware's `kind` field is the record.

Three of these fields close round one's own KNOWN GAPs and were added
2026-09-19. A block missing `_G6` or `_NOVICTIM` is refused — the firmware
emits both from paths every block runs, so their absence is transit loss. A
`_SPAN` line missing **both** `lost=` and `n_min=` is a different thing: a
**pre-fix capture**, which `read_xtalk.py` reads and labels
`span_format=pre-fix` in the metadata, with a `g5_risk` row saying what that
block's G5 gives up. Refusing it would not be more rigorous — every published
number in `docs/hardware/crosstalk-measured.md` came from a pre-fix capture,
and a reader that cannot re-read it makes that document unreproducible. A
`_SPAN` line carrying exactly one of the two is a shape no image prints, i.e.
a damaged line, and is refused.

- `_SPAN`'s `lost=` and `n_min=` — the repeats the four 0 Ω tie reads lost,
  and the smallest surviving count on any one tie. The four tie reads used to
  run through `probe_adc::mean_of_repeats()`, which folds a timed-out
  repeat's `0` into the mean with nothing in the return value to say so; on
  an AGND tie, whose true reading is already ~0, that was completely
  invisible and could pass a broken tie as the yardstick G5 is judged
  against. The span is now refused outright when any repeat is lost.
- `_G6`'s `pairs=` and `compared=` — how many control-vs-silent point pairs
  survived per victim, and whether that cleared a majority of the grid. The
  gate used to count victims rather than comparisons, so a victim whose every
  pair was excluded still counted and handed G6 a worst delta of `0`.
- `_NOVICTIM` — a case whose `(group, channel)` is in no victim table row.
  It used to print nothing at all.

`shell/read_xtalk.py` follows `read_settle.py`: accumulate to `_END`, discard
an incomplete block, tolerate the `$$`-marked spliced lines USB-CDC produces
here, write `xtalk.csv` (one row per printed point) and `xtalk.csv.meta.csv`
(`scope,case,key,value`), compute `delta(d)` per aggressor case against its
victim's control, compute the verdict against `scan_settle_ns`, exit 1 on any
failed gate. Guard: `shell/test_read_xtalk.py`, registered in CTest as
`read_xtalk_guard`.

## 9. How this can go red

The firmware's timing is not testable on the host; everything around it is.

- **`tests/test_xtalk_plan.cpp`**, new, wired beside `test_settle_plan.cpp`:
  every case's victim channel inside its mux's range; `word_a` and `word_b`
  agree in the victim's address field and enable bit; a `Latch` case's words
  differ, a control's do not; every `r_src_ohm` matches what `netlist.py`
  wires for that channel (the same table `settle_plan.cpp` already carries);
  every victim appears in a Silent and a Control case. **Prove the red once**
  by asserting the `MUX_A3` cases' `word_a`/`word_b` differ only in bit 3 and
  watching it fail before the table exists.
- **The refactor** (§3) is proven on the board, not on the host, and the
  proof is a printed comparison. A settle block that no longer matches the
  2026-09-18 capture blocks everything after it.
- **The reader's guard** gets fixtures for each gate, with G6 the one to
  prove red deliberately: a synthetic control curve 9 counts off its silent
  curve must fail, because that is the case where every aggressor `delta`
  still looks like a number.
- **The verdict arithmetic** is pure data and lives in the reader, with a
  fixture where `delta` crosses 8 exactly one grid point before
  `scan_settle_ns` (verdict: pass) and one where it crosses one point after
  (verdict: fail).
- **On the board**, the two 0 Ω victims are the instrument's zero: a `delta`
  on a channel tied to `AGND` through 0 Ω that grows with nothing cannot be
  the node. If the 0 Ω victims show the same `delta` as the 5150 Ω ones, the
  coupling is not into the node, and the R-ladder has said so before anyone
  reads a curve.

## 10. What is read, what is derived, what is unmeasured

| Claim | Class |
|---|---|
| Which 595 output carries which control net; the 8:1 ignores `MUX_A3` | read — `netlist.py:163`, `:267` |
| The chain word layout, `kCouponChain` | read — `mux_plan.h`, guarded by `test_mux_plan.cpp` |
| Five control nets cross the moat 70–74 mm from `JP_GND`; the three longest 0 R ties sit on the 8:1's west approach | read — `hardware/coupon/README.md`, measured there by `check_layout.py` |
| The offset floor, the clock, the jitter, G2's and G4's bounds | measured — `settle-measured.md` §1–§3, reused, not re-derived |
| The working rung reads ~200 counts low at 5150 Ω | measured — `settle-measured.md` §7; cancels in every difference here |
| A static neighbour is a poor aggressor | **reasoned, not measured** — README's argument; this probe does not test it and does not need to |
| That `shift_chain_timed()` leaves the 595 outputs untouched | read — 74HC595 datasheet: outputs follow the storage register, which moves on `RCLK` only. **Unmeasured on this board**; case 2 against case 9 is the check, and a case-9 `delta` on a 0 Ω victim that matches a control would say the shift alone does nothing to the outputs |
| The LED current per output | derived — (3.3 V − V_f) / 1 kΩ ≈ 1.3 mA at a green LED's ~2 V; "~2 mA" above is an upper bound |
| `kScanSettleNs` | **read from `shell/` at build time**, printed, never assumed in this document |
| What any `delta` will be | unmeasured — that is the run |

## 11. Out of scope, and round two

- **The codec as aggressor** — its own spec, above. It needs `StartAudio`,
  which this image deliberately does not call.
- **The edge inside the sampling window** (§6) — new sequencing; deferred to
  the same round as the codec so both new sequences are designed against the
  same refactored primitives.
- **A second board or a second submodule** — `settle-measured.md` §8 lists
  both; this probe runs on the one populated coupon and says so in its
  output.
- **Crosstalk into the audio path** — the 2026-08-23 artifact series is the
  other direction (scan into audio) and is not touched here.
- **Any rework**, including the ferrite-bead substitute for the lost supply
  A/B the README names. Populate copies, never rework one board.
