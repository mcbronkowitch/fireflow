# Modulation Lane Grid Lock in STEP — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** In STEP mode the four texture modulation lanes stop owning a clock
and read the deck's integer step count instead, so they cannot drift off the
step grid at any SHAPE setting.

**Architecture:** `SuperModulator` counts the steps the PITCH lane enters and
hands each texture lane that count plus the fraction the deck sits at inside
its current step. A texture lane's position is `count mod slots` plus that
fraction — one integer modulo and one shared float, so no lane can round away
from another. The old rate ratio becomes the lane's slot count; TIDE scales
slot counts instead of rates.

**Tech Stack:** C++17, doctest (vendored in `third_party/`), CMake + Ninja +
clang. Engine only — no VCV panel change, no new control.

**Spec:** `docs/superpowers/specs/2026-07-25-mod-lane-step-grid-lock-design.md`

**History:** Task 1 is done (`51f65c7`). An earlier Task 2 gave every lane the
same `rate_hz` and let each keep its own phasor; it was reverted (`370206e`)
after measurement showed float32 accumulation makes lanes with different cycle
lengths drift ~2 samples per step — a full step of slip in about 90 seconds,
worst at SIZE's default 16 slots. The spec's "Why not equal rates" section
carries the numbers. Do not reintroduce a per-lane phasor in STEP.

## Global Constraints

- Build environment: source `env.sh` at the fork root before any `cmake`/`ctest`.
  It sets `PATH` to LLVM + ninja and `CC=clang CXX=clang++ CMAKE_GENERATOR=Ninja`.
  This machine has no MSVC and no native GCC.
- Configure/build/test: `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`.
  Ninja is single-config; the binary is at `build/spky_tests.exe`.
- Run one doctest case: `./build/spky_tests.exe -tc="<case name>"`.
- The baseline is green: 4/4 ctest, including the `ctrl_identity` render gate,
  whose stale reference was refreshed in `2278a53`. Any render-gate movement
  from here is caused by this work and must be explained, not re-baselined.
- Patch compatibility is out of scope; the instrument is in development.
- FLOW behaviour must be unchanged by every task in this plan.
- Integer types follow the codebase: `int32_t`, not `int64_t`. The deck step
  count runs at most a few hundred per second and this engine ships on a
  Cortex-M7 — do not introduce 64-bit arithmetic into the mod plane.
- Commit trailer for every commit:
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- New test files must be added to the `spky_tests` source list in
  `CMakeLists.txt` or they will not run.

## File Structure

| File | Responsibility |
|---|---|
| `engine/mod/lane_len.h` | DONE (Task 1). The STEP cycle-length table and `lane_slots()`. |
| `engine/mod/lane.h` / `lane.cpp` (modify) | The follower mode: a lane that is told where the deck is instead of integrating a phase. |
| `engine/mod/super_modulator.h` / `.cpp` (modify) | Owns the STEP decision: the deck step count, per-lane slot counts, follow dispatch, SPOT's slot offset. |
| `tests/test_lane_follow.cpp` (create) | ModLane-level: follow fires on deck steps, multi-step catch-up, slot nudge, exactness over a long horizon. |
| `tests/test_step_grid_lock.cpp` (create) | SuperModulator-level: rates and slots, the grid invariant under chaos, ten-minute lock, shape independence, live STEPS turn, FLOW regression. |
| `docs/roadmap.md` (modify) | One row. |

`engine/center/center.cpp` is deliberately absent: a follower has no rate, so
`mod_scale` has nothing to act on and the DRIFT/COUPLE call sites need no
change.

---

### Task 2: ModLane follower mode

**Files:**
- Modify: `engine/mod/lane.h`
- Modify: `engine/mod/lane.cpp`
- Create: `tests/test_lane_follow.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces, on `spky::ModLane`:
  - `float follow(int32_t deck_step, float frac, float shuffle)` — advance this lane to the
    deck's position and return the post-range output, the follower twin of
    `tick()`.
  - `void nudge_slots(int n, float dshape)` — SPOT: shift this lane's slot
    offset by `n`, fire the new slot at the next `follow()`.
  - `float rate_hz_for_test() const` — under `SPKY_TESTING` only.

- [ ] **Step 1: Write the failing test**

Create `tests/test_lane_follow.cpp`:

```cpp
#include <doctest/doctest.h>
#include <vector>
#include "mod/lane.h"
using namespace spky;

// Follower mode (spec 2026-07-25 mod-lane-step-grid-lock). A lane in STEP
// holds no clock: it is told the deck's cumulative step count and where the
// deck sits inside its current step, and derives everything from that. These
// tests drive follow() with hand-written integer counts, so they pin the
// contract without a SuperModulator and without any float clock at all.
namespace {
constexpr float kSr = 48000.f;

void configure(ModLane& l, int slots) {
    l.set_melodic(false);
    l.init(kSr, 4242u);
    l.set_step(true, slots);
    l.set_rate_hz(2.f);        // assigned but unused by a follower
    l.set_shape(1.f);
    l.set_smooth(0.f);
}
} // namespace

