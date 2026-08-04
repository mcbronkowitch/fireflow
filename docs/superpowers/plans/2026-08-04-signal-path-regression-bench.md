# Signal-path regression bench — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure whether the seventeen unbenched `engine/` commits since
`19f7560` pushed the instrument's decision gate over the 960,000-cycle block
budget, and price the BBD stage-tap crossfade, both as a same-session A/B on
the real Daisy Seed.

**Architecture:** One new two-family bench profile (`regress` = `system` +
`bbd`) and one new workload row (`bbd_line_stage_walk`) are added to
`bench/`. A baseline branch is then constructed as *today's `bench/` with
`19f7560`'s `engine/`*, so the two trees differ in `engine/` and nothing
else. Four hardware cycles measure both trees at `-O3` in both execution
layouts. Nothing under `engine/` changes.

**Tech Stack:** Python 3 (`bench/run.py`, `bench/profiles.py`, `unittest`),
C++17 (`bench/workloads_bbd.cpp`), `arm-none-eabi-gcc`, OpenOCD +
ST-Link V3 semihosting, CMake + Ninja + clang for the desktop host.

## Global Constraints

- **Spec:** `docs/superpowers/specs/2026-08-04-signal-path-regression-bench-design.md`. Every task's requirements implicitly include it.
- **`engine/` is not modified on `main`, and the round produces no engine change.** Not one line. If a task appears to need an engine change, stop and report instead. **The single exception is Task 3**, whose entire purpose is to revert `engine/` to `19f7560` **on the branch `bench/baseline-19f7560` only** — that revert is the baseline, not a change to the product. No engine content from that branch is ever merged back.
- **Work happens on `main`** and on the baseline branch it creates. This is the repo's own convention for bench rounds, and it is deliberate here: `bench/build`, the QSPI receipt binding and the branch construction all assume one working copy on this desk.
- **Row order is execution state.** New rows are **appended** to the end of `kBbdWorkloads[]`, never inserted. Inserting a row ahead of another changes the other row's checksum.
- **Workload basenames must be unique across the whole bench**, not just within one table — libDaisy's Makefile flattens paths with `notdir`.
- **Hardware evidence is refused from a dirty working tree** (`bench/qspi_tools.py:require_clean_tree`, `git status --porcelain --untracked-files=all`). Every measurement cycle therefore ends by committing its own captures before the next cycle builds.
- **Build identity is fixed:** `--optimization o3` on all four cycles. LTO is already rejected and is not measured. Layout is the only variable: `axi` and `--itcm-hot`.
- **`--repeat 2` is the default and the minimum.** Do not lower it.
- **The full README sequence per cycle is mandatory and does not shorten:** `--build-only`, then `--no-build --program-qspi --build-only`, then `--repeat 2`. Any change under `engine/` invalidates the QSPI receipt even though the bank's 65,024 bytes are untouched.
- **Read linked facts from `bench/build/bench.map`, never from a memory table.** The bench build can silently relink a stale object and still print a plausible figure for code that was never linked.
- **Desktop host builds need `source env.sh` first** (clang + Ninja + vendored headers). System `g++` is the ARM cross-compiler on this desk.
- **Commit trailer:** every commit ends with `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **Python tests run from `bench/`:** `python -m unittest test_run_contract -v`.

---

### Task 1: The `regress` profile

Adds a two-family profile so the decision-gate rows and the BBD kernel rows
live in the same binary. `body` (`system`+`body`) and `sweep`
(`system`+`sweep`) are the precedents that a two-family image links.

**Files:**
- Modify: `bench/profiles.py` (the `PROFILES` dict)
- Modify: `bench/README.md` (the profile table under "Profiles, and one command per profile")
- Test: `bench/test_run_contract.py` (class `ProfileContract`)

**Interfaces:**
- Consumes: `profiles.resolve(name, rows_by_family)`, `profiles.Profile`, `profiles.WAVE_ACCEPTANCE`, `runner.BENCH_PROTOCOL_ROWS_BY_FAMILY`, and the existing test helpers `family_row`, `bench_row`, `capture_lines`, `resolve_profile`, `runner.validate_captures`.
- Produces: profile name `"regress"` with `families == ("system", "bbd")` and `gates == frozenset({WAVE_ACCEPTANCE})`. Task 2 extends the `"bbd"` row tuple this profile filters; Tasks 5–8 pass `--profile regress` on the command line.

- [ ] **Step 1: Write the failing tests**

Add to `bench/test_run_contract.py`, inside `class ProfileContract`, after
`test_system_profile_validates_against_its_filtered_rowset`:

```python
    def regress_rows(self):
        """A complete row set for the regress profile: the system rows a
        two-family image still emits, plus every bbd row. Checksums are
        offset from the system block so no two rows collide by accident."""
        rows = self.system_rows()
        for i, name in enumerate(runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["bbd"]):
            rows.append(family_row("bbd", name, 100, 101, "%08x" % (0xb0 + i)))
        return rows

    def test_regress_profile_carries_system_and_bbd(self):
        """The profile's whole point is that the gate rows and the BBD
        kernel rows are in ONE image, so gate-versus-kernel is a same-build
        comparison rather than a cross-image subtraction."""
        profile = resolve_profile("regress")

        self.assertEqual(("system", "bbd"), profile.families)
        self.assertIn(WAVE_ACCEPTANCE, profile.gates)

    def test_regress_capture_validates_against_its_filtered_rowset(self):
        """A capture holding both families is complete for the profile."""
        capture = runner.parse(
            capture_lines(self.regress_rows(), families="system bbd")
        )

        runner.validate_captures(
            [capture, capture], resolve_profile("regress")
        )

    def test_regress_rejects_a_capture_missing_the_bbd_family(self):
        """A system-only image must not be accepted under this profile --
        that is exactly the stale-image mix-up the round cannot afford."""
        capture = runner.parse(
            capture_lines(self.system_rows(), families="system")
        )

        with self.assertRaises(runner.BenchValidationError):
            runner.validate_captures(
                [capture, capture], resolve_profile("regress")
            )
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cd bench && python -m unittest test_run_contract.ProfileContract -v
```

Expected: FAIL. `resolve_profile("regress")` raises
`KeyError: "unknown bench profile 'regress' (known: ablate, bbd, body, full, sweep, system)"`.

`runner.BenchValidationError` is the class `validate_captures` raises
(`bench/run.py:368`); four existing tests in this file already assert against
it. Do not weaken the assertion to a bare `Exception`.

- [ ] **Step 3: Add the profile**

In `bench/profiles.py`, insert into `PROFILES` after the `"sweep"` entry:

```python
    # The 2026-08-04 signal-path regression round (spec
    # 2026-08-04-signal-path-regression-bench-design). `system` supplies the
    # decision gate and its anchors; `bbd` supplies the kernel rows the same
    # round A/Bs against a baseline tree. Two families in ONE image is the
    # whole point: 2026-07-31-b9afe47-bbd-engine.md left "what would settle
    # it is a same-build A/B" open, and a cross-image subtraction is not a
    # measurement -- composition and layout move the gate by points at an
    # unchanged checksum. `body` and `sweep` are the precedents that a
    # two-family image links.
    "regress": Profile(
        families=("system", "bbd"),
        gates=frozenset({WAVE_ACCEPTANCE}),
    ),
