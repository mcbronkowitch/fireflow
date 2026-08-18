# SWARM Additive Drone Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A sixth part engine, `ENGINE_SWARM`, that plays notes like SYNTH/WAVE/BODY
but whose sound moves from within — one fixed bank of N sine partials per deck,
retuned by the chord layer, bloomed by one `Env`, and kept alive by a free-running
per-partial drift below the lane timescale.

**Architecture:** Two layers behind one `IPartEngine`. `SwarmBank` (header-only,
`engine/swarm/swarm_bank.h`) owns the hot loop: N partials, each a phase
accumulator plus two per-channel amplitude accumulators, all advanced by
per-sample **slopes** so the inner loop is a branch-free multiply-add chain with
one `fast_sin` per partial. `SwarmEngine` (`engine/swarm/swarm_engine.{h,cpp}`)
owns everything at control rate: the allocation of partials onto the chord tones'
overtone series, the HARM warp, the tilt/aperture/even-odd weighting, the drift
walks, and one `Env` used unmodified as the bloom. Targets are recomputed a
**slice at a time** (round robin), and each retarget closes a fixed fraction of
the remaining distance, so glides fall out of the slope arithmetic with no
counter and no branch.

**Tech Stack:** C++17, clang + Ninja, doctest. ARM GCC + `bench/run.py` for the
two hardware CPU measurements. No new dependencies, no new memory arena, no
tables.

**Spec:** `docs/superpowers/specs/2026-08-17-swarm-additive-drone-engine-design.md`

---

## Global Constraints

- **Build with `-DCMAKE_BUILD_TYPE=Release`.** A fresh configure defaults to
  Debug and the render-hash gates then fail with "SYNTH reference moved" for a
  reason that has nothing to do with this work. `source env.sh` first (clang +
  Ninja, never MSVC), and **never** source `env.sh` in a shell used for `bench/`
  or `shell/` — those are ARM GCC and the two toolchains must not mix.
- **Everything written into the repo is English** — code, comments, commit
  messages, docs, scenario `_comment` fields.
- Commit trailer is
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **Shell rules, all four, they are not optional** (memory
  `subagent-shell-auto-approval`): (1) never modify a file through the shell —
  no `sed -i`, `>`, `>>`, `tee`, `awk -i inplace`, `perl -pi`; use the Edit tool
  for existing files and Write for new ones. (2) Never prefix a command with
  `cd`, not even a read-only one — the tool already starts in the repo root.
  (3) Never chain a write (`rm`, `mv`, `git add`, `git commit`) behind `&&` or
  `;`. (4) Repo-relative paths in write calls. A long compound that genuinely
  needs a working directory goes into a script file in the scratchpad.
- **N — the partial count — is a MEASURED number and this plan does not contain
  it.** `swarm_cfg::kPartials` carries a placeholder from Task 1 until Task 3's
  bench on the Patch Submodule prints cycles per partial. **No test, no
  comment, no commit message may quote the placeholder as if it were a
  decision**, and no gate may depend on its value: every test loops to
  `swarm_cfg::kPartials`, never to a literal. `swarm_cfg::kNDecided` is the flag
  that says the bench has spoken, and Task 3 exists to flip it.
- **No Seed figure may be quoted for a submodule claim.** The two CPU
  measurements are `--board patch_sm`. The only submodule numbers that may be
  cited as prior art are the ones in
  `docs/bench/2026-08-07-d0b3c08-regress-axi-o3-patch_sm-usb.csv`
  (`instrument_worst` 102.27 % avg / 108.62 % max at `axi`/`o3`, repeat 1).
- **Bench rows are compared inside ONE image, never across commits or
  profiles.** Adding a translation unit shifts small rows by ~7 % from icache
  layout alone (memory `fireflow-gotchas`), and `SerialArena` overlays its
  groups so an unchanged memory table proves nothing. Verify a new row landed by
  grepping `bench/build/bench.map` for its mangled setup/proc symbols, and
  `touch` the workload sources after any checkout or stash pop (memory
  `fireflow-bench-stale-object-trap`).
- **No render hash gates for SWARM.** Renders are sanity checks, not checksums.
  The two hashes that already exist (`ctrl_identity`, `wave_formant_sweep`) must
  stay **unchanged** through every task in this plan: both run SYNTH/WAVE decks
  that SWARM does not touch. If one moves, that is a finding to report, never a
  baseline to bump.
- **By-ear constants ship flagged, not final.** Every value in
  `engine/swarm/swarm_config.h` marked `BY EAR, first try` is Bastian's to
  confirm in Task 12. No gate may assert one of those literals — derive the
  expectation from the named constant, the way `test_step_accent.cpp` derives
  from `kAccentVelFloor` (memory `fireflow-vacuous-test-gates`, shape 3: the
  threshold must not live in the file it polices, and a gate that recomputes its
  subject from the subject is not a gate).
- **A test that cannot go red gets fixed, even if this plan mandated it.** For
  every gate: before accepting a RED or a GREEN, satisfy yourself the assertion
  depends on the line you changed. If it does not, strengthen the test and say so
  in the task report. Task 11 red-proofs the whole set with one-line mutations.
- **Where this plan needs a runtime number it does not have, it says "measure it
  with a probe and record what it printed"** — it does not guess one. The probe
  recipe is `docs/engine-map.md` §6: `clang++ -O2 -Iengine -o probe.exe
  probe.cpp <sources>`, 0.4 s to compile, 0.1 s to run. Probes are scratch files
  and belong in the scratchpad, never in the repo.

**Build and test commands used throughout:**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/spky_tests -tc="swarm G*"
```

**Hardware tasks (3 and 10) need Bastian and the board.** They are flagged in
their headings. DFU flashing and the two button presses of the first USB run are
manual steps no agent can take. See "Ordering and the hardware fallback" below.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `engine/swarm/swarm_config.h` | Every SWARM constant in one place: N and its decided-flag, the retarget slice, the by-ear voicing values. Nothing else in `engine/swarm/` may hold a tuning literal. | 1 |
| `engine/swarm/swarm_bank.h` | Header-only. The N-partial hot loop and the slope arithmetic. No allocation logic, no libm beyond `fast_sin`. Header-only on purpose: `SwarmBank::process` must inline into `SwarmEngine::process` the way `Part::process` inlines into `Instrument::process`. | 2 |
| `engine/swarm/swarm_engine.h` | The `IPartEngine` implementation: state, overrides, observers. | 1 |
| `engine/swarm/swarm_engine.cpp` | Everything at control rate — allocation, the HARM/TILT/FOCUS/BAL map, drift, the bloom, `std::pow`. The only new `.cpp` in the tree, so the six build files are touched once. | 1 |
| `tests/test_swarm_bank.cpp` | The bank's own gates (frequency, glide, pan, exact silence). | 2 |
| `tests/test_swarm_engine.cpp` | Everything else — id census, contract, spectrum map, bloom, chord, drift, determinism. One file, shared helpers at the top. | 1, 3–8 |
| `bench/workloads_swarm.cpp` | The `swarm` family: kernel rows that price the bank at several N. | 3 |
| `host/render/scenarios/swarm_drone.json` | The listening render. Sanity only, no hash. | 9 |

Six build files carry `engine/swarm/swarm_engine.cpp`, and all six are edited in
Task 1: `CMakeLists.txt` (twice — `spky_tests` and `render`), `bench/Makefile`,
`bench/audition/Makefile`, `host/vcv/Makefile`, `shell/Makefile`. A missing one
does not fail at compile time; it fails at link time in a host nobody built that
day, which is why they go in together.

## Ordering and the hardware fallback

Tasks 1, 2, 4, 5, 6, 7, 8, 9 and 11 are pure desktop work and run in order.
Task 3 (the `swarm_bank` bench) and Task 10 (`inst_swarm_engine_worst`) need
Bastian at the machine.

**If Task 3's result is not available when the queue reaches it**, skip it and
continue with Tasks 4–9. Nothing in them depends on N's *value*: they depend only
on `kPartials` being a compile-time constant, which it already is. Then run Tasks
3 and 10 together in one hardware session before Task 11. This is the expected
case, not the exception — it costs one board session instead of two.

**What may NOT be deferred**: Task 3's `kNDecided` gate. Task 11 does not pass
while N is a placeholder, and the branch does not merge without Task 11.

**Plan B stays closed unless the bench opens it.** Recursive sine oscillators
(`s[n] = 2cos(ω)s[n−1] − s[n−2]`) appear in this plan exactly once, in Task 3
Step 6, as the contingency if the bench says even a small N is unaffordable. Do
not pre-emptively design for them. Everything else in spec §7 is settled and not
to be re-opened: `Env` as the bloom unmodified, `Rng` streams, `fast_sin`,
slopes-not-smoothers in the hot loop, one sine per partial with pan gains.

## Open points this plan carries rather than resolves

These are places where the spec did not decide and the plan had to. **All four
were put to Bastian on 2026-08-17 and accepted as written** (the spec carries
the rulings now, in its §3/§4/§5/§6). They stay listed because each is sited so
that changing it is a one-line edit — do not re-decide them silently.

1. **Nyquist.** Spec §3's allocation says nothing about an upper frequency
   limit, and it needs one: `pitch_to_hz(1.0)` is 880 Hz (`synth_engine.cpp`),
   so the 32nd overtone of the top note is 28 160 Hz — past Nyquist at 48 kHz.
   The plan mutes (amplitude target 0) any partial above
   `swarm_cfg::kMaxHzFrac * sample_rate` rather than re-allocating it, because
   "CPU is constant regardless of played density" (spec §2) depends on the loop
   length never changing. Gate G13.
2. **FOCUS's single number.** Spec §4 asks one value to be both aperture and
   position ("wide = whole swarm, narrow = a soft window the lane sweeps"). The
   plan reads the SIZE target's **distance from 0.5** as narrowness and its
   **side** as position: 0.5 = fully open, toward 0 = a narrow low formant,
   toward 1 = a narrow high one. This is the only reading in which one number
   does both jobs, and it puts "fully open" exactly on the lane's boot base
   (`Part::_base[LANE_SIZE]` is 0.5). Gate G15.
3. **When NEW's reseed takes effect.** Spec §5 says NEW redraws the base seed but
   not when. The plan reseeds **immediately**; the new cluster map arrives as a
   glide because every target change does, so there is nothing to defer it for.
   The alternative (pending until the next phrase wrap, as `ModLane::new_phrase`
   does) is one `if` away. Gate G29.
4. **What a SWARM deck reports to the meter.** `Part::voice_env`/`active_voices`
   return 0 for any engine they have no arm for, so without an arm the VCV LED
   and `Instrument`'s meter go dead on a SWARM deck. The plan reports the bloom
   envelope as `voice_env(0)` and `active_voices() == 1` while audible. The spec
   does not mention it. Gate G3b.

---

### Task 1: The engine id, the silent engine, and every tripwire it trips

**Files:**
- Modify: `engine/parts/engine_iface.h` — `ENGINE_SWARM = 6` before the sentinel
- Create: `engine/swarm/swarm_config.h`
- Create: `engine/swarm/swarm_engine.h`, `engine/swarm/swarm_engine.cpp`
- Modify: `engine/parts/part.h` — include, member, `_engine_for` case, voice-row
  forwards, `swarm()` accessor, `active_voices`/`voice_env` arms
- Modify: `engine/param_table.h` — the `P_ENGINE_A/B` range and its comment
- Modify: `CMakeLists.txt` (both targets), `bench/Makefile`,
  `bench/audition/Makefile`, `host/vcv/Makefile`, `shell/Makefile`
- Modify: `tests/test_deck_bus.cpp` — `static_assert`, engine list,
  `consumes_input` census
- Modify: `tests/test_bbd_engine.cpp:21-22` — the `ENGINE_COUNT` check
- Modify: `tests/test_part.cpp` — the id census
- Modify: `tests/test_param_table.cpp` — the discrete-clamp expectation
- Create: `tests/test_swarm_engine.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `spky::ENGINE_SWARM == 6`, `spky::ENGINE_COUNT == 7`
  - `namespace spky::swarm_cfg` with `kPartials`, `kNDecided`, `kRetargetSlice`,
    `kSubPartials`, `kMaxHzFrac`, and the by-ear block
  - `class spky::SwarmEngine : public IPartEngine` with
    `void set_seed(uint32_t)` (call BEFORE `init`), `void init(float) override`,
    `void set_targets(const float*, float) override`,
    `void trigger(float) override`, `void process(float&, float&) override`,
    and `static constexpr int kCtrlInterval = 96`
  - `SwarmEngine& Part::swarm()` / `const SwarmEngine& Part::swarm() const`

- [ ] **Step 1: Write the failing tests**

Create `tests/test_swarm_engine.cpp`. The helper block at the top is shared by
every later task in this plan — later tasks append cases, never a second helper
block.

```cpp
// SWARM -- the additive partial-swarm drone engine.
// Spec: docs/superpowers/specs/2026-08-17-swarm-additive-drone-engine-design.md
//
// N is swarm_cfg::kPartials and is a MEASURED number (spec section 8). Nothing
// in this file may assume its value: every loop runs to kPartials and every
// expectation is derived from the named constants in swarm_config.h, never from
// their literals.
#include <doctest/doctest.h>
#include "parts/part.h"
#include "swarm/swarm_engine.h"
#include "part_engine_contract.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace spky;

namespace {

// A SwarmEngine at a known state. set_seed() BEFORE init(), the SynthEngineT
// convention (synth_engine.h) -- init() consumes the seed to build the drift
// streams and the cluster map, so the reverse order measures a different
// object.
SwarmEngine fresh_swarm(uint32_t seed = 99u) {
    SwarmEngine e;
    e.set_seed(seed);
    e.init(48000.f);
    e.set_cycle(1.f);
    return e;
}

// The five lane targets, in the order Part pushes them: SOURCE, SIZE, PITCH,
// MOTION, LEVEL (engine/mod/lane_id.h).
void feed(SwarmEngine& e, float pitch, float tilt = 0.5f, float focus = 0.5f,
          float drift = 0.f, float level = 1.f) {
    const float t[LANE_COUNT] = { tilt, focus, pitch, drift, level };
    e.set_targets(t, 0.5f);
}

std::vector<float> render_l(SwarmEngine& e, int n) {
    std::vector<float> out(n);
    for (auto& s : out) { float l = 0.f, r = 0.f; e.process(l, r); s = l; }
    return out;
}

float peak_of(const std::vector<float>& b) {
    float p = 0.f;
    for (float v : b) p = std::max(p, std::fabs(v));
    return p;
}

// Line memory for the BBD decks the deck-bus sweep constructs. Declared here
// only because the sweep in test_deck_bus.cpp has its own; this file's Part
// cases do not need any.

}  // namespace

TEST_CASE("swarm G1: the engine id is appended, never renumbered") {
    // A saved patch stores the id, so moving one silently reassigns every deck
    // that used it (engine_iface.h). This case is the census; the
    // static_assert in test_deck_bus.cpp is the build-time half.
    CHECK(ENGINE_TEST_TONE == 0);
    CHECK(ENGINE_SYNTH == 1);
    CHECK(ENGINE_SAMPLER == 2);
    CHECK(ENGINE_WAVE == 3);
    CHECK(ENGINE_BODY == 4);
    CHECK(ENGINE_BBD == 5);
    CHECK(ENGINE_SWARM == 6);
    CHECK(ENGINE_COUNT == 7);
}

TEST_CASE("swarm G2: SwarmEngine satisfies the universal part-engine contract") {
    // Silence in stays bounded and finite forever; the process_in/
    // consumes_input pairing holds (SWARM overrides neither, so the static
    // assert reads both as IPartEngine's); every no-op setter is safe in any
    // order. tests/part_engine_contract.h owns the reasoning.
    check_part_engine_contract<SwarmEngine>([](SwarmEngine& e) {
        e.set_seed(7u);
        e.init(48000.f);
    });
}

TEST_CASE("swarm G3: a SWARM deck is a note deck, and the switch completes") {
    Part p;
    p.init(48000.f, 5u);
    float l = 0.f, r = 0.f;
    p.set_engine(ENGINE_SWARM);
    for (int i = 0; i < 500; ++i) p.process(l, r);   // 4 ms fade out + in
    REQUIRE(p.engine_id() == ENGINE_SWARM);
    // Part derives the note-deck flag as "not SAMPLER and not BBD"
    // (part.cpp _engine_swap), so SWARM gets the melodic phrase for free --
    // and that is exactly the kind of free behaviour that silently stops
    // being true when someone adds an engine to the exclusion list.
    CHECK(p.mod().pitch_lane_is_note_lane_for_test());
}

TEST_CASE("swarm G3b: a SWARM deck reports its bloom to the meter") {
    // Without an arm in Part::voice_env, max_voice_env() is 0 on a SWARM deck
    // and the VCV LED plus Instrument's meter go dead. Plan open point 4.
    Part p;
    p.init(48000.f, 5u);
    float l = 0.f, r = 0.f;
    p.set_engine(ENGINE_SWARM);
    for (int i = 0; i < 500; ++i) p.process(l, r);
    REQUIRE(p.engine_id() == ENGINE_SWARM);
    p.trigger_manual();
    float m = 0.f;
    for (int i = 0; i < 4800; ++i) { p.process(l, r); m = std::max(m, p.max_voice_env()); }
    CHECK(m > 0.1f);
}
```

`pitch_lane_is_note_lane_for_test()` does not exist yet — add it in Step 5.

Add to `CMakeLists.txt`, in the `spky_tests` list immediately after
`tests/test_bbd_engine.cpp`:

```cmake
    engine/swarm/swarm_engine.cpp
    tests/test_swarm_engine.cpp
```

- [ ] **Step 2: Run it and confirm the build fails**

```bash
source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Expected: `swarm/swarm_engine.h: No such file or directory`, and — if you comment
that include out to see it — `use of undeclared identifier 'ENGINE_SWARM'` plus
the `static_assert` in `tests/test_deck_bus.cpp:165` firing with its own message.
Note the `static_assert` firing in the task report: that tripwire working is
part of this task's deliverable, not an obstacle to it.

- [ ] **Step 3: Add the id and the config header**

`engine/parts/engine_iface.h`, immediately before `ENGINE_COUNT`:

```cpp
    // The additive partial swarm (spec 2026-08-17 swarm-additive-drone-engine).
    // A melodic engine like SYNTH/WAVE/BODY -- Part's note-deck flag is derived
    // as "not SAMPLER and not BBD" (part.cpp), so this id needs no entry there
    // -- but NOT a SynthEngineT<Voice>: it has no per-note voices at all, one
    // bank of N partials per deck instead.
    ENGINE_SWARM = 6,
```

Create `engine/swarm/swarm_config.h`:

```cpp
#pragma once

