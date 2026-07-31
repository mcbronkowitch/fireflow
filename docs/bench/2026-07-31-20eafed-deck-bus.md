# Bench evidence 2026-07-31 — pricing the cross-deck audio bus (`20eafed`)

Measured on the real Daisy Seed (STM32H750) on this desk, `bench/` profile
`system`, execution layout `axi`, optimization `o2`. Two full builds
(`--repeat 2` each, `--program-qspi` each), one with `SPKY_DECK_BUS` at its
default of 1 and one rebuilt with it forced to 0. Raw captures:
`2026-07-31-20eafed-deck-bus-on.md/.csv` (bus on) and
`2026-07-31-20eafed-deck-bus-off.md/.csv` (bus off), both in this directory.
The 0-arm was originally built with `C_USER_FLAGS=-DSPKY_DECK_BUS=0` after a
manual `make clean`; a later, durable fix (below) replaced that ad hoc
mechanism with `make BENCH_DECK_BUS=0`, which no longer needs a manual clean.
The numbers in this document were captured under the manual-clean discipline
and are unaffected by the later mechanism change — the object code the two
arms produce is identical either way.

**Revision note (review round 2):** this revision corrects three framing
defects a review found in the first version of this document — the noise
floor, the control's status, and the settle-depth conditioning — without
changing any measured figure. See the three sections below with those
headings. No hardware was re-flashed for this revision; the corrections use
figures already present in the committed CSVs, plus one new desktop-only
(non-hardware) convergence check for the settle-depth question.

## Row real, not stale

`grep -c "inst_worst_deck_bus" bench/build/bench.map` returned `1` for every
build in this document (bus-on and bus-off, both before and after the
mid-session engine fix described below).

## The row had to be fixed mid-session

The brief's literal instruction — copy `setup_inst_worst` and turn on
`other_deck` on both decks, engine left untouched — does not work.
`configure_inst_worst()` never calls `set_engine`, so both decks stay on the
boot default `ENGINE_SYNTH`. `IPartEngine::consumes_input()` defaults to
`false` and `SamplerEngine` is the only override
(`engine/parts/engine_iface.h`, `engine/sampler/sampler_engine.h`).
`Part::process`'s `if (_engine_wants_in)` guard is keyed off that cached
override, and the `_src_deck` branch — and `process_in()` itself — live
*inside* it. On `ENGINE_SYNTH` that branch is not merely false, it is never
reached, regardless of `other_deck` or `SPKY_DECK_BUS`.

This was not a theoretical concern: the first build of the row (SYNTH,
unmodified) produced an *identical* checksum for `inst_worst_deck_bus`
whether `SPKY_DECK_BUS` was 1 or 0 — the row was measuring nothing. (That
first pair of runs also tripped a second, unrelated problem — a stale-object
relink, below — so the "identical checksum" symptom briefly had two possible
causes; both are accounted for.)

Fix: both decks now run `ENGINE_SAMPLER` with the monitor on and
`other_deck=true`, closely modelled on — but not identical to — Task 4's
mutual-routing test (`tests/test_deck_bus.cpp`, "sampler <-> sampler mutual
routing stays finite": same engine pair, same monitor, same `other_deck`).
It differs from Task 4 in `audio_in` (false here, true there), in drive
signal (bench's fixed `test_input()` noise vs Task 4's asymmetric constant
±0.5), and in kind (a hardware bench row here, a desktop doctest there).
Task 4 proved *its own* configuration finite and bounded over 10 s; this
row's safety rests on the same underlying bound (`fast_tanh`'s hard clamp,
`engine/util/fast_tanh.h`), not on Task 4 having separately proven this exact
setup. This does mean `inst_worst_deck_bus` is no longer a same-source A/B
against `instrument_worst` within one build (the engine swap dominates that
diff) — the real A/B is `inst_worst_deck_bus` measured across the two
`SPKY_DECK_BUS` builds, with the instrument-family rows that never touch
`other_deck` serving as controls instead (see "The control" below).

## A stale-object trap, caught before it produced a number — and closed structurally

