# Bench evidence 2026-07-31 — pricing the cross-deck audio bus (`20eafed`)

Measured on the real Daisy Seed (STM32H750) on this desk, `bench/` profile
`system`, execution layout `axi`, optimization `o2`. Two full builds
(`--repeat 2` each, `--program-qspi` each), one with `SPKY_DECK_BUS` at its
default of 1 and one rebuilt from a clean tree with
`C_USER_FLAGS=-DSPKY_DECK_BUS=0`. Raw captures: `2026-07-31-20eafed-deck-bus-on.md/.csv`
(bus on) and `2026-07-31-20eafed-deck-bus-off.md/.csv` (bus off), both in this
directory.

## Row real, not stale

`grep -c "inst_worst_deck_bus" bench/build/bench.map` returned `1` for every
build in this document (bus-on and bus-off, both before and after the
mid-session fix described below).

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
`other_deck=true` — Task 4's exact configuration
(`tests/test_deck_bus.cpp`, "sampler <-> sampler mutual routing stays
finite"), already proven finite and bounded over a 10 s run. This makes
`inst_worst_deck_bus` no longer a same-source A/B against `instrument_worst`
within one build (the engine swap dominates that diff) — `instrument_worst`
stays in the profile as an unrelated **control** instead, to separate
build-to-build layout noise from the bus's own cost. The real A/B is
`inst_worst_deck_bus` measured across the two `SPKY_DECK_BUS` builds.

## A stale-object trap, caught before it produced a number

Between builds, `bench`'s Makefile has no header-file dependency trick for
`C_USER_FLAGS` (unlike `BENCH_GIT_HASH`, `BENCH_FAMILIES`, or
`BENCH_ITCM_HOT`, each of which is written into a real header specifically so
Make sees a real dependency edge). A `make` invocation that changes only
`C_USER_FLAGS` — the mechanism used here to flip `SPKY_DECK_BUS` — leaves
every already-up-to-date `.o` alone, so an un-cleaned rebuild silently
relinks stale objects built under the *previous* flag. This bit once during
this session: `python run.py --profile system --program-qspi` (its own
`build()` never passes `C_USER_FLAGS`) was run directly after a
`C_USER_FLAGS=-DSPKY_DECK_BUS=0` build without an intervening `make clean`,
and only `main.o` (forced by the git-hash header) actually rebuilt — the
"bus on" run was silently built from bus-off objects. `SRAM_EXEC` size
(184768 B logged vs. the true bus-on 184848 B after the engine fix, vs. bus-off's
184048 B) caught it before any number was trusted. Every measurement in this
document was taken only after `make clean` immediately preceded its build.

## Results

Two runs (`--repeat 2`) per arm; both rows read `pct_max`, the gate metric
(not `pct_avg`).

| row | bus ON `pct_max` | bus OFF `pct_max` | checksum ON | checksum OFF |
|---|---:|---:|---|---|
| `inst_worst_deck_bus` | 82.21, 82.28 | 76.51, 76.47 | `8eaf4037` | `8b05a866` |
| `instrument_worst` (control) | 105.14, 105.04 | 105.53, 105.59 | `4c4a29ce` | `4c4a29ce` |

`inst_worst_deck_bus`'s checksum differs between the two arms (confirming
the bus is genuinely live, not a no-op that happens to cost cycles);
`instrument_worst`'s checksum is identical in both arms (confirming it never
touches `other_deck`, as designed).

**Bus cost, taking the two `inst_worst_deck_bus` runs per arm:**
mean bus-on 82.245 %, mean bus-off 76.49 % → **≈ 5.75 percentage points**
of the 960,000-cycle block budget (≈ 55,200 cycles per 96-sample block, ≈ 575
cycles/sample), for two SAMPLER decks in full mutual cross-deck routing with
every FX block on (Grit, Flux, Comp, Reverb at `configure_inst_worst`'s
maxima).

**Control drift** (`instrument_worst`, which never touches `other_deck`,
across the same two builds): mean 105.09 % (bus-on build) vs. 105.56 %
(bus-off build) → **≈ 0.47 points** of cross-build layout/timing noise for a
row the feature cannot touch. The bus's ≈ 5.75-point effect is about 12x
that noise floor and reproduces to within 0.11 points across the two repeats
in each arm — a real, resolvable effect, not noise.

## This number is much bigger than the ≈0.04-point ISA hand-count, and the honest reason is not fully pinned down here

Two `fast_tanh` calls per sample (part.h; ~30 cycles each on the M7 per that
file's own comment) times two decks is order ~120 cycles/sample, not ~575.
The measured delta is real (reproducible, checksum-confirmed, well clear of
the control's noise floor) but is larger than a hand-count of the guarded
lines alone would predict — consistent with this task's premise that a
hand-count of this exact kind of loop was previously falsified, here by a
larger margin than the earlier round's 2-4x. The likely mechanism is that
the closed mutual loop raises the signal energy reaching the FX chain
(Grit/Flux/Reverb/Comp, all at their maxima in this row), and at least one
of those stages' cost is amplitude- or code-path-dependent -- but that
mechanism is not measured here, only inferred, and per this project's own
ablation discipline it is not asserted as the cause. Isolating it (e.g. a
`_src_deck`-on/off pair with the FX chain bypassed) is a natural follow-up
and is out of scope for this task.

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
bench image from the last capture in this document, not the shipping
firmware. Reflash the real firmware via the repo-root `Makefile` before
using the instrument to play.
