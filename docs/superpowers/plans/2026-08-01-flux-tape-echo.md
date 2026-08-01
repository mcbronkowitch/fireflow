# FLUX Tape Echo (movement 3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace FLUX's mono BBD with an injected-memory stereo tape echo, make LINK a full-travel THIN control, preserve old THIN patches, and re-point the BBD/ITCM benchmark gates at the BBD part engine.

**Architecture:** A header-only `InjectedDelayLine`/`TapeBpf`/`TapeEcho` layer owns DSP state but not sample memory. `Flux` owns two `TapeEcho` instances and one shared 30 ms delay-time slew; `FXT_FLUX_TIME` multiplies the ladder time geometrically from x1/4 through x1 to x4. `FxMem` keeps two deliberately different buffer families: `echo[part][channel]` at 262144 floats per line and `bbd[part][channel]` at 8192 floats per line.

**Tech Stack:** C++17; no heap, `<memory>`, `std::complex`, or libm in the per-sample path under `engine/**`; doctest; CMake + Ninja; VCV Rack host; Daisy Seed SDRAM; Python panel and benchmark contract tests.

**Spec:** `docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md`, especially sections 3, 6.1-6.10, 7, 9, 10, and Appendix A.

---

## Decisions signed off before this plan

1. **`FXT_FLUX_TIME` remains live.** It controls tape delay time geometrically: `0 -> x1/4`, `0.5 -> x1`, `1 -> x4`. It uses FLUX's existing 30 ms shared delay-time slew, so tape/Doppler pitch motion is intended. The two-millisecond `PartFx` target smoother remains in front of it.
2. **Patch migration gets its own marker.** New patches write `"linkVersion": 1`; `formSongVersion` stays at 1 and continues to mean FORM/SONG only. Missing or malformed `linkVersion` means legacy bipolar LINK: negative THIN values map to their magnitude, positive DRAG values map to 0, and 0 stays 0. With no version keys, FORM/SONG and LINK migrations both run; they touch disjoint params, so ordering is immaterial.
3. **THIN's shared accumulator becomes a Repeat Scheduler first.** `_drag_phase`/`_drag_step_len` become `_repeat_phase_samples`/`_repeat_period_samples`, and arming leaves `apply_drag()`. This is its own commit with all THIN tests green before any DRAG symbol is removed.

## Global Constraints

- `Flux::kMaxSamples` becomes the tape length, **262144 floats per channel**. BBD allocations use `BbdEngine::kCells` or `bbd_tuning::kMaxStages / 2`, never `Flux::kMaxSamples`.
- `FxMem::echo[PART_COUNT][2]` is stereo tape memory; `FxMem::bbd[PART_COUNT][2]` remains BBD-engine memory. `nullptr` leaves FLUX disengaged/silent, matching the existing host-memory contract.
- `Part::init`'s new parameters remain trailing/defaulted where possible. Update positional echo callers, not the roughly 85 two-argument callers.
- VCV holds the roughly 4 MiB tape memory on the heap using `std::vector`, resized in `Spotymod::reinit()`. It must not become a 4 MiB by-value `Module` member.
- Do not restore DUST/ROT, taps, widening, panning, compander tuning, or a second delay. Do not raise `kClockMaxHz` or lower `kMinStages`.
- Keep `SoftSwitch`, `engaged()`, the fully-off exact dry return, `set_rate`, `set_mix`, `set_feedback`, `set_bpm`, and the one shared time slew.
- Keep the five-slot `FxTargetId` contract and pad-slot-equals-lane-index rule. `FXT_FLUX_TIME` is tested, not deleted or left as a no-op.
- Preserve append-only VCV parameter ids. `DRIVE_A/B` and `STAGES_A/B` may lose a FLUX destination, but their numeric ids are not removed or reordered. `STAGES_A/B` continues to feed `LANE_PITCH` on a BBD deck.
- No heap, `<memory>`, `std::complex`, or libm in the per-sample path under `engine/**`. Init/control-rate `tan`, `pow`, and `exp` are allowed. The per-sample time multiplier remains a LUT lookup and lerp.
- Denormal floor idiom, where required: `if (x < 1e-9f && x > -1e-9f) x = 0.f;`.
- Every commit ends with:
  ```
  Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
  ```
- Build/test from the repo root after sourcing `env.sh`:
  ```bash
  source env.sh
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
  ctest --test-dir build --output-on-failure
  ```
- VCV builds only with:
  ```bash
  cd host/vcv && ./build-local.sh
  ```
- Panel tests are a script, not pytest:
  ```bash
  cd host/vcv/res && python test_panel.py
  ```
- A claimed benchmark row is trusted only after `grep -c <row> bench/build/bench.map` returns at least 1.

---

## File Structure

| File | Responsibility |
|---|---|
| `engine/fx/tape_echo.h` *(create)* | Injected-memory interpolating delay, tape band-pass, bounded feedback echo, and the x1/4..x4 time-multiplier LUT. No owned sample buffer. |
| `engine/fx/flux.h/.cpp` *(modify)* | Stereo tape FLUX, shared delay-time slew, FXT time modulation, THIN repeat gate. No BBD or DRAG state in the final form. |
| `engine/fx/part_fx.h/.cpp` *(modify)* | Inject both tape channels and continue forwarding all five FX targets. |
| `engine/instrument.h/.cpp`, `engine/parts/part.h/.cpp` *(modify)* | Two-family `FxMem`; pass stereo tape memory and stereo BBD memory without changing two-argument callers. |
| `host/vcv/src/link_migration.hpp` *(create)* | Pure, Rack-independent version check and legacy bipolar LINK to unipolar THIN map. |
| `host/vcv/src/Spotymod.cpp` *(modify)* | Heap tape buffers, unipolar LINK UI, independent `linkVersion` migration. |
| `engine/fx/drag.cpp`, `tests/test_drag.cpp` *(delete)* | DRAG-only interval derivation and its tests. `drag.h` stays, reduced to `link_tuning`, to avoid cosmetic include churn. |
| `tests/test_tape_echo.cpp` *(create)* | Injected-memory interpolation, isolation, feedback bound, LUT endpoints. |
| `tests/test_flux.cpp`, `tests/test_part_fx.cpp`, `tests/test_instrument.cpp` *(modify)* | Stereo tape, time modulation, THIN, null memory, and removed-surface contracts. |
| Hosts and benches listed per task | Correctly-sized two-channel tape allocation, source lists, DTCM A/B re-pointing, and same-build figures. |

---

## Task 1: Decouple every BBD allocation from `Flux::kMaxSamples`

This must land before `Flux::kMaxSamples` grows by 32x. Today several BBD tests and workloads size raw BBD lines from the FLUX constant only because both happen to equal 8192. Once FLUX becomes tape, those sites would silently allocate 1 MiB per BBD line and invalidate both BSS and benchmark memory claims.

**Files:**
- Modify: `tests/test_bbd.cpp:767-868`
- Modify: `tests/test_bbd_engine.cpp:16-1381`
- Verify: `tests/test_deck_bus.cpp:201-212`
- Modify: `bench/workloads_bbd.cpp:32,66`
- Modify: `bench/workloads_sweep.cpp:195-206,525-572`
- Modify: `engine/instrument.h:27-58`

**Interfaces:**
- Consumes: `BbdEngine::kCells == bbd_tuning::kMaxStages / 2`.
- Produces: no BBD allocation or `BbdLine::Init` capacity derived from FLUX.

- [ ] **Step 1: Capture the disengaged-deck sanity reference before audio code changes**

```bash
source env.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
./build/render host/render/scenarios/wave_formant_sweep.json build/flux-off-before.wav build/flux-off-before.csv
```

Expected: the render completes and `build/flux-off-before.wav` is non-empty. This is an ear/sanity reference for Task 8, not a hash gate.

- [ ] **Step 2: Run the failing source contract**

```bash
rg -n "Flux::kMaxSamples" tests/test_bbd.cpp tests/test_bbd_engine.cpp \
  bench/workloads_bbd.cpp
rg -n "probe\.init|group\.[lr]\.Init" bench/workloads_sweep.cpp
```

Expected: the first command finds BBD allocations in all three files; the
second shows the temporary FLUX probe on `m.echo` and its two raw BBD lines
also borrowing `m.echo`. Save this output in the task report.

Run the current compiled behavior as the second half of the baseline:

```bash
cmake --build build && ctest --test-dir build -R spky_tests --output-on-failure
```

Expected: green before the constant changes.

- [ ] **Step 3: Re-size BBD-only sites**

Use `BbdEngine::kCells` in engine-level tests and `bbd_tuning::kMaxStages / 2` in raw `BbdEcho`/`BbdLine` tests and `bench/workloads_bbd.cpp`:

```cpp
static constexpr size_t kBbdCells = bbd_tuning::kMaxStages / 2;
static float buf[kBbdCells];
e.Init(48000.f, buf, kBbdCells);
```

In `tests/test_bbd_engine.cpp`, all `init_buffers(..., Flux::kMaxSamples)` calls become:

```cpp
e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
```

and the globals become:

```cpp
static float s_bbd_l[BbdEngine::kCells];
static float s_bbd_r[BbdEngine::kCells];
```

`tests/test_deck_bus.cpp` already follows this rule; do not change it.

- [ ] **Step 4: Stop the sweep workloads borrowing tape buffers**

