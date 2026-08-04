# Signal-Path Declick Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the four measured defects from `docs/audits/2026-08-04-signal-path-bug-round.md` (SoftSwitch 48-kHz hardwiring, FLUX gating the wrong side, reverb clear-on-sleep tail amputation, COMP cap cliff) plus the one unsmoothed parameter (master DRIVE) — all without measurable CPU cost.

**Architecture:** Five independent, surgical fixes in `engine/`, each with its own RED test first. No block changes its steady-state sound; every fix only changes *transitions* (switch ramps, knob rides, sleep hand-offs). At 48 kHz with static knobs, every touched path stays bit-identical to today except where the audit names the defect.

**Tech Stack:** C++17 header/impl in `engine/`, doctest suite in `tests/`, desktop build via clang+Ninja.

## Global Constraints

- Work in an isolated worktree/branch (e.g. `signal-path-declick`) off `main`; baseline is 59e6dcd or later.
- Build & test, from the repo root, in Git Bash: `source env.sh && cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`. Single-suite runs: `./build/spky_tests.exe --test-case="<pattern>"`.
- **Never touch `src/core/**`** — those are the firmware originals; the Daisy runs fixed at 48 kHz where finding 1 cannot bite. All fixes live in `engine/`.
- No heap allocation, no `<vector>`/`<deque>` etc. in `engine/` code paths — the engine is RT-safe and firmware-portable. (Tests may use anything.)
- At 48 kHz with static parameters, behavior of touched blocks must remain bit-identical except for the audited transition defects. Existing tests must stay green — if one legitimately conflicts, stop and surface it, do not "fix" the test.
- By-ear constants stay by-ear: new tunables (`kRevReturnFadeS`, `kCapEngage`, DRIVE slew) get a named constant + "ear-tunable" comment, values as specified here.
- Commit trailer (always, instead of any Claude/Anthropic trailer):
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- Prove every RED once: run the new test before implementing and confirm the expected failure value, then implement.

---

### Task 1: SoftSwitch — rate-correct ramp bound + OOB guard

The rise/fall iterator bound is the constant `191`; `_kof` scales with the
sample rate. They agree only at 48 kHz. Below 48 kHz `hann_value_at` reads
past the 192-entry table (UB); above it the ramp stops partway and steps.

**Files:**
- Modify: `engine/fx/fx_util.h:27-34` (`hann_value_at`), `engine/fx/fx_util.h:66-116` (`SoftSwitch`)
- Test: `tests/test_fx_util.cpp`

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: a `SoftSwitch` whose ramp is ~4 ms and click-free at any `init()` rate. Task 2's FLUX fade rides on this.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_fx_util.cpp` (add `#include <algorithm>` at the top):

```cpp
static float max_rise_step(float sr) {
    SoftSwitch s;
    s.init(sr);
    s.process();                     // settle in idle
    s.set_on(true);
    float prev = 0.f, worst = 0.f;
    const int n = static_cast<int>(0.02f * sr);   // 20 ms >> the 4 ms ramp
    for (int i = 0; i < n; ++i) {
        const float v = s.process();
        worst = std::max(worst, v - prev);
        prev = v;
    }
    CHECK(prev == 1.f);              // the ramp must actually arrive at hold
    return worst;
}

TEST_CASE("softswitch: the 4 ms ramp is a ramp at 44.1/48/96 kHz") {
    CHECK(max_rise_step(48000.f) < 0.02f);
    CHECK(max_rise_step(96000.f) < 0.02f);   // today: 0.508 — ramp dies at 0.492, then snaps
    CHECK(max_rise_step(44100.f) < 0.02f);   // today: OOB table read, then a >0.27 snap
}
```

- [ ] **Step 2: Run it, verify the RED**

Run: `./build/spky_tests.exe --test-case="*44.1/48/96*"` (after `cmake --build build`)
Expected: FAIL — the 96 kHz check reports `worst` ≈ 0.508 (deterministic); 44.1 kHz also fails.

