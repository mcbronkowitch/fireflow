# O3+LTO static rejection — `1aa74ee`

This tracked receipt transcribes the already completed Task 4 static gate. It
does not add or imply a hardware run.

## Build identity

- Source commit: `1aa74ee7142fe24bb46064a0374919e6e6b408ea`
- Branch used for the experiment: `codex/perf-o3-lto`
- Toolchain: GNU Arm Embedded Toolchain 10-2020-q4-major,
  `arm-none-eabi-g++` 10.2.1 20201103 (release)
- Requested benchmark mode: `BENCH_OPTIMIZATION=o3-lto`
- Compile/link recipe: `-O3 -flto`
- Rejected ELF SHA-256:
  `df10e438f3e5b8be2d4176ed9cdf2419b5503883b35add81efcc7bc2e24d325f`

## Exact gate command and exit

Run from `bench/`:

```powershell
$env:BENCH_TEST_OPTIMIZATION = "o3-lto"
try {
  python -m unittest test_itcm_link
  $testExit = $LASTEXITCODE
} finally {
  Remove-Item Env:BENCH_TEST_OPTIMIZATION -ErrorAction SilentlyContinue
}
"STATIC_GATE_EXIT=$testExit"
"BENCH_TEST_OPTIMIZATION_PRESENT=$([bool](Test-Path Env:BENCH_TEST_OPTIMIZATION))"
exit $testExit
```

Recorded result: `STATIC_GATE_EXIT=1`,
`BENCH_TEST_OPTIMIZATION_PRESENT=False`, `Ran 1 test in 8.183s`, and
`FAILED (failures=11)`.

## Placement facts

`arm-none-eabi-objdump -h` reported:

```text
Idx Name            Size      VMA       LMA       File off  Algn
  0 .itcm_audio_hot 00000000  00000100  00000100  0007fe00  2**0
                    CONTENTS
```

The section had the nominal `0x00000100` VMA/LMA but zero bytes. The program
headers contained no executable ITCM `LOAD` at `0x00000100`, and
`.itcm_audio_hot` was absent from the section-to-segment map.

Representative-symbol results:

| Symbol fragment | Linked result |
|---|---|
| `spky::Instrument::process(` | missing |
| `spky::Part::_control_tick()` | AXI SRAM at `0x24013cf4` |
| `spky::PartFx::process(` | AXI SRAM at `0x240163d0` |
| `spky::Flux::process(` | missing |
| `spky::BbdLine::Process(` | missing |
| `spky::Grit::process(` | missing |
| `spky::AmbientReverb::process(` | missing |
| `spky::Comp::process(` | missing |
| `spky::VoiceT<spky::MorphOsc>::process(` | missing |
| `spky::SynthEngineT<spky::VoiceT<spky::MorphOsc> >::process(` | AXI SRAM at `0x2401956c` |

The DTCM control remained valid:
`g_dtcm_instrument_storage` was `0xc280` bytes at `0x20000528`, within the
128 KiB DTCM region.

## Decision boundary

O3+LTO was rejected before QSPI programming or SRAM execution. There are no
LTO hardware captures, CPU measurements, callback anchors, checksums, CSV,
Markdown measurement report, or QSPI receipt. The selected production recipe
remains O3 without LTO. This static rejection makes no claim about LTO runtime
performance, output, sound quality, or perceptual equivalence.
