# Modulation Lane Grid Lock in STEP — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In STEP mode the four texture modulation lanes run on the deck's step
clock instead of their own, so they can never drift off the step grid at any
SHAPE setting.

**Architecture:** The step clock is already normalized — `_phase_inc =
rate_hz / sr * (8 / steps)` makes one step last `sr / (8 * rate_hz)` regardless
of `_steps`. Giving all five lanes the same `rate_hz` therefore yields one
shared step grid, and the old per-lane rate ratio moves into the lane's slot
count (its cycle length). TIDE scales slot counts instead of rates, DRIFT's
separate `mod_scale` and the per-lane EVOLVE rate walk are bypassed in STEP,
SPOT's phase kick is quantized to whole slots, and FLOW→STEP entry snaps all
five lanes rather than PITCH alone.

**Tech Stack:** C++17, doctest (vendored in `third_party/`), CMake + Ninja +
clang. Engine only — no VCV panel change, no new control.

**Spec:** `docs/superpowers/specs/2026-07-25-mod-lane-step-grid-lock-design.md`

## Global Constraints

- Build environment: source `env.sh` at the fork root before any `cmake`/`ctest`.
  It sets `PATH` to LLVM + ninja and `CC=clang CXX=clang++ CMAKE_GENERATOR=Ninja`.
  This machine has no MSVC and no native GCC.
- Configure/build/test: `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`.
  Ninja is single-config; the binary is at `build/spky_tests.exe`.
- Run one doctest case: `./build/spky_tests.exe -tc="<case name>"`.
- Patch compatibility is out of scope; the instrument is in development. No
  parameter-ID migration, no preservation of old behaviour.
- No byte-identity or checksum gates. Renders are sanity checks only.
- FLOW behaviour must be unchanged by every task in this plan.
- Commit trailer for every commit:
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- New test files must be added to the `spky_tests` source list in
  `CMakeLists.txt` (around line 37-60) or they will not run.
- Float phase drift (~0.0006/cycle) makes exact sample-index comparisons across
  many cycles flaky. Where two lanes' boundary sample indices are compared,
  allow a tolerance of 1 sample. This is a known trap in this codebase, not
  sloppiness.

## File Structure

| File | Responsibility |
|---|---|
| `engine/mod/lane_len.h` (create) | The STEP cycle-length table and `lane_slots()`. Pure, header-only, no dependency on `ModLane`. |
| `engine/mod/lane.h` / `lane.cpp` (modify) | Two new lane capabilities: an externally supplied EVOLVE rate walk, and a whole-slot phase kick. |
| `engine/mod/super_modulator.h` / `.cpp` (modify) | Owns the STEP decision: per-lane slot counts, one shared rate, EVOLVE inheritance, quantized SPOT, deck-wide snap. |
| `engine/center/center.cpp` (modify) | One call site rename. No logic change — DRIFT's `mod_scale` is ignored inside `SuperModulator`, not recomputed here. |
| `tests/test_lane_len.cpp` (create) | Slot-count table. |
| `tests/test_lane_grid.cpp` (create) | ModLane-level: shared grid, external rate walk, whole-slot kick. |
| `tests/test_step_grid_lock.cpp` (create) | SuperModulator-level: the grid invariant under chaos, shape independence, STEP entry, live STEPS turn, FLOW regression. |
| `tests/test_lane.cpp` (modify) | Rename in the existing FLOW snap test. |
| `docs/roadmap.md` (modify) | One row. |

---

### Task 1: Cycle-length table

**Files:**
- Create: `engine/mod/lane_len.h`
- Create: `tests/test_lane_len.cpp`
- Modify: `CMakeLists.txt` (test source list)

**Interfaces:**
- Consumes: `LaneId` / `LANE_COUNT` from `engine/mod/lane_id.h`, `kTideRatios` /
  `kTideCount` from `engine/mod/divisions.h` (test only).
- Produces: `spky::kLaneLenFactor[LANE_COUNT]`, `spky::kLaneSlotsMin` (= 2),
  `spky::kLaneSlotsMax` (= 64), and
  `int spky::lane_slots(int lane, int steps, float tide)`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_lane_len.cpp`:

```cpp
#include <doctest/doctest.h>
#include "mod/lane_len.h"
#include "mod/divisions.h"
using namespace spky;

TEST_CASE("lane_len: the default phrase yields the 4/6/8/12/16 set") {
    CHECK(lane_slots(LANE_SOURCE, 8, 1.f) ==  4);
    CHECK(lane_slots(LANE_LEVEL,  8, 1.f) ==  6);
    CHECK(lane_slots(LANE_PITCH,  8, 1.f) ==  8);
    CHECK(lane_slots(LANE_MOTION, 8, 1.f) == 12);
    CHECK(lane_slots(LANE_SIZE,   8, 1.f) == 16);
}

TEST_CASE("lane_len: TIDE stretches the texture lanes and never PITCH") {
    CHECK(lane_slots(LANE_SOURCE, 8, 0.5f) ==  8);
    CHECK(lane_slots(LANE_SIZE,   8, 0.5f) == 32);
    CHECK(lane_slots(LANE_MOTION, 8, 0.5f) == 24);
    CHECK(lane_slots(LANE_LEVEL,  8, 0.5f) == 12);
    CHECK(lane_slots(LANE_PITCH,  8, 0.5f) ==  8);
    CHECK(lane_slots(LANE_PITCH,  8, 4.f)  ==  8);
}

TEST_CASE("lane_len: clamps at both ends") {
    // 1 slot would pin the lane to phase 0 and emit a constant value.
    CHECK(lane_slots(LANE_SOURCE,  2, 1.f)   ==  2);
    CHECK(lane_slots(LANE_SIZE,   16, 0.25f) == 64);   // wants 128
}

TEST_CASE("lane_len: odd phrase lengths round half away from zero") {
    CHECK(lane_slots(LANE_SOURCE, 5, 1.f) ==  3);   // 2.50
    CHECK(lane_slots(LANE_LEVEL,  5, 1.f) ==  4);   // 3.75
    CHECK(lane_slots(LANE_MOTION, 5, 1.f) ==  8);   // 7.50
    CHECK(lane_slots(LANE_SIZE,   5, 1.f) == 10);
}

