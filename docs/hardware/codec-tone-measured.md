# The codec tone — measured on the coupon

> **This document is the bench half of round two.** It records what the test
> coupon said on **2026-09-19**, on one board, in one session, and nothing
> else. Its question is the one round one could not ask: when the submodule's
> own audio output carries a tone through the analog zone, past the
> multiplexers, how far does a settled multiplexer channel move?
>
> **The answer the campaign's own criterion returns is negative** — §4. The
> answer a second statistic returns, on the same blocks, is **852 counts** —
> §6. Those are not in conflict: they are two different quantities, and the
> criterion is blind to the second by construction. §6 is the part of this run
> that was not analysed while the campaign was running and is first reported
> here.
>
> Its predecessor is [`crosstalk-measured.md`](crosstalk-measured.md), whose
> §6 deferred one sequence — a transient that has already decayed by the
> 991 ns offset floor. §7 below closes it. Read round one's §1–§3 first for
> what the ADC path and the victims are; this probe inherits both unchanged.
>
> **Every number here is either measured or derived, and each one says which.**
> Measured means a probe printed it. Derived means it was computed from
> something printed, and the computation is given. Nothing is upgraded from
> the second class to the first. Where the run characterised something without
> explaining it — §6, §7, §9 — it is left characterised. **No mechanism is
> asserted anywhere in this document**, including in §6, where a numerical
> correspondence with an earlier measurement is strong enough to be tempting
> and is written down as a correspondence.
>
> The instrument is `shell/tone_probe.cpp`, built behind `SHELL_TONE_PROBE`.
> It shares `shell/probe_adc.cpp` and round one's victim table verbatim, and
> defines no victims of its own. The image that produced every figure below
> ships as commit **`52843e0`**; the capture's own `git=86070d9+` stamp is the
> pre-commit HEAD, and `shell/testdata/tone-block-86070d9.txt`'s header
> carries the three commands that check the difference.

## 1. What the instrument is

Round one holds a settled channel and fires **one digital edge** at it. This
one holds the same channel and plays **a tone out of the codec** at it.

The five victims are round one's, used directly from `xtalk_plan.h`:

| victim | channel | mux | `r_src_ohm` | what it is |
|---|---|---|---:|---|
| `REF_A` | group 0, ch 8 | 74HC4067 on `ADC_9` | 5150 Ω | divider |
| `REF_B` | group 0, ch 9 | 74HC4067 on `ADC_9` | 650 Ω | divider |
| `R_SP10` | group 0, ch 10 | 74HC4067 on `ADC_9` | 150 Ω | 0 Ω link to `AGND` |
| `REF_C` | group 1, ch 6 | 74HC4051 on `ADC_10` | 5150 Ω | divider |
| `R_LO3` | group 1, ch 3 | 74HC4051 on `ADC_10` | 150 Ω | 0 Ω link to `AGND` |

The 150 Ω entries are the netlist's 0 Ω ties plus switch `Ron`; round one's §1
explains the naming and it is unchanged here. **They are the attribution
axis**: 150 Ω is 34× below the dividers, so a disturbance that is in the node
must be far smaller on them, and a disturbance that is in the ADC or the
firmware need not be.

The ladder is **three frequencies × three levels, plus one static row**:

| | −20 dBFS | −6 dBFS | 0 dBFS |
|---|---|---|---|
| **100 Hz** | ✔ | ✔ | ✔ |
| **1 kHz** | ✔ | ✔ | ✔ |
| **5 kHz** | ✔ | ✔ | ✔ |
| **DC (`f=0`)** | — | ✔ | — |

The static row exists only because a meter said it could. Task 1 measured the
Patch Submodule's audio output at `TP_AUDIO_L` against `TP_AGND` while the
callback wrote a constant: **−4.34 V at −6 dBFS, −8.66 V at 0 dBFS, −10.8 mV
at true silence**, holding indefinitely and collapsing on RESET. The output is
**DC-coupled at B1/B2** — no coupling capacitor, so `kToneCornerHz` is 0 and
no row is attenuated by a corner. Two things fall out of those three readings
and both are measured: the path **inverts** (a positive constant gives a
negative voltage), and −8.66 / −4.34 = 1.995 against 10^(6/20) = 1.995, so the
chain does not saturate across the top 6 dB. Full scale is 8.66 V peak at the
unloaded output. Spec §9 is the record.

