# Coupon bring-up Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the test coupon measurable at all — correct sense pins, a chain layout that matches the board, and a bring-up probe that prints and judges every channel.

**Architecture:** The chain layout stops being a set of global constants and becomes a `ChainProfile` value with two instances, `kPanelChain` (the production panel, unchanged numbers) and `kCouponChain` (two 595s, eight LEDs, one 4067 and one 4051). All the write-side logic takes a profile, so `tests/test_mux_plan.cpp` exercises both on the host and no `#ifdef` enters the logic. The firmware picks its profile through a new build switch, generated as a real header exactly like the three switches that already exist. On top of that sits a foreground bring-up scan that walks every channel, reads the free-running ADC, compares against a table derived from the coupon's netlist, and prints a verdict per channel over USB-CDC.

**Tech Stack:** C++17, doctest (host tests via ctest), ARM GCC + make (firmware), Python 3 + pyserial 3.5 (reader).

**Spec:** [`docs/superpowers/specs/2026-09-17-coupon-settle-probe-design.md`](../specs/2026-09-17-coupon-settle-probe-design.md) — this plan builds the bring-up half that spec declares a precondition (§1), plus the profile work its §5 specifies. The settle probe itself (spec §4, §6, §7) is **not** in this plan.

## Global Constraints

- **Everything written into the repo is English** — code, comments, commit messages, docs. (Existing German comments in `shell/` stay as they are; do not translate them while editing nearby lines.)
- **Never prefix a shell command with `cd`.** The Bash tool already starts in the repo root. The firmware build is the documented exception and uses `make -C shell`.
- **Host build is clang + Ninja via `source env.sh`, and `-DCMAKE_BUILD_TYPE=Release` is not optional.** A Debug configure makes `spky_tests` and `ctrl_identity` fail with "SYNTH reference moved".
- **`ctest` does not build.** Always `cmake --build build` first; a green ctest can otherwise be a stale binary.
- **Firmware build never sources `env.sh`** — the two toolchains must not mix. `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"` and `make -C shell -j8 images` (`images`, not `all`).
- **Commit trailer:** `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`
- **A test that cannot go red gets fixed.** Every task below proves its RED once, and the step that does it says what the failure message must look like.
- **Do not touch `hardware/coupon/`.** That tree has uncommitted work from an earlier session. Add only the paths each task's commit step names.

---

### Task 1: The sense-pin base becomes data, and the wrong pin is fixed

`shell/mux_scan.cpp:63` reads `hw.GetAdcValue(daisy::patch_sm::CV_1 + s)` — the eight bipolar CV pins on C2–C9. The coupon's mux outputs are on A2/A3, which libDaisy calls `ADC_9`/`ADC_10`. `docs/hardware/io-budget.md:328` spends A2/A3/D8/D9 on raw pot sense, and line 564 records that the CV pins were **rejected** for it (`InitBipolarCv`: ±5 V, inverted, 2 ms slew). For the CPU-cost measurement the code was written for, the pin was irrelevant; on the coupon it is everything.

The index is put into the profile as plain data so the host can assert it, and a `static_assert` in the firmware ties that number back to libDaisy's enum.

**Files:**
- Modify: `shell/mux_plan.h` (add `sense_adc_base` to the constants block — the full profile struct arrives in Task 2)
- Modify: `shell/mux_scan.cpp:63`
- Test: `tests/test_mux_plan.cpp`

**Interfaces:**
- Consumes: nothing
- Produces: `shell::kSenseAdcBase` (`inline constexpr int`, value 8)

- [ ] **Step 1: Write the failing test**

Append to `tests/test_mux_plan.cpp`:

```cpp
TEST_CASE("mux plan: the sense pins are the raw ADC inputs, not the CV pins") {
    // libDaisy's patch_sm enum runs CV_1..CV_8 = 0..7 and then ADC_9 = 8
    // (daisy_patch_sm.h:20-28). io-budget section 3 spends A2/A3/D8/D9 =
    // ADC_9..ADC_12 on raw pot sense and explicitly rejects the CV pins for
    // it, because those are conditioned bipolar inputs (InitBipolarCv:
    // +-5 V, inverted, 2 ms slew). Reading CV_1 on the coupon returns a pin
    // nothing on the board drives.
    CHECK(shell::kSenseAdcBase == 8);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure -R spky_tests
```

Expected: compile error, `'kSenseAdcBase' is not a member of 'shell'`.

- [ ] **Step 3: Write minimal implementation**

In `shell/mux_plan.h`, after `inline constexpr int kChainBits = 32;`:

```cpp
// The first of the raw ADC pins, as an index into libDaisy's patch_sm
// channel enum (CV_1..CV_8 = 0..7, then ADC_9 = 8). It is a number here and
// not the enum constant because this header may not include a hardware
// header -- mux_scan.cpp static_asserts the two against each other.
inline constexpr int kSenseAdcBase = 8;
```

- [ ] **Step 4: Run test to verify it passes**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure -R spky_tests
```

Expected: PASS.

- [ ] **Step 5: Fix the firmware read and tie it to the enum**

In `shell/mux_scan.cpp`, replace the read in `MuxScan::step()`:

```cpp
                g_mux_values[ch] = hw.GetAdcValue(kSenseAdcBase + s);
```

and add near the top of the anonymous namespace, under the pin constants:

```cpp
// The sense pins are the RAW ADC inputs A2/A3/D8/D9, not the conditioned CV
// pins. Until 2026-09-17 this read CV_1 + s, which cost nothing in the CPU
// measurement it was written for and would have made every coupon reading
// meaningless. The number lives in mux_plan.h so the host can assert it;
// this is where it gets checked against libDaisy.
static_assert(daisy::patch_sm::ADC_9 == kSenseAdcBase,
              "libDaisy's patch_sm channel enum moved under kSenseAdcBase");
```

- [ ] **Step 6: Build the firmware**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images
```

Expected: links clean. If the `static_assert` fires, libDaisy's enum changed and `kSenseAdcBase` must follow it — do not silence it.

- [ ] **Step 7: Commit**

```bash
git add shell/mux_plan.h shell/mux_scan.cpp tests/test_mux_plan.cpp
git commit -m "fix(shell): the scan reads the raw ADC pins, not the CV pins

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: The chain layout becomes a profile

`shell/mux_plan.h` describes four 595s, 19 LEDs and eight mux chips. The coupon is two 595s, eight LEDs, one 4067 and one 4051 (`hardware/coupon/scripts/netlist.py:268`). Overwriting the production numbers would invalidate what `SHELL_MUX_PROBE` measured, so both layouts have to exist side by side as data.

The coupon's group 1 is a 4051 with eight channels against the 4067's sixteen, so channels-per-group becomes an array and the total step count a sum, not a product.

**Files:**
- Modify: `shell/mux_plan.h`
- Modify: `shell/mux_plan.cpp`
- Modify: `shell/mux_scan.h`, `shell/mux_scan.cpp` (call the new signatures)
- Modify: `tests/test_mux_plan.cpp`

**Interfaces:**
- Consumes: `shell::kSenseAdcBase` from Task 1
- Produces:
  - `struct shell::ChainProfile` with fields `sense_pins, sense_adc_base, groups, channels[2], sense_of_group[2], chain_bits, addr_shift, enable_shift, led_shift, led_bits`
  - `inline constexpr ChainProfile shell::kPanelChain`, `shell::kCouponChain`
  - `constexpr int shell::scan_steps(const ChainProfile&)`
  - `constexpr int shell::mux_total(const ChainProfile&)`
  - `int shell::group_of_step(const ChainProfile&, int step)`
  - `StepPattern shell::step_pattern(const ChainProfile&, int step)`
  - `int shell::mux_channel(const ChainProfile&, int step, int sense)`
  - `uint32_t shell::chain_word(const ChainProfile&, StepPattern, uint32_t leds)`

- [ ] **Step 1: Write the failing test**

Replace the whole body of `tests/test_mux_plan.cpp` below its file comment with this. Every existing case is kept and now runs against both profiles; the coupon bit table of the spec's §5 is added as explicit expectations.

```cpp
#include <doctest/doctest.h>
#include <set>
#include <vector>
#include "../shell/mux_plan.h"

