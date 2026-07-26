# BODY Resonator Engine (M5j) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add BODY, a selectable part engine that morphs continuously from a Karplus-Strong string through dispersive metal into a 24-mode bell, with a sympathetic excitation bus.

**Architecture:** A band-pass mode bank whose coefficients are computed on the 96-sample control tick (not per sample — the whole feasibility argument) sits alongside a Karplus string pair inside one voice; MATL blends them. `SynthEngineT` is lifted from the oscillator type to the voice type so BODY inherits the existing allocation machine. Three excitation taps (own FLUX tape, the other deck, audio in) feed a per-part bus gated by SUB.

**Tech Stack:** C++17, no exceptions/RTTI. Desktop tests via doctest + CTest (clang + ninja). Reference-render gates via `tests/check_render_hash.cmake`. Hardware measurement via `bench/run.py` on a Daisy Seed over ST-Link.

**Spec:** `docs/superpowers/specs/2026-07-26-body-resonator-engine-design.md`

## Global Constraints

- Portable engine code (`engine/**`) must not include libDaisy. DaisySP primitives are allowed only via the `daisysp_min` target.
- All parameter derivation and smoothing runs on the control cadence `SynthEngineT::kCtrlInterval = 96`. Nothing new goes in the per-sample path that does not have to be there.
- No libm in any per-sample path. Measured: `sinf` 117 cycles/call, `tanhf` 208, `powf` 198. Use `engine/util/fast_sin.h`, `engine/util/fast_tanh.h`.
- Zero new panel controls and zero new parameter ids. Every BODY control is a reinterpretation of an existing one (spec §5).
- `EngineId` values are appended in milestone order and never renumbered: `ENGINE_BODY = 4`. ZAP moves to 5 (already corrected in its spec).
- Determinism: same seed → bit-identical render on desktop, VCV and firmware.
- Every commit message ends with `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- Desktop build environment: `source env.sh` first (clang + ninja + vendored headers). Never invoke the system g++ — it is the ARM cross-compiler.

**Build and test commands** (from the repo root, after `source env.sh`):

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Single test case: `./build/spky_tests.exe -tc="<test case name>"`

## The gate, and what it decides

Phase 1 measures before anything is built on top of it. Two numbers come back
from real hardware:

- `mode_bank_24` — the new bank at 24 modes with cached coefficients
- `ks_string_pair` — two `daisysp::String` instances, one voice's worth

Their sum plus ~25 cycles/sample of exciter, pan and mix is the per-voice cost.
Task 4 turns that into a single decision: the value of `BodyVoice::kVoices`
(4, 2, or 1). **Tasks 7 onward read that number from Task 4's recorded
result.** The mode count stays at 24 in every outcome — voices degrade first
(spec §7, user decision).

If the sum lands at or below ~405 cycles/sample, `kVoices = 4` and nothing
downstream changes.

---

## File Structure

**Created:**

| File | Responsibility |
|---|---|
| `engine/util/svf_bp.h` | Batched band-pass SVF taking *pre-computed* coefficients. No tan, no division in the per-sample path. |
| `engine/body/mode_bank.h/.cpp` | 24 modes in batches of 4. Owns the control-rate coefficient derivation; `process()` is the per-sample bank sum. |
| `engine/body/exciter.h` | Four strike characters (click / noise / sputter / ping) from a deterministic `Rng`. |
| `engine/body/body_voice.h/.cpp` | One voice: string pair + mode bank + MATL morph + energy follower. Implements the `SynthEngineT` voice contract. |
| `tests/test_svf_bp.cpp` | SvfBp recurrence + coefficient caching. |
| `tests/test_mode_bank.cpp` | Mode tuning, damping, and the coefficient-recompute count guard. |
| `tests/test_body_voice.cpp` | Follower, MATL endpoints, palm mute, excitation gate. |
| `tests/test_body_engine.cpp` | Shared part-engine contract against `BodyEngine`. |
| `bench/workloads_body.cpp` | Bench rows for the bank, the string pair, and later the full engine. |
| `host/render/scenarios/body_strum.json` | STEP + chord layer, MATL rising string → bell. |
| `host/render/scenarios/body_bow.json` | FLOW drone, bowed excitation, SUB sweep. |
| `host/render/scenarios/body_sympathetic.json` | Deck A sampler, deck B BODY answering it. |

**Modified:**

| File | Change |
|---|---|
| `engine/synth/synth_engine.h/.cpp` | Template parameter `OscT` → `V` (voice type); aliases and explicit instantiations updated; `BodyEngine` added. |
| `engine/synth/voice.h` | `set_hold`, `set_excitation` no-ops added to `VoiceT`. |
| `engine/parts/engine_iface.h` | `ENGINE_BODY = 4`. |
| `engine/parts/part.cpp` | Forward the excitation bus and `process_in` to the engine. |
| `engine/fx/part_fx.h/.cpp` | Expose the previous block's tape tap. |
| `engine/instrument.h/.cpp` | Hold each deck's previous-block dry output; feed the cross-deck tap. |
| `host/render/scenario.cpp:85-88` | Parse `"body"`. |
| `host/vcv/src/Spotymod.cpp` | Engine button gains a sixth state; per-deck context menu gains three excitation checkboxes. |
| `CMakeLists.txt` | New sources in `spky_tests` and `render`; three new render-hash tests. |
| `bench/Makefile`, `bench/workload.h`, `bench/runner.cpp`, `bench/main.cpp` | Register the `body` workload family. |
| `README.md`, `docs/roadmap.md` | M5j status. |

---

## Phase 1 — The gate

### Task 1: SvfBp — band-pass SVF with cached coefficients

**Files:**
- Create: `engine/util/svf_bp.h`
- Test: `tests/test_svf_bp.cpp`
- Modify: `CMakeLists.txt` (add the test source to `spky_tests`)

**Interfaces:**
- Consumes: nothing.
- Produces: `template <int N> class spky::SvfBp` with
  `void reset()`,
  `void set_coeffs(int i, float g, float r_plus_g, float h)`,
  `float process(const float* gain, float in)` — returns the gain-weighted band-pass sum of all `N` modes.

**Why this file exists.** `daisysp::ResonatorSvf<N>::Process` recomputes
`g = fasttan(f)`, `r = 1/q` and `h = 1/(1 + r*g + g*g)` **per sample, per
mode** — a polynomial tangent and two divisions per mode per sample. Those
depend only on frequency and Q, which in this engine change once per control
tick. `SvfBp` takes them pre-computed. The recurrence itself is copied
unchanged from `lib/DaisySP/Source/PhysicalModeling/resonator.h` (MIT,
Electrosmith + Émilie Gillet) — the point of this class is that it is the same
filter, not a better one. Same rule as `engine/util/svf_lp.h`; record the
attribution in `THIRD_PARTY.md` the same way.

- [ ] **Step 1: Write the failing test**

Create `tests/test_svf_bp.cpp`:

```cpp
#include "doctest/doctest.h"
#include "util/svf_bp.h"
#include <cmath>

using namespace spky;

// Drive one mode with an impulse and confirm it rings at the tuned frequency.
TEST_CASE("SvfBp rings at the frequency its coefficients encode") {
    SvfBp<4> bank;
    bank.reset();

    // 1 kHz at 48 kHz, Q = 40, expressed the way ModeBank will express it.
    const float f  = 1000.f / 48000.f;
    const float q  = 40.f;
    const float g  = std::tan(3.14159265f * f);
    const float r  = 1.f / q;
    const float h  = 1.f / (1.f + r * g + g * g);
    bank.set_coeffs(0, g, r + g, h);
    for (int i = 1; i < 4; ++i) bank.set_coeffs(i, 0.f, 0.f, 1.f);

    const float gain[4] = { 1.f, 0.f, 0.f, 0.f };

    // Impulse, then count zero crossings over 48000 samples (1 s).
    int   crossings = 0;
    float prev = bank.process(gain, 1.f);
    for (int i = 1; i < 48000; ++i) {
        const float s = bank.process(gain, 0.f);
        if ((prev < 0.f) != (s < 0.f)) ++crossings;
        prev = s;
    }
    // Two crossings per cycle; allow 2 % for the ring decaying into noise.
    CHECK(crossings > 1960);
    CHECK(crossings < 2040);
}

TEST_CASE("SvfBp is silent with zero gains and stays finite") {
    SvfBp<4> bank;
    bank.reset();
    for (int i = 0; i < 4; ++i) bank.set_coeffs(i, 0.5f, 0.6f, 0.7f);
    const float gain[4] = { 0.f, 0.f, 0.f, 0.f };
    for (int i = 0; i < 1000; ++i) CHECK(bank.process(gain, 1.f) == 0.f);
}

TEST_CASE("SvfBp reset clears state") {
    SvfBp<4> bank;
    bank.reset();
    for (int i = 0; i < 4; ++i) bank.set_coeffs(i, 0.2f, 0.3f, 0.9f);
    const float gain[4] = { 1.f, 1.f, 1.f, 1.f };
    for (int i = 0; i < 100; ++i) bank.process(gain, 1.f);
    const float ringing = bank.process(gain, 0.f);
    CHECK(std::fabs(ringing) > 0.f);
    bank.reset();
    CHECK(bank.process(gain, 0.f) == 0.f);
}
```

Add to `CMakeLists.txt` in the `add_executable(spky_tests ...)` list:
`tests/test_svf_bp.cpp`

- [ ] **Step 2: Run test to verify it fails**

```bash
source env.sh && cmake -S . -B build && cmake --build build
```
Expected: FAIL — `util/svf_bp.h` not found.

- [ ] **Step 3: Write the implementation**

Create `engine/util/svf_bp.h`:

```cpp
#pragma once

