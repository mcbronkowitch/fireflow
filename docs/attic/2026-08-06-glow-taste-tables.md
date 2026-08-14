# Glow Taste Tables Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Encode the by-ear rules into the flow taste tables — hard vetoes, archetype-conditional spans, musical weights and a per-draw adventure level — so NEW stops producing terrains that are broken for reasons already known.

**Architecture:** Four independent mechanisms on top of the existing tables. A `kVetos` table of absolute limits, enforced by a build-time test over the tables and a runtime clamp for the one path the tables cannot see (the blend residual). An `arch_window` on `StoryVariant` so an archetype reads a smaller part of a macro's story without duplicating curves. Weight vectors for parameters whose good values are a set, not a range. And an adventure level `a` drawn per terrain that widens spans and flattens weights, so chaos stays possible and rare.

**Tech Stack:** C++17, clang + Ninja, doctest (vendored in `third_party/`), CTest.

**Spec:** `docs/superpowers/specs/2026-08-06-glow-taste-structure-design.md` §3, §3.1, §4, §6, §7.

**Depends on:** `docs/superpowers/plans/2026-08-06-glow-operating-mode.md` must be complete. Task 5 of this plan needs `P_MODE` to exist, and the audio gates must already have been re-measured once, or this plan's changes and that plan's changes will be indistinguishable in the measurements.

## Global Constraints

- Build engine/tests with clang + Ninja, never MSVC. `source env.sh` first, then
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`. **`-DCMAKE_BUILD_TYPE=Release`
  is not optional** — a Debug configure makes `spky_tests` and `ctrl_identity`
  fail with "SYNTH reference moved".
- Commit trailer is `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **Every test must be proven RED once** before its implementation lands.
- All tuning data lives in `engine/flow/taste.h`. No tuning numbers in
  `terrain.cpp` or `flow.cpp` — those files are plumbing that walks the tables.
- Every table entry carries a comment naming the rule it implements.
- FireFlow is in dev alpha: saved patches may change what they sound like. No
  migrations, no compatibility shims.
- These values are by-ear decisions. Where a step says "confirm by ear", that is
  a real gate — do not substitute a measurement for a listening judgment.

## File Structure

| File | Responsibility after this plan |
|---|---|
| `engine/flow/taste.h` | `kVetos`; redrawn story curves; edited base rules; `arch_window` data; `kRateRungW`, `kStepsW`, `kShuffleSkew`; adventure constants |
| `engine/flow/terrain.h` | `arch_window` member on `StoryVariant`; `Terrain::adventure` |
| `engine/flow/terrain.cpp` | `draw_span` (adventure-aware), weighted rung/step picks, window remap, adventure draw |
| `engine/flow/flow.cpp` | runtime veto clamp in the push tail; window applied to `_eff` |
| `engine/flow/flow_rng.h` | `kStreamAdventure` id |
| `tests/test_flow_veto.cpp` | **new** — table conformance and the blend-residual runtime case |
| `tests/test_flow_taste.cpp` | window defaults, weight table shapes |
| `tests/test_flow_terrain.cpp` | draw distributions, adventure statistics |

---

### Task 1: The veto table, and the curves that breach it

**Files:**
- Modify: `engine/flow/taste.h` (add `kVetos`; redraw five curve targets; edit `P_COMP_B`)
- Create: `tests/test_flow_veto.cpp`
- Modify: `CMakeLists.txt:165` (register the new test file)

**Interfaces:**
- Produces: `struct Veto { int param; float lo, hi; };` and
  `inline const Veto kVetos[]`; `inline const int kVetoCount`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_flow_veto.cpp`:

```cpp
// tests/test_flow_veto.cpp
#include "doctest/doctest.h"
#include "flow/taste.h"
#include "flow/flow_params.h"
using namespace spky::flow;

namespace {
const Veto* veto_for(int param) {
    for (int i = 0; i < kVetoCount; ++i)
        if (kVetos[i].param == param) return &kVetos[i];
    return nullptr;
}
} // namespace

TEST_CASE("flow veto: no table span may leave a veto band") {
    // This is the enforcement. A veto that only held at runtime would let a
    // broken table ship silently; here a bad span is a red build.
    for (int i = 0; i < kBaseRuleCount; ++i) {
        const Veto* v = veto_for(kBaseRules[i].param);
        if (!v) continue;
        for (int a = 0; a < ARCH_COUNT; ++a) {
            CAPTURE(kParams[kBaseRules[i].param].name); CAPTURE(a);
            CHECK(kBaseRules[i].per_arch[a].lo >= v->lo);
            CHECK(kBaseRules[i].per_arch[a].hi <= v->hi);
        }
    }
    for (int s = 0; s < kStoryCount; ++s)
        for (int t = 0; t < kStories[s].n_targets; ++t) {
            const auto& c = kStories[s].targets[t];
            const Veto* v = veto_for(c.param);
            if (!v) continue;
            for (int b = 0; b < 5; ++b) {
                CAPTURE(kStories[s].name); CAPTURE(kParams[c.param].name);
                CAPTURE(b);
                CHECK(c.bp[b].lo >= v->lo);
                CHECK(c.bp[b].hi <= v->hi);
            }
        }
}