TEST_CASE("follow: one deck step is one slot, whatever the cycle length") {
    for (int slots : {2, 3, 4, 6, 8, 12, 16, 32}) {
        ModLane l;
        configure(l, slots);
        int fires = 0;
        for (int32_t s = 0; s < 200; ++s) {
            l.follow(s, 0.f, 0.f);
            if (l.fired()) ++fires;
        }
        CHECK(fires == 200);          // exactly one boundary per deck step
    }
}

TEST_CASE("follow: the slot index is the deck count modulo the cycle") {
    ModLane l;
    configure(l, 6);
    for (int32_t s = 0; s < 40; ++s) {
        l.follow(s, 0.f, 0.f);
        CHECK(l.cur_step() == static_cast<int>(s % 6));
    }
}

TEST_CASE("follow: repeat calls inside one deck step do not re-fire") {
    ModLane l;
    configure(l, 8);
    l.follow(0, 0.f, 0.f);
    REQUIRE(l.fired());
    for (float frac : {0.25f, 0.5f, 0.75f, 0.99f}) {
        l.follow(0, frac, 0.f);
        CHECK_FALSE(l.fired());
        CHECK(l.cur_step() == 0);
    }
    l.follow(1, 0.f, 0.f);
    CHECK(l.fired());
}

TEST_CASE("follow: a multi-step advance replays every slot in order") {
    // The raster window normally holds at most one deck step, but COUPLE and
    // DRIFT can push pitch_scale up. A skipped slot would drop a wrap event.
    ModLane l;
    configure(l, 4);
    l.follow(0, 0.f, 0.f);
    std::vector<int> seen;
    for (int32_t s = 3; s <= 15; s += 3) {          // three slots per call
        l.follow(s, 0.f, 0.f);
        seen.push_back(l.cur_step());
    }
    // Landing slots after 3, 6, 9, 12, 15 deck steps in a 4-slot cycle.
    CHECK(seen == std::vector<int>{3, 2, 1, 0, 3});
}

TEST_CASE("follow: wrapped() marks the cycle seam, once per cycle") {
    ModLane l;
    configure(l, 4);
    for (int32_t s = 0; s < 13; ++s) {
        l.follow(s, 0.f, 0.f);
        // s == 0 is the cold start. It enters slot 0, but no cycle ended, so
        // no wrap runs -- the same choice tick() makes at its own cold start.
        CHECK(l.wrapped() == (s != 0 && s % 4 == 0));
    }
}

TEST_CASE("follow: arming never runs a cycle wrap, whatever slot it lands on") {
    // Wrap events evolve the pattern that just ENDED. At a cold start none
    // did, and STEP entry restarts the deck count at 0 -- so without this,
    // every switch into STEP would walk EVOLVE once on all four texture lanes
    // and burn RNG draws for a phrase nobody heard.
    for (int32_t start : {0, 1, 4, 8, 13}) {
        ModLane l;
        configure(l, 4);
        l.set_variation(0.9f);        // GROW: a wrap here would walk _ev_*
        l.follow(start, 0.f, 0.f);
        CHECK_FALSE(l.wrapped());
        CHECK(l.fired());
        CHECK(l.cur_step() == static_cast<int>(start % 4));
    }
}

TEST_CASE("follow: a slot nudge offsets the lane and fires immediately") {
    ModLane l;
    configure(l, 8);
    for (int32_t s = 0; s <= 4; ++s) l.follow(s, 0.f, 0.f);
    REQUIRE(l.cur_step() == 4);

    l.nudge_slots(3, 0.f);
    l.follow(4, 0.5f, 0.f);                 // same deck step, mid-step
    CHECK(l.fired());                  // the stumble is audible at once
    CHECK(l.cur_step() == 7);

    l.follow(5, 0.f, 0.f);                  // the offset persists
    CHECK(l.cur_step() == 0);
}

TEST_CASE("follow: a zero-slot nudge is a shape kick and nothing more") {
    ModLane l;
    configure(l, 8);
    for (int32_t s = 0; s <= 4; ++s) l.follow(s, 0.f, 0.f);
    REQUIRE(l.cur_step() == 4);

    l.nudge_slots(0, 0.2f);
    l.follow(4, 0.5f, 0.f);            // same deck step: nothing moved, nothing fires
    CHECK_FALSE(l.fired());
    CHECK(l.cur_step() == 4);

    l.nudge_slots(2, 0.f);             // a real move in the same situation does
    l.follow(4, 0.6f, 0.f);
    CHECK(l.fired());
    CHECK(l.cur_step() == 6);
}

TEST_CASE("follow: a negative nudge does not stall the lane") {
    ModLane l;
    configure(l, 8);
    for (int32_t s = 0; s <= 4; ++s) l.follow(s, 0.f, 0.f);

    l.nudge_slots(-3, 0.f);
    l.follow(4, 0.5f, 0.f);
    CHECK(l.cur_step() == 1);
    int fires = 0;
    for (int32_t s = 5; s < 15; ++s) { l.follow(s, 0.f, 0.f); if (l.fired()) ++fires; }
    CHECK(fires == 10);                // still one boundary per deck step
}

