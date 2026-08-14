# Flow Patch Transfer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a place by hand in the `Fireflow` module and carry it onto one of `Glow`'s twelve pads, so the pad recalls that patch's base skeleton while the terrain keeps supplying the story layer the six macro knobs move.

**Architecture:** `generate()` gains an optional `BaseOverlay` applied over the `kBaseRules` rows only, between the stage-1 engine write and stage 4. `Flow` stores one overlay beside `_state` and routes both `generate()` call sites through a single private wrapper, so blend, reroll and undo inherit it. A Rack-free converter header turns a Fireflow patch into an overlay plus a report of everything it could not carry. The terrain code stays `F1` at 24 characters and now names the story layer.

**Tech Stack:** C++17, clang + Ninja + CMake for engine and tests, doctest (vendored in `third_party/`), VCV Rack SDK via `host/vcv/build-local.sh`, jansson for module JSON.

**Spec:** [`docs/superpowers/specs/2026-08-11-flow-patch-transfer-design.md`](../specs/2026-08-11-flow-patch-transfer-design.md)

## Global Constraints

- **Everything written into the repo is English** — code, comments, tests, docs, commit messages. The conversation is German; the files are not.
- **Commit trailer is `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`**, never the default Anthropic one.
- **Engine and tests build with clang + Ninja, never MSVC.** `source env.sh` first, and `-DCMAKE_BUILD_TYPE=Release` is **not optional** — a Debug configure makes `spky_tests` and `ctrl_identity` fail with "SYNTH reference moved".
- **Never `source env.sh` in a shell that builds the VCV plugin.** The plugin builds only via `host/vcv/build-local.sh`; the system `g++` on this machine is the Daisy ARM cross-compiler and fails with "MinGW not found".
- **Do not run `build-local.sh install`, and do not launch `Rack.exe`.** Installing while Rack is open puts a modal error dialog on the owner's desktop. Installs happen on his say-so, not an implementer's.
- **A test that cannot go red gets fixed even if this plan mandated it.** Every gate below names its RED recipe; run it, see the failure, then implement. If a RED recipe does not redden, the gate is wrong — report it rather than proceeding.
- **No bit-exactness gates.** Renders are sanity checks, not checksums.
- **`BaseOverlay` must stay trivially copyable.** `Place` holds one and `Glow.cpp` copies the whole `Place` array to the audio thread; a heap-owning member would put a `malloc` in that copy.
- **`kTerrainCodeLen` must not move.** The code stays `F1` at 24 characters, `decode_code` keeps its exact-length check, and `Place::code` keeps its size.
- **Never hardcode which parameters are base-rule parameters.** Derive membership from `kBaseRules` so a `taste.h` change moves it too.

## Background an implementer needs

Three facts about this codebase are invisible from reading any single file, and every task below depends on them. They are measured, not assumed — see spec §2.

1. **`Terrain::base[p]` is not the played value for 25 of 63 parameters.** `Flow::eval_terrain` (`engine/flow/flow.cpp:291`) seeds `out[p] = t.base[p]` and then overwrites `out[c.param]` from the curve for every target of every macro. The `has[c.param]` guard starts false, so the first curve to touch a parameter always wins. For story-owned parameters `base[]` is a curve floor and a tiebreak term, nothing more.
2. **`taste.h` partitions the space horizontally.** `kBaseRules` has exactly **38** rows; the other **25** parameters are story-owned. 38 + 25 = 63 = `P_COUNT`. Note that **`P_COMP_A` is NOT a base rule** — it appears only in a comment inside the table — while `P_COMP_B` is. Do not eyeball this list; derive it.
3. **Reroll counters cannot move the base patch.** Every stage-0..3 draw uses `make_stream(st.master, …, 0)` with the counter pinned at literal zero. Only stages 4 and 5 read `st.reroll[]`.

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `engine/flow/terrain.h` | `BaseOverlay`, `is_base_rule()`, `generate()` overload, the three wish filters — declarations | 1, 2, 4 |
| `engine/flow/terrain.cpp` | overlay application, `a_carries` recomputation, wish-filter bodies | 1, 2, 4 |
| `engine/flow/flow.h` / `flow.cpp` | stores one overlay, routes both `generate()` sites, carries it through the undo slot | 3 |
| `engine/flow/flow_params.h` | the stale "verified against Fireflow.cpp" comment gets corrected | 5 |
| `docs/flow-fireflow-param-map.md` | the authoritative 38-row mapping table, produced before the converter is written | 5 |
| `host/vcv/src/flow_patch_bridge.hpp` | Rack-free converter: `FireflowPatch` → `TransferReport` | 6 |
| `host/vcv/src/touch_pads.hpp` | `Place` grows a `BaseOverlay`; `export_pool_tsv` gains a column | 7 |
| `host/vcv/src/glow_ui.hpp` | `GlowSave` carries the live and undo overlays | 7 |
| `host/vcv/src/Glow.cpp` | JSON round trip, `wakePad`/`pinCurrent`, the paste menu item | 7, 8 |
| `host/vcv/src/Fireflow.cpp` | fills a `FireflowPatch`, the copy menu item | 8 |
| `tests/test_flow_overlay.cpp` | every engine-side gate | 1, 2, 3, 4 |
| `tests/test_flow_patch_bridge.cpp` | every converter gate | 6, 7 |

---

### Task 1: `BaseOverlay` and the `generate()` overload

**Files:**
- Modify: `engine/flow/terrain.h`
- Modify: `engine/flow/terrain.cpp` (insert between lines 399 and 401)
- Create: `tests/test_flow_overlay.cpp`
- Modify: `CMakeLists.txt:162-174` (the flow test source list)

**Interfaces:**
- Consumes: `spky::flow::TerrainState`, `Terrain`, `kParams`, `clamp_to` (all existing).
- Produces: `struct BaseOverlay { float v[P_COUNT]; bool has[P_COUNT]; }`; `bool is_base_rule(int param)`; `Terrain generate(const TerrainState& st, const BaseOverlay* ov)`. Tasks 2, 3, 6, 7 and 8 all use these exact names.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_flow_overlay.cpp`:

```cpp
// tests/test_flow_overlay.cpp
//
// The base overlay (spec 2026-08-11 flow-patch-transfer §4). Two claims, and
// the second one is the whole design: an overlay reaches every kBaseRules
// parameter, and reaches NO story-owned parameter.
#include "doctest.h"
#include "flow/terrain.h"
#include "flow/taste.h"

using namespace spky::flow;

TEST_CASE("is_base_rule agrees with the kBaseRules table") {
    // Derived from the table on both sides on purpose -- but the SUBJECT is
    // the function and the EXPECTATION is the raw table, so a function that
    // stopped reading the table would fail here.
    bool in_table[P_COUNT] = {};
    for (int i = 0; i < kBaseRuleCount; ++i) in_table[kBaseRules[i].param] = true;
    for (int p = 0; p < P_COUNT; ++p) CHECK(is_base_rule(p) == in_table[p]);

    int n = 0;
    for (int p = 0; p < P_COUNT; ++p) if (is_base_rule(p)) ++n;
    CHECK(n == kBaseRuleCount);
    // Pins the two facts the plan's Background section states. If taste.h
    // legitimately grows a base rule, update BOTH numbers together and say so
    // in the commit -- do not delete the assertion.
    CHECK(kBaseRuleCount == 38);
    CHECK(is_base_rule(P_COMP_B));
    CHECK_FALSE(is_base_rule(P_COMP_A));
}

TEST_CASE("an overlay reaches every base-rule parameter") {
    TerrainState st; st.master = 0x51A7E1u;
    const Terrain plain = generate(st, nullptr);

    BaseOverlay ov;
    // A value that differs from the drawn one for every row: take the opposite
    // end of each parameter's own range.
    for (int i = 0; i < kBaseRuleCount; ++i) {
        const int p = kBaseRules[i].param;
        const float mid = 0.5f * (kParams[p].lo + kParams[p].hi);
        ov.v[p]   = plain.base[p] < mid ? kParams[p].hi : kParams[p].lo;
        ov.has[p] = true;
    }
    const Terrain over = generate(st, &ov);

    for (int i = 0; i < kBaseRuleCount; ++i) {
        const int p = kBaseRules[i].param;
        // P_ENGINE_A/B and the constrained rows may be moved again by
        // apply_constraints(); assert the overlay MOVED the value rather than
        // that it landed exactly, so this gate does not fight §4.2's last word.
        CHECK(over.base[p] != doctest::Approx(plain.base[p]));
    }
}

