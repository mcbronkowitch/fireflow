# DPTH and EDGE across all six engines — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the two new VOICE knobs mean something on all six engines — DPTH as the MOTION lane's hand-set base, EDGE as a bipolar trim on each engine's second filter.

**Architecture:** DPTH needs almost no engine code: every engine already reads `LANE_MOTION`, and only the VCV host pins its base to 0.5 through one ternary. EDGE needs a new broadcast setter (`Part::set_voice_edge`, mirroring `set_voice_filt`), a `set_edge(float t)` on each engine with `t == 0` meaning that engine's existing behaviour, and — on the three linear engines — a new one-pole high-pass on the engine's stereo sum.

**Tech Stack:** C++17 engine (`engine/`), doctest (`tests/`), clang + Ninja + CMake for desktop, Python generators for both panels (`host/vcv/res/gen_panel.py`, `gen_hw_panel.py`), ARM GCC for `bench/` and `shell/`.

**Spec:** [`docs/superpowers/specs/2026-08-19-voice-knobs-dpth-edge-design.md`](../specs/2026-08-19-voice-knobs-dpth-edge-design.md) — read it before Task 1; this plan argues from it and does not repeat its reasoning.

## Global Constraints

- **Build:** `source env.sh` first, then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`. **Release is not optional** — a Debug configure makes `spky_tests` and `ctrl_identity` fail with "SYNTH reference moved". Never `source env.sh` in a shell used for `bench/` or `shell/`.
- **VCV plugin:** always `host/vcv/build-local.sh`; never invoke `g++` by hand (the system one is the ARM cross-compiler).
- **Panels are generated**, never hand-edited. Guards run as plain scripts (`python res/test_panel.py`), not under pytest — pytest is not installed on this machine.
- **Never `cd` in a shell command** and never write into the repo through the shell (no `sed -i`, `>`, `tee`). Use the editing tools, or a Python script file in the scratchpad.
- **Everything written into the repo is English.**
- **Commit trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **The probe rule:** no runtime claim enters a comment, a doc or a commit message until a probe or a bench printed it. A number from this plan is a claim until you re-print it.
- **Every gate must be proven RED once.** A test that cannot fail gets fixed, even if this plan mandated it. Watch for the four vacuous shapes: asserting a setter's own echo, asserting a constant against itself, asserting on an unreached branch, asserting a tolerance nothing can exceed.
- **EDGE's neutral is `t == 0` on every engine, and neutral must be bit-unchanged.** This is the property that keeps five engines' factory sound intact; it is the first thing to test in every engine task.
- **Branch:** cut `feat/voice-knobs-dpth-edge` from `main` before Task 1. Do not work on `main`.

---

### Task 1: The sampler's MOTION base becomes a scale

The one engine-side change DPTH needs. Today `Part::_control_tick` throws the base away on a sampler deck; it becomes a halved base instead. Spec §3.3.

**Files:**
- Modify: `engine/sampler/sampler_config.h` (new constant)
- Modify: `engine/parts/part.cpp` (the `ENGINE_SAMPLER` block after `_tg[LANE_PITCH]`)
- Modify: `tests/test_sampler_part.cpp` (new cases, and one stale comment)

No new test file: the scaling lives in `Part::_control_tick`, so the test has
to drive a `Part`, and `tests/test_sampler_part.cpp` already owns `InstRig`
for exactly that. `tests/test_sampler_engine.cpp`'s `Rig` wraps the bare
engine and cannot see this code path at all.

**Interfaces:**
- Consumes: nothing.
- Produces: `spky::sampler_cfg::kMotionBaseScale` (`constexpr float`, value `0.5f`). Task 9 cites it in the docs.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_sampler_part.cpp`. The observation already exists —
`SamplerEngine::last_spawn_pos()` and `spawn_count()`, which
`tests/test_sampler_engine.cpp::collect()` uses to build `SpawnStats.pos`.
Copy that loop's shape against `InstRig` rather than inventing a second
observer, and do not infer spawn positions from audio.

```cpp
// The sampler used to DISCARD LANE_MOTION's base (part.cpp), which made the
// knob that now writes it inert there. It reads it halved instead. Measured
// at the effect -- the scatter the base is supposed to cause -- not at the
// base, which would be the setter asserting its own echo.
static float spawn_pos_spread(float dpth_base, int want = 200) {
    InstRig g;
    g.load_material();                    // whatever InstRig's loader is called
    g.p.set_engine(ENGINE_SAMPLER);
    g.p.set_depth(0.f);                   // MOD 0: the lane contributes nothing,
                                          // so only the BASE can cause scatter
    g.p.set_target_base(LANE_MOTION, dpth_base);
    std::vector<float> pos;
    int last = g.e().spawn_count(), guard = 0;
    while (int(pos.size()) < want && guard++ < 4000000) {
        g.render(64);
        if (g.e().spawn_count() != last) {
            last = g.e().spawn_count();
            pos.push_back(g.e().last_spawn_pos());
        }
    }
    REQUIRE(int(pos.size()) == want);      // a starved loop would make every
                                           // assertion below vacuous
    const auto mm = std::minmax_element(pos.begin(), pos.end());
    return *mm.second - *mm.first;
}

TEST_CASE("sampler: DPTH 0 is exactly the old flattened behaviour") {
    CHECK(spawn_pos_spread(0.f) == doctest::Approx(0.f));
}

TEST_CASE("sampler: DPTH mid-travel scatters, and less than the knob's top") {
    const float mid = spawn_pos_spread(0.5f);
    CHECK(mid > 0.f);
    // The degenerate window -- one whole content length, where ORGANIZE and
    // SCAN provably stop mattering -- is now only reachable at the knob's top.
    CHECK(mid < spawn_pos_spread(1.f));
}
```

