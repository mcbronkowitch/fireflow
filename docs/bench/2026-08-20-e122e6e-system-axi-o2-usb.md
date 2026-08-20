# Bench evidence 2026-08-20 — `e122e6e`

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

All 2 runs report QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27` and device fingerprint `26696514052c2c6aaf668d4a6c74569bd9a563f769dae106012dd024791f2849` (SHA-256 of the MCU UID).

## WAVE performance gate — PASS

All 2 runs satisfy the matched WAVE/SYNTH acceptance gates.

- **Run 1 — PASS:** `wave_2x4` average 318109 <= `synth_2x4` average 340880; maximum 321080 <= 345646; maximum 321080 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 317904 <= `synth_2x4` average 341431; maximum 320987 <= 346312; maximum 320987 < 960000.

## Verdict

**DTCM BBD-engine budget — go/no-go.** The decision workload is `instrument_worst_bbd_dtcm`: the full instrument with both decks on the BBD part engine, shortest division, clock ceiling, maximum COLOR/feedback/mix, STEP freeze engaged, live inputs, and retained DTCM instrument state. `instrument_worst_bbd` is its checksum-equal AXI comparison. `fx_flux_sdram` separately prices stereo tape FLUX; the five retained `sweep_flux_rate_*` rows carry its delay-time cost curve. Run maxima (offline / real callback): Run 1 98.51 % / 98.84 %; Run 2 98.56 % / 98.79 %. Across all 2 repeats, the worst maxima are **98.56 % offline** and **98.84 % in the real callback**. **Conclusion: the DTCM BBD-engine gate fits.** Every offline and real-callback maximum is below 100 % of the block budget.

**Cost per candidate, relative to one real fireflow voice.**

- n/a (family `voice` not in this profile)

**SRAM vs SDRAM.** The grain-read proxy (8 scattered interpolated stereo reads per sample, identical window in both regions) costs **n/a (row missing)** in SDRAM against SRAM. That is a bare access pattern, written before the sampler existed to stand in for it; the `sampler_win_*` pair below is the same contrast with the real engine around it. The Oliverb pair reads **n/a (row missing)**, and the shortened echo-style streaming walk **n/a (row missing)**.

*The decision gate retains the firmware's two-decimal percentages because values immediately around 100 % determine the stop gate. Other prose uses whole percentage points and two significant figures for ratios; the tables below retain full measured precision.*

## Run 1

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `26696514052c2c6aaf668d4a6c74569bd9a563f769dae106012dd024791f2849`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 13 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 84775 | 89675 | 8.83 | 9.34 | `5af19015` |
| system | `synth_1_voice` | 54958 | 55825 | 5.72 | 5.81 | `1816acc1` |
| system | `synth_2_voices` | 94958 | 96638 | 9.89 | 10.06 | `4dc805b7` |
| system | `synth_4_voices` | 170386 | 172727 | 17.74 | 17.99 | `2292dae2` |
| system | `synth_2x4` | 340880 | 345646 | 35.50 | 36.00 | `0d15b5eb` |
| system | `wave_2x4` | 318109 | 321080 | 33.13 | 33.44 | `6f28f4ea` |
| system | `fx_none` | 24414 | 24481 | 2.54 | 2.55 | `b538ce01` |
| system | `fx_grit` | 50612 | 50675 | 5.27 | 5.27 | `74f9b9f5` |
| system | `fx_flux_sdram` | 102320 | 105981 | 10.65 | 11.03 | `9ca91007` |
| system | `fx_comp` | 31352 | 31410 | 3.26 | 3.27 | `47a4392b` |
| system | `oliverb_solo_sram` | 99390 | 100336 | 10.35 | 10.45 | `82d044c4` |
| system | `instrument_init` | 669525 | 698220 | 69.74 | 72.73 | `3bbafe55` |
| system | `instrument_worst` | 951418 | 1000591 | 99.10 | 104.22 | `851c7d43` |
| system | `inst_worst_deck_bus` | 733132 | 780242 | 76.36 | 81.27 | `6f9b841c` |
| system | `instrument_worst_bbd` | 912656 | 957007 | 95.06 | 99.68 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 903811 | 945707 | 94.14 | 98.51 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 912645 | 958963 | 95.06 | 99.89 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 786081 | 842988 | 81.88 | 87.81 | `d66cbb8c` |
| system | `inst_feed_engine_idle` | 772848 | 783796 | 80.50 | 81.64 | `8e4f6664` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.40 | 10.62 |
| `instrument_worst_bbd_dtcm` | 94.01 | 98.84 |
| `instrument_worst_bbd` | 94.93 | 100.24 |
| `instrument_worst` | 99.35 | 104.07 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `26696514052c2c6aaf668d4a6c74569bd9a563f769dae106012dd024791f2849`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 13 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 84900 | 89647 | 8.84 | 9.33 | `5af19015` |
| system | `synth_1_voice` | 54962 | 55814 | 5.72 | 5.81 | `1816acc1` |
| system | `synth_2_voices` | 94674 | 96280 | 9.86 | 10.02 | `4dc805b7` |
| system | `synth_4_voices` | 170380 | 172710 | 17.74 | 17.99 | `2292dae2` |
| system | `synth_2x4` | 341431 | 346312 | 35.56 | 36.07 | `0d15b5eb` |
| system | `wave_2x4` | 317904 | 320987 | 33.11 | 33.43 | `6f28f4ea` |
| system | `fx_none` | 24414 | 24481 | 2.54 | 2.55 | `b538ce01` |
| system | `fx_grit` | 50612 | 50675 | 5.27 | 5.27 | `74f9b9f5` |
| system | `fx_flux_sdram` | 102324 | 105730 | 10.65 | 11.01 | `9ca91007` |
| system | `fx_comp` | 31352 | 31407 | 3.26 | 3.27 | `47a4392b` |
| system | `oliverb_solo_sram` | 99394 | 100430 | 10.35 | 10.46 | `82d044c4` |
| system | `instrument_init` | 669424 | 697590 | 69.73 | 72.66 | `3bbafe55` |
| system | `instrument_worst` | 951481 | 996503 | 99.11 | 103.80 | `851c7d43` |
| system | `inst_worst_deck_bus` | 733129 | 778620 | 76.36 | 81.10 | `6f9b841c` |
| system | `instrument_worst_bbd` | 912666 | 958151 | 95.06 | 99.80 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 903822 | 946208 | 94.14 | 98.56 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 912680 | 959331 | 95.07 | 99.93 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 786057 | 843257 | 81.88 | 87.83 | `d66cbb8c` |
| system | `inst_feed_engine_idle` | 772827 | 783572 | 80.50 | 81.62 | `8e4f6664` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.40 | 10.63 |
| `instrument_worst_bbd_dtcm` | 94.01 | 98.79 |
| `instrument_worst_bbd` | 94.91 | 100.10 |
| `instrument_worst` | 99.37 | 103.91 |
