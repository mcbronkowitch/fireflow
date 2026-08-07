# Glow GENRE and SCALE Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give Glow two explicit controls flanking NEW — GENRE, which constrains
which archetype NEW may draw, and SCALE, which overrides the terrain's tonality —
so that one genre can be auditioned and tuned by ear.

**Architecture:** GENRE is a *filter on the draw*: the archetype is a pure
function of the master seed, so `draw_new` rejection-samples masters and nothing
about `TerrainState`, the terrain code or persistence changes. SCALE is a *live
override*: `Flow` replaces the pushed value for `P_SCALE`/`P_ROOT` after the
quantizer hysteresis has run, so the terrain's own value keeps tracking
underneath and returns intact when the override is released.

**Tech Stack:** C++17 engine (`engine/flow/`), doctest suite (`tests/`,
binary `build/spky_tests`), VCV Rack 2 host (`host/vcv/`), Python panel
generator (`host/vcv/res/gen_flow_panel.py`).

**Spec:** `docs/superpowers/specs/2026-08-07-glow-genre-and-scale-design.md` —
read it first. This plan implements it; where the two disagree, the spec wins.

## Global Constraints

- **Branch:** `glow-genre-scale`, already created, spec already committed on it.
- **Engine build is clang + Ninja, never MSVC.** Always
  `source env.sh` first, then
  `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`.
  `-DCMAKE_BUILD_TYPE=Release` is **not optional** — a Debug configure makes
  `spky_tests` and `ctrl_identity` fail with "SYNTH reference moved".
- **VCV module builds only via `host/vcv/build-local.sh`.** The system `g++` on
  this machine is the ARM cross-compiler; invoking it directly fails with
  "MinGW not found".
- **Run one doctest case:** `./build/spky_tests -tc="<case name>"`.
  Whole suite: `ctest --test-dir build --output-on-failure`.
- **Run the panel tests:** from `host/vcv/`, `python res/test_flow_panel.py`.
  Regenerate the panel: from `host/vcv/`, `python res/gen_flow_panel.py`.
- **Every test must be proven RED once** before its implementation lands. A test
  that cannot fail gets fixed, even if this plan specified it.
- **Commit trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- No bit-exactness gates on rendered audio. RNG-chain pins are fine and are used
  deliberately in Task 2.
- `engine/` never sees a hardware or Rack type. `glow_ui.hpp` never includes
  `rack.hpp`.

---

### Task 1: `arch_of` — the archetype without the terrain

**Files:**
- Modify: `engine/flow/flow_ids.h`
- Modify: `engine/flow/terrain.h`
- Modify: `engine/flow/terrain.cpp:245-250`
- Test: `tests/test_flow_terrain.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `constexpr int spky::flow::ARCH_ANY = -1;` and
  `spky::flow::Archetype spky::flow::arch_of(uint32_t master);`

Why this is its own task: everything downstream filters candidates with
`arch_of` instead of a full `generate()`. If the cheap path ever disagreed with
the real one, the genre filter would select phantoms and every later test would
be measuring the wrong thing. This proves they agree before anything uses it.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_flow_terrain.cpp`:

```cpp
TEST_CASE("flow terrain: arch_of is the archetype generate() draws") {
    // The cheap stage-0-only path exists so draw_new can filter candidates
    // without paying for a full generate() on the audio thread. It is only
    // sound if it never disagrees with the real thing.
    for (uint32_t m = 1; m <= 5000; ++m) {
        spky::flow::TerrainState st;
        st.master = m;
        CHECK(spky::flow::arch_of(m) == spky::flow::generate(st).arch);
    }
    // A partial reroll must not move it: reroll[] never reaches kStreamArch.
    spky::flow::TerrainState st;
    st.master = 0xBEEF;
    for (int i = 0; i < spky::flow::MACRO_COUNT; ++i) st.reroll[i] = 7;
    CHECK(spky::flow::generate(st).arch == spky::flow::arch_of(0xBEEF));
}
```

- [ ] **Step 2: Run it and watch it fail**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Expected: **compile error**, `'arch_of' is not a member of 'spky::flow'`. That
is the RED for this task — record it.

- [ ] **Step 3: Add `ARCH_ANY` to `flow_ids.h`**

Replace the enum block in `engine/flow/flow_ids.h`:

```cpp
enum Archetype { ARCH_DRONE = 0, ARCH_PULSE, ARCH_ARP, ARCH_FRAGMENT,
                 ARCH_COUNT };

// "No constraint" for draw_new's genre filter. Deliberately an int and not an
// Archetype enumerator: it is the ABSENCE of an archetype, and widening the
// enum with it would put a non-archetype into every array sized ARCH_COUNT
// and every switch over Archetype.
constexpr int ARCH_ANY = -1;
```

- [ ] **Step 4: Declare `arch_of` in `terrain.h`**

Add above `Terrain generate(const TerrainState& st);`:

```cpp
// The archetype alone, without building a terrain. Stage 0 is a pure function
// of the master -- make_stream(master, kStreamArch, 0), counter pinned at 0,
// no dependence on adventure, roles or anything drawn later -- and draw_new's
// genre filter needs to reject candidates before paying for a full generate()
// on the audio thread. generate() itself now calls this, so the two cannot
// drift apart; test_flow_terrain.cpp pins that they agree anyway.
Archetype arch_of(uint32_t master);
```

- [ ] **Step 5: Define it, and route stage 0 through it**

In `engine/flow/terrain.cpp`, add above `generate()`:

```cpp
Archetype arch_of(uint32_t master) {
    Rng r = make_stream(master, kStreamArch, 0);
    return Archetype(pick_weighted(r, kArchWeight, ARCH_COUNT));
}
```

Then replace the body of stage 0 (`terrain.cpp:245-250`) so it delegates —
leaving the existing comment above it in place:

```cpp
    // Stage 0: archetype -- the correlation structure that keeps terrains
    // from being noise. (Body moved to arch_of() so draw_new's genre filter
    // and generate() cannot draw it differently.)
    t.arch = arch_of(st.master);
```

- [ ] **Step 6: Run the test**

```bash
cmake --build build && ./build/spky_tests -tc="flow terrain: arch_of is the archetype generate() draws"
```
Expected: PASS.

- [ ] **Step 7: Run the whole suite** — stage 0 is upstream of everything.

```bash
ctest --test-dir build --output-on-failure
```
Expected: all pass. Nothing should move: `arch_of` consumes exactly the same
single `next_unipolar()` from the same stream that the inlined code did.

- [ ] **Step 8: Commit**

```bash
git add engine/flow/flow_ids.h engine/flow/terrain.h engine/flow/terrain.cpp tests/test_flow_terrain.cpp
git commit -m "feat(flow): arch_of -- the archetype without building the terrain

Stage 0 is a pure function of the master; three comment blocks in
terrain.cpp already treat that as load-bearing. Make it callable so the
genre filter can reject candidates without a full generate() on the audio
thread, and route generate() through it so they cannot drift.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: `draw_new` gains a genre branch

**Files:**
- Modify: `engine/flow/taste.h` (near `kDistanceMin`, line ~65)
- Modify: `engine/flow/terrain.h:118-125` (declaration **and** its doc comment)
- Modify: `engine/flow/terrain.cpp:592-621` (definition **and** its doc comment)
- Test: `tests/test_flow_terrain.cpp`

**Interfaces:**
- Consumes: `arch_of`, `ARCH_ANY` (Task 1).
- Produces: `TerrainState draw_new(const TerrainState& cur, Rng& seq, int want = ARCH_ANY);`
  and `constexpr int kGenreCandidates = 8; constexpr int kGenreDrawCap = 256;`

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_flow_terrain.cpp`. Note what the last one does **not**
do: it must not compare `draw_new(cur, seq)` against
`draw_new(cur, seq, ARCH_ANY)` — those are the same call through the default
argument, a tautology that can never fail. It pins the literal master the
current code produces instead.