Adapt `InstRig`'s exact member names by reading the file; the shape above is
the contract, not the spelling.

- [ ] **Step 2: Run it and watch it fail**

```
source env.sh && cmake --build build && ./build/spky_tests.exe -ts="*sampler*motion*"
```

Expected: the mid-travel case FAILS (spread is 0 — the base is discarded).

- [ ] **Step 3: Add the constant**

In `engine/sampler/sampler_config.h`, next to `kScatterPosFrac`:

```cpp
// LANE_MOTION's base, as read by a SAMPLER deck (part.cpp). Halved, and the
// halving is the whole point: at base >= 0.5 the position jitter is uniform
// over one content length, which makes ORGANIZE and SCAN provably irrelevant
// (measured 2026-07-22: means 12036 / 11896 / 11951 at SOURCE 0 / 0.25 / 0.9
// over content 24000). That state used to be unavoidable, which is why the
// base was discarded entirely; with DPTH on the panel it becomes the top of
// the knob's travel instead -- a choice, not an accident.
constexpr float  kMotionBaseScale = 0.5f;
```

- [ ] **Step 4: Replace the flattening**

In `engine/parts/part.cpp`, the `if (_engine_id == ENGINE_SAMPLER)` block that currently reads:

```cpp
const float mmod = _active[LANE_MOTION]
    ? _mod.lane_output(LANE_MOTION) * _depth * _tdepth[LANE_MOTION]
    : 0.f;
_tg[LANE_MOTION] = clampf(mmod, 0.f, 1.f);
```

becomes:

```cpp
_tg[LANE_MOTION] = clampf(
    _base[LANE_MOTION] * sampler_cfg::kMotionBaseScale + _mod_term(LANE_MOTION),
    0.f, 1.f);
```

`_mod_term(LANE_MOTION)` is exactly the expression it replaces — `LANE_MOTION` is neither `LANE_PITCH` nor the sampler's `LANE_SOURCE`, so neither of `_mod_term`'s two special cases applies. Rewrite the German comment block above it in English (it is being changed, so it is no longer one of the sixteen frozen German files) and keep the measurement it records.

- [ ] **Step 5: Fix the comment this falsifies**

`tests/test_sampler_part.cpp` around line 646 carries a German comment stating
that `LANE_MOTION`'s base is 0.5 and nobody writes it. Both halves are now
wrong. Rewrite it in English, saying what is true: the host writes it from
DPTH, and the sampler halves it.

- [ ] **Step 6: Run the tests**

```
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS, all of them. If `sampler_extremes` or another render moved, that is real — a sampler deck's scatter at DPTH 0.5 is genuinely new behaviour. Re-baseline deliberately and say so in the commit.

- [ ] **Step 7: Prove the RED**

Set `kMotionBaseScale` to `0.f` and re-run: the mid-travel case must fail. That
is the whole behaviour in one constant, so it is the honest mutation.

- [ ] **Step 8: Commit**

```bash
git add engine/sampler/sampler_config.h engine/parts/part.cpp tests/test_sampler_part.cpp
git commit -m "feat(sampler): MOTION's base is halved, not discarded"
```

---

### Task 2: DPTH reaches all six engines

The ternary goes, the caption tuple arrives, and the host guard inverts. No engine code at all. Spec §3.1, §5.

**Files:**
- Modify: `host/vcv/src/Fireflow.cpp` (the `LANE_MOTION` base push, ~line 959)
- Modify: `host/vcv/res/gen_panel.py` (`DYNAMIC_CAPTIONS`, the `DEPTH_A/B` `Ctl` labels)
- Modify: `host/vcv/res/test_panel.py` (`feed_host_wiring_issues`, its mutation list, the caption fixtures)
- Modify: `host/vcv/res/test_hw_panel.py` if the plate word check needs the new tuple

**Interfaces:**
- Consumes: `sampler_cfg::kMotionBaseScale` behaviour from Task 1 (not its symbol).
- Produces: `DYNAMIC_CAPTIONS` entry `("DEPTH", "ENGINE", ("DPTH", "SCAT", "DPTH", "SWAY", "RPTS", "DPTH"))`. Task 8 adds the `DAMP` row beside it.

- [ ] **Step 1: Write the failing guard change**

In `host/vcv/res/test_panel.py`, inside `feed_host_wiring_issues`, replace the FEED-only assertion

```python
    if "feedPart?pp(DEPTH_A,p):0.5f" not in push_n:
        issues.append("LANE_MOTION's base must be the DPTH knob on a FEED deck "
                      "and Part's own 0.5f elsewhere")
```

with

```python
    if "inst.set_target_base(p,spky::LANE_MOTION,pp(DEPTH_A,p));" not in push_n:
        issues.append("DPTH must write LANE_MOTION's base on EVERY engine -- "
                      "each one reads that lane as something (width, drift, "
                      "scatter, feedback, FM index) and a per-engine ternary "
                      "here is what made five of them unreachable")
    if "feedPart?pp(DEPTH_A,p)" in push_n:
        issues.append("the FEED-only ternary on LANE_MOTION's base is back")
```

and add the matching entry to `test_feed_host_wiring_guard_rejects_representative_regressions`:

```python
        ("            inst.set_target_base(p, spky::LANE_MOTION, pp(DEPTH_A, p));",
         "            inst.set_target_base(p, spky::LANE_MOTION,\n"
         "                                 feedPart ? pp(DEPTH_A, p) : 0.5f);",
         "the FEED-only ternary restored"),
