# BBD Dynamic Stage Changes — Click-Free Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve RATE-modulated BBD COLOR while removing the short stereo-channel impulses caused by dynamic stage-count changes.

**Architecture:** `BbdLine` will write continuously through the full injected ring and interpret the requested stage count as a read distance behind that write head. Stage changes will crossfade two read taps over exactly sixteen BBD READ ticks before the existing single reconstruction-filter path; repeated control-rate requests will coalesce to the newest target without restarting an audible transition.

**Tech Stack:** C++17, doctest 2.4.11, CMake/Ninja desktop builds, the existing Daisy firmware benchmark build, and the render host's 16-bit stereo WAV output.

## Global Constraints

- Preserve BBD COLOR modulation by MOTION/RATE.
- Preserve `delay = stages / (2 * f_clk)` at settled endpoints and keep both stereo repeat intervals on the grid.
- Preserve the existing compander, feedback, dither, loss-pole, freeze and output reconstruction-filter paths.
- Add no line-sized allocation, no heap allocation and no second complete BBD/filter/compander path.
- Add no audio-rate transcendental calls; the crossfade uses arithmetic and a compile-time reciprocal only.
- Keep the fixed-stage path bit-identical to the pre-fix Release-build FNV-1a digest `0x12156b08`.
- Keep COLOR 0 bit-identical left to right for mono input.
- The modulated-COLOR regression must keep adjacent-sample deltas below `0.03` on both channels after settling.
- Do not change host parameters, panel layout, patch migration, COLOR's 30-cent endpoint, DETUNE, FILT, feedback or freeze tuning.
- Do not relax a failing identity, continuity, grid, render-hash, ITCM-placement or output-bound test to make the implementation pass.

---

## File map

| File | Responsibility in this fix |
|---|---|
| `tests/test_bbd.cpp` | Pin the fixed-stage raw-float digest and verify shrink, expansion, rapid retarget, Reset and null-memory transition state. |
| `tests/test_bbd_engine.cpp` | Reproduce the user-visible RATE-modulated COLOR impulse and gate adjacent-sample continuity for both channels. |
| `engine/fx/bbd.h` | Replace active-length ring wrapping with full-ring writes and a sixteen-READ smoothstep between read distances. |
| `docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md` | Correct the obsolete statement that `SetStages` merely changes `cells_` and cannot click; record the full-ring/tap-crossfade implementation. |
| `build/bbd_color_declick.json` | Temporary ignored listening scenario created during verification; not committed. |

No new production source file or public engine/host interface is needed.

---

### Task 1: Pin the pre-fix fixed-stage output

**Files:**
- Modify: `tests/test_bbd.cpp:1-6`
- Modify: `tests/test_bbd.cpp:146-176`

**Interfaces:**
- Consumes: `BbdLine::Init(float*, size_t, float)`, `SetStages(int)`, `SetDither(float)`, `Reset()`, `SetClock(float)`, and `Process(float)`.
- Produces: a deterministic Release-build raw-float FNV-1a contract with expected digest `0x12156b08`; Task 2 uses it to prove the settled full-ring path is chronologically identical to the old active ring.

- [ ] **Step 1: Add the raw-float folding helper and fixed-stage digest test**

Add `<cstdint>` and `<cstring>` beside the existing standard-library includes in `tests/test_bbd.cpp`:

```cpp
#include <cstdint>
#include <cstring>
```

Add this helper below `static float s_bbd_mem[8192];`:

```cpp
static uint32_t fold_bbd_sample(uint32_t hash, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (hash ^ bits) * 16777619u;
}
```

Add this test immediately before the existing arrival-time test:

```cpp
TEST_CASE("bbd line: fixed-stage output stays bit-identical") {
    BbdLine line;
    line.Init(s_bbd_mem, 8192, 48000.f);
    line.SetStages(8192);                  // 4096 cells
    line.SetDither(4e-5f);
    line.Reset();                          // settle that requested length before audio
    line.SetClock(16384.f);

    uint32_t hash = 2166136261u;
    for (int i = 0; i < 48000; ++i) {
        const float x = 0.25f * std::sin(
            TWO_PI * 137.f * static_cast<float>(i) / 48000.f)
            + (i < 16 ? 0.5f : 0.f);
        hash = fold_bbd_sample(hash, line.Process(x));
    }
    CHECK(hash == 0x12156b08u);
}
```

