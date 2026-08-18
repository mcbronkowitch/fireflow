# SWARM CHARACTER Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give SWARM four spectral laws on the RES slot instead of one, and make HARM's full travel usable in all four.

**Architecture:** Everything lives in `SwarmEngine::_rebuild_targets` and its setters. `SwarmBank` — the hot loop the bench priced at 7405 cycles per partial per block — is not touched, so N = 14 and the kernel measurement stand. CHARACTER is a four-zone read of a continuous knob with hysteresis, following `ChordBuilder::set_color`'s existing idiom. HARM keeps one meaning across all four characters and each character decides where "away from pure" points.

**Tech Stack:** C++17, clang + Ninja, doctest. Engine code is host-free — no hardware type ever crosses into `engine/`.

**Spec:** `docs/superpowers/specs/2026-08-17-swarm-character-and-breath-design.md`

## Global Constraints

- **Build:** `source env.sh` first, then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`. Release is **not optional** — a Debug configure makes `spky_tests` and `ctrl_identity` fail with "SYNTH reference moved".
- **Never `cd`** as a shell command prefix; the Bash tool already starts in the repo root.
- **Everything written into the repo is English** — code, comments, tests, commit messages.
- **Commit trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- **Nothing may assume the value of `swarm_cfg::kPartials`.** Every loop runs to it and every expectation derives from the named constants in `swarm_config.h`, never from their literals. This is the existing rule at the top of `tests/test_swarm_engine.cpp` and it still holds.
- **No bit-exactness or byte-identity gates.** Renders are sanity checks (`fireflow-bit-exactness-not-required`).
- **A test that cannot go red gets fixed.** Every gate below has a "run it and watch it fail" step, and that step is not optional.
- **`SwarmBank` (`engine/swarm/swarm_bank.h`) is not modified by any task in this plan.** If a task seems to need it, the task is wrong — stop and report.

---

### Task 1: Raise the RES parameter range so the fourth character is reachable

`param_table.h` gives `P_RES_A` / `P_RES_B` the range 0..0.75. Neither shipping surface honours it (spec §3.1) — VCV and the render host both call `set_voice_resonance` directly with 0..1 — but `apply_param` clamps, and `apply_param` is what `tests/test_param_impact.cpp` and `tests/test_param_table.cpp` drive. Left at 0.75, the top character would be untestable on that path.

This 0.75 table ceiling is not where a by-ear resonance decision lived — there is no single by-ear cap on this signal. Downstream of `Part::set_voice_resonance` (`part.h:184`), `VoiceT::set_resonance` clamps the synth/wave voices to 0.95 (`voice.cpp:91`) and `SamplerEngine::set_resonance` clamps to 0.90 (`sampler_engine.cpp:1011`), both by ear; BBD, Body and Swarm carry no by-ear ceiling at all and already ran the full 0..1 this setter passes through. This task raises only the table's own range and touches none of those five clamps.

**Files:**
- Modify: `engine/param_table.h:89`
- Test: `tests/test_param_table.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `kParams[P_RES_A].hi == 1.0f` and `kParams[P_RES_B].hi == 1.0f`, which every later task's zone arithmetic assumes.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_param_table.cpp`:

```cpp
// The RES slot carries CHARACTER on a SWARM deck (spec 2026-08-17 section 3.1),
// and its top zone must be reachable through apply_param. Both shipping
// surfaces already pass 0..1: VCV's generic configParam branch
// (Fireflow.cpp:436) and the render host's scenario reader, which calls
// set_voice_resonance directly -- swarm_drone.json:13 already sends 0.8.
TEST_CASE("param table: the RES slot spans the full range both hosts send") {
    CHECK(kParams[P_RES_A].hi == doctest::Approx(1.0f));
    CHECK(kParams[P_RES_B].hi == doctest::Approx(1.0f));
    // Still continuous, not snapped: CHARACTER's zones are read inside the
    // engine, so the parameter itself must not quantize.
    CHECK(kParams[P_RES_A].steps == 0);
    CHECK(kParams[P_RES_B].steps == 0);
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake --build build && ./build/spky_tests -tc="param table: the RES slot spans the full range both hosts send"`
Expected: FAIL — `0.75 == Approx(1.0)` on the first two CHECKs.

- [ ] **Step 3: Raise the range**

In `engine/param_table.h`, replace line 89:

```cpp
  X(P_RES_A,      0.f, 1.f, 0)   X(P_RES_B,      0.f, 1.f, 0) \
```

Add above the `SPKY_PARAMS` macro, with the other range notes:

```cpp
// - RES: 0..1, raised from 0..0.75 on 2026-08-17. The old ceiling was reached
//   by nothing that ships. VCV configures RES through the generic
//   configParam(c.id, 0.f, 1.f, ...) branch (Fireflow.cpp:436) and passes the
//   raw knob value to set_voice_resonance; the render host's scenario reader
//   calls set_voice_resonance directly and swarm_drone.json:13 already sends
//   0.8. apply_param -- the only place the ceiling applied -- is reached from
//   tests/ and bench/ alone, so the clamp made the fourth SWARM character
//   untestable there while changing nothing anybody could hear. This table
//   ceiling was never where a by-ear resonance decision lived: downstream of
//   Part::set_voice_resonance (part.h:184), VoiceT::set_resonance clamps the
//   synth/wave voices to 0.95 (voice.cpp:91) and SamplerEngine::set_resonance
//   clamps to 0.90 (sampler_engine.cpp:1011), both by-ear; BBD, Body and
//   Swarm carry no by-ear ceiling at all -- each already ran the full 0..1
//   this setter passes through. None of those five clamps moves here.
```

- [ ] **Step 4: Run the test and the whole suite**

Run: `cmake --build build && ctest --test-dir build --output-on-failure`
Expected: PASS, including `spky_tests`, `ctrl_identity` and `spky_render_hash`. If `test_param_impact` reports a newly-moving parameter, that is the point of this change — read its assertion before touching it.

- [ ] **Step 5: Commit**

```bash
git add engine/param_table.h tests/test_param_table.cpp
git commit -m "fix(params): the RES ceiling was reached by nothing that ships"
```

---

### Task 2: The CHARACTER zone reader, with hysteresis

A pure, standalone piece: value in, character out, with hysteresis so a boundary cannot chatter. It goes in first and alone because `fireflow-choke-silences-a-deck` records a five-zone control drawn continuously that muted a whole deck — the zone reader is where that class of bug lives, and it is worth its own gate before anything depends on it.

**Files:**
- Modify: `engine/swarm/swarm_config.h`
- Create: `engine/swarm/swarm_character.h`
- Test: `tests/test_swarm_character.cpp` (new), registered in `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `enum class SwarmChar { Ladder = 0, Vowel = 1, Bell = 2, Choir = 3 }`
  - `class CharZone { void reset(float v); SwarmChar read(float v); SwarmChar current() const; }` — `read()` is stateful (that is what hysteresis means) and returns the zone the value now belongs to.
  - `swarm_cfg::kCharZones = 4`, `swarm_cfg::kCharHyst = 0.02f`

- [ ] **Step 1: Write the failing tests**

Create `tests/test_swarm_character.cpp`:

```cpp
// The CHARACTER zone reader. Spec: docs/superpowers/specs/
// 2026-08-17-swarm-character-and-breath-design.md section 4.
//
// Nothing here may assume kCharZones == 4 by literal; the boundaries are
// derived from the constant, the way the swarm tests derive from kPartials.
#include <doctest/doctest.h>
#include "swarm/swarm_character.h"
#include "swarm/swarm_config.h"