```

- [ ] **Step 2: Run it and watch it fail**

```
python host/vcv/res/test_panel.py
```

Expected: FAIL — the source still holds the ternary.

- [ ] **Step 3: Drop the ternary**

In `host/vcv/src/Fireflow.cpp`:

```cpp
            // DPTH writes LANE_MOTION's base on every engine, because every
            // engine reads that lane: width (and drift) on SYNTH/WAVE, drift
            // alone on BODY, scatter on the sampler, the feedback amount on
            // the BBD, the FM index on FEED. This host never wrote the base at
            // all until 2026-08-18, so all six had a control whose ends the
            // player could not reach; FEED got the repair first, through a
            // ternary that pinned the other five to Part's compiled-in 0.5.
            // The knob's init default IS that 0.5 (and IS feed_cfg::kDepthBase),
            // so an untouched patch writes exactly what the ternary wrote --
            // the sampler excepted, which halves the base (sampler_config.h).
            inst.set_target_base(p, spky::LANE_MOTION, pp(DEPTH_A, p));
```

- [ ] **Step 4: Add the caption tuple and the resting words**

In `gen_panel.py`, append to `DYNAMIC_CAPTIONS`:

```python
    ("DEPTH",    "ENGINE",   ("DPTH", "SCAT", "DPTH",  "SWAY",  "RPTS", "DPTH")),
```

The SYNTH cell is the axis name on purpose — see spec §6: `gen_hw_panel.place()` applies `HW_CAPTION` *before* `test_hw_panel.py`'s `test_static_captions_only` reads the label, so a plate word that departs from `words[0]` fails the guard rather than slipping past it. `FILT`'s tuple has the same shape.

The two `Ctl` labels stay `"DPTH"` — they already equal `words[0]`, so nothing moves on either panel.

- [ ] **Step 5: Regenerate, run both guards, prove the RED**

```
python host/vcv/res/gen_panel.py
python host/vcv/res/gen_hw_panel.py
python host/vcv/res/test_panel.py
python host/vcv/res/test_hw_panel.py
```

Expected: PASS. Then prove RED twice by hand: restore the ternary (guard must fail), and change the `SWAY` cell to `DRFT` (a word `HW_CAPTION` already prints for the global DRIFT knob — the printed-word gate must catch it). Restore both.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/src/Fireflow.cpp host/vcv/res/gen_panel.py host/vcv/res/test_panel.py host/vcv/src/generated_panel.hpp host/vcv/src/generated_hw_panel.hpp host/vcv/res/Fireflow.svg host/vcv/res/FireflowHW.svg
git commit -m "feat(vcv): DPTH writes MOTION's base on every engine"
```

---

### Task 3: The EDGE spine, and FEED as its first tenant

The broadcast setter, the param-table entry, the render-host action, and FEED's conversion from an absolute Hz setter to a trim. Spec §4.2, §4.4.

**Files:**
- Create: `engine/util/onepole_hp.h`
- Modify: `engine/feed/feed_engine.h`, `engine/feed/feed_engine.cpp`, `engine/feed/feed_config.h`
- Modify: `engine/parts/part.h` (broadcast line; remove `set_feed_damp_hz`)
- Modify: `engine/instrument.h` (forward; remove `set_feed_damp_hz`)
- Modify: `engine/param_table.h` (`X(P_EDGE_A, -1.f, 1.f, 0)`, `apply_param` cases)
- Modify: `host/render/scenario.cpp` (`set_voice_edge` action)
- Modify: `host/vcv/src/Fireflow.cpp` (push `set_voice_edge`; delete `feedDampHzFromKnob` and `kFeedDampLoHz/HiHz`)
- Modify: `tests/test_feed_engine.cpp`, `tests/test_param_table.cpp`
- Create: `tests/test_voice_edge_broadcast.cpp`; modify `CMakeLists.txt`

**Interfaces:**
- Produces, and every later engine task consumes exactly these:
  - `void <Engine>::set_edge(float t)` — bipolar, `t` clamped to `[-1, +1]`, **`t == 0` leaves the engine bit-unchanged**.
  - `void Part::set_voice_edge(float t)` — broadcasts to all six engines.
  - `void Instrument::set_voice_edge(int p, float t)`.
  - `spky::OnePoleHp` with `void init(float sr)`, `void set_hz(float hz)`, `float process(float x)`, `void reset()`.
  - `feed_cfg::kEdgeOctaves` — the trim's span in octaves either side of neutral.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_voice_edge_broadcast.cpp`:

```cpp
#include <doctest/doctest.h>
#include "instrument.h"

using namespace spky;

// An engine missing from a broadcast line is a knob that is silently dead on
// that engine -- this project's oldest recurring failure (part.h says so on
// the set_voice_* block). One case per engine, each asserting that EDGE at a
// non-zero trim CHANGES that engine's output, and that EDGE at 0 does not.
static void render(Instrument& inst, float* l, float* r, int n) {
    float in[64] = {};
    for (int i = 0; i < n; i += 64) inst.process(in, in, l + i, r + i, 64);
}

static void edge_case(EngineId eng) {
    static float a[24000], b[24000], c[24000], d[24000];
    Instrument i1, i2;
    i1.init(48000.f, 7); i2.init(48000.f, 7);
    i1.set_engine(PART_A, eng); i2.set_engine(PART_A, eng);
    i1.set_voice_edge(PART_A, 0.f);          // neutral
    i2.set_voice_edge(PART_A, 0.9f);         // trimmed
    render(i1, a, b, 24000);
    render(i2, c, d, 24000);
    double diff = 0.0;
    for (int n = 0; n < 24000; ++n) diff += std::fabs(a[n] - c[n]);
    CHECK(diff > 1e-3);                       // the knob reaches this engine
}

TEST_CASE("edge: the trim reaches FEED")    { edge_case(ENGINE_FEED); }
```

