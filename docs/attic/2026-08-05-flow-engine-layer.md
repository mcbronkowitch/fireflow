# Flow Engine Layer (Plan A) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `engine/flow/` — the terrain generator, story-curve macro layer, weather, NEW state machine and gesture decoder from `docs/superpowers/specs/2026-08-05-flow-machine-design.md` — driveable from the render host, with the spec's §7 test suite. The VCV module is **Plan B** (separate plan); nothing in this plan touches `host/vcv/`.

**Architecture:** Pure control-rate logic on `Instrument`'s public setters. Three layers: data (`taste.h` tables), generation (`terrain.*`: state → full patch + mappings, deterministic via the existing `spky::Rng`), runtime (`flow.*`: macros + CV + weather + blend → setter pushes; `gesture.h`: button/knob events → semantic ops). No heap, no globals, fixed-size arrays throughout (`FxMem` precedent).

**Tech Stack:** C++17, doctest (vendored), nlohmann_json (render host only), CMake/Ninja/clang.

## Global Constraints

- Build only per CLAUDE.md: `source env.sh`, `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, `cmake --build build`. **Release is not optional** (Debug breaks the render-hash gates of *existing* tests).
- Run tests: `ctest --test-dir build --output-on-failure` or directly `./build/spky_tests.exe -tc="<pattern>"`.
- Every new test must be proven RED once (run before the implementation exists or with it stubbed) — project convention "a test that cannot go red gets fixed".
- No new render-hash/byte-identity gates. Audio checks are sanity bounds (RMS, NaN, dB deltas).
- No heap allocation inside `engine/` code; fixed-size arrays only. `engine/flow/` must not include any host header.
- Commit trailer (every commit): `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- Spec is the authority: `docs/superpowers/specs/2026-08-05-flow-machine-design.md`. By-ear values (memory `spotykach-by-ear-decisions`) become range centers, never get overwritten.
- Naming trap: `Instrument::weather()` **already exists** (Center's weather, unrelated). Everything in this plan lives in `namespace spky::flow`; never touch or shadow Center's weather.
- All tuning numbers introduced here (spans, floors, weather depths, thresholds) are **first guesses for the listening phase** — put them in `taste.h`, never inline in logic.

---

### Task 1: Flow parameter table (`flow_params.h`)

The schema §4 calls "the implementation plan's first task": every parameter the flow layer owns, with real ranges/types, and one function that routes a value to the right `Instrument` setter.

**Files:**
- Create: `engine/flow/flow_ids.h`
- Create: `engine/flow/flow_params.h`
- Test: `tests/test_flow_params.cpp`
- Modify: `CMakeLists.txt` (add test file to `spky_tests` sources)

**Interfaces:**
- Produces: `spky::flow::Macro` (`M_MOTION, M_DENSITY, M_BRIGHT, M_DIRT, M_WANDER, M_SPACE, MACRO_COUNT`), `spky::flow::Archetype` (`ARCH_DRONE, ARCH_PULSE, ARCH_ARP, ARCH_FRAGMENT, ARCH_COUNT`), `spky::flow::ParamId` enum with `P_COUNT`, `struct ParamInfo { const char* name; float lo, hi; int steps; }` (`steps == 0` → continuous), `constexpr ParamInfo kParams[P_COUNT]`, `void apply_param(Instrument&, int param, float v)`.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_flow_params.cpp
#include "doctest/doctest.h"
#include "flow/flow_params.h"
#include "instrument.h"
using namespace spky;
using namespace spky::flow;

TEST_CASE("flow params: table is sane") {
    for (int p = 0; p < P_COUNT; ++p) {
        CAPTURE(kParams[p].name);
        CHECK(kParams[p].lo < kParams[p].hi);
        if (kParams[p].steps > 0) CHECK(kParams[p].steps >= 2);
    }
}

TEST_CASE("flow params: apply routes to the engine (spot checks via observers)") {
    Instrument inst;
    inst.init(48000.f);                       // engine only, no FX chain needed
    apply_param(inst, P_ENGINE_A, float(ENGINE_BODY));
    CHECK(inst.engine_id(PART_A) == ENGINE_BODY);
    apply_param(inst, P_FORM_A, 2.f);
    CHECK(inst.form(PART_A) == 2);
    apply_param(inst, P_SONG_B, 1.f);
    CHECK(inst.song(PART_B) == 1);
    // Discrete params clamp, never wrap: over-range engine id stays legal.
    apply_param(inst, P_ENGINE_B, 99.f);
    CHECK(inst.engine_id(PART_B) >= 0);
    CHECK(inst.engine_id(PART_B) < ENGINE_COUNT);
}
```

Note for the implementer: `ENGINE_COUNT`'s actual spelling lives in `engine/parts/engine_iface.h` — check it (and `Principle::kCount` in `engine/mod/song_form.h`, and the SONG clamp in `Instrument::clamp_song`) before writing the table; the values below marked `/*verify*/` must match those headers.

- [ ] **Step 2: Run test to verify it fails**

Run: `./build/spky_tests.exe -tc="flow params*"` (after `cmake --build build`)
Expected: build FAILURE — `flow/flow_params.h` does not exist. That is this task's RED.

- [ ] **Step 3: Write the implementation**

```cpp
// engine/flow/flow_ids.h
#pragma once
namespace spky { namespace flow {

enum Macro { M_MOTION = 0, M_DENSITY, M_BRIGHT, M_DIRT, M_WANDER, M_SPACE,
             MACRO_COUNT };
enum Archetype { ARCH_DRONE = 0, ARCH_PULSE, ARCH_ARP, ARCH_FRAGMENT,
                 ARCH_COUNT };

} } // namespace spky::flow
```

```cpp
// engine/flow/flow_params.h
#pragma once
#include "flow/flow_ids.h"
#include "instrument.h"

namespace spky { namespace flow {

// Every parameter the flow layer owns. Ranges are ENGINE units (§2: the
// surface is not uniformly 0..1 — FILT/VARIATION/LINK/CHOKE are bipolar,
// ENGINE/SCALE/ROOT/FORM/SONG/STEPS are discrete). steps==0 -> continuous.
// The RES ceiling 0.75 encodes the by-ear resonance cap as a hard limit.
#define SPKY_FLOW_PARAMS(X) \
  X(P_ENGINE_A,   0.f, 4.f,  5)  X(P_ENGINE_B,   0.f, 4.f,  5) /*verify ENGINE ids 0..4*/ \
  X(P_SCALE,      0.f, 12.f, 13) X(P_ROOT,       0.f, 11.f, 12) \
  X(P_FORM_A,     0.f, 3.f,  4)  X(P_FORM_B,     0.f, 3.f,  4) /*verify Principle::kCount*/ \
  X(P_SONG_A,     0.f, 3.f,  4)  X(P_SONG_B,     0.f, 3.f,  4) /*verify clamp_song*/ \
  X(P_STEPS_A,    2.f, 16.f, 15) X(P_STEPS_B,    2.f, 16.f, 15) \
  X(P_TUNE_A,     0.f, 1.f, 0)   X(P_TUNE_B,     0.f, 1.f, 0) \
  X(P_RATE_A,     0.f, 1.f, 0)   X(P_RATE_B,     0.f, 1.f, 0) \
  X(P_SHAPE_A,    0.f, 1.f, 0)   X(P_SHAPE_B,    0.f, 1.f, 0) \
  X(P_DENSITY_A,  0.f, 1.f, 0)   X(P_DENSITY_B,  0.f, 1.f, 0) \
  X(P_SMOOTH_A,   0.f, 1.f, 0)   X(P_SMOOTH_B,   0.f, 1.f, 0) \
  X(P_RANGE_A,    0.f, 1.f, 0)   X(P_RANGE_B,    0.f, 1.f, 0) \
  X(P_DEPTH_A,    0.f, 1.f, 0)   X(P_DEPTH_B,    0.f, 1.f, 0) \
  X(P_COLOR_A,    0.f, 1.f, 0)   X(P_COLOR_B,    0.f, 1.f, 0) \
  X(P_VARIATION_A,-1.f, 1.f, 0)  X(P_VARIATION_B,-1.f, 1.f, 0) \
  X(P_ATTACK_A,   0.f, 1.f, 0)   X(P_ATTACK_B,   0.f, 1.f, 0) \
  X(P_DECAY_A,    0.f, 1.f, 0)   X(P_DECAY_B,    0.f, 1.f, 0) \
  X(P_RES_A,      0.f, 0.75f, 0) X(P_RES_B,      0.f, 0.75f, 0) \
  X(P_SUB_A,      0.f, 1.f, 0)   X(P_SUB_B,      0.f, 1.f, 0) \
  X(P_FILT_A,    -1.f, 1.f, 0)   X(P_FILT_B,    -1.f, 1.f, 0) \
  X(P_FLUXMIX_A,  0.f, 1.f, 0)   X(P_FLUXMIX_B,  0.f, 1.f, 0) \
  X(P_GRIT_A,     0.f, 1.f, 0)   X(P_GRIT_B,     0.f, 1.f, 0) \
  X(P_COMP_A,     0.f, 1.f, 0)   X(P_COMP_B,     0.f, 1.f, 0) \
  X(P_LINK_A,    -1.f, 1.f, 0)   X(P_LINK_B,    -1.f, 1.f, 0) \
  X(P_REVMIX_A,   0.f, 1.f, 0)   X(P_REVMIX_B,   0.f, 1.f, 0) \
  X(P_MORPH,      0.f, 1.f, 0)   X(P_COUPLE,     0.f, 1.f, 0) \
  X(P_DRIFT,      0.f, 1.f, 0)   X(P_TIDE,       0.f, 1.f, 0) \
  X(P_CHOKE,     -1.f, 1.f, 0)   X(P_SHUFFLE,    0.f, 1.f, 0) \
  X(P_DRIVE,      0.f, 1.f, 0) \
  X(P_REV_SIZE,   0.f, 1.f, 0)   X(P_REV_DECAY,  0.f, 1.f, 0) \
  X(P_REV_TONE,   0.f, 1.f, 0)   X(P_REV_DIFF,   0.f, 1.f, 0) \
  X(P_REV_SMEAR,  0.f, 1.f, 0)   X(P_REV_MOD,    0.f, 1.f, 0) \
  X(P_TEMPO_BPM, 50.f, 140.f, 0)

enum ParamId {
#define SPKY_FLOW_ENUM(id, lo, hi, st) id,
  SPKY_FLOW_PARAMS(SPKY_FLOW_ENUM)
#undef SPKY_FLOW_ENUM
  P_COUNT
};

struct ParamInfo { const char* name; float lo, hi; int steps; };

constexpr ParamInfo kParams[P_COUNT] = {
#define SPKY_FLOW_INFO(id, lo_, hi_, st) { #id, lo_, hi_, st },
  SPKY_FLOW_PARAMS(SPKY_FLOW_INFO)
#undef SPKY_FLOW_INFO
};

inline float clamp_to(const ParamInfo& pi, float v) {
    if (v < pi.lo) v = pi.lo;
    if (v > pi.hi) v = pi.hi;
    return v;
}

// Route one parameter value to the engine. Discrete params arrive as floats
// and are rounded here; every value is clamped to the table range first.
inline void apply_param(Instrument& in, int param, float v) {
    v = clamp_to(kParams[param], v);
    const int i = int(v + 0.5f);
    switch (param) {
    case P_ENGINE_A:   in.set_engine(PART_A, EngineId(i)); break;
    case P_ENGINE_B:   in.set_engine(PART_B, EngineId(i)); break;
    case P_SCALE:      in.set_scale(i); break;
    case P_ROOT:       in.set_root(PART_A, i); in.set_root(PART_B, i); break;
    case P_FORM_A:     in.set_form(PART_A, i); break;
    case P_FORM_B:     in.set_form(PART_B, i); break;
    case P_SONG_A:     in.set_song(PART_A, i); break;
    case P_SONG_B:     in.set_song(PART_B, i); break;
    case P_STEPS_A:    in.set_step(PART_A, true, i); break;
    case P_STEPS_B:    in.set_step(PART_B, true, i); break;
    case P_TUNE_A:     in.set_tune(PART_A, v); break;
    case P_TUNE_B:     in.set_tune(PART_B, v); break;
    case P_RATE_A:     in.set_rate(PART_A, v); break;
    case P_RATE_B:     in.set_rate(PART_B, v); break;
    case P_SHAPE_A:    in.set_shape(PART_A, v); break;
    case P_SHAPE_B:    in.set_shape(PART_B, v); break;
    case P_DENSITY_A:  in.set_density(PART_A, v); break;
    case P_DENSITY_B:  in.set_density(PART_B, v); break;
    case P_SMOOTH_A:   in.set_smooth(PART_A, v); break;
    case P_SMOOTH_B:   in.set_smooth(PART_B, v); break;
    case P_RANGE_A:    in.set_range(PART_A, v); break;
    case P_RANGE_B:    in.set_range(PART_B, v); break;
    case P_DEPTH_A:    in.set_depth(PART_A, v); break;
    case P_DEPTH_B:    in.set_depth(PART_B, v); break;
    case P_COLOR_A:    in.set_color(PART_A, v); break;
    case P_COLOR_B:    in.set_color(PART_B, v); break;
    case P_VARIATION_A: in.set_variation(PART_A, v); break;
    case P_VARIATION_B: in.set_variation(PART_B, v); break;
    case P_ATTACK_A:   in.set_voice_attack(PART_A, v); break;
    case P_ATTACK_B:   in.set_voice_attack(PART_B, v); break;
    case P_DECAY_A:    in.set_voice_decay(PART_A, v); break;
    case P_DECAY_B:    in.set_voice_decay(PART_B, v); break;
    case P_RES_A:      in.set_voice_resonance(PART_A, v); break;
    case P_RES_B:      in.set_voice_resonance(PART_B, v); break;
    case P_SUB_A:      in.set_voice_sub(PART_A, v); break;
    case P_SUB_B:      in.set_voice_sub(PART_B, v); break;
    case P_FILT_A:     in.set_voice_filt(PART_A, v); break;
    case P_FILT_B:     in.set_voice_filt(PART_B, v); break;
    case P_FLUXMIX_A:  in.set_flux_mix(PART_A, v); break;
    case P_FLUXMIX_B:  in.set_flux_mix(PART_B, v); break;
    case P_GRIT_A:     in.set_grit_mix(PART_A, v); break;
    case P_GRIT_B:     in.set_grit_mix(PART_B, v); break;
    case P_COMP_A:     in.set_comp(PART_A, v); break;
    case P_COMP_B:     in.set_comp(PART_B, v); break;
    case P_LINK_A:     in.set_link(PART_A, v); break;
    case P_LINK_B:     in.set_link(PART_B, v); break;
    case P_REVMIX_A:   in.set_reverb_mix(PART_A, v); break;
    case P_REVMIX_B:   in.set_reverb_mix(PART_B, v); break;
    case P_MORPH:      in.set_morph(v); break;
    case P_COUPLE:     in.set_couple(v); break;
    case P_DRIFT:      in.set_drift(v); break;
    case P_TIDE:       in.set_tide(v); break;
    case P_CHOKE:      in.set_choke(v); break;
    case P_SHUFFLE:    in.set_shuffle(v); break;
    case P_DRIVE:      in.set_master_drive(v); break;
    case P_REV_SIZE:   in.set_reverb_size(v); break;
    case P_REV_DECAY:  in.set_reverb_decay(v); break;
    case P_REV_TONE:   in.set_reverb_tone(v); break;
    case P_REV_DIFF:   in.set_reverb_diffusion(v); break;
    case P_REV_SMEAR:  in.set_reverb_smear(v); break;
    case P_REV_MOD:    in.set_reverb_mod(v); break;
    case P_TEMPO_BPM:  in.set_tempo_bpm(v); break;
    default: break;
    }
}

} } // namespace spky::flow
```

Add `tests/test_flow_params.cpp` to the `spky_tests` source list in `CMakeLists.txt` (alphabetically near the other `tests/test_*.cpp` entries; `flow_params.h` is header-only, nothing else to add).

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ./build/spky_tests.exe -tc="flow params*"`
Expected: PASS (2 test cases). Also run the full `ctest --test-dir build --output-on-failure` — nothing else may break.

- [ ] **Step 5: Commit**

```bash
git add engine/flow/flow_ids.h engine/flow/flow_params.h tests/test_flow_params.cpp CMakeLists.txt
git commit -m "feat(flow): the parameter table - every knob the terrain may own"
```

---

### Task 2: Deterministic per-parameter RNG streams (`flow_rng.h`)

Spec §4: every drawn value gets its stream from `(master seed, parameter id, override counter)` so a partial reroll never shifts a neighboring stream. Reuses the existing `spky::Rng` (xorshift32, `engine/mod/rng.h`) — the §2 owned-PRNG rule is satisfied by reuse, not new code.

**Files:**
- Create: `engine/flow/flow_rng.h`
- Test: `tests/test_flow_rng.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `spky::Rng` from `engine/mod/rng.h`.
- Produces: `uint32_t stream_seed(uint32_t master, uint32_t stream_id, uint32_t counter)`, `Rng make_stream(uint32_t master, uint32_t stream_id, uint32_t counter)`. Stream ids: `kStreamParamBase + ParamId` for stage-3 draws, `kStreamMacroBase + Macro` for stage-4 draws, `kStreamArch`, `kStreamRoles`, `kStreamTonality`, `kStreamWeather`, `kStreamDistance` for the stage-level draws.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_flow_rng.cpp
#include "doctest/doctest.h"
#include "flow/flow_rng.h"
using namespace spky::flow;

TEST_CASE("flow rng: streams are deterministic and independent") {
    // Same triple -> same sequence.
    auto a = make_stream(0xDEADBEEF, kStreamParamBase + 3, 0);
    auto b = make_stream(0xDEADBEEF, kStreamParamBase + 3, 0);
    for (int i = 0; i < 16; ++i) CHECK(a.next_u32() == b.next_u32());
    // Bumping ONE counter changes only that stream: id 3's sequence moves,
    // id 4's is untouched.
    auto c0 = make_stream(0xDEADBEEF, kStreamParamBase + 4, 0);
    auto c1 = make_stream(0xDEADBEEF, kStreamParamBase + 4, 0);
    auto d  = make_stream(0xDEADBEEF, kStreamParamBase + 3, 1);
    CHECK(c0.next_u32() == c1.next_u32());          // neighbor unmoved
    auto e = make_stream(0xDEADBEEF, kStreamParamBase + 3, 0);
    CHECK(d.next_u32() != e.next_u32());            // own stream moved
    // Different masters diverge.
    auto f = make_stream(0xDEADBEEF, kStreamArch, 0);
    auto g = make_stream(0xDEADBEF0, kStreamArch, 0);
    CHECK(f.next_u32() != g.next_u32());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build` → build error (`flow/flow_rng.h` missing). RED proven.

- [ ] **Step 3: Write the implementation**

```cpp
// engine/flow/flow_rng.h
#pragma once
#include <cstdint>
#include "mod/rng.h"

namespace spky { namespace flow {

// Stream id blocks. Params and macros get one id each; the stage-level
// draws get fixed ids above both blocks.
enum : uint32_t {
    kStreamParamBase = 0,          // + ParamId
    kStreamMacroBase = 1000,       // + Macro
    kStreamArch      = 2000,
    kStreamRoles     = 2001,
    kStreamTonality  = 2002,
    kStreamWeather   = 2003,
    kStreamDistance  = 2004,
};

// splitmix32-style avalanche of the (master, stream, counter) triple.
// One multiply-xor round per word is enough: Rng itself keeps mixing.
inline uint32_t stream_seed(uint32_t master, uint32_t stream_id,
                            uint32_t counter) {
    uint32_t h = master;
    h ^= stream_id + 0x9E3779B9u + (h << 6) + (h >> 2);
    h *= 0x85EBCA6Bu; h ^= h >> 13;
    h ^= counter + 0x9E3779B9u + (h << 6) + (h >> 2);
    h *= 0xC2B2AE35u; h ^= h >> 16;
    return h ? h : 0x1u;           // Rng::seed treats 0 as 1 anyway
}

inline Rng make_stream(uint32_t master, uint32_t stream_id,
                       uint32_t counter) {
    Rng r;
    r.seed(stream_seed(master, stream_id, counter));
    return r;
}

} } // namespace spky::flow
```

Add `tests/test_flow_rng.cpp` to `CMakeLists.txt`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build && ./build/spky_tests.exe -tc="flow rng*"` → PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/flow/flow_rng.h tests/test_flow_rng.cpp CMakeLists.txt
git commit -m "feat(flow): per-parameter rng streams - rerolls never shift a neighbor"
```

---

### Task 3: Taste tables (`taste.h`)

All tuning data in one header: archetype weights, per-archetype base-draw ranges, named constraints, the v1 story library (one variant per macro, two for DENSITY), weather config, gesture/blend thresholds. **Data, not code** — the listening loop edits this file only.

**Files:**
- Create: `engine/flow/taste.h`
- Test: `tests/test_flow_taste.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ParamId`, `Macro`, `Archetype`, `kParams`.
- Produces:

```cpp
namespace spky::flow {
struct Span   { float lo, hi; };                     // draw range, engine units
struct BaseRule { int param; Span per_arch[ARCH_COUNT]; };
struct CurveRule { int param; Span bp[5]; };         // per-breakpoint draw spans
struct StoryVariant {
    Macro macro; const char* name;
    int n_targets; CurveRule targets[6];             // max 6 targets per macro
};
// tables:
extern const float    kArchWeight[ARCH_COUNT];       // drone-heavy
extern const BaseRule kBaseRules[];  extern const int kBaseRuleCount;
extern const StoryVariant kStories[]; extern const int kStoryCount;
// scalar tuning:
constexpr float kWeatherDepthMax = 0.10f, kWeatherDepthMin = 0.05f;
constexpr float kWeatherPeriodMinS = 300.f, kWeatherPeriodMaxS = 1200.f;
constexpr int   kWeatherOscMin = 2, kWeatherOscMax = 4;
constexpr float kBlendS = 6.f, kMinSpan = 0.08f;
constexpr float kTapMaxS = 0.4f, kUndoArmS = 1.5f, kLockS = 5.f;
constexpr float kMarkDelta = 0.01f;
constexpr float kDistanceMin = 0.18f;               // NEW rejection threshold
constexpr float kCalmCornerRmsMax = 0.06f;          // §7.8 ceiling, lin FS
constexpr float kBodyFiltFloor = -0.3f;             // BODY FILT cliff margin
constexpr float kSpaceSlewS = 2.5f;                 // lazy SIZE/DECAY follower
constexpr float kHysteresisFrac = 0.5f;             // half a discrete step
}
```

- [ ] **Step 1: Write the failing test**

```cpp
// tests/test_flow_taste.cpp
#include "doctest/doctest.h"
#include "flow/taste.h"
#include "flow/flow_params.h"
using namespace spky::flow;

TEST_CASE("flow taste: static data is internally consistent") {
    // Base rules stay inside the param table's legal range.
    for (int i = 0; i < kBaseRuleCount; ++i)
        for (int a = 0; a < ARCH_COUNT; ++a) {
            const auto& r = kBaseRules[i];
            CAPTURE(kParams[r.param].name);
            CHECK(r.per_arch[a].lo >= kParams[r.param].lo);
            CHECK(r.per_arch[a].hi <= kParams[r.param].hi);
            CHECK(r.per_arch[a].lo <= r.per_arch[a].hi);
        }
    // Every macro has at least one story; DENSITY has two.
    int per_macro[MACRO_COUNT] = {};
    for (int s = 0; s < kStoryCount; ++s) per_macro[kStories[s].macro]++;
    for (int m = 0; m < MACRO_COUNT; ++m) CHECK(per_macro[m] >= 1);
    CHECK(per_macro[M_DENSITY] == 2);
    // Story breakpoint spans: inside the param range, and monotone in the
    // direction bp0 -> bp4 (lo bounds non-decreasing or non-increasing).
    for (int s = 0; s < kStoryCount; ++s)
        for (int t = 0; t < kStories[s].n_targets; ++t) {
            const auto& c = kStories[s].targets[t];
            bool up = c.bp[4].lo >= c.bp[0].lo;
            for (int b = 0; b < 5; ++b) {
                CHECK(c.bp[b].lo >= kParams[c.param].lo);
                CHECK(c.bp[b].hi <= kParams[c.param].hi);
                if (b > 0) {
                    if (up) CHECK(c.bp[b].lo >= c.bp[b-1].lo);
                    else    CHECK(c.bp[b].lo <= c.bp[b-1].lo);
                }
            }
        }
    // MOTION's and DIRT's Q4 cells may exceed centers (risk zone) but the
    // hard param limit still caps them: already covered by the range check.
    // Archetype weights: drone is the heaviest.
    for (int a = 1; a < ARCH_COUNT; ++a)
        CHECK(kArchWeight[ARCH_DRONE] >= kArchWeight[a]);
}
```

- [ ] **Step 2: Run test to verify it fails**

`cmake --build build` → build error (`flow/taste.h` missing). RED proven.

- [ ] **Step 3: Write the implementation**

`taste.h` with the interface above and this v1 data (all values are listening-phase first guesses; comments name the spec cell they implement). The story library implements §3's table — targets chosen from the pools; DENSITY split per the two variants:

```cpp
// engine/flow/taste.h  (excerpt — the full file carries all six macros)
inline const StoryVariant kStories[] = {
// MOTION "still photo -> breathing -> wobble -> seasick" (§3 row 3).
// Q4 hi values deliberately sit at the params' full ceiling: the risk zone.
{ M_MOTION, "orbit", 4, {
  { P_TIDE,      {{0.f,.05f},{.1f,.2f},{.25f,.4f},{.45f,.6f},{.7f,1.f}} },
  { P_DRIFT,     {{0.f,.05f},{.05f,.15f},{.2f,.35f},{.4f,.55f},{.6f,.9f}} },
  { P_REV_SMEAR, {{0.f,.05f},{.05f,.15f},{.15f,.3f},{.3f,.5f},{.5f,.8f}} },
  { P_REV_MOD,   {{0.f,.05f},{.05f,.1f},{.1f,.25f},{.25f,.45f},{.45f,.85f}} } } },
// DENSITY rate-led (§3 row 2a): events carry the sweep.
{ M_DENSITY, "rate", 3, {
  { P_DENSITY_A, {{.02f,.08f},{.1f,.2f},{.3f,.5f},{.5f,.7f},{.7f,.95f}} },
  { P_DENSITY_B, {{.02f,.08f},{.08f,.18f},{.25f,.45f},{.45f,.65f},{.65f,.9f}} },
  { P_STEPS_A,   {{2.f,4.f},{4.f,6.f},{6.f,10.f},{10.f,13.f},{13.f,16.f}} } } },
// DENSITY thickness-led (§3 row 2b): chords/pad carry it.
{ M_DENSITY, "thick", 3, {
  { P_COLOR_A,   {{0.f,.1f},{.15f,.3f},{.35f,.55f},{.55f,.75f},{.75f,1.f}} },
  { P_COLOR_B,   {{0.f,.1f},{.1f,.25f},{.3f,.5f},{.5f,.7f},{.7f,.95f}} },
  { P_SUB_A,     {{.1f,.2f},{.2f,.35f},{.35f,.5f},{.5f,.65f},{.6f,.8f}} } } },
// BRIGHT "ember -> sweep -> open -> air" (§3 row 1). Q1 dips the dry leg
// via REVMIX (the spec-named level mechanism) and blooms REV_DECAY --
// REV_DECAY's curve here runs HIGH at bp0 and settles by bp1: monotone
// falling, active only in Q1. REVMIX likewise falls Q1-only.
{ M_BRIGHT, "dawn", 5, {
  { P_FILT_A,    {{-.55f,-.4f},{-.3f,-.1f},{0.f,.2f},{.3f,.5f},{.6f,.9f}} },
  { P_FILT_B,    {{-.55f,-.4f},{-.3f,-.1f},{0.f,.2f},{.3f,.5f},{.6f,.9f}} },
  { P_REV_TONE,  {{.1f,.2f},{.25f,.4f},{.4f,.55f},{.55f,.7f},{.7f,.9f}} },
  { P_REVMIX_A,  {{.75f,.9f},{.45f,.6f},{.4f,.55f},{.4f,.55f},{.4f,.55f}} },
  { P_REV_DECAY, {{.75f,.9f},{.5f,.65f},{.5f,.65f},{.5f,.65f},{.5f,.65f}} } } },
// DIRT "clean glue -> warmth -> grit -> risk + DRIVE threshold" (§3 row 4).
// P_DRIVE flat 0 through Q1..Q3, joins in Q4 only (the threshold rule).
{ M_DIRT, "heat", 4, {
  { P_GRIT_A,    {{0.f,0.f},{.05f,.15f},{.2f,.4f},{.45f,.65f},{.7f,1.f}} },
  { P_GRIT_B,    {{0.f,0.f},{.05f,.12f},{.15f,.35f},{.4f,.6f},{.65f,.95f}} },
  { P_COMP_A,    {{.3f,.5f},{.3f,.5f},{.35f,.55f},{.4f,.6f},{.5f,.75f}} },
  { P_DRIVE,     {{0.f,0.f},{0.f,0.f},{0.f,0.f},{0.f,.05f},{.3f,.7f}} } } },
// WANDER "frozen -> fine variation -> melodic wander -> FORM/SONG churn"
// (§3 row 5). FORM/SONG are discrete: flat until Q4, hysteresis in Task 7.
{ M_WANDER, "path", 4, {
  { P_VARIATION_A, {{0.f,0.f},{.05f,.15f},{.25f,.45f},{.5f,.7f},{.75f,1.f}} },
  { P_VARIATION_B, {{0.f,0.f},{.05f,.15f},{.25f,.45f},{.5f,.7f},{.75f,1.f}} },
  { P_FORM_A,    {{0.f,0.f},{0.f,0.f},{0.f,1.f},{1.f,2.f},{2.f,3.f}} },
  { P_SONG_A,    {{0.f,0.f},{0.f,0.f},{0.f,1.f},{1.f,2.f},{2.f,3.f}} } } },
// SPACE "intimate -> room -> hall -> dissolve" (§3 row 6). SIZE/DECAY get
// the lazy follower in the runtime (kSpaceSlewS); dry duck at Q4 comes from
// REVMIX riding high (equal-power: wet up = dry down).
{ M_SPACE, "bloom", 4, {
  { P_REVMIX_A,  {{.02f,.1f},{.15f,.3f},{.35f,.5f},{.5f,.7f},{.75f,.95f}} },
  { P_REVMIX_B,  {{.02f,.1f},{.15f,.3f},{.35f,.5f},{.5f,.7f},{.75f,.95f}} },
  { P_REV_SIZE,  {{.2f,.35f},{.35f,.5f},{.5f,.65f},{.65f,.8f},{.8f,.95f}} },
  { P_REV_DECAY, {{.3f,.4f},{.4f,.5f},{.5f,.65f},{.65f,.8f},{.8f,.92f}} } } },
};
inline const int kStoryCount = int(sizeof(kStories)/sizeof(kStories[0]));
```

`kBaseRules` covers every `ParamId` **not** in any story (engines, scale, root, tune, rate, shape, smooth, range, depth, attack, decay, res, sub, flux, link, morph, couple, tide, choke, shuffle, rev size/diff, tempo, steps-B, form-B, song-B, variation-…): one row each with four archetype spans. Drone archetype pulls DENSITY/STEPS/RATE spans low and ATTACK/DECAY long; pulse raises DENSITY mid and shortens envelopes; arp raises RATE/STEPS; fragment widens VARIATION and shortens DECAY. Write all rows (mechanical; ~35 lines), each with a one-word comment naming its intent. `kArchWeight = {0.5f, 0.2f, 0.15f, 0.15f}`.

Wait — a param must not appear in `kBaseRules` *and* a story of the same macro set; overlap across macros is legal (spec: shared targets). Duplication rule for the generator: story targets take their **base** from the story's own bp values at the resting macro position, so `kBaseRules` simply must not contain any storied param. The test above checks ranges; add one more check in Step 1's test file (same test case): no `kBaseRules` param appears in any story:

```cpp
    for (int i = 0; i < kBaseRuleCount; ++i)
        for (int s = 0; s < kStoryCount; ++s)
            for (int t = 0; t < kStories[s].n_targets; ++t)
                CHECK(kBaseRules[i].param != kStories[s].targets[t].param);
```

- [ ] **Step 4: Run test to verify it passes**

`cmake --build build && ./build/spky_tests.exe -tc="flow taste*"` → PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/flow/taste.h tests/test_flow_taste.cpp CMakeLists.txt
git commit -m "feat(flow): taste tables - the whole listening loop edits one file"
```

---

### Task 4: Terrain generator (`terrain.h/.cpp`) — stages 0–4

**Files:**
- Create: `engine/flow/terrain.h`, `engine/flow/terrain.cpp`
- Test: `tests/test_flow_terrain.cpp`
- Modify: `CMakeLists.txt` (`terrain.cpp` into **both** `spky_tests` and `render` source lists)

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces:

```cpp
namespace spky::flow {
struct TerrainState {
    uint32_t master = 1;
    uint16_t reroll[MACRO_COUNT] = {};   // override counters per macro domain
};
struct Curve { int param; float bp[5]; };          // drawn story curve
struct MacroMap { int story; int n_targets; Curve targets[6]; };
struct Terrain {
    Archetype arch;
    float     base[P_COUNT];              // engine units; storied params too
    bool      storied[P_COUNT];           // owned by some macro's curve
    MacroMap  map[MACRO_COUNT];
    int       weather_n;                  // 2..4
    float     weather_period_s[4], weather_depth[4];
    Macro     weather_target[4];
};
Terrain generate(const TerrainState& st);
}
```

Generation (in `terrain.cpp`): stage 0 archetype from `kStreamArch`; stage 1 roles/engines from `kStreamRoles` (carrier ∈ {SYNTH, WAVE, BODY}, texture ∈ all five, weighted; never Sampler+BBD together — the "loud pair" rule); stage 2 scale/root from `kStreamTonality`; stage 3 every `kBaseRules` row from its **own** param stream `make_stream(master, kStreamParamBase + param, reroll[owning_macro_or_0])` — params not owned by a macro use counter 0; stage 4 per macro: story pick + per-target curve draw from `make_stream(master, kStreamMacroBase + m, reroll[m])`, each target's 5 breakpoints drawn inside its `CurveRule` spans then **sorted monotone** in the story's direction; storied params' `base[p]` = their bp[0] draw (calm floor). Constraints enforced post-draw: BODY deck → `base[P_FILT_*]` and every BRIGHT curve bp floor for that deck clamped `>= kBodyFiltFloor`; `min(P_DENSITY_A base, P_DENSITY_B base)` clamped `<= 0.5f` (no-double-density); RES cap already structural (param range).

- [ ] **Step 1: Write the failing tests**

```cpp
// tests/test_flow_terrain.cpp
#include "doctest/doctest.h"
#include "flow/terrain.h"
#include "flow/taste.h"
using namespace spky::flow;

static TerrainState st(uint32_t m) { TerrainState s; s.master = m; return s; }

TEST_CASE("flow terrain: 10k seeds stay inside taste limits (spec 7.1)") {
    for (uint32_t k = 1; k <= 10000; ++k) {
        Terrain t = generate(st(k * 2654435761u));
        for (int p = 0; p < P_COUNT; ++p) {
            CAPTURE(k); CAPTURE(kParams[p].name);
            CHECK(t.base[p] >= kParams[p].lo);
            CHECK(t.base[p] <= kParams[p].hi);
        }
        for (int m = 0; m < MACRO_COUNT; ++m) {
            const auto& mm = t.map[m];
            CHECK(mm.n_targets >= 1);
            float span_max = 0.f;
            for (int i = 0; i < mm.n_targets; ++i) {
                const auto& c = mm.targets[i];
                bool up = c.bp[4] >= c.bp[0];
                for (int b = 0; b < 5; ++b) {
                    CHECK(c.bp[b] >= kParams[c.param].lo);
                    CHECK(c.bp[b] <= kParams[c.param].hi);
                    if (b) { if (up) CHECK(c.bp[b] >= c.bp[b-1]);
                             else    CHECK(c.bp[b] <= c.bp[b-1]); }
                }
                float norm = (kParams[c.param].hi - kParams[c.param].lo);
                float sp = (c.bp[4] > c.bp[0] ? c.bp[4]-c.bp[0]
                                              : c.bp[0]-c.bp[4]) / norm;
                if (sp > span_max) span_max = sp;
            }
            CHECK(span_max >= kMinSpan);       // no dead knob (spec 7.1)
        }
        // Named constraints (spec 7.1).
        if (int(t.base[P_ENGINE_A] + .5f) == ENGINE_BODY)
            CHECK(t.base[P_FILT_A] >= kBodyFiltFloor);
        if (int(t.base[P_ENGINE_B] + .5f) == ENGINE_BODY)
            CHECK(t.base[P_FILT_B] >= kBodyFiltFloor);
        bool both_hot = t.base[P_DENSITY_A] > 0.5f && t.base[P_DENSITY_B] > 0.5f;
        CHECK(!both_hot);
        CHECK(t.weather_n >= kWeatherOscMin);
        CHECK(t.weather_n <= kWeatherOscMax);
    }
}

TEST_CASE("flow terrain: determinism - same state twice, identical terrain") {
    Terrain a = generate(st(0xC0FFEE)), b = generate(st(0xC0FFEE));
    CHECK(a.arch == b.arch);
    for (int p = 0; p < P_COUNT; ++p) CHECK(a.base[p] == b.base[p]);
    for (int m = 0; m < MACRO_COUNT; ++m) {
        CHECK(a.map[m].story == b.map[m].story);
        for (int i = 0; i < a.map[m].n_targets; ++i)
            for (int b5 = 0; b5 < 5; ++b5)
                CHECK(a.map[m].targets[i].bp[b5] == b.map[m].targets[i].bp[b5]);
    }
}

TEST_CASE("flow terrain: archetypes reach the data (spec 7.7, fixed seeds)") {
    // Generous-margin statistical assertion over a FIXED seed set: mean
    // deck-A density of drone terrains sits clearly below pulse terrains'.
    double sum[ARCH_COUNT] = {}; int n[ARCH_COUNT] = {};
    for (uint32_t k = 1; k <= 4000; ++k) {
        Terrain t = generate(st(k * 40503u + 7u));
        sum[t.arch] += t.base[P_DENSITY_A]; n[t.arch]++;
    }
    REQUIRE(n[ARCH_DRONE] > 100); REQUIRE(n[ARCH_PULSE] > 100);
    CHECK(sum[ARCH_DRONE]/n[ARCH_DRONE] < sum[ARCH_PULSE]/n[ARCH_PULSE] - 0.05);
}
```

- [ ] **Step 2: Run to verify RED** — build error (`flow/terrain.h` missing).

- [ ] **Step 3: Implement `terrain.h` / `terrain.cpp`** per the interface and generation rules above. Core loop sketch (real structure, fill per the rules):

```cpp
// terrain.cpp (structure)
Terrain generate(const TerrainState& st) {
    Terrain t{};
    { Rng r = make_stream(st.master, kStreamArch, 0);
      t.arch = pick_weighted(r, kArchWeight, ARCH_COUNT); }
    { Rng r = make_stream(st.master, kStreamRoles, 0);
      // carrier/texture engines, loud-pair rule, write P_ENGINE_A/B into base
    }
    { Rng r = make_stream(st.master, kStreamTonality, 0);
      // P_SCALE, P_ROOT, and both decks' P_TUNE_* coupling
    }
    for (int i = 0; i < kBaseRuleCount; ++i) {
        const BaseRule& br = kBaseRules[i];
        Rng r = make_stream(st.master, kStreamParamBase + br.param, 0);
        const Span& s = br.per_arch[t.arch];
        t.base[br.param] = s.lo + r.next_unipolar() * (s.hi - s.lo);
    }
    for (int m = 0; m < MACRO_COUNT; ++m) {
        Rng r = make_stream(st.master, kStreamMacroBase + m, st.reroll[m]);
        // pick story variant among kStories with .macro == m, then draw each
        // target's 5 bps inside its CurveRule spans; sort monotone in the
        // story's direction; base[param] = bp[0]; storied[param] = true;
    }
    { Rng r = make_stream(st.master, kStreamWeather, st.reroll_weather_counter());
      // weather_n, periods (kWeatherPeriodMin..Max), depths, targets
    }
    apply_constraints(t);   // BODY FILT floor, no-double-density clamp
    return t;
}
```

(Weather rerolls with the whole terrain: derive its counter as the sum of all six macro counters — any partial reroll refreshes the weather too, which is §4's "a new sub-seed like any other stage" reading; document that choice in a comment.)

- [ ] **Step 4: Run to verify PASS** — `./build/spky_tests.exe -tc="flow terrain*"` and full ctest.

- [ ] **Step 5: Commit**

```bash
git add engine/flow/terrain.h engine/flow/terrain.cpp tests/test_flow_terrain.cpp CMakeLists.txt
git commit -m "feat(flow): the terrain generator - archetype down to story curves"
```

---

### Task 5: Terrain code, distance metric, NEW draw (`terrain_code.h`, distance in `terrain.cpp`)

**Files:**
- Create: `engine/flow/terrain_code.h`
- Modify: `engine/flow/terrain.h`, `engine/flow/terrain.cpp`
- Test: `tests/test_flow_terrain_code.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `int encode_code(const TerrainState&, char* out, int cap)` (writes e.g. `F1-DEADBEEF-000100020000`, returns length), `bool decode_code(const char* code, TerrainState& out)`, `float distance(const Terrain&, const Terrain&)` (mean |Δ normalized base| over `P_COUNT` + `0.25f` if archetypes differ), `TerrainState draw_new(const TerrainState& cur, uint32_t& next_stream_state)` (draw-and-retry masters until `distance >= kDistanceMin`, max 16 tries then take the farthest seen; `next_stream_state` is the caller-held sequence RNG state so the draw chain is deterministic).

- [ ] **Step 1: Failing tests**

```cpp
// tests/test_flow_terrain_code.cpp
#include "doctest/doctest.h"
#include "flow/terrain_code.h"
#include "flow/terrain.h"
using namespace spky::flow;

TEST_CASE("flow code: roundtrip") {
    TerrainState a; a.master = 0xDEADBEEF;
    a.reroll[M_BRIGHT] = 3; a.reroll[M_SPACE] = 1;
    char buf[48]; REQUIRE(encode_code(a, buf, sizeof buf) > 0);
    TerrainState b; REQUIRE(decode_code(buf, b));
    CHECK(b.master == a.master);
    for (int m = 0; m < MACRO_COUNT; ++m) CHECK(b.reroll[m] == a.reroll[m]);
    TerrainState c; CHECK(!decode_code("garbage", c));
}

TEST_CASE("flow distance: NEW lands elsewhere, deterministically (spec 7.4)") {
    TerrainState cur; cur.master = 0xC0FFEE;
    Terrain here = generate(cur);
    uint32_t seq1 = 42u, seq2 = 42u;
    TerrainState n1 = draw_new(cur, seq1), n2 = draw_new(cur, seq2);
    CHECK(n1.master == n2.master);                       // deterministic chain
    CHECK(distance(here, generate(n1)) >= kDistanceMin); // clears threshold
    for (int m = 0; m < MACRO_COUNT; ++m) CHECK(n1.reroll[m] == 0);
}
```

- [ ] **Step 2: RED** (build error). 
- [ ] **Step 3: Implement.** Encoding: `"F1-%08X-"` then six `%02X` counters (counters cap at 255 per spec-silent detail — wrap at 255 is fine, document). Distance per the interface note. `draw_new`: run an `Rng` seeded from `next_stream_state`, per try `master = r.next_u32()`, generate + measure, accept on threshold; write back `next_stream_state = r` internal state via `next_u32()` round-trip (expose `Rng::state()` is not available — instead pass `Rng&` directly; adjust signature to `TerrainState draw_new(const TerrainState& cur, Rng& seq)` and update the test accordingly — the test then constructs `Rng seq; seq.seed(42);` twice).
- [ ] **Step 4: PASS + full ctest.**
- [ ] **Step 5: Commit** — `feat(flow): terrain codes and the distance rule - NEW lands elsewhere`

---

### Task 6: Flow runtime core (`flow.h/.cpp`) — story-curve eval, CV, hysteresis, weather, tick

**Files:**
- Create: `engine/flow/flow.h`, `engine/flow/flow.cpp`
- Test: `tests/test_flow_runtime.cpp`
- Modify: `CMakeLists.txt` (both targets)

**Interfaces:**
- Consumes: everything above; `Instrument` (a pointer, engine-only init is fine for tests).
- Produces (Plan B consumes exactly this):

```cpp
namespace spky::flow {
class Flow {
public:
    void init(Instrument* inst, float ctrl_hz);       // ctrl_hz = tick rate
    void wake(const TerrainState& s);                 // instant, no blend
    void set_macro(int m, float v);                   // knob, 0..1
    void set_cv(int m, float v);                      // additive, any range
    void tick();                                      // one control tick
    void new_full();                                  // blended (Task 7)
    void new_partial(uint8_t macro_mask);             // blended (Task 7)
    void undo();                                      // blended (Task 7)
    void set_lock(bool on);   bool locked() const;
    const TerrainState& state() const;
    float blend_phase() const;                        // 1.f when settled
    float eff_macro(int m) const;                     // clamp(knob+cv+weather)
    float param_now(int p) const;                     // last pushed value
    double now_s() const;                             // flow-internal clock
};
}
```

Runtime rules implemented here (all spec §3/§4): per tick — `eff = clamp01(knob + cv + weather(m, t - terrain_t0))` where weather offset sums that macro's oscillators (`fast_sin` on period phases, depth scaled by **pre-weather** MOTION sum `clamp01(knob[MOTION]+cv[MOTION])`); per storied target `v = piecewise(bp, eff)`; discrete targets pass through `hysteresis(param, v)` (switch only when `v` crosses the current step's boundary by more than `kHysteresisFrac` of a step); SPACE's `P_REV_SIZE`/`P_REV_DECAY` targets go through a one-pole lazy follower with `kSpaceSlewS`; non-storied params push their `base` once on wake/blend only. Push through `apply_param` only when the value actually changed (setter spam guard).

