# MOD depth split (S&H left / continuous right) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give every MOD-layer depth knob a bipolar meaning — right of noon the
lane's continuous output as today, left of noon the same lane sampled and held
at its own slot boundaries.

**Architecture:** One mechanism in the engine, both paths consume it.
`ModLane` grows a second, held output latched on its own slot changes; that
value travels up through `SuperModulator` → `Part` → `Instrument` exactly
beside the continuous one. The *sign* of a stored depth picks which of the two
a target reads, the *magnitude* scales it — in the engine's `_mod_term` /
`fx_target_value` and, identically, in the host's `mod_layer.hpp`. The host's
depth params widen from `0..1` to `−1..+1` with a dead zone at noon.

**Tech Stack:** C++11/14 engine (`engine/`), doctest (`tests/`), the VCV Rack
host (`host/vcv/`, clang via `build-local.sh`), Python panel generators
(`host/vcv/res/gen_panel.py`, run as plain scripts — pytest is not installed).

**Spec:** [`docs/superpowers/specs/2026-08-22-mod-sh-split-design.md`](../specs/2026-08-22-mod-sh-split-design.md)
(revised 2026-08-23). The parent layer it extends:
[`docs/superpowers/specs/2026-08-22-mod-latch-layer-design.md`](../specs/2026-08-22-mod-latch-layer-design.md).

## Global Constraints

- **Build the engine with clang + Ninja and `-DCMAKE_BUILD_TYPE=Release`.**
  Release is not optional — a Debug configure makes `spky_tests` and
  `ctrl_identity` fail with "SYNTH reference moved". `source env.sh` first.
  Never `source env.sh` in a shell used for `shell/` or `bench/`.
- **Never prefix a shell command with `cd`.** The Bash tool already starts in
  the repo.
- **Everything written into the repo is English** — code, comments, tests,
  commit messages, docs.
- **Commit trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **No bit-exactness gates.** Renders are sanity checks. Use absolute epsilons
  (`docs/engine-map.md` §5). The one exception already in the tree is
  `mod_layer.hpp`'s depth-0 early return, which is honest because no
  arithmetic touches the value — keep it that way.
- **A test that cannot go red gets fixed.** Every new gate in this plan has a
  named RED proof; run it once, watch it fail, revert the sabotage.
- **The probe rule.** No new runtime claim goes into a commit message, a
  comment, or a review reply until a probe printed it. Recipe:
  `docs/engine-map.md` §6. Probes live in the scratchpad, never in the repo.
- **`kShFlowSlots` is 8 and is NOT `_steps`.** On the VCV host the STPS knob
  position that *selects* FLOW is 0, so `_steps` is 1 there and a
  `_steps`-derived S&H grid emits no edges at all (spec §7 probe 1 and 2).
  Do not "simplify" the constant away.
- **VCV builds go through `host/vcv/build-local.sh`, never a hand-rolled
  `g++`** — the system `g++` on this machine is the ARM cross-compiler.
- **Generated files are never hand-edited.** `generated_panel.hpp`,
  `generated_hw_panel.hpp`, `init_patch.hpp` and both `.svg` plates come from
  `res/gen_panel.py` / `res/gen_hw_panel.py`, each run from `host/vcv/`.

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `engine/mod/lane.h` / `.cpp` | `kShFlowSlots`, `_stepped_out`, `stepped_output()`, `_hold_slot()`, `_latch_stepped()` | 1 |
| `tests/test_lane_sh.cpp` (new) | the held twin's own gates | 1 |
| `CMakeLists.txt` | register the new test file | 1 |
| `engine/mod/super_modulator.h` / `.cpp` | `_out_stepped[]`, `lane_output_stepped(i)` | 2 |
| `engine/parts/part.h` | `lane_output_stepped(slot)`, signed depth clamps | 2, 3 |
| `engine/parts/part.cpp` | sign selection in `_mod_term` and `fx_target_value` | 3 |
| `engine/instrument.h` | `lane_output_stepped(p, s)` pass-through | 2 |
| `tests/test_super_modulator.cpp` | the raster gate for the mirrored array | 2 |
| `tests/test_part.cpp` | the signed-depth engine gates | 3 |
| `host/vcv/src/mod_layer.hpp` | `kDepthDead`, `depth_of()`, `depth_knob()`, signed `modded()` | 4 |
| `tests/test_mod_layer.cpp` | dead zone, sign selection, init round trip | 4, 5 |
| `host/vcv/res/gen_panel.py` | bipolar init pre-images, depth tooltips | 5 |
| `host/vcv/src/generated_panel.hpp`, `init_patch.hpp` | regenerated | 5 |
| `host/vcv/src/Fireflow.cpp` | stepped lane frame, `mv()`, engine-backed push, `configParam` range | 6 |
| `docs/engine-map.md`, `docs/gotchas.md`, `docs/roadmap.md`, `docs/release-notes.md` | where the measured facts land | 7 |

---

### Task 1: `ModLane` — the held twin

**Files:**
- Modify: `engine/mod/lane.h` (public constant + accessor near `phase()`; two
  private members and two private methods)
- Modify: `engine/mod/lane.cpp` (`init`, `set_step`, `reset`, and the three
  return sites: `process()` ~`:846`, `follow()` ~`:550`, `tick()` ~`:1096`)
- Create: `tests/test_lane_sh.cpp`
- Modify: `CMakeLists.txt` (add the test file to `spky_tests`, after
  `tests/test_lane.cpp` on line 56)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `static constexpr int spky::ModLane::kShFlowSlots = 8;`
  - `float spky::ModLane::stepped_output() const;` — the finished output held
    between S&H slot boundaries. Same units and range as
    `process()`/`tick()`/`follow()` return, because it *is* one of their past
    return values.

**Background the implementer needs.** `ModLane` has three return sites and one
lane is driven by exactly one of them: `process()` per sample (the PITCH lane),
`tick()` every 96 samples (texture lanes in FLOW), `follow()` every 96 samples
(texture lanes in STEP). All three end in `apply_range(smoothed, _range)`. The
latch goes at all three, on the value each is about to return.

There is already a private `int _sh_slot() const` in this class
(`lane.cpp:619`) and it means something completely different — which `_seq`
slot the SHAPE lookup reads. **Do not touch it and do not name anything near
it.** The new helper is `_hold_slot()`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_lane_sh.cpp`. The gates assert the engine's
`stepped_output()` against a shadow computed from *public accessors only*
(`phase()` for FLOW, `cur_step()` for STEP) — so there are no magic numbers in
the invariant itself, and the counts below are the measured ones from spec §7
probe 2 and the plan's probe 3 (2026-08-23, scratchpad `probe_sh_gates.cpp`).

```cpp
#include <doctest/doctest.h>
#include "mod/lane.h"
#include <cmath>
#include <set>

using namespace spky;

// A FLOW texture lane built the way SuperModulator builds one:
// set_melodic(false) BEFORE init() (docs/engine-map.md §6 -- init() branches
// on _melodic and a reversed order measures a different object), then the
// FLOW push the VCV host actually makes. set_step(false, 0) clamps _steps to
// 1, which is the whole reason kShFlowSlots exists (spec §7 probe 1).
static void make_flow_lane(ModLane& l, float hz, float smooth) {
    l.set_melodic(false);
    l.init(48000.f, 12345);
    l.set_step(false, 0);
    l.set_rate_hz(hz);
    l.set_shape(0.f);
    l.set_smooth(smooth);
    l.set_range(1.f);
    l.set_variation(0.f);
}