```

Also add the `WAVE_ACCEPTANCE` import to the test file's imports if it is
not already there — `bench/test_run_contract.py:23` already reads
`from profiles import Profile, WAVE_ACCEPTANCE, resolve`, so it is.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cd bench && python -m unittest test_run_contract -v
```

Expected: PASS, whole file, no regressions in the other classes.

- [ ] **Step 5: Document the profile in the README**

In `bench/README.md`, add a row to the profile table so the table lists what
`profiles.py` ships:

```markdown
| `regress` | system, bbd | the 2026-08-04 signal-path regression A/B: gate rows and BBD kernel rows in one image |
```

- [ ] **Step 6: Commit**

```bash
git add bench/profiles.py bench/README.md bench/test_run_contract.py
git commit -m "$(cat <<'EOF'
bench(profile): the gate and the BBD kernel share one image

regress = system + bbd. A cross-image subtraction is not a measurement;
this is what makes gate-versus-kernel a same-build comparison.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 2: The `bbd_line_stage_walk` row

Every existing row holds its stage count fixed, and a fixed stage count never
enters the crossfade branch `a183852` added — `_stage_transition_active()` is
true for `kStageXfadeReads = 16` read events after a change and false
otherwise. The whole table therefore prices the crossfade at zero. This row
is `bbd_line_tap` with one difference, so the pair is a same-build A/B and
the difference **is** the crossfade.

**Files:**
- Modify: `bench/run.py` (`BENCH_PROTOCOL_ROWS_BY_FAMILY["bbd"]`)
- Modify: `bench/workloads_bbd.cpp` (the new row, appended; and the stale comment at line 145)
- Test: `bench/test_run_contract.py` (class `ProfileContract`)

**Interfaces:**
- Consumes: Task 1's `"regress"` profile; `spky::BbdLine` (`Init(float* buf, size_t max_cells, float sample_rate)`, `SetStages(int stages)`, `SetClock(float hz)`, `Process(float)`), `bench::sdram_arena()`, `bench::test_input()`, `bench::kBlock`, `bench::kSampleRate`, `spky::bbd_tuning::kClockMaxHz`, the file-local `kTapStages == 4096`.
- Produces: row name `"bbd_line_stage_walk"`, family `"bbd"`, last entry of `kBbdWorkloads[]`. Tasks 5–8 expect it in every capture; Task 9 reads it against `bbd_line_tap`.

- [ ] **Step 1: Write the failing test**

Add to `bench/test_run_contract.py`, inside `class ProfileContract`:

```python
    def test_bbd_family_ends_with_the_stage_walk_row(self):
        """The crossfade row is APPENDED, not inserted: row order is
        execution state, and inserting ahead of a row changes that row's
        checksum (bench/README.md, 'Row order is state')."""
        bbd_rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["bbd"]

        self.assertEqual("bbd_line_stage_walk", bbd_rows[-1])
        self.assertEqual(
            (
                "bbd_ceiling",
                "bbd_line_only",
                "bbd_line_tap",
                "bbd_line_tap_half",
                "bbd_walk_sdram",
                "bbd_line_stage_walk",
            ),
            bbd_rows,
        )

    def test_regress_rejects_a_capture_without_the_stage_walk_row(self):
        """The row-set gate must be able to go red on a missing row, or it
        proves nothing when it is green."""
        rows = [
            row
            for row in self.regress_rows()
            if row.split(",")[2] != "bbd_line_stage_walk"
        ]
        capture = runner.parse(capture_lines(rows, families="system bbd"))

        with self.assertRaises(runner.BenchValidationError):
            runner.validate_captures(
                [capture, capture], resolve_profile("regress")
            )
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd bench && python -m unittest test_run_contract.ProfileContract -v
```

Expected: FAIL on `test_bbd_family_ends_with_the_stage_walk_row` —
`'bbd_walk_sdram' != 'bbd_line_stage_walk'`. This is the RED that proves the
row-set gate is live; record that it was seen.

- [ ] **Step 3: Add the row name to the protocol**

In `bench/run.py`, append to the `"bbd"` tuple in
`BENCH_PROTOCOL_ROWS_BY_FAMILY`:

```python
    "bbd": (
        "bbd_ceiling",
        "bbd_line_only",
        "bbd_line_tap",
        "bbd_line_tap_half",
        "bbd_walk_sdram",
        # The crossfade path a183852 added. Every row above holds its stage
        # count fixed and therefore never enters it. Appended, not inserted:
        # row order is execution state.
        "bbd_line_stage_walk",
    ),
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
cd bench && python -m unittest test_run_contract -v
```

Expected: PASS.

- [ ] **Step 5: Add the workload**

In `bench/workloads_bbd.cpp`, insert this **after** `proc_bbd_walk_sdram`
and **before** the closing `} // namespace`:

```cpp
// --- the stage-transition path ----------------------------------------------
// Every row above holds its stage count fixed, and a fixed stage count never
// enters the crossfade branch: _stage_transition_active() is true for
// kStageXfadeReads = 16 read events after a change and false otherwise. So
// the tap crossfade (engine/fx/bbd.h, commit a183852) is priced at zero by
// this whole table.
//
// This row is setup_bbd_line_tap with one difference -- the stage count walks
// -- so the pair is a same-build A/B and the difference IS the crossfade.
//
// The period is 24 samples rather than one. At the ceiling clock a line runs
// 2*32000/48000 = 1.33 ticks per sample, half of them reads, so sixteen reads
// is about 24 samples. Re-arming every 24 samples keeps a transition active
// for essentially the whole block while calling SetStages four times per
// block instead of 96: the crossfade is what this row prices, not the setter.
// One transition per block would price the crossfade at a quarter and read as
// repeat noise.
//
// Its own BbdLine object, for the reason stated above kTapStages: sharing the
// arena between rows is safe because the runner calls setup() immediately
// before each warmup, but sharing the OBJECT would leave this row's walking
// SetStages fighting a neighbour's settled state.

constexpr int kStageWalkPeriod = 24;
constexpr int kStageWalkAlt    = kTapStages / 2;   // 2048 stages, 1024 cells

BbdLine g_stagewalk;
int     g_stagewalk_phase = 0;

void setup_bbd_line_stage_walk()
{
    g_stagewalk.Init(sdram_arena(), kTapStages / 2, kSampleRate);
    g_stagewalk.SetStages(kTapStages);
    g_stagewalk.SetClock(bbd_tuning::kClockMaxHz);
    g_stagewalk_phase = 0;
    // Same settle as settle_tap(), on this row's own object: the line must be
    // FULL before measuring or the row measures an empty machine.
    for (int i = 0; i < 32768; ++i)
        g_stagewalk.Process(0.3f * sinf(static_cast<float>(i) * 0.01f));
}

float proc_bbd_line_stage_walk()
{
    const float* in = test_input();
    float acc = 0.f;
    for (size_t s = 0; s < kBlock; ++s) {
        if (g_stagewalk_phase == 0)
            g_stagewalk.SetStages(kTapStages);
        else if (g_stagewalk_phase == kStageWalkPeriod)
            g_stagewalk.SetStages(kStageWalkAlt);
        if (++g_stagewalk_phase >= 2 * kStageWalkPeriod) g_stagewalk_phase = 0;
        acc += g_stagewalk.Process(in[s]);
    }
    return acc;
}
```