- [ ] **Step 1: Failing tests**

```cpp
// tests/test_flow_runtime.cpp
#include "doctest/doctest.h"
#include "flow/flow.h"
using namespace spky;
using namespace spky::flow;

static Flow make(Instrument& in, uint32_t seed) {
    Flow f; f.init(&in, 100.f);
    TerrainState s; s.master = seed; f.wake(s);
    return f;
}

TEST_CASE("flow runtime: macro sweep is monotone per target (spec 7.2)") {
    Instrument in; in.init(48000.f);
    Flow f = make(in, 0xA11CE);
    for (int m = 0; m < MACRO_COUNT; ++m) {
        // capture each storied target at 33 knob positions, check monotone
        const auto& mm = terrain_of(f).map[m];    // add test accessor
        for (int t = 0; t < mm.n_targets; ++t) {
            float prev = 0.f; bool first = true;
            bool up = mm.targets[t].bp[4] >= mm.targets[t].bp[0];
            for (int k = 0; k <= 32; ++k) {
                f.set_macro(m, k / 32.f); f.tick();
                float v = f.param_now(mm.targets[t].param);
                if (!first) { if (up) CHECK(v >= prev - 1e-6f);
                              else    CHECK(v <= prev + 1e-6f); }
                prev = v; first = false;
            }
        }
    }
}

TEST_CASE("flow runtime: weather is bounded, deterministic, becalmed (7.5)") {
    Instrument in; in.init(48000.f);
    Flow f = make(in, 0xB0B0);
    f.set_macro(M_MOTION, 1.f);
    float mn = 1.f, mx = -1.f;
    for (int i = 0; i < 100000; ++i) {           // 1000 s at 100 Hz
        f.tick();
        float w = f.eff_macro(M_DENSITY) - 0.f;  // knob 0, cv 0 -> pure weather
        mn = std::min(mn, w); mx = std::max(mx, w);
    }
    CHECK(mx <= kWeatherDepthMax + 1e-4f);
    CHECK(mn >= -1e-6f);                          // clamped at 0
    // Two runs agree (pure function of sub-seed and time).
    Instrument in2; in2.init(48000.f);
    Flow g = make(in2, 0xB0B0);
    g.set_macro(M_MOTION, 1.f);
    for (int i = 0; i < 500; ++i) { g.tick(); }
    Instrument in3; in3.init(48000.f);
    Flow h = make(in3, 0xB0B0);
    h.set_macro(M_MOTION, 1.f);
    for (int i = 0; i < 500; ++i) { h.tick(); }
    CHECK(g.eff_macro(M_SPACE) == h.eff_macro(M_SPACE));
    // MOTION at 0 becalms: weather contribution is 0.
    Instrument in4; in4.init(48000.f);
    Flow z = make(in4, 0xB0B0);
    z.set_macro(M_MOTION, 0.f);
    for (int i = 0; i < 5000; ++i) z.tick();
    CHECK(z.eff_macro(M_DENSITY) == doctest::Approx(0.f));
}

TEST_CASE("flow runtime: discrete targets switch once per crossing (7.6)") {
    Instrument in; in.init(48000.f);
    Flow f = make(in, 0x5EED);
    // Sweep WANDER slowly up and down around a FORM threshold; count changes.
    int changes = 0; float last = f.param_now(P_FORM_A);
    for (int i = 0; i <= 4000; ++i) {
        float k = 0.70f + 0.02f * std::sin(i * 0.01f);   // hovers at a seam
        f.set_macro(M_WANDER, k); f.tick();
        float v = f.param_now(P_FORM_A);
        if (v != last) { changes++; last = v; }
    }
    CHECK(changes <= 2);   // with hysteresis; without it this is dozens
}
```

