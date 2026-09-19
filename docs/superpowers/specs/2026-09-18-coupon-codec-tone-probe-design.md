# The coupon codec-tone probe — design (round two)

**Date:** 2026-09-18
**Status:** design, unbuilt; **depends on round one**
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
two instruments' curves line up end to end — round one's `d` after the edge
continues where this one's `d_before_end` stops. `t0` here is the conversion
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
| **G8** floor agreement | the **boot-virgin** stopped level's `settled_mean_spread` per victim — the `SHELL_TONE_LEVEL`/`STAT` lines with `audio_virgin=1`, and only those — within 4 counts of round one's silent block for the same victim | an image whose floor is not the floor round one measured — the refactor or the linker having moved something; the number is round one's own conversion-noise estimate |

G8 is the one to prove red deliberately: a stopped-level spread 5 counts off
round one's must fail, because that is a run whose tone results would all be
plausible and all be against the wrong baseline.

**Measured across three boots, no mechanism attached** (Task 4 fix round 3):
the boot-virgin floor's `settled_mean_spread`, against round one's own two
published columns (`xtalk.csv.meta.csv`'s whole silent grid, and that grid
with its `d=0` point dropped):

| victim | boot-virgin spread | round one, whole grid | round one, `d=0` dropped |
|---|---|---|---|
| `REF_A` g0 ch8 5150 Ω | 17, 16, 17 | 15/16/17 | 10/11/12 |
| `REF_C` g1 ch6 5150 Ω | 3, 3, 4 | 10/11/12 | 6/6/6 |
| `REF_B` g0 ch9 650 Ω | 2, 3, 4 | 6/6/7 | 6/6/7 |
| `R_SP10` 150 Ω | 1, 1, 0 | 0/0/1 | 0/0/1 |
| `R_LO3` 150 Ω | 0, 0, 0 | 0/0/0 | 0/0/0 |

The floor is not identical across boots: `REF_B` reads 2, 3 and 4 across the
three boots, a spread of two counts, and `REF_C` and `R_SP10` each move by
one count. An earlier note in this project's working record claimed the
boot-virgin floor was reproducible across boots "to about one count"; the
third boot supersedes that claim, and this spec records the wider figure
instead. This bears directly on G8, whose bound is 4 counts: a floor that
itself moves 2 counts between boots spends half that budget before any
image change has been measured at all.

`REF_A` lands on round one's whole-grid figure. `REF_C` and `REF_B` come out
**lower** than either of round one's columns, not higher. Carrying the same
minimum/maximum-of-absolute-difference method through all three boots
against both of round one's published columns, over the same table, gives
the full picture:

| victim | vs. whole-grid column | vs. `d=0`-dropped column |
|---|---|---|
| `REF_A` | 0-2 counts off — inside | 4-7 counts off — **outside** |
| `REF_C` | 6-9 counts off — **outside** | 2-3 counts off — inside |
| `REF_B` | 2-5 counts off — **outside** at its worst pairing | 2-5 counts off — **outside** at its worst pairing |
| `R_SP10` | 0-1 counts off — inside | 0-1 counts off — inside |
| `R_LO3` | 0 counts off — inside | 0 counts off — inside |

`REF_A` and `REF_C` fail under **opposite** columns: the whole-grid column
clears `REF_A` and fails `REF_C`; the `d=0`-dropped column clears `REF_C`
and fails `REF_A`. Each column rescues one of the two and breaks the other.
`REF_B` exceeds G8's 4-count bound at its worst pairing under **both**
columns — a fact the single-column framing above does not surface at all.
**This is stated as a measured fact, open, not resolved here**: no choice
of column satisfies G8 as specified against `REF_A`, `REF_C` and `REF_B`
together. The open question is therefore not "which column" — no column
answers it. What G8 should do about `REF_A`, `REF_C` and `REF_B` is a
decision for the controller and Bastian, not answered by this spec.

## 8. Output

```
SHELL_TONE_CFG   adc_khz=%d repeats=%d phase_points=%d block_size=%d sr=%d rv4=%d git=%s
SHELL_TONE_CLK / _CAL                       (round one's lines, unchanged)
SHELL_TONE_CASE  case=%d level=%d f_hz=%d dbfs=%d victim_group=%d victim_ch=%d r_src=%d below_corner=%d
SHELL_TONE       case=%d phase_idx=%d n=%d mean=%d min=%d max=%d
SHELL_TONE_LEVEL case=%d level=%d victim_group=%d victim_ch=%d r_src=%d n=%d mean=%d min=%d max=%d audio_virgin=%d   (stopped and running-silent)
SHELL_TONE_STAT  case=%d settled_mean_spread=%d widest_sample_band=%d
SHELL_TONE_WIN   case=%d victim_group=%d victim_ch=%d r_src=%d word_a=%d word_b=%d d_before_end_ns=%d n=%d mean=%d min=%d max=%d
SHELL_TONE_SPAN  zero=%d rail=%d hi_spread=%d lo_spread=%d valid=%d
SHELL_TONE_G5    victim_group=%d victim_ch=%d expect=%d mean=%d ok=%d
SHELL_TONE_GATES g2=%d g4=%d g5=%d g7=%d g8=%d missed_blocks=%d gates_ok=%d phase_timeouts=%d block_ms=%d
SHELL_TONE_END
```

`SHELL_TONE_SPAN` is the G5 span calibration (zero/rail/spread), copied
from round one's pass. `SHELL_TONE_G5` is the per-victim G5 address
verdict, one line per victim, also copied from round one's pass.

`SHELL_TONE_STAT` is printed immediately after every `SHELL_TONE_LEVEL` line
(`shell/tone_probe.cpp:389-391`), the two forming the pair the prose below
refers to. `settled_mean_spread` is the quantity G8 reads; `widest_sample_band`
is the per-conversion band.

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
derived from the plan's table constants. Neither is folded into
`gates_ok`. The static row (present only when Task 1 measured a
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