namespace spky {

// Band-pass-only batched SVF for the BODY mode bank.
//
// The recurrence, the two-integrator topology and the h/g/r_plus_g formulation
// are copied unchanged from daisysp::ResonatorSvf<N>::Process
// (lib/DaisySP/Source/PhysicalModeling/resonator.h, Copyright 2020
// Electrosmith / Emilie Gillet, MIT -- see THIRD_PARTY.md). One thing is
// different, and it is the reason this file exists:
//
//   ResonatorSvf::Process computes g = fasttan(f), r = 1/q and
//   h = 1/(1 + r*g + g*g) INSIDE the per-sample call, for every mode. That is
//   a polynomial tangent and two divisions per mode per sample. Those three
//   values depend only on frequency and Q, and in this engine both change once
//   per 96-sample control tick. So they are pushed in from outside via
//   set_coeffs() and the per-sample path does arithmetic only.
//
// Low-pass, high-pass, notch and peak outputs are not computed: nothing reads
// them (same reasoning as util/svf_lp.h). The low-pass intermediate `lp` stays
// because state_2's update needs it.
template <int N>
class SvfBp {
public:
    void reset() {
        for (int i = 0; i < N; ++i) { _s1[i] = 0.f; _s2[i] = 0.f; }
    }

    // Control-rate feed. g = tan(pi * f_normalized), r_plus_g = 1/q + g,
    // h = 1 / (1 + g/q + g*g).
    void set_coeffs(int i, float g, float r_plus_g, float h) {
        _g[i] = g; _rg[i] = r_plus_g; _h[i] = h;
    }

    // Per-sample. Returns sum(gain[i] * bandpass_i(in)).
    float process(const float* gain, float in) {
        float out = 0.f;
        for (int i = 0; i < N; ++i) {
            const float hp = (in - _rg[i] * _s1[i] - _s2[i]) * _h[i];
            const float bp = _g[i] * hp + _s1[i];
            _s1[i] = _g[i] * hp + bp;
            const float lp = _g[i] * bp + _s2[i];
            _s2[i] = _g[i] * bp + lp;
            out += gain[i] * bp;
        }
        return out;
    }

private:
    float _g[N]  = {};
    float _rg[N] = {};
    float _h[N]  = {};
    float _s1[N] = {};
    float _s2[N] = {};
};

} // namespace spky
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests.exe -tc="SvfBp*"
```
Expected: PASS, 3 test cases.

- [ ] **Step 5: Record the attribution**

Add an entry to `THIRD_PARTY.md` for `engine/util/svf_bp.h` in the same shape as the existing `engine/util/svf_lp.h` entry: recurrence ported from `daisysp::ResonatorSvf`, MIT, Electrosmith + Émilie Gillet.

- [ ] **Step 6: Commit**

```bash
git add engine/util/svf_bp.h tests/test_svf_bp.cpp CMakeLists.txt THIRD_PARTY.md
git commit -m "feat(body): band-pass SVF that takes its coefficients pre-computed

ResonatorSvf recomputes fasttan and two divisions per mode per sample for
values that only change on the control tick. SvfBp takes them from outside;
the per-sample path is arithmetic only. Recurrence copied unchanged.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: ModeBank — 24 modes, control-rate coefficients

**Files:**
- Create: `engine/body/mode_bank.h`, `engine/body/mode_bank.cpp`
- Test: `tests/test_mode_bank.cpp`
- Modify: `CMakeLists.txt` (`spky_tests` and `render` source lists)

**Interfaces:**
- Consumes: `spky::SvfBp<4>` from Task 1.
- Produces: `class spky::ModeBank` with
  `static constexpr int kModes = 24`,
  `void init(float sample_rate)`,
  `void reset()`,
  `void set_params(float f0_hz, float stretch, float damping, float brightness)` — control rate only,
  `float process(float in)` — per sample,
  `uint32_t coeff_updates() const` — test hook, counts `set_params` bodies that actually recomputed.

**Parameter meanings** (spec §4, §5): `f0_hz` is the fundamental; `stretch`
0..1 is inharmonicity (DETUNE); `damping` 0..1 is ring time (DECAY);
`brightness` 0..1 is the high-mode roll-off (FILTER). Mode amplitudes come
from a fixed strike position — position is not a control (spec §2).

- [ ] **Step 1: Write the failing test**

Create `tests/test_mode_bank.cpp`:

```cpp
#include "doctest/doctest.h"
#include "body/mode_bank.h"
#include <cmath>
#include <vector>

using namespace spky;

static int zero_crossings(const std::vector<float>& v) {
    int n = 0;
    for (size_t i = 1; i < v.size(); ++i)
        if ((v[i - 1] < 0.f) != (v[i] < 0.f)) ++n;
    return n;
}

static std::vector<float> strike(ModeBank& b, int samples) {
    std::vector<float> out(samples);
    out[0] = b.process(1.f);
    for (int i = 1; i < samples; ++i) out[i] = b.process(0.f);
    return out;
}

TEST_CASE("ModeBank fundamental tracks the requested pitch") {
    ModeBank b;
    b.init(48000.f);
    // stretch 0 => harmonic; the fundamental dominates.
    b.set_params(220.f, 0.f, 0.5f, 0.2f);
    const auto v = strike(b, 48000);
    // 220 Hz => ~440 crossings/s. Allow 5 % for higher modes colouring it.
    CHECK(zero_crossings(v) > 418);
    CHECK(zero_crossings(v) < 462);
}

TEST_CASE("ModeBank damping sets ring time") {
    ModeBank tight, ringing;
    tight.init(48000.f);
    ringing.init(48000.f);
    tight.set_params(220.f, 0.f, 0.f, 0.5f);
    ringing.set_params(220.f, 0.f, 1.f, 0.5f);

    auto energy_at = [](ModeBank& b, int n) {
        strike(b, n);
        float e = 0.f;
        for (int i = 0; i < 4800; ++i) { const float s = b.process(0.f); e += s * s; }
        return e;
    };
    CHECK(energy_at(ringing, 24000) > energy_at(tight, 24000));
}

TEST_CASE("ModeBank stretch makes the partials inharmonic") {
    ModeBank harmonic, stretched;
    harmonic.init(48000.f);
    stretched.init(48000.f);
    harmonic.set_params(220.f, 0.f, 0.8f, 0.8f);
    stretched.set_params(220.f, 1.f, 0.8f, 0.8f);
    const auto a = strike(harmonic, 8192);
    const auto b = strike(stretched, 8192);
    bool differs = false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) differs = true;
    CHECK(differs);
    // Stretched partials sit higher, so the waveform crosses zero more often.
    CHECK(zero_crossings(b) > zero_crossings(a));
}

TEST_CASE("ModeBank recomputes coefficients only when parameters move") {
    ModeBank b;
    b.init(48000.f);
    b.set_params(220.f, 0.2f, 0.5f, 0.5f);
    const uint32_t after_first = b.coeff_updates();
    CHECK(after_first == 1);
    for (int i = 0; i < 50; ++i) b.set_params(220.f, 0.2f, 0.5f, 0.5f);
    CHECK(b.coeff_updates() == after_first);
    b.set_params(221.f, 0.2f, 0.5f, 0.5f);
    CHECK(b.coeff_updates() == after_first + 1);
}

TEST_CASE("ModeBank stays finite under extreme settings") {
    ModeBank b;
    b.init(48000.f);
    b.set_params(4000.f, 1.f, 1.f, 1.f);
    for (int i = 0; i < 96000; ++i) {
        const float s = b.process(i % 96 == 0 ? 1.f : 0.f);
        REQUIRE(std::isfinite(s));
    }
}
```

Add `tests/test_mode_bank.cpp` and `engine/body/mode_bank.cpp` to the `spky_tests` sources in `CMakeLists.txt`, and `engine/body/mode_bank.cpp` to the `render` sources.

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build
```
Expected: FAIL — `body/mode_bank.h` not found.

- [ ] **Step 3: Write the header**

Create `engine/body/mode_bank.h`:

```cpp
#pragma once
#include <cstdint>
#include "util/svf_bp.h"

namespace spky {

// A bank of kModes band-pass resonators, the modal half of BodyVoice.
//
// The mode frequency / Q / amplitude formulas are ported from
// daisysp::Resonator::Process (lib/DaisySP/Source/PhysicalModeling/
// resonator.cpp, MIT, Electrosmith + Emilie Gillet -- see THIRD_PARTY.md).
// The difference is WHERE they run: Resonator recomputes all of them for
// every mode on every sample, including a powf (measured at 198 cycles per
// call on the Seed) and a stretch-factor loop. Here they run once per
// set_params() call, which the engine makes on the 96-sample control tick.
// process() is the SvfBp bank sum and nothing else.
//
// Strike position is fixed (kPosition): modal synthesis wants a position
// parameter, this engine does not have a knob to spend on one (spec 2).
class ModeBank {
public:
    static constexpr int kModes = 24;
    static constexpr int kBatch = 4;
    static constexpr int kBatches = kModes / kBatch;
    static_assert(kModes % kBatch == 0, "mode count must fill whole batches");

    void init(float sample_rate);
    void reset();

    // Control rate ONLY. f0_hz: fundamental. stretch/damping/brightness: 0..1
    // (DETUNE / DECAY / FILTER). Recomputes cached coefficients when any
    // argument actually changed; otherwise returns without touching them.
    void set_params(float f0_hz, float stretch, float damping, float brightness);

    // Per sample.
    float process(float in);

    uint32_t coeff_updates() const { return _updates; }

private:
    void _recompute();

    static constexpr float kPosition = 0.31f;   // strike position, tuning material
    static constexpr float kPi = 3.14159265358979f;

    SvfBp<kBatch> _svf[kBatches];
    float _gain[kBatches][kBatch] = {};

    float _sr = 48000.f;
    float _f0 = 220.f, _stretch = 0.f, _damping = 0.5f, _brightness = 0.5f;
    bool  _dirty = true;
    uint32_t _updates = 0;
};

} // namespace spky
```

- [ ] **Step 4: Write the implementation**

Create `engine/body/mode_bank.cpp`:

```cpp
#include "body/mode_bank.h"
#include <cmath>

