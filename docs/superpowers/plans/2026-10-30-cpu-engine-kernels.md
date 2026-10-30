# CPU Round 2 on Rev A: Engine Kernels — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the SYNTH, WAVE and SAMPLER kernels cheaper on Rev A, so the 20 worst listener workloads end with worst `frame_max` ≤ 880 000. Then keep going down the ranked list, to leave room for a third, one-grain-at-a-time element.

**Architecture:** Phase A adds measurement-only probes. It extends the existing `ProbeEng` read-out to SYNTH, adds a SAMPLER split, and adds ablation toggle bits for the stages inside a voice and a grain. It then measures them on one measurement image of today's `main`, and a committed tool turns the serial logs into section means. Phase B runs one task per candidate. A class-1 candidate becomes the only path once a bit-identity test and the board's section means agree. A class-2 candidate becomes a firmware-only `SHELL_LEAN_*` switch and goes to Bastian's blind listening batch. The round ends with the 20-workload batch, the reserve numbers, and `bastian` for 10 minutes on the play image.

**Tech Stack:** C++17 (`engine/`, `shell/` firmware via ARM GCC + make, desktop via clang + Ninja), doctest, Python 3 (plain-script tests under ctest; pytest absent), `tools/remote_run.py`, dfu-util, ffmpeg.

**Spec:** `docs/superpowers/specs/2026-10-30-cpu-engine-kernels-design.md`. Read it first; § numbers below are its sections. The candidate ids (V1-V7, G1-G4) are from its §6.

## Global Constraints

