# O3/LTO CPU Experiment Implementation Plan

> **Executed result and supersession — 2026-07-30.** This is the preserved
> historical pre-registration plan; its body below is not rewritten after the
> fact. Execution occurred on `codex/perf-o3-lto`, not the earlier branch name
> recorded in the global constraints. The final measured design and outcome
> are in
> [the O3/LTO result](../specs/2026-07-30-o3-lto-benchmark-design.md):
> O3 was selected, O3+LTO was
> [statically rejected](../../bench/2026-07-30-1aa74ee-system-itcm-hot-o3-lto-static-rejection.md),
> and the human owner approved four deterministic O2/O3 cross-mode checksum
> differences as acceptable selection evidence. That override does not claim
> inaudibility, sound quality, or perceptual equivalence.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Compare the retained O2+DTCM+ITCM benchmark with bit-exact O3 and, only if necessary, O3+LTO builds, then apply the fastest correct compiler recipe to the production build.

**Architecture:** A generated optimization header gives Make a real dependency edge and gives the target a fail-closed `o2`, `o3`, or `o3-lto` wire identity. `BENCH_OPTIMIZATION` is the sole source of compiler mode, while the existing layout identity continues to prove ITCM use independently. Static linker checks precede two-run hardware comparisons; LTO is attempted only if O3 does not clear the budget or fails a static gate.

**Tech Stack:** GNU Make, GNU Arm Embedded 10.2.1, STM32H750/libDaisy, Python 3 `unittest`, OpenOCD/ST-Link, PowerShell evidence checks.

## Global Constraints

- Work only on `codex/perf-tcm-ladder`, never directly on `main`.
- Keep four voices per deck and do not change DSP expressions, parameters, sample rate, block size, or buffers.
- Keep `g_dtcm_instrument_storage` in DTCM and the accepted audio hotset in `0x00000100..0x0000ffff`.
- Treat the actual current `-O2` build as the control; do not activate the dormant `C_USR_FLAGS = -ffast-math -funroll-loops`.
- Accept only `o2`, `o3`, and `o3-lto` optimization identities.
- Require all 16 row checksums in each candidate run to match O2 exactly; the DTCM BBD gate remains `483e8e82`.
- Require at least 0.50 CPU-point savings in both average and maximum against both O2 runs.
- Collect two hardware runs for every viable measured variant from one committed source state, with a clean worktree and a receipt bound to the exact ELF.
- Stop only when both offline runs and the real callback are below 100 % average and maximum; otherwise continue to half-rate reverb.
- Use the exact final commit trailer `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`, with nothing after it.

## File Map

- `bench/write_bench_optimization.py`: generate the validated wire-identity header without touching its timestamp when content is unchanged.
- `bench/Makefile`: validate `BENCH_OPTIMIZATION`, select exact compile/link flags, and force a complete object rebuild when the mode changes.
- `bench/report.cpp`: append the optimization identity to `BENCH_BEGIN`.
- `bench/run.py`: request, parse, validate, and persist the optimization identity.
- `bench/test_run_contract.py`: host protocol, stale-image, repeat, requested-mode, and evidence-name contracts.
- `bench/test_optimization_make.py`: dry-run compiler/link command contract for all three modes.
- `bench/test_itcm_link.py`: mode-selectable ITCM/DTCM/load-segment contract.
- `bench/README.md`: user-facing commands and the corrected current-flag explanation.
- `Makefile`: adopt only the accepted production optimization recipe.
- `bench/test_task8_contract.py`: prove the production dry-run recipe matches the accepted winner.
- `docs/superpowers/specs/2026-07-30-o3-lto-benchmark-design.md`: append measured results and the keep/reject decision.
- `docs/roadmap.md`: record the retained optimization and whether half-rate reverb is next.
- `docs/bench/2026-07-30-*-system-itcm-hot-{o2,o3,o3-lto}.{md,csv}`: two-run hardware evidence.

---

### Task 1: Fail-closed optimization identity

**Files:**
- Create: `bench/write_bench_optimization.py`
- Modify: `bench/Makefile`
- Modify: `bench/report.cpp`
- Modify: `bench/run.py`
- Modify: `bench/test_run_contract.py`
- Modify: `bench/README.md`

