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

- **Run 1 — PASS:** `wave_2x4` average 307518 <= `synth_2x4` average 338794; maximum 311059 <= 343821; maximum 311059 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 307499 <= `synth_2x4` average 338796; maximum 311009 <= 343875; maximum 311009 < 960000.

## Verdict

**DTCM+BBD budget — go/no-go.** The decision workload is `instrument_worst_bbd_dtcm`: the full eight-voice instrument, BBD echo and the retained DTCM instrument state. Run maxima (offline / real callback): Run 1 108.06 % / 108.02 %; Run 2 108.05 % / 108.04 %. Across all 2 repeats, the worst maxima are **108.06 % offline** and **108.04 % in the real callback**. **Conclusion: the DTCM+BBD gate does not fit.** At least one offline or real-callback maximum is at or above 100 % of the block budget.

**Cost per candidate, relative to one real spotymod voice.**

- n/a (family `voice` not in this profile)

**SRAM vs SDRAM.** The grain-read proxy (8 scattered interpolated stereo reads per sample, identical window in both regions) costs **n/a (row missing)** in SDRAM against SRAM. That is a bare access pattern, written before the sampler existed to stand in for it; the `sampler_win_*` pair below is the same contrast with the real engine around it. The Oliverb pair reads **n/a (row missing)**, and the shortened echo-style streaming walk **n/a (row missing)**.

*The decision gate retains the firmware's two-decimal percentages because values immediately around 100 % determine the stop gate. Other prose uses whole percentage points and two significant figures for ratios; the tables below retain full measured precision.*

## Run 1

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 11 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 71189 | 73346 | 7.41 | 7.64 | `61d42d20` |
| system | `synth_1_voice` | 53290 | 54174 | 5.55 | 5.64 | `1816acc1` |
| system | `synth_2_voices` | 93367 | 95103 | 9.72 | 9.90 | `4dc805b7` |
| system | `synth_4_voices` | 168941 | 171299 | 17.59 | 17.84 | `2292dae2` |
| system | `synth_2x4` | 338794 | 343821 | 35.29 | 35.81 | `0d15b5eb` |
| system | `wave_2x4` | 307518 | 311059 | 32.03 | 32.40 | `6f28f4ea` |
| system | `fx_none` | 24413 | 24451 | 2.54 | 2.54 | `b538ce01` |
| system | `fx_grit` | 51526 | 51847 | 5.36 | 5.40 | `74f9b9f5` |
| system | `fx_flux_sdram` | 124725 | 125613 | 12.99 | 13.08 | `5b9094c3` |
| system | `fx_comp` | 31240 | 31310 | 3.25 | 3.26 | `47a4392b` |
| system | `oliverb_solo_sram` | 90110 | 91022 | 9.38 | 9.48 | `f09fa14e` |
| system | `instrument_init` | 585111 | 690155 | 60.94 | 71.89 | `2b52554a` |
| system | `instrument_worst` | 972513 | 1009405 | 101.30 | 105.14 | `4c4a29ce` |
| system | `inst_worst_deck_bus` | 755735 | 789238 | 78.72 | 82.21 | `8eaf4037` |
| system | `instrument_worst_bbd` | 1009132 | 1049021 | 105.11 | 109.27 | `483e8e82` |
| system | `instrument_worst_bbd_dtcm` | 1000293 | 1037387 | 104.19 | 108.06 | `483e8e82` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.43 | 9.66 |
| `instrument_worst_bbd_dtcm` | 104.42 | 108.02 |
| `instrument_worst_bbd` | 105.31 | 109.53 |
| `instrument_worst` | 101.48 | 105.11 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 11 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 71273 | 74421 | 7.42 | 7.75 | `61d42d20` |
| system | `synth_1_voice` | 53291 | 54175 | 5.55 | 5.64 | `1816acc1` |
| system | `synth_2_voices` | 93367 | 95104 | 9.72 | 9.90 | `4dc805b7` |
| system | `synth_4_voices` | 168942 | 171300 | 17.59 | 17.84 | `2292dae2` |
| system | `synth_2x4` | 338796 | 343875 | 35.29 | 35.82 | `0d15b5eb` |
| system | `wave_2x4` | 307499 | 311009 | 32.03 | 32.39 | `6f28f4ea` |
| system | `fx_none` | 24413 | 24450 | 2.54 | 2.54 | `b538ce01` |
| system | `fx_grit` | 51524 | 51780 | 5.36 | 5.39 | `74f9b9f5` |
| system | `fx_flux_sdram` | 124728 | 125694 | 12.99 | 13.09 | `5b9094c3` |
| system | `fx_comp` | 31240 | 31306 | 3.25 | 3.26 | `47a4392b` |
| system | `oliverb_solo_sram` | 90109 | 90957 | 9.38 | 9.47 | `f09fa14e` |
| system | `instrument_init` | 585103 | 690289 | 60.94 | 71.90 | `2b52554a` |
| system | `instrument_worst` | 972479 | 1008449 | 101.29 | 105.04 | `4c4a29ce` |
| system | `inst_worst_deck_bus` | 755739 | 789908 | 78.72 | 82.28 | `8eaf4037` |
| system | `instrument_worst_bbd` | 1009179 | 1049067 | 105.12 | 109.27 | `483e8e82` |
| system | `instrument_worst_bbd_dtcm` | 1000277 | 1037361 | 104.19 | 108.05 | `483e8e82` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.43 | 9.68 |
| `instrument_worst_bbd_dtcm` | 104.42 | 108.04 |
| `instrument_worst_bbd` | 105.31 | 109.45 |
| `instrument_worst` | 101.50 | 105.13 |
