# Bench evidence 2026-07-31 — `b9afe47`

## Gate ledger

Execution layout: `itcm-hot`.

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

All 2 runs report QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27` and device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8` (SHA-256 of the MCU UID).

## WAVE performance gate — PASS

All 2 runs satisfy the matched WAVE/SYNTH acceptance gates.

- **Run 1 — PASS:** `wave_2x4` average 307240 <= `synth_2x4` average 338774; maximum 309889 <= 343322; maximum 309889 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 307236 <= `synth_2x4` average 338774; maximum 309880 <= 343323; maximum 309880 < 960000.

## Verdict

**DTCM+BBD budget — go/no-go.** The decision workload is `instrument_worst_bbd_dtcm`: the full eight-voice instrument, BBD echo and the retained DTCM instrument state. Run maxima (offline / real callback): Run 1 103.83 % / 103.88 %; Run 2 103.78 % / 103.91 %. Across all 2 repeats, the worst maxima are **103.83 % offline** and **103.91 % in the real callback**. **Conclusion: the DTCM+BBD gate does not fit.** At least one offline or real-callback maximum is at or above 100 % of the block budget.

**Cost per candidate, relative to one real spotymod voice.**

- n/a (family `voice` not in this profile)

**SRAM vs SDRAM.** The grain-read proxy (8 scattered interpolated stereo reads per sample, identical window in both regions) costs **n/a (row missing)** in SDRAM against SRAM. That is a bare access pattern, written before the sampler existed to stand in for it; the `sampler_win_*` pair below is the same contrast with the real engine around it. The Oliverb pair reads **n/a (row missing)**, and the shortened echo-style streaming walk **n/a (row missing)**.

*The decision gate retains the firmware's two-decimal percentages because values immediately around 100 % determine the stop gate. Other prose uses whole percentage points and two significant figures for ratios; the tables below retain full measured precision.*

## Run 1

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 11 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 68626 | 70618 | 7.14 | 7.35 | `61d42d20` |
| system | `synth_1_voice` | 54154 | 55037 | 5.64 | 5.73 | `1816acc1` |
| system | `synth_2_voices` | 94184 | 95805 | 9.81 | 9.97 | `4dc805b7` |
| system | `synth_4_voices` | 169628 | 171763 | 17.66 | 17.89 | `2292dae2` |
| system | `synth_2x4` | 338774 | 343322 | 35.28 | 35.76 | `0d15b5eb` |
| system | `wave_2x4` | 307240 | 309889 | 32.00 | 32.28 | `6f28f4ea` |
| system | `fx_none` | 26334 | 26372 | 2.74 | 2.74 | `b538ce01` |
| system | `fx_grit` | 50328 | 50430 | 5.24 | 5.25 | `74f9b9f5` |
| system | `fx_flux_sdram` | 127807 | 128678 | 13.31 | 13.40 | `5b9094c3` |
| system | `fx_comp` | 33266 | 33333 | 3.46 | 3.47 | `47a4392b` |
| system | `oliverb_solo_sram` | 90829 | 91585 | 9.46 | 9.54 | `f09fa14e` |
| system | `instrument_init` | 581653 | 686194 | 60.58 | 71.47 | `2b52554a` |
| system | `instrument_worst` | 925870 | 961854 | 96.44 | 100.19 | `4c4a29ce` |
| system | `inst_worst_deck_bus` | 683287 | 717658 | 71.17 | 74.75 | `8eaf4037` |
| system | `instrument_worst_bbd` | 964012 | 1002339 | 100.41 | 104.41 | `483e8e82` |
| system | `instrument_worst_bbd_dtcm` | 958818 | 996841 | 99.87 | 103.83 | `483e8e82` |
| system | `inst_bbd_engine_worst` | 795733 | 827429 | 82.88 | 86.19 | `7636f0a7` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.51 | 9.68 |
| `instrument_worst_bbd_dtcm` | 100.02 | 103.88 |
| `instrument_worst_bbd` | 100.54 | 104.49 |
| `instrument_worst` | 96.64 | 100.25 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 11 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 68624 | 70586 | 7.14 | 7.35 | `61d42d20` |
| system | `synth_1_voice` | 54154 | 55037 | 5.64 | 5.73 | `1816acc1` |
| system | `synth_2_voices` | 94184 | 95805 | 9.81 | 9.97 | `4dc805b7` |
| system | `synth_4_voices` | 169628 | 171763 | 17.66 | 17.89 | `2292dae2` |
| system | `synth_2x4` | 338774 | 343323 | 35.28 | 35.76 | `0d15b5eb` |
| system | `wave_2x4` | 307236 | 309880 | 32.00 | 32.27 | `6f28f4ea` |
| system | `fx_none` | 26334 | 26371 | 2.74 | 2.74 | `b538ce01` |
| system | `fx_grit` | 50336 | 50430 | 5.24 | 5.25 | `74f9b9f5` |
| system | `fx_flux_sdram` | 127815 | 128730 | 13.31 | 13.40 | `5b9094c3` |
| system | `fx_comp` | 33265 | 33335 | 3.46 | 3.47 | `47a4392b` |
| system | `oliverb_solo_sram` | 90826 | 91642 | 9.46 | 9.54 | `f09fa14e` |
| system | `instrument_init` | 581642 | 685879 | 60.58 | 71.44 | `2b52554a` |
| system | `instrument_worst` | 925891 | 962028 | 96.44 | 100.21 | `4c4a29ce` |
| system | `inst_worst_deck_bus` | 683281 | 717843 | 71.17 | 74.77 | `8eaf4037` |
| system | `instrument_worst_bbd` | 964040 | 1002621 | 100.42 | 104.43 | `483e8e82` |
| system | `instrument_worst_bbd_dtcm` | 958820 | 996351 | 99.87 | 103.78 | `483e8e82` |
| system | `inst_bbd_engine_worst` | 795747 | 828229 | 82.89 | 86.27 | `7636f0a7` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.51 | 9.69 |
| `instrument_worst_bbd_dtcm` | 100.02 | 103.91 |
| `instrument_worst_bbd` | 100.54 | 104.47 |
| `instrument_worst` | 96.65 | 100.26 |
