# BBD Part Engine (movement 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `ENGINE_BBD = 5` a selectable part engine — a stereo bucket-brigade delay played from the modulation plane, fed by audio-in and by the neighbouring deck.

**Architecture:** `BbdEcho` (already in the tree, physics only) gains a handful of neutral-by-default hooks. A new header-only `bbd_music.h` carries the musical layer `Flux` used to provide — the division ladder, the reachable clock window, and the lane→clock map. `BbdEngine` (`engine/parts/bbd_engine.{h,cpp}`) owns two `BbdEcho`, one per channel, and implements `IPartEngine`. `Part` pushes lane values into it exactly as it does for every other engine.

**Tech Stack:** C++17, no heap and no libm in the per-sample path under `engine/**`; doctest; CMake + Ninja + clang on the desktop, arm-none-eabi-g++ on the Seed; `host/render` for CSV-measurable behaviour; `host/vcv` for the panel.

**Spec:** `docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md` §5. Read the section a task names; do not read the whole spec.

---

## Corrections to the spec, applied by this plan

Two things in §5 do not survive being worked out numerically. Both are recorded here and the spec is amended in Task 3.

**1. The division clamp is at the SHORT end, not the long one.** §5.4 says a long `div` at a slow tempo can exceed `kMaxStages` and that `T` must then be clamped down. Under §5.3's decision — *the lane is scaled to what is reachable* — that cannot happen. The window is

```
f_lo = kMinStages / (2T) = 256 / T          f_hi = min(kClockMaxHz, kMaxStages / (2T)) = min(32000, 8192 / T)
```

and at `f_lo` the stage count is `2·T·(256/T) = 512` **for every T**. The long end is self-normalising. What does break is the short end: `f_lo > kClockMaxHz` whenever `T < 256 / 32000 = 8 ms`, and that is reachable — a free-running master lane at 30 Hz gives a 33 ms cycle, so `div = 1/32` asks for `T = 1.04 ms`. The clamp this engine needs is therefore

```
kMinDelayS = kMinStages / (2 · kClockMaxHz) = 8 ms
```

with `T` raised to it, which takes the repeats off the grid — and that is the thing the observer must report.

**2. The second observable is span truncation, not a second clamp.** §9 asks for "both clamp flags". There is only one clamp. The second quantity worth exposing is that the reachable span `f_hi/f_lo = min(32, 125·T)` falls below three octaves (`< 8`) at `T < 64 ms`, at which point §5.5's STEP grid cannot reach the top of the quantizer's 36-semitone span. Flag name: `scale_truncated`.

---

## Global Constraints

- **`EngineId` is append-only.** `ENGINE_BBD = 5`, after `ENGINE_BODY = 4`, before the `ENGINE_COUNT` sentinel. Never renumber: patches persist the id.
- **`process_in()` and `consumes_input()` are overridden together** or the engine's input feed goes silently missing (`engine_iface.h`). `Part::_engine_for`'s `default:` routes to the test tone — a missing `case` is silent.
- **All six `Part::set_voice_*` forwards are extended together** (`part.h:153-163`). One missed line is a dead knob with no diagnostic.
- **Bit-exactness is not an acceptance gate** (spec, "Note on bit-exactness"). A behaviour change is acceptable where it is *intended and stated*; a change nobody noticed is not. Where a neutral default is free — and in Task 2 it is — take it, because it makes the neutrality proof cheap.
- **No heap, no `std::complex`, no `<memory>`, no libm in the per-sample path under `engine/**`.** `powf`/`expf` at control rate is fine.
- **Denormal floor idiom:** `if (x < 1e-9f && x > -1e-9f) x = 0.f;` — as `comp.cpp:56` and `limiter.h:37` already do it.
- **Bound idiom:** `fast_tanh(...)` from `engine/util/fast_tanh.h`, as `part.cpp:385` and `part.h:350` already do it.
- **Commit trailer**, on every commit:
  ```
  Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
  ```
- **Build/test commands** (from the repo root; `source env.sh` first — the system `g++` on this machine is the ARM cross-compiler):
  ```bash
  source env.sh
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
  ctest --test-dir build --output-on-failure
  ```
  The VCV host builds **only** via `./build-local.sh`. Never invoke `g++` by hand.
- **Panel tests:** `cd host/vcv/res && python test_panel.py` — a plain script, not pytest. It prints `FAIL (n):` followed by one line per failure, or `OK`.

---

## File Structure

| File | Responsibility |
|---|---|
| `engine/fx/bbd.h` *(modify)* | Physics. Gains `Reset()`, a settable loss coefficient, a feedback-path DC blocker and tilt, a dither level, and a denormal floor. Every addition neutral at its default. |
| `engine/parts/bbd_music.h` *(create)* | The musical layer, header-only and stateless but for the ladder's hysteresis: rungs, clock window, lane→clock, clock→stages, STEP semitones. No audio. |
| `engine/parts/bbd_engine.h/.cpp` *(create)* | `BbdEngine : IPartEngine`. Two `BbdEcho`, the freeze, the VOICE row, COLOR, the observers. |
| `engine/parts/engine_iface.h` *(modify)* | `ENGINE_BBD = 5`; `virtual void set_width(float)`. |
| `engine/parts/part.h/.cpp` *(modify)* | `_engine_for` arm, buffer injection, the six VOICE forwards, the `_pitch_q` grid rule, the `set_width` push. |
| `engine/instrument.h/.cpp` *(modify)* | `FxMem::bbd[PART_COUNT][2]`, forwarded to `Part::init`. |
| `host/render/scenario.cpp`, `host/render/main.cpp` *(modify)* | `parse_engine` spelling `"bbd"`; six observer columns per deck. |
| `host/vcv/src/Spotymod.cpp`, `host/vcv/res/test_panel.py` *(modify)* | The five-position ENG switch, the dispatch arm, the shade, the caption, the re-pointed `STAGES_A/B`. |
| `bench/audition/init_patch.cpp` *(modify)* | The missing BODY arm (Task 1) and the BBD arm (Task 10). |
| `bench/workloads_system.cpp`, `bench/run.py` *(modify)* | The `inst_bbd_engine_worst` row. |
| `tests/test_bbd_music.cpp`, `tests/test_bbd_engine.cpp` *(create)* | The two new suites. Register both in `CMakeLists.txt`. |

---

## Task 1: Rebase the panel and audition pins onto the shipped snapshot

`host/vcv/res/test_panel.py` is red with **53** failures on `main` today, and none of them is this plan's doing: **3** pin a three-engine ENG switch that shipped as four, **3** pin a three-state SOURCE caption that shipped as four, and **47** are init-snapshot values that drifted when `drone.vcvm` replaced `sampler.vcvm`. The spec (§5.12) rules that **the code is authoritative and the pins are rewritten**. Doing this first, on its own commit, means every later task starts from a green board and any new red is genuinely new.

`tests/test_seed_audition_init.cpp` reads the same table and carries the same drift. `bench/audition/init_patch.cpp` is separately mis-routing: it has no BODY arm, so with `initParamDefault(ENGINE_B) == 3` deck B boots there as SAMPLER while the VCV host gives it BODY.

**Files:**
- Modify: `host/vcv/res/test_panel.py:109-111` (SOURCE caption), `:876-905` (ENG latch/config/dispatch), `:1247-1272` (init snapshot)
- Modify: `tests/test_seed_audition_init.cpp:33-45`
- Modify: `bench/audition/init_patch.cpp:59-65`

**Interfaces:**
- Consumes: nothing.
- Produces: a green `test_panel.py` and a green `ctest`. Later tasks extend the same three pin sites.

- [ ] **Step 1: Capture the current failures as the working list**

```bash
cd host/vcv/res && python test_panel.py > /tmp/panel-before.txt 2>&1; tail -60 /tmp/panel-before.txt
```

Expected: `FAIL (53):` and 53 `  - ` lines. If the count is not 53, **stop and report** — the tree moved and this task's premise needs re-checking.

- [ ] **Step 2: Rewrite the three ENG pins to what shipped**

In `host/vcv/res/test_panel.py`, the `latch_expected` string pins a two-shade `drawLayer`; the shipped `EngineCycleLatch` (`host/vcv/src/Spotymod.cpp:111-134`) reads a four-entry `kEngineShades[]` table with a defensive index. Replace the body of `latch_expected` with the shipped one:

```python
    latch_expected = """
void drawLayer(const DrawArgs& args, int layer) override {
    VCVLatch::drawLayer(args, layer);
    if (layer != 1) return;
    engine::ParamQuantity* pq = getParamQuantity();
    if (!pq) return;
    const int state = static_cast<int>(std::round(pq->getValue()));
    if (state == 0) return;
    constexpr int kShadeCount = sizeof(kEngineShades) / sizeof(kEngineShades[0]);
    const NVGcolor color = kEngineShades[
        state >= 0 && state < kShadeCount ? state : kShadeCount - 1];
    const Vec c = box.size.div(2.f);
    nvgBeginPath(args.vg);
    nvgCircle(args.vg, c.x, c.y, 5.2f);
    nvgStrokeWidth(args.vg, 1.4f);
    nvgStrokeColor(args.vg, color);
    nvgStroke(args.vg);
}"""
```

Then the config pin:

```python
    engine_config = """
else if (c.id == ENGINE_A || c.id == ENGINE_B) {
    configSwitch(c.id, 0.f, 3.f, init, "Engine", {"Synth", "Sampler", "Wave", "Body"});
    getParamQuantity(c.id)->snapEnabled = true;
}"""
    if compact_cpp(config).count(compact_cpp(engine_config)) != 1:
        issues.append("ENG config must be one snapped Synth/Sampler/Wave/Body 0..3 branch")
```

Then the dispatch pin:

```python
    dispatch = """
const int eng = static_cast<int>(std::round(pp(ENGINE_A, p)));
const spky::EngineId id =
    eng == 0 ? spky::ENGINE_SYNTH :
    eng == 2 ? spky::ENGINE_WAVE :
    eng == 3 ? spky::ENGINE_BODY :
    smp[p].testTone ? spky::ENGINE_TEST_TONE : spky::ENGINE_SAMPLER;
inst.set_engine(p, id);"""
    if push_n.count(compact_cpp(dispatch)) != 1:
        issues.append("ENG dispatch must exactly preserve Synth/Sampler/Wave/Body/test-tone states")
```

- [ ] **Step 3: Rewrite the SOURCE caption pin**

`host/vcv/res/test_panel.py:109-111` expects three states; `sourceCaption` (`Spotymod.cpp:1066-1068`) ships four, BODY's being `MATL`:

```python
    want = {0: "TIMB", 1: "ORG", 2: "FRAME", 3: "MATL"}
```

Search the same file for the sibling assertion that names the caption set in prose (it reports *"SOURCE A/B need stable names and a TIMB/FRAME/ORG description"*) and extend its expected description to include `MATL`, matching the shipped tooltip text exactly. Read the tooltip out of `host/vcv/res/gen_panel.py` rather than guessing it.

- [ ] **Step 4: Regenerate the init snapshot pin from the shipped header**

Do not hand-edit 47 floats. Generate the list:

```bash
cd host/vcv/res && python - <<'PY'
import re
h = open("../src/init_patch.hpp").read()
body = re.search(r"kInitParamDefaults\[\]\s*=\s*\{(.*?)\};", h, re.DOTALL).group(1)
vals = []
for raw in body.splitlines():
    v = raw.split("//", 1)[0].strip().rstrip(",")
    if v:
        vals.append(repr(float(v.removesuffix("f"))))
print("    expected = [")
for i in range(0, len(vals), 3):
    print("        " + ", ".join(vals[i:i+3]) + ",")
print("    ]")
print("# count:", len(vals))
PY
```

Expected: `# count: 84`. Paste the printed block over `test_panel.py:1247-1272`. The assertion loop below it (`math.isclose(..., abs_tol=1e-7)`) is unchanged — it is what makes this a pin rather than a comment.

- [ ] **Step 5: Run the panel suite**

```bash
cd host/vcv/res && python test_panel.py
```

Expected: `OK`. Any residual failure is a real drift the generation above did not cover — fix it against the shipped code, never by loosening a tolerance.

- [ ] **Step 6: Rebase the audition pins**

`tests/test_seed_audition_init.cpp:33-45` asserts `ENGINE_B == 1` and `TEMPO == 0.5`; the shipped snapshot has `3` and `0.169333577`. Read the four values out of `init_patch.hpp` and write them in:

```cpp
    CHECK(spkyvcv::NUM_PARAMS == 84);
    CHECK(spkyvcv::initParamDefault(spkyvcv::ENGINE_A)
          == doctest::Approx(0.f));
    // drone.vcvm boots deck B on BODY (3), not on the sampler. The pin moved
    // with the snapshot; the two must never disagree, because
    // bench/audition/init_patch.cpp dispatches off this same table.
    CHECK(spkyvcv::initParamDefault(spkyvcv::ENGINE_B)
          == doctest::Approx(3.f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::TEMPO)
          == doctest::Approx(0.169333577f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::DETUNE_B)
          == doctest::Approx(6.f / 35.f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::LINK_A) == doctest::Approx(0.f));
    CHECK(spkyvcv::initParamDefault(spkyvcv::LINK_B) == doctest::Approx(0.f));
```

Check `DETUNE_B` and both `LINK` values against the header before assuming they still hold.

- [ ] **Step 7: Give the audition bench its missing BODY arm**

`bench/audition/init_patch.cpp:59-65`:

```cpp
        const int engine_value
            = static_cast<int>(std::lround(part(ENGINE_A, deck)));
        const spky::EngineId engine
            = engine_value == 0 ? spky::ENGINE_SYNTH
              : engine_value == 2 ? spky::ENGINE_WAVE
              : engine_value == 3 ? spky::ENGINE_BODY
                                  : spky::ENGINE_SAMPLER;
```

- [ ] **Step 8: Write the test that this arm exists**

The arm above is exactly the class of thing that goes wrong silently. Add to `tests/test_seed_audition_init.cpp`:

```cpp
TEST_CASE("Seed audition boots the same engines the VCV host does")
{
    spky::Instrument inst;
    spkyvcv::audition_init(inst);   // use this file's existing init helper name
    // The snapshot puts deck B on BODY. Before 2026-07-31 this file had no
    // BODY arm and deck B silently booted as SAMPLER -- audible on the Seed,
    // invisible in every test.
    CHECK(inst.engine_id(spky::PART_A) == spky::ENGINE_SYNTH);
    CHECK(inst.engine_id(spky::PART_B) == spky::ENGINE_BODY);
}
```

Read the file's existing `TEST_CASE` bodies for the real name of the init helper and the include it needs; do not invent one.

- [ ] **Step 9: Prove the new test can fail**

Temporarily delete the `engine_value == 3` arm added in Step 7, rebuild, and run:

```bash
cmake --build build && ctest --test-dir build -R seed_audition --output-on-failure
```

Expected: RED, reporting `ENGINE_BODY` wanted and `ENGINE_SAMPLER` seen. Restore the arm and confirm green. **Paste both outputs into the task report** — a pin nobody has seen fail is not evidence.

- [ ] **Step 10: Full suite**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all green, including the two render-hash gates (`ctrl_identity`, `wave_formant_sweep`) — this task touches no DSP.

- [ ] **Step 11: Commit**

```bash
git add host/vcv/res/test_panel.py tests/test_seed_audition_init.cpp bench/audition/init_patch.cpp
git commit -F - <<'EOF'
test(panel): rebase the pins onto the snapshot that shipped

test_panel.py has been red with 53 failures since drone.vcvm replaced
sampler.vcvm: 3 pin a three-engine ENG switch that shipped as four, 3 pin a
three-state SOURCE caption that shipped as four, and 47 are init values that
moved with the preset. The code is authoritative -- the drift shipped and is
what the instrument boots -- so the pins are rewritten, not the snapshot.

tests/test_seed_audition_init.cpp carried the same drift. bench/audition/
init_patch.cpp was separately wrong: with no BODY arm, deck B booted there as
SAMPLER while the VCV host gave it BODY. Fixed, and pinned by a test proven
to go red without the arm.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 2: `BbdEcho` gains the engine's hooks, neutral by default

The engine needs five things the class does not have: a public reset, a settable loss pole (FILT), a feedback-path DC blocker and tilt (the freeze and RESONANCE), a dither floor (so the engine sings from nothing), and a denormal floor (it runs always-on, unlike the `SoftSwitch`-gated `Flux`).

Every one of them defaults to the value that makes the current code path bit-identical, so `Flux` is untouched and the neutrality proof is one render.

**Files:**
- Modify: `engine/fx/bbd.h` — `BbdLine` (`:298-412`), `BbdEcho` (`:494-586`)
- Test: `tests/test_bbd.cpp`

**Interfaces:**
- Produces, on `BbdEcho`:
  ```cpp
  void  Reset();                                   // line + compander + fb state
  void  SetLossCoef(float a);                      // default bbd_tuning::kLossCoef
  void  SetDither(float amp);                      // default 0.f  -> bit-exact
  void  SetFeedbackTilt(float tilt, float corner_hz); // default 0.f -> bit-exact
  void  SetFeedbackDcBlock(bool on);               // default false -> bit-exact
  void  SeedDither(uint32_t s);
  float FeedbackState() const;                     // observer, tests only
  ```

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_bbd.cpp`:

```cpp
TEST_CASE("bbd: the new hooks are all neutral at their defaults") {
    static float bufA[Flux::kMaxSamples];
    static float bufB[Flux::kMaxSamples];
    BbdEcho a, b;
    a.Init(48000.f, bufA, Flux::kMaxSamples);
    b.Init(48000.f, bufB, Flux::kMaxSamples);
    // b touches every new setter with its documented neutral value.
    b.SetLossCoef(bbd_tuning::kLossCoef);
    b.SetDither(0.f);
    b.SetFeedbackTilt(0.f, 4000.f);
    b.SetFeedbackDcBlock(false);
    a.SetFeedback(0.6f);  b.SetFeedback(0.6f);
    a.SetDrive(0.3f);     b.SetDrive(0.3f);
    a.SetStages(8192);    b.SetStages(8192);
    for (int i = 0; i < 48000; ++i) {
        const float x = std::sin(i * 0.01f) * (i < 4800 ? 1.f : 0.f);
        // Bit-identical, not approximately: a neutral default that only nearly
        // reproduces the old path is a behaviour change nobody stated.
        CHECK(a.Process(x, 6000.f) == b.Process(x, 6000.f));
    }
}

TEST_CASE("bbd: Reset clears the line, the compander and the feedback state") {
    static float buf[Flux::kMaxSamples];
    BbdEcho e;
    e.Init(48000.f, buf, Flux::kMaxSamples);
    e.SetFeedback(0.8f);
    e.SetStages(2048);
    for (int i = 0; i < 24000; ++i) e.Process(std::sin(i * 0.05f), 6000.f);
    CHECK(std::fabs(e.FeedbackState()) > 1e-4f);   // charge is in flight
    e.Reset();
    CHECK(e.FeedbackState() == 0.f);
    // Nothing may come back out of a reset line for a full delay period.
    float peak = 0.f;
    for (int i = 0; i < 24000; ++i)
        peak = std::max(peak, std::fabs(e.Process(0.f, 6000.f)));
    CHECK(peak < 1e-6f);
}

TEST_CASE("bbd: dither makes a silent line audible and stays inaudible itself") {
    static float buf[Flux::kMaxSamples];
    BbdEcho e;
    e.Init(48000.f, buf, Flux::kMaxSamples);
    e.SeedDither(0x1234u);
    e.SetDither(4e-5f);
    e.SetStages(4096);
    e.SetFeedback(0.f);
    float rms = 0.f;
    for (int i = 0; i < 48000; ++i) {
        const float y = e.Process(0.f, 6000.f);
        rms += y * y;
    }
    rms = std::sqrt(rms / 48000.f);
    CHECK(rms > 0.f);          // it is not silence
    CHECK(rms < 1e-3f);        // and it is below -60 dBFS
}

TEST_CASE("bbd: the loss coefficient moves the darkness") {
    static float bufD[Flux::kMaxSamples];
    static float bufB[Flux::kMaxSamples];
    auto hi_energy = [](BbdEcho& e) {
        float s = 0.f;
        for (int i = 0; i < 48000; ++i) {
            const float x = std::sin(i * 0.5f);      // ~3.8 kHz at 48 k
            const float y = e.Process(x, 8000.f);
            if (i > 24000) s += y * y;
        }
        return s;
    };
    BbdEcho dark, bright;
    dark.Init(48000.f, bufD, Flux::kMaxSamples);
    bright.Init(48000.f, bufB, Flux::kMaxSamples);
    dark.SetStages(4096);   bright.SetStages(4096);
    dark.SetLossCoef(0.2f);
    bright.SetLossCoef(0.95f);
    CHECK(hi_energy(dark) < hi_energy(bright));
}

TEST_CASE("bbd: the feedback tilt brightens or darkens the repeats") {
    static float bufN[Flux::kMaxSamples];
    static float bufU[Flux::kMaxSamples];
    auto tail_hi = [](BbdEcho& e) {
        float s = 0.f;
        for (int i = 0; i < 96000; ++i) {
            const float x = (i < 2400) ? std::sin(i * 0.4f) : 0.f;
            const float y = e.Process(x, 8000.f);
            if (i > 48000) s += y * y;   // long after the input stopped
        }
        return s;
    };
    BbdEcho flat, up;
    flat.Init(48000.f, bufN, Flux::kMaxSamples);
    up.Init(48000.f, bufU, Flux::kMaxSamples);
    flat.SetStages(4096);  up.SetStages(4096);
    flat.SetFeedback(0.7f); up.SetFeedback(0.7f);
    up.SetFeedbackTilt(0.8f, 2000.f);
    CHECK(tail_hi(up) > tail_hi(flat));
}

TEST_CASE("bbd: the feedback DC blocker stops a frozen loop drifting") {
    static float buf[Flux::kMaxSamples];
    BbdEcho e;
    e.Init(48000.f, buf, Flux::kMaxSamples);
    e.SetStages(4096);
    e.SetFeedbackDcBlock(true);
    e.SetFeedback(0.999f);
    for (int i = 0; i < 4800; ++i) e.Process(0.5f, 8000.f);   // pump DC in
    float mean = 0.f;
    const int n = 48000 * 10;
    for (int i = 0; i < n; ++i) mean += e.Process(0.f, 8000.f);
    mean /= n;
    CHECK(std::fabs(mean) < 1e-3f);
}
```

- [ ] **Step 2: Run them and watch them fail**

```bash
source env.sh && cmake --build build && ctest --test-dir build -R test_bbd --output-on-failure
```

Expected: compile errors — `SetLossCoef`, `SetDither`, `SetFeedbackTilt`, `SetFeedbackDcBlock`, `SeedDither`, `FeedbackState`, `Reset` are not members of `BbdEcho`.

- [ ] **Step 3: Give `BbdLine` a settable loss coefficient, dither and a denormal floor**

In `engine/fx/bbd.h`, add `#include "mod/rng.h"` at the top, then inside `class BbdLine`:

```cpp
    // FILT. The loss pole is the pole that actually carries the darkness --
    // at 16384 stages the fixed Butterworth chain contributes only -0.93 dB at
    // 2.5 kHz. kFilterHz cannot be a knob: it is constexpr, baked into two
    // file-scope singletons every line holds raw pointers into, and a rebuild
    // is 396 transcendentals (see bbd.h:270-281).
    void SetLossCoef(float a) {
        loss_a_ = a < 1e-4f ? 1e-4f : (a > 0.999f ? 0.999f : a);
    }
    // A few LSBs injected at the WRITE tick. The compander exists to manage the
    // BBD's 75 dB noise floor, which this model does not have: zero in, zero
    // state, zero out, forever, at any feedback. Behind Flux's SoftSwitch that
    // never mattered. An always-on part engine with FEEDBACK up and nothing
    // connected would otherwise produce bit-exact silence. 0 keeps the old path.
    void SetDither(float amp) { dither_ = amp < 0.f ? 0.f : amp; }
    void SeedDither(uint32_t s) { rng_.seed(s); }
```

and the members, next to `loss_z_`:

```cpp
    float loss_a_ = bbd_tuning::kLossCoef;
    float dither_ = 0.f;
    Rng   rng_;
```

`Reset()` gains `rng_.seed(0x9e3779b9u);` so a reset line is reproducible. In `Process`, the WRITE branch becomes:

```cpp
                    loss_z_ += loss_a_ * (s - loss_z_);
                    // Denormal floor: geometric decay parks every one of these
                    // states in the denormal range during a long silence, and
                    // the buffer fills with them -- a large, load-dependent
                    // stall on x86. Same idiom as comp.cpp:56 / limiter.h:37.
                    if (loss_z_ < 1e-9f && loss_z_ > -1e-9f) loss_z_ = 0.f;
                    mem_[imem_] = dither_ != 0.f
                                      ? loss_z_ + dither_ * rng_.next_bipolar()
                                      : loss_z_;
```

The ternary is what keeps the default path bit-exact: with `dither_ == 0` the stored value is `loss_z_` itself, not `loss_z_ + 0.f * r`, and the PRNG is not advanced. Add the same floor to `ybbd_old_` right after it is assigned in the READ branch.

- [ ] **Step 4: Give `BbdEcho` the feedback-path hooks and a reset**

Inside `class BbdEcho`, after `SetStages`:

```cpp
    // Clears the line, the compander envelopes and the feedback state. Needed
    // because Part has no swap-away notification: an engine switched away from
    // and back to would otherwise return the previous take's charge. BbdLine::
    // Reset was private to this class and BbdEcho exposed nothing.
    void Reset() {
        line_.Reset();
        comp_.Reset();
        fb_state_ = 0.f;
        tilt_z_ = 0.f;
        dc_x1_ = 0.f;
        dc_y1_ = 0.f;
    }

    void SetLossCoef(float a) { line_.SetLossCoef(a); }
    void SetDither(float amp) { line_.SetDither(amp); }
    void SeedDither(uint32_t s) { line_.SeedDither(s); }

    // RESONANCE, and the freeze's spectral leg. A one-pole shelf on the
    // FEEDBACK path only: y = x + tilt * (x - lp(x)). tilt > 0 restores what
    // the loss pole took, so repeats keep their brightness; tilt < 0 darkens
    // faster than physics. EXACTLY 0 is `y = x`, hence bit-exact.
    //
    // Why the freeze needs it: the compander's round trip is L^2, not L (with
    // constant inner gain L, compressor -> line -> expander measures L^2 above
    // about -40 dBFS and L below it), so a scalar loop gain cannot hold a
    // spectrum -- at 8192 stages, k tuned for 1 kHz leaves 110 Hz at +7.2 dB
    // and 2.5 kHz at -48.8 dB after ten circulations. Flattening L to ~1 with
    // this tilt is what makes L^2 harmless and makes one scalar sufficient.
    void SetFeedbackTilt(float tilt, float corner_hz) {
        tilt_ = tilt;
        const float fc = corner_hz > 1.f ? corner_hz : 1.f;
        float c = 1.f - std::exp(-2.f * 3.14159265f * fc / sr_);
        tilt_c_ = c > 1.f ? 1.f : (c < 1e-5f ? 1e-5f : c);
    }

    // The Butterworth sections are normalised H(0)=1 and the loss pole is unity
    // at DC, so any loop gain above unity at 1 kHz is strictly above unity at
    // DC and grows monotonically until it parks the saturator. daisysp::DcBlock
    // idiom, as part.cpp:385 uses it.
    void SetFeedbackDcBlock(bool on) { dc_on_ = on; }

    float FeedbackState() const { return fb_state_; }
```

`Init` gains `sr_ = sample_rate > 0.f ? sample_rate : 48000.f;` and a `SetFeedbackTilt(0.f, 4000.f); dc_on_ = false; Reset();` tail. `Process` changes only in how `fb_state_` re-enters:

```cpp
    float Process(float in, float clock_hz) {
        line_.SetClock(clock_hz);
        const float x = in + fb_path(fb_state_) * feedback_;
        const float sat = fast_tanh(x * sat_in_) * sat_out_;
        const float y = comp_.Expand(line_.Process(comp_.Compress(sat)));
        fb_state_ = y;
        return y;
    }

private:
    // Identity when tilt_ == 0 and dc_on_ == false -- the two defaults -- so
    // the shipped Flux path is bit-exact through it.
    float fb_path(float x) {
        if (dc_on_) {
            const float y = x - dc_x1_ + 0.999f * dc_y1_;
            dc_x1_ = x;
            dc_y1_ = y;
            if (dc_y1_ < 1e-9f && dc_y1_ > -1e-9f) dc_y1_ = 0.f;
            x = y;
        }
        if (tilt_ != 0.f) {
            tilt_z_ += tilt_c_ * (x - tilt_z_);
            if (tilt_z_ < 1e-9f && tilt_z_ > -1e-9f) tilt_z_ = 0.f;
            x += tilt_ * (x - tilt_z_);
        }
        return x;
    }

    float sr_ = 48000.f;
    float tilt_ = 0.f;
    float tilt_c_ = 1e-5f;
    float tilt_z_ = 0.f;
    bool  dc_on_ = false;
    float dc_x1_ = 0.f;
    float dc_y1_ = 0.f;
```

`std::exp` is only reachable from `SetFeedbackTilt`, a control-rate setter — not the per-sample path.

- [ ] **Step 5: Run the tests**

```bash
cmake --build build && ctest --test-dir build -R test_bbd --output-on-failure
```

Expected: PASS, including the bit-identity case.

- [ ] **Step 6: Prove the neutrality test can fail**

Change `SetDither`'s default member from `0.f` to `1e-6f`, rebuild, run. Expected: the bit-identity case goes RED. Revert. Paste both outputs into the report.

- [ ] **Step 7: Full suite plus the render gates**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all green, `ctrl_identity` and `wave_formant_sweep` included. If a render hash moved, the cause is the unconditional denormal floor — which fires only below −180 dBFS. Investigate before re-baselining; if it really is the floor, re-baseline and **say so in the commit message**.

- [ ] **Step 8: Commit**

```bash
git add engine/fx/bbd.h tests/test_bbd.cpp
git commit -F - <<'EOF'
feat(bbd): the hooks a part engine needs, all neutral by default

Reset (the class exposed none, and BbdLine::Reset was private), a settable
loss coefficient (FILT -- kFilterHz cannot be a knob), a feedback-path DC
blocker and one-pole tilt (the freeze and RESONANCE are the same filter), a
dither level, and a denormal floor.

Every addition defaults to the value that reproduces the shipped path bit for
bit, so Flux is untouched and the neutrality proof is one render. The dither
ternary is deliberate: at amp 0 the stored value is loss_z_ itself and the
PRNG does not advance.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 3: `bbd_music.h` — the layer `Flux` used to provide

Pure arithmetic, header-only, with one small stateful class for the ladder's hysteresis. Testable without any audio, which is why it is its own task: every claim §5.2–§5.5 makes about pitch, time and range becomes an assertion here.

Read **§5.2, §5.3, §5.4 and §5.5** of the spec, and the "Corrections to the spec" section at the top of this plan — the clamp is at the **short** end, and the plan's derivation supersedes §5.4's text.

**Files:**
- Create: `engine/parts/bbd_music.h`
- Create: `tests/test_bbd_music.cpp`
- Modify: `CMakeLists.txt` (add `tests/test_bbd_music.cpp` next to `tests/test_bbd.cpp`)
- Modify: `docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md` (§5.4 and §9, per Step 7)

**Interfaces:**
- Consumes: `bbd_tuning::kMinStages`, `kMaxStages`, `kClockMaxHz` from `engine/fx/bbd.h`.
- Produces:
  ```cpp
  namespace spky { namespace bbd_music {
      inline constexpr float kDivs[11];
      inline constexpr int   kDivCount = 11;
      inline constexpr float kMinDelayS;                 // 0.008
      class  DivLadder { int process(float lane); int index() const; void reset(int i); };
      struct Window { float t_eff, f_lo, f_hi; bool time_clamped, scale_truncated; };
      Window window(float t_seconds);
      float  clock_flow(const Window& w, float lane);    // geometric across the window
      float  clock_step(const Window& w, float q_norm);  // 36 semitones from f_lo
      int    stages_for(const Window& w, float f_clk);
  }}
  ```

- [ ] **Step 1: Write the failing tests**

Create `tests/test_bbd_music.cpp`:

```cpp
#include "doctest.h"
#include "parts/bbd_music.h"
#include <cmath>

using namespace spky;
using namespace spky::bbd_music;

TEST_CASE("bbd_music: the ladder has eleven rungs, fastest first") {
    CHECK(kDivCount == 11);
    CHECK(kDivs[0] == doctest::Approx(1.f / 32.f));
    CHECK(kDivs[10] == doctest::Approx(1.f));
    for (int i = 1; i < kDivCount; ++i) CHECK(kDivs[i] > kDivs[i - 1]);
}

TEST_CASE("bbd_music: the ladder snaps, and holds through one rung of overlap") {
    DivLadder L;
    L.reset(5);
    // Inside the held rung's own half-width: no move.
    CHECK(L.process(5.f / 10.f) == 5);
    CHECK(L.process(5.4f / 10.f) == 5);
    // Past the NEXT rung's centre: move, and only by one.
    CHECK(L.process(6.0f / 10.f) == 6);
    CHECK(L.process(5.6f / 10.f) == 6);   // sticky coming back
    CHECK(L.process(5.0f / 10.f) == 5);
    CHECK(L.process(0.f) == 0);
    CHECK(L.process(1.f) == 10);
}