- [ ] **Step 3: Implement**

In `hann_value_at`, clamp the integer index (safety net, keeps the function total):

```cpp
inline float hann_value_at(float norm_pos) {
    const auto& curve = hann_curve();
    float pos = static_cast<float>(curve.size() - 1) * norm_pos;
    if (pos < 0.f) pos = 0.f;
    auto ipos = static_cast<size_t>(pos);
    if (ipos >= curve.size() - 1) return curve[curve.size() - 1];
    float frac = pos - static_cast<float>(ipos);
    return curve[ipos] + (curve[ipos + 1] - curve[ipos]) * frac;
}
```

In `SoftSwitch`, derive the iterator bound from the rate. `init()` becomes:

```cpp
    void init(float sample_rate) {
        _kof = 1.f / (0.004f * sample_rate);
        // The iterator bound must come from the same 4 ms the step size
        // comes from — a constant 191 is only right at 48 kHz (the OOB
        // read / half-ramp of the 2026-08-04 audit, finding 1).
        _end = static_cast<int32_t>(0.004f * sample_rate) - 1;
        if (_end < 1) _end = 1;
    }
```

Replace both hardwired `191`s in `process()` — `if (++_iterator >= 191)` becomes `if (++_iterator >= _end)`, and in `Stage::hold` `_iterator = 191;` becomes `_iterator = _end;`. Add the member next to `_iterator`:

```cpp
    int32_t _end = 191;   // pre-init() default matches the old 48 kHz constant
```

At 48 kHz `_end` is exactly 191 — bit-identical to today.

- [ ] **Step 4: Run the fx_util suite, verify green**

Run: `./build/spky_tests.exe --test-case="*softswitch*,*xfade*"`
Expected: all PASS, including the two pre-existing 48 kHz cases untouched.

- [ ] **Step 5: Full suite, then commit**

Run: `ctest --test-dir build --output-on-failure` — all green.

```bash
git add engine/fx/fx_util.h tests/test_fx_util.cpp
git commit -m "fix(fx): SoftSwitch ramp bound follows the sample rate

The rise/fall iterator ran to a constant 191 while the step size scaled
with the rate: below 48 kHz that read past the 192-entry Hann table
(UB, audible click on every FX/engine switch at 44.1 kHz), above it the
ramp stopped at 0.49 and snapped. Audit 2026-08-04, finding 1.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: FLUX — fade the return, not the send; flush the stale take

`Flux::process` scales the delay-line *input* by the switch ramp and adds the
*return* ungated, so switching off clicks (full-level echo cut in one sample
when `is_idle()` lands) and switching back on replays a frozen, arbitrarily
old take at full level. GRIT already does this right by crossfading its
output.

**Files:**
- Modify: `engine/fx/flux.cpp:155-182` (`Flux::process`), `engine/fx/flux.h` (one flag, one helper), `engine/fx/tape_echo.h` (add `TapeBpf::Reset`, `TapeEcho::Reset`)
- Test: `tests/test_flux.cpp`

**Interfaces:**
- Consumes: Task 1's rate-correct `SoftSwitch` (the fade this task moves to the return).
- Produces: `TapeEcho::Reset()` / `TapeBpf::Reset()` (public, reusable). `Flux`'s public API is unchanged.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_flux.cpp`:

```cpp
TEST_CASE("flux tape: switching off fades the echo instead of cutting it") {
    FluxTapeMem mem;
    Flux f;
    mem.init(f);
    f.set_bpm(120.f);
    f.set_rate(3);                    // 0.5 s line
    f.set_on(true, true);
    f.set_mix(1.f);
    f.set_feedback(0.5f);
    for (int i = 0; i < 48000; ++i) { // fill the line with a 200 Hz tone
        float s = 0.8f * std::sin(6.2831853f * 200.f * i / 48000.f);
        float l = s, r = s;
        f.process(l, r);
    }
    f.set_on(false);                  // ramped off, into silence
    float prev = 0.f, worst = 0.f;
    for (int i = 0; i < 400; ++i) {   // 4 ms ramp = 192 samples, plus margin
        float l = 0.f, r = 0.f;
        f.process(l, r);
        if (i > 0) worst = std::max(worst, std::fabs(l - prev));
        prev = l;
    }
    // A 200 Hz tone at 0.8 moves at most ~0.021/sample by itself; the
    // one-sample cut the audit measured is 0.476.
    CHECK(worst < 0.1f);
}

TEST_CASE("flux tape: re-enabling does not resurrect a stale take") {
    FluxTapeMem mem;
    Flux f;
    mem.init(f);
    f.set_bpm(120.f);
    f.set_rate(3);
    f.set_on(true, true);
    f.set_mix(1.f);
    f.set_feedback(0.5f);
    for (int i = 0; i < 48000; ++i) {
        float s = 0.8f * std::sin(6.2831853f * 200.f * i / 48000.f);
        float l = s, r = s;
        f.process(l, r);
    }
    f.set_on(false);
    run_silence(f, 480);              // let the fall finish, land in idle
    f.set_on(true);                   // wake into silence
    float peak = 0.f;
    for (int i = 0; i < 4000; ++i) {
        float l = 0.f, r = 0.f;
        f.process(l, r);
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
    }
    CHECK(peak < 1e-3f);              // today: first sample already -0.466
}
```

- [ ] **Step 2: Run them, verify the RED**

Run: `./build/spky_tests.exe --test-case="*switching off fades*,*stale take*"`
Expected: both FAIL — `worst` ≈ 0.48, `peak` ≈ 0.6.

- [ ] **Step 3: Implement**

`engine/fx/tape_echo.h` — give `TapeBpf` a state reset (after `Process`):

```cpp
    void Reset() { _s1 = _s2 = 0.f; }
```

and `TapeEcho` a full reset (after `Init`):

```cpp
    void Reset() {
        _line.Reset();
        _bpf.Reset();
    }
```

`engine/fx/flux.h` — add to the private section:

```cpp
    void flush_lines();

    bool _line_dirty = false;
```

`engine/fx/flux.cpp` — rewrite `process()` and add the helper:

```cpp
void Flux::flush_lines() {
    // One memset per switch-off (2 x 1 MB). Desktop-cheap; the Daisy shell
    // will need an amortized clear before this engine runs on hardware.
    _echo_l.Reset();
    _echo_r.Reset();
    _line_dirty = false;
}

void Flux::process(float& l, float& r) {
    if (!_buf_ok) return;
    const bool was_idle = _sw.is_idle();
    const float k = _sw.process();
    if (_sw.is_idle()) {
        // The fall just completed. PartFx stops calling us the moment
        // engaged() drops, so this transition sample is the last chance
        // to flush the stale take (audit finding 2: a frozen line used
        // to replay, full-level, on the next ON).
        if (!was_idle && _line_dirty) flush_lines();
        return;
    }
    // set_on(false, immediate) skips the fall entirely — catch it on wake.
    if (was_idle && _line_dirty) flush_lines();

    const bool thinning = _thin > 0.f && _rhy_valid;
    if (thinning) {
        _repeat_phase_samples += 1.f;
        if (_repeat_period_samples > 0.f &&
            _repeat_phase_samples >= _repeat_period_samples) {
            _repeat_phase_samples = 0.f;
            advance_gate();
        }
    }

    daisysp::fonepole(_dt_current, _dt_target, _dt_coef);
    const float samples = _dt_current * _sr;
    // The line always hears the live input; the RAMP rides the return —
    // the signal that is actually audible (GRIT's pattern, grit.cpp:99).
    // The old form gated the send, i.e. audio already in the past.
    float wet_l = _echo_l.Process(l, samples);
    float wet_r = _echo_r.Process(r, samples);
    _line_dirty = true;
    if (thinning || _gate != 1.f) {
        daisysp::fonepole(_gate, _gate_target, _gate_coef);
        if (std::fabs(_gate - 1.f) < 1e-4f) _gate = 1.f;
        wet_l *= _gate;
        wet_r *= _gate;
    }
    l += wet_l * _mix_lin * k;
    r += wet_r * _mix_lin * k;
}
```

