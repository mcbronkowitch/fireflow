# ROOT per deck and WOBL Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the three reserved pots `ROOT_A`, `ROOT_B` and `REV_MOD` (WOBL) into real parameters: per-deck key, a song offset that makes a new song start from the new key, and the reverb wobble knob. This covers the engine, the shared control law, both VCV modules and the Rev A firmware.

**Architecture:** The engine already has a root per deck (`Instrument::set_root(p, …)`). This plan splits the one global `P_ROOT` into two, adds a song offset to the note decks, and wires all three panel ids through the shared control law. The song offset works like this: `ModLane` counts its full phrase rebuilds; `Part` latches `fold(root)` whenever that count moves and at `init()`, and adds the latched offset to the pitch before quantizing. Both panel generators and the firmware map are regenerated from their Python sources.

**Tech Stack:** C++17 engine (clang + Ninja, doctest), Python panel generators and guards, VCV Rack plugin (`build-local.sh`), Daisy firmware (`shell/`, ARM GCC).

**Spec:** `docs/superpowers/specs/2026-10-18-root-and-wobl-knobs-design.md`. Read it with this plan; the plan argues from it.

## Global Constraints

- Ids are exactly `ROOT_A`, `ROOT_B`, `REV_MOD` (ffctl / spkyvcv); engine ids `P_ROOT_A` (renamed in place from `P_ROOT`) and `P_ROOT_B` (appended after `P_PAN_B`).
- ROOT: `KNOBI`, range `0..11`, snap on, the law calls `std::round`. 0 = C, 11 = B. Tooltip names: `C C# D D# E F F# G G# A A# B`.
- Song offset: `fold(r) = r <= 6 ? r : r - 12` on `r` reduced to `0..11` first. So 0→0, 6→+6, 7→−5, 11→−1.
- It latches only on a **full** rebuild (the `if (rebuild)` branch of `ModLane::_apply_pending_song_work()`) and at `Part::init()`. It does not latch on partial renewals (`_renew_units`, `_renew_walk`) or on a ROOT turn.
- The offset applies only while the deck's engine is neither `ENGINE_SAMPLER` nor `ENGINE_BBD`. It is evaluated at use, not at latch time.
- WOBL boots at `0.15`; ROOT_A and ROOT_B boot at `0`. The factory patch must sound unchanged.
- At offset 0 the pitch expression must stay bit-identical. The `ctrl_identity` and `wave_formant_sweep` hashes in `CMakeLists.txt` must stay green and must not be re-cut.
- Neither ROOT nor WOBL gets a MOD-layer depth. ROOT_A and ROOT_B join SCALE in the excluded faces.
- SMEAR and PUSH stay constants (`0.30f`, `0.40f`).
- No file under `hardware/` changes.
- Panels are generated, never hand-edited. Engine/tests use `source env.sh` + Release; `shell/` uses the Daisy toolchain in a shell that never sourced `env.sh`.
- Never prefix a shell command with `cd`. Generators that must run from a directory go into a scratchpad script (template in Task 3).
- Everything written into the repo is English. Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- Every new test is proven RED once before the code that turns it green.

## What was measured for this plan (2026-10-19)

Probe: scratchpad `probe_root_impact.cpp`, the `test_param_impact.cpp` rig (two `Instrument`s, static echo/BBD memory, 48 kHz, 4 s, first 0.5 s skipped, relative RMS difference), main @ `c7daab04`, offset not yet wired.

- **The four frozen points, deck root 0 vs 11:**
  - master 1 FLOW: A 0.751, B 1.169.
  - master 2 FLOW: A 0.950, B 1.068.
  - master 3 STEP: A 0.119, B 0.943.
  - master 8 STEP: A 1.015, B 1.003.

  So `P_ROOT_A` and `P_ROOT_B` each move audio in **both** modes. `test_param_impact`'s expected sets need **no** new entry.
