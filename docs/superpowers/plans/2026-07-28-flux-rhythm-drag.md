# FLUX drag Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One knob per deck (DRAG) that pulls FLUX's delay time toward the *other* deck's rhythm, alternating between its two most recent onset gaps, so the echo limps in the neighbour's groove.

**Architecture:** A pure function turns the neighbour's `RhythmView` into two intervals. `Flux` alternates between them on its own repeats, interpolates geometrically from the RATE-ladder time by the knob, and writes the result into `_dt_target` — the delay-time slew that already exists. No new DSP objects, no new memory, no new lines.

**Tech Stack:** C++17, doctest, CMake + Ninja + clang (desktop), VCV Rack SDK (`host/vcv`), Daisy toolchain (bench).

**Spec:** `docs/superpowers/specs/2026-07-28-flux-rhythm-drag-design.md`

## Global Constraints

- Namespace `spky` for engine code, `spkyvcv` for VCV host code.
- The mod layer must not include fx headers. `fx/drag.h` includes `mod/rhythm_view.h`; never the reverse (`engine/mod/rhythm_view.h:13-18`).
- **DRAG at 0 must be bit-identical to today.** Every existing test in `tests/` stays green without modification.
- No migration path for old patches (owner's decision, 2026-07-28). Old DRIVE values landing in DRAG is accepted.
- `res/gen_panel.py`'s ordering rule: renaming a `Ctl` in place keeps its param id; anything genuinely new is **appended last**. `PART_STRIDE` stays 23.
- Commit trailer on every commit: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- Desktop build environment: `source env.sh` first (clang + Ninja + vendored headers). VCV host builds **only** via `./build-local.sh` — never hand-rolled.

## File Structure

| File | Responsibility |
|---|---|
| `engine/fx/drag.h` (new) | Tuning constants + the `derive_intervals` declaration. Includes `mod/rhythm_view.h`. |
| `engine/fx/drag.cpp` (new) | `derive_intervals` — pure, no state, no sample rate, no bounds. |
| `tests/test_drag.cpp` (new) | The function on its own. |
| `engine/fx/flux.h` / `.cpp` | Two setters, one private `apply_drag()`, the step accumulator in `process()`. |
| `tests/test_flux.cpp` | Bit-identity at 0, the alternation, the interpolation, self-gating. |
| `engine/fx/part_fx.h` | One-line passthrough. |
| `engine/instrument.h` / `.cpp` | `set_drag(int p, float)` and the cross-deck rhythm push at control rate. |
| `tests/test_instrument.cpp` | The push actually crosses the deck boundary, both ways. |
| `host/vcv/res/gen_panel.py` | DRAG takes DRIVE's slot; DRIVE becomes menu-only. |
| `host/vcv/src/Spotymod.cpp` | Param config, per-block push, DRIVE submenu. |
| `host/vcv/src/init_patch.hpp` | Defaults: DRAG 0, DRIVE 0.20. |
| `CMakeLists.txt`, `host/vcv/Makefile`, `bench/Makefile` | `drag.cpp` in three build lists. |

---

### Task 1: `derive_intervals` — the neighbour's rhythm as two durations

**Files:**
- Create: `engine/fx/drag.h`, `engine/fx/drag.cpp`, `tests/test_drag.cpp`
- Modify: `CMakeLists.txt:86-89` (test target) and `CMakeLists.txt:162` (render target), `host/vcv/Makefile:45-50`, `bench/Makefile` (the `CPP_SOURCES` block listing `../engine/fx/*.cpp`)

**Interfaces:**
- Consumes: `spky::RhythmView` from `engine/mod/rhythm_view.h` (`int32_t gap[2]; bool valid;`)
- Produces: `void spky::derive_intervals(const RhythmView&, int32_t out[2])`, and `spky::drag_tuning::{kNone, kMinGap, kUniformTol, kUniformSpread}`

**Background the implementer needs:** this function is `derive_offsets` from the deleted tape-era `engine/fx/taps.cpp` (see it with `git show main:engine/fx/taps.cpp`), with two changes. It returns **intervals** (`g0`, `g1`) rather than cumulative positions (`g0`, `g0+g1`), because a repeat interval is a duration and a tape tap was a position. And it has **no bounds arguments**: the old `tape_len` existed because a tape read could run off the buffer, and the taps design that briefly replaced it needed bounds against two lines colliding at the clock ceiling. One line cannot collide with anything, and `bbd_clock_hz` already clamps.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_drag.cpp`:

```cpp
#include <doctest/doctest.h>
#include "fx/drag.h"

using namespace spky;

namespace {
RhythmView view(int32_t g0, int32_t g1) {
    RhythmView rv;
    rv.gap[0] = g0;
    rv.gap[1] = g1;
    rv.valid  = true;
    return rv;
}
}  // namespace

TEST_CASE("derive_intervals: the two gaps come through as durations") {
    int32_t out[2];
    derive_intervals(view(6000, 9000), out);
    CHECK(out[0] == 6000);
    CHECK(out[1] == 9000);      // NOT 15000 -- an interval, not a position
}

TEST_CASE("derive_intervals: an invalid view yields no intervals") {
    RhythmView rv = view(6000, 9000);
    rv.valid = false;
    int32_t out[2];
    derive_intervals(rv, out);
    CHECK(out[0] == drag_tuning::kNone);
    CHECK(out[1] == drag_tuning::kNone);
}

TEST_CASE("derive_intervals: uniform gaps are spread into a limp") {
    // Evenly spaced repeats ARE a plain delay, and RATE already delivers one
    // in sync. The limp is the thing that cannot be had another way.
    int32_t out[2];
    derive_intervals(view(6000, 6000), out);
    CHECK(out[0] == 6000);
    CHECK(out[1] == 4500);      // 0.75 * g0, the MOTION lane's polyrhythm
}

TEST_CASE("derive_intervals: gaps within the tolerance still count as uniform") {
    int32_t out[2];
    derive_intervals(view(6000, 6060), out);    // 1 % apart, inside kUniformTol
    CHECK(out[1] == 4500);
}

TEST_CASE("derive_intervals: gaps outside the tolerance are left alone") {
    int32_t out[2];
    derive_intervals(view(6000, 6600), out);    // 10 % apart
    CHECK(out[0] == 6000);
    CHECK(out[1] == 6600);
}

TEST_CASE("derive_intervals: a buzz-length gap yields nothing") {
    int32_t out[2];
    derive_intervals(view(16, 9000), out);      // below kMinGap
    CHECK(out[0] == drag_tuning::kNone);
    CHECK(out[1] == drag_tuning::kNone);
}

TEST_CASE("derive_intervals: a spread that would fall below kMinGap yields nothing") {
    int32_t out[2];
    derive_intervals(view(40, 40), out);        // uniform; 0.75*40 = 30 < 32
    CHECK(out[0] == drag_tuning::kNone);
    CHECK(out[1] == drag_tuning::kNone);
}
```

- [ ] **Step 2: Add the file to the build lists**

In `CMakeLists.txt`, inside `add_executable(spky_tests ...)`, beside the existing `engine/fx/flux.cpp` / `tests/test_flux.cpp` pair (around line 86):

```cmake
    engine/fx/drag.cpp
    tests/test_drag.cpp
```

In the same file, the `render` target (around line 162) lists `engine/fx/flux.cpp`; add `engine/fx/drag.cpp` beside it.

In `host/vcv/Makefile`, beside `$(REPO)/engine/fx/flux.cpp` (line 46):

```make
	$(REPO)/engine/fx/drag.cpp \
```

In `bench/Makefile`, beside `../engine/fx/flux.cpp` in `CPP_SOURCES`:

```make
	../engine/fx/drag.cpp \
```

- [ ] **Step 3: Run the tests to verify they fail**

```bash
source env.sh
cmake -S . -B build && cmake --build build
```

Expected: **compile failure**, `fatal error: 'fx/drag.h' file not found`.

- [ ] **Step 4: Write the header**

Create `engine/fx/drag.h`:

```cpp
#pragma once
#include <cstdint>
#include "mod/rhythm_view.h"

namespace spky {

namespace drag_tuning {

// "No usable interval." 0 is safe as the sentinel because a usable interval is
// always >= kMinGap.
constexpr int32_t kNone = 0;

// Below this a gap is a buzz, not a rhythm -- and 0.75 * g would round toward a
// second interval equal to the first, defeating the uniformity guard.
constexpr int32_t kMinGap = 32;

// Gaps count as uniform when both lie within this fraction of their mean. A
// fraction, not an absolute count: at 240 samples a 2-sample jitter must not
// read as non-uniform, at 30000 a 50-sample drift must not read as uniform.
constexpr float kUniformTol = 0.02f;

// The spread applied when the guard fires: the MOTION lane's x3/4 ratio, a
// polyrhythm the instrument already runs.
constexpr float kUniformSpread = 0.75f;

}  // namespace drag_tuning

// Turn the other deck's published rhythm into two repeat intervals, in samples.
// Pure: no state, no sample rate, no bounds.
//
// This is the tape-era `derive_offsets` (engine/fx/taps.cpp on `main`, deleted
// at e004a3d) with its two hard-won rules intact and its output re-read as
// DURATIONS rather than positions behind a write head.
//
// The uniformity guard earns its keep for a sharper reason here than it did
// there. On tape the argument was "evenly spaced taps ARE a delay". Here RATE
// is already tempo-synced to divisions, so an echo locked to an even neighbour
// rhythm is a sound the instrument already makes -- the limp is what cannot be
// had any other way, and this guard is what guarantees one.
//
// out[i] == drag_tuning::kNone means "no usable rhythm"; both entries are set
// together, never one of them.
void derive_intervals(const RhythmView& rv, int32_t out[2]);

}  // namespace spky
```

- [ ] **Step 5: Write the implementation**

Create `engine/fx/drag.cpp`:

```cpp
#include "fx/drag.h"
#include <cmath>

using namespace spky;

void spky::derive_intervals(const RhythmView& rv, int32_t out[2]) {
    out[0] = out[1] = drag_tuning::kNone;
    if (!rv.valid) return;

    int32_t g0 = rv.gap[0];
    int32_t g1 = rv.gap[1];
    if (g0 < drag_tuning::kMinGap || g1 < drag_tuning::kMinGap) return;

    const float mean = 0.5f * (static_cast<float>(g0) + static_cast<float>(g1));
    const float tol  = drag_tuning::kUniformTol * mean;
    if (std::fabs(static_cast<float>(g0) - mean) <= tol &&
        std::fabs(static_cast<float>(g1) - mean) <= tol) {
        g1 = static_cast<int32_t>(drag_tuning::kUniformSpread * static_cast<float>(g0));
        if (g1 < drag_tuning::kMinGap) return;   // too short to limp audibly
    }

    // No upper bound. A long interval is a slow, dark echo -- which RATE can
    // already ask for -- and bbd_clock_hz clamps the short end on its own. With
    // a single line there is nothing for a clamped value to collide with.
    out[0] = g0;
    out[1] = g1;
}
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests -ts="*" -tc="derive_intervals*"
```

Expected: 7 test cases, all passing.

- [ ] **Step 7: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: everything green. Nothing else calls this function yet, so nothing else can have moved.

- [ ] **Step 8: Commit**

```bash
git add engine/fx/drag.h engine/fx/drag.cpp tests/test_drag.cpp \
        CMakeLists.txt host/vcv/Makefile bench/Makefile
git commit -m "feat(drag): derive two repeat intervals from the other deck's rhythm

The tape-era derive_offsets, restored with its uniformity guard and its
kMinGap rule intact, and its output re-read as durations rather than
positions behind a write head. Both bounds are gone: a single line has
nothing to collide with, and bbd_clock_hz already clamps the short end.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: `Flux` alternates between the intervals and interpolates to them

**Files:**
- Modify: `engine/fx/flux.h`, `engine/fx/flux.cpp`
- Test: `tests/test_flux.cpp`

**Interfaces:**
- Consumes: `spky::derive_intervals`, `spky::drag_tuning::kNone` (Task 1)
- Produces:
  - `void Flux::set_drag(float norm)` — 0..1, control rate
  - `void Flux::set_rhythm(const RhythmView& rv)` — control rate
  - `float Flux::drag_time_s() const` — test observer, returns the delay time DRAG is currently aiming at (`_dt_target`, i.e. the interpolated value; equals the ladder time at DRAG 0)

**How it works.** `apply_drag()` is the single place `_dt_target` is written. It geometrically interpolates between the RATE ladder's `_delay_time` and the currently selected neighbour interval, by the knob. `process()` advances a sample counter and flips the selected interval each time the current target's own duration has elapsed — since the echo's repeat interval *is* that duration, that is one interval per repeat. The existing 30 ms slew (`_dt_coef`) then turns each step into a pitch bend; **no new smoothing is added anywhere.**

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_flux.cpp`. Note the two extra static buffers — the bit-identity test needs two independent `Flux` instances.

```cpp
static float s_buf_l2[Flux::kMaxSamples];
static float s_buf_r2[Flux::kMaxSamples];

static RhythmView drag_view(int32_t g0, int32_t g1) {
    RhythmView rv;
    rv.gap[0] = g0;
    rv.gap[1] = g1;
    rv.valid  = true;
    return rv;
}

TEST_CASE("flux: DRAG at 0 is bit-identical to a Flux that never heard a rhythm") {
    Flux plain, dragged;
    plain.init(48000.f, s_buf_l, s_buf_r);
    dragged.init(48000.f, s_buf_l2, s_buf_r2);
    for (Flux* f : { &plain, &dragged }) {
        f->set_on(true, true);
        f->set_bpm(120.f);
        f->set_rate(3);
        f->set_stages(0.8f);
        f->set_mix(1.f);
        f->set_feedback(0.5f);
    }
    dragged.set_rhythm(drag_view(12000, 6000));
    dragged.set_drag(0.f);

    for (int i = 0; i < 60000; ++i) {
        const float in = (i < 32) ? 1.f : 0.f;
        float al = in, ar = in, bl = in, br = in;
        plain.process(al, ar);
        dragged.process(bl, br);
        REQUIRE(al == bl);      // bit-identical, not Approx
        REQUIRE(ar == br);
    }
}

TEST_CASE("flux: DRAG at 1 alternates between the neighbour's two intervals") {
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);                       // ladder = 0.5 s, well away from both
    f.set_stages(0.8f);
    f.set_mix(1.f);
    f.set_feedback(0.f);
    f.set_rhythm(drag_view(12000, 6000));   // 0.25 s and 0.125 s
    f.set_drag(1.f);

    CHECK(f.drag_time_s() == doctest::Approx(0.25f).epsilon(0.001));

    auto run = [&f](int n) { for (int i = 0; i < n; ++i) { float l = 0.f, r = 0.f; f.process(l, r); } };

    run(11990);
    CHECK(f.drag_time_s() == doctest::Approx(0.25f).epsilon(0.001));   // not yet
    run(20);
    CHECK(f.drag_time_s() == doctest::Approx(0.125f).epsilon(0.001));  // flipped
    run(5990);
    CHECK(f.drag_time_s() == doctest::Approx(0.125f).epsilon(0.001));
    run(20);
    CHECK(f.drag_time_s() == doctest::Approx(0.25f).epsilon(0.001));   // and back
}

