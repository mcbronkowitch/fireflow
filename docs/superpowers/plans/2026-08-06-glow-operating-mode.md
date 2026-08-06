# Glow Operating Mode (P_MODE) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the Glow flow layer a drawn operating mode — FLOW/free or STEP/synced — so terrains stop being step sequencers with no grid, and re-measure every gate that mode change moves.

**Architecture:** One new global parameter `P_MODE` (2 steps) appended at the tail of `SPKY_FLOW_PARAMS`, drawn per terrain from archetype weights. Because `Instrument::set_sync` is global and `set_step` takes mode *and* count together, the three parameters `P_MODE`/`P_STEPS_A`/`P_STEPS_B` move out of the stateless `apply_param` into a helper owned by `Flow`, which holds all three values in `_pushed[]`. During a blend `P_MODE` switches at phase 0 and ducks both decks together, deliberately collapsing the per-deck stagger for that one press.

> **CORRECTION 2026-08-06, post-implementation — the last sentence above is WRONG; do not re-execute it.** Collapsing the stagger was built, escalated as **Critical** in review, and replaced. It leaves the carrier deck's `P_ENGINE` switch — which stays at `kCarrierStaggerFrac`, the stagger being a by-ear decision the project owner re-affirmed — unducked in the open at `press + 1.5 s`, where `duck()` computes `u = 6` and returns the send unchanged. What shipped: the carrier gets a **second** duck at the press for the global `set_sync` flip, and **keeps** its stagger duck. Authority is `engine/flow/flow.cpp` `begin_blend()` and the corrected spec §5.2.

**Tech Stack:** C++17, clang + Ninja, doctest (vendored in `third_party/`), CTest.

**Spec:** `docs/superpowers/specs/2026-08-06-glow-taste-structure-design.md` §5, §5.1–5.3, §8.

## Global Constraints

- Build engine/tests with clang + Ninja, never MSVC. `source env.sh` first, then
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`. **`-DCMAKE_BUILD_TYPE=Release`
  is not optional** — a Debug configure makes `spky_tests` and `ctrl_identity`
  fail with "SYNTH reference moved".
- Run tests with `ctest --test-dir build --output-on-failure`, or the single
  binary `./build/spky_tests -ts="<test suite>"` for a faster loop.
- Commit trailer is `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **Every test must be proven RED once** before its implementation lands. A test
  that cannot fail gets fixed, even if this plan mandated it.
- No bit-exactness gates. Renders are sanity checks, not checksums.
- FireFlow is in dev alpha: saved patches may change what they sound like. Do
  not add migrations or compatibility shims.
- `P_MODE` **must be the last entry** of `SPKY_FLOW_PARAMS`. Base draws are keyed
  `kStreamParamBase + uint32_t(param)` (`terrain.cpp:160`); inserting it anywhere
  else re-seeds every later parameter's stream.

## File Structure

| File | Responsibility after this plan |
|---|---|
| `engine/flow/flow_params.h` | `P_MODE` at enum tail; `apply_param` no longer handles `P_MODE`/`P_STEPS_A`/`P_STEPS_B` |
| `engine/flow/taste.h` | `kModeW[ARCH_COUNT]`; placeholder `P_MODE` base rule |
| `engine/flow/terrain.cpp` | draws `P_MODE` from `kModeW` in a new stage |
| `engine/flow/flow.cpp` | `push_mode_and_steps()` helper; `switch_phase_for` and the duck schedule know about `P_MODE` |
| `engine/flow/flow.h` | declares the helper |
| `tests/test_flow_mode.cpp` | **new** — enum tail, routing invariant, draw distribution, blend behaviour |
| `tests/test_flow_taste.cpp` | completeness check still green with `P_MODE` |
| `engine/flow/terrain.cpp` (comment) | re-measured `distance()` commentary |
| `tests/test_flow_audio.cpp` | re-measured RMS band, blend gate, churn gate |

---

### Task 1: P_MODE exists, at the tail, with a placeholder row

