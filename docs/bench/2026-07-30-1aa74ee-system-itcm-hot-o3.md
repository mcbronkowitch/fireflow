# Bench evidence 2026-07-30 — `1aa74ee`

## Gate ledger

Execution layout: `itcm-hot`.

Optimization: `o3` (`-O3`).

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

Measured on a Daisy Seed (STM32H750). 480000000 Hz core clock, block size 96, dcache+icache. Block budget 960000 cycles.

All 2 runs report QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27` and device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8` (SHA-256 of the MCU UID).

## WAVE performance gate — PASS

All 2 runs satisfy the matched WAVE/SYNTH acceptance gates.

- **Run 1 — PASS:** `wave_2x4` average 283493 <= `synth_2x4` average 328636; maximum 286119 <= 333917; maximum 286119 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 283493 <= `synth_2x4` average 328636; maximum 286119 <= 333917; maximum 286119 < 960000.

## Verdict

**2x4 budget — go/no-go.** The full instrument at its worst case (8 voices, COLOR 4-note on both parts, all FX on, high diffusion, echo at max) costs **97 % of the block budget offline**, and **97 % measured inside a real audio callback**. The anchored figure is the one that decides. **Conclusion: the 2x4 architecture fits.** The anchored figure is under 100 % of the block budget.

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
| system | `mod_plane_2x_center` | 66007 | 68288 | 6.87 | 7.11 | `61d42d20` |
| system | `synth_1_voice` | 52738 | 53630 | 5.49 | 5.58 | `1816acc1` |
| system | `synth_2_voices` | 91653 | 93085 | 9.54 | 9.69 | `4dc805b7` |
| system | `synth_4_voices` | 164608 | 167414 | 17.14 | 17.43 | `2292dae2` |
| system | `synth_2x4` | 328636 | 333917 | 34.23 | 34.78 | `0d15b5eb` |
| system | `wave_2x4` | 283493 | 286119 | 29.53 | 29.80 | `6f28f4ea` |
| system | `fx_none` | 23453 | 23491 | 2.44 | 2.44 | `b538ce01` |
| system | `fx_grit` | 50264 | 50312 | 5.23 | 5.24 | `74f9b9f5` |
| system | `fx_flux_sdram` | 125203 | 125961 | 13.04 | 13.12 | `28a535f0` |
| system | `fx_comp` | 30625 | 30701 | 3.19 | 3.19 | `47a4392b` |
| system | `oliverb_solo_sram` | 91186 | 92071 | 9.49 | 9.59 | `f09fa14e` |
| system | `instrument_init` | 573241 | 677252 | 59.71 | 70.54 | `2b52554a` |
| system | `instrument_worst` | 896043 | 927576 | 93.33 | 96.62 | `5f0f7b2a` |
| system | `instrument_worst_bbd` | 921900 | 961386 | 96.03 | 100.14 | `3ad2d267` |
| system | `instrument_worst_bbd_dtcm` | 916310 | 954884 | 95.44 | 99.46 | `3ad2d267` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.54 | 9.73 |
| `instrument_worst_bbd_dtcm` | 95.63 | 99.52 |
| `instrument_worst_bbd` | 96.20 | 100.30 |
| `instrument_worst` | 93.54 | 96.69 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 11 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 66007 | 68288 | 6.87 | 7.11 | `61d42d20` |
| system | `synth_1_voice` | 52738 | 53630 | 5.49 | 5.58 | `1816acc1` |
| system | `synth_2_voices` | 91653 | 93085 | 9.54 | 9.69 | `4dc805b7` |
| system | `synth_4_voices` | 164608 | 167414 | 17.14 | 17.43 | `2292dae2` |
| system | `synth_2x4` | 328636 | 333917 | 34.23 | 34.78 | `0d15b5eb` |
| system | `wave_2x4` | 283493 | 286119 | 29.53 | 29.80 | `6f28f4ea` |
| system | `fx_none` | 23453 | 23491 | 2.44 | 2.44 | `b538ce01` |
| system | `fx_grit` | 50263 | 50310 | 5.23 | 5.24 | `74f9b9f5` |
| system | `fx_flux_sdram` | 125193 | 125977 | 13.04 | 13.12 | `28a535f0` |
| system | `fx_comp` | 30625 | 30700 | 3.19 | 3.19 | `47a4392b` |
| system | `oliverb_solo_sram` | 91188 | 92061 | 9.49 | 9.58 | `f09fa14e` |
| system | `instrument_init` | 573239 | 677466 | 59.71 | 70.56 | `2b52554a` |
| system | `instrument_worst` | 896045 | 927546 | 93.33 | 96.61 | `5f0f7b2a` |
| system | `instrument_worst_bbd` | 921910 | 961050 | 96.03 | 100.10 | `3ad2d267` |
| system | `instrument_worst_bbd_dtcm` | 916322 | 955328 | 95.45 | 99.51 | `3ad2d267` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.54 | 9.73 |
| `instrument_worst_bbd_dtcm` | 95.63 | 99.54 |
| `instrument_worst_bbd` | 96.19 | 100.22 |
| `instrument_worst` | 93.54 | 96.70 |