TEST_CASE("lane_len: every panel-reachable combination stays inside bounds") {
    for (int s = 2; s <= 16; ++s)
        for (int t = 0; t < kTideCount; ++t)
            for (int l = 0; l < LANE_COUNT; ++l) {
                const int n = lane_slots(l, s, kTideRatios[t]);
                CHECK(n >= kLaneSlotsMin);
                CHECK(n <= kLaneSlotsMax);
            }
}
```

- [ ] **Step 2: Register the test file**

In `CMakeLists.txt`, inside the `add_executable(spky_tests ...)` list, add the
new file directly after the line `tests/test_divisions.cpp`:

```cmake
    tests/test_lane_len.cpp
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
source env.sh && cmake -S . -B build && cmake --build build
```

Expected: FAIL at compile time with `fatal error: 'mod/lane_len.h' file not found`.

- [ ] **Step 4: Write the implementation**

Create `engine/mod/lane_len.h`:

```cpp
#pragma once
#include <cmath>
#include "mod/lane_id.h"

namespace spky {

// STEP-mode cycle lengths (spec 2026-07-25 mod-lane-step-grid-lock).
//
// In STEP every lane runs on the deck's step clock, so the old rate ratio in
// super_modulator.cpp's kLaneRatio becomes a length factor f = 1 / ratio on
// the phrase length. MOTION and LEVEL are deliberately rounded from x3/4 and
// x3/2 to x2/3 and x4/3: that turns the two lanes that could never align into
// clean 2- and 3-relations to the phrase, giving the set 4, 6, 8, 12, 16 at
// STEPS = 8 -- congruent again every 48 steps, or six phrases. The polyrhythm
// is preserved, it is just deliberate now.
inline constexpr float kLaneLenFactor[LANE_COUNT] = {
    0.5f,    // LANE_SOURCE  was x2    -> half the phrase
    2.f,     // LANE_SIZE    was x1/2  -> twice the phrase
    1.f,     // LANE_PITCH   x1        -> the phrase itself
    1.5f,    // LANE_MOTION  x3/4 -> x2/3 -> one and a half phrases
    0.75f,   // LANE_LEVEL   x3/2 -> x4/3 -> three quarters of a phrase
};

// A single slot would put the lane's only boundary at phase 0, so it would
// emit a constant value. 64 bounds the other end; the contour buffer is 32
// slots (ModLane::kSeqSlots) and repeats inside longer cycles, which is
// accepted for texture lanes and documented in the spec.
inline constexpr int kLaneSlotsMin = 2;
inline constexpr int kLaneSlotsMax = 64;

// Slot count of one lane in STEP. `tide` is the ladder ratio (kTideRatios), so
// a slower lane (tide < 1) loops over proportionally more steps. The PITCH
// lane is the phrase itself: it returns `steps` unchanged and never sees TIDE
// or the clamps, because changing it would change the phrase length.
inline int lane_slots(int lane, int steps, float tide) {
    if (lane == LANE_PITCH) return steps;
    if (tide <= 0.f) tide = 1.f;
    const float want =
        static_cast<float>(steps) * kLaneLenFactor[lane] / tide;
    int n = static_cast<int>(std::lround(want));
    if (n < kLaneSlotsMin) n = kLaneSlotsMin;
    if (n > kLaneSlotsMax) n = kLaneSlotsMax;
    return n;
}

} // namespace spky
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -ts="*" -tc="lane_len:*"
```

Expected: `[doctest] Status: SUCCESS!` with 5 test cases passing.

- [ ] **Step 6: Commit**

```bash
git add engine/mod/lane_len.h tests/test_lane_len.cpp CMakeLists.txt
git commit -m "feat(mod): add STEP cycle-length table

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: ModLane — external rate walk and whole-slot kick

**Files:**
- Modify: `engine/mod/lane.h`
- Modify: `engine/mod/lane.cpp`
- Create: `tests/test_lane_grid.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces, on `spky::ModLane`:
  - `void set_ev_rate_external(bool on, float v)` — while `on`, the phase
    advance uses `v` instead of the lane's own `_ev_rate`.
  - `float ev_rate() const` — the lane's own walked rate offset.
  - `void kick_steps(int n, float dshape)` — jump `n` whole slots.
  - `float rate_hz_for_test() const` — under `SPKY_TESTING` only.

- [ ] **Step 1: Write the failing test**

Create `tests/test_lane_grid.cpp`:

```cpp
#include <doctest/doctest.h>
#include <cstdlib>
#include <vector>
#include "mod/lane.h"
using namespace spky;

// One shared step grid (spec 2026-07-25 mod-lane-step-grid-lock). The step
// clock is normalized -- _phase_inc = rate/sr * (8/steps) makes one step last
// sr/(8*rate) whatever _steps is -- so lanes at the same rate_hz share their
// boundaries no matter how long their cycles are. These tests pin that down at
// the ModLane level, on the per-sample path, away from the 96-sample raster.
namespace {
constexpr float kSr   = 48000.f;
constexpr float kRate = 2.f;          // step = 3000 samples

void configure(ModLane& l, int slots, float shuffle, float variation) {
    l.set_melodic(false);
    l.init(kSr, 4242u);
    l.set_shuffle(shuffle);
    l.set_step(true, slots);
    l.set_rate_hz(kRate);
    l.set_shape(1.f);
    l.set_smooth(0.f);
    l.set_variation(variation);
}

std::vector<int> fire_samples(int slots, int n, float shuffle = 0.f,
                              float variation = 0.f) {
    ModLane l;
    configure(l, slots, shuffle, variation);
    std::vector<int> out;
    for (int i = 0; i < n; ++i) {
        l.process();
        if (l.fired()) out.push_back(i);
    }
    return out;
}

// Boundary detection reads a free-running float phasor, so two lanes with
// different _phase_inc can cross the same instant one sample apart. That is
// float drift, not misalignment -- see the plan's global constraints.
void check_aligned(const std::vector<int>& got, const std::vector<int>& ref) {
    REQUIRE(got.size() == ref.size());
    for (size_t i = 0; i < ref.size(); ++i)
        CHECK(std::abs(got[i] - ref[i]) <= 1);
}
} // namespace

TEST_CASE("grid: equal rate gives one shared step grid for any slot count") {
    const auto ref = fire_samples(8, 48000);
    REQUIRE(ref.size() >= 16);
    for (int slots : {2, 4, 6, 12, 16, 24, 32})
        check_aligned(fire_samples(slots, 48000), ref);
}