- Desktop: `source env.sh`, `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` (Release is mandatory), `cmake --build build`, then `ctest --test-dir build --output-on-failure -E "^reva_"`. ctest does not build, so always build first.
- Firmware: never `source env.sh` in that shell. Use `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"`. Per image, `make -C shell clean`, then `make -C shell -j8 images <switches>`. `cmp` the new pair against the previous one before flashing.
- Images. Measurement: `SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1 SHELL_REMOTE=1 SHELL_REMOTE_MEASURE=1`. Listener: the same without `SHELL_REMOTE_MEASURE`. Both use the shipping class-2 defaults (`SHELL_REVERB_HALF=1`, `SHELL_FAST_MATH=1`, unset = default). Only the last image is a plain play image of `main`: `SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1`.
- Flash always as a pair, QSPI first, from a committed clean tree (`tools/remote_run.py flash … --expect-build STAMP`). The persisted patch must read `SHELL_PERSIST src=flash` after every boot. Ask Bastian before each use of the Apollo Twin.
- Budget: 96 samples = 960 000 DWT cycles. Clicks start at a window of ~945 000. The goal is the batch's worst `frame_max` ≤ 880 000 (§3), and it is a floor, not a stop.
- Toggle bits 10-29 are free, claimed in order, never reused. Bits 30-31 are the shell's. The word is read once per block in `Instrument::process` and handed down from there (`engine/util/probe_toggles.h`, `shell/test_remote_wiring.py`).
- `SynthEngineT` is shared with BODY. BODY's output must stay bit-identical (§3 anti-goals).
- Board verdicts are section window means: interleaved base/candidate, ≥ 3 rounds × 30 s per workload, on one measurement image (§5.2). One 60 s `frame_max` is no verdict (`docs/gotchas.md`).
- No runtime claim enters a doc before a board log or probe printed it. Never `cd`; no shell writes into the repo (memory `subagent-shell-auto-approval`).
- Everything written into the repo is English. Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`. Every new test is shown RED once.
- Branch `feat/cpu-engine-kernels`, cut from `main` after the spec commit. Never write into the repo while another agent implements there.

**The workload set (§5.1)**, ids in `shell/generated_workloads.h`:
- the 9 over 880 000: 90, 127, 99, 117, 226, 313, 314, 10, 92;
- `bastian`: 145;
- coverage: 108 (SYNTH_SYNTH_top1), 135 (WAVE_WAVE_top1), 81 and 82 (SAMPLER_SAMPLER_top1/2).

Voice ablations run on 117, 127, 313 and 108; grain ablations on 90, 99, 81 and 82.

## Review Focus

1. **A class-2 path leaking into VCV or the desktop render.** With no `SPKY_LEAN_*` define, the old code must compile. The render-hash tests (`tests/check_render_hash.cmake`) stay at their current hashes, and every class-2 task adds a desktop target that builds its define and passes its own property test.
2. **BODY changed by an edit to `SynthEngineT`.** Every task touching `synth_engine.h/.cpp` adds or extends a BODY bit-identity check (BODY rendered before/after against a test-local reference, or its render hash unchanged).
3. **G1's fold fast path at the edges.** `frame` one ulp below `fsz`, exactly `fsz`, `-0.f`, tiny negatives (reverse grains), a `set_rec_size` shrink mid-cloud. Pinned in G1's test against the old fold.
4. **V7 skipping a crossfade on a voice that is still sounding.** A steal from a loud voice must keep the crossfade. Pinned in V7's test: no sample-to-sample jump above the old path's largest at the same strike.
5. **ITCM placement after inlining or renaming.** `shell/itcm_placement.py` refuses a stale symbol and an overfull `ITCM_HOT`. Every candidate that inlines, removes or adds a hot function updates `shell/itcm_hot.ld` in the same commit, and the image build must pass the guard.

---

### Task 1: Section-means tool (host, pure)

**Files:**
- Create: `tools/section_means.py`, `tools/test_section_means.py`
- Modify: `CMakeLists.txt` (add `section_means_guard` next to `remote_lib_guard`)

**Interfaces:**
- Produces: `parse_windows(lines: Iterable[str]) -> list[dict[str, int]]`. One dict per `CBP_WIN*` window. Keys are `"<TAG>.<field>"`, e.g. `"WIN2.ae"`. For lines with a `d=` field the key is `"<TAG>.d<d>.<field>"`, e.g. `"WIN10.d1.m0"`. The fields `w` and `d` themselves are dropped, and the lines of one window are merged by `w`.
- Produces: `run_means(lines, skip: int = 1) -> dict[str, float]`, the mean per key over the run's windows minus the first `skip` (settle).
- Produces: `compare(base: list[dict], cand: list[dict]) -> dict[str, tuple[float, float, float, float]]`. Per key: (mean of base runs, mean of cand runs, cand − base, spread = max − min of the base runs).
- CLI: `section_means.py BASE_DIR... -- CAND_DIR... [--keys PREFIX...]`. It reads `DIR/serial.txt` and prints one row per key.

- [ ] **Step 1: Write the failing tests** in `tools/test_section_means.py`, on hand-written serial lines with the host-time prefix `"12.345 "` that `remote_run.py` writes:
  - `test_parse_merges_lines_of_one_window`: `CBP_WIN w=3 cb=900000 …` plus `CBP_WIN2 w=3 ae=150000 …` give one dict with `WIN.cb == 900000` and `WIN2.ae == 150000`.
  - `test_deck_lines_are_keyed_by_deck`: two `CBP_WIN10 w=3 d=0 …` / `d=1` lines give `WIN10.d0.m0` and `WIN10.d1.m0`.
  - `test_run_means_skips_settle`: three windows with `cb` 1, 10, 20 and `skip=1` give mean 15.
  - `test_compare_reports_spread`: base runs with `ae` 100 and 110, cand 90 give `(105, 90, -15, 10)`.
  - `test_ignores_non_window_lines`: `SHELL_PLAY_LAW …` and garbage lines change nothing.
- [ ] **Step 2:** Run `python tools/test_section_means.py`. Expected: FAIL (module missing).
- [ ] **Step 3:** Implement `tools/section_means.py`, stdlib only, using the same `k=v` parsing style as `tools/run_stats.py`.
- [ ] **Step 4:** Run the script test (PASS), then add `section_means_guard` to `CMakeLists.txt` in the shape of `remote_lib_guard`. Build and run `ctest -R section_means_guard`. Expected: PASS.
- [ ] **Step 5: Commit** `tools(cpu-round-2): section means from CBP_WIN logs`.

---

### Task 2: Phase A probes in the measurement image

**Files:**
- Modify: `engine/instrument.h` (`probe_synth(p)`, `probe_sampler(p)` beside `probe_wave(p)`, under `SPKY_PROBE_MARKS`)
- Modify: `shell/cb_profile.cpp` (engine split from SYNTH or WAVE; new SAMPLER split `CBP_WIN11` / `CBP_REC7`)
- Modify: `engine/sampler/sampler_engine.h/.cpp` (`ProbeEng` stamps, `SPKY_PROBE_MARKS` only)
- Modify: `engine/synth/voice.h/.cpp`, `engine/synth/synth_engine.h/.cpp`, `engine/sampler/grain.h`, `engine/sampler/sample_buffer.cpp`, `engine/instrument.cpp`, `engine/util/probe_toggles.h` (ablation bits, `SPKY_PROBE_TOGGLES` only)
- Modify: `shell/read_cbp.py`, `shell/test_read_cbp.py` (the new lines)
- Test: `tests/test_cpu_round2_probes.cpp` (new), added to the test target like `test_cpu_hunt_equiv.cpp`

**Interfaces:**
- Produces, `SamplerEngine` (only with `SPKY_PROBE_MARKS`): `struct ProbeEng { uint32_t ctrl, sched, grains, post, gsamp; }` and `const ProbeEng& probe_eng() const`. These are running totals like `SynthEngineT::ProbeEng`. `gsamp` adds the number of active grains at every sample. The stamps are `::spky::probe_clock` reads, as in `SynthEngineT::process`: one pair around the control tick, one around scheduling/spawn, one around the whole grain loop (not per grain), one around norm + SVF + level.
- Produces, `cb_profile`: the existing `WENG_*` / `vcyc` read-out now takes the per-block delta of `probe_wave(p)` **plus** `probe_synth(p)`. Only the active one moves, and the lines and their names stay the same. New lines, one per deck: `CBP_WIN11 w=%d d=%d sc=%d ss=%d sg=%d sp=%d sn=%d`. `sc/ss/sg/sp` are the window means per block of ctrl/sched/grains/post; `sn` is the window mean of `gsamp` per block. `CBP_REC7 blk d sc ss sg sp sn` is the same for a hot block.
- Produces, ablation toggles. Each engine gets `void set_probe_word(uint32_t w)` (`SynthEngineT<V>`, `SamplerEngine`; only with `SPKY_PROBE_TOGGLES`). `Instrument::process` calls it on every part's `synth()`, `wave()` and `sampler()` once per block, right after `_probe_tog = probe::toggles()`. Bits in `probe_toggles.h`, all marked "USED, reverted after Phase A":
  - 10 `ABL_OSC_B`: `VoiceT::process` skips `_osc_b` (uses osc A twice)
  - 11 `ABL_SUB`: skips the sub oscillator
  - 12 `ABL_SVF`: skips `_filt` (the voice signal passes unfiltered)
  - 13 `ABL_ENV_PAN`: uses a constant envelope value of 0.5 and fixed gains; `_env.process()` still runs for state, so the voice ends as before. This bit prices only pan and the multiplies, so it is read with care.
  - 14 `ABL_GRAIN_WIN`: `Grain::process` uses `w = 1` while ramping, with the counters advanced as before
  - 15 `ABL_GRAIN_FOLD`: `read_linear` skips the fold when `0 ≤ frame < fsz`, and clamps otherwise
  - 16 `ABL_GRAIN_SRAM`: `read_linear` reads from a 4 096-frame `static` array in DTCM (index masked) instead of the buffer
  - The voice reads its bits from a member copy that `SynthEngineT::_update_control` refreshes from the engine's word, so nothing global is touched per sample. The grain gets them as an argument from `SamplerEngine::process`, which reads its member.

- [ ] **Step 1: Write the failing test** `tests/test_cpu_round2_probes.cpp`, compiled into its own test executable with `SPKY_PROBE_MARKS=1 SPKY_PROBE_TOGGLES=1` (like the reverb variant targets in `CMakeLists.txt` near line 284). The default desktop build defines neither, so a test there could not fail. The executable gets `probe_clock` from a test-local counter. `"sampler probe counts grain-samples"`: a `SamplerEngine` with a filled buffer in FLOW for 9 600 samples has `probe_eng().gsamp` equal to the sum of `active_grains()` taken after each `process()` call. And `"ablation bits change only their stage"`: with `set_probe_word(1u << 12)`, a WAVE voice's output differs from bit 0, and with word 0 it is bit-identical to an engine that never got a word.
- [ ] **Step 2:** Build and run. Expected: FAIL (no `probe_eng()` on `SamplerEngine`, no `set_probe_word`).
- [ ] **Step 3:** Implement the stamps, the read-out, the bits and the forwarding as in Interfaces. Extend `shell/read_cbp.py` and its test with `CBP_WIN11` / `CBP_REC7`.
- [ ] **Step 4:** Desktop suite green (render hashes unchanged). Build the measurement image; `itcm_placement.py` passes. Note `ITCM_HOT` and `SRAM_EXEC` free from the map for §5.1's placement reading.
- [ ] **Step 5:** `python shell/test_remote_wiring.py` passes (`toggles()` still read in `instrument.cpp` only).
- [ ] **Step 6: Commit** `probe(cpu-round-2): SYNTH and SAMPLER splits, voice and grain ablation bits (measurement image only)`.

---

### Task 3: Phase A on the board

Needs Bastian once: the board runs the play image (no remote).

- [ ] **Step 1:** Ask Bastian for BOOT + RESET (board into DFU). Flash the Task 2 measurement image (`remote_run.py flash … --expect-build`). Confirm `SHELL_PERSIST src=flash`.
- [ ] **Step 2: Base.** All 14 workloads, 30 s each, 3 rounds, in rotation, toggle word 0, into `<scratchpad>/runs/A/base/<round>/w<id>/` (`remote_run.py run --load ID --seconds 30 --out …`). SAMPLER workloads: `--seconds 34` (5 s settle).
- [ ] **Step 3: Ablations.** Interleave with base on the same workloads: bits 10-13 on 117, 127, 313 and 108; bits 14-16 on 90, 99, 81 and 82; 3 rounds each (`--tog 12=1` etc.).
- [ ] **Step 4: Tabulate** with `tools/section_means.py`:
  - per engine: engine section (`WIN2.ae/be`), its split (`WIN9`, the voice bins `WIN10`, the sampler `WIN11`), each ablation's delta and the base spread;
  - active voices per hot block (`CBP_REC6` call counts per bin) and grains per hot block (`CBP_REC7 sn`);
  - the grain price: least-squares slope of `sg` against `sn` over all windows of the SAMPLER runs, with the intercept and the residual spread.
- [ ] **Step 5: Record** §7 "Round 2, Phase A" in `docs/hardware/cpu-hunt-measured.md`: the table, the placement reading, the grain price, and the ranking of V1-V7 / G1-G4 by the measured share of the stage each one touches. Say where a candidate's stage is below the base spread: it drops (§3 "Stop"). Price the class-3 list (§6) from the same table: one voice from the bins, WAVE's second oscillator from bit 10, one grain from the slope. Commit `docs(hardware): cpu round 2 phase A — the kernels split`.
- [ ] **Step 6: Checkpoint.** Send Bastian the ranking in German, three lines. Proceed unless he objects (§5.1).
- [ ] **Step 7: Revert** the ablation bits (not the splits: those stay for Phase B). `probe_toggles.h` keeps bits 10-16 as "USED, reverted". Desktop suite green. Commit `probe(cpu-round-2): ablation bits reverted after phase A`.

---

### The candidate protocol (Tasks 4-14 each follow it)

Run the candidate tasks **in the Phase A ranking order**, not in task-number order, and skip any that §7 dropped. Each claims the next free bit from 17 on.

**Class 1** (bit-identical):
1. Test first, in `tests/test_cpu_round2_equiv.cpp` (new on the first class-1 task). It compares the new path bit for bit with a test-local copy of the old arithmetic, as `tests/test_cpu_hunt_equiv.cpp` does, over a stated input sweep. Show the RED once by sabotaging the new path. Never undo the sabotage with `git checkout <file>` (memory `fireflow-scripted-edits-crlf`).
2. Implement both paths in the measurement image, `if (probe::on(<member word>, kBit…))` new, else old. Outside `SPKY_PROBE_TOGGLES` the new path only is compiled; `probe::on` is constant false there, so write it as `#if defined(SPKY_PROBE_TOGGLES)` dispatch `#else` new `#endif`.
3. Desktop suite green (render hashes unchanged: class 1 changes no bit on the desktop).
4. Measurement image; base vs bit on the candidate's workloads, interleaved, ≥ 3 rounds. Compare the candidate's section with `section_means.py`.
5. Saves more than the base spread: the toggle dispatch goes, the new path is the only path, the bit retires in `probe_toggles.h`. Otherwise revert. Record the row in §7 of the record either way.
6. Commit `perf(<area>): <candidate id> <what> — <section> <delta> on <workloads>` (or `revert(...)`).

