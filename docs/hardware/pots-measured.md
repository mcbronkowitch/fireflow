# The pots — measured on the coupon

> **This document is the bench half of round four.** It records what the
> test coupon said on **2026-09-28**, on one board, in one session: three
> complete settle blocks and three complete wait blocks, with RV2, RV4 and
> RV6 set to mid travel and not touched in between. Its questions are the two
> the earlier rounds could only answer on fixed dividers: how long does a
> *real* pot take to settle after a channel change (Phase-0 Task 6 step 5b),
> and does the wait effect of round three — and its fix, the 387.5-cycle rung
> — carry over to a real pot?
>
> **The answer, in three lines:** the long rung holds every pot flat within
> **3 counts** at every wait, as it held every divider. The wait effect hits a
> pot as hard as a divider of the same impedance — at a 2 ms read cadence a
> 20 k pot at mid travel reads **~650 counts low** at the working rung. And a
> 20 k pot settles like its 5150 Ω divider, but the **10 k pots settle 1.7–2.1×
> slower than the model**, outside the 1.3–1.5× band the dividers set.
>
> **Every number here is either measured or derived, and each one says
> which.** No mechanism is asserted anywhere in this document. Its
> predecessors are [`settle-measured.md`](settle-measured.md) and
> [`wait-measured.md`](wait-measured.md); the instrument is theirs, extended.
>
> Spec [`2026-09-28-coupon-pot-round-design.md`](../superpowers/specs/2026-09-28-coupon-pot-round-design.md),
> plan beside it. The images are commit **`618427c`** (settle) and
> **`e20b8fd`** (wait), each stamped on its own `SHELL_POT_CFG` line.

## 1. What the instrument is

The settle probe (`shell/settle_probe.cpp`) and the wait probe
(`shell/wait_probe.cpp`), unchanged in method, built with
`SHELL_POT_ROUND=1`, which adds the three pots (`shell/pot_plan.h`):

| pot | mux | channel | neighbours | R_track (nominal) | R_src at mid, derived |
|---|---|---:|---|---:|---:|
| RV2 | 4067 (`ADC_9`) | 2 | 1 (A+3V3), 3 (AGND) | 10 k | 2650 Ω |
| RV4 | 4067 (`ADC_9`) | 6 | 5 (A+3V3), 7 (AGND) | 20 k | 5150 Ω |
| RV6 | 4051 (`ADC_10`) | 2 | 1 (A+3V3), 3 (AGND) | 10 k | 2650 Ω |

- **Settle:** six more pairs, P6–P11 — for each pot a step from its high
  neighbour, then from its low one. Everything else is the six-pair probe of
  `settle-measured.md` §1: 20 µs park, 65 grid points at 200 ns, 64 repeats,
  the knee judged against the curve's own tail. P0–P5 run in the same boot as
  controls.
- **Wait:** three more victims, 5–7, through all four arms (A wait, B one
  discard, L 387.5-cycle rung, C codec running). Everything else is
  `wait-measured.md` §1. Round one's five victims run in the same boot as
  controls.
- **The pots were set by hand** on the bring-up image, to what `REF_A` — the
  10k/10k divider, nominally 50/50 — reads there (about 31734 on that image's
  scale, whose rail is 63484; not the spec's 32768, which on that scale is
  x ≈ 0.516). The pots hang on their solder joints only; the board was not
  touched between the bring-up image and the last wait block.

## 2. The run, and its gates

**Measured**, every complete block (`_CFG`, `_CAL`, `_GATES`, `_HEALTH`):

| | settle 1 | settle 2 | settle 3 | wait 1 | wait 2 | wait 3 |
|---|---:|---:|---:|---:|---:|---:|
| `sweep_dir` | 1 | 0 | 1 | 1 | 0 | 1 |
| `adc_khz` | 6146 | 6146 | 6146 | 6146 | 6146 | 6146 |
| `lat_min…lat_max` ns | 671…773 | 671…807 | 671…704 | 673…813 | 671…792 | 659…811 |
| `b0` | 34 | 40 | 25 | 8 | 8 | 11 |
| gates | G1 G2 G4 pass, **G3 fails** | same | same | G2 G4 G5 G7 pass | same | same |
| G9 (host) | — | — | — | −867 PASS | −870 PASS | −866 PASS |
| `timeouts` | 0 | 0 | 0 | 0 | 0 | 0 |
| `block_ms` | not printed | | | 143 417 | 143 419 | 143 417 |

`adc_khz = 6146` is the fifth image in a row to measure the same clock. The
spec **estimated** ~143.5 s per wait block (`wait_block_estimate_ms(8)`); the
board **measured** 143.4 s. Both captures open with a partial block that
started before the host opened the port; neither is used anywhere here.

