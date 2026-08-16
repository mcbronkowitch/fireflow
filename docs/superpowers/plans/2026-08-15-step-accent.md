# STEP Accent Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give a STEP deck per-note velocity and per-note decay, both derived
from the groove rank so that their intensity follows DENSE without any new
control.

**Architecture:** `ModLane` computes one normalized accent `a` per note from
`GrooveCell::rank_of_slot` against the cell length, `Part` pushes it to the
active engine through a new no-op-default virtual, and `SynthEngineT` spends it
in two independent places — a velocity factor at trigger time and a decay scale
latched into the voice. FLOW is excluded at the source, so no consumer carries
a mode test.

**Tech Stack:** C++17, clang + Ninja, doctest. No new dependencies.

**Spec:** `docs/superpowers/specs/2026-08-15-step-accent-design.md`

## Global Constraints

- **Build with `-DCMAKE_BUILD_TYPE=Release`.** A fresh configure defaults to
  Debug and the render-hash gates then fail for an unrelated reason. Source
  `env.sh` first; never use MSVC, never source `env.sh` in a shell used for
  `shell/` or `bench/`.
- **Everything written into the repo is English** — code, comments, commit
  messages, docs.
- Commit trailer is `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **Never prefix a shell command with `cd`.** The tool already starts in the
  repo root.
- `kAccentVelFloor = 0.3f` and `kAccentDecFloor = 0.3f` are **by-ear tuning
  values**, deliberately equal on the first try. No gate may depend on the
  literal `0.3` — derive expectations from the named constants.
- A test that cannot go red gets fixed. Prove the RED once, and say in the
  commit what the RED said.
- **`ModLane::set_step()` does not regenerate the groove — the next cycle wrap
  does.** Any test that observes groove-derived values must run the lane past
  a wrap first, or it measures the previous cell.

**Build and test commands used throughout:**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/spky_tests -ts="<test suite or case filter>"
```

---

### Task 1: The accent value in the lane

**Files:**
- Modify: `engine/mod/lane.h` — one member, one accessor
- Modify: `engine/mod/lane.cpp` — `_start_note()`, `set_step()`
- Create: `tests/test_step_accent.cpp`
- Modify: `CMakeLists.txt` — add the new test file after `tests/test_gate_density.cpp` (line 61)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `float ModLane::note_accent() const` — 0 in FLOW, and in STEP the
  value `rank_of_slot[slot % L] / (L - 1)` of the note that most recently
  started, where `L` is `pattern_groove.len`. `0.f` means the anchor / full
  strength; `1.f` means the last note DENSE reveals.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_step_accent.cpp`:

```cpp
// The STEP accent: per-note velocity and decay from the groove rank.
// Spec: docs/superpowers/specs/2026-08-15-step-accent-design.md
//
// Setup note: set_step() does NOT regenerate the groove -- the next cycle
// wrap does. Every helper below therefore runs the lane past a wrap before it
// believes anything it reads, and the accents are collected over a whole
// cycle delimited by wrap_count_for_test(), never by a hand-computed sample
// count (the STEP clock scales the cycle by 8/steps, so a fixed count is a
// different fraction of a cycle at every STEPS value).
#include <doctest/doctest.h>
#include "mod/lane.h"
#include <algorithm>
#include <set>
#include <vector>

using namespace spky;

namespace {

// A note deck in STEP: melodic AND flow_melody, which is what Part pushes for
// SYNTH/WAVE/BODY. set_melodic() BEFORE init() -- docs/engine-map.md section 6.
ModLane note_step(uint32_t seed, int steps) {
    ModLane l;
    l.set_melodic(true);
    l.init(48000.f, seed);
    l.set_flow_melody(true);
    l.set_step(true, steps);
    l.set_rate_hz(0.5f);
    l.set_shape(0.f);
    l.set_smooth(0.f);
    l.set_range(1.f);
    l.set_variation(0.f);
    return l;
}

constexpr int kSampleCap = 8'000'000;   // ~166 s: far past any cycle here

void run_to_wrap(ModLane& l, uint32_t target) {
    for (int i = 0; i < kSampleCap; ++i) {
        l.process();
        if (l.wrap_count_for_test() >= target) return;
    }
    FAIL("lane never reached the requested wrap count");
}

// The accents emitted over exactly one cycle, in fire order, measured after
// the groove has settled.
std::vector<float> accents_in_cycle(ModLane& l) {
    run_to_wrap(l, 2);
    const uint32_t end = l.wrap_count_for_test() + 1;
    std::vector<float> out;
    for (int i = 0; i < kSampleCap; ++i) {
        l.process();
        if (l.fired()) out.push_back(l.note_accent());
        if (l.wrap_count_for_test() >= end) return out;
    }
    FAIL("lane never wrapped while collecting");
    return out;
}

const int kStepSet[] = {4, 8, 16};
const uint32_t kSeeds[] = {999u, 12345u, 7u, 4242u};

}  // namespace