```cpp
TEST_CASE("flow terrain: a genre-locked draw_new never leaves the genre") {
    // Today this fails by construction: distance()'s flat +0.25 archetype
    // bonus alone clears kDistanceMin, so NEW leaves the archetype every
    // time (listening notes item 8: 0 same-archetype results in 3 000 calls).
    for (int a = 0; a < spky::flow::ARCH_COUNT; ++a) {
        spky::Rng seq;
        seq.seed(9876u + uint32_t(a));
        spky::flow::TerrainState cur;
        cur.master = 0x101;
        for (int i = 0; i < 200; ++i) {
            cur = spky::flow::draw_new(cur, seq, a);
            REQUIRE(spky::flow::arch_of(cur.master) == spky::flow::Archetype(a));
            CHECK(cur.reroll[0] == 0);          // NEW replaces the whole terrain
        }
    }
}

TEST_CASE("flow terrain: a genre-locked draw_new returns the farthest candidate") {
    // Best-of-N is the whole rule in this branch -- there is no threshold --
    // so "the result is the maximum" is the only thing that makes it a rule.
    // Replay the same seed by hand and confirm nothing nearer was passed over.
    spky::Rng seq;
    seq.seed(4242u);
    spky::flow::TerrainState cur;
    cur.master = 0x707;
    const spky::flow::Terrain cur_t = spky::flow::generate(cur);
    const spky::flow::TerrainState got =
        spky::flow::draw_new(cur, seq, spky::flow::ARCH_DRONE);

    spky::Rng replay;
    replay.seed(4242u);
    float best = -1.f;
    int matched = 0;
    uint32_t best_master = 0;
    for (int i = 0; i < spky::flow::kGenreDrawCap &&
                    matched < spky::flow::kGenreCandidates; ++i) {
        const uint32_t m = replay.next_u32();
        if (m == cur.master) continue;
        if (spky::flow::arch_of(m) != spky::flow::ARCH_DRONE) continue;
        ++matched;
        spky::flow::TerrainState cand;
        cand.master = m;
        const float d = spky::flow::distance(cur_t, spky::flow::generate(cand));
        if (d > best) { best = d; best_master = m; }
    }
    CHECK(matched == spky::flow::kGenreCandidates);
    CHECK(got.master == best_master);
}

TEST_CASE("flow terrain: a genre-locked draw_new never returns where it started") {
    // The ANY branch guarantees this (terrain.cpp's `continue` on a repeat)
    // and test_flow_terrain_code.cpp asserts it there; the new branch needs
    // its own assertion, since it has its own skip.
    spky::Rng seq;
    seq.seed(31337u);
    spky::flow::TerrainState cur;
    cur.master = 0x20;
    for (int i = 0; i < 300; ++i) {
        const spky::flow::TerrainState next =
            spky::flow::draw_new(cur, seq, spky::flow::arch_of(cur.master));
        CHECK(next.master != cur.master);
        cur = next;
    }
}

TEST_CASE("flow terrain: the unconstrained draw_new chain is unchanged") {
    // NOT draw_new(cur, seq) vs draw_new(cur, seq, ARCH_ANY): those are the
    // same call through the default argument and could never disagree. Pin
    // the actual chain instead, so a change to the ANY branch shows up here.
    // The four literals below are captured from the CURRENT implementation
    // in step 2 -- do not invent them.
    spky::Rng seq;
    seq.seed(12345u);
    spky::flow::TerrainState cur;
    cur.master = 1;
    const uint32_t want[4] = { 0, 0, 0, 0 };   // <-- fill in from step 2
    for (int i = 0; i < 4; ++i) {
        cur = spky::flow::draw_new(cur, seq);
        CHECK(cur.master == want[i]);
    }
}
```

- [ ] **Step 2: Capture the four ANY-chain literals from the CURRENT code**

Before touching `draw_new`, build and run a throwaway case to read the values
out, then paste them into `want[4]` above. Add this temporarily, run it, note
the four numbers from the failure output, delete it:

```cpp
TEST_CASE("scratch: print the ANY chain") {
    spky::Rng seq;
    seq.seed(12345u);
    spky::flow::TerrainState cur;
    cur.master = 1;
    for (int i = 0; i < 4; ++i) {
        cur = spky::flow::draw_new(cur, seq);
        CHECK(cur.master == 0u);        // forced failure prints the real value
    }
}
```

```bash
cmake --build build && ./build/spky_tests -tc="scratch: print the ANY chain"
```

- [ ] **Step 3: Run the four real tests and watch them fail**

```bash
cmake --build build && ./build/spky_tests -tc="flow terrain: a genre*"
```
Expected: compile error (`draw_new` takes 2 arguments), which is the RED for
the three genre cases. After Step 5 compiles them, the genre-lock case must be
seen failing *at least once* if the branch is stubbed wrong — do not skip
straight to green.

- [ ] **Step 4: Add the two constants to `taste.h`**

Next to `kDistanceMin` (~line 65):

```cpp
// The genre-locked NEW draw (spec 2026-08-07 §2.2). kDistanceMin does NOT
// apply in that branch: under a lock every candidate shares cur's archetype,
// so distance()'s flat +0.25 is a constant on all of them and cancels in the
// argmax -- and terrain.cpp's own measurement (2026-08-06, at 89eb461) found
// NO same-archetype pair of 6 603 clearing kDistanceMin on its base patch, so
// a threshold there would reject every candidate and let the fallback decide
// every press. Best-of-N is the rule instead, and kGenreCandidates is the one
// number that tunes it: larger means NEW works harder for contrast.
constexpr int kGenreCandidates = 8;
// Termination guard, not a tuning knob. At the rarest archetype weight (0.15)
// 256 draws yield a mean of 38.4 matches, sd ~5.7 -- fewer than 8 is past 5
// sigma and zero matches is ~1e-18, so this cap is unreachable in practice.
constexpr int kGenreDrawCap = 256;
```

- [ ] **Step 5: Change the declaration and rewrite its doc comment**

Replace `terrain.h:118-125` (the whole comment block plus the declaration):

```cpp
// Draw a new terrain state that reads as a different place from cur (spec
// 7.4's NEW gesture, extended by spec 2026-08-07 §2.2). Every candidate is a
// fresh master with all reroll counters zero. seq is the caller-held sequence
// Rng -- passing the same seeded Rng twice reproduces the same draw chain --
// and cur.master is never returned.
//
// TWO BRANCHES, and they use different rules on purpose:
//
//   want == ARCH_ANY   the original: retry until a candidate clears
//                      kDistanceMin, or give up after 16 tries and take the
//                      farthest seen. Unchanged, down to the RNG draw count.
//   want == archetype  candidates whose arch_of() does not match are skipped
//                      without being generated; once kGenreCandidates of them
//                      match, the farthest by distance() wins. NO threshold --
//                      see taste.h at kGenreCandidates for why one would be
//                      decorative here. Bounded by kGenreDrawCap draws.
TerrainState draw_new(const TerrainState& cur, Rng& seq, int want = ARCH_ANY);
```