- **BBD:** deck A on BBD, deck B muted, based on STEP point 0, root 0 vs 5, 6 s. FLOW reads exactly **0.0000**, so ROOT is inert there (the quantizer is bypassed). STEP reads **0.2215**, because the delay clock steps on the scale grid. ROOT's grid therefore reaches BBD in STEP. The song offset still stays off BBD, per the spec: its PITCH lane is the delay clock, not a note (owner ruling 2026-08-07, `lane.h:277-283`).
- No render scenario under `host/render/scenarios/` sets a root, so every hash scenario runs at root 0 → offset 0.

## Review Focus

1. **Engine swap with a latched offset.** Take a SYNTH deck latched at +2 and switch it to BBD or SAMPLER: the offset must not apply. Switch it back to SYNTH without a rebuild: the old +2 applies again. Pinned in Task 2, `song offset: applies only on note engines`.
2. **Reinit mid-session.** A VCV sample-rate change calls `Part::init()`, and the lane regenerates its phrase there (`lane.cpp:86`, not through `_apply_pending_song_work`). The offset must re-latch from the current root, not fall back to 0. Pinned in Task 2, `song offset: init latches the current root`.
3. **Out-of-range root ints.** A host or scenario can push 12, −1 or 23. `fold` must reduce mod 12 first: 12→0, −1→−1, 23→−1. Pinned in Task 2, `song offset: fold table`.
4. **Frozen-point B slot.** If `P_ROOT_B` were zero-filled, deck B would silently move to C at every operating point. Pinned in Task 1, `param impact points: P_ROOT_B mirrors P_ROOT_A`.
5. **Pitch stays inside 0..1 at the span edge.** With offset +6 and TUNE at max, the clamp must sit after the offset is added. Pinned in Task 2, `song offset: clamp holds at the span edge`.

---

### Task 1: Engine table: split `P_ROOT` into `P_ROOT_A` and `P_ROOT_B`

**Files:**
- Modify: `engine/param_table.h:74` (rename), `:126` (append), `:162` (apply), `:18-20` (comment)
- Modify: `tests/param_impact_points.h` (header note plus the four vectors)
- Test: `tests/test_param_table.cpp:70`, plus a new case in `tests/test_param_table.cpp`

**Interfaces:**
- Produces: `P_ROOT_A` (same index as old `P_ROOT`), `P_ROOT_B == P_COUNT - 1`, both `X(…, 0.f, 11.f, 12)`. `apply_param(in, P_ROOT_A, v)` → `in.set_root(PART_A, i)` only; `P_ROOT_B` → `PART_B` only.

- [ ] **Step 1: Write the failing tests.**
  - In `test_param_table.cpp`, change the inventory marker to `CHECK(P_ROOT_B == P_COUNT - 1);`.
  - Add `TEST_CASE("param table: ROOT is per deck")`. It applies `P_ROOT_A = 5` to a fresh `Instrument` (`init(48000.f)`) and checks `part(PART_A).quant().root_semis() == 5` and `part(PART_B)…root_semis() == 0`. Then it does the mirror with `P_ROOT_B = 9`.
  - Add `TEST_CASE("param impact points: P_ROOT_B mirrors P_ROOT_A")`, which includes `param_impact_points.h`. For each of the four points, `v[P_ROOT_B] == v[P_ROOT_A]` and `v[P_ROOT_A]` is one of 6/6/8/4 in order.
  - If `quant()` is not reachable as const through `part()`, add a `const Quantizer& quant() const` to `Part`.
- [ ] **Step 2: Run and see RED.** Use `source env.sh && cmake --build build && build/spky_tests.exe -tc="param table*,param impact points*"`. It is expected to fail to compile (`P_ROOT_B` undeclared).
- [ ] **Step 3: Implement.**
  - Rename `P_ROOT` → `P_ROOT_A` in place.
  - Append `X(P_ROOT_B, 0.f, 11.f, 12)` after `P_PAN_B`, with a comment in the PULL/PAN style that says why it is appended.
  - Split the `apply_param` case.
  - In each of the four vectors in `param_impact_points.h`:
    - Rename the `// P_ROOT` comment to `// P_ROOT_A`.
    - Make the tail explicit after `// P_PACE`: `0.0f, // P_PULL`, `0.0f, // P_PAN_A`, `0.0f, // P_PAN_B`. These are the values the zero-fill gives today.
    - Add `<that point's P_ROOT_A value>, // P_ROOT_B`.
  - Add an "APPENDED, NOT ZERO-FILLED" paragraph to the header saying why the B slot carries the old global value.
  - Update `test_param_impact.cpp`'s `HOW TO TELL` pointer text if it names `P_PAN_B` as the last id.