TEST_CASE("flux: DRAG interpolates geometrically") {
    // Pitch tracks the clock RATIO, so the interpolation is geometric --
    // the same reasoning behind the modulation lane's x1/4..x4 mapping.
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);                       // ladder = 0.5 s
    f.set_rhythm(drag_view(12000, 18000));   // 0.25 s and 0.375 s
    f.set_drag(0.5f);
    // sqrt(0.5 * 0.25) == 0.353553
    CHECK(f.drag_time_s() == doctest::Approx(0.353553f).epsilon(0.001));
}

TEST_CASE("flux: DRAG reaches the clock") {
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);
    f.set_stages(0.8f);                  // 8192
    f.set_mix(1.f);
    f.set_feedback(0.f);
    f.set_rhythm(drag_view(24000, 18000));   // 0.5 s and 0.375 s, long plateaus
    f.set_drag(1.f);
    // The 30 ms slew has to run before clock_hz() reflects the target.
    for (int i = 0; i < 5000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    // f = stages / (2 * t) = 8192 / (2 * 0.5) = 8192 Hz
    CHECK(f.clock_hz() == doctest::Approx(8192.f).epsilon(0.02));
}

TEST_CASE("flux: a step bends pitch by the ratio of the two intervals") {
    // The bend IS the clock ratio -- there is no crossfade in a BBD and there
    // must be none in the model. Asserting the clock ratio across a step is
    // asserting the pitch ratio (spec section 1.4).
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);
    f.set_stages(0.8f);
    f.set_mix(1.f);
    f.set_feedback(0.f);
    f.set_rhythm(drag_view(24000, 12000));   // 0.5 s and 0.25 s -- a 2:1 step
    f.set_drag(1.f);

    auto run = [&f](int n) { for (int i = 0; i < n; ++i) { float l = 0.f, r = 0.f; f.process(l, r); } };

    run(5000);                               // past the 30 ms slew, still step 0
    const float before = f.clock_hz();
    run(24000);                              // step 0 elapses, flip to 0.25 s
    run(5000);                               // let the slew settle on the new one
    const float after = f.clock_hz();
    CHECK(after / before == doctest::Approx(2.0f).epsilon(0.02));
}

