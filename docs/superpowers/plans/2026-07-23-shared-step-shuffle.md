# Shared STEP Shuffle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add one shared `0..1` SHUFFLE control that warps the complete STEP time base of both parts from straight timing to a `2:1` long/short feel while preserving raw phase, phrase length, clock anchors, groove determinism, and sampler source slicing.

**Architecture:** Add a header-only raw-phase boundary helper and make both `ModLane::process()` and `ModLane::tick()` use it. `Instrument` fans one shared target out through both `SuperModulator` instances; every lane latches that target at pair boundaries, with an immediate latch on FLOW-to-STEP entry. The renderer and VCV host expose the same API, while the VCV center panel becomes a TIME `2 x 2` and ROOM `3 x 2` layout.

**Tech Stack:** C++17, doctest, CMake/CTest, Python 3 panel generator/tests, VCV Rack SDK host code, JSON renderer scenarios.

## Global Constraints

- Range is normalized `0..1`; default is exactly `0`.
- `0` is straight and `1` is exactly `4/3` then `2/3` nominal steps (`2:1`).
- The raw phase, cycle wrap, step-clock factor, transport, COUPLE/DRIFT servos, external-clock anchors, and RST remain straight.
- FLOW must be bit-identical for every SHUFFLE value.
- One shared target drives both parts and all five lanes; there are no per-part or per-lane values.
- Odd phrase lengths leave the final unpaired step exactly straight.
- Live changes finish the active pair and latch at the next even step.
- FLOW-to-STEP retains the existing transport/partner snap and latches immediately at the landed step.
- Sampler fires swing, but sampler source-grid positions and nominal `_step_samples` remain straight.
- Shuffle adds no RNG draws and must not disturb phrase/groove determinism at `0`.
- No heap allocation, virtual dispatch, new dependency, hardware mapping, negative shuffle, or evolving/random shuffle.

---

## File Structure

- Create `engine/mod/shuffle_grid.h`: allocation-free boundary math shared by both lane execution paths.
- Create `tests/test_shuffle_grid.cpp`: exact helper invariants and endpoint tests.
- Modify `engine/mod/lane.h` and `engine/mod/lane.cpp`: target/latched state, shuffled boundary traversal, live `STEPS` remap.
- Modify `tests/test_step.cpp`, `tests/test_step_clock.cpp`, and `tests/test_lane_tick.cpp`: sample-level behavior and process/tick equivalence.
- Modify `engine/mod/super_modulator.h/.cpp`: fan-out API and shuffled step lookup for sampler cursor alignment.
- Modify `engine/instrument.h`: one shared public setter.
- Modify `engine/center/center.cpp`: use the lane's shuffled step lookup after FLOW-to-STEP snap.
- Modify `tests/test_super_modulator.cpp`, `tests/test_instrument.cpp`, and `tests/test_center.cpp`: shared fan-out, raw-phase invariants, entry snap.
- Modify `host/render/scenario.cpp` and `tests/test_scenario.cpp`: shared `set_shuffle` action.
- Create `host/render/scenarios/demo_shuffle.json`: listening/trace scenario.
- Modify `host/vcv/res/gen_panel.py` and `host/vcv/res/test_panel.py`: SHUFFLE parameter and center reflow.
- Regenerate `host/vcv/res/Spotymod.svg` and `host/vcv/src/generated_panel.hpp`.
- Modify `host/vcv/src/Spotymod.cpp`: configure and forward the shared parameter.
- Modify `host/vcv/README.md`: document the shared control and panel placement.
- Modify `CMakeLists.txt`: compile the new helper test.

---

### Task 1: Pure shuffle-grid boundary model