- [ ] **Step 4: Run and see GREEN.** Run the same command, then `build/spky_tests.exe -tc="param impact*"`. Every case must pass, and the expected sets stay unchanged (see the measurement above).
- [ ] **Step 5: Commit** `feat(engine): P_ROOT splits into P_ROOT_A and P_ROOT_B`.

### Task 2: Engine: the song offset

**Files:**
- Modify: `engine/mod/lane.h` and `engine/mod/lane.cpp:313-328` (rebuild counter)
- Modify: `engine/mod/super_modulator.h` (pass-through)
- Modify: `engine/parts/part.h` and `engine/parts/part.cpp:13-106`, `:167-169`, `:256` (latch and use)
- Modify: `engine/instrument.h` (observer under `SPKY_TESTING`)
- Create: `tests/test_song_offset.cpp`; Modify: `CMakeLists.txt` (add it to `spky_tests`)

**Interfaces:**
- Consumes: `Instrument::set_root(int p, int semis)`, `Instrument::new_phrase(int p)`, `Instrument::target_value(int p, int slot)` (returns `_pitch_q`, 0..1, i.e. semitones / 36).
- Produces:
  - `constexpr int song_offset_for_root(int r)` in `engine/parts/part.h` (namespace `spky`): reduce `r` mod 12 into 0..11, then `r <= 6 ? r : r - 12`.
  - `uint32_t ModLane::phrase_builds() const`. It counts how often the `if (rebuild)` branch ran; nothing else increments it.
  - `uint32_t SuperModulator::pitch_phrase_builds() const`, which returns `_lanes[LANE_PITCH].phrase_builds()`.
  - `int Part::song_offset() const` returns the latched value, whatever the current engine.
  - `int Instrument::song_offset_for_test(int p) const` under `SPKY_TESTING`.
- Placement:
  - `Part::init()` sets `_seen_builds = _mod.pitch_phrase_builds()` and `_song_offset = song_offset_for_root(_quant.root_semis())`. This goes after `_mod.init`, before the closing `_pitch_q = _quant.process(...)`.
  - `_control_tick()` re-latches at its top when the count moved.
  - `pitch_pre_quant()` becomes `clampf(target_raw(LANE_PITCH) + (_tune - 0.5f) + _offset_norm(), 0.f, 1.f)`. `_offset_norm()` is `float(_song_offset) / Quantizer::SPAN_SEMIS` when `_engine_id` is neither SAMPLER nor BBD, else `0.f`. At offset 0 it adds `0.0f`, which is exact.

