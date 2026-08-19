## P is decided: 6 pairs

**These are Patch Submodule numbers. No Seed figure entered this decision**, and
no `-O3` figure either: the `system` family overflows SRAM_EXEC by 2844 B at
`-O3`, and did so before FEED existed. Everything below is `-O2`, AXI layout,
over USB, two runs per point with identical checksums.

### The three sweep points

`feed_cfg::kPairs` is a compile-time constant, so each point is its own image
and its own commit -- `run.py` refuses evidence from a dirty tree.

| P | commit | `feed_pairs` avg | `inst_feed_engine_worst` avg / max | % of budget |
|---:|---|---:|---|---:|
| 2 | `f836a32` | 39 110 | 662 453 / 710 455 | 74.0 |
| 4 | `500775c` | 77 176 | 745 995 / 799 939 | 83.3 |
| 8 | `ab0a6bf` | 151 190 | 888 687 / 951 817 | 99.2 |

### Cycles per pair

- slope 2 → 4: **19 033** cycles per pair
- slope 4 → 8: **18 504** cycles per pair
- ratio 0.972, i.e. the cost is linear in P to within 3 % over a 4× range

Least squares over the three points: **18 655 cycles per pair**, plus about
**2 100 cycles** of fixed overhead. The overhead is 5 % of one pair, so
`feed_pairs` is very nearly pure per-pair cost and the slope is the whole
story.

The whole-engine row is an independent check on that, and it agrees: fitting
`inst_feed_engine_worst` gives 37 415 cycles per pair for two decks, i.e.
**18 708 per pair per deck** -- within 0.3 % of the bank-only figure. SWARM's
history is the reason this check exists: its kernel row alone sized the bank
too generously and the whole-engine row corrected the reading. Here the two do
not disagree.

### The budget, and the reserve

The decision workload is `inst_feed_engine_worst` -- both decks on FEED,
worst-case knobs -- whose maximum fits `39 904*P + 634 516`.

**The reserve is not a formality.** That row runs with **FLUX off**, exactly as
`inst_bbd_engine_worst` does, so none of the percentages above include stereo
tape. FLUX is one switch away and prices at 10.3 % on its own. P=8 measured
99.2 % and had no room for it at all.

At a ~9 % reserve the ceiling is 6.95 pairs. Rounded **down** to a multiple of
`kPairsPerTone` = 2, that is **P = 6**. The rounding is not cosmetic: an odd
pair left over sits in a group of one, and SPREAD detunes a tone's pairs
against each other, so a lone pair has nothing to beat against.

### Confirmed, not interpolated

6 is not one of the sweep points, so it got its own run -- this one:

| workload | avg | max | % of budget | predicted max |
|---|---:|---:|---:|---:|
| `feed_pairs` | 118 025 | 119 483 | 12.44 | — |
| `inst_feed_engine_worst` | 830 151 | 888 099 | **92.51** | 873 940 (91.04 %) |
| `inst_feed_engine_idle` | 811 338 | 824 009 | 85.91 | — |
| `instrument_worst`, same image | 940 530 | 986 459 | 102.76 | — |

The linear fit runs about **1.6 % optimistic** between its points, so the
reserve that actually survives is **7.5 %**, not the 9 % the derivation aimed
at. 92.51 is the number to quote. Task 11's gate -- `inst_feed_engine_worst`
must not exceed `instrument_worst` inside one image -- passes with 10 points to
spare.

### What P also decides

The bank voices `kPairs / kPairsPerTone` chord tones, capped at
`ChordBuilder::kMaxNotes` = 4. So P is not only a CPU number:

| P | tones voiced at COLOR max |
|---:|---|
| 2 | 1 of 4 |
| 4 | 2 of 4 |
| **6** | **3 of 4** |
| 8 | 4 of 4 |

P=6 sounds a complete triad and drops only the fourth note at the very top of
the COLOR knob. P=4 would have sounded two of four -- half the chord, silently.
That is why the choice went to 6 rather than to the cheapest option that fits.

---

# Bench evidence 2026-08-19 — `3def5d5`

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

- **Run 1 — PASS:** `wave_2x4` average 317240 <= `synth_2x4` average 340444; maximum 319717 <= 345505; maximum 319717 < 960000.
- **Run 2 — PASS:** `wave_2x4` average 317122 <= `synth_2x4` average 340448; maximum 319539 <= 345798; maximum 319539 < 960000.

## Verdict