Only the FEED case exists in this task. Tasks 4–7 each add their own case to this file; that is deliberate — the file is the ledger of which engines are wired.

Add to `tests/test_feed_engine.cpp`:

```cpp
TEST_CASE("feed: EDGE at 0 is exactly kDampFixedHz") {
    FeedEngine e;
    e.init(48000.f);
    const float neutral = e.damp_hz_for_test();
    e.set_edge(0.f);
    CHECK(e.damp_hz_for_test() == neutral);
    CHECK(neutral == doctest::Approx(feed_cfg::kDampFixedHz));
}

TEST_CASE("feed: EDGE spans kEdgeOctaves either side, symmetrically") {
    FeedEngine e;
    e.init(48000.f);
    e.set_edge(1.f);
    CHECK(e.damp_hz_for_test() ==
          doctest::Approx(feed_cfg::kDampFixedHz *
                          std::pow(2.f, feed_cfg::kEdgeOctaves)));
    e.set_edge(-1.f);
    CHECK(e.damp_hz_for_test() ==
          doctest::Approx(feed_cfg::kDampFixedHz *
                          std::pow(2.f, -feed_cfg::kEdgeOctaves)));
}
```

- [ ] **Step 2: Run and watch them fail**

```
source env.sh && cmake --build build
```

Expected: FAIL TO COMPILE — `set_edge` does not exist. That is the correct red for this step.

- [ ] **Step 3: Write `OnePoleHp`**

Create `engine/util/onepole_hp.h`:

```cpp
#pragma once
#include <cmath>

namespace spky {

// A plain one-pole high-pass for the audio path.
//
// NOT engine/util/onepole.h: that one is a control-rate SMOOTHER and carries a
// 0.0005 deadband (`if (!_smoothing && fabs(diff) < 0.0005f) return _value;`)
// which is exactly wrong under an audio signal -- it would gate quiet passages
// to a frozen value. Same order, different job.
//
// y[n] = a * (y[n-1] + x[n] - x[n-1]), a = 1 / (1 + 2*pi*fc/sr).
class OnePoleHp {
public:
    void init(float sample_rate) {
        _sr = sample_rate > 0.f ? sample_rate : 48000.f;
        reset();
        set_hz(20.f);
    }
    void reset() { _x1 = 0.f; _y1 = 0.f; }
    void set_hz(float hz) {
        const float f = hz < 0.f ? 0.f : (hz > 0.45f * _sr ? 0.45f * _sr : hz);
        _a = 1.f / (1.f + 6.2831853f * f / _sr);
    }
    float process(float x) {
        _y1 = _a * (_y1 + x - _x1);
        _x1 = x;
        return _y1;
    }
private:
    float _sr = 48000.f, _a = 1.f, _x1 = 0.f, _y1 = 0.f;
};

} // namespace spky
```

Note for the implementer: at `_a == 1` this is not a bypass, it is a DC blocker. **Neutral for the three linear engines is therefore "corner at the bottom rail", and the bottom rail has to be low enough that the engine's own DC blocker already does the same job** — check what `Part`/`PartFx` already remove before choosing it, and pin the choice with the neutral test, not by eye.

- [ ] **Step 4: Give FEED `set_edge`**

In `feed_config.h`, beside `kDampFixedHz`:

```cpp
// EDGE's span either side of neutral, in octaves. Neutral (t == 0) is
// kDampFixedHz itself, so a patch that never touches the knob is unchanged.
// FIRST VALUE -- the by-ear pass owns it (docs/by-ear-decisions.md).
constexpr float kEdgeOctaves = 2.f;
```

In `feed_engine.h`/`.cpp`, replace `set_damp_hz(float hz)` with:

```cpp
void FeedEngine::set_edge(float t) {
    const float c = clampf(t, -1.f, 1.f);
    if (c == _edge) return;              // the exact-argument guard stays: the
    _edge = c;                           // host pushes this every block
    _set_damp_hz(feed_cfg::kDampFixedHz *
                 std::pow(2.f, feed_cfg::kEdgeOctaves * c));
}
```

`_set_damp_hz` is the old public `set_damp_hz`, made private and keeping its own clamp and `expf`. `init()` calls `set_edge(0.f)` after forcing `_edge` to a value it cannot be (e.g. `-2.f`) so the first call is not swallowed by the guard.

- [ ] **Step 5: Wire the spine**

- `part.h`: delete `set_feed_damp_hz`; add beside `set_voice_filt`, with all six engines on one line and a comment saying an engine missing from it is a silently dead knob:

```cpp
    void set_voice_edge(float t)      { _synth.set_edge(t);     _wave.set_edge(t);     _body.set_edge(t);      _sampler.set_edge(t);          _bbd.set_edge(t);      _feed.set_edge(t); }
```

  Until Tasks 4–7 land, the other five `set_edge` bodies are one-line stubs that store `_edge` and do nothing else. **Say so in each stub's comment, with the task that fills it in** — a silent stub is the failure this line exists to prevent.