(Add a `#ifdef SPKY_TESTING`-guarded `const Terrain& terrain_of(const Flow&)` accessor — the pattern `song_position_for_test` already sets.)

- [ ] **Step 2: RED** — build error. When the runtime exists but hysteresis is still a straight pass-through, test 7.6 must fail with dozens of changes; capture that run in the commit message (this is the load-bearing RED of the task).
- [ ] **Step 3: Implement `flow.cpp`** per the runtime rules above.
- [ ] **Step 4: PASS + full ctest.**
- [ ] **Step 5: Commit** — `feat(flow): the runtime - story curves, weather, hysteresis, one tick`

---

### Task 7: NEW ops — blend, staggered discrete switch, partial reroll, undo, lock

**Files:**
- Modify: `engine/flow/flow.h`, `engine/flow/flow.cpp`
- Test: `tests/test_flow_new.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:** fills in `new_full/new_partial/undo/set_lock` declared in Task 6.

Behavior (spec §5): `new_full()` → `draw_new` a target state, keep the old as undo slot, start a `kBlendS` ramp; per tick every continuous param moves `current -> target` on the ramp (retarget-from-interpolated on re-press = just restart the ramp from `param_now`); discrete params switch once, texture deck at blend start, carrier at 25 % of the blend, each "under a duck" = that deck's `P_REVMIX_*` is pushed toward wet for 0.5 s around the switch (the equal-power dry leg is the duck mechanism, same as BRIGHT ember). `new_partial(mask)` → bump `reroll[m]` for each masked macro, regenerate, blend identically; params outside the masked domains have identical targets so they do not move. `undo()` → blend to the stored previous state (single slot). `set_lock(true)` → `new_full/new_partial/undo` become no-ops. Weather crossfades old→new with the same ramp (evaluate both weathers during the blend, lerp the offsets).

- [ ] **Step 1: Failing tests**

```cpp
// tests/test_flow_new.cpp
#include "doctest/doctest.h"
#include "flow/flow.h"
using namespace spky;
using namespace spky::flow;