**Files:**
- Modify: `engine/flow/flow_params.h:70` (append to `SPKY_FLOW_PARAMS`)
- Modify: `engine/flow/taste.h` (add `kModeW`, add placeholder base rule)
- Create: `tests/test_flow_mode.cpp`
- Modify: `CMakeLists.txt:165` (register the new test file)

**Interfaces:**
- Produces: `P_MODE` (ParamId, range `0..1`, 2 steps; 0 = FLOW/free, 1 = STEP/synced);
  `inline const float kModeW[ARCH_COUNT]` = probability of STEP/synced per archetype.

- [ ] **Step 1: Write the failing test**

Create `tests/test_flow_mode.cpp`:

```cpp
// tests/test_flow_mode.cpp
#include "doctest/doctest.h"
#include "flow/taste.h"
#include "flow/flow_params.h"
using namespace spky::flow;

TEST_CASE("flow mode: P_MODE is the last parameter") {
    // Base draws are keyed kStreamParamBase + param (terrain.cpp). If P_MODE
    // is not last, every parameter after it gets a different RNG stream and
    // every existing terrain code resolves to a different sound.
    CHECK(P_MODE == P_COUNT - 1);
    CHECK(kParams[P_MODE].lo == 0.f);
    CHECK(kParams[P_MODE].hi == 1.f);
    CHECK(kParams[P_MODE].steps == 2);
}

TEST_CASE("flow mode: archetype weights are probabilities, drone lowest") {
    for (int a = 0; a < ARCH_COUNT; ++a) {
        CAPTURE(a);
        CHECK(kModeW[a] >= 0.f);
        CHECK(kModeW[a] <= 1.f);
    }
    // A drone is the archetype that normally wants no step sequencer.
    for (int a = 0; a < ARCH_COUNT; ++a)
        if (a != ARCH_DRONE) CHECK(kModeW[ARCH_DRONE] < kModeW[a]);
}
```

- [ ] **Step 2: Run test to verify it fails**

Register the file first — add `tests/test_flow_mode.cpp` to the `spky_tests`
source list in `CMakeLists.txt` after line 165, then:

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Expected: **compile error**, `'P_MODE' was not declared in this scope`. That is
the RED for this task.

- [ ] **Step 3: Append P_MODE at the enum tail**

In `engine/flow/flow_params.h`, extend the last line of the `SPKY_FLOW_PARAMS`
macro (currently `X(P_TEMPO_BPM, 50.f, 140.f, 0)`):

```cpp
  X(P_TEMPO_BPM, 50.f, 140.f, 0) \
  /* The terrain's operating mode, spec 2026-08-06 §5. 0 = FLOW/free (lanes
     breathe in their own kLaneRatio relationships, no grid), 1 = STEP/synced
     (step sequencer on the divisions.h ladder). ONE global value, not one per
     deck: Instrument::set_sync is global (instrument.h:274), so a per-deck
     mode would need SYNC on and off at once.
     MUST STAY LAST. Base draws are keyed kStreamParamBase + param
     (terrain.cpp:160) -- inserting a param before this one re-seeds every
     later stream and re-resolves every terrain code. */ \
  X(P_MODE,       0.f, 1.f,  2)
```

- [ ] **Step 4: Add kModeW and the placeholder base rule**

In `engine/flow/taste.h`, after the `kTextureW` block:

```cpp
// P_MODE draw weights (spec 2026-08-06 §5): probability that a terrain of this
// archetype comes out STEP/synced rather than FLOW/free. A drone normally has
// no step sequencer at all; an arp is one almost by definition.
// Order: {ARCH_DRONE, ARCH_PULSE, ARCH_ARP, ARCH_FRAGMENT}.
inline const float kModeW[ARCH_COUNT] = { 0.15f, 0.90f, 0.95f, 0.75f };
```

And in `kBaseRules`, alongside the other discrete world picks:

```cpp
// P_MODE: drawn from kModeW, NOT from this span. The row exists only so the
// coverage test has no hole -- exactly like the P_ENGINE_A/B rows above. Do
// not tune it expecting audible effect.
{ P_MODE,     {{0.f,1.f},{0.f,1.f},{0.f,1.f},{0.f,1.f}} },
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests -ts="*" -tc="flow mode:*"
ctest --test-dir build --output-on-failure
```

