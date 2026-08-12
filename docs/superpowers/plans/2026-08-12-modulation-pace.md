# PACE Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add one global modulation time-stretch, PACE (x1/32 .. x4, centre = exactly x1), reachable as a Fireflow knob and as Glow's macro slot 3, replacing DIRT.

**Architecture:** PACE multiplies two clocks at once — the Center transport BPM and every `SuperModulator`'s `_base_hz` — so the grid servo aims at the stretched target instead of fighting it. In the flow layer it is deliberately *not* a story: `P_PACE` is a base rule so a transferred patch keeps its own speed, and the macro adds a live offset on top inside `Flow`'s guard chain. Reaching x1/32 requires moving the lane phase accumulator to `double`, because the current `float` one stalls below ~1.4e-3 Hz.

**Tech Stack:** C++17 engine (`engine/`), doctest (`tests/`), clang + Ninja, VCV Rack plugin (`host/vcv/`), Python panel generators (`host/vcv/res/`).

**Spec:** `docs/superpowers/specs/2026-08-12-modulation-pace-design.md` (revision 3). Section references below (§N) point there. Read §2.1, §3.2, §4.2 and §6 before starting — they contain the three traps this design already fell into once.

## Global Constraints

- **Build with `-DCMAKE_BUILD_TYPE=Release`, never Debug.** `source env.sh` first. Debug fails `spky_tests` and `ctrl_identity` with "SYNTH reference moved" — the render hashes are Release-built.
- **Never build `host/vcv` by hand.** Always `host/vcv/build-local.sh`. The system `g++` is the ARM cross-compiler.
- **Everything written into the repo is English** — code, comments, commit messages, docs. The conversation is German; the files are not.
- **Commit trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- **A test that cannot go red gets fixed.** Every new gate is proven red once before it is trusted.
- **Panel SVGs and generated headers are byte-gated.** Never hand-edit `res/Fireflow.svg`, `res/FireflowHW.svg`, `res/Glow.svg`, `src/generated_panel.hpp`, `src/generated_hw_panel.hpp` or `src/generated_flow_panel.hpp` — regenerate them.
- **Task 1 can veto this whole plan.** Do not start Task 2 until it has passed.

**Standard commands:**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
# single test case:
./build/spky_tests -tc="the case name"
# panels (run from host/vcv/):
python3 res/gen_panel.py && python3 res/gen_hw_panel.py && python3 res/gen_flow_panel.py
python3 -m pytest res/test_panel.py res/test_hw_panel.py res/test_flow_panel.py -q
```

---

## File Structure

| File | Responsibility in this work |
|---|---|
| `engine/mod/divisions.h` | `pace_mult(norm)` — the curve, shared with the host tooltip |
| `engine/mod/lane.h` / `lane.cpp` | `double` phase accumulator, including `tick()`'s shadow; wrap counter observer |
| `engine/center/transport.h` | pulse-anchored `clock_pulse(float pace)` |
| `engine/center/center.h` | forwards the pace argument; exposes transport BPM for tests |
| `engine/mod/super_modulator.h` / `.cpp` | `set_pace`, `_base_hz *= _pace` |
| `engine/instrument.h` / `.cpp` | `set_pace`, private `_apply_tempo()`, observers |
| `engine/fx/flux.cpp` | THIN reads the rhythm in the same time frame as the delay |
| `engine/flow/flow_params.h` | `P_PACE` appended after `P_MODE`; `apply_param` case |
| `engine/flow/taste.h` | `M_DIRT` story deleted; five new base rules |
| `engine/flow/terrain.cpp` | `n_var == 0` guard |
| `engine/flow/flow.h` / `flow.cpp` | `_knob[M_PACE]` default; the offset guard |
| `engine/flow/flow_ids.h` | `M_DIRT` → `M_PACE` |
| `host/vcv/src/flow_patch_bridge.hpp` | `P_COMP_A` destination + veto note; report prose |
| `host/vcv/res/gen_panel.py`, `gen_hw_panel.py`, `gen_flow_panel.py` | the two panel positions and the rename |
| `host/vcv/src/init_patch.hpp` | `INIT_DEFAULTS["PACE"] = 0.5` |
| `host/render/scenario.cpp` | `set_pace` verb |
| `docs/flow-fireflow-param-map.md` | the authority; five rows move |

---

### Task 1: Re-home the veto proof (gate, already measured)

`tests/test_flow_veto.cpp` holds the suite's only red-capable proof that the runtime veto clamp at `flow.cpp:577-582` fires at all, as opposed to the ordinary `clamp_to(kParams, …)`. It rests on `P_COMP_A`, whose story curve Task 8 deletes, so it moves first.

**The measurement is done — 2026-08-12, recorded in §8.2.** A first attempt aimed the proof at `P_REVMIX_A` and failed: `eval_terrain` resolves a multi-owner parameter by *farthest from base wins* (`flow.cpp:329`), and `P_REVMIX_A`'s second owner BRIGHT "dawn" (`taste.h:894`, floor 0.40) keeps out-distancing SPACE's low candidate, holding A off its own 0.08 bound. Measured 0/400 masters. `P_REVMIX_B` (`taste.h:929`) carries the identical SPACE "bloom" curve and the identical 0.08 bound with **no second owner** and clears easily. Do not re-derive this; the numbers below are the ones to use.

| param | 60 masters, `% 30` (as committed) | 60 masters, `% 23` |
|---|---|---|
| `P_COMP_A` (the bar) | 13/60 | 31/60 |
| `P_REVMIX_A` | 0/60 | 0/60 |
| **`P_REVMIX_B`** | 0/60 | **37/60** |

**Files:**
- Modify: `tests/test_flow_veto.cpp` — the `TEST_CASE("flow veto: a macro moved mid-blend cannot breach a veto")` body only

**Interfaces:**
- Consumes: nothing.
- Produces: a veto test whose `interior_hit` requirement names `P_REVMIX_B`, surviving Task 8 unchanged.

- [ ] **Step 1: Widen the re-press cadence**

The sweep alternates macros between hard 0 and hard 1 every three ticks, and re-presses every 30. Those two cadences share a factor, so the residual always forms at the same handful of points. Replace

```cpp
            if (i > 0 && i % 30 == 0) f.new_full();
```

with

```cpp
            // Re-press on a cadence coprime with the 3-tick macro alternation
            // above, so a residual forms at many different points of the
            // BRIGHT/SPACE disagreement rather than always the same one.
            if (i > 0 && i % 23 == 0) f.new_full();
```

Leave the master count at 60. `P_REVMIX_B` clears the `P_COMP_A` bar there (37 vs 13); the 400-master escalation the first attempt used is not needed.

- [ ] **Step 2: Move the requirement to `P_REVMIX_B`**

Replace the final `for (int v = 0; v < kVetoCount; ++v)` requirement loop and its comment with:

```cpp
    // Required on P_REVMIX_B. Its 0.08 lo sits strictly inside kParams' 0..1,
    // so a hit there cannot come from clamp_to(kParams, ...) and is specific
    // evidence the veto clamp itself fired mid-blend. It took this role from
    // P_COMP_A on 2026-08-12, when the PACE work deleted the DIRT story and
    // left COMP_A a near-constant base rule with no range to overshoot from.
    //
    // B and not A, though the two share a bound and a curve: eval_terrain
    // resolves a multi-owner param by farthest-from-base (flow.cpp:329), and
    // REVMIX_A has a second owner in BRIGHT "dawn" whose floor is 0.40. That
    // distant candidate keeps winning the combine and holds A off its own
    // 0.08 bound -- measured 0/400 masters. REVMIX_B has only SPACE "bloom",
    // so it reaches the floor freely: 37/60 masters here.
    // Do not widen this to all five params without re-measuring first.
    for (int v = 0; v < kVetoCount; ++v) {
        if (kVetos[v].param != P_REVMIX_B) continue;
        CAPTURE(pname(kVetos[v].param));
        CHECK(interior_hit[v]);
    }
```

Keep whatever other `CHECK`s the case already makes; only the interior-bound requirement moves.

- [ ] **Step 3: Run it green**

```bash
source env.sh && cmake --build build
./build/spky_tests -tc="flow veto: a macro moved mid-blend cannot breach a veto"
```

Expected: PASS.

- [ ] **Step 4: Prove the new gate RED**

Temporarily comment out the body of the `for (int vi = 0; vi < kVetoCount; ++vi)` veto-clamp loop at `engine/flow/flow.cpp:577-582`, rebuild, and run the same case.

Expected: FAIL on the `P_REVMIX_B` requirement — with the clamp gone, nothing pulls the residual back inside the band, so the value that used to land on the interior bound now sails past it and `interior_hit` is never set.

Restore `engine/flow/flow.cpp` (`git checkout -- engine/flow/flow.cpp`), rebuild, confirm green again. `git status` must show `flow.cpp` clean before you commit.

- [ ] **Step 5: Full suite, then commit**

```bash
ctest --test-dir build --output-on-failure
git add tests/test_flow_veto.cpp
git commit -m "$(cat <<'EOF'
test(flow): the veto proof moves from COMP_A to REVMIX_B

