# Bench evidence 2026-08-19 — `ac26589`

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

- **Run 1 — PASS:** `wave_2x4` average 314515 <= `synth_2x4` average 340267; maximum 325426 <= 345498; maximum 325426 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 314427 <= `synth_2x4` average 340272; maximum 325555 <= 345295; maximum 325555 < 960000.

## Verdict

**DTCM BBD-engine budget — go/no-go.** The decision workload is `instrument_worst_bbd_dtcm`: the full instrument with both decks on the BBD part engine, shortest division, clock ceiling, maximum COLOR/feedback/mix, STEP freeze engaged, live inputs, and retained DTCM instrument state. `instrument_worst_bbd` is its checksum-equal AXI comparison. `fx_flux_sdram` separately prices stereo tape FLUX; the five retained `sweep_flux_rate_*` rows carry its delay-time cost curve. Run maxima (offline / real callback): Run 1 96.92 % / 96.77 %; Run 2 96.86 % / 96.80 %. Across all 2 repeats, the worst maxima are **96.92 % offline** and **96.80 % in the real callback**. **Conclusion: the DTCM BBD-engine gate fits.** Every offline and real-callback maximum is below 100 % of the block budget.

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
| system | `mod_plane_2x_center` | 83481 | 87588 | 8.69 | 9.12 | `5af19015` |
| system | `synth_1_voice` | 53879 | 55443 | 5.61 | 5.77 | `1816acc1` |
| system | `synth_2_voices` | 94033 | 96134 | 9.79 | 10.01 | `4dc805b7` |
| system | `synth_4_voices` | 169751 | 172230 | 17.68 | 17.94 | `2292dae2` |
| system | `synth_2x4` | 340267 | 345498 | 35.44 | 35.98 | `0d15b5eb` |
| system | `wave_2x4` | 314515 | 325426 | 32.76 | 33.89 | `6f28f4ea` |
| system | `fx_none` | 24512 | 25406 | 2.55 | 2.64 | `b538ce01` |
| system | `fx_grit` | 50873 | 52106 | 5.29 | 5.42 | `74f9b9f5` |
| system | `fx_flux_sdram` | 100107 | 102638 | 10.42 | 10.69 | `9ca91007` |
| system | `fx_comp` | 31490 | 32502 | 3.28 | 3.38 | `47a4392b` |
| system | `oliverb_solo_sram` | 99820 | 101201 | 10.39 | 10.54 | `82d044c4` |
| system | `instrument_init` | 689077 | 717286 | 71.77 | 74.71 | `3bbafe55` |
| system | `instrument_worst` | 953295 | 1000213 | 99.30 | 104.18 | `851c7d43` |
| system | `inst_worst_deck_bus` | 751720 | 800105 | 78.30 | 83.34 | `6f9b841c` |
| system | `instrument_worst_bbd` | 898247 | 945529 | 93.56 | 98.49 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 888868 | 930523 | 92.59 | 96.92 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 898182 | 944589 | 93.56 | 98.39 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 729593 | 781499 | 75.99 | 81.40 | `ec6bd451` |
| feed | `feed_pairs` | 77216 | 79105 | 8.04 | 8.24 | `d5cedcb2` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.39 | 10.62 |
| `instrument_worst_bbd_dtcm` | 91.74 | 96.77 |
| `instrument_worst_bbd` | 92.67 | 98.02 |
| `instrument_worst` | 98.67 | 103.53 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `26696514052c2c6aaf668d4a6c74569bd9a563f769dae106012dd024791f2849`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 12 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 83505 | 86724 | 8.69 | 9.03 | `5af19015` |
| system | `synth_1_voice` | 53879 | 55513 | 5.61 | 5.78 | `1816acc1` |
| system | `synth_2_voices` | 94033 | 95939 | 9.79 | 9.99 | `4dc805b7` |
| system | `synth_4_voices` | 169745 | 172373 | 17.68 | 17.95 | `2292dae2` |
| system | `synth_2x4` | 340272 | 345295 | 35.44 | 35.96 | `0d15b5eb` |
| system | `wave_2x4` | 314427 | 325555 | 32.75 | 33.91 | `6f28f4ea` |
| system | `fx_none` | 24513 | 25735 | 2.55 | 2.68 | `b538ce01` |
| system | `fx_grit` | 50873 | 52200 | 5.29 | 5.43 | `74f9b9f5` |
| system | `fx_flux_sdram` | 100119 | 102607 | 10.42 | 10.68 | `9ca91007` |
| system | `fx_comp` | 31489 | 32461 | 3.28 | 3.38 | `47a4392b` |
| system | `oliverb_solo_sram` | 99827 | 101309 | 10.39 | 10.55 | `82d044c4` |
| system | `instrument_init` | 688998 | 717934 | 71.77 | 74.78 | `3bbafe55` |
| system | `instrument_worst` | 953147 | 1004503 | 99.28 | 104.63 | `851c7d43` |
| system | `inst_worst_deck_bus` | 751736 | 800783 | 78.30 | 83.41 | `6f9b841c` |
| system | `instrument_worst_bbd` | 898217 | 943229 | 93.56 | 98.25 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 888847 | 929943 | 92.58 | 96.86 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 898173 | 944665 | 93.55 | 98.40 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 729574 | 781581 | 75.99 | 81.41 | `ec6bd451` |
| feed | `feed_pairs` | 77215 | 79102 | 8.04 | 8.23 | `d5cedcb2` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.39 | 10.63 |
| `instrument_worst_bbd_dtcm` | 91.74 | 96.80 |
| `instrument_worst_bbd` | 92.67 | 98.41 |
| `instrument_worst` | 98.68 | 103.84 |
