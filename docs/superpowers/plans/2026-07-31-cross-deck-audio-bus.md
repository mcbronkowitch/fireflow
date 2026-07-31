# Cross-deck audio bus — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give each deck an audio-rate, stereo, one-sample-latency tap of the other deck's post-FX output, so a part engine can be played by its neighbour — and so the sampler can record the neighbouring deck without an external patch cable.

**Architecture:** `Instrument` owns `_deck_tap[PART_COUNT][2]`, written at the bottom of each sample from the per-deck FX-chain outputs and read at the top of the next. Both decks are pushed their sibling's tap through a new `Part::set_deck_in` **before** either one processes, so the latency is exactly one sample in both directions regardless of the CHOKE-driven processing order. Inside `Part`, the tap is summed into the engine's input and bounded with `fast_tanh`, behind the existing `_src_deck` flag — so with the source off (the default) the audio path is bit-identical to today.

**Tech Stack:** C++17, CMake + Ninja + clang, doctest. `source env.sh` before building. The VCV host builds **only** via `./build-local.sh`.

This is **movement 1** of `docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md`. It is self-contained and shippable on its own: the resampling feature lands here, with no dependency on `ENGINE_BBD` (movement 2) or the tape-echo revert (movement 3).

## Global Constraints

- **No new panel control.** The bus reuses the existing per-deck `set_excitation_sources(tape, other_deck, audio_in)`. The hardware-reducibility constraint forbids adding one.
- **Only the `other_deck` flag is shared** between the control-rate excitation bus and the new audio-rate path. `audio_in` keeps its own behaviour on each path: the excitation bus defaults it **false** (so an unmodified BODY deck is untouched), while the sampler receives audio-in **unconditionally** today.
- **`_audio_in_tap` keeps latching the raw input** (`part.h:313`), never the bounded sum. Latching the bounded value would count the neighbour twice — once through the engine input, once through `_other_deck_tap` (`part.cpp:383`) — and stack a second `fast_tanh` under `part.cpp:385`, moving what `bench/workloads_body.cpp` measures.
- **`set_engine` does not write patch state.** No engine switch may flip `_src_deck`; defaults belong in the init patch and the host's switch handler.
- **Source off ⇒ bit-exact.** Every existing render hash and every engine's output must be unchanged when neither deck selects `other_deck`.
- **`Part::process` is `__attribute__((always_inline))` into ten call sites** (`part.h:243`). Statements added to its body are inlined everywhere, silently — `-Winline` will not warn. Keep new work behind the `_src_deck` branch.
- **Latency is one sample in both directions, independent of CHOKE.** This is the contract; §4.2 of the spec explains why a "whoever runs first" bus is unacceptable.
- Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`

## File Structure

| file | responsibility | change |
|---|---|---|
| `engine/parts/part.h` | consume the tap: storage, setter, bounded sum at the engine input | modify |
| `engine/instrument.h` | own `_deck_tap[PART_COUNT][2]`; expose `deck_tap(p)` observer | modify |
| `engine/instrument.cpp` | read at the top of the sample, write at the bottom | modify |
| `tests/test_deck_bus.cpp` | the bus's own tests — latency, CHOKE independence, bound, neutrality, runaway | **create** |
| `CMakeLists.txt` | register the new test file | modify |
| `bench/workloads_system.cpp` | the A/B row, next to `instrument_worst` (`:437`) | modify |
| `bench/run.py` | register the row | modify |

One new test file rather than additions to `tests/test_instrument.cpp`, because the bus's tests need a shared fixture (a two-deck instrument with a known signal on one side) that nothing else wants.

---

### Task 1: `Part` consumes a bounded cross-deck input

**Files:**
- Modify: `engine/parts/part.h` (setter and members near `set_other_deck_tap`, `part.h:41-70`; the engine-input site at `part.h:315-324`; members near `part.h:563-565`)
- Test: `tests/test_deck_bus.cpp` (create), `CMakeLists.txt`

**Interfaces:**
- Produces: `void Part::set_deck_in(float l, float r)` — pushed once per deck per sample by `Instrument`, holding the **sibling** deck's post-FX stereo output from the previous sample. Not for panel or host use.
- Consumes: the existing `_src_deck` flag from `set_excitation_sources(bool, bool, bool)`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_deck_bus.cpp`:

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include "parts/part.h"
#include "util/fast_tanh.h"
using namespace spky;

