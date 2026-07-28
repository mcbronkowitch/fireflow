# FLUX becomes a bucket-brigade delay — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Spec:** `docs/superpowers/specs/2026-07-27-flux-bbd-delay-design.md`

**Goal:** Replace FLUX's tape echo and its two rhythm-fed taps with a physical bucket-brigade model whose clock rate *is* the delay time, so that changing time bends pitch, bandwidth follows the clock, and the reclaimed budget buys a Deluxe-Memory-Man voicing that keeps going past where the pedal stops.

**Architecture:** A new `engine/fx/bbd.{h,cpp}` carries three units with one job each — `BbdLine` (the Holters/Parker DAFx-18 model: no read pointer, an N-cell array clocked forward, input and output filters carried as partial-fraction parallel branches so the resampling interpolation is done *by* the filters), `Compander` (the NE570 pair, τ = 10 ms), and `BbdEcho` (DRIVE → saturate → compress → line → expand → feedback). `Flux` keeps its class, its name, its `SoftSwitch` and its bit-exact off path; it loses `EchoDelay`, `TapeBpf`, `DeLine` and the whole tap bank. Between physics and music sits one pure free function, `bbd_clock_hz(delay_seconds, stages)`.

**Tech Stack:** C++17, no heap in `engine/**`, injected memory (`FxMem`), doctest for unit tests, CMake+Ninja for the desktop build, `host/vcv/build-local.sh` for the Rack plugin, the ARM bench for the CPU gate.

---

## Global Constraints

Every task's requirements implicitly include this section.

- **C++17.** No `std::complex`, no `<memory>`, no heap allocation anywhere under `engine/**`. All buffers are injected by the host (`FxMem`).
- **No libm in per-sample paths where a `util/fast_*.h` exists.** `fast_tanh` is the saturator. `std::sqrt` is permitted (single `VSQRT.F32` on the M7); `powf`/`expf`/`cosf`/`sinf` are permitted **only** at init time or at control rate behind an unchanged-value guard.
- **Attribution.** `engine/fx/bbd.{h,cpp}` are derived from `jpcima/bbd-delay-experimental` (Boost Software License 1.0, MIT-compatible). Both files carry the BSL-1.0 notice verbatim in their header comment; `THIRD_PARTY.md` gets a full entry. The repo itself stays MIT.
- **VCV param ids are append-only.** DUST/ROT are **renamed in place** — same positions in `PARAMS`, same ids, `PART_STRIDE` stays 23. Nothing is inserted, nothing shifts. `gen_panel.py:232-234`'s append-only rule is never engaged.
- **By-ear values are off limits** unless this plan names them as a decision point. The ones this plan *does* set (`kFilterHz`, `kCompRef`, `kSatCeil`, the DRIVE range, `kLossCoef`) are physics-derived starting points and are explicitly flagged as ear-tunable in Task 12.
- **Feedback keeps its 1.2 over unity** (`flux.cpp:55`). Self-oscillation must stay reachable.
- **Bit-exact off path.** `Flux::process`'s `if (_sw.is_idle()) return;` early return is untouched and stays covered by a test.
- **Commits** end with `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **Desktop build:** `source env.sh && cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure`. Never MSVC.
- **VCV build:** `cd host/vcv && ./build-local.sh`. Never hand-rolled — the system `g++` is the ARM cross-compiler.
- **No bit-exactness gates.** Renders are sanity checks; this project does not add checksum/byte-identity gates for new audio paths.

---

## Two numbers that must be right before anything is written

Both fall out of the reference implementation and both differ from a naive reading of the spec. They are stated once, here, because every task downstream depends on them.

**1. The two-phase clock, and why the buffer is half the stage count.**

The model alternates: **even ticks write** one cell, **odd ticks read** one cell. That alternation *is* the BBD's two-phase clock. So:

```
ticks per audio sample  = 2 · f_clk / fs         (fed to BbdLine::SetClock)
cells (floats) in the line = stages / 2
delay = 2 · cells / tick_rate = stages / (2 · f_clk)      ← the spec's law, exactly
```

The spec sizes `FxMem::echo` at `kMaxStages` = 16384 floats per channel. The correct size is **`kMaxStages / 2` = 8192 floats** — a two-phase BBD stores one sample per two stages. Memory is therefore **128 KB across all four lines, not 256 KB**; the spec's headline shrink (4.19 MB → far less) holds with room to spare. `Flux::kMaxSamples` keeps its name and its meaning ("floats per channel the host must provide") and only changes value, so no consumer outside `flux.h` needs touching.

**2. Events per sample.** At the 32 kHz ceiling the tick rate is `2 · 32000 / 48000` = **1.33 ticks per sample** — 0.67 write events and 0.67 read events. The spec's "0.67 events per sample" is per kind, and both kinds cost roughly the same. Task 11's bench gate measures the real number; do not argue with it.

**3. The reference is already scalar.** Spec risk 3 says "SIMD (`SSEComplex` and friends) must be rewritten scalar". That applies to the ChowDSP/Surge variants of this model, not to `jpcima/bbd-delay-experimental`, which is plain `std::complex<double>`. What actually has to change in the port: `double` → `float`, `std::complex` → a local `Cf` struct, `std::vector`/`unique_ptr` → injected memory and fixed-size arrays, and the `std::mutex` filter cache → a single init-time build. The `powf`/`cosf` risk is also already solved upstream: the reference precomputes `exp`/`pow` into an interpolation table (`G`) at init and only lerps in the hot path.

---

## File Structure

| File | Responsibility |
|---|---|
| `engine/fx/bbd.h` (new) | `Cf` complex helpers, `bbd_tuning` constants, `bbd_clock_hz`, `bbd_time_mult`, `BbdFilterCoef`, `BbdLine`, `Compander`, `BbdEcho`. Header-heavy so the per-sample path inlines, matching `flux.h`'s existing shape. |
| `engine/fx/bbd.cpp` (new) | Init-time only: Butterworth pole/residue derivation, the `G` table build, the two shared `BbdFilterCoef` statics. |
| `engine/fx/flux.h` / `flux.cpp` | Music, not physics: ladder → seconds → clock, the two smoothers, mix, feedback, the `SoftSwitch`. Loses `DeLine`, `TapeBpf`, `EchoDelay`, `TapBank`. |
| `engine/fx/taps.{h,cpp}` | **Deleted.** |
| `engine/fx/part_fx.{h,cpp}` | Two setters renamed; `FXT_FLUX_TIME` wired through. |
| `engine/instrument.{h,cpp}` | Two forwarders renamed; the rhythm cross-feed deleted. |
| `engine/parts/part.h` | `FXT_FLUX_TIME` base default 0.4 → 0.5 (the neutral ×1). |
| `tests/test_bbd.cpp` (new) | Replaces `tests/test_taps.cpp`. The core against physics. |
| `bench/workloads_bbd.cpp` (new) | Replaces `bench/workloads_taps.cpp`. The CPU gate. |
| `host/vcv/**`, `host/render/**`, `bench/audition/**` | Rename DUST→DRIVE, ROT→STAGES; new scenario actions. |

---

### Task 1: `bbd_clock_hz` — the one formula

The formula everybody would otherwise doubt while debugging. Pure, stateless, no dependencies, and it lands in the build lists first so every later task has a place to put code.

**Files:**
- Create: `engine/fx/bbd.h`, `engine/fx/bbd.cpp`
- Create: `tests/test_bbd.cpp`
- Modify: `CMakeLists.txt:86-89` (tests list), `CMakeLists.txt:162-165` (render list), `bench/Makefile:94-99`, `host/vcv/Makefile:46-49`

**Interfaces:**
- Produces: `spky::bbd_tuning::*` constants; `float spky::bbd_clock_hz(float delay_seconds, int stages)`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_bbd.cpp`:

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include "fx/bbd.h"
using namespace spky;

TEST_CASE("bbd_clock_hz: f_clk = stages / (2 * t_d)") {
    // 8192 stages at 250 ms -> 16384 Hz. The DMM's own numbers: 8192 stages
    // at 550 ms -> 7447 Hz, against the EH-7850 factory calibration document's
    // measured clock period of 120-140 us (7143-8333 Hz) at maximum delay.
    CHECK(bbd_clock_hz(0.25f, 8192) == doctest::Approx(16384.f));
    CHECK(bbd_clock_hz(0.550f, 8192) == doctest::Approx(7447.27f).epsilon(0.001));
    CHECK(bbd_clock_hz(1.0f, 4096) == doctest::Approx(2048.f));
}

TEST_CASE("bbd_clock_hz: the 32 kHz ceiling is hard") {
    // At 8192 stages the ceiling engages below 128 ms -- exactly where the
    // fixed post-BBD filter chain dominates anyway (spec "The clock law").
    CHECK(bbd_clock_hz(0.128f, 8192) == doctest::Approx(32000.f));
    CHECK(bbd_clock_hz(0.001f, 16384) == bbd_tuning::kClockMaxHz);
    CHECK(bbd_clock_hz(1e-9f, 16384) == bbd_tuning::kClockMaxHz);
}

TEST_CASE("bbd_clock_hz: there is no floor, and no way to return garbage") {
    // The mud at the long end is the point -- a 10 s delay at 512 stages runs
    // the line at 25.6 Hz and that is allowed. What is NOT allowed is a
    // non-finite or negative clock reaching BbdLine.
    CHECK(bbd_clock_hz(10.f, 512) == doctest::Approx(25.6f));
    CHECK(bbd_clock_hz(0.f, 8192) == bbd_tuning::kClockMaxHz);
    CHECK(bbd_clock_hz(-1.f, 8192) == bbd_tuning::kClockMaxHz);
    CHECK(std::isfinite(bbd_clock_hz(NAN, 8192)));
}

TEST_CASE("bbd_tuning: the ceiling buys twice the bandwidth the chain needs") {
    // 32 kHz / 4 = 8 kHz of BBD bandwidth against a fixed filter chain at
    // ~3.6 kHz: inaudible by construction, and it bounds the worst case at
    // 2 * 32000 / 48000 = 1.33 ticks per sample.
    CHECK(bbd_tuning::kClockMaxHz * 0.25f > 2.f * bbd_tuning::kFilterHz);
    CHECK(2.f * bbd_tuning::kClockMaxHz / 48000.f < 1.5f);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
source env.sh && cmake -S . -B build && cmake --build build 2>&1 | tail -20
```

Expected: FAIL at compile — `fx/bbd.h: No such file or directory`. (`tests/test_bbd.cpp` is not in `CMakeLists.txt` yet either; add it in step 3 together with the header.)

- [ ] **Step 3: Write the header**

Create `engine/fx/bbd.h`:

```cpp
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "util/math.h"

// Bucket-brigade delay: the Holters/Parker combined model of a BBD and its
// input/output filters (DAFx-18), with the NE570 compander and the drive path
// of an Electro-Harmonix Deluxe Memory Man around it.
//
// Derived from jpcima/bbd-delay-experimental (bbd_line, bbd_filter), used
// under the Boost Software License 1.0:
//
//   Boost Software License - Version 1.0 - August 17th, 2003
//
//   Permission is hereby granted, free of charge, to any person or
//   organization obtaining a copy of the software and accompanying
//   documentation covered by this license (the "Software") to use, reproduce,
//   display, distribute, execute, and transmit the Software, and to prepare
//   derivative works of the Software, and to permit third-parties to whom the
//   Software is furnished to do so, all subject to the following:
//
//   The copyright notices in the Software and this entire statement,
//   including the above license grant, this restriction and the following
//   disclaimer, must be included in all copies of the Software, in whole or
//   in part, and all derivative works of the Software, unless such copies or
//   derivative works are solely in the form of machine-executable object code
//   generated by a source language processor.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND
//   NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR ANYONE
//   DISTRIBUTING THE SOFTWARE BE LIABLE FOR ANY DAMAGES OR OTHER LIABILITY,
//   WHETHER IN CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
//   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//   SOFTWARE.
//
// Deviations from the reference, all deliberate: float instead of double, a
// local Cf instead of std::complex, injected memory instead of std::vector /
// unique_ptr, one init-time filter build instead of a mutex-guarded cache,
// our own filter chain (the DMM's ~3.6 kHz, not the Juno-60's ~7-10 kHz), and
// a charge-transfer loss pole the reference does not model at all.

namespace spky {

namespace bbd_tuning {

// --- the clock law ---------------------------------------------------------
// f_clk = stages / (2 * t_d), clamped. The ceiling is physical, not
// arbitrary: the fixed post-BBD filter chain sits at kFilterHz and the clock
// never overtakes it, so BBD bandwidth past kClockMaxHz/4 = 8 kHz is
// inaudible by construction. It also bounds the tick rate at
// 2 * 32000 / 48000 = 1.33 per sample, which is what makes the budget
// computable. There is deliberately NO floor.
constexpr float kClockMaxHz = 32000.f;

// --- stages ----------------------------------------------------------------
// Geometric, five octaves. 8192 is the DMM (2x MN3005 at 4096 stages each);
// the rest of the scale is a chip no pedal exposes.
constexpr int kMinStages = 512;
constexpr int kMaxStages = 16384;

// --- the fixed filter chain ------------------------------------------------
// The real device's chain is roughly six poles around 3.5-4 kHz and does NOT
// move with the clock. Modelled as two 3rd-order Butterworth sections, one
// before the line and one after: six poles, one corner, analytic residues.
// Ear-tunable (spec: the filters are the device's character, not a setting).
constexpr float kFilterHz  = 3600.f;
constexpr int   kFiltOrder = 3;

// Rows in the G interpolation table. The tick's sub-sample position d lands
// between two rows and is lerped. 65 rows over exp(p*ts*d), |p*ts| <= 0.48 at
// 48 kHz, gives a relative error below 1e-5 -- far under the model's own
// error. The table is fs-dependent only, and shared by all four lines.
constexpr int kInterpSteps = 65;

// --- charge-transfer loss --------------------------------------------------
// Bandwidth follows the clock: f_-3dB ~ f_clk / 4 over 8192 stages. One
// one-pole INSIDE the clocked domain (i.e. running at the line's own sample
// rate, f_clk) with a fixed normalised corner of 1/4 tracks the clock for
// free -- no coefficient recompute when the clock moves, and the corner is
// correct at every clock by construction.
//
// For y[n] = y[n-1] + a*(x - y[n-1]) with b = 1-a, half power at w = pi/2
// requires 1 + b^2 = 2*(1-b)^2, i.e. b = 2 - sqrt(3). So a = sqrt(3) - 1.
constexpr float kLossCoef = 0.7320508f;   // sqrt(3) - 1

// --- the NE570 compander ---------------------------------------------------
// 2:1 in, 1:2 out, tau = 10 kOhm * 1 uF. Not a parameter: it is the device's
// character, and "compression" is already spoken for by COMP on the panel.
constexpr float kCompTauS = 0.010f;
constexpr float kCompRef  = 0.1f;         // -20 dBFS reference level
// Envelope floors/ceilings that bound the compander's gain to [1/4, 4] in
// each direction. Derived, not tasted: the compressor's gain is
// sqrt(kCompRef/e), so e >= kCompRef/16 caps it at 4; the expander's gain is
// e/kCompRef, so e in [kCompRef/4, 4*kCompRef] caps it at 4 either way.
constexpr float kCompFloorC = kCompRef / 16.f;
constexpr float kCompFloorE = kCompRef / 4.f;
constexpr float kCompCeilE  = kCompRef * 4.f;

// --- drive -----------------------------------------------------------------
// DRIVE sits INSIDE the loop, so every repeat saturates again. The threshold
// is fixed at the MN3005's 0.9 V_RMS ceiling; DRIVE moves the signal against
// it, with makeup gain after the saturator so that small-signal loop gain --
// and therefore FEEDBACK's meaning -- does not move with DRIVE.
constexpr float kSatCeil   = 0.9f;
constexpr float kDriveLoDb = -6.f;
constexpr float kDriveHiDb = 24.f;

}  // namespace bbd_tuning

// The one formula. Pure, stateless, separately tested. Everything else in
// this file trusts it.
//
// A non-finite or non-positive delay returns the ceiling rather than an
// infinity: BbdLine must never see a clock it cannot count ticks from, and a
// silent, very short delay is a far better failure than a NaN in the line.
inline float bbd_clock_hz(float delay_seconds, int stages) {
    if (!(delay_seconds > 0.f) || !std::isfinite(delay_seconds))
        return bbd_tuning::kClockMaxHz;
    const float hz = static_cast<float>(stages) / (2.f * delay_seconds);
    return hz < bbd_tuning::kClockMaxHz ? hz : bbd_tuning::kClockMaxHz;
}

}  // namespace spky
```

Create `engine/fx/bbd.cpp` with only the include for now (Task 2 fills it):

```cpp
#include "fx/bbd.h"

// Init-time only. Everything in the per-sample path lives in bbd.h so it
// inlines, the same split flux.h/flux.cpp already uses.
```

- [ ] **Step 4: Add both files to all four build lists**

`CMakeLists.txt` — in the `spky_tests` list, after `tests/test_flux.cpp` (line 87):

```cmake
    engine/fx/bbd.cpp
    tests/test_bbd.cpp
```

`CMakeLists.txt` — in the `render` list, after `engine/fx/flux.cpp` (line 163):

```cmake
    engine/fx/bbd.cpp
```

`bench/Makefile` — in `CPP_SOURCES`, after `../engine/fx/flux.cpp` (line 95):

```make
	../engine/fx/bbd.cpp \
```

`host/vcv/Makefile` — in the engine `SOURCES`, after `$(REPO)/engine/fx/flux.cpp` (line 46):

```make
	$(REPO)/engine/fx/bbd.cpp \
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
source env.sh && cmake --build build && ./build/spky_tests -tc="bbd_clock_hz*,bbd_tuning*"
```

Expected: 4 test cases, all assertions pass.

- [ ] **Step 6: Commit**

```bash
git add engine/fx/bbd.h engine/fx/bbd.cpp tests/test_bbd.cpp CMakeLists.txt bench/Makefile host/vcv/Makefile
git commit -m "feat(bbd): the clock law, and a home for the BBD model"
```

---

### Task 2: The filter model — partial fractions and the G table

The model's whole trick: the input and output filters are carried as partial-fraction parallel branches, so the resampling interpolation is done *by* the filters rather than before them. This task builds the analog spec (six Butterworth poles at 3.6 kHz), converts it to the discretised `G`/`P`/`H` form, and proves the result really is the filter it claims to be.

**Files:**
- Modify: `engine/fx/bbd.h` (add `Cf`, `BbdFilterCoef`, the accessors)
- Modify: `engine/fx/bbd.cpp` (the build)
- Modify: `tests/test_bbd.cpp`

**Interfaces:**
- Consumes: `bbd_tuning::kFilterHz`, `kFiltOrder`, `kInterpSteps` (Task 1).
- Produces:
  - `struct spky::Cf { float re, im; }` plus `cf_add`, `cf_mul`, `cf_div`, `cf_scale`, `cf_exp`.
  - `struct spky::BbdFilterCoef { Cf G[kInterpSteps][kFiltOrder]; Cf P[kFiltOrder]; float H; void interpolate_g(float d, Cf* g) const; }`
  - `const BbdFilterCoef& spky::bbd_filter_in(float sample_rate);`
  - `const BbdFilterCoef& spky::bbd_filter_out(float sample_rate);`
  - `void spky::bbd_analog_spec(bool output_kind, Cf* poles, Cf* residues);` — exposed for tests.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_bbd.cpp`:

```cpp
// --- the fixed filter chain -------------------------------------------------
// The analog spec is H(s) = sum_m R[m] / (s - P[m]). These tests pin that it
// really is a 3rd-order Butterworth at kFilterHz, because every claim the
// model makes downstream ("the filters do not move", "long delays are dark
// because of the CLOCK, not the filters") rests on this chain being the one
// the DMM has and staying where it is put.

static float analog_mag_db(bool out_kind, float hz) {
    Cf poles[bbd_tuning::kFiltOrder], res[bbd_tuning::kFiltOrder];
    bbd_analog_spec(out_kind, poles, res);
    const Cf s{ 0.f, TWO_PI * hz };
    Cf h{ 0.f, 0.f };
    for (int m = 0; m < bbd_tuning::kFiltOrder; ++m)
        h = cf_add(h, cf_div(res[m], Cf{ s.re - poles[m].re, s.im - poles[m].im }));
    return 20.f * std::log10(std::sqrt(h.re * h.re + h.im * h.im));
}

TEST_CASE("bbd filter: DC gain is exactly unity") {
    // H(0) = sum(-R/P) == 1. If this drifts, every level in the chain drifts
    // with it and the compander's reference stops meaning anything.
    CHECK(analog_mag_db(false, 0.f) == doctest::Approx(0.f).epsilon(0.001));
    CHECK(analog_mag_db(true,  0.f) == doctest::Approx(0.f).epsilon(0.001));
}

TEST_CASE("bbd filter: -3 dB at kFilterHz, -18 dB/oct above it") {
    CHECK(analog_mag_db(false, bbd_tuning::kFilterHz)
          == doctest::Approx(-3.0103f).epsilon(0.01));
    // Butterworth order 3: one octave up is -18 dB, two octaves -36 dB
    // (asymptotically). Generous windows -- the shape is what is pinned.
    const float oct1 = analog_mag_db(false, 2.f * bbd_tuning::kFilterHz);
    const float oct2 = analog_mag_db(false, 4.f * bbd_tuning::kFilterHz);
    CHECK(oct1 < -16.f);
    CHECK(oct1 > -22.f);
    CHECK(oct2 - oct1 < -16.f);
    CHECK(oct2 - oct1 > -20.f);
}

TEST_CASE("bbd filter: every pole is in the left half plane") {
    // A pole with Re >= 0 makes P[m] = exp(ts*p) leave the unit disc and the
    // parallel branches diverge -- silently, over seconds. This is the guard
    // that a hand-edited kFilterHz or kFiltOrder cannot get past.
    for (bool out_kind : { false, true }) {
        Cf poles[bbd_tuning::kFiltOrder], res[bbd_tuning::kFiltOrder];
        bbd_analog_spec(out_kind, poles, res);
        for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
            CHECK(poles[m].re < 0.f);
            CHECK(std::isfinite(res[m].re));
            CHECK(std::isfinite(res[m].im));
        }
    }
}

TEST_CASE("bbd filter: the discretised poles sit strictly inside the unit disc") {
    const BbdFilterCoef& fin = bbd_filter_in(48000.f);
    const BbdFilterCoef& fout = bbd_filter_out(48000.f);
    for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
        const float rin = std::sqrt(fin.P[m].re * fin.P[m].re + fin.P[m].im * fin.P[m].im);
        const float rout = std::sqrt(fout.P[m].re * fout.P[m].re + fout.P[m].im * fout.P[m].im);
        CHECK(rin < 1.f);
        CHECK(rout < 1.f);
        CHECK(rin > 0.f);
    }
    // H is the output filter's DC feed-through term, sum(-R/P) == H(0) == 1.
    CHECK(fout.H == doctest::Approx(1.f).epsilon(0.001));
}

TEST_CASE("bbd filter: interpolate_g matches the table at both endpoints") {
    // d == 0 and d == 1 must hit rows 0 and N-1 exactly, not one row short.
    // An off-by-one here shows up as a faint, clock-rate-dependent whine that
    // is very hard to attribute later.
    const BbdFilterCoef& f = bbd_filter_in(48000.f);
    Cf g[bbd_tuning::kFiltOrder];
    f.interpolate_g(0.f, g);
    for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
        CHECK(g[m].re == doctest::Approx(f.G[0][m].re));
        CHECK(g[m].im == doctest::Approx(f.G[0][m].im));
    }
    f.interpolate_g(1.f, g);
    for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
        CHECK(g[m].re == doctest::Approx(f.G[bbd_tuning::kInterpSteps - 1][m].re));
        CHECK(g[m].im == doctest::Approx(f.G[bbd_tuning::kInterpSteps - 1][m].im));
    }
    // And it must not read past the table for d slightly out of range.
    f.interpolate_g(1.0001f, g);
    CHECK(std::isfinite(g[0].re));
}

TEST_CASE("bbd filter: the table is built once per sample rate") {
    // Four lines call this at init. Rebuilding the 3 kB of tables four times
    // would be harmless but silly; rebuilding them from a DIFFERENT sample
    // rate and handing the stale result to the other lines would not be.
    const BbdFilterCoef& a = bbd_filter_in(48000.f);
    const BbdFilterCoef& b = bbd_filter_in(48000.f);
    CHECK(&a == &b);
    const BbdFilterCoef& c = bbd_filter_in(44100.f);
    CHECK(&c == &a);                       // same storage, rebuilt in place
    CHECK(c.P[0].re != doctest::Approx(0.f));
    // Put it back so later cases in this file see 48 kHz tables.
    bbd_filter_in(48000.f);
    bbd_filter_out(48000.f);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
source env.sh && cmake --build build 2>&1 | tail -20
```

Expected: FAIL at compile — `'Cf' was not declared`, `'bbd_analog_spec' was not declared`.

- [ ] **Step 3: Add the complex helpers and the coef struct to `bbd.h`**

Insert into `namespace spky`, above `namespace bbd_tuning`:

```cpp
// Minimal complex float. std::complex<float> would do the same job, but it
// drags <complex> into every translation unit that includes this header and
// its operator* is not guaranteed to avoid the NaN-safe slow path on every
// toolchain. Six lines here, no surprises on the M7.
struct Cf {
    float re = 0.f;
    float im = 0.f;
};

inline Cf cf_add(Cf a, Cf b) { return Cf{ a.re + b.re, a.im + b.im }; }
inline Cf cf_scale(Cf a, float s) { return Cf{ a.re * s, a.im * s }; }
inline Cf cf_mul(Cf a, Cf b) {
    return Cf{ a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re };
}
inline Cf cf_div(Cf a, Cf b) {
    const float d = b.re * b.re + b.im * b.im;
    const float inv = (d != 0.f) ? 1.f / d : 0.f;
    return Cf{ (a.re * b.re + a.im * b.im) * inv,
               (a.im * b.re - a.re * b.im) * inv };
}
inline Cf cf_exp(Cf a) {
    const float m = std::exp(a.re);
    return Cf{ m * std::cos(a.im), m * std::sin(a.im) };
}
```

And below `bbd_clock_hz`, add:

```cpp
// Discretised filter, one per direction, shared by every BbdLine at a given
// sample rate.
//
//   P[m] = exp(ts * p_m)                          per-sample pole advance
//   G[step][m]                                    the tick's sub-sample
//     input  kind: ts * R[m] * P[m]^d               position d, tabulated
//     output kind: (R[m] / p_m) * P[m]^(1-d)        and lerped in the hot path
//   H = sum(-R[m] / p_m)                          output DC feed-through
//
// The G table is why there is no powf/cosf per event: the reference's own
// suggestion, and the desktop build profits from it too.
struct BbdFilterCoef {
    Cf    G[bbd_tuning::kInterpSteps][bbd_tuning::kFiltOrder];
    Cf    P[bbd_tuning::kFiltOrder];
    float H = 0.f;

    void interpolate_g(float d, Cf* g) const {
        float row = d * static_cast<float>(bbd_tuning::kInterpSteps - 1);
        if (!(row > 0.f)) row = 0.f;                    // also catches NaN
        int r1 = static_cast<int>(row);
        if (r1 > bbd_tuning::kInterpSteps - 1) r1 = bbd_tuning::kInterpSteps - 1;
        const int r2 = (r1 + 1 < bbd_tuning::kInterpSteps) ? r1 + 1 : r1;
        const float mu = row - static_cast<float>(r1);
        for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
            g[m].re = G[r1][m].re + (G[r2][m].re - G[r1][m].re) * mu;
            g[m].im = G[r1][m].im + (G[r2][m].im - G[r1][m].im) * mu;
        }
    }
};

// The analog specification: kFiltOrder Butterworth poles at kFilterHz and
// their partial-fraction residues for H(s) = prod(-p_j) / prod(s - p_j).
// Both directions currently share the same prototype; the flag is kept
// because the DISCRETISATION differs (see BbdFilterCoef) and because giving
// the two chains different corners later must not need a signature change.
void bbd_analog_spec(bool output_kind, Cf* poles, Cf* residues);

// Shared, built on first call and rebuilt in place if the sample rate moves.
// Not thread-safe by design: both hosts call Flux::init from one thread with
// the audio callback stopped (Rack's onSampleRateChange, the render host's
// setup), which is the same contract hann_curve() in fx/fx_util.h already
// relies on.
const BbdFilterCoef& bbd_filter_in(float sample_rate);
const BbdFilterCoef& bbd_filter_out(float sample_rate);
```

- [ ] **Step 4: Write the build in `bbd.cpp`**

Replace the contents of `engine/fx/bbd.cpp` with:

```cpp
#include "fx/bbd.h"

// Init-time only. Everything in the per-sample path lives in bbd.h so it
// inlines, the same split flux.h/flux.cpp already uses.

namespace {

using spky::Cf;
using spky::cf_add;
using spky::cf_div;
using spky::cf_exp;
using spky::cf_mul;
using spky::cf_scale;
namespace tuning = spky::bbd_tuning;

spky::BbdFilterCoef g_fin;
spky::BbdFilterCoef g_fout;
float g_built_sr = 0.f;

// Butterworth poles, order M, corner w:  p_k = w * exp(j*pi*(2k + M + 1)/(2M))
// For M = 3 that is 120 deg, 180 deg, 240 deg -- one real pole and one
// conjugate pair, all with Re < 0.
void butterworth_poles(Cf* poles) {
    constexpr int M = tuning::kFiltOrder;
    const float w = spky::TWO_PI * tuning::kFilterHz;
    for (int k = 0; k < M; ++k) {
        const float ang = 3.14159265358979f
                        * static_cast<float>(2 * k + M + 1)
                        / static_cast<float>(2 * M);
        poles[k] = Cf{ w * std::cos(ang), w * std::sin(ang) };
    }
}

void residues_for(const Cf* poles, Cf* residues) {
    constexpr int M = tuning::kFiltOrder;
    // num = prod(-p_j): the numerator that makes H(0) == 1 exactly.
    Cf num{ 1.f, 0.f };
    for (int j = 0; j < M; ++j) num = cf_mul(num, Cf{ -poles[j].re, -poles[j].im });
    for (int k = 0; k < M; ++k) {
        Cf den{ 1.f, 0.f };
        for (int j = 0; j < M; ++j) {
            if (j == k) continue;
            den = cf_mul(den, Cf{ poles[k].re - poles[j].re,
                                  poles[k].im - poles[j].im });
        }
        residues[k] = cf_div(num, den);
    }
}

void compute_filter(float sample_rate, bool output_kind, spky::BbdFilterCoef& out) {
    constexpr int M = tuning::kFiltOrder;
    Cf poles[M], residues[M];
    spky::bbd_analog_spec(output_kind, poles, residues);

    const float ts = 1.f / sample_rate;
    for (int m = 0; m < M; ++m)
        out.P[m] = cf_exp(Cf{ ts * poles[m].re, ts * poles[m].im });

    for (int step = 0; step < tuning::kInterpSteps; ++step) {
        const float d = static_cast<float>(step)
                      / static_cast<float>(tuning::kInterpSteps - 1);
        for (int m = 0; m < M; ++m) {
            if (!output_kind) {
                // ts * R[m] * P[m]^d, with P^d spelled as exp(d*ts*p) so no
                // complex pow is needed.
                const Cf pd = cf_exp(Cf{ d * ts * poles[m].re, d * ts * poles[m].im });
                out.G[step][m] = cf_mul(cf_scale(residues[m], ts), pd);
            } else {
                const Cf pd = cf_exp(Cf{ (1.f - d) * ts * poles[m].re,
                                         (1.f - d) * ts * poles[m].im });
                out.G[step][m] = cf_mul(cf_div(residues[m], poles[m]), pd);
            }
        }
    }

    Cf h{ 0.f, 0.f };
    for (int m = 0; m < M; ++m) h = cf_add(h, cf_div(residues[m], poles[m]));
    out.H = -h.re;
}

void build_if_needed(float sample_rate) {
    if (!(sample_rate > 0.f)) return;
    if (sample_rate == g_built_sr) return;
    compute_filter(sample_rate, false, g_fin);
    compute_filter(sample_rate, true,  g_fout);
    g_built_sr = sample_rate;
}

}  // namespace

void spky::bbd_analog_spec(bool /*output_kind*/, Cf* poles, Cf* residues) {
    butterworth_poles(poles);
    residues_for(poles, residues);
}

