# SWARM BREATH Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give SWARM a slow lane, so modulation makes it breathe instead of shifting it as a block.

**Architecture:** Three control-side mechanisms plus a time base for DRIFT. The normalizer's gain gets a long slew so spectral modulation produces a real swell; the bloom-stagger ring is generalized to the modulation lanes so a coherent LFO becomes a wave travelling up the spectrum; the stereo image gains a per-partial walk; and MOTION becomes a breath-to-shimmer axis. `SwarmBank` is untouched.

**Tech Stack:** C++17, clang + Ninja, doctest.

**Spec:** `docs/superpowers/specs/2026-08-17-swarm-character-and-breath-design.md` (§7)

**Runs after:** `docs/superpowers/plans/2026-08-17-swarm-character.md`. Task 1 below is the audio gate that plan's self-review lists as its known gap.

## Global Constraints

Identical to the character plan's, and they apply in full:

- **Build:** `source env.sh`, then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`. Release is **not optional**.
- **Never `cd`** as a shell command prefix.
- **Everything written into the repo is English.**
- **Commit trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- **Nothing may assume the value of `swarm_cfg::kPartials`.**
- **No bit-exactness gates.** Renders are sanity checks.
- **A test that cannot go red gets fixed.** Every gate has a watch-it-fail step.
- **`SwarmBank` (`engine/swarm/swarm_bank.h`) is not modified by any task in this plan.**

---

### Task 1: The audio gate — no character is ever silent

Gate G-K from the spec, and the character plan's known gap. It comes first here because every later task in this plan changes level behaviour, and a silence gate is worth having in place before that starts. `fireflow-choke-silences-a-deck` is the precedent: a zoned control that muted a whole deck shipped once.

**Files:**
- Test: `tests/test_swarm_engine.cpp`

**Interfaces:**
- Consumes: `set_character`, `zone_centre_for_test` from the character plan.
- Produces: nothing but confidence.

- [ ] **Step 1: Write the gate**

```cpp
TEST_CASE("swarm G38: no character is silent at any voicing setting") {
    // The CHOKE lesson (fireflow-choke-silences-a-deck): a zoned control
    // shipped once that muted a whole deck on almost every terrain. This
    // sweeps the corners of the voicing space in every character.
    for (int c = 0; c < swarm_cfg::kCharZones; ++c) {
        for (float harm : {0.f, 0.5f, 1.f}) {
            for (float focus : {0.f, 0.5f, 1.f}) {
                for (float bal : {-1.f, 0.f, 1.f}) {
                    SwarmEngine e = fresh_swarm();
                    e.set_character(zone_centre_for_test(c));
                    feed(e, 1.f / 3.f, 0.5f, focus, 0.f, 1.f);
                    e.set_harm(harm);
                    e.set_balance(bal);
                    e.set_sub(0.3f);
                    e.set_rise(0.01f);
                    e.set_fall(1.f);           // drone: the top of FALL
                    e.set_flow(true);
                    double peak = 0.0;
                    for (int i = 0; i < 48000 * 3; ++i) {
                        float l, r; e.process(l, r);
                        peak = std::max(peak, double(std::fabs(l)) );
                    }
                    const double db = peak > 1e-12
                        ? 20.0 * std::log10(peak) : -240.0;
                    INFO("character ", c, " harm ", harm, " focus ", focus,
                         " bal ", bal, " peak ", db, " dBFS");
                    CHECK(db > -60.0);
                }
            }
        }
    }
}
```

- [ ] **Step 2: Run it**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G38"`

Expected: it may already PASS. **That is not good enough** — a gate that cannot go red gets fixed (`fireflow-tests-must-be-able-to-fail`). Prove the RED once by temporarily returning `0.f` from `_focus_weight` for the narrow case, watching the gate fail, then reverting. Record in the commit message that the RED was proven and how.

- [ ] **Step 3: Commit**

```bash
git add tests/test_swarm_engine.cpp
git commit -m "test(swarm): a silence gate across every character and corner"
```

---

### Task 2: The normalizer learns to lag

Measured defect (spec §1.2): the level moves 0.57 dB across the entire TILT lane, because `_normalize_power()` re-pins total power at every control tick. Slewing its gain separates a slow knob turn (stays level-matched) from an LFO (breathes).