// Settle the 4 ms engine-swap fade (192 samples at 48 kHz) plus the control
// raster, so `fade` is exactly 1.0 and the engine is the one we asked for.
static void settle(Part& p) {
    float l, r, sl, sr;
    for (int i = 0; i < 1000; ++i) { p.set_deck_in(0.f, 0.f); p.process(0.f, 0.f, l, r, sl, sr); }
}

TEST_CASE("deck bus: with the source off the tap never reaches the engine") {
    Part p;
    p.init(48000.f, 5);
    p.set_engine(ENGINE_SAMPLER);
    p.sampler().set_monitor(true);
    p.set_excitation_sources(true, /*other_deck=*/false, /*audio_in=*/false);
    settle(p);

    float l, r, sl, sr;
    p.set_deck_in(10.f, -10.f);          // absurd on purpose
    p.process(0.f, 0.f, l, r, sl, sr);
    CHECK(l == 0.f);                     // exactly zero, not approximately
    CHECK(r == 0.f);
}

TEST_CASE("deck bus: with the source on the tap reaches the engine") {
    Part p;
    p.init(48000.f, 5);
    p.set_engine(ENGINE_SAMPLER);
    p.sampler().set_monitor(true);
    p.set_excitation_sources(true, /*other_deck=*/true, /*audio_in=*/false);
    settle(p);

    float l, r, sl, sr;
    p.set_deck_in(0.01f, -0.01f);
    p.process(0.f, 0.f, l, r, sl, sr);
    CHECK(l == doctest::Approx(fast_tanh(0.01f)));
    CHECK(r == doctest::Approx(fast_tanh(-0.01f)));
}

TEST_CASE("deck bus: the engine input is bounded") {
    Part p;
    p.init(48000.f, 5);
    p.set_engine(ENGINE_SAMPLER);
    p.sampler().set_monitor(true);
    p.set_excitation_sources(true, /*other_deck=*/true, /*audio_in=*/false);
    settle(p);

    float l, r, sl, sr;
    for (float drive : {1.f, 10.f, 1000.f}) {
        p.set_deck_in(drive, -drive);
        p.process(0.f, 0.f, l, r, sl, sr);
        CHECK(std::fabs(l) <= 1.f);
        CHECK(std::fabs(r) <= 1.f);
        CHECK(std::isfinite(l));
        CHECK(std::isfinite(r));
    }
}
```

Register it in `CMakeLists.txt` alongside the other `tests/test_*.cpp` entries in the `spky_tests` target's source list.

**Note on the `process` overloads:** `Part` has `process(float, float, float&, float&, float&, float&)`, `process(float&, float&, float&, float&)` and `process(float&, float&)`. A call like `p.process(0.25f, 0.25f, l, r)` does **not** compile — literals cannot bind to the four-argument all-reference overload. Always use the six-argument form when passing input.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
source env.sh && cmake --build build --target spky_tests && ./build/spky_tests -tc="deck bus*"
```

Expected: FAIL — `'class spky::Part' has no member named 'set_deck_in'`.

- [ ] **Step 3: Add the setter and members**

In `engine/parts/part.h`, next to `set_other_deck_tap` (around line 66):

```cpp
    // Pushed once per sample by Instrument (the only scope where both parts
    // are visible), holding the SIBLING part's post-FX stereo output from the
    // PREVIOUS sample. Audio rate, unlike _other_deck_tap, which is the
    // control-rate mono excitation bus and stays exactly as it was. Not for
    // panel or host use.
    void set_deck_in(float l, float r) { _deck_in_l = l; _deck_in_r = r; }
```

With the members, next to `_src_tape` / `_src_deck` / `_src_audio` (around line 565):