The interior-bound requirement was the suite's only red-capable proof
that flow.cpp's veto clamp fires rather than kParams' ordinary range
clamp, and it rested on P_COMP_A having a story curve wide enough to
overshoot from. The PACE work deletes that story, so the proof moves
first, before anything depends on it.

REVMIX_B earns the role by owning its 0.08 floor alone. Its twin
REVMIX_A shares the bound and the SPACE curve but has BRIGHT "dawn" as a
second owner, and eval_terrain's farthest-from-base combine lets that
0.40 floor hold A off its own bound -- measured 0/400 masters against
B's 37/60. The re-press cadence moves to 23 ticks, coprime with the
sweep's 3-tick macro alternation, so the residual forms at many points
of the BRIGHT/SPACE disagreement instead of always the same one.

Proven red by disabling the clamp.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 2: `pace_mult` in `divisions.h`

**Files:**
- Modify: `engine/mod/divisions.h` (after `tide_free`, ~line 80)
- Test: `tests/test_divisions.cpp` (create if absent; otherwise append)

**Interfaces:**
- Produces: `float spky::pace_mult(float norm)` — 0..1 in, x1/32..x4 out, exactly `1.0f` at `norm == 0.5f`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("pace: the curve meets exactly 1 at centre") {
    // The whole safety argument for a global time control is that it has a
    // position where it provably does not exist. Exact, not Approx: the
    // no-op claim is a bit-identity claim (spec 2026-08-12 PACE §2).
    CHECK(spky::pace_mult(0.5f) == 1.0f);
    CHECK(spky::pace_mult(0.f)  == doctest::Approx(1.f / 32.f));
    CHECK(spky::pace_mult(1.f)  == doctest::Approx(4.f));
    CHECK(spky::pace_mult(0.75f) == doctest::Approx(2.f));
    // Monotone rising across the knee, and clamped outside 0..1.
    float prev = spky::pace_mult(0.f);
    for (int i = 1; i <= 100; ++i) {
        const float m = spky::pace_mult(float(i) * 0.01f);
        CHECK(m > prev);
        prev = m;
    }
    CHECK(spky::pace_mult(-1.f) == spky::pace_mult(0.f));
    CHECK(spky::pace_mult(2.f)  == spky::pace_mult(1.f));
}
```

- [ ] **Step 2: Run it to verify it fails**

```bash
source env.sh && cmake --build build
./build/spky_tests -tc="pace: the curve meets exactly 1 at centre"
```

Expected: compile error, `pace_mult` not declared.

- [ ] **Step 3: Implement**

Add to `engine/mod/divisions.h` after `tide_free`:

```cpp
// PACE: the global modulation time-stretch (spec 2026-08-12 modulation-pace).
// Piecewise so both halves meet at EXACTLY 1.0 at the centre -- that identity
// is what makes PACE 0.5 a bit-identical no-op, the same property that makes
// TIDE 0.5 one. Asymmetric on purpose: the fast end is already reachable
// through RATE, so the resolution goes where it was missing.
//
//   0.0 -> x1/32   0.25 -> x1/5.7   0.5 -> x1   0.75 -> x2   1.0 -> x4
//
// Lives here rather than in the host for the reason stated at the top of
// free_hz: the curve belongs where the engine reads it, so a tooltip cannot
// drift from what actually runs. The NAME does not follow it here -- PACE is
// continuous, unlike kTideNames' 9 rungs, so a label has to be formatted and
// the host owns that.
inline float pace_mult(float norm) {
    const float n = clampf(norm, 0.f, 1.f);
    return n <= 0.5f ? std::pow(32.f, 2.f * n - 1.f)
                     : std::pow(4.f,  2.f * n - 1.f);
}
```

- [ ] **Step 4: Run it to verify it passes**

Same command. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/mod/divisions.h tests/test_divisions.cpp
git commit -m "feat(mod): pace_mult, the global time-stretch curve

Piecewise 32^ below centre and 4^ above so both halves meet at exactly
1.0 -- the identity the bit-identical no-op rests on. Asymmetric because
the fast end is already reachable through RATE.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The lane phase accumulator moves to `double`

Below ~1.4e-3 Hz the `float` increment is under half an ulp and the melodic lane **freezes** (§2.1). This is a latent bug today — RATE 0 has only 14x of margin — and PACE x1/32 spends all of it.

**Files:**
- Modify: `engine/mod/lane.h:190` and the accessors at `:67`, `:102-106`
- Modify: `engine/mod/lane.cpp:10-33` (the shadow), `:585`, `:645-646`, `:675`, `:703-704`
- Modify: `tests/test_lane_tick.cpp` (re-derive the skew budget)
- Modify: `tests/check_render_hash.cmake` (re-baseline)

**Interfaces:**
- Produces: `ModLane` accumulating phase in `double`; all public accessors keep their `float` signatures.

- [ ] **Step 1: Write the failing test**

> **CORRECTION, 2026-08-12 — the test code below is inverted and must not be
> used.** It was written into this plan and rejected during execution, then
> independently confirmed broken by review. At 0.02 Hz, 400 s is exactly 8
> whole turns, so a *healthy* lane returns to its start phase and
> `fabs(phase - start) > 0.05` **fails on the one working rate**. At 0.00125 Hz
> the lane owes exactly 0.5 turns and the documented stall parks at exactly
> 0.50; at 0.000625 Hz it owes 0.25 and stalls at 0.25. The frozen lane
> produces the very number the healthy lane owes, so the gate **passes on the
> bug it was written to catch** — inverted on all four rates.
>
> What shipped instead (`tests/test_lane.cpp`, commit `e449bf4`) counts *turns
> over 1600 s against a ±2% band*, which is what spec §8 asked for in the first
> place: gate on wraps per unit time, never on displacement and never on Hz.
> It goes red on 0.00125 Hz, 0.000625 Hz **and** 0.0025 Hz — the last one
> running 7% fast in the round-up band above the stall, which the original
> formulation could not have seen at all.

```cpp
TEST_CASE("lane: phase still advances at PACE-reachable slow rates") {
    // The float accumulator stalls once _phase_inc drops below half an ulp of
    // the current binade: measured freezes at phase 0.50 for 0.00125 Hz and at
    // 0.25 for 0.000625 Hz. 0.000625 Hz is RATE 0 (0.02 Hz) at PACE x1/32,
    // i.e. the slowest setting the design advertises.
    //
    // This gate counts WRAPS, not Hz. Every rate observer in the engine reports
    // the COMMANDED rate, which stays perfectly correct while the lane is
    // frozen solid -- which is exactly how a float accumulator would have
    // shipped this green (spec 2026-08-12 modulation-pace, §2.1 and §8).
    for (float hz : {0.02f, 0.0025f, 0.00125f, 0.000625f}) {
        spky::ModLane lane;
        lane.init(48000.f, 12345u);
        lane.set_rate_hz(hz);
        const float start = lane.phase();
        const int   secs  = 400;
        for (int i = 0; i < 48000 * secs; ++i) lane.process();
        CAPTURE(hz);
        // At 0.000625 Hz one cycle is 1600 s, so 400 s must advance a quarter
        // turn. Any stall parks the phase and this difference collapses.
        CHECK(std::fabs(lane.phase() - start) > 0.05f);
    }
}
```

- [ ] **Step 2: Run it to verify it fails**

```bash
source env.sh && cmake --build build
./build/spky_tests -tc="lane: phase still advances at PACE-reachable slow rates"
```

Expected: FAIL on `hz = 0.00125` and `hz = 0.000625`.

- [ ] **Step 3: Convert the accumulator AND `tick()`'s shadow**

This is the step revision 2 of the spec got wrong. `process_window_end` is explicitly a *model* of `process()`; leaving it `float` both breaks the model and — via `lane.cpp:703-704`, which assigns `shadow_end.phase` back into `_phase` — re-quantizes the accumulator to `float` on every near-endpoint tick.

`engine/mod/lane.h`:

```cpp
    double _phase = 0.0;
    double _phase_inc = 0.0;
```

Accessors keep their float signatures and cast on read:

```cpp
    float phase() const { return static_cast<float>(_phase); }
```

```cpp
    float step_samples() const {
        return _phase_inc > 0.0
            ? static_cast<float>(1.0 / (_phase_inc * (1.0 + double(_ev_rate))
                                        * double(_steps)))
            : 0.f;
    }
```

`engine/mod/lane.cpp`, the shadow struct and function:

```cpp
struct ProcessWindowEnd {
    double phase;
    int wraps;
};

