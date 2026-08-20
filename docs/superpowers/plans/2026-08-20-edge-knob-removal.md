# EDGE Knob Removal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the EDGE knob, its `ParamId` pair, and all six per-engine EDGE cells from the instrument, leaving both panel positions empty.

**Architecture:** Top-down, so the tree stays green after every task. The knob leaves the two panels first (Task 1), then the parameter and the broadcast line (Task 2), then each engine's now-dead cell (Tasks 3–5), then the bench row and the docs (Task 6). Removal is not additive work, so the TDD cycle inverts: the tests that pin EDGE are deleted *with* the code they pin, and the RED that must be proved once is the param-inventory tripwire in Task 2.

**Tech Stack:** C++17 engine (clang + Ninja, Release), doctest, VCV Rack plugin (own toolchain), Python panel generators, ARM GCC bench firmware (not built here).

**Spec:** None. This plan reverses the EDGE half of `docs/superpowers/specs/2026-08-19-voice-knobs-dpth-edge-design.md`; that document stays in the repo as the record of a decision that was made and then withdrawn. The withdrawal decision itself (2026-08-20, Bastian, by ear) is recorded in Task 6.

## Global Constraints

- **`-DCMAKE_BUILD_TYPE=Release` is mandatory.** A Debug configure makes `spky_tests` and `ctrl_identity` fail with "SYNTH reference moved" for reasons unrelated to this work.
- **`source env.sh` before any engine/test/render build; never in a shell used for `shell/` or `bench/`.** The two toolchains must not mix.
- **The VCV plugin builds only via `host/vcv/build-local.sh`**, run from `host/vcv/`. The system `g++` on this machine is the ARM cross-compiler.
- **Both panels are generated, never hand-edited.** `res/gen_panel.py` and `res/gen_hw_panel.py`, each run from `host/vcv/`.
- **Everything written into the repo is English** — code, comments, commit messages, docs.
- **Commit trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- **DPTH stays.** `DEPTH_A`/`DEPTH_B`, `sampler_cfg::kMotionBaseScale`, and every `set_target_base(p, LANE_MOTION, …)` call are out of scope. DPTH and EDGE shipped in one wave; only EDGE is being withdrawn.
- **No renders may move.** EDGE's neutral is an exact bypass on every engine by construction: SYNTH/WAVE/SAMPLER/BBD skip `OnePoleHp::process()` entirely at `_edge == 0`, and FEED and BODY multiply their corner by `std::pow(2.f, k * 0.f)`, which is exactly `1.0f`. So `ctrl_identity`, `wave_formant_sweep` and every other render hash in `CMakeLists.txt` must survive this plan unchanged. **If a render hash moves, stop and report it — do not re-bless the hash.** A moved hash means something other than EDGE was removed.
- **The panel positions stay empty.** Bastian's decision, 2026-08-20: the two knob slots become free space. Do not re-flow, re-centre, or re-distribute the neighbouring controls into the gap on either panel.

---

### Task 1: The knob leaves both panels

Removes `DAMP_A`/`DAMP_B` from the generated panel inventory. After this task nothing pushes EDGE from the host; `Instrument::set_voice_edge` still exists and is simply unused.

**The trap in this task:** removing two `Ctl` entries shifts the VCV-side `ParamId` enum in `generated_panel.hpp`. `host/vcv/src/init_patch.hpp` is an *ordered array indexed by that enum* — the two `DAMP` slots must be deleted from it in the same commit, or every init default from that point on is paired with the wrong parameter and still compiles. This is the VCV twin of the trap `tests/param_impact_points.h` documents for the engine-side enum.

**Files:**
- Modify: `host/vcv/res/gen_panel.py:286`, `:647-648`, `:802-803`, and the `DAMP`-related comment block at `:613-634`
- Modify: `host/vcv/res/gen_hw_panel.py:125`, `:154-159`, `:219-221`, `:405`
- Modify: `host/vcv/src/Fireflow.cpp:55-60`, `:322-327`, `:400-405`, `:940-948`
- Modify: `host/vcv/src/init_patch.hpp:78-79`
- Regenerate: `host/vcv/src/generated_panel.hpp`, `host/vcv/src/generated_hw_panel.hpp`
- Modify: `host/vcv/res/test_panel.py:68`, `:83-84`, `:106`, `:191`, `:195`, `:328`, `:490`, `:1809`, `:1840-1841`, `:1872`, `:1890`, `:1906-1943`, `:1977-1979`
- Modify: `host/vcv/res/test_hw_panel.py:330`, `:769-780`
- Modify: `tests/test_seed_audition_init.cpp:68-80`