const spky::BbdFilterCoef& spky::bbd_filter_in(float sample_rate) {
    build_if_needed(sample_rate);
    return g_fin;
}

const spky::BbdFilterCoef& spky::bbd_filter_out(float sample_rate) {
    build_if_needed(sample_rate);
    return g_fout;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
source env.sh && cmake --build build && ./build/spky_tests -tc="bbd filter*"
```

Expected: 6 test cases, all pass. If `-3 dB at kFilterHz` fails, the pole angles are wrong — check that `butterworth_poles` produces 120°/180°/240°, not 60°/180°/300°.

- [ ] **Step 6: Commit**

```bash
git add engine/fx/bbd.h engine/fx/bbd.cpp tests/test_bbd.cpp
git commit -m "feat(bbd): partial-fraction filter chain and the G table"
```

---

### Task 3: `BbdLine` — the core, and the spike

**This is spec risk 1 and it is faced first.** At a 5 s delay and 8192 stages the line runs at 819 Hz — 0.034 ticks per audio sample. Whether the parallel filter branches stay stable and meaningful at that ratio is what the whole design rests on. The test is in this task, not discovered later.

**Files:**
- Modify: `engine/fx/bbd.h` (add `BbdLine`)
- Modify: `tests/test_bbd.cpp`

**Interfaces:**
- Consumes: `BbdFilterCoef`, `bbd_filter_in/out`, `bbd_tuning::kLossCoef` (Tasks 1–2).
- Produces:
  - `class spky::BbdLine` with `void Init(float* buf, size_t max_cells, float sample_rate)`, `void Reset()`, `void SetClock(float hz)`, `void SetStages(int stages)`, `float Process(float in)`, `int cells() const`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_bbd.cpp`:

```cpp
// --- BbdLine ----------------------------------------------------------------
// The core knows nothing about music: no BPM, no divisions, no feedback. That
// is what makes it testable against physics rather than against itself.

static float s_bbd_mem[8192];

// Peak-detect the arrival of a short burst, so a single-sample impulse's
// filtered smear does not decide the answer.
static int first_arrival(BbdLine& line, int n, int burst_len = 16) {
    float peak = 0.f;
    int peak_at = -1;
    for (int i = 0; i < n; ++i) {
        const float x = (i < burst_len) ? 1.f : 0.f;
        const float y = line.Process(x);
        if (i > burst_len * 4 && std::fabs(y) > peak) { peak = std::fabs(y); peak_at = i; }
    }
    return peak > 1e-3f ? peak_at : -1;
}

// RMS of the line's output for a steady sine, after settling.
static float line_rms(BbdLine& line, float hz, float sr, int settle, int measure) {
    double acc = 0.0;
    for (int i = 0; i < settle + measure; ++i) {
        const float x = std::sin(TWO_PI * hz * static_cast<float>(i) / sr);
        const float y = line.Process(x);
        if (i >= settle) acc += static_cast<double>(y) * y;
    }
    return static_cast<float>(std::sqrt(acc / measure));
}

TEST_CASE("bbd line: the arrival lands where stages/(2*f_clk) says it does") {
    BbdLine line;
    line.Init(s_bbd_mem, 8192, 48000.f);
    line.SetStages(8192);
    // Driven directly, not through bbd_clock_hz: this case is about the LINE,
    // and the ladder's ceiling has its own tests.
    line.SetClock(16384.f);                    // 8192 stages -> 250 ms
    const int idx = first_arrival(line, 20000);
    REQUIRE(idx > 0);
    CHECK(idx > 11700);                        // 12000 samples = 250 ms @48k
    CHECK(idx < 12400);
}

TEST_CASE("bbd line: halving the clock doubles the delay") {
    BbdLine a, b;
    static float mem_a[8192], mem_b[8192];
    a.Init(mem_a, 8192, 48000.f);
    b.Init(mem_b, 8192, 48000.f);
    a.SetStages(8192);
    b.SetStages(8192);
    a.SetClock(16384.f);                       // 250 ms
    b.SetClock(8192.f);                        // 500 ms
    const int ia = first_arrival(a, 40000);
    const int ib = first_arrival(b, 40000);
    REQUIRE(ia > 0);
    REQUIRE(ib > 0);
    CHECK(static_cast<float>(ib) / static_cast<float>(ia)
          == doctest::Approx(2.f).epsilon(0.05));
}

TEST_CASE("bbd line: bandwidth follows the clock, not the filters") {
    // THE claim of the design: long delays are dark because of the CLOCK.
    // Two clocks an octave apart, both far below the fixed 3.6 kHz chain, and
    // a probe tone that sits above the lower one's corner and below the
    // higher one's. f_clk/4 is 2 kHz and 1 kHz respectively; the probe is at
    // 1.4 kHz. A model whose bandwidth did NOT track the clock would give the
    // same level twice.
    static float mem_hi[8192], mem_lo[8192];
    BbdLine hi, lo;
    hi.Init(mem_hi, 8192, 48000.f);
    lo.Init(mem_lo, 8192, 48000.f);
    hi.SetStages(2048);                        // short line: settles fast
    lo.SetStages(2048);
    hi.SetClock(8000.f);                       // corner ~2000 Hz
    lo.SetClock(4000.f);                       // corner ~1000 Hz
    const float ref_hi = line_rms(hi, 100.f, 48000.f, 24000, 24000);
    const float ref_lo = line_rms(lo, 100.f, 48000.f, 24000, 24000);
    hi.Reset(); lo.Reset();
    const float p_hi = line_rms(hi, 1400.f, 48000.f, 24000, 24000);
    const float p_lo = line_rms(lo, 1400.f, 48000.f, 24000, 24000);
    REQUIRE(ref_hi > 1e-3f);
    REQUIRE(ref_lo > 1e-3f);
    const float rel_hi = p_hi / ref_hi;        // 1.4 kHz vs 100 Hz, fast clock
    const float rel_lo = p_lo / ref_lo;        // ... and slow clock
    INFO("rel_hi=" << rel_hi << " rel_lo=" << rel_lo);
    CHECK(rel_lo < rel_hi * 0.8f);             // the slow clock IS darker
}

TEST_CASE("bbd line: changing the clock bends the pitch of what is stored") {
    // EHX documents this as a feature. There is no crossfade in the physical
    // device and there must be none here: the stored charge packets simply
    // come out faster.
    static float mem[8192];
    BbdLine line;
    line.Init(mem, 8192, 48000.f);
    line.SetStages(4096);
    line.SetClock(8192.f);                     // 4096/(2*8192) = 250 ms
    // Fill the line with a 400 Hz tone for well over one delay's worth.
    for (int i = 0; i < 20000; ++i)
        line.Process(std::sin(TWO_PI * 400.f * static_cast<float>(i) / 48000.f));
    // Now double the clock and read the SAME material back at 2x.
    line.SetClock(16384.f);
    int crossings = 0;
    float prev = 0.f;
    const int window = 4000;                   // 83 ms of readback
    for (int i = 0; i < window; ++i) {
        const float y = line.Process(0.f);     // silence in: only stored charge
        if (i > 200 && prev <= 0.f && y > 0.f) ++crossings;
        prev = y;
    }
    // 800 Hz over 79 ms is ~63 positive-going crossings; 400 Hz would be ~32.
    INFO("crossings=" << crossings);
    CHECK(crossings > 45);
    CHECK(crossings < 85);
}

TEST_CASE("bbd line: SPIKE -- 819 Hz clock (5 s at 8192 stages) stays stable") {
    // Spec risk 1, faced first. 0.034 ticks per audio sample: most samples
    // produce NO tick at all and the parallel branches coast on their own
    // pole advance. If the partial-fraction decomposition is going to fall
    // apart anywhere, it is here.
    static float mem[8192];
    BbdLine line;
    line.Init(mem, 8192, 48000.f);
    line.SetStages(8192);
    line.SetClock(bbd_clock_hz(5.f, 8192));    // 819.2 Hz
    double energy = 0.0;
    float peak = 0.f;
    for (int i = 0; i < 480000; ++i) {         // 10 s: two full delays
        const float x = 0.5f * std::sin(TWO_PI * 100.f * static_cast<float>(i) / 48000.f);
        const float y = line.Process(x);
        REQUIRE(std::isfinite(y));
        if (std::fabs(y) > peak) peak = std::fabs(y);
        if (i > 288000) energy += static_cast<double>(y) * y;   // after 6 s
    }
    INFO("peak=" << peak);
    CHECK(peak < 8.f);                         // bounded, not exploding
    CHECK(energy > 1e-3);                      // and not silent either:
    // a 100 Hz tone sits below the 205 Hz corner this clock implies, so it
    // must survive attenuated but audible. Silence here means the model has
    // collapsed at low clock rates and the design needs the spec's fallback.
}

TEST_CASE("bbd line: a stage change mid-run drifts, it does not explode") {
    // STAGES is slewed through the 30 ms path (spec "Modulation"): swapping
    // the chip is not what a physical part does, but the artefact it produces
    // -- a drift in time and pitch -- is exactly the class this device
    // already makes. What it must never produce is a NaN or a read past the
    // buffer.
    static float mem[8192];
    BbdLine line;
    line.Init(mem, 8192, 48000.f);
    line.SetStages(8192);
    line.SetClock(8192.f);
    for (int i = 0; i < 96000; ++i) {
        if (i % 64 == 0) {
            const int n = 512 + (i / 64) % 15872;   // sweeps 512 .. 16383
            line.SetStages(n);
        }
        const float y = line.Process(0.3f * std::sin(0.05f * static_cast<float>(i)));
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) < 20.f);
    }
    CHECK(line.cells() >= bbd_tuning::kMinStages / 2);
    CHECK(line.cells() <= 8192);
}

TEST_CASE("bbd line: a zero clock holds instead of crashing") {
    // No floor on the clock means f_clk can be pushed arbitrarily low by a
    // very slow tempo. Zero ticks per sample must be a hold, not a divide by
    // zero -- note the 1/fclk inside the tick loop.
    static float mem[8192];
    BbdLine line;
    line.Init(mem, 8192, 48000.f);
    line.SetStages(1024);
    line.SetClock(0.f);
    for (int i = 0; i < 1000; ++i) CHECK(std::isfinite(line.Process(0.5f)));
    line.SetClock(-1.f);
    for (int i = 0; i < 1000; ++i) CHECK(std::isfinite(line.Process(0.5f)));
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
source env.sh && cmake --build build 2>&1 | tail -20
```

Expected: FAIL at compile — `'BbdLine' was not declared in this scope`.

- [ ] **Step 3: Add `BbdLine` to `bbd.h`**

Insert after `BbdFilterCoef`'s declarations (after `bbd_filter_out`):

```cpp
// An N-cell bucket-brigade line over injected memory, driven by a clock
// frequency. It has NO read pointer: all charge packets are clocked forward
// together, and the delay is a consequence of the clock, not of an index.
//
// Even ticks WRITE one cell, odd ticks READ one cell -- that alternation is
// the two-phase clock of the physical part, and it is why the cell count is
// half the stage count and why the tick rate is twice f_clk:
//
//     ticks/sample = 2 * f_clk / fs
//     cells        = stages / 2
//     delay        = 2 * cells / tick_rate = stages / (2 * f_clk)
//
// It knows nothing about music -- no BPM, no divisions, no feedback.
class BbdLine {
public:
    BbdLine() = default;
    BbdLine(const BbdLine&) = delete;
    BbdLine& operator=(const BbdLine&) = delete;

    // `buf` holds max_cells floats and is owned by the host (FxMem).
    void Init(float* buf, size_t max_cells, float sample_rate) {
        mem_ = buf;
        max_cells_ = max_cells;
        sr_ = (sample_rate > 0.f) ? sample_rate : 48000.f;
        fin_ = &bbd_filter_in(sr_);
        fout_ = &bbd_filter_out(sr_);
        cells_ = static_cast<int>(max_cells_ > 0 ? max_cells_ : 1);
        Reset();
    }

    void Reset() {
        if (mem_ && max_cells_) std::memset(mem_, 0, max_cells_ * sizeof(float));
        imem_ = 0;
        pclk_ = 0.f;
        ptick_ = 0;
        ybbd_old_ = 0.f;
        loss_z_ = 0.f;
        for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
            Xin_[m] = Cf{};
            Xout_mem_[m] = Cf{};
        }
    }

    // The BBD clock in Hz. Non-finite or non-positive means "hold": no ticks,
    // no division by the clock, the output filter simply coasts.
    void SetClock(float hz) {
        ticks_ = (hz > 0.f && std::isfinite(hz)) ? (2.f * hz / sr_) : 0.f;
    }

    // Physical stage count. The cell array is half of it; content is
    // deliberately NOT cleared, so a stage change drifts in time and pitch
    // instead of clicking.
    void SetStages(int stages) {
        int c = stages / 2;
        const int lo = bbd_tuning::kMinStages / 2;
        const int hi = static_cast<int>(max_cells_);
        if (c < lo) c = lo;
        if (c > hi) c = hi;
        if (c < 1) c = 1;
        if (c == cells_) return;
        cells_ = c;
        if (imem_ >= cells_) imem_ = 0;
    }

    int cells() const { return cells_; }

    float Process(float in) {
        Cf Xout[bbd_tuning::kFiltOrder] = {};

        const float fclk = ticks_;
        if (fclk > 0.f) {
            const float pclk_old = pclk_;
            const float p = pclk_ + fclk;
            const int tick_count = static_cast<int>(p);
            pclk_ = p - static_cast<float>(tick_count);
            const float inv = 1.f / fclk;
            Cf g[bbd_tuning::kFiltOrder];
            for (int t = 0; t < tick_count; ++t) {
                // The tick's position inside this audio sample, in [0, 1).
                float d = (1.f - pclk_old + static_cast<float>(t)) * inv;
                d -= static_cast<float>(static_cast<int>(d));
                if ((ptick_ & 1u) == 0u) {
                    // WRITE phase: sample the input filter at d and push one
                    // charge packet, after the charge-transfer loss.
                    fin_->interpolate_g(d, g);
                    float s = 0.f;
                    for (int m = 0; m < bbd_tuning::kFiltOrder; ++m)
                        s += g[m].re * Xin_[m].re - g[m].im * Xin_[m].im;
                    loss_z_ += bbd_tuning::kLossCoef * (s - loss_z_);
                    mem_[imem_] = loss_z_;
                    imem_ = (imem_ + 1 < cells_) ? imem_ + 1 : 0;
                } else {
                    // READ phase: imem_ points at the oldest cell. The output
                    // filter is driven by the STEP between consecutive
                    // readings, which is what makes the staircase exact.
                    fout_->interpolate_g(d, g);
                    const float ybbd = mem_[imem_];
                    const float delta = ybbd - ybbd_old_;
                    ybbd_old_ = ybbd;
                    for (int m = 0; m < bbd_tuning::kFiltOrder; ++m)
                        Xout[m] = cf_add(Xout[m], cf_scale(g[m], delta));
                }
                ++ptick_;
            }
        }

        // Input filter states advance by exactly one audio sample, always --
        // whether or not a tick happened. This is the continuous-time part of
        // the model and it must not be inside the tick loop.
        for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
            const Cf p = fin_->P[m];
            const Cf x = Xin_[m];
            Xin_[m] = Cf{ x.re * p.re - x.im * p.im + in,
                          x.re * p.im + x.im * p.re };
        }

        float y = fout_->H * ybbd_old_;
        for (int m = 0; m < bbd_tuning::kFiltOrder; ++m) {
            const Cf x = cf_add(cf_mul(fout_->P[m], Xout_mem_[m]), Xout[m]);
            Xout_mem_[m] = x;
            y += x.re;
        }
        return y;
    }

private:
    const BbdFilterCoef* fin_ = nullptr;
    const BbdFilterCoef* fout_ = nullptr;
    float*   mem_ = nullptr;
    size_t   max_cells_ = 0;
    int      cells_ = 1;
    int      imem_ = 0;
    float    sr_ = 48000.f;
    float    ticks_ = 0.f;      // ticks per audio sample = 2*f_clk/fs
    float    pclk_ = 0.f;       // fractional tick phase carried between samples
    uint32_t ptick_ = 0;        // parity picks write vs read
    float    ybbd_old_ = 0.f;
    float    loss_z_ = 0.f;     // charge-transfer loss, one pole at f_clk/4
    Cf       Xin_[bbd_tuning::kFiltOrder];
    Cf       Xout_mem_[bbd_tuning::kFiltOrder];
};
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
source env.sh && cmake --build build && ./build/spky_tests -tc="bbd line*" -s
```

Expected: 7 test cases pass. The three that carry `INFO(...)` print their measured values on failure — if a bound is off but the *shape* of the result is right (arrival ratio ≈ 2, `rel_lo < rel_hi`, crossings roughly doubled), record the measured number in the test's comment and keep the assertion at a comfortable margin. Never loosen an assertion until it stops testing the shape.

**If the SPIKE case fails** — non-finite, exploding, or silent at 819 Hz — stop. Do not proceed to Task 4. Report the failure mode with the measured `peak` and `energy`; the spec's design rests on this and the fallback is a spec-level decision, not an implementation one.

- [ ] **Step 5: Commit**

```bash
git add engine/fx/bbd.h tests/test_bbd.cpp
git commit -m "feat(bbd): the clocked line -- no read pointer, bandwidth tracks the clock"
```

---

### Task 4: `Compander` — the NE570 pair

Small, standalone, and the source of the DMM's most recognisable behaviour: tails that get pulled down harder than a linear delay would pull them, with the noise breathing along.

**Files:**
- Modify: `engine/fx/bbd.h`
- Modify: `tests/test_bbd.cpp`

**Interfaces:**
- Consumes: `bbd_tuning::kCompTauS`, `kCompRef`, `kCompFloorC`, `kCompFloorE`, `kCompCeilE`.
- Produces: `class spky::Compander` with `void Init(float sample_rate)`, `void Reset()`, `float Compress(float x)`, `float Expand(float x)`, `float env_comp() const`, `float env_exp() const`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_bbd.cpp`:

```cpp
// --- Compander --------------------------------------------------------------

TEST_CASE("compander: the 10 ms time constant is measurable") {
    // tau = 10 kOhm * 1 uF. A one-pole reaches 1 - 1/e = 63.2 % of a step in
    // exactly tau, and this is the only place that number is observable.
    Compander c;
    c.Init(48000.f);
    const float target = 0.5f;                       // |x| of a DC step
    const int tau_samples = static_cast<int>(bbd_tuning::kCompTauS * 48000.f);
    for (int i = 0; i < tau_samples; ++i) c.Compress(target);
    CHECK(c.env_comp() == doctest::Approx(0.632f * target).epsilon(0.05));
    for (int i = 0; i < 4 * tau_samples; ++i) c.Compress(target);
    CHECK(c.env_comp() == doctest::Approx(target).epsilon(0.05));
}

TEST_CASE("compander: compress then expand is unity in steady state") {
    // 2:1 followed by 1:2 must give back the level it was handed, or every
    // gain staging decision downstream is built on sand. Checked at three
    // levels spanning 30 dB.
    for (float amp : { 0.03f, 0.1f, 0.5f }) {
        Compander c;
        c.Init(48000.f);
        double in_sq = 0.0, out_sq = 0.0;
        for (int i = 0; i < 48000; ++i) {
            const float x = amp * std::sin(TWO_PI * 220.f * static_cast<float>(i) / 48000.f);
            const float y = c.Expand(c.Compress(x));
            if (i > 24000) { in_sq += (double)x * x; out_sq += (double)y * y; }
        }
        const float ratio = static_cast<float>(std::sqrt(out_sq / in_sq));
        INFO("amp=" << amp << " ratio=" << ratio);
        CHECK(ratio == doctest::Approx(1.f).epsilon(0.15));
    }
}

TEST_CASE("compander: 2:1 really is 2:1 on the way in") {
    // A 12 dB input change must come out as a 6 dB change from Compress
    // alone. This is what pulls the tails down harder than a linear delay
    // would -- the audible signature of the part.
    auto compressed_rms = [](float amp) {
        Compander c;
        c.Init(48000.f);
        double acc = 0.0;
        for (int i = 0; i < 48000; ++i) {
            const float x = amp * std::sin(TWO_PI * 220.f * static_cast<float>(i) / 48000.f);
            const float y = c.Compress(x);
            if (i > 24000) acc += (double)y * y;
        }
        return static_cast<float>(std::sqrt(acc / 24000.0));
    };
    const float lo = compressed_rms(0.05f);
    const float hi = compressed_rms(0.2f);           // +12 dB in
    const float db = 20.f * std::log10(hi / lo);
    INFO("delta_db=" << db);
    CHECK(db > 4.f);
    CHECK(db < 8.f);
}

TEST_CASE("compander: gain is bounded in both directions") {
    // Silence must not be amplified into the noise floor of the universe,
    // and a loud transient must not be expanded without limit. The bounds are
    // derived from kCompRef, not tasted -- see the constants.
    Compander c;
    c.Init(48000.f);
    for (int i = 0; i < 48000; ++i) CHECK(std::isfinite(c.Compress(0.f)));
    CHECK(std::fabs(c.Compress(1e-9f)) < 1e-6f);     // gain capped at 4
    Compander d;
    d.Init(48000.f);
    for (int i = 0; i < 48000; ++i) {
        const float y = d.Expand(3.f);
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) < 16.f);
    }
}
```

- [ ] **Step 2: Run to verify it fails**

Expected: FAIL at compile — `'Compander' was not declared in this scope`.

- [ ] **Step 3: Add `Compander` to `bbd.h`**

Insert after `BbdLine`:

```cpp
// The NE570 pair: a 2:1 compressor before the line and a 1:2 expander after
// it, both with tau = 10 ms. Its job is the BBD's 75 dB noise floor; its
// audible signature is tails pulled down harder than a linear delay would
// pull them, with the noise breathing along.
//
// The round trip is unity by construction, not by tuning. With a compressor
// envelope e and reference r:
//
//   g_c = sqrt(r / e)                     -> out level = sqrt(e * r)
//   the expander's own envelope is then sqrt(e * r)
//   g_e = sqrt(e * r) / r = sqrt(e / r)
//   g_c * g_e = 1
//
// It is NOT a parameter. It is tuned by ear and fixed: the device's
// character, not a setting of it, and "compression" is already spoken for by
// COMP on the panel.
class Compander {
public:
    void Init(float sample_rate) {
        float k = 1.f / (bbd_tuning::kCompTauS * (sample_rate > 0.f ? sample_rate : 48000.f));
        coef_ = k > 1.f ? 1.f : k;
        Reset();
    }

