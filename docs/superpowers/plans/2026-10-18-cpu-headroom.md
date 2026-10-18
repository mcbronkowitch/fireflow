# CPU Headroom Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the clicks on the Daisy firmware by giving the audio callback headroom: the init patch's worst block ≤ 900 000 cycles and two stress patches ≤ 930 000, measured on the coupon.

**Architecture:** Two placement levers first, both sound-neutral: the WAVE wavetable bank copied from QSPI into D2 SRAM at boot (measured, spec §2.3), and the per-sample audio path executed from ITCM, copied there from QSPI at boot (spec §4.2). Two conditional levers after a measurement each: WAVE's transition cost (sound may change) and Plan B (the control law across two blocks). The callback profile from the brainstorm stays in the tree as the measuring tool, off by default.

**Tech Stack:** C++17 engine (`engine/`, clang desktop + ARM GCC firmware), libDaisy shell (`shell/`, make, `alt_sram.lds` + linker fragments), Python 3 guards (plain scripts, ctest), doctest.

**Spec:** `docs/superpowers/specs/2026-10-18-cpu-headroom-design.md` (approved 2026-10-18). Read it first; this plan does not repeat its reasons.

## Global Constraints

- `engine/` carries no hardware type: placement (D2, ITCM, QSPI) lives in `shell/` and the linker scripts only (CLAUDE.md, spec §5).
- Every cycle target is against the measured deadline of **~945 000** cycles in the `cb` window, never 960 000 (spec §2.1).
- Targets: init patch worst `cb_max` **≤ 900 000** over 10 min; stress patches W2, F2 **≤ 930 000** over 10 min; no click by the detector (spec §3).
- Judging rule for a lever (spec §4): two probe images from one tree, the lever's switch the only difference, one ≥ 5-min run each; **kept** at ≥ 10 k mean drop in the touched segments or ≥ 20 k on the worst block, with an untouched segment moving < 5 k; ITCM (no control segment) kept at ≥ 20 k mean on cb, second run of both images if < 40 k; at most two runs per image.
- Rev A image (`SHELL_PANEL_SCAN=1`, no `SHELL_COUPON_PROBE`) keeps **≥ 8 KB** of `SRAM_EXEC` free (P6b-1 §5).
- Flashing stays DFU-only and paired: `shell-qspi.bin` first, then `shell-sram.bin` (`shell/README.md`, "Flashen").
- **Clean build between switch positions** (`make -C shell clean`), and `cmp` the two images before flashing either: make has one-second mtime resolution here.
- Firmware builds: `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"`, `make -C shell -j8 images ...`; **never** `source env.sh` in that shell. Desktop: `source env.sh`, `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` (Release is mandatory), `cmake --build build`, then `ctest` — ctest does not build.
- Never prefix a shell command with `cd`; long compounds go into a scratchpad script.
- Everything written into the repo is English; commit trailer `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- `bench/` is not touched. `alt_sram.lds` is shared with it: changes there are additive only.
- No bit-exactness gates. A test that cannot go red gets fixed: prove each RED once.
- Every coupon session needs Bastian (RESET then BOOT for DFU, 12 V on). Batch the images of one session so he presses as rarely as possible.

## Review Focus

1. **An interrupt or a static constructor reaching ITCM before the copy:** the board must still boot and play; a dark board has no SWD to debug it. Pinned by Task 5's guard cases (b) and (c).
2. **A mismatched image pair after this round:** the ITCM code lives in `shell-qspi.bin`, so an old QSPI image under a new SRAM image jumps into stale ITCM code. Expected: the README says so in the flashing section, and Task 5's image check refuses a build whose QSPI image lacks the ITCM section.
3. **Engine switch while playing** (ENGINE knob turned on Rev A): the ITCM set and the D2 bank must not break `FeedEngine::_rebuild_allocation`, `SamplerEngine::_update_control` or WAVE's first note after a switch. Checked by ear in Task 9, step 4.
4. **The D2 copy racing the first WAVE read:** nothing may read the bank through the pointer while the copy runs. Pinned by Task 2's order (copy before `Instrument::init`) and its desktop test that `table()` follows `set_bank()`.
5. **A desktop/VCV build picking up a probe or placement switch:** `SPKY_PROBE_MARKS` off must leave the engine bit-identical in behaviour. Pinned by the full desktop ctest in Tasks 2 and 3 (render hashes) and a Rack check in Task 9.

---

### Task 1: The click detector as a committed tool

**Files:**
- Create: `tools/click_detect.py`
- Create: `tools/test_click_detect.py`
- Modify: `CMakeLists.txt` (next to the `tools/test_scan_budget.py` test, ~line 594)

**Interfaces:**
- Produces: `find_clicks(x: numpy.ndarray, sr: int, k: float = 12.0, win_s: float = 0.02) -> list[tuple[float, float]]` — (time in s, score) per click; CLI `python tools/click_detect.py FILE.wav [--channel N] [--k K] [--period S]` printing one line per click and a count line `clicks=N`. Reads 16-bit PCM WAV, also a file still being written (raw read past the `data` tag).
- Method (spec §2.6): second difference `|diff(x, 2)|`; 20 ms windows; per window its peak over the median of the 11 window medians centred on it (+1e-7); a window above `k` is a hit; hits within two windows merge, keeping the highest score.

- [ ] **Step 1: Write the failing test** `tools/test_click_detect.py` (plain script, exit code is the verdict, `check()` helper as in `shell/test_qspi_placement.py`):
  - `clean`: 10 s, 48 kHz, 220 Hz sine at 0.3 → `find_clicks(...) == []`.
  - `one dropped block`: the same sine with samples `[240000, 240096)` set to 0 → exactly one click, `abs(t - 5.0) < 0.01`.
  - `two blocks 6 s apart`: zeros at 2.0 s and 8.0 s → two clicks, times within 0.01 s.
  - `quiet passage is not a click`: sine whose amplitude ramps from 0.3 to 0.003 over 1 s → no click.
- [ ] **Step 2: Run it, expect failure**
  Run: `python tools/test_click_detect.py` → fails with `ModuleNotFoundError: click_detect`.
- [ ] **Step 3: Implement `find_clicks` and the CLI** in `tools/click_detect.py`.
- [ ] **Step 4: Run it, expect all `ok`**, exit code 0.
- [ ] **Step 5: Prove the RED once**: temporarily return `[]` from `find_clicks` → the two click cases FAIL; restore by editing the file back (never `git checkout` the file). Note it in the commit message.
- [ ] **Step 6: Wire into ctest** as `tools_click_detect_guard` (same form as the `test_scan_budget.py` entry); `source env.sh`, configure Release, `ctest --test-dir build -R click_detect --output-on-failure` → passed.
- [ ] **Step 7: Commit** `tools: click detector for board recordings (CPU headroom spec §2.6)`.

### Task 2: The wavetable bank in D2 SRAM (spec §4.1)

**Files:**
- Modify: `engine/synth/wt_bank.h` (replace the `SPKY_WT_BANK_RAM` probe block)
- Delete: `shell/wt_bank_ram.cpp`
- Create: `shell/wt_bank_d2.cpp`
- Modify: `shell/main.cpp` (the probe's `SPKY_WT_BANK_RAM` blocks become unconditional), `shell/Makefile` (`wt_bank_ram.cpp` → `wt_bank_d2.cpp`)
- Test: `tests/test_wt_bank.cpp`

**Interfaces:**
- Produces (engine, always compiled, no `#if`): in `namespace spky::wt`, `void set_bank(const int16_t* samples)` (must hold `kTotalSamples` values; `nullptr` restores `kBankSamples`) and `const int16_t* bank()`; `table(frame, mip)` reads `bank()`. All inline in `wt_bank.h` over an internal pointer that starts at `kBankSamples` (constant-initialised, so it is valid before any constructor runs); `wt_bank.cpp` stays generated and untouched.
- Produces (shell): `namespace shell { void wt_bank_to_d2(); }` in `shell/wt_bank_d2.cpp` — copies `kBankSamples` into a `.d2_bss` array of `kTotalSamples` `int16_t` and calls `wt::set_bank()` on it. Called in `main()` after `bench::board_init(hw)` and the ADC re-init, before `inst.init(...)`.

