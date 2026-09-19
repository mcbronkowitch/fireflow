# The coupon codec-tone probe — design (round two)

**Date:** 2026-09-18
**Status:** BUILT, flashed and measured over seven tasks, 2026-09-18/19.
The campaign's own write-up is not yet in `docs/hardware/`; until it is,
§7's four-boot floor table and the reproduction command in §8 are what
this repository holds of the result. **Depends on round one**
([`2026-09-18-coupon-crosstalk-probe-design.md`](2026-09-18-coupon-crosstalk-probe-design.md))
having been built, its refactored ADC primitives proven on the board, and its
silent-block result known
**Closes:** the last aggressor the coupon can raise from firmware — the
submodule's own audio output, running through the analog zone past the muxes
— and, as a by-product, the "edge inside the sampling window" sequence round
one deferred

## 1. What this is

Round one pits digital edges against a settled mux channel. This probe pits
the **audio output** against it: a tone of chosen frequency and level on
`AUDIO_OUT_L`/`AUDIO_OUT_R`, which leave the Patch Submodule on B2/B1
(`netlist.py:106`), run through the analog domain to the unpopulated
`J_AUDIO` and to `TP_AUDIO_L`, while a divider channel is read.

That is the product question in its purest available form. The 60 HP panel
puts 65 pot lines, two muxes and the audio path on one board; whatever the
audio couples into a pot read there is what this coupon's audio trace couples
into `REF_A` here, differing in geometry but not in kind — which is the same
argument `settle-budget.md` §6 makes for measuring on the coupon rather than
on a breadboard.

It is **not** the 2026-08-23 artifact question. That series
([`docs/bench/2026-08-23-978cbaf-artifact-triage.md`](../../bench/2026-08-23-978cbaf-artifact-triage.md))
is the scan coupling *into* audio; this is audio coupling *into* the scan.
The two share a board and nothing else, and this document does not quote
that one's numbers.

## 2. Why this is round two and not a case in round one

Three things the round-one instrument does not have:

- **Audio running.** Round one's image, like the settle probe's, never calls
  `StartAudio`. This one must, and that changes the environment of every
  read: the codec's I2S clocks, the SAI DMA and the block interrupt all run
  for the whole block. That is itself an aggressor — `settle-measured.md` §5
  names DMA activity as an unexamined candidate — and it has to be separated
  from the tone. §4 does that with a three-level ladder.
- **A periodic aggressor, not an edge.** A tone has no `t0` to latch from. The
  grid over `d` becomes a grid over **phase**: the reading as a function of
  where in the tone's period the aperture opens. Same 64 repeats, same
  per-point mean, a different abscissa.
- **A time reference that lives in the audio callback.** The DWT count at the
  start of each block, captured in the callback, plus the sample index, is
  the phase clock. Round one has no such thing because it needs none.

Sharing a file with round one would have meant three `#if` branches through
the repeat loop. It gets its own switch, `SHELL_TONE_PROBE`, its own
`tone_probe.cpp` and `tone_plan.cpp`, and it calls the same `probe_adc`
primitives round one's refactor produced.

## 3. The tone

Generated in the audio callback from a phase accumulator, both channels, and
nothing else in the callback — no engine, no `inst.process()`. The callback
also records, per block, the DWT count at its entry into a volatile, and
increments a block counter. That pair is what the foreground reads to compute
phase.

Three parameters, each a small ladder, each a compile-time table in
`tone_plan.cpp` so a run's configuration is data and prints:

| Parameter | Ladder | Why these |
|---|---|---|
| frequency | 100 Hz, 1 kHz, 5 kHz | capacitive coupling grows as `C · dV/dt`, i.e. linearly with `f` at fixed level; resistive or ground coupling does not. Three points give a slope. 5 kHz keeps the phase grid coarse enough for the callback's timing (§5); nothing above it is needed to read the slope |
| level | −20, −6, 0 dBFS | a linearity check; coupling that is not linear in level is not coupling |
| shape | sine | a square wave would be a stronger aggressor, but its harmonics fold the frequency ladder into one point. Sine first; square as an optional fourth row once the sine slope is known |

Silence is a fourth "level", and it is the important one (§4).

## 4. Three levels per victim

For each victim (the same five as round one: `REF_A`, `REF_C`, `REF_B`, and
the two 0 Ω ties as the zero) the block produces:

| Level | Audio | What it measures |
|---|---|---|
| **Stopped** | `StopAudio()` before the block; codec idle | round one's silent block, repeated in this image: the floor, and the check that this image's floor is round one's |
| **Running, silent** | audio started, callback writes zeros | I2S, SAI DMA and the block interrupt as aggressor, with no signal on the trace |
| **Running, tone** | one row of the frequency × level table per case | the trace as aggressor |

