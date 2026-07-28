# BODY Sustain Gain Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Level BODY's continuous (FLOW) excitation so it stops running 5–60 dB above its own struck level, by having each resonator declare the gain it applies to sustained excitation and compensating for it feed-forward.

**Architecture:** `KsString` and `ModeBank` each gain a `sustain_gain()` accessor, computed on the control tick they already use for coefficients — closed-form for the string, the largest single-mode peak gain for the bank. `BodyVoice` combines the two with the MATL power weights and scales the exciter's output by that combined gain raised to a negative exponent. In STEP the scale is exactly `1.0f`, so the struck path is bit-identical and `process()` needs no branch.

**Tech Stack:** C++17, doctest, CMake + Ninja (desktop), `daisysp::OnePole` / `SvfBp` primitives already in the tree.

**Spec:** `docs/superpowers/specs/2026-07-28-body-sustain-gain-design.md`. Read it once before Task 1; it carries the measurements this plan does not repeat.

**One deliberate deviation from the spec's §6 table.** Its last row asks for a render scenario proving a FLOW patch at MATL 1 and long DECAY stays below full scale. That claim is instead carried by `CHECK(f < 1.f)` inside Task 3's band test, which asserts it at all 81 corners of the control space rather than at one point. No render scenario is added. A reviewer should read this as a substitution, not a gap.

## Global Constraints

- **Branch: `fix/body-flow-bow`**, already rebased onto `main`. It carries the bow pitch fix (`7e7fc99`) that this work ships with. Do NOT start from `main` — the §6 band test must exercise the repaired zone 0.
- **Build the desktop suite only via CMake + Ninja after `source env.sh`.** Never invoke the system `g++` directly; it is the ARM cross-compiler in this environment.
- **No value from `_apply_params()` or a `set_params()` may move to the per-sample path.** Every derivation added here runs on the 96-sample control tick. This is the parent spec's §4 claim and three separate primitives in this engine have had to be unwound for breaking it.
- **STEP must stay bit-identical.** `_exc_gain` is set to *exactly* `1.0f` when not sustaining — the literal, not a computed value that rounds to one.
- **The compensation may only attenuate.** Every declared gain is clamped to `[1, kSustainGainMax]` so no parameter setting can make the exciter louder than it is today.
- **`kSustainGainMax = 1000.f` (60 dB)** and **`kSustainComp = 0.82f`**. Both are tuning material; name them as such in a comment at the constant.
- **Naming: "sustain gain", `sustain_gain()`.** Not "Q" — the bank has many and the string has none in the textbook sense.
- **`damping` in this codebase is the DECAY amount: higher means a LONGER ring.** `KsString` raises its filter cutoff with it, `ModeBank` raises Q with it. Sustain gain therefore *rises* with `damping`. Do not "correct" a test that asserts this direction.
- **No checksum or byte-identity gates against stored files.** Bit-identity between two states of the same running build is a different thing and is used in Task 3.
- Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`

## File Structure

| file | responsibility after this plan |
|---|---|
| `engine/body/ks_string.h` | declares `sustain_gain()` and caches it; no new audio-path state |
| `engine/body/ks_string.cpp` | derives the loop magnitude in `set_params`, next to the `SetFrequency` call it shares inputs with |
| `engine/body/mode_bank.h` | declares `sustain_gain()` and caches it |
| `engine/body/mode_bank.cpp` | derives the largest mode peak at the end of `_recompute()` |
| `engine/body/body_voice.h` | holds `_exc_gain` and (under `SPKY_TESTING`) exposes it |
| `engine/body/body_voice.cpp` | combines the two gains in `_apply_params`, applies `_exc_gain` to the exciter in `process()` |
| `tests/test_ks_string.cpp` | closed form against a measured steady state |
| `tests/test_mode_bank.cpp` | bounds and direction of the bank's gain |
| `tests/test_body_engine.cpp` | the FLOW-to-STEP band, and the STEP bit-identity guarantee |
| `tests/test_instrument.cpp` | the cross-deck test repaired on its own terms (Task 4) |

---

### Task 1: `KsString::sustain_gain()`

**Files:**
- Modify: `engine/body/ks_string.h`
- Modify: `engine/body/ks_string.cpp` (inside `set_params`, after `_iir_damping.SetFrequency(damping_f)`)
- Test: `tests/test_ks_string.cpp`

**Interfaces:**
- Produces: `float KsString::sustain_gain() const` — amplitude gain from a sustained drive at the string's own fundamental to the steady state, clamped to `[1, 1000]`. Task 3 consumes it.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_ks_string.cpp`:

```cpp
// A sine at the string's own fundamental is exactly the drive the closed form
// models, so the measured steady-state peak is the number sustain_gain() is
// claiming. Tolerance 1.5 dB covers the DC blocker and the dispersion allpass,
// which the derivation treats as unit magnitude.
TEST_CASE("ks_string: sustain_gain matches the measured steady state") {
    for (float hz : {110.f, 220.f, 660.f}) {
        for (float damp : {0.2f, 0.5f, 0.8f}) {
            CAPTURE(hz);
            CAPTURE(damp);
            KsString s;
            s.init(48000.f, 7);
            s.set_params(hz, 0.5f, damp, 0.f);

            const float claimed = s.sustain_gain();
            REQUIRE(claimed >= 1.f);

            const float inc = hz / 48000.f;
            float phase = 0.f, peak = 0.f;
            for (int i = 0; i < 6 * 48000; ++i) {
                phase += inc;
                if (phase >= 1.f) phase -= 1.f;
                const float out = s.process(std::sin(6.2831853f * phase));
                if (i >= 5 * 48000) {
                    const float m = std::fabs(out);
                    if (m > peak) peak = m;
                }
            }
            const float err_db = 20.f * std::log10(peak / claimed);
            CAPTURE(err_db);
            CHECK(std::fabs(err_db) < 1.5f);
        }
    }
}
```

- [ ] **Step 2: Run it and watch it fail to compile**

Run: `source env.sh && cmake --build build --target spky_tests -j 8`
Expected: `error: no member named 'sustain_gain' in 'spky::KsString'`

- [ ] **Step 3: Declare the accessor and its cache**

In `engine/body/ks_string.h`, public section:

```cpp
    // Amplitude gain from sustained excitation to the steady state this
    // string settles at, at the pitch it is currently tuned to. 1 means
    // nothing accumulates. Clamped to kSustainGainMax so the deliberately
    // lossless loop at damping >= 0.95 stays finite. Control rate: cached by
    // set_params, never derived here.
    float sustain_gain() const { return _sustain_gain; }

    static constexpr float kSustainGainMax = 1000.f;   // 60 dB, TUNING MATERIAL
```

In the private section, beside the other cached scalars:

```cpp
    float _sustain_gain = 1.f;
```

- [ ] **Step 4: Derive it in `set_params`**

In `engine/body/ks_string.cpp`, immediately after the existing
`_iir_damping.SetFrequency(damping_f);`:

```cpp
    // Sustain gain. The loop is delay line -> damping filter -> dispersion
    // allpass -> back. The allpass has unit magnitude by construction and the
    // DC blocker sits at 1.6 Hz, so the loop's magnitude at the fundamental is
    // the damping filter's, and the steady state is 1 / (1 - |H(f0)|).
    //
    // _iir_damping is a daisysp::OnePole in TPT form with gd = tan(pi*fc) and
    // lp = (gd*in + state) * gi, i.e. H(z) = gd(1+z^-1) / ((1+gd) + (gd-1)z^-1).
    // Taking the magnitude and folding |1 + e^-jw|^2 = 2 + 2cos(w):
    //
    //   |H|^2 = 2 gd^2 (1 + cos w) / ((1+gd)^2 + (gd-1)^2 + 2(gd^2 - 1) cos w)
    //
    // which is exactly 1 at DC and exactly 0 at Nyquist -- both worth checking
    // by hand before touching this. One tanf, one cosf, one sqrtf per control
    // tick, next to the two powf this function already runs.
    {
        const float fc  = damping_f < 0.497f ? damping_f : 0.497f;   // OnePole's own clamp
        const float gd  = std::tan(kPi * fc);
        const float c   = std::cos(2.f * kPi * frequency);
        const float num = 2.f * gd * gd * (1.f + c);
        const float den = (1.f + gd) * (1.f + gd)
                        + (gd - 1.f) * (gd - 1.f)
                        + 2.f * (gd * gd - 1.f) * c;
        float h = (den > 0.f && num > 0.f) ? std::sqrt(num / den) : 0.f;

        // Clamp in |H| rather than in the quotient: 1 - 1/kSustainGainMax is
        // the largest loop magnitude the cap allows, and clamping here keeps
        // the division away from zero entirely.
        const float h_max = 1.f - 1.f / kSustainGainMax;
        if (h > h_max) h = h_max;
        if (h < 0.f)   h = 0.f;
        _sustain_gain = 1.f / (1.f - h);
    }
```