TEST_CASE("bbd_music: a lane dithering on a boundary does not chatter") {
    DivLadder L;
    L.reset(4);
    int changes = 0, prev = 4;
    for (int i = 0; i < 400; ++i) {
        // A boundary-hugging wobble of +-2% of the lane, which a bare
        // nearest-rung round would flip on nearly every sample.
        const float lane = 0.45f + 0.02f * std::sin(i * 1.7f);
        const int r = L.process(lane);
        if (r != prev) { ++changes; prev = r; }
    }
    CHECK(changes == 0);
}

TEST_CASE("bbd_music: the reachable window matches the spec's table") {
    // T >= 256 ms: the full 32x, five octaves.
    const Window a = window(0.256f);
    CHECK(a.f_lo == doctest::Approx(1000.f));
    CHECK(a.f_hi == doctest::Approx(32000.f));
    CHECK(!a.time_clamped);
    CHECK(!a.scale_truncated);
    // Long T stays at 32x -- the long end is self-normalising, because f_lo is
    // DEFINED as kMinStages/(2T), so the stage count at f_lo is 512 for every T.
    const Window l = window(2.0f);
    CHECK(l.f_hi / l.f_lo == doctest::Approx(32.f));
    // 125 ms: 15.6x.
    const Window b = window(0.125f);
    CHECK(b.f_lo == doctest::Approx(2048.f));
    CHECK(b.f_hi / b.f_lo == doctest::Approx(15.625f));
    // 50 ms: 6.25x, under three octaves -- STEP cannot reach the scale's top.
    const Window c = window(0.050f);
    CHECK(c.f_hi / c.f_lo == doctest::Approx(6.25f));
    CHECK(c.scale_truncated);
}

TEST_CASE("bbd_music: T below the floor is clamped, and says so") {
    // A free master lane at 30 Hz gives a 33 ms cycle; div 1/32 asks for 1 ms.
    const Window w = window(0.00104f);
    CHECK(w.time_clamped);
    CHECK(w.t_eff == doctest::Approx(kMinDelayS));
    CHECK(kMinDelayS == doctest::Approx(0.008f));
    // At the floor the window has collapsed: the lane is dead, and honestly so.
    CHECK(w.f_hi / w.f_lo == doctest::Approx(1.f));
}

TEST_CASE("bbd_music: the stage count holds the delay on the grid") {
    const Window w = window(0.25f);
    for (float lane = 0.f; lane <= 1.0001f; lane += 0.05f) {
        const float f = clock_flow(w, lane);
        const int st = stages_for(w, f);
        CHECK(st >= bbd_tuning::kMinStages);
        CHECK(st <= bbd_tuning::kMaxStages);
        // delay = stages / (2 f_clk) == T, to within the rounding of one stage.
        const float delay = st / (2.f * f);
        CHECK(delay == doctest::Approx(w.t_eff).epsilon(0.002));
    }
}

TEST_CASE("bbd_music: the lane spans its full travel at every division") {
    // No dead zone at the top: whatever the division, lane 1 lands exactly on
    // f_hi and lane 0 exactly on f_lo. The alternative -- a fixed frequency
    // range that clamps -- puts a silently-moving dead zone in the master lane.
    const float times[] = { 0.020f, 0.050f, 0.125f, 0.25f, 0.5f, 1.0f, 4.0f };
    for (float t : times) {
        const Window w = window(t);
        CHECK(clock_flow(w, 0.f) == doctest::Approx(w.f_lo));
        CHECK(clock_flow(w, 1.f) == doctest::Approx(w.f_hi));
        CHECK(clock_flow(w, 0.5f)
              == doctest::Approx(std::sqrt(w.f_lo * w.f_hi)));   // geometric
    }
}

TEST_CASE("bbd_music: STEP re-derives semitones against the quantizer's span") {
    // Quantizer::SPAN_SEMIS is 36, not 60: process() returns note/36. Mapping
    // that straight onto a five-octave clock span would make one quantizer step
    // 1.667 semitones of clock ratio -- a grid that is not a scale.
    const Window w = window(0.5f);            // full 32x available
    CHECK(clock_step(w, 0.f) == doctest::Approx(w.f_lo));
    CHECK(clock_step(w, 12.f / 36.f) == doctest::Approx(w.f_lo * 2.f));
    CHECK(clock_step(w, 24.f / 36.f) == doctest::Approx(w.f_lo * 4.f));
    CHECK(clock_step(w, 1.f) == doctest::Approx(w.f_lo * 8.f));
    // And it never leaves the reachable window.
    const Window n = window(0.050f);          // only 6.25x
    CHECK(n.scale_truncated);
    CHECK(clock_step(n, 1.f) == doctest::Approx(n.f_hi));
}
```

- [ ] **Step 2: Run and watch them fail**

```bash
source env.sh && cmake --build build 2>&1 | head -20
```

Expected: `fatal error: parts/bbd_music.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `engine/parts/bbd_music.h`:

```cpp
#pragma once
#include <cmath>
#include "fx/bbd.h"
#include "util/math.h"

namespace spky {
namespace bbd_music {

// The division ladder. `div` multiplies the master-lane CYCLE -- Part feeds
// set_cycle 1/master_hz, i.e. the whole phrase, not a beat (part.cpp:423-426),
// and master_hz excludes clock_scale() and the EVOLVE walk. The ladder must
// therefore reach down to a step boundary, which in STEP is cycle/steps.
//
// Eleven rungs, straight and triplet interleaved, fastest first.
inline constexpr float kDivs[] = {
    1.f / 32.f, 1.f / 24.f, 1.f / 16.f, 1.f / 12.f, 1.f / 8.f, 1.f / 6.f,
    1.f / 4.f,  1.f / 3.f,  1.f / 2.f,  2.f / 3.f,  1.f,
};
inline constexpr int kDivCount = 11;

// The shortest delay the physics can hold: below it kMinStages forces a clock
// above kClockMaxHz. See "Corrections to the spec" in the plan -- the clamp is
// at the SHORT end, because f_lo is defined as kMinStages/(2T), which makes the
// stage count at f_lo exactly kMinStages for EVERY T. The long end cannot
// overflow.
inline constexpr float kMinDelayS =
    static_cast<float>(bbd_tuning::kMinStages) / (2.f * bbd_tuning::kClockMaxHz);

// LANE_SIZE is a continuously modulated lane, so a bare nearest-rung round
// chatters at every boundary. One rung of overlap: the held rung keeps the lane
// until it passes a NEIGHBOURING rung's centre.
class DivLadder {
public:
    int process(float lane) {
        const float x = clampf(lane, 0.f, 1.f) * (kDivCount - 1);
        if (x >= static_cast<float>(_i + 1)) _i = static_cast<int>(x);
        else if (x <= static_cast<float>(_i - 1)) _i = static_cast<int>(x + 0.999999f);
        if (_i < 0) _i = 0;
        if (_i > kDivCount - 1) _i = kDivCount - 1;
        return _i;
    }
    int  index() const { return _i; }
    void reset(int i) { _i = i < 0 ? 0 : (i > kDivCount - 1 ? kDivCount - 1 : i); }
private:
    int _i = 6;   // 1/4
};

// The reachable clock window for a delay time T, plus what had to give.
struct Window {
    float t_eff = kMinDelayS;
    float f_lo = bbd_tuning::kClockMaxHz;
    float f_hi = bbd_tuning::kClockMaxHz;
    bool  time_clamped = false;      // T was raised to kMinDelayS
    bool  scale_truncated = false;   // span < 3 octaves: STEP loses the top
};

inline Window window(float t_seconds) {
    Window w;
    w.time_clamped = !(t_seconds >= kMinDelayS);
    w.t_eff = w.time_clamped ? kMinDelayS : t_seconds;
    w.f_lo = (bbd_tuning::kMinStages * 0.5f) / w.t_eff;
    const float ceil_stages = (bbd_tuning::kMaxStages * 0.5f) / w.t_eff;
    w.f_hi = ceil_stages < bbd_tuning::kClockMaxHz ? ceil_stages
                                                   : bbd_tuning::kClockMaxHz;
    if (w.f_hi < w.f_lo) w.f_hi = w.f_lo;
    w.scale_truncated = w.f_hi < w.f_lo * 8.f;   // 8x == 36 semitones
    return w;
}

// LANE_PITCH in FLOW: geometric across the whole window, so the lane always
// spans its full travel and there is no dead zone at the top. The interval per
// lane step is NOT constant across divisions -- accepted, because this lane is
// a bend, not a keyboard.
inline float clock_flow(const Window& w, float lane) {
    const float n = clampf(lane, 0.f, 1.f);
    if (!(w.f_hi > w.f_lo)) return w.f_lo;
    return w.f_lo * std::pow(w.f_hi / w.f_lo, n);
}

// LANE_PITCH in STEP: the quantizer's normalized output carries 36 semitones
// (Quantizer::SPAN_SEMIS == 36, quantizer.h:66), so convert back to semitones
// and apply them as a ratio on the clock. Clamped to the window, which is what
// `scale_truncated` warns about.
inline float clock_step(const Window& w, float q_norm) {
    const float semis = clampf(q_norm, 0.f, 1.f) * 36.f;
    const float f = w.f_lo * std::pow(2.f, semis * (1.f / 12.f));
    return f > w.f_hi ? w.f_hi : f;
}

// delay = stages/(2 f_clk), so holding the delay at T means stages = 2 T f_clk.
// Note it does not involve fs: the reachable delay range is identical at 44.1
// and 192 kHz, unlike a buffer measured in samples.
inline int stages_for(const Window& w, float f_clk) {
    int s = static_cast<int>(2.f * w.t_eff * f_clk + 0.5f);
    if (s < bbd_tuning::kMinStages) s = bbd_tuning::kMinStages;
    if (s > bbd_tuning::kMaxStages) s = bbd_tuning::kMaxStages;
    return s;
}

}  // namespace bbd_music
}  // namespace spky
```

- [ ] **Step 4: Register the suite**

In `CMakeLists.txt`, next to `tests/test_bbd.cpp`, add `tests/test_bbd_music.cpp`. The header needs no source file.

- [ ] **Step 5: Run**

```bash
cmake --build build && ctest --test-dir build -R test_bbd_music --output-on-failure
```

Expected: PASS. If the hysteresis case fails, work `DivLadder::process` by hand against the test's numbers before changing the test — the deadband is deliberately one full rung wide in each direction.

- [ ] **Step 6: Prove the chatter test can fail**

Replace `DivLadder::process`'s body with a bare `_i = static_cast<int>(clampf(lane,0.f,1.f) * (kDivCount-1) + 0.5f); return _i;`, rebuild, run. Expected: the chatter case goes RED with a large `changes` count. Revert. Paste both outputs.

- [ ] **Step 7: Amend the spec**

In `docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md`, replace §5.4's final paragraph (the one beginning *"`div = 1` is a whole phrase"*) with:

```markdown
**The clamp is at the SHORT end, not the long one.** Under §5.3's decision the
long end is self-normalising: `f_lo` is *defined* as `kMinStages/(2T)`, so the
stage count at the lane's lowest clock is exactly `kMinStages` for every `T`,
and `div = 1` at 40 BPM cannot overflow. What does break is
`f_lo > kClockMaxHz`, which happens whenever `T < kMinStages/(2·kClockMaxHz)
= 8 ms` — reachable, because a free master lane at 30 Hz gives a 33 ms cycle
and `div = 1/32` then asks for 1.04 ms. `T` is raised to that floor, which
takes the repeats off the grid, and the observer reports it as `time_clamped`.
Corrected 2026-07-31 while working the arithmetic for the implementation plan;
`engine/parts/bbd_music.h` is the authority.
```

And in §9, replace *"both clamp flags (§5.3's reachable-range clamp and §5.4's long-division clamp)"* with *"the clamp flag `time_clamped` and the span-truncation flag `scale_truncated` (see §5.4 — there is one clamp, not two)"*.

- [ ] **Step 8: Full suite and commit**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
git add engine/parts/bbd_music.h tests/test_bbd_music.cpp CMakeLists.txt \
        docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md
git commit -F - <<'EOF'
feat(bbd): the musical layer, as arithmetic that can be asserted

The division ladder with one rung of hysteresis, the reachable clock window,
the geometric lane->clock map, the STEP semitone re-derivation against the
quantizer's own 36-semitone span, and the stage derivation that holds the
delay on the grid. No audio, so every claim in spec 5.2-5.5 about pitch, time
and range is a unit test.

Corrects the spec while doing it: the division clamp is at the SHORT end, not
the long one. f_lo is defined as kMinStages/(2T), so the stage count at the
lane's lowest clock is kMinStages for every T and a long division cannot
overflow. T < 8 ms can, and does -- a free master lane at 30 Hz with div 1/32
asks for 1.04 ms.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 4: `BbdEngine` exists, is reachable, and passes audio

The engine appears, `Part` can select it, and audio arrives from `process_in` and leaves through `process`. Lanes: SOURCE→DRIVE, PITCH→`f_clk`, SIZE→`div`, MOTION→FEEDBACK, LEVEL→MIX. Freeze, stereo spread and the VOICE row come in Tasks 6–8; this task is the spine.

Read **§5, §5.3 and §5.8's MIX paragraph** of the spec.

**Files:**
- Create: `engine/parts/bbd_engine.h`, `engine/parts/bbd_engine.cpp`
- Create: `tests/test_bbd_engine.cpp`
- Modify: `engine/parts/engine_iface.h` (`ENGINE_BBD = 5`), `engine/parts/part.h` (`_engine_for`, member, `init` signature), `engine/parts/part.cpp` (`init`), `engine/instrument.h` (`FxMem`), `engine/instrument.cpp` (`init`), `tests/test_part.cpp:279-287`, `CMakeLists.txt`

**Interfaces:**
- Consumes: `bbd_music::{window, clock_flow, stages_for, DivLadder}` (Task 3); `BbdEcho::{Reset, SetDither, SeedDither}` (Task 2).
- Produces:
  ```cpp
  class BbdEngine : public IPartEngine {
      void init(float sample_rate) override;
      void init_buffers(float* l, float* r, size_t cells);
      void set_targets(const float* targets, float tune) override;
      void trigger(float pitch_norm) override;
      void process(float& outL, float& outR) override;
      void process_in(float inL, float inR) override;
      bool consumes_input() const override { return true; }
      void set_cycle(float seconds) override;
      void reset();
      // observers
      float clock_hz() const;  int stages() const;  int div_index() const;
      bool  time_clamped() const;  bool scale_truncated() const;
  };
  ```
  and `Part::bbd()` / `Part::bbd() const`, `FxMem::bbd[PART_COUNT][2]`.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_bbd_engine.cpp`:

```cpp
#include "doctest.h"
#include "parts/part.h"
#include "parts/bbd_engine.h"
#include "fx/flux.h"
#include <cmath>

using namespace spky;

static float s_bbd_l[Flux::kMaxSamples];
static float s_bbd_r[Flux::kMaxSamples];

TEST_CASE("bbd engine: the id is appended, never renumbered") {
    CHECK(ENGINE_BBD == 5);
    CHECK(ENGINE_COUNT == 6);
}

TEST_CASE("bbd engine: a deck set to BBD reaches the BBD, not the test tone") {
    Part p;
    p.init(48000.f, 7u);
    p.set_engine(ENGINE_BBD);
    float l, r;
    for (int i = 0; i < 500; ++i) p.process(l, r);   // let the fade complete
    CHECK(p.engine_id() == ENGINE_BBD);
}

TEST_CASE("bbd engine: input reaches the output, and MIX decides how much") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
    e.set_cycle(1.0f);
    float t[LANE_COUNT] = { 0.f, 0.5f, 0.5f, 0.f, 0.f };   // SRC SIZE PITCH MOT LEVEL
    auto rms = [&](float mix) {
        t[LANE_LEVEL] = mix;
        e.set_targets(t, 0.5f);
        float s = 0.f;
        for (int i = 0; i < 48000; ++i) {
            float l, r;
            e.process_in(std::sin(i * 0.05f), std::sin(i * 0.05f));
            e.process(l, r);
            if (i > 24000) s += l * l;
        }
        return std::sqrt(s / 24000.f);
    };
    const float wet = rms(1.f);
    e.reset();
    const float dry = rms(0.f);
    CHECK(dry > 1e-3f);                 // MIX 0 is the dry input, not silence
    CHECK(wet > 1e-3f);                 // MIX 1 is the delayed signal
    CHECK(std::fabs(wet - dry) > 1e-4f);   // and they are not the same thing
}

TEST_CASE("bbd engine: the delay time follows LANE_SIZE and lands on the grid") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
    e.set_cycle(2.0f);                  // a 2 s phrase
    float t[LANE_COUNT] = { 0.f, 0.f, 0.5f, 0.f, 1.f };
    // SIZE 1 -> div 1 -> T = 2 s; SIZE at the 1/8 rung -> T = 250 ms.
    t[LANE_SIZE] = 1.f;
    e.set_targets(t, 0.5f);
    const float long_delay = e.stages() / (2.f * e.clock_hz());
    CHECK(long_delay == doctest::Approx(2.0f).epsilon(0.01));
    t[LANE_SIZE] = 4.f / 10.f;          // rung index 4 == 1/8
    e.set_targets(t, 0.5f);
    CHECK(e.div_index() == 4);
    const float short_delay = e.stages() / (2.f * e.clock_hz());
    CHECK(short_delay == doctest::Approx(0.25f).epsilon(0.01));
}

