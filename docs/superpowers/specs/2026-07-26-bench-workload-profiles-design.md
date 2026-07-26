# Bench workload profiles

**Date:** 2026-07-26
**Status:** design approved
**Scope:** compile-time selection of which workload families the bench firmware
contains, so a measurement image only carries the rows its question needs. Each
profile declares which acceptance gates apply to it, and every written capture
records which gates ran and which were not applicable. No change to any
workload's measurement semantics, to the cycle counter, or to the shipping
firmware.

## Problem

The bench firmware does not link.

At `main` tip (`abb9e26`, Spotymod 2.13.2), built in a clean worktree with no
branch changes applied:

```
SRAM_EXEC:  275,256 / 262,880 B  (104.71 %)  — over by 12,376 B
SRAM:       284,840 / 261,408 B  (108.96 %)  — over by 23,432 B
```

At `d294556` — the commit behind the last accepted capture,
`docs/bench/2026-07-25-d294556.md` — the same build links:

```
SRAM_EXEC:  262,840 / 262,880 B  (99.98 %)   — 40 bytes to spare
SRAM:       261,384 / 261,408 B  (99.99 %)   — 24 bytes to spare
```

Forty bytes and twenty-four. The STEP mod grid lock landed after that capture,
grew the engine, and the bench went over. Nothing was done wrong: the image had
no room left to absorb anything at all.

This is the third time the same wall has been hit. `bench/serial_arena.h` exists
because of the second time — commit `8f8fa98`, *"bench(wave): overlay serial
workloads to fit AXI SRAM"* — where the WAVE rows had to be overlaid to fit, and
the project owner confirms that fight took a long time before the WAVE benches
ran at all. The pattern is structural: every engine added to the fork enlarges
the engine code that the bench image links, and the bench image is a single
monolith containing every workload family at once.

Two consequences follow, and the second is worse than the first:

1. **No hardware measurement is possible.** The BODY engine's gate (milestone
   M5j) is blocked on it, and so is every future engine's.
2. **The WAVE acceptance gate has silently stopped running.** `run.py` enforces
   that `wave_2x4` is no slower than `synth_2x4` and stays under the
   960,000-cycle block budget. That is a product regression guard, and it has
   been unenforceable since the grid lock landed, because the image carrying
   those rows cannot be built.

Squeezing again would restore both for one more engine. This spec removes the
recurrence instead.

## Decisions (user)

- **Compile-time family selection**, not another squeeze, and not a one-off
  minimal image for the current gate.
- **Each profile declares which gates apply to it**, and the written capture
  records which gates ran and which were not applicable. A partial run must not
  be able to skip a gate silently — the skip is part of the evidence.
- **The profile appears in the evidence filename.** Existing captures keep their
  names; nothing is migrated.
- This work lives on **its own branch with its own spec**, not inside the BODY
  plan.

## Design

### 1. Profiles select families, not rows

The family list already exists twice: as the static C++ tables
(`bench/workload.h` externs, `bench/runner.cpp`'s `tables[]`, `bench/main.cpp`'s
print loops) and as `BENCH_PROTOCOL_ROWS_BY_FAMILY` in `bench/run.py`. A naive
profile mechanism would make it three, and three copies of a list drift.

So a profile names **families**, never rows. `run.py`'s row protocol stays
exactly as it is and stays the fail-closed check; for a partial image it is
filtered down to the profile's families. There is no new source of truth for
rows.

```
bench/profiles.py       profile -> (families, gates)
        |
        v
   run.py --profile system
        |
        +--> make BENCH_FAMILIES="system"
        |         -> -DBENCH_FAMILY_SYSTEM=1
        |         -> each k*Workloads table is #if-guarded
        |         -> --gc-sections drops the engine code only the
        |            omitted families referenced
        |            <-- this is where the space comes from
        |
        +--> validates against BENCH_PROTOCOL_ROWS_BY_FAMILY,
             filtered to {system}
```

The saving comes from `--gc-sections`, which is already on. Engine code is in
the image because a workload references it; removing the family removes the
reference, and the linker drops the code.

**Table order stays static.** `bench/workload.h` states that execution order
must not depend on link order, and nothing here changes that: the guards remove
whole tables, they do not reorder or self-register them.

### 2. Three-way agreement, not two

The manifest says what was *meant*. That is not enough — the failure mode
everyone eventually hits is measuring against a stale image after passing
`--no-build`.

So the firmware **reports the families it actually contains** in its output
header, and `run.py` refuses to accept a capture whose reported families differ
from the requested profile's. The manifest declares intent, the firmware reports
reality, and `run.py` insists they agree.

### 3. Two classes of gate

Most of `run.py`'s existing gates are universal and apply to every profile,
filtered where they refer to the row set:

- the capture's row set exactly equals the profile's expected set — missing
  *and* extra rows both fail
- no duplicate rows
- QSPI payload digest and device fingerprint identical across repeats
- per-row checksums identical across repeats
- at least two runs (`--repeat`, minimum 2)