namespace spky {
namespace swarm_cfg {

// --- N, the one number here that is not a taste decision ------------------
//
// N is MEASURED. The swarm_bank rows in bench/workloads_swarm.cpp print cycles
// per partial on the Daisy Patch Submodule, and N follows from the
// 960 000-cycle block budget (spec section 8). The literal below is a
// PLACEHOLDER that exists so the desktop tasks can build, and it carries NO
// CPU claim of any kind: do not quote it, do not size anything against it by
// hand, and do not let a test depend on its value.
constexpr int kPartials = 32;

// False until the bench has run and kPartials above is its result. The gate
// "swarm G8" in tests/test_swarm_engine.cpp fails while this is false, so an
// undecided N cannot reach main.
constexpr bool kNDecided = false;

// --- structure ------------------------------------------------------------

// How many partials one control tick retargets (spec section 7, lever 3). The
// whole bank is covered in kRetargetPeriod ticks, which flattens the
// control-tick spike the budget gate measures. It is also the glide's time
// base: see kApproachFrac.
constexpr int kRetargetSlice = 8;
constexpr int kRetargetPeriod =
    (kPartials + kRetargetSlice - 1) / kRetargetSlice;

// Partials reserved for SUB -- spec section 4, "one or two dedicated partials
// an octave below the root". Two, so drift makes them beat against each other
// instead of standing still. They always run and always cost the same; at
// SUB 0 their amplitude target is 0.
constexpr int kSubPartials = 2;
static_assert(kPartials > kSubPartials + 4,
              "N must leave room for a swarm above the SUB pair");

// The ceiling a partial's frequency may reach, as a fraction of the sample
// rate. Above it the partial keeps running with amplitude 0 -- it is NOT
// re-allocated, because "CPU is constant regardless of played density"
// (spec section 2) depends on the loop length never changing.
//
// NOT FROM THE SPEC. Spec section 3's allocation names no upper limit and
// needs one: pitch_to_hz(1.0) is 880 Hz (synth_engine.cpp), so the 32nd
// overtone of the top note is 28 160 Hz, past Nyquist at 48 kHz. Plan open
// point 1.
constexpr float kMaxHzFrac = 0.45f;

// --- by ear, first try (spec section 10) ----------------------------------
// Every constant below this line is Bastian's to confirm in the listening
// session at the end of the plan. No gate asserts any of these literals.

// The fraction of the remaining distance a retarget closes. 1.0 would arrive
// within one retarget period (kRetargetPeriod * 96 samples, ~8 ms at N = 32
// and slice 8) -- a portamento, not the glissando spec section 5 asks a COLOR
// move to be. Below 1 the approach is geometric and can never overshoot,
// which is what keeps the hot loop free of a clamp.
constexpr float kApproachFrac = 0.22f;

// Bloom stagger: the highest partial's bloom lags the lowest by this much, so
// the swarm blooms rather than switches (spec section 3).
constexpr float kBloomStaggerS = 0.012f;

// The floor FLOW enforces however low FLOOR is set, so the drone promise holds
// at FLOOR 0 (spec section 4).
constexpr float kFlowFloorMin = 0.12f;

// RISE / FALL, in seconds. Deliberately long at the top (spec section 4).
constexpr float kRiseMinS = 0.005f, kRiseMaxS = 3.0f;
constexpr float kFallMinS = 0.05f,  kFallMaxS = 20.0f;

// The accent's two halves, deliberately equal on the first try -- the same
// choice SynthEngineT made for kAccentVelFloor/kAccentDecFloor, and for the
// same reason: a listening session says which half wants to differ.
constexpr float kAccentVelFloor = 0.3f;
constexpr float kAccentDecFloor = 0.3f;

// TILT: the exponent of the amplitude-over-overtone-index law, amp ~ n^-slope.
// Dark end is steep, bright end tips energy upward. Total power is normalized
// afterwards, so TILT moves the colour and not the level.
constexpr float kTiltDark = 2.0f, kTiltBright = -0.5f;

// FOCUS: the aperture's width in octaves at its narrowest and at fully open.
constexpr float kFocusNarrowOct = 0.5f, kFocusWideOct = 8.0f;

// HARM: harmonic -> stretched -> clustered. Below kHarmClusterStart the
// overtone exponent grows from 1 to 1 + kStretchMax; above it the positions
// blend toward the seeded cluster spread.
constexpr float kHarmClusterStart = 0.6f;
constexpr float kStretchMax = 0.35f;
constexpr float kClusterSpan = 0.6f;   // +/- this fraction of the overtone gap

// DRIFT: the walk's reach at depth 1, and the walk's own step and pull. Rate
// has no control of its own and scales gently with depth (spec section 3).
constexpr float kDriftCentsMax = 18.f;
constexpr float kDriftAmpMax = 0.35f;
constexpr float kDriftWalkStep = 0.06f;
constexpr float kDriftPull = 0.02f;
constexpr float kDriftRateDepthBoost = 0.5f;   // deeper = up to 1.5x faster

// Output trim: the bank's total power, before LEVEL. Sized against the part's
// headroom the same way SynthEngineT's kVoiceGain is.
constexpr float kSwarmGain = 0.30f;

}  // namespace swarm_cfg
}  // namespace spky
```

- [ ] **Step 4: Add the silent engine**

Create `engine/swarm/swarm_engine.h`. This task ships a **deliberately silent**
engine: it holds the state and satisfies the contract, and every later task
fills one part of it in. A silent engine is why G2 can pass here and why the
audio gates in Tasks 4–8 are real REDs rather than compile errors.

```cpp
#pragma once
#include <cstdint>
#include "mod/rng.h"
#include "parts/engine_iface.h"
#include "pitch/chord.h"
#include "synth/env.h"
#include "swarm/swarm_config.h"
#include "util/math.h"        // clampf, in the inline setters below
#include "util/onepole.h"

namespace spky {

// The additive partial swarm (spec 2026-08-17). One bank of
// swarm_cfg::kPartials sine partials per deck, retuned rather than re-voiced:
// a chord redistributes the partials over the chord tones' overtone series, so
// CPU is independent of played density and every note or chord change is a
// glide of the whole spectrum.
//
// Not a SynthEngineT<V>: there are no per-note voices to allocate, steal or
// demote. What SynthEngineT's machine does for notes, this engine does for
// TARGETS -- and the target computation is the only expensive part, so it runs
// a slice at a time (swarm_cfg::kRetargetSlice) at control rate.
class SwarmEngine : public IPartEngine {
public:
    // Must equal the raster Part and SynthEngine share; part.cpp's
    // static_assert pins the mod tick to it.
    static constexpr int kCtrlInterval = 96;

    void set_seed(uint32_t s) { _seed = s; }   // call BEFORE init

    void init(float sample_rate) override;
    void set_targets(const float* t, float tune) override;
    void trigger(float pitch_norm) override;
    void process(float& outL, float& outR) override;

    // Observers the hosts need (not SPKY_TESTING-gated: Part reads them).
    float bloom_level() const { return _bloom.value(); }
    bool  audible() const { return _bloom.active(); }

private:
    void _control_tick();

    float _sr = 48000.f;
    uint32_t _seed = 0xC0FFEEu;
    float _targets[LANE_COUNT] = { 0.5f, 0.5f, 0.5f, 0.f, 0.8f };
    Env   _bloom;                 // the whole swarm's envelope, unmodified
    OnePole _level;               // LEVEL, smoothed -- control side only
    int   _ctrl_ctr = 0;
};

}  // namespace spky
```

Create `engine/swarm/swarm_engine.cpp`:

```cpp
#include "swarm/swarm_engine.h"
#include "util/math.h"

using namespace spky;

void SwarmEngine::init(float sample_rate) {
    _sr = sample_rate;
    _bloom.init(sample_rate);
    _level.init(sample_rate, 0.01f);
    _level.reset(_targets[LANE_LEVEL]);
    _ctrl_ctr = 0;                 // first process() runs a control tick
}

void SwarmEngine::set_targets(const float* t, float /*tune*/) {
    for (int i = 0; i < LANE_COUNT; ++i) _targets[i] = t[i];
}

void SwarmEngine::trigger(float /*pitch_norm*/) {}   // Task 5 blooms here

void SwarmEngine::_control_tick() {}                 // Tasks 4-8 fill this in

void SwarmEngine::process(float& outL, float& outR) {
    if (--_ctrl_ctr <= 0) { _ctrl_ctr = kCtrlInterval; _control_tick(); }
    // Deliberately silent until Task 2 gives it a bank. The contract gate G2
    // passes on silence, which is the point: it proves the plumbing before any
    // audio claim exists to confuse it with.
    outL = 0.f;
    outR = 0.f;
}
```

- [ ] **Step 5: Wire it into Part, and into every build**

`engine/parts/part.h` — the include next to `#include "synth/synth_engine.h"`:

```cpp
#include "swarm/swarm_engine.h"
```

the member, immediately after `BbdEngine _bbd;`:

```cpp
    SwarmEngine    _swarm;
```

the `_engine_for` arm, after `case ENGINE_BBD:`:

```cpp
            case ENGINE_SWARM:   return static_cast<IPartEngine*>(&_swarm);
```

the accessor, next to `bbd()`:

```cpp
    SwarmEngine& swarm() { return _swarm; }
    const SwarmEngine& swarm() const { return _swarm; }
```

the meter arms (plan open point 4) — in `active_voices()` and `voice_env(int)`,
before the trailing `return`:

```cpp
        // A swarm is one sound, not n voices: report the bloom envelope on
        // slot 0 so max_voice_env() -- and through it the VCV LED and
        // Instrument's meter -- reads "how much is this deck sounding".
        if (_engine_id == ENGINE_SWARM) return _swarm.audible() ? 1 : 0;
```
```cpp
        if (_engine_id == ENGINE_SWARM) return v == 0 ? _swarm.bloom_level() : 0.f;
```

the six voice-row forwards — append `_swarm.` to each of the five that the whole
melodic family shares, and to FILT:

```cpp
    void set_voice_attack(float n)    { ... _bbd.set_attack(n); _swarm.set_rise(n); }
    void set_voice_decay(float n)     { ... _bbd.set_decay(n); _swarm.set_fall(n); }
    void set_voice_resonance(float n) { ... _bbd.set_resonance(n); _swarm.set_floor(n); }
    void set_voice_sub(float n)       { ... _bbd.set_sub(n); _swarm.set_sub(n); }
    void set_voice_detune(float n)    { ... _bbd.set_detune(n); _swarm.set_detune(n); }
    void set_voice_filt(float t)      { ... _bbd.set_filt(t); _swarm.set_balance(t); }
```

The six setters are declared in `swarm_engine.h` now and stored-only until
Tasks 4 and 5 spend them — the same shape Task 2 of the STEP-accent plan used,
so the later gates are behavioural REDs:

```cpp
    // The VOICE row, reinterpreted (spec section 4). RES becomes FLOOR, the
    // sustain floor; FILT becomes the even/odd balance, NOT another tilt --
    // the SOURCE lane owns tilt. SOURCE's contextual knob is HARM and arrives
    // through set_harm(), pushed by the host beside the rest of the row.
    void set_rise(float n)    { _rise_n  = clampf(n, 0.f, 1.f); }
    void set_fall(float n)    { _fall_n  = clampf(n, 0.f, 1.f); }
    void set_floor(float n)   { _floor_n = clampf(n, 0.f, 1.f); }
    void set_sub(float n)     { _sub_n   = clampf(n, 0.f, 1.f); }
    void set_detune(float n)  { _detune_n = clampf(n, 0.f, 1.f); }
    void set_balance(float t) { _balance = clampf(t, -1.f, 1.f); }
    void set_harm(float n)    { _harm_n  = clampf(n, 0.f, 1.f); }
```
with `float _rise_n = 0.3f, _fall_n = 0.5f, _floor_n = 0.5f, _sub_n = 0.3f,
_detune_n = 0.f, _balance = 0.f, _harm_n = 0.f;` private.

`engine/mod/super_modulator.h`, next to `pitch_note_accent()` — the observer
G3 needs:

```cpp
#ifdef SPKY_TESTING
    // Whether LANE_PITCH is running as a note lane rather than as an LFO --
    // the _note_lane() predicate of docs/engine-map.md section 1, which Part
    // sets from the engine id and nothing else can move.
    bool pitch_lane_is_note_lane_for_test() const {
        return _lanes[LANE_PITCH].note_lane_for_test();
    }
#endif
```

and the matching one-liner on `ModLane` (`engine/mod/lane.h`, inside its
existing `SPKY_TESTING` block):

```cpp
    bool note_lane_for_test() const { return _melodic && _flow_melody; }
```

Then the build files. `CMakeLists.txt`'s `render` target, after
`engine/synth/synth_engine.cpp`:

```cmake
    engine/swarm/swarm_engine.cpp
```

`bench/Makefile`, `bench/audition/Makefile`, `host/vcv/Makefile` and
`shell/Makefile` each carry an engine source list; add the same file with that
list's own prefix (`../engine/swarm/swarm_engine.cpp`,
`../../engine/swarm/swarm_engine.cpp`, `$(REPO)/engine/swarm/swarm_engine.cpp`,
`../engine/swarm/swarm_engine.cpp` respectively).

- [ ] **Step 6: Extend the tripwires the sentinel just tripped**

These are not chores; each one is a claim that stops being checked if it is
extended carelessly.

`engine/param_table.h` — the range, line 73:

```cpp
  X(P_ENGINE_A,   0.f, 6.f,  7)  X(P_ENGINE_B,   0.f, 6.f,  7) \
```

and its comment at line 34-35: `ENGINE_TEST_TONE=0 .. ENGINE_SWARM=6,
ENGINE_COUNT=7 -- so 0..6, 7 steps`.

`tests/test_param_table.cpp` — the over-range clamp case says 99 clamps to
`hi=5 == ENGINE_BBD`; it now clamps to `hi=6 == ENGINE_SWARM`. Update both the
comment and `CHECK(inst.engine_id(PART_B) == ENGINE_SWARM);`.

`tests/test_bbd_engine.cpp:22` — `CHECK(ENGINE_COUNT == 7);`.

`tests/test_part.cpp` — add `CHECK(ENGINE_SWARM == 6);` to the id census and
rename the case to `"part: engine ids stay patch-stable when an engine is
appended"`.

`tests/test_deck_bus.cpp` — three edits, and the third is the one that matters:

1. `static_assert(ENGINE_COUNT == 7, ...)` — keep the message verbatim.
2. Add `ENGINE_SWARM` to the sweep's initializer list.
3. The census case: `CHECK_FALSE(swarm.consumes_input());`, with a
   `SwarmEngine swarm;` beside the other five. **This is what keeps the
   sweep's claim honest**: the comment above the sweep argues that the
   `if (_src_deck)` check is structurally unreachable for every engine that
   does not override `consumes_input()`, and SWARM joins that set only because
   this line proves it does.

Note in the task report that the SWARM arm of the sweep is **audible**, not
vacuous — the sweep's own non-silence guard covers that, and it will only pass
once Task 4 gives the engine a spectrum. Until then the SWARM arm passes on
silence, which the sweep's `peak` guard at the end of the loop will catch. If it
does, extend the guard's engine exclusion **with a comment naming this task**
and remove it in Task 4 — do not weaken the guard.

- [ ] **Step 7: Run the tests**

```bash
cmake --build build && ./build/spky_tests -tc="swarm G*"
```

Expected: **G1 and G2 PASS**, **G3 FAILS** on
`CHECK(p.mod().pitch_lane_is_note_lane_for_test())` only if the derivation in
`part.cpp` is wrong (it should pass — record that it passed **and why that is a
finding worth stating**: the note-deck flag was free), and **G3b FAILS** on
`CHECK(m > 0.1f)` because the engine is silent and its bloom never triggers.
G3b's RED is expected to survive until Task 5; mark it `[[expected-red]]` in the
task report and do **not** weaken it.

If G3b's red is unacceptable for the intervening tasks, move the case verbatim
into Task 5's step 1 rather than loosening it.

- [ ] **Step 8: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: green except G3b, **with `ctrl_identity` and `wave_formant_sweep` at
their existing hashes**. `Part` gained a member, so `sizeof(Part)` moved; if
either hash moves, that is a finding — a declaration-order or padding change
cannot alter arithmetic, so a moved hash means something else changed. Stop and
report; do not re-baseline.

- [ ] **Step 9: Commit**

```bash
git add engine/parts/engine_iface.h engine/swarm/swarm_config.h \
        engine/swarm/swarm_engine.h engine/swarm/swarm_engine.cpp \
        engine/parts/part.h engine/param_table.h engine/mod/lane.h \
        engine/mod/super_modulator.h CMakeLists.txt bench/Makefile \
        bench/audition/Makefile host/vcv/Makefile shell/Makefile \
        tests/test_swarm_engine.cpp tests/test_deck_bus.cpp \
        tests/test_bbd_engine.cpp tests/test_part.cpp tests/test_param_table.cpp
git commit -m "feat(swarm): ENGINE_SWARM as a selectable silent engine

..."
```

The commit message states: which tripwires fired and what each of them was
protecting, that `kPartials` is a placeholder with `kNDecided == false`, and
G3b's expected RED.

---

### Task 2: The partial bank — the hot loop

**Files:**
- Create: `engine/swarm/swarm_bank.h`
- Create: `tests/test_swarm_bank.cpp`
- Modify: `CMakeLists.txt` — add the new test file after `tests/test_swarm_engine.cpp`

**Interfaces:**
- Consumes: `swarm_cfg::kPartials`, `kRetargetPeriod`, `kApproachFrac` (Task 1).
- Produces `class spky::SwarmBank`:
  - `void init(float sample_rate)` — all partials silent at phase 0
  - `void snap(int i, float hz, float amp, float pan)` — jump, no glide
  - `void set_target(int i, float hz, float amp, float pan)` — control rate;
    sizes the slopes so the partial closes `kApproachFrac` of the remaining
    distance over one retarget period and can never overshoot
  - `void process(float& outL, float& outR)` — inline, per sample
  - `float hz(int i) const`, `float amp(int i) const`, `float pan_power(int i) const`

- [ ] **Step 1: Write the failing tests**

Create `tests/test_swarm_bank.cpp`:

```cpp
// The SWARM partial bank -- the hot loop and nothing else.
// Spec: docs/superpowers/specs/2026-08-17-swarm-additive-drone-engine-design.md
// section 7 (CPU levers 1 and 2).
#include <doctest/doctest.h>
#include "swarm/swarm_bank.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace spky;
using namespace spky::swarm_cfg;

namespace {

// Frequency by zero crossings of the RISING edge, which is robust against the
// 1.2e-3 amplitude error fast_sin carries and needs no FFT.
float measured_hz(const std::vector<float>& b, float sr) {
    int first = -1, last = -1, n = 0;
    for (size_t i = 1; i < b.size(); ++i)
        if (b[i - 1] <= 0.f && b[i] > 0.f) {
            if (first < 0) first = static_cast<int>(i);
            last = static_cast<int>(i);
            ++n;
        }
    if (n < 2) return 0.f;
    return (n - 1) * sr / static_cast<float>(last - first);
}

std::vector<float> render(SwarmBank& b, int n, bool right = false) {
    std::vector<float> out(n);
    for (auto& s : out) { float l = 0.f, r = 0.f; b.process(l, r); s = right ? r : l; }
    return out;
}

// One partial audible, the rest silent -- the only way to measure a single
// partial's frequency in a bank that sums into one output.
SwarmBank one_partial(float hz, float amp = 0.5f, float pan = 0.f) {
    SwarmBank b;
    b.init(48000.f);
    b.snap(0, hz, amp, pan);
    for (int i = 1; i < kPartials; ++i) b.snap(i, 1000.f, 0.f, 0.f);
    return b;
}

}  // namespace

TEST_CASE("swarm G4: a snapped partial sounds at the frequency it was given") {
    for (float hz : {55.f, 220.f, 1000.f, 8000.f}) {
        CAPTURE(hz);
        SwarmBank b = one_partial(hz);
        std::vector<float> buf = render(b, 48000);
        CHECK(measured_hz(buf, 48000.f) == doctest::Approx(hz).epsilon(0.005));
        CHECK(b.hz(0) == doctest::Approx(hz).epsilon(1e-4));
    }
}

TEST_CASE("swarm G5: a retarget glides, arrives, and never steps") {
    SwarmBank b = one_partial(220.f);
    // The steady-state reference: the largest sample-to-sample step a 220 Hz
    // sine of this amplitude takes at all. Measured in the SAME run, so the
    // comparison carries no invented number -- a threshold written as a
    // literal here would be exactly the "threshold lives in the file it
    // polices" shape.
    std::vector<float> steady = render(b, 4800);
    float step_steady = 0.f;
    for (size_t i = 1; i < steady.size(); ++i)
        step_steady = std::max(step_steady, std::fabs(steady[i] - steady[i - 1]));

    // Now move it two octaves, retargeting once per period the way the engine
    // does, and watch the first difference across the whole glide.
    float step_glide = 0.f;
    float prev = steady.back();
    for (int period = 0; period < 200; ++period) {
        b.set_target(0, 880.f, 0.5f, 0.f);
        std::vector<float> seg = render(b, 96 * kRetargetPeriod);
        for (float v : seg) {
            step_glide = std::max(step_glide, std::fabs(v - prev));
            prev = v;
        }
    }
    // A sine at 880 Hz steps 4x as hard per sample as one at 220 Hz, so the
    // bound is the target's own steady step, not the start's. Derived, not
    // chosen: 4 == 880/220.
    CHECK(step_glide <= 4.2f * step_steady);
    CHECK(b.hz(0) == doctest::Approx(880.f).epsilon(0.01));
    // And the glide is not instantaneous -- otherwise the case above would
    // pass on a bank that simply jumps.
    SwarmBank c = one_partial(220.f);
    c.set_target(0, 880.f, 0.5f, 0.f);
    render(c, 96 * kRetargetPeriod);
    CHECK(c.hz(0) < 500.f);      // one period closes only kApproachFrac of it
}

TEST_CASE("swarm G6: the pan law is constant power") {
    // fast_sin's error is < 1.2e-3, so the window is 3e-3, not exact.
    float ref = 0.f;
    for (float pan : {-1.f, -0.5f, 0.f, 0.5f, 1.f}) {
        CAPTURE(pan);
        SwarmBank b = one_partial(220.f, 0.5f, pan);
        if (ref == 0.f) ref = b.pan_power(0);
        CHECK(b.pan_power(0) == doctest::Approx(ref).epsilon(0.003));
    }
    SwarmBank hard_left = one_partial(220.f, 0.5f, -1.f);
    CHECK(peak_l(render(hard_left, 4800)) > 0.4f);
    CHECK(peak_l(render(hard_left, 4800, /*right=*/true)) < 0.01f);
}

TEST_CASE("swarm G7: a zero-amplitude partial is exactly silent") {
    // The Nyquist rule and SUB-at-0 both rely on this being EXACT: a muted
    // partial still runs (the loop length must not change) and must contribute
    // bit-zero, not a denormal.
    SwarmBank b;
    b.init(48000.f);
    for (int i = 0; i < kPartials; ++i) b.snap(i, 100.f * (i + 1), 0.f, 0.f);
    for (int n = 0; n < 48000; ++n) {
        float l = 1.f, r = 1.f;
        b.process(l, r);
        REQUIRE(l == 0.f);
        REQUIRE(r == 0.f);
    }
}
```

`peak_l` is a two-line local helper; write it in the anonymous namespace beside
`render`.

Add to `CMakeLists.txt`, after `tests/test_swarm_engine.cpp`:

```cmake
    tests/test_swarm_bank.cpp
```

- [ ] **Step 2: Run it and confirm the build fails**

```bash
cmake --build build
```

Expected: `swarm/swarm_bank.h: No such file or directory`.

- [ ] **Step 3: Write the bank**

Create `engine/swarm/swarm_bank.h`:

```cpp
#pragma once
#include "swarm/swarm_config.h"
#include "util/fast_sin.h"
#include "util/math.h"

namespace spky {

// The SWARM hot loop (spec section 7, levers 1 and 2).
//
// Per partial: a phase accumulator, a phase increment, and TWO amplitudes --
// one per channel, so the constant-power pan gain is folded into the amplitude
// and glides for free. Each of the three carries a per-sample slope, so the
// inner loop is a branch-free multiply-add chain: three adds, one truncation
// wrap, one fast_sin, two multiply-accumulates. Seven floats per partial,
// ~1 KB at N = 32, no tables and no delay lines.
//
// Slopes, not smoothers, and no arrival counter either. set_target() sizes a
// slope to close swarm_cfg::kApproachFrac of the remaining distance over one
// retarget period, and the engine calls it once per period per partial. So the
// slope is always replaced before it can overshoot, the approach is geometric
// (a control-side one-pole with an audio-rate linear ramp between its points --
// spec section 7's "OnePole on the control side only"), and no branch, clamp or
// counter is needed in here at all.
//
// Header-only so this loop inlines into SwarmEngine::process the way
// Part::process inlines into Instrument::process.
class SwarmBank {
public:
    void init(float sample_rate) {
        _sr = sample_rate;
        _inv_sr = 1.f / sample_rate;
        for (int i = 0; i < swarm_cfg::kPartials; ++i) {
            _p[i] = P{};
        }
    }

    // Jump. Used at init and on an engine activation, where there is no
    // previous spectrum to glide from.
    void snap(int i, float hz, float amp, float pan) {
        P& p = _p[i];
        float gl, gr;
        _pan_gains(pan, gl, gr);
        p.inc = _inc_of(hz);
        p.al = amp * gl;
        p.ar = amp * gr;
        p.inc_slope = 0.f;
        p.al_slope = 0.f;
        p.ar_slope = 0.f;
    }

    // Control rate: once per retarget period per partial.
    void set_target(int i, float hz, float amp, float pan) {
        P& p = _p[i];
        float gl, gr;
        _pan_gains(pan, gl, gr);
        p.inc_slope = (_inc_of(hz) - p.inc) * kSlope;
        p.al_slope  = (amp * gl - p.al) * kSlope;
        p.ar_slope  = (amp * gr - p.ar) * kSlope;
    }

    // Per sample.
    void process(float& outL, float& outR) {
        float l = 0.f, r = 0.f;
        for (int i = 0; i < swarm_cfg::kPartials; ++i) {
            P& p = _p[i];
            p.inc += p.inc_slope;
            p.al  += p.al_slope;
            p.ar  += p.ar_slope;
            p.ph  += p.inc;
            // Truncation wrap, not `if (ph >= 1) ph -= 1`: the branch is what
            // this loop exists to avoid, and inc is always in [0, 0.5) so one
            // subtraction is always enough. fast_sin wraps too, but leaving
            // the accumulator unwrapped would bleed mantissa bits.
            p.ph -= static_cast<float>(static_cast<int>(p.ph));
            const float s = fast_sin(p.ph);
            l += s * p.al;
            r += s * p.ar;
        }
        outL = l;
        outR = r;
    }

    // Observers. Plain const, not SPKY_TESTING-gated: the engine's own gates
    // read them and they cost nothing.
    float hz(int i) const { return _p[i].inc * _sr; }
    float amp(int i) const {
        const float a = _p[i].al, b = _p[i].ar;
        return std::sqrt(a * a + b * b);
    }
    float pan_power(int i) const { return amp(i); }

private:
    struct P {
        float ph = 0.f;
        float inc = 0.f, inc_slope = 0.f;
        float al = 0.f, al_slope = 0.f;
        float ar = 0.f, ar_slope = 0.f;
    };

    // The equal-power pan law VoiceT::_update_control already uses (voice.cpp):
    // angle 0..0.25 turns, gl = cos, gr = sin, both through fast_sin so the
    // control side stays free of libm.
    static void _pan_gains(float pan, float& gl, float& gr) {
        const float a = (clampf(pan, -1.f, 1.f) + 1.f) * 0.125f;
        gr = fast_sin(a);
        gl = fast_sin(a + 0.25f);
    }

    float _inc_of(float hz) const { return hz * _inv_sr; }

    // The whole reason no counter and no clamp is needed: one period's worth of
    // this slope covers exactly kApproachFrac of the distance, and the engine
    // replaces it every period.
    static constexpr float kSlope =
        swarm_cfg::kApproachFrac /
        static_cast<float>(swarm_cfg::kCtrlInterval * swarm_cfg::kRetargetPeriod);

    P _p[swarm_cfg::kPartials];
    float _sr = 48000.f;
    float _inv_sr = 1.f / 48000.f;
};

}  // namespace spky
```

Reading `SwarmEngine::kCtrlInterval` from here would be a circular include, so
the raster lives in `swarm_config.h` and the engine reads it from there:

```cpp
// swarm_config.h, in the structure block
constexpr int kCtrlInterval = 96;   // pinned to SynthEngine's by a static_assert
```
```cpp
// swarm_engine.h
static constexpr int kCtrlInterval = swarm_cfg::kCtrlInterval;
```
and in `swarm_engine.cpp`, at file scope:

```cpp
static_assert(SwarmEngine::kCtrlInterval == SynthEngine::kCtrlInterval,
              "the swarm's control raster must be the part layer's raster");
```
(`swarm_engine.cpp` includes `synth/synth_engine.h` for that one line only; say
so in a comment beside the include.)

`swarm_bank.h` then uses `swarm_cfg::kCtrlInterval` in `kSlope` and needs
`<cmath>` for `std::sqrt` in the observer.

- [ ] **Step 4: Run the bank tests**

```bash
cmake --build build && ./build/spky_tests -tc="swarm G4" -tc="swarm G5" -tc="swarm G6" -tc="swarm G7"
```

Expected: all four PASS. If G4 misses at 8000 Hz, check `measured_hz`'s crossing
count before touching the bank — 8 kHz at 48 kHz is 6 samples per cycle and the
crossing estimator's resolution, not the bank's accuracy, is what is being
measured there. If that is the cause, lower the top test frequency to 4000 Hz
and **say so in the report**, naming the estimator as the reason.

- [ ] **Step 5: Prove each of the four can go red**

Four one-line mutations, one at a time, rebuild, confirm the named case fails,
revert:

- G4 — make `_inc_of` return a constant `0.01f`. G4 fails on the measured Hz at
  every frequency but 480.
- G5 — make `set_target` call `snap` instead. The `step_glide` bound fails, and
  so does `CHECK(c.hz(0) < 500.f)`.
- G6 — replace `_pan_gains` with the linear law `gl = 1 - x, gr = x`. The
  constant-power check fails at pan 0 (0.707 against 1.0).
- G7 — initialise `P::al` to `1e-30f` instead of `0.f`. G7 fails on
  `REQUIRE(l == 0.f)`.

Copy the four failure lines into the Step 6 commit message.

- [ ] **Step 6: Commit**

```bash
git add engine/swarm/swarm_bank.h engine/swarm/swarm_config.h \
        engine/swarm/swarm_engine.h engine/swarm/swarm_engine.cpp \
        tests/test_swarm_bank.cpp CMakeLists.txt
git commit -m "feat(swarm): the partial bank -- slopes, one fast_sin, pan gains

..."
```

---

### Task 3: The `swarm_bank` bench decides N — **NEEDS BASTIAN + HARDWARE**

**This task cannot be completed by an agent alone.** It builds with the ARM
toolchain, flashes the Daisy Patch Submodule over DFU, and needs the two button
presses of a first USB session. Everything up to the flash can be prepared
unattended; the measurement itself is Bastian's.

**Files:**
- Create: `bench/workloads_swarm.cpp`
- Modify: `bench/workload.h` — the `kSwarmWorkloads` extern pair
- Modify: `bench/families.cpp` — the registry entry
- Modify: `bench/Makefile` — `FAMILY_SOURCE_swarm`, `FAMILY_DEFINE_swarm`
- Modify: `bench/run.py` — `BENCH_PROTOCOL_ROWS_BY_FAMILY["swarm"]`
- Modify: `bench/profiles.py` — the `swarm` profile
- Modify: `bench/test_run_contract.py` — the row-set expectation
- Modify: `engine/swarm/swarm_config.h` — `kPartials`, `kNDecided`
- Create: `docs/bench/<date>-<hash>-swarm-axi-o3-patch_sm-usb.{md,csv}` (written
  by `run.py`, prose section by hand)
- Modify: `tests/test_swarm_engine.cpp` — G8

**Interfaces:**
- Consumes: `SwarmBank` (Task 2).
- Produces: a measured `swarm_cfg::kPartials` and `kNDecided == true`.

- [ ] **Step 1: Write the gate that N is a decision, and watch it fail**

Append to `tests/test_swarm_engine.cpp`:

```cpp
TEST_CASE("swarm G8: N is a measured decision, not a placeholder") {
    // swarm_cfg::kPartials is set by the swarm_bank rows on the Patch
    // Submodule (spec section 8). This gate is the only thing standing between
    // a guessed N and main; the bench task flips kNDecided when the result is
    // in, and names the run's docs/bench/ file in the commit message.
    CHECK(swarm_cfg::kNDecided);
}
```

```bash
cmake --build build && ./build/spky_tests -tc="swarm G8"
```

Expected: FAIL, `CHECK( swarm_cfg::kNDecided )` with `kNDecided` false. That is
the RED, and it stays red until Step 5.

- [ ] **Step 2: Add the bench family**

Create `bench/workloads_swarm.cpp`. The rows price the **real** `SwarmBank`, not
a bench-local copy — a kernel row that measures a copy measures the copy.

```cpp
#include "workload.h"
#include "serial_arena.h"
#include "swarm/swarm_bank.h"
#include "swarm/swarm_config.h"

namespace bench {
namespace {

using namespace spky;

// Rows run strictly serially in table order, so their state shares one
// arena slot -- the pattern workloads_body.cpp and workloads_system.cpp use.
struct SwarmBankGroup {
    SwarmBank bank;
    int       tick;
    int       slice;
};

SerialArena<SwarmBankGroup> g_swarm_arena;

// The worst case a partial bank can be in: every partial audible, every
// partial retargeted on its own slice, and the frequencies moving every
// period so no slope is ever zero (a bank at rest would price a loop the
// compiler can hoist half of).
void setup_swarm_bank()
{
    auto& g = g_swarm_arena.emplace<SwarmBankGroup>();
    g.bank.init(kSampleRate);
    g.tick = 0;
    g.slice = 0;
    for (int i = 0; i < swarm_cfg::kPartials; ++i)
        g.bank.snap(i, 80.f * static_cast<float>(i + 1), 0.25f,
                    -1.f + 2.f * static_cast<float>(i) / swarm_cfg::kPartials);
}

float proc_swarm_bank()
{
    auto& g = g_swarm_arena.get<SwarmBankGroup>();
    // One control tick per block, one slice of partials retargeted, exactly as
    // SwarmEngine::_control_tick does it.
    const int base = g.slice * swarm_cfg::kRetargetSlice;
    for (int k = 0; k < swarm_cfg::kRetargetSlice; ++k) {
        const int i = base + k;
        if (i >= swarm_cfg::kPartials) break;
        const float wob = (g.tick & 1) ? 1.002f : 0.998f;
        g.bank.set_target(i, 80.f * static_cast<float>(i + 1) * wob, 0.25f,
                          -1.f + 2.f * static_cast<float>(i) / swarm_cfg::kPartials);
    }
    if (++g.slice >= swarm_cfg::kRetargetPeriod) { g.slice = 0; ++g.tick; }

    float acc = 0.f;
    for (size_t n = 0; n < kBlock; ++n) {
        float l = 0.f, r = 0.f;
        g.bank.process(l, r);
        acc += l + r;
    }
    return acc;
}

}  // namespace

const Workload kSwarmWorkloads[] = {
    { "swarm", "swarm_bank", setup_swarm_bank, proc_swarm_bank },
};
const int kSwarmCount = sizeof(kSwarmWorkloads) / sizeof(kSwarmWorkloads[0]);

}  // namespace bench
```

**One row, several builds — not several rows.** N is a compile-time constant, so
"N = 16 against N = 32" is two images, not two rows, and a row that instantiated
a second bank at a second N would double the icache footprint and price neither
honestly. The sweep is: build, measure, edit `kPartials`, rebuild, measure. The
per-partial cost is the slope of that line, and the whole point of taking three
points is that a single point cannot tell a linear loop from one with a fixed
overhead.

`bench/workload.h`, with the other family externs:

```cpp
extern const Workload kSwarmWorkloads[];
extern const int      kSwarmCount;
```

`bench/families.cpp`, before the `sampler` entry (which must stay last):

```cpp
#if BENCH_FAMILY_SWARM
    { "swarm",   kSwarmWorkloads,   kSwarmCount   },
#endif
```

`bench/Makefile`:

```make
FAMILY_SOURCE_swarm   = workloads_swarm.cpp
FAMILY_DEFINE_swarm   = BENCH_FAMILY_SWARM
```

`bench/run.py`, a new entry in `BENCH_PROTOCOL_ROWS_BY_FAMILY`:

```python
    "swarm": (
        "swarm_bank",
    ),
```

`bench/profiles.py`:

```python
    # The SWARM kernel round (spec 2026-08-17). Carries `system` for the same
    # reason `body` and `sweep` do: without it verdict() finds no DTCM+BBD gate
    # anchor and reports "undetermined", and the whole question -- how many
    # partials fit -- is only meaningful against the instrument's own worst
    # case measured in the SAME image (bench rows shift by points from icache
    # layout alone, so a cross-image subtraction is not a measurement).
    "swarm": Profile(
        families=("system", "swarm"),
        gates=frozenset({WAVE_ACCEPTANCE}),
    ),
```