TEST_CASE("bbd engine: LANE_PITCH moves the clock and leaves the delay alone") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
    e.set_cycle(1.0f);
    float t[LANE_COUNT] = { 0.f, 1.f, 0.f, 0.f, 1.f };
    e.set_targets(t, 0.5f);
    const float f_lo = e.clock_hz();
    const float d_lo = e.stages() / (2.f * f_lo);
    t[LANE_PITCH] = 1.f;
    e.set_targets(t, 0.5f);
    const float f_hi = e.clock_hz();
    const float d_hi = e.stages() / (2.f * f_hi);
    CHECK(f_hi > f_lo * 4.f);                       // the clock really moved
    CHECK(d_hi == doctest::Approx(d_lo).epsilon(0.01));   // the delay did not
}

TEST_CASE("bbd engine: the output stays inside its stated bound") {
    // The expander's 4x ceiling puts the raw self-oscillating return at roughly
    // +8..+11 dBFS. There is no per-deck limiter and the reverb send taps
    // before the master one, so the engine bounds itself.
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
    e.set_cycle(0.5f);
    float t[LANE_COUNT] = { 1.f, 0.5f, 0.5f, 1.f, 1.f };   // DRIVE 1, FEEDBACK 1
    e.set_targets(t, 0.5f);
    float peak = 0.f;
    for (int i = 0; i < 48000 * 20; ++i) {
        float l, r;
        e.process_in(std::sin(i * 0.03f), std::sin(i * 0.07f));
        e.process(l, r);
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        CHECK(std::isfinite(l));
        CHECK(std::isfinite(r));
    }
    CHECK(peak <= 1.f);
}

TEST_CASE("bbd engine: switching away and back returns silence, not old charge") {
    Part p;
    p.init(48000.f, 11u, nullptr, nullptr, 0, s_bbd_l, s_bbd_r);
    p.set_engine(ENGINE_BBD);
    p.set_target_base(LANE_MOTION, 0.9f);    // FEEDBACK up: charge circulates
    float l, r;
    for (int i = 0; i < 48000; ++i) p.process(std::sin(i * 0.05f), std::sin(i * 0.05f), l, r);
    p.set_engine(ENGINE_SYNTH);
    for (int i = 0; i < 2000; ++i) p.process(l, r);
    p.set_engine(ENGINE_BBD);
    for (int i = 0; i < 500; ++i) p.process(l, r);   // complete the fade
    float peak = 0.f;
    for (int i = 0; i < 24000; ++i) {
        p.process(l, r);
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
    }
    CHECK(peak < 1e-4f);
}
```

- [ ] **Step 2: Run and watch it fail**

```bash
source env.sh && cmake --build build 2>&1 | head -20
```

Expected: `parts/bbd_engine.h: No such file or directory`.

- [ ] **Step 3: Append the enum value**

`engine/parts/engine_iface.h`:

```cpp
    ENGINE_BODY = 4,
    // The bucket-brigade delay (spec 2026-07-31 bbd-part-engine). Voiceless
    // and input-consuming, so it is the second engine after the sampler to
    // override the process_in/consumes_input pair.
    ENGINE_BBD = 5,
    ENGINE_COUNT
```

Extend the runtime pin at `tests/test_part.cpp:279-287` with `CHECK(ENGINE_BBD == 5);` and rename the case to *"engine ids stay patch-stable when BBD is appended"*.

- [ ] **Step 4: Write the engine header**

Create `engine/parts/bbd_engine.h`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include "fx/bbd.h"
#include "parts/bbd_music.h"
#include "parts/engine_iface.h"
#include "util/fast_tanh.h"

namespace spky {

// The bucket-brigade delay as a part engine. Two BbdEcho, one per channel: a
// part engine has no dry path -- it IS the signal path -- and its input is
// stereo at every boundary.
//
// MIX is on LANE_LEVEL, not on a knob. Instrument::process composes
// l = al*ga + bl*gb, so the audio input reaches the output nowhere else: on a
// wet/dry engine the mix IS the level, and putting it there lets the plane open
// and close the echo rhythmically.
class BbdEngine : public IPartEngine {
public:
    // Cells per line. Same sizing as Flux's, which is kMaxStages/2 -- a
    // two-phase BBD stores one sample per TWO stages.
    static constexpr size_t kCells = bbd_tuning::kMaxStages / 2;

    void init(float sample_rate) override;
    // The host owns the memory, as it does for Flux. Two lines per deck at
    // kCells floats = 64 KB, in SDRAM on the Seed. nullptr -> the deck is
    // silent, which is what a host that forgot to allocate deserves.
    void init_buffers(float* l, float* r, size_t cells);

    void set_targets(const float* targets, float tune) override;
    void trigger(float /*pitch_norm*/) override {}
    void process(float& outL, float& outR) override;
    void process_in(float inL, float inR) override;
    bool consumes_input() const override { return true; }
    void set_cycle(float seconds) override;

    // Clears both lines, both companders and both feedback states. Part has no
    // swap-away notification, so this is called on activation instead.
    void reset();

    // Observers (spec 9): a clamp that is invisible reads as a broken knob.
    float clock_hz() const { return _f_clk; }
    int   stages() const { return _stages; }
    int   div_index() const { return _ladder.index(); }
    bool  time_clamped() const { return _win.time_clamped; }
    bool  scale_truncated() const { return _win.scale_truncated; }

private:
    void _recompute();

    BbdEcho _l, _r;
    bbd_music::DivLadder _ladder;
    bbd_music::Window _win;
    float _sr = 48000.f;
    float _cycle = 1.f;
    float _in_l = 0.f, _in_r = 0.f;
    float _mix = 0.f;
    float _pitch = 0.5f;
    float _f_clk = 4000.f;
    int   _stages = 8192;
    bool  _buf_ok = false;
};

}  // namespace spky
```

- [ ] **Step 5: Write the engine source**

Create `engine/parts/bbd_engine.cpp`:

```cpp
#include "parts/bbd_engine.h"

namespace spky {

namespace {
// The dither floor. A few LSBs, injected at the write tick -- see
// BbdLine::SetDither for why the engine needs a noise floor the model does not
// otherwise have.
constexpr float kDither = 4e-5f;
}  // namespace

void BbdEngine::init(float sample_rate) {
    _sr = sample_rate > 0.f ? sample_rate : 48000.f;
    _l.Init(_sr, nullptr, 0);
    _r.Init(_sr, nullptr, 0);
    _buf_ok = false;
    _win = bbd_music::window(_cycle);
    _recompute();
}

void BbdEngine::init_buffers(float* l, float* r, size_t cells) {
    _buf_ok = (l != nullptr && r != nullptr && cells > 0);
    _l.Init(_sr, l, cells);
    _r.Init(_sr, r, cells);
    // Two seeds, or both lines dither identically and COLOR 0's bit-identity
    // test would pass for the wrong reason.
    _l.SeedDither(0x5bd1e995u);
    _r.SeedDither(0x27d4eb2fu);
    _l.SetDither(kDither);
    _r.SetDither(kDither);
    _recompute();
}

void BbdEngine::reset() {
    _l.Reset();
    _r.Reset();
    _in_l = 0.f;
    _in_r = 0.f;
}

void BbdEngine::set_cycle(float seconds) {
    _cycle = seconds > 0.f ? seconds : 1.f;
    _recompute();
}

void BbdEngine::set_targets(const float* t, float /*tune*/) {
    // SOURCE -> DRIVE, the dirt inside the loop. Both setters carry unchanged-
    // value guards inside BbdEcho's callee; putting DRIVE on a lane defeats
    // them permanently, which is a handful of transcendentals per block.
    const float drive = clampf(t[LANE_SOURCE], 0.f, 1.f);
    _l.SetDrive(drive);
    _r.SetDrive(drive);
    // MOTION -> FEEDBACK. Flux's law, kept: without dividing bbd_drive_gain
    // back out the bloom point slides from 0.57 to 0.14 across DRIVE, and since
    // LANE_SOURCE *is* DRIVE the plane would drive the loop through
    // self-oscillation via a lane that is not the feedback lane.
    const float fb = clampf(t[LANE_MOTION], 0.f, 1.f) * 1.2f
                     / bbd_drive_gain(drive);
    _l.SetFeedback(fb);
    _r.SetFeedback(fb);

    _mix = clampf(t[LANE_LEVEL], 0.f, 1.f);
    _pitch = clampf(t[LANE_PITCH], 0.f, 1.f);
    _ladder.process(clampf(t[LANE_SIZE], 0.f, 1.f));
    _recompute();
}

void BbdEngine::_recompute() {
    const float T = _cycle * bbd_music::kDivs[_ladder.index()];
    _win = bbd_music::window(T);
    _f_clk = bbd_music::clock_flow(_win, _pitch);
    _stages = bbd_music::stages_for(_win, _f_clk);
    _l.SetStages(_stages);
    _r.SetStages(_stages);
}

void BbdEngine::process_in(float inL, float inR) {
    _in_l = inL;
    _in_r = inR;
}

void BbdEngine::process(float& outL, float& outR) {
    if (!_buf_ok) { outL = 0.f; outR = 0.f; return; }
    const float wl = _l.Process(_in_l, _f_clk);
    const float wr = _r.Process(_in_r, _f_clk);
    // The engine's stated bound. The expander's 4x ceiling puts the raw return
    // at roughly +8..+11 dBFS in the self-oscillating regime, there is no
    // per-deck limiter, and the reverb send taps BEFORE the master one. Same
    // idiom as part.h:350 and part.cpp:385.
    outL = fast_tanh(_in_l + _mix * (wl - _in_l));
    outR = fast_tanh(_in_r + _mix * (wr - _in_r));
}

}  // namespace spky
```

- [ ] **Step 6: Wire it into `Part`**

`engine/parts/part.h`: add `#include "parts/bbd_engine.h"`, extend `init`:

```cpp
    void init(float sample_rate, uint32_t seed_base,
              float* echo = nullptr,
              SampleBuffer::Frame* sampler_mem = nullptr, size_t sampler_frames = 0,
              float* bbd_l = nullptr, float* bbd_r = nullptr);
```

add the member `BbdEngine _bbd;` next to `_body`, the accessor pair

```cpp
    BbdEngine& bbd() { return _bbd; }
    const BbdEngine& bbd() const { return _bbd; }
```

and the `_engine_for` arm — **without which the deck silently plays the test tone**:

```cpp
            case ENGINE_BODY:    return static_cast<IPartEngine*>(&_body);
            case ENGINE_BBD:     return static_cast<IPartEngine*>(&_bbd);
```

`engine/parts/part.cpp`, in `Part::init`, alongside the other engine inits:

```cpp
    _bbd.init(sample_rate);
    _bbd.init_buffers(bbd_l, bbd_r, BbdEngine::kCells);
```

and in `Part::_engine_swap`, before the state re-forwarding:

```cpp
    // The BBD holds charge, and IPartEngine has no swap-away notification --
    // Part only ever pushes state INTO the engine being swapped in. Without
    // this a deck switched away from and back to returns the previous take.
    if (_engine_id == ENGINE_BBD) _bbd.reset();
```

- [ ] **Step 7: Wire the buffers through `Instrument`**

`engine/instrument.h`, in `struct FxMem`:

```cpp
    // The BBD part engine's two lines per deck (spec 2026-07-31 §5.7). Sized
    // BbdEngine::kCells floats each = 32 KB per line, 128 KB for the
    // instrument. SDRAM on the Seed, static or heap on the desktop.
    // nullptr -> that deck's BBD engine runs silent.
    float* bbd[PART_COUNT][2] = { { nullptr, nullptr }, { nullptr, nullptr } };
```

`engine/instrument.cpp`, in `init`:

```cpp
    _parts[PART_A].init(sample_rate, 0x1234abcdu,
                        mem.echo[PART_A],
                        mem.sampler_buf[PART_A], mem.sampler_frames,
                        mem.bbd[PART_A][0], mem.bbd[PART_A][1]);
    _parts[PART_B].init(sample_rate, 0x9e3779b9u,
                        mem.echo[PART_B],
                        mem.sampler_buf[PART_B], mem.sampler_frames,
                        mem.bbd[PART_B][0], mem.bbd[PART_B][1]);
```

Then allocate in all four hosts, mirroring each one's existing `echo` allocation exactly:
`host/render/main.cpp:13`, `host/vcv/src/Spotymod.cpp:174`, `bench/mem.cpp:37` (`DSY_SDRAM_BSS`), `bench/audition/memory.cpp:17`. Grep for `Flux::kMaxSamples` to find every site and give each one a `bbd` twin.

- [ ] **Step 8: Register and build**

Add `engine/parts/bbd_engine.cpp` and `tests/test_bbd_engine.cpp` to `CMakeLists.txt`, next to `engine/parts/part.cpp` / `tests/test_part.cpp`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build -R "test_bbd_engine|test_part" --output-on-failure
```

Expected: PASS.

- [ ] **Step 9: Prove the `_engine_for` arm can fail**

Delete the `case ENGINE_BBD:` line, rebuild, run. Expected: the "reaches the BBD, not the test tone" case and the MIX case both go RED. Restore. Paste both outputs — `default:` routing to the test tone is exactly the silent failure this task's constraints name.

- [ ] **Step 10: Full suite and commit**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
git add engine/parts/bbd_engine.h engine/parts/bbd_engine.cpp engine/parts/engine_iface.h \
        engine/parts/part.h engine/parts/part.cpp engine/instrument.h engine/instrument.cpp \
        host/render/main.cpp host/vcv/src/Spotymod.cpp bench/mem.cpp bench/audition/memory.cpp \
        tests/test_bbd_engine.cpp tests/test_part.cpp CMakeLists.txt
git commit -F - <<'EOF'
feat(bbd): ENGINE_BBD = 5 exists, is reachable, and passes audio

Two BbdEcho behind an IPartEngine, one per channel, fed from process_in and
bounded on the way out. SOURCE -> DRIVE, PITCH -> f_clk, SIZE -> div,
MOTION -> FEEDBACK, LEVEL -> MIX: a part engine has no dry path, so on a
wet/dry engine the mix IS the level.

Reset on activation, because IPartEngine has no swap-away notification and a
line holds charge. Buffers come from FxMem like Flux's, two lines per deck.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 5: The grid and the fires

`LANE_PITCH` means something different in each mode: quantized to the scale in STEP, free in FLOW. A step fire latches the clock; a FLOW cycle boundary must not. And the documented gating — PITCH is inaudible at FEEDBACK 0 — becomes an assertion rather than a surprise.

Read **§5.5** of the spec.

**Files:**
- Modify: `engine/parts/part.cpp:223` (the `_pitch_q` rule), `engine/parts/bbd_engine.h/.cpp` (`set_flow`, the latch)
- Test: `tests/test_bbd_engine.cpp`

**Interfaces:**
- Consumes: `bbd_music::clock_step` (Task 3), `BbdEngine` (Task 4).
- Produces: `BbdEngine::set_flow(bool)`, `BbdEngine::latch_clock()`, `BbdEngine::flow() const`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_bbd_engine.cpp`:

```cpp
TEST_CASE("bbd engine: FLOW is free, STEP is on the scale grid") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
    e.set_cycle(1.0f);
    float t[LANE_COUNT] = { 0.f, 1.f, 0.5f, 0.f, 1.f };
    e.set_flow(true);
    e.set_targets(t, 0.5f);
    const float free_hz = e.clock_hz();
    e.set_flow(false);
    e.latch_clock();
    e.set_targets(t, 0.5f);
    e.latch_clock();
    const float step_hz = e.clock_hz();
    // FLOW spreads 0..1 over the whole window (up to five octaves); STEP maps
    // the quantizer's 36 semitones onto three. At lane 0.5 they cannot agree.
    CHECK(free_hz != doctest::Approx(step_hz));
    CHECK(step_hz == doctest::Approx(
        bbd_music::clock_step(bbd_music::window(1.0f), 0.5f)));
}

TEST_CASE("bbd engine: in FLOW a cycle boundary does not latch the clock") {
    // lane.cpp:447-452: "FLOW has no per-step gate so it always fires." A FLOW
    // deck fires once per master-lane cycle; un-gated, the latch would freeze
    // the clock at the top of every cycle and the continuous bend FLOW exists
    // for would not happen.
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
    e.set_cycle(1.0f);
    e.set_flow(true);
    float t[LANE_COUNT] = { 0.f, 1.f, 0.2f, 0.f, 1.f };
    e.set_targets(t, 0.5f);
    e.latch_clock();                       // the fire arrives and is ignored
    const float before = e.clock_hz();
    t[LANE_PITCH] = 0.8f;
    e.set_targets(t, 0.5f);                // the plane keeps moving
    CHECK(e.clock_hz() != doctest::Approx(before));
}

TEST_CASE("bbd engine: in STEP the clock holds between fires") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
    e.set_cycle(1.0f);
    e.set_flow(false);
    float t[LANE_COUNT] = { 0.f, 1.f, 0.2f, 0.f, 1.f };
    e.set_targets(t, 0.5f);
    e.latch_clock();
    const float latched = e.clock_hz();
    t[LANE_PITCH] = 0.9f;
    e.set_targets(t, 0.5f);                // plane moves, no fire
    CHECK(e.clock_hz() == doctest::Approx(latched));
    e.latch_clock();                       // now it fires
    CHECK(e.clock_hz() != doctest::Approx(latched));
}

TEST_CASE("bbd engine: PITCH is inaudible at FEEDBACK 0, and that is the design") {
    // The wet output at FEEDBACK 0 is the first pass only, which is always at
    // unity pitch: a BBD writes and reads at the same clock. MOTION is the
    // switch that turns PITCH on. Asserted rather than discovered.
    auto tail = [](float motion, float pitch_a, float pitch_b, float* out, int n) {
        BbdEngine e;
        e.init(48000.f);
        e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
        e.set_cycle(0.5f);
        e.set_flow(true);
        float t[LANE_COUNT] = { 0.f, 1.f, pitch_a, motion, 1.f };
        e.set_targets(t, 0.5f);
        for (int i = 0; i < 24000; ++i) {       // fill the line
            float l, r;
            e.process_in(std::sin(i * 0.06f), std::sin(i * 0.06f));
            e.process(l, r);
        }
        t[LANE_PITCH] = pitch_b;
        e.set_targets(t, 0.5f);                 // bend, input now silent
        for (int i = 0; i < n; ++i) {
            float l, r;
            e.process_in(0.f, 0.f);
            e.process(l, r);
            out[i] = l;
        }
    };
    static float with_fb[48000], without_fb[48000];
    tail(0.9f, 0.3f, 0.7f, with_fb, 48000);
    tail(0.0f, 0.3f, 0.7f, without_fb, 48000);
    auto energy = [](const float* x, int n) {
        float s = 0.f;
        for (int i = n / 2; i < n; ++i) s += x[i] * x[i];
        return s;
    };
    // With feedback there is still a bent tail long after the input stopped;
    // without it, the first pass has run out and there is nothing to bend.
    CHECK(energy(with_fb, 48000) > 100.f * energy(without_fb, 48000));
}

TEST_CASE("bbd engine: a BBD deck in FLOW gets the raw pitch, in STEP the scale") {
    Part p;
    p.init(48000.f, 13u, nullptr, nullptr, 0, s_bbd_l, s_bbd_r);
    p.set_engine(ENGINE_BBD);
    float l, r;
    for (int i = 0; i < 500; ++i) p.process(l, r);
    p.set_target_base(LANE_PITCH, 0.37f);
    p.set_target_active(LANE_PITCH, false);   // base + TUNE only
    p.set_tune(0.5f);
    p.set_step(false, 8);                     // FLOW
    for (int i = 0; i < 200; ++i) p.process(l, r);
    const float flow_v = p.target_value(LANE_PITCH);
    p.set_step(true, 8);                      // STEP
    for (int i = 0; i < 200; ++i) p.process(l, r);
    const float step_v = p.target_value(LANE_PITCH);
    // FLOW hands the engine the unquantized value; STEP hands it the scale.
    CHECK(flow_v == doctest::Approx(p.pitch_pre_quant()).epsilon(0.001));
    CHECK(step_v != doctest::Approx(flow_v));
}
```

- [ ] **Step 2: Run and watch them fail**

Expected: `set_flow` is not declared as an override on `BbdEngine`, `latch_clock` does not exist, and the Part-level case fails because `_pitch_q` still quantizes on every non-sampler engine.

- [ ] **Step 3: Add the mode and the latch to `BbdEngine`**

Header — two members and three methods:

```cpp
    void set_flow(bool flow) override { _flow = flow; if (_flow) _recompute(); }
    bool flow() const { return _flow; }
    // A step fire latches the clock and holds it until the next one -- the
    // pattern SynthEngine already uses for pitch. In FLOW the engine ignores
    // fires and follows the plane continuously: lane.cpp:447-452 makes a FLOW
    // deck fire once per master-lane cycle un-gated, so latching there would
    // freeze the clock at the top of every cycle.
    void latch_clock() { if (!_flow) { _latched = true; _recompute(); } }
```

with `bool _flow = false;` and `bool _latched = false;`.

`_recompute()` picks the map by mode, and in STEP only re-derives on a latch:

```cpp
void BbdEngine::_recompute() {
    const float T = _cycle * bbd_music::kDivs[_ladder.index()];
    _win = bbd_music::window(T);
    if (_flow) {
        _f_clk = bbd_music::clock_flow(_win, _pitch);
    } else if (_latched) {
        _f_clk = bbd_music::clock_step(_win, _pitch);
        _latched = false;
    }
    // Either way the stage count follows, so SIZE keeps moving the rhythm
    // between fires while the clock -- and therefore the pitch -- holds.
    _stages = bbd_music::stages_for(_win, _f_clk);
    _l.SetStages(_stages);
    _r.SetStages(_stages);
}
```

- [ ] **Step 4: Add the grid rule to `Part`**

`engine/parts/part.cpp:223`:

```cpp
    // The SAMPLER does not quantize (see the comment above). The BBD does not
    // either, but only in FLOW: STEP puts the clock on scale steps so the bend
    // is in the key, and FLOW leaves it continuous, which is the gesture FLOW
    // exists for.
    _pitch_q = (_engine_id == ENGINE_SAMPLER ||
                (_engine_id == ENGINE_BBD && !_step_on)) ? pitch_raw
                                                         : pitch_quantized;
```

- [ ] **Step 5: Route the fire**

`Part::_fire_trigger` (`part.cpp:431`) already runs on every PITCH fire that is not inhibited. Add, before the chord build:

```cpp
    if (_engine_id == ENGINE_BBD) _bbd.latch_clock();
```

`latch_clock` is itself a no-op in FLOW, so the mode rule lives in one place.

- [ ] **Step 6: Run**

```bash
cmake --build build && ctest --test-dir build -R test_bbd_engine --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Prove the FLOW-latch guard can fail**

Change `latch_clock`'s body to `{ _latched = true; _recompute(); }` — dropping the `!_flow` guard — rebuild, run. Expected: "in FLOW a cycle boundary does not latch the clock" goes RED. Revert. Paste both outputs.

- [ ] **Step 8: Full suite and commit**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
git add engine/parts/bbd_engine.h engine/parts/bbd_engine.cpp engine/parts/part.cpp \
        tests/test_bbd_engine.cpp
git commit -F - <<'EOF'
feat(bbd): the lane sets the clock, and a fire latches it

STEP quantizes the clock onto scale steps, re-deriving semitones against the
quantizer's own 36-semitone span rather than against the engine's five-octave
one -- otherwise STEP snaps to a grid that is not a scale. FLOW leaves it
continuous and ignores fires: a FLOW deck fires once per master-lane cycle
un-gated, so a latch there would freeze the clock at every cycle top.

Also asserts the documented gating: at FEEDBACK 0 the wet output is the first
pass, which is always at unity pitch, so PITCH is inaudible. That is a
property of a bucket brigade, not a bug, and it belongs in the manual.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 6: The freeze

Input muted, loop gain held, content circulating and staying audible. `SetClock(0)` would be free and is already implemented, but it stops the READ ticks too — that is a pause in the signal, not a freeze of it.

Read **§5.6** of the spec in full, including the three measured facts that killed the previous scheme. `k₀` is measured in this task, not guessed.

**Files:**
- Modify: `engine/parts/bbd_engine.h/.cpp`
- Test: `tests/test_bbd_engine.cpp`

**Interfaces:**
- Consumes: `BbdEcho::{SetFeedbackTilt, SetFeedbackDcBlock}` (Task 2).
- Produces: `BbdEngine::{set_gate, set_hold, set_attack, set_decay, frozen}`; the constant `kFreezeGain` (`k₀`).

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("bbd engine: the freeze holds a broadband burst per octave") {
    // Rev. 1's scalar-k scheme measured the one frequency it tuned. The
    // compander's round trip is L^2 above about -40 dBFS and L below it, and L
    // is a lowpass -- at 8192 stages with k tuned for 1 kHz, ten circulations
    // left 110 Hz at +7.2 dB and 2.5 kHz at -48.8 dB. So: broadband material,
    // per-octave criterion, DRIVE swept during the hold.
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
    e.set_cycle(1.0f);
    e.set_flow(false);
    e.set_decay(1.f);                      // DECAY max: no trim below k0
    float t[LANE_COUNT] = { 0.f, 8.f / 10.f, 0.5f, 0.5f, 1.f };  // div 1/2 -> T=500ms
    e.set_targets(t, 0.5f);
    e.latch_clock();

    // A noise burst in, one delay period long.
    Rng rng; rng.seed(0xfeedu);
    const int period = static_cast<int>(0.5f * 48000.f);
    for (int i = 0; i < period; ++i) {
        float l, r;
        const float n = rng.next_bipolar() * 0.3f;
        e.process_in(n, n);
        e.process(l, r);
    }
    e.set_gate(true);                       // freeze engages
    CHECK(e.frozen());

    // Six one-octave probes at 110, 220, 440, 880, 1760, 3520 Hz. SvfBp<N>
    // SUMS its N bands, so use six SvfBp<1> and read them one at a time.
    constexpr float kProbeHz[6] = { 110.f, 220.f, 440.f, 880.f, 1760.f, 3520.f };
    SvfBp<1> probe[6];
    auto arm_probes = [&] {
        for (int b = 0; b < 6; ++b) {
            probe[b].reset();
            const float g = std::tan(3.14159265f * kProbeHz[b] / 48000.f);
            const float q = 2.f;                       // ~half an octave wide
            probe[b].set_coeffs(0, g, 1.f / q + g, 1.f / (1.f + g / q + g * g));
        }
    };

    // Band energy over the LAST circulation of a `circulations`-long hold, with
    // DRIVE swept 0 -> 1 across the whole run -- the term k = k0/bbd_drive_gain
    // exists precisely to keep that sweep from moving the loop gain.
    auto octaves = [&](int circulations, float* bands) {
        for (int b = 0; b < 6; ++b) bands[b] = 0.f;
        arm_probes();
        const int total = circulations * period;
        const float one = 1.f;
        for (int i = 0; i < total; ++i) {
            t[LANE_SOURCE] = static_cast<float>(i) / static_cast<float>(total);
            if ((i % 96) == 0) e.set_targets(t, 0.5f);   // control raster
            float l, r;
            e.process_in(0.f, 0.f);
            e.process(l, r);
            if (i >= total - period)
                for (int b = 0; b < 6; ++b) {
                    const float y = probe[b].process(&one, l);
                    bands[b] += y * y;
                }
            else
                for (int b = 0; b < 6; ++b) probe[b].process(&one, l);
        }
    };
    float a[6], b[6];
    octaves(1, a);
    // The engine is NOT re-primed between the two calls: the second run
    // continues the same frozen loop, so `b` really is ten circulations later
    // than `a` rather than a second first circulation.
    octaves(9, b);
    for (int i = 0; i < 6; ++i) {
        const float db = 20.f * std::log10((b[i] + 1e-12f) / (a[i] + 1e-12f));
        CAPTURE(i);
        CAPTURE(db);
        CHECK(std::fabs(db) <= 1.0f);
    }
}

TEST_CASE("bbd engine: a frozen loop shows no DC growth over 60 s") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
    e.set_cycle(1.0f);
    e.set_flow(false);
    e.set_decay(1.f);
    float t[LANE_COUNT] = { 0.f, 8.f / 10.f, 0.5f, 0.5f, 1.f };
    e.set_targets(t, 0.5f);
    e.latch_clock();
    for (int i = 0; i < 24000; ++i) {
        float l, r;
        e.process_in(0.4f, 0.4f);           // a deliberate DC offset
        e.process(l, r);
    }
    e.set_gate(true);
    double mean = 0.0;
    const int n = 48000 * 60;
    float peak = 0.f;
    for (int i = 0; i < n; ++i) {
        float l, r;
        e.process_in(0.f, 0.f);
        e.process(l, r);
        mean += l;
        if (i > n - 48000) peak = std::max(peak, std::fabs(l));
    }
    CHECK(std::fabs(mean / n) < 1e-3);
    CHECK(peak < 1.f);                      // it has not parked the saturator
}

TEST_CASE("bbd engine: DECAY trims below k0, ATTACK sets the ramp") {
    auto tail_after = [](float decay, float seconds) {
        BbdEngine e;
        e.init(48000.f);
        e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
        e.set_cycle(1.0f);
        e.set_flow(false);
        e.set_attack(0.f);
        e.set_decay(decay);
        float t[LANE_COUNT] = { 0.f, 8.f / 10.f, 0.5f, 0.5f, 1.f };
        e.set_targets(t, 0.5f);
        e.latch_clock();
        for (int i = 0; i < 24000; ++i) {
            float l, r;
            e.process_in(std::sin(i * 0.05f) * 0.5f, 0.f);
            e.process(l, r);
        }
        e.set_gate(true);
        float s = 0.f;
        const int n = static_cast<int>(seconds * 48000.f);
        for (int i = 0; i < n; ++i) {
            float l, r;
            e.process_in(0.f, 0.f);
            e.process(l, r);
            if (i > n - 24000) s += l * l;
        }
        return s;
    };
    CHECK(tail_after(0.2f, 8.f) < 0.05f * tail_after(1.f, 8.f));
}

TEST_CASE("bbd engine: FLOW ignores the gate, so the freeze is unreachable there") {
    // A FLOW deck's gate is effectively always on. Without this rule a FLOW BBD
    // would be permanently frozen. Consequence: ATTACK and DECAY are inert in
    // FLOW -- a mode-dependent dead knob, accepted, and it belongs in the manual.
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
    e.set_flow(true);
    e.set_gate(true);
    CHECK(!e.frozen());
}

TEST_CASE("bbd engine: CHOKE closes the input and lets the tail run out") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
    e.set_cycle(0.5f);
    e.set_flow(true);
    float t[LANE_COUNT] = { 0.f, 1.f, 0.5f, 0.6f, 1.f };
    e.set_targets(t, 0.5f);
    for (int i = 0; i < 24000; ++i) {
        float l, r;
        e.process_in(std::sin(i * 0.05f), 0.f);
        e.process(l, r);
    }
    e.set_hold(true);
    float first = 0.f, later = 0.f;
    for (int i = 0; i < 48000; ++i) {
        float l, r;
        e.process_in(std::sin(i * 0.05f), 0.f);   // still arriving, ignored
        e.process(l, r);
        if (i < 4800) first = std::max(first, std::fabs(l));
        if (i > 43200) later = std::max(later, std::fabs(l));
    }
    CHECK(later < 0.5f * first);    // it ran out
    CHECK(first > 1e-3f);           // and it was not cut dead
}
```

The test needs `#include "util/svf_bp.h"` and `#include "mod/rng.h"`. `SvfBp<N>::process` takes a gain array and sums all `N` bands, which is why the probes are six separate `SvfBp<1>` rather than one `SvfBp<6>`.

- [ ] **Step 2: Run and watch them fail**

Expected: `set_gate`, `set_hold`, `set_attack`, `set_decay`, `frozen` are not members.

- [ ] **Step 3: Implement the freeze**

Header additions:

```cpp
    void set_gate(bool on) override;
    void set_hold(bool on) override;
    void set_attack(float n);
    void set_decay(float n);
    bool frozen() const { return _freeze > 0.5f; }
```

Source. The loop gain during a freeze is one tuning constant, DRIVE divided out analytically:

```cpp
namespace {
// k0: the freeze's unity reference, measured with broadband material against
// the per-octave criterion in tests/test_bbd_engine.cpp. DECAY trims BELOW it;
// the acceptance test runs at DECAY maximum.
constexpr float kFreezeGain = 1.0f;   // <- replaced by the measured value, Step 5
// ATTACK's endpoints: how long the freeze takes to engage and release.
constexpr float kFreezeRampMinS = 0.002f;
constexpr float kFreezeRampMaxS = 2.0f;
}

void BbdEngine::set_gate(bool on) {
    // STEP only. A FLOW deck's gate is effectively always on, so honouring it
    // there would leave a FLOW BBD permanently frozen.
    _freeze_want = (!_flow && on) ? 1.f : 0.f;
}

void BbdEngine::set_hold(bool on) { _choked = on; }

void BbdEngine::set_attack(float n) {
    const float s = kFreezeRampMinS
                    * std::pow(kFreezeRampMaxS / kFreezeRampMinS,
                               clampf(n, 0.f, 1.f));
    _freeze_ramp_s = s;              // observer; Task 8's dead-knob test reads it
    _freeze_coef = 1.f / (s * _sr);
}

void BbdEngine::set_decay(float n) { _decay = clampf(n, 0.f, 1.f); }
```