TEST_CASE("flow veto: the table is well formed and inside kParams") {
    for (int i = 0; i < kVetoCount; ++i) {
        CAPTURE(kParams[kVetos[i].param].name);
        CHECK(kVetos[i].lo < kVetos[i].hi);
        CHECK(kVetos[i].lo >= kParams[kVetos[i].param].lo);
        CHECK(kVetos[i].hi <= kParams[kVetos[i].param].hi);
    }
}
```

- [ ] **Step 2: Add only the table, then run to see RED**

Add to `engine/flow/taste.h`, after the scalar tuning block:

```cpp
// ---------------------------------------------------------------------------
// Hard by-ear limits (spec 2026-08-06 §3). These hold under EVERY archetype,
// every macro position, every weather offset and every adventure level. A row
// here is a claim that no music in this box ever wants that value.
//
// THIS IS NOT THE COMPLETE LIST OF HARD LIMITS. Two live elsewhere on purpose:
//   - P_RES's 0.75 ceiling is in kParams (flow_params.h) because that range
//     also normalises the terrain distance metric in terrain.cpp.
//   - kBodyFiltFloor is a runtime clamp in flow.cpp because it is conditional
//     on a deck's engine, and this table is engine-independent.
struct Veto { int param; float lo, hi; };
inline const Veto kVetos[] = {
    { P_REV_MOD,  0.00f, 0.25f },  // above: the reverb tail comes apart
    { P_DRIVE,    0.00f, 0.40f },  // above: the limiter rides and DRIVE stops
                                   // controlling dirt (it only gets louder)
    { P_COMP_A,   0.10f, 0.50f },  // never uncompressed, never squashed
    { P_COMP_B,   0.10f, 0.50f },
    { P_REVMIX_A, 0.08f, 1.00f },  // never fully dry
    { P_REVMIX_B, 0.08f, 1.00f },
};
inline const int kVetoCount = int(sizeof(kVetos) / sizeof(kVetos[0]));
```

Register `tests/test_flow_veto.cpp` in `CMakeLists.txt`, then:

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/spky_tests -tc="flow veto: no table span*"
```

Expected: **FAIL**, with these specific breaches — record them in the commit
message, they are the RED proof:
`MOTION "orbit"` `P_REV_MOD` bp3 (`.25–.45`) and bp4 (`.45–.85`);
`DIRT "heat"` `P_DRIVE` bp4 (`.3–.7`); `DIRT "heat"` `P_COMP_A` bp2 (`.35–.55`),
bp3 (`.4–.6`), bp4 (`.5–.75`); `SPACE "bloom"` `P_REVMIX_A/B` bp0 (`.02–.1`);
base rule `P_COMP_B` (`.3–.6`).

- [ ] **Step 3: Redraw the curves, do not clip them**

Clipping would leave dead knob travel — the top quarter of MOTION would move
nothing. Each curve is rescaled into its allowed band instead.

In `engine/flow/taste.h`, MOTION "orbit":

```cpp
{ M_MOTION, "orbit", 4, {
  { P_TIDE,      {{0.f,.05f},{.1f,.2f},{.25f,.4f},{.45f,.6f},{.7f,1.f}} },
  { P_DRIFT,     {{0.f,.05f},{.05f,.15f},{.2f,.35f},{.4f,.55f},{.6f,.9f}} },
  // SMEAR is the diffuser LFO (the wash). It carries the seasick end now that
  // WOBL is capped: smear washes the reverb where MOD tears it.
  { P_REV_SMEAR, {{0.f,.05f},{.05f,.2f},{.2f,.4f},{.4f,.65f},{.65f,.95f}} },
  // WOBL, capped at the veto. Flattens out rather than stopping dead, so the
  // top of the knob still moves it -- just inside the band that survives.
  { P_REV_MOD,   {{0.f,.03f},{.03f,.08f},{.08f,.14f},{.14f,.20f},{.10f,.25f}} } } },
```

DIRT "heat":

```cpp
{ M_DIRT, "heat", 4, {
  { P_GRIT_A,    {{0.f,0.f},{.05f,.15f},{.2f,.4f},{.45f,.65f},{.7f,1.f}} },
  { P_GRIT_B,    {{0.f,0.f},{.05f,.12f},{.15f,.35f},{.4f,.6f},{.65f,.95f}} },
  // COMP rescaled into 0.10-0.50, relative shape kept.
  { P_COMP_A,    {{.25f,.38f},{.25f,.38f},{.28f,.42f},{.32f,.46f},{.35f,.50f}} },
  // PUSH joins in Q4 only (the threshold rule), inside the veto band.
  { P_DRIVE,     {{0.f,0.f},{0.f,0.f},{0.f,0.f},{0.f,.05f},{.25f,.40f}} } } },
```

SPACE "bloom", bp0 of both REVMIX targets:

```cpp
  { P_REVMIX_A,  {{.08f,.15f},{.15f,.3f},{.35f,.5f},{.5f,.7f},{.75f,.95f}} },
  { P_REVMIX_B,  {{.08f,.15f},{.15f,.3f},{.35f,.5f},{.5f,.7f},{.75f,.95f}} },
```

And the base rule:

```cpp
{ P_COMP_B,   {{.3f,.5f},{.3f,.5f},{.3f,.5f},{.3f,.5f}} },     // gentle glue
```