```cpp
    float _deck_in_l = 0.f;
    float _deck_in_r = 0.f;
```

- [ ] **Step 4: Sum and bound at the engine input**

In `Part::process`, replace the engine-input line at `part.h:323`:

```cpp
        if (_engine_wants_in) _engine->process_in(inL, inR);
```

with:

```cpp
        if (_engine_wants_in) {
            float el = inL, er = inR;
            // Sum first, bound the sum -- the same idiom as the excitation
            // bus's fast_tanh(_bus_dc.Process(bus)) at part.cpp:385. The bound
            // lives HERE and not in SamplerEngine's monitor, which is
            // deliberately "dry input at unity": a fast_tanh there would move
            // the monitor level for every existing user (tanh(1) ~ 0.76) and
            // break the neutrality proof. With _src_deck false -- the default,
            // and today's behaviour -- this branch is not taken and the path
            // is bit-exact unchanged.
            if (_src_deck) {
                el = fast_tanh(el + _deck_in_l);
                er = fast_tanh(er + _deck_in_r);
            }
            _engine->process_in(el, er);
        }
```

Add `#include "util/fast_tanh.h"` to `part.h` if it is not already included.

**Do not touch `part.h:313`.** `_audio_in_tap = 0.5f * (inL + inR)` must keep latching the raw input — see Global Constraints.

- [ ] **Step 5: Run the tests to verify they pass**

```bash
source env.sh && cmake --build build --target spky_tests && ./build/spky_tests -tc="deck bus*"
```

Expected: PASS, all three cases.

- [ ] **Step 6: Commit**

```bash
git add engine/parts/part.h tests/test_deck_bus.cpp CMakeLists.txt
git commit -m "feat(part): the engine can hear the other deck, bounded at its input"
```

---

### Task 2: `Instrument` owns the tap, with one-sample latency both ways

**Files:**
- Modify: `engine/instrument.h` (observer near `tape_tap`/`excitation_bus`, `instrument.h:117-139`; member near `_dry_tap`, `instrument.h:291`)
- Modify: `engine/instrument.cpp` (the per-sample loop, around lines 100–150)
- Test: `tests/test_deck_bus.cpp`

**Interfaces:**
- Consumes: `Part::set_deck_in(float, float)` from Task 1.
- Produces: `float Instrument::deck_tap(int p, int ch) const` — deck `p`'s post-FX output from the sample just processed, `ch` 0 = L, 1 = R. Observer only, for tests.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_deck_bus.cpp`:

```cpp
#include "instrument.h"
#include <vector>

// Deck A monitors deck B through the bus; deck B makes a signal. Deck A's
// output at sample n must be the bound applied to deck B's output at n-1,
// for every CHOKE position -- that is the whole contract.
static void check_latency_one_sample(float choke, int src, int dst) {
    Instrument inst;
    inst.init(48000.f);                       // no FxMem: the FX chain stays off
    inst.set_engine(src, ENGINE_TEST_TONE);
    inst.set_engine(dst, ENGINE_SAMPLER);
    inst.sampler_monitor(dst, true);
    inst.set_excitation_sources(dst, true, /*other_deck=*/true, /*audio_in=*/false);
    inst.set_excitation_sources(src, true, /*other_deck=*/false, /*audio_in=*/false);
    inst.set_choke(choke);

    const int kN = 512;
    std::vector<float> tap_src(kN), tap_dst(kN);
    float outL[1], outR[1];
    for (int n = 0; n < kN; ++n) {
        inst.process(nullptr, nullptr, outL, outR, 1);
        tap_src[n] = inst.deck_tap(src, 0);
        tap_dst[n] = inst.deck_tap(dst, 0);
    }

    // Skip the engine-swap fade and the first control block.
    int checked = 0;
    for (int n = 200; n < kN; ++n) {
        CHECK(tap_dst[n] == doctest::Approx(fast_tanh(tap_src[n - 1])));
        if (std::fabs(tap_src[n - 1]) > 1e-4f) ++checked;
    }
    CHECK(checked > 0);        // the source must actually have been sounding
}

TEST_CASE("deck bus: one sample of latency, both directions, at every CHOKE") {
    for (float choke : {-1.f, -0.5f, 0.f, 0.5f, 1.f}) {
        check_latency_one_sample(choke, PART_A, PART_B);
        check_latency_one_sample(choke, PART_B, PART_A);
    }
}
```

