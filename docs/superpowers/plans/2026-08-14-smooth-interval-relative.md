# SMOOTH becomes interval-relative — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `ModLane`'s absolute-seconds slew law with an interval-relative
one, so SMOOTH means the same thing at 0.02 Hz and at 30 Hz.

**Architecture:** One function changes — `ModLane::_update_slew`
(`engine/mod/lane.cpp:358`). Today it computes `τ = 0.00002 · 25000^smooth`, a
wall-clock time; it becomes `τ = smooth · TOP · interval`, where `interval` is the
lane cycle in FLOW, one slot in FLOW-melody, and one step in STEP. Everything else
in the change is consequence: two existing test cases that asserted the old law,
one new test file holding the gates the flow/Glow removal deleted, the shipped
init patch's stored SMOOTH values (which sit at the top of the knob only because
the knob currently does nothing), and one render hash.

**Tech Stack:** C++17, clang + Ninja, doctest, CMake. Panel assets are generated
by Python scripts, never hand-edited.

**Spec:** [`docs/superpowers/specs/2026-08-13-shape-smooth-rework-design.md`](../specs/2026-08-13-shape-smooth-rework-design.md)
(revision 7). Read it first — this plan argues from it and does not restate its
evidence.

## Global Constraints

- **Build:** `source env.sh` then
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`. **Release is not optional** —
  a Debug configure makes `spky_tests` and `ctrl_identity` fail with "SYNTH
  reference moved". Never MSVC. Never `source env.sh` in a shell used for
  `shell/` or `bench/`.
- **Test command:** `ctest --test-dir build --output-on-failure`.
- **Everything written into the repo is English** — code, comments, commit
  messages, docs.
- **Commit trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **No bit-exactness gates.** Renders are sanity checks, not checksums. Do not
  add a hash assertion that this plan does not ask for.
- **A test that cannot go red gets fixed.** Every new gate in Task 3 has an
  explicit prove-the-RED step. Do not skip it.
- **Shell hygiene:** never prefix a command with `cd` (the working directory is
  already the repo root), never modify a file through the shell (`sed -i`, `>`,
  `>>`) — use the editing tools.
- **`TOP_TEXTURE = 0.5`**, decided by ear on 2026-08-14 (spec §2.2). It is a
  by-ear constant: revisable by listening, not by argument, and not to be
  "fixed" toward 1.0 for symmetry with `interval`.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `engine/mod/lane.h` | Holds `kSmoothTopTexture` beside the other slew constants | 1 |
| `engine/mod/lane.cpp` | `_update_slew` — the law itself | 1 |
| `tests/test_lane.cpp` | G1 (rate invariance); re-baseline the glide case | 1, 2 |
| `tests/test_flow_melody.cpp` | Re-baseline the STEP-slew case | 2 |
| `tests/test_smooth_law.cpp` | **new** — G4′, G4″, G5 | 3 |
| `CMakeLists.txt` | Register the new test file | 3 |
| `host/vcv/res/gen_panel.py` | Source of truth for the stored SMOOTH defaults | 4 |
| `host/vcv/src/init_patch.hpp` | Generated — regenerate, never hand-edit | 4 |
| `host/vcv/res/test_panel.py` | The panel guard's mirror of those defaults | 4 |
| `tests/check_render_hash.cmake` | `wave_formant_sweep`'s expected hash | 5 |
| `docs/engine-map.md`, `docs/roadmap.md` | Measured facts and status | 6 |

---

### Task 1: The law

**Files:**
- Modify: `engine/mod/lane.h` (beside `kFlowSlewFrac`, ~line 293)
- Modify: `engine/mod/lane.cpp:358-398` (`_update_slew`)
- Test: `tests/test_lane.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `ModLane::kSmoothTopTexture` (`static constexpr float`, public, so
  tests can read it). `_update_slew()`'s signature is unchanged.

**Background the implementer needs:**

- `τ` feeds `OnePole::init(sr, time_s)` (`engine/util/onepole.h:14`), which
  computes `k = 1/(time_s·sr)` and clamps `k` to 1. **`time_s ≤ 0` sets `k = 1`,
  i.e. passthrough** — so the new law must floor `τ` at one sample rather than
  let `smooth = 0` produce exactly 0.
- Lines `386` and `395-397` are **a matched pair**. `:386` sets the per-sample
  slew; `:395-397` derives the tick-rate twin by compounding the same `k`
  formula `kTickInterval` times. The file says so in a comment. Compute `τ` once
  and let both read it — do not duplicate the law.
- **Every setter that can change the interval already refreshes the slew.**
  `set_rate_hz` → `_update_inc()` → `_update_slew()` (`lane.cpp:118, 260`);
  `set_step` ends in `_update_inc()`; `set_flow_melody` calls it directly
  (`:200`); `init` calls it (`:108`). **No new call site is needed.** Verify this
  rather than assume it — it is the one way this change can go silently wrong.
- **`_ev_rate` is deliberately not a recompute trigger.** It walks per wrap and
  is clamped to ±0.2; re-deriving the slew every wrap for a ±20 % term buys
  nothing, and the value refreshes at the next rate change. The existing comment
  at `lane.cpp:369-372` says this. Keep it true — do not add a recompute.
- `_effective_length()` (`lane.cpp:267`) returns `kFlowPhraseSlots` (8) in FLOW
  melody and `_steps` otherwise. **It is therefore NOT usable for the FLOW-LFO
  case**, where the interval is the whole cycle and `_effective_length()` would
  hand you the deck's step count.

- [ ] **Step 1: Write the failing test (G1 — rate invariance)**