namespace spky {

void ModeBank::init(float sample_rate) {
    _sr = sample_rate;
    reset();
    _dirty = true;
    _updates = 0;
}

void ModeBank::reset() {
    for (int b = 0; b < kBatches; ++b) _svf[b].reset();
}

void ModeBank::set_params(float f0_hz, float stretch, float damping,
                          float brightness) {
    if (f0_hz == _f0 && stretch == _stretch && damping == _damping
        && brightness == _brightness && !_dirty)
        return;
    _f0 = f0_hz; _stretch = stretch; _damping = damping;
    _brightness = brightness;
    _recompute();
    _dirty = false;
    ++_updates;
}

// Everything below runs once per control tick, never per sample.
void ModeBank::_recompute() {
    // Stiffness drives how far the partials depart from the harmonic series:
    // 0 = harmonic (string), 1 = strongly stretched (bell). Ported from
    // Resonator's CalcStiff/structure path, reduced to the branch this engine
    // uses (positive stiffness only -- negative stiffness compresses partials
    // toward the fundamental, which the MATL axis reaches through the string
    // side instead).
    const float stiffness = _stretch * 0.4f;

    // Q from damping: the Resonator mapping, evaluated once.
    const float q_sqrt = std::pow(2.f, _damping * 79.7f / 12.f);
    const float q_base = 500.f * q_sqrt * q_sqrt;

    // Brightness rolls the upper modes off; same shape as Resonator's q_loss.
    float bright = _brightness * (1.f - _stretch * 0.3f);
    bright *= 1.f - _damping * 0.3f;
    const float q_loss = bright * (2.f - bright) * 0.85f + 0.15f;

    const float amp0 = std::cos(kPosition * 2.f * kPi) * 0.25f;

    float stretch_factor = 1.f;
    float stiff_iter = stiffness;
    float loss = 1.f;

    for (int i = 0; i < kModes; ++i) {
        const float mode_hz = _f0 * stretch_factor;
        float f = mode_hz / _sr;                 // cycles per sample
        if (f > 0.49f) f = 0.49f;                // Nyquist guard

        const float attenuation = 1.f - f * 2.f;
        const float q = 1.f + f * q_base * loss;

        const float g = std::tan(kPi * f);
        const float r = 1.f / q;
        const float h = 1.f / (1.f + r * g + g * g);

        const int b = i / kBatch, s = i % kBatch;
        _svf[b].set_coeffs(s, g, r + g, h);
        _gain[b][s] = amp0 * attenuation;

        // Advance to the next partial. stretch_factor grows superlinearly with
        // stiffness -- that is what turns a harmonic series into a bell.
        stretch_factor += 1.f + stiff_iter;
        stiff_iter *= 0.98f;
        loss *= q_loss;
    }
}

float ModeBank::process(float in) {
    float out = 0.f;
    for (int b = 0; b < kBatches; ++b) out += _svf[b].process(_gain[b], in);
    return out;
}

} // namespace spky
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests.exe -tc="ModeBank*"
```
Expected: PASS, 5 test cases. If the fundamental-tracking test misses its
window, the `stretch_factor` seeding is the cause — mode 0 must come out at
exactly `_f0`, so `stretch_factor` starts at 1.

- [ ] **Step 6: Commit**

```bash
git add engine/body/mode_bank.h engine/body/mode_bank.cpp tests/test_mode_bank.cpp CMakeLists.txt
git commit -m "feat(body): 24-mode bank with coefficients on the control tick

The modal half of BODY. Mode frequency/Q/amplitude formulas ported from
daisysp::Resonator; the difference is that they run once per set_params()
instead of once per sample per mode. process() is the SvfBp sum and nothing
else.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: Bench rows for the two unmeasured halves

**Files:**
- Create: `bench/workloads_body.cpp`
- Modify: `bench/workload.h`, `bench/runner.cpp:51`, `bench/main.cpp`, `bench/Makefile`

**Interfaces:**
- Consumes: `spky::ModeBank` (Task 2), `daisysp::String`.
- Produces: bench rows `body/mode_bank_24` and `body/ks_string_pair`, plus the family externs `kBodyWorkloads` / `kBodyCount`.

**What this measures and why.** `mode_bank_24` prices the bank against the
already-measured `voice/resonator` row (329,000 cycles, 34.27 %) in the *same
run*, so the comparison is free of cross-build layout drift. `ks_string_pair`
prices two `daisysp::String` instances — the KS primitive itself, which has
never been measured; the existing `voice/string_voice` row measures
`daisysp::StringVoice`, a much fatter Rings-derived model (110,628 cycles,
11.52 % for one voice) that this engine does not use.

- [ ] **Step 1: Write the workload file**

Create `bench/workloads_body.cpp`:

```cpp
#include "workload.h"
#include "body/mode_bank.h"
#include "PhysicalModeling/KarplusString.h"
#include "test_input.h"

namespace bench {
namespace {

spky::ModeBank g_bank;
int            g_bank_ctr = 0;

void setup_mode_bank()
{
    g_bank.init(kSampleRate);
    g_bank.set_params(220.f, 0.6f, 0.8f, 0.7f);
    g_bank_ctr = 0;
}

// One control tick per block, exactly as BodyVoice will drive it: the point
// of the row is that the coefficient math is NOT in the per-sample loop.
float proc_mode_bank()
{
    const float* in = test_input();
    g_bank.set_params(220.f, 0.6f, 0.8f, 0.7f);
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i) acc += g_bank.process(in[i]);
    return acc;
}

daisysp::String g_str_a, g_str_b;

void setup_ks_pair()
{
    g_str_a.Init(kSampleRate);
    g_str_b.Init(kSampleRate);
    g_str_a.SetFreq(220.f);
    g_str_b.SetFreq(220.f * 1.008f);   // the DETUNE spread a voice runs with
    g_str_a.SetBrightness(0.7f);
    g_str_b.SetBrightness(0.7f);
    g_str_a.SetDamping(0.7f);
    g_str_b.SetDamping(0.7f);
    g_str_a.SetNonLinearity(0.4f);
    g_str_b.SetNonLinearity(0.4f);
}

float proc_ks_pair()
{
    const float* in = test_input();
    float acc = 0.f;
    for (size_t i = 0; i < kBlock; ++i)
        acc += g_str_a.Process(in[i]) + g_str_b.Process(in[i]);
    return acc;
}

} // namespace

const Workload kBodyWorkloads[] = {
    { "body", "mode_bank_24",  setup_mode_bank, proc_mode_bank },
    { "body", "ks_string_pair", setup_ks_pair,  proc_ks_pair   },
};
const int kBodyCount = sizeof(kBodyWorkloads) / sizeof(kBodyWorkloads[0]);

} // namespace bench
```

Check the include path and method names of `daisysp::String` against
`lib/DaisySP/Source/PhysicalModeling/KarplusString.h` before building — the
setter names above (`SetFreq`, `SetBrightness`, `SetDamping`,
`SetNonLinearity`) come from that header and must match it exactly. Check
`bench/workloads_memory.cpp` for the exact spelling of the `test_input()`
include used by the other workload files.

- [ ] **Step 2: Register the family**

In `bench/workload.h`, after the `kSamplerWorkloads` block:

```cpp
extern const Workload kBodyWorkloads[];
extern const int      kBodyCount;
```

In `bench/runner.cpp:51`, add `kBodyWorkloads` to the `tables[]` array and its
count to the parallel counts array (read the surrounding lines — the two arrays
must stay index-aligned).

In `bench/main.cpp`, mirror the existing per-family print loops for the new
family.

In `bench/Makefile`, add `workloads_body.cpp` to `CPP_SOURCES`, and add
`../engine/body/mode_bank.cpp`.

- [ ] **Step 3: Build the bench firmware without hardware**

```bash
cd bench && python run.py --build-only
```
Expected: builds clean; the memory report prints and `SRAM_EXEC` stays under
262,880 B.

- [ ] **Step 4: Commit**

```bash
git add bench/
git commit -m "bench(body): price the mode bank and the KS string pair

The two halves of a BodyVoice that have never been measured. mode_bank_24
runs against the existing voice/resonator row in the same capture, so the
comparison carries no cross-build layout drift; ks_string_pair measures the
KS primitive itself rather than the Rings-derived StringVoice wrapper the
existing row uses.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: Run the gate and decide `kVoices`

**Files:**
- Create: `docs/bench/YYYY-MM-DD-<githash>.md` and `.csv` (written by `run.py`)
- Modify: `docs/roadmap.md` (record the decision)

**Interfaces:**
- Consumes: the bench rows from Task 3.
- Produces: the value of `BodyVoice::kVoices`, consumed by Task 7 onward.

**This task needs the hardware.** Connect an ST-Link V3 over SWD, power the
Seed, turn the monitor level down.

- [ ] **Step 1: Run the bench**

```bash
cd bench && python run.py
```
Expected: exit 0, two runs agreeing, capture written to `../docs/bench/`.

- [ ] **Step 2: Read the two rows and compute the per-voice cost**

From the capture's offline table, take the `avg cyc` of `body/mode_bank_24`
and `body/ks_string_pair`. Both are per 96-sample block. Per voice per sample:

```
(mode_bank_24 + ks_string_pair) / 96 + 25      # 25 = exciter + pan + mix + follower
```

- [ ] **Step 3: Apply the ladder**

| per-voice cycles/sample | `kVoices` | 2×4 estimate |
|---|---|---|
| ≤ 405 | **4** | ≤ 32 % of the block |
| 406–810 | **2** | ≤ 32 % |
| > 810 | **1** | — |

The mode count stays at 24 in every case (spec §7, user decision: a rich bell
at low polyphony beats four poor ones).

- [ ] **Step 4: Record the decision**

In `docs/roadmap.md`, under `### M5j — BODY ⬜`, add a line naming the two
measured figures, the derived per-voice cost, and the chosen `kVoices`. If
`kVoices < 4`, also note in one sentence what that costs musically: overlapping
STEP notes first, then the chord layer entirely at 1 voice.

- [ ] **Step 5: Commit**