TEST_CASE("lane S&H: a FLOW texture lane holds between kShFlowSlots edges") {
    ModLane l;
    make_flow_lane(l, 0.5f, 0.7f);        // 0.5 Hz -> 10 cycles in 20 s

    int prev = -1, edges = 0, violations = 0;
    float shadow = 0.f;
    std::set<float> contDistinct, heldDistinct;
    const int ticks = int(48000.f * 20.f) / ModLane::kTickInterval;
    for (int i = 0; i < ticks; ++i) {
        const float v = l.tick();
        const int slot = ModLane::step_index(l.phase(), ModLane::kShFlowSlots);
        if (slot != prev) { prev = slot; shadow = v; ++edges; }
        // The invariant: the held value is the value latched at the last slot
        // change, and nothing else ever moves it. Counted rather than
        // CHECKed per iteration -- a CHECK inside a 10 000-call loop costs
        // more than the lane does and buries the real failure in noise.
        if (l.stepped_output() != shadow) ++violations;
        contDistinct.insert(v);
        heldDistinct.insert(l.stepped_output());
    }
    CHECK(violations == 0);
    // 10 cycles x 8 slots + the initial latch (measured 2026-08-23)
    CHECK(edges == 81);
    // The point of the feature: the held stream is orders of magnitude
    // coarser than its continuous twin. Measured 32 against 3931; the bound
    // is deliberately loose -- this gate is about the order of magnitude,
    // not about reproducing a float count.
    CHECK(heldDistinct.size() * 20 < contDistinct.size());
}

TEST_CASE("lane S&H: the FLOW grid survives kRateFreeMax") {
    // At 30 Hz a cycle is 1600 samples, i.e. ~16.7 tick() calls, so one of
    // the 8 slots is barely 2 calls wide. This is the case a per-sample
    // probe cannot see: if the 96-sample raster could skip a slot, it would
    // skip it here. Measured 4801 = 600 cycles x 8 + 1 (2026-08-23).
    ModLane l;
    make_flow_lane(l, 30.f, 0.7f);

    int prev = -1, edges = 0, violations = 0;
    float shadow = 0.f;
    const int ticks = int(48000.f * 20.f) / ModLane::kTickInterval;
    for (int i = 0; i < ticks; ++i) {
        const float v = l.tick();
        const int slot = ModLane::step_index(l.phase(), ModLane::kShFlowSlots);
        if (slot != prev) { prev = slot; shadow = v; ++edges; }
        if (l.stepped_output() != shadow) ++violations;
    }
    CHECK(violations == 0);
    CHECK(edges == 4801);
}

TEST_CASE("lane S&H: _steps is NOT the FLOW grid") {
    // The claim the spec's first draft made and probe 1 killed: on the VCV
    // host _steps is 1 in FLOW, so a _steps-derived grid never changes slot
    // and the held output would be a flat line (measured p2p 0.0000 over
    // 20 s). This gate is what stops a future "simplification" back to
    // _steps.
    ModLane l;
    make_flow_lane(l, 0.5f, 0.7f);
    CHECK(l.steps() == 1);                       // the state the host pushes
    float mn = 1e9f, mx = -1e9f;
    const int ticks = int(48000.f * 20.f) / ModLane::kTickInterval;
    for (int i = 0; i < ticks; ++i) {
        l.tick();
        const float h = l.stepped_output();
        if (h < mn) mn = h;
        if (h > mx) mx = h;
    }
    CHECK(mx - mn > 0.5f);       // it moves; a _steps grid gives exactly 0
}

TEST_CASE("lane S&H: in STEP at SMOOTH 0 both halves are the same signal") {
    // The follower is already a staircase, so sampling it changes nothing.
    // Measured max|held - continuous| == 0.000000 over 20 s (2026-08-23).
    // This is the row that tells the owner where the feature is audible and
    // where it is not.
    ModLane l;
    l.set_melodic(false);
    l.init(48000.f, 12345);
    l.set_step(true, 8);
    l.set_rate_hz(0.5f);
    l.set_shape(0.f);
    l.set_smooth(0.f);
    l.set_range(1.f);
    l.set_variation(0.f);

    float worst = 0.f;
    for (int i = 0; i < 48000 * 20; ++i) {
        const float v = l.process();
        worst = std::fmax(worst, std::fabs(l.stepped_output() - v));
    }
    CHECK(worst == 0.f);
}

TEST_CASE("lane S&H: in STEP at SMOOTH 0.7 the held stream collapses") {
    // Same lane, SMOOTH up: continuous visits 149 673 distinct values, held
    // 14 (measured 2026-08-23). The gate is the order of magnitude.
    ModLane l;
    l.set_melodic(false);
    l.init(48000.f, 12345);
    l.set_step(true, 8);
    l.set_rate_hz(0.5f);
    l.set_shape(0.f);
    l.set_smooth(0.7f);
    l.set_range(1.f);
    l.set_variation(0.f);

    int prev = -1, violations = 0;
    float shadow = 0.f;
    std::set<float> contDistinct, heldDistinct;
    for (int i = 0; i < 48000 * 20; ++i) {
        const float v = l.process();
        if (l.cur_step() != prev) { prev = l.cur_step(); shadow = v; }
        if (l.stepped_output() != shadow) ++violations;
        contDistinct.insert(v);
        heldDistinct.insert(l.stepped_output());
    }
    CHECK(violations == 0);
    // measured: 14 held against 149 673 continuous
    CHECK(heldDistinct.size() * 1000 < contDistinct.size());
}
```

Register it in `CMakeLists.txt`, right after line 56:

```cmake
    tests/test_lane.cpp
    tests/test_lane_sh.cpp
    tests/test_lane_tick.cpp
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Expected: **compile error**, `no member named 'stepped_output' in 'spky::ModLane'`
and `no member named 'kShFlowSlots'`. That is the RED for every gate in this
file at once; note it and move on.

- [ ] **Step 3: Add the constant, the state and the accessor to `lane.h`**

Public, next to `phase()` / `cur_step()` (around `lane.h:131-138`):

```cpp
    // S&H slot count for a FLOW texture lane (spec 2026-08-22 mod-sh-split
    // §3). Deliberately NOT _steps: on the VCV host the STPS knob position
    // that SELECTS flow is 0 (Fireflow.cpp, set_step(p, steps > 0, steps)),
    // so every lane carries _steps == 1 in FLOW there and a _steps-derived
    // grid emits no edges at all -- measured p2p 0.0000 over 20 s (spec §7
    // probe 2). 8 is what the FLOW melody lane already runs on
    // (kFlowPhraseSlots), so the two FLOW rasters agree.
    static constexpr int kShFlowSlots = 8;

    // The finished output, held between S&H slot boundaries -- the left half
    // of a bipolar MOD depth knob reads this where the right half reads the
    // return value of process()/tick()/follow(). It IS one of their past
    // return values (post-SHAPE, post-SMOOTH, post-RANGE), never a separately
    // computed signal, which is why its edges are always hard no matter what
    // SMOOTH does.
    float stepped_output() const { return _stepped_out; }
```

Private, beside `_sh_slot()`'s declaration (`lane.h:229`) but clearly apart
from it:

```cpp
    // The S&H slot this lane occupies now. NOT _sh_slot() above -- that one
    // is the SHAPE lookup's _seq slot and has nothing to do with this.
    int   _hold_slot() const;
    void  _latch_stepped(float out);
```

Private state, beside `_target` (`lane.h:348`):

```cpp
    float _stepped_out  = 0.f;   // S&H twin of the return value
    int   _sh_prev_slot = -1;    // -1 arms the first call to latch
```

- [ ] **Step 4: Implement the two helpers in `lane.cpp`**

Put them directly above `float ModLane::process()` (around `lane.cpp:800`):

