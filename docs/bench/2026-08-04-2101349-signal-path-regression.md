# Bench evidence 2026-08-04 — what the signal-path round cost (`2101349`)

Seventeen commits landed under `engine/` between `19f7560` and today, all of
them in the per-sample path the decision gate executes, and none of them
measured. This round measures them, against a baseline built for the purpose,
on the real Daisy Seed (STM32H750) on this desk.

The answer, first: **the gate fits.** `instrument_worst_bbd_dtcm` reads
**96.43 % `pct_max`** offline and **96.69 %** in the real audio callback,
against the 960,000-cycle block budget — **3.57 points of margin offline**.
The seventeen commits are visible in the rows they touched, and three rows
moved in a direction this round did not predict and did not measure a cause
for. Both statements are below, and the second is left as a fact.

`pct_max`, not `pct_avg`, is the decision value. Both are reported; `pct_max`
is bold everywhere.

## What was measured

Two hardware cycles in one session, differing in `engine/` and in nothing
else.

| # | tree | branch | `engine/` |
|---|---|---|---|
| 1 (baseline) | `6134b4f` | `bench/baseline-19f7560` | at `19f7560` |
| 2 (main) | `bd01608` | ancestor of `main` HEAD | today's |

The baseline is a **constructed** commit, not a checkout of `19f7560`: today's
`bench/` with `engine/` replaced by `19f7560`'s. That construction is
available only because no commit between `19f7560` and HEAD touched `bench/`,
so today's bench code has by definition already compiled against the old
engine. `git log --oneline 19f7560..main -- engine/` returns exactly **17**
commits. Both trees were committed before measuring; the bench refuses
evidence from a dirty tree and that refusal is load-bearing here.

Both cycles: profile `regress` (families `system` and `bbd` in one image),
execution layout `axi`, optimization `o3` (`-O3`), 480 MHz core, 96-sample
blocks, D-cache + I-cache, 960,000-cycle block budget, `--repeat 2`. The
effective Make identity was `BENCH_FAMILIES="system bbd" BENCH_ITCM_HOT=0
BENCH_OPTIMIZATION=o3`.

Each cycle ran the full `bench/README.md` sequence, unshortened, after
`rm -rf bench/build`:

```text
python run.py --profile regress --optimization o3 --build-only
python run.py --profile regress --optimization o3 --no-build --program-qspi --build-only
python run.py --profile regress --optimization o3 --repeat 2
```