- [ ] **Step 1: Write the failing test** in `tests/test_wt_bank.cpp`: `TEST_CASE("wt_bank: table() follows set_bank()")` — copy `kBankSamples` into a static `int16_t copy[wt::kTotalSamples]`, set `copy[5 * wt::kSamplesPerFrame + wt::kMipOffset[3]] = 12345`, `wt::set_bank(copy)`; `CHECK(wt::table(5, 3) == copy + 5 * wt::kSamplesPerFrame + wt::kMipOffset[3])`, `CHECK(wt::table(5, 3)[0] == 12345)`; then `wt::set_bank(nullptr)`; `CHECK(wt::table(0, 0) == wt::kBankSamples)`.
- [ ] **Step 2: Build and run, expect failure**: `cmake --build build` fails to compile (`set_bank` undeclared).
- [ ] **Step 3: Implement `set_bank()` / `bank()` and route `table()` through `bank()`** in `wt_bank.h`; remove the probe's `SPKY_WT_BANK_RAM` block.
- [ ] **Step 4: Run** `ctest --test-dir build --output-on-failure` → all pass, render hashes unchanged.
- [ ] **Step 5: Prove the RED**: make `table()` read `kBankSamples` again → the new case fails; revert by editing.
- [ ] **Step 6: Shell side**: create `shell/wt_bank_d2.cpp`, delete `shell/wt_bank_ram.cpp`, make the call in `main.cpp` unconditional with a comment naming spec §2.3's figures, update the Makefile source list. Build the coupon play image (`SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1`) and the Rev A play image (`SHELL_PANEL_SCAN=1`): both link, the placement guard passes, `RAM_D2CACHE` reads 65 024 B, `nm` shows `wt_bank_to_d2` in SRAM (0x24…) and the copy array at 0x3000_8000 or above. Record the Rev A image's `SRAM_EXEC` free bytes in the commit message (floor 8 KB).
- [ ] **Step 7: Commit** `shell: wavetable bank copied to D2 SRAM at boot (CPU headroom spec §4.1)`.