Add to `tests/test_lane.cpp`:

```cpp
TEST_CASE("lane: SMOOTH is relative to the lane cycle, not to wall clock") {
    // The whole point of the interval-relative law: one knob position must
    // mean the same fraction of a cycle at every rate. Under the old absolute
    // law tau was fixed in seconds, so tau/cycle scaled with the rate and this
    // case fails by four orders of magnitude across the sweep.
    //
    // Measured as settling progress one full cycle after a step: with
    // tau = 0.25 * kSmoothTopTexture * cycle the remaining gap is
    // exp(-1/(0.25*TOP)) of the original, identical at every rate.
    const float rates[] = {0.02f, 0.5f, 5.f, 30.f};
    float first = 0.f;
    for (int r = 0; r < 4; ++r) {
        ModLane l;
        l.set_melodic(false);          // BEFORE init() -- see engine-map.md 6
        l.init(48000.f, 55);
        l.set_range(1.f);
        l.set_shape(0.5f);
        l.set_step(true, 2);
        l.set_smooth(0.25f);
        l.set_rate_hz(rates[r]);

        const int cycle = int(48000.f / rates[r]);
        // Settle, then find a boundary and measure one cycle of catch-up.
        for (int i = 0; i < cycle * 2; ++i) l.process();
        float out = l.process();
        float goal = l.target();
        // Walk to the next boundary where the target actually moves.
        int guard = cycle * 4;
        while (std::fabs(l.target() - goal) < 0.1f && guard-- > 0) {
            out = l.process();
        }
        REQUIRE(guard > 0);
        goal = l.target();
        const float gap0 = std::fabs(goal - out);
        REQUIRE(gap0 > 0.05f);
        for (int i = 0; i < cycle; ++i) out = l.process();
        const float ratio = std::fabs(goal - out) / gap0;

        if (r == 0) first = ratio;
        else CHECK(ratio == doctest::Approx(first).epsilon(0.02));
    }
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `ctest --test-dir build --output-on-failure -R spky_tests`

Expected: FAIL. Under the old law `τ` is a constant 0.0033 s while the cycle
spans 0.033 s to 50 s, so `ratio` is ~0 at 0.02 Hz and ~1 at 30 Hz — nowhere
near equal. **Record the actual four numbers in the commit message**; they are
this gate's proof of RED.

- [ ] **Step 3: Add the constant**

In `engine/mod/lane.h`, immediately after `kFlowSlewFrac` (~line 293):

```cpp
    // SMOOTH's ceiling on the four texture lanes, as a fraction of the lane
    // cycle. LANE_PITCH uses kFlowSlewFrac instead: a note must arrive inside
    // its own slot, and one value cannot serve both cases (spec 2.1).
    //
    // 0.5 rather than 1.0 by ear, 2026-08-14: at 0.5 the right stop lands
    // within 0.3% of where the OLD law's right stop already sat (p2p 0.322 vs
    // 0.323 at a 0.5 Hz patch), so the change is confined to the middle of the
    // axis -- which is where the complaint was. 1.0 buys 6 dB more ceiling at
    // a stop nobody asked for. Revisable by ear; the law does not depend on it.
    static constexpr float kSmoothTopTexture = 0.5f;
