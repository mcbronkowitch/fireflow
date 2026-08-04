# Bench evidence 2026-08-04 — what the signal-path round cost (`2101349`)

Seventeen commits landed under `engine/` between `19f7560` and today, none of
them measured. This round measures them, against a baseline built for the
purpose, on the real Daisy Seed (STM32H750) on this desk.

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
`bench/` with `engine/` replaced by `19f7560`'s.

The premise that makes that construction available is stated over the commit
this round **started from**, not over HEAD. `git log --oneline
19f7560..c54f190 -- bench/` returns **0** commits: nothing under `bench/`
changed between the old engine's commit and this round's starting point. Over
HEAD the same query returns **two** — `d2a57cd` (the `regress` profile) and
`d975039` (the `bbd_line_stage_walk` row) — and both are this round's own work.
That is why the plan's premise check was amended to name `c54f190` rather than
HEAD (`e7d80d8`, "the premise check names the round's own start, not HEAD").

What licenses swapping `engine/` under a fixed `bench/` is therefore **not**
"this bench code has already compiled against the old engine". It has not:
`bbd_line_stage_walk` was written after `a183852` landed and had never been
compiled against `engine/`@`19f7560` until this round did it. What licenses it
is that **the engine API the bench calls is unchanged across the seventeen
commits** — demonstrated rather than assumed, since the identical `bench/`
sources compiled, linked and ran against both `engine/` trees in this session
with no bench-side edit between the two cycles. That the *behaviour* behind one
of those unchanged entry points differs between the trees is a separate matter
— and it is exactly what the "does not price the same thing on both trees"
caveat under "The crossfade price" exists to state.

`git log --oneline 19f7560..main -- engine/` returns exactly **17** commits.
Both trees were committed before measuring; the bench refuses evidence from a
dirty tree and that refusal is load-bearing here.

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

**Which image that was taken on matters, so it is stated.** Those counts come
from a `bench/baseline-19f7560`, `-O2`, `--itcm-hot` link — the last
successfully linked image available when the check was run — not from either of
the two `axi`/`-O3` images the measurements came from. What establishes that
the rows are real in *those* images is the runner's own gate, which both
captures record as applied and passed: "row set matches the profile exactly (no
missing, no extra rows)" and "no duplicate rows", with all 24 rows present in
all four offline tables. The string count is corroboration of the mechanism,
not the primary evidence for the measured images.

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

Two of the four matched and two returned zero, and neither outcome reflects row
presence: all four rows ran in both cycles. The zeros are the check being
wrong, not the rows being missing; the non-zeros are substring coincidence
between the row text and a setup symbol's name. The map-grep habit is
documented elsewhere in this repo and this is the correction to it.

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
optimization, same desk, same session, same probe. `git diff 6134b4f bd01608
-- bench/` is **empty**: the two images were built from byte-identical bench
sources. The only difference that reaches the binary is `engine/` — 11 files —
and the only other difference between the two trees at all is under `docs/`.
Each figure is the worse `pct_max` of that tree's two repeats, and the
`pct_avg` printed beside it is the one from that same repeat.

**All 24 rows in the profile are listed.** A partial table would hide the
checksum-identical pattern reported further down, which is only visible across
the whole set. The rows are ordered by interest, not by execution order; the
profile's execution order is `system` then `bbd`, as the captures record.