- [ ] **Step 1: Delete the two `Ctl` rows and the caption row from the large panel generator**

In `host/vcv/res/gen_panel.py`, delete these two lines (`:647-648`):

```python
    Ctl("DAMP_A",  SMKNOB, VOICE_X[3],     ROW_V2, "EDGE", "Second-filter trim"),
    Ctl("DAMP_B",  SMKNOB, W - VOICE_X[3], ROW_V2, "EDGE", "Second-filter trim"),
```

Delete the `DAMP` row from `DYNAMIC_CAPTIONS` (`:286`):

```python
    ("DAMP",     "ENGINE",   ("EDGE", "EDGE", "EDGE",  "SNAP",  "PRE",  "EDGE")),
```

Delete both `INIT_DEFAULTS` entries (`:802-803`):

```python
    "DAMP_A": 0.000000000,
    "DAMP_B": 0.000000000,
```

Leave `VOICE_X[3]` and `ROW_V2` themselves in place — `DEPTH_A`/`DEPTH_B` still use that column. Rewrite the surrounding comment block at `:613-634` so it explains that the fourth VOICE column now carries DPTH alone and the slot below it is deliberately empty; do not leave a comment describing a knob that no longer exists.

- [ ] **Step 2: Delete DAMP from the hardware panel generator**

In `host/vcv/res/gen_hw_panel.py`, delete `"DAMP": "S"` from the size map (`:125`), delete `"DAMP": "EDGE"` from the word map (`:159`) together with its four-line PLACEHOLDER WORD comment (`:154-158`), and delete `"DAMP":   (107.25, Y_B1M)` from the position map (`:221`). Update the comments at `:219` and `:405` — they say ENG's move "pays for DEPTH and DAMP"; it now pays for DPTH and a free slot.

- [ ] **Step 3: Regenerate both panels**

```bash
python host/vcv/res/gen_panel.py
python host/vcv/res/gen_hw_panel.py
```

Both scripts resolve paths relative to `host/vcv/`; if either fails on a path, run it with `host/vcv` as the working directory instead. `gen_hw_panel.py` prints its own parameter count — record what it prints; the count drops by 2.

- [ ] **Step 4: Remove the host-side push and the param config**

In `host/vcv/src/Fireflow.cpp`, delete the `configParam` branch (`:404-405`):

```cpp
                    else if (c.id == DAMP_A || c.id == DAMP_B)
                        configParam(c.id, -1.f, 1.f, init, lbl);
```

and its four-line comment above it. Delete the push line and its comment block (`:940-948`), whose last line is:

```cpp
            inst.set_voice_edge(p, pp(DAMP_A, p));
```

Delete the file-scope comment at `:55-60` (it explains why EDGE has no knob-to-Hz law here) and trim the comment at `:322-327` so it speaks only about DPTH.

- [ ] **Step 5: Delete the two init-patch slots**

In `host/vcv/src/init_patch.hpp`, delete exactly these two lines (`:78-79`) and nothing else:

```cpp
    0.0f, // DAMP_A
    0.0f, // DAMP_B
```

Verify the remaining entries still read in enum order against the regenerated `generated_panel.hpp` — the comment on each line names its parameter, so a mis-shift is visible by reading, not by compiling.

- [ ] **Step 6: Update the panel guards**

In `host/vcv/res/test_panel.py`: remove `'DAMP_A', 'DAMP_B'` from the inventory list at `:68` and from the three ordering assertions at `:106`, `:191`, `:195`. Delete the whole EDGE gate — the `EDGE_NEUTRAL_CONSTANTS` tuple at `:1921` and the test that consumes it at `:1932-1943` — and the mutation entry at `:1977-1979` that proves that gate can go red. Delete the `set_voice_edge` push check at `:1840-1841`. Update the prose at `:83-84`, `:328`, `:490`, `:1809`, `:1872` and `:1890` so no comment claims a knob or a gate that is gone.

In `host/vcv/res/test_hw_panel.py`: change the count comment and literal at `:330` from 57 to 55, and remove the `("DAMP_A", "SUB_A")` pair from the position-mirror check at `:780`.

- [ ] **Step 7: Update the VCV param-count pin**

In `tests/test_seed_audition_init.cpp:74-80`, change 73 to 71 in both the `CHECK_MESSAGE` condition and its message, and add one line to the comment above recording the move (`73 -> 71 on 2026-08-20: DAMP_A/B, the EDGE knob, removed`).