using namespace spky;

namespace {
// The nominal centre of zone z, in knob units.
float zone_centre(int z) {
    return (static_cast<float>(z) + 0.5f) /
           static_cast<float>(swarm_cfg::kCharZones);
}
// The nominal boundary between zone z and z + 1.
float zone_edge(int z) {
    return static_cast<float>(z + 1) /
           static_cast<float>(swarm_cfg::kCharZones);
}
}  // namespace

TEST_CASE("swarm C1: every zone centre selects its own character") {
    CharZone z;
    for (int i = 0; i < swarm_cfg::kCharZones; ++i) {
        z.reset(zone_centre(i));
        CHECK(static_cast<int>(z.read(zone_centre(i))) == i);
    }
}

TEST_CASE("swarm C2: the top of the knob reaches the LAST character") {
    // The failure this exists for: a range or a divisor that leaves the top
    // zone one epsilon out of reach, so one character ships unreachable.
    CharZone z;
    z.reset(0.f);
    CHECK(z.read(1.0f) == static_cast<SwarmChar>(swarm_cfg::kCharZones - 1));
    CHECK(z.read(0.999f) == static_cast<SwarmChar>(swarm_cfg::kCharZones - 1));
}

TEST_CASE("swarm C3: the bottom of the knob reaches the FIRST character") {
    CharZone z;
    z.reset(1.f);
    CHECK(z.read(0.0f) == SwarmChar::Ladder);
}

TEST_CASE("swarm C4: a value parked ON a boundary does not chatter") {
    // The CHOKE lesson (fireflow-choke-silences-a-deck): a continuously drawn
    // zone control sitting exactly on an edge must pick one side and stay
    // there, however often it is read.
    for (int i = 0; i + 1 < swarm_cfg::kCharZones; ++i) {
        CharZone z;
        z.reset(zone_centre(i));                 // arrive from below
        const SwarmChar first = z.read(zone_edge(i));
        for (int k = 0; k < 100; ++k)
            CHECK(z.read(zone_edge(i)) == first);
    }
}

TEST_CASE("swarm C5: the hysteresis band is real and symmetric") {
    // Approaching an edge from below must NOT flip until the value has passed
    // the edge by kCharHyst; approaching from above, likewise.
    const float h = swarm_cfg::kCharHyst;
    REQUIRE(h > 0.f);
    for (int i = 0; i + 1 < swarm_cfg::kCharZones; ++i) {
        const float e = zone_edge(i);
        CharZone up;
        up.reset(zone_centre(i));
        CHECK(static_cast<int>(up.read(e + h * 0.5f)) == i);       // not yet
        CHECK(static_cast<int>(up.read(e + h * 1.5f)) == i + 1);   // now

        CharZone down;
        down.reset(zone_centre(i + 1));
        CHECK(static_cast<int>(down.read(e - h * 0.5f)) == i + 1); // not yet
        CHECK(static_cast<int>(down.read(e - h * 1.5f)) == i);     // now
    }
}

TEST_CASE("swarm C6: reset() snaps without hysteresis") {
    // reset is for init and patch load, where there is no previous position to
    // be sticky about.
    CharZone z;
    z.reset(1.f);
    CHECK(z.current() == static_cast<SwarmChar>(swarm_cfg::kCharZones - 1));
    z.reset(0.f);
    CHECK(z.current() == SwarmChar::Ladder);
}
```

Register it in `CMakeLists.txt` beside the other `tests/test_*.cpp` entries.

- [ ] **Step 2: Run them and watch them fail**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`
Expected: FAIL to compile — `swarm/swarm_character.h` does not exist.

- [ ] **Step 3: Add the constants**

In `engine/swarm/swarm_config.h`, in the `--- by ear, first try ---` block:

```cpp
// CHARACTER: how many spectral laws the RES slot selects between (spec
// section 4). Four zones over the knob's 0..1 travel.
constexpr int kCharZones = 4;

// The hysteresis band around each zone boundary, in knob units. NOT optional:
// a five-zone control drawn continuously once muted a whole deck
// (fireflow-choke-silences-a-deck), and a boundary that flips back and forth
// under a slewed value is the same failure. By ear only in its width; its
// existence is structural.
constexpr float kCharHyst = 0.02f;
static_assert(kCharHyst * 2.f < 1.f / static_cast<float>(kCharZones),
              "the hysteresis band must be narrower than the zone it guards");
```

- [ ] **Step 4: Write the zone reader**

Create `engine/swarm/swarm_character.h`:

```cpp
#pragma once
#include "swarm/swarm_config.h"
#include "util/math.h"

namespace spky {

// Which spectral law SWARM's _rebuild_targets follows (spec section 4).
// Appended, never renumbered: a patch stores the knob position, but a saved
// scenario stores the enum in its CSV column and a reordering would silently
// re-voice it.
enum class SwarmChar {
    Ladder = 0,   // pure overtone series; HARM is physical inharmonicity
    Vowel  = 1,   // harmonic positions, formant-pair amplitude
    Bell   = 2,   // free-bar modal ratios
    Choir  = 3,   // partials collapse onto the chord tones, detuned
};

// A continuous knob read as kCharZones zones, with hysteresis at every
// boundary. Stateful on purpose: hysteresis IS the state.
//
// The idiom is ChordBuilder::set_color's (part.h:344, 708), not a new one.
class CharZone {
public:
    // Init and patch load: snap, no stickiness -- there is no previous
    // position to be sticky about.
    void reset(float v) { _cur = _nominal(v); }

    // Play: sticky. The zone changes only once the value has passed the
    // boundary by kCharHyst, whichever way it is travelling.
    SwarmChar read(float v) {
        const int nom = _nominal(v);
        if (nom == _cur) return static_cast<SwarmChar>(_cur);
        // Moving up: the edge that must be cleared is the top of the current
        // zone. Moving down: the bottom of it.
        const float edge = nom > _cur
            ? _edge(_cur)          // top of _cur
            : _edge(_cur - 1);     // bottom of _cur
        const float d = v - edge;
        if (nom > _cur && d > swarm_cfg::kCharHyst) _cur = nom;
        else if (nom < _cur && -d > swarm_cfg::kCharHyst) _cur = nom;
        return static_cast<SwarmChar>(_cur);
    }

    SwarmChar current() const { return static_cast<SwarmChar>(_cur); }

private:
    // The zone v falls in, ignoring hysteresis. The clamp on the high side is
    // what makes v == 1.0 land in the LAST zone rather than one past it.
    static int _nominal(float v) {
        const float c = clampf(v, 0.f, 1.f);
        int z = static_cast<int>(c * static_cast<float>(swarm_cfg::kCharZones));
        if (z >= swarm_cfg::kCharZones) z = swarm_cfg::kCharZones - 1;
        if (z < 0) z = 0;
        return z;
    }
    static float _edge(int z) {
        return static_cast<float>(z + 1) /
               static_cast<float>(swarm_cfg::kCharZones);
    }

    int _cur = 0;
};

}  // namespace spky
```