namespace {
const std::vector<shell::ChainProfile> kProfiles
    = {shell::kPanelChain, shell::kCouponChain};
}

TEST_CASE("mux plan: the address walks each group's channels once") {
    for(const auto& p : kProfiles)
    {
        int step = 0;
        for(int g = 0; g < p.groups; ++g)
            for(int a = 0; a < p.channels[g]; ++a, ++step)
            {
                const shell::StepPattern s = shell::step_pattern(p, step);
                CHECK(static_cast<int>(s.address) == a);
                CHECK(shell::group_of_step(p, step) == g);
            }
        CHECK(step == shell::scan_steps(p));
    }
}

TEST_CASE("mux plan: exactly one group is enabled per step") {
    // Enables are active low. Two groups on at once shorts two mux outputs
    // onto one sense pin -- silently, and the reading looks plausible.
    for(const auto& p : kProfiles)
        for(int s = 0; s < shell::scan_steps(p); ++s)
        {
            const shell::StepPattern sp = shell::step_pattern(p, s);
            int low = 0;
            for(int g = 0; g < p.groups; ++g)
                if(((sp.enable_mask >> g) & 1u) == 0u) ++low;
            CHECK(low == 1);
            CHECK(((sp.enable_mask >> shell::group_of_step(p, s)) & 1u) == 0u);
        }
}

TEST_CASE("mux plan: a step that does not exist enables nothing") {
    // The safe answer, and not an obvious one: an out-of-range ADDRESS
    // would still select some channel and return a foreign knob's voltage.
    for(const auto& p : kProfiles)
        for(int s : {-1, shell::scan_steps(p), shell::scan_steps(p) + 7})
        {
            const shell::StepPattern sp = shell::step_pattern(p, s);
            for(int g = 0; g < p.groups; ++g)
                CHECK(((sp.enable_mask >> g) & 1u) == 1u);
            CHECK(shell::group_of_step(p, s) == -1);
        }
}

TEST_CASE("mux plan: every channel is reached exactly once per sweep") {
    for(const auto& p : kProfiles)
    {
        std::set<int> seen;
        for(int s = 0; s < shell::scan_steps(p); ++s)
            for(int i = 0; i < p.sense_pins; ++i)
            {
                const int ch = shell::mux_channel(p, s, i);
                CHECK(ch >= 0);
                CHECK(ch < shell::mux_total(p));
                seen.insert(ch);
            }
        CHECK(static_cast<int>(seen.size()) == shell::mux_total(p));
    }
}

TEST_CASE("mux plan: an index that does not exist is answered, not assumed") {
    for(const auto& p : kProfiles)
    {
        CHECK(shell::mux_channel(p, -1, 0) == -1);
        CHECK(shell::mux_channel(p, 0, -1) == -1);
        CHECK(shell::mux_channel(p, shell::scan_steps(p), 0) == -1);
        CHECK(shell::mux_channel(p, 0, p.sense_pins) == -1);
    }
}

TEST_CASE("mux plan: the LED field cannot collide with address or enable") {
    // One chain carries both, which is why LEDs cost no extra CPU. It is
    // also why an overlap would make a lit LED move a knob.
    for(const auto& p : kProfiles)
    {
        const shell::StepPattern sp = shell::step_pattern(p, 5);
        const uint32_t all_leds = (1u << p.led_bits) - 1u;
        const uint32_t dark     = shell::chain_word(p, sp, 0u);
        const uint32_t lit      = shell::chain_word(p, sp, all_leds);
        CHECK(((dark >> p.addr_shift) & 0x0Fu) == sp.address);
        CHECK(((dark >> p.enable_shift) & 0x03u) == sp.enable_mask);
        // Lighting every LED changes nothing below the LED field.
        const uint32_t below = (1u << p.led_shift) - 1u;
        CHECK((lit & below) == (dark & below));
        // And the whole word still fits the chain.
        CHECK((lit >> p.chain_bits) == 0u);
    }
}

TEST_CASE("mux plan: the coupon profile matches the coupon's 595 wiring") {
    // netlist.py:268 wires U_SR1 QA..QH = A0,A1,A2,A3,EN16,EN8,LED_1,LED_2
    // and U_SR2 QA..QF = LED_3..LED_8, with QG/QH open. write_chain() clocks
    // MSB first and U_SR1.QH' feeds U_SR2.SER, so the bit clocked LAST sits
    // nearest the input, at U_SR1.QA. That makes bit 0 the first address
    // line and bit 13 the last LED.
    const shell::ChainProfile& p = shell::kCouponChain;
    CHECK(p.chain_bits == 16);
    CHECK(p.addr_shift == 0);
    CHECK(p.enable_shift == 4);
    CHECK(p.led_shift == 6);
    CHECK(p.led_bits == 8);
    CHECK(p.groups == 2);
    CHECK(p.channels[0] == 16);   // CD74HC4067 on ADC_9
    CHECK(p.channels[1] == 8);    // CD74HC4051 on ADC_10
    CHECK(p.sense_pins == 2);     // ADC_11/ADC_12 reach test points only
    CHECK(p.sense_of_group[0] == 0);
    CHECK(p.sense_of_group[1] == 1);
    CHECK(shell::scan_steps(p) == 24);
}