**Class 2** (output changes, firmware only):
1. A switch `SHELL_LEAN_<NAME>` (default **0** until Bastian accepts it), modelled on `SHELL_FAST_MATH`. The `shell/Makefile` variable is checked to 0/1, a stamp file deletes `ENGINE_OBJECTS` when it changes (`shell/write_shell_engine_lean.py OUTPUT NAME=V… -- STALE…`, written on the first class-2 task, one stamp for all lean switches), and it sets `C_DEFS += -DSPKY_LEAN_<NAME>=1`. `shell/check_images_build.py` builds one image with every lean switch at its non-default value.
2. Engine code: `#if defined(SPKY_PROBE_TOGGLES)` dispatch on the bit, `#elif SPKY_LEAN_<NAME>` new, `#else` old `#endif`. The old branch is today's source, unedited.
3. Desktop: the default build compiles the old path, so the render hashes are unchanged. A separate test target compiled with `-DSPKY_LEAN_<NAME>=1` runs the candidate's property test (named per task: finite, bounded, and its stated closeness to the old path).
4. Board, as class 1 step 4. If it saves more than the spread: render a desktop A/B pair (old vs new) of `bastian` and one workload where the engine is prominent into the scratchpad, as a first listen for Claude only. Queue the candidate for Task 15. Otherwise revert.
5. Commit `perf(<area>): <candidate id> <what> behind SHELL_LEAN_<NAME> (class 2, queued for listening)`.

