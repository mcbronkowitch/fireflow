# Bench evidence 2026-08-19 — `5130bd2`

## Gate ledger

Execution layout: `axi`.

Optimization: `o2` (`-O2`).

Profile `feed` — families: `system`, `feed`

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

Measured on a Daisy Patch Submodule (STM32H750). 480000000 Hz core clock, block size 96, dcache+icache. Block budget 960000 cycles.

All 2 runs report QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27` and device fingerprint `26696514052c2c6aaf668d4a6c74569bd9a563f769dae106012dd024791f2849` (SHA-256 of the MCU UID).

## WAVE performance gate — PASS

All 2 runs satisfy the matched WAVE/SYNTH acceptance gates.

- **Run 1 — PASS:** `wave_2x4` average 318668 <= `synth_2x4` average 340251; maximum 331145 <= 345381; maximum 331145 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 318542 <= `synth_2x4` average 340247; maximum 331421 <= 344858; maximum 331421 < 960000.

## Verdict

**DTCM BBD-engine budget — go/no-go.** The decision workload is `instrument_worst_bbd_dtcm`: the full instrument with both decks on the BBD part engine, shortest division, clock ceiling, maximum COLOR/feedback/mix, STEP freeze engaged, live inputs, and retained DTCM instrument state. `instrument_worst_bbd` is its checksum-equal AXI comparison. `fx_flux_sdram` separately prices stereo tape FLUX; the five retained `sweep_flux_rate_*` rows carry its delay-time cost curve. Run maxima (offline / real callback): Run 1 97.04 % / 96.79 %; Run 2 97.03 % / 96.93 %. Across all 2 repeats, the worst maxima are **97.04 % offline** and **96.93 % in the real callback**. **Conclusion: the DTCM BBD-engine gate fits.** Every offline and real-callback maximum is below 100 % of the block budget.

**Cost per candidate, relative to one real fireflow voice.**

- n/a (family `voice` not in this profile)

**SRAM vs SDRAM.** The grain-read proxy (8 scattered interpolated stereo reads per sample, identical window in both regions) costs **n/a (row missing)** in SDRAM against SRAM. That is a bare access pattern, written before the sampler existed to stand in for it; the `sampler_win_*` pair below is the same contrast with the real engine around it. The Oliverb pair reads **n/a (row missing)**, and the shortened echo-style streaming walk **n/a (row missing)**.

*The decision gate retains the firmware's two-decimal percentages because values immediately around 100 % determine the stop gate. Other prose uses whole percentage points and two significant figures for ratios; the tables below retain full measured precision.*

## Run 1

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `26696514052c2c6aaf668d4a6c74569bd9a563f769dae106012dd024791f2849`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 12 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 79256 | 82137 | 8.25 | 8.55 | `5af19015` |
| system | `synth_1_voice` | 53871 | 55283 | 5.61 | 5.75 | `1816acc1` |
| system | `synth_2_voices` | 94023 | 96284 | 9.79 | 10.02 | `4dc805b7` |
| system | `synth_4_voices` | 169742 | 172331 | 17.68 | 17.95 | `2292dae2` |
| system | `synth_2x4` | 340251 | 345381 | 35.44 | 35.97 | `0d15b5eb` |
| system | `wave_2x4` | 318668 | 331145 | 33.19 | 34.49 | `6f28f4ea` |
| system | `fx_none` | 24511 | 25722 | 2.55 | 2.67 | `b538ce01` |
| system | `fx_grit` | 51070 | 52353 | 5.31 | 5.45 | `74f9b9f5` |
| system | `fx_flux_sdram` | 99182 | 101850 | 10.33 | 10.60 | `9ca91007` |
| system | `fx_comp` | 31492 | 32533 | 3.28 | 3.38 | `47a4392b` |
| system | `oliverb_solo_sram` | 99750 | 101244 | 10.39 | 10.54 | `82d044c4` |
| system | `instrument_init` | 687676 | 716735 | 71.63 | 74.65 | `3bbafe55` |
| system | `instrument_worst` | 952243 | 1004561 | 99.19 | 104.64 | `851c7d43` |
| system | `inst_worst_deck_bus` | 748922 | 800005 | 78.01 | 83.33 | `6f9b841c` |
| system | `instrument_worst_bbd` | 899646 | 945003 | 93.71 | 98.43 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 889453 | 931595 | 92.65 | 97.04 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 899608 | 946613 | 93.70 | 98.60 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 721796 | 772189 | 75.18 | 80.43 | `ae36ad8b` |
| feed | `feed_pairs` | 77105 | 78649 | 8.03 | 8.19 | `d5cedcb2` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.38 | 10.61 |
| `instrument_worst_bbd_dtcm` | 91.81 | 96.79 |
| `instrument_worst_bbd` | 92.82 | 98.40 |
| `instrument_worst` | 98.60 | 103.82 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `26696514052c2c6aaf668d4a6c74569bd9a563f769dae106012dd024791f2849`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 12 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 79251 | 82374 | 8.25 | 8.58 | `5af19015` |
| system | `synth_1_voice` | 53871 | 55324 | 5.61 | 5.76 | `1816acc1` |
| system | `synth_2_voices` | 94022 | 96120 | 9.79 | 10.01 | `4dc805b7` |
| system | `synth_4_voices` | 169740 | 172260 | 17.68 | 17.94 | `2292dae2` |
| system | `synth_2x4` | 340247 | 344858 | 35.44 | 35.92 | `0d15b5eb` |
| system | `wave_2x4` | 318542 | 331421 | 33.18 | 34.52 | `6f28f4ea` |
| system | `fx_none` | 24513 | 26509 | 2.55 | 2.76 | `b538ce01` |
| system | `fx_grit` | 51073 | 52716 | 5.32 | 5.49 | `74f9b9f5` |
| system | `fx_flux_sdram` | 99187 | 102250 | 10.33 | 10.65 | `9ca91007` |
| system | `fx_comp` | 31492 | 32550 | 3.28 | 3.39 | `47a4392b` |
| system | `oliverb_solo_sram` | 99753 | 101286 | 10.39 | 10.55 | `82d044c4` |
| system | `instrument_init` | 687645 | 716195 | 71.62 | 74.60 | `3bbafe55` |
| system | `instrument_worst` | 952356 | 1006145 | 99.20 | 104.80 | `851c7d43` |
| system | `inst_worst_deck_bus` | 749106 | 799663 | 78.03 | 83.29 | `6f9b841c` |
| system | `instrument_worst_bbd` | 899672 | 947633 | 93.71 | 98.71 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 889463 | 931537 | 92.65 | 97.03 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 899650 | 946709 | 93.71 | 98.61 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 721785 | 772585 | 75.18 | 80.47 | `ae36ad8b` |
| feed | `feed_pairs` | 77105 | 78755 | 8.03 | 8.20 | `d5cedcb2` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.39 | 10.62 |
| `instrument_worst_bbd_dtcm` | 91.80 | 96.93 |
| `instrument_worst_bbd` | 92.83 | 98.20 |
| `instrument_worst` | 98.61 | 103.78 |