`bench/test_run_contract.py` carries the expected row set; extend it the way the
existing families are listed there and run it:

```bash
python bench/test_run_contract.py
```

- [ ] **Step 3: Build the three images, and prove each one actually contains the bank**

Never `cd`; `run.py` sets its own working directory for make. **Do not** source
`env.sh` in this shell.

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" \
  python bench/run.py --profile swarm --board patch_sm --transport usb \
  --optimization o3 --build-only
```

Then, before believing anything:

```bash
grep -c "setup_swarm_bank\|proc_swarm_bank" bench/build/bench.map
```

Expected: both symbols present. **An unchanged memory table is not evidence** —
`SerialArena` overlays its groups, so adding a row legitimately leaves SRAM
byte-identical (memory `fireflow-bench-stale-object-trap`). If the symbols are
missing, `touch bench/workloads_swarm.cpp` and rebuild: `bench/Makefile` compares
mtimes at one-second granularity and a checkout landing inside the same second
leaves a stale `.o` that links and reports success.

- [ ] **Step 4: Measure — Bastian, at the board**

For each N in the sweep (start at the placeholder, then one clearly smaller and
one clearly larger — three points, and none of the three is a claim until it is
printed), edit `swarm_cfg::kPartials`, then:

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" \
  python bench/run.py --profile swarm --board patch_sm --transport usb \
  --optimization o3 --repeat 2
```

First run of a session: tap RESET, then BOOT inside the bootloader's 2-second
window. Later repeats are unattended. `bench/run.py` names its output file by the
HEAD commit hash, so **commit the workload code before measuring** or the result
file is labelled with the wrong hash (memory `fireflow-gotchas`).

Record, per N: `avg_cyc`, `max_cyc`, `pct_avg`, `pct_max` for `swarm_bank`, and
the same image's `instrument_worst` — the anchor that makes the numbers mean
something.

- [ ] **Step 5: Decide N, and flip the flag**

The arithmetic, written out so the next reader can check it:

- cycles per partial = (`swarm_bank` avg at N₂ − at N₁) / (N₂ − N₁)
- fixed overhead = `swarm_bank` avg at N₁ − N₁ × cycles per partial
- the budget SWARM may spend = whatever headroom the same image's
  `instrument_worst` leaves under 960 000 cycles, **minus** what SWARM's control
  tick and the deck FX will add on top (Task 10 measures that; here, leave a
  stated reserve and say what it is)
- N = floor of that budget over cycles per partial

Then edit `engine/swarm/swarm_config.h`: `kPartials` to the decided value, and

```cpp
constexpr bool kNDecided = true;   // measured <date>, docs/bench/<file>.md
```

replacing the placeholder comment with a citation of the run: board, transport,
optimization, profile, commit, and the cycles-per-partial figure.

```bash
source env.sh && cmake --build build && ./build/spky_tests -tc="swarm G8"
```

Expected: PASS. Then the whole desktop suite, because `kPartials` changed and
every gate loops to it:

```bash
ctest --test-dir build --output-on-failure
```

- [ ] **Step 6: If the bench says no — and only then**

Two ways it can say no, with different answers:

1. **N comes out usable but small** (fewer partials than a swarm needs to beat):
   that is a voicing question, not an architecture one. Report the number, ship
   it, and let Task 12's listening session say whether it is enough. `kPartials`
   is a rebuild.
2. **Cycles per partial is so high that no useful N fits**: this is the one case
   that opens Plan B — recursive sine oscillators,
   `s[n] = 2cos(ω)·s[n−1] − s[n−2]`, which trade `fast_sin` for two multiplies
   at the cost of coefficient updates and renormalisation under a glide.
   **Do not start building it.** Report the measurement, stop the plan, and let
   Bastian decide: Plan B is a different `SwarmBank`, so it is a new spec round
   for Task 2, not a patch to it.

- [ ] **Step 7: Write the bench document and commit**

`run.py` writes the CSV and the table into `docs/bench/`. Add the prose section
by hand, in the idiom of `docs/bench/2026-08-07-d0b3c08-regress-axi-o3-patch_sm-usb.md`:
the three N points, the derived cycles per partial, the same-image
`instrument_worst`, the reserve subtracted, and the N chosen. State explicitly
that these are **submodule** numbers and that no Seed figure entered the
decision.

```bash
git add bench/workloads_swarm.cpp bench/workload.h bench/families.cpp \
        bench/Makefile bench/run.py bench/profiles.py bench/test_run_contract.py
git commit -m "bench(swarm): the swarm_bank kernel row, and the swarm profile

..."
```

```bash
git add engine/swarm/swarm_config.h tests/test_swarm_engine.cpp docs/bench
git commit -m "feat(swarm): N is measured -- <value> partials

..."
```

---

### Task 4: Allocation and the spectral map

**Files:**
- Modify: `engine/swarm/swarm_engine.h` — the bank, the target arrays, the map's
  private methods, the SPKY_TESTING observers
- Modify: `engine/swarm/swarm_engine.cpp` — `_control_tick`, `_rebuild_targets`,
  `_map_partial`
- Modify: `tests/test_swarm_engine.cpp` — append G9–G15

**Interfaces:**
- Consumes: `SwarmBank` (Task 2), the voice-row setters (Task 1 Step 5).
- Produces:
  - `float SwarmEngine::partial_hz_for_test(int i) const` and
    `partial_amp_for_test(int i) const`, `SPKY_TESTING` only
  - the invariant every later task depends on: **the target list is generated
    sorted ascending by frequency, and partial slot `i` always holds target
    `i`.** That is what makes retargeting nearest-neighbour for free (Task 6).

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_swarm_engine.cpp`:

```cpp
namespace {

// The bank's settled state after enough ticks for every slice to have been
// retargeted and the glides to have arrived. kRetargetPeriod ticks cover the
// bank once; 40 periods is far past the geometric approach's settling.
void settle(SwarmEngine& e) {
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 96 * swarm_cfg::kRetargetPeriod * 40; ++i) e.process(l, r);
}

std::vector<float> hz_of(const SwarmEngine& e) {
    std::vector<float> out;
    for (int i = 0; i < swarm_cfg::kPartials; ++i) out.push_back(e.partial_hz_for_test(i));
    return out;
}

std::vector<float> amp_of(const SwarmEngine& e) {
    std::vector<float> out;
    for (int i = 0; i < swarm_cfg::kPartials; ++i) out.push_back(e.partial_amp_for_test(i));
    return out;
}

// Spectral centroid over the audible partials -- the one number that says
// "where the energy sits" without an FFT.
float centroid(const SwarmEngine& e) {
    float num = 0.f, den = 0.f;
    for (int i = 0; i < swarm_cfg::kPartials; ++i) {
        const float a = e.partial_amp_for_test(i);
        num += a * e.partial_hz_for_test(i);
        den += a;
    }
    return den > 0.f ? num / den : 0.f;
}

}  // namespace

TEST_CASE("swarm G9: the bank is sorted, audible, and built on the root") {
    SwarmEngine e = fresh_swarm();
    e.set_harm(0.f);
    feed(e, 0.5f);                    // pitch_to_hz(0.5) == 110 * 8^0.5
    e.trigger(0.5f);
    settle(e);
    std::vector<float> hz = hz_of(e);
    // Sorted ascending, always. Task 6's nearest-neighbour retargeting is
    // exactly this invariant and nothing else.
    for (size_t i = 1; i < hz.size(); ++i) {
        CAPTURE(i);
        CHECK(hz[i] >= hz[i - 1]);
    }
    // And it is a swarm, not one note: at least half the partials audible.
    std::vector<float> a = amp_of(e);
    const int audible = static_cast<int>(std::count_if(
        a.begin(), a.end(), [](float x) { return x > 1e-4f; }));
    CHECK(audible > swarm_cfg::kPartials / 2);
}

TEST_CASE("swarm G10: TILT walks the energy up and down the stacks") {
    SwarmEngine dark = fresh_swarm();
    feed(dark, 0.5f, /*tilt=*/0.f);
    dark.trigger(0.5f);
    settle(dark);
    SwarmEngine bright = fresh_swarm();
    feed(bright, 0.5f, /*tilt=*/1.f);
    bright.trigger(0.5f);
    settle(bright);
    // Direction AND size: a gate on direction alone passes on a tilt that
    // moves the centroid by a hertz.
    CHECK(centroid(bright) > 2.f * centroid(dark));
    // And the level does NOT move with it -- the tilt is normalized, so TILT
    // is a colour control and not a volume control.
    const float pk_dark = peak_of(render_l(dark, 4800));
    const float pk_bright = peak_of(render_l(bright, 4800));
    REQUIRE(pk_dark > 1e-3f);
    CHECK(pk_bright == doctest::Approx(pk_dark).epsilon(0.5));
}

TEST_CASE("swarm G11: HARM runs harmonic -> stretched -> clustered, deterministically") {
    SwarmEngine harmonic = fresh_swarm(4242u);
    harmonic.set_harm(0.f);
    feed(harmonic, 0.5f);
    harmonic.trigger(0.5f);
    settle(harmonic);
    // Harmonic: every audible partial above the SUB pair is an integer
    // multiple of the root, to the glide's own residual.
    const float root = harmonic.root_hz_for_test();
    REQUIRE(root > 1.f);
    int checked = 0;
    for (int i = swarm_cfg::kSubPartials; i < swarm_cfg::kPartials; ++i) {
        if (harmonic.partial_amp_for_test(i) <= 1e-4f) continue;
        const float n = harmonic.partial_hz_for_test(i) / root;
        CAPTURE(i);
        CAPTURE(n);
        CHECK(std::fabs(n - std::round(n)) < 0.02f);
        ++checked;
    }
    REQUIRE(checked > 4);            // the loop above actually ran

    // Clustered: the same partials are NOT integer multiples any more.
    SwarmEngine clustered = fresh_swarm(4242u);
    clustered.set_harm(1.f);
    feed(clustered, 0.5f);
    clustered.trigger(0.5f);
    settle(clustered);
    int off_grid = 0;
    for (int i = swarm_cfg::kSubPartials; i < swarm_cfg::kPartials; ++i) {
        if (clustered.partial_amp_for_test(i) <= 1e-4f) continue;
        const float n = clustered.partial_hz_for_test(i) / root;
        if (std::fabs(n - std::round(n)) > 0.05f) ++off_grid;
    }
    CHECK(off_grid > 4);

    // "Same knob position, same metal": the cluster map is seeded, so a
    // second engine at the same seed lands on the same frequencies to the bit.
    SwarmEngine twin = fresh_swarm(4242u);
    twin.set_harm(1.f);
    feed(twin, 0.5f);
    twin.trigger(0.5f);
    settle(twin);
    for (int i = 0; i < swarm_cfg::kPartials; ++i) {
        CAPTURE(i);
        REQUIRE(twin.partial_hz_for_test(i) == clustered.partial_hz_for_test(i));
    }
}

TEST_CASE("swarm G12: BAL empties the even partials at its negative end") {
    SwarmEngine e = fresh_swarm();
    e.set_harm(0.f);
    e.set_balance(-1.f);
    feed(e, 0.5f);
    e.trigger(0.5f);
    settle(e);
    const float root = e.root_hz_for_test();
    int odd_audible = 0, even_audible = 0;
    for (int i = swarm_cfg::kSubPartials; i < swarm_cfg::kPartials; ++i) {
        if (e.partial_amp_for_test(i) <= 1e-4f) continue;
        const int n = static_cast<int>(std::round(e.partial_hz_for_test(i) / root));
        if (n % 2 == 0) ++even_audible; else ++odd_audible;
    }
    CHECK(odd_audible > 2);
    CHECK(even_audible == 0);
}

TEST_CASE("swarm G13: no partial is ever placed above the Nyquist ceiling") {
    // Plan open point 1. At the top of the pitch axis (880 Hz root) a 32-deep
    // overtone ladder reaches 28 kHz, so this is reachable in play, not a
    // theoretical corner.
    SwarmEngine e = fresh_swarm();
    e.set_harm(0.f);
    feed(e, 1.f);                    // the top note
    e.trigger(1.f);
    settle(e);
    const float ceiling = swarm_cfg::kMaxHzFrac * 48000.f;
    int muted = 0;
    for (int i = 0; i < swarm_cfg::kPartials; ++i) {
        CAPTURE(i);
        if (e.partial_amp_for_test(i) > 1e-4f)
            CHECK(e.partial_hz_for_test(i) <= ceiling);
        else
            ++muted;
    }
    // Whether anything CAN be muted depends on N, and N is measured, not known
    // here -- at a small N the harmonic ladder never reaches the ceiling. So
    // derive the expectation from the constants instead of assuming it: the
    // top note's root is 880 Hz, its highest harmonic slot is
    // (kPartials - kSubPartials), and which side of the ceiling that lands on
    // decides which half of this gate has teeth.
    const float top_harmonic_hz =
        880.f * static_cast<float>(swarm_cfg::kPartials - swarm_cfg::kSubPartials);
    if (top_harmonic_hz > ceiling)
        CHECK(muted > 0);        // the ladder crosses: prove the mute fires
    else
        CHECK(muted == 0);       // it does not: prove the mute never overreaches
    // And the loop length did not change: CPU is constant regardless.
    CHECK(e.partial_count_for_test() == swarm_cfg::kPartials);
}

TEST_CASE("swarm G14: SUB puts its pair an octave below the root, and only it") {
    SwarmEngine on = fresh_swarm();
    on.set_sub(1.f);
    feed(on, 0.5f);
    on.trigger(0.5f);
    settle(on);
    const float root = on.root_hz_for_test();
    for (int i = 0; i < swarm_cfg::kSubPartials; ++i) {
        CAPTURE(i);
        CHECK(on.partial_hz_for_test(i) == doctest::Approx(root * 0.5f).epsilon(0.02));
        CHECK(on.partial_amp_for_test(i) > 1e-3f);
    }
    // At SUB 0 the pair is silent but still there -- muted, not removed.
    SwarmEngine off = fresh_swarm();
    off.set_sub(0.f);
    feed(off, 0.5f);
    off.trigger(0.5f);
    settle(off);
    for (int i = 0; i < swarm_cfg::kSubPartials; ++i) {
        CAPTURE(i);
        CHECK(off.partial_amp_for_test(i) < 1e-5f);
    }
    CHECK(off.partial_count_for_test() == swarm_cfg::kPartials);
}

TEST_CASE("swarm G15: FOCUS is open at the centre and a formant away from it") {
    // Plan open point 2: one number, both jobs. 0.5 = fully open; toward 0 a
    // narrow LOW window; toward 1 a narrow HIGH one.
    SwarmEngine open = fresh_swarm();
    feed(open, 0.5f, 0.5f, /*focus=*/0.5f);
    open.trigger(0.5f);
    settle(open);
    SwarmEngine low = fresh_swarm();
    feed(low, 0.5f, 0.5f, /*focus=*/0.f);
    low.trigger(0.5f);
    settle(low);
    SwarmEngine high = fresh_swarm();
    feed(high, 0.5f, 0.5f, /*focus=*/1.f);
    high.trigger(0.5f);
    settle(high);

    auto audible = [](const SwarmEngine& e) {
        int n = 0;
        for (int i = 0; i < swarm_cfg::kPartials; ++i)
            if (e.partial_amp_for_test(i) > 0.05f * e.loudest_for_test()) ++n;
        return n;
    };
    // Narrow really is narrower, at BOTH ends -- a gate on one end alone
    // passes on a monotone aperture that cannot sweep.
    CHECK(audible(low) < audible(open));
    CHECK(audible(high) < audible(open));
    // And the two windows sit in different places.
    CHECK(centroid(high) > 2.f * centroid(low));
}
```

- [ ] **Step 2: Run to confirm it fails to build**

```bash
cmake --build build
```

Expected: `no member named 'partial_hz_for_test' in 'spky::SwarmEngine'`.

- [ ] **Step 3: Add the observers and the bank, but not the map**

`engine/swarm/swarm_engine.h` — the bank member and the target arrays:

```cpp
    SwarmBank _bank;
    // The target the map computed for each slot, sorted ascending by
    // frequency. The bank glides toward these; the slot order IS the
    // nearest-neighbour rule (Task 6).
    float _t_hz[swarm_cfg::kPartials] = {};
    float _t_amp[swarm_cfg::kPartials] = {};
    float _t_pan[swarm_cfg::kPartials] = {};
    float _root_hz = 220.f;
    // The chord surface Part pushes, the SynthEngineT member of the same name
    // and the same size. init() sets _chord[0] = _targets[LANE_PITCH] and
    // _chord_n = 1, so a set_targets before any set_chord still allocates.
    float _chord[ChordBuilder::kMaxNotes] = {};
    int   _chord_n = 1;
```

and the `SPKY_TESTING` twins Tasks 6 and 7 read:

```cpp
    int   chord_n_for_test() const { return _chord_n; }
    int   bloom_count_for_test() const { return _bloom_count; }
    int   retargets_last_tick_for_test() const { return _retargets_last_tick; }
    float target_hz_for_test(int i) const { return _t_hz[i]; }
```
with `int _slice = 0;` and `int _retargets_last_tick = 0;` private (Task 7 is
what makes `_slice` move; declaring both here keeps the observer block in one
place).

and the observers, in a new `SPKY_TESTING` block — same idiom as
`SynthEngineT::accent_for_test`, compiled only for the tests target so `render`
and the firmware never see them:

```cpp
#ifdef SPKY_TESTING
    int   partial_count_for_test() const { return swarm_cfg::kPartials; }
    float partial_hz_for_test(int i) const { return _bank.hz(i); }
    float partial_amp_for_test(int i) const { return _bank.amp(i); }
    float root_hz_for_test() const { return _root_hz; }
    float loudest_for_test() const {
        float m = 0.f;
        for (int i = 0; i < swarm_cfg::kPartials; ++i) m = std::max(m, _bank.amp(i));
        return m;
    }