In `bench/workloads_sweep.cpp`, move only the raw BBD lines to `fx_mem().bbd`.
The temporary `Flux probe` still needs the current FLUX echo buffer in this
commit; Task 4 deletes that probe when its BBD observers disappear:

```cpp
group.l.Init(kSampleRate, m.bbd[PART_A][0], BbdEngine::kCells);
group.r.Init(kSampleRate, m.bbd[PART_A][1], BbdEngine::kCells);
```

Update the nearby comments that currently claim `Flux::kMaxSamples == BbdEngine::kCells`.

- [ ] **Step 5: Remove the cross-family assertion**

Delete `engine/instrument.h:50-58`'s `Flux::kMaxSamples >= BbdEngine::kCells` assertion and replace the field comment with the direct invariant:

```cpp
static_assert(BbdEngine::kCells == bbd_tuning::kMaxStages / 2,
              "BBD engine storage is one cell per two physical stages");
```

- [ ] **Step 6: Verify the separation and build**

```bash
rg -n "Flux::kMaxSamples" tests/test_bbd.cpp tests/test_bbd_engine.cpp bench/workloads_bbd.cpp
rg -n "probe\.init|group\.[lr]\.Init" bench/workloads_sweep.cpp
source env.sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: the first `rg` returns no matches; the second shows `probe.init` still
using `m.echo` and both `BbdEcho::Init` calls using `m.bbd` plus
`BbdEngine::kCells`; the full suite passes.

- [ ] **Step 7: Build the bench and verify the BBD rows are linked**

```bash
make -C bench clean all
grep -c "sweep_flux_lines_2ch" bench/build/bench.map
grep -c "inst_bbd_engine_worst" bench/build/bench.map
```

Expected: both counts are at least 1. No figure is claimed in this task.

- [ ] **Step 8: Commit**

```bash
git add engine/instrument.h tests/test_bbd.cpp \
        tests/test_bbd_engine.cpp bench/workloads_bbd.cpp bench/workloads_sweep.cpp
git commit -F - <<'EOF'
refactor(memory): decouple BBD cells from FLUX tape length

The two constants happened to be 8192 and several raw BBD tests and workloads
borrowed Flux::kMaxSamples. Movement 3 makes FLUX 262144 samples per channel;
letting that propagate would add megabytes to BBD fixtures and make the BBD
bench rows measure a different arena contract.

BBD sites now size from BbdEngine::kCells or kMaxStages/2 directly, and sweep
workloads borrow FxMem::bbd rather than FxMem::echo.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 2: Extract and rename the Repeat Scheduler, with THIN still green

This is the safety commit required by section 6.5. It changes no sound. Both DRAG and THIN temporarily use the neutral repeat scheduler; only after this commit is independently green may Task 5 delete DRAG.

**Files:**
- Modify: `engine/fx/flux.h:97-104,164-191`
- Modify: `engine/fx/flux.cpp:11-51,250-287,299-344`
- Test: `tests/test_flux.cpp:546-809`

**Interfaces:**
- Produces: `Flux::refresh_repeat_scheduler()`, `_repeat_phase_samples`, `_repeat_period_samples`.
- Keeps temporarily: `apply_drag()`, DRAG state, `derive_intervals`, and all DRAG tests.

- [ ] **Step 1: Add the regression that names the ownership boundary**

Append to `tests/test_flux.cpp` beside the existing valid-but-DRAG-unusable THIN case:

```cpp
TEST_CASE("flux: THIN owns a repeat scheduler even when DRAG has no intervals") {
    Flux f;
    f.init(48000.f, s_buf);
    f.set_rate(10);                         // short, easy-to-observe repeat
    f.set_link(-1.f);
    f.set_rhythm(drag_view(6000, 20));      // valid for THIN, invalid for DRAG
    f.set_on(true, true);
    float l = 0.f, r = 0.f;
    bool ducked = false;
    for (int i = 0; i < 18000; ++i) {
        f.process(l, r);
        if (f.gate_for_test() < 0.1f) ducked = true;
    }
    CHECK(ducked);
}
```

Run once on the old code; it should PASS. This is a characterization test whose purpose is to stay green through the extraction.

- [ ] **Step 2: Rename the shared state before extracting behavior**

In `Flux`:

```cpp
float _repeat_phase_samples = 0.f;
float _repeat_period_samples = 0.f;
```

Replace every `_drag_phase`/`_drag_step_len` use, including comments and test prose. At this point both consumers still work exactly as before.

- [ ] **Step 3: Extract arming out of `apply_drag()`**

Declare and implement:

```cpp
void Flux::refresh_repeat_scheduler() {
    const bool thinning = (_thin > 0.f && _rhy_valid);
    const bool dragging = (_drag > 0.f && _drag_active);
    if (thinning)
        _repeat_period_samples = _delay_time * _sr;
    else if (dragging)
        _repeat_period_samples = _dt_target * _sr;
    else {
        _repeat_period_samples = 0.f;
        _repeat_phase_samples = 0.f;
    }
}
```

`apply_drag()` remains the single writer of the transient DRAG target but calls `refresh_repeat_scheduler()` after choosing it. `set_link`, `set_rhythm`, and `recompute_time` reach the scheduler through their existing `apply_drag()` call. Gate reset stays based on `thinning`, not on the DRAG branch.

- [ ] **Step 4: Make `process()` consume the neutral names**

```cpp
if (thinning || dragging) {
    _repeat_phase_samples += 1.f;
    if (_repeat_period_samples > 0.f &&
        _repeat_phase_samples >= _repeat_period_samples) {
        _repeat_phase_samples = 0.f;
        if (dragging) { _drag_i ^= 1; apply_drag(); }
        else          { advance_gate(); }
    }
}
```

- [ ] **Step 5: Run the focused THIN set, then everything**

```bash
source env.sh
cmake --build build
./build/spky_tests --test-case="*THIN*"
./build/spky_tests --test-case="*thinning*"
ctest --test-dir build --output-on-failure
```

Expected: all existing THIN cases and the new characterization case pass. DRAG tests also remain green.

- [ ] **Step 6: Commit this extraction by itself**

```bash
git add engine/fx/flux.h engine/fx/flux.cpp tests/test_flux.cpp
git commit -F - <<'EOF'
refactor(flux): give THIN its repeat scheduler before DRAG leaves

DRAG and THIN shared _drag_phase/_drag_step_len, and THIN was armed from
inside apply_drag(). The shared clock is now explicitly a repeat scheduler;
both consumers still use it and every THIN test is green before any DRAG
symbol is removed.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 3: Build the injected-memory tape primitives

The missing primitive is not interpolation itself; DaisySP already has that. The missing boundary is an interpolating line that accepts host memory instead of owning a template-sized array. Keep it independent of `Flux` so interpolation, feedback, filtering, and buffer wrap are directly testable.

**Files:**
- Create: `engine/fx/tape_echo.h`
- Create: `tests/test_tape_echo.cpp`
- Modify: `CMakeLists.txt:88-96`
- Modify later in this task: `engine/fx/bbd.h:209-232` (move the FLUX-only LUT)
- Modify: `engine/fx/flux.h:6-8`, `engine/fx/flux.cpp:11-22,218-222` (use the renamed LUT while FLUX is still a BBD)

**Interfaces:**
- Produces:
  ```cpp
  template <typename T, size_t MaxSize> class InjectedDelayLine;
  class TapeBpf;
  template <size_t MaxSize> class TapeEcho;
  float tape_time_mult(float norm);  // LUT: 0=>.25, .5=>1, 1=>4
  ```

- [ ] **Step 1: Write the failing primitive tests**

Create `tests/test_tape_echo.cpp`:

```cpp
#include "doctest.h"
#include "fx/tape_echo.h"
#include <algorithm>
#include <cmath>

using namespace spky;

TEST_CASE("injected delay: fractional reads interpolate host memory") {
    float mem[16] = {};
    InjectedDelayLine<float, 16> d;
    d.Init(mem);
    d.SetDelay(2.5f);
    float y[6] = {};
    for (int i = 0; i < 6; ++i) {
        y[i] = d.Read();
        d.Write(i == 0 ? 1.f : 0.f);
    }
    CHECK(y[2] == doctest::Approx(0.5f));
    CHECK(y[3] == doctest::Approx(0.5f));
}

TEST_CASE("injected delay: reset clears exactly the injected arena") {
    float mem[16];
    std::fill(std::begin(mem), std::end(mem), 1.f);
    InjectedDelayLine<float, 16> d;
    d.Init(mem);
    for (float x : mem) CHECK(x == 0.f);
}

TEST_CASE("tape time target spans four octaves without per-call libm") {
    CHECK(tape_time_mult(0.f) == doctest::Approx(0.25f));
    CHECK(tape_time_mult(0.5f) == doctest::Approx(1.f));
    CHECK(tape_time_mult(1.f) == doctest::Approx(4.f));
    CHECK(tape_time_mult(-1.f) == doctest::Approx(0.25f));
    CHECK(tape_time_mult(2.f) == doctest::Approx(4.f));
}

