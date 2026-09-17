# Coupon Settle Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure how long a multiplexer channel on the test coupon actually takes to settle to within half an LSB, through the ADC, and make the run refuse to report a number when the instrument is not good enough to produce one.

**Architecture:** The probe takes ADC1 away from libDaisy after `hw.Init()` and drives it single-shot with no oversampling, timing from the 595 latch edge with the DWT cycle counter. Everything that can be decided without hardware — the pair table, the delay grid, the `d_settle` reduction and the four honesty gates — lives in a hardware-free translation unit that the host test suite exercises. The firmware is then a thin loop that feeds measurements into those pure functions.

**Tech Stack:** ARM GCC via `make` (libDaisy, STM32H7 HAL), C++17, doctest on the host through CMake/ctest, Python 3 + pyserial 3.5 for the reader.

**Spec:** [`docs/superpowers/specs/2026-09-17-coupon-settle-probe-design.md`](../specs/2026-09-17-coupon-settle-probe-design.md) — read it before Task 1. The plan argues from it throughout and cites section numbers; §7a in particular is where every gate constant comes from.

## Already built — do not redo

The spec was written before the bring-up branch landed. Two of its sections describe work that now exists on `main`:

- **§5 (the board profile) is done.** `shell/mux_plan.h` already carries `struct ChainProfile` with `kPanelChain` and `kCouponChain`, and `step_pattern()` / `chain_word()` / `group_of_step()` / `scan_steps()` already take a profile argument. `tests/test_mux_plan.cpp` already exercises both. **Do not reintroduce the profile struct.**
- **§9's first bullet is done** for the same reason — that RED was proven on the bring-up branch.

What remains unbuilt is the instrument itself (§4), the sweep (§6), the zero point and its gates (§7/§7a), the output (§8) and the reader.

## Global Constraints

- **Everything written into the repo is English** — code, comments, commit messages, docs. The conversation is German; the files are not. `shell/Makefile` and `shell/main.cpp` contain older German comment blocks. **Leave them alone.** New content is English; do not translate the neighbours and do not file the mismatch as a defect.
- **Never prefix a shell command with `cd`.** The Bash tool already starts in the repo root. Use `make -C shell`, not `cd shell && make`.
- **The firmware build must never `source env.sh`.** Two toolchains, never mixed:
  ```bash
  PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_SETTLE_PROBE=1
  ```
  `images`, not `all`.
- **The host build needs `env.sh` and `-DCMAKE_BUILD_TYPE=Release`.** Release is not optional; a Debug configure makes `spky_tests` and `ctrl_identity` fail with "SYNTH reference moved".
  ```bash
  source env.sh; cmake -S . -B build -DCMAKE_BUILD_TYPE=Release; cmake --build build; ctest --test-dir build --output-on-failure
  ```
- **`ctest` does not build.** A green run can be a stale binary. Always `cmake --build build` first.
- **Commit trailer** is `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`, not the Anthropic default.
- **A test that cannot go red gets fixed.** Every task that adds a gate proves the RED once, by hand, and says in its report what it saw. Do not claim a RED you did not watch.
- **Never `git checkout <file>` to undo** a deliberate sabotage — retype the original. A scripted edit rewrites the whole file's line endings and the diff stays deceptively clean.
- **Do not touch `hardware/coupon/`.** It carries uncommitted work that is not yours. Read `netlist.py` freely; write nothing there.
- **No runtime claim without a probe.** Every number in this plan that came from the model is labelled derived. Do not upgrade one to "measured" in a report because the code compiled.
- **`ADC_CLOCK_ASYNC_DIV2` stays untouched** so the 12.29 MHz of `settle-budget.md` §1 still holds. Everything timed here is quoted against that clock.

---

## File Structure

| File | Responsibility |
|---|---|
| `shell/settle_plan.h` / `.cpp` | **New.** Pure data and pure functions: the six pair table, the delay grid, `d_settle_index()`, the four gates. No hardware type ever enters. Same reason as `mux_plan.h` and `coupon_expect.h`: this is where a wrong constant is a visible line instead of a board that "looks slow". |
| `tests/test_settle_plan.cpp` | **New.** Host gate for the above. Recomputes the table's predictions from first principles rather than copying them. |
| `shell/cycles.h` | **New.** Copy of `bench/cycles.h`. `shell/README.md` is explicit that exactly one file is shared with `bench/` (`src/hw/board.h`) and that this is deliberate, so this is a copy carrying an origin comment, not an include across trees. |
| `shell/settle_probe.h` / `.cpp` | **New.** The instrument. Owns ADC1, does the timed measurement, runs the sweep, prints the block. Board-only; never compiled on the host. |
| `shell/write_shell_settle_probe.py` | **New.** Switch-header generator, same shape and same reason as `write_shell_coupon_probe.py`. |
| `shell/mux_scan.h` / `.cpp` | **Modify.** Gains `write_chain_timed()` — the existing `write_chain()` cannot return the latch-edge timestamp and `t = 0` is defined as that edge (§4). |
| `shell/Makefile` | **Modify.** New `SHELL_SETTLE_PROBE` switch, new sources, new generator, new stale-object entry. |
| `shell/main.cpp` | **Modify.** Dispatch, alongside the existing probes. |
| `shell/read_settle.py` | **New.** Collects one complete block from USB-CDC, writes CSV. |
| `shell/test_read_settle.py` | **New.** Host guard for the parser, no board and no pyserial needed. |
| `CMakeLists.txt` | **Modify.** `settle_plan.cpp` + its test into `spky_tests`; `test_read_settle.py` as its own `add_test`. |

Six tasks. Tasks 1, 2 and 6 are host-only and need no board. Tasks 3, 4 and 5 need the coupon on USB.

---

### Task 1: The pair table and the delay grid

**Files:**
- Create: `shell/settle_plan.h`, `shell/settle_plan.cpp`
- Create: `tests/test_settle_plan.cpp`
- Modify: `CMakeLists.txt` (around line 95, beside `shell/coupon_expect.cpp`)

**Interfaces:**
- Consumes: `shell/mux_plan.h` — `kCouponChain`, `ChainProfile::channels`.
- Produces: `struct SettlePair`, `kSettlePlan[kSettlePairs]`, `kGridStepNs`, `kGridPoints`, `kRepeats`, `kSettleCounts`, `kParkNs`, `grid_ns(int)`. Tasks 2, 4 and 5 all build on these exact names.

- [ ] **Step 1: Write the failing test**

Create `tests/test_settle_plan.cpp`. Note what it does **not** do: it does not copy the spec's `tau9_ns` column. It recomputes it, so that a typo in the table is a failure rather than a matching pair of typos.