- [ ] **Step 5: Run the tests**

Run: `cmake --build build && ./build/spky_tests -tc="swarm C*"`
Expected: PASS, all six.

- [ ] **Step 6: Commit**

```bash
git add engine/swarm/swarm_character.h engine/swarm/swarm_config.h tests/test_swarm_character.cpp CMakeLists.txt
git commit -m "feat(swarm): a zone reader for CHARACTER, hysteresis included"
```

---

### Task 3: Wire CHARACTER onto the RES slot and take FLOOR off it

FLOOR keeps working in this task — it just stops being reachable from RES. Task 8 gives it its new home on FALL. Between this task and that one, FLOOR sits at its stored default, which is deliberate: it keeps the two changes separately reviewable, and no gate in between depends on FLOOR being drivable.

**Files:**
- Modify: `engine/swarm/swarm_engine.h`, `engine/swarm/swarm_engine.cpp`
- Modify: `engine/parts/part.h:184`
- Test: `tests/test_swarm_engine.cpp`

**Interfaces:**
- Consumes: `SwarmChar`, `CharZone` from Task 2.
- Produces:
  - `void SwarmEngine::set_character(float n)` — takes the raw 0..1 knob value, reads it through `CharZone`.
  - `SwarmChar SwarmEngine::character_for_test() const` (under `SPKY_TESTING`).
  - `Part::set_voice_resonance` routes to `_swarm.set_character(n)` instead of `_swarm.set_floor(n)`.

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_swarm_engine.cpp`:

```cpp
TEST_CASE("swarm G32: the RES slot carries CHARACTER, not FLOOR") {
    SwarmEngine e = fresh_swarm();
    feed(e, 0.5f);
    // Bottom of the knob is the first character, top is the last. Derived from
    // kCharZones, never from the literal 4.
    e.set_character(0.f);
    CHECK(e.character_for_test() == SwarmChar::Ladder);
    e.set_character(1.f);
    CHECK(e.character_for_test() ==
          static_cast<SwarmChar>(swarm_cfg::kCharZones - 1));
}

TEST_CASE("swarm G33: a SWARM deck's RES knob reaches CHARACTER through Part") {
    // The routing gate. Part::set_voice_resonance is what the hosts call, and
    // this is the seam where a control merge silently keeps the old target
    // (fireflow-control-merge-init-trap).
    Instrument in;
    in.init(48000.f);
    in.set_engine(PART_A, ENGINE_SWARM);
    in.set_voice_resonance(PART_A, 1.f);
    CHECK(in.part(PART_A).swarm().character_for_test() ==
          static_cast<SwarmChar>(swarm_cfg::kCharZones - 1));
    in.set_voice_resonance(PART_A, 0.f);
    CHECK(in.part(PART_A).swarm().character_for_test() == SwarmChar::Ladder);
}

TEST_CASE("swarm G34: both decks boot in the FIRST character") {
    // RES_A boots at 0.000 and RES_B at 0.220 (gen_panel.py INIT_DEFAULTS).
    // 0.220 is within 0.03 of the first boundary, so this is a real gate and
    // not a formality: nudge kCharZones and deck B changes instrument on boot.
    Instrument in;
    in.init(48000.f);
    in.set_engine(PART_A, ENGINE_SWARM);
    in.set_engine(PART_B, ENGINE_SWARM);
    in.set_voice_resonance(PART_A, 0.000000000f);
    in.set_voice_resonance(PART_B, 0.220000312f);
    CHECK(in.part(PART_A).swarm().character_for_test() == SwarmChar::Ladder);
    CHECK(in.part(PART_B).swarm().character_for_test() == SwarmChar::Ladder);
}
```

`tests/test_swarm_engine.cpp` needs `#include "swarm/swarm_character.h"` and `Part` needs a `swarm()` observer beside the existing `sampler()` one (`part.h:213`).

- [ ] **Step 2: Run them and watch them fail**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G32"`
Expected: FAIL to compile — `set_character` does not exist.

- [ ] **Step 3: Add the engine side**

In `engine/swarm/swarm_engine.h`, include `swarm/swarm_character.h`, add beside the other VOICE-row setters:

```cpp
    // CHARACTER, on the RES slot (spec section 4). Takes the raw knob value;
    // the zone reading and its hysteresis live in CharZone. The RES slot no
    // longer carries FLOOR -- FALL does (spec section 6).
    void set_character(float n) { _char_zone.read(clampf(n, 0.f, 1.f)); }
```

and in the private members:

```cpp
    CharZone _char_zone;
```

and under `SPKY_TESTING`:

```cpp
    SwarmChar character_for_test() const { return _char_zone.current(); }
```

In `SwarmEngine::init`, before the first `_rebuild_targets()`, snap the zone so a boot is not sticky against whatever the default-constructed value was:

```cpp
    _char_zone.reset(0.f);          // LADDER on boot; G34 pins this
```

- [ ] **Step 4: Reroute the Part slot**

In `engine/parts/part.h:184`, change the swarm leg only:

```cpp
    void set_voice_resonance(float n) { _synth.set_resonance(n); _wave.set_resonance(n); _body.set_resonance(n); _sampler.set_resonance(n);     _bbd.set_resonance(n); _swarm.set_character(n); }
```

Update the block comment above it (`part.h:178-181`): RESONANCE is CHARACTER on a swarm deck now, and FLOOR has moved to the top of FALL.

Add the observer beside `sampler()`:

```cpp
    SwarmEngine& swarm() { return _swarm; }
    const SwarmEngine& swarm() const { return _swarm; }
```

- [ ] **Step 5: Run the tests**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G3*"`
Expected: PASS for G32, G33, G34.

Also run the whole suite: `ctest --test-dir build --output-on-failure`. **G16 ("FLOOR is the whole difference between a swell and a drone") will now fail** — it drives FLOOR through `set_voice_resonance`. Change it to call `SwarmEngine::set_floor` directly and leave a comment saying Task 8 rewrites it for the FALL fold. Do not delete it.

- [ ] **Step 6: Commit**

```bash
git add engine/swarm/swarm_engine.h engine/swarm/swarm_engine.cpp engine/parts/part.h tests/test_swarm_engine.cpp
git commit -m "feat(swarm): the RES slot carries CHARACTER"
```

---

### Task 4: LADDER — physical inharmonicity replaces the exponent arc

The measured defect (spec §1.1): `pow(n, 1 + beta)` with `kStretchMax = 0.35` puts the octave partial 140 cents sharp at HARM 0.2 and stops being monotone above 0.6. Replace the whole arc with `f_n = n * f0 * sqrt(1 + B * n^2)`, `B = kInharmMax * harm^2`.