```bash
git add docs/bench/ docs/roadmap.md
git commit -m "bench(body): gate result -- mode bank and KS pair priced on the Seed

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

- [ ] **Step 6: Stop and report**

If the per-voice cost lands above 810 cycles/sample, do **not** continue to
Phase 3. Report the numbers and stop: at 1 voice the spec's control mapping
(chord layer, stab humanization) needs a design revisit, which is a
brainstorming decision, not an implementation one.

---

## Phase 2 — The template lift

### Task 5: Lift `SynthEngineT` from the oscillator to the voice type

**Files:**
- Modify: `engine/synth/synth_engine.h:31-136`, `engine/synth/synth_engine.cpp:316-317`, `engine/synth/voice.h`
- Test: existing `tests/test_synth_engine.cpp`, `tests/test_wave_engine.cpp`, `tests/test_voice.cpp` — unchanged
- Gate: existing CTest cases `ctrl_identity` and `wave_formant_sweep` — unchanged

**Interfaces:**
- Consumes: nothing new.
- Produces: `template <class V> class spky::SynthEngineT`, with the aliases
  `using SynthEngine = SynthEngineT<VoiceT<MorphOsc>>` and
  `using WaveEngine = SynthEngineT<VoiceT<WtOsc>>` keeping their current names
  and public surface. `VoiceT` gains `void set_hold(bool)` and
  `void set_excitation(float)`, both no-ops.

**Why this is its own task.** It touches SYNTH and WAVE and nothing else, and
its gate is binary: two reference renders must hash the same before and after.
A reviewer can accept or reject it without knowing anything about BODY.

- [ ] **Step 1: Record the baseline hashes**

```bash
source env.sh && cmake -S . -B build && cmake --build build
ctest --test-dir build -R "ctrl_identity|wave_formant_sweep" --output-on-failure
```
Expected: both PASS. These same two tests are the gate in Step 5 — no new test
is written, because the existing ones already assert exactly what must not
move.

- [ ] **Step 2: Change the template parameter**

In `engine/synth/synth_engine.h`, change the class template from
`template <class OscT> class SynthEngineT : public IPartEngine` to
`template <class V> class SynthEngineT : public IPartEngine`. Replace every
internal use of `VoiceT<OscT>` with `V`. At the bottom, replace the aliases and
extern declarations:

```cpp
using SynthEngine = SynthEngineT<VoiceT<MorphOsc>>;
extern template class SynthEngineT<VoiceT<MorphOsc>>;

using WaveEngine = SynthEngineT<VoiceT<WtOsc>>;
extern template class SynthEngineT<VoiceT<WtOsc>>;
```

- [ ] **Step 3: Update the explicit instantiations**

In `engine/synth/synth_engine.cpp:316-317`:

```cpp
template class spky::SynthEngineT<spky::VoiceT<spky::MorphOsc>>;
template class spky::SynthEngineT<spky::VoiceT<spky::WtOsc>>;
```

`engine/synth/voice.cpp:128-129` stays as it is — `VoiceT` itself is unchanged
in shape.

- [ ] **Step 4: Add the two contract no-ops to `VoiceT`**

In `engine/synth/voice.h`, in the public section of `VoiceT`:

```cpp
    // BODY contract methods. A synth voice has no body to mute and no
    // excitation input; both are no-ops here so SynthEngineT can call them
    // unconditionally (spec 2026-07-26 body-resonator, §1).
    void set_hold(bool /*on*/) {}
    void set_excitation(float /*x*/) {}
```

- [ ] **Step 5: Run the full suite and the two byte-identity gates**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: everything PASS, in particular `ctrl_identity` and
`wave_formant_sweep`. If either reports "reference moved", the lift changed
behaviour — that is a bug in the refactor, not a reference to re-cut. Do not
update the expected hashes.

- [ ] **Step 6: Commit**

```bash
git add engine/synth/synth_engine.h engine/synth/synth_engine.cpp engine/synth/voice.h
git commit -m "refactor(engine): template SynthEngineT on the voice, not the oscillator

WAVE shipped as SynthEngineT<WtOsc> holding VoiceT<OscT>, which works only
while every engine's voice is an oscillator in a Svf/Env chain. A resonator
voice is not, so the template moves one level up and the allocation machine
-- round-robin, steal, FLOW, chords, CHOKE, control cadence -- stays
inherited rather than copied.

SYNTH and WAVE reference renders hash unchanged.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Phase 3 — The voice

### Task 6: Exciter — four strike characters

**Files:**
- Create: `engine/body/exciter.h`
- Test: `tests/test_body_voice.cpp` (first test cases; the file grows in Task 7)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `spky::Rng` (`engine/mod/rng.h`), `spky::fast_sin` (`engine/util/fast_sin.h`), `spky::OnePole` (`engine/util/onepole.h`).
- Produces: `class spky::Exciter` with
  `void init(uint32_t seed, float sample_rate)`,
  `void set_character(float c)` — 0..1, four zones (RESO),
  `void set_length(float seconds)` — ATTACK,
  `void set_freq(float hz)` — for the ping zone,
  `void strike(float velocity)`,
  `void set_continuous(bool on)` — FLOW bows instead of strikes,
  `float process()`.

**Zones** (spec §2): 0 click (filtered impulse), 1 noise burst, 2 granular
sputter (rng-gated micro-bursts), 3 tonal ping (`fast_sin` blip at the
fundamental). Zone boundaries and crossfades are tuning material.

- [ ] **Step 1: Write the failing test**

Create `tests/test_body_voice.cpp`:

```cpp
#include "doctest/doctest.h"
#include "body/exciter.h"
#include <cmath>

using namespace spky;

static float energy(Exciter& e, int n) {
    float sum = 0.f;
    for (int i = 0; i < n; ++i) { const float s = e.process(); sum += s * s; }
    return sum;
}

TEST_CASE("Exciter is silent until struck and decays after") {
    Exciter e;
    e.init(7, 48000.f);
    e.set_character(0.f);
    e.set_length(0.005f);
    e.set_freq(220.f);
    CHECK(energy(e, 480) == 0.f);
    e.strike(1.f);
    const float during = energy(e, 240);   // 5 ms
    const float after  = energy(e, 4800);  // 100 ms later
    CHECK(during > 0.f);
    CHECK(after < during * 0.01f);
}

TEST_CASE("Exciter zones produce different signals from the same seed") {
    float sig[4][512];
    for (int z = 0; z < 4; ++z) {
        Exciter e;
        e.init(7, 48000.f);
        e.set_character(z / 3.f);
        e.set_length(0.005f);
        e.set_freq(220.f);
        e.strike(1.f);
        for (int i = 0; i < 512; ++i) sig[z][i] = e.process();
    }
    for (int a = 0; a < 4; ++a)
        for (int b = a + 1; b < 4; ++b) {
            bool differs = false;
            for (int i = 0; i < 512; ++i) if (sig[a][i] != sig[b][i]) differs = true;
            CHECK(differs);
        }
}

TEST_CASE("Exciter is deterministic for a given seed") {
    float a[512], b[512];
    for (int pass = 0; pass < 2; ++pass) {
        Exciter e;
        e.init(1234, 48000.f);
        e.set_character(0.5f);
        e.set_length(0.01f);
        e.set_freq(330.f);
        e.strike(0.8f);
        for (int i = 0; i < 512; ++i) (pass ? b : a)[i] = e.process();
    }
    for (int i = 0; i < 512; ++i) CHECK(a[i] == b[i]);
}

TEST_CASE("Exciter in continuous mode does not decay to silence") {
    Exciter e;
    e.init(7, 48000.f);
    e.set_character(0.4f);
    e.set_length(0.05f);
    e.set_freq(220.f);
    e.set_continuous(true);
    e.strike(1.f);
    energy(e, 48000);
    CHECK(energy(e, 4800) > 0.f);
}
```

Add `tests/test_body_voice.cpp` to `spky_tests` in `CMakeLists.txt`.

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build
```
Expected: FAIL — `body/exciter.h` not found.

- [ ] **Step 3: Write the implementation**

Create `engine/body/exciter.h`. Read `engine/mod/rng.h` for the exact draw
method name and `engine/util/onepole.h` for its setter names before writing —
the sketch below uses `_rng.next_bipolar()` and `_lp.set_cutoff_hz()`, which
must be replaced with whatever those headers actually expose.

```cpp
#pragma once
#include <cstdint>
#include "mod/rng.h"
#include "util/fast_sin.h"
#include "util/onepole.h"

namespace spky {

// The playable strike for BodyVoice. Four characters across RESO (spec §2);
// in FLOW the same character becomes continuous excitation -- the bow.
//
// Zone boundaries and the crossfade widths here are TUNING MATERIAL: the
// contract is "four distinguishable characters that all decay with the
// strike", not these particular numbers (DUST-zone precedent).
class Exciter {
public:
    void init(uint32_t seed, float sample_rate) {
        _rng.seed(seed);
        _sr = sample_rate;
        _lp.init(sample_rate);
        _env = 0.f;
        _phase = 0.f;
        _burst = 0;
    }

    void set_character(float c) { _char = c < 0.f ? 0.f : (c > 1.f ? 1.f : c); }
    void set_length(float seconds) {
        const float n = seconds * _sr;
        _decay = n > 1.f ? (1.f - 1.f / n) : 0.f;
    }
    void set_freq(float hz) { _inc = hz / _sr; }
    void set_continuous(bool on) { _continuous = on; }

    void strike(float velocity) {
        _env = velocity;
        _phase = 0.f;
        _burst = 0;
        _fresh = true;
    }