TEST_CASE("follow: two lanes of different length never diverge") {
    // The whole point of the design. An equal-rate implementation drifts about
    // two samples per step here; a follower cannot, because the position is an
    // integer modulo of one shared count.
    ModLane a, b;
    configure(a, 8);
    configure(b, 16);
    int fa = 0, fb = 0;
    for (int32_t s = 0; s < 200000; ++s) {     // ~7 hours of 8-step bars
        a.follow(s, 0.f, 0.f);
        b.follow(s, 0.f, 0.f);
        if (a.fired()) ++fa;
        if (b.fired()) ++fb;
    }
    CHECK(fa == 200000);
    CHECK(fb == 200000);
    CHECK(a.cur_step() == static_cast<int>((200000 - 1) % 8));
    CHECK(b.cur_step() == static_cast<int>((200000 - 1) % 16));
}
```

- [ ] **Step 2: Register the test file**

In `CMakeLists.txt`, inside `add_executable(spky_tests ...)`, add directly
after `tests/test_step_clock.cpp`:

```cmake
    tests/test_lane_follow.cpp
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
source env.sh && cmake -S . -B build && cmake --build build
```

Expected: FAIL at compile time — `no member named 'follow' in 'spky::ModLane'`.

- [ ] **Step 4: Add the public API in `engine/mod/lane.h`**

In the `#ifdef SPKY_TESTING` block (currently holding `pattern_for_test`,
`cadence_slot_for_test`, `bound_a_opening_for_test`), add:

```cpp
    float rate_hz_for_test() const { return _rate_hz; }
```

Directly after the existing `void kick(float dphase, float dshape);` in the
`--- M4 center hooks ---` section, add:

```cpp
    // --- STEP follower mode (spec 2026-07-25 mod-lane-step-grid-lock) ---
    //
    // In STEP a texture lane owns no clock. Instead of integrating a phase it
    // is told where the deck is: `deck_step` is the cumulative count of steps
    // the master lane has entered, `frac` where the deck currently sits inside
    // its step. This lane's position is (deck_step + offset) mod _steps plus
    // that fraction.
    //
    // That is not a cheaper way to stay aligned, it is the only exact one.
    // Five float phasors at the same nominal rate round differently depending
    // on how large their phase gets, so lanes with different cycle lengths
    // drift apart -- measured at ~2 samples per 3000-sample step between an
    // 8-slot and a 16-slot lane, a full step of slip in about 90 seconds. One
    // integer count and one shared fraction cannot do that.
    //
    // Returns the post-range output, exactly like tick() does for FLOW.
    float follow(int32_t deck_step, float frac, float shuffle);
    // `shuffle` is the amount the DECK's grid was built with, not this lane's
    // own _shuffle_latched. In STEP a follower's latch is irrelevant: its
    // boundary times come from the deck, so its slot-to-phase mapping has to
    // use the deck's amount or the mapping and `frac` disagree -- and a live
    // SHUFFLE turn on an odd step would then clamp the phase to a slot edge.
    //
    // SPOT in STEP: shift this lane by `n` whole slots. The offset persists
    // and is exact; the new slot fires at the next follow() call, which is the
    // audible stumble. No rounding or parity care is needed -- boundary times
    // come from the deck, never from this lane's own warp.
    //
    // A draw that rounds to n == 0 is a shape kick and nothing more. It must
    // NOT force a boundary: re-entering the slot the lane is already on would
    // fire a stumble that moves nothing and, under GROW, spend an RNG draw on
    // it. FLOW behaves the same way -- kick() with a near-zero dphase applies
    // the shape offset and leaves the clock alone.
    void  nudge_slots(int n, float dshape);
```

In the private section, next to the other M4 center-hook state, add:

```cpp
    // Follower state. _follow_pos is the last absolute position this lane was
    // advanced to (deck count + offset), _follow_armed false until the first
    // follow() call after init/reset/STEP entry so that entering STEP does not
    // replay history. _follow_jumped makes a nudge audible on the next call
    // even when no deck step has elapsed.
    int32_t _follow_pos    = 0;
    int32_t _follow_offset = 0;
    bool    _follow_armed  = false;
    bool    _follow_jumped = false;
```

- [ ] **Step 5: Implement the follower in `engine/mod/lane.cpp`**

Add near the top of the file, below the existing anonymous helpers:

```cpp
// Positive modulo: _follow_offset can be negative after a backwards SPOT
// nudge, and C++'s % keeps the sign of the dividend.
static int slot_of(int32_t pos, int slots) {
    int m = static_cast<int>(pos % slots);
    if (m < 0) m += slots;
    return m;
}
```

Add, directly after the existing `ModLane::kick` definition:

```cpp
void ModLane::nudge_slots(int n, float dshape) {
    // Move the offset AND the remembered position by the same amount: the
    // jump must not look like elapsed time, or the next follow() would replay
    // n slots (or, for a negative n, stall until the deck caught back up).
    _follow_offset += n;
    _follow_pos    += n;
    // Only a real move fires. See the header comment: a zero-slot draw is a
    // shape kick, not a stumble.
    if (n != 0) _follow_jumped = true;
    _kick_shape    += dshape;
}

float ModLane::follow(int32_t deck_step, float frac, float shuffle) {
    _fired   = false;
    _wrapped = false;
    _apply_preroll_work();
    _kick_shape *= _kick_coef_tick;
    if (_settle_ctr > 0) {
        _settle_ctr = _settle_ctr > kTickInterval ? _settle_ctr - kTickInterval : 0;
        _ev_phase   *= _settle_coef_tick;
        _ev_shape   *= _settle_coef_tick;
        _ev_rate    *= _settle_coef_tick;
        _kick_shape *= _settle_coef_tick;
    }

    const int     slots = _steps < 1 ? 1 : _steps;
    const int32_t pos   = deck_step + _follow_offset;
    const int     here  = slot_of(pos, slots);

    bool    land_only = false;
    int32_t elapsed   = 0;
    if (!_follow_armed) {
        // First call after init/reset/STEP entry. Land on the current position
        // without replaying every step the deck has taken since it started --
        // and, just as important, WITHOUT running a wrap even when the landing
        // slot is 0. Wrap events evolve the pattern that just ENDED
        // (_evolve_outgoing_pattern); at a cold start none did. STEP entry
        // restarts the deck count at 0, so treating that as a wrap would walk
        // EVOLVE once on all four texture lanes every time the switch is
        // thrown. tick() makes the same choice at its own cold start: it
        // enters the step its phase points at and runs no wrap events.
        _follow_armed = true;
        land_only     = true;
    } else {
        elapsed = pos - _follow_pos;
        if (elapsed < 0) elapsed = 0;          // only nudge_slots moves backwards
        // A jump this large means the caller skipped a long stretch (a stopped
        // transport, a mode switch). Replaying it would burn RNG draws for
        // cycles nobody heard; land on the current slot instead. Same spirit
        // as tick()'s edge-walk guard.
        if (elapsed > 2 * kSeqSlots) land_only = true;
    }

    bool entered = false;
    if (land_only) {
        _phase = shuffle_phase_for_position(
            static_cast<float>(here), slots, shuffle);
        _enter_step(here);
        entered = true;
    } else {
        const int32_t prev = pos - elapsed;
        for (int32_t k = 1; k <= elapsed; ++k) {
            const int slot = slot_of(prev + k, slots);
            // Boundary targets are evaluated at the exact grid phase, the same
            // sampling tick() documents for its edge walk.
            _phase = shuffle_phase_for_position(
                static_cast<float>(slot), slots, shuffle);
            if (slot == 0) {
                _wrapped = true;
                _wrap_events();      // before the new cycle's step 0, as in tick()
            }
            _enter_step(slot);
            entered = true;
        }
    }
    _follow_pos = pos;

    if (_follow_jumped) {
        _follow_jumped = false;
        if (!entered) {
            _phase = shuffle_phase_for_position(
                static_cast<float>(here), slots, shuffle);
            _enter_step(here);
        }
    }

    // Park at the live position so phase(), phase_eff() and any external
    // reader see where the lane actually is inside its slot.
    _phase = shuffle_phase_for_position(
        static_cast<float>(here) + frac, slots, shuffle);

    float smoothed = _slew_tick.process(_target);
    return apply_range(smoothed, _range);
}
```

- [ ] **Step 6: Clear the follower state in `init()`, `reset()` and STEP entry**

In `ModLane::init()`, directly after the line `_ev_rate  = 0.f;`, add:

```cpp
    _follow_pos    = 0;
    _follow_offset = 0;
    _follow_armed  = false;
    _follow_jumped = false;
```

In `ModLane::reset()`, directly after `_cur_step = -1;`, add:

```cpp
    // RST is the resync gesture: it clears the SPOT offset too, so the lane
    // comes back to the deck's own slot 0 rather than to a stumbled one.
    _follow_pos    = 0;
    _follow_offset = 0;
    _follow_armed  = false;
    _follow_jumped = false;
```

In `ModLane::set_step()`, inside the existing `if (entering_step)` handling —
directly after the line `if (entering_step) { _note_age = 0; _note_hold = 0; }`
— add:

```cpp
    // Entering STEP disarms the follower so its first follow() call lands on
    // the deck's current position instead of replaying the whole count.
    if (entering_step) { _follow_armed = false; _follow_jumped = false; }
```

- [ ] **Step 7: Run the tests to verify they pass**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -tc="follow:*"
```

Expected: `[doctest] Status: SUCCESS!` with 10 test cases passing.

- [ ] **Step 8: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 4/4. Nothing calls `follow()` yet, so every existing suite —
`test_lane_tick` in particular — must be untouched.

- [ ] **Step 9: Commit**

```bash
git add engine/mod/lane.h engine/mod/lane.cpp tests/test_lane_follow.cpp CMakeLists.txt
git commit -m "feat(mod): give ModLane a follower mode

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: SuperModulator — deck step count and follow dispatch

