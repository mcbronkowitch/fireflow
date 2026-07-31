# Bench evidence 2026-07-31 — `20eafed`

## Gate ledger

Execution layout: `axi`.

Optimization: `o2` (`-O2`).

Profile `system` — families: `system`

Applied and passed:

- row set matches the profile exactly (no missing, no extra rows)
- no duplicate rows
- anchor set matches the profile exactly and is numeric
- all decision-gate measurements are numeric
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

- **Run 1 — PASS:** `wave_2x4` average 309262 <= `synth_2x4` average 338629; maximum 312731 <= 343715; maximum 312731 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 309262 <= `synth_2x4` average 338629; maximum 312731 <= 343715; maximum 312731 < 960000.

## Verdict

**DTCM+BBD budget — go/no-go.** The decision workload is `instrument_worst_bbd_dtcm`: the full eight-voice instrument, BBD echo and the retained DTCM instrument state. Run maxima (offline / real callback): Run 1 108.52 % / 108.56 %; Run 2 108.49 % / 108.45 %. Across all 2 repeats, the worst maxima are **108.52 % offline** and **108.56 % in the real callback**. **Conclusion: the DTCM+BBD gate does not fit.** At least one offline or real-callback maximum is at or above 100 % of the block budget.

**Cost per candidate, relative to one real spotymod voice.**

- n/a (family `voice` not in this profile)

**SRAM vs SDRAM.** The grain-read proxy (8 scattered interpolated stereo reads per sample, identical window in both regions) costs **n/a (row missing)** in SDRAM against SRAM. That is a bare access pattern, written before the sampler existed to stand in for it; the `sampler_win_*` pair below is the same contrast with the real engine around it. The Oliverb pair reads **n/a (row missing)**, and the shortened echo-style streaming walk **n/a (row missing)**.

*The decision gate retains the firmware's two-decimal percentages because values immediately around 100 % determine the stop gate. Other prose uses whole percentage points and two significant figures for ratios; the tables below retain full measured precision.*

## Run 1

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 10 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 70870 | 72797 | 7.38 | 7.58 | `61d42d20` |
| system | `synth_1_voice` | 53321 | 54233 | 5.55 | 5.64 | `1816acc1` |
| system | `synth_2_voices` | 93380 | 95165 | 9.72 | 9.91 | `4dc805b7` |
| system | `synth_4_voices` | 168955 | 171363 | 17.59 | 17.85 | `2292dae2` |
| system | `synth_2x4` | 338629 | 343715 | 35.27 | 35.80 | `0d15b5eb` |
| system | `wave_2x4` | 309262 | 312731 | 32.21 | 32.57 | `6f28f4ea` |
| system | `fx_none` | 24413 | 24451 | 2.54 | 2.54 | `b538ce01` |
| system | `fx_grit` | 51344 | 51600 | 5.34 | 5.37 | `74f9b9f5` |
| system | `fx_flux_sdram` | 124720 | 125636 | 12.99 | 13.08 | `5b9094c3` |
| system | `fx_comp` | 31241 | 31308 | 3.25 | 3.26 | `47a4392b` |
| system | `oliverb_solo_sram` | 90140 | 91070 | 9.38 | 9.48 | `f09fa14e` |
| system | `instrument_init` | 604492 | 710583 | 62.96 | 74.01 | `2b52554a` |
| system | `instrument_worst` | 976530 | 1013089 | 101.72 | 105.53 | `4c4a29ce` |
| system | `inst_worst_deck_bus` | 697885 | 734569 | 72.69 | 76.51 | `8b05a866` |
| system | `instrument_worst_bbd` | 1010046 | 1053041 | 105.21 | 109.69 | `483e8e82` |
| system | `instrument_worst_bbd_dtcm` | 1002178 | 1041829 | 104.39 | 108.52 | `483e8e82` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.44 | 9.67 |
| `instrument_worst_bbd_dtcm` | 104.55 | 108.56 |
| `instrument_worst_bbd` | 105.35 | 109.60 |
| `instrument_worst` | 101.90 | 105.73 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 10 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 70870 | 72797 | 7.38 | 7.58 | `61d42d20` |
| system | `synth_1_voice` | 53321 | 54233 | 5.55 | 5.64 | `1816acc1` |
| system | `synth_2_voices` | 93380 | 95165 | 9.72 | 9.91 | `4dc805b7` |
| system | `synth_4_voices` | 168955 | 171363 | 17.59 | 17.85 | `2292dae2` |
| system | `synth_2x4` | 338629 | 343715 | 35.27 | 35.80 | `0d15b5eb` |
| system | `wave_2x4` | 309262 | 312731 | 32.21 | 32.57 | `6f28f4ea` |
| system | `fx_none` | 24413 | 24450 | 2.54 | 2.54 | `b538ce01` |
| system | `fx_grit` | 51550 | 51837 | 5.36 | 5.39 | `74f9b9f5` |
| system | `fx_flux_sdram` | 124731 | 125600 | 12.99 | 13.08 | `5b9094c3` |
| system | `fx_comp` | 31241 | 31329 | 3.25 | 3.26 | `47a4392b` |
| system | `oliverb_solo_sram` | 90134 | 91053 | 9.38 | 9.48 | `f09fa14e` |
| system | `instrument_init` | 604488 | 710757 | 62.96 | 74.03 | `2b52554a` |
| system | `instrument_worst` | 976536 | 1013755 | 101.72 | 105.59 | `4c4a29ce` |
| system | `inst_worst_deck_bus` | 697867 | 734123 | 72.69 | 76.47 | `8b05a866` |
| system | `instrument_worst_bbd` | 1010098 | 1052369 | 105.21 | 109.62 | `483e8e82` |
| system | `instrument_worst_bbd_dtcm` | 1002159 | 1041565 | 104.39 | 108.49 | `483e8e82` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.44 | 9.69 |
| `instrument_worst_bbd_dtcm` | 104.55 | 108.45 |
| `instrument_worst_bbd` | 105.35 | 109.56 |
| `instrument_worst` | 101.90 | 105.83 |
