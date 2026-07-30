# Plan: split the remainder into `Part` code and contention

**Design:** `docs/superpowers/specs/2026-07-30-remainder-split-design.md`
**Branch:** `perf/remainder-split`
**Execution:** subagent-driven, one task per subagent, review after each
**Date:** 2026-07-30

Four tasks. Tasks 1–3 each add one bench row and **all three edit
`bench/workloads_instr.cpp`**, so they are strictly sequential — no two may run
at the same time. Task 4 builds, measures on hardware and writes the results.

---

## Constraints in force for every task

Read these before starting. They are not advisory; each one has cost this
project a fix round at least once.

1. **Repository:** `C:\Users\bernd\Documents\AI\Spotykach`. The Bash tool's
   default working directory is a **different repository** — every git command
   must be `cd "/c/Users/bernd/Documents/AI/Spotykach" && git …` or
   `git -C "…" …`. Verify `git branch --show-current` prints
   `perf/remainder-split` before committing.
2. **`engine/` is locked.** `git diff main -- engine/` must be empty at every
   commit. Read anything under `engine/`; modify nothing. The owner chose a
   strictly diagnostic round.
3. **Commit trailer, exactly, as the last line with nothing after it:**
   `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
   Not the default Claude/Anthropic trailer.
4. **Never run `python bench/run.py` without `--profile`.** The default profile
   is `full`, which fails to link by design (`bench/README.md`).
   `--profile ablate --build-only` is the build check for tasks 1–3.
5. **`source ./env.sh` in the same shell command** as any cmake/ctest/bench
   invocation. It does not persist between calls.
6. **Do not modify any existing bench row's setup or proc function.** Changing a
   row changes its checksum and destroys comparability with two committed rounds
   of evidence. All three new rows are pure additions.
7. **Do not edit anything under `docs/bench/`** — machine-written evidence.
8. **Do not edit §5 of the design spec** (the pre-registration) at any point,
   including in task 4. It is registered before the build on purpose.
9. **Every declarative comment is a claim.** This repository's recurring defect
   is a comment or a document sentence asserting something false about the code
   beside it; about a dozen were caught across rounds 1 and 2, nearly all in
   hand-written text. Verify each one against the source before writing it. If
   you cannot verify something this plan asserts, say so in your report rather
   than writing it down as fact.
10. **Self-check asserts must be able to fail** under the specific mistake they
    guard. Round 2 shipped an assert whose band was so wide the bug would have
    passed, and had to correct it. State in your report, for each assert, what
    value it would see under the mistake and why that fails the check.

---

## Task 1 — `fx_flux_hot`: FLUX at the deck's operating point

**Goal.** Price FLUX where a deck actually runs it, so the remainder stops
carrying ±2.29 points of slop from round 1's additive estimate.

**File.** `bench/workloads_instr.cpp` (not `workloads_system.cpp` — leaving that
translation unit untouched keeps the perturbation smaller, and `fx_mem()` is
declared in `bench/mem.h` and available here).

**What to write.**

- A group holding one `PartFx` plus the `float values[FXT_COUNT]` array, in the
  shape `FxGroup` uses in `bench/workloads_system.cpp`. Add it to the
  `SerialArena<…>` template argument list at `bench/workloads_instr.cpp:244`.
  The arena **overlays** its groups — capacity is `max(sizeof)`, not the sum —
  and `InstrNoVerbGroup` (a whole `Instrument`) is far larger, so SRAM must not
  grow. Confirm that in your report from the link output's SRAM line.
- A setup that reproduces `setup_fx(SEL_FLUX)` from
  `bench/workloads_system.cpp` **exactly**, then additionally applies the deck's
  operating point:
  - `set_stages(1.f)` → 16384
  - `set_flux_rate(kFluxRateCount - 1)` → index 11 (`kFluxRateCount` is 12,
    `engine/mod/divisions.h:49`)
  - `values[FXT_FLUX_FB] = 0.9f`
  Read `setup_fx` and `setup_instr_part_common` yourself and enumerate, in a
  comment, **every** difference between `fx_flux_hot` and `fx_flux_sdram`. The
  difference must be the operating point and nothing else — if you find a fourth
  difference, name it in the comment and in your report rather than quietly
  leaving it.
- A proc function identical in shape to `proc_fx`.

**Self-checks.** Use the same readbacks `setup_instr_part_common` uses at
`bench/workloads_instr.cpp:352-355`:

```cpp
assert(g.fx.flux().stages()   == bbd_tuning::kMaxStages);
assert(g.fx.flux().clock_hz() >= bbd_tuning::kClockMaxHz);
```

These catch the mistake that matters: if `set_stages`/`set_flux_rate` silently
fail to take, `stages()` returns 8192 and the row measures `fx_flux_sdram` a
second time under a new name — a plausible number and a worthless one. Report
what each assert would see under that failure.

**Registration.** The `kInstrWorkloads[]` table, `run.py`'s
`BENCH_PROTOCOL_ROWS_BY_FAMILY`, and `bench/test_run_contract.py`. No change to
`families.cpp`, the `Makefile` or `profiles.py` — the `instr` family already
exists and is already in the `ablate` profile; `profiles.py` names families,
never rows. Verify that claim rather than trusting it.

**Acceptance.** `source ./env.sh && python bench/run.py --profile ablate
--build-only` links with no new warnings; SRAM unchanged at 97.83 %;
`python bench/test_run_contract.py` passes; `git diff main -- engine/` empty.

**Commit.** `bench(instr): FLUX at the deck's own operating point`