**Interfaces:**
- Consumes: existing `BENCH_LAYOUT`, `build(..., itcm_hot=False)`, `parse(lines)`, `validate_captures(...)`, and `write_results(...)`.
- Produces: `BENCH_OPTIMIZATION` make value; generated macro `BENCH_OPTIMIZATION`; `build(families, itcm_hot=False, optimization="o2")`; parsed header key `optimization: str`; `validate_captures(..., expected_optimization=None)`; CLI `--optimization {o2,o3,o3-lto}`.

- [ ] **Step 1: Extend the capture helper and write failing protocol tests**

Add an `optimization="o2"` keyword to `capture_lines()` and make it emit the
new tenth `BENCH_BEGIN` field. Add these tests to the existing protocol/layout
classes:

```python
def test_parse_reads_layout_and_optimization_fields(self):
    lines = [
        "BENCH_BEGIN,abc1234,480000000,96,dcache+icache,"
        + "0" * 64
        + ",dead,system voice,itcm-hot,o3\n",
        "BENCH,system,empty_callback,2,12,0.00,0.00,ea306fb5\n",
        "BENCH_END\n",
    ]
    header, _rows, _anchors = runner.parse(lines)
    self.assertEqual(header["layout"], "itcm-hot")
    self.assertEqual(header["optimization"], "o3")

def test_parse_rejects_a_header_without_optimization(self):
    lines = [
        "BENCH_BEGIN,abc1234,480000000,96,dcache+icache,"
        + "0" * 64
        + ",dead,system voice,itcm-hot\n",
        "BENCH,system,empty_callback,2,12,0.00,0.00,ea306fb5\n",
        "BENCH_END\n",
    ]
    self.assertIsNone(runner.parse(lines))
```

- [ ] **Step 2: Write failing build and validation tests**

Add the build test to `ItcmLayoutContract`. Add the three validation tests to
`ProfileContract`, whose `self.system_rows()` helper supplies a complete
system capture:

```python
def test_build_requests_the_optimization_make_mode(self):
    with (
        mock.patch.object(runner.subprocess, "run") as run,
        mock.patch.object(
            runner, "prepare_existing_artifacts", return_value={}
        ),
    ):
        runner.build(("system",), itcm_hot=True, optimization="o3")
    self.assertIn(
        "BENCH_OPTIMIZATION=o3",
        run.call_args_list[1].args[0],
    )

def test_repeat_rejects_mixed_optimizations(self):
    rows = self.system_rows()
    o2 = runner.parse(capture_lines(rows, families="system", optimization="o2"))
    o3 = runner.parse(capture_lines(rows, families="system", optimization="o3"))
    with self.assertRaisesRegex(
        runner.BenchValidationError, "optimization differs"
    ):
        runner.validate_captures([o2, o3], resolve_profile("system"))

def test_requested_o3_rejects_an_o2_capture(self):
    rows = self.system_rows()
    capture = runner.parse(
        capture_lines(rows, families="system", optimization="o2")
    )
    with self.assertRaisesRegex(
        runner.BenchValidationError, "requested optimization o3"
    ):
        runner.validate_captures(
            [capture, capture],
            resolve_profile("system"),
            expected_optimization="o3",
        )

def test_unknown_reported_optimization_is_rejected(self):
    rows = self.system_rows()
    capture = runner.parse(
        capture_lines(rows, families="system", optimization="turbo")
    )
    with self.assertRaisesRegex(
        runner.BenchValidationError, "unknown optimization turbo"
    ):
        runner.validate_captures(
            [capture, capture],
            resolve_profile("system"),
        )
```

- [ ] **Step 3: Write failing persistence tests**

Add this test to `ProfileContract`:

```python
def test_evidence_persists_layout_and_optimization_identity(self):
    capture = runner.parse(
        capture_lines(
            self.system_rows(),
            families="system",
            layout="itcm-hot",
            optimization="o3",
        )
    )
    with tempfile.TemporaryDirectory() as temp:
        base = runner.write_results(
            temp, [capture, capture], resolve_profile("system"), "system"
        )
        with open(base + ".csv", encoding="utf-8") as stream:
            csv_text = stream.read()
        with open(base + ".md", encoding="utf-8") as stream:
            md_text = stream.read()
    self.assertTrue(base.endswith("-system-itcm-hot-o3"))
    self.assertIn(
        "run,profile,layout,optimization,qspi_sha256,device_fingerprint",
        csv_text,
    )
    self.assertIn("Optimization: `o3` (`-O3`).", md_text)
    self.assertNotIn("`-ffast-math -funroll-loops`", md_text)
```

- [ ] **Step 4: Run the focused tests and observe RED**