TEST_CASE("tape echo: feedback blooms but remains finite and bounded") {
    static float mem[1024];
    TapeEcho<1024> e;
    e.Init(48000.f, mem);
    e.SetFeedback(1.2f);
    float peak = 0.f;
    for (int i = 0; i < 48000; ++i) {
        const float y = e.Process(i == 0 ? 1.f : 0.f, 240.f);
        REQUIRE(std::isfinite(y));
        peak = std::max(peak, std::fabs(y));
    }
    CHECK(peak <= 1.f);
}
```

Register `tests/test_tape_echo.cpp` next to `tests/test_flux.cpp`. Run and expect a missing-header compile failure.

- [ ] **Step 2: Implement `InjectedDelayLine`**

In `engine/fx/tape_echo.h`, port the historical `DeLine` from `e004a3d^` without the tap accessors:

```cpp
template <typename T, size_t MaxSize>
class InjectedDelayLine {
    static_assert(MaxSize > 1 && (MaxSize & (MaxSize - 1)) == 0,
                  "MaxSize must be a power of two");
    static constexpr int32_t kMask = static_cast<int32_t>(MaxSize) - 1;
public:
    InjectedDelayLine() = default;
    InjectedDelayLine(const InjectedDelayLine&) = delete;
    InjectedDelayLine& operator=(const InjectedDelayLine&) = delete;
    void Init(T* memory) { _line = memory; Reset(); }
    void Reset() {
        std::memset(_line, 0, MaxSize * sizeof(T));
        _write = 0; _delay = 1; _frac = 0.f;
    }
    void SetDelay(float samples) {
        if (samples < 1.f) samples = 1.f;
        if (samples > static_cast<float>(MaxSize - 2))
            samples = static_cast<float>(MaxSize - 2);
        const int32_t whole = static_cast<int32_t>(samples);
        _delay = whole & kMask;
        _frac = samples - static_cast<float>(whole);
    }
    T Read() const {
        const T a = _line[(_write + _delay) & kMask];
        const T b = _line[(_write + _delay + 1) & kMask];
        return a + (b - a) * _frac;
    }
    void Write(T x) { _line[_write] = x; _write = (_write - 1) & kMask; }
private:
    T* _line = nullptr;
    float _frac = 0.f;
    int32_t _write = 0;
    int32_t _delay = 1;
};
```

Only call `Init` after the owning `Flux` has validated both pointers; the primitive deliberately has no bypass policy.

- [ ] **Step 3: Port the stripped tape filter and echo**

Copy `TapeBpf` and `EchoDelay` from `git show e004a3d^:engine/fx/flux.h`, rename `EchoDelay` to `TapeEcho`, and omit all `TapBank`, `line()`, and `write_ptr()` surface. Preserve:

```cpp
float Process(float in, float delay_samples) {
    _line.SetDelay(delay_samples);
    float out = _bpf.Process(_line.Read());
    out = fast_tanh(out);
    _line.Write(out * _feedback + in);
    return out;
}
```

`TapeBpf::Init` may use `std::tan`; it runs only during `Flux::init`.

- [ ] **Step 4: Move and rename the time LUT**

Move `bbd_time_mult` from `engine/fx/bbd.h` to `tape_echo.h` as `tape_time_mult`, preserving its 65-entry, linearly-interpolated x1/4..x4 law. Force the table's one-time `std::pow` construction from the current `Flux::init` before audio starts; calls from `PartFx::process` then execute only clamp, index, and lerp.

For this task's still-BBD `Flux`, include `tape_echo.h`, rename the live call to
`tape_time_mult(norm)`, and force construction during its existing init:

```cpp
_time_mult = tape_time_mult(0.5f);  // also constructs the LUT before audio
```

Task 4 retains the same init-time call when it swaps in `TapeEcho`.

Update tests and benchmark comments that name `bbd_time_mult`; there must be no live-code reference left:

```bash
rg -n "bbd_time_mult" engine tests bench --glob '!build/**' --glob '!bench/build/**'
```

Expected: no matches outside historical docs.

- [ ] **Step 5: Run focused and full verification**

```bash
source env.sh
cmake --build build
./build/spky_tests --test-case="*injected delay*"
./build/spky_tests --test-case="*tape echo*"
ctest --test-dir build --output-on-failure
```

Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add engine/fx/tape_echo.h engine/fx/bbd.h engine/fx/flux.h engine/fx/flux.cpp \
        tests/test_tape_echo.cpp tests/test_flux.cpp bench/workloads_sweep.cpp \
        bench/workloads_instr.cpp CMakeLists.txt
git commit -F - <<'EOF'
feat(flux): an interpolating tape line over injected memory

The tree already had interpolation in DaisySP; the missing boundary was a
line that accepts host-owned memory. InjectedDelayLine, TapeBpf and TapeEcho
restore that primitive without restoring the deleted tap bank.

FXT_FLUX_TIME's four-octave LUT moves out of bbd.h under its actual tape
meaning. Its table is constructed at init; the audio-rate call is clamp,
index and lerp, with no per-sample libm.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 4: Switch FLUX and `FxMem` to the injected stereo tape

This is the central change. It grows tape memory, changes the injection signature, moves VCV tape storage to the heap, restores stereo processing, and disposes of the BBD-only FLUX surface. DRAG remains temporarily reachable in this task so Task 2's green-before-delete proof remains an independent commit; Task 5 removes it and changes the LINK domain.

**Files:**
- Modify: `engine/fx/flux.h`, `engine/fx/flux.cpp`
- Modify: `engine/fx/part_fx.h:32`, `engine/fx/part_fx.cpp:8-10,38-47`
- Modify: `engine/parts/part.h:33-36`, `engine/parts/part.cpp` (`Part::init`)
- Modify: `engine/instrument.h:16-58`, `engine/instrument.cpp:35-46`
- Modify: `host/render/scenario.cpp`, `tests/test_scenario.cpp` (remove retired FLUX DRIVE/STAGES dispatch arms)
- Modify: `host/vcv/res/test_panel.py` (pin heap tape storage)
- Modify allocations: `host/render/main.cpp:12-50`, `host/vcv/src/Spotymod.cpp:169-239,351-371`, `bench/mem.cpp:34-70`, `bench/audition/memory.cpp:14-53`
- Modify obsolete FLUX benchmark setup/readbacks: `bench/workloads_instr.cpp:93-112,339-345,605-656,1068-1269`, `bench/workloads_system.cpp:417-461`, `bench/workloads_sweep.cpp:25-93,230-418,486-608,879-896`, `bench/run.py:198-210`, `bench/test_run_contract.py`
- Modify the audition snapshot dispatcher: `bench/audition/init_patch.cpp:40-78`
- Modify positional/indexing callers: `bench/workloads_instr.cpp:687-691,1077,1465`, `bench/workloads_system.cpp:212-214`, `bench/workloads_sweep.cpp:252,357,538,571-572`, `bench/workloads_abl.cpp`, `bench/workloads_mod.cpp`, and every `rg "\.init\(.*echo|\.echo\["` result
- Modify tests with echo fixtures: `tests/test_flux.cpp`, `tests/test_part_fx.cpp`, `tests/test_instrument.cpp`, `tests/test_part.cpp`, `tests/test_scenario.cpp`, `tests/test_sampler_part.cpp`, `tests/test_bbd_engine.cpp`

**Interfaces:**
- Produces:
  ```cpp
  inline constexpr size_t kTapeSamples = 262144;
  Flux::kMaxSamples == kTapeSamples;
  void Flux::init(float sample_rate, float* left, float* right);
  void PartFx::init(float sample_rate, float* echo_l, float* echo_r);
  FxMem::echo[PART_COUNT][2];
  Part::init(sr, seed, echo_l=nullptr, echo_r=nullptr,
             sampler=nullptr, frames=0, bbd_l=nullptr, bbd_r=nullptr);
  ```
- Removes from `Flux`: `set_drive`, `set_stages`, `stages`, `clock_hz`, `drive_norm_for_test`, `feedback_coef_for_test`.
- Removes their forwarders: `PartFx::{set_drive,set_stages}`, `Instrument::{set_drive,set_stages}`. VCV parameter ids remain allocated.
- Keeps: `set_time_mod`, `set_link`, `set_rhythm`, `drag_time_s` temporarily, `gate_for_test`, `thin_n_for_test`.

- [ ] **Step 1: Capture executable storage before growing the tape fixtures**

```bash
"/c/Program Files/LLVM/bin/llvm-size.exe" build/spky_tests.exe | tee build/spky-tests-size-before.txt
```


Record `data + bss`. Task acceptance is that the final value grows by less than 1 MiB, not by the roughly 20 MiB that naive static tape fixtures would add.

- [ ] **Step 2: Replace the BBD-specific FLUX tests with tape contracts first**

In `tests/test_flux.cpp`, remove/replace tests for clock, stage count, DRIVE, the BBD feedback law, and the mono sum. Add these failing contracts:

```cpp
static std::vector<float> s_flux_l(Flux::kMaxSamples);
static std::vector<float> s_flux_r(Flux::kMaxSamples);

TEST_CASE("flux tape: stereo input remains stereo") {
    Flux f;
    f.init(48000.f, s_flux_l.data(), s_flux_r.data());
    f.set_on(true, true); f.set_mix(1.f); f.set_feedback(0.f);
    float energy_l = 0.f, energy_r = 0.f;
    for (int i = 0; i < 48000; ++i) {
        float l = i == 0 ? 1.f : 0.f;
        float r = 0.f;
        f.process(l, r);
        energy_l += l * l; energy_r += r * r;
    }
    CHECK(energy_l > 1e-6f);
    CHECK(energy_r == 0.f);
}