TEST_CASE("accent G1: the anchor is at full strength, whatever DENSE is") {
    for (int steps : kStepSet) {
        for (uint32_t seed : kSeeds) {
            CAPTURE(steps);
            CAPTURE(seed);

            ModLane sparse = note_step(seed, steps);
            sparse.set_density(0.f);
            std::vector<float> a_sparse = accents_in_cycle(sparse);
            REQUIRE(a_sparse.size() == 1);          // k == 1: only the anchor fires
            CHECK(a_sparse[0] == doctest::Approx(0.f));

            // The contrast is part of the gate, not decoration: without it a
            // stub returning a constant 0 would pass this case.
            ModLane dense = note_step(seed, steps);
            dense.set_density(1.f);
            std::vector<float> a_dense = accents_in_cycle(dense);
            REQUIRE(!a_dense.empty());
            CHECK(*std::max_element(a_dense.begin(), a_dense.end()) > 0.9f);
        }
    }
}

TEST_CASE("accent G2: at DENSE 1 the contour is the whole rank scale") {
    for (int steps : kStepSet) {
        for (uint32_t seed : kSeeds) {
            CAPTURE(steps);
            CAPTURE(seed);
            ModLane l = note_step(seed, steps);
            l.set_density(1.f);
            std::vector<float> a = accents_in_cycle(l);

            // One note per step: this is also what pins L == the STEPS count.
            REQUIRE(a.size() == static_cast<size_t>(steps));
            std::set<float> uniq(a.begin(), a.end());
            CHECK(uniq.size() == static_cast<size_t>(steps));   // a permutation
            CHECK(*uniq.begin() == doctest::Approx(0.f));
            CHECK(*uniq.rbegin() == doctest::Approx(1.f));
        }
    }
}

TEST_CASE("accent G3: FLOW reports 0, including right after leaving STEP") {
    ModLane l = note_step(12345u, 8);
    l.set_density(1.f);
    run_to_wrap(l, 2);

    bool saw_nonzero = false;
    for (int i = 0; i < 400000; ++i) {
        l.process();
        if (l.fired() && l.note_accent() != 0.f) saw_nonzero = true;
    }
    REQUIRE(saw_nonzero);      // the STEP leg really produced accents

    l.set_step(false, 8);
    bool leaked = false;
    for (int i = 0; i < 400000; ++i) {
        l.process();
        if (l.note_accent() != 0.f) leaked = true;
    }
    CHECK_FALSE(leaked);
}
```

Add to `CMakeLists.txt`, immediately after the `tests/test_gate_density.cpp`
line:

```cmake
    tests/test_step_accent.cpp
```

- [ ] **Step 2: Run to verify it fails to build**

```bash
source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Expected: compile error, `no member named 'note_accent' in 'spky::ModLane'`.

- [ ] **Step 3: Add a deliberate stub so the RED is behavioural, not a compile error**

A compile error is not a proof that the test measures anything. Add to
`engine/mod/lane.h`, in the public observer block next to `note_sustain()`:

```cpp
    // STEP accent: 0 = the rank-0 anchor at full strength, 1 = the last note
    // DENSE reveals. Spec 2026-08-15-step-accent-design.md section 3.
    float note_accent() const { return 0.f; }   // STUB -- Step 5 replaces this
```

- [ ] **Step 4: Run the tests to see them fail for the right reason**

```bash
cmake --build build && ./build/spky_tests -tc="accent G*"
```

Expected: **G1 fails** on `*std::max_element(...) > 0.9f` (the dense contrast),
**G2 fails** on `*uniq.rbegin() == Approx(1.0)`, **G3 fails** on
`REQUIRE(saw_nonzero)`. Copy the three failure lines into the Step 8 commit
message — that is the RED proof.

- [ ] **Step 5: Implement the accent**

In `engine/mod/lane.h`, replace the stub with the guarded accessor and add the
member. The accessor:

```cpp
    // STEP accent: 0 = the rank-0 anchor at full strength, 1 = the last note
    // DENSE reveals. Spec 2026-08-15-step-accent-design.md section 3.
    //
    // The _step_mode guard is not redundant with the reset in set_step():
    // lanes fire in FLOW too and Part::_fire_trigger() runs on those fires, so
    // without it a deck leaving STEP would push its last STEP note's accent
    // into its drone. One mode test at the one place that cannot be bypassed.
    float note_accent() const { return _step_mode ? _note_accent : 0.f; }
```