```cpp
// Which S&H slot this lane occupies right now (spec 2026-08-22 mod-sh-split
// §3). STEP and FLOW melody both already walk a slot raster and keep it in
// _cur_step, so they reuse it -- in STEP that is the lane's OWN slot count,
// which lane_slots() derives from the deck's STEPS (measured 4/16/8/12/6 at
// deck STEPS 8), not the deck count itself. The FLOW texture LFO walks no
// raster at all -- its only edge is the wrap, next_edge is always 1.0 (engine
// map §4) -- so its slot is derived from the phase against kShFlowSlots.
//
// step_index, NOT shuffle_step_index: SHUFFLE is a rhythmic control and stays
// out of FLOW (spec §2), the same call process()'s own flow-melody branch
// makes and for the same reason.
int ModLane::_hold_slot() const {
    if (_step_mode || _flow_melody_on()) return _cur_step;
    return step_index(static_cast<float>(_phase), kShFlowSlots);
}

// Latch on a slot change, with the value THIS call is about to return. The
// ordering is the one probe 2 measured (spec §7): the slot index is read
// AFTER the phase advance, the value comes from the same call. Reading the
// slot before the advance would latch one call late at every edge.
void ModLane::_latch_stepped(float out) {
    const int slot = _hold_slot();
    if (slot != _sh_prev_slot) {
        _sh_prev_slot = slot;
        _stepped_out  = out;
    }
}
```

- [ ] **Step 5: Latch at all three return sites**

In `process()` (currently `lane.cpp:846-850`) replace:

```cpp
    float smoothed = _slew.process(_target);
#ifdef SPKY_TESTING
    _last_out = apply_range(smoothed, _range);
#endif
    return apply_range(smoothed, _range);
```

with:

```cpp
    const float out = apply_range(_slew.process(_target), _range);
    _latch_stepped(out);
#ifdef SPKY_TESTING
    _last_out = out;
#endif
    return out;
```

In `tick()` (currently `lane.cpp:1096-1100`) and in `follow()` (currently
`lane.cpp:550-554`) the same shape, but both use `_slew_tick`:

```cpp
    const float out = apply_range(_slew_tick.process(_target), _range);
    _latch_stepped(out);
#ifdef SPKY_TESTING
    _last_out = out;
#endif
    return out;
```

`apply_range` is pure, so collapsing the two calls into one changes nothing
observable — it only removes the duplicate that existed to feed `_last_out`.

- [ ] **Step 6: Re-arm the latch wherever the slot index changes meaning**

In `ModLane::init()` (`lane.cpp:49`), beside the other field initialisations:

```cpp
    _stepped_out  = 0.f;
    _sh_prev_slot = -1;
```

In `ModLane::set_step()`, inside the existing `if (mode_changed)` branch
(`lane.cpp:211`), append `_sh_prev_slot = -1;` to it — a slot index from the
other mode means nothing in this one, exactly the reason `_cur_step` is reset
on the same line.

In `ModLane::reset()` (`lane.cpp:561`), beside `_cur_step = -1;`, add
`_sh_prev_slot = -1;`.

- [ ] **Step 7: Run the tests to verify they pass**

```bash
source env.sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS, whole suite green. If `edges == 81` or `edges == 4801` is off
by one, the latch is reading the slot on the wrong side of the phase advance —
re-read Step 4's comment, do not adjust the expected count.

- [ ] **Step 8: Prove the gates RED once (house rule)**

Sabotage `_latch_stepped` to latch unconditionally:

```cpp
void ModLane::_latch_stepped(float out) {
    _sh_prev_slot = _hold_slot();
    _stepped_out  = out;          // SABOTAGE: no slot-change guard
}
```

Expected: the two FLOW cases and the STEP SMOOTH-0.7 case fail on
`l.stepped_output() == shadow`, and the distinct-count gates fail. Then
sabotage the FLOW grid instead — `return step_index(..., _steps);` in
`_hold_slot()` — and confirm "`_steps` is NOT the FLOW grid" fails on
`mx - mn > 0.5f`. Revert both, rebuild, confirm green again.

- [ ] **Step 9: Commit**

```bash
git add engine/mod/lane.h engine/mod/lane.cpp tests/test_lane_sh.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(mod): a lane's finished output, sampled and held on its own slots

ModLane gains stepped_output(): the value process()/tick()/follow() last
returned at a slot change, held until the next one. The left half of a
bipolar MOD depth knob will read this where the right half reads the
continuous return value (spec 2026-08-22 mod-sh-split).

FLOW's grid is the fixed kShFlowSlots (8), not _steps. Measured: on the VCV
host the STPS position that SELECTS flow is 0, so _steps is 1 in FLOW there
and a _steps-derived grid holds one value forever (p2p 0.0000 over 20 s).
8 is what the FLOW melody lane already runs on.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 2: The pass-through — `SuperModulator`, `Part`, `Instrument`

**Files:**
- Modify: `engine/mod/super_modulator.h` (mirror array + accessor)
- Modify: `engine/mod/super_modulator.cpp` (`process()`, two lines)
- Modify: `engine/parts/part.h` (`lane_output_stepped`, beside `lane_output`
  at `:262`)
- Modify: `engine/instrument.h` (pass-through beside `lane_output` at `:435`)
- Test: `tests/test_super_modulator.cpp`

**Interfaces:**
- Consumes: `ModLane::stepped_output()`, `ModLane::kShFlowSlots` (Task 1).
- Produces:
  - `float spky::SuperModulator::lane_output_stepped(int i) const;`
  - `float spky::Part::lane_output_stepped(int slot) const;`
  - `float spky::Instrument::lane_output_stepped(int p, int s) const;`

**Why a mirror array and not a live read.** `_out[]` exists because the four
texture lanes only advance every 96 samples; everything downstream must see one
frozen frame per raster, not a value that changes under it. The held twin has
exactly the same requirement, so it gets exactly the same treatment — updated
on the same two sites, never read straight off the lane.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_super_modulator.cpp`:

```cpp
TEST_CASE("super: the stepped mirror updates on the same raster as _out") {
    // Both arrays are written at the same two sites -- LANE_PITCH per sample,
    // the four texture lanes every kTickInterval. The gate: between two tick
    // boundaries the texture entries do not move, and across one they may.
    spky::SuperModulator m;
    m.init(48000.f, 12345);
    m.set_step(false, 0);          // FLOW, exactly as the VCV host pushes it
    m.set_rate(0.5f);
    m.set_smooth(0.7f);

    // Run past the cold start so the lanes are actually moving.
    for (int i = 0; i < 48000; ++i) m.process();

    bool sawAChange = false;
    int  violations = 0;
    for (int frame = 0; frame < 400; ++frame) {
        m.process();                                   // this call may tick
        float held[spky::LANE_COUNT];
        for (int i = 0; i < spky::LANE_COUNT; ++i)
            held[i] = m.lane_output_stepped(i);
        for (int k = 1; k < spky::ModLane::kTickInterval; ++k) {
            m.process();                               // these cannot tick
            for (int i = 0; i < spky::LANE_COUNT; ++i) {
                if (i == spky::LANE_PITCH) continue;   // per-sample path
                if (m.lane_output_stepped(i) != held[i]) ++violations;
            }
        }
        for (int i = 0; i < spky::LANE_COUNT; ++i)
            if (i != spky::LANE_PITCH && m.lane_output_stepped(i) != held[i])
                sawAChange = true;
    }
    CHECK(violations == 0);
    CHECK(sawAChange);   // a mirror that never moves would pass the rest
}
```

- [ ] **Step 2: Run it to verify it fails**

```bash
source env.sh && cmake --build build && ./build/spky_tests -tc="super: the stepped mirror*"
```

Expected: compile error, `no member named 'lane_output_stepped' in 'spky::SuperModulator'`.

- [ ] **Step 3: Add the mirror to `SuperModulator`**

In `super_modulator.h`, beside `_out` (`:222`):

```cpp
    std::array<float, LANE_COUNT>   _out_stepped {};