The reported quantity for a tone case at phase point `φ` is

```
delta(φ) = mean_tone(φ) − mean_running_silent
```

where the running-silent mean is that victim's whole-block mean (there is no
phase in silence). Because a sinusoidal disturbance has zero mean over a
period, the number that carries the result is the **peak-to-peak of
`delta(φ)`** across the phase grid, `delta_pp`, per victim, per tone row. The
running-silent level's own `settled_mean_spread` and `widest_sample_band`
(round one's statistics) are reported beside it, and their difference from
the stopped level is the I2S/DMA finding.

**The criterion** is round one's, in its natural form for a periodic
disturbance: `delta_pp ≤ 8` counts on every 5150 Ω victim at every table row.
No timing enters it — an asynchronous free-running ADC read on the shipping
firmware lands at a random phase, so the peak-to-peak is the worst case it
can meet.

**One exception, deliberate: the static row is reported and not gated.** A
constant has no phase, so that row is one point, so its `delta_pp` is 0 by
construction — a gate on it could never go red, and this project fixes gates
that cannot fail. `read_tone.py`'s `static_deltas()` carries the same
statement at its definition, and the row's `delta` against the
running-silent level is printed like any other.

## 5. The phase grid

The callback runs at 48 kHz with the block size the board reports
(`hw.AudioBlockSize()`, read at runtime and printed, never assumed — the
CPU probe's own comment in `main.cpp` records why). The tone's phase at DWT
count `t` is

```
φ(t) = phase_at_block_start + f · (t − dwt_at_block_start) / f_core
```

with `phase_at_block_start` advanced by the callback. The foreground picks a
target phase, spins until `φ(cycles_now())` reaches it, and converts. 16
phase points per period, 64 repeats each; every repeat waits for the next
crossing of its target phase, so repeats are one or more periods apart and
never share a block's interrupt jitter.

What bounds the phase precision: the callback's entry latency relative to the
DMA edge is interrupt latency, some hundreds of nanoseconds, plus the
aperture jitter round one inherited (138 ns worst measured). At 5 kHz a period
is 200 µs and one phase point is 12.5 µs, so a microsecond of slop is under a
tenth of a point; at 100 Hz it is nothing. The grid could be finer at low
frequency; it does not need to be, because the quantity is a peak-to-peak and
a sine's extremes are broad.

**The conversion is masked, the wait is not.** `sample_now()` disables
interrupts around the conversion (as it does today, `settle_probe.cpp:230`),
so a block interrupt landing during the conversion is delayed by one
conversion, ~2 µs — far inside a 2 ms block. The spin-wait runs with
interrupts enabled so the callback keeps the codec fed. A callback that
starves prints as a missed-block count (§7), not as a silent glitch.

## 6. The edge inside the sampling window — the deferred sequence

Round one's §6 deferred one thing: seeing a transient that decays before the
991 ns offset floor. The reverse order does it — start a conversion at the
387.5-cycle rung (63 µs of acquisition, measured in `settle-measured.md` §7
to land on the divider's true value), and latch the aggressor edge at a
chosen time **inside** that window. The sample-and-hold tracks the node
through the whole window and the aperture closes at its end; a transient
whose remainder at the aperture is still above the criterion shows as a
function of how long before the end it was fired.

This is a round-one aggressor (a chain word pair) with a round-two
sequence, and it belongs here because it is the second new sequence built on
the same primitives:

```
adc_select_time(victim, 387.5-cycle rung)
start conversion, t0 = cycles_now()
spin until t0 + (window − d_before_end)
write_chain_timed(word_b)
poll EOC, read
```

Grid: `d_before_end` from 0 to 12.8 µs in 200 ns, the same 65 points, so the
two instruments' `d` **axes** line up end to end — round one's `d` after the
edge continues where this one's `d_before_end` stops. **That is a statement
about the axes and not about the curves, and the 2026-09-19 capture measured
the difference: see §8's note on the junction.** `t0` here is the conversion
start, not a latch edge; the window's length is derived from the measured
clock, as every window in `settle-measured.md` is, and the calibration pass
that measures start-to-aperture latency runs unchanged.

It runs for the round-one aggressors that showed a `delta` at all, and for
`MUX8_EN_N` against `REF_A` regardless, because that is the case the moat
crossing is most directly in.

## 7. Gates

Round one's G2, G4 and G5 unchanged, plus:

| Gate | Bound | What it refuses |
|---|---|---|
| **G7** callback health | zero missed blocks across the case (the callback counts entries; the foreground compares against elapsed DWT time and the block size) | a run in which the tone was not the tone: a starved callback outputs the DMA buffer's stale contents, and a `delta_pp` against that is unreadable |
| **G8(a)** round-one comparison | **none — REPORT ONLY, never a verdict.** The boot-virgin floor per victim is printed beside round one's silent block and labelled NOT LIKE-FOR-LIKE | nothing. It gates nothing by construction |
| **G8(b)** floor agreement | the **boot-virgin** stopped level's `settled_mean_spread` per victim — the `SHELL_TONE_LEVEL`/`STAT` lines with `audio_virgin=1`, and only those — within 4 counts of the **observed range across this campaign's own four boots**, i.e. inside `[min(boots) − 4, max(boots) + 4]` | an image whose floor is not the floor this campaign measured — the refactor or the linker having moved something |

**This section originally specified one gate, against round one, and that is
not what ships.** The change was ruled during Task 6 and is recorded here
rather than left to the reader of the code. It is **flagged for Bastian to
overturn**; nothing below is closed by it.

*Why (a) carries no bound.* The two `settled_mean_spread` values have the
same count and different content, which is a statement about what two pieces
of code compute and is checkable from their source. Round one's
`measure_silent_point(d_ns)` spins `park_cycles + d_cycles` before **every**
conversion and its 65 grid points carry 65 **different** `d` values, 0 to
12800 ns, so its spread is a peak-to-peak across 65 different pre-conversion
delays. This image's `measure_level()` spins nothing and varies nothing
across its 65 points, so its spread is repeat-to-repeat noise on the mean.
`tone_plan.h`'s own comment states the requirement — the two must be the same
shape "or the 4-count bound compares two differently-shaped spreads and means
nothing" — and the implementation matched the count and missed the content.
By that condition the comparison means nothing, so it is printed and never
gated. **No mechanism is attached**: what the `d`-dependence in round one's
grid is, and why `REF_A` agrees while `REF_C` and `REF_B` come out lower,
stay unexplained.

*Why (b)'s baseline is a range and not a central value.* Four points do not
establish a central value, and the midpoint of 14..17 is a number no boot
ever produced. A range bound uses only what was measured.

G8(b) is the one to prove red deliberately: with `REF_A`'s four boots at
14..17 the bound is `[10, 21]`, so a boot-virgin spread of **22** or of **9**
must fail. Both boundaries and both first failures are pinned in
`shell/test_read_tone.py`.

**The two baselines in this section are not the same capture, and that
matters.** The round-one columns transcribed below come from
`docs/hardware/crosstalk-measured.md`, i.e. the **2026-09-18** run. The file
`read_tone.py` actually consumes is
`docs/hardware/2026-09-19-xtalk.csv.meta.csv`, the **2026-09-19**
re-measurement, whose row-1 silent spreads are `REF_A` 15, `REF_C` **14**,
`REF_B` 5, `R_SP10` 1, `R_LO3` 0. `REF_C` at 14 contradicts the 10/11/12
below. The disagreement is recorded and **not explained**; §13 of
`crosstalk-measured.md` quarantines that run from round one's published
numbers.

**Measured across four boots, no mechanism attached** (Task 4 fix round 3
for the first three, Task 5's capture for the fourth): the boot-virgin
floor's `settled_mean_spread`, against round one's own two published columns
(`xtalk.csv.meta.csv`'s whole silent grid, and that grid with its `d=0` point
dropped). Every figure is `SHELL_TONE_STAT` at `case=-1` through `case=-5`:

| victim | boot-virgin spread | round one, whole grid | round one, `d=0` dropped |
|---|---|---|---|
| `REF_A` g0 ch8 5150 Ω | 17, 16, 17, **14** | 15/16/17 | 10/11/12 |
| `REF_C` g1 ch6 5150 Ω | 3, 3, 4, **4** | 10/11/12 | 6/6/6 |
| `REF_B` g0 ch9 650 Ω | 2, 3, 4, **5** | 6/6/7 | 6/6/7 |
| `R_SP10` 150 Ω | 1, 1, 0, **0** | 0/0/1 | 0/0/1 |
| `R_LO3` 150 Ω | 0, 0, 0, **0** | 0/0/0 | 0/0/0 |

The four boots, in order and with the capture each came from (all in the
plan's gitignored workspace, `.superpowers/sdd/2026-09-18-coupon-codec-tone-probe/`):
`task-3-board-capture-1c15987.txt`, `task-4-board-capture-438fd51.txt`,
`task-4-board-capture-c5631f4.txt`, `task-5-board-capture.txt`. The same four
columns are the baseline `read_tone.py`'s `BOOT_VIRGIN_FLOOR` carries.

The floor is not identical across boots: `REF_A` reads 17, 16, 17 and 14 — a
spread of **three** counts — and `REF_B` reads 2, 3, 4 and 5, also three. An
earlier note in this project's working record claimed the boot-virgin floor
was reproducible across boots "to about one count"; the third and fourth
boots supersede that claim, and this spec records the wider figure instead.
This bears directly on G8, whose bound is 4 counts: a floor that itself moves
3 counts between boots spends most of that budget before any image change has
been measured at all. It is also why G8(b) compares against the observed
**range** rather than against any one boot — against a per-victim range the
worst deviation across the four boots is 0 by construction, and the bound is
then spent only on what a new image does.

Observed and recorded without mechanism: `REF_B`'s four readings happen to be
2, 3, 4, 5 in capture order. Four points, an ordering, and nothing more is to
be said about it.

`REF_A` lands on round one's whole-grid figure. `REF_C` and `REF_B` come out
**lower** than either of round one's columns, not higher. Carrying the same
minimum/maximum-of-absolute-difference method through all four boots
against both of round one's published columns, over the same table, gives
the full picture:

| victim | vs. whole-grid column | vs. `d=0`-dropped column |
|---|---|---|
| `REF_A` | 0-3 counts off — inside | 2-7 counts off — **outside** at its worst pairing |
| `REF_C` | 6-9 counts off — **outside** | 2-3 counts off — inside |
| `REF_B` | 1-5 counts off — **outside** at its worst pairing | 1-5 counts off — **outside** at its worst pairing |
| `R_SP10` | 0-1 counts off — inside | 0-1 counts off — inside |
| `R_LO3` | 0 counts off — inside | 0 counts off — inside |

The fourth boot widens three of these ranges and changes none of the
verdicts.

`REF_A` and `REF_C` fail under **opposite** columns: the whole-grid column
clears `REF_A` and fails `REF_C`; the `d=0`-dropped column clears `REF_C`
and fails `REF_A`. Each column rescues one of the two and breaks the other.
`REF_B` exceeds a 4-count bound at its worst pairing under **both**
columns — a fact the single-column framing above does not surface at all.
**This is stated as a measured fact**: no choice of column satisfies a
4-count round-one comparison against `REF_A`, `REF_C` and `REF_B` together.
The question is therefore not "which column" — no column answers it.

**What was decided, and what stays open.** The arithmetic above is one of
the two reasons the round-one comparison became report-only; the other, and
the decisive one, is the source reading in the G8(a) row above — the two
spreads are not the same statistic, so no bound over them would have meant
anything however the columns fell. **Still open, and not answered here:**
why `REF_C` and `REF_B` come out lower than round one at all, what the
`d`-dependence in round one's grid is, and whether Bastian wants a different
baseline for G8(b) than this campaign's own four boots. None of these is
closed by the split.

## 8. Output

```
SHELL_TONE_CFG   adc_khz=%d repeats=%d phase_points=%d block_size=%d sr=%d rv4=%d git=%s
SHELL_TONE_CLK   span_short_cyc=%d span_long_cyc=%d smp_short_tenths=%d smp_long_tenths=%d
SHELL_TONE_CAL   lat_mean_ns=%d lat_min_ns=%d lat_max_ns=%d b0=%d timeouts=%d
SHELL_TONE_LEVEL case=%d level=%d victim_group=%d victim_ch=%d r_src=%d n=%d mean=%d min=%d max=%d audio_virgin=%d   (stopped and running-silent)
SHELL_TONE_STAT  case=%d settled_mean_spread=%d widest_sample_band=%d
SHELL_TONE_CASE  case=%d level=%d f_hz=%d dbfs=%d victim_group=%d victim_ch=%d r_src=%d below_corner=%d   (level=2 tone, level=3 silent-cadence)
SHELL_TONE       case=%d phase_idx=%d n=%d mean=%d min=%d max=%d
SHELL_TONE_SPAN  zero=%d rail=%d hi_spread=%d lo_spread=%d valid=%d
SHELL_TONE_G5    victim_group=%d victim_ch=%d expect=%d mean=%d ok=%d
SHELL_TONE_WINDOW  window_ns=%d nominal_ns=%d grid_end_ns=%d fits=%d
SHELL_TONE_WINCASE case=%d xtalk_case=%d victim_group=%d victim_ch=%d r_src=%d word_a=%d word_b=%d codec=%d
SHELL_TONE_WIN     case=%d d_before_end_ns=%d n=%d mean=%d min=%d max=%d
SHELL_TONE_GATES  g2=%d g4=%d g5=%d g7=%d g8=%d gates_ok=%d
SHELL_TONE_HEALTH missed_blocks=%d phase_timeouts=%d win_timeouts=%d block_ms=%d
SHELL_TONE_END
```

**`level` on `SHELL_TONE_CASE` has a fourth value (2026-09-19).**
`ToneLevel::SilentCadence = 3` is the discriminating arm
[`docs/hardware/codec-tone-measured.md`](../../hardware/codec-tone-measured.md)
section 6 asks for: the phase grid walked at a row's real phase step with the
callback's amplitude at zero, so the cadence is a tone case's and the output
is silent. It is announced with the same `SHELL_TONE_CASE` tag and walks the
same `SHELL_TONE` grid — deliberately, so the block's completeness rules cover
it with no new tag and no new rule — and the ONLY fields separating it from a
tone case are `level=3` and `dbfs=-127` (`tone_plan.h:kToneSilentDbfs`; silence
has no level and the line shape has a field for one). A reader keying on
`f_hz != 0` alone will judge silence against a criterion about a tone;
`read_tone.py`'s `deltas()` filters on `level` for that reason and its guard
red-proves the filter. One case per victim per distinct frequency, printed
after the static row, so every case index published before this arm existed
keeps its number.

`SHELL_TONE_SPAN` is the G5 span calibration (zero/rail/spread), copied
from round one's pass. `SHELL_TONE_G5` is the per-victim G5 address
verdict, one line per victim, also copied from round one's pass.

`SHELL_TONE_CLK` and `SHELL_TONE_CAL` carry round one's fields unchanged,
and they are **written out here rather than pointed at** (2026-09-19): this
section had documented them by reference to round one's spec, and a reader
author who parses by field name — which `read_tone.py` does — is not served
by a pointer to a section that had itself drifted. Transcribed from
`tone_probe.cpp`'s two `PrintLine` calls, not from round one's document.

`SHELL_TONE_STAT` is printed immediately after every `SHELL_TONE_LEVEL` line
(`print_level_lines()`, `shell/tone_probe.cpp:530-544`), the two forming
the pair the prose below
refers to. `settled_mean_spread` is the quantity G8 reads; `widest_sample_band`
is the per-conversion band.

**The order above is the stream's order, and it is not a unique one.** A
block opens with fifteen `_LEVEL`/`_STAT` pairs — five boot-virgin, ten
ladder — before its first `_CASE`: counted on the vendored block
`shell/testdata/tone-block-86070d9.txt`, where the first `_CASE` is
preceded by 15 of each. The static row then prints `_CASE` **before** its
own `_LEVEL`/`_STAT`, so the two tags interleave in both directions over a
whole block and no single linear list is the whole truth. The list is a
reading order, not a grammar; a reader keys on `(tag, case)` and not on
position. (A reader author should transcribe this section from
`tone_probe.cpp`'s format strings rather than trust it — see
`docs/gotchas.md`.)

**The `SHELL_TONE` line above is not this spec's original one.** The plan's
[decision 1](2026-09-18-coupon-codec-tone-probe.md) already records why and
is the citation for it, not repeated here: libDaisy's log buffer is 128
bytes and this line's combined form (case, level, f_hz, dbfs, victim
identity and one phase point together) runs about 130 characters at its
widest values — past the buffer, truncated and stamped `"$$"`. The identity
fields that do not change across a case's 16 phase points — `level`,
`f_hz`, `dbfs`, `victim_group`, `victim_ch`, `r_src`, `below_corner` — move
to `SHELL_TONE_CASE`, printed once per case; `SHELL_TONE` carries only what
changes per phase point. Every tone case is split this way, not only the
static one below. Same decision and same reason as round one's
`SHELL_XTALK_CASE`/point-line split.

**The window sweep's lines above are not this spec's original one either
(Task 5).** This section listed §6's sweep as a single combined
`SHELL_TONE_WIN` carrying the case, the victim identity, both chain words
and one window point together. That form runs **143 characters**, past the
same 128-byte log buffer — every window point would
have been truncated and stamped `"$$"`. The principle this section already
states for the tone lines ("Every tone case is split this way") was simply
never applied to it, so the split is made here and for the same reason,
citing the plan's [decision 1](2026-09-18-coupon-codec-tone-probe.md): the
identity fields that do not change across a case's 65 window points —
`xtalk_case`, `victim_group`, `victim_ch`, `r_src`, `word_a`, `word_b`,
`codec` — move to `SHELL_TONE_WINCASE`, printed once per window case, and
`SHELL_TONE_WIN` carries only what changes per point. The two lines run
**112** and **79** characters.

**Which bound those figures are**, because “widest” admits two and they are
far apart. They are **data-widest**: the widest rendering reachable with
this board's own tables (`xtalk_case` ≤ 57, `victim_ch` ≤ 15, `r_src` ≤ 5150,
16-bit chain words, 16-bit ADC readings, `d_before_end_ns` ≤ 12800, `n` ≤ 64).
The 143 is at `victim_ch=8`; at `victim_ch=15` it is 144, and the two lines
as actually printed in the capture are 104 and up to 79. **Type-widest** —
every `%d` at the 11 characters a negative `int` can print — is much larger
(179 for `SHELL_TONE_WINCASE`, 225 for the combined form) and is unreachable
from these tables. Ruling 18 holds under either bound: the combined line is
over the 125-byte payload both ways. `SHELL_TONE_GATES`/`SHELL_TONE_HEALTH`
below are quoted at **type-widest** (53 and 116; 112 at ten digits), which is
the right bound there because those counters are unbounded lifetime counts.

**The split addresses only the
too-long-line failure**; `logger.cpp:78-87`'s accumulation overflow strikes
a line of any length when the host does not drain fast enough, and nothing
here changes that.

`SHELL_TONE_WINDOW` is printed once per block, immediately before the two
window cases, and is the refusal gate: `window_ns` is 387.5 sampling cycles
at **this boot's measured** ADC clock, `nominal_ns` is the
`kToneWinWindowNsNominal` constant the host asserts against, `grid_end_ns`
is the last grid point, and `fits` is 1 only when the grid is strictly
shorter than the measured window. `fits=0` means the sweep did not run and
no `SHELL_TONE_WINCASE`/`SHELL_TONE_WIN` lines follow — a failed clock pass
reaches the same place, because an unmeasured clock leaves `window_ns` at 0.
`codec` on `SHELL_TONE_WINCASE` is 0 because the sweep runs with the codec
**stopped**: it is a round-one aggressor and the tone is not part of the
question. `xtalk_case` is the index into `kXtalkPlan` the aggressor came
from.

**Window case indices are their own namespace.** `case` on
`SHELL_TONE_WINCASE` and `SHELL_TONE_WIN` is the index into the window-case
table, numbered from 0, independently of the tone case indices — it is not a
continuation of the counter `SHELL_TONE_CASE` and `SHELL_TONE_LEVEL` share,
so `case=0` and `case=1` exist under both sets of tags and mean different
things. **A reader keys on the pair (tag, case), never on `case` alone**;
keying on `case` across different tags was never valid here. The
boot-virgin lines' negative numbering is not a precedent for doing the same
to these: that exists because boot-virgin and live level measurements share
one tag (`SHELL_TONE_LEVEL`), so nothing but the number could separate them,
whereas here the tags themselves differ. `codec` is still a printed field
rather than a remark in the prose, because the codec state genuinely differs
between the window sweep and every tone case and a reader must not have to
infer it.
`d_before_end_ns` counts backwards from the **end** of the acquisition
window, so this sweep's `d` **axis** and round one's `d`-after-the-edge axis
line up end to end.

**The axes line up; the curves do not, and that is measured (2026-09-19,
`task-5-board-capture.txt`).** This sweep's `d_before_end = 0` point on
`REF_A` reads 32760–32764 against round one's absolute grid mean of **32495**
for the same victim (`crosstalk-measured.md` §7) — a step of roughly **265**
counts, or **280** against round one's own `d = 0` point once §8's ~13-count
first-arrival depression is included. The two instruments also do not
publish the same quantity: round one's per-point statistic is a `delta`
against a control case, and this sweep has no control case at all, so it
reports an absolute mean. A reader must not take “line up end to end” as
“are comparable”. No account of the step is offered here.

**`audio_virgin` on `SHELL_TONE_LEVEL`/`SHELL_TONE_STAT` (Task 3, missing
from this section until Task 4 fix round 3) is what G8 reads.** `1` marks
the boot-virgin floor: measured exactly once per boot, before any
`StartAudio()` call anywhere in the image, then cached and re-emitted
verbatim inside every block at `case=-1` through `case=-5` (negative, one
per victim, so it can never collide with a live case). Every other
`SHELL_TONE_LEVEL` line — every per-block Stopped and RunningSilent
measurement, `case >= 0` — carries `audio_virgin=0`: a per-block Stopped
level is `StopAudio()` called on a codec an earlier block has already
started and stopped, which is a different history from "never started",
and is not the floor round one's own silent block (which never touches
audio at all) is comparable to. **G8 reads the `audio_virgin=1` lines and
only those.** The numbers on those lines are measured once, at boot, and
re-emitted rather than re-measured on every later block — re-measuring
would destroy the one property `audio_virgin=1` exists to assert.

`dbfs` is printed as a negative integer; `level` is 0 stopped, 1 running
silent, 2 tone. `phase_timeouts` (Task 4) is a lifetime count of repeats
that never saw their target phase, not reset per block; `block_ms` (Task 4)
is the wall-clock duration of the block that just finished, in
milliseconds, measured from the board's own millisecond tick and not
derived from the plan's table constants. `win_timeouts` (Task 5) is the
window sweep's own lifetime count of repeats whose EOC poll never saw the
conversion end — a separate counter because that sweep polls EOC itself and
never goes through `probe_adc::sample_now()`. None of the three is folded
into `gates_ok`.

**Those four fields are on `SHELL_TONE_HEALTH` and not on
`SHELL_TONE_GATES` (Task 5 fix round).** The combined line was measured, not
estimated, at **117 bytes** on a healthy run, **124 bytes** at ordinary
failure-run counter values (`missed_blocks=12`, `phase_timeouts=4096`,
`win_timeouts=8320`), **144 bytes** with the three counters at ten digits
and a realistic `block_ms` (170123), and **148 bytes** only when
`block_ms` is at ten digits as well — against the same 128-byte log
buffer. It therefore
truncated and was stamped `"$$"` precisely in the runs whose counters were
large, which is to say precisely when these four fields were the ones
anyone needed; a diagnostic that hides itself when things go wrong reads as
health. The split is the same move as `SHELL_TONE_CASE`/`SHELL_TONE` and
`SHELL_TONE_WINCASE`/`SHELL_TONE_WIN`, for the same buffer and the same plan
[decision 1](2026-09-18-coupon-codec-tone-probe.md). Measured widths of the
two lines: **53** and **112** bytes at ten-digit `uint32` values, **116** for
`SHELL_TONE_HEALTH` at type-widest (its counters are `static_cast<int>`-ed,
so a value above `INT_MAX` prints as 11 characters). `SHELL_TONE_HEALTH`
is printed immediately after `SHELL_TONE_GATES`, every block. **Fields moved,
nothing was recomputed**: `gates_ok` is still the AND of the same **four**
terms — `g2_floor`, `g4_jitter`, `g5_address` and `missed_blocks == 0`,
which *is* `g7`, so it is one term and not two, while `g8` is the literal
`-1` and has never been in it — the three counters are still lifetime
counts that are not reset per block, and `block_ms` is still the same
measured wall-clock subtraction.

The static row (present only when Task 1 measured a
DC-coupled output) is a tone case like any other for this purpose: it
prints its own `SHELL_TONE_CASE` line (`f_hz=0`, its `dbfs`), immediately
followed by a `SHELL_TONE_LEVEL`/`SHELL_TONE_STAT` pair and no `SHELL_TONE`
point lines — it has no phase, so there is no grid to walk, and a reader
keys its identity off `SHELL_TONE_CASE` the same way it does for every
other case. `shell/read_tone.py` follows `read_xtalk.py`,
computes `delta(φ)` and `delta_pp` per tone case, the stopped-versus-running
difference per victim, and the verdict; guard `shell/test_read_tone.py`,
CTest `read_tone_guard`. G8 needs round one's silent-block numbers as an
input: the reader takes round one's `xtalk.csv.meta.csv` on the command
line and refuses to run without it.

**The static row is reported and deliberately NOT gated**, which is a
knowing deviation from "a tone case like any other" above. Its `delta_pp`
over one point is 0 by construction, so a gate on it could never go red —
and this project fixes gates that cannot fail. `read_tone.py`'s
`static_deltas()` carries the same statement at its definition.

### Reproducing the campaign's numbers from the repository alone

The 2026-09-18/19 campaign's captures live under `.superpowers/sdd/`, which
is gitignored, and its `docs/hardware/` write-up is separate work that has
not been done. What **is** committed is one complete block of the final
image, `shell/testdata/tone-block-86070d9.txt`, and the reader beside it —
so the analysis is reproducible from committed inputs. From the repository
root:

```
python -c "import sys; sys.path.insert(0, 'shell'); import read_tone as r; raise SystemExit(r.report(r.parse_block(open('shell/testdata/tone-block-86070d9.txt')), open('docs/hardware/2026-09-19-xtalk.csv.meta.csv').read(), 'tone.csv'))"
```

It prints the four-boot G8(b) table, G8(a)'s report-only column, the 45-row
`delta_pp` table with each victim's own boot-virgin floor beside it, the
level diffs and the static rows, and writes `tone.csv` plus
`tone.csv.meta.csv`. **It exits 1 and that is the expected result**: ten
rows fail the `delta_pp ≤ 8` criterion, nine of them `REF_A` with seven
flagged as within its own 14-count floor. `read_tone.py`'s module docstring
is the authority for this command and carries it too.

**The window sweep's sign-agreement statistic needs its definition quoted
with it.** Commit `52843e0`'s message says "63 of 65 and 56 of 65 points
deviate with the same sign in the two blocks" and does not say deviate *from
what*. It is **deviation relative to each curve's own `d=12800` endpoint**,
counting only strict sign agreement — five other plausible references give
53, 55, 56, 57, 59 and 63, so the figure is not self-identifying. The verdict
("does not flip") is robust under all of them. The **56** half is
reproducible from the vendored block above; the **63** came from the
capture's partial first block, which is not committed. This bottom has now
defeated four summary statistics, so quote the averaged curve's deviation
bound over a stated interval (`d=3600..7000` within 1.75 counts of the
minimum at `d=5400`, the tightest interval of that form) and nothing
cleverer.

**Quote the frequency answer per victim.** "No slope — 4 counts or less over
a 50× span" is `REF_A` **only**. Worst span over 100 Hz → 5 kHz at a fixed
level, on the block above: `REF_A` **4**, `REF_C` **7**, `REF_B` **9**, and
**0** on both 150 Ω ties at all nine rows. The negative conclusion is
unaffected — a linear law over a 50× span needs far more than nine counts,
and `REF_C`'s and `REF_B`'s largest movements run *downward* with frequency
— and it must not be strengthened to compensate for the wider bound.

## 9. Before it is built

One bench reading, unpowered by the probe and unmeasured before this task:
**whether the Patch Submodule's audio output is DC-coupled at B1/B2.**
Nothing in this repo said, and it decides whether a static-level case (the
codec at a constant, no tone, the simplest possible aggressor) is available
or whether the lowest usable frequency is set by a coupling capacitor's
corner. The check was a meter on `TP_AUDIO_L` against `TP_AGND` while the
callback wrote a constant.

Measured 2026-09-18, handheld multimeter, DC volts, black probe on
`TP_AGND`, red probe on `TP_AUDIO_L`:

| what the callback wrote | reading at `TP_AUDIO_L` |
|---|---|
| −6 dBFS constant (`0.5011872f`), single-constant image | **−4.34 V**, steady — holds indefinitely, dips toward zero on RESET, returns to exactly −4.34 V when the callback restarts |
| true silence (`0.0f`), discriminator image | **−10.8 mV** |
| 0 dBFS (`1.0f`), discriminator image | **−8.66 V** |

The output is **DC-coupled** at B1/B2: there is no coupling capacitor — the
level holds for minutes and tracks the callback, collapsing on reset and
returning on restart. The static-level case therefore exists; `kToneCornerHz`
is 0. `SHELL_TONE_DC` is built as **1** by default (`shell/Makefile`).

The path **inverts**: a positive constant produces a negative voltage at
`TP_AUDIO_L`. Measured; no mechanism is claimed for it.

−8.66 / −4.34 = 1.995, against 10^(6/20) = 1.995: the chain does **not**
saturate across the top 6 dB, and full scale is 8.66 V peak.

True silence leaves 10.8 mV standing — a small offset, not zero. Stated as
measured; not characterised further.

## 10. How this can go red

- `tests/test_tone_plan.cpp`: every table row's frequency below half the
  sample rate, every level at or below 0 dBFS, every victim inside its mux's
  range, every `WIN` case's word pair agreeing in the victim's address and
  enable (the round-one assertion, reused). Prove the red by asserting a
  row's frequency against `sr / 2` and watching a deliberate 30 kHz row fail.
- The phase arithmetic is pure integer data and gets host assertions: 16
  points span exactly one period, and the crossing detector never waits
  more than two periods.
- The reader's G7 and G8 fixtures, G8 red on purpose (§7).
- On the board, the two 0 Ω victims are the zero again, and `REF_B` at 650 Ω
  is the impedance control: a `delta_pp` that does not scale between 650 and
  5150 Ω is not in the node.

## 11. What is read, what is derived, what is unmeasured

| Claim | Class |
|---|---|
| Audio out leaves on B1/B2 and runs to `J_AUDIO` and `TP_AUDIO_L` in the analog domain | read — `netlist.py:106`, `:330–337`; README's zone map |
| 48 kHz, block size from the board | read at runtime, printed |
| Interrupt latency of "some hundreds of nanoseconds" | **estimate**; G7 and the phase grid are sized so it does not matter, and the CAL pass measures the part of it that does |
| The 387.5-cycle rung lands on the divider's true value | measured — `settle-measured.md` §7 |
| DC coupling of the audio output | measured — §9 |
| That a 0 dBFS sine on an unloaded output is a valid aggressor for a loaded jack | **reasoned** — the trace is the coupling path either way; the load changes the drive impedance, not the geometry. Unverified, stated |
| That 0 dBFS does not clip the unloaded output | measured — §9: −8.66 V is 1.995× the −6 dBFS reading of −4.34 V, matching 10^(6/20) exactly, so the chain is linear across the top 6 dB. This is a narrower claim than the row above it and does not settle the loaded-jack question |
| Any `delta_pp` | unmeasured — that is the run |

## 12. Out of scope

- Coupling from the scan into the audio (the 2026-08-23 series).
- Anything with the jack populated: the coupon has no output stage to
  measure through, and the artifact rig's capture path is not this probe.
- The square-wave row until the sine slope is known.
- A second board; a second submodule.
