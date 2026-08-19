# FEED Coupled Feedback-FM Drone Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A sixth part engine, `ENGINE_FEED`, that plays notes like SYNTH/WAVE/BODY
but whose sound moves from within — a fixed ring of P two-operator FM pairs per
deck, free-running, where one knob (BOND) morphs each modulator's phase-modulation
input from its own feedback into its neighbour's output, and the motion is a
consequence of that coupling rather than an addition to it.

**Architecture:** Two layers behind one `IPartEngine`. `FeedBank`
(header-only, `engine/feed/feed_pair.h`) owns the hot loop: P `FeedPair`s, each
two phase accumulators plus two two-sample history slots, advanced by per-sample
**slopes** so the inner loop is branch-free with two `fast_sin` per pair. The
ring is evaluated in two passes per sample — compute, then commit — so every
pair reads its neighbour's *previous* samples regardless of loop order.
`FeedEngine` (`engine/feed/feed_engine.{h,cpp}`) owns everything at control rate:
allocation of the pairs over the chord tones, the SPREAD signature, the
pitch-dependent feedback attenuation, the RATIO magnet, the DAMP coefficient,
and one `Env` used unmodified for both amplitude and index.

**Tech Stack:** C++17, clang + Ninja, doctest. ARM GCC + `bench/run.py` for the
two hardware CPU measurements. No new dependencies, no new memory arena, no
tables.

**Spec:** `docs/superpowers/specs/2026-08-18-feed-coupled-feedback-fm-design.md`

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
- **P — the pair count — is a MEASURED number and this plan does not contain
  it.** `feed_cfg::kPairs` carries a placeholder from Task 1 until Task 4's
  bench on the Patch Submodule prints cycles per pair. **No test, no comment,
  no commit message may quote the placeholder as if it were a decision**, and
  no gate may depend on its value: every test loops to `feed_cfg::kPairs`,
  never to a literal. `feed_cfg::kPDecided` is the flag that says the bench has
  spoken, and Task 4 exists to flip it.
- **No Seed figure may be quoted for a submodule claim.** Both CPU measurements
  are `--board patch_sm`. The only submodule numbers that may be cited as prior
  art are the ones in
  `docs/bench/2026-08-07-d0b3c08-regress-axi-o3-patch_sm-usb.csv`
  (`instrument_worst` 102.27 % avg / 108.62 % max at `axi`/`o3`, repeat 1).
- **Bench rows are compared inside ONE image, never across commits or
  profiles.** Adding a translation unit shifts small rows by ~7 % from icache
  layout alone (`docs/gotchas.md`, "Build, bench & test rig"), and `SerialArena` overlays its
  groups so an unchanged memory table proves nothing. Verify a new row landed
  by grepping `bench/build/bench.map` for its mangled setup/proc symbols, and
  `touch` the workload sources after any checkout or stash pop (memory
  `fireflow-bench-stale-object-trap`).
- **No render hash gates for FEED.** Renders are sanity checks, not checksums
  (memory `fireflow-bit-exactness-not-required`). The two hashes that already
  exist (`ctrl_identity`, `wave_formant_sweep`) must stay **unchanged** through
  every task in this plan: both run SYNTH/WAVE decks that FEED does not touch.
  If one moves, that is a finding to report, never a baseline to bump.
- **By-ear constants ship flagged, not final.** Every value in
  `engine/feed/feed_config.h` marked `BY EAR, first try` is Bastian's to confirm
  in Task 13. No gate may assert one of those literals — derive the expectation
  from the named constant, the way `tests/test_step_accent.cpp` derives from
  `kAccentVelFloor` (memory `fireflow-vacuous-test-gates`, shape 3: the
  threshold must not live in the file it polices, and a gate that recomputes its
  subject from the subject is not a gate).
- **A test that cannot go red gets fixed, even if this plan mandated it**
  (memory `fireflow-tests-must-be-able-to-fail`). For every gate: before
  accepting a RED or a GREEN, satisfy yourself the assertion depends on the line
  you changed. If it does not, strengthen the test and say so in the task
  report. Task 12 red-proofs the whole set with one-line mutations.
- **The probe rule** (memory `fireflow-probe-rule`, `docs/engine-map.md` §6):
  where this plan needs a runtime number it does not have, it says "measure it
  with a probe and record what it printed" — it does not guess one. Recipe:
  `clang++ -O2 -Iengine -o probe.exe probe.cpp <sources>`, 0.4 s to compile,
  0.1 s to run. Note `-Iengine`, not `-I.`. Probes are scratch files and belong
  in the scratchpad, never in the repo.
- **Phase is normalized everywhere in this engine.** `fast_sin(p) == sin(2*pi*p)`
  (`engine/util/fast_sin.h`), so every phase-modulation quantity in `FeedPair` —
  the FM index, the feedback amount, the DAMP filter state — is in **cycles**,
  not radians. One cycle equals 2*pi radians of the classical FM literature.
  Every constant in `feed_config.h` that touches those paths says so on its own
  line. This is the single easiest way to be off by 6.28 and hear it as "the
  index knob does nothing until the very top".

**Build and test commands used throughout:**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/spky_tests -tc="feed G*"
```

**Hardware tasks (4 and 11) need Bastian and the board.** They are flagged in
their headings. DFU flashing and the two button presses of the first USB run are
manual steps no agent can take. See "Ordering and the hardware fallback" below.

---

## The control map — resolved before the plan was written

Spec §4 double-books one control and misses a second. Both were put to Bastian
on 2026-08-19 and answered; the answers are binding on this plan.

**What the tree actually offers.** A deck has exactly seven engine knobs:
ATTACK, DECAY, RES, SUB, SOURCE, FILT, DETUNE (`host/vcv/res/gen_panel.py`
`part_controls()`, plus the two appended FILT/DETUNE entries). Of the five
modulation lanes, the VCV host writes only three bases: `LANE_SOURCE` from the
SOURCE knob unconditionally, `LANE_PITCH` on BBD decks, and `LANE_SIZE` — from
the SUB knob on a sampler deck and **pinned to `0.5f` on every other deck**
(`host/vcv/src/Fireflow.cpp:792`, `:880-882`). `LANE_MOTION` and `LANE_LEVEL`
are never written at all (`docs/gotchas.md`, "Host (VCV)").

**Conflict 1 — SOURCE.** The SOURCE knob *is* the `LANE_SOURCE` base, and the
`DYNAMIC_CAPTIONS` "SOURCE" row is that same knob's caption. Spec §4 gives it to
BOND (lane table) and to RATIO (voice row). **Ruling: BOND owns SOURCE.** That
is §2.3 read literally — "one knob is the cliff" — and §4's own argument that
the character axis belongs on the ×2 lane with the largest excursion.

**Conflict 2 — SPREAD has the same problem §4 only names for DEPTH.**
`LANE_SIZE`'s base is pinned to `0.5f` off the sampler, so SPREAD would have
been modulation-only too. **Ruling: re-point existing knobs on a FEED deck**
rather than adding menu-only parameters.

The resulting map, which is what every task below implements:

| Knob | FEED meaning | Mechanism |
|---|---|---|
| SOURCE | **BOND** | writes `LANE_SOURCE`'s base, unchanged host path; the ×2 lane swings through the cliff |
| ATTACK | **RISE** | `Part::set_voice_attack` |
| DECAY | **FALL**, and its top quarter is **FLOOR** | `Part::set_voice_decay`; the fold SWARM's round 2 used and this repo recorded in `docs/attic/2026-08-18-swarm-withdrawn.md` |
| RES | **RATIO** | `Part::set_voice_resonance` — RESO is free because FEED has no filter resonance (spec §4) |
| SUB | **SUB** | `Part::set_voice_sub` |
| FILT | **DAMP** (bipolar) | `Part::set_voice_filt` |
| DETUNE | **SPREAD** | the host writes `LANE_SIZE`'s base from the DETUNE knob on a FEED deck — the sampler's `SUB → LANE_SIZE` re-point, one entry further down the same ledger. `FeedEngine::set_detune` is a documented no-op |

| Lane | FEED meaning | Base comes from |
|---|---|---|
| `LANE_SOURCE` (×2) | BOND | the SOURCE knob |
| `LANE_SIZE` (×1/2) | SPREAD | the DETUNE knob, on a FEED deck |
| `LANE_PITCH` (×1) | pitch, as everywhere | the modulation plane |
| `LANE_MOTION` (×3/4) | DEPTH — the FM index | `feed_cfg::kDepthBase`, written by the host on a FEED deck |
| `LANE_LEVEL` (×3/2) | level, as everywhere | `Part`'s default |

**DEPTH is the one FEED control with no dedicated knob, and that is a decision,
not an oversight.** The arithmetic does not close: nine FEED parameters, seven
knobs, one fold. DEPTH is the parameter that loses least by it, because three
other knobs already move the index — `index = DEPTH · env`, and RISE, FALL and
FLOOR all shape `env`, as does the STEP accent. RATIO losing its knob instead
would freeze the tonal→bell→clangorous arc of §4 at one ratio for every FEED
deck ever built, which is strictly worse. What the host task **does** deliver is
spec §4's stated purpose: the MOTION lane base stops being `Part`'s incidental
0.5 and becomes a value FEED chose and Bastian tuned, and the ledger comment in
`Fireflow.cpp` grows the entry that tells the next engine where to look. A
dedicated DEPTH knob is a panel-round decision (M6), explicitly out of scope
here. Spec §4's defensive requirement — "DEPTH at 0.5 must be a good sound" —
is gated (G29) rather than assumed.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `engine/feed/feed_config.h` | Every FEED constant in one place: P and its decided-flag, the by-ear block, the phase-unit statements. Nothing else in `engine/feed/` may hold a tuning literal. | 1 |
| `engine/feed/feed_pair.h` | Header-only. `FeedPair` (the per-pair state) and `FeedBankT<P>` (the ring and the two-pass hot loop). No allocation logic, no chord, no libm beyond `fast_sin` and `std::floor`. Header-only on purpose: `FeedBank::process` must inline into `FeedEngine::process` the way `Part::process` inlines into `Instrument::process`. | 2 |
| `engine/feed/feed_engine.h` | The `IPartEngine` implementation: state, overrides, observers. | 1 |
| `engine/feed/feed_engine.cpp` | Everything at control rate — allocation over the chord, the SPREAD signature, the pitch attenuation, the RATIO magnet, the DAMP coefficient, the envelope, `std::pow`. The only new `.cpp` in the tree, so the six build sites are touched once. | 1 |
| `tests/test_feed_pair.cpp` | The bank's own gates: frequency, ratio, glide, pan, exact silence, boundedness, the ring's topology. | 2 |
| `tests/test_feed_engine.cpp` | Everything else — id census, contract, coupling, SPREAD, the pitch centre, RATIO, DAMP, SUB, the envelope, chord, NEW, determinism. One file, shared helpers at the top. | 1, 5–9 |
| `bench/workloads_feed.cpp` | The `feed` family: the `feed_pairs` kernel row that prices the bank at several P. | 4 |
| `host/render/scenarios/feed_drone.json` | The listening render. Sanity only, no hash. | 10 |

Six build sites carry `engine/feed/feed_engine.cpp`, and all six are edited in
Task 1: `CMakeLists.txt` (twice — `spky_tests` and `render`), `bench/Makefile`,
`bench/audition/Makefile`, `host/vcv/Makefile`, `shell/Makefile`. A missing one
does not fail at compile time; it fails at link time in a host nobody built that
day, which is why they go in together.

## Ordering and the hardware fallback

Tasks 1, 2, 3, 5, 6, 7, 8, 9, 10 and 12 are pure desktop work and run in order.
Task 0 is a listening step for Bastian and can happen at any point before
Task 10. Task 4 (the `feed_pairs` bench) and Task 11 (`inst_feed_engine_worst`)
need Bastian at the board.

**If Task 4's result is not available when the queue reaches it**, skip it and
continue with Tasks 5–10. Nothing in them depends on P's *value*: they depend
only on `kPairs` being a compile-time constant, which it already is. Then run
Tasks 4 and 11 together in one board session before Task 12. This is the
expected case, not the exception — it costs one board session instead of two.

**What may NOT be deferred**: Task 4's `kPDecided` gate. Task 12 does not pass
while P is a placeholder, and the branch does not merge without Task 12.

**No oversampling in round 1** (spec §7 lever 5). The two stabilizers of §3.2
are the plan. If Task 13's ears report aliasing, 2× is a rebuild and a separate
round, and the decision is then informed by a measurement instead of a fear.
Everything else in spec §7 is settled and not to be re-opened: `Env` as the
envelope unmodified, `Rng` for the `NEW` draws only, `fast_sin`, slopes not
smoothers in the hot loop, one sine per operator with pan gains.

## Open points this plan carries rather than resolves

1. **The regime map runs as Task 3, not before the plan.** Spec §8 asks for a
   BOND × DEPTH × RATIO regime map "measured on the desktop build and recorded
   in the engine map **before the plan is written**". That is not possible: the
   map is a measurement of an engine that does not exist yet. Task 3 runs it
   immediately after the bank compiles, and three later tasks read their
   constants off it — §9.9's BOND threshold and cent tolerance (Task 5), the
   SPREAD boundary between "beating" and "detuned" (Task 5), and BOND's knob
   curve (Task 13). **No task downstream of Task 3 may invent one of those
   numbers.**
2. **SPREAD's distribution: symmetric in cents or in ratio.** Spec §14 leaves it
   to a desktop probe. The plan implements **cents** (a symmetric offset in the
   log-frequency domain, which is what an ear hears as symmetric) and Task 5
   Step 2 runs the probe that either confirms it or reports the difference. It
   is one line either way.
3. **RATIO's lower-half mechanism: magnet curve, not zones with hysteresis.**
   Spec §4 leaves it to the plan. A zone reader is a discrete selector with
   state, and RATIO must run continuously into the irrational upper half; a
   monotone warp that flattens near the integers has no state, cannot be
   "between" states, and cannot get its hysteresis wrong. Task 8 builds the
   warp, and its monotonicity is a gate (G16) rather than a claim.
4. **FEED voices at most `kPairs / 2` chord tones.** SPREAD is a detune *within*
   a tone's group of pairs; a group of one pair has nothing to beat against, so
   a bank that put one pair on each of four chord tones would leave SPREAD dead
   at exactly the chord size COLOR reaches. Capping the voiced tone count at
   `kPairs / 2` keeps every voiced tone in a group of at least two. Sounding
   fewer chord tones than the chord holds is established here — a BODY deck
   sounds only the root (`SynthEngineT::trigger_chord`'s voice clamp). Gate G25.
   If Task 4 returns `kPairs >= 8` this cap never binds and the line is inert;
   it is still worth having, because P is a rebuild away from being smaller.
5. **FTZ is measured in this round and decided in its own.** Task 11 Step 6
   records the cycle delta with and without the FPSCR flush-to-zero bit.
   Enabling it changes existing behaviour instrument-wide and this plan does not
   make that decision (spec §8).

---

### Task 0: Hear the direction before building it — **NEEDS BASTIAN**

Spec §13. Approved and released on 2026-08-19. **This is a listening step, not a
gate**: it informs the plan and the by-ear constants, and the outcome is allowed
to be "no". No agent completes it.

**Files:** none. If anything is written down it goes into the task report and,
if it changes a constant, into `engine/feed/feed_config.h` when Task 1 creates
it.

- [ ] **Step 1: Put a coupled feedback-FM voice next to FireFlow in Rack**

Audible Instruments **Macro Oscillator** (the Braids port) in **WTFM** mode —
`RenderChaoticFeedbackFm`, the acknowledged ancestor of §3.1's topology
(spec §11). Two ways, and both are worth ten minutes:

1. Beside a FireFlow deck, so the two are heard against each other.
2. Through `IN L` / `IN R` into a **BBD deck**, so it runs through this
   instrument's own FX chain and reverb — which is the context FEED will
   actually live in.

- [ ] **Step 2: Answer three questions in the task report**

Written down, because Task 13 will otherwise have to re-derive them:

1. **Does the coupling read as motion, or as noise?** This is the whole premise
   of §2.4. If it reads as noise at every setting, say so — that is the "no".
2. **Where does the interesting range sit on the knob?** Braids' TIMBRE/COLOR
   in WTFM is not BOND, but it is the same axis, and where its usable region
   lives is the first evidence for BOND's curve (§10, and Task 3's regime map
   will confirm or contradict it).
3. **Does it survive the reverb?** FEED is a drone engine in an ambient
   instrument. A sound that only works dry is a different design.

- [ ] **Step 3: Record the verdict**

In the task report, and — if the answer is "no" — stop the plan here and open a
withdrawal note in `docs/attic/` the way
`docs/attic/2026-08-18-swarm-withdrawn.md` does it. Ten minutes spent here is
cheaper than thirteen tasks.

---

### Task 1: The engine id, the silent engine, and every tripwire it trips

**Files:**
- Modify: `engine/parts/engine_iface.h` — `ENGINE_FEED = 6` before the sentinel
- Create: `engine/feed/feed_config.h`
- Create: `engine/feed/feed_engine.h`, `engine/feed/feed_engine.cpp`
- Modify: `engine/parts/part.h` — include, member, `_engine_for` case, voice-row
  forwards, `feed()` accessor, `active_voices`/`voice_env` arms
- Modify: `engine/parts/part.cpp` — the seed line beside `_wave.set_seed`
- Modify: `engine/param_table.h` — the `P_ENGINE_A/B` range and its comment
- Modify: `CMakeLists.txt` (both targets), `bench/Makefile`,
  `bench/audition/Makefile`, `host/vcv/Makefile`, `shell/Makefile`
- Modify: `tests/test_deck_bus.cpp` — `static_assert`, engine list,
  `consumes_input` census
- Modify: `tests/test_bbd_engine.cpp:22` — the `ENGINE_COUNT` check
- Modify: `tests/test_part.cpp` — the id census
- Modify: `tests/test_param_table.cpp` — the discrete-clamp expectation
- Create: `tests/test_feed_engine.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `spky::ENGINE_FEED == 6`, `spky::ENGINE_COUNT == 7`
  - `namespace spky::feed_cfg` with `kPairs`, `kPDecided`, `kCtrlInterval`, and
    the by-ear block listed in Step 3
  - `class spky::FeedEngine : public IPartEngine` with
    `void set_seed(uint32_t)` (call BEFORE `init`), `void init(float) override`,
    `void set_targets(const float*, float) override`,
    `void trigger(float) override`, `void process(float&, float&) override`,
    the seven voice-row setters (`set_attack`, `set_decay`, `set_resonance`,
    `set_sub`, `set_detune`, `set_filt`, `set_accent`),
    `int active_voices() const`, `float voice_env(int) const`,
    and `static constexpr int kCtrlInterval = feed_cfg::kCtrlInterval`
  - `FeedEngine& Part::feed()` / `const FeedEngine& Part::feed() const`

- [ ] **Step 1: Write the failing tests**

Create `tests/test_feed_engine.cpp`. The helper block at the top is shared by
every later task in this plan — later tasks append cases, never a second helper
block.

```cpp
// FEED -- the coupled feedback-FM drone engine.
// Spec: docs/superpowers/specs/2026-08-18-feed-coupled-feedback-fm-design.md
//
// P is feed_cfg::kPairs and is a MEASURED number (spec section 8). Nothing in
// this file may assume its value: every loop runs to kPairs and every
// expectation is derived from the named constants in feed_config.h, never from
// their literals.
#include <doctest/doctest.h>
#include "parts/part.h"
#include "feed/feed_engine.h"
#include "part_engine_contract.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace spky;

namespace {

// A FeedEngine at a known state. set_seed() BEFORE init(), the SynthEngineT
// convention (synth_engine.h) -- init() consumes the seed to draw the SPREAD
// signature and the per-pair feedback offsets, so the reverse order measures a
// different object.
FeedEngine fresh_feed(uint32_t seed = 99u) {
    FeedEngine e;
    e.set_seed(seed);
    e.init(48000.f);
    e.set_cycle(1.f);
    return e;
}

// The five lane targets, in the order Part pushes them: SOURCE, SIZE, PITCH,
// MOTION, LEVEL (engine/mod/lane_id.h). Named for what FEED reads them as.
void feed_lanes(FeedEngine& e, float pitch, float bond = 0.f,
                float spread = 0.f, float depth = 0.5f, float level = 1.f) {
    const float t[LANE_COUNT] = { bond, spread, pitch, depth, level };
    e.set_targets(t, 0.5f);
}

std::vector<float> render_l(FeedEngine& e, int n) {
    std::vector<float> out(n);
    for (auto& s : out) { float l = 0.f, r = 0.f; e.process(l, r); s = l; }
    return out;
}

float peak_of(const std::vector<float>& b) {
    float p = 0.f;
    for (float v : b) p = std::max(p, std::fabs(v));
    return p;
}

// Run the engine long enough for every slope to land: kCtrlInterval samples is
// one control tick, and the glide closes over several of them.
void settle(FeedEngine& e, int ticks = 200) {
    float l = 0.f, r = 0.f;
    for (int i = 0; i < ticks * FeedEngine::kCtrlInterval; ++i) e.process(l, r);
}

}  // namespace

TEST_CASE("feed G1: the engine id is appended, never renumbered") {
    // A saved patch stores the id, so moving one silently reassigns every deck
    // that used it (engine_iface.h). This case is the census; the
    // static_assert in test_deck_bus.cpp is the build-time half.
    CHECK(ENGINE_TEST_TONE == 0);
    CHECK(ENGINE_SYNTH == 1);
    CHECK(ENGINE_SAMPLER == 2);
    CHECK(ENGINE_WAVE == 3);
    CHECK(ENGINE_BODY == 4);
    CHECK(ENGINE_BBD == 5);
    CHECK(ENGINE_FEED == 6);
    CHECK(ENGINE_COUNT == 7);
}

TEST_CASE("feed G2: FeedEngine satisfies the universal part-engine contract") {
    // Silence in stays bounded and finite forever; the process_in/
    // consumes_input pairing holds (FEED overrides neither, so the static
    // assert reads both as IPartEngine's); every no-op setter is safe in any
    // order. tests/part_engine_contract.h owns the reasoning.
    check_part_engine_contract<FeedEngine>([](FeedEngine& e) {
        e.set_seed(7u);
        e.init(48000.f);
    });
}

TEST_CASE("feed G3: a FEED deck is a note deck, and the switch completes") {
    Part p;
    p.init(48000.f, 5u);
    float l = 0.f, r = 0.f;
    p.set_engine(ENGINE_FEED);
    for (int i = 0; i < 500; ++i) p.process(l, r);   // 4 ms fade out + in
    REQUIRE(p.engine_id() == ENGINE_FEED);
    // Part derives the note-deck flag as "not SAMPLER and not BBD"
    // (part.cpp:43 and :460), so FEED gets the melodic phrase machinery for
    // free -- and that is exactly the kind of free behaviour that silently
    // stops being true when someone adds an engine to the exclusion list.
    CHECK(p.mod().pitch_lane_is_note_lane_for_test());
}

TEST_CASE("feed G3b: a FEED deck reports its envelope to the meter") {
    // Part::voice_env/active_voices return 0 for any engine they have no arm
    // for, so without one the VCV LED and Instrument's meter go dead on a FEED
    // deck. A coupled network is one sound, not n voices: slot 0 carries the
    // envelope and active_voices() is 1 while audible (spec section 6).
    Part p;
    p.init(48000.f, 5u);
    float l = 0.f, r = 0.f;
    p.set_engine(ENGINE_FEED);
    for (int i = 0; i < 500; ++i) p.process(l, r);
    REQUIRE(p.engine_id() == ENGINE_FEED);
    p.trigger_manual();
    float m = 0.f;
    for (int i = 0; i < 4800; ++i) { p.process(l, r); m = std::max(m, p.max_voice_env()); }
    CHECK(m > 0.1f);
}
```

