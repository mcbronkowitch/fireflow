# Glow Tonality Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop Glow sounding dissonant by bounding the BBD deck's unquantized bend and by drawing the terrain scale weighted instead of uniform.

**Architecture:** Two independent changes in the flow layer. The first adds a runtime clamp on `P_RANGE_A/B` inside `Flow::recompute_and_push`, next to the existing `kBodyFiltFloor` clamp, active only while that deck is pushed as BBD and the instrument runs FLOW. The second replaces `pick_index` with `pick_weighted` in the terrain generator's tonality stage and moves that stage after the adventure draw so the weights can be tempered. Neither touches the engine core, the Fireflow module, or the Daisy firmware.

**Tech Stack:** C++17, clang + Ninja, doctest (vendored in `third_party/`), CMake.

**Spec:** `docs/superpowers/specs/2026-08-07-glow-tonality-design.md`

## Global Constraints

- Build the engine, tests and render host with clang + Ninja only, never MSVC. `source env.sh` first — it puts LLVM on PATH and sets `CC`/`CXX`/`CMAKE_GENERATOR`.
- `-DCMAKE_BUILD_TYPE=Release` is **not optional**. A Debug configure makes `spky_tests` and `ctrl_identity` fail with "SYNTH reference moved". `README.md` omits the flag and is wrong about it.
- Every test added here must be proven RED once before its implementation lands. A test that cannot go red gets fixed, even if this plan mandated it.
- Commit trailer is `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`, never the default Anthropic one.
- All flow-layer tuning values live in `engine/flow/taste.h`. No bare numeric literal for a tunable quantity goes into `terrain.cpp` or `flow.cpp`.
- Saved patches and terrain codes carry no compatibility promise in this dev alpha. Do not add migrations.

**Build and test commands, used unchanged throughout:**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

To run one doctest case: `./build/spky_tests -tc="<full test case name>"`

---

### Task 1: Weighted scale draw

**Files:**
- Modify: `engine/flow/taste.h` (add `#include "pitch/quantizer.h"` near the existing includes; add `kScaleW` beside the stage-1 role tables around line 643)
- Modify: `engine/flow/terrain.cpp:268-296` (swap the tonality block and the adventure block; change the scale draw)
- Test: `tests/test_flow_terrain.cpp` (two new cases at the end of the file)

**Interfaces:**
- Consumes: `pick_weighted(Rng&, const float*, int)` and `temper(float w, float adv)`, both already file-local in `terrain.cpp`'s anonymous namespace (lines 56 and 99). `SCALE_LIST_COUNT` and the `SCALE_*` enumerators from `engine/pitch/quantizer.h`, visible in `spky::flow` through the enclosing `spky` namespace.
- Produces: `spky::flow::kScaleW[SCALE_LIST_COUNT]`, a `constexpr float` array in `ScaleId` order, read by the tests in this task and by nothing else.

- [ ] **Step 1: Write the two failing tests**

Append to `tests/test_flow_terrain.cpp`:

```cpp
// Scale groups, by ScaleId (engine/pitch/quantizer.h), ordered by how much a
// scale can rub when two sustained voices land on it at once. Minor and major
// pentatonic contain neither a minor second nor a tritone; the other three
// pentatonics contain a minor second but no tritone; every seven-note mode
// contains both, which is a property of seven notes in twelve rather than a
// choice among the modes.
static int scale_group(int s) {   // 0 clean pent, 1 mode, 2 mild pent, 3 exotic
    switch (s) {
    case SCALE_MIN_PENT: case SCALE_MAJ_PENT:                 return 0;
    case SCALE_AEOLIAN:  case SCALE_DORIAN:
    case SCALE_MIXO:     case SCALE_LYDIAN:                   return 1;
    case SCALE_HIRAJOSHI: case SCALE_PYGMY: case SCALE_KUMOI: return 2;
    default:                                                  return 3;
    }
}

TEST_CASE("flow terrain: the scale draw is weighted away from friction") {
    const int N = 10000;
    int n[4] = {};
    for (uint32_t k = 1; k <= uint32_t(N); ++k) {
        Terrain t = generate(st(k * 2654435761u));
        ++n[scale_group(int(t.base[P_SCALE] + .5f))];
    }
    const float clean  = float(n[0]) / float(N);
    const float exotic = float(n[3]) / float(N);
    CAPTURE(clean); CAPTURE(exotic);
    // A uniform draw over thirteen gives clean = 2/13 = 0.154 and
    // exotic = 4/13 = 0.308, so both bounds fail against the old code -- that
    // is this case's RED. kScaleW asks for 0.35 / 0.10; adventure tempering
    // pulls both toward uniform and the resulting mixture is 0.301 / 0.106.
    CHECK(clean  > 0.26f);
    CHECK(clean  < 0.34f);
    CHECK(exotic > 0.08f);
    CHECK(exotic < 0.14f);
}

TEST_CASE("flow terrain: adventure reopens the exotic scales") {
    const int N = 20000;
    int lo_n = 0, lo_ex = 0, hi_n = 0, hi_ex = 0;
    for (uint32_t k = 1; k <= uint32_t(N); ++k) {
        Terrain t = generate(st(k * 2654435761u));
        const bool ex = scale_group(int(t.base[P_SCALE] + .5f)) == 3;
        if (t.adventure_base < 0.2f)      { ++lo_n; lo_ex += ex ? 1 : 0; }
        else if (t.adventure_base > 0.5f) { ++hi_n; hi_ex += ex ? 1 : 0; }
    }
    // P(a > 0.5) is 12.5% at kAdventureShape 3, so the high bucket is the
    // small one; both must be big enough for the comparison to mean anything.
    REQUIRE(lo_n > 1000);
    REQUIRE(hi_n > 1000);
    const float lo = float(lo_ex) / float(lo_n);
    const float hi = float(hi_ex) / float(hi_n);
    CAPTURE(lo); CAPTURE(hi);
    // Untempered weights would make these two equal, so this is the case that
    // pins temper() into the draw rather than just the weights. Measured
    // 0.092 low against 0.157 high.
    CHECK(hi > lo + 0.02f);
}
```

- [ ] **Step 2: Run both tests to verify they fail**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/spky_tests -tc="flow terrain: the scale draw is weighted away from friction"
./build/spky_tests -tc="flow terrain: adventure reopens the exotic scales"
```

Expected: both FAIL. The first reports `clean` near 0.154 and `exotic` near 0.308 (the uniform draw). The second reports `lo` and `hi` both near 0.308 and roughly equal, so `hi > lo + 0.02` fails. Record the actual printed numbers in the commit message — they are the RED proof.

- [ ] **Step 3: Add the weight table to `engine/flow/taste.h`**

Add the include beside the existing explicit ones (after `#include "mod/divisions.h"`):

```cpp
// For SCALE_LIST_COUNT and the ScaleId names in the scale weight table below.
// Already transitively visible via flow_params.h -> instrument.h; this only
// makes the dependency explicit, exactly as the engine_iface.h include does.
#include "pitch/quantizer.h"
```

Add the table immediately after `kModeW` (the P_MODE draw weights, around line 643):