| row | main `pct_avg` / **`pct_max`** | baseline `pct_avg` / **`pct_max`** | **`pct_max`** delta | own repeat band |
|---|---:|---:|---:|---:|
| `instrument_worst_bbd_dtcm` (gate) | 91.99 / **96.43** | 88.24 / **92.25** | **+4.18** | 0.25 |
| `instrument_worst_bbd` | 92.65 / **96.83** | 88.90 / **93.05** | **+3.78** | 0.17 |
| `inst_bbd_engine_worst` | 92.65 / **96.91** | 88.89 / **92.91** | **+4.00** | 0.19 |
| `instrument_worst` | 101.32 / **107.23** | 102.38 / **108.05** | **−0.82** | 0.58 |
| `inst_worst_deck_bus` | 77.03 / **82.23** | 79.17 / **84.61** | **−2.38** | 0.20 |
| `instrument_init` | 66.17 / **77.38** | 64.73 / **74.94** | **+2.44** | 0.16 |
| `bbd_line_tap` | 3.40 / **3.44** | 2.94 / **2.95** | **+0.49** | 0.01 |
| `bbd_line_tap_half` | 2.50 / **2.51** | 2.16 / **2.17** | **+0.34** | 0.00 |
| `bbd_line_only` | 3.50 / **3.57** | 3.04 / **3.10** | **+0.47** | 0.02 |
| `bbd_ceiling` | 5.37 / **5.54** | 4.86 / **5.02** | **+0.52** | 0.01 |
| `bbd_walk_sdram` | 0.09 / **0.40** | 0.10 / **0.36** | **+0.04** | 0.04 |
| `bbd_line_stage_walk` | 3.71 / **3.72** | 2.87 / **2.87** | **+0.85** | 0.00 |
| `synth_4_voices` | 17.07 / **17.37** | 17.07 / **17.36** | **+0.01** | 0.01 |
| `fx_flux_sdram` | 10.67 / **11.27** | 10.24 / **11.00** | **+0.27** | 0.24 |
| `fx_comp` | 3.02 / **3.03** | 3.01 / **3.02** | **+0.01** | 0.00 |
| `fx_grit` | 4.75 / **4.77** | 4.92 / **4.97** | **−0.20** | 0.02 |
| `oliverb_solo_sram` | 9.94 / **10.04** | 9.49 / **9.60** | **+0.44** | 0.01 |
| `mod_plane_2x_center` | 6.95 / **7.16** | 6.88 / **7.12** | **+0.04** | 0.02 |
| `synth_1_voice` | 5.41 / **5.52** | 5.40 / **5.50** | **+0.02** | 0.01 |
| `synth_2_voices` | 9.46 / **9.62** | 9.46 / **9.60** | **+0.02** | 0.01 |
| `synth_2x4` | 34.26 / **34.81** | 34.23 / **34.79** | **+0.02** | 0.00 |
| `wave_2x4` | 29.79 / **30.63** | 29.76 / **30.09** | **+0.54** | 0.45 |
| `fx_none` | 2.25 / **2.25** | 2.25 / **2.25** | **0.00** | 0.00 |
| `empty_callback` | 0.00 / **0.00** | 0.00 / **0.00** | **0.00** | 0.00 |

The delta column is `pct_max` only. `pct_avg` is reported per tree and is not
subtracted here; no `pct_avg` delta is claimed.

**`instrument_init` is the second-largest absolute mover in the table.** It
went **74.94 → 77.38 = +2.44 against a 0.16 band**, about fifteen times its
own repeat noise, and its checksum changed with it (`96eeadec` → `8e9a0dbf`).
It is one of the eleven rows the seventeen commits reach, so the movement is
where the round would expect to find one. What the row runs is the **init
patch — "the typical load"** (`bench/workloads_system.cpp`), the same
`process()` loop as the worst-case rows at the default configuration rather
than at the ceiling, which is why it is worth reading beside them. **No
mechanism is measured for the +2.44**; it is stated, like the others, and left
open to an attribution round.

**"Own repeat band" is the larger of that row's two within-cycle `pct_max`
spreads**, computed per row from the two committed CSVs rather than quoted as
a single global figure — because there is no single global figure. Across all
24 rows in the profile the band ranges from **0.00 to 0.58 points**, and the
largest belongs to `instrument_worst` itself (baseline, 108.05 → 107.47);
`wave_2x4` is next at 0.45 (main, 30.18 → 30.63). Most rows sit at or below
0.04. A movement means nothing until it clears *that row's* band, and the
bands differ by more than an order of magnitude between rows.