// Rare endpoint fallback for tick(): replay only process()'s raw phase
// additions for the whole control window. This is a MODEL of process(), so its
// arithmetic must match process()'s exactly -- it moved from float to double
// together with _phase on 2026-08-12 (PACE). If the two ever diverge again,
// tick()'s endpoint decision is made against a path the lane does not run, and
// line "_phase = shadow_end.phase" below silently re-quantizes the accumulator.
static ProcessWindowEnd process_window_end(
    double phase, const double* phase_per_sample_by_wrap, int rate_count) {
    int wraps = 0;
    for (int sample = 0; sample < ModLane::kTickInterval; ++sample) {
        const int rate_index = wraps < rate_count ? wraps : rate_count - 1;
        phase += phase_per_sample_by_wrap[rate_index];
        while (phase >= 1.0) {
            phase -= 1.0;
            ++wraps;
        }
    }
    return {phase, wraps};
}
```

At `:585`: `_phase += _phase_inc * (1.0 + double(_ev_rate));`
At `:587`: `while (_phase >= 1.0) { _phase -= 1.0; wrapped = true; }`
At `:645-646`: `const double window_start_phase = _phase;` and `double window_dp[2 * kSeqSlots + 1];`
At `:648`: `window_dp[0] = _phase_inc * (1.0 + double(_ev_rate));`
At `:675`: `const double dp1 = _phase_inc * (1.0 + double(_ev_rate));`

Also update `set_rate_hz` (`lane.cpp:102`) to compute `_phase_inc` in double, and every remaining `1.f`/`0.f` literal compared against `_phase` or `_phase_inc` in `tick()`.

- [ ] **Step 4: Run to verify it passes**

Expected: PASS on all four rates.

- [ ] **Step 5: Re-derive the tick-equivalence budget by measurement**

`tests/test_lane_tick.cpp` asserts `skew_events <= 4` over 400 ticks, and its comment at `:67-73` states the premise this change breaks: "the two paths accumulate phase differently (96 rounded adds vs one fused product)". Run it:

```bash
./build/spky_tests -tc="*tick*" -s
```

If it fails, **measure the new worst case** — add a temporary `MESSAGE` reporting the observed `skew_events` per case — set the budget to that number, and rewrite the comment to say the paths now accumulate in double and what the new measurement was. Do **not** widen it to a round number that happens to fit.

- [ ] **Step 6: Re-baseline the render hashes**

```bash
ctest --test-dir build --output-on-failure
```

`ctrl_identity` and the `spky_tests` render hashes will fail. Regenerate the reference hashes in this commit and say why in the message. Nothing else in this task may change a hash — verify by re-running the full suite after the update.

> **CORRECTION, 2026-08-12:** the `EXPECTED=` hashes do **not** live in
> `tests/check_render_hash.cmake` as this plan and the Files list above both
> claimed — that file carried no hash at all. They are in `CMakeLists.txt:33`
> and `:57`. A third baseline also moves: the FNV phrase-determinism digest in
> `tests/test_song_form.cpp:284-288`. All three moved in `e449bf4`.

- [ ] **Step 7: Commit**

```bash
git add engine/mod/lane.h engine/mod/lane.cpp tests/test_lane.cpp \
        tests/test_lane_tick.cpp tests/check_render_hash.cmake
git commit -m "$(cat <<'EOF'
fix(mod): the lane phase accumulator moves to double

ModLane::_phase was a float accumulated per sample, and LANE_PITCH runs
that path every sample. Below ~1.4e-3 Hz at 48 kHz the increment is under
half an ulp and the lane FREEZES -- measured: 0.00125 Hz parks at phase
0.50, 0.000625 Hz at 0.25. Just above the stall there is a worse band
where every add rounds up to a full ulp and the lane runs up to twice as
fast as commanded. At RATE 0 today the margin is only 14x, and DRIFT's
_ev_rate eats a fifth of that, so this was already a cliff nobody had
documented; PACE x1/32 would have driven straight off it.

tick()'s process_window_end shadow converts with it. It is explicitly a
MODEL of process(), and it writes its result back into _phase -- left as
float it would both mis-model the new path and re-quantize the
accumulator on every near-endpoint tick.

The new gate counts WRAPS, not Hz: every rate observer reports the
commanded rate, which stays correct while the lane is stopped.

Render hashes re-baselined: double accumulation changes the low bits.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 4: Pulse-anchored `clock_pulse`

`Transport::clock_pulse()` is `_beats = std::round(_beats)`, which encodes "one external pulse == one transport beat". A paced transport breaks that: at x1/32 `_beats` advances 0.03125 per pulse and `round()` snaps it back, freezing the transport at beat 0 (§3.2).

**Files:**
- Modify: `engine/center/transport.h:26`
- Modify: `engine/center/center.h:36`
- Modify: `engine/instrument.h:398`
- Test: `tests/test_transport.cpp` (create if absent)

**Interfaces:**
- Produces: `Transport::clock_pulse(float pace)`, `Center::clock_pulse(float pace)`, `Instrument::clock_pulse()` (which supplies its own `_pace`, added in Task 5 — until then pass `1.f`).

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("transport: an external clock survives a paced transport") {
    spky::Transport t;
    t.init(500.f);                 // control rate
    t.set_bpm(120.f * 0.03125f);   // 120 BPM at PACE x1/32

    // One pulse per REAL beat: at 120 BPM that is every 0.5 s = 250 ticks.
    // Each pulse must advance the paced transport by `pace` beats, not snap
    // it back to the same integer.
    for (int pulse = 0; pulse < 8; ++pulse) {
        for (int i = 0; i < 250; ++i) t.tick();
        t.clock_pulse(0.03125f);
    }
    CHECK(t.beats() == doctest::Approx(8.0 * 0.03125).epsilon(0.02));

    // And a pace CHANGE mid-stream must not jump: the anchor is the previous
    // pulse, not absolute zero. Anchored at zero this walks off by up to half
    // a real beat on every pulse while the knob moves -- straight into a hard
    // servo whose authority is kLockCap = 0.35.
    const double before = t.beats();
    t.set_bpm(120.f * 1.32f);
    t.set_pace_anchor();            // what Instrument::set_pace calls
    for (int i = 0; i < 250; ++i) t.tick();
    t.clock_pulse(1.32f);
    CHECK(t.beats() - before == doctest::Approx(1.32).epsilon(0.02));
}
```

- [ ] **Step 2: Run it to verify it fails**

```bash
./build/spky_tests -tc="transport: an external clock survives a paced transport"
```

Expected: compile error (`clock_pulse` takes no argument).

- [ ] **Step 3: Implement**

`engine/center/transport.h`:

```cpp
    // One external pulse is one REAL beat, which is `pace` paced-beats. The
    // snap grid is therefore a multiple of pace -- but anchored at the PREVIOUS
    // PULSE, not at absolute zero. _beats is the integral of bpm*pace, so after
    // a pace change _beats/pace is no longer the real beat count, and a
    // zero-anchored grid jumps: 100 beats at x1, then pace 1.32, and the next
    // pulse computes round(100/1.32)*1.32 = 100.28. Under a swept PACE macro
    // that fires on EVERY pulse, up to half a real beat, into the hard grid
    // servo (kLockCap 0.35). At pace == 1 the whole expression is bit-identical
    // to the round(_beats) it replaced.
    void clock_pulse(float pace) {
        if (!(pace > 0.f) || !std::isfinite(pace)) return;
        _beats  = _anchor + double(pace)
                          * std::round((_beats - _anchor) / double(pace));
        _anchor = _beats;
    }
    // PACE moved: re-anchor so the grid follows the knob instead of lagging it.
    void set_pace_anchor() { _anchor = _beats; }
    void reset()       { _beats = 0.0; _anchor = 0.0; }
```

with `double _anchor = 0.0;` beside `_beats`.

`engine/center/center.h:36`: `void clock_pulse(float pace) { _transport.clock_pulse(pace); }` plus `void set_pace_anchor() { _transport.set_pace_anchor(); }`.

`engine/instrument.h:398`: `void clock_pulse() { _center.clock_pulse(_pace); }` — `_pace` arrives in Task 5; until then write `1.f` and change it there.

- [ ] **Step 4: Run to verify it passes**

Expected: PASS both halves.

- [ ] **Step 5: Prove it RED**

Revert `clock_pulse`'s body to `_beats = std::round(_beats / double(pace)) * double(pace);` (zero-anchored). Expected: the second half FAILS. Restore.

- [ ] **Step 6: Full suite and commit**

```bash
ctest --test-dir build --output-on-failure
git add engine/center/transport.h engine/center/center.h engine/instrument.h \
        tests/test_transport.cpp
git commit -m "$(cat <<'EOF'
fix(center): the external clock snap anchors on the previous pulse

clock_pulse() encoded "one pulse == one transport beat" as round(_beats).
Once the transport is paced that is wrong twice over: at x1/32 the beats
advance 0.03125 between pulses and round() snaps them back, freezing the
transport at beat 0 while the lanes keep running.

Snapping to a multiple of pace fixes that but not the second half --
_beats is the integral of bpm*pace, so after a pace change _beats/pace is
not the real beat count and a zero-anchored grid jumps on every pulse
while the knob moves. The anchor is the previous pulse. At pace 1 the
expression is bit-identical to what it replaced.

The pace is passed, never stored: a Transport::_pace member would have
been a value-initialised zero that DIVIDES, and the resulting NaN in
_beats reaches set_rate_hz, where it becomes 0 and stops both decks.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 5: `set_pace` on the engine