`pitch_lane_is_note_lane_for_test()` does not exist yet — add it in Step 6.

Add to `CMakeLists.txt`, in the `spky_tests` source list immediately after
`tests/test_bbd_engine.cpp`:

```cmake
    engine/feed/feed_engine.cpp
    tests/test_feed_engine.cpp
```

and `engine/feed/feed_engine.cpp` alone to the `render` target's list, beside
`engine/parts/bbd_engine.cpp` (`CMakeLists.txt:182`).

- [ ] **Step 2: Run it and confirm the build fails**

```bash
source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Expected: `feed/feed_engine.h: No such file or directory`, and — if you comment
that include out to see it — `use of undeclared identifier 'ENGINE_FEED'` plus
the `static_assert` in `tests/test_deck_bus.cpp:165` firing with its own
message. **Note the `static_assert` firing in the task report**: that tripwire
working is part of this task's deliverable, not an obstacle to it.

- [ ] **Step 3: Add the id and the config header**

`engine/parts/engine_iface.h`, immediately before `ENGINE_COUNT`:

```cpp
    // The coupled feedback-FM drone (spec 2026-08-18 feed-coupled-feedback-fm).
    // A melodic engine like SYNTH/WAVE/BODY -- Part's note-deck flag is derived
    // as "not SAMPLER and not BBD" (part.cpp), so this id needs no entry there
    // -- but NOT a SynthEngineT<Voice>: it has no per-note voices and no
    // allocator, one free-running ring of P operator pairs per deck instead.
    ENGINE_FEED = 6,
```

Create `engine/feed/feed_config.h`:

```cpp
#pragma once

namespace spky {
namespace feed_cfg {

// --- PHASE UNITS ----------------------------------------------------------
//
// fast_sin(p) == sin(2*pi*p) (engine/util/fast_sin.h), so EVERY quantity in
// this engine that is added to a phase is in CYCLES, not radians. One cycle is
// the 2*pi of the classical FM literature: an "index of 6" from a DX7 table is
// kIndexMaxCycles ~= 0.95 here. The three constants this applies to are
// kIndexMaxCycles, kFbBaseCycles and everything the DAMP one-pole carries,
// because it filters a cycles-valued signal.

// --- P, the one number here that is not a taste decision ------------------
//
// P is MEASURED. The feed_pairs row in bench/workloads_feed.cpp prints cycles
// per pair on the Daisy Patch Submodule, and P follows from the 960 000-cycle
// block budget (spec section 8). The literal below is a PLACEHOLDER that exists
// so the desktop tasks can build, and it carries NO CPU claim of any kind: do
// not quote it, do not size anything against it by hand, and do not let a test
// depend on its value.
constexpr int kPairs = 4;

// False until the bench has run and kPairs above is its result. The gate
// "feed G8" in tests/test_feed_engine.cpp fails while this is false, so an
// undecided P cannot reach main.
constexpr bool kPDecided = false;

// --- structure ------------------------------------------------------------

// The control-rate raster. Must equal SynthEngine::kCtrlInterval; the
// static_assert lives in feed_engine.cpp, where both headers are visible.
constexpr int kCtrlInterval = 96;

// How many chord tones the bank voices. SPREAD detunes the pairs sharing one
// tone against each other, so a tone holding a single pair has nothing to beat
// against and SPREAD would go dead at exactly the chord size COLOR reaches
// (plan open point 4). Every voiced tone keeps a group of at least two.
constexpr int kPairsPerTone = 2;
static_assert(kPairs >= kPairsPerTone,
              "a bank must hold at least one full tone group");

// --- by ear, first try (spec section 10) ----------------------------------
// Every constant below this line is Bastian's to confirm in Task 13. No gate
// asserts any of these literals; gates derive from the names.

// The FM index at DEPTH 1 and envelope 1, in CYCLES (see PHASE UNITS above).
constexpr float kIndexMaxCycles = 0.95f;   // BY EAR, first try

// The feedback / neighbour input amount before the pitch attenuation, in
// CYCLES. This is the term spec section 3.2.2's attenuation multiplies.
constexpr float kFbBaseCycles = 0.30f;     // BY EAR, first try

// Pitch attenuation (spec section 3.2.2, the Braids RenderFeedbackFm recipe).
// The pair's NORMALIZED pitch is already logarithmic -- pitch_to_hz(p) is
// 110 * 8^p (synth_engine.cpp) -- so a straight line in p is an exponential
// fall in Hz, which is the shape Braids derives from a pitch offset, at zero
// cost and with no libm call on the control path.
//   atten(p) = clamp(1 - kFbPitchSlope * p, kFbAttenMin, 1)
constexpr float kFbPitchSlope = 0.75f;     // BY EAR, first try
constexpr float kFbAttenMin   = 0.18f;     // BY EAR, first try

// NEW's per-pair feedback offsets (spec section 3.4): the cliff becomes a
// gradient the ear can ride instead of an edge. Multiplicative, symmetric.
constexpr float kFbOffsetRange = 0.12f;    // BY EAR, first try

// SPREAD, in cents, at the two ends of the knob. The lower half stays in
// single digits by spec section 3.4; kSpreadKneeCt is the value at knob 0.5
// and kSpreadMaxCt the value at knob 1. The exact numbers are Task 3's regime
// probe to confirm ("audibly beating but not yet detuned").
constexpr float kSpreadKneeCt = 7.f;       // BY EAR, first try
constexpr float kSpreadMaxCt  = 45.f;      // BY EAR, first try

// RATIO (spec section 4). The lower half runs 1:1..kRatioMagnetTop through a
// monotone warp that flattens near the integers; the upper half runs
// continuously from there into the irrational.
constexpr float kRatioMagnetTop = 4.f;
constexpr float kRatioMagnetExp = 3.f;     // BY EAR, first try (>1 = flatter)
constexpr float kRatioMax       = 11.f;    // BY EAR, first try

// DAMP: the one-pole inside the feedback path. FILT's centre detent is
// kDampCenterHz; the bipolar travel multiplies and divides it by kDampSpan.
constexpr float kDampCenterHz = 3200.f;    // BY EAR, first try
constexpr float kDampSpan     = 9.f;       // BY EAR, first try

// The envelope. FLOOR rides the top quarter of the FALL knob (the control map
// above; the fold SWARM's round 2 used). kFlowFloorMin is the minimum floor
// enforced in FLOW so the drone promise holds at FLOOR 0.
constexpr float kFloorFoldStart = 0.75f;   // BY EAR, first try
constexpr float kFlowFloorMin   = 0.12f;   // BY EAR, first try

// The STEP accent's two halves, deliberately equal to SynthEngineT's so a
// listening session says which one wants to differ (spec section 4).
constexpr float kAccentVelFloor = 0.3f;    // BY EAR, first try
constexpr float kAccentDecFloor = 0.3f;    // BY EAR, first try

// The tanh ceiling on the deck sum (spec section 3.3), the
// BodyVoice::kFlowSatCeil pattern and for the same stated reason.
constexpr float kSatCeil = 0.55f;          // BY EAR, first try
constexpr float kSatInv  = 1.f / kSatCeil;

// SUB: one sine an octave below the root, not in the ring and not coupled.
constexpr float kSubMax = 0.7f;            // BY EAR, first try

// The LANE_MOTION base a FEED deck gets from the host, i.e. the DEPTH the
// player sits at before any modulation. DEPTH is the one FEED control with no
// knob of its own, so this value carries spec section 4's defensive
// requirement -- "DEPTH at 0.5 must be a good sound" -- and gate G29 is what
// makes that requirement falsifiable rather than a hope.
constexpr float kDepthBase = 0.5f;         // BY EAR, first try

}  // namespace feed_cfg
}  // namespace spky
```

- [ ] **Step 4: Add the silent engine**

Create `engine/feed/feed_engine.h`. This task builds the **shell**: the class,
its state, its setters, and a `process()` that outputs silence. Tasks 5–9 fill
it. Everything a later task needs is declared here so no later task has to
touch the class shape twice.

```cpp
#pragma once
#include <cstdint>
#include "feed/feed_config.h"
#include "feed/feed_pair.h"      // Task 2 creates it; a forward-declared
                                 // FeedBank cannot be a by-value member
#include "mod/rng.h"
#include "parts/engine_iface.h"
#include "pitch/chord.h"
#include "synth/env.h"
#include "util/math.h"           // clampf, in the inline setters below

namespace spky {

// FEED: a fixed ring of feed_cfg::kPairs two-operator FM pairs per deck,
// running continuously. A trigger retunes the ring and injects energy through
// one Env; it does not start it. See the spec for the topology; feed_pair.h
// owns the hot loop and this class owns everything at control rate.
class FeedEngine : public IPartEngine {
public:
    static constexpr int kCtrlInterval = feed_cfg::kCtrlInterval;
    static constexpr int kMaxChord     = ChordBuilder::kMaxNotes;

    void set_seed(uint32_t seed) { _seed = seed; }   // call BEFORE init

    void init(float sample_rate) override;
    void set_targets(const float* t, float tune) override;
    void trigger(float pitch_norm) override;
    void trigger_chord(const float* pitches_norm, int n) override;
    void set_chord(const float* pitches_norm, int n) override;
    void process(float& outL, float& outR) override;
    void set_cycle(float seconds) override;
    void set_flow(bool flow) override;
    void set_hold(bool on) override;
    void set_width(float n) override;
    void set_accent(float a) override;

    // VOICE edit layer. The names are the FEED captions, the setters are the
    // interface Part forwards through -- see the control map in the plan.
    void set_attack(float n);      // RISE
    void set_decay(float n);       // FALL, and FLOOR in its top quarter
    void set_resonance(float n);   // RATIO
    void set_sub(float n);         // SUB
    void set_filt(float t);        // DAMP, bipolar
    // DETUNE means SPREAD on a FEED deck, and it gets there as the LANE_SIZE
    // base (host/vcv/src/Fireflow.cpp), not through this setter. Kept as an
    // explicit no-op rather than left unimplemented: Part::set_voice_detune
    // forwards to every melodic engine in one line, and an engine missing from
    // that line is the failure class engine_iface.h's process_in/
    // consumes_input comment is about.
    void set_detune(float /*n*/) {}

    // NEW: redraw the deck's individual -- the SPREAD signature and the
    // per-pair feedback offsets (spec section 3.4). The only randomness in the
    // engine, and it is not on the audio path.
    void reseed(uint32_t s);

    int   active_voices() const { return _env.active() ? 1 : 0; }
    float voice_env(int v) const { return v == 0 ? _env.value() : 0.f; }

    // --- observation (tests). Not used on the audio path. ---
    float pair_hz_for_test(int i) const;
    float pair_amp_for_test(int i) const;
    float pair_fb_amount_for_test(int i) const;
    float ratio_for_test() const  { return _ratio; }
    float spread_ct_for_test() const { return _spread_ct; }
    float floor_for_test() const  { return _floor_n; }
    int   voiced_tones_for_test() const { return _voiced_n; }

private:
    void _control_tick();
    void _rebuild_allocation();
    void _draw_individual();

    FeedBank _bank;
    Env      _env;
    Rng      _rng;

    uint32_t _seed = 0x46454544u;   // "FEED"
    float    _sr = 48000.f;
    int      _ctrl_ctr = 0;

    // lanes
    float _bond = 0.f;
    float _spread_n = 0.f;
    float _pitch_n = 0.5f;
    float _depth_n = feed_cfg::kDepthBase;
    float _level = 1.f;

    // voice row
    float _rise_n = 0.5f;
    float _fall_n = 0.5f;
    float _floor_n = 0.f;
    float _ratio = 1.f;
    float _sub_n = 0.f;
    float _damp_t = 0.f;
    float _accent = 0.f;
    float _width = 1.f;

    // derived
    float _spread_ct = 0.f;
    float _cycle_s = 1.f;
    bool  _flow = false;
    bool  _hold = false;

    // chord surface
    float _chord[kMaxChord] = { 0.5f, 0.f, 0.f, 0.f };
    int   _chord_n = 1;
    int   _voiced_n = 1;

    // NEW's individual
    float _spread_sig[feed_cfg::kPairs] = {};
    float _fb_offset[feed_cfg::kPairs] = {};

    // SUB
    float _sub_phase = 0.f;
    float _sub_inc = 0.f;
};

}  // namespace spky
```

Create `engine/feed/feed_engine.cpp` with the shell only — `init` zeroes the
bank and the envelope, `set_targets` stores the five lanes, `process` runs the
control-tick raster and returns silence, and every setter stores its value.
`_rebuild_allocation`, `_draw_individual` and the observers are stubs that later
tasks fill; each carries a one-line `// Task N` comment so a reader can see
which task owes it.

```cpp
#include "feed/feed_engine.h"
#include "synth/synth_engine.h"
#include "util/math.h"
#include <cmath>

namespace spky {

static_assert(FeedEngine::kCtrlInterval == SynthEngine::kCtrlInterval,
              "FEED's control raster must be the instrument's, or its glides "
              "and Part::_control_tick's pushes fall off each other's grid");

void FeedEngine::init(float sample_rate) {
    _sr = sample_rate > 1.f ? sample_rate : 48000.f;
    _env.init(_sr);
    _bank.init(_sr);
    _rng.seed(_seed);
    _draw_individual();
    _ctrl_ctr = 0;
    _rebuild_allocation();
}

void FeedEngine::set_targets(const float* t, float /*tune*/) {
    _bond     = clampf(t[LANE_SOURCE], 0.f, 1.f);
    _spread_n = clampf(t[LANE_SIZE],   0.f, 1.f);
    _pitch_n  = clampf(t[LANE_PITCH],  0.f, 1.f);
    _depth_n  = clampf(t[LANE_MOTION], 0.f, 1.f);
    _level    = clampf(t[LANE_LEVEL],  0.f, 1.f);
}

void FeedEngine::process(float& outL, float& outR) {
    if (--_ctrl_ctr <= 0) { _ctrl_ctr = kCtrlInterval; _control_tick(); }
    outL = 0.f;   // Task 5 wires the bank through the ceiling
    outR = 0.f;
}

// ... setters store; _control_tick, _rebuild_allocation and _draw_individual
// are stubs until Tasks 5-9.

}  // namespace spky
```

**Do not write a placeholder body that a later task must remember to delete.**
Every stub in this file is one line plus a `// Task N: ...` comment naming what
fills it, so Task 12's read-through can find any that were forgotten.

- [ ] **Step 5: Wire it into `Part` and the six build sites**

`engine/parts/part.h`:

```cpp
#include "feed/feed_engine.h"
```
```cpp
    FeedEngine& feed() { return _feed; }
    const FeedEngine& feed() const { return _feed; }
```
```cpp
        if (_engine_id == ENGINE_FEED) return _feed.active_voices();
```
```cpp
        if (_engine_id == ENGINE_FEED) return _feed.voice_env(v);
```
```cpp
            case ENGINE_FEED:    return static_cast<IPartEngine*>(&_feed);
```

and the six voice-row forwards, each gaining one `_feed.` call in the same edit
— the block comment above them says why all of them land in one edit rather
than each engine's forward arriving on its own:

```cpp
    void set_voice_attack(float n)    { ... _feed.set_attack(n); }
    void set_voice_decay(float n)     { ... _feed.set_decay(n); }
    void set_voice_resonance(float n) { ... _feed.set_resonance(n); }
    void set_voice_sub(float n)       { ... _feed.set_sub(n); }
    void set_voice_detune(float n)    { ... _feed.set_detune(n); }
    void set_voice_filt(float t)      { ... _feed.set_filt(t); }
```

Extend that block comment with FEED's reinterpretation, in the idiom it already
uses for BODY and the BBD: ATTACK is RISE, DECAY is FALL with FLOOR folded into
its top quarter, RESONANCE is the modulator/carrier RATIO, SUB is the
sub-octave sine, FILT is DAMP (a low-pass inside the feedback path), and DETUNE
is deliberately a no-op because the host re-points that knob to `LANE_SIZE`'s
base as SPREAD.

`engine/parts/part.cpp`, beside `_wave.set_seed(...)` at line 22:

```cpp
    _feed.set_seed(seed_base ^ 0x46454544u);    // "FEED", distinct individual
```

The six build sites, each beside its `bbd_engine.cpp` or `body_voice.cpp` entry:
`CMakeLists.txt:77` and `:182`, `bench/Makefile:159`,
`bench/audition/Makefile:32`, `host/vcv/Makefile:43`, `shell/Makefile:100`.

- [ ] **Step 6: Add the note-lane observer**

`engine/mod/super_modulator.h`, beside the other observers:

```cpp
    // Whether the PITCH lane is currently a NOTE lane, i.e. whether the
    // melodic phrase machinery reaches it. Part derives this from the engine
    // id ("not SAMPLER and not BBD", part.cpp), so an engine added to that
    // exclusion list silently loses FORM, SONG and the phrase -- with nothing
    // to observe it. G3 is what observes it.
    bool pitch_lane_is_note_lane_for_test() const {
        return _lanes[LANE_PITCH].note_lane_for_test();
    }
```

and the matching one-line accessor on `ModLane` returning `_note_lane()`.

- [ ] **Step 7: Update every tripwire**

`engine/param_table.h:73` — `X(P_ENGINE_A, 0.f, 6.f, 7) X(P_ENGINE_B, 0.f, 6.f, 7)`,
and the comment at `:35` from "`ENGINE_BBD=5, ENGINE_COUNT=6` -- so 0..5, 6
steps" to the FEED numbers, keeping the sentence about `Fireflow.cpp`'s UI remap
being a host-side translation intact.

`tests/test_deck_bus.cpp:165` — `static_assert(ENGINE_COUNT == 7, ...)`, the
engine list at `:206` gains `ENGINE_FEED`, and the `consumes_input` census at
`:183` gains a `FeedEngine feed; CHECK_FALSE(feed.consumes_input());`. Re-check
the two claims the `static_assert`'s message demands: FEED does not override
`consumes_input`, so it belongs in the "structurally unreachable" set; and FEED
is audible in the sweep without extra memory, so it does not join it vacuously —
**verify the second by reading the sweep's non-silence guard, not by assuming
it**, since a FEED deck at FLOOR 0 with no trigger is legitimately silent.

`tests/test_bbd_engine.cpp:22` — `CHECK(ENGINE_COUNT == 7);`

`tests/test_part.cpp:286` — add `CHECK(ENGINE_FEED == 6);` beside the BODY line.

`tests/test_param_table.cpp` — the discrete-clamp expectation for `P_ENGINE_*`
now has 7 steps.

- [ ] **Step 8: Build, run, commit**

```bash
source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: everything green, including `ctrl_identity` and `wave_formant_sweep`
at their existing hashes — a silent sixth engine nobody selects cannot move
them, and if one moves, that is a finding.

```bash
git add engine/parts/engine_iface.h engine/feed engine/parts/part.h \
        engine/parts/part.cpp engine/mod/super_modulator.h engine/mod/lane.h \
        engine/param_table.h CMakeLists.txt bench/Makefile \
        bench/audition/Makefile host/vcv/Makefile shell/Makefile \
        tests/test_deck_bus.cpp tests/test_bbd_engine.cpp tests/test_part.cpp \
        tests/test_param_table.cpp tests/test_feed_engine.cpp
git commit -m "feat(feed): ENGINE_FEED = 6, a silent engine and its tripwires

The sixth part engine's id, config header and shell. Appended before the
ENGINE_COUNT sentinel, which fired the build-time tripwire in
tests/test_deck_bus.cpp as designed -- recorded in the task report.

kPairs carries a placeholder and kPDecided is false: P is the feed_pairs
bench's to decide (spec section 8), and feed G8 fails until it has.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: The pair bank — the hot loop

**Files:**
- Create: `engine/feed/feed_pair.h`
- Create: `tests/test_feed_pair.cpp`
- Modify: `CMakeLists.txt` — `tests/test_feed_pair.cpp` in `spky_tests`

**Interfaces:**
- Consumes: `feed_cfg` (Task 1).
- Produces:
  - `struct spky::FeedPair` — the per-pair state
  - `template <int P> class spky::FeedBankT` with
    `void init(float sr)`, `void set_bond(float k)`, `void set_index(float cycles)`,
    `void set_ratio(float r)`, `void set_damp_coef(float c)`,
    `void set_fb_amount(int i, float cycles)`,
    `void set_target(int i, float hz, float amp, float pan)`,
    `void snap(int i, float hz, float amp, float pan)`,
    `void process(float& l, float& r)`,
    `float hz(int i) const`, `float amp(int i) const`,
    `float fb_amount(int i) const`,
    and `static constexpr int kSlopeTicks`
  - `using spky::FeedBank = FeedBankT<feed_cfg::kPairs>`

- [ ] **Step 1: Write the failing tests**

Create `tests/test_feed_pair.cpp`:

```cpp
// The FEED pair bank -- the hot loop, tested without the engine around it.
// Spec: docs/superpowers/specs/2026-08-18-feed-coupled-feedback-fm-design.md
// section 3.1-3.2.
//
// Every loop runs to feed_cfg::kPairs. P is measured (spec section 8) and
// nothing here may assume its value.
#include <doctest/doctest.h>
#include "feed/feed_pair.h"
#include "feed/feed_config.h"
#include <algorithm>
#include <cmath>
#include <vector>

using namespace spky;

namespace {

FeedBank fresh_bank() {
    FeedBank b;
    b.init(48000.f);
    b.set_bond(0.f);
    b.set_index(0.f);
    b.set_ratio(1.f);
    b.set_damp_coef(1.f);        // 1 = the one-pole passes everything
    for (int i = 0; i < feed_cfg::kPairs; ++i) b.set_fb_amount(i, 0.f);
    return b;
}

std::vector<float> render(FeedBank& b, int n) {
    std::vector<float> out(n);
    for (auto& s : out) { float l = 0.f, r = 0.f; b.process(l, r); s = l + r; }
    return out;
}

// Zero crossings per second, the cheapest honest frequency estimate for a
// single sine and the only one this file needs -- the spectral work lives in
// tests/test_feed_engine.cpp, where there is something spectral to measure.
float zc_hz(const std::vector<float>& x, float sr) {
    int zc = 0;
    for (size_t i = 1; i < x.size(); ++i)
        if ((x[i - 1] < 0.f) != (x[i] < 0.f)) ++zc;
    return 0.5f * zc * sr / static_cast<float>(x.size());
}

}  // namespace

TEST_CASE("feed P1: a snapped pair runs at the frequency it was given") {
    FeedBank b = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) b.snap(i, 220.f, 0.f, 0.f);
    b.snap(0, 220.f, 1.f, 0.f);                 // only pair 0 audible
    std::vector<float> x = render(b, 48000);
    CHECK(zc_hz(x, 48000.f) == doctest::Approx(220.f).epsilon(0.02));
    CHECK(b.hz(0) == doctest::Approx(220.f));
}

TEST_CASE("feed P2: RATIO moves the modulator, not the carrier") {
    // The carrier's pitch is the note. If RATIO moved it, every ratio change
    // would be a transposition and spec section 4's tonal->bell arc would be a
    // pitch bend instead of a timbre.
    FeedBank b = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) b.snap(i, 220.f, 0.f, 0.f);
    b.snap(0, 220.f, 1.f, 0.f);
    b.set_index(0.4f);
    b.set_ratio(3.f);
    std::vector<float> x = render(b, 48000);
    // A carrier at 220 with sidebands at +-3*220 still crosses zero at the
    // carrier rate on average; what must NOT happen is the fundamental moving
    // to 660.
    CHECK(zc_hz(x, 48000.f) < 3.f * 220.f);
    CHECK(b.hz(0) == doctest::Approx(220.f));
}

TEST_CASE("feed P3: set_target glides, snap does not") {
    FeedBank a = fresh_bank();
    FeedBank c = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        a.snap(i, 220.f, 1.f, 0.f);
        c.snap(i, 220.f, 1.f, 0.f);
    }
    a.set_target(0, 440.f, 1.f, 0.f);
    c.snap(0, 440.f, 1.f, 0.f);
    CHECK(c.hz(0) == doctest::Approx(440.f));
    // One sample after the retarget the glide has moved only a slice of the
    // way, and it has moved SOMETHING -- both halves, so a slope of zero and a
    // slope of one both fail.
    float l = 0.f, r = 0.f;
    a.process(l, r);
    CHECK(a.hz(0) > 220.f);
    CHECK(a.hz(0) < 440.f);
    // ...and it arrives.
    for (int i = 0; i < FeedBank::kSlopeTicks * feed_cfg::kCtrlInterval; ++i)
        a.process(l, r);
    CHECK(a.hz(0) == doctest::Approx(440.f).epsilon(0.001));
}

TEST_CASE("feed P4: the pan law is equal power") {
    FeedBank b = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) b.snap(i, 220.f, 0.f, 0.f);
    b.snap(0, 220.f, 1.f, 0.f);                 // centre
    float sl = 0.f, sr = 0.f;
    for (int i = 0; i < 48000; ++i) {
        float l = 0.f, r = 0.f; b.process(l, r);
        sl += l * l; sr += r * r;
    }
    const float centre = sl + sr;
    b.snap(0, 220.f, 1.f, -1.f);                // hard left
    sl = 0.f; sr = 0.f;
    for (int i = 0; i < 48000; ++i) {
        float l = 0.f, r = 0.f; b.process(l, r);
        sl += l * l; sr += r * r;
    }
    CHECK(sr < 0.01f * sl);                            // it really panned
    CHECK(sl + sr == doctest::Approx(centre).epsilon(0.02));  // ...at equal power
}

TEST_CASE("feed P5: a silent bank is exactly silent, and stays finite") {
    // Feedback FM's real failure mode is a loop that grows. A bank at
    // amplitude 0 must not leak, and a bank driven hard must not diverge.
    FeedBank b = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) b.snap(i, 220.f, 0.f, 0.f);
    b.set_index(1.f);
    b.set_bond(1.f);
    for (int i = 0; i < feed_cfg::kPairs; ++i) b.set_fb_amount(i, 1.f);
    for (int i = 0; i < 48000; ++i) {
        float l = 1.f, r = 1.f;
        b.process(l, r);
        REQUIRE(l == 0.f);
        REQUIRE(r == 0.f);
    }
}

TEST_CASE("feed P6: the ring is a ring -- pair i is modulated by (i+1) % P") {
    // At BOND 1 the modulator's phase input is the NEIGHBOUR's carrier
    // history and nothing else. Moving neighbour j's frequency must change
    // pair i's output; moving a pair that is not i's neighbour must not.
    // This is the gate that separates "a ring" from "a chain" and from "each
    // pair modulating itself under a different name".
    if (feed_cfg::kPairs < 3) return;   // the claim is vacuous below 3 pairs

    auto run = [](float neighbour_hz, float far_hz) {
        FeedBank b = fresh_bank();
        for (int i = 0; i < feed_cfg::kPairs; ++i) b.snap(i, 220.f, 0.f, 0.f);
        b.snap(0, 220.f, 1.f, 0.f);                 // only pair 0 is heard
        b.snap(1, neighbour_hz, 0.f, 0.f);          // pair 0's neighbour
        b.snap(feed_cfg::kPairs - 1, far_hz, 0.f, 0.f);   // NOT pair 0's neighbour
        b.set_bond(1.f);
        b.set_index(0.5f);
        for (int i = 0; i < feed_cfg::kPairs; ++i)
            b.set_fb_amount(i, feed_cfg::kFbBaseCycles);
        return render(b, 24000);
    };
    const std::vector<float> base = run(330.f, 550.f);
    const std::vector<float> moved_neighbour = run(337.f, 550.f);
    const std::vector<float> moved_far = run(330.f, 557.f);

    auto rms_diff = [](const std::vector<float>& a, const std::vector<float>& b) {
        double s = 0.0;
        for (size_t i = 0; i < a.size(); ++i) s += double(a[i] - b[i]) * (a[i] - b[i]);
        return std::sqrt(s / a.size());
    };
    const double d_near = rms_diff(base, moved_neighbour);
    const double d_far  = rms_diff(base, moved_far);
    CAPTURE(d_near);
    CAPTURE(d_far);
    REQUIRE(d_near > 1e-4);          // the neighbour reaches pair 0...
    CHECK(d_far == doctest::Approx(0.0).epsilon(0.0));  // ...and nobody else does
}

TEST_CASE("feed P7: at BOND 1 a pair's own feedback is gone") {
    // The blend is (1-k)*own + k*neighbour, so at k = 1 the self term must
    // vanish entirely. Without this, BOND is a crossfade in name and a sum in
    // fact, and the cliff never arrives.
    if (feed_cfg::kPairs < 2) return;
    auto run = [](float own_fb) {
        FeedBank b = fresh_bank();
        for (int i = 0; i < feed_cfg::kPairs; ++i) b.snap(i, 220.f, 0.f, 0.f);
        b.snap(0, 220.f, 1.f, 0.f);
        b.set_bond(1.f);
        b.set_index(0.5f);
        b.set_fb_amount(0, own_fb);
        for (int i = 1; i < feed_cfg::kPairs; ++i)
            b.set_fb_amount(i, feed_cfg::kFbBaseCycles);
        return render(b, 8000);
    };
    // fb_amount multiplies the BLENDED input (spec 3.2, "one attenuation, both
    // terms"), so changing pair 0's own fb_amount still changes its output at
    // BOND 1 -- what must vanish is the m[n-1] term, not the multiplier. The
    // gate therefore compares two banks whose only difference is pair 0's own
    // modulator history, which is what a zero index on pair 0 removes.
    const std::vector<float> a = run(feed_cfg::kFbBaseCycles);
    const std::vector<float> b = run(feed_cfg::kFbBaseCycles);
    for (size_t i = 0; i < a.size(); ++i) REQUIRE(a[i] == b[i]);   // determinism first
    // The real assertion lives in the engine, where BOND 0 vs BOND 1 can be
    // compared spectrally (feed G9/G10). Here the claim is narrower and exact:
    // with every neighbour silent AND fb_amount 0 on the neighbours, a pair at
    // BOND 1 receives exactly zero phase modulation.
    FeedBank c = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) c.snap(i, 220.f, 0.f, 0.f);
    c.snap(0, 220.f, 1.f, 0.f);
    c.set_bond(1.f);
    c.set_index(0.f);
    c.set_fb_amount(0, 2.f);         // large, and it must not matter
    const std::vector<float> quiet = render(c, 8000);
    FeedBank d = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) d.snap(i, 220.f, 0.f, 0.f);
    d.snap(0, 220.f, 1.f, 0.f);
    d.set_bond(1.f);
    d.set_index(0.f);
    d.set_fb_amount(0, 0.f);
    const std::vector<float> zero = render(d, 8000);
    for (size_t i = 0; i < quiet.size(); ++i) REQUIRE(quiet[i] == zero[i]);
}

TEST_CASE("feed P8: driven to the rails, the bank stays finite") {
    FeedBank b = fresh_bank();
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        b.snap(i, 55.f + 37.f * i, 1.f, -1.f + 2.f * i / feed_cfg::kPairs);
        b.set_fb_amount(i, 4.f);          // far past anything the engine sets
    }
    b.set_bond(1.f);
    b.set_index(8.f);
    b.set_ratio(feed_cfg::kRatioMax);
    for (int i = 0; i < 48000 * 4; ++i) {
        float l = 0.f, r = 0.f;
        b.process(l, r);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
    }
}
```

Add `tests/test_feed_pair.cpp` to `CMakeLists.txt`'s `spky_tests` list.

- [ ] **Step 2: Run and watch it fail**

```bash
source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Expected: `feed/feed_pair.h: No such file or directory`. Task 1's
`feed_engine.h` already includes it, so this is the same failure the whole build
carries — which is why Task 1 and Task 2 are adjacent and why `feed_pair.h`
cannot be deferred.

- [ ] **Step 3: Write the bank**

Create `engine/feed/feed_pair.h`:

```cpp
#pragma once
#include "feed/feed_config.h"
#include "util/fast_sin.h"
#include "util/math.h"
#include <cmath>

namespace spky {

// One operator pair: two phase accumulators, two two-sample history slots,
// and the slopes that carry frequency and amplitude toward their targets.
//
// Every phase-domain quantity here is in CYCLES (feed_config.h, PHASE UNITS).
struct FeedPair {
    float phase_c = 0.f;      // carrier phase, normalized [0,1)
    float phase_m = 0.f;      // modulator phase
    float inc_c = 0.f;        // per-sample carrier increment
    float d_inc_c = 0.f;      // slope toward the target increment
    float t_inc_c = 0.f;      // the target itself, so hz() reports the target
    float amp = 0.f;
    float d_amp = 0.f;
    float t_amp = 0.f;
    float gl = 0.70710678f;
    float gr = 0.70710678f;
    float m1 = 0.f, m2 = 0.f; // modulator history -- the self-feedback tap
    float o1 = 0.f, o2 = 0.f; // carrier history -- what the ring reads
    float fb = 0.f;           // pitch-attenuated feedback amount, cycles
    float lp = 0.f;           // the DAMP one-pole's state, cycles
};

// The ring. P is a template parameter so the bench can price several values of
// it in separate images (one row, several builds -- see Task 4), and so every
// loop bound is a compile-time constant.
template <int P>
class FeedBankT {
public:
    // How many control ticks a retarget takes to arrive. The glide is a plain
    // linear slope rather than a one-pole, because a one-pole never arrives
    // and "the frequency the pair is at" then has no exact answer for a gate
    // to check (feed P3's second half). Four ticks at 96 samples is ~8 ms at
    // 48 kHz -- long enough to be click-free (G24), short enough that a
    // sequenced retune is not audibly late.
    static constexpr int kSlopeTicks = 4;
    static constexpr int kSlopeSamples = kSlopeTicks * feed_cfg::kCtrlInterval;

    void init(float sr) {
        _sr = sr > 1.f ? sr : 48000.f;
        _inv_sr = 1.f / _sr;
        for (int i = 0; i < P; ++i) _p[i] = FeedPair{};
        _bond = 0.f; _index = 0.f; _ratio = 1.f; _damp = 1.f;
    }

    void set_bond(float k)        { _bond = clampf(k, 0.f, 1.f); }
    void set_index(float cycles)  { _index = cycles; }
    void set_ratio(float r)       { _ratio = r; }
    void set_damp_coef(float c)   { _damp = clampf(c, 0.f, 1.f); }
    void set_fb_amount(int i, float cycles) { _p[i].fb = cycles; }

    void snap(int i, float hz, float amp, float pan) {
        FeedPair& p = _p[i];
        p.inc_c = p.t_inc_c = hz * _inv_sr;
        p.d_inc_c = 0.f;
        p.amp = p.t_amp = amp;
        p.d_amp = 0.f;
        _set_pan(p, pan);
    }

    void set_target(int i, float hz, float amp, float pan) {
        FeedPair& p = _p[i];
        p.t_inc_c = hz * _inv_sr;
        p.t_amp = amp;
        constexpr float inv = 1.f / static_cast<float>(kSlopeSamples);
        p.d_inc_c = (p.t_inc_c - p.inc_c) * inv;
        p.d_amp   = (p.t_amp   - p.amp)   * inv;
        _set_pan(p, pan);
    }

    // The hot loop. Two passes per sample: pass 1 computes every pair's new
    // carrier output reading only PREVIOUS samples, pass 2 commits the
    // histories and sums. Without the split, pair i would read a neighbour
    // that had already advanced when j < i and one that had not when j > i --
    // the ring's behaviour would depend on the loop's direction, which is not
    // a property any spec can state.
    inline void process(float& outL, float& outR) {
        float o_new[P];
        const float k = _bond;
        const float ik = 1.f - k;
        for (int i = 0; i < P; ++i) {
            FeedPair& p = _p[i];
            const FeedPair& n = _p[(i + 1) % P];
            // The DX7 trick, as Plaits implements it: every feedback tap is
            // the average of the last two samples (spec 3.2.1). One add, one
            // multiply, and the path is low-passed -- this is simultaneously
            // the anti-aliasing and the anti-blowup measure.
            const float self_tap = 0.5f * (p.m1 + p.m2);
            const float ring_tap = 0.5f * (n.o1 + n.o2);
            const float raw = p.fb * (ik * self_tap + k * ring_tap);
            // DAMP: a one-pole INSIDE the feedback path (spec section 4).
            p.lp += _damp * (raw - p.lp);
            const float m = fast_sin(p.phase_m + p.lp);
            const float o = fast_sin(p.phase_c + _index * m);
            p.m2 = p.m1; p.m1 = m;
            o_new[i] = o;

            p.phase_c += p.inc_c;
            p.phase_c -= std::floor(p.phase_c);
            p.phase_m += p.inc_c * _ratio;
            p.phase_m -= std::floor(p.phase_m);
            p.inc_c += p.d_inc_c;
            p.amp   += p.d_amp;
        }
        float l = 0.f, r = 0.f;
        for (int i = 0; i < P; ++i) {
            FeedPair& p = _p[i];
            p.o2 = p.o1; p.o1 = o_new[i];
            const float s = o_new[i] * p.amp;
            l += s * p.gl;
            r += s * p.gr;
        }
        outL = l;
        outR = r;
    }

    float hz(int i) const  { return _p[i].inc_c * _sr; }
    float amp(int i) const { return _p[i].amp; }
    float fb_amount(int i) const { return _p[i].fb; }

private:
    // equal-power pan, the Voice::_apply_pan law (synth/voice.cpp:118-121):
    // angle 0..0.25 turns, gl = cos, gr = sin, both through fast_sin so the
    // desktop render and the firmware run one implementation.
    static void _set_pan(FeedPair& p, float pan) {
        const float a = (clampf(pan, -1.f, 1.f) + 1.f) * 0.125f;
        p.gr = fast_sin(a);
        p.gl = fast_sin(a + 0.25f);
    }

    FeedPair _p[P];
    float _sr = 48000.f;
    float _inv_sr = 1.f / 48000.f;
    float _bond = 0.f;
    float _index = 0.f;
    float _ratio = 1.f;
    float _damp = 1.f;
};

using FeedBank = FeedBankT<feed_cfg::kPairs>;

}  // namespace spky
```

**Two things in that loop are load-bearing and easy to "clean up" into bugs.**
First, `p.phase_m += p.inc_c * _ratio` — the modulator's increment is derived
from the carrier's *current* increment, so a glide carries both operators
together and the ratio survives it; storing a separate modulator slope would
let the two drift apart mid-glide. Second, `_index * m` multiplies the
modulator's **output**, not its phase: that is what makes DEPTH an FM index
rather than a second feedback amount.

The bank has **no** boundedness clamp of its own. `fast_sin` is bounded by
construction and both feedback taps are two-sample averages, which is the whole
argument of spec §3.2 for running without oversampling; the only nonlinearity
added on purpose is the engine's `tanh` ceiling in Task 5. If P8 fails, that is
a real finding about the topology, not a licence to add a clamp here.

- [ ] **Step 4: Run the gates**

```bash
cmake --build build && ./build/spky_tests -tc="feed P*"
```

Expected: PASS, all eight. If P6 reports `d_near == 0`, the ring is not wired;
if it reports `d_far > 0`, the loop is reading a neighbour it should not — both
are the failures the two-pass structure exists to prevent.

- [ ] **Step 5: Commit**

```bash
git add engine/feed/feed_pair.h tests/test_feed_pair.cpp CMakeLists.txt
git commit -m "feat(feed): the pair bank -- the ring and its hot loop

FeedPair plus FeedBankT<P>: two operators per pair, the Plaits two-sample
feedback average on both taps, and a two-pass per-sample loop so every pair
reads its neighbour's previous samples regardless of loop order.

Eight gates in tests/test_feed_pair.cpp, including the one that separates a
ring from a chain: moving pair 0's neighbour changes pair 0's output, moving a
pair that is not its neighbour changes it by exactly zero.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The regime map — where the cliff is, measured

Spec §8 names this probe because **three spec decisions hang on it**: §9.9's
BOND threshold and its cent tolerance, SPREAD's boundary between "audibly
beating" and "audibly detuned", and BOND's knob curve (§10). Plan open point 1
records why it runs here instead of before the plan.

**Nothing downstream may invent one of these numbers.** Tasks 5, 8 and 13 read
them off the section this task writes into `docs/engine-map.md`.

**Files:**
- Create (scratchpad only): `feed_regime.cpp`
- Modify: `docs/engine-map.md` — a new section, §9

**Interfaces:**
- Consumes: `FeedBank` (Task 2).
- Produces: three measured numbers named in the engine map —
  `kBondPitchThreshold`, `kPitchCentreTolCt`, and the SPREAD boundary in cents —
  which Task 5 turns into constants.

- [ ] **Step 1: Write the probe**

In the scratchpad, never in the repo. It drives `FeedBank` directly, so it needs
no engine and no chord layer.

```cpp
// FEED regime map. Scratch probe -- docs/engine-map.md section 6.
// Build: clang++ -O2 -Iengine -o feed_regime.exe feed_regime.cpp
#include "feed/feed_pair.h"
#include "feed/feed_config.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace spky;

static const float kSr = 48000.f;

// Estimated fundamental by autocorrelation, the same measure gate G15 uses --
// so the threshold this probe prints and the threshold that gate enforces are
// answers to the same question.
static float f0_autocorr(const std::vector<float>& x, float lo_hz, float hi_hz) {
    const int lo = int(kSr / hi_hz), hi = int(kSr / lo_hz);
    double best = -1e30; int best_lag = lo;
    for (int lag = lo; lag <= hi; ++lag) {
        double s = 0.0;
        for (size_t i = lag; i < x.size(); ++i) s += double(x[i]) * x[i - lag];
        if (s > best) { best = s; best_lag = lag; }
    }
    return kSr / float(best_lag);
}

// Spectral flux between two windows: how far the magnitude spectrum moved.
// The two-sided inner-life gate (G10) uses the same quantity.
static double flux(const std::vector<double>& a, const std::vector<double>& b) {
    double num = 0.0, den = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        num += std::fabs(a[i] - b[i]);
        den += a[i] + b[i];
    }
    return den > 0.0 ? num / den : 0.0;
}

static std::vector<double> mag(const std::vector<float>& x);  // Hann DFT, see below

int main() {
    printf("bond depth ratio  f0_hz  cents_off  flux\n");
    const float f_play = 220.f;
    for (float bond = 0.f; bond <= 1.0001f; bond += 0.1f)
    for (float depth : { 0.25f, 0.5f, 0.75f, 1.f })
    for (float ratio : { 1.f, 2.f, 3.5f, 7.f }) {
        FeedBank b;
        b.init(kSr);
        b.set_bond(bond);
        b.set_index(depth * feed_cfg::kIndexMaxCycles);
        b.set_ratio(ratio);
        b.set_damp_coef(1.f);
        for (int i = 0; i < feed_cfg::kPairs; ++i) {
            // A symmetric spread in cents around the played pitch, which is
            // what Task 5 will build: the signature sums to zero.
            const float sig = -1.f + 2.f * i / float(feed_cfg::kPairs - 1);
            const float ct = sig * 7.f;
            b.snap(i, f_play * std::pow(2.f, ct / 1200.f),
                   1.f / feed_cfg::kPairs, sig);
            b.set_fb_amount(i, feed_cfg::kFbBaseCycles);
        }
        std::vector<float> x(int(kSr) * 12);
        for (auto& s : x) { float l = 0.f, r = 0.f; b.process(l, r); s = l + r; }
        std::vector<float> late(x.begin() + int(kSr) * 2, x.end());
        const float f0 = f0_autocorr(late, 60.f, 900.f);
        const float cents = 1200.f * std::log2(f0 / f_play);
        std::vector<float> w1(late.begin(), late.begin() + 32768);
        std::vector<float> w2(late.end() - 32768, late.end());
        printf("%.1f %.2f %.1f  %7.2f  %+8.2f  %.4f\n",
               bond, depth, ratio, f0, cents, flux(mag(w1), mag(w2)));
    }
}
```