Then append the row to the table — **last**, after `bbd_walk_sdram`:

```cpp
const Workload kBbdWorkloads[] = {
    { "bbd", "bbd_ceiling",     setup_bbd_ceiling,     proc_bbd_ceiling     },
    { "bbd", "bbd_line_only",   setup_bbd_line_only,   proc_bbd_line_only   },
    { "bbd", "bbd_line_tap",      setup_bbd_line_tap,      proc_bbd_line_tap },
    { "bbd", "bbd_line_tap_half", setup_bbd_line_tap_half, proc_bbd_line_tap },
    { "bbd", "bbd_walk_sdram",  setup_bbd_walk_sdram,  proc_bbd_walk_sdram  },
    { "bbd", "bbd_line_stage_walk",
      setup_bbd_line_stage_walk, proc_bbd_line_stage_walk },
};
```

Note the row compiles against **both** engines by design. On the baseline
tree `SetStages` returns early on an unchanged value and resets the ring
index on a changed one; on `HEAD` it arms a crossfade. That difference is the
measurement.

- [ ] **Step 6: Correct the stale comment**

`a183852` changed where the write index wraps: from `cells_` to `max_cells_`.
The comment above `kWalkCells` in `bench/workloads_bbd.cpp:145` describes the
old ring. Replace:

```cpp
// The active window at 8192 stages is 4096 cells = 16 KB per line, walked
// SEQUENTIALLY (imem advances by exactly one cell per write tick and the read
// tick reads the cell about to be overwritten). The 3.29x SDRAM penalty
// measured for streaming walks is EXPECTED to largely disappear here. That is
// an expectation, not a measurement -- this row is what turns it into one.
```