- [ ] **Step 1: Write the failing tests** in `tests/test_song_offset.cpp`.
  - Rig: two `Instrument`s, `init(48000.f)`, deck A on SYNTH in STEP (`set_sync(true)`, `set_step(PART_A, true, 8)`), the same patch in both.
  - Pitch is read as `inst.target_value(PART_A, LANE_PITCH) * 36.f` after each 96-sample `process()` block.
  - Use absolute bounds (`std::fabs(x - want) < 1e-3f`), not `doctest::Approx` (memory `doctest-approx-epsilon-trap`).
  - Cases:
    - `song offset: fold table`: 0→0, 1→1, 6→6, 7→−5, 11→−1, 12→0, −1→−1, 23→−1.
    - `song offset: a rebuilt song sits exactly the root distance away`:
      - Set root 0 on twin 1 and root 2 on twin 2, then `new_phrase(PART_A)` on both.
      - Render until `song_offset_for_test(PART_A)` is 2 on twin 2. Fail if that takes more than 30 s.
      - Then, for 8 s of ticks where both pitches are inside `[1, 35]`, check `p2 - p1 == 2` within 1e-3.
      - Also check that at least 100 ticks were compared, so the case cannot pass on an empty window.
    - `song offset: a root turn alone does not re-latch`:
      - After the twin-2 rebuild above, `set_root(PART_A, 4)` and render 10 s without `new_phrase`. The offset is still 2.
      - Then `new_phrase` and render until it is 4.
      - Then set root 7, `new_phrase`, and expect −5.
      - Then set root 6, `new_phrase`, and expect +6.
    - `song offset: applies only on note engines`:
      - Twin X: root 2, then `new_phrase`, so it latches +2.
      - Twin Y: root 0, then `new_phrase`, so it latches 0; then `set_root(PART_A, 2)` with no rebuild.
      - Both now run root 2 on the same phrase (one `new_phrase` each, so the RNG streams match); only the latched offset differs.
      - On SYNTH, X is 2 above Y.
      - Switch both to BBD (`set_engine`) and wait for the swap. In STEP, X and Y have equal pitch, while `song_offset_for_test` still reports 2 on X.
      - Switch both back to SYNTH: X is again 2 above Y, with no rebuild in between.
      - Repeat the check with SAMPLER in place of BBD. With no record buffer it is silent, but `target_value` still carries the pitch.
    - `song offset: init latches the current root`: `set_root(PART_A, 5)`, then `init(48000.f)` again → `song_offset_for_test(PART_A) == 5`.
    - `song offset: clamp holds at the span edge`: root 6 latched, TUNE at 1.0 (`set_tune(PART_A, 1.f)`). For 4 s, `target_value(PART_A, LANE_PITCH)` stays within `[0, 1]`.
    - Bit-identity at root 0 is guarded by the existing `ctrl_identity` hash, not by a new case. A twin comparison here would run the same code on both sides and could never go red. Prove that guard's RED once: temporarily add `1e-7f` in `_offset_norm()`, watch `ctrl_identity` fail, then revert by hand. Do not use `git checkout` (memory `fireflow-scripted-edits-crlf`).
- [ ] **Step 2: Run and see RED.** Use `source env.sh && cmake --build build && build/spky_tests.exe -tc="song offset*"`. Expected: a compile failure on the missing functions. After stubbing them to return 0, it must still fail on the twin, turn and fold cases (a RED for the right reason).
- [ ] **Step 3: Implement** the interfaces above. The counter increment goes inside the existing `if (rebuild) { … }` block, nowhere else. Write a comment at the latch saying why partial renewals do not re-latch: spec §2.1, it would transpose half a song.
- [ ] **Step 4: Run and see GREEN.** Run `build/spky_tests.exe -tc="song offset*"`, then the full `ctest --test-dir build --output-on-failure -E "^reva_"`. It must be all green. `ctrl_identity` and `wave_formant_sweep` must be green with their existing hashes; if either moved, the offset leaked at root 0, so fix the code and do not re-cut the hash.
- [ ] **Step 5: Commit** `feat(engine): a rebuilt song starts from the deck's root`.

### Task 3: Panel params, control law, big VCV panel, bench mirror

**Files:**
- Modify: `host/vcv/res/gen_panel.py`:
  - three `Ctl`s appended to `APPENDED_PANEL_PARAMS` after `PAN_B`
  - `_KNOBI_TOP` gets `"ROOT_A": "11.f", "ROOT_B": "11.f"`
  - `INIT_DEFAULTS` gets `ROOT_A 0.0, ROOT_B 0.0, REV_MOD 0.15`
  - the comment at `:532-534` is updated
- Regenerate: the files `gen_panel.py` writes (`control/params.hpp`, the generated VCV headers, the SVG).
- Modify: `control/control_law.h` — the `D_ROOT` deck slot appended after `D_REV_MIX`, `Out::set_root`, the `:927-936` block, and the root calls after `set_scale` at `:937`.
- Modify: `tests/control_recorder.h:80` (`REC_P(set_root)`).
- Modify: `host/vcv/src/Fireflow.cpp:460-503` (ROOT tooltip branch).
- Modify: `bench/audition/init_patch.cpp:222-227`.
- Tests: `tests/test_control_law.cpp`, `tests/test_seed_audition_init.cpp:87-92`, `tests/test_control_params.cpp:52`, `tests/test_mod_layer.cpp:139`, `host/vcv/res/test_panel.py`.