Expected: the two new cases PASS, and `test_flow_taste.cpp`'s existing
"every param is owned" coverage check stays green because of the placeholder row.

- [ ] **Step 6: Commit**

```bash
git add engine/flow/flow_params.h engine/flow/taste.h tests/test_flow_mode.cpp CMakeLists.txt
git commit -m "feat(flow): the terrain gets an operating mode, and it sits at the enum tail"
```

---

### Task 2: Route mode and steps together, out of apply_param

**Files:**
- Modify: `engine/flow/flow_params.h:95-163` (`apply_param`: drop three cases)
- Modify: `engine/flow/flow.h:76` (declare the helper)
- Modify: `engine/flow/flow.cpp:434-441` (push tail calls the helper)
- Modify: `tests/test_flow_mode.cpp`

**Interfaces:**
- Consumes: `P_MODE` from Task 1.
- Produces: `void Flow::push_mode_and_steps(bool force);` — private; reads
  `_pushed[P_MODE]`, `_pushed[P_STEPS_A]`, `_pushed[P_STEPS_B]` and issues
  `set_sync` plus both `set_step` calls.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_mode.cpp`:

```cpp
#include "flow/flow.h"
#include "instrument.h"

TEST_CASE("flow mode: steps never run without a grid") {
    // The failure this guards: STEP mode on with SYNC off is a step sequencer
    // at a free-running rate, which is what Glow shipped with. No reachable
    // tick may show that combination.
    for (uint32_t master = 1; master <= 200; ++master) {
        spky::Instrument inst;
        inst.init(48000.f);
        Flow f;
        f.init(&inst, 100.f);
        TerrainState st; st.master = master;
        f.wake(st);
        for (int i = 0; i < 50; ++i) f.tick();
        CAPTURE(master);
        CHECK(inst.step_on(spky::PART_A) == inst.synced(spky::PART_A));
        CHECK(inst.step_on(spky::PART_B) == inst.synced(spky::PART_B));
    }
}
```

`Instrument` has no public part accessor — it exposes named observers instead
(`form(p)`, `song(p)` at `instrument.h:62-66`). Add two more in that style:

```cpp
    // Observers for the mode invariant (spec 2026-08-06 §5): steps and grid
    // are one decision, and a test must be able to see both halves of it.
    bool step_on(int p) const { return _parts[p].mod().step_on(); }
    bool synced(int p) const  { return _parts[p].mod().synced(); }
```

`SuperModulator::synced()` already exists (`super_modulator.h:160`); add its
twin beside it:

```cpp
bool step_on() const { return _step_on; }
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build && ./build/spky_tests -tc="flow mode: steps never run without a grid"
```

Expected: FAIL on the first master — `step_on()` is true (hardcoded at
`flow_params.h:107`) while `synced()` is false (nothing ever calls `set_sync`).
This is the RED that proves the bug the whole plan exists for. Record the
failure output in the commit message.

- [ ] **Step 3: Remove the three cases from apply_param**

In `engine/flow/flow_params.h`, delete the `P_STEPS_A` and `P_STEPS_B` cases and
add a comment where they were:

```cpp
    // P_MODE, P_STEPS_A and P_STEPS_B are deliberately NOT handled here.
    // set_step() takes mode AND count together and set_sync() is global, so
    // routing them needs all three values at once -- which this per-param,
    // stateless function cannot see. Flow::push_mode_and_steps() owns them.
    case P_MODE: case P_STEPS_A: case P_STEPS_B: break;
```

- [ ] **Step 4: Add the helper**

Declare in `engine/flow/flow.h` beside the other private helpers:

```cpp
    void push_mode_and_steps(bool force);