If `kPi` is not already defined in this translation unit, add it beside
`kOneTwelfth`: `constexpr float kPi = 3.14159265358979f;`

- [ ] **Step 5: Run the test**

Run: `source env.sh && cmake --build build --target spky_tests -j 8 && ./build/spky_tests -tc="ks_string: sustain_gain*"`
Expected: PASS, all nine combinations.

If a combination misses by more than 1.5 dB, do NOT widen the tolerance — report it. The likely causes are the dispersion allpass being non-unity (it should not be) or `frequency` being the clamped rather than the requested value, and both are worth knowing.

- [ ] **Step 6: Pin the cap and the direction**

```cpp
TEST_CASE("ks_string: sustain_gain is bounded and rises with damping") {
    // NOTE the sign. In this codebase `damping` is the DECAY amount: higher
    // means a longer ring, because damping_cutoff rises with it and a higher
    // cutoff filters the loop less. Sustain gain therefore RISES with damping.
    KsString s;
    s.init(48000.f, 7);
    float prev = 0.f;
    for (float d : {0.f, 0.25f, 0.5f, 0.75f, 0.9f, 0.98f, 1.f}) {
        CAPTURE(d);
        s.set_params(220.f, 0.5f, d, 0.f);
        const float g = s.sustain_gain();
        CHECK(g >= 1.f);
        CHECK(g <= KsString::kSustainGainMax);
        CHECK(std::isfinite(g));
        CHECK(g >= prev);          // monotone, ties allowed at the cap
        prev = g;
    }
    // The infinite-decay crossfade must actually reach the cap, or the cap is
    // untested decoration. Brightness 1 as well as damping 1: damping_cutoff
    // is 12 + damping^2*60 + brightness*24, so both push the loop toward
    // lossless and only the pair reaches the ceiling reliably.
    s.set_params(220.f, 1.f, 1.f, 0.f);
    CHECK(s.sustain_gain() == doctest::Approx(KsString::kSustainGainMax));
}
```

Run: `./build/spky_tests -tc="ks_string: sustain_gain*"` — both cases pass.

- [ ] **Step 7: Full suite**

Run: `source env.sh && ctest --test-dir build --output-on-failure`
Expected: everything passes except `cross-deck excitation is symmetric and off by default`, which this branch already fails and Task 4 owns.

- [ ] **Step 8: Commit**

```bash
git add engine/body/ks_string.h engine/body/ks_string.cpp tests/test_ks_string.cpp
git commit -m "feat(body): KsString declares its sustain gain"
```

---

### Task 2: `ModeBank::sustain_gain()`

**Files:**
- Modify: `engine/body/mode_bank.h`
- Modify: `engine/body/mode_bank.cpp` (end of `_recompute()`)
- Test: `tests/test_mode_bank.cpp`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `float ModeBank::sustain_gain() const` — the largest single-mode peak gain, clamped to `[1, 1000]`. Task 3 consumes it.

**Why the largest and not the sum.** The bank is 24 band-passes and the excitation is broadband, so there is no single closed form. The compensation needs the worst case bounded, and a sum would over-compensate badly at high Q where the modes do not actually coincide. The spec records this as an approximation; do not present it as exact.