- [ ] **Step 4: Run the flux suites, verify green**

Run: `./build/spky_tests.exe --test-case="*flux*"`
Expected: all PASS. The pre-existing cases use `set_on(true, true)` on a clean line, so nothing there may move.

- [ ] **Step 5: Full suite, then commit**

Run: `ctest --test-dir build --output-on-failure` — all green. `tests/test_part_fx.cpp` exercises the send-tap arithmetic around FLUX (`l - pre_flux_l`); that stays exact because process() still only *adds* to l/r.

```bash
git add engine/fx/flux.h engine/fx/flux.cpp engine/fx/tape_echo.h tests/test_flux.cpp
git commit -m "fix(flux): ramp the audible return, flush the line on switch-off

The 4 ms anti-click ramp gated the delay-line INPUT — audio already in
the past — and added the return ungated, so OFF cut a full-level echo in
one sample and ON resurrected a frozen take of arbitrary age. The ramp
now rides the return (GRIT's output-crossfade pattern) and the line is
cleared once the fall completes. Audit 2026-08-04, finding 2.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: Reverb — fade the return before clear-on-sleep

When both wet gains glide to exactly 0, `Instrument::process` clears the room
and stops calling it — but the wet gains ride the *send*; the return joins at
unity, and at that moment it is a tail seconds long (−10.5 dBFS measured at
DECAY 0.75 / MIX 0.5). Closing REVERB MIX therefore steps the tail to zero
~72 ms after the knob.

**Files:**
- Modify: `engine/instrument.cpp:279-293` (the `!_rev_asleep` block) + the anon-namespace constants near the top, `engine/instrument.h` (two members near `_rev_asleep`, line ~311)
- Test: `tests/test_instrument.cpp`

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: nothing consumed later. `reverb_asleep()` semantics shift: the flag now lands after an additional ~150 ms fade (tests that only check it *eventually* sleeps stay valid).

- [ ] **Step 1: Write the failing test**

Append to `tests/test_instrument.cpp`, below the `test_fx_mem()` fixture (it reuses `s_ti_tape`/`s_ti_reverb`; do NOT call `set_reverb_decay` here — that would arm the bloom duck and dirty the plain-vs-fx dry comparison):

```cpp
TEST_CASE("instrument: closing REVERB MIX fades the tail instead of cutting it") {
    Instrument plain;                  // dry twin: same seeds, no FX memory
    plain.init(48000.f);
    Instrument fx;
    fx.init(48000.f, test_fx_mem());
    fx.set_reverb_mix(0.6f);
    float pl, pr, fl, fr;
    for (int i = 0; i < 96000; ++i) {  // 2 s: fill the room
        plain.process(nullptr, nullptr, &pl, &pr, 1);
        fx.process(nullptr, nullptr, &fl, &fr, 1);
    }
    REQUIRE(!fx.reverb_asleep());
    fx.set_reverb_mix(0.f);
    // |fx - plain| is the wet residual: past the 10 ms dry-gain glide the
    // dry paths are identical (duck disarmed — set_reverb_decay never ran).
    float win[32] = {};
    int slept_at = -1;
    for (int i = 0; i < 96000; ++i) {
        plain.process(nullptr, nullptr, &pl, &pr, 1);
        fx.process(nullptr, nullptr, &fl, &fr, 1);
        win[i % 32] = std::max(std::fabs(fl - pl), std::fabs(fr - pr));
        if (fx.reverb_asleep()) { slept_at = i; break; }
    }
    REQUIRE(slept_at > 0);
    // 1) the room may only sleep after the ~150 ms return fade has run
    //    (today the clear lands ~66-76 ms after the knob: FAIL)
    CHECK(slept_at > static_cast<int>(0.15f * 48000.f));
    // 2) whatever tail was left at the clear had been faded to nothing
    float worst = 0.f;
    for (float v : win) worst = std::max(worst, v);
    CHECK(worst < 0.005f);
}
```

- [ ] **Step 2: Run it, verify the RED**

Run: `./build/spky_tests.exe --test-case="*closing REVERB MIX*"`
Expected: FAIL — `slept_at` ≈ 3200–3700 (< 7200). The level check may or may not also trip (tail phase); the timing check is the deterministic RED.

- [ ] **Step 3: Implement**

`engine/instrument.h` — next to `_rev_asleep` (~line 311):

```cpp
    // Return fade for clear-on-sleep: the wet gains ride the SEND, so the
    // moment they hit zero the room still holds a tail seconds long. Fade
    // the RETURN to zero first, then clear (audit 2026-08-04, finding 3).
    float _rev_return_gain = 1.f;
    float _rev_return_step = 0.f;