```

Implement in `engine/flow/flow.cpp`, above `recompute_and_push`:

```cpp
// P_MODE + P_STEPS_A/B, pushed as one unit (spec 2026-08-06 §5.3).
// apply_param cannot express this: set_step takes mode and count together, and
// set_sync is global across both parts (instrument.h:274). Issuing them here,
// from _pushed[], means no tick can ever observe steps without a grid.
void Flow::push_mode_and_steps(bool force) {
    if (!_inst) return;
    const bool step = _pushed[P_MODE] > 0.5f;
    const int  sa = int(clamp_to(kParams[P_STEPS_A], _pushed[P_STEPS_A]) + 0.5f);
    const int  sb = int(clamp_to(kParams[P_STEPS_B], _pushed[P_STEPS_B]) + 0.5f);
    if (!force && step == _mode_now && sa == _steps_now[0] && sb == _steps_now[1])
        return;
    _mode_now = step; _steps_now[0] = sa; _steps_now[1] = sb;
    _inst->set_sync(step);
    _inst->set_step(PART_A, step, sa);
    _inst->set_step(PART_B, step, sb);
}
```

Add the three state members to `engine/flow/flow.h` beside `_pushed`:

```cpp
    bool _mode_now = false;         // last pushed mode, for the change guard
    int  _steps_now[2] = { -1, -1 }; // last pushed step counts
```

- [ ] **Step 5: Call it from the push tail**

In `recompute_and_push`, after the per-parameter loop closes (`flow.cpp:441`):

```cpp
    }
    // All three of this unit's values are now in _pushed[]; route them as one.
    push_mode_and_steps(force);
}
```

- [ ] **Step 6: Run tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: the invariant case PASSES for all 200 masters. Other flow tests may
now fail on changed audio — that is Task 5's work; note which, do not fix here.

- [ ] **Step 7: Commit**

```bash
git add engine/flow/flow_params.h engine/flow/flow.h engine/flow/flow.cpp engine/mod/super_modulator.h tests/test_flow_mode.cpp
git commit -m "fix(flow): mode and step count are one push, so steps can never run without a grid"
```

---

### Task 3: Draw P_MODE from the archetype weights

**Files:**
- Modify: `engine/flow/terrain.cpp:164-168` (after the stage 1-2 overrides)
- Modify: `tests/test_flow_mode.cpp`

**Interfaces:**
- Consumes: `kModeW` (Task 1), `pick_weighted` (already in `terrain.cpp:20`).
- Produces: `Terrain::base[P_MODE]` is 0.f or 1.f, never an intermediate value.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_mode.cpp`:

```cpp
#include "flow/terrain.h"

TEST_CASE("flow mode: the draw follows kModeW per archetype") {
    int n[ARCH_COUNT] = {}, step[ARCH_COUNT] = {};
    for (uint32_t master = 1; master <= 4000; ++master) {
        TerrainState st; st.master = master;
        const Terrain t = generate(st);
        // The value is a clean discrete, never something in between.
        CHECK((t.base[P_MODE] == 0.f || t.base[P_MODE] == 1.f));
        n[t.arch]++;
        if (t.base[P_MODE] > 0.5f) step[t.arch]++;
    }
    for (int a = 0; a < ARCH_COUNT; ++a) {
        if (n[a] < 100) continue;              // too few to judge
        const float got = float(step[a]) / float(n[a]);
        CAPTURE(a); CAPTURE(got); CAPTURE(kModeW[a]);
        CHECK(got > kModeW[a] - 0.08f);
        CHECK(got < kModeW[a] + 0.08f);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build && ./build/spky_tests -tc="flow mode: the draw follows kModeW per archetype"
```

Expected: FAIL — `base[P_MODE]` comes from the placeholder base rule's uniform
`{0,1}` span, so it is a continuous value (the first `CHECK` fires immediately)
and the STEP share sits near 0.5 for every archetype.

- [ ] **Step 3: Draw it in terrain.cpp**

In `generate()`, directly after the stage 1-2 override block
(`t.base[P_ROOT] = float(root);`):