    void Reset() {
        env_c_ = bbd_tuning::kCompFloorC;
        env_e_ = bbd_tuning::kCompFloorE;
    }

    float Compress(float x) {
        const float a = x < 0.f ? -x : x;
        env_c_ += coef_ * (a - env_c_);
        float e = env_c_;
        if (!(e > bbd_tuning::kCompFloorC)) e = bbd_tuning::kCompFloorC;
        return x * std::sqrt(bbd_tuning::kCompRef / e);
    }

    float Expand(float x) {
        const float a = x < 0.f ? -x : x;
        env_e_ += coef_ * (a - env_e_);
        float e = env_e_;
        if (!(e > bbd_tuning::kCompFloorE)) e = bbd_tuning::kCompFloorE;
        if (e > bbd_tuning::kCompCeilE) e = bbd_tuning::kCompCeilE;
        return x * (e * (1.f / bbd_tuning::kCompRef));
    }

    // Observers for tests only: the 10 ms time constant is otherwise
    // unobservable, and "the compander breathes" is not an assertion.
    float env_comp() const { return env_c_; }
    float env_exp() const { return env_e_; }

private:
    float coef_ = 1.f;
    float env_c_ = bbd_tuning::kCompFloorC;
    float env_e_ = bbd_tuning::kCompFloorE;
};
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
source env.sh && cmake --build build && ./build/spky_tests -tc="compander*" -s
```

Expected: 4 test cases pass. If "unity in steady state" lands outside ±15 %, print `ratio` at all three amplitudes before touching anything: a *consistent* offset means the envelope-floor constants need the derivation re-checked; an offset that grows with amplitude means the expander law is wrong (it must be linear in its envelope, not a square root).

- [ ] **Step 5: Commit**

```bash
git add engine/fx/bbd.h tests/test_bbd.cpp
git commit -m "feat(bbd): NE570 compander, 10 ms, unity round trip"
```

---

### Task 5: `BbdEcho` — the chain

Assembles DRIVE → saturate → compress → line → expand → feedback, and drops into `EchoDelay`'s place with the same `Process(in, ...)` shape.

**Files:**
- Modify: `engine/fx/bbd.h`
- Modify: `tests/test_bbd.cpp`

**Interfaces:**
- Consumes: `BbdLine`, `Compander`, `bbd_tuning::kSatCeil`, `kDriveLoDb`, `kDriveHiDb`; `spky::fast_tanh` from `util/fast_tanh.h`.
- Produces: `class spky::BbdEcho` with `void Init(float sample_rate, float* buf, size_t max_cells)`, `void SetFeedback(float fb)`, `float Feedback() const`, `void SetDrive(float norm)`, `void SetStages(int stages)`, `float Process(float in, float clock_hz)`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_bbd.cpp`:

```cpp
// --- BbdEcho ----------------------------------------------------------------

static float s_echo_mem[8192];

TEST_CASE("bbd echo: feedback produces decaying repeats") {
    BbdEcho e;
    e.Init(48000.f, s_echo_mem, 8192);
    e.SetStages(8192);
    e.SetDrive(0.f);
    e.SetFeedback(0.5f);
    const float hz = bbd_clock_hz(0.25f, 8192);      // 250 ms
    std::vector<float> out(60000);
    for (int i = 0; i < 60000; ++i)
        out[i] = e.Process((i < 32) ? 1.f : 0.f, hz);
    auto peak_around = [&](int c) {
        float p = 0.f;
        for (int i = c - 900; i < c + 900; ++i) p = std::max(p, std::fabs(out[i]));
        return p;
    };
    const float p1 = peak_around(12000);
    const float p2 = peak_around(24000);
    const float p3 = peak_around(36000);
    INFO("p1=" << p1 << " p2=" << p2 << " p3=" << p3);
    CHECK(p1 > 1e-3f);
    CHECK(p2 < p1);
    CHECK(p3 < p2);
}

TEST_CASE("bbd echo: each repeat is darker than the last") {
    // The feedback path re-enters BEFORE the compander, so every repeat pays
    // the whole chain again and bandwidth shrinks multiplicatively. That is
    // the difference between this and a delay with a one-off filter on it.
    BbdEcho e;
    static float mem[8192];
    e.Init(48000.f, mem, 8192);
    e.SetStages(8192);
    e.SetDrive(0.f);
    e.SetFeedback(0.7f);
    const float hz = bbd_clock_hz(0.25f, 8192);
    std::vector<float> out(60000);
    for (int i = 0; i < 60000; ++i) {
        // A burst with real high-frequency content to lose.
        const float x = (i < 480)
            ? 0.5f * std::sin(TWO_PI * 1500.f * static_cast<float>(i) / 48000.f)
            : 0.f;
        out[i] = e.Process(x, hz);
    }
    // High-frequency energy per repeat, measured as first-difference energy
    // normalised by total energy -- a cheap brightness proxy that needs no FFT.
    auto brightness = [&](int c) {
        double hf = 0.0, tot = 0.0;
        for (int i = c - 700; i < c + 700; ++i) {
            const double d = out[i] - out[i - 1];
            hf += d * d;
            tot += (double)out[i] * out[i];
        }
        return tot > 0.0 ? hf / tot : 0.0;
    };
    const double b1 = brightness(12300);
    const double b2 = brightness(24300);
    INFO("b1=" << b1 << " b2=" << b2);
    CHECK(b2 < b1);
}

TEST_CASE("bbd echo: feedback at max blooms but stays bounded") {
    // FEEDBACK keeps its 1.2 over unity so self-oscillation stays reachable
    // -- documented behaviour of the original. The bound now comes from
    // saturation WITHIN the loop rather than a tanh on the read path.
    BbdEcho e;
    static float mem[8192];
    e.Init(48000.f, mem, 8192);
    e.SetStages(8192);
    e.SetDrive(0.5f);
    e.SetFeedback(1.2f);
    const float hz = bbd_clock_hz(0.25f, 8192);
    float peak = 0.f;
    double late_sq = 0.0;
    int late_n = 0;
    for (int i = 0; i < 480000; ++i) {               // 10 s
        const float y = e.Process((i < 32) ? 1.f : 0.f, hz);
        REQUIRE(std::isfinite(y));
        peak = std::max(peak, std::fabs(y));
        if (i >= 432000) { late_sq += (double)y * y; ++late_n; }
    }
    const float late_rms = static_cast<float>(std::sqrt(late_sq / late_n));
    INFO("peak=" << peak << " late_rms=" << late_rms);
    CHECK(peak > 0.2f);                              // it did bloom
    CHECK(peak < 12.f);                              // and it stayed bounded
    CHECK(late_rms > 0.01f);                         // and it sustained
}

TEST_CASE("bbd echo: feedback below unity decays to silence") {
    BbdEcho e;
    static float mem[8192];
    e.Init(48000.f, mem, 8192);
    e.SetStages(8192);
    e.SetDrive(0.f);
    e.SetFeedback(0.6f);
    const float hz = bbd_clock_hz(0.25f, 8192);
    float early = 0.f, late = 0.f;
    for (int i = 0; i < 480000; ++i) {
        const float y = e.Process((i < 32) ? 1.f : 0.f, hz);
        if (i > 11000 && i < 13000) early = std::max(early, std::fabs(y));
        if (i > 400000) late = std::max(late, std::fabs(y));
    }
    CHECK(early > 1e-3f);
    CHECK(late < early * 0.05f);
}

TEST_CASE("bbd echo: DRIVE dirties every pass, not just the input") {
    // This is what makes DRIVE not redundant with GRIT: GRIT runs before FLUX
    // and dirties the input once; DRIVE sits inside the loop.
    auto harmonic_growth = [](float drive) {
        BbdEcho e;
        static float mem[8192];
        e.Init(48000.f, mem, 8192);
        e.SetStages(8192);
        e.SetDrive(drive);
        e.SetFeedback(0.85f);
        const float hz = bbd_clock_hz(0.25f, 8192);
        // A pure 200 Hz tone for one delay's worth, then silence: what comes
        // back is the loop's own doing.
        std::vector<float> out(140000);
        for (int i = 0; i < 140000; ++i) {
            const float x = (i < 12000)
                ? 0.4f * std::sin(TWO_PI * 200.f * static_cast<float>(i) / 48000.f)
                : 0.f;
            out[i] = e.Process(x, hz);
        }
        // Compare the first repeat's waveform crest factor with the fourth's.
        // Saturation flattens peaks: crest falls as harmonics accumulate.
        auto crest = [&](int c) {
            float pk = 0.f;
            double sq = 0.0;
            for (int i = c; i < c + 6000; ++i) {
                pk = std::max(pk, std::fabs(out[i]));
                sq += (double)out[i] * out[i];
            }
            const float rms = static_cast<float>(std::sqrt(sq / 6000.0));
            return rms > 0.f ? pk / rms : 0.f;
        };
        return crest(15000) - crest(51000);          // repeat 1 vs repeat 4
    };
    const float clean = harmonic_growth(0.f);
    const float dirty = harmonic_growth(0.9f);
    INFO("clean=" << clean << " dirty=" << dirty);
    CHECK(dirty > clean);
}

TEST_CASE("bbd echo: DRIVE does not move the small-signal loop gain") {
    // Makeup after the saturator: FEEDBACK must mean the same thing at DRIVE
    // 0 and DRIVE 1 for quiet material, or the two knobs fight.
    auto tail_at = [](float drive) {
        BbdEcho e;
        static float mem[8192];
        e.Init(48000.f, mem, 8192);
        e.SetStages(8192);
        e.SetDrive(drive);
        e.SetFeedback(0.7f);
        const float hz = bbd_clock_hz(0.25f, 8192);
        float p = 0.f;
        for (int i = 0; i < 100000; ++i) {
            // 40 dB below the saturator's knee: nothing here should clip.
            const float y = e.Process((i < 32) ? 0.01f : 0.f, hz);
            if (i > 60000 && i < 64000) p = std::max(p, std::fabs(y));
        }
        return p;
    };
    const float a = tail_at(0.f);
    const float b = tail_at(1.f);
    INFO("a=" << a << " b=" << b);
    REQUIRE(a > 1e-6f);
    CHECK(b / a == doctest::Approx(1.f).epsilon(0.25));
}
```

Add `#include <vector>` and `#include <algorithm>` at the top of `tests/test_bbd.cpp` if not already there.

- [ ] **Step 2: Run to verify it fails**

Expected: FAIL at compile — `'BbdEcho' was not declared in this scope`.

- [ ] **Step 3: Add `BbdEcho` to `bbd.h`**

Add `#include "util/fast_tanh.h"` to the header's include list, then insert after `Compander`:

```cpp
// The chain: DRIVE -> soft saturation -> compressor -> BbdLine -> expander,
// with the feedback path re-entering BEFORE the compander so every repeat
// pays the whole chain again.
//
// Drops into EchoDelay's place with the same Process(in, ...) shape -- only
// the second argument changed meaning, from a length in samples to a clock in
// Hz, which is the entire redesign in one signature.
class BbdEcho {
public:
    BbdEcho() = default;
    BbdEcho(const BbdEcho&) = delete;
    BbdEcho& operator=(const BbdEcho&) = delete;

    void Init(float sample_rate, float* buf, size_t max_cells) {
        line_.Init(buf, max_cells, sample_rate);
        comp_.Init(sample_rate);
        feedback_ = 0.f;
        fb_state_ = 0.f;
        SetDrive(0.f);
    }

    void SetFeedback(float fb) { feedback_ = fb; }
    float Feedback() const { return feedback_; }

    // 0..1 -> kDriveLoDb .. kDriveHiDb into a FIXED-threshold saturator, with
    // makeup after it so small-signal loop gain -- and therefore FEEDBACK's
    // meaning -- does not move with DRIVE.
    void SetDrive(float norm) {
        const float n = clampf(norm, 0.f, 1.f);
        const float db = bbd_tuning::kDriveLoDb
                       + n * (bbd_tuning::kDriveHiDb - bbd_tuning::kDriveLoDb);
        const float g = std::pow(10.f, db * 0.05f);   // control rate only
        sat_in_ = g * (1.f / bbd_tuning::kSatCeil);
        sat_out_ = bbd_tuning::kSatCeil / g;
    }

    void SetStages(int stages) { line_.SetStages(stages); }

    float Process(float in, float clock_hz) {
        line_.SetClock(clock_hz);
        const float x = in + fb_state_ * feedback_;
        // MN3005 ceiling: the loop saturates softly and then self-oscillates
        // as a thick distorted tone rather than a screech.
        const float sat = fast_tanh(x * sat_in_) * sat_out_;
        const float y = comp_.Expand(line_.Process(comp_.Compress(sat)));
        fb_state_ = y;
        return y;
    }

private:
    BbdLine   line_;
    Compander comp_;
    float feedback_ = 0.f;
    float sat_in_ = 1.f;
    float sat_out_ = 1.f;
    float fb_state_ = 0.f;
};
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
source env.sh && cmake --build build && ./build/spky_tests -tc="bbd echo*" -s
```

Expected: 6 test cases pass. Every case prints its measured values; where a bound turns out to be in the wrong place but the *relation* holds, record the measured value in the comment and keep a margin. If "feedback at max blooms" fails on `peak < 12.f` with a value that keeps climbing over the 10 s run, the loop is not bounded — that is a real bug, not a tolerance question: check that `fast_tanh`'s output really is being multiplied by `sat_out_` and not by `1/sat_out_`.

- [ ] **Step 5: Commit**

```bash
git add engine/fx/bbd.h tests/test_bbd.cpp
git commit -m "feat(bbd): the echo chain -- drive inside the loop, compander around the line"
```

---

### Task 6: Delete the taps

A BBD has no read pointer. The taps are not being removed for budget alone; they contradict the model. This task removes them completely and leaves the tree green with `EchoDelay` still in place — so the removal is reviewable on its own, separately from the model that replaces it.

**Files:**
- Delete: `engine/fx/taps.h`, `engine/fx/taps.cpp`, `tests/test_taps.cpp`, `bench/workloads_taps.cpp`
- Modify: `engine/fx/flux.h:7,172-176,185,196-208`, `engine/fx/flux.cpp:21-23,65-84,86-124`
- Modify: `engine/fx/part_fx.h:48-52`
- Modify: `engine/instrument.h:101-102`, `engine/instrument.cpp:2,86-93`
- Modify: `bench/workloads_system.cpp:11,326-404,408+`
- Modify: `bench/families.cpp`, `bench/Makefile:36,43,52`, `bench/run.py:177,224-227`, `bench/profiles.py:39`, `bench/test_run_contract.py:31,842`, `bench/README.md:362`
- Modify: `CMakeLists.txt:88-89,164`, `host/vcv/Makefile:47`, `bench/audition/Makefile:36`
- Modify: `tests/test_flux.cpp` (drop the four tap cases), `tests/test_part_fx.cpp` (drop the tap cases)

**Interfaces:**
- Removes: `spky::TapBank`, `spky::TapeTap`, `spky::derive_offsets`, `spky::tap_tuning::*`, `Flux::set_tap_offsets`, `Flux::taps_active`, `PartFx::set_tap_offsets`.
- Keeps (for now, replaced in Task 7): `Flux::set_dust`, `Flux::set_rot`, `PartFx::set_dust`, `PartFx::set_rot`, `Instrument::set_dust`, `Instrument::set_rot` — reduced to storing their argument and nothing else, so every caller keeps compiling and this task's diff stays about deletion.

- [ ] **Step 1: Delete the four files**

```bash
git rm engine/fx/taps.h engine/fx/taps.cpp tests/test_taps.cpp bench/workloads_taps.cpp
```

- [ ] **Step 2: Cut the taps out of `Flux`**

In `engine/fx/flux.h`: delete `#include "fx/taps.h"` (line 7); delete the `set_tap_offsets` and `taps_active` declarations (lines 174-177); delete the `TapBank _taps;` member (line 185); leave `set_dust` / `set_rot` declared. Replace the long `_dust_norm` / `_rot_norm` comment block (lines 196-208) with:

```cpp
    // Renamed to _drive_norm / _stages_norm in the BBD rewrite; kept here as
    // dead guards only so the two setters stay compilable between the taps'
    // removal and the model that replaces them.
    float _dust_norm = -1.f;
    float _rot_norm = -1.f;
```

In `engine/fx/flux.cpp`:
- delete `_taps.init(sample_rate);` and the two guard resets' comments (lines 21-23), leaving `_dust_norm = -1.f; _rot_norm = -1.f;`
- replace `set_dust` / `set_rot` bodies with a stored value and nothing else
- delete `set_tap_offsets` entirely
- replace `process()` with the tap-free version:

```cpp
void Flux::process(float& l, float& r) {
    if (!_buf_ok) return;
    float send = _sw.process();
    if (_sw.is_idle()) return;   // fully off: bit-exact dry

    daisysp::fonepole(_dt_current, _dt_target, _dt_coef);
    const float ds = _dt_current * _sr;

    l += _echo_l.Process(l * send, ds) * _mix_lin;
    r += _echo_r.Process(r * send, ds) * _mix_lin;
}
```

Also delete `line()` / `write_ptr()` from `EchoDelay` (`flux.h:122-123`) and `data()` / `write_ptr()` from `DeLine` (`flux.h:57-59`) — nothing reads them any more, and Task 7 deletes both classes outright.

- [ ] **Step 3: Cut the cross-feed out of `Instrument` and `PartFx`**

`engine/fx/part_fx.h`: delete `set_tap_offsets` (lines 50-52).

`engine/instrument.cpp`: delete `#include "fx/taps.h"` (line 2) and the whole cross-feed block (lines 85-93), leaving the excitation-bus hand-over that follows it. Update the block comment above `_ctrl_ctr = Center::kCtrlInterval;` so it no longer claims to place taps.

`engine/instrument.h`: keep `set_dust` / `set_rot` (renamed in Task 8); update the `rhythm(int p)` comment (line 212-213) — it is now read by tests only, not by a control tick.

- [ ] **Step 4: Cut the bench's taps family**

- `bench/workloads_system.cpp`: delete `#include "fx/taps.h"` (line 11), the whole `setup_inst_worst_taps` function with its comment block (lines 326-404), and the `instrument_worst_taps` row from `kCoreWorkloads`.
- `bench/families.cpp`: delete the `#if BENCH_FAMILY_TAPS ... #endif` block and the `kTapsWorkloads` extern in `bench/families.h`.
- `bench/Makefile`: remove `taps` from `BENCH_FAMILIES` (line 36), delete `FAMILY_SOURCE_taps` (line 43) and `FAMILY_DEFINE_taps` (line 52), delete `../engine/fx/taps.cpp` (line 96).
- `bench/run.py`: delete `"instrument_worst_taps",` (line 177) and the whole `"taps": (...)` block (lines 224-227).
- `bench/profiles.py:39`: drop `"taps",` from the `full` profile.
- `bench/test_run_contract.py:31`: `ALL_FAMILIES = "system voice mem mod abl sampler"`; line 842: drop `"taps"` from the set.
- `bench/README.md:362`: drop the `workloads_taps.cpp` clause.

- [ ] **Step 5: Cut the remaining build-list entries and tests**

- `CMakeLists.txt`: delete `engine/fx/taps.cpp` and `tests/test_taps.cpp` (lines 88-89) and `engine/fx/taps.cpp` from the render list (line 164).
- `host/vcv/Makefile:47`: delete `$(REPO)/engine/fx/taps.cpp \`.
- `bench/audition/Makefile:36`: delete `../../engine/fx/taps.cpp \`.
- `tests/test_flux.cpp`: delete the four tap-dependent cases — `"flux: dust 0 stays inert at any rot ..."`, `"flux: taps sound only once offsets have been pushed"`, `"flux: init resets the DUST guard ..."`, `"flux: init resets the ROT guard ..."` — **and** `"deline: N samples behind the head reads the sample written N steps ago"`, which exists only to pin the tap-read indexing convention and calls the `data()` / `write_ptr()` accessors this task removes.
- `tests/test_part_fx.cpp`: delete every case that calls `set_dust`, `set_rot` or `set_tap_offsets` (the blocks around lines 160, 190, 242, 275, 303). Keep everything about the tape tap, FX MIX and the send law.

- [ ] **Step 6: Run the full suite**

```bash
source env.sh && cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: everything builds and passes; the tap test cases are simply gone. Then verify the pre-fork originals are genuinely unbuilt (spec: "`src/core/echo.h` and `src/core/deline.h` are pre-fork originals and are not touched. Whether they are still built is a verification item"):

```bash
grep -rn "src/core" CMakeLists.txt bench/Makefile host/vcv/Makefile bench/audition/Makefile
```

Expected: no matches. The root `Makefile:31`'s `$(wildcard src/core/*.cpp)` is the **firmware** target, which does not compile `engine/**` yet — record that in the task report and leave it alone.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor(flux): delete the tap bank -- a BBD has no read pointer"
```

---

### Task 7: `Flux` becomes a BBD

`Flux` keeps its class, its name and its public form: the `SoftSwitch`, `engaged()`, the bit-exact off path, `set_rate` / `set_mix` / `set_feedback` / `set_bpm`, and the shared delay-time slew. What changes is what sits behind them.

**Files:**
- Modify: `engine/fx/flux.h` (delete `DeLine`, `TapeBpf`, `EchoDelay`; rewrite `Flux`'s members)
- Modify: `engine/fx/flux.cpp`
- Modify: `engine/mod/divisions.h:41-46` (the comment only)
- Rewrite: `tests/test_flux.cpp`

**Interfaces:**
- Consumes: `BbdEcho`, `bbd_clock_hz`, `bbd_time_mult`, `bbd_tuning::*` (Tasks 1–5).
- Produces:
  - `Flux::kMaxStages` = 16384, `Flux::kMinStages` = 512, `Flux::kMaxSamples` = 8192 (floats per channel — the name and meaning are unchanged, only the value).
  - `void Flux::set_drive(float norm)`, `void Flux::set_stages(float norm)`, `void Flux::set_time_mod(float norm)`.
  - Observers `int Flux::stages() const`, `float Flux::clock_hz() const`.
  - Removed: `Flux::set_dust`, `Flux::set_rot`, `spky::DeLine`, `spky::TapeBpf`, `spky::EchoDelay`.

- [ ] **Step 1: Add `bbd_time_mult` to `bbd.h`**

The lane pulls multiplicatively on the clock and is evaluated **every sample**, so this cannot be a `powf`. Insert next to `bbd_clock_hz`:

```cpp
// FXT_FLUX_TIME's geometric depth map: 0 -> x1/4, 0.5 -> x1, 1 -> x4. Since
// pitch tracks the clock ratio directly, that is +-2 octaves at full depth --
// the original's +-10 % clock swing (+-1.65 semitones) lands in the bottom
// tenth of the control. The historical device is the start of the scale, not
// the target.
//
// A LUT, not powf: this is evaluated once per sample per part. 65 rows over
// four octaves with linear interpolation gives a worst-case error of 2.3e-4
// relative, i.e. 0.4 cents -- inaudible on a modulation depth control.
inline float bbd_time_mult(float norm) {
    static const std::array<float, 65> table = [] {
        std::array<float, 65> t{};
        for (size_t i = 0; i < t.size(); ++i) {
            const float n = static_cast<float>(i) / static_cast<float>(t.size() - 1);
            t[i] = std::pow(2.f, 4.f * (n - 0.5f));
        }
        return t;
    }();
    const float p = clampf(norm, 0.f, 1.f) * 64.f;
    int i = static_cast<int>(p);
    if (i > 64) i = 64;
    const int j = (i < 64) ? i + 1 : 64;
    return table[i] + (table[j] - table[i]) * (p - static_cast<float>(i));
}
```

- [ ] **Step 2: Write the failing tests**

Replace `tests/test_flux.cpp` entirely:

```cpp
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "fx/flux.h"
#include "mod/divisions.h"
using namespace spky;

static float s_buf_l[Flux::kMaxSamples];
static float s_buf_r[Flux::kMaxSamples];

// Peak-detect the first echo arrival. A single-sample impulse comes out of a
// band-limited BBD as a smear, so a burst plus a peak search is the honest
// way to ask "when did it arrive".
static int first_echo_index(Flux& f, int n) {
    float peak = 0.f;
    int at = -1;
    for (int i = 0; i < n; ++i) {
        float l = (i < 32) ? 1.f : 0.f;
        float r = l;
        f.process(l, r);
        if (i > 500 && std::fabs(l) > peak) { peak = std::fabs(l); at = i; }
    }
    return peak > 1e-3f ? at : -1;
}

TEST_CASE("flux: synced 1/4 at 120 BPM = 0.5 s echo") {
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);                   // slice 3 -> kDivisions[8] "1/4"
    f.set_feedback(0.f);
    f.set_mix(1.f);
    CHECK(f.delay_time() == doctest::Approx(0.5f).epsilon(0.001));
    const int idx = first_echo_index(f, 40000);
    REQUIRE(idx > 0);
    CHECK(idx > 23400);
    CHECK(idx < 24700);
}

TEST_CASE("flux: the clock law reaches the line") {
    // RATE is a tone control as much as a time control now: the ladder spans
    // 16x in time at a fixed tempo, which after the ceiling is roughly 8x in
    // brightness at 120 BPM -- 8 kHz at "1/32" down to 1.0 kHz at "1/2".
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_stages(0.8f);              // 8192, the Memory Man
    f.set_rate(0);                   // "1/2" -> 1.0 s @120
    // The 30 ms slew has to run before clock_hz() reflects the target.
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    CHECK(f.stages() == 8192);
    CHECK(f.clock_hz() == doctest::Approx(4096.f).epsilon(0.02));
    f.set_rate(11);                  // "1/32" -> 0.0625 s @120 -> hits the ceiling
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    CHECK(f.clock_hz() == doctest::Approx(bbd_tuning::kClockMaxHz).epsilon(0.001));
}

TEST_CASE("flux: the buffer no longer bounds the delay time") {
    // The t_max clamp is GONE. Delay time is bounded by how dark the user is
    // willing to go, not by a buffer length -- a "1/2" at 20 BPM is 6 s and
    // that is now a legal, very muddy setting.
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(20.f);
    f.set_rate(0);
    CHECK(f.delay_time() == doctest::Approx(6.f).epsilon(0.001));
    for (int i = 0; i < 48000; ++i) {
        float l = 0.2f, r = 0.2f;
        f.process(l, r);
        REQUIRE(std::isfinite(l));
    }
}

TEST_CASE("flux: STAGES is geometric, 512 to 16384, and 0.8 is the Memory Man") {
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    auto settled_stages = [&](float norm) {
        f.set_stages(norm);
        for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
        return f.stages();
    };
    CHECK(settled_stages(0.f) == Flux::kMinStages);
    CHECK(settled_stages(0.8f) == doctest::Approx(8192).epsilon(0.01));
    CHECK(settled_stages(1.f) == Flux::kMaxStages);
    CHECK(settled_stages(0.4f) == doctest::Approx(2048).epsilon(0.01));
}

TEST_CASE("flux: FXT_FLUX_TIME moves the clock -- the test that could not exist before") {
    // The 2026-07-17 spec retired this target with "modulating the delay time
    // makes no musical sense" -- true of a crossfade delay, false of a BBD,
    // where clock modulation IS the sound generation.
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_stages(0.8f);
    f.set_rate(3);                   // 0.5 s -> 8192 Hz
    f.set_time_mod(0.5f);            // neutral
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    const float neutral = f.clock_hz();
    CHECK(neutral == doctest::Approx(8192.f).epsilon(0.02));
    f.set_time_mod(0.75f);           // +1 octave
    { float l = 0.f, r = 0.f; f.process(l, r); }
    CHECK(f.clock_hz() == doctest::Approx(neutral * 2.f).epsilon(0.02));
    f.set_time_mod(0.f);             // -2 octaves
    { float l = 0.f, r = 0.f; f.process(l, r); }
    CHECK(f.clock_hz() == doctest::Approx(neutral * 0.25f).epsilon(0.02));
}

TEST_CASE("flux: the lane reaches the clock through the FAST path, not the 30 ms slew") {
    // Two smoothers, two jobs. Had modulation gone through the 30 ms path it
    // would have been a ~5 Hz low-pass and a 4 Hz vibrato would not have
    // survived. set_time_mod must therefore take effect within one sample.
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(3);
    f.set_time_mod(0.5f);
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    const float before = f.clock_hz();
    f.set_time_mod(0.75f);
    { float l = 0.f, r = 0.f; f.process(l, r); }   // exactly ONE sample later
    CHECK(f.clock_hz() > before * 1.9f);
}

TEST_CASE("flux: the ceiling holds when ladder and lane push together") {
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(200.f);
    f.set_stages(1.f);               // 16384
    f.set_rate(11);                  // "1/32"
    f.set_time_mod(1.f);             // x4 on top
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    CHECK(f.clock_hz() == doctest::Approx(bbd_tuning::kClockMaxHz).epsilon(0.001));
}

TEST_CASE("flux: off is bit-exact dry") {
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    for (int i = 0; i < 2000; ++i) {
        const float s = std::sin(0.01f * i) * 0.4f;
        float l = s, r = s;
        f.process(l, r);
        CHECK(l == s);
        CHECK(r == s);
    }
}

TEST_CASE("flux: null buffers never engage") {
    Flux f;
    f.init(48000.f, nullptr, nullptr);
    f.set_on(true, true);
    CHECK(!f.has_buffers());
    CHECK(!f.engaged());
    float l = 0.5f, r = 0.5f;
    f.process(l, r);
    CHECK(l == 0.5f);
}

TEST_CASE("flux: feedback produces decaying repeats") {
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(6);                   // 0.25 s
    f.set_feedback(0.45f);
    f.set_mix(1.f);
    std::vector<float> out(80000);
    for (int i = 0; i < (int)out.size(); ++i) {
        float l = (i < 32) ? 1.f : 0.f;
        float r = l;
        f.process(l, r);
        out[i] = l;
    }
    auto peak_around = [&](int c) {
        float p = 0.f;
        for (int i = c - 900; i < c + 900; ++i) p = std::max(p, std::fabs(out[i]));
        return p;
    };
    const float p1 = peak_around(12000);
    const float p2 = peak_around(24000);
    const float p3 = peak_around(36000);
    CHECK(p1 > 1e-3f);
    CHECK(p2 < p1);
    CHECK(p3 < p2);
}

TEST_CASE("flux: feedback at max blooms but stays bounded") {
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(6);
    f.set_feedback(1.f);             // -> 1.2 coefficient
    f.set_drive(0.5f);
    f.set_mix(1.f);
    float peak = 0.f;
    double late_sq = 0.0;
    int late_n = 0;
    for (int i = 0; i < 480000; ++i) {
        float l = (i < 32) ? 1.f : 0.f;
        float r = l;
        f.process(l, r);
        REQUIRE(std::isfinite(l));
        peak = std::max(peak, std::fabs(l));
        if (i >= 432000) { late_sq += (double)l * l; ++late_n; }
    }
    const float late_rms = static_cast<float>(std::sqrt(late_sq / late_n));
    INFO("peak=" << peak << " late_rms=" << late_rms);
    CHECK(peak > 0.2f);
    CHECK(peak < 12.f);
    CHECK(late_rms > 0.01f);
}

TEST_CASE("flux: init resets the DRIVE guard so a re-init's repeated push isn't swallowed") {
    // Reproduces Spotymod::reinit() -> Instrument::init() -> Flux::init(): a
    // sample-rate change rebuilds BbdEcho, and if Flux::init did not also
    // reset its unchanged-value guard, the next push of the SAME value the
    // user already had dialled in would be swallowed forever.
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_drive(0.7f);
    f.init(48000.f, s_buf_l, s_buf_r);     // simulate the re-init
    f.set_on(true, true);
    f.set_bpm(120.f);
    f.set_rate(6);
    f.set_mix(1.f);
    f.set_feedback(0.9f);
    f.set_drive(0.7f);                      // SAME value as before the re-init
    // With a stale guard this push is dropped and BbdEcho keeps the drive
    // SetDrive(0.f) that Init() left behind -- measurably cleaner.
    float peak = 0.f;
    for (int i = 0; i < 120000; ++i) {
        float l = (i < 480) ? 0.8f * std::sin(0.2f * i) : 0.f;
        float r = l;
        f.process(l, r);
        peak = std::max(peak, std::fabs(l));
    }
    CHECK(peak > 0.05f);
    CHECK(f.drive_norm_for_test() == doctest::Approx(0.7f));
}

TEST_CASE("flux: init resets the STAGES guard the same way") {
    Flux f;
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_stages(0.2f);
    f.init(48000.f, s_buf_l, s_buf_r);
    f.set_on(true, true);
    f.set_stages(0.2f);                     // SAME value again
    for (int i = 0; i < 20000; ++i) { float l = 0.f, r = 0.f; f.process(l, r); }
    CHECK(f.stages() == doctest::Approx(1024).epsilon(0.01));
}