```

`engine/instrument.cpp` — in the anon namespace next to the duck constants (~line 36):

```cpp
constexpr float kRevReturnFadeS = 0.15f;  // ear-tunable: tail fade-out before the room sleeps
```

In `Instrument::init` (where the reverb/duck state is set up, near line 70):

```cpp
    _rev_return_gain = 1.f;
    _rev_return_step = 1.f / (kRevReturnFadeS * sample_rate);
```

Replace the body of the `if (!_rev_asleep) { ... }` block (instrument.cpp:279-293) with:

```cpp
            if (!_rev_asleep) {
                // Per-deck send: the equal-power wet curve (sin) rides the SEND
                // -- one shared room has only one return. MORPH fades the send
                // too (M4 rule): a morphed-away deck injects no new reverb.
                float wl, wr;
                _reverb->process(asl * ga * wga + bsl * gb * wgb,
                                 asr * ga * wga + bsr * gb * wgb, wl, wr);
                const bool closing = wga == 0.f && wgb == 0.f &&
                    _rev_wet_target[PART_A] == 0.f && _rev_wet_target[PART_B] == 0.f;
                if (closing) {
                    _rev_return_gain -= _rev_return_step;      // linear, exact zero
                    if (_rev_return_gain < 0.f) _rev_return_gain = 0.f;
                } else if (_rev_return_gain < 1.f) {
                    _rev_return_gain += _rev_return_step;      // reopened mid-fade
                    if (_rev_return_gain > 1.f) _rev_return_gain = 1.f;
                }
                l += wl * _rev_return_gain;  // wl already carries kWetGain; the return joins at unity
                r += wr * _rev_return_gain;
                if (closing && _rev_return_gain == 0.f) {
                    _reverb->clear();        // clear-on-sleep: waking starts empty
                    _rev_asleep = true;      // Oliverb CPU is off until a MIX reopens
                    _rev_return_gain = 1.f;  // room is empty; the next wake starts at unity
                }
            }
```

Also reset `_rev_return_gain = 1.f;` in the `!_rev_primed` snap branch (instrument.cpp:231-243), next to `_duck_gain = 1.f;`.

Cost: two multiplies and a branch per sample while the reverb runs, plus 150 ms of extra Oliverb before each sleep. Nothing while asleep.

- [ ] **Step 4: Run the instrument suites, verify green**

Run: `./build/spky_tests.exe --test-case="*instrument*"`
Expected: all PASS. Watch the two M4 morph-isolation cases and "all FX off + send 0" (they never enter the closing path with a filled room, so they must not move).

- [ ] **Step 5: Full suite, then commit**

Run: `ctest --test-dir build --output-on-failure` — all green.

```bash
git add engine/instrument.h engine/instrument.cpp tests/test_instrument.cpp
git commit -m "fix(reverb): fade the return before clear-on-sleep