```cpp
// The six channel pairs the settle probe steps between, and the grid it
// steps them on. Derived from the settle probe spec section 6 and from
// hardware/coupon/scripts/netlist.py; if the two disagree, the netlist wins
// and this file is wrong.
#include <doctest/doctest.h>
#include "../shell/settle_plan.h"
#include "../shell/mux_plan.h"

TEST_CASE("settle plan: the grid is monotonic and ends where the spec says") {
    CHECK(shell::kGridPoints == 65);
    CHECK(shell::kGridStepNs == 100);
    CHECK(shell::grid_ns(0) == 0u);
    CHECK(shell::grid_ns(shell::kGridPoints - 1) == 6400u);
    for(int i = 1; i < shell::kGridPoints; ++i)
        CHECK(shell::grid_ns(i) > shell::grid_ns(i - 1));
}

TEST_CASE("settle plan: every channel exists on the mux it names") {
    for(int p = 0; p < shell::kSettlePairs; ++p) {
        const shell::SettlePair& sp = shell::kSettlePlan[p];
        CAPTURE(p);
        REQUIRE((sp.group == 0 || sp.group == 1));
        const int n = shell::kCouponChain.channels[sp.group];
        CHECK(sp.from_ch >= 0);
        CHECK(sp.from_ch < n);
        CHECK(sp.to_ch >= 0);
        CHECK(sp.to_ch < n);
        CHECK(sp.from_ch != sp.to_ch);
    }
}

TEST_CASE("settle plan: the predictions are recomputed, not copied") {
    // The model, from settle-budget.md section 1: C_node is C_COM plus 15 pF
    // of stray -- 50 + 15 on the 4067, 25 + 15 on the 4051. The criterion is
    // 9.01 tau, which is ln(8192), half an LSB of 12 bit.
    const double kC[2]   = {65e-12, 40e-12};
    const double kLn8192 = 9.0109;

    for(int p = 0; p < shell::kSettlePairs; ++p) {
        const shell::SettlePair& sp = shell::kSettlePlan[p];
        CAPTURE(p);
        const double tau = static_cast<double>(sp.r_src_ohm) * kC[sp.group];
        const double ns  = tau * kLn8192 * 1e9;
        // One nanosecond of slack: the table is integers, the model is not.
        CHECK(std::abs(ns - static_cast<double>(sp.tau9_ns)) < 1.0);
    }
}

TEST_CASE("settle plan: the source impedances follow from the netlist") {
    // Switch on-resistance is 150 ohms in the model. The rail ties add
    // nothing (0 ohm links, netlist.py NEIGHBOURS); the dividers add their
    // two legs in parallel:
    //   REF_A  10k / 10k -> 5000
    //   REF_B   1k /  1k ->  500
    //   REF_C  10k / 10k -> 5000
    const uint32_t kRon = 150;
    CHECK(shell::kSettlePlan[0].r_src_ohm == kRon +    0);  // P0 rail tie
    CHECK(shell::kSettlePlan[1].r_src_ohm == kRon + 5000);  // P1 REF_A
    CHECK(shell::kSettlePlan[2].r_src_ohm == kRon + 5000);  // P2 REF_A
    CHECK(shell::kSettlePlan[3].r_src_ohm == kRon +  500);  // P3 REF_B
    CHECK(shell::kSettlePlan[4].r_src_ohm == kRon + 5000);  // P4 REF_C
    CHECK(shell::kSettlePlan[5].r_src_ohm == kRon +    0);  // P5 rail tie
}

TEST_CASE("settle plan: exactly two pairs are the instrument's own zero") {
    // Section 7: P0 and P5 step between channels tied to a rail through
    // 0 ohms, so their settle is the instrument and not the board. Anything
    // else marked reference would silently become a subtrahend.
    int refs = 0;
    for(int p = 0; p < shell::kSettlePairs; ++p)
        if(shell::kSettlePlan[p].is_reference) ++refs;
    CHECK(refs == 2);
    CHECK(shell::kSettlePlan[0].is_reference);
    CHECK(shell::kSettlePlan[5].is_reference);
    // One per mux, or one of the two muxes has no zero point of its own.
    CHECK(shell::kSettlePlan[0].group == 0);
    CHECK(shell::kSettlePlan[5].group == 1);
}

TEST_CASE("settle plan: the park is long enough to be called settled") {
    // The park IS the reference the curve is compared against, so it must be
    // far past the slowest prediction rather than merely past it.
    uint32_t slowest = 0;
    for(int p = 0; p < shell::kSettlePairs; ++p)
        if(shell::kSettlePlan[p].tau9_ns > slowest)
            slowest = shell::kSettlePlan[p].tau9_ns;
    CHECK(shell::kParkNs >= 5 * slowest);
    // And the grid must reach past the slowest prediction, or the run can
    // only ever report "did not settle" for that pair.
    CHECK(shell::grid_ns(shell::kGridPoints - 1) > 2 * slowest);
}
```

Add `#include <cmath>` at the top for `std::abs` on doubles.

- [ ] **Step 2: Run the test to verify it fails**

```bash
source env.sh; cmake --build build
```

Expected: FAIL at compile — `shell/settle_plan.h` does not exist. That is the correct first red.

- [ ] **Step 3: Write the header**

Create `shell/settle_plan.h`:

```cpp
#pragma once

// What the settle probe steps between, and how it decides. Data and pure
// arithmetic with no hardware type in it, for the same reason as mux_plan.h
// and coupon_expect.h: this is where a wrong constant is a visible line
// rather than a board that "looks slow" at the bench.
//
// Every number here is DERIVED -- from netlist.py for the channels and the
// resistors, from settle-budget.md's model for the predictions. The point of
// the run is to break or confirm them, so nothing in this file may be quoted
// as measured.
#include <cstdint>

namespace shell {

// One measured step: from a fully settled source channel to a target whose
// source impedance is known from the netlist.
struct SettlePair
{
    int      group;         // 0 = the 4067 on ADC_9, 1 = the 4051 on ADC_10
    int      from_ch;       // mux channel, parked and settled before the step
    int      to_ch;         // mux channel under test
    uint32_t r_src_ohm;     // switch Ron plus whatever the netlist wires
    uint32_t tau9_ns;       // 9.01 tau prediction, derived
    bool     is_reference;  // a 0 ohm step: the instrument's own zero (sec. 7)
};

inline constexpr int kSettlePairs = 6;
extern const SettlePair kSettlePlan[kSettlePairs];

// Delay grid: 0 to 6400 ns in 100 ns steps.
inline constexpr int kGridStepNs = 100;
inline constexpr int kGridPoints = 65;

constexpr uint32_t grid_ns(int i)
{
    return static_cast<uint32_t>(i) * static_cast<uint32_t>(kGridStepNs);
}

// Repeats per grid point. Not decoration: min and max across these are where
// jitter in the start-to-aperture latency shows up, and section 7a's G2 reads
// the spread of the settled point as the instrument's noise floor.
inline constexpr int kRepeats = 64;

// Half an LSB of 12 bit on the 16-bit scale: a 12-bit LSB is 16 counts.
inline constexpr int kSettleCounts = 8;

// How long the probe parks on the source channel before stepping. The park is
// the reference the whole curve is measured against, so it is not "past the
// prediction" but far past it -- 6.6x the slowest of them.
inline constexpr uint32_t kParkNs = 20000;

} // namespace shell
```

- [ ] **Step 4: Write the table**

Create `shell/settle_plan.cpp`. The channel numbers come from spec §6, which took them from `netlist.py`; the comment carries the part designators so a reader can check them without opening the spec.

```cpp
#include "settle_plan.h"

namespace shell {

// Section 6's table. Each target's source impedance is the switch on-
// resistance plus what the netlist wires to that channel, and the two
// reference channels bracket the pot range the panel will actually use --
// REF_A at 5.15k sits on the model's 20k-pot row, REF_B an order of
// magnitude below it.
const SettlePair kSettlePlan[kSettlePairs] = {
    // group from  to   R_src  9.01 tau  reference
    {0, 1, 10, 150, 88, true},      // P0  R_HI1 (A+3V3) -> R_SP10 (AGND)
    {0, 7, 8, 5150, 3016, false},   // P1  R_LO2 (AGND)  -> REF_A
    {0, 5, 8, 5150, 3016, false},   // P2  R_HI2 (A+3V3) -> REF_A
    {0, 7, 9, 650, 381, false},     // P3  R_LO2 (AGND)  -> REF_B
    {1, 7, 6, 5150, 1856, false},   // P4  R_LO4 (AGND)  -> REF_C
    {1, 5, 3, 150, 54, true},       // P5  R_HI4 (A+3V3) -> R_LO3 (AGND)
};

} // namespace shell
```

- [ ] **Step 5: Wire it into the host build**

In `CMakeLists.txt`, in the `spky_tests` source list, directly after the two `coupon_expect` lines (around line 95-96):

```cmake
    shell/settle_plan.cpp
    tests/test_settle_plan.cpp
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
source env.sh; cmake -S . -B build -DCMAKE_BUILD_TYPE=Release; cmake --build build; ctest --test-dir build --output-on-failure
```

Expected: 7/7 tests pass.

- [ ] **Step 7: Prove the RED once**

Change `kSettlePlan[3]`'s `tau9_ns` from `381` to `380`, rebuild, run. Expected: the "predictions are recomputed, not copied" case fails. Then **retype** `381` — do not `git checkout`. Rebuild, confirm green again. Report what you saw.

- [ ] **Step 8: Commit**

```bash
git add shell/settle_plan.h shell/settle_plan.cpp tests/test_settle_plan.cpp CMakeLists.txt
git commit -m "feat(shell): the settle probe's pair table and delay grid

Six channel pairs and a 65-point grid, as data with no hardware in it. The
test recomputes every 9.01 tau prediction from R and C rather than copying
the spec's column, so a typo in the table fails instead of matching a typo
in the test.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: The reduction and the four gates

**Files:**
- Modify: `shell/settle_plan.h`, `shell/settle_plan.cpp` — **extend, do not replace.** Task 1's table and grid stay exactly as they are.
- Modify: `tests/test_settle_plan.cpp` — **append test cases.** Do not delete or rewrite Task 1's cases; this plan has been bitten before by a task that replaced its predecessor's test file.

**Interfaces:**
- Consumes: everything Task 1 produced.
- Produces: `struct Point`, `struct RunSummary`, `struct Gates`, `d_settle_index()`, `settle_gates()`, `kFloorMaxCounts`, `kBandFactor`, `kKneeMaxNs`, `kJitterMaxNs`. Task 5 calls both functions and prints `Gates::ok()`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_settle_plan.cpp`:

```cpp
namespace {

// A curve that is flat at `settled` from `knee` onward and far away before
// it, with a fixed band on every point.
std::vector<shell::Point> make_curve(int knee, int32_t settled, int32_t band) {
    std::vector<shell::Point> pts;
    for(int i = 0; i < shell::kGridPoints; ++i) {
        const int32_t m = (i >= knee) ? settled : settled - 4000;
        pts.push_back(shell::Point{m, m - band / 2, m + band / 2});
    }
    return pts;
}

// A summary that passes every gate, for tests that break exactly one thing.
shell::RunSummary clean_summary() {
    shell::RunSummary s{};
    for(int p = 0; p < shell::kSettlePairs; ++p) s.knee_ns[p] = 100;
    s.b0          = 12;
    s.widest_band = 30;
    s.lat_min_ns  = 300;
    s.lat_max_ns  = 340;
    s.lat_mean_ns = 320;
    return s;
}

} // namespace

TEST_CASE("settle reduction: the knee is the first point that stays settled") {
    const std::vector<shell::Point> c = make_curve(7, 31742, 4);
    CHECK(shell::d_settle_index(c.data(), shell::kGridPoints, 31742) == 7);
}

TEST_CASE("settle reduction: a point that settles and leaves does not count") {
    // The whole reason the definition says "and stays within it": a curve
    // that crosses the band early and wanders back out has not settled, and
    // reporting the first crossing would report a ringing node as a fast one.
    std::vector<shell::Point> c = make_curve(7, 31742, 4);
    c[3] = shell::Point{31742, 31740, 31744};   // an early visit
    CHECK(shell::d_settle_index(c.data(), shell::kGridPoints, 31742) == 7);
}

TEST_CASE("settle reduction: a curve that never settles says so") {
    std::vector<shell::Point> c = make_curve(7, 31742, 4);
    c[shell::kGridPoints - 1].mean = 31742 - 4000;
    CHECK(shell::d_settle_index(c.data(), shell::kGridPoints, 31742) == -1);
}

TEST_CASE("settle reduction: the bracket is half an LSB of 12 bit") {
    std::vector<shell::Point> c = make_curve(0, 31742, 4);
    c[0].mean = 31742 + shell::kSettleCounts;
    CHECK(shell::d_settle_index(c.data(), shell::kGridPoints, 31742) == 0);
    c[0].mean = 31742 + shell::kSettleCounts + 1;
    CHECK(shell::d_settle_index(c.data(), shell::kGridPoints, 31742) == 1);
}

TEST_CASE("settle gates: a clean run passes all four") {
    const shell::Gates g = shell::settle_gates(clean_summary());
    CHECK(g.g1_knee);
    CHECK(g.g2_floor);
    CHECK(g.g3_band);
    CHECK(g.g4_jitter);
    CHECK(g.ok());
}

TEST_CASE("settle gates: G1 refuses a reference knee more than one step out") {
    shell::RunSummary s = clean_summary();
    s.knee_ns[0] = 200;                  // P0, a reference pair
    CHECK_FALSE(shell::settle_gates(s).g1_knee);
    s = clean_summary();
    s.knee_ns[5] = 200;                  // P5, the other one
    CHECK_FALSE(shell::settle_gates(s).g1_knee);
    // A slow knee on a pair under test is the measurement, not a fault.
    s             = clean_summary();
    s.knee_ns[1]  = 3100;
    CHECK(shell::settle_gates(s).g1_knee);
}

TEST_CASE("settle gates: G1 refuses a reference pair that never settled") {
    shell::RunSummary s = clean_summary();
    s.knee_ns[0] = -1;
    CHECK_FALSE(shell::settle_gates(s).g1_knee);
}

TEST_CASE("settle gates: G2 refuses a floor that swallows the criterion") {
    // The load-bearing gate. d_settle decides on the mean of 64 repeats,
    // whose spread is sigma/8, and 64 counts peak-to-peak across 64 samples
    // is about 5.1 sigma -- so the ceiling puts the mean's uncertainty at a
    // fifth of the 8-count threshold. Above it every curve still LOOKS like
    // a curve, which is exactly why this must fail loudly.
    shell::RunSummary s = clean_summary();
    s.b0          = shell::kFloorMaxCounts;
    s.widest_band = shell::kFloorMaxCounts;
    CHECK(shell::settle_gates(s).g2_floor);
    s.b0 = shell::kFloorMaxCounts + 1;
    CHECK_FALSE(shell::settle_gates(s).g2_floor);
}

TEST_CASE("settle gates: G3 measures the band against the measured floor") {
    shell::RunSummary s = clean_summary();
    s.b0          = 10;
    s.widest_band = 30;
    CHECK(shell::settle_gates(s).g3_band);
    s.widest_band = 31;
    CHECK_FALSE(shell::settle_gates(s).g3_band);
}

TEST_CASE("settle gates: G3 never demands better than the criterion itself") {
    // A band below the decision threshold cannot change a verdict, so an
    // exceptionally quiet run must not fail for being quiet. Without the
    // floor at kSettleCounts, b0 = 1 would demand every point inside
    // 3 counts and fail a perfectly good run.
    shell::RunSummary s = clean_summary();
    s.b0          = 1;
    s.widest_band = shell::kSettleCounts;
    CHECK(shell::settle_gates(s).g3_band);
}

TEST_CASE("settle gates: G4 refuses aperture jitter wider than a grid step") {
    shell::RunSummary s = clean_summary();
    s.lat_min_ns = 300;
    s.lat_max_ns = 400;
    CHECK(shell::settle_gates(s).g4_jitter);
    s.lat_max_ns = 401;
    CHECK_FALSE(shell::settle_gates(s).g4_jitter);
}

TEST_CASE("settle gates: G4 refuses a latency the conversion model forbids") {
    // A negative mean latency means start-to-EOC came back shorter than the
    // 25 ADC cycles the conversion is supposed to take. Then the 2034 ns
    // subtraction is wrong, and nothing downstream of it is trustworthy --
    // including the delays every other pair was measured at.
    shell::RunSummary s = clean_summary();
    s.lat_mean_ns = -1;
    CHECK_FALSE(shell::settle_gates(s).g4_jitter);
}
```

Add `#include <vector>` to the test file's includes.

- [ ] **Step 2: Run the tests to verify they fail**

```bash
source env.sh; cmake --build build
```

Expected: FAIL at compile — `shell::Point`, `shell::RunSummary`, `shell::Gates`, `d_settle_index` and `settle_gates` do not exist yet.

- [ ] **Step 3: Extend the header**

Append inside `namespace shell` in `shell/settle_plan.h`, after `kParkNs`:

```cpp
// One grid point's statistics across kRepeats conversions.
struct Point
{
    int32_t mean;
    int32_t min;
    int32_t max;
};

// The smallest grid index whose mean is within kSettleCounts of `settled` AND
// stays within it for every larger index, or -1 if there is none.
//
// "And stays within it" is the whole definition. A node that rings crosses the
// band early and wanders back out; reporting the first crossing would report
// that node as the fastest one on the board.
int d_settle_index(const Point* pts, int n, int32_t settled);

// Section 7a's gate constants. Each is derived; the spec carries the
// derivation and it is not repeated here, but none of them is a taste.
inline constexpr int32_t  kKneeMaxNs      = 100;  // G1, one grid step
inline constexpr int32_t  kFloorMaxCounts = 64;   // G2
inline constexpr int32_t  kBandFactor     = 3;    // G3
inline constexpr int32_t  kJitterMaxNs    = 100;  // G4, one grid step

// What a completed run reduces to before it is judged.
struct RunSummary
{
    int32_t knee_ns[kSettlePairs];  // -1 where the pair never settled
    int32_t b0;                     // spread of P0's settled reference read
    int32_t widest_band;            // widest max-min over every point of every pair
    int32_t lat_min_ns;
    int32_t lat_max_ns;
    int32_t lat_mean_ns;
};

struct Gates
{
    bool g1_knee;
    bool g2_floor;
    bool g3_band;
    bool g4_jitter;

    bool ok() const { return g1_knee && g2_floor && g3_band && g4_jitter; }
};

// A run that fails any gate prints its numbers and refuses to name a settle
// time. It does NOT report a board defect -- the distinction is the entire
// point of section 7.
Gates settle_gates(const RunSummary& s);
```