```

and beside `lane_output` (`:109`):

```cpp
    // The S&H twin of lane_output, on the same frozen frame. Reading
    // _lanes[i].stepped_output() directly would bypass the 96-sample raster
    // _out exists to enforce -- see the comment on process().
    float lane_output_stepped(int i) const { return _out_stepped[i]; }
```

In `super_modulator.cpp::process()`, beside the two existing writers.
After `_out[LANE_PITCH] = _lanes[LANE_PITCH].process();` (`:109`):

```cpp
    _out_stepped[LANE_PITCH] = _lanes[LANE_PITCH].stepped_output();
```

and inside the tick loop, after the `_out[i] = ...` assignment (`:167-168`):

```cpp
            _out_stepped[i] = _lanes[i].stepped_output();
```

- [ ] **Step 4: Add the two pass-throughs**

`engine/parts/part.h`, directly under `lane_output` (`:262`):

```cpp
    float lane_output_stepped(int slot) const {
        return _mod.lane_output_stepped(slot);
    }
```

`engine/instrument.h`, directly under `lane_output` (`:435`):

```cpp
    float lane_output_stepped(int p, int s) const {
        return _parts[p].lane_output_stepped(s);
    }
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS, whole suite green.

- [ ] **Step 6: Prove the gate RED once**

Change the tick-loop line to read the lane live on every sample — move
`_out_stepped[i] = _lanes[i].stepped_output();` out of the
`if (_tick_ctr == 0)` block into the bottom of `process()`, looping all lanes.
Expected: the "cannot tick" inner `CHECK` fails, because a FLOW texture lane's
held value now changes on samples where `_out` did not. Revert, rebuild, green.

- [ ] **Step 7: Commit**

```bash
git add engine/mod/super_modulator.h engine/mod/super_modulator.cpp \
        engine/parts/part.h engine/instrument.h tests/test_super_modulator.cpp
git commit -m "$(cat <<'EOF'
feat(mod): carry the held lane output up beside the continuous one

_out_stepped[] mirrors _out[] on the same 96-sample raster, and Part and
Instrument pass it through beside lane_output. No consumer yet -- the sign
of a depth picks between the two in the next commit.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 3: Signed depth in `Part`

**Files:**
- Modify: `engine/parts/part.h:120` (`set_target_depth` clamp),
  `:124` (`set_fx_target_depth` clamp)
- Modify: `engine/parts/part.cpp:120-125` (`_mod_term`),
  `:170-174` (`fx_target_value`)
- Test: `tests/test_part.cpp`

**Interfaces:**
- Consumes: `Part::lane_output_stepped(slot)` (Task 2).
- Produces: `set_target_depth(slot, d)` and `set_fx_target_depth(slot, d)` now
  accept `d` in `−1..+1`. Sign selects the lane reading, magnitude scales.
  `target_raw()`, `fx_target_value()` and `lane_excursion()` keep their
  signatures and their meaning.

**The rule, stated once so it cannot drift:** a negative depth does **not**
invert the modulation. It selects the held lane reading and scales by
`|depth|`. The host repeats exactly this rule in Task 4; if the two ever
disagree, the LEDs (which read `_mod_term` through `lane_excursion`) will
report one thing while a host-computed neighbour does another.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_part.cpp` (the file's existing idiom: build a `Part`,
`init`, push through `mod()`, run `process()` in a loop and watch a target):

```cpp
TEST_CASE("part: a negative fx depth steps where the positive twin glides") {
    // Same magnitude, same lane, same base -- only the sign differs. At
    // SMOOTH 0.7 the continuous reading visits a large number of distinct
    // values and the held one a handful (engine-side echo of the lane gates
    // in tests/test_lane_sh.cpp).
    auto run = [](float depth) {
        Part p;
        p.init(48000.f, 5);
        p.set_step(false, 0);                 // FLOW, as the VCV host pushes
        p.set_fx_target_active(FXT_FLUX_TIME, true);
        p.set_fx_target_base(FXT_FLUX_TIME, 0.5f);
        p.set_fx_target_depth(FXT_FLUX_TIME, depth);
        p.set_depth(1.f);
        p.mod().set_range(1.f);
        p.mod().set_rate(0.6f);
        p.mod().set_smooth(0.7f);
        std::set<float> distinct;
        float l, r;
        for (int i = 0; i < 48000 * 4; ++i) {
            p.process(l, r);
            distinct.insert(p.fx_target_value(FXT_FLUX_TIME));
        }
        return distinct.size();
    };
    const size_t glides = run(+0.8f);
    const size_t steps  = run(-0.8f);
    CHECK(steps * 20 < glides);
    CHECK(steps > 1);          // it still moves; a pinned target is not S&H
}

TEST_CASE("part: depth sign selects the reading, it does not invert it") {
    // The load-bearing rule (spec §4). In STEP at SMOOTH 0 the two lane
    // readings are the same signal (measured max|difference| == 0.000000,
    // tests/test_lane_sh.cpp), so at equal magnitude the two signs must
    // produce the SAME target -- not mirrored ones.
    auto run = [](float depth) {
        Part p;
        p.init(48000.f, 5);
        p.set_step(true, 8);
        p.set_fx_target_active(FXT_FLUX_TIME, true);
        p.set_fx_target_base(FXT_FLUX_TIME, 0.5f);
        p.set_fx_target_depth(FXT_FLUX_TIME, depth);
        p.set_depth(1.f);
        p.mod().set_range(1.f);
        p.mod().set_rate(0.6f);
        p.mod().set_smooth(0.f);
        float l, r, last = 0.f;
        for (int i = 0; i < 48000 * 4; ++i) {
            p.process(l, r);
            last = p.fx_target_value(FXT_FLUX_TIME);
        }
        return last;
    };
    const float pos = run(+0.8f);
    const float neg = run(-0.8f);
    CHECK(neg == doctest::Approx(pos).epsilon(1e-6));
    CHECK(pos != doctest::Approx(0.5f));   // and it actually moved off base
}

TEST_CASE("part: an over-range negative depth clamps to -1, not to 0") {
    // Today's clamp is 0..1, so -3 lands on 0 and the target stops moving
    // altogether. Both halves of this gate matter: it must MOVE (the old
    // clamp pins it) and it must stay INSIDE the lane's own +-1 (a widened
    // clamp that forgot its floor would not).
    Part p;
    p.init(48000.f, 5);
    p.set_target_base(LANE_SIZE, 0.5f);
    p.set_target_active(LANE_SIZE, true);
    p.set_target_depth(LANE_SIZE, -3.f);
    p.set_depth(1.f);
    p.mod().set_range(1.f);
    p.mod().set_rate(0.6f);
    float l, r, worst = 0.f;
    for (int i = 0; i < 48000; ++i) {
        p.process(l, r);
        worst = std::fmax(worst, std::fabs(p.lane_excursion(LANE_SIZE)));
    }
    CHECK(worst > 0.f);        // -3 must not have been clamped to 0
    CHECK(worst <= 1.f);       // and not to -3 either
}
```

Add `#include <set>` and `#include <cmath>` at the top of the file if they are
not already there.

- [ ] **Step 2: Run them to verify they fail**

```bash
source env.sh && cmake --build build && ./build/spky_tests -tc="part: a negative fx depth*,part: depth sign selects*"
```

Expected: all three fail. The first two because `set_fx_target_depth(-0.8f)`
clamps to `0.f` today — `steps` is the un-modulated single value so `steps > 1`
fails, and `neg` sits at the base while `pos` moved. The third because `-3.f`
clamps to `0.f` for the same reason, so `worst > 0.f` fails.