**DTCM BBD-engine budget — go/no-go.** The decision workload is `instrument_worst_bbd_dtcm`: the full instrument with both decks on the BBD part engine, shortest division, clock ceiling, maximum COLOR/feedback/mix, STEP freeze engaged, live inputs, and retained DTCM instrument state. `instrument_worst_bbd` is its checksum-equal AXI comparison. `fx_flux_sdram` separately prices stereo tape FLUX; the five retained `sweep_flux_rate_*` rows carry its delay-time cost curve. Run maxima (offline / real callback): Run 1 97.16 % / 96.76 %; Run 2 97.02 % / 96.81 %. Across all 2 repeats, the worst maxima are **97.16 % offline** and **96.81 % in the real callback**. **Conclusion: the DTCM BBD-engine gate fits.** Every offline and real-callback maximum is below 100 % of the block budget.

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
| system | `mod_plane_2x_center` | 78842 | 81782 | 8.21 | 8.51 | `5af19015` |
| system | `synth_1_voice` | 53897 | 55541 | 5.61 | 5.78 | `1816acc1` |
| system | `synth_2_voices` | 94066 | 95990 | 9.79 | 9.99 | `4dc805b7` |
| system | `synth_4_voices` | 169811 | 172406 | 17.68 | 17.95 | `2292dae2` |
| system | `synth_2x4` | 340444 | 345505 | 35.46 | 35.99 | `0d15b5eb` |
| system | `wave_2x4` | 317240 | 319717 | 33.04 | 33.30 | `6f28f4ea` |
| system | `fx_none` | 24512 | 25467 | 2.55 | 2.65 | `b538ce01` |
| system | `fx_grit` | 51076 | 52373 | 5.32 | 5.45 | `74f9b9f5` |
| system | `fx_flux_sdram` | 99200 | 101966 | 10.33 | 10.62 | `9ca91007` |
| system | `fx_comp` | 31493 | 32534 | 3.28 | 3.38 | `47a4392b` |
| system | `oliverb_solo_sram` | 99756 | 101412 | 10.39 | 10.56 | `82d044c4` |
| system | `instrument_init` | 677152 | 705673 | 70.53 | 73.50 | `3bbafe55` |
| system | `instrument_worst` | 940530 | 985909 | 97.97 | 102.69 | `851c7d43` |
| system | `inst_worst_deck_bus` | 737111 | 784983 | 76.78 | 81.76 | `6f9b841c` |
| system | `instrument_worst_bbd` | 897949 | 943197 | 93.53 | 98.24 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 888414 | 932793 | 92.54 | 97.16 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 897917 | 942553 | 93.53 | 98.18 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 830151 | 888099 | 86.47 | 92.51 | `d66cbb8c` |
| system | `inst_feed_engine_idle` | 811338 | 824009 | 84.51 | 85.83 | `8e4f6664` |
| feed | `feed_pairs` | 118025 | 119483 | 12.29 | 12.44 | `5ab66f1c` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.39 | 10.62 |
| `instrument_worst_bbd_dtcm` | 91.68 | 96.76 |
| `instrument_worst_bbd` | 92.56 | 97.89 |
| `instrument_worst` | 97.33 | 102.41 |

## Run 2

QSPI payload SHA-256 `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`; device fingerprint `26696514052c2c6aaf668d4a6c74569bd9a563f769dae106012dd024791f2849`.

### Offline table

| family | workload | avg cyc | max cyc | avg % | max % | checksum |
|---|---|---:|---:|---:|---:|---|
| system | `empty_callback` | 2 | 12 | 0.00 | 0.00 | `ea306fb5` |
| system | `mod_plane_2x_center` | 78843 | 82114 | 8.21 | 8.55 | `5af19015` |
| system | `synth_1_voice` | 53896 | 55893 | 5.61 | 5.82 | `1816acc1` |
| system | `synth_2_voices` | 94064 | 96118 | 9.79 | 10.01 | `4dc805b7` |
| system | `synth_4_voices` | 169808 | 172525 | 17.68 | 17.97 | `2292dae2` |
| system | `synth_2x4` | 340448 | 345798 | 35.46 | 36.02 | `0d15b5eb` |
| system | `wave_2x4` | 317122 | 319539 | 33.03 | 33.28 | `6f28f4ea` |
| system | `fx_none` | 24511 | 25467 | 2.55 | 2.65 | `b538ce01` |
| system | `fx_grit` | 51074 | 52352 | 5.32 | 5.45 | `74f9b9f5` |
| system | `fx_flux_sdram` | 99198 | 102052 | 10.33 | 10.63 | `9ca91007` |
| system | `fx_comp` | 31492 | 32594 | 3.28 | 3.39 | `47a4392b` |
| system | `oliverb_solo_sram` | 99759 | 101402 | 10.39 | 10.56 | `82d044c4` |
| system | `instrument_init` | 677140 | 705471 | 70.53 | 73.48 | `3bbafe55` |
| system | `instrument_worst` | 940502 | 986459 | 97.96 | 102.75 | `851c7d43` |
| system | `inst_worst_deck_bus` | 737059 | 782325 | 76.77 | 81.49 | `6f9b841c` |
| system | `instrument_worst_bbd` | 897957 | 942091 | 93.53 | 98.13 | `07fb14fc` |
| system | `instrument_worst_bbd_dtcm` | 888354 | 931417 | 92.53 | 97.02 | `07fb14fc` |
| system | `inst_bbd_engine_worst` | 897950 | 940455 | 93.53 | 97.96 | `07fb14fc` |
| system | `inst_feed_engine_worst` | 830170 | 886537 | 86.47 | 92.34 | `d66cbb8c` |
| system | `inst_feed_engine_idle` | 811332 | 824821 | 84.51 | 85.91 | `8e4f6664` |
| feed | `feed_pairs` | 118025 | 119742 | 12.29 | 12.47 | `5ab66f1c` |

### Anchor mode (real audio callback, CpuLoadMeter)

| workload | avg % | max % |
|---|---:|---:|
| `oliverb_solo_sram` | 10.39 | 10.61 |
| `instrument_worst_bbd_dtcm` | 91.68 | 96.81 |
| `instrument_worst_bbd` | 92.55 | 97.77 |
| `instrument_worst` | 97.38 | 102.07 |