All four runs (two cycles × two repeats) reported QSPI payload SHA-256
`ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`
and device fingerprint
`1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.
The raw MCU UID is not reproduced here; it stays in
`bench/build/qspi-verified.json`, where the runner's identity check reads it.

All 24 row checksums are identical across both repeats within each cycle. The
accepted captures are `2026-08-04-6134b4f-regress-axi-o3.md` / `.csv`
(baseline) and `2026-08-04-bd01608-regress-axi-o3.md` / `.csv` (main), both in
this directory. Every figure below comes from those two captures.

The round was planned as four cycles — two trees × two execution layouts — and
ran as two, both `axi`. Why the `itcm-hot` half did not run is itself a result
of the round and has its own section below.

## Row real, not stale

The bench build can silently relink a stale object and still print a plausible
figure for code that was never linked, so row presence is checked in the
linked image. `arm-none-eabi-strings build/bench.elf | grep -c '^NAME$'`
returns:

| row name | exact ELF string count |
|---|---:|
| `instrument_worst_bbd_dtcm` | 1 |
| `instrument_worst_bbd` | 1 |
| `inst_bbd_engine_worst` | 1 |
| `bbd_line_stage_walk` | 1 |

Each row name appears exactly once: no omission, no duplication.

**`bench.map` is the wrong artifact for this question.** The map lists
*symbol* names — the mangled and demangled identifiers of the setup functions,
e.g. `bench::(anonymous namespace)::setup_inst_worst_bbd_dtcm()` — while a row
*name* is a runtime data literal that bears no required relation to its setup
function's symbol. Grepping the map for a row name therefore matches only by
luck. The cross-check makes that concrete:

| row name | occurrences in `bench.map` |
|---|---:|
| `instrument_worst_bbd_dtcm` | 0 |
| `instrument_worst_bbd` | 0 |
| `inst_bbd_engine_worst` | 3 |
| `bbd_line_stage_walk` | 2 |

Two of the four matched, two returned zero, and neither outcome reflects row
presence — all four rows are linked and all four ran. The zeros are the check
being wrong, not the rows being missing; the non-zeros are substring
coincidence between the row text and a setup symbol's name. The map-grep habit
is documented elsewhere in this repo and this is the correction to it.

## The gate, both trees

`instrument_worst_bbd_dtcm` — the full instrument, both decks on the BBD part
engine, shortest division, clock ceiling, maximum COLOR/feedback/mix, STEP
freeze engaged, live inputs, `Instrument` state retained in DTCM.

| tree | run 1 `pct_avg` / **`pct_max`** | run 2 `pct_avg` / **`pct_max`** | checksum |
|---|---:|---:|---|
| baseline `6134b4f` | 88.24 / **92.00** | 88.24 / **92.25** | `96128080` |
| main `bd01608` | 91.98 / **96.30** | 91.99 / **96.43** | `6d20538d` |

Real-callback (`CpuLoadMeter`) maxima for the same row: baseline **92.44 %**
and **92.55 %**; main **96.59 %** and **96.69 %**.

**Verdict: it fits.** On `main`, the worst observed result is **96.43 %
offline / 96.69 % in the real callback**, both below 100 % of the block
budget, leaving **3.57 points of margin offline**.

The last hardware reading of this row before today was **97.91 % `pct_max`**
offline / 98.02 % callback, in
`docs/bench/2026-08-01-19f7560-flux-tape.md` — a different identity: profile
`sweep`, families `system sweep`, and **`-O2`**. That figure is recorded here
for orientation only and is deliberately **not** placed in a table with this
round's numbers and **not** subtracted from them. A cross-build,
cross-identity subtraction is not a measurement; that premise is the reason
this round constructed a baseline instead of reusing an old capture.

## The A/B delta per row

Baseline `axi` against `main` `axi`. Same profile, same layout, same
optimization, same desk, same session, same probe, and bench code that is
bit-identical between the two trees — `engine/` is the only difference. Each
figure is the worse `pct_max` of that tree's two repeats.

| row | main | baseline | delta |
|---|---:|---:|---:|
| `instrument_worst_bbd_dtcm` (gate) | **96.43** | **92.25** | **+4.18** |
| `instrument_worst_bbd` | **96.83** | **93.05** | **+3.78** |
| `inst_bbd_engine_worst` | **96.91** | **92.91** | **+4.00** |
| `instrument_worst` | **107.23** | **108.05** | **−0.82** |
| `inst_worst_deck_bus` | **82.23** | **84.61** | **−2.38** |
| `bbd_line_tap` | **3.44** | **2.95** | **+0.49** |
| `bbd_line_tap_half` | **2.51** | **2.17** | **+0.34** |
| `bbd_line_only` | **3.57** | **3.10** | **+0.47** |
| `bbd_ceiling` | **5.54** | **5.02** | **+0.52** |
| `bbd_walk_sdram` | **0.40** | **0.36** | **+0.04** |
| `bbd_line_stage_walk` | **3.72** | **2.87** | **+0.85** |
| `synth_4_voices` | **17.37** | **17.36** | **+0.01** |
| `fx_flux_sdram` | **11.27** | **11.00** | **+0.27** |
| `fx_comp` | **3.03** | **3.02** | **+0.01** |
| `fx_grit` | **4.77** | **4.97** | **−0.20** |
| `oliverb_solo_sram` | **10.04** | **9.60** | **+0.44** |

Within-cycle repeat noise runs **~0.01–0.24 points** depending on the row, so
a movement has to clear that row's own band before it means anything.

**Where the seventeen commits are visible.** Every BBD-line row moved up. The
three full-instrument BBD-engine rows moved up by roughly an order of
magnitude more than the line rows did. `oliverb_solo_sram` moved up
materially (+0.44). That is the shape the round expected to find, and it found
it.

**Where they are not.** `synth_4_voices` (+0.01), `fx_comp` (+0.01) and
`bbd_walk_sdram` (+0.04) are flat, which is what parts the round did not touch
should do. `fx_flux_sdram` (+0.27) sits at the edge of its own noise band and
this table does not resolve it either way.

**Three rows moved down, and this round did not measure why.**
`instrument_worst` moved **−0.82** and `inst_worst_deck_bus` moved **−2.38** —
both materially, both in the opposite direction to everything the seventeen
commits were expected to do. `fx_grit` moved **−0.20**: small, but roughly ten
times that row's own repeat noise, so it is reproducible rather than jitter,
and grit is not among the subsystems the seventeen commits touched. **These
three are facts without a measured mechanism.** No cause is named for any of
them here, because none was measured. They are open, and they are the kind of
thing an attribution round would take up.

**Component rows do not sum.** No figure in this table may be subtracted from
another to derive a component's cost. This repo's own recorded example is a
set of component rows summing to ~120 % of budget against a measured ~159 % —
a 39-point gap with no named owner. The rows above price what each row runs,
and nothing else.

## The crossfade price

`bbd_line_stage_walk` drives `SetStages` between two values across the
measured block, so that the stage transition is active for the large majority
of it rather than diluted into a block average.

**Same build, on `main`:** `bbd_line_stage_walk` reads **3.72** against
`bbd_line_tap`'s **3.44**. That gap is **not pure crossfade** — the walk row
also carries its own per-sample phase bookkeeping, which `bbd_line_tap` does
not run, and this session did not separate the two. And the scale is wrong for
production in a direction that flatters the number: this row's ring is **2,048
cells / 8 KB with its two taps 4 KB apart**, while a production `BbdEngine`
line is **8,192 cells / 32 KB with taps 16 KB apart**. Any crossfade figure
taken from this row is a **cache-friendly lower bound**, not the production
cost.

**Across the A/B:** `bbd_line_stage_walk` moved **+0.85**, from **2.87** on
the baseline to **3.72** on `main`. That delta **does not price the same thing
on both trees.** `a183852` postdates `19f7560`, so the baseline tree contains
no crossfade code at all; there the row prices the **old hard-cut
`SetStages`**, which shortens the ring to 1,024 cells for half of every block
and therefore measures *better cache locality*, not less work. That is why the
baseline row (2.87) sits *below* its own tree's `bbd_line_tap` (2.95). The
+0.85 is a comparison of two designs — click versus crossfade — and not an
incremental cost added to a fixed design. The same 8 KB / 32 KB scale caveat
applies to it.

## The write-ring observation

`a183852` also changed where the write index wraps: every delay now walks the
full `max_cells_` span rather than a span sized to the current delay.

What the two tap rows did across the A/B: `bbd_line_tap` moved **+0.49**
(2.95 → 3.44) and `bbd_line_tap_half` moved **+0.34** (2.17 → 2.51). Both are
well clear of their own repeat noise. That is the observation. The two rows
stand at two different clock rates and both moved up; this round did not
isolate the write ring from anything else the seventeen commits changed inside
`BbdLine::Process`, so **no mechanism is named for the movement**.

One thing the reader of `bench/workloads_bbd.cpp` should carry: that file's
`kWalkCells = 4096` now proxies a **real written span of 8,192**. The proxy
was sized to a state that no longer exists, so `bbd_walk_sdram` (+0.04, flat)
understates the span the production line actually walks.

## The ITCM finding

This is a result of the round in its own right, and it needed no hardware.

The matrix called for four cycles in two execution layouts. Both `itcm-hot`
cells failed at build time, so two cycles ran, both `axi`. The diagnostic that
established why is reproduced here.

| build | `.itcm_audio_hot` | free below 64 KiB |
|---|---:|---:|
| baseline, `-O2` | 46,784 B | 18,496 B |
| main, `-O2` | 48,288 B | 16,992 B |
| baseline, `-O3` | 65,248 B | 32 B |
| main, `-O3` | 66,112 B | **−832 B (link fails)** |

The seventeen commits grow the hot section by **+864 B at `-O3`** and
**+1,504 B at `-O2`**. At `-O2` the headroom absorbs that comfortably. At
`-O3`, **the baseline already stood at 32 bytes free** — `-O3` alone, with
`engine/` rolled back, had consumed 99.95 % of the 64 KiB region — so the
growth spends the last sliver and `main` **overflows by 832 bytes**. The
failure is the compounding of the two: **neither factor alone explains it.**
`main` at `-O3 --itcm-hot` does not link; the linker script's
`ASSERT(... <= 0x10000, "ITCM audio hotset exceeds the 64 KiB ITCM region")`
fires and no `bench.elf` is produced.

There is a **third, independent failure** underneath that one. At `-O2` the
link succeeds on both trees, and `bench/itcm_placement.py` still fails.
`spky::BbdLine::Process` is emitted per translation unit as a weak/COMDAT
symbol; under `regress` the linker keeps the copy in
**`build/workloads_bbd.o`** — a bench-harness TU — and discards the engine
objects' copies, and `bench/itcm_hot.lds` does not list `workloads_bbd.o`. The
surviving symbol therefore lands outside ITCM (`0x24004434` on `main`,
`0x24004374` on the baseline) and the fail-closed placement inspector rejects
the image. `regress` is the first `itcm-hot` image ever to compile the `bbd`
family, which is why this has not been seen before. Adding `workloads_bbd.o`
to the hotset would place bench-harness code in ITCM and **distort every
measurement ever taken there**, so it was **not** done.

**What this means for M6: the intended ITCM placement does not currently fit
the optimization level that ships.** `-O3` is what the production root
makefile builds; the hotset as currently defined does not link at `-O3` on
`main`, and at `-O2` it does not place the BBD kernel where it is supposed to
go. Both are M6 problems, and neither is solved by this round.

## What this does not show

- **No within-FX attribution.** Nothing here separates reverb bloom from the
  dry-bus duck from the DRIVE shaper from the COMP ceiling. No row isolates
  any of them today and this round did not build one. The gate holds, so the
  attribution round the spec anticipated is not forced; if it is run, it needs
  its own spec.
- **The three downward movements are unexplained.** `instrument_worst`
  (−0.82), `inst_worst_deck_bus` (−2.38) and `fx_grit` (−0.20) are stated, not
  accounted for. Naming a mechanism for them without measuring one would be
  the specific defect this repo has recorded against itself before.
- **`fx_flux_sdram` (+0.27) is unresolved** at this table's precision.
- **The layout axis was dropped, and that is the round's result, not a gap in
  it.** `axi` is what an M6 firmware would get today, since there is no ITCM
  loader yet; `itcm-hot` is the intended placement and it does not build. The
  `itcm-hot` half of the matrix is therefore absent by measurement, not by
  omission, and nothing here says what the gate would read under the intended
  placement.
- **No comparison is made to `-O2` sessions.** The 2026-08-01 reading of the
  gate (97.91 % `pct_max`, profile `sweep`, `-O2`, `19f7560`) is a different
  build identity and is not subtracted from anything here.
- **Component rows do not sum**, so no component cost is derived from the
  per-row table.
- **`instrument_worst` remains over budget offline** in both trees (107.23 on
  `main`, 108.05 on the baseline). That row is intentionally over budget and
  is not the decision gate; its anchor segment is underrun garbage on purpose.
- **The other families were not re-priced.** `voice`, `mem`, `mod`, `abl`,
  `body` and `sampler` keep their last figures and the dates those were taken.