- [ ] **Step 3: Widen the two clamps**

`engine/parts/part.h:120` and `:124`:

```cpp
    // Bipolar since spec 2026-08-22 mod-sh-split: the SIGN picks which lane
    // reading the target follows (negative = the S&H twin), the MAGNITUDE
    // scales it. A negative depth does NOT invert the modulation -- see
    // _mod_term in part.cpp, and mod_layer.hpp for the host's copy of the
    // same rule.
    void set_target_depth(int slot, float d)  { _tdepth[slot] = clampf(d, -1.f, 1.f); }
```

```cpp
    void set_fx_target_depth(int slot, float d)  { _fx_depth[slot] = clampf(d, -1.f, 1.f); }
```

- [ ] **Step 4: Select by sign in the two combine points**

`engine/parts/part.cpp`, `_mod_term` (`:120-125`) becomes:

```cpp
float Part::_mod_term(int slot) const {
    float d = (slot == LANE_PITCH) ? 1.f : _depth;
    if (slot == LANE_SOURCE && _engine_id == ENGINE_SAMPLER)
        d = std::pow(d, sampler_cfg::kSourceModExp);
    // Sign picks the reading, magnitude scales (spec 2026-08-22 mod-sh-split
    // §4). Everything else in this expression is untouched -- master MOD, the
    // sampler SOURCE exponent above, and the operand order, so a positive
    // depth still produces exactly the float it produced before.
    const float td   = _tdepth[slot];
    const float lane = td < 0.f ? _mod.lane_output_stepped(slot)
                                : _mod.lane_output(slot);
    return _active[slot] ? lane * d * std::fabs(td) : 0.f;
}
```

`fx_target_value` (`:170-174`) becomes:

```cpp
float Part::fx_target_value(int slot) const {
    const float fd   = _fx_depth[slot];
    const float lane = fd < 0.f ? _mod.lane_output_stepped(slot)
                                : _mod.lane_output(slot);
    float mod = _fx_active[slot] ? lane * _depth * std::fabs(fd) : 0.f;
    return clampf(_fx_base[slot] + mod, 0.f, 1.f);
}
```

`<cmath>` is already included in `part.cpp` (`std::pow` above).

- [ ] **Step 5: Run the tests to verify they pass**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS, whole suite green — including the render-hash gates, because
every depth in the tree is still positive and the positive path's arithmetic
did not move.

- [ ] **Step 6: Prove the gates RED once**

Change `lane * d * std::fabs(td)` to `lane * d * td` (i.e. let the sign
invert). Expected: "depth sign selects the reading, it does not invert it"
fails — `neg` comes out mirrored around the base instead of equal to `pos`.
Then narrow `set_target_depth`'s clamp back to `clampf(d, 0.f, 1.f)` and
confirm "an over-range negative depth clamps to -1, not to 0" fails on
`worst > 0.f`. Revert both, rebuild, green.

- [ ] **Step 7: Commit**

```bash
git add engine/parts/part.h engine/parts/part.cpp tests/test_part.cpp
git commit -m "$(cat <<'EOF'
feat(part): depth is bipolar -- sign picks the lane reading

_tdepth and _fx_depth clamp to -1..1. A negative depth reads the lane's S&H
twin and scales by |depth|; it does not invert the modulation. The positive
path's arithmetic is unchanged, operand order included, so nothing in the
tree moves.

The LED law needs no change: it reads _mod_term through lane_excursion, so
it reports the S&H variant for free.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 4: `mod_layer.hpp` — the dead zone and the signed push

**Files:**
- Modify: `host/vcv/src/mod_layer.hpp`
- Test: `tests/test_mod_layer.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks (this header is Rack-free math).
- Produces:
  - `constexpr float spkymod::kDepthDead = 0.04f;`
  - `float spkymod::depth_of(float raw);` — knob position → depth, with noon
    dead-zoned to exactly `0.f` and the remainder rescaled so both stops still
    reach `±1`.
  - `float spkymod::depth_knob(float d);` — the inverse.
  - `float spkymod::modded(float knob, float depth, float contTerm,
    float stepTerm, float lo, float hi);` — **six** parameters now; the old
    five-parameter form is gone.
  - `lane_term` and `center_term` are unchanged.

**Why the dead zone rescales.** GRIT already does exactly this
(`Fireflow.cpp`, `kGritDead`) and for the same reason: a 9 mm pot on an ADC
cannot hit an exact zero, so "standstill" needs a zone, and the remainder is
rescaled so the stops still mean full depth. The cost is that a *booted* engine
depth is no longer its own knob position — 0.7 sits at knob 0.712. `depth_knob`
is that inverse, and Task 5 makes the generator use it so there is one source
for the arithmetic. Measured round trip (2026-08-23, scratchpad
`probe_dead.cpp`): 1.0 → 1.0 exactly, 0.7 → 0.712 → 0.699999988,
0.55 → 0.568 → 0.550000012.

- [ ] **Step 1: Write the failing tests**

In `tests/test_mod_layer.cpp`, replace the existing "depth 0 is the identity"
case (its `modded` calls take the old five-parameter form) and append the rest:

```cpp
TEST_CASE("mod layer: depth 0 is the identity") {
    for (float knob : {0.f, 0.1337f, 0.5f, 0.99f, 1.f, -0.73f}) {
        CHECK(spkymod::modded(knob, 0.f, 0.83f, -0.4f, -1.f, 1.f) == knob);
        CHECK(spkymod::modded(knob, 0.f, -1.f, 1.f, -1.f, 1.f) == knob);
    }
}

TEST_CASE("mod layer: the dead zone makes noon exact and keeps both stops") {
    CHECK(spkymod::depth_of(0.f) == 0.f);
    CHECK(spkymod::depth_of(spkymod::kDepthDead) == 0.f);
    CHECK(spkymod::depth_of(-spkymod::kDepthDead) == 0.f);
    CHECK(spkymod::depth_of(0.02f) == 0.f);
    CHECK(spkymod::depth_of(-0.02f) == 0.f);
    CHECK(spkymod::depth_of(1.f) == doctest::Approx(1.f));
    CHECK(spkymod::depth_of(-1.f) == doctest::Approx(-1.f));
    // just outside the zone is small but not zero -- the zone is a detent,
    // not a chunk out of the axis
    CHECK(spkymod::depth_of(0.0401f) > 0.f);
    CHECK(spkymod::depth_of(0.0401f) < 0.01f);
}

TEST_CASE("mod layer: depth_knob is depth_of's inverse") {
    // This is what lets an engine-backed init depth survive the rescale, so
    // init still sounds exactly like today (spec §2).
    for (float d : {1.f, 0.7f, 0.55f, 0.f, -0.7f, -1.f})
        CHECK(spkymod::depth_of(spkymod::depth_knob(d)) == doctest::Approx(d));
    CHECK(spkymod::depth_knob(0.7f) == doctest::Approx(0.712f));
    CHECK(spkymod::depth_knob(0.55f) == doctest::Approx(0.568f));
    CHECK(spkymod::depth_knob(1.f) == doctest::Approx(1.f));
}

TEST_CASE("mod layer: the sign picks the term, the magnitude scales it") {
    const float cont = 0.5f, step = -0.25f;
    // right of noon: the continuous term, exactly as before the split
    CHECK(spkymod::modded(0.5f, 0.5f, cont, step, 0.f, 1.f)
          == doctest::Approx(0.75f));
    // left of noon: the stepped term, NOT inverted -- 0.5 + 0.5*(-0.25)
    CHECK(spkymod::modded(0.5f, -0.5f, cont, step, 0.f, 1.f)
          == doctest::Approx(0.375f));
    // equal magnitudes, equal terms -> equal results
    CHECK(spkymod::modded(0.5f, -0.5f, cont, cont, 0.f, 1.f)
          == doctest::Approx(spkymod::modded(0.5f, 0.5f, cont, cont, 0.f, 1.f)));
    // clamping still happens in knob space
    CHECK(spkymod::modded(0.9f, -1.f, 0.f, 1.f, 0.f, 1.f) == 1.f);
    CHECK(spkymod::modded(-0.9f, -1.f, 0.f, -1.f, -1.f, 1.f) == -1.f);
}
```