Every candidate touching a function named in `shell/itcm_hot.ld` updates that file in the same commit (Review Focus 5).

---

### Task 4: V1, MorphOsc per-sample invariants (class 1)

**Files:** `engine/synth/morph_osc.h`; test in `tests/test_cpu_round2_equiv.cpp`.
**Interfaces:** `MorphOsc` keeps its public API. New private members `_seg_idx` (0/1/2), `_seg_t` and `_dt`, computed in `set_morph` and wherever `_inc` changes (`set_freq`, `set_detune_cents`).
- [ ] Test `"V1: MorphOsc output bit-identical"`: the old `process()` as a test-local struct against the new one, at morph ∈ {0, 0.2, 1/3, 0.5, 2/3, 0.9, 1}, freq ∈ {20, 220, 4 000, 18 000} Hz, detune changes every 96 samples, 48 000 samples each. Every bit equal.
- [ ] Then the protocol. Workloads: 108, 117, 127, 10.

### Task 5: V2, skip idle voices before the call (class 1)

**Files:** `engine/synth/synth_engine.cpp` (`process`'s voice loop), test.
**Interfaces:** in `SynthEngineT<V>::process`, `if (!v.active()) continue;` before `v.process(l, r)` for `V` ∈ {`VoiceT<MorphOsc>`, `VoiceT<WtOsc>`} only (`if constexpr`). The `SPKY_PROBE_MARKS` bin 4 count still increments for a skipped voice.
- [ ] Test `"V2: engine output bit-identical with idle voices"`: SYNTH and WAVE engines in STEP with sparse triggers (voices idle most of the time), 96 000 samples, against a test-local engine loop that calls every voice. Plus `"V2: BODY untouched"`: BODY's render against its pre-change output, captured as a reference array in the test.
- [ ] Then the protocol. Board verdict only if Phase A showed idle voices in hot blocks (§4: skipping idle work counts only then). Otherwise record "mean only" and keep it, since it is bit-identical and free.

### Task 6: V3, sub oscillator without the redundant wrap (class 1)

**Files:** `engine/util/fast_sin.h` (add `fast_sin_wrapped(float p)` for `p` ∈ [0, 1)), `engine/synth/voice.cpp`, test.
- [ ] Test `"V3: fast_sin_wrapped equals fast_sin on [0,1)"`: every float in [0, 1) with a stride of 1 ulp below 2⁻¹⁰ and 2¹⁶ evenly spaced values above. Bits equal. Plus `"V3: voice output bit-identical"` on the sub-heavy setting (`sub_level` 1).
- [ ] Then the protocol.

### Task 7: V4, SYNTH control code into ITCM (class 1, placement only)

**Files:** `shell/itcm_hot.ld` (tier 2: `VoiceT<MorphOsc>::_apply_freq`, `SynthEngineT<VoiceT<MorphOsc>>::_update_control`, mangled as the map prints them).
- [ ] No engine change, so there is no equivalence test. The guard: the image builds and `itcm_placement.py` passes. If `ITCM_HOT` lacks the room, record that and stop.
- [ ] Board: no toggle is possible, so compare a base image and a V4 image, A-B-A-B by reflash, on 108, 117 and 127, ctrl split `WIN9.*wc`, 3 rounds each. Keep it only if `wc` drops by more than the spread.

### Task 8: V5, cheaper exp2 for the detune ratio (class 2)

**Files:** `engine/util/fast_exp2.h` (new: `float fast_exp2(float x)` for x ∈ [-0.1, 0.1], the range of ±105 ct + drift), `engine/synth/morph_osc.h`, `engine/synth/wt_osc.h`, test.
- [ ] Property test `"V5: fast_exp2 within 0.01 ct of std::pow(2, x)"` over the range, i.e. a ratio error ≤ 5.8e-6 relative. Plus the engines' output finite and pitch-stable (zero-crossing count over 1 s equal to the old path's).
- [ ] Then the protocol, class 2. Workloads: 108, 117, 127, 313.