#endif
```

and `process()` sums the bank instead of writing zeros:

```cpp
void SwarmEngine::process(float& outL, float& outR) {
    if (--_ctrl_ctr <= 0) { _ctrl_ctr = kCtrlInterval; _control_tick(); }
    _bank.process(outL, outR);
    const float g = _level.process(_targets[LANE_LEVEL]) * swarm_cfg::kSwarmGain;
    outL *= g;
    outR *= g;
}
```

- [ ] **Step 4: Run the tests to see them fail behaviourally**

```bash
cmake --build build && ./build/spky_tests -tc="swarm G9" -tc="swarm G1[0-5]"
```

Expected: **G9 fails** on `CHECK(audible > kPartials / 2)` (every amplitude is
still 0), **G10 fails** on `REQUIRE(pk_dark > 1e-3f)`, **G11 fails** on
`REQUIRE(root > 1.f)`, **G12–G15 fail** on their own non-vacuity guards
(`odd_audible > 2`, `muted > 0`, `partial_amp_for_test > 1e-3f`,
`audible(low) < audible(open)` reading 0 < 0). Every one of those is the
non-vacuity guard doing its job on an engine that makes no sound; copy the six
lines into the Step 6 commit message.

- [ ] **Step 5: Build the map**

`engine/swarm/swarm_engine.cpp`. Three functions, all control rate, `std::pow`
allowed:

```cpp
namespace {

// The pitch contract, identical to SynthEngine's (synth_engine.cpp): 0..1 is
// 36 semitones, 110 Hz .. 880 Hz. Shared by value, not by include, because
// that function is in an anonymous namespace there -- if one moves, both move.
inline float pitch_to_hz(float p) { return 110.f * std::pow(8.f, clampf(p, 0.f, 1.f)); }

}  // namespace

// One partial's place in the spectrum: which chord tone, which overtone, and
// what the four voicing axes make of it.
//
// Frequency: the HARM arc. Below kHarmClusterStart the overtone exponent grows
// from 1 (harmonic) to 1 + kStretchMax (stretched -- piano, bell); above it the
// stretched position blends toward the partial's own seeded cluster offset, so
// the same knob position always yields the same metal.
//
// Amplitude: tilt over the overtone index, times the even/odd balance, times
// the focus window, then normalized as a group so none of the three is a volume
// control.
void SwarmEngine::_rebuild_targets() {
    const float root = pitch_to_hz(_targets[LANE_PITCH]);
    _root_hz = root;
    const float ceiling = swarm_cfg::kMaxHzFrac * _sr;

    // SUB first: kSubPartials slots at the octave below, amplitude from the
    // knob alone. They are always slots 0..kSubPartials-1 because the list is
    // sorted ascending and nothing is lower.
    for (int i = 0; i < swarm_cfg::kSubPartials; ++i) {
        _t_hz[i] = root * 0.5f;
        _t_amp[i] = _sub_n;
        _t_pan[i] = 0.f;             // the foundation stays centred
    }

    // The swarm proper. Partial j takes chord tone (j % nch) and overtone
    // (j / nch + 1), so the tones interleave and a low overtone of every tone
    // exists before any tone gets a high one.
    const int n_sw = swarm_cfg::kPartials - swarm_cfg::kSubPartials;
    const float beta = _harm_n < swarm_cfg::kHarmClusterStart
        ? swarm_cfg::kStretchMax * (_harm_n / swarm_cfg::kHarmClusterStart)
        : swarm_cfg::kStretchMax;
    const float cluster = _harm_n < swarm_cfg::kHarmClusterStart
        ? 0.f
        : (_harm_n - swarm_cfg::kHarmClusterStart) /
          (1.f - swarm_cfg::kHarmClusterStart);
    const float tilt = lerpf(swarm_cfg::kTiltDark, swarm_cfg::kTiltBright,
                             clampf(_targets[LANE_SOURCE], 0.f, 1.f));

    for (int k = 0; k < n_sw; ++k) {
        const int slot = swarm_cfg::kSubPartials + k;
        const int tone = _chord_n > 0 ? k % _chord_n : 0;
        const int over = k / (_chord_n > 0 ? _chord_n : 1) + 1;
        const float f0 = pitch_to_hz(_chord[tone]);
        const float n = static_cast<float>(over);
        // harmonic -> stretched
        float f = f0 * std::pow(n, 1.f + beta);
        // stretched -> clustered: pull toward a seeded position inside the gap
        // to the next overtone. _spread[] is drawn once, at init/reseed, so a
        // chord change draws nothing (spec section 5).
        f *= 1.f + cluster * swarm_cfg::kClusterSpan * _spread[slot];
        f *= _detune_ratio;          // the deck's swarm-wide DETUNE offset

        float a = std::pow(n, -tilt);
        // Even/odd: BAL negative empties the even overtones (hollow), positive
        // doubles them (organ-full). Not another tilt -- the SOURCE lane owns
        // tilt (spec section 4).
        if (over % 2 == 0) a *= clampf(1.f + _balance, 0.f, 2.f);
        a *= _focus_weight(f);
        // The Nyquist rule (plan open point 1): mute, do not re-allocate --
        // the loop length must not depend on the played note.
        if (f > ceiling) a = 0.f;

        _t_hz[slot] = f;
        _t_amp[slot] = a;
        // Constant-power spread across the swarm, widened by set_width.
        _t_pan[slot] = _width * (-1.f + 2.f * static_cast<float>(k) /
                                 static_cast<float>(n_sw > 1 ? n_sw - 1 : 1));
    }

    _normalize_power();
    _sort_targets();
}
```

`_focus_weight` — plan open point 2, a raised-cosine window on a log axis:

```cpp
// FOCUS: the SIZE target's DISTANCE from 0.5 is how narrow the window is, its
// SIDE is where the window sits. 0.5 is fully open, which is exactly where the
// lane's boot base sits (Part::_base[LANE_SIZE]); the lane's own bipolar
// excursion then sweeps the formant low and high (spec section 4).
float SwarmEngine::_focus_weight(float hz) const {
    const float v = clampf(_targets[LANE_SIZE], 0.f, 1.f);
    const float off = (v - 0.5f) * 2.f;                 // -1 .. +1
    const float narrow = std::fabs(off);
    if (narrow < 1e-3f) return 1.f;                     // fully open, exactly
    const float width_oct = lerpf(swarm_cfg::kFocusWideOct,
                                  swarm_cfg::kFocusNarrowOct, narrow);
    // The window's centre travels from one octave below the root to five above
    // as off goes -1 -> +1.
    const float centre_oct = lerpf(-1.f, 5.f, (off + 1.f) * 0.5f);
    const float oct = std::log2(hz / _root_hz) - centre_oct;
    const float x = clampf(oct / width_oct, -1.f, 1.f);
    // Raised cosine, via fast_sin so the shape matches the rest of the engine.
    return 0.5f + 0.5f * fast_sin(0.25f + x * 0.5f);
}
```

`_normalize_power` divides every amplitude by `sqrt(Σ a²)` and multiplies by 1,
so total power is fixed and TILT/BAL/FOCUS move colour only. `_sort_targets` is
an insertion sort over the three arrays keyed on `_t_hz`; the list is nearly
sorted by construction, so insertion sort is the right one and it is amortised
over the slices anyway.

`_spread[]` is `float _spread[kPartials]`, filled in `init()` and on reseed:

```cpp
    _map_rng.seed(_seed ^ 0x5A0C1A5Du);
    for (int i = 0; i < swarm_cfg::kPartials; ++i)
        _spread[i] = _map_rng.next_bipolar();
```

`_detune_ratio` is `std::pow(2.f, (_detune_n * kDetuneCeilCt) / 1200.f)`, with
`kDetuneCeilCt` read from `SynthEngineT` so a SWARM deck's DETUNE reaches as far
as every other melodic engine's.

`_control_tick()` for now rebuilds every target every tick (Task 7 makes it a
slice) and pushes them:

```cpp
void SwarmEngine::_control_tick() {
    _rebuild_targets();
    for (int i = 0; i < swarm_cfg::kPartials; ++i)
        _bank.set_target(i, _t_hz[i], _t_amp[i], _t_pan[i]);
}
```

and `init()` snaps rather than glides, then `_chord[0] = _targets[LANE_PITCH]`,
`_chord_n = 1` the way `SynthEngineT::init` does.

- [ ] **Step 6: Run the tests, then the suite**

```bash
cmake --build build && ./build/spky_tests -tc="swarm G*"
ctest --test-dir build --output-on-failure
```

Expected: G1, G2, G4–G7, G9–G15 PASS; G3b and G8 still their expected REDs.
`ctrl_identity` and `wave_formant_sweep` unchanged. If either moved, report —
`SwarmEngine` is not on either scenario's path.

Also re-check the `test_deck_bus` SWARM arm: it should now clear the sweep's own
non-silence guard on its own, so any exclusion added in Task 1 Step 6 comes back
out here.

- [ ] **Step 7: Commit**

```bash
git add engine/swarm/swarm_engine.h engine/swarm/swarm_engine.cpp \
        tests/test_swarm_engine.cpp tests/test_deck_bus.cpp
git commit -m "feat(swarm): allocation, the HARM arc, and the four voicing axes

..."
```

---

### Task 5: The bloom — `Env`, FLOOR, the accent, CHOKE

**Files:**
- Modify: `engine/swarm/swarm_engine.h` — `set_flow`, `set_hold`, `set_accent`,
  `trigger_chord` declarations, `_bloom_gain`, the stagger state
- Modify: `engine/swarm/swarm_engine.cpp`
- Modify: `tests/test_swarm_engine.cpp` — append G16–G20, and G3b moves to green

**Interfaces:**
- Consumes: the map (Task 4), `_rise_n`/`_fall_n`/`_floor_n` (Task 1).
- Produces:
  - `void SwarmEngine::set_flow(bool) override`,
    `set_hold(bool) override`, `set_accent(float) override`
  - `float SwarmEngine::floor_now_for_test() const` (`SPKY_TESTING`)

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_swarm_engine.cpp`:

```cpp
namespace {

// A struck bloom's envelope, sampled per control block: enough to see the
// shape without keeping 48 000 floats per case.
std::vector<float> bloom_curve(SwarmEngine& e, int blocks) {
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b) {
        float l = 0.f, r = 0.f;
        for (int i = 0; i < 96; ++i) e.process(l, r);
        out.push_back(e.bloom_level());
    }
    return out;
}

}  // namespace

TEST_CASE("swarm G16: FLOOR is the whole difference between a swell and a drone") {
    // FLOOR 0 in STEP: the bloom decays away and the swarm goes quiet.
    SwarmEngine swell = fresh_swarm();
    swell.set_flow(false);
    swell.set_floor(0.f);
    swell.set_fall(0.f);                 // the shortest FALL, so 20 s is not needed
    feed(swell, 0.5f);
    swell.trigger(0.5f);
    std::vector<float> c = bloom_curve(swell, 600);
    REQUIRE(*std::max_element(c.begin(), c.end()) > 0.5f);
    CHECK(c.back() == 0.f);              // Env snaps idle below -80 dB

    // FLOOR 1: it holds. Same run length, same everything else.
    SwarmEngine drone = fresh_swarm();
    drone.set_flow(false);
    drone.set_floor(1.f);
    drone.set_fall(0.f);
    feed(drone, 0.5f);
    drone.trigger(0.5f);
    std::vector<float> d = bloom_curve(drone, 600);
    CHECK(d.back() > 0.5f);
}

TEST_CASE("swarm G17: FLOW enforces a minimum floor, STEP does not") {
    SwarmEngine e = fresh_swarm();
    e.set_floor(0.f);
    feed(e, 0.5f);
    e.set_flow(false);
    CHECK(e.floor_now_for_test() == doctest::Approx(0.f));
    e.set_flow(true);
    // Derived from the constant, never from its literal.
    CHECK(e.floor_now_for_test() == doctest::Approx(swarm_cfg::kFlowFloorMin));
    // And the drone promise is kept without anyone triggering: entering FLOW
    // with nothing sounding blooms once, the SynthEngineT _auto_pending idiom.
    std::vector<float> c = bloom_curve(e, 200);
    CHECK(*std::max_element(c.begin(), c.end()) > 0.5f);
    CHECK(peak_of(render_l(e, 4800)) > 1e-3f);
}

TEST_CASE("swarm G18: CHOKE decays the drone out and stops re-blooming") {
    SwarmEngine e = fresh_swarm();
    e.set_floor(1.f);
    feed(e, 0.5f);
    e.set_flow(true);
    bloom_curve(e, 200);
    REQUIRE(e.bloom_level() > 0.5f);

    e.set_hold(true);
    std::vector<float> held = bloom_curve(e, 2000);
    CHECK(held.back() < 1e-3f);          // decayed out, at the FALL rate
    // Nothing re-armed it while held -- a re-bloom would show as a rise.
    for (size_t i = 1; i < held.size(); ++i) CHECK(held[i] <= held[i - 1] + 1e-6f);

    e.set_hold(false);
    std::vector<float> back = bloom_curve(e, 400);
    CHECK(*std::max_element(back.begin(), back.end()) > 0.5f);
}

TEST_CASE("swarm G19: the accent scales the bloom height and leaves the floor alone") {
    auto peak_at = [](float accent) {
        SwarmEngine e = fresh_swarm();
        e.set_flow(false);
        e.set_floor(0.f);
        feed(e, 0.5f);
        e.set_accent(accent);
        e.trigger(0.5f);
        return peak_of(render_l(e, 48000));
    };
    const float loud = peak_at(0.f);
    const float soft = peak_at(1.f);
    REQUIRE(loud > 1e-3f);
    CHECK(soft / loud
          == doctest::Approx(swarm_cfg::kAccentVelFloor).epsilon(0.08));

    // At FLOOR 1 there is nothing to bloom into, so the accent is inert --
    // and that is a claim about the formula, not a gap in it.
    auto floor_peak_at = [](float accent) {
        SwarmEngine e = fresh_swarm();
        e.set_flow(false);
        e.set_floor(1.f);
        feed(e, 0.5f);
        e.set_accent(accent);
        e.trigger(0.5f);
        std::vector<float> b = render_l(e, 48000);
        // The tail, not the peak: the floor is what is left at the end.
        return peak_of(std::vector<float>(b.end() - 4800, b.end()));
    };
    CHECK(floor_peak_at(1.f) == doctest::Approx(floor_peak_at(0.f)).epsilon(0.05));
}

TEST_CASE("swarm G20: the accent shortens FALL only when DEC has room") {
    auto len_at = [](float fall_knob, float accent) {
        SwarmEngine e = fresh_swarm();
        e.set_flow(false);
        e.set_floor(0.f);
        e.set_fall(fall_knob);
        feed(e, 0.5f);
        e.set_accent(accent);
        e.trigger(0.5f);
        std::vector<float> c = bloom_curve(e, 4000);
        const float pk = *std::max_element(c.begin(), c.end());
        if (pk <= 1e-3f) return -1;
        int last = 0;
        for (size_t i = 0; i < c.size(); ++i) if (c[i] > pk * 0.05f) last = static_cast<int>(i);
        return last >= static_cast<int>(c.size()) - 2 ? -1 : last;
    };
    // Half one: at FALL 0 there is no room to take away.
    const int flat_loud = len_at(0.f, 0.f);
    const int flat_soft = len_at(0.f, 1.f);
    REQUIRE(flat_loud > 0);
    CHECK(flat_loud == flat_soft);
    // Half two: at FALL up, the accented note rings measurably shorter.
    const int full = len_at(0.7f, 0.f);
    const int cut = len_at(0.7f, 1.f);
    REQUIRE(full > 0);
    REQUIRE(cut > 0);
    CHECK(cut < full);
}
```

Delete the `[[expected-red]]` note from G3b in the task report: it goes green in
this task.

- [ ] **Step 2: Run to see the REDs**

```bash
cmake --build build && ./build/spky_tests -tc="swarm G1[6-9]" -tc="swarm G20"
```

Expected: **G16 fails** on `REQUIRE(*max > 0.5f)` — `trigger` is still empty, so
no bloom exists; **G17 fails** on `floor_now_for_test` not compiling until Step 3
adds it, then on the FLOW value; **G18 fails** on `REQUIRE(bloom_level > 0.5f)`;
**G19 and G20 fail** on their `REQUIRE(loud > 1e-3f)` / `REQUIRE(flat_loud > 0)`
non-vacuity guards. Copy the lines.

- [ ] **Step 3: Wire the bloom**

`engine/swarm/swarm_engine.h`:

```cpp
    void set_flow(bool flow) override;
    void set_hold(bool on) override;
    void set_accent(float a) override;
    void trigger_chord(const float* pitches_norm, int n) override;
    void set_chord(const float* pitches_norm, int n) override;
    void set_width(float n) override { _width = clampf(n, 0.f, 1.f); }
```
```cpp
    bool  _flow = false;
    bool  _hold = false;
    bool  _auto_pending = false;
    float _accent = 0.f;
    float _width = 0.f;
    // Per-partial bloom stagger, in samples, low partial first: what makes it
    // bloom rather than switch (spec section 3). One Env for the whole swarm,
    // read at an offset per partial.
    float _stagger_gain[swarm_cfg::kPartials] = {};
```

`engine/swarm/swarm_engine.cpp`:

```cpp
// FLOW's floor is the knob, raised to kFlowFloorMin: the drone promise has to
// hold at FLOOR 0 (spec section 4). CHOKE takes it to 0, which is exactly
// Env's documented demotion -- "setting sustain to 0 while holding IS the
// release" (env.h) -- so no separate release path exists to get wrong.
float SwarmEngine::_floor_now() const {
    if (_hold) return 0.f;
    return _flow ? std::max(_floor_n, swarm_cfg::kFlowFloorMin) : _floor_n;
}

void SwarmEngine::set_flow(bool flow) {
    if (flow == _flow) return;
    _flow = flow;
    _set_floor_now();
    if (flow && !_hold && !_bloom.active()) _auto_pending = true;
}

void SwarmEngine::set_hold(bool on) {
    if (on == _hold) return;
    _hold = on;
    _set_floor_now();
    if (on) _auto_pending = false;
    else if (_flow) _auto_pending = true;
}

void SwarmEngine::set_accent(float a) { _accent = clampf(a, 0.f, 1.f); }

// One bloom, whatever the chord size -- see Task 6 for trigger_chord. trigger()
// is the single-note door onto the same thing.
void SwarmEngine::trigger(float pitch_norm) {
    _chord[0] = pitch_norm;
    _chord_n = 1;
    _do_bloom();
}

void SwarmEngine::_do_bloom() {
    // RISE and FALL are absolute seconds, deliberately long at the top (spec
    // section 4) -- not cycle ratios like SynthEngineT's, because a drone's
    // swell has nothing to do with the transport's tempo.
    const float rise = lerpf(swarm_cfg::kRiseMinS, swarm_cfg::kRiseMaxS, _rise_n);
    // The accent's second half, the SynthEngineT shape: the room it has to
    // shorten the tail is the room the FALL knob dialled in, so at FALL 0 the
    // term vanishes and the envelope is untouchable.
    const float fall = lerpf(swarm_cfg::kFallMinS, swarm_cfg::kFallMaxS, _fall_n)
        * (1.f - (1.f - swarm_cfg::kAccentDecFloor) * _accent * _fall_n);
    _bloom.set_times(rise, fall);
    _set_floor_now();
    _bloom.trigger();                  // rises from the CURRENT level: click-free
    ++_bloom_count;                    // observed by G21/G22, nothing else
    // The accent's first half, on bloom HEIGHT. Spent in process() as
    // floor + (env - floor) * gain, so the floor itself never ducks -- which
    // is why the accent is inert at FLOOR 1 and G19's second half says so.
    _bloom_gain = 1.f - (1.f - swarm_cfg::kAccentVelFloor) * _accent;
}
```