- [ ] **Step 2: Run them to verify they fail**

```bash
source env.sh && cmake --build build && ./build/spky_tests -tc="mod layer:*"
```

Expected: compile error — `no member named 'depth_of' in namespace 'spkymod'`
and too many arguments to `modded`.

- [ ] **Step 3: Implement**

Replace `modded` in `host/vcv/src/mod_layer.hpp` and add the three new
entities above it:

```cpp
// The depth knobs are bipolar around noon since spec 2026-08-22
// mod-sh-split: right of noon the lane's continuous output, left of noon its
// S&H twin, noon a standstill. Noon needs a dead zone for the same reason
// GRIT's kGritDead exists (Fireflow.cpp): a 9 mm pot on an ADC cannot hit an
// exact zero, so without a zone "off" would be unreachable on hardware. The
// remainder is rescaled so both stops still reach a full +-1.
constexpr float kDepthDead = 0.04f;

// Knob position -> depth. Inside the zone the answer is exactly 0.f, which
// is what modded()'s identity early return leans on.
inline float depth_of(float raw) {
    const float m = raw < 0.f ? -raw : raw;
    if (m <= kDepthDead) return 0.f;
    const float d = (m - kDepthDead) / (1.f - kDepthDead);
    return raw < 0.f ? -d : d;
}

// The inverse: which knob position yields depth `d`. Exists because the
// engine-backed faces boot with a depth (1.0 / 0.7 / 0.55) and their init
// knob positions have to be the PRE-IMAGES of those, or the dead zone would
// quietly shave every one of them and init would stop sounding like today.
// gen_panel.py computes INIT_DEFAULTS with the same arithmetic; this is the
// C++ side of that one source, and tests/test_mod_layer.cpp pins the round
// trip.
inline float depth_knob(float d) {
    if (d == 0.f) return 0.f;
    const float m   = d < 0.f ? -d : d;
    const float raw = kDepthDead + m * (1.f - kDepthDead);
    return d < 0.f ? -raw : raw;
}

// pushed value = clamp(knob + |depth| * term) in KNOB space, before the
// parameter's own engine mapping, where `term` is the continuous lane term
// right of noon and the S&H one left of it.
//
// A negative depth does NOT invert the modulation -- it selects the other
// reading and scales by the magnitude. This is the same rule Part::_mod_term
// applies engine-side (part.cpp); the two must not drift, or the LEDs (which
// read _mod_term) would report one thing while a host-computed neighbour did
// another.
//
// Depth 0 returns the knob untouched -- bit-exact by early return, which is
// what the identity gate leans on. `depth` here is already through
// depth_of(), so inside the dead zone it is exactly 0.f.
inline float modded(float knob, float depth, float contTerm, float stepTerm,
                    float lo, float hi) {
    if (depth == 0.f) return knob;
    const float term = depth < 0.f ? stepTerm : contTerm;
    const float mag  = depth < 0.f ? -depth : depth;
    return clampf(knob + mag * term, lo, hi);
}
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS. `test_mod_layer.cpp`'s "init defaults keep today's sound" case
still asserts the OLD raw values (1.0 / 0.7 / 0.55) and will still pass here —
Task 5 is what changes those defaults and that assertion.

- [ ] **Step 5: Prove the gates RED once**

Change `mag` back to `depth` (letting the sign invert). Expected: "the sign
picks the term, the magnitude scales it" fails on the equal-terms equality.
Then change `if (m <= kDepthDead)` to `if (m < 0.f)` (no dead zone).
Expected: "the dead zone makes noon exact" fails on `depth_of(0.02f) == 0.f`.
Revert both, rebuild, green.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/src/mod_layer.hpp tests/test_mod_layer.cpp
git commit -m "$(cat <<'EOF'
feat(vcv): signed depth and a dead zone at noon in the mod layer

modded() takes both lane terms and picks by sign; depth_of() dead-zones noon
to an exact 0 and rescales the remainder so both stops still reach +-1, the
same shape GRIT's dead zone already has. depth_knob() is the inverse, so an
engine-backed init depth can be stored as its pre-image and survive the
rescale unchanged.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 5: The generator — bipolar init pre-images and the new tooltips

**Files:**
- Modify: `host/vcv/res/gen_panel.py` (the `INIT_DEFAULTS` block at `:861-869`
  and the `kModLayer` emitter at `:1157-1165`)
- Regenerate: `host/vcv/src/generated_panel.hpp`,
  `host/vcv/src/generated_hw_panel.hpp`, `host/vcv/src/init_patch.hpp`,
  `host/vcv/res/Fireflow.svg`, `host/vcv/res/FireflowHW.svg`
- Test: `tests/test_mod_layer.cpp` (the init-defaults case),
  `host/vcv/res/test_panel.py`, `host/vcv/res/test_hw_panel.py`

**Interfaces:**
- Consumes: `spkymod::depth_of` (Task 4).
- Produces: `initParamDefault(MODD_SOURCE_A) == 1.0f`,
  `initParamDefault(MODD_DEPTH_A) == 0.712f`,
  `initParamDefault(MODD_FILT_A) == 0.568f`, every other depth `0.0f`;
  `kModLayer[i].name` gains the half-labels.

- [ ] **Step 1: Write the failing test**

Replace the init-defaults case in `tests/test_mod_layer.cpp` with one that
asserts what the ENGINE receives, not what the knob reads — the assertion that
actually means "init sounds like today":

```cpp
TEST_CASE("mod layer: init defaults keep today's sound through the dead zone") {
    CHECK(initParamDefault(MODBTN) == 0.f);
    // The knob positions are the PRE-IMAGES: 0.7 depth sits at knob 0.712,
    // because depth_of() rescales the axis around the dead zone. What has to
    // stay at the booted engine value is the DEPTH, not the knob.
    CHECK(spkymod::depth_of(initParamDefault(MODD_SOURCE_A))
          == doctest::Approx(1.0f));
    CHECK(spkymod::depth_of(initParamDefault(MODD_DEPTH_A))
          == doctest::Approx(0.7f));
    CHECK(spkymod::depth_of(initParamDefault(MODD_FILT_A))
          == doctest::Approx(0.55f));
    CHECK(spkymod::depth_of(initParamDefault(MODD_SOURCE_B))
          == doctest::Approx(1.0f));
    CHECK(spkymod::depth_of(initParamDefault(MODD_DEPTH_B))
          == doctest::Approx(0.7f));
    CHECK(spkymod::depth_of(initParamDefault(MODD_FILT_B))
          == doctest::Approx(0.55f));
    // every host-computed depth and every FX depth still boots at standstill
    for (const auto& t : kModLayer)
        if (t.kind != MODK_TDEPTH)
            CHECK(initParamDefault(t.depthId) == 0.f);
}
```

Add `#include "vcv/src/mod_layer.hpp"` — it is already the file's first
include, so nothing to do.

- [ ] **Step 2: Run it to verify it fails**

```bash
source env.sh && cmake --build build && ./build/spky_tests -tc="mod layer: init defaults*"
```

Expected: FAIL — `depth_of(1.0f)` is 1.0 so the SOURCE lines pass, but
`depth_of(0.7f)` is `0.6875`, not `0.7`, so the DEPTH and FILT lines fail. That
failure IS the bug this task fixes: without the pre-image, the dead zone shaves
every booted depth.