**Files:**
- Modify: `engine/mod/super_modulator.h`
- Modify: `engine/mod/super_modulator.cpp`
- Create: `tests/test_step_grid_lock.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `spky::lane_slots` (Task 1), `ModLane::follow`,
  `ModLane::rate_hz_for_test` (Task 2).
- Produces, on `spky::SuperModulator`:
  - `bool step_mode() const`, `int deck_steps() const`
  - under `SPKY_TESTING`: `float lane_rate_hz_for_test(int i) const`,
    `int lane_slots_for_test(int i) const`, `int32_t deck_step_for_test() const`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_step_grid_lock.cpp`:

```cpp
#include <doctest/doctest.h>
#include <vector>
#include "mod/super_modulator.h"
#include "mod/lane_len.h"
#include "mod/divisions.h"
using namespace spky;

TEST_CASE("steplock: STEP gives each lane its own slot count") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(true, 8);

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
    CHECK(m.lane_rate_hz_for_test(LANE_PITCH) == doctest::Approx(r));
    CHECK(m.lane_slots_for_test(LANE_SOURCE) ==  8);
    CHECK(m.lane_slots_for_test(LANE_SIZE)   == 32);
    CHECK(m.lane_slots_for_test(LANE_MOTION) == 24);
    CHECK(m.lane_slots_for_test(LANE_LEVEL)  == 12);
    CHECK(m.lane_slots_for_test(LANE_PITCH)  ==  8);   // the phrase, always
}

TEST_CASE("steplock: the deck step count follows the pitch lane") {
    SuperModulator m;
    m.init(48000.f, 7u);
    m.set_rate(0.5f);
    m.set_step(true, 8);

    int last = m.pitch_cur_step();
    int changes = 0;
    for (int i = 0; i < 200000; ++i) {
        m.process();
        if (m.pitch_cur_step() != last) { last = m.pitch_cur_step(); ++changes; }
    }
    REQUIRE(changes > 8);
    // The count starts at 0 on the first step, so it trails the change count
    // by exactly one.
    CHECK(m.deck_step_for_test() == changes - 1);
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

TEST_CASE("steplock: FLOW lane ratios are unchanged on the phase") {
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

- [ ] **Step 2: Register the test file**

In `CMakeLists.txt`, add directly after `tests/test_super_modulator.cpp`:

```cmake
    tests/test_step_grid_lock.cpp
```

- [ ] **Step 3: Run the test to verify it fails**

```bash
source env.sh && cmake -S . -B build && cmake --build build
```

Expected: FAIL at compile time — `no member named 'lane_slots_for_test' in 'spky::SuperModulator'`.

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
    float   lane_rate_hz_for_test(int i) const { return _lanes[i].rate_hz_for_test(); }
    int     lane_slots_for_test(int i)   const { return _lanes[i].steps(); }
    int32_t deck_step_for_test()         const { return _deck_step; }
```

In the private declarations, next to `void _update_tide();`, add:

```cpp
    void _apply_steps();
```

In the private data, next to `bool _synced = false;`, add:

```cpp
    bool    _step_on    = false;   // the deck's STEP flag; drives the grid lock
    int     _deck_steps = 8;       // the phrase length; PITCH's slot count
    // The deck's own clock, in whole steps. This integer is what makes the
    // grid exact: every follower derives its slot from it, so no float
    // rounding can put two lanes on different boundaries. int32_t is ample --
    // a few hundred steps per second overflows in centuries -- and this engine
    // ships on a Cortex-M7, where 64-bit arithmetic is not free.
    int32_t _deck_step       = 0;
    int     _last_pitch_step = -1;
```

- [ ] **Step 5: Wire the slot counts and the rates in `engine/mod/super_modulator.cpp`**

Replace `SuperModulator::_apply_rate` with:

```cpp
void SuperModulator::_apply_rate() {
    _master_hz = _base_hz * _pitch_scale;
    for (int i = 0; i < LANE_COUNT; ++i) {
        if (_step_on) {
            // STEP: the PITCH lane is the deck's only clock. The texture lanes
            // are followers and consume no rate at all -- kLaneRatio and TIDE
            // reappear as slot counts in _apply_steps(), and _mod_scale has
            // nothing left to scale. They are still handed the master rate so
            // nothing reads a stale value across a mode switch.
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
    const bool entering = on && !_step_on;
    _step_on    = on;
    _deck_steps = n < 1 ? 1 : n;
    if (entering) { _deck_step = 0; _last_pitch_step = -1; }
    _apply_steps();
    _apply_rate();
}
```

`SuperModulator::set_shuffle` is unchanged: it forwards to the lanes and
keeps no copy of its own. The follow path reads the PITCH lane's latched
amount instead, which is the only one that matches the phase it measures.