- `instrument.h`: `void set_voice_edge(int p, float t) { _parts[p].set_voice_edge(t); }`; delete `set_feed_damp_hz`.
- `param_table.h`: add `X(P_EDGE_A, -1.f, 1.f, 0)   X(P_EDGE_B, -1.f, 1.f, 0)` next to `P_FILT_*`, and the two `apply_param` cases.
- `scenario.cpp`: `else if (a == "set_voice_edge") inst.set_voice_edge(e.part, e.value);` — **without this line EDGE is unreachable from every render test**, which is the one way this feature could ship with a green suite and no coverage at all. DPTH needs nothing here: `set_target_base` is already a scenario action.
- `shell/`: **nothing to do, and that is a finding, not an omission.** The firmware maps one control (`shell/src/controls.cpp`); it never called `set_feed_damp_hz` and will not call `set_voice_edge`. Stated here so nobody goes looking for a third host to wire.
- `Fireflow.cpp`: delete `kFeedDampLoHz`, `kFeedDampHiHz`, `feedDampHzFromKnob` and `FeedDampParamQuantity`; `configParam` for `DAMP_A/B` becomes a plain bipolar `configParam(c.id, -1.f, 1.f, init, lbl)`; `pushParams` pushes `inst.set_voice_edge(p, pp(DAMP_A, p))`.

- [ ] **Step 6: Fix the two gates this moves**

`test_panel.py`'s `feed_host_wiring_issues` currently pins `inst.set_feed_damp_hz(p,feedDampHzFromKnob(pp(DAMP_A,p)));`. It becomes `inst.set_voice_edge(p,pp(DAMP_A,p));`. `test_feed_shipped_defaults_are_the_engine_constants` is rewritten in Task 8 — for now, make it assert the neutral: `INIT_DEFAULTS["DAMP_A"] == 0.0`.

`INIT_DEFAULTS` in `gen_panel.py`: `"DAMP_A"/"DAMP_B": 0.0` (the knob is bipolar now; centre is neutral).

- [ ] **Step 7: Build, test, prove the RED**

```
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure
python host/vcv/res/test_panel.py
```

RED proofs, each restored after: remove `_feed.set_edge(t)` from the broadcast line (the FEED case in `test_voice_edge_broadcast.cpp` must fail); change `kEdgeOctaves`'s sign in the trim (the symmetry test must fail); set `INIT_DEFAULTS["DAMP_A"]` to 0.1 (the neutral gate must fail).

- [ ] **Step 8: Commit**

```bash
git add engine host/render/scenario.cpp host/vcv/src/Fireflow.cpp host/vcv/res tests CMakeLists.txt
git commit -m "feat(engine): EDGE becomes a broadcast trim, FEED first"
```

---

### Task 4: EDGE on BODY — the exciter's corner

Spec §4.3, §4.6. Trim on a filter that already exists, plus the blind spot written down.

**Files:**
- Modify: `engine/body/exciter.h` (`_recompute_filter`, a `set_edge`)
- Modify: `engine/body/body_voice.h`/`.cpp` (forward `set_edge` to the exciter)
- Modify: `engine/synth/synth_engine.h` (dispatch `set_edge` to the voice)
- Modify: `tests/test_body_engine.cpp`, `tests/test_voice_edge_broadcast.cpp`

**Interfaces:**
- Consumes: `set_edge(float t)` from Task 3.
- Produces: `BodyVoice::set_edge`, and `Exciter::set_edge`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("body: EDGE at 0 leaves the exciter's corner exactly where RESO put it") {
    Exciter ex;
    ex.init(1, 48000.f);
    ex.set_character(0.2f);                 // zone 0, click
    const float k0 = ex.coef_for_test();
    ex.set_edge(0.f);
    CHECK(ex.coef_for_test() == k0);        // bit equality, not Approx
}

TEST_CASE("body: EDGE moves the corner in both directions, inside zones 0 and 1") {
    Exciter ex;
    ex.init(1, 48000.f);
    for (float c : {0.2f, 0.5f}) {          // click zone, noise zone
        ex.set_character(c);
        ex.set_edge(0.f);   const float k0 = ex.coef_for_test();
        ex.set_edge(+1.f);  CHECK(ex.coef_for_test() > k0);
        ex.set_edge(-1.f);  CHECK(ex.coef_for_test() < k0);
    }
}

TEST_CASE("body: EDGE is inert in zone 2, and that is the documented behaviour") {
    // NOT a bug and NOT a tolerance: Exciter::process computes
    // sputter*(1-t) + ping*t and never calls _lp.process(), so the one-pole is
    // not in the zone-2 path at all. This test exists so that INSERTING it
    // later breaks something loudly instead of quietly changing a character.
    Exciter ex;
    ex.init(1, 48000.f);
    ex.set_character(0.9f);                 // zone 2, sputter -> ping
    ex.set_edge(0.f);   const float a = ex.render_sum_for_test(4800);
    ex.set_edge(+1.f);  CHECK(ex.render_sum_for_test(4800) == a);
}
```

`coef_for_test()` and `render_sum_for_test(int)` are new `#ifdef SPKY_TESTING` observers on `Exciter`, following `Part::overlap_eff()`'s idiom.

- [ ] **Step 2: Run and watch them fail**

```
source env.sh && cmake --build build && ./build/spky_tests.exe -ts="*body*EDGE*"
```

Expected: compile failure (`set_edge` on `Exciter` does not exist).

- [ ] **Step 3: Implement**

`Exciter` gains `void set_edge(float t) { _edge = clampf(t, -1.f, 1.f); _recompute_filter(); }` and `_recompute_filter` multiplies its computed `cutoff_hz` by `std::pow(2.f, kEdgeOctaves * _edge)` before the coefficient conversion — control rate only, never in `process()`. Put `kEdgeOctaves` for BODY next to the zone table in `exciter.h`, as a first value.

`BodyVoice::set_edge(float t)` forwards to its exciter. `VoiceT::set_edge(float)` is an empty inline, exactly like `set_material_character`. `SynthEngineT<V>::set_edge(float t)` stores `_edge` and forwards to every voice in `update_control` (the same place `set_material_character` is pushed) — **not** from the setter, so the push stays on the control tick.