**Files:**
- Modify: `engine/mod/super_modulator.h` (declare `set_pace`), `super_modulator.cpp:27-31`
- Modify: `engine/instrument.h` (declare `set_pace`, `_pace`, observers), `engine/instrument.cpp:100-114`
- Test: `tests/test_instrument.cpp`

**Interfaces:**
- Consumes: `pace_mult` (Task 2), `Transport::set_pace_anchor` (Task 4).
- Produces: `Instrument::set_pace(float norm)`; `Instrument::pace_for_test()`, `Instrument::transport_bpm_for_test()`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("pace: centre is exactly neutral, and both worlds scale") {
    spky::Instrument in;
    in.init(48000.f);
    in.set_tempo_bpm(120.f);
    for (int p = 0; p < 2; ++p) in.set_rate(p, 0.5f);

    float before[2][spky::LANE_COUNT];
    for (int p = 0; p < 2; ++p)
        for (int l = 0; l < spky::LANE_COUNT; ++l)
            before[p][l] = in.lane_rate_hz_for_test(p, l);

    in.set_pace(0.5f);
    for (int p = 0; p < 2; ++p)
        for (int l = 0; l < spky::LANE_COUNT; ++l)
            CHECK(in.lane_rate_hz_for_test(p, l) == before[p][l]);  // EXACT

    // Free world.
    in.set_sync(false);
    in.set_pace(0.f);
    for (int p = 0; p < 2; ++p)
        for (int l = 0; l < spky::LANE_COUNT; ++l)
            CHECK(in.lane_rate_hz_for_test(p, l)
                  == doctest::Approx(before[p][l] / 32.f));

    // Grid world: the LANES and the TRANSPORT scale together, or the servo
    // pulls the lanes back onto the unstretched grid and wins.
    in.set_sync(true);
    in.set_pace(1.f);
    CHECK(in.transport_bpm_for_test() == doctest::Approx(120.f * 4.f));
    in.set_pace(0.5f);
    CHECK(in.transport_bpm_for_test() == doctest::Approx(120.f));
}

TEST_CASE("pace: a fresh instrument runs at x1") {
    spky::Instrument in;
    in.init(48000.f);
    CHECK(in.pace_for_test() == 1.0f);
}

TEST_CASE("pace: a non-finite request is dropped, not propagated") {
    spky::Instrument in;
    in.init(48000.f);
    in.set_tempo_bpm(120.f);
    in.set_rate(0, 0.5f);
    const float ok = in.lane_rate_hz_for_test(0, spky::LANE_PITCH);
    in.set_pace(std::numeric_limits<float>::quiet_NaN());
    // Without the guard NaN reaches _base_hz, set_rate_hz maps it to 0, and
    // both decks go silent with no error at all.
    CHECK(in.lane_rate_hz_for_test(0, spky::LANE_PITCH) == ok);
}
```

- [ ] **Step 2: Run to verify it fails**

Expected: compile error, `set_pace` not declared.

- [ ] **Step 3: Implement `SuperModulator::set_pace`**

`super_modulator.h`, beside `set_tempo_bpm`:

```cpp
    // The global modulation time-stretch. Multiplies _base_hz in BOTH branches
    // of _update_rate -- synced via division_hz, free via free_hz -- because
    // free mode never reads _bpm and would otherwise ignore PACE entirely.
    void set_pace(float mult)      { _pace = mult; _update_rate(); }
```

with `float _pace = 1.f;` beside `_bpm`, and in `super_modulator.cpp:27-31`:

```cpp
void SuperModulator::_update_rate() {
    _base_hz = (_synced ? division_hz(division_index(_rate_norm), _bpm)
                        : free_hz(_rate_norm)) * _pace;
    _apply_rate();
}
```

- [ ] **Step 4: Implement `Instrument::set_pace` and factor the fan-out**

`instrument.cpp` — `_bpm` must stay the **raw** BPM, or pace compounds on every call:

```cpp
void Instrument::set_tempo_bpm(float bpm) {
    // ... existing finite guard, unchanged ...
    if (!(bpm > 0.f) || !std::isfinite(bpm)) return;
    _bpm = bpm;
    _apply_tempo();
}

// PACE (spec 2026-08-12 modulation-pace). Same guard as set_tempo_bpm and for
// the same stated reason: host/render/scenario.cpp forwards scenario-file
// values unvalidated. A NaN pace is worse than a NaN BPM -- Transport::set_bpm
// catches the latter, but a NaN _base_hz is mapped to 0 by set_rate_hz and both
// decks go silent with no error.
void Instrument::set_pace(float norm) {
    if (!std::isfinite(norm)) return;
    const float m = pace_mult(norm);
    if (m == _pace) return;              // Fireflow pushes every knob per tick
    _pace = m;
    _center.set_pace_anchor();           // the clock grid follows the knob
    _apply_tempo();
}

// The single door, shared by both entry points. PACE reaches the transport and
// the mod lanes; FLUX gets the RAW bpm and is corrected in its own rhythm
// reader instead (spec §3.3).
void Instrument::_apply_tempo() {
    _center.set_tempo_bpm(_bpm * _pace);
    for (auto& p : _parts) p.mod().set_pace(_pace);
    for (auto& p : _parts) p.mod().set_tempo_bpm(_bpm);
    for (auto& p : _parts) p.fx().set_bpm(_bpm);
}
```

`instrument.h`: declare `void set_pace(float norm);`, private `void _apply_tempo();`, `float _pace = 1.f;`, and inside the existing `#ifdef SPKY_TESTING` block:

```cpp
    float pace_for_test() const { return _pace; }
    // Reads the TRANSPORT's bpm, which carries the pace -- NOT Instrument::_bpm,
    // which is the raw value PACE never touches. A gate written against _bpm
    // would be measuring something this control cannot move.
    float transport_bpm_for_test() const { return _center.transport().bpm(); }
```

Change `Instrument::clock_pulse()` to `_center.clock_pulse(_pace);`.

- [ ] **Step 5: Run to verify it passes**

Expected: all three cases PASS.

- [ ] **Step 6: Prove the grid gate RED**

Remove `* _pace` from the **synced branch only** (split the ternary so the free branch keeps it). The grid case must fail — but note it only reddens outside PACE ∈ [0.438, 0.608], because the servo absorbs any mismatch inside `kLockCap = 0.35`. The test above uses PACE 1.0 and 0.0, so it is outside that window by construction. Restore.

- [ ] **Step 7: Full suite and commit**

```bash
git add engine/mod/super_modulator.h engine/mod/super_modulator.cpp \
        engine/instrument.h engine/instrument.cpp tests/test_instrument.cpp
git commit -m "$(cat <<'EOF'
feat(engine): set_pace, one global modulation time-stretch

PACE scales the Center transport BPM and every SuperModulator's _base_hz
together. Scaling only the lanes would put them against Center's grid
servo, whose hard-lock gain is deliberately strong enough to win; scaling
both keeps the ratio intact and the free world's grid gravity cancels the
factor on both sides of its own comparison.

set_pace re-runs the fan-out itself rather than waiting for the next tempo
push. Fireflow would have hidden the omission -- it pushes tempo every
control tick -- but Glow only pushes it when a fader is assigned, and the
render host pushes it once before any scenario event.

_bpm stays the RAW value and both entry points call a private _apply_tempo,
so pace cannot compound. FLUX keeps the raw bpm on purpose.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 6: FLUX reads the rhythm in its own time frame

FLUX stays in real time, but `update_thin_pattern` compares `_rhy_gap` — samples between PITCH-lane onsets, fully paced — against an unpaced `rep`. Below x1 the skip count pins at `kMaxSkip = 16` and any LINK > 0 becomes a near-permanent mute of the FLUX return (§3.3).

**Files:**
- Modify: `engine/fx/flux.h` (a pace setter + `_thin_n` observer), `engine/fx/flux.cpp:94-104`
- Modify: `engine/fx/part_fx.h`, `engine/instrument.cpp` (`_apply_tempo` forwards the pace to FLUX's reader only)
- Test: `tests/test_flux.cpp`

**Interfaces:**
- Produces: `Flux::set_rhythm_pace(float)`, `Flux::thin_n_for_test(int)`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("flux: THIN reads the rhythm in the delay's own time frame") {
    // _rhy_gap is measured in samples between PITCH-lane onsets and is fully
    // paced; rep comes from the RAW bpm and is not. Below x1 the ratio pins at
    // kMaxSkip and LINK becomes a near-permanent mute of the return. Dividing
    // the gap by the pace puts both sides in the same frame.
    spky::Flux fx;
    fx.init(48000.f, nullptr, nullptr);
    fx.set_bpm(120.f);
    fx.set_rate(0.5f);
    const int gap = 24000;                 // one onset every 0.5 s at x1
    fx.set_rhythm_pace(1.f);
    fx.set_rhythm_gap_for_test(0, gap);
    const int n_at_x1 = fx.thin_n_for_test(0);

    // The same music at x1/32: onsets are 32x further apart in samples.
    fx.set_rhythm_pace(1.f / 32.f);
    fx.set_rhythm_gap_for_test(0, gap * 32);
    CHECK(fx.thin_n_for_test(0) == n_at_x1);
}
```

