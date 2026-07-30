# Plan: recover the per-sample overhead of `Part::process`

**Design:** `docs/superpowers/specs/2026-07-30-part-per-sample-design.md`
**Branch:** `perf/part-per-sample`
**Execution:** subagent-driven, one task per subagent, review after each
**Date:** 2026-07-30

Six tasks. Tasks 2 and 4 both edit `engine/parts/part.h` and
`engine/parts/part.cpp`, so they are strictly sequential — no two may run at the
same time. Tasks 1, 3 and 5 are hardware measurements run by the coordinator at
the board.

**This is the first round in the sequence that changes `engine/`.** Every previous
task brief could rely on "you cannot break the audio because you cannot touch the
audio path". That protection is gone. Stage 1's entire claim is that the audio is
unchanged *bit for bit*, and the bench checksum is what proves it.

---

## Constraints in force for every task

Read these before starting. Each one has cost this project a fix round at least
once.

1. **Repository:** `C:\Users\bernd\Documents\AI\Spotykach`. The Bash tool's
   default working directory is a **different repository** — every git command
   must be `cd "/c/Users/bernd/Documents/AI/Spotykach" && git …` or
   `git -C "…" …`. Verify `git branch --show-current` prints
   `perf/part-per-sample` before committing.
2. **Commit trailer, exactly, as the last line with nothing after it:**
   `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
   Not the default Claude/Anthropic trailer.
3. **Never run `python bench/run.py` without `--profile`.** The default profile is
   `full`, which fails to link by design (`bench/README.md`).
   `--profile ablate --build-only` is the build check.
4. **`source ./env.sh` in the same shell command** as any cmake/ctest/bench
   invocation. It does not persist between calls.
5. **Do not modify any existing bench row's setup or proc function**, and do not
   add rows. Round 4 compares rows to themselves; changing a row destroys that
   comparison and three rounds of committed evidence with it.
6. **Do not edit anything under `docs/bench/`** — machine-written evidence.
7. **Do not edit §5 of the design spec** (the pre-registration) at any point,
   including in task 6. It is registered before the build on purpose.
8. **Every declarative comment is a claim.** This repository's recurring defect is
   a comment or a document sentence asserting something false about the code
   beside it. Round 3 produced thirteen such defects, nearly all in hand-written
   prose, against zero in its three measurement rows. Verify each claim against
   the source before writing it. If you cannot verify something this plan asserts,
   say so in your report rather than writing it down as fact.
9. **Never state a cost direction derived from a mechanism.** Round 3 had two
   directional predictions refuted by further reading. State bounds; let the bench
   settle cost.
10. **Bit-exactness is this round's gate.** If a change you make alters any bench
    checksum, it is a bug in the change, not an acceptable side effect. Report it
    and revert rather than reinterpreting it.
11. **The bench refuses hardware evidence from a dirty tree** — commit before any
    measurement task.
12. **Desktop suite acceptance is "no new failure", not "all green".**
    `tests/test_seed_audition_init.cpp` is already red on `main` with two failing
    assertions. Any *other* failure is yours.

---

## Task 1 — baseline measurement (coordinator, at the hardware)

**No code changes.** The tree carries only the design commit, so the compiled code
is identical to round 3's `ccd5f12`. The point is not new information about the
code; it is to put the baseline and every later build in **one session on one
machine**, which design §6.3 requires and which round 3 could not do.

**Order — build, then rebind the QSPI receipt, then measure. Not negotiable.**

```bash
source ./env.sh && python bench/run.py --profile ablate --build-only
source ./env.sh && python bench/run.py --profile ablate --no-build --program-qspi --build-only
source ./env.sh && python bench/run.py --profile ablate
```

A `QSPI verification receipt does not match current payload (artifacts)` error
names the **artifacts**, not the bank: a stale binding, not corrupt flash, and the
second command rebinds it.

**Also capture the baseline disassembly**, because task 2's static test compares
against it:

```bash
arm-none-eabi-objdump -d -C bench/build/bench.elf > /tmp/baseline-bench.asm
arm-none-eabi-nm --print-size -C bench/build/bench.elf > /tmp/baseline-bench.sym
```

Keep these outside the repository — they are working files, not evidence.

**Acceptance.** Two runs; all checksums identical between them; the gate and
`instr_part_1` within run-to-run spread of round 3's figures (110.51 / 45.65
`pct_max`). A large discrepancy against round 3 means the machine or board state
changed and must be understood before anything is attributed to a code change.

**Commit.** The evidence file only, `docs/bench/<date>-<sha>-ablate.{csv,md}`,
message `bench(part-per-sample): baseline before any engine change`.

---

## Task 2 — item 4: make the per-sample body inlinable

**Goal.** Remove the nine-register save/restore and the VFP pair that
`Part::process` pays 96 times per block (design §3.2).

**Files.** `engine/parts/part.h`, `engine/parts/part.cpp`, and whatever the chosen
mechanism requires.

**What to write.**

- Move the per-sample body of `Part::process` (`engine/parts/part.cpp:376-488`) to
  where **every** caller can inline it. Design §4 item 4 is explicit that this
  must not be special-cased for `engine/instrument.cpp`: `deck_shell` calls
  `part.process(...)` directly from `bench/workloads_instr.cpp`, and it is the row
  that isolates a single `Part`. If only `Instrument` benefits, §5.1's prediction
  for `deck_shell` cannot come true and the round loses its cleanest instrument.
- `_control_tick()` should **stay out of line**. It runs once per 96 samples
  (design §2), inlining it would add 1404 bytes at every call site for no
  per-sample benefit, and keeping it out of line keeps the two halves separately
  measurable in the disassembly.
- **Change no behaviour.** Not the order of the calls, not the branch structure,
  not the arithmetic. The body executes exactly as it does now; only where it is
  compiled changes. Every comment in the moved body must move with it — that
  block carries a great deal of hard-won reasoning (the fade/swap ordering, the
  raster-versus-fire exclusivity, the `_audio_in_tap` guard) and none of it may be
  dropped or paraphrased in transit.

**The static test, which is §5.1's primary evidence.** After building, verify with
`objdump` that the per-sample path reached from `Instrument::process` **and** from
`bench/workloads_instr.cpp`'s `deck_shell` contains no per-sample nine-register
`stmdb`/`ldmia` pair and no `vpush {d8-d9}`/`vpop {d8-d9}` pair. Report the
addresses you inspected and the instructions you found there. **If the pair is
still emitted per sample, the item did not do what it claims** — say so plainly;
do not proceed to interpret cycle figures from it.

Report also the `.text` size change from `arm-none-eabi-size -A`, against design
§3.5's 66 KB of free `SRAM_EXEC`.

**Acceptance.** `source ./env.sh && python bench/run.py --profile ablate
--build-only` links with no new warnings; SRAM_EXEC still under 100 %; the desktop
suite shows no new failure; the static test passes.

**Commit.** `perf(part): let callers inline the per-sample body`

---

## Task 3 — measure item 4 alone (coordinator, at the hardware)

Same three commands as task 1. Two runs.

**Then report §5.1's four predictions as held or falsified**, against task 1's
baseline and **not** against round 3's `ccd5f12` figures:

| quantity | predicted | falsified if |
|---|---|---|
| `deck_shell` | −0.20 to −0.35 | outside −0.05 … −0.60 |
| `instrument_worst_bbd` | −0.40 to −0.70 | outside −0.10 … −1.20 |
| every control-group row | 0.00 ± drift | moves more than the largest control-group mover |
| every shared checksum | unchanged | **any** checksum changes |

**Verify the control group from the workload sources**, as design §6.2 requires —
do not trust the list in the spec. A row is in the control group only if its setup
constructs no `Part`.

**A checksum change stops the round.** It means a change asserted to be bit-exact
was not, and the correct response is to find out why, not to decide the difference
is inaudible.

**Commit.** The evidence file only.

---

## Task 4 — items 1, 2 and 3

**Only if task 3's static test passed.** If the model is broken, this task is
replaced by a decision, not by more edits.

**Files.** `engine/parts/part.h`, `engine/parts/part.cpp`.

**Three separate commits, in this order**, so that a checksum change can be
attributed:

**4a — item 2, the dead store.** `part.cpp:183` writes
`_tg[LANE_PITCH] = target_raw(LANE_PITCH)`; `:210` overwrites it; nothing reads it
between, because `pitch_pre_quant()` at `:185` recomputes `target_raw(LANE_PITCH)`
from members rather than reading `_tg`. **Verify that read-path claim yourself
before changing anything** — it is the whole justification. Then stop computing
the value that is discarded.
Commit: `perf(part): drop the discarded PITCH target in the control tick`

**4b — item 3, the fade multiply at hold.** `_engine_fade.process()` returns
exactly 1.0 at hold; `outL *= fade; outR *= fade;` (`part.cpp:484-485`) is then
two wasted multiplies. Re-verify the exactly-1.0 claim against
`engine/fx/fx_util.h:82-105` and the M1.6 bypass invariant — design §6.7 requires
it and forbids taking it from the spec's own sentence. Guard the multiplies.
Note in the comment *why* this is bit-exact (`x * 1.0f == x`) rather than merely
inaudible.
Commit: `perf(part): skip the engine-fade multiply at hold`

**4c — item 1, group the per-sample hot members.** `_ctrl_ctr`, `_gate_ctr`,
`_last_gate`, `_switching`, `_note_suppressed`, `_last_master_hz` and the
engine-fade state sit past a 20 KB object base (design §3.2), so every touch is a
32-bit encoding with a recomputed base. Declare them together near the start of
the object.

**Before reordering, search for anything that assumes the layout** — design §6.6:
a `memcpy` or `memset` over a member range, a serialised snapshot, a
`static_assert` on an offset or on `sizeof`, a designated-initialiser list, a
reinterpret_cast. **Report what you found, including "nothing", and where you
looked.** This is the one item in Stage 1 that can break something without
changing a single statement.

Register no expected value for this item — design §5.2 gives it no prediction on
purpose, and a comment claiming it will help would be exactly the directional
claim constraint 9 forbids.
Commit: `perf(part): group the per-sample hot state`

**Acceptance, for each commit separately.** Builds; desktop suite shows no new
failure; and for 4c additionally: `.text`/`SRAM_EXEC` reported.

---

## Task 5 — final measurement (coordinator, at the hardware)

Same three commands, two runs, after all of task 4 is committed.

Report per item where possible. Tasks 4a–4c are three commits but one build, so
their effects cannot be separated by this measurement — **say so** rather than
attributing the total to one of them. If a per-item split is wanted, that is three
more builds and three more runs, and it is the owner's call whether the items are
worth it.

---

## Task 6 — write §9

**Requirements, and they are the ones rounds 2 and 3 were corrected for missing:**

- Quote `pct_max` unless stated, and say so. Every figure must be locatable in a
  committed `docs/bench/*.csv`. **No figure may cross a build boundary
  undeclared**, and every before/after in this round crosses one (design §6.1).
- Report **every** §5 prediction as held or falsified, including the ones that
  were wrong. Do not edit §5.
- Report the control group's drift explicitly, and state for each claimed saving
  whether it exceeds the largest control-group movement. A saving that does not
  is not a saving; it is drift.
- Report item 1 separately and **without folding it into the model's score**
  (design §5.2).
- State the residue: what fraction of `Part`-level code Stage 1 did *not*
  recover, and what that implies for whether Stage 2 is worth its sonic risk.
- If Stage 1's total is small, **say that plainly and recommend against Stage 2 if
  that is what follows.** The design's §7 already forbids starting Stage 2 on a
  prediction; §9 is where the round earns the right to recommend it or not.
- Do not name a mechanism for any residue (round 2's correction).

**Acceptance.** Two runs, all shared checksums matching; desktop suite shows no
new failure; tree clean.

**Commits.** One for the evidence, one for the prose (spec §9 plus a
`docs/roadmap.md` section). Keep them separate so the machine-written and
hand-written parts stay distinguishable in the log.

---

## After task 6

A whole-branch review, dispatched as **two** independent reviewers, per design §8:

1. A code reviewer on the `engine/` diff, whose **first** job is to find any
   Stage 1 item whose claim to bit-exactness is false, and to check task 4c's
   layout assumptions. This is new for this round — the previous three could not
   break the audio.
2. A prose auditor with the CSV as ground truth, told to distrust the
   coordinator's summary, to check every §9 figure against the CSV, and to
   confirm §5 was not edited.

Round 2's mislabelled residue was caught only by the second reviewer, and only
because its instructions made the CSV ground truth.

Then the merge decision, which is the owner's.
