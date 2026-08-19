# Bench evidence 2026-08-19 — `ab0a6bf`

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

- **Run 1 — PASS:** `wave_2x4` average 319846 <= `synth_2x4` average 341292; maximum 323229 <= 346278; maximum 323229 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 320034 <= `synth_2x4` average 341290; maximum 323349 <= 346337; maximum 323349 < 960000.

## Verdict

**DTCM BBD-engine budget — go/no-go.** The decision workload is `instrument_worst_bbd_dtcm`: the full instrument with both decks on the BBD part engine, shortest division, clock ceiling, maximum COLOR/feedback/mix, STEP freeze engaged, live inputs, and retained DTCM instrument state. `instrument_worst_bbd` is its checksum-equal AXI comparison. `fx_flux_sdram` separately prices stereo tape FLUX; the five retained `sweep_flux_rate_*` rows carry its delay-time cost curve. Run maxima (offline / real callback): Run 1 97.10 % / 96.81 %; Run 2 97.13 % / 96.90 %. Across all 2 repeats, the worst maxima are **97.13 % offline** and **96.90 % in the real callback**. **Conclusion: the DTCM BBD-engine gate fits.** Every offline and real-callback maximum is below 100 % of the block budget.

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
| system | `mod_plane_2x_center` | 79513 | 82722 | 8.28 | 8.61 | `5af19015` |
| system | `synth_1_voice` | 54273 | 55800 | 5.65 | 5.81 | `1816acc1` |
| system | `synth_2_voices` | 94436 | 96566 | 9.83 | 10.05 | `4dc805b7` |
| system | `synth_4_voices` | 170165 | 172738 | 17.72 | 17.99 | `2292dae2` |
| system | `synth_2x4` | 341292 | 346278 | 35.55 | 36.07 | `0d15b5eb` |
| system | `wave_2x4` | 319846 | 323229 | 33.31 | 33.66 | `6f28f4ea` |
| system | `fx_none` | 24510 | 25452 | 2.55 | 2.65 | `b538ce01` |
| system | `fx_grit` | 50874 | 52157 | 5.29 | 5.43 | `74f9b9f5` |
| system | `fx_flux_sdram` | 98933 | 102041 | 10.30 | 10.62 | `9ca91007` |
| system | `fx_comp` | 31500 | 32541 | 3.28 | 3.38 | `47a4392b` |
| system | `oliverb_solo_sram` | 99827 | 101471 | 10.39 | 10.56 | `82d044c4` |
| system | `instrument_init` | 683058 | 710897 | 71.15 | 74.05 | `3bbafe55` |
| system | `instrument_worst` | 949057 | 989025 | 98.86 | 103.02 | `851c7d43` |
| system | `inst_worst_deck_bus` | 738848 | 784309 | 76.96 | 81.69 | `6f9b841c` |
| system | `instrument_worst_bbd` | 899533 | 942759 | 93.70 | 98.20 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 890679 | 932199 | 92.77 | 97.10 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 899533 | 941371 | 93.70 | 98.05 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 888687 | 951817 | 92.57 | 99.14 | `6dba3657` |
| system | `inst_feed_engine_idle` | 874979 | 885147 | 91.14 | 92.20 | `24d29f27` |
| feed | `feed_pairs` | 151190 | 152940 | 15.74 | 15.93 | `cde68d0d` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.39 | 10.61 |
| `instrument_worst_bbd_dtcm` | 91.93 | 96.81 |
| `instrument_worst_bbd` | 92.78 | 97.80 |
| `instrument_worst` | 98.24 | 102.73 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `26696514052c2c6aaf668d4a6c74569bd9a563f769dae106012dd024791f2849`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 12 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 79574 | 82488 | 8.28 | 8.59 | `5af19015` |
| system | `synth_1_voice` | 54272 | 55946 | 5.65 | 5.82 | `1816acc1` |
| system | `synth_2_voices` | 94434 | 96664 | 9.83 | 10.06 | `4dc805b7` |
| system | `synth_4_voices` | 169780 | 172239 | 17.68 | 17.94 | `2292dae2` |
| system | `synth_2x4` | 341290 | 346337 | 35.55 | 36.07 | `0d15b5eb` |
| system | `wave_2x4` | 320034 | 323349 | 33.33 | 33.68 | `6f28f4ea` |
| system | `fx_none` | 24511 | 25450 | 2.55 | 2.65 | `b538ce01` |
| system | `fx_grit` | 50874 | 52112 | 5.29 | 5.42 | `74f9b9f5` |
| system | `fx_flux_sdram` | 98912 | 101538 | 10.30 | 10.57 | `9ca91007` |
| system | `fx_comp` | 31505 | 32547 | 3.28 | 3.39 | `47a4392b` |
| system | `oliverb_solo_sram` | 99821 | 101382 | 10.39 | 10.56 | `82d044c4` |
| system | `instrument_init` | 683033 | 710559 | 71.14 | 74.01 | `3bbafe55` |
| system | `instrument_worst` | 949096 | 987321 | 98.86 | 102.84 | `851c7d43` |
| system | `inst_worst_deck_bus` | 738895 | 782639 | 76.96 | 81.52 | `6f9b841c` |
| system | `instrument_worst_bbd` | 899439 | 941213 | 93.69 | 98.04 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 890719 | 932499 | 92.78 | 97.13 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 899495 | 940679 | 93.69 | 97.98 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 888655 | 952003 | 92.56 | 99.16 | `6dba3657` |
| system | `inst_feed_engine_idle` | 874954 | 884653 | 91.14 | 92.15 | `24d29f27` |
| feed | `feed_pairs` | 151191 | 152971 | 15.74 | 15.93 | `cde68d0d` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.39 | 10.62 |
| `instrument_worst_bbd_dtcm` | 91.94 | 96.90 |
| `instrument_worst_bbd` | 92.78 | 97.72 |
| `instrument_worst` | 98.23 | 102.86 |