---

## Task 2 — `tone_solo`: the shell's engine, priced

**Goal.** Make `deck_shell` interpretable. Without this row the shell's cost
cannot be split, because a shell pays the FX shell **and** the tone on top of
`Part`-level code.

**File.** `bench/workloads_instr.cpp`.

**What to write.**

- A group holding one `TestToneEngine` (`engine/parts/test_tone_engine.h`), an
  `IPartEngine*` and the target array. Add it to the arena's argument list.
- A setup that initialises the engine and pushes targets **once**, then a proc
  function that, per sample, calls through the base pointer in `Part::process`'s
  order — `process_in` then `process` (`engine/parts/part.cpp:482-483`) — and
  pushes `set_targets` once per 96-sample block, which is what
  `Part::_control_tick` does. Read `Part::process` and confirm the control-tick
  cadence before writing the comment that claims it.
- **From here on the engine is reached only through the `IPartEngine*`.** This
  is the trap round 2 nearly fell into: a concrete `TestToneEngine&` lets the
  compiler inline `process()`, and the row would then measure something a `Part`
  never does. `TestToneEngine` does **not** override `process_in` — it inherits
  `IPartEngine`'s empty body (`engine/parts/engine_iface.h:57-59`) — so on this
  row, as on `deck_engine_hot`, the `process_in` call is a dispatch and not
  compute. Say so in the comment; do not let it read as a second cost.

**Self-check.** The tone is a sine, so its output is bounded and non-constant.
An assert that the accumulated block output is finite and not identically zero
catches a setup that never pushed a non-zero LEVEL target — under which the row
would return a real, cheap, meaningless number. State the amplitude you expect
from reading `TestToneEngine::process` (`_amp * 0.3f`) and pick a band that a
zero-level setup fails.

**Registration and acceptance.** As task 1.

**Commit.** `bench(instr): the test tone, priced through the interface`

---

## Task 3 — `deck_shell`: a whole `Part` with the cheapest engine and no FX

**Goal.** Price `Part`-level code: the per-sample loop, the control tick, the
chord and quantizer path, the `_engine_fade` multiply.

**File.** `bench/workloads_instr.cpp`.

**What to write.**

- A group holding one `Part` plus the readback fields the asserts need. Add it
  to the arena's argument list.