- [ ] **Step 4: Add the BODY case to the broadcast ledger**

In `tests/test_voice_edge_broadcast.cpp`:

```cpp
TEST_CASE("edge: the trim reaches BODY")    { edge_case(ENGINE_BODY); }
```

Drive the deck at a RESO in zone 0 or 1 — at RESO ≥ 0.67 this engine is legitimately inert and the case would be measuring the blind spot.

- [ ] **Step 5: Run, prove RED, commit**

RED proof: drop the `std::pow` factor from `_recompute_filter` (the direction test fails); insert `s = _lp.process(s);` into zone 2 (the inertness test fails — which is the point of that test).

```bash
git add engine/body engine/synth/synth_engine.h tests
git commit -m "feat(body): EDGE trims the exciter's corner, and zone 2 says why it cannot"
```

---

### Task 5: EDGE on SYNTH and WAVE — the output high-pass

Spec §4.1, §4.3. One implementation, two engines, because they share `SynthEngineT` and are bit-identical by test.

**Files:**
- Modify: `engine/synth/synth_engine.h`/`.cpp` (the sum point, the HP pair, the `V::` gate)
- Modify: `engine/synth/voice.h` (the constant that says this voice wants the output HP)
- Modify: `tests/test_voice_edge_broadcast.cpp`, `tests/test_synth_engine.cpp`

**Interfaces:**
- Consumes: `OnePoleHp` and `set_edge` from Task 3.
- Produces: `VoiceT::kEdgeUsesOutputHp = true`, `BodyVoice::kEdgeUsesOutputHp = false`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("synth: EDGE at 0 is bit-identical to no EDGE at all") {
    // The whole reason EDGE is a trim rather than an absolute corner: five
    // engines' factory sound has to survive the knob's arrival untouched.
    static float a[9600], b[9600], c[9600], d[9600];
    Instrument i1, i2;
    i1.init(48000.f, 3); i2.init(48000.f, 3);
    // i1 never calls set_voice_edge; i2 calls it with 0.
    i2.set_voice_edge(PART_A, 0.f);
    render(i1, a, b, 9600); render(i2, c, d, 9600);
    for (int n = 0; n < 9600; ++n) CHECK(a[n] == c[n]);
}

TEST_CASE("synth: EDGE up removes low end and leaves the top alone") {
    // The harness is tests/test_filt.cpp's: fresh() puts the engine in
    // measurement trim (pure sine, no sub, no detune), feed() pins the lanes
    // so nothing wanders generatively, render_l() and rms() do the rest. This
    // case belongs in that file, beside FILT's own characteristic tests.
    auto level_at = [](float pitch, float edge) {
        SynthEngine e;
        fresh(e);
        feed(e, pitch);
        e.set_edge(edge);
        return rms(render_l(e, 24000), 4800);   // skip the attack
    };
    const float lo_flat = level_at(0.05f, 0.f), lo_up = level_at(0.05f, 1.f);
    const float hi_flat = level_at(0.90f, 0.f), hi_up = level_at(0.90f, 1.f);
    CHECK(lo_up < 0.7f * lo_flat);              // the bottom goes
    CHECK(hi_up > 0.95f * hi_flat);             // the top stays
}
```

The two thresholds are first values chosen so the test states a real claim
rather than a tolerance nothing can exceed. If `0.7` passes with the filter
removed, it is vacuous and must be tightened — check that during the RED proof
in Step 5, not by eye.

- [ ] **Step 2: Run and watch it fail**
- [ ] **Step 3: Implement**

`SynthEngineT<V>` gains `OnePoleHp _hp_l, _hp_r;` and, in `set_edge`, `if constexpr (V::kEdgeUsesOutputHp) { ... set_hz(neutral * pow(2, span * t)) ... }` — plus the forward to voices Task 4 added. Apply the pair at the engine's sum point in `process()`, after the voices are mixed and before whatever the engine already does to the sum. `init()` calls `reset()` on both.

The neutral corner (`kEdgeHpNeutralHz`) goes in `synth_engine.h` as a first value. **Choose it so that `t == 0` is genuinely bit-neutral** — that is what the first test measures, and it is the reason `OnePoleHp` at `a == 1` is a DC blocker rather than a bypass. If no corner is low enough, the honest fix is a bypass branch at `t == 0`, not a smaller number.

- [ ] **Step 4: Add both cases to the ledger**

```cpp
TEST_CASE("edge: the trim reaches SYNTH")   { edge_case(ENGINE_SYNTH); }
TEST_CASE("edge: the trim reaches WAVE")    { edge_case(ENGINE_WAVE); }
```

- [ ] **Step 5: Run everything**

`ctrl_identity` and `wave_formant_sweep` are byte gates over these two engines. If either moves, EDGE at 0 is not neutral — fix the engine, do not re-baseline.

- [ ] **Step 6: Commit**

```bash
git add engine/synth tests
git commit -m "feat(synth): EDGE takes the low end off SYNTH and WAVE"
```

---

### Task 6: EDGE on the sampler

Same filter, own rails, own sum point. Spec §4.3.

**Files:**
- Modify: `engine/sampler/sampler_engine.h`/`.cpp`, `engine/sampler/sampler_config.h`
- Modify: `tests/test_voice_edge_broadcast.cpp`, `tests/test_sampler_engine.cpp`

**Interfaces:** consumes `OnePoleHp`, `set_edge`. Produces `sampler_cfg::kEdgeHpNeutralHz`, `kEdgeOctaves`.

- [ ] **Step 1: Write the failing tests**

In `tests/test_sampler_engine.cpp`, on its `Rig` and its `rms()`. **Load
material first** — on an empty buffer every measurement is zero and every
assertion below passes for the wrong reason, which is vacuous-gate shape 3.

```cpp
// A crude low-band meter, built here rather than borrowed: the engine's own
// filters are what is under test, so the measurement must not share them.
static float low_energy(const std::vector<float>& v, float sr = 48000.f) {
    const float a = 1.f - std::exp(-6.2831853f * 200.f / sr);
    float y = 0.f; double acc = 0.0;
    for (float x : v) { y += a * (x - y); acc += (double)y * y; }
    return std::sqrt(acc / v.size());
}