and in `process()`, between the bank and the level:

```cpp
    if (_auto_pending) { _auto_pending = false; _do_bloom(); }
    const float env = _bloom.process();
    // floor + (env - floor) * gain: the accent scales the SWELL and leaves the
    // floor where it is, which is what makes it inert at FLOOR 1 (G19).
    const float bloom = _floor_cached + (env - _floor_cached) * _bloom_gain;
```
with `bloom` folded into `g`.

`Env` has no sustain getter and **stays unmodified** (spec §7, settled), so keep
the floor in a member of this engine instead:

```cpp
    // The floor Env was last told about. Held here rather than read back from
    // Env, because Env is reused UNMODIFIED (spec section 7) and giving it a
    // getter would be the smallest possible way to break that. Every write to
    // _bloom.set_sustain() must write this too -- there are exactly three
    // (set_flow, set_hold, _do_bloom), and _set_floor_now() is the only one of
    // them that touches either.
    float _floor_cached = 0.f;
    float _bloom_gain = 1.f;
    int   _bloom_count = 0;
```
```cpp
void SwarmEngine::_set_floor_now() {
    _floor_cached = _floor_now();
    _bloom.set_sustain(_floor_cached);
}
```
with `float floor_now_for_test() const { return _floor_cached; }` in the
`SPKY_TESTING` block, and `set_flow`/`set_hold`/`_do_bloom` calling
`_set_floor_now()` rather than `_bloom.set_sustain()` directly.

The stagger: `_stagger_gain[i]` is a 0..1 ramp over `kBloomStaggerS`, applied by
delaying each partial's share of `bloom`. Implement it at control rate as a
per-partial extra multiplier on the amplitude target — cheap, and it needs no
per-partial envelope:

```cpp
// The bloom reaches the low partials first (spec section 3). At control rate
// this is a per-partial gain that lags the shared envelope by up to
// kBloomStaggerS -- so a bloom SWELLS across the spectrum, and a bank at rest
// pays nothing for it (every gain is 1).
const float lag = swarm_cfg::kBloomStaggerS * _sr;   // samples, low -> high
```
with the lagged value taken from a small ring of the last `ceil(lag / 96) + 1`
envelope samples, indexed per partial. Size the ring from the constant, not from
a literal.

- [ ] **Step 4: Run the tests, then the suite**

```bash
cmake --build build && ./build/spky_tests -tc="swarm G*" && ctest --test-dir build --output-on-failure
```

Expected: every gate but G8 PASS, including G3b. Watch
`tests/test_part_engine_contract.cpp` and `tests/test_choke.cpp` — the first
because `IPartEngine` gained three more overrides on one engine, the second
because CHOKE now reaches a sixth engine.

- [ ] **Step 5: Commit**

```bash
git add engine/swarm/swarm_engine.h engine/swarm/swarm_engine.cpp \
        engine/swarm/swarm_config.h tests/test_swarm_engine.cpp
git commit -m "feat(swarm): the bloom -- Env unmodified, FLOOR, accent, CHOKE

..."
```

---

### Task 6: The chord — one bloom, and a glissando instead of a retrigger

**Files:**
- Modify: `engine/swarm/swarm_engine.cpp` — `trigger_chord`, `set_chord`
- Modify: `tests/test_swarm_engine.cpp` — append G21–G24

**Interfaces:**
- Consumes: `_rebuild_targets` (Task 4), `_do_bloom` (Task 5).
- Produces: no new symbols. The behaviour: `set_chord` retargets and never
  blooms; `trigger_chord` blooms exactly once whatever `n` is.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("swarm G21: trigger_chord fires ONE bloom, not n") {
    // IPartEngine's default calls trigger() n times, which on this engine
    // would be n blooms per chord (spec section 5). The count is observable
    // because a second trigger() re-arms the attack: the envelope would show
    // a second rise inside the same block.
    SwarmEngine e = fresh_swarm();
    e.set_flow(false);
    e.set_floor(0.f);
    e.set_rise(0.6f);                     // a slow rise, so a re-arm is visible
    feed(e, 0.5f);
    const float chord[4] = { 0.35f, 0.45f, 0.55f, 0.65f };
    e.set_chord(chord, 4);
    CHECK(e.bloom_count_for_test() == 0);  // set_chord never blooms
    e.trigger_chord(chord, 4);
    CHECK(e.bloom_count_for_test() == 1);
    e.trigger_chord(chord, 1);
    CHECK(e.bloom_count_for_test() == 2);  // and one per call, not per note
    // All four pitches reached the allocation, not just the first.
    settle(e);
    CHECK(e.chord_n_for_test() == 4);
}

TEST_CASE("swarm G22: a chord change re-voices without retriggering") {
    SwarmEngine e = fresh_swarm();
    e.set_flow(true);
    e.set_floor(0.5f);
    feed(e, 0.5f);
    bloom_curve(e, 200);
    const int blooms = e.bloom_count_for_test();
    REQUIRE(blooms > 0);
    const float before = e.bloom_level();
    const float chord[3] = { 0.5f, 0.58f, 0.66f };
    e.set_chord(chord, 3);
    settle(e);
    CHECK(e.bloom_count_for_test() == blooms);           // no retrigger
    CHECK(e.bloom_level() == doctest::Approx(before).epsilon(0.05));
    CHECK(e.chord_n_for_test() == 3);
}

TEST_CASE("swarm G23: retargeting is click-free") {
    SwarmEngine e = fresh_swarm();
    e.set_flow(true);
    e.set_floor(1.f);                     // a steady drone: no envelope motion
    feed(e, 0.5f);
    settle(e);
    // The reference, measured in this same run: the largest sample step the
    // settled drone takes at all. No literal threshold anywhere.
    std::vector<float> steady = render_l(e, 9600);
    float step_steady = 0.f;
    for (size_t i = 1; i < steady.size(); ++i)
        step_steady = std::max(step_steady, std::fabs(steady[i] - steady[i - 1]));
    REQUIRE(step_steady > 0.f);

    const float chord[4] = { 0.5f, 0.6f, 0.7f, 0.8f };
    e.set_chord(chord, 4);
    std::vector<float> moving = render_l(e, 96 * swarm_cfg::kRetargetPeriod * 8);
    float step_moving = std::fabs(moving[0] - steady.back());
    for (size_t i = 1; i < moving.size(); ++i)
        step_moving = std::max(step_moving, std::fabs(moving[i] - moving[i - 1]));
    // A glide up the spectrum legitimately raises the per-sample step, because
    // the partials are higher afterwards. The bound is measured, not chosen:
    // run the settled drone at the NEW chord and take its own steady step.
    SwarmEngine after = fresh_swarm();
    after.set_flow(true);
    after.set_floor(1.f);
    feed(after, 0.5f);
    after.set_chord(chord, 4);
    settle(after);
    std::vector<float> steady2 = render_l(after, 9600);
    float step_steady2 = 0.f;
    for (size_t i = 1; i < steady2.size(); ++i)
        step_steady2 = std::max(step_steady2, std::fabs(steady2[i] - steady2[i - 1]));
    CAPTURE(step_steady);
    CAPTURE(step_steady2);
    CAPTURE(step_moving);
    CHECK(step_moving <= 1.5f * std::max(step_steady, step_steady2));
}

TEST_CASE("swarm G24: a chord change that keeps the root moves less than one that does not") {
    // The nearest-neighbour claim of spec section 5, expressed as the thing it
    // actually is: the target list is generated sorted, so slot i keeps target
    // i, and a chord sharing tones barely reorders the list.
    auto travel = [](const float* to, int n) {
        SwarmEngine e = fresh_swarm();
        e.set_flow(true);
        e.set_floor(1.f);
        feed(e, 0.5f);
        settle(e);
        std::vector<float> before = hz_of(e);
        e.set_chord(to, n);
        settle(e);
        std::vector<float> after = hz_of(e);
        float sum = 0.f;
        for (size_t i = 0; i < before.size(); ++i)
            sum += std::fabs(std::log2(std::max(after[i], 1e-6f) /
                                       std::max(before[i], 1e-6f)));
        return sum;   // total travel, in octaves summed over the bank
    };
    const float keeps_root[3] = { 0.5f, 0.58f, 0.66f };
    const float new_root[3]   = { 0.7f, 0.78f, 0.86f };
    const float t_keep = travel(keeps_root, 3);
    const float t_move = travel(new_root, 3);
    CAPTURE(t_keep);
    CAPTURE(t_move);
    REQUIRE(t_move > 0.f);
    CHECK(t_keep < 0.5f * t_move);
}
```

`bloom_count_for_test()` and `chord_n_for_test()` are two more `SPKY_TESTING`
observers; `_bloom_count` increments in `_do_bloom()`.

- [ ] **Step 2: Run to see the REDs**

```bash
cmake --build build && ./build/spky_tests -tc="swarm G2[1-4]"
```

Expected: compile error first (`bloom_count_for_test`), then — with the two
observers added but `trigger_chord`/`set_chord` still IPartEngine's defaults —
**G21 fails** with `bloom_count_for_test() == 4` against 1 (the default fires
`trigger` per note, which is the exact defect the case is named for), and
**G22 and G24 fail** on `chord_n_for_test()` / the travel comparison because
`set_chord`'s default is a no-op and the allocation never sees the chord. G23 may
pass vacuously at this point (nothing moves) — **say so in the report**: G23
alone is not proof of this task.

- [ ] **Step 3: Override the two**

```cpp
// One bloom, whatever the chord size. IPartEngine's default calls trigger() n
// times, which on a retuned bank would mean n blooms and n re-armed attacks per
// chord (spec section 5). SWARM allocates all n pitches and blooms once.
void SwarmEngine::trigger_chord(const float* p, int n) {
    if (n < 1) return;
    if (n > ChordBuilder::kMaxNotes) n = ChordBuilder::kMaxNotes;
    for (int i = 0; i < n; ++i) _chord[i] = p[i];
    _chord_n = n;
    _do_bloom();
}

// Live re-voicing. The surface arrives once per control tick from
// Part::_control_tick, so this only records it; the retarget happens in
// _control_tick where it is already amortised, and NOTHING here draws from an
// Rng -- the cluster spread was drawn at init/reseed precisely so a COLOR move
// is a glissando and not a new instrument (spec section 5).
void SwarmEngine::set_chord(const float* p, int n) {
    if (n < 1) return;
    if (n > ChordBuilder::kMaxNotes) n = ChordBuilder::kMaxNotes;
    for (int i = 0; i < n; ++i) _chord[i] = p[i];
    _chord_n = n;
}
```

- [ ] **Step 4: Run the tests and the suite**

```bash
cmake --build build && ./build/spky_tests -tc="swarm G*" && ctest --test-dir build --output-on-failure
```

Expected: everything but G8 green. If G23's bound is exceeded, **do not widen
it** — check first whether the glide is arriving in one period (that would mean
`kApproachFrac` is being applied twice, or `set_target` is being called every
tick instead of every period, which Task 7 is about).

- [ ] **Step 5: Commit**

```bash
git add engine/swarm/swarm_engine.h engine/swarm/swarm_engine.cpp tests/test_swarm_engine.cpp
git commit -m "feat(swarm): one bloom per chord, and a glissando on a COLOR move

..."
```

---

### Task 7: Amortized retargeting

**Files:**
- Modify: `engine/swarm/swarm_engine.h` — `_slice` cursor
- Modify: `engine/swarm/swarm_engine.cpp` — `_control_tick`
- Modify: `tests/test_swarm_engine.cpp` — append G25, G26

**Interfaces:**
- Consumes: `_rebuild_targets`, `_control_tick` (Tasks 4, 6).
- Produces: the guarantee every earlier gate's `settle()` already assumes — each
  partial is retargeted exactly once per `kRetargetPeriod` ticks, in round-robin
  order, and no partial is ever starved.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("swarm G25: every partial is retargeted exactly once per period") {
    SwarmEngine e = fresh_swarm();
    feed(e, 0.5f);
    e.set_flow(true);
    settle(e);
    // Move the root, then count how many ticks each partial takes to start
    // moving. The answer must be < kRetargetPeriod for every one of them: a
    // starved partial is a partial left on the previous chord forever.
    std::vector<float> before = hz_of(e);
    feed(e, 0.8f);
    std::vector<bool> moved(swarm_cfg::kPartials, false);
    float l = 0.f, r = 0.f;
    for (int tick = 0; tick < swarm_cfg::kRetargetPeriod; ++tick) {
        for (int i = 0; i < 96; ++i) e.process(l, r);
        for (int p = 0; p < swarm_cfg::kPartials; ++p)
            if (std::fabs(e.partial_hz_for_test(p) - before[p]) > 1e-4f) moved[p] = true;
    }
    for (int p = 0; p < swarm_cfg::kPartials; ++p) {
        CAPTURE(p);
        // A muted partial has nothing to move; the amplitude is what moved for
        // it, so check either.
        CHECK((moved[p] || e.partial_amp_for_test(p) < 1e-5f));
    }
    // And the spike is flattened: no tick retargets more than the slice.
    CHECK(e.retargets_last_tick_for_test() <= swarm_cfg::kRetargetSlice);
}

TEST_CASE("swarm G26: amortizing does not change where the spectrum settles") {
    // The whole risk of lever 3 is that a partially-updated bank settles
    // somewhere other than the map says. It must not: the map is a pure
    // function of the state, so a slice is a delay, not a difference.
    SwarmEngine e = fresh_swarm(31337u);
    e.set_harm(0.4f);
    feed(e, 0.6f, 0.3f, 0.7f);
    e.set_flow(true);
    settle(e);
    for (int i = 0; i < swarm_cfg::kPartials; ++i) {
        CAPTURE(i);
        // The bank arrived at the map's own target, to the geometric
        // approach's residual after settle()'s 40 periods.
        CHECK(e.partial_hz_for_test(i)
              == doctest::Approx(e.target_hz_for_test(i)).epsilon(0.001));
    }
}
```

- [ ] **Step 2: Run to see the RED**

```bash
cmake --build build && ./build/spky_tests -tc="swarm G25" -tc="swarm G26"
```

Expected: compile error on `retargets_last_tick_for_test` /
`target_hz_for_test`; with those added as observers but `_control_tick` still
retargeting the whole bank, **G25 fails** on
`CHECK(e.retargets_last_tick_for_test() <= kRetargetSlice)`, reading `kPartials`.
G26 passes before and after — it is the *regression* half of this task, and
saying so is what stops it being mistaken for the proof.

- [ ] **Step 3: Amortize**

```cpp
void SwarmEngine::_control_tick() {
    // The map is cheap to evaluate per partial and expensive over the whole
    // bank (one std::pow per partial for the overtone warp, one for the tilt),
    // so recompute the WHOLE target list only when something it depends on
    // moved, and push a SLICE of it per tick (spec section 7, lever 3). The
    // budget gate measures the WORST block, so flattening this spike buys
    // budget directly.
    _rebuild_targets();
    const int base = _slice * swarm_cfg::kRetargetSlice;
    _retargets_last_tick = 0;
    for (int k = 0; k < swarm_cfg::kRetargetSlice; ++k) {
        const int i = base + k;
        if (i >= swarm_cfg::kPartials) break;
        _advance_drift(i);                     // Task 8 hooks in here
        _bank.set_target(i, _t_hz[i], _t_amp[i], _t_pan[i]);
        ++_retargets_last_tick;
    }
    if (++_slice >= swarm_cfg::kRetargetPeriod) _slice = 0;
}
```

If the bench (Task 3) showed `_rebuild_targets` itself to be the spike rather
than the pushes, split it the same way — compute only slots
`base .. base + kRetargetSlice` — and say in the commit message which of the two
the measurement pointed at. **Do not split it on suspicion**: an unmeasured
optimization here would be the kind of claim this project's probe rule exists to
stop.

- [ ] **Step 4: Run, then commit**

```bash
cmake --build build && ./build/spky_tests -tc="swarm G*" && ctest --test-dir build --output-on-failure
```

```bash
git add engine/swarm/swarm_engine.h engine/swarm/swarm_engine.cpp tests/test_swarm_engine.cpp
git commit -m "perf(swarm): retarget a slice per control tick

..."
```

---

### Task 8: Drift, NEW, and determinism

**Files:**
- Modify: `engine/swarm/swarm_engine.h` — the drift state, `reseed()`
- Modify: `engine/swarm/swarm_engine.cpp` — `_advance_drift`, `reseed`
- Modify: `engine/parts/part.h` / `engine/parts/part.cpp` — `Part::new_phrase()`
- Modify: `engine/instrument.h` — `new_phrase(int)` routes through `Part`
- Modify: `tests/test_swarm_engine.cpp` — append G27–G29

**Interfaces:**
- Consumes: `_control_tick`'s per-partial slice (Task 7).
- Produces:
  - `void SwarmEngine::reseed(uint32_t s)` — redraws the cluster map and every
    drift stream from one base seed
  - `void Part::new_phrase()` — `_mod.new_phrase()` plus, on a SWARM deck, a
    reseed. `Instrument::new_phrase(p)` now calls this instead of reaching into
    `mod()` directly.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("swarm G27: DRIFT 0 is exactly static, DRIFT 1 moves") {
    // "Motion below the lane timescale" is the engine's reason to exist
    // (spec section 1), so both directions are asserted -- a gate on the
    // moving half alone passes on a drift nothing can switch off.
    SwarmEngine still = fresh_swarm();
    feed(still, 0.5f, 0.5f, 0.5f, /*drift=*/0.f);
    still.set_flow(true);
    settle(still);
    std::vector<float> a = hz_of(still);
    for (int i = 0; i < 48000 * 10; ++i) { float l, r; still.process(l, r); }
    std::vector<float> b = hz_of(still);
    for (size_t i = 0; i < a.size(); ++i) {
        CAPTURE(i);
        REQUIRE(b[i] == a[i]);       // bit-identical: depth multiplies by zero
    }

    SwarmEngine alive = fresh_swarm();
    feed(alive, 0.5f, 0.5f, 0.5f, /*drift=*/1.f);
    alive.set_flow(true);
    settle(alive);
    std::vector<float> c = hz_of(alive);
    for (int i = 0; i < 48000 * 10; ++i) { float l, r; alive.process(l, r); }
    std::vector<float> d = hz_of(alive);
    float worst_cents = 0.f;
    for (size_t i = 0; i < c.size(); ++i)
        if (c[i] > 1.f)
            worst_cents = std::max(worst_cents,
                std::fabs(1200.f * std::log2(d[i] / c[i])));
    CAPTURE(worst_cents);
    // Sized against the constant, not a literal: the walk must actually use a
    // usable part of the reach it was given, or DRIFT is a knob that does
    // nothing audible.
    CHECK(worst_cents > 0.2f * swarm_cfg::kDriftCentsMax);
    CHECK(worst_cents <= 1.05f * swarm_cfg::kDriftCentsMax);   // and stays inside it
}

