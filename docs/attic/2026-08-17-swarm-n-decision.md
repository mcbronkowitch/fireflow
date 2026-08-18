# How many partials fit — the SWARM N decision

**Board: Daisy Patch Submodule.** Transport USB, optimization `-O3`, layout
`axi`, profile `swarm` (families `system` + `swarm`), 2 repeats per image,
2026-08-17. Core clock 480 MHz, block size 96, block budget 960 000 cycles,
dcache+icache on.

**No Seed figure entered this decision.** Every number below is from the three
runs named here, all on the submodule.

## The three images

`kPartials` is a compile-time constant, so pricing the bank at several N means
several images, not several rows. A row that instantiated a second bank at a
second N would double the icache footprint and price neither honestly.

| commit | N | `swarm_bank` avg | max | avg % | same-image `instrument_worst` avg % |
|---|---:|---:|---:|---:|---:|
| [`ead66c1`](2026-08-17-ead66c1-swarm-axi-o3-patch_sm-usb.md) | 16 | 120 652 | 121 190 | 12.56 | 102.02 |
| [`824f14d`](2026-08-17-824f14d-swarm-axi-o3-patch_sm-usb.md) | 32 | 239 182 | 239 313 | 24.91 | 102.04 |
| [`b127c90`](2026-08-17-b127c90-swarm-axi-o3-patch_sm-usb.md) | 64 | 476 095 | 476 197 | 49.59 | 102.03 |

Figures are run 1 of each image; run 2 agrees to within 0.01 % everywhere.

`instrument_worst` holding still across all three images — 102.02, 102.04,
102.03 — is what says the sweep moved the swarm row and not the instrument
around it. That was worth checking rather than assuming: adding a translation
unit shifts small rows by points from icache layout alone.

## Cycles per partial

- 16 → 32: (239 182 − 120 652) / 16 = **7408.1** cycles per partial
- 32 → 64: (476 095 − 239 182) / 32 = **7403.5** cycles per partial

Linear to within 0.06 %. Taking **7405 cycles per partial per 96-sample block**
and a fixed overhead of

- 120 652 − 16 × 7405 = **2172 cycles**

predicts 64 × 7405 + 2172 = 476 092 against 476 095 measured.

Per sample that is **77.1 cycles per partial** — one `fast_sin`, three slope
adds, a truncation wrap and two multiply-accumulates.

The row is also almost perfectly flat between avg and max (239 182 against
239 313 at N = 32, 0.05 % apart), which is what a branch-free loop with no
allocation and no delay line should look like, and is itself evidence that the
loop does what the header claims.

## Why the kernel row alone sizes N too generously

**A first pass read N = 16 off the numbers above, and the whole-engine row then
measured that reading wrong.** The reasoning is kept here rather than deleted,
because the shape of the mistake is the useful part.

## Why N = 16 was derived, and what it missed

**The gate this has to answer to is relative, not absolute.** The plan's
arithmetic asked for "whatever headroom the same image's `instrument_worst`
leaves under 960 000 cycles". In these images `instrument_worst` measures
**102.04 % avg / 107.13 % max** — that headroom is *negative*, and the formula
as written yields N = 0. It is the wrong formula, not a stop signal:
`instrument_worst` is a deliberately pessimistic synthetic worst case that this
project has long accepted sitting over the budget, and the project's actual
go/no-go gate in these same runs, `instrument_worst_bbd_dtcm`, reports
**98.80 % offline / 98.98 % in the real callback — it fits**.

The operative gate for SWARM is the one Task 10 will read:
`inst_swarm_engine_worst` must not exceed the **same image's**
`instrument_worst`. Both rows carry the identical FX shell, so what that gate
really compares is two SWARM decks against two SYNTH decks at four voices. That
makes `synth_2x4` the budget:

```
per-deck budget = synth_2x4 / 2 = 330 290 / 2 = 165 145 cycles
bank(N)         = 2172 + 7405 * N
reserve         =  41 286 cycles   (one quarter of the per-deck budget)
N               = (165 145 - 41 286 - 2172) / 7405 = 16.4  ->  16
```