TEST_CASE("grid: SHUFFLE warps every even-slot lane identically") {
    const auto ref = fire_samples(8, 48000, 0.7f);
    REQUIRE(ref.size() >= 16);
    for (int slots : {4, 6, 12, 16})
        check_aligned(fire_samples(slots, 48000, 0.7f), ref);
}

TEST_CASE("grid: an external rate walk overrides the lane's own EVOLVE walk") {
    const auto ref = fire_samples(8, 96000, 0.f, 0.f);

    ModLane dut;
    configure(dut, 8, 0.f, 0.9f);            // GROW: would walk _ev_rate +-20%
    dut.set_ev_rate_external(true, 0.f);
    std::vector<int> got;
    for (int i = 0; i < 96000; ++i) {
        dut.process();
        if (dut.fired()) got.push_back(i);
    }
    check_aligned(got, ref);

    // The lane still walks its own value; it is simply not the one used.
    CHECK(dut.ev_rate() != 0.f);
}

TEST_CASE("grid: an even whole-slot kick keeps the lane on the grid") {
    // SHUFFLE is on deliberately: the kick has to preserve both the position
    // inside the step and the step parity the warp is keyed to. An even jump
    // does; that is why SuperModulator::spot rounds to an even count.
    ModLane ref, dut;
    configure(ref, 8, 0.5f, 0.f);
    configure(dut, 12, 0.5f, 0.f);

    std::vector<int> ref_fires, dut_fires;
    for (int i = 0; i < 96000; ++i) {
        ref.process();
        if (ref.fired()) ref_fires.push_back(i);
        if (i == 30000) dut.kick_steps(4, 0.f);
        dut.process();
        if (dut.fired()) dut_fires.push_back(i);
    }
    // The jump itself fires one extra boundary -- that is the audible stumble.
    // Every OTHER fire must still land on a reference boundary.
    REQUIRE(ref_fires.size() >= 16);
    REQUIRE(dut_fires.size() >= 16);
    for (int f : dut_fires) {
        if (f == 30000) continue;
        bool on_ref = false;
        for (int r : ref_fires)
            if (std::abs(f - r) <= 1) { on_ref = true; break; }
        CHECK(on_ref);
    }
}

TEST_CASE("grid: a zero-slot kick is a no-op, not a rewind to the boundary") {
    // Regression guard for the tempting wrong implementation: snapping to
    // shuffle_phase_for_position(cur_step) drops the fraction inside the step
    // and silently delays every later boundary by it.
    ModLane ref, dut;
    configure(ref, 8, 0.f, 0.f);
    configure(dut, 8, 0.f, 0.f);

    std::vector<int> ref_fires, dut_fires;
    for (int i = 0; i < 96000; ++i) {
        ref.process();
        if (ref.fired()) ref_fires.push_back(i);
        if (i == 30000) dut.kick_steps(0, 0.f);
        dut.process();
        if (dut.fired()) dut_fires.push_back(i);
    }
    check_aligned(dut_fires, ref_fires);
}
```

- [ ] **Step 2: Register the test file**

In `CMakeLists.txt`, add directly after `tests/test_step_clock.cpp`:

```cmake
    tests/test_lane_grid.cpp
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
source env.sh && cmake -S . -B build && cmake --build build
```

Expected: FAIL at compile time — `no member named 'set_ev_rate_external' in 'spky::ModLane'`
and `no member named 'kick_steps'`.

- [ ] **Step 4: Add the public API in `engine/mod/lane.h`**

In the `#ifdef SPKY_TESTING` block (currently holding `pattern_for_test`,
`cadence_slot_for_test`, `bound_a_opening_for_test`), add:

```cpp
    float rate_hz_for_test() const { return _rate_hz; }
```

Directly after the existing `void kick(float dphase, float dshape);` in the
`--- M4 center hooks ---` section, add:

```cpp
    // STEP grid lock (spec 2026-07-25 mod-lane-step-grid-lock). In STEP the
    // texture lanes must not walk their own EVOLVE rate -- an independent
    // +-20% walk per lane is one of the six things that used to push them off
    // the deck's step grid for good. While `on`, the phase advance uses `v`
    // (the master lane's walk) instead of this lane's `_ev_rate`. The per-lane
    // draw in _evolve_outgoing_pattern still happens and is discarded, which
    // keeps _ev_phase and _ev_shape on their established RNG progression.
    void  set_ev_rate_external(bool on, float v) {
        _ev_rate_ext_on = on;
        _ev_rate_ext = v;
    }
    float ev_rate() const { return _ev_rate; }
    // SPOT in STEP: a phase jump of `n` whole slots. Unlike kick()'s raw
    // phase addition this lands on a real boundary under SHUFFLE too, so the
    // stumble stays on the grid.
    void  kick_steps(int n, float dshape);
```

In the private section, next to `_ev_rate`, add:

```cpp
    // The rate offset the phase advance actually uses. Every advance path
    // must read this, never _ev_rate directly -- process(), tick()'s dp
    // derivations and step_samples() are a matched set here.
    float _rate_walk() const { return _ev_rate_ext_on ? _ev_rate_ext : _ev_rate; }
    bool  _ev_rate_ext_on = false;
    float _ev_rate_ext    = 0.f;
```

- [ ] **Step 5: Route every advance path through `_rate_walk()`**

In `engine/mod/lane.h`, `step_samples()` — replace `_ev_rate` with `_rate_walk()`:

```cpp
    float step_samples() const {
        return _phase_inc > 0.f
            ? 1.f / (_phase_inc * (1.f + _rate_walk()) * static_cast<float>(_steps))
            : 0.f;
    }
```

In `engine/mod/lane.cpp`, four sites:

`process()` — `_phase += _phase_inc * (1.f + _ev_rate);` becomes

```cpp
    _phase += _phase_inc * (1.f + _rate_walk());
```

`tick()` — `window_dp[0] = _phase_inc * (1.f + _ev_rate);` becomes

```cpp
    window_dp[0] = _phase_inc * (1.f + _rate_walk());
```

`tick()` — `const float dp1 = _phase_inc * (1.f + _ev_rate);` becomes

