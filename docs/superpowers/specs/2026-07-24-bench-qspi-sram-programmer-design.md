# Bench QSPI SRAM Programmer Design

## Problem

The Daisy bootloader DFU endpoint accepts a bootable application at its QSPI
app slot. It reports success for a raw 65,024-byte wavetable bank download at
`0x90040000`, but repeat uploads return stable bytes that do not match the
payload. A raw data section is therefore not a valid DFU application image.

Task 8 still needs to place the exact `bench-qspi.bin` bytes at
`0x90040000`, prove them there, and leave internal flash and the benchmark's
memory layout untouched.

## Architecture

Add a separate, temporary `bench/qspi_programmer` BOOT_SRAM application. It
is a programming tool, not part of `bench.elf`.

OpenOCD performs two RAM-only loads:

1. Load `qspi_programmer.elf` at its linked SRAM addresses beginning at
   `0x24000000`.
2. Load `bench-qspi.bin` as raw bytes at the fixed staging address
   `0x24040000`.

The programmer image must have no loadable section at or above the staging
address. The host validates that constraint before running it. Neither step
programs STM32 internal flash.

## Target Operation

After normal Daisy Seed initialization, the programmer:

1. Treats `0x24040000` as a 65,024-byte source buffer.
2. Erases one 64 KiB external-QSPI block at offset `0x40000`.
3. Writes exactly `0xfe00` bytes at QSPI offset `0x40000`.
4. Invalidates the cache for mapped QSPI address `0x90040000`.
5. Compares all 65,024 mapped bytes against the staging buffer.
6. Computes SHA-256 from the mapped QSPI bytes.
7. Reads the STM32 96-bit unique device ID.
8. Emits exactly one semihosting result:

```text
QSPI_PROGRAM_OK,90040000,65024,<64 lowercase hex SHA>,<24 hex UID>
```

Any initialization, erase, write, compare, or digest failure emits
`QSPI_PROGRAM_ERROR,<stage>` and never emits the success record.

The expected payload SHA remains the raw binary digest
`0163e3ba4988f5769eece514be01fbf48e134af4b407b0710762f81356f20f82`
for the current bank. The host derives the expected digest from the current
payload rather than hard-coding it.

## Host Orchestration

`run.py --program-qspi` uses OpenOCD and the ST-Link, not DFU:

- build or validate the helper ELF;
- reject a helper image overlapping `0x24040000`;
- load the helper ELF and raw payload into SRAM;
- enable semihosting and start from the helper vector table;
- require the exact success address, size, SHA, and UID syntax;
- write `qspi-verified.json` only after the target reports byte comparison
  success and the expected SHA.

The receipt remains bound to the authoritative bench ELF, SRAM derivative,
payload, and helper ELF hashes. Its device identity is the target-reported
MCU UID. A later normal benchmark run independently hashes the live
memory-mapped QSPI bank before `BENCH_BEGIN`; this remains the second guard
against stale receipts, another board, or later QSPI mutation.

## Testing and Evidence

- Python tests fake only the external OpenOCD process and assert observable
  receipt/refusal behavior for correct output, wrong SHA, wrong address,
  malformed UID, timeout, and helper overlap.
- A host C++ test exercises the target programming core through a small fake
  QSPI boundary: exact erase block, exact write offset/length, byte identity,
  and failure propagation.
- The actual ARM helper build must link below `0x24040000`; its map and ELF
  program headers are recorded.
- Existing QSPI guard, Task 8 contracts, generator check, desktop CTest, and
  benchmark ARM build remain green.

## Documentation and Safety

README instructions replace the invalid BOOT/RESET/DFU flow with the
ST-Link-only SRAM helper flow. They explicitly state that the operation
erases one external-QSPI 64 KiB block at offset `0x40000`, writes no internal
flash, and invalidates any prior bootloader application occupying that
external block.

Hardware execution is performed by the controller with the physical device;
the implementation session does not issue programming commands.