Run from `bench/`:

```powershell
python -m unittest `
  test_run_contract.ParseContract `
  test_run_contract.ItcmLayoutContract `
  test_run_contract.ProfileContract.test_repeat_rejects_mixed_optimizations `
  test_run_contract.ProfileContract.test_requested_o3_rejects_an_o2_capture `
  test_run_contract.ProfileContract.test_unknown_reported_optimization_is_rejected `
  test_run_contract.ProfileContract.test_evidence_persists_layout_and_optimization_identity
```

Expected: failures for the missing tenth field, unsupported function
arguments, missing make variable, and old evidence name/header.

- [ ] **Step 5: Add the generated optimization header**

Create `bench/write_bench_optimization.py`:

```python
#!/usr/bin/env python3
"""Update the generated benchmark optimization header only when it changed."""

from pathlib import Path
import sys


VALID = {"o2", "o3", "o3-lto"}


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[2] not in VALID:
        raise SystemExit(
            "usage: write_bench_optimization.py OUTPUT {o2|o3|o3-lto}"
        )
    output = Path(sys.argv[1])
    content = '#define BENCH_OPTIMIZATION "%s"\n' % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

Add before the core Makefile include:

```make
BENCH_OPTIMIZATION ?= o2

ifneq ($(filter $(BENCH_OPTIMIZATION),o2 o3 o3-lto),$(BENCH_OPTIMIZATION))
$(error BENCH_OPTIMIZATION must be o2, o3, or o3-lto)
endif
```

Add after the core Makefile include:

```make
$(BUILD_DIR)/bench_optimization.h: FORCE | $(BUILD_DIR)
	@python write_bench_optimization.py $@ $(BENCH_OPTIMIZATION)

$(BUILD_DIR)/report.o: $(BUILD_DIR)/bench_optimization.h
```

- [ ] **Step 6: Emit and parse the optimization identity**

In `report.cpp`, include `bench_optimization.h`, add a fail-safe `"unknown"`
macro, append one `%s` to `kBeginFormat`, and pass `BENCH_OPTIMIZATION` after
`BENCH_LAYOUT`.

In `run.py`, use:

```python
OPTIMIZATION_FLAGS = {
    "o2": "-O2",
    "o3": "-O3",
    "o3-lto": "-O3 -flto",
}


def build(families, itcm_hot=False, optimization="o2"):
```

Pass `"BENCH_OPTIMIZATION=%s" % optimization` to the benchmark make command.
Require exactly ten header fields and parse:

```python
"layout": f[8],
"optimization": f[9].strip(),
```

Track `first_optimization` alongside `first_layout`, reject mixed repeats,
reject values outside `OPTIMIZATION_FLAGS`, and reject
`optimization != expected_optimization` when an expected value is provided:

```python
optimization = header["optimization"]
if optimization not in OPTIMIZATION_FLAGS:
    raise BenchValidationError(
        "run %d reports unknown optimization %s"
        % (run_index, optimization)
    )
```

- [ ] **Step 7: Add the CLI and accurate evidence fields**

Add:

```python
ap.add_argument(
    "--optimization",
    default="o2",
    choices=tuple(OPTIMIZATION_FLAGS),
    help="compiler optimization identity carried by the firmware",
)
```

Pass it to `build()`, `validate_captures()`, filenames, CSV rows, and Markdown.
Replace the hard-coded `-ffast-math -funroll-loops` evidence claim with:

```python
fh.write(
    "Optimization: `%s` (`%s`).\n\n"
    % (
        header["optimization"],
        OPTIMIZATION_FLAGS[header["optimization"]],
    )
)
```

Document the three modes in `bench/README.md`, including that current
`C_USR_FLAGS` is dormant and is deliberately not corrected in this round.

- [ ] **Step 8: Run focused and full host tests GREEN**

Run from `bench/`:

```powershell
python -m unittest test_run_contract
python -m unittest `
  test_run_contract `
  test_task8_contract `
  test_qspi_guard `
  test_itcm_link
```

Expected: all tests pass with the new ten-field protocol.

- [ ] **Step 9: Commit the identity layer**

```powershell
git add -- `
  bench/Makefile `
  bench/report.cpp `
  bench/run.py `
  bench/test_run_contract.py `
  bench/write_bench_optimization.py `
  bench/README.md