Signatures used above, all verified against the tree: `Instrument::process(const float* inL, const float* inR, float* outL, float* outR, size_t n)` (`instrument.h:265`), `set_choke(float)` (`:247`), `set_excitation_sources(int p, bool, bool, bool)` (`:90`), `sampler_monitor(int p, bool)` (`:165`).

- [ ] **Step 2: Run it to verify it fails**

```bash
source env.sh && cmake --build build --target spky_tests && ./build/spky_tests -tc="deck bus: one sample*"
```

Expected: FAIL — `'class spky::Instrument' has no member named 'deck_tap'`.

- [ ] **Step 3: Add the storage and the observer**

In `engine/instrument.h`, next to `_dry_tap` (around line 291):

```cpp
    // Audio-rate cross-deck bus (spec 2026-07-31 bbd-part-engine §4.3).
    // Distinct from _dry_tap above, which is the control-rate MONO excitation
    // bus and is unchanged: this one is stereo, written every sample, and
    // carries the deck's POST-FX output -- what the player hears. Read at the
    // top of the sample and written at the bottom, so the latency is one
    // sample in both directions no matter which deck CHOKE runs first.
    float _deck_tap[PART_COUNT][2] = { { 0.f, 0.f }, { 0.f, 0.f } };
```

And with the other test observers (around line 130):

```cpp
    // Observer only, for tests: deck p's post-FX output from the sample just
    // processed. ch 0 = L, 1 = R. Latency cannot be measured from the summed
    // output, which cannot distinguish 0 samples from 1.
    float deck_tap(int p, int ch) const { return _deck_tap[p][ch]; }
```

- [ ] **Step 4: Wire the read and the write**

In `engine/instrument.cpp`, **before** `_parts[pri].set_inhibit(false);` (i.e. before either deck processes):

```cpp
        // Read the bus at the TOP of the sample, for BOTH decks, before either
        // one runs. Doing it here rather than between the two process() calls
        // is what makes the latency CHOKE-independent: a "whoever runs first
        // feeds whoever runs second" bus would be 0 samples one way and 1 the
        // other, and would swap as the knob crossed zero -- and a mutual
        // routing would then contain a 0-sample algebraic loop.
        for (int p = 0; p < PART_COUNT; ++p)
            _parts[p].set_deck_in(_deck_tap[1 - p][0], _deck_tap[1 - p][1]);
```

And **after** both decks have produced `pl[]` / `prr[]`, next to the existing `_dry_tap` write:

```cpp
        // Write at the BOTTOM, every sample -- unlike _dry_tap's once-per-block
        // guard below, which is a control-rate quantity.
        _deck_tap[PART_A][0] = al;  _deck_tap[PART_A][1] = ar;
        _deck_tap[PART_B][0] = bl;  _deck_tap[PART_B][1] = br;
```

- [ ] **Step 5: Run the test to verify it passes**

```bash
source env.sh && cmake --build build --target spky_tests && ./build/spky_tests -tc="deck bus*"
```

Expected: PASS — ten latency checks (five CHOKE positions × two directions).

- [ ] **Step 6: Commit**

```bash
git add engine/instrument.h engine/instrument.cpp tests/test_deck_bus.cpp
git commit -m "feat(instrument): the cross-deck bus, one sample either way regardless of CHOKE"
```

---

### Task 3: The neutrality proof

**Files:**
- Test: `tests/test_deck_bus.cpp`
- Run: the two render-hash ctests

**Interfaces:** consumes Tasks 1 and 2. Produces nothing new.