One gate is profile-scoped:

- **`wave_acceptance`** — requires `synth_2x4` and `wave_2x4`; enforces
  `wave_avg <= synth_avg`, `wave_max <= synth_max`, and
  `wave_max < 960,000`.

A profile that does not declare `wave_acceptance` does not run it, and the
capture says so in as many words. A profile that *does* declare it but whose
families cannot supply the rows is a manifest error and fails at load time, not
at measurement time.

### 4. Evidence

Captures are written to
`docs/bench/YYYY-MM-DD-<githash>-<profile>.md` and `.csv`.

Existing files keep their current `YYYY-MM-DD-<githash>` names. They are not
renamed and remain valid — they predate profiles and were all full runs.

Each capture's prose gains a **gate ledger**: the profile name, its families,
the gates that ran and passed, and the gates that were not applicable with the
reason. The CSV gains a profile column alongside the existing run index, QSPI
digest and device fingerprint.

### 5. The profiles that ship

Two on this branch. No catalogue.

| profile | families | purpose | gates |
|---|---|---|---|
| `system` | system | carries the WAVE acceptance gate | universal + `wave_acceptance` |
| `full` | all | the complete run, as today | universal + `wave_acceptance` |

**Not here: the `body` profile.** This branch starts from `main`, where the
`body` workload family does not exist — it lives on `body-resonator-engine`
(`bench/workloads_body.cpp`, added by that milestone's Task 3), and
`run.py`'s `BENCH_PROTOCOL_ROWS_BY_FAMILY` on this branch has no `body`
entry either. `resolve()` (`bench/profiles.py`) checks every family a
profile names against that dict before anything is built, so naming `body`
here does fail at manifest load — but only because `body` is unknown to
`BENCH_PROTOCOL_ROWS_BY_FAMILY`, not because it is uncompiled: `resolve()`
knows nothing about the Makefile. A family that *does* have a
`BENCH_PROTOCOL_ROWS_BY_FAMILY` entry but is never wired into
`bench/Makefile`'s `FAMILY_SOURCE_*`/`FAMILY_DEFINE_*` pairs would sail
through `resolve()` and fail later instead: at the Makefile's own
unknown-family guard on a normal build, or, under `--no-build` against a
stale image that never had it compiled in, as a families mismatch once
hardware has already run. The BODY branch adds its own `body` profile entry
once it merges this work, and must give `body` a `BENCH_PROTOCOL_ROWS_BY_FAMILY`
entry and a Makefile entry alongside it for the manifest to resolve and the
image to build; the profile half of that is a two-line addition.

That ordering is deliberate: this branch lands on `main`, BODY picks it up from
`main`, and BODY's blocked hardware gate resumes there.

`full` is expected to fail to link until the engine shrinks or the region grows.
That is the honest state and this spec records it rather than hiding it. It is
also why `system` exists: it fits comfortably and restores the WAVE regression
guard immediately, independent of whether `full` ever fits again.

The default profile when `--profile` is not given is **`full`** — the existing
behaviour and the existing documented command. It will fail loudly, which is
correct: the debt should be visible to whoever runs the bare command, not
papered over by silently measuring something smaller than they asked for.

### 6. Testing

`bench/test_run_contract.py` and `bench/test_task8_contract.py` encode today's
contract and grow four cases:

- a capture matching a profile's **filtered** row set validates
- a capture whose firmware-reported families differ from the requested profile
  is rejected
- a profile that does not declare `wave_acceptance` does not pass it silently —
  the capture must record it as not applicable, and a capture that claims it ran
  without the rows present is rejected
- the written capture contains the gate ledger

These are host-side Python tests and need no hardware. The `--build-only` path
covers that a profile builds and that its firmware reports the expected
families.

Hardware verification on this branch is **one `system` run** producing an
accepted capture with its gate ledger — which also re-establishes the WAVE
acceptance evidence that has been missing since the grid lock landed. The
`body` profile is verified on the BODY branch, where its family exists.

A `full` run is expected to fail at build. The test suite asserts that it fails
at the *link* step with a region overflow rather than at manifest load or family
mismatch, so that a genuinely broken manifest can never masquerade as the known
size problem.

### 7. Documentation

`bench/README.md`'s "the one command" section becomes one command per profile,
and gains a short paragraph on why profiles exist — the SRAM history above, in
two sentences, so the next person to hit the wall knows it is a known shape and
not a fresh disaster.

## Out of scope

- Making `full` fit. This spec makes progress possible without it; reclaiming
  the space is separate work with its own trade-offs (moving non-measured bench
  code to `.qspiflash_text` is the obvious candidate, and it must not touch code
  that runs inside a measured window).
- Any change to what a workload measures, to `bench/cycles.h`, to the anchor
  mode, or to the QSPI programming path.
- The shipping firmware: the repo-root `Makefile`, `main.cpp`, `app.cpp`,
  `src/**` and `engine/**` are untouched.
- Renaming or migrating existing captures.