TEST_CASE("flux: an invalid neighbour rhythm leaves DRAG inert at any setting") {
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);                       // ladder = 0.5 s
    RhythmView rv = drag_view(12000, 6000);
    rv.valid = false;
    f.set_rhythm(rv);
    f.set_drag(1.f);
    CHECK(f.drag_time_s() == doctest::Approx(0.5f).epsilon(0.001));
}

TEST_CASE("flux: RATE still reaches the ladder at intermediate DRAG") {
    // Guards against an interpolation that accidentally saturates to the
    // neighbour's interval as soon as DRAG leaves zero.
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rhythm(drag_view(12000, 18000));
    f.set_drag(0.5f);
    f.set_rate(3);                       // 0.5 s -> sqrt(0.5*0.25)  = 0.353553
    const float at_quarter = f.drag_time_s();
    f.set_rate(0);                       // 1.0 s -> sqrt(1.0*0.25)  = 0.5
    const float at_half = f.drag_time_s();
    CHECK(at_quarter == doctest::Approx(0.353553f).epsilon(0.001));
    CHECK(at_half    == doctest::Approx(0.5f).epsilon(0.001));
}
```

- [ ] **Step 2: Run to verify they fail**

```bash
cmake --build build
```

Expected: **compile failure**, `no member named 'set_drag' in 'spky::Flux'`.

- [ ] **Step 3: Declare the interface in `engine/fx/flux.h`**

Add `#include "fx/drag.h"` beside the existing `#include "fx/bbd.h"`.