```cpp
    // Stage 3b: operating mode (spec 2026-08-06 §5). Its base-rule row is a
    // placeholder like the ENGINE rows -- the real draw is this weighted coin,
    // taken from the param's OWN stream so it rerolls exactly when a full
    // terrain does. Written as a clean 0/1 so nothing downstream has to guess
    // where the rounding boundary is.
    {
        Rng r = make_stream(st.master, kStreamParamBase + uint32_t(P_MODE), 0);
        const float w[2] = { 1.f - kModeW[t.arch], kModeW[t.arch] };
        t.base[P_MODE] = float(pick_weighted(r, w, 2));
    }
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests -tc="flow mode:*"
```

Expected: PASS. Every archetype's measured STEP share sits within 0.08 of its
weight.

- [ ] **Step 5: Prove the test can fail**

Temporarily change `kModeW[ARCH_DRONE]` to `0.90f`, rebuild, confirm the case
goes RED with the drone row out of tolerance, then revert.

> **CORRECTION 2026-08-06, post-implementation — this perturbation cannot work.**
> It is self-referential: `kModeW` feeds **both** the draw in `terrain.cpp` and
> the tolerance window the test asserts against (`got > kModeW[a] - 0.08f`).
> Moving the weight moves the target and the goalposts by the same amount, so
> the case stays green by construction. To redden it, perturb only one side —
> e.g. hard-code a literal probability in the generator's `P_MODE` draw, or
> assert against a literal in the test. A genuine RED was obtained instead on
> that case's other new assertion (`judged == ARCH_COUNT`) by raising the
> `n[a] < 100` skip threshold.

- [ ] **Step 6: Commit**

```bash
git add engine/flow/terrain.cpp tests/test_flow_mode.cpp
git commit -m "feat(flow): drones stop drawing a step sequencer they never wanted"
```

---

### Task 4: P_MODE switches at phase 0, under a double duck

**Files:**
- Modify: `engine/flow/flow.cpp:288-292` (`switch_phase_for`)
- Modify: `engine/flow/flow.cpp` (`begin_blend`, the `_duck_t` schedule)
- Modify: `tests/test_flow_mode.cpp`

**Interfaces:**
- Consumes: `P_MODE` draw (Task 3), `push_mode_and_steps` (Task 2).
- Produces: `switch_phase_for(P_MODE) == 0.f`; on a mode-changing press both
  `_duck_t[0]` and `_duck_t[1]` are set to the press instant.

- [ ] **Step 1: Write the failing test**

> **CORRECTION 2026-08-06, post-implementation — the sentinel in this snippet is
> vacuous.** `if (a.master && b.master) break;` is meant to stop once both a FLOW
> and a STEP terrain have been found, but `TerrainState::master` **defaults to 1,
> not 0** (`engine/flow/terrain.h`), so both sides are already truthy on entry and
> the loop breaks on its first iteration with `a` and `b` possibly unset. The
> `REQUIRE(a.master != 0)` guards below are vacuous for the same reason. Use
> explicit `have_flow` / `have_step` bools instead — which is what the shipped
> cases in `tests/test_flow_mode.cpp` do. Note also that this case's premise
> (both decks ducked *instead of* the stagger) is superseded; see the Task 4
> Step 4 correction below.

Append to `tests/test_flow_mode.cpp`:

```cpp
TEST_CASE("flow mode: a mode change ducks both decks at the press") {
    // P_MODE belongs to no deck, and set_sync is global -- so it cannot ride
    // the carrier stagger without flipping the texture deck's whole clocking
    // 1.5 s after that deck's own duck had already closed. A mode change is a
    // whole-terrain event: it goes at phase 0 with BOTH decks ducked.
    spky::Instrument inst;
    inst.init(48000.f);
    Flow f;
    f.init(&inst, 100.f);

    // Find two terrains that differ in mode.
    TerrainState a, b;
    for (uint32_t m = 1; m < 500; ++m) {
        TerrainState s; s.master = m;
        if (generate(s).base[P_MODE] > 0.5f) { b = s; } else { a = s; }
        if (a.master && b.master) break;
    }
    REQUIRE(a.master != 0);
    REQUIRE(b.master != 0);

    f.wake(a);
    for (int i = 0; i < 20; ++i) f.tick();
    const bool before = inst.step_on(spky::PART_A);

    f.wake(b);                       // instant, no blend: the mode really moved
    f.tick();
    CHECK(inst.step_on(spky::PART_A) != before);

    // Now the blended path: the mode must be live within one tick of the
    // press, not a quarter of the ramp later.
    f.wake(a);
    for (int i = 0; i < 20; ++i) f.tick();
    f.restore_undo(b, true);
    REQUIRE(f.undo());               // blends toward b
    f.tick();
    CHECK(f.blend_phase() < kCarrierStaggerFrac);
    CHECK(inst.step_on(spky::PART_A) == (generate(b).base[P_MODE] > 0.5f));
}

TEST_CASE("flow mode: P_MODE is scheduled with the texture deck, not the carrier") {
    CHECK(spky::flow::switch_phase_for_test(P_MODE) == 0.f);
}
```