Also update the BRIGHT "dawn" comment block, which currently claims REVMIX's Q1
dips toward dry — it now bottoms at the veto floor, not at silence.

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: the veto cases PASS. `test_flow_taste.cpp`'s monotonicity check must
also still pass — the redrawn WOBL curve is monotone ascending on `lo` bounds
(`0, .03, .08, .14, .10`) — **note bp4 lo (.10) is below bp3 lo (.14)**, which
will fail that check. Fix by making bp4 `{.20f,.25f}` and re-running.

- [ ] **Step 5: Listen to MOTION at full**

```bash
cd host/vcv && ./build-local.sh
```

Sweep MOTION to the top on several terrains. The question is whether the end of
the knob still reads as "seasick" now that SMEAR and TIDE carry it instead of
WOBL. If it reads as merely "wobbly", raise SMEAR's bp4 rather than WOBL's.

- [ ] **Step 6: Commit**

```bash
git add engine/flow/taste.h tests/test_flow_veto.cpp CMakeLists.txt
git commit -m "feat(flow): the taste tables get their hard limits, and four curves get redrawn"
```

---

### Task 2: The runtime clamp, for the one path the tables cannot see

**Files:**
- Modify: `engine/flow/flow.cpp` (push tail, before the change guard)
- Modify: `tests/test_flow_veto.cpp`

**Interfaces:**
- Consumes: `kVetos`, `kVetoCount` (Task 1).
- Produces: no reachable `param_now(p)` outside a veto band, at any tick.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_veto.cpp`:

```cpp
#include "flow/flow.h"
#include "instrument.h"