```cpp
// Scale draw weights (spec 2026-08-07 §3). A uniform draw over all thirteen
// put whole tone, hijaz, phrygian and harmonic minor together at 31% of
// terrains, which is most of why Glow read dissonant.
//
// Staggered by how much friction a scale can produce when two sustained
// voices land on it at once, read off SCALE_MASKS rather than by feel:
// minor and major pentatonic contain neither a minor second nor a tritone;
// hirajoshi, pygmy and kumoi contain a minor second but no tritone; EVERY
// seven-note mode contains both, which is a property of seven notes in twelve
// and not a choice among the modes; whole tone has no minor second and three
// tritones.
//
// Order is ScaleId (engine/pitch/quantizer.h), so this indexes with
// SCALE_MASKS. Tempered by the terrain's adventure level at the draw site, so
// these are the shape at adventure 0 and the table reads uniform at adventure
// 1 -- an adventurous terrain can still reach whole tone, it just rarely does.
inline constexpr float kScaleW[SCALE_LIST_COUNT] = {
    // modes -- 0.45
    0.1125f, 0.1125f, 0.1125f, 0.1125f,
    // pentatonics -- 0.20 across hirajoshi/pygmy/kumoi, 0.35 across the two
    // that cannot produce a minor second or a tritone
    0.0667f, 0.0667f, 0.1750f, 0.0667f, 0.1750f,
    // exotic / handpan -- 0.10
    0.0250f, 0.0250f, 0.0250f, 0.0250f,
};
```

- [ ] **Step 4: Reorder the two blocks in `engine/flow/terrain.cpp`**

The adventure block (currently at lines 280-296, opening with the comment "The base patch's adventure level (spec 2026-08-06 §7)") must run **before** the tonality block (currently lines 268-278). Move the whole adventure block, comment included, so it sits directly after stage 1's closing brace and before the "Stage 2: tonality" comment.

They draw from different streams (`kStreamAdventure` against `kStreamTonality`), so the move consumes nothing and every other draw stays bit-identical.

Then amend one sentence inside the moved comment. Replace:

```
// Drawn HERE, before stage 3a, because every base draw from 3a onward
// reads it: the mode coin's weights are tempered, and so is every base
// span. Its own stream means the position costs no other stage a value.
```

with:

```
// Drawn HERE, before stage 2, because every draw from stage 2 onward reads
// it: the scale weights are tempered, the mode coin's weights are tempered,
// and so is every base span. Its own stream means the position costs no
// other stage a value -- moving it up past stage 2 (2026-08-07) left every
// other draw bit-identical for that reason.
```

- [ ] **Step 5: Change the scale draw**

In the tonality block, replace:

```cpp
    int scale, root;
    {
        Rng r = make_stream(st.master, kStreamTonality, 0);
        scale = pick_index(r, kParams[P_SCALE].steps);   // 0..12
        root  = pick_index(r, kParams[P_ROOT].steps);    // 0..11
    }
```

with:

```cpp
    static_assert(SCALE_LIST_COUNT == 13,
                  "kScaleW and kParams[P_SCALE].steps must cover the same list");
    int scale, root;
    {
        Rng r = make_stream(st.master, kStreamTonality, 0);
        // Weighted, not uniform (spec 2026-08-07 §3). pick_weighted and
        // pick_index each consume exactly ONE next_unipolar(), so the stream
        // position after this line is unchanged and the ROOT draw below stays
        // bit-identical to the uniform version.
        float w[SCALE_LIST_COUNT];
        for (int i = 0; i < SCALE_LIST_COUNT; ++i)
            w[i] = temper(kScaleW[i], t.adventure_base);
        scale = pick_weighted(r, w, SCALE_LIST_COUNT);   // 0..12
        root  = pick_index(r, kParams[P_ROOT].steps);    // 0..11
    }
```

Also extend the block's own leading comment, replacing:

```
    // Stage 2: tonality. Scale and root are one draw each -- both decks
```

with:

```
    // Stage 2: tonality. Scale and root are one draw each -- the scale
    // weighted by kScaleW and tempered by adventure, the root still
    // uniform -- both decks
```

- [ ] **Step 6: Run the two tests to verify they pass**

```bash
cmake --build build
./build/spky_tests -tc="flow terrain: the scale draw is weighted away from friction"
./build/spky_tests -tc="flow terrain: adventure reopens the exotic scales"
```

Expected: both PASS.

- [ ] **Step 7: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 4/4. If `tests/test_flow_new.cpp` fails, look first at its `REQUIRE(far > 0)` around line 222 — it runs on a fixed seed and aggregates over six parameters including `P_SCALE`, so a changed scale draw could in principle move it. Do **not** relax the assertion; pick a seed for that case that restores a genuine RED-able condition and say so in the commit message.

