# FLOW Melody Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the free (FLOW) mode a melody engine — a slot sequencer without rhythm — so a FLOW deck spans one standing note, through a drone with minimal variation, to a slow melody or chord progression.

**Architecture:** `ModLane`'s melodic lane (`LANE_PITCH`) gains a third state beside STEP and the existing continuous LFO. In that state the cycle is divided into `kFlowPhraseSlots` slots, DENSITY selects k of them through the groove ranking already used by STEP, and the closed slots hold the previous note. `Part` turns the state on for note engines and leaves SAMPLER and BBD on the old LFO. Two rate floors bound the fast end; SMOOTH's slew is clamped against the slot interval.

**Tech Stack:** C++17, clang + Ninja, doctest (vendored in `third_party/`), CMake. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-08-13-flow-melody-engine-design.md` — the authority. Where this plan and the spec disagree, the spec wins; report the disagreement rather than picking one.

**Evidence the spec rests on:** `docs/attic/2026-08-13-glow-macro-audit.md`
(moved there 2026-08-14 when the flow layer and Glow were struck; see
`docs/attic/README.md`).

## Global Constraints

- **Build with `-DCMAKE_BUILD_TYPE=Release`.** Not optional. A fresh Debug configure makes `spky_tests` and `ctrl_identity` fail with "SYNTH reference moved" — the render hashes in `tests/check_render_hash.cmake` are byte-identity gates built from Release. `README.md` omits the flag and is wrong about this.
- **`source env.sh` first** (puts LLVM on PATH, sets `CC`/`CXX`, `CMAKE_GENERATOR=Ninja`). Never MSVC. Never `source env.sh` in a shell used for the Daisy firmware.
- **Everything written into the repo is English** — code, comments, commit messages, docs. (The conversation this came from is German; the files are not.)
- **Commit trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>` — not the default Anthropic one.
- **`engine/` must not include any hardware type.** No libDaisy, no host headers.
- **Every new gate must be shown RED once before it is made green.** A test that cannot fail gets fixed. Each task below names its RED recipe; run it, see the failure, then revert the perturbation.
- **No bit-exactness gates exist in this project** except the two pinned render hashes named in Task 8. Renders are otherwise sanity checks.
- **New constants introduced by this work** (all `static constexpr` on `ModLane`, declared beside `kSeqSlots`):

  | Constant | Value | Task |
  |---|---|---|
  | `kFlowPhraseSlots` | `8` | 2 |
  | `kFlowNoteMinSamples` | `0.060f * _sr` samples | 6 |
  | `kFlowPhraseMinSamples` | `kFlowPhraseSlots * kFlowNoteMinSamples` — **derived, not separately tunable** | 6 |
  | `kFlowSlewFrac` | `0.35f` | 7 |

  `kFlowNoteMinSamples` and `kFlowSlewFrac` are first guesses set by arithmetic, not by ear. Do not re-tune them during implementation; the owner judges them at Task 12.

## Build & test commands

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Single test case while iterating:

```bash
./build/spky_tests --test-case="the exact TEST_CASE name"
```

---

## File Structure

| File | Responsibility | Tasks |
|---|---|---|
| `engine/mod/lane.h` | the `_flow_melody` flag, the two predicates, the four new constants, the two floor counters | 1, 2, 6, 7 |
| `engine/mod/lane.cpp` | `_sh_slot`, `_compute_raw`, `process`, `_effective_length`, `_effective_gate`, `_groove_k`, `_on_boundary`, `_wrap_events`, `set_step`, `set_flow_melody`, `_update_slew`, `tick` | 1–7, 11 |
| `engine/mod/super_modulator.h` | one forwarding setter to `LANE_PITCH` | 8 |
| `engine/parts/part.cpp` | pushes the flag at the two sites where `_engine_id` is written | 8 |
| `engine/flow/flow.cpp` | `M_WANDER` joins `weather_of`'s exclusion list | 9 |
| `tests/test_flow_melody.cpp` | **new** — every behavioural gate for the new state | 1–7 |
| `tests/test_flow_melody_wiring.cpp` | **new** — `Part`/`Instrument`-level gates and the non-regression renders | 8 |
| `tests/test_lane_tick.cpp` | the `tick()` ≡ `process()` equivalence case | 11 |
| `tests/test_song_lane.cpp`, `tests/test_rhythm_ring.cpp`, `tests/test_gate_density.cpp` | retitled to name the path they pin, plus melody-mode siblings | 10 |
| `tests/test_param_impact.cpp` | the audit gates, corrected | 10 |
| `tests/test_flow_runtime.cpp` | the WANDER weather gate | 9 |
| `host/render/scenarios/flow_melody.json` | **new** — the demo scenario | 10 |
| `CMakeLists.txt` | registers the two new test files | 1, 8 |
| `docs/roadmap.md`, `docs/2026-08-13-glow-macro-audit.md`, `host/vcv/README.md` | documentation deliverables | 12 |

**Ordering note that differs from the spec.** Spec §12 expects the pinned render hashes to move in two commits. They move in **exactly one** — Task 8 — because this plan puts every engine change before the `Part` wiring, and with `_flow_melody` defaulting to `false` nothing is audible until that push lands. Tasks 1–7 change no render at all.

---

## Task 1: The slot walk

Introduces the flag, the two predicates, and the three functions that make a FLOW melodic lane emit held notes instead of a continuous LFO.