### Task 3: Keep the probe; boot-patch switch for the stress patches (spec §4.6, §4.7)

**Files:**
- Modify: `shell/control_tick.cpp` (`control_boot`)
- Modify: `shell/cb_profile.cpp` (one boot line)
- Modify: `shell/README.md` (probe table row, "Known" CPU facts)
- Modify: `engine/util/probe_mark.h`, `shell/cb_profile.h` (comments only, if they still name the probe as temporary)

**Interfaces:**
- Consumes: `ffctl::ENGINE_A`, `ffctl::ENGINE_B` (`control/params.hpp`); ENGINE knob values: **2.0 = WAVE, 5.0 = FEED** (`control/control_law.h:599-606`). The init patch is A = 5.0 (FEED), B = 2.0 (WAVE).
- Produces: build define `SHELL_BOOT_PATCH` (via `C_USER_FLAGS`, clean build): absent or `0` = init patch; `1` = **W2** (`knobs[ENGINE_A] = 2.0f`); `2` = **F2** (`knobs[ENGINE_B] = 5.0f`). Applied in `control_boot()` after the defaults are copied and before `on_restore()`. Any other value is a `#error`.
- Produces: with `SPKY_PROBE_MARKS`, the report prints once per summary `CBP_BOOT patch=%d eng_a=%d eng_b=%d` with the decks' `engine_id()` as the instrument reports it, so a session log proves which patch ran.

- [ ] **Step 1: Implement the switch and the `CBP_BOOT` line.** Firmware-only code, no host test; the board log in Task 4 is the check (both engine ids printed).
- [ ] **Step 2: Build three probe images** (`C_USER_FLAGS="-DSPKY_PROBE_MARKS=1 -DSHELL_BOOT_PATCH=N"`, N = 0, 1, 2, clean build each, copy each pair to a scratch folder named by N); `cmp` the three SRAM images pairwise → all differ.
- [ ] **Step 3: README**: the probe table in `shell/README.md` gets the row "`C_USER_FLAGS=-DSPKY_PROBE_MARKS=1` (clean build) | where the audio block goes: per-deck cycles, worst blocks, lane events | `CBP_*` lines | `docs/hardware/cpu-headroom-measured.md`", and a sentence that the measured window's deadline is ~945 000 (spec §2.1). Also the `SHELL_BOOT_PATCH` values.
- [ ] **Step 4: Desktop**: `ctest --test-dir build --output-on-failure` → all pass (the probe is off there).
- [ ] **Step 5: Commit** `shell: keep the callback profile; SHELL_BOOT_PATCH for the stress patches`.

