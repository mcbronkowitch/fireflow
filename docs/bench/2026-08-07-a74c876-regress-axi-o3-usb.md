# Bench evidence 2026-08-07 — `a74c876`

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

Measured on a Daisy Seed (STM32H750). 480000000 Hz core clock, block size 96, dcache+icache. Block budget 960000 cycles.

All 2 runs report QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27` and device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8` (SHA-256 of the MCU UID).

## WAVE performance gate — PASS

All 2 runs satisfy the matched WAVE/SYNTH acceptance gates.

- **Run 1 — PASS:** `wave_2x4` average 284988 <= `synth_2x4` average 328659; maximum 289854 <= 333956; maximum 289854 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 284982 <= `synth_2x4` average 328651; maximum 290217 <= 334006; maximum 290217 < 960000.

## Verdict

**DTCM BBD-engine budget — go/no-go.** The decision workload is `instrument_worst_bbd_dtcm`: the full instrument with both decks on the BBD part engine, shortest division, clock ceiling, maximum COLOR/feedback/mix, STEP freeze engaged, live inputs, and retained DTCM instrument state. `instrument_worst_bbd` is its checksum-equal AXI comparison. `fx_flux_sdram` separately prices stereo tape FLUX; the five retained `sweep_flux_rate_*` rows carry its delay-time cost curve. Run maxima (offline / real callback): Run 1 97.03 % / 97.18 %; Run 2 96.94 % / 97.10 %. Across all 2 repeats, the worst maxima are **97.03 % offline** and **97.18 % in the real callback**. **Conclusion: the DTCM BBD-engine gate fits.** Every offline and real-callback maximum is below 100 % of the block budget.

**Cost per candidate, relative to one real fireflow voice.**

- n/a (family `voice` not in this profile)

**SRAM vs SDRAM.** The grain-read proxy (8 scattered interpolated stereo reads per sample, identical window in both regions) costs **n/a (row missing)** in SDRAM against SRAM. That is a bare access pattern, written before the sampler existed to stand in for it; the `sampler_win_*` pair below is the same contrast with the real engine around it. The Oliverb pair reads **n/a (row missing)**, and the shortened echo-style streaming walk **n/a (row missing)**.

*The decision gate retains the firmware's two-decimal percentages because values immediately around 100 % determine the stop gate. Other prose uses whole percentage points and two significant figures for ratios; the tables below retain full measured precision.*