**Interfaces:**
- Consumes: `P_ROOT_A`/`P_ROOT_B` (Task 1) — not used by the law, which calls `Instrument::set_root(int, int)` directly.
- Produces:
  - `ffctl::ROOT_A`, `ffctl::ROOT_B`, `ffctl::REV_MOD` (and the `spkyvcv::` twins), with `NUM_PARAMS` 125 → 128.
  - `kParamRange[ROOT_*] = {0, 11, snap}`, `kParamRange[REV_MOD] = {0, 1, no snap}`.
  - `kInitParamDefaults` = 0, 0, 0.15.
  - In the law: `inst.set_reverb_mod(prm(REV_MOD))`, `inst.set_root(spky::PART_A, (int)std::round(prm(ROOT_A)))`, and the same for B. Read them as `prm(ROOT_A)`/`prm(ROOT_B)`, never through `mvp()` (an appended pair, `:586`).
- Big-panel placement:
  - ROOT_A at `(L, ROW_DUO2)` under SCALE and ROOT_B at `(R, ROW_DUO2)` under DRIFT. Both are `KNOBI`, label `ROOT`, in the row the comment at `:525-530` calls empty apart from PULL.
  - WOBL is `Ctl("REV_MOD", SMKNOB, R, ROW_ROOM2, "WOBL")`, the freed R-column ROOM slot `(CX+10.5, 104.5)`.
  - If a layout guard rejects a position, the builder moves it inside the guards and records why in the commit message.

Run generators through a scratchpad script, never `cd` in the command:

```bash
# <scratchpad>/regen_panels.sh
cd "$(git rev-parse --show-toplevel)/host/vcv" && for g in "$@"; do python "res/$g" || exit 1; done
```

Task 3 runs `bash <scratchpad>/regen_panels.sh gen_panel.py`; Task 4 runs it with `gen_panel.py gen_hw_panel.py`.

- [ ] **Step 1: Write the failing tests.**
  - `test_control_law.cpp`, `TEST_CASE("law: ROOT per deck, rounded")`:
    - For `k` in 0..11, set `r.k[ROOT_A] = k` and `r.k[ROOT_B] = 0`, then tick. Check `r.inst.last("set_root", 0) == k` and `r.inst.last("set_root", 1) == 0`.
    - Then the mirror.
    - Then `r.k[ROOT_A] = 4.4f` → 4 and `4.6f` → 5.
  - `TEST_CASE("law: WOBL drives set_reverb_mod")`:
    - A fresh `Rig` (init defaults): `last("set_reverb_mod") == 0.15f` within 1e-6.
    - `k[REV_MOD] = 0` → 0; `k[REV_MOD] = 1` → 1.
    - Also check that SMEAR is still `0.30f` and PUSH `0.40f` (`set_reverb_smear`, `set_master_drive`).
  - `test_seed_audition_init.cpp`:
    - `NUM_PARAMS == 128`, with the comment history extended (`125 -> 128 on 2026-10-19: ROOT_A/B, REV_MOD`).
    - The init audition pushes root 0 on both decks.
    - Keep the existing `reverb_mod_for_test() == 0.15` pin.
  - `test_control_params.cpp`: `is(ROOT_A, 0.f, 11.f, true); is(ROOT_B, 0.f, 11.f, true); is(REV_MOD, 0.f, 1.f, false);`.
  - `test_mod_layer.cpp:139`: `ROOT_A` and `ROOT_B` join the excluded faces, and `REV_MOD` too, so no depth row can appear.
  - `test_panel.py`:
    - `test_fixed_values_and_dead_controls` (`:3575`): `REV_MOD` leaves the dead list (`REV_SMEAR` stays).
    - The literal loop at `:3626` drops `set_reverb_mod`. A new check requires `set_reverb_mod(prm(REV_MOD))` in `control/control_law.h` and `set_reverb_mod(value(REV_MOD))` in `bench/audition/init_patch.cpp`, each exactly once.
    - Update the `APPENDED_PANEL_PARAMS`/`PARAM_ORDER` and negative-index pins.
    - The excluded-faces list at `:3724` gains ROOT_A, ROOT_B and REV_MOD.
    - Retype `approved` (`:2859`) by hand: `ROOT_A 0.0`, `ROOT_B 0.0`, `REV_MOD 0.15`. It skips unknown names, so this is the only thing that pins them.