### Task 4: Coupon session 1 — the baseline after the bank move (Bastian)

**Files:**
- Create: `docs/hardware/cpu-headroom-measured.md` (Session 1; carries spec §2's figures by reference, not copied)

- [ ] **Step 1: Flash image N = 0**, log the serial port 330 s (`CBP_*`, `SHELL_PLAY_LAW`) and record the coupon's output (ffmpeg dshow, "Line (Universal Audio Twin USB)", 48 kHz, 320 s) in parallel.
- [ ] **Step 2: Same for N = 1 (W2) and N = 2 (F2).** Check each log's `CBP_BOOT` line names the expected engines.
- [ ] **Step 3: Write Session 1** in the new doc: per image cb mean/max, deck A / deck B / mix means, blocks above 930 k and 945 k, `tools/click_detect.py` count; the commit the images were built from. Compare N = 0 with spec §2.3's D2 column (same lever, now the shipping code).
- [ ] **Step 4: Commit** `docs(hardware): CPU headroom session 1 -- baseline with the bank in D2`.

### Task 5: The per-sample audio path in ITCM (spec §4.2)

**Files:**
- Create: `shell/itcm_hot.ld` (linker fragment, exact function names like `shell/qspi_cold.ld`)
- Create: `shell/itcm_load.cpp`
- Create: `shell/itcm_placement.py`, `shell/test_itcm_placement.py`
- Modify: `shell/Makefile` (switch, fragment, `QSPI_SECTIONS`, guard in the `.nm` rule), `shell/main.cpp` (the copy call), `CMakeLists.txt` (`shell_itcm_placement_guard` next to `shell_qspi_placement_guard`), `shell/README.md` (flashing: the ITCM code is in `shell-qspi.bin`)

**Interfaces:**
- Produces: make switch `SHELL_ITCM ?= 1` (0 or 1, refused otherwise, like `SHELL_PANEL_SCAN`). At 1: `LDFLAGS += -Wl,-T,itcm_hot.ld` (before the main script, as `qspi_cold.ld` is) and `C_DEFS += -DSHELL_ITCM=1`; `.itcm_hot` joins `QSPI_SECTIONS`.
- Produces: section `.itcm_hot` — VMA `0x00000100` (the first 256 B of ITCM stay empty, spec §2.5), LMA in `QSPI_COLD` behind `.qspi_cold` (`AT > QSPI_COLD`), symbols `__itcm_hot_start__`, `__itcm_hot_end__`, `__itcm_hot_load__`. `ASSERT`s: not empty; `end <= 0x10000`.
- Produces: `namespace shell { void itcm_load(); }` — copies `[__itcm_hot_load__, +size)` to `__itcm_hot_start__`, then `__DSB(); __ISB();`. Placed in SRAM. Called in `main()` directly after `bench::board_init(hw)` (QSPI is memory-mapped from then on), before the ADC re-init and before anything else. Without `SHELL_ITCM` the call compiles to nothing.
- Produces: `itcm_placement.py ELF_NM DIS ELF` — exit 0 or a refusal list; rules: (a) `.itcm_hot` exists, is non-empty, starts at ≥ 0x100, ends ≤ 0x10000; (b) no function in ITCM is reachable by direct calls from `Reset_Handler`, any `_GLOBAL__sub_I_*`, any symbol named `*_Handler` or `*IRQHandler`, or `bench::board_init`; (c) in `main`'s disassembly, the call to `shell::itcm_load()` comes before the first call to any function whose direct-call closure contains an ITCM function; (d) every name listed in `itcm_hot.ld` exists in the image (a stale name is a refusal here, unlike `qspi_cold.ld`: a silent miss would quietly undo the lever); (e) the QSPI image contains `.itcm_hot`'s bytes (its LMA range is inside the `shell-qspi.bin` span). Reuse `qspi_placement.py`'s parsing (`_code_symbols`, `_INSN`, `_TARGET`) by import.
- **What goes into `itcm_hot.ld`** (the builder makes the list, the rule is fixed): start from the direct-call closure of the per-sample roots — `spky::Instrument::process`, every `process` / `process_in` override of `IPartEngine`, `VoiceT<…>::process`, `BodyVoice::process`, `clouds::Oliverb::Process`, `spky::Flux::process`, the limiter's `process` — and keep only functions that run on **every sample** of a playing deck. Leave out what runs once per control tick or on an event (`_control_tick`, `_update_control`, `generate_phrase`, `derive_turnaround`, `regenerate_unit`, `ChordBuilder::build`, `Center::update`, `_rebuild_allocation`, `_spawn_one`, `ModLane::tick`, `_apply_freq`, libm). Order of filling, until the section would pass 0x10000: `Instrument::process`, deck B's WAVE path (`SynthEngineT<VoiceT<WtOsc>>::process`, `VoiceT<WtOsc>::process`, the oscillator and filter it calls), the mix (`Oliverb::Process`, limiter, `Flux::process`), deck A's FEED path, then the other engines. The brainstorm's measurement script for the closure is in the session scratchpad; a committed helper is the builder's call. The final list, with each function's size and the total, goes into the fragment's header comment.

- [ ] **Step 1: Write the failing guard test** `shell/test_itcm_placement.py` (plain script, fed nm/dis text fixtures like `test_qspi_placement.py`): (a1) a clean fixture passes; (a2) empty section → refused; (a3) section at 0x00000000 → refused; (a4) end at 0x10004 → refused; (b) `Reset_Handler` → `foo` (branch), `foo` at 0x00000200 → refused; (b2) `SysTick_Handler` reaching ITCM → refused; (c) `main` calling an ITCM-reaching function before `shell::itcm_load()` → refused, after it → passes; (d) a listed name missing from nm → refused.
- [ ] **Step 2: Run it**: `python shell/test_itcm_placement.py` → fails on import.
- [ ] **Step 3: Implement `itcm_placement.py`** to make every case pass; prove one RED by disabling rule (c) → case (c) fails; restore.
- [ ] **Step 4: Wire both**: ctest `shell_itcm_placement_guard`; the Makefile's `.nm` rule runs `itcm_placement.py` after `qspi_placement.py` when `SHELL_ITCM=1`, with the same delete-on-refusal branch.
- [ ] **Step 5: Linker fragment, loader, switch** as in Interfaces; build the coupon probe image (`SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1`, `C_USER_FLAGS=-DSPKY_PROBE_MARKS=1`) at `SHELL_ITCM=1` and `0`, clean each time; both link and pass both guards; `nm` shows `spky::Instrument::process` at 0x0000_01xx in the `1` image; `arm-none-eabi-size -A` reports `.itcm_hot` ≤ 65 280 B. Build the Rev A image at `SHELL_ITCM=1`: `SRAM_EXEC` free ≥ 8 KB, recorded in the commit.
- [ ] **Step 6: Desktop** `ctest --test-dir build --output-on-failure` → all pass, including both shell guards.
- [ ] **Step 7: Commit** `shell: per-sample audio path in ITCM, loaded from QSPI at boot (CPU headroom spec §4.2)`.

### Task 6: Coupon session 2 — judging ITCM (Bastian)

- [ ] **Step 1: Two probe images from Task 5's commit**, `SHELL_ITCM=1` and `0`, init patch; `cmp` → differ. Flash `1` first (a boot check: the board plays and logs), log + record 330 s; then `0`, same. If `1` stays dark or silent: stop, report, do not iterate on the board without the guard telling why.
- [ ] **Step 2: Apply the judging rule** (Global Constraints): ITCM kept at ≥ 20 k mean on cb; < 40 k → one more run of both images, judge on the mean of two.
- [ ] **Step 3: If kept**, W2 and F2 probe images at `SHELL_ITCM=1` (Task 3's switch), log + record each.
- [ ] **Step 4: Session 2 in `docs/hardware/cpu-headroom-measured.md`**: the table of Task 4 for every image; the verdict; where init, W2, F2 stand against 900 k / 930 k.
- [ ] **Step 5: Commit** `docs(hardware): CPU headroom session 2 -- ITCM`.
- [ ] **Step 6: Decide the next task from the figures:** init patch ≤ 900 k and W2, F2 ≤ 930 k → skip Tasks 7 and 8, go to Task 9. Otherwise Task 7 if deck B is what crosses, Task 8 if the margin missing is ≤ ~6 k, and spec §4.5 (stop, report to Bastian) if neither can close it.

### Task 7 (conditional): WAVE's transition cost (spec §4.3)

**Files:**
- Modify: `engine/synth/wt_osc.h` (and only what the ablation names)
- Test: `tests/test_wt_osc.cpp`

- [ ] **Step 1: Ablation image** (probe build, a `C_USER_FLAGS` define of the builder's naming): `WtOsc`'s mip crossfade off (`kRampSamples` path skipped, the new mip read at once), everything else equal. Coupon run, init patch and W2: deck B's worst block and mean against Task 6's figures. This names (or clears) the mechanism of spec §2.3.
- [ ] **Step 2: If the crossfade is the cost:** write the failing test first in `tests/test_wt_osc.cpp` for the chosen change (e.g. a shorter crossfade: after a mip change the output reaches the new mip's value within the new ramp length; or a two-table crossfade: `_read_mip_source` reads at most one table). Implement, prove RED, ctest green.
- [ ] **Step 3: Coupon run** of the change (probe image, init and W2), and **Bastian listens** to notes crossing mip boundaries (WAVE deck, a slow PITCH sweep). His verdict goes into the measured doc; a "sounds wrong" reverts the change.
- [ ] **Step 4: Commit** with the figures.

### Task 8 (conditional): Plan B — the control law across two blocks (spec §4.4)

**Files:**
- Modify: `control/control_law.h`
- Modify: `shell/control_tick.cpp`, `shell/panel_scan.cpp`
- Test: `tests/test_control_law.cpp`

**Interfaces:**
- Produces: `ControlLawT::tick_half(const float* knobs, const Options& opt, Inst& inst, int half)` — `half == 0`: deck A and the globals; `half == 1`: deck B, the depth loop and the tail (P6b-1 §6). `tick()` stays the whole tick, unchanged, and is what VCV calls.
- Produces: the shell calls `tick_half(…, g_block & 1)`; the law's `Events` are merged across the two halves.

- [ ] **Step 1: Failing test** in `tests/test_control_law.cpp`: the law on a recording `Inst` over a knob sweep — after `tick_half(0)` + `tick_half(1)` the recorder's end state equals one `tick()`'s; a BBD edge on deck B is reported by the half that ticks deck B, exactly once.
- [ ] **Step 2: Implement**, prove RED, ctest green (VCV's `ctrl_identity` render hash unchanged).
- [ ] **Step 3: Coupon run** (probe image): `cyc_last` halves to ~6 k alternating; init figures.
- [ ] **Step 4: Commit.**

### Task 9: Final session and the record (Bastian)

**Files:**
- Modify: `docs/hardware/cpu-headroom-measured.md`, `docs/roadmap.md` (a 2026-10-xx entry in the M6 section, like P6b-2's), `shell/README.md` (the CPU paragraph: deadline, levers, the new layout), `docs/gotchas.md` (two traps: global stores in the sample loop wreck the hot loop — spec §2.6; the measured window's deadline is ~945 k — spec §2.1)

- [ ] **Step 1: Shipping image** (no probe, `SHELL_ITCM=1`, init patch): flash, 10 min log + recording. Pass: `cb_max` ≤ 900 000, `tools/click_detect.py` reports `clicks=0`.
- [ ] **Step 2: W2 and F2 shipping images** (only `SHELL_BOOT_PATCH` set): 10 min each. Pass: `cb_max` ≤ 930 000, `clicks=0`.
- [ ] **Step 3: Deadline re-check** (spec §3.5): one probe image, init patch, recording aligned to the log by the click/phase method of spec §2.1; report the cb of every clicked and of the largest unclicked block. If nothing clicks, report the largest block and say the deadline could not be re-found (a pass, not a failure).
- [ ] **Step 4: By ear (Bastian)**: the init patch for several pattern cycles; the P6b-2 checks 1–4 (ownership, nudge, MOD latch with LED_1 and RV6, unlatch); turning the coupon pots through their range. Verdicts into the measured doc.
- [ ] **Step 5: Rack check**: `host/vcv/build-local.sh install`, restart Rack; `Fireflow` and `FireflowHW` load and play the init patch (the engine changes of Tasks 2 and 7 reach VCV).
- [ ] **Step 6: Write the record** (files above), including the outcome of every decision rule and the Rev A `SRAM_EXEC` figure, and the open item for P7 (spec §6: re-run §3.1/§3.2 on Rev A with the probe).
- [ ] **Step 7: Commit** `docs: CPU headroom round -- record`.