TEST_CASE("flow NEW: partial reroll touches only its domain (spec 7.3)") {
    Instrument in; in.init(48000.f);
    Flow f; f.init(&in, 100.f);
    TerrainState s; s.master = 0xFEED; f.wake(s);
    Terrain before = terrain_of(f);
    f.new_partial(1u << M_BRIGHT);
    // run the blend to completion
    for (int i = 0; i < 1000; ++i) f.tick();
    Terrain after = terrain_of(f);
    // BRIGHT's own targets may move; every parameter outside the BRIGHT
    // domain is identical (proves per-stream isolation, spec 7.3).
    bool in_domain[P_COUNT] = {};
    for (int t = 0; t < after.map[M_BRIGHT].n_targets; ++t)
        in_domain[after.map[M_BRIGHT].targets[t].param] = true;
    for (int t = 0; t < before.map[M_BRIGHT].n_targets; ++t)
        in_domain[before.map[M_BRIGHT].targets[t].param] = true;
    for (int p = 0; p < P_COUNT; ++p)
        if (!in_domain[p]) { CAPTURE(kParams[p].name);
                             CHECK(after.base[p] == before.base[p]); }
    CHECK(f.state().reroll[M_BRIGHT] == 1);
    CHECK(f.state().reroll[M_MOTION] == 0);
}