- [ ] **Step 4: Extend the implementation**

Append inside `namespace shell` in `shell/settle_plan.cpp`:

```cpp
int d_settle_index(const Point* pts, int n, int32_t settled)
{
    if(pts == nullptr || n <= 0) return -1;

    // Walk backwards. The definition is "settled from here on", so the answer
    // is the start of the final settled run -- which a forward scan can only
    // find by rescanning the tail at every candidate.
    int first = -1;
    for(int i = n - 1; i >= 0; --i)
    {
        const int32_t d = pts[i].mean - settled;
        const int32_t a = (d < 0) ? -d : d;
        if(a > kSettleCounts) break;
        first = i;
    }
    return first;
}

Gates settle_gates(const RunSummary& s)
{
    Gates g{};

    // G1: both reference pairs must collapse to the first grid step or the
    // next. They step between 0 ohm ties, so anything slower is the
    // instrument, and the section 7 subtraction would be a guess.
    g.g1_knee = true;
    for(int p = 0; p < kSettlePairs; ++p)
    {
        if(!kSettlePlan[p].is_reference) continue;
        if(s.knee_ns[p] < 0 || s.knee_ns[p] > kKneeMaxNs) g.g1_knee = false;
    }

    // G2: the measured noise floor must leave the mean's own uncertainty well
    // inside the decision threshold.
    g.g2_floor = s.b0 >= 0 && s.b0 <= kFloorMaxCounts;

    // G3: no point may be wider than three floors -- but never demand better
    // than the criterion itself, or a quiet run fails for being quiet.
    const int32_t allowed = (kBandFactor * s.b0 > kSettleCounts)
                                ? kBandFactor * s.b0
                                : static_cast<int32_t>(kSettleCounts);
    g.g3_band = s.widest_band >= 0 && s.widest_band <= allowed;

    // G4: aperture jitter inside one grid step, and a latency the conversion
    // model does not forbid. A negative mean means the 2034 ns subtraction is
    // wrong, which invalidates every delay in the run.
    g.g4_jitter = s.lat_mean_ns >= 0 && s.lat_max_ns >= s.lat_min_ns
                  && (s.lat_max_ns - s.lat_min_ns) <= kJitterMaxNs;

    return g;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
source env.sh; cmake --build build; ctest --test-dir build --output-on-failure
```

Expected: 7/7 pass, including every case from Task 1 — confirm they are still present and still running.

- [ ] **Step 6: Prove the RED on G2, deliberately**

The spec names G2 as the one to red-prove, because it is the gate whose absence leaves every curve still looking like a curve. In `settle_gates()`, replace

```cpp
    g.g2_floor = s.b0 >= 0 && s.b0 <= kFloorMaxCounts;
```

with

```cpp
    g.g2_floor = s.b0 >= 0;
```

Rebuild and run only that case:

```bash
source env.sh; cmake --build build; ./build/spky_tests.exe -tc="settle gates: G2 refuses a floor*"
```

Expected: FAILURE, on the `CHECK_FALSE` with `b0 = kFloorMaxCounts + 1`. **Retype** the original line — do not `git checkout`. Rebuild, run the full suite, confirm 7/7. Report what you saw.

- [ ] **Step 7: Commit**

```bash
git add shell/settle_plan.h shell/settle_plan.cpp tests/test_settle_plan.cpp
git commit -m "feat(shell): the settle reduction and the four honesty gates

d_settle_index() walks backwards, because 'settled from here on' is the
definition and a forward scan finds it only by rescanning the tail. A node
that rings crosses the band early and comes back out; the first crossing
would report it as the fastest channel on the board.

The gates are spec section 7a. G2 was red-proven by hand: without its
ceiling a run whose noise floor swallows the 8-count criterion passes, and
every curve in it still looks like a curve.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The build switch, the cycle counter, and an image that says nothing

This task ships a firmware that prints an empty, well-formed block. That is deliberate: it separates "the switch, the link and the USB path work" from "the instrument works", and those two fail for entirely different reasons.

**Files:**
- Create: `shell/cycles.h`, `shell/write_shell_settle_probe.py`
- Create: `shell/settle_probe.h`, `shell/settle_probe.cpp`
- Modify: `shell/Makefile`, `shell/main.cpp`

**Interfaces:**
- Consumes: `shell/settle_plan.h` (Task 1), `bench::Board` from `src/hw/board.h`.
- Produces: `void shell::run_settle_probe(bench::Board& hw)` — never returns. `shell::cycles_init()`, `shell::cycles_now()`.

- [ ] **Step 1: Copy the cycle counter**

Create `shell/cycles.h`. `shell/README.md` says exactly one file is shared with `bench/` and that this is deliberate, so this is a copy, not an include across trees:

```cpp
#pragma once

// The Cortex-M7 DWT cycle counter. Free-running at the core clock (480 MHz),
// so one tick is 2.08 ns and a delay grid in nanoseconds is a plain integer
// compare.
//
// COPIED from bench/cycles.h rather than included across trees:
// shell/README.md is explicit that exactly one file is shared with bench/
// (src/hw/board.h) and that the separation is deliberate. The origin is named
// here so the trap below is not re-learned from scratch.
//
// THE TRAP: the M7 gates the DWT registers behind a lock and LAR must be
// unlocked before CYCCNT counts at all. The M3/M4 examples on the web omit
// this, and without it the counter reads zero forever -- which looks like an
// instrument that is infinitely fast.
#include <cstdint>
#include <daisy_seed.h>   // pulls in the CMSIS core headers for CoreDebug/DWT

namespace shell {

inline void cycles_init()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR   = 0xC5ACCE55;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

inline uint32_t cycles_now()
{
    return DWT->CYCCNT;
}

// 480 MHz core clock. Integer arithmetic on purpose: this runs inside the
// timed path and a float divide there would be measuring the measurement.
inline constexpr uint32_t kCoreMhz = 480;

constexpr uint32_t ns_to_cycles(uint32_t ns)
{
    return ns * kCoreMhz / 1000u;
}

constexpr uint32_t cycles_to_ns(uint32_t cyc)
{
    return cyc * 1000u / kCoreMhz;
}

} // namespace shell
```

- [ ] **Step 2: Copy the switch generator**

Create `shell/write_shell_settle_probe.py` as a copy of `shell/write_shell_coupon_probe.py` with every `COUPON` replaced by `SETTLE`, keeping the English docstring and keeping the stale-object deletion verbatim. The docstring's second paragraph should read:

```
Like write_shell_coupon_probe.py this ALWAYS defines the symbol, including
in position 0, because `#if SHELL_SETTLE_PROBE` has to work in both.
```

Do not shorten the timestamp paragraph. It documents a real failure from 2026-08-23 and is the reason the script deletes objects itself.

- [ ] **Step 3: Add the Makefile switch**

In `shell/Makefile`, after the `SHELL_COUPON_PROBE` validation block (around line 133), add — **in English**, and do not translate the German blocks above it:

```make
# Position 1 replaces the instrument with the settle probe: the coupon's mux
# channels are stepped one pair at a time and timed through the ADC. It needs
# the coupon chain profile, so it implies SHELL_COUPON_PROBE's board -- but
# not its scan, which is a different measurement.
# Spec:  ../docs/superpowers/specs/2026-09-17-coupon-settle-probe-design.md
# Plan:  ../docs/superpowers/plans/2026-09-17-coupon-settle-probe.md
SHELL_SETTLE_PROBE ?= 0

ifneq ($(filter $(SHELL_SETTLE_PROBE),0 1),$(SHELL_SETTLE_PROBE))
$(error SHELL_SETTLE_PROBE must be 0 or 1)
endif

ifeq ($(SHELL_SETTLE_PROBE),1)
ifneq ($(SHELL_COUPON_PROBE),1)
$(error SHELL_SETTLE_PROBE=1 needs SHELL_COUPON_PROBE=1: the probe drives the \
coupon chain profile, and the panel profile has none of its channels)
endif
endif
```

In `CPP_SOURCES`, after `coupon_scan.cpp`:

```make
	settle_plan.cpp \
	settle_probe.cpp \
```

In `SWITCH_OBJECTS` (line 191), add the new object so a switch flip cannot leave a stale one behind:

```make
SWITCH_OBJECTS = $(BUILD_DIR)/main.o $(BUILD_DIR)/mux_scan.o $(BUILD_DIR)/coupon_scan.o $(BUILD_DIR)/settle_probe.o
```

In `SWITCH_HEADERS` (after line 213), add a continuation line:

```make
  $(shell python write_shell_settle_probe.py $(BUILD_DIR)/shell_settle_probe.h $(SHELL_SETTLE_PROBE) $(SWITCH_OBJECTS))