- [ ] **Step 6: Implement the branch**

In `terrain.cpp`, keep the existing comment block above `draw_new` but add to
its end:

```cpp
// The genre branch (spec 2026-08-07 §2.2) is separate below rather than folded
// into the loop above, because it answers a different question: not "is this
// far enough away" but "which of these eight is farthest". Only genre-matching
// candidates ever enter `best` -- taking the farthest of ALL candidates would
// break the lock precisely because of the +0.25 archetype term, which across
// archetypes dominates the base patch outright.
```

and change the signature and body (note: **no default argument in the `.cpp`**):

```cpp
TerrainState draw_new(const TerrainState& cur, Rng& seq, int want) {
    const Terrain cur_terrain = generate(cur);
    if (want == ARCH_ANY) {
        TerrainState best;
        float best_dist = -1.f;
        for (int try_i = 0; try_i < 16; ++try_i) {
            const uint32_t master = seq.next_u32();
            if (master == cur.master) continue;    // redraw, still a spent try
            TerrainState cand;
            cand.master = master;                  // reroll[] already zero
            const float d = distance(cur_terrain, generate(cand));
            if (d >= kDistanceMin) return cand;
            if (d > best_dist) { best_dist = d; best = cand; }
        }
        return best;
    }

    // Genre-locked: collect kGenreCandidates matching masters, keep the
    // farthest. arch_of() rejects a non-matching master for the price of one
    // stream seed, so the expensive generate() runs only on the eight that
    // survive -- fewer than the ANY branch's sixteen.
    //
    // If the cap is somehow exhausted with no match at all, `best` is still a
    // default-constructed TerrainState (master 1, every counter zero), whose
    // archetype need not match `want`. That is a real but unreachable edge --
    // ~1e-18 at the rarest weight -- left uncorrected rather than given
    // logic that could never be tested, exactly as the ANY branch leaves its
    // own all-16-draws-hit-cur.master edge.
    TerrainState best;
    float best_dist = -1.f;
    int matched = 0;
    for (int i = 0; i < kGenreDrawCap && matched < kGenreCandidates; ++i) {
        const uint32_t master = seq.next_u32();
        if (master == cur.master) continue;
        if (arch_of(master) != want) continue;
        ++matched;
        TerrainState cand;
        cand.master = master;
        const float d = distance(cur_terrain, generate(cand));
        if (d > best_dist) { best_dist = d; best = cand; }
    }
    return best;
}
```

- [ ] **Step 7: Run the four tests, then the suite**

```bash
cmake --build build && ./build/spky_tests -tc="flow terrain: a genre*"
./build/spky_tests -tc="flow terrain: the unconstrained draw_new chain is unchanged"
ctest --test-dir build --output-on-failure
```
Expected: all PASS. If the ANY-chain case fails, the ANY branch was altered —
fix the branch, do not update the literals.

- [ ] **Step 8: Commit**

```bash
git add engine/flow/taste.h engine/flow/terrain.h engine/flow/terrain.cpp tests/test_flow_terrain.cpp
git commit -m "feat(flow): draw_new can be locked to one archetype

Best-of-8 rather than a threshold: under a lock every candidate shares
cur's archetype, so distance()'s flat +0.25 is a constant that cancels in
the argmax, and terrain.cpp's own measurement says no same-archetype pair
of 6 603 clears kDistanceMin anyway -- a gate there would reject every
candidate and let the fallback decide every press.

The ANY branch is untouched, and pinned by an RNG-chain test so it stays
that way.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: `Flow::set_genre` — and proof it is silent

**Files:**
- Modify: `engine/flow/flow.h` (public verbs + `_genre` member)
- Modify: `engine/flow/flow.cpp:185-189` (`new_full`)
- Test: `tests/test_flow_runtime.cpp`

**Interfaces:**
- Consumes: `draw_new(cur, seq, want)` (Task 2), `ARCH_ANY` (Task 1).
- Produces: `void Flow::set_genre(int arch);` and `int Flow::genre() const;`

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_flow_runtime.cpp`:

```cpp
TEST_CASE("flow runtime: the genre setting changes no sound by itself") {
    // The design's central safety claim: GENRE constrains the next NEW draw
    // and nothing else. Two identical instruments, one locked to DRONE, no
    // press: every pushed parameter must agree, tick for tick.
    spky::Instrument ia, ib;
    ia.init(48000.f);
    ib.init(48000.f);
    spky::flow::Flow fa, fb;
    fa.init(&ia, 100.f);
    fb.init(&ib, 100.f);
    spky::flow::TerrainState st;
    st.master = 0xC0FFEE;
    fa.wake(st);
    fb.wake(st);
    fb.set_genre(spky::flow::ARCH_DRONE);
    for (int t = 0; t < 400; ++t) {
        const float x = float(t) / 400.f;
        for (int m = 0; m < spky::flow::MACRO_COUNT; ++m) {
            fa.set_macro(m, x);
            fb.set_macro(m, x);
        }
        fa.tick();
        fb.tick();
        for (int p = 0; p < spky::flow::P_COUNT; ++p)
            REQUIRE(fa.param_now(p) == fb.param_now(p));
    }
}

TEST_CASE("flow runtime: a genre-locked NEW press lands in that genre") {
    spky::Instrument inst;
    inst.init(48000.f);
    spky::flow::Flow fl;
    fl.init(&inst, 100.f);
    spky::flow::TerrainState st;
    st.master = 0x101;
    fl.wake(st);
    fl.set_genre(spky::flow::ARCH_FRAGMENT);
    CHECK(fl.genre() == spky::flow::ARCH_FRAGMENT);
    for (int i = 0; i < 40; ++i) {
        REQUIRE(fl.new_full());
        CHECK(spky::flow::arch_of(fl.state().master) ==
              spky::flow::ARCH_FRAGMENT);
        for (int t = 0; t < 700; ++t) fl.tick();   // let the blend settle
    }
}
```

- [ ] **Step 2: Run and watch it fail**

```bash
cmake --build build && ./build/spky_tests -tc="flow runtime: a genre*"
```
Expected: compile error, `'class spky::flow::Flow' has no member named 'set_genre'`.

- [ ] **Step 3: Add the verbs to `flow.h`**

In the public section, after `void set_lock(bool on);`:

```cpp
    // The genre lock (spec 2026-08-07 §2). Constrains which archetype
    // new_full() may draw and NOTHING else -- no parameter moves when this
    // changes, which test_flow_runtime.cpp pins. ARCH_ANY (flow_ids.h) is the
    // unconstrained default.
    //
    // Note what this is: state that lives in neither TerrainState nor the
    // terrain code, and that wake()/init() do NOT reset. Flow therefore stops
    // being a pure function of (TerrainState, macros). The host owns it and
    // re-pushes it every control tick.
    void set_genre(int arch) { _genre = arch; }
    int  genre() const { return _genre; }
```