**Where the seventeen commits are visible.** Every BBD-line row moved up. The
three full-instrument BBD-engine rows moved up by roughly an order of
magnitude more than the line rows did. `oliverb_solo_sram` moved up
materially (+0.44). That is the shape the round expected to find, and it found
it.

**Where they are not.** `synth_4_voices` (+0.01 against a 0.01 band) and
`bbd_walk_sdram` (+0.04 against a 0.04 band) are flat, and neither is touched
by the seventeen commits. `fx_flux_sdram` (+0.27 against a 0.24 band) sits at
the edge of its own band and this table does not resolve it either way.

**`fx_comp` is not an untouched row, and it is the more interesting for it.**
`653f49a` ("env ceiling fades in with the knob; disengage glides out") edits
`engine/fx/comp.cpp` and is one of the seventeen. The row moved **+0.01**
against a **0.00** band — and its checksum is **`47a4392b` on both trees**, so
in this row's configuration the changed code produced bit-identical output. It
is not evidence that the round missed `Comp`; it is evidence that `Comp`'s
change does not reach this operating point.

**Three rows moved down, and this round did not measure why.**
`inst_worst_deck_bus` moved **−2.38** against a **0.20** band, roughly twelve
times its own repeat noise. `fx_grit` moved **−0.20** against a **0.02** band,
about ten times its own — small, but reproducible rather than jitter, and grit
is not among the subsystems the seventeen commits touched. `instrument_worst`
moved **−0.82**, and that one carries the weakest claim of the three: its band
is **0.58**, the widest in the profile, so the movement is only about 1.4× its
own repeat noise and this table does not establish it as more than that. **All
three are facts without a measured mechanism.** No cause is named for any of
them here, because none was measured. They are open, and they are the kind of
thing an attribution round would take up.

**Nine rows move at bit-identical checksums, and that is a pattern, not two
curiosities.** Eleven of the 24 rows change checksum between the trees, and
they are the eleven the seventeen commits reach: the three BBD-engine gate
rows, `instrument_worst`, `inst_worst_deck_bus`, `instrument_init`,
`oliverb_solo_sram`, and the four `bbd_line_*` rows. The other **thirteen are
checksum-identical across the trees** — they compute exactly the same output
on both — and **nine of the thirteen still exceed their own repeat band**:

| row (checksum identical on both trees) | checksum | delta | own band |
|---|---|---:|---:|
| `wave_2x4` | `6f28f4ea` | **+0.54** | 0.45 |
| `bbd_ceiling` | `7f70a86d` | **+0.52** | 0.01 |
| `fx_flux_sdram` | `9ca91007` | **+0.27** | 0.24 |
| `fx_grit` | `74f9b9f5` | **−0.20** | 0.02 |
| `mod_plane_2x_center` | `61d42d20` | **+0.04** | 0.02 |
| `synth_1_voice` | `1816acc1` | **+0.02** | 0.01 |
| `synth_2_voices` | `4dc805b7` | **+0.02** | 0.01 |
| `synth_2x4` | `0d15b5eb` | **+0.02** | 0.00 |
| `fx_comp` | `47a4392b` | **+0.01** | 0.00 |

The remaining four checksum-identical rows do **not** clear their bands:
`synth_4_voices` (+0.01 against 0.01), `bbd_walk_sdram` (+0.04 against 0.04),
and `fx_none` and `empty_callback`, both flat at **0.00**.

**Twelve of the thirteen move non-negatively** — ten strictly up, two exactly
flat — and only `fx_grit` moves down. Five of the nine band-clearing
movements sit at **0.01 to 0.04 points**. Of the four larger ones, `wave_2x4`
and `fx_flux_sdram` clear their own (wide) bands by only 0.09 and 0.03, while
`bbd_ceiling` (+0.52 against 0.01) and `fx_grit` (−0.20 against 0.02) clear
theirs by more than an order of magnitude and are the real tail.

Seen whole, `bbd_ceiling` is the large end of a distribution rather than an
isolated oddity, and the same reframing is what makes the headline safe: this
offset is hundredths to half a point across code that did not change, while
the gate moved **4.18**.