This matters more here than it did for WAVE or BODY: those *added* engines, this changes `Part::process`, which every engine runs.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("deck bus: every engine is bit-identical with the source off") {
    for (EngineId e : {ENGINE_TEST_TONE, ENGINE_SYNTH, ENGINE_SAMPLER,
                       ENGINE_WAVE, ENGINE_BODY}) {
        Part a, b;
        a.init(48000.f, 7);  b.init(48000.f, 7);
        a.set_engine(e);     b.set_engine(e);
        // Give the engines something to play, or a silent engine makes this
        // pass vacuously -- see the non-silence guard below.
        for (Part* p : {&a, &b}) {
            p->set_target_base(LANE_LEVEL, 1.f);
            p->mod().set_rate(0.5f);
        }
        // b is handed a hostile tap it must ignore; a is never told anything.
        float al, ar, asl, asr, bl, br, bsl, bsr;
        float peak = 0.f;
        for (int i = 0; i < 4000; ++i) {
            a.process(0.f, 0.f, al, ar, asl, asr);
            b.set_deck_in(3.f, -3.f);
            b.process(0.f, 0.f, bl, br, bsl, bsr);
            REQUIRE(bl == al);          // bit-identical, not Approx
            REQUIRE(br == ar);
            peak = std::max(peak, std::fabs(al));
        }
        // The sampler runs silent with no buffer (documented: "nullptr ->
        // runs silent"), so it is exempt -- it is covered by Tasks 1 and 4,
        // which drive it through the monitor path. Every other engine must
        // actually have sounded, or the identity above proved nothing.
        if (e != ENGINE_SAMPLER) {
            INFO("engine ", static_cast<int>(e), " produced silence");
            CHECK(peak > 1e-6f);
        }
    }
}
```

> If an engine other than the sampler comes back silent, **do not delete the
> guard** — find the missing setup (a level base, a trigger, a target) and
> make it sound. A silent reference is the failure this guard exists to catch.

> **Correction, from the review round — this test as drafted could not fail.**
> `_deck_in_l/_r` are read only inside `if (_engine_wants_in)`, i.e. only when
> the engine overrides `consumes_input()` — and `SamplerEngine` is the **only**
> override in the tree. So for TEST_TONE, SYNTH, WAVE and BODY the `_src_deck`
> guard is never reached at all, and for the sampler the tap reaches the output
> only through `if (_monitor)`, which defaults false and this draft never enabled.
> Deleting the guard entirely would have left the test green.
>
> The shipped test enables the sampler's monitor so the guard is genuinely
> exercised, and says in a comment that for the other four engines the guard is
> *unreachable* rather than merely untested — a stronger guarantee than a test,
> and the honest description of what their bit-identity proves. See
> `tests/test_deck_bus.cpp`, which is authoritative over this plan text.
>
> **The general lesson, worth carrying into movements 2 and 3:** the cross-deck
> bus does nothing for any engine but the sampler today. The BBD engine will be
> the second consumer of `process_in`, and until it exists, any test written as
> if the bus were live for other engines is testing nothing.

- [ ] **Step 2: Run it**

```bash
source env.sh && ./build/spky_tests -tc="deck bus: every engine*"
```

Expected: PASS immediately if Task 1 was implemented correctly (the `_src_deck` branch is not taken). **If it fails, Task 1 put work outside the guard** — fix that rather than relaxing the test.

- [ ] **Step 3: Run the render-hash gates**

```bash
source env.sh && ctest --test-dir build -R "ctrl_identity|wave_formant_sweep" --output-on-failure
```

Expected: PASS, hashes unchanged.

**Know what this does and does not cover.** These are the **only two** render-hash ctests (`CMakeLists.txt:183-203`) — there is none for SAMPLER or BODY — and `tests/check_render_hash.cmake:23-24` hashes the **WAV only** and deletes the CSV unread. The doctest above is what actually covers the other three engines; do not report "the hashes cover it".

- [ ] **Step 4: Commit**

```bash
git add tests/test_deck_bus.cpp
git commit -m "test(deck bus): the source-off path is bit-identical for all five engines"
```

---

### Task 4: Mutual routing stays finite

**Files:**
- Test: `tests/test_deck_bus.cpp`

**Interfaces:** consumes Tasks 1 and 2.

Sampler ↔ sampler is the one topology that would otherwise run away: both engines monitor their input through, neither bounds it, and the circulation would reach `inf`/`NaN` rather than merely getting loud — which poisons the record buffers. The master `Limiter` cannot prevent it: it is applied to the summed output after MORPH and reverb, **outside** the loop, and there is no per-deck limiter (per-deck COMP has a bit-exact bypass, `comp.h:17`).

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("deck bus: sampler <-> sampler mutual routing stays finite") {
    Instrument inst;
    inst.init(48000.f);
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_engine(p, ENGINE_SAMPLER);
        inst.sampler_monitor(p, true);
        inst.set_excitation_sources(p, true, /*other_deck=*/true, /*audio_in=*/true);
    }

    // 10 s at 48 kHz, with a hot input to give the loop something to build on.
    const int kBlock = 96;
    std::vector<float> inl(kBlock, 0.5f), inr(kBlock, -0.5f);
    std::vector<float> outL(kBlock), outR(kBlock);
    float peak = 0.f;
    for (int b = 0; b < 48000 * 10 / kBlock; ++b) {
        inst.process(inl.data(), inr.data(), outL.data(), outR.data(), kBlock);
        for (int i = 0; i < kBlock; ++i) {
            REQUIRE(std::isfinite(outL[i]));
            REQUIRE(std::isfinite(outR[i]));
            peak = std::max({peak, std::fabs(outL[i]), std::fabs(outR[i])});
        }
    }
    CHECK(peak < 100.f);      // bounded, not merely finite
}
```

