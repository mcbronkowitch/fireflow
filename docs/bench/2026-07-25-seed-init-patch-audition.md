# Daisy Seed Init-Patch Audition — 2026-07-25

## Result

The current VCV init patch ran audibly on a physical Daisy Seed
(STM32H750) at commit `4060762`. The operator confirmed the expected signal
at the hardware audio output after the first launch and again after a complete
Seed power cycle and SRAM reload.

This verifies the following path on the target hardware:

- the portable `Instrument` engine initializes and processes audio at 48 kHz
  with 96-sample blocks;
- the complete generated VCV init-parameter snapshot is applied;
- part A uses the synth engine and part B uses the sampler engine;
- `host/vcv/res/factory.wav` is converted to planar float32, uploaded to SDRAM,
  validated on the Seed, and played through the real audio codec;
- the existing WAVE bank matches the linked 65,024-byte payload at
  `0x90040000`;
- stereo output continues after the debugger detaches.

The launcher observed the firmware state transition
`WAITING (0x41550002)` → `RUNNING (0x41550004)` on both hardware launches.
The ST-Link reported target voltages of approximately 3.21–3.26 V.

## Reproduce

Connect an ST-Link V3 over SWD, power the Seed, turn the monitor level down,
and run from the repository root:

```powershell
python bench\audition\launch.py
```

The launcher builds the firmware, validates its memory layout, verifies the
installed QSPI WAVE bank without modifying it, uploads the factory sample to
SDRAM, starts the engine, and exits only after the Seed reports `RUNNING`.

Use `--build-only` to create and validate the artifacts without contacting the
hardware:

```powershell
python bench\audition\launch.py --build-only
```

## Persistence and safety boundary

This is an SRAM-only development host. It does not erase or program internal
flash, the bootloader, application QSPI, or the WAVE-bank region. Resetting or
removing Seed power stops the audition and discards the loaded program and
sample; run the launcher again to restart it.

The launcher rejects unexpected QSPI placement and OpenOCD configurations
containing persistent write commands. It leaves the output silent if the
sample validation or runtime-state checks fail.

## Scope

This hardware result proves that the current engine, init patch, bundled WAV,
memory arrangement, and audio-output path run on the Seed. It does not yet
prove the final M6 control-surface integration, production boot flow,
persistent firmware packaging, or all hardware controls.

See also:

- `docs/superpowers/specs/2026-07-25-seed-init-patch-audition-design.md`
- `docs/superpowers/plans/2026-07-25-seed-init-patch-audition.md`
