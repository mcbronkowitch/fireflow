# Reverb Bloom Duck Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** While the reverb self-drives (DECAY over unity), its return envelope pulls the dry bus back, so the sum the master shaper sees stays at the level the player set instead of escalating with the bloom.

**Architecture:** One new read-out on `AmbientReverb` (`return_level()`), one new gain stage in `Instrument::process` (armed by regime in `set_reverb_decay`, driven by the envelope at the control raster, slewed per sample, multiplied into the dry mix terms only). No new panel control, no VCV changes.

**Tech Stack:** C++17, doctest 2.4.11, CMake/Ninja desktop build (clang), spec at `docs/superpowers/specs/2026-08-03-reverb-bloom-duck-design.md`.

## Global Constraints

- Repo: `c:\Users\bernd\Documents\AI\Spotykach` (the fork). All paths below are relative to it.
- Build env: `source env.sh` first (clang + Ninja; the system g++ is an ARM cross-compiler and must never be used).
- Build/run tests: `cmake --build build --target spky_tests` then `./build/spky_tests.exe --test-case="<name>"`. Full suite: `./build/spky_tests.exe`.
- The VCV host is built ONLY via `./host/vcv/build-local.sh` — never hand-rolled.
- Every test is proven RED before it goes green. For pure regression guards the red is proven by the scripted mutation given in the task, then the mutation is reverted.
- By-ear values are untouchable: `kWetGain`, `kWetKnee`/`kWetRatio`, the MIX curves (`set_reverb_mix`), `kDefaultReverbMix`, the DECAY curve (`decay_loop_gain`), all master limiter constants.
- Engine code: no heap allocation, narrative comment style matching the surrounding files.
- Commit trailer (exact line, replaces any default):
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`

---

### Task 1: `AmbientReverb::return_level()`

**Files:**
- Modify: `engine/fx/reverb.h` (public section, after `limiter_gain()`, ~line 40)
- Test: `tests/test_reverb.cpp` (append)

**Interfaces:**
- Consumes: existing private members `_wet_peak`, `_lim_gain` (both already maintained by `process()`; `clear()` and `init()` already zero/reset them).
- Produces: `float AmbientReverb::return_level() const` — the level the room is currently handing the master: seconds-slow return peak × the ceiling's own ride. `0.f` on a fresh or cleared room. Task 2 reads this as the duck's envelope; the Task 3 test reads it directly off the shared test reverb.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_reverb.cpp`:

```cpp
TEST_CASE("reverb: return_level reads 0 fresh, rises with a bloom, forgets on clear") {
    s_rev.init(48000.f);
    CHECK(s_rev.return_level() == 0.f);
    s_rev.set_decay(1.f);                     // 110% loop gain: self-driving
    for (int i = 0; i < 48000 * 6; ++i) {
        float wl = 0.f, wr = 0.f;
        float in = (i < 4800) ? 0.5f : 0.f;   // 100 ms burst seeds the room
        s_rev.process(in, in, wl, wr);
    }
    // Post-ceiling plateau sits near 0.55 (knee 0.45 + overshoot/7). The
    // bound is loose on purpose: this pins "well above the duck threshold",
    // not a render checksum.
    CHECK(s_rev.return_level() > 0.3f);
    s_rev.clear();
    CHECK(s_rev.return_level() == 0.f);       // clear() forgets the ride
}
```

- [ ] **Step 2: Run it, expect the red**

```bash
source env.sh && cmake --build build --target spky_tests
```
Expected: compile error — `return_level` is not a member of `AmbientReverb`. That is this test's red.

- [ ] **Step 3: Implement the read-out**

In `engine/fx/reverb.h`, directly under `limiter_gain()`:

```cpp
    // The level the room is handing the master right now: the seconds-slow
    // return peak times the ceiling's own ride. The bloom duck (Instrument)
    // reads this as its envelope. 0.0 on a fresh or cleared room.
    float return_level() const { return _wet_peak * _lim_gain; }
```