Extend `SuperModulator::_update_tide` so a TIDE turn re-derives the slot counts:

```cpp
void SuperModulator::_update_tide() {
    _tide_mult = _synced ? kTideRatios[tide_index(_tide_norm)]
                         : tide_free(_tide_norm);
    _apply_steps();
    _apply_rate();
}
```

`set_synced()` already calls `_update_tide()` followed by `_update_rate()`, so
the synced switch picks the slot counts up for free.

- [ ] **Step 6: Count the deck's steps and dispatch follow in `process()`**

In `SuperModulator::process()`, directly after the line
`_out[LANE_PITCH] = _lanes[LANE_PITCH].process();` insert:

```cpp
    // The deck's step clock. Counted from the master lane's own step index
    // rather than from lane_fired(), because a gated melodic step still
    // advances the grid -- the followers must move even when the melody rests.
    if (_step_on) {
        const int cs = _lanes[LANE_PITCH].cur_step();
        if (cs != _last_pitch_step) {
            if (_last_pitch_step >= 0) ++_deck_step;
            _last_pitch_step = cs;
        }
    }
```

Then replace the texture-lane tick block at the end of `process()` with:

```cpp
    if (_tick_ctr == 0) {
        _tick_ctr = ModLane::kTickInterval;
        const int   deck = _deck_steps < 1 ? 1 : _deck_steps;
        const int   ps   = _lanes[LANE_PITCH].cur_step();
        // The amount the PITCH lane's grid was actually built with.
        // _shuffle_target only reaches _shuffle_latched at an even step entry,
        // so a live SHUFFLE turn leaves them apart for up to a step -- reading
        // the target here would measure `frac` against boundaries that never
        // existed and clamp the followers to a slot edge.
        const float sh   = _lanes[LANE_PITCH].shuffle_latched();
        const float frac = ps < 0 ? 0.f
            : shuffle_step_fraction(
                  _lanes[LANE_PITCH].phase(), ps, deck, sh);
        for (int i = 0; i < LANE_COUNT; ++i) {
            if (i == LANE_PITCH) continue;
            _out[i] = _step_on ? _lanes[i].follow(_deck_step, frac, sh)
                               : _lanes[i].tick();
        }
    }
    --_tick_ctr;
```

- [ ] **Step 7: Run the tests to verify they pass**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -tc="steplock:*"
```

Expected: `[doctest] Status: SUCCESS!` with 5 test cases passing.

- [ ] **Step 8: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 4/4. FLOW is untouched, so `test_super_modulator`, `test_mod_tide`,
`test_center` and the render gates must all stay green. If `ctrl_identity`
moves, stop and report it — the baseline was refreshed in `2278a53` and any
movement from here is caused by this change.

- [ ] **Step 9: Commit**

```bash
git add engine/mod/super_modulator.h engine/mod/super_modulator.cpp \
        tests/test_step_grid_lock.cpp CMakeLists.txt
git commit -m "feat(mod): follow the deck step count in STEP

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: SPOT on the grid, and the invariant that proves it

**Files:**
- Modify: `engine/mod/super_modulator.cpp` (`spot`)
- Modify: `tests/test_step_grid_lock.cpp`

**Interfaces:**
- Consumes: `ModLane::nudge_slots` (Task 2), `_step_on` (Task 3).
- Produces: no new API.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_step_grid_lock.cpp`:

```cpp
namespace {
// In follower mode every deck step gives every texture lane exactly one
// boundary. So "on the grid" needs no sample arithmetic and no tolerance at
// all: the fire counts simply have to match the deck's step count. That
// equality is what an equal-rate design could not hold -- it drifted about two
// samples per step and lost a whole one every 90 seconds.
//
// A boundary is reported at the raster edge that covers it, and fired()
// latches until the next follow(), so counting at raster edges counts each
// boundary exactly once as long as a step is never shorter than the raster.
// At the fastest panel rate a step is ~200 samples against a 96-sample raster.
struct LockResult {
    int   deck_steps = 0;
    int   fires[LANE_COUNT] = {0, 0, 0, 0, 0};
    float end_phase[LANE_COUNT] = {0.f, 0.f, 0.f, 0.f, 0.f};
};

LockResult run_locked(float shape, bool chaos, int samples, int spot_at = -1) {
    SuperModulator m;
    m.init(48000.f, 99u);
    m.set_rate(0.45f);
    m.set_step(true, 8);
    m.set_shape(shape);
    m.set_smooth(0.f);
    m.set_tide(0.25f);                       // off-centre ladder rung
    if (chaos) {
        m.set_variation(0.8f);               // EVOLVE walks the master rate
        m.set_rate_scale(1.f, 1.6f);         // DRIFT: mod_scale != pitch_scale
        m.set_shuffle(0.6f);
    }
    Rng spot_rng;
    spot_rng.seed(5u);

    // The loop runs one call PAST `samples`, and `samples` is required to be a
    // whole number of raster windows, so that final call is itself a tick. A
    // deck step landing in the last window is counted the sample it happens,
    // but its boundary is only reported at the NEXT follow() -- without the
    // flush the equality below would hold by luck of where the last transition
    // fell rather than by construction. Two deck steps inside one window would
    // still collapse into one latched fired(), but at any panel-reachable rate
    // a step is ~200 samples against a 96-sample raster.
    REQUIRE(samples % ModLane::kTickInterval == 0);
    LockResult r;
    int last = m.pitch_cur_step();
    for (int i = 0; i <= samples; ++i) {
        if (i == spot_at) m.spot(spot_rng);
        m.process();
        if (m.pitch_cur_step() != last) { last = m.pitch_cur_step(); ++r.deck_steps; }
        if (i % ModLane::kTickInterval == 0)
            for (int l = 0; l < LANE_COUNT; ++l)
                if (l != LANE_PITCH && m.lane_fired(l)) ++r.fires[l];
    }
    for (int l = 0; l < LANE_COUNT; ++l) r.end_phase[l] = m.lane_phase(l);
    return r;
}
} // namespace

