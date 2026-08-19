# Bench evidence 2026-08-19 — `3bdcb17`

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

Measured on a Daisy Seed (STM32H750). 480000000 Hz core clock, block size 96, dcache+icache. Block budget 960000 cycles.

All 2 runs report QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27` and device fingerprint `26696514052c2c6aaf668d4a6c74569bd9a563f769dae106012dd024791f2849` (SHA-256 of the MCU UID).

## WAVE performance gate — PASS

All 2 runs satisfy the matched WAVE/SYNTH acceptance gates.

- **Run 1 — PASS:** `wave_2x4` average 314332 <= `synth_2x4` average 339046; maximum 316062 <= 343891; maximum 316062 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 314328 <= `synth_2x4` average 339048; maximum 316024 <= 343849; maximum 316024 < 960000.

## Verdict

**DTCM BBD-engine budget — go/no-go.** The decision workload is `instrument_worst_bbd_dtcm`: the full instrument with both decks on the BBD part engine, shortest division, clock ceiling, maximum COLOR/feedback/mix, STEP freeze engaged, live inputs, and retained DTCM instrument state. `instrument_worst_bbd` is its checksum-equal AXI comparison. `fx_flux_sdram` separately prices stereo tape FLUX; the five retained `sweep_flux_rate_*` rows carry its delay-time cost curve. Run maxima (offline / real callback): Run 1 96.00 % / 95.91 %; Run 2 95.69 % / 96.01 %. Across all 2 repeats, the worst maxima are **96.00 % offline** and **96.01 % in the real callback**. **Conclusion: the DTCM BBD-engine gate fits.** Every offline and real-callback maximum is below 100 % of the block budget.

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
| system | `mod_plane_2x_center` | 83070 | 86407 | 8.65 | 9.00 | `5af19015` |
| system | `synth_1_voice` | 53933 | 54599 | 5.61 | 5.68 | `1816acc1` |
| system | `synth_2_voices` | 94130 | 95664 | 9.80 | 9.96 | `4dc805b7` |
| system | `synth_4_voices` | 169734 | 172171 | 17.68 | 17.93 | `2292dae2` |
| system | `synth_2x4` | 339046 | 343891 | 35.31 | 35.82 | `0d15b5eb` |
| system | `wave_2x4` | 314332 | 316062 | 32.74 | 32.92 | `6f28f4ea` |
| system | `fx_none` | 24414 | 24486 | 2.54 | 2.55 | `b538ce01` |
| system | `fx_grit` | 51033 | 51104 | 5.31 | 5.32 | `74f9b9f5` |
| system | `fx_flux_sdram` | 97627 | 99926 | 10.16 | 10.40 | `9ca91007` |
| system | `fx_comp` | 31361 | 31428 | 3.26 | 3.27 | `47a4392b` |
| system | `oliverb_solo_sram` | 99382 | 100336 | 10.35 | 10.45 | `82d044c4` |
| system | `instrument_init` | 672023 | 700018 | 70.00 | 72.91 | `3bbafe55` |
| system | `instrument_worst` | 934128 | 986615 | 97.30 | 102.77 | `851c7d43` |
| system | `inst_worst_deck_bus` | 748488 | 790900 | 77.96 | 82.38 | `6f9b841c` |
| system | `instrument_worst_bbd` | 883163 | 928229 | 91.99 | 96.69 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 875189 | 921637 | 91.16 | 96.00 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 883156 | 929141 | 91.99 | 96.78 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 687642 | 744144 | 71.62 | 77.51 | `3b65b8bd` |
| feed | `feed_pairs` | 76873 | 78236 | 8.00 | 8.14 | `4ba09b83` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.40 | 10.61 |
| `instrument_worst_bbd_dtcm` | 91.04 | 95.91 |
| `instrument_worst_bbd` | 91.89 | 97.04 |
| `instrument_worst` | 97.48 | 102.59 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `26696514052c2c6aaf668d4a6c74569bd9a563f769dae106012dd024791f2849`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 12 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 82975 | 86318 | 8.64 | 8.99 | `5af19015` |
| system | `synth_1_voice` | 53935 | 54587 | 5.61 | 5.68 | `1816acc1` |
| system | `synth_2_voices` | 94132 | 95754 | 9.80 | 9.97 | `4dc805b7` |
| system | `synth_4_voices` | 169736 | 172178 | 17.68 | 17.93 | `2292dae2` |
| system | `synth_2x4` | 339048 | 343849 | 35.31 | 35.81 | `0d15b5eb` |
| system | `wave_2x4` | 314328 | 316024 | 32.74 | 32.91 | `6f28f4ea` |
| system | `fx_none` | 24414 | 24486 | 2.54 | 2.55 | `b538ce01` |
| system | `fx_grit` | 51009 | 51077 | 5.31 | 5.32 | `74f9b9f5` |
| system | `fx_flux_sdram` | 97598 | 99731 | 10.16 | 10.38 | `9ca91007` |
| system | `fx_comp` | 31361 | 31426 | 3.26 | 3.27 | `47a4392b` |
| system | `oliverb_solo_sram` | 99379 | 100353 | 10.35 | 10.45 | `82d044c4` |
| system | `instrument_init` | 672041 | 699278 | 70.00 | 72.84 | `3bbafe55` |
| system | `instrument_worst` | 934039 | 984950 | 97.29 | 102.59 | `851c7d43` |
| system | `inst_worst_deck_bus` | 748413 | 795526 | 77.95 | 82.86 | `6f9b841c` |
| system | `instrument_worst_bbd` | 883219 | 927761 | 92.00 | 96.64 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 875224 | 918647 | 91.16 | 95.69 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 883130 | 928503 | 91.99 | 96.71 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 687609 | 744204 | 71.62 | 77.52 | `3b65b8bd` |
| feed | `feed_pairs` | 76873 | 78236 | 8.00 | 8.14 | `4ba09b83` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.40 | 10.63 |
| `instrument_worst_bbd_dtcm` | 91.05 | 96.01 |
| `instrument_worst_bbd` | 91.87 | 96.94 |
| `instrument_worst` | 97.52 | 102.73 |