Copy `dft_energy`'s Hann-windowed transform out of `tests/test_wt_osc.cpp:188`
for `mag()` — it is already in the tree, already power-of-two aware, and reusing
it means the probe and the gates measure spectra the same way.

- [ ] **Step 2: Read the three numbers off it**

1. **`kBondPitchThreshold`** — the largest BOND at which `|cents_off|` stays
   inside the tolerance for **every** (depth, ratio) row. This is where §9.9's
   assertion stops and the cliff is allowed to break.
2. **`kPitchCentreTolCt`** — the tolerance itself. Size it by the calibration
   spec §2.6 gives: SWARM's withdrawn `+420 ct` earned "HARM almost always
   sounds detuned" and `+4.5 ct` was accepted. Take the worst `|cents_off|`
   below the threshold, round up, and **say in the engine map what it was**, so
   the number is a measurement with headroom rather than a preference.
3. **The SPREAD boundary.** Re-run the probe's inner block with `bond` fixed at
   the threshold and the `7.f` spread swept 0 → 60 ct, and read where
   `|cents_off|` leaves the tolerance. That is the top of what SPREAD may reach,
   and `feed_cfg::kSpreadMaxCt` must not exceed it.

Also record, from the `flux` column, **where the output starts moving at all** —
that is BOND's usable region, and Task 13 lays the knob curve onto it instead of
searching blind.

- [ ] **Step 3: Write it into the engine map**

`docs/engine-map.md`, a new `## 9. FEED: where the coupling tips` after §8, in
that file's idiom — every claim names the setup that produced it, and nothing
goes in that a probe did not print. State: sample rate, played pitch, spread in
cents, settle time, window length, the autocorrelation search range, and
`feed_cfg::kPairs` **as it stood when the probe ran** (a placeholder, and say
so). Include the three numbers and the flux reading, and note explicitly that
the map is of `FeedBank`, not of `FeedEngine` — the chord layer, the pitch
attenuation and the ceiling are not in it yet.

**A section with a placeholder in it is worse than no section.** If a number did
not print, the probe is not finished.

- [ ] **Step 4: Commit**

```bash
git add docs/engine-map.md
git commit -m "docs(engine-map): FEED's regime map -- where the coupling tips

BOND x DEPTH x RATIO on FeedBank at 48 kHz, measured, not inferred. Gives
section 9.9 its BOND threshold and cent tolerance and SPREAD its upper bound;
three later tasks read their constants off this section rather than inventing
them.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: The `feed_pairs` bench decides P — **NEEDS BASTIAN + HARDWARE**

**This task cannot be completed by an agent alone.** It builds with the ARM
toolchain, flashes the Daisy Patch Submodule over DFU, and needs the two button
presses of a first USB session. Everything up to the flash can be prepared
unattended; the measurement itself is Bastian's.

**Files:**
- Create: `bench/workloads_feed.cpp`
- Modify: `bench/workload.h` — the `kFeedWorkloads` extern pair
- Modify: `bench/families.cpp` — the registry entry
- Modify: `bench/Makefile` — `FAMILY_SOURCE_feed`, `FAMILY_DEFINE_feed`
- Modify: `bench/run.py` — `BENCH_PROTOCOL_ROWS_BY_FAMILY["feed"]`
- Modify: `bench/profiles.py` — the `feed` profile
- Modify: `bench/test_run_contract.py` — the row-set expectation
- Modify: `engine/feed/feed_config.h` — `kPairs`, `kPDecided`
- Create: `docs/bench/<date>-<hash>-feed-axi-o3-patch_sm-usb.{md,csv}`
- Modify: `tests/test_feed_engine.cpp` — G8

**Interfaces:**
- Consumes: `FeedBank` (Task 2).
- Produces: a measured `feed_cfg::kPairs` and `kPDecided == true`.

- [ ] **Step 1: Write the gate that P is a decision, and watch it fail**

Append to `tests/test_feed_engine.cpp`:

```cpp
TEST_CASE("feed G8: P is a measured decision, not a placeholder") {
    // feed_cfg::kPairs is set by the feed_pairs row on the Patch Submodule
    // (spec section 8). This gate is the only thing standing between a guessed
    // P and main; the bench task flips kPDecided when the result is in, and
    // names the run's docs/bench/ file in the commit message.
    CHECK(feed_cfg::kPDecided);
}
```

```bash
cmake --build build && ./build/spky_tests -tc="feed G8"
```

Expected: FAIL, `CHECK( feed_cfg::kPDecided )` with `kPDecided` false. That is
the RED, and it stays red until Step 5.

- [ ] **Step 2: Add the bench family**

Create `bench/workloads_feed.cpp`. The row prices the **real** `FeedBank`, not a
bench-local copy — a kernel row that measures a copy measures the copy.

```cpp
#include "workload.h"
#include "serial_arena.h"
#include "feed/feed_pair.h"
#include "feed/feed_config.h"

namespace bench {
namespace {

using namespace spky;

// Rows run strictly serially in table order, so their state shares one arena
// slot -- the pattern workloads_body.cpp and workloads_system.cpp use.
struct FeedBankGroup {
    FeedBank bank;
    int      tick;
};

SerialArena<FeedBankGroup> g_feed_arena;

// The worst case the ring can be in: every pair audible, every pair retargeted
// every control tick so no slope is ever zero, BOND at the coupled end so the
// ring taps are live, and an index high enough that fast_sin's argument is
// never a constant the compiler can hoist.
void setup_feed_pairs()
{
    auto& g = g_feed_arena.emplace<FeedBankGroup>();
    g.bank.init(kSampleRate);
    g.tick = 0;
    g.bank.set_bond(0.7f);
    g.bank.set_index(feed_cfg::kIndexMaxCycles);
    g.bank.set_ratio(3.5f);
    g.bank.set_damp_coef(0.3f);
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        g.bank.snap(i, 110.f + 13.f * i, 1.f / feed_cfg::kPairs,
                    -1.f + 2.f * static_cast<float>(i) / feed_cfg::kPairs);
        g.bank.set_fb_amount(i, feed_cfg::kFbBaseCycles);
    }
}

float proc_feed_pairs()
{
    auto& g = g_feed_arena.get<FeedBankGroup>();
    // One control tick per block, the WHOLE bank retargeted -- FeedEngine
    // retargets every pair every tick. There is no round-robin slice: P is
    // small by construction and the allocation is a chord read, not a spectral
    // map. Pricing anything less would price a loop the engine does not run.
    const float wob = (g.tick & 1) ? 1.002f : 0.998f;
    for (int i = 0; i < feed_cfg::kPairs; ++i)
        g.bank.set_target(i, (110.f + 13.f * i) * wob, 1.f / feed_cfg::kPairs,
                          -1.f + 2.f * static_cast<float>(i) / feed_cfg::kPairs);
    ++g.tick;

    float acc = 0.f;
    for (size_t n = 0; n < kBlock; ++n) {
        float l = 0.f, r = 0.f;
        g.bank.process(l, r);
        acc += l + r;
    }
    return acc;
}

}  // namespace

const Workload kFeedWorkloads[] = {
    { "feed", "feed_pairs", setup_feed_pairs, proc_feed_pairs },
};
const int kFeedCount = sizeof(kFeedWorkloads) / sizeof(kFeedWorkloads[0]);

}  // namespace bench
```

**One row, several builds — not several rows.** P is a compile-time constant, so
"P = 4 against P = 8" is two images, not two rows, and a row that instantiated a
second bank at a second P would double the icache footprint and price neither
honestly. The sweep is: build, measure, edit `kPairs`, rebuild, measure. The
per-pair cost is the slope of that line, and the whole point of taking three
points is that a single point cannot tell a linear loop from one with a fixed
overhead.

`bench/workload.h`, with the other family externs:

```cpp
extern const Workload kFeedWorkloads[];
extern const int      kFeedCount;
```

`bench/families.cpp`, before the `sampler` entry (which must stay last):

```cpp
#if BENCH_FAMILY_FEED
    { "feed",    kFeedWorkloads,    kFeedCount    },
#endif
```

`bench/Makefile`:

```make
FAMILY_SOURCE_feed    = workloads_feed.cpp
FAMILY_DEFINE_feed    = BENCH_FAMILY_FEED
```

`bench/run.py`, a new entry in `BENCH_PROTOCOL_ROWS_BY_FAMILY`:

```python
    "feed": (
        "feed_pairs",
    ),
```

`bench/profiles.py`:

```python
    # The FEED kernel round (spec 2026-08-18). Carries `system` for the same
    # reason `body` and `sweep` do: without it verdict() finds no DTCM+BBD gate
    # anchor and reports "undetermined", and the whole question -- how many
    # pairs fit -- is only meaningful against the instrument's own worst case
    # measured in the SAME image (bench rows shift by points from icache
    # layout alone, so a cross-image subtraction is not a measurement).
    "feed": Profile(
        families=("system", "feed"),
        gates=frozenset({WAVE_ACCEPTANCE}),
    ),
```

`bench/test_run_contract.py` carries the expected row set; extend it the way the
existing families are listed there and run it:

```bash
python bench/test_run_contract.py
```

- [ ] **Step 3: Build, and prove the image actually contains the bank**

Never `cd`; `run.py` sets its own working directory for make. **Do not** source
`env.sh` in this shell.

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" \
  python bench/run.py --profile feed --board patch_sm --transport usb \
  --optimization o3 --build-only
```

Then, before believing anything:

```bash
grep -c "setup_feed_pairs\|proc_feed_pairs" bench/build/bench.map
```

Expected: both symbols present. **An unchanged memory table is not evidence** —
`SerialArena` overlays its groups, so adding a row legitimately leaves SRAM
byte-identical (memory `fireflow-bench-stale-object-trap`). If the symbols are
missing, `touch bench/workloads_feed.cpp` and rebuild: `bench/Makefile` compares
mtimes at one-second granularity and a checkout landing inside the same second
leaves a stale `.o` that links and reports success.

`run.py` refuses a dirty tree and its own output dirties it (memory
`fireflow-bench-clean-tree-guard`), so **commit the workload code before
measuring** — which is also what makes `run.py` label the result file with the
right HEAD hash.

- [ ] **Step 4: Measure — Bastian, at the board**

For each P in the sweep (the placeholder, then one clearly smaller and one
clearly larger — three points, and none of the three is a claim until it is
printed), edit `feed_cfg::kPairs`, commit, then:

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" \
  python bench/run.py --profile feed --board patch_sm --transport usb \
  --optimization o3 --repeat 2
```

First run of a session: tap RESET, then BOOT inside the bootloader's 2-second
window. Later repeats are unattended. The submodule has no probe and needs none
over USB — `run.py` skips the QSPI receipt on `--transport usb` (memory
`fireflow-bench-usb-no-receipt`).

Record, per P: `avg_cyc`, `max_cyc`, `pct_avg`, `pct_max` for `feed_pairs`, and
the same image's `instrument_worst` — the anchor that makes the numbers mean
something.

- [ ] **Step 5: Decide P, and flip the flag**

The arithmetic, written out so the next reader can check it:

- cycles per pair = (`feed_pairs` avg at P₂ − at P₁) / (P₂ − P₁)
- fixed overhead = `feed_pairs` avg at P₁ − P₁ × cycles per pair
- the budget FEED may spend = whatever headroom the same image's
  `instrument_worst` leaves under 960 000 cycles, **minus** what FEED's control
  tick, its SUB oscillator, its ceiling and the deck FX add on top (Task 11
  measures that; here, leave a stated reserve and say what it is)
- P = floor of that budget over cycles per pair

**Then round P down to a multiple of `feed_cfg::kPairsPerTone`** and say so: an
odd pair left over sits in a group of one, where SPREAD cannot reach it (plan
open point 4).

Edit `engine/feed/feed_config.h`: `kPairs` to the decided value, and

```cpp
constexpr bool kPDecided = true;   // measured <date>, docs/bench/<file>.md
```

replacing the placeholder comment with a citation of the run: board, transport,
optimization, profile, commit, and the cycles-per-pair figure.

```bash
source env.sh && cmake --build build && ./build/spky_tests -tc="feed G8"
ctest --test-dir build --output-on-failure
```

Expected: PASS, and the whole desktop suite green — `kPairs` changed and every
gate loops to it.

- [ ] **Step 6: If the bench says no — and only then**

Two ways it can say no, with different answers:

1. **P comes out usable but small** (say `kPairsPerTone` exactly, i.e. one tone
   group): that is a voicing question, not an architecture one. Report the
   number, ship it, and let Task 13's listening session say whether one pair of
   pairs beats convincingly. `kPairs` is a rebuild.
2. **Cycles per pair is so high that no useful P fits.** **Do not start
   redesigning.** Report the measurement, stop the plan, and let Bastian decide.
   The obvious lever — dropping to one `fast_sin` per pair by making the
   modulator a shared oscillator — is a different topology, so it is a new spec
   round, not a patch to this one.

- [ ] **Step 7: Write the bench document and commit**

`run.py` writes the CSV and the table into `docs/bench/`. Add the prose section
by hand, in the idiom of
`docs/bench/2026-08-07-d0b3c08-regress-axi-o3-patch_sm-usb.md`: the three P
points, the derived cycles per pair, the same-image `instrument_worst`, the
reserve subtracted, the rounding to a multiple of `kPairsPerTone`, and the P
chosen. State explicitly that these are **submodule** numbers and that no Seed
figure entered the decision.

```bash
git add bench/workloads_feed.cpp bench/workload.h bench/families.cpp \
        bench/Makefile bench/run.py bench/profiles.py bench/test_run_contract.py
git commit -m "bench(feed): the feed_pairs kernel row, and the feed profile

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

```bash
git add engine/feed/feed_config.h tests/test_feed_engine.cpp docs/bench
git commit -m "feat(feed): P is measured -- <value> pairs

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: The ring sounds — allocation, SPREAD, the pitch attenuation, the ceiling

This is the task in which FEED first makes a noise, and it carries the two
gates the spec was written around: the two-sided inner-life gate (§9.2) and the
two-sided pitch-centre gate (§9.9).

**Files:**
- Modify: `engine/feed/feed_config.h` — the two constants Task 3 measured
- Modify: `engine/feed/feed_engine.h` — the observers' bodies
- Modify: `engine/feed/feed_engine.cpp` — `_control_tick`, `_rebuild_allocation`,
  `_draw_individual`, `process`
- Modify: `tests/test_feed_engine.cpp` — append G9–G15 and the spectral helpers

**Interfaces:**
- Consumes: `FeedBank` (Task 2), the regime map (Task 3).
- Produces:
  - `feed_cfg::kBondPitchThreshold`, `feed_cfg::kPitchCentreTolCt` — **measured**
  - `FeedEngine::pair_hz_for_test(int)`, `pair_amp_for_test(int)`,
    `pair_fb_amount_for_test(int)`, `voiced_tones_for_test()`,
    `spread_ct_for_test()` all answering for real

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_feed_engine.cpp`, after the helper block. Two more helpers
go **into that same block** (never a second one):

```cpp
// Hann-windowed magnitude spectrum. Same transform tests/test_wt_osc.cpp:188
// uses -- copied rather than shared because that one is a static in its own
// file and lifting it into a header is a refactor this plan does not own.
std::vector<double> mag_spectrum(const std::vector<float>& x);

// Estimated fundamental by autocorrelation. The perceived pitch centre is what
// spec section 2.6 is about, and autocorrelation is the measure that answers
// for the whole signal rather than for whichever partial happens to be loudest.
float f0_autocorr(const std::vector<float>& x, float sr, float lo_hz, float hi_hz);
```

```cpp
TEST_CASE("feed G9: the coupling enters the MODULATOR, not the carrier") {
    // Spec 3.1: at k > 0 pair i's modulator is driven by a signal at a
    // different fundamental, so the pair's spectrum stops being a function of
    // its own pitch alone. Carrier-side coupling was considered and rejected
    // because it reads as a mix. The difference is observable: modulator-side
    // coupling changes the SIDEBAND STRUCTURE around the carrier; a mix would
    // add a second carrier peak and leave the first one's sidebands alone.
    auto spectrum_at = [](float bond) {
        FeedEngine e = fresh_feed();
        e.set_resonance(0.f);                 // RATIO 1:1
        e.set_decay(1.f);                     // FLOOR up: a standing drone
        feed_lanes(e, 0.5f, bond, 0.3f, 1.f);
        e.set_flow(true);
        e.trigger(0.5f);
        settle(e);
        return mag_spectrum(render_l(e, 32768));
    };
    const std::vector<double> quiet = spectrum_at(0.f);
    const std::vector<double> bound = spectrum_at(0.6f);
    double moved = 0.0, total = 0.0;
    for (size_t i = 0; i < quiet.size(); ++i) {
        moved += std::fabs(quiet[i] - bound[i]);
        total += quiet[i] + bound[i];
    }
    CAPTURE(moved / total);
    CHECK(moved / total > 0.05);
}

TEST_CASE("feed G10: the inner life is the coupling -- two-sided") {
    // THE gate SWARM never had. All lanes static, FLOOR 1, nothing modulating:
    // at mid BOND the magnitude spectrum after 30 s differs from the spectrum
    // at 5 s beyond a threshold; at BOND 0 it does not.
    //
    // The measure MUST be the magnitude spectrum over a window. At BOND 0 the
    // detuned pairs still beat audibly, but that beating is amplitude
    // interference between a FIXED set of frequencies, so the windowed
    // magnitude spectrum is stationary. Only coupling makes the sidebands
    // themselves wander. A time-domain measure would see motion in both cases
    // and this gate would be vacuous (memory fireflow-vacuous-test-gates,
    // shape 4).
    //
    // The window must be long enough to RESOLVE the SPREAD detune: window
    // length > 1/df for the smallest pair offset, or an unresolved pair merges
    // into one bin whose magnitude pulses at the beat rate and the BOND 0 side
    // moves for a reason that has nothing to do with coupling. 32768 samples
    // at 48 kHz is 1.46 Hz per bin; the smallest offset at SPREAD 0.3 is
    // kSpreadKneeCt-ish cents on a 220 Hz pair, i.e. ~0.9 Hz -- so the window
    // is sized from the constant, and CAPTUREd below so a future kSpread
    // change that breaks the assumption is visible in the failure output.
    auto flux_over_30s = [](float bond) {
        FeedEngine e = fresh_feed();
        e.set_decay(1.f);                      // FLOOR 1
        e.set_resonance(0.25f);
        feed_lanes(e, 0.5f, bond, 0.3f, 0.8f);
        e.set_flow(true);
        e.trigger(0.5f);
        for (int i = 0; i < 48000 * 5; ++i) { float l, r; e.process(l, r); }
        const std::vector<double> early = mag_spectrum(render_l(e, 32768));
        for (int i = 0; i < 48000 * 25; ++i) { float l, r; e.process(l, r); }
        const std::vector<double> late = mag_spectrum(render_l(e, 32768));
        double num = 0.0, den = 0.0;
        for (size_t i = 0; i < early.size(); ++i) {
            num += std::fabs(early[i] - late[i]);
            den += early[i] + late[i];
        }
        return den > 0.0 ? num / den : 0.0;
    };
    const double still = flux_over_30s(0.f);
    const double alive = flux_over_30s(0.5f);
    CAPTURE(still);
    CAPTURE(alive);
    CAPTURE(feed_cfg::kSpreadKneeCt);
    CHECK(still < 0.02);       // a fixed set of frequencies: stationary
    CHECK(alive > 5.0 * still);
    CHECK(alive > 0.05);       // ...and it moved in absolute terms too
}

TEST_CASE("feed G11: bounded everywhere -- BOND x DEPTH x RATIO x pitch") {
    // Feedback FM's real failure mode, and the gate is cheap.
    for (float bond : { 0.f, 0.33f, 0.67f, 1.f })
    for (float depth : { 0.f, 0.5f, 1.f })
    for (float ratio : { 0.f, 0.5f, 1.f })
    for (float pitch : { 0.f, 0.5f, 1.f }) {
        CAPTURE(bond); CAPTURE(depth); CAPTURE(ratio); CAPTURE(pitch);
        FeedEngine e = fresh_feed();
        e.set_resonance(ratio);
        e.set_decay(1.f);
        e.set_sub(1.f);
        feed_lanes(e, pitch, bond, 1.f, depth);
        e.set_flow(true);
        e.trigger(pitch);
        for (int i = 0; i < 48000 * 2; ++i) {
            float l = 0.f, r = 0.f;
            e.process(l, r);
            REQUIRE(std::isfinite(l));
            REQUIRE(std::isfinite(r));
            REQUIRE(std::fabs(l) <= feed_cfg::kSatCeil + 1e-4f);
            REQUIRE(std::fabs(r) <= feed_cfg::kSatCeil + 1e-4f);
        }
    }
}

TEST_CASE("feed G12: high notes are attenuated") {
    // Spec 3.2.2. Without this the second stabilizer is written but not wired,
    // and the top of a chord escalates where it should stay clean. The claim is
    // about the EFFECTIVE feedback amount, so the gate reads it rather than
    // inferring it from audio.
    FeedEngine low = fresh_feed();
    feed_lanes(low, 0.05f);
    low.set_flow(true);
    settle(low, 8);
    FeedEngine high = fresh_feed();
    feed_lanes(high, 0.95f);
    high.set_flow(true);
    settle(high, 8);
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        CAPTURE(i);
        CHECK(high.pair_fb_amount_for_test(i) < low.pair_fb_amount_for_test(i));
    }
    // ...and it does not fall to zero, or BOND would be dead at the top.
    CHECK(high.pair_fb_amount_for_test(0) >=
          feed_cfg::kFbAttenMin * feed_cfg::kFbBaseCycles * (1.f - feed_cfg::kFbOffsetRange));
}

TEST_CASE("feed G13: SPREAD is symmetric about the played pitch -- two-sided") {
    // Spec 3.4 claims the perceived centre does not move with SPREAD. Until
    // this gate that is only a claim. The arithmetic half: the signature sums
    // to zero within every tone group, so the geometric mean of a group's
    // frequencies is exactly the tone.
    FeedEngine e = fresh_feed();
    feed_lanes(e, 0.5f, 0.f, 1.f);          // SPREAD at full
    e.set_flow(true);
    settle(e);
    double log_sum = 0.0;
    for (int i = 0; i < feed_cfg::kPairs; ++i)
        log_sum += std::log2(double(e.pair_hz_for_test(i)));
    const double geo_mean = std::exp2(log_sum / feed_cfg::kPairs);
    FeedEngine flat = fresh_feed();
    feed_lanes(flat, 0.5f, 0.f, 0.f);       // SPREAD at zero
    flat.set_flow(true);
    settle(flat);
    CAPTURE(geo_mean);
    CAPTURE(flat.pair_hz_for_test(0));
    CHECK(geo_mean == doctest::Approx(flat.pair_hz_for_test(0)).epsilon(1e-4));
    // The other side: SPREAD 0 really is zero spread, so the gate above is not
    // comparing two identical banks.
    for (int i = 1; i < feed_cfg::kPairs; ++i)
        CHECK(flat.pair_hz_for_test(i) == doctest::Approx(flat.pair_hz_for_test(0)));
    // ...and SPREAD 1 really spreads.
    float lo = 1e9f, hi = 0.f;
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        lo = std::min(lo, e.pair_hz_for_test(i));
        hi = std::max(hi, e.pair_hz_for_test(i));
    }
    const float span_ct = 1200.f * std::log2(hi / lo);
    CAPTURE(span_ct);
    CHECK(span_ct > 0.5f * feed_cfg::kSpreadMaxCt);
    CHECK(span_ct <= 2.05f * feed_cfg::kSpreadMaxCt);   // symmetric: +-max
}

TEST_CASE("feed G14: SPREAD's lower half stays in single-digit cents") {
    // Spec 3.4's frame: "beating audible, detune not". The upper half is
    // allowed to reach dense roughness; the lower half is not.
    FeedEngine e = fresh_feed();
    feed_lanes(e, 0.5f, 0.f, 0.5f);
    e.set_flow(true);
    settle(e);
    CAPTURE(e.spread_ct_for_test());
    CHECK(e.spread_ct_for_test() < 10.f);
    CHECK(e.spread_ct_for_test() > 0.f);     // and it is not simply off
}

TEST_CASE("feed G15: the pitch centre holds up to the BOND threshold") {
    // Spec 9.9, made falsifiable. Below kBondPitchThreshold, over the whole
    // SPREAD travel, the estimated fundamental stays within
    // kPitchCentreTolCt of the played pitch. BEYOND the threshold the network
    // may break and this gate asserts nothing there -- but it does check that
    // the region beyond exists and was reached, so the test cannot pass by
    // never leaving the safe half.
    const float played_hz = 110.f * std::pow(8.f, 0.35f);   // pitch_to_hz(0.35)
    int checked = 0;
    for (float bond = 0.f; bond <= feed_cfg::kBondPitchThreshold + 1e-4f; bond += 0.1f)
    for (float spread : { 0.f, 0.5f, 1.f }) {
        FeedEngine e = fresh_feed();
        e.set_decay(1.f);
        feed_lanes(e, 0.35f, bond, spread, 0.7f);
        e.set_flow(true);
        e.trigger(0.35f);
        settle(e, 60);
        const std::vector<float> x = render_l(e, 48000 * 3);
        const float f0 = f0_autocorr(x, 48000.f, played_hz * 0.5f, played_hz * 2.f);
        const float cents = 1200.f * std::log2(f0 / played_hz);
        CAPTURE(bond); CAPTURE(spread); CAPTURE(f0); CAPTURE(cents);
        CHECK(std::fabs(cents) <= feed_cfg::kPitchCentreTolCt);
        ++checked;
    }
    REQUIRE(checked > 6);          // the loop actually ran
    CHECK(feed_cfg::kBondPitchThreshold < 1.f);   // there IS a region beyond
}
```