with:

```cpp
// The walk is SEQUENTIAL: the write index advances by exactly one cell per
// write tick, and the read tick reads a fixed distance behind it. The 3.29x
// SDRAM penalty measured for streaming walks is EXPECTED to largely
// disappear here. That is an expectation, not a measurement -- this row is
// what turns it into one.
//
// Corrected 2026-08-04: this comment used to say "the active window at 8192
// stages is 4096 cells = 16 KB per line". That stopped being true at
// a183852, which moved the write wrap from cells_ to max_cells_. The written
// span is now max_cells_ at EVERY stage count -- kCells = kMaxStages/2 =
// 8192 floats, 32 KB per line -- and the stage count sets only how far
// behind the write head the read tap sits. kWalkCells below is this row's
// own constant and is unaffected; what changed is what it is a proxy FOR.
```

- [ ] **Step 7: Commit**

```bash
git add bench/run.py bench/workloads_bbd.cpp bench/test_run_contract.py
git commit -m "$(cat <<'EOF'
bench(bbd): a row whose stage count walks, so the crossfade is priced

Every existing row holds its division fixed and never enters the
transition branch. Appended, not inserted. The walk period is 24 samples
because sixteen read events is about 24 samples at the ceiling clock.

Also corrects the write-ring comment a183852 invalidated.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 3: The baseline branch

The delta this round reports is a difference between two trees. Checking out
`19f7560` would not work — that tree has neither the `regress` profile nor
the new row, and its `run.py` row expectations do not contain it. The
baseline is therefore **today's `bench/` with `19f7560`'s `engine/`**, which
is only sound because **no commit between `19f7560` and `HEAD` touched
`bench/`**: today's bench code has, by construction, already compiled against
that engine.

**Files:**
- Create: branch `bench/baseline-19f7560`
- Modify (on that branch only): everything under `engine/`, reverted to `19f7560`

**Interfaces:**
- Consumes: Tasks 1 and 2, both committed on `main`.
- Produces: a committed, clean branch `bench/baseline-19f7560` whose `git diff main -- bench/` is empty and whose `git diff 19f7560 -- engine/` is empty. Tasks 5 and 6 measure it.

- [ ] **Step 1: Verify the premise before relying on it**

```bash
git log --oneline 19f7560..HEAD --name-only -- bench | wc -l
```

Expected: `0`. If it is not zero, **stop and report** — the whole A/B
construction in the spec's §3 rests on this and a non-zero answer refutes it.

- [ ] **Step 2: Create the branch and revert the engine**

```bash
git checkout -b bench/baseline-19f7560
git checkout 19f7560 -- engine/
git status --short
```

Expected: only paths under `engine/` are listed as modified.

- [ ] **Step 3: Verify the branch is exactly what it claims**

```bash
git diff --stat 19f7560 -- engine/     # expected: empty (staged content matches)
git diff --stat main -- bench/         # expected: empty
```

If either prints anything, stop and report rather than adjusting until it
looks right.

- [ ] **Step 4: Commit the branch**

```bash
git commit -m "$(cat <<'EOF'
bench(baseline): engine at 19f7560, bench at HEAD

Not a checkout of 19f7560: that tree has no regress profile and its row
expectations do not contain bbd_line_stage_walk. This branch exists so the
A/B differs in engine/ and nothing else.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
git checkout main
```

---

### Task 4: Offline preflight

Everything that can fail without hardware fails here, before a cable is
touched. A link failure discovered mid-session costs a programming cycle and
reads like a hardware fault.

**Files:**
- No source changes. This task produces a written result, not a diff.

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces: a go/no-go for Tasks 5–8, and — if the link probe fails — the decision to fall back to the spec's §8 plan B (separate `system` and `bbd` profiles, eight cycles).

- [ ] **Step 1: Desktop test suite on `main`**

```bash
source env.sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass. The seventeen commits each passed tests
individually; they have not been run together on this exact state.

- [ ] **Step 2: One render as a plausibility check**

