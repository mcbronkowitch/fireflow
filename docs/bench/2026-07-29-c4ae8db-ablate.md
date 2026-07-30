# Bench evidence 2026-07-29 — `c4ae8db`

## Gate ledger

Profile `ablate` — families: `system`, `instr`

Applied and passed:

- row set matches the profile exactly (no missing, no extra rows)
- no duplicate rows
- QSPI digest and device fingerprint identical across runs
- per-row checksums identical across runs
- at least two runs (`--repeat`, minimum 2)
- `wave_acceptance`: wave_2x4 no slower than synth_2x4, below the 960000-cycle block budget

Not applicable to this profile:

- none

Measured on a Daisy Seed (STM32H750). 480000000 Hz core clock, block size 96, dcache+icache, `-ffast-math -funroll-loops`. Block budget 960000 cycles.

All 2 runs report QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27` and device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8` (SHA-256 of the MCU UID).

## WAVE performance gate — PASS

All 2 runs satisfy the matched WAVE/SYNTH acceptance gates.

- **Run 1 — PASS:** `wave_2x4` average 306641 <= `synth_2x4` average 338951; maximum 309310 <= 343937; maximum 309310 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 306550 <= `synth_2x4` average 338950; maximum 309237 <= 344009; maximum 309237 < 960000.

## Verdict

**2x4 budget — go/no-go.** The full instrument at its worst case (8 voices, COLOR 4-note on both parts, all FX on, high diffusion, echo at max) costs **106 % of the block budget offline**, and **106 % measured inside a real audio callback**. The anchored figure is the one that decides. **Conclusion: the 2x4 architecture does not fit.** The anchored figure is over 100 % of the block budget, so the design has to shed voices or FX.

**Cost per candidate, relative to one real spotymod voice.**

- n/a (family `voice` not in this profile)

**SRAM vs SDRAM.** The grain-read proxy (8 scattered interpolated stereo reads per sample, identical window in both regions) costs **n/a (row missing)** in SDRAM against SRAM. That is a bare access pattern, written before the sampler existed to stand in for it; the `sampler_win_*` pair below is the same contrast with the real engine around it. The Oliverb pair reads **n/a (row missing)**, and the shortened echo-style streaming walk **n/a (row missing)**.

*Figures in this section are quoted to whole percentage points and two significant figures for ratios — honest to what this bench can actually resolve (intra-run jitter of roughly 1700 cycles on a 1.5M-cycle workload, and a cross-build layout shift that moved a 29K-cycle workload by about 7%). The tables below retain full measured precision.*

## Run 1

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 11 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 69017 | 71566 | 7.18 | 7.45 | `61d42d20` |
| system | `synth_1_voice` | 53413 | 54305 | 5.56 | 5.65 | `1816acc1` |
| system | `synth_2_voices` | 93487 | 95267 | 9.73 | 9.92 | `4dc805b7` |
| system | `synth_4_voices` | 169067 | 171473 | 17.61 | 17.86 | `2292dae2` |
| system | `synth_2x4` | 338951 | 343937 | 35.30 | 35.82 | `0d15b5eb` |
| system | `wave_2x4` | 306641 | 309310 | 31.94 | 32.21 | `6f28f4ea` |
| system | `fx_none` | 24413 | 24450 | 2.54 | 2.54 | `b538ce01` |
| system | `fx_grit` | 51953 | 52211 | 5.41 | 5.43 | `74f9b9f5` |
| system | `fx_flux_sdram` | 125139 | 125951 | 13.03 | 13.11 | `5b9094c3` |
| system | `fx_comp` | 31291 | 31366 | 3.25 | 3.26 | `47a4392b` |
| system | `oliverb_solo_sram` | 91829 | 92633 | 9.56 | 9.64 | `f09fa14e` |
| system | `instrument_init` | 616947 | 725558 | 64.26 | 75.57 | `2b52554a` |
| system | `instrument_worst` | 981892 | 1017588 | 102.28 | 105.99 | `4c4a29ce` |
| system | `instrument_worst_bbd` | 1019206 | 1056870 | 106.16 | 110.09 | `483e8e82` |
| instr | `instr_part_1` | 421354 | 441575 | 43.89 | 45.99 | `0a0be1b0` |
| instr | `instr_part_2` | 843444 | 883983 | 87.85 | 92.08 | `0604adaf` |
| instr | `instr_noverb` | 880202 | 918005 | 91.68 | 95.62 | `ac73d2a0` |
| instr | `deck_mod_hot` | 34273 | 36530 | 3.57 | 3.80 | `3a1002cf` |
| instr | `deck_engine_hot` | 172235 | 178573 | 17.94 | 18.60 | `f682ef98` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.61 | 9.88 |
| `instrument_worst` | 102.53 | 106.08 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `1157cbd949e6c4100daa57d61d85e016d57e733f949f39e9a967add1e5e22dc8`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 11 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 69249 | 71265 | 7.21 | 7.42 | `61d42d20` |
| system | `synth_1_voice` | 53415 | 54309 | 5.56 | 5.65 | `1816acc1` |
| system | `synth_2_voices` | 93489 | 95227 | 9.73 | 9.91 | `4dc805b7` |
| system | `synth_4_voices` | 169068 | 171429 | 17.61 | 17.85 | `2292dae2` |
| system | `synth_2x4` | 338950 | 344009 | 35.30 | 35.83 | `0d15b5eb` |
| system | `wave_2x4` | 306550 | 309237 | 31.93 | 32.21 | `6f28f4ea` |
| system | `fx_none` | 24413 | 24450 | 2.54 | 2.54 | `b538ce01` |
| system | `fx_grit` | 51957 | 52215 | 5.41 | 5.43 | `74f9b9f5` |
| system | `fx_flux_sdram` | 125148 | 126008 | 13.03 | 13.12 | `5b9094c3` |
| system | `fx_comp` | 31297 | 31388 | 3.26 | 3.26 | `47a4392b` |
| system | `oliverb_solo_sram` | 91839 | 92767 | 9.56 | 9.66 | `f09fa14e` |
| system | `instrument_init` | 616912 | 726160 | 64.26 | 75.64 | `2b52554a` |
| system | `instrument_worst` | 981847 | 1016692 | 102.27 | 105.90 | `4c4a29ce` |
| system | `instrument_worst_bbd` | 1019281 | 1056998 | 106.17 | 110.10 | `483e8e82` |
| instr | `instr_part_1` | 421363 | 441649 | 43.89 | 46.00 | `0a0be1b0` |
| instr | `instr_part_2` | 843396 | 884233 | 87.85 | 92.10 | `0604adaf` |
| instr | `instr_noverb` | 880266 | 917285 | 91.69 | 95.55 | `ac73d2a0` |
| instr | `deck_mod_hot` | 34267 | 36586 | 3.56 | 3.81 | `3a1002cf` |
| instr | `deck_engine_hot` | 172234 | 178662 | 17.94 | 18.61 | `f682ef98` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 9.61 | 9.88 |
| `instrument_worst` | 102.52 | 106.07 |