- [ ] **Step 4: Run the test, expect green**

```bash
cmake --build build --target spky_tests && ./build/spky_tests.exe --test-case="reverb: return_level*"
```
Expected: PASS (3 assertions).

- [ ] **Step 5: Commit**

```bash
git add engine/fx/reverb.h tests/test_reverb.cpp
git commit -m "feat(reverb): the room reports what it hands the master

return_level() = the seconds-slow return peak times the ceiling's own
ride -- the number the bloom duck will read as its envelope. No new
state; clear() already forgets both factors.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: The duck core — envelope → dry gain (snap version, no arming yet)

**Files:**
- Modify: `engine/instrument.h` (observer near `excitation_bus()` ~line 133; members after `_rev_asleep` ~line 302)
- Modify: `engine/instrument.cpp` (constants in the anonymous namespace ~line 30; target computation inside the `if (_ctrl_ctr == 0)` block at ~line 97–116; dry-mix multiply at ~lines 206–207)
- Test: `tests/test_instrument.cpp` (append; reuses the existing `test_fx_mem()` helper and `s_ti_reverb` static)

**Interfaces:**
- Consumes: `AmbientReverb::return_level()` (Task 1); existing `_reverb`, `_ctrl_ctr`, dry mix terms `al*ga*dga` etc.
- Produces: `float Instrument::duck_gain() const` (observer, exactly `1.f` when idle); members `_duck_gain`, `_duck_target`; constants `kDuckThresh 0.30f`, `kDuckFull 0.60f`, `kDuckFloor 0.316f`. Task 3 adds `_duck_armed` into the target computation; Task 4 replaces the snap with a slew; Task 6 adds the resets.

- [ ] **Step 1: Write the two failing tests and the shared rig**

Append to `tests/test_instrument.cpp`:

```cpp
// ---- bloom duck (spec 2026-08-03-reverb-bloom-duck-design.md) ----

// A patch that reliably blooms: hot per-part sends, hot MIX, DECAY at the
// stop (110% loop gain). The parts' own generative output seeds the room.
static void duck_bloom_rig(Instrument& inst) {
    inst.init(48000.f, test_fx_mem());
    for (int p = 0; p < PART_COUNT; ++p)
        inst.set_fx_target_base(p, FXT_REV_SEND, 1.f);
    inst.set_reverb_mix(0.9f);
    inst.set_reverb_decay(1.f);
}

static void duck_render_blocks(Instrument& inst, int blocks) {
    std::vector<float> l(96), r(96);
    for (int i = 0; i < blocks; ++i)
        inst.process(nullptr, nullptr, l.data(), r.data(), 96);
}

TEST_CASE("instrument duck: a normal patch never ducks -- exactly 1.0") {
    Instrument inst;
    inst.init(48000.f, test_fx_mem());   // boot defaults: MIX 0.25, DECAY 0.55
    std::vector<float> l(96), r(96);
    for (int i = 0; i < 2500; ++i) {     // 5 s
        inst.process(nullptr, nullptr, l.data(), r.data(), 96);
        CHECK(inst.duck_gain() == 1.f);  // ==, not Approx: bit-transparent
    }
}