- [ ] **Step 1: Write the failing test**

Add to `tests/test_mode_bank.cpp`:

```cpp
TEST_CASE("mode_bank: sustain_gain bounds the loudest mode") {
    ModeBank b;
    b.init(48000.f);
    b.set_params(220.f, 0.f, 0.5f, 0.5f);

    const float g = b.sustain_gain();
    CHECK(std::isfinite(g));
    CHECK(g >= 1.f);
    CHECK(g <= ModeBank::kSustainGainMax);

    // It must bound every individual mode: a band-pass SVF peaks at its own Q,
    // so mode i can deliver |gain_i| * q_i and the declared value may not be
    // under that. mode_q is the observation accessor the bank already has.
    for (int i = 0; i < ModeBank::kModes; ++i) {
        CAPTURE(i);
        CHECK(g + 1e-3f >= b.mode_gain(i) * b.mode_q(i));
    }
}

TEST_CASE("mode_bank: sustain_gain rises with damping") {
    // Same sign note as ks_string: `damping` is DECAY, and ModeBank derives Q
    // from it as pow(2, damping * 79.7/12) -- higher damping, higher Q.
    ModeBank b;
    b.init(48000.f);
    float prev = 0.f;
    for (float d : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
        CAPTURE(d);
        b.set_params(220.f, 0.f, d, 0.5f);
        const float g = b.sustain_gain();
        CHECK(g >= prev);
        prev = g;
    }
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `source env.sh && cmake --build build --target spky_tests -j 8`
Expected: `no member named 'sustain_gain'` and `no member named 'mode_gain'`.

- [ ] **Step 3: Declare the accessors and the cache**

In `engine/body/mode_bank.h`, public section beside `mode_freq` / `mode_q`:

```cpp
    // Amplitude gain from sustained excitation to the steady state the bank
    // settles at: the largest single-mode peak. A band-pass SVF peaks at its
    // own Q, so mode i contributes |gain_i| * q_i. An APPROXIMATION -- the
    // bank has 24 modes and no single closed form -- chosen to bound the worst
    // case, which is what the compensation needs. Control rate.
    float sustain_gain() const { return _sustain_gain; }

    // Mode i's cached output gain. Observation only, like mode_freq/mode_q.
    float mode_gain(int i) const { return _gain[i / kBatch][i % kBatch]; }

    static constexpr float kSustainGainMax = 1000.f;   // 60 dB, TUNING MATERIAL
```

Private, beside `_updates`:

```cpp
    float _sustain_gain = 1.f;
```

- [ ] **Step 4: Derive it at the end of `_recompute()`**

After the loop that fills `_gain[b][s]` and pushes the SVF coefficients:

```cpp
    // Sustain gain (see the header). mode_q reads the coefficients the bank
    // actually pushed, so this cannot drift from production state the way a
    // re-derivation beside the ladder would.
    float peak = 1.f;
    for (int i = 0; i < kModes; ++i) {
        const float gi = _gain[i / kBatch][i % kBatch];
        const float m  = (gi < 0.f ? -gi : gi) * mode_q(i);
        if (m > peak) peak = m;
    }
    _sustain_gain = peak > kSustainGainMax ? kSustainGainMax : peak;