Between builds, `bench`'s Makefile had no header-file dependency trick for
the ad hoc `C_USER_FLAGS=-DSPKY_DECK_BUS=0` mechanism (unlike
`BENCH_GIT_HASH`, `BENCH_FAMILIES`, or `BENCH_ITCM_HOT`, each of which is
written into a real header specifically so Make sees a real dependency
edge). A `make` invocation that changed only `C_USER_FLAGS` left every
already-up-to-date `.o` alone, so an un-cleaned rebuild silently relinked
stale objects built under the *previous* flag. This bit once during this
session: `python run.py --profile system --program-qspi` (its own `build()`
never passes `C_USER_FLAGS`) was run directly after a
`C_USER_FLAGS=-DSPKY_DECK_BUS=0` build without an intervening `make clean`,
and only `main.o` (forced by the git-hash header) actually rebuilt — the
"bus on" run was silently built from bus-off objects. `SRAM_EXEC` size
(184768 B logged vs. the true bus-on 184848 B after the engine fix, vs.
bus-off's 184048 B) caught it before any number was trusted. Every
measurement in this document was taken only after `make clean` immediately
preceded its build.

Because `Part::process` is `inline` in a header, this was worse than stale
data — a build that mixed objects compiled under different `SPKY_DECK_BUS`
values would link an ODR violation, not merely report a wrong number. The
durable fix: `bench/Makefile` now generates `build/bench_deck_bus.h` via
`bench/write_bench_deck_bus.py` from a new `BENCH_DECK_BUS` variable
(`make BENCH_DECK_BUS=0 build/bench.elf`), declares it a prerequisite of
`$(OBJECTS)` as a whole (the same mechanism `bench_optimization.h` already
uses for `-O2`/`-O3`, since both flags affect essentially every translation
unit, not one), and force-includes it ahead of `part.h`'s own
`#ifndef SPKY_DECK_BUS`. Verified directly: building bus-on from a clean tree
(`SRAM_EXEC` 184848 B), then — **without** `make clean` — rebuilding with
`BENCH_DECK_BUS=0` on top of it recompiled all 31 objects (every compile line
carried `-include build/bench_deck_bus.h`) and linked the correct bus-off
size (184048 B). The ad hoc `-DSPKY_DECK_BUS=0` compile-line form is no
longer part of the documented workflow.

## Results

Two runs (`--repeat 2`) per arm; the bus-cost row reads both `pct_max` (the
gate) and `pct_avg` (cited below to corroborate — see "Results, corroborated").

| row | bus ON `pct_max` | bus OFF `pct_max` | checksum ON | checksum OFF |
|---|---:|---:|---|---|
| `inst_worst_deck_bus` | 82.21, 82.28 | 76.51, 76.47 | `8eaf4037` | `8b05a866` |

`inst_worst_deck_bus`'s checksum differs between the two arms, confirming
the bus is genuinely live (not a no-op that happens to cost cycles).

**Bus cost, taking the two `inst_worst_deck_bus` runs per arm:**
mean bus-on 82.245 %, mean bus-off 76.49 % → **≈ 5.75 percentage points**
of the 960,000-cycle block budget (≈ 55,200 cycles per 96-sample block, ≈ 575
cycles/sample), for two SAMPLER decks in full mutual cross-deck routing with
every FX block on (Grit, Flux, Comp, Reverb at `configure_inst_worst`'s
maxima).

**Results, corroborated:** `pct_avg` moves the same direction and by a
similar amount — 78.72 % (bus on) vs 72.69 % (bus off) → **6.03 points** —
ruling out a single-block `pct_max` tail artifact as the source of the
5.75-point figure.

## The noise floor (corrected)

The first version of this document derived a 0.47-point cross-build floor
from `instrument_worst` alone. `instrument_worst` is not the only available
control, and it is not the widest one. Four instrument-family rows carry a
**bit-identical checksum** across the two arms (proving identical DSP
output — see "The control" below for what that does and does not prove
about the *code*), so each is a valid same-audio comparison of cross-build
drift:

| row (checksum identical both arms) | `pct_max` on | `pct_max` off | drift |
|---|---:|---:|---:|
| `instrument_worst` (`4c4a29ce`) | 105.14, 105.04 | 105.53, 105.59 | 0.39, 0.55 (mean 0.47) |
| `instrument_worst_bbd` (`483e8e82`) | 109.27, 109.27 | 109.69, 109.62 | 0.42, 0.35 (mean 0.39) |
| `instrument_worst_bbd_dtcm` (`483e8e82`) | 108.06, 108.05 | 108.52, 108.49 | 0.46, 0.44 (mean 0.45) |
| `instrument_worst` anchor (real callback) | 105.11, 105.13 | 105.73, 105.83 | 0.62, 0.70 (mean 0.66) |
| `instrument_init` (`2b52554a`) | 71.89, 71.90 | 74.01, 74.03 | **2.12, 2.13 (mean 2.12)** |

`instrument_init`'s `pct_avg` drifts by the same amount (60.94 → 62.96,
2.02 points), so this is not a max-tail artifact either.