TEST_CASE("flow NEW: undo returns, lock refuses") {
    Instrument in; in.init(48000.f);
    Flow f; f.init(&in, 100.f);
    TerrainState s; s.master = 0xFACE; f.wake(s);
    uint32_t m0 = f.state().master;
    f.new_full(); for (int i = 0; i < 1000; ++i) f.tick();
    CHECK(f.state().master != m0);
    f.undo();     for (int i = 0; i < 1000; ++i) f.tick();
    CHECK(f.state().master == m0);
    f.set_lock(true);
    f.new_full(); f.tick();
    CHECK(f.state().master == m0);               // refused
}

TEST_CASE("flow NEW: re-press retargets from the interpolated state") {
    Instrument in; in.init(48000.f);
    Flow f; f.init(&in, 100.f);
    TerrainState s; s.master = 0xD1CE; f.wake(s);
    f.new_full();
    for (int i = 0; i < 50; ++i) f.tick();       // mid-blend
    float mid = f.param_now(P_REV_SIZE);
    f.new_full();                                 // retarget mid-flight
    f.tick();
    float step1 = f.param_now(P_REV_SIZE);
    CHECK(std::fabs(step1 - mid) < 0.05f);        // no jump at retarget
    CHECK(f.blend_phase() < 1.f);
}
```

- [ ] **Step 2: RED** — `new_full` etc. are Task-6 stubs (no-op bodies): the undo test fails on `master != m0`. That failing run is the RED.
- [ ] **Step 3: Implement** per behavior block above.
- [ ] **Step 4: PASS + full ctest.**
- [ ] **Step 5: Commit** — `feat(flow): NEW - blends, partial rerolls, one undo, a lock`

---

### Task 8: Gesture decoder (`gesture.h`)

**Files:**
- Create: `engine/flow/gesture.h` (header-only)
- Test: `tests/test_flow_gesture.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces (Plan B wires the VCV button/knobs to exactly this):