and in `process`, before the two `Process` calls:

```cpp
    // The freeze ramp. Linear in the crossfade, not in dB: this is a mix
    // between "input open, feedback at the lane" and "input closed, feedback at
    // k0", and both endpoints are already the right shape.
    _freeze += (_freeze_want - _freeze) * _freeze_coef;
    if (_freeze > 1.f) _freeze = 1.f;
    if (_freeze < 1e-9f) _freeze = 0.f;
    if (_freeze != _freeze_last) {
        _freeze_last = _freeze;
        _apply_freeze();
    }
    const float gate_in = (_choked ? 0.f : 1.f) * (1.f - _freeze) * _in_gain;
    const float xl = _in_l * gate_in;
    const float xr = _in_r * gate_in;
```

with `_apply_freeze()` doing the control-rate work:

```cpp
void BbdEngine::_apply_freeze() {
    // The three legs of spec 5.6, all of which the rev. 1 scalar scheme lacked:
    //
    // 1. A DC blocker in the feedback path. The Butterworth sections are
    //    normalised H(0)=1 and the loss pole is unity at DC, so any loop gain
    //    above unity at 1 kHz is strictly above unity at DC and grows until it
    //    parks the saturator.
    // 2. The feedback-path tilt, tracking f_clk/4 -- the same filter RESONANCE
    //    plays. The freeze is RESONANCE at its neutral point, not a separate
    //    mechanism. Flattening the line's gain to ~1 is also what makes the
    //    compander's L^2 round trip harmless.
    // 3. DRIVE divided out analytically. bbd_drive_gain spans 1.0..3.98 and the
    //    small-signal loop gain IS feedback * g, so with LANE_SOURCE running,
    //    the plane would otherwise swing the loop gain +-12 dB PER CIRCULATION.
    //    This term is exactly known; leaving it to the ear was wrong.
    const bool on = _freeze > 0.f;
    _l.SetFeedbackDcBlock(on);
    _r.SetFeedbackDcBlock(on);
    const float tilt = _res_tilt + _freeze * (kFreezeTilt - _res_tilt);
    _l.SetFeedbackTilt(tilt, _f_clk * 0.25f);
    _r.SetFeedbackTilt(tilt, _f_clk * 0.25f);
    const float k = kFreezeGain * _decay / bbd_drive_gain(_drive);
    const float fb_lane = _fb_lane;
    const float fb = fb_lane + _freeze * (k - fb_lane);
    _l.SetFeedback(fb);
    _r.SetFeedback(fb);
}
```

Store `_drive`, `_fb_lane` and `_res_tilt` in `set_targets` / `set_resonance` instead of pushing them straight down, and call `_apply_freeze()` from `set_targets` and `_recompute` too — the tilt corner tracks `f_clk`. `kFreezeTilt` is the tilt that inverts `kLossCoef`; start it at `0.6f` and let Step 5's measurement settle it alongside `kFreezeGain`.

- [ ] **Step 4: Run, and expect the per-octave case to fail**

```bash
cmake --build build && ctest --test-dir build -R test_bbd_engine --output-on-failure
```

Expected: the DC, DECAY, FLOW and CHOKE cases pass; the per-octave case fails, reporting the dB error per band. **That failure is the measurement.**

- [ ] **Step 5: Measure `kFreezeGain` and `kFreezeTilt`**

The two constants are coupled: the tilt flattens the line's response and the gain sets its level. Bisect, one at a time, from the per-octave output:

1. Fix `kFreezeGain` and vary `kFreezeTilt` until the **spread** across the six bands (max dB − min dB) is minimised. That is the tilt that inverts `kLossCoef` at this operating point.
2. Then vary `kFreezeGain` until the **mean** dB across the bands is near 0.

Record every trial — value pair, six band figures, spread and mean — in a table in the task report. Two or three rounds should converge. Write the settled values into the two constants with a comment naming the operating point they were measured at (`div 1/2`, `T = 500 ms`, DRIVE swept 0→1, DECAY 1).

If the criterion cannot be met at ±1 dB, **stop and report** with the table rather than loosening it. §9 already flags residual STAGES-dependence as an open listening question; a measured miss is a finding, not a reason to move the goalposts.

- [ ] **Step 6: Re-run and confirm**

Expected: all six bands within ±1 dB, and every other case still green.

- [ ] **Step 7: Prove the DC blocker is load-bearing**

Change `_l.SetFeedbackDcBlock(on)` to `SetFeedbackDcBlock(false)` in both lines, rebuild, run. Expected: the 60 s DC case goes RED. Revert. Paste both outputs.

- [ ] **Step 8: Full suite and commit**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
git add engine/parts/bbd_engine.h engine/parts/bbd_engine.cpp tests/test_bbd_engine.cpp
git commit -F - <<'EOF'
feat(bbd): the freeze circulates, and holds a spectrum rather than a tone

Input muted, loop gain held: SetClock(0) is free and already implemented, but
it stops the READ ticks too, which is a pause in the signal rather than a
freeze of it.

Three legs, none of which the scalar-k scheme had. A DC blocker, because the
Butterworth sections are normalised H(0)=1 and any loop gain above unity at
1 kHz is above unity at DC. A feedback-path tilt tracking f_clk/4, because one
scalar cannot hold a spectrum through a lowpass -- and flattening the line to
~1 is also what makes the compander's L^2 round trip harmless. And DRIVE
divided out analytically, because the small-signal loop gain IS feedback * g,
so LANE_SOURCE would otherwise swing the loop +-12 dB per circulation.

k0 and the neutral tilt are measured against a per-octave criterion on
broadband material, not tuned at one frequency; the table is in the report.
FLOW ignores the gate, so the freeze -- and with it ATTACK and DECAY -- is
unreachable there. That is a mode-dependent dead knob and it belongs in the
manual.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 7: Stereo, and COLOR as its width

Two lines already exist (Task 4); this task gives them something to differ about. COLOR spreads the clock geometrically and holds the delay time on the grid for both, so the width is a brightness split rather than a rhythmic one.

Read **§5.7** of the spec, including what it deliberately gives up.

**Files:**
- Modify: `engine/parts/engine_iface.h` (`set_width`), `engine/parts/part.cpp` (`_control_tick` push), `engine/parts/bbd_engine.h/.cpp`
- Test: `tests/test_bbd_engine.cpp`

**Interfaces:**
- Produces: `virtual void IPartEngine::set_width(float)` (default no-op), `BbdEngine::set_width(float)`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("bbd engine: COLOR 0 with a mono source is bit-identical L to R") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
    e.set_cycle(0.5f);
    e.set_width(0.f);
    float t[LANE_COUNT] = { 0.3f, 1.f, 0.5f, 0.6f, 1.f };
    e.set_targets(t, 0.5f);
    for (int i = 0; i < 96000; ++i) {
        float l, r;
        const float x = std::sin(i * 0.05f) * (i < 24000 ? 1.f : 0.f);
        e.process_in(x, x);
        e.process(l, r);
        // Same signal, same clock, same stage count -> the two lines cannot
        // differ. Note this only holds because init_buffers seeds the two
        // dither streams differently AND SetDither is off at width 0... it is
        // not: both lines dither. So the identity must come from the SEED.
        CHECK(l == r);
    }
}

TEST_CASE("bbd engine: COLOR opened splits the lines and keeps the grid") {
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
    e.set_cycle(0.5f);
    e.set_width(0.6f);
    float t[LANE_COUNT] = { 0.3f, 1.f, 0.5f, 0.6f, 1.f };
    e.set_targets(t, 0.5f);
    bool differed = false;
    for (int i = 0; i < 48000; ++i) {
        float l, r;
        const float x = std::sin(i * 0.05f);
        e.process_in(x, x);
        e.process(l, r);
        if (l != r) differed = true;
    }
    CHECK(differed);
    // Both lines still land on the same delay: what differs is the stage count,
    // hence the bandwidth and grain, not the rhythm.
    CHECK(e.stages_l() / (2.f * e.clock_l())
          == doctest::Approx(e.stages_r() / (2.f * e.clock_r())).epsilon(0.01));
    // Symmetric and geometric: the geometric mean of the two clocks is the
    // un-spread one.
    CHECK(std::sqrt(e.clock_l() * e.clock_r())
          == doctest::Approx(e.clock_hz()).epsilon(0.001));
}

TEST_CASE("bbd engine: a mono source through the stereo engine stays mono") {
    // With both lines fed the same signal at the same clock they are
    // bit-identical; with a genuinely stereo source they differ from the first
    // sample. Both are correct.
    BbdEngine e;
    e.init(48000.f);
    e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
    e.set_cycle(0.5f);
    e.set_width(0.f);
    float t[LANE_COUNT] = { 0.f, 1.f, 0.5f, 0.5f, 1.f };
    e.set_targets(t, 0.5f);
    int diffs = 0;
    for (int i = 0; i < 48000; ++i) {
        float l, r;
        e.process_in(std::sin(i * 0.05f), std::sin(i * 0.09f));   // stereo in
        e.process(l, r);
        if (l != r) ++diffs;
    }
    CHECK(diffs > 40000);
}
```

Note the first test's own comment: it will fail as written if both lines dither from different seeds. **Resolve it in Step 3, not by weakening the assertion** — the two lines must be bit-identical for a mono source at COLOR 0, so the dither streams have to be identical too, and the different-seed line in Task 4's `init_buffers` must go. Delete it there and state why in this task's commit.

- [ ] **Step 2: Run and watch them fail**

Expected: `set_width`, `stages_l`, `clock_l`, `stages_r`, `clock_r` are not members.

- [ ] **Step 3: Implement the spread**

Remove the two different seeds from `init_buffers` and give both lines the same one, with the reason inline:

```cpp
    // The SAME seed for both lines. A mono source at COLOR 0 must come out
    // bit-identical L to R -- that is what "a mono source through a stereo
    // engine is mono" means -- and two dither streams would break it for a
    // reason no listener could name.
    _l.SeedDither(0x5bd1e995u);
    _r.SeedDither(0x5bd1e995u);
```

Add to `IPartEngine` (`engine_iface.h`), next to `set_excitation`:

```cpp
    // Stereo width, pushed once per control tick by Part::_control_tick from
    // the SAME effective COLOR the chord layer receives. Default no-op, the
    // set_excitation idiom: whichever engine is active is exactly the one that
    // gets pushed, and an engine switched away from and back to needs no
    // re-sync reasoning. One virtual call per part per control tick.
    virtual void set_width(float /*n*/) {}
```

In `Part::_control_tick`, immediately after the existing `set_color`/chord push:

```cpp
    _engine->set_width(_color_eff);
```

In `BbdEngine`:

```cpp
    void set_width(float n) override { _width = clampf(n, 0.f, 1.f); _recompute(); }
    float clock_l() const { return _f_l; }
    float clock_r() const { return _f_r; }
    int   stages_l() const { return _st_l; }
    int   stages_r() const { return _st_r; }
```

and in `_recompute`, after `_f_clk` is settled:

```cpp
    // COLOR: a symmetric geometric clock spread with the delay held on the grid
    // for BOTH lines. f_L = f*r, f_R = f/r, stages scaled to match, so both
    // delays remain T and what differs is the stage count -- hence the
    // bandwidth and grain (f_clk/4), plus the comb offset from the sub-sample
    // stage rounding.
    //
    // This deliberately gives up two-tone behaviour, which would need DIFFERENT
    // delay times: at T = 500 ms and 50 cents the lines are 29 ms apart on the
    // first repeat and 232 ms apart by the eighth, and the character changes
    // completely with the division. Consequence to know: at self-oscillation
    // both lines sing at 1/T, i.e. in unison.
    //
    // kWidthMaxCents is deliberately small: the stage-count ratio is r^2, so a
    // few cents already give an audible brightness split at no rhythmic cost.
    const float cents = _width * kWidthMaxCents;
    const float r = std::pow(2.f, cents * (1.f / 1200.f));
    _f_l = _f_clk * r;
    _f_r = _f_clk / r;
    _st_l = bbd_music::stages_for(_win, _f_l);
    _st_r = bbd_music::stages_for(_win, _f_r);
    _l.SetStages(_st_l);
    _r.SetStages(_st_r);
```

with `constexpr float kWidthMaxCents = 30.f;` in the anonymous namespace, and `process` using `_f_l` / `_f_r` instead of `_f_clk`. `_f_clk` stays the observer's un-spread centre.

- [ ] **Step 4: Run**

```bash
cmake --build build && ctest --test-dir build -R test_bbd_engine --output-on-failure
```

Expected: PASS, including the bit-identity case.

- [ ] **Step 5: Prove the bit-identity test can fail**

Restore the two different dither seeds, rebuild, run. Expected: the COLOR 0 case goes RED at the first sample. Revert. Paste both outputs — this is the test that decides whether COLOR 0 is genuinely a mono position.

- [ ] **Step 6: Full suite and commit**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
git add engine/parts/engine_iface.h engine/parts/part.cpp \
        engine/parts/bbd_engine.h engine/parts/bbd_engine.cpp tests/test_bbd_engine.cpp
git commit -F - <<'EOF'
feat(bbd): COLOR takes the clock offset between the stereo lines

A symmetric geometric spread with the delay time held on the grid for both
lines: f_L = f*r, f_R = f/r, stage counts scaled to match. What differs is the
bandwidth and grain, plus the comb offset from sub-sample stage rounding -- a
stereo image made of two differently-bright copies of the SAME rhythm.

Gives up two-tone deliberately. Two tones need different delay times, which
splits the lines rhythmically and cumulatively: at T = 500 ms and 50 cents
they are 29 ms apart on the first repeat and 232 ms apart by the eighth, and
the character changes completely with the division. Consequence stated: at
self-oscillation both lines sing in unison.

Both lines dither from the same seed, so a mono source at COLOR 0 comes out
bit-identical L to R. Different seeds would have broken that for a reason no
listener could name.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 8: The VOICE row, all six forwards

`Part::set_voice_*` is six hand-written lines, one per knob (`part.h:153-163`). An engine missing from any of them has a silently dead knob. ATTACK and DECAY landed in Task 6; this task adds the other four and extends all six forwards together.

Read **§5.8** of the spec, including why FILT moves the loss pole and not `kFilterHz`.

**Files:**
- Modify: `engine/parts/part.h:153-163`, `engine/parts/bbd_engine.h/.cpp`
- Test: `tests/test_bbd_engine.cpp`

**Interfaces:**
- Produces: `BbdEngine::{set_resonance, set_sub, set_detune, set_filt}`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("bbd engine: every VOICE knob reaches the BBD") {
    // Part::set_voice_* is six hand-written lines. An engine missing from one
    // of them is a dead knob with no diagnostic, so this checks all six by
    // observing a consequence, not by reading the source.
    Part p;
    p.init(48000.f, 17u, nullptr, nullptr, 0, s_bbd_l, s_bbd_r);
    p.set_engine(ENGINE_BBD);
    float l, r;
    for (int i = 0; i < 500; ++i) p.process(l, r);
    // FILT: the loss-pole corner. Dark and bright must differ in HF energy.
    auto hf = [&](float filt) {
        p.set_voice_filt(filt);
        p.bbd().reset();
        float s = 0.f;
        for (int i = 0; i < 48000; ++i) {
            const float x = std::sin(i * 0.5f);
            p.process(x, x, l, r);
            if (i > 24000) s += l * l;
        }
        return s;
    };
    CHECK(hf(-1.f) < hf(1.f));
    // FILT centre is exactly kLossCoef -- the neutral position, so a knob left
    // alone changes nothing.
    p.set_voice_filt(0.f);
    CHECK(p.bbd().loss_coef() == doctest::Approx(bbd_tuning::kLossCoef));
    // RESONANCE plays the feedback tilt.
    p.set_voice_resonance(0.f);
    const float lo = p.bbd().resonance_tilt();
    p.set_voice_resonance(1.f);
    CHECK(p.bbd().resonance_tilt() > lo);
    // SUB is the input level.
    p.set_voice_sub(0.f);
    CHECK(p.bbd().input_gain() == doctest::Approx(0.f));
    p.set_voice_sub(1.f);
    CHECK(p.bbd().input_gain() == doctest::Approx(1.f));
    // DETUNE (menu-only) is the slew time.
    p.set_voice_detune(0.f);
    const float fast = p.bbd().slew_seconds();
    p.set_voice_detune(1.f);
    CHECK(p.bbd().slew_seconds() > fast);
    // ATTACK and DECAY reached it in the freeze task; assert they still do.
    p.set_voice_attack(1.f);
    p.set_voice_decay(0.3f);
    CHECK(p.bbd().freeze_ramp_seconds() > 0.5f);
    CHECK(p.bbd().decay_norm() == doctest::Approx(0.3f));
}

TEST_CASE("bbd engine: DETUNE's slew decides how far a modulated bend travels") {
    auto travel = [](float detune) {
        BbdEngine e;
        e.init(48000.f);
        e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
        e.set_cycle(1.0f);
        e.set_flow(true);
        e.set_detune(detune);
        float t[LANE_COUNT] = { 0.f, 1.f, 0.1f, 0.5f, 1.f };
        e.set_targets(t, 0.5f);
        for (int i = 0; i < 4800; ++i) { float l, r; e.process_in(0.f,0.f); e.process(l, r); }
        const float start = e.clock_now();
        t[LANE_PITCH] = 0.9f;                 // a step the slew must chase
        e.set_targets(t, 0.5f);
        for (int i = 0; i < 2400; ++i) { float l, r; e.process_in(0.f,0.f); e.process(l, r); }
        return e.clock_now() / start;
    };
    // clock_now(), not clock_hz(): clock_hz() is the TARGET the lane asks for
    // and moves instantly. What DETUNE decides is how fast the line follows it.
    CHECK(travel(0.f) > travel(1.f));    // a short slew gets further in 50 ms
}
```