**Files:**
- Create: `engine/mod/shuffle_grid.h`
- Create: `tests/test_shuffle_grid.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: normalized amount `float`, raw phase in `[0,1)`, and `int steps >= 1`.
- Produces:
  - `float shuffle_boundary_phase(int boundary, int steps, float amount)`
  - `int shuffle_step_index(float phase, int steps, float amount)`
  - `float shuffle_step_length(int step, int steps, float amount)`
  - `float shuffle_step_fraction(float phase, int step, int steps, float amount)`
  - `float shuffle_phase_for_position(float step_position, int steps, float amount)`

- [ ] **Step 1: Write the failing helper tests**

Add `tests/test_shuffle_grid.cpp` with concrete endpoint and invariant checks:

```cpp
#include <doctest/doctest.h>
#include "mod/shuffle_grid.h"
using namespace spky;

TEST_CASE("shuffle-grid: endpoints are straight and two-to-one") {
    CHECK(shuffle_boundary_phase(1, 8, 0.f) == doctest::Approx(1.f / 8.f));
    CHECK(shuffle_boundary_phase(1, 8, 1.f) == doctest::Approx((4.f / 3.f) / 8.f));
    CHECK(shuffle_step_length(0, 8, 1.f) == doctest::Approx(4.f / 3.f));
    CHECK(shuffle_step_length(1, 8, 1.f) == doctest::Approx(2.f / 3.f));
}

TEST_CASE("shuffle-grid: pairs and cycles keep their duration") {
    for (float s : {0.f, 0.25f, 0.5f, 1.f}) {
        for (int step = 0; step < 8; step += 2)
            CHECK(shuffle_step_length(step, 8, s)
                + shuffle_step_length(step + 1, 8, s) == doctest::Approx(2.f));
        CHECK(shuffle_boundary_phase(8, 8, s) == doctest::Approx(1.f));
    }
}

TEST_CASE("shuffle-grid: odd final step stays straight") {
    CHECK(shuffle_step_length(4, 5, 1.f) == doctest::Approx(1.f));
    CHECK(shuffle_boundary_phase(4, 5, 1.f) == doctest::Approx(4.f / 5.f));
    CHECK(shuffle_boundary_phase(5, 5, 1.f) == doctest::Approx(1.f));
}

TEST_CASE("shuffle-grid: position round-trips") {
    for (int steps : {1, 2, 5, 8, 16})
        for (float s : {0.f, 0.4f, 1.f})
            for (int i = 0; i < 100; ++i) {
                float ph = static_cast<float>(i) / 100.f;
                int step = shuffle_step_index(ph, steps, s);
                float pos = static_cast<float>(step)
                    + shuffle_step_fraction(ph, step, steps, s);
                CHECK(shuffle_phase_for_position(pos, steps, s)
                    == doctest::Approx(ph).epsilon(0.0001));
            }
}
```

Register `tests/test_shuffle_grid.cpp` beside the other modulation tests in `CMakeLists.txt`.

- [ ] **Step 2: Build and verify the tests fail**

Run:

```powershell
cmake --build build --target spky_tests
```

Expected: compilation fails because `mod/shuffle_grid.h` and its functions do not exist.

- [ ] **Step 3: Implement the minimal helper**

Create `engine/mod/shuffle_grid.h` as a header-only unit. Clamp `amount`, `steps`, boundaries, phase, and step indices. Use the exact model:

```cpp
#pragma once
#include <cmath>
#include "util/math.h"