```cpp
        const float dp1 = _phase_inc * (1.f + _rate_walk());
```

`tick()` — inside the wrap branch, the `window_dp` append becomes

```cpp
                window_dp[window_dp_count++] =
                    _phase_inc * (1.f + _rate_walk());
```

Verify none are left:

```bash
grep -n "1.f + _ev_rate" engine/mod/lane.cpp engine/mod/lane.h
```

Expected: no output.

- [ ] **Step 6: Reset the new state in `init()`**

In `ModLane::init()` in `engine/mod/lane.cpp`, directly after the line
`_ev_rate  = 0.f;`, add:

```cpp
    _ev_rate_ext_on = false;
    _ev_rate_ext    = 0.f;
```

- [ ] **Step 7: Implement `kick_steps`**

In `engine/mod/lane.cpp`, directly after the existing `ModLane::kick`
definition, add:

```cpp
void ModLane::kick_steps(int n, float dshape) {
    // Jump n whole slots while KEEPING the position inside the current step.
    // Dropping that fraction would look like a clean landing on a boundary and
    // is in fact the one thing this must not do: the lane would then need a
    // full step to reach its next boundary while the deck needs only what is
    // left of the current one, so the two would come out `frac` apart and stay
    // that way. Preserving it is what makes the kick grid-neutral.
    //
    // The position is expressed in step units and mapped back through the same
    // warp the clock walker uses, so a shuffled lane lands on a shuffled
    // position rather than a straight one. _cur_step is deliberately left
    // alone -- both process() and tick() open with a "phase says a different
    // step than _cur_step remembers" check, which fires the new step for us
    // (see the comment in tick()). That one fire at the moment of the kick is
    // the audible stumble; it is the only boundary SPOT places off the grid.
    const int   steps = _steps < 1 ? 1 : _steps;
    const int   cur   = _cur_step < 0 ? 0 : _cur_step;
    const float frac  = shuffle_step_fraction(
        _phase, cur, steps, _shuffle_latched);
    _phase = shuffle_phase_for_position(
        static_cast<float>(cur + n) + frac, steps, _shuffle_latched);
    _kick_shape += dshape;
}
```

Note for the caller (Task 5): under SHUFFLE the warp is applied to odd step
indices, so a jump of an odd number of slots flips the lane's swing parity
against the deck's and offsets it by up to a third of a step. `kick_steps`
itself does not round — `SuperModulator::spot` quantizes to an even count.

- [ ] **Step 8: Run the tests to verify they pass**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -tc="grid:*"
```

Expected: `[doctest] Status: SUCCESS!` with 5 test cases passing.

- [ ] **Step 9: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass. `test_lane_tick`'s equivalence cases in particular
must stay green — both `process()` and `tick()` were touched.

- [ ] **Step 10: Commit**

```bash
git add engine/mod/lane.h engine/mod/lane.cpp tests/test_lane_grid.cpp CMakeLists.txt
git commit -m "feat(mod): give ModLane an external rate walk and a whole-slot kick

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: SuperModulator — one clock, per-lane slot counts

**Files:**
- Modify: `engine/mod/super_modulator.h`
- Modify: `engine/mod/super_modulator.cpp`
- Create: `tests/test_step_grid_lock.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `spky::lane_slots` (Task 1), `ModLane::rate_hz_for_test` (Task 2).
- Produces, on `spky::SuperModulator`:
  - `bool step_mode() const`
  - `int  deck_steps() const`
  - under `SPKY_TESTING`: `float lane_rate_hz_for_test(int i) const`,
    `int lane_slots_for_test(int i) const`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_step_grid_lock.cpp`:

```cpp
#include <doctest/doctest.h>
#include <vector>
#include "mod/super_modulator.h"
#include "mod/lane_len.h"
#include "mod/divisions.h"
using namespace spky;

TEST_CASE("steplock: STEP gives every lane one rate and its own slot count") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(true, 8);

    const float r = m.lane_rate_hz_for_test(LANE_PITCH);
    CHECK(r > 0.f);
    for (int i = 0; i < LANE_COUNT; ++i)
        CHECK(m.lane_rate_hz_for_test(i) == doctest::Approx(r));

    CHECK(m.lane_slots_for_test(LANE_SOURCE) ==  4);
    CHECK(m.lane_slots_for_test(LANE_LEVEL)  ==  6);
    CHECK(m.lane_slots_for_test(LANE_PITCH)  ==  8);
    CHECK(m.lane_slots_for_test(LANE_MOTION) == 12);
    CHECK(m.lane_slots_for_test(LANE_SIZE)   == 16);
}

TEST_CASE("steplock: TIDE moves slot counts in STEP, not rates") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(true, 8);
    const float r = m.lane_rate_hz_for_test(LANE_PITCH);

    m.set_tide(0.25f);                       // ladder rung x1/2
    REQUIRE(kTideRatios[tide_index(0.25f)] == doctest::Approx(0.5f));
    for (int i = 0; i < LANE_COUNT; ++i)
        CHECK(m.lane_rate_hz_for_test(i) == doctest::Approx(r));
    CHECK(m.lane_slots_for_test(LANE_SOURCE) ==  8);
    CHECK(m.lane_slots_for_test(LANE_SIZE)   == 32);
    CHECK(m.lane_slots_for_test(LANE_MOTION) == 24);
    CHECK(m.lane_slots_for_test(LANE_LEVEL)  == 12);
    CHECK(m.lane_slots_for_test(LANE_PITCH)  ==  8);   // the phrase, always
}

TEST_CASE("steplock: STEP ignores DRIFT's separate mod_scale") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(true, 8);
    m.set_rate_scale(1.f, 1.6f);             // COUPLE < max under DRIFT

    const float r = m.lane_rate_hz_for_test(LANE_PITCH);
    for (int i = 0; i < LANE_COUNT; ++i)
        CHECK(m.lane_rate_hz_for_test(i) == doctest::Approx(r));
}

TEST_CASE("steplock: FLOW keeps the old ratios, TIDE and mod_scale") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(false, 8);
    m.set_rate_scale(1.f, 2.f);

    const float pitch = m.lane_rate_hz_for_test(LANE_PITCH);
    CHECK(m.lane_rate_hz_for_test(LANE_SOURCE)
          == doctest::Approx(pitch * 2.f * 2.f));      // mod_scale x ratio
    CHECK(m.lane_rate_hz_for_test(LANE_MOTION)
          == doctest::Approx(pitch * 2.f * 0.75f));
    for (int i = 0; i < LANE_COUNT; ++i)
        CHECK(m.lane_slots_for_test(i) == 8);          // no per-lane slots
}
```