- [ ] **Step 2: Run to verify it fails**

Expected: FAIL — `thin_n_for_test(0)` returns `kMaxSkip` (16) instead of `n_at_x1`.

- [ ] **Step 3: Implement**

`engine/fx/flux.cpp:94-104`:

```cpp
void Flux::update_thin_pattern() {
    const float rep = _delay_time * _sr;
    for (int i = 0; i < 2; ++i) {
        int n = 1;
        if (rep > 0.f) {
            // _rhy_gap is paced (it counts samples between PITCH-lane onsets);
            // rep comes from the raw bpm because FLUX itself stays in real
            // time. Divide the gap back into real time so the ratio means what
            // it meant before PACE existed (spec 2026-08-12 §3.3).
            const float gap = static_cast<float>(_rhy_gap[i]) * _rhythm_pace;
            n = static_cast<int>(gap / rep + 0.5f);
        }
        if (n < 1) n = 1;
        if (n > link_tuning::kMaxSkip) n = link_tuning::kMaxSkip;
        _thin_n[i] = n;
    }
}
```

with `float _rhythm_pace = 1.f;` and `void set_rhythm_pace(float p) { _rhythm_pace = p; update_thin_pattern(); }`. In `Instrument::_apply_tempo`, add `for (auto& p : _parts) p.fx().set_rhythm_pace(_pace);`.

- [ ] **Step 4: Run to verify it passes**

- [ ] **Step 5: Commit**

```bash
git add engine/fx/flux.h engine/fx/flux.cpp engine/fx/part_fx.h \
        engine/instrument.cpp tests/test_flux.cpp
git commit -m "$(cat <<'EOF'
fix(flux): THIN reads the rhythm in the delay's own time frame

update_thin_pattern compared _rhy_gap -- samples between PITCH-lane
onsets, fully paced -- against a rep derived from the raw bpm. Below x1
the ratio pins at kMaxSkip and any LINK above zero turns into a
near-permanent mute of the FLUX return.

The delay itself stays in real time; only the reader is corrected. An
earlier draft scaled the delay time on the fast half instead, which fixed
nothing: above x1 both quantities already shrink by the same factor, so
the ratio was already pace-invariant there.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 7: The wraps-per-time gate at instrument level

Task 3 gated the lane in isolation. This gates it through the whole stack, which is where a missing `set_pace` fan-out or a servo fight would show (§8).

**Files:**
- Modify: `engine/mod/lane.h` (wrap counter, `SPKY_TESTING`-guarded **including the increment**), `lane.cpp:587`
- Modify: `engine/mod/super_modulator.h`, `engine/instrument.h` (pass-throughs)
- Test: `tests/test_instrument.cpp`

**Interfaces:**
- Produces: `Instrument::lane_wraps_for_test(int part, int lane)`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("pace: wraps per unit time follow the commanded rate") {
    for (float knob : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
        spky::Instrument in;
        in.init(48000.f);
        in.set_sync(false);
        in.set_tempo_bpm(120.f);
        in.set_rate(0, 0.35f);
        in.set_pace(knob);
        const float hz = in.lane_rate_hz_for_test(0, spky::LANE_PITCH);
        const int secs = 60;
        const int w0 = in.lane_wraps_for_test(0, spky::LANE_PITCH);
        // drive the engine, not the lane, so the whole fan-out is exercised
        std::vector<float> l(256), r(256);
        for (int i = 0; i < 48000 * secs / 256; ++i)
            in.process(nullptr, nullptr, l.data(), r.data(), 256);
        const float expected = hz * float(secs);
        const float got = float(in.lane_wraps_for_test(0, spky::LANE_PITCH) - w0);
        CAPTURE(knob); CAPTURE(hz); CAPTURE(expected); CAPTURE(got);
        // Wraps, not Hz. Every rate observer reports the COMMANDED rate and
        // stays correct while a lane is frozen; only this counts real motion.
        if (expected >= 1.f) CHECK(got == doctest::Approx(expected).epsilon(0.05));
        else                 CHECK(got >= 0.f);   // sub-one-cycle window
    }
}
```

At PACE 0 the pitch lane runs at 0.005 Hz, so 60 s is well under one cycle. Extend the window for that case to 400 s and assert the phase moved instead, mirroring Task 3's gate.

- [ ] **Step 2: Run to verify it fails**

Expected: compile error, `lane_wraps_for_test` not declared.

- [ ] **Step 3: Implement the counter with a guarded increment**

`engine/mod/lane.h`, inside the existing `#ifdef SPKY_TESTING` block:

```cpp
    uint32_t wrap_count_for_test() const { return _wraps; }
#endif
```

and the member plus its increment **both** guarded — the increment sits in the per-sample hot path, and `body_voice.cpp:178` is the precedent for guarding it rather than making firmware pay for a test:

```cpp
#ifdef SPKY_TESTING
    uint32_t _wraps = 0;
#endif
```

`lane.cpp:587`:

```cpp
    while (_phase >= 1.0) {
        _phase -= 1.0;
        wrapped = true;
#ifdef SPKY_TESTING
        ++_wraps;
#endif
    }
```

Pass-throughs in `super_modulator.h` and `instrument.h` mirror the existing `lane_rate_hz_for_test` chain.

- [ ] **Step 4: Run to verify it passes**

- [ ] **Step 5: Prove it RED**

Temporarily revert `_phase` to `float` in `lane.h`. The PACE 0 and 0.25 cases must fail. Restore.

- [ ] **Step 6: Commit**

```bash
git add engine/mod/lane.h engine/mod/lane.cpp engine/mod/super_modulator.h \
        engine/instrument.h tests/test_instrument.cpp
git commit -m "test(engine): gate PACE on wraps per unit time, not on Hz

Every rate observer in the engine reports the COMMANDED rate, which stays
perfectly correct while a lane is frozen -- so a stalled accumulator
would pass every other gate in this design green. This one counts real
motion, through the whole fan-out rather than on a bare lane.

The counter's increment is SPKY_TESTING-guarded as well as its getter:
it sits in the per-sample path and firmware must not pay for a test.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 8: The flow layer — one atomic commit

Deleting the `M_DIRT` story without §4.3's guard leaves `t.window[M_PACE]` at the `{0,0}` non-identity: no crash, silently wrong terrain output. Everything below lands together.

**Files:**
- Modify: `engine/flow/flow_ids.h:5`, `flow_params.h` (param + `apply_param` + the stale comment), `taste.h` (story deleted, five base rules), `terrain.cpp:520-556`, `terrain.h:93-102` + `:55-57`, `flow.h:177`, `flow.cpp:82` + the guard chain + `weather_of`
- Modify: `docs/flow-fireflow-param-map.md`
- Modify: `tests/test_flow_overlay.cpp:24-36`, `test_flow_terrain.cpp:23,39`, `test_flow_taste.cpp:26-28`, `test_flow_new.cpp:393`, `test_flow_veto.cpp:107,232-233`

**Interfaces:**
- Consumes: `Instrument::set_pace` (Task 5).
- Produces: `P_PACE` (after `P_MODE`), `M_PACE` (index 3, no story).

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("flow: PACE is an offset on the patch's own pace, not a replacement") {
    spky::Instrument in; in.init(48000.f);
    spky::flow::Flow f; f.init(&in, 500.f);
    spky::flow::TerrainState st; st.master = 7u;

    // A carried patch that was built slow.
    spky::flow::BaseOverlay ov{};
    ov.set(spky::flow::P_PACE, 0.25f);
    f.wake(st, &ov);
    f.set_macro(spky::flow::M_PACE, 0.5f);
    for (int i = 0; i < 10; ++i) f.tick();
    CHECK(f.param_now(spky::flow::P_PACE) == doctest::Approx(0.25f));

    // The knob offsets it; it does not overwrite it.
    f.set_macro(spky::flow::M_PACE, 0.75f);
    for (int i = 0; i < 10; ++i) f.tick();
    CHECK(f.param_now(spky::flow::P_PACE) == doctest::Approx(0.50f));
}

TEST_CASE("flow: NEW does not move the pace") {
    spky::Instrument in; in.init(48000.f);
    spky::flow::Flow f; f.init(&in, 500.f);
    spky::flow::TerrainState st; st.master = 11u;
    f.wake(st);
    f.set_macro(spky::flow::M_PACE, 0.75f);
    for (int i = 0; i < 10; ++i) f.tick();
    const float settled = f.param_now(spky::flow::P_PACE);
    REQUIRE(f.new_full());
    // Through the whole 6 s ramp -- an offset that leaked into _resid would
    // show as a spike here, decaying back over the blend.
    for (int i = 0; i < 700; ++i) {
        f.tick();
        CHECK(f.param_now(spky::flow::P_PACE) == doctest::Approx(settled));
    }
}

TEST_CASE("flow: a Flow whose macros were never pushed runs at x1") {
    // Every value-initialised carrier in this codebase is zero, and zero here
    // means x1/32 -- the extreme of the range. host/render's flow_wake pushes
    // no macros at all, so without this default every headless demo of PACE
    // would play 32x too slow (spec 2026-08-12 §6).
    spky::Instrument in; in.init(48000.f);
    spky::flow::Flow f; f.init(&in, 500.f);
    spky::flow::TerrainState st; st.master = 3u;
    f.wake(st);
    for (int i = 0; i < 10; ++i) f.tick();
    CHECK(f.param_now(spky::flow::P_PACE) == doctest::Approx(0.5f));
    CHECK(in.pace_for_test() == doctest::Approx(1.f));
}
```

