# Panel Scan, Part 1 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The shell's first control path from a mux channel to the engine: a
measurement image (`SHELL_SCAN_CHECK`) that proves the scan pattern on the
test coupon, and a playing image (`SHELL_PANEL_SCAN`) in which the coupon's
RV2, RV4 and RV6 drive `RATE_A`, `DENSITY_A` and `FILT_A`.

**Architecture:** The scan runs first in the audio callback: read the step
selected one block earlier from libDaisy's free-running DMA buffer, then
select the next. Hardware-free logic (which sense pin is live, the value
path, the control table, the check image's block layout) lives in host-tested
units compiled into `spky_tests`. Board code is thin and gated behind
generated switch headers.

**Tech Stack:** C++17 (clang/Ninja for the host, ARM GCC via `make` for the
Daisy), doctest, Python 3 readers with plain-script guards run by ctest.

**Spec:** `docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md`

## Global Constraints

- Everything written into the repo is English: code, comments, docs, commit messages.
- Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- Never prefix a shell command with `cd`. Firmware builds run as `make -C shell ...` from the repo root.
- Host toolchain: `source env.sh`, then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, `cmake --build build`, `ctest --test-dir build --output-on-failure`. **ctest does not build**: always run `cmake --build build` first.
- Firmware toolchain, never in a shell that sourced `env.sh`: `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images <SWITCHES>`.
- Build switches are generated headers written at Makefile parse time; a switch header's writer deletes every object in `SWITCH_OBJECTS` when its content changes. Never replace this with a rule or a bare `-D`.
- Before any flash, `cmp` images built from different switch positions: they must differ.
- The scan runs **first** in the callback, before `inst.process()`, in both new images. `SHELL_MUX_PROBE`'s images keep their placement after `process()`.
- The scan reads the raw DMA word `hw.adc.Get(i)`, never `hw.GetAdcValue(i)`.
- No hardware header in any host-tested file (`mux_plan`, `scan_value`, `controls`, `scan_check_plan`).
- The criterion is **|mean_S − mean_P| ≤ 8 counts**. G3 qualifying threshold: predecessor in the same group, P means more than **4096** counts apart; at least **3** qualifying steps.
- Hysteresis H: the largest `max − min` in arm S over the seven pot steps, across all complete blocks of the committed capture, rounded up to a multiple of 16, at least 16.
- The panel span constant is `{0, 63485, valid}`, labelled as the coupon's 2026-09-17 rail reading.
- The coupon table: RV2 (group 0, ch 2) → `P_RATE_A`, RV4 (group 0, ch 6) → `P_DENSITY_A`, RV6 (group 1, ch 2) → `P_FILT_A`.
- A test that cannot go red gets fixed. Prove each new guard's RED once.

## File Structure

| file | status | responsibility |
|---|---|---|
| `shell/mux_plan.{h,cpp}` | modify | + `sense_live()` |
| `shell/mux_scan.{h,cpp}` | modify | raw reads of live pins, `select()`, `read_step()`, `step()` returns the step read, `g_mux_raw[]`, LED walk switchable |
| `shell/scan_value.{h,cpp}` | create | `span_normalize()`, `PotFilter`, `pot_filter()`, `kPanelSpan`, later `kPotHysteresis` |
| `shell/controls.{h,cpp}` | rewrite | `ControlEntry`, `ControlTable`, `kCouponTable`, `kPanelTable`, `find_control()`, `control_value()`, `apply_control()` |
| `shell/scan_check_plan.{h,cpp}` | create | `ScanArm`, `CheckSlot`, `check_slot()`, block constants |
| `shell/scan_check.{h,cpp}` | create | check image: `scan_check_init()`, `scan_check_tick()`, `run_scan_check_report()` |
| `shell/panel_scan.{h,cpp}` | create | playing image: `panel_scan_init()`, `panel_scan_tick()`, `run_panel_scan_report()` |
| `shell/write_shell_scan_check.py`, `shell/write_shell_panel_scan.py` | create | switch header writers |
| `shell/Makefile`, `shell/main.cpp`, `shell/write_git_hash.py` (caller list only, in Makefile) | modify | wiring |
| `shell/read_scan_check.py`, `shell/test_read_scan_check.py` | create | reader, gates, H; guard |
| `tests/test_mux_plan.cpp` | modify | `sense_live` cases |
| `tests/test_scan_value.cpp`, `tests/test_scan_check_plan.cpp` | create | host tests |
| `tests/test_controls_map.cpp` | rewrite | table tests |
| `CMakeLists.txt` | modify | new test sources, `read_scan_check_guard` |
| `docs/hardware/scan-measured.md`, `docs/hardware/captures/scan-check-capture-<hash>.txt` | create | write-up, raw capture |
| `docs/hardware/scan-budget.md`, `docs/hardware/captures/README.md`, `shell/README.md`, `docs/roadmap.md` | modify | status |

---

### Task 1: The scan core reads what it claims to read

**Files:**
- Modify: `shell/mux_plan.h`, `shell/mux_plan.cpp`
- Modify: `shell/mux_scan.h`, `shell/mux_scan.cpp`
- Test: `tests/test_mux_plan.cpp`

**Interfaces:**
- Produces: `bool shell::sense_live(const ChainProfile& p, int step, int sense);`
- Produces (board): `void MuxScan::select(int step);`, `void MuxScan::read_step(bench::Board& hw, int step);`, `int MuxScan::step(bench::Board& hw);` (returns the step whose values it just stored, or −1), `int MuxScan::live_step() const;`, `void MuxScan::set_walk_leds(bool on);`, `extern volatile uint16_t shell::g_mux_raw[mux_total(kActiveChain)];` (replaces `g_mux_values`).

- [ ] **Step 1: Write the failing test.** Append to `tests/test_mux_plan.cpp`:

```cpp
TEST_CASE("mux plan: on the panel every sense pin is live on every step") {
    const shell::ChainProfile& p = shell::kPanelChain;
    for(int s = 0; s < shell::scan_steps(p); ++s)
        for(int pin = 0; pin < p.sense_pins; ++pin)
            CHECK(shell::sense_live(p, s, pin));
}

TEST_CASE("mux plan: on the coupon only the group's own sense pin is live") {
    // Group 0 (the 4067) is wired to ADC_9 only, group 1 (the 4051) to
    // ADC_10 only. The other pin's mux is disabled during the step, so its
    // node floats, and a scan that stored it would store noise under a real
    // channel's index.
    const shell::ChainProfile& p = shell::kCouponChain;
    for(int s = 0; s < 16; ++s)
    {
        CHECK(shell::sense_live(p, s, 0));
        CHECK_FALSE(shell::sense_live(p, s, 1));
    }
    for(int s = 16; s < 24; ++s)
    {
        CHECK_FALSE(shell::sense_live(p, s, 0));
        CHECK(shell::sense_live(p, s, 1));
    }
}

TEST_CASE("mux plan: sense_live refuses what does not exist") {
    for(const auto& p : kProfiles)
    {
        CHECK_FALSE(shell::sense_live(p, -1, 0));
        CHECK_FALSE(shell::sense_live(p, shell::scan_steps(p), 0));
        CHECK_FALSE(shell::sense_live(p, 0, -1));
        CHECK_FALSE(shell::sense_live(p, 0, p.sense_pins));
    }
}
```

- [ ] **Step 2: Run it to verify it fails.**

Run: `source env.sh && cmake --build build 2>&1 | tail -5`
Expected: compile error, `sense_live` is not a member of `shell`.

- [ ] **Step 3: Implement `sense_live`.** In `shell/mux_plan.h`, after the `mux_channel` declaration, add:

```cpp
// Whether sense pin `sense` carries a live channel during `step`: the step's
// group is the one enabled, and that group is wired to this pin (or to all
// of them, sense_of_group == -1). mux_channel() deliberately ignores the
// wiring; a reader that stores values must ask this first, because on the
// coupon the other pin's mux is disabled and its node floats.
bool sense_live(const ChainProfile& p, int step, int sense);
```

In `shell/mux_plan.cpp`, before `chain_word`, add:

```cpp
bool sense_live(const ChainProfile& p, int step, int sense)
{
    const int g = group_of_step(p, step);
    if(g < 0 || sense < 0 || sense >= p.sense_pins) return false;
    const int wired = p.sense_of_group[g];
    return wired < 0 || wired == sense;
}
```

- [ ] **Step 4: Run the tests to verify they pass.**

Run: `source env.sh && cmake --build build && ctest --test-dir build -R spky_tests --output-on-failure 2>&1 | tail -5`
Expected: `100% tests passed`.

- [ ] **Step 5: Rework `MuxScan`.** In `shell/mux_scan.h`:

Replace the class's public block from `void step(bench::Board& hw);` through `uint32_t steps() const { return steps_; }` with:

```cpp
    // Read the step selected last time, then select the next one. Returns
    // the step whose values were just stored in g_mux_raw, or -1 on the
    // first call (nothing was selected yet).
    //
    // The read is the RAW DMA word, hw.adc.Get(). Until 2026-09-28 this read
    // hw.GetAdcValue(), which returns libDaisy's AnalogControl::Value() -- a
    // filtered value that only moves when ProcessAnalogControls() runs, and
    // nothing in shell/ calls it. The CPU run of 2026-08-23 priced those
    // reads and was not affected; every value they stored was 0. The slew
    // filter behind AnalogControl must never sit behind a mux in any case:
    // it would average across channel changes.
    // (spec 2026-09-28-coupon-panel-scan-design.md section 2)
    int step(bench::Board& hw);

    // The two halves of step(), for the scan-check image, whose arms park,
    // re-order and repeat them.
    void select(int step);
    void read_step(bench::Board& hw, int step);

    int      live_step() const { return live_step_; }
    uint32_t steps() const { return steps_; }

    // step() walks the LED field by default, as the 2026-08-23 CPU probe
    // needs: a constant word would let the compiler hoist the loop's work
    // and price a scan nobody ships. The panel-scan images turn it off and
    // keep the coupon's LEDs dark, so both images put the same digital load
    // beside the analog read.
    void set_walk_leds(bool on) { walk_leds_ = on; }
```

In the private block, add `bool walk_leds_ = true;` after `uint32_t steps_ = 0;`.

Replace the `g_mux_values` declaration and its comment at the end of the file with:

```cpp
// Where the scan puts what it read: the raw 16-bit DMA word per channel,
// indexed by mux_channel(). Only live (step, sense) pairs are ever written.
// Volatile so a build that does not use the values still performs the reads
// -- the CPU probe images deliberately do NOT push them into the engine.
extern volatile uint16_t g_mux_raw[mux_total(kActiveChain)];
```

Change the comment line `// two objects gives g_mux_values two sizes in one link.` to `// two objects gives g_mux_raw two sizes in one link.`

In `shell/mux_scan.cpp`, replace `volatile float g_mux_values[mux_total(kActiveChain)] = {};` with `volatile uint16_t g_mux_raw[mux_total(kActiveChain)] = {};`, and replace the whole `MuxScan::step` definition with:

```cpp
void MuxScan::select(int step)
{
    const StepPattern p = step_pattern(kActiveChain, step);
    write_chain(chain_word(kActiveChain, p, leds_));
    live_step_ = step;
}

void MuxScan::read_step(bench::Board& hw, int step)
{
    for(int s = 0; s < kActiveChain.sense_pins; ++s)
    {
        if(!sense_live(kActiveChain, step, s)) continue;
        const int ch = mux_channel(kActiveChain, step, s);
        if(ch >= 0)
            g_mux_raw[ch] = hw.adc.Get(static_cast<uint8_t>(kSenseAdcBase + s));
    }
}

int MuxScan::step(bench::Board& hw)
{
    const int read = live_step_;
    if(read >= 0) read_step(hw, read);

    // The LED field changes every step in the CPU probe, as it does in
    // production -- see set_walk_leds() in the header.
    if(walk_leds_)
        leds_ = (leds_ + 1u) & ((1u << kActiveChain.led_bits) - 1u);

    select(next_step_);
    next_step_ = (next_step_ + 1) % scan_steps(kActiveChain);
    ++steps_;
    return read;
}
```

- [ ] **Step 6: Build the firmware images that compile `MuxScan`.**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_CPU_PROBE=1 SHELL_MUX_PROBE=1 2>&1 | tail -3
```
Expected: links without errors (`shell-sram.bin` written). Then build the default image the same way with no switches; expected: links without errors.

- [ ] **Step 7: Commit.**

```bash
git add shell/mux_plan.h shell/mux_plan.cpp shell/mux_scan.h shell/mux_scan.cpp tests/test_mux_plan.cpp
git commit -m "fix(shell): the mux scan reads raw DMA words, live pins only

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: The value path and the control table

**Files:**
- Create: `shell/scan_value.h`, `shell/scan_value.cpp`, `tests/test_scan_value.cpp`
- Rewrite: `shell/controls.h`, `shell/controls.cpp`, `tests/test_controls_map.cpp`
- Modify: `CMakeLists.txt` (spky_tests sources)

**Interfaces:**
- Consumes: `shell::Span` from `shell/coupon_expect.h` (`uint16_t zero, rail; bool valid;`); `spky::kParams`, `spky::apply_param`, `spky::P_RATE_A`, `spky::P_DENSITY_A`, `spky::P_FILT_A`, `spky::P_COUNT` from `engine/param_table.h`; `shell::kPots` from `shell/pot_plan.h`.
- Produces:
  - `float shell::span_normalize(uint16_t raw, const Span& span);`
  - `struct shell::PotFilter { uint16_t last_raw; float last_v; bool emitted; };`
  - `bool shell::pot_filter(PotFilter& f, uint16_t raw, const Span& span, int hysteresis, float* out);`
  - `inline constexpr Span shell::kPanelSpan{0, 63485, true};`
  - `struct shell::ControlEntry { int group; int ch; int param; };`
  - `struct shell::ControlTable { const ControlEntry* entries; int count; };`
  - `inline constexpr ControlTable shell::kCouponTable, shell::kPanelTable;`
  - `const ControlEntry* shell::find_control(const ControlTable& t, int group, int ch);`
  - `float shell::control_value(int param, float v);`
  - `void shell::apply_control(const ControlEntry& e, float v, spky::Instrument& inst);`
  - `shell::map_control()` is removed.

- [ ] **Step 1: Write the failing tests.** Create `tests/test_scan_value.cpp`:

```cpp
// The value path from a raw mux word to a normalised control value. Pure
// data logic, no hardware type: on the board a wrong clamp or a band that
// never lets go shows up as "the knob does not reach its stop", which is
// expensive to find. Spec: docs/superpowers/specs/
// 2026-09-28-coupon-panel-scan-design.md section 5.
#include <doctest/doctest.h>
#include "../shell/scan_value.h"

namespace {
constexpr shell::Span kSpan{100, 60100, true};   // 60000 counts wide
constexpr int kH = 32;
}

TEST_CASE("scan value: normalise maps the span onto 0..1 and clamps") {
    CHECK(shell::span_normalize(100, kSpan) == doctest::Approx(0.0f));
    CHECK(shell::span_normalize(60100, kSpan) == doctest::Approx(1.0f));
    CHECK(shell::span_normalize(30100, kSpan) == doctest::Approx(0.5f));
    CHECK(shell::span_normalize(0, kSpan) == doctest::Approx(0.0f));
    CHECK(shell::span_normalize(65535, kSpan) == doctest::Approx(1.0f));
}

TEST_CASE("scan value: an invalid or inverted span normalises to 0") {
    const shell::Span invalid{100, 60100, false};
    const shell::Span inverted{60100, 100, true};
    CHECK(shell::span_normalize(30100, invalid) == doctest::Approx(0.0f));
    CHECK(shell::span_normalize(30100, inverted) == doctest::Approx(0.0f));
}

TEST_CASE("scan value: nothing is emitted before the span is valid") {
    shell::PotFilter f{};
    float v = -1.0f;
    const shell::Span invalid{100, 60100, false};
    CHECK_FALSE(shell::pot_filter(f, 30100, invalid, kH, &v));
    CHECK(v == doctest::Approx(-1.0f));
    CHECK_FALSE(f.emitted);
}

TEST_CASE("scan value: the first valid reading is always emitted") {
    shell::PotFilter f{};
    float v = -1.0f;
    CHECK(shell::pot_filter(f, 30100, kSpan, kH, &v));
    CHECK(v == doctest::Approx(0.5f));
}

TEST_CASE("scan value: a move inside the band is silent, beyond it emits") {
    shell::PotFilter f{};
    float v = 0.0f;
    REQUIRE(shell::pot_filter(f, 30100, kSpan, kH, &v));
    CHECK_FALSE(shell::pot_filter(f, 30100 + kH, kSpan, kH, &v));
    CHECK_FALSE(shell::pot_filter(f, 30100 - kH, kSpan, kH, &v));
    CHECK(shell::pot_filter(f, 30100 + kH + 1, kSpan, kH, &v));
    CHECK(v == doctest::Approx((30000.0f + kH + 1) / 60000.0f));
}

TEST_CASE("scan value: the band is measured from the last EMITTED reading") {
    // A slow drift of half a band per read must still emit once it has
    // added up; measuring from the last SEEN reading would never emit.
    shell::PotFilter f{};
    float v = 0.0f;
    REQUIRE(shell::pot_filter(f, 30100, kSpan, kH, &v));
    CHECK_FALSE(shell::pot_filter(f, 30100 + kH / 2, kSpan, kH, &v));
    CHECK_FALSE(shell::pot_filter(f, 30100 + kH, kSpan, kH, &v));
    CHECK(shell::pot_filter(f, 30100 + kH + kH / 2, kSpan, kH, &v));
}

TEST_CASE("scan value: both stops are reachable through the band") {
    shell::PotFilter f{};
    float v = 0.5f;
    // Just outside the low snap region, then one count into it: a move of 1,
    // far inside the band, must still deliver an exact 0.
    REQUIRE(shell::pot_filter(f, 100 + kH + 1, kSpan, kH, &v));
    CHECK(v > 0.0f);
    CHECK(shell::pot_filter(f, 100 + kH, kSpan, kH, &v));
    CHECK(v == 0.0f);
    // Staying in the region is silent.
    CHECK_FALSE(shell::pot_filter(f, 100, kSpan, kH, &v));

    shell::PotFilter g{};
    REQUIRE(shell::pot_filter(g, 60100 - kH - 1, kSpan, kH, &v));
    CHECK(v < 1.0f);
    CHECK(shell::pot_filter(g, 60100 - kH, kSpan, kH, &v));
    CHECK(v == 1.0f);
    CHECK_FALSE(shell::pot_filter(g, 60100, kSpan, kH, &v));
}

TEST_CASE("scan value: the panel span is the coupon's rail reading") {
    CHECK(shell::kPanelSpan.zero == 0);
    CHECK(shell::kPanelSpan.rail == 63485);
    CHECK(shell::kPanelSpan.valid);
}
```

Replace `tests/test_controls_map.cpp` entirely with:

```cpp
// The control table: which (group, channel) drives which parameter, and how
// a 0..1 value is scaled into that parameter's range. Pure data logic, host
// tested: on the board a wrong row is only audible as "the knob does the
// wrong thing". Spec: docs/superpowers/specs/
// 2026-09-28-coupon-panel-scan-design.md sections 3 and 5.
#include <doctest/doctest.h>
#include "../shell/controls.h"
#include "../shell/pot_plan.h"
#include "instrument.h"

TEST_CASE("controls: the coupon table maps RV2, RV4 and RV6") {
    const shell::ControlTable& t = shell::kCouponTable;
    REQUIRE(t.count == 3);
    CHECK(t.entries[0].param == spky::P_RATE_A);
    CHECK(t.entries[1].param == spky::P_DENSITY_A);
    CHECK(t.entries[2].param == spky::P_FILT_A);
}

TEST_CASE("controls: the coupon table's channels are pot_plan.h's pots") {
    // One source of truth for where the pots sit: the pot round's table.
    const shell::ControlTable& t = shell::kCouponTable;
    REQUIRE(t.count == shell::kPotCount);
    for(int i = 0; i < t.count; ++i)
    {
        CHECK(t.entries[i].group == shell::kPots[i].group);
        CHECK(t.entries[i].ch == shell::kPots[i].channel);
    }
}

TEST_CASE("controls: the panel table is empty until part 2") {
    CHECK(shell::kPanelTable.count == 0);
    CHECK(shell::find_control(shell::kPanelTable, 0, 0) == nullptr);
}

TEST_CASE("controls: find_control answers only for mapped channels") {
    const shell::ControlTable& t = shell::kCouponTable;
    REQUIRE(shell::find_control(t, 0, 6) != nullptr);
    CHECK(shell::find_control(t, 0, 6)->param == spky::P_DENSITY_A);
    CHECK(shell::find_control(t, 0, 3) == nullptr);    // a rail tie
    CHECK(shell::find_control(t, 1, 6) == nullptr);    // group 1's divider
    CHECK(shell::find_control(t, 5, 0) == nullptr);
    CHECK(shell::find_control(t, -1, 2) == nullptr);
}

TEST_CASE("controls: values scale into the parameter's own range") {
    CHECK(shell::control_value(spky::P_RATE_A, 0.25f) == doctest::Approx(0.25f));
    CHECK(shell::control_value(spky::P_FILT_A, 0.0f) == doctest::Approx(-1.0f));
    CHECK(shell::control_value(spky::P_FILT_A, 0.5f) == doctest::Approx(0.0f));
    CHECK(shell::control_value(spky::P_FILT_A, 1.0f) == doctest::Approx(1.0f));
    CHECK(shell::control_value(-1, 0.5f) == doctest::Approx(0.0f));
    CHECK(shell::control_value(spky::P_COUNT, 0.5f) == doctest::Approx(0.0f));
}

TEST_CASE("controls: applying RV2's entry moves part A's rate and only it") {
    spky::Instrument inst;
    inst.init(48000.0f);
    const float b_before = inst.rate(spky::PART_B);
    const shell::ControlEntry* e = shell::find_control(shell::kCouponTable, 0, 2);
    REQUIRE(e != nullptr);
    shell::apply_control(*e, 0.75f, inst);
    CHECK(inst.rate(spky::PART_A) == doctest::Approx(0.75f));
    CHECK(inst.rate(spky::PART_B) == doctest::Approx(b_before));
}
```

In `CMakeLists.txt`, in the `spky_tests` source list, after the line `    tests/test_pot_plan.cpp`, add:

```
    shell/scan_value.cpp
    tests/test_scan_value.cpp
```

- [ ] **Step 2: Run them to verify they fail.**

Run: `source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build 2>&1 | tail -5`
Expected: compile errors (`scan_value.h` missing, `kCouponTable` not declared).

- [ ] **Step 3: Create `shell/scan_value.h`:**

```cpp
#pragma once

// The value path from a raw mux word to a normalised control value: the
// span, a clamp, a hysteresis band and a snap at both stops. No hardware
// type, host tested (tests/test_scan_value.cpp) -- the same arrangement as
// mux_plan.h and controls.h.
//
// Spec: ../docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md
// section 5.
#include <cstdint>
#include "coupon_expect.h"

namespace shell {

// The panel's span until part 2 gives the control PCB its own tie channels
// (spec section 8): the rail the coupon's 0 ohm ties read through libDaisy's
// path on 2026-09-17, 63485 of 65535. One board's reading, not a property of
// the design.
inline constexpr Span kPanelSpan{0, 63485, true};

// (raw - zero) / (rail - zero), clamped to 0..1. The clamp belongs here, on
// the reading side: only the reader knows the span. An invalid or inverted
// span gives 0 rather than a division by zero or a negative scale.
float span_normalize(uint16_t raw, const Span& span);

struct PotFilter
{
    uint16_t last_raw = 0;      // the raw word of the last EMITTED value
    float    last_v   = 0.0f;
    bool     emitted  = false;
};

// Whether this reading should reach the engine. Emits (true, *out = v) when
//   - the span is valid, and
//   - the channel has never emitted, or the raw word has moved more than
//     `hysteresis` counts from the last EMITTED raw word, or the reading lies
//     in a snap region whose value (0 or 1) differs from the last emitted
//     one -- so a stop is reachable from inside the band.
// Snap regions: raw <= zero + hysteresis is 0, raw >= rail - hysteresis is 1.
bool pot_filter(PotFilter& f, uint16_t raw, const Span& span, int hysteresis,
                float* out);

} // namespace shell
```

Create `shell/scan_value.cpp`:

```cpp
#include "scan_value.h"

namespace shell {

float span_normalize(uint16_t raw, const Span& span)
{
    if(!span.valid || span.rail <= span.zero) return 0.0f;
    const float v = (static_cast<float>(raw) - static_cast<float>(span.zero))
                    / (static_cast<float>(span.rail)
                       - static_cast<float>(span.zero));
    if(v < 0.0f) return 0.0f;
    if(v > 1.0f) return 1.0f;
    return v;
}

bool pot_filter(PotFilter& f, uint16_t raw, const Span& span, int hysteresis,
                float* out)
{
    if(!span.valid || span.rail <= span.zero) return false;

    const int r = static_cast<int>(raw);
    float     v;
    bool      snapped = true;
    if(r <= static_cast<int>(span.zero) + hysteresis)
        v = 0.0f;
    else if(r >= static_cast<int>(span.rail) - hysteresis)
        v = 1.0f;
    else
    {
        v       = span_normalize(raw, span);
        snapped = false;
    }

    const int  d     = r - static_cast<int>(f.last_raw);
    const bool moved = d > hysteresis || -d > hysteresis;
    const bool stop  = snapped && v != f.last_v;
    if(f.emitted && !moved && !stop) return false;

    f.emitted  = true;
    f.last_raw = raw;
    f.last_v   = v;
    *out       = v;
    return true;
}

} // namespace shell
```

Replace `shell/controls.h` entirely with:

```cpp
#pragma once

// Mux channel -> engine parameter, per board, as data. No hardware type:
// tests/test_controls_map.cpp holds it on the host. A knob that does the
// wrong thing is only audible on a board and expensive to find; here it is
// one row.
//
// The coupon's table maps its three measured pots (pot_plan.h), Bastian's
// choice of 2026-09-28. The panel's table is empty: which pot sits on which
// mux channel is the control PCB's pin map, part 2 of the panel scan, and
// filling it before that exists would decide the routing in code.
//
// Spec: ../docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md
#include "instrument.h"
#include "param_table.h"

namespace shell {

struct ControlEntry
{
    int group;   // mux group (chip) as in mux_plan.h
    int ch;      // channel on that chip
    int param;   // spky::ParamId
};

struct ControlTable
{
    const ControlEntry* entries;
    int                 count;
};

inline constexpr ControlEntry kCouponControls[] = {
    {0, 2, spky::P_RATE_A},      // RV2, 10 k, on the 4067
    {0, 6, spky::P_DENSITY_A},   // RV4, 20 k, on the 4067
    {1, 2, spky::P_FILT_A},      // RV6, 10 k, on the 4051
};

inline constexpr ControlTable kCouponTable{
    kCouponControls,
    static_cast<int>(sizeof(kCouponControls) / sizeof(kCouponControls[0]))};

inline constexpr ControlTable kPanelTable{nullptr, 0};

// The entry for (group, ch), or nullptr. A channel that is not in the table
// changes nothing: a half-seated chip produces indices nobody planned, and
// guessing would hand a foreign knob's voltage to a parameter.
const ControlEntry* find_control(const ControlTable& t, int group, int ch);

// lo + v * (hi - lo) of the parameter's range in param_table.h, or 0 for a
// parameter that does not exist.
float control_value(int param, float v);

// Scales v into the entry's parameter range and routes it via apply_param().
void apply_control(const ControlEntry& e, float v, spky::Instrument& inst);

} // namespace shell
```

Replace `shell/controls.cpp` entirely with:

```cpp
#include "controls.h"

namespace shell {

const ControlEntry* find_control(const ControlTable& t, int group, int ch)
{
    for(int i = 0; i < t.count; ++i)
        if(t.entries[i].group == group && t.entries[i].ch == ch)
            return &t.entries[i];
    return nullptr;
}

float control_value(int param, float v)
{
    if(param < 0 || param >= spky::P_COUNT) return 0.0f;
    const spky::ParamInfo& pi = spky::kParams[param];
    return pi.lo + v * (pi.hi - pi.lo);
}

void apply_control(const ControlEntry& e, float v, spky::Instrument& inst)
{
    // No clamp here: v arrives clamped from scan_value's span_normalize(),
    // and apply_param() clamps to the table range once more.
    spky::apply_param(inst, e.param, control_value(e.param, v));
}

} // namespace shell
```

- [ ] **Step 4: Run the tests to verify they pass.**

Run: `source env.sh && cmake --build build && ctest --test-dir build -R spky_tests --output-on-failure 2>&1 | tail -5`
Expected: `100% tests passed`.

- [ ] **Step 5: Prove one RED.** Temporarily change `{0, 6, spky::P_DENSITY_A}` to `{0, 5, spky::P_DENSITY_A}` in `shell/controls.h`, rebuild, run `spky_tests`: the pot_plan agreement test and the find_control test must fail. Restore `{0, 6, ...}` with an edit (never `git checkout` the file), rebuild, rerun: green.

- [ ] **Step 6: Commit.**

```bash
git add shell/scan_value.h shell/scan_value.cpp shell/controls.h shell/controls.cpp tests/test_scan_value.cpp tests/test_controls_map.cpp CMakeLists.txt
git commit -m "feat(shell): the value path and a per-board control table

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The check image's block layout

**Files:**
- Create: `shell/scan_check_plan.h`, `shell/scan_check_plan.cpp`, `tests/test_scan_check_plan.cpp`
- Modify: `CMakeLists.txt` (spky_tests sources)

**Interfaces:**
- Consumes: `shell::scan_steps`, `shell::kCouponChain` from `mux_plan.h`.
- Produces:
  - `enum class shell::ScanArm : uint8_t { S = 0, P = 1, Zero = 2 };`
  - constants `kCheckSteps = 24`, `kCheckReads = 64`, `kParkHold = 69`, `kParkFirstRead = 5`, `kArmSBlocks = 1537`, `kArmPBlocks = 1656`, `kArmZeroBlocks = 1536`, `kRunBlocks = 4729`, `kArmCount = 3`
  - `struct shell::CheckSlot { ScanArm arm; int select_step; int read_step; bool read_after_select; };`
  - `CheckSlot shell::check_slot(int block);`
  - `char shell::arm_letter(ScanArm a);` → `'S'`, `'P'`, `'0'`

- [ ] **Step 1: Write the failing test.** Create `tests/test_scan_check_plan.cpp`:

```cpp
// The scan-check image's run block, as data: which arm each block belongs
// to, which step it selects and which it reads. Spec: docs/superpowers/
// specs/2026-09-28-coupon-panel-scan-design.md section 4.
#include <doctest/doctest.h>
#include "../shell/scan_check_plan.h"
#include "../shell/mux_plan.h"

using shell::ScanArm;

TEST_CASE("scan check plan: the numbers the spec prints") {
    CHECK(shell::kCheckSteps == shell::scan_steps(shell::kCouponChain));
    CHECK(shell::kArmSBlocks == 1537);
    CHECK(shell::kArmPBlocks == 1656);
    CHECK(shell::kArmZeroBlocks == 1536);
    CHECK(shell::kRunBlocks == 4729);
}

TEST_CASE("scan check plan: every arm reads every step exactly 64 times") {
    int reads[shell::kArmCount][shell::kCheckSteps] = {};
    for(int b = 0; b < shell::kRunBlocks; ++b)
    {
        const shell::CheckSlot s = shell::check_slot(b);
        if(s.read_step < 0) continue;
        REQUIRE(s.read_step < shell::kCheckSteps);
        ++reads[static_cast<int>(s.arm)][s.read_step];
    }
    for(int a = 0; a < shell::kArmCount; ++a)
        for(int st = 0; st < shell::kCheckSteps; ++st)
            CHECK(reads[a][st] == shell::kCheckReads);
}

TEST_CASE("scan check plan: arm S reads the step it selected one block earlier") {
    const shell::CheckSlot first = shell::check_slot(0);
    CHECK(first.arm == ScanArm::S);
    CHECK(first.select_step == 0);
    CHECK(first.read_step == -1);
    int selected = first.select_step;
    for(int b = 1; b < shell::kArmSBlocks; ++b)
    {
        const shell::CheckSlot s = shell::check_slot(b);
        CHECK(s.arm == ScanArm::S);
        CHECK_FALSE(s.read_after_select);
        CHECK(s.read_step == selected);
        selected = s.select_step;
    }
    CHECK(shell::check_slot(shell::kArmSBlocks - 1).select_step == -1);
}

TEST_CASE("scan check plan: arm P parks each step and reads it from block 5") {
    for(int st = 0; st < shell::kCheckSteps; ++st)
    {
        const int base = shell::kArmSBlocks + st * shell::kParkHold;
        const shell::CheckSlot sel = shell::check_slot(base);
        CHECK(sel.arm == ScanArm::P);
        CHECK(sel.select_step == st);
        CHECK(sel.read_step == -1);
        for(int k = 1; k < shell::kParkHold; ++k)
        {
            const shell::CheckSlot s = shell::check_slot(base + k);
            CHECK(s.arm == ScanArm::P);
            CHECK(s.select_step == -1);
            CHECK(s.read_step == (k >= shell::kParkFirstRead ? st : -1));
        }
    }
}

TEST_CASE("scan check plan: arm 0 reads in the same block it selects") {
    const int base = shell::kArmSBlocks + shell::kArmPBlocks;
    for(int b = base; b < shell::kRunBlocks; ++b)
    {
        const shell::CheckSlot s = shell::check_slot(b);
        CHECK(s.arm == ScanArm::Zero);
        CHECK(s.read_after_select);
        CHECK(s.select_step == (b - base) % shell::kCheckSteps);
        CHECK(s.read_step == s.select_step);
    }
}

TEST_CASE("scan check plan: out of range does nothing") {
    for(int b : {-1, shell::kRunBlocks, shell::kRunBlocks + 100})
    {
        const shell::CheckSlot s = shell::check_slot(b);
        CHECK(s.select_step == -1);
        CHECK(s.read_step == -1);
    }
}

TEST_CASE("scan check plan: arm letters") {
    CHECK(shell::arm_letter(ScanArm::S) == 'S');
    CHECK(shell::arm_letter(ScanArm::P) == 'P');
    CHECK(shell::arm_letter(ScanArm::Zero) == '0');
}
```

In `CMakeLists.txt`, after the `tests/test_scan_value.cpp` line added in Task 2, add:

```
    shell/scan_check_plan.cpp
    tests/test_scan_check_plan.cpp
```

- [ ] **Step 2: Run it to verify it fails.**

Run: `source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build 2>&1 | tail -5`
Expected: compile error, `scan_check_plan.h` not found.

- [ ] **Step 3: Implement.** Create `shell/scan_check_plan.h`:

```cpp
#pragma once

// The scan-check image's run block, as data (spec 2026-09-28-coupon-panel-
// scan-design.md section 4). One run block is three arms, 4729 audio blocks,
// ~9.46 s at 2 ms:
//
//   S  the shipping pattern. Block 0 selects step 0; every later block reads
//      the step selected one block earlier, then selects the next. 64 full
//      sweeps. The last block selects nothing.
//   P  parked. Per step: block 0 selects it, blocks 5..68 read it -- 64
//      reads, each at least four blocks after the address settled.
//   0  lag zero. Each block selects a step and reads it in the same
//      callback, so the read comes from the previous address. The control
//      that proves a clean S could have been dirty.
//
// No hardware type; tests/test_scan_check_plan.cpp holds it.
#include <cstdint>

namespace shell {

enum class ScanArm : uint8_t
{
    S    = 0,
    P    = 1,
    Zero = 2,
};

inline constexpr int kArmCount      = 3;
inline constexpr int kCheckSteps    = 24;   // the coupon: 16 + 8
inline constexpr int kCheckReads    = 64;
inline constexpr int kParkHold      = 69;
inline constexpr int kParkFirstRead = 5;
inline constexpr int kArmSBlocks    = 1 + kCheckSteps * kCheckReads;
inline constexpr int kArmPBlocks    = kCheckSteps * kParkHold;
inline constexpr int kArmZeroBlocks = kCheckSteps * kCheckReads;
inline constexpr int kRunBlocks     = kArmSBlocks + kArmPBlocks + kArmZeroBlocks;

static_assert(kParkHold - kParkFirstRead == kCheckReads,
              "arm P must read each step as often as the other arms");

struct CheckSlot
{
    ScanArm arm;
    int     select_step;        // step to select this block, -1 = none
    int     read_step;          // step to read this block, -1 = none
    bool    read_after_select;  // true only in arm 0
};

// The slot for block `block` of a run block; out of range selects and reads
// nothing.
CheckSlot check_slot(int block);

char arm_letter(ScanArm a);

} // namespace shell
```

Create `shell/scan_check_plan.cpp`:

```cpp
#include "scan_check_plan.h"

namespace shell {

CheckSlot check_slot(int block)
{
    if(block < 0 || block >= kRunBlocks) return {ScanArm::S, -1, -1, false};

    if(block < kArmSBlocks)
    {
        if(block == 0) return {ScanArm::S, 0, -1, false};
        const int read   = (block - 1) % kCheckSteps;
        const int select = (block < kArmSBlocks - 1) ? block % kCheckSteps : -1;
        return {ScanArm::S, select, read, false};
    }

    int q = block - kArmSBlocks;
    if(q < kArmPBlocks)
    {
        const int step = q / kParkHold;
        const int k    = q % kParkHold;
        if(k == 0) return {ScanArm::P, step, -1, false};
        return {ScanArm::P, -1, (k >= kParkFirstRead) ? step : -1, false};
    }

    q -= kArmPBlocks;
    const int step = q % kCheckSteps;
    return {ScanArm::Zero, step, step, true};
}

char arm_letter(ScanArm a)
{
    switch(a)
    {
        case ScanArm::S: return 'S';
        case ScanArm::P: return 'P';
        case ScanArm::Zero: return '0';
    }
    return '?';
}

} // namespace shell
```

- [ ] **Step 4: Run the tests to verify they pass.**

Run: `source env.sh && cmake --build build && ctest --test-dir build -R spky_tests --output-on-failure 2>&1 | tail -5`
Expected: `100% tests passed`.

- [ ] **Step 5: Commit.**

```bash
git add shell/scan_check_plan.h shell/scan_check_plan.cpp tests/test_scan_check_plan.cpp CMakeLists.txt
git commit -m "feat(shell): the scan-check image's block layout

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: The check image, `SHELL_SCAN_CHECK`

**Files:**
- Create: `shell/scan_check.h`, `shell/scan_check.cpp`, `shell/write_shell_scan_check.py`
- Modify: `shell/Makefile`, `shell/main.cpp`

**Interfaces:**
- Consumes: Task 1's `MuxScan::select/read_step/set_walk_leds`, `g_mux_raw`, `sense_live`; Task 3's `check_slot`, `arm_letter`, constants; `SHELL_GIT_HASH` from the generated `shell_git_hash.h`.
- Produces: `void shell::scan_check_init();`, `void shell::scan_check_tick(bench::Board& hw);`, `[[noreturn]] void shell::run_scan_check_report(bench::Board& hw);`; the output lines of spec section 4.

- [ ] **Step 1: Create the switch writer** `shell/write_shell_scan_check.py`:

```python
"""Writes the scan-check switch as a real header.

Same shape and same reason as write_shell_pot_round.py: a bare -D is
invisible to make's dependency graph, and an existing build/ would happily
reuse a stale scan_check.o or main.o -- shipping a bring-up image under a
scan-check name, or the reverse.

ALWAYS defines the symbol, including in position 0, because
`#if SHELL_SCAN_CHECK` has to work in both.

THE TIMESTAMP EDGE IS NOT ENOUGH (2026-08-23, see write_shell_wait_probe.py):
this script deletes the dependent objects itself whenever the content
changes. The objects come in as further arguments.

Spec: ../docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in {"0", "1"}:
        raise SystemExit(
            "usage: write_shell_scan_check.py OUTPUT {0|1} [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    # The script runs while the Makefile is parsed, so before any rule has
    # created build/.
    output.parent.mkdir(parents=True, exist_ok=True)
    content = "#define SHELL_SCAN_CHECK %s\n" % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[3:]:
            Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Create `shell/scan_check.h`:**

```cpp
#pragma once

// The scan-check image (SHELL_SCAN_CHECK=1, coupon only): the engine plays
// at the shell's fixed operating point, and the scan runs at the start of
// the audio callback in three arms (scan_check_plan.h). Nothing it reads
// reaches the engine. The foreground prints each finished run block.
//
// Spec: ../docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md
// section 4.
#include "hw/board.h"

namespace shell {

void scan_check_init();

// Call FIRST in the audio callback, before process(): arm S's read and the
// next select have to be exactly one block apart (spec section 2).
void scan_check_tick(bench::Board& hw);

// Starts the log and prints every finished run block. Never returns.
[[noreturn]] void run_scan_check_report(bench::Board& hw);

} // namespace shell
```

- [ ] **Step 3: Create `shell/scan_check.cpp`:**

```cpp
#include "scan_check.h"

#include "shell_scan_check.h"
#include "shell_coupon_probe.h"

#if SHELL_SCAN_CHECK

#include <atomic>
#include <cstdint>

#include "mux_scan.h"
#include "scan_check_plan.h"
#include "shell_git_hash.h"

namespace shell {

static_assert(scan_steps(kActiveChain) == kCheckSteps,
              "SHELL_SCAN_CHECK walks the coupon's 24 steps; the Makefile "
              "requires SHELL_COUPON_PROBE=1");

namespace {

struct Acc
{
    uint32_t n;
    uint32_t sum;    // 64 * 65535 < 2^32
    uint16_t min;
    uint16_t max;
};

struct RunBuf
{
    Acc      acc[kArmCount][kCheckSteps];
    uint32_t ticks;
};

MuxScan g_scan;

// Double buffer: the callback fills g_buf[g_fill]; at the end of a run
// block it publishes that buffer (g_ready, then g_done_blk) and starts the
// other. The foreground prints the published one; it has a whole run block,
// ~9.46 s, before the callback comes back to it.
RunBuf       g_buf[2];
int          g_fill  = 0;
int          g_block = 0;
int          g_run   = 0;
volatile int g_ready    = -1;
volatile int g_done_blk = -1;

void reset(RunBuf& b)
{
    for(int a = 0; a < kArmCount; ++a)
        for(int s = 0; s < kCheckSteps; ++s)
            b.acc[a][s] = Acc{0u, 0u, 0xFFFFu, 0u};
    b.ticks = 0u;
}

// The raw word of the step's live pin. On the coupon each group is wired to
// exactly one sense pin (sense_of_group), so there is exactly one.
uint16_t live_raw(int step)
{
    const int g     = group_of_step(kActiveChain, step);
    const int sense = kActiveChain.sense_of_group[g];
    return g_mux_raw[mux_channel(kActiveChain, step, sense)];
}

void read_into(bench::Board& hw, RunBuf& b, ScanArm arm, int step)
{
    g_scan.read_step(hw, step);
    const uint16_t raw = live_raw(step);
    Acc&           a   = b.acc[static_cast<int>(arm)][step];
    ++a.n;
    a.sum += raw;
    if(raw < a.min) a.min = raw;
    if(raw > a.max) a.max = raw;
}

} // namespace

void scan_check_init()
{
    g_scan.init();
    g_scan.set_walk_leds(false);
    reset(g_buf[0]);
    reset(g_buf[1]);
}

void scan_check_tick(bench::Board& hw)
{
    RunBuf&         b = g_buf[g_fill];
    const CheckSlot s = check_slot(g_block);

    if(!s.read_after_select && s.read_step >= 0)
        read_into(hw, b, s.arm, s.read_step);
    if(s.select_step >= 0) g_scan.select(s.select_step);
    if(s.read_after_select && s.read_step >= 0)
        read_into(hw, b, s.arm, s.read_step);

    ++b.ticks;
    if(++g_block >= kRunBlocks)
    {
        g_block = 0;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        g_ready    = g_fill;
        g_done_blk = g_run++;
        g_fill     = 1 - g_fill;
        reset(g_buf[g_fill]);
    }
}

void run_scan_check_report(bench::Board& hw)
{
    hw.StartLog(false);
    int printed = -1;
    while(1)
    {
        const int blk = g_done_blk;
        if(blk == printed) continue;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const RunBuf& b = g_buf[g_ready];

        hw.PrintLine("SHELL_SCAN_CFG blk=%d block=%d sr=%d steps=%d git=%s",
                     blk, static_cast<int>(hw.AudioBlockSize()),
                     static_cast<int>(hw.AudioSampleRate()), kCheckSteps,
                     SHELL_GIT_HASH);
        for(int a = 0; a < kArmCount; ++a)
            for(int s = 0; s < kCheckSteps; ++s)
            {
                const Acc& acc = b.acc[a][s];
                hw.PrintLine(
                    "SHELL_SCAN_CH blk=%d arm=%c step=%d group=%d ch=%d n=%d "
                    "sum=%d min=%d max=%d",
                    blk, arm_letter(static_cast<ScanArm>(a)), s,
                    group_of_step(kActiveChain, s),
                    static_cast<int>(step_pattern(kActiveChain, s).address),
                    static_cast<int>(acc.n), static_cast<int>(acc.sum),
                    static_cast<int>(acc.min), static_cast<int>(acc.max));
            }
        hw.PrintLine("SHELL_SCAN_HEALTH blk=%d ticks=%d expected=%d", blk,
                     static_cast<int>(b.ticks), kRunBlocks);
        hw.PrintLine("SHELL_SCAN_END blk=%d", blk);
        printed = blk;
    }
}

} // namespace shell

#endif // SHELL_SCAN_CHECK
```

- [ ] **Step 4: Wire the Makefile.** In `shell/Makefile`:

After the `SHELL_POT_ROUND` validation block (the `endif` pair following `it adds the pots to one of those two probes and selects nothing on its own)`), add:

```make
# The panel scan's measurement image (spec 2026-09-28-coupon-panel-scan-
# design.md section 4). The engine plays at the fixed operating point, the
# scan runs at the start of the callback in three arms, and nothing it reads
# reaches the engine. Coupon only: its arms walk the coupon's 24 steps.
SHELL_SCAN_CHECK ?= 0

ifneq ($(filter $(SHELL_SCAN_CHECK),0 1),$(SHELL_SCAN_CHECK))
$(error SHELL_SCAN_CHECK must be 0 or 1)
endif

ifeq ($(SHELL_SCAN_CHECK),1)
ifneq ($(SHELL_COUPON_PROBE),1)
$(error SHELL_SCAN_CHECK=1 needs SHELL_COUPON_PROBE=1: its arms walk the \
coupon's 24 steps)
endif
ifneq ($(filter 1,$(SHELL_SETTLE_PROBE) $(SHELL_XTALK_PROBE) $(SHELL_TONE_PROBE) $(SHELL_WAIT_PROBE) $(SHELL_CPU_PROBE)),)
$(error SHELL_SCAN_CHECK=1 excludes the settle, xtalk, tone, wait and CPU probes)
endif
ifneq ($(SHELL_MUX_PROBE),0)
$(error SHELL_SCAN_CHECK=1 excludes SHELL_MUX_PROBE)
endif
endif
```

In `CPP_SOURCES`, after `	pot_report.cpp \`, add:

```make
	scan_check_plan.cpp \
	scan_check.cpp \
```

Append ` $(BUILD_DIR)/scan_check.o` to the `SWITCH_OBJECTS =` line.

In `SWITCH_HEADERS`, after the `write_shell_pot_round.py` line, add:

```make
  $(shell python write_shell_scan_check.py $(BUILD_DIR)/shell_scan_check.h $(SHELL_SCAN_CHECK) $(SWITCH_OBJECTS)) \
```

In the `write_git_hash.py` line, append ` $(BUILD_DIR)/scan_check.o` after `$(BUILD_DIR)/pot_report.o`, and in the comment below it change `The git stamp deletes only xtalk_probe.o, tone_probe.o, wait_probe.o and pot_report.o` to `The git stamp deletes only xtalk_probe.o, tone_probe.o, wait_probe.o, pot_report.o and scan_check.o`.

Append ` $(BUILD_DIR)/shell_scan_check.h` to the `$(BUILD_DIR)/main.o:` dependency line, and add a new line after the `pot_report.o` dependency line:

```make
$(BUILD_DIR)/scan_check.o: $(BUILD_DIR)/shell_scan_check.h $(BUILD_DIR)/shell_coupon_probe.h $(BUILD_DIR)/shell_git_hash.h
```

- [ ] **Step 5: Wire `shell/main.cpp`.**

After `#include "shell_wait_probe.h"`, add `#include "shell_scan_check.h"`.

After the `#if SHELL_WAIT_PROBE ... #endif` include block, add:

```cpp
#if SHELL_SCAN_CHECK
#include "scan_check.h"
#endif
```

In `AudioCallback`, directly before the second `inst.process(in[0], in[1], out[0], out[1], size);` (the one after the `SHELL_CPU_PROBE` block's `#endif`), add:

```cpp
#if SHELL_SCAN_CHECK
    // FIRST, before process(): the read and the next select must be exactly
    // one block apart. After process() the interval would carry the
    // difference between two process() durations, and across the engine's
    // load range that eats the ~300 us of slack scan-budget.md section 3
    // found (spec 2026-09-28-coupon-panel-scan-design.md section 2).
    shell::scan_check_tick(hw);
#endif
```

Change `#if SHELL_COUPON_PROBE` (the one that guards `shell::run_coupon_bringup(hw);`) to `#if SHELL_COUPON_PROBE && !SHELL_SCAN_CHECK`.

After `inst.set_density(spky::PART_A, 0.6f);`, add:

```cpp
#if SHELL_SCAN_CHECK
    // The engine plays at the fixed operating point above; nothing the scan
    // reads reaches it, so the point stays fixed through the run, as in every
    // earlier coupon round.
    shell::scan_check_init();
    hw.StartAudio(AudioCallback);
    shell::run_scan_check_report(hw);   // never returns
#endif
```

- [ ] **Step 6: Build the images and prove they differ.**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 2>&1 | tail -2
cp shell/build/shell-sram.bin shell/build/img-bringup.bin
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_SCAN_CHECK=1 2>&1 | tail -2
cp shell/build/shell-sram.bin shell/build/img-scan-check.bin
cmp shell/build/img-bringup.bin shell/build/img-scan-check.bin
```
Expected: both builds link; `cmp` reports a difference (exit 1). Then verify the refusals:

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_SCAN_CHECK=1 2>&1 | tail -1
```
Expected: `SHELL_SCAN_CHECK=1 needs SHELL_COUPON_PROBE=1`. And with `SHELL_COUPON_PROBE=1 SHELL_SCAN_CHECK=1 SHELL_WAIT_PROBE=1`: `excludes the settle, xtalk, tone, wait and CPU probes`.

Finally build the default image (no switches) and confirm it links.

- [ ] **Step 7: Run the host suite** (nothing host-side changed, but `mux_plan` did in Task 1): `source env.sh && cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -3`. Expected: all pass.

- [ ] **Step 8: Commit.**

```bash
git add shell/scan_check.h shell/scan_check.cpp shell/write_shell_scan_check.py shell/Makefile shell/main.cpp
git commit -m "feat(shell): SHELL_SCAN_CHECK, the panel scan's measurement image

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: The reader and its guard

**Files:**
- Create: `shell/read_scan_check.py`, `shell/test_read_scan_check.py`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: the line format of Task 4 (`SHELL_SCAN_CFG/CH/HEALTH/END`, fields as printed there); `shell/coupon_expect.cpp`'s `kMux16` and `kMux8` arrays (parsed by the guard).
- Produces: `parse_blocks(lines) -> list[dict]`, `mean(block, arm, step)`, `g1_span(block) -> (ok, zero, rail)`, `g2_complete(block) -> bool`, `qualifying(block) -> list[int]`, `g3_control(block) -> (ok, qualifying)`, `deltas(block) -> dict[int, float]`, `criterion(block) -> bool`, `hysteresis(blocks) -> (widest, H)`, `report(blocks, out=None) -> int`, constants `STEPS, READS, RUN_BLOCKS, ARMS, CRITERION, G3_NEIGHBOUR, G3_MIN_QUALIFYING, H_QUANTUM, EXPECT, POT_STEPS`.

- [ ] **Step 1: Write the guard first.** Create `shell/test_read_scan_check.py`:

```python
"""Guard for read_scan_check.py. Plain asserts, exit code is the verdict --
pytest is not installed here. ctest runs it as read_scan_check_guard.

Every fixture is built the way the firmware prints (scan_check.cpp), and
each one must drive the verdict it is named for.
"""
import io
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import read_scan_check as r

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


RAIL = 63485
LEVEL = {"H": RAIL, "L": 2, "M": RAIL // 2, "U": RAIL // 2}


def p_level(step):
    return LEVEL[r.EXPECT[step]]


def sample_lines(blk=0, s_of=None, zero_of=None, spread=6, ticks=None,
                 drop=None, p_of=None):
    """One block as the firmware prints it. Defaults: a clean run -- S reads
    exactly P, arm 0 reads its predecessor's P (the previous address)."""
    p_of = p_of or p_level
    s_of = s_of or p_of
    zero_of = zero_of or (lambda s: p_of(s - 1) if s > 0 else p_of(23))
    lines = ["SHELL_SCAN_CFG blk=%d block=96 sr=48000 steps=24 git=deadbee"
             % blk]
    for arm, fn in (("S", s_of), ("P", p_of), ("0", zero_of)):
        for s in range(r.STEPS):
            if drop == (arm, s):
                continue
            v = int(round(fn(s)))
            lines.append(
                "SHELL_SCAN_CH blk=%d arm=%s step=%d group=%d ch=%d n=64 "
                "sum=%d min=%d max=%d"
                % (blk, arm, s, r.group_of(s), s if s < 16 else s - 16,
                   64 * v, max(0, v - spread // 2),
                   min(65535, v + spread - spread // 2)))
    lines.append("SHELL_SCAN_HEALTH blk=%d ticks=%d expected=4729"
                 % (blk, r.RUN_BLOCKS if ticks is None else ticks))
    lines.append("SHELL_SCAN_END blk=%d" % blk)
    return lines


def one(lines):
    blocks = r.parse_blocks(lines)
    check(len(blocks) == 1, "expected one complete block, got %d"
          % len(blocks))
    return blocks[0] if blocks else None


def quiet_report(blocks):
    return r.report(blocks, out=io.StringIO())


def test_expect_matches_the_firmware_table():
    src = (HERE / "coupon_expect.cpp").read_text(encoding="utf-8")
    tokens = re.findall(r"Expect::(\w+)", src.split("coupon_expect(int")[0])
    letter = {"High": "H", "Low": "L", "Mid": "M", "Unchecked": "U"}
    check(tuple(letter[t] for t in tokens) == r.EXPECT,
          "read_scan_check.EXPECT drifted from coupon_expect.cpp's tables")


def test_clean_run_passes():
    b = one(sample_lines())
    if b is None:
        return
    check(r.g1_span(b)[0], "clean: G1 failed")
    check(r.g2_complete(b), "clean: G2 failed")
    ok, q = r.g3_control(b)
    check(ok, "clean: G3 failed")
    check(len(q) >= r.G3_MIN_QUALIFYING, "clean: too few qualifying steps")
    check(r.criterion(b), "clean: criterion failed")
    check(quiet_report([b]) == 0, "clean: exit code not 0")


def test_contaminated_run_fails_the_criterion():
    # S reads 10 % of the predecessor's value -- what an oversampling group
    # straddling the address change would do.
    def s_of(s):
        prev = p_level(s - 1) if s > 0 else p_level(23)
        return 0.9 * p_level(s) + 0.1 * prev
    b = one(sample_lines(s_of=s_of))
    if b is None:
        return
    check(not r.criterion(b), "contaminated: criterion passed")
    check(quiet_report([b]) == 1, "contaminated: exit code not 1")


def test_a_control_that_cannot_fail_is_refused():
    # Arm 0 reading exactly P: the comparison could never have gone red.
    b = one(sample_lines(zero_of=p_level))
    if b is None:
        return
    check(not r.g3_control(b)[0], "G3 passed with arm 0 equal to P")
    check(quiet_report([b]) == 1, "G3-can't-fail: exit code not 1")


def test_too_few_qualifying_steps_is_refused():
    # Every P level the same: nothing qualifies, so G3 cannot be asserted.
    flat = lambda s: RAIL // 2
    b = one(sample_lines(p_of=flat, s_of=flat, zero_of=flat))
    if b is None:
        return
    ok, q = r.g3_control(b)
    check(not ok and q == [], "flat board: G3 did not refuse")


def test_invalid_span_fails_g1():
    def p_of(s):
        return RAIL - 1000 if s == 1 else p_level(s)   # one rail tie off
    b = one(sample_lines(p_of=p_of))
    if b is None:
        return
    check(not r.g1_span(b)[0], "open tie: G1 passed")
    check(quiet_report([b]) == 1, "open tie: exit code not 1")


def test_short_run_fails_g2():
    b = one(sample_lines(ticks=4700))
    if b is None:
        return
    check(not r.g2_complete(b), "short run: G2 passed")


def test_incomplete_block_is_not_a_block():
    check(r.parse_blocks(sample_lines(drop=("P", 7))) == [],
          "a block with a missing line was accepted")
    check(quiet_report([]) == 1, "no blocks: exit code not 1")


def test_blk_mismatch_resets():
    # A blk-4 line inside block 3 (a host that lost lines mid-block) tears
    # block 3 off; the rest of block 3 is ignored, and block 4 still parses.
    a = sample_lines(blk=3)
    b = sample_lines(blk=4)
    mixed = a[:10] + [b[1]] + a[10:] + b
    blocks = r.parse_blocks(mixed)
    check(len(blocks) == 1 and blocks[0]["cfg"]["blk"] == 4,
          "a torn block leaked into the next one")


def test_hysteresis_rule():
    b23 = one(sample_lines(spread=23))
    b0 = one(sample_lines(spread=0))
    b32 = one(sample_lines(spread=32))
    if None in (b23, b0, b32):
        return
    check(r.hysteresis([b23]) == (23, 32), "23 counts did not round to 32")
    check(r.hysteresis([b0]) == (0, 16), "0 counts did not floor at 16")
    check(r.hysteresis([b32]) == (32, 32), "32 counts moved")
    check(r.hysteresis([b23, b32])[1] == 32, "H is not the widest block's")


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print("FAIL (%d)" % len(FAILS))
        for f in FAILS:
            print("  - " + f)
        sys.exit(1)
    print("read_scan_check guard OK")
```

- [ ] **Step 2: Run it to verify it fails.**

Run: `python shell/test_read_scan_check.py`
Expected: `ModuleNotFoundError: No module named 'read_scan_check'`.

- [ ] **Step 3: Write the reader.** Create `shell/read_scan_check.py`:

```python
"""Reads the SHELL_SCAN_CHECK image's output and judges it.

Spec: ../docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md
section 4.

Call:
    python read_scan_check.py PORT capture.txt [seconds]   # record, analyse
    python read_scan_check.py --file capture.txt           # analyse only

Recording writes every line the board sends, unedited, to capture.txt --
that file is what gets committed under docs/hardware/captures/. Default
recording time 60 s: ~6 run blocks of 9.46 s, minus the one the port opened
into.

Per complete block: G1 (span from arm P's ties, coupon_span()'s rules), G2
(ticks == 4729, every n == 64), G3 (arm 0 must miss P on every qualifying
step, and at least 3 must qualify), then the criterion |mean_S - mean_P| <= 8
on every step. Exit 1 on no complete block or any failure. H, the
hysteresis for the playing image, is printed across all complete blocks.

The line format was transcribed from scan_check.cpp's PrintLine calls.
"""
import re
import sys
import time

STEPS = 24
READS = 64
RUN_BLOCKS = 4729
ARMS = ("S", "P", "0")
CRITERION = 8            # counts: half an LSB of 12 bit
G3_NEIGHBOUR = 4096      # counts between a step's and its predecessor's P
G3_MIN_QUALIFYING = 3
H_QUANTUM = 16

# coupon_expect.h
TIE_SPREAD = 328
RAIL_FLOOR = 58982
RAIL_MARGIN = 1311

# coupon_expect.cpp's kMux16 then kMux8, in step order. H/L = 0 ohm tie to
# the rail / AGND, M = divider, U = pot. The guard parses the C++ file and
# fails when the two drift apart.
EXPECT = ("U", "H", "U", "L", "U", "H", "U", "L",
          "M", "M", "L", "L", "L", "L", "L", "L",
          "U", "H", "U", "L", "U", "H", "M", "L")
POT_STEPS = tuple(s for s in range(STEPS) if EXPECT[s] == "U")

_LINE = re.compile(r"^(SHELL_SCAN_(?:CFG|CH|HEALTH|END))\s+(.*)$")
_INT_KEYS = {"blk", "block", "sr", "steps", "step", "group", "ch", "n", "sum",
             "min", "max", "ticks", "expected"}


def group_of(step):
    return 0 if step < 16 else 1


def parse_line(line):
    m = _LINE.match(line.strip())
    if not m:
        return None
    fields = {}
    for tok in m.group(2).split():
        if "=" not in tok:
            return None
        k, v = tok.split("=", 1)
        if k in _INT_KEYS:
            try:
                v = int(v)
            except ValueError:
                return None
        fields[k] = v
    return m.group(1), fields


def _complete(block):
    return (block["health"] is not None
            and all((a, s) in block["ch"] for a in ARMS for s in range(STEPS)))


def parse_blocks(lines):
    """Every complete block in `lines`, in order. A block opens at _CFG and
    closes at _END; a line from another blk tears the open block off."""
    blocks, cur = [], None
    for line in lines:
        parsed = parse_line(line)
        if parsed is None:
            continue
        kind, f = parsed
        if kind == "SHELL_SCAN_CFG":
            cur = {"cfg": f, "ch": {}, "health": None}
            continue
        if cur is None:
            continue
        if f.get("blk") != cur["cfg"].get("blk"):
            cur = None
            continue
        if kind == "SHELL_SCAN_CH":
            cur["ch"][(f["arm"], f["step"])] = f
        elif kind == "SHELL_SCAN_HEALTH":
            cur["health"] = f
        elif kind == "SHELL_SCAN_END":
            if _complete(cur):
                blocks.append(cur)
            cur = None
    return blocks


def mean(block, arm, step):
    row = block["ch"][(arm, step)]
    return row["sum"] / row["n"] if row["n"] else float("nan")


def g1_span(block):
    hi = [mean(block, "P", s) for s in range(STEPS) if EXPECT[s] == "H"]
    lo = [mean(block, "P", s) for s in range(STEPS) if EXPECT[s] == "L"]
    zero, rail = sum(lo) / len(lo), sum(hi) / len(hi)
    ok = (max(hi) - min(hi) <= TIE_SPREAD and max(lo) - min(lo) <= TIE_SPREAD
          and rail >= RAIL_FLOOR and zero <= RAIL_MARGIN and rail > zero)
    return ok, zero, rail


def g2_complete(block):
    h = block["health"]
    return (h["ticks"] == RUN_BLOCKS and h["expected"] == RUN_BLOCKS
            and all(block["ch"][(a, s)]["n"] == READS
                    for a in ARMS for s in range(STEPS)))


def qualifying(block):
    out = []
    for s in range(1, STEPS):
        if group_of(s) != group_of(s - 1):
            continue
        if abs(mean(block, "P", s) - mean(block, "P", s - 1)) > G3_NEIGHBOUR:
            out.append(s)
    return out


def g3_control(block):
    q = qualifying(block)
    if len(q) < G3_MIN_QUALIFYING:
        return False, q
    return all(abs(mean(block, "0", s) - mean(block, "P", s)) > CRITERION
               for s in q), q


def deltas(block):
    return {s: mean(block, "S", s) - mean(block, "P", s) for s in range(STEPS)}


def criterion(block):
    return all(abs(d) <= CRITERION for d in deltas(block).values())


def hysteresis(blocks):
    widest = max(block["ch"][("S", s)]["max"] - block["ch"][("S", s)]["min"]
                 for block in blocks for s in POT_STEPS)
    h = -(-widest // H_QUANTUM) * H_QUANTUM
    return widest, max(h, H_QUANTUM)


def report(blocks, out=None):
    out = out or sys.stdout
    if not blocks:
        print("no complete SHELL_SCAN block", file=out)
        return 1
    failed = False
    for b in blocks:
        g1, zero, rail = g1_span(b)
        g2 = g2_complete(b)
        g3, q = g3_control(b)
        crit = criterion(b)
        print("block %d  git=%s  G1=%s (zero %.1f, rail %.1f)  G2=%s  "
              "G3=%s (%d qualifying)  criterion=%s"
              % (b["cfg"]["blk"], b["cfg"].get("git"), g1, zero, rail, g2, g3,
                 len(q), crit), file=out)
        print("  step g ch exp     P mean     S-P  S range     0-P  0-Pprev",
              file=out)
        d = deltas(b)
        for s in range(STEPS):
            row = b["ch"][("S", s)]
            prev = mean(b, "P", s - 1) if s > 0 else float("nan")
            print("  %4d %d %2d  %s  %9.1f  %+6.1f  %7d  %+6.1f  %+7.1f%s"
                  % (s, group_of(s), s if s < 16 else s - 16, EXPECT[s],
                     mean(b, "P", s), d[s], row["max"] - row["min"],
                     mean(b, "0", s) - mean(b, "P", s),
                     mean(b, "0", s) - prev,
                     "  <- over" if abs(d[s]) > CRITERION else ""), file=out)
        failed = failed or not (g1 and g2 and g3 and crit)
    widest, h = hysteresis(blocks)
    print("pot noise: widest S range %d counts over %d block(s) -> H=%d"
          % (widest, len(blocks), h), file=out)
    return 1 if failed else 0


def record(port, path, seconds):
    import serial   # imported here so the guard runs without pyserial
    with serial.Serial(port, timeout=1.0) as ser, \
            open(path, "w", encoding="utf-8", newline="\n") as dst:
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            line = ser.readline().decode("utf-8", "replace")
            if line:
                dst.write(line if line.endswith("\n") else line + "\n")


def main(argv):
    if len(argv) == 3 and argv[1] == "--file":
        path = argv[2]
    elif len(argv) in (3, 4) and argv[1] != "--file":
        path = argv[2]
        record(argv[1], path, float(argv[3]) if len(argv) == 4 else 60.0)
    else:
        raise SystemExit("usage: read_scan_check.py PORT capture.txt "
                         "[seconds] | --file capture.txt")
    with open(path, encoding="utf-8", errors="replace") as f:
        blocks = parse_blocks(f.readlines())
    return report(blocks)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
```

- [ ] **Step 4: Run the guard to verify it passes.**

Run: `python shell/test_read_scan_check.py`
Expected: `read_scan_check guard OK`.

- [ ] **Step 5: Prove G3's RED.** Temporarily change `g3_control`'s comparison `> CRITERION` to `>= 0`; run the guard: `test_a_control_that_cannot_fail_is_refused` must fail. Restore with an edit, rerun: OK.

- [ ] **Step 6: Register in ctest.** In `CMakeLists.txt`, after the `read_pots_guard` test, add:

```cmake
# The panel scan's measurement image: gates G1-G3, the criterion, and the
# hysteresis rule. Generated fixtures; the committed capture joins after the
# first board session (spec 2026-09-28-coupon-panel-scan-design.md).
add_test(NAME read_scan_check_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_scan_check.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)
```

Run: `source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null && cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -3`
Expected: all tests pass, including `read_scan_check_guard`.

- [ ] **Step 7: Commit.**

```bash
git add shell/read_scan_check.py shell/test_read_scan_check.py CMakeLists.txt
git commit -m "feat(shell): read_scan_check.py, the scan-check reader and its gates

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: Board session 1 (controller with Bastian, not a subagent)

- [ ] **Step 1:** Build `SHELL_COUPON_PROBE=1 SHELL_SCAN_CHECK=1` fresh and `cmp` it against the bring-up image (Task 4 Step 6). Note the commit hash the image was built from (`git rev-parse --short HEAD`).
- [ ] **Step 2: The QSPI bank.** The engine's wavetable bank lives in QSPI at `0x90100000`. Whether this submodule has it is unknown (the coupon images so far ran no engine). With the board in DFU, read it back and compare. The read changes nothing:

```bash
dfu-util -a 0 -s 0x90100000:$(stat -c %s shell/build/shell-qspi.bin) -U shell/build/qspi-readback.bin
cmp shell/build/qspi-readback.bin shell/build/shell-qspi.bin
```
If they differ, ask Bastian before writing: `dfu-util -a 0 -s 0x90100000 -D shell/build/shell-qspi.bin`.
- [ ] **Step 3:** Bastian puts the board into DFU. Flash: `dfu-util -a 0 -s 0x90040000:leave -D shell/build/shell-sram.bin`.
- [ ] **Step 4:** Record and analyse: `python shell/read_scan_check.py COM4 docs/hardware/captures/scan-check-capture-<hash>.txt 60`. Expected: at least four complete blocks.
- [ ] **Step 5:** If any gate fails, stop and bring it to Bastian with the reader's table. If the criterion fails with the gates holding, the playing image is not built (spec section 9); continue with Task 7 only.
- [ ] **Step 6:** Commit the capture unedited:

```bash
git add docs/hardware/captures/scan-check-capture-<hash>.txt
git commit -m "data(hardware): the scan-check capture, board session 1

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 7: The write-up, H, and the real capture in the guard

**Files:**
- Create: `docs/hardware/scan-measured.md`
- Modify: `shell/scan_value.h`, `tests/test_scan_value.cpp`, `shell/test_read_scan_check.py`, `docs/hardware/scan-budget.md`, `docs/hardware/captures/README.md`

**Interfaces:**
- Consumes: the committed capture; `read_scan_check.hysteresis()`.
- Produces: `inline constexpr int shell::kPotHysteresis` in `scan_value.h`.

- [ ] **Step 1: Guard the capture.** Append to `shell/test_read_scan_check.py`, before the `if __name__` block (replace `<hash>` with the capture's real file name):

```python
CAPTURE = (HERE.parent / "docs" / "hardware" / "captures"
           / "scan-check-capture-<hash>.txt")


def test_the_committed_capture():
    blocks = r.parse_blocks(
        CAPTURE.read_text(encoding="utf-8", errors="replace").splitlines())
    check(len(blocks) >= 4, "capture: %d complete blocks, expected >= 4"
          % len(blocks))
    for b in blocks:
        check(r.g1_span(b)[0], "capture blk %s: G1" % b["cfg"]["blk"])
        check(r.g2_complete(b), "capture blk %s: G2" % b["cfg"]["blk"])
        check(r.g3_control(b)[0], "capture blk %s: G3" % b["cfg"]["blk"])


def test_scan_value_h_carries_the_capture_s_hysteresis():
    blocks = r.parse_blocks(
        CAPTURE.read_text(encoding="utf-8", errors="replace").splitlines())
    src = (HERE / "scan_value.h").read_text(encoding="utf-8")
    m = re.search(r"kPotHysteresis\s*=\s*(\d+)\s*;", src)
    check(m is not None, "scan_value.h has no kPotHysteresis")
    if m and blocks:
        check(int(m.group(1)) == r.hysteresis(blocks)[1],
              "kPotHysteresis %s is not the capture's H %d"
              % (m.group(1), r.hysteresis(blocks)[1]))
```

If the capture's verdict is that every block passes the criterion, add `check(r.criterion(b), ...)` inside the loop too. If it does not, add the opposite check with a comment naming the failing steps, so the guard pins what the write-up says.

- [ ] **Step 2:** Run `python shell/test_read_scan_check.py`: expected FAIL, `scan_value.h has no kPotHysteresis`.
- [ ] **Step 3:** Add to `shell/scan_value.h`, after `kPanelSpan`, with `N` = the `H=` value `read_scan_check.py --file <capture>` prints:

```cpp
// The hysteresis band, in raw counts: the widest max - min any of the
// coupon's seven pots showed in the scan-check image's arm S, across every
// complete block of docs/hardware/captures/scan-check-capture-<hash>.txt,
// rounded up to a multiple of 16 and at least 16 (spec section 4).
// shell/test_read_scan_check.py recomputes it from the capture and fails if
// the two differ.
inline constexpr int kPotHysteresis = N;
```

and to `tests/test_scan_value.cpp`:

```cpp
TEST_CASE("scan value: the hysteresis band obeys the spec's rule") {
    CHECK(shell::kPotHysteresis >= 16);
    CHECK(shell::kPotHysteresis % 16 == 0);
}
```

- [ ] **Step 4:** Rebuild and run everything: `source env.sh && cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -3`. Expected: all pass.
- [ ] **Step 5: Write `docs/hardware/scan-measured.md`** in the house style of `docs/hardware/wait-measured.md`: a boxed summary with the one-line answer; §1 the instrument (arms S/P/0, image hash, spec link); §2 the run and its gates (blocks, G1 zero/rail, G2, G3 qualifying count, per block); §3 the result: per step the P mean, S−P, S range, 0−P and 0−P(prev), from the reader's table, across blocks; §4 what it answers for `scan-budget.md` §7 (oversampling order, slack with the engine running, F1 on real pots under a stepping mux), each labelled measured, derived or not claimed; §5 the noise and H; §6 what this rests on (one board, one session, classes); §7 open items; §8 how to repeat (build switches, flash, the reader call). Every number from the reader's output on the committed capture.
- [ ] **Step 6: Status.** In `docs/hardware/scan-budget.md` §7, append a dated paragraph: done on 2026-09-28 (or the session's date), with the result in one sentence and a link to `scan-measured.md`. In the boxed summary at the top of `scan-budget.md`, append the same one-line pointer. In `docs/hardware/captures/README.md`, add a row for the capture (what depends on it: `shell/test_read_scan_check.py`, `scan-measured.md`, `kPotHysteresis`) and a short paragraph like the pot captures' (image switches, date, board, where the first complete block starts).
- [ ] **Step 7: Commit.**

```bash
git add docs/hardware/scan-measured.md docs/hardware/scan-budget.md docs/hardware/captures/README.md shell/scan_value.h tests/test_scan_value.cpp shell/test_read_scan_check.py
git commit -m "docs(hardware): the scan measured on the coupon; H from the capture

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 8: The playing image, `SHELL_PANEL_SCAN`

Only if Task 6's criterion passed (spec section 9).

**Files:**
- Create: `shell/panel_scan.h`, `shell/panel_scan.cpp`, `shell/write_shell_panel_scan.py`
- Modify: `shell/Makefile`, `shell/main.cpp`

**Interfaces:**
- Consumes: `MuxScan::step()` (returns the step read), `set_walk_leds`, `g_mux_raw`, `sense_live`, `mux_channel`, `group_of_step`, `step_pattern`, `step_of`, `scan_steps`, `kActiveChain`; `Span`, `coupon_span`; `PotFilter`, `pot_filter`, `kPanelSpan`, `kPotHysteresis`; `ControlTable`, `kCouponTable`, `kPanelTable`, `kCouponControls`, `find_control`, `apply_control`.
- Produces: `void shell::panel_scan_init();`, `void shell::panel_scan_tick(bench::Board& hw, spky::Instrument& inst);`, `[[noreturn]] void shell::run_panel_scan_report(bench::Board& hw);`, the `SHELL_PLAY` line.

- [ ] **Step 1: Create the switch writer** `shell/write_shell_panel_scan.py`: the file from Task 4 Step 1 with every `scan_check`/`SHELL_SCAN_CHECK` replaced by `panel_scan`/`SHELL_PANEL_SCAN`, and its first docstring paragraph ending "...shipping a scan-check image under a panel-scan name, or the reverse."

- [ ] **Step 2: Create `shell/panel_scan.h`:**

```cpp
#pragma once

// The playing image (SHELL_PANEL_SCAN=1): the scan runs first in the audio
// callback, and every mapped channel it reads goes through the value path
// (scan_value.h) into the engine (controls.h). On the coupon, RV2/RV4/RV6
// drive RATE_A/DENSITY_A/FILT_A; on the panel profile the table is empty
// until part 2 and nothing is applied.
//
// Spec: ../docs/superpowers/specs/2026-09-28-coupon-panel-scan-design.md
// section 5.
#include "hw/board.h"
#include "instrument.h"

namespace shell {

void panel_scan_init();

// Call FIRST in the audio callback, before process() (spec section 2).
void panel_scan_tick(bench::Board& hw, spky::Instrument& inst);

// Starts the log and prints SHELL_PLAY twice a second. Never returns.
[[noreturn]] void run_panel_scan_report(bench::Board& hw);

} // namespace shell
```

- [ ] **Step 3: Create `shell/panel_scan.cpp`:**

```cpp
#include "panel_scan.h"

#include "shell_panel_scan.h"
#include "shell_coupon_probe.h"

#if SHELL_PANEL_SCAN

#include <atomic>

#include "controls.h"
#include "coupon_expect.h"
#include "mux_scan.h"
#include "scan_value.h"

namespace shell {

namespace {

#if SHELL_COUPON_PROBE
constexpr ControlTable kTable = kCouponTable;
#else
constexpr ControlTable kTable = kPanelTable;
#endif

constexpr int kSteps    = scan_steps(kActiveChain);
constexpr int kChannels = mux_total(kActiveChain);

MuxScan   g_scan;
PotFilter g_filter[kChannels];

// Last emitted value per channel, -1 = never emitted. Read by the
// foreground for SHELL_PLAY only.
volatile float g_value[kChannels];

// The span the value path uses. On the coupon it starts invalid and is
// replaced after every full sweep whose ties give a valid one; an invalid
// sweep leaves it in place. On the panel it is kPanelSpan until part 2.
#if SHELL_COUPON_PROBE
Span     g_span{0, 0, false};
uint16_t g_step_raw[kSteps];   // the live pin's raw word per step
#else
Span g_span = kPanelSpan;
#endif
volatile uint32_t g_sweeps = 0;

} // namespace

void panel_scan_init()
{
    g_scan.init();
    g_scan.set_walk_leds(false);
    for(int c = 0; c < kChannels; ++c) g_value[c] = -1.0f;
}

void panel_scan_tick(bench::Board& hw, spky::Instrument& inst)
{
    const int step = g_scan.step(hw);
    if(step < 0) return;

    const int g  = group_of_step(kActiveChain, step);
    const int ch = static_cast<int>(step_pattern(kActiveChain, step).address);
    for(int s = 0; s < kActiveChain.sense_pins; ++s)
    {
        if(!sense_live(kActiveChain, step, s)) continue;
        const int      idx = mux_channel(kActiveChain, step, s);
        const uint16_t raw = g_mux_raw[idx];
#if SHELL_COUPON_PROBE
        g_step_raw[step] = raw;
#endif
        // On the panel profile every group sits on all four pins, so (g, ch)
        // names four channels; part 2's table will key on the sense pin too.
        // With an empty table this lookup never matches.
        const ControlEntry* e = find_control(kTable, g, ch);
        if(e == nullptr) continue;
        float v;
        if(pot_filter(g_filter[idx], raw, g_span, kPotHysteresis, &v))
        {
            apply_control(*e, v, inst);
            g_value[idx] = v;
        }
    }

    if(step == kSteps - 1)
    {
#if SHELL_COUPON_PROBE
        const Span sp = coupon_span(g_step_raw, kSteps);
        if(sp.valid) g_span = sp;
#endif
        g_sweeps = g_sweeps + 1u;
    }
}

void run_panel_scan_report(bench::Board& hw)
{
    hw.StartLog(false);
    while(1)
    {
        // The span is copied field by field from the ISR's variable; a torn
        // read is possible and only cosmetic, this line is for a human. The
        // fence stops the compiler from hoisting the reads out of the loop:
        // g_span is not volatile, and nothing here tells it the ISR writes it.
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const int zero  = g_span.zero;
        const int rail  = g_span.rail;
        const int valid = g_span.valid ? 1 : 0;
#if SHELL_COUPON_PROBE
        int v[3];
        for(int i = 0; i < 3; ++i)
        {
            const ControlEntry& e = kCouponControls[i];
            const int step = step_of(kActiveChain, e.group, e.ch);
            const int idx  = mux_channel(kActiveChain, step,
                                         kActiveChain.sense_of_group[e.group]);
            v[i] = static_cast<int>(g_value[idx] * 1000.0f);
        }
        hw.PrintLine("SHELL_PLAY rv2=%d rv4=%d rv6=%d zero=%d rail=%d "
                     "valid=%d sweeps=%d",
                     v[0], v[1], v[2], zero, rail, valid,
                     static_cast<int>(g_sweeps));
#else
        hw.PrintLine("SHELL_PLAY zero=%d rail=%d valid=%d sweeps=%d", zero,
                     rail, valid, static_cast<int>(g_sweeps));
#endif
        hw.Delay(500);
    }
}

} // namespace shell

#endif // SHELL_PANEL_SCAN
```

- [ ] **Step 4: Wire the Makefile.** After the `SHELL_SCAN_CHECK` block from Task 4, add:

```make
# The playing image (spec 2026-09-28-coupon-panel-scan-design.md section 5):
# the scan runs first in the callback and drives the engine through the
# board's control table. With SHELL_COUPON_PROBE=1 it is the coupon's chain
# and table; without it, the panel's chain and its (still empty) table.
SHELL_PANEL_SCAN ?= 0

ifneq ($(filter $(SHELL_PANEL_SCAN),0 1),$(SHELL_PANEL_SCAN))
$(error SHELL_PANEL_SCAN must be 0 or 1)
endif

ifeq ($(SHELL_PANEL_SCAN),1)
ifneq ($(filter 1,$(SHELL_SETTLE_PROBE) $(SHELL_XTALK_PROBE) $(SHELL_TONE_PROBE) $(SHELL_WAIT_PROBE) $(SHELL_CPU_PROBE) $(SHELL_SCAN_CHECK)),)
$(error SHELL_PANEL_SCAN=1 excludes the settle, xtalk, tone, wait, CPU and \
scan-check images)
endif
ifneq ($(SHELL_MUX_PROBE),0)
$(error SHELL_PANEL_SCAN=1 excludes SHELL_MUX_PROBE)
endif
endif
```

In `CPP_SOURCES`, after `	scan_check.cpp \`, add:

```make
	scan_value.cpp \
	controls.cpp \
	panel_scan.cpp \
```

Append ` $(BUILD_DIR)/panel_scan.o` to `SWITCH_OBJECTS`. In `SWITCH_HEADERS`, after the `write_shell_scan_check.py` line, add:

```make
  $(shell python write_shell_panel_scan.py $(BUILD_DIR)/shell_panel_scan.h $(SHELL_PANEL_SCAN) $(SWITCH_OBJECTS)) \
```

Append ` $(BUILD_DIR)/shell_panel_scan.h` to the `main.o` dependency line, and add:

```make
$(BUILD_DIR)/panel_scan.o: $(BUILD_DIR)/shell_panel_scan.h $(BUILD_DIR)/shell_coupon_probe.h
```

- [ ] **Step 5: Wire `shell/main.cpp`.** After `#include "shell_scan_check.h"`, add `#include "shell_panel_scan.h"`. After the `#if SHELL_SCAN_CHECK #include "scan_check.h" #endif` block, add:

```cpp
#if SHELL_PANEL_SCAN
#include "panel_scan.h"
#endif
```

Extend the USB identity guard `#if defined(SHELL_CPU_PROBE) || SHELL_COUPON_PROBE || SHELL_SETTLE_PROBE || SHELL_XTALK_PROBE || SHELL_TONE_PROBE || SHELL_WAIT_PROBE` with ` || SHELL_PANEL_SCAN` (the panel-profile image logs too).

In `AudioCallback`, directly after the `#if SHELL_SCAN_CHECK ... #endif` block added in Task 4, add:

```cpp
#if SHELL_PANEL_SCAN
    // FIRST, before process(), for the same reason as the scan check above.
    shell::panel_scan_tick(hw, inst);
#endif
```

Change `#if SHELL_COUPON_PROBE && !SHELL_SCAN_CHECK` to `#if SHELL_COUPON_PROBE && !SHELL_SCAN_CHECK && !SHELL_PANEL_SCAN`.

After the `#if SHELL_SCAN_CHECK ... run_scan_check_report ... #endif` block in `main()`, add:

```cpp
#if SHELL_PANEL_SCAN
    // RATE_A and DENSITY_A above stay as the start point; on the coupon the
    // pots override them with their first emission once the span is valid.
    shell::panel_scan_init();
    hw.StartAudio(AudioCallback);
    shell::run_panel_scan_report(hw);   // never returns
#endif
```

- [ ] **Step 6: Build and compare four images.**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 2>&1 | tail -1 && cp shell/build/shell-sram.bin shell/build/img-bringup.bin
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_SCAN_CHECK=1 2>&1 | tail -1 && cp shell/build/shell-sram.bin shell/build/img-scan-check.bin
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_PANEL_SCAN=1 2>&1 | tail -1 && cp shell/build/shell-sram.bin shell/build/img-panel-play.bin
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_PANEL_SCAN=1 2>&1 | tail -1 && cp shell/build/shell-sram.bin shell/build/img-coupon-play.bin
for a in bringup scan-check panel-play; do cmp -s shell/build/img-$a.bin shell/build/img-coupon-play.bin && echo "IDENTICAL: $a"; done; echo done
```
Expected: four links, no `IDENTICAL:` line. Also: `make -C shell images SHELL_PANEL_SCAN=1 SHELL_SCAN_CHECK=1 SHELL_COUPON_PROBE=1` must stop with `SHELL_PANEL_SCAN=1 excludes ... scan-check images`.

- [ ] **Step 7:** Host suite: `source env.sh && cmake --build build && ctest --test-dir build --output-on-failure 2>&1 | tail -3`. Expected: all pass.

- [ ] **Step 8: Commit.**

```bash
git add shell/panel_scan.h shell/panel_scan.cpp shell/write_shell_panel_scan.py shell/Makefile shell/main.cpp
git commit -m "feat(shell): SHELL_PANEL_SCAN, the coupon's pots drive the engine

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 9: Board session 2 and the status documents (controller with Bastian)

**Files:**
- Modify: `shell/README.md`, `docs/roadmap.md`

- [ ] **Step 1:** Rebuild `SHELL_COUPON_PROBE=1 SHELL_PANEL_SCAN=1` last (so `shell-sram.bin` is that image) and `cmp` it against the saved `img-coupon-play.bin` from Task 8: identical. Bastian puts the board in DFU; flash `dfu-util -a 0 -s 0x90040000:leave -D shell/build/shell-sram.bin`.
- [ ] **Step 2:** Read `SHELL_PLAY` lines (e.g. `python -c "import serial,time; s=serial.Serial('COM4',timeout=1); t=time.time()+20; [print(s.readline().decode(errors='replace').strip()) for _ in iter(lambda: time.time()<t, False)]"`) while Bastian turns RV2, RV4, RV6 stop to stop. Expected: `valid=1`, each value spans 0 to 1000 and reaches both stops, and `sweeps` climbs by ~20 per second (48 ms per sweep). Bastian listens: rate, density and filter move. Record what he reports, including whether `FILT_A` steps audibly.
- [ ] **Step 3: Status.** In `shell/README.md`'s switch table, add rows:

```markdown
| `+ SHELL_SCAN_CHECK=1` | the panel scan's pattern: clean at one step per block, with the engine running | `read_scan_check.py` | `scan-measured.md` |
| `SHELL_PANEL_SCAN=1` (± `SHELL_COUPON_PROBE=1`) | the playing image: the scan drives the engine through the board's control table | `SHELL_PLAY` line | — |
```

and rewrite "Next, in order" item 1 to: part 1 of the panel scan is done (link the spec and `scan-measured.md`, one sentence on the board-session-2 result); part 2, the 70-pot table plus keycaps and LEDs, waits for the control PCB's pin map, and the pin map should give one spare channel to AGND and one to the rail (spec section 8). Add a dated M6 entry to `docs/roadmap.md` (after the scan-budget entry of 2026-09-28) and prepend the "Last updated" bullet, in the style of the existing entries: what was built, what the board said, what Bastian heard, what is next.
- [ ] **Step 4: Commit.**

```bash
git add shell/README.md docs/roadmap.md
git commit -m "docs: panel scan part 1 done -- status in the shell README and roadmap

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```