- [ ] **Step 8: Build and run every gate**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all pass, including `panel_guard` and `hw_panel_guard` (both run under ctest, `CMakeLists.txt:298-310`) and every render-hash gate. Then build the plugin:

```bash
host/vcv/build-local.sh
```

Expected: clean build. `pytest` is not installed on this machine — the panel guards run as plain scripts, which is what ctest does.

- [ ] **Step 9: Commit**

```bash
git add host/vcv tests/test_seed_audition_init.cpp
git commit -m "$(cat <<'EOF'
feat(panel): the EDGE knob leaves both panels, its slot stays empty

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 2: The parameter and the broadcast line

Removes `P_EDGE_A`/`P_EDGE_B` from the engine's `ParamId` enum, the `Instrument`/`Part` API, and the render host. After this task no caller can reach any engine's `set_edge`; the six cells themselves are still compiled and still tested by their own per-engine cases, which Tasks 3–5 remove.

**This is the trap task.** `tests/param_impact_points.h` holds four frozen vectors written out in `ParamId` order. Removing two ids shifts every later value up by two — and C++ accepts a short aggregate initializer without a warning, so **the file keeps compiling and every value past `P_FILT_B` is silently paired with the wrong parameter**. The file's own header (`:20-44`) documents this exact failure mode from the insertion side. The tripwire is `tests/test_param_table.cpp`'s inventory marker, and Step 2 proves it fires before Step 3 fixes it.

**Interfaces:**
- Removes: `Instrument::set_voice_edge(int p, float t)`, `Part::set_voice_edge(float t)`, `ParamId::P_EDGE_A`, `ParamId::P_EDGE_B`
- Leaves untouched: every engine's own `set_edge(float)` — Tasks 3–5 remove those

**Files:**
- Modify: `engine/param_table.h:92`, `:179-180`
- Modify: `engine/instrument.h:325-326`
- Modify: `engine/parts/part.h:203-215`
- Modify: `host/render/scenario.cpp:170`
- Modify: `tests/param_impact_points.h:29-35`, `:98-99`, `:166-167`, `:237-238`, `:305-306`
- Modify: `tests/test_param_table.cpp:49-55`
- Modify: `tests/test_param_impact.cpp:308-317`
- Delete: `tests/test_voice_edge_broadcast.cpp`
- Modify: `CMakeLists.txt:171`

- [ ] **Step 1: Remove the parameter, the API and the render-host verb**

In `engine/param_table.h`, delete the X-macro row (`:92`):

```cpp
  X(P_EDGE_A,    -1.f, 1.f, 0)   X(P_EDGE_B,    -1.f, 1.f, 0) \
```

and the two dispatch cases (`:179-180`):

```cpp
    case P_EDGE_A:     in.set_voice_edge(PART_A, v); break;
    case P_EDGE_B:     in.set_voice_edge(PART_B, v); break;
```

In `engine/instrument.h`, delete `set_voice_edge` and its comment (`:325-326`). In `engine/parts/part.h`, delete `set_voice_edge` (`:215`) and the comment block above it (`:203-213`) that names `tests/test_voice_edge_broadcast.cpp` as its ledger. In `host/render/scenario.cpp`, delete the verb (`:170`):

```cpp
    else if (a == "set_voice_edge")      inst.set_voice_edge(e.part, e.value);
```

- [ ] **Step 2: Prove the first tripwire — the build must not compile**

Build *before* touching any test file:

```bash
source env.sh
cmake --build build
```

Expected: **compile error** in `tests/param_impact_points.h` — `excess elements in array initializer` (or your compiler's wording for it), four times, once per frozen vector.

This is the tripwire, and it is stronger than the test below. `param_impact_points.h:53` declares `float v[P_COUNT]` and fills it positionally: *inserting* a `ParamId` leaves a short initializer, which C++ accepts silently and zero-fills — that is the silent failure the file's header warns about. *Removing* two ids leaves each vector two elements too long, which is a hard error. Record the observed error in the task report. Do not proceed until you have seen it.

- [ ] **Step 3: Prove the second tripwire — the inventory marker goes red**

Delete the eight lines named in Step 4 below (two per vector), then:

```bash
source env.sh
cmake --build build
./build/spky_tests -ts="*param table*"
```

Expected: the build now succeeds and the run **FAILS** on `CHECK(P_MODE == 64)` at `tests/test_param_table.cpp:55`, reporting 62. Record this second observation in the task report too. Only after seeing it red do you move the marker in Step 4.

- [ ] **Step 4: Un-shift the frozen vectors and move the marker**

The eight lines deleted in Step 3 are exactly these two, from **each of the four vectors** (at `:98-99`, `:166-167`, `:237-238`, `:305-306`):

```cpp
      0.0f, // P_EDGE_A
      0.0f, // P_EDGE_B
