# Bench QSPI SRAM Programmer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Program and verify the raw WAVE bank in external QSPI through a temporary ST-Link-loaded SRAM helper, without touching internal flash or changing benchmark memory placement.

**Architecture:** A small standalone BOOT_SRAM helper receives the raw payload at `0x24040000`, erases external-QSPI offset `0x40000`, writes exactly `0xfe00` bytes, compares every mapped byte, and reports SHA-256 plus MCU UID over semihosting. Python orchestration builds and validates the helper, loads both RAM artifacts with OpenOCD, parses the exact success record, and creates an artifact-bound receipt; the normal benchmark's live SHA remains an independent guard.

**Tech Stack:** C++17, libDaisy `DaisySeed`/`QSPIHandle`, ARM semihosting, OpenOCD/ST-Link, Python `unittest`, CMake/doctest, GNU Arm Embedded toolchain.

## Global Constraints

- Do not write STM32 internal flash.
- Load the helper below `0x24040000`; stage the payload at exactly `0x24040000`.
- Erase exactly one external-QSPI 64 KiB block at offset `0x40000`.
- Write and compare exactly `0xfe00` bytes at external-QSPI offset `0x40000` / mapped address `0x90040000`.
- Preserve `bench.elf`, `g_sram`, `g_system_arena`, and the benchmark linker layout.
- Bind the receipt to bench ELF, SRAM ELF, QSPI payload, helper ELF, and target MCU UID.
- Retain the normal benchmark's live mapped-QSPI SHA-256 check.
- Do not execute hardware programming commands in the implementation session.

---

### Task 1: Target-Side Programming Core

**Files:**
- Create: `bench/qspi_programmer/program_core.h`
- Create: `tests/test_qspi_programmer.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: a device with `bool erase_block(uint32_t)`, `bool write(uint32_t, uint32_t, uint8_t*)`, and `void invalidate(const void*, size_t)`.
- Produces: `bench::qspi_programmer::program(Device&, uint8_t*, const volatile uint8_t*) -> Result`, plus constants `kQspiOffset`, `kMappedAddress`, `kPayloadSize`, `kStagingAddress`.

- [ ] **Step 1: Write failing exact-operation tests**

Add doctests that use a fake device and independently literal expectations:

```cpp
CHECK(fake.erased_offset == 0x40000u);
CHECK(fake.write_offset == 0x40000u);
CHECK(fake.write_size == 0xfe00u);
CHECK(result == Result::Ok);
```

Add separate tests for erase failure, write failure, and a one-byte mapped mismatch. The fake must copy the supplied staging bytes only when write succeeds.

- [ ] **Step 2: Run the focused desktop build/test and observe RED**

Run:

```powershell
cmake --build build --target spky_tests -j 4
ctest --test-dir build --output-on-failure
```

Expected: compilation fails because `bench/qspi_programmer/program_core.h` does not exist.

- [ ] **Step 3: Implement the minimal generic core**

Define:

```cpp
enum class Result { Ok, EraseFailed, WriteFailed, CompareFailed };