```cpp
namespace spky::flow {
struct GestureOut {
    enum Op { NONE, NEW_FULL, NEW_PARTIAL, UNDO, LOCK_TOGGLE, REFUSED } op = NONE;
    uint8_t mask = 0;                 // NEW_PARTIAL: macro bitmask
};
class Gesture {
public:
    // Feed events; poll() after each to collect an op (at most one).
    void button(bool down, double now_s, bool locked);
    void knob_delta(int macro, float delta, double now_s); // physical travel
    void tick(double now_s);          // for LED state only
    GestureOut poll();
    // LED signature for the host to render (spec §5 table + lock blink):
    enum Led { LED_IDLE, LED_BLEND, LED_MARKED, LED_UNDO_ARMED,
               LED_LOCKED, LED_REFUSE };
    Led led(float blend_phase, bool locked) const;
};
}
```

Rules (spec §5, all thresholds from `taste.h`): release < `kTapMaxS` with no mark → `NEW_FULL`; `knob_delta` accumulating ≥ `kMarkDelta` while held → mark that macro, **cancel** armed undo and the lock timer permanently for this hold; release with marks → `NEW_PARTIAL` with the mask; clean hold ≥ `kUndoArmS` → undo armed, release before `kLockS` → `UNDO`; clean hold ≥ `kLockS` → `LOCK_TOGGLE` fires immediately (not on release), the rest of the hold is inert; any press while locked (except the 5 s unlock hold) → `REFUSED` on release.