```

Every other line carries its parameter's name in a trailing comment, so verify by reading that `// P_FILT_B` is now immediately followed by `// P_FLUXMIX_A` in all four. Then rewrite the file's `AN INSERTION HAS HAPPENED ONCE` paragraph (`:29-35`) into the honest record: the insertion happened on 2026-08-19 and was undone on 2026-08-20, the vectors are back at their pre-insertion alignment, and the two removed slots held EDGE's neutral so the four points still describe exactly the instrument they were captured from. Add the asymmetry Step 2 demonstrated, because it is the file's most useful fact and is currently absent: **an insertion shifts these vectors silently, a removal does not compile.** The warning in this file exists for the first case only.

In `tests/test_param_table.cpp`, change `CHECK(P_MODE == 64)` to `CHECK(P_MODE == 62)` (`:55`) and rewrite the `62 -> 64 on 2026-08-19` note (`:49-53`) to record the reverse move.

- [ ] **Step 5: Drop the broadcast test and the stale impact note**

Delete `tests/test_voice_edge_broadcast.cpp` and remove its line from `CMakeLists.txt:171`. In `tests/test_param_impact.cpp:308-317`, delete the paragraph explaining why the STUBBED `EDGE_A`/`EDGE_B` group was deleted — the ids it names no longer exist.

- [ ] **Step 6: Rebuild and run everything**

```bash
source env.sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all pass. Pay particular attention to `param_impact`, `ctrl_identity` and `wave_formant_sweep`. A moved render hash here is a stop-and-report, not a re-bless (see Global Constraints).

- [ ] **Step 7: Commit**

```bash
git add engine tests CMakeLists.txt host/render/scenario.cpp
git commit -m "$(cat <<'EOF'
feat(engine): P_EDGE and the voice-edge broadcast leave the instrument

The frozen vectors in tests/param_impact_points.h are back at their
pre-2026-08-19 alignment; the inventory marker was proved red first.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 3: SynthEngineT's cell — SYNTH, WAVE and BODY

`SynthEngineT<V>` serves all three, so one cell removal covers SYNTH's and WAVE's output high-pass pair *and* the forwarding path that reaches BODY's exciter. The exciter's own trim goes with it.

**Files:**
- Modify: `engine/synth/synth_engine.h:73-105`, `:141-177`, `:289-300`, `:341-342`, `:370`
- Modify: `engine/synth/synth_engine.cpp:47-56`, `:359-366`, `:428-440`
- Modify: `engine/synth/voice.h:35-38`, `:83-87`
- Modify: `engine/body/body_voice.h:29-32`, `:57-60`
- Modify: `engine/body/body_voice.cpp:154-156`
- Modify: `engine/body/exciter.h:69`, `:82-89`, `:179-184`, `:201-210`, `:219`
- Modify: `tests/test_synth_engine.cpp:137-150`
- Modify: `tests/test_body_voice.cpp:273-325`
- Modify: `tests/test_filt.cpp:105-125`

- [ ] **Step 1: Strip the cell from `SynthEngineT`**

In `engine/synth/synth_engine.h`: delete `kEdgeHpNeutralHz` and `kEdgeHpOctaves` (`:104-105`) with their long comment block (`:73-103`); delete `set_edge()` (`:168-177`) with its comment (`:141-167`); delete the `_edge` member (`:289-293`) and the `OnePoleHp _hp_l, _hp_r;` pair (`:300`) with its comment (`:294-299`); delete `kEdgeUsesOutputHp` from the `BodyVoice` traits block (`:341-342`) and the empty `set_edge` at `:370`. Remove `#include "util/onepole_hp.h"` (`:11`).

In `engine/synth/synth_engine.cpp`: delete the `if constexpr (V::kEdgeUsesOutputHp)` init block (`:53-56`) and its comment (`:47-52`); delete the `vc.set_edge(_edge);` push (`:366`) and its comment (`:359-365`); delete the output-filter block (`:434-440`) and its comment (`:428-433`):