- [ ] **Step 2: Build and verify the baseline test passes before touching `BbdLine`**

Run:

```powershell
cmake --build build --target spky_tests
build\spky_tests.exe --test-case="bbd line: fixed-stage output stays bit-identical" --no-skip
```

Expected: one test case passes and reports zero failed assertions in the repository's Release build. If the digest is not `0x12156b08`, stop: confirm the build uses the current pre-production-change checkout plus only this test, and do not substitute a new digest.

- [ ] **Step 3: Commit the baseline contract**

```powershell
git add tests/test_bbd.cpp
git commit -m "test(bbd): pin fixed-stage line output"
```

---

### Task 2: Reproduce the click and implement the full-ring tap transition

**Files:**
- Modify: `tests/test_bbd_engine.cpp:1-18`
- Modify: `tests/test_bbd_engine.cpp:205-274`
- Modify: `tests/test_bbd.cpp:280-313`
- Modify: `engine/fx/bbd.h:273-443`

**Interfaces:**
- Consumes: Task 1's fixed-stage digest contract; existing `BbdEngine::set_width(float)` and `BbdLine` tick/filter behavior.
- Produces: production behavior in `BbdLine::SetStages(int)` and `Process(float)` plus test-only `BbdLine::settled_cells() const -> int` and `BbdLine::stage_transition_active() const -> bool` under `SPKY_TESTING`.

- [ ] **Step 1: Add the engine-level A/B/C continuity regression**

Add this test after `"bbd engine: COLOR opened splits the lines and keeps the grid"` in `tests/test_bbd_engine.cpp`:

```cpp
TEST_CASE("bbd engine: RATE-modulated COLOR does not emit channel clicks") {
    struct Metrics {
        float peak = 0.f;
        float max_delta_l = 0.f;
        float max_delta_r = 0.f;
        double stereo_delta = 0.0;
    };

    auto render = [](int width_mode) {
        BbdEngine e;
        e.init(48000.f);
        e.init_buffers(s_bbd_l, s_bbd_r, BbdEngine::kCells);
        e.reset();
        e.set_cycle(0.5f);
        e.set_flow(true);
        e.set_detune(0.f);
        float targets[LANE_COUNT] = { 0.15f, 0.5f, 0.5f, 0.35f, 1.f };
        e.set_targets(targets, 0.5f);

        Metrics m;
        float previous_l = 0.f;
        float previous_r = 0.f;
        bool have_previous = false;
        const int total = 48000 * 8;
        for (int i = 0; i < total; ++i) {
            if ((i % 96) == 0) {
                float width = 0.f;
                if (width_mode == 1) width = 0.2f;
                if (width_mode == 2) {
                    width = 0.2f + 0.2f * std::sin(
                        TWO_PI * 2.f * static_cast<float>(i) / 48000.f);
                }
                e.set_width(width);
            }

            const float in = 0.5f * std::sin(
                TWO_PI * 110.f * static_cast<float>(i) / 48000.f);
            float l = 0.f, r = 0.f;
            e.process_in(in, in);
            e.process(l, r);
            REQUIRE(std::isfinite(l));
            REQUIRE(std::isfinite(r));

            if (i >= 48000) {
                m.peak = std::max(m.peak, std::max(std::fabs(l), std::fabs(r)));
                m.stereo_delta += std::fabs(l - r);
                if (have_previous) {
                    m.max_delta_l = std::max(m.max_delta_l, std::fabs(l - previous_l));
                    m.max_delta_r = std::max(m.max_delta_r, std::fabs(r - previous_r));
                }
                have_previous = true;
            }
            previous_l = l;
            previous_r = r;
        }
        return m;
    };

    const Metrics zero = render(0);
    const Metrics fixed = render(1);
    const Metrics moving = render(2);
    CAPTURE(zero.max_delta_l, zero.max_delta_r);
    CAPTURE(fixed.max_delta_l, fixed.max_delta_r);
    CAPTURE(moving.max_delta_l, moving.max_delta_r);
    CAPTURE(moving.stereo_delta);

    CHECK(zero.peak > 0.05f);
    CHECK(fixed.peak > 0.05f);
    CHECK(moving.peak > 0.05f);
    CHECK(zero.stereo_delta == 0.0);
    CHECK(fixed.stereo_delta > 1.0);
    CHECK(moving.stereo_delta > 1.0);
    CHECK(zero.max_delta_l < 0.03f);
    CHECK(zero.max_delta_r < 0.03f);
    CHECK(fixed.max_delta_l < 0.03f);
    CHECK(fixed.max_delta_r < 0.03f);
    CHECK(moving.max_delta_l < 0.03f);
    CHECK(moving.max_delta_r < 0.03f);
}
```