template <class Device>
Result program(Device& device,
               uint8_t* staging,
               const volatile uint8_t* mapped)
{
    if (!device.erase_block(0x40000u)) return Result::EraseFailed;
    if (!device.write(0x40000u, 0xfe00u, staging))
        return Result::WriteFailed;
    device.invalidate((const void*)mapped, 0xfe00u);
    for (size_t i = 0; i < 0xfe00u; ++i)
        if (mapped[i] != staging[i]) return Result::CompareFailed;
    return Result::Ok;
}
```

Use named constants in production; the tests retain independent literals.

- [ ] **Step 4: Run focused CTest and confirm GREEN**

Run the commands from Step 2. Expected: 2/2 CTest targets pass.

---

### Task 2: Standalone SRAM Helper Firmware

**Files:**
- Create: `bench/qspi_programmer/Makefile`
- Create: `bench/qspi_programmer/main.cpp`
- Create: `bench/openocd/qspi-programmer.cfg`
- Modify: `bench/test_task8_contract.py`

**Interfaces:**
- Consumes: Task 1 `program_core.h`, parent `bench/qspi_digest.cpp`, libDaisy, raw payload staged at `0x24040000`.
- Produces: `bench/qspi_programmer/build/qspi_programmer.elf` and semihost records `QSPI_PROGRAM_OK,90040000,65024,<sha>,<uid>` or `QSPI_PROGRAM_ERROR,<stage>`.

- [ ] **Step 1: Add failing helper/source/OpenOCD contract tests**

Test observable build/config contracts:

- helper source uses `DaisySeed::Init`, `EraseBlock(0x40000)`, `Write(..., 0xfe00, ...)` through the core adapter;
- OpenOCD contains `load_image $PROGRAMMER`, `load_image $PAYLOAD 0x24040000 bin`, FPU enable, vector setup at `0x24000000`, semihost enable, and no `flash write`, `program`, or internal-flash address;
- Makefile selects BOOT_SRAM and builds only helper sources plus `../qspi_digest.cpp`.

- [ ] **Step 2: Run contracts and observe RED**

Run:

```powershell
python -m unittest bench.test_task8_contract -v
```

Expected: failures for missing helper files/config.

- [ ] **Step 3: Implement helper firmware**

Use the standard libDaisy SRAM linker and a small helper. Stamp recognized boot info before `hw.Init()` as the benchmark does. Adapt `hw.qspi`:

```cpp
bool erase_block(uint32_t offset) {
    return hw.qspi.EraseBlock(offset, false) == daisy::QSPIHandle::OK;
}
bool write(uint32_t offset, uint32_t size, uint8_t* data) {
    return hw.qspi.Write(offset, size, data) == daisy::QSPIHandle::OK;
}
```

After `program()` returns `Ok`, hash the volatile mapped bytes with
`sha256_hex`, format UID words from `UID_BASE`, and semihost the exact success
record. All failures emit one exact error stage and loop.

- [ ] **Step 4: Implement the RAM-only OpenOCD script**

The config must `reset halt`, load the helper ELF and raw payload, enable FPU,
set VTOR/MSP/PC from `0x24000000`, enable semihosting, and resume. It must
contain no flash programming command.

- [ ] **Step 5: Build the ARM helper and prove placement**

Run:

```powershell
$env:PATH='C:\Program Files\Git\usr\bin;'+$env:PATH
make -C bench/qspi_programmer -j8 build/qspi_programmer.elf
& 'C:\Program Files\DaisyToolchain\bin\arm-none-eabi-readelf.exe' -l bench/qspi_programmer/build/qspi_programmer.elf
```

Expected: link succeeds; every nonempty load segment's VMA/LMA range ends at
or below `0x24040000`, with no section in internal flash or QSPI.

- [ ] **Step 6: Run Task 1 CTest and Task 2 contracts**

Expected: all pass.

---

### Task 3: Host OpenOCD Orchestration and Receipt

**Files:**
- Modify: `bench/qspi_tools.py`
- Modify: `bench/test_qspi_guard.py`
- Modify: `bench/run.py`

**Interfaces:**
- Consumes: helper ELF, OpenOCD config, current artifact identity, payload.
- Produces: `validate_programmer_elf(...)`, `parse_programmer_result(...)`, `program_and_verify(...)`, and a receipt with verification mode `swd-sram-target-byte-identity`, helper SHA, and MCU UID.

- [ ] **Step 1: Replace DFU tests with failing OpenOCD behavior tests**

Add literal tests for:

- command includes helper ELF, payload, `qspi-programmer.cfg`, and no DFU tool;
- helper overlap at `0x24040000` is rejected before launch;
- exact success record creates a receipt bound to helper SHA and UID;
- wrong address, size, SHA, malformed UID, error marker, and timeout create no receipt.

The fake boundary returns captured OpenOCD lines; assertions target receipt
existence/content and raised `QspiGuardError`, not fake call counts.

- [ ] **Step 2: Run focused tests and observe RED**

Run:

```powershell
python -m unittest bench.test_qspi_guard -v
```

Expected: failures because the DFU implementation has no helper validation or
OpenOCD parser.

- [ ] **Step 3: Implement helper ELF validation**

Parse `objdump -h` loadable sections and reject any VMA or LMA range that
overlaps staging address `0x24040000`, internal flash, or QSPI. Return helper
SHA-256 for insertion into artifact identity.

- [ ] **Step 4: Implement exact result parsing and capture**

Accept only:

```text
QSPI_PROGRAM_OK,90040000,65024,[0-9a-f]{64},[0-9a-fA-F]{24}
```

Require the reported SHA to equal the current payload SHA. A target error,
process exit, or timeout raises `QspiGuardError`.

- [ ] **Step 5: Replace DFU receipt flow**

Remove DFU listing/download/upload code and messages. Build an OpenOCD command
with `PROGRAMMER` and `PAYLOAD` Tcl variables, run until the status marker,
and write the receipt only after exact parse/validation. Normalize UID to
lowercase and store it as `device_id`.

- [ ] **Step 6: Integrate run.py**

Build the helper alongside the bench unless `--no-build`; even with
`--no-build`, validate it and add its SHA to artifact identity. Route
`--program-qspi` through OpenOCD/ST-Link, using the selected `--interface`.
Normal measurement continues to call `require_verified_payload` and
`require_live_digest`.

- [ ] **Step 7: Run focused and full Python suites**

Run:

```powershell
python -m unittest bench.test_qspi_guard bench.test_task8_contract -v
python tools\bake_wavetables.py --check
```

Expected: all pass.

---

### Task 4: Documentation, Full Builds, and Review

**Files:**
- Modify: `bench/README.md`
- Modify: `.superpowers/sdd/task-8-report.md` (ignored evidence file)

**Interfaces:**
- Consumes: completed Tasks 1-3 and their actual command output.
- Produces: accurate ST-Link-only programming instructions and verified build/map evidence.

- [ ] **Step 1: Replace invalid DFU documentation**

Document that `--program-qspi` loads a temporary SRAM helper and payload
through ST-Link, erases one external 64 KiB block at offset `0x40000`, writes
no internal flash, target-compares exact bytes, and records SHA/UID. Remove
BOOT/RESET/DFU instructions and DFU error messages.

- [ ] **Step 2: Run full verification**

Run:

```powershell
python -m unittest bench.test_qspi_guard bench.test_task8_contract -v
cmake --build build --target spky_tests -j 4
ctest --test-dir build --output-on-failure
$env:PATH='C:\Program Files\Git\usr\bin;'+$env:PATH
python bench\run.py --build-only
git diff --check
```

Also inspect helper and benchmark program headers/maps. Do not invoke
`--program-qspi`.

- [ ] **Step 3: Self-review**

Verify line by line:

- no internal-flash programming command;
- exact offset/address/size constants;
- helper and staging do not overlap;
- receipt is impossible on every failure path;
- helper hash and UID are bound;
- normal live SHA guard is unchanged;
- benchmark memory-region usage and symbol placement are unchanged.

- [ ] **Step 4: Commit implementation**

Stage only implementation/test/docs files and commit:

```text
bench(wave): program raw QSPI bank through SRAM helper
```
