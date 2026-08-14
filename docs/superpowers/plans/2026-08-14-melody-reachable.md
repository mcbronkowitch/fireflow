# The melody becomes reachable — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A note deck in STEP emits its composed phrase instead of a waveform, so FORM and SONG are audible at every SHAPE position instead of only above 0.75.

**Architecture:** One guard in `ModLane::_compute_raw()` changes from `_flow_melody_on()` to a new predicate `_note_lane()` = `_melodic && _flow_melody`. `_flow_melody` is pushed from the engine id (`part.cpp:43,441`: `!= ENGINE_SAMPLER && != ENGINE_BBD`) in **both** modes, so it is really "this deck has a note engine". That gate excludes SAMPLER and BBD in both modes, which is what keeps `kBbdFlowRangeMax` intact. Measured: exactly one of the six reachable lane states changes.

**Tech Stack:** C++17, clang + Ninja, doctest (vendored in `third_party/`).

**Spec:** [`docs/superpowers/specs/2026-08-14-melody-reachable-design.md`](../specs/2026-08-14-melody-reachable-design.md) §2.1. **§2.2 (the pitch RANGE law) is NOT in this plan** — it is unspecified in the spec and needs a design pass first.

## Global Constraints

- Build: `source env.sh` then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`. **Release is not optional** — a Debug configure makes `spky_tests` and `ctrl_identity` fail with "SYNTH reference moved".
- Never `source env.sh` in a shell also used for `shell/` or `bench/`; the two toolchains must not mix.
- Everything written into the repo is English. Commit trailer is `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **Every gate must be shown RED once before it counts.** A test that cannot fail gets fixed, not kept.
- No bit-exactness gates. If a render hash moves, explain *why this change reached that scenario* before re-baselining — `tests/check_render_hash.cmake:19-20`: "only the hashes the change actually reaches may move. An unexplained one is a finding, not a baseline."
- **Probe rule:** no runtime claim goes into a commit message or a comment until a probe or a test has printed it. See `docs/engine-map.md` §6.

## Deviation from the spec, and why

The spec asks for `_flow_melody` to be **renamed** `_note_engine` in the same commit. Enumerated: that is ~90 sites across `engine/`, `host/` and **14 test files**, most of them comments. A rename that size buries a one-line behaviour change in its own diff and makes the review worthless.

**Instead: add a named predicate at the point of use and leave the field alone.** The reader gets the right name where the decision is made, the diff stays reviewable, and the trap is documented next to the predicate. If the field rename is still wanted it is a separate mechanical commit with no behaviour in it.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `engine/mod/lane.h` | lane state + predicates | **Modify** — add `_note_lane()` beside the two existing predicates (~line 200) |
| `engine/mod/lane.cpp` | lane behaviour | **Modify** — one guard at `:551`, plus its comment |
| `tests/test_melody_reachable.cpp` | the gates for this change | **Create** |
| `CMakeLists.txt` | test registration | **Modify** — one line in the `spky_tests` source list |
| `docs/engine-map.md` | measured facts | **Modify** — §1 state table rows and §7's closing paragraph |

---

## Task 1: The failing gate — FORM is inaudible at SHAPE 0

**Files:**
- Create: `tests/test_melody_reachable.cpp`
- Modify: `CMakeLists.txt` (after line 57, `tests/test_engine_map.cpp`)

**Interfaces:**
- Consumes: `spky::ModLane` (`engine/mod/lane.h`), `spky::Principle` (`engine/mod/phrase_gen.h`)
- Produces: nothing for later tasks; this file gains cases in Task 3.

- [ ] **Step 1: Write the failing test**

Create `tests/test_melody_reachable.cpp`:

```cpp
// tests/test_melody_reachable.cpp
// Gates for spec 2026-08-14-melody-reachable-design.md §2.1.
//
// Construction order: set_melodic() BEFORE init() -- init() branches on
// _melodic when it seeds the pattern (lane.cpp:70), and SuperModulator
// orders them that way (super_modulator.cpp:14-15). A probe in the other
// order measures a lane whose RNG stream was spent on a contour walk.
#include <doctest/doctest.h>
#include <set>
#include <vector>
#include "mod/lane.h"
using namespace spky;

namespace {

// The distinct value set a melodic STEP lane emits over 20 s at 0.5 Hz.
// SMOOTH 0 so the raw target is visible; VARY 0 so nothing mutates.
std::set<float> emitted(Principle form, float shape, uint32_t seed,
                        bool note_engine = true) {
    ModLane l;
    l.set_melodic(true);                 // BEFORE init -- see header
    l.init(48000.f, seed);
    l.set_form(form);
    l.set_flow_melody(note_engine);      // engine class: note vs SAMPLER/BBD
    l.set_step(true, 8);
    l.set_rate_hz(0.5f);
    l.set_shape(shape);
    l.set_smooth(0.f);
    l.set_range(1.f);
    l.set_variation(0.f);
    std::set<float> vals;
    for (int i = 0; i < 48000 * 20; ++i) vals.insert(l.process());
    return vals;
}

// How many of the four other Principles differ from TwoMotif.
int forms_differing(float shape, uint32_t seed) {
    const std::set<float> ref = emitted(Principle::TwoMotif, shape, seed);
    int n = 0;
    for (int f = 1; f < static_cast<int>(Principle::kCount); ++f)
        if (emitted(Principle(f), shape, seed) != ref) ++n;
    return n;
}

} // namespace

TEST_CASE("melody-reachable: FORM changes the sequence at every SHAPE") {
    // Before this change a STEP note deck ran its pitch through the waveform
    // bank, and sh_hold -- the phrase -- is only weighted in shape_value's
    // fourth arm (waveforms.h:32, shape >= 0.75). Below that the phrase was
    // computed and discarded, so every Principle emitted the same sine
    // staircase: measured 0 of 4 differing at SHAPE 0.00 AND at 0.50, on all
    // four seeds, with only 5 distinct values.
    for (uint32_t seed : {999u, 12345u, 7u, 4242u}) {
        CAPTURE(seed);
        CHECK(forms_differing(0.00f, seed) >= 3);
        CHECK(forms_differing(0.50f, seed) >= 3);
        CHECK(forms_differing(1.00f, seed) >= 3);
    }
}
```

Register it — in `CMakeLists.txt`, in the `spky_tests` source list, directly after `tests/test_engine_map.cpp`:

```cmake
    tests/test_melody_reachable.cpp
```

- [ ] **Step 2: Run it and confirm it fails for the stated reason**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/spky_tests.exe --test-case="melody-reachable*" -s
```

Expected: **FAIL** at the `0.00f` and `0.50f` checks with `0 >= 3`, on every seed; the `1.00f` check passes. If the `1.00f` check also fails, stop — the harness is wrong, not the engine.

- [ ] **Step 3: Commit the RED**

```bash
git add tests/test_melody_reachable.cpp CMakeLists.txt
git commit -m "test(melody): the gate that proves FORM is inaudible below SHAPE 0.75

Red as written: 0 of 4 Principles differ at SHAPE 0.00 and 0.50 on all
four seeds, 3 of 4 at 1.00. The phrase is only weighted in shape_value's
fourth arm, so below 0.75 it is computed and discarded.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 2: The guard

**Files:**
- Modify: `engine/mod/lane.h:195-200`
- Modify: `engine/mod/lane.cpp:544-556`

**Interfaces:**
- Consumes: `_melodic`, `_flow_melody` (existing members)
- Produces: `ModLane::_note_lane()` — private, `const`, no arguments, returns `bool`. Task 3 does not call it directly; it observes its effect.

- [ ] **Step 1: Add the predicate**

In `engine/mod/lane.h`, after `_melody_engine_on()` (currently line 200):

```cpp
    // "this lane carries composed notes", i.e. the deck runs a note engine.
    // NAMING TRAP: _flow_melody reads as "FLOW melody is on" and is not that.
    // Part pushes it from the engine id at part.cpp:43 and :441 --
    // `_engine_id != ENGINE_SAMPLER && != ENGINE_BBD` -- in BOTH modes, so it
    // is an engine-class flag. Gating on it rather than on _melody_engine_on()
    // is what keeps this out of a BBD deck's PITCH lane, which is the delay
    // clock and not a note: kBbdFlowRangeMax derives its 2 from apply_range
    // itself (taste.h:586-588), and the owner ruled for that trade 2026-08-07.
    bool _note_lane() const { return _melodic && _flow_melody; }
```