Two reference levels are measured before every ladder and are not tone rows:
**Stopped** (`StopAudio()`, the codec idle) and **RunningSilent** (audio
started, the callback writing zeros). They are what a tone row is compared
against.

**The phase grid.** A sine has no single amplitude at the aperture, so each
case is measured at **16 phase points per period, 64 repeats each**, and each
repeat waits for the *next* crossing of its phase target — so repeats never
share one block's interrupt jitter. The reported quantity is **`delta_pp`**,
the peak-to-peak across those 16 phase means. A peak-to-peak and not a mean,
because a sinusoidal disturbance has zero mean over a period and a mean would
report every row as clean.

The criterion is spec §4's: **`delta_pp ≤ 8` counts** on every 5150 Ω victim at
every row — the same half-LSB-of-12-bit bound round one used, in the 16-bit
left-aligned counts the ADC returns.

## 2. What this instrument cannot see, and what it refuses to say

**`delta_pp` is blind to a constant offset, by construction.** It is a
peak-to-peak *within* a case. If a tone moves a victim's reading by a fixed
amount for the whole case, `delta_pp` reports zero movement. That is not a
defect in the statistic — it is the right statistic for the question spec §4
asks — but it means §4's negative result is a statement about the *shape* of a
row's 16 phase means and about nothing else. §6 is what happens when the same
blocks are read for offset instead.

**Frequency and measurement cadence are the same variable in this design, and
the run cannot separate them.** Each repeat waits for the next phase crossing,
so the interval between consecutive conversions on a victim is **about one
period of the tone** — 10 ms at 100 Hz, 1 ms at 1 kHz, 0.2 ms at 5 kHz. Any
effect that depends on how long the ADC waits between conversions therefore
tracks frequency exactly, and this instrument prints nothing that tells the two
apart. The confound is structural, it was not noticed while the campaign ran,
and §6 is where it bites.

*Derived, and it confirms the cadence figure from the run's own clock:* 45 tone
cases, 15 per frequency, 16 points × 64 repeats each, at one period per
repeat, predicts 153.6 + 15.4 + 3.1 = **172.0 s**. The block printed
`block_ms=170654`, i.e. **170.7 s** — 0.8 % apart. The wait really is one
period, and the 100 Hz rows really do spend 90 % of the run's wall clock.

**Absolute levels are biased low and may not be quoted as the node's voltage.**
Each victim is read at the rung its impedance picks, and `settle-measured.md`
§7 measured that rung reading **180 to 245 counts low in steady state at
5150 Ω**. Every comparison in this document is between two readings **of the
same victim at the same rung**, where that bias cancels — `park_victim()` sets
the rung once from `r_src_ohm` and the level cases and the tone cases go
through it identically (`tone_probe.cpp:318-325`). §7's window sweep is the one
exception and says so.

**What was not measured.** No second board, no second submodule, no rework, no
load on the jack — the coupon has no output stage, so every tone row is an
*unloaded* output. Spec §11 classes "a 0 dBFS sine on an unloaded output is a
valid aggressor for a loaded jack" as **reasoned, not verified**, and that
stands. The shipping firmware's free-running ADC DMA is not running in this
image either.

## 3. The gates

Round one's G2, G4 and G5 unchanged — two instruments judging one board by two
thresholds is the failure that avoids — plus two new ones.

| gate | bound | what it refuses |
|---|---|---|
| **G2** floor | reference channel's 64-conversion spread `b0` in 0…64 counts | a run whose conversion noise makes a mean of 64 repeats no longer decisively inside an 8-count criterion |
| **G4** jitter | `lat_max − lat_min` ≤ 200 ns, latency mean not negative | a run whose aperture jitter is coarser than the grid |
| **G5** address | every victim inside its `coupon_expect` band against a valid span | a victim that is not where the table says it is |
| **G7** callback health | zero missed blocks | a run in which the tone was not the tone — a starved callback outputs the DMA buffer's stale contents |
| **G8(b)** floor agreement | boot-virgin `settled_mean_spread` per victim inside `[min(boots) − 4, max(boots) + 4]` over this campaign's own four boots | an image whose floor is not the floor this campaign measured |