namespace spky {

inline float shuffle_amount(float amount) {
    return clampf(amount, 0.f, 1.f);
}

inline float shuffle_step_length(int step, int steps, float amount) {
    if (steps < 1) steps = 1;
    if (step < 0) step = 0;
    if (step >= steps) step = steps - 1;
    const float d = shuffle_amount(amount) / 3.f;
    if ((step & 1) == 0)
        return step + 1 < steps ? 1.f + d : 1.f;
    return 1.f - d;
}

inline float shuffle_boundary_phase(int boundary, int steps, float amount) {
    if (steps < 1) steps = 1;
    if (boundary <= 0) return 0.f;
    if (boundary >= steps) return 1.f;
    float pos = static_cast<float>(boundary);
    if (boundary & 1) pos += shuffle_amount(amount) / 3.f;
    return pos / static_cast<float>(steps);
}

inline int shuffle_step_index(float phase, int steps, float amount) {
    if (steps < 1) steps = 1;
    phase = clampf(phase, 0.f, 0.99999994f);
    const float pos = phase * static_cast<float>(steps);
    int step = static_cast<int>(pos);
    if ((step & 1) && pos < static_cast<float>(step) + shuffle_amount(amount) / 3.f)
        --step;
    if (step < 0) step = 0;
    if (step >= steps) step = steps - 1;
    return step;
}

inline float shuffle_step_fraction(float phase, int step, int steps, float amount) {
    const float start = shuffle_boundary_phase(step, steps, amount);
    const float end = shuffle_boundary_phase(step + 1, steps, amount);
    return clampf((phase - start) / (end - start), 0.f, 1.f);
}

inline float shuffle_phase_for_position(float step_position, int steps, float amount) {
    if (steps < 1) steps = 1;
    step_position = std::fmod(step_position, static_cast<float>(steps));
    if (step_position < 0.f) step_position += static_cast<float>(steps);
    int step = static_cast<int>(step_position);
    float frac = step_position - static_cast<float>(step);
    float start = shuffle_boundary_phase(step, steps, amount);
    float end = shuffle_boundary_phase(step + 1, steps, amount);
    return start + frac * (end - start);
}

} // namespace spky
```

- [ ] **Step 4: Build and run the focused tests**

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="shuffle-grid:*"
```

Expected: all `shuffle-grid:*` cases pass.

- [ ] **Step 5: Commit**

```powershell
git add CMakeLists.txt engine/mod/shuffle_grid.h tests/test_shuffle_grid.cpp
git commit -m "feat(mod): add shuffle grid timing model"
```

---

### Task 2: ModLane process/tick integration and live latching

**Files:**
- Modify: `engine/mod/lane.h`
- Modify: `engine/mod/lane.cpp`
- Modify: `tests/test_step.cpp`
- Modify: `tests/test_step_clock.cpp`
- Modify: `tests/test_lane_tick.cpp`

**Interfaces:**
- Consumes: Task 1 helper functions.
- Produces:
  - `void ModLane::set_shuffle(float amount)`
  - `int ModLane::step_at_phase(float phase) const`
  - `_shuffle_target` and `_shuffle_latched`, both defaulting to `0`
  - identical boundary order from `process()` and `tick()`

- [ ] **Step 1: Add failing lane behavior tests**

Add focused cases that collect `fired()` sample indices at DENSITY `1`:

```cpp
TEST_CASE("lane STEP: full shuffle makes long-short timing") {
    ModLane l;
    l.init(48000.f, 7);
    l.set_density(1.f);
    l.set_step(true, 8);
    l.set_rate_hz(1.f);       // nominal step = 6000 samples
    l.set_shuffle(1.f);
    auto fires = fire_samples(l, 26000);
    REQUIRE(fires.size() >= 5);
    CHECK(fires[2] - fires[1] == doctest::Approx(4000).epsilon(0.02));
    CHECK(fires[3] - fires[2] == doctest::Approx(8000).epsilon(0.02));
}

TEST_CASE("lane STEP: live shuffle waits for next even step") {
    ModLane l = step_lane(8, 1.f);
    l.set_shuffle(0.f);
    auto first = fire_samples(l, 7000);
    REQUIRE(first.size() == 2);
    l.set_shuffle(1.f);       // currently inside odd step 1
    auto later = fire_samples(l, 18000);
    REQUIRE(later.size() >= 3);
    CHECK(later[1] - later[0] == doctest::Approx(8000).epsilon(0.02));
    CHECK(later[2] - later[1] == doctest::Approx(4000).epsilon(0.02));
}
```

Extend the existing live `STEPS` grow/shrink tests to set `shuffle = 1`, compute the old audible position through `step_at_phase()` plus the helper fraction, and assert the same position after the change with no immediate fire. Add an odd-five-step test that observes a straight final interval.

In `tests/test_lane_tick.cpp`, add a `TickPair` case with shuffle `0.6`, odd step count `5`, and a mid-pair target change. Compare every observable already used by the equivalence harness.