git commit -m "bench(perf): add optimization identity" `
  -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

### Task 2: Exact compiler modes and static memory gates

**Files:**
- Create: `bench/test_optimization_make.py`
- Modify: `bench/Makefile`
- Modify: `bench/test_itcm_link.py`

**Interfaces:**
- Consumes: `BENCH_OPTIMIZATION`, generated `build/bench_optimization.h`, `OPT`, `LDFLAGS`, and the existing ITCM symbol list.
- Produces: exact mode recipes (`o2 -> -O2`, `o3 -> -O3`, `o3-lto -> -O3 -flto` plus link `-flto`); environment-selectable `BENCH_TEST_OPTIMIZATION` linker contract.

- [ ] **Step 1: Write the failing dry-run recipe contract**

Create `bench/test_optimization_make.py`:

```python
"""Compiler/link recipes for fail-closed benchmark optimization modes."""

from pathlib import Path
import subprocess
import unittest


HERE = Path(__file__).resolve().parent


def dry_run(mode):
    return subprocess.run(
        [
            "make",
            "-n",
            "-B",
            "BENCH_FAMILIES=system",
            "BENCH_ITCM_HOT=1",
            "BENCH_OPTIMIZATION=%s" % mode,
            "build/bench.elf",
        ],
        cwd=HERE,
        check=True,
        capture_output=True,
        text=True,
    ).stdout


class OptimizationMakeContract(unittest.TestCase):
    def test_o2_uses_only_the_current_optimization(self):
        recipe = dry_run("o2")
        self.assertIn(" -O2 ", recipe)
        self.assertNotIn(" -O3 ", recipe)
        self.assertNotIn("-flto", recipe)
        self.assertNotIn("-ffast-math", recipe)
        self.assertNotIn("-funroll-loops", recipe)

    def test_o3_uses_o3_without_lto_or_dormant_flags(self):
        recipe = dry_run("o3")
        self.assertIn(" -O3 ", recipe)
        self.assertNotIn(" -O2 ", recipe)
        self.assertNotIn("-flto", recipe)
        self.assertNotIn("-ffast-math", recipe)
        self.assertNotIn("-funroll-loops", recipe)

    def test_o3_lto_passes_lto_to_compile_and_link(self):
        recipe = dry_run("o3-lto")
        compile_lines = [
            line for line in recipe.splitlines()
            if " -c " in line and ("arm-none-eabi-gcc" in line
                                   or "arm-none-eabi-g++" in line)
        ]
        link_lines = [
            line for line in recipe.splitlines()
            if "arm-none-eabi-g++" in line and "build/bench.elf" in line
        ]
        self.assertTrue(compile_lines)
        self.assertTrue(link_lines)
        self.assertTrue(all(" -O3 " in line for line in compile_lines))
        self.assertTrue(all("-flto" in line for line in compile_lines))
        self.assertTrue(all("-flto" in line for line in link_lines))

    def test_every_object_depends_on_the_mode_header(self):
        makefile = (HERE / "Makefile").read_text(encoding="utf-8")
        self.assertIn(
            "$(OBJECTS): $(BUILD_DIR)/bench_optimization.h",
            makefile,
        )


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the recipe contract and observe RED**

Run:

```powershell
python -m unittest test_optimization_make
```

Expected: O3/LTO recipes are still O2 and the all-object dependency is absent.

- [ ] **Step 3: Make the optimization identity the sole flag source**

Add before the core Makefile include:

```make
ifeq ($(BENCH_OPTIMIZATION),o2)
override OPT := -O2
else ifeq ($(BENCH_OPTIMIZATION),o3)
override OPT := -O3
else
override OPT := -O3 -flto
endif
```

Add after the include:

```make
$(OBJECTS): $(BUILD_DIR)/bench_optimization.h

ifeq ($(BENCH_OPTIMIZATION),o3-lto)
LDFLAGS += -flto
endif
```

Keep `C_USR_FLAGS` unchanged so the comparison does not activate the dormant
flags.

- [ ] **Step 4: Make the linker contract mode-selectable**

In `bench/test_itcm_link.py`, add:

```python
import os

OPTIMIZATION = os.environ.get("BENCH_TEST_OPTIMIZATION", "o2")
if OPTIMIZATION not in {"o2", "o3", "o3-lto"}:
    raise RuntimeError(
        "BENCH_TEST_OPTIMIZATION must be o2, o3, or o3-lto"
    )
```

Pass `"BENCH_OPTIMIZATION=%s" % OPTIMIZATION` to make. Strengthen capacity
from `size <= 0x10000` to:

```python
self.assertLessEqual(0x100 + size, 0x10000)
```

The existing symbol, DTCM, VMA/LMA, and load-segment assertions remain
unchanged; missing or relocated LTO symbols are a deliberate RED rejection.

- [ ] **Step 5: Run O2 and O3 static gates**

Run from `bench/`:

```powershell
python -m unittest test_optimization_make
$env:BENCH_TEST_OPTIMIZATION = "o2"
python -m unittest test_itcm_link
$env:BENCH_TEST_OPTIMIZATION = "o3"
python -m unittest test_itcm_link
Remove-Item Env:BENCH_TEST_OPTIMIZATION
```

Expected: O2 and O3 both link; every representative symbol stays in ITCM,
the state stays in DTCM, and the ITCM section plus prefix remains within
65,536 bytes. If O3 fails a static gate, record the exact failure and skip
its hardware run; Task 4 then tries the LTO static gate.

- [ ] **Step 6: Run the full static/controller suite**

```powershell
python -m unittest `
  test_run_contract `
  test_task8_contract `
  test_qspi_guard `
  test_optimization_make `
  test_itcm_link
```

Expected: all tests pass in the default O2 mode.

- [ ] **Step 7: Commit the mode recipes and static gates**

```powershell
git add -- `
  bench/Makefile `
  bench/test_itcm_link.py `
  bench/test_optimization_make.py
git commit -m "bench(perf): add exact O3 and LTO modes" `
  -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

### Task 3: O2 and O3 hardware comparison

**Files:**
- Create: `docs/bench/2026-07-30-*-system-itcm-hot-o2.csv`
- Create: `docs/bench/2026-07-30-*-system-itcm-hot-o2.md`
- Create when O3 passes static gates: `docs/bench/2026-07-30-*-system-itcm-hot-o3.csv`
- Create when O3 passes static gates: `docs/bench/2026-07-30-*-system-itcm-hot-o3.md`

**Interfaces:**
- Consumes: committed Task 2 source, `run.py --optimization`, exact QSPI receipt, and the `instrument_worst_bbd_dtcm` gate.
- Produces: two clean O2 controls; when viable, two O3 captures; exact cross-build checksum and strict-minimum delta verdict.

- [ ] **Step 1: Verify the immutable measurement preconditions**

From the repository root, run:

```powershell
git branch --show-current
git status --short
git rev-parse --short HEAD
```

Expected: branch `codex/perf-tcm-ladder`, empty status, and a committed Task 2
HEAD. Do not measure a dirty tree.

- [ ] **Step 2: Create an external evidence directory**

```powershell
$perfCommit = git rev-parse --short HEAD
$perfEvidenceDir = Join-Path $env:TEMP "spotykach-o3-$perfCommit"
if (Test-Path -LiteralPath $perfEvidenceDir) {
  throw "evidence directory already exists: $perfEvidenceDir"
}
New-Item -ItemType Directory -Path $perfEvidenceDir
(Resolve-Path $perfEvidenceDir).Path
```

Verify the resolved directory is below the operating-system temporary
directory. Passing it through `--out-dir` keeps the worktree clean between
variants.

- [ ] **Step 3: Build, bind, and measure O2 twice**

From `bench/`:

```powershell
python run.py `
  --profile system `
  --itcm-hot `
  --optimization o2 `
  --program-qspi `
  --repeat 2 `
  --out-dir $perfEvidenceDir
```

Expected: two completed runs, reported layout `itcm-hot`, optimization `o2`,
gate checksum `483e8e82`, and evidence files ending
`-system-itcm-hot-o2.{md,csv}`.

- [ ] **Step 4: Reconfirm O3 static placement immediately before hardware**

```powershell
$env:BENCH_TEST_OPTIMIZATION = "o3"
python -m unittest test_itcm_link
Remove-Item Env:BENCH_TEST_OPTIMIZATION
```

If this fails, do not load O3 on hardware; save the failure output and proceed
to Task 4.

- [ ] **Step 5: Build, bind, and measure O3 twice**

Run only when Step 4 passes:

```powershell
python run.py `
  --profile system `
  --itcm-hot `
  --optimization o3 `
  --program-qspi `
  --repeat 2 `
  --out-dir $perfEvidenceDir
```

Expected: two completed runs with `itcm-hot`, `o3`, a freshly bound QSPI
receipt, and evidence files ending `-system-itcm-hot-o3.{md,csv}`.