```

- [ ] **Step 5: Run the tests**

Run: `source env.sh && cmake --build build --target spky_tests -j 8 && ./build/spky_tests -tc="mode_bank: sustain_gain*"`
Expected: PASS.

- [ ] **Step 6: Full suite**

Run: `source env.sh && ctest --test-dir build --output-on-failure`
Expected: as in Task 1 — only the cross-deck test fails.

- [ ] **Step 7: Commit**

```bash
git add engine/body/mode_bank.h engine/body/mode_bank.cpp tests/test_mode_bank.cpp
git commit -m "feat(body): ModeBank declares its sustain gain"
```

---

### Task 3: `BodyVoice` compensates the exciter

**Files:**
- Modify: `engine/body/body_voice.h`
- Modify: `engine/body/body_voice.cpp` (`_apply_params`, `process`)
- Test: `tests/test_body_engine.cpp`

**Interfaces:**
- Consumes: `KsString::sustain_gain()` (Task 1), `ModeBank::sustain_gain()` (Task 2).
- Produces: no public API change. Under `SPKY_TESTING`, `float exc_gain_for_test() const`.

- [ ] **Step 1: Write the failing tests**

Add to `tests/test_body_engine.cpp`:

```cpp
// Peak of the settled second of a sustained note, or of a struck one.
static float body_mode_peak(bool flow, float matl, float dec, float reso,
                            float pitch) {
    BodyEngine e;
    e.set_seed(99);
    e.init(48000.f);
    e.set_sub(0.f);
    e.set_detune(0.f);
    e.set_cycle(4.f);
    e.set_decay(dec);
    e.set_resonance(reso);
    float t[LANE_COUNT] = { matl, 1.f, pitch, 0.f, 1.f };
    e.set_targets(t, 0.5f);
    e.set_flow(flow);
    e.trigger(pitch);

    float p = 0.f;
    const int n = flow ? 4 * 48000 : 2 * 48000;
    for (int i = 0; i < n; ++i) {
        float l = 0.f, r = 0.f;
        e.process(l, r);
        if (!flow || i >= 3 * 48000) {
            const float m = std::fabs(l);
            if (m > p) p = m;
        }
    }
    return p;
}

TEST_CASE("body: FLOW stays within a band of STEP across the control space") {
    // The finding this whole change exists for. Before compensation the same
    // sweep spanned +4.8 dB (strings, short decay) to +59.8 dB (mode bank,
    // long decay), peaking at 88.2 from ONE voice -- 39 dB over full scale,
    // before the FX chain and before the second deck.
    float lo = 1e9f, hi = -1e9f;
    for (float matl : {0.f, 0.5f, 1.f})
        for (float dec : {0.2f, 0.5f, 0.9f})
            for (float reso : {0.f, 0.5f, 1.f})
                for (float pitch : {0.f, 0.5f, 1.f}) {
                    CAPTURE(matl); CAPTURE(dec); CAPTURE(reso); CAPTURE(pitch);
                    const float f = body_mode_peak(true,  matl, dec, reso, pitch);
                    const float s = body_mode_peak(false, matl, dec, reso, pitch);
                    REQUIRE(s > 1e-6f);
                    const float rel = 20.f * std::log10(f / s);
                    CAPTURE(rel);
                    if (rel < lo) lo = rel;
                    if (rel > hi) hi = rel;
                    CHECK(f < 1.f);        // one voice must not exceed full scale
                }
    CAPTURE(lo);
    CAPTURE(hi);
    CHECK(hi - lo < 14.f);     // the spread the user asked to be compressed to
    CHECK(hi < 18.f);          // and no corner may sit far above the strike
}