- [ ] **Step 2: Run and see RED.**
  - Run `source env.sh && cmake --build build && build/spky_tests.exe -tc="law: ROOT*,law: WOBL*"`. It is expected not to compile: `ROOT_A` and `REV_MOD` are undeclared.
  - Run `ctest --test-dir build -R panel_guard --output-on-failure`, which should be red on the new pins.
- [ ] **Step 3: Implement.**
  - Write the `gen_panel.py` entries and regenerate (`bash <scratchpad>/regen_panels.sh gen_panel.py`).
  - Law:
    - Add the `D_ROOT` slot.
    - Add `void set_root(int p, int v) { if (law.deck(p, D_ROOT, v)) to.set_root(p, v); }` beside `set_pan`.
    - Replace `inst.set_reverb_mod(0.15f)` with `prm(REV_MOD)`.
    - Add the two root calls right after `set_scale`.
    - Rewrite the comment at `:927-933`: PUSH and SMEAR stay fixed by ear, with the same quotes; WOBL got its knob back on 2026-10-07 and boots at 0.15.
  - `Fireflow.cpp`: add a branch before the `STEPS` fallback, `else if (c.id == ROOT_A || c.id == ROOT_B) configSwitch(c.id, lo, hi, init, "Root", {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"});`.
  - `init_patch.cpp`:
    - Replace the `0.15f` with `value(REV_MOD)`.
    - After `set_scale`, add `inst.set_root(spky::PART_A, static_cast<int>(std::lround(value(ROOT_A))))` and the same for B.
    - Fix the comment at `:222-223` (only PUSH and SMEAR are mirrored constants now).
- [ ] **Step 4: Run and see GREEN.**
  - Run `cmake --build build && ctest --test-dir build --output-on-failure -E "^reva_"`.
    - Green: `spky_tests` (with `test_param_impact`), `ctrl_identity` with an unchanged hash, and `panel_guard`.
    - `hw_panel_guard` and `shell_panel_map_guard` belong to Task 4. The HW generator still treats ROOT and REV_MOD as reserved, so it may reject three params that have no position. Write their state into the task report.
    - If `gen_hw_panel.py` itself refuses to run, merge Task 4 into this task's commit rather than leaving a red tree.
  - Then run `host/vcv/build-local.sh` (a build, not yet an install).
- [ ] **Step 5: Commit** `feat: ROOT_A/ROOT_B and WOBL become panel parameters`, with the regenerated files included.

### Task 4: Hardware panel and firmware map: the pots stop being reserved

**Files:**
- Modify: `host/vcv/res/gen_hw_panel.py`:
  - `:217` comment
  - `:368-371` `RESERVED` (delete), `:384-385` (no `RESERVED` filter)
  - `:520-525` `HW_ONLY` (only `SHIFTBTN` stays)
  - caption: REV_MOD prints `WOBL`, ROOT prints `ROOT`
- Modify: `shell/gen_panel_map.py:16-18` (docstring) and `:57-61` (`RESERVED = {}`). Keep the mechanism; SHIFT and future reserved pots still use it.
- Modify: `tools/scan_budget.py:124` comment.
- Regenerate: `host/vcv/res` HW outputs and `shell/generated_panel_map.h` (`python shell/gen_panel_map.py`).
- Tests: `host/vcv/res/test_hw_panel.py:514` (58 → 61), `:521-535` (3 hw-only small pots → 0, docstring rewritten), `shell/test_gen_panel_map.py:34` (70 → 73), `:42-49`, `:115-116`, `:133-134`.