**Measured, the shipped block:** `gates_ok=1`.

```
SHELL_TONE_CFG    adc_khz=6146 repeats=64 phase_points=16 block_size=96 sr=48000 rv4=0
SHELL_TONE_CAL    lat_mean_ns=689 lat_min_ns=644 lat_max_ns=773 b0=17 timeouts=0
SHELL_TONE_SPAN   zero=0 rail=65532 hi_spread=0 lo_spread=0 valid=1
SHELL_TONE_GATES  g2=1 g4=1 g5=1 g7=1 g8=-1 gates_ok=1
SHELL_TONE_HEALTH missed_blocks=0 phase_timeouts=0 win_timeouts=0 block_ms=170654
```

`b0=17` against a bound of 64. `lat_max − lat_min = 129 ns` against 200.
`adc_khz=6146`, matching `settle-measured.md` §1's independently measured
6.146 MHz in a third image. All five victims `ok=1` on G5. **Zero missed
blocks, zero phase timeouts, zero window timeouts, zero ADC timeouts** across
170 seconds of measurement — G7 never came close to firing, and a 63 µs window
sweep inside a 2 ms block is why.

`g8=-1` is not a failure: G8 is computed on the host, and the firmware prints
`-1` for "not evaluated here". `read_tone.py` computes it.