TEST_CASE("instrument duck: a bloom pulls the dry bus below 0.5") {
    Instrument inst;
    duck_bloom_rig(inst);
    std::vector<float> l(96), r(96);
    float min_gain = 1.f;
    for (int i = 0; i < 7500; ++i) {     // 15 s: swell + duck both engage
        inst.process(nullptr, nullptr, l.data(), r.data(), 96);
        min_gain = std::min(min_gain, inst.duck_gain());
    }
    // The rig's return envelope BREATHES (measured on this rig: ~0.39-0.58,
    // ~10 s period -- generative material; a wash fluctuates by nature), so
    // the gain at any single checkpoint straddles the line. The spec's
    // claim is "the gain falls below 0.5"; the render minimum pins exactly
    // that, and the fixed seeds make it deterministic.
    CHECK(min_gain < 0.5f);
}
```

- [ ] **Step 2: Run, expect the red**

```bash
cmake --build build --target spky_tests
```
Expected: compile error — `duck_gain` is not a member of `Instrument`. That is the red for both tests.

- [ ] **Step 3: Implement the snap duck**

`engine/instrument.cpp`, anonymous namespace, after `kMixSmoothS`:

```cpp
// Bloom duck (spec 2026-08-03-reverb-bloom-duck-design.md). While the room
// self-drives, its return envelope pulls the dry bus back so the sum the
// master sees stays at the level the player set. All ear-tunable.
constexpr float kDuckThresh = 0.30f;  // below: exactly 1.0 even when armed
constexpr float kDuckFull   = 0.60f;  // env at which the floor is reached
constexpr float kDuckFloor  = 0.316f; // -10 dB: makes room, does not mute
```

`engine/instrument.h`, private members, after `_rev_asleep`:

```cpp
    // Bloom duck (spec 2026-08-03-reverb-bloom-duck): while the room is over
    // unity loop gain its return envelope pulls the dry bus back. Exactly
    // 1.0 whenever it is not ducking -- guarded by a test, like the return
    // ceiling's limiter_gain().
    float _duck_gain = 1.f, _duck_target = 1.f;
```

`engine/instrument.h`, public observers, after `excitation_bus()`:

```cpp
    // Observer only, for tests: the bloom duck's gain on the dry bus. From
    // outside a duck is indistinguishable from quieter playing (the same
    // argument as limiter_gain()), so this is the only honest probe.
    float duck_gain() const { return _duck_gain; }
```

`engine/instrument.cpp`, inside the `if (_ctrl_ctr == 0)` block in `process()`, immediately before `_ctrl_ctr = Center::kCtrlInterval;`:

```cpp
            // Bloom duck target (spec 2026-08-03): feedforward from the
            // room's own return envelope -- seconds-slow by construction,
            // so it cannot pump the way the dead return-side rides did.
            if (_reverb) {
                const float env = _reverb->return_level();
                _duck_target = env <= kDuckThresh
                    ? 1.f
                    : 1.f - (1.f - kDuckFloor)
                          * std::min(1.f, (env - kDuckThresh)
                                              / (kDuckFull - kDuckThresh));
            } else {
                _duck_target = 1.f;
            }
            _duck_gain = _duck_target;   // snap; Task 4 replaces with a slew
```

`engine/instrument.cpp`, the per-deck dry mix inside the `if (_reverb)` branch (currently `l = al * ga * dga + bl * gb * dgb;` / `r = ...`): multiply the whole dry sum, and ONLY there — never scale `al/ar/bl/br` themselves, they feed `_dry_tap` (the excitation bus) and `_deck_tap`:

```cpp
            // Duck multiplies the dry SUM only. al/ar/bl/br must stay
            // untouched: they feed _dry_tap (BODY's excitation) and
            // _deck_tap, which must not starve when the bloom peaks.
            l = (al * ga * dga + bl * gb * dgb) * _duck_gain;
            r = (ar * ga * dga + br * gb * dgb) * _duck_gain;
```

`std::min` needs `<algorithm>` — `instrument.cpp` includes `<cmath>` only; add `#include <algorithm>` at the top.

- [ ] **Step 4: Run both tests, expect green**

```bash
cmake --build build --target spky_tests && ./build/spky_tests.exe --test-case="instrument duck:*"
```
Expected: both PASS. If "a bloom pulls the dry bus below 0.5" fails because the env never crossed the threshold, print `s_ti_reverb.return_level()` at the end of the render and check the rig actually blooms (DECAY must map to 110%); do not weaken the assertion.

- [ ] **Step 5: Run the whole instrument suite (no collateral)**

