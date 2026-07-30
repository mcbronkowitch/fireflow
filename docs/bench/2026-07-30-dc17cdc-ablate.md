# Bench evidence 2026-07-30 — `dc17cdc`

## Gate ledger

Profile `ablate` — families: `system`, `instr`

Applied and passed:

- row set matches the profile exactly (no missing, no extra rows)
- no duplicate rows
- QSPI digest and device fingerprint identical across runs
- per-row checksums identical across runs
- at least two runs (`--repeat`, minimum 2)
- `wave_acceptance`: wave_2x4 no slower than synth_2x4, below the 960000-cycle block budget

Not applicable to this profile:

- none

Measured on a Daisy Seed (STM32H750). 480000000 Hz core clock, block size 96, dcache+icache, `-ffast-math -funroll-loops`. Block budget 960000 cycles.

All 2 runs report QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27` and device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8` (SHA-256 of the MCU UID).

## WAVE performance gate — PASS

All 2 runs satisfy the matched WAVE/SYNTH acceptance gates.

- **Run 1 — PASS:** `wave_2x4` average 306439 <= `synth_2x4` average 340893; maximum 309056 <= 346205; maximum 309056 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 306518 <= `synth_2x4` average 340884; maximum 309194 <= 346314; maximum 309194 < 960000.

## Verdict

**2x4 budget — go/no-go.** The full instrument at its worst case (8 voices, COLOR 4-note on both parts, all FX on, high diffusion, echo at max) costs **104 % of the block budget offline**, and **105 % measured inside a real audio callback**. The anchored figure is the one that decides. **Conclusion: the 2x4 architecture does not fit.** The anchored figure is over 100 % of the block budget, so the design has to shed voices or FX.

**Cost per candidate, relative to one real spotymod voice.**

- n/a (family `voice` not in this profile)

**SRAM vs SDRAM.** The grain-read proxy (8 scattered interpolated stereo reads per sample, identical window in both regions) costs **n/a (row missing)** in SDRAM against SRAM. That is a bare access pattern, written before the sampler existed to stand in for it; the `sampler_win_*` pair below is the same contrast with the real engine around it. The Oliverb pair reads **n/a (row missing)**, and the shortened echo-style streaming walk **n/a (row missing)**.

*Figures in this section are quoted to whole percentage points and two significant figures for ratios — honest to what this bench can actually resolve (intra-run jitter of roughly 1700 cycles on a 1.5M-cycle workload, and a cross-build layout shift that moved a 29K-cycle workload by about 7%). The tables below retain full measured precision.*

## Run 1

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 11 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 69180 | 72175 | 7.20 | 7.51 | `61d42d20` |
| system | `synth_1_voice` | 54022 | 55341 | 5.62 | 5.76 | `1816acc1` |
| system | `synth_2_voices` | 93920 | 95511 | 9.78 | 9.94 | `4dc805b7` |
| system | `synth_4_voices` | 169485 | 171868 | 17.65 | 17.90 | `2292dae2` |
| system | `synth_2x4` | 340893 | 346205 | 35.50 | 36.06 | `0d15b5eb` |
| system | `wave_2x4` | 306439 | 309056 | 31.92 | 32.19 | `6f28f4ea` |
| system | `fx_none` | 24413 | 24450 | 2.54 | 2.54 | `b538ce01` |
| system | `fx_grit` | 51953 | 52215 | 5.41 | 5.43 | `74f9b9f5` |
| system | `fx_flux_sdram` | 125002 | 125833 | 13.02 | 13.10 | `5b9094c3` |
| system | `fx_comp` | 31255 | 31325 | 3.25 | 3.26 | `47a4392b` |
| system | `oliverb_solo_sram` | 90041 | 91023 | 9.37 | 9.48 | `f09fa14e` |
| system | `instrument_init` | 599654 | 706002 | 62.46 | 73.54 | `2b52554a` |
| system | `instrument_worst` | 968001 | 1003034 | 100.83 | 104.48 | `4c4a29ce` |
| system | `instrument_worst_bbd` | 1002266 | 1041378 | 104.40 | 108.47 | `483e8e82` |
| instr | `instr_part_1` | 410834 | 430438 | 42.79 | 44.83 | `0a0be1b0` |
| instr | `instr_part_2` | 845136 | 885064 | 88.03 | 92.19 | `0604adaf` |
| instr | `instr_noverb` | 871945 | 912062 | 90.82 | 95.00 | `ac73d2a0` |
| instr | `deck_mod_hot` | 34798 | 37071 | 3.62 | 3.86 | `3a1002cf` |
| instr | `deck_engine_hot` | 171866 | 178115 | 17.90 | 18.55 | `f682ef98` |
| instr | `fx_flux_hot` | 141466 | 141871 | 14.73 | 14.77 | `7c9e4d14` |
| instr | `tone_solo` | 19285 | 19811 | 2.00 | 2.06 | `68a83b17` |
| instr | `deck_shell` | 105251 | 121458 | 10.96 | 12.65 | `e06bb0f5` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.43 | 9.70 |
| `instrument_worst` | 101.03 | 104.55 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 11 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 69240 | 72180 | 7.21 | 7.51 | `61d42d20` |
| system | `synth_1_voice` | 54009 | 54888 | 5.62 | 5.71 | `1816acc1` |
| system | `synth_2_voices` | 93927 | 95745 | 9.78 | 9.97 | `4dc805b7` |
| system | `synth_4_voices` | 169486 | 171792 | 17.65 | 17.89 | `2292dae2` |
| system | `synth_2x4` | 340884 | 346314 | 35.50 | 36.07 | `0d15b5eb` |
| system | `wave_2x4` | 306518 | 309194 | 31.92 | 32.20 | `6f28f4ea` |
| system | `fx_none` | 24413 | 24450 | 2.54 | 2.54 | `b538ce01` |
| system | `fx_grit` | 51957 | 52241 | 5.41 | 5.44 | `74f9b9f5` |
| system | `fx_flux_sdram` | 125007 | 126051 | 13.02 | 13.13 | `5b9094c3` |
| system | `fx_comp` | 31260 | 31311 | 3.25 | 3.26 | `47a4392b` |
| system | `oliverb_solo_sram` | 90045 | 90947 | 9.37 | 9.47 | `f09fa14e` |
| system | `instrument_init` | 599599 | 705998 | 62.45 | 73.54 | `2b52554a` |
| system | `instrument_worst` | 968013 | 1003970 | 100.83 | 104.58 | `4c4a29ce` |
| system | `instrument_worst_bbd` | 1002313 | 1042366 | 104.40 | 108.57 | `483e8e82` |
| instr | `instr_part_1` | 410827 | 430346 | 42.79 | 44.82 | `0a0be1b0` |
| instr | `instr_part_2` | 845113 | 885814 | 88.03 | 92.27 | `0604adaf` |
| instr | `instr_noverb` | 871960 | 911720 | 90.82 | 94.97 | `ac73d2a0` |
| instr | `deck_mod_hot` | 34799 | 37118 | 3.62 | 3.86 | `3a1002cf` |
| instr | `deck_engine_hot` | 171865 | 178065 | 17.90 | 18.54 | `f682ef98` |
| instr | `fx_flux_hot` | 141455 | 142017 | 14.73 | 14.79 | `7c9e4d14` |
| instr | `tone_solo` | 19284 | 19796 | 2.00 | 2.06 | `68a83b17` |
| instr | `deck_shell` | 105328 | 121612 | 10.97 | 12.66 | `e06bb0f5` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.43 | 9.70 |
| `instrument_worst` | 101.04 | 104.58 |