    float process() {
        if (_env <= 0.f && !_continuous) return 0.f;

        // Character zones. 0..1/3 click, 1/3..2/3 noise, 2/3..1 sputter/ping;
        // the ping zone rides the top third of the range.
        const float z = _char * 3.f;
        float s = 0.f;

        if (z < 1.f) {                          // click: one filtered impulse
            s = _fresh ? 1.f : 0.f;
            _lp.set_cutoff_hz(2000.f + 6000.f * z);
            s = _lp.process(s);
        } else if (z < 2.f) {                   // noise burst
            s = _rng.next_bipolar();
            _lp.set_cutoff_hz(1000.f + 9000.f * (z - 1.f));
            s = _lp.process(s);
        } else {                                // sputter blended into ping
            const float t = z - 2.f;
            if (_burst-- <= 0) {
                _burst = 8 + static_cast<int>(24.f * (_rng.next_bipolar() * 0.5f + 0.5f));
                _gate = _rng.next_bipolar() > 0.f ? 1.f : 0.f;
            }
            const float sputter = _rng.next_bipolar() * _gate;
            _phase += _inc;
            if (_phase >= 1.f) _phase -= 1.f;
            const float ping = fast_sin(_phase);
            s = sputter * (1.f - t) + ping * t;
        }

        _fresh = false;
        if (!_continuous) _env *= _decay;
        return s * _env;
    }

private:
    Rng     _rng;
    OnePole _lp;
    float _sr = 48000.f;
    float _char = 0.f, _decay = 0.f, _inc = 0.f;
    float _env = 0.f, _phase = 0.f, _gate = 0.f;
    int   _burst = 0;
    bool  _continuous = false, _fresh = false;
};

} // namespace spky
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests.exe -tc="Exciter*"
```
Expected: PASS, 4 test cases.

- [ ] **Step 5: Commit**

```bash
git add engine/body/exciter.h tests/test_body_voice.cpp CMakeLists.txt
git commit -m "feat(body): playable exciter with four strike characters

Click, noise burst, granular sputter, tonal ping across RESO; in FLOW the
same character runs continuously as the bow. Deterministic per-voice Rng.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 7: BodyVoice — strings, bank, MATL morph, follower

**Files:**
- Create: `engine/body/body_voice.h`, `engine/body/body_voice.cpp`
- Test: `tests/test_body_voice.cpp` (append)
- Modify: `CMakeLists.txt` (`spky_tests` and `render` sources)

**Interfaces:**
- Consumes: `spky::ModeBank` (Task 2), `spky::Exciter` (Task 6), `daisysp::String`, and the value of `kVoices` decided in Task 4.
- Produces: `class spky::BodyVoice` implementing the full voice contract that `SynthEngineT` calls (Task 5): `init(float sample_rate, uint32_t seed)`, `trigger(float freq_hz)`, `set_sustaining(bool)`, `set_pitch_hz(float)`, `set_vel(float)`, `set_env_times(float attack_s, float decay_s)`, `set_morph(float)`, `set_detune_cents(float)`, `set_sub_level(float)`, `set_cutoff_hz(float)`, `set_resonance(float)`, `set_pan(float)`, `set_drift_amount(float)`, `set_hold(bool)`, `set_excitation(float)`, `update_control(float dt_s)`, `process(float& accL, float& accR)`, `active()`, `env_value()`, `detune_cents()`.

**Contract mapping** (spec §5) — the setter names come from SYNTH, the
meanings do not:

| setter | BODY meaning |
|---|---|
| `set_morph` | **MATL** — 0 string, 1 modal bank |
| `set_cutoff_hz` | brightness (log-mapped from the same Hz value) |
| `set_env_times(a, d)` | `a` → exciter length; `d` → damping (string damping + mode Q) |
| `set_resonance` | exciter character (RESO) |
| `set_detune_cents` | string spread **and** mode-bank stretch |
| `set_sub_level` | excitation bus level (SUB) — Task 9 feeds it |
| `set_hold` | palm mute: damping snaps high on both structures |
| `set_excitation` | per-sample bus feed (Task 9) |

**`active()` has no envelope** (spec §1): it is an energy follower — block peak,
decaying — OR'd with a minimum hold after a trigger so a quiet strike is not
stolen instantly.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_body_voice.cpp`:

```cpp
#include "body/body_voice.h"

static void tick(BodyVoice& v, int samples, float* l = nullptr, float* r = nullptr) {
    for (int i = 0; i < samples; ++i) {
        if (i % 96 == 0) v.update_control(96.f / 48000.f);
        float a = 0.f, b = 0.f;
        v.process(a, b);
        if (l) *l += a * a;
        if (r) *r += b * b;
    }
}

// Configure in place: BodyVoice owns daisysp::String instances with internal
// buffers, so it is never copied or returned by value.
static void fresh_voice(BodyVoice& v, float matl) {
    v.init(48000.f, 42);
    v.set_env_times(0.005f, 2.f);
    v.set_resonance(0.f);
    v.set_cutoff_hz(8000.f);
    v.set_detune_cents(0.f);
    v.set_sub_level(0.f);
    v.set_pan(0.f);
    v.set_drift_amount(0.f);
    v.set_vel(1.f);
    v.set_morph(matl);
    v.update_control(96.f / 48000.f);
}

TEST_CASE("BodyVoice MATL endpoints sound different") {
    BodyVoice string, bell;
    fresh_voice(string, 0.f);
    fresh_voice(bell, 1.f);
    string.trigger(220.f);
    bell.trigger(220.f);
    float es = 0.f, eb = 0.f;
    tick(string, 9600, &es, nullptr);
    tick(bell,   9600, &eb, nullptr);
    CHECK(es > 0.f);
    CHECK(eb > 0.f);
    CHECK(es != eb);
}

TEST_CASE("BodyVoice reports inactive after ringing out") {
    BodyVoice v;
    fresh_voice(v, 0.5f);
    v.set_env_times(0.002f, 0.05f);   // shortest decay
    v.update_control(96.f / 48000.f);
    v.trigger(440.f);
    tick(v, 960);
    CHECK(v.active());
    tick(v, 48000 * 5);
    CHECK_FALSE(v.active());
}

TEST_CASE("BodyVoice holds a quiet strike briefly so it is not stolen") {
    BodyVoice v;
    fresh_voice(v, 0.5f);
    v.set_vel(0.01f);
    v.update_control(96.f / 48000.f);
    v.trigger(440.f);
    tick(v, 96);
    CHECK(v.active());
}

// Spec §10: "the fundamental tracks pitch within a few cents across the
// register at both ends of MATL". The string half and the bank half derive
// pitch by completely different routes, so both ends must be checked.
TEST_CASE("BodyVoice tracks pitch across the register at both MATL ends") {
    const float pitches[] = { 110.f, 220.f, 440.f, 880.f, 1760.f };
    for (float matl : { 0.f, 1.f }) {
        for (float hz : pitches) {
            BodyVoice v;
            fresh_voice(v, matl);
            v.trigger(hz);
            // Count zero crossings over 0.5 s, skipping the strike transient.
            tick(v, 4800);
            int crossings = 0;
            float prev = 0.f;
            for (int i = 0; i < 24000; ++i) {
                if (i % 96 == 0) v.update_control(96.f / 48000.f);
                float l = 0.f, r = 0.f;
                v.process(l, r);
                if (i > 0 && (prev < 0.f) != (l < 0.f)) ++crossings;
                prev = l;
            }
            const float measured = crossings / 2.f / 0.5f;   // Hz
            // 3 % window: higher partials colour the crossing count, and this
            // is a tuning check, not a pitch detector.
            CHECK(measured > hz * 0.97f);
            CHECK(measured < hz * 1.03f);
        }
    }
}

TEST_CASE("BodyVoice palm mute drops energy fast") {
    BodyVoice open, muted;
    fresh_voice(open, 0.5f);
    fresh_voice(muted, 0.5f);
    open.trigger(220.f);
    muted.trigger(220.f);
    tick(open, 960);
    tick(muted, 960);
    muted.set_hold(true);
    muted.update_control(96.f / 48000.f);
    float eo = 0.f, em = 0.f;
    tick(open, 9600, &eo, nullptr);
    tick(muted, 9600, &em, nullptr);
    CHECK(em < eo * 0.5f);
}

TEST_CASE("BodyVoice excitation is bit-exact off at sub level zero") {
    BodyVoice a, b;
    fresh_voice(a, 0.5f);
    fresh_voice(b, 0.5f);
    a.trigger(220.f);
    b.trigger(220.f);
    for (int i = 0; i < 4800; ++i) {
        if (i % 96 == 0) { a.update_control(0.002f); b.update_control(0.002f); }
        b.set_excitation(0.9f);            // fed, but sub level is 0
        float al = 0.f, ar = 0.f, bl = 0.f, br = 0.f;
        a.process(al, ar);
        b.process(bl, br);
        REQUIRE(al == bl);
        REQUIRE(ar == br);
    }
}