- [ ] **Step 2: Change the guard**

In `engine/mod/lane.cpp`, replace the head of `_compute_raw()` (currently lines 544-551):

```cpp
float ModLane::_compute_raw() const {
    // A note lane emits its phrase directly, in STEP as in FLOW. Routing it
    // through shape_value instead would weight the phrase only in the bank's
    // fourth arm (waveforms.h:32, shape >= 0.75): below that the composed
    // pitch is computed and discarded, and every FORM emits the same sine
    // staircase -- measured, 5 distinct values, identical across seeds. The
    // terrain draws that top quarter in 4.65 % of cases and never on a drone,
    // so the melody system was unreachable where the instrument plays.
    // SHAPE is therefore inert on this lane in both modes, consistently. What
    // SHAPE should mean for a melody is the SHAPE rework's question.
    if (_note_lane()) return _active_pattern().pitch[_sh_slot()];
```

Leave the rest of the function unchanged.

Note the removed comment claimed the old form was "arithmetically `shape_value(ph, 1.f, pitch[slot])` … the return at :32 is `sh_hold` **exactly**". That is false — `lerpf(a, b, 1)` is `a + (b−a)` and differs from `b` by up to 2⁻²⁴ (`docs/engine-map.md` §5). Do not carry the claim forward.

- [ ] **Step 3: Run the gate and confirm GREEN**

```bash
cmake --build build && ./build/spky_tests.exe --test-case="melody-reachable*" -s
```

Expected: PASS, with `3 >= 3` at every SHAPE and seed.

- [ ] **Step 4: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all green. `ctrl_identity` sets **no engine and no `set_step`** — two SYNTH decks in FLOW, already on the phrase path — so its hash must **not** move. If it does, stop and find out why before touching the baseline.

- [ ] **Step 5: Commit**

```bash
git add engine/mod/lane.h engine/mod/lane.cpp
git commit -m "feat(mod): a note lane emits its phrase in STEP as in FLOW

Gates on _melodic && _flow_melody, not _melody_engine_on(): _flow_melody
is an engine-class flag pushed from the engine id in both modes, so this
excludes SAMPLER and BBD everywhere and leaves kBbdFlowRangeMax's
derivation intact. Exactly one of the six reachable lane states changes.

FORM now differs on 3 of 4 Principles at every SHAPE; before, only above
0.75, which the terrain draws in 4.65 % of cases and never on a drone.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 3: The non-regression gates

**Files:**
- Modify: `tests/test_melody_reachable.cpp`

**Interfaces:**
- Consumes: the `emitted()` helper from Task 1.

- [ ] **Step 1: Write the tests**

Append to `tests/test_melody_reachable.cpp`:

```cpp
// The full emitted stream, for sample-exact comparison.
namespace {
std::vector<float> stream(bool melodic, bool step, bool note_engine,
                          float shape, uint32_t seed) {
    ModLane l;
    l.set_melodic(melodic);
    l.init(48000.f, seed);
    l.set_flow_melody(note_engine);
    l.set_step(step, 8);
    l.set_rate_hz(0.5f);
    l.set_shape(shape);
    l.set_smooth(0.f);
    l.set_range(1.f);
    l.set_variation(0.f);
    std::vector<float> out(48000 * 5);
    for (float& v : out) v = l.process();
    return out;
}
} // namespace

TEST_CASE("melody-reachable: SHAPE is inert on a note lane, both modes") {
    for (bool step : {false, true}) {
        CAPTURE(step);
        const std::vector<float> ref = stream(true, step, true, 0.00f, 999u);
        for (float sh : {0.25f, 0.50f, 0.75f, 1.00f}) {
            CAPTURE(sh);
            CHECK(stream(true, step, true, sh, 999u) == ref);
        }
    }
}