```

- [ ] **Step 4: Replace the law**

In `engine/mod/lane.cpp`, replace the opening of `_update_slew` (line 358, from
`float t = _fixed_slew ? ...` through the end of the `if (_flow_melody_on())`
clamp block, i.e. up to but not including `_slew.init(_sr, t);`) with:

```cpp
void ModLane::_update_slew() {
    // SMOOTH is a fraction of the lane's own INTERVAL, not a wall-clock time.
    // The old law was `0.00002 * pow(25000, _smooth)` -- absolute seconds
    // against cycles spanning four decades, so the knob's reach was whatever
    // the rate made it: inert on a 40 s cycle, annihilating on a 0.03 s one.
    // Spec: docs/superpowers/specs/2026-08-13-shape-smooth-rework-design.md 1-2.
    //
    // _fixed_slew keeps the absolute 0.02 s on purpose. It is reachable only
    // from the render host's set_fixed_slew scenario action and is an
    // absolute-seconds escape hatch for test fixtures (spec 4).
    float t;
    if (_fixed_slew) {
        t = 0.02f;
    } else {
        // Samples per cycle. _ev_rate is part of the phase advance, exactly as
        // in step_samples() -- and is deliberately NOT a recompute trigger; see
        // the note below the clamp.
        const double denom = _phase_inc * (1.0 + double(_ev_rate));
        double interval = 0.0;                 // in SAMPLES
        if (denom > 0.0) {
            const double cycle = 1.0 / denom;
            if (_step_mode) {
                // One STEP of THIS lane. In STEP each lane carries its own slot
                // count (kLaneRatio reappears as slots), so one knob position
                // is a different tau per lane -- intended, spec 2.3.
                interval = cycle / double(_steps);
            } else if (_flow_melody_on()) {
                // One SLOT, floored at the note minimum: where the floor
                // decimates, the raw slot is far shorter than the notes
                // actually are, and using it would glide much tighter than
                // anything needs.
                const double slot = cycle / double(_effective_length());
                const double floor_s = double(_note_min_samples);
                interval = slot > floor_s ? slot : floor_s;
            } else {
                // FLOW LFO: the lane cycle. NOTE _effective_length() is wrong
                // here -- it returns _steps on this path, not 1.
                interval = cycle;
            }
        }
        const float top = _melodic ? kFlowSlewFrac : kSmoothTopTexture;
        t = static_cast<float>(double(_smooth) * double(top) * interval)
          / _sr;
        // OnePole::init treats time_s <= 0 as passthrough (k = 1), which is the
        // right behaviour at SMOOTH 0 -- but floor it at one sample so a
        // stopped lane (denom == 0) cannot produce a negative or NaN tau.
        const float min_t = 1.f / _sr;
        if (!(t > min_t)) t = min_t;
    }
    _slew.init(_sr, t);
```

Then **delete** the now-dead `if (_flow_melody_on()) { ... }` clamp block that
followed the old law: with `TOP_MELODY = kFlowSlewFrac` and the interval pinned
to one slot, `τ` can never exceed the cap the clamp enforced (spec §2.1). Leave
`:395-397`'s tick twin exactly as it is — it reads `t` and must keep doing so.

- [ ] **Step 5: Verify the call sites**

Run: `grep -n "_update_slew()" engine/mod/lane.cpp`

Expected: five call sites — `init` (`:108`), `set_flow_melody` (`:200`),
`_update_inc` (`:260`), `set_smooth` (`:350`), `set_fixed_slew` (`:355`) — plus
the definition. Confirm by reading that `set_rate_hz` and `set_step` both reach
`_update_inc()`. **If any path can change `_rate_hz`, `_steps`, `_step_mode` or
`_flow_melody` without reaching `_update_slew()`, add the call and say so in the
commit message.**

- [ ] **Step 6: Run the test to verify it passes**

Run: `ctest --test-dir build --output-on-failure -R spky_tests`

Expected: the new G1 case PASSES. **Two other cases are expected to FAIL** —
`tests/test_lane.cpp`'s "SMOOTH turns a step into a glide" and
`tests/test_flow_melody.cpp`'s "STEP's slew is unchanged by the melody clamp".
That is by design and is Task 2's work. Do not fix them here and do not
"nudge until green".

- [ ] **Step 7: Commit**

```bash
git add engine/mod/lane.h engine/mod/lane.cpp tests/test_lane.cpp
git commit -m "feat(mod): SMOOTH becomes a fraction of the lane interval

The slew law was absolute seconds against cycles spanning four decades,
so the knob had no behaviour -- only a per-patch one. Measured across
every setpoint that survives in the repo it reached at most 0.026 x the
cycle, which is 0.11 dB.

Two cases go red by design and are re-baselined next.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: Re-baseline the two cases that assert the old law

**Files:**
- Modify: `tests/test_lane.cpp:35-54`
- Modify: `tests/test_flow_melody.cpp:564` (the case "STEP's slew is unchanged by
  the melody clamp")

**Interfaces:**
- Consumes: `ModLane::kSmoothTopTexture` from Task 1.
- Produces: nothing.

**Why these two, and why not delete them:** both still test something real. The
first proves SMOOTH produces a glide rather than a jump; the second proves the
melody clamp does not leak into STEP. Only their *numbers* were derived from the
old law. `fireflow-tests-must-be-able-to-fail` applies: re-baseline deliberately,
and keep each case's ability to catch its original regression.

- [ ] **Step 1: Re-baseline the glide case**

`tests/test_lane.cpp:35-54` runs STEP with 2 steps at 1 Hz, so one step is 6000
samples (0.125 s). At SMOOTH 0.5 the old `τ` was ~3 ms and the case asserts the
lane settles "well within a step". New `τ` = `0.5 · kSmoothTopTexture · 6000` =
1500 samples (31 ms at TOP 0.5), so it no longer settles inside 5000 samples the
way 3 ms did. Lower the knob instead of loosening the assertion — the case is
about a glide that settles, and a smaller SMOOTH is the honest way to ask for
one:

```cpp
TEST_CASE("lane: SMOOTH turns a step into a glide") {
    ModLane l;
    l.init(48000.f, 55);
    l.set_range(1.f);
    l.set_shape(0.5f);        // ramp: consecutive step values differ
    l.set_step(true, 2);      // step-clock: step = 6000 samples; boundary at ~6000
    // SMOOTH is a fraction of one STEP now, not an absolute time: 0.05 gives
    // tau = 0.05 * kSmoothTopTexture * 6000 = 150 samples (~3 ms at TOP 0.5),
    // which is the glide this case was written around. It was 0.5 under the
    // absolute law, where 0.5 also meant ~3 ms -- the number moved, the
    // intent did not.
    l.set_smooth(0.05f);
    l.set_rate_hz(1.f);       // cycle_hz = 4 -> 12000 samples/cycle

    for (int i = 0; i < 5000; ++i) l.process();    // settle in step 0
    float settled0 = l.process();
    float target0  = l.target();
    for (int i = 5002; i < 6050; ++i) l.process(); // cross into step 1
    float out_after = l.process();                 // ~1 ms past boundary
    float target1   = l.target();

    CHECK(target1 != doctest::Approx(target0));        // new value latched
    CHECK(std::fabs(out_after - target1) > 0.01f);     // output still gliding
    CHECK(std::fabs(settled0  - target0) < 0.01f);     // was settled before
}
```

- [ ] **Step 2: Re-baseline the STEP-clamp case**

`tests/test_flow_melody.cpp`'s "STEP's slew is unchanged by the melody clamp"
sets SMOOTH 1.0 on a STEP lane at 1 Hz / 8 steps and asserts the output has moved
less than 0.02 after 200 samples. Its own comment explains the bound: unclamped
moves ~0.006, a leaked melody clamp moves ~0.065, and 0.02 sits between them.

Under the new law unclamped `τ` = `1.0 · kSmoothTopTexture · 6000` = 3000
samples, so 200 samples of catch-up is `1 − e^(−200/3000)` ≈ 6.5 % of the gap —
roughly four times the old movement and no longer safely under 0.02.

**Do not widen the bound.** Widening destroys exactly the discrimination the
comment describes. Instead shorten the window so the same *fraction* is measured:

```cpp
    // 50 samples, not 200: the new law's tau is a fraction of the STEP, so the
    // window has to shrink with it to keep measuring the same part of the
    // curve. At tau = kSmoothTopTexture * 6000 samples this is ~1.7% of the
    // gap unclamped; a leaked melody clamp (tau = kFlowSlewFrac * one FLOW
    // slot, far shorter) still moves several times that. The bound below is
    // unchanged and still sits between the two.
    for (int i = 0; i < 50; ++i) out = step_lane.process();
```

- [ ] **Step 3: Prove the discrimination survived**

This is the step that keeps the case non-vacuous. Temporarily change
`_flow_melody_on()` to `_melodic` in `_update_slew`'s `top` selection — the exact
regression the case exists to catch — and confirm it goes RED.

Run: `ctest --test-dir build --output-on-failure -R spky_tests`
Expected: FAIL on "STEP's slew is unchanged by the melody clamp".
Then **revert the temporary change** and confirm it passes again.

- [ ] **Step 4: Run the full suite**

Run: `ctest --test-dir build --output-on-failure`

Expected: `spky_tests` green. The render-hash tests `ctrl_identity` and
`wave_formant_sweep` are Task 5 — `wave_formant_sweep` is expected to be RED
from here until then; `ctrl_identity` must be GREEN already (it sets SMOOTH 0.0
twice, which is passthrough under both laws). **If `ctrl_identity` is red, stop
and investigate — that is not an expected failure and means the law changed
something at SMOOTH 0.**

- [ ] **Step 5: Commit**

```bash
git add tests/test_lane.cpp tests/test_flow_melody.cpp
git commit -m "test(mod): re-baseline the two cases that encoded absolute seconds

Both still test what they were written for -- a glide that settles, and a
melody clamp that does not leak into STEP. Only their numbers came from
the old law. The clamp case's window shrinks rather than its bound
widening, so it keeps discriminating between unclamped and leaked.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The gates the removal deleted

**Files:**
- Create: `tests/test_smooth_law.cpp`
- Modify: `CMakeLists.txt:165` (add the file to `spky_tests`, before the closing
  `)`)
- Modify: `engine/mod/lane.h`, `engine/mod/lane.cpp:500,784,1031`,
  `engine/mod/super_modulator.h`, `engine/instrument.h` — the test accessor
  (Step 1b)

**Interfaces:**
- Consumes: `kFlowPoints` / `kStepPoints` from `tests/param_impact_points.h`;
  `apply_param` / `apply_mode_and_steps` from `engine/param_table.h`.
- Produces: nothing.

**Why this file exists:** `tests/test_flow_audio.cpp` held the only
whole-instrument audio-health gates and was deleted on 2026-08-14 with the flow
layer. Spec §3 rebuilds three of them on what survives. G5 (NaN-freedom) is not
this rework's business and is included because this is the first work since the
removal that renders every surviving setpoint anyway — see spec §3.

**Read `tests/test_param_impact.cpp:41-63` for the `FxMem` slot pattern** — two
concurrently-rendered instruments must not share echo/BBD memory. This file
renders one at a time, so one slot is enough, but the static-buffer shape is the
same and should be copied rather than reinvented.

- [ ] **Step 1: Write the file with all three gates**

Create `tests/test_smooth_law.cpp`:

```cpp
// The whole-instrument audio-health gates for the interval-relative SMOOTH
// law. Spec: docs/superpowers/specs/2026-08-13-shape-smooth-rework-design.md 3.
//
// G4' and G4" are deliberately DIFFERENT gates. Only the init patch is
// converted to preserve its sound (spec 2.4), so only it owes a +-3 dB band.
// The four frozen points carry SMOOTH values drawn from the deleted terrain
// generator against the OLD law; under the new one they legitimately land 5-14
// dB quieter, and asking them to hold a band would be asking the rework not to
// work. They owe continued life, which is what G4" asserts.
#include "doctest/doctest.h"
#include "param_table.h"
#include "param_impact_points.h"
#include "center/center.h"
#include "fx/flux.h"
#include "parts/bbd_engine.h"
#include <cmath>
#include <cstdio>
#include <string>

using namespace spky;

namespace {

float s_sl_echo[PART_COUNT][2][Flux::kMaxSamples];
float s_sl_bbd[PART_COUNT][2][BbdEngine::kCells];
AmbientReverb s_sl_reverb;

FxMem sl_fx_mem() {
    FxMem m;
    for (int p = 0; p < PART_COUNT; ++p) {
        m.echo[p][0] = s_sl_echo[p][0];
        m.echo[p][1] = s_sl_echo[p][1];
        m.bbd[p][0]  = s_sl_bbd[p][0];
        m.bbd[p][1]  = s_sl_bbd[p][1];
    }
    m.reverb = &s_sl_reverb;
    return m;
}

constexpr float kSr = 48000.f;
constexpr int   kTextureLanes[4] = {LANE_SOURCE, LANE_SIZE, LANE_MOTION, LANE_LEVEL};

// The shipped VCV init patch, in ENGINE ParamId order. init_patch.hpp is in
// VCV PANEL order, so this is mapped by NAME, not by index -- copying it
// positionally silently pairs every value with the wrong parameter.
void apply_init_patch(Instrument& in) {
    float v[P_COUNT] = {0.f};
    v[P_ENGINE_A] = 2.f;          v[P_ENGINE_B] = 0.f;
    v[P_SCALE]    = 3.f;
    v[P_SONG_B]   = 6.f;
    v[P_RATE_A]   = 0.184337318f; v[P_RATE_B]   = 0.163855359f;
    v[P_DENSITY_A]= 0.534939826f;
    v[P_SMOOTH_A] = kInitSmoothA; v[P_SMOOTH_B] = kInitSmoothB;
    v[P_DEPTH_A]  = 0.403613269f; v[P_DEPTH_B]  = 0.681928277f;
    v[P_VARIATION_A] = 0.768674195f; v[P_VARIATION_B] = 0.671083927f;
    v[P_TUNE_A]   = 0.001204819f; v[P_TUNE_B]   = 0.321686625f;
    v[P_ATTACK_A] = 1.f;          v[P_ATTACK_B] = 1.f;
    v[P_DECAY_A]  = 1.f;          v[P_DECAY_B]  = 1.f;
    v[P_RES_B]    = 0.220000312f;
    v[P_SUB_A]    = 0.738666236f;
    v[P_FILT_A]   = -0.199999928f; v[P_FILT_B]  = -0.292000026f;
    v[P_FLUXMIX_A]= 0.353333473f; v[P_FLUXMIX_B]= 0.650667071f;
    v[P_GRIT_A]   = 0.173493922f;
    v[P_COMP_A]   = 0.761333168f; v[P_COMP_B]   = 0.848000109f;
    v[P_COLOR_A]  = 0.001204819f; v[P_COLOR_B]  = 0.862999976f;
    v[P_REVMIX_A] = 0.343394309f; v[P_REVMIX_B] = 0.805333197f;
    v[P_MORPH]    = 0.495180398f; v[P_COUPLE]   = 1.0f;
    v[P_DRIFT]    = 0.791999996f;
    v[P_REV_SIZE] = 1.f;          v[P_REV_DECAY]= 0.800755024f;
    v[P_REV_TONE] = 0.905333221f; v[P_REV_DIFF] = 0.768000245f;
    v[P_TEMPO_BPM]= 50.f;         v[P_PACE]     = 0.5f;
    for (int p = 0; p < P_COUNT; ++p) {
        if (p == P_MODE || p == P_STEPS_A || p == P_STEPS_B) continue;
        apply_param(in, p, v[p]);
    }
    apply_mode_and_steps(in, false, 0, 0);
}

void apply_frozen(Instrument& in, const FrozenPoint& rp) {
    for (int p = 0; p < P_COUNT; ++p) {
        if (p == P_MODE || p == P_STEPS_A || p == P_STEPS_B) continue;
        apply_param(in, p, rp.v[p]);
    }
    in.set_sync(rp.step);
    in.set_step(PART_A, rp.step, rp.steps_a);
    in.set_step(PART_B, rp.step, rp.steps_b);
}

// Peak-to-peak of one lane's post-slew output, plus a NaN watch on the audio.
struct Sweep { float p2p[PART_COUNT][LANE_COUNT]; bool finite; };

Sweep sweep(Instrument& in, float seconds) {
    Sweep s{};
    s.finite = true;
    float mn[PART_COUNT][LANE_COUNT], mx[PART_COUNT][LANE_COUNT];
    for (int p = 0; p < PART_COUNT; ++p)
        for (int i = 0; i < LANE_COUNT; ++i) { mn[p][i] = 1e9f; mx[p][i] = -1e9f; }

    const int total = int(seconds * kSr);
    for (int n = 0; n < total; ++n) {
        float l = 0.f, r = 0.f;
        in.process(nullptr, nullptr, &l, &r, 1);
        if (!std::isfinite(l) || !std::isfinite(r)) s.finite = false;
        for (int p = 0; p < PART_COUNT; ++p)
            for (int i = 0; i < LANE_COUNT; ++i) {
                const float v = in.lane_value_for_test(p, i);
                if (v < mn[p][i]) mn[p][i] = v;
                if (v > mx[p][i]) mx[p][i] = v;
            }
    }
    for (int p = 0; p < PART_COUNT; ++p)
        for (int i = 0; i < LANE_COUNT; ++i) s.p2p[p][i] = mx[p][i] - mn[p][i];
    return s;
}

} // namespace

TEST_CASE("G4': the init patch's texture lanes keep their movement") {
    // Baseline captured on the pre-change tree -- see the plan, Task 3 Step 2.
    // Per lane, per deck, in ParamId lane order SOURCE/SIZE/MOTION/LEVEL.
    // A +-3 dB band: the conversion in gen_panel.py exists to hold this.
    static const float kBaseline[PART_COUNT][4] = {
        {0.f, 0.f, 0.f, 0.f},   // FILL FROM STEP 2
        {0.f, 0.f, 0.f, 0.f},   // FILL FROM STEP 2
    };
    Instrument in;
    in.init(kSr, sl_fx_mem());
    apply_init_patch(in);
    // The init patch's slowest texture lane cycles in ~153 s; 8 cycles is not
    // affordable in a unit test, so this measures 40 s -- long enough for the
    // two fast lanes to complete several cycles and for the slow ones to
    // traverse most of one, which is what a p2p comparison needs.
    const Sweep s = sweep(in, 40.f);
    CHECK_MESSAGE(s.finite, "G5: non-finite audio at the init patch");
    for (int p = 0; p < PART_COUNT; ++p)
        for (int k = 0; k < 4; ++k) {
            const float got = s.p2p[p][kTextureLanes[k]];
            const float want = kBaseline[p][k];
            const float db = 20.f * std::log10((got + 1e-9f) / (want + 1e-9f));
            CHECK_MESSAGE(std::fabs(db) <= 3.f,
                          "deck " << p << " lane " << kTextureLanes[k]
                                  << " moved " << db << " dB");
        }
}

// G4" and G5 share one render per frozen point. Kept as one TEST_CASE rather
// than two on purpose: `sweep` already returns both answers, and rendering the
// four points twice would double this file's contribution to suite runtime for
// nothing. The two CHECKs stay separately worded so a failure still names which
// gate fell over.
TEST_CASE("G4\"/G5: the frozen points still move, and stay finite") {
    for (int i = 0; i < kPer; ++i) {
        for (const FrozenPoint* rp : {&kFlowPoints[i], &kStepPoints[i]}) {
            Instrument in;
            in.init(kSr, sl_fx_mem());
            apply_frozen(in, *rp);
            const Sweep s = sweep(in, 20.f);
            CHECK_MESSAGE(s.finite, "G5: non-finite audio at "
                                        << std::string(rp->origin));
            for (int p = 0; p < PART_COUNT; ++p)
                for (int k = 0; k < 4; ++k)
                    CHECK_MESSAGE(s.p2p[p][kTextureLanes[k]] > 0.05f,
                                  "G4\": " << std::string(rp->origin) << " deck "
                                           << p << " lane " << kTextureLanes[k]);
        }
    }
}
```

**Runtime note:** G4′ renders 40 s and the case above renders 4 × 20 s, so this
file adds ~120 s of rendered audio to a suite that currently runs in a few
seconds. That is the price of a whole-instrument gate and it is why the two
cheap gates share a render. If it turns out unacceptable in practice, shorten
G4′'s window and re-capture its baseline at the shorter one — **do not** shorten
it without re-capturing, or the baseline describes a different measurement.
G4′'s `finite` flag is checked implicitly by the same sweep; assert it there too
rather than adding a fourth render.

**One constant this file declares itself:** `kInitSmoothA` / `kInitSmoothB`, at
the top of the anonymous namespace, as `constexpr float`, holding the
**pre-conversion** values — `0.836144507f` and `1.0f`. They are duplicated here
rather than included from `host/vcv/` because `tests/` must not depend on the VCV
host. Say so in a comment, and say that Task 4 converts them together with the
three VCV mirrors.

> **Corrected 2026-08-14 (preflight ruling).** An earlier draft of this task
> declared the *converted* values here. That is wrong twice over: G4′ could then
> never show the RED this task's Step 4 depends on, and Task 4's "G4′ now passes"
> would be a claim about a change it did not make. Declaring the original values
> here also makes the four mirrors literally move together in Task 4, which is
> the point of the `fireflow-control-merge-init-trap` memory that task cites.

> **Struck (preflight ruling).** Step 1b — building `lane_value_for_test` — moved
> to **Task 0**, which the controller inserted ahead of Task 1. The accessor has
> to exist on the *pre-law* tree so the G4′ baseline can be measured there, and
> by this task that tree is two commits back. The accessor is therefore already
> present when this task runs; use `Instrument::lane_value_for_test(part, lane)`
> as given. Task 0's brief carries the reasoning (the two one-poles, and why
> `_slew.value()` would be wrong for exactly the lanes these gates measure).

`ModLane` holds **two** one-pole smoothers, and which one carries the lane's
output depends on how the lane is driven:

- `process()` (per-sample) writes `_slew` — `lane.cpp:784`.
- `tick()` (per 96-sample control block) writes `_slew_tick` — `lane.cpp:500`
  and `:1031`.

**The four texture lanes in FLOW run `tick()`** (`super_modulator.cpp:167-168`) —
that is, exactly the lanes G4′ and G4″ measure. An accessor returning
`_slew.value()` would hand those lanes a value that was last written at `init()`,
and every gate in this file would compare two frozen numbers and pass. This is
the same trap spec §5.2 records for SHAPE: *a measurement taken on `process()`
does not describe a lane the engine drives through `tick()`.*

So record the value at the point of emission instead of guessing the source.

- [ ] **Step 2: Fill in the G4′ baseline**

**The controller measures this and hands it to you in the dispatch** — do not
try to derive it yourself.

Why it cannot be a step here: the baseline has to be measured on the tree as it
was *before* Task 1 changed the law, or the gate compares the new law against
itself and proves nothing. By the time this task runs, that tree is two commits
back, so no working-copy trick reaches it. The controller captured it on the
Task 0 tree — accessor present, law untouched, which is exactly the condition
the baseline needs.

Paste the eight numbers into `kBaseline`, delete the `FILL FROM STEP 2`
comments, and **record the numbers and the commit they were measured at in the
commit message** — a baseline nobody can trace is a baseline nobody can
re-derive.

- [ ] **Step 3: Register the file**

In `CMakeLists.txt`, add `tests/test_smooth_law.cpp` on its own line immediately
after `tests/test_param_impact.cpp` (line 165), before the closing `)`.

- [ ] **Step 4: Run and expect G4′ to FAIL**

Run: `ctest --test-dir build --output-on-failure -R spky_tests`

Expected: the G4″/G5 case PASSES; **G4′ FAILS**, reporting roughly −14 to −16 dB
per lane. That is correct at this point — the stored defaults have not been converted
yet. Task 4 makes it pass. **Record the failing dB figures**; they are the
evidence that Task 4's conversion did something.

- [ ] **Step 5: Prove the G4″ and G5 assertions can go red**

`fireflow-vacuous-test-gates`: a gate that cannot fail is decoration, and both
assertions in the shared case need their own proof.

- G4″: temporarily set `kSmoothTopTexture = 8.0f`. Expected: the G4″ CHECKs FAIL
  (lanes flattened) while the G5 CHECK still passes — which also proves the two
  assertions are independent despite sharing a render. Revert.
- G5: temporarily make `_update_slew` compute `t = 0.f/0.f` when
  `_smooth > 0.25f`. Expected: the G5 CHECK FAILS at the frozen points (their
  SMOOTH values run 0.276–0.764). Revert. **If G5 cannot be made to fail, delete
  it** and say so — a NaN gate that no NaN reaches is worse than no gate.

- [ ] **Step 6: Commit**

```bash
git add tests/test_smooth_law.cpp CMakeLists.txt engine/instrument.h engine/mod/lane.h engine/mod/lane.cpp engine/mod/super_modulator.h
git commit -m "test: whole-instrument gates for the SMOOTH law