The member, next to `_note_age` (`lane.h:340`):

```cpp
    float _note_accent = 0.f;   // groove rank of the running note, normalized
```

In `engine/mod/lane.cpp`, inside `_start_note(int slot)` — after
`groove_length` is computed, which the function already does for `note_len`:

```cpp
    // The groove rank IS the accent. Normalized against the cell length rather
    // than against the DENSE depth k, which is what makes a thin pattern loud:
    // at k == 1 only the rank-0 anchor fires, so a == 0 by construction and no
    // separate depth control has to enforce it (spec section 2).
    _note_accent =
        groove_length > 1
            ? static_cast<float>(pattern.pattern_groove.rank_of_slot[slot % groove_length])
                  / static_cast<float>(groove_length - 1)
            : 0.f;
```

In `engine/mod/lane.cpp`, `set_step()`, extend the existing mode-change reset
(the `if (mode_changed)` line):

```cpp
    if (mode_changed) { _cur_step = -1; _frozen = false; _note_accent = 0.f; _prime_floors(); }
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests -tc="accent G*"
```

Expected: PASS, 3 test cases.

- [ ] **Step 7: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: everything green. Nothing else reads `note_accent()` yet, so any
failure here is a real regression in `_start_note` or `set_step`.

- [ ] **Step 8: Commit**

```bash
git add engine/mod/lane.h engine/mod/lane.cpp tests/test_step_accent.cpp CMakeLists.txt
git commit -m "feat(mod): per-note accent from the groove rank

..."
```

---

### Task 2: The accent reaches the engine

**Files:**
- Modify: `engine/parts/engine_iface.h` — new virtual on `IPartEngine`
- Modify: `engine/mod/super_modulator.h` — forwarder next to `pitch_steps()` (line 90)
- Modify: `engine/parts/part.cpp` — `_fire_trigger()` (line 471)
- Modify: `engine/synth/synth_engine.h` — `set_accent` override, `_accent`, `accent_for_test()`
- Modify: `engine/synth/synth_engine.cpp` — `set_accent` body
- Modify: `tests/test_step_accent.cpp` — append the seam test

**Interfaces:**
- Consumes: `float ModLane::note_accent() const` (Task 1).
- Produces:
  - `virtual void IPartEngine::set_accent(float /*a*/) {}` — no-op default.
  - `float SuperModulator::pitch_note_accent() const`
  - `void SynthEngineT<V>::set_accent(float a)` — clamps to `0..1`, stores.
  - `float SynthEngineT<V>::accent_for_test() const` — `SPKY_TESTING` only.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_step_accent.cpp`:

```cpp
#include "parts/part.h"