**Files:**
- Modify: `engine/swarm/swarm_config.h`, `engine/swarm/swarm_engine.cpp:122-185`
- Test: `tests/test_swarm_engine.cpp` (rewrites G11, keeps G31)

**Interfaces:**
- Consumes: `SwarmChar` from Task 2, `set_character` from Task 3.
- Produces: `_rebuild_targets` branches on `_char_zone.current()`; the `SwarmChar::Ladder` branch is the only one implemented after this task. `swarm_cfg::kInharmMax = 0.0013f`.

- [ ] **Step 1: Write the failing tests**

Replace `TEST_CASE("swarm G11: ...")` in `tests/test_swarm_engine.cpp` with:

```cpp
// The measured numbers this gate defends (spec section 1.1 and 4.1), root
// 220 Hz: the old exponent law put overtone 2 at +140 cents by HARM 0.2 and
// +420 by 0.6, and went NON-MONOTONE above 0.6 (+206 at 0.8, -39 at 1.0).
// The inharmonicity law puts overtone 2 at +4.5 cents and overtone 12 at
// +148.5 at HARM 1.0, printed from the law itself.
namespace {
// Cents between a partial and where the pure series would put it.
float cents_off(SwarmEngine& e, int overtone, float f0) {
    const int slot = swarm_cfg::kSubPartials + overtone - 1;
    const float f = e.target_hz_for_test(slot);
    return 1200.f * std::log2(f / (f0 * static_cast<float>(overtone)));
}
// Settle the engine at a HARM position and return it ready to read.
void settle(SwarmEngine& e, float harm, float pitch) {
    feed(e, pitch, 0.5f, 0.5f, 0.f, 1.f);      // MOTION 0: drift cannot blur
    e.set_harm(harm);
    e.set_balance(0.f);
    e.trigger(pitch);
    for (int i = 0; i < swarm_cfg::kCtrlInterval * 4; ++i) {
        float l, r; e.process(l, r);
    }
}
}  // namespace

TEST_CASE("swarm G11: LADDER's HARM is physical inharmonicity, not a stretch") {
    const float pitch = 1.f / 3.f;             // pitch_to_hz -> 220 Hz exactly
    const float f0 = 220.f;

    SUBCASE("HARM 0 is the pure series") {
        SwarmEngine e = fresh_swarm();
        e.set_character(0.f);
        settle(e, 0.f, pitch);
        for (int n = 1; n <= 6; ++n)
            CHECK(cents_off(e, n, f0) == doctest::Approx(0.f).epsilon(0.001f));
    }

    SUBCASE("the low partials barely move -- the whole complaint") {
        // The old law moved overtone 2 by +420 cents at HARM 0.6. This one
        // must keep it inside a few cents across the ENTIRE travel, because
        // overtone 2 is what carries pitch.
        SwarmEngine e = fresh_swarm();
        e.set_character(0.f);
        settle(e, 1.f, pitch);
        CHECK(std::fabs(cents_off(e, 2, f0)) < 10.f);
    }

    SUBCASE("the top of the travel is bell-like, not broken") {
        SwarmEngine e = fresh_swarm();
        e.set_character(0.f);
        settle(e, 1.f, pitch);
        const float c12 = cents_off(e, 12, f0);
        CHECK(c12 > 100.f);       // audibly inharmonic
        CHECK(c12 < 250.f);       // not the +1506 the old law produced
    }

    SUBCASE("HARM is monotone -- the defect that made it unusable") {
        // Overtone 6's deviation must never decrease as HARM rises. The old
        // law failed this between 0.6 and 1.0 in every column.
        float prev = -1.f;
        for (int i = 0; i <= 10; ++i) {
            SwarmEngine e = fresh_swarm();
            e.set_character(0.f);
            settle(e, i * 0.1f, pitch);
            const float c = cents_off(e, 6, f0);
            CHECK(c >= prev - 0.01f);
            prev = c;
        }
    }
}
```

- [ ] **Step 2: Run them and watch them fail**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G11*"`
Expected: FAIL — "the low partials barely move" reports roughly +815 cents (the old law at HARM 1.0), and the monotone subcase fails between 0.6 and 0.8.

- [ ] **Step 3: Replace the constants**

In `engine/swarm/swarm_config.h`, delete `kHarmClusterStart`, `kStretchMax` and `kClusterSpan`, and add:

```cpp
// HARM in LADDER: physical inharmonicity, f_n = n*f0*sqrt(1 + B*n^2), with
// B = kInharmMax * harm^2. MEASURED, not by ear -- the old exponent law
// (pow(n, 1 + beta), kStretchMax 0.35) put overtone 2 at +140 cents by HARM
// 0.2 and +420 by 0.6, ten times a concert piano at a fifth of the travel,
// and went non-monotone above 0.6. Printed from this law at kInharmMax:
//
//   HARM  0.25 -> overtone 12 at  +10.1 cents
//         0.50 ->                 +39.6   (a concert piano sits near +50)
//         1.00 ->                +148.5   (bell-like; overtone 2 moves +4.5)
//
// The square on harm is what puts the piano range mid-knob instead of in the
// first hair of the travel.
constexpr float kInharmMax = 0.0013f;
```

- [ ] **Step 4: Rewrite the frequency law**

In `engine/swarm/swarm_engine.cpp::_rebuild_targets`, delete the `beta` and `cluster` locals (lines 122-128) and the `over > 1` cluster block with its comment (lines 139-163), and put the character branch in their place:

```cpp
    const SwarmChar chr = _char_zone.current();
    // HARM keeps ONE meaning in every character -- how far from pure -- and
    // the character decides where "away" points (spec section 5).
    const float B = swarm_cfg::kInharmMax * _harm_n * _harm_n;
```

and inside the `k` loop, replacing `float f = f0 * std::pow(n, 1.f + beta);` and the cluster block:

```cpp
        float f;
        switch (chr) {
            case SwarmChar::Ladder:
            default:
                // f_n = n * f0 * sqrt(1 + B*n^2). The fundamental is exempt by
                // construction rather than by a special case: at n == 1 the
                // radicand is 1 + B, which is 4.5 cents at the very top of the
                // knob and inaudible -- the old law needed an explicit
                // exemption because pow(1, x) left the fundamental to take the
                // full seeded cluster displacement instead.
                f = f0 * n * std::sqrt(1.f + B * n * n);
                break;
        }
        f *= _detune_ratio;
```

Delete the now-dead `#include` of nothing and leave `_spread[]` in place — Task 7 (CHOIR) uses it.

- [ ] **Step 5: Run the tests**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G11*"`
Expected: PASS, all four subcases.

Then the whole suite: `ctest --test-dir build --output-on-failure`. **G30 and G31 both touch HARM.** G31 ("HARM never moves the fundamental") should still pass and is now nearly free — keep it, and add a line to its comment saying the exemption became structural. G30 ("the DETUNE slot carries HARM") should still pass; if it asserts a cluster-zone number, retarget it at the new law.