- [ ] **Step 1: Failing tests**

```cpp
// tests/test_flow_gesture.cpp
#include "doctest/doctest.h"
#include "flow/gesture.h"
#include "flow/taste.h"
using namespace spky::flow;

static GestureOut run_press(Gesture& g, double t0, double dt,
                            int mark_macro = -1, double mark_at = 0.0,
                            bool locked = false) {
    g.button(true, t0, locked);
    if (mark_macro >= 0) g.knob_delta(mark_macro, 0.05f, t0 + mark_at);
    g.tick(t0 + dt);
    g.button(false, t0 + dt, locked);
    return g.poll();
}

TEST_CASE("flow gesture: the §5 table") {
    Gesture g;
    CHECK(run_press(g, 0.0, 0.2).op == GestureOut::NEW_FULL);          // tap
    auto pr = run_press(g, 10.0, 2.0, M_BRIGHT, 0.5);                   // mark
    CHECK(pr.op == GestureOut::NEW_PARTIAL);
    CHECK(pr.mask == (1u << M_BRIGHT));
    CHECK(run_press(g, 20.0, 2.0).op == GestureOut::UNDO);             // hold
    // Mark AFTER undo armed: mark wins, undo cancelled.
    auto late = run_press(g, 30.0, 3.0, M_SPACE, 2.5);
    CHECK(late.op == GestureOut::NEW_PARTIAL);
    // Clean 5 s hold: LOCK fires during the hold, release adds nothing.
    g.button(true, 40.0, false);
    g.tick(40.0 + kLockS + 0.1);
    CHECK(g.poll().op == GestureOut::LOCK_TOGGLE);
    g.button(false, 46.0, true);
    CHECK(g.poll().op == GestureOut::NONE);
    // While locked: tap refuses.
    CHECK(run_press(g, 50.0, 0.2, -1, 0.0, true).op == GestureOut::REFUSED);
    // Marked hold past 5 s: NO lock (knob turned) - partial on release.
    auto held = run_press(g, 60.0, 6.0, M_DIRT, 0.5);
    CHECK(held.op == GestureOut::NEW_PARTIAL);
}
```

