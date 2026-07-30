# Bench evidence 2026-07-30 — `d570e47`

## Gate ledger

Execution layout: `itcm-hot`.

Profile `system` — families: `system`

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

- **Run 1 — PASS:** `wave_2x4` average 307259 <= `synth_2x4` average 338662; maximum 309874 <= 343217; maximum 309874 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 307259 <= `synth_2x4` average 338662; maximum 309874 <= 343217; maximum 309874 < 960000.

## Verdict

**2x4 budget — go/no-go.** The full instrument at its worst case (8 voices, COLOR 4-note on both parts, all FX on, high diffusion, echo at max) costs **99 % of the block budget offline**, and **99 % measured inside a real audio callback**. The anchored figure is the one that decides. **Conclusion: the 2x4 architecture fits.** The anchored figure is under 100 % of the block budget.

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
| system | `mod_plane_2x_center` | 67628 | 70074 | 7.04 | 7.29 | `61d42d20` |
| system | `synth_1_voice` | 54151 | 55033 | 5.64 | 5.73 | `1816acc1` |
| system | `synth_2_voices` | 94183 | 95802 | 9.81 | 9.97 | `4dc805b7` |
| system | `synth_4_voices` | 169629 | 171827 | 17.66 | 17.89 | `2292dae2` |
| system | `synth_2x4` | 338662 | 343217 | 35.27 | 35.75 | `0d15b5eb` |
| system | `wave_2x4` | 307259 | 309874 | 32.00 | 32.27 | `6f28f4ea` |
| system | `fx_none` | 26619 | 26656 | 2.77 | 2.77 | `b538ce01` |
| system | `fx_grit` | 50497 | 50609 | 5.26 | 5.27 | `74f9b9f5` |
| system | `fx_flux_sdram` | 124483 | 125231 | 12.96 | 13.04 | `5b9094c3` |
| system | `fx_comp` | 33569 | 33658 | 3.49 | 3.50 | `47a4392b` |
| system | `oliverb_solo_sram` | 90937 | 91836 | 9.47 | 9.56 | `f09fa14e` |
| system | `instrument_init` | 582283 | 686912 | 60.65 | 71.55 | `2b52554a` |
| system | `instrument_worst` | 917290 | 949702 | 95.55 | 98.92 | `4c4a29ce` |
| system | `instrument_worst_bbd` | 951289 | 990058 | 99.09 | 103.13 | `483e8e82` |
| system | `instrument_worst_bbd_dtcm` | 947098 | 985433 | 98.65 | 102.64 | `483e8e82` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.52 | 9.69 |
| `instrument_worst_bbd_dtcm` | 98.81 | 102.78 |
| `instrument_worst_bbd` | 99.24 | 103.23 |
| `instrument_worst` | 95.74 | 99.03 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 11 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 67628 | 70074 | 7.04 | 7.29 | `61d42d20` |
| system | `synth_1_voice` | 54151 | 55033 | 5.64 | 5.73 | `1816acc1` |
| system | `synth_2_voices` | 94183 | 95802 | 9.81 | 9.97 | `4dc805b7` |
| system | `synth_4_voices` | 169629 | 171827 | 17.66 | 17.89 | `2292dae2` |
| system | `synth_2x4` | 338662 | 343217 | 35.27 | 35.75 | `0d15b5eb` |
| system | `wave_2x4` | 307259 | 309874 | 32.00 | 32.27 | `6f28f4ea` |
| system | `fx_none` | 26619 | 26656 | 2.77 | 2.77 | `b538ce01` |
| system | `fx_grit` | 50497 | 50609 | 5.26 | 5.27 | `74f9b9f5` |
| system | `fx_flux_sdram` | 124484 | 125136 | 12.96 | 13.03 | `5b9094c3` |
| system | `fx_comp` | 33569 | 33655 | 3.49 | 3.50 | `47a4392b` |
| system | `oliverb_solo_sram` | 90938 | 91824 | 9.47 | 9.56 | `f09fa14e` |
| system | `instrument_init` | 582282 | 686966 | 60.65 | 71.55 | `2b52554a` |
| system | `instrument_worst` | 917256 | 949679 | 95.54 | 98.92 | `4c4a29ce` |
| system | `instrument_worst_bbd` | 951316 | 989162 | 99.09 | 103.03 | `483e8e82` |
| system | `instrument_worst_bbd_dtcm` | 947090 | 986074 | 98.65 | 102.71 | `483e8e82` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.52 | 9.64 |
| `instrument_worst_bbd_dtcm` | 98.82 | 102.79 |
| `instrument_worst_bbd` | 99.23 | 103.09 |
| `instrument_worst` | 95.74 | 99.02 |