- [ ] **Step 6: Prove all cross-build checksums are exact**

Point `$o2Csv` and `$o3Csv` at the two files printed by `run.py`, then run:

```powershell
$o2Rows = Import-Csv -LiteralPath $o2Csv
$o3Rows = Import-Csv -LiteralPath $o3Csv
$o2Checks = $o2Rows |
  Group-Object family,name |
  ForEach-Object {
    $values = @($_.Group.checksum | Sort-Object -Unique)
    if ($values.Count -ne 1) { throw "O2 checksum drift: $($_.Name)" }
    "$($_.Name)=$($values[0])"
  } |
  Sort-Object
$o3Checks = $o3Rows |
  Group-Object family,name |
  ForEach-Object {
    $values = @($_.Group.checksum | Sort-Object -Unique)
    if ($values.Count -ne 1) { throw "O3 checksum drift: $($_.Name)" }
    "$($_.Name)=$($values[0])"
  } |
  Sort-Object
$o2Groups = @($o2Rows | Group-Object family,name)
$o3Groups = @($o3Rows | Group-Object family,name)
if ($o2Groups.Count -ne 16 -or $o3Groups.Count -ne 16) {
  throw "expected 16 unique rows in each optimization mode"
}
$checksumDiff = Compare-Object $o2Checks $o3Checks
if ($checksumDiff) {
  $checksumDiff
  throw "O2/O3 checksum mismatch"
}
```

Expected: no output and no exception. Confirm the gate entry is
`instrument_worst_bbd_dtcm=483e8e82`.

- [ ] **Step 7: Calculate the strict four-way O3 gate delta**

```powershell
$gateName = "instrument_worst_bbd_dtcm"
$o2Gate = @($o2Rows | Where-Object name -eq $gateName)
$o3Gate = @($o3Rows | Where-Object name -eq $gateName)
if ($o2Gate.Count -ne 2 -or $o3Gate.Count -ne 2) {
  throw "expected two O2 and two O3 gate rows"
}
$avgSavings = foreach ($candidate in $o3Gate) {
  foreach ($control in $o2Gate) {
    [decimal]$control.pct_avg - [decimal]$candidate.pct_avg
  }
}
$maxSavings = foreach ($candidate in $o3Gate) {
  foreach ($control in $o2Gate) {
    [decimal]$control.pct_max - [decimal]$candidate.pct_max
  }
}
$minAvgSaving = ($avgSavings | Measure-Object -Minimum).Minimum
$minMaxSaving = ($maxSavings | Measure-Object -Minimum).Minimum
"strict minimum avg saving: $minAvgSaving"
"strict minimum max saving: $minMaxSaving"
```

O3 passes the keep threshold only when both printed minima are at least 0.50.
Read both O3 Markdown anchor tables and require
`instrument_worst_bbd_dtcm` average and maximum below 100 in both runs to
stop the ladder.

- [ ] **Step 8: Choose the next task**

- If O3 passes correctness, the 0.50-point threshold, and the offline plus
  callback 100 % budget, skip Task 4 and proceed to Task 5 with O3 selected.
- If O3 is correct and saves at least 0.50 points but either maximum remains
  at or above 100 %, retain it provisionally, execute Task 4, and select the
  faster correct candidate afterward.
- If O3 fails a static/checksum gate or saves less than 0.50 points, reject
  it, execute Task 4, and compare LTO with O2.

### Task 4: Conditional O3+LTO static and hardware gate

**Files:**
- Create when LTO passes static gates: `docs/bench/2026-07-30-*-system-itcm-hot-o3-lto.csv`
- Create when LTO passes static gates: `docs/bench/2026-07-30-*-system-itcm-hot-o3-lto.md`

**Interfaces:**
- Consumes: Task 2 `o3-lto` recipe and Task 3 O2 evidence.
- Produces: either an explicit static LTO rejection or two correct hardware runs and a strict-minimum delta verdict.

- [ ] **Step 1: Run the LTO static placement gate**

From `bench/`:

```powershell
$env:BENCH_TEST_OPTIMIZATION = "o3-lto"
python -m unittest test_itcm_link
Remove-Item Env:BENCH_TEST_OPTIMIZATION
```

If any representative symbol is absent/outside ITCM, DTCM moves, ITCM
overflows, the load segment disappears, or the link fails, record the exact
failure and reject LTO without hardware measurement.