TEST_CASE("flux tape: FXT time is x1/4 x1 x4 through the shared slew") {
    Flux f;
    f.init(48000.f, s_flux_l.data(), s_flux_r.data());
    f.set_rate(3); f.set_bpm(120.f);          // base 0.5 s
    f.set_time_mod(0.5f);
    CHECK(f.delay_target_for_test() == doctest::Approx(0.5f));
    f.set_time_mod(0.f);
    CHECK(f.delay_target_for_test() == doctest::Approx(0.125f));
    f.set_time_mod(1.f);
    CHECK(f.delay_target_for_test() == doctest::Approx(2.0f));
    CHECK(f.delay_current_for_test() != doctest::Approx(2.0f));
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 48000; ++i) f.process(l, r);
    CHECK(f.delay_current_for_test() == doctest::Approx(2.0f).epsilon(0.01));
}

TEST_CASE("flux tape: one missing channel leaves the block disengaged") {
    Flux a, b;
    a.init(48000.f, s_flux_l.data(), nullptr);
    b.init(48000.f, nullptr, s_flux_r.data());
    a.set_on(true, true); b.set_on(true, true);
    CHECK(!a.engaged()); CHECK(!b.engaged());
}
```

Keep the exact-off dry test, feedback-repeat test, LINK/THIN tests, and re-init guard tests, adapting every `init` call to two buffers.

- [ ] **Step 3: Give `Flux` its final tape-sized stereo core**

In `flux.h`:

```cpp
inline constexpr size_t kTapeSamples = 262144;

class Flux {
public:
    static constexpr size_t kMaxSamples = kTapeSamples;
    void init(float sample_rate, float* left, float* right);
    // ...existing public musical surface...
    float delay_target_for_test() const { return _dt_target; }
    float delay_current_for_test() const { return _dt_current; }
private:
    TapeEcho<kTapeSamples> _echo_l, _echo_r;
    // no BbdEcho, stage slew, clock or drive state
};
```

`init` validates both pointers, initializes both echoes, calls `tape_time_mult(0.5f)` during init so the LUT's static table is constructed off the audio thread, and keeps the 30 ms coefficient:

```cpp
_buf_ok = left != nullptr && right != nullptr;
if (!_buf_ok) return;
_echo_l.Init(sample_rate, left);
_echo_r.Init(sample_rate, right);
_time_mod_norm = 0.5f;
_time_mult = tape_time_mult(_time_mod_norm);
```

`set_feedback` pushes the same `clampf(norm,0,1) * 1.2f` coefficient to both `TapeEcho` instances. There is no BBD DRIVE compensation.

- [ ] **Step 4: Implement the signed-off time target without per-sample libm**

Use a single helper:

```cpp
void Flux::update_time_target(bool immediate) {
    const float max_s = static_cast<float>(kTapeSamples - 2) / _sr;
    _dt_target = clampf(_delay_time * _time_mult, 1.f / _sr, max_s);
    if (immediate) _dt_current = _dt_target;
    refresh_repeat_scheduler();
}
```

`recompute_time` computes the unmodulated RATE/BPM ladder time, then calls `update_time_target(immediate)`. `set_time_mod` clamps, guards, performs `tape_time_mult`, and calls `update_time_target(false)`. While DRAG still exists in this one transitional task, its active branch owns `_dt_target`; the signed-off FXT behavior is asserted with LINK neutral. Task 5 removes that transient competition.

In `process`:

```cpp
daisysp::fonepole(_dt_current, _dt_target, _dt_coef);
const float samples = _dt_current * _sr;
float wet_l = _echo_l.Process(l * send, samples);
float wet_r = _echo_r.Process(r * send, samples);
// existing THIN gate, including its return-to-unity snap, applies to both
wet_l *= _gate;
wet_r *= _gate;
l += wet_l * _mix_lin;
r += wet_r * _mix_lin;
```

The `_sw.is_idle()` return remains before the slew and echo calls, preserving the fully-off exact dry path.

- [ ] **Step 5: Remove the BBD-only public surface and its tests**

Delete `Flux::{set_drive,set_stages,stages,clock_hz,drive_norm_for_test,feedback_coef_for_test}` and all corresponding members. Delete `PartFx` and `Instrument` forwarding methods. Update VCV's control push so:

```cpp
// DRIVE_A/B remains an append-only saved parameter but has no FLUX target.
// STAGES_A/B is live only on a BBD deck, where movement 2 maps it to PITCH.
if (bbdPart)
    inst.set_target_base(p, spky::LANE_PITCH,
                         params[p ? STAGES_B : STAGES_A].getValue());
```

Remove or rewrite the four tests that pinned the old forwarding. Do not remove or reorder the VCV ids.

Remove the `set_drive` and `set_stages` apply-event arms from
`host/render/scenario.cpp` and their parser tests, because their only destination
was the removed `Instrument` forwarding methods. Do not retain scenario actions
as no-ops. The obsolete `bbd_bloom.json` is replaced in Task 8.

- [ ] **Step 6: Give every obsolete benchmark and audition call site an explicit disposition**

Do this in the same commit as the public-surface removal so that Task 4 builds
on its own; do not leave dead `set_drive`/`set_stages` calls for Task 7 to repair.

In `bench/workloads_sweep.cpp`:

- Keep the five `sweep_flux_rate_*` rows. They now sweep the tape RATE ladder
  and are the direct section 6.8 flat-cost experiment. `SweepFxGroup` no longer
  stores or folds `stages_achieved`/`clock_achieved`; instead store
  `delay_target_s`, read it from `Flux::delay_target_for_test()` after setup,
  assert that it is positive, and fold it once per block so a row stuck on the
  boot rate cannot return an apparently valid duplicate. Add a desktop test
  over the same five indices that proves their delay targets strictly decrease.
- Delete the four `sweep_stages_*` setup functions, C++ registrations, protocol
  entries in `bench/run.py`, and their BBD-only comments. STAGES no longer
  configures FLUX, so retaining these rows under their old names would be a
  false experiment.
- Keep `sweep_grit_no_bbd_mem` as the null-tape-memory PartFx-shell ablation,
  but remove its BBD clock/stage readbacks and rewrite its explanation for the
  tape `_buf_ok` guard. It no longer claims to isolate the historical per-sample
  `bbd_drive_gain` `powf`, because that code is gone.
- Keep the protocol name `sweep_flux_lines_2ch` for comparison with existing
  captures, but document it as a historical raw-BBD component-row name. Remove
  the throwaway `Flux` probe and initialize its two `BbdEcho` lines directly
  from `fx_mem().bbd[0][0]`/`[0][1]` at the previously measured 8192-stage,
  8192-Hz operating point. This remains the same-build "second BBD line" row;
  it must never borrow the new tape arena or derive BBD configuration from tape
  FLUX observers that no longer exist.

In `bench/workloads_instr.cpp`:

- Rename the local `configure_worst_bbd(Part&)` helper and its comments to a
  tape-FLUX operating point. Remove DRIVE/STAGES, keep RATE at the shortest
  division and `FXT_FLUX_FB = 0.9`, and use this same setup for the matched Part
  and `instr_noverb` rows.
- Reduce `FxFluxHotGroup` to tape-relevant state. `setup_fx_flux_hot` keeps the
  shortest RATE, feedback 0.9, stereo input and settle; delete BBD stage/clock/
  drive/coefficient observers and their checksum fields. Store/assert/fold the
  actual tape delay target instead. This row remains the hot tape-FLUX context
  row rather than silently retaining a BBD meaning.

In `bench/workloads_system.cpp`, make the legacy DTCM-pair setup compile in this
commit by removing only its FLUX DRIVE/STAGES calls and stale BBD-FLUX prose;
keep RATE and feedback hot temporarily. Task 7 then re-points both rows together
to the BBD engine and freeze configuration, before any benchmark is reported.

In `bench/audition/init_patch.cpp`, resolve/set the engine before applying the
engine-specific STAGES meaning. Delete the DRIVE forwarding, and for
`ENGINE_BBD` map `STAGES_A/B` to `LANE_PITCH` with `set_target_base`, matching
VCV. Non-BBD decks ignore the saved STAGES value. Add/update
`tests/test_seed_audition_init.cpp` so the generated init snapshot proves this
routing instead of merely proving that the audition host compiles.

Run a zero-call-site guard before proceeding:

```bash
rg -n "\.set_drive\(|\.set_stages\(|stages_achieved|clock_achieved|drive_norm_for_test|feedback_coef_for_test" \
  engine tests host bench --glob '!build/**' --glob '!bench/build/**'
```

Expected: no FLUX/PartFx/Instrument call or observer remains. Limiter
`set_drive` calls may remain and must be distinguished by receiver rather than
deleted.

Run `python -m unittest bench.test_run_contract` after changing the protocol
row set; the test must pin the five RATE rows, reject every retired STAGES row,
and retain the historical raw-BBD component row.

- [ ] **Step 7: Change the memory contract and positional callers**

`FxMem` becomes exactly:

```cpp
struct FxMem {
    float* echo[PART_COUNT][2] = {};
    float* bbd[PART_COUNT][2] = {};
    AmbientReverb* reverb = nullptr;
    SampleBuffer::Frame* sampler_buf[PART_COUNT] = {};
    size_t sampler_frames = 0;
};
```

`Part::init` becomes:

```cpp
void init(float sample_rate, uint32_t seed_base,
          float* echo_l = nullptr, float* echo_r = nullptr,
          SampleBuffer::Frame* sampler_mem = nullptr,
          size_t sampler_frames = 0,
          float* bbd_l = nullptr, float* bbd_r = nullptr);