TEST_CASE("sampler: EDGE at 0 is bit-identical to no EDGE at all") {
    Rig g1, g2;
    feed_clicks(g1, 24000, 8); feed_clicks(g2, 24000, 8);
    g2.e.set_edge(0.f);
    const auto a = g1.render(24000), b = g2.render(24000);
    for (size_t n = 0; n < a.size(); ++n) CHECK(a[n] == b[n]);
}

TEST_CASE("sampler: EDGE up thins the grain bus") {
    Rig g1, g2;
    feed_clicks(g1, 24000, 8); feed_clicks(g2, 24000, 8);
    g1.e.set_edge(0.f); g2.e.set_edge(1.f);
    const auto flat = g1.render(24000), thin = g2.render(24000);
    CHECK(low_energy(thin) < 0.7f * low_energy(flat));
    CHECK(rms(thin, 0, thin.size()) > 0.3f * rms(flat, 0, flat.size()));
}
```

The second assertion is the one that stops this from passing for the wrong
reason: a bug that simply muted the engine would satisfy the first.
- [ ] **Step 2: Run and watch it fail**
- [ ] **Step 3: Implement** — `_hp_l`/`_hp_r` applied where `_svf_l`/`_svf_r` already run, and specifically **before** them, so the two filters are in a fixed, documented order.
- [ ] **Step 4: Add the ledger case**

```cpp
TEST_CASE("edge: the trim reaches SAMPLER") { edge_case(ENGINE_SAMPLER); }
```

- [ ] **Step 5: Run everything, prove RED, commit**

```bash
git add engine/sampler tests
git commit -m "feat(sampler): EDGE takes the low end off the grain bus"
```

---

### Task 7: EDGE on the BBD — pre-emphasis at the input

Spec §4.5. The only cell that shapes what *arrives* rather than how it decays.

**Files:**
- Modify: `engine/parts/bbd_engine.h`/`.cpp` (`process_in`, a `set_edge`)
- Modify: `tests/test_bbd_engine.cpp`, `tests/test_voice_edge_broadcast.cpp`

**Interfaces:** consumes `OnePoleHp`, `set_edge`. Produces `BbdEngine::set_edge`.

- [ ] **Step 1: Write the failing tests**

In `tests/test_bbd_engine.cpp`, on whatever rig that file already uses to push
audio into a `BbdEngine` (it is voiceless and input-consuming, so it is fed
through `process_in`, not by notes).

```cpp
TEST_CASE("bbd: EDGE at 0 leaves the line exactly as it was") {
    // bit equality between an engine that never hears set_edge and one that
    // hears set_edge(0.f) -- same rig, same input, same seed.
}

TEST_CASE("bbd: EDGE shapes what ARRIVES, not how it decays") {
    // The distinguishing test, and the reason this cell is not redundant with
    // FILT (the loss pole) or RES (the feedback tilt): EDGE must change the
    // FIRST pass. Feed one burst, let it circulate, and split the output at
    // the delay time into pass 1 and pass 4.
    const int d = delay_samples();          // from the rig's clock/stage state
    auto pass = [&](float edge, int n) {
        auto v = render_burst(edge);        // one burst, then silence
        return low_energy({v.begin() + n * d, v.begin() + (n + 1) * d});
    };
    // Pass 1 must move: that is the claim.
    CHECK(pass(1.f, 0) < 0.8f * pass(0.f, 0));
    // And it must not move merely because the whole line got quieter -- if the
    // ratio at pass 4 equals the ratio at pass 1, EDGE is behaving like a loss
    // pole and this cell IS redundant. That is a design failure to report, not
    // a threshold to widen.
    const float r1 = pass(1.f, 0) / pass(0.f, 0);
    const float r4 = pass(1.f, 3) / pass(0.f, 3);
    CHECK(r1 != doctest::Approx(r4).epsilon(0.05));
}
```

`low_energy` is the same crude 200 Hz meter Task 6 defines; if both files need
it, put it in one place rather than copying it twice.

- [ ] **Step 2: Run and watch it fail**
- [ ] **Step 3: Implement** — a `OnePoleHp` per channel in `process_in`, ahead of `_in_gain` and the line. Neutral is flat; use the same "bit-neutral at 0 or an explicit bypass" rule as Task 5.
- [ ] **Step 4: Add the ledger case**

```cpp
TEST_CASE("edge: the trim reaches BBD")     { edge_case(ENGINE_BBD); }
```

The BBD is voiceless and input-consuming: `edge_case`'s helper must feed it audio, not notes. Check `consumes_input()` and follow whatever `tests/test_bbd_engine.cpp` already does.

- [ ] **Step 5: Run, prove RED, commit**

```bash
git add engine/parts tests
git commit -m "feat(bbd): EDGE pre-emphasises what enters the line"
```

---

### Task 8: The panel closes — captions, plate, and the neutral gate

**Files:**
- Modify: `host/vcv/res/gen_panel.py` (`DYNAMIC_CAPTIONS` row for `DAMP`)
- Modify: `host/vcv/res/test_panel.py` (rewrite the shipped-defaults gate)
- Modify: `host/vcv/res/test_hw_panel.py` if the plate check needs it

- [ ] **Step 1: Write the failing gate**

Replace `test_feed_shipped_defaults_are_the_engine_constants` with a gate that asserts what §4.2 actually promises — **the neutral is the engine constant**, one assertion per engine, each read from that engine's own header:

```python
def test_edge_neutral_is_every_engine_s_own_constant():
    """EDGE boots at centre and centre means 'unchanged' on all six engines.

    This replaces the FEED-only default gate. The old one derived one knob
    position from one constant; that shape cannot survive six engines with six
    neutrals, which is exactly the defect that made EDGE a trim (spec 4.2).
    What is checkable instead: the knob's init default is the centre, the
    centre is 0, and every engine's neutral is a named constant in its own
    header rather than a literal in the host.
    """
    check(g.INIT_DEFAULTS["DAMP_A"] == 0.0 and g.INIT_DEFAULTS["DAMP_B"] == 0.0,
          "EDGE must boot at the trim's centre")
    for header, name in (("engine/feed/feed_config.h",       "kEdgeOctaves"),
                         ("engine/sampler/sampler_config.h", "kEdgeOctaves"),
                         ("engine/synth/synth_engine.h",     "kEdgeOctaves")):
        check(_read_float(header, name) is not None,
              f"{name} must be a named constant in {header}, not a host literal")