- [ ] **Step 2: Run the symptom regression and verify RED**

Run:

```powershell
cmake --build build --target spky_tests
build\spky_tests.exe --test-case="bbd engine: RATE-modulated COLOR does not emit channel clicks" --no-skip
```

Expected: the test fails on at least one `moving.max_delta_* < 0.03f` assertion. The zero-width and fixed-width delta assertions pass, `zero.stereo_delta == 0`, and both fixed/moving stereo non-vacuity assertions pass. This is the required symptom-level RED gate.

- [ ] **Step 3: Replace the old bounded-only stage-change test with focused transition-state coverage**

Keep the existing finite/bounded sweep test. Add the following tests immediately after it in `tests/test_bbd.cpp`:

```cpp
TEST_CASE("bbd line: stage tap crossfade shrinks, expands, and coalesces targets") {
    static float mem[8192];
    BbdLine line;
    line.Init(mem, 8192, 48000.f);
    line.SetStages(8192);                   // requested/settled = 4096 cells
    line.Reset();
    line.SetClock(12000.f);                 // one READ tick every four samples

    auto run = [&](int samples) {
        for (int i = 0; i < samples; ++i)
            line.Process(0.25f * std::sin(0.011f * static_cast<float>(i)));
    };
    run(20000);                             // fill a live history
    CHECK(line.settled_cells() == 4096);
    CHECK_FALSE(line.stage_transition_active());

    line.SetStages(4096);                   // shrink to 2048 cells
    CHECK(line.cells() == 2048);            // public observer is the request
    CHECK(line.settled_cells() == 4096);
    run(8);
    CHECK(line.stage_transition_active());
    run(72);                                // >16 READ ticks total
    CHECK_FALSE(line.stage_transition_active());
    CHECK(line.settled_cells() == 2048);

    line.SetStages(12288);                  // expand to 6144 cells
    run(80);
    CHECK_FALSE(line.stage_transition_active());
    CHECK(line.settled_cells() == 6144);

    line.SetStages(4096);                   // begin 6144 -> 2048
    run(20);                                // five READ ticks: transition active
    REQUIRE(line.stage_transition_active());
    line.SetStages(6144);                   // newest request is 3072, do not restart
    CHECK(line.cells() == 3072);
    run(128);                               // finish old transition, then newest one
    CHECK_FALSE(line.stage_transition_active());
    CHECK(line.settled_cells() == 3072);
}

TEST_CASE("bbd line: Reset cancels a stage transition at the latest request") {
    static float mem[8192];
    BbdLine line;
    line.Init(mem, 8192, 48000.f);
    line.SetStages(8192);
    line.Reset();
    line.SetClock(12000.f);
    for (int i = 0; i < 1000; ++i) line.Process(0.3f);

    line.SetStages(2048);                   // request 1024 cells
    for (int i = 0; i < 20; ++i) line.Process(0.3f);
    REQUIRE(line.stage_transition_active());
    line.Reset();
    CHECK_FALSE(line.stage_transition_active());
    CHECK(line.cells() == 1024);
    CHECK(line.settled_cells() == 1024);
    for (int i = 0; i < 256; ++i) CHECK(line.Process(0.f) == 0.f);
}

TEST_CASE("bbd line: null memory can hold a pending stage request safely") {
    BbdLine line;
    line.Init(nullptr, 8192, 48000.f);
    line.SetStages(4096);                   // request 2048, no memory to advance it
    line.SetClock(12000.f);
    CHECK(line.cells() == 2048);
    CHECK(line.settled_cells() == 8192);
    for (int i = 0; i < 1000; ++i) CHECK(std::isfinite(line.Process(0.5f)));
    CHECK_FALSE(line.stage_transition_active());
    line.Reset();
    CHECK(line.settled_cells() == 2048);
}
```