```bash
./build/render.exe host/render/scenarios/dorian_vs_drift.json out.wav mods.csv
```

Expected: exits 0 and writes a non-empty `out.wav`. **This is a sanity check,
not a gate** — this repo does not use checksum or byte-identity gates on
renders. Both output paths are gitignored (`/out.wav`, `/mods.csv`), so this
does not dirty the tree.

- [ ] **Step 3: The link probe — four builds, no hardware**

```bash
cd bench
git checkout main
python run.py --profile regress --optimization o3 --build-only
python run.py --profile regress --optimization o3 --itcm-hot --build-only
git checkout bench/baseline-19f7560
python run.py --profile regress --optimization o3 --build-only
python run.py --profile regress --optimization o3 --itcm-hot --build-only
git checkout main
```

Expected: all four exit 0.

**If an `axi` build fails to link** with an SRAM/SRAM_EXEC region overflow:
`regress` does not fit. Stop, record the exact region and overflow byte count
from the linker output, and switch to the spec's §8 plan B — Tasks 5–8 then
run `--profile system` and `--profile bbd` separately, eight cycles instead
of four, and Task 9 reports the BBD figures as component prices only, with no
gate-versus-kernel arithmetic.

**If only the `--itcm-hot` builds fail** the placement preflight: record the
exact symbol and address from the error, run `axi` only, and name the
omission in Task 9's document. `bbd` together with `--itcm-hot` is an
untested combination; the hotset symbols all come from objects that are on
the link line regardless of profile, so it is expected to pass, which is why
it is a gate and not a formality.

- [ ] **Step 4: The row-real check, from the map**

After the last `main` build:

```bash
cd bench
grep -c "bbd_line_stage_walk" build/bench.map
grep -c "instrument_worst_bbd_dtcm" build/bench.map
grep -c "inst_bbd_engine_worst" build/bench.map
grep -c "bbd_line_tap" build/bench.map
```

Expected: each returns a non-zero count. Read from `bench.map`, not from the
memory table: the bench build can silently relink a stale object and still
print a plausible figure for code that was never linked.

- [ ] **Step 5: Record the preflight result**

No commit — this task's output is the go/no-go and the numbers above,
reported to the reviewer before any hardware runs.

---

### Task 5: Cycle 1 — baseline, `axi`

**Files:**
- Create: `docs/bench/2026-08-04-<baseline-hash>-regress-axi-o3.md` and `.csv` (written by `run.py`)

**Interfaces:**
- Consumes: Task 4's go-ahead.
- Produces: the first of four accepted captures. Task 9 reads it.

- [ ] **Step 1: Check out the baseline and confirm the tree is clean**

```bash
git checkout bench/baseline-19f7560
git status --porcelain --untracked-files=all
```

Expected: empty output. Hardware evidence is refused from a dirty tree.

- [ ] **Step 2: Build**

```bash
cd bench && python run.py --profile regress --optimization o3 --build-only
```

Expected: exits 0.

- [ ] **Step 3: Bind the QSPI receipt**

```bash
cd bench && python run.py --profile regress --optimization o3 --no-build --program-qspi --build-only
```

Expected: exits 0 and writes `build/qspi-verified.json`. The ST-Link must be
on the SWD header and the Seed powered. **Do not put the Seed into DFU
mode.** Do not skip step 2 and come straight here: with `--no-build` this
binds to whatever `bench.elf` is lying on disk, and step 4 rebuilds.

- [ ] **Step 4: Measure**

Monitors connected and **quiet** first. This produces two bursts of harsh
buzz, and the `instrument_worst` segment inside each is underrun garbage on
purpose — that row is over budget offline.

```bash
cd bench && python run.py --profile regress --optimization o3 --repeat 2
```

Expected: exits 0, and writes a `.md`/`.csv` pair into `../docs/bench/`.
Exit 0 means both runs passed every gate the profile declares.

If it exits non-zero, capture the exact message and stop. A gate failure
writes no accepted evidence; a valid over-budget DTCM+BBD result is archived
as **rejected** evidence and is a result, not a failure.

