# ITCM audio-hotset experiment

## 1. Purpose

The retained DTCM placement lowered the accepted BBD gate to 104.64 % average
and 108.75--108.77 % maximum offline, with 104.80 % / 108.74--108.81 % in the
real callback. Four voices per deck remain fixed.

The STM32H750 still has 64 KiB unused ITCM. This round asks:

> How much does the exact DTCM gate save when its per-sample audio call path
> executes from ITCM instead of cached AXI SRAM?

No DSP expression, parameter, voice count, data residency, compiler option, or
audio buffer changes.

## 2. Hotset

Move code sections contributed by these engine objects:

- `instrument.o`
- `part.o`
- `part_fx.o`
- `flux.o`
- `bbd.o`
- `grit.o`
- `reverb.o`
- `comp.o`
- `synth_engine.o`
- `voice.o`

Their current object-file text totals 47,737 bytes before dead-section
elimination, below the 65,536-byte ITCM capacity. The set follows the
per-sample audio path. Control-plane objects (`lane.o`, `super_modulator.o`,
`center.o`) stay in AXI for this first stage rather than consuming capacity
before the audio hotset is weighed.

The supplementary linker script selects only `.text`/`.text.*`; constants
remain at their existing residency. The first 256 ITCM bytes stay reserved so
no legitimate code symbol is placed at the null address. Link failure is the
capacity guard, including that reservation.

## 3. Loading model and product boundary

The bench is loaded directly by OpenOCD from `bench-sram.elf`. The ITCM output
section therefore has its ITCM virtual/load address and OpenOCD writes it
directly; no startup copy is needed for this diagnostic binary.

This is not yet the firmware-shell loader. If ITCM is retained, M6 must either
load the section directly or copy it before any selected function executes.
The benchmark proves the cycle effect and capacity, not that later boot path.

## 4. Fail-closed identity

`BENCH_ITCM_HOT=0` is the default AXI build. `BENCH_ITCM_HOT=1` adds the
supplementary linker script.

The generated layout value is reported in every `BENCH_BEGIN` record as
`axi` or `itcm-hot`. The host:

- requires the new field;
- requires all repeated captures to report the same layout;
- requires the reported layout to match the requested CLI mode;
- includes the layout in evidence filenames and tables.

This prevents a stale AXI image from being accepted as ITCM evidence.

## 5. Comparison

Build and measure two committed variants from the same source:

1. AXI control: default build, two runs.
2. ITCM hotset: `--itcm-hot`, two runs.

For each run compare `instrument_worst_bbd_dtcm`, whose `Instrument` state
remains in DTCM in both variants. Require checksum `483e8e82` in all four
captures.

Static evidence must show:

- ITCM use greater than zero and below 65,536 bytes;
- `Instrument::process`, `Part::_control_tick`, `PartFx::process`,
  `Flux::process`, `BbdLine::Process`, `Grit::process`,
  `AmbientReverb::process`, `Comp::process`, the Morph voice process, and the
  SynthEngine process in `0x00000100..0x0000ffff`;
- `g_dtcm_instrument_storage` still in DTCM;
- the AXI control keeps those functions in `0x24000000..0x2403ffff`.

The AXI replay must stay within 0.25 points of the immediately preceding
`8702bc8` DTCM result for both average and maximum. That confirms the new
measurement plumbing did not move the control materially before ITCM is
credited.

The first hardware probe placed `.itcm_audio_hot` at `0x00000000`. Its first
weak symbol, `SoftSwitch::process`, consequently occupied the null address.
The instrument rows kept running but their audio sum collapsed to zero; for
the BBD row the checksum became the fold of the constant eight active voices
(`50306fb5`). A diagnostic relink at `0x00000100` restored every AXI checksum,
including `483e8e82`, on two runs. The reserved prefix is therefore a
correctness requirement, not a timing choice.

## 6. Decision rule

- Keep the ITCM hotset only if every checksum matches and both average and
  maximum save at least 1.00 CPU point in both ITCM runs against both AXI
  runs.
- Reject it if either metric regresses, any checksum differs, ITCM overflows,
  layout identity is ambiguous, or the minimum saving is below 1.00 point.
- If the retained ITCM gate is below 100 % in both metrics and both runs, stop
  the CPU ladder.
- Otherwise proceed in the agreed order to a separate `-O3`/LTO round, then
  half-rate reverb only if still necessary.

The 1.00-point threshold prices using most of a scarce 64 KiB execution
memory. A smaller result is not sufficient justification for this hotset.

## 7. Constraints

- Work on `codex/perf-tcm-ladder`, never directly on `main`.
- Do not change engine behavior or voices.
- Keep DTCM enabled in both control and ITCM variants.
- Build, bind a fresh QSPI receipt, then measure for each variant.
- Hardware evidence requires a clean tree and two runs.
- Commit trailer is exactly
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`,
  with nothing after it.