- [ ] **Step 4: Build and verify the new line tests fail to compile for the right reason**

Run:

```powershell
cmake --build build --target spky_tests
```

Expected: compilation fails because `BbdLine` has no members named `settled_cells` and `stage_transition_active`. If it fails for syntax or an unrelated symbol, correct the test before touching production code.

- [ ] **Step 5: Replace active-length ring state with full-ring/tap-transition state**

In `engine/fx/bbd.h`, keep `cells_` as the latest clamped request and replace `imem_` with these members:

```cpp
    static constexpr int   kStageXfadeReads = 16;
    static constexpr float kStageXfadeInv = 1.f / 15.f;

    int      cells_ = 1;              // latest requested/clamped read distance
    int      write_index_ = 0;         // physical ring always wraps at max_cells_
    int      read_cells_ = 1;          // settled read distance
    int      stage_from_cells_ = 1;
    int      stage_to_cells_ = 1;
    int      stage_xfade_read_ = kStageXfadeReads;  // 16 means idle
```

Add these private helpers before the member block:

```cpp
    bool _stage_transition_active() const {
        return stage_xfade_read_ < kStageXfadeReads;
    }

    float _read_tap(int delay_cells) const {
        int index = write_index_ - delay_cells;
        if (index < 0) index += static_cast<int>(max_cells_);
        return mem_[index];
    }
```

Expose only the test observers next to `cells()`:

```cpp
    int cells() const { return cells_; }

#ifdef SPKY_TESTING
    int settled_cells() const { return read_cells_; }
    bool stage_transition_active() const { return _stage_transition_active(); }
#endif
```

Do not expose these observers through `BbdEcho`, `BbdEngine`, `Instrument` or any host API.

- [ ] **Step 6: Make Reset settle the latest request and cancel transitions**

Replace the current `imem_ = 0;` line in `BbdLine::Reset()` with:

```cpp
        write_index_ = 0;
        read_cells_ = cells_;
        stage_from_cells_ = cells_;
        stage_to_cells_ = cells_;
        stage_xfade_read_ = kStageXfadeReads;
```

Leave buffer clearing, tick phase/parity, filter-state clearing and dither reseeding unchanged.

- [ ] **Step 7: Make SetStages store a request without touching ring topology**

Replace `BbdLine::SetStages()` with:

```cpp
    // Physical stage count expressed as a requested read distance. The write
    // history always spans max_cells_; Process crossfades read taps instead
    // of changing the ring topology or resetting its head.
    void SetStages(int stages) {
        int c = stages / 2;
        const int lo = bbd_tuning::kMinStages / 2;
        const int hi = static_cast<int>(max_cells_);
        if (c < lo) c = lo;
        if (c > hi) c = hi;
        if (c < 1) c = 1;
        cells_ = c;
    }
```

There must be no assignment to `write_index_`, `read_cells_` or transition endpoints in this setter.

- [ ] **Step 8: Write through the full ring and crossfade taps on READ ticks**

Inside the WRITE phase of `BbdLine::Process()`, keep the input filter, loss pole, denormal guard and dither expression unchanged, but replace the memory access and wrap:

```cpp
                    mem_[write_index_] = dither_ != 0.f
                                              ? loss_z_ + dither_ * rng_.next_bipolar()
                                              : loss_z_;
                    write_index_ = (write_index_ + 1 < static_cast<int>(max_cells_))
                                         ? write_index_ + 1
                                         : 0;
```

Replace the READ phase's `const float ybbd = mem_[imem_];` with:

```cpp
                    if (!_stage_transition_active() && read_cells_ != cells_) {
                        stage_from_cells_ = read_cells_;
                        stage_to_cells_ = cells_;
                        stage_xfade_read_ = 0;
                    }

                    float ybbd = 0.f;
                    if (_stage_transition_active()) {
                        const float from = _read_tap(stage_from_cells_);
                        const float to = _read_tap(stage_to_cells_);
                        const float x = static_cast<float>(stage_xfade_read_)
                                      * kStageXfadeInv;
                        const float w = x * x * (3.f - 2.f * x);
                        ybbd = from + w * (to - from);
                        ++stage_xfade_read_;
                        if (stage_xfade_read_ >= kStageXfadeReads) {
                            read_cells_ = stage_to_cells_;
                            stage_xfade_read_ = kStageXfadeReads;
                        }
                    } else {
                        ybbd = _read_tap(read_cells_);
                    }
```

Keep the following `delta`, `ybbd_old_`, denormal guard and `Xout` accumulation unchanged. Do not duplicate the output filter or move the smoothstep after it.

- [ ] **Step 9: Build and run the focused RED-to-GREEN set**

Run:

```powershell
cmake --build build --target spky_tests
build\spky_tests.exe --test-case="bbd line: fixed-stage output stays bit-identical,bbd line: stage tap crossfade shrinks*,bbd line: Reset cancels*,bbd line: null memory*,bbd engine: RATE-modulated COLOR does not emit channel clicks" --no-skip
```

Expected: all selected tests pass. In particular:

- the Release-build digest remains exactly `0x12156b08`;
- shrink and expansion land after sixteen READ ticks;
- the rapid retarget lands at 3072 cells rather than restarting the first transition;
- Reset settles at 1024 cells and returns exact zero with dither disabled;
- the null-memory case stays finite;
- all three engine A/B/C runs stay below `0.03`, zero-width stays mono, and moving width remains stereo.

- [ ] **Step 10: Run the complete BBD test subset**

```powershell
build\spky_tests.exe --test-case="*bbd*" --no-skip
```

Expected: every matching BBD line, echo, music and engine test passes with zero failures.

- [ ] **Step 11: Inspect the production diff for forbidden costs and state duplication**

Run:

```powershell
git diff -- engine/fx/bbd.h tests/test_bbd.cpp tests/test_bbd_engine.cpp
rg -n "std::(sin|cos|pow|exp)|new |malloc|calloc|vector|BbdFilterCoef.*\[2\]|Compander.*\[2\]" engine/fx/bbd.h
```

Expected: the diff contains one physical write head, two integer read taps only during transitions, one existing filter state and no new matches for allocation, container ownership or transcendental calls. Existing `std::isfinite`/`std::sqrt` usages outside the new transition are not part of this cost check.

- [ ] **Step 12: Commit the tested DSP fix**

```powershell
git add engine/fx/bbd.h tests/test_bbd.cpp tests/test_bbd_engine.cpp
git commit -m "fix(bbd): crossfade dynamic stage taps"
```

---

### Task 3: Correct the design record and verify the complete system

**Files:**
- Modify: `docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md:343-351`
- Modify: `docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md:615-650`
- Create temporarily: `build/bbd_color_declick.json`
- Generate temporarily: `build/bbd_color_declick.wav`
- Generate temporarily: `build/bbd_color_declick.csv`

**Interfaces:**
- Consumes: Task 2's click-free `BbdLine`, the existing render host scenario actions, and the accepted design at `docs/superpowers/specs/2026-08-03-bbd-dynamic-stages-declick-design.md`.
- Produces: corrected durable documentation, complete desktop/build evidence and a WAV for the user's final headphone gate.