- [ ] **Step 2: Build and verify failure**

Run:

```powershell
cmake --build build --target spky_tests
```

Expected: compilation fails because `ModLane::set_shuffle()` and `step_at_phase()` do not exist.

- [ ] **Step 3: Add lane state and public API**

In `lane.h`, include `mod/shuffle_grid.h`, add:

```cpp
void set_shuffle(float amount) { _shuffle_target = shuffle_amount(amount); }
int step_at_phase(float phase) const {
    return shuffle_step_index(phase, _steps, _shuffle_latched);
}
```

Keep the old static `step_index()` only until all callers migrate in Task 3. Add:

```cpp
float _shuffle_target = 0.f;
float _shuffle_latched = 0.f;
```

Add a private `_enter_step(int step, bool latch_now = false)` that latches when
`latch_now || (step % 2 == 0)`, assigns `_cur_step`, and calls `_on_boundary()`.

- [ ] **Step 4: Replace straight boundary lookup in both execution paths**

In `process()`:

```cpp
const int step = shuffle_step_index(_phase, _steps, _shuffle_latched);
if (step != _cur_step) _enter_step(step);
```

In `tick()`, replace the initial mismatch lookup with the same helper and replace:

```cpp
static_cast<float>(_cur_step + 1) / static_cast<float>(_steps)
```

with:

```cpp
shuffle_boundary_phase(_cur_step + 1, _steps, _shuffle_latched)
```

At wrap, call `_enter_step(0)` after `_wrap_events()`. At non-wrap boundaries,
call `_enter_step(_cur_step + 1)`. This ensures a newly latched even-step value
is used to calculate that pair's delayed odd boundary.

Initialize both shuffle fields to `0` in `init()`. In `reset(phase)`, latch the
current target before setting `_cur_step = -1`. On FLOW-to-STEP entry, latch the
current target immediately before any center snap:

```cpp
if (on && !_step_mode) _shuffle_latched = _shuffle_target;
```

- [ ] **Step 5: Preserve audible position on a live STEPS change**

Replace the straight `phase * old_steps` rescale inside `set_step()` with:

```cpp
int old_step = shuffle_step_index(_phase, _steps, _shuffle_latched);
float old_frac = shuffle_step_fraction(
    _phase, old_step, _steps, _shuffle_latched);
float pos = std::fmod(
    static_cast<float>(old_step) + old_frac,
    static_cast<float>(new_steps));
_phase = shuffle_phase_for_position(pos, new_steps, _shuffle_latched);
_cur_step = shuffle_step_index(_phase, new_steps, _shuffle_latched);
```

Do not change `clock_scale()` or nominal `step_samples()`.