- [ ] **Step 3: Compute the pre-images in the generator**

In `host/vcv/res/gen_panel.py`, replace the `INIT_DEFAULTS` block at `:861-869`:

```python
# MOD latch layer defaults (spec §3a, revised by 2026-08-22 mod-sh-split §5):
# engine-backed depths carry the booted values, host-computed depths and the
# latch itself start at noon.
#
# The stored numbers are KNOB positions, and since the depth axis became
# bipolar the knob is not the depth: mod_layer.hpp's depth_of() dead-zones
# noon and rescales the remainder, so a booted depth of 0.7 has to be stored
# as 0.712 to arrive at the engine as 0.7. _depth_knob is that inverse, and
# it is the same arithmetic as spkymod::depth_knob -- tests/test_mod_layer.cpp
# pins the round trip through both.
MOD_DEPTH_DEAD = 0.04


def _depth_knob(d):
    if d == 0.0:
        return 0.0
    raw = MOD_DEPTH_DEAD + abs(d) * (1.0 - MOD_DEPTH_DEAD)
    return -raw if d < 0.0 else raw


INIT_DEFAULTS["MODBTN"] = 0.0
for _base, _kind, _slot, _init in MOD_DECK_TARGETS:
    INIT_DEFAULTS[f"MODD_{_base}_A"] = _depth_knob(_init)
    INIT_DEFAULTS[f"MODD_{_base}_B"] = _depth_knob(_init)
for _base, _kind, _slot, _init in MOD_CENTER_TARGETS:
    INIT_DEFAULTS[f"MODD_{_base}"] = _depth_knob(_init)
```

- [ ] **Step 4: Say what the two halves mean, in the tooltip**

In the `kModLayer` emitter (`:1158-1164`), extend the two name strings.
ASCII only — this string ends up in a generated C++ header that Rack reads,
and every other string in that file is ASCII:

```python
    for base, kind, slot, _init in MOD_DECK_TARGETS:
        for pi, sfx in enumerate(("_A", "_B")):
            L2.append(f'    {{{base}{sfx}, MODD_{base}{sfx}, {KINDMAP[kind]}, '
                      f'{slot}, {pi}, "{base} {"AB"[pi]} mod depth '
                      f'(left of noon: S&H, right: continuous)"}},')
    for base, kind, slot, _init in MOD_CENTER_TARGETS:
        L2.append(f'    {{{base}, MODD_{base}, {KINDMAP[kind]}, {slot}, 2, '
                  f'"{base} mod depth (left of noon: S&H, right: '
                  f'continuous)"}},')
```

- [ ] **Step 5: Regenerate and run the panel guards**

Both generators, both run from `host/vcv/` (pytest is not installed on this
machine — the guards are plain scripts):

```bash
python host/vcv/res/gen_panel.py
python host/vcv/res/gen_hw_panel.py
python host/vcv/res/test_panel.py
python host/vcv/res/test_hw_panel.py
```

Expected: both generators write their files, both guards print their pass
lines, and **no guard needs editing**. No geometry moved, so
`test_hw_panel.py`'s wreath and clearance checks are untouched;
`test_panel.py`'s `TIP_ORDER` is the `Ctl` tips (still 48 empty strings) and
not the `kModLayer` names, so it does not move either. If `test_panel.py`
fails on the tips, the change went into the wrong table — the depth `Ctl`s at
`gen_panel.py:692`/`:694` must keep their empty labels.

Spec §6 anticipated "the param range table and the depth-param guard follow
the bipolar range" — there is no such table. A depth param's range is set
once, in `Fireflow.cpp`'s `configParam` call (Task 6 Step 4), and no
generator or guard carries a copy of it. Nothing to do here; do not invent a
table to satisfy the sentence.

- [ ] **Step 6: Run the C++ tests to verify they pass**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS, whole suite green, `NUM_PARAMS` still 121 (no params were
added or removed, so `tests/test_seed_audition_init.cpp` needs no change).

- [ ] **Step 7: Prove the gate RED once**

Change `_depth_knob` to `return d` (the pre-layer behaviour). Expected: "init
defaults keep today's sound through the dead zone" fails on the DEPTH and FILT
lines. Revert, regenerate, rebuild, green.

- [ ] **Step 8: Commit**

```bash
git add host/vcv/res/gen_panel.py host/vcv/src/generated_panel.hpp \
        host/vcv/src/generated_hw_panel.hpp host/vcv/src/init_patch.hpp \
        host/vcv/res/Fireflow.svg host/vcv/res/FireflowHW.svg \
        tests/test_mod_layer.cpp
git commit -m "$(cat <<'EOF'
fix(vcv): store the booted depths as their pre-images under the dead zone

The depth axis is bipolar now, and depth_of() rescales it around noon's dead
zone -- so a stored 0.7 would arrive at the engine as 0.6875 and init would
stop sounding like today. gen_panel.py stores the inverse instead (0.712,
0.568), and the gate asserts the DEPTH the engine receives rather than the
knob position.

Tooltips say which half is which.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 6: The host wiring

**Files:**
- Modify: `host/vcv/src/Fireflow.cpp` — `:379-380` (the frame), `:600`
  (`configParam` range), `:697-712` (`mv`), `:734-738` (frame sampling),
  `:1134-1143` (the engine-backed push loop)

**Interfaces:**
- Consumes: `Instrument::lane_output_stepped(p, s)` (Task 2),
  `Part::set_target_depth` / `set_fx_target_depth` accepting `−1..1` (Task 3),
  `spkymod::depth_of` and the six-parameter `spkymod::modded` (Task 4), the
  regenerated defaults (Task 5).
- Produces: the finished feature. No new symbols for later tasks.

**This is the task where a wrong `depth_of` call site silently costs the dead
zone.** There are exactly three reads of a depth param's raw value in
`Fireflow.cpp` after this task — `mv()` and the two branches of the
engine-backed loop — and all three must go through `depth_of`. A raw read
compiles fine and just means that knob has no detent.

- [ ] **Step 1: Add the stepped lane frame**

Beside `laneOut` (`:379`):

```cpp
    float laneOutStepped[spky::PART_COUNT][spky::LANE_COUNT] = {};
```

and in `pushParams`'s frame sampling (`:736-737`):

```cpp
            for (int s = 0; s < spky::LANE_COUNT; ++s) {
                laneOut[p][s]        = inst.lane_output(p, s);
                laneOutStepped[p][s] = inst.lane_output_stepped(p, s);
            }
```

The frozen-frame reason in the comment above that loop applies to both arrays
unchanged: one frame per control tick so deck A's first knob and deck B's last
knob see the same lane positions.

- [ ] **Step 2: Pass both terms through `mv()`**

Replace the body of `mv` (`:697-712`) from the `const float term` line down:

```cpp
        // t.part == 2 marks a center-column target: both decks mixed, so both
        // masters down means the center is still. Both readings are built the
        // same way -- the sum of two staircases is itself a staircase (spec
        // 2026-08-22 mod-sh-split §5), so the center needs no extra clock.
        const float term = (t.part == 2)
            ? spkymod::center_term(modMaster[0], laneOut[0][t.slot],
                                   modMaster[1], laneOut[1][t.slot])
            : spkymod::lane_term(modMaster[t.part], laneOut[t.part][t.slot]);
        const float stepTerm = (t.part == 2)
            ? spkymod::center_term(modMaster[0], laneOutStepped[0][t.slot],
                                   modMaster[1], laneOutStepped[1][t.slot])
            : spkymod::lane_term(modMaster[t.part],
                                 laneOutStepped[t.part][t.slot]);
        ParamQuantity* q = paramQuantities[soundId];
        return spkymod::modded(v, spkymod::depth_of(params[t.depthId].getValue()),
                               term, stepTerm,
                               q->getMinValue(), q->getMaxValue());
