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

- [x] Write tests that reject an old header without layout, reject mixed
  layouts across repeats, and require `--itcm-hot` to request
  `BENCH_ITCM_HOT=1`.
- [x] Run the focused tests and observe RED.
- [x] Generate `build/bench_layout.h` with `BENCH_LAYOUT "axi"` or
  `"itcm-hot"` and a real prerequisite on `report.o`.
- [x] Append the layout to `BENCH_BEGIN`; parse and persist it.
- [x] Add `--itcm-hot`, pass the make variable, and reject a requested/reported
  mismatch before evidence is written.
- [x] Run the focused and full controller suite GREEN.

### Task 2: Supplementary ITCM placement

**Files:**
- Create: `bench/itcm_hot.lds`
- Modify: `bench/Makefile`

- [x] Add a linker-contract test that expects representative hot symbols in
  ITCM when the mode is enabled.
- [x] Run it against the AXI build and observe RED.
- [x] Add `.itcm_audio_hot`, selecting the ten object files from the design,
  before the base script consumes their `.text` sections.
- [x] Add the supplementary script to `LDFLAGS` only for
  `BENCH_ITCM_HOT=1`.
- [x] Build AXI and ITCM variants. Confirm both fit all memory regions.
- [x] Verify representative symbols and DTCM data placement with
  `arm-none-eabi-nm`; verify `bench-sram.elf` contains the ITCM load segment.
- [x] Diagnose the null-address hardware failure and reserve
  `0x00000000..0x000000ff`; two diagnostic runs restored every AXI checksum.
- [x] Run desktop build plus controller tests and commit.

### Task 3: Hardware evidence

- [x] Build the committed AXI control, bind QSPI, collect two runs.
- [x] Confirm it is within 0.25 points of `8702bc8`.
- [x] Build the committed ITCM variant, bind QSPI, collect two runs.
- [x] Confirm identical checksums and apply the 1.00-point decision rule.
- [x] Record both evidence sets, the delta table, capacity, and retain/revert
  decision in the design and roadmap.
- [x] Run final verification and commit the evidence.