**The reserve is a judgement, and it is stated as one.** The `swarm_bank` row
prices the *bank*. It does not price the *control tick*, and
`SwarmEngine::_rebuild_targets` spends two `std::pow` per partial per tick plus
an insertion sort over three arrays. Nothing here has measured that; Task 10's
`inst_swarm_engine_worst` row is what will. A quarter of the per-deck budget is
what was set aside for it, and if Task 10 comes in with room to spare, raising
N is a rebuild and a re-measure — not a redesign, because N is a compile-time
constant and nothing in the engine or its gates depends on its value.

16 also divides `kRetargetSlice` (8) exactly, so `kRetargetPeriod` is 2 and no
slice runs short.

## The whole-engine gate — and the answer, N = 14

`inst_swarm_engine_worst` puts both decks on `ENGINE_SWARM` with
`instrument_worst`'s FX shell and modulation plane untouched, so the difference
between the two rows in one image is the difference between two engines.

**The gate:** the swarm row must not exceed the *same image's*
`instrument_worst`, on both `pct_avg` and `pct_max`.

| N | swarm avg | swarm max | `instrument_worst` avg | max | verdict |
|---:|---:|---:|---:|---:|---|
| 16 | 99.75 | **105.81** | 99.48 | 103.45 | **FAIL**, both halves |
| 12 | 91.77 | 97.86 | 100.30 | 104.12 | pass — 8.5 / 6.3 points |
| 14 | 95.67 | 102.03 | 99.73 | 103.83 | **PASS** — 4.1 / 1.8 points |

`patch_sm` / `usb` / `o2` / `axi`, two repeats each; run 1 shown, run 2 agrees
to within 0.35 points everywhere.

**N = 14 is the answer.** Its 1.8-point margin on the maximum is roughly four
times the drift the *unchanged* `instrument_worst` row shows across these three
builds — 99.48, 100.30, 99.73 — which is the icache-layout drift this project's
bench discipline exists to keep out of comparisons, and exactly why every pair
above is read inside one image and never subtracted across two.

12 was measured too, and it is the fallback if that margin ever proves thin.

From N = 12 to N = 14 the swarm row moved 76 583 cycles over eight
partial-instances: **9573 cycles per partial per block at `-O2`**, against 7405
at `-O3`. `-O2` is about 29 % dearer per partial — the cost of losing the
unrolled bank loop.

## The `-O3` run is still owed, and why it could not be made

**The bench image no longer links at `-O3`.** Measured:

```
system profile, o3, patch_sm:  SRAM_EXEC 267388 B of 262880 B — overflow 4508 B
```

That overflow is present **without** `inst_swarm_engine_worst` in the tree; the
new row adds 1304 bytes on top of it. What pushed the image over is the
finished SWARM engine itself: the Task-3 images linked at 260 452 B (99.08 %)
when `SwarmEngine` was still silent, and the map, bloom, stagger and drift have
been added since — with `-funroll-loops` and a compile-time `kPartials`, the
control-rate loops unroll.

**The shipping firmware is unaffected.** `shell/` builds clean at `-O3`
(`shell-sram.bin`, 229 200 B). Only the measuring tool, which carries every
workload row on top of the engine, runs out of `SRAM_EXEC`.

So `-O2` is not a preference here, it is the only level at which both rows of
the gate could be put in one image at all. It is also the pessimistic side to
have measured on, so an `-O3` run should widen the margin rather than close it.

**This needs a decision.** Getting the `-O3` gate back means making the bench
image smaller — dropping rows from the `system` family, or using the 64 KB of
`ITCMRAM` that currently reads 0.00 % — and that is a bench-capacity question,
not an engine one.

## What this does not say

- It says nothing about how a swarm of 14 partials **sounds**. Whether 14 is
  enough to beat rather than to chord is a listening question, and it is open
  (spec §10, the by-ear pass).
- It says nothing about the Seed. These are submodule numbers only.
- It does not open Plan B. The contingency in the plan — recursive sine
  oscillators instead of one `fast_sin` per partial — was reserved for the case
  where cycles per partial left no useful N. A useful N fits and clears the
  whole-engine gate, so Plan B stays closed.