**No mechanism is asserted for any of it**, because none was measured. What
*would* settle the obvious hypothesis costs **no hardware**: both trees still
build, `--build-only` produces a `bench/build/bench.map` for each, and
comparing the link addresses and section placement of these rows' setup and
process symbols across the two maps would test whether code layout moved
under bit-identical code. It is a desk experiment, offered as what would
settle it — **not** as the cause. (Note the constraint from "Row real, not
stale": the map answers questions about *symbols*, which is exactly what this
experiment asks, and not about row *names*, which is what it cannot answer.)

**Component rows do not sum.** No figure in this table may be subtracted from
another to derive a component's cost. This repo's own recorded example is a
set of component rows summing to ~120 % of budget against a measured ~159 % —
a 39-point gap with no named owner. The rows above price what each row runs,
and nothing else.

**That rule and the spec's §5 are in direct conflict, and the rule wins.**
The design spec instructed: "Read against `bbd_line_tap`, same build, same
session: the difference **is** the crossfade price" — which is precisely the
row-minus-row subtraction the paragraph above forbids. This document does not
perform it. "The crossfade price" below prints **3.72** and **3.44** side by
side and names what sits between them instead of publishing a difference, and
that is deliberate, not an oversight in the presentation. The spec has been
annotated accordingly.

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
`SetStages`**, which resets the ring index and leaves the row walking 1,024
cells for half of every block instead of 2,048. Two things are then true and
worth keeping apart. The ring sizes are a source fact. The ordering is
measured: the baseline row (**2.87**) sits *below* its own tree's
`bbd_line_tap` (**2.95**) — a row doing an *extra* thing costs *less*. **No
cache measurement was taken in this round**, so the shorter walk is a stated
difference in what the two trees' code does, not a measured explanation of the
ordering. What follows regardless is that the +0.85 compares two designs —
click against crossfade — rather than pricing an increment added to a fixed
one. The same 8 KB / 32 KB scale caveat applies to it.

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
was sized to a state that no longer exists, so `bbd_walk_sdram` (+0.04, which
is its own repeat band exactly) understates the span the production line
actually walks.

## The ITCM finding

This is a result of the round in its own right, and it needed no hardware.

The matrix called for four cycles in two execution layouts. Neither `itcm-hot`
cell produced a usable image, so two cycles ran, both `axi` — but the two cells
failed by **different** mechanisms, and the difference matters to whoever
retries this.

- **`main`, `-O3 --itcm-hot`: the link fails.** The linker script's
  `ASSERT(ADDR(.itcm_audio_hot) + SIZEOF(.itcm_audio_hot) <= 0x10000, "ITCM
  audio hotset exceeds the 64 KiB ITCM region")` fires and no `bench.elf` is
  produced.
- **baseline, `-O3 --itcm-hot`: the link succeeds and placement fails.**
  `bench.elf` is produced, the `ASSERT` does not fire — the table below shows
  why, 32 bytes to spare — and `bench/itcm_placement.py` then rejects the image
  with `ERROR: representative ITCM symbol missing: spky::BbdLine::Process(`.
  The symbol is absent from the ELF entirely: at `-O3` on that tree it is
  inlined away, with `spky::BbdEngine::process` at `0x00005ed8` inside the hot
  section as the natural landing site for its body.

The diagnostic that established both is reproduced here.

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

There is a **third, independent failure** underneath that one. At `-O2` the
link succeeds on both trees, and `bench/itcm_placement.py` still fails.
`spky::BbdLine::Process` is emitted per translation unit as a weak/COMDAT
symbol; under `regress` the linker keeps the copy in
**`build/workloads_bbd.o`** — a bench-harness TU — and discards the engine
objects' copies, and `bench/itcm_hot.lds` does not list `workloads_bbd.o`. The
surviving symbol therefore lands outside ITCM (`0x24004434` on `main`,
`0x24004374` on the baseline) and the fail-closed placement inspector rejects
the image. `regress` is the first `itcm-hot` image ever to compile the `bbd`
family, which makes it a candidate for why this has not been seen before — but
so does independent regression since the last known-good `-O2 --itcm-hot`
session on 2026-08-01, and **this round did not separate the two**. Adding
`workloads_bbd.o` to the hotset would place bench-harness code in ITCM and
**distort every measurement ever taken there**, so it was **not** done.

