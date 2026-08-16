# SONG phrase flash Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** SONG lamps flash 150 ms on an A/B snapshot edge then go dark; STEPS lamps leave the FireflowHW plate.

**Architecture:** `led_law.hpp` replaces `phrase_on()` with a per-deck edge latch and remaining-time on `Panel`. `gen_hw_panel.py` drops `FLOW_*` from `KNOB_LAMPS` and from `HW_LIGHTS`. LightIds stay at 21; `fill()` still writes `FLOW_*` as 0.

**Tech Stack:** C++17 (`host/vcv/src/led_law.hpp`, doctest), Python 3 panel generator (`host/vcv/res/gen_hw_panel.py`).

## Global Constraints

- Spec: `docs/superpowers/specs/2026-08-16-song-phrase-flash-design.md`. Parent LED spec §3.2 / §3.5 / §4.4 already amended.
- `kSongFlash = 0.15f` seconds wall-clock. Hard on/off. A→B and B→A are the same flash. First `fill()` arms, no flash. Retrigger restarts the timer.
- `phrase_on()` is deleted. `Panel::blink` stays — REC still uses it.
- No engine change. `NUM_LIGHTS` stays 21. `FLOW_A_L` / `FLOW_B_L` remain in the enum.
- Files in the repo are English. Do not commit — the owner has not asked for a commit.
- Engine tests: clang + Ninja Release via `env.sh`, never MSVC. `-DCMAKE_BUILD_TYPE=Release` is required if configuring fresh.
- Never prefix a shell command with `cd`. Run `python host/vcv/res/gen_hw_panel.py` and `python host/vcv/res/test_hw_panel.py` from the repo root.
- VCV: `"C:\Program Files\Git\bin\bash.exe"` + `host/vcv/build-local.sh install`. Never system `g++`.
- Branch: stay on `hw-led-under-label`. Do not create a worktree.
- TDD: failing test first, watch it fail, then implement.
- A test that cannot go red gets fixed. Prove the RED once.

---

## File map

| File | Role |
|---|---|
| `tests/test_led_law.cpp` | S1–S3 replace G11 |
| `host/vcv/src/led_law.hpp` | flash law; delete `phrase_on` |
| `host/vcv/res/gen_hw_panel.py` | drop FLOW from cluster and `HW_LIGHTS` |
| `host/vcv/res/test_hw_panel.py` | S4 + inventory counts 19 drawn |
| `host/vcv/res/FireflowHW.svg`, `host/vcv/src/generated_hw_panel.hpp` | regenerated, never hand-edited |

---

### Task 1: SONG flash law

**Files:**
- Modify: `tests/test_led_law.cpp` (replace G11 with S1–S3)
- Modify: `host/vcv/src/led_law.hpp` (`phrase_on` gone; `Panel` latch; `fill()` flash)

**Interfaces:**
- Consumes: `Instrument::active_pattern(int p)`, `set_song`, `set_step`, `set_form`, `set_rate`, `set_shape`, `set_density`, `song_position_for_test` (all existing)
- Produces: `spkyled::kSongFlash` (`0.15f`); `Panel::{song_armed, song_pat, song_remain}[2]`; `fill()` writes `SONG_*_L` to `steps-1` during a flash else 0

- [ ] **Step 1: Write the failing tests**

Delete `TEST_CASE("led G11: the phrase lamps differ by shape, not by level")` entirely. Add `#include "mod/song_form.h"` if `SongMode` is not already visible. Add this helper and these three cases at the same place G11 lived:

```cpp
static void arm_song_deck(Instrument& inst) {
    inst.set_form(0, static_cast<int>(Principle::Hierarchical));
    inst.set_song(0, static_cast<int>(SongMode::AAAB));
    inst.set_rate(0, 1.f);
    inst.set_shape(0, 1.f);
    inst.set_density(0, 1.f);
    inst.set_step(0, true, 8);
}

static void process_until_pattern(Instrument& inst, uint8_t want) {
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 200000; ++i) {
        if (inst.active_pattern(0) == want) return;
        inst.process(nullptr, nullptr, &l, &r, 1);
    }
    FAIL("active_pattern did not reach ", (int)want, " within the safety bound");
}

TEST_CASE("led S1: a snapshot edge produces a flash, then dark") {
    Instrument inst;
    inst.init(48000.f);
    arm_song_deck(inst);
    process_until_pattern(inst, 0);

    spkyled::Panel panel;
    int duty[spkyvcv::NUM_LIGHTS] = {};
    const float dt = 1.f / 750.f;
    const int steps = 16;
    spkyled::fill(inst, panel, dt, steps, duty);
    CHECK(duty[spkyvcv::SONG_A_L] == 0);          // first fill arms, no flash

    float l = 0.f, r = 0.f;
    for (int i = 0; i < 200000; ++i) {
        inst.process(nullptr, nullptr, &l, &r, 1);
        if (inst.active_pattern(0) == 1) break;
    }
    REQUIRE(inst.active_pattern(0) == 1);
    spkyled::fill(inst, panel, dt, steps, duty);
    CHECK(duty[spkyvcv::SONG_A_L] == steps - 1);  // A→B flash

    const int hold = static_cast<int>(spkyled::kSongFlash / dt) + 2;
    for (int k = 0; k < hold; ++k)
        spkyled::fill(inst, panel, dt, steps, duty);
    CHECK(duty[spkyvcv::SONG_A_L] == 0);          // dark after 150 ms

    for (int i = 0; i < 200000; ++i) {
        inst.process(nullptr, nullptr, &l, &r, 1);
        if (inst.active_pattern(0) == 0) break;
    }
    REQUIRE(inst.active_pattern(0) == 0);
    spkyled::fill(inst, panel, dt, steps, duty);
    CHECK(duty[spkyvcv::SONG_A_L] == steps - 1);  // B→A same flash
}

TEST_CASE("led S2: no flash on first fill") {
    Instrument inst;
    inst.init(48000.f);
    arm_song_deck(inst);
    process_until_pattern(inst, 1);

    spkyled::Panel panel;
    int duty[spkyvcv::NUM_LIGHTS] = {};
    spkyled::fill(inst, panel, 1.f / 750.f, 16, duty);
    CHECK(duty[spkyvcv::SONG_A_L] == 0);
}

TEST_CASE("led S3: OFF stays dark") {
    Instrument inst;
    inst.init(48000.f);
    arm_song_deck(inst);
    inst.set_song(0, static_cast<int>(SongMode::Off));

    spkyled::Panel panel;
    int duty[spkyvcv::NUM_LIGHTS] = {};
    const float dt = 1.f / 750.f;
    float l = 0.f, r = 0.f;
    for (int i = 0; i < 50000; ++i) {
        inst.process(nullptr, nullptr, &l, &r, 1);
        if ((i % 64) == 0) {
            spkyled::fill(inst, panel, dt, 16, duty);
            CHECK(duty[spkyvcv::SONG_A_L] == 0);
        }
    }
}
```

S5 is the deletion of `phrase_on` in Step 3: after that, G11 would not compile, which is the proof. Do not leave a call to `phrase_on` anywhere.

- [ ] **Step 2: Run tests to verify they fail**

In Git Bash, with LLVM on PATH from `env.sh` (do not mix Daisy toolchain):

```
source env.sh
cmake --build build --target spky_tests
./build/spky_tests.exe --test-case="led S1*" --test-case="led S2*" --test-case="led S3*"
```