TEST_CASE("BodyVoice is deterministic for a given seed") {
    float first[2048], second[2048];
    for (int pass = 0; pass < 2; ++pass) {
        BodyVoice v;
        fresh_voice(v, 0.6f);
        v.trigger(330.f);
        for (int i = 0; i < 2048; ++i) {
            if (i % 96 == 0) v.update_control(0.002f);
            float l = 0.f, r = 0.f;
            v.process(l, r);
            (pass ? second : first)[i] = l;
        }
    }
    for (int i = 0; i < 2048; ++i) CHECK(first[i] == second[i]);
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build
```
Expected: FAIL — `body/body_voice.h` not found.

- [ ] **Step 3: Write the header**

Create `engine/body/body_voice.h`. Copy the drift-LFO members and the
`set_pan` / `set_drift_amount` / equal-power pan arithmetic from
`engine/synth/voice.h` and `voice.cpp` — BODY's MOTION behaviour is identical
to SYNTH's and must not be reinvented.

```cpp
#pragma once
#include <cstdint>
#include "body/exciter.h"
#include "body/mode_bank.h"
#include "mod/rng.h"
#include "PhysicalModeling/KarplusString.h"

namespace spky {

// One BODY voice (spec 2026-07-26 body-resonator, §2):
//
//   Exciter ──┬──→ String A ─┐
//   bus (SUB) ┘    String B ─┼─ MATL morph ─→ pan → vel
//                  ModeBank ─┘
//
// No Svf, no Env: the decay IS the envelope, for both structures. active()
// is an energy follower, not an envelope flag.
//
// Setter names are SYNTH's (the SynthEngineT voice contract); the meanings
// are resonator-native -- see the mapping table in the plan and spec §5.
class BodyVoice {
public:
    void init(float sample_rate, uint32_t seed);

    void trigger(float freq_hz);
    void set_sustaining(bool on);
    void set_pitch_hz(float freq_hz);
    void set_vel(float v);

    void set_env_times(float attack_s, float decay_s);  // exciter length, damping
    void set_morph(float m);                            // MATL
    void set_detune_cents(float max_ct);                // spread + mode stretch
    void set_sub_level(float n);                        // excitation bus level
    void set_cutoff_hz(float hz);                       // brightness
    void set_resonance(float n);                        // exciter character
    void set_pan(float pan);
    void set_drift_amount(float a);
    void set_hold(bool on);                             // palm mute
    void set_excitation(float x);                       // per-sample bus feed

    void update_control(float dt_s);
    void process(float& accL, float& accR);

    bool  active() const { return _follower > kFloor || _hold_samples > 0; }
    float env_value() const { return _follower; }
    float detune_cents() const { return _detune_ct; }

private:
    void _apply_params();

    static constexpr float kFloor = 0.000251f;   // -72 dB
    static constexpr int   kMinHoldSamples = 4800;   // 100 ms

    daisysp::String _str_a, _str_b;
    ModeBank        _bank;
    Exciter         _exciter;
    Rng             _rng;

    float _sr = 48000.f;
    float _freq = 220.f, _matl = 0.f, _detune_ct = 0.f;
    float _damping = 0.5f, _brightness = 0.5f, _sub = 0.f;
    float _vel = 1.f, _vel_target = 1.f;
    float _mix_string = 1.f, _mix_modal = 0.f;   // equal-power MATL gains
    float _excitation = 0.f;
    float _follower = 0.f, _peak = 0.f;
    float _gain_l = 0.70710678f, _gain_r = 0.70710678f;
    float _pan_base = 0.f, _drift_amt = 0.f;
    float _drift_pan_phase = 0.f, _drift_det_phase = 0.f;
    float _drift_pan_hz = 0.1f, _drift_det_hz = 0.1f;
    float _drift_ct_cur = 0.f;
    int   _hold_samples = 0;
    bool  _sustaining = false, _hold = false;
};

} // namespace spky
```

- [ ] **Step 4: Write the implementation**

Create `engine/body/body_voice.cpp`. The three parts that carry the design:

```cpp
#include "body/body_voice.h"
#include <cmath>

namespace spky {

void BodyVoice::_apply_params() {
    // Detune splits +/- half the spread across the two strings, exactly as
    // VoiceT does for its oscillator pair.
    const float half = (_detune_ct + _drift_ct_cur) * 0.5f;
    const float ratio_a = std::pow(2.f, -half / 1200.f);
    const float ratio_b = std::pow(2.f, +half / 1200.f);
    _str_a.SetFreq(_freq * ratio_a);
    _str_b.SetFreq(_freq * ratio_b);

    // MATL rides the string's dispersion up as it heads for the bank: the
    // three sound worlds are one physical axis (spec §3).
    const float damp = _hold ? 0.02f : _damping;
    _str_a.SetDamping(damp);
    _str_b.SetDamping(damp);
    _str_a.SetBrightness(_brightness);
    _str_b.SetBrightness(_brightness);
    _str_a.SetNonLinearity(_matl);
    _str_b.SetNonLinearity(_matl);

    // The bank's stretch comes from DETUNE, its Q from DECAY, its roll-off
    // from FILTER. All of this is control rate -- ModeBank::set_params is the
    // one place the coefficient math is allowed to run.
    const float stretch = _detune_ct / 140.f;
    _bank.set_params(_freq, stretch < 0.f ? 0.f : (stretch > 1.f ? 1.f : stretch),
                     _hold ? 0.f : _damping, _brightness);

    _exciter.set_freq(_freq);

    // Equal-power blend gains for MATL, computed here so process() carries no
    // square root. Global constraint: derived quantities live on the control
    // tick.
    const float m = _matl < 0.f ? 0.f : (_matl > 1.f ? 1.f : _matl);
    _mix_string = std::sqrt(1.f - m);
    _mix_modal  = std::sqrt(m);
}

void BodyVoice::process(float& accL, float& accR) {
    // SUB = 0 hard-gates the bus: bit-exact off (spec §6).
    const float drive = _exciter.process()
                      + (_sub > 0.f ? _excitation * _sub * _sub * 0.5f : 0.f);

    const float string = 0.5f * (_str_a.Process(drive) + _str_b.Process(drive));
    const float modal  = _bank.process(drive);

    // Equal-power blend along MATL. The two gains are computed in
    // _apply_params, NOT here: MATL is a control-rate parameter, so its
    // square roots are control-rate work. Two sqrt per sample per voice would
    // be 16 per sample across a full instrument for a value that changes once
    // per 96 samples.
    const float s = (string * _mix_string + modal * _mix_modal) * _vel;

    const float mag = s < 0.f ? -s : s;
    if (mag > _peak) _peak = mag;
    if (_hold_samples > 0) --_hold_samples;

    accL += s * _gain_l;
    accR += s * _gain_r;
}

void BodyVoice::update_control(float dt_s) {
    // Energy follower: block peak, decaying. This is what active() reads --
    // there is no envelope to ask (spec §1).
    const float fall = _hold ? 0.5f : 0.92f;
    _follower = _peak > _follower ? _peak : _follower * fall;
    _peak = 0.f;

    // Drift LFOs and velocity slew: copy the arithmetic from VoiceT verbatim.
    // ... (see engine/synth/voice.cpp update_control)

    _apply_params();
}

} // namespace spky
```

Fill in `init`, `trigger`, `set_sustaining`, `set_pitch_hz`, the velocity slew
and the drift LFOs from `engine/synth/voice.cpp` — same behaviour, same
constants, copied rather than reinvented so MOTION and DRIFT behave identically
across engines. `trigger` additionally calls `_exciter.strike(_vel_target)` and
sets `_hold_samples = kMinHoldSamples`. `set_sustaining(true)` calls
`_exciter.set_continuous(true)`.

The remaining setters are one line each and all of them only store — every
derivation happens in `_apply_params` on the control tick:

```cpp
void BodyVoice::set_morph(float m)            { _matl = m; }
void BodyVoice::set_detune_cents(float ct)    { _detune_ct = ct; }
void BodyVoice::set_sub_level(float n)        { _sub = n; }
void BodyVoice::set_hold(bool on)             { _hold = on; }
void BodyVoice::set_excitation(float x)       { _excitation = x; }

// ATTACK is exciter length, DECAY is damping (spec §5).
void BodyVoice::set_env_times(float attack_s, float decay_s) {
    _exciter.set_length(attack_s);
    // Longer decay = less damping = longer ring. Curve is tuning material.
    const float d = decay_s / (decay_s + 1.f);
    _damping = d;
}

// RESO is the exciter character, not filter resonance.
void BodyVoice::set_resonance(float n)        { _exciter.set_character(n); }

// FILTER's Hz value becomes brightness on a log map over the engine's own
// 60 Hz - 14 kHz rail.
void BodyVoice::set_cutoff_hz(float hz) {
    const float lo = std::log(60.f), hi = std::log(14000.f);
    float b = (std::log(hz < 60.f ? 60.f : hz) - lo) / (hi - lo);
    _brightness = b < 0.f ? 0.f : (b > 1.f ? 1.f : b);
}
```

- [ ] **Step 5: Set `kVoices` from the Task 4 result**

In `engine/synth/synth_engine.h`, `kVoices` is currently a single
`static constexpr int kVoices = 4` on `SynthEngineT`. If Task 4 chose 4, no
change. If it chose 2 or 1, make the count come from the voice type so only
BODY is reduced:

```cpp
    static constexpr int kVoices = V::kEngineVoices;
```

and add `static constexpr int kEngineVoices = 4;` to `VoiceT` and
`static constexpr int kEngineVoices = <Task 4 value>;` to `BodyVoice`. Rerun
`ctrl_identity` and `wave_formant_sweep` after this change — they must still
hash unchanged.

- [ ] **Step 6: Run tests to verify they pass**

```bash
cmake --build build && ./build/spky_tests.exe -tc="BodyVoice*"
```
Expected: PASS, 6 test cases.

- [ ] **Step 7: Commit**

```bash
git add engine/body/body_voice.h engine/body/body_voice.cpp tests/test_body_voice.cpp CMakeLists.txt engine/synth/synth_engine.h
git commit -m "feat(body): the resonator voice -- strings, bank, MATL morph

One voice carrying a Karplus pair and a 24-mode bank, blended along MATL with
the string's dispersion riding up the same axis. No envelope: active() is an
energy follower with a minimum hold so a quiet strike is not stolen. SUB=0
hard-gates the excitation bus.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 8: BodyEngine — id, alias, scenario parser, contract

**Files:**
- Modify: `engine/parts/engine_iface.h`, `engine/synth/synth_engine.h`, `engine/synth/synth_engine.cpp`, `engine/parts/part.cpp`, `host/render/scenario.cpp:85-88`, `CMakeLists.txt`
- Create: `tests/test_body_engine.cpp`

**Interfaces:**
- Consumes: `spky::BodyVoice` (Task 7), `SynthEngineT<V>` (Task 5).
- Produces: `using BodyEngine = SynthEngineT<BodyVoice>`, `ENGINE_BODY = 4`, and the scenario keyword `"body"`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_body_engine.cpp`:

```cpp
#include "doctest/doctest.h"

#include "synth/synth_engine.h"
#include "synth_engine_contract.h"

using namespace spky;

TEST_CASE("body engine satisfies the shared part-engine contract") {
    contract_round_robin_and_steal<BodyEngine>();
    contract_flow_drone_and_surface<BodyEngine>();
    contract_chord_surface_and_hold<BodyEngine>();
    contract_deterministic_seed<BodyEngine>();
    contract_detune_is_independent_of_source<BodyEngine>();
}

TEST_CASE("body engine SOURCE moves the material, not an oscillator shape") {
    BodyEngine str, bell;
    str.init(48000.f);
    bell.init(48000.f);
    str.set_detune(0.f);
    bell.set_detune(0.f);
    float ts[LANE_COUNT] = {0.f, 1.f, 0.45f, 0.f, 1.f};
    float tb[LANE_COUNT] = {1.f, 1.f, 0.45f, 0.f, 1.f};
    str.set_targets(ts, 0.5f);
    bell.set_targets(tb, 0.5f);
    str.trigger(0.35f);
    bell.trigger(0.35f);
    bool differs = false;
    for (int i = 0; i < 4096; ++i) {
        float sl, sr, bl, br;
        str.process(sl, sr);
        bell.process(bl, br);
        if (sl != bl || sr != br) differs = true;
    }
    CHECK(differs);
}
```

Add the file to `spky_tests` in `CMakeLists.txt`.

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build
```
Expected: FAIL — `BodyEngine` undeclared.

- [ ] **Step 3: Add the id**

In `engine/parts/engine_iface.h`, extend the enum. Ids are appended in
milestone order and never renumbered:

```cpp
enum EngineId {
    ENGINE_TEST_TONE = 0,
    ENGINE_SYNTH = 1,
    ENGINE_SAMPLER = 2,
    ENGINE_WAVE = 3,
    ENGINE_BODY = 4
};
```

- [ ] **Step 4: Add the alias and instantiation**

In `engine/synth/synth_engine.h`, after the WAVE alias:

```cpp
using BodyEngine = SynthEngineT<BodyVoice>;
extern template class SynthEngineT<BodyVoice>;
```

with `#include "body/body_voice.h"` at the top. In
`engine/synth/synth_engine.cpp`, add
`template class spky::SynthEngineT<spky::BodyVoice>;`

- [ ] **Step 5: Wire it into Part**

In `engine/parts/part.cpp`, find the `set_engine` switch that constructs
`WaveEngine` for `ENGINE_WAVE` and add the `ENGINE_BODY` case constructing
`BodyEngine` the same way (same arena/placement idiom — copy the WAVE branch).

- [ ] **Step 6: Teach the scenario parser**

In `host/render/scenario.cpp`, in `parse_engine` (line ~85):

```cpp
    if (s == "body")      return ENGINE_BODY;
```

Add `engine/body/body_voice.cpp` and `engine/body/mode_bank.cpp` to the
`render` target's sources in `CMakeLists.txt` if Task 2 and Task 7 did not
already.

- [ ] **Step 7: Run the whole suite**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: everything PASS, including `ctrl_identity` and `wave_formant_sweep`
(BODY must not perturb SYNTH or WAVE).

- [ ] **Step 8: Commit**

```bash
git add engine/ host/render/scenario.cpp tests/test_body_engine.cpp CMakeLists.txt
git commit -m "feat(body): BodyEngine behind the shared part-engine contract

ENGINE_BODY = 4, appended in milestone order. The engine is
SynthEngineT<BodyVoice>, so allocation, FLOW, chords, steal order and CHOKE
come from the same machine SYNTH and WAVE use -- the contract suite runs
against it unchanged.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Phase 4 — The excitation bus

### Task 9: Tape tap and the SUB gate

**Files:**
- Modify: `engine/fx/part_fx.h`, `engine/fx/part_fx.cpp`, `engine/parts/part.cpp`, `engine/synth/synth_engine.h/.cpp`
- Test: `tests/test_part_fx.cpp` (append), `tests/test_body_engine.cpp` (append)

**Interfaces:**
- Consumes: `BodyVoice::set_excitation` (Task 7).
- Produces: `float PartFx::tape_tap() const` — the previous block's mono echo playback signal, DC-blocked and soft-clipped; and `SynthEngineT::set_excitation(float)` forwarding to every active voice.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_part_fx.cpp`:

```cpp
TEST_CASE("PartFx exposes the previous block's tape tap, not the mix") {
    // Build a PartFx with FLUX on, push a block of signal, and confirm the
    // tap is nonzero on the NEXT block and zero before any audio arrives.
    // Follow the construction idiom used by the other PartFx tests in this
    // file -- read them first and mirror the setup exactly.
    PartFx fx;
    fx.init(48000.f);
    fx.set_flux(0.6f);
    CHECK(fx.tape_tap() == 0.f);
    for (int i = 0; i < 96; ++i) { float l = 0.5f, r = 0.5f; fx.process(l, r); }
    bool nonzero = false;
    for (int i = 0; i < 48000; ++i) {
        float l = 0.f, r = 0.f;
        fx.process(l, r);
        if (i % 96 == 0 && fx.tape_tap() != 0.f) nonzero = true;
    }
    CHECK(nonzero);
}
```

Append to `tests/test_body_engine.cpp`:

```cpp
TEST_CASE("body engine excitation is bit-exact off at SUB 0") {
    BodyEngine gated, fed;
    gated.init(48000.f);
    fed.init(48000.f);
    gated.set_sub(0.f);
    fed.set_sub(0.f);
    float t[LANE_COUNT] = {0.5f, 1.f, 0.45f, 0.f, 1.f};
    gated.set_targets(t, 0.5f);
    fed.set_targets(t, 0.5f);
    gated.trigger(0.35f);
    fed.trigger(0.35f);
    for (int i = 0; i < 9600; ++i) {
        fed.set_excitation(0.8f);
        float gl, gr, fl, fr;
        gated.process(gl, gr);
        fed.process(fl, fr);
        REQUIRE(gl == fl);
        REQUIRE(gr == fr);
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build
```
Expected: FAIL — `tape_tap` and `set_excitation` undeclared.

- [ ] **Step 3: Add the tape tap**

In `engine/fx/part_fx.h/.cpp`, cache the echo playback sample at the end of
each block into a member, DC-block it (`engine/util/onepole.h` high-pass form —
check what the file exposes), soft-clip it with `fast_tanh`, and expose it via
`float tape_tap() const`. The tap is read-only: nothing about FLUX's own signal
path changes.

- [ ] **Step 4: Forward it through the engine**

Add to `SynthEngineT<V>`:

```cpp
    void set_excitation(float x) {
        for (int i = 0; i < kVoices; ++i) _voices[i].set_excitation(x);
    }
```

(Use whatever the class actually calls its voice array — read the private
section first.) In `engine/parts/part.cpp`, once per control tick, call
`engine->set_excitation(_fx.tape_tap())` before the engine's block. Guard the
call so engines that no-op it pay nothing measurable.

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: all PASS. `ctrl_identity` and `wave_formant_sweep` must be unmoved —
`VoiceT::set_excitation` is a no-op, so SYNTH and WAVE cannot hear this.

- [ ] **Step 6: Commit**

```bash
git add engine/fx/part_fx.h engine/fx/part_fx.cpp engine/parts/part.cpp engine/synth/synth_engine.h tests/
git commit -m "feat(body): FLUX tape tap feeds the excitation bus

The part's own echo playback signal, one block late, DC-blocked and soft
clipped, into every active voice. SUB=0 is a hard gate: bit-exact off, and
SYNTH/WAVE reference renders confirm the no-op path costs them nothing.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 10: Cross-deck tap, audio input, and source selection

**Files:**
- Modify: `engine/instrument.h`, `engine/instrument.cpp`, `engine/parts/part.h`, `engine/parts/part.cpp`
- Test: `tests/test_instrument.cpp` (append)

**Interfaces:**
- Consumes: `PartFx::tape_tap()` (Task 9), `IPartEngine::process_in`.
- Produces: `void Part::set_excitation_sources(bool tape, bool other_deck, bool audio_in)` — patch state — and `Instrument` holding each deck's previous-block dry output.

**Ordering.** Every tap is one control block late, so deck A and deck B read
each other's *previous* output. That makes the coupling symmetric and
independent of which deck `Instrument` processes first — no ordering
constraint is introduced.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_instrument.cpp`:

```cpp
TEST_CASE("cross-deck excitation is symmetric and off by default") {
    // Deck A on the synth, deck B on BODY with no notes of its own.
    // With the cross-deck source disabled (the default) B stays silent;
    // with it enabled and SUB up, B rings. Mirror the Instrument setup
    // idiom used by the other tests in this file.
    Instrument quiet, coupled;
    for (Instrument* inst : { &quiet, &coupled }) {
        inst->init(48000.f);
        inst->set_engine(0, ENGINE_SYNTH);
        inst->set_engine(1, ENGINE_BODY);
    }
    coupled.part(1).set_excitation_sources(false, true, false);
    coupled.part(1).set_sub(1.f);

    float e_quiet = 0.f, e_coupled = 0.f;
    for (int i = 0; i < 48000; ++i) {
        float ql = 0.f, qr = 0.f, cl = 0.f, cr = 0.f;
        if (i == 0) { quiet.trigger(0, 0.4f); coupled.trigger(0, 0.4f); }
        quiet.process(0.f, 0.f, ql, qr);
        coupled.process(0.f, 0.f, cl, cr);
        e_quiet += ql * ql;
        e_coupled += cl * cl;
    }
    CHECK(e_coupled > e_quiet);
}

TEST_CASE("two BODY decks with the bus hot stay bounded") {
    Instrument inst;
    inst.init(48000.f);
    inst.set_engine(0, ENGINE_BODY);
    inst.set_engine(1, ENGINE_BODY);
    for (int p = 0; p < 2; ++p) {
        inst.part(p).set_excitation_sources(true, true, true);
        inst.part(p).set_sub(1.f);
    }
    inst.trigger(0, 0.5f);
    inst.trigger(1, 0.5f);
    for (int i = 0; i < 48000 * 10; ++i) {
        float l = 0.f, r = 0.f;
        inst.process(0.3f, 0.3f, l, r);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
        REQUIRE(std::fabs(l) < 8.f);
        REQUIRE(std::fabs(r) < 8.f);
    }
}
```

Read the actual `Instrument` / `Part` accessor names in
`engine/instrument.h` and `engine/parts/part.h` before writing — `part(int)`,
`trigger(int, float)` and `process(...)` above are placeholders for whatever
those headers really expose, and the test must use the real names.

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build
```
Expected: FAIL — `set_excitation_sources` undeclared.

- [ ] **Step 3: Hold the previous block's deck output**

In `engine/instrument.h/.cpp`, add two floats holding each part's previous-block
mono dry output (the signal *before* the master join), updated once per control
block. Feed part 0's engine with part 1's value and vice versa.

- [ ] **Step 4: Sum the bus in Part**

In `engine/parts/part.h/.cpp`, add the three enable flags (default: tape on,
other deck off, audio in off) and build the bus:

```cpp
    float bus = 0.f;
    if (_src_tape)  bus += _fx.tape_tap();
    if (_src_deck)  bus += _other_deck_tap;
    if (_src_audio) bus += _audio_in_tap;
    bus = fast_tanh(_dc.process(bus));
    _engine->set_excitation(bus);
```

`_audio_in_tap` is the mono sum of the last block's `process_in` arguments.
The `fast_tanh` soft clip plus SUB² ≤ 0.5 in the voice plus the body's own
damping are what bound the loop (spec §6).

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```
Expected: all PASS, `ctrl_identity` and `wave_formant_sweep` unmoved.

- [ ] **Step 6: Commit**

```bash
git add engine/instrument.h engine/instrument.cpp engine/parts/part.h engine/parts/part.cpp tests/test_instrument.cpp
git commit -m "feat(body): cross-deck and audio-in excitation sources

Each tap is one block late, which makes deck-to-deck coupling symmetric and
free of any processing-order constraint. Soft clip, SUB^2 <= 0.5 and the
body's own damping bound the loop; two BODY decks with everything hot stay
finite over ten seconds.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Phase 5 — Hosts, scenarios, final bench

### Task 11: VCV surface

**Files:**
- Modify: `host/vcv/src/Spotymod.cpp`
- Test: `tests/test_vcv_form_song_migration.cpp` (read for the persistence idiom; add a migration case if the patch schema grows)

**Interfaces:**
- Consumes: `ENGINE_BODY` (Task 8), `Part::set_excitation_sources` (Task 10).
- Produces: a sixth engine-button state and three per-deck context-menu checkboxes persisted in the patch.

- [ ] **Step 1: Extend the engine button**

Find the ENGINE_A/ENGINE_B parameter handling in `host/vcv/src/Spotymod.cpp`
and raise the state count from five to six, with BODY appended after WAVE
(`EngineId` order: test-tone → synth → sampler → wave → body). Give it its LED
shade following the existing per-engine shade table. The panel does not change.

- [ ] **Step 2: Add the context-menu checkboxes**

In the per-deck context menu (the Detune A/B entry is the precedent), add three
checkboxes — "Excite: FLUX tape", "Excite: other deck", "Excite: audio in" —
calling `Part::set_excitation_sources`. Persist them in `dataToJson` /
`dataFromJson` alongside the existing per-deck settings, defaulting to
tape-on / deck-off / audio-off so old patches load unchanged.

- [ ] **Step 3: Build the VCV plugin**

```bash
cd host/vcv && ./build-local.sh
```
Never hand-roll this build — the system `g++` is the ARM cross-compiler and
will fail with "MinGW not found".

- [ ] **Step 4: Verify by hand in VCV Rack**

Load the plugin, cycle the engine button to BODY on deck B, turn SOURCE from
0 to 1 while a note rings, and confirm the material moves from plucked to bell.
Toggle each excitation checkbox and confirm the patch reloads with them intact.

- [ ] **Step 5: Commit**

```bash
git add host/vcv/src/Spotymod.cpp
git commit -m "feat(vcv): BODY on the engine button, excitation sources in the menu

Sixth engine state in EngineId order. The three excitation sources are patch
state, not performance controls -- checkboxes in the per-deck menu, persisted,
defaulting to tape-only so existing patches load unchanged.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 12: Demo scenarios and render gates

**Files:**
- Create: `host/render/scenarios/body_strum.json`, `body_bow.json`, `body_sympathetic.json`
- Modify: `CMakeLists.txt` (three `add_test` blocks)

**Interfaces:**
- Consumes: the `"body"` scenario keyword (Task 8) and the excitation sources (Task 10).
- Produces: three reference renders with SHA-256 gates.

- [ ] **Step 1: Write the scenarios**

Read an existing scenario — `host/render/scenarios/wave_formant_sweep.json` —
for the exact schema, then write:

- `body_strum.json` — STEP, chord layer on, MATL swept 0 → 1 across the render: plucked harp → prepared piano → struck bells.
- `body_bow.json` — FLOW drone, `set_engine` body, exciter in the noise zone, SUB swept up into tape self-oscillation, CHOKE at the end.
- `body_sympathetic.json` — deck A on the sampler playing the factory loop, deck B on BODY with the cross-deck source enabled and no notes of its own.

- [ ] **Step 2: Render and listen**

```bash
./build/render.exe host/render/scenarios/body_strum.json /tmp/body_strum.wav /tmp/body_strum.csv
```
Repeat for the other two. **Listen to all three.** The response curves in
Tasks 2, 6 and 7 are explicitly tuning material — this is the pass where they
get set by ear. Adjust `kPosition`, the exciter zone boundaries, the MATL
taper and the damping map until it sounds right, then re-render.

- [ ] **Step 3: Add the hash gates**

Take the SHA-256 of each accepted WAV and add three `add_test` blocks to
`CMakeLists.txt`, following the `wave_formant_sweep` block exactly:

```cmake
add_test(
    NAME body_strum
    COMMAND ${CMAKE_COMMAND}
        -DRENDER=$<TARGET_FILE:render>
        -DSCENARIO=${CMAKE_SOURCE_DIR}/host/render/scenarios/body_strum.json
        -DEXPECTED=<sha256 of the accepted render>
        -DOUT_DIR=${CMAKE_BINARY_DIR}
        -DGATE_STEM=body_strum
        -DREFERENCE=BODY
        -P ${CMAKE_SOURCE_DIR}/tests/check_render_hash.cmake
)
```

- [ ] **Step 4: Run the suite**

```bash
ctest --test-dir build --output-on-failure
```
Expected: all PASS including the three new gates.

- [ ] **Step 5: Commit**

```bash
git add host/render/scenarios/body_*.json CMakeLists.txt renders/
git commit -m "test(body): three reference renders -- strum, bow, sympathetic

Tuning pass done by ear; the accepted renders are now hash gates.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 13: Final bench and status

**Files:**
- Modify: `bench/workloads_body.cpp`, `README.md`, `docs/roadmap.md`
- Create: `docs/bench/YYYY-MM-DD-<githash>.md` and `.csv`

**Interfaces:**
- Consumes: `BodyEngine` (Task 8), the excitation bus (Task 10).
- Produces: the measured rows the spec's §7 gate names, and the milestone's status change.

- [ ] **Step 1: Add the three engine-level rows**

In `bench/workloads_body.cpp`, add — mirroring
`bench/workloads_system.cpp`'s `setup_engine_2x4` / `proc_engine_2x4` pattern:

- `body_2x4` — both parts on `BodyEngine`, MATL at 1.0 (the modal end; both structures run, so this is the worst case)
- `body_2x4_string` — the same at MATL 0.0, the ablation that prices the bank in context
- `inst_body_worst` — BODY on both decks inside the full FX chain with the excitation bus hot, mirroring `setup_inst_worst`

- [ ] **Step 2: Run the bench on hardware**

```bash
cd bench && python run.py
```
Expected: exit 0, two agreeing runs, capture written.

- [ ] **Step 3: Check against the spec's estimate**

The spec predicts `body_2x4` at 280k–311k cycles (29–32 % of the 960,000-cycle
block). Record the measured figure next to the prediction in the bench doc's
prose. If `inst_body_worst` exceeds 100 % of the block on its maximum, apply
the ladder from Task 4 — reduce `kVoices`, not the mode count — and re-run.

- [ ] **Step 4: Check the memory claim**

The bench build prints the region table. Spec §8 predicts ≈ 84 KB of static
SRAM for BODY (80 KB of string delay lines + ≈ 4 KB of bank state). Compare
the `SRAM` row against the same build with `ENGINE_BODY` unreferenced:

```bash
cd bench && python run.py --build-only
arm-none-eabi-size -A build/bench.elf | head -20
```

Two things must hold: the string delay lines are in **SRAM, not SDRAM** (they
are random-access — the cache trap the research flagged), and `SRAM_EXEC` stays
under 262,880 B. If a delay line landed in SDRAM, a `DSY_SDRAM_BSS` annotation
leaked in from the buffer code — remove it.

- [ ] **Step 5: Update the status**

In `docs/roadmap.md`, move M5j from Planned to Done with the measured figures,
following the shape of the M5i (WAVE) entry. In `README.md`, change the M5j row
to **done**. State plainly whether the estimate held.

- [ ] **Step 6: Commit**

```bash
git add bench/ docs/ README.md
git commit -m "bench(body): BODY priced on the Seed; M5j done

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

- [ ] **Step 7: Finish the branch**

Use `superpowers:finishing-a-development-branch` to decide how
`body-resonator-engine` merges into `main`.

---

## Notes for the implementer

**The one thing that must not slip.** Every derived quantity in this engine —
mode coefficients, string setters, exciter parameters — belongs in
`update_control`, never in `process`. `ModeBank::coeff_updates()` and its test
exist to catch a regression here, but they only cover the bank. If a `powf`,
`sinf`, `tanhf` or division appears in a per-sample path during review, that is
a defect regardless of whether tests pass: measured, those cost 198, 117, 208
and ~15 cycles per call, and there are 96 samples in a block and up to 8 voices.

**Tuning material versus contract.** The spec is explicit that response curves
(damping map, MATL taper, exciter zone boundaries, strike position) are to be
set by ear in Task 12, not derived. Do not "fix" a curve that sounds
deliberate. The contract is the mapping table in Task 7 and spec §5.

**Reference renders are gates, not outputs.** If `ctrl_identity` or
`wave_formant_sweep` moves at any point in Phases 2–5, the change is a bug.
Never update the expected hash to make a test pass.