**Files:**
- Modify: `engine/swarm/swarm_config.h`, `engine/swarm/swarm_engine.h`, `engine/swarm/swarm_engine.cpp:221-231`
- Test: `tests/test_swarm_engine.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `swarm_cfg::kNormSlewS = 0.7f`; `_norm_slew` (a `OnePole`) inside `SwarmEngine`.

- [ ] **Step 1: Write the failing tests**

```cpp
namespace {
// RMS in dBFS of a window at the end of a run in which `drive` is called once
// per sample to move whatever lane the caller wants moved.
template <typename F>
double swarm_rms_db(F drive, int seconds, int win_seconds) {
    SwarmEngine e = fresh_swarm();
    e.set_character(0.f);
    e.set_rise(0.02f);
    e.set_fall(1.f);                  // drone
    e.set_flow(true);
    feed(e, 1.f / 3.f);
    const int total = 48000 * seconds, win = 48000 * (seconds - win_seconds);
    double acc = 0.0; int n = 0;
    for (int i = 0; i < total; ++i) {
        drive(e, i);
        float l, r; e.process(l, r);
        if (i >= win) { acc += 0.5 * (double(l)*l + double(r)*r); ++n; }
    }
    const double v = n ? std::sqrt(acc / n) : 0.0;
    return v > 1e-12 ? 20.0 * std::log10(v) : -240.0;
}
}  // namespace

TEST_CASE("swarm G39: a modulated lane produces a real swell") {
    // Measured before this change: 0.57 dB across the WHOLE TILT travel, i.e.
    // a full-depth LFO was inaudible as a level movement (spec section 1.2).
    // Peak-to-trough of a 0.3 Hz sweep, sampled at its extremes.
    auto at = [](float tilt) {
        return swarm_rms_db([tilt](SwarmEngine& e, int) {
            const float t[LANE_COUNT] = { tilt, 0.5f, 1.f/3.f, 0.f, 1.f };
            e.set_targets(t, 0.f);
        }, 4, 1);
    };
    // A lane that moves FAST compared to kNormSlewS must swing the level.
    double lo = 1e9, hi = -1e9;
    for (int k = 0; k < 8; ++k) {
        // Drive the lane as a square between the extremes, one full period
        // every 0.5 s -- well inside the slew's memory.
        const double d = swarm_rms_db([k](SwarmEngine& e, int i) {
            const float ph = float((i / 12000) % 2);
            const float t[LANE_COUNT] = { ph, 0.5f, 1.f/3.f, 0.f, 1.f };
            e.set_targets(t, 0.f);
        }, 4, 1);
        lo = std::min(lo, d); hi = std::max(hi, d);
    }
    // The gate itself: measure the instantaneous spread inside one period.
    SwarmEngine e = fresh_swarm();
    e.set_character(0.f);
    e.set_rise(0.02f); e.set_fall(1.f); e.set_flow(true);
    feed(e, 1.f / 3.f);
    for (int i = 0; i < 48000 * 2; ++i) { float l, r; e.process(l, r); }
    double mn = 1e9, mx = -1e9;
    for (int i = 0; i < 48000 * 4; ++i) {
        const float ph = float((i / 12000) % 2);
        const float t[LANE_COUNT] = { ph, 0.5f, 1.f/3.f, 0.f, 1.f };
        e.set_targets(t, 0.f);
        float l, r; e.process(l, r);
        // Block RMS over 1024 samples, tracked as an envelope.
        static double acc = 0.0; static int n = 0;
        acc += 0.5 * (double(l)*l + double(r)*r); ++n;
        if (n == 1024) {
            const double v = 20.0 * std::log10(std::sqrt(acc / n) + 1e-12);
            mn = std::min(mn, v); mx = std::max(mx, v);
            acc = 0.0; n = 0;
        }
    }
    CHECK(mx - mn > 3.0);        // it must actually swell
}

TEST_CASE("swarm G40: a STATIC setting is still level-matched") {
    // The guard on G39. Delete the normalizer instead of slewing it and this
    // goes to 20+ dB; prove that RED once by stubbing _normalize_power out.
    double mn = 1e9, mx = -1e9;
    for (int i = 0; i <= 10; ++i) {
        const float tilt = i * 0.1f;
        const double d = swarm_rms_db([tilt](SwarmEngine& e, int) {
            const float t[LANE_COUNT] = { tilt, 0.5f, 1.f/3.f, 0.f, 1.f };
            e.set_targets(t, 0.f);
        }, 6, 1);
        mn = std::min(mn, d); mx = std::max(mx, d);
    }
    CHECK(mx - mn < 1.5);
}
```

- [ ] **Step 2: Run them and watch G39 fail**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G39"`
Expected: FAIL — the spread is well under 3 dB, because the normalizer re-pins power every tick.