And in the private members, next to `Rng _seq;`:

```cpp
    int   _genre = ARCH_ANY;      // draw constraint for new_full(), not state
```

- [ ] **Step 4: Forward it in `new_full`**

`engine/flow/flow.cpp`:

```cpp
bool Flow::new_full() {
    if (!_woken || _locked) return false;
    begin_blend(draw_new(_state, _seq, _genre));
    return true;
}
```

`new_partial` is deliberately untouched: a partial reroll keeps the master, and
`reroll[]` never reaches `kStreamArch`, so the archetype provably cannot move.

- [ ] **Step 5: Run the tests, then the suite**

```bash
cmake --build build && ./build/spky_tests -tc="flow runtime: a genre*"
./build/spky_tests -tc="flow runtime: the genre setting changes no sound by itself"
ctest --test-dir build --output-on-failure
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add engine/flow/flow.h engine/flow/flow.cpp tests/test_flow_runtime.cpp
git commit -m "feat(flow): Flow::set_genre constrains what NEW draws

Forwarded to draw_new by new_full only. A test pins that setting it moves
no parameter at all, which is the whole safety claim: the knob is a draw
rule, not a sound control.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: the SCALE / ROOT override

**Files:**
- Modify: `engine/flow/flow.h` (verbs + two members)
- Modify: `engine/flow/flow.cpp` — inside `recompute_and_push`, after the veto
  loop and **before** the change guard (`flow.cpp:554-566`)
- Test: `tests/test_flow_runtime.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `void Flow::set_scale_override(int scale);`,
  `void Flow::set_root_override(int root);`, `int Flow::scale_override() const;`,
  `int Flow::root_override() const;` — `-1` meaning AUTO in all four.

**This is the task with the trap.** Read spec §3.3 before writing code. The
override must run **after** `quantize_hyst`, not instead of it.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_flow_runtime.cpp`. The third case is the one that matters:
the naive "back to AUTO restores the scale" test passes against the buggy
implementation too, because it never presses NEW while the override is held.

```cpp
TEST_CASE("flow runtime: a scale override holds, and AUTO gives the terrain back") {
    spky::Instrument inst;
    inst.init(48000.f);
    spky::flow::Flow fl;
    fl.init(&inst, 100.f);
    spky::flow::TerrainState st;
    st.master = 0x333;
    fl.wake(st);
    for (int t = 0; t < 50; ++t) fl.tick();
    const float terrain_scale = fl.param_now(spky::flow::P_SCALE);

    fl.set_scale_override(spky::SCALE_MIN_PENT);
    for (int t = 0; t < 50; ++t) {
        for (int m = 0; m < spky::flow::MACRO_COUNT; ++m)
            fl.set_macro(m, float(t) / 50.f);       // macros must not shake it
        fl.tick();
        REQUIRE(fl.param_now(spky::flow::P_SCALE) == float(spky::SCALE_MIN_PENT));
    }
    fl.set_scale_override(-1);
    fl.tick();
    CHECK(fl.param_now(spky::flow::P_SCALE) == terrain_scale);
}

TEST_CASE("flow runtime: an override released after NEW lands on the new scale") {
    // THE test for spec 3.3. quantize_hyst compares strictly at
    // kHysteresisFrac 0.5, so an un-forced ONE-STEP move never passes its
    // guard. If the override skipped quantize_hyst, _step_now would freeze
    // while the override was held, and a terrain one step away would be
    // unreachable forever after release. Search the seeds for exactly that
    // pairing rather than hoping for it.
    spky::Instrument inst;
    inst.init(48000.f);
    spky::flow::Flow fl;
    fl.init(&inst, 100.f);

    bool tested = false;
    for (uint32_t m = 1; m < 400 && !tested; ++m) {
        spky::flow::TerrainState a;
        a.master = m;
        const int sa = int(spky::flow::generate(a).base[spky::flow::P_SCALE]);
        for (uint32_t n = 1; n < 400 && !tested; ++n) {
            spky::flow::TerrainState b;
            b.master = n;
            const int sb = int(spky::flow::generate(b).base[spky::flow::P_SCALE]);
            if (std::abs(sa - sb) != 1) continue;      // need a ONE-step move
            tested = true;

            fl.wake(a);
            for (int t = 0; t < 50; ++t) fl.tick();
            REQUIRE(fl.param_now(spky::flow::P_SCALE) == float(sa));

            fl.set_scale_override(spky::SCALE_WHOLE);
            for (int t = 0; t < 50; ++t) fl.tick();
            fl.wake(b);                                // stands in for a NEW press
            for (int t = 0; t < 800; ++t) fl.tick();
            REQUIRE(fl.param_now(spky::flow::P_SCALE) == float(spky::SCALE_WHOLE));

            fl.set_scale_override(-1);
            fl.tick();
            CHECK(fl.param_now(spky::flow::P_SCALE) == float(sb));
        }
    }
    REQUIRE(tested);      // the search must actually have found a pair
}

TEST_CASE("flow runtime: an override survives a blend and can be released inside one") {
    spky::Instrument inst;
    inst.init(48000.f);
    spky::flow::Flow fl;
    fl.init(&inst, 100.f);
    spky::flow::TerrainState st;
    st.master = 0x55;
    fl.wake(st);
    for (int t = 0; t < 50; ++t) fl.tick();
    fl.set_scale_override(spky::SCALE_KUMOI);
    REQUIRE(fl.new_full());
    for (int t = 0; t < 300; ++t) {                    // mid-blend
        fl.tick();
        REQUIRE(fl.param_now(spky::flow::P_SCALE) == float(spky::SCALE_KUMOI));
    }
    fl.set_scale_override(-1);                         // release INSIDE the blend
    for (int t = 0; t < 400; ++t) fl.tick();
    CHECK(fl.param_now(spky::flow::P_SCALE) ==
          float(int(spky::flow::terrain_of(fl).base[spky::flow::P_SCALE])));
}

TEST_CASE("flow runtime: a root override reaches both parts and stays in range") {
    spky::Instrument inst;
    inst.init(48000.f);
    spky::flow::Flow fl;
    fl.init(&inst, 100.f);
    spky::flow::TerrainState st;
    st.master = 0x9;
    fl.wake(st);
    fl.set_root_override(7);
    fl.tick();
    CHECK(fl.param_now(spky::flow::P_ROOT) == 7.f);
    CHECK(inst.part(spky::PART_A).root() == 7);
    CHECK(inst.part(spky::PART_B).root() == 7);
    // Out of range must be clamped, not published: param_now is a public
    // observer and flow.cpp says it must never show an out-of-range value.
    fl.set_root_override(99);
    fl.tick();
    CHECK(fl.param_now(spky::flow::P_ROOT) <= spky::flow::kParams[spky::flow::P_ROOT].hi);
}
```

> If `Part::root()` does not exist, read the part back through whatever
> accessor `tests/test_quantizer.cpp` or `tests/test_part.cpp` already uses for
> the root; do not add an accessor to the engine for a test's convenience.

- [ ] **Step 2: Run and watch them fail**

```bash
cmake --build build && ./build/spky_tests -tc="flow runtime: a scale override*"
```
Expected: compile error, no `set_scale_override`.

- [ ] **Step 3: Add the verbs to `flow.h`**

After `set_genre`/`genre`:

```cpp
    // Explicit tonality (spec 2026-08-07 §3). -1 means AUTO: the terrain's own
    // drawn value, i.e. exactly today's behaviour. Any other value replaces
    // what this Flow pushes for that parameter, immediately, without touching
    // the terrain -- so AUTO gives the terrain's value back intact.
    //
    // Like _genre, these are host-owned settings and not part of TerrainState;
    // wake()/init() do not reset them.
    void set_scale_override(int scale) { _scale_ovr = scale; }
    void set_root_override(int root)   { _root_ovr = root; }
    int  scale_override() const { return _scale_ovr; }
    int  root_override() const  { return _root_ovr; }