**G8 does not do what spec §7 originally specified, and the split is the
honest part.** It was to compare this image's boot-virgin floor against round
one's published `settled_mean_spread`. Reading both instruments' source
settles that it cannot: round one's `measure_silent_point(d_ns)` spins
`park + d` before **every** conversion and its 65 points carry 65 **different**
delays, 0…12800 ns; this image's `measure_level()` spins nothing and varies
nothing across its 65 points. Same count, different content —
`tone_plan.h:79-84` states the requirement in its own words ("or the 4-count
bound compares two differently-shaped spreads and means nothing") and the
implementation matched the count and missed the content. So:

- **G8(a)**, the round-one comparison, is printed and **never gated**, labelled
  NOT LIKE-FOR-LIKE on every line.
- **G8(b)**, a real gate, compares against this campaign's own four boots.

All five victims **PASS** G8(b) on the shipped block. The four boots, the
bounds and the result:

| victim | four boots | bound | this block | |
|---|---|---|---:|---|
| `REF_A` 5150 Ω | 17, 16, 17, 14 | [10, 21] | 14 | PASS |
| `REF_C` 5150 Ω | 3, 3, 4, 4 | [−1, 8] | 4 | PASS |
| `REF_B` 650 Ω | 2, 3, 4, 5 | [−2, 9] | 5 | PASS |
| `R_SP10` 150 Ω | 1, 1, 0, 0 | [−4, 5] | 0 | PASS |
| `R_LO3` 150 Ω | 0, 0, 0, 0 | [−4, 4] | 0 | PASS |

**The floor itself moves three counts between boots** — `REF_A` 17/16/17/14,
`REF_B` 2/3/4/5. An earlier note in this project's working record claimed it
reproduced "to about one count"; the third and fourth boots supersede that. It
matters because the bound is 4 counts: a floor that wanders 3 between boots
spends most of that budget before any image change is measured at all. That is
also why G8(b) compares against a **range** and not a central value — the
midpoint of 14…17 is a number no boot ever produced.

## 4. The result: the criterion comes back negative on all three questions

Spec §10 states the falsification criterion in advance: **a `delta_pp` that
does not scale between 650 Ω and 5150 Ω is not in the node.** It does not
scale. All three questions return negative.

`delta_pp`, the shipped block, all 45 rows. **The criterion gates the two
5150 Ω victims — eighteen rows.** `REF_B` and the two ties are printed as
`report`: the 650 Ω victim is the impedance control and the ties are the
attribution axis, and neither is a row the bound was written for.

| victim | 100 Hz −20/−6/0 | 1 kHz −20/−6/0 | 5 kHz −20/−6/0 | floor |
|---|---|---|---|---:|
| `REF_A` 5150 Ω | 12, 11, 11 | 14, 15, 13 | 14, 14, 15 | 14 |
| `REF_C` 5150 Ω | 2, 6, 11 | 6, 5, 8 | 4, 3, 4 | 4 |
| `REF_B` 650 Ω | 7, 11, 6 | 9, 10, 8 | 4, 2, 5 | 5 |
| `R_SP10` 150 Ω | 0, 0, 0 | 0, 0, 0 | 0, 0, 0 | 0 |
| `R_LO3` 150 Ω | 0, 0, 0 | 0, 0, 0 | 0, 0, 0 | 0 |

**Q1, frequency: no slope.** Worst span over the 50× frequency range at a fixed
level is `REF_A` **4**, `REF_C` **7**, `REF_B` **9**, and **0** on both 150 Ω
ties. No linear law in frequency fits nine counts over 50×, and `REF_C`'s and
`REF_B`'s largest movements run *downward* with frequency, which is the wrong
direction for a capacitive path.

*This figure must be quoted per victim.* "4 counts or less over a 50× span" is
`REF_A` alone; the campaign's working record twice stated it as covering every
victim, and it does not.

**Q2, level: no slope on the two victims that fail.** `REF_A` runs 11…15 across
the whole table, with a span of 1 or 2 counts over a 10× amplitude change at
any fixed frequency.

**Q3, impedance: no ordering.** The 5150/650 ratio, computed over all nine
frequency-level pairs for both 5150 Ω victims against `REF_B`, spans **0.29 to
7.00** — against the 7.9× the impedance ratio would predict, and with `REF_C`
coming out *below* the 650 Ω victim in five of nine pairs. The two 5150 Ω
victims do not even agree with each other.

**And the attribution axis registers a zero and holds it.** Both 150 Ω ties read
`delta_pp = 0` at **all eighteen** of their rows, and their absolute means read
exactly 0 everywhere in the run. Whatever the 5150 Ω victims are doing, the
low-impedance channels are not doing a smaller version of it — they are doing
none of it.

### What the failing rows actually say

Ten of the eighteen gated rows exceed `delta_pp ≤ 8`. Nine are `REF_A`, and
**seven of those nine sit at or below `REF_A`'s own 14-count boot-virgin
floor**; the other two are one count above it. The reader flags each one:

```
FAILED case=10 f_hz=100  dbfs=-20 victim (0,8) delta_pp=12 floor=14 -- WITHIN THE FLOOR
FAILED case=15 f_hz=100  dbfs=-6  victim (0,8) delta_pp=11 floor=14 -- WITHIN THE FLOOR
FAILED case=20 f_hz=100  dbfs=0   victim (0,8) delta_pp=11 floor=14 -- WITHIN THE FLOOR
FAILED case=21 f_hz=100  dbfs=0   victim (1,6) delta_pp=11 floor=4
FAILED case=25 f_hz=1000 dbfs=-20 victim (0,8) delta_pp=14 floor=14 -- WITHIN THE FLOOR
FAILED case=30 f_hz=1000 dbfs=-6  victim (0,8) delta_pp=15 floor=14
FAILED case=35 f_hz=1000 dbfs=0   victim (0,8) delta_pp=13 floor=14 -- WITHIN THE FLOOR
FAILED case=40 f_hz=5000 dbfs=-20 victim (0,8) delta_pp=14 floor=14 -- WITHIN THE FLOOR
FAILED case=45 f_hz=5000 dbfs=-6  victim (0,8) delta_pp=14 floor=14 -- WITHIN THE FLOOR
FAILED case=50 f_hz=5000 dbfs=0   victim (0,8) delta_pp=15 floor=14
```

**On `REF_A` the criterion is reading the floor.** That victim's boot-virgin
spread is 14 with the codec stopped and no tone anywhere; a bound of 8 was
already unreachable before the audio was started. `REF_A` cannot pass this
criterion on this board and nothing about the tone follows from its nine
failures.

### The one row that is clearly outside its floor

`REF_C` at 100 Hz, `case=21`: **`delta_pp = 11` against a floor of 4.** It is
the only failing row anywhere in the table whose victim's floor leaves room for
it, and its level ladder is the only monotone one in the run:

| `REF_C`, 100 Hz | −20 dBFS | −6 dBFS | 0 dBFS |
|---|---:|---:|---:|
| `delta_pp` | 2 | 6 | 11 |

That is the shape a coupled signal has — roughly proportional to amplitude,
1.1 / 5.5 / 11 for a straight line through the origin against the measured
2 / 6 / 11. **It is recorded unlabelled, and here is why it stays unlabelled:**
nine level ladders in this run can vary at all — the other six belong to the two
150 Ω ties and are all zeros — and a strictly rising triple arises by chance one
time in six, so **1.5 of those nine** are expected to rise monotonically with no
cause behind them. Exactly **one** does. A single occurrence, however
well-shaped, is what chance already predicts. It is the best candidate in the
run, and it is a candidate.

What would settle it costs one capture: the same row at more than three levels,
on more than one boot. Nothing else in this campaign's data can.

## 5. What the static row and the silent levels say

Three readings bracket the tone rows, and all three are flat:

| victim | Stopped | RunningSilent | diff | static (DC, −6 dBFS) |
|---|---:|---:|---:|---:|
| `REF_A` 5150 Ω | 32513 | 32514 | +1 | −2 |
| `REF_C` 5150 Ω | 32610 | 32609 | −1 | −1 |
| `REF_B` 650 Ω | 32765 | 32766 | +1 | −2 |
| `R_SP10` 150 Ω | 0 | 0 | 0 | 0 |
| `R_LO3` 150 Ω | 0 | 0 | 0 | 0 |

**Starting the codec does nothing.** The SAI clocking, the DMA running and the
callback writing zeros move every victim by one count or less.

**A DC constant on the output does nothing either.** The static row holds
−4.34 V on `TP_AUDIO_L` — a large, steady voltage sitting in the analog zone
alongside the muxes for the whole measurement — and moves `REF_A` by **2
counts**. The static row is reported and not gated: one point has no
peak-to-peak.

Those two rows matter for §6, because they remove two candidate explanations
before it starts.

## 6. The 852 counts `delta_pp` cannot see

**This section was not part of the campaign's analysis.** It reads the same
committed blocks for a quantity the campaign's statistic discards: the
**absolute mean** of a tone case against that victim's own boot-virgin Stopped
level, at the same rung, where the rung bias cancels.

Shipped block, mean of a case's 16 phase means, minus the Stopped level:

| victim | 100 Hz | 1 kHz | 5 kHz | static (DC) | RunningSilent |
|---|---:|---:|---:|---:|---:|
| `REF_A` 5150 Ω | **−852** | **−310** | **−77** | −2 | +1 |
| `REF_C` 5150 Ω | **−673** | **−256** | **−77** | −1 | −1 |
| `REF_B` 650 Ω | −18 | −12 | −9 | −2 | +1 |
| `R_SP10` 150 Ω | 0 | 0 | 0 | 0 | 0 |
| `R_LO3` 150 Ω | 0 | 0 | 0 | 0 | 0 |

852 counts is **1.3 % of full scale** — 53 LSB of 12 bit, against a criterion
of half an LSB. It is roughly a hundred times the largest `delta_pp` anywhere
in the run.

**It reproduces across boots.** Three independent captures, three boots, same
image family:

| | `438fd51` | `c5631f4` | `task-5` |
|---|---:|---:|---:|
| `REF_A` 100 Hz | −850.7 | −851.7 | −852.4 |
| `REF_A` 1 kHz | −284.4 | −281.1 | −308.9 |
| `REF_A` 5 kHz | −70.2 | −70.8 | −76.2 |
| `REF_C` 100 Hz | −670.2 | −670.5 | −672.7 |
| `REF_B` 100 Hz | −17.2 | −18.2 | −17.9 |

The 100 Hz rows agree to **two counts across three boots**. The 1 kHz row is
the loose one, spanning 28 counts, and it is the only figure in this table that
would not survive being quoted to three digits.

**It does not scale with amplitude.** This is the decisive column, and it is
what separates this quantity from anything the tone's own signal could do:

| victim | f | −20 dBFS | −6 dBFS | 0 dBFS | spread |
|---|---|---:|---:|---:|---:|
| `REF_A` | 100 Hz | 31661.56 | 31661.81 | 31661.56 | **0.2** |
| `REF_A` | 1 kHz | 32204.88 | 32204.81 | 32203.06 | **1.8** |
| `REF_A` | 5 kHz | 32436.62 | 32437.00 | 32436.12 | **0.9** |
| `REF_C` | 100 Hz | 31938.19 | 31938.38 | 31937.56 | **0.8** |
| `REF_B` | 100 Hz | 32747.44 | 32747.62 | 32747.75 | **0.3** |

A **ten-fold** change in the aggressor's amplitude moves the reading by **under
two counts**, while changing 100 Hz to 5 kHz at a fixed amplitude moves
`REF_A` by **775**. Whatever this is, the tone's amplitude is not in it.

**It is ordered by source impedance, and the ties read exactly zero.** At
100 Hz: 5150 Ω → −852 and −673, 650 Ω → −18, 150 Ω → 0 and 0. That is the
attribution axis responding, and responding *harder* than proportionally —
852/18 is 47× across an impedance ratio of 7.9×.

### What this run can and cannot say about it

**Three explanations are already excluded by measurements in this document:**

- Not the codec running — RunningSilent moves ±1 (§5).
- Not a DC level on the output — the static row holds −4.34 V and moves 2 (§5).
- Not the tone's amplitude — ≤1.8 counts over 10× (the table above).

**What is left is confounded, and the confound is structural** (§2): frequency
and the interval between conversions are the same variable in this instrument.
The shift is monotone in frequency, and it is equally monotone in the wait —
10 ms, 1 ms, 0.2 ms. This run contains no case with a long wait and no tone,
so it cannot tell the two apart. **That is not a hedge; it is the design's
limit, and it was not seen while the design was being reviewed.**

**One numerical correspondence is worth recording, as a correspondence.**
`settle-measured.md` §7 measured what a 5150 Ω victim reads on its first
conversion after arriving from an `AGND` tie:

| victim | §7, first read from `AGND` | this run, 100 Hz shift |
|---|---:|---:|
| `REF_A` | −866 | −852 |
| `REF_C` | −682 | −673 |
| `REF_B` | −9 | −18 |

The two 5150 Ω figures agree to within 2 %. **That is an observation across two
instruments and two questions, and it is not an explanation**, for a reason
visible in §7 itself: §7's column is about arriving from a *different channel*,
and nothing changes channel inside a tone case — `park_victim()` runs once per
case and the channel is held for all 1024 conversions. Calling the
correspondence a mechanism would attach an unmeasured one, which this project
does not do.

### What would settle it

**Three cases, one per frequency, and the callback already supports them.**
Set `g_amplitude = 0.0f` while leaving `g_phase_step` at the row's real value,
and measure on the phase grid exactly as a tone row is measured. The callback
writes `a * s` with `a = 0`, so the output is **silent**; the phase accumulator
keeps advancing, so the grid waits exactly as long as it does for the real
tone. Cadence identical, aggressor gone, and the codec is running in both arms.

*Not "with the codec stopped".* That is the obvious phrasing and it is the
wrong experiment twice over: `phase_now()` derives its base from
`g_phase_at_block` and `g_dwt_at_block_start`, which only the callback writes,
so a stopped codec freezes the phase base the grid waits on; and stopping the
codec changes a second variable, which is the one thing this comparison cannot
afford.

If the shift survives the silent arm, it is the probe's cadence and the audio
output is exonerated. If it vanishes, the audio output moves a 5150 Ω pot
reading by 53 LSB of 12 bit and the design has a problem that `delta_pp ≤ 8`
never saw.

It is a small change to `tone_probe.cpp` and a reflash. **Until it is run,
neither reading of §4's negative result is safe:** the campaign concluded the
tone does not disturb a settled channel, and the statistic it concluded that
from cannot see the largest movement in its own data.

## 7. The edge inside the sampling window

Round one's §6 deferred one sequence: a transient that has already decayed
before its 991 ns offset floor. The reverse order reaches it — **the conversion
starts first** and the aggressor edge lands *inside* the acquisition window, at
a commanded time before its end.

This runs at the long rung, **387.5 ADC cycles**, with the codec **stopped**:
it is a round-one aggressor and the tone is not part of this question. The
firmware computes the window from *this boot's* measured clock and refuses the
sweep if the grid does not fit:

```
SHELL_TONE_WINDOW window_ns=63047 nominal_ns=63050 grid_end_ns=12800 fits=1
```

**63047 ns against a nominal 63050** — three nanoseconds, from a clock the
probe measured rather than assumed. The grid fits with a factor of five to
spare.

`MUX8_EN_N` against `REF_A`, both edge directions, 65 points at 200 ns, 64
repeats, four complete curves across two blocks of the committed capture:

- **The two directions are not mirror images. They are the same curve.** Both
  trace a shallow bowl. Round one's direction test — a disturbance that flips
  sign with the edge is coupling, one that does not is the pulse itself —
  returns **does not flip**.
- **The bowl, quoted the only way it survives being quoted.** The four-curve
  average minimum is **32750.25 at d = 5400 ns**, and every point from
  **d = 3600 to 7000 lies within 1.75 counts of it** — the tightest interval of
  that form. At 2.50 counts the interval widens to 3400…7600. The bottom is
  flat over roughly a third of the grid and no single argmin means anything:
  the four curves put theirs at 4400/4600/5200, 3800/4400/6800, 5400, and
  4200…6200 (a six-way tie).
- **The magnitude is the floor's magnitude.** Per-curve peak-to-peak is 18, 18,
  19, 16 — against `REF_A`'s own boot-virgin floor of 14…17. Only the
  four-fold reproducibility of the bowl's *shape* separates the two, and
  **whether the floor reproduces in shape is unmeasured** — the firmware prints
  the floor's peak-to-peak and never its 65 sub-means.

**The curve's endpoint independently confirms `settle-measured.md` §7.** The
four-curve average at `d = 12800` is **32766.25**, and §7 measured `REF_A` at
this same 387.5-cycle rung reading **32761…32765**, three weeks earlier, in a
different image, on a different question. Nothing was tuned to make those
agree.

### The junction with round one does not line up

The two instruments' grids were built to meet end to end: this sweep's
`d_before_end = 0` and round one's `d = 0` are the same victim and the same
aggressor at two places in one conversion. They do not join.

| | round one, `d = 0` | round two, `d_before_end = 0` | step |
|---|---:|---:|---:|
| case 10 | 32487 | 32760 / 32762 | **+273 / +275** |
| case 11 | 32485 | 32760 / 32764 | **+275 / +279** |

**Reported unsmoothed.** Part of it is accounted for and part is not: the two
points are **not at the same sampling rung** — round one reads `REF_A` at rung
1 (2.5 ADC cycles, a 407 ns window), this sweep at rung 6 (387.5 cycles,
63 µs) — and `settle-measured.md` §7 measured that exact rung pair differing by
**+243 to +245 counts** on `REF_A`. So the step's sign and most of its
magnitude are a known, measured rung bias. **The residue is 28 to 34 counts and
it is not explained.** Two different sessions and two different boots are in
that number as well, and this run cannot decompose it further.

*One retired statistic.* Commit `52843e0`'s message quotes "63 of 65 and 56 of
65 points deviate with the same sign in the two blocks". Recomputing it against
each curve's own `d = 12800` endpoint on the committed capture gives 64, 63, 63
and **55**. Spec §8 already records that the figure is not self-identifying —
six plausible reference choices give 53, 55, 56, 57, 59, 63 — and instructs
that the interval bound above be quoted instead. The verdict the statistic
supported, *does not flip*, is robust under every one of them.

## 8. What this rests on

| claim | class |
|---|---|
| The audio output is DC-coupled at B1/B2, full scale 8.66 V peak, inverting | **measured** — handheld meter, three readings, spec §9 |
| `adc_khz = 6146` | **measured**, printed, and matching `settle-measured.md` §1 in a different image |
| Every `delta_pp` in §4 | **measured**, printed per phase point |
| The absolute shifts in §6 | **derived** — case mean of 16 printed phase means, minus the printed Stopped level, same victim, same rung |
| The 172 s cadence figure in §2 | **derived** from the ladder, checked against printed `block_ms` |
| The window in §7, 63047 ns | **measured** from this boot's clock |
| The rung bias that absorbs most of §7's junction step | **measured**, but in a different run — `settle-measured.md` §7 |
| Why §6's shift happens | **unmeasured**, and the one case that would decide it was not run |
| Whether `REF_C`'s 100 Hz ladder is coupling or chance | **unmeasured** — one monotone ladder out of fifteen |
| That an unloaded output is a valid aggressor for a loaded jack | **reasoned, not verified** — spec §11 |

## 9. Open, characterised, deliberately unexplained

- **§6's 852 counts.** Three explanations excluded, cadence and tone
  confounded, one case would settle it. No mechanism attached.
- **§7's 28–34 count residue** at the two instruments' junction, after the
  measured rung bias is taken out.
- **§4's `REF_C` 100 Hz ladder**, 2 → 6 → 11. The best candidate in the run and
  inside what chance produces from fifteen triples.
- **`REF_A`'s floor of 14** makes an 8-count criterion unreachable on that
  victim before any aggressor exists. The criterion is reading the floor, and
  this document says so rather than widening the bound.
- **The boot-virgin floor wanders 3 counts between boots**, against a G8 bound
  of 4.
- **Round one's G6 fails on this board at 8–12 counts** against a bound of 8,
  in the 2026-09-19 re-measurement, while both 150 Ω ties do not move at all.
  That failure belongs to round one and is quarantined in
  [`crosstalk-measured.md`](crosstalk-measured.md) §13 — **no mechanism
  attached there either**. It is listed here because round two's G8(a) column
  consumes that same re-measurement's metadata, and because a second board, or
  this one under recorded conditions, is what would turn it into a finding.
- **`REF_C` reads 14 in that re-measurement's metadata against 3/3/4/4 here.**
  Recorded, not explained; spec §7 carries the full arithmetic showing that no
  choice of round-one column reconciles `REF_A`, `REF_C` and `REF_B` together.

## 10. What it means for the design

**As the criterion poses it, nothing needs changing.** No frequency slope, no
level slope, no impedance ordering, both attribution ties at exactly zero. The
audio trace running through the analog zone past the muxes does not put a
readable sinusoid on a settled pot channel, at any of nine frequency-level
combinations, on this board.

**As §6 poses it, one measurement decides whether that sentence is true.** If
the 852 counts are the probe's own cadence, the sentence stands and the
instrument needs a note. If they are the audio output, a 5150 Ω divider moves
53 LSB of 12 bit when a 100 Hz tone plays, and the panel's high-impedance
controls have a problem that no statistic in this campaign was looking for.
**That is three silent cases, one flash and one capture, and it should happen
before any panel decision leans on round two.**

Two things this run does *not* license either way: it says nothing about a
loaded jack, and nothing about the shipping firmware's free-running ADC DMA,
which is not in this image.

## 11. How to repeat it

The four captures this document reads are committed under
[`captures/`](captures/), and the §4 analysis is reproducible from committed
inputs alone. From the repository root:

```bash
python -c "import sys; sys.path.insert(0, 'shell'); import read_tone as r; raise SystemExit(r.report(r.parse_block(open('shell/testdata/tone-block-86070d9.txt')), open('docs/hardware/2026-09-19-xtalk.csv.meta.csv').read(), 'tone.csv'))"
```

**It exits 1 and that is the expected result** — the ten rows of §4. It prints
the G8(b) table, G8(a)'s report-only column, the 45-row `delta_pp` table with
each victim's floor beside it, the level diffs and the static rows, and writes
`tone.csv` and `tone.csv.meta.csv` beside itself. `read_tone.py`'s module
docstring is the authority for this command.

§6's and §7's tables are computed from the capture files directly; both are
plain `SHELL_TONE` and `SHELL_TONE_WIN` line scans over
`docs/hardware/captures/`.

To rebuild and reflash the image:

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_TONE_PROBE=1
```

`images`, not `all`, and never `source env.sh` in that shell. `cmp` the result
against the previous image before flashing — `make` has one-second mtime
resolution on this machine and has twice produced byte-identical images for two
different switch positions.

The design half is
`docs/superpowers/specs/2026-09-18-coupon-codec-tone-probe-design.md`.