G40 should PASS already. Prove its RED by stubbing `_normalize_power` to return immediately, watch it fail, revert.

- [ ] **Step 3: Add the constant**

```cpp
// How slowly the power normalizer's gain follows the spectrum (spec section
// 7.1). This is what separates a knob turn from an LFO: turning TILT slowly
// stays level-matched because the slew catches up, while a 0.3 Hz lane
// produces a real swell because it does not. By ear.
constexpr float kNormSlewS = 0.7f;
```

- [ ] **Step 4: Slew the gain**

In `swarm_engine.h`, add `OnePole _norm_slew;` beside `_level`. In `init()`:

```cpp
    _norm_slew.init(sample_rate, swarm_cfg::kNormSlewS);
    _norm_slew.reset(1.f);
```

In `_normalize_power()`:

```cpp
// Total power is normalized, but the gain that does it LAGS (spec section
// 7.1). Measured before the lag: the level moved 0.57 dB across the entire
// TILT lane, so every swell a modulated FOCUS or TILT could have produced was
// removed in the same control tick that produced it.
//
// The lag is what makes the normalizer keep its job -- no setting is a volume
// control once the slew has settled -- while modulation regains its dynamic.
void SwarmEngine::_normalize_power() {
    float sum = 0.f;
    for (int i = 0; i < swarm_cfg::kPartials; ++i) sum += _t_amp[i] * _t_amp[i];
    // Nothing audible: leave the zeros EXACTLY zero rather than dividing by
    // one. G7 in tests/test_swarm_bank.cpp is what makes that worth
    // protecting. The slew is NOT advanced here -- an inaudible spectrum must
    // not drag the gain toward a number it will never use.
    if (sum < 1e-12f) return;
    const float g = _norm_slew.process(1.f / std::sqrt(sum));
    for (int i = 0; i < swarm_cfg::kPartials; ++i) _t_amp[i] *= g;
}
```

`_normalize_power` is called once per control tick, so the `OnePole`'s rate must be expressed against the control raster, not the sample rate. If `OnePole::init` takes a sample rate, pass `sample_rate / kCtrlInterval`. Read `engine/util/onepole.h` before wiring it and match what it actually expects.

- [ ] **Step 5: Run the tests**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G39,swarm G40"`
Expected: both PASS.

Then: `ctest --test-dir build --output-on-failure`. G9, G10, G14, G15 and G26 all read settled amplitudes; if any fails, it is because it reads them before the slew has settled — extend its settle loop, do not loosen its assertion.

- [ ] **Step 6: Commit**

```bash
git add engine/swarm/swarm_config.h engine/swarm/swarm_engine.h engine/swarm/swarm_engine.cpp tests/test_swarm_engine.cpp
git commit -m "feat(swarm): the normalizer lags, so modulation can swell"
```

---

### Task 3: The spectral stagger — a modulation wave, not a block shift

SWARM already owns this mechanism for the envelope: `_env_ring` plus `_apply_stagger` (`swarm_engine.cpp:349-363`). Generalize it so the SOURCE and SIZE lanes are read per partial at a rising lag.