- [ ] **Step 6: Run focused and regression tests**

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="lane STEP:*"
.\build\spky_tests.exe --test-case="step-clock:*"
.\build\spky_tests.exe --test-case="tick:*"
```

Expected: all focused cases pass, including process/tick equivalence.

- [ ] **Step 7: Commit**

```powershell
git add engine/mod/lane.h engine/mod/lane.cpp tests/test_step.cpp tests/test_step_clock.cpp tests/test_lane_tick.cpp
git commit -m "feat(mod): swing STEP lane boundaries"
```

---

### Task 3: Shared engine API, center snap, and sampler contract

**Files:**
- Modify: `engine/mod/super_modulator.h`
- Modify: `engine/mod/super_modulator.cpp`
- Modify: `engine/instrument.h`
- Modify: `engine/center/center.cpp`
- Modify: `tests/test_super_modulator.cpp`
- Modify: `tests/test_instrument.cpp`
- Modify: `tests/test_center.cpp`
- Modify: `tests/test_sampler_part.cpp`

**Interfaces:**
- Consumes: `ModLane::set_shuffle(float)` and `ModLane::step_at_phase(float)`.
- Produces:
  - `void SuperModulator::set_shuffle(float amount)`
  - `int SuperModulator::pitch_step_at_phase(float phase) const`
  - `void Instrument::set_shuffle(float amount)`

- [ ] **Step 1: Add failing shared-engine tests**

Add an instrument test that configures both parts identically in STEP, calls
only `inst.set_shuffle(1.f)`, and records `lane_fired(p, lane)` timestamps.
Assert:

```cpp
CHECK(fires_a == fires_b);
CHECK(long_gap == doctest::Approx(2.f * short_gap).epsilon(0.03));
```

Add a mixed-mode case proving a FLOW lane's output sequence is exactly equal
between an instrument at shuffle `0` and one at shuffle `1`.

In `test_center.cpp`, extend the FLOW-to-STEP snap rig with nonzero shuffle.
Assert the landed raw `pitch_phase()` is still the transport/partner target,
the sampler cursor uses `pitch_step_at_phase()`, and RST still returns raw
phase to `0`.

In `test_sampler_part.cpp`, record sampler fires under shuffle `1` and verify
the event gaps alternate while `sampler.step_clock()` remains the nominal
straight value.

- [ ] **Step 2: Build and verify failure**

Run:

```powershell
cmake --build build --target spky_tests
```

Expected: compilation fails because the shared engine APIs do not exist.

- [ ] **Step 3: Implement fan-out and center lookup**

Add to `SuperModulator`:

```cpp
void set_shuffle(float amount);
int pitch_step_at_phase(float phase) const {
    return _lanes[LANE_PITCH].step_at_phase(phase);
}
```

Implement:

```cpp
void SuperModulator::set_shuffle(float amount) {
    for (auto& lane : _lanes) lane.set_shuffle(amount);
}
```

Add to `Instrument`:

```cpp
void set_shuffle(float amount) {
    for (auto& part : _parts) part.mod().set_shuffle(amount);
}
```

In `Center::_snap_phase()`, replace the straight static lookup:

```cpp
p.snap_sampler_cursor(m.pitch_step_at_phase(tgt));
```

Do not change `_grid_servo`, `_rebase_grid`, `clock_scale`, `Transport`, or
sampler `_step_samples`.

- [ ] **Step 4: Run focused integration tests**

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="super modulator:*"
.\build\spky_tests.exe --test-case="instrument:*shuffle*"
.\build\spky_tests.exe --test-case="center:*"
.\build\spky_tests.exe --test-case="*sampler*shuffle*"
```

Expected: shared fan-out, entry snap, raw-phase, and sampler source-grid tests pass.

- [ ] **Step 5: Commit**

```powershell
git add engine/mod/super_modulator.h engine/mod/super_modulator.cpp engine/instrument.h engine/center/center.cpp tests/test_super_modulator.cpp tests/test_instrument.cpp tests/test_center.cpp tests/test_sampler_part.cpp
git commit -m "feat(engine): expose shared step shuffle"
```

---

### Task 4: Offline renderer action and listening scenario

**Files:**
- Modify: `host/render/scenario.cpp`
- Modify: `tests/test_scenario.cpp`
- Create: `host/render/scenarios/demo_shuffle.json`

**Interfaces:**
- Consumes: `Instrument::set_shuffle(float)`.
- Produces: global renderer action `{ "action": "set_shuffle", "value": n }`.

- [ ] **Step 1: Add a failing dispatch test**

Create two identically initialized instruments. Drive one through:

```cpp
Event e;
e.action = "set_shuffle";
e.value = 1.f;
apply_event(via_event, e);
direct.set_shuffle(1.f);
```

Process both sample by sample and compare `lane_fired()` for both parts over
multiple pairs. This proves dispatch rather than merely proving no crash.

- [ ] **Step 2: Build and verify failure**

Run:

```powershell
cmake --build build --target spky_tests
.\build\spky_tests.exe --test-case="scenario:*shuffle*"
```

Expected: the event-driven instrument remains straight and the comparison fails.

- [ ] **Step 3: Implement the action**

Add beside the other shared center actions in `apply_event()`:

```cpp
else if (a == "set_shuffle") inst.set_shuffle(e.value);
```

The action ignores `part`; the scenario file must omit it.

- [ ] **Step 4: Add the listening scenario**

