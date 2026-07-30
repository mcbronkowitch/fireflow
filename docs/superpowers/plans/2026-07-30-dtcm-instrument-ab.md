# DTCM Instrument A/B Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure the exact worst-case instrument from DTCM and AXI SRAM inside one firmware image without changing voices, sound, or DSP behavior.

**Architecture:** Add a raw aligned DTCM allocation for one `Instrument`, construct it explicitly during setup, and give both rows the same counter and output arrays in AXI SRAM. Route both rows through one active-instrument pointer and one process callback. Extend the fail-closed host controller so unequal A/B checksums invalidate the capture, then collect offline and real-callback hardware evidence.

**Tech Stack:** C++17, STM32H750/libDaisy, GNU Arm Embedded 10.2.1, Python `unittest`, OpenOCD bench controller.

## Global Constraints

- Work on `codex/perf-tcm-ladder`, never directly on `main`.
- Do not change `engine/` behavior or any voice count.
- Do not rename, remove, or change the original `instrument_worst_bbd` row.
- Never run `bench/run.py` without an explicit `--profile`.
- Build, bind the QSPI receipt, then measure, in that order.
- Hardware evidence requires a clean Git tree and two runs.
- Commit trailer is exactly `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`, with nothing after it.

---

### Task 1: Fail-closed DTCM A/B protocol

**Files:**
- Modify: `bench/test_run_contract.py`
- Modify: `bench/run.py`

**Interfaces:**
- Consumes: `validate_captures(captures, profile)` and `BENCH_PROTOCOL_ROWS_BY_FAMILY`.
- Produces: a `system/instrument_worst_bbd_dtcm` protocol row and per-run equality validation against `instrument_worst_bbd`.

- [ ] **Step 1: Write the failing checksum-equality test**

Add a test to `ProfileContract` that constructs a complete `system` capture
plus `instrument_worst_bbd_dtcm`, gives the AXI and DTCM rows different literal
checksums, and asserts:

```python
with self.assertRaisesRegex(
    runner.BenchValidationError,
    "DTCM A/B checksum mismatch",
):
    runner.validate_captures([capture, capture], resolve_profile("system"))
```

The production change this catches is accepting a DTCM row that computes
different audio from the AXI control.

Update the two current-profile system fixtures (`ProfileContract.system_rows`
and
`ProfileAwareEvidenceContract.test_an_ungated_profile_does_not_claim_the_wave_gate_passed`)
so the DTCM row copies the AXI row's literal checksum. Historical
`PRE_BODY_ROWS_BY_FAMILY` fixtures remain untouched because they describe a
real protocol from before this row existed.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```powershell
cd bench
python -m unittest test_run_contract.ProfileContract.test_dtcm_ab_rejects_unequal_checksums
```

Expected: FAIL because the new row is not yet part of the protocol and no
DTCM-specific equality gate exists.

- [ ] **Step 3: Add the protocol row and equality gate**

Append `"instrument_worst_bbd_dtcm"` after `"instrument_worst_bbd"` in
`BENCH_PROTOCOL_ROWS_BY_FAMILY["system"]`.

Inside `validate_captures`, after the complete row-set check and before
cross-run checksum comparison, use `by_name(rows)` and reject a run when both
gate rows exist but their `checksum` fields differ. The error must name the run
and both checksums:

```python
raise BenchValidationError(
    "run %d DTCM A/B checksum mismatch: AXI %s, DTCM %s"
    % (run_index, axi["checksum"], dtcm["checksum"])
)
```

- [ ] **Step 4: Run the focused and full controller tests**

Run:

```powershell
cd bench
python -m unittest test_run_contract.ProfileContract.test_dtcm_ab_rejects_unequal_checksums
python -m unittest test_run_contract
```

Expected: the focused test passes and the full controller suite reports zero
failures.

- [ ] **Step 5: Commit the protocol gate**

```powershell
git add bench/test_run_contract.py bench/run.py
git commit -m "test(bench): require identical DTCM gate output" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

### Task 2: Same-code AXI/DTCM workload pair

**Files:**
- Modify: `bench/workloads_system.cpp`
- Modify: `bench/anchor.cpp`

**Interfaces:**
- Consumes: `InstrumentGroup`, `setup_inst_worst_bbd`, `proc_inst`, and linker section `.dtcmram_bss`.
- Produces: workload `system/instrument_worst_bbd_dtcm`; both A/B workloads use the identical `proc_inst` function.

- [ ] **Step 1: Add the new workload row without storage**

Append this entry after the existing gate:

```cpp
{ "system", "instrument_worst_bbd_dtcm",
  setup_inst_worst_bbd_dtcm, proc_inst },
```

Declare a stub `setup_inst_worst_bbd_dtcm()` that deliberately fails at build
time with an unresolved call to `dtcm_instrument_group_not_implemented()`.

- [ ] **Step 2: Run build-only and verify RED**

Run:

```powershell
cd bench
python run.py --profile system --build-only
```

Expected: link failure naming `dtcm_instrument_group_not_implemented`.

- [ ] **Step 3: Implement raw DTCM storage and shared setup**

In `workloads_system.cpp`:

1. Include `<cstddef>` and `<new>`.
2. Define an ARM-only section macro:

```cpp
#if defined(__ARM_EABI__)
#define BENCH_DTCM_BSS __attribute__((section(".dtcmram_bss")))
#else
#define BENCH_DTCM_BSS
#endif
```

3. Define raw storage and its lifetime rule:

```cpp
struct DtcmInstrumentStorage {
    alignas(Instrument)
        unsigned char bytes[sizeof(Instrument)];
};