- [ ] **Step 2: Register the test file**

In `CMakeLists.txt`, add directly after `tests/test_super_modulator.cpp`:

```cmake
    tests/test_step_grid_lock.cpp
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
source env.sh && cmake -S . -B build && cmake --build build
```

Expected: FAIL at compile time — `no member named 'lane_rate_hz_for_test' in 'spky::SuperModulator'`.

- [ ] **Step 4: Extend `engine/mod/super_modulator.h`**

Add the include next to the existing ones at the top:

```cpp
#include "mod/lane_len.h"
```

In the public section, directly after `void set_step(bool on, int steps);`, add:

```cpp
    bool step_mode()  const { return _step_on; }
    int  deck_steps() const { return _deck_steps; }
```

Inside the existing `#ifdef SPKY_TESTING` block, add:

```cpp
    float lane_rate_hz_for_test(int i) const { return _lanes[i].rate_hz_for_test(); }
    int   lane_slots_for_test(int i)   const { return _lanes[i].steps(); }
```

In the private declarations, next to `void _update_tide();`, add:

```cpp
    void _apply_steps();
```

In the private data, next to `bool _synced = false;`, add:

```cpp
    bool  _step_on    = false;   // the deck's STEP flag; drives the grid lock
    int   _deck_steps = 8;       // the phrase length; PITCH's slot count
    float _shuffle    = 0.f;     // latched target, mirrored from set_shuffle
```

- [ ] **Step 5: Rewrite the rate and step wiring in `engine/mod/super_modulator.cpp`**

Replace `SuperModulator::_apply_rate` with:

```cpp
void SuperModulator::_apply_rate() {
    _master_hz = _base_hz * _pitch_scale;
    for (int i = 0; i < LANE_COUNT; ++i) {
        if (_step_on) {
            // STEP: one clock for the whole deck (spec 2026-07-25
            // mod-lane-step-grid-lock). kLaneRatio, TIDE and _mod_scale all
            // drop out here -- the first two reappear as slot counts in
            // _apply_steps(), the third is deliberately discarded, because a
            // texture rate that differs from PITCH's is exactly what used to
            // walk the lanes off the grid and never bring them back. The step
            // clock is normalized (_phase_inc = rate/sr * 8/steps), so equal
            // rate_hz means one shared grid whatever each cycle's length is.
            _lanes[i].set_rate_hz(_master_hz);
        } else {
            const float s = (i == LANE_PITCH) ? _pitch_scale
                                              : _mod_scale * _tide_mult;
            _lanes[i].set_rate_hz(_base_hz * s * kLaneRatio[i]);
        }
    }
}
```

Add `_apply_steps` directly below it:

```cpp
void SuperModulator::_apply_steps() {
    // In STEP each lane gets its own cycle length; in FLOW every lane keeps
    // the deck's phrase length, exactly as before. TIDE always uses the
    // rational ladder here, even when the global SYNC switch is Free -- a
    // continuous factor is the one TIDE setting that cannot be expressed as
    // an integer slot count.
    const float tide = kTideRatios[tide_index(_tide_norm)];
    for (int i = 0; i < LANE_COUNT; ++i) {
        const int slots = _step_on ? lane_slots(i, _deck_steps, tide)
                                   : _deck_steps;
        _lanes[i].set_step(_step_on, slots);
    }
}
```

Replace `SuperModulator::set_step` with:

```cpp
void SuperModulator::set_step(bool on, int n) {
    _step_on    = on;
    _deck_steps = n < 1 ? 1 : n;
    _apply_steps();
    _apply_rate();
}
```

Replace `SuperModulator::set_shuffle` with:

```cpp
void SuperModulator::set_shuffle(float amount) {
    _shuffle = shuffle_amount(amount);
    for (auto& l : _lanes) l.set_shuffle(amount);
}
```

Extend `SuperModulator::_update_tide` so a TIDE turn also re-derives the slot
counts:

```cpp
void SuperModulator::_update_tide() {
    _tide_mult = _synced ? kTideRatios[tide_index(_tide_norm)]
                         : tide_free(_tide_norm);
    _apply_steps();
    _apply_rate();
}
```

Note that `set_synced()` already calls `_update_tide()` followed by
`_update_rate()`, so the synced switch picks the slot counts up for free.

- [ ] **Step 6: Run the tests to verify they pass**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -tc="steplock:*"
```

Expected: `[doctest] Status: SUCCESS!` with 4 test cases passing.

- [ ] **Step 7: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass, including `super: lane rate ratios (x2, x1/2, x1, x3/4, x3/2)`
in `tests/test_super_modulator.cpp` — that case runs in FLOW and must be
untouched by this change. If it fails, the FLOW branch of `_apply_rate` was
altered; restore it verbatim.

- [ ] **Step 8: Commit**

```bash
git add engine/mod/super_modulator.h engine/mod/super_modulator.cpp \
        tests/test_step_grid_lock.cpp CMakeLists.txt
git commit -m "feat(mod): run every lane on the deck clock in STEP

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: Deck-wide STEP-entry snap

**Files:**
- Modify: `engine/mod/super_modulator.h`
- Modify: `engine/center/center.cpp:263-283` (the `_snap_phase` body)
- Modify: `tests/test_lane.cpp:162-181`
- Modify: `tests/test_step_grid_lock.cpp`

**Interfaces:**
- Consumes: `_step_on`, `_deck_steps`, `_shuffle` (Task 3).
- Produces: `void SuperModulator::snap_deck_phase(float ph)` — replaces
  `snap_pitch_phase(float)`, which is removed. In FLOW it behaves exactly as
  `snap_pitch_phase` did; in STEP it additionally places every texture lane on
  the same deck boundary.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_step_grid_lock.cpp`:

```cpp
TEST_CASE("steplock: STEP entry puts every lane on the same deck boundary") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(false, 8);
    for (int i = 0; i < 5000; ++i) m.process();   // let the lanes de-phase

    m.set_step(true, 8);
    m.snap_deck_phase(0.f);

    // Phase 0 is slot 0 for every slot count, so this is the assertion --
    // do not try to read it back through lane_fired(): the texture lanes only
    // report at their 96-sample raster edge, which after 5000 samples is 88
    // process() calls away.
    for (int i = 0; i < LANE_COUNT; ++i)
        CHECK(m.lane_phase(i) == doctest::Approx(0.f).epsilon(1e-6));
}