```

Private members, next to `_genre`:

```cpp
    int   _scale_ovr = -1;        // -1 = AUTO (use the terrain's P_SCALE)
    int   _root_ovr  = -1;        // -1 = AUTO (use the terrain's P_ROOT)
```

- [ ] **Step 4: Apply the override in `recompute_and_push`**

In `engine/flow/flow.cpp`, insert **after** the veto loop and **before** the
"Setter spam guard" block:

```cpp
        // Explicit tonality (spec 2026-08-07 §3.3), the last word on these two
        // params -- and deliberately AFTER quantize_hyst rather than instead
        // of it. kHysteresisFrac is 0.5 and quantize_hyst compares strictly,
        // so for P_SCALE and P_ROOT (step size exactly 1, every candidate an
        // integer) an un-forced ONE-step move never passes its guard: forcing
        // at the switch phase is the only thing that ever moves them. Skipping
        // quantize_hyst here would freeze _step_now, and a NEW press onto a
        // terrain one step away would then be unreachable on release -- the
        // instrument would sit on the stale scale until some later terrain
        // moved it two steps or more. Running it and overwriting its result
        // keeps _step_now and _disc_done tracking the terrain underneath.
        //
        // Clamped for the same reason the blend line is: param_now() is a
        // public observer and must never publish an out-of-range value, and
        // an override can arrive from a hand-edited patch.
        if (p == P_SCALE && _scale_ovr >= 0)
            v = clamp_to(kParams[p], float(_scale_ovr));
        else if (p == P_ROOT && _root_ovr >= 0)
            v = clamp_to(kParams[p], float(_root_ovr));
```

- [ ] **Step 5: Prove the RED is real**

The stale-step test must be seen catching the bug it exists for. Temporarily
move the block from step 4 to *before* the `if (discrete) v = quantize_hyst(...)`
line and guard the quantizer so the override skips it — i.e. build the buggy
version on purpose — then run:

```bash
cmake --build build && ./build/spky_tests -tc="flow runtime: an override released after NEW lands on the new scale"
```
Expected: **FAIL**. Then restore step 4's placement and confirm it passes.
Record both outcomes; this is the one test that distinguishes the two designs.

- [ ] **Step 6: Run the four tests, then the suite**

```bash
cmake --build build && ./build/spky_tests -tc="flow runtime: a scale override*"
./build/spky_tests -tc="flow runtime: an override*"
./build/spky_tests -tc="flow runtime: a root override*"
ctest --test-dir build --output-on-failure
```
Expected: all PASS.

- [ ] **Step 7: Commit**

```bash
git add engine/flow/flow.h engine/flow/flow.cpp tests/test_flow_runtime.cpp
git commit -m "feat(flow): explicit SCALE and ROOT overrides

Applied after quantize_hyst, not instead of it. kHysteresisFrac is 0.5 and
the comparison is strict, so an un-forced one-step move never passes the
guard -- skipping the quantizer would freeze _step_now, and releasing the
override after a NEW press onto an adjacent scale would strand the
instrument there. A test builds exactly that pairing by search rather than
by luck.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: `kScaleKnobOrder` — knob travel from calm to sharp

**Files:**
- Modify: `host/vcv/src/glow_ui.hpp`
- Test: `tests/test_glow_ui.cpp`

**Interfaces:**
- Consumes: `spky::SCALE_NAMES`, `spky::SCALE_LIST_COUNT` (`engine/pitch/quantizer.h`),
  `spky::flow::kScaleW` (`engine/flow/taste.h`).
- Produces: `spkyvcv::kScaleKnobOrder[13]` and
  `int spkyvcv::scale_of_knob(int pos)` — `pos` 0 = AUTO returning `-1`,
  1..13 returning a `ScaleId`.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_glow_ui.cpp`:

```cpp
TEST_CASE("glow: the scale knob travels from least to most friction") {
    // A permutation check alone would test the table, not the feature. The
    // monotonicity check is what catches a kScaleW retune that reorders the
    // groups and silently leaves the knob travel no longer running calm to
    // sharp.
    bool seen[spky::SCALE_LIST_COUNT] = {};
    for (int i = 0; i < spky::SCALE_LIST_COUNT; ++i) {
        const int s = spkyvcv::kScaleKnobOrder[i];
        REQUIRE(s >= 0);
        REQUIRE(s < spky::SCALE_LIST_COUNT);
        CHECK(!seen[s]);
        seen[s] = true;
    }
    for (int i = 1; i < spky::SCALE_LIST_COUNT; ++i)
        CHECK(spky::flow::kScaleW[spkyvcv::kScaleKnobOrder[i]] <=
              spky::flow::kScaleW[spkyvcv::kScaleKnobOrder[i - 1]]);
}

TEST_CASE("glow: knob position 0 is AUTO, the rest are scales") {
    CHECK(spkyvcv::scale_of_knob(0) == -1);
    for (int p = 1; p <= spky::SCALE_LIST_COUNT; ++p)
        CHECK(spkyvcv::scale_of_knob(p) == spkyvcv::kScaleKnobOrder[p - 1]);
    // Out of range reads as AUTO rather than as scale 0 -- a corrupt patch
    // must not silently retune the instrument to Aeolian.
    CHECK(spkyvcv::scale_of_knob(-3) == -1);
    CHECK(spkyvcv::scale_of_knob(99) == -1);
}
```

- [ ] **Step 2: Run and watch it fail**

```bash
cmake --build build && ./build/spky_tests -tc="glow: the scale knob*"
```
Expected: compile error, no `kScaleKnobOrder`.

- [ ] **Step 3: Implement**

Add to `host/vcv/src/glow_ui.hpp`, after `kCvMacro`, with
`#include "flow/taste.h"` and `#include "pitch/quantizer.h"` added to the
header's include block if not already reachable:

```cpp
// Knob position -> ScaleId for Glow's SCALE switch (spec 2026-08-07 §3.1).
// ScaleId is ordered by provenance (modes, pentatonics, exotic); the knob is
// ordered by FRICTION, so the travel runs calm -> sharp: the two scales with
// neither a minor second nor a tritone first, then the seven-note modes (which
// all contain both, a property of seven notes in twelve), then the
// hirajoshi/pygmy/kumoi bucket, then the exotics.
//
// That ordering is not re-derived by feel -- it is kScaleW (taste.h) read
// descending, and test_glow_ui.cpp pins the two together so a retune of the
// weights cannot leave this table quietly stale.
inline constexpr int kScaleKnobOrder[spky::SCALE_LIST_COUNT] = {
    spky::SCALE_MIN_PENT, spky::SCALE_MAJ_PENT,                   // 0.1750
    spky::SCALE_AEOLIAN,  spky::SCALE_DORIAN,
    spky::SCALE_MIXO,     spky::SCALE_LYDIAN,                     // 0.1125
    spky::SCALE_HIRAJOSHI, spky::SCALE_PYGMY, spky::SCALE_KUMOI,  // 0.0667
    spky::SCALE_PHRYGIAN, spky::SCALE_HIJAZ,
    spky::SCALE_HARM_MIN, spky::SCALE_WHOLE,                      // 0.0250
};

// Switch position -> what Flow::set_scale_override wants. Position 0 is AUTO,
// and so is anything out of range: a corrupt patch must not retune the
// instrument to whatever scale happens to sit at index 0.
inline int scale_of_knob(int pos) {
    if (pos < 1 || pos > spky::SCALE_LIST_COUNT) return -1;
    return kScaleKnobOrder[pos - 1];
}
```

- [ ] **Step 4: Run the tests**

```bash
cmake --build build && ./build/spky_tests -tc="glow: the scale knob*"
./build/spky_tests -tc="glow: knob position 0 is AUTO, the rest are scales"
ctest --test-dir build --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add host/vcv/src/glow_ui.hpp tests/test_glow_ui.cpp
git commit -m "feat(glow): scale knob order, derived from kScaleW not from feel

The SCALE switch travels calm to sharp instead of in ScaleId order, and a
test pins the table against kScaleW so retuning the weights cannot leave
the knob order silently wrong.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: the panel — a third widget kind

**Files:**
- Modify: `host/vcv/res/gen_flow_panel.py`
- Modify: `host/vcv/res/test_flow_panel.py:30` (`PARAM_ORDER`)
- Regenerate: `host/vcv/res/Glow.svg`, `host/vcv/src/generated_flow_panel.hpp`

**Interfaces:**
- Consumes: nothing.
- Produces: enum ids `GENRE` and `SCALE` in `ParamId` (**after** `NEW_BTN`),
  and `WK_SEL` in `WidgetKind`.

This is a generator change, not a hand-edit: `test_committed_files_match_the_generator`
compares both committed files against the generator's output.

- [ ] **Step 1: Extend `PARAM_ORDER` in the test — this is the RED**

`host/vcv/res/test_flow_panel.py:30`:

```python
PARAM_ORDER = ['MOTION', 'DENSITY', 'BRIGHT', 'DIRT', 'WANDER', 'SPACE',
               'NEW_BTN', 'GENRE', 'SCALE']
```

The new entries go **after** `NEW_BTN` even though they sit left and right of
it on the panel: enum order is the frozen contract that pins the first six
params to `flow_ids.h`'s macro order, and panel position is independent of it.

- [ ] **Step 2: Run the panel tests and watch them fail**

```bash
cd host/vcv && python res/test_flow_panel.py
```
Expected: FAIL, "param enum order drifted".

- [ ] **Step 3: Add the kind and its geometry to `gen_flow_panel.py`**

After the `NEW_LBL_DY` line (~37):

```python
SEL_R  = 5.5                      # GENRE / SCALE -- NEW's visual weight, not
                                  # the macros': at KNOB_R the captions would
                                  # land at y=89.4, on top of the patch field's
                                  # top border at y=89.0.
SEL_LBL_DY = 7.3                  # shares NEW's caption baseline at y=85.3
```

Then the kind and its four table entries (~58-67):

```python
MACRO = "MACRO"
BTN   = "BTN"
SEL   = "SEL"
IN    = "IN"
OUT   = "OUT"

RADIUS = {MACRO: KNOB_R, BTN: BTN_R, SEL: SEL_R, IN: JACK_R, OUT: JACK_R}
LBL_DY = {MACRO: KNOB_LBL_DY, BTN: NEW_LBL_DY, SEL: SEL_LBL_DY,
          IN: JACK_LBL_DY, OUT: JACK_LBL_DY}
LBL_SZ = {MACRO: 2.2, BTN: 2.2, SEL: 2.2, IN: 2.2, OUT: 2.2}
WKMAP  = {MACRO: "WK_MACRO", BTN: "WK_BTN", SEL: "WK_SEL",
          IN: "WK_IN", OUT: "WK_OUT"}
```

A missing key in any of these four is a `KeyError` inside `radius_of()`, which
three geometry tests call — that is why all four are edited together.

- [ ] **Step 4: Add the two controls to the `PARAMS` table**

Append to `PARAMS` (after `NEW_BTN`):

```python
    Ctl("GENRE", SEL, COL_X[0], NEW_XY[1], "GENRE",
        "GENRE -- which archetype NEW may draw. ANY: the weighted draw. "
        "Changes nothing until the next NEW press."),
    Ctl("SCALE", SEL, COL_X[2], NEW_XY[1], "SCALE",
        "SCALE -- fixes the scale. AUTO: whatever the terrain drew. "
        "Takes effect at once; the terrain's own scale returns on AUTO."),
```

- [ ] **Step 5: Add a third draw function**

`knob_svg` would `KeyError` on `MACRO_ACCENTS`, and `button_svg` emits
`id="newCopperCollar"` — reusing it would produce three elements sharing one
SVG id. Add after `button_svg`:

```python
def sel_svg(c):
    """GENRE / SCALE: a small graphite cap, no accent collar, no id."""
    return (
        '  <circle cx="%s" cy="%s" r="%s" fill="url(#knobCap)" stroke="%s" '
        'stroke-width="0.28"/>\n'
        '  <line x1="%s" y1="%s" x2="%s" y2="%s" stroke="%s" '
        'stroke-width="0.45" stroke-linecap="round"/>\n'
        % (mm(c.x), mm(c.y), mm(SEL_R), base.GRAPHITE,
           mm(c.x), mm(c.y - SEL_R * 0.40), mm(c.x), mm(c.y - SEL_R * 0.82),
           base.INK)
    )
```

and replace the two-way dispatch in `svg()` (line ~232):

```python
    for c in PARAMS:
        if c.kind == MACRO:
            out.append(knob_svg(c))
        elif c.kind == SEL:
            out.append(sel_svg(c))
        else:
            out.append(button_svg(c))
```

- [ ] **Step 6: Regenerate and run the panel tests**

```bash
cd host/vcv && python res/gen_flow_panel.py && python res/test_flow_panel.py
```
Expected: `panel OK`. If a geometry test fails, do not widen its tolerance —
the clearances hold by design: 20 mm to NEW (needs 5.5 + 4.5 = 10), 24 mm to
the macro row (needs 13.5), 4.98 mm and 55.98 mm at the edges (needs
2.0 … 58.96).

- [ ] **Step 7: Sanity-check the generated header**

`git diff host/vcv/src/generated_flow_panel.hpp` must show `GENRE` and `SCALE`
appended after `NEW_BTN` in `ParamId`, `WK_SEL` in `WidgetKind`, and two new
`kParamCtls` rows at x = 10.480 / 50.480, y = 78.000. Nothing else may move.

- [ ] **Step 8: Commit**

```bash
git add host/vcv/res/gen_flow_panel.py host/vcv/res/test_flow_panel.py \
        host/vcv/res/Glow.svg host/vcv/src/generated_flow_panel.hpp