```

Remember the trailing backslash on the line that precedes it.

- [ ] **Step 4: Write the skeleton probe**

Create `shell/settle_probe.h`:

```cpp
#pragma once

// The settle probe. Takes ADC1 away from libDaisy and drives it single-shot
// so that the aperture is a thing the firmware decides rather than a thing
// the DMA happens to do. Spec:
// ../docs/superpowers/specs/2026-09-17-coupon-settle-probe-design.md
#include "hw/board.h"

namespace shell {

// Never returns. Prints one block per pass on USB-CDC, forever.
void run_settle_probe(bench::Board& hw);

} // namespace shell
```

Create `shell/settle_probe.cpp` — for this task, the block is empty but complete:

```cpp
#include "settle_probe.h"

#include "cycles.h"
#include "settle_plan.h"

namespace shell {

void run_settle_probe(bench::Board& hw)
{
    cycles_init();
    hw.StartLog(false);

    while(1)
    {
        // The configuration line comes FIRST and the end marker always
        // arrives, even on a pass that measured nothing. A reader that can
        // only recognise a complete block is the point: a truncated one must
        // be discarded rather than half-believed.
        hw.PrintLine("SHELL_SETTLE_CFG sample_cycles=165 adc_khz=12290 "
                     "repeats=%d grid_step_ns=%d grid_points=%d park_ns=%d",
                     kRepeats, kGridStepNs, kGridPoints,
                     static_cast<int>(kParkNs));
        hw.PrintLine("SHELL_SETTLE_END");
        hw.Delay(1000);
    }
}

} // namespace shell
```

`sample_cycles=165` is `ADC_SAMPLETIME_16CYCLES_5` expressed in tenths, so it stays an integer for `%d` — `PrintLine()` is the lightweight printf and the existing probes stay on `%d` for that reason. Say so in a comment.

- [ ] **Step 5: Dispatch from main**

In `shell/main.cpp`, beside the existing `#include "shell_coupon_probe.h"` (line 14):

```cpp
#include "shell_settle_probe.h"
```

Beside the existing coupon include block (line 34):

```cpp
#if SHELL_SETTLE_PROBE
#include "settle_probe.h"
#endif
```

Extend the USB identity guard (line 43) so the settle image links — the strings are needed by any image that opens USB-CDC:

```cpp
#if defined(SHELL_CPU_PROBE) || SHELL_COUPON_PROBE || SHELL_SETTLE_PROBE
```

And in `main()`, **before** the `SHELL_COUPON_PROBE` block (line 180), because the settle probe is the more specific of the two and both never return:

```cpp
#if SHELL_SETTLE_PROBE
    // The board under test is the coupon, and the question is time, not
    // wiring. No engine, no audio: this image exists to say how long a mux
    // channel takes to settle to within half an LSB.
    shell::run_settle_probe(hw);   // never returns
#endif
```

- [ ] **Step 6: Build both switch positions and prove they differ**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_SETTLE_PROBE=0
cp shell/build/shell-sram.bin /tmp/settle0.bin
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_SETTLE_PROBE=1
cmp shell/build/shell-sram.bin /tmp/settle0.bin
```

Expected: both builds succeed, and `cmp` reports the files **differ**. Identical images mean the switch did not reach the objects — that exact failure has happened on this machine and is why the generator deletes objects itself. Do not proceed past an identical `cmp`.

Also confirm the guard fires:

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell images SHELL_SETTLE_PROBE=1 SHELL_COUPON_PROBE=0
```

Expected: the `$(error ...)` message, no build.

- [ ] **Step 7: Flash and confirm the block arrives**

Ask the operator to put the board into DFU (hold BOOT, tap RESET, release BOOT), then:

```bash
dfu-util -a 0 -s 0x90040000:leave -D shell/build/shell-sram.bin
```

Find the port and read a few lines:

```bash
python -c "from serial.tools import list_ports; [print(p.device) for p in list_ports.comports()]"
```

Expected: `SHELL_SETTLE_CFG ...` and `SHELL_SETTLE_END` alternating once per second. Nothing else yet.

- [ ] **Step 8: Commit**

```bash
git add shell/cycles.h shell/write_shell_settle_probe.py shell/settle_probe.h shell/settle_probe.cpp shell/Makefile shell/main.cpp
git commit -m "feat(shell): the settle probe's build switch and an empty block

An image that prints a well-formed block with nothing in it, on purpose:
'the switch, the link and the USB path work' and 'the instrument works'
fail for different reasons and are worth separating.

SHELL_SETTLE_PROBE=1 without SHELL_COUPON_PROBE=1 is a make error rather
than a silent image built against the panel profile, whose channels the
pair table does not have. The two switch positions were cmp'd, not assumed.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: The instrument, and its own latency

**Files:**
- Modify: `shell/settle_probe.cpp`
- Modify: `shell/mux_scan.h`, `shell/mux_scan.cpp`

**Interfaces:**
- Consumes: Task 3's `run_settle_probe`, `cycles_now()`, `ns_to_cycles()`.
- Produces: `uint32_t MuxScan::write_chain_timed(uint32_t word)`; and inside `settle_probe.cpp`, a file-local `adc_init()`, `adc_select(int sense)` and `sample_now(uint32_t* span_cycles)`.

- [ ] **Step 1: Determine the ADC1 channel numbers — do not guess**

The sense pins are `ADC_9` and `ADC_10`, which are module pins A2 and A3 (`netlist.py:98-99`, `daisy_patch_sm.cpp:18`). The **STM32 ADC1 channel number** behind each is not stated in the spec and must not be invented.

Find it in libDaisy: `lib/libDaisy/src/per/adc.cpp` carries the pin-to-channel mapping that `AdcChannelConfig` uses, and `lib/libDaisy/src/daisy_patch_sm.cpp` says which physical pin `ADC_9` and `ADC_10` are. Record both numbers in a comment with the file they came from.

Then **prove it on hardware at step 7**, not here. A wrong channel number produces a plausible-looking number, not an error — this is the same shape as the frozen probe, and reading a different pin than you think you are reading is not something the compiler can catch.

- [ ] **Step 2: Add the timed latch to MuxScan**

In `shell/mux_scan.h`, beside `write_chain()`:

```cpp
    // Like write_chain(), but returns the DWT cycle count taken immediately
    // after the 595s' RCLK rising edge.
    //
    // t = 0 IS THAT EDGE, not the start of the bit-bang. write_chain() clocks
    // 16 bits before the address reaches the mux at all, so timing from the
    // call would fold the bit-bang into every settle time and make the fast
    // channels look slow by a constant nobody measured.
    uint32_t write_chain_timed(uint32_t word);
```

`shell/mux_scan.h` must now `#include "cycles.h"`.

In `shell/mux_scan.cpp`, implement it beside `write_chain()`, duplicating the shift loop and taking the timestamp between the two latch writes:

```cpp
uint32_t MuxScan::write_chain_timed(uint32_t word)
{
    for(int i = kActiveChain.chain_bits - 1; i >= 0; --i)
    {
        data_.Write(((word >> i) & 1u) != 0u);
        clock_.Write(true);
        clock_.Write(false);
    }
    latch_.Write(true);
    const uint32_t t0 = cycles_now();   // the edge the address becomes visible on
    latch_.Write(false);
    return t0;
}
```

Match `write_chain()`'s existing shift order exactly — read it first and copy it rather than reconstructing it from this plan.

- [ ] **Step 3: Take ADC1 and configure it**

In `shell/settle_probe.cpp`, in an anonymous namespace. The sequence is spec §4 and the order matters:

```cpp
// libDaisy still owns the PINS: hw.Init() has already configured A2/A3 as
// analog inputs and brought up the ADC clock tree through MspInit, and none
// of that is duplicated here. What is taken over is ADC1 itself.
//
// The public API was unusable for exactly one reason: AdcHandle::Start()
// recalibrates, so a measurement loop that started and stopped the ADC would
// recalibrate inside its own timed path. Calibration happens once, here.
ADC_HandleTypeDef g_adc{};

void adc_init(bench::Board& hw)
{
    hw.StopAdc();                    // public on DaisyPatchSM; ADC1 is now free

    g_adc.Instance                      = ADC1;
    g_adc.Init.ClockPrescaler           = ADC_CLOCK_ASYNC_DIV2;   // 12.29 MHz
    g_adc.Init.Resolution               = ADC_RESOLUTION_16B;
    g_adc.Init.ScanConvMode             = ADC_SCAN_DISABLE;
    g_adc.Init.EOCSelection             = ADC_EOC_SINGLE_CONV;
    g_adc.Init.LowPowerAutoWait         = DISABLE;
    g_adc.Init.ContinuousConvMode       = DISABLE;
    g_adc.Init.NbrOfConversion          = 1;
    g_adc.Init.DiscontinuousConvMode    = DISABLE;
    g_adc.Init.ExternalTrigConv         = ADC_SOFTWARE_START;
    g_adc.Init.ExternalTrigConvEdge     = ADC_EXTERNALTRIGCONVEDGE_NONE;
    g_adc.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;  // no DMA
    g_adc.Init.Overrun                  = ADC_OVR_DATA_OVERWRITTEN;
    g_adc.Init.OversamplingMode         = DISABLE;                // OVS_NONE

    HAL_ADC_Init(&g_adc);
    HAL_ADCEx_Calibration_Start(&g_adc, ADC_CALIB_OFFSET,
                                ADC_SINGLE_ENDED);   // ONCE, never in the loop
}

// Runs when the sense pin changes -- once per channel pair, never inside the
// timed path.
void adc_select(uint32_t channel)
{
    ADC_ChannelConfTypeDef cfg{};
    cfg.Channel      = channel;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_16CYCLES_5;   // what section 6 predicts against
    cfg.SingleDiff   = ADC_SINGLE_ENDED;
    cfg.OffsetNumber = ADC_OFFSET_NONE;
    cfg.Offset       = 0;
    HAL_ADC_ConfigChannel(&g_adc, &cfg);
}
```

If the HAL field names differ in this libDaisy version, follow the header, not this plan — and say so in the report.

- [ ] **Step 4: The measurement primitive**

```cpp
// One conversion. Returns the raw 16-bit value and writes the start-to-EOC
// span in DWT cycles.
//
// Polling happens AFTER the aperture opens, so it costs wall clock and
// nothing else. The span is what makes section 7a's G4 possible: it brackets
// the start-to-aperture latency plus the conversion, and the conversion is
// known.
uint16_t sample_now(uint32_t* span_cycles)
{
    const uint32_t t0 = cycles_now();
    HAL_ADC_Start(&g_adc);
    while(__HAL_ADC_GET_FLAG(&g_adc, ADC_FLAG_EOC) == 0u) { }
    const uint32_t t1 = cycles_now();
    const uint16_t v  = static_cast<uint16_t>(HAL_ADC_GetValue(&g_adc));
    HAL_ADC_Stop(&g_adc);
    if(span_cycles) *span_cycles = t1 - t0;
    return v;
}

// ADC_SAMPLETIME_16CYCLES_5 plus 8.5 cycles of 16-bit conversion is 25 ADC
// cycles, and at 12.29 MHz that is 2034 ns. Subtracting it from the measured
// span leaves the start-to-aperture latency. DERIVED, not measured -- if it
// is wrong the latency comes out negative, which G4 refuses.
constexpr int32_t kConversionNs = 2034;
```

- [ ] **Step 5: Measure the latency and print it**

Replace the skeleton's body so that each pass parks on P0's source channel, takes `kRepeats` conversions there, and reduces the spans:

```cpp
    // The instrument measuring itself. Parked and fully settled, so the only
    // thing that varies between these conversions is the instrument.
    MuxScan chain;
    chain.init();
    chain.write_chain(chain_word(kCouponChain,
                                 step_pattern(kCouponChain, kSettlePlan[0].from_ch),
                                 0u));
    hw.Delay(1);

    int32_t lat_min = 0x7FFFFFFF, lat_max = -0x7FFFFFFF;
    int64_t lat_sum = 0;
    int32_t val_min = 0x7FFFFFFF, val_max = -0x7FFFFFFF;
    for(int i = 0; i < kRepeats; ++i)
    {
        uint32_t span = 0;
        const int32_t v = sample_now(&span);
        const int32_t l = static_cast<int32_t>(cycles_to_ns(span)) - kConversionNs;
        if(l < lat_min) lat_min = l;
        if(l > lat_max) lat_max = l;
        lat_sum += l;
        if(v < val_min) val_min = v;
        if(v > val_max) val_max = v;
    }
    const int32_t lat_mean = static_cast<int32_t>(lat_sum / kRepeats);
    const int32_t b0       = val_max - val_min;   // G2's measured noise floor
```

and print, after the existing `SHELL_SETTLE_CFG` line:

```cpp
        hw.PrintLine("SHELL_SETTLE_CAL lat_mean_ns=%d lat_min_ns=%d "
                     "lat_max_ns=%d b0=%d gates_ok=%d",
                     lat_mean, lat_min, lat_max, b0, 0);
```

`gates_ok=0` for now — Task 5 computes it. Leave a comment saying so, so nobody reads this pass as a verdict.

The step pattern must select the pair's group: use `step_pattern(kCouponChain, s)` with the scan step `s` that corresponds to `(group, channel)` — for group 0 that is the channel itself, for group 1 it is `kCouponChain.channels[0] + channel`. Write a one-line helper `step_of(int group, int ch)` and use it everywhere rather than repeating the arithmetic.

- [ ] **Step 6: Build**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_SETTLE_PROBE=1
```

Expected: clean build.

- [ ] **Step 7: Flash, and verify the channel number from step 1**

Flash as in Task 3. Read a few blocks.

Two things must be true, and **both are hardware claims that need the board**, not reasoning:

1. `b0` is small — single digits to low tens of counts. If `b0` is in the thousands, the ADC is not reading a settled rail-tied channel and the channel number from step 1 is the first suspect.
2. `lat_mean_ns` is positive and `lat_max_ns - lat_min_ns` is small. A negative mean means `kConversionNs` is wrong for this configuration; report the raw span instead of adjusting the constant to make the number look right.

To confirm the channel number independently, temporarily park on `kSettlePlan[0].to_ch` (channel 10, tied to AGND) instead of `from_ch` (channel 1, tied to A+3V3) and check the value moves from near the rail to near zero. Record both numbers in the report. Then restore.

Report `lat_mean_ns`, `lat_min_ns`, `lat_max_ns` and `b0` as **measured** — these are the first measured numbers in the whole spec.

- [ ] **Step 8: Commit**

```bash
git add shell/settle_probe.cpp shell/mux_scan.h shell/mux_scan.cpp
git commit -m "feat(shell): the settle probe owns ADC1, and measures its own latency

Calibration runs once, outside the timed path -- that it does not is the
only reason libDaisy's ADC could not serve this probe at all.

write_chain_timed() exists because t=0 is the 595s' RCLK rising edge and
not the start of the bit-bang: timing from the call would fold 16 clocked
bits into every settle time and make the fast channels slow by a constant
nobody measured.

The start-to-EOC span brackets the start-to-aperture latency plus a known
25 ADC cycles, so the latency stops being spec section 10's unmeasured row
for the price of one extra timestamp in an already-timed path.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: The sweep, the verdict, and the full block