**G3 fails in every settle block, and the settle times are quoted anyway —
on the same terms as 2026-09-17/18** (`settle-measured.md` §3, §4). G3 bounds
the settled region's peak-to-peak spread at 8 counts over P0–P5; the dividers
P1–P4 wander **9–12** counts, which is the 2026-09-18 board's own behaviour.
G3 is about 2× stricter than the criterion a knee is decided on (every
settled point within ±8 counts of the tail reference), so every settle time
below met that criterion at every settled point. What travels with them is
the width of the settled region, and for the pot pairs that width is
**6–13** counts (`SHELL_SETTLE_BAND`, §4). `read_settle.py` and
`read_pots.py` refuse the run for G3 exactly as designed; this document
quotes it by Bastian's decision of 2026-09-28, not because a gate was moved.

## 3. The pots' positions

**Measured**, per image, the reading the position gates judge (spec §7):

| pot | settle image (mean of its two pairs' `tail_ref`) | wait image (arm L, `W` = 0) | PG1 | PG2 \|wait − settle\| |
|---|---:|---:|---|---:|
| RV2 | 32480.0 / 32480.0 / 32480.5 | 32681 / 32682 / 32681 | PASS | 200.5 … 202.0 |
| RV4 | 32430.5 / 32430.5 / 32429.5 | 32718 / 32719 / 32718 | PASS | 287.5 … 288.5 |
| RV6 | 32544.0 / 32543.5 / 32544.0 | 32687 / 32687 / 32686 | PASS | 142.0 … 143.5 |

**The pots did not move.** Each reading holds within one count across the
three blocks of its image. The difference between the images is the
instrument's, not the pots': the same comparison on the dividers, which
cannot move, gives **274–275** counts on `REF_A` and **218–219** on `REF_C`
(settle's short rung after a 20 µs park against the long rung back to back).

*Derived:* from the wait image's long-rung reading, x = 0.4987 / 0.4992 /
0.4988, so each pot sits within 0.2 % of its 50/50 point and its source
impedance within 0.001 % of R_track/4 + 150 Ω — **with R_track nominal**
(spec §3: the track cannot be metered in circuit). Take the position from
the wait image: the settle-side x is 0.003–0.004 lower for the reason in the
paragraph above.

## 4. Settle: the pots against the model and against the dividers

**Measured** knees, **derived** true settle = knee + that pair's offset
(991 ns at the 1.5-cycle rung, 1154 ns at 2.5), blocks 1 / 2 / 3:

| pair | step | R_src | true settle, ns | model | ratio | settled-region spread |
|---|---|---:|---|---:|---|---|
| P6 | ch1 (A+3V3) → **RV2**, 4067 | 2650 | 2791 / 2791 / 2591 | 1552 | 1.67–1.80 | 7 / 8 / 8 |
| P7 | ch3 (AGND) → **RV2**, 4067 | 2650 | 2591 / 2591 / 2591 | 1552 | 1.67 | 11 / 10 / 10 |
| P8 | ch5 (A+3V3) → **RV4**, 4067 | 5150 | 4554 / 4554 / 4754 | 3016 | 1.51–1.58 | 9 / 11 / 10 |
| P9 | ch7 (AGND) → **RV4**, 4067 | 5150 | 4154 / 4154 / 4154 | 3016 | 1.38 | 10 / 12 / 13 |
| P10 | ch1 (A+3V3) → **RV6**, 4051 | 2650 | 1991 / 1991 / 1991 | 955 | 2.08 | 7 / 6 / 8 |
| P11 | ch3 (AGND) → **RV6**, 4051 | 2650 | 1991 / 1991 / 1991 | 955 | 2.08 | 10 / 10 / 10 |
| P1 | `R_LO2` (AGND) → `REF_A`, 4067 | 5150 | 3954 / 4154 / 4354 | 3016 | 1.31–1.44 | 11 / 9 / 9 |
| P2 | `R_HI2` (A+3V3) → `REF_A`, 4067 | 5150 | 4354 / 4554 / 4354 | 3016 | 1.44–1.51 | 11 / 10 / 9 |
| P4 | `R_LO4` (AGND) → `REF_C`, 4051 | 5150 | 2754 / 2754 / 2754 | 1856 | 1.48 | 9 / 10 / 10 |
| P3 | `R_LO2` (AGND) → `REF_B`, 4067 | 650 | ≤ 991 / 1191 / ≤ 991 | 381 | — | 10 / 12 / 11 |

**A 20 k pot settles like its divider.** RV4's six times, 4154–4754 ns, lie
inside the range this board has shown for `REF_A` at the same impedance on the
same mux: 3954–4554 ns today, 3754–5154 ns across the two earlier sessions
(`settle-measured.md` §4). Nothing here separates a pot from a divider at
5150 Ω.

**The 10 k pots are slower than the model by more than the dividers are.**
RV2 comes out at 1.67–1.80×, RV6 at 2.08× in all six values — against the
1.3–1.5× band the resolvable divider pairs set on 2026-09-17 and the
1.2–1.7× of the 2026-09-18 repeat. RV6's knee is 1000 ns in every block,
five grid points above zero, so it is resolved, not the instrument floor the
spec had flagged as a possible outcome. The model is linear in R·C; at
2650 Ω it predicts roughly half the 5150 Ω time, and the board gives about
60 % (4067) and 72 % (4051). *Derived*, not attributed: whatever the model is
missing does not scale with R the way its one pole does.

**Characterised, not explained:**

- On RV4 the step from the high neighbour (P8) is slower than the step from
  the low one (P9) in all three blocks, by 400–600 ns; RV2 shows the same
  sign in two of three blocks, by 200 ns; RV6 shows none.
- P3 (`REF_B`, 650 Ω) resolved one knee above its floor once in three
  blocks — the same floor behaviour `settle-measured.md` §4 recorded for it.

## 5. Wait, arm A: the pots take the shift

Shift in counts against the same case's `W` = 0, blocks 1 / 2 / 3:

| `W` | RV2 2650 Ω, 4067 | RV4 5150 Ω, 4067 | RV6 2650 Ω, 4051 | `REF_A` 5150 Ω, 4067 | `REF_C` 5150 Ω, 4051 |
|---|---|---|---|---|---|
| 100 µs | −36 / −37 / −35 | −49 / −49 / −48 | −35 / −36 / −34 | −38 / −42 / −41 | −49 / −48 / −49 |
| 1 ms | −270 / −268 / −267 | −380 / −377 / −376 | −194 / −195 / −192 | −357 / −358 / −362 | −294 / −292 / −290 |
| 2 ms | −465 / −464 / −461 | −654 / −650 / −650 | −327 / −328 / −324 | −620 / −623 / −617 | −497 / −495 / −490 |
| 5 ms | −642 / −647 / −642 | −903 / −902 / −903 | −450 / −452 / −450 | −859 / −860 / −862 | −681 / −681 / −681 |
| 10 ms | −644 / −646 / −643 | −904 / −902 / −902 | −449 / −452 / −450 | −861 / −863 / −863 | −681 / −682 / −680 |
| 50 ms | −642 / −644 / −643 | −903 / −903 / −902 | −449 / −452 / −450 | −859 / −864 / −864 | −682 / −681 / −680 |

`REF_B` (650 Ω) sits at −14…−19 from 2 ms on. The two 150 Ω ties read 0
everywhere except three points at +1 across all arms and blocks, each inside
`read_wait.py`'s ±1 tolerance, as round three also saw.

**The curve has round three's shape on every pot:** it grows over one to five
milliseconds and saturates by 5 ms, flat within 4 counts from 5 to 50 ms in
every block.
`REF_A` saturates at −859…−864 here against −855…−862 on 2026-09-27
(`wait-measured.md` §3), so the instrument measured the same thing again.

**Ordered by impedance and by chip, and a pot is not quite its divider:**

- At 5150 Ω on the 4067, RV4 saturates about **40 counts deeper** than
  `REF_A` (−902…−904 against −859…−864) — about 5 %, in all three blocks.
- At 2650 Ω, the 4067 channel (RV2, about −643) shifts **1.4×** the 4051
  channel (RV6, about −450). At 5150 Ω the dividers show the same order:
  `REF_A` about −862 on the 4067, `REF_C` about −681 on the 4051 (1.27×).
- *Derived:* on the 4067, the 2650 Ω pot takes about **71 %** of the 5150 Ω
  pot's saturated shift (−643 against −903), while the 650 Ω divider takes
  about 2 %. The shift is far from linear in source impedance.

None of the three is explained here.

## 6. Arm B: one discarded conversion

Shift at saturation (mean of 10, 20, 50 ms), blocks 1 / 2 / 3: RV2
−39.7 / −42.0 / −40.0, **RV4 −53.3 / −54.7 / −54.7**, RV6 −23.3 / −27.0 /
−25.0; `REF_A` −42.7 / −43.7 / −42.7, `REF_C` −34.0 / −36.7 / −35.3, `REF_B`
−13.0 / −13.3 / −14.0.

**One discard pays off 94 % of the pot's shift, and the rest stays** — the
round-three result, now on pots. *Derived:* RV4's residue of about 54 counts
is 3.4 LSB of 12 bit, more than `REF_A`'s ~43 on the same mux, in step with
arm A's ordering in §5.

## 7. Arm L: the 387.5-cycle rung

**Flat on every pot.** Largest |shift| anywhere in arm L, every `W`, blocks
1 / 2 / 3: RV2 2 / 3 / 2, **RV4 3 / 2 / 2**, RV6 1 / 1 / 2. On the controls:
`REF_A` 2 / 4 / 2, `REF_C` 1 / 2 / 1, `REF_B` 4 / 1 / 2. The long rung reads a
pot the same after a 50 ms wait as back to back, as round three found for the
dividers.

## 8. Arm C, the codec bridge

At 10 ms: RV2 −645 / −643 / −642, RV4 −903 / −905 / −901, RV6 −451 / −450 /
−452 — each within 3 counts of the same pot's arm A at 10 ms. G9 (`REF_A`,
arm C, 10 ms) passes at −867 / −870 / −866. **The running codec makes no
difference this run can separate on the pots either.** At 1 ms and 200 µs
arm C and arm A differ on the pots by 4 counts or less, with either sign from
block to block — unlike the dividers in `wait-measured.md` §6, where arm C
ran consistently deeper at 1 ms. Recorded, not attributed.

## 9. What this rests on

| claim | class |
|---|---|
| Every table in §2–§8 | **measured**, the two committed captures |
| True settle, ratios, shifts, arm-B means, x, R_src | **derived**, `read_settle.py` / `read_wait.py` / `read_pots.py` |
| That the pots did not move | **measured** — each reading within one count across its image's three blocks |
| That the image-to-image difference is the instrument's | **measured on the dividers**, applied to the pots by analogy |
| R_src of a pot | **derived with a nominal R_track** — the part's tolerance is unmeasured |
| Quoting settle times with G3 failed | **a decision** (Bastian, 2026-09-28), on `settle-measured.md` §4's terms |
| Any mechanism — the 10 k slow-down, the pot-versus-divider 5 %, the chip ratio, the hi/lo asymmetry | **none** |

One board, one submodule, one session, three pots.

## 10. Open, characterised, deliberately unexplained

- **The 10 k settle factor**, 1.67–2.08× against the dividers' 1.3–1.5×.
- **RV4 about 40 counts deeper than `REF_A`** at saturation, and 11 counts
  more arm-B residue — a real pot, at nominally the same impedance, on the
  same mux. The pot's tolerance could do it; nothing here measures it.
- **4067 against 4051**: at the same impedance the 4067 channel shifts
  1.3–1.4× more.
- **The hi/lo asymmetry** in RV4's settle (§4).
- **The G3 wander**, 9–12 counts on the dividers, 6–13 on the pots — the
  2026-09-18 finding, unchanged.

## 11. What it means for the design

**Step 5b is closed, on real pots, with these constants** (true settle, the
largest value seen, at mid travel where the source impedance peaks):

| | 4067 | 4051 |
|---|---:|---:|
| 10 k pot | **≤ 2.8 µs** (RV2) | **≤ 2.0 µs** (RV6) |
| 20 k pot | **≤ 4.8 µs** (RV4) | not measured |

The model times 1.3–1.5 is **not** a safe settle constant for 10 k pots; the
measured values above are. `docs/hardware/io-budget.md` has not been updated
with them — that is the scan-budget pass, which this round does not do.
*(Done the same day: [`scan-budget.md`](scan-budget.md).)*

**The long rung is licensed for pots** as round three licensed it for
dividers: flat within 3 counts at every wait. One discard is not: it leaves
23–55 counts on a pot at saturation, up to 3.4 LSB.

**Without either fix, a scan that reads each pot once per ~2 ms audio block
reads a 20 k pot at mid travel about 650 counts low** (arm A, 2 ms) — 41 LSB
of 12 bit, 1.0 % of full scale, on every read and on the same side. A 10 k pot
on the 4067 loses about 465 counts, on the 4051 about 325.

What this does **not** license, unchanged from `wait-measured.md` §9: a
verdict on libDaisy's free-running DMA as it stands, and the cost of a
387.5-cycle rung (≈ 63 µs of acquisition per conversion) in a real scan.

## 12. How to repeat it

The captures are committed:
[`captures/pot-settle-capture-618427c.txt`](captures/pot-settle-capture-618427c.txt)
(complete blocks at lines 708, 1545, 2382) and
[`captures/pot-wait-capture-e20b8fd.txt`](captures/pot-wait-capture-e20b8fd.txt)
(complete blocks at lines 412, 855, 1298). `read_settle_guard` and
`read_wait_guard` parse the first complete block of each and pin the pot
readings, `block_ms` and G9.

On the board: spec §10 — set the pots on the bring-up scan to `REF_A`'s
reading there, then flash
`SHELL_COUPON_PROBE=1 SHELL_SETTLE_PROBE=1 SHELL_POT_ROUND=1` and
`SHELL_COUPON_PROBE=1 SHELL_WAIT_PROBE=1 SHELL_POT_ROUND=1` in turn, capture
two or more complete blocks of each, and join them with
`python shell/read_pots.py settle.csv.meta.csv wait.csv`. It exits 1 on this
board because of G3 (§2) and prints its tables regardless.