TEST_CASE("melody-reachable: SAMPLER and BBD PITCH lanes are untouched") {
    // Their decks push set_flow_melody(false) from the engine id
    // (part.cpp:43,441), so _note_lane() is false in BOTH modes and the
    // guard must not reach them. This is what protects kBbdFlowRangeMax.
    // RED against the _melody_engine_on() variant: it moves the STEP case
    // from p2p 1.750 / 9 distinct to 0.351 / 8.
    for (bool step : {false, true}) {
        CAPTURE(step);
        const std::vector<float> s = stream(true, step, false, 0.50f, 999u);
        std::set<float> vals(s.begin(), s.end());
        CHECK(vals.size() > 8);          // still a waveform, not an 8-slot phrase
    }
}

TEST_CASE("melody-reachable: non-melodic lanes are untouched") {
    for (bool step : {false, true}) {
        CAPTURE(step);
        const std::vector<float> s = stream(false, step, false, 0.50f, 999u);
        std::set<float> vals(s.begin(), s.end());
        CHECK(vals.size() > 8);
    }
}
```

- [ ] **Step 2: Run them — expect GREEN**

```bash
cmake --build build && ./build/spky_tests.exe --test-case="melody-reachable*" -s
```

- [ ] **Step 3: Prove the SAMPLER/BBD gate can go RED**

Temporarily change `lane.cpp`'s guard to `_melody_engine_on()`, rebuild, run. Expected: **"SAMPLER and BBD PITCH lanes are untouched" FAILS** on the `step == true` case (8 distinct, not > 8). Then revert the guard and confirm green again. Record both outputs in the commit message.

- [ ] **Step 4: Commit**

```bash
git add tests/test_melody_reachable.cpp
git commit -m "test(melody): pin the three states that must not move

SHAPE inert on a note lane in both modes; SAMPLER/BBD PITCH lanes
untouched in both modes; non-melodic lanes untouched. The second was
proven red against the _melody_engine_on() variant, which moves the STEP
case to an 8-slot phrase and would void kBbdFlowRangeMax.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Task 4: The map catches up

**Files:**
- Modify: `docs/engine-map.md` — §1's state table and §7's closing paragraph

**Interfaces:** none.

- [ ] **Step 1: Correct §1's state table**

Rows 5 and 6 (`melodic + STEP`) currently read `p2p 2.000 / distinct 5`. Re-measure both with a probe (recipe in §6) and replace with the new figures. The sentence "`_flow_melody` is ignored whenever `_step_mode` is true" is **no longer true of the pitch output** — it remains true of `_flow_melody_on()` itself. Say which.

Check whether `tests/test_engine_map.cpp`'s §1 case still passes; if it pinned the old STEP behaviour, re-baseline it in this commit with the reason.

- [ ] **Step 2: Correct §7's closing paragraph**

It currently ends: *"the instrument plays the decoy loudly and the melody almost never"* and *"Any SHAPE/SMOOTH design has to answer where on the axis the melody should live."* Both are now historical. Replace with the measured after-state and a pointer to this plan's commit.

- [ ] **Step 3: Verify and commit**

```bash
ctest --test-dir build --output-on-failure
git add docs/engine-map.md tests/test_engine_map.cpp
git commit -m "docs(map): the decoy is gone

Section 1's melodic STEP rows and section 7's closing paragraph described
behaviour this branch removed. Re-measured, not edited from memory.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Self-review notes

**Spec coverage.** §2.1 is fully covered by Tasks 1–3; its gates G1 (Task 1), G2 and G5 and G6 (Task 3) all have tasks. **G3 and G4 are NOT covered** — they belong to §2.2, which is out of scope, and must not be claimed as done.

**Known gap, stated rather than hidden.** After this plan the melody is audible at every SHAPE but still narrow where RANGE is low: on the drone band it clears 1.8–3.3 scale degrees. That is the §2.2 problem, and it is a real remaining limitation of what ships here.

**Not touched:** `engine/flow/taste.h`, `engine/center/`, `host/vcv/`, `bench/`. No stored value changes meaning, so there is no `fireflow-control-merge-init-trap` exposure and the factory patch is unaffected.