```cpp
    if constexpr (V::kEdgeUsesOutputHp) {
        if (_edge != 0.f) {
            l = _hp_l.process(l);
            r = _hp_r.process(r);
        }
    }
```

In `engine/synth/voice.h`: delete `kEdgeUsesOutputHp` (`:35-38`) and the empty `set_edge` (`:83-87`).

- [ ] **Step 2: Strip BODY's forwarding and the exciter's trim**

In `engine/body/body_voice.h`, delete `set_edge` (`:60`) and its comment (`:57-59`), and trim the class comment at `:29-32` so it no longer says EDGE is real on BodyVoice. In `engine/body/body_voice.cpp`, delete (`:154-156`):

```cpp
void BodyVoice::set_edge(float t) { _exciter.set_edge(t); }
```

In `engine/body/exciter.h`: delete `set_edge` (`:86-89`) with its comment (`:82-85`), delete `kEdgeOctaves` (`:184`) with its comment (`:179-183`), delete `_edge = 0.f;` from the reset at `:69`, drop `_edge` from the member list at `:219`, and delete the trim line in `_recompute_filter` (`:210`) together with its ten-line comment (`:201-209`):

```cpp
        cutoff_hz *= std::pow(2.f, kEdgeOctaves * _edge);
```

The line below it — `const float k = 1.f - std::exp(-kTwoPi * cutoff_hz / _sr);` — now consumes the zone's raw cutoff directly. That is bit-identical to the current behaviour at `_edge == 0`, because `std::pow(2.f, k * 0.f)` is exactly `1.0f`.

- [ ] **Step 3: Delete the three engines' EDGE test cases**

Delete `TEST_CASE("synth: EDGE at 0 is bit-identical to no EDGE at all")` (`tests/test_synth_engine.cpp:137`). Delete all three BODY cases (`tests/test_body_voice.cpp:273`, `:302`, `:313`) — including `"body: EDGE is inert in zone 2, and that is the documented behaviour"`, whose entire purpose was to catch someone inserting a filter into zone 2 *for EDGE's sake*. Delete `TEST_CASE("edge: up removes low end and leaves the top alone")` (`tests/test_filt.cpp:108`).

Check the surrounding file-level comments in each of the three files and remove any that now introduce a section that is gone.

- [ ] **Step 4: Rebuild and run everything**

```bash
source env.sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all pass, render hashes unmoved.

- [ ] **Step 5: Commit**

```bash
git add engine/synth engine/body tests
git commit -m "$(cat <<'EOF'
feat(engine): SYNTH, WAVE and BODY lose their EDGE cells

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 4: SAMPLER and BBD, and `OnePoleHp` itself

Both carry the same one-pole high-pass idiom with their own constants. EDGE is `OnePoleHp`'s only consumer, so the header and its test go too.

**Files:**
- Modify: `engine/sampler/sampler_engine.h:11`, `:249-280`, `:337-350`, `:411`, `:484-486`
- Modify: `engine/sampler/sampler_engine.cpp:112-119`, `:955-967`
- Modify: `engine/sampler/sampler_config.h:328-355`
- Modify: `engine/parts/bbd_engine.h:8`, `:106-127`, `:189-199`, `:283-285`, `:294`
- Modify: `engine/parts/bbd_engine.cpp:95-129`, `:137-148`, `:444-452`, `:520-534`
- Modify: `tests/test_sampler_engine.cpp:3490-3560`
- Modify: `tests/test_bbd_engine.cpp:1520-1600`, `:1738-1760`
- Delete: `engine/util/onepole_hp.h`
- Delete: `tests/test_onepole_hp.cpp`
- Modify: `CMakeLists.txt:172-176`

- [ ] **Step 1: Strip the sampler's cell**

In `engine/sampler/sampler_engine.h`: delete `set_edge` (`:272-280`) with its comment (`:249-271`), the two test accessors `edge_hp_x1_for_test`/`edge_hp_y1_for_test` (`:349-350`) with their comment (`:337-348`), the `OnePoleHp _hp_l, _hp_r;` member (`:411`), the `_edge` member (`:486`) with its comment (`:484-485`), and the `#include "util/onepole_hp.h"` (`:11`).

In `engine/sampler/sampler_engine.cpp`: delete the `_hp_l.init/_hp_r.init` pair (`:118-119`) with its comment (`:112-117`), and delete the process block (`:963-967`) with its comment (`:955-962`):

```cpp
    if (_edge != 0.f) {
        l = _hp_l.process(l);
        r = _hp_r.process(r);
    }
```