Expose the scheduler for the test next to the existing `terrain_for_test`
pattern in `engine/flow/flow.h`:

```cpp
#ifdef SPKY_TESTING
    float switch_phase_for_test(int p) const { return switch_phase_for(p); }
#endif
```

and a free wrapper beside `terrain_of`:

```cpp
#ifdef SPKY_TESTING
inline float switch_phase_for_test(int p) {
    static Flow probe; return probe.switch_phase_for_test(p);
}
#endif
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build && ./build/spky_tests -tc="flow mode: P_MODE is scheduled*"
```

Expected: FAIL — `deck_of(P_MODE)` returns −1, so `switch_phase_for` falls
through to `_carrier_deck` and returns `kCarrierStaggerFrac` (0.25), not 0.

- [ ] **Step 3: Schedule P_MODE at phase 0**

In `engine/flow/flow.cpp`, amend `switch_phase_for`:

```cpp
float Flow::switch_phase_for(int p) const {
    // P_MODE is a whole-terrain event, not a per-deck one: set_sync is global
    // (instrument.h:274), so its switch flips BOTH decks' rate mapping at once.
    // Riding the carrier would jump the texture deck's clocking 1.5 s after
    // that deck's own duck had closed -- exactly what the stagger prevents. So
    // it goes at the press, with the texture deck, and begin_blend() opens both
    // ducks for it.
    if (p == P_MODE) return 0.f;
    int d = deck_of(p);
    if (d < 0) d = _carrier_deck;
    return d == _carrier_deck ? kCarrierStaggerFrac : 0.f;
}
```

- [ ] **Step 4: Duck both decks when the mode moves**

> **CORRECTION 2026-08-06, post-implementation — DO NOT RE-EXECUTE THIS STEP AS
> WRITTEN.** The `if (mode_moves) _duck_t[0] = _duck_t[1] = _t;` line below (and
> the comment block above it, which repeats the collapse and the gate-coverage
> claim) was built and then escalated as **Critical** in review. Overwriting the
> carrier's slot moves its duck off `kCarrierStaggerFrac`, leaving the carrier
> deck's own `P_ENGINE` switch — which stays at 1.5 s, the stagger being a by-ear
> decision the project owner re-affirmed — in the open, where `duck()` computes
> `u = 6` and returns the send unchanged. The shipped design instead makes
> `_duck_t` a **two-slot-per-deck** schedule and *adds* a duck:
> `if (mode_moves) _duck_t[_carrier_deck][1] = _t;`, with `duck()` combining a
> deck's slots by maximum. The gate-coverage claim in the snippet's comment is
> also wrong: only the press-instant duck lands inside `kBlendGateWindowS`; the
> stagger duck is still structurally outside it. Authority: `engine/flow/flow.cpp`
> `begin_blend()`, the corrected spec §5.2, and the `kBlendGateWindowS` comment
> in `engine/flow/taste.h`.

In `begin_blend`, where `_duck_t` is scheduled, add the mode case. The existing
code sets the texture deck's duck at `_t` and the carrier's at
`_t + kCarrierStaggerFrac * kBlendS`; override the carrier when the incoming
terrain's mode differs from the outgoing one:

```cpp
    // A mode change collapses the stagger on purpose: both decks' clocking
    // flips at the press, so both ducks open there too. A mode-changing NEW is
    // a harder cut than a same-mode one, which is honest -- the terrain is
    // going from ambient to rhythm or back. Both ducks then fall inside
    // kBlendGateWindowS, so the level gate actually covers this transition.
    const bool mode_moves =
        (_terrain.base[P_MODE] > 0.5f) != (_prev_terrain.base[P_MODE] > 0.5f);
    if (mode_moves) _duck_t[0] = _duck_t[1] = _t;
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests -tc="flow mode:*"
```

Expected: both new cases PASS.

- [ ] **Step 6: Listen to it once**

Build the VCV module and press NEW until a mode change happens (a drone
followed by an arp will usually do it):

```bash
cd host/vcv && ./build-local.sh
```

`ModLane::set_step` entering step mode sets `_song.new_pending`
(`lane.cpp:156-158`), so a mode flip regenerates phrase material under the
double duck. Confirm by ear that this reads as a transition rather than a
glitch. If it does not, record what you heard in the plan — do not retune the
duck constants silently, they are by-ear values.

- [ ] **Step 7: Commit**

```bash
git add engine/flow/flow.cpp engine/flow/flow.h tests/test_flow_mode.cpp
git commit -m "feat(flow): a mode change is a whole-terrain cut, ducked on both decks"
```

---

### Task 5: Re-measure every gate the mode moved

**Files:**
- Modify: `engine/flow/terrain.cpp:241-257` (the `distance()` commentary)
- Modify: `engine/flow/taste.h` (`kFixedSeedRmsMin/Max`, `kBlendSpikeDb`, `kBlendDropDb`, `kDiscreteChurnMax` if they move)
- Modify: `tests/test_flow_audio.cpp`
- Create: `bench/measure_mode_gates.cpp` (throwaway measuring harness, deleted in the last step)

**Interfaces:**
- Consumes: everything from Tasks 1–4.
- Produces: re-measured constants, each with a comment naming the measurement
  that produced it.

**This task is measurement, not tuning.** Every number below is *measured and
then written down*, never nudged until a test goes green. If a gate cannot be
made green by an honest measurement, that is a finding about the mode change,
and it gets reported rather than absorbed.

- [ ] **Step 1: Run the full suite and record what is red**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tee /tmp/mode-gates-before.txt
```

Write the list of failing cases into the commit message draft. Expect
`test_flow_audio.cpp` cases; `check_render_hash.cmake` should stay green (it
drives the render host over engine scenarios, not the flow layer) — if it went
red, stop and investigate, because that means the mode work touched the engine
core.

- [ ] **Step 2: Measure the terrain distance distribution**

Write `bench/measure_mode_gates.cpp`:

```cpp
// Throwaway: re-measures the numbers quoted in terrain.cpp's distance()
// comment after P_MODE joined the parameter table. Deleted once the comment
// is updated.
#include "flow/terrain.h"
#include "flow/taste.h"
#include <cstdio>
#include <algorithm>
using namespace spky::flow;