TEST_CASE("body: STEP is untouched by the sustain compensation") {
    // _exc_gain is exactly 1.0f outside FLOW, and multiplication by 1.0f is
    // exact in IEEE 754 -- so a struck note must be bit-identical whether or
    // not the voice has previously been in FLOW and computed a compensation.
    BodyEngine a, b;
    for (BodyEngine* e : { &a, &b }) {
        e->set_seed(99);
        e->init(48000.f);
        e->set_sub(0.f);
        e->set_detune(0.f);
        e->set_cycle(4.f);
        float t[LANE_COUNT] = { 1.f, 1.f, 0.5f, 0.f, 1.f };  // MATL 1: worst case
        e->set_targets(t, 0.5f);
    }
    b.set_flow(true);                 // b takes a detour through FLOW ...
    for (int i = 0; i < 4800; ++i) { float l, r; b.process(l, r); }
    b.set_flow(false);                // ... and comes back
    for (int i = 0; i < 4800; ++i) { float l, r; b.process(l, r); }

    a.trigger(0.5f);
    b.trigger(0.5f);
    for (int i = 0; i < 48000; ++i) {
        float la = 0.f, ra = 0.f, lb = 0.f, rb = 0.f;
        a.process(la, ra);
        b.process(lb, rb);
        REQUIRE(la == lb);
        REQUIRE(ra == rb);
    }
}
```

- [ ] **Step 2: Run and watch them fail**

Run: `source env.sh && cmake --build build --target spky_tests -j 8 && ./build/spky_tests -tc="body: FLOW stays*,body: STEP is untouched*"`
Expected: the band test FAILS with `hi` around +60 and at least one `f < 1.f` violated. The STEP test may already pass — that is fine, it is a regression guard for Step 4.

- [ ] **Step 3: Add the constants and the member**

In `engine/body/body_voice.cpp`, in the anonymous namespace beside `kBrightTiltDb`:

```cpp
// Sustain compensation. A struck resonator decays; a continuously driven one
// accumulates until dissipation balances the input, and BODY's structure gain
// spans three orders of magnitude across MATL and DECAY. The exciter is scaled
// by the combined sustain gain to this negative power.
//
// The exponent IS the design: 0 leaves the level alone, 1 flattens it
// completely, and in between the dB range compresses proportionally. 0.82
// turns the measured 55 dB into about 10, which is the compression the user
// chose -- a bowed bell stays louder than a bowed damped string, because that
// difference is expression. TUNING MATERIAL, and the only number a listening
// pass has to turn.
constexpr float kSustainComp = 0.82f;
```

In `engine/body/body_voice.h`, private:

```cpp
    // Exciter scale from the resonators' declared sustain gain. EXACTLY 1.0f
    // outside FLOW -- see _apply_params.
    float _exc_gain = 1.f;
```

Do NOT add a `SPKY_TESTING` accessor for it. The Step 1 bit-identity test
covers the `1.0f` guarantee through observable behaviour, and an accessor no
test reads is cruft.

- [ ] **Step 4: Combine and apply**

At the end of `BodyVoice::_apply_params()`, after the `set_params` calls that
refresh both resonators (their gains must be current):

```cpp
    // Sustain compensation (see kSustainComp). The two structures are summed
    // by an equal-power blend, so their gains combine by POWER -- the squares
    // of the same mix factors process() uses.
    //
    // _str_a alone stands for the string leg: A and B differ only by DETUNE's
    // spread, at most 70 cents, which moves the gain far less than the
    // compression tolerance. One pow instead of two, and the assumption is
    // stated here so it can be argued with.
    //
    // STEP gets the literal 1.0f, not a computed value that rounds to it.
    // Multiplication by 1.0f is exact in IEEE 754, so the struck path stays
    // bit-identical and process() needs no branch.
    if (_sustaining) {
        const float g = _mix_string * _mix_string * _str_a.sustain_gain()
                      + _mix_modal  * _mix_modal  * _bank.sustain_gain();
        _exc_gain = std::pow(g < 1.f ? 1.f : g, -kSustainComp);
    } else {
        _exc_gain = 1.f;
    }
```

In `BodyVoice::process()`, the drive line becomes:

```cpp
    // SUB = 0 hard-gates the bus: bit-exact off (spec §6). The bus is NOT
    // compensated -- it is a level the player sets, and driving the body into
    // self-oscillation through it is what spec §6 intends and bounds.
    const float drive = _exciter.process() * _exc_gain
                      + (_sub > 0.f ? _excitation * _sub * _sub * 0.5f : 0.f);