- [ ] **Step 2: Run to verify they fail**

Expected: compile error, `P_PACE` / `M_PACE` not declared.

- [ ] **Step 3: Add `P_PACE` after `P_MODE`**

In `flow_params.h`, append **after** the `P_MODE` line and rewrite the comment block above `P_MODE`, which currently says "MUST STAY LAST":

```cpp
  /* P_MODE ... 
     MUST BE PUSHED AFTER P_RANGE_A/B (static_assert in flow.cpp) and its base
     draw must keep its stream key: base draws are keyed kStreamParamBase +
     param (terrain.cpp:382), so inserting a parameter BEFORE this one re-seeds
     its stream and re-resolves the FLOW/STEP draw of every existing terrain
     code. Appending AFTER it is free, which is where P_PACE went on
     2026-08-12. This used to read "MUST STAY LAST"; the positional half of
     that argument is now an explicit static_assert. */ \
  X(P_MODE,       0.f, 1.f,  2) \
  /* PACE: the global modulation time-stretch (spec 2026-08-12). 0.5 = x1.
     Deliberately NOT story-owned -- a story-owned parameter is unreachable
     from the base overlay by construction (terrain.cpp:437-461), so owning it
     would throw away a transferred patch's own speed. M_PACE adds a live
     offset on top in Flow's guard chain instead. */ \
  X(P_PACE,       0.f, 1.f, 0)
```

Add the `static_assert` beside the two existing ones in `flow.cpp:53-56`:

```cpp
// P_MODE no longer holds the last slot, so the argument that recompute_and_push
// may read _mode_now during the P_RANGE_A/B iteration -- because P_MODE has not
// been pushed yet this tick -- becomes explicit instead of positional.
static_assert(P_RANGE_A < P_MODE && P_RANGE_B < P_MODE,
              "P_RANGE_A/B must be pushed before P_MODE");
```

and `case P_PACE: in.set_pace(v); break;` to `apply_param`. Also rewrite the three echoes of the positional argument at `flow.cpp:512-513, 527-528, 542-543`.

- [ ] **Step 4: Delete the DIRT story, add five base rules**

In `taste.h`, delete the `{ M_DIRT, "heat", 4, {...} }` entry entirely and add to `kBaseRules`:

```cpp
// P_PACE: the terrain draws NO pace -- the row exists so the base overlay has
// a destination (generate() applies the overlay by iterating kBaseRules) and
// the coverage test has no hole. 0.5 is exactly x1; draw_span returns the
// centre exactly when lo == hi.
{ P_PACE,     {{.5f,.5f},{.5f,.5f},{.5f,.5f},{.5f,.5f}} },
// --- former M_DIRT "heat" targets, base rules since 2026-08-12 (PACE) ------
// Real spans, not the story's degenerate {0,0} bp0. Inheriting bp0 would have
// kept every existing terrain code rendering identically -- at the price of
// pinning P_DRIVE at 0 forever, and DRIVE has had no Fireflow control since
// the 2026-08-09 reduction retired MASTER_DRIVE/PUSH, so Glow would have lost
// master drive outright with nothing able to restore it. Terrain codes
// re-render instead; that was the owner's ruling.
// P_DRIVE stays inside its veto band (kVetos: 0.00-0.40) and well under the
// 0.25 the limiter's own comment calls the end of "clean".
{ P_DRIVE,    {{0.f,.10f},{.05f,.20f},{.05f,.25f},{.10f,.30f}} },  // drone = clean
// GRIT is the wet/dry of a block that is never switched on under Glow
// (nothing in engine/flow/ calls set_fx_on), so these spans are inaudible
// today and carried for the transfer's sake. Keep them modest so they are not
// a surprise the day that gap closes.
{ P_GRIT_A,   {{0.f,.15f},{0.f,.25f},{0.f,.25f},{.05f,.35f}} },
{ P_GRIT_B,   {{0.f,.15f},{0.f,.25f},{0.f,.25f},{.05f,.35f}} },
// COMP_A joins COMP_B's band. Both sit inside kVetos' 0.40-0.60, the by-ear
// ceiling the owner set by listening -- do NOT widen either to claw back level.
{ P_COMP_A,   {{.50f,.60f},{.50f,.60f},{.50f,.60f},{.50f,.60f}} },
```

- [ ] **Step 5: Guard the story-less macro**

`terrain.cpp`, immediately before the `pick_index` call at `:531`:

```cpp
        if (n_var == 0) {
            // M_PACE has no story: its whole effect is a runtime role in
            // Flow, like M_MOTION's weather depth but without a curve. Guard
            // BEFORE pick_index, which returns n-1 == -1 for n == 0 and would
            // leave mm.story naming story index 0 by value-init. The window
            // must be written explicitly too: Terrain t{} leaves it {0,0},
            // and terrain.h's own comment warns that the {0,1} identity is
            // only "currently unreachable" while every macro has a story.
            mm.story = -1;
            mm.n_targets = 0;
            t.window[m] = {0.f, 1.f};
            continue;
        }
        const int pick = pick_index(r, n_var);
```

Rewrite `terrain.h:93-102`'s comment to say the invariant has moved and which macro is the exception.

- [ ] **Step 6: The offset, in the one safe window**

In `flow.cpp`'s guard chain, **after** `_cont_now[p] = v;` (`:489`) and **before** the veto loop (`:577`), beside the FILT floor and RANGE cap:

```cpp
        // PACE's macro is an OFFSET on the terrain's (or a transferred patch's)
        // own pace, not a replacement -- so a carried patch plays at its own
        // speed with the knob centred. It lands HERE, in the guard chain, and
        // not between the blend line and _cont_now above: there it would land
        // in _resid, which begin_blend freezes at press time, and a NEW press
        // with the knob at 0.75 would push the pace to x4 and slide it back to
        // x2 over six seconds. The operand is v, the blend line's result --
        // NOT t.base[P_PACE], which would step instead of ramp when two
        // terrains carry different transferred paces.
        else if (p == P_PACE)
            v = clamp_to(kParams[p], v + (_eff[M_PACE] - 0.5f));
```

- [ ] **Step 7: Weather exclusion and the knob default**

`flow.cpp:297`, beside the M_MOTION skip:

```cpp
        // M_PACE is excluded too, for a different reason: the offset is up to
        // +-0.10 in knob units, and in pace_mult's curve 0.10 is a factor of
        // TWO. A tempo wandering by two makes every other macro's motion
        // unreadable, because the pace is the frame the ear judges them
        // against. It also keeps eff[M_PACE] exactly 0.5 when nothing moves
        // it, which the bit-identical-no-op claim depends on.
        if (m == M_MOTION || m == M_PACE) continue;
```

`flow.cpp:82`:

```cpp
    for (int m = 0; m < MACRO_COUNT; ++m) {
        // 0.5, not 0: PACE's neutral is the CENTRE of its knob, and zero here
        // would mean x1/32 on every draw. host/render's flow_wake pushes no
        // macros at all (scenario.cpp), so a zero default would make every
        // headless demo run 32x too slow.
        _knob[m] = (m == M_PACE) ? 0.5f : 0.f;
        _cv[m] = 0.f; _eff[m] = 0.f;
    }
```

- [ ] **Step 8: Rename the macro and update the six red tests**

`flow_ids.h:5`: `M_DIRT` → `M_PACE`. Then:

```bash
git grep -lw M_DIRT | xargs sed -i 's/\bM_DIRT\b/M_PACE/g'
```

Then update, each with a comment naming this change:

- `test_flow_overlay.cpp:34-36` — `kBaseRuleCount == 42` → 47; `CHECK_FALSE(is_base_rule(P_COMP_A))` → `CHECK(is_base_rule(P_COMP_A))`.
- `test_flow_terrain.cpp:23` and `:39` — skip `M_PACE` by name, not by loosening the bound for all six.
- `test_flow_taste.cpp:26-28` — permit exactly one named story-less macro.
- `test_flow_new.cpp:393` — the budget becomes `MACRO_COUNT - 2` with both exceptions named (SPACE, slewed/ducked; PACE, no story).
- `test_flow_veto.cpp:107` — rewrite; it describes `P_DRIVE`'s degenerate DIRT story, which is gone.
- `test_flow_veto.cpp:232-233` — note that `t.adventure[M_PACE]` is drawn but unused, so a dead level can qualify a terrain as high-adventure.

- [ ] **Step 9: Update the authority**

`docs/flow-fireflow-param-map.md` is the repo's declared authority — "if a mapping is not in that file, the converter does not do it". Update:

- Counts: mapped 41 → 44, UNREACHABLE 1 → 2, total 42 → 47.
- Delete "`P_COMP_A` is deliberately absent … story-owned" (`:28`).
- "The other 21 of `P_COUNT` = 63" → 17 of 64 (`:33`).
- Rewrite `:178-182`.
- Add rows: `P_PACE` (from Fireflow's PACE knob, direct); `P_COMP_A` (from the deck-A LVL knob, with the veto note); `P_GRIT_A/B` (direct, with the "inaudible under Glow" note the FLUXMIX row carries); **`P_DRIVE` in the UNREACHABLE column** — its Fireflow control was retired 2026-08-09.

- [ ] **Step 10: Re-measure `distance()`**

`terrain.cpp:594-682` states `P_COUNT 63` and derives min/mean/max plus "NO same-archetype pair of 6 603 reaches kDistanceMin". `P_COUNT` is now 64 and `P_COMP_A`'s value changes on every terrain. Re-run that block's own harness and update every number, or the comment becomes exactly the stale lie it was written twice to correct. Also fix `terrain.h:55-57` ("38 base-rule slots … 315 bytes"; 315 = 63×5 becomes 320).

- [ ] **Step 11: Full suite, then commit**

```bash
ctest --test-dir build --output-on-failure
git add engine/flow docs/flow-fireflow-param-map.md tests/
git commit -m "$(cat <<'EOF'
feat(flow): DIRT becomes PACE

The DIRT macro was mostly dead: both its GRIT targets set the wet/dry of
a block nothing ever switches on, leaving a 0.13 move of deck A's
compressor and a master DRIVE that stayed flat until three quarters of
the knob. It was a loudness control, which is what its own veto comment
warned against.

PACE replaces it, and deliberately without a story. Story-owned
parameters are unreachable from the base overlay by construction, so a
macro that OWNED the pace would throw away a transferred patch's speed --
the bug 887b767 just closed one level down. P_PACE is a base rule the
overlay can reach and the macro adds a live offset on top, in the guard
chain where it cannot leak into _resid.

P_PACE is appended AFTER P_MODE so no stream re-seeds; the positional
half of P_MODE's "MUST STAY LAST" becomes a static_assert.

DIRT's four targets become base rules with real spans. Terrain codes
re-render as a result -- the alternative pinned master DRIVE at zero
forever, and it has had no Fireflow control since 2026-08-09.

Flow::_knob defaults M_PACE to 0.5: every value-initialised carrier here
is zero, and zero means x1/32.

kBaseRuleCount 42 -> 47, and docs/flow-fireflow-param-map.md moves with
it -- it is the authority, and four of these rows are named in it.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 9: The patch bridge

**Files:**
- Modify: `host/vcv/src/flow_patch_bridge.hpp:449-468`, `:540-559`
- Test: `tests/test_flow_patch_bridge.cpp`, `tests/test_flow_transfer_diff.cpp`

**Interfaces:**
- Consumes: `P_PACE`, the five new base rules (Task 8).
- Produces: `to_flow_base` writing `P_PACE`, `P_COMP_A`, `P_GRIT_A/B`.

- [ ] **Step 1: Write the failing test**

```cpp
TEST_CASE("bridge: a patch carries its own pace and its deck-A compression") {
    spkyvcv::FireflowPatch fp{};
    fp.p[spkyvcv::kFfPace]  = 0.25f;
    fp.p[spkyvcv::kFfCompA] = 0.55f;
    NoteSink sink;
    const auto r = spkyvcv::to_flow_base(fp, sink);
    CHECK(r.base[spky::flow::P_PACE] == doctest::Approx(0.25f));
    CHECK(r.set[spky::flow::P_COMP_A]);
    // COMP_A is veto-confined to 0.40-0.60, so a carried value the runtime
    // moves must be tagged, exactly as COMP_B's row already is.
    CHECK(sink.has_tag(spky::flow::P_COMP_A, "REWRITTEN AT RUNTIME"));
}
```

- [ ] **Step 2: Run to verify it fails**

- [ ] **Step 3: Implement**

Replace the `"NO DESTINATION … P_COMP_A is story-owned"` note at `:465-468` with a `set_base` and the veto note modelled on `P_COMP_B`'s at `:449-458`. **The deleted note carries a second fact** — that the deck BALANCE is lost with it — which must move to a note of its own rather than vanish. Add `set_base(r, P_PACE, fp.p[kFfPace])`, and `P_GRIT_A/B` with the "carried but inaudible under Glow" note the FLUXMIX row already uses.

- [ ] **Step 4: Fix the report prose**

`:540-559` is hand-counted and no test checks its arithmetic: "63 parameters and 42 base rules, so 21 are story-owned … Four of those (FORM_A, SONG_A, STEPS_A, COMP_A) … the 17 remaining … GRIT … plus DRIFT, DRIVE". After this work: 64 parameters, 47 base rules, 17 story-owned, three named individually, and GRIT and DRIVE leave the list. Also `Glow.cpp:89`'s "a 38-row overlay", which is already wrong today.

- [ ] **Step 5: Run to verify it passes, then commit**

```bash
ctest --test-dir build --output-on-failure
git add host/vcv/src/flow_patch_bridge.hpp host/vcv/src/Glow.cpp tests/
git commit -m "feat(vcv): the bridge carries PACE, COMP_A and GRIT

Four rows that had no destination now have one. COMP_A needs two edits,
not one: the veto band confines it to 0.40-0.60, so it takes the same
REWRITTEN AT RUNTIME tag COMP_B already carries. The BALANCE loss the
deleted note also recorded moves to a note of its own.

The report's hand-counted prose is corrected; nothing tests its
arithmetic, so it would have rotted silently.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 10: Glow's panel and label

**Files:**
- Modify: `host/vcv/res/gen_flow_panel.py:233,238`, `host/vcv/src/Glow.cpp:46,74,1357-1365`
- Regenerate: `host/vcv/src/generated_flow_panel.hpp`, `res/Glow.svg`
- Modify: `host/vcv/res/test_flow_panel.py:40,48,79,84`, `host/vcv/README.md:528,541`
- Modify: `docs/superpowers/specs/2026-08-11-glow-touch-2-panel-design.md:212`

- [ ] **Step 1: Rename in the generator and the module**

`_MACRO_NAMES` and the tip text; `Glow.cpp`'s `static_assert` and name table. The tip becomes `"PACE -- stretched to rhythmic  [S34]"`.

- [ ] **Step 2: Regenerate and run the guards**

```bash
cd host/vcv && python3 res/gen_flow_panel.py
python3 -m pytest res/test_flow_panel.py -q
```

`res/Glow.svg` must come back byte-identical — the macro knobs print no caption.

- [ ] **Step 3: Skip PACE in the Workshop's "Reroll one macro" submenu**

Spec §4.4. PACE has no story, so `new_partial(1u << M_PACE)` does nothing to PACE and instead redraws the whole weather layer, because the weather counter is the sum of all six macro counters (`terrain.h:47-51`). An entry that does something other than what its label says gets left out.

In `Glow.cpp`'s `createSubmenuItem("Reroll one macro", ...)` loop:

```cpp
for (int i = 0; i < spky::flow::MACRO_COUNT; ++i) {
    // PACE owns no story -- rerolling it cannot change PACE, and would
    // only redraw every other macro's weather (the weather counter is
    // the sum of all six). A menu entry that does something other than
    // its label says is worse than a missing one. Spec 2026-08-12 §4.4.
    if (i == spky::flow::M_PACE) continue;
    sub->addChild(createMenuItem(kMacroNames[i], "", [m, i]() {
        m->uiMask = uint8_t(1u << i);
        m->uiOp = Glow::UiOp::NEW_PARTIAL;
    }));
}
```

The pad path (`Glow.cpp:1028`, `new_partial(0x3F)`) is deliberately **not** touched: it rerolls all six anyway, so the weather is redrawn there whether PACE is in the mask or not.

No test guards this — `test_flow_panel.py` reads the generator, not `Glow.cpp`, and the menu is Rack-widget code the headless doctest suite cannot reach. Verify by eye in Rack: the submenu lists five macros, and PACE is not among them.

- [ ] **Step 4: Update the live hardware assignment**

`docs/superpowers/specs/2026-08-11-glow-touch-2-panel-design.md:212` assigns DIRT to trim knob `S34` on the shipping Touch-2 panel. That is a live assignment, not history — update it. The older flow-machine and taste-structure specs stay as they are, per the repo's convention on finished decisions.

- [ ] **Step 5: Build the plugin and commit**

```bash
./build-local.sh
git add host/vcv docs/superpowers/specs/2026-08-11-glow-touch-2-panel-design.md
git commit -m "feat(vcv): Glow's macro 3 is PACE

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 11: The Fireflow panel — one atomic commit

`gen_hw_panel.py:166` runs `place()` over every param at *module scope* and `place()` ends in `raise KeyError`. The instant PACE enters `PARAMS` without a `CENTER_POS` entry, `import gen_hw_panel` throws and `res/test_hw_panel.py` dies at import, taking the `hw_panel_guard` ctest with it. Both edits land together.

**Files:**
- Modify: `host/vcv/res/gen_panel.py` (`APPENDED_PANEL_PARAMS`, `INIT_DEFAULTS`), `gen_hw_panel.py` (`CENTER_POS`, `HW_SIZE`)
- Regenerate: `res/Fireflow.svg`, `res/FireflowHW.svg`, `src/generated_panel.hpp`, `src/generated_hw_panel.hpp`
- Modify: `host/vcv/src/Fireflow.cpp` (configParam, tooltip, `pushParams`), `src/init_patch.hpp`
- Modify: `bench/audition/init_patch.cpp`, `tests/test_seed_audition_init.cpp:74`

- [ ] **Step 1: Add the control to both generators, together**

`gen_panel.py`, in `APPENDED_PANEL_PARAMS` (appended so no existing param id shifts):

```python
    # PACE: the global modulation time-stretch (spec 2026-08-12). Takes the
    # ROW_TIME1 slot freed when SYNC folded into COUPLE. It belongs in TIMING
    # beside TEMPO, and next to TIDE on the hardware grid -- those two are the
    # pair this control came out of confusing: TIDE is a ratio, PACE is speed.
    Ctl("PACE", SMKNOB, CX - 9.0, ROW_TIME1, "PACE"),
```

and `INIT_DEFAULTS["PACE"] = 0.5` — **exactly 0.5**. `res/test_panel.py:2619-2622` requires an entry for every `PARAMS` member, and this number is the factory sound: any other value reboots the approved `FF_hw_Init.vcvm` instrument at a different speed.

`gen_hw_panel.py`: `HW_SIZE["PACE"] = "S"` and

```python
    # PACE under TEMPO, left of TIDE. The G row is already used for a control
    # hanging under its K-row column head (MOD under SHAPE, DENSITY under
    # RANGE), so this is the panel's own construction rather than a new one.
    # Clearances measured, not estimated: 13.0 mm to TIDE against an 11.0 mm
    # radius sum -- more generous than the 12.5 mm pitch of the row above.
    # A "G" glyph does NOT fit here: it needs 13.5 mm and gets 13.0.
    "PACE": (127.4, Y_B1G),
```

- [ ] **Step 2: Regenerate and run every panel guard**

```bash
cd host/vcv
python3 res/gen_panel.py && python3 res/gen_hw_panel.py
python3 -m pytest res/test_panel.py res/test_hw_panel.py -q
```

Expected: PASS. If `test_labels_stay_off_neighbour_footprints` fails, the position is wrong — do not widen `LBL_MARGIN`.

- [ ] **Step 3: Wire the module**

`Fireflow.cpp`: `configParam(PACE, 0.f, 1.f, initParamDefault(PACE), "Pace")` with a `PaceQuantity` that prints `pace_mult` host-side (`"x1/5.7"`), `inst.set_pace(params[PACE].getValue())` in `pushParams`, and the RATE tooltip multiplied by the pace so it keeps the promise at `divisions.h:55-56`. `FluxRateQuantity` gains a note that its division name no longer refers to the stretched grid.

- [ ] **Step 4: Mirror it in the bench firmware**

`bench/audition/init_patch.cpp` needs `inst.set_pace(value(PACE));`. **No test catches a miss here** — `test_panel.py`'s scraper guards only three named calls — so verify by reading the file, not by running the suite.

- [ ] **Step 5: Bump `NUM_PARAMS`**

`tests/test_seed_audition_init.cpp:74`: 68 → 69. Its own comment says to update the literal "and nothing else".

- [ ] **Step 6: Build, test, commit**

```bash
cd host/vcv && ./build-local.sh && cd ../..
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure
git add host/vcv bench/audition/init_patch.cpp tests/test_seed_audition_init.cpp
git commit -m "$(cat <<'EOF'
feat(vcv): PACE joins the panel, beside TEMPO on both grids

VCV: the ROW_TIME1 slot freed when SYNC folded into COUPLE. Hardware:
under TEMPO and left of TIDE, following the panel's own construction of
hanging a G-row control under its K-row column head. Clearances measured
against the real geometry -- 13.0 mm to TIDE, more generous than the
12.5 mm pitch of the row above.

Both generator edits are one commit by necessity: gen_hw_panel.py runs
place() at module scope and raises KeyError without a slot, which kills
test_hw_panel.py at import and takes hw_panel_guard with it.

INIT_DEFAULTS["PACE"] is exactly 0.5, and so is the audition firmware's
push. Zero would mean x1/32.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
)"
```

---

### Task 12: The render host verb and a CPU bench row

**Files:**
- Modify: `host/render/scenario.cpp` (setter chain), `host/render/scenario.h`
- Create: `host/render/scenarios/flow_pace_sweep.json`
- Modify: `bench/workloads_instr.cpp`

- [ ] **Step 1: Add the verb**

In `scenario.cpp`'s setter chain beside `set_tide` (`:171`): `else if (a == "set_pace") inst.set_pace(e.value);`. Note that unknown actions are **silently ignored** (`:220`), so a scenario written before this lands is a no-op rather than an error — add the verb before writing the scenario.

- [ ] **Step 2: A listening scenario**

`flow_pace_sweep.json` wakes a flow terrain, holds every macro at 0.5, and sweeps PACE from 0.5 to 0.0 and back over 120 s. This is the artefact for the listening checks §3.3 requires on a BBD deck, a Sampler deck and the synth envelopes.

- [ ] **Step 3: A bench row for the double accumulator**

`-mfpu=fpv5-d16` means these are hardware double adds, but they are roughly twice the latency of float and the melodic lane runs per sample. Add a row measuring `ModLane::process()` and verify it landed via `bench.map` — **not** via the memory table, per `spotykach-bench-stale-object-trap`. Commit the code before running, or `bench/run.py` labels the result with the wrong hash.

- [ ] **Step 4: Commit**

```bash
git add host/render bench/
git commit -m "feat(render): a set_pace verb and a PACE sweep scenario

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 13: The stale-comment sweep