### Task 9: V6, single-pass SvfLp (class 2)

**Files:** `engine/util/svf_lp.h` (a `ProcessSingle` with coefficients for one pass, retuned in `SetFreq`/`SetRes` so the −3 dB cutoff matches the two-pass filter), `engine/synth/voice.cpp`.
- [ ] Property test `"V6: single-pass cutoff within 3 % of two-pass"` at cutoffs 100 Hz-14 kHz × res {0, 0.5, 0.95}, measured from the magnitude response of 8 192-sample sine sweeps. Stable (bounded output) at res 0.95, cutoff 14 kHz.
- [ ] Then the protocol, class 2. The sampler's two `SvfLp` are not touched.

### Task 10: V7, WAVE crossfade only where it can be heard (class 2)

**Files:** `engine/synth/wt_osc.h` (`set_freq` gets a `bool quiet` argument, default false; when quiet, it snaps to the new mip: `_set_mip_source(next_mip)`, no ramp), `engine/synth/voice.cpp` (`trigger()` passes `_env.value() < kXfadeQuiet`, with `kXfadeQuiet = 1e-3f`, −60 dB).
- [ ] Tests: `"V7: a strike from silence skips the crossfade"` (`mip_crossfading()` false after the strike) and `"V7: a steal from a sounding voice keeps it"` (env 0.5 at the strike: crossfade runs, and the largest sample-to-sample step is not above the old path's at the same strike).
- [ ] Then the protocol, class 2. Workloads: 127, 313, 314, 135. The verdict also counts the crossfade bin calls (`WIN10.*n1`, `n3`) before and after.

### Task 11: G1, fold fast path in read_linear (class 1)

**Files:** `engine/sampler/sample_buffer.cpp` (`read_linear`: if `frame >= 0.f && frame < fsz` skip the fold), test.
- [ ] Test `"G1: read_linear bit-identical"`: against a test-local copy of today's `read_linear` on a filled 24 000-frame buffer. `frame` sweeps [−2·fsz, 3·fsz] in 0.37-frame steps, plus the edges `nextafter(fsz, 0)`, `fsz`, `-0.f`, `-1e-7f`, `nextafter(0, -1)`, plus a `set_rec_size` shrink to 1 000 with stale positions. Every output bit equal. If the edge `nextafter(fsz, 0)` differs (`frame / fsz` rounding to 1), tighten the fast path's bound until the test is green, and say so in the commit.
- [ ] Then the protocol. Workloads: 90, 99, 81, 82.

### Task 12: G2, Hann window without per-sample divisions (class 2)

**Files:** `engine/sampler/grain.h` (latch `_inv_atk = 1.f / _atk`, `_inv_dec` at `spawn`, `_inv_rel` at `release` / `trim_total`; `_window()` and `_level()` multiply).
- [ ] Property test `"G2: window within 1e-6 of the dividing window"` for every `_i` of grains with atk/dec ∈ {1, 2, 3, 96, 4 801, 96 000}, plus releases at random points. And the windows still reach exactly 0 and 1 at their ends.
- [ ] Then the protocol, class 2.

### Task 13: G3, read_linear inlined into the grain loop (class 1)

**Files:** `engine/sampler/sample_buffer.h` (move `read_linear`'s body into the header, `inline`), `shell/itcm_hot.ld` (drop its entry, since it is no longer a symbol). If G1 has landed, G3 inlines G1's version.
- [ ] Test: the G1 test passes unchanged against the inlined function. Plus the itcm guard.
- [ ] Then the protocol. If inlining grows `SamplerEngine::process` past the room `ITCM_HOT` has, record that and revert.

### Task 14: G4, int16 sampler buffer (class 2, only if Phase A's SRAM ablation priced SDRAM above the base spread)

**Files:** `engine/sampler/sample_buffer.h/.cpp` (`Frame16 { int16_t l, r; }` storage behind `SPKY_LEAN_SAMPLER_I16`, scaled 1/32 768 on read, saturating on write; the feedback/overdub path writes through the same converter), `shell/sdram_mem.cpp` (the arena's type), test.
- [ ] Property tests: `"G4: round trip within 1 LSB"`, `"G4: overdub at feedback 1 stays bounded"` (by-ear rule `kFbSatKnee` unchanged), `"G4: load and save keep the length"`.
- [ ] Then the protocol, class 2. Skip the whole task, and record why, when Phase A's bit-16 delta is inside the spread.

---

### Task 15: The listening batch with Bastian

- [ ] **Step 1:** For each queued class-2 candidate, build two listener images: all accepted switches, plus this one at 0 or 1. Record `bastian` and one prominent workload for 30 s each through the Twin (ask first). Align and level-match the recordings, assign A/B at random, and keep the key in the scratchpad (the method of `cpu-hunt-measured.md` §6).
- [ ] **Step 2:** Bastian listens. Record each decision verbatim in §8 of the record ("Bastian's decisions, round 2").
- [ ] **Step 3:** For each accepted candidate, `SHELL_LEAN_<NAME> ?= 1` in `shell/Makefile`, and a line in `shell/README.md` "Turning a CPU saving back". Each rejected one is reverted (code, switch, test target). Commit.

### Task 16: Landing and the end of the round

- [ ] **Step 1:** Desktop suite green in Release. `check_images_build.py` passes. `engine/` changed, so run `host/vcv/build-local.sh install` and tell Bastian to restart Rack.
- [ ] **Step 2:** The 20-workload batch of the spec §1 (`captures/cpu-hunt-2026-10-30-top20-class2.csv`, ids from there) on the final listener image, 60 s each. Save it as `captures/cpu-round2-<date>-top20.csv`. Any workload over 880 000 gets one repeat before a verdict (§9).
- [ ] **Step 3: Reserve numbers** (§3), side by side in the record: 96 × the grain price, the batch's worst `frame_max`, 945 000. No verdict. Bastian judges.
- [ ] **Step 4:** Review (superpowers:requesting-code-review). Bastian's go-ahead, then merge to `main`.
- [ ] **Step 5:** Flash the plain play image of `main` remotely (`boot`, `flash`, confirm the boot print). Run `bastian` for 10 minutes through the Twin with `tools/click_detect.py` (ask first). Record `cb_max` and the hits.
- [ ] **Step 6:** Docs. Fill §7-§9 of `cpu-hunt-measured.md`. Update `docs/engine-map.md` "Where a board block goes", with the per-stage numbers. Note any new trap in `docs/gotchas.md`, plus the lean switches in its "bench/ prices…" entry. In `docs/roadmap.md`, close or narrow §4 row 11, and add S-block as the next lever if the goal was missed. Commit `docs: cpu round 2 on Rev A — results`.
- [ ] **Step 7:** Tell Bastian the board runs plain `main`; the next remote flash needs BOOT + RESET.