- [ ] **Step 2: Run and watch it fail**

Expected: the four setters and the six observers do not exist.

- [ ] **Step 3: Implement the four setters and the slew**

```cpp
namespace {
// FILT's endpoints, geometric around kLossCoef so the knob centre is EXACTLY
// the physical value -- a knob left alone must change nothing.
constexpr float kFiltOctaves = 2.f;
// DETUNE (menu): the slew the clock chases a moved lane at. Flux records this
// as a deliberate musical value, and since PITCH and SIZE are both driven by
// the plane, it decides HOW the engine answers modulation.
constexpr float kSlewMinS = 0.001f;
constexpr float kSlewMaxS = 0.5f;
}

void BbdEngine::set_filt(float t) {
    // The loss pole, NOT kFilterHz. kFilterHz is constexpr, baked into
    // butterworth_poles(), and its coefficients live in two file-scope
    // singletons that every BbdLine holds raw pointers into -- one deck's knob
    // would retune the whole instrument, a rebuild is 396 transcendentals, and
    // bbd.h:270-281 documents in-place rebuild as a shared-mutable hazard safe
    // only with the audio callback stopped. The loss pole is a per-line scalar
    // with no rebuild cost, and it is the pole that carries the darkness: at
    // 16384 stages the fixed chain contributes only -0.93 dB at 2.5 kHz.
    _loss_a = bbd_tuning::kLossCoef
              * std::pow(2.f, kFiltOctaves * clampf(t, -1.f, 1.f));
    if (_loss_a > 0.999f) _loss_a = 0.999f;
    if (_loss_a < 1e-4f) _loss_a = 1e-4f;
    _l.SetLossCoef(_loss_a);
    _r.SetLossCoef(_loss_a);
}

void BbdEngine::set_resonance(float n) {
    // How bright the repeats stay. Neutral inverts kLossCoef's tilt; left, they
    // darken faster than physics; right, they brighten and approach the freeze
    // condition. The same filter the freeze needs, so it costs one biquad that
    // is already there.
    _res_tilt = (clampf(n, 0.f, 1.f) - 0.5f) * 2.f * kFreezeTilt;
    _apply_freeze();
}

void BbdEngine::set_sub(float n) { _in_gain = clampf(n, 0.f, 1.f); }

void BbdEngine::set_detune(float n) {
    const float s = kSlewMinS * std::pow(kSlewMaxS / kSlewMinS, clampf(n, 0.f, 1.f));
    _slew_s = s;
    _slew_coef = 1.f / (s * _sr);
}
```

The slew acts on the clock, not on the lane, because pitch tracks the clock ratio — so interpolate geometrically. In `process`, before the two `Process` calls:

```cpp
    // GEOMETRIC, because pitch tracks the clock RATIO, not its difference: a
    // linear slew from 500 Hz to 8000 Hz would cross the first octave in a
    // twentieth of the time it spends on the last one. Interpolating the LOG
    // makes a semitone take the same time wherever it starts. Flux's DRAG
    // interpolates geometrically for exactly this reason. It also doubles as
    // the VCO slew of the real circuit: a division change is click-free AND
    // bends in pitch, like the hardware.
    //
    // A one-pole on the log is a per-sample logf/expf pair, which the
    // per-sample budget will not carry. Multiply toward the target instead --
    // the same fixed point, no transcendentals: x *= (target/x)^c is
    // x * (1 + c*(ratio-1)) to first order, and the first order is what a slew
    // is. Clamped so a large jump cannot overshoot.
    auto glide = [](float now, float target, float c) {
        const float ratio = target / now;
        const float step = 1.f + c * (ratio - 1.f);
        const float next = now * (step > 0.f ? step : 1.f);
        return (ratio > 1.f) ? (next > target ? target : next)
                             : (next < target ? target : next);
    };
    _f_now = glide(_f_now, _f_l, _slew_coef);
    _f_now_r = glide(_f_now_r, _f_r, _slew_coef);
```

`_f_now` and `_f_now_r` must be initialised to a positive value (`init` sets both to `_f_clk`) — a zero would make the ratio non-finite. Use them as the clock arguments in place of `_f_l` / `_f_r`.

Add the observers the tests read: `loss_coef()`, `resonance_tilt()`, `input_gain()`, `slew_seconds()`, `freeze_ramp_seconds()`, `decay_norm()` — each a one-line const getter over the member it names (`_loss_a`, `_res_tilt`, `_in_gain`, `_slew_s`, `_freeze_ramp_s`, `_decay`) — plus one more that is not a knob:

```cpp
    // The clock the line is actually running at, as opposed to the one the lane
    // is asking for. Before the slew existed the two were the same number; they
    // are not any more, and every test about how the engine ANSWERS modulation
    // has to read this one.
    float clock_now() const { return _f_now; }
```

- [ ] **Step 4: Extend all six forwards, together**

`engine/parts/part.h:153-163` — one `_bbd.` call per line, none omitted:

```cpp
    void set_voice_attack(float n)    { _synth.set_attack(n);    _wave.set_attack(n);    _body.set_attack(n);    _sampler.set_window_attack(n); _bbd.set_attack(n); }
    void set_voice_decay(float n)     { _synth.set_decay(n);     _wave.set_decay(n);     _body.set_decay(n);     _sampler.set_window_decay(n);  _bbd.set_decay(n); }
    void set_voice_resonance(float n) { _synth.set_resonance(n); _wave.set_resonance(n); _body.set_resonance(n); _sampler.set_resonance(n);     _bbd.set_resonance(n); }
    void set_voice_sub(float n)       { _synth.set_sub(n);       _wave.set_sub(n);       _body.set_sub(n);       _bbd.set_sub(n); }
    void set_voice_detune(float n)    { _synth.set_detune(n);    _wave.set_detune(n);    _body.set_detune(n);    _bbd.set_detune(n); }
    void set_voice_filt(float t)      { _synth.set_filt(t);      _wave.set_filt(t);      _body.set_filt(t);      _sampler.set_filt(t);          _bbd.set_filt(t); }
```

Update the block comment above them to name the BBD's reinterpretation, in the shape it already uses for the sampler and BODY.

- [ ] **Step 5: Run**

Expected: PASS.

- [ ] **Step 6: Prove the forward test can fail**

Delete `_bbd.set_sub(n);` from `set_voice_sub`, rebuild, run. Expected: the six-knob case goes RED on `input_gain()`. Restore. Paste both outputs — a dead knob is precisely the failure this test exists for.

- [ ] **Step 7: Full suite and commit**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
git add engine/parts/part.h engine/parts/bbd_engine.h engine/parts/bbd_engine.cpp \
        tests/test_bbd_engine.cpp
git commit -F - <<'EOF'
feat(bbd): the VOICE row, and all six forwards extended together

RESONANCE plays the feedback-path tilt -- the same filter the freeze needs, so
it costs one biquad that is already there, and "how the tail colours" is an
honest meaning for a resonance control on a delay. SUB is the input level.
DETUNE (menu) is the slew the clock chases a moved lane at, applied
geometrically because pitch tracks the clock ratio. FILT moves the LOSS POLE.

FILT cannot move kFilterHz: it is constexpr, baked into butterworth_poles(),
and its coefficients live in two file-scope singletons every BbdLine holds raw
pointers into -- one deck's knob would retune the whole instrument. The loss
pole is a per-line scalar with no rebuild cost, and it is the pole that carries
the darkness anyway.

All six Part::set_voice_* lines gain their _bbd. call in one edit, because
missing one is a dead knob with no diagnostic. The test observes a consequence
per knob rather than reading the source, and was proven red by deleting one.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 9: The observer, and the render host

A BBD deck writes 0 into `a_voices`/`a_v0..3` and exposes nothing, so a render scenario would pass vacuously. `f_clk`, the derived stage count, the active `div` rung, the freeze state and the clamp flag all become CSV columns — a clamp that is invisible reads as a broken knob.

**§9 corrects rev. 1's claim of precedent here:** `Flux`'s observer is `stages()` (`flux.h:72`), `stages_for_test` is an `Instrument` method used only by a test, and **neither appears in the CSV header** (`host/render/main.cpp:80-85`). This is new work, not a pattern to copy.

**Files:**
- Modify: `host/render/scenario.cpp:85-91` (`parse_engine`), `host/render/main.cpp:79-86` (the header) and its row-writing loop, `engine/instrument.h` (the observer forwards)
- Create: `host/render/scenarios/bbd_pitch_bend.json`
- Test: `tests/test_scenario.cpp`

**Interfaces:**
- Produces: `Instrument::bbd_clock_hz(int p)`, `bbd_stages(int p)`, `bbd_div(int p)`, `bbd_frozen(int p)`, `bbd_time_clamped(int p)`, `bbd_scale_truncated(int p)`; the scenario spelling `"bbd"`; six new CSV columns per deck.

- [ ] **Step 1: Write the failing test**

Append to `tests/test_scenario.cpp`:

```cpp
TEST_CASE("scenario: the BBD engine is selectable by name") {
    // Without a spelling here no render scenario can select the engine, and
    // half of the definition of done is unmeasurable.
    Scenario s;
    const char* json = R"({"duration_s":0.1,"init":[
        {"action":"set_engine","part":0,"svalue":"bbd"}]})";
    REQUIRE(parse_scenario_string(json, s));   // use this file's existing helper
    CHECK(s.init_events[0].svalue == "bbd");
    Instrument inst;
    inst.init(48000.f);
    apply_event(inst, s.init_events[0]);
    CHECK(inst.engine_id(0) == spky::ENGINE_BBD);
}
```

Read the file's existing cases for the real parse helper name and `Event` field spellings; do not invent them.

- [ ] **Step 2: Run and watch it fail**

Expected: `engine_id(0)` is `ENGINE_SYNTH` — `parse_engine`'s fallthrough.

- [ ] **Step 3: Add the spelling**

`host/render/scenario.cpp:85-91`:

```cpp
static EngineId parse_engine(const std::string& s) {
    if (s == "test_tone") return ENGINE_TEST_TONE;
    if (s == "sampler")   return ENGINE_SAMPLER;
    if (s == "wave")      return ENGINE_WAVE;
    if (s == "body")      return ENGINE_BODY;
    if (s == "bbd")       return ENGINE_BBD;
    return ENGINE_SYNTH;
}
```

- [ ] **Step 4: Forward the observers through `Instrument`**

`engine/instrument.h`, alongside the existing per-part observers:

```cpp
    // BBD observers (spec 2026-07-31 9). A BBD deck writes 0 into a_voices and
    // a_v0..3 and would otherwise expose nothing, so a demo scenario would pass
    // vacuously. Zero on every other engine, which is what the CSV should show.
    float bbd_clock_hz(int p) const {
        return _parts[p].engine_id() == ENGINE_BBD ? _parts[p].bbd().clock_hz() : 0.f;
    }
    int   bbd_stages(int p) const {
        return _parts[p].engine_id() == ENGINE_BBD ? _parts[p].bbd().stages() : 0;
    }
    int   bbd_div(int p) const {
        return _parts[p].engine_id() == ENGINE_BBD ? _parts[p].bbd().div_index() : -1;
    }
    bool  bbd_frozen(int p) const {
        return _parts[p].engine_id() == ENGINE_BBD && _parts[p].bbd().frozen();
    }
    bool  bbd_time_clamped(int p) const {
        return _parts[p].engine_id() == ENGINE_BBD && _parts[p].bbd().time_clamped();
    }
    bool  bbd_scale_truncated(int p) const {
        return _parts[p].engine_id() == ENGINE_BBD && _parts[p].bbd().scale_truncated();
    }
```

- [ ] **Step 5: Add the CSV columns**

`host/render/main.cpp`, in the header string, append to each deck's block — after `a_matl,` and `b_matl,` respectively:

```
a_fclk,a_stages,a_div,a_frz,a_tclamp,a_strunc,
```

and the matching `b_` six. Then add the six values per deck to the row `fprintf` in the same order. Count the columns in the header against the count in the row before running: a mismatch shifts every column after it and every later reader silently misreads.

- [ ] **Step 6: Write the demo scenario**

Create `host/render/scenarios/bbd_pitch_bend.json` — a BBD deck on deck A, fed from audio-in, FEEDBACK up, `LANE_PITCH` walked across its travel with material in flight. Copy the structure of an existing scenario in that directory (`ls host/render/scenarios/`) rather than composing the schema from scratch. It is a demo and a manual listening target, not a gate.

- [ ] **Step 7: Run**

```bash
cmake --build build && ctest --test-dir build -R "test_scenario|render" --output-on-failure
./build/spotyrender host/render/scenarios/bbd_pitch_bend.json /tmp/bbd.wav /tmp/bbd.csv
head -2 /tmp/bbd.csv
```

Expected: the test passes; the CSV's header and first row have the same number of fields, and `a_fclk` is non-zero.

- [ ] **Step 8: Verify the columns line up**

```bash
python - <<'PY'
h, r = open("/tmp/bbd.csv").readlines()[:2]
print(len(h.strip().split(",")), len(r.strip().split(",")))
PY
```

Expected: two equal numbers. If they differ, fix the `fprintf` — do not proceed.

- [ ] **Step 9: Commit**

```bash
git add host/render/scenario.cpp host/render/main.cpp engine/instrument.h \
        host/render/scenarios/bbd_pitch_bend.json tests/test_scenario.cpp
git commit -F - <<'EOF'
feat(bbd): the engine is selectable from a scenario, and it reports itself

parse_engine gains "bbd" -- without it no render scenario can select the
engine and half of the definition of done is unmeasurable.

Six CSV columns per deck: f_clk, the derived stage count, the active div rung,
the freeze state, and the two flags. A BBD deck writes 0 into a_voices and
a_v0..3, so a demo scenario would otherwise pass vacuously, and a clamp that is
invisible reads as a broken knob. Note there was no precedent to copy: Flux's
stages() and Instrument::stages_for_test appear in NEITHER the CSV header nor
any scenario.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 10: The VCV host

Five things break silently if any one is missed: the switch range, the dispatch arm (whose own comment says *"anything that isn't 0/2/3 still falls through to Sampler"*), the shade table, the SOURCE caption, and the audition bench's arm. `STAGES_A/B` is orphaned by movement 3 and becomes the `LANE_PITCH` base on a BBD deck.

Read **§5.10 and §5.12's "VCV and render surface"** list.

**Files:**
- Modify: `host/vcv/src/Spotymod.cpp` — `:111-116` (shades), `:287-291` (configSwitch), `:438-444` (dispatch), `:1066-1068` (caption), the control-tick push, the FLUX default
- Modify: `host/vcv/res/test_panel.py` (the three pins Task 1 rebased), `host/vcv/res/gen_panel.py` (the ENG and SOURCE tooltips), `bench/audition/init_patch.cpp`
- Modify: `host/vcv/README.md`

**Interfaces:**
- Consumes: everything from Tasks 4–8.
- Produces: ENG state 4 = BBD, reachable and saved.

- [ ] **Step 1: Update the panel pins first, so they are the failing test**

In `host/vcv/res/test_panel.py`, change the three pins Task 1 rebased to what this task is about to ship:

```python
    engine_config = """
else if (c.id == ENGINE_A || c.id == ENGINE_B) {
    configSwitch(c.id, 0.f, 4.f, init, "Engine",
                 {"Synth", "Sampler", "Wave", "Body", "BBD"});
    getParamQuantity(c.id)->snapEnabled = true;
}"""
```

```python
    dispatch = """
const int eng = static_cast<int>(std::round(pp(ENGINE_A, p)));
const spky::EngineId id =
    eng == 0 ? spky::ENGINE_SYNTH :
    eng == 2 ? spky::ENGINE_WAVE :
    eng == 3 ? spky::ENGINE_BODY :
    eng == 4 ? spky::ENGINE_BBD :
    smp[p].testTone ? spky::ENGINE_TEST_TONE : spky::ENGINE_SAMPLER;
inst.set_engine(p, id);"""
```

```python
    want = {0: "TIMB", 1: "ORG", 2: "FRAME", 3: "MATL", 4: "DRIVE"}