In `engine/sampler/sampler_config.h`: delete `kEdgeHpNeutralHz` and `kEdgeOctaves` (`:354-355`) with the comment block above them (`:328-353`).

- [ ] **Step 2: Strip the BBD's cell**

In `engine/parts/bbd_engine.h`: delete the `set_edge` declaration (`:127`) with its comment (`:106-126`), the two test accessors (`:198-199`) with their comment (`:189-197`), the `_edge` member (`:285`) with its comment (`:283-284`), the `OnePoleHp _hp_l, _hp_r;` member (`:294`), and the include (`:8`).

In `engine/parts/bbd_engine.cpp`: delete the anonymous-namespace constants `kEdgeHpNeutralHz` and `kEdgeOctaves` (`:128-129`) with their comment block (`:95-127`); delete the `_hp_l.init(_sr); _hp_r.init(_sr);` pair (`:146-147`) with its comment (`:137-145`); delete `BbdEngine::set_edge` entirely (`:444-452`); delete the `process_in` filter block (`:529-534`) with its comment (`:520-528`).

- [ ] **Step 3: Delete `OnePoleHp` and its test**

```bash
git rm engine/util/onepole_hp.h tests/test_onepole_hp.cpp
```

In `CMakeLists.txt`, delete `tests/test_onepole_hp.cpp` (`:176`) and the four-line comment above it (`:172-175`) that explains which engines include the header. Confirm nothing else references it:

```bash
grep -rn --exclude-dir=build "onepole_hp\|OnePoleHp" engine host bench tests CMakeLists.txt
```

Expected: no hits. `--exclude-dir=build` is required: `bench/build/*.lst` are listing files from an earlier ARM build that quote the removed sources verbatim (`bbd_engine.lst:864` still carries `void BbdEngine::set_edge(float t) {`). They are stale generated output — do not edit them, and do not read a hit there as unfinished work. (`engine/util/onepole.h`, the control-rate smoother, is a different class and stays.)

- [ ] **Step 4: Delete both engines' EDGE test cases**

From `tests/test_sampler_engine.cpp`, delete the three cases at `:3499`, `:3528` and `:3546`, plus the section comment at `:3490-3498`. From `tests/test_bbd_engine.cpp`, delete the four cases at `:1524`, `:1567`, `:1591` and `:1738`, plus the section comment at `:1520-1523`.

Leave `tests/test_bbd_edge_state.cpp` and `host/vcv/src/bbd_edge_state.hpp` alone — despite the name they are about the ENG-switch edge detector, not this knob.

- [ ] **Step 5: Rebuild and run everything**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Reconfigure here rather than only rebuilding: two source files left the build. Expected: all pass, render hashes unmoved.

- [ ] **Step 6: Commit**

```bash
git add -A engine tests CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(engine): SAMPLER and BBD lose their EDGE cells, OnePoleHp goes with them

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 5: FEED — the trim goes, the filter stays

FEED is the one engine whose filter predates EDGE. `feed_cfg::kDampFixedHz` returns to being a fixed corner; only the bipolar trim around it disappears.

**The trap in this task:** `FeedEngine::init` (`:34-40`) does not set the damping coefficient directly. It forces `_edge = -2.f` and `_damp_hz = -1.f` past their own early-out guards and then calls `set_edge(0.f)` to establish `kDampFixedHz` and its coefficient. Delete `set_edge` without replacing that call and FEED boots with an unset damping coefficient.

**Files:**
- Modify: `engine/feed/feed_engine.cpp:30-40`, `:399-428`
- Modify: `engine/feed/feed_engine.h:45-52`
- Modify: `engine/feed/feed_config.h:198-202`
- Modify: `tests/test_feed_engine.cpp:1389-1425`

- [ ] **Step 1: Re-point `init` at `_set_damp_hz` directly**

In `engine/feed/feed_engine.cpp`, replace the forced-guard block (`:38-40`):

```cpp
    _edge    = -2.f;                      // outside [-1, +1] on purpose
    _damp_hz = -1.f;
    set_edge(0.f);
```

with a direct call that keeps the same guard-opening intent:

```cpp
    // _damp_hz is forced to a value the clamp cannot produce so this call is
    // not swallowed by _set_damp_hz's own early-out.
    _damp_hz = -1.f;
    _set_damp_hz(feed_cfg::kDampFixedHz);