- [ ] **Step 2: Run it**

```bash
source env.sh && ./build/spky_tests -tc="deck bus: sampler*"
```

Expected: PASS.

> **Correction, from the review round — the sentence that stood here was false.**
> It claimed the wrong bound ordering would show up as `inf`/`NaN`. It does not.
> Both orderings are contraction maps, because the exogenous term is a fixed
> constant rather than something that grows: correct order `x[n] = tanh(0.5 + x[n-2])`
> converges to ≈0.881, wrong order `x[n] = 0.5 + tanh(x[n-2])` converges to ≈1.381.
> Both stay bounded and both would clear a `peak < 100.f` ceiling, so the test as
> first drafted could not have caught the ordering bug it was written for. Worse,
> the master `Limiter` compresses both to ≈1.0 at the instrument's *output*, so no
> output-side ceiling can separate them at all.
>
> **What discriminates them is `deck_tap`, upstream of the limiter.** Correct
> ordering hard-clamps at `|y| <= 1.0` by `fast_tanh`'s own compare-clamp
> (`engine/util/fast_tanh.h:37`); wrong ordering adds the exogenous input *after*
> the clamp, so it can reach 1.5 and measures 1.381. The shipped test asserts
> that, plus a dead-routing baseline measured in the same test — see
> `tests/test_deck_bus.cpp`, which is authoritative over this plan text.

- [ ] **Step 3: Commit**

```bash
git add tests/test_deck_bus.cpp
git commit -m "test(deck bus): mutual sampler routing saturates instead of diverging"
```

---

### Task 5: Measure the bus

**Files:**
- Modify: `engine/parts/part.h`, `engine/instrument.cpp` (the compile-time guard)
- Modify: `bench/workloads_system.cpp` (the row, and its registration in `kSystemWorkloads[]` around lines 423-442)
- Modify: `bench/run.py` (`BENCH_PROTOCOL_ROWS_BY_FAMILY`, lines 167-303)

**Interfaces:** consumes Tasks 1 and 2.

`instrument_worst` — the row this one is paired against — lives in `bench/workloads_system.cpp:437` in the **`system`** family, not in `workloads_instr.cpp`. Put the new row next to it.

No cost figure is claimed for the bus anywhere in the spec, deliberately: an ISA hand-count would say ≈0.04 points, and round 4 made the same kind of count about the same loop and **was falsified by 2–4×, in the unfavourable direction**. This task replaces the guess with a measurement.

- [ ] **Step 1: Add a compile-time switch**