**The honest cross-build floor for instrument-family rows is a range up to
≈2.1 points, not a single 0.47-point number.** Against that range, the bus
effect (5.75 points) is **≈2.7× the worst observed drift**, not the ≈12×
this document previously claimed. It is still a real, resolvable effect —
comfortably clear of even the widest control's drift — but the margin is
narrower than first stated.

The two within-arm repeats agreeing to within 0.11 points (`inst_worst_deck_bus`:
82.21/82.28 on, 76.51/76.47 off) is *within-build* repeat variance. It says
the ST-Link capture and the block-budget measurement are themselves stable
run-to-run on a fixed image; it says nothing about — and must not be read as
corroborating — the *cross-build* variance the table above measures. The two
do not support each other.

## The control (corrected)

The first version of this document called `instrument_worst` "a row the
feature cannot touch" and treated its identical checksum as proof of that.
**The checksum proves the audio is unchanged, not that the code is.** The
`set_deck_in` read loop (`instrument.cpp`, around line 117) and the
`_deck_tap` write (around line 164) are unconditional, once per sample, for
**every** instrument row — they do not check which engine is
active or whether any part has `other_deck` on. They are compiled out of the
bus-off arm entirely. So every row in the table above, including all four
controls, carries a real (if small) slice of the feature: exactly the fixed
per-sample overhead the spec's own avoided ≈0.04-point hand-count was
pricing.

The sign of that slice is the useful part. Removing that code (going bus-on
→ bus-off) should, on its own, make a control row slightly *faster* — fewer
instructions executed. Three of the four non-anchor controls above show the
**opposite**: bus-off reads *slower* by 0.35–2.13 points. The fixed overhead
is real, but it is demonstrably **buried under cross-build layout noise** in
this measurement — the noise runs in the wrong direction to isolate it, and
does so consistently across every control. That is arguably the most useful
finding in this capture, separate from the headline bus-cost number: a
single build-vs-build hardware comparison of this kind cannot resolve an
effect the size of a few array reads/writes, which is exactly the situation
the spec avoided guessing its way past.

One further consequence follows directly, and belongs here rather than being
left implicit: since the ambient cross-build bias runs against bus-on on
every instrument-family row measured (0.35–2.13 points, all in the same
direction), and `inst_worst_deck_bus` is itself an instrument-family row,
the same bias plausibly suppresses part of its bus-on − bus-off gap rather
than inflating it. **5.75 points is plausibly an under-estimate of the bus's
true cost**, not an over-estimate, though "plausibly" is as far as this
single A/B can honestly go — pinning it down further would need more than
two builds.

## Settle-depth conditioning (new)

`setup_inst_worst_deck_bus` settles 200 blocks (0.4 s at 48 kHz) before the
measured window opens. Task 4's own harness ran the analogous mutual
sampler loop for 10 s (5000 blocks) to establish its peak, and this row's own
comment already conceded the loop climbs toward a fixed point over "many
blocks" — so the reported figures above are conditioned on that depth, and
this document did not previously say so.