- [ ] **Step 8: Commit**

```bash
git add engine/flow/taste.h engine/flow/terrain.cpp tests/test_flow_terrain.cpp
git commit -F - <<'EOF'
feat(flow): the scale draw stops being a coin toss over all thirteen

Uniform over thirteen put whole tone, hijaz, phrygian and harmonic minor
together at 31% of terrains. kScaleW staggers the list by how much a scale
can rub when two sustained voices land on it at once -- read off
SCALE_MASKS, not by feel -- and the draw is tempered by adventure like
every other weight table, so an adventurous terrain can still reach whole
tone.

pick_weighted and pick_index each consume one next_unipolar(), and the
adventure block moved up past stage 2 across separate streams, so the ROOT
draw is bit-identical and only the scale changes.

RED before the fix: <paste the measured clean/exotic shares here>.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 2: BBD bend clamp

**Files:**
- Modify: `engine/flow/taste.h` (add two constants beside `kBodyFiltFloor` at line 555; add a third bullet to the "NOT THE COMPLETE LIST OF HARD LIMITS" comment at lines 582-585)
- Modify: `engine/flow/flow.cpp:53-54` (extend the ordering `static_assert`) and `flow.cpp:489-493` (add the clamp after the FILT block)
- Test: `tests/test_flow_runtime.cpp` (one new case at the end of the file)

**Interfaces:**
- Consumes: `spky::flow::kBodyFiltFloor`'s neighbourhood in `taste.h`; `Flow::param_now(int)`, already public; `Flow::_pushed[]` and `Flow::_mode_now`, both private members reachable from inside `recompute_and_push`; `terrain_of(f)`, the test-only accessor already used throughout `test_flow_runtime.cpp`.
- Produces: `spky::flow::kBbdFlowSemis` and `spky::flow::kBbdFlowRangeMax`, both `constexpr float`, read by the clamp and by the test.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_runtime.cpp`:

```cpp
TEST_CASE("flow runtime: a BBD deck in FLOW keeps its bend inside the budget") {
    Instrument in; in.init(48000.f);
    int seen_bbd_flow = 0, seen_free_deck = 0;
    for (uint32_t k = 1; k <= 400; ++k) {
        Flow f = make(in, k * 2654435761u);
        const bool step = terrain_of(f).base[P_MODE] > 0.5f;
        for (int m = 0; m < MACRO_COUNT; ++m) f.set_macro(m, 0.5f);
        for (int i = 0; i < 200; ++i) f.tick();   // past the wake transient
        for (int d = 0; d < 2; ++d) {
            const int ep = d ? P_ENGINE_B : P_ENGINE_A;
            const int rp = d ? P_RANGE_B  : P_RANGE_A;
            const bool bbd = int(f.param_now(ep) + .5f) == ENGINE_BBD;
            CAPTURE(k); CAPTURE(d); CAPTURE(f.param_now(rp));
            if (bbd && !step) {
                ++seen_bbd_flow;
                CHECK(f.param_now(rp) <= kBbdFlowRangeMax);
            } else if (f.param_now(rp) > kBbdFlowRangeMax) {
                ++seen_free_deck;
            }
        }
    }
    // Non-vacuous in BOTH directions. The clamped case has to actually occur
    // -- otherwise the CHECK above never runs -- and a deck that is not a BBD
    // in FLOW has to be seen ABOVE the cap, without which this case would
    // also pass against an implementation that clamped RANGE on every deck
    // unconditionally and killed the pitch lane everywhere.
    CAPTURE(seen_bbd_flow); CAPTURE(seen_free_deck);
    CHECK(seen_bbd_flow > 0);
    CHECK(seen_free_deck > 0);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake --build build
./build/spky_tests -tc="flow runtime: a BBD deck in FLOW keeps its bend inside the budget"
```