DtcmInstrumentStorage BENCH_DTCM_BSS g_dtcm_instrument_storage;
Instrument* g_dtcm_instrument = nullptr;
Instrument* g_active_instrument = nullptr;
InstrumentHarness g_instrument_harness;
```

4. Construct the DTCM instrument with placement `new` on every DTCM setup. If
   `g_dtcm_instrument` is non-null, destroy that live instrument first,
   matching `SerialArena::reset()`. Reset the arena before constructing the
   DTCM object, and destroy the DTCM object before constructing an AXI object,
   so only one instrument is live. Never read retained NOLOAD bytes before
   construction; after a debug reset the ordinary-BSS pointer is null.
5. Refactor the existing AXI setup into shared helpers taking `Instrument&`.
   Both BBD rows call those helpers and use the exact same AXI-resident
   `InstrumentHarness`.
6. Set `g_active_instrument` during every instrument setup.
7. Make `proc_inst()` read only `*g_active_instrument` plus the common harness;
   it must contain no AXI/DTCM branch.

In `anchor.cpp`, add both `instrument_worst_bbd` rows to `kAnchorNames` and
update `kAnchorCount` from the array size rather than a hand-maintained literal.
Keep the DTCM row before the AXI row so the more likely under-budget segment is
heard first.

- [ ] **Step 4: Build and verify GREEN**

Run:

```powershell
cd bench
python run.py --profile system --build-only
```

Expected: exit 0, no new warnings, DTCMRAM below 131,072 bytes, SRAM and
SRAM_EXEC below their limits.

- [ ] **Step 5: Verify the linked placement and shared callback**

Run:

```powershell
arm-none-eabi-nm --print-size -C bench/build/bench.elf
```

Verify:

- `g_dtcm_instrument_storage` starts in `0x20000000..0x2001ffff`;
- its size is exactly `sizeof(Instrument)` and approximately 49 KiB;
- `g_system_arena` remains in `0x24000000..0x2407ffff`;
- the workload table points both A/B names at the same `proc_inst` symbol.

- [ ] **Step 6: Run desktop and controller verification**

Run:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
cd bench
python -m unittest test_run_contract test_task8_contract test_qspi_guard
```

Expected: all commands exit 0 and all tests report zero failures.

- [ ] **Step 7: Commit the workload**

```powershell
git add bench/workloads_system.cpp bench/anchor.cpp
git commit -m "bench(dtcm): add same-binary instrument gate" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

### Task 3: Hardware evidence and decision

**Files:**
- Create: the hash-named `system` CSV emitted by `bench/run.py` under `docs/bench`
- Create: the matching hash-named `system` Markdown report emitted by `bench/run.py`
- Modify: `docs/superpowers/specs/2026-07-30-dtcm-instrument-ab-design.md`
- Modify: `docs/roadmap.md`

**Interfaces:**
- Consumes: committed clean-tree system profile with both A/B rows.
- Produces: two-run offline and anchored A/B measurements plus a keep/revert decision.

- [ ] **Step 1: Build the exact committed source**

Run:

```powershell
cd bench
python run.py --profile system --build-only
```

Expected: exit 0 with the committed short hash embedded in the image.

- [ ] **Step 2: Bind the QSPI receipt**

Run:

```powershell
cd bench
python run.py --profile system --no-build --program-qspi --build-only
```

Expected: target programming and byte verification succeed and
`build/qspi-verified.json` is refreshed for the exact ELF artifacts.

- [ ] **Step 3: Collect two hardware runs**

Run:

```powershell
cd bench
python run.py --profile system --repeat 2
```

Expected: both runs reach `BENCH_END`; the controller accepts complete unique
row sets, identical per-row repeat checksums, equal AXI/DTCM checksums, matching
QSPI digest, and matching device fingerprint.

- [ ] **Step 4: Apply the pre-registered decision rule**

For each run calculate AXI minus DTCM for `pct_avg` and `pct_max`, then calculate
the same deltas for the two anchor rows. Record all raw values, both deltas, and
their run-to-run spreads in the design document.

Keep DTCM only when both offline metrics save at least 0.50 points in both runs,
their directions agree, and each saving's spread is at most 0.25 points.
Otherwise revert the DTCM workload implementation after preserving the evidence
and record that data placement was not material.

- [ ] **Step 5: Update roadmap and commit evidence**

Record the DTCM result and whether the next round is ITCM. Then run:

```powershell
git add docs/bench docs/superpowers/specs/2026-07-30-dtcm-instrument-ab-design.md docs/roadmap.md
git commit -m "docs(dtcm): record instrument placement result" -m "Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

- [ ] **Step 6: Final verification**

Run:

```powershell
git status --short
git diff --check HEAD~1 HEAD
cd bench
python -m unittest test_run_contract test_task8_contract test_qspi_guard
```

Expected: clean tree, no whitespace errors, and zero test failures.