TEST_CASE("steplock: a mid-phrase snap folds into each lane's own cycle") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(true, 8);

    m.snap_deck_phase(0.25f);                     // deck step 2 of 8, frac 0

    // Step position 2 in a 4-slot lane is phase 2/4; in a 12-slot lane 2/12.
    CHECK(m.lane_phase(LANE_PITCH)  == doctest::Approx(0.25f).epsilon(1e-6));
    CHECK(m.lane_phase(LANE_SOURCE) == doctest::Approx(0.5f).epsilon(1e-6));
    CHECK(m.lane_phase(LANE_SIZE)   == doctest::Approx(2.f / 16.f).epsilon(1e-6));
    CHECK(m.lane_phase(LANE_MOTION) == doctest::Approx(2.f / 12.f).epsilon(1e-6));
    CHECK(m.lane_phase(LANE_LEVEL)  == doctest::Approx(2.f / 6.f).epsilon(1e-6));
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
source env.sh && cmake --build build
```

Expected: FAIL at compile time — `no member named 'snap_deck_phase' in 'spky::SuperModulator'`.

- [ ] **Step 3: Replace `snap_pitch_phase` in `engine/mod/super_modulator.h`**

Replace the whole `snap_pitch_phase` member — its long German comment block
included — with:

```cpp
    // STEP-Einstiegs-Snap (spec 2026-07-23 sampler-performance-fixes, in STEP
    // erweitert durch spec 2026-07-25 mod-lane-step-grid-lock). Bewusst nicht
    // reset_phases: das ist die RST-Geste, die beide Decks auf Phase 0 wirft.
    //
    // In FLOW setzt das hier weiterhin NUR die PITCH-Lane -- ein Sprung in
    // freilaufenden Texturlanes waere ein hoerbarer Ruck in Filter und Pan,
    // ohne dass er einem Raster nuetzt, das es in FLOW gar nicht gibt.
    //
    // In STEP teilen sich alle fuenf Lanes den Step-Clock des Decks, und dann
    // ist genau das Gegenteil richtig: eine Texturlane, die ihre alte Phase
    // behaelt, sitzt fuer immer neben dem Raster. Der Snap nimmt deshalb die
    // gebrochene Step-Position der Master-Lane und faltet sie in die eigene
    // Slot-Zahl jeder Texturlane. Der Ruck bleibt, er wird vom vorhandenen
    // SMOOTH-Slew abgefangen und liest sich in einem rhythmischen Deck als
    // Akzent.
    //
    // Der Onset-Gap-Ring wird mitgenullt, dieselbe Kopplung, auf der
    // reset_phases oben besteht: nach einem Phasensprung misst der erste
    // Onset sonst einen Abstand, den es nie gab, und dieser Rhythmus-Blick
    // steuert ueber Instrument die FX-Abgriffe des ANDEREN Decks. Das Nullen
    // hier setzt _rhythm.valid auf false (super_modulator.cpp, _onsets >= 3),
    // und derive_offsets (taps.cpp) liefert fuer ein ungueltiges RhythmView
    // kMuted auf beiden Taps -- das andere Deck verliert also seine beiden
    // Tape-Abgriffe, bis das snappende Deck erneut drei Onsets gezaehlt hat.
    // Das ist kein neuer Nebeneffekt: reset_phases loescht denselben Ring auf
    // dieselbe Weise, also traegt RST bereits denselben Preis.
    void snap_deck_phase(float ph);
```

- [ ] **Step 4: Implement it in `engine/mod/super_modulator.cpp`**

Add, directly after `SuperModulator::set_step`:

```cpp
void SuperModulator::snap_deck_phase(float ph) {
    _lanes[LANE_PITCH].reset(ph);
    if (_step_on) {
        // The deck's fractional step position is the one quantity every lane
        // shares. Folding it into each lane's slot count (fmod is inside
        // shuffle_phase_for_position) puts them all on the same boundary,
        // each at its own point in its own cycle.
        const int   deck = _deck_steps < 1 ? 1 : _deck_steps;
        const int   step = shuffle_step_index(ph, deck, _shuffle);
        const float frac = shuffle_step_fraction(ph, step, deck, _shuffle);
        const float pos  = static_cast<float>(step) + frac;
        for (int i = 0; i < LANE_COUNT; ++i) {
            if (i == LANE_PITCH) continue;
            _lanes[i].reset(shuffle_phase_for_position(
                pos, _lanes[i].steps(), _shuffle));
        }
    }
    _since_onset = 0;
    _onsets = 0;
    _gap[0] = _gap[1] = 0;
    _rhythm = RhythmView{};
}
```

- [ ] **Step 5: Update the one production call site**

In `engine/center/center.cpp`, inside `Center::_snap_phase`, change

```cpp
    m.snap_pitch_phase(tgt);
```

to

```cpp
    m.snap_deck_phase(tgt);