- [ ] **Step 5: Commit the captures**

The next cycle cannot build until the tree is clean again.

```bash
git add docs/bench/
git commit -m "$(cat <<'EOF'
bench(evidence): baseline tree, regress profile, axi, -O3

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 6: Cycle 2 — baseline, `itcm-hot`

Identical to Task 5 with `--itcm-hot` appended to **all three** commands.
Using the flag inconsistently across build, QSPI binding and measurement is
what the host's layout check exists to reject; an AXI receipt must not be
reused for an ITCM run.

**Files:**
- Create: `docs/bench/2026-08-04-<baseline-hash>-regress-itcm-hot-o3.md` and `.csv`

**Interfaces:**
- Consumes: Task 5 committed, tree clean, still on `bench/baseline-19f7560`.
- Produces: the second accepted capture.

- [ ] **Step 1: Confirm branch and clean tree**

```bash
git rev-parse --abbrev-ref HEAD          # expected: bench/baseline-19f7560
git status --porcelain --untracked-files=all   # expected: empty
```

- [ ] **Step 2: Build**

```bash
cd bench && python run.py --profile regress --optimization o3 --itcm-hot --build-only
```

- [ ] **Step 3: Bind the QSPI receipt**

```bash
cd bench && python run.py --profile regress --optimization o3 --itcm-hot --no-build --program-qspi --build-only
```

- [ ] **Step 4: Measure**

```bash
cd bench && python run.py --profile regress --optimization o3 --itcm-hot --repeat 2
```

- [ ] **Step 5: Record the hotset size and commit**

From `bench/build/bench.map`, record the `.itcm_audio_hot` section start, end
and size, and the bytes free below the hard 64 KiB `ASSERT`. Task 9 reports
it against the 48,352 bytes recorded on 2026-08-01.

```bash
git add docs/bench/
git commit -m "$(cat <<'EOF'
bench(evidence): baseline tree, regress profile, itcm-hot, -O3

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 7: Cycle 3 — `main`, `axi`

**Files:**
- Create: `docs/bench/2026-08-04-<main-hash>-regress-axi-o3.md` and `.csv`

**Interfaces:**
- Consumes: Tasks 5 and 6 committed on the baseline branch.
- Produces: the third accepted capture, and the first figure that answers the round's question 1.

- [ ] **Step 1: Bring the baseline captures onto `main`, then check the tree**

The two capture pairs from Tasks 5 and 6 are committed on the baseline
branch. Task 9's document lives on `main` and cites all four, so copy them
across now rather than at the end:

```bash
git checkout main
git checkout bench/baseline-19f7560 -- docs/bench/
git status --short
```

Expected: only the two new `.md`/`.csv` pairs appear as added.

```bash
git commit -am "$(cat <<'EOF'
bench(evidence): carry the baseline captures onto main

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
git status --porcelain --untracked-files=all   # expected: empty
```

- [ ] **Step 2: Build**

```bash
cd bench && python run.py --profile regress --optimization o3 --build-only
```

- [ ] **Step 3: Bind the QSPI receipt**

```bash
cd bench && python run.py --profile regress --optimization o3 --no-build --program-qspi --build-only
```

The engine differs from Task 5's, so the receipt is stale by construction.
`ERROR: QSPI verification receipt does not match current payload (artifacts)`
after skipping the build step means the binding is stale, **not** that the
QSPI is corrupt or a different Seed is attached — those have their own checks.

- [ ] **Step 4: Measure**

```bash
cd bench && python run.py --profile regress --optimization o3 --repeat 2
```

- [ ] **Step 5: Commit the captures**