TEST_CASE("swarm G28: same seed and same knobs give bit-identical audio") {
    auto run = [](uint32_t seed) {
        SwarmEngine e = fresh_swarm(seed);
        e.set_harm(0.75f);
        feed(e, 0.5f, 0.3f, 0.4f, 0.8f);
        e.set_flow(true);
        return render_l(e, 48000 * 2);
    };
    std::vector<float> a = run(4242u);
    std::vector<float> b = run(4242u);
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) REQUIRE(a[i] == b[i]);
    // Different seed, different individual -- otherwise the seed is dead state
    // and G29 below could not tell anything.
    std::vector<float> c = run(999u);
    bool differs = false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != c[i]) differs = true;
    CHECK(differs);
}

TEST_CASE("swarm G29: NEW gives the deck a new individual, and only NEW does") {
    SwarmEngine e = fresh_swarm(4242u);
    e.set_harm(1.f);                    // deep in the cluster zone: the metal
    feed(e, 0.5f);
    e.set_flow(true);
    settle(e);
    std::vector<float> before = hz_of(e);

    // Everything short of NEW leaves the metal alone: re-pushing the same
    // knobs, moving the chord, blooming again.
    feed(e, 0.5f);
    const float chord[2] = { 0.5f, 0.6f };
    e.trigger_chord(chord, 2);
    e.set_chord(chord, 2);
    settle(e);
    // Restore the single-note surface and compare like with like.
    const float one[1] = { 0.5f };
    e.set_chord(one, 1);
    settle(e);
    std::vector<float> unchanged = hz_of(e);
    for (size_t i = 0; i < before.size(); ++i) {
        CAPTURE(i);
        CHECK(unchanged[i] == doctest::Approx(before[i]).epsilon(0.001));
    }

    e.reseed(777u);
    settle(e);
    std::vector<float> after = hz_of(e);
    int moved = 0;
    for (size_t i = swarm_cfg::kSubPartials; i < after.size(); ++i)
        if (std::fabs(after[i] - before[i]) > before[i] * 0.005f) ++moved;
    CHECK(moved > 4);
}

TEST_CASE("swarm G29b: NEW on a SWARM deck reaches the swarm") {
    // Instrument::new_phrase reached only mod() until this task, so the seed
    // redraw had no route in at all.
    Part p;
    p.init(48000.f, 5u);
    float l = 0.f, r = 0.f;
    p.set_engine(ENGINE_SWARM);
    for (int i = 0; i < 500; ++i) p.process(l, r);
    REQUIRE(p.engine_id() == ENGINE_SWARM);
    for (int i = 0; i < 96 * swarm_cfg::kRetargetPeriod * 40; ++i) p.process(l, r);
    std::vector<float> before;
    for (int i = 0; i < swarm_cfg::kPartials; ++i)
        before.push_back(p.swarm().partial_hz_for_test(i));
    p.new_phrase();
    for (int i = 0; i < 96 * swarm_cfg::kRetargetPeriod * 40; ++i) p.process(l, r);
    int moved = 0;
    for (int i = swarm_cfg::kSubPartials; i < swarm_cfg::kPartials; ++i)
        if (std::fabs(p.swarm().partial_hz_for_test(i) - before[i]) > before[i] * 0.005f)
            ++moved;
    CHECK(moved > 0);
}
```

- [ ] **Step 2: Run to see the REDs**

```bash
cmake --build build && ./build/spky_tests -tc="swarm G2[7-9]*"
```

Expected: compile error on `reseed` / `Part::new_phrase`; then **G27's moving
half fails** on `worst_cents > 0.2 * kDriftCentsMax` reading 0 (nothing drifts),
**G28 passes** (a static engine is trivially deterministic — note in the report
that G28 alone proves nothing until drift exists, which is exactly the "identity
on an engine that runs silent" shape from
memory `fireflow-tests-must-be-able-to-fail`), **G29 fails** on `moved > 4`, and
**G29b fails** on `moved > 0`.

- [ ] **Step 3: Add the drift and the reseed**

`engine/swarm/swarm_engine.h`:

```cpp
    // One Rng stream per partial, all derived from one base seed, so NEW is a
    // single redraw (spec section 7). Two independent walks per partial:
    // detune and amplitude undulation. Advanced only on the partial's own
    // retarget slice, so the drift is amortised with everything else.
    Rng   _drift_rng[swarm_cfg::kPartials];
    float _det_walk[swarm_cfg::kPartials] = {};
    float _amp_walk[swarm_cfg::kPartials] = {};
    Rng   _map_rng;
    float _spread[swarm_cfg::kPartials] = {};
```
```cpp
    // NEW: a fresh individual for this deck (spec section 5). Determinism is
    // untouched -- "same knob position, same metal" is a claim about a GIVEN
    // seed, and NEW is an explicit user gesture, the way it spawns a grain on
    // the sampler.
    void reseed(uint32_t s);
```

`engine/swarm/swarm_engine.cpp`:

```cpp
void SwarmEngine::reseed(uint32_t s) {
    _seed = s;
    _map_rng.seed(s ^ 0x5A0C1A5Du);
    for (int i = 0; i < swarm_cfg::kPartials; ++i) {
        _spread[i] = _map_rng.next_bipolar();
        // Per-partial stream, derived from the base seed rather than seeded
        // independently: one number gives the deck its whole individuality.
        _drift_rng[i].seed(s ^ (0x9E3779B9u * static_cast<uint32_t>(i + 1)));
        _det_walk[i] = 0.f;
        _amp_walk[i] = 0.f;
    }
    // The walks reset to centre, so a reseed is a glide toward a new metal
    // rather than a jump through an arbitrary drift position. It is NOT
    // deferred to a phrase boundary (plan open point 3): every target change
    // glides, so there is nothing left for a deferral to protect.
}

// One partial's drift, advanced on its own retarget slice. A bounded random
// walk: a step from the partial's own stream, pulled back toward centre, so it
// wanders without escaping. Rate has no control of its own and rides gently on
// depth (spec section 3) -- deeper is up to kDriftRateDepthBoost faster.
void SwarmEngine::_advance_drift(int i) {
    const float depth = clampf(_targets[LANE_MOTION], 0.f, 1.f);
    const float step = swarm_cfg::kDriftWalkStep *
        (1.f + swarm_cfg::kDriftRateDepthBoost * depth);
    _det_walk[i] += step * _drift_rng[i].next_bipolar()
                  - swarm_cfg::kDriftPull * _det_walk[i];
    _amp_walk[i] += step * _drift_rng[i].next_bipolar()
                  - swarm_cfg::kDriftPull * _amp_walk[i];
    _det_walk[i] = clampf(_det_walk[i], -1.f, 1.f);
    _amp_walk[i] = clampf(_amp_walk[i], -1.f, 1.f);
    // Depth multiplies at the END, so DRIFT 0 is exactly static (G27) while
    // the walk itself stays free-running -- which is what "free-running on
    // purpose" means: turning DRIFT up does not restart the motion.
    const float cents = _det_walk[i] * swarm_cfg::kDriftCentsMax * depth;
    _t_hz[i] *= std::pow(2.f, cents * (1.f / 1200.f));
    _t_amp[i] *= 1.f + _amp_walk[i] * swarm_cfg::kDriftAmpMax * depth;
}
```

`init()` calls `reseed(_seed)`.

`engine/parts/part.h` — the declaration next to `trigger_manual()`:

```cpp
    // NEW (spec 2026-08-17 section 5): a fresh phrase for the lane, and on a
    // SWARM deck a fresh swarm as well. Routed through Part rather than
    // straight at mod() so the gesture can reach the engine at all --
    // Instrument::new_phrase used to call _parts[p].mod().new_phrase()
    // directly, which no engine could see.
    void new_phrase();
```

`engine/parts/part.cpp`:

```cpp
void Part::new_phrase() {
    _mod.new_phrase();
    // Immediate, not pending: the lane's own redraw waits for a cycle wrap
    // (ModLane::new_phrase), but a swarm has no wrap to wait for and every
    // target change is a glide anyway.
    if (_engine_id == ENGINE_SWARM) _swarm.reseed(_new_rng.next_u32());
}
```
with `Rng _new_rng;` private in `part.h` and `_new_rng.seed(seed_base ^
0x5EEDBEEFu);` in `Part::init` — so repeated NEW presses walk a **deterministic**
sequence of individuals rather than reusing one value, and two decks with
different `seed_base` walk different ones. Seeding it in `init()` also means a
VCV sample-rate reinit restarts the sequence, which matches how `_synth`'s own
seed is handled two lines above.

`engine/instrument.h:99`:

```cpp
    void new_phrase(int p)                   { _parts[p].new_phrase(); }
```

- [ ] **Step 4: Run, and check G27's upper bound with a probe if it fails**

```bash
cmake --build build && ./build/spky_tests -tc="swarm G*"
```

Expected: all but G8 PASS. **If G27's `worst_cents > 0.2 * kDriftCentsMax` misses**,
do not lower the fraction: write a scratchpad probe that prints `worst_cents`
over 10 s at depth 1 for ten seeds, and use it to decide whether
`kDriftWalkStep`/`kDriftPull` give the walk enough of its reach. That is a
voicing constant and the answer belongs in Task 12's listening notes — but the
*measurement* belongs in the task report either way.

- [ ] **Step 5: Run the suite and commit**

```bash
ctest --test-dir build --output-on-failure
```

Watch `tests/test_new_phrase.cpp`, `tests/test_center.cpp` and
`tests/test_song_lane.cpp` — all three call `new_phrase()` on a lane or through
`Instrument`, and the route changed.

```bash
git add engine/swarm/swarm_engine.h engine/swarm/swarm_engine.cpp \
        engine/parts/part.h engine/parts/part.cpp engine/instrument.h \
        tests/test_swarm_engine.cpp
git commit -m "feat(swarm): per-partial drift, and NEW redraws the individual

..."
```

---

### Task 9: The hosts — a listening render and the sixth ENG state

**Files:**
- Modify: `host/render/scenario.cpp` — `parse_engine`
- Create: `host/render/scenarios/swarm_drone.json`
- Modify: `tests/test_scenario.cpp` — the spelling case
- Modify: `host/vcv/res/gen_panel.py` — `DYNAMIC_CAPTIONS`, the `words[]` arity,
  the ENG comment
- Modify: `host/vcv/res/test_panel.py` — `driver_states`, the arity guards, the
  header-string check
- Modify: `host/vcv/src/Fireflow.cpp` — the `configSwitch` labels,
  `kEngineShades`, the ENG remap, the SOURCE tooltip
- Modify: `host/vcv/src/generated_panel.hpp` — regenerated, never hand-edited

**Interfaces:**
- Consumes: `ENGINE_SWARM` (Task 1) and everything the engine does (Tasks 4–8).
- Produces: `"swarm"` as a scenario engine spelling; ENG state 5 in the VCV host.

- [ ] **Step 1: Write the failing tests**

`tests/test_scenario.cpp`, beside the existing BODY/BBD spelling cases:

```cpp
TEST_CASE("scenario: swarm engine spelling selects ENGINE_SWARM") {
    // The same shape as the body/bbd cases above it: an unknown spelling falls
    // back to SYNTH, so a typo in this table is invisible without a case per
    // spelling.
    auto inst = std::make_unique<Instrument>();
    Scenario s = parse_scenario_string(R"({
      "sample_rate": 48000, "duration_s": 0.1,
      "init": [ {"action":"set_engine","part":0,"value":"swarm"} ],
      "events": []
    })");
    apply_init(*inst, s);
    for (int i = 0; i < 1000; ++i) { float l, r; inst->process(nullptr, nullptr, &l, &r, 1); }
    CHECK(inst->engine_id(0) == ENGINE_SWARM);
}
```

(Match the exact helper names the neighbouring cases in that file use — they are
the authority on the harness, not this snippet.)

`host/vcv/res/test_panel.py` — three edits, and each one is a guard that would
otherwise silently stop covering the new state:

```python
    driver_states = {"ENGINE": 6}
```
```python
        check(len(words) <= 6,
              f"{target}: {len(words)} words exceeds the header's word[6]")
```
and the generated-header assertion:

```python
    check("const char* words[6]; };" in h,
```

Run the panel guards before touching the generator, so the RED is visible:

```bash
python host/vcv/res/test_panel.py
```

Expected: FAIL — `MELODY: 5 words for a ENGINE driver`, once per row, plus the
header-string check. **Delete `host/vcv/res/__pycache__` first**: `.pyc`
invalidation keys on mtime *and size*, so a size-neutral edit inside one mtime
tick silently re-runs the old module and shows a false green
(memory `fireflow-vacuous-test-gates`).

- [ ] **Step 2: Add the render host's spelling and the listening scenario**

`host/render/scenario.cpp`:

```cpp
    if (s == "swarm")     return ENGINE_SWARM;
```

Create `host/render/scenarios/swarm_drone.json`. A sanity render, **no hash
gate** — renders are spot checks, not checksums:

```json
{
  "_comment": "SWARM listening render (spec 2026-08-17 section 6). Part A is a SWARM deck left in FLOW (set_step is never called for part 0, so Part::flow() stays true -- 'lanes boot in FLOW -> drone', part.cpp), part B is silenced at LEVEL so nothing else is in the picture. The four sweeps are the four things the engine exists for, one at a time: HARM through the arc, DRIFT from still to alive, FOCUS from open to a swept formant, and one COLOR move to hear a chord change arrive as a glissando rather than a retrigger. No hash gate: this is a sanity render, not a checksum (CLAUDE.md).",
  "sample_rate": 48000,
  "bpm": 90,
  "duration_s": 40,
  "init": [
    {"action":"set_engine","part":0,"value":"swarm"},
    {"action":"set_engine","part":1,"value":"test_tone"},
    {"action":"set_target_active","part":1,"slot":4,"flag":false},
    {"action":"set_target_base","part":1,"slot":4,"value":0.0},

    {"_comment":"A held drone: FLOOR up, a slow RISE, a long FALL."},
    {"action":"set_voice_resonance","part":0,"value":0.8},
    {"action":"set_voice_attack","part":0,"value":0.5},
    {"action":"set_voice_decay","part":0,"value":0.7},
    {"action":"set_voice_sub","part":0,"value":0.4},
    {"action":"set_voice_filt","part":0,"value":0.0},

    {"_comment":"Lanes held still, so what moves is the engine and not the plane: DRIFT is the MOTION lane's BASE here, swept as an event."},
    {"action":"set_depth","part":0,"value":0.0},
    {"action":"set_target_base","part":0,"slot":0,"value":0.35},
    {"action":"set_target_base","part":0,"slot":1,"value":0.5},
    {"action":"set_target_base","part":0,"slot":2,"value":0.35},
    {"action":"set_target_base","part":0,"slot":3,"value":0.0},
    {"action":"set_reverb_mix","value":0.25}
  ],
  "events": [
    {"_comment":"0-10s: harmonic, still. The reference the other three are heard against."},
    {"t":10.0,"action":"set_target_base","part":0,"slot":3,"value":0.5},
    {"_comment":"10-18s: DRIFT half up -- the beating starts."},
    {"t":18.0,"action":"set_target_base","part":0,"slot":3,"value":1.0},
    {"_comment":"18-24s: DRIFT at full."},
    {"t":24.0,"action":"set_target_base","part":0,"slot":1,"value":0.15},
    {"_comment":"24-30s: FOCUS narrowed low -- the formant."},
    {"t":30.0,"action":"set_target_base","part":0,"slot":1,"value":0.85},
    {"_comment":"30-34s: the same aperture, swept to the top."},
    {"t":34.0,"action":"set_color","part":0,"value":0.8},
    {"_comment":"34-40s: a chord arrives as a glide of the whole spectrum, with no retrigger."}
  ]
}
```

HARM is the SOURCE contextual knob and has no scenario action of its own; note in
the render's `_comment` that HARM stays at its boot value here and that the arc
is heard in Task 12's session through the VCV panel instead. **Do not invent a
scenario action for it** — that is host surface, and adding one is a separate
decision.

```bash
source env.sh && cmake --build build && ./build/render host/render/scenarios/swarm_drone.json /tmp/swarm.wav /tmp/swarm.csv
```

Use the scratchpad, not `/tmp`. Confirm the WAV is non-silent (the render host
prints its peak) — a silent listening render is the one failure mode a sanity
render can have.

- [ ] **Step 3: The VCV host**

`host/vcv/res/gen_panel.py` — one word per row, appended in ENG order (0 Synth,
1 Sampler, 2 Wave, 3 Body, 4 BBD, **5 Swarm**), and the `words[]` arity:

```python
DYNAMIC_CAPTIONS = [
    ("MELODY",   "ENGINE",   ("VARY", "SCAN", "VARY", "VARY", "VARY", "VARY")),
    ("ATTACK",   "ENGINE",   ("ATK",  "ATK",  "ATK",   "HIT",   "ATK",  "RISE")),
    ("DECAY",    "ENGINE",   ("DEC",  "DEC",  "DEC",   "DAMP",  "TAIL", "FALL")),
    ("RES",      "ENGINE",   ("RES",  "RES",  "RES",   "CHAR",  "TILT", "FLOOR")),
    ("SUB",      "ENGINE",   ("SUB",  "LEN",  "SUB",   "EXCIT", "INPUT", "SUB")),
    ("FILT",     "ENGINE",   ("FILT", "FILT", "FILT",  "BRITE", "LOSS", "BAL")),
    ("SOURCE",   "ENGINE",   ("TIMB", "ORG",  "FRAME", "MATL",  "DRIVE", "HARM")),
]
```
```python
              "const char* words[6]; };")
```

Extend the sources comment above the table the way it documents every other
engine's words — naming the setter each word stands for, so the next reader can
check the word against the engine rather than against taste: RISE/FALL are the
bloom envelope in absolute seconds, FLOOR is `set_floor` (the sustain floor,
0 = blooms only, 1 = endless drone), BAL is `set_balance` (even/odd, *not*
another tilt — the SOURCE lane owns tilt), HARM is `set_harm` (harmonic →
stretched → clustered).

Two words need checking against the panel's own "no word is printed twice" guard
before they are committed: **SUB** repeats a word SWARM shares with SYNTH/WAVE,
which is fine (`printed_words()` keys on the word, and the guard is about two
*controls* printing one word — verify, do not assume), and **FLOOR** is five
characters in a slot whose widest existing word is EXCIT, also five. Run the
guard; if the footprint check complains, shorten to `FLR` and record why.

`host/vcv/src/Fireflow.cpp` — three edits:

```cpp
                        configSwitch(c.id, 0.f, 5.f, init, "Engine",
                                     {"Synth", "Sampler", "Wave", "Body", "BBD", "Swarm"});