```

and calls `_fx.init(sample_rate, echo_l, echo_r)`. `Instrument::init` passes `mem.echo[p][0]` and `[1]`. Update every positional caller found by:

```bash
rg -n "\.init\([^;]*echo|\.echo\[" engine tests host bench --glob '!build/**' --glob '!bench/build/**'
```

No two-argument `Part::init(sr, seed)` call changes.

- [ ] **Step 8: Allocate two tape channels in every host**

Static/SDRAM hosts use:

```cpp
float DSY_SDRAM_BSS g_echo[PART_COUNT][2][Flux::kMaxSamples];
// ...
mem.echo[p][0] = g_echo[p][0];
mem.echo[p][1] = g_echo[p][1];
```

Apply that shape to `bench/mem.cpp`, `bench/audition/memory.cpp`, and `host/render/main.cpp` (without `DSY_SDRAM_BSS` on the desktop). Keep the audition `kSdramBytes < 64 MiB` assertion and update its arithmetic/comment.

VCV must use heap storage:

```cpp
std::vector<float> echoMem[spky::PART_COUNT][2];
```

Remove the by-value `float echo[...]` member. In `Spotymod::reinit`, before `inst.init`:

```cpp
for (int p = 0; p < spky::PART_COUNT; ++p) {
    for (int ch = 0; ch < 2; ++ch) {
        if (echoMem[p][ch].size() != spky::Flux::kMaxSamples)
            echoMem[p][ch].resize(spky::Flux::kMaxSamples);
        fxmem.echo[p][ch] = echoMem[p][ch].data();
    }
}
```

Add a panel/source contract:

```python
check('std::vector<float> echoMem[spky::PART_COUNT][2];' in cpp,
      "VCV tape memory is not heap-backed stereo storage")
check('float echo[spky::PART_COUNT][spky::Flux::kMaxSamples]' not in cpp,
      "VCV still embeds the tape arena by value in every Module")
```

- [ ] **Step 9: Move expanded test fixtures to the heap**

Replace file-scope `float [...][Flux::kMaxSamples]` tape fixtures in `test_instrument.cpp`, `test_part_fx.cpp`, `test_flux.cpp`, `test_part.cpp`, and `test_bbd_engine.cpp` with `std::vector<float>` owned by each fixture/test. BBD arrays remain static at `BbdEngine::kCells` from Task 1.

Use the same two-channel helper shape in each file:

```cpp
struct TapeMem {
    std::vector<float> l{Flux::kMaxSamples};
    std::vector<float> r{Flux::kMaxSamples};
    void bind(FxMem& m, int p) {
        m.echo[p][0] = l.data();
        m.echo[p][1] = r.data();
    }
};
```

Do not put a 1 MiB array on the stack.

- [ ] **Step 10: Build, test, and check storage growth**

```bash
source env.sh
cmake --build build
ctest --test-dir build --output-on-failure
"/c/Program Files/LLVM/bin/llvm-size.exe" build/spky_tests.exe | tee build/spky-tests-size-after.txt
```

Expected: full suite green; `data + bss` grows by less than 1 MiB versus Step 1. If it grows by multiple MiB, find remaining static tape fixtures before proceeding.

- [ ] **Step 11: Verify desktop, VCV, panel, and audition builds**

```bash
./build/render host/render/scenarios/ambient_wash.json build/tape-smoke.wav build/tape-smoke.csv
cd host/vcv && ./build-local.sh
cd res && python test_panel.py
cd ../../../bench/audition && make
```

Expected: all build; panel prints `OK`; the WAV is non-empty; audition's SDRAM assertion passes.

- [ ] **Step 12: Commit**

```bash
git add engine/fx/flux.h engine/fx/flux.cpp engine/fx/part_fx.h engine/fx/part_fx.cpp \
        engine/parts/part.h engine/parts/part.cpp engine/instrument.h engine/instrument.cpp \
        host/render/main.cpp host/render/scenario.cpp tests/test_scenario.cpp \
        host/vcv/src/Spotymod.cpp host/vcv/res/test_panel.py \
        bench/mem.cpp bench/audition/memory.cpp \
        bench tests
git commit -F - <<'EOF'
feat(flux): restore the injected-memory stereo tape echo

FLUX is again two interpolating tape lines behind its existing SoftSwitch,
wet level, feedback, BPM/rate ladder and shared 30 ms time slew. The five-slot
modulation contract stays whole: FXT_FLUX_TIME maps x1/4..x4 and intentionally
produces tape/Doppler motion through that slew.

FxMem now names the real two-family layout: 262144-sample stereo tape buffers
and 8192-cell stereo BBD-engine buffers. Rack allocates the 4 MiB tape arena on
the heap; Daisy hosts keep it in SDRAM; expanded test fixtures are heap-backed.

The BBD-shaped FLUX surface -- DRIVE, STAGES, clock/stage observers and feedback
compensation -- is removed. Saved parameter ids remain stable, and STAGES still
feeds PITCH on a BBD deck.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 5: Make LINK full-travel THIN and remove DRAG

Task 2 proved THIN owns a working repeat scheduler before this deletion. Now the engine contract becomes unipolar: 0 is off, 1 is maximum thinning. THIN stays locked to the unmodulated RATE/BPM repeat grid; `FXT_FLUX_TIME` may Doppler the tape head, but it does not continuously restart or resize the rhythm scheduler.

**Files:**
- Modify: `engine/fx/flux.h`, `engine/fx/flux.cpp`
- Modify: `engine/fx/drag.h`
- Delete: `engine/fx/drag.cpp`, `tests/test_drag.cpp`
- Modify source lists: `CMakeLists.txt:90-93,171-173`, `bench/Makefile:125-133`, `bench/audition/Makefile:32-40`, `host/vcv/Makefile:43-51`
- Modify: `engine/instrument.h:167-170`, `engine/instrument.cpp:110-115`
- Test: `tests/test_flux.cpp:330-809`, `tests/test_instrument.cpp:1254` and related DRAG cases

**Interfaces:**
- Keeps: `Flux::set_link(float thin)`, `Flux::set_rhythm(const RhythmView&)`, `gate_for_test`, `thin_n_for_test`.
- Removes: `apply_drag`, `drag_time_s`, all `_drag*` state, `derive_intervals`, `drag_tuning`, and `Instrument::drag_time_for_test`.
- Keeps file name `drag.h` for now, containing only `link_tuning`; renaming it is cosmetic and deliberately out of scope.

- [ ] **Step 1: Convert the THIN tests to the final positive domain**

Every THIN setup changes from `f.set_link(-depth)` to `f.set_link(depth)`. Replace the old centre-neutral test with endpoints:

```cpp
TEST_CASE("flux tape: LINK is unipolar THIN over the full travel") {
    std::vector<float> a_l(Flux::kMaxSamples), a_r(Flux::kMaxSamples);
    std::vector<float> b_l(Flux::kMaxSamples), b_r(Flux::kMaxSamples);
    std::vector<float> c_l(Flux::kMaxSamples), c_r(Flux::kMaxSamples);
    Flux off, half, full;
    off.init(48000.f, a_l.data(), a_r.data());
    half.init(48000.f, b_l.data(), b_r.data());
    full.init(48000.f, c_l.data(), c_r.data());
    RhythmView rv;
    rv.gap[0] = 12000;
    rv.gap[1] = 6000;
    rv.valid = true;
    for (Flux* f : {&off, &half, &full}) {
        f->set_on(true, true);
        f->set_bpm(120.f);
        f->set_rate(10);
        f->set_mix(1.f);
        f->set_feedback(0.f);
        f->set_rhythm(rv);
    }
    off.set_link(0.f); half.set_link(0.5f); full.set_link(1.f);
    auto run = [](Flux& f, int samples) {
        for (int i = 0; i < samples; ++i) {
            float l = 0.f, r = 0.f;
            f.process(l, r);
        }
    };
    run(off, 7500); run(half, 7500); run(full, 7500);
    CHECK(off.gate_for_test() == doctest::Approx(1.f));
    CHECK(half.gate_for_test() == doctest::Approx(0.5f).epsilon(0.02));
    CHECK(full.gate_for_test() == doctest::Approx(0.f).epsilon(0.02));
}
```

- [ ] **Step 2: Delete DRAG-only tests and prove the remaining suite is red**

Remove tests whose names contain `DRAG`, clock bending by LINK, geometric interval interpolation, and straight THIN-to-DRAG crossing. Keep every THIN pattern, invalid-rhythm, RATE/BPM re-derive, and re-init guard test.

Run now. Expected: positive LINK tests fail because the implementation still interprets positive values as DRAG.

- [ ] **Step 3: Reduce `set_link` to THIN**