TEST_CASE("flow veto: a macro moved mid-blend cannot breach a veto") {
    // The mechanism this guards, and the ONLY one the clamp is for:
    // flow.cpp's blend line clamps to kParams, not to the veto band, and its
    // own comment says the sum can exceed a param's range even when both
    // terrains are inside it. _resid is frozen at press time while prv[]
    // re-evaluates live, so moving a knob DURING the ramp is what breaks it.
    // A static macro grid would never reach this -- that test could not fail.
    spky::Instrument inst;
    inst.init(48000.f);
    Flow f;
    f.init(&inst, 100.f);

    for (uint32_t master = 1; master <= 60; ++master) {
        TerrainState st; st.master = master;
        f.wake(st);
        for (int m = 0; m < MACRO_COUNT; ++m) f.set_macro(m, 0.5f);
        for (int i = 0; i < 40; ++i) f.tick();

        REQUIRE(f.new_full());              // start a blend

        // Sweep every macro across its full travel while the ramp runs.
        const int ticks = int(kBlendS * 100.f) + 20;
        for (int i = 0; i < ticks; ++i) {
            const float u = float(i) / float(ticks);
            for (int m = 0; m < MACRO_COUNT; ++m)
                f.set_macro(m, (m % 2) ? u : 1.f - u);
            f.tick();
            for (int v = 0; v < kVetoCount; ++v) {
                const float got = f.param_now(kVetos[v].param);
                CAPTURE(master); CAPTURE(i);
                CAPTURE(kParams[kVetos[v].param].name); CAPTURE(got);
                CHECK(got >= kVetos[v].lo - 1e-5f);
                CHECK(got <= kVetos[v].hi + 1e-5f);
            }
        }
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build && ./build/spky_tests -tc="flow veto: a macro moved mid-blend*"
```

Expected: FAIL on `P_REVMIX_A` or `P_REVMIX_B` dipping below 0.08. If it passes
on the first run, the test is not exercising the residual — increase the sweep
speed (move the macros a full travel within `kBlendS`) until it goes red, and
say in the commit message which seed and tick produced the breach.

- [ ] **Step 3: Add the clamp**

In `engine/flow/flow.cpp`'s `recompute_and_push`, immediately before the
"Setter spam guard" block:

```cpp
        // The veto band (taste.h kVetos, spec §3), enforced HERE and only
        // here at runtime. The build-time test already proves no table span
        // leaves the band, so a settled terrain cannot breach one. What can:
        // the blend line above clamps to kParams, not to the veto band, and
        // _resid is frozen at press time while prv[] keeps re-evaluating --
        // so a macro moved DURING a ramp can push the sum outside even though
        // both terrains are legal. This runs before the change guard because
        // param_now() is a public observer and must never show a vetoed value.
        for (int vi = 0; vi < kVetoCount; ++vi) {
            if (kVetos[vi].param != p) continue;
            if (v < kVetos[vi].lo) v = kVetos[vi].lo;
            else if (v > kVetos[vi].hi) v = kVetos[vi].hi;
            break;
        }
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS across all 60 masters.

- [ ] **Step 5: Commit**

```bash
git add engine/flow/flow.cpp tests/test_flow_veto.cpp
git commit -m "fix(flow): the blend residual could breach a veto, so the clamp lands after it"
```

---

### Task 3: Base rule edits

**Files:**
- Modify: `engine/flow/taste.h` (`P_SHAPE_A/B`, `P_REV_DIFF`, `P_STEPS_B`)
- Modify: `tests/test_flow_taste.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: drone `P_SHAPE_A/B` spans capped at 0.25.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_taste.cpp`:

```cpp
TEST_CASE("flow taste: drones get round LFOs only") {
    // waveforms.h shape_value morphs sine(0) -> triangle(.25) -> ramp(.5) ->
    // pulse(.75) -> S&H(1). From the ramp up the lane emits a discontinuity
    // per cycle, and that is what makes a drone read as rhythmic. So a drone
    // may only draw the sine..triangle quarter. This is mechanical, not taste.
    const int shape[2] = { P_SHAPE_A, P_SHAPE_B };
    for (int i = 0; i < kBaseRuleCount; ++i)
        for (int k = 0; k < 2; ++k)
            if (kBaseRules[i].param == shape[k]) {
                CAPTURE(kParams[shape[k]].name);
                CHECK(kBaseRules[i].per_arch[ARCH_DRONE].hi <= 0.25f);
                // The other archetypes stay wildcards: nothing collected says
                // an arp may not have an angular LFO.
                CHECK(kBaseRules[i].per_arch[ARCH_ARP].hi > 0.25f);
            }
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build && ./build/spky_tests -tc="flow taste: drones get round LFOs only"
```

Expected: FAIL — drone `P_SHAPE_A` hi is 1.0, so three quarters of every drone
draw currently gets an angular LFO.

- [ ] **Step 3: Edit the three rows**

```cpp
// SHAPE morphs sine(0) -> tri(.25) -> ramp(.5) -> pulse(.75) -> S&H(1)
// (mod/waveforms.h). A drone gets the round quarter only: from the ramp up the
// lane emits a per-cycle discontinuity and the drone reads as rhythmic. The
// other archetypes keep the full wildcard.
{ P_SHAPE_A,  {{0.f,.25f},{0.f,1.f},{0.f,1.f},{0.f,1.f}} },
{ P_SHAPE_B,  {{0.f,.25f},{0.f,1.f},{0.f,1.f},{0.f,1.f}} },
```

```cpp
// DIFF: 0.4-0.6 is simply not wanted, so this is a span narrowing rather than
// a weight -- the value is meant to be unreachable.
{ P_REV_DIFF, {{.6f,.8f},{.6f,.8f},{.6f,.8f},{.6f,.8f}} },
```

```cpp
// Drones normally have STEP off entirely (kModeW), but a drone that does draw
// the step mode gets the same preferred counts as everything else, so the
// 8/16 weight has something to bite on.
{ P_STEPS_B,  {{2.f,16.f},{4.f,10.f},{8.f,16.f},{4.f,12.f}} },
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

- [ ] **Step 5: Listen to a handful of drones**

```bash
cd host/vcv && ./build-local.sh
```

Press NEW until several drone terrains come up. The expected change is that
they stop having a pulse in them. If a drone now reads as *too* static, the fix
is TIDE or DRIFT, not raising the SHAPE ceiling.

- [ ] **Step 6: Commit**

```bash
git add engine/flow/taste.h tests/test_flow_taste.cpp
git commit -m "fix(flow): a drone's LFOs stop being square three times out of four"
```

---

### Task 4: The archetype window on story curves

**Files:**
- Modify: `engine/flow/terrain.h:27-30` (`StoryVariant`)
- Modify: `engine/flow/taste.h` (window on DENSITY "rate")
- Modify: `engine/flow/flow.cpp` (`eval_terrain` remaps `_eff`)
- Modify: `tests/test_flow_taste.cpp`, `tests/test_flow_terrain.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `Span StoryVariant::arch_window[ARCH_COUNT]`, defaulting to
  `{0,1}`; `Terrain::window[MACRO_COUNT]` holding the picked variant's window
  for this terrain's archetype, so the runtime does not re-read `kStories`.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_taste.cpp`:

```cpp
TEST_CASE("flow taste: story windows default to the whole curve") {
    for (int s = 0; s < kStoryCount; ++s)
        for (int a = 0; a < ARCH_COUNT; ++a) {
            CAPTURE(kStories[s].name); CAPTURE(a);
            CHECK(kStories[s].arch_window[a].lo >= 0.f);
            CHECK(kStories[s].arch_window[a].hi <= 1.f);
            CHECK(kStories[s].arch_window[a].lo < kStories[s].arch_window[a].hi);
            // Only DENSITY "rate" narrows, and only for drone. Everything else
            // must stay on the default or today's sound changes for no reason.
            const bool narrows = kStories[s].macro == M_DENSITY
                              && a == ARCH_DRONE;
            if (!narrows) {
                CHECK(kStories[s].arch_window[a].lo == 0.f);
                CHECK(kStories[s].arch_window[a].hi == 1.f);
            }
        }
}

TEST_CASE("flow taste: a drone's density knob stays in the sparse half") {
    for (int s = 0; s < kStoryCount; ++s) {
        if (kStories[s].macro != M_DENSITY) continue;
        CHECK(kStories[s].arch_window[ARCH_DRONE].hi <= 0.5f);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build && ./build/spky_tests -tc="flow taste: story windows*"
```

Expected: **compile error**, `'struct StoryVariant' has no member named
'arch_window'`.

- [ ] **Step 3: Add the member, last, with a default**

In `engine/flow/terrain.h` — note the member goes **after** `targets` so every
existing positional initializer in `kStories` keeps compiling:

```cpp
struct StoryVariant {
    Macro macro; const char* name;
    int n_targets; CurveRule targets[6];             // max 6 targets per macro
    // Where each archetype reads this story (spec 2026-08-06 §4). The knob
    // still sweeps its full physical travel; only the sampling position is
    // remapped, so a narrower window means the macro covers a smaller part of
    // the story and never reaches the rest. Default is the whole curve, and
    // this member is LAST with a default initialiser so the existing
    // positional entries in kStories need no edit.
    Span arch_window[ARCH_COUNT] = {{0.f,1.f},{0.f,1.f},{0.f,1.f},{0.f,1.f}};
};
```

Add the window to the DENSITY "rate" entry in `taste.h` by naming it after the
targets:

```cpp
// DENSITY rate-led (§3 row 2a): events carry the sweep. Drone reads only the
// sparse part of it -- a drone at full DENSITY lands where an arp sits at
// half, and STEPS_A comes down with it because it lives in the same story.
{ M_DENSITY, "rate", 3, {
  { P_DENSITY_A, {{.02f,.08f},{.1f,.2f},{.3f,.5f},{.5f,.7f},{.7f,.95f}} },
  { P_DENSITY_B, {{.02f,.08f},{.08f,.18f},{.25f,.45f},{.45f,.65f},{.65f,.9f}} },
  { P_STEPS_A,   {{2.f,4.f},{4.f,6.f},{6.f,10.f},{10.f,13.f},{13.f,16.f}} } },
  {{0.f,.45f},{0.f,1.f},{0.f,1.f},{0.f,1.f}} },
```

- [ ] **Step 4: Carry the window into the Terrain and apply it**

In `engine/flow/terrain.h`, add to `struct Terrain`:

```cpp
    // The picked variant's window for THIS terrain's archetype, per macro.
    // Copied at generate() time so the runtime never re-reads kStories.
    Span window[MACRO_COUNT];
```

In `terrain.cpp`'s stage 4, where the picked variant is recorded:

```cpp
            if (picked) {
                mm.story = s;                    // global kStories index
                t.window[m] = sv.arch_window[t.arch];
            }
```

In `flow.cpp`'s `eval_terrain`, remap the macro position per terrain:

```cpp
    for (int m = 0; m < MACRO_COUNT; ++m) {
        const MacroMap& mm = t.map[m];
        // The archetype window (spec §4). eff already carries knob + CV +
        // weather and is clamped 0..1, so CV and weather ride inside the
        // window exactly like the knob rather than bypassing it.
        const Span& w = t.window[m];
        const float pos = w.lo + eff[m] * (w.hi - w.lo);
        for (int i = 0; i < mm.n_targets; ++i) {
            const Curve& c = mm.targets[i];
            const float v = eval_curve(c, pos);
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS. Any other flow test that asserted a DENSITY value for a drone
terrain will move — check each one is moving for this reason before touching it.

- [ ] **Step 6: Commit**

```bash
git add engine/flow/terrain.h engine/flow/terrain.cpp engine/flow/flow.cpp engine/flow/taste.h tests/test_flow_taste.cpp
git commit -m "feat(flow): a drone's DENSITY knob reads only the sparse half of the story"
```

---

### Task 5: Weights — rate rungs, step counts, shuffle

**Files:**
- Modify: `engine/flow/taste.h` (`kRateRungW`, `kStepsW`, `kShuffleSkew`)
- Modify: `engine/flow/terrain.cpp` (weighted picks in stage 3 and `draw_curve`)
- Modify: `tests/test_flow_terrain.cpp`

**Interfaces:**
- Consumes: `P_MODE` (operating-mode plan), `pick_weighted` (`terrain.cpp:20`).
- Produces: `kRateRungW[kDivisionCount]`, `kStepsW[15]` (indices for step counts
  2..16), `kShuffleSkew`.

**Scope note, stated because it limits the result:** `P_RATE_A/B`, `P_STEPS_B`
and `P_SHUFFLE` are base rules and get their weight at draw time, cleanly.
`P_STEPS_A` is **storied** (owned by DENSITY "rate"), so its runtime value is
interpolated from a curve. Its weight therefore applies to the five drawn
breakpoints, not to every reachable value — the DENSITY knob's endpoints prefer
8 and 16, and positions between two breakpoints still land between them. That
is a real limitation and must go in the code comment, not be papered over.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_terrain.cpp`:

```cpp
#include "mod/divisions.h"

TEST_CASE("flow terrain: synced rates prefer the straight rungs") {
    // divisions.h's ladder is speed-sorted, so dotted and triplet rungs sit
    // BETWEEN the straight ones -- a uniform draw hits them roughly half the
    // time in the middle of the range. They stay reachable (this is a weight,
    // not a veto), they just get rare.
    auto crooked = [](int idx) {
        const char* n = kDivisions[idx].name;
        for (const char* c = n; *c; ++c) if (*c == '.' || *c == 'T') return true;
        return false;
    };
    int total = 0, odd = 0;
    for (uint32_t master = 1; master <= 4000; ++master) {
        TerrainState st; st.master = master;
        const Terrain t = generate(st);
        if (t.base[P_MODE] < 0.5f) continue;            // free mode has no ladder
        for (int p : { P_RATE_A, P_RATE_B }) {
            ++total;
            if (crooked(division_index(t.base[p]))) ++odd;
        }
    }
    REQUIRE(total > 500);
    const float share = float(odd) / float(total);
    CAPTURE(share);
    CHECK(share < 0.20f);      // was roughly 0.5 with a uniform draw
    CHECK(share > 0.01f);      // still reachable -- a weight, not a veto
}

TEST_CASE("flow terrain: step counts prefer 8 and 16") {
    int total = 0, preferred = 0;
    for (uint32_t master = 1; master <= 4000; ++master) {
        TerrainState st; st.master = master;
        const Terrain t = generate(st);
        const int s = int(t.base[P_STEPS_B] + 0.5f);
        ++total;
        if (s == 8 || s == 16) ++preferred;
    }
    const float share = float(preferred) / float(total);
    CAPTURE(share);
    CHECK(share > 0.45f);
    CHECK(share < 0.95f);      // other counts still happen
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build && ./build/spky_tests -tc="flow terrain: synced rates*" -tc="flow terrain: step counts*"
```

Expected: FAIL — the crooked share sits near 0.4–0.5 and the 8/16 share near
0.15, because every draw is uniform inside its span.

- [ ] **Step 3: Add the weight tables**

In `engine/flow/taste.h`:

```cpp
// ---------------------------------------------------------------------------
// Musical weights (spec 2026-08-06 §6). These are WEIGHTS, not vetoes: the
// unlikely values stay reachable and simply come up rarely, and the adventure
// draw flattens them further (terrain.cpp).
//
// Rung preference on kDivisions (mod/divisions.h), which is speed-sorted, so
// the dotted and triplet rungs sit between the straight ones. Straight rungs
// weigh 1, dotted 0.20, triplet 0.15.
inline const float kRateRungW[kDivisionCount] = {
//  8bar 4bar 2bar 1bar  1/2.  1/2  1/4.  1/2T  1/4  1/8.  1/4T  1/8
    1.0f,1.0f,1.0f,1.0f, .20f,1.0f, .20f, .15f,1.0f, .20f, .15f,1.0f,
//  1/16. 1/8T 1/16  1/16T 1/32
     .20f,.15f,1.0f,  .15f, 1.0f,
};
// Step counts 2..16 (index = count - 2). 8 and 16 are the counts actually
// played; 4 and 12 are usable; the rest exist for the rare terrain.
inline const float kStepsW[15] = {
//  2    3    4    5    6    7    8    9   10   11   12   13   14   15   16
   .15f,.05f,.50f,.05f,.20f,.05f,1.0f,.05f,.15f,.05f,.50f,.05f,.10f,.05f,1.0f,
};
// SHUFFLE has no rungs to weight, so its bias is a skew inside the drawn span:
// v = lo + (hi-lo) * u^kShuffleSkew. Above 1 pulls toward the low end; a heavy
// -shuffle fragment stays reachable, which a narrowed span would have killed.
constexpr float kShuffleSkew = 2.5f;
```

`taste.h` must now include `mod/divisions.h` for `kDivisionCount`.

- [ ] **Step 4: Apply the weights in terrain.cpp**

Add these helpers in the anonymous namespace **above `draw_curve`** (currently
`terrain.cpp:45`) — `draw_curve` calls `snap_steps` later in this step, so it
must already be declared. `terrain.cpp` must include `mod/divisions.h`:

```cpp
// Snap a normalized rate to a weighted rung of the divisions.h ladder, chosen
// among the rungs that fall inside the drawn span. Free-mode terrains skip
// this entirely -- there is no ladder to snap to.
float snap_rate(Rng& r, float lo, float hi) {
    int idx[kDivisionCount], n = 0;
    float w[kDivisionCount];
    for (int i = 0; i < kDivisionCount; ++i) {
        const float norm = float(i) / float(kDivisionCount - 1);
        if (norm < lo || norm > hi) continue;
        idx[n] = i; w[n] = kRateRungW[i]; ++n;
    }
    if (n == 0) return lo;                  // span narrower than one rung
    return float(idx[pick_weighted(r, w, n)]) / float(kDivisionCount - 1);
}

// Weighted integer step count inside a span.
float snap_steps(Rng& r, float lo, float hi) {
    int val[15], n = 0;
    float w[15];
    for (int s = 2; s <= 16; ++s) {
        if (float(s) < lo - 1e-4f || float(s) > hi + 1e-4f) continue;
        val[n] = s; w[n] = kStepsW[s - 2]; ++n;
    }
    if (n == 0) return lo;
    return float(val[pick_weighted(r, w, n)]);
}
```

In stage 3, after the uniform base draw, replace the value for the three
weighted base-rule params:

```cpp
        if (br.param == P_RATE_A || br.param == P_RATE_B) {
            // Only meaningful synced: in free mode RATE is free_hz's continuous
            // curve and there is no ladder. base[P_MODE] is already drawn.
            if (t.base[P_MODE] > 0.5f)
                t.base[br.param] = snap_rate(r, s.lo, s.hi);
        } else if (br.param == P_STEPS_B) {
            t.base[br.param] = snap_steps(r, s.lo, s.hi);
        } else if (br.param == P_SHUFFLE) {
            const float u = r.next_unipolar();
            t.base[br.param] = s.lo + (s.hi - s.lo)
                             * std::pow(u, kShuffleSkew);
        }
```

**Ordering requirement:** `P_MODE` is drawn in stage 3b, *after* the stage-3
loop. Move the stage 3b block to run **before** the loop, and update its comment
to say why: the RATE weight depends on the mode. `P_MODE`'s own draw uses its
own stream and does not consume the loop's, so moving it changes no other draw.

For the storied `P_STEPS_A`, snap each drawn breakpoint inside `draw_curve`:

```cpp
    for (int b = 0; b < 5; ++b) {
        c.bp[b] = cr.bp[b].lo + r.next_unipolar() * (cr.bp[b].hi - cr.bp[b].lo);
        // STEPS is storied (DENSITY owns it), so the weight can only reach the
        // five drawn breakpoints -- the DENSITY knob's ENDPOINTS prefer 8 and
        // 16, and a knob position between two breakpoints still interpolates
        // between them. That is the honest limit of weighting a storied
        // discrete; snapping at runtime instead would fight the hysteresis.
        if (cr.param == P_STEPS_A)
            c.bp[b] = snap_steps(r, cr.bp[b].lo, cr.bp[b].hi);
    }
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS, with the crooked share well under 0.20 and still above 0.01.

- [ ] **Step 6: Prove the tests can fail**

Set every entry of `kRateRungW` to `1.0f`, rebuild, confirm the crooked-share
case goes RED, revert. Then set `kStepsW` uniform, confirm the 8/16 case goes
RED, revert.

- [ ] **Step 7: Commit**

```bash
git add engine/flow/taste.h engine/flow/terrain.cpp tests/test_flow_terrain.cpp
git commit -m "feat(flow): rates land on bars and steps land on 8 or 16, most of the time"
```

---

### Task 6: The adventure draw

**Files:**
- Modify: `engine/flow/flow_rng.h` (`kStreamAdventure`)
- Modify: `engine/flow/terrain.h` (`Terrain::adventure`)
- Modify: `engine/flow/terrain.cpp` (`draw_span`, tempered weights)
- Modify: `engine/flow/taste.h` (`kAdventureNarrow`)
- Modify: `tests/test_flow_terrain.cpp`

**Interfaces:**
- Consumes: everything above.
- Produces: `float Terrain::adventure` in `0..1`; every base and curve span draw
  routed through `draw_span(Rng&, const Span&, float adventure)`.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_terrain.cpp`:

```cpp
TEST_CASE("flow terrain: adventure is rare and rerolls with the weather") {
    // a = 1 - u^(1/3), so P(a > x) = (1-x)^3: above 0.5 in 12.5% of draws and
    // above 0.8 in 0.8%. Brave terrain is the rule, outliers the exception.
    int over_half = 0, over_eighty = 0;
    const int n = 20000;
    for (uint32_t master = 1; master <= uint32_t(n); ++master) {
        TerrainState st; st.master = master;
        const Terrain t = generate(st);
        CHECK(t.adventure >= 0.f);
        CHECK(t.adventure <= 1.f);
        if (t.adventure > 0.5f) ++over_half;
        if (t.adventure > 0.8f) ++over_eighty;
    }
    const float p50 = float(over_half) / float(n);
    const float p80 = float(over_eighty) / float(n);
    CAPTURE(p50); CAPTURE(p80);
    CHECK(p50 > 0.105f); CHECK(p50 < 0.145f);      // 0.125 expected
    CHECK(p80 > 0.004f); CHECK(p80 < 0.014f);      // 0.008 expected

    // A partial reroll must redraw it, exactly as the weather does: otherwise
    // a terrain that drew wild stays wild in the very domain the player asked
    // to be redone.
    TerrainState st; st.master = 7;
    const float before = generate(st).adventure;
    st.reroll[M_DENSITY] = 1;
    CHECK(generate(st).adventure != before);
}

TEST_CASE("flow terrain: at full adventure a span is drawn in full") {
    // a = 1 is the no-op: the whole span, which is how the tables read on
    // their own. Anything less narrows toward the middle.
    Rng r; r.seed(99);
    const Span s{ 0.f, 1.f };
    float lo = 1.f, hi = 0.f;
    for (int i = 0; i < 5000; ++i) {
        const float v = draw_span(r, s, 1.f);
        lo = std::min(lo, v); hi = std::max(hi, v);
    }
    CHECK(lo < 0.02f);
    CHECK(hi > 0.98f);

    float lo0 = 1.f, hi0 = 0.f;
    for (int i = 0; i < 5000; ++i) {
        const float v = draw_span(r, s, 0.f);
        lo0 = std::min(lo0, v); hi0 = std::max(hi0, v);
    }
    CHECK(lo0 > 0.29f);        // middle 40% of 0..1 is 0.30..0.70
    CHECK(hi0 < 0.71f);
}
```

`draw_span` must be reachable from the test — declare it in `terrain.h` rather
than leaving it in `terrain.cpp`'s anonymous namespace.

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build && ./build/spky_tests -tc="flow terrain: adventure*" -tc="flow terrain: at full adventure*"
```

Expected: compile error — no `Terrain::adventure`, no `draw_span`.

- [ ] **Step 3: Add the stream, the constant and the draw**

`engine/flow/flow_rng.h`:

```cpp
    kStreamAdventure = 2006,       // per-terrain risk level
```

`engine/flow/taste.h`:

```cpp
// The adventure draw (spec 2026-08-06 §7). Not a control -- a property of the
// DRAW, so NEW occasionally surprises and the panel gains no knob. At a=0 a
// span is sampled only in its middle kAdventureNarrow; at a=1 in full, which
// is the no-op. The (1-x)^3 shape of the draw itself lives in terrain.cpp.
constexpr float kAdventureNarrow = 0.40f;
```

`engine/flow/terrain.h`, in `struct Terrain`:

```cpp
    float adventure;                      // 0..1, this terrain's risk level
```

and the shared helper, declared beside `generate`:

```cpp
// One value inside a span, narrowed toward the middle by the terrain's
// adventure level: full span at adv == 1, the middle kAdventureNarrow at 0.
float draw_span(Rng& r, const Span& s, float adv);
```

`engine/flow/terrain.cpp`:

```cpp
float draw_span(Rng& r, const Span& s, float adv) {
    const float w = kAdventureNarrow + (1.f - kAdventureNarrow) * adv;
    const float c = 0.5f * (s.lo + s.hi);
    const float half = 0.5f * (s.hi - s.lo) * w;
    return (c - half) + r.next_unipolar() * (2.f * half);
}
```

In `generate()`, before stage 3b and stage 3:

```cpp
    // Adventure level. Keyed on reroll_weather_counter() -- the sum of all six
    // macro counters -- exactly as the weather is, so ANY partial reroll
    // redraws it. Without that, a terrain that drew wild would stay wild in
    // the very domain the player just asked to be redone (spec §7).
    // a = 1 - u^(1/3) gives P(a > x) = (1-x)^3: above 0.5 in 12.5% of draws,
    // above 0.8 in 0.8%.
    {
        Rng r = make_stream(st.master, kStreamAdventure,
                            st.reroll_weather_counter());
        t.adventure = 1.f - std::pow(r.next_unipolar(), 1.f / 3.f);
    }
```

- [ ] **Step 4: Route every span draw through it**

Stage 3's base draw:

```cpp
        t.base[br.param] = draw_span(r, s, t.adventure);
```

`draw_curve` takes the adventure level as a parameter and uses it per
breakpoint:

```cpp
Curve draw_curve(Rng& r, const CurveRule& cr, float adv) {
    Curve c;
    c.param = cr.param;
    for (int b = 0; b < 5; ++b) {
        c.bp[b] = draw_span(r, cr.bp[b], adv);
```

Temper the weight tables so an adventurous terrain may draw the unlikely rung.
Add to the anonymous namespace and use it in `snap_rate`/`snap_steps` and the
`P_MODE` pick:

```cpp
// w^(1-a): weights as written at a = 0, uniform at a = 1.
float temper(float w, float adv) { return std::pow(w, 1.f - adv); }
```

The SHUFFLE skew tempers the same way: `std::pow(u, temper(kShuffleSkew, adv))`
— careful, `temper` expects a weight; for the skew use
`std::pow(kShuffleSkew, 1.f - adv)` directly so it reaches 1.0 (uniform) at
full adventure, and say so in a comment.

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS. Task 5's weight-share cases will shift slightly (tempering lets
a few more crooked rungs through); confirm they are still inside their bounds
rather than widening the bounds.

- [ ] **Step 6: Confirm the veto still holds at full adventure**

Append to `tests/test_flow_veto.cpp`:

```cpp
TEST_CASE("flow veto: adventure never reaches past a veto") {
    // The wildest terrain still gets no WOBBLE above 0.25. If this ever goes
    // red, the adventure widening has been applied somewhere it must not be.
    for (uint32_t master = 1; master <= 20000; ++master) {
        TerrainState st; st.master = master;
        const Terrain t = generate(st);
        if (t.adventure < 0.9f) continue;
        for (int v = 0; v < kVetoCount; ++v) {
            CAPTURE(master); CAPTURE(kParams[kVetos[v].param].name);
            CHECK(t.base[kVetos[v].param] >= kVetos[v].lo - 1e-5f);
            CHECK(t.base[kVetos[v].param] <= kVetos[v].hi + 1e-5f);
        }
    }
}
```

Run it. It must pass without any new clamp: `draw_span` only ever narrows
toward the middle of a span that Task 1 already proved is inside the band.

- [ ] **Step 7: Press NEW forty times**

```bash
cd host/vcv && ./build-local.sh
```

The whole point of this plan is that this is now a pleasant thing to do. Count
roughly how many of forty presses are keepers, and write the number in the
commit message — it is the baseline the second spec's tuning log will improve on.

- [ ] **Step 8: Commit**

```bash
git add engine/flow/flow_rng.h engine/flow/terrain.h engine/flow/terrain.cpp engine/flow/taste.h tests/test_flow_terrain.cpp tests/test_flow_veto.cpp
git commit -m "feat(flow): every terrain draws its own nerve, and mostly it has none"
```

---

## Done when

- `ctest --test-dir build --output-on-failure` is green.
- No table span leaves a veto band, proven at build time; no reachable tick
  leaves one, proven through a blend with moving macros.
- A drone draws round LFOs, a sparse DENSITY story and normally no step clock.
- Synced rates land on straight rungs in more than four draws out of five, and
  crooked rungs are still reachable.
- `P(adventure > 0.5)` measures ~12.5%.
- Forty NEW presses were listened to, and the keeper count is recorded.