```

- [ ] **Step 2: Run the panel suite and watch it fail**

```bash
cd host/vcv/res && python test_panel.py
```

Expected: `FAIL (3):` — config, dispatch, caption. Everything Task 1 fixed stays green.

- [ ] **Step 3: Extend the switch, the shades and the caption**

`Spotymod.cpp:287-291`:

```cpp
                    else if (c.id == ENGINE_A || c.id == ENGINE_B) {
                        configSwitch(c.id, 0.f, 4.f, init, "Engine",
                                     {"Synth", "Sampler", "Wave", "Body", "BBD"});
                        getParamQuantity(c.id)->snapEnabled = true;
                    }
```

`:111-116` — one more shade; the index is already defensive:

```cpp
    nvgRGBA(160, 255, 150, 140),  // Body: green
    nvgRGBA(230, 140, 255, 140),  // BBD: violet
```

`:1066-1068` — on a BBD deck SOURCE is the DRIVE base:

```cpp
static const char* sourceCaption(int state) {
    return state == 1 ? "ORG" : state == 2 ? "FRAME"
         : state == 3 ? "MATL" : state == 4 ? "DRIVE" : "TIMB";
}
```

- [ ] **Step 4: Add the dispatch arm**

`Spotymod.cpp:438-444`, and extend the comment above it — the existing one names 0/2/3 explicitly:

```cpp
            // Saved ENG meanings remain 0 = Synth and 1 = Sampler; 2 adds
            // Wave, 3 Body, 4 the BBD. Each new engine needs its own explicit
            // arm here -- anything that isn't 0/2/3/4 still falls through to
            // Sampler (or the dev test tone), which is also why old patches
            // keep their exact meaning. The test tone stays a Sampler-only
            // override.
            const int eng = static_cast<int>(std::round(pp(ENGINE_A, p)));
            const spky::EngineId id =
                eng == 0 ? spky::ENGINE_SYNTH :
                eng == 2 ? spky::ENGINE_WAVE :
                eng == 3 ? spky::ENGINE_BODY :
                eng == 4 ? spky::ENGINE_BBD :
                smp[p].testTone ? spky::ENGINE_TEST_TONE : spky::ENGINE_SAMPLER;
            inst.set_engine(p, id);
```

- [ ] **Step 5: Re-point `STAGES_A/B` and default FLUX off**

In the control-tick push, alongside the existing `samplerPart` gate, add a `bbdPart` gate and route the orphaned knob:

```cpp
            const bool bbdPart = inst.engine_id(p) == spky::ENGINE_BBD;
            // STAGES is orphaned by movement 3 and becomes the LANE_PITCH base
            // on a BBD deck. Re-pointing a knob per engine is not new -- the
            // sampler already moves SUB_A to LANE_SIZE as GENE SIZE.
            if (bbdPart)
                inst.set_target_base(p, spky::LANE_PITCH, pp(STAGES_A, p));
            else
                inst.set_stages(p, pp(STAGES_A, p));
```

Read the surrounding code for the exact spelling of the existing `set_stages` call before replacing it; keep the non-BBD branch byte-identical to what is there now.

Two more things happen on the **switch edge**, not every tick, or the player can never override them:

```cpp
            if (bbdPart && !wasBbd[p]) {
                // FLUX defaults disengaged (spec 5.11). The BBD's output is
                // already six poles at 3600 Hz plus a loss pole breathing under
                // a compander, and its gappy repeats are its most distinctive
                // trait -- which a tape echo behind it fills in. The player can
                // add it back; the default should not be darker-and-smeared.
                params[p ? FLUX_B : FLUX_A].setValue(0.f);
                // The silence trap's first half (spec 5.12): a BBD deck with no
                // source selected is an FX unit wired to nothing. Default the
                // neighbouring deck ON. Audio-in already reaches process_in
                // unconditionally through Part::process; what the checkbox gates
                // is the cross-deck bus (movement 1, Part::_src_deck), and that
                // is what makes resampling work without external cabling.
                smp[p].exciteDeck = true;
            }
            wasBbd[p] = bbdPart;
```

with `bool wasBbd[2] = { false, false };` as module state. Read `SamplerPartState` for the real field name behind `exciteDeck` — the menu item is the one relabelled in movement 1 to *"Route: other deck (BODY excite, SAMPLER feed+rec)"*, and its label needs the BBD added.

The second half of the silence trap — *no signal present* — is Task 2's dither, which is what makes an unconnected deck audible at all.

- [ ] **Step 6: Update the tooltips and regenerate the panel**

`host/vcv/res/gen_panel.py`: extend the ENG tooltip's engine list and the SOURCE tooltip's caption list to include the BBD. Then:

```bash
cd host/vcv/res && python gen_panel.py && python test_panel.py
```

Expected: `OK`. `generated_panel.hpp` should show tooltip text changes and **no id reordering** — check `git diff` before continuing. A moved id silently reassigns every saved patch.

- [ ] **Step 7: Give the audition bench the BBD arm too**

`bench/audition/init_patch.cpp`, extending Task 1's dispatch:

```cpp
              : engine_value == 3 ? spky::ENGINE_BODY
              : engine_value == 4 ? spky::ENGINE_BBD
                                  : spky::ENGINE_SAMPLER;
```

- [ ] **Step 8: Build the VCV host**

```bash
./build-local.sh
```

Never invoke `g++` directly — the system one here is the ARM cross-compiler and fails with *"MinGW not found"*. Expected: a clean build.

- [ ] **Step 9: Prove the dispatch arm can fail**

Delete `eng == 4 ? spky::ENGINE_BBD :`, run `python test_panel.py`. Expected: RED on the dispatch pin. Restore, confirm `OK`. Paste both outputs — the ternary's fallthrough to Sampler is the exact silent failure §5.12 names.

- [ ] **Step 10: Document the surface**

`host/vcv/README.md`: a short section listing what the panel means on a BBD deck — the five lanes, SOURCE as the DRIVE base, STAGES as the PITCH base, the VOICE row's six meanings, and the two consequences that will otherwise read as bugs: **PITCH is inaudible at FEEDBACK 0**, and **ATTACK and DECAY are inert in FLOW** because the freeze is unreachable there.

- [ ] **Step 11: Full suite and commit**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure
cd host/vcv/res && python test_panel.py && cd ../../..
git add host/vcv/src/Spotymod.cpp host/vcv/res/test_panel.py host/vcv/res/gen_panel.py \
        host/vcv/res/generated_panel.hpp host/vcv/res/Spotymod.svg \
        bench/audition/init_patch.cpp host/vcv/README.md
git commit -F - <<'EOF'
feat(bbd): ENG state 4 selects the BBD, on every surface that dispatches

Five sites, each of which fails silently on its own: the switch range and
labels, the dispatch ternary (whose own comment says anything that isn't
0/2/3 falls through to Sampler), the shade table, the SOURCE caption, and the
audition bench's arm.

STAGES_A/B is orphaned by movement 3 and becomes the LANE_PITCH base on a BBD
deck -- re-pointing a knob per engine is the pattern the sampler already uses
for GENE SIZE. FLUX defaults disengaged at the switch edge: the BBD's output
is already six poles at 3600 Hz plus a loss pole breathing under a compander,
and its gappy repeats are the trait a tape echo behind it fills in. At the
edge, not every tick, so the player can still turn it on.

README states the two things that would otherwise read as bugs: PITCH is
inaudible at FEEDBACK 0, and the freeze -- so ATTACK and DECAY -- is
unreachable in FLOW.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Task 11: The bench row, the contract, and the definition of done

Two things remain that no earlier task could finish: a hardware cost figure, and the cross-cutting bullets of §5.13. WAVE and BODY both entered through `tests/synth_engine_contract.h`; a voiceless non-`SynthEngineT` engine cannot satisfy it, so §5.13's list is the raw material for a replacement.

Read **§5.13 and §8.3** of the spec.

**Files:**
- Create: `tests/part_engine_contract.h`
- Modify: `tests/test_bbd_engine.cpp` (invoke the contract), `bench/workloads_system.cpp`, `bench/run.py`
- Create: `docs/bench/2026-07-31-<sha>-bbd-engine.md`

**Interfaces:**
- Consumes: everything.
- Produces: the bench row `inst_bbd_engine_worst`; a reusable `part_engine_contract` header.

- [ ] **Step 1: Write the contract header**

`tests/part_engine_contract.h` — what any `IPartEngine` must do regardless of whether it has voices. Take the invariants from §5.13 that are engine-agnostic:

```cpp
#pragma once
#include "doctest.h"
#include "parts/engine_iface.h"
#include <cmath>

namespace spky {

// The contract every part engine owes, voiced or not. tests/synth_engine_
// contract.h covers SynthEngineT specifically -- voices, envelopes, note
// allocation -- and a voiceless input-consuming engine cannot satisfy it.
// This is the part that is genuinely universal.
template <typename E, typename Setup>
inline void check_part_engine_contract(Setup setup) {
    {   // Silence in, no output growth, and finite forever.
        E e; setup(e);
        float peak = 0.f;
        for (int i = 0; i < 48000 * 5; ++i) {
            float l, r;
            if (e.consumes_input()) e.process_in(0.f, 0.f);
            e.process(l, r);
            REQUIRE(std::isfinite(l));
            REQUIRE(std::isfinite(r));
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
        }
        CHECK(peak <= 1.f);
    }
    {   // consumes_input() and process_in() are overridden TOGETHER. Nothing in
        // the language enforces this and the failure is silent: an engine that
        // implements process_in and forgets the flag never hears its input.
        E e; setup(e);
        if (e.consumes_input()) {
            float l0, r0, l1, r1;
            for (int i = 0; i < 4800; ++i) {
                e.process_in(0.f, 0.f);
                e.process(l0, r0);
            }
            E f; setup(f);
            for (int i = 0; i < 4800; ++i) {
                f.process_in(std::sin(i * 0.05f), std::sin(i * 0.05f));
                f.process(l1, r1);
            }
            // If process_in were unreachable the two runs would be identical.
            CHECK(l0 != l1);
        }
    }
    {   // Every no-op setter is safe to call in any order, at any time.
        E e; setup(e);
        e.set_flow(true); e.set_flow(false);
        e.set_gate(true); e.set_gate(false);
        e.set_hold(true); e.set_hold(false);
        e.set_cycle(0.f); e.set_cycle(1e6f); e.set_cycle(0.25f);
        e.set_width(0.f); e.set_width(1.f);
        float l, r;
        for (int i = 0; i < 4800; ++i) {
            if (e.consumes_input()) e.process_in(0.f, 0.f);
            e.process(l, r);
            REQUIRE(std::isfinite(l));
        }
    }
}

}  // namespace spky
```

Invoke it from `tests/test_bbd_engine.cpp`:

```cpp
TEST_CASE("bbd engine: satisfies the part-engine contract") {
    check_part_engine_contract<BbdEngine>([](BbdEngine& e) {
        e.init(48000.f);
        e.init_buffers(s_bbd_l, s_bbd_r, Flux::kMaxSamples);
        e.set_cycle(0.5f);
    });
}
```

- [ ] **Step 2: Run**

```bash
source env.sh && cmake --build build && ctest --test-dir build -R test_bbd_engine --output-on-failure
```

Expected: PASS. If the `set_cycle(0.f)` or `set_cycle(1e6f)` case trips, fix the engine's clamping — a host can and will push both.

- [ ] **Step 3: Add the bench row**

`bench/workloads_system.cpp`: register `inst_bbd_engine_worst` in **`kCoreWorkloads[]`** — note `kSystemWorkloads[]` does not exist. Model it on `instrument_worst` at `bench/workloads_system.cpp:437`, changing only what the row is about: both decks on `ENGINE_BBD`, FEEDBACK high, COLOR open, the freeze off, audio arriving.

**The row must actually reach the engine.** Movement 1's Task 5 shipped a row that stayed on `ENGINE_SYNTH` and therefore never reached `process_in` — measuring nothing. Before trusting a number, assert in the row's own setup that `engine_id()` is `ENGINE_BBD` for both decks.

Register the row in `bench/run.py`'s `BENCH_PROTOCOL_ROWS_BY_FAMILY` alongside its neighbours.

- [ ] **Step 4: Verify the object is not stale**

The bench build can silently relink a stale object. After building, confirm the new row is really in the image:

```bash
grep -c "inst_bbd_engine_worst" bench/build/bench.map
```

Expected: at least 1. **Read `bench.map`, not the memory table** — the table can show a plausible number for code that was never linked.

- [ ] **Step 5: Measure on hardware**

Flash and run per `bench/README.md`. Record `pct_avg` and `pct_max` for `inst_bbd_engine_worst` beside `instrument_worst` from the same run — same binary, same session, or the comparison is not a comparison.

`pct_max` is the gate, not `pct_avg`. §2's estimate is ≈11.8–12.6 `pct_avg` for a stereo BBD deck against a SYNTH deck's 17.60–18.21; if the measured figure is far outside that, **report it rather than explaining it** — an unexplained gap is a finding.

- [ ] **Step 6: Write the bench note**

`docs/bench/2026-07-31-<sha>-bbd-engine.md`, following the shape of `docs/bench/2026-07-31-20eafed-deck-bus.md`. State what the row does, both figures, the comparison row, and — explicitly — whether this is a **use** cost or a **code** cost. Movement 1's note records why that distinction matters: a use cost and a code cost are not commensurable, and conflating them produced a 140× discrepancy that took a separate experiment to explain.

- [ ] **Step 7: Walk §5.13 and report**

Go through the definition-of-done list bullet by bullet. For each: name the test that covers it, or say plainly that it is not covered. In particular:

- *"The other four engines show no unintended change"* — run the two render-hash gates and the full suite. Under the owner's ruling a deliberate change is fine; name any that moved and why.
- *"With no input connected and FEEDBACK high, the engine self-oscillates from the dither floor rather than outputting silence"* — if no test asserts this, write one now.
- *"After 60 s of silence at the input, no denormal stall is measurable on x86"* — time a 60 s silent run and compare against a run with the dither on. A stall shows as wall-clock, not as a wrong number.

Put the walk in the task report as a table. Anything uncovered goes to the controller as a finding, not into the commit message as a claim.

- [ ] **Step 8: Full suite and commit**

```bash
cmake --build build && ctest --test-dir build --output-on-failure
cd host/vcv/res && python test_panel.py && cd ../../..
git add tests/part_engine_contract.h tests/test_bbd_engine.cpp \
        bench/workloads_system.cpp bench/run.py docs/bench/
git commit -F - <<'EOF'
test(bbd): a contract a voiceless engine can satisfy, and a hardware figure

WAVE and BODY both entered through tests/synth_engine_contract.h, which a
voiceless non-SynthEngineT engine cannot satisfy. part_engine_contract.h is
the genuinely universal part: bounded and finite forever on silence, the
process_in/consumes_input pair actually reachable, and every no-op setter safe
in any order.

inst_bbd_engine_worst measures both decks on the BBD with feedback high and
COLOR open. The row asserts engine_id() in its own setup, because movement 1
shipped a row that stayed on ENGINE_SYNTH and measured nothing, and the figure
is verified present via bench.map rather than the memory table.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>
EOF
```

---

## Self-review notes for the executing controller

Three things in this plan are known-soft and are flagged rather than hidden:

1. **Task 7's first test contradicts Task 4's `init_buffers` on purpose.** Task 4 seeds the two dither streams differently; Task 7 makes them the same and says why. If a reviewer flags the contradiction, that is the mechanism working, not a defect.
2. **`kFreezeGain`, `kFreezeTilt` and `kWidthMaxCents` are procedures, not values.** The first two are measured in Task 6 Step 5 against a stated criterion, and the report must carry the table. `kWidthMaxCents` is left for the ear (§9) — 30 cents is a starting point, not a decision, and that has to be said when the branch is handed back.
3. **Task 6's per-octave test is the one place where a red result is the deliverable.** It is written to fail on the first run; Step 5's bisection is what turns it green. A report that shows it green without a measurement table means the constants were guessed.

Spec sections with no task, deliberately: **§5.11's "does a BBD deck want a tape echo after it"** stays a listening question (§9) — Task 10 only sets the default. **§5.12's CHOKE stage 1 question** is not touched: it is vacuous for a voiceless engine, and changing it affects every engine. **§5.13's CHOKE stage 2 degradation** stands as specified — `Part::max_voice_env()` loops over `SynthEngine::kVoices` and returns 0, so the decay window never opens, which is arguably correct: a frozen delay tail is infinite and would otherwise choke its neighbour forever.

Spec sections with no task: **§5.11's tape-echo-behind-the-engine default** is implemented in Task 10 Step 5, but "does a BBD deck want a tape echo after it" stays open for the ear (§9). **§5.12's CHOKE stage 1 question** is deliberately not touched: changing it affects every engine and the spec leaves it open.