**Files:**
- Modify: `engine/swarm/swarm_config.h`, `engine/swarm/swarm_engine.h`, `engine/swarm/swarm_engine.cpp`
- Test: `tests/test_swarm_engine.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `swarm_cfg::kModStaggerS = 0.9f`, `kModRingDecim = 4`, `kModRing`; `float SwarmEngine::_lane_at(int lane, int partial) const`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("swarm G41: a lane step reaches the top partial later than the bottom") {
    // Measured before this change: modulation hit all partials coherently, so
    // an LFO was a block shift with zero lag (spec section 1.2). The bloom
    // stagger already proves the mechanism; this is it on the mod lanes.
    SwarmEngine e = fresh_swarm();
    e.set_character(0.f);
    e.set_rise(0.02f); e.set_fall(1.f); e.set_flow(true);
    // MOTION scales the stagger, so it has to be up for the lag to exist.
    feed(e, 1.f / 3.f, 0.f, 0.5f, 1.f, 1.f);
    for (int i = 0; i < 48000; ++i) { float l, r; e.process(l, r); }

    const int lo = swarm_cfg::kSubPartials;
    const int hi = swarm_cfg::kPartials - 1;
    const float lo0 = e.target_amp_for_test(lo);
    const float hi0 = e.target_amp_for_test(hi);

    // Step TILT from 0 to 1 and find when each end first moves by 10 %.
    feed(e, 1.f / 3.f, 1.f, 0.5f, 1.f, 1.f);
    int lo_tick = -1, hi_tick = -1;
    for (int t = 0; t < 400; ++t) {
        for (int i = 0; i < swarm_cfg::kCtrlInterval; ++i) {
            float l, r; e.process(l, r);
        }
        if (lo_tick < 0 &&
            std::fabs(e.target_amp_for_test(lo) - lo0) > 0.1f * lo0)
            lo_tick = t;
        if (hi_tick < 0 &&
            std::fabs(e.target_amp_for_test(hi) - hi0) > 0.1f * hi0)
            hi_tick = t;
    }
    REQUIRE(lo_tick >= 0);
    REQUIRE(hi_tick >= 0);
    CHECK(hi_tick > lo_tick);       // the wave travels
}

TEST_CASE("swarm G42: at MOTION 0 the stagger is off and every partial is in step") {
    SwarmEngine e = fresh_swarm();
    e.set_character(0.f);
    e.set_rise(0.02f); e.set_fall(1.f); e.set_flow(true);
    feed(e, 1.f / 3.f, 0.f, 0.5f, 0.f, 1.f);      // MOTION 0
    for (int i = 0; i < 48000; ++i) { float l, r; e.process(l, r); }
    for (int p = 0; p < swarm_cfg::kPartials; ++p)
        CHECK(e.lane_at_for_test(LANE_SOURCE, p) == doctest::Approx(0.f));
}
```

- [ ] **Step 2: Run them and watch G41 fail**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G41"`
Expected: FAIL — `hi_tick == lo_tick`, because every partial reads the same lane value.

- [ ] **Step 3: Add the constants**

```cpp
// The modulation stagger (spec section 7.2): the same mechanism as the bloom
// stagger, on the SOURCE and SIZE lanes. A coherent LFO becomes a wave
// travelling up the spectrum instead of a block shift.
//
// The bloom ring spans 12 ms; this one wants up to 0.9 s, so it is decimated
// -- one sample every kModRingDecim control ticks. At 48 kHz that is an 8 ms
// lag resolution, far finer than the effect needs, for 250 floats per lane
// instead of 1000.
constexpr float kModStaggerS = 0.9f;
constexpr int kModRingDecim = 4;
constexpr int kModRing =
    static_cast<int>(kModStaggerS * kStaggerMaxSr /
                     (kCtrlInterval * kModRingDecim)) + 2;
constexpr int kModLanes = 2;        // LANE_SOURCE and LANE_SIZE
```

- [ ] **Step 4: Implement the ring**

In `swarm_engine.h`:

```cpp
    // One decimated ring per staggered lane, and the read head. The bloom
    // stagger's ring (_env_ring) is the same idea at a much shorter span; the
    // two are kept separate because their spans differ by two orders of
    // magnitude and sharing one would size the cheap ring for the expensive
    // one.
    float _mod_ring[swarm_cfg::kModLanes][swarm_cfg::kModRing] = {};
    int   _mod_head = 0;
    int   _mod_decim = 0;
```

and under `SPKY_TESTING`:

```cpp
    float lane_at_for_test(int lane, int partial) const {
        return _lane_at(lane, partial);
    }
```

In `swarm_engine.cpp`:

```cpp
// The lane value partial `p` sees: not the value now, but the value as it was
// at p's own lag. Lag rises with partial index, so a step on a lane sweeps up
// the spectrum instead of arriving everywhere at once (spec section 7.2).
//
// Scaled by MOTION, so a patch with DRIFT down is completely unaffected --
// which is what keeps this from changing every existing swarm sound.
float SwarmEngine::_lane_at(int lane, int p) const {
    const int idx = lane == LANE_SOURCE ? 0 : 1;
    const float depth = clampf(_targets[LANE_MOTION], 0.f, 1.f);
    const int span = static_cast<int>(
        swarm_cfg::kModStaggerS * depth * _sr /
        static_cast<float>(kCtrlInterval * swarm_cfg::kModRingDecim));
    const int max_lag = span < swarm_cfg::kModRing - 1
        ? span : swarm_cfg::kModRing - 1;
    const int lag = swarm_cfg::kPartials > 1
        ? (p * max_lag) / (swarm_cfg::kPartials - 1) : 0;
    const int at = (_mod_head - lag + 2 * swarm_cfg::kModRing) %
                   swarm_cfg::kModRing;
    return _mod_ring[idx][at];
}
```

Write the ring at the top of `_control_tick`, before `_rebuild_targets`:

```cpp
    if (++_mod_decim >= swarm_cfg::kModRingDecim) {
        _mod_decim = 0;
        _mod_head = (_mod_head + 1) % swarm_cfg::kModRing;
        _mod_ring[0][_mod_head] = _targets[LANE_SOURCE];
        _mod_ring[1][_mod_head] = _targets[LANE_SIZE];
    }
```

In `_rebuild_targets`, the tilt and the focus become per-partial reads. `tilt` moves inside the loop:

```cpp
        const float tilt_p = lerpf(swarm_cfg::kTiltDark, swarm_cfg::kTiltBright,
                                   clampf(_lane_at(LANE_SOURCE, slot), 0.f, 1.f));
        float a = std::pow(n, -tilt_p);
```

and `_focus_weight` takes the staggered SIZE value as a second argument rather than reading `_targets[LANE_SIZE]` itself. Change its signature to `_focus_weight(float hz, float size)` and pass `_lane_at(LANE_SIZE, slot)`. Do the same for `_vowel_weight`.

`init()` must fill both rings with the boot lane values, or the first second of every patch is a sweep from zero:

```cpp
    for (int i = 0; i < swarm_cfg::kModRing; ++i) {
        _mod_ring[0][i] = _targets[LANE_SOURCE];
        _mod_ring[1][i] = _targets[LANE_SIZE];
    }
```

- [ ] **Step 5: Run the tests**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G41,swarm G42"`
Expected: both PASS.

Then: `ctest --test-dir build --output-on-failure`. G10 (TILT walks the energy) and G15 (FOCUS) both drive these lanes; both run at MOTION 0 in their current form, where the stagger is off by construction, so both must still pass unchanged. If either fails, the MOTION scaling is wrong — fix that, not the gate.

- [ ] **Step 6: Commit**

```bash
git add engine/swarm/swarm_config.h engine/swarm/swarm_engine.h engine/swarm/swarm_engine.cpp tests/test_swarm_engine.cpp
git commit -m "feat(swarm): modulation travels up the spectrum now"
```

---

### Task 4: The stereo image moves

**Files:**
- Modify: `engine/swarm/swarm_config.h`, `engine/swarm/swarm_engine.h`, `engine/swarm/swarm_engine.cpp`
- Test: `tests/test_swarm_engine.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `_pan_walk[swarm_cfg::kPartials]`, `swarm_cfg::kPanWalkMax = 0.5f`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("swarm G43: the stereo image moves, and WIDTH still gates it") {
    SUBCASE("at WIDTH 0 the image is dead centre and stays there") {
        SwarmEngine e = fresh_swarm();
        e.set_character(0.f);
        e.set_width(0.f);
        e.set_rise(0.02f); e.set_fall(1.f); e.set_flow(true);
        feed(e, 1.f / 3.f, 0.5f, 0.5f, 1.f, 1.f);
        for (int i = 0; i < 48000 * 4; ++i) {
            float l, r; e.process(l, r);
            CHECK(l == doctest::Approx(r).epsilon(0.001f));
        }
    }
    SUBCASE("at WIDTH 1 with DRIFT up, the pan of a partial actually changes") {
        SwarmEngine e = fresh_swarm();
        e.set_character(0.f);
        e.set_width(1.f);
        e.set_rise(0.02f); e.set_fall(1.f); e.set_flow(true);
        feed(e, 1.f / 3.f, 0.5f, 0.5f, 1.f, 1.f);
        const int slot = swarm_cfg::kSubPartials + 3;
        float mn = 1e9f, mx = -1e9f;
        for (int t = 0; t < 2000; ++t) {
            for (int i = 0; i < swarm_cfg::kCtrlInterval; ++i) {
                float l, r; e.process(l, r);
            }
            const float p = e.target_pan_for_test(slot);
            mn = std::min(mn, p); mx = std::max(mx, p);
        }
        CHECK(mx - mn > 0.05f);
    }
}
```