- [ ] **Step 6: Commit**

```bash
git add engine/swarm/swarm_config.h engine/swarm/swarm_engine.cpp tests/test_swarm_engine.cpp
git commit -m "fix(swarm): HARM had the wrong law, not the wrong constant"
```

---

### Task 5: VOWEL — a formant pair instead of one window

**Files:**
- Modify: `engine/swarm/swarm_config.h`, `engine/swarm/swarm_engine.h`, `engine/swarm/swarm_engine.cpp`
- Test: `tests/test_swarm_engine.cpp`

**Interfaces:**
- Consumes: Task 4's `switch (chr)`.
- Produces: `float SwarmEngine::_vowel_weight(float hz) const`, used in place of `_focus_weight` when `chr == SwarmChar::Vowel`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("swarm G35: VOWEL puts TWO peaks in the spectrum, and HARM moves them") {
    const float pitch = 1.f / 3.f;
    // Count local maxima across the swarm slots. A single raised-cosine
    // window has exactly one; a formant pair has two. This is the whole
    // difference between VOWEL and LADDER and it is what the gate measures.
    auto peak_count = [](SwarmEngine& e) {
        int peaks = 0;
        for (int i = swarm_cfg::kSubPartials + 1;
             i + 1 < swarm_cfg::kPartials; ++i) {
            const float a = e.target_amp_for_test(i);
            if (a > e.target_amp_for_test(i - 1) &&
                a > e.target_amp_for_test(i + 1) && a > 1e-4f)
                ++peaks;
        }
        return peaks;
    };

    SwarmEngine e = fresh_swarm();
    e.set_character(zone_centre_for_test(1));      // VOWEL
    settle(e, 0.5f, pitch);                        // /a/
    CHECK(peak_count(e) >= 2);

    // HARM moves the pair: the spectral centroid must differ between /u/ and
    // /i/, the two ends of the vowel path.
    auto centroid = [](SwarmEngine& e) {
        double num = 0.0, den = 0.0;
        for (int i = 0; i < swarm_cfg::kPartials; ++i) {
            const double a = e.target_amp_for_test(i);
            num += a * e.target_hz_for_test(i);
            den += a;
        }
        return den > 0.0 ? num / den : 0.0;
    };
    SwarmEngine u = fresh_swarm();
    u.set_character(zone_centre_for_test(1));
    settle(u, 0.f, pitch);
    SwarmEngine iv = fresh_swarm();
    iv.set_character(zone_centre_for_test(1));
    settle(iv, 1.f, pitch);
    CHECK(centroid(iv) > centroid(u) * 1.2);       // /i/ is the brighter vowel
}
```

`target_amp_for_test(int)` and a `zone_centre_for_test(int)` helper are needed; add the former beside `target_hz_for_test` under `SPKY_TESTING`, and the latter as a file-local helper in the test file mirroring `zone_centre` from Task 2.

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G35"`
Expected: FAIL — VOWEL currently falls through to LADDER's single window, so `peak_count` is 1 and the two centroids are equal.

- [ ] **Step 3: Add the vowel table**

In `engine/swarm/swarm_config.h`:

```cpp
// VOWEL: five formant-pair anchors, interpolated in log-frequency by HARM
// (spec section 4.2). The classic pair set; HARM 0 is /u/ and HARM 1 is /i/.
constexpr int kVowelAnchors = 5;
constexpr float kVowelF1[kVowelAnchors] = { 300.f, 500.f, 730.f, 530.f, 270.f };
constexpr float kVowelF2[kVowelAnchors] = { 870.f, 1000.f, 1090.f, 1840.f, 2290.f };

// Peak width, in octaves. Deliberately wider than a real formant: twelve
// partials spaced 220 Hz apart at the bottom of the played range cannot
// resolve a 100 Hz bandwidth, and a peak narrower than the partial spacing
// turns the vowel into an amplitude lottery. By ear.
constexpr float kVowelWidthOct = 0.45f;

// How far FOCUS's distance-from-centre closes the gap between F1 and F2.
// 1.0 would collapse them onto each other; by ear.
constexpr float kVowelGapRange = 0.6f;
```

- [ ] **Step 4: Implement the weight**

In `engine/swarm/swarm_engine.cpp`, beside `_focus_weight`:

```cpp
// VOWEL's amplitude law (spec section 4.2). Partial positions stay harmonic;
// what changes is that the aperture becomes a PAIR of peaks.
//
// FOCUS keeps both halves of the meaning _focus_weight already gives it,
// applied to the pair: its SIDE shifts both formants together in octaves (the
// singer's size), its DISTANCE from 0.5 closes the gap between them.
float SwarmEngine::_vowel_weight(float hz) const {
    const float h = clampf(_harm_n, 0.f, 1.f) *
                    static_cast<float>(swarm_cfg::kVowelAnchors - 1);
    const int   i0 = static_cast<int>(h);
    const int   i1 = i0 + 1 < swarm_cfg::kVowelAnchors ? i0 + 1 : i0;
    const float t  = h - static_cast<float>(i0);
    // Interpolate in log frequency: a vowel path is a ratio path, not a
    // linear one.
    float f1 = std::exp2(lerpf(std::log2(swarm_cfg::kVowelF1[i0]),
                               std::log2(swarm_cfg::kVowelF1[i1]), t));
    float f2 = std::exp2(lerpf(std::log2(swarm_cfg::kVowelF2[i0]),
                               std::log2(swarm_cfg::kVowelF2[i1]), t));

    const float v = clampf(_targets[LANE_SIZE], 0.f, 1.f);
    const float off = (v - 0.5f) * 2.f;              // -1 .. +1, the SIDE
    const float shift = std::exp2(off);              // +/- one octave
    f1 *= shift;
    f2 *= shift;
    // The DISTANCE from centre closes the gap, in log space around the pair's
    // geometric mean.
    const float close = std::fabs(off) * swarm_cfg::kVowelGapRange;
    const float mid = 0.5f * (std::log2(f1) + std::log2(f2));
    f1 = std::exp2(lerpf(std::log2(f1), mid, close));
    f2 = std::exp2(lerpf(std::log2(f2), mid, close));

    return std::max(_peak(hz, f1), _peak(hz, f2));
}

// One raised-cosine peak, the same shape and the same fast_sin the FOCUS
// window uses, so the two characters sound like the same instrument.
float SwarmEngine::_peak(float hz, float centre_hz) const {
    const float oct = std::log2(hz / centre_hz);
    const float x = clampf(oct / swarm_cfg::kVowelWidthOct, -1.f, 1.f);
    return 0.5f + 0.5f * fast_sin(0.25f + x * 0.5f);
}
```

Declare both in `swarm_engine.h`'s private section. In `_rebuild_targets`, replace the unconditional `a *= _focus_weight(f);` with:

```cpp
        a *= (chr == SwarmChar::Vowel) ? _vowel_weight(f) : _focus_weight(f);
```

and add the VOWEL case to the frequency switch — positions stay harmonic, so it shares LADDER's line:

```cpp
            case SwarmChar::Vowel:
                // Positions stay on the pure series; VOWEL is an AMPLITUDE
                // law. HARM drives the formants, not the frequencies, which
                // is why B is not applied here.
                f = f0 * n;
                break;
```

- [ ] **Step 5: Run the tests**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G35"`
Expected: PASS.

Then: `ctest --test-dir build --output-on-failure`. G15 (FOCUS) must still pass — it runs on a LADDER deck.

- [ ] **Step 6: Commit**

```bash
git add engine/swarm/swarm_config.h engine/swarm/swarm_engine.h engine/swarm/swarm_engine.cpp tests/test_swarm_engine.cpp
git commit -m "feat(swarm): VOWEL, a formant pair where FOCUS had one window"
```

---

### Task 6: BELL — free-bar modal ratios, truncated where they stay reachable

The truncation point is measured, not chosen (spec §4.3): the pure quadratic law leaves 5 of 12 partials past Nyquist at the top of the played range, mode-6 truncation leaves 1, and mode-5 with unit-ratio continuation leaves 0.

**Files:**
- Modify: `engine/swarm/swarm_config.h`, `engine/swarm/swarm_engine.cpp`
- Test: `tests/test_swarm_engine.cpp`

**Interfaces:**
- Consumes: Task 4's `switch (chr)`.
- Produces: `swarm_cfg::kBellModes = 5`, `float bell_ratio(int k)` in `swarm_config.h`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("swarm G36: BELL is the modal set, and every mode stays reachable") {
    SUBCASE("HARM 0 in BELL is still the pure series") {
        // So the zone boundary is silent until HARM is up (spec section 4.3).
        SwarmEngine e = fresh_swarm();
        e.set_character(zone_centre_for_test(2));
        settle(e, 0.f, 1.f / 3.f);
        for (int n = 1; n <= 5; ++n)
            CHECK(cents_off(e, n, 220.f) == doctest::Approx(0.f).epsilon(0.001f));
    }

    SUBCASE("HARM 1 puts the low modes on the bar ratios") {
        SwarmEngine e = fresh_swarm();
        e.set_character(zone_centre_for_test(2));
        settle(e, 1.f, 1.f / 3.f);
        // ((2k+1)/3)^2: mode 2 is 2.778, mode 3 is 5.444.
        CHECK(e.target_hz_for_test(swarm_cfg::kSubPartials + 1) ==
              doctest::Approx(220.f * 2.7778f).epsilon(0.01f));
        CHECK(e.target_hz_for_test(swarm_cfg::kSubPartials + 2) ==
              doctest::Approx(220.f * 5.4444f).epsilon(0.01f));
    }

    SUBCASE("no mode is muted at the TOP of the played range") {
        // The measured defect this task exists to avoid: the untruncated
        // quadratic law left 5 of 12 partials past the Nyquist ceiling at
        // f0 = 880 Hz, i.e. a bell with seven partials.
        SwarmEngine e = fresh_swarm();
        e.set_character(zone_centre_for_test(2));
        settle(e, 1.f, 1.0f);                  // pitch_to_hz(1.0) == 880 Hz
        for (int i = 0; i < swarm_cfg::kPartials; ++i)
            CHECK(e.target_hz_for_test(i) < swarm_cfg::kMaxHzFrac * 48000.f);
    }
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G36"`
Expected: FAIL — BELL falls through to LADDER, so mode 2 sits at 440 Hz, not 611 Hz.

- [ ] **Step 3: Add the ratio law**

In `engine/swarm/swarm_config.h`:

```cpp
// BELL: the free-bar / tubular mode ratios, ((2k+1)/3)^2 -- 1, 2.78, 5.44,
// 9.00, 13.44 -- which are what make a struck bar sound struck.
//
// TRUNCATED, and the truncation point is MEASURED. The quadratic law reaches
// 51x f0 at the twelfth partial; at the top of the played range (880 Hz)
// that is far past the kMaxHzFrac ceiling. Counted against 21600 Hz:
//
//   no truncation                       5 of 12 muted
//   mode 6, local-spacing continuation  1 of 12 muted
//   mode 5, unit-ratio continuation     0 of 12 muted   <- this
//
// Above kBellModes the ratios continue at UNIT spacing, which is harmonic
// spacing at that height -- and physically right as well as affordable: a
// bell's inharmonic strike character lives in its low modes, with dense
// near-harmonic partials above them.
constexpr int kBellModes = 5;

inline float bell_ratio(int k) {
    if (k <= kBellModes) {
        const float a = (2.f * static_cast<float>(k) + 1.f) / 3.f;
        return a * a;
    }
    const float a = (2.f * static_cast<float>(kBellModes) + 1.f) / 3.f;
    return a * a + static_cast<float>(k - kBellModes);
}
```

- [ ] **Step 4: Add the BELL case**

In `_rebuild_targets`'s frequency switch:

```cpp
            case SwarmChar::Bell: {
                // HARM blends harmonic -> modal in LOG frequency, so HARM 0 in
                // BELL is bit-for-bit LADDER's HARM 0 and the zone boundary is
                // silent until the knob is up.
                const float pure = std::log2(n);
                const float modal = std::log2(swarm_cfg::bell_ratio(over));
                f = f0 * std::exp2(lerpf(pure, modal, _harm_n));
                break;
            }
```

- [ ] **Step 5: Run the tests**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G36"`
Expected: PASS, all three subcases.

Then: `ctest --test-dir build --output-on-failure`. G13 (no partial above Nyquist) must still pass.

- [ ] **Step 6: Commit**

```bash
git add engine/swarm/swarm_config.h engine/swarm/swarm_engine.cpp tests/test_swarm_engine.cpp
git commit -m "feat(swarm): BELL, with a truncation the probe chose"
```

---

### Task 7: CHOIR — the partials collapse onto the chord tones

**Files:**
- Modify: `engine/swarm/swarm_config.h`, `engine/swarm/swarm_engine.cpp`
- Test: `tests/test_swarm_engine.cpp`

**Interfaces:**
- Consumes: Task 4's `switch (chr)`, the existing `_spread[]` array.
- Produces: `swarm_cfg::kChoirCentsMax = 50.f`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("swarm G37: CHOIR collapses onto the chord tones and beats") {
    const float pitch = 1.f / 3.f;

    SUBCASE("HARM 0 is a hard unison -- every partial on a tone or its octave") {
        SwarmEngine e = fresh_swarm();
        e.set_character(zone_centre_for_test(3));
        settle(e, 0.f, pitch);
        for (int i = swarm_cfg::kSubPartials; i < swarm_cfg::kPartials; ++i) {
            const float r = e.target_hz_for_test(i) / 220.f;
            // 1x or 2x the tone, nothing between.
            CHECK((r == doctest::Approx(1.f).epsilon(0.001f) ||
                   r == doctest::Approx(2.f).epsilon(0.001f)));
        }
    }

    SUBCASE("HARM 1 spreads them by cents, not by octaves") {
        SwarmEngine e = fresh_swarm();
        e.set_character(zone_centre_for_test(3));
        settle(e, 1.f, pitch);
        bool any_offset = false;
        for (int i = swarm_cfg::kSubPartials; i < swarm_cfg::kPartials; ++i) {
            const float r = e.target_hz_for_test(i) / 220.f;
            const float nearest = r < 1.5f ? 1.f : 2.f;
            const float cents = std::fabs(1200.f * std::log2(r / nearest));
            CHECK(cents <= swarm_cfg::kChoirCentsMax + 1.f);
            if (cents > 1.f) any_offset = true;
        }
        CHECK(any_offset);          // it must actually spread
    }

    SUBCASE("the spread is deterministic for a seed") {
        SwarmEngine a = fresh_swarm(4242u);
        SwarmEngine b = fresh_swarm(4242u);
        a.set_character(zone_centre_for_test(3));
        b.set_character(zone_centre_for_test(3));
        settle(a, 1.f, pitch);
        settle(b, 1.f, pitch);
        for (int i = 0; i < swarm_cfg::kPartials; ++i)
            CHECK(a.target_hz_for_test(i) ==
                  doctest::Approx(b.target_hz_for_test(i)));
    }
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G37"`
Expected: FAIL — CHOIR falls through to LADDER, so partial 3 sits at 3x the tone.

- [ ] **Step 3: Add the constant**

```cpp
// CHOIR: the spread of the collapsed unison, in cents at HARM 1. At 220 Hz a
// 25-cent offset beats at about 3.2 Hz -- chorus, not vibrato. By ear.
constexpr float kChoirCentsMax = 50.f;
```

- [ ] **Step 4: Add the CHOIR case**

In `_rebuild_targets`'s frequency switch:

```cpp
            case SwarmChar::Choir: {
                // Every partial lands on its chord tone or that tone's octave,
                // detuned by its OWN seeded offset. _spread[] is drawn once at
                // init and reseed, so a chord change is still a glissando and
                // NEW is still what redraws the individual (spec section 5).
                //
                // Members alternate fundamental / octave so a group has body
                // instead of being a single loud sine.
                const float member = (over % 2 == 0) ? 2.f : 1.f;
                const float cents =
                    _spread[slot] * swarm_cfg::kChoirCentsMax * _harm_n;
                f = f0 * member * std::exp2(cents * (1.f / 1200.f));
                break;
            }
```

Note `f0` here is already `pitch_to_hz(_chord[tone])` — the chord tone, not the root — so the collapse follows the played chord for free.

- [ ] **Step 5: Run the tests**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G37"`
Expected: PASS, all three subcases.

Then: `ctest --test-dir build --output-on-failure`. G28 (same seed, same audio) and G29 (NEW gives a new individual) both exercise `_spread[]` and must still pass.

- [ ] **Step 6: Commit**

```bash
git add engine/swarm/swarm_config.h engine/swarm/swarm_engine.cpp tests/test_swarm_engine.cpp
git commit -m "feat(swarm): CHOIR, the swarm the name promised"
```

---

### Task 8: FLOOR folds into the top of FALL

**Files:**
- Modify: `engine/swarm/swarm_config.h`, `engine/swarm/swarm_engine.cpp:288-306`
- Test: `tests/test_swarm_engine.cpp` (rewrites G16)

**Interfaces:**
- Consumes: Task 3 (FLOOR is already off the RES slot).
- Produces: `_floor_n` is computed from `_fall_n` inside `_do_bloom` / `_floor_now`; `set_floor` stays as an engine-internal setter for tests but has no host caller.

- [ ] **Step 1: Write the failing test**

Replace `TEST_CASE("swarm G16: ...")` with:

```cpp
TEST_CASE("swarm G16: FALL's top quarter is the drone, its lower travel decays") {
    // FLOOR lost its knob (spec section 6). FALL carries both now: below
    // kDroneStart the deck blooms and decays, above it the floor rises.
    //
    // The measured authority this replaces (spec section 1.3): FLOOR 0 vs 1
    // was 43.6 dB in STEP at the default FALL and 18.4 dB in FLOW.
    auto rms_after = [](float fall_n, bool flow) {
        SwarmEngine e = fresh_swarm();
        feed(e, 1.f / 3.f);
        e.set_character(0.f);
        e.set_rise(0.05f);
        e.set_fall(fall_n);
        e.set_flow(flow);
        if (!flow) e.trigger(1.f / 3.f);
        double acc = 0.0;
        const int total = 48000 * 8, win = 48000 * 7;
        for (int i = 0; i < total; ++i) {
            float l, r; e.process(l, r);
            if (i >= win) acc += 0.5 * (double(l) * l + double(r) * r);
        }
        const double v = std::sqrt(acc / 48000.0);
        return v > 1e-12 ? 20.0 * std::log10(v) : -240.0;
    };

    SUBCASE("FALL at the top stands, in STEP") {
        CHECK(rms_after(1.f, false) > -25.0);
    }
    SUBCASE("FALL in the lower travel decays away, in STEP") {
        CHECK(rms_after(0.2f, false) < -50.0);
    }
    SUBCASE("the top of FALL is louder than the middle by a wide margin") {
        CHECK(rms_after(1.f, false) - rms_after(0.5f, false) > 20.0);
    }
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G16"`
Expected: FAIL — `_floor_n` still sits at its 0.5 default regardless of FALL, so the top and middle of FALL measure the same.

- [ ] **Step 3: Add the constant**

```cpp
// Where FALL stops lengthening the decay and starts raising the floor (spec
// section 6). Below this the whole old FALL range survives intact; above it a
// very long decay becomes a standing drone. By ear.
constexpr float kDroneStart = 0.75f;
```

- [ ] **Step 4: Fold the two**

In `engine/swarm/swarm_engine.cpp`, replace `_do_bloom`'s `rise`/`fall` block's fall line and add the floor derivation. `_floor_now()` keeps FLOW's `kFlowFloorMin` behaviour untouched:

```cpp
// FALL carries the decay time AND the sustain floor (spec section 6). Below
// kDroneStart it is the decay alone and the floor is 0; above it the decay
// stays at its maximum and the floor rises, so the knob's last quarter turns a
// very long decay into a standing drone.
float SwarmEngine::_fall_seconds() const {
    const float t = clampf(_fall_n / swarm_cfg::kDroneStart, 0.f, 1.f);
    return lerpf(swarm_cfg::kFallMinS, swarm_cfg::kFallMaxS, t);
}

float SwarmEngine::_floor_from_fall() const {
    if (_fall_n <= swarm_cfg::kDroneStart) return 0.f;
    return (_fall_n - swarm_cfg::kDroneStart) / (1.f - swarm_cfg::kDroneStart);
}
```

`set_fall` writes `_floor_n = _floor_from_fall();` after storing `_fall_n`, and `_do_bloom` uses `_fall_seconds()` in place of its `lerpf`. `_floor_now()` is unchanged — it still reads `_floor_n`, still applies `kFlowFloorMin` in FLOW and still returns 0 under `_hold`.

- [ ] **Step 5: Run the tests**

Run: `cmake --build build && ./build/spky_tests -tc="swarm G16"`
Expected: PASS, all three subcases.

Then: `ctest --test-dir build --output-on-failure`. G17 (FLOW's minimum floor), G18 (CHOKE decays the drone out), G19 and G20 (the accent) all read the floor and must still pass. If G20 fails, it is because the accent's FALL term now multiplies a different number — re-derive its expectation from `_fall_seconds()`, do not loosen the assertion.

- [ ] **Step 6: Commit**

```bash
git add engine/swarm/swarm_config.h engine/swarm/swarm_engine.h engine/swarm/swarm_engine.cpp tests/test_swarm_engine.cpp
git commit -m "feat(swarm): FALL's last quarter is the drone now"
```

---

### Task 9: The panel says CHARACTER

**Files:**
- Modify: `host/vcv/res/gen_panel.py:260`
- Modify: `host/vcv/res/test_panel.py`
- Regenerate: `host/vcv/res/*.svg` via the generator

**Interfaces:**
- Consumes: nothing from earlier tasks — this is the plate, not the engine.
- Produces: the RES slot prints `CHAR` on a SWARM deck.

- [ ] **Step 1: Write the failing guard**

In `host/vcv/res/test_panel.py`, beside the other caption checks:

```python
def test_res_prints_char_on_swarm():
    """RES carries CHARACTER on a SWARM deck (spec 2026-08-17 section 4).
    It used to print FLOOR; FLOOR folded into FALL and has no knob."""
    row = dict((c[0], c) for c in DYNAMIC_CAPTIONS)["RES"]
    captions = row[2]
    assert captions[-1] == "CHAR", (
        "the SWARM column of the RES caption row must read CHAR, not %r"
        % (captions[-1],))
    assert "FLOOR" not in captions, "FLOOR no longer has a knob"
```

- [ ] **Step 2: Run it and watch it fail**

Run: `python host/vcv/res/test_panel.py`
Expected: FAIL — the SWARM column currently reads `FLOOR`.

Note: pytest is **not** installed on this machine (`fireflow-vcv-host-build-env`). The panel guards run as plain scripts, so the file's `__main__` block must call the new function like the existing ones do.

- [ ] **Step 3: Change the caption**

In `host/vcv/res/gen_panel.py:260`:

```python
    ("RES",      "ENGINE",   ("RES",  "RES",  "RES",   "CHAR",  "TILT", "CHAR")),
```

BODY's column already reads `CHAR` and means the same thing there — which character. Reusing the word is honest, and the plate is read per engine.

- [ ] **Step 4: Regenerate and verify**

Run:
```bash
python host/vcv/res/gen_panel.py
python host/vcv/res/test_panel.py
python host/vcv/res/gen_hw_panel.py
python host/vcv/res/test_hw_panel.py
```
Expected: all four succeed; the SVGs change. Both panels are generated and never hand-edited.

- [ ] **Step 5: Commit**

```bash
git add host/vcv/res/gen_panel.py host/vcv/res/test_panel.py host/vcv/res/*.svg
git commit -m "feat(vcv): the RES slot prints CHAR on a swarm deck"
```

---

### Task 10: Re-measure the CPU on the board

`_rebuild_targets` gained a switch, a formant evaluation and a modal ratio. The kernel is untouched, so N is not in question from the bank's side — but the control tick is where the swarm's cost actually sits, and the gate is relative.

**Do not run this in a shell that has sourced `env.sh`.** The ARM and desktop toolchains must not mix.

**Files:**
- Create: `docs/bench/2026-08-17-<hash>-swarm-*.md` (written by the runner)
- Modify: `engine/swarm/swarm_config.h` **only if the gate fails**

**Interfaces:**
- Consumes: Tasks 4–8.
- Produces: a measured verdict on N.

- [ ] **Step 1: Read the bench rules before running anything**

Read `bench/README.md` and the memories `fireflow-bench-stale-object-trap` and `fireflow-bench-clean-tree-guard`. Two traps: the build can silently relink a stale object (verify new rows through `bench.map`, never the memory table), and `run.py` refuses a dirty tree while its own output dirties it.

- [ ] **Step 2: Commit everything first, then run**

```bash
git status --porcelain          # must be empty before run.py will start
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" python bench/run.py --profile swarm --repeat 2
```

- [ ] **Step 3: Read the gate, inside the image**

The gate is RELATIVE and is read **inside one image, never across two**: `inst_swarm_engine_worst` must not exceed the same image's `instrument_worst`. It cannot be read against the 960000-cycle block budget directly — `instrument_worst` is itself at or over 100 % in every image, so the headroom it leaves is negative.

Expected: PASS with a margin. The N = 14 decision carried 4.1 points on the average and 1.8 on the maximum.

- [ ] **Step 4: If it fails, drop N to 12**

12 was measured on 2026-08-17 as the fallback and carried 8.5 / 6.3 points. Change `kPartials` in `engine/swarm/swarm_config.h`, update its block comment with the new measurement, and re-run. **Do not** change anything else to buy budget — N is the lever the bench already priced.

- [ ] **Step 5: Commit the evidence**

```bash
git add docs/bench/
git commit -m "bench(swarm): the control tick got wider, and the relative gate still holds"
```

---

## Self-Review

**Spec coverage.** §3.1 → Task 1. §4 zones and hysteresis → Task 2, wiring → Task 3. §4.1 LADDER → Task 4. §4.2 VOWEL → Task 5. §4.3 BELL → Task 6. §4.4 CHOIR → Task 7. §4.5 BAL stays live in LADDER and VOWEL and inert elsewhere — this falls out of the code as written (the even/odd multiply runs on `over`, which is meaningless in CHOIR and BELL) and is **not** separately gated; that gap is deliberate and noted here rather than hidden. §5 HARM unified → Tasks 4–7 together. §6 FALL fold → Task 8. §7 breath → the second plan, not this one. §8 cost → Task 10. §9 gates: G-A → G11, G-B → G11's monotone subcase, G-C → G35/G36/G37 collectively, G-D → C4/C5, G-I → G16, G-J → Task 10, G-K → not yet gated (it belongs with the breath plan's audio checks), G-L → existing G27, untouched.

**Placeholders.** None: every code step carries the actual code, and every test step the actual assertions.

**Type consistency.** `SwarmChar` / `CharZone` / `set_character` / `character_for_test` / `target_amp_for_test` / `zone_centre_for_test` / `_vowel_weight` / `_peak` / `_fall_seconds` / `_floor_from_fall` / `bell_ratio` are each defined in exactly one task and used with the same name and signature afterwards. `settle()` and `cents_off()` are defined in Task 4's test block and reused by Tasks 5–7; a subagent implementing Task 6 in isolation must add them if Task 4's commit is not in its tree — noted here because the tasks are otherwise independent.

**Known gap.** G-K (no character produces silence at any setting) has no task in this plan. It needs the audio-level gate the breath plan introduces, and it is listed as the first task there.