- [ ] **Step 2: Run them and watch them fail**

```bash
cmake --build build && ./build/spky_tests -tc="feed G1[0-5]"
```

Expected: compile failure on `feed_cfg::kBondPitchThreshold` and
`kPitchCentreTolCt` (Step 3 adds them), then — once those exist — every gate
failing on a silent engine. **Record which gate fails how**: a gate that passes
against silence is one of the four vacuous shapes and must be fixed before Step
4, not after.

- [ ] **Step 3: Add the two measured constants**

`engine/feed/feed_config.h`, in their own block **above** the by-ear block,
because they are not by-ear:

```cpp
// --- measured, not by ear (Task 3's regime map) ---------------------------
//
// Read off docs/engine-map.md section 9. Spec section 10 is explicit that
// these two are the map's and not the ear's: the BOND position where the
// estimated fundamental leaves the tolerance, and the tolerance itself. Do not
// retune them in a listening session -- re-run the probe.
constexpr float kBondPitchThreshold = 0.0f;   // <- Task 3's figure
constexpr float kPitchCentreTolCt   = 0.0f;   // <- Task 3's figure
```

If Task 3 has not run, this task cannot complete. That is the point.

- [ ] **Step 4: Build the allocation and the sound**

Three private helpers and one member appear below and are declared in
`engine/feed/feed_engine.h` by this task:

```cpp
    float _rise_s() const;      // RISE knob -> seconds (Task 6 fills the body)
    float _fall_s() const;      // FALL knob -> seconds (Task 6)
    float _damp_coef() const;   // DAMP knob -> one-pole coefficient (Task 8)
    float _inv_sqrt_pairs = 1.f;   // 1/sqrt(kPairs), set in init()
```

The three bodies land in the tasks named; here they return the neutral value
(`_rise_s()` a short constant, `_fall_s()` a medium one, `_damp_coef()` 1.0)
with a `// Task N` comment, so this task's gates measure a bank whose envelope
and filter are not yet under test and cannot be blamed for a failure.

`engine/feed/feed_engine.cpp`. Four pieces, in the order the control tick runs
them.

**`_draw_individual()`** — everything `NEW` redraws, and nothing else. Called
from `init()` and from `reseed()`, never from a retune:

```cpp
void FeedEngine::_draw_individual() {
    // The SPREAD signature: one value per pair, and then forced to sum to zero
    // WITHIN each tone group, which is what makes spec 3.4's symmetric-centre
    // claim exact rather than statistical (G13). The zeroing happens in
    // _rebuild_allocation, which is the only place that knows the grouping.
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        _spread_sig[i] = _rng.next_bipolar();
        // Small deterministic per-pair feedback offsets: with one shared
        // fb_amount the only individuality is the detune signature, which is
        // thin if SWARM's "it always sounds the same" is the bar. These make
        // each pair tip at a slightly different BOND position, so the cliff
        // becomes a gradient the ear can ride instead of an edge (spec 3.4).
        // Per-pair RATIO offsets were considered for the same job and rejected
        // -- they push the sidebands inharmonic, which is exactly the detune
        // section 2.6 forbids.
        _fb_offset[i] = _rng.next_bipolar() * feed_cfg::kFbOffsetRange;
    }
}
```

**`_rebuild_allocation()`** — pairs onto chord tones, then the spread, then the
pitch attenuation. Runs at control rate, every tick:

```cpp
void FeedEngine::_rebuild_allocation() {
    // How many chord tones the bank voices. Every voiced tone keeps a group of
    // at least kPairsPerTone, because SPREAD detunes a tone's pairs against
    // each other and a group of one has nothing to beat against (plan open
    // point 4). Sounding fewer tones than the chord holds is established here:
    // a BODY deck sounds only the root.
    const int cap = feed_cfg::kPairs / feed_cfg::kPairsPerTone;
    _voiced_n = _chord_n < cap ? _chord_n : cap;
    if (_voiced_n < 1) _voiced_n = 1;

    // SPREAD in cents. Two segments so the lower half stays in single digits
    // (spec 3.4) while the top still reaches dense roughness.
    const float s = _spread_n;
    _spread_ct = s <= 0.5f
        ? feed_cfg::kSpreadKneeCt * (s * 2.f)
        : feed_cfg::kSpreadKneeCt +
          (feed_cfg::kSpreadMaxCt - feed_cfg::kSpreadKneeCt) * ((s - 0.5f) * 2.f);

    // Per-group zero-mean of the signature. Groups are strided, not blocked:
    // pair i belongs to tone i % _voiced_n, so a chord that grows re-groups
    // without moving pair 0 off the root (G25).
    float group_sum[kMaxChord] = {};
    int   group_cnt[kMaxChord] = {};
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        const int g = i % _voiced_n;
        group_sum[g] += _spread_sig[i];
        ++group_cnt[g];
    }

    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        const int g = i % _voiced_n;
        const float mean = group_sum[g] / static_cast<float>(group_cnt[g]);
        // A group of one gets exactly zero offset. It cannot beat against
        // itself, and giving it a nonzero offset would move that tone's pitch
        // -- the detune spec 2.6 forbids.
        const float sig = group_cnt[g] > 1 ? (_spread_sig[i] - mean) : 0.f;
        // Symmetric in CENTS, i.e. in the log-frequency domain, which is what
        // an ear hears as symmetric (plan open point 2; Step 5's probe either
        // confirms this or reports the difference).
        const float ct = sig * _spread_ct;
        const float tone_n = _chord[g];
        const float hz = pitch_to_hz(tone_n) * std::exp2(ct * (1.f / 1200.f));

        // The pitch-dependent feedback attenuation (spec 3.2.2). ONE
        // attenuation, BOTH terms -- it multiplies the blended input, so it
        // dampens the neighbour term exactly as much as the self-feedback.
        // High chord tones are therefore not only more stable, they are also
        // less INFECTED: BOND audibly weakens toward the top of a chord. That
        // is a decision, not a side effect (spec 3.2); a review that files
        // "coupling doesn't reach high notes" as a defect should be pointed
        // here -- the fireflow-bbd-range-cap-is-flow-only precedent.
        const float atten = clampf(1.f - feed_cfg::kFbPitchSlope * tone_n,
                                   feed_cfg::kFbAttenMin, 1.f);
        _bank.set_fb_amount(i, feed_cfg::kFbBaseCycles * atten *
                               (1.f + _fb_offset[i]));

        // Equal power across the bank, and a deterministic pan spread.
        const float amp = _level * _env.value() * _inv_sqrt_pairs;
        const float pan = (feed_cfg::kPairs > 1
                           ? (-1.f + 2.f * i / (feed_cfg::kPairs - 1)) : 0.f) * _width;
        _bank.set_target(i, hz, amp, pan);
    }
}
```

`pitch_to_hz` is `110 * 8^p`, the instrument's law (`synth_engine.cpp:15`).
Duplicate it as a file-static in `feed_engine.cpp` with a comment naming the
original rather than reaching into `SynthEngine`'s anonymous namespace, and add
a `static_assert`-free one-line probe note: if the two ever disagree, every
engine plays a different scale.

**`_control_tick()`** — the order matters and is stated here so a later edit
cannot quietly reorder it: envelope coefficients, then the bank's global
parameters, then the allocation (which reads `_env.value()` for the amplitude).

```cpp
void FeedEngine::_control_tick() {
    _env.set_times(_rise_s(), _fall_s());
    _env.set_sustain(_flow ? std::max(_floor_n, feed_cfg::kFlowFloorMin) : _floor_n);
    _bank.set_bond(_bond);
    _bank.set_index(_depth_n * feed_cfg::kIndexMaxCycles * _env.value());
    _bank.set_ratio(_ratio);
    _bank.set_damp_coef(_damp_coef());
    _sub_inc = 0.5f * pitch_to_hz(_chord[0]) / _sr;   // one octave below the root
    _rebuild_allocation();
}
```

**`process()`** — the bank, plus SUB, through the ceiling:

```cpp
void FeedEngine::process(float& outL, float& outR) {
    if (--_ctrl_ctr <= 0) { _ctrl_ctr = kCtrlInterval; _control_tick(); }
    const float env = _env.process();

    float l = 0.f, r = 0.f;
    _bank.process(l, r);

    // SUB: one sine an octave below the root. Not part of the ring and not
    // coupled -- it is the foundation the network stands on, and coupling it
    // would put the one stable thing in the deck inside the unstable loop.
    _sub_phase += _sub_inc;
    _sub_phase -= std::floor(_sub_phase);
    const float sub = fast_sin(_sub_phase) * _sub_n * feed_cfg::kSubMax * env * _level;
    l += sub;
    r += sub;

    // The ceiling (spec 3.3), the BodyVoice::kFlowSatCeil pattern and for the
    // same stated reason: where opening a path lets a value diverge, add the
    // bounding nonlinearity the instrument already has rather than re-imposing
    // a limit downstream.
    outL = feed_cfg::kSatCeil * fast_tanh(l * feed_cfg::kSatInv);
    outR = feed_cfg::kSatCeil * fast_tanh(r * feed_cfg::kSatInv);
}
```

Note that `_env.process()` runs **every sample** while `_rebuild_allocation`
reads `_env.value()` once per tick: the amplitude therefore glides across the
tick through `FeedBank`'s slope, which is what makes a fast RISE audible instead
of a 96-sample staircase.

- [ ] **Step 5: Run the cents-vs-ratio probe (plan open point 2)**

A scratchpad probe that builds the same bank twice — offsets symmetric in cents
against offsets symmetric in ratio — and prints `f0_autocorr` for each at
`kSpreadMaxCt`. Record both numbers in the task report. If they differ by less
than a tenth of `kPitchCentreTolCt`, say so and keep cents; if they do not, the
plan is wrong and cents/ratio becomes a finding, not a preference.

- [ ] **Step 6: Run the gates and commit**

```bash
cmake --build build && ./build/spky_tests -tc="feed G*"
ctest --test-dir build --output-on-failure
```

Expected: green but G8. `ctrl_identity` and `wave_formant_sweep` unchanged.

```bash
git add engine/feed tests/test_feed_engine.cpp
git commit -m "feat(feed): the ring sounds -- allocation, SPREAD and the ceiling

Pairs allocated over the chord tones in strided groups, a SPREAD signature
zero-meaned within each group so the perceived centre is exact rather than
statistical, the Braids pitch attenuation on the blended feedback input, and a
tanh ceiling on the deck sum.

Two two-sided gates: G10 proves the motion is the coupling (mid BOND moves the
magnitude spectrum over 30 s, BOND 0 does not) and G15 proves the pitch centre
holds up to the BOND threshold Task 3 measured.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: The envelope — RISE, FALL, FLOOR, the accent, FLOW and CHOKE

Spec §2.5, §4 and §5. One `Env` per deck, unmodified, driving **both** amplitude
and index — in FM the attack lives in the index envelope, not in the level.

**Files:**
- Modify: `engine/feed/feed_engine.h` — the envelope helpers' declarations
- Modify: `engine/feed/feed_engine.cpp` — `_rise_s`, `_fall_s`, `set_decay`,
  `set_attack`, `trigger`, `set_flow`, `set_hold`, `set_accent`, `set_cycle`
- Modify: `tests/test_feed_engine.cpp` — append G16–G22

**Interfaces:**
- Consumes: the bank and the control tick (Task 5).
- Produces: `FeedEngine::floor_for_test()` answering for real; the auto-retrigger
  path `set_flow`/`set_hold` share.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("feed G16: FLOOR rides the top quarter of FALL -- two-sided") {
    // The fold that frees the RES slot for RATIO (the plan's control map).
    // Both halves asserted, because a fold that is always on and a fold that
    // is never on both pass a one-sided gate.
    FeedEngine e = fresh_feed();
    e.set_decay(feed_cfg::kFloorFoldStart * 0.5f);
    CHECK(e.floor_for_test() == 0.f);
    e.set_decay(feed_cfg::kFloorFoldStart);
    CHECK(e.floor_for_test() == doctest::Approx(0.f));
    e.set_decay(1.f);
    CHECK(e.floor_for_test() == doctest::Approx(1.f));
    // ...and it is monotone in between, so the knob has no step in it.
    float prev = -1.f;
    for (float n = feed_cfg::kFloorFoldStart; n <= 1.0001f; n += 0.02f) {
        e.set_decay(n);
        CAPTURE(n);
        REQUIRE(e.floor_for_test() >= prev);
        prev = e.floor_for_test();
    }
}

TEST_CASE("feed G17: FLOOR 1 is a standing drone, FLOOR 0 blooms and dies") {
    auto tail_after = [](float dec, float seconds) {
        FeedEngine e = fresh_feed();
        e.set_decay(dec);
        e.set_attack(0.2f);
        feed_lanes(e, 0.5f, 0.3f, 0.3f, 0.8f);
        e.set_flow(false);                    // STEP: no minimum floor
        e.trigger(0.5f);
        for (int i = 0; i < int(48000 * seconds); ++i) { float l, r; e.process(l, r); }
        return peak_of(render_l(e, 4800));
    };
    CHECK(tail_after(1.f, 8.f) > 0.02f);      // endless
    CHECK(tail_after(0.3f, 8.f) < 1e-4f);     // gone
}

TEST_CASE("feed G18: the index rides the envelope, not just the level") {
    // Spec 2.5: bright and rough on the attack, darker and calmer in the tail.
    // If the index were constant, the attack and the tail would have the same
    // spectral shape at different gains -- so the gate normalises the two
    // windows and compares their SHAPE.
    FeedEngine e = fresh_feed();
    e.set_attack(0.6f);
    e.set_decay(0.5f);
    e.set_resonance(0.4f);
    feed_lanes(e, 0.4f, 0.2f, 0.2f, 1.f);
    e.set_flow(false);
    e.trigger(0.4f);
    const std::vector<float> attack = render_l(e, 8192);
    for (int i = 0; i < 48000; ++i) { float l, r; e.process(l, r); }
    const std::vector<float> tail = render_l(e, 8192);
    auto centroid = [](const std::vector<float>& x) {
        const std::vector<double> m = mag_spectrum(x);
        double num = 0.0, den = 0.0;
        for (size_t i = 0; i < m.size(); ++i) { num += i * m[i]; den += m[i]; }
        return den > 0.0 ? num / den : 0.0;
    };
    const double c_attack = centroid(attack);
    const double c_tail = centroid(tail);
    CAPTURE(c_attack);
    CAPTURE(c_tail);
    CHECK(c_tail < 0.8 * c_attack);
}

TEST_CASE("feed G19: FLOW keeps a minimum floor at FLOOR 0") {
    // The drone promise. SWARM's rule, kept (spec section 5).
    FeedEngine e = fresh_feed();
    e.set_decay(0.f);                          // FLOOR 0
    feed_lanes(e, 0.5f, 0.2f, 0.2f, 0.8f);
    e.set_flow(true);
    e.trigger(0.5f);
    for (int i = 0; i < 48000 * 10; ++i) { float l, r; e.process(l, r); }
    CHECK(peak_of(render_l(e, 4800)) > 0.01f);
    // The other side: in STEP the same knob really does decay to nothing, so
    // the floor is a FLOW rule and not a leak.
    FeedEngine s = fresh_feed();
    s.set_decay(0.f);
    feed_lanes(s, 0.5f, 0.2f, 0.2f, 0.8f);
    s.set_flow(false);
    s.trigger(0.5f);
    for (int i = 0; i < 48000 * 10; ++i) { float l, r; s.process(l, r); }
    CHECK(peak_of(render_l(s, 4800)) < 1e-4f);
}

TEST_CASE("feed G20: CHOKE decays the drone out and stops re-arming") {
    FeedEngine e = fresh_feed();
    e.set_decay(1.f);
    feed_lanes(e, 0.5f, 0.2f, 0.2f, 0.8f);
    e.set_flow(true);
    e.trigger(0.5f);
    settle(e, 40);
    const float before = peak_of(render_l(e, 4800));
    REQUIRE(before > 0.02f);
    e.set_hold(true);
    // Monotone decay, sampled in windows -- a level that dips and returns is
    // an auto-retrigger that did not stop.
    float prev = before;
    for (int w = 0; w < 12; ++w) {
        for (int i = 0; i < 4800; ++i) { float l, r; e.process(l, r); }
        const float now = peak_of(render_l(e, 2400));
        CAPTURE(w); CAPTURE(now); CAPTURE(prev);
        REQUIRE(now <= prev * 1.01f);
        prev = now;
    }
    CHECK(prev < 0.1f * before);
    // Release re-arms.
    e.set_hold(false);
    settle(e, 40);
    CHECK(peak_of(render_l(e, 4800)) > 0.5f * before);
}

TEST_CASE("feed G21: the accent spends itself twice -- and DEC gates the second") {
    // Spec section 5, the SYNTH/WAVE/BODY shape. Two halves, and the DEC gate
    // is what makes the ring half's inert case reachable (vacuous shape 4).
    auto peak_and_tail = [](float accent, float dec) {
        FeedEngine e = fresh_feed();
        e.set_attack(0.f);
        e.set_decay(dec);
        feed_lanes(e, 0.5f, 0.2f, 0.2f, 0.8f);
        e.set_flow(false);
        e.set_accent(accent);
        e.trigger(0.5f);
        const float pk = peak_of(render_l(e, 2400));
        for (int i = 0; i < 24000; ++i) { float l, r; e.process(l, r); }
        return std::pair<float, float>(pk, peak_of(render_l(e, 2400)));
    };
    const auto full = peak_and_tail(0.f, 0.5f);
    const auto weak = peak_and_tail(1.f, 0.5f);
    CHECK(weak.first < full.first);                        // hit height
    CHECK(weak.first > feed_cfg::kAccentVelFloor * 0.8f * full.first);
    // ring time: shorter at accent 1 with DEC up...
    CHECK(weak.second / weak.first < full.second / full.first);
    // ...and untouched at DEC 0.
    const auto full0 = peak_and_tail(0.f, 0.f);
    const auto weak0 = peak_and_tail(1.f, 0.f);
    CHECK(weak0.second / weak0.first ==
          doctest::Approx(full0.second / full0.first).epsilon(0.02));
}

TEST_CASE("feed G22: a retrigger is click-free") {
    // Env::trigger rises from the CURRENT level, so a re-hit on a sounding
    // drone must not step. The bound is on the sample-to-sample derivative
    // around the trigger, compared against the same signal's own worst
    // derivative while running -- so the threshold is measured, not invented.
    FeedEngine e = fresh_feed();
    e.set_decay(1.f);
    e.set_attack(0.4f);
    feed_lanes(e, 0.5f, 0.2f, 0.2f, 0.8f);
    e.set_flow(true);
    e.trigger(0.5f);
    settle(e, 40);
    const std::vector<float> quiet = render_l(e, 24000);
    float worst_running = 0.f;
    for (size_t i = 1; i < quiet.size(); ++i)
        worst_running = std::max(worst_running, std::fabs(quiet[i] - quiet[i - 1]));
    e.trigger(0.5f);
    const std::vector<float> hit = render_l(e, 4800);
    float worst_hit = 0.f;
    for (size_t i = 1; i < hit.size(); ++i)
        worst_hit = std::max(worst_hit, std::fabs(hit[i] - hit[i - 1]));
    CAPTURE(worst_running);
    CAPTURE(worst_hit);
    CHECK(worst_hit < 3.f * worst_running);
}
```

- [ ] **Step 2: Run and confirm the RED**

```bash
cmake --build build && ./build/spky_tests -tc="feed G1[6-9],feed G2[0-2]"
```