Create `demo_shuffle.json` with a deterministic 8-step LOOP phrase at DENSITY
`1`, then schedule:

```json
{"t": 4,  "action": "set_shuffle", "value": 0.5},
{"t": 12, "action": "set_shuffle", "value": 1.0},
{"t": 20, "action": "set_shuffle", "value": 0.0}
```

Use comments to state that the t=12 change intentionally may land mid-pair and
must wait for the next even boundary.

- [ ] **Step 5: Build, test, and render**

Run:

```powershell
cmake --build build --target spky_tests render
.\build\spky_tests.exe --test-case="scenario:*shuffle*"
.\build\render.exe host/render/scenarios/demo_shuffle.json build/demo_shuffle.wav build/demo_shuffle.csv
```

Expected: test passes; WAV and CSV are created; CSV fire gaps show straight,
intermediate, `2:1`, then straight timing.

- [ ] **Step 6: Commit**

```powershell
git add host/render/scenario.cpp host/render/scenarios/demo_shuffle.json tests/test_scenario.cpp
git commit -m "feat(render): add shared shuffle scenario"
```

---

### Task 5: VCV parameter and center-panel reflow

**Files:**
- Modify: `host/vcv/res/gen_panel.py`
- Modify: `host/vcv/res/test_panel.py`
- Modify: `host/vcv/src/Spotymod.cpp`
- Modify: `host/vcv/README.md`
- Regenerate: `host/vcv/res/Spotymod.svg`
- Regenerate: `host/vcv/src/generated_panel.hpp`

**Interfaces:**
- Consumes: `Instrument::set_shuffle(float)`.
- Produces: appended VCV `SHUFFLE` `ParamId`, default `0`, center TIME `2 x 2`,
  and ROOM `3 x 2`.

- [ ] **Step 1: Update panel tests first**

Change `CENTER` expectations to:

```python
'SYNC': (-9.0, 42.0), 'TEMPO': (9.0, 42.0),
'COUPLE': (-9.0, 54.0), 'SHUFFLE': (9.0, 54.0),
'SCALE': (-11.5, 68.0), 'CHOKE': (0.0, 68.0), 'DRIFT': (11.5, 68.0),
'SPOT': (-11.5, 78.0), 'MASTER_DRIVE': (0.0, 78.0), 'SETTLE': (11.5, 78.0),
'REV_SIZE': (-12.0, 94.0), 'REV_TONE': (0.0, 94.0), 'REV_SMEAR': (12.0, 94.0),
'REV_DECAY': (-12.0, 104.5), 'REV_DIFF': (0.0, 104.5), 'REV_MOD': (12.0, 104.5),
```

Change center group expectations to BLEND `(13.0,19.5)`, TIME `(35.0,25.0)`,
DUO `(62.5,22.5)`, and ROOM `(87.5,23.7)`. Add an enum-order guard:

```python
check(g.PARAMS[-1].enum == 'SHUFFLE',
      "SHUFFLE must append after every existing ParamId")
```

- [ ] **Step 2: Run panel tests and verify failure**

Run:

```powershell
python host/vcv/res/test_panel.py
```

Expected: center-position, group-box, and missing-SHUFFLE assertions fail.

- [ ] **Step 3: Reflow the generator and append SHUFFLE**

In `gen_panel.py`, replace the center row constants with the exact coordinates
from Step 1. Preserve semantic ROOM columns:

```text
SIZE   TONE   SMEAR
DECAY  DIFF   WOBL
```

Append, after `REV_MIX_B`, never inside `SHARED`:

```python
Ctl("SHUFFLE", SMKNOB, CX + 9.0, ROW_TIME2, "SHUFL"),
```

Move `SYNC`, `TEMPO`, and `COUPLE` to the other three TIME coordinates. Update
the center group boxes without moving BLEND or the jack groups.

- [ ] **Step 4: Wire the Rack module**

In `Spotymod::defaultFor()`, add:

```cpp
case SHUFFLE: return 0.f;
```

In the shared parameter push beside TIDE/SYNC:

```cpp
inst.set_shuffle(params[SHUFFLE].getValue());
```

No custom quantity is needed: the standard `0..1` knob is correct.

- [ ] **Step 5: Regenerate and run guards**

Run:

```powershell
python host/vcv/res/gen_panel.py
python host/vcv/res/test_panel.py
```

Expected: generator reports the new parameter count; all panel guards pass.
Visually inspect `host/vcv/res/Spotymod.svg` for readable labels and a ROOM
bottom edge flush with PLAY.

- [ ] **Step 6: Document the control and build when the Rack SDK is available**

Add a concise TIME-section paragraph to `host/vcv/README.md`: shared control,
straight-to-`2:1` range, STEP-only behavior, pair latching, and external-clock
anchors.

Run the repository's configured Rack build:

```powershell
make -C host/vcv
```

Expected when `RACK_DIR`/toolchain is configured: plugin builds successfully.
If the SDK is unavailable, record that environment limitation and rely on the
generated-header compile through the next full repository verification.

- [ ] **Step 7: Commit**

```powershell
git add host/vcv/res/gen_panel.py host/vcv/res/test_panel.py host/vcv/res/Spotymod.svg host/vcv/src/generated_panel.hpp host/vcv/src/Spotymod.cpp host/vcv/README.md
git commit -m "feat(vcv): add shared shuffle control"
```

---

### Task 6: Full verification and contract audit

**Files:**
- Modify only if a verification failure exposes a defect in Task 1-5 files.

**Interfaces:**
- Consumes: all prior tasks.
- Produces: a green full suite, clean generated artifacts, and a clean worktree.

- [ ] **Step 1: Reconfigure and build all desktop targets**

Run:

```powershell
cmake -S . -B build
cmake --build build
```

Expected: `spky_tests` and `render` build successfully.

- [ ] **Step 2: Run the complete C++ suite**

Run:

```powershell
ctest --test-dir build --output-on-failure
```

Expected: `100% tests passed`.

- [ ] **Step 3: Re-run generated panel checks**

Run:

```powershell
python host/vcv/res/gen_panel.py
python host/vcv/res/test_panel.py
git diff --exit-code -- host/vcv/res/Spotymod.svg host/vcv/src/generated_panel.hpp
```

Expected: panel guards pass and regeneration leaves no diff.

- [ ] **Step 4: Audit the non-regression contracts**

Run:

```powershell
.\build\spky_tests.exe --test-case="*shuffle*"
.\build\spky_tests.exe --test-case="tick:*"
.\build\spky_tests.exe --test-case="center:*"
git diff --check
git status --short
```

Expected: focused tests pass, `git diff --check` is empty, and status is clean.
Confirm from tests or code inspection that shuffle `0` uses the straight helper
path without RNG draws, `_phase` is never warped, and sampler `_step_samples`
remains nominal.

- [ ] **Step 5: Request final code review**

Use `superpowers:requesting-code-review` against the implementation branch.
Resolve every correctness issue, rerun the affected focused suite, then rerun
Steps 1-4.

- [ ] **Step 6: Commit review fixes only if needed**

```powershell
git add CMakeLists.txt engine/mod/shuffle_grid.h engine/mod/lane.h engine/mod/lane.cpp engine/mod/super_modulator.h engine/mod/super_modulator.cpp engine/instrument.h engine/center/center.cpp tests/test_shuffle_grid.cpp tests/test_step.cpp tests/test_step_clock.cpp tests/test_lane_tick.cpp tests/test_super_modulator.cpp tests/test_instrument.cpp tests/test_center.cpp tests/test_sampler_part.cpp host/render/scenario.cpp host/render/scenarios/demo_shuffle.json tests/test_scenario.cpp host/vcv/res/gen_panel.py host/vcv/res/test_panel.py host/vcv/res/Spotymod.svg host/vcv/src/generated_panel.hpp host/vcv/src/Spotymod.cpp host/vcv/README.md
git commit -m "fix: address shared shuffle review"
```

If review finds no defects, do not create an empty commit.