```

Trim the comment at `:34-37` accordingly — it currently explains why *two* guards are forced open.

- [ ] **Step 2: Delete `set_edge` and its constant**

In `engine/feed/feed_engine.cpp`, delete `FeedEngine::set_edge` (`:422-428`) and the comment block above it (`:399-421`). In `engine/feed/feed_engine.h`, delete the `set_edge` declaration (`:52`) and its comment (`:47-51`). In `engine/feed/feed_config.h`, delete `kEdgeOctaves` (`:202`) and the comment above it (`:199-201`); leave `kDampFixedHz` (`:197`) and its `BY EAR, first try` note exactly as they are.

Adjust the comment on `_set_damp_hz` in `feed_engine.h:82-86` and `feed_engine.cpp:430-433`, both of which describe it as "kept private behind set_edge" — `init` is now its only caller.

- [ ] **Step 3: Delete FEED's two EDGE cases**

From `tests/test_feed_engine.cpp`, delete `TEST_CASE("feed: EDGE at 0 is exactly kDampFixedHz")` (`:1393`) and `TEST_CASE("feed: EDGE spans kEdgeOctaves either side, symmetrically")` (`:1414`), plus the section comment at `:1389-1392`.

**Keep** `TEST_CASE("feed G28: DAMP is honestly a filter, and its centre is neutral")` at `:1084` — it exercises `set_filt`, not `set_edge`, and must still pass untouched.

- [ ] **Step 4: Add the replacement pin for `init`'s corner**

The two deleted cases were the only thing asserting that a fresh FEED lands on `kDampFixedHz`. Step 1 rewired exactly that path, so it needs a gate of its own. Add to `tests/test_feed_engine.cpp`, where the deleted section was:

```cpp
// init() no longer reaches the damping corner through set_edge (EDGE removed
// 2026-08-20); _set_damp_hz's early-out guard makes a missed call silent, so
// this pins the corner a fresh engine actually boots with.
TEST_CASE("feed: a fresh engine boots on kDampFixedHz") {
    FeedEngine e = fresh_feed();
    CHECK(e.damp_hz_for_test() == doctest::Approx(feed_cfg::kDampFixedHz));
}
```

- [ ] **Step 5: Prove the new pin can go red**

Temporarily comment out the `_set_damp_hz(feed_cfg::kDampFixedHz);` line added in Step 1, then:

```bash
source env.sh
cmake --build build
./build/spky_tests -tc="feed: a fresh engine boots on kDampFixedHz"
```

Expected: **FAIL**, reporting `-1` against 3200. Restore the line and re-run; expected PASS. Record both observations — a pin on an early-out-guarded setter is exactly the shape that passes vacuously if the guard is left closed.

- [ ] **Step 6: Rebuild and run everything**

```bash
source env.sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all pass, render hashes unmoved.

- [ ] **Step 7: Commit**

```bash
git add engine/feed tests/test_feed_engine.cpp
git commit -m "$(cat <<'EOF'
feat(feed): the EDGE trim goes, kDampFixedHz becomes fixed again

init() reaches _set_damp_hz directly now; a new pin covers the corner a
fresh engine boots with, proved red once.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 6: The bench row and the paper trail

Removes the workload built to price EDGE's filters, and brings every status document in line. The bench firmware is not built or run here — that needs the board and is Bastian's call.

**Files:**
- Modify: `bench/workloads_system.cpp:722-745`, `:746-830`, `:859-860`
- Modify: `bench/run.py:343-347`
- Modify: `docs/engine-map.md:1286-1330`, `:1356-1500`
- Modify: `docs/by-ear-decisions.md`
- Modify: `docs/roadmap.md:610-626` and the DPTH/EDGE section around `:600`
- Modify: `docs/gotchas.md:66`
- Modify: `docs/milestone-history.md`

- [ ] **Step 1: Remove the bench workload**

In `bench/workloads_system.cpp`: delete `configure_inst_edge_synth_bbd` (`:746`), `setup_inst_edge_synth_bbd` (`:802`), `proc_inst_edge_synth_bbd` (`:819`), the section comment `// --- 11. EDGE's own filter cost …` (`:722-745`), and the registry entry (`:859-860`):

```cpp
    { "system", "inst_edge_synth_bbd",
      setup_inst_edge_synth_bbd, proc_inst_edge_synth_bbd },
```

In `bench/run.py`, delete `"inst_edge_synth_bbd",` (`:347`) and its four-line comment (`:343-346`).

`bench/test_run_contract.py` needs no change — it never names this row (a `grep` for "edge" there matches "ledger").