```bash
./build/spky_tests.exe --test-case="instrument*"
```
Expected: all PASS — the existing bit-identity test ("all FX off + send 0…") must still pass, since an idle duck multiplies by exactly 1.0.

- [ ] **Step 6: Commit**

```bash
git add engine/instrument.h engine/instrument.cpp tests/test_instrument.cpp
git commit -m "feat(instrument): the bloom pulls the dry bus back

The duck core: the room's return envelope, read at the control raster,
maps to a dry-bus gain -- exactly 1.0 below the threshold, sliding to
-10 dB at the settled-bloom level. Applied to the dry SUM only; the deck
variables stay untouched because the excitation and deck taps read them.
Snap for now: the slew and the regime arming are the next two tasks.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: Arming — the duck exists only in the bloom regime

**Files:**
- Modify: `engine/instrument.h` (`set_reverb_decay` ~line 113; one new member next to `_duck_gain`)
- Modify: `engine/instrument.cpp` (the duck-target condition from Task 2)
- Test: `tests/test_instrument.cpp` (append)

**Interfaces:**
- Consumes: `AmbientReverb::decay_loop_gain(float)` (public static, already exists — the tooltip reads it too); Task 2's target computation.
- Produces: member `bool _duck_armed = false;`. The target computation now requires `_reverb && _duck_armed`.

- [ ] **Step 1: Write the failing test**

The room is driven into a full bloom, then DECAY drops below unity while the room is still loud (the 4 s peak release holds the envelope up). Level alone would keep ducking; the regime bit must release it.

```cpp
TEST_CASE("instrument duck: sub-unity DECAY releases the duck even while the room is loud") {
    Instrument inst;
    duck_bloom_rig(inst);
    duck_render_blocks(inst, 7500);                  // 15 s: visibly ducked
    // Precondition only (the env breathes, ~0.39-0.58): any visible duck at
    // the disarm instant serves; the proof is the monotone rise below.
    REQUIRE(inst.duck_gain() < 0.9f);
    REQUIRE(s_ti_reverb.return_level() > 0.30f);     // env above the threshold
    inst.set_reverb_decay(0.75f);                    // loop gain ~0.94: player takes over
    float g = inst.duck_gain();
    const float g0 = g;
    bool monotone = true;
    std::vector<float> l(96), r(96);
    for (int i = 0; i < 500; ++i) {                  // 1 s
        inst.process(nullptr, nullptr, l.data(), r.data(), 96);
        if (inst.duck_gain() + 1e-9f < g) monotone = false;
        g = inst.duck_gain();
    }
    // The room is STILL over the threshold (4 s release), so a pure level
    // duck would hold: this line is what makes the red claim honest.
    CHECK(s_ti_reverb.return_level() > 0.30f);
    CHECK(monotone);
    // Snap-stage identity: with arming the target snaps to exactly 1.0 at
    // the disarm instant. (A "g > g0 + margin" form was measured to be
    // non-discriminating: draining the tank moves even a level-driven duck
    // by +0.12..+0.26 in this window.) Task 4 adapts this line to the slew.
    CHECK(g == 1.f);
    (void)g0;
}
```

- [ ] **Step 2: Run it, expect the red**

```bash
cmake --build build --target spky_tests && ./build/spky_tests.exe --test-case="instrument duck: sub-unity*"
```
Expected: FAIL — without arming the env (still > 0.30) keeps the target below 1, so `g > g0 + 0.05` (and likely `monotone`) go red.

- [ ] **Step 3: Implement the arming**

`engine/instrument.h`, member next to `_duck_gain`:

```cpp
    // Who controls the room: below unity loop gain the player does (never
    // duck, at any level -- an ordinary long room legitimately returns +6 dB
    // over its send, overlapping the bloom's +7.6 dB, so LEVEL cannot tell
    // them apart; 10961a0 measured that). Above unity the loop drives itself
    // and the duck takes the envelope. Not 02134e3's dead trim: the knob is
    // read as a regime bit here, never as a level.
    bool _duck_armed = false;