```cpp
void Flux::set_link(float norm) {
    if (!_buf_ok) return;
    const float n = clampf(norm, 0.f, 1.f);
    if (n == _link) return;
    _link = n;
    _thin = n;
    const bool thinning = (_thin > 0.f && _rhy_valid);
    if (!thinning) {
        _gate_target = 1.f;
        _thin_count = 0;
        _thin_i = 0;
    }
    refresh_repeat_scheduler();
}
```

`init` resets `_link = _thin = 0.f`, the gate to unity, and repeat scheduler state to zero. A repeated non-zero host push after re-init still lands because `_link` matches the reset state, not the saved knob.

- [ ] **Step 4: Make rhythm publication THIN-only**

`set_rhythm` stores `rv.gap[0]`, `rv.gap[1]`, and `rv.valid`, calls `update_thin_pattern()`, then `refresh_repeat_scheduler()`. Delete interval derivation and every DRAG-active guard.

The final scheduler is:

```cpp
void Flux::refresh_repeat_scheduler() {
    if (_thin > 0.f && _rhy_valid) {
        // Stable musical grid: FXT time modulation moves the tape head but does
        // not reset this counter every sample.
        _repeat_period_samples = _delay_time * _sr;
    } else {
        _repeat_period_samples = 0.f;
        _repeat_phase_samples = 0.f;
    }
}
```

`process` advances only when thinning; on each period it calls `advance_gate()`.

- [ ] **Step 5: Remove every DRAG symbol**

Delete from `Flux`: `_drag`, `_drag_iv`, `_drag_i`, `_drag_active`, `apply_drag`, and `drag_time_s`. Delete `Instrument::drag_time_for_test`. Remove the per-control-tick `set_rhythm` comments that describe DRAG but keep the actual sibling-rhythm push because THIN still consumes it.

Reduce `engine/fx/drag.h` to the existing `link_tuning` constants and comments. Delete `drag.cpp` and `test_drag.cpp`, then remove them from all five hand-synced source lists.

Verify:

```bash
rg -n "DRAG|apply_drag|derive_intervals|drag_time|_drag" engine tests host bench \
  --glob '!build/**' --glob '!bench/build/**'
```

Expected: no live-code/test matches. Historical docs may still contain the word.

- [ ] **Step 6: Build every source list and run tests**

```bash
source env.sh
cmake --build build
ctest --test-dir build --output-on-failure
make -C bench clean all
make -C bench/audition clean all
cd host/vcv && ./build-local.sh
```

Expected: all build; `spky_tests` has no `test_drag.cpp`; audition no longer has the previously fragile DRAG-only link dependency.

- [ ] **Step 7: Commit**

```bash
git add -A engine/fx/drag.cpp tests/test_drag.cpp engine/fx/drag.h \
        engine/fx/flux.h engine/fx/flux.cpp engine/instrument.h engine/instrument.cpp \
        tests/test_flux.cpp tests/test_instrument.cpp CMakeLists.txt bench/Makefile \
        bench/audition/Makefile host/vcv/Makefile
git commit -F - <<'EOF'
feat(link): THIN takes the whole knob and DRAG leaves

LINK is now 0..1 THIN. Its repeat scheduler was extracted and proven green in
the preceding commit, so deleting DRAG cannot silently delete THIN's clock.
The scheduler stays on the stable RATE/BPM grid; audio-rate tape-head motion
does not restart the rhythm counter every sample.

derive_intervals, drag.cpp, the uniformity guard and every DRAG state/test are
gone. drag.h remains only as the home of link_tuning; renaming it would be
cosmetic include churn.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 6: Migrate old LINK patches and update the VCV surface

Rack restores ordinary params; `dataFromJson` must then remap the already-restored LINK values before the module resumes. This migration is independent of FORM/SONG and must run even when there is no `sampler` object—the current early return at `Spotymod.cpp:824-825` must not bypass it.

**Files:**
- Create: `host/vcv/src/link_migration.hpp`
- Create: `tests/test_vcv_link_migration.cpp`
- Modify: `CMakeLists.txt` (register the test)
- Modify: `host/vcv/src/Spotymod.cpp:80-88,266-267,440,763-824`
- Modify: `host/vcv/src/init_patch.hpp:10-13,98-99`
- Modify: `host/vcv/res/test_panel.py:189-228,1329-1348`
- Modify: `host/vcv/README.md:320-395`

**Interfaces:**
- Produces:
  ```cpp
  bool is_modern_link_version(bool marker_is_integer, int64_t value);
  float migrate_legacy_link(float old_bipolar_value);
  ```
- JSON: `"linkVersion": 1`, independent of `"formSongVersion": 1`.

- [ ] **Step 1: Write the pure migration tests**

Create `tests/test_vcv_link_migration.cpp`:

```cpp
#include "doctest.h"
#include "vcv/src/form_song_migration.hpp"
#include "vcv/src/link_migration.hpp"

using namespace spkyvcv;

TEST_CASE("VCV LINK migration preserves old THIN and drops old DRAG") {
    CHECK(migrate_legacy_link(-1.f) == doctest::Approx(1.f));
    CHECK(migrate_legacy_link(-0.37f) == doctest::Approx(0.37f));
    CHECK(migrate_legacy_link(0.f) == 0.f);
    CHECK(migrate_legacy_link(0.42f) == 0.f);
    CHECK(migrate_legacy_link(2.f) == 0.f);
}

TEST_CASE("VCV LINK has an independent schema marker") {
    CHECK(is_modern_link_version(true, 1));
    CHECK(is_modern_link_version(true, 2));
    CHECK_FALSE(is_modern_link_version(false, 1));
    CHECK_FALSE(is_modern_link_version(true, 0));
}

TEST_CASE("a patch with no version keys requests both independent migrations") {
    CHECK_FALSE(is_modern_form_song_version(false, 0));
    CHECK_FALSE(is_modern_link_version(false, 0));
    const auto form_song = migrate_legacy_form_song(false, false, 0, false, 0);
    CHECK(form_song.form == 2);

    CHECK(form_song.song == 0);
    CHECK(migrate_legacy_link(-0.6f) == doctest::Approx(0.6f));
}
```

Run and expect a missing-header failure.

- [ ] **Step 2: Implement the helper**

```cpp
#pragma once
#include <cstdint>

namespace spkyvcv {
inline bool is_modern_link_version(bool marker_is_integer,
                                   std::int64_t marker_value) {
    return marker_is_integer && marker_value >= 1;
}

inline float migrate_legacy_link(float v) {
    if (!(v < 0.f)) return 0.f;  // positive, zero and NaN become neutral
    const float thin = -v;
    return thin > 1.f ? 1.f : thin;
}
} // namespace spkyvcv
```

- [ ] **Step 3: Make the runtime control unipolar**

`LinkQuantity` displays only THIN:

```cpp
std::string getDisplayValueString() override {
    const float v = getValue();
    return v > 0.005f ? string::f("thin %.0f %%", 100.f * v) : "off";
}
```

and configuration becomes:

```cpp
configParam<LinkQuantity>(c.id, 0.f, 1.f, init, lbl);
```

The existing `inst.set_link` push remains, now forwarding 0..1.

- [ ] **Step 4: Persist and apply `linkVersion` before the sampler early return**

`dataToJson` writes both markers:

```cpp
json_object_set_new(root, "formSongVersion", json_integer(1));
json_object_set_new(root, "linkVersion", json_integer(1));
```

In `dataFromJson`, after the FORM/SONG block and before reading `sampler`:

```cpp
json_t* link_version = json_object_get(root, "linkVersion");
const bool modern_link = is_modern_link_version(
    link_version && json_is_integer(link_version),
    link_version && json_is_integer(link_version)
        ? json_integer_value(link_version) : 0);
if (!modern_link) {
    for (int p = 0; p < spky::PART_COUNT; ++p) {
        const int id = p ? LINK_B : LINK_A;
        params[id].setValue(migrate_legacy_link(params[id].getValue()));
    }
}
```

FORM/SONG may run before or after this block; keep them adjacent and comment that they touch disjoint parameter ids. Crucially, the LINK block precedes `if (!parts) return;`.

- [ ] **Step 5: Pin the migration and panel contract**

Extend `test_panel.py` to require:

```python
check('json_object_set_new(root, "linkVersion", json_integer(1));' in cpp,
      "new patches do not carry the LINK schema marker")
check('json_object_get(root, "linkVersion")' in cpp,
      "LINK migration does not check its independent marker")
check('migrate_legacy_link(params[id].getValue())' in cpp,
      "legacy bipolar LINK is not remapped after Rack restores params")
check(cpp.index('migrate_legacy_link(params[id].getValue())') <
      cpp.index('if (!parts) return;'),
      "a patch without sampler data bypasses LINK migration")
