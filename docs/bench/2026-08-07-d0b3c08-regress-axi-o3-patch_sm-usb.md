# Bench evidence 2026-08-07 — `d0b3c08`

## Gate ledger

Execution layout: `axi`.

Optimization: `o3` (`-O3`).

Profile `regress` — families: `system`, `bbd`

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

- **Run 1 — PASS:** `wave_2x4` average 285819 <= `synth_2x4` average 329987; maximum 291714 <= 335758; maximum 291714 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 285935 <= `synth_2x4` average 329986; maximum 292267 <= 335015; maximum 292267 < 960000.

## Verdict

**DTCM BBD-engine budget — go/no-go.** The decision workload is `instrument_worst_bbd_dtcm`: the full instrument with both decks on the BBD part engine, shortest division, clock ceiling, maximum COLOR/feedback/mix, STEP freeze engaged, live inputs, and retained DTCM instrument state. `instrument_worst_bbd` is its checksum-equal AXI comparison. `fx_flux_sdram` separately prices stereo tape FLUX; the five retained `sweep_flux_rate_*` rows carry its delay-time cost curve. Run maxima (offline / real callback): Run 1 97.56 % / 97.75 %; Run 2 97.83 % / 97.78 %. Across all 2 repeats, the worst maxima are **97.83 % offline** and **97.78 % in the real callback**. **Conclusion: the DTCM BBD-engine gate fits.** Every offline and real-callback maximum is below 100 % of the block budget.

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
| system | `mod_plane_2x_center` | 66760 | 69092 | 6.95 | 7.19 | `61d42d20` |
| system | `synth_1_voice` | 52111 | 53715 | 5.42 | 5.59 | `1816acc1` |
| system | `synth_2_voices` | 91219 | 93017 | 9.50 | 9.68 | `4dc805b7` |
| system | `synth_4_voices` | 164544 | 167602 | 17.14 | 17.45 | `2292dae2` |
| system | `synth_2x4` | 329987 | 335758 | 34.37 | 34.97 | `0d15b5eb` |
| system | `wave_2x4` | 285819 | 291714 | 29.77 | 30.38 | `6f28f4ea` |
| system | `fx_none` | 21721 | 22657 | 2.26 | 2.36 | `b538ce01` |
| system | `fx_grit` | 45881 | 46907 | 4.77 | 4.88 | `74f9b9f5` |
| system | `fx_flux_sdram` | 103089 | 112228 | 10.73 | 11.69 | `9ca91007` |
| system | `fx_comp` | 29107 | 30160 | 3.03 | 3.14 | `47a4392b` |
| system | `oliverb_solo_sram` | 95615 | 97181 | 9.95 | 10.12 | `82d044c4` |
| system | `instrument_init` | 639262 | 748511 | 66.58 | 77.96 | `8e9a0dbf` |
| system | `instrument_worst` | 981819 | 1042824 | 102.27 | 108.62 | `7d827e50` |
| system | `inst_worst_deck_bus` | 737565 | 782959 | 76.82 | 81.55 | `aa8ee611` |
| system | `instrument_worst_bbd` | 909163 | 952877 | 94.70 | 99.25 | `6d20538d` |
| system | `instrument_worst_bbd_dtcm` | 900657 | 936658 | 93.81 | 97.56 | `6d20538d` |
| system | `inst_bbd_engine_worst` | 909107 | 956920 | 94.69 | 99.67 | `6d20538d` |
| bbd | `bbd_ceiling` | 51917 | 54403 | 5.40 | 5.66 | `7f70a86d` |
| bbd | `bbd_line_only` | 33743 | 35192 | 3.51 | 3.66 | `e69ddb3b` |
| bbd | `bbd_line_tap` | 32854 | 33792 | 3.42 | 3.52 | `b9c14970` |
| bbd | `bbd_line_tap_half` | 24139 | 25096 | 2.51 | 2.61 | `adcd93a1` |
| bbd | `bbd_walk_sdram` | 1026 | 4139 | 0.10 | 0.43 | `7728ad1a` |
| bbd | `bbd_line_stage_walk` | 35796 | 36746 | 3.72 | 3.82 | `5c603aa8` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.96 | 10.20 |
| `instrument_worst_bbd_dtcm` | 93.00 | 97.75 |
| `instrument_worst_bbd` | 93.84 | 98.81 |
| `instrument_worst` | 101.99 | 107.83 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `26696514052c2c6aaf668d4a6c74569bd9a563f769dae106012dd024791f2849`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 33 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 66567 | 69017 | 6.93 | 7.18 | `61d42d20` |
| system | `synth_1_voice` | 52111 | 53724 | 5.42 | 5.59 | `1816acc1` |
| system | `synth_2_voices` | 91220 | 92897 | 9.50 | 9.67 | `4dc805b7` |
| system | `synth_4_voices` | 164545 | 167679 | 17.14 | 17.46 | `2292dae2` |
| system | `synth_2x4` | 329986 | 335015 | 34.37 | 34.89 | `0d15b5eb` |
| system | `wave_2x4` | 285935 | 292267 | 29.78 | 30.44 | `6f28f4ea` |
| system | `fx_none` | 21722 | 22651 | 2.26 | 2.35 | `b538ce01` |
| system | `fx_grit` | 45882 | 46906 | 4.77 | 4.88 | `74f9b9f5` |
| system | `fx_flux_sdram` | 103092 | 111716 | 10.73 | 11.63 | `9ca91007` |
| system | `fx_comp` | 29106 | 30168 | 3.03 | 3.14 | `47a4392b` |
| system | `oliverb_solo_sram` | 95611 | 97300 | 9.95 | 10.13 | `82d044c4` |
| system | `instrument_init` | 639292 | 747659 | 66.59 | 77.88 | `8e9a0dbf` |
| system | `instrument_worst` | 981888 | 1043418 | 102.28 | 108.68 | `7d827e50` |
| system | `inst_worst_deck_bus` | 737373 | 783535 | 76.80 | 81.61 | `aa8ee611` |
| system | `instrument_worst_bbd` | 909094 | 951217 | 94.69 | 99.08 | `6d20538d` |
| system | `instrument_worst_bbd_dtcm` | 900689 | 939196 | 93.82 | 97.83 | `6d20538d` |
| system | `inst_bbd_engine_worst` | 909103 | 954645 | 94.69 | 99.44 | `6d20538d` |
| bbd | `bbd_ceiling` | 51910 | 54279 | 5.40 | 5.65 | `7f70a86d` |
| bbd | `bbd_line_only` | 33751 | 35228 | 3.51 | 3.66 | `e69ddb3b` |
| bbd | `bbd_line_tap` | 32854 | 33795 | 3.42 | 3.52 | `b9c14970` |
| bbd | `bbd_line_tap_half` | 24135 | 25093 | 2.51 | 2.61 | `adcd93a1` |
| bbd | `bbd_walk_sdram` | 1015 | 4111 | 0.10 | 0.42 | `7728ad1a` |
| bbd | `bbd_line_stage_walk` | 35798 | 36728 | 3.72 | 3.82 | `5c603aa8` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.96 | 10.23 |
| `instrument_worst_bbd_dtcm` | 93.00 | 97.78 |
| `instrument_worst_bbd` | 93.84 | 98.77 |
| `instrument_worst` | 101.95 | 107.86 |