## Run 1

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 12 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 66085 | 68222 | 6.88 | 7.10 | `61d42d20` |
| system | `synth_1_voice` | 51896 | 52791 | 5.40 | 5.49 | `1816acc1` |
| system | `synth_2_voices` | 90853 | 92268 | 9.46 | 9.61 | `4dc805b7` |
| system | `synth_4_voices` | 163884 | 166745 | 17.07 | 17.36 | `2292dae2` |
| system | `synth_2x4` | 328659 | 333956 | 34.23 | 34.78 | `0d15b5eb` |
| system | `wave_2x4` | 284988 | 289854 | 29.68 | 30.19 | `6f28f4ea` |
| system | `fx_none` | 21630 | 21707 | 2.25 | 2.26 | `b538ce01` |
| system | `fx_grit` | 45679 | 45798 | 4.75 | 4.77 | `74f9b9f5` |
| system | `fx_flux_sdram` | 102926 | 109424 | 10.72 | 11.39 | `9ca91007` |
| system | `fx_comp` | 28979 | 29055 | 3.01 | 3.02 | `47a4392b` |
| system | `oliverb_solo_sram` | 95485 | 96542 | 9.94 | 10.05 | `82d044c4` |
| system | `instrument_init` | 634563 | 743151 | 66.10 | 77.41 | `8e9a0dbf` |
| system | `instrument_worst` | 970825 | 1027201 | 101.12 | 107.00 | `7d827e50` |
| system | `inst_worst_deck_bus` | 730216 | 780715 | 76.06 | 81.32 | `aa8ee611` |
| system | `instrument_worst_bbd` | 895567 | 936915 | 93.28 | 97.59 | `6d20538d` |
| system | `instrument_worst_bbd_dtcm` | 888833 | 931515 | 92.58 | 97.03 | `6d20538d` |
| system | `inst_bbd_engine_worst` | 895593 | 939244 | 93.29 | 97.83 | `6d20538d` |
| bbd | `bbd_ceiling` | 51690 | 53300 | 5.38 | 5.55 | `7f70a86d` |
| bbd | `bbd_line_only` | 33598 | 34113 | 3.49 | 3.55 | `e69ddb3b` |
| bbd | `bbd_line_tap` | 32713 | 32828 | 3.40 | 3.41 | `b9c14970` |
| bbd | `bbd_line_tap_half` | 24051 | 24123 | 2.50 | 2.51 | `adcd93a1` |
| bbd | `bbd_walk_sdram` | 951 | 3972 | 0.09 | 0.41 | `7728ad1a` |
| bbd | `bbd_line_stage_walk` | 35642 | 35745 | 3.71 | 3.72 | `5c603aa8` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.99 | 10.23 |
| `instrument_worst_bbd_dtcm` | 92.44 | 97.18 |
| `instrument_worst_bbd` | 93.15 | 97.90 |
| `instrument_worst` | 101.48 | 106.88 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 12 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 66085 | 68192 | 6.88 | 7.10 | `61d42d20` |
| system | `synth_1_voice` | 51902 | 52792 | 5.40 | 5.49 | `1816acc1` |
| system | `synth_2_voices` | 90858 | 92217 | 9.46 | 9.60 | `4dc805b7` |
| system | `synth_4_voices` | 163884 | 166679 | 17.07 | 17.36 | `2292dae2` |
| system | `synth_2x4` | 328651 | 334006 | 34.23 | 34.79 | `0d15b5eb` |
| system | `wave_2x4` | 284982 | 290217 | 29.68 | 30.23 | `6f28f4ea` |
| system | `fx_none` | 21630 | 21707 | 2.25 | 2.26 | `b538ce01` |
| system | `fx_grit` | 45683 | 45810 | 4.75 | 4.77 | `74f9b9f5` |
| system | `fx_flux_sdram` | 102733 | 106421 | 10.70 | 11.08 | `9ca91007` |
| system | `fx_comp` | 28979 | 29055 | 3.01 | 3.02 | `47a4392b` |
| system | `oliverb_solo_sram` | 95476 | 96384 | 9.94 | 10.04 | `82d044c4` |
| system | `instrument_init` | 634571 | 741471 | 66.10 | 77.23 | `8e9a0dbf` |
| system | `instrument_worst` | 970752 | 1027637 | 101.12 | 107.04 | `7d827e50` |
| system | `inst_worst_deck_bus` | 730297 | 780660 | 76.07 | 81.31 | `aa8ee611` |
| system | `instrument_worst_bbd` | 895545 | 935897 | 93.28 | 97.48 | `6d20538d` |
| system | `instrument_worst_bbd_dtcm` | 888830 | 930708 | 92.58 | 96.94 | `6d20538d` |
| system | `inst_bbd_engine_worst` | 895540 | 936777 | 93.28 | 97.58 | `6d20538d` |
| bbd | `bbd_ceiling` | 51689 | 53298 | 5.38 | 5.55 | `7f70a86d` |
| bbd | `bbd_line_only` | 33578 | 34127 | 3.49 | 3.55 | `e69ddb3b` |
| bbd | `bbd_line_tap` | 32713 | 32828 | 3.40 | 3.41 | `b9c14970` |
| bbd | `bbd_line_tap_half` | 24022 | 24091 | 2.50 | 2.50 | `adcd93a1` |
| bbd | `bbd_walk_sdram` | 946 | 4103 | 0.09 | 0.42 | `7728ad1a` |
| bbd | `bbd_line_stage_walk` | 35661 | 35716 | 3.71 | 3.72 | `5c603aa8` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.99 | 10.23 |
| `instrument_worst_bbd_dtcm` | 92.47 | 97.10 |
| `instrument_worst_bbd` | 93.16 | 97.79 |
| `instrument_worst` | 101.41 | 107.12 |