The per-deck wet gains ride the send; the return joined at unity, so
the instant both gains hit zero the clear amputated a tail measured at
-10.5 dBFS. A 150 ms linear return fade now runs first, then the room
clears and sleeps exactly as before. Audit 2026-08-04, finding 3.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: COMP — the ceiling fades in with the knob; disengage glides out

The −8 dBFS envelope ceiling (`kEnvCeiling`) is applied unconditionally, so
knob 0.001 already takes −5 dB (the whole rest of the travel adds 0.8 dB),
and the disengage branch snaps `_gain` to 1.0 in one sample (+5.3 dB click).

**Files:**
- Modify: `engine/fx/comp.cpp` (`compute_gain`, `Comp::process`, one new constant)
- Test: `tests/test_comp.cpp`

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: nothing consumed later. At `_curve_amount >= 0.15` the cap law is bit-identical to today — the by-ear init defaults (0.630 / 0.561) are untouched.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_comp.cpp` (reuse its existing includes/helpers; add `#include <algorithm>` / `<cmath>` if missing):

```cpp
TEST_CASE("comp: knob 0.001 is transparent, not a -5 dB cliff") {
    Comp c;
    c.init(48000.f);
    c.set_amount(0.001f);
    float peak = 0.f;
    for (int i = 0; i < 96000; ++i) {
        const float s = 0.8f * std::sin(6.2831853f * 220.f * i / 48000.f);
        float l = s, r = s;
        c.process(l, r);
        if (i > 48000) peak = std::max(peak, std::fabs(l));   // steady state only
    }
    CHECK(peak > 0.75f);   // today: 0.4505 (-5 dB) — the unscaled env ceiling
}

TEST_CASE("comp: riding the knob to zero glides out, no gain snap") {
    Comp c;
    c.init(48000.f);
    c.set_amount(0.5f);
    int n = 0;
    auto tick = [&]() {
        const float s = 0.8f * std::sin(6.2831853f * 220.f * n++ / 48000.f);
        float l = s, r = s;
        c.process(l, r);
        return c.gain_db();
    };
    float g = 0.f;
    for (int i = 0; i < 48000; ++i) g = tick();   // settle at 0.5
    float prev = g, worst = 0.f;
    for (int i = 0; i < 96000; ++i) {             // ride 0.5 -> 0 over 0.5 s, then hold
        const float a = 0.5f * std::max(0.f, 1.f - static_cast<float>(i) / 24000.f);
        c.set_amount(a);
        g = tick();
        worst = std::max(worst, std::fabs(g - prev));
        prev = g;
    }
    CHECK(worst < 0.5f);   // dB per sample; today the disengage snap is ~+5.3 dB in one sample
}
```

- [ ] **Step 2: Run them, verify the RED**

Run: `./build/spky_tests.exe --test-case="*-5 dB cliff*,*no gain snap*"`
Expected: both FAIL — `peak` ≈ 0.45; `worst` ≈ 5.3 dB.

- [ ] **Step 3: Implement**

`engine/fx/comp.cpp` — add to the anon namespace next to `kEnvCeiling`:

```cpp
// The env ceiling belongs to the compressor, not to knob zero: it fades in
// over the first kCapEngage of (smoothed) travel, so amount -> 0 ramps to
// true bypass instead of the -5 dB cliff at 0.001 (audit 2026-08-04,
// finding 4). At and above kCapEngage the cap law is exactly the old one —
// the by-ear defaults (0.630 / 0.561) sit far past it, untouched.
constexpr float kCapEngage = 0.15f;   // ear-tunable
```

In `compute_gain()`, replace the two cap lines (comp.cpp:71-72) with:

```cpp
    const float relax = std::min(1.f, _curve_amount * (1.f / kCapEngage));
    if (relax > 0.f) {
        const float cap = kEnvCeiling / (std::max(_env, 1e-6f) * relax);
        if (_gain_target > cap) _gain_target = cap;
    }
```

In `Comp::process()`, replace the disengage branch (comp.cpp:76-83) with:

```cpp
    if (!engaged()) {
        if (_gain != 1.f) {                          // glide out, then re-arm
            _gain += _gain_coef * (1.f - _gain);     // ~2 ms, the recovery coef
            // fabs, not (1 - gain): makeup can leave _gain ABOVE 1 on quiet
            // material, and the glide has to converge from both sides
            if (std::fabs(1.f - _gain) < 1e-4f) {
                _gain = _gain_target = 1.f;
                _env = 0.f;
                _ctr = 0;
                return;                              // bit-exact from here on
            }
            l *= _gain;
            r *= _gain;
            return;
        }
        return;                                      // bit-exact bypass
    }
```

- [ ] **Step 4: Run the comp suite, verify green**

Run: `./build/spky_tests.exe --test-case="*comp*"`
Expected: all PASS. Pre-existing cases at amounts ≥ 0.15 must be bit-identical (relax == 1 there). If an existing case pins behavior at an amount below 0.15, stop and surface it before touching it.

- [ ] **Step 5: Full suite, then commit**

Run: `ctest --test-dir build --output-on-failure` — all green.

```bash
git add engine/fx/comp.cpp tests/test_comp.cpp
git commit -m "fix(comp): env ceiling fades in with the knob; disengage glides out

The post-comp ceiling was applied unconditionally, so knob 0.001 took
-5 dB (the remaining travel adds 0.8 dB) and the bottom stop snapped
gain to 1.0 in one sample. The cap now fades in over the first 0.15 of
smoothed travel — bit-identical at and above that, so the by-ear
defaults are untouched — and disengage glides out over ~2 ms.
Audit 2026-08-04, finding 4.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: Master DRIVE — smooth `_pre` inside the limiter

DRIVE is the one parameter reaching the audio path unsmoothed: the VCV shell
pushes it on a 16-sample raster and `set_drive` writes `_pre` (1…4) directly,
so a knob sweep steps the master gain every 16 samples.

**Files:**
- Modify: `engine/fx/limiter.h`
- Test: `tests/test_limiter.cpp`

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: `pre_gain()` now returns the *target* (so `test_limiter.cpp:31`'s immediate `Approx(4.f)` check stays green); the internal `_pre` glides.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_limiter.cpp`:

```cpp
TEST_CASE("limiter: a DRIVE step glides instead of stepping the master gain") {
    Limiter lim;
    lim.init();
    float worst = 0.f, prevv = 0.5f;
    for (int i = 0; i < 48000; ++i) {
        if (i == 4800) lim.set_drive(1.f);   // worst-case knob step: 0 -> 1
        float l = 0.5f, r = 0.5f;            // DC probe: any output step IS the artefact
        lim.process(l, r);
        if (i > 0) worst = std::max(worst, std::fabs(l - prevv));
        prevv = l;
    }
    CHECK(worst < 0.02f);   // today: 0.5 -> ~0.998 in ONE sample at i=4800
}
```

(Add `#include <algorithm>` at the top if not present.)

- [ ] **Step 2: Run it, verify the RED**

Run: `./build/spky_tests.exe --test-case="*DRIVE step glides*"`
Expected: FAIL — `worst` ≈ 0.5 (the one-sample jump into the driven knee).

- [ ] **Step 3: Implement**

`engine/fx/limiter.h`:

```cpp
    void init() {
        _peak = 0.5f;
        _pre = 1.f;
        _pre_target = 1.f;
    }
    void set_drive(float n) {
        const float c = std::clamp(n, 0.f, 1.f);
        _pre_target = 1.f + 3.f * c * c;
    }
    float pre_gain() const { return _pre_target; }
```

At the top of `process()`, before `pl`/`pr` are computed:

```cpp
        // DRIVE was the one knob reaching the audio path unsmoothed (the
        // VCV shell pushes it on a 16-sample raster; audit 2026-08-04,
        // observations). Glide _pre over ~10 ms at 48 kHz — a fixed
        // coefficient, this is a knob smoother, not a tuning — and snap on
        // convergence so the drive-0 bit-transparency branch stays exact.
        if (_pre != _pre_target) {
            _pre += 0.002f * (_pre_target - _pre);
            if (std::fabs(_pre_target - _pre) < 1e-4f) _pre = _pre_target;
        }
```

Add the member next to `_pre`:

```cpp
    float _pre_target = 1.f;
```

Everything downstream (`drive` readback, knee morph, transparency check) already reads `_pre` and follows the glide automatically.

- [ ] **Step 4: Run the limiter suite, verify green**

Run: `./build/spky_tests.exe --test-case="*limiter*"`
Expected: all PASS — "bit-transparent below the knee at drive 0" (`_pre` stays exactly 1), "never exceeds 1.0" (ceiling logic untouched), "stereo-linked" (its assertions start after 4800 samples; `_pre` converges in ~10 ms ≪ that), and the new glide case.

- [ ] **Step 5: Full suite, then commit**

Run: `ctest --test-dir build --output-on-failure` — all green.

```bash
git add engine/fx/limiter.h tests/test_limiter.cpp
git commit -m "fix(limiter): glide DRIVE's pre-gain instead of stepping it

set_drive wrote _pre (1..4) directly on the shell's 16-sample raster —
the one unsmoothed parameter in the audio path. _pre now glides over
~10 ms with a convergence snap, so drive-0 stays bit-transparent;
pre_gain() reports the target. Audit 2026-08-04, observations.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: Wrap-up — full verification, VCV build, audit status

**Files:**
- Modify: `docs/audits/2026-08-04-signal-path-bug-round.md:4` (status line)

**Interfaces:**
- Consumes: all previous tasks committed.
- Produces: a branch ready for review/merge.

- [ ] **Step 1: Full desktop suite from a clean configure**

Run: `source env.sh && cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`
Expected: every test green.

- [ ] **Step 2: VCV plugin compiles against the changed engine headers**

Run: `cd host/vcv && ./build-local.sh` (ALWAYS this script — the system g++ is the ARM cross-compiler and hand-rolled builds fail with "MinGW not found").
Expected: plugin builds without errors or new warnings in the touched headers.

- [ ] **Step 3: Update the audit's status line**

In `docs/audits/2026-08-04-signal-path-bug-round.md`, change line 4 from

```
**Status:** findings only, nothing fixed, nothing decided
```

to

```
**Status:** findings 1-4 + the DRIVE-smoothing observation fixed on branch `signal-path-declick` (plan `docs/superpowers/plans/2026-08-04-signal-path-declick.md`); listening sign-off pending
```

- [ ] **Step 4: Commit**

```bash
git add docs/audits/2026-08-04-signal-path-bug-round.md
git commit -m "docs(audit): signal-path round is fixed pending listening sign-off

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

- [ ] **Step 5: Report the listening checklist**

This branch changes four audible *transitions*; Bastian signs them off by ear before merge. Report this list verbatim at the end of the run:

1. **FLUX off** now fades the echo out in 4 ms instead of letting it ring and then cutting. If the old "ring out then stop" felt better, the alternative is fading only the send and letting the tail decay — say so rather than tweaking blind.
2. **FLUX on** always starts from a clean line (no dub-style memory of the last take). If tail-keeping is wanted, drop the `flush_lines()` calls — the return fade already declicks it.
3. **REVERB MIX → 0**: tail now fades over `kRevReturnFadeS = 0.15 s` (ear-tunable, `engine/instrument.cpp`).
4. **COMP below 0.15**: the ceiling now fades in over `kCapEngage` (ear-tunable, `engine/fx/comp.cpp`); defaults 0.630/0.561 are mathematically untouched, but a quick A/B at the defaults confirms it.
5. **Master DRIVE sweeps** are now zipper-free; static settings unchanged. NOTE: none of these fixes touch the DRIVE > 0.4 saturation character — that is the documented knee-morph design, deliberately left alone.