**What this means for M6: the intended ITCM placement does not currently fit
the optimization level that ships.** `-O3` is what the production root
makefile builds; the hotset as currently defined does not link at `-O3` on
`main`, and at `-O2` it does not place the BBD kernel where it is supposed to
go. Both are M6 problems, and neither is solved by this round.

**All four builds in the table above are `regress` (`system` + `bbd`)
images.** The `system`-only `--itcm-hot` image that last passed, on 2026-07-30,
carried a `0xd8e0` = 55,520-byte hot section; it was **not rebuilt at today's
`main`**, so nothing here says whether that image still links or still places.
What the table does say is that the hotset definition currently in the tree
fails on the profile that puts the BBD kernel and the gate rows in one binary —
which is the image M6 would need.

## What this does not show

- **The round's own question 3 is not answered — neither half of it.** The
  design spec's §1.1 asked "what does the BBD crossfade cost, and what does
  the full-span write ring cost?". A reader who checks §1.1 against the
  section headings above ("The crossfade price", "The write-ring
  observation") would conclude both were priced. They were not.
  - *The crossfade.* Two candidate figures exist and this document retracts
    both. The same-build gap (3.72 versus 3.44 on `main`) is **not pure
    crossfade** — it also carries the walk row's own per-sample phase
    bookkeeping, which this session did not separate — and it is a
    **cache-friendly lower bound**, taken on a ring a quarter of a production
    line's. The cross-tree gap (+0.85) **compares two designs**, hard cut
    against crossfade, rather than pricing an increment on a fixed one.
    Neither is "the crossfade price", and no third figure was taken. What
    would answer it is a same-build, same-object A/B against a row with the
    identical phase bookkeeping and no stage change, on a production-sized
    ring — a row that does not exist yet.
  - *The write ring.* **Nothing in this matrix could have isolated it**, and
    that was true from the moment the matrix was designed, not a failure in
    execution. The baseline lacks the ring change, the crossfade, and
    everything else in `a183852`/`28fda8d` simultaneously, so the A/B cannot
    attribute a movement to one of them; and no row in the profile varies
    written span alone while holding the rest fixed. The two `bbd_line_tap`
    rows moved up together (+0.49, +0.34) and that observation stands, but it
    is an observation about `BbdLine::Process` as a whole.
- **No within-FX attribution.** Nothing here separates reverb bloom from the
  dry-bus duck from the DRIVE shaper from the COMP ceiling. No row isolates
  any of them today and this round did not build one. The gate holds, so the
  attribution round the spec anticipated is not forced; if it is run, it needs
  its own spec.
- **The three downward movements are unexplained.** `inst_worst_deck_bus`
  (−2.38 against a 0.20 band) and `fx_grit` (−0.20 against 0.02) are stated,
  not accounted for. `instrument_worst` (−0.82 against a 0.58 band) is stated
  more weakly still: this round does not establish it as more than about 1.4×
  its own repeat noise. Naming a mechanism for any of them without measuring
  one would be the specific defect this repo has recorded against itself
  before.
- **`fx_flux_sdram` (+0.27 against a 0.24 band) is unresolved** at this
  table's precision.
- **The layout axis was dropped, and that is the round's result, not a gap in
  it.** `axi` is what an M6 firmware would get today, since there is no ITCM
  loader yet; `itcm-hot` is the intended placement, and on the `regress`
  profile it does not produce a usable image on either tree — link failure on
  `main`, placement failure on the baseline. The `itcm-hot` half of the matrix
  is therefore absent by measurement, not by omission, and nothing here says
  what the gate would read under the intended placement.
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