int main() {
    float mn = 1e9f, mx = -1e9f, sum = 0.f;
    int same_arch = 0, same_arch_clears = 0, n = 0;
    for (uint32_t i = 1; i <= 20000; ++i) {
        TerrainState a, b; a.master = i * 2 - 1; b.master = i * 2;
        const Terrain ta = generate(a), tb = generate(b);
        float base = 0.f;
        for (int p = 0; p < P_COUNT; ++p)
            base += std::fabs(ta.base[p] - tb.base[p])
                  / (kParams[p].hi - kParams[p].lo);
        base /= float(P_COUNT);
        mn = std::min(mn, base); mx = std::max(mx, base); sum += base; ++n;
        if (ta.arch == tb.arch) {
            ++same_arch;
            if (base >= kDistanceMin) ++same_arch_clears;
        }
    }
    std::printf("base-patch mean: min %.4f mean %.4f max %.4f (n=%d)\n",
                mn, sum / float(n), mx, n);
    std::printf("same-archetype pairs %d, of which clear kDistanceMin: %d\n",
                same_arch, same_arch_clears);

    Rng seq; seq.seed(12345);
    TerrainState cur; cur.master = 1;
    int same = 0;
    for (int i = 0; i < 3000; ++i) {
        const TerrainState nxt = draw_new(cur, seq);
        if (generate(nxt).arch == generate(cur).arch) ++same;
        cur = nxt;
    }
    std::printf("draw_new returned a same-archetype terrain %d times in 3000\n",
                same);
    return 0;
}
```

Add it to `bench/CMakeLists.txt` following the pattern of the existing bench
targets, build, and run it:

```bash
cmake --build build --target measure_mode_gates && ./build/bench/measure_mode_gates
```

- [ ] **Step 3: Rewrite the distance() commentary with the measured numbers**

Replace the numbers in `terrain.cpp:241-257` with what Step 2 printed, and add
one sentence naming why they moved:

```cpp
// Re-measured 2026-08-06 after P_MODE joined the parameter table (spec
// §5.1): the mean is over P_COUNT params, so one more param changes the
// denominator, and a mode mismatch contributes a full 1.0/P_COUNT of its own.
// Over 20 000 random terrain pairs the base-patch mean spans
// <MIN> (min) / <MEAN> (mean) / <MAX> (max) ... draw_new returned a
// same-archetype terrain <N> times in 3 000 calls.
```

Keep the closing paragraph unchanged: `kDistanceMin` and the flat `0.25` stay
by-ear knobs and must not be "fixed" from the code side.

- [ ] **Step 4: Re-measure the audio gates**

For each red case in `test_flow_audio.cpp`, read the comment in `taste.h` that
records how its constant was originally measured, and repeat *that* measurement
against the new mode-drawing generator:

- `kFixedSeedRmsMin/Max` — windowed RMS across the same 8 terrains at all macros
  0.5. Record the new band and set the constants with the same margins the old
  comment describes (two orders of magnitude below `kCalmCornerRmsMax` on the
  low side, below full scale on the high side).
- `kBlendSpikeDb` / `kBlendDropDb` — the differential press-vs-control
  comparison inside `kBlendGateWindowS`. The mode change now ducks **both**
  decks at phase 0, which falls inside the 1.0 s window, so the carrier deck's
  transition is measured here for the first time. If the measured spike exceeds
  6 dB, report it: it means the double duck is not covering the transition, and
  that is a design finding for the spec, not a constant to raise.
- `kDiscreteChurnMax` — `P_MODE` is a discrete and can now change; confirm the
  per-60 s churn count with static macros still fits, and if not, state the new
  measured maximum.

Update each constant's comment to say it was re-measured on 2026-08-06 and what
the measurement was.

- [ ] **Step 5: Re-choose kHouseCode by ear**

`kHouseCode` is documented in `taste.h:43-49` as a placeholder chosen by a
render pass that could only judge level, to be re-chosen by ear once Glow could
be played. Build the VCV module, press NEW until a terrain is worth waking on,
and read its code from the module:

```bash
cd host/vcv && ./build-local.sh
```

Replace `kHouseCode` and rewrite its comment to say it was chosen by ear on
2026-08-06, naming what it sounds like. Delete the paragraph explaining the
render-pass placeholder.

- [ ] **Step 6: Delete the measuring harness and run everything**

```bash
git rm bench/measure_mode_gates.cpp
# revert the bench/CMakeLists.txt entry too
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: fully green.

- [ ] **Step 7: Commit**

```bash
git add engine/flow/terrain.cpp engine/flow/taste.h tests/test_flow_audio.cpp bench/CMakeLists.txt
git commit -m "test(flow): the mode change moved four gates, so all four were measured again"
```

---

## Done when

- `ctest --test-dir build --output-on-failure` is green.
- No reachable tick has `step_on() != synced()` on either deck.
- `P_MODE == P_COUNT - 1`, asserted by test.
- Every constant that moved carries a comment naming its new measurement and
  its date.
- `kHouseCode` was chosen by ear, not by render.