```

Confirm there are no others:

```bash
grep -rn "snap_pitch_phase" engine/ host/vcv/src/ host/render/ tests/
```

Expected: only `tests/test_lane.cpp` (handled in the next step).

- [ ] **Step 6: Update the existing FLOW snap test**

In `tests/test_lane.cpp`, replace the comment and case name at lines 162-175.
The behaviour under test is unchanged — in FLOW the snap still moves PITCH
alone — only the method name and the reason change:

```cpp
// snap_deck_phase setzt in FLOW die PITCH-Lane und NUR sie -- die vier
// Texturlanes laufen weiter, sonst waere es die RST-Geste (reset_phases)
// unter anderem Namen. In FLOW gibt es kein Raster, auf das ein Sprung sie
// bringen koennte; der STEP-Fall steht in tests/test_step_grid_lock.cpp.
// Der Onset-Gap-Ring wird mitgenullt: nach einem Phasensprung waere der
// naechste gemessene Abstand einer, den es nie gab, und dieser
// Rhythmus-Blick steuert die FX-Abgriffe des ANDEREN Decks.
TEST_CASE("mod: snap_deck_phase moves the pitch lane alone in FLOW") {
```

and the call at line 175:

```cpp
    m.snap_deck_phase(0.25f);
```

- [ ] **Step 7: Run the tests to verify they pass**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -tc="steplock:*,mod: snap_deck_phase*"
```

Expected: `[doctest] Status: SUCCESS!` with 7 test cases passing.

- [ ] **Step 8: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass, including `tests/test_instrument.cpp`'s onset-ring
cases, which depend on the snap clearing the ring.

- [ ] **Step 9: Commit**

```bash
git add engine/mod/super_modulator.h engine/mod/super_modulator.cpp \
        engine/center/center.cpp tests/test_lane.cpp tests/test_step_grid_lock.cpp
git commit -m "feat(mod): snap the whole deck onto the grid at STEP entry

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: EVOLVE inheritance and quantized SPOT

**Files:**
- Modify: `engine/mod/super_modulator.cpp` (`process`, `spot`)
- Modify: `tests/test_step_grid_lock.cpp`

**Interfaces:**
- Consumes: `ModLane::set_ev_rate_external`, `ModLane::ev_rate`,
  `ModLane::kick_steps` (Task 2); `_step_on` (Task 3).
- Produces: no new API.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_step_grid_lock.cpp`:

`Rng` needs no new include — `super_modulator.h` pulls in `mod/rng.h` through
`mod/lane.h`.

```cpp
namespace {
// The texture lanes advance on the 96-sample raster, so a boundary is reported
// at the tick that covers it, never at the boundary sample itself. A tick at
// sample f advances the lane across [f, f + kTickInterval), so the master's
// own step for the same boundary is reported at some s in [f, f + 96).
constexpr int kTick = ModLane::kTickInterval;

struct GridTrace {
    std::vector<int> deck_step;
    std::vector<int> fire[LANE_COUNT];
    int spot_sample = -1;
};

GridTrace run_deck(float shape, bool chaos, int samples) {
    SuperModulator m;
    m.init(48000.f, 99u);
    m.set_rate(0.45f);
    m.set_step(true, 8);
    m.set_shape(shape);
    m.set_smooth(0.f);
    m.set_tide(0.25f);                       // off-centre ladder rung
    if (chaos) {
        m.set_variation(0.8f);               // EVOLVE rate walk per lane
        m.set_rate_scale(1.f, 1.6f);         // DRIFT: mod_scale != pitch_scale
        m.set_shuffle(0.6f);
    }
    Rng spot_rng;
    spot_rng.seed(5u);

    GridTrace t;
    int last_step = m.pitch_cur_step();
    for (int i = 0; i < samples; ++i) {
        if (chaos && i == samples / 3) { m.spot(spot_rng); t.spot_sample = i; }
        m.process();
        if (m.pitch_cur_step() != last_step) {
            last_step = m.pitch_cur_step();
            t.deck_step.push_back(i);
        }
        // fired() latches for the whole control interval, so only the raster
        // edge itself carries a fresh verdict.
        if (i % kTick == 0)
            for (int l = 0; l < LANE_COUNT; ++l)
                if (l != LANE_PITCH && m.lane_fired(l)) t.fire[l].push_back(i);
    }
    return t;
}

bool on_grid(const GridTrace& t) {
    for (int l = 0; l < LANE_COUNT; ++l) {
        if (l == LANE_PITCH) continue;
        for (int f : t.fire[l]) {
            // SPOT's jump fires one boundary at the moment of the kick. That
            // is the stumble itself, not drift -- skip the raster edge that
            // reports it, and demand the grid everywhere else.
            if (t.spot_sample >= 0 &&
                f >= t.spot_sample && f - t.spot_sample <= kTick) continue;
            bool ok = false;
            for (int s : t.deck_step)
                if (s >= f && s - f < kTick) { ok = true; break; }
            if (!ok) return false;
        }
    }
    return true;
}
} // namespace

TEST_CASE("steplock: the grid survives EVOLVE, DRIFT, TIDE, SHUFFLE and SPOT") {
    const GridTrace t = run_deck(0.5f, true, 240000);
    REQUIRE(t.deck_step.size() >= 8);
    for (int l = 0; l < LANE_COUNT; ++l)
        if (l != LANE_PITCH) REQUIRE(t.fire[l].size() >= 8);
    CHECK(on_grid(t));
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -tc="steplock: the grid survives*"
```

Expected: FAIL — `CHECK( on_grid(t) )` is false. The per-lane EVOLVE walk and
SPOT's raw phase kick are still pushing the texture lanes off the grid.

- [ ] **Step 3: Inherit the master's rate walk in `process()`**

In `engine/mod/super_modulator.cpp`, in `SuperModulator::process()`, directly
after the line `_out[LANE_PITCH] = _lanes[LANE_PITCH].process();` insert:

```cpp
    // STEP grid lock (spec 2026-07-25): the texture lanes take the master's
    // EVOLVE rate walk instead of their own, so no lane can wander off the
    // shared step clock. Pushed every sample rather than at a wrap because the
    // master's walk changes at ITS wrap, which the texture lanes do not see.
    // In FLOW the flag goes false and each lane is back on its own walk.
    {
        const float ev = _lanes[LANE_PITCH].ev_rate();
        for (int i = 0; i < LANE_COUNT; ++i)
            if (i != LANE_PITCH) _lanes[i].set_ev_rate_external(_step_on, ev);
    }
```

- [ ] **Step 4: Quantize SPOT's phase kick**

Replace `SuperModulator::spot` with:

```cpp
void SuperModulator::spot(Rng& rng) {
    // SPOT stumbles every lane EXCEPT the PITCH master lane: the melody is the
    // anchor everything else stumbles around, so pitch is never a target of the
    // stumble. Draw a kick for every lane so the RNG stream stays independent
    // of which lanes are skipped.
    for (int i = 0; i < LANE_COUNT; ++i) {
        float dphase = rng.next_bipolar() * 0.5f;    // uniform +/- 1/2 cycle
        float dshape = rng.next_bipolar() * 0.35f;   // uniform +/- 0.35
        if (i == LANE_PITCH) continue;
        if (_step_on) {
            // In STEP the same gesture is rounded to whole slots (spec
            // 2026-07-25 mod-lane-step-grid-lock). Still a stumble, but one
            // that lands on the deck's grid instead of permanently beside it.
            //
            // Rounded to an EVEN count, because SHUFFLE keys its warp to step
            // parity: an odd jump would put the lane on the opposite swing
            // from the deck and leave it up to a third of a step out. The lost
            // resolution is one slot out of a +-slots/2 range.
            const float slots = static_cast<float>(_lanes[i].steps());
            const int   n = 2 * static_cast<int>(
                std::lround(dphase * slots * 0.5f));
            _lanes[i].kick_steps(n, dshape);
        } else {
            _lanes[i].kick(dphase, dshape);
        }
    }
}
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -tc="steplock:*"
```

Expected: `[doctest] Status: SUCCESS!` with 7 test cases passing.

- [ ] **Step 6: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass. `tests/test_center.cpp`'s SPOT cases run their decks
in FLOW, where the raw `kick()` path is unchanged.

- [ ] **Step 7: Commit**

```bash
git add engine/mod/super_modulator.cpp tests/test_step_grid_lock.cpp
git commit -m "feat(mod): inherit the master rate walk and quantize SPOT in STEP

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: Shape independence, live STEPS turn, docs

**Files:**
- Modify: `tests/test_step_grid_lock.cpp`
- Modify: `docs/roadmap.md:53` (insert a row after the `+ FORM/SONG` row)

**Interfaces:**
- Consumes: `run_deck` / `on_grid` from Task 5's anonymous namespace in the
  same file.
- Produces: nothing.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_step_grid_lock.cpp`:

```cpp
TEST_CASE("steplock: the fire grid is identical at every SHAPE setting") {
    // This is the whole point of the change: SHAPE was the detector, never the
    // cause. At the S&H end an offset reads as different random values; left
    // of it, as a ramp stepping beside the beat.
    const GridTrace ref = run_deck(1.f, true, 240000);
    for (float shape : {0.f, 0.25f, 0.5f, 0.75f}) {
        const GridTrace t = run_deck(shape, true, 240000);
        CHECK(t.deck_step == ref.deck_step);
        for (int l = 0; l < LANE_COUNT; ++l)
            if (l != LANE_PITCH) CHECK(t.fire[l] == ref.fire[l]);
    }
}

TEST_CASE("steplock: a live STEPS turn keeps the deck aligned") {
    SuperModulator m;
    m.init(48000.f, 99u);
    m.set_rate(0.45f);
    m.set_step(true, 8);

    GridTrace t;
    int last_step = m.pitch_cur_step();
    for (int i = 0; i < 240000; ++i) {
        if (i ==  80000) m.set_step(true, 16);
        if (i == 160000) m.set_step(true, 8);
        m.process();
        if (m.pitch_cur_step() != last_step) {
            last_step = m.pitch_cur_step();
            t.deck_step.push_back(i);
        }
        if (i % kTick == 0)
            for (int l = 0; l < LANE_COUNT; ++l)
                if (l != LANE_PITCH && m.lane_fired(l)) t.fire[l].push_back(i);
    }
    REQUIRE(t.deck_step.size() >= 8);
    CHECK(on_grid(t));
}

TEST_CASE("steplock: FLOW is untouched by the grid lock") {
    // The texture lanes must still run at their old ratios off the master.
    SuperModulator m;
    m.init(48000.f, 42u);
    m.set_rate(0.3f);
    m.set_step(false, 8);
    for (int i = 0; i < ModLane::kTickInterval; ++i) m.process();
    const float pitch = m.lane_phase(LANE_PITCH);
    CHECK(m.lane_phase(LANE_SOURCE) == doctest::Approx(pitch * 2.00f));
    CHECK(m.lane_phase(LANE_SIZE)   == doctest::Approx(pitch * 0.50f));
    CHECK(m.lane_phase(LANE_MOTION) == doctest::Approx(pitch * 0.75f));
    CHECK(m.lane_phase(LANE_LEVEL)  == doctest::Approx(pitch * 1.50f));
}
```

- [ ] **Step 2: Run the tests**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -tc="steplock:*"
```

Expected: `[doctest] Status: SUCCESS!` with 10 test cases passing. If the
shape-independence case fails, a timing path is reading SHAPE — check that
nothing added in Tasks 2-5 feeds `_shape`, `_ev_shape`, `_shape_offset` or
`_kick_shape` into a phase or boundary computation.

- [ ] **Step 3: Red-prove the grid invariant**

A guard that has never failed is not a guard. Temporarily revert the STEP
branch of `_apply_rate` in `engine/mod/super_modulator.cpp` to the FLOW
formula:

```cpp
        if (false) {
```

Then run:

```bash
cmake --build build && ./build/spky_tests.exe -tc="steplock: the grid survives*"
```

Expected: FAIL on `CHECK( on_grid(t) )`. Restore `if (_step_on) {` and re-run
to confirm SUCCESS. Do not commit the temporary edit.

- [ ] **Step 4: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 5: Update the roadmap**

In `docs/roadmap.md`, insert a row directly after the `| **+ FORM/SONG** | ... |`
row (line 53) and before the `| **M5j** |` row:

```markdown
| **Mod grid lock** | In STEP the four texture lanes run on the deck's step clock; the lane ratios become cycle lengths (4/6/8/12/16 at STEPS = 8), TIDE stretches slot counts, and DRIFT, EVOLVE and SPOT can no longer push a lane off the grid | ✅ **done** (engine; spec `docs/superpowers/specs/2026-07-25-mod-lane-step-grid-lock-design.md`) |
```

- [ ] **Step 6: Commit**

```bash
git add tests/test_step_grid_lock.cpp docs/roadmap.md
git commit -m "test(mod): pin shape independence and the live STEPS turn

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Verification

After Task 6, from the fork root:

```bash
source env.sh
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: every test passes, including the pre-existing `test_lane_tick`,
`test_step_clock`, `test_super_modulator`, `test_center` and `test_instrument`
suites.

Then build the VCV plugin and play a deck in STEP with SHAPE swept end to end,
DRIFT up, VARIATION toward GROW and SPOT triggered:

```bash
cd host/vcv && ./build-local.sh
```

Never hand-roll the VCV build — the system `g++` on this machine is the ARM
cross-compiler and fails with "MinGW not found".