Expected: FAIL. The build fails first with `kBbdFlowRangeMax` undeclared — add the constants (Step 3) and re-run to get the real RED, which is the `CHECK(f.param_now(rp) <= kBbdFlowRangeMax)` firing on BBD decks whose RANGE sits in the drawn 0.1–0.8 band. Record the count of failing decks; that is the RED proof.

- [ ] **Step 3: Add the constants to `engine/flow/taste.h`**

Directly after `constexpr float kBodyFiltFloor = -0.3f;` (line 555):

```cpp
// The BBD bend budget under Glow (spec 2026-08-07 §2). In FLOW the BBD's
// PITCH lane is not a note, it is the delay clock, spread geometrically
// across the whole reachable window (bbd_music.h's clock_flow, "a bend, not a
// keyboard"), so a full lane travel is kMaxStages/kMinStages = 32 = 5 octaves
// = 60 semitones against a scale-locked second deck. The flow layer bounds
// that travel by capping P_RANGE_A/B on a deck currently pushed as BBD --
// RANGE is the only lever available, because SuperModulator::set_range
// touches LANE_PITCH and nothing else.
//
// The 60 is that window in semitones. The 2 is apply_range: at r <= 0.5 the
// lane output is unipolar 0..2r (engine/mod/range.h), so the travel is
// one-sided. Written as a semitone budget rather than a raw RANGE value
// because semitones are the quantity the ear judges.
//
// At 1 semitone the cap is 0.0083 and the BBD deck's PITCH lane is
// effectively static: the clock stands still, and the wobble that lane used
// to contribute has to come from DRIFT/MOTION/FLUX instead. That trade was
// stated and the owner ruled for it (2026-08-07). Raising kBbdFlowSemis buys
// the motion back at a proportional cost in off-key travel.
constexpr float kBbdFlowSemis    = 1.f;
constexpr float kBbdFlowRangeMax = kBbdFlowSemis / (2.f * 60.f);
```

Then extend the "THIS IS NOT THE COMPLETE LIST OF HARD LIMITS" comment (lines 582-585) with a third bullet, after the `kBodyFiltFloor` one:

```
//   - kBbdFlowRangeMax is a runtime clamp in flow.cpp for the same reason as
//     kBodyFiltFloor: it is conditional on a deck's engine AND on the
//     operating mode, and this table is independent of both.
```

- [ ] **Step 4: Extend the ordering static_assert in `engine/flow/flow.cpp`**

Replace lines 50-54:

```cpp
// The BODY FILT runtime floor in recompute_and_push() reads this tick's
// already-pushed engine for the deck it is guarding, which only works while
// ENGINE_A/B precede FILT_A/B in the parameter table.
static_assert(P_ENGINE_A < P_FILT_A && P_ENGINE_B < P_FILT_B,
              "ENGINE_A/B must be pushed before FILT_A/B");
```

with:

```cpp
// The BODY FILT floor and the BBD RANGE cap in recompute_and_push() both read
// this tick's already-pushed engine for the deck they guard, which only works
// while ENGINE_A/B precede FILT_A/B and RANGE_A/B in the parameter table.
static_assert(P_ENGINE_A < P_FILT_A && P_ENGINE_B < P_FILT_B,
              "ENGINE_A/B must be pushed before FILT_A/B");
static_assert(P_ENGINE_A < P_RANGE_A && P_ENGINE_B < P_RANGE_B,
              "ENGINE_A/B must be pushed before RANGE_A/B");
```

- [ ] **Step 5: Add the clamp**

In `Flow::recompute_and_push`, immediately after the `P_FILT_A || P_FILT_B` block closes (`flow.cpp:493`), add another `else if` to the same chain:

```cpp
        // The BBD bend cap (spec 2026-08-07 §2), the FILT floor's twin: also
        // conditional on the deck's engine, so also runtime rather than a
        // terrain constraint -- the blend interpolates RANGE between two
        // terrains whose engine assignments differ, so a deck can run as BBD
        // for nearly the whole ramp on a RANGE value drawn by a terrain that
        // never put a BBD there.
        //
        // The mode comes from _mode_now, NOT _pushed[P_MODE]: P_MODE must
        // stay LAST in the parameter table (stream seeding, flow_params.h)
        // and has therefore not been through this loop yet on this tick.
        // _mode_now is the mode the instrument is currently RUNNING, which is
        // the question this guard actually asks. It lags a mode change by one
        // control tick; a mode change happens only on NEW or wake, and one
        // tick at 100-500 Hz is inaudible. On the first forced tick after
        // wake _mode_now is still false (FLOW), so the cap applies and is
        // released a tick later if the terrain turns out to be STEP -- the
        // conservative direction.
        else if (p == P_RANGE_A || p == P_RANGE_B) {
            const int ep = (p == P_RANGE_A) ? P_ENGINE_A : P_ENGINE_B;
            if (int(_pushed[ep] + 0.5f) == ENGINE_BBD && !_mode_now
                && v > kBbdFlowRangeMax)
                v = kBbdFlowRangeMax;
        }
```

- [ ] **Step 6: Run the test to verify it passes**

```bash
cmake --build build
./build/spky_tests -tc="flow runtime: a BBD deck in FLOW keeps its bend inside the budget"
```

Expected: PASS, with `seen_bbd_flow` and `seen_free_deck` both above zero. If `seen_bbd_flow` is 0 the case is vacuous — raise the master count above 400 until BBD-in-FLOW decks appear, and say in the commit message how many were found.

- [ ] **Step 7: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 4/4.

- [ ] **Step 8: Commit**

```bash
git add engine/flow/taste.h engine/flow/flow.cpp tests/test_flow_runtime.cpp
git commit -F - <<'EOF'
feat(flow): the BBD keeps its bend, but not the whole five octaves

In FLOW the BBD's PITCH lane is the delay clock, spread geometrically over
kMaxStages/kMinStages = 32 = 60 semitones, and it does not quantize -- so a
BBD texture deck glides continuously against a scale-locked carrier. Nine
of 48 measured decks played mostly off-grid; two of them were BBDs.

RANGE is the only lever the flow layer has on that lane, so the cap is a
runtime clamp beside kBodyFiltFloor, conditional on the deck's pushed
engine and on _mode_now rather than _pushed[P_MODE] -- P_MODE must stay
last in the table and has not been through the loop yet on this tick.

kBbdFlowSemis is a semitone budget, not a raw RANGE value, because
semitones are what the ear judges. At 1 the lane is nearly static; that
trade is written down where the constant lives.

RED before the fix: <paste the failing deck count here>.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

### Task 3: Re-measure and record the result

**Files:**
- Modify: `docs/superpowers/specs/2026-08-07-glow-tonality-design.md` (append a "6. After" section)
- No engine or test changes.

**Interfaces:**
- Consumes: `build/render.exe`, built by Task 1's configure step; the measurement method described in the spec's §1.
- Produces: nothing other code reads. This task exists because §1's numbers are the only evidence the change worked, and a claim of "it sounds better now" without the after-numbers is not a result.

- [ ] **Step 1: Render the same 24-terrain sweep**

Write the scenarios to the scratchpad, not into the repo. Each is 20 s, all six macros parked at 0.5, no NEW press, master `k * 2654435761` for k in 1..24 — the same population the spec measured:

```bash
SCR="$(mktemp -d)"
python - "$SCR" <<'PY'
import json, sys, os
S = sys.argv[1]
for i in range(1, 25):
    m = "%08X" % (i * 2654435761 & 0xFFFFFFFF)
    sc = {"sample_rate": 48000, "bpm": 120, "duration_s": 20,
          "init": [{"action": "flow_wake", "value": f"F1-{m}-000000000000"}]
                  + [{"action": "flow_macro", "slot": k, "value": 0.5} for k in range(6)],
          "events": []}
    open(os.path.join(S, f"m_{i:02d}_{m}.json"), "w").write(json.dumps(sc))