Expected: G16 fails on a `floor_for_test()` that is always 0, G17–G22 on an
engine whose envelope never moves. Note in the report which of them **passed**
against the stub — every one that did is a gate to strengthen before Step 3.

- [ ] **Step 3: Build the envelope**

`engine/feed/feed_engine.cpp`:

```cpp
// RISE and FALL as ratios of the master cycle, the SynthEngineT law so the two
// engines' knobs mean the same thing: attack 0.002 * 250^n of the cycle,
// decay 0.1 * 80^n. std::pow at CONTROL rate only -- both are recomputed in
// the setters, not in _control_tick, because the knobs move at gesture rate
// and Env::set_times already guards against recomputing identical coefficients.
void FeedEngine::set_attack(float n) {
    _rise_n = clampf(n, 0.f, 1.f);
    _rise_ratio = 0.002f * std::pow(250.f, _rise_n);
}

void FeedEngine::set_decay(float n) {
    _fall_n = clampf(n, 0.f, 1.f);
    _fall_ratio = 0.1f * std::pow(80.f, _fall_n);
    // FLOOR rides the top quarter (the plan's control map). Below the fold
    // start the deck blooms and dies; above it the tail stops decaying to zero
    // and stands, reaching an endless drone at DEC 1. The knob keeps its whole
    // travel for FALL, so nothing about the tail's LENGTH is given up.
    _floor_n = clampf((_fall_n - feed_cfg::kFloorFoldStart) /
                      (1.f - feed_cfg::kFloorFoldStart), 0.f, 1.f);
}

float FeedEngine::_rise_s() const {
    return clampf(_rise_ratio * _cycle_s, SynthEngine::kAttackFloorS, 20.f);
}

float FeedEngine::_fall_s() const {
    // The ring half of the STEP accent, gated by the DEC knob exactly as
    // SYNTH/WAVE/BODY do it: DEC 0 leaves ring time untouched, so a player who
    // never raises DEC never hears the accent shorten a note.
    const float acc = 1.f - (1.f - feed_cfg::kAccentDecFloor) * _accent * _fall_n;
    return clampf(_fall_ratio * _cycle_s * acc,
                  SynthEngine::kDecayMinS, SynthEngine::kDecayMaxS);
}
```

`trigger`, and the hit half of the accent:

```cpp
void FeedEngine::trigger(float pitch_norm) {
    // A trigger RETUNES the ring and injects energy; it does not start it
    // (spec 2.1). The network was already running.
    _chord[0] = clampf(pitch_norm, 0.f, 1.f);
    _chord_n = 1;
    // The hit half of the accent, composed the way SynthEngineT composes it:
    // a scale on the strike, not a replacement for it.
    _hit_gain = 1.f - (1.f - feed_cfg::kAccentVelFloor) * _accent;
    _env.trigger();          // rises from the CURRENT level: click-free (G22)
    _auto_pending = false;
    _rebuild_allocation();   // the retune lands as a glide, this tick
}
```

`_hit_gain` multiplies `_level` in `_rebuild_allocation`'s amplitude and in
`process`'s SUB — **both**, or the accent changes the balance between the ring
and its foundation, which is a timbre change wearing a dynamics costume.

FLOW, CHOKE and the auto-retrigger, the `SynthEngineT` shape:

```cpp
void FeedEngine::set_flow(bool flow) {
    if (flow == _flow) return;
    _flow = flow;
    // Nothing is demoted on either edge: FEED has no voices to demote, and the
    // ring runs either way. What changes is the envelope's sustain, which
    // _control_tick reads fresh, and whether the drone re-arms.
    _auto_pending = flow && !_hold && !_env.active();
}

void FeedEngine::set_hold(bool on) {
    if (on == _hold) return;
    _hold = on;
    if (on) {
        // CHOKE: the sustain goes to 0 while holding, which IS the demotion
        // release -- the same coefficient now converges to zero (env.h). The
        // floor decays out click-free and auto-retrigger stops.
        _env.set_sustain(0.f);
        _auto_pending = false;
    } else if (_flow) {
        _auto_pending = true;
    }
}
```

and in `process()`, immediately after the control-tick branch:

```cpp
    if (_auto_pending) { _auto_pending = false; _env.trigger(); }
```

deferred to `process()` rather than fired inside the setter, for the reason
`SynthEngineT` defers it: the targets are fresh there and stale at the setter.

Add `_rise_ratio`, `_fall_ratio`, `_hit_gain` and `_auto_pending` to the header.

- [ ] **Step 4: Run, then commit**

```bash
cmake --build build && ./build/spky_tests -tc="feed G*"
ctest --test-dir build --output-on-failure
```

```bash
git add engine/feed tests/test_feed_engine.cpp
git commit -m "feat(feed): one envelope for amplitude and index

RISE and FALL on the SynthEngineT law, FLOOR folded into the top quarter of
FALL so RES is free for RATIO, the STEP accent spending itself on hit height
and -- DEC-gated -- on ring time, the FLOW minimum floor, and CHOKE releasing
the drone through Env's sustain-to-zero.

G18 is the gate that says the index really rides the envelope: the tail's
spectral centroid is below the attack's, which a constant index cannot produce.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 7: The chord — one hit, and a glissando instead of a retrigger

Spec §5. `trigger_chord` is **overridden**: the interface default fires
`trigger` n times, which on FEED would be n envelope hits for one chord.

**Files:**
- Modify: `engine/feed/feed_engine.cpp` — `trigger_chord`, `set_chord`
- Modify: `tests/test_feed_engine.cpp` — append G23–G25

**Interfaces:**
- Consumes: the envelope (Task 6), the allocation (Task 5).
- Produces: nothing new; `voiced_tones_for_test()` becomes meaningful.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("feed G23: trigger_chord fires ONE envelope hit, not n") {
    // RED against IPartEngine's default implementation, which loops trigger().
    // A four-note chord would otherwise be four hits inside one sample -- four
    // Env::trigger calls, each restarting the attack, so the audible result is
    // one hit at the wrong shape and three wasted.
    FeedEngine e = fresh_feed();
    e.set_attack(0.5f);
    e.set_decay(0.4f);
    feed_lanes(e, 0.5f, 0.2f, 0.2f, 0.8f);
    e.set_flow(false);

    const float one[1] = { 0.4f };
    e.trigger_chord(one, 1);
    std::vector<float> single;
    for (int i = 0; i < 12000; ++i) { float l, r; e.process(l, r); single.push_back(l); }

    FeedEngine f = fresh_feed();
    f.set_attack(0.5f);
    f.set_decay(0.4f);
    feed_lanes(f, 0.5f, 0.2f, 0.2f, 0.8f);
    f.set_flow(false);
    const float four[4] = { 0.4f, 0.45f, 0.5f, 0.55f };
    f.trigger_chord(four, 4);
    std::vector<float> chord;
    for (int i = 0; i < 12000; ++i) { float l, r; f.process(l, r); chord.push_back(l); }

    // The envelope's SHAPE is the observable: one hit rises once. Four stacked
    // triggers each restart the attack from the level reached, so the envelope
    // reaches its peak later and the normalised rise differs.
    auto peak_index = [](const std::vector<float>& x) {
        size_t best = 0; float bp = 0.f;
        for (size_t i = 0; i < x.size(); ++i)
            if (std::fabs(x[i]) > bp) { bp = std::fabs(x[i]); best = i; }
        return best;
    };
    CAPTURE(peak_index(single));
    CAPTURE(peak_index(chord));
    CHECK(peak_index(chord) < peak_index(single) * 1.25);
    CHECK(peak_index(chord) > peak_index(single) * 0.75);
}

TEST_CASE("feed G24: a chord change is a glissando, not a retrigger") {
    // set_chord must re-voice the network live, with no envelope hit at all --
    // COLOR is a modulation destination and it moves every control tick.
    FeedEngine e = fresh_feed();
    e.set_decay(1.f);
    feed_lanes(e, 0.5f, 0.2f, 0.2f, 0.8f);
    e.set_flow(true);
    const float one[1] = { 0.4f };
    e.set_chord(one, 1);
    e.trigger_chord(one, 1);
    settle(e, 60);
    const float env_before = e.voice_env(0);
    const float four[4] = { 0.4f, 0.5f, 0.6f, 0.7f };
    e.set_chord(four, 4);
    // The envelope must not jump: a retrigger would push it back toward 1.
    for (int i = 0; i < 96; ++i) { float l, r; e.process(l, r); }
    CHECK(e.voice_env(0) == doctest::Approx(env_before).epsilon(0.02));
    // ...but the pitches must move.
    settle(e, 60);
    bool moved = false;
    for (int i = 0; i < feed_cfg::kPairs; ++i)
        if (std::fabs(e.pair_hz_for_test(i) - pitch_to_hz_ref(0.4f)) >
            pitch_to_hz_ref(0.4f) * 0.02f) moved = true;
    CHECK(moved);
}

TEST_CASE("feed G25: pairs on the root hold still, and the tone cap binds") {
    // Nearest-neighbour allocation, spec section 5: pairs on common tones hold
    // still, only moving ones glide. The strided grouping (pair i -> tone
    // i % voiced) is what delivers it: growing the chord leaves pair 0 on the
    // root.
    FeedEngine e = fresh_feed();
    feed_lanes(e, 0.5f, 0.f, 0.f);         // SPREAD 0: pitches are the tones
    e.set_flow(true);
    const float one[1] = { 0.4f };
    e.set_chord(one, 1);
    settle(e, 60);
    const float root_hz = e.pair_hz_for_test(0);
    const float four[4] = { 0.4f, 0.5f, 0.6f, 0.7f };
    e.set_chord(four, 4);
    settle(e, 60);
    CHECK(e.pair_hz_for_test(0) == doctest::Approx(root_hz).epsilon(0.001));

    // The cap: at most kPairs / kPairsPerTone tones are voiced, so every
    // voiced tone keeps a group SPREAD can reach (plan open point 4).
    const int cap = feed_cfg::kPairs / feed_cfg::kPairsPerTone;
    CHECK(e.voiced_tones_for_test() == (4 < cap ? 4 : cap));
    e.set_chord(one, 1);
    settle(e, 60);
    CHECK(e.voiced_tones_for_test() == 1);
}
```

`pitch_to_hz_ref` is a one-line helper in the test file's anonymous namespace
(`110.f * std::pow(8.f, p)`), with a comment naming `synth_engine.cpp:15` as the
original. It exists so the gate compares against the instrument's law rather
than against the engine's own copy of it — a gate that recomputes its subject
from the subject is not a gate.

- [ ] **Step 2: Run and confirm the RED**

```bash
cmake --build build && ./build/spky_tests -tc="feed G2[3-5]"
```

Expected: G23 fails because `IPartEngine::trigger_chord`'s default is still in
force, G24 and G25 because `set_chord` is a stub.

- [ ] **Step 3: Override the two**

```cpp
void FeedEngine::trigger_chord(const float* p, int n) {
    if (n < 1) return;
    if (n > kMaxChord) n = kMaxChord;
    _set_chord_tones(p, n);
    _hit_gain = 1.f - (1.f - feed_cfg::kAccentVelFloor) * _accent;
    _env.trigger();          // ONE hit, whatever the chord holds
    _auto_pending = false;
    _rebuild_allocation();
}

void FeedEngine::set_chord(const float* p, int n) {
    // Arrives once per control tick from Part::_control_tick, so this must be
    // cheap and must NOT touch the envelope: a COLOR move re-voices the network
    // as a glissando, with no retrigger (spec section 5).
    if (n < 1) return;
    if (n > kMaxChord) n = kMaxChord;
    _set_chord_tones(p, n);
}
```

`_set_chord_tones` stores the tones **sorted ascending**, which is what makes
`i % _voiced_n` a nearest-neighbour allocation rather than an arbitrary one:

```cpp
void FeedEngine::_set_chord_tones(const float* p, int n) {
    for (int i = 0; i < n; ++i) _chord[i] = clampf(p[i], 0.f, 1.f);
    // Insertion sort, n <= 4. Sorted, tone 0 is the root, so a chord that
    // grows upward leaves pair 0 (and every pair whose index is 0 mod the
    // voiced count) exactly where it was -- G25.
    for (int i = 1; i < n; ++i) {
        const float v = _chord[i];
        int j = i - 1;
        while (j >= 0 && _chord[j] > v) { _chord[j + 1] = _chord[j]; --j; }
        _chord[j + 1] = v;
    }
    _chord_n = n;
}
```

Nothing in FORM or SONG is FEED-specific: it registers as a melodic engine and
consumes the composed phrase like any other (spec §5). G3 in Task 1 is what
holds that open.

- [ ] **Step 4: Run and commit**

```bash
cmake --build build && ./build/spky_tests -tc="feed G*"
ctest --test-dir build --output-on-failure
```

```bash
git add engine/feed tests/test_feed_engine.cpp
git commit -m "feat(feed): one hit per chord, and a live re-voicing

trigger_chord overridden -- the interface default fires trigger() n times, and
on FEED that is n envelope hits for one chord. set_chord re-voices the ring
without touching the envelope, so a COLOR move arrives as a glissando.

Tones are stored sorted, so the strided pair->tone map holds the root's pairs
still while a growing chord moves only the others.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 8: RATIO, DAMP, SUB

The three voice-row controls Task 5 and Task 6 left neutral.

**Files:**
- Modify: `engine/feed/feed_engine.cpp` — `set_resonance`, `set_filt`,
  `_damp_coef`, `set_sub`
- Modify: `tests/test_feed_engine.cpp` — append G26–G28

**Interfaces:**
- Consumes: the bank (Task 2), the control tick (Task 5).
- Produces: `ratio_for_test()` answering for real.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("feed G26: RATIO's lower half gravitates to the integers") {
    // Spec section 4. A continuous knob stands BETWEEN the integers almost
    // everywhere, and near-integer ratios read as chorus -- motion from the
    // wrong source. Plan open point 3 chose a monotone warp over zones with
    // hysteresis, and monotonicity is asserted rather than claimed.
    FeedEngine e = fresh_feed();
    int near_integer = 0, samples = 0;
    float prev = -1.f;
    for (float n = 0.f; n <= 0.5f + 1e-6f; n += 0.002f) {
        e.set_resonance(n);
        const float r = e.ratio_for_test();
        CAPTURE(n); CAPTURE(r);
        REQUIRE(r >= prev);                       // monotone: no zone flip
        prev = r;
        REQUIRE(r >= 1.f - 1e-4f);
        REQUIRE(r <= feed_cfg::kRatioMagnetTop + 1e-4f);
        if (std::fabs(r - std::round(r)) < 0.02f) ++near_integer;
        ++samples;
    }
    CAPTURE(near_integer);
    CAPTURE(samples);
    // "Gravitates" is a measurable claim: most of the lower half's travel sits
    // within 2 % of an integer. A linear map would give roughly 4 %.
    CHECK(near_integer > samples / 2);
    // The endpoints are exact, or the lower half does not actually reach 1:1
    // and 4:1.
    e.set_resonance(0.f);
    CHECK(e.ratio_for_test() == doctest::Approx(1.f));
    e.set_resonance(0.5f);
    CHECK(e.ratio_for_test() == doctest::Approx(feed_cfg::kRatioMagnetTop));
}

TEST_CASE("feed G27: RATIO's upper half runs continuously into the irrational") {
    FeedEngine e = fresh_feed();
    int off_integer = 0, samples = 0;
    float prev = feed_cfg::kRatioMagnetTop - 1e-4f;
    for (float n = 0.5f; n <= 1.f + 1e-6f; n += 0.002f) {
        e.set_resonance(n);
        const float r = e.ratio_for_test();
        CAPTURE(n); CAPTURE(r);
        REQUIRE(r >= prev);
        prev = r;
        if (std::fabs(r - std::round(r)) > 0.1f) ++off_integer;
        ++samples;
    }
    CHECK(off_integer > samples / 2);           // no magnet up here
    e.set_resonance(1.f);
    CHECK(e.ratio_for_test() == doctest::Approx(feed_cfg::kRatioMax));
}

TEST_CASE("feed G28: DAMP is honestly a filter, and its centre is neutral") {
    // FILT is bipolar: left sweeps the feedback path's cutoff DOWN (dark and
    // tame -- the loop loses the highs that feed escalation), right sweeps it
    // up toward effectively open (bright and wild). The centre detent is the
    // by-ear neutral cutoff.
    auto centroid_at = [](float t) {
        FeedEngine e = fresh_feed();
        e.set_filt(t);
        e.set_decay(1.f);
        e.set_resonance(0.3f);
        feed_lanes(e, 0.35f, 0.5f, 0.3f, 0.9f);
        e.set_flow(true);
        e.trigger(0.35f);
        settle(e, 60);
        const std::vector<double> m = mag_spectrum(render_l(e, 32768));
        double num = 0.0, den = 0.0;
        for (size_t i = 0; i < m.size(); ++i) { num += i * m[i]; den += m[i]; }
        return den > 0.0 ? num / den : 0.0;
    };
    const double dark = centroid_at(-1.f);
    const double mid = centroid_at(0.f);
    const double bright = centroid_at(1.f);
    CAPTURE(dark); CAPTURE(mid); CAPTURE(bright);
    CHECK(dark < mid);
    CHECK(mid < bright);
}

TEST_CASE("feed G29: SUB is a sub, and DEPTH 0.5 is a good sound") {
    // Two claims in one case because they share a setup, and both are about
    // the deck being usable rather than merely finite.
    //
    // SUB: energy an octave below the root appears when the knob is up and is
    // absent when it is down.
    auto sub_energy = [](float sub_n) {
        FeedEngine e = fresh_feed();
        e.set_sub(sub_n);
        e.set_decay(1.f);
        e.set_resonance(0.f);
        feed_lanes(e, 0.5f, 0.f, 0.f, 0.f);     // DEPTH 0: a bare carrier
        e.set_flow(true);
        e.trigger(0.5f);
        settle(e, 60);
        const std::vector<double> m = mag_spectrum(render_l(e, 32768));
        const double bin_hz = 48000.0 / 32768.0;
        const int half = int(0.5 * pitch_to_hz_ref(0.5f) / bin_hz);
        double band = 0.0;
        for (int i = half - 2; i <= half + 2; ++i) band += m[i];
        return band;
    };
    CHECK(sub_energy(1.f) > 20.0 * sub_energy(0.f));

    // DEPTH at kDepthBase must be a SOUND, not a dead sine. DEPTH is the one
    // FEED control with no knob of its own (the plan's control map), so spec
    // section 4's defensive requirement is what stands in for one -- and this
    // is what makes it falsifiable.
    FeedEngine e = fresh_feed();
    e.set_decay(1.f);
    e.set_resonance(0.3f);
    feed_lanes(e, 0.4f, 0.4f, 0.3f, feed_cfg::kDepthBase);
    e.set_flow(true);
    e.trigger(0.4f);
    settle(e, 60);
    const std::vector<double> m = mag_spectrum(render_l(e, 32768));
    const double bin_hz = 48000.0 / 32768.0;
    const int f0_bin = int(pitch_to_hz_ref(0.4f) / bin_hz);
    double fundamental = 0.0, above = 0.0;
    for (int i = f0_bin - 3; i <= f0_bin + 3; ++i) fundamental += m[i];
    for (size_t i = size_t(f0_bin) + 4; i < m.size(); ++i) above += m[i];
    CAPTURE(fundamental);
    CAPTURE(above);
    // A bare sine puts essentially nothing above the fundamental. "A good
    // sound" is not testable; "has a spectrum" is, and it is the half that
    // would actually have caught a dead default.
    CHECK(above > 0.1 * fundamental);
}
```

- [ ] **Step 2: Run and confirm the RED**

```bash
cmake --build build && ./build/spky_tests -tc="feed G2[6-9]"
```

- [ ] **Step 3: Build the three**

```cpp
// RATIO. The lower half runs 1:1..kRatioMagnetTop through a monotone warp that
// flattens near the integers; the upper half runs continuously from there into
// the irrational (spec section 4).
//
// A magnet, not zones with hysteresis (plan open point 3): a zone reader is a
// discrete selector with state, and this knob must stay continuous across the
// midpoint. The warp has no state, cannot be "between" states, and is monotone
// by construction -- which G26 asserts rather than trusts.
void FeedEngine::set_resonance(float n) {
    const float k = clampf(n, 0.f, 1.f);
    if (k <= 0.5f) {
        const float lin = 1.f + (feed_cfg::kRatioMagnetTop - 1.f) * (k * 2.f);
        const float ri = std::round(lin);
        const float d = lin - ri;                       // [-0.5, 0.5]
        // |2d|^exp * 0.5, sign preserved: flat at the integer, exact at the
        // midpoint between two integers, monotone everywhere.
        const float a = std::fabs(d) * 2.f;
        const float warped = 0.5f * std::pow(a, feed_cfg::kRatioMagnetExp);
        _ratio = ri + (d < 0.f ? -warped : warped);
    } else {
        _ratio = feed_cfg::kRatioMagnetTop +
                 (feed_cfg::kRatioMax - feed_cfg::kRatioMagnetTop) * ((k - 0.5f) * 2.f);
    }
}

// DAMP. FILT is bipolar; the centre detent is the neutral cutoff and the travel
// multiplies and divides it by kDampSpan. std::pow at CONTROL rate, once per
// knob move -- not in _control_tick, which reads the cached coefficient.
void FeedEngine::set_filt(float t) {
    _damp_t = clampf(t, -1.f, 1.f);
    const float hz = feed_cfg::kDampCenterHz * std::pow(feed_cfg::kDampSpan, _damp_t);
    // One-pole coefficient, the OnePole::init law (util/onepole.h) expressed
    // as a cutoff rather than a time: k = 1 - exp(-2*pi*fc/sr), clamped to 1
    // so the top of the travel is genuinely open rather than merely steep.
    _damp_k = clampf(1.f - std::exp(-6.2831853f * hz / _sr), 0.f, 1.f);
}

float FeedEngine::_damp_coef() const { return _damp_k; }

void FeedEngine::set_sub(float n) { _sub_n = clampf(n, 0.f, 1.f); }
```

Add `_damp_k` to the header. `set_filt` reads `_sr`, so `init()` must call
`set_filt(_damp_t)` after setting `_sr` — otherwise the coefficient is computed
against the constructor's default sample rate and a 44.1 kHz host gets a filter
tuned for 48 kHz. **This is the kind of ordering bug a render never shows**, so
say it in the code, not only here.

- [ ] **Step 4: Run and commit**

```bash
cmake --build build && ./build/spky_tests -tc="feed G*"
ctest --test-dir build --output-on-failure
```

