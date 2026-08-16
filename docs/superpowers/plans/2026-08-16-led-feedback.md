# LED Feedback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the 60 HP hardware panel 21 LEDs that show what is modulating right now, driven by a tested, Rack-free display law.

**Architecture:** Two small const observers are added to `engine/` (the modulation excursion and the limiter's squash). A Rack-free header `host/vcv/src/led_law.hpp` turns those into quantised brightnesses; `spky_tests` drives it directly, exactly as `tests/test_bbd_edge_state.cpp` drives `vcv/src/bbd_edge_state.hpp` today. The `FireflowHW` widget only copies the result into `lights[]`. The large `Fireflow` panel is untouched: the shared `LightId` enum grows, but `kLightCtls` — the table the large widget loops — does not.

**Tech Stack:** C++17, doctest, CMake + Ninja + clang (Release), Python 3 panel generators.

**Spec:** [`docs/superpowers/specs/2026-08-16-led-feedback-design.md`](../specs/2026-08-16-led-feedback-design.md)

## Global Constraints

- **Build:** `source env.sh` then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`. Release is not optional — Debug fails the render-hash gates with "SYNTH reference moved".
- **Never `cd`.** Run panel generators and their guards as `python host/vcv/res/<script>.py` is *not* valid — they resolve paths relative to their own file, so use the commands given in each task verbatim.
- **Everything written into the repo is English** — code, comments, commit messages.
- **Commit trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- **Every gate must be shown RED once** before it is trusted (`fireflow-tests-must-be-able-to-fail`). Each task's "verify it fails" step is that proof; do not skip it.
- **No render hash may move.** `ctrl_identity` and `wave_formant_sweep` must stay green throughout; nothing in this plan touches the audio path.
- **`panel_guard` must stay green from first commit to last**, untouched. It is the proof that nothing leaked into the large module.
- **21 lights**, 4 mux address + 5 enable lines = 30 of 32 chain outputs (spec §3).

## File Structure

| File | Responsibility |
|---|---|
| `engine/parts/part.h` / `.cpp` | new private `_mod_term()`, public `lane_excursion()` |
| `engine/fx/limiter.h` | store the squash factor `process()` currently discards |
| `engine/instrument.h` | two const observers: `lane_excursion()`, `limiter_squash()` |
| `host/vcv/src/led_law.hpp` | **new** — the whole display law, Rack-free and testable |
| `host/vcv/res/gen_panel.py` | second light list; `LightId` grows, `kLightCtls` does not |
| `host/vcv/res/gen_hw_panel.py` | delete CAP, move GATE and SYNC, 13 new `LIGHT_POS` entries |
| `host/vcv/res/test_hw_panel.py` | inventory, order and mirror gates for 21 lights |
| `host/vcv/src/Fireflow.cpp` | wiring only: call the law, copy into `lights[]` |
| `tests/test_led_law.cpp` | **new** — G1–G6, G9 |
| `CMakeLists.txt` | register the new test file |

---

### Task 1: The modulation excursion

The lights must show the modulation alone, not knob-plus-modulation. `Part::target_raw()` already computes the modulation term; this task extracts it so the display and the audio path can never drift apart, and exposes it.

**Files:**
- Modify: `engine/parts/part.cpp` (inside `target_raw`, around line 117)
- Modify: `engine/parts/part.h` (public and private sections)
- Modify: `engine/instrument.h` (beside `lane_output`, around line 409)
- Create: `tests/test_led_law.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `float Part::lane_excursion(int slot) const`, `float Instrument::lane_excursion(int p, int slot) const` — the modulation term in roughly `[-1, 1]`, exactly `0.f` when the lane is inactive or MOD is zero.

- [ ] **Step 1: Register the new test file so it can be built**

In `CMakeLists.txt`, add one line to the `add_executable(spky_tests ...)` source list, directly after `tests/test_step_accent.cpp`:

```cmake
    tests/test_led_law.cpp
```

- [ ] **Step 2: Write the failing test**

Create `tests/test_led_law.cpp`:

```cpp
#include "doctest.h"
#include "instrument.h"
#include <cmath>

using namespace spky;

// A settled instrument on deck 0, texture lanes moving.
static void settle(Instrument& inst, int blocks = 2000) {
    float l = 0.f, r = 0.f;
    for (int i = 0; i < blocks; ++i) inst.process(nullptr, nullptr, &l, &r, 1);
}

TEST_CASE("led G0: the excursion is the modulation alone, never the knob") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_rate(0, 0.5f);
    inst.set_range(0, 1.f);
    inst.set_target_base(0, LANE_SOURCE, 0.9f);

    SUBCASE("MOD 0 means no excursion, however high the knob sits") {
        inst.set_depth(0, 0.f);
        settle(inst);
        CHECK(inst.lane_excursion(0, LANE_SOURCE) == doctest::Approx(0.f));
        CHECK(inst.target_value(0, LANE_SOURCE) == doctest::Approx(0.9f));
    }
    SUBCASE("MOD up means the excursion moves while the knob does not") {
        inst.set_depth(0, 1.f);
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 4000; ++i) {
            settle(inst, 20);
            const float e = inst.lane_excursion(0, LANE_SOURCE);
            lo = std::fmin(lo, e);
            hi = std::fmax(hi, e);
        }
        CHECK(hi - lo > 0.1f);          // it actually swings
        CHECK(hi <= 1.0f);
        CHECK(lo >= -1.0f);
    }
}
```

- [ ] **Step 3: Run it and watch it fail**

```bash
source env.sh && cmake --build build 2>&1 | tail -5
```

Expected: compile error, `no member named 'lane_excursion' in 'spky::Instrument'`. That is the RED proof for this task.

- [ ] **Step 4: Extract the modulation term in `part.cpp`**

In `engine/parts/part.cpp`, insert this function immediately **above** `float Part::target_raw(int slot) const`:

```cpp
// The modulation term alone: what the lanes add to the knob, before the base
// and before any clamping. target_raw() below adds the base to exactly this,
// and the LED law displays exactly this (spec 2026-08-16 §3.1) -- one
// expression, so the display and the audio path cannot drift apart.
float Part::_mod_term(int slot) const {
    float d = (slot == LANE_PITCH) ? 1.f : _depth;
    if (slot == LANE_SOURCE && _engine_id == ENGINE_SAMPLER)
        d = std::pow(d, sampler_cfg::kSourceModExp);
    return _active[slot] ? _mod.lane_output(slot) * d * _tdepth[slot] : 0.f;
}
```

Then inside `target_raw`, delete the four lines that computed `d` and `mod` (from `float d = (slot == LANE_PITCH) ? 1.f : _depth;` through `float mod = _active[slot] ? ... : 0.f;`, keeping the comments above them) and replace them with:

```cpp
    float mod = _mod_term(slot);
```

- [ ] **Step 5: Declare it in `part.h`**

In the public section, next to `lane_output`:

```cpp
    float lane_excursion(int slot) const { return _mod_term(slot); }
```

In the private section, next to the other private helpers:

```cpp
    float _mod_term(int slot) const;
```

- [ ] **Step 6: Expose it on `Instrument`**

In `engine/instrument.h`, directly below `float lane_output(int p, int s) const`:

```cpp
    // The modulation term alone -- what the LED law displays. Deliberately
    // NOT target_value(), which is base + mod and would show the knob.
    float lane_excursion(int p, int s) const { return _parts[p].lane_excursion(s); }
```

- [ ] **Step 7: Run the test and the whole suite**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -tc="led G0*"
```

Expected: PASS. Then:

```bash
ctest --test-dir build --output-on-failure
```

Expected: 6/6 pass. The render hashes must not move — `_mod_term` is the same expression, so they will not.

- [ ] **Step 8: Commit**

```bash
git add engine/parts/part.cpp engine/parts/part.h engine/instrument.h tests/test_led_law.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(engine): expose the modulation excursion for the LED law

target_raw() computed the modulation term inline and threw it away into
base + mod. The LED display needs that term alone -- showing target_value()
would show the knob, which is what the design explicitly rejects. Extracted
as Part::_mod_term() and exposed as lane_excursion() on both Part and
Instrument, so the display and the audio path read one expression and cannot
drift apart. No behaviour change: all six tests green, both render hashes
unmoved.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 2: The limiter squash observer

The ceiling light must report the **audible onset** — where `shape()` starts bending — not the gain reduction, which at DRIVE 0.40 begins about 1.5–1.7 dB later (spec §2). `Limiter::process` already knows both; it stores neither.

**Files:**
- Modify: `engine/fx/limiter.h` (`process()` and a new member)
- Modify: `engine/instrument.h`
- Modify: `tests/test_led_law.cpp`

**Interfaces:**
- Produces: `float Limiter::squash() const` and `float Instrument::limiter_squash() const` — `0.f` when the shaper is transparent, rising toward `1.f` as the signal is driven past the knee.

- [ ] **Step 1: Write the failing test, ORDERED**

Append to `tests/test_led_law.cpp`:

```cpp
#include "fx/limiter.h"

// Feed a sine of the given amplitude through a settled limiter and return
// the squash it reports at the end.
static float squash_at(float knob, float amp) {
    Limiter lim;
    lim.init();
    lim.set_drive(knob);
    for (int i = 0; i < 20000; ++i) { float a = 0.f, b = 0.f; lim.process(a, b); }
    for (int i = 0; i < 4000; ++i) {
        float s = amp * std::sin(6.2831853f * 300.f * i / 48000.f);
        float l = s, r = s;
        lim.process(l, r);
    }
    return lim.squash();
}

TEST_CASE("led G9: the ceiling observer tracks the bend, and clears again") {
    // Ordered on purpose. Written as two independent cases the second clause
    // passes from the init value and could never catch a stale reading left
    // behind by the transparent early return in Limiter::process.
    Limiter lim;
    lim.init();
    lim.set_drive(0.40f);
    for (int i = 0; i < 20000; ++i) { float a = 0.f, b = 0.f; lim.process(a, b); }

    for (int i = 0; i < 4000; ++i) {                 // well past the knee
        float s = 0.95f * std::sin(6.2831853f * 300.f * i / 48000.f);
        float l = s, r = s;
        lim.process(l, r);
    }
    CHECK(lim.squash() > 0.f);

    for (int i = 0; i < 48000; ++i) {                // silence, long enough to settle
        float a = 0.f, b = 0.f;
        lim.process(a, b);
    }
    CHECK(lim.squash() == doctest::Approx(0.f));

    // The band the design exists for: bending, but no gain reduction yet.
    CHECK(squash_at(0.40f, 0.60f) > 0.f);
}
```

- [ ] **Step 2: Run it and watch it fail**

```bash
source env.sh && cmake --build build 2>&1 | tail -5
```

Expected: `no member named 'squash' in 'spky::Limiter'`.

- [ ] **Step 3: Store the squash in `limiter.h`**

In `engine/fx/limiter.h`, add to the private members beside `_peak`:

```cpp
    // How hard shape() is bending, 0 = transparent. Read by the panel's
    // ceiling light: the AUDIBLE onset is peak*gain > knee, which at the
    // shipped DRIVE 0.40 arrives about 1.5-1.7 dB before gain reduction
    // exists at all (bus peak 0.5545 against 0.676, measured 2026-08-16).
    float _squash = 0.f;
```

Add the accessor beside `pre_gain()`:

```cpp
    float squash() const { return _squash; }
```

In `init()`, add `_squash = 0.f;`.

In `process()`, set it on **both** paths. Replace the transparent early return:

```cpp
        if (_pre == 1.f && _peak <= 1.f && peak <= knee) {
            _squash = 0.f;      // MUST be cleared here: this path skips the
            return;             // computation below, and a stale reading
        }                       // would light the lamp during silence.
        const float gain = _peak > 1.f ? 1.f / _peak : 1.f;
        _squash = (peak * gain > knee)
                ? std::min(1.f, (peak * gain - knee) / (1.f - knee))
                : 0.f;
        l = shape(pl * gain, knee);
        r = shape(pr * gain, knee);
```

Add `#include <algorithm>` at the top if `std::min` is not already available.

- [ ] **Step 4: Expose it on `Instrument`**

In `engine/instrument.h`, beside `duck_gain()`:

```cpp
    // 0 while the master shaper is transparent, rising as it bends. This is
    // the audible onset, not the gain reduction -- see limiter.h.
    float limiter_squash() const { return _limiter.squash(); }
```

- [ ] **Step 5: Run the test and the whole suite**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -tc="led G9*"
ctest --test-dir build --output-on-failure
```

Expected: PASS, then 6/6. The render hashes must not move: `_squash` is written but never read by the audio path.

- [ ] **Step 6: Prove the staleness clause can go red**

Temporarily delete the `_squash = 0.f;` line inside the early return, rebuild, and run `./build/spky_tests.exe -tc="led G9*"`. Expected: FAIL on `lim.squash() == Approx(0.0)`. Restore the line, rebuild, confirm PASS. This is the RED proof that the ordered gate catches the one bug this observer can have.

- [ ] **Step 7: Commit**

```bash
git add engine/fx/limiter.h engine/instrument.h tests/test_led_law.cpp
git commit -m "$(cat <<'EOF'
feat(engine): the limiter reports how hard it is bending

Limiter::process computed its gain factor and discarded it, so nothing could
say whether the master shaper was working -- and with MASTER_DRIVE retired
from the hardware panel, nothing tells a player where the ceiling is.

The observer reports the AUDIBLE onset (peak*gain > knee), not the gain
reduction. At the shipped DRIVE 0.40 the shaper starts bending at bus peak
0.5545 while gain reduction needs 0.676, about 1.5-1.7 dB later, so a
gain-based lamp would stay dark exactly where the sound first changes.

The transparent early return clears _squash before returning. Its gate is
ordered -- drive above, then below, then assert zero -- and was proved red
by deleting that one line; written as two independent cases it would have
passed from the init value and caught nothing.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 3: The display law

The whole law as one Rack-free header, so it can go red in `spky_tests` instead of being judged by eye. Same pattern as `host/vcv/src/bbd_edge_state.hpp`, which `tests/test_bbd_edge_state.cpp` already drives.

**Files:**
- Create: `host/vcv/src/led_law.hpp`
- Modify: `tests/test_led_law.cpp`

**Interfaces:**
- Produces: `spkyled::Lamp` (member `float env`, method `void follow(float excursion, float dt)`), `float spkyled::intensity(float env, float excursion)`, `int spkyled::duty(float intens, int steps)`, and the constants `kFloor`, `kGamma`, `kEnvFall`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_led_law.cpp`:

```cpp
#include "vcv/src/led_law.hpp"

TEST_CASE("led G1: dark means zero modulation, and nothing else does") {
    CHECK(spkyled::duty(spkyled::intensity(0.f, 0.f), 16) == 0);
    CHECK(spkyled::duty(spkyled::intensity(0.5f, 0.f), 16) > 0);
    CHECK(spkyled::duty(spkyled::intensity(0.5f, 0.5f), 16) > 0);
}

TEST_CASE("led G2: no non-zero intensity is quantised away to off") {
    for (int i = 1; i <= 10000; ++i) {
        const float v = static_cast<float>(i) / 10000.f;
        CHECK(spkyled::duty(v, 16) >= 1);
    }
    CHECK(spkyled::duty(0.f, 16) == 0);
}

TEST_CASE("led G3: the step count is the mux width and every step is reached") {
    for (int steps : {8, 16}) {
        bool seen[64] = {false};
        for (int i = 0; i <= 100000; ++i) {
            const float v = static_cast<float>(i) / 100000.f;
            const int d = spkyled::duty(v, steps);
            REQUIRE(d >= 0);
            REQUIRE(d < steps);
            seen[d] = true;
        }
        for (int d = 0; d < steps; ++d)
            CHECK_MESSAGE(seen[d], "step ", d, " of ", steps, " unreachable");
    }
}

TEST_CASE("led G4: gamma runs in the perceptual direction") {
    const int steps = 16;
    const int mid   = spkyled::duty(0.5f, steps);
    const int lin   = static_cast<int>(0.5f * (steps - 1) + 0.5f);
    CHECK(mid < lin - 1);                       // measurably BELOW linear
    // ... and it is perceptually linear: duty^(1/gamma) tracks the input.
    for (float v : {0.25f, 0.5f, 0.75f, 1.0f}) {
        const float d = static_cast<float>(spkyled::duty(v, steps)) / (steps - 1);
        CHECK(std::pow(d, 1.f / spkyled::kGamma) == doctest::Approx(v).epsilon(0.12));
    }
}

TEST_CASE("led G5: the trough scales with depth") {
    const float deep    = spkyled::intensity(0.9f, 0.f);
    const float shallow = spkyled::intensity(0.2f, 0.f);
    CHECK(deep > shallow);
    // A lane frozen at its own peak stays bright rather than fading out.
    CHECK(spkyled::intensity(0.9f, 0.9f) == doctest::Approx(0.9f));
}

TEST_CASE("led: the envelope attacks instantly and falls slowly") {
    spkyled::Lamp lamp;
    const float dt = 1.f / 750.f;               // the control rate used in Rack
    lamp.follow(0.8f, dt);
    CHECK(lamp.env == doctest::Approx(0.8f));   // instant attack
    lamp.follow(0.f, dt);
    CHECK(lamp.env > 0.7f);                     // one tick barely moves it
    for (int i = 0; i < 4000; ++i) lamp.follow(0.f, dt);
    CHECK(lamp.env < 0.05f);                    // but it does let go
}
```

- [ ] **Step 2: Run and watch it fail**

```bash
source env.sh && cmake --build build 2>&1 | tail -5
```

Expected: `'vcv/src/led_law.hpp' file not found`.

- [ ] **Step 3: Write the header**

Create `host/vcv/src/led_law.hpp`:

```cpp
#pragma once
// The panel's LED display law. Rack-free on purpose so spky_tests can drive
// it -- the same arrangement as bbd_edge_state.hpp. Fireflow.cpp keeps only
// the wiring. Spec: docs/superpowers/specs/2026-08-16-led-feedback-design.md
#include <algorithm>
#include <cmath>

namespace spkyled {

// Trough of a breath, as a fraction of the envelope. A FIXED floor would make
// an idle lane and a gently moving one look alike, and would push every
// breath's bottom through the same dark band -- which at this instrument's
// slow rates reads as a loose contact. By-ear candidate, not an invariant.
constexpr float kFloor = 0.28f;

// Perceived lightness goes roughly as the cube root of duty, so perceptual
// linearity needs duty = intensity^gamma with gamma above 1. That puts the
// midpoint duty BELOW the linear midpoint; a linear ramp looks static across
// its top half.
constexpr float kGamma = 2.2f;

// Envelope release, in seconds. Long enough to hold through a breath at
// audible rates, short enough to let go when modulation stops. NOTE the
// limit this implies: for lane cycles much longer than this the envelope
// follows the excursion directly rather than holding its peak, so a very
// slow lane tracks its own position instead of showing a steady depth. It is
// still never dark while it moves, which is what the design needs.
constexpr float kEnvFall = 2.0f;

// One light's state: a peak-tracked envelope of |excursion|, i.e. the
// modulation DEPTH.
struct Lamp {
    float env = 0.f;

    void follow(float excursion, float dt) {
        const float a = std::fabs(excursion);
        if (a >= env) { env = a; return; }           // instant attack
        const float k = dt / (kEnvFall + dt);        // one-pole release
        env += (a - env) * k;
        if (env < 1e-6f) env = 0.f;
    }
};

// The envelope sets the ceiling, the instantaneous excursion breathes inside
// it, and the trough scales with depth. Three readings fall out of one
// expression: dark (nothing modulating), dim breath (shallow), bright breath
// (deep).
inline float intensity(float env, float excursion) {
    if (env <= 0.f) return 0.f;
    const float a   = std::fabs(excursion);
    const float rel = a >= env ? 1.f : a / env;
    return env * (kFloor + (1.f - kFloor) * rel);
}

// Quantised duty, 0 .. steps-1. `steps` is the mux width -- 16 with 16:1
// parts, 8 with 8:1, and that choice is still open, so it is a parameter.
// Every non-zero intensity must reach at least one step: naive quantisation
// of a gamma curve sends the bottom third of every breath to zero, which
// would read as "nothing is modulating" and destroy the whole distinction.
inline int duty(float intens, int steps) {
    if (intens <= 0.f) return 0;
    const float v = std::min(1.f, intens);
    const int   q = static_cast<int>(std::pow(v, kGamma) * (steps - 1) + 0.5f);
    return q < 1 ? 1 : q;
}

} // namespace spkyled
```

- [ ] **Step 4: Run the tests**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -tc="led G1*,led G2*,led G3*,led G4*,led G5*,led: the envelope*"
```

Expected: PASS.

- [ ] **Step 5: Prove G2 and G4 can go red**

Change `return q < 1 ? 1 : q;` to `return q;`, rebuild, run `-tc="led G2*"`. Expected: FAIL. Restore.
Change `kGamma` to `0.45f`, rebuild, run `-tc="led G4*"`. Expected: FAIL on `mid < lin - 1`. Restore, rebuild, confirm both PASS.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/src/led_law.hpp tests/test_led_law.cpp
git commit -m "$(cat <<'EOF'
feat(vcv): the LED display law, as a testable unit

An envelope sets each light's ceiling, the instantaneous excursion breathes
inside it, and the trough scales with depth -- so dark, dim breath and bright
breath fall out of one expression instead of being three bolted-on states,
and "dark" means exactly one thing: nothing is modulating here.

Rack-free by design, the same arrangement as bbd_edge_state.hpp, so the law
goes red in spky_tests rather than being judged by eye.

Two rules are gates rather than comments. Gamma runs in the perceptual
direction, which puts midpoint duty BELOW linear -- proved red by inverting
it. And every non-zero intensity reaches at least one duty step: quantising
a gamma curve naively sends the bottom third of every breath to zero, which
would read as "nothing is modulating" -- proved red by removing the lift.

The step count is a parameter, not 16, because the 8:1/16:1 mux choice is
still open.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 4: The shared enum grows, the large module does not

Seventeen new `LightId`s. They go in a **second list**, following the pattern `gen_panel.py` already uses for the eight hardware-only CV jacks: the enum is concatenated, the tables are not.

**Files:**
- Modify: `host/vcv/res/gen_panel.py` (after `LIGHTS`, and at `emit_enum`)
- Modify: `host/vcv/res/test_panel.py` (one new guard; `LIGHT_ORDER` stays untouched)

**Interfaces:**
- Produces: `gen_panel.HW_ONLY_LIGHTS` (17 `Ctl` entries at 0/0), `LightId` with 21 members, `NUM_LIGHTS == 21`. `kLightCtls` keeps its four entries.

- [ ] **Step 1: Write the failing guard**

In `host/vcv/res/test_panel.py`, add:

```python
def test_hw_only_lights_are_in_the_enum_but_not_in_the_table():
    """The two widgets share one Fireflow module class, so LightId must
    carry every lamp on either panel. The TABLES must not: kLightCtls is
    what FireflowWidget loops to create widgets, so anything appended there
    would be drawn on the large panel at hardware coordinates. Same split
    the eight HW-only CV jacks already use (emit_enum INPUTS +
    HW_MOD_INPUTS, emit_table kInputCtls INPUTS)."""
    extra = getattr(g, "HW_ONLY_LIGHTS", None)
    check(extra is not None, "gen_panel has no HW_ONLY_LIGHTS list")
    if extra is None:
        return
    check(len(g.LIGHTS) + len(extra) == 21,
          f"expected 21 lights in the enum, got {len(g.LIGHTS)} + {len(extra)}")
    names = {c.enum for c in g.LIGHTS}
    for c in extra:
        check(c.enum not in names, f"{c.enum} is in both lists")
    check("CAP_A_L" not in {c.enum for c in extra},
          "CAP_A_L is back: the capture sequencer was deleted 2026-07-14")
```

- [ ] **Step 2: Run it and watch it fail**

```bash
source env.sh && ctest --test-dir build -R panel_guard --output-on-failure
```

Expected: FAIL, "gen_panel has no HW_ONLY_LIGHTS list".

- [ ] **Step 3: Add the second list**

In `host/vcv/res/gen_panel.py`, directly below `STATIC_LIGHTS`:

```python
# Lights that exist only on the 60 HP hardware panel. Their IDs must be in
# the shared LightId enum -- both widgets share one Fireflow module class --
# but they must NOT enter kLightCtls, which FireflowWidget loops to build
# widgets: appended there they would be drawn on the large panel at hardware
# coordinates. Exactly the split HW_MOD_INPUTS already uses. Coordinates are
# 0/0 here and come from gen_hw_panel's LIGHT_POS, same as those jacks.
HW_ONLY_LIGHTS = [
    Ctl("SRC_A_L",     LIGHT, 0, 0, ""),   # LANE_SOURCE excursion
    Ctl("SRC_B_L",     LIGHT, 0, 0, ""),
    Ctl("FLT_A_L",     LIGHT, 0, 0, ""),   # LANE_SIZE excursion
    Ctl("FLT_B_L",     LIGHT, 0, 0, ""),
    Ctl("CLR_A_L",     LIGHT, 0, 0, ""),   # LANE_MOTION excursion
    Ctl("CLR_B_L",     LIGHT, 0, 0, ""),
    Ctl("LVL_A_L",     LIGHT, 0, 0, ""),   # LANE_LEVEL excursion
    Ctl("LVL_B_L",     LIGHT, 0, 0, ""),
    Ctl("SONG_A_L",    LIGHT, 0, 0, ""),   # which phrase snapshot is sounding
    Ctl("SONG_B_L",    LIGHT, 0, 0, ""),
    Ctl("FLOW_A_L",    LIGHT, 0, 0, ""),   # drawn since the regroup, never lit
    Ctl("FLOW_B_L",    LIGHT, 0, 0, ""),
    Ctl("TEMPO_L",     LIGHT, 0, 0, ""),
    Ctl("SYNC_L",      LIGHT, 0, 0, ""),
    Ctl("MODBTN_L",    LIGHT, 0, 0, ""),   # latched, per spec 3.4
    Ctl("SHIFTBTN_L",  LIGHT, 0, 0, ""),
    Ctl("CEIL_L",      LIGHT, 0, 0, ""),   # the master shaper is bending
]
```

- [ ] **Step 4: Concatenate at the enum only**

In the same file, change the light enum emission and leave the table alone:

```python
    emit_enum("LightId",  LIGHTS + HW_ONLY_LIGHTS,  "NUM_LIGHTS")
```

`emit_table("kLightCtls", LIGHTS)` is **not** changed.

- [ ] **Step 5: Regenerate and run both panel guards**

```bash
source env.sh && python "host/vcv/res/gen_panel.py" && ctest --test-dir build -R "panel_guard|hw_panel_guard" --output-on-failure
```

Expected: `panel_guard` PASSES — including the untouched `LIGHT_ORDER` and `STATIC_LIGHTS` assertions, which is the proof that nothing leaked into the large module. `hw_panel_guard` may fail on the light-count assertions; that is Task 5's job.

- [ ] **Step 6: Verify the generated enum**

```bash
grep -n "NUM_LIGHTS\|CEIL_L\|SHIFTBTN_L" host/vcv/src/generated_panel.hpp | head
```

Expected: `CEIL_L`, `SHIFTBTN_L` present in `LightId`, terminator `NUM_LIGHTS` following them.

- [ ] **Step 7: Gate G8 — the two enum spaces must not collide**

`Fireflow.cpp:1571` records the trap in a comment (`REC_A_L == 2 == DENSITY_A`)
and nothing enforces it; `test_hw_panel.py:169` only asserts the tables exist by
name. Adding seventeen light IDs is exactly when this wants a guard. Add to
`host/vcv/res/test_panel.py`:

```python
def test_light_ids_do_not_collide_with_param_ids():
    """LightId and ParamId are separate enums that both start at 0, and the
    C++ indexes lights[] and params[] with them. A comment in Fireflow.cpp
    (REC_A_L == 2 == DENSITY_A) has been the only thing standing between
    that and a silent mix-up. Assert the shapes instead: every light id must
    be reachable in lights[], and NUM_LIGHTS must cover the whole list."""
    all_lights = list(g.LIGHTS) + list(g.HW_ONLY_LIGHTS)
    check(len(all_lights) == len(set(c.enum for c in all_lights)),
          "duplicate light enum name")
    param_names = {c.enum for c in g.RUNTIME_PANEL_PARAMS}
    for c in all_lights:
        check(c.enum not in param_names,
              f"{c.enum} is both a light and a parameter name")
```

Run it:

```bash
source env.sh && ctest --test-dir build -R panel_guard --output-on-failure
```

Expected: PASS. Prove it red by temporarily renaming `CEIL_L` to `MORPH` in
`HW_ONLY_LIGHTS`; expected FAIL, "MORPH is both a light and a parameter name".
Restore.

- [ ] **Step 7: Commit**

```bash
git add host/vcv/res/gen_panel.py host/vcv/res/test_panel.py host/vcv/src/generated_panel.hpp
git commit -m "$(cat <<'EOF'
feat(vcv): seventeen hardware-only light IDs, without touching the large panel

The two widgets share one Fireflow module class, so LightId has to carry
every lamp on either panel. The tables do not, and must not: FireflowWidget
loops kLightCtls to create widgets, so lights appended there would be drawn
on the large panel at hardware coordinates.

The split is the one gen_panel.py already uses for the eight CV jacks that
exist only on the hardware panel -- emit_enum concatenates, emit_table does
not. LIGHTS keeps its four entries in their frozen order, so the large
module needs no change at all and both of its existing guards stay green
untouched, which is the proof that nothing leaked.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 5: Positions on the hardware panel

Delete the two dead lamps, move two, and place thirteen new ones at the measured coordinates. Every position is a satellite at exactly `anchor radius + 1.5 mm` inside its row's existing ink band, so `_row_ink()` cannot change and no frame moves (spec §5).

**Files:**
- Modify: `host/vcv/res/gen_hw_panel.py` (`LIGHT_POS`, `HW_ONLY`, `HW_LIGHTS`)
- Modify: `host/vcv/res/test_hw_panel.py` (`test_hw_only_inventory`, the light-count assertions, order assertion at line 32)

**Interfaces:**
- Consumes: the enum names from Task 4.
- Produces: `hw.HW_LIGHTS` with 21 placed entries; `spkyhw::kLightCtls` with 21 rows.

- [ ] **Step 1: Write the failing inventory gate**

In `host/vcv/res/test_hw_panel.py`, replace the two hard-coded light counts (`kinds.get("L") == 6` in `test_hw_only_inventory` and `total_leds == 10`) and add:

```python
def test_led_inventory_after_the_feedback_round():
    """21 lamps, the two capture indicators gone, GATE out of the timing row
    and SYNC at the CLOCK jack. Spec 2026-08-16 sections 3.1-3.5."""
    names = {c.enum for c in hw.HW_LIGHTS}
    check(len(hw.HW_LIGHTS) == 21, f"{len(hw.HW_LIGHTS)} lights, expected 21")
    for dead in ("CAP_A_L", "CAP_B_L"):
        check(dead not in names, f"{dead} still drawn -- capture was deleted 2026-07-14")
    for new in ("SRC_A_L", "FLT_A_L", "CLR_A_L", "LVL_A_L", "SONG_A_L",
                "MODBTN_L", "SHIFTBTN_L", "CEIL_L"):
        check(new in names, f"{new} missing")
    by = {c.enum: c for c in hw.HW_LIGHTS}
    check(abs(by["GATE_A_L"].y - 37.75) < 1e-6,
          f"GATE_A_L is at y={by['GATE_A_L'].y}, not in the VOICE row")
    check(abs(by["SYNC_L"].y - 114.0) < 1e-6,
          f"SYNC_L is at y={by['SYNC_L'].y}, not on the jack row")
```

Also update the order assertion at line 32 to the concatenation:

```python
    assert [c.enum for c in hw.HW_LIGHTS] == \
           [c.enum for c in gp.LIGHTS] + [c.enum for c in gp.HW_ONLY_LIGHTS]
```

- [ ] **Step 2: Run it and watch it fail**

```bash
source env.sh && ctest --test-dir build -R hw_panel_guard --output-on-failure
```

Expected: FAIL — the light count is 4, not 21.

- [ ] **Step 3: Take the four now-real lamps out of `HW_ONLY`**

In `host/vcv/res/gen_hw_panel.py`, delete these six entries from the `HW_ONLY` list: `TEMPO_L`, `SYNC_L`, `FLOW_A_L`, `CAP_A_L`, `FLOW_B_L`, `CAP_B_L`. `CAP_*` are gone for good; the other four are now driven lights and come through `gp.HW_ONLY_LIGHTS`. `MODBTN` and `SHIFTBTN` stay in `HW_ONLY` — those are the pads, not their lamps.

- [ ] **Step 4: Extend `HW_LIGHTS` and add the positions**

Change the `HW_LIGHTS` line:

```python
HW_LIGHTS  = [place(c) for c in gp.LIGHTS + gp.HW_ONLY_LIGHTS]
```

And add to `LIGHT_POS` (mirrors are `W - x`, i.e. `304.8 - x`):

```python
    # --- LED feedback round, 2026-08-16 -------------------------------------
    # Satellites: each at exactly anchor radius + 1.5 mm, inside its row's
    # existing ink band, so _row_ink() is unchanged and no frame can move.
    # Measured free at these points against every drawn element (spec 5.2).
    "SRC_A_L":    (109.75,  50.22),  "SRC_B_L":    (195.05,  50.22),
    "FLT_A_L":    ( 96.13,  54.56),  "FLT_B_L":    (208.67,  54.56),
    "CLR_A_L":    ( 33.50,  95.00),  "CLR_B_L":    (271.30,  95.00),
    "LVL_A_L":    (116.50,  76.00),  "LVL_B_L":    (188.30,  76.00),
    "SONG_A_L":   ( 54.50,  18.25),  "SONG_B_L":   (250.30,  18.25),
    # GATE leaves the timing row, where it sat 4 mm from REC and CAP and
    # meant nothing, for the VOICE row where the note is shaped.
    "GATE_A_L":   ( 74.75,  37.75),  "GATE_B_L":   (230.05,  37.75),
    # FLOW and the two pad lamps keep their drawn positions; SYNC leaves
    # SHUFFLE's side, where only mirror symmetry had put it, for the CLOCK
    # jack, where an external clock actually arrives.
    "SYNC_L":     (130.50, 114.00),
    "MODBTN_L":   (285.30, 114.00),  "SHIFTBTN_L": ( 19.50, 114.00),
    # On the centre axis, in the master column between REV_DECAY and
    # REV_TONE. Unsuffixed, so _twin_enum declares no mirror partner and
    # test_mirror_symmetry leaves it alone -- as it already does TEMPO_L.
    "CEIL_L":     (152.40,  89.30),
```

`TEMPO_L` (130.40 / 34.00), `FLOW_A_L` (93.50 / 14.50) and `FLOW_B_L` (211.30 / 14.50) keep the coordinates they already have — move their entries from the `HW_ONLY` loop into `LIGHT_POS` unchanged.

- [ ] **Step 5: Regenerate and run the guard**

```bash
source env.sh && python "host/vcv/res/gen_hw_panel.py" && ctest --test-dir build -R "panel_guard|hw_panel_guard" --output-on-failure
```

Expected: both PASS. If a caption guard rejects a position, **turn the bearing only** — keep the distance at the minimum and never move a knob (spec §5.3). Re-run until green, and record any bearing you changed in the commit message.

- [ ] **Step 6: Look at it**

```bash
"/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" --headless --disable-gpu \
  --force-device-scale-factor=2 --screenshot=led_panel.png --window-size=1220,520 \
  --hide-scrollbars --default-background-color=ffffffff \
  "file:///C:/Users/bernd/Documents/AI/FireFlow/host/vcv/res/FireflowHW.svg"
```

Open `led_panel.png` and confirm no lamp sits on a caption and the two decks mirror.

- [ ] **Step 7: Commit**

```bash
git add host/vcv/res/gen_hw_panel.py host/vcv/res/test_hw_panel.py \
        host/vcv/res/FireflowHW.svg host/vcv/src/generated_hw_panel.hpp
git commit -m "$(cat <<'EOF'
feat(hw): 21 lamps on the plate, and two dead ones removed

CAP_A/B_L indicated the capture sequencer, deleted on 2026-07-14 with its
tests and scenarios -- they are gone. GATE leaves the timing row, where it
sat four millimetres from REC and CAP and related to neither, for the VOICE
row. SYNC leaves SHUFFLE's side, where only mirror symmetry had put it, for
the CLOCK jack where an external clock actually arrives.

The thirteen new positions are satellites: each at exactly anchor radius +
1.5 mm, inside its row's existing ink band. That is what makes them safe --
test_rows_are_centred_on_their_ink derives every frame from _row_ink() and
chains rows through BOX_GAP, so anything extending a row shifts every frame
below it, and a 1.5 mm lamp inside a band defined by 6.0 and 8.5 mm knobs
cannot. Every coordinate was measured free against the whole inventory
before it was written.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 6: From instrument state to 21 brightnesses

The mapping is the part that has never been guarded on this panel — six lamps sat drawn with no `LightId` for months and nothing noticed. Putting it in the Rack-free header makes that testable.

**Files:**
- Modify: `host/vcv/src/led_law.hpp`
- Modify: `tests/test_led_law.cpp`

**Interfaces:**
- Consumes: `Instrument::lane_excursion`, `Instrument::limiter_squash`, `Instrument::active_pattern_for_test`, `Instrument::gate`, `Instrument::engine_id`, `Instrument::sampler_is_recording`, `Instrument::sampler_empty`, `Instrument::sampler_fill`; the `LightId` enum from `generated_panel.hpp`.
- Produces: `struct spkyled::Panel { Lamp lamp[NUM_LIGHTS]; float phase = 0.f; }` and
  `void spkyled::fill(const spky::Instrument&, Panel&, float dt, int steps, int* duty_out)` — writes exactly `NUM_LIGHTS` entries.

- [ ] **Step 1: Write the failing wiring gate**

Append to `tests/test_led_law.cpp`:

```cpp
#include "vcv/src/generated_panel.hpp"

TEST_CASE("led G6: every light is written, and a modulating lane moves") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_rate(0, 2.0f);
    inst.set_range(0, 1.f);
    inst.set_depth(0, 1.f);

    spkyled::Panel panel;
    int duty[spkyvcv::NUM_LIGHTS];
    for (int i = 0; i < spkyvcv::NUM_LIGHTS; ++i) duty[i] = -1;

    const float dt = 1.f / 750.f;
    settle(inst, 200);
    spkyled::fill(inst, panel, dt, 16, duty);

    for (int i = 0; i < spkyvcv::NUM_LIGHTS; ++i)
        CHECK_MESSAGE(duty[i] >= 0, "light ", i, " was never written");

    // The SOURCE excursion light must actually change over time.
    int lo = 99, hi = -1;
    for (int k = 0; k < 400; ++k) {
        settle(inst, 64);
        spkyled::fill(inst, panel, dt, 16, duty);
        lo = std::min(lo, duty[spkyvcv::SRC_A_L]);
        hi = std::max(hi, duty[spkyvcv::SRC_A_L]);
    }
    CHECK(hi > lo);

    // At MOD 0 the same light is dark, and stays dark.
    inst.set_depth(0, 0.f);
    for (int k = 0; k < 4000; ++k) {
        settle(inst, 64);
        spkyled::fill(inst, panel, dt, 16, duty);
    }
    CHECK(duty[spkyvcv::SRC_A_L] == 0);
}
```

- [ ] **Step 2: Run and watch it fail**

```bash
source env.sh && cmake --build build 2>&1 | tail -5
```

Expected: `no member named 'fill' in namespace 'spkyled'`.

- [ ] **Step 3: Add the mapping to `led_law.hpp`**

Append inside `namespace spkyled`, after `duty()`:

First add the two includes at the **top** of `led_law.hpp`, below the existing
`<cmath>` — the file stays Rack-free, it just stops being engine-free:

```cpp
#include "instrument.h"
#include "generated_panel.hpp"
```

Then append the following inside the existing `namespace spkyled`, after `duty()`:

```cpp
struct Panel {
    Lamp  lamp[spkyvcv::NUM_LIGHTS];
    float blink = 0.f;                 // free-running, for the phrase lamps
};

// The whole panel in one call, so a gate can assert that every light is
// written -- six lamps sat on this plate for months with no LightId and
// nothing noticed. Writes exactly NUM_LIGHTS entries of duty_out.
inline void fill(const spky::Instrument& inst, Panel& p, float dt,
                 int steps, int* duty_out) {
    using namespace spkyvcv;
    for (int i = 0; i < NUM_LIGHTS; ++i) duty_out[i] = 0;

    p.blink += dt;
    if (p.blink >= 1.f) p.blink -= 1.f;

    struct Slot { int id; int lane; };
    static const Slot kExc[8] = {
        {SRC_A_L, spky::LANE_SOURCE}, {SRC_B_L, spky::LANE_SOURCE},
        {FLT_A_L, spky::LANE_SIZE},   {FLT_B_L, spky::LANE_SIZE},
        {CLR_A_L, spky::LANE_MOTION}, {CLR_B_L, spky::LANE_MOTION},
        {LVL_A_L, spky::LANE_LEVEL},  {LVL_B_L, spky::LANE_LEVEL},
    };
    for (int i = 0; i < 8; ++i) {
        const int part = i & 1;
        const float e  = inst.lane_excursion(part, kExc[i].lane);
        p.lamp[kExc[i].id].follow(e, dt);
        duty_out[kExc[i].id] = duty(intensity(p.lamp[kExc[i].id].env, e), steps);
    }

    // Phrase: steady for snapshot A, double-pulse for B. Brightness is the
    // channel the excursion lights use, so this one is carried by shape.
    const int songId[2] = {SONG_A_L, SONG_B_L};
    for (int part = 0; part < 2; ++part) {
        const bool b = inst.active_pattern_for_test(part) != 0;
        const bool on = b ? (p.blink < 0.15f || (p.blink > 0.3f && p.blink < 0.45f))
                          : true;
        duty_out[songId[part]] = on ? steps - 1 : 0;
    }

    const int gateId[2] = {GATE_A_L, GATE_B_L};
    for (int part = 0; part < 2; ++part) {
        p.lamp[gateId[part]].follow(inst.gate(part) ? 1.f : 0.f, dt);
        duty_out[gateId[part]] = duty(p.lamp[gateId[part]].env, steps);
    }

    duty_out[CEIL_L] = duty(inst.limiter_squash(), steps);
}

} // namespace spkyled
```

> The REC, FLOW, TEMPO, SYNC, MODBTN and SHIFTBTN lamps stay at duty 0 in this
> task and are filled in Task 7, where the host state they need (recording,
> STEP flag, transport, latch) is available. G6 asserts only that every entry
> is *written*, which the `for` loop above guarantees.

- [ ] **Step 4: Run the gate**

```bash
source env.sh && cmake --build build && ./build/spky_tests.exe -tc="led G6*"
```

Expected: PASS.

- [ ] **Step 5: Prove it can go red**

Change the excursion loop bound from `i < 8` to `i < 6`, rebuild, run `-tc="led G6*"`. Expected: FAIL — `light … was never written` for `LVL_A_L`. Restore and confirm PASS.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/src/led_law.hpp tests/test_led_law.cpp
git commit -m "$(cat <<'EOF'
feat(vcv): map instrument state onto all 21 lights, in one guarded call

The mapping lives in the Rack-free header rather than inside process(), so a
gate can assert that every light is written every call. That gate is the one
this round most needed: six lamps sat drawn on this panel for months with no
LightId at all and nothing noticed. Proved red by shortening the excursion
loop by one deck.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 7: Wire it into the hardware widget

**Files:**
- Modify: `host/vcv/src/led_law.hpp` (the remaining lamps)
- Modify: `host/vcv/src/Fireflow.cpp` (`process`, around lines 1011–1033)

**Interfaces:**
- Consumes: `spkyled::fill` from Task 6.

- [ ] **Step 1: Fill the remaining lamps in `led_law.hpp`**

Add before the closing brace of `fill()`:

```cpp
    // REC keeps the three-state behaviour it already had: pulsing while
    // recording, steady at the fill level when the part holds content, dark
    // otherwise and on any non-Sampler engine.
    const int recId[2] = {REC_A_L, REC_B_L};
    for (int part = 0; part < 2; ++part) {
        float v = 0.f;
        const bool sampler = inst.engine_id(part) == spky::ENGINE_SAMPLER;
        if (inst.sampler_is_recording(part))
            v = p.blink < 0.5f ? 1.f : 0.25f;
        else if (sampler && !inst.sampler_empty(part))
            v = 0.15f + 0.55f * inst.sampler_fill(part);
        duty_out[recId[part]] = duty(v, steps);
    }
```

- [ ] **Step 2: Replace the light block in `Fireflow.cpp`**

Add near the other members of the `Fireflow` module struct:

```cpp
    spkyled::Panel ledPanel;
    rack::dsp::ClockDivider ledDiv;          // ledDiv.setDivision(64) in the ctor
    int ledDuty[NUM_LIGHTS] = {0};
```

Replace the existing gate-light and REC-light block in `process` (the `gateFilt` loop through the `recPhase` loop) with:

```cpp
        // One law, one call, 21 lamps -- host/vcv/src/led_law.hpp. Quantised
        // to 16 steps even here: that is what the mux scan gives the hardware
        // for free, and a Rack module that breathes more finely than the panel
        // ever can is validating itself against the wrong instrument.
        if (ledDiv.process()) {
            const float dt = 64.f * args.sampleTime;
            spkyled::fill(inst, ledPanel, dt, 16, ledDuty);
            for (int i = 0; i < NUM_LIGHTS; ++i)
                lights[i].setBrightness(float(ledDuty[i]) / 15.f);
        }
```

Add `#include "led_law.hpp"` at the top, and delete the now-unused `gateFilt` and `recPhase` members.

- [ ] **Step 3: Build the plugin**

```bash
source env.sh && host/vcv/build-local.sh 2>&1 | tail -20
```

Expected: builds clean. **Never invoke `g++` directly** — the system compiler on this machine is the ARM cross-compiler.

- [ ] **Step 4: Run everything**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: 6/6 pass, including both render hashes unmoved.

- [ ] **Step 5: Look at it in Rack**

Load `FireFlow HW Draft`, set both decks to STEP with MOD up, and confirm: the four excursion lamps per deck breathe at visibly different rates; turning TIDE makes them drift apart; MOD at zero puts all eight out; SONG's lamp double-pulses on snapshot B.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/src/led_law.hpp host/vcv/src/Fireflow.cpp
git commit -m "$(cat <<'EOF'
feat(vcv): drive the hardware panel's 21 lamps

The widget now does nothing but call the law and copy the result, at a
64-sample divider so the update rate is close to the 500 Hz block rate the
hardware will have. Brightness is quantised to 16 steps in Rack too: that is
what the mux scan gives the hardware for free, and a module that breathes
more finely than the panel can is validating itself against the wrong
instrument.

The gate and REC lamps keep the behaviour they had; their code moved into
the law with everything else, so all 21 follow one rule.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

## Notes for the executor

- **`kFloor`, `kGamma`, `kEnvFall` are tuning values, not invariants.** No gate depends on their exact numbers, only on the relationships (`G4` on the direction of gamma, `G5` on the trough scaling). They are by-ear candidates; do not "fix" them toward round numbers.
- **The step count 16 is provisional.** The 8:1 versus 16:1 mux choice is still open (io-budget §6). `duty()` takes it as a parameter and G3 runs at both widths for that reason.
- **If a caption guard rejects a position in Task 5, turn the bearing.** Never grow the distance, never move a knob. Record the bearing you changed.
- **The FLOW, TEMPO, SYNC, MODBTN and SHIFTBTN lamps stay dark** at the end of this plan. FLOW and TEMPO need host state that exists (`set_step`, transport) and can be wired in a follow-up; SYNC needs a clock-input detector; the two pad lamps need the latch that spec §3.4 deliberately leaves to the round that builds MOD and SHIFT. G6 asserts they are written, not that they are non-zero.