```

- [ ] **Step 3: Push signed depths to the engine-backed faces**

Replace the loop at `:1134-1143`:

```cpp
        for (const auto& t : kModLayer) {
            // Through depth_of, not raw: noon needs its dead zone here too,
            // and a negative depth is what tells Part to read the lane's S&H
            // twin (spec 2026-08-22 mod-sh-split §4).
            const float d = spkymod::depth_of(params[t.depthId].getValue());
            if (t.kind == MODK_TDEPTH) {
                inst.set_target_depth(t.part, t.slot, d);
            } else if (t.kind == MODK_FXDEPTH) {
                inst.set_fx_target_depth(t.part, t.slot, d);
                // Active on EITHER side of noon now -- the old `d > 0.f`
                // would have left every S&H FX target pinned to its base.
                inst.set_fx_target_active(t.part, t.slot, d != 0.f);
            }
        }
```

Extend the comment block above the loop: the boot values it names are now the
pre-images (1.0 / 0.712 / 0.568 and three zeroes), and the depths they resolve
to are 1.0 / 0.7 / 0.55.

- [ ] **Step 4: Widen the param range**

At `:599-600`:

```cpp
        // Every depth is bipolar -1..+1 whatever its sound twin's range is:
        // it scales a swing and picks which reading of the lane that swing
        // follows (left = S&H), it is not a second copy of the knob. Noon is
        // standstill, dead-zoned in mod_layer.hpp so it is reachable on a pot.
        for (const auto& t : kModLayer)
            configParam(t.depthId, -1.f, 1.f, initParamDefault(t.depthId), t.name);
```

- [ ] **Step 5: Build the plugin**

```bash
host/vcv/build-local.sh
```

Expected: a clean build. Never invoke `g++` directly here — the system `g++` on
this machine is the ARM cross-compiler and fails with "MinGW not found".

- [ ] **Step 6: Run the whole engine suite once more**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS. `Fireflow.cpp` is not in `spky_tests`, so this is a regression
check on everything the previous tasks touched, not a gate on this one.

- [ ] **Step 7: Check it by hand in Rack**

Load a `FireflowHW` in Rack, latch MOD, and turn one depth knob left of noon on
a face whose lane is clearly audible — SMTH or RES on a FLOW deck with SMOOTH
up is the loudest case (spec §7: the two halves diverge most in FLOW at high
SMOOTH, and are identical in STEP at SMOOTH 0). Confirm: noon is silent, right
of noon glides as before, left of noon steps. Report what you heard; do not
tune anything by ear without the owner.

- [ ] **Step 8: Commit**

```bash
git add host/vcv/src/Fireflow.cpp
git commit -m "$(cat <<'EOF'
feat(vcv): wire the bipolar depth knobs to both lane readings

Depth params widen to -1..+1. mv() builds both the continuous and the S&H
deck/center terms from one frozen frame and lets the sign pick; the
engine-backed faces push the signed depth straight into Part, and an FX
target is active on either side of noon rather than only above it.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 7: Where the measured facts land

**Files:**
- Modify: `docs/engine-map.md` (§1 or §4 — wherever the lane state space
  records what a lane's slot count is)
- Modify: `docs/gotchas.md` (the "Host (VCV)" section, `:235`)
- Modify: `docs/roadmap.md`
- Modify: `docs/release-notes.md`

**Interfaces:** none — documentation only. It is a separate task because a
reviewer can reject the prose while approving the code, and because the map is
where a measured fact goes so the next session does not re-measure it
(`CLAUDE.md`, the probe rule).

- [ ] **Step 1: Put the FLOW slot-count measurement in the engine map**

Add to `docs/engine-map.md` §1 (the lane state space) a row or a short
subsection carrying probe 1's table verbatim — the five call/`deck_steps`/lane
steps rows from spec §7 — under a heading that names the trap:
"**In FLOW the VCV host leaves every lane at one slot.**" State the setup
(`SuperModulator`, `init(48000, 12345)`, the exact `set_step` calls,
2026-08-23) beside the numbers, per §6's rule.

- [ ] **Step 2: Add the gotcha**

Append to the "Host (VCV)" section of `docs/gotchas.md`:

```markdown
- **STPS 0 *is* FLOW on the VCV host, so `_steps` is 1 there.**
  `Fireflow.cpp` pushes `set_step(p, steps > 0, steps)` from a 0..16 knob —
  the position that selects FLOW is 0, and `set_step` clamps 0 to 1. Any
  engine feature that derives a grid from `_steps` therefore degenerates to a
  single slot per cycle in FLOW on this host, while the render host
  (`param_table.h`, `P_STEPS_A` 2..16 with a separate mode param) and
  `shell/` reach real counts. Measured 2026-08-23; it is what put the fixed
  `ModLane::kShFlowSlots` into the S&H split instead of a `_steps` grid.
  Do not raise the FLOW count to "fix" this without reading
  `docs/superpowers/specs/2026-08-22-mod-sh-split-design.md` §9: `_deck_steps`
  also feeds `pitch_step_samples()` into the sampler unguarded by mode
  (`part.cpp:390`), so the clock would move with it.
```

- [ ] **Step 3: Update the living status**

In `docs/roadmap.md`, mark the MOD depth split as shipped and note the two
things it deliberately did not do (a TEMP-locked FLOW grid; `shell/` wiring),
per spec §9.

- [ ] **Step 4: Rewrite the release notes**

`docs/release-notes.md` is the body of the *current* release, not a changelog —
rewrite it rather than appending. It needs: what the two halves of a depth knob
do, that noon is a standstill, and the one honest caveat from spec §7 probe
2.5 — sampling shaves the peaks slightly (0.9054 against 0.9411 p2p at
SMOOTH 0.7), because the grid rarely catches the exact extremes. Also say
plainly that in STEP at SMOOTH 0 both halves are the same signal, so the
feature is most audible in FLOW and at high SMOOTH.

- [ ] **Step 5: Commit**

```bash
git add docs/engine-map.md docs/gotchas.md docs/roadmap.md docs/release-notes.md
git commit -m "$(cat <<'EOF'
docs: record where FLOW's slot count actually comes from

The engine map gets probe 1's table (the VCV host leaves every lane at one
slot in FLOW) and gotchas gets the trap that follows from it, so the next
session does not re-measure it or "simplify" kShFlowSlots back to _steps.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

## Notes for whoever executes this

**Do not re-measure what is already measured.** Every number quoted in a test
comment above came from one of four probes run 2026-08-23 (scratchpad:
`probe_flowsteps.cpp`, `probe_sh_flow8.cpp`, `probe_sh_gates.cpp`,
`probe_dead.cpp`), and the spec's §7 carries the two that matter. If a gate's
expected count does not match, the implementation is wrong, not the number —
check the latch ordering in Task 1 Step 4 before touching an expectation.

**Do measure anything new.** If a step turns up a question the plan does not
answer, write a probe (`docs/engine-map.md` §6, 0.4 s to compile) rather than
reasoning about it. Three consecutive SHAPE/SMOOTH drafts were rejected on
facts no amount of reading finds.

**Verify at every task boundary.** Run `ctest --test-dir build
--output-on-failure` yourself and read the output. A subagent's report of a
green suite is not evidence — that has been wrong twice in this repo.

**Not in scope, do not drift into it:** a TEMP-locked S&H grid for FLOW; making
STPS reachable in FLOW on the VCV host; `shell/` firmware wiring; CV over
depths through the MOD1..4 jacks; any change to lane shuffle semantics. All
five are spec §9.