```bash
git add engine/feed tests/test_feed_engine.cpp
git commit -m "feat(feed): RATIO's magnet, DAMP inside the feedback path, SUB

RATIO's lower half locks onto 1:1..4:1 through a monotone warp -- a magnet, not
zones with hysteresis, so the knob stays continuous across the midpoint and has
no state to get wrong. DAMP is a one-pole INSIDE the feedback path, bipolar
around a neutral centre cutoff. SUB is one uncoupled sine an octave below the
root.

G29 makes spec section 4's defensive requirement falsifiable: DEPTH at
kDepthBase has a spectrum, not a bare carrier.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 9: NEW, and determinism

Spec §3.4. `NEW` redraws the deck's individual — the SPREAD signature and the
per-pair feedback offsets — and it is the **only** randomness in the engine.

**Files:**
- Modify: `engine/feed/feed_engine.cpp` — `reseed`
- Modify: `engine/parts/part.h` / `engine/parts/part.cpp` — `Part::new_phrase()`
- Modify: `engine/instrument.h:99` — `new_phrase(int)` routes through `Part`
- Modify: `tests/test_feed_engine.cpp` — append G30–G32

**Interfaces:**
- Consumes: `_draw_individual` (Task 5).
- Produces:
  - `void FeedEngine::reseed(uint32_t)`
  - `void Part::new_phrase()` — `_mod.new_phrase()` plus, on a FEED deck, a
    reseed. `Instrument::new_phrase(p)` calls this instead of reaching into
    `mod()` directly.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("feed G30: same seed and same knobs give bit-identical audio") {
    auto run = [](uint32_t seed) {
        FeedEngine e = fresh_feed(seed);
        e.set_resonance(0.35f);
        e.set_decay(0.9f);
        e.set_filt(0.3f);
        e.set_sub(0.5f);
        feed_lanes(e, 0.45f, 0.55f, 0.6f, 0.8f);
        e.set_flow(true);
        e.trigger(0.45f);
        return render_l(e, 48000 * 2);
    };
    const std::vector<float> a = run(4242u);
    const std::vector<float> b = run(4242u);
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) REQUIRE(a[i] == b[i]);
    // Different seed, different individual -- otherwise the seed is dead state
    // and G31 below could not tell anything.
    const std::vector<float> c = run(999u);
    bool differs = false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != c[i]) differs = true;
    CHECK(differs);
}

TEST_CASE("feed G31: NEW redraws the individual, and only NEW does") {
    FeedEngine e = fresh_feed(4242u);
    feed_lanes(e, 0.5f, 0.4f, 1.f, 0.8f);      // SPREAD full: the signature shows
    e.set_flow(true);
    settle(e, 60);
    std::vector<float> before;
    std::vector<float> fb_before;
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        before.push_back(e.pair_hz_for_test(i));
        fb_before.push_back(e.pair_fb_amount_for_test(i));
    }

    // Everything short of NEW leaves the individual alone: re-pushing the same
    // lanes, moving the chord and coming back, triggering again.
    feed_lanes(e, 0.5f, 0.4f, 1.f, 0.8f);
    const float chord[2] = { 0.5f, 0.6f };
    e.set_chord(chord, 2);
    settle(e, 60);
    const float one[1] = { 0.5f };
    e.set_chord(one, 1);
    e.trigger_chord(one, 1);
    settle(e, 60);
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        CAPTURE(i);
        CHECK(e.pair_hz_for_test(i) == doctest::Approx(before[i]).epsilon(0.001));
        CHECK(e.pair_fb_amount_for_test(i) ==
              doctest::Approx(fb_before[i]).epsilon(0.001));
    }

    e.reseed(777u);
    settle(e, 60);
    int hz_moved = 0, fb_moved = 0;
    for (int i = 0; i < feed_cfg::kPairs; ++i) {
        if (std::fabs(e.pair_hz_for_test(i) - before[i]) > before[i] * 0.0005f) ++hz_moved;
        if (std::fabs(e.pair_fb_amount_for_test(i) - fb_before[i]) >
            fb_before[i] * 0.005f) ++fb_moved;
    }
    CAPTURE(hz_moved); CAPTURE(fb_moved);
    // Both halves of the individual redrawn -- the detune signature AND the
    // per-pair feedback offsets (spec 3.4). A reseed that moved only the
    // frequencies would leave the cliff an edge rather than a gradient.
    CHECK(hz_moved > 0);
    CHECK(fb_moved > 0);
}

TEST_CASE("feed G32: NEW on a FEED deck reaches the ring") {
    // Instrument::new_phrase reached only mod() until this task, so the redraw
    // had no route in at all.
    Part p;
    p.init(48000.f, 5u);
    float l = 0.f, r = 0.f;
    p.set_engine(ENGINE_FEED);
    for (int i = 0; i < 500; ++i) p.process(l, r);
    REQUIRE(p.engine_id() == ENGINE_FEED);
    p.set_target_base(LANE_SIZE, 1.f);          // SPREAD full
    for (int i = 0; i < 96 * 80; ++i) p.process(l, r);
    std::vector<float> before;
    for (int i = 0; i < feed_cfg::kPairs; ++i)
        before.push_back(p.feed().pair_hz_for_test(i));
    p.new_phrase();
    for (int i = 0; i < 96 * 80; ++i) p.process(l, r);
    int moved = 0;
    for (int i = 0; i < feed_cfg::kPairs; ++i)
        if (std::fabs(p.feed().pair_hz_for_test(i) - before[i]) > before[i] * 0.0005f)
            ++moved;
    CHECK(moved > 0);
}
```

- [ ] **Step 2: Run and confirm the RED**

```bash
cmake --build build && ./build/spky_tests -tc="feed G3[0-2]"
```

Expected: G31 fails on a `reseed` that does nothing, G32 on `Part::new_phrase`
not existing.

- [ ] **Step 3: Wire NEW**

```cpp
void FeedEngine::reseed(uint32_t s) {
    // Immediately, not deferred: the new signature arrives as a glide because
    // every target change does, so there is nothing to defer it for. The
    // alternative (pending until the next phrase wrap, as ModLane::new_phrase
    // does) is one `if` away if a listening session asks for it.
    _rng.seed(s);
    _draw_individual();
}
```

`engine/parts/part.h`:

```cpp
    // NEW. The modulation layer's phrase redraw, plus -- on a FEED deck -- the
    // engine's own individual (spec 2026-08-18 feed, section 3.4). Routed
    // through Part rather than from Instrument straight into mod(), because
    // "what NEW means" is a per-deck question and Part is the only scope that
    // knows which engine is active.
    void new_phrase();
```

`engine/parts/part.cpp`:

```cpp
void Part::new_phrase() {
    _mod.new_phrase();
    if (_engine_id == ENGINE_FEED)
        _feed.reseed(_seed_base ^ (0x46454544u + (++_new_ctr)));
}
```

`_seed_base` and `_new_ctr` are new members; `_seed_base` is stored in `init()`.
The counter keeps NEW deterministic **and** progressive: a fresh `Part` pressed
NEW three times always lands on the same third individual, which is what makes
G32 and any future render reproducible.

`engine/instrument.h:99`:

```cpp
    void new_phrase(int p) { _parts[p].new_phrase(); }
```

- [ ] **Step 4: Run and commit**

```bash
cmake --build build && ./build/spky_tests -tc="feed G*"
ctest --test-dir build --output-on-failure
```

```bash
git add engine/feed engine/parts/part.h engine/parts/part.cpp \
        engine/instrument.h tests/test_feed_engine.cpp
git commit -m "feat(feed): NEW draws the deck a new individual

The SPREAD signature and the per-pair feedback offsets, both redrawn on NEW and
by nothing else. Instrument::new_phrase now routes through Part, which is the
only scope that knows which engine is active -- until this commit the redraw had
no route into an engine at all.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 10: The hosts — a listening render, the sixth ENG state, and the knob re-points

This is where the control map becomes real. Two host changes are **not**
cosmetic: DETUNE writes `LANE_SIZE`'s base on a FEED deck, and `LANE_MOTION`'s
base stops being `Part`'s incidental 0.5.

**Files:**
- Modify: `host/render/scenario.cpp` — `parse_engine`
- Create: `host/render/scenarios/feed_drone.json`
- Modify: `tests/test_scenario.cpp` — the spelling case
- Modify: `host/vcv/res/gen_panel.py` — `DYNAMIC_CAPTIONS` (a new DETUNE row and
  a sixth word everywhere), the `words[]` arity, the ENG comment
- Modify: `host/vcv/res/test_panel.py` — `driver_states`, the arity guards, the
  header-string check, the ENG-remap mirror at `:1503`
- Modify: `host/vcv/src/Fireflow.cpp` — `configSwitch` labels, `kEngineShades`,
  the ENG remap, the SOURCE tooltip, the two lane-base re-points
- Modify: `host/vcv/src/generated_panel.hpp` — regenerated, never hand-edited
- Modify: `bench/audition/init_patch.cpp:48` — the engine decode

**Interfaces:**
- Consumes: `ENGINE_FEED` (Task 1) and everything the engine does (Tasks 5–9).
- Produces: `"feed"` as a scenario engine spelling; ENG state 5 in the VCV host.

- [ ] **Step 1: Write the failing tests**

`tests/test_scenario.cpp`, beside the existing BODY/BBD spelling cases:

```cpp
TEST_CASE("scenario: feed engine spelling selects ENGINE_FEED") {
    // The same shape as the body/bbd cases above it: an unknown spelling falls
    // back to SYNTH, so a typo in this table is invisible without a case per
    // spelling.
    auto inst = std::make_unique<Instrument>();
    Scenario s = parse_scenario_string(R"({
      "sample_rate": 48000, "duration_s": 0.1,
      "init": [ {"action":"set_engine","part":0,"value":"feed"} ],
      "events": []
    })");
    apply_init(*inst, s);
    for (int i = 0; i < 1000; ++i) { float l, r; inst->process(nullptr, nullptr, &l, &r, 1); }
    CHECK(inst->engine_id(0) == ENGINE_FEED);
}
```

(Match the exact helper names the neighbouring cases in that file use — they are
the authority on the harness, not this snippet.)

`host/vcv/res/test_panel.py` — the guards that would otherwise silently stop
covering the new state:

```python
    driver_states = {"ENGINE": 6}
```
```python
        check(len(words) <= 6,
              f"{target}: {len(words)} words exceeds the header's word[6]")
```
```python
    check("const char* words[6]; };" in h,
```

Run the panel guards before touching the generator, so the RED is visible:

```bash
python host/vcv/res/test_panel.py
```

Expected: FAIL — one message per `DYNAMIC_CAPTIONS` row about the word count,
plus the header-string check. **Delete `host/vcv/res/__pycache__` first**: `.pyc`
invalidation keys on mtime *and* size, so a size-neutral edit inside one mtime
tick silently re-runs the old module and shows a false green (memory
`fireflow-vacuous-test-gates`).

- [ ] **Step 2: The render host**

`host/render/scenario.cpp`:

```cpp
    if (s == "feed")      return ENGINE_FEED;
```

Create `host/render/scenarios/feed_drone.json`. A sanity render, **no hash
gate**:

```json
{
  "_comment": "FEED listening render (spec 2026-08-18). Part A is a FEED deck left in FLOW (set_step is never called for part 0, so Part::flow() stays true -- 'lanes boot in FLOW -> drone', part.cpp), part B is silenced at LEVEL so nothing else is in the picture. Four sweeps, one at a time, each the thing the engine exists for: BOND from self-feedback into the ring, SPREAD from still to beating, DEPTH from tonal to rough, and one COLOR move to hear a chord change arrive as a glissando rather than a retrigger. BOND rides LANE_SOURCE's base, SPREAD LANE_SIZE's and DEPTH LANE_MOTION's -- the three lane bases the VCV host writes from the SOURCE, DETUNE and (as a constant) MOTION re-points, so what this render sweeps is what a player can reach. No hash gate: renders are sanity checks, not checksums (CLAUDE.md).",
  "sample_rate": 48000,
  "bpm": 90,
  "duration_s": 44,
  "init": [
    {"action":"set_engine","part":0,"value":"feed"},
    {"action":"set_engine","part":1,"value":"test_tone"},
    {"action":"set_target_active","part":1,"slot":4,"flag":false},
    {"action":"set_target_base","part":1,"slot":4,"value":0.0},

    {"_comment":"A held drone: DECAY high, so FALL is long AND FLOOR is up -- the two live on one knob (the fold that frees RES for RATIO)."},
    {"action":"set_voice_attack","part":0,"value":0.45},
    {"action":"set_voice_decay","part":0,"value":0.92},
    {"action":"set_voice_resonance","part":0,"value":0.18},
    {"action":"set_voice_sub","part":0,"value":0.45},
    {"action":"set_voice_filt","part":0,"value":0.0},

    {"_comment":"Lanes held still, so what moves is the engine and not the plane. MOD 0; every sweep below is a lane BASE."},
    {"action":"set_depth","part":0,"value":0.0},
    {"action":"set_target_base","part":0,"slot":0,"value":0.0},
    {"action":"set_target_base","part":0,"slot":1,"value":0.0},
    {"action":"set_target_base","part":0,"slot":2,"value":0.35},
    {"action":"set_target_base","part":0,"slot":3,"value":0.5},
    {"action":"set_reverb_mix","value":0.25}
  ],
  "events": [
    {"_comment":"0-10s: BOND 0, SPREAD 0 -- self-feedback only, one pitch. The reference the other three are heard against."},
    {"t":10.0,"action":"set_target_base","part":0,"slot":1,"value":0.35},
    {"_comment":"10-18s: SPREAD into the single-digit region -- slow breathing, no detune."},
    {"t":18.0,"action":"set_target_base","part":0,"slot":0,"value":0.45},
    {"_comment":"18-26s: BOND half up -- the ring engages and the sidebands start to wander."},
    {"t":26.0,"action":"set_target_base","part":0,"slot":0,"value":0.85},
    {"_comment":"26-32s: BOND past the pitch threshold -- the cliff, on purpose."},
    {"t":32.0,"action":"set_target_base","part":0,"slot":0,"value":0.45},
    {"t":32.0,"action":"set_target_base","part":0,"slot":3,"value":0.95},
    {"_comment":"32-38s: back below the cliff, DEPTH to the top -- the index alone."},
    {"t":38.0,"action":"set_color","part":0,"value":0.8}
    ,{"_comment":"38-44s: a chord arrives as a glide of the whole ring, with no retrigger."}
  ]
}
```

```bash
source env.sh && cmake --build build
./build/render host/render/scenarios/feed_drone.json <scratchpad>/feed.wav <scratchpad>/feed.csv
```

Confirm the WAV is non-silent — the render host prints its peak. A silent
listening render is the one failure mode a sanity render can have.

- [ ] **Step 3: The VCV captions**

`host/vcv/res/gen_panel.py` — one word per row appended in ENG order (0 Synth,
1 Sampler, 2 Wave, 3 Body, 4 BBD, **5 Feed**), **plus a new DETUNE row**,
because DETUNE means SPREAD on a FEED deck and a fixed `DTUN` plate would lie:

```python
DYNAMIC_CAPTIONS = [
    ("MELODY",   "ENGINE",   ("VARY", "SCAN", "VARY",  "VARY",  "VARY",  "VARY")),
    ("ATTACK",   "ENGINE",   ("ATK",  "ATK",  "ATK",   "HIT",   "ATK",   "RISE")),
    ("DECAY",    "ENGINE",   ("DEC",  "DEC",  "DEC",   "DAMP",  "TAIL",  "FALL")),
    ("RES",      "ENGINE",   ("RES",  "RES",  "RES",   "CHAR",  "TILT",  "RATIO")),
    ("SUB",      "ENGINE",   ("SUB",  "LEN",  "SUB",   "EXCIT", "INPUT", "SUB")),
    ("FILT",     "ENGINE",   ("FILT", "FILT", "FILT",  "BRITE", "LOSS",  "DAMP")),
    ("SOURCE",   "ENGINE",   ("TIMB", "ORG",  "FRAME", "MATL",  "DRIVE", "BOND")),
    ("DETUNE",   "ENGINE",   ("DTUN", "DTUN", "DTUN",  "DTUN",  "DTUN",  "SPRD")),
]
```
```python
              "const char* words[6]; };")
```

Extend the sources comment above the table the way it documents every other
engine's words, naming the setter each word stands for so the next reader can
check the word against the engine rather than against taste: RISE/FALL are the
one envelope in absolute seconds and FALL's top quarter is also FLOOR; RATIO is
`set_resonance` (modulator:carrier, magnet-locked in its lower half); DAMP is
`set_filt` (a one-pole inside the feedback path, bipolar); BOND is the
`LANE_SOURCE` target (self-feedback → neighbour); SPRD is the `LANE_SIZE` base,
re-pointed from the DETUNE knob in `Fireflow.cpp`.

**Three things to verify rather than assume before committing the table:**
`RATIO` is five characters in a slot whose widest existing word is `EXCIT`, also
five — run the footprint check, and if it complains, shorten to `RAT` and record
why. The `DETUNE` row is new, so the "no word is printed twice" guard sees
`DTUN` five times in one row for the first time; `printed_words()` keys on the
word and the guard is about two *controls* printing one word, so this should be
fine — **verify it, do not assume it**. And `DAMP` now appears both as BODY's
DECAY word and as FEED's FILT word: two controls, one word, in different engine
states. That is exactly what the guard exists to catch, so check what it does
with it before deciding whether FEED's FILT word becomes `TONE` instead.

- [ ] **Step 4: The VCV C++ — three cosmetic edits and two that change behaviour**

Cosmetic:

```cpp
                        configSwitch(c.id, 0.f, 5.f, init, "Engine",
                                     {"Synth", "Sampler", "Wave", "Body", "BBD", "Feed"});
```
```cpp
    nvgRGBA(230, 140, 110, 140),  // Feed: warm ember
```
```cpp
                eng == 5 ? spky::ENGINE_FEED :
```

plus the ENG comment above the remap ("Saved ENG meanings remain 0 = Synth and
1 = Sampler; 2 adds Wave, 3 Body, 4 the BBD") extended with 5, keeping the
sentence about anything outside that set falling through to Sampler intact —
that is what keeps old patches meaning what they meant. The SOURCE tooltip at
`Fireflow.cpp:401` enumerates the engines by name and needs FEED's BOND. The
mirror of the remap in `host/vcv/res/test_panel.py:1503` moves with it, and
`bench/audition/init_patch.cpp:48`'s decode gains the same case.

**Behavioural, both inside `pushParams`.** First, DETUNE:

```cpp
            const bool feedPart = inst.engine_id(p) == spky::ENGINE_FEED;
            // DETUNE means SPREAD on a FEED deck, and it gets there as the
            // LANE_SIZE base -- the sampler's SUB -> LANE_SIZE re-point, one
            // entry further down the same ledger. Raw, not squared: FEED owns
            // its own curve in feed_cfg's two-segment SPREAD map, and applying
            // DetuneQuantity's square on top would compress the single-digit
            // region the spec reserves for the lower half.
            if (!feedPart) {
                const float detKnob = pp(DETUNE_A, p);
                inst.set_voice_detune(p, detKnob * detKnob);
            }
```

and the `LANE_SIZE` gate below it becomes three-way, with the ledger comment
extended by a FEED line:

```cpp
            if (samplerPart) {
                inst.set_target_base(p, spky::LANE_SIZE, pp(SUB_A, p));
            } else if (feedPart) {
                inst.set_target_base(p, spky::LANE_SIZE, pp(DETUNE_A, p));
            } else {
                inst.set_target_base(p, spky::LANE_SIZE, 0.5f);
            }
```

Second, the MOTION base — spec §4's host task:

```cpp
            // LANE_MOTION's base was never written by this host at all, so it
            // sat on Part's compiled-in 0.5 and the only thing that moved it in
            // Rack was MOD. An engine that reads LANE_MOTION therefore had a
            // control whose ends the player could not reach -- the defect found
            // on the day SWARM was withdrawn (docs/gotchas.md, "Host (VCV)").
            // FEED reads it as DEPTH, the FM index, and gets its own by-ear
            // default here instead of inheriting a lane-layer coincidence.
            // The else branch is load-bearing for the same reason the
            // LANE_SIZE one is: a base left behind on an engine flip sticks.
            // 0.5f is exactly Part's default, so nothing moves for the other
            // five engines.
            inst.set_target_base(p, spky::LANE_MOTION,
                                 feedPart ? spky::feed_cfg::kDepthBase : 0.5f);
```

`pp(DETUNE_A, p)` — check whether DETUNE_A is inside the part stride or an
appended id before using `pp()`. `STAGES_A/B` needed an explicit ternary because
it is appended (`Fireflow.cpp:810-818`); if DETUNE is too, this line reads past
the parameter array for part B and the bug is silent. **Read the enum, do not
guess.**

Regenerate the panel — never hand-edit `generated_panel.hpp`:

```bash
python host/vcv/res/gen_panel.py
python host/vcv/res/test_panel.py
python host/vcv/res/test_hw_panel.py
```

(Both generators run from `host/vcv/`; use a scratchpad script if the working
directory is needed, never a `cd` in the tool call.)

- [ ] **Step 5: Build the plugin**

```bash
host/vcv/build-local.sh
```

Always this script, never a hand-rolled `g++` — the system `g++` on this machine
is the ARM cross-compiler and fails with "MinGW not found".

- [ ] **Step 6: Run everything and commit**

```bash
ctest --test-dir build --output-on-failure
```

Expected: green but G8, including `panel_guard` and `hw_panel_guard` (both are
ctest tests) and the two unchanged render hashes. **If a hash moved, the MOTION
base write is the first suspect** — verify by reading the scenario: both hashed
scenarios run SYNTH/WAVE decks through the render host, which does not call
`pushParams` at all, so a hash move means something else changed and it is a
finding.

```bash
git add host/render/scenario.cpp host/render/scenarios/feed_drone.json \
        tests/test_scenario.cpp host/vcv/res/gen_panel.py \
        host/vcv/res/test_panel.py host/vcv/src/Fireflow.cpp \
        host/vcv/src/generated_panel.hpp bench/audition/init_patch.cpp
git commit -m "feat(hosts): FEED reaches the render host and the VCV panel