Rebuilds three of the gates test_flow_audio.cpp held before the flow/Glow
removal deleted it, scoped to the setpoints that survive. G4' is red here
by design -- the stored defaults are converted next.

G5 asserts NaN-freedom over a populated patch set, which nothing has done
since 2026-08-14. It is narrower than what was lost and is included
because this is the first work since to render these points anyway.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: Convert the stored defaults

**Files:**
- Modify: `host/vcv/res/gen_panel.py:598,643` (`INIT_DEFAULTS`)
- Regenerate: `host/vcv/src/init_patch.hpp:10,30`
- Modify: `host/vcv/res/test_panel.py:2263,2287` (the guard's mirror)
- Modify: `tests/test_smooth_law.cpp` (`kInitSmoothA` / `kInitSmoothB`)

**Interfaces:**
- Consumes: `kSmoothTopTexture` from Task 1.
- Produces: nothing.

**The trap this task exists to avoid** (`fireflow-control-merge-init-trap`, which
bit four times in one branch): the stored SMOOTH defaults live in **four** places
that must move together. `init_patch.hpp` is **generated** — edit `gen_panel.py`
and regenerate; hand-editing it is silently undone by the next generator run.
`test_panel.py` is the panel guard wired into `CMakeLists.txt:300-304`, so missing
it fails the **build**, not the ear. Note also that `pytest` is not installed on
this machine — the panel guards run as plain scripts
(`fireflow-vcv-host-build-env`).

**Why the values change at all:** spec §2.4. `SMOOTH_A = 0.836` and
`SMOOTH_B = 1.0` sit at the top of the knob only because the knob currently does
nothing there; under the new law they would mean −14.6 and −16.1 dB and the
factory patch would go nearly static. The owner chose to preserve the shipped
sound.

- [ ] **Step 1: Derive the values**

```
smooth_new = (tau_today / T_ref) / kSmoothTopTexture
tau_today  = 0.00002 * 25000^smooth_old
T_ref      = the cycle of the deck's fastest texture lane (LANE_SOURCE)
```

Measured `T_ref` at the init patch: **deck A 38.240 s, deck B 38.423 s**;
`tau_today` **0.0951 s** and **0.5 s**. At `kSmoothTopTexture = 0.5`:

- `SMOOTH_A = (0.0951 / 38.240) / 0.5 = 0.004974` → **`0.004974`**
- `SMOOTH_B = (0.5 / 38.423) / 0.5 = 0.026026` → **`0.026026`**

(At TOP 1.0 they are `0.002487` and `0.013013`.)

One value cannot preserve all four texture lanes — they have different cycles and
the old law had no cycle in it. Anchoring on the fastest lane preserves the one
the knob reached furthest and leaves the slower three marginally smoother; spec
§2.4 bounds the residual.

- [ ] **Step 2: Edit `gen_panel.py`**

Line 598: `"SMOOTH_A": 0.836144507,` → `"SMOOTH_A": 0.004974,`
Line 643: `"SMOOTH_B": 1.000000000,` → `"SMOOTH_B": 0.026026,`

Add a comment above the block:

```python
    # SMOOTH_A/B were 0.836144507 / 1.0 -- the top of the knob, where they sat
    # because the absolute-seconds slew law made the knob inert (measured: at
    # most 0.11 dB anywhere in this patch). Converted 2026-08-14 to preserve
    # the shipped sound under the interval-relative law; see spec
    # docs/superpowers/specs/2026-08-13-shape-smooth-rework-design.md 2.4.
    # tests/test_smooth_law.cpp's G4' is the gate that these two are right.
```

- [ ] **Step 3: Regenerate and update the guard**

Run: `python host/vcv/res/gen_panel.py` **from `host/vcv/`**.

Then edit `host/vcv/res/test_panel.py:2263` to `"SMOOTH_A": 0.004974,` and
`:2287` to `"SMOOTH_B": 0.026026,`.

Verify the generated file changed:
Run: `git diff --stat host/vcv/src/init_patch.hpp`
Expected: two lines changed. **If it is unchanged, the generator was run from the
wrong directory** — it resolves paths relative to `host/vcv/`.

- [ ] **Step 4: Update the test's copy**

In `tests/test_smooth_law.cpp`, set `kInitSmoothA = 0.004974f` and
`kInitSmoothB = 0.026026f`.

- [ ] **Step 5: Run the panel guard and the suite**

Run: `python host/vcv/res/test_panel.py` (from `host/vcv/`)
Expected: PASS.

Run: `ctest --test-dir build --output-on-failure -R spky_tests`
Expected: **G4′ now PASSES.** If it does not, the conversion is wrong — report the
per-lane dB figures rather than widening the band.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/res/gen_panel.py host/vcv/src/init_patch.hpp host/vcv/res/test_panel.py tests/test_smooth_law.cpp
git commit -m "fix(vcv): convert the stored SMOOTH defaults to the new law

0.836 and 1.0 meant nothing under the absolute law and would mean -14.6
and -16.1 dB under the interval-relative one. Converted so the shipped
patch sounds as it does today; the knob now boots near its left stop,
which is honestly where it already sat.

All four mirrors moved together (gen_panel.py, the generated
init_patch.hpp, the panel guard, and the engine test's copy).

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: Re-cut the render hash

**Files:**
- Modify: `tests/check_render_hash.cmake`

**Interfaces:**
- Consumes: everything above.
- Produces: nothing.

**Which hashes move and why:** `wave_formant_sweep.json` sets `set_smooth` 0.65,
so its render changes. `ctrl_identity.json` sets `set_smooth` **0.0** twice —
passthrough under both laws — so it must **not** move. Verified by parsing in
spec §4. `ctrl_identity` moving is a bug report, not a re-cut.

- [ ] **Step 1: Confirm which is red**

Run: `ctest --test-dir build --output-on-failure`

Expected: `wave_formant_sweep` RED, `ctrl_identity` GREEN. **If `ctrl_identity` is
red, stop and investigate** — SMOOTH 0 must be bit-identical under the new law,
and if it is not, the floor in Task 1 Step 4 is wrong.

- [ ] **Step 2: Listen before re-cutting**

Run:
```bash
./build/render.exe host/render/scenarios/wave_formant_sweep.json /tmp/wfs.wav /tmp/wfs.csv
```

The hash is a sanity check, not a checksum (`fireflow-bit-exactness-not-required`),
so the question is whether the render still sounds like itself. **Hand this to
the owner rather than deciding it** — a re-cut hash is a recorded claim that the
new sound is the intended one.

- [ ] **Step 3: Re-cut**

Update `wave_formant_sweep`'s expected hash in `tests/check_render_hash.cmake`
with the new value from the failing test's output.

- [ ] **Step 4: Verify**

Run: `ctest --test-dir build --output-on-failure`
Expected: **all green.**

- [ ] **Step 5: Commit**

```bash
git add tests/check_render_hash.cmake
git commit -m "test: re-cut wave_formant_sweep for the interval-relative SMOOTH

It sets SMOOTH 0.65, so its render moves. ctrl_identity sets 0.0 twice and
is bit-identical under both laws -- confirmed green, not re-cut.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: Record what was measured

**Files:**
- Modify: `docs/engine-map.md` §1 (the STEP/FLOW SMOOTH row)
- Modify: `docs/roadmap.md` (the "Last updated" block and the SHAPE/SMOOTH entry
  under "Planned")

**Interfaces:**
- Consumes: everything above.
- Produces: nothing.

**Why this is a task and not a footnote:** `docs/engine-map.md` exists so the next
session does not re-measure what this one measured (`fireflow-probe-rule`). Its §1
currently records a **0.2995 STEP/FLOW gap at SMOOTH 1.0, 2 Hz**, produced by the
old law interacting with the `_flow_melody_on()` guard. That number now describes
an engine that does not exist.

- [ ] **Step 1: Re-measure the map's SMOOTH row**

Write a scratch probe reproducing §1's setup exactly — SMOOTH 1.0, 2 Hz, the same
seed and construction order the row states — and print the STEP and FLOW values
under the new law. Construction order matters: **`set_melodic()` before
`init()`** (engine-map §6).

- [ ] **Step 2: Update `docs/engine-map.md` §1**

Replace the row's numbers with the measured ones and add a dated line saying the
law changed and when. Do not delete the old number — state it as superseded, with
its date, the way the rest of the file does.

- [ ] **Step 3: Update `docs/roadmap.md`**

Under "Planned", the "SHAPE + SMOOTH rework" entry becomes a "Done" entry, moved
into the Done section in the house style: what it delivered, what it deliberately
did not (spec §6 — SHAPE, `_fixed_slew`, the `_flow_melody_on()` guard), the spec
and plan paths, and the branch name. Add a "Last updated" bullet at the top.

Say explicitly that **the Marbles round is now the only entry left under
"Planned" before M5k**, and that spec §5 hands it the measured SHAPE findings.

- [ ] **Step 4: Commit**

```bash
git add docs/engine-map.md docs/roadmap.md
git commit -m "docs: the map recorded a gap the new slew law does not produce

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Done when

- `ctest --test-dir build --output-on-failure` is green, including
  `ctrl_identity` (unchanged) and `wave_formant_sweep` (re-cut after listening).
- `python host/vcv/res/test_panel.py` passes from `host/vcv/`.
- G1, G4′, G4″ and G5 have each been shown RED once, and the RED is recorded in a
  commit message.
- The owner has heard `wave_formant_sweep` and accepted it.
- `docs/engine-map.md` §1 no longer describes the old law.

## Deliberately out of scope

Copied from spec §6 so an executor does not drift into them:

- **SHAPE.** Spec §5 hands it to the Marbles/VARY round with its measurements.
- **`_fixed_slew`** stays absolute at 0.02 s, by decision (spec §4).
- **Removing the `_flow_melody_on()` guard** on the melody path.
  `docs/engine-map.md` §7 owns that question.
- **Re-tuning `TOP_MELODY`.** 0.35 is ear-confirmed and reused, not re-derived.