On PowerShell without `source`, use the same `env.sh` via Git Bash. Expected: FAIL — `kSongFlash` is undeclared, and/or `SONG_A_L` still follows the old double-pulse (S1's post-arm A→B check is not `steps-1` for a single `dt`, or G11 is gone and the new cases fail to compile). The failure must be the missing flash law, not a typo. If S1/S2/S3 pass against the old law, the tests are wrong — stop and report.

If `spky_tests` is missing, configure first:

```
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target spky_tests
```

- [ ] **Step 3: Minimal implementation**

In `host/vcv/src/led_law.hpp`:

1. Delete `phrase_on` entirely (the function and its comment).
2. After `kTempoPulse`, add:

```cpp
constexpr float kSongFlash = 0.15f;
```

3. Extend `Panel`:

```cpp
struct Panel {
    Lamp  lamp[spkyvcv::NUM_LIGHTS];
    float blink = 0.f;
    bool    song_armed[2]  = {false, false};
    uint8_t song_pat[2]    = {0, 0};
    float   song_remain[2] = {0.f, 0.f};
};
```

4. Replace the SONG block in `fill()` (the `songId` loop) with:

```cpp
    const int songId[2] = {SONG_A_L, SONG_B_L};
    for (int part = 0; part < 2; ++part) {
        const uint8_t now = inst.active_pattern(part);
        if (!p.song_armed[part]) {
            p.song_pat[part] = now;
            p.song_armed[part] = true;
        } else if (now != p.song_pat[part]) {
            p.song_pat[part] = now;
            p.song_remain[part] = kSongFlash;
        }
        if (p.song_remain[part] > 0.f) {
            duty_out[songId[part]] = steps - 1;
            p.song_remain[part] -= dt;
            if (p.song_remain[part] < 0.f) p.song_remain[part] = 0.f;
        } else {
            duty_out[songId[part]] = 0;
        }
    }
```

Keep `FLOW_*` on the zero list. Keep `blink` for REC. Do not touch TEMPO, GATE, CEIL, excursion, REC.

- [ ] **Step 4: Run tests to verify they pass**

```
source env.sh
cmake --build build --target spky_tests
./build/spky_tests.exe --test-case="led S1*" --test-case="led S2*" --test-case="led S3*" --test-case="led G6*" --test-case="led G11*"
```

Expected: S1–S3 PASS. G6 still PASS (every light written, including `FLOW_*` at 0). G11 is gone — doctest should report 0 cases for that filter, not an error. Then:

```
./build/spky_tests.exe --test-case="led*"
```

Expected: all remaining `led*` cases PASS.

- [ ] **Step 5: Do not commit**

Stop. The owner has not asked for a commit.

---

### Task 2: STEPS lamp off the HW plate

**Files:**
- Modify: `host/vcv/res/test_hw_panel.py`
- Modify: `host/vcv/res/gen_hw_panel.py`

**Interfaces:**
- Consumes: `KNOB_LAMPS`, `hw_label`, `HW_LIGHTS` from Task 1's tree (law already landed; this task does not edit `led_law.hpp`)
- Produces: `FLOW_A_L` / `FLOW_B_L` absent from `KNOB_LAMPS` and from `HW_LIGHTS`; STPS caption on the knob x

- [ ] **Step 1: Write the failing guards**

In `test_same_runtime_params_same_order`, change the HW_LIGHTS assertion so FLOW is excluded from the *expected* list (the generator still concatenates everything until Step 3, so this goes red):

```python
    flow = {"FLOW_A_L", "FLOW_B_L"}
    assert [c.enum for c in hw.HW_LIGHTS] == \
           [c.enum for c in gp.LIGHTS] + \
           [c.enum for c in gp.HW_ONLY_LIGHTS if c.enum not in flow]
```

In `test_hw_only_inventory`, the drawn-LED count becomes 19 (21 IDs minus the two undrawn FLOW lamps). Update the comment and:

```python
    check(total_leds == 19, f"expected 19 LEDs on the plate, got {total_leds}")
```

In `test_led_inventory_after_the_feedback_round`:

```python
    check(len(hw.HW_LIGHTS) == 19, f"{len(hw.HW_LIGHTS)} lights, expected 19")
```

Add:

```python
def test_steps_has_no_lamp_on_the_hw_plate():
    """Spec 2026-08-16 song-phrase-flash S4: FLOW lamps are not drawn;
    STPS captions sit on the knob x."""
    names = {c.enum for c in hw.HW_LIGHTS}
    check("FLOW_A_L" not in names and "FLOW_B_L" not in names,
          "FLOW_* still drawn on FireflowHW")
    check("STEPS_A" not in hw.KNOBS_WITH_LAMPS and
          "STEPS_B" not in hw.KNOBS_WITH_LAMPS,
          "STEPS is still in KNOBS_WITH_LAMPS")
    by = {c.enum: c for c in hw.HW_PARAMS}
    for enum in ("STEPS_A", "STEPS_B"):
        lx, ly = hw.hw_label(by[enum])[:2]
        check(abs(lx - by[enum].x) < 1e-6,
              f"{enum} caption x={lx:.2f} is not on the knob ({by[enum].x:.2f})")
```

Do not change `test_knob_lamps_sit_in_the_caption_cluster` — it iterates `KNOB_LAMPS`, which still contains FLOW until Step 3, so it stays green until then.

- [ ] **Step 2: Run guards to verify they fail**

```
python host/vcv/res/test_hw_panel.py
```

Expected: FAIL with FLOW still in `HW_LIGHTS` and/or `total_leds` 21 vs 19 and/or STPS caption not on knob x (cluster still shifts it).

- [ ] **Step 3: Minimal generator change**

In `host/vcv/res/gen_hw_panel.py` `KNOB_LAMPS`, delete the `FLOW_A_L` / `FLOW_B_L` entries.

Replace

```python
HW_LIGHTS  = [place(c) for c in gp.LIGHTS + gp.HW_ONLY_LIGHTS]
```

with

```python
_SKIP_HW_LIGHTS = {"FLOW_A_L", "FLOW_B_L"}
HW_LIGHTS  = [place(c) for c in gp.LIGHTS + gp.HW_ONLY_LIGHTS
              if c.enum not in _SKIP_HW_LIGHTS]
```

Do not remove the Ctls from `gen_panel.py` `HW_ONLY_LIGHTS` — that would drop the LightIds. Do not add FLOW to `LIGHT_POS`.

- [ ] **Step 4: Run guards (will still fail committed-files until Task 3 regen)**

```
python host/vcv/res/test_hw_panel.py
```

Expected: S4 and inventory pass. `test_committed_files_match_the_generator` FAIL because SVG/header are stale — that is Task 3. If anything else fails, fix it here.

- [ ] **Step 5: Do not commit**

Stop.

---

### Task 3: Regenerate the plate and install the plugin

**Files:**
- Generate: `host/vcv/res/FireflowHW.svg`
- Generate: `host/vcv/src/generated_hw_panel.hpp`

**Interfaces:**
- Consumes: Task 2's generator
- Produces: matching SVG/header; VCV plugin in Rack's user dir

- [ ] **Step 1: Regenerate**

```
python host/vcv/res/gen_hw_panel.py
```

Expected: `wrote res/FireflowHW.svg and src/generated_hw_panel.hpp` and `lights=19` (or still prints 21 if the print uses the enum — if it prints 21, that is `gp.LIGHTS + HW_ONLY_LIGHTS` length; the drawn table is `HW_LIGHTS`. Do not "fix" the print unless it is clearly counting `HW_LIGHTS`).

Confirm the header has no `FLOW_A_L` row in `kLightCtls` and still has the `FLOW_A_L` enumerator in `generated_panel.hpp` (untouched).

- [ ] **Step 2: Panel guards green**

```
python host/vcv/res/test_hw_panel.py
```

Expected: `PASS -- hw panel guards ok`

- [ ] **Step 3: Install VCV**

PowerShell:

```
& "C:\Program Files\Git\bin\bash.exe" host/vcv/build-local.sh install
```

Expected: exit 0, `Restart Rack.`

- [ ] **Step 4: Do not commit**

Stop.