```

Update LINK range/display pins from `-1..1` and drag/thin to `0..1` and thin/off. Update `init_patch.hpp`'s note: both LINK defaults remain 0, now the neutral end rather than a bipolar centre.

- [ ] **Step 6: Document the user-visible migration**

In `host/vcv/README.md`, replace the BBD-era FLUX/DRAG text with:

- FLUX is a stereo tape echo.
- LINK is THIN across its full travel.
- Old negative THIN values retain their depth; old positive DRAG values load at off.
- FLUX TIME produces tape/Doppler motion from x1/4 to x4 through the 30 ms slew.
- DRIVE/STAGES no longer voice FLUX; STAGES remains PITCH base only on BBD decks.

- [ ] **Step 7: Run all migration and host verification**

```bash
source env.sh
cmake --build build
./build/spky_tests --test-case="*VCV LINK*"
./build/spky_tests --test-case="*no version keys*"
ctest --test-dir build --output-on-failure
cd host/vcv/res && python test_panel.py
cd .. && ./build-local.sh
```

Expected: all tests pass, panel prints `OK`, VCV builds.

- [ ] **Step 8: Prove the no-version case can fail**

Temporarily move the LINK migration below `if (!parts) return;`, run the no-version helper/source contract and panel test, and confirm RED. Restore and confirm green. Record both outputs in the task report.

- [ ] **Step 9: Load real legacy patches in Rack**

Using the movement-2 plugin/parent commit, save two minimal Rack patches with
`LINK_A = -0.63`: one normal patch carrying `formSongVersion: 1` but no
`linkVersion`, and one copy with both version keys removed from the module's
custom data. Open both with the movement-3 plugin.

Expected for each: LINK A displays `thin 63 %`, FORM/SONG retains the expected
value, and the module produces the same THIN pattern. Also load an old patch at
`LINK_A = +0.63`; it must display `off`. Record the three patch filenames and
observed values in the task report. This is the actual-load evidence; the pure
helper tests alone are not allowed to satisfy section 6.10.

- [ ] **Step 10: Commit**

```bash
git add host/vcv/src/link_migration.hpp host/vcv/src/Spotymod.cpp \
        host/vcv/src/init_patch.hpp host/vcv/res/test_panel.py host/vcv/README.md \
        tests/test_vcv_link_migration.cpp CMakeLists.txt
git commit -F - <<'EOF'
feat(vcv): preserve legacy THIN under an independent LINK schema

LINK is now unipolar THIN. A separate linkVersion marker keeps FORM/SONG's
version honest: missing or malformed means legacy bipolar state, negative
values map to their magnitude, and positive DRAG values map to off.

The migration runs before the sampler-data early return, so a patch with no
version keys receives both independent migrations and keeps its THIN setting.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 7: Re-point the DTCM A/B pair, verify ITCM, and measure the new rows

`instrument_worst_bbd` and `_dtcm` remain the benchmark harness's only checksum-equal AXI/DTCM pair and `_dtcm` remains the gate row. They now measure the BBD **part engine**, not FLUX. `fx_flux_sdram` prices tape FLUX in the system family; the five retained `sweep_flux_rate_*` rows prove that its cost is flat across delay time. All figures are same-build `pct_max`.

**Files:**
- Modify: `bench/workloads_system.cpp:417-558,587-608`
- Modify: `bench/run.py:184-194,305-307,499-529,663-726`
- Modify: `bench/test_run_contract.py` where row descriptions/configuration are pinned
- Modify comments only if needed: `bench/itcm_hot.lds:16-31`, `bench/itcm_placement.py:13-27`
- Create after hardware run: `docs/bench/2026-08-01-<sha>-flux-tape.md`

**Interfaces:**
- Keeps rows: `instrument_worst`, `fx_flux_sdram`, `instrument_worst_bbd`, `instrument_worst_bbd_dtcm`, `inst_bbd_engine_worst`.
- Produces one shared `configure_inst_bbd_engine_worst(Instrument&)` and one specialized process function that keeps STEP freeze engaged.

- [ ] **Step 1: Write/update the benchmark contract tests first**

In `bench/test_run_contract.py`, assert that:

- both legacy `_bbd` row names remain registered and anchored;
- the AXI/DTCM comparison still requires checksum equality;
- help/report prose calls them BBD **engine** rows, not FLUX mono-BBD rows;
- `fx_flux_sdram` is described as stereo tape FLUX;
- all five `sweep_flux_rate_*` rows remain registered as the flat-cost curve
  and no `sweep_stages_*` row can return;
- `_dtcm` remains `gate_name`.

Run:

```bash
python -m unittest bench.test_run_contract
```

Expected: RED on stale FLUX/BBD descriptions before implementation.

- [ ] **Step 2: Share the real worst-case engine configuration**

Move `configure_inst_bbd_engine_worst` above all three setup functions, then have:

```cpp
void setup_inst_worst_bbd() {
    auto& g = construct_axi_instrument_group();
    configure_inst_common(g.instrument);
    configure_inst_worst(g.instrument);
    configure_inst_bbd_engine_worst(g.instrument);
}

void setup_inst_worst_bbd_dtcm() {
    auto& inst = construct_dtcm_instrument();
    configure_inst_common(inst);
    configure_inst_worst(inst);
    configure_inst_bbd_engine_worst(inst);
}

void setup_inst_bbd_engine_worst() { setup_inst_worst_bbd(); }
```

There is no FLUX STAGES/DRIVE setup left.

- [ ] **Step 3: Make the row satisfy section 8.3's actual worst case**

For both decks: `ENGINE_BBD`, shortest division, PITCH at the clock ceiling, COLOR maximum, FEEDBACK/MIX maximum, input sources live, and STEP freeze engaged. Use a specialized row process:

The configuration explicitly adds:

```cpp
inst.set_step(p, true, 16);          // freeze is unreachable in FLOW
inst.set_color(p, 1.f);              // maximum stereo clock spread
inst.set_voice_attack(p, 0.f);       // 2 ms freeze ramp
inst.set_voice_decay(p, 1.f);        // no trim below the measured freeze gain
```

```cpp
float proc_inst_bbd_frozen() {
    auto& inst = *g_active_instrument;
    inst.trigger_manual(PART_A);
    inst.trigger_manual(PART_B);  // retrigger every 96-sample block; gate stays high
    return proc_inst();
}
```

Set STEP before the 200-block settle and trigger both parts on every settle block. After settling, assert for both decks:

```cpp
assert(inst.engine_id(p) == ENGINE_BBD);
assert(inst.bbd_div(p) == 0);
assert(inst.bbd_clock_hz(p) >= bbd_tuning::kClockMaxHz);
assert(inst.bbd_frozen(p));
```

Register all three engine-worst row names with `proc_inst_bbd_frozen`. This fixes movement 2's current `assert(!frozen)` mismatch with section 8.3 rather than carrying it forward.

- [ ] **Step 4: Preserve and verify the ITCM hotset**

Do not delete either symbol requirement:

```python
"spky::Flux::process(",
"spky::BbdLine::Process(",
"spky::BbdEngine::process(",
```

FLUX now resolves from tape `flux.o`; `BbdLine::Process` resolves through `bbd_engine.o`/`bbd.o`. Update comments that still say the BBD line is reached from FLUX. Run:

```bash
python -m unittest bench.test_itcm_link
python bench/itcm_placement.py --help
```

Then use the exact build/inspection command documented in `bench/README.md` for the ITCM image. Expected: the 64 KiB assertion holds and all three symbols resolve inside ITCM.

- [ ] **Step 5: Build the bench and prove every claimed row is in the image**

```bash
make -C bench clean all
grep -c "fx_flux_sdram" bench/build/bench.map
grep -c "instrument_worst" bench/build/bench.map
grep -c "instrument_worst_bbd" bench/build/bench.map
grep -c "instrument_worst_bbd_dtcm" bench/build/bench.map
grep -c "inst_bbd_engine_worst" bench/build/bench.map
grep -c "sweep_flux_rate_0" bench/build/bench.map
grep -c "sweep_flux_rate_11" bench/build/bench.map
```

Expected: every count is at least 1. Do not trust a memory table or an old serial capture.

- [ ] **Step 6: Run desktop contracts before hardware**

```bash
source env.sh
cmake --build build
ctest --test-dir build --output-on-failure
python -m unittest bench.test_run_contract bench.test_itcm_link
```

Expected: all green.

- [ ] **Step 7: Measure on the Daisy in one build/session**

Flash and run using `bench/README.md`. Capture `pct_avg` and `pct_max`, but report and compare **`pct_max`** for:

| row | purpose |
|---|---|
| `instrument_worst` | unchanged same-build control |
| `fx_flux_sdram` | stereo tape FLUX flat cost |
| `sweep_flux_rate_0/3/6/8/11` | same-context tape cost across the delay ladder |
| `instrument_worst_bbd` | AXI instrument, BBD engine worst/frozen |
| `instrument_worst_bbd_dtcm` | DTCM pair and gate |
| `inst_bbd_engine_worst` | explicit movement-2 engine row, same config |

The AXI/DTCM checksums must match. If any expected row is absent or the checksums differ, stop; do not explain a number from a different workload.

- [ ] **Step 8: Write the benchmark note**

Create `docs/bench/2026-08-01-<sha>-flux-tape.md` in the same structure as the latest movement-2 bench note. Include:

- exact commit and build flags;
- `bench.map` counts;
- the same-build system/gate rows above, with `pct_max` emphasized;
- the five RATE sweep points in one table, with their target delays and
  `pct_max`; call the curve flat only if the observed spread is within the
  bench's repeat noise, otherwise report the non-flat result and investigate;
- ITCM hot size and remaining bytes;
- AXI/DTCM checksum equality;
- an explicit statement that movement 3 is musical differentiation, not a proven CPU saving.

- [ ] **Step 9: Commit**