In the public section, after `set_time_mod`:

```cpp
    // DRAG: how much of the delay time the OTHER deck's rhythm owns. 0 is the
    // bit-exact today-path; 1 hands the time over outright and RATE goes
    // inert. Interpolation is geometric because pitch tracks the clock ratio.
    void set_drag(float norm);
    // The other deck's published rhythm, pushed at control rate by Instrument.
    void set_rhythm(const RhythmView& rv);
```

Beside the other test observers:

```cpp
    // The delay time DRAG is currently aiming at, before the 30 ms slew. Equal
    // to the ladder time whenever DRAG is 0 or the neighbour has no rhythm.
    float drag_time_s() const { return _dt_target; }
```

In the private section, beside `recompute_time` / `apply_feedback`:

```cpp
    // The ONLY place _dt_target is written. Called from recompute_time,
    // set_drag, set_rhythm and the step flip in process().
    void apply_drag();
```

And the state, after `_time_mult`:

```cpp
    // DRAG state. _drag_iv holds the neighbour's two intervals in samples;
    // _drag_i selects which one is in force; _drag_phase counts samples into
    // the current step and _drag_step_len is that step's length in samples,
    // cached so process() does not multiply per sample.
    float   _drag = 0.f;
    int32_t _drag_iv[2] = { 0, 0 };
    int     _drag_i = 0;
    float   _drag_phase = 0.f;
    float   _drag_step_len = 0.f;
    bool    _drag_active = false;
```