**What was checked, and what it does and does not show:** a desktop-only,
non-hardware convergence check (doctest, not the ARM target; deleted after
use, not committed — same throwaway-scratch convention as Task 4's RED
proofs) built an `Instrument` with this row's exact
`configure_inst_worst()` settings plus `ENGINE_SAMPLER` + monitor +
`other_deck=true` on both decks, drove it with a fixed deterministic signal
(a low-frequency sine plus DC offset — not bench's `test_input()` noise, and
not Task 4's asymmetric constant), and logged `deck_tap` at blocks 50, 100,
200, 500, 1000, 2000, and 5000:

| block | tapA | tapB |
|---:|---:|---:|
| 50 | 0.400002 | 0.400002 |
| 100 | 0.400002 | 0.400002 |
| 200 | 0.400002 | 0.400002 |
| 500 | 0.388279 | 0.388284 |
| 1000 | 0.391573 | 0.391573 |
| 2000 | 0.397996 | 0.397996 |
| 5000 | 0.399670 | 0.399670 |

The tap is not monotonically climbing: it holds flat through block 200, dips
≈3% by block 500, then recovers to within ≈0.008% of the block-200 value by
block 5000. So at this settle depth the *audio* signal driving the suspected
downstream-cost mechanism is not wildly unconverged relative to its
long-run value — but a slower, few-thousand-block transient clearly exists
in between, and this check says nothing about whether the **cycle cost**
tracks that transient the same way the tap magnitude does. It is desktop
evidence about the signal, not hardware evidence about the cycle count.

**Stated plainly, per the review's second acceptable resolution: the
5.75-point (and 6.03-point `pct_avg`) bus-cost figures in this document are
conditioned on a 200-block settle and have not been established as the
steady-state hardware cost.** Confirming that would need a hardware
re-measurement at a substantially deeper settle (matching, or approaching,
Task 4's 5000-block depth) — not performed in this round.

## Bus-cost magnitude vs. the ISA hand-count

Two `fast_tanh` calls per sample (`engine/util/fast_tanh.h`; ~30 cycles each
on the M7 per that file's own comment) times two decks is order
~120 cycles/sample, not the measured ~575. The delta is real (reproducible,
checksum-confirmed, clear of even the widest control's noise floor), larger
than a hand-count of the guarded lines alone would predict, and — per "The
noise floor" above — plausibly an under-estimate rather than an
over-estimate. This is consistent with, and now measured to be
substantially larger than, this task's founding premise that a hand-count of
this exact kind of loop was previously falsified by 2–4×; here the honest
comparison (≈2.7× the widest control's drift, not the ≈12× first claimed)
still clears that floor, just less dramatically than first stated.

The likely mechanism is that the closed mutual loop raises the signal energy
reaching the FX chain (Grit/Flux/Reverb/Comp, all at their maxima in this
row), and at least one of those stages' cost is amplitude- or
code-path-dependent — but that mechanism is not measured here, only
inferred, and per this project's own ablation discipline it is not asserted
as the cause. Isolating it (e.g. a `_src_deck`-on/off pair with the FX chain
bypassed, and/or a deeper-settle hardware re-run per the section above) is a
natural follow-up and is out of scope for this task.

## Gates

Both builds' full `run.py --profile system --program-qspi` invocations
exited 0: row set matched the profile exactly, no duplicates, checksums and
device/QSPI identity matched across each arm's two repeats, and the
`wave_acceptance` gate passed in both. Full ledgers are in the two raw
capture files in this directory.

## Desktop suite

`ctest --test-dir build --output-on-failure` at `SPKY_DECK_BUS` left at its
compiled-in default (1): **845/846** test cases pass. The one failure is the
pre-existing, out-of-scope `test_seed_audition_init.cpp` case (VCV
init-patch parameter defaults, BODY-era drift), unchanged by this branch.

## Housekeeping note for whoever next touches this Seed

The device's QSPI and SRAM currently hold the **bus-off** (`SPKY_DECK_BUS=0`)
bench image from the last hardware capture in this document, not the
shipping firmware. Reflash the real firmware via the repo-root `Makefile`
before using the instrument to play.