TEST_CASE("mux plan: the panel profile still describes the shipping panel") {
    // These are the numbers SHELL_MUX_PROBE's CPU cost was measured against
    // (docs/bench/2026-08-23-978cbaf-shell-mux-placement.md). If a coupon
    // change moves them, that measurement silently stops meaning anything.
    const shell::ChainProfile& p = shell::kPanelChain;
    CHECK(p.chain_bits == 32);
    CHECK(p.led_bits == 19);
    CHECK(p.led_shift == 8);
    CHECK(p.sense_pins == 4);
    CHECK(shell::scan_steps(p) == 32);
    CHECK(shell::mux_total(p) == 128);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure -R spky_tests
```

Expected: compile error, `'ChainProfile' is not a member of 'shell'`. That is the RED for this task — note that the old `0x7FFFFu`-based LED assertion is gone, so a profile that forgets `led_bits` cannot slip through.

- [ ] **Step 3: Write minimal implementation — the header**

Replace the constants block in `shell/mux_plan.h` (everything from `inline constexpr int kSensePins` through `inline constexpr int kLedShift`) with:

```cpp
inline constexpr int kMaxGroups = 2;

// One board's chain, as data. Two boards exist: the shipping panel and the
// test coupon, and they differ in every number below. This is a value and
// not a set of #defines so that the host test can run the same assertions
// against both -- a wrong address pattern is a line here and a knob that
// misbehaves on a board there.
struct ChainProfile
{
    int sense_pins;        // raw ADC pins this board populates
    int sense_adc_base;    // index of the first of them in patch_sm's enum
    int groups;            // enable lines, one per group
    int channels[kMaxGroups];        // channels on that group's chip
    int sense_of_group[kMaxGroups];  // sense pin carrying it, -1 = all of them
    int chain_bits;        // bits clocked per step; the bit-bang cost scales
    int addr_shift;
    int enable_shift;
    int led_shift;
    int led_bits;
};

// The shipping panel. 32 = four 74HC595: 19 LEDs (what FireflowHW draws
// today), four address lines, two enables, seven spare. Up to eight
// CD74HC4067 share the four raw ADC pins (io-budget section 3), which is
// what makes the panel cost zero GPIOs. Demand today is 67 pot positions,
// so the 128 channels are headroom, not a plan.
inline constexpr ChainProfile kPanelChain{
    4, kSenseAdcBase, 2, {16, 16}, {-1, -1}, 32, 0, 4, 8, 19};

// The test coupon (hardware/coupon/). Two 74HC595 = 16 bits, eight LEDs, one
// CD74HC4067 on ADC_9 and one CD74HC4051 on ADC_10 -- so the two groups do
// NOT have the same channel count, and each sits on its own sense pin.
// Derivation of the bit order: netlist.py:268 plus MSB-first clocking
// through U_SR1.QH' -> U_SR2.SER.
inline constexpr ChainProfile kCouponChain{
    2, kSenseAdcBase, 2, {16, 8}, {0, 1}, 16, 0, 4, 6, 8};

constexpr int scan_steps(const ChainProfile& p)
{
    int n = 0;
    for(int g = 0; g < p.groups; ++g) n += p.channels[g];
    return n;
}

constexpr int mux_total(const ChainProfile& p)
{
    return scan_steps(p) * p.sense_pins;
}
```

Then change the four declarations at the bottom of the header to take the profile:

```cpp
StepPattern step_pattern(const ChainProfile& p, int step);

// The group a step belongs to, or -1 for a step that does not exist.
int group_of_step(const ChainProfile& p, int step);

// The channel a sense pin carries during `step`, or -1 for an index that does
// not exist. Out of range gets an answer instead of an assumption: a
// half-seated chip produces steps nobody planned, and an access past the end
// would be a crash inside the audio callback.
int mux_channel(const ChainProfile& p, int step, int sense);

// The chain word for a step, with `leds` in the LED field.
uint32_t chain_word(const ChainProfile& p, StepPattern s, uint32_t leds);
```

- [ ] **Step 4: Write minimal implementation — the source**

Replace the body of `shell/mux_plan.cpp` below its include with:

```cpp
namespace shell {

namespace {
constexpr uint8_t all_off(const ChainProfile& p)
{
    return static_cast<uint8_t>((1u << p.groups) - 1u);
}
}

int group_of_step(const ChainProfile& p, int step)
{
    if(step < 0 || step >= scan_steps(p)) return -1;
    int rest = step;
    for(int g = 0; g < p.groups; ++g)
    {
        if(rest < p.channels[g]) return g;
        rest -= p.channels[g];
    }
    return -1;
}

StepPattern step_pattern(const ChainProfile& p, int step)
{
    // A step that does not exist parks the scan with every enable off. An
    // out-of-range address would still select SOME channel and hand back a
    // foreign knob's voltage, which is worse than reading nothing.
    const int g = group_of_step(p, step);
    if(g < 0) return StepPattern{0, all_off(p)};

    int addr = step;
    for(int i = 0; i < g; ++i) addr -= p.channels[i];
    return StepPattern{static_cast<uint8_t>(addr),
                       static_cast<uint8_t>(all_off(p) & ~(1u << g))};
}

int mux_channel(const ChainProfile& p, int step, int sense)
{
    if(step < 0 || step >= scan_steps(p)) return -1;
    if(sense < 0 || sense >= p.sense_pins) return -1;
    return step * p.sense_pins + sense;
}

uint32_t chain_word(const ChainProfile& p, StepPattern s, uint32_t leds)
{
    const uint32_t led_mask = (1u << p.led_bits) - 1u;
    return (static_cast<uint32_t>(s.address & 0x0Fu) << p.addr_shift)
           | (static_cast<uint32_t>(s.enable_mask & all_off(p))
              << p.enable_shift)
           | ((leds & led_mask) << p.led_shift);
}

} // namespace shell
```

- [ ] **Step 5: Run test to verify it passes**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure -R spky_tests
```

Expected: PASS, including both new profile cases.

- [ ] **Step 6: Point the firmware at the active profile**

In `shell/mux_scan.h`, above the class, add the profile selection and change the array bound:

```cpp
// Which board this image is built for. The switch header is generated at
// Makefile PARSE time; see write_shell_coupon_probe.py for why a bare -D
// is not enough. Task 3 of this plan adds that switch -- until then this
// resolves to the panel.
#if defined(SHELL_COUPON_PROBE) && SHELL_COUPON_PROBE
inline constexpr ChainProfile kActiveChain = kCouponChain;
#else
inline constexpr ChainProfile kActiveChain = kPanelChain;
#endif

...
extern volatile float g_mux_values[mux_total(kActiveChain)];
```

In `shell/mux_scan.cpp`, change the definition and every call:

```cpp
volatile float g_mux_values[mux_total(kActiveChain)] = {};
...
            const int ch = mux_channel(kActiveChain, live_step_, s);
...
    leds_ = (leds_ + 1u) & ((1u << kActiveChain.led_bits) - 1u);

    const StepPattern p = step_pattern(kActiveChain, next_step_);
    write_chain(chain_word(kActiveChain, p, leds_));
...
    next_step_ = (next_step_ + 1) % scan_steps(kActiveChain);
```

and in `write_chain`, replace `kChainBits` with `kActiveChain.chain_bits`, and the sense loop bound `kSensePins` with `kActiveChain.sense_pins`.

- [ ] **Step 7: Build the firmware**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images
```

Expected: links clean, and `SHELL_MUX_PROBE=1` still builds:

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 SHELL_CPU_PROBE=1 SHELL_MUX_PROBE=1 images
```

- [ ] **Step 8: Commit**

```bash
git add shell/mux_plan.h shell/mux_plan.cpp shell/mux_scan.h shell/mux_scan.cpp tests/test_mux_plan.cpp
git commit -m "feat(shell): the chain layout is a profile, and the coupon is the second one

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The coupon's expected channel table

What each coupon channel should read is fixed by the netlist, so the board's verdict is a table and not a judgement call at the bench. The table lives in C++ and is host-tested, which puts the gate inside ctest — `tools/test_*.py` has no runner in this repo, and a guard nobody runs is how the panel guard stood red for 23 days.

**Files:**
- Create: `shell/coupon_expect.h`
- Create: `shell/coupon_expect.cpp`
- Create: `tests/test_coupon_expect.cpp`
- Modify: `CMakeLists.txt:93-94` (add both new files to the same list)

**Interfaces:**
- Consumes: `shell::kCouponChain`, `shell::scan_steps`, `shell::group_of_step` from Task 2
- Produces:
  - `enum class shell::Expect { Low, High, Mid, Unchecked }`
  - `shell::Expect shell::coupon_expect(int step)`
  - `bool shell::coupon_verdict(Expect e, uint16_t raw)`
  - `inline constexpr uint16_t shell::kRailMargin`, `shell::kMidMargin`

- [ ] **Step 1: Write the failing test**

Create `tests/test_coupon_expect.cpp`:

```cpp
// What the coupon must read, per channel. The table is derived from
// hardware/coupon/scripts/netlist.py and nothing else; if the two disagree,
// the netlist wins and this file is wrong.
#include <doctest/doctest.h>
#include "../shell/coupon_expect.h"
#include "../shell/mux_plan.h"

TEST_CASE("coupon expect: the 4067's rail ties and dividers") {
    using shell::Expect;
    // MUX16 channel plan, proof/review.md section 2:
    //  0,2,4,6  RV1..RV4 wipers      -- pots, not asserted
    //  1,5      R_HI1/R_HI2 -> A+3V3
    //  3,7      R_LO1/R_LO2 -> AGND
    //  8        REF_A 10k/10k        -- mid scale
    //  9        REF_B 1k/1k          -- mid scale
    //  10..15   R_SP10..15 -> AGND
    const Expect want[16] = {
        Expect::Unchecked, Expect::High, Expect::Unchecked, Expect::Low,
        Expect::Unchecked, Expect::High, Expect::Unchecked, Expect::Low,
        Expect::Mid,       Expect::Mid,  Expect::Low,       Expect::Low,
        Expect::Low,       Expect::Low,  Expect::Low,       Expect::Low};
    for(int a = 0; a < 16; ++a)
        CHECK(shell::coupon_expect(a) == want[a]);
}

TEST_CASE("coupon expect: the 4051's rail ties and divider") {
    using shell::Expect;
    // MUX8 starts at step 16. Channels: 0,2,4 = RV5..RV7 wipers; 1,5 =
    // R_HI3/R_HI4 -> A+3V3; 3,7 = R_LO3/R_LO4 -> AGND; 6 = REF_C 10k/10k.
    const Expect want[8] = {
        Expect::Unchecked, Expect::High, Expect::Unchecked, Expect::Low,
        Expect::Unchecked, Expect::High, Expect::Mid,       Expect::Low};
    for(int a = 0; a < 8; ++a)
        CHECK(shell::coupon_expect(16 + a) == want[a]);
}

TEST_CASE("coupon expect: a step that does not exist is unchecked") {
    CHECK(shell::coupon_expect(-1) == shell::Expect::Unchecked);
    CHECK(shell::coupon_expect(shell::scan_steps(shell::kCouponChain))
          == shell::Expect::Unchecked);
}

TEST_CASE("coupon expect: the verdict brackets are not a matter of taste") {
    using shell::Expect;
    // 16-bit conversions, full scale 65535. A rail tie through 0 ohms has
    // nothing to pull it off the rail, so the margin is generous on purpose
    // -- it is there to catch an open or a swapped net, not to grade noise.
    CHECK(shell::coupon_verdict(Expect::Low, 0));
    CHECK(shell::coupon_verdict(Expect::Low, shell::kRailMargin));
    CHECK_FALSE(shell::coupon_verdict(Expect::Low, shell::kRailMargin + 1));

    CHECK(shell::coupon_verdict(Expect::High, 65535));
    CHECK(shell::coupon_verdict(Expect::High, 65535 - shell::kRailMargin));
    CHECK_FALSE(shell::coupon_verdict(Expect::High,
                                      65535 - shell::kRailMargin - 1));

    // Mid scale is 32767 or 32768; both dividers are two equal resistors.
    CHECK(shell::coupon_verdict(Expect::Mid, 32768));
    CHECK(shell::coupon_verdict(Expect::Mid, 32768 - shell::kMidMargin));
    CHECK_FALSE(shell::coupon_verdict(Expect::Mid,
                                      32768 - shell::kMidMargin - 1));

    // An unfitted pot floats. Anything it reads is allowed, including 0.
    CHECK(shell::coupon_verdict(Expect::Unchecked, 0));
    CHECK(shell::coupon_verdict(Expect::Unchecked, 65535));
}
```

Add both new files to the `spky_tests` source list in `CMakeLists.txt`, right after the `tests/test_mux_plan.cpp` line:

```cmake
    shell/coupon_expect.cpp
    tests/test_coupon_expect.cpp
```

- [ ] **Step 2: Run test to verify it fails**

```bash
source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build --output-on-failure -R spky_tests
```

Expected: compile error, `shell/coupon_expect.h: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `shell/coupon_expect.h`:

```cpp
#pragma once

// What the test coupon must read on each channel, derived from
// hardware/coupon/scripts/netlist.py. Data logic with no hardware type in
// it, for the same reason as mux_plan.h: this is where a wrong expectation
// is a visible line rather than a board that "looks broken" at the bench.
#include <cstdint>

namespace shell {

enum class Expect
{
    Low,        // tied to AGND through 0 ohms
    High,       // tied to A+3V3 through 0 ohms
    Mid,        // a divider of two equal resistors
    Unchecked,  // a pot wiper: floats until the pot is fitted
};

// How far off a rail a rail-tied channel may read, in 16-bit counts. 2 % of
// full scale. Generous on purpose: this catches an open, a swapped net or a
// dead enable, not noise.
inline constexpr uint16_t kRailMargin = 1311;

// How far off mid scale a divider may read. 5 % of full scale, which covers
// 1 % resistors with room to spare and still fails a divider that is not
// there.
inline constexpr uint16_t kMidMargin = 3277;

// The expectation for a scan step of kCouponChain, or Unchecked for a step
// that does not exist.
Expect coupon_expect(int step);

// Whether a raw 16-bit conversion satisfies an expectation.
bool coupon_verdict(Expect e, uint16_t raw);

} // namespace shell
```

Create `shell/coupon_expect.cpp`:

```cpp
#include "coupon_expect.h"

#include "mux_plan.h"

namespace shell {

namespace {

// MUX16, proof/review.md section 2. Pot wipers sit on the even channels of
// the first eight, with their two neighbours at opposite rails -- that
// arrangement is requirement 5 and the reason the rails appear twice.
constexpr Expect kMux16[16] = {
    Expect::Unchecked, Expect::High, Expect::Unchecked, Expect::Low,
    Expect::Unchecked, Expect::High, Expect::Unchecked, Expect::Low,
    Expect::Mid,       Expect::Mid,  Expect::Low,       Expect::Low,
    Expect::Low,       Expect::Low,  Expect::Low,       Expect::Low};

constexpr Expect kMux8[8] = {
    Expect::Unchecked, Expect::High, Expect::Unchecked, Expect::Low,
    Expect::Unchecked, Expect::High, Expect::Mid,       Expect::Low};

} // namespace

Expect coupon_expect(int step)
{
    const int g = group_of_step(kCouponChain, step);
    if(g < 0) return Expect::Unchecked;
    const int addr = (g == 0) ? step : step - kCouponChain.channels[0];
    return (g == 0) ? kMux16[addr] : kMux8[addr];
}

bool coupon_verdict(Expect e, uint16_t raw)
{
    switch(e)
    {
        case Expect::Low: return raw <= kRailMargin;
        case Expect::High: return raw >= 65535 - kRailMargin;
        case Expect::Mid:
            return raw >= 32768 - kMidMargin && raw <= 32768 + kMidMargin;
        case Expect::Unchecked: break;
    }
    return true;
}

} // namespace shell
```

- [ ] **Step 4: Run test to verify it passes**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure -R spky_tests
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add shell/coupon_expect.h shell/coupon_expect.cpp tests/test_coupon_expect.cpp CMakeLists.txt
git commit -m "feat(shell): what the coupon must read is a table, not a judgement call

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: The bring-up probe

A foreground scan that walks every coupon channel slowly, reads the free-running ADC, judges it against Task 3's table and prints the lot over USB-CDC. No timing work: the channel is held for milliseconds, so libDaisy's DMA scan is fine here — it is only the 1.6 µs question it cannot answer.

**How long to hold each channel, derived:** libDaisy converts at 12.29 MHz (`adc.cpp:229`, `ADC_CLOCK_ASYNC_DIV2` on the 24.58 MHz kernel clock) with the default `SPEED_8CYCLES_5` sampling plus 8.5 cycles of 16-bit conversion — 17 ADC cycles, 1.38 µs. The patch_sm init takes the default `OVS_32` and scans 12 channels, so one full round is 12 × 32 × 1.38 µs ≈ 531 µs. Holding 5 ms gives nine complete rounds, so every reported value is certainly of the channel being held.

**Files:**
- Create: `shell/coupon_scan.h`, `shell/coupon_scan.cpp`
- Create: `shell/write_shell_coupon_probe.py`
- Modify: `shell/Makefile` (switch block, `SWITCH_OBJECTS`, `SWITCH_HEADERS`, `CPP_SOURCES`, the `main.o` dependency line)
- Modify: `shell/main.cpp`

**Interfaces:**
- Consumes: `shell::kCouponChain`, `shell::step_pattern`, `shell::chain_word`, `shell::group_of_step`, `shell::scan_steps` (Task 2); `shell::coupon_expect`, `shell::coupon_verdict` (Task 3); `shell::MuxScan::write_chain` — which must become accessible, so move it to `public:` in `shell/mux_scan.h` with a comment saying the bring-up probe drives it directly
- Produces: `shell::run_coupon_bringup(bench::Board& hw)`, which never returns

- [ ] **Step 1: Write the switch generator**

Create `shell/write_shell_coupon_probe.py`. It is `write_shell_cpu_probe.py` with a different symbol; the header must be defined in **both** positions because the code tests it with `#if`:

```python
"""Writes the coupon-probe switch as a real header.

Same shape and same reason as write_shell_cpu_probe.py: a bare -D is
invisible to make's dependency graph, and an existing build/ would happily
reuse a stale main.o -- in the worst case shipping a measurement of the
wrong board under the right name.

Like write_shell_mux_probe.py this ALWAYS defines the symbol, including in
position 0, because `#if SHELL_COUPON_PROBE` has to work in both.

THE TIMESTAMP EDGE IS NOT ENOUGH; that happened on this machine on
2026-08-23, when a header written 0.36 s after main.o landed in the same
wall-clock second and make judged it "not newer". So this script deletes the
dependent objects itself whenever the content changes, which hangs on no
timer resolution. The objects come in as further arguments.
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in {"0", "1"}:
        raise SystemExit(
            "usage: write_shell_coupon_probe.py OUTPUT {0|1} [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    # The script runs while the Makefile is parsed, so before any rule has
    # created build/.
    output.parent.mkdir(parents=True, exist_ok=True)
    content = "#define SHELL_COUPON_PROBE %s\n" % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[3:]:
            Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Wire the switch into the Makefile**

In `shell/Makefile`, after the `SHELL_MUX_PROBE` block, add:

```make
# --- Coupon-Sonde --------------------------------------------------------
# 0 aus, 1 der Bring-up-Scan fuer den Testcoupon (hardware/coupon/): laeuft
# im Vordergrund, haelt jeden Kanal 5 ms, liest den frei laufenden ADC und
# meldet pro Kanal ein Urteil ueber USB-CDC. KEIN Zeitmass -- die 1,6-us-
# Frage ist die Settle-Sonde, nicht diese.
#
# Stellung 1 baut ausserdem das Kettenprofil des Coupons statt des Panels;
# die beiden Platinen haben nicht eine Zahl gemeinsam. Plan:
# ../docs/superpowers/plans/2026-09-17-coupon-bring-up.md
SHELL_COUPON_PROBE ?= 0

ifneq ($(filter $(SHELL_COUPON_PROBE),0 1),$(SHELL_COUPON_PROBE))
$(error SHELL_COUPON_PROBE must be 0 or 1)
endif
```

Then — and this is the part that is easy to forget and looks like a measurement when it goes wrong — extend `SWITCH_OBJECTS`, because `mux_scan.o` and `coupon_scan.o` now read the switch too:

```make
SWITCH_OBJECTS = $(BUILD_DIR)/main.o $(BUILD_DIR)/mux_scan.o $(BUILD_DIR)/coupon_scan.o
```

Add the generator to `SWITCH_HEADERS`:

```make
  $(shell python write_shell_coupon_probe.py $(BUILD_DIR)/shell_coupon_probe.h $(SHELL_COUPON_PROBE) $(SWITCH_OBJECTS)) \
```

Extend the explicit dependency line and add one for the two other objects:

```make
$(BUILD_DIR)/main.o: $(BUILD_DIR)/shell_selftest.h $(BUILD_DIR)/shell_cpu_probe.h $(BUILD_DIR)/shell_mux_probe.h $(BUILD_DIR)/shell_idle_fill.h $(BUILD_DIR)/shell_coupon_probe.h
$(BUILD_DIR)/mux_scan.o $(BUILD_DIR)/coupon_scan.o: $(BUILD_DIR)/shell_coupon_probe.h
```

And add the new sources to `CPP_SOURCES`, next to `mux_scan.cpp`:

```make
	coupon_expect.cpp \
	coupon_scan.cpp \
```

- [ ] **Step 3: Write the probe**

Create `shell/coupon_scan.h`:

```cpp
#pragma once

// The test coupon's bring-up scan (hardware/coupon/). Walks every channel,
// holds it long enough that the free-running ADC certainly reports it,
// judges it against coupon_expect.h and prints the lot over USB-CDC.
//
// It is NOT a settle measurement: libDaisy's ADC scans twelve channels at
// OVS_32 and has no defined time relation to the address write, which is
// exactly why the settle probe owns ADC1 itself
// (docs/superpowers/specs/2026-09-17-coupon-settle-probe-design.md section 3).
// This probe answers the other question -- whether the board is wired the
// way the netlist says -- and that one has no clock in it.
#include "hw/board.h"

namespace shell {

// Runs the scan once, then repeats the report forever. Never returns.
void run_coupon_bringup(bench::Board& hw);

} // namespace shell
```

Create `shell/coupon_scan.cpp`:

```cpp
#include "coupon_scan.h"

#include "coupon_expect.h"
#include "mux_plan.h"
#include "mux_scan.h"

namespace shell {

namespace {

// Nine full DMA rounds. libDaisy converts at 12.29 MHz (adc.cpp:229) with
// SPEED_8CYCLES_5 sampling plus 8.5 cycles of 16-bit conversion = 17 ADC
// cycles = 1.38 us, at OVS_32 over twelve channels = 531 us per round. Five
// milliseconds is not a round number picked for comfort.
constexpr uint32_t kHoldMs = 5;

constexpr int kSteps = scan_steps(kCouponChain);

uint16_t g_raw[kSteps] = {};

} // namespace

void run_coupon_bringup(bench::Board& hw)
{
    MuxScan chain;
    chain.init();

    for(int s = 0; s < kSteps; ++s)
    {
        // All LEDs dark during the scan. A walking pattern would be prettier
        // and would also put a changing digital load beside the analog read
        // -- that is the noise question, and it is not this probe's.
        chain.write_chain(chain_word(kCouponChain, step_pattern(kCouponChain, s), 0u));
        hw.Delay(kHoldMs);

        const int g     = group_of_step(kCouponChain, s);
        const int sense = kCouponChain.sense_of_group[g];
        g_raw[s] = static_cast<uint16_t>(
            hw.adc.Get(static_cast<uint8_t>(kCouponChain.sense_adc_base + sense)));
    }

    // Park with both muxes disabled before the port opens: nothing should be
    // connected to a sense pin while nobody is reading it.
    chain.write_chain(chain_word(kCouponChain, step_pattern(kCouponChain, -1), 0u));

    hw.StartLog(false);

    int failures = 0;
    for(int s = 0; s < kSteps; ++s)
        if(!coupon_verdict(coupon_expect(s), g_raw[s])) ++failures;

    while(1)
    {
        hw.PrintLine("COUPON_BEGIN steps=%d hold_ms=%d fails=%d",
                     kSteps, static_cast<int>(kHoldMs), failures);
        for(int s = 0; s < kSteps; ++s)
        {
            const int    g = group_of_step(kCouponChain, s);
            const Expect e = coupon_expect(s);
            hw.PrintLine("COUPON_CH step=%d group=%d addr=%d sense=%d raw=%d "
                         "expect=%d pass=%d",
                         s, g,
                         static_cast<int>(step_pattern(kCouponChain, s).address),
                         kCouponChain.sense_of_group[g],
                         static_cast<int>(g_raw[s]), static_cast<int>(e),
                         coupon_verdict(e, g_raw[s]) ? 1 : 0);
        }
        hw.PrintLine("COUPON_END");
        hw.Delay(1000);
    }
}

} // namespace shell
```

**Why the raw getter and not `GetAdcValue`:** `bench::Board` is an alias for `daisy::patch_sm::DaisyPatchSM` (`src/hw/board.h:24`), whose `AdcHandle adc` member is public (`daisy_patch_sm.h:248`, inside the `public:` block that opens at line 54). `adc.Get()` hands back the unconditioned 16-bit conversion, which is what `coupon_expect.h`'s brackets are written in. `GetAdcValue()` would route through libDaisy's `AnalogControl` — and for `ADC_9..ADC_12` that is a plain `Init()` rather than `InitBipolarCv()`, but it still normalises to 0..1 and carries the control's slew, so the brackets would have to be rewritten in floats for no gain.

- [ ] **Step 4: Let `mux_scan.h` actually see the switch, and make `write_chain` reachable**

Task 2 left `mux_scan.h` selecting its profile with `#if defined(SHELL_COUPON_PROBE) && SHELL_COUPON_PROBE`, and nothing had defined that symbol yet. Now that the header exists, `mux_scan.h` must include it **itself** — `main.cpp` including it is not enough. `mux_scan.cpp` includes only `mux_scan.h`, so without this line `mux_scan.o` compiles against the panel profile while `main.o` compiles against the coupon's, and `g_mux_values` gets two different sizes in one link. That is an ODR violation the linker will not report.

At the top of `shell/mux_scan.h`, above `#include "mux_plan.h"`:

```cpp
// Which board this image is for. Included HERE and not left to main.cpp:
// mux_scan.cpp sees only this header, and a profile that differs between
// two objects gives g_mux_values two sizes in one link.
#include "shell_coupon_probe.h"
```

and simplify the guard below it to `#if SHELL_COUPON_PROBE`, since the symbol is now always defined.

Then move `void write_chain(uint32_t word);` from `private:` to `public:` with:

```cpp
    // Public because the coupon bring-up probe drives the chain directly
    // instead of stepping the scan: it has to hold one address still while
    // the ADC is read, which step() deliberately never does.
    void write_chain(uint32_t word);
```

- [ ] **Step 5: Wire it into main.cpp**

Add the switch header to the include block at the top, after `shell_idle_fill.h`:

```cpp
#include "shell_coupon_probe.h"
```

And immediately after `hw.Init()` and the memory setup — before the engine is configured, because this image never reaches the audio callback — add:

```cpp
#if SHELL_COUPON_PROBE
    // The board under test is the coupon, not an instrument. No engine, no
    // audio, no operating point: this image exists to say whether the thing
    // is wired the way the netlist claims.
    shell::run_coupon_bringup(hw);   // never returns
#endif
```

with `#if SHELL_COUPON_PROBE` / `#include "coupon_scan.h"` / `#endif` next to the existing `SHELL_MUX_PROBE` include block.

Also add the USB identity strings guard: the probe calls `StartLog`, so the image needs `USBD_MANUFACTURER_STRING`/`USBD_PRODUCT_STRING_HS`, which today only exist under `#if defined(SHELL_CPU_PROBE)`. Change that condition to:

```cpp
#if defined(SHELL_CPU_PROBE) || SHELL_COUPON_PROBE
```

around the `extern "C"` block only — not around the `CpuLoadMeter` include.

- [ ] **Step 6: Build both switch positions and prove they differ**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 SHELL_COUPON_PROBE=0 images && cp shell/build/shell-sram.bin shell/build/compare-p0.bin && make -C shell -j8 SHELL_COUPON_PROBE=1 images && cmp shell/build/compare-p0.bin shell/build/shell-sram.bin
```

Expected: `cmp` reports the files differ. **If they are byte-identical, the switch did not take** — that is the stale-object trap the Makefile comment describes, and it means an object reading `SHELL_COUPON_PROBE` is missing from `SWITCH_OBJECTS`. Do not flash anything until this step differs. Delete `shell/build/compare-p0.bin` afterwards; `shell/build/` is not tracked, but leaving a stray image beside the real ones is how the wrong file gets flashed.

- [ ] **Step 7: Commit**

```bash
git add shell/coupon_scan.h shell/coupon_scan.cpp shell/write_shell_coupon_probe.py shell/Makefile shell/main.cpp shell/mux_scan.h
git commit -m "feat(shell): a bring-up scan that judges the coupon against its own netlist

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: The button comes back through the 165

Requirement 7 is "the real 595 chain", and the coupon wires a tactile switch through a 74HC165 whose `Q7` returns on `D10` (`netlist.py:275`). `MuxScan` initialises that pin as an input with a pull-up and then never clocks anything in. The read side is data logic, so which bit carries the button is asserted on the host.

`~PL` sits on the same line as the 595s' `RCLK`, which is what makes four GPIOs enough: latch low loads the 165 in parallel, latch high both latches the 595 outputs and puts the 165 into shift mode. `Q7` is the last stage, so the first bit available after the load is `D7` and the button on `D0` arrives eighth. `D1..D7` are tied to `GND` and `DS` is tied to `GND`, so everything after the eighth bit is zero. The switch pulls `BTN_1` to ground against a 10 k pull-up: **pressed reads 0.**

**Files:**
- Modify: `shell/mux_plan.h`, `shell/mux_plan.cpp`
- Modify: `shell/mux_scan.h`, `shell/mux_scan.cpp`
- Modify: `tests/test_mux_plan.cpp`
- Modify: `shell/coupon_scan.cpp`

**Interfaces:**
- Consumes: `shell::ChainProfile` (Task 2), `shell::MuxScan` (Task 4)
- Produces: `int shell::button_bit(const ChainProfile&)`; `uint32_t shell::MuxScan::read_chain(uint32_t word)`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_mux_plan.cpp`:

```cpp
TEST_CASE("mux plan: the coupon's button is the eighth bit shifted out") {
    // U_IN1 is a 74HC165 with Q7 on SR_DATA_IN. Q7 is the LAST parallel
    // stage, so after ~PL the first bit read is D7 and D0 arrives eighth.
    // netlist.py:283 puts BTN_1 on D0 and ties D1..D7 to GND, so bit 7
    // counting from the first bit read is the only one that can move.
    CHECK(shell::button_bit(shell::kCouponChain) == 7);
    // The shipping panel has no single button on the chain yet.
    CHECK(shell::button_bit(shell::kPanelChain) == -1);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure -R spky_tests
```

Expected: compile error, `'button_bit' is not a member of 'shell'`.

- [ ] **Step 3: Write minimal implementation**

Add to `ChainProfile` in `shell/mux_plan.h`, after `led_bits`:

```cpp
    int button_bit;        // index into the bits shifted out of the 165, -1 = none
```

Set it in both profile literals — `kPanelChain` gains a trailing `, -1` and `kCouponChain` a trailing `, 7`. Declare below the other functions:

```cpp
// Which bit of the 74HC165 return stream carries the board's button, counted
// from the first bit shifted out, or -1 if the board has none.
int button_bit(const ChainProfile& p);
```

and in `shell/mux_plan.cpp`:

```cpp
int button_bit(const ChainProfile& p) { return p.button_bit; }
```

- [ ] **Step 4: Run test to verify it passes**

```bash
source env.sh && cmake --build build && ctest --test-dir build --output-on-failure -R spky_tests
```

Expected: PASS.

- [ ] **Step 5: Read the stream in the firmware**

In `shell/mux_scan.h`, declare next to `write_chain`:

```cpp
    // Clocks `word` out and the 165's parallel load back in, in the same
    // pass -- the two chains share clock and latch, so a separate read pass
    // would cost a second latch and re-load the buttons mid-flight.
    // Returns the return stream, first bit shifted out in bit 0.
    uint32_t read_chain(uint32_t word);
```

In `shell/mux_scan.cpp`, add:

```cpp
uint32_t MuxScan::read_chain(uint32_t word)
{
    // Latch LOW first: that is the 165's ~PL, and the parallel load happens
    // while it is low. The 595s do not care -- they latch on the rising
    // edge at the end.
    latch_.Write(false);
    latch_.Write(true);

    uint32_t in = 0;
    for(int i = kActiveChain.chain_bits - 1; i >= 0; --i)
    {
        // Sample BEFORE the clock edge: the bit standing at Q7 now is the
        // one the previous edge shifted there.
        if(sense_in_.Read())
            in |= 1u << (kActiveChain.chain_bits - 1 - i);
        data_.Write(((word >> i) & 1u) != 0u);
        clock_.Write(true);
        clock_.Write(false);
    }
    latch_.Write(true);
    latch_.Write(false);
    return in;
}
```

- [ ] **Step 6: Report it from the bring-up probe**

In `shell/coupon_scan.cpp`, replace the park line and everything between it and the `while(1)` with this. The park write becomes a read pass, so the muxes end up disabled exactly as before and the button state reported is the one standing when the scan finished:

```cpp
    // Park with both muxes disabled, and take the 165's stream on the same
    // pass -- the two chains share clock and latch, so a separate read would
    // cost a second latch and re-load the buttons mid-flight.
    const uint32_t parked
        = chain_word(kCouponChain, step_pattern(kCouponChain, -1), 0u);
    const uint32_t ret     = chain.read_chain(parked);
    const int      bb      = button_bit(kCouponChain);
    const int      pressed = (bb < 0) ? -1 : (((ret >> bb) & 1u) == 0u ? 1 : 0);

    hw.StartLog(false);

    int failures = 0;
    for(int s = 0; s < kSteps; ++s)
        if(!coupon_verdict(coupon_expect(s), g_raw[s])) ++failures;
```

and replace the `COUPON_BEGIN` line inside the loop with:

```cpp
        hw.PrintLine("COUPON_BEGIN steps=%d hold_ms=%d fails=%d button=%d "
                     "ret=%d",
                     kSteps, static_cast<int>(kHoldMs), failures, pressed,
                     static_cast<int>(ret));
```

- [ ] **Step 7: Build and prove the switch still separates**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 SHELL_COUPON_PROBE=1 images
```

Expected: links clean.

- [ ] **Step 8: Commit**

```bash
git add shell/mux_plan.h shell/mux_plan.cpp shell/mux_scan.h shell/mux_scan.cpp shell/coupon_scan.cpp tests/test_mux_plan.cpp
git commit -m "feat(shell): the 165 return stream comes back, and the button with it

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: The reader, with a runner

`shell/read_probe.py` returns on the first matching line; this report is a block. The parser is a pure function so it can be tested without a board — and the test gets registered in ctest, because `tools/test_*.py` in this repo is wired to nothing, and that is how the panel guard stood red for 23 days.

**Files:**
- Create: `shell/read_coupon.py`
- Create: `shell/test_read_coupon.py`
- Modify: `CMakeLists.txt` (register the guard as a ctest test)

**Interfaces:**
- Consumes: the line format from Task 4 and Task 5
- Produces: `parse_block(lines) -> dict`, `format_csv(block) -> str`

- [ ] **Step 1: Write the failing test**

Create `shell/test_read_coupon.py`:

```python
"""Guard for read_coupon.py's parser. Runs as a plain script -- pytest is
not installed on this machine -- and is registered in CMakeLists.txt so
ctest actually runs it. A guard nobody runs is how the VCV panel guard
stood red for 23 days.
"""
import sys

from read_coupon import format_csv, parse_block

SAMPLE = [
    "noise before the block",
    "COUPON_BEGIN steps=2 hold_ms=5 fails=1 button=0 ret=255",
    "COUPON_CH step=0 group=0 addr=0 sense=0 raw=100 expect=3 pass=1",
    "COUPON_CH step=1 group=0 addr=1 sense=0 raw=60000 expect=1 pass=0",
    "COUPON_END",
]


def check(name, got, want):
    if got != want:
        print("FAIL %s: got %r want %r" % (name, got, want), file=sys.stderr)
        return 1
    return 0


def main() -> int:
    bad = 0
    block = parse_block(SAMPLE)
    bad += check("steps", block["steps"], 2)
    bad += check("fails", block["fails"], 1)
    bad += check("button", block["button"], 0)
    bad += check("rows", len(block["rows"]), 2)
    bad += check("raw", block["rows"][1]["raw"], 60000)
    bad += check("pass", block["rows"][1]["pass"], 0)

    # A block that never ends is not a block. Returning a partial one would
    # report a board as clean because the tail never arrived.
    bad += check("incomplete", parse_block(SAMPLE[:-1]), None)
    bad += check("absent", parse_block(["nothing here"]), None)

    csv = format_csv(block)
    bad += check("csv header", csv.splitlines()[0],
                 "step,group,addr,sense,raw,expect,pass")
    bad += check("csv rows", len(csv.splitlines()), 3)

    print("read_coupon guard: %s" % ("FAILED" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Run it to verify it fails**

```bash
python shell/test_read_coupon.py
```

Expected: `ModuleNotFoundError: No module named 'read_coupon'`.

- [ ] **Step 3: Write the reader**

Create `shell/read_coupon.py`:

```python
"""Reads one COUPON_BEGIN..COUPON_END block from the board's USB-CDC port
and writes it as CSV.

The firmware repeats the block forever with a one-second gap and there is no
handshake, so this simply listens until a whole block has arrived. Unlike
read_probe.py it may NOT return on the first matching line: a partial block
would report a clean board because the failing tail never came.

Find the port first:

    python -c "from serial.tools import list_ports; \
print([p.device for p in list_ports.comports()])"

Call:
    python read_coupon.py COM7 [out.csv] [timeout_seconds]
"""
import sys
import time

import serial

FIELDS = ("step", "group", "addr", "sense", "raw", "expect", "pass")


def _fields(line, prefix):
    out = {}
    for token in line[len(prefix):].split():
        key, _, value = token.partition("=")
        out[key] = int(value)
    return out


def parse_block(lines):
    """The first complete block in `lines`, or None if there is not one."""
    block = None
    for line in lines:
        line = line.strip()
        if line.startswith("COUPON_BEGIN"):
            block = _fields(line, "COUPON_BEGIN")
            block["rows"] = []
        elif line.startswith("COUPON_CH") and block is not None:
            block["rows"].append(_fields(line, "COUPON_CH"))
        elif line.startswith("COUPON_END") and block is not None:
            if len(block["rows"]) != block["steps"]:
                block = None
                continue
            return block
    return None


def format_csv(block):
    rows = [",".join(FIELDS)]
    for row in block["rows"]:
        rows.append(",".join(str(row[f]) for f in FIELDS))
    return "\n".join(rows) + "\n"


def main() -> int:
    if len(sys.argv) not in (2, 3, 4):
        raise SystemExit("usage: read_coupon.py PORT [out.csv] [timeout_s]")
    port = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else None
    limit = float(sys.argv[3]) if len(sys.argv) > 3 else 30.0

    lines = []
    with serial.Serial(port, timeout=1.0) as ser:
        deadline = time.monotonic() + limit
        while time.monotonic() < deadline:
            lines.append(ser.readline().decode("utf-8", "replace"))
            block = parse_block(lines)
            if block is not None:
                break
        else:
            block = None

    if block is None:
        print("no complete COUPON block within %.0f s" % limit, file=sys.stderr)
        return 1

    csv = format_csv(block)
    if out:
        with open(out, "w", encoding="utf-8", newline="") as fh:
            fh.write(csv)
    else:
        sys.stdout.write(csv)
    print("fails=%d button=%d" % (block["fails"], block["button"]),
          file=sys.stderr)
    return 1 if block["fails"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Run it to verify it passes**

```bash
python shell/test_read_coupon.py
```

Expected: `read_coupon guard: ok`, exit 0.

- [ ] **Step 5: Give the guard a runner**

In `CMakeLists.txt`, beside the other `add_test` calls:

```cmake
# The Python guards need a runner or they rot unnoticed -- tools/test_*.py in
# this repo is wired to nothing, and the VCV panel guard stood red for 23
# days behind exactly that gap. pytest is not installed here, so it runs as
# a plain script and its exit code is the verdict.
add_test(NAME read_coupon_guard
         COMMAND ${CMAKE_COMMAND} -E env python
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_coupon.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)
```

- [ ] **Step 6: Prove the runner can go red**

Temporarily break the parser — in `read_coupon.py`, change `if len(block["rows"]) != block["steps"]:` to `if False:` — then:

```bash
source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && ctest --test-dir build --output-on-failure -R read_coupon_guard
```

Expected: FAIL on `incomplete`. Restore the line with the Edit tool — **not** with `git checkout`, which would also throw away the rest of the file's uncommitted work — and re-run to see it green.

- [ ] **Step 7: Commit**

```bash
git add shell/read_coupon.py shell/test_read_coupon.py CMakeLists.txt
git commit -m "feat(shell): the coupon report is read as a block, and the guard has a runner

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Done means

- `ctest --test-dir build --output-on-failure` is green after `cmake --build build`, including `read_coupon_guard`.
- `make -C shell SHELL_COUPON_PROBE=0 images` and `SHELL_COUPON_PROBE=1 images` produce images that `cmp` reports as different.
- `make -C shell SHELL_CPU_PROBE=1 SHELL_MUX_PROBE=1 images` still links, and `tests/test_mux_plan.cpp` still asserts the panel's 32 bits and 19 LEDs.
- Nothing under `hardware/coupon/` is modified.

## What this plan does NOT do

- No settle measurement. That is the spec's §4/§6/§7 and needs ADC1 taken away from libDaisy; it is a separate plan.
- No flashing, and no claim about what the board reads. Every number here is a derivation from the netlist and the datasheets; the first real measurement happens when someone flashes `SHELL_COUPON_PROBE=1` and runs `read_coupon.py`. Until then `fails=` is a prediction.
- No crosstalk configuration and no pot channels beyond `Expect::Unchecked`. Once the pots are fitted, `coupon_expect.h`'s two tables gain entries and `tests/test_coupon_expect.cpp` gains the matching assertions.