```

`engine/instrument.h`, `set_reverb_decay` becomes:

```cpp
    void set_reverb_decay(float n) {
        if (_reverb) _reverb->set_decay(n);
        // Same public curve the tooltip reads: one source, no drift. The
        // seconds-slow slew stands in for hysteresis at the unity point.
        _duck_armed = AmbientReverb::decay_loop_gain(n) > 1.f;
    }
```

(Verified: `instrument.h` includes `fx/reverb.h` at its top, so the full `AmbientReverb` type — and the static `decay_loop_gain` — is available inline.)

`engine/instrument.cpp`, the Task 2 condition gains the bit:

```cpp
            if (_reverb && _duck_armed) {
```

- [ ] **Step 4: Run the duck tests, expect green**

```bash
cmake --build build --target spky_tests && ./build/spky_tests.exe --test-case="instrument duck:*"
```
Expected: all three PASS. (The bloom rig sets DECAY 1.0 → loop gain 1.10 → armed, so Task 2's tests are unaffected.)

- [ ] **Step 5: Commit**

```bash
git add engine/instrument.h engine/instrument.cpp tests/test_instrument.cpp
git commit -m "feat(instrument): the duck arms on regime, not on level

Below unity loop gain the player owns the room's level and the duck
reads exactly 1.0 whatever that level is; above it the loop owns it and
the envelope ducks. decay_loop_gain() is the one curve, shared with the
tooltip. The test drops DECAY mid-bloom while the room is still over the
threshold -- red on any pure level duck.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: The slew — seconds in, seconds out

**Files:**
- Modify: `engine/instrument.h` (two coeff members next to `_duck_gain`)
- Modify: `engine/instrument.cpp` (constants; `init()`; replace the snap with a per-sample slew next to the dry mix)
- Test: `tests/test_instrument.cpp` (append)

**Interfaces:**
- Consumes: Task 2's `_duck_target` (control raster) and dry-mix multiply.
- Produces: members `float _duck_down = 0.f, _duck_up = 0.f;`, constants `kDuckDownS 1.5f`, `kDuckUpS 4.0f`. `_duck_gain` now moves at most `coeff × (1−kDuckFloor)` per sample (~9.5e-6 at 48 kHz).

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("instrument duck: the gain never steps -- per-sample delta is slew-bounded") {
    Instrument inst;
    duck_bloom_rig(inst);
    inst.set_reverb_decay(0.5f);            // start sub-unity: duck idle at 1.0
    duck_render_blocks(inst, 2500);         // 5 s of material into the room
    inst.set_reverb_decay(1.f);             // jump into the bloom regime
    std::vector<float> l(1), r(1);
    float prev = inst.duck_gain(), max_delta = 0.f;
    for (int i = 0; i < 48000 * 10; ++i) {  // 10 s, per-sample
        inst.process(nullptr, nullptr, l.data(), r.data(), 1);
        max_delta = std::max(max_delta, std::fabs(inst.duck_gain() - prev));
        prev = inst.duck_gain();
    }
    REQUIRE(inst.duck_gain() < 0.9f);       // the duck did engage during the window
    // 1-exp(-1/(1.5 s * 48k)) * (1-0.316) = 9.5e-6; bound leaves 2x slack.
    CHECK(max_delta < 2e-5f);
}
```

- [ ] **Step 2: Run it, expect the red**

```bash
cmake --build build --target spky_tests && ./build/spky_tests.exe --test-case="instrument duck: the gain never steps*"
```
Expected: FAIL — the snap moves the gain by the full target change at a control boundary (delta ~0.1 or larger).

- [ ] **Step 3: Implement the slew**

`engine/instrument.cpp`, constants, after `kDuckFloor`:

```cpp
// SLOW, seconds not milliseconds, both directions: the reverb work measured
// (twice) that fast gain rides on a wash read as dirt, not level. Down a
// little faster than the 2-3 s swell it answers; up slower still.
constexpr float kDuckDownS = 1.5f;
constexpr float kDuckUpS   = 4.0f;
```

`engine/instrument.h`, members next to `_duck_gain`:

```cpp
    float _duck_down = 0.f, _duck_up = 0.f;   // asymmetric slew, set in init()
```

`engine/instrument.cpp`, in `init(float, const FxMem&)`, after `_limiter.init();`:

```cpp
    _duck_down = 1.f - std::exp(-1.f / (kDuckDownS * sample_rate));
    _duck_up   = 1.f - std::exp(-1.f / (kDuckUpS * sample_rate));
```

`engine/instrument.cpp`, in `process()`: delete the `_duck_gain = _duck_target;` snap line from the control-raster block, and add the per-sample slew inside the `if (_reverb)` branch immediately before the ducked dry mix:

```cpp
            // Per-sample ride toward the raster target. When idle both are
            // exactly 1.0 and this is a multiply-add by zero.
            _duck_gain += (_duck_target < _duck_gain ? _duck_down : _duck_up)
                          * (_duck_target - _duck_gain);
```

- [ ] **Step 4: Adapt the Task 3 disarm test to the slew**

The snap-stage identity `CHECK(g == 1.f)` cannot hold under the slew: a
float one-pole approaches 1.0 asymptotically and stalls below it when the
per-sample increment rounds under half an ulp. The arming feature keeps its
red-proof from Task 3 (proven once, against the snap); under the slew the
test becomes a regression guard. In the Task 3 test, extend the post-disarm
render from 500 to 2000 blocks (4 s) and replace the identity:

```cpp
    // Slew-stage form (Task 4): the snap identity g == 1.f is unreachable
    // under a float one-pole. Arming kept its red-proof against the snap;
    // this guards the release ride: from any breathing g0 <= ~0.8, four
    // seconds of the 4 s up-slew rise at least (1-g0)*(1-1/e) > 0.1.
    CHECK(monotone);
    CHECK(g > g0 + 0.1f);
```

(The pre-disarm `REQUIRE(s_ti_reverb.return_level() > 0.30f)` stays; the
POST-window copy moves its evaluation point to the 1 s mark — measured on
this rig the env crosses 0.30 on its own by ~3.1 s (0.301 @ 1500 blocks,
0.261 @ 1999), so at 4 s the honesty check would fail for reasons that
have nothing to do with the duck. Monotone tracking stays; delete the
`(void)g0;`.)

- [ ] **Step 5: Run every duck test, expect green**

```bash
cmake --build build --target spky_tests && ./build/spky_tests.exe --test-case="instrument duck:*"
```
Expected: all four PASS. The 15 s renders in Tasks 2/3 were sized for the slewed version, so they hold.

- [ ] **Step 6: Commit**

```bash
git add engine/instrument.h engine/instrument.cpp tests/test_instrument.cpp
git commit -m "feat(instrument): the duck rides in seconds, both directions

Asymmetric one-pole between raster targets: 1.5 s into the duck (the
swell it answers takes 2-3 s), 4 s back out. The step test renders per
sample and bounds the delta at the slew rate -- red against the snap.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: Purity guards — the send, the return, and the taps stay unducked

**Files:**
- Test: `tests/test_instrument.cpp` (append; no production code expected)

**Interfaces:**
- Consumes: `duck_bloom_rig`, `duck_render_blocks`, `Instrument::excitation_bus(int)`, `Instrument::deck_tap(int,int)`, `Instrument::duck_gain()`.
- Produces: two regression guards later refactors must keep green.

- [ ] **Step 1: Write the wet-solo guard**

```cpp
TEST_CASE("instrument duck: wet-solo bloom settles at the plateau, not the floor") {
    Instrument inst;
    duck_bloom_rig(inst);
    inst.set_reverb_mix(1.f);   // exact endpoint: dry gain 0, output is the return
    std::vector<float> l(96), r(96);
    float peak = 0.f;
    for (int i = 0; i < 7500; ++i) {        // 15 s; measure the last 2 s
        inst.process(nullptr, nullptr, l.data(), r.data(), 96);
        if (i >= 6500)
            for (int k = 0; k < 96; ++k)
                peak = std::max(peak, std::fabs(l[k]));
    }
    // The settled return sits ~0.5-0.6 (post ceiling, master transparent at
    // drive 0). If the duck ever multiplied the send or the return, this
    // lands near plateau * floor ~= 0.17.
    CHECK(peak > 0.35f);
}
```

- [ ] **Step 2: Write the tap-purity guard**

Two renders differing ONLY in DECAY. The dry path is entirely upstream of the reverb, so every part-output observer must be bit-identical between them — the engine is seeded and deterministic. Sequential (not interleaved) because both instruments would otherwise share `s_ti_reverb` and the tape buffers.

```cpp
static std::vector<float> duck_purity_trace(float decay_norm, float* duck_at_end) {
    Instrument inst;
    duck_bloom_rig(inst);
    inst.set_reverb_decay(decay_norm);
    std::vector<float> trace;
    std::vector<float> l(96), r(96);
    for (int i = 0; i < 7500; ++i) {
        inst.process(nullptr, nullptr, l.data(), r.data(), 96);
        trace.push_back(inst.excitation_bus(PART_A));
        trace.push_back(inst.excitation_bus(PART_B));
        trace.push_back(inst.deck_tap(PART_A, 0));
        trace.push_back(inst.deck_tap(PART_B, 1));
    }
    if (duck_at_end) *duck_at_end = inst.duck_gain();
    return trace;
}

TEST_CASE("instrument duck: excitation and deck taps are bit-identical with and without a duck") {
    float duck_quiet = 0.f, duck_bloom = 0.f;
    auto quiet = duck_purity_trace(0.55f, &duck_quiet);
    auto bloom = duck_purity_trace(1.f, &duck_bloom);
    REQUIRE(duck_quiet == 1.f);            // control render never ducked
    REQUIRE(duck_bloom < 0.9f);            // precondition: visibly ducked at the end
    REQUIRE(quiet.size() == bloom.size());
    for (size_t i = 0; i < quiet.size(); ++i)
        CHECK(quiet[i] == bloom[i]);       // ==, the decks never saw the duck
}
```

- [ ] **Step 3: Prove both red by mutation, then revert**

These are guards; their feature already exists. Prove each can fail:

1. In `instrument.cpp`, temporarily change the wet join to `l += wl * _duck_gain;` (and `r`). Rebuild, run `--test-case="instrument duck: wet-solo*"` → must FAIL. Revert.
2. Temporarily scale the deck variables in place after the part loop: `const_cast` not needed — just add `al *= _duck_gain; ar *= _duck_gain;` requires them non-const; instead change their declaration line to non-const for the mutation: `float al = pl[PART_A] * _duck_gain, ar = prr[PART_A] * _duck_gain;` (leaving `bl/br` alone is enough). Rebuild, run `--test-case="instrument duck: excitation*"` → must FAIL. Revert.

Both reverts verified by `git diff --stat engine/` showing no engine changes.

- [ ] **Step 4: Run green, then the full suite**

```bash
cmake --build build --target spky_tests && ./build/spky_tests.exe --test-case="instrument duck:*" && ./build/spky_tests.exe
```
Expected: all duck tests PASS; full suite PASS.

- [ ] **Step 5: Commit**

```bash
git add tests/test_instrument.cpp
git commit -m "test(instrument): the duck may touch nothing but the dry sum

Two guards, each proven red by a scripted mutation: a wet-solo bloom
settles at the return plateau (red if the send or return is ever
ducked), and the excitation/deck taps are bit-identical between a
ducked and an unducked render (red if al/ar/bl/br are scaled in place).

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: Resets — init and the first-block prime forget the duck

**Files:**
- Modify: `engine/instrument.cpp` (`init()`; the `!_rev_primed` block in `process()`)
- Test: `tests/test_instrument.cpp` (append)

**Interfaces:**
- Consumes: Task 2's members; the existing `_rev_primed` snap block.
- Produces: after `init()` or the first processed block, `duck_gain() == 1.f` exactly.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("instrument duck: re-init forgets the duck") {
    Instrument inst;
    duck_bloom_rig(inst);
    duck_render_blocks(inst, 7500);
    REQUIRE(inst.duck_gain() < 0.9f);       // precondition: visibly ducked
    duck_bloom_rig(inst);                   // re-init: a new patch starts clean
    CHECK(inst.duck_gain() == 1.f);         // exactly, before any process()
}
```

- [ ] **Step 2: Run it, expect the red**

```bash
cmake --build build --target spky_tests && ./build/spky_tests.exe --test-case="instrument duck: re-init*"
```
Expected: FAIL — members are set at construction, not in `init()`, so the ducked gain survives re-init.

- [ ] **Step 3: Implement the resets**

`engine/instrument.cpp`, in `init(float, const FxMem&)`, next to the `_rev_primed = false;` line:

```cpp
    _duck_gain = 1.f;
    _duck_target = 1.f;
    _duck_armed = false;   // set_reverb_decay() re-arms on the next param push
```

And in `process()`, inside the `if (!_rev_primed)` block (belt and braces with init, same policy as the mix snap):

```cpp
                _duck_gain = 1.f;
                _duck_target = 1.f;
```

- [ ] **Step 4: Run all duck tests, expect green**

```bash
cmake --build build --target spky_tests && ./build/spky_tests.exe --test-case="instrument duck:*"
```
Expected: all PASS. Note `duck_bloom_rig` calls `set_reverb_decay(1.f)` after `init()`, so `_duck_armed = false` in init does not break the rig.

- [ ] **Step 5: Commit**

```bash
git add engine/instrument.cpp tests/test_instrument.cpp
git commit -m "fix(instrument): a re-initialised instrument starts unducked

init() and the first-block prime both snap the duck to exactly 1.0, the
same policy as the mix smoothers: a new patch never inherits the last
bloom's dry attenuation.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 7: Full verification and the listening check

**Files:**
- No source changes expected.

**Interfaces:**
- Consumes: everything above.
- Produces: a green full suite, a fresh VCV build for Bastian's ears.

- [ ] **Step 1: Full desktop suite**

```bash
source env.sh && cmake --build build --target spky_tests && ./build/spky_tests.exe
```
Expected: every test PASSES, including the pre-existing reverb, limiter, deck-bus and instrument bit-identity tests. Any failure is investigated, not waived.

- [ ] **Step 2: Build the VCV host**

```bash
./host/vcv/build-local.sh
```
Expected: plugin builds and installs to the local Rack plugins dir. (Never hand-roll this build — the system g++ is the ARM cross-compiler.)

- [ ] **Step 3: Hand over for the by-ear check**

Report to Bastian, explicitly:
- Load `cliping_test.vcvm`, DRIVE at the usual 0.3–0.6, DECAY to the stop, listen for the bloom to swell while the master's dirt stays at the pre-bloom amount; the mix should step back ~10 dB over ~1.5 s and come back over ~4 s when DECAY drops.
- The five duck constants (`kDuckThresh`, `kDuckFull`, `kDuckFloor`, `kDuckDownS`, `kDuckUpS`) sit together at the top of `engine/instrument.cpp`, ear-tunable — record any change he lands on as a by-ear decision.
- Known, documented limit (spec "What this deliberately does not fix"): at DRIVE ≥ ~0.4 the bloom alone still crosses the shaper knee; the duck holds the dirt constant, it does not clean it.

- [ ] **Step 4: Nothing to commit here** — this task only verifies. If steps 1–2 forced any fix, it was committed under the task that owned the code.