- A setup that puts the `Part` at the gate's operating point the way
  `setup_instr_part_common` does — same seed base for PART_A, FLOW, RATE 0.8,
  DENSITY 1.0 — and then departs in exactly two ways, both of which must be
  commented as deliberate:
  1. `set_engine(ENGINE_TEST_TONE)`, **run to completion.** The switch is faded,
     not immediate: `_switching` is set and the swap finishes through
     `_engine_fade`, which re-pushes `set_flow`, `set_hold`, `set_gate` and
     `set_cycle` into the newly selected engine (`engine/parts/part.cpp:386-393`).
     Read that function and determine how many blocks the fade needs; settle at
     least that long, and say in the comment where the number came from.
  2. Every FX block off **at `fx_none`'s exact operating point**, so that
     subtracting `fx_none` is a clean subtraction rather than a fourth
     operating-point mismatch. Read `setup_fx(SEL_NONE)` in
     `bench/workloads_system.cpp` and mirror it — including the `values[]`
     entries, which are part of that operating point.
- A proc function in `Part::process`'s own shape.

**Self-checks.** Two, and both must be able to fail:

```cpp
assert(g.part.engine_id() == ENGINE_TEST_TONE);   // the switch took
```
and a readback proving the fade finished rather than being mid-crossfade. Find
what `Part` exposes for that; if it exposes nothing, say so in your report and
fall back to a settle long enough that the fade cannot still be running, with
the arithmetic shown. Do **not** invent a getter in `engine/`.

**The interpretation guard.** `deck_shell` must come out **strictly less than
`instr_part_1` (46.00)** and by roughly the engine and FX difference. A number
anywhere near 46 means the engine switch did not take and the row is still
running a `SynthEngine`. Note this in the comment as the reader's sanity check.

**Registration and acceptance.** As task 1.

**Commit.** `bench(instr): a Part shell -- Part-level code, uncontended`

---

## Task 4 — build, measure, write the results

**This task needs the owner at the hardware.** The board is not reachable from a
subagent. Task 4's subagent prepares and analyses; the owner runs the three
commands.

**Order — build, then rebind the QSPI receipt, then measure. Not negotiable.**

```bash
source ./env.sh && python bench/run.py --profile ablate --build-only
source ./env.sh && python bench/run.py --profile ablate --no-build --program-qspi --build-only
source ./env.sh && python bench/run.py --profile ablate
```

A `QSPI verification receipt does not match current payload (artifacts)` error
names the **artifacts**, not the bank: it is a stale binding, not corrupt flash,
and the second command above is what rebinds it.

The bench refuses hardware evidence from a dirty tree — **commit tasks 1–3
before measuring.**

**Then write §9 of the design spec.** Requirements, and they are the ones round
2 was corrected for missing:

- Quote run 2 `pct_max` unless stated, and say so. Every figure must be locatable
  in `docs/bench/<date>-<sha>-ablate.csv`. No figure from an earlier build may
  enter any difference.
- Compute `Part`-level code = `deck_shell − fx_none − tone_solo` and the residue
  per §4.1 of the design. Show the arithmetic.
- **Report every §5 prediction as held or falsified, including the ones that were
  wrong.** Do not edit §5. If `Part`-level code comes out ≤ 0, report the method
  as broken — do not write it up as "the `Part` costs nothing".
- Label the residue "contention plus interaction", never "contention" alone
  (design §6.2). State that `Part`-level code is a **floor**, measured
  uncontended (§6.1).
- Report which shared-row checksums held across the two runs and across builds,
  and report the layout drift on the gate and on `fx_grit` — §6.3 requires it,
  and no claim may rest on a cross-build difference.
- Name `Flux::set_rhythm` as a fix candidate for a later round if `Part`-level
  code is hot. Do not guard it here.

**Acceptance.** Two runs, all shared checksums matching; desktop suite shows no
new failure (`tests/test_seed_audition_init.cpp` is already red on `main`);
`git diff main -- engine/` empty; tree clean.

**Commits.** One for the evidence (`docs/bench/…`), one for the results prose
(spec §9 + a `docs/roadmap.md` section). Keep them separate so the
machine-written and hand-written parts are distinguishable in the log.

---

## After task 4

A whole-branch review, dispatched as **two** independent reviewers — one on the
code, one auditing every number against the CSV. Round 2's mislabelled residue
was caught only by the second, and only because its instructions made the CSV
ground truth and told it to distrust the coordinator's summary. Do the same
here.

Then the merge decision, which is the owner's.