- [ ] **Step 1: Amend the original BBD physics description**

Replace the paragraph beginning `` `SetStages` changes `cells_` and nothing else`` with:

```markdown
`SetStages` stores a requested cell count; it does not change the physical ring
topology. The line writes continuously through its full injected memory and
reads at the requested distance behind that write head. When the request moves,
the old and new read distances are smoothstep-crossfaded over sixteen BBD READ
ticks before the existing reconstruction filter. This preserves the stored
history and removes the index reset that previously emitted a short impulse
during MOTION-modulated COLOR. The settled delay law is unchanged.
```

In section 5.7, after the COLOR clock/stage formulas, add:

```markdown
The per-channel stage requests may move every control block because COLOR also
receives MOTION. They therefore use the click-free read-tap transition described
in §5.1; neither channel resizes or resets its physical write ring. During the
short transition both taps read one continuous charge history, and the settled
endpoints still satisfy the two grid equations above.
```

- [ ] **Step 2: Run documentation and whitespace checks**

```powershell
rg -n "SetStages.*changes.*cells_|imem_ = 0|instead of clicking" docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md engine/fx/bbd.h
git diff --check
```

Expected: no live description claims that dynamic `SetStages` merely resizes the active ring or cannot click. Historical defect explanations may mention the removed `imem_ = 0` only when explicitly written in the past tense. `git diff --check` exits zero.

- [ ] **Step 3: Build the desktop tests, render host and both benchmark profiles**

Run from the repository root:

```powershell
cmake --build build --target spky_tests render
Push-Location bench
python run.py --profile bbd --build-only
python run.py --profile system --build-only
Pop-Location
```

Expected: all four builds exit zero. The BBD profile contains the isolated line rows; the system profile contains `instrument_worst_bbd`. Build-only does not require a connected Seed or ST-Link.

- [ ] **Step 4: Run the complete desktop suite and render-hash gates**

```powershell
ctest --test-dir build --output-on-failure
```

Expected: all four CTest entries pass: `spky_tests`, `ctrl_identity`, `wave_formant_sweep`, and `wavetable_bank_fresh`.

- [ ] **Step 5: Create the ignored listening scenario**

Create `build/bbd_color_declick.json` with exactly:

```json
{
  "sample_rate": 48000,
  "bpm": 120,
  "duration_s": 8,
  "input_wav": "host/render/scenarios/assets/in_pad.wav",
  "init": [
    {"action":"set_engine","part":0,"value":"bbd"},
    {"action":"set_engine","part":1,"value":"test_tone"},
    {"action":"set_target_active","part":1,"slot":4,"flag":false},
    {"action":"set_target_base","part":1,"slot":4,"value":0.0},
    {"action":"set_sync","ivalue":1},
    {"action":"set_rate","part":0,"value":0.5},
    {"action":"set_shape","part":0,"value":0.0},
    {"action":"set_depth","part":0,"value":1.0},
    {"action":"set_color","part":0,"value":0.2},
    {"action":"set_step","part":0,"flag":false,"ivalue":8},
    {"action":"set_target_active","part":0,"slot":0,"flag":false},
    {"action":"set_target_base","part":0,"slot":0,"value":0.15},
    {"action":"set_target_active","part":0,"slot":1,"flag":false},
    {"action":"set_target_base","part":0,"slot":1,"value":0.5},
    {"action":"set_target_active","part":0,"slot":2,"flag":false},
    {"action":"set_target_base","part":0,"slot":2,"value":0.5},
    {"action":"set_target_active","part":0,"slot":3,"flag":true},
    {"action":"set_target_base","part":0,"slot":3,"value":0.35},
    {"action":"set_target_depth","part":0,"slot":3,"value":0.0},
    {"action":"set_target_active","part":0,"slot":4,"flag":false},
    {"action":"set_target_base","part":0,"slot":4,"value":1.0},
    {"action":"set_voice_sub","part":0,"value":1.0},
    {"action":"set_voice_detune","part":0,"value":0.0},
    {"action":"set_reverb_mix","value":0.0}
  ],
  "events": []
}
```