`target_pan_for_test(int)` goes beside the other observers under `SPKY_TESTING`.

- [ ] **Step 2: Run it and watch the second subcase fail**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G43"`
Expected: FAIL — `_t_pan[slot]` is a static ramp, so `mx - mn` is 0.

- [ ] **Step 3: Add the constant**

```cpp
// How far the per-partial pan walk reaches at WIDTH 1, in pan units. The
// stereo image was frozen before this (spec section 7.3): _t_pan was a static
// ramp over the slot index and nothing ever moved it. By ear.
constexpr float kPanWalkMax = 0.5f;
```

- [ ] **Step 4: Add the walk**

In `swarm_engine.h`, beside `_det_walk` and `_amp_walk`:

```cpp
    float _pan_walk[swarm_cfg::kPartials] = {};
```

Zero it in `reseed()` beside the other two. In `_advance_drift(i)`, add a third walk on the same stream and the same bounds:

```cpp
    _pan_walk[i] += step * _drift_rng[i].next_bipolar()
                  - swarm_cfg::kDriftPull * _pan_walk[i];
    _pan_walk[i] = clampf(_pan_walk[i], -1.f, 1.f);
    // Applied in _rebuild_targets, not here: _t_pan is rewritten every tick
    // and this function runs only on the partial's own retarget slice.
```

In `_rebuild_targets`, after the static ramp assignment:

```cpp
        // The image moves. WIDTH gates it exactly as it gates the ramp, so a
        // mono patch stays mono (G43's first subcase).
        _t_pan[slot] = clampf(_t_pan[slot] +
                              _width * swarm_cfg::kPanWalkMax * _pan_walk[slot],
                              -1.f, 1.f);
```

- [ ] **Step 5: Run the tests**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G43"`
Expected: both subcases PASS.

Then: `ctest --test-dir build --output-on-failure`. Check the FLUX mono decision in `fireflow-by-ear-decisions` is not affected — it is about the FX bus, not this, but confirm nothing in `test_deck_bus.cpp` reads swarm pan.

- [ ] **Step 6: Commit**

```bash
git add engine/swarm/swarm_config.h engine/swarm/swarm_engine.h engine/swarm/swarm_engine.cpp tests/test_swarm_engine.cpp
git commit -m "feat(swarm): the stereo image stopped standing still"
```

---

### Task 5: MOTION becomes breath-to-shimmer

Measured defect (spec §1.2): the drift reverses direction every 18–27 ms at **every** depth, so it is a rough random FM and never a breath. MOTION gains a time base.