git commit -m "feat(glow): panel gains GENRE and SCALE either side of NEW

A third widget kind rather than a reused one: knob_svg needs a macro
accent and button_svg emits the NEW collar's SVG id, which three elements
cannot share. Sized at NEW's 5.5 mm, not the macros' 8 -- at knob size the
captions would print on the patch field's top border.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 7: wire the two controls into `Glow.cpp`

**Files:**
- Modify: `host/vcv/src/Glow.cpp` — the `config()` loop (`:130-140`),
  `controlTick` (`:364`), the widget loop (`:539-550`)

**Interfaces:**
- Consumes: `Flow::set_genre`, `Flow::set_scale_override` (Tasks 3-4),
  `spkyvcv::scale_of_knob` (Task 5), `GENRE` / `SCALE` / `WK_SEL` (Task 6).
- Produces: nothing for later tasks except a working module.

- [ ] **Step 1: Replace the two-way `config()` dispatch with a three-way switch**

A new kind falling into the existing `else` would call `configButton` — a 0..1
momentary — and the switches would be silently unusable. `Glow.cpp:130-140`:

```cpp
        for (const auto& c : kParamCtls) {
            switch (c.kind) {
                case WK_MACRO:
                    // The six macro knobs stay on c.label, matching
                    // Fireflow.cpp -- deliberate, not an oversight.
                    configParam(c.id, 0.f, 1.f, 0.5f, c.label);
                    break;
                case WK_BTN:
                    // The NEW button's whole interaction model lives in one
                    // control, so its Rack tooltip should be the panel's full
                    // gesture-table string (c.tip), not just "NEW" (c.label).
                    configButton(c.id, c.tip);
                    break;
                case WK_SEL:
                    configSel(c);
                    break;
            }
        }
```

and add above the constructor:

```cpp
    // GENRE / SCALE. Both are snapped switches whose FIRST position is the
    // random one -- ANY draws the archetype at random, AUTO takes whatever
    // the terrain drew -- so the player selects randomness at the control.
    // That is exactly why neither joins Rack's Randomize: configSwitch leaves
    // randomizeEnabled at its default true (configButton is the one that
    // clears it), and letting Randomize pin the instrument to Whole tone
    // would remove a choice rather than add one.
    void configSel(const PanelCtl& c) {
        std::vector<std::string> labels;
        if (c.id == GENRE) {
            labels = { "Any", "Drone", "Pulse", "Arp", "Fragment" };
        } else {
            labels.push_back("Auto");
            for (int i = 0; i < spky::SCALE_LIST_COUNT; ++i)
                labels.push_back(spky::SCALE_NAMES[spkyvcv::kScaleKnobOrder[i]]);
        }
        configSwitch(c.id, 0.f, float(labels.size() - 1), 0.f, c.tip, labels);
        if (auto* pq = paramQuantities[c.id]) pq->randomizeEnabled = false;
    }
```

Scale names come from `spky::SCALE_NAMES` (`quantizer.h:53-57`), which exists
"so the two lists cannot drift apart" — they are not retyped here.

- [ ] **Step 2: Pin the knob→archetype arithmetic**

Add next to the existing `static_assert` block at `Glow.cpp:35-46`:

```cpp
// GENRE position 0 is ANY and 1..4 are the archetypes in enum order, so
// controlTick's `pos - 1` is only correct while ARCH_DRONE is 0. The six
// macro asserts above set the precedent for pinning arithmetic like this.
static_assert(spky::flow::ARCH_DRONE == 0,
              "GENRE's knob position -> archetype mapping assumes ARCH_DRONE == 0");
```

- [ ] **Step 3: Push both settings at the TOP of `controlTick`**

Before the `uiOp.exchange(...)` switch at `Glow.cpp:364` — not after.
`SET_TERRAIN` and `RESTORE` call `flow.wake()`, which force-pushes every
parameter through `recompute_and_push(true)`; pushing afterwards would land one
tick on the terrain's own scale after every patch load.

```cpp
    void controlTick(float sr) {
        // Host-owned settings first: wake() below force-pushes every
        // parameter, so an override applied after it would miss that push and
        // let one tick out on the terrain's own tonality.
        const int gpos = int(params[GENRE].getValue() + 0.5f);
        flow.set_genre(gpos <= 0 ? spky::flow::ARCH_ANY : gpos - 1);
        flow.set_scale_override(
            spkyvcv::scale_of_knob(int(params[SCALE].getValue() + 0.5f)));

        // Apply whatever the UI thread staged (fix round 3): flag read FIRST
        switch (uiOp.exchange(UiOp::NONE)) {
```

`configSwitch` sets `smoothEnabled = false`, so `getValue()` returns the
snapped integer with no smoothing lag; the `+ 0.5f` is belt and braces against
float representation, not a real rounding decision.

- [ ] **Step 4: Replace the two-way widget dispatch**

The existing `else` hard-wires the light id `NEW_L` — three widgets would drive
one lamp. `Glow.cpp:539-550`:

```cpp
        for (const auto& c : kParamCtls) {
            const Vec pos = mm2px(Vec(c.mm.x, c.mm.y));
            switch (c.kind) {
                case WK_MACRO:
                    // Rack's stock knobs come in fixed sizes;
                    // RoundLargeBlackKnob is 46 px ~ 15.6 mm, the nearest to
                    // the panel's 16 mm. RoundBigBlackKnob (54 px ~ 18.3 mm)
                    // would overhang the printed footprint by more than a
                    // millimetre a side.
                    addParam(createParamCentered<RoundLargeBlackKnob>(
                        pos, module, c.id));
                    break;
                case WK_BTN:
                    addParam(createLightParamCentered<VCVLightBezel<GreenLight>>(
                        pos, module, c.id, NEW_L));
                    break;
                case WK_SEL:
                    // 11 mm printed footprint; RoundBlackKnob is 38 px ~ 12.9
                    // mm, RoundSmallBlackKnob 28 px ~ 9.5 mm -- the smaller
                    // one stays inside the print.
                    addParam(createParamCentered<RoundSmallBlackKnob>(
                        pos, module, c.id));
                    break;
            }
        }
```

- [ ] **Step 5: Build the module**

```bash
cd host/vcv && ./build-local.sh
```
Never invoke `g++` directly — the system one is the ARM cross-compiler.

- [ ] **Step 6: Verify in Rack, by hand**

Load Glow and check all five:
1. GENRE at DRONE, then tap NEW ten times — the context menu's terrain code
   changes every time and (after Task 8) the archetype label always reads Drone.
2. Turning GENRE alone produces no audible change at all.
3. SCALE off AUTO retunes immediately; back to AUTO returns the terrain's scale.
4. Right-click → Randomize moves the six macros and leaves GENRE and SCALE
   where they were.
5. Right-click → Initialize returns GENRE to Any and SCALE to Auto.

- [ ] **Step 7: Commit**