This deliberately sets the MOTION target depth to zero while leaving the MOTION lane active: feedback stays fixed, while `Part::_color_eff` still receives the global MOTION depth. The render therefore isolates the COLOR/stage movement rather than conflating it with feedback modulation.

- [ ] **Step 6: Render and measure the listening case**

Run:

```powershell
build\render.exe build\bbd_color_declick.json build\bbd_color_declick.wav build\bbd_color_declick.csv
@'
import math
import struct
import wave

path = r"build/bbd_color_declick.wav"
with wave.open(path, "rb") as wav:
    assert wav.getframerate() == 48000
    assert wav.getnchannels() == 2
    assert wav.getsampwidth() == 2
    frames = wav.getnframes()
    raw = wav.readframes(frames)

pcm = struct.unpack("<" + "h" * (frames * 2), raw)
left = [pcm[i] / 32768.0 for i in range(0, len(pcm), 2)]
right = [pcm[i + 1] / 32768.0 for i in range(0, len(pcm), 2)]
start = 48000
max_l = max(abs(left[i] - left[i - 1]) for i in range(start, frames))
max_r = max(abs(right[i] - right[i - 1]) for i in range(start, frames))
stereo_rms = math.sqrt(
    sum((left[i] - right[i]) ** 2 for i in range(start, frames))
    / (frames - start)
)
print(f"max_delta_l={max_l:.6f}")
print(f"max_delta_r={max_r:.6f}")
print(f"stereo_rms={stereo_rms:.6f}")
assert max_l < 0.03
assert max_r < 0.03
assert stereo_rms > 0.001
'@ | python -
```

Expected: render exits zero; both maximum deltas are below `0.03`; `stereo_rms` is greater than `0.001`, proving modulation was not removed. Preserve the printed values in the implementation handoff.

- [ ] **Step 7: Perform the headphone acceptance gate**

Listen to `build/bbd_color_declick.wav` on headphones, concentrating on the right channel at the quarter-note RATE. Expected: the pad and BBD texture move in stereo, but the regular few-millisecond impulse is absent. If a rhythmic click remains audible despite the numeric bound, stop and retain the WAV plus its timestamps for a new root-cause pass; do not lower the threshold or add an output smoother.

- [ ] **Step 8: Record hardware measurement honestly**

If a Daisy Seed and ST-Link are connected, run both profiles normally and compare `bbd_line_ceiling` plus `instrument_worst_bbd` against the latest accepted evidence:

```powershell
Push-Location bench
python run.py --profile bbd --repeat 2
python run.py --profile system --repeat 2
Pop-Location
```

Expected with hardware: both repeated captures are deterministic and archived by `run.py`; report the before/after cycle figures without inventing an acceptance percentage. Without hardware: state `hardware benchmark not run — Daisy Seed/ST-Link unavailable`; the successful build-only results remain the only performance evidence.

- [ ] **Step 9: Commit the corrected design record**

```powershell
git add docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md
git commit -m "docs(bbd): describe click-free stage transitions"
```

Do not add `build/bbd_color_declick.json`, WAV, CSV or benchmark build products.

- [ ] **Step 10: Run the final clean-tree verification**

```powershell
git status --short
git log --oneline -4
ctest --test-dir build --output-on-failure
```

Expected: `git status --short` is empty; the log contains the baseline-test, DSP-fix and documentation commits after the design/plan commits; all four CTest entries pass in this fresh final run.

---

## Completion evidence to hand back

Report all of the following together:

- the fixed-stage Release-build digest (`0x12156b08` before and after);
- RED and GREEN maximum deltas for the moving-width regression;
- zero/fixed/moving stereo non-vacuity results;
- focused BBD and full CTest counts;
- render `max_delta_l`, `max_delta_r` and `stereo_rms`;
- headphone result;
- BBD/system benchmark build result;
- hardware cycle measurements, or the explicit unavailable-hardware statement;
- the final commit hashes and a clean-tree status.