- [ ] **Step 2: Verify the bench sources still parse**

The bench builds with the ARM toolchain in a shell that must never have sourced `env.sh`. Do not build it as part of this task. Instead confirm the row is gone from both sides:

```bash
grep -rn --exclude-dir=build "inst_edge_synth_bbd" bench docs/roadmap.md
```

Expected: hits only in whatever roadmap text Step 3 rewrites. `--exclude-dir=build` is not optional: `bench/build/` holds `.lst` listing files from an earlier ARM build that still quote the removed sources verbatim. They are stale generated output, not source — do not edit them, and do not read a hit there as unfinished work. (The same staleness bites the other way round too: the bench build can silently relink a stale object, so a bench row's presence or absence is verified from `bench.map`, never from a build artifact.)

`docs/bench/2026-08-20-*.md` keeps its `inst_edge_synth_bbd` rows untouched — those are historical captures recording a measurement that was really taken on hardware.

- [ ] **Step 3: Rewrite the status docs**

`docs/engine-map.md`: delete the EDGE half of §10 — the `EDGE's neutral, per engine` table and its surrounding prose, the negative-half measurements, and the `BODY's zone-2 blind spot` subsection (`:1407-1500`). Keep the `LANE_MOTION, read six ways` table and everything about DPTH. Rewrite the §9 paragraphs at `:1286-1330` so they describe DPTH as the one knob that arrived on 2026-08-19, and add a line recording that EDGE arrived and was withdrawn on 2026-08-20.

`docs/roadmap.md`: delete the "What the new one-poles actually cost, isolated" item (`:617-626`) — there is nothing left to price. Reduce the listening-pass item (`:611-616`) to the DPTH values that remain: `sampler_cfg::kMotionBaseScale`, BODY's `kDriftDetuneCt = 3.f`, and DPTH's reach above unity on a BBD deck. Delete the EDGE entries elsewhere in the file.

`docs/by-ear-decisions.md`: remove EDGE's first values from the outstanding-listening checklist. Add a short entry recording the closed decision: *EDGE was removed on 2026-08-20 after a listening pass — at neutral 20 Hz ±3 octaves the knob's negative half was inaudible (≤0.08 dB at 55 Hz) and its positive extreme took only ~5 dB off a 110 Hz fundamental. Do not reintroduce it as a wider-span high-pass without a new design pass; the open question was whether EDGE should have been a tilt at the deck output, not whether its span was too small.*

`docs/gotchas.md:66`: the bench-commit reference names `inst_edge_synth_bbd` as the row that separates two captures. Reword it to name the commits without implying the row still exists.

`docs/milestone-history.md`: add one line to the chronology recording the removal.

- [ ] **Step 4: Full verification**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
host/vcv/build-local.sh
```

Expected: everything passes, no render hash moved. Then a final sweep for survivors:

```bash
grep -rn --exclude-dir=build \
  "set_edge\|set_voice_edge\|P_EDGE\|kEdgeOctaves\|kEdgeHpNeutralHz\|kEdgeUsesOutputHp\|DAMP_A\|DAMP_B\|OnePoleHp" \
  engine host bench tests CMakeLists.txt
```

Expected: **no hits**. Any hit is unfinished work from an earlier task. `--exclude-dir=build` again excludes stale ARM listing files under `bench/build/` (see Step 2).

- [ ] **Step 5: Commit**

```bash
git add bench docs
git commit -m "$(cat <<'EOF'
docs: EDGE removed, and the paper trail says why

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

## What this plan deliberately does not do

- **It does not touch DPTH.** The two knobs shipped together; only EDGE is withdrawn.
- **It does not re-flow either panel.** Both knob slots become free space, by decision.
- **It does not revert commits.** The EDGE commits interleave with DPTH commits (`9e15177`, `28e2c7a`, `5de8f0b`), with an unrelated hygiene fix (`f4f073f`), and three later commits (`a31b848`, `e122e6e`, `993f65b`) carry both. A revert chain would conflict repeatedly; hand removal keeps each commit honest.
- **It does not rewrite the 2026-08-19 spec or plan.** Those documents record decisions that were really made. `docs/by-ear-decisions.md` is where the withdrawal is recorded.
- **It does not claim a CPU saving.** All four high-pass engines already skip their filter at `_edge == 0`, FEED and BODY lose no filter at all, and `docs/roadmap.md` states that the isolated cost of the one-poles was never measured. The reason for this removal is that the knob does not earn its place, not that it was expensive.