```
```cpp
    nvgRGBA(255, 205, 120, 140),  // Swarm: warm amber-gold
```
```cpp
                eng == 5 ? spky::ENGINE_SWARM :
```

and the ENG comment above the remap, which says "Saved ENG meanings remain 0 =
Synth and 1 = Sampler; 2 adds Wave, 3 Body, 4 the BBD" — extend it with 5, and
keep the sentence about anything not 0/2/3/4/5 falling through to Sampler
intact: that is what keeps old patches meaning what they meant.

The SOURCE tooltip at `Fireflow.cpp:401` enumerates the engines by name and
needs SWARM's HARM added.

Regenerate the panel — never hand-edit `generated_panel.hpp`:

```bash
python host/vcv/res/gen_panel.py
python host/vcv/res/test_panel.py
python host/vcv/res/test_hw_panel.py
```

(Both generators are run from `host/vcv/`; use a scratchpad script if the
working directory is needed, never a `cd` in the tool call.)

- [ ] **Step 4: Build the VCV plugin**

```bash
host/vcv/build-local.sh
```

Always this script, never a hand-rolled `g++` — the system `g++` on this machine
is the ARM cross-compiler and fails with "MinGW not found".

- [ ] **Step 5: Run everything and commit**

```bash
ctest --test-dir build --output-on-failure
```

Expected: green but G8, including `panel_guard` and `hw_panel_guard` (both are
ctest tests) and the two unchanged render hashes.

```bash
git add host/render/scenario.cpp host/render/scenarios/swarm_drone.json \
        tests/test_scenario.cpp host/vcv/res/gen_panel.py \
        host/vcv/res/test_panel.py host/vcv/src/Fireflow.cpp \
        host/vcv/src/generated_panel.hpp
git commit -m "feat(hosts): SWARM reaches the render host and the VCV panel

..."
```

---

### Task 10: The whole-engine CPU gate — **NEEDS BASTIAN + HARDWARE**

**Files:**
- Modify: `bench/workloads_system.cpp` — `inst_swarm_engine_worst`
- Modify: `bench/run.py` — the `system` row tuple
- Modify: `bench/test_run_contract.py`
- Modify: `bench/anchor.cpp` — the anchor order, if the new row belongs in it
- Create: `docs/bench/<date>-<hash>-swarm-axi-o3-patch_sm-usb.{md,csv}`
- Modify: `docs/roadmap.md`

**Interfaces:**
- Consumes: the finished engine (Tasks 4–8), the `swarm` profile (Task 3).
- Produces: the measured verdict on whether SWARM fits.

- [ ] **Step 1: Add the row**

`bench/workloads_system.cpp`, modelled line for line on
`configure_inst_bbd_engine_worst` / `setup_inst_bbd_engine_worst`
(`workloads_system.cpp:432`, `:529`, `:589`): both decks on `ENGINE_SWARM`, both
in STEP with a fire every ~half second so blooms and chord retargeting are
actually happening, the FX chain left exactly as `instrument_worst` has it, and
the returned checksum folding in `inst.active_voices(PART_A/B)` so a wrong
engine moves the checksum instead of passing silently.

```cpp
    { "system", "inst_swarm_engine_worst",
      setup_inst_swarm_engine_worst, proc_inst_swarm_engine_worst },
```

and the same name in `bench/run.py`'s `"system"` tuple and in
`bench/test_run_contract.py`.

```bash
python bench/test_run_contract.py
```

- [ ] **Step 2: Build, and prove the row landed**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" \
  python bench/run.py --profile swarm --board patch_sm --transport usb \
  --optimization o3 --build-only
```
```bash
grep -c "setup_inst_swarm_engine_worst\|proc_inst_swarm_engine_worst" bench/build/bench.map
```

Expected: both symbols. If not, `touch bench/workloads_system.cpp` and rebuild —
and do not accept an unchanged memory table as evidence either way.

Commit the workload code **before** measuring: `run.py` names its result file by
the HEAD commit hash.

- [ ] **Step 3: Measure — Bastian, at the board**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" \
  python bench/run.py --profile swarm --board patch_sm --transport usb \
  --optimization o3 --repeat 2
```

- [ ] **Step 4: Read the gate — inside one image**

**The gate is:** in the image just measured, `inst_swarm_engine_worst` must not
exceed that **same image's** `instrument_worst`, on both `pct_avg` and
`pct_max`. The 102.27 % / 108.62 % in `docs/roadmap.md` and
`docs/bench/2026-08-07-d0b3c08-regress-axi-o3-patch_sm-usb.csv` is
**orientation**, not the comparison: a new translation unit shifts small rows by
~7 % from icache layout alone, so a cross-image subtraction is not a measurement
(memory `fireflow-gotchas`). Record both rows from the one run, and record the
same image's `instrument_worst_bbd_dtcm` verdict beside them.

If the gate fails: N is a compile-time constant, so the answer is a **rebuild at
a lower N**, not an architecture change (spec §8). Repeat Steps 2–4 at the lower
N and update `swarm_cfg::kPartials` and its citation comment.

- [ ] **Step 5: Write it down**

The `docs/bench/` document, in the idiom of the existing ones: the two rows, the
anchor, the verdict, and — explicitly — that the comparison was made inside one
image and why.

`docs/roadmap.md`: extend the living status with SWARM's arrival, the measured N,
the two CPU figures, and the fact that the by-ear pass is still open.

- [ ] **Step 6: Commit**

```bash
git add bench/workloads_system.cpp bench/run.py bench/test_run_contract.py bench/anchor.cpp
git commit -m "bench(swarm): inst_swarm_engine_worst, the whole-engine gate

..."
```

```bash
git add docs/bench docs/roadmap.md engine/swarm/swarm_config.h
git commit -m "docs: the SWARM CPU verdict on the submodule

..."
```

---

### Task 11: Red-proof the whole set, and write down what was measured

**Files:**
- Modify: `docs/engine-map.md` — a new section for the SWARM measurements
- Modify: `docs/roadmap.md` — the open by-ear item
- No code changes expected. If this task needs one, it is a finding.

**Interfaces:**
- Consumes: everything.
- Produces: no code.

- [ ] **Step 1: Full suite from a clean configure**

```bash
rm -rf build
source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: every test green, **G8 included** — if `kNDecided` is still false,
Task 3 has not happened and this task cannot complete. `ctrl_identity` and
`wave_formant_sweep` at their existing hashes. Record the assertion count.

- [ ] **Step 2: Prove every gate can go red**

One mutation at a time, rebuild, confirm the **named** case fails and no other,
revert. This is the step that catches the four vacuous shapes, and the memory is
explicit that plan text is where they are cheapest to produce — so treat this
table as suspect until each line has actually reddened.

| Gate | Mutation | What must fail |
|---|---|---|
| G1 | renumber `ENGINE_SWARM = 2` | G1's census, and `test_deck_bus`'s `static_assert` stays silent — note that, it means the census is the only guard on ordering |
| G2 | give `SwarmEngine` a `process_in` override without `consumes_input` | the contract's `static_assert`, at compile time |
| G3 | add `ENGINE_SWARM` to `part.cpp`'s `set_flow_melody` exclusion | G3 |
| G3b | delete the `ENGINE_SWARM` arm in `Part::voice_env` | G3b |
| G4 | `_inc_of` returns a constant | G4 |
| G5 | `set_target` calls `snap` | G5 |
| G6 | linear pan law | G6 |
| G7 | `P::al` initialised to `1e-30f` | G7 |
| G8 | — | it *was* red; the Task 3 commit is the proof |
| G9 | skip `_sort_targets()` | G9's ascending check |
| G10 | pin `tilt` to `kTiltDark` | G10 |
| G11 | draw `_spread[]` inside `_rebuild_targets` instead of at reseed | G11's twin comparison **and** G22 (a chord change would then redraw the metal) |
| G12 | drop the `over % 2` branch | G12 |
| G13 | drop the `f > ceiling` mute — and if the decided N keeps the top note's ladder below the ceiling (G13's `muted == 0` branch is the live one), first lower `kMaxHzFrac` to `0.1f` so the `muted > 0` branch runs, then confirm the drop reddens it | G13 |
| G14 | give the SUB pair the tilt amplitude instead of `_sub_n` | G14 |
| G15 | return `1.f` from `_focus_weight` | G15 |
| G16 | pass `0.f` as `Env::set_sustain`'s argument | G16's FLOOR-1 half |
| G17 | drop the `std::max(_floor_n, kFlowFloorMin)` | G17 |
| G18 | drop `_auto_pending = false` in `set_hold(true)` | G18's monotone-decay check |
| G19 | apply the bloom gain to the whole envelope instead of to `env - floor` | G19's FLOOR-1 half |
| G20 | drop the `* _fall_n` factor | G20's FALL-0 half |
| G21 | remove the `trigger_chord` override | G21 |
| G22 | call `_do_bloom()` from `set_chord` | G22 |
| G23 | `set_target` calls `snap` | G23 |
| G24 | reverse `_sort_targets`'s comparator | G24 |
| G25 | retarget the whole bank per tick | G25's slice bound |
| G26 | push `_t_hz[i] * 1.01f` into the bank | G26 |
| G27 | move the `depth` multiply before the walk clamp so DRIFT 0 still walks | G27's static half |
| G28 | seed `_drift_rng[i]` from a counter instead of the base seed | G28's "different seed differs" half |
| G29 | make `reseed` a no-op | G29 and G29b |

**A gate whose mutation does not redden it is a finding, not a formality.** Fix
the gate, record what was wrong with it, and say which of the four vacuous
shapes it was.

- [ ] **Step 3: Add the measured facts to the engine map**

`docs/engine-map.md`, a new section after §8, in that file's idiom — every claim
names the setup that produced it, and nothing goes in that a probe or a gate did
not print:

```markdown
## 9. SWARM: what the partial bank actually does

Measured 2026-08-17 on `SwarmEngine` at 48 kHz, `set_seed()` before `init()`
(the SynthEngineT order -- init consumes the seed to draw the cluster map and
the drift streams), seeds 99 / 4242 / 31337 / 999, settled over 40 retarget
periods. N is `swarm_cfg::kPartials` and was decided by the `swarm_bank` bench,
not chosen -- <run>.

- **The target list is generated sorted ascending, and slot i holds target i.**
  That, and nothing else, is the nearest-neighbour retargeting of the design
  spec: a chord change that keeps the root moved <x> octaves summed over the
  bank against <y> for one that does not (G24).
- **DRIFT 0 is bit-static** -- the walk runs free but the depth multiplies at
  the end, so the partial frequencies are identical over 10 s to the last bit.
  At DRIFT 1 the worst partial wandered <z> cents in 10 s against a
  `kDriftCentsMax` of <c> (G27).
- **The bloom's floor and its height are independent.** The accent scales
  `floor + (env - floor)`, so at FLOOR 1 the accent is measurably inert (G19)
  -- a knob that does nothing at one end of another knob, by construction, not
  by omission.
- **A partial above `kMaxHzFrac * sample_rate` is muted, not re-allocated.** At
  the top of the pitch axis (880 Hz root) <n> of N partials are silent, and the
  loop length is unchanged -- which is what makes "CPU is constant regardless of
  played density" true (G13).
```

Fill every `<...>` from the gates' `CAPTURE` output or from a probe. **A section
with a placeholder in it is worse than no section.**

- [ ] **Step 4: Roadmap**

Extend `docs/roadmap.md`'s living status: SWARM shipped, the measured N and the
bench that decided it, the two CPU rows, the render `swarm_drone.json`, and the
**open** by-ear item (Task 12) listing the constants awaiting Bastian's ears.
Per memory `fireflow-status-docs-check-removals`, also check nothing in the file
still describes the five-engine world as complete.

- [ ] **Step 5: Commit**

```bash
git add docs/engine-map.md docs/roadmap.md
git commit -m "docs: what the swarm measured, and what is still owed to ears

..."
```

---

### Task 12: The listening session — **NEEDS BASTIAN**

No agent completes this task. It exists so the by-ear constants are not mistaken
for settled values, and so whoever reads `swarm_config.h` next knows which
comments describe an origin and which describe an open item.

**Files:**
- Modify: `engine/swarm/swarm_config.h` — the comments, and whatever Bastian
  moves
- Modify: `docs/roadmap.md` — close the item
- Possibly: a memory entry, if a value is decided by ear (that is what
  `fireflow-by-ear-decisions` is for)

- [ ] **Step 1: Prepare the material**

Render `swarm_drone.json`, and open the VCV plugin with a SWARM deck on part A —
HARM has no render-host action, so the arc is a panel listen.

The list, in the order spec §10 gives it:

1. **Drift depth range** — `kDriftCentsMax`, `kDriftAmpMax`, and the walk's
   `kDriftWalkStep`/`kDriftPull`/`kDriftRateDepthBoost`. The question: at DRIFT 1
   is it beating or is it seasick, and at DRIFT 0.25 is anything happening at all.
2. **The minimum floor in FLOW** — `kFlowFloorMin`. The question: at FLOOR 0 in
   FLOW, is the drone promise kept without the floor swallowing the bloom.
3. **The bloom stagger** — `kBloomStaggerS`. The question: does it bloom, or does
   it switch, or does it smear.
4. **HARM's cluster zone** — `kHarmClusterStart`, `kStretchMax`, `kClusterSpan`.
   The question spec §2 sets: is the pretty range the lower half of the knob, and
   is the extreme reachable rather than merely present.

Two more the plan added and Bastian should hear alongside them, both flagged in
the config header: `kApproachFrac` (how long a COLOR move takes to arrive — the
difference between a portamento and a glissando) and `kSwarmGain`.

- [ ] **Step 2: Record the verdict where the next session will find it**

For each value: kept, or moved to what. A value Bastian confirms goes into the
memory `fireflow-by-ear-decisions` with the same shape the entries there already
have — what it is, what it was, and **why a later session must not "fix" it
back**. The comment in `swarm_config.h` changes from `BY EAR, first try` to a
statement that the pass happened and the value stands; leaving the old wording
in place is exactly how `kFlowNoteMinS`'s "A FIRST GUESS SET BY ARITHMETIC"
comment came to look like an open item after its pass had closed.

- [ ] **Step 3: Close the roadmap item and commit**

```bash
git add engine/swarm/swarm_config.h docs/roadmap.md
git commit -m "tune(swarm): the by-ear pass

..."
```

---

## Self-Review

**Spec coverage.** §2.1 (note engine) → Task 1 G3 and Task 4. §2.2 (one swarm
per deck, retuned) → Tasks 4 and 6, gated by G9/G13/G22/G24. §2.3 (triggers
bloom) → Task 5. §2.4 (the character arc) → Task 4 G11. §3 (the swarm core:
allocation, arc, drift, bloom, stagger, accent) → Tasks 4, 5, 8. §4 (lanes and
voice row) → Task 1 Step 5 (the row's forwards), Task 4 (TILT G10, FOCUS G15,
BAL G12, SUB G14), Task 5 (RISE/FALL/FLOOR G16/G17/G20), Task 8 (DRIFT G27). §5
(chord, FORM/SONG, STEP/FLOW, NEW) → Task 6 for the chord, Task 1 G3 for the
melodic class that FORM and SONG reach through, Task 5 for STEP/FLOW and CHOKE,
Task 8 for NEW. §6 (integration) → Task 1 for the id, the engine class and the
overrides, Task 9 for both hosts. §7 (reuse and CPU levers) → `Env` unmodified
and `Rng`/`fast_sin` in Tasks 5/8/2, lever 1 and 2 in Task 2, lever 3 in Task 7,
lever 4 in Task 5 (Env's idle snap), lever 5 in Task 3 Step 6 as the contingency
only, lever 6 (DTCM/ITCM) deliberately **not** a task — it is a placement
experiment, not engine work, and the ITCM caveat in `bench/profiles.py` says why
it cannot be done under a two-family profile today. §8 (two-stage CPU) → Tasks 3
and 10. §9 (tests) → G1–G29b, red-proofed in Task 11. §10 (by-ear) → flagged in
`swarm_config.h`, Task 12. §11 (open points) → N in Task 3, captions in Task 9,
the minimum floor in Task 12.

**Type consistency.** `SwarmBank::set_target(int, float, float, float)` and
`snap` (Task 2) are called from `SwarmEngine::_control_tick` and `init` (Tasks 4,
7). `SwarmBank::hz/amp` (Task 2) back `partial_hz_for_test`/`partial_amp_for_test`
(Task 4), which every gate from Task 4 on reads. `set_rise/set_fall/set_floor/
set_sub/set_detune/set_balance/set_harm` are declared in Task 1 Step 5, stored
there, and spent in Tasks 4 (`_harm_n`, `_sub_n`, `_balance`, `_detune_n`) and 5
(`_rise_n`, `_fall_n`, `_floor_n`). `reseed(uint32_t)` (Task 8) is called from
`Part::new_phrase()` (Task 8) and by G29 directly. `swarm_cfg::kCtrlInterval`
(Task 2 Step 3) is what both `SwarmEngine::kCtrlInterval` and `SwarmBank::kSlope`
read, and the `static_assert` against `SynthEngine::kCtrlInterval` lives in
`swarm_engine.cpp`.

**Where this plan deliberately does not assert a number.** Every threshold in
the gates is either derived from a named constant, measured in the same run as
its subject (G5, G23), or a non-vacuity floor whose only job is to prove the
loop ran. The three places where a bound had to be sized against something the
plan cannot know — G23's click bound, G24's travel ratio, G27's drift reach — say
so, and say to probe rather than to widen. The four vacuous shapes were checked
for explicitly: **shape 1** (never runs) by the `REQUIRE(checked > 4)` /
`REQUIRE(t_move > 0)` counters; **shape 2** (re-derives its subject) is why G26
compares the bank against `target_hz_for_test` rather than recomputing the map,
and why G11's determinism half compares two engines rather than one engine
against its own formula; **shape 3** (the threshold in the policed file) is why
no gate reads a `swarm_cfg` literal and why G5/G23 measure their own reference;
**shape 4** (cannot reach its failure branch) is why G19 and G20 each assert two
halves, one of which is the inert case.

**Known open items, deliberately not tasks.** The four in "Open points this plan
carries rather than resolves" above. Each is one line from being decided the
other way, and each is flagged at its site.