TEST_CASE("an overlay reaches no story-owned parameter") {
    TerrainState st; st.master = 0x7A11E5u;
    const Terrain plain = generate(st, nullptr);

    BaseOverlay ov;
    for (int p = 0; p < P_COUNT; ++p) {
        if (is_base_rule(p)) continue;
        ov.v[p]   = kParams[p].hi;   // as far from any drawn floor as the range allows
        ov.has[p] = true;
    }
    const Terrain over = generate(st, &ov);

    for (int p = 0; p < P_COUNT; ++p)
        CHECK(over.base[p] == doctest::Approx(plain.base[p]));
}

TEST_CASE("a null overlay leaves generate unchanged") {
    TerrainState st; st.master = 0xBEEF01u;
    const Terrain a = generate(st);
    const Terrain b = generate(st, nullptr);
    for (int p = 0; p < P_COUNT; ++p) CHECK(a.base[p] == doctest::Approx(b.base[p]));
    CHECK(a.arch == b.arch);
    CHECK(a.a_carries == b.a_carries);
}

TEST_CASE("an out-of-range overlay value is clamped on the way in") {
    TerrainState st; st.master = 0xC1A11Pu & 0xFFFFFFu;   // any master
    BaseOverlay ov;
    ov.v[P_TUNE_A] = 40.f;      // P_TUNE_A is 0..1
    ov.has[P_TUNE_A] = true;
    const Terrain t = generate(st, &ov);
    CHECK(t.base[P_TUNE_A] <= kParams[P_TUNE_A].hi);
    CHECK(t.base[P_TUNE_A] >= kParams[P_TUNE_A].lo);
}
```

Fix the deliberate typo before committing: `0xC1A11Pu` is not a literal — use `0xC1A11Fu`. It is here so the implementer reads the test rather than pasting it.

Register the file by adding `tests/test_flow_overlay.cpp` to the source list in `CMakeLists.txt` immediately after `tests/test_flow_terrain_code.cpp` (line 169), inside the same target.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Expected: **compile error**, `is_base_rule` and `BaseOverlay` not declared. That is the RED for all five cases at once.

- [ ] **Step 3: Declare the types in `engine/flow/terrain.h`**

Add after the `TerrainState` struct:

```cpp
// A hand-authored base patch riding alongside a seed (spec 2026-08-11 §4.1).
// Indexed by ParamId rather than packed to the 38 base-rule slots: a packed
// form needs a second index table that can drift from kBaseRules, and 315
// bytes is not worth that risk.
//
// Trivially copyable on purpose -- host/vcv/src/touch_pads.hpp's Place holds
// one, and Glow.cpp copies the whole Place array to the AUDIO thread as one
// staged handover. A heap-owning member would put a malloc in that copy.
struct BaseOverlay {
    float v[P_COUNT]   = {};
    bool  has[P_COUNT] = {};
};

static_assert(std::is_trivially_copyable<BaseOverlay>::value,
              "BaseOverlay is copied on the audio thread (Glow.cpp, "
              "UiOp::SET_PLACES); it must not own heap memory");

// True if `param` is set by a kBaseRules row -- i.e. if an overlay entry for
// it is honoured. Reads the table; never a transcribed list, because taste.h
// owns this partition and moves it.
bool is_base_rule(int param);
```

Add `#include <type_traits>` at the top of the file, and change the `generate` declaration to:

```cpp
Terrain generate(const TerrainState& st, const BaseOverlay* ov = nullptr);
```

- [ ] **Step 4: Implement in `engine/flow/terrain.cpp`**

Add near the top, outside the anonymous namespace:

```cpp
bool is_base_rule(int param) {
    for (int i = 0; i < kBaseRuleCount; ++i)
        if (kBaseRules[i].param == param) return true;
    return false;
}
```

Change the signature to `Terrain generate(const TerrainState& st, const BaseOverlay* ov) {`.

Insert between line 399 (`t.base[P_ROOT] = float(root);`) and line 401 (the `// Stage 4:` comment):

```cpp
    // The hand-authored base overlay (spec 2026-08-11 §4.2). Applied HERE and
    // nowhere else, and iterating kBaseRules rather than P_COUNT, which buys
    // three properties the design leans on:
    //
    //   - Story-owned parameters are unreachable BY CONSTRUCTION. Stage 4
    //     below writes base[c.param] = c.bp[0] for every curve target, so an
    //     overlay entry there would be erased anyway -- but erased silently.
    //     Never reading it makes the ruling testable instead of emergent.
    //   - apply_constraints() still gets the last word: it runs after stage 4
    //     and sees the overlaid engines and values.
    //   - kParams clamping happens on the way IN, so a host with a wider knob
    //     cannot put an out-of-range value into a terrain at all.
    //
    // If you are tempted to loop p over P_COUNT here: that is exactly the bug
    // tests/test_flow_overlay.cpp's "reaches no story-owned parameter" case
    // exists to catch.
    if (ov) {
        for (int i = 0; i < kBaseRuleCount; ++i) {
            const int p = kBaseRules[i].param;
            if (ov->has[p]) t.base[p] = clamp_to(kParams[p], ov->v[p]);
        }
    }
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS, and every previously passing test still passes — `generate(st)` keeps its old meaning because `ov` defaults to `nullptr`.

- [ ] **Step 6: Prove the RED on the load-bearing gate**

Temporarily change the apply loop to iterate `for (int p = 0; p < P_COUNT; ++p)` with `if (ov->has[p])`. Rebuild and run.

Expected: **"an overlay reaches no story-owned parameter" FAILS.** Revert the change and confirm green again. If it does not fail, the gate is wrong — stop and report it.

- [ ] **Step 7: Commit**

```bash
git add engine/flow/terrain.h engine/flow/terrain.cpp tests/test_flow_overlay.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(flow): a terrain can be generated over a hand-authored base

generate() takes an optional BaseOverlay applied between the stage-1
engine write and stage 4, iterating kBaseRules rather than P_COUNT.
Story-owned parameters are therefore unreachable by construction rather
than erased by stage 4 a few lines later, which is what makes the rule
testable. apply_constraints() still runs afterwards and keeps the last
word, and values are clamped to kParams on the way in.

is_base_rule() reads the table instead of a transcribed list: taste.h
owns this partition and has moved it before.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 2: `a_carries` follows the overlaid engines

**Files:**
- Modify: `engine/flow/terrain.cpp` (the overlay block from Task 1)
- Modify: `tests/test_flow_overlay.cpp`

**Interfaces:**
- Consumes: `BaseOverlay`, `generate(st, ov)`, `is_base_rule()` from Task 1; `kCarrierEngine` (3 entries: `ENGINE_SYNTH`, `ENGINE_WAVE`, `ENGINE_BODY`) from `taste.h`.
- Produces: no new symbols. Task 6's converter relies on the rejection behaviour to write its report.

**Why this is its own task:** `Terrain::a_carries` is drawn from a coin in stage 1 and published because `Flow` needs it for the staggered discrete switch and the duck schedule (`flow.cpp:99, 156`). An overlay that sets the engines can name a deck holding a texture-only engine, and `switch_phase_for()` then rides SCALE and ROOT with the wrong deck. Nothing crashes and nothing sounds obviously broken — which is why it needs its own gate.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_flow_overlay.cpp`:

```cpp
static bool is_carrier_engine(int e) {
    for (int i = 0; i < 3; ++i) if (kCarrierEngine[i] == e) return true;
    return false;
}

// Find a master whose drawn roles put the carrier on the deck we do NOT want,
// so the test is about the recomputation and not about a lucky coin.
static uint32_t master_with_a_carrying(bool want_a) {
    for (uint32_t m = 1; m < 4000u; ++m) {
        const Terrain t = generate(TerrainState{ m, {} });
        if (t.a_carries == want_a) return m;
    }
    FAIL("no master found -- the roles coin cannot be this skewed");
    return 1u;
}