TEST_CASE("accent: a STEP deck pushes its note accent into the active engine") {
    Part part;
    part.init(48000.f, 0xabcd1234u);          // null FX memory is fine here
    part.set_engine(ENGINE_SYNTH);
    part.mod().set_tempo_bpm(120.f);
    part.mod().set_rate(0.8f);
    part.mod().set_density(1.f);
    part.set_step(true, 8);

    float l = 0.f, r = 0.f;
    float seen_max = 0.f;
    bool seen_any = false;
    for (int i = 0; i < 48000 * 8; ++i) {
        part.process(l, r);
        const float a = part.synth().accent_for_test();
        if (a > 0.f) { seen_any = true; seen_max = std::max(seen_max, a); }
    }
    CHECK(seen_any);              // the push happens at all
    CHECK(seen_max > 0.9f);       // and it carries the whole range, not a floor

    // FLOW must not push a stale accent into the drone.
    part.set_step(false, 8);
    for (int i = 0; i < 48000; ++i) part.process(l, r);
    float flow_max = 0.f;
    for (int i = 0; i < 48000 * 4; ++i) {
        part.process(l, r);
        flow_max = std::max(flow_max, part.synth().accent_for_test());
    }
    CHECK(flow_max == doctest::Approx(0.f));
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build
```

Expected: compile error, `no member named 'accent_for_test'`.

- [ ] **Step 3: Add the interface and the storage, but NOT the push**

This is deliberate: it makes the next run a behavioural RED instead of a
compile error, and it proves the test is measuring the push rather than the
storage.

`engine/parts/engine_iface.h`, after `set_width` (line 117):

```cpp
    // STEP accent, pushed by Part::_fire_trigger() immediately before
    // trigger_chord(): 0 = the groove's rank-0 anchor at full strength, 1 =
    // the last note DENSE reveals, and always 0 in FLOW. Default no-op, the
    // set_excitation idiom -- SAMPLER has no per-note envelope and the BBD's
    // DECAY is not a note property, so both take the default on purpose
    // (spec 2026-08-15-step-accent-design.md section 7).
    virtual void set_accent(float /*a*/) {}
```

`engine/mod/super_modulator.h`, after `pitch_steps()` (line 90):

```cpp
    float pitch_note_accent() const { return _lanes[LANE_PITCH].note_accent(); }
```

`engine/synth/synth_engine.h` — declare next to `set_decay` (line 88):

```cpp
    void set_accent(float a) override;   // STEP accent, 0..1 (0 = full strength)
```

a private member next to `_decay_ratio` (line 163):

```cpp
    float _accent = 0.f;           // STEP accent of the note being struck
```

and inside the existing `#ifdef SPKY_TESTING` observer block:

```cpp
    float accent_for_test() const { return _accent; }
```

`engine/synth/synth_engine.cpp`, next to `set_decay` (line 389):

```cpp
template <class V>
void SynthEngineT<V>::set_accent(float a) { _accent = clampf(a, 0.f, 1.f); }
```

- [ ] **Step 4: Run the test to see it fail behaviourally**

```bash
cmake --build build && ./build/spky_tests -tc="accent: a STEP deck*"
```

Expected: FAIL on `CHECK(seen_any)` — the engine stores an accent nobody sends
it. Copy that line into the Step 7 commit message.

- [ ] **Step 5: Add the push**

`engine/parts/part.cpp`, in `_fire_trigger()`, immediately before
`_engine->trigger_chord(chord, nch)`:

```cpp
    // Before the strike, like set_material_character inside the engine: the
    // note being triggered must be struck with ITS OWN accent, not with the
    // one the previous fire left behind.
    _engine->set_accent(_mod.pitch_note_accent());
```

- [ ] **Step 6: Run the tests**

```bash
cmake --build build && ./build/spky_tests -tc="accent*" && ctest --test-dir build --output-on-failure
```

Expected: all accent cases PASS and the full suite green. Two things to look
at specifically, because they are the ones this task could break: the
`part_engine_contract` cases (a new virtual on `IPartEngine`) and
`test_synth_engine_voice_count` (`SynthEngineT` gained a member).

- [ ] **Step 7: Commit**

```bash
git add engine/parts/engine_iface.h engine/mod/super_modulator.h engine/parts/part.cpp \
        engine/synth/synth_engine.h engine/synth/synth_engine.cpp tests/test_step_accent.cpp
git commit -m "feat(parts): push the STEP accent to the active engine

..."
```

---

### Task 3: VEL — the accent scales the strike

**Files:**
- Modify: `engine/synth/synth_engine.h` — the `kAccentVelFloor` constant
- Modify: `engine/synth/synth_engine.cpp` — `_do_trigger()` (line 218)
- Modify: `tests/test_step_accent.cpp` — append the audio gates

**Interfaces:**
- Consumes: `SynthEngineT::_accent`, `accent_for_test()` (Task 2).
- Produces: `static constexpr float SynthEngineT<V>::kAccentVelFloor` — the
  velocity a fully-accented (weakest) note strikes at, relative to the anchor.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_step_accent.cpp`:

```cpp
#include "synth_engine_contract.h"   // spky_contract::fresh / render_l
#include <cmath>

namespace {

// Peak of one struck note, in STEP, at a given accent. Everything except the
// accent is identical between calls, so the ratio of two of these isolates
// exactly what the accent did.
template <class EngineT>
float note_peak(uint32_t seed, float accent, int n_chord = 1) {
    EngineT e;
    spky_contract::fresh(e, seed);
    e.set_flow(false);                       // STEP: struck notes, no drone
    e.set_accent(accent);
    const float chord[3] = {0.35f, 0.5f, 0.65f};
    e.trigger_chord(chord, n_chord);
    std::vector<float> buf = spky_contract::render_l(e, 48000);
    float pk = 0.f;
    for (float v : buf) pk = std::max(pk, std::fabs(v));
    return pk;
}

}  // namespace

TEST_CASE("accent G4: the accent scales a struck note down to the VEL floor") {
    const float loud = note_peak<SynthEngine>(99u, 0.f);
    const float soft = note_peak<SynthEngine>(99u, 1.f);
    REQUIRE(loud > 1e-4f);                   // the reference note actually sounded
    CHECK(soft / loud
          == doctest::Approx(SynthEngine::kAccentVelFloor).epsilon(0.05));
}

TEST_CASE("accent G4: WAVE gets the same scaling as SYNTH") {
    // Both are VoiceT instantiations, so this is cheap; it exists so that a
    // future engine added to the SynthEngineT family cannot quietly miss the
    // accent while SYNTH keeps the gate green.
    const float loud = note_peak<WaveEngine>(99u, 0.f);
    const float soft = note_peak<WaveEngine>(99u, 1.f);
    REQUIRE(loud > 1e-4f);
    CHECK(soft / loud
          == doctest::Approx(WaveEngine::kAccentVelFloor).epsilon(0.05));
}

TEST_CASE("accent G6: the accent multiplies onto the chord compensation") {
    // If the accent REPLACED the 1/sqrt(n) equal-power compensation instead of
    // composing with it, the chord's accent ratio would differ from the solo
    // note's. That it does not is the whole claim.
    const float solo = note_peak<SynthEngine>(99u, 1.f, 1)
                     / note_peak<SynthEngine>(99u, 0.f, 1);
    const float chord = note_peak<SynthEngine>(99u, 1.f, 3)
                      / note_peak<SynthEngine>(99u, 0.f, 3);
    REQUIRE(note_peak<SynthEngine>(99u, 0.f, 3) > 1e-4f);
    CHECK(chord == doctest::Approx(solo).epsilon(0.05));
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build
```

Expected: compile error, `no member named 'kAccentVelFloor'`.

- [ ] **Step 3: Add the constant only**

`engine/synth/synth_engine.h`, in the public section near the other tuning
constants:

```cpp
    // How hard the WEAKEST note of a full pattern strikes, relative to the
    // rank-0 anchor. By ear, first try, deliberately equal to
    // kAccentDecFloor so a listening session says which half wants to differ.
    static constexpr float kAccentVelFloor = 0.3f;
```

- [ ] **Step 4: Run the tests to see them fail behaviourally**

```bash
cmake --build build && ./build/spky_tests -tc="accent G4" -tc="accent G6"
```

Expected: **G4 fails** — `soft / loud` is `1.0`, not `0.3`: the accent is
stored and never spent. **G6 passes vacuously** at this point (both ratios are
1.0), which is why it is not the gate that proves the feature — note this in
the commit message so a later reader knows G6 alone is not a proof.

- [ ] **Step 5: Spend the accent**

`engine/synth/synth_engine.cpp`, in `_do_trigger()`, replace the
`_voices[pick].set_vel(vel);` line with:

```cpp
    // The accent MULTIPLIES onto vel rather than replacing it: vel carries the
    // 1/sqrt(n) equal-power chord compensation, which answers "how many notes
    // are sounding", while the accent answers "how strong is this one". Two
    // independent quantities, so they compose. At _accent == 0 this is exactly
    // the value that shipped before.
    _voices[pick].set_vel(vel * (1.f - (1.f - kAccentVelFloor) * _accent));
```

- [ ] **Step 6: Run the tests**

```bash
cmake --build build && ./build/spky_tests -tc="accent*"
```

Expected: PASS. If G4's ratio lands outside the 5 % window, do **not** widen
the window — check first that `fresh()`'s level and cycle are not putting the
note into the limiter, where the ratio would legitimately compress.

- [ ] **Step 7: Run the whole suite, and read the render hashes carefully**

```bash
ctest --test-dir build --output-on-failure
```

Expected: green, **including `ctrl_identity` and `wave_formant_sweep`
unchanged**. Both scenarios run in FLOW, where the accent is 0 — if either
hash moves, that is a finding (the accent is reaching a FLOW deck), not a
baseline to bump. Do not re-baseline; stop and report.

- [ ] **Step 8: Commit**

```bash
git add engine/synth/synth_engine.h engine/synth/synth_engine.cpp tests/test_step_accent.cpp
git commit -m "feat(synth): the STEP accent scales the strike

..."
```

---

### Task 4: DEC — the accent shortens the note, with room from the DEC knob

**Files:**
- Modify: `engine/synth/voice.h`, `engine/synth/voice.cpp` — `set_decay_scale`
- Modify: `engine/body/body_voice.h`, `engine/body/body_voice.cpp` — the same
- Modify: `engine/synth/synth_engine.h` — `kAccentDecFloor`, `_decay_n`, the `VoiceCountProbe` stub
- Modify: `engine/synth/synth_engine.cpp` — `set_decay()`, `_do_trigger()`
- Modify: `tests/test_step_accent.cpp` — append G5

**Interfaces:**
- Consumes: `SynthEngineT::_accent` (Task 2), `kAccentVelFloor` (Task 3).
- Produces:
  - `void VoiceT<OscT>::set_decay_scale(float s)` and
    `void BodyVoice::set_decay_scale(float s)` — clamp to `0..1`, store, and
    re-apply the envelope immediately. Every `V` that `SynthEngineT<V>` is
    instantiated with must have this method; that is compile-enforced.
  - `static constexpr float SynthEngineT<V>::kAccentDecFloor`.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_step_accent.cpp`:

```cpp
namespace {

// Index of the last sample above 5 % of the note's own peak: a length measure
// that does not depend on the envelope's exact shape. Returns -1 if the note
// is still sounding when the buffer runs out, which the caller must reject --
// a truncated note would make every comparison meaningless.
template <class EngineT>
int note_len_samples(uint32_t seed, float dec_knob, float accent) {
    EngineT e;
    spky_contract::fresh(e, seed);
    e.set_cycle(0.25f);                  // keeps even DEC 1 inside the buffer
    e.set_flow(false);
    e.set_decay(dec_knob);
    e.set_accent(accent);
    const float chord[1] = {0.5f};
    e.trigger_chord(chord, 1);
    std::vector<float> buf = spky_contract::render_l(e, 48000 * 6);
    float pk = 0.f;
    for (float v : buf) pk = std::max(pk, std::fabs(v));
    if (pk <= 1e-5f) return -1;
    const float thr = pk * 0.05f;
    int last = 0;
    for (int i = 0; i < static_cast<int>(buf.size()); ++i)
        if (std::fabs(buf[i]) > thr) last = i;
    return (last >= static_cast<int>(buf.size()) - 2) ? -1 : last;
}

}  // namespace

TEST_CASE("accent G5: the DEC accent is inert at DEC 0 and real at DEC 1") {
    // Half one: at DEC 0 there is no room to take away, so the accent must not
    // change the note at all. A gate asserting only half two would pass on an
    // implementation that ignores the knob entirely.
    const int flat_loud = note_len_samples<SynthEngine>(99u, 0.f, 0.f);
    const int flat_soft = note_len_samples<SynthEngine>(99u, 0.f, 1.f);
    REQUIRE(flat_loud > 0);
    CHECK(flat_loud == flat_soft);

    // Half two: at DEC 1 the weakest note is measurably shorter.
    const int full = note_len_samples<SynthEngine>(99u, 1.f, 0.f);
    const int cut  = note_len_samples<SynthEngine>(99u, 1.f, 1.f);
    REQUIRE(full > 0);
    REQUIRE(cut > 0);
    CHECK(cut < full);
    CHECK(static_cast<float>(cut) / static_cast<float>(full) < 0.6f);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
cmake --build build && ./build/spky_tests -tc="accent G5"
```

Expected: FAIL on `CHECK(cut < full)` — the two lengths are equal, because
nothing scales the decay yet. Both halves of the case compile and run, so this
is a behavioural RED, not a build error. Copy the failure line into the Step 7
commit message.

- [ ] **Step 3: Give the voices a latched decay scale**

`engine/synth/voice.h`, public, next to `set_env_times`:

```cpp
    // Per-note decay scale, latched at trigger time by SynthEngineT. Kept
    // separate from set_env_times because that one is re-pushed to EVERY voice
    // on EVERY control tick and would otherwise overwrite it within a block.
    void set_decay_scale(float s);
```

and private, next to the other envelope state:

```cpp
    float _attack_s = 0.f, _decay_s = 0.f;   // last times pushed, unscaled
    float _decay_scale = 1.f;
    bool  _env_seen = false;                 // has set_env_times run yet?
```

`engine/synth/voice.cpp`, replacing `set_env_times`:

```cpp
template <class OscT>
void VoiceT<OscT>::set_env_times(float attack_s, float decay_s) {
    _attack_s = attack_s;
    _decay_s  = decay_s;
    _env_seen = true;
    _env.set_times(attack_s, decay_s * _decay_scale);
}

template <class OscT>
void VoiceT<OscT>::set_decay_scale(float s) {
    _decay_scale = clampf(s, 0.f, 1.f);
    // Re-apply now rather than waiting for the next control tick: at a short
    // attack the envelope would otherwise start decaying on the previous
    // note's time. The _env_seen guard stops a scale pushed before any
    // set_env_times from writing invented times into the envelope.
    if (_env_seen) _env.set_times(_attack_s, _decay_s * _decay_scale);
}
```

`engine/body/body_voice.h` — the same declaration and the same three private
members. `engine/body/body_voice.cpp`, replacing `set_env_times`:

```cpp
void BodyVoice::set_env_times(float attack_s, float decay_s) {
    _attack_s = attack_s;
    _decay_s  = decay_s;
    _env_seen = true;
    _apply_env();
}

void BodyVoice::set_decay_scale(float s) {
    _decay_scale = clampf(s, 0.f, 1.f);
    if (_env_seen) _apply_env();
}

void BodyVoice::_apply_env() {
    _exciter.set_length(_attack_s);
    // Longer decay = less damping = longer ring. Curve is tuning material.
    const float d_s = _decay_s * _decay_scale;
    _damping = d_s / (d_s + 1.f);
}
```

with `void _apply_env();` declared private in `body_voice.h`.

`engine/synth/synth_engine.h`, in `detail::VoiceCountProbe`, next to
`set_env_times`:

```cpp
    void set_decay_scale(float /*s*/) {}
```

- [ ] **Step 4: Run the build to confirm every voice satisfies the concept**

```bash
cmake --build build
```

Expected: builds clean. A missing `set_decay_scale` on any `V` fails here, by
design — the concept is compile-enforced, not documented.

- [ ] **Step 5: Keep the knob position and spend it**

`engine/synth/synth_engine.h`, public, next to `kAccentVelFloor`:

```cpp
    // How long the WEAKEST note of a full pattern rings, relative to the DEC
    // knob's setting, at DEC fully up. By ear, first try.
    static constexpr float kAccentDecFloor = 0.3f;
```

and private, next to `_decay_ratio`:

```cpp
    float _decay_n = 0.f;          // DEC knob position; the accent's room
```

`engine/synth/synth_engine.cpp`, `set_decay`:

```cpp
template <class V>
void SynthEngineT<V>::set_decay(float n) {
    // The knob position is kept beside the ratio because the accent scales
    // with it, and 0.1 * 80^n is not worth inverting.
    _decay_n = clampf(n, 0.f, 1.f);
    _decay_ratio = 0.1f * std::pow(80.f, _decay_n);
}
```

`engine/synth/synth_engine.cpp`, `_do_trigger()`, on the line after
`set_vel(...)`:

```cpp
    // The room the accent has to shorten a note is the room the DEC knob
    // dialled in: at DEC 0 the term vanishes and the envelope is untouchable,
    // at DEC 1 the weakest note rings for kAccentDecFloor of the set time.
    // The knob stays the ceiling -- the accent only ever subtracts.
    _voices[pick].set_decay_scale(
        1.f - (1.f - kAccentDecFloor) * _accent * _decay_n);
```

- [ ] **Step 6: Run the tests**

```bash
cmake --build build && ./build/spky_tests -tc="accent*" && ctest --test-dir build --output-on-failure
```

Expected: all accent cases PASS and the full suite green. Watch
`tests/test_voice.cpp`, `tests/test_body_voice.cpp` and the
`synth_voicet_contract` cases in particular — `set_env_times` changed shape in
both voice classes, and the VoiceT contract asserts cycle-scaled envelope
timing directly. At `_decay_scale == 1.f` (the boot value, and the value every
existing caller leaves it at) those assertions must be untouched; if one
moves, the caching is wrong, not the contract.

- [ ] **Step 7: Commit**

```bash
git add engine/synth/voice.h engine/synth/voice.cpp engine/body/body_voice.h \
        engine/body/body_voice.cpp engine/synth/synth_engine.h \
        engine/synth/synth_engine.cpp tests/test_step_accent.cpp
git commit -m "feat(synth): the STEP accent shortens the note, scaled by DEC

..."
```

---

### Task 5: Verify the whole thing and write down what was measured

**Files:**
- Modify: `docs/engine-map.md` — a new section for the groove facts
- Modify: `docs/roadmap.md` — the "Last updated" entry

**Interfaces:**
- Consumes: everything from Tasks 1–4.
- Produces: no code.

- [ ] **Step 1: Run the full suite from a clean configure**

```bash
rm -rf build && source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: every test green, `ctrl_identity` and `wave_formant_sweep` at their
existing hashes. Record the pass count.

- [ ] **Step 2: Confirm each gate can still go red**

For each of G1–G6, make the one-line mutation that should break it, rebuild,
confirm the expected case fails, and revert. The mutations:

- G1/G2 — normalize against `k` instead of `groove_length`: in `_start_note`,
  divide by `_groove_k() - 1`. G1's sparse contrast survives; G2's
  `uniq.size() == steps` fails.
- G3 — drop the `_step_mode` guard from `note_accent()`.
- G4 — change `_do_trigger`'s velocity line back to `set_vel(vel)`.
- G5 — drop the `* _decay_n` factor. The DEC-0 half fails.
- G6 — change the velocity line to `set_vel(1.f - (1.f - kAccentVelFloor) * _accent)`,
  i.e. replace the compensation instead of composing with it.

Write the six observed failure lines into the Step 5 commit message. A gate
whose mutation does **not** redden it is a finding: fix the gate before
continuing.

- [ ] **Step 3: Add the measured groove facts to the engine map**

`docs/engine-map.md` — a new section after §7, in the file's own idiom (each
claim names the probe setup that produced it):

```markdown
## 8. The groove cell: length, rank, and when it exists

Measured 2026-08-15 on a note deck (`_melodic` and `_flow_melody` both true),
STEP, rate 0.5 Hz, SHAPE 0, SMOOTH 0, RANGE 1, VARY 0, `set_melodic()` before
`init()`, seeds 999 / 12345 / 7 / 4242, STEPS 4 / 8 / 16, DENSE 0.0 / 0.05 /
0.125 / 0.25 / 0.5 / 0.75 / 1.0.

- **`pattern_groove.len` equals the STEPS count**, not `pg_target_len()`'s
  constant 8 — that function sizes the pitch motif, not the groove cell.
- **`set_step()` does not regenerate the groove. The next cycle wrap does.**
  Read the table immediately after the call and you get the previous cell: at
  4 steps an 8-slot groove whose slots 4..7 the phrase never reaches. The
  first probe written against this measured exactly that and reported a false
  mismatch on two of three STEPS counts.
- **`rank_of_slot[]` is a permutation of `0..L-1` with slot 0 pinned to rank
  0**, in every cell measured, both patterns of the song pair.
- **The firing set is exactly `{ slot : rank_of_slot[slot % L] < k }`** with
  `k = clamp(round(DENSE·L), 1, L)` (`lane.cpp:643`, `:655`) — every cell of
  the sweep matched after the settle wrap.

Pinned by `tests/test_step_accent.cpp` (G1 and G2: G2's
`a.size() == steps` is what holds `len == STEPS`, and its permutation check
is what holds the rank claim).
```

- [ ] **Step 4: Add the roadmap entry**

`docs/roadmap.md` — extend the "Last updated" chain with a 2026-08-15 entry
naming the feature, the two by-ear constants, the six gates, and the fact that
the render hashes did not move because both hashed scenarios run in FLOW.

- [ ] **Step 5: Commit**

```bash
git add docs/engine-map.md docs/roadmap.md
git commit -m "docs: record the groove-cell measurements and the STEP accent

..."
```

---

## Self-Review

**Spec coverage.** §3 (accent value) → Task 1. §4 (delivery) → Task 2. §5 (VEL)
→ Task 3. §6 (DEC) → Task 4. §7 (engine scope) → Task 2 Step 3, where SAMPLER
and BBD take the default and no per-engine branch exists to forget. §8 (gates)
→ G1/G2/G3 in Task 1, the seam case in Task 2, G4/G6 in Task 3, G5 in Task 4,
red-proofs for all six in Task 5. §9 (consequences) → Task 3 Step 7 (hashes)
and Task 5 Step 1.

**Type consistency.** `note_accent()` (Task 1) is consumed by
`pitch_note_accent()` (Task 2) and by the Task 1 tests. `set_accent(float)`
(Task 2) is consumed by `_do_trigger` in Tasks 3 and 4 via `_accent`.
`set_decay_scale(float)` (Task 4) is declared on `VoiceT`, `BodyVoice` and
`VoiceCountProbe` and called from one site. `kAccentVelFloor` (Task 3) and
`kAccentDecFloor` (Task 4) are both public `static constexpr` on
`SynthEngineT<V>` and are read by name in the tests.

**BODY has no length gate, on purpose.** G4 covers SYNTH and WAVE, which are
both `VoiceT` instantiations with an AD envelope. `BodyVoice::env_value()` is
an energy follower and its ring time comes from damping, which is exactly the
split `tests/synth_voicet_contract.h` already documents: "how tempo-locked it
feels is a listening decision, not a bound a contract can carry." What holds
BODY is that `set_decay_scale` is compile-enforced on every `V` (Task 4
Step 4) and that its `_apply_env()` is the single site both callers route
through. A BODY-specific bound would have to be invented rather than derived,
and an invented bound is the vacuous-gate failure mode, not coverage.

**Known open item, deliberately not a task.** The by-ear pass on the two
floors is Bastian's, after Task 4 lands. Both constants are sited so that
changing either is a one-line edit that no gate contradicts.