```bash
git add bench/workloads_system.cpp bench/run.py bench/test_run_contract.py \
        bench/itcm_hot.lds bench/itcm_placement.py docs/bench/
git commit -F - <<'EOF'
bench(flux): re-point the DTCM pair at the BBD engine

The harness's only AXI/DTCM checksum pair keeps its names and gate role, but
now measures the BBD part engine at the real worst case: shortest division,
clock ceiling, COLOR open and STEP freeze engaged. The explicit engine row
shares the same configuration.

fx_flux_sdram now prices the stereo tape echo. Every claimed row was verified
in bench.map, and instrument_worst was captured in the same build as the
pct_max control.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 8: Listening renders and the section 6.10 definition-of-done walk

No hash is an acceptance gate here. Produce one engaged tape render for musical inspection and compare the disengaged path by ear/sanity against Task 1's reference. Then walk every section 6.10 bullet with evidence.

**Files:**
- Delete: `host/render/scenarios/bbd_bloom.json`
- Create: `host/render/scenarios/flux_tape_echo.json`
- Modify: `host/render/scenario.cpp` and its tests if the retired `set_drive`/`set_stages` actions remain
- Modify: `host/vcv/README.md` if the final listen changes only descriptive wording
- Create: task report or append the definition-of-done table to `docs/bench/2026-08-01-<sha>-flux-tape.md`

**Interfaces:**
- Produces no new DSP API. This task verifies the delivered one.

- [ ] **Step 1: Remove the obsolete BBD-FLUX scenario surface**

Delete `bbd_bloom.json`. Remove render action arms/tests whose only destination was `Instrument::set_drive`/`set_stages`; do not keep them as no-ops. Saved Rack parameter ids are stable, but render scenario action names are not patch schema.

Create `host/render/scenarios/flux_tape_echo.json`:

```json
{
  "sample_rate": 48000,
  "bpm": 110,
  "duration_s": 30,
  "init": [
    {"_comment":"Stereo tape FLUX listening check: rate slew, feedback bloom, and FXT time Doppler."},
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
    {"action":"set_fx_target_base","part":0,"slot":4,"value":0.65},
    {"action":"set_fx_target_base","part":0,"slot":1,"value":0.5}
  ],
  "events": [
    {"t":6.0,"action":"set_flux_rate","part":0,"ivalue":6},
    {"t":10.0,"action":"set_flux_rate","part":0,"ivalue":0},
    {"t":14.0,"action":"set_fx_target_base","part":0,"slot":4,"value":0.9},
    {"t":18.0,"action":"set_fx_target_base","part":0,"slot":4,"value":0.55},
    {"t":20.0,"action":"set_fx_target_active","part":0,"slot":1,"flag":true},
    {"t":20.0,"action":"set_fx_target_depth","part":0,"slot":1,"value":0.35},
    {"t":26.0,"action":"set_fx_target_depth","part":0,"slot":1,"value":0.0}
  ]
}
```

THIN itself is covered deterministically in unit tests and the VCV knob; the render host has no sibling-rhythm LINK event, so do not invent one for this file.

- [ ] **Step 2: Render and inspect the engaged tape echo**

```bash
source env.sh
cmake --build build
./build/render host/render/scenarios/flux_tape_echo.json build/flux-tape.wav build/flux-tape.csv
```

Listen for: distinct L/R echoes, smooth rate-change pitch travel, bounded feedback bloom, and the x1/4..x4 lane gesture. Record observations, including anything undesirable; “sounds right” without named observations is not evidence.

- [ ] **Step 3: Render and compare the disengaged path by ear/sanity**

```bash
./build/render host/render/scenarios/wave_formant_sweep.json build/flux-off-after.wav build/flux-off-after.csv
```

Listen to `build/flux-off-before.wav` from Task 1 and `build/flux-off-after.wav`. They should present the same deck performance with FLUX disengaged. Do not make byte identity or a hash an acceptance condition; investigate any audible or gross waveform-level difference.

- [ ] **Step 4: Final automated verification**

```bash
source env.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build --output-on-failure
cd host/vcv/res && python test_panel.py
cd .. && ./build-local.sh
cd ../..
make -C bench/audition clean all
python -m unittest bench.test_run_contract bench.test_itcm_link
```


Expected: all green; panel `OK`; audition builds.

- [ ] **Step 5: Walk section 6.10 verbatim**

Put this table in the final task report, filling the Evidence column with exact test names, build outputs, render filenames, map counts, and measured figures:

| Section 6.10 bullet | Evidence required |
|---|---|
| Interpolating stereo tape echo over injected memory behind FLUX's public form | `test_tape_echo`, stereo/null/off-path FLUX tests; symbol disposition table below |
| `FXT_FLUX_TIME` decided and tested | x1/4/x1/x4 target test plus `flux-tape.wav` Doppler listen |
| FLUX disengaged leaves the deck sounding unchanged | before/after `wave_formant_sweep` ear/sanity comparison, not hash |
| THIN full LINK travel, green before DRAG deletion | Task 2 commit/test output and Task 5 endpoint test |
| Old patch keeps THIN, including no version key | `test_vcv_link_migration`, deliberate early-return failure proof, and the three real Rack patch loads from Task 6 Step 9 |
| Tape heap-allocated on VCV; `spky_tests` BSS sane | VCV vector source pin/build; before/after `llvm-size` delta < 1 MiB |
| `bench/audition/Makefile` builds | Task 8 build output |
| DTCM A/B pair exists, re-pointed | row registrations, `bench.map` counts, checksum equality |
| `instrument_worst` re-measured same build as `pct_max` | hardware table in bench note |

Also include this completed symbol-disposition table:

| Old FLUX symbol | Final disposition |
|---|---|
| `set_drive` | removed with forwarders/tests; VCV id retained only for patch-id stability |
| `set_stages` | removed with forwarders/tests; STAGES parameter only feeds BBD-engine PITCH |
| `set_time_mod` | kept: x1/4..x4 tape delay target through 30 ms slew |
| `set_link` | kept: unipolar THIN 0..1 |
| `set_rhythm` | kept: sibling gaps feed THIN |
| `stages()` / `clock_hz()` | removed with BBD-only tests |
| `drive_norm_for_test()` / `feedback_coef_for_test()` | removed with BBD-only tests |
| `drag_time_s()` | removed with DRAG |
| `gate_for_test()` / `thin_n_for_test()` | kept for THIN contracts |

- [ ] **Step 6: Final documentation commit**

```bash
git add -A host/render/scenarios/bbd_bloom.json host/render/scenarios/flux_tape_echo.json \
        host/render/scenario.cpp tests/test_scenario.cpp host/vcv/README.md docs/bench/
git commit -F - <<'EOF'
docs(flux): the tape listening target and movement-3 evidence

The obsolete BBD-FLUX render is replaced by a stereo tape scenario covering
rate slew, feedback bloom and the signed-off FXT time gesture. The disengaged
path was compared by ear and sanity render rather than promoted to a hash gate.

The movement-3 report walks section 6.10 bullet by bullet, including patch
migration without version keys, VCV heap storage, audition build, ITCM/DTCM
evidence and same-build pct_max figures.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Self-review notes for the executing controller

1. **THIN follows the stable RATE/BPM grid, not the continuously modulated tape time.** Recomputing or restarting its counter for every 2 ms-smoothed `FXT_FLUX_TIME` sample would turn a rhythm gate into jitter. The tape head can move inside that grid; this is intentional and should be judged in the engaged render.
2. **Task 4 is the only deliberately broad commit.** The stereo DSP and two-channel memory signature cannot be independently green without one temporarily ignoring a channel or constructing a 4 MiB by-value VCV module. The primitive and BBD-size work are already isolated in Tasks 1 and 3.
3. **The `drag.h` filename remains after DRAG.** Section 6.5 explicitly permits this; it contains only `link_tuning`. Renaming it adds four source/include edits with no behavioral value.
4. **Saved `DRIVE_A/B` and `STAGES_A/B` ids remain.** Removing ids would reorder or invalidate patches. A retained id is not a retained FLUX setter: DRIVE has no final destination; STAGES has one destination only on BBD decks.
5. **The DTCM pair's configuration changes materially.** The names stay because the harness and gate contract depend on them; comments, help text, assertions, checksum pair, and bench note must all say “BBD part engine,” and freeze must be engaged.
6. **No CPU saving is promised.** `fx_flux_sdram`, the two engine rows, and `instrument_worst` are measurements. Report surprising results without explaining them from component-row arithmetic; Appendix A shows why that inference is unsafe.

## Plan self-review checklist

- **Spec coverage:** Sections 6.1-6.10 are covered by Tasks 1-8; section 7's final controls are covered in Tasks 4 and 6; section 9's FXT decision is signed off; section 10 exclusions appear in Global Constraints.
- **Appendix A hazards:** no taps, no mono assumption, no `Flux::kMaxSamples` BBD sizing, no `form_song_migration.hpp` misuse, no deletion of the DTCM pair, no stale benchmark-row trust without `bench.map`, and no bit-exact acceptance gate.
- **Type consistency:** tape memory is `[PART_COUNT][2]` at every boundary; BBD memory remains `[PART_COUNT][2]`; `Part::init` orders `echo_l, echo_r, sampler, frames, bbd_l, bbd_r`; LINK runtime and migration both end at 0..1.
- **No placeholders:** the only runtime-created filename contains the measured commit SHA, as required by the repository's bench-note convention; every implementation/test interface and acceptance command is named above.

---