```

`_read_float` is the existing helper pattern from `_feed_cfg_floats()`; generalise that function rather than adding a second parser.

- [ ] **Step 2: Run it and watch it fail**
- [ ] **Step 3: Add the caption row**

```python
    ("DAMP",     "ENGINE",   ("EDGE", "EDGE", "EDGE",  "SNAP",  "PRE",  "EDGE")),
```

The `Ctl` labels for `DAMP_A/B` stay `"EDGE"` — already equal to `words[0]`, so the plate does not move.

- [ ] **Step 4: Regenerate, run both guards, prove RED**

RED proofs: set `INIT_DEFAULTS["DAMP_A"]` to 0.2; change `SNAP` to `DAMP` (BODY's `DECAY` prints `DAMP` — the printed-word gate must catch it).

- [ ] **Step 5: Build and install the plugin**

```
host/vcv/build-local.sh install
```

- [ ] **Step 6: Commit**

```bash
git add host/vcv
git commit -m "feat(panel): EDGE and DPTH print six words each"
```

---

### Task 9: The documents catch up

**Files:** `docs/engine-map.md`, `docs/by-ear-decisions.md`, `docs/gotchas.md`, `docs/roadmap.md`

- [ ] **Step 1: `engine-map.md`** — the per-engine `LANE_MOTION` table (it is a lane fact and belongs in the map), EDGE's neutral per engine, and BODY's zone-2 blind spot.
- [ ] **Step 2: `by-ear-decisions.md`** — every first value this plan introduces: each engine's `kEdgeOctaves` and HP neutral, `kMotionBaseScale`, BODY's `kDriftDetuneCt = 3.f` ceiling (spec §9 item 6: a full DPTH sweep on BODY buys ±3 cents, which may be too small to be worth a quarter of the VOICE row), and DPTH at the top of its travel on a BBD deck (spec §3.4: `1.2 / bbd_drive_gain(DRIVE)`, above unity before the loss pole — reachable through MOD today, but never before under one finger).
- [ ] **Step 3: `gotchas.md`** — two entries: EDGE does nothing in BODY's top third of RESO, and the sampler reads MOTION's base halved.
- [ ] **Step 4: `roadmap.md`** — close the "FEED knob re-pointing" entry, and state what is still owed: the listening pass, and the bench.
- [ ] **Step 5: Commit** — `docs: DPTH and EDGE, on six engines`

---

### Task 10: Bench, then hand over to the ears

**Files:** `bench/workloads_system.cpp`, `bench/run.py`, `docs/bench/`

- [ ] **Step 1: Price the new filters.** Four one-poles per deck on the linear engines and one per channel on the BBD is a claim about the design, not the chip. Add or extend a system row that runs a SYNTH deck and a BBD deck with EDGE off-neutral, and run it on the board. Read `bench/README.md` first — and note the clean-tree guard: `run.py` refuses a dirty tree and its own output dirties it, so a sweep costs one commit per point.
- [ ] **Step 2: Record the result** in `docs/bench/`, and only then write any CPU figure into a document.
- [ ] **Step 3: Hand over the listening pass.** Everything in spec §9 is now turnable by hand in Rack. The by-ear ledger already names them; this step is the message that says they are ready, not a change.

---

## Notes for whoever executes this

**The two things most likely to go wrong:**

1. **A neutral that is not neutral.** Every engine task has a bit-equality test at `t == 0` as its *first* test, on purpose. `OnePoleHp` at its lowest corner is a DC blocker, not a bypass — if bit-equality fails, add an explicit bypass branch rather than lowering the corner until the difference hides under a tolerance. A tolerance that nothing can exceed is a vacuous gate.
2. **A stub that stays a stub.** Task 3 puts five one-line `set_edge` stubs into the engines so the broadcast line can exist. Tasks 4–7 replace them. `tests/test_voice_edge_broadcast.cpp` is the ledger that says which are real: it must end this plan with six cases, one per engine.

**What is deliberately not here:** the octave spans and neutral corners are first values, chosen so the tests can run, and the listening pass owns them (spec §8, §9). Do not tune by eye — pick something plausible, make the gates green, and hand the knob over.