TEST_CASE("steplock: every texture boundary is a deck step, under chaos") {
    const LockResult r = run_locked(0.5f, true, 480000, -1);
    REQUIRE(r.deck_steps >= 16);
    for (int l = 0; l < LANE_COUNT; ++l)
        if (l != LANE_PITCH) CHECK(r.fires[l] == r.deck_steps);
}

TEST_CASE("steplock: SPOT stumbles by whole slots and stays on the grid") {
    const LockResult plain = run_locked(0.5f, true, 480000);
    const LockResult spot  = run_locked(0.5f, true, 480000, 160000);
    REQUIRE(spot.deck_steps >= 16);
    CHECK(spot.deck_steps == plain.deck_steps);   // SPOT never touches PITCH

    // The offset has to have landed somewhere: at least one texture lane sits
    // at a different point in its cycle than the un-stumbled run.
    bool moved = false;
    for (int l = 0; l < LANE_COUNT; ++l)
        if (l != LANE_PITCH && spot.end_phase[l] != plain.end_phase[l]) moved = true;
    CHECK(moved);

    // And it stayed on the grid. The jump adds at most one extra boundary --
    // the stumble itself -- and it is only separately countable when it falls
    // in a raster window that had no deck step of its own, so the count is
    // deck_steps or deck_steps + 1, never more and never less.
    for (int l = 0; l < LANE_COUNT; ++l) {
        if (l == LANE_PITCH) continue;
        CHECK(spot.fires[l] >= spot.deck_steps);
        CHECK(spot.fires[l] <= spot.deck_steps + 1);
    }
}