```bash
git add host/vcv/src/Glow.cpp
git commit -m "feat(glow): wire GENRE and SCALE into the module

Both dispatch sites become explicit three-way switches: a new widget kind
falling into the old else would have been configured as a momentary button
and would have driven NEW's light. Settings are pushed at the top of
controlTick, before the staged-op switch, because wake() force-pushes every
parameter. Neither switch joins Randomize -- their first detent already is
the random setting.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 8: the ROOT menu, its persistence, and the sharing story

**Files:**
- Modify: `host/vcv/src/Glow.cpp` — member block (`:100-126`), `dataToJson`
  (`:156-163`), `dataFromJson` (`:165-210`), `onReset` (`:354-358`),
  `controlTick`, `appendContextMenu` (`:564-585`)
- Modify: `host/vcv/src/glow_ui.hpp:133` (the `GlowSave` comment)

**Interfaces:**
- Consumes: `Flow::set_root_override` (Task 4), `arch_of` (Task 1).
- Produces: nothing for later tasks.

- [ ] **Step 1: Add the atomic member**

A plain `int` written from the menu lambda (UI thread) and read every control
tick (audio thread) is a data race. The existing `UiOp` staging is the wrong
shape — it is a one-shot `exchange()`, while this is a standing value. Add
below the `UiOp` block:

```cpp
    // The ROOT override (spec 2026-08-07 §3.1), set from appendContextMenu on
    // the UI thread and read every controlTick on the audio thread. Atomic
    // rather than a plain int, and NOT a UiOp: UiOp is a one-shot exchange for
    // an operation, this is a standing value. Default sequential consistency,
    // matching uiOp -- no relaxed/acquire-release hand-rolling.
    std::atomic<int> rootOverride { -1 };     // -1 = AUTO
```

- [ ] **Step 2: Push it in `controlTick`**

Beside the GENRE/SCALE pushes added in Task 7, before the `uiOp.exchange`:

```cpp
        flow.set_root_override(rootOverride.load());
```

- [ ] **Step 3: Persist it — reading BEFORE the early return**

`dataToJson`, after the existing `lock` line:

```cpp
        json_object_set_new(root, "root", json_integer(rootOverride.load()));
```

`dataFromJson` — the read must sit **above** `if (!json_is_string(code)) return;`
at `Glow.cpp:168-169`, or a patch with a root override and a missing or corrupt
terrain string silently drops the override:

```cpp
    void dataFromJson(json_t* root) override {
        if (!root) return;
        // Read BEFORE the terrain's early return below: the override is
        // independent of the terrain, and a patch whose code is missing or
        // malformed must not lose it as collateral.
        if (json_t* r = json_object_get(root, "root")) {
            const int v = int(json_integer_value(r));
            rootOverride = (v >= 0 && v <= 11) ? v : -1;
        }
        spkyvcv::GlowSave s;
        json_t* code = json_object_get(root, "terrain");
        if (!json_is_string(code)) return;              // nothing to restore
```

- [ ] **Step 4: Clear it on Initialize**

`onReset()` (`Glow.cpp:354-358`) overrides Rack's *deprecated* hook, which the
default `onReset(const ResetEvent&)` calls after resetting params — so GENRE
and SCALE return to Any/Auto by themselves, but a non-param member does not:

```cpp
    void onReset() override {
        reinit(curSr > 0.f ? curSr : 48000.f);
        wakeHouse();
        knobs.primed = false;
        // Params are reset for us by Rack's default ResetEvent handler before
        // this runs, which covers GENRE and SCALE. rootOverride is not a
        // param, so Initialize would otherwise leave it stale.
        rootOverride = -1;
    }
```

- [ ] **Step 5: Add the menu items**

In `appendContextMenu`, after the existing `createMenuLabel("Terrain " + ...)`:

```cpp
        // Which genre the CURRENT terrain is. Free now that arch_of exists,
        // and the point of the GENRE control is auditioning one archetype at
        // a time -- without this the player cannot see which one they are in.
        static const char* kArchNames[] = { "Drone", "Pulse", "Arp", "Fragment" };
        menu->addChild(createMenuLabel(
            std::string("Genre ") +
            kArchNames[spky::flow::arch_of(m->flow.state().master)]));

        menu->addChild(createIndexSubmenuItem(
            "Root",
            { "Auto", "C", "C#", "D", "D#", "E", "F",
              "F#", "G", "G#", "A", "A#", "B" },
            [m]() { return m->rootOverride.load() + 1; },
            [m](int i) { m->rootOverride = i - 1; }));
```

- [ ] **Step 6: Correct the two comments that are now false**

A SCALE or ROOT override changes what the listener hears while leaving the
terrain code unchanged, so a pasted code no longer reproduces the sharer's
sound. GENRE is exempt — it is inaudible by construction.

`Glow.cpp:570-571`:

```cpp
        // Share a terrain: the code is the terrain's whole identity, so
        // copying it out and pasting it in carries the place itself. It is no
        // longer the instrument's whole STATE -- an explicit SCALE or ROOT
        // override (spec 2026-08-07 §3) rides on top of it and travels in the
        // patch, not in the code -- so a pasted code reproduces the sharer's
        // terrain, not necessarily their tonality.
```

`glow_ui.hpp:133`:

```cpp
// Exactly what a patch stores OF THE TERRAIN: current code, lock, undo slot.
// The tonality overrides (spec 2026-08-07 §3) are module settings rather than
// terrain state and are saved by Glow.cpp directly, not through here.
```

- [ ] **Step 7: Build and verify by hand**

```bash
cd host/vcv && ./build-local.sh
```

In Rack:
1. Set Root to F#, save the patch, reload it — the root is still F#.
2. Set Root to F#, Initialize — it returns to Auto.
3. Hand-edit the saved patch's `"terrain"` value to `"garbage"`, reload — Rack
   logs nothing catastrophic, the terrain is left alone, and the root override
   is still F#.
4. The menu's Genre label matches what GENRE draws after ten NEW presses under
   a lock.

- [ ] **Step 8: Run everything**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure
cd host/vcv && python res/test_flow_panel.py && ./build-local.sh
```

- [ ] **Step 9: Commit**

```bash
git add host/vcv/src/Glow.cpp host/vcv/src/glow_ui.hpp
git commit -m "feat(glow): ROOT in the context menu, and the sharing story corrected

The override is written from the UI thread and read on the audio thread,
so it is an atomic -- and not a UiOp, which is a one-shot exchange for an
operation rather than a standing value. Its JSON read sits above
dataFromJson's terrain early-return: a patch whose code is malformed must
not lose the override as collateral, and onReset has to clear it because
Rack's param reset cannot.

Two comments claiming the terrain code is the whole state are no longer
true and say so now.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## After the plan

The controls exist so that ten drones can be auditioned in a row and the actual
defect named. Candidates from
`docs/superpowers/specs/2026-08-05-flow-listening-notes.md`, none of them yet a
conclusion: the slow entrance (item 9 — half the drone terrains take over 4 s to
sound), the ~15.9 dB loudness spread (item 2), the discrete churn (item 7).

Two things this plan deliberately leaves undone, both recorded in spec §3.5 and
§4.7: a hand-turned SCALE fires without the reverb duck that hides scale changes
during a blend, and holding NEW while adjusting an adjacent knob still arms undo
or lock. Both are accepted pending ears, not oversights.

The `terrain.cpp` distance measurement quoted throughout was taken at `89eb461`
on 2026-08-06, before the weighted scale draw changed `base[P_SCALE]`'s
distribution. Re-running that harness is worth a session of its own; nothing in
this plan depends on it, because the genre branch has no threshold.