**Files:**
- Modify: `shell/settle_probe.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1, 2 and 4.
- Produces: the complete `SHELL_SETTLE` output block of spec §8.

- [ ] **Step 1: Write the sweep**

For each pair, for each grid point, for each repeat: park on `from_ch` for `kParkNs`, latch to `to_ch` taking `t0`, spin until `d` has elapsed, convert. Accumulate mean, min and max.

```cpp
// One grid point.
Point measure_point(bench::Board& hw, MuxScan& chain, const SettlePair& sp,
                    uint32_t d_ns)
{
    const uint32_t park_word
        = chain_word(kCouponChain,
                     step_pattern(kCouponChain, step_of(sp.group, sp.from_ch)), 0u);
    const uint32_t test_word
        = chain_word(kCouponChain,
                     step_pattern(kCouponChain, step_of(sp.group, sp.to_ch)), 0u);
    const uint32_t d_cycles = ns_to_cycles(d_ns);
    const uint32_t park_cycles = ns_to_cycles(kParkNs);

    int64_t sum = 0;
    int32_t lo = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    for(int r = 0; r < kRepeats; ++r)
    {
        chain.write_chain(park_word);
        const uint32_t p0 = cycles_now();
        while(cycles_now() - p0 < park_cycles) { }

        const uint32_t t0 = chain.write_chain_timed(test_word);
        while(cycles_now() - t0 < d_cycles) { }

        uint32_t span = 0;
        const int32_t v = sample_now(&span);
        sum += v;
        if(v < lo) lo = v;
        if(v > hi) hi = v;
    }
    return Point{static_cast<int32_t>(sum / kRepeats), lo, hi};
}
```

Note `cycles_now() - t0` on `uint32_t`: the subtraction wraps correctly and needs no special case, which is exactly why the counter is read as unsigned. Say so in a comment — someone will otherwise "fix" it.

- [ ] **Step 2: Reduce and judge**

Per pair: take the settled reference by parking on `to_ch` for `kParkNs` and converting (**the target is measured, not assumed** — §6), then `d_settle_index()` against it, then fill `RunSummary` and call `settle_gates()`.

```cpp
    RunSummary summary{};
    summary.b0          = b0;          // from Task 4's latency pass
    summary.lat_min_ns  = lat_min;
    summary.lat_max_ns  = lat_max;
    summary.lat_mean_ns = lat_mean;
    summary.widest_band = 0;

    for(int p = 0; p < kSettlePairs; ++p)
    {
        const SettlePair& sp = kSettlePlan[p];
        adc_select(channel_of_group(sp.group));

        // The settled reference for THIS pair. Reading it rather than
        // assuming the rail or the divider's nominal value is what makes the
        // curve a comparison instead of a prediction.
        chain.write_chain(chain_word(
            kCouponChain, step_pattern(kCouponChain, step_of(sp.group, sp.to_ch)), 0u));
        const uint32_t s0 = cycles_now();
        while(cycles_now() - s0 < ns_to_cycles(kParkNs)) { }
        uint32_t ignored = 0;
        const int32_t settled = sample_now(&ignored);

        Point pts[kGridPoints];
        for(int i = 0; i < kGridPoints; ++i)
        {
            pts[i] = measure_point(hw, chain, sp, grid_ns(i));
            const int32_t band = pts[i].max - pts[i].min;
            if(band > summary.widest_band) summary.widest_band = band;
            hw.PrintLine("SHELL_SETTLE pair=%d sense=%d from=%d to=%d d_ns=%d "
                         "n=%d mean=%d min=%d max=%d",
                         p, sp.group, sp.from_ch, sp.to_ch,
                         static_cast<int>(grid_ns(i)), kRepeats,
                         pts[i].mean, pts[i].min, pts[i].max);
        }

        const int idx = d_settle_index(pts, kGridPoints, settled);
        summary.knee_ns[p] = (idx < 0) ? -1 : static_cast<int32_t>(grid_ns(idx));
    }

    const Gates gates = settle_gates(summary);
```

`channel_of_group()` is a one-line helper returning the ADC1 channel number recorded in Task 4 step 1, one per group. Do not inline the constants at two call sites.

- [ ] **Step 3: Print the whole block**

Order matters for the reader: config, then the per-point lines, then the calibration and verdict, then the end marker.

```cpp
        hw.PrintLine("SHELL_SETTLE_CAL lat_mean_ns=%d lat_min_ns=%d "
                     "lat_max_ns=%d b0=%d gates_ok=%d",
                     summary.lat_mean_ns, summary.lat_min_ns,
                     summary.lat_max_ns, summary.b0, gates.ok() ? 1 : 0);
        for(int p = 0; p < kSettlePairs; ++p)
            hw.PrintLine("SHELL_SETTLE_KNEE pair=%d d_settle_ns=%d "
                         "predicted_ns=%d reference=%d",
                         p, summary.knee_ns[p],
                         static_cast<int>(kSettlePlan[p].tau9_ns),
                         kSettlePlan[p].is_reference ? 1 : 0);
        hw.PrintLine("SHELL_SETTLE_GATES g1=%d g2=%d g3=%d g4=%d",
                     gates.g1_knee ? 1 : 0, gates.g2_floor ? 1 : 0,
                     gates.g3_band ? 1 : 0, gates.g4_jitter ? 1 : 0);
        hw.PrintLine("SHELL_SETTLE_END");
```

The probe prints the knees **whether or not the gates passed**, and prints the gates beside them. It does not suppress numbers; it labels them. A reader that sees `gates_ok=0` must not quote the knees, and `read_settle.py` enforces that at Task 6.

- [ ] **Step 4: Build and flash**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_SETTLE_PROBE=1
```

Flash as before, read one full block to a file.

- [ ] **Step 5: Read the result honestly**

Report, in this order:

1. `gates_ok`. **If it is 0, stop.** Report which gate failed and its numbers, and do not quote a single settle time. That is the spec working, not the task failing.
2. If `gates_ok=1`: the six `d_settle_ns` values beside their `predicted_ns`, and say plainly for each whether the model held. The predictions are derived from a model three estimates deep; a pair that misses by a factor of two is a finding, not a bug in this task.
3. `b0`, the latency numbers, and the widest band.

Do not adjust any constant to make a gate pass. If a gate fails, that is the deliverable.

- [ ] **Step 6: Commit**

```bash
git add shell/settle_probe.cpp
git commit -m "feat(shell): the settle sweep, and a run that can refuse to answer

Six pairs across 65 grid points at 64 repeats, timed from the 595 latch
edge. Each pair's settled reference is READ at the end of a long park
rather than assumed from the divider's nominal value, so the curve is a
comparison and not a prediction checked against itself.

The block prints the knees whether or not the gates passed, beside the
gates. It does not suppress numbers, it labels them: a reader that sees
gates_ok=0 may not quote a settle time, and read_settle.py enforces that.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: The reader and its guard

**Files:**
- Create: `shell/read_settle.py`, `shell/test_read_settle.py`
- Modify: `CMakeLists.txt` (beside the `read_coupon_guard` block, around line 316)

**Interfaces:**
- Consumes: the block format from Task 5.
- Produces: `parse_block(lines)`, `format_csv(block)` — pure functions the guard exercises without pyserial and without a board.

- [ ] **Step 1: Write the failing guard**

Create `shell/test_read_settle.py`. It follows `shell/test_read_coupon.py`: a plain script, exit code is the verdict, no pytest (not installed here).

```python
"""Guard for read_settle.py's parser. Runs as a plain script; pytest is not
installed on this machine and the tools/ guards that had no runner stood red
for 23 days behind exactly that gap.
"""
import sys

from read_settle import parse_block, format_csv

SAMPLE = [
    "SHELL_SETTLE_CFG sample_cycles=165 adc_khz=12290 repeats=64 "
    "grid_step_ns=100 grid_points=2 park_ns=20000",
    "SHELL_SETTLE pair=0 sense=0 from=1 to=10 d_ns=0 n=64 mean=100 min=98 max=102",
    "SHELL_SETTLE pair=0 sense=0 from=1 to=10 d_ns=100 n=64 mean=31742 "
    "min=31740 max=31744",
    "SHELL_SETTLE_CAL lat_mean_ns=320 lat_min_ns=300 lat_max_ns=340 b0=12 "
    "gates_ok=1",
    "SHELL_SETTLE_KNEE pair=0 d_settle_ns=100 predicted_ns=88 reference=1",
    "SHELL_SETTLE_GATES g1=1 g2=1 g3=1 g4=1",
    "SHELL_SETTLE_END",
]

failures = []


def check(label, cond):
    if not cond:
        failures.append(label)


block = parse_block(SAMPLE)
check("a complete block parses", block is not None)
check("every point is kept", block and len(block["points"]) == 2)
check("the verdict is carried", block and block["cal"]["gates_ok"] == 1)
check("the knees are carried", block and len(block["knees"]) == 1)

# A block cut off before its end marker must NOT come back. This is the whole
# reason the reader accumulates instead of returning on the first match: a
# partial block reports a clean instrument because the failing tail never
# arrived.
check("a truncated block is refused", parse_block(SAMPLE[:-1]) is None)

# And one cut in the middle, which is what a serial read timeout actually
# produces -- the end marker arrives, a row does not.
short = SAMPLE[:2] + SAMPLE[3:]
check("a block missing a row is refused", parse_block(short) is None)

# A run whose gates failed still parses -- refusing to parse it would hide
# the very numbers that say why.
failed = list(SAMPLE)
failed[3] = failed[3].replace("gates_ok=1", "gates_ok=0")
bad = parse_block(failed)
check("a failed run still parses", bad is not None)
check("a failed run is marked", bad and bad["cal"]["gates_ok"] == 0)

csv = format_csv(block)
check("the CSV has a header and one row per point", len(csv.strip().split("\n")) == 3)

if failures:
    for f in failures:
        print("FAIL: %s" % f, file=sys.stderr)
    raise SystemExit(1)