TEST_CASE("steplock: the lock holds over ten minutes of audio") {
    // The test the reverted equal-rate design would have failed. Ten minutes
    // is well past the ~90 s at which that design lost a whole step.
    const LockResult r = run_locked(0.5f, true, 48000 * 600);
    REQUIRE(r.deck_steps >= 2000);
    for (int l = 0; l < LANE_COUNT; ++l)
        if (l != LANE_PITCH) CHECK(r.fires[l] == r.deck_steps);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -tc="steplock: SPOT stumbles*"
```

Expected: FAIL on `CHECK( moved )`. SPOT still calls `kick()`, which moves the
raw phase — but a follower's position is derived from the deck count, so the
next `follow()` overwrites that phase and the gesture leaves no trace at all.
The two runs come out identical.

- [ ] **Step 3: Route SPOT through the slot offset**

Replace `SuperModulator::spot` in `engine/mod/super_modulator.cpp` with:

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
            // A follower has no phase to jump, so the same gesture becomes an
            // offset on its slot index (spec 2026-07-25
            // mod-lane-step-grid-lock). Exact, permanent, and incapable of
            // leaving the grid.
            const int n = static_cast<int>(std::lround(
                dphase * static_cast<float>(_lanes[i].steps())));
            _lanes[i].nudge_slots(n, dshape);
        } else {
            _lanes[i].kick(dphase, dshape);
        }
    }
}
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -tc="steplock:*"
```

Expected: `[doctest] Status: SUCCESS!` with 8 test cases passing. The
ten-minute case walks 28.8 M samples and takes a few seconds.

- [ ] **Step 5: Red-prove the invariant**

A guard that has never failed is not a guard. Temporarily make the followers
integrate again by changing the dispatch in `process()` to
`_out[i] = _lanes[i].tick();` unconditionally, then run:

```bash
cmake --build build && ./build/spky_tests.exe -tc="steplock: the lock holds*"
```

Expected: FAIL — the fire counts no longer equal the deck step count. Restore
the `_step_on ? … : …` dispatch and re-run to confirm SUCCESS. Do not commit
the temporary edit.

- [ ] **Step 6: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 4/4. `test_center.cpp`'s SPOT cases run their decks in FLOW, where
the raw `kick()` path is unchanged.

- [ ] **Step 7: Commit**

```bash
git add engine/mod/super_modulator.cpp tests/test_step_grid_lock.cpp
git commit -m "feat(mod): make SPOT a slot offset in STEP

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: Shape independence, live STEPS turn, docs

**Files:**
- Modify: `tests/test_step_grid_lock.cpp`
- Modify: `docs/roadmap.md` (insert a row after the `+ FORM/SONG` row)

**Interfaces:**
- Consumes: `run_locked` / `LockResult` from Task 4's anonymous namespace in
  the same file.
- Produces: nothing.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_step_grid_lock.cpp`:

```cpp
TEST_CASE("steplock: the fire grid is identical at every SHAPE setting") {
    // This is the whole point of the change: SHAPE was the detector, never the
    // cause. At the S&H end an offset reads as different random values; left
    // of it, as a ramp stepping beside the beat.
    const LockResult ref = run_locked(1.f, true, 480000);
    for (float shape : {0.f, 0.25f, 0.5f, 0.75f}) {
        const LockResult r = run_locked(shape, true, 480000);
        CHECK(r.deck_steps == ref.deck_steps);
        for (int l = 0; l < LANE_COUNT; ++l)
            if (l != LANE_PITCH) CHECK(r.fires[l] == ref.fires[l]);
    }
}

TEST_CASE("steplock: a live STEPS turn keeps the deck aligned") {
    SuperModulator m;
    m.init(48000.f, 99u);
    m.set_rate(0.45f);
    m.set_step(true, 8);

    int deck_steps = 0, fires[LANE_COUNT] = {0, 0, 0, 0, 0};
    int last = m.pitch_cur_step();
    for (int i = 0; i < 480000; ++i) {
        if (i == 160000) m.set_step(true, 16);
        if (i == 320000) m.set_step(true, 8);
        m.process();
        if (m.pitch_cur_step() != last) { last = m.pitch_cur_step(); ++deck_steps; }
        if (i % ModLane::kTickInterval == 0)
            for (int l = 0; l < LANE_COUNT; ++l)
                if (l != LANE_PITCH && m.lane_fired(l)) ++fires[l];
    }
    REQUIRE(deck_steps >= 16);
    for (int l = 0; l < LANE_COUNT; ++l)
        if (l != LANE_PITCH) CHECK(fires[l] == deck_steps);
}

TEST_CASE("steplock: leaving STEP hands the lanes back their own clocks") {
    SuperModulator m;
    m.init(48000.f, 99u);
    m.set_rate(0.45f);
    m.set_step(true, 8);
    for (int i = 0; i < 240000; ++i) m.process();

    m.set_step(false, 8);
    for (int i = 0; i < ModLane::kTickInterval * 64; ++i) m.process();

    // Back on their own ratios: SOURCE runs at twice the master's rate.
    CHECK(m.lane_rate_hz_for_test(LANE_SOURCE)
          == doctest::Approx(m.lane_rate_hz_for_test(LANE_PITCH) * 2.f));
    for (int i = 0; i < LANE_COUNT; ++i) {
        CHECK(m.lane_slots_for_test(i) == 8);
        CHECK(m.lane_phase(i) >= 0.f);
        CHECK(m.lane_phase(i) <  1.f);
    }
}
```

- [ ] **Step 2: Run the tests**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -tc="steplock:*"
```

Expected: `[doctest] Status: SUCCESS!` with 11 test cases passing. If the
shape-independence case fails, a timing path is reading SHAPE — check that
nothing feeds `_shape`, `_ev_shape`, `_shape_offset` or `_kick_shape` into a
slot or boundary computation.

- [ ] **Step 3: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 4/4.

- [ ] **Step 4: Update the roadmap**

In `docs/roadmap.md`, insert a row directly after the `| **+ FORM/SONG** | ... |`
row and before the `| **M5j** |` row:

```markdown
| **Mod grid lock** | In STEP the four texture lanes stop owning a clock and follow the deck's integer step count; the lane ratios become cycle lengths (4/6/8/12/16 at STEPS = 8), TIDE stretches slot counts, and DRIFT, EVOLVE, SPOT and float drift can no longer push a lane off the grid | ✅ **done** (engine; spec `docs/superpowers/specs/2026-07-25-mod-lane-step-grid-lock-design.md`) |
```

- [ ] **Step 5: Commit**

```bash
git add tests/test_step_grid_lock.cpp docs/roadmap.md
git commit -m "test(mod): pin shape independence and the live STEPS turn

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Verification

After Task 5, from the fork root:

```bash
source env.sh
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: 4/4, including the pre-existing `test_lane_tick`, `test_step_clock`,
`test_super_modulator`, `test_center` and `test_instrument` suites and both
render gates.

Then build the VCV plugin and play a deck in STEP with SHAPE swept end to end,
DRIFT up, VARIATION toward GROW and SPOT triggered:

```bash
cd host/vcv && ./build-local.sh
```

Never hand-roll the VCV build — the system `g++` on this machine is the ARM
cross-compiler and fails with "MinGW not found".