Counts and invariants this work invalidates, none of them gated by a test.

**Files:**
- `docs/roadmap.md:2234` — "38 base-rule parameters … 25 story-owned"
- `CLAUDE.md:17` — "37 rows mapped + `P_ROOT` unreachable = 38" (untracked, already stale)
- `docs/roadmap.md` — add PACE to the living status, and note that `set_fx_on` (open item 2) now blocks `P_GRIT_A/B` as well as `P_FLUXMIX_A/B`

- [ ] **Step 1: Fix each, verifying the new number rather than computing it**

```bash
awk '/^inline const BaseRule kBaseRules\[\] = \{/,/^\};/' engine/flow/taste.h \
  | grep -E "^\s*\{" | grep -o "P_[A-Z_]*" | sort | wc -l
```

- [ ] **Step 2: Commit**

```bash
git add docs/ CLAUDE.md
git commit -m "docs: counts that moved with PACE

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Self-Review

**Spec coverage.** §2 → Task 2. §2.1 → Tasks 3 and 7. §3/§3.1 → Task 5. §3.2 → Task 4. §3.3 → Task 6 (FLUX) and Task 12 step 2 (the BBD/sampler/envelope listening checks). §4.1–4.6 → Task 8. §5 (the map) → Task 8 step 9. §6 (the seven carriers) → Task 5 step 3 (`_pace`), Task 8 step 7 (`_knob`), Task 9 (`FireflowPatch`), Task 11 steps 1 and 4 (`INIT_DEFAULTS`, bench). §7 → Tasks 10 and 11. §8 → the gates in each task. §8.1 → Task 3 step 5, Task 8 step 8, Task 11 step 5. §8.2 → Task 1. §8.3 → the RED steps in Tasks 1, 4, 5, 7. §9 ordering → task order, with the two atomic commits at Tasks 8 and 11.

**Known gaps, deliberate.** Task 12's listening checks produce a WAV, not a gate; they are for the owner, not for CI. Task 10 Step 3 has no automated guard either, for the reason stated there: it is Rack-widget code the headless suite cannot reach.

§4.4's reroll question is closed, not open. The owner ruled on 2026-08-12 that PACE is skipped in the Workshop submenu (Task 10, Step 3). The spec's earlier framing — a ruling on a gesture mark mask — was written against `engine/flow/gesture.h`, which has had no caller outside `tests/` since Glow's NEW button was removed on 2026-08-11; there is no hold-and-turn gesture left to rule on.

**Type consistency checked:** `pace_mult(float)->float` (Task 2) is consumed by `Instrument::set_pace` (Task 5) and the host tooltip (Task 11). `Transport::clock_pulse(float)` / `set_pace_anchor()` (Task 4) are called from `Instrument::clock_pulse` and `set_pace` (Task 5). `Flux::set_rhythm_pace(float)` (Task 6) is called from `_apply_tempo` (Task 5) — Task 5 must be amended by Task 6, which its step 3 states. `lane_wraps_for_test(int,int)` (Task 7) is used only in Task 7.
