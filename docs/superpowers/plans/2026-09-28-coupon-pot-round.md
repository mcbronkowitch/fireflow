# Coupon Pot Round Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add RV2, RV4 and RV6 at mid travel to the existing settle and wait probes behind one build switch, `SHELL_POT_ROUND`, plus the host readers that turn two board captures into round four's answer.

**Architecture:** The plans (`settle_plan`, `wait_plan`) carry both halves at all times and never read a switch, so the host suite holds both; only `settle_probe.cpp` and `wait_probe.cpp` read `SHELL_POT_ROUND` and pick a pair/victim count from it. A new header `pot_plan.h` is the one pot table; a new Python module `pot_round.py` is its host-side twin and holds the two position gates. `read_pots.py` joins the two readers' output files.

**Tech Stack:** C++20 (firmware: ARM GCC via `shell/Makefile`; host tests: clang + doctest via CMake), Python 3 (readers and guards, run as plain scripts — pytest is not installed).

**Spec:** `docs/superpowers/specs/2026-09-28-coupon-pot-round-design.md`

## Global Constraints

- Two toolchains, never mixed. Host: `source env.sh` then CMake/Ninja with `-DCMAKE_BUILD_TYPE=Release` (a Debug configure fails two render-hash tests). Firmware: in a shell **without** `env.sh`, `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images ...`.
- `ctest` does **not** build. Always `cmake --build build` first, or a green run can be a stale binary.
- Never prefix a shell command with `cd`. Commands below run from the repository root.
- Everything written into the repo is English. Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **At `SHELL_POT_ROUND=0` both images measure and print exactly what they do today** (spec §2): no existing `PrintLine` format string changes, no field is added to an existing line. `read_settle_guard` and `read_wait_guard` stay green against their existing fixtures and the vendored `shell/testdata/wait-block-e22a628.txt`.
- The pot table (spec §3): RV2 = group 0 ch 2, hi 1, lo 3, 10 k; RV4 = group 0 ch 6, hi 5, lo 7, 20 k; RV6 = group 1 ch 2, hi 1, lo 3, 10 k. R_src at mid = R_track/4 + 150 Ω → 2650 / 5150 / 2650.
- Settle pot pairs P6–P11 (spec §4), in this order: RV2 from hi, RV2 from lo, RV4 from hi, RV4 from lo, RV6 from hi, RV6 from lo. Predictions 1552 / 1552 / 3016 / 3016 / 955 / 955 ns.
- G3 is computed over P0–P5 only, at either switch position (spec §4).
- Wait victims 0–4 are `kXtalkVictimTable` unchanged; 5–7 are RV2, RV4, RV6. Case = `arm × victims + victim` (spec §5).
- `SHELL_POT_CFG` / `SHELL_POT_ID` print **directly after** each probe's own `_CFG` line (spec §6).
- PG1: reading in **[28180, 37355]** counts, inclusive. PG2: |settle − wait| **≤ 1024** counts (spec §7).
- A test that cannot go red gets fixed: every new test is proved red once by the sabotage its step names, then the sabotage is reverted **by hand-editing it back**, never with `git checkout <file>` (that would also discard the step's own work).
- Scripted edits (sed/python) convert CRLF files to LF invisibly. Use the Edit tool for every change to an existing file.

## File Structure

| file | status | responsibility |
|---|---|---|
| `shell/pot_plan.h` | create | the pot table and `pot_r_src_mid()`; header-only, host-compilable |
| `tests/test_pot_plan.cpp` | create | the table against `coupon_expect` and `design.py`'s values |
| `shell/settle_plan.h/.cpp` | modify | `kSettlePotPlan`, `settle_pair()`, `settle_pair_count()`, `g3_spread()`, `RunSummary` sized to 12 |
| `tests/test_settle_plan.cpp` | modify | pot-pair and `g3_spread` tests |
| `shell/wait_plan.h/.cpp` | modify | `wait_victim()`, `wait_victim_count()`, `wait_block_estimate_ms(int)` |
| `tests/test_wait_plan.cpp` | modify | victim and estimate tests |
| `shell/write_shell_pot_round.py` | create | writes `build/shell_pot_round.h`, deletes stale objects |
| `shell/pot_report.h/.cpp` | create | `print_pot_lines()`, the two announcement lines both probes print |
| `shell/settle_probe.cpp`, `shell/wait_probe.cpp` | modify | run to `kPairs` / `kVictims`, print the pot lines |
| `shell/Makefile` | modify | the switch, its validation, the new source and dependencies |
| `shell/pot_round.py` | create | host twin of `pot_plan.h`, PG1/PG2, `r_src()`, pot-line parser |
| `shell/test_pot_round.py` | create | guard for `pot_round.py`, incl. the header cross-check |
| `shell/read_settle.py`, `shell/test_read_settle.py` | modify | parse pot lines, completeness, PG1, metadata rows |
| `shell/read_wait.py`, `shell/test_read_wait.py` | modify | victims per block, pot lines, PG1, a metadata file |
| `shell/read_pots.py`, `shell/test_read_pots.py` | create | the joining reader and its guard |
| `CMakeLists.txt` | modify | register `test_pot_plan.cpp` and the two new guards |
| `hardware/coupon/scripts/design.py` | modify | the `REF_A` comment (spec §1 correction) |
| `docs/roadmap.md` | modify | the round-four status entry |

---

### Task 1: The pot table

**Files:**
- Create: `shell/pot_plan.h`
- Create: `tests/test_pot_plan.cpp`
- Modify: `CMakeLists.txt:103-104` (test list)
- Modify: `hardware/coupon/scripts/design.py:76-78` (comment only)

**Interfaces:**
- Produces: `struct shell::Pot { const char* name; int group; int channel; int hi_ch; int lo_ch; uint32_t r_track_ohm; }`, `shell::kPotCount` (3), `shell::kPots[3]`, `shell::kPotRonOhm` (150), `constexpr uint32_t shell::pot_r_src_mid(uint32_t r_track_ohm)`.

- [ ] **Step 1: Write the failing test**

Create `tests/test_pot_plan.cpp`:

```cpp
// Round four's pot table, held against the coupon's own channel plan. If
// this file and hardware/coupon/scripts/design.py disagree, design.py wins
// and this file is wrong.
// Spec: ../docs/superpowers/specs/2026-09-28-coupon-pot-round-design.md
#include <cstring>
#include <doctest/doctest.h>
#include "../shell/coupon_expect.h"
#include "../shell/mux_plan.h"
#include "../shell/pot_plan.h"

TEST_CASE("pot plan: each pot's neighbours sit at opposite rails") {
    // The rig step 5b calls for: only with the neighbours at opposite
    // rails does a step onto the wiper show short settling at all.
    for(int i = 0; i < shell::kPotCount; ++i) {
        const shell::Pot& p = shell::kPots[i];
        CAPTURE(i);
        const int hi = shell::step_of(shell::kCouponChain, p.group, p.hi_ch);
        const int lo = shell::step_of(shell::kCouponChain, p.group, p.lo_ch);
        const int me = shell::step_of(shell::kCouponChain, p.group, p.channel);
        // A step of -1 reads as Unchecked, so a wrong channel number must
        // fail here rather than pass the wiper check below vacuously.
        REQUIRE(hi >= 0);
        REQUIRE(lo >= 0);
        REQUIRE(me >= 0);
        CHECK(shell::coupon_expect(hi) == shell::Expect::High);
        CHECK(shell::coupon_expect(lo) == shell::Expect::Low);
        CHECK(shell::coupon_expect(me) == shell::Expect::Unchecked);
    }
}

TEST_CASE("pot plan: the fitted values are design.py's POT_VALUES") {
    // design.py: RV2 10k, RV4 20k, RV6 10k -- and what is soldered on the
    // board (hardware/coupon/order-bom.md, "The pots").
    CHECK(std::strcmp(shell::kPots[0].name, "RV2") == 0);
    CHECK(shell::kPots[0].r_track_ohm == 10000u);
    CHECK(std::strcmp(shell::kPots[1].name, "RV4") == 0);
    CHECK(shell::kPots[1].r_track_ohm == 20000u);
    CHECK(std::strcmp(shell::kPots[2].name, "RV6") == 0);
    CHECK(shell::kPots[2].r_track_ohm == 10000u);
}

TEST_CASE("pot plan: the mid-travel source impedance is R/4 plus Ron") {
    // A linear pot at 50/50 is two R/2 halves in parallel: R/4. Plus the
    // same 150 ohm switch Ron every other table on this board carries.
    CHECK(shell::pot_r_src_mid(10000u) == 2650u);
    CHECK(shell::pot_r_src_mid(20000u) == 5150u);
}
```

Register it in `CMakeLists.txt`, directly after the wait-plan lines:

```cmake
    shell/wait_plan.cpp
    tests/test_wait_plan.cpp
    tests/test_pot_plan.cpp
```

- [ ] **Step 2: Run it to verify it fails**

Run: `source env.sh && cmake --build build`
Expected: compile error, `../shell/pot_plan.h` not found.

- [ ] **Step 3: Write the header**

Create `shell/pot_plan.h`:

```cpp
#pragma once

// The three pots round four measures at mid travel. Data only, no hardware
// type, so the host suite holds it (tests/test_pot_plan.cpp) -- the same
// reason as settle_plan.h and xtalk_plan.h.
//
// Every number here is DERIVED: the channels from
// hardware/coupon/scripts/design.py, the values from what is soldered on the
// board. R_track is NOMINAL and stays nominal -- the pot ends hang on
// A+3V3/AGND together with three dividers and R_BTN, so the track cannot be
// metered in circuit (spec section 3).
//
// shell/pot_round.py carries the host-side copy; test_pot_round.py parses
// this file and fails if the two drift apart. Keep one pot per line in the
// {"name", group, channel, hi, lo, r_track} shape that parser expects.
//
// Spec: ../docs/superpowers/specs/2026-09-28-coupon-pot-round-design.md
#include <cstdint>

namespace shell {

struct Pot
{
    const char* name;
    int         group;        // 0 = the 4067 on ADC_9, 1 = the 4051 on ADC_10
    int         channel;      // the wiper's mux channel
    int         hi_ch;        // neighbour tied to A+3V3 through 0 ohm
    int         lo_ch;        // neighbour tied to AGND through 0 ohm
    uint32_t    r_track_ohm;  // nominal track resistance
};

// The switch on-resistance every source-impedance table on this board adds.
inline constexpr uint32_t kPotRonOhm = 150;

// A linear pot at 50/50 is two R/2 halves in parallel -- R/4 -- the peak of
// its wiper's source impedance.
constexpr uint32_t pot_r_src_mid(uint32_t r_track_ohm)
{
    return r_track_ohm / 4u + kPotRonOhm;
}

inline constexpr int kPotCount = 3;
inline constexpr Pot kPots[kPotCount] = {
    {"RV2", 0, 2, 1, 3, 10000},
    {"RV4", 0, 6, 5, 7, 20000},
    {"RV6", 1, 2, 1, 3, 10000},
};

} // namespace shell
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `source env.sh && cmake --build build && build/spky_tests.exe -tc="pot plan*"`
Expected: 3 test cases passed.

- [ ] **Step 5: Prove the neighbour test can go red**

Edit `shell/pot_plan.h`: change RV2's row to `{"RV2", 0, 2, 3, 1, 10000},` (hi and lo swapped).
Run: `source env.sh && cmake --build build && build/spky_tests.exe -tc="pot plan: each*"`
Expected: FAIL on the `Expect::High` and `Expect::Low` checks for `i := 0`.
Restore the row to `{"RV2", 0, 2, 1, 3, 10000},` with the Edit tool and re-run: PASS.

- [ ] **Step 6: Correct design.py's REF_A comment**

In `hardware/coupon/scripts/design.py` (lines 76–77, read 2026-09-28), replace

```python
# "the pot is noisy": REF_A has the source impedance of a 10k pot at mid travel,
# REF_B is a tenth of that. If REF_B is quiet and REF_A is not, the noise came
```

with

```python
# "the pot is noisy": REF_A (10k/10k, 5k) has the source impedance of a 20k pot
# at mid travel -- R/4; this said "10k pot" until 2026-09-28 -- and REF_B is a
# tenth of that. If REF_B is quiet and REF_A is not, the noise came
```

It is a comment: no generator reads it, and no generated file under `hardware/coupon/` changes.

- [ ] **Step 7: Commit**

```bash
git add shell/pot_plan.h tests/test_pot_plan.cpp CMakeLists.txt hardware/coupon/scripts/design.py
git commit -m "feat(shell): the pot round's pot table, and REF_A is a 20k pot's impedance

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: The settle plan grows six pot pairs

**Files:**
- Modify: `shell/settle_plan.h` (after `kSettlePlan`'s declaration at line 29; `RunSummary::knee_ns` at line 147; before `settle_gates` at line 217)
- Modify: `shell/settle_plan.cpp` (after `kSettlePlan` at line 18; after `settle_gates` at line 98)
- Test: `tests/test_settle_plan.cpp` (append at the end)

**Interfaces:**
- Consumes: `shell::kPots`, `shell::pot_r_src_mid()` (Task 1).
- Produces: `shell::kSettlePotPairs` (6), `shell::kSettlePairsMax` (12), `shell::kSettlePotPlan[6]`, `constexpr int shell::settle_pair_count(bool pot_round)`, `const shell::SettlePair& shell::settle_pair(int p)`, `int32_t shell::g3_spread(const int32_t* per_pair, int n)`. `kSettlePairs` stays 6 and `kSettlePlan` stays P0–P5, unchanged.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_settle_plan.cpp` (at the very end, so the file's anonymous-namespace helpers `required_acq_s` and `rung_window_s` are in scope). Add `#include "../shell/pot_plan.h"` to the includes at the top.

```cpp
// --- Round four: the pot pairs (spec 2026-09-28-coupon-pot-round-design.md
// section 4) ---

TEST_CASE("settle plan: the pot round adds six pairs and keeps P0-P5") {
    CHECK(shell::kSettlePotPairs == 6);
    CHECK(shell::kSettlePairsMax == 12);
    CHECK(shell::settle_pair_count(false) == 6);
    CHECK(shell::settle_pair_count(true) == 12);
    // P0-P5 are kSettlePlan itself, not a copy that could drift.
    for(int p = 0; p < shell::kSettlePairs; ++p) {
        CAPTURE(p);
        CHECK(&shell::settle_pair(p) == &shell::kSettlePlan[p]);
    }
}

TEST_CASE("settle plan: each pot is stepped onto from its high, then its low "
          "neighbour") {
    for(int i = 0; i < shell::kPotCount; ++i) {
        const shell::Pot& pot = shell::kPots[i];
        const shell::SettlePair& from_hi = shell::settle_pair(6 + 2 * i);
        const shell::SettlePair& from_lo = shell::settle_pair(7 + 2 * i);
        CAPTURE(i);
        CHECK(from_hi.group == pot.group);
        CHECK(from_hi.to_ch == pot.channel);
        CHECK(from_hi.from_ch == pot.hi_ch);
        CHECK(from_lo.group == pot.group);
        CHECK(from_lo.to_ch == pot.channel);
        CHECK(from_lo.from_ch == pot.lo_ch);
        CHECK(from_hi.r_src_ohm == shell::pot_r_src_mid(pot.r_track_ohm));
        CHECK(from_lo.r_src_ohm == shell::pot_r_src_mid(pot.r_track_ohm));
        // A pot is the thing measured, never the instrument's zero: G1
        // reads reference pairs only, and a pot marked reference would
        // turn a slow wiper into a failed instrument.
        CHECK_FALSE(from_hi.is_reference);
        CHECK_FALSE(from_lo.is_reference);
    }
}

TEST_CASE("settle plan: the pot pairs' predictions are recomputed, not copied") {
    const double kC[2]   = {65e-12, 40e-12};
    const double kLn8192 = 9.0109;
    for(int p = shell::kSettlePairs; p < shell::kSettlePairsMax; ++p) {
        const shell::SettlePair& sp = shell::settle_pair(p);
        CAPTURE(p);
        const double ns = static_cast<double>(sp.r_src_ohm) * kC[sp.group] * kLn8192 * 1e9;
        CHECK(std::abs(ns - static_cast<double>(sp.tau9_ns)) < 0.5);
    }
}

TEST_CASE("settle plan: every pot pair's channels exist, its rung covers it, "
          "and the park and grid still reach past it") {
    for(int p = shell::kSettlePairs; p < shell::kSettlePairsMax; ++p) {
        const shell::SettlePair& sp = shell::settle_pair(p);
        CAPTURE(p);
        const int n = shell::kCouponChain.channels[sp.group];
        CHECK(sp.from_ch >= 0);
        CHECK(sp.from_ch < n);
        CHECK(sp.to_ch >= 0);
        CHECK(sp.to_ch < n);
        const int idx = shell::sample_time_index_for(sp.r_src_ohm);
        CHECK(rung_window_s(idx) >= required_acq_s(sp.r_src_ohm));
        if(idx > 0) CHECK(rung_window_s(idx - 1) < required_acq_s(sp.r_src_ohm));
        CHECK(shell::kParkNs >= 5 * sp.tau9_ns);
        CHECK(shell::grid_ns(shell::kGridPoints - 1) > 2 * sp.tau9_ns);
    }
}

TEST_CASE("settle gates: G3's spread reads P0-P5 only") {
    // Spec section 4: a wiper that wanders more than a divider must not
    // refuse the run that is measuring it. -1 is "no settled region".
    int32_t per_pair[shell::kSettlePairsMax] = {3, 4, 2, 4, 3, 1,
                                                50, 60, -1, 70, 2, 2};
    CHECK(shell::g3_spread(per_pair, shell::kSettlePairsMax) == 4);
    CHECK(shell::g3_spread(per_pair, shell::kSettlePairs) == 4);
    // The six-pair probe's own behaviour, unchanged: the max over what has
    // a settled region, 0 when nothing has one.
    int32_t none[shell::kSettlePairs] = {-1, -1, -1, -1, -1, -1};
    CHECK(shell::g3_spread(none, shell::kSettlePairs) == 0);
    int32_t wide[shell::kSettlePairs] = {1, 9, -1, 2, 3, 0};
    CHECK(shell::g3_spread(wide, shell::kSettlePairs) == 9);
}

TEST_CASE("settle gates: a RunSummary holds a knee for every pair of the pot "
          "round") {
    shell::RunSummary s{};
    CHECK(sizeof(s.knee_ns) / sizeof(s.knee_ns[0])
          == static_cast<size_t>(shell::kSettlePairsMax));
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `source env.sh && cmake --build build`
Expected: compile errors — `kSettlePotPairs`, `settle_pair_count`, `settle_pair`, `g3_spread` undeclared.

- [ ] **Step 3: Implement**

In `shell/settle_plan.h`, directly after `extern const SettlePair kSettlePlan[kSettlePairs];`:

```cpp
// Round four (spec 2026-09-28-coupon-pot-round-design.md section 4): six more
// pairs, two per pot, stepping onto the wiper from each of its rail-tied
// neighbours -- P6..P11. The plan carries them at all times and reads no
// switch, so the host suite holds both halves; only settle_probe.cpp decides,
// from SHELL_POT_ROUND, how many pairs it sweeps. P0-P5 keep their numbers
// and their meaning at either position.
inline constexpr int kSettlePotPairs = 6;
inline constexpr int kSettlePairsMax = kSettlePairs + kSettlePotPairs;
extern const SettlePair kSettlePotPlan[kSettlePotPairs];

constexpr int settle_pair_count(bool pot_round)
{
    return pot_round ? kSettlePairsMax : kSettlePairs;
}

// Pair p of the full numbering: kSettlePlan for P0-P5, kSettlePotPlan for
// P6-P11. p must be in [0, kSettlePairsMax).
const SettlePair& settle_pair(int p);
```

In `RunSummary`, change

```cpp
    int32_t knee_ns[kSettlePairs];  // -1 where the pair never settled
```

to

```cpp
    int32_t knee_ns[kSettlePairsMax];  // -1 where the pair never settled
```

Directly before `Gates settle_gates(const RunSummary& s);`:

```cpp
// G3's input, reduced from the per-pair settled-region mean spreads the sweep
// prints on SHELL_SETTLE_BAND (-1 = the pair has no settled region). Only
// P0-P5 enter it, at either switch position: G3 judges the instrument, and a
// pot wiper that wanders more than a divider must not refuse the run that is
// measuring it (spec 2026-09-28 section 4). 0 when no counted pair has a
// settled region -- the value the sweep's running max always started from.
int32_t g3_spread(const int32_t* per_pair, int n);
```

In `shell/settle_plan.cpp`, add `#include "pot_plan.h"` below `#include "settle_plan.h"`, and directly after `kSettlePlan`'s closing `};`:

```cpp
// Round four's pot pairs. Channels and values from pot_plan.h's kPots (the
// host test holds every row to it); R_src is pot_r_src_mid(), the pot at
// 50/50; tau9 is tools/settle_budget.py's term A for that impedance, run
// 2026-09-28 -- derived, and recomputed by the host test, not measured.
const SettlePair kSettlePotPlan[kSettlePotPairs] = {
    // group from  to   R_src  9.01 tau  reference
    {0, 1, 2, 2650, 1552, false},   // P6   ch1 (A+3V3) -> RV2, 10k at mid
    {0, 3, 2, 2650, 1552, false},   // P7   ch3 (AGND)  -> RV2
    {0, 5, 6, 5150, 3016, false},   // P8   ch5 (A+3V3) -> RV4, 20k at mid
    {0, 7, 6, 5150, 3016, false},   // P9   ch7 (AGND)  -> RV4
    {1, 1, 2, 2650, 955, false},    // P10  ch1 (A+3V3) -> RV6, 10k at mid
    {1, 3, 2, 2650, 955, false},    // P11  ch3 (AGND)  -> RV6
};

const SettlePair& settle_pair(int p)
{
    return (p < kSettlePairs) ? kSettlePlan[p] : kSettlePotPlan[p - kSettlePairs];
}
```

And directly after `settle_gates()`'s closing brace:

```cpp
int32_t g3_spread(const int32_t* per_pair, int n)
{
    int32_t   worst   = 0;
    const int counted = (n < kSettlePairs) ? n : kSettlePairs;
    for(int p = 0; p < counted; ++p)
        if(per_pair[p] > worst) worst = per_pair[p];
    return worst;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `source env.sh && cmake --build build && build/spky_tests.exe -tc="settle*"`
Expected: every `settle*` case passes, the pre-existing ones included.

- [ ] **Step 5: Prove the G3 scope test can go red**

Edit `g3_spread()`: change `const int counted = (n < kSettlePairs) ? n : kSettlePairs;` to `const int counted = n;`.
Run: `source env.sh && cmake --build build && build/spky_tests.exe -tc="settle gates: G3's spread*"`
Expected: FAIL — `70 == 4`.
Restore the line with the Edit tool; re-run: PASS.

- [ ] **Step 6: Commit**

```bash
git add shell/settle_plan.h shell/settle_plan.cpp tests/test_settle_plan.cpp
git commit -m "feat(shell): the settle plan's six pot pairs, and G3 reads P0-P5 only

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The wait plan grows three pot victims

**Files:**
- Modify: `shell/wait_plan.h` (includes at line 14-15; after `kWaitRepeats` at line 67; the estimate declaration at line 113)
- Modify: `shell/wait_plan.cpp:9-24`
- Test: `tests/test_wait_plan.cpp` (append)

**Interfaces:**
- Consumes: `shell::kPots`, `shell::kPotCount`, `shell::pot_r_src_mid()` (Task 1); `shell::XtalkVictim`, `shell::kXtalkVictimTable`, `shell::kXtalkVictims` (existing, `xtalk_plan.h`).
- Produces: `shell::kWaitVictimsMax` (8), `constexpr int shell::wait_victim_count(bool pot_round)`, `constexpr shell::XtalkVictim shell::wait_victim(int v)`, `uint32_t shell::wait_block_estimate_ms(int victims = kXtalkVictims)`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_wait_plan.cpp` (add `#include <cstdlib>` and `#include "../shell/pot_plan.h"` at the top if absent):

```cpp
// --- Round four: the pot victims (spec 2026-09-28-coupon-pot-round-design.md
// section 5) ---

TEST_CASE("wait: the pot round appends three victims and keeps round one's") {
    CHECK(shell::kWaitVictimsMax == 8);
    CHECK(shell::wait_victim_count(false) == 5);
    CHECK(shell::wait_victim_count(true) == 8);
    for(int v = 0; v < shell::kXtalkVictims; ++v) {
        CAPTURE(v);
        const shell::XtalkVictim w = shell::wait_victim(v);
        CHECK(w.group == shell::kXtalkVictimTable[v].group);
        CHECK(w.channel == shell::kXtalkVictimTable[v].channel);
        CHECK(w.r_src_ohm == shell::kXtalkVictimTable[v].r_src_ohm);
    }
    for(int i = 0; i < shell::kPotCount; ++i) {
        CAPTURE(i);
        const shell::XtalkVictim w = shell::wait_victim(shell::kXtalkVictims + i);
        CHECK(w.group == shell::kPots[i].group);
        CHECK(w.channel == shell::kPots[i].channel);
        CHECK(w.r_src_ohm == shell::pot_r_src_mid(shell::kPots[i].r_track_ohm));
    }
}

TEST_CASE("wait: the block estimate scales with the victim count") {
    // Spec section 5: ~89.7 s at five victims, ~143.5 s at eight. The
    // estimate is per-victim cost times the count, floored to ms once, so
    // the two differ from an exact 8:5 by less than 8 ms.
    const uint32_t five  = shell::wait_block_estimate_ms(5);
    const uint32_t eight = shell::wait_block_estimate_ms(8);
    CAPTURE(five);
    CAPTURE(eight);
    CHECK(five == shell::wait_block_estimate_ms());
    CHECK(std::llabs(static_cast<long long>(eight) * 5 - static_cast<long long>(five) * 8) < 8);
    CHECK(eight > 120000u);
    CHECK(eight < 180000u);
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `source env.sh && cmake --build build`
Expected: compile errors — `kWaitVictimsMax`, `wait_victim_count`, `wait_victim` undeclared; `wait_block_estimate_ms` takes no argument.

- [ ] **Step 3: Implement**

In `shell/wait_plan.h`, add `#include "pot_plan.h"` after `#include "xtalk_plan.h"`. Replace the header comment's sentence

```cpp
// It defines NO victim table: the victims are round one's five, used directly
// from xtalk_plan.h, exactly as round two uses them.
```

with

```cpp
// It defines NO victim table: the victims are round one's five, used directly
// from xtalk_plan.h, exactly as round two uses them -- plus, in a
// SHELL_POT_ROUND=1 image, pot_plan.h's three pots appended as victims 5-7
// (wait_victim() below). The plan reads no switch; wait_probe.cpp does.
```

Directly after `inline constexpr int kWaitRepeats = kRepeats;`:

```cpp
// Round four (spec 2026-09-28-coupon-pot-round-design.md section 5): the
// pots join round one's five victims as 5..7, in pot_plan.h's order. Case
// numbering stays arm * victims + victim, so a five-victim image numbers its
// cases exactly as it always did.
inline constexpr int kWaitVictimsMax = kXtalkVictims + kPotCount;

constexpr int wait_victim_count(bool pot_round)
{
    return pot_round ? kWaitVictimsMax : kXtalkVictims;
}

// Victim v of the full numbering, by value: kXtalkVictimTable for 0..4, a pot
// at mid travel for 5..7. v must be in [0, kWaitVictimsMax).
constexpr XtalkVictim wait_victim(int v)
{
    return v < kXtalkVictims
               ? kXtalkVictimTable[v]
               : XtalkVictim{kPots[v - kXtalkVictims].group,
                             kPots[v - kXtalkVictims].channel,
                             pot_r_src_mid(kPots[v - kXtalkVictims].r_track_ohm)};
}
```

Change the estimate's declaration to

```cpp
uint32_t wait_block_estimate_ms(int victims = kXtalkVictims);
```

and add one line to the comment above it: `// `victims` is the count the image sweeps -- wait_victim_count().`

In `shell/wait_plan.cpp`, change the definition's signature to `uint32_t wait_block_estimate_ms(int victims)`, the comment's "each summed over five victims" to "each summed over `victims` victims", and the return to

```cpp
    return static_cast<uint32_t>(per_victim_us * static_cast<uint64_t>(victims) / 1000u);
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `source env.sh && cmake --build build && build/spky_tests.exe -tc="wait*"`
Expected: all `wait*` cases pass, including the pre-existing "under two minutes" one.

- [ ] **Step 5: Prove the scaling test can go red**

Edit the return in `wait_plan.cpp` back to `per_victim_us * kXtalkVictims / 1000u` (ignoring `victims`).
Run: `source env.sh && cmake --build build && build/spky_tests.exe -tc="wait: the block estimate scales*"`
Expected: FAIL — `eight > 120000u` does not hold.
Restore the `victims` form with the Edit tool; re-run: PASS.

- [ ] **Step 6: Commit**

```bash
git add shell/wait_plan.h shell/wait_plan.cpp tests/test_wait_plan.cpp
git commit -m "feat(shell): the wait plan's three pot victims, and an estimate per count

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: The switch and the two probes

**Files:**
- Create: `shell/write_shell_pot_round.py`
- Create: `shell/pot_report.h`, `shell/pot_report.cpp`
- Modify: `shell/Makefile` (after the `SHELL_WAIT_PROBE` block ending near line 281; `CPP_SOURCES`; `SWITCH_HEADERS`; the git-hash line; dependency lines)
- Modify: `shell/settle_probe.cpp` (lines 1-7, 20, 179-185, 263-267, 290-294, 365-394, 568-570, 584, 608-657)
- Modify: `shell/wait_probe.cpp` (lines 1-11, 186-187, 226-229, 270-296, 366-368)

**Interfaces:**
- Consumes: `settle_pair_count()`, `settle_pair()`, `g3_spread()`, `kSettlePairsMax` (Task 2); `wait_victim_count()`, `wait_victim()` (Task 3); `kPots`, `kPotCount` (Task 1).
- Produces: firmware lines `SHELL_POT_CFG round=1 pots=3 git=<hash>` and `SHELL_POT_ID idx=<i> name=<RVn> group=<g> ch=<c> r_track=<ohm> hi=<h> lo=<l>`, printed directly after `SHELL_SETTLE_CFG` / `SHELL_WAIT_CFG` in a `SHELL_POT_ROUND=1` image. Tasks 5–8 parse exactly these.

This task has no host test: the probes include `daisy_seed.h` and do not compile on the host. Its checks are the four builds, the `cmp` proof, and the unchanged-format proof below.

- [ ] **Step 1: The switch writer**

Create `shell/write_shell_pot_round.py`:

```python
"""Writes the pot-round switch as a real header.

Same shape and same reason as write_shell_wait_probe.py: a bare -D is
invisible to make's dependency graph, and an existing build/ would happily
reuse a stale settle_probe.o or wait_probe.o -- shipping a five-victim image
under a pot-round name, or the reverse.

ALWAYS defines the symbol, including in position 0, because
`#if SHELL_POT_ROUND` and `SHELL_POT_ROUND != 0` have to work in both.

THE TIMESTAMP EDGE IS NOT ENOUGH (2026-08-23, see write_shell_wait_probe.py):
this script deletes the dependent objects itself whenever the content
changes. The objects come in as further arguments.

Spec: ../docs/superpowers/specs/2026-09-28-coupon-pot-round-design.md
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in {"0", "1"}:
        raise SystemExit(
            "usage: write_shell_pot_round.py OUTPUT {0|1} [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    # The script runs while the Makefile is parsed, so before any rule has
    # created build/.
    output.parent.mkdir(parents=True, exist_ok=True)
    content = "#define SHELL_POT_ROUND %s\n" % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[3:]:
            Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: The shared announcement lines**

Create `shell/pot_report.h`:

```cpp
#pragma once

// Round four's two announcement lines: SHELL_POT_CFG, then one SHELL_POT_ID
// per pot. Shared by the settle and the wait probe so the two images cannot
// describe the same pots differently. Each probe prints them DIRECTLY AFTER
// its own _CFG line -- both readers open a block at that line and drop
// anything before it (spec 2026-09-28-coupon-pot-round-design.md section 6).
#include "hw/board.h"

namespace shell {

void print_pot_lines(bench::Board& hw);

} // namespace shell
```

Create `shell/pot_report.cpp`:

```cpp
#include "pot_report.h"

#include "pot_plan.h"
#include "shell_git_hash.h"

namespace shell {

void print_pot_lines(bench::Board& hw)
{
    // BYTE BUDGET: the ID line runs ~65 bytes at its widest, the CFG line
    // ~40 plus the hash, both far inside libDaisy's 125-byte payload
    // (logger.h:29). shell/pot_round.py parses both field sets exactly.
    hw.PrintLine("SHELL_POT_CFG round=1 pots=%d git=%s", kPotCount, SHELL_GIT_HASH);
    for(int i = 0; i < kPotCount; ++i)
    {
        const Pot& p = kPots[i];
        hw.PrintLine("SHELL_POT_ID idx=%d name=%s group=%d ch=%d r_track=%d "
                     "hi=%d lo=%d",
                     i, p.name, p.group, p.channel,
                     static_cast<int>(p.r_track_ohm), p.hi_ch, p.lo_ch);
    }
}

} // namespace shell
```

- [ ] **Step 3: Wire the switch into the Makefile**

In `shell/Makefile`, directly after the `SHELL_WAIT_PROBE` validation block (the `endif` that closes `ifeq ($(SHELL_WAIT_PROBE),1)`), add:

```make
# Round four: adds the three pots (RV2, RV4, RV6 at mid travel) to the settle
# probe as six extra pairs, or to the wait probe as three extra victims. It
# selects nothing on its own, so it is an error without exactly one of those
# two probes. Position 0 is today's six-pair / five-victim image, unchanged.
# Spec:  ../docs/superpowers/specs/2026-09-28-coupon-pot-round-design.md
# Plan:  ../docs/superpowers/plans/2026-09-28-coupon-pot-round.md
SHELL_POT_ROUND ?= 0

ifneq ($(filter $(SHELL_POT_ROUND),0 1),$(SHELL_POT_ROUND))
$(error SHELL_POT_ROUND must be 0 or 1)
endif

ifeq ($(SHELL_POT_ROUND),1)
ifeq ($(filter 1,$(SHELL_SETTLE_PROBE) $(SHELL_WAIT_PROBE)),)
$(error SHELL_POT_ROUND=1 needs SHELL_SETTLE_PROBE=1 or SHELL_WAIT_PROBE=1: \
it adds the pots to one of those two probes and selects nothing on its own)
endif
endif
```

(Settle and wait are already mutually exclusive, so "at least one" is "exactly one".)

Add `pot_report.cpp \` to `CPP_SOURCES`, directly after `wait_plan.cpp \`.

Add a line to `SWITCH_HEADERS`, directly after the `write_shell_wait_probe.py` line:

```make
  $(shell python write_shell_pot_round.py $(BUILD_DIR)/shell_pot_round.h $(SHELL_POT_ROUND) $(SWITCH_OBJECTS)) \
```

In the `write_git_hash.py` line of `SWITCH_HEADERS`, append ` $(BUILD_DIR)/pot_report.o` after `$(BUILD_DIR)/wait_probe.o`, and in the comment below it change "deletes only xtalk_probe.o, tone_probe.o and wait_probe.o" to "deletes only xtalk_probe.o, tone_probe.o, wait_probe.o and pot_report.o".

Add, after the existing dependency lines (below `$(BUILD_DIR)/mux_scan.o $(BUILD_DIR)/coupon_scan.o: ...`):

```make
$(BUILD_DIR)/settle_probe.o $(BUILD_DIR)/wait_probe.o: $(BUILD_DIR)/shell_pot_round.h
$(BUILD_DIR)/pot_report.o: $(BUILD_DIR)/shell_git_hash.h
```

- [ ] **Step 4: The settle probe runs to kPairs**

In `shell/settle_probe.cpp`:

(a) Below `#include "settle_plan.h"` add:

```cpp
#include "pot_report.h"
#include "shell_pot_round.h"
```

(b) Directly after `constexpr int kTailWindowPoints = 8;` add:

```cpp
// Round four (spec 2026-09-28-coupon-pot-round-design.md section 4): P6-P11,
// the pots, join the sweep only in a SHELL_POT_ROUND=1 image. Every per-pair
// loop and array below runs to kPairs, never to kSettlePairs, so position 0
// is exactly the six-pair probe it always was.
constexpr int kPairs = settle_pair_count(SHELL_POT_ROUND != 0);
```

(c) The rung/offset setup: `int32_t rung_idx[kSettlePairs];` and `int32_t offset_ns[kSettlePairs];` become `[kPairs]`; its loop becomes `for(int p = 0; p < kPairs; ++p)` and `kSettlePlan[p].r_src_ohm` becomes `settle_pair(p).r_src_ohm`.

(d) Directly after the `SHELL_SETTLE_CFG` `hw.PrintLine(...)` call (the statement ending `ascending_this_block ? 0 : 1);`), add:

```cpp
#if SHELL_POT_ROUND
        // Directly after _CFG, never before it: read_settle.py opens a block
        // at SHELL_SETTLE_CFG and drops everything printed ahead of it.
        print_pot_lines(hw);
#endif
```

(e) The `SHELL_SETTLE_OFFSET` loop: `p < kSettlePairs` → `p < kPairs`.

(f) The eight per-pair arrays declared before the sweep (`d_settle_ns_print`, `at_or_below_offset`, `settled_raw_pre`, `settled_raw_post`, `tail_ref`, `tail_spread`, `widest_band_counts`, `widest_band_d_ns`, `settled_mean_spread`) change `[kSettlePairs]` → `[kPairs]`. In the comment above `settled_mean_spread`, change "summary.settled_mean_spread is the MAX across pairs" to "summary.settled_mean_spread is the MAX across P0-P5 (g3_spread(), settle_plan.h)".

(g) The sweep loop header becomes

```cpp
        for(int p = 0; p < kPairs; ++p)
        {
            const SettlePair& sp = settle_pair(p);
```

(h) Inside `if(idx >= 0)`, delete these two lines (G3's running max moves out of the loop):

```cpp
                if(mean_spread > summary.settled_mean_spread)
                    summary.settled_mean_spread = mean_spread;
```

and directly before `const Gates gates = settle_gates(summary);` add:

```cpp
        // G3 reads P0-P5 only, at either switch position: a pot wiper that
        // wanders more than a divider must not refuse the run measuring it
        // (spec 2026-09-28 section 4). At position 0 this is the same max the
        // sweep used to keep inline.
        summary.settled_mean_spread = g3_spread(settled_mean_spread, kPairs);
```

(i) The `SHELL_SETTLE_KNEE`, `SHELL_SETTLE_REF` and `SHELL_SETTLE_BAND` loops: `p < kSettlePairs` → `p < kPairs`; in the KNEE call, `kSettlePlan[p].tau9_ns` → `settle_pair(p).tau9_ns` and `kSettlePlan[p].is_reference` → `settle_pair(p).is_reference`. No format string changes.

`p0 = kSettlePlan[0]` and everything referring to `p0` stay as they are.

- [ ] **Step 5: The wait probe runs to kVictims**

In `shell/wait_probe.cpp`:

(a) Below `#include "mux_scan.h"` add `#include "pot_report.h"`; below `#include "shell_git_hash.h"` add `#include "shell_pot_round.h"`.

(b) Directly after the `static_assert(kWaitCoreMhz == kCoreMhz, ...)`, add:

```cpp
// Round four (spec 2026-09-28-coupon-pot-round-design.md section 5): the
// three pots join as victims 5-7 only in a SHELL_POT_ROUND=1 image. Every
// per-victim loop and array runs to kVictims, and case = arm * kVictims +
// victim, so position 0 numbers its twenty cases exactly as before.
constexpr int kVictims = wait_victim_count(SHELL_POT_ROUND != 0);
```

(c) `int32_t g_wait_zero_mean[kXtalkVictims];` and `bool g_wait_zero_seen[kXtalkVictims];` become `[kVictims]`.

(d) Directly after the `SHELL_WAIT_CFG` `hw.PrintLine(...)` call, add:

```cpp
#if SHELL_POT_ROUND
        // Directly after _CFG, never before it: read_wait.py opens a block at
        // SHELL_WAIT_CFG and drops everything printed ahead of it.
        print_pot_lines(hw);
#endif
```

(e) The reset loop `for(int v = 0; v < kXtalkVictims; ++v)` before the arms → `v < kVictims`.

(f) The comment `// --- The four arms, arm-major. case = arm * kXtalkVictims + victim.` → `... case = arm * kVictims + victim.` In the arm loop, the victim loop becomes

```cpp
            for(int v = 0; v < kVictims; ++v)
            {
                const XtalkVictim  vv       = wait_victim(v);
                const int          case_idx = a * kVictims + v;
```

(g) The G5 loop becomes `for(int v = 0; v < kVictims; ++v)` with `const XtalkVictim vv = wait_victim(v);`. (The pots are `Expect::Unchecked`, so `coupon_verdict()` passes them; PG1 judges their position host-side — spec §5.)

`v0 = kXtalkVictimTable[3]` stays as it is.

- [ ] **Step 6: Build all four images and prove they differ**

Run each in a shell **without** `env.sh`, copying each image out before the next build:

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_SETTLE_PROBE=1 SHELL_POT_ROUND=0
```

then `cp shell/build/shell-sram.bin <scratchpad>/settle0.bin`; repeat with `SHELL_POT_ROUND=1` → `settle1.bin`; with `SHELL_WAIT_PROBE=1 SHELL_POT_ROUND=0` (and no `SHELL_SETTLE_PROBE`) → `wait0.bin`; with `SHELL_WAIT_PROBE=1 SHELL_POT_ROUND=1` → `wait1.bin`. `<scratchpad>` is the session scratchpad directory, never the repo.

Expected: all four builds succeed with no warning from the files this task touched.
Then: `cmp <scratchpad>/settle0.bin <scratchpad>/settle1.bin` and `cmp <scratchpad>/wait0.bin <scratchpad>/wait1.bin` must both report a difference (exit 1). Identical images mean a stale object survived — memory `fireflow-bench-stale-object-trap`; stop and find it before going on.

- [ ] **Step 7: Prove the validation refuses a pot round with no probe**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -n images SHELL_COUPON_PROBE=1 SHELL_POT_ROUND=1
```

Expected: make stops with "SHELL_POT_ROUND=1 needs SHELL_SETTLE_PROBE=1 or SHELL_WAIT_PROBE=1".

- [ ] **Step 8: Prove position 0's output did not change**

Run: `git diff -U0 -- shell/settle_probe.cpp shell/wait_probe.cpp | grep -E '^-.*"'`
Expected: no output — no line containing a string literal was removed, so no existing format string changed. (Added lines are the pot-line calls behind `#if SHELL_POT_ROUND` and comments.)

- [ ] **Step 9: Leave the build in its default state and commit**

Rebuild once with no switches beyond the default (`make -C shell -j8 images`, same PATH) so `build/` does not carry a probe image into the next session.

```bash
git add shell/write_shell_pot_round.py shell/pot_report.h shell/pot_report.cpp shell/Makefile shell/settle_probe.cpp shell/wait_probe.cpp
git commit -m "feat(shell): SHELL_POT_ROUND, the pots in the settle and wait probes

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: The host-side pot module

**Files:**
- Create: `shell/pot_round.py`
- Create: `shell/test_pot_round.py`
- Modify: `CMakeLists.txt` (after the `read_wait_guard` test at line 401-404)

**Interfaces:**
- Consumes: `shell/pot_plan.h`'s table text (Task 1); the two line formats (Task 4).
- Produces (module `pot_round`): `POTS` (tuple of `(name, group, ch, hi, lo, r_track)`), `RON_OHM`, `FULL_SCALE`, `PG1_LO`, `PG1_HI`, `PG2_MAX`, `SETTLE_POT_PAIR0` (6), `WAIT_POT_VICTIM0` (5), `parse_pot_line(line) -> ("cfg"|"id", dict) | None` (raises `ValueError`), `ids_match(cfg, ids) -> bool`, `r_src(reading, r_track) -> float`, `pg1(reading) -> bool`, `pg2(a, b) -> bool`, `sample_lines(git="deadbee") -> list[str]`.

- [ ] **Step 1: Write the failing guard**

Create `shell/test_pot_round.py`:

```python
"""Guard for pot_round.py: the two position gates at their edges, the
derived impedance, the pot-line parser, and the pot table held against
shell/pot_plan.h so the host copy cannot drift from the firmware's.

Runs as a plain script; pytest is not installed on this machine.
Spec: ../docs/superpowers/specs/2026-09-28-coupon-pot-round-design.md
"""
import os
import re
import sys

import pot_round as pr

FAILURES = []


def check(label, cond):
    if not cond:
        FAILURES.append(label)


def raises(fn):
    try:
        fn()
    except ValueError:
        return True
    return False


# --- A. PG1 edges, inclusive, in counts (spec section 7) ---
check("A1 PG1 admits its low edge", pr.pg1(28180))
check("A2 PG1 admits its high edge", pr.pg1(37355))
check("A3 PG1 refuses one below", not pr.pg1(28179))
check("A4 PG1 refuses one above", not pr.pg1(37356))
check("A5 PG1 refuses a missing reading", not pr.pg1(None))
check("A6 PG1 is symmetric about 32767.5",
      32767.5 - pr.PG1_LO == pr.PG1_HI - 32767.5)

# --- B. PG2 edges, inclusive ---
check("B1 PG2 admits 1024", pr.pg2(32000, 33024))
check("B2 PG2 refuses 1025", not pr.pg2(32000, 33025))
check("B3 PG2 is symmetric", pr.pg2(33024, 32000) and not pr.pg2(33025, 32000))
check("B4 PG2 refuses a missing reading", not pr.pg2(None, 32000))

# --- C. the derived impedance ---
check("C1 a 10k pot at exact mid is 2650 ohm",
      abs(pr.r_src(32767.5, 10000) - 2650.0) < 1e-6)
check("C2 a 20k pot at exact mid is 5150 ohm",
      abs(pr.r_src(32767.5, 20000) - 5150.0) < 1e-6)
check("C3 at PG1's edge the impedance is within 2 % of its peak",
      pr.r_src(pr.PG1_LO, 20000) - pr.RON_OHM > 0.98 * 5000)

# --- D. the parser ---
lines = pr.sample_lines()
parsed = [pr.parse_pot_line(l) for l in lines]
check("D1 the sample CFG line parses",
      parsed[0] == ("cfg", {"round": 1, "pots": 3, "git": "deadbee"}))
check("D2 three ID lines parse", [p[0] for p in parsed[1:]] == ["id"] * 3)
check("D3 an ID carries its name as a string",
      parsed[2][1]["name"] == "RV4" and parsed[2][1]["r_track"] == 20000)
check("D4 a foreign line is None",
      pr.parse_pot_line("SHELL_WAIT_CFG adc_khz=6146") is None)
check("D5 the overflow marker is refused",
      raises(lambda: pr.parse_pot_line(lines[1] + "$$")))
check("D6 a line short of a field is refused",
      raises(lambda: pr.parse_pot_line(lines[1].rsplit(" ", 1)[0])))
check("D7 ids_match accepts the sample",
      pr.ids_match(parsed[0][1], [p[1] for p in parsed[1:]]))
wrong = [dict(p[1]) for p in parsed[1:]]
wrong[1]["ch"] = 7
check("D8 ids_match refuses a pot on the wrong channel",
      not pr.ids_match(parsed[0][1], wrong))
check("D9 ids_match refuses a missing CFG line",
      not pr.ids_match(None, [p[1] for p in parsed[1:]]))

# --- E. the table against the firmware's ---
header = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pot_plan.h")
with open(header, encoding="utf-8") as fh:
    text = fh.read()
rows = re.findall(r'\{"(RV\d)",\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}',
                  text)
from_header = tuple((n, int(g), int(c), int(h), int(l), int(r))
                    for n, g, c, h, l, r in rows)
check("E1 pot_plan.h's kPots parses to three rows", len(from_header) == 3)
check("E2 POTS is pot_plan.h's kPots, row for row", from_header == pr.POTS)

if FAILURES:
    for f in FAILURES:
        print("FAIL: %s" % f, file=sys.stderr)
    raise SystemExit(1)
print("pot_round guard: ok")
```

Register it in `CMakeLists.txt`, directly after `read_wait_guard`:

```cmake
# Round four's shared host-side module: the two position gates, the derived
# impedance, and the pot table held against shell/pot_plan.h.
add_test(NAME pot_round_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_pot_round.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python shell/test_pot_round.py`
Expected: `ModuleNotFoundError: No module named 'pot_round'`.

- [ ] **Step 3: Write the module**

Create `shell/pot_round.py`:

```python
"""Round four's host-side constants and arithmetic, in one module so
read_settle.py, read_wait.py and read_pots.py cannot disagree about a bound.

What lives here: the pot table (shell/pot_plan.h's twin -- test_pot_round.py
parses the header and fails when they drift), the two position gates PG1 and
PG2, the derived source impedance, and the parser for the two lines both
probes print in a SHELL_POT_ROUND=1 image (shell/pot_report.cpp).

Spec: ../docs/superpowers/specs/2026-09-28-coupon-pot-round-design.md
"""

# pot_plan.h's kPots, in order: (name, group, channel, hi_ch, lo_ch, r_track).
POTS = (("RV2", 0, 2, 1, 3, 10000),
        ("RV4", 0, 6, 5, 7, 20000),
        ("RV6", 1, 2, 1, 3, 10000))

RON_OHM = 150
FULL_SCALE = 65535

# PG1, spec section 7: inclusive, in counts, symmetric about 32767.5
# (+-4587.5), i.e. x ~ 0.43..0.57, where x(1-x) is at least 98 % of its peak.
PG1_LO = 28180
PG1_HI = 37355

# PG2, spec section 7: inclusive. 1024 counts is dx = 0.0156, which moves
# x(1-x) by at most 0.1 % around mid travel; a knocked pot moves thousands.
PG2_MAX = 1024

# settle_plan.cpp kSettlePotPlan: the pots' pairs start at P6, two per pot in
# POTS order -- from the high neighbour, then from the low one.
SETTLE_POT_PAIR0 = 6
# wait_plan.h wait_victim(): the pots are victims 5..7 in POTS order.
WAIT_POT_VICTIM0 = 5

_STR_FIELDS = ("name", "git")
_KEYS = {
    "SHELL_POT_CFG": {"round", "pots", "git"},
    "SHELL_POT_ID": {"idx", "name", "group", "ch", "r_track", "hi", "lo"},
}


def parse_pot_line(line):
    """("cfg", fields) or ("id", fields) for a SHELL_POT_* line, None for any
    other line. Raises ValueError on libDaisy's "$$" overflow marker, on a
    non-integer where an integer belongs, or on a wrong field set -- a line
    cut short without a marker parses as valid ints and simply lacks its
    tail, and only the key-set check turns that into a refusal."""
    line = line.strip()
    for tag, kind in (("SHELL_POT_CFG", "cfg"), ("SHELL_POT_ID", "id")):
        if line.startswith(tag + " "):
            if "$$" in line:
                raise ValueError("libDaisy overflow marker")
            out = {}
            for token in line[len(tag):].split():
                key, _, value = token.partition("=")
                out[key] = value if key in _STR_FIELDS else int(value)
            if set(out) != _KEYS[tag]:
                raise ValueError("%s carries the wrong field set" % tag)
            return kind, out
    return None


def ids_match(cfg, ids):
    """True iff a block's SHELL_POT_CFG and SHELL_POT_ID lines describe POTS
    exactly -- the image measured the pots this reader is about to name."""
    if cfg is None or cfg.get("round") != 1 or cfg.get("pots") != len(POTS):
        return False
    want = [{"idx": i, "name": n, "group": g, "ch": c, "r_track": r,
             "hi": h, "lo": l}
            for i, (n, g, c, h, l, r) in enumerate(POTS)]
    return sorted(ids, key=lambda d: d["idx"]) == want


def r_src(reading, r_track):
    """The wiper's source impedance at `reading`, DERIVED: x = reading / full
    scale, R_track * x * (1 - x) + Ron. R_track is nominal (spec section 3)."""
    x = reading / FULL_SCALE
    return r_track * x * (1.0 - x) + RON_OHM


def pg1(reading):
    return reading is not None and PG1_LO <= reading <= PG1_HI


def pg2(a, b):
    return a is not None and b is not None and abs(a - b) <= PG2_MAX


def sample_lines(git="deadbee"):
    """The two line kinds exactly as pot_report.cpp prints them, for the
    guards' fixtures. Transcribed from its format strings, not the spec."""
    out = ["SHELL_POT_CFG round=1 pots=%d git=%s" % (len(POTS), git)]
    for i, (n, g, c, h, l, r) in enumerate(POTS):
        out.append("SHELL_POT_ID idx=%d name=%s group=%d ch=%d r_track=%d "
                   "hi=%d lo=%d" % (i, n, g, c, r, h, l))
    return out
```

- [ ] **Step 4: Run it to verify it passes**

Run: `python shell/test_pot_round.py`
Expected: `pot_round guard: ok`.

- [ ] **Step 5: Prove two checks can go red**

(a) Edit `PG1_HI = 37355` to `PG1_HI = 37356`. Run the guard: FAIL on `A4` and `A6`. Restore with the Edit tool.
(b) Edit `POTS`' RV4 row to `("RV4", 0, 6, 5, 7, 10000)`. Run the guard: FAIL on `E2` (and `D3`). Restore. Re-run: ok.

- [ ] **Step 6: Commit**

```bash
git add shell/pot_round.py shell/test_pot_round.py CMakeLists.txt
git commit -m "feat(shell): pot_round.py, the pot round's gates and table on the host

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: read_settle.py reads a pot round

**Files:**
- Modify: `shell/read_settle.py` (imports; `_new_block` line 89-91; `_is_complete` line 109-125; `parse_block` line 128-169; `format_meta_csv` line 179-199; `main` before the gates block at line 268)
- Test: `shell/test_read_settle.py` (append before the final `if FAILURES:`)

**Interfaces:**
- Consumes: `pot_round` (Task 5).
- Produces: `read_settle.pot_readings(block) -> list[tuple[str, float | None]]` (empty for a block with no pot lines); `read_settle.pot_report(block) -> tuple[list[str], bool]`; metadata rows `pot_cfg,,<key>,<value>` and `pot_id,<idx>,<key>,<value>`.

- [ ] **Step 1: Write the failing guard cases**

Append to `shell/test_read_settle.py`, before the closing `if FAILURES:` block (add `import pot_round` and `import re` to the imports, and `pot_readings, pot_report` to the `from read_settle import ...` line):

```python
# --- 14: a pot-round block (spec 2026-09-28 sections 4, 6, 7) ---
def pot_block(tail=32768, drop_id=False):
    """build_block() at twelve pairs, the pot lines directly after _CFG, and
    the pot pairs' tail_ref set to `tail` -- the reading PG1 judges."""
    lines = build_block(num_pairs=12)
    pot = pot_round.sample_lines()
    if drop_id:
        pot = pot[:2] + pot[3:]
    lines[1:1] = pot
    out = []
    for line in lines:
        m = re.match(r"SHELL_SETTLE_REF pair=(\d+) tail_ref=100 ", line)
        if m and int(m.group(1)) >= 6:
            line = line.replace("tail_ref=100 ", "tail_ref=%d " % tail)
        out.append(line)
    return out


pb = parse_block(pot_block())
check("14a a twelve-pair pot block parses", pb is not None)
check("14b its pot lines are carried",
      pb is not None and pb["pot_cfg"]["pots"] == 3 and len(pb["pot_ids"]) == 3)
check("14c three readings, one per pot, from P6-P11's tail_ref",
      pb is not None and pot_readings(pb) == [("RV2", 32768.0),
                                              ("RV4", 32768.0),
                                              ("RV6", 32768.0)])
check("14d a pot block missing an ID line is refused",
      parse_block(pot_block(drop_id=True)) is None)
six_with_pots = build_block(num_pairs=6)
six_with_pots[1:1] = pot_round.sample_lines()
check("14e pot lines on a six-pair block are refused",
      parse_block(six_with_pots) is None)
check("14f a block with no pot lines has no readings",
      pot_readings(parse_block(build_block())) == [])
_, ok_mid = pot_report(pb)
_, ok_off = pot_report(parse_block(pot_block(tail=28179)))
_, ok_edge = pot_report(parse_block(pot_block(tail=28180)))
check("14g PG1 passes at mid travel", ok_mid)
check("14h PG1 refuses 28179", not ok_off)
check("14i PG1 admits 28180", ok_edge)
pmeta = {tuple(r.split(",")[:3]): r.split(",")[3]
         for r in format_meta_csv(pb).strip().split("\n")[1:]}
check("14j the metadata carries the pot lines",
      pmeta.get(("pot_cfg", "", "pots")) == "3"
      and pmeta.get(("pot_id", "1", "name")) == "RV4")
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python shell/test_read_settle.py`
Expected: `ImportError: cannot import name 'pot_readings'`.

- [ ] **Step 3: Implement**

In `shell/read_settle.py`:

Add `import pot_round` below `from collections import Counter`. Add one sentence to the module docstring's block-format paragraph: "In a SHELL_POT_ROUND=1 image, SHELL_POT_CFG and three SHELL_POT_ID lines follow SHELL_SETTLE_CFG directly, and the block carries twelve pairs."

`_new_block()` returns, additionally, `"pot_cfg": None, "pot_ids": []`.

In `_is_complete()`, directly after `num_pairs = ...`:

```python
    # A pot round (spec 2026-09-28): the pot lines must name exactly
    # pot_round.POTS, and the block must carry their six pairs. Pot lines on
    # a six-pair block, or twelve pairs whose ID lines lost one in transit,
    # are refused rather than read against the wrong pots.
    if block["pot_cfg"] is not None or block["pot_ids"]:
        if not pot_round.ids_match(block["pot_cfg"], block["pot_ids"]):
            return False
        if num_pairs != pot_round.SETTLE_POT_PAIR0 + 2 * len(pot_round.POTS):
            return False
```

In `parse_block()`, directly after the `elif block is None: continue` branch:

```python
            elif line.startswith("SHELL_POT_"):
                kind, fields = pot_round.parse_pot_line(line) or (None, None)
                if kind == "cfg":
                    block["pot_cfg"] = fields
                elif kind == "id":
                    block["pot_ids"].append(fields)
```

(`parse_pot_line` raises `ValueError` on a damaged line, which the existing `except (ValueError, KeyError)` turns into a dropped block.)

Add, after `format_csv()`:

```python
def pot_readings(block):
    """[(name, reading)] per pot, in POTS order: the mean of the pot's two
    pairs' tail_ref (SHELL_SETTLE_REF) -- the settled value the settle image
    measured it at. [] for a block with no pot lines. A reading is None when
    either pair's tail_ref is missing."""
    if block["pot_cfg"] is None:
        return []
    tail = {r["pair"]: r["tail_ref"] for r in block["refs"]}
    out = []
    for i, pot in enumerate(pot_round.POTS):
        a = tail.get(pot_round.SETTLE_POT_PAIR0 + 2 * i)
        b = tail.get(pot_round.SETTLE_POT_PAIR0 + 2 * i + 1)
        out.append((pot[0], None if a is None or b is None else (a + b) / 2.0))
    return out


def pot_report(block):
    """(lines, ok): one line per pot with its reading, x and derived R_src,
    and PG1's verdict. ok is True for a block with no pot lines."""
    lines, ok = [], True
    for (name, reading), pot in zip(pot_readings(block), pot_round.POTS):
        passed = pot_round.pg1(reading)
        ok = ok and passed
        if reading is None:
            lines.append("%s: no reading -- PG1 FAIL" % name)
            continue
        lines.append("%s: reading=%.1f x=%.4f r_src=%.0f ohm (R_track %d "
                     "nominal) PG1 %s [%d, %d]"
                     % (name, reading, reading / pot_round.FULL_SCALE,
                        pot_round.r_src(reading, pot[5]), pot[5],
                        "PASS" if passed else "FAIL",
                        pot_round.PG1_LO, pot_round.PG1_HI))
    return lines, ok
```

In `format_meta_csv()`, directly before `return "\n".join(rows) + "\n"`:

```python
    # Pot-round rows (spec 2026-09-28). %s, not %d: `name` and `git` are
    # strings.
    if block.get("pot_cfg") is not None:
        for name, value in block["pot_cfg"].items():
            rows.append("pot_cfg,,%s,%s" % (name, value))
        for entry in sorted(block["pot_ids"], key=lambda e: e["idx"]):
            for name, value in entry.items():
                if name != "idx":
                    rows.append("pot_id,%d,%s,%s" % (entry["idx"], name, value))
```

In `main()`, directly before the `# Not gates, and deliberately not folded into the exit code:` comment:

```python
    pot_lines, pot_ok = pot_report(block)
    for line in pot_lines:
        print(line, file=sys.stderr)
```

and change the final gate block so PG1 also refuses: after the existing `if not cal["gates_ok"]: ... return 1`, before `return 0`, add

```python
    if not pot_ok:
        print("PG1 FAILED -- a pot is outside its mid-travel window; set it "
              "again and re-run before reading anything from this block",
              file=sys.stderr)
        return 1
```

- [ ] **Step 4: Run it to verify it passes**

Run: `python shell/test_read_settle.py`
Expected: silent success (no `FAIL:` lines, exit 0) — cases 1–13 unchanged plus 14a–14j.

- [ ] **Step 5: Prove the completeness check can go red**

Edit `_is_complete()`: delete the `if num_pairs != ...: return False` pair of lines. Run the guard: FAIL on `14e`. Restore with the Edit tool; re-run: pass.

- [ ] **Step 6: Commit**

```bash
git add shell/read_settle.py shell/test_read_settle.py
git commit -m "feat(shell): read_settle.py reads a pot-round block, and PG1

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 7: read_wait.py reads a pot round

**Files:**
- Modify: `shell/read_wait.py` (imports; `VICTIMS`/`CASES` at line 53-54; `_new_block` line 153-156; `_is_complete` line 159-178; `parse_block` line 181-219; `report` line 296-365)
- Test: `shell/test_read_wait.py` (`build_block` line 40-88; new section before `# --- H.`)

**Interfaces:**
- Consumes: `pot_round` (Task 5).
- Produces: `read_wait.BASE_VICTIMS` (5), `read_wait.victims(block) -> int`, `read_wait.pot_readings(block) -> list[tuple[str, int | None]]`, `read_wait.format_meta_csv(block) -> str` (header `scope,pair,key,value`; scopes `cfg`, `clk`, `cal`, `span`, `gates`, `health`, `host`, `pot_cfg`, `pot_id`), and `report(block, out)` writing `out + ".meta.csv"` beside `out`. The `host` scope carries `g9_pass` (1/0), `g9_shift`, `pg1_pass` (1/0) — the verdicts this reader computes that the firmware cannot.

- [ ] **Step 1: Write the failing guard cases**

In `shell/test_read_wait.py`:

Add `import pot_round` to the imports, and `pot_readings, victims, format_meta_csv` to the `from read_wait import (...)` list.

Change `build_block`'s signature and its victim handling so a pot block can be built (the defaults keep every existing case identical):

```python
POT_VICTIMS = ((0, 2, 2650, 15), (0, 6, 5150, 25), (1, 2, 2650, 15))


def build_block(g9_shift=-852, gates_ok=1, sweep_dir=0, tie_shift=0,
                drop_point=None, dup_point=False, overflow=False, n_at=None,
                pots=False, pot_base=32768):
```

Inside it, after the initial `lines = [...]` list (CFG, CLK, CAL) and before `for arm in range(4):`, add — `lines[1:1]` puts the pot lines directly after `SHELL_WAIT_CFG`, where the firmware prints them:

```python
    if pots:
        lines[1:1] = pot_round.sample_lines()
    victims_used = VICTIMS + (POT_VICTIMS if pots else ())
    base = dict(BASE)
    if pots:
        base.update({(0, 2): pot_base, (0, 6): pot_base, (1, 2): pot_base})
```

and replace, in the rest of the function, `enumerate(VICTIMS)` → `enumerate(victims_used)`, `case = arm * 5 + v` → `case = arm * len(victims_used) + v`, `BASE[(g, ch)]` → `base[(g, ch)]` (both places), and the G5 loop's `for (g, ch, _, _) in VICTIMS:` → `for (g, ch, _, _) in victims_used:`.

Add, directly before `# --- H.`:

```python
# --- P. a pot-round block (spec 2026-09-28 sections 5, 6, 7) ---
pb = parse_block(build_block(pots=True))
check("P1 an eight-victim pot block parses", pb is not None)
check("P2 it counts eight victims and 32 cases",
      pb is not None and victims(pb) == 8 and len(pb["cases"]) == 32)
check("P3 the old five-victim block still counts five",
      victims(parse_block(build_block())) == 5)
check("P4 one reading per pot, from arm L at W=0",
      pb is not None and pot_readings(pb) == [("RV2", 32768), ("RV4", 32768),
                                              ("RV6", 32768)])
check("P5 a pot block exits 0 at mid travel", run_report(pb)[0] == 0)
check("P6 a pot at 37356 fails PG1 and the exit code",
      run_report(parse_block(build_block(pots=True, pot_base=37356)))[0] == 1)
check("P7 a pot at 37355 passes",
      run_report(parse_block(build_block(pots=True, pot_base=37355)))[0] == 0)
no_ids = [l for l in build_block(pots=True) if not l.startswith("SHELL_POT_ID idx=2")]
check("P8 a pot block missing an ID line is refused", parse_block(no_ids) is None)
meta = {tuple(r.split(",")[:3]): r.split(",")[3]
        for r in format_meta_csv(pb).strip().split("\n")[1:]}
check("P9 the metadata carries the gate, G9, PG1 and the pots",
      meta.get(("gates", "", "gates_ok")) == "1"
      and meta.get(("host", "", "g9_pass")) == "1"
      and meta.get(("host", "", "pg1_pass")) == "1"
      and meta.get(("pot_id", "2", "name")) == "RV6")
check("P10 a block with no pot lines has no readings and pg1_pass=1",
      pot_readings(parse_block(build_block())) == []
      and ("host", "", "pg1_pass") in
      {tuple(r.split(",")[:3]) for r in
       format_meta_csv(parse_block(build_block())).strip().split("\n")[1:]})
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python shell/test_read_wait.py`
Expected: `ImportError: cannot import name 'pot_readings'`.

- [ ] **Step 3: Implement**

In `shell/read_wait.py`:

Add `import pot_round` below `import time`. Add to the module docstring, after the exit-code paragraph: "In a SHELL_POT_ROUND=1 image the block carries eight victims -- round one's five, then RV2, RV4, RV6 -- and SHELL_POT_CFG plus three SHELL_POT_ID lines directly after SHELL_WAIT_CFG. PG1 (spec 2026-09-28 section 7) then joins the exit code. With an out path, a `out.csv.meta.csv` is written beside `out.csv`, in read_settle.py's `scope,pair,key,value` shape."

Replace

```python
VICTIMS = 5
CASES = 4 * VICTIMS
```

with

```python
# Round one's five victims. A pot-round block adds pot_round.POTS after them;
# victims(block) is the count a block actually carries.
BASE_VICTIMS = 5
```

`_new_block()`: add `"pot_cfg": None, "pot_ids": []` to `block.update(...)`.

Add, directly before `def _is_complete(block):`:

```python
def victims(block):
    return BASE_VICTIMS + (len(pot_round.POTS) if block["pot_cfg"] else 0)
```

In `_is_complete()`: at its top, after the `_SINGLE` loop, add

```python
    if block["pot_cfg"] is not None or block["pot_ids"]:
        if not pot_round.ids_match(block["pot_cfg"], block["pot_ids"]):
            return False
    n = victims(block)
```

and change `list(range(CASES))` → `list(range(4 * n))`, both `VICTIMS` in the G5 check → `n`, and `case // VICTIMS` → `case // n`.

In `parse_block()`'s `else:` chain, before `elif line.startswith("SHELL_WAIT_G5 "):`, add

```python
                elif line.startswith("SHELL_POT_"):
                    kind, fields = pot_round.parse_pot_line(line) or (None, None)
                    if kind == "cfg":
                        block["pot_cfg"] = fields
                    elif kind == "id":
                        block["pot_ids"].append(fields)
```

Add, after `tie_faults()`:

```python
def pot_readings(block):
    """[(name, reading)] per pot, in POTS order: arm L's W = 0 mean -- the
    387.5-cycle rung back to back, which round three measured flat at every
    wait. [] for a block with no pot lines; None where that point converted
    nothing."""
    if block["pot_cfg"] is None:
        return []
    out = []
    for name, g, ch, _, _, _ in pot_round.POTS:
        reading = None
        for case_id, c in block["cases"].items():
            if c["arm"] == ARM_LONG and _victim(c) == (g, ch):
                p = block["points"][case_id][0]
                reading = p["mean"] if p["n"] > 0 else None
        out.append((name, reading))
    return out


def _pg1_ok(block):
    return all(pot_round.pg1(r) for _, r in pot_readings(block))


def format_meta_csv(block):
    """Everything the block says that is not a grid point, plus the verdicts
    this reader computes, as `scope,pair,key,value` rows -- read_settle.py's
    shape, so read_pots.py reads both files the same way."""
    rows = ["scope,pair,key,value"]
    for scope in ("cfg", "clk", "cal", "span", "gates", "health"):
        for name, value in block[scope].items():
            rows.append("%s,,%s,%s" % (scope, name, value))
    passed, s = g9(block)
    rows.append("host,,g9_pass,%d" % (1 if passed else 0))
    rows.append("host,,g9_shift,%s" % ("" if s is None else s))
    rows.append("host,,pg1_pass,%d" % (1 if _pg1_ok(block) else 0))
    if block["pot_cfg"] is not None:
        for name, value in block["pot_cfg"].items():
            rows.append("pot_cfg,,%s,%s" % (name, value))
        for entry in sorted(block["pot_ids"], key=lambda e: e["idx"]):
            for name, value in entry.items():
                if name != "idx":
                    rows.append("pot_id,%d,%s,%s" % (entry["idx"], name, value))
    return "\n".join(rows) + "\n"
```

In `report()`: directly before `if out is not None:`, add

```python
    for (name, reading), pot in zip(pot_readings(block), pot_round.POTS):
        if reading is None:
            print("%s: no arm-L W=0 reading -- PG1 FAIL" % name, file=err)
        else:
            print("%s: reading=%d x=%.4f r_src=%.0f ohm (R_track %d nominal) "
                  "PG1 %s" % (name, reading, reading / pot_round.FULL_SCALE,
                              pot_round.r_src(reading, pot[5]), pot[5],
                              "PASS" if pot_round.pg1(reading) else "FAIL"),
                  file=err)
```

replace the `if out is not None:` block with

```python
    if out is not None:
        with open(out, "w", encoding="utf-8", newline="") as fh:
            fh.write(format_csv(block))
        with open(out + ".meta.csv", "w", encoding="utf-8", newline="") as fh:
            fh.write(format_meta_csv(block))
```

and after `if passed is not True: reasons.append("G9")` add

```python
    if not _pg1_ok(block):
        reasons.append("PG1 (a pot outside its mid-travel window)")
```

- [ ] **Step 4: Run it to verify it passes**

Run: `python shell/test_read_wait.py`
Expected: `read_wait guard: ok` — sections A–H unchanged (H parses the vendored board block as five victims), plus P1–P10.

- [ ] **Step 5: Prove the per-block victim count can go red**

Edit `victims()` to `return BASE_VICTIMS + len(pot_round.POTS)` (ignoring the block). Run the guard: FAIL on `A1` and `H1` among others — the old blocks stop parsing. Restore with the Edit tool; re-run: ok.

- [ ] **Step 6: Commit**

```bash
git add shell/read_wait.py shell/test_read_wait.py
git commit -m "feat(shell): read_wait.py reads a pot-round block, PG1, and a metadata file

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 8: read_pots.py, the joining reader

**Files:**
- Create: `shell/read_pots.py`
- Create: `shell/test_read_pots.py`
- Modify: `CMakeLists.txt` (after `pot_round_guard`)

**Interfaces:**
- Consumes: `pot_round` (Task 5); the settle metadata rows from Task 6 (`ref,<pair>,tail_ref`, `knee,<pair>,d_settle_ns|at_or_below_offset|predicted_ns`, `offset,<pair>,offset_ns`, `cal,,gates_ok`, `pot_cfg,,pots`); `wait.csv` columns `case,arm,victim_group,victim_ch,r_src,rung_tenths,codec,w_us,n,mean,min,max,shift` (existing); the wait metadata rows from Task 7 (`gates,,gates_ok`, `host,,g9_pass`, `host,,pg1_pass`, `pot_cfg,,pots`).
- Produces: CLI `python shell/read_pots.py settle.csv.meta.csv wait.csv` (reads `wait.csv.meta.csv` beside it); pure functions `settle_readings(meta)`, `wait_readings(rows)`, `ref_deltas(meta, rows)`, `settle_rows(meta)`, `wait_summary(rows)`, `verdict(settle_meta, wait_meta, rows) -> tuple[bool, list[str]]`.

- [ ] **Step 1: Write the failing guard**

Create `shell/test_read_pots.py`:

```python
"""Guard for read_pots.py: PG2 at its edge, the input gates, and the two
tables' arithmetic, on synthetic metadata and rows built in memory.

Runs as a plain script; pytest is not installed on this machine.
Spec: ../docs/superpowers/specs/2026-09-28-coupon-pot-round-design.md
"""
import sys

import read_pots as rp

FAILURES = []


def check(label, cond):
    if not cond:
        FAILURES.append(label)


def settle_meta(pot_tail=32768, gates_ok=1):
    """What read_settle.py's format_meta_csv() writes for a pot block,
    reduced to the rows read_pots.py reads. Keys are (scope, pair, key)."""
    m = {("cal", "", "gates_ok"): str(gates_ok),
         ("pot_cfg", "", "pots"): "3"}
    for p in range(12):
        tail = pot_tail if p >= 6 else {1: 32700, 2: 32710, 4: 32650}.get(p, 0)
        m[("ref", str(p), "tail_ref")] = str(tail)
        m[("offset", str(p), "offset_ns")] = "1154" if p in (1, 2, 4, 8, 9) else "991"
        m[("knee", str(p), "d_settle_ns")] = "3000" if p in (8, 9) else "1000"
        m[("knee", str(p), "at_or_below_offset")] = "0"
        m[("knee", str(p), "predicted_ns")] = "3016" if p in (8, 9) else "1552"
    return m


def wait_meta(gates_ok=1, g9_pass=1, pg1_pass=1):
    return {("gates", "", "gates_ok"): str(gates_ok),
            ("host", "", "g9_pass"): str(g9_pass),
            ("host", "", "pg1_pass"): str(pg1_pass),
            ("pot_cfg", "", "pots"): "3"}


def wait_rows(pot_mean=32768, ref_a_mean=32705, l_shift=2):
    """wait.csv rows for arms A (0), B (1) and L (2) on the pots and the
    three dividers, at the W points read_pots.py reads."""
    rows = []
    victims = {(0, 2): pot_mean, (0, 6): pot_mean, (1, 2): pot_mean,
               (0, 8): ref_a_mean, (1, 6): 32650, (0, 9): 32750}
    for arm in (0, 1, 2):
        for (g, ch), base in victims.items():
            for w in (0, 2000, 10000, 20000, 50000):
                if arm == 0:
                    shift = 0 if w == 0 else -(w // 20) if w < 10000 else -800
                elif arm == 1:
                    shift = 0 if w == 0 else -40
                else:
                    shift = 0 if w == 0 else l_shift
                rows.append({"arm": arm, "victim_group": g, "victim_ch": ch,
                             "w_us": w, "n": 64, "mean": base + shift,
                             "shift": shift})
    return rows


# --- A. PG2 at its edge ---
ok, why = rp.verdict(settle_meta(32768), wait_meta(), wait_rows(32768 + 1024))
check("A1 PG2 admits 1024 counts between the images", ok)
ok, why = rp.verdict(settle_meta(32768), wait_meta(), wait_rows(32768 + 1025))
check("A2 PG2 refuses 1025", not ok and any("PG2" in w for w in why))

# --- B. the inputs' own gates ---
check("B1 a failed settle gate refuses",
      not rp.verdict(settle_meta(gates_ok=0), wait_meta(), wait_rows())[0])
check("B2 a failed wait gate refuses",
      not rp.verdict(settle_meta(), wait_meta(gates_ok=0), wait_rows())[0])
check("B3 a failed G9 refuses",
      not rp.verdict(settle_meta(), wait_meta(g9_pass=0), wait_rows())[0])
check("B4 PG1 in the settle image refuses",
      not rp.verdict(settle_meta(28179), wait_meta(), wait_rows(28179))[0])
check("B5 a healthy pair of inputs passes",
      rp.verdict(settle_meta(), wait_meta(), wait_rows()) == (True, []))

# --- C. the arithmetic ---
check("C1 settle readings are the mean of each pot's two tail_refs",
      rp.settle_readings(settle_meta(33000)) == [("RV2", 33000.0),
                                                ("RV4", 33000.0),
                                                ("RV6", 33000.0)])
check("C2 wait readings are arm L at W=0",
      rp.wait_readings(wait_rows(33100)) == [("RV2", 33100), ("RV4", 33100),
                                             ("RV6", 33100)])
deltas = dict((n, d) for n, d in rp.ref_deltas(settle_meta(), wait_rows()))
check("C3 REF_A's instrument delta is wait minus the P1/P2 mean",
      deltas["REF_A"] == 32705 - 32705.0)
check("C4 REF_C's instrument delta is wait minus P4",
      deltas["REF_C"] == 32650 - 32650.0)
srows = {r["pair"]: r for r in rp.settle_rows(settle_meta())}
check("C5 true settle is knee plus offset",
      srows[8]["settle_ns"] == 3000 + 1154 and srows[6]["settle_ns"] == 1000 + 991)
check("C6 the ratio is against the prediction",
      abs(srows[8]["ratio"] - (4154 / 3016)) < 1e-9)
summary = dict((v["name"], v) for v in rp.wait_summary(wait_rows(l_shift=-4)))
check("C7 arm L's largest |shift| is reported per victim",
      summary["RV4"]["l_max_abs"] == 4)
check("C8 arm A's saturation is the mean of 10/20/50 ms",
      summary["REF_A"]["a_sat"] == -800)

if FAILURES:
    for f in FAILURES:
        print("FAIL: %s" % f, file=sys.stderr)
    raise SystemExit(1)
print("read_pots guard: ok")
```

Register it in `CMakeLists.txt`, directly after `pot_round_guard`:

```cmake
add_test(NAME read_pots_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_pots.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python shell/test_read_pots.py`
Expected: `ModuleNotFoundError: No module named 'read_pots'`.

- [ ] **Step 3: Write the reader**

Create `shell/read_pots.py`:

```python
"""Joins round four's two readers' files into the round's answer.

    python read_pots.py settle.csv.meta.csv wait.csv

reads `wait.csv.meta.csv` beside `wait.csv`. It computes nothing the two
readers already compute; it joins and compares (spec
2026-09-28-coupon-pot-round-design.md section 8):

  - per pot: its reading in each image, x and derived R_src; PG1 in both;
    PG2 between them, beside REF_A's and REF_C's own difference between the
    two images -- the instrument's share of any PG2 difference, which has
    never been measured on its own (spec section 7);
  - the settle table: true settle (knee + offset) per pot pair, the model's
    prediction and the ratio, with REF_A's P1/P2 beside RV4's P8/P9;
  - the wait table: per victim, arm A's shift at 2 ms and at saturation
    (mean of 10/20/50 ms), arm B's at saturation, arm L's largest |shift|.

Exit code: 1 when either input's own gates failed, G9 failed, PG1 failed in
either image, or PG2 failed. The tables are printed regardless -- a refused
run is read for what failed, not hidden.
"""
import csv
import sys

import pot_round

REF_A = (0, 8)
REF_C = (1, 6)
REF_B = (0, 9)
ARM_WAIT, ARM_DISCARD, ARM_LONG = 0, 1, 2
SAT_W_US = (10000, 20000, 50000)


def load_meta(path):
    with open(path, encoding="utf-8", newline="") as fh:
        return {(r["scope"], r["pair"], r["key"]): r["value"]
                for r in csv.DictReader(fh)}


def load_rows(path):
    with open(path, encoding="utf-8", newline="") as fh:
        rows = []
        for r in csv.DictReader(fh):
            rows.append({"arm": int(r["arm"]),
                         "victim_group": int(r["victim_group"]),
                         "victim_ch": int(r["victim_ch"]),
                         "w_us": int(r["w_us"]), "n": int(r["n"]),
                         "mean": int(r["mean"]),
                         "shift": None if r["shift"] == "" else int(r["shift"])})
        return rows


def _int(meta, scope, pair, key):
    v = meta.get((scope, str(pair), key))
    return None if v is None or v == "" else int(v)


def _tail(meta, pair):
    return _int(meta, "ref", pair, "tail_ref")


def settle_readings(meta):
    out = []
    for i, pot in enumerate(pot_round.POTS):
        a = _tail(meta, pot_round.SETTLE_POT_PAIR0 + 2 * i)
        b = _tail(meta, pot_round.SETTLE_POT_PAIR0 + 2 * i + 1)
        out.append((pot[0], None if a is None or b is None else (a + b) / 2.0))
    return out


def _row(rows, arm, victim, w_us):
    for r in rows:
        if (r["arm"] == arm and (r["victim_group"], r["victim_ch"]) == victim
                and r["w_us"] == w_us):
            return r
    return None


def wait_readings(rows):
    out = []
    for name, g, ch, _, _, _ in pot_round.POTS:
        r = _row(rows, ARM_LONG, (g, ch), 0)
        out.append((name, r["mean"] if r is not None and r["n"] > 0 else None))
    return out


def ref_deltas(meta, rows):
    """[(name, wait - settle)] for REF_A and REF_C: how far the two images'
    conditions alone move a divider's reading."""
    out = []
    a1, a2 = _tail(meta, 1), _tail(meta, 2)
    settle = {"REF_A": None if a1 is None or a2 is None else (a1 + a2) / 2.0,
              "REF_C": None if _tail(meta, 4) is None else float(_tail(meta, 4))}
    for name, victim in (("REF_A", REF_A), ("REF_C", REF_C)):
        r = _row(rows, ARM_LONG, victim, 0)
        s = settle[name]
        out.append((name, None if r is None or s is None else r["mean"] - s))
    return out


def settle_rows(meta):
    """One dict per pair of interest: the pot pairs P6-P11 and REF_A's P1/P2."""
    out = []
    for pair in (1, 2) + tuple(range(6, 12)):
        d = _int(meta, "knee", pair, "d_settle_ns")
        off = _int(meta, "offset", pair, "offset_ns")
        pred = _int(meta, "knee", pair, "predicted_ns")
        below = _int(meta, "knee", pair, "at_or_below_offset")
        settle = d + off if d is not None and d >= 0 and off is not None else None
        out.append({"pair": pair, "settle_ns": settle, "predicted_ns": pred,
                    "at_or_below_offset": below, "offset_ns": off,
                    "ratio": None if settle is None or not pred else settle / pred})
    return out


def wait_summary(rows):
    names = [(p[0], (p[1], p[2])) for p in pot_round.POTS]
    names += [("REF_A", REF_A), ("REF_C", REF_C), ("REF_B", REF_B)]
    out = []
    for name, victim in names:
        def shift(arm, w):
            r = _row(rows, arm, victim, w)
            return None if r is None else r["shift"]

        def sat(arm):
            vals = [shift(arm, w) for w in SAT_W_US]
            return None if None in vals else sum(vals) / len(vals)

        l_vals = [abs(r["shift"]) for r in rows
                  if r["arm"] == ARM_LONG and r["shift"] is not None
                  and (r["victim_group"], r["victim_ch"]) == victim]
        out.append({"name": name, "a_2ms": shift(ARM_WAIT, 2000),
                    "a_sat": sat(ARM_WAIT), "b_sat": sat(ARM_DISCARD),
                    "l_max_abs": max(l_vals) if l_vals else None})
    return out


def verdict(settle_meta, wait_meta, rows):
    reasons = []
    if settle_meta.get(("cal", "", "gates_ok")) != "1":
        reasons.append("the settle image's gates failed")
    if wait_meta.get(("gates", "", "gates_ok")) != "1":
        reasons.append("the wait image's gates failed")
    if wait_meta.get(("host", "", "g9_pass")) != "1":
        reasons.append("G9 failed in the wait image")
    if settle_meta.get(("pot_cfg", "", "pots")) != str(len(pot_round.POTS)) \
            or wait_meta.get(("pot_cfg", "", "pots")) != str(len(pot_round.POTS)):
        reasons.append("an input is not a pot-round block")
    s_read = dict(settle_readings(settle_meta))
    w_read = dict(wait_readings(rows))
    for name, _, _, _, _, _ in pot_round.POTS:
        if not pot_round.pg1(s_read.get(name)):
            reasons.append("PG1 failed for %s in the settle image" % name)
        if not pot_round.pg1(w_read.get(name)):
            reasons.append("PG1 failed for %s in the wait image" % name)
        if not pot_round.pg2(s_read.get(name), w_read.get(name)):
            reasons.append("PG2 failed for %s (moved between the images?)" % name)
    return (not reasons), reasons


def _fmt(v, spec="%s"):
    return "-" if v is None else spec % v


def main(argv):
    if len(argv) != 3:
        raise SystemExit("usage: read_pots.py settle.csv.meta.csv wait.csv")
    settle_meta = load_meta(argv[1])
    rows = load_rows(argv[2])
    wait_meta = load_meta(argv[2] + ".meta.csv")
    err = sys.stderr

    print("pots -- reading, x, derived R_src (R_track nominal):", file=err)
    s_read, w_read = dict(settle_readings(settle_meta)), dict(wait_readings(rows))
    for name, _, _, _, _, r_track in pot_round.POTS:
        for label, reading in (("settle", s_read.get(name)),
                               ("wait", w_read.get(name))):
            print("  %s %-6s reading=%s x=%s r_src=%s PG1 %s"
                  % (name, label, _fmt(reading, "%.1f"),
                     _fmt(None if reading is None else
                          reading / pot_round.FULL_SCALE, "%.4f"),
                     _fmt(None if reading is None else
                          pot_round.r_src(reading, r_track), "%.0f"),
                     "PASS" if pot_round.pg1(reading) else "FAIL"), file=err)
        a, b = s_read.get(name), w_read.get(name)
        print("  %s PG2 |wait - settle| = %s (bound %d) %s"
              % (name, _fmt(None if a is None or b is None else abs(b - a), "%.1f"),
                 pot_round.PG2_MAX,
                 "PASS" if pot_round.pg2(a, b) else "FAIL"), file=err)
    for name, d in ref_deltas(settle_meta, rows):
        print("  %s wait - settle = %s (the instrument's own share)"
              % (name, _fmt(d, "%.1f")), file=err)

    print("\nsettle -- true settle = knee + offset:", file=err)
    for r in settle_rows(settle_meta):
        tag = " (at or below offset %s ns)" % r["offset_ns"] \
            if r["settle_ns"] is None and r["at_or_below_offset"] else ""
        print("  P%-2d settle_ns=%s predicted_ns=%s ratio=%s%s"
              % (r["pair"], _fmt(r["settle_ns"]), _fmt(r["predicted_ns"]),
                 _fmt(r["ratio"], "%.2f"), tag), file=err)

    print("\nwait -- shifts in counts:", file=err)
    print("  %-6s %8s %8s %8s %8s" % ("", "A 2ms", "A sat", "B sat", "L max"),
          file=err)
    for v in wait_summary(rows):
        print("  %-6s %8s %8s %8s %8s"
              % (v["name"], _fmt(v["a_2ms"]), _fmt(v["a_sat"], "%.1f"),
                 _fmt(v["b_sat"], "%.1f"), _fmt(v["l_max_abs"])), file=err)

    ok, reasons = verdict(settle_meta, wait_meta, rows)
    if not ok:
        print("\nREFUSED: %s" % "; ".join(reasons), file=err)
        return 1
    print("\nall gates, PG1 and PG2 pass", file=err)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
```

- [ ] **Step 4: Run it to verify it passes**

Run: `python shell/test_read_pots.py`
Expected: `read_pots guard: ok`.

- [ ] **Step 5: Prove PG2's edge can go red**

Edit `pot_round.py`'s `pg2()` to use `< PG2_MAX` instead of `<= PG2_MAX`. Run `python shell/test_read_pots.py`: FAIL on `A1`. Run `python shell/test_pot_round.py`: FAIL on `B1`. Restore with the Edit tool; both guards: ok.

- [ ] **Step 6: Commit**

```bash
git add shell/read_pots.py shell/test_read_pots.py CMakeLists.txt
git commit -m "feat(shell): read_pots.py joins the settle and wait images, PG2

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 9: Full verification and the status entry

**Files:**
- Modify: `docs/roadmap.md` (the "Last updated" bullet at line 39, and the M6 section's dated entries — add one above the `**2026-09-27 — the next coupon instrument is built` entry)

- [ ] **Step 1: The whole host suite, freshly built**

Run: `source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build --output-on-failure`
Expected: every test passes, including `spky_tests`, `read_settle_guard`, `read_wait_guard`, `pot_round_guard`, `read_pots_guard`. Count the tests in the summary line and record it for the commit message. Do not trust an implementer's report of this — run it.

- [ ] **Step 2: The two images the board session flashes, once more**

Build `SHELL_COUPON_PROBE=1 SHELL_SETTLE_PROBE=1 SHELL_POT_ROUND=1` and `SHELL_COUPON_PROBE=1 SHELL_WAIT_PROBE=1 SHELL_POT_ROUND=1` (firmware shell, Task 4 Step 6's command), confirm both succeed, then rebuild the default image (`make -C shell -j8 images`).

- [ ] **Step 3: The roadmap entry**

In `docs/roadmap.md`, add directly above `**2026-09-27 — the next coupon instrument is built, and it waits on a board`:

```markdown
**2026-09-28 — round four, the pot round, is built and waits on a board
session.** The pots have been on the coupon since 2026-09-17 and have had one
reading, a wiring check at a stop. `SHELL_POT_ROUND=1` adds RV2, RV4 and RV6
at mid travel to both existing probes — six pairs in the settle probe (P6–P11,
from each pot's high and low neighbour), three victims in the wait probe — with
every divider kept as a same-boot control and G3 still judged on P0–P5 only.
The pots are set by hand to ~32768 on the bring-up scan; PG1 (reading in
[28180, 37355]) and PG2 (≤ 1024 counts between the two images) refuse a run
whose pot was off-centre or moved. `shell/read_pots.py` joins the two images'
files into the round's tables. Spec
`docs/superpowers/specs/2026-09-28-coupon-pot-round-design.md`, plan beside
it. Built and host-tested; nothing it predicts is measured. It closes
Phase-0 Task 6 step 5b on real pots once the board has spoken.
```

and in the "Last updated" bullet, change `2026-09-27, evening (**round three is measured**:` to `2026-09-28 (**round four, the pot round, is built** — see the M6 entry of that date; earlier, 2026-09-27, evening: **round three is measured**:`, and close the added parenthesis at the end of the round-three sentence it wraps (after "the evidence favours the rung."). Read the bullet's first ten lines before editing and keep its existing structure.

- [ ] **Step 4: Commit**

```bash
git add docs/roadmap.md
git commit -m "docs(roadmap): round four, the pot round, is built

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## After the plan: the board session

Not a task — it needs Bastian at the bench. Spec §10 carries the four steps (set the pots on the bring-up scan, two settle blocks, two wait blocks, `read_pots.py`), and spec §10's closing paragraph says which blocks get vendored under `shell/testdata/`. The write-up is `docs/hardware/pots-measured.md`, in the style of the three earlier rounds.