**Interfaces:**
- Consumes: `ffctl::ROOT_A/ROOT_B/REV_MOD` from Task 3 (they must exist in `control/params.hpp` and the generated HW header before `gen_panel_map.py` accepts them).
- Produces: `FireflowHW` draws three real knobs at the frozen plate positions (`DECK_CELLS["ROOT"] = (5, 1)` mirrored, `CENTRE_CELLS["REV_MOD"] = (5, 1)`). The firmware rows send `ffctl::ROOT_A`, `ffctl::ROOT_B` and `ffctl::REV_MOD`.

- [ ] **Step 1: Write the failing guards.**
  - `test_gen_panel_map.py`:
    - `73 rows send a parameter`.
    - `0 reserved rows send nothing`.
    - Replace the "are ROOT_A, ROOT_B and REV_MOD" check with "ROOT_A, ROOT_B and REV_MOD send their own ffctl id", read from the row code part.
    - Rewrite the two sabotages so each still proves its point:
      - `"formerly reserved pot loses its id"`: `i["params"].discard("ROOT_A")` → `"ROOT_A: no ffctl id"`.
      - `"WOBL missing from the VCV HW panel"`: `i["hw_ids"].discard("REV_MOD")` → `"not in generated_hw_panel.hpp: REV_MOD"`.
    - Keep `"reserved name that is no pot"` unchanged; it proves the mechanism still validates.
  - `test_hw_panel.py`: `len(small) == 61`, `HW_ONLY` small pots `== 0`, and a new check that `ROOT_A`, `ROOT_B` and `REV_MOD` are in `HW_PARAMS` at exactly the positions the old `HwOnly` entries had (`_deck_xy("ROOT")`, its mirror, `_centre_xy("REV_MOD")`).
- [ ] **Step 2: Run and see RED.** Run `ctest --test-dir build -R "hw_panel_guard|shell_panel_map_guard" --output-on-failure`. It is expected to be red on the counts and the position check.
- [ ] **Step 3: Implement** the generator edits and regenerate (`bash <scratchpad>/regen_panels.sh gen_panel.py gen_hw_panel.py`, then `python shell/gen_panel_map.py`). Check that `git status hardware/` is clean.
- [ ] **Step 4: Run and see GREEN.**
  - Run `ctest --test-dir build --output-on-failure -E "^reva_"` and `python shell/gen_panel_map.py --check`; both must be green.
  - Firmware, in a fresh shell without `env.sh`: `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images`, which must link.
  - Check that `git status hardware/` shows nothing.
- [ ] **Step 5: Commit** `feat(hw): ROOT_A, ROOT_B and WOBL pots send their parameters`.

### Task 5: Docs, measured facts, install, listening handoff

**Files:**
- Modify:
  - `docs/by-ear-decisions.md:60-68` (WOBL has a knob, boots 0.15; SMEAR's and PUSH's "no knob" rules stand)
  - `host/vcv/README.md:146-149`, `shell/README.md:266-269`
  - `docs/roadmap.md` (the "Reserved knobs become parameters" entry, plus its other mentions)
  - `engine/param_table.h:18-20`
  - `docs/engine-map.md`: under §6, the measured BBD fact (ROOT inert in FLOW, alive in STEP, 0.0000 / 0.2215, with this plan's setup) and the song-offset latch rule with its file:line.
- Grep check: `grep -rn "reserved" host/vcv/res/gen_hw_panel.py shell/gen_panel_map.py tools/scan_budget.py engine/param_table.h` → only the SHIFT key and the generic mechanism remain.

- [ ] **Step 1: Write the doc edits** listed above. Do not claim any sound result; the listening pass is Bastian's.
- [ ] **Step 2: Verify.** Run `ctest --test-dir build --output-on-failure -E "^reva_"` (all green) and `host/vcv/build-local.sh install` (it must report the install path).
- [ ] **Step 3: Commit** `docs: ROOT per deck and WOBL -- reserved knobs are parameters`.
- [ ] **Step 4: Hand off to Bastian**: restart Rack, then do the listening pass of spec §8.7:
  - The init patch sounds as before.
  - Turning ROOT changes the key without moving the register.
  - The next SONG rung brings a song in the new key.
  - WOBL wobbles.
  - Both modules show the three knobs, and the ROOT tooltip shows note names.

  §8.8 (the real Rev A pots) waits for the P7 bring-up and is not a gate here.