- [ ] **Step 2: RED** (missing header). 
- [ ] **Step 3: Implement** the small state machine (held?, press_t, marked_mask, undo_armed, lock_fired). `tick` only advances lock firing; everything else is edge-driven.
- [ ] **Step 4: PASS + full ctest.**
- [ ] **Step 5: Commit** — `feat(flow): the one-button gesture family, corners closed`

---

### Task 9: Render-host integration — flow scenarios

**Files:**
- Modify: `host/render/scenario.h` (add `bool has_flow; std::string flow_code;` to `Scenario`), `host/render/scenario.cpp`, `host/render/main.cpp`
- Create: `host/render/scenarios/flow_smoke.json`, `host/render/scenarios/flow_calm_corner.json`, `host/render/scenarios/flow_new_ride.json`
- Test: extend `tests/test_scenario.cpp`
- Modify: `CMakeLists.txt` (add `engine/flow/terrain.cpp`, `engine/flow/flow.cpp` to the `render` target — they were added to `spky_tests` in Tasks 4/6)

**Interfaces:**
- Consumes: `Flow` API (Task 6/7), `decode_code` (Task 5), existing `apply_event` pattern.
- Produces scenario actions (`svalue`/`slot`/`value` per the existing Event fields):
  - `{"action":"flow_wake",  "value":"F1-..."}` — decode + `wake` (init section)
  - `{"action":"flow_macro", "slot":<0..5>, "value":<0..1>}`
  - `{"action":"flow_cv",    "slot":<0..5>, "value":<float>}`
  - `{"action":"flow_new"}`, `{"action":"flow_new_partial","ivalue":<mask>}`, `{"action":"flow_undo"}`, `{"action":"flow_lock","flag":true|false}`

Wiring in `main.cpp`: when a scenario contains any `flow_*` action (or `flow_wake` in init), construct a `Flow` bound to the `Instrument`, and call `flow.tick()` once per control block (the existing block loop; ctrl rate = block rate). Dispatch: extend `apply_event` with a second overload `apply_event(Instrument&, Flow*, const Event&)`; flow actions no-op with a stderr warning when `Flow*` is null. Scenario content: `flow_smoke.json` = 30 s, `flow_wake` a fixed code, one macro ride, one `flow_new` at 12 s. `flow_calm_corner.json` = 20 s, wake + all six `flow_macro` 0. `flow_new_ride.json` = 40 s, wake + `flow_new` every 8 s (audition material for the listening loop).

- [ ] **Step 1: Failing test** — in `tests/test_scenario.cpp` add:

```cpp
TEST_CASE("scenario: flow actions parse and drive the flow layer") {
    // write a temp scenario json with flow_wake + flow_macro, load it,
    // apply through the new overload, check Flow state took the code:
    // decode_code(the same code) == flow.state() and eff_macro moved.
}
```

(Write it out fully in the task — construct `Scenario` via `load_scenario` on a string written to a temp file with `std::ofstream`, following the existing patterns in `tests/test_scenario.cpp`.)

- [ ] **Step 2: RED** — unknown actions are ignored by design, so assert on *state effects*; with no implementation the Flow state check fails. 
- [ ] **Step 3: Implement** dispatch + main-loop wiring + the three JSONs.
- [ ] **Step 4: PASS**, then render one manually: `./build/render.exe host/render/scenarios/flow_smoke.json /tmp/flow_smoke.wav /tmp/flow_smoke.csv` and confirm it produces a WAV without stderr warnings.
- [ ] **Step 5: Commit** — `feat(render): scenarios learn to ride the flow layer`

---

### Task 10: Audio gates in-process (spec §7.8) — NaN, RMS bounds, level jump, calm corner

**Files:**
- Test: `tests/test_flow_audio.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:** consumes `Flow` + `Instrument` with a real `FxMem` (follow the allocation pattern used in `tests/test_instrument.cpp` — static echo/reverb/sampler buffers; sampler buffers may be null for these tests, decks that roll Sampler then run silent, which the RMS floor check must tolerate: use seeds whose terrains avoid Sampler, pick them by generating and filtering at test-start).

- [ ] **Step 1: Failing test**

```cpp
// tests/test_flow_audio.cpp
#include "doctest/doctest.h"
#include "flow/flow.h"
#include <cmath>
using namespace spky;
using namespace spky::flow;

// Render n seconds through Instrument::process in 64-sample blocks,
// ticking the flow once per block; returns overall RMS and max |sample|,
// asserts no NaN. Helper shared by the cases below (write it here).

TEST_CASE("flow audio: fixed seeds render clean and inside RMS bounds (7.8)") {
    for (uint32_t master : {0x101u, 0x202u, 0x303u, 0x404u}) {
        // skip masters that rolled a Sampler deck (no buffer in this rig)
        // ... generate(state), check engines, skip if sampler ...
        // 10 s at all-macros 0.5: no NaN; 0.001 < rms < 0.5
    }
}

TEST_CASE("flow audio: calm corner sits under the ceiling (7.8)") {
    // same seeds, all macros 0, 10 s (skip first 3 s for tails):
    // rms <= kCalmCornerRmsMax
}

TEST_CASE("flow audio: a NEW blend never jumps the level (7.8)") {
    // seed 0x101, macros 0.5, render 6 s, fire new_full, render the 6 s
    // blend in 250 ms windows; consecutive window RMS ratio stays within
    // +/- 6 dB (ratio < 2.0 and > 0.5, guarding both spike and dropout).
}
```

Write the three cases out completely (the helper makes each ~10 lines).

- [ ] **Step 2: RED proof.** The calm-corner case is the one that must be *demonstrated* red: temporarily set `kCalmCornerRmsMax = 0.0f` locally (do not commit), watch it fail, restore. The NaN/jump cases prove themselves against real behavior — if they never fail, tighten the window once to confirm they can (convention: a test that cannot go red gets fixed).
- [ ] **Step 3/4:** Implement the helper, tune the four fixed masters to seeds that avoid Sampler decks, run `./build/spky_tests.exe -tc="flow audio*"` → PASS; then full `ctest`.
- [ ] **Step 5: Commit** — `test(flow): the audio gates - calm corner, clean blends, no NaN`

---

### Task 11: Docs + roadmap close-out

**Files:**
- Modify: `docs/roadmap.md` (Plan A line under M6: flow layer built, listening phase open; Plan B next)
- Create: `docs/superpowers/specs/2026-08-05-flow-listening-notes.md` (empty template: date / seed code / verdict / taste.h change — the listening loop's logbook)
- Modify: `CLAUDE.md` (one line in "Where things are": `engine/flow/` — terrain+macro layer, tuning data in `engine/flow/taste.h`)

- [ ] **Step 1:** Make the three edits (roadmap wording follows the file's existing voice — read it first).
- [ ] **Step 2:** `ctest --test-dir build --output-on-failure` — full suite green.
- [ ] **Step 3: Commit** — `docs(flow): plan A lands - the listening loop has a logbook`

---

## Self-Review (done at write time)

- **Spec coverage:** §2 layer+split (T1–T8 / plan B separate), owned PRNG (T2 reuses `spky::Rng`, documented), §3 macros+stories+calm corner (T3/T6), CV clamp (T6), discrete hysteresis (T6), §4 identity/streams (T2/T5), stages 0–5 (T4), distance (T5), generation not in audio callback (render host ticks at block rate — T9; Daisy is Plan C territory), §5 house seed/persistence (wake() is the primitive; VCV persistence is Plan B), gestures (T8), §7.1–7.8 (T4, T6, T7, T5, T6, T4, T10). **Gap accepted:** §5's staggered-deck duck is implemented (T7) but its *audibility* is listening-phase, not unit-testable.
- **Type consistency:** `TerrainState.reroll` is `uint16_t[6]` everywhere; `draw_new(cur, Rng&)` (T5 corrected signature) is what T7's `new_full` calls; `terrain_of(const Flow&)` test accessor introduced in T6, used in T6/T7.
- **Placeholder scan:** T9 step 1 asks the implementer to write the test *fully* against named existing patterns; taste.h base rules are enumerated by rule, not per-line — both are deliberate: the authority (existing test file / param table) is named precisely.