PY
for f in "$SCR"/m_*.json; do b=$(basename "$f" .json); ./build/render.exe "$f" "$SCR/$b.wav" "$SCR/$b.csv"; done
```

- [ ] **Step 2: Measure the two numbers that changed**

```bash
python - "$SCR" <<'PY'
import csv, glob, sys, collections, os
MASKS = [0x05AD,0x06AD,0x06B5,0x0AD5,0x018D,0x048D,0x04A9,0x028D,0x0295,
         0x05AB,0x05B3,0x09AD,0x0555]
def fits(pcs):
    return any(all(((m >> ((p - r) % 12)) & 1) for p in pcs)
               for m in MASKS for r in range(12))
offgrid = decks = nofit = terrains = 0
for f in sorted(glob.glob(os.path.join(sys.argv[1], "m_*.csv"))):
    rows = list(csv.DictReader(open(f)))
    union = set()
    for d in "ab":
        semi = [float(r[d + "_pcv"]) * 36 for r in rows]
        # tolerance 0.01 semitones: the CSV prints %.4f, so a grid semitone
        # reads as 33.9984 and a tighter test makes every deck look off-grid
        off = sum(1 for s in semi if abs(s - round(s)) > 0.01) / len(semi)
        decks += 1
        if off > 0.2: offgrid += 1
        c = collections.Counter()
        n = 0
        for i in range(1, len(semi) - 1):
            s = semi[i]
            if abs(s - round(s)) > 0.01: continue
            if abs(semi[i-1] - s) > 0.01 or abs(semi[i+1] - s) > 0.01: continue
            c[round(s) % 12] += 1; n += 1
        union |= {p for p, k in c.items() if k >= 0.01 * max(n, 1)}
    terrains += 1
    if not fits(union): nofit += 1
print(f"decks mostly off-grid: {offgrid}/{decks}")
print(f"terrains whose two decks share no scale: {nofit}/{terrains}")
PY
```

Baseline from the spec's §1, for comparison: 9/48 decks off-grid, 4/24 terrains sharing no scale.

- [ ] **Step 3: Record the numbers in the spec**

Append to `docs/superpowers/specs/2026-08-07-glow-tonality-design.md`:

```markdown
## 6. After

Same population, same method as §1, measured after both changes landed:

- decks mostly off-grid: **N/48** (was 9/48)
- terrains whose two decks share no scale: **N/24** (was 4/24)
- scale-group shares over 10 000 masters: clean pentatonic **N**, modes **N**,
  mild pentatonic **N**, exotic **N** (was 0.154 / 0.308 / 0.231 / 0.308
  uniform)

The remaining off-grid decks are SAMPLER decks, which this work deliberately
left free (`part.cpp:211-225`: TUNE there transposes a recording as a whole,
and snapping that to the instrument's scale is meaningless). They are silent
in the render host, which has no input material — whether they matter in VCV
is open and unmeasured.
```

Fill every **N** with the measured value. If the off-grid count did not drop, the clamp is not firing — do not paper over it, go back to Task 2 Step 6 and find out why.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/2026-08-07-glow-tonality-design.md
git commit -F - <<'EOF'
docs(flow): the after-numbers for the Glow tonality work

Same 24 terrains, same method as the spec's section 1. The off-grid deck
count and the no-shared-scale terrain count are what this change was for,
so they belong beside the before-numbers rather than in a commit message.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Not in scope

Named here so a later reader does not mistake them for oversights. All three
are recorded in the spec:

- The SAMPLER's unquantized pitch stays as it is (owner's ruling 2026-08-07).
- BODY's inharmonic partials from `ModeBank`'s stretch are a timbre property,
  not a tonality bug, and are untouched.
- Whether the VCV Glow module ever puts material into the sampler buffer is
  unmeasured and stays unmeasured here.