**Files:**
- Modify: `engine/swarm/swarm_config.h`, `engine/swarm/swarm_engine.cpp:57-73`
- Test: `tests/test_swarm_engine.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `swarm_cfg::kDriftStepSlow`, `kDriftStepFast`, `kDriftPullSlow`, `kDriftPullFast`; `kDriftWalkStep` and `kDriftPull` are deleted.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("swarm G44: MOTION's lower half breathes, its upper half shimmers") {
    // Measured before this change: 0.027 s between direction reversals at
    // MOTION 0.25 and 0.018 s at 1.0 -- 40 to 55 reversals per SECOND at every
    // depth, i.e. no slow motion existed anywhere in the engine (spec 1.2).
    //
    // Reversals are counted on the BANK frequency, not the target: the target
    // array is rebuilt every tick and only carries drift on the partial's own
    // retarget slice, so sampling it manufactures one reversal per tick.
    auto reversal_seconds = [](float motion) {
        SwarmEngine e = fresh_swarm();
        e.set_character(0.f);
        e.set_rise(0.02f); e.set_fall(1.f); e.set_flow(true);
        feed(e, 1.f / 3.f, 0.5f, 0.5f, motion, 1.f);
        const int slot = swarm_cfg::kSubPartials + 4;
        const int ticks = 60 * 48000 / swarm_cfg::kCtrlInterval;
        float prev = 0.f, prev_d = 0.f;
        int reversals = 0; bool first = true;
        for (int t = 0; t < ticks; ++t) {
            for (int i = 0; i < swarm_cfg::kCtrlInterval; ++i) {
                float l, r; e.process(l, r);
            }
            const float hz = e.partial_hz_for_test(slot);
            if (first) { prev = hz; first = false; continue; }
            const float d = hz - prev;
            if (d * prev_d < 0.f) ++reversals;
            if (d != 0.f) prev_d = d;
            prev = hz;
        }
        return reversals > 0 ? 60.f / float(reversals) : 60.f;
    };

    SUBCASE("the bottom of MOTION is a breath") {
        CHECK(reversal_seconds(0.15f) > 0.3f);
    }
    SUBCASE("the top of MOTION is still a shimmer") {
        CHECK(reversal_seconds(1.0f) < 0.1f);
    }
    SUBCASE("MOTION 0 is still exactly static") {
        // G27's invariant, restated here because this task is the one that
        // could break it.
        SwarmEngine e = fresh_swarm();
        e.set_character(0.f);
        e.set_rise(0.02f); e.set_fall(1.f); e.set_flow(true);
        feed(e, 1.f / 3.f, 0.5f, 0.5f, 0.f, 1.f);
        for (int i = 0; i < 48000; ++i) { float l, r; e.process(l, r); }
        const float a = e.partial_hz_for_test(swarm_cfg::kSubPartials + 4);
        for (int i = 0; i < 48000 * 3; ++i) { float l, r; e.process(l, r); }
        const float b = e.partial_hz_for_test(swarm_cfg::kSubPartials + 4);
        CHECK(a == doctest::Approx(b));
    }
}
```

- [ ] **Step 2: Run it and watch the first subcase fail**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G44"`
Expected: FAIL on "the bottom of MOTION is a breath" — it measures roughly 0.027 s, two orders of magnitude short of 0.3 s.

- [ ] **Step 3: Replace the constants**

Delete `kDriftWalkStep`, `kDriftPull` and `kDriftRateDepthBoost`, and add:

```cpp
// DRIFT's time base (spec section 7.4). MOTION is a breath -> shimmer axis
// now, not pure depth: it was measured reversing direction every 18-27 ms at
// EVERY depth, which is a rough random FM and never a breath.
//
// A bounded walk's correlation time is set by its pull: the reversal interval
// scales with the step-to-pull ratio. Slow end: small steps, weak pull, so the
// walk wanders for seconds. Fast end: today's numbers, which are a usable
// shimmer once they are a CHOICE rather than the only option.
constexpr float kDriftStepSlow = 0.006f, kDriftStepFast = 0.06f;
constexpr float kDriftPullSlow = 0.0015f, kDriftPullFast = 0.02f;

// Depth still scales the EXCURSION, and still multiplies at the very end so
// MOTION 0 is exactly static (G27, G44's third subcase).
constexpr float kDriftCentsMax = 18.f;      // unchanged
constexpr float kDriftAmpMax = 0.35f;       // unchanged
```

- [ ] **Step 4: Rework the walk**

In `_advance_drift`:

```cpp
void SwarmEngine::_advance_drift(int i) {
    const float depth = clampf(_targets[LANE_MOTION], 0.f, 1.f);
    // MOTION drives the RATE across its whole travel, and the depth scaling
    // below is what still makes 0 exactly static.
    const float step = lerpf(swarm_cfg::kDriftStepSlow,
                             swarm_cfg::kDriftStepFast, depth);
    const float pull = lerpf(swarm_cfg::kDriftPullSlow,
                             swarm_cfg::kDriftPullFast, depth);
    _det_walk[i] += step * _drift_rng[i].next_bipolar() - pull * _det_walk[i];
    _amp_walk[i] += step * _drift_rng[i].next_bipolar() - pull * _amp_walk[i];
    _pan_walk[i] += step * _drift_rng[i].next_bipolar() - pull * _pan_walk[i];
    _det_walk[i] = clampf(_det_walk[i], -1.f, 1.f);
    _amp_walk[i] = clampf(_amp_walk[i], -1.f, 1.f);
    _pan_walk[i] = clampf(_pan_walk[i], -1.f, 1.f);
    // Depth multiplies at the END, so DRIFT 0 is exactly static (G27) while
    // the walk itself stays free-running -- turning DRIFT up does not restart
    // the motion.
    const float cents = _det_walk[i] * swarm_cfg::kDriftCentsMax * depth;
    _t_hz[i] *= std::pow(2.f, cents * (1.f / 1200.f));
    _t_amp[i] *= 1.f + _amp_walk[i] * swarm_cfg::kDriftAmpMax * depth;
}
```