TEST_CASE("a_carries follows the overlaid engines") {
    // Deck A gets a texture-only engine, deck B a carrier engine. Whatever the
    // coin said, B must carry.
    TerrainState st; st.master = master_with_a_carrying(true);
    BaseOverlay ov;
    ov.v[P_ENGINE_A] = float(ENGINE_SAMPLER); ov.has[P_ENGINE_A] = true;
    ov.v[P_ENGINE_B] = float(ENGINE_SYNTH);   ov.has[P_ENGINE_B] = true;

    const Terrain t = generate(st, &ov);
    CHECK(t.a_carries == false);
    CHECK(int(t.base[P_ENGINE_A] + 0.5f) == ENGINE_SAMPLER);
    CHECK(int(t.base[P_ENGINE_B] + 0.5f) == ENGINE_SYNTH);
}

TEST_CASE("two carrier engines keep the drawn coin") {
    for (bool want : { true, false }) {
        TerrainState st; st.master = master_with_a_carrying(want);
        BaseOverlay ov;
        ov.v[P_ENGINE_A] = float(ENGINE_SYNTH); ov.has[P_ENGINE_A] = true;
        ov.v[P_ENGINE_B] = float(ENGINE_WAVE);  ov.has[P_ENGINE_B] = true;
        CHECK(generate(st, &ov).a_carries == want);
    }
}

TEST_CASE("an overlay with no carrier is rejected whole") {
    TerrainState st; st.master = 0x10AD5u;
    const Terrain plain = generate(st);

    BaseOverlay ov;
    ov.v[P_ENGINE_A] = float(ENGINE_SAMPLER); ov.has[P_ENGINE_A] = true;
    ov.v[P_ENGINE_B] = float(ENGINE_BBD);     ov.has[P_ENGINE_B] = true;
    ov.v[P_TUNE_A]   = 0.9f;                  ov.has[P_TUNE_A]   = true;

    const Terrain t = generate(st, &ov);
    // WHOLE, not just the engines: a terrain with no carrier has no defined
    // role structure, so half-applying it would be worse than not applying it.
    CHECK(t.base[P_TUNE_A] == doctest::Approx(plain.base[P_TUNE_A]));
    CHECK(int(t.base[P_ENGINE_A] + 0.5f) == int(plain.base[P_ENGINE_A] + 0.5f));
    CHECK(t.a_carries == plain.a_carries);
    CHECK_FALSE(is_carrier_engine(ENGINE_SAMPLER));   // the premise, pinned
    CHECK_FALSE(is_carrier_engine(ENGINE_BBD));
}
```

- [ ] **Step 2: Run to verify they fail**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: "a_carries follows the overlaid engines" and "an overlay with no carrier is rejected whole" FAIL. "two carrier engines keep the drawn coin" passes already — it describes today's behaviour, and it is here to pin that the fix does not disturb it.

- [ ] **Step 3: Implement**

Replace the Task 1 overlay block in `engine/flow/terrain.cpp` with:

```cpp
    if (ov) {
        // §4.3: an overlay that sets the engines can contradict the roles coin.
        // Decide the carrier from the engines themselves before applying
        // anything, because a terrain with no carrier has no role structure at
        // all -- switch_phase_for() would ride SCALE and ROOT with a deck
        // holding a texture-only engine, and the duck schedule would protect
        // the wrong one. Nothing crashes; it just quietly staggers wrong.
        const bool sets_engines = ov->has[P_ENGINE_A] && ov->has[P_ENGINE_B];
        bool ok = true;
        bool carries_a = t.a_carries;
        if (sets_engines) {
            const int ea = int(clamp_to(kParams[P_ENGINE_A], ov->v[P_ENGINE_A]) + 0.5f);
            const int eb = int(clamp_to(kParams[P_ENGINE_B], ov->v[P_ENGINE_B]) + 0.5f);
            const bool ca = is_carrier_engine(ea), cb = is_carrier_engine(eb);
            if (ca && cb)      carries_a = t.a_carries;   // both eligible: keep the coin
            else if (ca)       carries_a = true;
            else if (cb)       carries_a = false;
            else               ok = false;                // the "loud pair": no carrier
        }
        if (ok) {
            for (int i = 0; i < kBaseRuleCount; ++i) {
                const int p = kBaseRules[i].param;
                if (ov->has[p]) t.base[p] = clamp_to(kParams[p], ov->v[p]);
            }
            t.a_carries = carries_a;
        }
    }
```

Add beside `is_base_rule()` in the same file:

```cpp
// Whether `engine` may lead a terrain. kCarrierEngine is taste.h's stage-1
// role table {SYNTH, WAVE, BODY}; the exclusion of SAMPLER and BBD from it is
// what makes taste.h's "loud pair" rule structural rather than a check.
bool is_carrier_engine(int engine) {
    for (int i = 0; i < 3; ++i) if (kCarrierEngine[i] == engine) return true;
    return false;
}
```

Declare it in `engine/flow/terrain.h` beside `is_base_rule`, and drop the local copy from the test file (the test's `is_carrier_engine` helper above becomes `using spky::flow::is_carrier_engine;` — delete the static helper so the test cannot pass against its own duplicate).

- [ ] **Step 4: Run to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS, all of Task 1's cases still green.

- [ ] **Step 5: Prove the RED on the rejection**

Temporarily change `else ok = false;` to `else carries_a = true;`. Rebuild.

Expected: **"an overlay with no carrier is rejected whole" FAILS** on the `P_TUNE_A` assertion. Revert and confirm green.

- [ ] **Step 6: Commit**

```bash
git add engine/flow/terrain.h engine/flow/terrain.cpp tests/test_flow_overlay.cpp
git commit -m "$(cat <<'EOF'
feat(flow): the carrier deck follows the overlaid engines

a_carries is drawn from the roles coin and published because Flow needs
it for the staggered discrete switch and the duck schedule. An overlay
that sets the engines can name a deck holding a texture-only engine, and
then SCALE and ROOT ride with the wrong deck -- silently, since nothing
crashes and the audio still arrives.

So the carrier is recomputed from the overlaid engines: one eligible
deck carries, two keep the coin, and neither rejects the overlay WHOLE.
Half-applying a terrain with no role structure would be worse than not
applying it. Today that last branch is reachable only for the four pairs
taste.h cannot draw at all.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 3: `Flow` carries the overlay, including the undo slot

**Files:**
- Modify: `engine/flow/flow.h`
- Modify: `engine/flow/flow.cpp:85-100` (`wake`), `:143-152` (`begin_blend`), `:208-215` (`undo`), `:221-224` (`restore_undo`)
- Modify: `tests/test_flow_overlay.cpp`

**Interfaces:**
- Consumes: `BaseOverlay`, `generate(st, ov)` from Task 1.
- Produces: `void Flow::wake(const TerrainState& s, const BaseOverlay* ov)`; `void Flow::restore_undo(const TerrainState& s, bool have_undo, const BaseOverlay* ov)`; `const BaseOverlay* Flow::overlay() const`. Tasks 7 and 8 call all three.

**The trap this task exists to close:** `_undo` is a bare `TerrainState` (`flow.cpp:93, 147, 209, 221`). Undoing across two pads with different overlays would combine one place's base with another's stories — silently, with nothing in the code able to catch it.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_flow_overlay.cpp`:

```cpp
#include "flow/flow.h"
#include "instrument.h"

// A minimal harness: Flow needs an Instrument to push into, and the tests here
// only read Flow's own view of the terrain, never the audio.
struct FlowFixture {
    spky::Instrument inst;
    Flow             flow;
    FlowFixture() { inst.init(48000.f); flow.init(&inst, 100.f); }
};

static BaseOverlay tune_overlay(float tune_a) {
    BaseOverlay ov;
    ov.v[P_TUNE_A] = tune_a; ov.has[P_TUNE_A] = true;
    return ov;
}

TEST_CASE("wake applies the overlay") {
    FlowFixture f;
    const BaseOverlay ov = tune_overlay(0.87f);
    TerrainState st; st.master = 0x515Eu;
    f.flow.wake(st, &ov);
    CHECK(f.flow.terrain_for_test().base[P_TUNE_A] == doctest::Approx(0.87f));
}

TEST_CASE("a hold-reroll keeps the overlay") {
    FlowFixture f;
    const BaseOverlay ov = tune_overlay(0.87f);
    TerrainState st; st.master = 0x515Eu;
    f.flow.wake(st, &ov);
    REQUIRE(f.flow.new_partial(0x3F));
    CHECK(f.flow.terrain_for_test().base[P_TUNE_A] == doctest::Approx(0.87f));
}