```

- [ ] **Step 5: Run the tests**

Run: `source env.sh && cmake --build build --target spky_tests -j 8 && ./build/spky_tests -tc="body: FLOW stays*,body: STEP is untouched*"`
Expected: both PASS.

If the band is close but outside, report the measured `lo`/`hi` — do not adjust
`kSustainComp` to make a test pass. The exponent is the user's listening knob,
and a band test that was fitted to the constant tests nothing.

- [ ] **Step 6: Confirm the control-tick claim still holds**

The existing case `_apply_params()` runs once per control tick must still pass;
`std::pow` was added to a function that already runs two of them, but a
misplaced call would show up here.

Run: `./build/spky_tests -tc="*apply_params*,*control tick*"`
Expected: PASS.

- [ ] **Step 7: Full suite**

Run: `source env.sh && ctest --test-dir build --output-on-failure`
Expected: only the cross-deck test may still fail. Record whether it now passes
— Task 4 needs to know.

- [ ] **Step 8: Commit**

```bash
git add engine/body/body_voice.h engine/body/body_voice.cpp tests/test_body_engine.cpp
git commit -m "feat(body): compensate continuous excitation by the resonators' sustain gain"
```

---

### Task 4: restore the branch to a green suite

**Files:**
- Modify: `tests/test_instrument.cpp` (`cross-deck excitation is symmetric and off by default`)

**Context:** this test has failed on `fix/body-flow-bow` since the bow pitch fix. It measures deck B's energy in a window, mutes deck A, then measures a LATER window of the SAME instrument and requires the second to be smaller. That comparison assumes B's own excitation is steady between the two windows. Once BODY's bow sustains — which the pitch fix made it do properly — B is still building energy across both windows and the later one can be larger for reasons that have nothing to do with deck A.

- [ ] **Step 1: Determine whether Task 3 already fixed it**

Run: `source env.sh && ./build/spky_tests -tc="cross-deck excitation*"`

If it PASSES: the compensation restored the premise. Skip to Step 4, commit
nothing, and record in the report that no repair was needed.

If it FAILS: continue. The structural weakness is real either way, but do not
rewrite a passing test.

- [ ] **Step 2: Replace the time-window comparison with a lockstep control**

The instrument named `quiet` is already stepped in lockstep and already has the
cross-deck source off. Compare against it in the SAME window instead of against
an earlier window of `coupled`:

```cpp
    // Silencing A must pull B's energy back toward the uncoupled instrument.
    // Comparing against `quiet` in the same window rather than against an
    // earlier window of `coupled` removes the assumption that B's own
    // excitation is steady over time -- which a sustained BODY bow breaks,
    // because it is still building while both windows are measured.
    coupled.set_target_active(PART_A, LANE_LEVEL, false);
    coupled.set_target_base(PART_A, LANE_LEVEL, 0.f);
    for (int i = 0; i < 24000; ++i) {           // let A's level smoother settle
        quiet.process(nullptr, nullptr, &l, &r, 1);
        coupled.process(nullptr, nullptr, &l, &r, 1);
    }
    double e_muted_quiet = 0.0, e_muted_coupled = 0.0;
    for (int i = 0; i < 24000; ++i) {
        quiet.process(nullptr, nullptr, &l, &r, 1);
        const float qe = quiet.voice_env(PART_B, 0);
        e_muted_quiet += (double)qe * qe;

        coupled.process(nullptr, nullptr, &l, &r, 1);
        const float ce = coupled.voice_env(PART_B, 0);
        e_muted_coupled += (double)ce * ce;
    }
    // With A silent the two instruments differ only by a source that now
    // carries nothing, so the gap must have closed to a fraction of what it
    // was while A was playing.
    const double gap_playing = e_coupled - e_quiet;
    const double gap_muted   = e_muted_coupled - e_muted_quiet;
    CHECK(gap_muted < gap_playing * 0.5);
```

- [ ] **Step 3: Run it**

Run: `./build/spky_tests -tc="cross-deck excitation*"`
Expected: PASS.

If it still fails, STOP and report the two gap values rather than loosening the
factor. A gap that does not close means B is not listening to A, which is the
defect the original test was built to catch.

- [ ] **Step 4: Full suite, green**

Run: `source env.sh && ctest --test-dir build --output-on-failure`
Expected: 4/4 test targets pass, no failures.

- [ ] **Step 5: Commit**

```bash
git add tests/test_instrument.cpp
git commit -m "test: compare the cross-deck gap in one window, not across two"
```

---

## After the plan

The branch then carries the bow pitch fix and the sustain compensation
together, with a green suite. Build the VCV plugin (`host/vcv/build-local.sh
install`, never by hand) so the listening pass can judge `kSustainComp` — that
is the one number this design leaves open, and the ear decides it.