- [ ] **Step 4: Implement in `engine/fx/flux.cpp`**

Add `#include <cmath>` if not already present (it comes in via `flux.h`).

Replace the tail of `recompute_time` so the drag interpolation is never bypassed:

```cpp
    _delay_time = clampf(t, 0.001f, 60.f);
    apply_drag();
    if (immediate) _dt_current = _dt_target;
```

(The old body assigned `_dt_target = _delay_time;` and then `_dt_current = _delay_time;`. Both are now `apply_drag()`'s job and `_dt_target` respectively.)

Add the three new functions, after `set_time_mod`:

```cpp
// The single writer of _dt_target.
//
// Geometric, not linear: pitch tracks the clock RATIO directly, so a linear
// blend would put the perceived midpoint in the wrong place. Same reasoning
// that gave the modulation lane its x1/4..x4 mapping.
//
// _drag_step_len is the step's length in SAMPLES of the interpolated time, not
// of the neighbour's raw interval -- the echo's repeat interval is what is
// actually in force, so stepping on it is what makes "one interval per repeat"
// true at every DRAG setting rather than only at 1.
void Flux::apply_drag() {
    if (_drag <= 0.f || !_drag_active) {
        _dt_target = _delay_time;
        _drag_step_len = 0.f;
        return;
    }
    const float target = static_cast<float>(_drag_iv[_drag_i]) / _sr;
    _dt_target = std::pow(_delay_time, 1.f - _drag) * std::pow(target, _drag);
    _drag_step_len = _dt_target * _sr;
}

void Flux::set_drag(float norm) {
    if (!_buf_ok) return;
    const float d = clampf(norm, 0.f, 1.f);
    if (d == _drag) return;      // apply_drag runs two powf; do not run per push
    _drag = d;
    apply_drag();
}

void Flux::set_rhythm(const RhythmView& rv) {
    if (!_buf_ok) return;
    int32_t iv[2];
    derive_intervals(rv, iv);
    const bool active = (iv[0] != drag_tuning::kNone && iv[1] != drag_tuning::kNone);
    if (active == _drag_active && iv[0] == _drag_iv[0] && iv[1] == _drag_iv[1]) return;
    _drag_iv[0] = iv[0];
    _drag_iv[1] = iv[1];
    _drag_active = active;
    if (!active) { _drag_i = 0; _drag_phase = 0.f; }
    apply_drag();
}
```

In `process()`, immediately after the `if (_sw.is_idle()) return;` line and **before** the two `fonepole` calls:

```cpp
    // DRAG's step. One add and one compare per sample when engaged, nothing
    // but the compare when it is not -- which is what keeps DRAG 0 on the
    // same path it has always been on.
    if (_drag > 0.f && _drag_active) {
        _drag_phase += 1.f;
        if (_drag_phase >= _drag_step_len) {
            _drag_phase = 0.f;
            _drag_i ^= 1;
            apply_drag();
        }
    }
```

In `init()`, after `_time_mult = 1.f;`, reset the new state so a re-init cannot inherit a stale rhythm:

```cpp
    _drag = 0.f;
    _drag_iv[0] = _drag_iv[1] = 0;
    _drag_i = 0;
    _drag_phase = 0.f;
    _drag_step_len = 0.f;
    _drag_active = false;
```

- [ ] **Step 5: Run the new tests**

```bash
cmake --build build && ./build/spky_tests -tc="flux: DRAG*,flux: RATE still*,flux: an invalid*"
```

Expected: all passing.

- [ ] **Step 6: Run the whole suite — the bit-identity gate**

```bash
ctest --test-dir build --output-on-failure
```

Expected: everything green, including every pre-existing `test_flux.cpp` and `test_part_fx.cpp` case. If any pre-existing case moved, DRAG 0 is not bit-identical and the cause is in `apply_drag`'s zero path — fix that rather than the test.

The two witnesses in `test_part_fx.cpp` (soft clip, DC block) must stay **load-bearing**, not merely green: their premise already shifted once this week when a tuning change moved the loop's reachable peak. Re-prove each RED by temporarily removing its stage, confirm the failure, then restore. Observing green is not evidence that a witness still witnesses anything.

- [ ] **Step 7: Correct one overstatement in the spec**

The spec's §4 says *"Per sample: nothing changes."* That is now one compare per sample at DRAG 0, and an add plus a compare when engaged. Edit `docs/superpowers/specs/2026-07-28-flux-rhythm-drag-design.md` §4's first paragraph to say so — the claim it should make is that no *new arithmetic of consequence* runs per sample and the interpolation runs only on a step change.

- [ ] **Step 8: Commit**

```bash
git add engine/fx/flux.h engine/fx/flux.cpp tests/test_flux.cpp \
        docs/superpowers/specs/2026-07-28-flux-rhythm-drag-design.md
git commit -m "feat(drag): Flux alternates between the neighbour's intervals

apply_drag is the single writer of _dt_target: it interpolates
geometrically between the RATE ladder's time and whichever neighbour
interval is in force, and process() flips that selection each time the
current target's own duration has elapsed -- one interval per repeat.

The existing 30 ms slew turns each step into a pitch bend. No new
smoothing is added anywhere, which is the whole reason this design is
cheap.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The cross-deck push

**Files:**
- Modify: `engine/fx/part_fx.h:48` (beside `set_drive`), `engine/instrument.h:101` (beside `set_drive`) and around `:133` (test observers), `engine/instrument.cpp:90-91` (the control-rate block)
- Test: `tests/test_instrument.cpp`

**Interfaces:**
- Consumes: `Flux::set_drag`, `Flux::set_rhythm`, `Flux::drag_time_s` (Task 2)
- Produces:
  - `void PartFx::set_drag(float n)`
  - `void Instrument::set_drag(int p, float n)`
  - `float Instrument::drag_time_for_test(int p) const`

**Where the push goes and why.** `Instrument::process` already has a control-rate block (`instrument.cpp:79-93`, every `Center::kCtrlInterval` = 96 samples) that hands each part the *sibling's* dry tap. The rhythm push joins it, one line per direction, using the accessor `Instrument::rhythm(int p)` that already exists (`instrument.h:226`). Deck A reads B's rhythm and vice versa — the symmetry is in the indices, not in a second code path.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_instrument.cpp`:

```cpp
TEST_CASE("instrument: each deck's DRAG is fed by the OTHER deck's rhythm") {
    static Instrument inst;
    inst.init(48000.f, test_fx_mem());   // the file's own fixture, line 67
    inst.set_tempo_bpm(120.f);
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_fx_on(p, FxBlock::Flux, true, true);
        inst.set_flux_rate(p, 3);            // ladder = 0.5 s
        inst.set_drag(p, 1.f);               // the neighbour owns the time
    }

    // Long enough for both PITCH lanes to record three onsets, which is what
    // makes a RhythmView valid (engine/mod/rhythm_view.h).
    float out_l[64], out_r[64];
    for (int block = 0; block < 4000; ++block)
        inst.process(nullptr, nullptr, out_l, out_r, 64);

    // If this fails the rhythm ring never filled -- look there, not at DRAG.
    REQUIRE(inst.rhythm(PART_A).valid);
    REQUIRE(inst.rhythm(PART_B).valid);

    // Each deck has left its ladder time, which can only have come from the
    // sibling's rhythm reaching its Flux.
    CHECK(inst.drag_time_for_test(PART_A) != doctest::Approx(0.5f).epsilon(0.001));
    CHECK(inst.drag_time_for_test(PART_B) != doctest::Approx(0.5f).epsilon(0.001));
}
```

`test_fx_mem()` is defined at `tests/test_instrument.cpp:67` and is what every FLUX-carrying case in that file already uses. An `inst.init(48000.f)` without it gives a FLUX with no buffers (`_buf_ok == false`), where `set_drag` and `set_rhythm` both early-return and this test can never pass.

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build
```

Expected: **compile failure**, `no member named 'set_drag' in 'spky::Instrument'`.

- [ ] **Step 3: Add the passthroughs**

`engine/fx/part_fx.h`, beside `set_drive` (line 48):

```cpp
    void set_drag(float n)   { _flux.set_drag(n); }
    void set_rhythm(const RhythmView& rv) { _flux.set_rhythm(rv); }
```

`engine/instrument.h`, beside `set_drive` (line 101):

```cpp
    void set_drag(int p, float n)   { _parts[p].fx().set_drag(n); }
```

and beside the other test observers (around line 133):

```cpp
    float drag_time_for_test(int p) const { return _parts[p].fx().flux().drag_time_s(); }
```

- [ ] **Step 4: Add the push**

`engine/instrument.cpp`, inside the `if (_ctrl_ctr == 0)` block, directly after the two `set_other_deck_tap` lines (line 91):

```cpp
            // DRAG (spec 2026-07-28 flux-rhythm-drag): each deck's FLUX takes
            // its delay-time targets from the SIBLING's PITCH-lane rhythm.
            // Same cadence and same cross-over as the excitation tap above;
            // derive_intervals runs here, never per sample.
            _parts[PART_A].fx().set_rhythm(_parts[PART_B].mod().rhythm());
            _parts[PART_B].fx().set_rhythm(_parts[PART_A].mod().rhythm());
```

- [ ] **Step 5: Run the test**

```bash
cmake --build build && ./build/spky_tests -tc="instrument: each deck's DRAG*"
```

Expected: PASS. If `REQUIRE(inst.rhythm(...).valid)` fails, raise the block count rather than weakening the assertion — the rhythm needs three gated boundaries on the PITCH lane.

- [ ] **Step 6: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: everything green. DRAG defaults to 0 everywhere, so no existing instrument-level test can see this.

- [ ] **Step 7: Commit**

```bash
git add engine/fx/part_fx.h engine/instrument.h engine/instrument.cpp tests/test_instrument.cpp
git commit -m "feat(drag): push each deck's rhythm to the sibling's FLUX

Joins the control-rate block that already hands each part the sibling's
dry tap, using the Instrument::rhythm accessor that was left in place
when the tap bank was deleted. The symmetry is in the indices; there is
no second code path for the other direction.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: DRAG on the panel, DRIVE in the menu

**Files:**
- Modify: `host/vcv/res/gen_panel.py:405-406` and the tail of its `Ctl` list, `host/vcv/src/Spotymod.cpp` (param config ~line 236, per-block push ~line 402, menu ~line 1215-1229), `host/vcv/src/init_patch.hpp`, `host/vcv/README.md`
- Generated (do not hand-edit): `host/vcv/res/Spotymod.svg`, `host/vcv/src/generated_panel.hpp`

**Interfaces:**
- Consumes: `Instrument::set_drag(int, float)` (Task 3)
- Produces: `DRAG_A` / `DRAG_B` param ids (reusing DRIVE's old ids), new trailing `DRIVE_A` / `DRIVE_B` ids

**The id rule, and why this shape.** `gen_panel.py`'s `Ctl` list order defines param ids. Renaming a `Ctl` in place keeps its id — that is exactly how DUST became DRIVE on 2026-07-27. So DRAG **renames DRIVE in place** at `FX_BOT[0]` / `ROW_V2`, and the DRIVE that survives is **appended last** as a menu-only control in the shape `DETUNE_A/B` already uses (`gen_panel.py:429-430`: position `0.0, 0.0` and an empty label, which is what suppresses the panel widget). Old patches' DRIVE value lands in DRAG; the spec accepts that (no migration).

- [ ] **Step 1: Rename the panel control**

In `host/vcv/res/gen_panel.py`, replace lines 405-406:

```python
    Ctl("DRAG_A",   SMKNOB, FX_BOT[0],     ROW_V2, "DRAG"),
    Ctl("DRAG_B",   SMKNOB, W - FX_BOT[0], ROW_V2, "DRAG"),
```

and update the comment block above it (currently describing DRIVE/STAGES as the BBD's two voicing controls) to say that DRAG takes this slot per spec 2026-07-28 flux-rhythm-drag, that DRIVE moves to the menu, and that the id is reused deliberately.

- [ ] **Step 2: Append DRIVE as a menu-only control**

At the very end of the `Ctl` list in `gen_panel.py`, after the last existing entry:

```python
    # DRIVE loses its panel slot to DRAG (spec 2026-07-28 flux-rhythm-drag) and
    # becomes patch state, same menu-only shape as DETUNE_A/B above: position
    # 0,0 and an empty label mean no panel widget is emitted. Appended LAST so
    # every id before it stays put and PART_STRIDE remains 23.
    Ctl("DRIVE_A", SMKNOB, 0.0, 0.0, "", "Drive A"),
    Ctl("DRIVE_B", SMKNOB, 0.0, 0.0, "", "Drive B"),
```

- [ ] **Step 3: Regenerate and inspect**

```bash
cd host/vcv/res && python gen_panel.py && cd ../../..
git diff --stat host/vcv/res/Spotymod.svg host/vcv/src/generated_panel.hpp
```

Expected: the SVG's DRIV label becomes DRAG in both deck positions; `generated_panel.hpp` gains `DRIVE_A`/`DRIVE_B` at the end of the enum and renames the old two to `DRAG_A`/`DRAG_B`. Verify by eye that no id between the first entry and DRAG moved.

- [ ] **Step 4: Wire the host**

In `host/vcv/src/Spotymod.cpp`:

Param config (~line 236) — the existing branch keys on `DRIVE_A || DRIVE_B` and must keep doing so, since `DriveQuantity` follows DRIVE to the menu. Add a branch for DRAG beside it:

```cpp
                    else if (c.id == DRAG_A || c.id == DRAG_B)
                        configParam<DragQuantity>(c.id, 0.f, 1.f, init, lbl);
```

and define `DragQuantity` beside `DriveQuantity` (~line 71):

```cpp
// DRAG tooltip: how much of the delay time the OTHER deck's rhythm owns. At 0
// the RATE ladder is untouched; at 1 it is overridden outright.
struct DragQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        const float pct = 100.f * getValue();
        if (pct < 0.5f) return "off (RATE only)";
        return string::f("%.0f %% toward the other deck", pct);
    }
};
```

Per-block push (~line 402, beside the existing `set_drive`):

```cpp
            inst.set_drag(p, params[p ? DRAG_B : DRAG_A].getValue());
```

Menu (~line 1229, after the two Detune submenus and before the excitation-bus block):

```cpp
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            const std::string name = p ? "Drive B" : "Drive A";
            const int id = p ? DRIVE_B : DRIVE_A;
            menu->addChild(createSubmenuItem(name, "", [m, id](Menu* sub) {
                sub->addChild(new ParamMenuSlider(m->getParamQuantity(id)));
            }));
        }
```

- [ ] **Step 5: Set the defaults**

In `host/vcv/src/init_patch.hpp`, `kInitParamDefaults` is a hand-maintained array in ParamId order. Two edits:

- the entry currently commented `// DRIVE_A` (value `0.150000000f`, line 81) and its `DRIVE_B` sibling become `0.000000000f, // DRAG_A` and `0.000000000f, // DRAG_B`. **DRAG must default to 0** — that is the bit-identical path and the instrument must not ship with the neighbour owning FLUX's time.
- append two entries at the end of the array, in the new trailing id order: `0.200000000f, // DRIVE_A` and `0.200000000f, // DRIVE_B`.

Update the file's header comment (lines 5-8), which currently explains that DRIVE_A/B and STAGES_A/B were set deliberately for the BBD redesign, to cover the new arrangement.

- [ ] **Step 6: Remove the stale upgrade warning**

In `host/vcv/README.md`, delete the upgrade warning about a saved patch's DUST value landing on DRIVE. It described the 2026-07-27 rename and is now wrong twice over. Remove it rather than rewording it — the spec's "no migration" decision means there is nothing to warn about.

- [ ] **Step 7: Build the plugin**

```bash
cd host/vcv && ./build-local.sh && cd ../..
```

Expected: a clean build. **Never** hand-roll this command — the system `g++` on this machine is the ARM cross-compiler and a hand-rolled invocation fails with "MinGW not found".

- [ ] **Step 8: Run the whole suite once more**

```bash
ctest --test-dir build --output-on-failure
```

Expected: green. The engine is untouched by this task, so a failure here means a generated header drifted.

- [ ] **Step 9: Commit**

```bash
git add host/vcv/res/gen_panel.py host/vcv/res/Spotymod.svg \
        host/vcv/src/generated_panel.hpp host/vcv/src/Spotymod.cpp \
        host/vcv/src/init_patch.hpp host/vcv/README.md
git commit -m "feat(drag): DRAG takes DRIVE's panel slot, DRIVE moves to the menu

Renamed in place so the param id is reused -- the same move DUST->DRIVE
made on 2026-07-27 -- and DRIVE reappears appended last in the menu-only
shape DETUNE_A/B already uses. Old patches' DRIVE value lands in DRAG,
which the spec's no-migration decision accepts.

DRAG defaults to 0: that is the bit-identical path, and the instrument
must not ship with the neighbour owning FLUX's delay time.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: The ear pass

**Files:** none — this task produces a listening report and, if needed, a follow-up task list.

**Why it is a task at all.** The spec's §6 says outright that this design is owned by the ear more than by any measurement. Three of its named risks can only be settled by playing it, and one of them has a pre-identified remedy that must not be reached for prematurely.

- [ ] **Step 1: Build and launch**

```bash
cd host/vcv && ./build-local.sh && cd ../..
```

Then open VCV Rack with a two-deck patch: both decks running, FLUX engaged on at least deck A, MIX around 0.5, FEEDBACK around 0.4.

- [ ] **Step 2: Confirm the null case first**

With DRAG at 0 on both decks, the instrument must sound exactly as it did before this branch. If anything at all has changed here, stop — Task 2's bit-identity test passed but something downstream did not, and that is a bug, not a voicing question.

- [ ] **Step 3: Work the knob, in this order**

1. Deck B playing a straight rhythm, deck A's DRAG swept 0 → 1. Expect the echo to leave the grid and limp. The uniformity guard is what makes a *straight* neighbour rhythm still produce a limp; if a straight neighbour gives a plain in-sync delay, the guard is not firing and that is a defect, not taste.
2. The same sweep with FEEDBACK high. The spec's risk 1: the whole tail bends on every step. Note whether that is the best thing here or seasickness.
3. DRAG at 1, RATE swept. RATE is inert by design at full DRAG (risk 3). Note whether it reads as broken.
4. Deck B playing something busy. The spec's risk 2: the targets may change often enough that the echo never settles.

- [ ] **Step 4: Report, and do not pre-emptively fix**

Write the findings to the owner. If risk 1 lands badly, the pre-authorised lever is the **30 ms slew constant** (`flux.cpp:19`, `_dt_coef`) — lengthening it turns steps into drifts — and *not* a change to the stepping mechanism. Reach for it only if the ear pass asks for it, and record the value and the reason the way the BBD spec's errata records its own ear decisions.

- [ ] **Step 5: Record the outcome in the spec**

Append an errata section to `docs/superpowers/specs/2026-07-28-flux-rhythm-drag-design.md` following the shape of the BBD spec's, stating which of §6's three risks were heard, which were not, and any tuning value the ear pass moved.

```bash
git add docs/superpowers/specs/2026-07-28-flux-rhythm-drag-design.md
git commit -m "docs(drag): record the ear pass

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Notes for whoever executes this

**What is deliberately not here.** No bench workload and no CPU gate. The spec's §4 explains why: no new `BbdLine`, nothing of consequence per sample, and the interpolation runs on a step change rather than a sample. The `system`-profile whole-instrument run that the superseded taps design was blocked on is still worth doing — `instrument_worst` at 97.5 % predates the BBD entirely — but it is not a precondition of this work and no task here waits on it.

**The one thing that could genuinely go wrong.** `apply_drag()` is the single writer of `_dt_target`, and `recompute_time()` previously wrote it directly. If any future edit reintroduces a direct write, DRAG will silently stop working at whichever knob position that path is reached from. That is why Task 2's `RATE still reaches the ladder at intermediate DRAG` case exists — it is the regression guard for exactly that mistake.