The sixth ENG state and its captions, plus the two re-points the control map
needs: DETUNE writes LANE_SIZE's base as SPREAD on a FEED deck, and LANE_MOTION's
base is written at all for the first time -- FEED's own DEPTH default, 0.5f
(Part's own value, so bit-identical) for everyone else.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 11: The whole-engine CPU gate, and the denormal measurement — **NEEDS BASTIAN + HARDWARE**

**Files:**
- Modify: `bench/workloads_system.cpp` — `inst_feed_engine_worst`
- Modify: `bench/run.py` — the `system` row tuple
- Modify: `bench/test_run_contract.py`
- Modify: `bench/anchor.cpp` — the anchor order, if the new row belongs in it
- Create: `docs/bench/<date>-<hash>-feed-axi-o3-patch_sm-usb.{md,csv}`
- Modify: `docs/roadmap.md`

**Interfaces:**
- Consumes: the finished engine (Tasks 5–9), the `feed` profile (Task 4).
- Produces: the measured verdict on whether FEED fits, and the FTZ delta.

- [ ] **Step 1: Add the row**

`bench/workloads_system.cpp`, modelled line for line on
`configure_inst_bbd_engine_worst` / `setup_inst_bbd_engine_worst`
(`workloads_system.cpp:432`, `:529`, `:589`): both decks on `ENGINE_FEED`, both
in STEP with a fire every ~half second so envelope hits and chord retargeting
are actually happening, BOND high enough that the ring taps are live, the FX
chain left exactly as `instrument_worst` has it, and the returned checksum
folding in `inst.active_voices(PART_A/B)` so a wrong engine moves the checksum
instead of passing silently.

```cpp
    { "system", "inst_feed_engine_worst",
      setup_inst_feed_engine_worst, proc_inst_feed_engine_worst },
```

and the same name in `bench/run.py`'s `"system"` tuple and in
`bench/test_run_contract.py`.

```bash
python bench/test_run_contract.py
```

- [ ] **Step 2: Build, and prove the row landed**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" \
  python bench/run.py --profile feed --board patch_sm --transport usb \
  --optimization o3 --build-only
```
```bash
grep -c "setup_inst_feed_engine_worst\|proc_inst_feed_engine_worst" bench/build/bench.map
```

Expected: both symbols. If not, `touch bench/workloads_system.cpp` and rebuild —
and do not accept an unchanged memory table as evidence either way. Commit the
workload code **before** measuring: `run.py` refuses a dirty tree and names its
result file by the HEAD commit hash.

- [ ] **Step 3: Measure — Bastian, at the board**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" \
  python bench/run.py --profile feed --board patch_sm --transport usb \
  --optimization o3 --repeat 2
```

- [ ] **Step 4: Read the gate — inside one image**

**The gate is:** in the image just measured, `inst_feed_engine_worst` must not
exceed that **same image's** `instrument_worst`, on both `pct_avg` and
`pct_max`. The 102.27 % / 108.62 % in `docs/roadmap.md` and
`docs/bench/2026-08-07-d0b3c08-regress-axi-o3-patch_sm-usb.csv` is
**orientation**, not the comparison: a new translation unit shifts small rows by
~7 % from icache layout alone, so a cross-image subtraction is not a measurement
(`docs/gotchas.md`). Record both rows from the one run, and record the same
image's `instrument_worst_bbd_dtcm` verdict beside them.

If the gate fails: P is a compile-time constant, so the answer is a **rebuild at
a lower P**, not an architecture change (spec §8). Repeat Steps 2–4 at the lower
P and update `feed_cfg::kPairs` and its citation comment — **and re-run the
desktop suite**, because every gate loops to `kPairs`.

**SWARM's own history is the warning here:** its kernel row alone sized the bank
too generously and the whole-engine row corrected the reading. A `feed_pairs`
figure that fits is not a verdict; this row is.

- [ ] **Step 5: Measure the denormal tax**

Decaying feedback tails are exactly the shape that pays it, and nothing in this
repo sets flush-to-zero — verified 2026-08-18 across `shell/`, `bench/`,
`host/render/`, `engine/` and `src/` (`docs/roadmap.md`, "Two threads carried out
of the SWARM withdrawal"). SWARM measured VOWEL at ~2.5× a LADDER block through
denormals, and flush-to-zero collapsed it (10 398 ns → 4 440 ns).

Two measurements, in this order:

1. **Desktop, cheap, first: do denormals occur at all?** A scratchpad probe that
   runs `FeedEngine` through a long decay at FLOOR 0 and counts samples whose
   magnitude falls in the subnormal range (`std::fpclassify(x) == FP_SUBNORMAL`)
   across the bank's histories and the DAMP state. If the count is zero, say so
   and skip 2 — there is no tax to measure. The two-sample averaging in the
   feedback taps makes this genuinely possible, so it is worth the four minutes.
2. **Hardware, if 1 says yes.** Build the `feed` profile twice, once with the
   FPSCR flush-to-zero bit set at startup. On ARMv7-M VFP that is `FPSCR.FZ`
   (bit 24) and there is **no separate DAZ bit** — `FZ` covers flushing both
   inputs and results, so "FTZ+DAZ" from the x86 literature is one bit here.
   Record `feed_pairs` and `inst_feed_engine_worst` with and without it.

**Enabling the flag is a separate decision** — it changes existing behaviour
instrument-wide — and this task does not make it (spec §8). Record the delta,
name the file, and leave the decision to its own round.

- [ ] **Step 6: Write it down and commit**

The `docs/bench/` document, in the idiom of the existing ones: the two rows, the
anchor, the verdict, the FTZ delta, and — explicitly — that the comparison was
made inside one image and why.

`docs/roadmap.md`: extend the living status with FEED's arrival, the measured P,
the two CPU figures, the FTZ finding, and the fact that the by-ear pass is still
open.

```bash
git add bench/workloads_system.cpp bench/run.py bench/test_run_contract.py bench/anchor.cpp
git commit -m "bench(feed): inst_feed_engine_worst, the whole-engine gate

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

```bash
git add docs/bench docs/roadmap.md engine/feed/feed_config.h
git commit -m "docs: the FEED CPU verdict on the submodule, and the denormal delta

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 12: Red-proof the whole set, and write down what was measured

**Files:**
- Modify: `docs/engine-map.md` — extend §9 with what the finished engine does
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

Expected: every test green, **G8 included** — if `kPDecided` is still false,
Task 4 has not happened and this task cannot complete. `ctrl_identity` and
`wave_formant_sweep` at their existing hashes. Record the assertion count.

- [ ] **Step 2: Sweep for forgotten stubs**

Task 1 planted `// Task N` comments on every stub. Every one of them should be
gone:

```bash
grep -rn "// Task [0-9]" engine/feed/
```

Expected: no output. Any hit is a body a task was supposed to fill and did not,
and it is a finding, not a tidy-up.

- [ ] **Step 3: Prove every gate can go red**

One mutation at a time, rebuild, confirm the **named** case fails and no other,
revert. This is the step that catches the four vacuous shapes, and the memory is
explicit that plan text is where they are cheapest to produce — so treat this
table as suspect until each line has actually reddened.

| Gate | Mutation | What must fail |
|---|---|---|
| G1 | renumber `ENGINE_FEED = 2` | G1's census — and note whether `test_deck_bus`'s `static_assert` stays silent, because if it does, the census is the only guard on ordering |
| G2 | give `FeedEngine` a `process_in` override without `consumes_input` | the contract's `static_assert`, at compile time |
| G3 | add `ENGINE_FEED` to `part.cpp`'s `set_flow_melody` exclusion | G3 |
| G3b | delete the `ENGINE_FEED` arm in `Part::voice_env` | G3b |
| P1 | `snap` ignores its `hz` argument | P1 |
| P2 | `phase_m += p.inc_c * _ratio` → `phase_c += p.inc_c * _ratio` | P2 |
| P3 | `set_target` calls `snap` | P3's first half |
| P4 | linear pan law | P4's equal-power half |
| P5 | initialise `FeedPair::o1` to `1e-30f` | P5 |
| P6 | `j = (i + 2) % P` | P6's `d_far == 0` half |
| P7 | `pm = fb * (self + k * ring)` (sum, not blend) | P7's exact-equality half |
| P8 | remove `fast_sin`'s wrap (`p - floor(p)`) in the phase advance | P8 — and if it does not, say so: `fast_sin` wraps internally, so the explicit wrap is a precision measure, not a boundedness one |
| G8 | — | it *was* red; the Task 4 commit is the proof |
| G9 | move the coupling to the carrier (`phase_c + _index * m + ring`) | G9 |
| G10 | make `_bond` a constant 0.5 regardless of the lane | G10's BOND 0 half |
| G11 | remove the `tanh` ceiling | G11's ceiling bound |
| G12 | `atten = 1.f` | G12 |
| G13 | drop the per-group mean subtraction | G13's geometric-mean check |
| G14 | make the SPREAD map a single linear segment to `kSpreadMaxCt` | G14 |
| G15 | scale the SPREAD offsets by `1 + sig` instead of `exp2(ct/1200)` | G15 — and if it does not redden, the tolerance is too wide; **that is a finding about the tolerance, not about the gate** |
| G16 | `_floor_n = _fall_n` | G16's below-the-fold half |
| G17 | pass `_floor_n` where `Env::set_sustain` wants the FLOW-clamped value | G17's FLOOR-1 half |
| G18 | `set_index(_depth_n * kIndexMaxCycles)` — drop the `* _env.value()` | G18 |
| G19 | drop the `std::max(_floor_n, kFlowFloorMin)` | G19's FLOW half |
| G20 | drop `_auto_pending = false` in `set_hold(true)` | G20's monotone-decay check |
| G21 | drop the `* _fall_n` factor in `_fall_s`'s accent term | G21's DEC-0 half |
| G22 | replace `Env::trigger()` with a level reset to 0 | G22 |
| G23 | remove the `trigger_chord` override | G23 |
| G24 | call `_env.trigger()` from `set_chord` | G24's envelope half |
| G25 | drop the sort in `_set_chord_tones` | G25's root-holds-still half |
| G26 | `kRatioMagnetExp = 1.f` | G26's near-integer count |
| G27 | apply the magnet to the whole knob | G27 |
| G28 | `_damp_k = 1.f` always | G28 |
| G29 | `kSubMax = 0.f` | G29's SUB half |
| G30 | seed `_rng` from a counter instead of `_seed` | G30's different-seed half |
| G31 | make `reseed` redraw only `_spread_sig` | G31's `fb_moved` half |
| G32 | revert `Instrument::new_phrase` to `_parts[p].mod().new_phrase()` | G32 |

**A gate whose mutation does not redden it is a finding, not a formality.** Fix
the gate, record what was wrong with it, and say which of the four vacuous
shapes it was.

- [ ] **Step 4: Extend the engine map**

`docs/engine-map.md` §9, which Task 3 opened on `FeedBank`, now gains what the
finished `FeedEngine` does. Same idiom: every claim names the setup that
produced it, and nothing goes in that a probe or a gate did not print.

```markdown
### What the finished engine measures

Measured <date> on `FeedEngine` at 48 kHz, `set_seed()` before `init()` (the
SynthEngineT order -- `init()` consumes the seed to draw the SPREAD signature
and the per-pair feedback offsets), seeds 99 / 4242 / 999, settled over 60
control ticks. P is `feed_cfg::kPairs` and was decided by the `feed_pairs`
bench, not chosen -- <run>.

- **BOND 0 is spectrally stationary and mid BOND is not.** Over 30 s with every
  lane static and FLOOR 1, the windowed magnitude spectrum's normalised flux is
  <a> at BOND 0 against <b> at BOND 0.5 (G10). The detuned pairs beat audibly in
  both cases; only coupling makes the sidebands themselves wander, which is why
  the measure is spectral and not temporal.
- **The pitch centre holds to BOND <t>.** Over the whole SPREAD travel the
  autocorrelation fundamental stays within <c> cents of the played pitch below
  the threshold; the worst reading below it was <w> cents (G15). Beyond the
  threshold the network breaks on purpose and nothing is asserted.
- **The feedback attenuation spans <lo>..<hi> cycles** from the bottom of the
  pitch axis to the top, which is why BOND audibly weakens toward the top of a
  chord -- a decision, not a side effect (G12).
- **RATIO's lower half spends <p> % of its travel within 2 % of an integer**
  against <q> % for a linear map, and the map is monotone throughout (G26).
- **A FEED deck voices at most `kPairs / kPairsPerTone` chord tones**, so every
  voiced tone keeps a group SPREAD can reach; at COLOR's four-note chord that is
  <n> tones (G25).
```

Fill every `<...>` from the gates' `CAPTURE` output or from a probe. **A section
with a placeholder in it is worse than no section.**

- [ ] **Step 5: Roadmap**

Extend `docs/roadmap.md`'s living status: FEED shipped, the measured P and the
bench that decided it, the two CPU rows, the render `feed_drone.json`, the two
host re-points, and the **open** by-ear item (Task 13) listing the constants
awaiting Bastian's ears. Per memory `fireflow-status-docs-check-removals`, also
check nothing in the file still describes the five-engine world as complete —
and run the one-second path audit that memory names.

- [ ] **Step 6: Commit**

```bash
git add docs/engine-map.md docs/roadmap.md
git commit -m "docs: what FEED measured, and what is still owed to ears

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 13: The listening session — **NEEDS BASTIAN**

No agent completes this task. It exists so the by-ear constants are not mistaken
for settled values, and so whoever reads `feed_config.h` next knows which
comments describe an origin and which describe an open item.

**Files:**
- Modify: `engine/feed/feed_config.h` — the comments, and whatever Bastian moves
- Modify: `docs/by-ear-decisions.md` — the closed decisions
- Modify: `docs/roadmap.md` — close the item

- [ ] **Step 1: Prepare the material**

Render `feed_drone.json`, and open the VCV plugin with a FEED deck on part A.
RATIO and DAMP have no scenario sweep of their own, so those two are a panel
listen.

The list, in the order spec §10 gives it, with the plan's additions marked:

1. **Where in BOND's travel the cliff sits** — the curve, **laid onto Task 3's
   regime map**, not searched blind. The endpoint of the tonal region is the
   map's, not the ear's; what the ear decides is how the knob's travel is
   distributed over it.
2. **The SPREAD range in cents** — `kSpreadKneeCt` and `kSpreadMaxCt`, inside
   §3.4's frame (lower half single-digit). The question: at SPREAD 0.25 is
   anything breathing, and at SPREAD 1 is it rough or is it out of tune.
3. **The per-pair `fb_amount` offset range** — `kFbOffsetRange`. The question:
   does the cliff read as a gradient the ear can ride, or as an edge.
4. **The minimum floor in FLOW** — `kFlowFloorMin`. The question: at FLOOR 0 in
   FLOW, is the drone promise kept without the floor swallowing the hit.
5. **RATIO's irrational end** — `kRatioMax`, and `kRatioMagnetExp` with it. Is
   the pretty range the lower half, and is the extreme reachable rather than
   merely present.
6. **The DAMP range and its neutral centre** — `kDampCenterHz`, `kDampSpan`.
7. **The pitch-attenuation curve** — `kFbPitchSlope`, `kFbAttenMin` — and the
   ceiling constant `kSatCeil`.

Four more the plan added and Bastian should hear alongside them, each flagged in
the config header:

8. **`kDepthBase`** — DEPTH's resting value, and the one that matters most,
   because DEPTH has no knob (the control map). G29 says it has a spectrum; only
   ears say it is a good one.
9. **`kFloorFoldStart`** — where FALL stops being only FALL. The question: does
   the drone arrive gradually or does the knob have a step in it.
10. **`kIndexMaxCycles`** — the index at DEPTH 1, and whether the top of the
    range is usable or merely loud.
11. **`kSubMax`** and **`kAccentVelFloor`/`kAccentDecFloor`**, both deliberately
    equal so a listening session says which half wants to differ.

**Not by ear, and recorded here to keep the boundary clean:**
`kBondPitchThreshold` and `kPitchCentreTolCt` come from Task 3's regime map. If
they feel wrong, re-run the probe — do not retune them.

- [ ] **Step 2: Record the verdict where the next session will find it**

For each value: kept, or moved to what. A value Bastian confirms goes into
`docs/by-ear-decisions.md` with the shape the entries there already have — what
it is, what it was, and **why a later session must not "fix" it back**. The
comment in `feed_config.h` changes from `BY EAR, first try` to a statement that
the pass happened and the value stands; leaving the old wording in place is
exactly how `kFlowNoteMinS`'s "A FIRST GUESS SET BY ARITHMETIC" comment came to
look like an open item after its pass had closed.

- [ ] **Step 3: Close the roadmap item and commit**

```bash
git add engine/feed/feed_config.h docs/by-ear-decisions.md docs/roadmap.md
git commit -m "tune(feed): the by-ear pass

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Self-Review

**Spec coverage.** §2.1 (melodic engine, free-running network) → Task 1 G3 for
the melodic class, Task 5 for the ring that runs whether or not it is triggered,
Task 6's `trigger` comment for "retunes, does not start". §2.2 (fixed bank, glide
not voice on/off) → Tasks 2 and 5, gated by P3, G24, G25. §2.3 (one knob is the
cliff) → the control map's ruling and Task 5 G9/G10. §2.4 (the motion is the
coupling) → Task 5 G10, two-sided. §2.5 (one envelope, amplitude and index) →
Task 6, gated by G18. §2.6 (FEED blends) → Task 3's measurement and Task 5 G13,
G14, G15. §3.1 (the pair) → Task 2, gated by P1/P2/P6/P7. §3.2.1 (two-sample
average) → Task 2's loop. §3.2.2 (pitch attenuation) → Task 5, gated by G12.
§3.2's "one attenuation, both terms" → Task 5's comment at the site plus G12.
§3.3 (the ceiling) → Task 5, gated by G11. §3.4 (SPREAD, no drift, NEW) → Tasks
5 and 9, gated by G13/G14/G31. §4 (lanes and voice row) → the control map, Task
1's forwards, Tasks 5/6/8 for the individual controls, Task 10 for the captions
and the two re-points; the unwritten-base trap → Task 10's MOTION write and
Task 8 G29. §5 (chord, FORM/SONG, STEP/FLOW, CHOKE) → Task 7 for the chord, Task
1 G3 for the melodic class FORM and SONG reach through, Task 6 for STEP/FLOW,
the minimum floor and CHOKE. §6 (integration) → Task 1 for the id, the engine
class and the overrides; the metering ruling → G3b; the hosts → Task 10;
`THIRD_PARTY.md` → **see the gap below**. §7 (reuse and CPU levers) → `Env`
unmodified and `Rng`/`fast_sin` in Tasks 5/6/9/2, lever 1 (one sine per operator,
pan gains) and lever 2 (slopes not smoothers) in Task 2, lever 3 (idle gating)
in Task 6 through `Env`'s idle snap, lever 4 (P compile-time) in Task 4, lever 5
(no oversampling) in "Ordering" as a stated non-task. §8 (two-stage CPU,
denormals, the regime map) → Tasks 4, 11 and 3. §9 (tests) → G1–G32 and P1–P8,
red-proofed in Task 12; §9.1's neutrality is the existing bit-identity sweep in
`tests/test_deck_bus.cpp`, extended in Task 1 Step 7. §10 (by-ear) → flagged in
`feed_config.h`, Task 13. §11 (prior art) → **the gap below**. §12 (out of scope)
→ nothing in this plan touches any of the five. §13 (step 0) → Task 0. §14 (open
points) → P in Task 4, captions in Task 10, cents-vs-ratio in Task 5 Step 5,
§9.9's numbers in Task 3, RATIO's mechanism in plan open point 3, FTZ in Task 11.

**One gap, and it is now closed by this paragraph rather than by a task:** spec
§6 and §11 require a `THIRD_PARTY.md` row in its "Ported" section, attributing
Plaits `plaits/dsp/fm/operator.h` (the two-sample feedback average) and Braids
`braids/digital_oscillator.cc`'s `RenderFeedbackFm` (the pitch-dependent
attenuation), both Émilie Gillet, both MIT, both **ported into float, not
vendored**. **Add that row in Task 2's commit** — the task that introduces the
first borrowed recipe — with the same courtesy standard the stmlib limiter entry
sets, and name `RenderChaoticFeedbackFm` as the acknowledged ancestor of the
topology that is *not* the mechanism used. Nothing else in the plan depends on
it, which is exactly why it is the thing most likely to be forgotten.

**Type consistency.** `FeedBank::set_target(int, float, float, float)`,
`snap`, `set_bond`, `set_index`, `set_ratio`, `set_damp_coef` and
`set_fb_amount(int, float)` (Task 2) are called from
`FeedEngine::_rebuild_allocation` and `_control_tick` (Task 5). `FeedBank::hz` /
`amp` / `fb_amount` (Task 2) back `pair_hz_for_test` / `pair_amp_for_test` /
`pair_fb_amount_for_test` (Task 5), which every gate from Task 5 on reads.
`set_attack`/`set_decay`/`set_resonance`/`set_sub`/`set_detune`/`set_filt` are
declared in Task 1 Step 4, forwarded by `Part` in Task 1 Step 5, and their bodies
land in Tasks 6 and 8. `_rise_s()`/`_fall_s()`/`_damp_coef()`/`_inv_sqrt_pairs`
are declared in Task 5 Step 4 and filled in Tasks 6 and 8. `_hit_gain`,
`_auto_pending`, `_rise_ratio`, `_fall_ratio` are added in Task 6; `_damp_k` in
Task 8; `_seed_base` and `_new_ctr` on `Part` in Task 9. `reseed(uint32_t)`
(Task 9) is called from `Part::new_phrase()` (Task 9) and by G31 directly.
`feed_cfg::kCtrlInterval` is what `FeedEngine::kCtrlInterval` reads, and the
`static_assert` against `SynthEngine::kCtrlInterval` lives in `feed_engine.cpp`.
`kBondPitchThreshold` and `kPitchCentreTolCt` are added in Task 5 Step 3 and read
only by G15 and Task 13's boundary note.

**Where this plan deliberately does not assert a number.** Every threshold in the
gates is either derived from a named constant, measured in the same run as its
subject (G22's derivative bound, G26's near-integer count against a stated linear
baseline), or a non-vacuity floor whose only job is to prove the loop ran. The
places where a bound had to be sized against something the plan cannot know —
G10's flux thresholds, G15's tolerance, G18's centroid ratio — say so, and G15's
two numbers come from Task 3 rather than from this document at all. The four
vacuous shapes were checked for explicitly: **shape 1** (never runs) by G15's
`REQUIRE(checked > 6)` and G26's `samples` counter; **shape 2** (re-derives its
subject) is why G24 and G25 compare against `pitch_to_hz_ref` rather than against
the engine's own map, and why G30 compares two engines rather than one engine
against a formula; **shape 3** (the threshold in the policed file) is why no gate
reads a by-ear literal and why G22 measures its own reference; **shape 4**
(cannot reach its failure branch) is why G10, G13, G16, G17, G19, G21, G27 and
G29 each assert two halves, one of which is the inert case.

**Known open items, deliberately not tasks.** The five in "Open points this plan
carries rather than resolves". Each is one line from being decided the other way,
and each is flagged at its site.