```bash
git add docs/bench/
git commit -m "$(cat <<'EOF'
bench(evidence): main, regress profile, axi, -O3

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 8: Cycle 4 — `main`, `itcm-hot`

**Files:**
- Create: `docs/bench/2026-08-04-<main-hash>-regress-itcm-hot-o3.md` and `.csv`

**Interfaces:**
- Consumes: Task 7 committed, tree clean, on `main`.
- Produces: the fourth accepted capture — the figure the M6 placement target is judged against.

- [ ] **Step 1: Confirm branch and clean tree**

```bash
git rev-parse --abbrev-ref HEAD                 # expected: main
git status --porcelain --untracked-files=all    # expected: empty
```

- [ ] **Step 2: Build**

```bash
cd bench && python run.py --profile regress --optimization o3 --itcm-hot --build-only
```

- [ ] **Step 3: Bind the QSPI receipt**

```bash
cd bench && python run.py --profile regress --optimization o3 --itcm-hot --no-build --program-qspi --build-only
```

- [ ] **Step 4: Measure**

```bash
cd bench && python run.py --profile regress --optimization o3 --itcm-hot --repeat 2
```

- [ ] **Step 5: Record the hotset size and commit**

Record `.itcm_audio_hot` start, end, size and free bytes from
`bench/build/bench.map`, as in Task 6.

```bash
git add docs/bench/
git commit -m "$(cat <<'EOF'
bench(evidence): main, regress profile, itcm-hot, -O3

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 9: The evidence document and the roadmap

**Files:**
- Create: `docs/bench/2026-08-04-<main-hash>-signal-path-regression.md`
- Modify: `docs/roadmap.md` (the CPU-status paragraph near the top)

**Interfaces:**
- Consumes: all four accepted captures from Tasks 5–8, the map figures from Tasks 6 and 8, and the preflight result from Task 4.
- Produces: the round's answer. Nothing consumes it in this plan.

- [ ] **Step 1: Write the evidence document**

Follow the house style of `docs/bench/2026-08-01-19f7560-flux-tape.md`. It
must contain, in this order:

1. **What was measured** — the exact commands, both git hashes, the profile, both layouts, `-O3`, `--repeat 2`, the QSPI payload SHA-256 and the hashed device fingerprint each session reported. Never the raw MCU UID; the raw UID stays in `qspi-verified.json` only.
2. **Row real, not stale** — the `bench.map` counts from Task 4 step 4.
3. **The gate, both trees, both layouts** — a table of `instrument_worst_bbd_dtcm` `pct_avg` / **`pct_max`** with `pct_max` bold, plus its real-callback anchor maxima, against 100 %. State the verdict in one sentence: it fits, or it is over by N points.
4. **The A/B delta per row**, computed only within a layout — baseline `axi` against `main` `axi`, baseline `itcm-hot` against `main` `itcm-hot`. Never across layouts.
5. **The crossfade price** — `bbd_line_stage_walk` against `bbd_line_tap`, same build; and `bbd_line_stage_walk` baseline against `main`, which is click-versus-crossfade.
6. **The write-ring observation** — what the two `bbd_line_tap` rows did across the A/B, reported as an observation. Do not name a mechanism the round did not measure.
7. **The ITCM hotset** — size, end address, bytes free, against 48,352 on 2026-08-01.
8. **What this does not show** — the spec's §1.2 list (no within-FX attribution), plus anything the session itself left open, plus any omission forced by Task 4's fallbacks.

Two rules the document must not break, both of which this repo has been
burned by and recorded:

- **Component rows do not sum.** Do not subtract row figures to derive a
  component cost. The repo's own example is component rows summing to ~120 %
  of budget against a measured ~159 %, a 39-point gap with no named owner.
- **`pct_max` is the decision value, not `pct_avg`.** Report both; bold
  `pct_max`.

- [ ] **Step 2: Update the roadmap**

Add a dated update paragraph to `docs/roadmap.md`'s CPU-status block, in the
same form as the existing ones: the date, the gate figure, the overshoot or
the margin, the branch or commit, and a pointer to the evidence document. If
the gate broke, state that the attribution round is next and that it has no
spec yet.

- [ ] **Step 3: Commit**

```bash
git add docs/bench/ docs/roadmap.md
git commit -m "$(cat <<'EOF'
docs(bench): what the signal-path round cost, measured against its baseline

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

- [ ] **Step 4: Report the verdict**

State plainly whether the gate fits, at which layout, and by how much. If it
does not, the round ends there: this plan changes nothing under `engine/`,
and the attribution round gets its own spec.