The A/B needs the bus code *absent* in one arm, and no such switch exists. In `engine/parts/part.h`, above the class:

```cpp
// Movement 1's cross-deck bus, behind a switch so the bench can build the
// A arm without it. Default on; the B/A pair is the only thing that ever
// sets it to 0.
#ifndef SPKY_DECK_BUS
#define SPKY_DECK_BUS 1
#endif
```

Then wrap the `if (_src_deck) { ... }` body from Task 1 and both `instrument.cpp` hunks from Task 2 in `#if SPKY_DECK_BUS` / `#endif`. With it 0, `set_deck_in` must still exist and compile to nothing observable, so the tests' call sites keep building.

- [ ] **Step 2: Add the row**

In `bench/workloads_system.cpp`, add `setup_inst_worst_deck_bus` as a copy of `setup_inst_worst` plus, on **both** decks, `inst.set_excitation_sources(p, true, /*other_deck=*/true, false)` — so the branch is actually taken and the row measures the bus rather than the guard. Reuse `proc_inst` as the process function. Register it:

```cpp
    { "system", "inst_worst_deck_bus", setup_inst_worst_deck_bus, proc_inst },
```

next to the `instrument_worst` entry at line 437, and add the row name to `bench/run.py`'s `BENCH_PROTOCOL_ROWS_BY_FAMILY` under `system`.

**Those are the two places rows are registered.** `bench/profiles.py` and `bench/families.cpp` register **families, never rows** — `profiles.py:4-5` says so in its own docstring — and `bench/anchor.cpp` pins anchored rows, which is separate again. Do **not** add this row to `anchor.cpp`: it is a measurement, not a gate.

Note the settle requirement the neighbouring rows document: `kInstrSettleBlocks = 200` blocks, so every envelope and slew has arrived before the measured window opens.

- [ ] **Step 3: Build and run both arms**

```bash
source env.sh && cd bench && make clean && make && python run.py --rows inst_worst_deck_bus,instrument_worst
```

Then rebuild with `-DSPKY_DECK_BUS=0` and run the same rows.

- [ ] **Step 4: Verify the row is real, not stale**

```bash
grep -c "inst_worst_deck_bus" bench/bench.map
```

Expected: non-zero. **The bench build can silently relink a stale object** — verify new rows against `bench.map`, not against the memory table.

- [ ] **Step 5: Write the report and commit**

Record both arms in `docs/bench/2026-07-31-<sha>-deck-bus.md`, reporting **`pct_max`** (the gate), not `pct_avg`. Then:

```bash
git add bench/ docs/bench/
git commit -m "bench(deck bus): price the cross-deck tap as a paired A/B"
```

---

## Definition of done

From the spec's §4.7. All must hold before this branch merges:

- [ ] `_deck_tap[PART_COUNT][2]` and `Instrument::deck_tap(p, ch)` exist; latency is one sample and is **equal in both directions** across a CHOKE sweep that crosses zero. *(Task 2)*
- [ ] With `other_deck` off on both decks, `ctrl_identity` and `wave_formant_sweep` are unchanged, **and** all five engines are bit-identical under the doctest — the hash gates alone do not cover SAMPLER or BODY. *(Task 3)*
- [ ] Sampler ↔ sampler mutual routing at full monitor produces finite, bounded output over 10 s. *(Task 4)*
- [ ] A paired same-source A/B prices the bus, reported as `pct_max`. *(Task 5)*
- [ ] The full test suite is green: `ctest --test-dir build --output-on-failure`.

## Out of scope

- **`ENGINE_BBD`** — movement 2. This branch adds no engine.
- **The tape-echo revert** — movement 3.
- **`FxMem` and `Part::init`'s signature.** Both grow in movements 2 and 3; nothing here touches either.
- **`test_panel.py`'s 53 failures.** Pre-existing BODY-era drift; fixing it is movement 2's first task. Do not let it block this branch, and do not treat it as an acceptance gate here.
- **The VCV surface.** The bus needs no new parameter, so `Spotymod.cpp` is untouched. The *default* for a BBD deck's source belongs to movement 2.