TEST_CASE("undo restores the overlay that belongs to the terrain it restores") {
    FlowFixture f;
    const BaseOverlay a = tune_overlay(0.10f);
    const BaseOverlay b = tune_overlay(0.90f);

    TerrainState sa; sa.master = 0xAAA1u;
    TerrainState sb; sb.master = 0xBBB2u;
    f.flow.wake(sa, &a);
    REQUIRE(f.flow.new_full());            // accepted press: arms the undo slot
    f.flow.wake(sb, &b);
    f.flow.wake(sa, &a);
    REQUIRE(f.flow.new_full());
    REQUIRE(f.flow.undo());

    // The failure this catches: the seed goes back and the overlay does not,
    // so the restored terrain is one place's base under another's stories.
    CHECK(f.flow.terrain_for_test().base[P_TUNE_A] == doctest::Approx(0.10f));
}

TEST_CASE("waking without an overlay clears the previous one") {
    FlowFixture f;
    const BaseOverlay ov = tune_overlay(0.87f);
    TerrainState st; st.master = 0x515Eu;
    f.flow.wake(st, &ov);
    const float overlaid = f.flow.terrain_for_test().base[P_TUNE_A];

    f.flow.wake(st, nullptr);
    // A pad with no overlay must play the drawn terrain, not the last pad's
    // base. Same rule Glow.cpp already applies to a place with no code.
    CHECK(f.flow.terrain_for_test().base[P_TUNE_A] != doctest::Approx(overlaid));
}
```

`terrain_for_test()` does not exist yet; Step 3 adds it beside the existing `*_for_test` accessors in `flow.h` (`switch_phase_for_test`, `duck_t_for_test`), which is this file's established pattern for exposing internals to the suite.

- [ ] **Step 2: Run to verify they fail**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: **compile error** — `wake` takes one argument, `terrain_for_test` does not exist.

- [ ] **Step 3: Implement**

In `engine/flow/flow.h`, add to the public surface:

```cpp
    // Wake onto `s`, optionally over a hand-authored base (spec 2026-08-11 §4).
    // ov == nullptr CLEARS any stored overlay: a pad with no patch must play
    // the drawn terrain, not the last pad's base.
    void wake(const TerrainState& s, const BaseOverlay* ov = nullptr);

    const BaseOverlay* overlay() const { return _have_overlay ? &_overlay : nullptr; }

    const Terrain& terrain_for_test() const { return _terrain; }
```

Change `restore_undo` to `void restore_undo(const TerrainState& s, bool have_undo, const BaseOverlay* ov = nullptr);`

Add to the private members:

```cpp
    BaseOverlay _overlay;                 // the live place's hand-authored base
    bool        _have_overlay = false;
    BaseOverlay _undo_overlay;            // the undo slot's -- see undo() below
    bool        _have_undo_overlay = false;

    // Both generate() call sites go through here. There are exactly two
    // (wake and begin_blend), and routing them through one wrapper is what
    // lets reroll, undo and the blend inherit the overlay for free.
    Terrain gen(const TerrainState& s) const {
        return generate(s, _have_overlay ? &_overlay : nullptr);
    }
