# Bench evidence 2026-08-07 — `4aed1bf`

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

- **Run 1 — PASS:** `wave_2x4` average 284843 <= `synth_2x4` average 328835; maximum 289403 <= 334192; maximum 289403 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 284882 <= `synth_2x4` average 328828; maximum 291386 <= 334252; maximum 291386 < 960000.

## Verdict

**DTCM BBD-engine budget — go/no-go.** The decision workload is `instrument_worst_bbd_dtcm`: the full instrument with both decks on the BBD part engine, shortest division, clock ceiling, maximum COLOR/feedback/mix, STEP freeze engaged, live inputs, and retained DTCM instrument state. `instrument_worst_bbd` is its checksum-equal AXI comparison. `fx_flux_sdram` separately prices stereo tape FLUX; the five retained `sweep_flux_rate_*` rows carry its delay-time cost curve. Run maxima (offline / real callback): Run 1 96.24 % / 96.84 %; Run 2 96.15 % / 96.93 %. Across all 2 repeats, the worst maxima are **96.24 % offline** and **96.93 % in the real callback**. **Conclusion: the DTCM BBD-engine gate fits.** Every offline and real-callback maximum is below 100 % of the block budget.

**Cost per candidate, relative to one real fireflow voice.**

- n/a (family `voice` not in this profile)

**SRAM vs SDRAM.** The grain-read proxy (8 scattered interpolated stereo reads per sample, identical window in both regions) costs **n/a (row missing)** in SDRAM against SRAM. That is a bare access pattern, written before the sampler existed to stand in for it; the `sampler_win_*` pair below is the same contrast with the real engine around it. The Oliverb pair reads **n/a (row missing)**, and the shortened echo-style streaming walk **n/a (row missing)**.

*The decision gate retains the firmware's two-decimal percentages because values immediately around 100 % determine the stop gate. Other prose uses whole percentage points and two significant figures for ratios; the tables below retain full measured precision.*

## Run 1

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 13 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 66654 | 69131 | 6.94 | 7.20 | `61d42d20` |
| system | `synth_1_voice` | 51949 | 52831 | 5.41 | 5.50 | `1816acc1` |
| system | `synth_2_voices` | 90897 | 92327 | 9.46 | 9.61 | `4dc805b7` |
| system | `synth_4_voices` | 163924 | 166833 | 17.07 | 17.37 | `2292dae2` |
| system | `synth_2x4` | 328835 | 334192 | 34.25 | 34.81 | `0d15b5eb` |
| system | `wave_2x4` | 284843 | 289403 | 29.67 | 30.14 | `6f28f4ea` |
| system | `fx_none` | 21629 | 21667 | 2.25 | 2.25 | `b538ce01` |
| system | `fx_grit` | 45753 | 45900 | 4.76 | 4.78 | `74f9b9f5` |
| system | `fx_flux_sdram` | 102252 | 107503 | 10.65 | 11.19 | `9ca91007` |
| system | `fx_comp` | 29070 | 29152 | 3.02 | 3.03 | `47a4392b` |
| system | `oliverb_solo_sram` | 95460 | 96399 | 9.94 | 10.04 | `82d044c4` |
| system | `instrument_init` | 635661 | 743734 | 66.21 | 77.47 | `8e9a0dbf` |
| system | `instrument_worst` | 974109 | 1029544 | 101.46 | 107.24 | `7d827e50` |
| system | `inst_worst_deck_bus` | 737299 | 786914 | 76.80 | 81.97 | `aa8ee611` |
| system | `instrument_worst_bbd` | 889256 | 926340 | 92.63 | 96.49 | `6d20538d` |
| system | `instrument_worst_bbd_dtcm` | 882470 | 923990 | 91.92 | 96.24 | `6d20538d` |
| system | `inst_bbd_engine_worst` | 889241 | 926608 | 92.62 | 96.52 | `6d20538d` |
| bbd | `bbd_ceiling` | 51682 | 53259 | 5.38 | 5.54 | `7f70a86d` |
| bbd | `bbd_line_only` | 33622 | 34176 | 3.50 | 3.56 | `e69ddb3b` |
| bbd | `bbd_line_tap` | 32709 | 32841 | 3.40 | 3.42 | `b9c14970` |
| bbd | `bbd_line_tap_half` | 24013 | 24121 | 2.50 | 2.51 | `adcd93a1` |
| bbd | `bbd_walk_sdram` | 946 | 3544 | 0.09 | 0.36 | `7728ad1a` |
| bbd | `bbd_line_stage_walk` | 35655 | 35832 | 3.71 | 3.73 | `5c603aa8` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.99 | 10.25 |
| `instrument_worst_bbd_dtcm` | 91.78 | 96.84 |
| `instrument_worst_bbd` | 92.49 | 97.64 |
| `instrument_worst` | 101.83 | 107.45 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 13 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 66658 | 69163 | 6.94 | 7.20 | `61d42d20` |
| system | `synth_1_voice` | 51949 | 52822 | 5.41 | 5.50 | `1816acc1` |
| system | `synth_2_voices` | 90899 | 92349 | 9.46 | 9.61 | `4dc805b7` |
| system | `synth_4_voices` | 163926 | 166787 | 17.07 | 17.37 | `2292dae2` |
| system | `synth_2x4` | 328828 | 334252 | 34.25 | 34.81 | `0d15b5eb` |
| system | `wave_2x4` | 284882 | 291386 | 29.67 | 30.35 | `6f28f4ea` |
| system | `fx_none` | 21629 | 21666 | 2.25 | 2.25 | `b538ce01` |
| system | `fx_grit` | 45746 | 45884 | 4.76 | 4.77 | `74f9b9f5` |
| system | `fx_flux_sdram` | 102267 | 108000 | 10.65 | 11.25 | `9ca91007` |
| system | `fx_comp` | 29068 | 29148 | 3.02 | 3.03 | `47a4392b` |
| system | `oliverb_solo_sram` | 95457 | 96342 | 9.94 | 10.03 | `82d044c4` |
| system | `instrument_init` | 635673 | 744962 | 66.21 | 77.60 | `8e9a0dbf` |
| system | `instrument_worst` | 974140 | 1028780 | 101.47 | 107.16 | `7d827e50` |
| system | `inst_worst_deck_bus` | 737370 | 785684 | 76.80 | 81.84 | `aa8ee611` |
| system | `instrument_worst_bbd` | 889254 | 926924 | 92.63 | 96.55 | `6d20538d` |
| system | `instrument_worst_bbd_dtcm` | 882447 | 923068 | 91.92 | 96.15 | `6d20538d` |
| system | `inst_bbd_engine_worst` | 889220 | 928484 | 92.62 | 96.71 | `6d20538d` |
| bbd | `bbd_ceiling` | 51681 | 53359 | 5.38 | 5.55 | `7f70a86d` |
| bbd | `bbd_line_only` | 33592 | 34118 | 3.49 | 3.55 | `e69ddb3b` |
| bbd | `bbd_line_tap` | 32710 | 32984 | 3.40 | 3.43 | `b9c14970` |
| bbd | `bbd_line_tap_half` | 24012 | 24117 | 2.50 | 2.51 | `adcd93a1` |
| bbd | `bbd_walk_sdram` | 943 | 3268 | 0.09 | 0.34 | `7728ad1a` |
| bbd | `bbd_line_stage_walk` | 35643 | 35780 | 3.71 | 3.72 | `5c603aa8` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.99 | 10.25 |
| `instrument_worst_bbd_dtcm` | 91.78 | 96.93 |
| `instrument_worst_bbd` | 92.47 | 97.62 |
| `instrument_worst` | 101.85 | 106.98 |
