# Bench evidence 2026-08-19 — `500775c`

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

- **Run 1 — PASS:** `wave_2x4` average 319654 <= `synth_2x4` average 340449; maximum 323035 <= 345460; maximum 323035 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 319875 <= `synth_2x4` average 340452; maximum 323277 <= 345755; maximum 323277 < 960000.

## Verdict

**DTCM BBD-engine budget — go/no-go.** The decision workload is `instrument_worst_bbd_dtcm`: the full instrument with both decks on the BBD part engine, shortest division, clock ceiling, maximum COLOR/feedback/mix, STEP freeze engaged, live inputs, and retained DTCM instrument state. `instrument_worst_bbd` is its checksum-equal AXI comparison. `fx_flux_sdram` separately prices stereo tape FLUX; the five retained `sweep_flux_rate_*` rows carry its delay-time cost curve. Run maxima (offline / real callback): Run 1 96.26 % / 96.08 %; Run 2 96.28 % / 96.37 %. Across all 2 repeats, the worst maxima are **96.28 % offline** and **96.37 % in the real callback**. **Conclusion: the DTCM BBD-engine gate fits.** Every offline and real-callback maximum is below 100 % of the block budget.

**Cost per candidate, relative to one real fireflow voice.**

- n/a (family `voice` not in this profile)

**SRAM vs SDRAM.** The grain-read proxy (8 scattered interpolated stereo reads per sample, identical window in both regions) costs **n/a (row missing)** in SDRAM against SRAM. That is a bare access pattern, written before the sampler existed to stand in for it; the `sampler_win_*` pair below is the same contrast with the real engine around it. The Oliverb pair reads **n/a (row missing)**, and the shortened echo-style streaming walk **n/a (row missing)**.

*The decision gate retains the firmware's two-decimal percentages because values immediately around 100 % determine the stop gate. Other prose uses whole percentage points and two significant figures for ratios; the tables below retain full measured precision.*

## Run 1

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `26696514052c2c6aaf668d4a6c74569bd9a563f769dae106012dd024791f2849`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 19 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 78867 | 81641 | 8.21 | 8.50 | `5af19015` |
| system | `synth_1_voice` | 53945 | 55551 | 5.61 | 5.78 | `1816acc1` |
| system | `synth_2_voices` | 94106 | 96141 | 9.80 | 10.01 | `4dc805b7` |
| system | `synth_4_voices` | 170219 | 172702 | 17.73 | 17.98 | `2292dae2` |
| system | `synth_2x4` | 340449 | 345460 | 35.46 | 35.98 | `0d15b5eb` |
| system | `wave_2x4` | 319654 | 323035 | 33.29 | 33.64 | `6f28f4ea` |
| system | `fx_none` | 24608 | 25600 | 2.56 | 2.66 | `b538ce01` |
| system | `fx_grit` | 50876 | 52113 | 5.29 | 5.42 | `74f9b9f5` |
| system | `fx_flux_sdram` | 98954 | 103816 | 10.30 | 10.81 | `9ca91007` |
| system | `fx_comp` | 31601 | 32631 | 3.29 | 3.39 | `47a4392b` |
| system | `oliverb_solo_sram` | 99821 | 101357 | 10.39 | 10.55 | `82d044c4` |
| system | `instrument_init` | 679747 | 708045 | 70.80 | 73.75 | `3bbafe55` |
| system | `instrument_worst` | 947282 | 993483 | 98.67 | 103.48 | `851c7d43` |
| system | `inst_worst_deck_bus` | 738525 | 783467 | 76.92 | 81.61 | `6f9b841c` |
| system | `instrument_worst_bbd` | 898311 | 946347 | 93.57 | 98.57 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 882767 | 924161 | 91.95 | 96.26 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 898337 | 944511 | 93.57 | 98.38 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 745995 | 799939 | 77.70 | 83.32 | `ec6bd451` |
| system | `inst_feed_engine_idle` | 726187 | 737049 | 75.64 | 76.77 | `4fb5fdbf` |
| feed | `feed_pairs` | 77176 | 78896 | 8.03 | 8.21 | `d5cedcb2` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.39 | 10.64 |
| `instrument_worst_bbd_dtcm` | 91.09 | 96.08 |
| `instrument_worst_bbd` | 92.66 | 97.98 |
| `instrument_worst` | 98.01 | 102.34 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `26696514052c2c6aaf668d4a6c74569bd9a563f769dae106012dd024791f2849`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 12 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 78773 | 81319 | 8.20 | 8.47 | `5af19015` |
| system | `synth_1_voice` | 53944 | 55515 | 5.61 | 5.78 | `1816acc1` |
| system | `synth_2_voices` | 94108 | 96010 | 9.80 | 10.00 | `4dc805b7` |
| system | `synth_4_voices` | 169836 | 172516 | 17.69 | 17.97 | `2292dae2` |
| system | `synth_2x4` | 340452 | 345755 | 35.46 | 36.01 | `0d15b5eb` |
| system | `wave_2x4` | 319875 | 323277 | 33.32 | 33.67 | `6f28f4ea` |
| system | `fx_none` | 24607 | 25546 | 2.56 | 2.66 | `b538ce01` |
| system | `fx_grit` | 50877 | 52158 | 5.29 | 5.43 | `74f9b9f5` |
| system | `fx_flux_sdram` | 98964 | 104588 | 10.30 | 10.89 | `9ca91007` |
| system | `fx_comp` | 31501 | 32560 | 3.28 | 3.39 | `47a4392b` |
| system | `oliverb_solo_sram` | 99817 | 101324 | 10.39 | 10.55 | `82d044c4` |
| system | `instrument_init` | 679766 | 707341 | 70.80 | 73.68 | `3bbafe55` |
| system | `instrument_worst` | 947102 | 990199 | 98.65 | 103.14 | `851c7d43` |
| system | `inst_worst_deck_bus` | 738603 | 785869 | 76.93 | 81.86 | `6f9b841c` |
| system | `instrument_worst_bbd` | 898378 | 947345 | 93.58 | 98.68 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 882716 | 924371 | 91.94 | 96.28 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 898341 | 945017 | 93.57 | 98.43 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 746032 | 800117 | 77.71 | 83.34 | `ec6bd451` |
| system | `inst_feed_engine_idle` | 726179 | 737427 | 75.64 | 76.81 | `4fb5fdbf` |
| feed | `feed_pairs` | 77177 | 78921 | 8.03 | 8.22 | `d5cedcb2` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.39 | 10.61 |
| `instrument_worst_bbd_dtcm` | 91.09 | 96.37 |
| `instrument_worst_bbd` | 92.66 | 98.07 |
| `instrument_worst` | 98.00 | 102.84 |