- [ ] **Step 2: Measure LTO twice only after the static gate passes**

```powershell
python run.py `
  --profile system `
  --itcm-hot `
  --optimization o3-lto `
  --program-qspi `
  --repeat 2 `
  --out-dir $perfEvidenceDir
```

Expected: two completed `itcm-hot`/`o3-lto` runs and a receipt bound to the
exact LTO ELF.

- [ ] **Step 3: Apply the exact-checksum gate**

Point `$o3LtoCsv` at the LTO CSV printed by `run.py`, then run:

```powershell
$o2Rows = Import-Csv -LiteralPath $o2Csv
$o3LtoRows = Import-Csv -LiteralPath $o3LtoCsv
$o2Checks = $o2Rows |
  Group-Object family,name |
  ForEach-Object {
    $values = @($_.Group.checksum | Sort-Object -Unique)
    if ($values.Count -ne 1) { throw "O2 checksum drift: $($_.Name)" }
    "$($_.Name)=$($values[0])"
  } |
  Sort-Object
$o3LtoChecks = $o3LtoRows |
  Group-Object family,name |
  ForEach-Object {
    $values = @($_.Group.checksum | Sort-Object -Unique)
    if ($values.Count -ne 1) { throw "LTO checksum drift: $($_.Name)" }
    "$($_.Name)=$($values[0])"
  } |
  Sort-Object
$o2Groups = @($o2Rows | Group-Object family,name)
$o3LtoGroups = @($o3LtoRows | Group-Object family,name)
if ($o2Groups.Count -ne 16 -or $o3LtoGroups.Count -ne 16) {
  throw "expected 16 unique rows in each optimization mode"
}
$checksumDiff = Compare-Object $o2Checks $o3LtoChecks
if ($checksumDiff) {
  $checksumDiff
  throw "O2/LTO checksum mismatch"
}
```

Expected: no output and no exception. Confirm the gate entry is
`instrument_worst_bbd_dtcm=483e8e82`.

- [ ] **Step 4: Apply the strict LTO delta calculation**

```powershell
$gateName = "instrument_worst_bbd_dtcm"
$o2Gate = @($o2Rows | Where-Object name -eq $gateName)
$o3LtoGate = @($o3LtoRows | Where-Object name -eq $gateName)
if ($o2Gate.Count -ne 2 -or $o3LtoGate.Count -ne 2) {
  throw "expected two O2 and two LTO gate rows"
}
$avgSavings = foreach ($candidate in $o3LtoGate) {
  foreach ($control in $o2Gate) {
    [decimal]$control.pct_avg - [decimal]$candidate.pct_avg
  }
}
$maxSavings = foreach ($candidate in $o3LtoGate) {
  foreach ($control in $o2Gate) {
    [decimal]$control.pct_max - [decimal]$candidate.pct_max
  }
}
$minAvgSaving = ($avgSavings | Measure-Object -Minimum).Minimum
$minMaxSaving = ($maxSavings | Measure-Object -Minimum).Minimum
"strict minimum avg saving: $minAvgSaving"
"strict minimum max saving: $minMaxSaving"
```

LTO is retainable only when both printed minima are at least 0.50 CPU points.
Read both LTO Markdown anchor tables and apply the below-100 stop gate to
`instrument_worst_bbd_dtcm`.

- [ ] **Step 5: Select the fastest correct candidate**

Compare O3 and O3+LTO only when both passed all correctness and 0.50-point
gates. Select the mode with the lower `instrument_worst_bbd_dtcm` average and
maximum in both runs; if one wins average and the other wins maximum, select
the one with the lower worst maximum because the remaining failure is a peak
budget failure. If LTO failed statically or by checksum, it cannot be selected.

### Task 5: Production recipe, evidence, and final verification

**Files:**
- Modify conditionally: `Makefile`
- Modify conditionally: `bench/test_task8_contract.py`
- Modify: `docs/superpowers/specs/2026-07-30-o3-lto-benchmark-design.md`
- Modify: `docs/roadmap.md`
- Create: accepted/rejected evidence files copied from `$perfEvidenceDir` into `docs/bench/`

**Interfaces:**
- Consumes: selected mode and all evidence from Tasks 3--4.
- Produces: production build recipe for the winner, committed evidence, updated CPU-ladder decision, and clean final verification.

- [ ] **Step 1: Copy the complete evidence set into the repository**

Resolve and inspect both endpoints before copying:

```powershell
$resolvedEvidence = (Resolve-Path $perfEvidenceDir).Path
$resolvedBenchDocs = (Resolve-Path "..\docs\bench").Path
$resolvedEvidence
$resolvedBenchDocs
Get-ChildItem -LiteralPath $resolvedEvidence -File
```

From `bench/`, copy every measured mode's `.md` and `.csv`:

```powershell
$evidenceFiles = @(
  Get-ChildItem -LiteralPath $resolvedEvidence -File |
    Where-Object Extension -in ".md", ".csv"
)
foreach ($evidenceFile in $evidenceFiles) {
  Copy-Item -LiteralPath $evidenceFile.FullName `
    -Destination $resolvedBenchDocs
}
```

Check that `$evidenceFiles.Count` is four when two modes were measured or six
when all three modes were measured. Do not copy incomplete captures.

- [ ] **Step 2: Write the failing production-recipe contract when a candidate wins**

If O3 wins, add to `Task8Contract`:

```python
def test_shipping_recipe_uses_the_accepted_o3_mode(self) -> None:
    root_makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    self.assertRegex(root_makefile, r"(?m)^OPT\s*=\s*-O3$")
    self.assertNotRegex(root_makefile, r"(?m)^LDFLAGS\s*\+=\s*-flto$")
```

If O3+LTO wins, use:

```python
def test_shipping_recipe_uses_the_accepted_o3_lto_mode(self) -> None:
    root_makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    self.assertRegex(root_makefile, r"(?m)^OPT\s*=\s*-O3 -flto$")
    self.assertRegex(root_makefile, r"(?m)^LDFLAGS\s*\+=\s*-flto$")
```

If O2 wins, do not add a production-recipe test or change the root Makefile;
the existing libDaisy `OPT ?= -O2` remains the selected recipe.

- [ ] **Step 3: Run the winning production-recipe test RED**

For an O3 or O3+LTO winner, run:

```powershell
python -m unittest test_task8_contract.Task8Contract.test_shipping_recipe_uses_the_accepted_o3_mode
```

Use the `...accepted_o3_lto_mode` method name when LTO wins. Expected: FAIL
until the root Makefile contains the selected exact recipe.

- [ ] **Step 4: Apply only the accepted production recipe**

For O3, add before the root Makefile includes libDaisy:

```make
OPT = -O3
```

For O3+LTO, add:

```make
OPT = -O3 -flto
LDFLAGS += -flto
```

Do not rename `C_USR_FLAGS`; activating those dormant flags would make this
evidence inapplicable.

- [ ] **Step 5: Verify the production recipe and firmware link**

For O3 or O3+LTO, run the focused test GREEN. Then, from the repository root,
run:

```powershell
make -j8 build/spotykach.elf
```

Expected: successful production ELF, no memory-region overflow, and a map at
`build/spotykach.map`. Inspect the make output/map to confirm the selected
flags and linked memory usage. Run the production build for an O2 winner too,
using the unchanged Makefile. If an added O3/O3+LTO product recipe fails,
remove only those newly added lines with a focused patch and select O2; keep
the benchmark evidence as a documented rejection.

- [ ] **Step 6: Record exact results and decision**

Append to the design:

- O2, O3, and measured LTO gate averages/maxima for both runs;
- all strict minimum savings;
- checksum verdict and `483e8e82` gate confirmation;
- ITCM section size and DTCM symbol placement per viable mode;
- callback averages/maxima;
- selected/rejected reason for each mode;
- production build result;
- whether the next step is stop or half-rate reverb.

Add the same concise decision and evidence links near the current ITCM update
in `docs/roadmap.md`.

- [ ] **Step 7: Run final verification**

From `bench/`:

```powershell
python -m unittest `
  test_run_contract `
  test_task8_contract `
  test_qspi_guard `
  test_optimization_make `
  test_itcm_link
```

From the repository root:

```powershell
git diff --check
git status --short
```

Expected: all controller/static tests pass; `git diff --check` is silent;
status contains only the intended source, documentation, and evidence files.

- [ ] **Step 8: Commit the retained result**

Stage the exact intended files printed by `git status --short`, then commit:

```powershell
git commit -m "bench(perf): retain compiler optimization result" `
  -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

- [ ] **Step 9: Verify the committed state**

```powershell
git status --short
git show --stat --oneline HEAD
git log -1 --format="%B"
```

Expected: clean worktree, intended evidence/result commit, and the HAL trailer
as the final commit paragraph.