print("read_settle guard: ok")
```

- [ ] **Step 2: Run it to verify it fails**

```bash
python shell/test_read_settle.py
```

Expected: `ModuleNotFoundError: No module named 'read_settle'`. Run it from `shell/` — the guard imports its neighbour, which is why the `add_test` below sets `WORKING_DIRECTORY`.

- [ ] **Step 3: Write the reader**

Create `shell/read_settle.py`, following `read_coupon.py` exactly in shape: `import serial` **inside** `main()` so the guard can import the module without pyserial, and accumulate until the end marker rather than returning on the first match.

```python
"""Reads one SHELL_SETTLE_CFG..SHELL_SETTLE_END block from the board's
USB-CDC port and writes the grid points as CSV.

The firmware repeats the block forever with a delay and there is no
handshake, so this listens until a whole block has arrived. It may NOT
return early: a partial block would report a clean instrument because the
failing tail never came.

Find the port first:

    python -c "from serial.tools import list_ports; \
print([p.device for p in list_ports.comports()])"

Call:
    python read_settle.py COM4 [out.csv] [timeout_seconds]

Exit code is the verdict: 1 when the run's gates did not pass, because a
run that failed a gate may not have its settle times quoted.
"""
import sys
import time

FIELDS = ("pair", "sense", "from", "to", "d_ns", "n", "mean", "min", "max")


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
        try:
            if line.startswith("SHELL_SETTLE_CFG"):
                block = {"cfg": _fields(line, "SHELL_SETTLE_CFG"),
                         "points": [], "knees": [], "cal": None, "gates": None}
            elif block is None:
                continue
            elif line.startswith("SHELL_SETTLE_CAL"):
                block["cal"] = _fields(line, "SHELL_SETTLE_CAL")
            elif line.startswith("SHELL_SETTLE_KNEE"):
                block["knees"].append(_fields(line, "SHELL_SETTLE_KNEE"))
            elif line.startswith("SHELL_SETTLE_GATES"):
                block["gates"] = _fields(line, "SHELL_SETTLE_GATES")
            elif line.startswith("SHELL_SETTLE_END"):
                want = block["cfg"]["grid_points"] * (max(
                    (k["pair"] for k in block["knees"]), default=-1) + 1)
                if (block["cal"] is None or block["gates"] is None
                        or not block["knees"]
                        or len(block["points"]) != want):
                    block = None
                    continue
                return block
            elif line.startswith("SHELL_SETTLE "):
                block["points"].append(_fields(line, "SHELL_SETTLE "))
        except ValueError:
            # A line the serial read timeout cut in half. Drop the block and
            # keep listening rather than crash on the board's next breath; a
            # later SHELL_SETTLE_CFG still gets its own chance.
            block = None
    return None


def format_csv(block):
    rows = [",".join(FIELDS)]
    for row in block["points"]:
        rows.append(",".join(str(row[f]) for f in FIELDS))
    return "\n".join(rows) + "\n"


def main() -> int:
    # Imported here, not at module scope: parse_block()/format_csv() are the
    # pure parser the guard exercises, and that guard must not need pyserial
    # installed to import this module.
    import serial

    if len(sys.argv) not in (2, 3, 4):
        raise SystemExit("usage: read_settle.py PORT [out.csv] [timeout_s]")
    port  = sys.argv[1]
    out   = sys.argv[2] if len(sys.argv) > 2 else None
    limit = float(sys.argv[3]) if len(sys.argv) > 3 else 60.0

    lines = []
    with serial.Serial(port, timeout=1.0) as ser:
        deadline = time.monotonic() + limit
        block = None
        while time.monotonic() < deadline:
            lines.append(ser.readline().decode("utf-8", "replace"))
            block = parse_block(lines)
            if block is not None:
                break

    if block is None:
        print("no complete SHELL_SETTLE block within %.0f s" % limit,
              file=sys.stderr)
        return 1

    csv = format_csv(block)
    if out:
        with open(out, "w", encoding="utf-8", newline="") as fh:
            fh.write(csv)
    else:
        sys.stdout.write(csv)

    cal = block["cal"]
    print("lat_mean_ns=%d b0=%d gates_ok=%d"
          % (cal["lat_mean_ns"], cal["b0"], cal["gates_ok"]), file=sys.stderr)
    for k in block["knees"]:
        print("pair=%d d_settle_ns=%d predicted_ns=%d%s"
              % (k["pair"], k["d_settle_ns"], k["predicted_ns"],
                 "  (reference)" if k["reference"] else ""), file=sys.stderr)
    if not cal["gates_ok"]:
        print("GATES FAILED -- no settle time from this run may be quoted",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Run the guard to verify it passes**

```bash
python shell/test_read_settle.py
```

Run it with `shell/` as the working directory. Expected: `read_settle guard: ok`.

- [ ] **Step 5: Register it with ctest**

In `CMakeLists.txt`, after the `read_coupon_guard` block:

```cmake
add_test(NAME read_settle_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_settle.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)
```

- [ ] **Step 6: Run the whole suite**

```bash
source env.sh; cmake -S . -B build -DCMAKE_BUILD_TYPE=Release; cmake --build build; ctest --test-dir build --output-on-failure
```

Expected: 8/8 tests pass.

- [ ] **Step 7: Prove the RED once**

In `parse_block()`, change

```python
                        or len(block["points"]) != want):
```

to

```python
                        or len(block["points"]) < 0):
```

Run the guard. Expected: FAIL on "a block missing a row is refused". **Retype** the original line, re-run, confirm green, and report what you saw. This is the check that a truncated block cannot masquerade as a complete one, and the bring-up plan shipped a version of it that could not go red — do not repeat that.

- [ ] **Step 8: Read a real block from the board**

```bash
python shell/read_settle.py COM4 settle.csv
```

Report the exit code, the stderr summary, and the row count in the CSV.

- [ ] **Step 9: Commit**

```bash
git add shell/read_settle.py shell/test_read_settle.py CMakeLists.txt
git commit -m "feat(shell): the settle reader, and a guard that has a runner

Accumulates until the end marker: a partial block would report a clean
instrument because the failing tail never arrived. A run whose gates failed
still parses -- refusing it would hide the numbers that say why -- but the
exit code is 1 and the reader says plainly that no settle time from it may
be quoted.

The guard is registered in CMakeLists.txt. tools/test_*.py in this repo is
wired to nothing, and the VCV panel guard stood red for 23 days behind
exactly that gap.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Self-review against the spec

| Spec section | Where it lands |
|---|---|
| §1 what this is | The whole plan |
| §2 ADC not node | No task; it is the rationale, and there is nothing to build |
| §3 libDaisy's ADC unusable | Task 4 step 3 — calibration once, outside the loop |
| §4 the instrument | Task 4 (ADC1, DWT, `write_chain_timed`, the one-measurement sequence) |
| §5 board profile | **Already built** — see the note at the top |
| §6 what gets measured | Task 1 (table, grid) + Task 5 (the sweep, the read settled reference) |
| §7 the zero point | Task 2 (`is_reference`, G1) + Task 5 (knees printed beside predictions) |
| §7a the gates in numbers | Task 2 (all four, G2 red-proven) + Task 4 (the latency G4 needs) |
| §8 output | Task 3 (config + end), Task 4 (`SHELL_SETTLE_CAL`), Task 5 (points, knees, gates) |
| §9 how this can go red | Task 1 step 7, Task 2 step 6, Task 6 step 7; the first bullet is already built |
| §10 read/derived/unmeasured | Task 1's header labels the table derived; Task 4 step 7 moves the latency to measured |
| §11 out of scope | Nothing built for requirement 8, correctly — it cannot be met on this board |

**One gap, stated rather than hidden:** §7 says `d_settle(P0)` is *subtracted* from the other 4067 pairs and `d_settle(P5)` from the 4051 pair. This plan prints the reference knees beside every other knee but does **not** apply the subtraction in firmware. That is deliberate. G1 constrains both reference knees to at most one grid step, so the subtrahend is either 0 or 100 ns — at or below the grid's own resolution — and a subtraction at that scale would dress a rounding step up as a correction. The numbers are all in the block; if the run shows a reference knee that genuinely warrants subtracting, it can be applied in analysis where it is visible. Raise this with Bastian before Task 5 if you disagree.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-09-17-coupon-settle-probe.md`. Two execution options:

**1. Subagent-Driven (recommended)** — a fresh subagent per task, review between tasks, fast iteration. Tasks 3, 4 and 5 need the board on USB and a human to press BOOT+RESET, so those halt for the operator.

**2. Inline Execution** — tasks run in this session with checkpoints for review.
