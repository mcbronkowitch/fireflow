# ITCM audio-hotset implementation plan

**Goal:** Move the measured instrument's per-sample audio code into ITCM
without changing sound or voice count, then collect fail-closed AXI/ITCM
hardware evidence.

**Architecture:** A build-generated layout header identifies AXI versus ITCM
on the wire. A supplementary linker script moves a pre-registered object
hotset only in `BENCH_ITCM_HOT=1` builds. OpenOCD loads the linked ITCM segment
directly. The existing DTCM instrument row remains the decision gate.

**Tech stack:** GNU Arm Embedded 10.2.1, GNU ld supplementary scripts,
STM32H750/libDaisy, Python `unittest`, OpenOCD.

---

### Task 1: Fail-closed layout identity

**Files:**
- Modify: `bench/test_run_contract.py`
- Modify: `bench/run.py`
- Modify: `bench/report.cpp`
- Modify: `bench/Makefile`
- Create: `bench/write_bench_layout.py`

- [ ] Write tests that reject an old header without layout, reject mixed
  layouts across repeats, and require `--itcm-hot` to request
  `BENCH_ITCM_HOT=1`.
- [ ] Run the focused tests and observe RED.
- [ ] Generate `build/bench_layout.h` with `BENCH_LAYOUT "axi"` or
  `"itcm-hot"` and a real prerequisite on `report.o`.
- [ ] Append the layout to `BENCH_BEGIN`; parse and persist it.
- [ ] Add `--itcm-hot`, pass the make variable, and reject a requested/reported
  mismatch before evidence is written.
- [ ] Run the focused and full 79-test controller suite GREEN.

### Task 2: Supplementary ITCM placement

**Files:**
- Create: `bench/itcm_hot.lds`
- Modify: `bench/Makefile`

- [ ] Add a linker-contract test that expects representative hot symbols in
  ITCM when the mode is enabled.
- [ ] Run it against the AXI build and observe RED.
- [ ] Add `.itcm_audio_hot`, selecting the ten object files from the design,
  and insert it before `.text`.
- [ ] Add the supplementary script to `LDFLAGS` only for
  `BENCH_ITCM_HOT=1`.
- [ ] Build AXI and ITCM variants. Confirm both fit all memory regions.
- [ ] Verify representative symbols and DTCM data placement with
  `arm-none-eabi-nm`; verify `bench-sram.elf` contains the ITCM load segment.
- [ ] Run desktop build plus controller tests and commit.

### Task 3: Hardware evidence

- [ ] Build the committed AXI control, bind QSPI, collect two runs.
- [ ] Confirm it is within 0.25 points of `8702bc8`.
- [ ] Build the committed ITCM variant, bind QSPI, collect two runs.
- [ ] Confirm identical checksums and apply the 1.00-point decision rule.
- [ ] Record both evidence sets, the delta table, capacity, and retain/revert
  decision in the design and roadmap.
- [ ] Run final verification and commit the evidence.