A slow walk with a weak pull reaches further before it turns, so the excursion at the slow end grows even though `kDriftCentsMax` is unchanged. That is the intent — a breath is wide and slow. If G44's slow subcase passes but the pitch wobble is audible as detuning in the listening session, `kDriftCentsMax` is the knob to lower, not the pull.

- [ ] **Step 5: Run the tests**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G44"`
Expected: all three subcases PASS.

Then: `ctest --test-dir build --output-on-failure`. **G27 must still pass** — it is the same invariant as the third subcase and the one this task most endangers.

- [ ] **Step 6: Commit**

```bash
git add engine/swarm/swarm_config.h engine/swarm/swarm_engine.cpp tests/test_swarm_engine.cpp
git commit -m "feat(swarm): MOTION runs from breath to shimmer"
```

---

### Task 6: A listening render, and the CPU re-measurement

**Files:**
- Create: `host/render/scenarios/swarm_characters.json`
- Create: `docs/bench/2026-08-17-<hash>-swarm-*.md`

**Interfaces:**
- Consumes: every task above.
- Produces: audio Bastian can judge, and a measured CPU verdict.

- [ ] **Step 1: Write a scenario that visits all four characters**

Create `host/render/scenarios/swarm_characters.json`, modelled on `swarm_drone.json`. Read that file first and follow its shape. It must:

- put SWARM on part A and leave part B silent,
- sit in FLOW with FALL at 1.0 so the drone stands,
- step `set_voice_resonance` through each character's zone centre — 0.125, 0.375, 0.625, 0.875 — holding each for 12 s,
- sweep HARM 0 → 1 inside each hold, so the rescaled travel is audible,
- open MOTION to 0.2 in the second half so the breath is on,
- carry a `_comment` explaining what to listen for in each section.

- [ ] **Step 2: Render and listen**

```bash
cmake --build build
./build/render.exe host/render/scenarios/swarm_characters.json /tmp/swarm_characters.wav /tmp/swarm_characters.csv
```

Expected: it renders without clipping and the four sections are audibly different instruments.

- [ ] **Step 3: Re-measure the CPU on the board**

**Not in a shell that has sourced `env.sh`.** Follow the character plan's Task 10 exactly — same relative gate, same traps, same fallback to N = 12.

```bash
git status --porcelain
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" python bench/run.py --profile swarm --repeat 2
```

The gate: `inst_swarm_engine_worst` must not exceed the SAME image's `instrument_worst`. This plan adds two ring reads per partial per tick and a third random walk, so it is the run that matters more than the character plan's.

- [ ] **Step 4: Commit**

```bash
git add host/render/scenarios/swarm_characters.json docs/bench/
git commit -m "bench(swarm): breath measured on the board, and a render to judge it by"
```

---

## Self-Review

**Spec coverage.** §7.1 slewed normalizer → Task 2. §7.2 spectral stagger → Task 3. §7.3 moving image → Task 4. §7.4 MOTION time base → Task 5. §9 gates: G-E → G39, G-F → G40, G-G → G41, G-H → G44, G-K → G38 (Task 1), G-J → Task 6, G-L → G44's third subcase plus the existing G27.

**Placeholders.** One deliberate looseness: Task 6 Step 1 describes the scenario's content rather than printing the JSON, because `swarm_drone.json` is the template and its exact schema must be read rather than guessed. Every requirement of the file is enumerated.

**Type consistency.** `_lane_at` / `lane_at_for_test` / `target_pan_for_test` / `_pan_walk` / `_norm_slew` / `kNormSlewS` / `kModStaggerS` / `kModRingDecim` / `kModRing` / `kModLanes` / `kDriftStepSlow` / `kDriftStepFast` / `kDriftPullSlow` / `kDriftPullFast` are each defined in exactly one task and used consistently after. `_focus_weight` changes signature in Task 3 (gains a `size` argument) — Task 3 owns that change and both call sites move with it.

**Ordering risk.** Task 3 changes `_focus_weight`'s signature and Task 5 rewrites `_advance_drift`, which Task 4 also edits. Run these in order; they are not independent.