```

In `engine/flow/flow.cpp`:

- `wake()` — take the new argument, store it **before** generating, and pair the undo overlay with `_undo`:

```cpp
void Flow::wake(const TerrainState& s, const BaseOverlay* ov) {
    _have_overlay = ov != nullptr;
    if (ov) _overlay = *ov;
    _state = s;
    _terrain = gen(s);
```

…and further down, where `_undo = s;` sits (line 93), add `_undo_overlay = _overlay; _have_undo_overlay = _have_overlay;`.

- `begin_blend()` (line 149) — `_terrain = generate(target);` becomes `_terrain = gen(target);`. Where `_undo = _state;` sits (line 147), add the same overlay pairing:

```cpp
    _undo  = _state;
    _undo_overlay = _overlay;            // the slot carries the pair, not the seed
    _have_undo_overlay = _have_overlay;
```

- `undo()` (line 208) — swap the overlays with the states. `begin_blend` overwrites `_undo` with what we are leaving, so capture both first:

```cpp
    const TerrainState back = _undo;
    const BaseOverlay  back_ov = _undo_overlay;
    const bool         back_has = _have_undo_overlay;
    _have_overlay = back_has;            // set BEFORE begin_blend: gen() reads it
    if (back_has) _overlay = back_ov;
    begin_blend(back);
```

- `restore_undo()` (line 221) — store the overlay alongside:

```cpp
void Flow::restore_undo(const TerrainState& s, bool have_undo, const BaseOverlay* ov) {
    _undo = s;
    _have_undo = have_undo;
    _have_undo_overlay = ov != nullptr;
    if (ov) _undo_overlay = *ov;
}
```

Add `#include "flow/terrain.h"` if `flow.h` does not already have it (it does, via `Terrain`).

- [ ] **Step 4: Run to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS. Every existing `flow.wake(st)` call site still compiles — the argument defaults to `nullptr`.

- [ ] **Step 5: Prove the RED on the undo pairing**

Temporarily delete the two `_undo_overlay = _overlay;` lines in `wake()` and `begin_blend()`. Rebuild.

Expected: **"undo restores the overlay that belongs to the terrain it restores" FAILS.** Revert and confirm green.

- [ ] **Step 6: Commit**

```bash
git add engine/flow/flow.h engine/flow/flow.cpp tests/test_flow_overlay.cpp
git commit -m "$(cat <<'EOF'
flow: the overlay rides with the state, undo slot included

There are exactly two generate() call sites -- wake and begin_blend --
so one private gen() wrapper is enough for reroll, undo and the blend to
inherit a stored overlay with no further edits.

The undo slot needed explicit work. _undo was a bare TerrainState, so
undoing across two pads with different overlays would have combined one
place's base with another's stories: the seed goes back, the base does
not, and nothing in the code could notice. It now carries the pair.

wake(s, nullptr) clears a stored overlay rather than keeping it. A pad
with no patch must play the drawn terrain, not the last pad's base.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 4: Wish filters — order a seed instead of searching for one

**Files:**
- Modify: `engine/flow/terrain.h`, `engine/flow/terrain.cpp`
- Modify: `tests/test_flow_overlay.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `void roles_of(uint32_t master, int& engine_a, int& engine_b, bool& a_carries);` `void tonality_of(uint32_t master, int& scale, int& root);` `int mode_of(uint32_t master);`. Task 8's menu uses them.

**Why:** archetype, roles, tonality and mode are pure functions of the master at counter zero (Background fact 3), so a seed can be *ordered* rather than searched. `arch_of()` set this precedent: `draw_new`'s genre branch already uses it to reject candidates before paying for a full `generate()`. Spec §2.1 measured that searching for a *patch* reaches only 0.037 even when a perfect seed exists; a wish over these four is satisfied **exactly**, at roughly 1 in 300 masters.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_overlay.cpp`:

```cpp
TEST_CASE("the wish filters agree with generate, over many masters") {
    for (uint32_t m = 1; m < 600u; ++m) {
        const Terrain t = generate(TerrainState{ m, {} });

        int ea = -1, eb = -1; bool ac = false;
        roles_of(m, ea, eb, ac);
        int scale = -1, root = -1;
        tonality_of(m, scale, root);

        CAPTURE(m);
        CHECK(ea == int(t.base[P_ENGINE_A] + 0.5f));
        CHECK(eb == int(t.base[P_ENGINE_B] + 0.5f));
        CHECK(ac == t.a_carries);
        CHECK(scale == int(t.base[P_SCALE] + 0.5f));
        CHECK(root  == int(t.base[P_ROOT]  + 0.5f));
        CHECK(mode_of(m) == int(t.base[P_MODE] + 0.5f));
    }
}

TEST_CASE("the wish filters ignore the reroll counters") {
    // The point of the filters: everything they report is drawn at counter 0,
    // so a rerolled terrain still answers the same wish. If this ever fails,
    // the filters are reading a stage that moved.
    TerrainState st; st.master = 0x2C0FFEEu & 0xFFFFFFu;
    for (int m = 0; m < MACRO_COUNT; ++m) st.reroll[m] = uint16_t(7 * m + 3);
    const Terrain t = generate(st);

    int ea = -1, eb = -1; bool ac = false;
    roles_of(st.master, ea, eb, ac);
    CHECK(ea == int(t.base[P_ENGINE_A] + 0.5f));
    CHECK(eb == int(t.base[P_ENGINE_B] + 0.5f));
    CHECK(mode_of(st.master) == int(t.base[P_MODE] + 0.5f));
}
```

- [ ] **Step 2: Run to verify it fails**

Expected: **compile error** — the three functions do not exist.

- [ ] **Step 3: Implement**

The bodies are the corresponding blocks of `generate()`, lifted verbatim. `generate()` must then **call them** rather than keep its own copy — that is the whole point, and it is the same treatment `arch_of()` already received (`terrain.h:110` records that `generate()` calls it "so the two cannot drift apart").

In `engine/flow/terrain.cpp`:

```cpp
void roles_of(uint32_t master, int& engine_a, int& engine_b, bool& a_carries) {
    const Archetype arch = arch_of(master);
    Rng r = make_stream(master, kStreamRoles, 0);
    a_carries = r.next_unipolar() < 0.5f;
    const int carrier = kCarrierEngine[pick_weighted(r, kCarrierW[arch], 3)];
    const int texture = kTextureEngine[pick_weighted(r, kTextureW[arch], 5)];
    engine_a = a_carries ? carrier : texture;
    engine_b = a_carries ? texture : carrier;
}
```

`tonality_of` and `mode_of` lift the stage-2 and stage-3a blocks the same way. Both read `adventure_base`, which is itself a pure function of the master (`terrain.cpp:287`), so each filter draws it from `make_stream(master, kStreamAdventure, 0)` first.

Then replace the corresponding blocks in `generate()` with calls, keeping the existing comments in place.

- [ ] **Step 4: Run to verify it passes**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: PASS, **and every pre-existing terrain test still passes** — this is a refactor, so no terrain may change. If `test_flow_terrain.cpp` reddens, the lift changed the RNG draw order; fix the lift, not the test.

- [ ] **Step 5: Prove the RED**

Temporarily swap `carrier` and `texture` in `roles_of`'s two assignments. Rebuild.

Expected: **both new cases FAIL.** Revert and confirm green.

- [ ] **Step 6: Commit**

```bash
git add engine/flow/terrain.h engine/flow/terrain.cpp tests/test_flow_overlay.cpp
git commit -m "$(cat <<'EOF'
feat(flow): a seed can be ordered instead of searched

Archetype, roles, tonality and mode are pure functions of the master at
counter zero. roles_of / tonality_of / mode_of expose that, and
generate() now calls them rather than keeping a second copy -- the same
treatment arch_of already has, and for the same reason.

Searching for a seed whose terrain resembles a given patch reaches 0.037
mean normalized delta even where a perfect seed provably exists. A wish
over these four is satisfied exactly, at roughly 1 in 300 masters.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 5: The parameter map, written down before any code depends on it

**Files:**
- Create: `docs/flow-fireflow-param-map.md`
- Modify: `engine/flow/flow_params.h:22-47` (the stale comment)
- Modify: `docs/superpowers/specs/2026-08-11-flow-patch-transfer-design.md:§11`

**Interfaces:**
- Consumes: nothing.
- Produces: the table Task 6 implements. Task 6 must not invent a mapping that is not in this file.

**Why this is a task and not a step:** the converter's risk is not its code, it is its table. `flow_params.h` currently claims its ranges were "verified against `Fireflow.cpp`'s own `configParam` calls" and cites a `FORM_A/B configSwitch(0.f, 4.f, …)` that the 2026-08-09 control reduction deleted. That stale comment is the evidence the discarded design leaned on. The `fireflow-control-merge-init-trap` memory records that this repo has been bitten four times by exactly this class of conversion error. Writing the table first, as a reviewable artifact, is what stops a fifth.

- [ ] **Step 1: Enumerate the 38 base-rule parameters from the table, not from memory**

```bash
awk '/^inline const BaseRule kBaseRules\[\] = \{/,/^\};/' engine/flow/taste.h \
  | grep -E "^\s*\{" | grep -o "P_[A-Z_]*" | sort
```

Expected: exactly 38 lines. **`P_COMP_A` must not appear** (it is story-owned; it occurs only in a comment inside the table) and `P_COMP_B` must.

- [ ] **Step 2: Enumerate Fireflow's surface**

Read `host/vcv/src/generated_panel.hpp`'s `enum ParamId` (the authoritative list; `PART_STRIDE` is 20, part A occupies `[0, 20)` and part B the next 20) and every `inst.set_*` call in `Fireflow.cpp`'s control-push block, roughly lines 540–930.

- [ ] **Step 3: Write `docs/flow-fireflow-param-map.md`**

One row per base-rule parameter, in `ParamId` order, with these columns: **flow param | Fireflow ParamId | conversion | notes**. Every row's conversion must be one of `direct`, an explicit formula, or `UNREACHABLE` with the reason.

Seed the file with the conversions already established, each verified at the cited line:

| flow param | Fireflow | conversion |
|---|---|---|
| `P_ENGINE_A/B` | `ENGINE_A/B` | renumber (`Fireflow.cpp:649-655`): 0→SYNTH, 2→WAVE, 3→BODY, 4→BBD, anything else→SAMPLER. Position 1 is TEST_TONE only when the part's separate `testTone` flag is set, so it is not a knob position and does not transfer |
| `P_DEPTH_A/B` | `MOD_A/B` | direct (`Fireflow.cpp:567` — `set_depth(p, pp(MOD_A, p))`; the knob is named MOD, the parameter DEPTH. See the `set_depth` collision in `spotykach-gotchas`) |
| `P_RANGE_A/B` | `RANGE_A/B` | direct (`Fireflow.cpp:566`) |
| `P_CHOKE` | `CHOKE` | ×0.5 |
| `P_COUPLE` | `COUPLE` | grid-zone split; also the only source of `set_sync` (`Fireflow.cpp:876-884`) |
| `P_TEMPO_BPM` | `TEMPO` | Fireflow 40–240 against flow's 50–140: clamp, and report anything outside |
| `P_COMP_B` | `COMP_B` | the knob is LVL and drives `set_part_level`, which flow never calls; the runtime veto then forces the value into 0.40–0.60 |
| `P_FORM_B`, `P_SONG_B` | `SONG_B` | one 14-rung ladder through the 5×7 `(Principle, SongMode)` grid, via `song_ladder_at()` (`Fireflow.cpp:845`) |
| `P_MODE` | — | folded into COUPLE's zone split and `STEPS == 0`; flow has one global `P_MODE` driving `set_sync` and both decks' step flags, so a patch with deck A free and deck B stepped has no representation |
| `P_ROOT` | — | **UNREACHABLE**: no ROOT control exists (`set_root` appears zero times in `Fireflow.cpp`) |

Resolve every remaining row by reading the code. Do not guess: if a row cannot be resolved from `Fireflow.cpp`, mark it `UNREACHABLE` and say why.

- [ ] **Step 4: Verify the table is complete**

The file must contain exactly 38 parameter rows, and the union of "mapped" and "UNREACHABLE" must be all 38. State both counts explicitly at the top of the file (e.g. "33 mapped, 5 unreachable, 38 total"). A reviewer checks this arithmetic; a table that silently omits a row is the failure mode this task exists to prevent.

- [ ] **Step 5: Correct the stale comment in `engine/flow/flow_params.h`**

Its FORM bullet (lines 31-34) cites a control that no longer exists. Replace that bullet with the current fact — FORM has no Fireflow control since the 2026-08-09 control reduction; SONG reaches 14 of 35 `(Principle, SongMode)` pairs through `song_ladder_at()` — and add a line pointing at `docs/flow-fireflow-param-map.md` as the authority for the correspondence. Leave the ENGINE, SONG-range, STEPS and LINK bullets alone; re-verify each against `Fireflow.cpp` first and correct any that have also drifted.

- [ ] **Step 6: Correct the spec's scope section**

Spec §11 lists what is in scope and omits §7's wish filters, which Task 4 built. Add them to the In list.

- [ ] **Step 7: Commit**

```bash
git add docs/flow-fireflow-param-map.md engine/flow/flow_params.h docs/superpowers/specs/2026-08-11-flow-patch-transfer-design.md
git commit -m "$(cat <<'EOF'
docs: write the Fireflow/flow parameter map down before relying on it

flow_params.h claimed its ranges were verified against Fireflow's
configParam calls and cited a FORM_A/B configSwitch the 2026-08-09
control reduction deleted. That stale comment is the evidence the
discarded "pin eleven discrete values" design leaned on.

So the correspondence gets an authoritative file: one row per base-rule
parameter, each either mapped with its conversion or marked UNREACHABLE
with a reason, and the counts stated so an omission is visible. Five
have no path out of Fireflow at all.

The converter implements this table and invents nothing. Four earlier
conversion changes in this repo shipped a silent default change; see
fireflow-control-merge-init-trap.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 6: `flow_patch_bridge.hpp` — the converter and its report

**Files:**
- Create: `host/vcv/src/flow_patch_bridge.hpp`
- Create: `tests/test_flow_patch_bridge.cpp`
- Modify: `CMakeLists.txt` (add the test source beside `tests/test_flow_overlay.cpp`)

**Interfaces:**
- Consumes: `BaseOverlay`, `is_base_rule()`, `is_carrier_engine()` from Tasks 1-2; the table from Task 5.
- Produces:
  - `struct FireflowPatch { float p[spkyvcv::kFireflowParamCount]; bool test_tone[2]; };`
  - `struct TransferNote { int param; const char* reason; };`
  - `inline constexpr int kMaxNotes = spky::flow::P_COUNT + 8;`
  - `struct TransferReport { spky::flow::BaseOverlay overlay; TransferNote notes[kMaxNotes]; int note_count; bool overlay_rejected; };`
  - `TransferReport to_flow_base(const FireflowPatch&);`
  - `std::string format_report(const TransferReport&);`
  - `std::string encode_base(const spky::flow::BaseOverlay&);` and `bool decode_base(const char* text, spky::flow::BaseOverlay& out);` — the **one** textual encoding in this project, `param:value;` pairs keyed on `kParams[p].name`. Task 7 uses it for `pool.tsv` **and** for Glow's JSON; Task 8 uses it for the clipboard. Three consumers, one encoder, one round-trip test.

  Task 8 fills a `FireflowPatch` from `params[]` and shows `format_report`'s string.

**Pattern to follow:** `host/vcv/src/touch_pads.hpp` and `glow_ui.hpp` — no `<rack.hpp>`, no jansson, no widgets, so `spky_tests` compiles it headlessly (`target_include_directories(spky_tests PRIVATE host)` is already in `CMakeLists.txt`). The input is a plain float array, not a `Module`, which is what keeps the header Rack-free.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_flow_patch_bridge.cpp`:

```cpp
// tests/test_flow_patch_bridge.cpp
//
// The Fireflow -> flow converter (spec 2026-08-11 §5). The report is the
// deliverable: what could NOT be carried matters more than what could.
#include "doctest.h"
#include "src/flow_patch_bridge.hpp"
#include "flow/taste.h"

using namespace spky::flow;
using namespace spkyvcv;

static bool has_note_for(const TransferReport& r, int param) {
    for (int i = 0; i < r.note_count; ++i) if (r.notes[i].param == param) return true;
    return false;
}

TEST_CASE("the converter sets only base-rule parameters") {
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    for (int p = 0; p < P_COUNT; ++p)
        if (r.overlay.has[p]) CHECK(is_base_rule(p));
}

TEST_CASE("every unreachable parameter is reported, every time") {
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    // P_ROOT has no Fireflow control at all. A converter that silently left it
    // at zero would look correct and lose a third of the tonality.
    CHECK_FALSE(r.overlay.has[P_ROOT]);
    CHECK(has_note_for(r, P_ROOT));
}

TEST_CASE("the engine renumber follows Fireflow's own mapping") {
    struct Case { float knob; int engine; };
    const Case cases[] = {
        { 0.f, ENGINE_SYNTH }, { 1.f, ENGINE_SAMPLER }, { 2.f, ENGINE_WAVE },
        { 3.f, ENGINE_BODY },  { 4.f, ENGINE_BBD },
    };
    for (const Case& c : cases) {
        FireflowPatch fp{};
        fp.p[kFfEngineA] = c.knob;
        fp.p[kFfEngineB] = 0.f;              // SYNTH: a valid carrier on B
        const TransferReport r = to_flow_base(fp);
        CAPTURE(c.knob);
        REQUIRE_FALSE(r.overlay_rejected);
        CHECK(int(r.overlay.v[P_ENGINE_A] + 0.5f) == c.engine);
    }
}

TEST_CASE("the test tone does not transfer") {
    FireflowPatch fp{};
    fp.p[kFfEngineA] = 1.f;                  // the Sampler position
    fp.test_tone[0]  = true;
    const TransferReport r = to_flow_base(fp);
    // TEST_TONE is in neither kCarrierEngine nor kTextureEngine -- taste.h says
    // the generator must never roll it, so the converter must never write it.
    CHECK(int(r.overlay.v[P_ENGINE_A] + 0.5f) == ENGINE_SAMPLER);
    CHECK(has_note_for(r, P_ENGINE_A));
}

TEST_CASE("a loud pair is rejected whole and said so") {
    FireflowPatch fp{};
    fp.p[kFfEngineA] = 1.f;                  // SAMPLER
    fp.p[kFfEngineB] = 4.f;                  // BBD
    const TransferReport r = to_flow_base(fp);
    CHECK(r.overlay_rejected);
    CHECK(r.note_count > 0);
    // Nothing may be carried from a rejected transfer -- see Task 2.
    for (int p = 0; p < P_COUNT; ++p) CHECK_FALSE(r.overlay.has[p]);
}

TEST_CASE("an out-of-range tempo is clamped AND reported") {
    FireflowPatch fp{};
    fp.p[kFfEngineB] = 0.f;
    fp.p[kFfTempo]   = 180.f;                // flow's ceiling is 140
    const TransferReport r = to_flow_base(fp);
    CHECK(r.overlay.v[P_TEMPO_BPM] == doctest::Approx(kParams[P_TEMPO_BPM].hi));
    CHECK(has_note_for(r, P_TEMPO_BPM));
}

TEST_CASE("a value the runtime veto will rewrite is reported before it is heard") {
    FireflowPatch fp{};
    fp.p[kFfEngineB] = 0.f;
    fp.p[kFfCompB]   = 0.85f;                // veto band is 0.40..0.60
    const TransferReport r = to_flow_base(fp);
    CHECK(r.overlay.has[P_COMP_B]);          // it transfers...
    CHECK(has_note_for(r, P_COMP_B));        // ...and the owner is told it will not be heard
}

TEST_CASE("the report is never silently truncated") {
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    CHECK(r.note_count <= kMaxNotes);
    CHECK(kMaxNotes >= P_COUNT);             // one note per param is always representable
}

TEST_CASE("format_report names every note") {
    FireflowPatch fp{};
    const TransferReport r = to_flow_base(fp);
    const std::string s = format_report(r);
    CHECK(s.find("ROOT") != std::string::npos);
    CHECK_FALSE(s.empty());
}
```

`kFfEngineA`, `kFfEngineB`, `kFfTempo`, `kFfCompB` and `kFireflowParamCount` are named constants the header must export so the test does not duplicate `generated_panel.hpp`'s enum. Step 3 defines them.

Register `tests/test_flow_patch_bridge.cpp` in `CMakeLists.txt` beside `tests/test_flow_overlay.cpp`.

- [ ] **Step 2: Run to verify they fail**

Expected: **compile error**, the header does not exist.

- [ ] **Step 3: Implement `host/vcv/src/flow_patch_bridge.hpp`**

The header must:

1. **Mirror `generated_panel.hpp`'s indices as named constants** — `kFfEngineA = 17`, `kFfEngineB = 37`, and so on for every parameter the map uses, plus `kFireflowParamCount = 68` (counted from the enum; part A occupies `[0, 20)`, part B `[20, 40)`, and 28 global params follow). Do **not** `#include "generated_panel.hpp"`: it is generated from the panel script and pulls Rack types. Instead add a `static_assert` in `Fireflow.cpp` (Task 8) that each constant equals the matching `ParamId`, so the mirror cannot drift silently. That assert is the whole reason the mirror is safe — and it, not this plan, is the authority on the number.
2. **Implement exactly the rows in `docs/flow-fireflow-param-map.md`.** No mapping that is not in that file.
3. **Set `has[p]` only for base-rule parameters.** Guard the write with `is_base_rule(p)` rather than trusting the table transcription.
4. **Check the engine pair first** and, if neither engine is a carrier, return a report with `overlay_rejected = true`, no `has[]` bits set, and a note. This mirrors Task 2's whole-overlay rejection: two places that disagree about half-application would be worse than one that refuses.
5. **Emit a note for**: every UNREACHABLE row; every clamped value; every value the runtime veto will rewrite (`P_COMP_A/B` outside 0.40–0.60, `P_REVMIX_A/B` below 0.08, `P_DRIVE` above 0.40, `P_REV_MOD` above 0.25, `P_RES_A/B` above 0.75); the lost GRIT mode; the lost deck balance; and the 25 story-owned parameters as one summary note.
6. **`format_report`** returns a human-readable multi-line string, one line per note, each naming the parameter via `kParams[p].name` (which already holds the `P_*` spelling) and the reason.

Guard against overflow: `if (note_count < kMaxNotes)` on every push, and add a final note if any were dropped rather than truncating in silence.

- [ ] **Step 4: Run to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

- [ ] **Step 5: Prove the RED on the report**

Temporarily make the `P_ROOT` note conditional on something false, so the value is still not carried but the note vanishes. Rebuild.

Expected: **"every unreachable parameter is reported, every time" FAILS.** This is the gate that matters: a converter that quietly drops ROOT looks correct. Revert and confirm green.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/src/flow_patch_bridge.hpp tests/test_flow_patch_bridge.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(vcv): the Fireflow to flow converter, and the report it owes

Rack-free header in the pattern touch_pads.hpp and glow_ui.hpp already
use, so the desktop suite tests it headlessly. It takes a plain float
array rather than a Module, which is what keeps it that way.

The report is the deliverable. ROOT, FORM, per-deck MODE, GRIT's mode
and the deck balance have no path out of Fireflow at all; six veto bands
and three caps rewrite values that transfer perfectly. A converter that
carried what it could and said nothing about the rest would look right
and lose a third of the tonality without a word.

A loud pair is rejected whole, matching generate()'s rule: no carrier
means no role structure, and half-applying that is worse than refusing.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 7: `Place` grows, and the whole thing survives a reload

**Files:**
- Modify: `host/vcv/src/touch_pads.hpp` (the `Place` struct at line 138, `export_pool_tsv` at line 230)
- Modify: `host/vcv/src/glow_ui.hpp` (`GlowSave`, `glow_capture`, `glow_restore`)
- Modify: `host/vcv/src/Glow.cpp` (`dataToJson:426-434`, `dataFromJson:516-536`)
- Modify: `tests/test_flow_patch_bridge.cpp`

**Interfaces:**
- Consumes: `BaseOverlay` (Task 1), `Flow::overlay()` and the three-argument `restore_undo` (Task 3).
- Produces: `Place` gains `spky::flow::BaseOverlay base; bool has_base;`. Task 8 reads both.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_flow_patch_bridge.cpp`:

```cpp
#include "src/touch_pads.hpp"

TEST_CASE("Place stays trivially copyable after growing") {
    // Glow.cpp memcpys the whole Place array to the audio thread as one staged
    // handover (UiOp::SET_PLACES). A heap-owning member would put a malloc
    // there, for a patch somebody pasted.
    CHECK(std::is_trivially_copyable<spkyvcv::Place>::value);
}

TEST_CASE("the pool row carries the base and stays one line per place") {
    spkyvcv::Place places[2] = {};
    std::snprintf(places[0].code, sizeof places[0].code, "F1-00000020-000000000000");
    spkyvcv::set_label(places[0].name, spkyvcv::kNameCap, "opener");
    places[0].has_base = true;
    places[0].base.v[spky::flow::P_TUNE_A] = 0.25f;
    places[0].base.has[spky::flow::P_TUNE_A] = true;

    const std::string tsv = spkyvcv::export_pool_tsv(places, 2);
    // Header plus one row per place, and not one newline more: a base written
    // as its own line would make the file unreadable by the §10.3 generator.
    CHECK(std::count(tsv.begin(), tsv.end(), '\n') == 3);
    CHECK(tsv.find("opener") != std::string::npos);
}

TEST_CASE("a place with no base is distinguishable from one with a zero base") {
    spkyvcv::Place p{};
    CHECK_FALSE(p.has_base);
    // A default Place must not claim to carry a patch: wakePad passes
    // nullptr for it, and Flow::wake(s, nullptr) plays the drawn terrain.
}
```

Add `#include <algorithm>` and `#include <type_traits>` to the test file.

- [ ] **Step 2: Run to verify they fail**

Expected: compile error — `Place` has no `has_base`.

- [ ] **Step 3: Grow `Place`**

In `host/vcv/src/touch_pads.hpp`, add to the struct and extend the existing `static_assert`'s comment:

```cpp
struct Place {
    char code[spky::flow::kTerrainCodeLen + 1] = {};
    char name[kNameCap + 1] = {};
    char note[kNoteCap + 1] = {};
    // The hand-authored base, if this place came from a Fireflow patch (spec
    // 2026-08-11 §6). has_base false is NOT "an all-zero patch": wakePad
    // passes nullptr for it, and Flow::wake(s, nullptr) plays the drawn
    // terrain rather than the previous pad's base.
    spky::flow::BaseOverlay base;
    bool has_base = false;
};
```

`touch_pads.hpp` already includes `flow/terrain_code.h`, which includes `flow/terrain.h`, so `BaseOverlay` is in scope.

- [ ] **Step 4: Extend `export_pool_tsv`**

Add one column, `base`, after `note` in both the header line and every row, filled with `encode_base(p.base)` when `has_base` and empty otherwise. Keep the line ending `\n` and keep exactly one line per place — the `fp` column stays empty, as it has no producer and adding a second one here in a second language is what its gate exists to catch.

`encode_base` lives in `flow_patch_bridge.hpp` (Task 6), not here. `touch_pads.hpp` must therefore include it — check first that this does not make `touch_pads.hpp` pull a Rack type; both headers are Rack-free by design, so it should not, and if it does the include is the bug.

- [ ] **Step 5: Extend the JSON round trip, using the same encoder**

In `Glow.cpp`'s `dataToJson` places loop (line 426), write `json_object_set_new(o, "base", json_string(encode_base(p.base).c_str()))` — only when `has_base`. In `dataFromJson` (line 516), read the string back through `decode_base` and set `has_base` from its return value.

**One string encoding across all three consumers** — `pool.tsv`, the JSON, and Task 8's clipboard — rather than a nested JSON object for one of them. That is what lets a single round-trip test in `tests/test_flow_patch_bridge.cpp` cover the persistence path, which is otherwise untestable headlessly: `Glow.cpp` is not compiled by `spky_tests` and jansson is not available there.

Add that round-trip test now, in `tests/test_flow_patch_bridge.cpp`:

```cpp
TEST_CASE("an overlay survives the text round trip for every base-rule param") {
    spky::flow::BaseOverlay in;
    for (int i = 0; i < kBaseRuleCount; ++i) {
        const int p = kBaseRules[i].param;
        in.v[p]   = kParams[p].lo + 0.25f * (kParams[p].hi - kParams[p].lo);
        in.has[p] = true;
    }
    spky::flow::BaseOverlay out;
    REQUIRE(decode_base(encode_base(in).c_str(), out));
    for (int p = 0; p < P_COUNT; ++p) {
        CAPTURE(p);
        CHECK(out.has[p] == in.has[p]);
        if (in.has[p]) CHECK(out.v[p] == doctest::Approx(in.v[p]));
    }
}

TEST_CASE("a malformed base string is rejected, not half-read") {
    spky::flow::BaseOverlay in;
    in.v[P_TUNE_A] = 0.5f; in.has[P_TUNE_A] = true;
    in.v[P_RES_B]  = 0.3f; in.has[P_RES_B]  = true;
    std::string s = encode_base(in);
    s.erase(s.find(';'), 1);                 // splice two pairs into one token
    spky::flow::BaseOverlay out;
    CHECK_FALSE(decode_base(s.c_str(), out));
    for (int p = 0; p < P_COUNT; ++p) CHECK_FALSE(out.has[p]);
}

TEST_CASE("an empty base string decodes to no overlay, not a zero one") {
    spky::flow::BaseOverlay out;
    out.has[P_TUNE_A] = true;                // pre-dirty it
    CHECK(decode_base("", out));             // empty is VALID: a place with no patch
    for (int p = 0; p < P_COUNT; ++p) CHECK_FALSE(out.has[p]);
}
```

RED recipe for the first case: make `encode_base` print `%.2f` instead of a round-trippable format. RED for the second: make `decode_base` return true after a partial parse.

`GlowSave` in `glow_ui.hpp` gains the live overlay and the undo overlay, captured from `Flow::overlay()`, and `glow_restore` passes them to `wake()` and `restore_undo()`. Without this the live place's base is lost on reload while the pads keep theirs — the same class of bug as Task 3's undo slot, one level up.

- [ ] **Step 6: Run to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

- [ ] **Step 7: Prove the RED**

Temporarily change `Place::base` to a `std::vector<float>`. Rebuild.

Expected: **"Place stays trivially copyable after growing" FAILS**, and `touch_pads.hpp`'s own `static_assert` fires. Revert.

- [ ] **Step 8: Build the plugin, without installing**

```bash
cd host/vcv && ./build-local.sh
```

Do **not** pass `install`, and do not launch Rack. Expected: clean build.

- [ ] **Step 9: Commit**

```bash
git add host/vcv/src/touch_pads.hpp host/vcv/src/glow_ui.hpp host/vcv/src/Glow.cpp tests/test_flow_patch_bridge.cpp
git commit -m "$(cat <<'EOF'
feat(glow): a place can carry a hand-authored base

Place grows a BaseOverlay and stays trivially copyable, so the staged
handover to the audio thread is still a memcpy with no allocation. The
twelve-place payload roughly triples, which is bounded and off the heap.

has_base false is not an all-zero patch: wakePad passes nullptr and the
terrain plays as drawn. A patch written before this change has no "base"
key and comes back exactly that way.

GlowSave carries the live and undo overlays too. Without it the pads
would keep their bases across a reload while the place actually playing
lost its own -- the same shape as the undo slot one level down.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 8: The two menu items

**Files:**
- Modify: `host/vcv/src/Fireflow.cpp` (`appendFireflowMenu` at line 1647; add the mirror `static_assert`s near the existing `PART_STRIDE` guards at line 209)
- Modify: `host/vcv/src/Glow.cpp` (`appendContextMenu` at line 1127; `wakePad` at line 382; `pinCurrent` at line 336)

**Interfaces:**
- Consumes: `to_flow_base`, `format_report`, `FireflowPatch`, the `kFf*` constants (Task 6); `Place::base`/`has_base` (Task 7); `Flow::wake(s, ov)` and `Flow::overlay()` (Task 3); `roles_of`/`tonality_of`/`mode_of` (Task 4).
- Produces: no new symbols.

- [ ] **Step 1: Pin the index mirror**

In `Fireflow.cpp`, beside the existing `PART_STRIDE` guards around line 209, add one `static_assert` per `kFf*` constant:

```cpp
static_assert(spkyvcv::kFfEngineA == ENGINE_A, "flow_patch_bridge.hpp mirrors generated_panel.hpp's indices; they drifted");
static_assert(spkyvcv::kFfEngineB == ENGINE_B, "flow_patch_bridge.hpp mirrors generated_panel.hpp's indices; they drifted");
static_assert(spkyvcv::kFireflowParamCount == NUM_PARAMS, "flow_patch_bridge.hpp's param count drifted from the panel");
```

…and so on for every constant the header exports. This is the assert that makes the Rack-free mirror safe; without it the header is a silent duplicate of a generated file.

- [ ] **Step 2: Fireflow — "Copy patch as flow base"**

In `appendFireflowMenu`, after the "Resync loops to bar" item:

```cpp
menu->addChild(createMenuItem("Copy patch as flow base", "", [m]() {
    spkyvcv::FireflowPatch fp{};
    for (int i = 0; i < spkyvcv::kFireflowParamCount; ++i)
        fp.p[i] = m->params[i].getValue();
    fp.test_tone[0] = m->smp[0].testTone;
    fp.test_tone[1] = m->smp[1].testTone;
    const spkyvcv::TransferReport r = spkyvcv::to_flow_base(fp);
    glfwSetClipboardString(APP->window->win, spkyvcv::encode_base(r.overlay).c_str());
}));
```

…where the clipboard string is `spkyvcv::encode_base(r.overlay)` — the same encoder `pool.tsv` and Glow's JSON already use (Task 6, Task 7 Steps 4-5). No third format.

Show the report in the same gesture rather than hiding it: append `createMenuLabel` lines from `format_report(r)`, split on newlines, directly under the item, so the losses are visible without a second click.

- [ ] **Step 3: Glow — "Paste patch onto pad"**

In `appendContextMenu`, add a submenu listing the twelve pads. Each entry decodes the clipboard through the shared decoder and stages it:

```cpp
menu->addChild(createSubmenuItem("Paste patch onto pad", "", [m](Menu* sub) {
    for (int i = 0; i < spkyvcv::kPadCount; ++i)
        sub->addChild(createMenuItem(string::f("Pad %d", i + 1), "", [m, i]() {
            spky::flow::BaseOverlay ov;
            const char* clip = glfwGetClipboardString(APP->window->win);
            if (!clip || !spkyvcv::decode_base(clip, ov)) return;
            m->stagePlaces([&](spkyvcv::Place* p) {
                p[i].base = ov;
                p[i].has_base = true;
            });
        }));
}));
```

`stagePlaces` (`Glow.cpp:358`) is the existing UI-thread handover; use it rather than writing `places[]` directly, which belongs to the audio thread.

- [ ] **Step 4: Wire the overlay into `wakePad` and `pinCurrent`**

`wakePad` (line 382) passes the place's overlay:

```cpp
    flow.wake(st, places[pad].has_base ? &places[pad].base : nullptr);
```

`pinCurrent` (line 336) captures the live overlay alongside the code, so pinning a hand-built place keeps it:

```cpp
    const spky::flow::BaseOverlay* ov = flow.overlay();
    places[pad].has_base = ov != nullptr;
    if (ov) places[pad].base = *ov;
```

- [ ] **Step 5: Build the plugin, without installing**

```bash
cd host/vcv && ./build-local.sh
```

No `install`, and do not launch Rack. Expected: clean build.

- [ ] **Step 6: Run the whole suite**

```bash
source env.sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: everything green, including `flow_panel_guard` and the render hashes.

- [ ] **Step 7: Commit**

```bash
git add host/vcv/src/Fireflow.cpp host/vcv/src/Glow.cpp host/vcv/src/flow_patch_bridge.hpp tests/test_flow_patch_bridge.cpp
git commit -m "$(cat <<'EOF'
feat(vcv): carry a Fireflow patch onto a Glow pad

Fireflow gets "Copy patch as flow base", which puts the overlay on the
clipboard and prints what it could not carry right there in the menu --
seeing the losses should not cost a second click.

Glow gets "Paste patch onto pad N", staged through stagePlaces like
every other UI-thread edit, and wakePad hands the place's overlay to
Flow::wake. pinCurrent captures the live overlay too, so pinning a
hand-built place keeps it rather than degrading it to a bare seed.

The clipboard and pool.tsv share one encoder. flow_patch_bridge.hpp
mirrors generated_panel.hpp's indices to stay Rack-free, and Fireflow.cpp
now static_asserts every one of them -- an unchecked mirror of a
generated file is a silent duplicate waiting to drift.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

## After the last task

Two things the spec records as open, neither of which any task above closes:

1. **The `kCarrierEngine` widening** (spec §10) is a separate spec and a separate decision. Until it is taken, a loud pair is rejected by Task 2 and reported by Task 6, which is the honest behaviour either way.
2. **The measurement against real patches** (spec §10). Every target in spec §2.1 was synthetic. Ask the owner for two or three saved Fireflow patches, push them through `to_flow_base`, and read the report — that says how much of a real patch survives before twelve places get built against it. It validates the design; it does not block it.

Do not bump `plugin.json` or tag a release as part of this work. `main` is already ahead of `origin/main` with an unreleased version bump pending, and that is the owner's call.