TEST_CASE("flux slice: norm endpoints hit 1/2 and 1/32") {
    CHECK(kFluxRateCount == 12);
    CHECK(kFluxRateOffset == 5);
    CHECK(std::string(kDivisions[kFluxRateOffset + flux_division_index(0.f)].name) == "1/2");
    CHECK(std::string(kDivisions[kFluxRateOffset + flux_division_index(1.f)].name) == "1/32");
    CHECK(std::string(kDivisions[kFluxRateOffset + flux_division_index(3.f/11.f)].name) == "1/4");
}
```

- [ ] **Step 3: Run to verify it fails**

Expected: FAIL at compile — `'set_drive' is not a member of 'spky::Flux'`.

- [ ] **Step 4: Rewrite `flux.h`**

Replace everything above `namespace spky {`'s closing brace with:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>
#include "Utility/dsp.h"
#include "fx/bbd.h"
#include "fx/fx_util.h"
#include "mod/divisions.h"

namespace spky {

// FLUX block: a stereo bucket-brigade echo behind a click-free SoftSwitch,
// echo added onto the signal at FLUX MIX (original topology: send-style,
// full-wet echo).
//
// The class, its name and its public form are unchanged from the tape era --
// SoftSwitch, engaged(), the bit-exact off path, set_rate / set_mix /
// set_feedback / set_bpm, the shared delay-time slew. What changed is behind
// them: there is no read pointer any more. Flux knows only music, BbdLine
// knows only physics, and bbd_clock_hz sits between them.
class Flux {
public:
    // Physical stage counts -- the STAGES control's endpoints. 8192 is a pair
    // of MN3005s, i.e. a Deluxe Memory Man.
    static constexpr int kMinStages = bbd_tuning::kMinStages;
    static constexpr int kMaxStages = bbd_tuning::kMaxStages;

    // Floats per channel the host must provide. The NAME and MEANING are
    // unchanged (every FxMem consumer keeps compiling); only the value moved,
    // from 262144 to kMaxStages/2. A two-phase BBD stores one sample per TWO
    // stages -- see the "even ticks write, odd ticks read" comment on
    // BbdLine. 8192 floats x 4 lines = 128 KB, against 4.19 MB before.
    static constexpr size_t kMaxSamples = kMaxStages / 2;

    void init(float sample_rate, float* buf_l, float* buf_r);
    void set_on(bool on, bool immediate = false) { _sw.set_on(on, immediate); }
    bool is_on() const { return _sw.is_on(); }
    bool engaged() const { return _buf_ok && (_sw.is_on() || !_sw.is_idle()); }
    bool has_buffers() const { return _buf_ok; }
    void set_bpm(float bpm);
    void set_rate(int slice_idx);
    float delay_time() const { return _delay_time; }
    void set_feedback(float norm);
    void set_mix(float norm);
    void set_drive(float norm);      // 0..1 -> -6..+24 dB INSIDE the loop
    void set_stages(float norm);     // 0..1 -> 512..16384, geometric
    // FXT_FLUX_TIME. Pulls MULTIPLICATIVELY on the clock, downstream of the
    // base time, so it rides PartFx's 2 ms smoother and not the 30 ms
    // ladder slew -- a 4 Hz vibrato would not survive the latter.
    void set_time_mod(float norm);
    void process(float& l, float& r);

    // Observers for tests: the clock and the stage count are the only two
    // numbers that make "the ladder, the lane and the ceiling all landed
    // where the spec says" assertable at all.
    int stages() const { return _stages_now; }
    float clock_hz() const { return _clock_hz; }
    float drive_norm_for_test() const { return _drive_norm; }

private:
    void recompute_time(bool immediate);

    BbdEcho _echo_l;
    BbdEcho _echo_r;
    SoftSwitch _sw;
    float _mix_lin = 0.f;
    bool  _buf_ok = false;
    float _sr = 48000.f;
    float _bpm = 120.f;
    int   _rate_idx = 3;             // "1/4"
    float _delay_time = 0.5f;
    // Shared L/R delay-time slew (both channels always run the same length).
    // It stays, and it now doubles as the VCO slew of the real circuit:
    // division changes are click-free AND bend in pitch, like the hardware.
    float _dt_current = 0.05f;
    float _dt_target = 0.05f;
    float _dt_coef = 1.f;
    // STAGES rides the SAME 30 ms slew. Stage count is a buffer length, not a
    // continuous quantity; changing it means swapping the chip, and that
    // clicks. Slewing it is not what a physical part does, but it produces
    // exactly the class of artefact this device already makes -- a drift in
    // time and pitch -- which turns STAGES into a playable gesture rather
    // than a setup control.
    float _stage_current = 8192.f;
    float _stage_target = 8192.f;
    int   _stages_now = 8192;
    float _time_mult = 1.f;
    float _clock_hz = 0.f;
    // Unchanged-value guards: set_stages runs a powf and set_drive a pow10f,
    // and both are forwarded at control rate. -1 is unreachable for a
    // clamped 0..1 norm, so the FIRST push after init always forwards.
    float _drive_norm = -1.f;
    float _stages_norm = -1.f;
};

} // namespace spky
```

- [ ] **Step 5: Rewrite `flux.cpp`**

```cpp
#include "fx/flux.h"
#include "util/math.h"

using namespace spky;

namespace {
inline float dbfs2lin(float db) { return daisysp::pow10f(db * 0.05f); }
constexpr float kBootStagesNorm = 0.8f;   // 512 * 32^0.8 == 8192, the DMM
}

void Flux::init(float sample_rate, float* buf_l, float* buf_r) {
    _sw.init(sample_rate);
    _sr = sample_rate;
    _buf_ok = (buf_l != nullptr && buf_r != nullptr);
    if (!_buf_ok) return;
    _echo_l.Init(sample_rate, buf_l, kMaxSamples);
    _echo_r.Init(sample_rate, buf_r, kMaxSamples);
    // Short slew: click-free division changes, locks to grid (~30 ms lag).
    _dt_coef = daisysp::fmin(1.f / (0.03f * sample_rate), 1.f);
    _rate_idx = 3;               // boot "1/4"
    _bpm = 120.f;
    _time_mult = 1.f;
    // Both guards restart from an unreachable value: BbdEcho::Init has just
    // reset the drive to 0 and the stage count to its own default, so a
    // repeated push of the value the user already had dialled in must NOT be
    // swallowed. Same trap the tape-era DUST/ROT guards carried, same fix.
    _drive_norm = -1.f;
    _stages_norm = -1.f;
    set_stages(kBootStagesNorm);
    _stage_current = _stage_target;
    _stages_now = static_cast<int>(_stage_current + 0.5f);
    _echo_l.SetStages(_stages_now);
    _echo_r.SetStages(_stages_now);
    recompute_time(true);        // snap the boot delay time
    set_feedback(0.45f);
    set_mix(0.5f);
    set_drive(0.f);
}

void Flux::set_bpm(float bpm) {
    if (bpm == _bpm) return;
    _bpm = bpm;
    recompute_time(false);
}

void Flux::set_rate(int slice_idx) {
    if (slice_idx == _rate_idx) return;
    _rate_idx = slice_idx;
    recompute_time(false);
}

void Flux::recompute_time(bool immediate) {
    if (!_buf_ok) return;
    int slice = _rate_idx < 0 ? 0
              : (_rate_idx >= kFluxRateCount ? kFluxRateCount - 1 : _rate_idx);
    float hz = division_hz(kFluxRateOffset + slice, _bpm);
    float t = (hz > 0.f) ? 1.f / hz : 0.5f;
    // The buffer-safety clamp is GONE: delay time is no longer bounded by
    // buffer length, only by how dark the user is willing to go. The 60 s
    // ceiling is a sanity bound against a pathological tempo, not a musical
    // limit -- at 512 stages that is already a 4.3 Hz clock.
    _delay_time = clampf(t, 0.001f, 60.f);
    _dt_target = _delay_time;
    if (immediate) _dt_current = _delay_time;
}

void Flux::set_feedback(float norm) {
    if (!_buf_ok) return;
    // Up to ~120 %: self-oscillation stays reachable, documented behaviour of
    // the original. The bound now comes from saturation WITHIN the loop
    // (BbdEcho) rather than a fast_tanh on the read path.
    float fb = clampf(norm, 0.f, 1.f) * 1.2f;
    _echo_l.SetFeedback(fb);
    _echo_r.SetFeedback(fb);
}

void Flux::set_mix(float norm) {
    if (!_buf_ok) return;
    _mix_lin = dbfs2lin(daisysp::fmap(clampf(norm, 0.f, 1.f), -40.f, 0.f));
}

void Flux::set_drive(float norm) {
    if (!_buf_ok) return;
    const float d = clampf(norm, 0.f, 1.f);
    if (d == _drive_norm) return;
    _drive_norm = d;
    _echo_l.SetDrive(d);
    _echo_r.SetDrive(d);
}

void Flux::set_stages(float norm) {
    if (!_buf_ok) return;
    const float n = clampf(norm, 0.f, 1.f);
    if (n == _stages_norm) return;
    _stages_norm = n;
    // Geometric: 512 * (16384/512)^n == 512 * 32^n. Five octaves of
    // brightness at fixed delay time -- grainy, dark and image-rich at the
    // bottom, clean and fast at the top. Physically this is swapping the
    // chip; no pedal exposes it. Control rate, behind the guard above.
    _stage_target = static_cast<float>(kMinStages)
                  * std::pow(static_cast<float>(kMaxStages) / kMinStages, n);
}

void Flux::set_time_mod(float norm) {
    _time_mult = bbd_time_mult(norm);
}

void Flux::process(float& l, float& r) {
    if (!_buf_ok) return;
    float send = _sw.process();
    if (_sw.is_idle()) return;   // fully off: bit-exact dry

    // Both slews advance exactly ONCE per sample, before anything reads them.
    daisysp::fonepole(_dt_current, _dt_target, _dt_coef);
    daisysp::fonepole(_stage_current, _stage_target, _dt_coef);

    const int stages = static_cast<int>(_stage_current + 0.5f);
    if (stages != _stages_now) {
        _stages_now = stages;
        _echo_l.SetStages(stages);
        _echo_r.SetStages(stages);
    }

    // Base clock from the ladder, then the lane pulls multiplicatively on it,
    // then the ceiling -- applied AFTER the lane, so ladder and lane pushing
    // together still cannot overrun it.
    const float hz = clampf(bbd_clock_hz(_dt_current, stages) * _time_mult,
                            0.f, bbd_tuning::kClockMaxHz);
    _clock_hz = hz;

    l += _echo_l.Process(l * send, hz) * _mix_lin;
    r += _echo_r.Process(r * send, hz) * _mix_lin;
}
```

- [ ] **Step 6: Update the RATE ladder comment**

`engine/mod/divisions.h:41-46` — replace the buffer-fit justification, which is now false:

```cpp
// FLUX synced-delay rate: a slice of kDivisions starting at "1/2" (idx 5)
// through "1/32" (idx 16) — 12 rungs, incl. dotted & triplet. Since FLUX
// became a BBD the ladder is a TONE control as much as a time control: the
// 16x span in time is roughly 8x in BBD bandwidth once the 32 kHz clock
// ceiling is applied (8 kHz at "1/32" down to 1.0 kHz at "1/2" at 120 BPM),
// and further down as the tempo drops. There is no buffer to fit inside any
// more. Names come from kDivisions[kFluxRateOffset + i].
```

- [ ] **Step 7: Run the tests**

```bash
source env.sh && cmake --build build && ./build/spky_tests -tc="flux*,bbd*" -s
```

Expected: all `flux:` and `bbd` cases pass. Two likely first failures and their meaning:
- *"synced 1/4 = 0.5 s" arrival too early/late by a constant factor of 2 or 4* → the two-phase mapping is wrong. Check `ticks_ = 2*hz/sr` and `cells = stages/2` together; they only give the right delay as a pair.
- *`clock_hz()` reads 0* → `process()` returned at the `is_idle()` gate; the test must call `set_on(true, true)` before pumping samples.

- [ ] **Step 8: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Everything except `ctrl_identity` should pass unchanged. **`ctrl_identity` will fail**: it is a render-hash gate over a scenario whose audio path just changed. Re-baseline it in Task 10, not here — record the failure in the task report and move on.

- [ ] **Step 9: Commit**

```bash
git add engine/fx/flux.h engine/fx/flux.cpp engine/fx/bbd.h engine/mod/divisions.h tests/test_flux.cpp
git commit -m "feat(flux): the clock is the instrument -- FLUX becomes a bucket-brigade delay"
```

---

### Task 8: Wire it into the part — DRIVE, STAGES, and a target that finally lands

**Files:**
- Modify: `engine/fx/part_fx.h:48-49`, `engine/fx/part_fx.cpp:38`
- Modify: `engine/instrument.h:101-102`
- Modify: `engine/parts/part.h:334,375`
- Modify: `tests/test_part_fx.cpp`

**Interfaces:**
- Consumes: `Flux::set_drive`, `Flux::set_stages`, `Flux::set_time_mod` (Task 7).
- Produces: `PartFx::set_drive(float)`, `PartFx::set_stages(float)`, `Instrument::set_drive(int, float)`, `Instrument::set_stages(int, float)`.
- Removes: `PartFx::set_dust`, `PartFx::set_rot`, `Instrument::set_dust`, `Instrument::set_rot`.

- [ ] **Step 1: Write the failing tests**

In `tests/test_part_fx.cpp`, replace the case `"part_fx: synced rate + BPM place the echo, not FXT_FLUX_TIME"` (line 139) with its inverse, and add two more. The file's existing `fill(v, ...)` helper (line 12) already carries `FXT_FLUX_TIME` as its second slot.

```cpp
TEST_CASE("part_fx: FXT_FLUX_TIME reaches the clock -- and RATE still sets the base") {
    // The 2026-07-17 spec retired this target; the BBD reactivates it. This
    // case is the inverse of the one it replaces: the lane MUST move the
    // clock now, while the ladder still decides what it moves relative to.
    PartFx fx;
    fx.init(48000.f, s_pf_l, s_pf_r);
    fx.set_fx_on(FxBlock::Flux, true, true);
    fx.set_bpm(120.f);
    fx.set_flux_rate(3);                  // "1/4" -> 0.5 s
    fx.set_flux_mix(1.f);
    float v[FXT_COUNT];
    fill(v, 0.f, 0.5f, 1.f, 0.f, 0.f);    // FXT_FLUX_TIME neutral
    for (int i = 0; i < 40000; ++i) {
        float l = 0.f, r = 0.f, sl = 0.f, sr = 0.f;
        fx.process(l, r, sl, sr, v);
    }
    const float neutral = fx.flux().clock_hz();
    CHECK(neutral > 0.f);
    fill(v, 0.f, 1.f, 1.f, 0.f, 0.f);     // FXT_FLUX_TIME hard up -> x4
    for (int i = 0; i < 2000; ++i) {      // past PartFx's 2 ms smoother
        float l = 0.f, r = 0.f, sl = 0.f, sr = 0.f;
        fx.process(l, r, sl, sr, v);
    }
    INFO("neutral=" << neutral << " modulated=" << fx.flux().clock_hz());
    CHECK(fx.flux().clock_hz() > neutral * 3.f);
}

TEST_CASE("part_fx: the FLUX TIME lane rides the 2 ms path, so a 4 Hz vibrato survives") {
    // PartFx's own OnePole is 2 ms (part_fx.cpp:12). A 4 Hz sine on the lane
    // must still swing the clock by most of its range; through the 30 ms
    // ladder slew it would be a ~5 Hz low-pass and would not.
    PartFx fx;
    fx.init(48000.f, s_pf_l, s_pf_r);
    fx.set_fx_on(FxBlock::Flux, true, true);
    fx.set_bpm(120.f);
    fx.set_flux_rate(3);
    fx.set_flux_mix(1.f);
    float v[FXT_COUNT];
    fill(v, 0.f, 0.5f, 1.f, 0.f, 0.f);
    for (int i = 0; i < 40000; ++i) {
        float l = 0.f, r = 0.f, sl = 0.f, sr = 0.f;
        fx.process(l, r, sl, sr, v);
    }
    float lo = 1e30f, hi = 0.f;
    for (int i = 0; i < 24000; ++i) {     // 0.5 s = two vibrato cycles
        const float m = 0.5f + 0.4f * std::sin(TWO_PI * 4.f * i / 48000.f);
        fill(v, 0.f, m, 1.f, 0.f, 0.f);
        float l = 0.f, r = 0.f, sl = 0.f, sr = 0.f;
        fx.process(l, r, sl, sr, v);
        if (i > 12000) {
            lo = std::min(lo, fx.flux().clock_hz());
            hi = std::max(hi, fx.flux().clock_hz());
        }
    }
    INFO("lo=" << lo << " hi=" << hi << " ratio=" << hi / lo);
    // Full depth 0.1..0.9 is a ratio of 2^(4*0.8) = 9.19; the 2 ms smoother
    // takes some of it back. Anything above 5 proves the vibrato survived.
    CHECK(hi / lo > 5.f);
}

TEST_CASE("part_fx: DRIVE and STAGES reach FLUX") {
    PartFx fx;
    fx.init(48000.f, s_pf_l, s_pf_r);
    fx.set_fx_on(FxBlock::Flux, true, true);
    fx.set_stages(0.4f);
    fx.set_drive(0.6f);
    float v[FXT_COUNT];
    fill(v, 0.f, 0.5f, 1.f, 0.f, 0.f);
    for (int i = 0; i < 20000; ++i) {
        float l = 0.f, r = 0.f, sl = 0.f, sr = 0.f;
        fx.process(l, r, sl, sr, v);
    }
    CHECK(fx.flux().stages() == doctest::Approx(2048).epsilon(0.01));
    CHECK(fx.flux().drive_norm_for_test() == doctest::Approx(0.6f));
}
```

Also add to `tests/test_part.cpp`, near the existing `FXT_FLUX_TIME` cases (line 113):

```cpp
TEST_CASE("part: FXT_FLUX_TIME's base default is the neutral x1") {
    // The BBD reads this target as a multiplier around 0.5 == x1. A base of
    // 0.4 -- the value it carried while the target was retired and unread --
    // would put every un-modulated deck permanently 1.3x off its own synced
    // grid, which is the kind of bug that reads as "the delay is slightly
    // wrong" for weeks.
    Part p;
    p.init(48000.f, 0x1234abcdu, nullptr, nullptr, nullptr, 0);
    CHECK(p.fx_target_value(FXT_FLUX_TIME) == doctest::Approx(0.5f));
}
```

- [ ] **Step 2: Run to verify it fails**

Expected: FAIL at compile (`'set_stages' is not a member of 'spky::PartFx'`) and, once that is fixed, FAIL on the `0.5f` default.

- [ ] **Step 3: Rename the two forwarders**

`engine/fx/part_fx.h:48-49`:

```cpp
    void set_drive(float n)  { _flux.set_drive(n); }
    void set_stages(float n) { _flux.set_stages(n); }
```

`engine/instrument.h:101-102`:

```cpp
    void set_drive(int p, float n)  { _parts[p].fx().set_drive(n); }
    void set_stages(int p, float n) { _parts[p].fx().set_stages(n); }
```

- [ ] **Step 4: Deliver `FXT_FLUX_TIME` to `Flux`**

`engine/fx/part_fx.cpp`, immediately after `_flux.set_feedback(v[FXT_FLUX_FB]);` (line 38):

```cpp
        // v[FXT_FLUX_TIME] was smoothed and then DISCARDED here -- alone
        // among the five targets -- for as long as FLUX was a crossfade
        // delay, where modulating the delay time made no musical sense. In a
        // BBD, clock modulation IS the sound generation, so it lands. This
        // rides the 2 ms smoother above, deliberately NOT the 30 ms ladder
        // slew inside Flux: through that path a 4 Hz vibrato would not
        // survive (spec "Modulation": two smoothers, two jobs).
        _flux.set_time_mod(v[FXT_FLUX_TIME]);
```

- [ ] **Step 5: Move the target's neutral to 0.5**

`engine/parts/part.h:334` and `:375` — change the `FXT_FLUX_TIME` slot (index 1) from `0.4f` to `0.5f` in both `_fxv` and `_fx_base`, and add above them:

```cpp
    // Slot 1 (FXT_FLUX_TIME) is 0.5 because the BBD reads it as a geometric
    // multiplier on the clock with 0.5 == x1. The 0.4 it carried while the
    // target was retired and unread would put every deck 1.3x off its own
    // synced grid.
```

- [ ] **Step 6: Run the affected suites**

```bash
source env.sh && cmake --build build && ./build/spky_tests -tc="part_fx*,part:*,flux*" -s
```

Expected: pass. If "a 4 Hz vibrato survives" lands between 2 and 5, print the ratio: the 2 ms `OnePole` also has a 5e-4 dead-band (`util/onepole.h`) that can eat a slow-moving target — reduce the test's modulation frequency to 2 Hz before touching any smoother, and record which it was.

- [ ] **Step 7: Commit**

```bash
git add engine/fx/part_fx.h engine/fx/part_fx.cpp engine/instrument.h engine/parts/part.h tests/test_part_fx.cpp tests/test_part.cpp
git commit -m "feat(fx): DRIVE/STAGES forwarders and a live FXT_FLUX_TIME"
```

---

### Task 9: The panel — DUST becomes DRIVE, ROT becomes STAGES

Renamed **in place**. Param ids stay, `PART_STRIDE` stays 23, `gen_panel.py:232-234`'s append-only rule is never engaged. Old patches still load; their DUST value lands in DRIVE and their ROT value in STAGES. It sounds different, which full replacement already conceded.

**Files:**
- Modify: `host/vcv/res/gen_panel.py:198-200,398-407`
- Regenerate: `host/vcv/src/generated_panel.hpp`, `host/vcv/res/Spotymod.svg`
- Modify: `host/vcv/res/test_panel.py:50,64,183-207`
- Modify: `host/vcv/src/Spotymod.cpp:55-73,232-235,398-399`
- Modify: `host/vcv/src/init_patch.hpp` (comments + two values)
- Modify: `bench/audition/init_patch.cpp:50-51`
- Modify: `host/vcv/README.md:284`

- [ ] **Step 1: Rename in the generator**

`host/vcv/res/gen_panel.py:197-200`:

```python
# FX bottom row went from two slots to four (spec 2026-07-18 dust-grain-cloud);
# the left two were renamed in place when FLUX became a BBD (spec 2026-07-27):
# DRIVE STAGES GRIT COMP. Pitch 10.50 mm against a 3.0 mm knob radius, so the
# 6.0 mm minimum in test_no_overlap still has room to spare.
FX_BOT   = [44.25, 54.75, 65.25, 75.75]   # DRIV STGS | GRIT COMP
```

`host/vcv/res/gen_panel.py:398-407`:

```python
    # DRIVE / STAGES: the BBD's two voicing controls (spec 2026-07-27
    # flux-bbd-delay). Renamed IN PLACE from DUST / ROT -- same positions,
    # same param ids, PART_STRIDE untouched, so every already-saved .vcv
    # keeps every id it has. A patch's old DUST value lands in DRIVE and its
    # old ROT value in STAGES; it sounds different, which full replacement
    # already conceded.
    Ctl("DRIVE_A",  SMKNOB, FX_BOT[0],     ROW_V2, "DRIV"),
    Ctl("DRIVE_B",  SMKNOB, W - FX_BOT[0], ROW_V2, "DRIV"),
    Ctl("STAGES_A", SMKNOB, FX_BOT[1],     ROW_V2, "STGS"),
    Ctl("STAGES_B", SMKNOB, W - FX_BOT[1], ROW_V2, "STGS"),
```

- [ ] **Step 2: Update the panel tests**

`host/vcv/res/test_panel.py:50` → `'DRIVE_A', 'DRIVE_B', 'STAGES_A', 'STAGES_B',`
`:64` → replace `'DUST', 'DUST', 'ROT', 'ROT',` with `'DRIV', 'DRIV', 'STGS', 'STGS',`
`:183-207` — rename the two test functions and their bodies:

```python
def test_bbd_voicing_params():
    """DRIVE/STAGES are appended at the end of PARAMS, not templated into
    part_controls() -- appending keeps PART_STRIDE unchanged so SONG_A/B,
    every part-B id and every already-appended tail param keep their id.
    They were renamed in place from DUST/ROT (spec 2026-07-27 flux-bbd-delay):
    the POSITIONS are what saved patches depend on, and they did not move."""
    check(g.PART_STRIDE == 23, "PART_STRIDE must stay 23")
    ids = {c.enum: i for i, c in enumerate(g.PARAMS)}
    for e in ("DRIVE_A", "DRIVE_B", "STAGES_A", "STAGES_B"):
        check(e in ids, f"{e} missing")
        check(ids[e] >= 2 * g.PART_STRIDE, f"{e} must be appended, not templated")
    # The rename must not have reordered them: COLOR_B then DRIVE_A, DRIVE_B,
    # STAGES_A, STAGES_B, then REC_A. A reorder here silently remaps every
    # saved patch's two FLUX voicing knobs onto each other.
    check(ids["DRIVE_A"] == ids["COLOR_B"] + 1, "DRIVE_A must follow COLOR_B")
    check(ids["STAGES_B"] + 1 == ids["REC_A"], "REC_A must follow STAGES_B")


def test_bbd_voicing_kind():
    """DRIVE/STAGES must render as the small knob (GLYPH_R[SMKNOB] = 3.0 mm),
    not the big knob (4.2 mm) -- a SMKNOB->BIGKNOB typo still clears
    test_no_overlap's minimum spacing by 0.43 mm, so it would ship silently
    without a kind pin of its own. Read the generated header string, not
    g.PARAMS' in-memory `.kind`."""
    h = g.header()
    for enum in ("DRIVE_A", "DRIVE_B", "STAGES_A", "STAGES_B"):
        check(h.count(f"{{{enum}, WK_SMKNOB,") == 1,
              f"{enum} is not WK_SMKNOB in the generated header")
```

`:221` — `check(ids["REC_A"] > ids["STAGES_B"], "REC must append AFTER the existing tail")`
`:526` — `'DRIVE_A': (44.25, 89.40), 'STAGES_A': (54.75, 89.40),`

- [ ] **Step 3: Regenerate the panel and run the panel tests**

```bash
cd host/vcv/res && python gen_panel.py && python test_panel.py && cd ../../..
git diff --stat host/vcv/src/generated_panel.hpp host/vcv/res/Spotymod.svg
```

Expected: `generated_panel.hpp` shows four enum renames and four label/tip changes, **no id reordering**; `Spotymod.svg` shows four text elements changing. `test_panel.py` reports all checks passing.

- [ ] **Step 4: Rewrite the two tooltips**

`host/vcv/src/Spotymod.cpp:55-73` — replace `RotQuantity` and `DustQuantity`:

```cpp
// STAGES tooltip: the physical stage count of the virtual chip. 8192 is a
// pair of MN3005s, i.e. a Deluxe Memory Man; below that the line gets darker
// and grainier at the same delay time, above it cleaner and faster.
struct StagesQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        const float n = getValue();
        const float s = static_cast<float>(spky::Flux::kMinStages)
            * std::pow(static_cast<float>(spky::Flux::kMaxStages)
                       / spky::Flux::kMinStages, n);
        return string::f("%.0f stages", s);
    }
};

// DRIVE tooltip: gain into a fixed saturation threshold, INSIDE the feedback
// loop -- so each repeat saturates again. Not redundant with GRIT, which runs
// before FLUX and dirties the input once.
struct DriveQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        const float db = spky::bbd_tuning::kDriveLoDb
            + getValue() * (spky::bbd_tuning::kDriveHiDb - spky::bbd_tuning::kDriveLoDb);
        return string::f("%+.1f dB", db);
    }
};
```

`:232-235`:

```cpp
                    else if (c.id == DRIVE_A || c.id == DRIVE_B)
                        configParam<DriveQuantity>(c.id, 0.f, 1.f, init, lbl);
                    else if (c.id == STAGES_A || c.id == STAGES_B)
                        configParam<StagesQuantity>(c.id, 0.f, 1.f, init, lbl);
```

`:398-399`:

```cpp
            inst.set_drive(p, params[p ? DRIVE_B : DRIVE_A].getValue());
            inst.set_stages(p, params[p ? STAGES_B : STAGES_A].getValue());
```

- [ ] **Step 5: Set the defaults**

`host/vcv/src/init_patch.hpp` — the `DUST_A`/`DUST_B`/`ROT_A`/`ROT_B` entries (lines 78-81) currently all read `1.000000000f`. Rename the comments and set the values:

```cpp
     0.150000000f, // DRIVE_A   -- starts low: clean repeats, dirt on demand
     0.150000000f, // DRIVE_B
     0.800000000f, // STAGES_A  -- 512 * 32^0.8 == 8192, a Memory Man
     0.800000000f, // STAGES_B
```

Also update the file's header comment: the snapshot is still `sampler.vcvm (2026-07-24)` for every other slot, and these four are the two the BBD redesign set deliberately — say so in one line so the next person does not "restore" them from the snapshot.

`bench/audition/init_patch.cpp:50-51`:

```cpp
        inst.set_drive(deck, value(deck ? DRIVE_B : DRIVE_A));
        inst.set_stages(deck, value(deck ? STAGES_B : STAGES_A));
```

- [ ] **Step 6: Update the host's memory note**

`host/vcv/README.md:284` — the echo array is now `2 × 2 × 8192` floats = 131,072 B, not 4,194,304 B. Fix the number and the surrounding sentence.

- [ ] **Step 7: Build the plugin and the desktop suite**

```bash
cd host/vcv && ./build-local.sh && cd ../..
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: the plugin links; `tests/test_seed_audition_init.cpp` and `tests/test_vcv_form_song_migration.cpp` still pass (they read `init_patch.hpp` positionally — if either pins the old `1.0` values by index, update the pin and say so in the report).

- [ ] **Step 8: Commit**

```bash
git add host/vcv bench/audition/init_patch.cpp
git commit -m "feat(panel): DUST becomes DRIVE, ROT becomes STAGES -- renamed in place"
```

---

### Task 10: The render host — new actions, a listening scenario, and the re-baseline

**Files:**
- Modify: `host/render/scenario.cpp:140` (the action table)
- Create: `host/render/scenarios/bbd_bloom.json`
- Modify: `CMakeLists.txt:180-188` (the `ctrl_identity` expected hash)
- Modify: `tests/test_scenario.cpp` (add the two new actions to whatever table it pins)

- [ ] **Step 1: Add the two actions**

`host/render/scenario.cpp`, after `else if (a == "set_flux_mix") ...`:

```cpp
    else if (a == "set_drive")            inst.set_drive(e.part, e.value);
    else if (a == "set_stages")           inst.set_stages(e.part, e.value);
    else if (a == "set_flux_rate")        inst.set_flux_rate(e.part, e.ivalue);
```

(`set_flux_rate` has never been reachable from a scenario; the BBD makes RATE a tone control, so a listening scenario needs it.)

- [ ] **Step 2: Write the listening scenario**

Create `host/render/scenarios/bbd_bloom.json`, modelled on `reverb_delay.json`'s A/B base patch — sparse plucks, one deck, the room out of the way so the delay is what is heard:

```json
{
  "sample_rate": 48000,
  "bpm": 110,
  "duration_s": 30,
  "init": [
    {"_comment":"BBD listening check (spec 2026-07-27 flux-bbd-delay). Not a byte gate -- this project does not add checksum gates for new audio paths. It exists so the four claims of the design are audible in one file: the clock bends pitch, STAGES is five octaves of brightness, DRIVE dirties every repeat, and the lane turns the delay into a vibrato."},
    {"action":"set_sync","ivalue":1},
    {"action":"set_rate","part":0,"value":0.2},
    {"action":"set_step","part":0,"flag":true,"ivalue":6},
    {"action":"set_shape","part":0,"value":1.0},
    {"action":"set_range","part":0,"value":0.4},
    {"action":"set_density","part":0,"value":0.4},
    {"action":"set_voice_attack","part":0,"value":0.0},
    {"action":"set_voice_decay","part":0,"value":0.1},
    {"action":"set_target_active","part":1,"slot":4,"flag":false},
    {"action":"set_target_base","part":1,"slot":4,"value":0.0},
    {"action":"set_reverb_mix","value":0.1},

    {"action":"set_fx_on","part":0,"svalue":"flux","flag":true},
    {"action":"set_flux_mix","part":0,"value":0.8},
    {"action":"set_flux_rate","part":0,"ivalue":3},
    {"action":"set_fx_target_base","part":0,"slot":4,"value":0.55},
    {"action":"set_stages","part":0,"value":0.8},
    {"action":"set_drive","part":0,"value":0.15}
  ],
  "events": [
    {"_comment":"1) The clock bends pitch. No crossfade in the physical device and none here: a division change slides.","t":6.0,"action":"set_flux_rate","part":0,"ivalue":6},
    {"t":10.0,"action":"set_flux_rate","part":0,"ivalue":0},

    {"_comment":"2) STAGES: five octaves of brightness at a fixed delay time, and a drift in time and pitch on the way.","t":13.0,"action":"set_stages","part":0,"value":0.15},
    {"t":17.0,"action":"set_stages","part":0,"value":1.0},
    {"t":20.0,"action":"set_stages","part":0,"value":0.8},

    {"_comment":"3) DRIVE inside the loop, with feedback high: harmonics accumulate over the repeats and tip into thick self-oscillation.","t":21.0,"action":"set_fx_target_base","part":0,"slot":4,"value":0.9},
    {"t":21.5,"action":"set_drive","part":0,"value":0.85},
    {"t":25.0,"action":"set_drive","part":0,"value":0.15},
    {"t":25.5,"action":"set_fx_target_base","part":0,"slot":4,"value":0.5},

    {"_comment":"4) The lane on FLUX TIME: the whole historical chorus/vibrato range lives in the bottom tenth of this control.","t":26.0,"action":"set_fx_target_active","part":0,"slot":1,"flag":true},
    {"t":26.0,"action":"set_fx_target_base","part":0,"slot":1,"value":0.5},
    {"t":26.0,"action":"set_fx_target_depth","part":0,"slot":1,"value":0.08}
  ]
}
```

- [ ] **Step 3: Render it and listen**

```bash
source env.sh && cmake --build build
./build/render host/render/scenarios/bbd_bloom.json renders/bbd_bloom.wav
```

Expected: a 30 s file with no clicks at the division changes, no runaway level, and four audibly distinct sections. Report what it sounds like — this is a listening check, and its result belongs in the task report in words.

- [ ] **Step 4: Re-baseline `ctrl_identity`**

```bash
./build/render host/render/scenarios/ctrl_identity.json build/ctrl_identity.wav
sha256sum build/ctrl_identity.wav
```

Update `CMakeLists.txt:185`'s `-DEXPECTED=` with the new digest, and `host/render/scenarios/ctrl_identity.sha256` alongside it. In the commit message, state plainly that the hash moved because FLUX's audio path was replaced — that is expected and is the one place this plan changes a pinned render.

`wave_formant_sweep` must **not** move (it runs a WAVE deck with FX off). If it does, stop: something is reaching the dry path that should not be.

- [ ] **Step 5: Run the suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: everything green, including `ctrl_identity` on its new baseline.

- [ ] **Step 6: Commit**

```bash
git add host/render tests/test_scenario.cpp CMakeLists.txt renders/bbd_bloom.wav
git commit -m "feat(render): BBD scenario actions and a listening check; re-baseline ctrl_identity"
```

---

### Task 11: The bench — the CPU gate

> The design is not accepted until that number is in.

`bench/workloads_bbd.cpp` replaces `workloads_taps.cpp` and measures **the ceiling case** — shortest division, STAGES at maximum, high feedback, both decks.

**Files:**
- Create: `bench/workloads_bbd.cpp`
- Modify: `bench/families.h`, `bench/families.cpp`, `bench/Makefile:36,43,52`
- Modify: `bench/run.py`, `bench/profiles.py`, `bench/test_run_contract.py`
- Modify: `bench/workloads_system.cpp` (add `instrument_worst_bbd`)
- Modify: `bench/README.md`

- [ ] **Step 1: Write the workload**

Create `bench/workloads_bbd.cpp`:

```cpp
#include "workload.h"
#include "mem.h"
#include "fx/bbd.h"
#include <cmath>

namespace bench {
namespace {

using namespace spky;

// --- BbdEcho: one line at the ceiling ---------------------------------------
// taps_2_opt (bench/workloads_taps.cpp, now deleted) priced two mono reads
// and two one-poles. This row prices what replaced them, and it is priced at
// the WORST case rather than a typical one, because the whole CPU argument of
// the redesign is "the taps pay for the BBD and nothing more" and a typical
// figure cannot settle that.
//
// The worst case is the CLOCK, not the delay time: you pay CPU per Hz of BBD
// bandwidth and delay time does not enter (spec "One correction recorded on
// purpose"). So this row runs the clock at its 32 kHz ceiling -- 2*32000/48000
// = 1.33 ticks per audio sample, 0.67 write events and 0.67 read events --
// and STAGES at its maximum, which is where the cell array is largest and
// least cache-friendly. One line, because BbdEcho is per channel and the
// two-part instrument cost is this row four times.

constexpr int kBbdMaxCells = 8192;      // Flux::kMaxSamples

BbdEcho g_echo;

void setup_bbd_ceiling()
{
    g_echo.Init(kSampleRate, sdram_arena(), kBbdMaxCells);
    g_echo.SetStages(bbd_tuning::kMaxStages);
    g_echo.SetDrive(0.85f);             // deep into the saturator every pass
    g_echo.SetFeedback(1.1f);           // just under the bloom, loop always hot

    // Settle: the line must be FULL before measuring, or the loss pole, the
    // compander envelopes and the feedback path all run on zeros and the row
    // measures an empty machine. At the ceiling clock a full line is
    // 16384/(2*32000) = 256 ms = 12288 samples; run four times that.
    for (int i = 0; i < 49152; ++i)
        g_echo.Process(0.3f * sinf(static_cast<float>(i) * 0.01f),
                       bbd_tuning::kClockMaxHz);
}

float proc_bbd_ceiling()
{
    const float* in = test_input();
    float acc = 0.f;
    for (size_t s = 0; s < kBlock; ++s)
        acc += g_echo.Process(in[s], bbd_tuning::kClockMaxHz);
    return acc;
}

// --- BbdLine alone: the model without the compander or the drive path -------
// Splits the bill. If the ceiling row comes in over budget, this says whether
// the cost is the filter branches and the event work (which the spec's two
// pre-authorised levers address: drop the clock ceiling to 24 kHz, drop
// kMaxStages to 8192) or the compander's two sqrtf per sample (which they do
// not).

BbdLine g_line;

void setup_bbd_line_only()
{
    g_line.Init(sdram_arena(), kBbdMaxCells, kSampleRate);
    g_line.SetStages(bbd_tuning::kMaxStages);
    g_line.SetClock(bbd_tuning::kClockMaxHz);
    for (int i = 0; i < 49152; ++i)
        g_line.Process(0.3f * sinf(static_cast<float>(i) * 0.01f));
}

float proc_bbd_line_only()
{
    const float* in = test_input();
    float acc = 0.f;
    for (size_t s = 0; s < kBlock; ++s)
        acc += g_line.Process(in[s]);
    return acc;
}

// --- the SDRAM shape --------------------------------------------------------
// The active window at 8192 stages is 4096 cells = 16 KB per line, walked
// SEQUENTIALLY (imem advances by exactly one cell per write tick and the read
// tick reads the cell about to be overwritten). The 3.29x SDRAM penalty
// measured for streaming walks is EXPECTED to largely disappear here. That is
// an expectation, not a measurement -- this row is what turns it into one.

constexpr int kWalkCells = 4096;
int g_walk = 0;

void setup_bbd_walk_sdram()
{
    float* a = sdram_arena();
    for (int i = 0; i < kWalkCells; ++i) a[i] = sinf(static_cast<float>(i) * 0.0007f);
    g_walk = 0;
}

float proc_bbd_walk_sdram()
{
    const float* in = test_input();
    float* a = sdram_arena();
    float acc = 0.f;
    for (size_t s = 0; s < kBlock; ++s) {
        acc += a[g_walk];
        a[g_walk] = in[s];
        g_walk = (g_walk + 1 < kWalkCells) ? g_walk + 1 : 0;
    }
    return acc;
}

} // namespace

const Workload kBbdWorkloads[] = {
    { "bbd", "bbd_ceiling",     setup_bbd_ceiling,     proc_bbd_ceiling     },
    { "bbd", "bbd_line_only",   setup_bbd_line_only,   proc_bbd_line_only   },
    { "bbd", "bbd_walk_sdram",  setup_bbd_walk_sdram,  proc_bbd_walk_sdram  },
};
const int kBbdCount = sizeof(kBbdWorkloads) / sizeof(kBbdWorkloads[0]);

} // namespace bench
```

- [ ] **Step 2: Register the family**

`bench/families.h`: replace the `kTapsWorkloads`/`kTapsCount` externs with `kBbdWorkloads`/`kBbdCount`.

`bench/families.cpp`: replace the taps block with

```cpp
#if BENCH_FAMILY_BBD
    { "bbd",     kBbdWorkloads,     kBbdCount     },
#endif
```

`bench/Makefile`: `BENCH_FAMILIES ?= system voice mem mod abl bbd body sampler`; `FAMILY_SOURCE_bbd = workloads_bbd.cpp`; `FAMILY_DEFINE_bbd = BENCH_FAMILY_BBD`.

`bench/run.py`: add to `BENCH_PROTOCOL_ROWS_BY_FAMILY`

```python
    "bbd": (
        "bbd_ceiling",
        "bbd_line_only",
        "bbd_walk_sdram",
    ),
```

and add `"instrument_worst_bbd",` to the `system` tuple where `instrument_worst_taps` used to be.

`bench/profiles.py:39`: `"system", "voice", "mem", "mod", "abl", "bbd", "body", "sampler",`
`bench/test_run_contract.py:31`: `ALL_FAMILIES = "system voice mem mod abl bbd sampler"`; `:842`: swap `"taps"` for `"bbd"`.

- [ ] **Step 3: Add `instrument_worst_bbd` to the system family**

`bench/workloads_system.cpp`, where `setup_inst_worst_taps` used to be:

```cpp
// --- 10. the whole instrument, FLUX at the BBD's ceiling ---------------------
// instrument_worst never touches the FLUX voicing controls, so the combined
// worst case would otherwise be an extrapolation. This row measures
// instrument_worst's exact configuration plus STAGES at maximum and DRIVE
// high on both parts, with the FLUX rate pushed to the shortest division so
// the clock sits on its ceiling.
//
// Unlike the tap row this replaces, there is nothing to wait for: the BBD's
// cost does not depend on the OTHER deck's rhythm becoming valid, so the
// runner's fixed 100-block warm-up is enough. That simplification is the
// point -- the worst case is now constant and knowable, which is exactly what
// the design claimed.
void setup_inst_worst_bbd()
{
    setup_inst_worst();
    auto& group = g_system_arena.get<InstrumentGroup>();
    auto& inst = group.instrument;
    for (int p = 0; p < PART_COUNT; ++p) {
        inst.set_stages(p, 1.f);            // 16384: the largest cell array
        inst.set_drive(p, 0.85f);           // saturating every pass
        inst.set_flux_rate(p, kFluxRateCount - 1);   // "1/32" -> clock ceiling
        inst.set_fx_target_base(p, FXT_FLUX_FB, 0.9f);
    }
    // Fill both lines and settle every envelope before the runner measures.
    const float* in = test_input();
    for (int b = 0; b < 200; ++b)
        inst.process(in, in, group.out_l, group.out_r, kBlock);
}
```

and register it in `kCoreWorkloads`:

```cpp
    { "system", "instrument_worst_bbd", setup_inst_worst_bbd, proc_inst },
```

- [ ] **Step 4: Verify it builds for the target**

```bash
cd bench && python run.py --profile system --build-only && cd ..
```

Expected: links. If SRAM overflows, that is a profile problem and not this task's to solve — report it with the region sizes.

- [ ] **Step 5: Take the measurement (hardware, user-run)**

This step needs a Daisy Seed and an ST-Link. It is the gate the spec names, and it is the user's to run:

```bash
cd bench
python run.py --profile system --build-only
# connect ST-Link to the Seed's SWD header, power the Seed
python run.py --profile system --no-build --program-qspi --build-only
python run.py --profile system --repeat 2
```

Record `instrument_worst_bbd`'s average and maximum against `instrument_worst`, in `docs/bench/<date>-<hash>-system.md`, in the same shape as `docs/bench/2026-07-26-518f639-system.md:64-65,95-96`. The reference points:

| Row | Was |
|---|---|
| `instrument_worst` | 97.5 % max |
| `instrument_worst_taps` | 96.9 % avg, 101.8–102.2 % max |

Then run the `bbd` family for the isolated rows:

```bash
python run.py --profile full --repeat 2      # or a bbd-only profile if full still does not link
```

**If `instrument_worst_bbd` comes in over budget**, the spec pre-authorises two levers, neither of which changes the design:
1. Drop `bbd_tuning::kClockMaxHz` from 32000 to 24000 (6 kHz bandwidth, still above the filter chain).
2. Drop `bbd_tuning::kMaxStages` from 16384 to 8192.

A third lever is available and is an implementation detail, not a design change: the filter poles come as one real pole plus one conjugate pair, so the three complex branches can be folded into one real branch plus `2·Re()` of one complex branch — roughly halving the per-sample filter arithmetic with no change in output. Use it before either spec lever if `bbd_line_only` is where the cost sits.

Do not tune anything else to make a number. Report the measurement as measured.

- [ ] **Step 6: Update the bench README**

`bench/README.md:362` — replace the `workloads_taps.cpp` clause with `workloads_bbd.cpp` for the BBD line, and add one sentence about what `bbd_ceiling` measures and why it is a ceiling and not a typical case.

- [ ] **Step 7: Commit**

```bash
git add bench
git commit -m "bench(bbd): the CPU gate -- ceiling case, line-only split, SDRAM walk"
```

---

### Task 12: Documentation, attribution, and the ear pass

**Files:**
- Modify: `THIRD_PARTY.md`, `CREDITS.md`
- Modify: `docs/roadmap.md`
- Modify: `README.md` (the FLUX description, wherever it appears)
- Modify: `docs/superpowers/specs/2026-07-27-flux-bbd-delay-design.md` (errata)

- [ ] **Step 1: Attribution**

`THIRD_PARTY.md` — add an entry for `jpcima/bbd-delay-experimental`: what was taken (the combined BBD + filter model of Holters & Parker, DAFx-18, as implemented in `bbd_line.cc` / `bbd_filter.cc`), where it lives (`engine/fx/bbd.{h,cpp}`), the licence (Boost Software License 1.0, reproduced in full), and how the port differs (float, no heap, no `std::complex`, our own filter chain, an added charge-transfer loss pole).

`CREDITS.md` — a line for Holters & Parker (DAFx-18) and Raffel & Smith (DAFx-10) as the papers behind the model, and jpcima for the reference implementation.

- [ ] **Step 2: Roadmap**

`docs/roadmap.md` — add a section for the redesign in the same shape as the existing FX sections (M1.6, line 126 onwards). It must record:

- what FLUX is now: an interpolating tape echo became a bucket-brigade model where the clock rate *is* the delay time.
- the four consequences, in one line each: delay is clock rate; changing the clock bends stored pitch; bandwidth follows the clock; chorus is clock modulation.
- the two numbers that were corrected on the way: the clock relation (`f_clk = N/(2·t_d)`, not `2N/t_d` — the spec's own recorded correction) and the cell count (`stages/2`, not `stages` — this plan's, with the two-phase reason).
- the memory result: `FxMem::echo` 4.19 MB → 128 KB across four lines.
- the CPU result **as measured in Task 11**, not as estimated.
- what was deleted: the tap bank, and why (a BBD has no read pointer).

Also fix `docs/roadmap.md:736`, which says the tape tap is left in place "because FLUX is being redesigned" — it is redesigned now; restate whether the tap stays and on what grounds.

- [ ] **Step 3: Spec errata**

Append an errata section to `docs/superpowers/specs/2026-07-27-flux-bbd-delay-design.md`. Three items, each a fact discovered during implementation:

1. **Memory: `kMaxStages/2`, not `kMaxStages`.** The line stores one sample per two stages because the model's even ticks write and its odd ticks read — that alternation *is* the two-phase clock. 128 KB across four lines, not 256 KB.
2. **Events per sample are per kind.** The tick rate at the ceiling is 1.33/sample: 0.67 writes and 0.67 reads. The spec's 0.67 is right per kind.
3. **Risk 3's SIMD clause does not apply to the reference actually used.** `jpcima/bbd-delay-experimental` is plain scalar `std::complex<double>`; `SSEComplex` belongs to the ChowDSP/Surge variants, which are GPLv3 and were reference reading only. What the port did need: float, injected memory, no `std::complex`, and a single init-time filter build in place of the mutex-guarded cache. The `powf`/`cosf`-per-event risk was already solved upstream by the `G` interpolation table.

Add a fourth item if Task 3's spike or Task 11's gate changed anything.

- [ ] **Step 4: The ear pass**

Everything below was set from physics or from a derivation, never by listening, and the spec is explicit that this instrument's voicing is set by ear. Play the plugin and check each one; change what wants changing and record the reason:

| Constant | Set from | What to listen for |
|---|---|---|
| `kFilterHz` = 3600 | "roughly six poles around 3.5–4 kHz" | whether the top end is the DMM's or too polite |
| `kLossCoef` (corner at `f_clk/4`) | the spec's `f_-3dB ≈ f_clk/4` | whether long delays get dark fast enough — or too fast |
| `kCompRef` = 0.1, `kCompTauS` = 0.010 | the NE570's τ = 10 kΩ × 1 µF | whether the tails breathe, or pump |
| `kSatCeil` = 0.9 | the MN3005's 0.9 V_RMS | where self-oscillation tips from thick to nasty |
| `kDriveLoDb`/`kDriveHiDb` = −6/+24 | the spec | whether two thirds up is really where harmonics start |
| `init_patch.hpp` DRIVE 0.15, STAGES 0.8 | "STAGES defaults to 8192, DRIVE starts low" | whether the instrument ships as a Memory Man |

Once set by ear, these become by-ear values: record them in memory as such so a later session does not "fix" them back to their derived starting points.

- [ ] **Step 5: Full verification**

```bash
source env.sh && cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
cd host/vcv && ./build-local.sh && cd ../..
cd host/vcv/res && python test_panel.py && cd ../../..
cd bench && python -m pytest test_run_contract.py test_task8_contract.py -q && cd ..
```

Paste the actual output into the task report. No success claims without it.

- [ ] **Step 6: Commit**

```bash
git add THIRD_PARTY.md CREDITS.md docs README.md
git commit -m "docs(bbd): attribution, roadmap, spec errata, and the by-ear pass"
```

---

## Verification checklist for the whole plan

Run at the end, before calling the work done. Every line needs its command's real output in the report.

- [ ] `ctest --test-dir build --output-on-failure` — all green, `ctrl_identity` on its new baseline, `wave_formant_sweep` **unmoved**.
- [ ] `grep -rn "taps\|TapBank\|derive_offsets\|set_dust\|set_rot" engine host bench tests CMakeLists.txt` — no hits outside `docs/` and `.superpowers/`.
- [ ] `grep -rn "src/core" CMakeLists.txt bench/Makefile host/vcv/Makefile bench/audition/Makefile` — no hits (the pre-fork originals stay untouched and unbuilt by every engine target).
- [ ] `host/vcv/res/test_panel.py` — `PART_STRIDE == 23`, `DRIVE_A` immediately after `COLOR_B`, `REC_A` immediately after `STAGES_B`.
- [ ] `./build-local.sh` links, and an old `.vcv` patch still loads with its knobs in the right places.
- [ ] `bench/workloads_bbd.cpp` builds for the target and `instrument_worst_bbd` has a real measured number in `docs/bench/`.
- [ ] `renders/bbd_bloom.wav` rendered and listened to; the four sections are audibly what they claim.
- [ ] `Flux::kMaxSamples == 8192`; `sizeof` the VCV host's echo array is 131,072 bytes; `host/vcv/README.md` says so.

---

## Deviations from the spec, in one place

Each is stated where it happens too; this is the index.

1. **Cell count is `stages/2`, not `stages`** (Task 7). Two-phase clock. Memory 128 KB, not 256 KB. Better than the spec; the reason is physical.
2. **The filter chain is two 3rd-order Butterworth sections at 3600 Hz**, derived at init, rather than a vendored pole/residue table (Task 2). The reference's table is the Juno-60's chain at 7–10 kHz — a chorus BBD, not the DMM. Six poles around 3.5–4 kHz is what the spec asks for, and deriving them means no magic numbers and a testable magnitude response.
3. **The charge-transfer loss is one pole at a fixed `f_clk/4` inside the clocked domain** (Task 3). The reference does not model charge-transfer loss at all; the spec's `f_-3dB ≈ f_clk/4` needs it, and a fixed *ratio* means bandwidth tracks the clock with no coefficient recompute.
4. ~~**DRIVE has makeup gain after the saturator** (Task 5). Without it, FEEDBACK means something different at every DRIVE setting and the two knobs fight. Ear pass item.~~ **Retired post-merge-review (2026-07-27 whole-branch review).** The makeup gain described here was the exact law `ce07532` removed: it held small-signal loop gain at unity by shrinking the saturator's ceiling by 30 dB across the knob (1.796 → 0.057), which measured as a 14 dB drop in the actual echo return between DRIVE 0 and DRIVE 1 — the opposite of what a DRIVE control should do. `sat_out_` is now a fixed ceiling (`bbd_tuning::kSatCeil`); DRIVE's range moved to 0..+12 dB (`3dea01a`) to keep self-oscillation reachable at DRIVE 0. See the design spec's errata item 7 for the full accounting. **Reinstated 2026-07-28.** The claim struck through above was right and the retirement was wrong: with the coupling in place, the owner's ear found exactly the fight this line predicts, and measurement confirmed it (the FEEDBACK knob giving a 15 s tail slid 0.57 → 0.14 across DRIVE). What was actually wrong was the *remedy* — makeup gain after the saturator, which paid for the decoupling with the ceiling. `Flux::set_feedback` now divides `bbd_drive_gain()` out of the feedback coefficient instead, which honours this claim and item 7's fixed ceiling at the same time. See errata item 8.
5. **`FXT_FLUX_TIME`'s base default moves from 0.4 to 0.5** (Task 8). The BBD reads the target as a multiplier around 0.5 == ×1; 0.4 would put every un-modulated deck 1.3× off its own synced grid.
6. **`set_flux_rate` is exposed to render scenarios** (Task 10). RATE is a tone control now and a listening scenario cannot demonstrate that without it.
7. **A third CPU lever is named** (Task 11): folding the conjugate-pair filter branches. It is an implementation optimisation, not a design change, and should be spent before either of the spec's two levers.