**Files:**
- Modify: `engine/mod/lane.h` (add the flag, the predicates, the setter declaration)
- Modify: `engine/mod/lane.cpp:418-429` (`_compute_raw`, `_sh_slot`), `:613-620` (`process`'s mode branch), plus the new setter
- Create: `tests/test_flow_melody.cpp`
- Modify: `CMakeLists.txt` (register the new test file)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `void ModLane::set_flow_melody(bool on)` — public. Later tasks add lines to its body; the signature is final.
  - `bool ModLane::_flow_melody_on() const` — private. True iff `_melodic && !_step_mode && _flow_melody`.
  - `bool ModLane::_melody_engine_on() const` — private. True iff `_melodic && (_step_mode || _flow_melody)`. Unused until Task 4; declared here so the taxonomy lands in one place.

- [ ] **Step 1: Write the failing test**

Create `tests/test_flow_melody.cpp`:

```cpp
// tests/test_flow_melody.cpp
//
// The FLOW melody engine (spec 2026-08-13 flow-melody-engine-design). In the
// free mode the melodic lane stops being a continuous LFO and becomes a slot
// sequencer without rhythm: one cycle is one phrase pass, DENSITY selects k of
// L slots through the groove ranking, and the slots it skips HOLD the previous
// note.
#include <doctest/doctest.h>
#include "mod/lane.h"

using namespace spky;

namespace {

// A melodic lane in FLOW melody mode: 8 slots per cycle, one cycle per second.
//
// 1 Hz is deliberate, not arbitrary. At 48 kHz a cycle is 48000 samples and a
// slot 6000 (125 ms), comfortably above the 60 ms note-rate floor Task 6 adds,
// so every case in this file keeps its meaning once that floor exists. A faster
// lane would start colliding with the floor and the fire counts below would
// silently become "whatever the floor allows".
//
// set_step() and set_form()/set_song() run BEFORE init(), mirroring
// tests/test_song_lane.cpp: init() generates the phrase and reads that state.
ModLane make_flow_melody_lane(uint32_t seed, float hz = 1.f, int steps = 8) {
    ModLane lane;
    lane.set_melodic(true);
    lane.set_step(false, steps);
    lane.set_form(Principle::Hierarchical);
    lane.set_song(SongMode::AAAB);
    lane.init(48000.f, seed);
    lane.set_flow_melody(true);
    lane.set_rate_hz(hz);
    lane.set_density(1.f);
    lane.set_variation(0.f);
    lane.set_smooth(0.f);
    return lane;
}

// Safety bound for every drive loop here: 4 M samples is ~83 s at 48 kHz, far
// past any cycle this file configures.
constexpr int kDriveGuard = 4000000;

void drive_to_wrap(ModLane& lane) {
    for (int i = 0; i < kDriveGuard; ++i) {
        lane.process();
        if (lane.wrapped()) return;
    }
    FAIL("lane did not wrap within the safety bound");
}

// Count fires over `cycles` whole cycles, starting from a wrap.
//
// Starting on a wrap is what makes the count exact: each cycle contains exactly
// one entry per slot, so a window that runs wrap-to-wrap has no partial slot at
// either end and the answer is (fires per cycle) * cycles.
int fires_over_cycles(ModLane& lane, int cycles) {
    drive_to_wrap(lane);
    int fires = 0, wraps = 0;
    for (int i = 0; i < kDriveGuard && wraps < cycles; ++i) {
        lane.process();
        if (lane.fired()) ++fires;
        if (lane.wrapped()) ++wraps;
    }
    REQUIRE(wraps == cycles);
    return fires;
}

} // namespace

TEST_CASE("FLOW melody: the lane fires once per slot, not once per cycle") {
    ModLane lane = make_flow_melody_lane(0xF10Wu);
    // Two cycles so the count cannot be satisfied by a single accidental edge.
    CHECK(fires_over_cycles(lane, 2) == 16);
}

TEST_CASE("FLOW melody: the emitted value is the phrase's note, held") {
    ModLane lane = make_flow_melody_lane(0xF10Wu);
    drive_to_wrap(lane);

    // Walk one cycle and check that at every sample the pre-slew target is the
    // active pattern's pitch for the slot the phase is in. This is the whole
    // contract of the new state in one assertion: the value comes from the
    // phrase, and it does not move between boundaries.
    const uint8_t active = lane.active_pattern();
    const MelodyPattern& pattern = lane.pattern_for_test(active);
    for (int i = 0; i < 48000; ++i) {
        lane.process();
        if (lane.wrapped()) break;
        const int slot = ModLane::step_index(lane.phase(), 8);
        CHECK(lane.target() == doctest::Approx(pattern.pitch[slot]));
    }
}

TEST_CASE("FLOW LFO mode is untouched when the flag is off") {
    ModLane lane = make_flow_melody_lane(0xF10Wu);
    lane.set_flow_melody(false);
    // The legacy free lane fires exactly once per cycle, at the wrap.
    CHECK(fires_over_cycles(lane, 3) == 3);
}
```

Register it in `CMakeLists.txt` beside the other lane tests (after `tests/test_lane_tick.cpp` on line 56):

```cmake
    tests/test_flow_melody.cpp
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/spky_tests --test-case="FLOW melody: the lane fires once per slot, not once per cycle"
```

Expected: **compile error**, `'set_flow_melody' is not a member of 'spky::ModLane'`. That is the failure for this step; the behavioural RED comes in Step 4.

- [ ] **Step 3: Add the flag and the predicates**

In `engine/mod/lane.h`, in the public section beside `set_melodic`:

```cpp
    // FLOW melody mode (spec 2026-08-13 flow-melody-engine). Off by default:
    // the boot value is the legacy continuous-LFO behaviour and the new state
    // has to be asked for, so a missing push from Part is a silent revert to
    // the old sound rather than a silent adoption of the new one. Part drives
    // this from the engine id -- SAMPLER and BBD keep the LFO, because on those
    // decks the PITCH lane is not a note.
    void set_flow_melody(bool on);
```

In the private section, beside the other helpers:

```cpp
    // "this lane is running the FLOW melody engine right now"
    bool _flow_melody_on() const { return _melodic && !_step_mode && _flow_melody; }
    // "this lane runs the melody system at all" (STEP or FLOW melody). NOT
    // named melody(): it would sit next to set_melodic/_melodic and read as a
    // getter for the flag, which is the naming collision spotykach-gotchas
    // records for set_depth.
    bool _melody_engine_on() const { return _melodic && (_step_mode || _flow_melody); }
```

And with the other bools, beside `_melodic`:

```cpp
    bool      _flow_melody = false;
```

- [ ] **Step 4: Implement the three functions**

In `engine/mod/lane.cpp`, add the setter beside `set_step`:

```cpp
void ModLane::set_flow_melody(bool on) {
    _flow_melody = on;
}
```

Replace `_compute_raw` (`lane.cpp:418-423`):

```cpp
float ModLane::_compute_raw() const {
    // FLOW melody mode emits the phrase's note directly. This is arithmetically
    // shape_value(ph, 1.f, pitch[slot]) -- at shape 1 the clamp in
    // waveforms.h:26 forces i == 3, f == 1 and the return at :32 is sh_hold
    // exactly -- but it is written out rather than pinned, so SHAPE's inertness
    // on this lane is visible here instead of implied by an argument. What
    // SHAPE should mean for a melody is the SHAPE/SMOOTH rework's question.
    if (_flow_melody_on()) return _active_pattern().pitch[_sh_slot()];
    const double phd = _phase + double(_ev_phase);
    float ph = static_cast<float>(phd - std::floor(phd));
    float sh = clampf(_shape + _ev_shape + _shape_offset + _kick_shape, 0.f, 1.f);
    return shape_value(ph, sh, _active_pattern().pitch[_sh_slot()]);
}
```

Replace `_sh_slot` (`lane.cpp:425-429`):

```cpp
int ModLane::_sh_slot() const {
    // FLOW LFO: one slot, loop-stable per cycle. STEP and FLOW melody both walk
    // the phrase. Getting this wrong is silent: leave it returning 0 in melody
    // mode and the lane emits pitch[0] forever, and because
    // expand_pattern_groove pins rank_of_slot[0] to 0 the gate is then always
    // open too, so DENSITY moves nothing and the whole mechanism is a no-op.
    if (!_step_mode && !_flow_melody) return 0;
    int s = _cur_step < 0 ? 0 : _cur_step;
    return s % kSeqSlots;
}
```

Replace `process()`'s mode branch (`lane.cpp:613-620`):

```cpp
    if (_step_mode) {
        const int step = shuffle_step_index(
            static_cast<float>(_phase), _steps, _shuffle_latched);
        if (step != _cur_step) _enter_step(step);
    } else if (_flow_melody_on()) {
        // One cycle is one phrase pass -- the same relation STEP already has.
        // A STRAIGHT grid, not shuffle_step_index: SHUFFLE is a rhythmic
        // control and stays out of FLOW, and tests/test_instrument.cpp:653
        // pins a FLOW deck bit-exact under a live shared SHUFFLE turn.
        const int slot = step_index(static_cast<float>(_phase),
                                    _effective_length());
        if (slot != _cur_step) { _cur_step = slot; _on_boundary(); }
        // No per-sample recompute: _target holds between boundaries.
    } else {
        if (wrapped) _on_boundary();
        if (!_frozen) _target = _compute_raw();     // continuous in FLOW
    }
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests --test-case="FLOW melody*"
ctest --test-dir build --output-on-failure
```

Expected: the three new cases PASS, and the whole suite is green — Tasks 1–7 must not move an existing test, because `_flow_melody` defaults to `false` and nothing pushes it yet.

- [ ] **Step 6: Prove the RED**

Temporarily change `_sh_slot`'s first line back to `if (!_step_mode) return 0;`, rebuild, and run:

```bash
./build/spky_tests --test-case="FLOW melody: the emitted value is the phrase's note, held"
```

Expected: **FAIL** — the target is stuck at `pitch[0]` while the slot index walks. Revert the perturbation and confirm green again. This is the exact defect two reviewers found in draft 1 of the spec; the gate exists to make it impossible to reintroduce.

- [ ] **Step 7: Commit**

```bash
git add engine/mod/lane.h engine/mod/lane.cpp tests/test_flow_melody.cpp CMakeLists.txt
git commit -m "feat(mod): FLOW melodic lane walks phrase slots instead of an LFO

_sh_slot() is the fix, not process(): leaving it at 0 in melody mode makes
the lane emit pitch[0] forever, and because expand_pattern_groove pins
rank_of_slot[0] to 0 the gate is then always open too, so the mechanism
would have been a silent no-op.

Off by default -- the boot value is the legacy behaviour, so nothing moves
until Part pushes the flag.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 2: The phrase length

Makes `_effective_length()` — which is currently dead code with zero callers — the single source of truth, fixed at `kFlowPhraseSlots` in melody mode.

**Files:**
- Modify: `engine/mod/lane.h` (add `kFlowPhraseSlots`)
- Modify: `engine/mod/lane.cpp:189-192` (`_effective_length`), `:196-206` (`_generate_pattern_a`, `_derive_pattern_b`), `:143-147` (`set_step`), `:488-489` (`_start_note`), plus `set_flow_melody`
- Test: `tests/test_flow_melody.cpp`

**Interfaces:**
- Consumes: `_flow_melody_on()` from Task 1.
- Produces: `static constexpr int ModLane::kFlowPhraseSlots = 8;` and `int ModLane::_effective_length() const` as the only place a phrase length is computed.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_melody.cpp`:

```cpp
TEST_CASE("FLOW melody: the phrase length is a constant, not STEPS") {
    // A deck whose STEPS says 3 still gets an 8-slot FLOW phrase. STEPS means
    // different things on the two hosts in FLOW -- Fireflow spends STEPS == 0
    // on the mode switch itself, which set_step clamps to 1, while Glow pushes
    // 2..16 in both modes -- so the free mode owns its own length and both
    // hosts produce the same phrase from the same terrain.
    ModLane lane = make_flow_melody_lane(0xF10Wu, 1.f, /*steps=*/3);
    CHECK(fires_over_cycles(lane, 2) == 16);
}

TEST_CASE("STEP is unaffected by the phrase-length constant") {
    // The same lane in STEP still follows STEPS exactly. _effective_length()
    // must not leak into the stepped world.
    ModLane lane;
    lane.set_melodic(true);
    lane.set_step(true, 3);
    lane.set_form(Principle::Hierarchical);
    lane.set_song(SongMode::AAAB);
    lane.init(48000.f, 0xF10Wu);
    lane.set_rate_hz(1.f);
    lane.set_density(1.f);
    lane.set_variation(0.f);
    CHECK(lane.steps() == 3);
    // A STEP lane's clock_scale is 8/steps, so one cycle is still one phrase:
    // three slot entries, three fires at full density.
    CHECK(fires_over_cycles(lane, 2) == 6);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build && ./build/spky_tests --test-case="FLOW melody: the phrase length is a constant, not STEPS"
```

Expected: **FAIL**, `16 == 6` — the lane is walking `_steps` (3) slots per cycle because `_effective_length()` still returns `_steps`. The STEP case passes already; it is the guard that the fix does not overshoot.

- [ ] **Step 3: Add the constant**

In `engine/mod/lane.h`, beside `kSeqSlots`:

```cpp
    // The free mode owns its phrase length. It cannot come from STEPS: Fireflow
    // spends STEPS == 0 on the mode switch (Fireflow.cpp:892-893) and set_step
    // clamps that to 1, while Glow pushes 2..16 in both modes -- so STEPS would
    // mean two different things. It also cannot be made variable: generate_phrase
    // fills only [0, n) (phrase_gen.h:165-200) and pitch[32] is zero-init, so a
    // length that grows past the generated one plays the root instead of a note.
    static constexpr int kFlowPhraseSlots = 8;
```

- [ ] **Step 4: Make `_effective_length()` mode-aware and route every call site**

Replace `_effective_length` (`lane.cpp:189-192`):

```cpp
int ModLane::_effective_length() const {
    if (_flow_melody_on()) return kFlowPhraseSlots;
    int n = _steps < 1 ? 1 : _steps;
    return n > kSeqSlots ? kSeqSlots : n;
}
```

In `_generate_pattern_a` (`lane.cpp:194-202`), replace both uses of `_steps`:

```cpp
void ModLane::_generate_pattern_a() {
    MelodyPattern& pattern = _song.patterns[0];
    const int len = _effective_length();
    generate_phrase(_song.selected_form, _rng, len,
                    pattern.pitch, pattern.gate, pattern.motif_id,
                    pattern.layout);
    pg_gen_groove(_rng, pattern.layout.motif_len, pattern.cell_groove);
    expand_pattern_groove(
        pattern.cell_groove, len, pattern.pattern_groove);
}
```

In `_derive_pattern_b` (`lane.cpp:204-207`):

```cpp
void ModLane::_derive_pattern_b() {
    derive_turnaround(_song.patterns[0], _effective_length(), _rng,
                      _song.patterns[1],
                      _song.cadence_slot, _song.bound_a_opening);
}
```

In `_start_note` (`lane.cpp:488-489`), replace the inline expression:

```cpp
    int n = _effective_length();                        // effective phrase length
    if (n < 1) n = 1;
```

In `set_step` (`lane.cpp:141-147`), replace the inline `old_n`/`new_n` computation. **The ordering matters**, because the helper now reads state that `set_step` itself mutates:

```cpp
    // Captured BEFORE the assignments below: _effective_length() reads
    // _step_mode and _steps, both of which this function is about to change.
    const int old_len = _melodic ? _effective_length() : 0;
    int new_steps = steps < 1 ? 1 : steps;
```

then, after `_step_mode = on;` and `_steps = new_steps;` (`lane.cpp:169-170`) and before `_update_inc()`:

```cpp
    // Only when the EFFECTIVE length changes. Keep the _melodic guard: the
    // helper clamps to kSeqSlots, and lane_slots() can hand a texture lane up
    // to 64 (tests/test_lane_len.cpp:26), so an unguarded check would start
    // clamping lengths that today pass through.
    if (_melodic && _effective_length() != old_len)
        _song.length_pending = true;
```

Delete the old `if (_melodic) { int old_n = ...; }` block that this replaces.

Finally, extend `set_flow_melody`:

```cpp
void ModLane::set_flow_melody(bool on) {
    _flow_melody = on;
    // One-sided by construction, and that is the point. ENTERING melody mode the
    // pattern may have been generated at another length -- at boot, whenever a
    // host pushed a STEPS other than kFlowPhraseSlots before this flag -- and
    // pitch[] past that length is zero, so it MUST regenerate. LEAVING it must
    // not: the lane moves to the FLOW LFO path where _wrap_events early-returns
    // and nothing reads the melody state, so a flag raised here would sit until
    // the deck came back and then re-roll the melody for nothing. Testing the
    // pattern rather than the flip gives both behaviours from one condition.
    if (_flow_melody_on() &&
        _active_pattern().pattern_groove.len != _effective_length())
        _song.length_pending = true;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests
ctest --test-dir build --output-on-failure
```

Expected: all green. If `test_lane_len.cpp` or `test_step_clock.cpp` moved, the `_melodic` guard in `set_step` was dropped — put it back.

- [ ] **Step 6: Prove the RED**

Change `_effective_length()`'s first line to `if (false)`, rebuild, run the phrase-length case. Expected: **FAIL** with `16 == 6`. Revert.

- [ ] **Step 7: Commit**

```bash
git add engine/mod/lane.h engine/mod/lane.cpp tests/test_flow_melody.cpp
git commit -m "feat(mod): the free mode owns its phrase length

_effective_length() was dead code with zero callers; it becomes the single
place a phrase length is computed and returns kFlowPhraseSlots in melody
mode. STEPS cannot serve: it means the mode switch on Fireflow and a real
count on Glow, and a variable length would play ungenerated slots, because
generate_phrase fills only [0, n) while pitch[32] is zero-init.

set_step's check moves to the helper and captures the old length BEFORE its
own assignments -- the helper reads the state set_step mutates.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 3: DENSITY reaches the free mode

Wires the groove gate into melody mode and deletes two branches that are now provably unreachable.

**Files:**
- Modify: `engine/mod/lane.cpp:431-458` (`_groove_k`, `_effective_gate`), `:460-476` (`_on_boundary`), `:492-505` (`_start_note`)
- Test: `tests/test_flow_melody.cpp`

**Interfaces:**
- Consumes: `_flow_melody_on()` (Task 1), `_effective_length()` (Task 2).
- Produces: `_effective_gate(int slot)` returning the `pattern_groove` decision for **every** melodic lane, in both modes.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_melody.cpp`:

```cpp
TEST_CASE("FLOW melody: DENSITY selects how many notes the drone uses") {
    SUBCASE("k == 1 is one standing note") {
        ModLane lane = make_flow_melody_lane(0xF10Wu);
        lane.set_density(0.f);           // lround(0 * L) == 0, clamped to 1
        // Exactly one fire per cycle, and the value never moves between them.
        drive_to_wrap(lane);
        const float held = lane.target();
        int fires = 0, wraps = 0;
        for (int i = 0; i < kDriveGuard && wraps < 3; ++i) {
            lane.process();
            if (lane.fired()) ++fires;
            if (lane.wrapped()) ++wraps;
            CHECK(lane.target() == doctest::Approx(held));
        }
        CHECK(fires == 3);
        // The lane really ran -- constancy alone would also pass on a stalled
        // phase accumulator, which is the vacuity the PACE spec recorded.
        CHECK(lane.wrap_count_for_test() >= 3u);
    }

    SUBCASE("k == L is a note per slot") {
        ModLane lane = make_flow_melody_lane(0xF10Wu);
        lane.set_density(1.f);
        CHECK(fires_over_cycles(lane, 2) == 16);
    }

    SUBCASE("a closed slot holds the previous note") {
        ModLane lane = make_flow_melody_lane(0xF10Wu);
        lane.set_density(0.5f);          // k == 4 of 8
        drive_to_wrap(lane);
        float last_fired = lane.target();
        int fires = 0;
        for (int i = 0; i < 48000; ++i) {
            lane.process();
            if (lane.wrapped()) break;
            if (lane.fired()) { last_fired = lane.target(); ++fires; }
            // Between fires the target must equal the last fired value, never
            // a fresh one: the skipped slots HOLD, they do not rest.
            CHECK(lane.target() == doctest::Approx(last_fired));
        }
        CHECK(fires == 4);
    }
}

TEST_CASE("FLOW melody: the gate is value-dependent, not merely executed") {
    // Two lanes identical but for DENSITY must emit different sequences. A
    // counter proving the branch ran would be shape 4 in the vacuous-gates
    // taxonomy -- it asserts the code executed, not that it did anything.
    ModLane sparse = make_flow_melody_lane(0xF10Wu);
    ModLane dense  = make_flow_melody_lane(0xF10Wu);
    sparse.set_density(0.f);
    dense.set_density(1.f);
    bool differed = false;
    for (int i = 0; i < 48000; ++i) {
        sparse.process();
        dense.process();
        if (sparse.target() != dense.target()) differed = true;
    }
    CHECK(differed);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build && ./build/spky_tests --test-case="FLOW melody: DENSITY selects how many notes the drone uses"
```

Expected: **FAIL** — every subcase reports 8 fires per cycle regardless of DENSITY, because `_on_boundary` still hard-codes `gated = true` off `_step_mode`.

- [ ] **Step 3: Wire the gate**

Replace `_groove_k` (`lane.cpp:431-441`):

```cpp
int ModLane::_groove_k() const {
    const MelodyPattern& pattern = _active_pattern();
    // Melodic lanes rank over the phrase-length expansion in BOTH modes. The
    // cell_groove arm this used to have was the motif length, which would only
    // ever have played the first motif instance -- FORM's A A B A arrangement
    // would stay inaudible, which is the audit finding this work exists to
    // close.
    int L = _melodic ? static_cast<int>(pattern.pattern_groove.len)
                     : static_cast<int>(pattern.cell_groove.len);
    if (L < 1) L = 1;
    int k = static_cast<int>(std::lround(_density * static_cast<float>(L)));
    if (k < 1) k = 1;              // the anchor is unmaskable
    if (k > L) k = L;
    return k;
}
```

Replace `_effective_gate` (`lane.cpp:443-458`):

```cpp
bool ModLane::_effective_gate(int slot) const {
    const MelodyPattern& pattern = _active_pattern();
    if (!_melodic)
        return pattern.gate[slot];   // non-melodic lanes: all-true, DENSE unrouted
    const int groove_length =
        pattern.pattern_groove.len < 1 ? 1 : pattern.pattern_groove.len;
    return pattern.pattern_groove.rank_of_slot[slot % groove_length] <
           _groove_k();
}
```

In `_on_boundary` (`lane.cpp:460-476`), change the gate expression and add the `_step_mode` guard on the rest branch:

```cpp
void ModLane::_on_boundary() {
    int slot = _sh_slot();
    // Both melody modes consult the groove rank against DENSE. The FLOW LFO
    // has no per-step gate and always fires (no freeze source after
    // PROBABILITY).
    bool gated = (_step_mode || _flow_melody_on()) ? _effective_gate(slot) : true;
    _frozen = !gated;
    if (gated) {
        _fired = true;
        if (_melodic && _step_mode) _start_note(slot);   // rhythm: STEP only
        if (_variation > 0.f && (!_melodic || _step_mode || _flow_melody))
            _mutate_slot(slot);  // GROW pitch
        _target = _compute_raw();
    } else if (_step_mode) {
        ++_note_age;   // rest step: the running note ages toward its release
                       // -- a STEP concept. Unreachable in FLOW before this
                       // change (gated was unconditionally true there), so the
                       // guard is bit-identical for every existing path.
    }
    // if !gated: hold the previous _target (frozen) -- and the buffer slot with it
}
```

In `_start_note` (`lane.cpp:492-505`), collapse the dead else branch:

```cpp
    const MelodyPattern& pattern = _active_pattern();
    // _start_note is called only from _on_boundary under _melodic && _step_mode,
    // so the cell_groove arm this used to carry was unreachable.
    const int groove_length =
        pattern.pattern_groove.len < 1 ? 1 : pattern.pattern_groove.len;
    int hold = static_cast<int>(
        pattern.pattern_groove.note_len[slot % groove_length]);
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests
ctest --test-dir build --output-on-failure
```

Expected: all green, including `test_gate_density.cpp` and `test_step.cpp` — the STEP path is unchanged because `_melodic && _step_mode` selected `pattern_groove` there already.

- [ ] **Step 5: Prove the RED twice**

First for the new behaviour: change `_on_boundary`'s gate expression back to `_step_mode ? _effective_gate(slot) : true`, rebuild, run the DENSITY case. Expected **FAIL**. Revert.

Second for the deletion — this is the one that justifies removing code. Restore `_groove_k`'s old ternary (`(_melodic && _step_mode) ? pattern_groove : cell_groove`), rebuild, and run:

```bash
./build/spky_tests --test-case="FLOW melody: DENSITY selects how many notes the drone uses"
```

Expected: **FAIL** — with the motif-cell length the fire counts do not match the phrase. Revert. If it *passes*, stop and report: the deletion's justification does not hold and the spec's §4.5 needs revisiting before this task lands.

- [ ] **Step 6: Commit**

```bash
git add engine/mod/lane.cpp tests/test_flow_melody.cpp
git commit -m "feat(mod): DENSITY reaches the free mode

The gate now ranks over pattern_groove for every melodic lane in both modes.
The dead cell_groove arms in _groove_k and _start_note go with it: both were
unreachable because every caller of _effective_gate sits under _step_mode,
and a raster on the motif cell would only ever have played the first motif
instance, leaving FORM's arrangement inaudible.

_on_boundary's rest branch gains a _step_mode guard. It was unreachable in
FLOW before this commit, so that is bit-identical for existing paths and it
keeps a STEP-only counter from creeping on a free deck.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 4: FORM, SONG and VARIATION reach the free mode

Removes the early return that made the whole melody system inert in FLOW.

**Files:**
- Modify: `engine/mod/lane.cpp:552-586` (`_evolve_outgoing_pattern`, `_wrap_events`), `:248-253` (`_apply_preroll_work`), `:545-550` (`_mutate_groove`)
- Test: `tests/test_flow_melody.cpp`

**Interfaces:**
- Consumes: `_melody_engine_on()` (Task 1).
- Produces: nothing new; existing observers `song_position()`, `active_pattern()`, `form()`, `pattern_for_test()` become meaningful in FLOW.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_melody.cpp`:

```cpp
#include <cstring>

TEST_CASE("FLOW melody: SONG advances one phrase per cycle") {
    ModLane lane = make_flow_melody_lane(0xF10Wu);
    drive_to_wrap(lane);
    const uint32_t start = lane.song_position();
    int wraps = 0;
    for (int i = 0; i < kDriveGuard && wraps < 3; ++i) {
        lane.process();
        if (lane.wrapped()) ++wraps;
    }
    CHECK(lane.song_position() == start + 3u);
}

TEST_CASE("FLOW melody: a FORM change reaches the phrase") {
    ModLane lane = make_flow_melody_lane(0xF10Wu);
    drive_to_wrap(lane);
    MelodyPattern before = lane.pattern_for_test(0);

    lane.set_form(Principle::CallResponse);
    int wraps = 0;
    for (int i = 0; i < kDriveGuard && wraps < 1; ++i) {
        lane.process();
        if (lane.wrapped()) ++wraps;
    }
    CHECK(lane.form() == Principle::CallResponse);
    CHECK(std::memcmp(&before, &lane.pattern_for_test(0), sizeof(before)) != 0);
}

TEST_CASE("FLOW melody: VARIATION moves the standing note two ways") {
    // At k == 1 the open slot is rank 0. VARIATION walks its pitch
    // (_mutate_slot) AND swaps groove ranks (mutate_pattern_groove), so the
    // open slot itself migrates and starts reading a different pitch[] entry.
    // Both are wanted; a gate on the pitch walk alone would miss half of it.
    SUBCASE("VARIATION 0 is loop-stable") {
        ModLane lane = make_flow_melody_lane(0xF10Wu);
        lane.set_density(0.f);
        lane.set_variation(0.f);
        drive_to_wrap(lane);
        const MelodyPattern before = lane.pattern_for_test(lane.active_pattern());
        int wraps = 0;
        for (int i = 0; i < kDriveGuard && wraps < 4; ++i) {
            lane.process();
            if (lane.wrapped()) ++wraps;
        }
        const MelodyPattern after = lane.pattern_for_test(lane.active_pattern());
        CHECK(std::memcmp(&before, &after, sizeof(before)) == 0);
    }

    SUBCASE("VARIATION 1 moves both pitch and groove") {
        ModLane lane = make_flow_melody_lane(0xF10Wu);
        lane.set_density(0.f);
        lane.set_variation(1.f);
        drive_to_wrap(lane);
        const MelodyPattern before = lane.pattern_for_test(lane.active_pattern());
        int wraps = 0;
        for (int i = 0; i < kDriveGuard && wraps < 8; ++i) {
            lane.process();
            if (lane.wrapped()) ++wraps;
        }
        const MelodyPattern after = lane.pattern_for_test(lane.active_pattern());
        bool pitch_moved = false, groove_moved = false;
        for (int s = 0; s < 32; ++s) {
            if (before.pitch[s] != after.pitch[s]) pitch_moved = true;
            if (before.pattern_groove.rank_of_slot[s] !=
                after.pattern_groove.rank_of_slot[s]) groove_moved = true;
        }
        CHECK(pitch_moved);
        CHECK(groove_moved);
    }
}

TEST_CASE("FLOW melody: pending work applies before the first slot") {
    // A lane whose STEPS disagrees with kFlowPhraseSlots has its phrase
    // generated at the wrong length by init(); set_flow_melody raises
    // length_pending and _apply_preroll_work must consume it on the first
    // process() call, while _cur_step is still -1. Otherwise the phrase would
    // stay wrong until the next wrap -- up to 50 s at kRateFreeMin.
    ModLane lane = make_flow_melody_lane(0xF10Wu, 1.f, /*steps=*/3);
    lane.process();
    CHECK(lane.pattern_for_test(lane.active_pattern()).pattern_groove.len == 8);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build && ./build/spky_tests --test-case="FLOW melody: SONG advances one phrase per cycle"
```

Expected: **FAIL**, `song_position()` never moves — `_wrap_events` returns early for the melodic lane.

- [ ] **Step 3: Widen the four guards**

Replace `_wrap_events` (`lane.cpp:571-586`):

```cpp
void ModLane::_wrap_events() {
    const bool pending =
        _song.form_pending ||
        _song.song_pending ||
        _song.new_pending ||
        _song.length_pending;

    // The FLOW LFO keeps its old contract: no evolution, no song advancement.
    if (_melodic && !_step_mode && !_flow_melody)
        return;

    _evolve_outgoing_pattern();
    if (_melody_engine_on()) {
        if (pending) _apply_pending_song_work();
        else         _advance_song();
    }
}
```

In `_evolve_outgoing_pattern` (`lane.cpp:559`), widen the RENEW guard:

```cpp
        if (_melody_engine_on()) _renew_units();
```

In `_mutate_groove` (`lane.cpp:545-546`), widen the early return:

```cpp
void ModLane::_mutate_groove(bool renew_side) {
    if (!_melody_engine_on()) return;
```

In `_apply_preroll_work` (`lane.cpp:248-253`), widen the guard:

```cpp
void ModLane::_apply_preroll_work() {
    if (_melody_engine_on() && _cur_step < 0 &&
        (_song.form_pending || _song.song_pending ||
         _song.new_pending || _song.length_pending))
        _apply_pending_song_work();
}
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests
ctest --test-dir build --output-on-failure
```

Expected: all green. `test_song_lane.cpp:193` and `:324` still pass — they drive a bare `ModLane` where `_flow_melody` is `false`, so they exercise the FLOW LFO path this change deliberately leaves alone. Task 10 retitles them to say so.

- [ ] **Step 5: Prove the RED**

Restore `_wrap_events`'s original early return (`if (_melodic && !_step_mode) return;`), rebuild, run the SONG case. Expected **FAIL**. Revert.

- [ ] **Step 6: Commit**

```bash
git add engine/mod/lane.cpp tests/test_flow_melody.cpp
git commit -m "feat(mod): FORM, SONG and VARIATION reach the free mode

_wrap_events' early return narrows to the FLOW LFO state, and the three
guards inside it widen with it. FORM and SONG measured dead on 40 of 40
terrains in the macro audit; this is the mechanism.

_apply_preroll_work widens too, which is what makes a phrase generated at
the wrong length correct itself before the first slot rather than at the
next wrap -- up to 50 s away at kRateFreeMin.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 5: Mode entry and reset

Clears the two pieces of state that were harmless while FLOW had no slots.

**Files:**
- Modify: `engine/mod/lane.cpp:134-140` (`set_step`), `:402-416` (`reset`), plus `set_flow_melody`
- Test: `tests/test_flow_melody.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1–4.
- Produces: the invariant that entering any mode leaves `_cur_step == -1` and `_frozen == false`.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_melody.cpp`:

```cpp
TEST_CASE("FLOW melody: mode entry does not carry stale slot state") {
    // Two hazards, both invisible before FLOW had slots:
    //  - a _cur_step left from the other mode means no boundary fires until the
    //    index happens to differ, so the phrase's first note is skipped or late;
    //  - a _frozen left true from a CLOSED slot follows the lane into the FLOW
    //    LFO path, whose branch is `if (!_frozen) _target = _compute_raw()` --
    //    the "LFO" would then hold a constant for up to a full cycle.
    ModLane lane = make_flow_melody_lane(0xF10Wu);
    lane.set_density(0.25f);          // k == 2 of 8: most slots are closed
    drive_to_wrap(lane);
    for (int i = 0; i < 20000; ++i) lane.process();   // land mid-phrase

    lane.set_step(true, 8);
    // The first sample in the new mode must fire, not wait for an index change.
    lane.process();
    CHECK(lane.fired());

    lane.set_step(false, 8);
    lane.set_flow_melody(false);
    // Back on the LFO path the lane must move again immediately.
    const float first = lane.target();
    for (int i = 0; i < 200; ++i) lane.process();
    CHECK(lane.target() != doctest::Approx(first));
}

TEST_CASE("FLOW melody: RST restarts the phrase at slot 0") {
    ModLane lane = make_flow_melody_lane(0xF10Wu);
    drive_to_wrap(lane);
    for (int i = 0; i < 20000; ++i) lane.process();

    lane.reset();
    lane.process();
    CHECK(lane.fired());
    CHECK(lane.target() ==
          doctest::Approx(lane.pattern_for_test(lane.active_pattern()).pitch[0]));
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build && ./build/spky_tests --test-case="FLOW melody: mode entry does not carry stale slot state"
```

Expected: **FAIL** — the first sample after `set_step(true, 8)` does not fire, because `_cur_step` still holds the FLOW slot index and the STEP index happens to match.

- [ ] **Step 3: Clear the state**

In `set_step` (`lane.cpp:134-140`), extend the entry block so it runs on entry to **either** mode:

```cpp
void ModLane::set_step(bool on, int steps) {
    const bool entering_step = on && !_step_mode;
    const bool mode_changed  = on != _step_mode;
    if (entering_step) _shuffle_latched = _shuffle_target;
    if (entering_step) { _note_age = 0; _note_hold = 0; }  // STEP entry: no stale sustain
    // Either direction: a slot index and a freeze decision from the other mode
    // mean nothing in this one. _cur_step = -1 makes the first sample fire slot
    // 0, the same way init and reset do.
    if (mode_changed) { _cur_step = -1; _frozen = false; }
    // Entering STEP disarms the follower so its first follow() call lands on
    // the deck's current position instead of replaying the whole count.
    if (entering_step) { _follow_armed = false; _follow_jumped = false; }
```

In `reset` (`lane.cpp:402-416`), add the freeze clear beside the existing `_cur_step = -1`:

```cpp
    _cur_step = -1;
    _frozen = false;
```

In `set_flow_melody`, add the freeze clear as the second line:

```cpp
void ModLane::set_flow_melody(bool on) {
    _flow_melody = on;
    _frozen = false;          // see set_step: a freeze from the other state is meaningless here
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests
ctest --test-dir build --output-on-failure
```

Expected: all green. Watch `test_step_clock.cpp` and `test_step_grid_lock.cpp` in particular — they exercise live STEPS turns, and `mode_changed` must be false there so nothing is cleared.

- [ ] **Step 5: Prove the RED**

Remove the `if (mode_changed)` line, rebuild, run the mode-entry case. Expected **FAIL**. Revert.

- [ ] **Step 6: Commit**

```bash
git add engine/mod/lane.cpp tests/test_flow_melody.cpp
git commit -m "fix(mod): clear slot and freeze state on every mode change

_cur_step and _frozen were harmless while FLOW had no slots. Now a stale
_cur_step skips the phrase's first note, and a _frozen carried from a closed
slot follows the lane onto the LFO path, whose branch is
'if (!_frozen) _target = _compute_raw()' -- the LFO would hold a constant
for up to a full cycle.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 6: The note and phrase floors

Bounds the fast end. Without this, `k × cycle rate` reaches over 100 fires per second on a non-drone FLOW terrain.

**Files:**
- Modify: `engine/mod/lane.h` (two constants, two counters), `engine/mod/lane.cpp` (`init`, `reset`, `set_step`, `set_flow_melody`, `_on_boundary`, `_wrap_events`, `process`)
- Test: `tests/test_flow_melody.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1–5.
- Produces: `int ModLane::_since_fire` and `int ModLane::_since_phrase`, both primed (not zeroed) at `init`/`reset`/`set_step`/`set_flow_melody`; Task 7 reads `kFlowNoteMinSamples`.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_melody.cpp`:

```cpp
TEST_CASE("FLOW melody: the note rate has a floor") {
    // A non-drone archetype reaches P_RATE_A {.55,.9} in FLOW, about 14 Hz.
    // At k == L == 8 that is over 100 fires a second, where Part's 5 ms gate
    // never expires so the envelope never re-attacks, and _chord.build()'s lay
    // search runs per note.
    ModLane lane = make_flow_melody_lane(0xF10Wu, /*hz=*/14.f);
    lane.set_density(1.f);
    int fires = 0;
    for (int i = 0; i < 48000; ++i) {       // one second
        lane.process();
        if (lane.fired()) ++fires;
    }
    const int ceiling = static_cast<int>(48000.f / (0.060f * 48000.f)) + 1;
    CHECK(fires <= ceiling);                // ~16/s, not ~112/s
    CHECK(fires > 0);                       // it decimates, it does not stop
}

TEST_CASE("FLOW melody: the phrase rate has a floor") {
    // The note floor bounds fires, not wraps. Without a second floor,
    // _advance_song() would run once per cycle at 14 Hz -- fourteen phrases a
    // second, and a pending FORM would drive generate_phrase at that rate.
    ModLane lane = make_flow_melody_lane(0xF10Wu, /*hz=*/14.f);
    drive_to_wrap(lane);
    const uint32_t start = lane.song_position();
    for (int i = 0; i < 48000; ++i) lane.process();
    const uint32_t ceiling =
        static_cast<uint32_t>(48000.f / (8 * 0.060f * 48000.f)) + 1u;
    CHECK(lane.song_position() - start <= ceiling);   // ~2/s, not 14/s
}

TEST_CASE("FLOW melody: the floor never swallows the first note") {
    // Primed, not zeroed. Left at 0 the counters would suppress the very first
    // note at boot, after RST, and on the first slot after mode entry.
    ModLane lane = make_flow_melody_lane(0xF10Wu);
    lane.process();
    CHECK(lane.fired());

    drive_to_wrap(lane);
    for (int i = 0; i < 20000; ++i) lane.process();
    lane.reset();
    lane.process();
    CHECK(lane.fired());
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build && ./build/spky_tests --test-case="FLOW melody: the note rate has a floor"
```

Expected: **FAIL**, roughly 112 fires against a ceiling of 17.

- [ ] **Step 3: Add the constants and counters**

In `engine/mod/lane.h`, beside `kFlowPhraseSlots`:

```cpp
    // Note-rate floor. A boundary arriving sooner than this after the last fire
    // HOLDS instead of firing, so the rate decimates to the floor rather than
    // falling off a cliff, and DENSITY keeps its full effect everywhere below
    // the ceiling. 60 ms is ~16 notes/s -- above anything ambient, below
    // anything that reads as a buzz. A FIRST GUESS SET BY ARITHMETIC, not by
    // ear; the owner judges it against flow_melody.json.
    static constexpr float kFlowNoteMinS = 0.060f;
```

and, with the other ints:

```cpp
    // Samples since the last fire / the last phrase event, for the two floors.
    // Primed rather than zeroed at init/reset/mode entry -- at 0 the floor
    // would swallow the first note of every phrase start, including RST's.
    int _since_fire   = 0;
    int _since_phrase = 0;
    int _note_min_samples   = 0;   // kFlowNoteMinS * _sr, cached at init
    int _phrase_min_samples = 0;   // kFlowPhraseSlots * _note_min_samples
```

- [ ] **Step 4: Wire the floors**

In `ModLane::init`, after `_sr` is set, add:

```cpp
    _note_min_samples   = static_cast<int>(kFlowNoteMinS * _sr);
    _phrase_min_samples = kFlowPhraseSlots * _note_min_samples;
    _since_fire   = _note_min_samples;      // primed: the first boundary fires
    _since_phrase = _phrase_min_samples;
```

Add a small private helper beside the predicates in `lane.h`:

```cpp
    void _prime_floors() { _since_fire = _note_min_samples;
                           _since_phrase = _phrase_min_samples; }
```

and call `_prime_floors();` from `reset()`, from `set_step()` inside the `mode_changed` block, and from `set_flow_melody()`.

In `process()`, advance both counters next to the existing per-sample work (immediately after `_apply_preroll_work();`):

```cpp
    if (_since_fire   < _note_min_samples)   ++_since_fire;
    if (_since_phrase < _phrase_min_samples) ++_since_phrase;
```

The clamped increment is deliberate: the counters are only ever compared against their own thresholds, so letting them saturate there costs nothing and cannot overflow on a lane that runs for days.

In `_on_boundary`, apply the note floor after the gate decision:

```cpp
    bool gated = (_step_mode || _flow_melody_on()) ? _effective_gate(slot) : true;
    if (gated && _flow_melody_on() && _since_fire < _note_min_samples)
        gated = false;                       // note-rate floor
    _frozen = !gated;
    if (gated) {
        _fired = true;
        _since_fire = 0;
```

In `_wrap_events`, apply the phrase floor right after the LFO early return:

```cpp
    if (_melodic && !_step_mode && !_flow_melody)
        return;
    if (_flow_melody_on()) {
        if (_since_phrase < _phrase_min_samples) return;
        _since_phrase = 0;
    }
    _evolve_outgoing_pattern();
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests
ctest --test-dir build --output-on-failure
```

Expected: all green, including every earlier case in `test_flow_melody.cpp` — the 1 Hz helper puts a slot at 125 ms, twice the floor.

- [ ] **Step 6: Prove the RED twice**

Remove the `_since_fire` check in `_on_boundary`, rebuild, run the note-floor case → **FAIL**. Revert.
Remove the `_since_phrase` block in `_wrap_events`, rebuild, run the phrase-floor case → **FAIL**. Revert.
Then change `_prime_floors()` to set both to 0, rebuild, run the first-note case → **FAIL**. Revert.

- [ ] **Step 7: Commit**

```bash
git add engine/mod/lane.h engine/mod/lane.cpp tests/test_flow_melody.cpp
git commit -m "feat(mod): floor the note and phrase rates in FLOW melody mode

k x cycle rate had no upper bound. A non-drone FLOW terrain reaches ~14 Hz,
and at k = L = 8 that is over 100 fires a second -- past the point where
Part's 5 ms gate expires, so the envelope never re-attacks.

Two floors, one tunable: the phrase floor is derived as kFlowPhraseSlots x
the note floor, because bounding fires alone would still let _advance_song
run fourteen times a second with generate_phrase behind it.

Both counters are primed, not zeroed, at init/reset/mode entry -- at 0 the
floor swallows the first note of every phrase start, RST's included.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 7: SMOOTH against the slot interval

`_target` is now a staircase, so SMOOTH is the glide law of a melody. At SMOOTH 1 its time constant reaches ~0.5 s and shorter notes never arrive.

**Files:**
- Modify: `engine/mod/lane.h` (`kFlowSlewFrac`), `engine/mod/lane.cpp:277-292` (`_update_slew`), `:181-183` (`_update_inc`), plus `set_flow_melody`
- Test: `tests/test_flow_melody.cpp`

**Interfaces:**
- Consumes: `_effective_length()` (Task 2), `_note_min_samples` (Task 6).
- Produces: nothing new.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_melody.cpp`:

```cpp
TEST_CASE("FLOW melody: a note arrives within its own slot at full SMOOTH") {
    // With _range at its default 1.0, apply_range is the identity
    // (range.h:18, lerpf(uni, v, 1) == v), so process()'s return value is the
    // post-slew value and can be compared against target() directly.
    ModLane lane = make_flow_melody_lane(0xF10Wu);
    lane.set_density(1.f);
    lane.set_smooth(1.f);              // ~0.5 s unclamped; a slot is 125 ms
    drive_to_wrap(lane);

    // Find a boundary where the note actually moves, then give it one slot.
    float out = 0.f;
    bool checked = false;
    for (int i = 0; i < 48000 * 3 && !checked; ++i) {
        out = lane.process();
        if (!lane.fired()) continue;
        const float goal = lane.target();
        const float gap  = goal - out;
        if (std::fabs(gap) < 0.05f) continue;      // too small to measure
        for (int s = 0; s < 6000; ++s) out = lane.process();   // one slot
        CHECK(std::fabs(goal - out) <= 0.10f * std::fabs(gap));
        checked = true;
    }
    CHECK(checked);
}

TEST_CASE("STEP's slew is unchanged by the melody clamp") {
    ModLane step_lane;
    step_lane.set_melodic(true);
    step_lane.set_step(true, 8);
    step_lane.init(48000.f, 0xF10Wu);
    step_lane.set_rate_hz(1.f);
    step_lane.set_smooth(1.f);
    // A STEP lane at SMOOTH 1 keeps the long glide: after 6000 samples it must
    // still be well short of its target, which is what the clamp must not do
    // to it.
    float out = 0.f;
    for (int i = 0; i < 200; ++i) out = step_lane.process();
    const float early = out;
    for (int i = 0; i < 6000; ++i) out = step_lane.process();
    CHECK(std::fabs(out - early) < 1.0f);
}
```

Add `#include <cmath>` at the top of the file if not already present.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build && ./build/spky_tests --test-case="FLOW melody: a note arrives within its own slot at full SMOOTH"
```

Expected: **FAIL** — after a full slot the output has covered only a fraction of the gap, because the slew time is ~0.5 s against a 125 ms slot.

- [ ] **Step 3: Add the constant**

In `engine/mod/lane.h`, beside `kFlowNoteMinS`:

```cpp
    // Melody-mode slew ceiling, as a fraction of the slot interval: a note
    // reaches 1 - e^(-1/0.35) ~= 94 % of its target inside its own slot. This
    // is the MINIMUM needed for a melody to be heard as notes rather than a
    // wobble; everything else about SMOOTH belongs to the SHAPE/SMOOTH rework.
    // A FIRST GUESS SET BY ARITHMETIC, not by ear.
    static constexpr float kFlowSlewFrac = 0.35f;
```

- [ ] **Step 4: Clamp the slew**

Replace the first two lines of `_update_slew` (`lane.cpp:277-280`):

```cpp
void ModLane::_update_slew() {
    // smooth 0 -> ~1 sample (near passthrough), smooth 1 -> ~0.5 s.
    float t = _fixed_slew ? 0.02f : (0.00002f * std::pow(25000.f, _smooth));
    if (_flow_melody_on()) {
        // Clamp against the interval the notes ACTUALLY have, not the raw slot:
        // where the note floor decimates, the raw slot is far shorter than the
        // notes are, and clamping to it would make the glide much tighter than
        // anything needs. Guard _phase_inc == 0 the way step_samples() does --
        // in double it yields inf and the clamp goes inert, which is benign but
        // silent, and a silent inert guard is the shape this project fixes.
        const double denom = _phase_inc * (1.0 + double(_ev_rate))
                           * double(_effective_length());
        if (denom > 0.0) {
            const double slot_samples = 1.0 / denom;
            const double effective = slot_samples > double(_note_min_samples)
                                   ? slot_samples : double(_note_min_samples);
            // OnePole::init takes SECONDS (onepole.h:14, k = 1/(time_s*sr)),
            // so the sample count has to be divided by _sr. Without this the
            // clamp never binds.
            const float cap =
                static_cast<float>(double(kFlowSlewFrac) * effective / double(_sr));
            if (t > cap) t = cap;
        }
    }
    _slew.init(_sr, t);
```

The rest of the function (the tick twin's `k` derivation) is unchanged and now
inherits the clamped `t` automatically.

In `_update_inc` (`lane.cpp:181-183`), call the slew update, because the slot
interval moves with the rate and with PACE:

```cpp
void ModLane::_update_inc() {
    _phase_inc = (double(_rate_hz) / double(_sr)) * double(clock_scale());
    _update_slew();     // the melody clamp is a function of the slot interval
}
```

In `set_flow_melody`, add `_update_slew();` as the last line — entering or leaving
melody mode changes both `_flow_melody_on()` and `_effective_length()`.

`_ev_rate` is deliberately **not** a recompute trigger: re-deriving the slew at
every wrap for a ±20 % term inside a 0.35 safety factor buys nothing, and the value
is refreshed at the next rate change anyway. Note this in a comment above the
`denom` line.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests
ctest --test-dir build --output-on-failure
```

Expected: all green. `test_lane_tick.cpp` matters here — the tick twin's coefficient is derived from the same `t`, so if that suite reddens the clamp was applied after `_slew.init` instead of before.

- [ ] **Step 6: Prove the RED**

Drop the `/ double(_sr)` from the `cap` expression, rebuild, and run the arrival case. Expected: **FAIL** — the clamp no longer binds, which is exactly the unit error the spec's second draft carried. Revert.

- [ ] **Step 7: Commit**

```bash
git add engine/mod/lane.h engine/mod/lane.cpp tests/test_flow_melody.cpp
git commit -m "feat(mod): clamp the melody-mode slew against the slot interval

_target is a staircase now, so SMOOTH is a melody's glide law rather than an
LFO's smoothing. At SMOOTH 1 the time constant reaches ~0.5 s and notes
shorter than that never arrive.

OnePole::init takes seconds, not samples, so the cap divides by _sr -- the
formula this replaces would never have bound. It clamps against the interval
the note floor actually produces, not the raw slot, and guards _phase_inc == 0
the way step_samples() does.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 8: Wire it to the decks

The commit that makes any of the previous seven audible. Also the only commit that moves the pinned render hashes.

**Files:**
- Modify: `engine/mod/super_modulator.h` (one forwarding setter)
- Modify: `engine/parts/part.cpp:32` (`Part::init`), `:417` (`Part::_engine_swap`)
- Create: `tests/test_flow_melody_wiring.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/check_render_hash.cmake` (re-based hashes)

**Interfaces:**
- Consumes: `ModLane::set_flow_melody(bool)` (Task 1).
- Produces: `void SuperModulator::set_flow_melody(bool on)` — forwards to `LANE_PITCH` only.

- [ ] **Step 1: Write the failing test**

Create `tests/test_flow_melody_wiring.cpp`:

```cpp
// tests/test_flow_melody_wiring.cpp
//
// Part decides whether a PITCH lane is a note at all. SYNTH, WAVE, BODY, ZAP
// and TEST_TONE get the FLOW melody engine; SAMPLER and BBD keep the
// continuous LFO, because on those decks the PITCH lane is a read position and
// a clock bend rather than a note.
#include <doctest/doctest.h>
#include "instrument.h"

using namespace spky;

namespace {

// A single deck in FLOW at a slow rate, with the neighbour muted so nothing
// downstream of the lane can colour the observation.
void configure_flow_deck(Instrument& inst, int part, EngineId engine) {
    inst.set_engine(part, engine);
    inst.set_step(part, false, 8);
    inst.set_rate(part, 0.f);          // free_hz floor, ~0.02 Hz
    inst.set_density(part, 1.f);
    inst.set_variation(part, 0.f);
    inst.set_smooth(part, 0.f);
}

} // namespace

TEST_CASE("a SYNTH deck in FLOW runs the melody engine") {
    Instrument inst;
    inst.init(48000.f);
    configure_flow_deck(inst, 0, ENGINE_SYNTH);
    inst.set_density(0, 1.f);

    // The pitch lane fires more than once per cycle -- the observable
    // difference between a slot sequencer and the old one-fire-per-wrap LFO.
    int fires = 0;
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 48000 * 30; ++i) {
        inst.process(&l, &r, 1);
        if (inst.lane_fired(0, LANE_PITCH)) ++fires;
    }
    CHECK(fires > 1);
}

TEST_CASE("a SAMPLER deck in FLOW keeps the continuous LFO") {
    Instrument inst;
    inst.init(48000.f);
    configure_flow_deck(inst, 0, ENGINE_SAMPLER);

    // The pitch target must keep moving every sample, not hold between slots.
    float l = 0.f, r = 0.f;
    inst.process(&l, &r, 1);
    const float a = inst.pitch_cv(0);
    for (int i = 0; i < 2000; ++i) inst.process(&l, &r, 1);
    const float b = inst.pitch_cv(0);
    CHECK(a != doctest::Approx(b));
}

TEST_CASE("a BBD deck in FLOW keeps the continuous LFO") {
    Instrument inst;
    inst.init(48000.f);
    configure_flow_deck(inst, 0, ENGINE_BBD);

    float l = 0.f, r = 0.f;
    inst.process(&l, &r, 1);
    const float a = inst.pitch_cv(0);
    for (int i = 0; i < 2000; ++i) inst.process(&l, &r, 1);
    const float b = inst.pitch_cv(0);
    CHECK(a != doctest::Approx(b));
}

TEST_CASE("an engine swap in FLOW leaves the phrase length consistent") {
    // SYNTH -> Sampler -> SYNTH. The check is at the first wrap AFTER the swap
    // back, not "at every point": while the deck is a Sampler the lane is on
    // the LFO path where the melody state is legitimately not maintained.
    Instrument inst;
    inst.init(48000.f);
    configure_flow_deck(inst, 0, ENGINE_SYNTH);
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 4800; ++i) inst.process(&l, &r, 1);

    inst.set_engine(0, ENGINE_SAMPLER);
    for (int i = 0; i < 4800; ++i) inst.process(&l, &r, 1);
    inst.set_engine(0, ENGINE_SYNTH);
    for (int i = 0; i < 4800; ++i) inst.process(&l, &r, 1);

    // No crash, no silence, and the deck still fires: the groove must match the
    // effective length or the rank lookup would be reading another length's map.
    int fires = 0;
    for (int i = 0; i < 48000 * 30; ++i) {
        inst.process(&l, &r, 1);
        if (inst.lane_fired(0, LANE_PITCH)) ++fires;
    }
    CHECK(fires > 1);
}
```

Register it in `CMakeLists.txt` beside `tests/test_part.cpp`:

```cmake
    tests/test_flow_melody_wiring.cpp
```

**Before writing the implementation**, confirm the observer names used above exist with these exact signatures: `Instrument::lane_fired(int part, int slot)`, `Instrument::pitch_cv(int part)`, `Instrument::set_density/set_variation/set_smooth/set_rate(int part, float)`, and the `EngineId` enumerators. Grep `engine/instrument.h`. If a name differs, use the real one and note the correction in the commit message — do not invent an accessor.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cmake --build build && ./build/spky_tests --test-case="a SYNTH deck in FLOW runs the melody engine"
```

Expected: **FAIL**, `fires` is 1 or 0 — nothing pushes the flag yet.

- [ ] **Step 3: Add the forwarding setter**

In `engine/mod/super_modulator.h`, beside `set_density`:

```cpp
    // PITCH lane only, exactly like set_density: the texture lanes have no
    // melody and no phrase.
    void set_flow_melody(bool on) { _lanes[LANE_PITCH].set_flow_melody(on); }
```

- [ ] **Step 4: Push it from `Part`**

In `engine/parts/part.cpp`, in `Part::init` immediately after `_engine_id` is assigned (`:32`) and after `_mod.init(...)` has run, and again in `Part::_engine_swap` immediately after `_engine_id` is assigned (`:417`), add:

```cpp
    // On a SAMPLER deck the PITCH lane is a read position, and on a BBD deck in
    // FLOW it is the continuous clock bend -- on both, the sweep IS the feature
    // and a slot sequencer would make it a staircase. Pushed at the two sites
    // where _engine_id is written, the same convention _engine_wants_in
    // follows, so the two cannot drift apart.
    //
    // This names the same two engines as the quantizer bypass in
    // _control_tick (SAMPLER || (BBD && !_step_on)) for the same underlying
    // reason -- on those decks the PITCH lane is not a note -- but it does NOT
    // derive from it. Do not move this to that per-tick site.
    _mod.set_flow_melody(_engine_id != ENGINE_SAMPLER && _engine_id != ENGINE_BBD);
```

Order inside `Part::init` matters: `_mod.init(...)` must already have run, so the
push lands on an initialised lane and its length check (Task 2) can see the
generated pattern.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests
```

Expected: the four new cases pass. **`ctest` will now fail on `ctrl_identity` and the WAVE render hash** — that is the next step, not a defect.

- [ ] **Step 6: Re-base the two pinned render hashes**

Both pinned scenarios run note engines in FLOW, so both move: `ctrl_identity.json` sets no engine and no `set_step`, i.e. **two SYNTH decks in FLOW**; `wave_formant_sweep.json` puts WAVE on part 0 with `set_step` flag `false`.

```bash
cmake --build build
./build/render.exe host/render/scenarios/ctrl_identity.json /tmp/ctrl.wav /tmp/ctrl.csv
./build/render.exe host/render/scenarios/wave_formant_sweep.json /tmp/wave.wav /tmp/wave.csv
```

Take the new hashes the same way `tests/check_render_hash.cmake` computes them (read that file for the exact command — do not guess the algorithm) and update the expected values in `CMakeLists.txt:221-230` and `:237-252`.

**Listen to both WAVs before committing the new hashes.** A hash re-base is where a
regression hides: the gate stops complaining either way. Confirm the renders are
musical and that the FLOW decks now step through notes rather than glide.

```bash
ctest --test-dir build --output-on-failure
```

Expected: all green.

- [ ] **Step 7: Verify the non-regression claim by hand**

The Sampler/BBD bit-identity claim cannot be a unit test: it is a comparison against
`main`. Render a **single-deck** scenario on both trees and compare byte for byte.
Single-deck matters — the reverb is instrument-level with per-part sends and the
master limiter is a shared threshold, so in a two-deck render the neighbour's new
trigger density moves the Sampler deck's contribution to the mix even though its
lane is untouched.

```bash
# on this branch
./build/render.exe host/render/scenarios/<single-deck sampler scenario> /tmp/after.wav /tmp/after.csv
git stash && cmake --build build
./build/render.exe host/render/scenarios/<same scenario> /tmp/before.wav /tmp/before.csv
git stash pop && cmake --build build
cmp /tmp/before.wav /tmp/after.wav && echo "bit-identical"
```

If `host/render/scenarios/` has no single-deck Sampler scenario, write one as part of
this task rather than weakening the comparison. Expected: **bit-identical**. If it is
not, stop and report — the guarantee in spec §7 does not hold and the cause must be
found before this task lands.

- [ ] **Step 8: Commit**

```bash
git add engine/mod/super_modulator.h engine/parts/part.cpp \
        tests/test_flow_melody_wiring.cpp CMakeLists.txt
git commit -m "feat(parts): give note engines the FLOW melody engine

Part pushes the flag from the engine id at the two sites where _engine_id is
written, the convention _engine_wants_in already follows. SAMPLER and BBD
keep the continuous LFO: there the PITCH lane is a read position and a clock
bend, and the sweep is the feature.

This is the commit that makes Tasks 1-7 audible, and the only one that moves
the two pinned render hashes -- ctrl_identity runs two SYNTH decks in FLOW
and wave_formant_sweep a WAVE deck. Both WAVs were listened to before the
re-base, and a single-deck Sampler render was confirmed bit-identical to main.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 9: WANDER leaves the weather layer

Makes VARIATION 0 exactly 0, which is what the standing note needs.

**Files:**
- Modify: `engine/flow/flow.cpp:312` (`weather_of`'s exclusion list)
- Test: `tests/test_flow_runtime.cpp`

**Interfaces:** none — self-contained in the flow layer.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_runtime.cpp`:

```cpp
TEST_CASE("WANDER at zero stays exactly zero under weather") {
    // The standing note needs _variation == 0 EXACTLY. taste.h:927 gives
    // P_VARIATION_A a bp0 cell of {0,0}, so WANDER at 0 draws zero -- but the
    // weather offsets _eff[M_WANDER] by up to kWeatherDepthMax (0.10) scaled by
    // MOTION, and at eff 0.10 the curve interpolates 40 % of the way to bp1,
    // whose span is {.05,.15}. On a terrain that drew the top of that span
    // P_VARIATION_A reaches ~0.06 and the mutations run at every wrap.
    Instrument inst;
    inst.init(48000.f);
    Flow f = make(inst, 0x5EEDu);
    f.set_macro(M_WANDER, 0.f);
    f.set_macro(M_MOTION, 1.f);        // the weather's own depth control

    // A full weather period, so the sweep covers the offset's whole excursion.
    const int ticks = static_cast<int>(kWeatherPeriodMaxS * 100.f);
    for (int i = 0; i < ticks; ++i) {
        f.tick();
        REQUIRE(f.param_now(P_VARIATION_A) == 0.f);
        REQUIRE(f.param_now(P_VARIATION_B) == 0.f);
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build && ./build/spky_tests --test-case="WANDER at zero stays exactly zero under weather"
```

Expected: **FAIL** — a nonzero `P_VARIATION_A` somewhere in the sweep. If it passes on this seed, try two or three other seeds; the leak depends on the terrain's bp1 draw. If no seed reddens it, stop and report rather than adding a test that cannot fail.

- [ ] **Step 3: Add `M_WANDER` to the exclusion**

In `engine/flow/flow.cpp:312`:

```cpp
        // M_WANDER joins them (spec 2026-08-13 flow-melody-engine §9): the FLOW
        // melody engine's standing note needs VARIATION at exactly 0, and the
        // sentence above about M_PACE -- that the exclusion is what keeps its
        // eff exactly at rest when nothing moves it -- is now true of WANDER
        // too. The cost is stated plainly: WANDER no longer breathes on its
        // own, and a terrain moves it only when the player does.
        if (m == M_MOTION || m == M_PACE || m == M_WANDER) continue;
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests
ctest --test-dir build --output-on-failure
```

Expected: all green. `test_flow_runtime.cpp`'s existing weather cases may assert
that *some* macro moves — check that none of them names WANDER specifically; if one
does, retarget it to a macro that still has weather.

- [ ] **Step 5: Prove the RED**

Remove `|| m == M_WANDER`, rebuild, run the new case → **FAIL**. Revert.

- [ ] **Step 6: Commit**

```bash
git add engine/flow/flow.cpp tests/test_flow_runtime.cpp
git commit -m "fix(flow): WANDER leaves the weather layer

The standing note needs VARIATION at exactly 0. taste.h gives P_VARIATION_A a
bp0 cell of {0,0}, so the knob at 0 draws zero -- but weather offsets eff by
up to 0.10, which the story curve turns into ~0.06 on a terrain that drew bp1
high, and the mutations then run at every wrap.

M_WANDER joins M_MOTION and M_PACE in weather_of's exclusion, the site that
exists for exactly this and whose comment already carries the reasoning. The
cost: WANDER no longer breathes on its own.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 10: Correct the inherited tests and add the demo scenario

Three existing tests now document a path production no longer takes, and the audit's own gates need correcting.

**Files:**
- Modify: `tests/test_song_lane.cpp:193`, `:324`; `tests/test_rhythm_ring.cpp:144`; `tests/test_gate_density.cpp:59`
- Modify: `tests/test_param_impact.cpp:96-109`, `:309-312`
- Create: `host/render/scenarios/flow_melody.json`

**Interfaces:** none.

- [ ] **Step 1: Retitle the three FLOW-LFO tests and give each a melody sibling**

These three stay **green** — they drive a bare `ModLane`/`SuperModulator` where
`_flow_melody` is `false` — but their titles now claim more than they pin. Rename
each to name the path, and add a sibling that pins the new one:

- `tests/test_song_lane.cpp:193` `"FLOW pauses SONG position and both snapshots"`
  → `"FLOW LFO pauses SONG position and both snapshots"`, plus a sibling asserting
  that with `set_flow_melody(true)` the position **advances**.
- `tests/test_song_lane.cpp:324` `"song LOOP playback consumes no RNG before future GROW"`
  → prefix `"FLOW LFO: "`, plus a melody-mode sibling.
- `tests/test_rhythm_ring.cpp:144` `"FLOW lanes fill the ring from cycle wraps"`
  → `"FLOW LFO lanes fill the ring from cycle wraps"`, plus a sibling asserting the
  ring's gaps shorten with DENSITY in melody mode.
- `tests/test_gate_density.cpp:59` `"FLOW never freezes after PROBABILITY removal"`:
  its stated contract at `:64` ("FLOW: no per-step gate ⇒ no freeze source") is now
  false for melody mode. Re-scope the title and the comment to the LFO path. **Do
  not leave it green by the accident of `_density` defaulting to 1** — make the
  path it pins explicit in the setup.

- [ ] **Step 2: Correct `test_param_impact.cpp`**

Two edits, and the first is the one draft 1 of the spec got wrong:

1. `P_FORM_A/B` and `P_SONG_A/B` are **excluded** from both gates at `:107-109`
   (the reason is documented at `:96-105`). Nothing there can "invert" — the
   exclusion has to be **deleted** for a FORM/SONG gate to exist at all.
2. The mode-exclusive **exact-set** comparison at `:309-312` lists
   `P_DENSITY_A, P_DENSITY_B, P_STEPS_A, P_STEPS_B, P_SHUFFLE, P_TEMPO_BPM,
   P_COUPLE`. `P_DENSITY_A/B` revive here and must be removed from the expected
   set; `P_STEPS_A/B` **stay**, because the phrase length is a constant (Task 2).

**Decided in advance, so a red gate is not weakened after the fact:** the audit
records a *second, untraced* gate on FORM/SONG — forcing `SHAPE_A` to 1.0 made FORM
audible on only 1 of 6 STEP terrains. The likeliest suspect is downstream of the
lane: `P_DEPTH_A` multiplies every lane output before its destination
(`part.cpp:105`) and `_active[LANE_PITCH]` gates it too, so a terrain can draw the
melody to inaudibility whatever the lane does. **Control for DEPTH and `_active` in
the new FORM/SONG gate.** If FORM/SONG still measure zero with those held, that is
an out-of-scope finding to record in the audit document — **not** a failure of this
work, and not a reason to weaken the gate.

- [ ] **Step 3: Write the demo scenario**

Create `host/render/scenarios/flow_melody.json`: one SYNTH deck in FLOW at a slow
rate, with DENSITY swept 0 → 1 across the render so a single WAV walks the whole
continuum from standing note to melody. Model it on an existing scenario's shape —
read `host/render/scenarios/ctrl_identity.json` for the schema. `set_density` is a
real scenario verb (`host/render/scenario.cpp:142`), so it cannot silently no-op the
way an unknown action would (`:220` ignores those); confirm the render's `mods.csv`
shows the pitch lane stepping.

- [ ] **Step 4: Run everything**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
./build/render.exe host/render/scenarios/flow_melody.json /tmp/flow_melody.wav /tmp/flow_melody.csv
```

Expected: all green, and a WAV whose pitch lane holds one note at the start and
steps through a phrase at the end.

- [ ] **Step 5: Commit**

```bash
git add tests/ host/render/scenarios/flow_melody.json
git commit -m "test(flow): retitle the FLOW-LFO gates and correct the audit gates

Three tests kept passing because a bare ModLane defaults to the LFO path, but
their titles claimed the free mode in general. Each now names the path it
pins and gains a melody-mode sibling.

test_param_impact EXCLUDES FORM/SONG from both gates, so nothing there could
invert -- the exclusion is deleted instead. DENSITY leaves the mode-exclusive
set; STEPS stays, because the FLOW phrase length is a constant.

The FORM/SONG gate controls for DEPTH and _active up front: the audit records
a second untraced gate on those parameters, and deciding in advance what a
still-zero result means is what keeps the gate from being weakened later.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 11: `tick()`

Last, and deliberately separable. `SuperModulator` drives `LANE_PITCH` through
`process()` exclusively (`super_modulator.cpp:109`; the `tick()`/`follow()` loop
skips it at `:165-166`), so **nothing in production takes this path.** It is here
because `tick()`'s documented contract is that it mirrors `process()`'s observable
sequence, and `tests/test_lane_tick.cpp` exercises it directly.

**Files:**
- Modify: `engine/mod/lane.cpp:646-755` (`tick`)
- Test: `tests/test_lane_tick.cpp`

**Interfaces:** none new.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_lane_tick.cpp` a case that drives two identically-seeded
melody-mode lanes — one through `kTickInterval` calls to `process()`, one through a
single `tick()` — and asserts the same observable sequence: same fire count, same
final `target()`, same `song_position()`. Follow the existing equivalence cases in
that file for the exact harness shape; its `make_*` helper only configures STEP
melodic lanes today, so a melody-mode helper has to be added.

- [ ] **Step 2: Run it to verify it fails**

```bash
cmake --build build && ./build/spky_tests --test-case="*tick*melody*"
```

Expected: **FAIL** — `tick()` still runs the FLOW LFO arithmetic.

- [ ] **Step 3: Implement the slot walk in `tick()`**

Four sites, all in `lane.cpp`:

1. **The pending-mismatch entry** (`:684-688`): the `if (_step_mode)` block gains a
   melody-mode arm that computes the slot with `step_index(_phase, _effective_length())`
   and calls `_on_boundary()` when it differs from `_cur_step`.
2. **`next_edge`** (`:699-702`): the `: 1.0` arm is the FLOW LFO's "the only edge is
   the wrap". In melody mode the next edge is the next slot boundary,
   `(_cur_step + 1) / _effective_length()`, or `1.0` when that reaches the cycle end.
3. **The wrap arm** (`:744`): `else _on_boundary();` fires the wrap's boundary. In
   melody mode the wrap enters slot 0, so `_cur_step` must be set to 0 before the
   call, mirroring `_enter_step(0)` on the STEP side.
4. **The trailing recompute** (`:751`): `if (!_step_mode && !_frozen) _target = _compute_raw();`
   must be skipped in melody mode — the value holds between boundaries.

Advance `_since_fire`/`_since_phrase` by `kTickInterval` **before** the edge walk, so
the window's first boundary does not read a stale count. That quantises the floors to
one control interval (~2 ms at 48 kHz), which is well inside their own tolerance.

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

- [ ] **Step 5: Prove the RED**

Restore `next_edge`'s hard-coded `1.0` for the melody arm, rebuild, run the new
equivalence case → **FAIL**. Revert.

- [ ] **Step 6: If the equivalence budget cannot be re-derived, stop here and document**

The spec permits this task to end at a documented gap. `tick()`'s near-endpoint
handling (`:705-726`) carries a sample-epsilon budget derived for STEP and FLOW-LFO
edges; if a melody-mode slot raster cannot be brought inside it without a
disproportionate rewrite, **do not force it**. Instead:

- leave `tick()` on the FLOW LFO path for melody mode,
- add an explicit comment at `lane.cpp:646` saying so, naming this task and the fact
  that `SuperModulator` never takes this path for `LANE_PITCH`,
- record the gap in `docs/roadmap.md`'s entry for this milestone,
- and commit that instead. A documented gap in a path production never takes is
  cheaper than a blocked milestone. Report the decision either way.

- [ ] **Step 7: Commit**

```bash
git add engine/mod/lane.cpp tests/test_lane_tick.cpp
git commit -m "feat(mod): tick() mirrors the melody-mode slot walk

Production never takes this path -- SuperModulator drives LANE_PITCH through
process() only -- but tick()'s contract is that it reproduces process()'s
observable sequence, and test_lane_tick.cpp exercises it directly. Letting
the isolated path drift from the production one is the trap the per-sample
call-boundary round already recorded.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 12: Documentation, and hand the listening checks to the owner

**Files:**
- Modify: `docs/roadmap.md`, `docs/2026-08-13-glow-macro-audit.md`, `host/vcv/README.md`

- [ ] **Step 1: `docs/roadmap.md`**

The "FLOW melody engine" entry moves out of **Planned** into **Done** with the
result. **Delete its TEMPO row** — the spec's §1.2 contradicts it: free lanes take
their rate from `free_hz(_rate_norm) * _pace` and never read `_bpm`
(`super_modulator.cpp:28-29`), which is what "free" means, so TEMPO-in-FLOW is not
the same cause as the other three findings and was never in scope. Update the
"Last updated" line.

- [ ] **Step 2: `docs/2026-08-13-glow-macro-audit.md`**

Result 3 ("DENSITY is dead in the free mode") and the per-macro DENSITY verdict are
now historical. **Add a pointer, do not edit the measurement** — the numbers are a
record of what was true on 2026-08-13 and stay that way.

- [ ] **Step 3: `host/vcv/README.md`**

DENSITY's description changes meaning on both modules: in the free mode it is now
"how many notes the drone uses", not a step-grid control.

- [ ] **Step 4: Commit**

```bash
git add docs/roadmap.md docs/2026-08-13-glow-macro-audit.md host/vcv/README.md
git commit -m "docs(flow): the FLOW melody engine lands

Roadmap entry moves to Done and loses its TEMPO row -- free lanes clock off
free_hz and never read the BPM ladder, which is what free means, so that
finding was never the same cause as the other three.

The audit's Result 3 gains a pointer rather than an edit: the measurement is
a record of what was true on the day.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

- [ ] **Step 5: Hand the listening checks to the owner**

These are the owner's, not automatable, and the work is not finished until they are
answered. Present them in this order — the first is the largest change in the whole
piece and is easy to miss while listening for the new controls:

1. **SHAPE's consequence.** A drone terrain today gets a *smooth sine pitch drift*,
   because `P_SHAPE_A/B` is capped at `{0,.25}` for drone; after this work it gets a
   *stepped sequence*. This is the single largest sonic change here, larger than any
   new control, and it is a direct consequence of the lane emitting the phrase note
   instead of a waveform. Judge it **first**.
2. **The continuum**, via `flow_melody.json`: standing note → two or three held notes
   → melody, across the DENSITY sweep.
3. **The retrigger consequences**: the pitch glide advances faster (the fire refresh
   drives `Quantizer::process`'s slew, which counts calls), and FLUX's THIN pattern
   now follows DENSITY through the onset-gap ring.
4. **`kFlowNoteMinS` (60 ms) and `kFlowSlewFrac` (0.35)** — both first guesses set by
   arithmetic. The gates prove the mechanisms bind; only the ear sets the values.
5. **Whether losing WANDER's self-motion (Task 9) is the right trade.** If not, the
   alternative is on record in spec §9: a dead zone on `_eff[M_WANDER]`, which costs
   10 % of the knob's travel and must sit **before** `eval_terrain`, not in the
   per-parameter guard chain.

---

## Self-review

**Spec coverage.** §4.1 → Task 1. §4.1.1 → Task 1 (the three consumers are named in
the spec; no code change follows from them). §4.2 → Task 2. §4.3 → Tasks 3 and 6.
§4.4 → Task 1. §4.5 → Task 3. §4.6 → Tasks 4 and 6. §4.7 → Task 11. §4.8 → Tasks 5
and 6. §4.9 → Task 2 (the constant is the decision; there is no live-length code to
write). §4.10 → Task 6. §4.11 → Task 7. §5 → Tasks 2 and 8. §6.5's render re-base →
Task 8. §7's non-regression → Task 8 Step 7. §8 → Task 10. §9 → Task 9. §10's gates
1–7 → Tasks 1–7; 8–11 → Tasks 6, 7, 9; 12–15 → Tasks 4, 5, 8; 16–18 → Tasks 8 and
10; 19–20 → Task 10; 21–25 → Task 12. §12's deliverables → Task 12.

**Two gaps found and closed while reviewing.** Spec §10.14 ("the flag's default and
boot order") had no home; it is covered by Task 4's preroll case and Task 8's swap
case. Spec §10.16-17's bit-identity gates are a manual render comparison rather than
a doctest, which Task 8 Step 7 now states explicitly instead of implying a unit test
that cannot exist.

**One thing the implementer must verify rather than trust.** Task 8's test uses
`Instrument` observer names taken from the spec's citations, not from a fresh read of
`engine/instrument.h`. Step 1 says so and requires a grep first.
