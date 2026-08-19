# Bench evidence 2026-08-19 — `f836a32`

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

- **Run 1 — PASS:** `wave_2x4` average 309670 <= `synth_2x4` average 340467; maximum 313038 <= 345555; maximum 313038 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 309668 <= `synth_2x4` average 340456; maximum 313038 <= 345711; maximum 313038 < 960000.

## Verdict

**DTCM BBD-engine budget — go/no-go.** The decision workload is `instrument_worst_bbd_dtcm`: the full instrument with both decks on the BBD part engine, shortest division, clock ceiling, maximum COLOR/feedback/mix, STEP freeze engaged, live inputs, and retained DTCM instrument state. `instrument_worst_bbd` is its checksum-equal AXI comparison. `fx_flux_sdram` separately prices stereo tape FLUX; the five retained `sweep_flux_rate_*` rows carry its delay-time cost curve. Run maxima (offline / real callback): Run 1 96.24 % / 95.96 %; Run 2 96.31 % / 96.07 %. Across all 2 repeats, the worst maxima are **96.31 % offline** and **96.07 % in the real callback**. **Conclusion: the DTCM BBD-engine gate fits.** Every offline and real-callback maximum is below 100 % of the block budget.

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
| system | `mod_plane_2x_center` | 79289 | 82387 | 8.25 | 8.58 | `5af19015` |
| system | `synth_1_voice` | 53884 | 55266 | 5.61 | 5.75 | `1816acc1` |
| system | `synth_2_voices` | 94040 | 96074 | 9.79 | 10.00 | `4dc805b7` |
| system | `synth_4_voices` | 169758 | 172348 | 17.68 | 17.95 | `2292dae2` |
| system | `synth_2x4` | 340467 | 345555 | 35.46 | 35.99 | `0d15b5eb` |
| system | `wave_2x4` | 309670 | 313038 | 32.25 | 32.60 | `6f28f4ea` |
| system | `fx_none` | 24509 | 25407 | 2.55 | 2.64 | `b538ce01` |
| system | `fx_grit` | 50869 | 52430 | 5.29 | 5.46 | `74f9b9f5` |
| system | `fx_flux_sdram` | 100093 | 104100 | 10.42 | 10.84 | `9ca91007` |
| system | `fx_comp` | 31490 | 32460 | 3.28 | 3.38 | `47a4392b` |
| system | `oliverb_solo_sram` | 99783 | 101208 | 10.39 | 10.54 | `82d044c4` |
| system | `instrument_init` | 675099 | 703157 | 70.32 | 73.24 | `3bbafe55` |
| system | `instrument_worst` | 937765 | 983995 | 97.68 | 102.49 | `851c7d43` |
| system | `inst_worst_deck_bus` | 729646 | 771903 | 76.00 | 80.40 | `6f9b841c` |
| system | `instrument_worst_bbd` | 892338 | 939875 | 92.95 | 97.90 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 883178 | 923951 | 91.99 | 96.24 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 892299 | 938579 | 92.94 | 97.76 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 662453 | 710455 | 69.00 | 74.00 | `f1749301` |
| system | `inst_feed_engine_idle` | 644474 | 656757 | 67.13 | 68.41 | `49e6f77b` |
| feed | `feed_pairs` | 39110 | 40354 | 4.07 | 4.20 | `30386be6` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.40 | 10.64 |
| `instrument_worst_bbd_dtcm` | 91.16 | 95.96 |
| `instrument_worst_bbd` | 92.08 | 97.02 |
| `instrument_worst` | 97.08 | 101.87 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `26696514052c2c6aaf668d4a6c74569bd9a563f769dae106012dd024791f2849`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 12 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 79391 | 82505 | 8.26 | 8.59 | `5af19015` |
| system | `synth_1_voice` | 53881 | 55260 | 5.61 | 5.75 | `1816acc1` |
| system | `synth_2_voices` | 94037 | 96208 | 9.79 | 10.02 | `4dc805b7` |
| system | `synth_4_voices` | 169755 | 172347 | 17.68 | 17.95 | `2292dae2` |
| system | `synth_2x4` | 340456 | 345711 | 35.46 | 36.01 | `0d15b5eb` |
| system | `wave_2x4` | 309668 | 313038 | 32.25 | 32.60 | `6f28f4ea` |
| system | `fx_none` | 24508 | 25419 | 2.55 | 2.64 | `b538ce01` |
| system | `fx_grit` | 50867 | 52060 | 5.29 | 5.42 | `74f9b9f5` |
| system | `fx_flux_sdram` | 100084 | 104054 | 10.42 | 10.83 | `9ca91007` |
| system | `fx_comp` | 31491 | 32486 | 3.28 | 3.38 | `47a4392b` |
| system | `oliverb_solo_sram` | 99789 | 101449 | 10.39 | 10.56 | `82d044c4` |
| system | `instrument_init` | 675091 | 703975 | 70.32 | 73.33 | `3bbafe55` |
| system | `instrument_worst` | 937594 | 983177 | 97.66 | 102.41 | `851c7d43` |
| system | `inst_worst_deck_bus` | 729621 | 767597 | 76.00 | 79.95 | `6f9b841c` |
| system | `instrument_worst_bbd` | 892413 | 939365 | 92.95 | 97.85 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 883236 | 924663 | 92.00 | 96.31 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 892306 | 941655 | 92.94 | 98.08 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 662456 | 710017 | 69.00 | 73.96 | `f1749301` |
| system | `inst_feed_engine_idle` | 644473 | 656729 | 67.13 | 68.40 | `49e6f77b` |
| feed | `feed_pairs` | 39068 | 40277 | 4.06 | 4.19 | `30386be6` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.40 | 10.63 |
| `instrument_worst_bbd_dtcm` | 91.17 | 96.07 |
| `instrument_worst_bbd` | 92.06 | 97.08 |
| `instrument_worst` | 97.09 | 101.51 |
