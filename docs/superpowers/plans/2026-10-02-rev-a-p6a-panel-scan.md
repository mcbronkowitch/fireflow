# Rev A P6a Panel Scan Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The shell firmware scans the Rev A panel over P2's real pin map: ten 4051s on four sense pins, one mux per pin per step, a 40-bit chain, four keys on the 165, and a live calibration span. A generated table sends the 35 pots whose VCV law equals `apply_param()`'s.

**Architecture:**
- `shell/mux_plan` gains a second step model, *parallel*, next to the measured *sequential* one. The coupon probes keep `kCouponChain` unchanged; the coupon play image rehearses Rev A's model on `kCouponPlayChain`.
- `shell/gen_panel_map.py` turns `panel-map.json`, `blocks.py` and `param_table.h` into a committed header `shell/generated_panel_map.h`. Its guard proves every input check can go red.
- Keys and the span are host-tested units (`keys.h`, `scan_value.h`). `mux_scan`/`panel_scan` wire them into the play images.

**Tech Stack:**
- C++17. Host: clang + Ninja, doctest. Firmware: ARM GCC via `make`, libDaisy.
- Python 3 for the generator and its guard, run as plain scripts (pytest is not installed).

**Spec:** `docs/superpowers/specs/2026-10-02-rev-a-p6a-panel-scan-design.md`

## Global Constraints

**Language and commits**
- Everything written into the repo is English: code, comments, docs, commit messages.
- Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.

**Shell rules (verbatim, every dispatch)**
1. No file writes via the shell: no `sed -i`, no `>`, no `>>`, no `tee`. Write files with the editor tools; scripts in this plan write their own outputs.
2. Never `cd`.
3. No write command behind `&&` or `;`.
4. Use repo-relative paths.

**Tool prefixes**
- `HOSTENV` = `PATH="/c/Program Files/LLVM/bin:/c/Users/bernd/AppData/Roaming/Python/Python314/Scripts:$PATH" CC=clang CXX=clang++ CMAKE_GENERATOR=Ninja`
  - Write it in front of `cmake`/`ctest`, because `env.sh` cannot be sourced across tool calls.
- `ARMENV` = `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"`
  - Write it in front of every firmware `make`.
  - Never mix it with `HOSTENV` in one command.

**Host build**
- Configure once: `HOSTENV cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`. Release is mandatory.
- Every test run is `HOSTENV cmake --build build`, then (a separate command) `HOSTENV ctest --test-dir build ... --output-on-failure`. ctest does not build.

**Worktree setup** (once, before Task 1)
- Copy `env.sh` from the main checkout. It is gitignored.
- `git submodule update --init lib/libDaisy lib/DaisySP`
- `ARMENV make -C lib/libDaisy -j8`
- `ARMENV make -C lib/DaisySP -j8`

**Firmware images** are built with `ARMENV make -C shell -j8 images <switches>`. Never `all`.
- Coupon play image: `SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1`
- Rev A play image: `SHELL_PANEL_SCAN=1`
- The linker prints a `Memory region` table; its `SRAM_EXEC` row is the code-space number.

**Facts the spec fixes (exact values)**

| Item | Value |
|---|---|
| Rev A chain | 40 bits: address 0–2, enables 3–12, LEDs 13–31, spare 32–39 |
| Muxes | 10 × 8 channels, sense pins 3/3/2/2 → 24 steps |
| Keys | REC_A, REC_B, MODBTN, SHIFTBTN on 165 D0..D3 → return bits 7, 6, 5, 4; active low |
| Debounce | 3 equal reads |
| Span | valid iff rail ≥ `kRailFloor` (58982) and zero ≤ `kRailMargin` (1311) |
| Hysteresis | H = `kPotHysteresis` = 16 |
| Safe pots | 35 of 70 (spec §2.3) |

**Code-space gate** (spec §6)
- Both play images must link.
- Record `SRAM_EXEC` for each, before and after.
- If an image does not fit, apply only the fallback in Task 4 Step 9. Anything further is **STOP** and report the numbers.

**Never commit** `hardware/reva/bom-hand.csv`, `bom-jlc.csv`, `review.md`, `kicad/fp-lib-table`, `kicad/sym-lib-table`, or anything under `hardware/reva/out/` or `shell/build/`. Stage files by name, never `git add -A`.

**Kept unchanged (regression-pinned)**
- `kCouponChain`: values and patterns.
- `kPanelChain`: `SHELL_MUX_PROBE`'s measured profile.
- The coupon play image's existing `SHELL_PLAY` line format.

## Review Focus

1. **SENSE_2/SENSE_3 in steps 16–23.** Their muxes are all disabled, so the node floats. Nothing may be stored or sent from them. Pinned by Task 1's "exhausted sense pin" test; `panel_scan` asks `group_at()`.
2. **Boot, before the first calibrated sweep.** The span starts invalid, and `pot_filter()` refuses an invalid span, so no knob value reaches the engine. Pinned by Task 2's "invalid span emits nothing" test.
3. **A key held while the board powers up.** It reads as one press after the debounce, never as a stream of presses. Pinned by Task 2's "held from the first read" test.
4. **Address bits leaking into the enable field.** Rev A has 3 address lines directly below EN0; a 4-bit mask would switch EN0 with every odd address above 7. Pinned by Task 1's address-mask test.
5. **A calibration or spare channel mistaken for a pot.** No table row may sit on CAL_GND, CAL_3V3 or a spare input. Pinned by Task 3's doctest and the generator's 80-input accounting.

---

### Task 1: The parallel step model

**Files:**
- Modify: `shell/mux_plan.h` (whole file shown below)
- Modify: `shell/mux_plan.cpp` (whole file shown below)
- Test: `tests/test_mux_plan.cpp` (append)

**Interfaces:**
- Consumes: nothing new.
- Produces (later tasks rely on these exact names):
  - `inline constexpr int kMaxGroups = 10;`
  - `struct ChainProfile` gains two trailing members: `int addr_bits = 4; bool parallel_sense = false;`
  - `struct StepPattern { uint8_t address; uint16_t enable_mask; };`
  - `struct MuxChannel { int group; int ch; };`
  - `constexpr int sense_channels(const ChainProfile&, int sense);`
  - `int group_at(const ChainProfile&, int step, int sense);` — the group the scan reads on that sense pin at that step, or −1.
  - `int channel_at(const ChainProfile&, int step, int sense);` — its channel, or −1.
  - `uint64_t chain_word(const ChainProfile&, StepPattern, uint32_t leds);`
  - `inline constexpr ChainProfile kCouponPlayChain`.
  - `group_of_step()` returns −1 on parallel profiles.
  - `sense_live()` is `group_at() >= 0`.
  - `step_of()` handles both models.

- [ ] **Step 1: Record the code-space baseline**

Build both play images on the unchanged tree. Copy each image's `SRAM_EXEC` row verbatim into the task report; Task 4 compares against it.

```bash
ARMENV make -C shell -j8 images SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1
```
```bash
ARMENV make -C shell -j8 images SHELL_PANEL_SCAN=1
```
Expected: both link. The coupon play row reads about 98.8 % (roadmap, "Carried into part 2"). If either fails to link at baseline: STOP and report.

- [ ] **Step 2: Write the failing tests**

Append to `tests/test_mux_plan.cpp`. Add `#include <utility>` beside the existing includes.

```cpp
namespace {
// A Rev A-shaped profile written out by hand from P2 sections 3 and 4: ten
// 8-channel muxes, 3/3/2/2 on four sense pins, a 40-bit chain with three
// address lines, ten enables and nineteen LEDs. Task 3 holds the generated
// kRevaChain to these same numbers.
constexpr shell::ChainProfile kRevaShape{
    4, shell::kSenseAdcBase, 10,
    {8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
    {0, 0, 0, 1, 1, 1, 2, 2, 3, 3},
    40, 0, 3, 13, 19, -1, 3, true};

const std::vector<shell::ChainProfile> kParallelProfiles
    = {kRevaShape, shell::kCouponPlayChain};

int total_channels(const shell::ChainProfile& p)
{
    int n = 0;
    for(int g = 0; g < p.groups; ++g) n += p.channels[g];
    return n;
}
}

TEST_CASE("parallel: a group is enabled exactly when its sense pin reads it") {
    // P2 section 3: one mux per sense pin per step. Two muxes of one pin on
    // at once short two wipers together; a mux on with nothing reading it is
    // harmless but means the model and the chain disagree.
    for(const auto& p : kParallelProfiles)
        for(int step = 0; step < shell::scan_steps(p); ++step)
        {
            const shell::StepPattern sp = shell::step_pattern(p, step);
            for(int g = 0; g < p.groups; ++g)
            {
                const bool on = ((sp.enable_mask >> g) & 1u) == 0u;
                CAPTURE(step);
                CAPTURE(g);
                CHECK(on == (shell::group_at(p, step, p.sense_of_group[g]) == g));
            }
        }
}

TEST_CASE("parallel: every enabled group sees the step's shared address") {
    // The address lines are common to every mux, so a step is only coherent
    // if each sense pin's channel IS the address on the chain.
    for(const auto& p : kParallelProfiles)
        for(int step = 0; step < shell::scan_steps(p); ++step)
        {
            const shell::StepPattern sp = shell::step_pattern(p, step);
            for(int s = 0; s < p.sense_pins; ++s)
                if(shell::group_at(p, step, s) >= 0)
                    CHECK(shell::channel_at(p, step, s) == sp.address);
        }
}

TEST_CASE("parallel: every (group, channel) is read exactly once per sweep") {
    for(const auto& p : kParallelProfiles)
    {
        std::set<std::pair<int, int>> seen;
        int reads = 0;
        for(int step = 0; step < shell::scan_steps(p); ++step)
            for(int s = 0; s < p.sense_pins; ++s)
            {
                const int g = shell::group_at(p, step, s);
                if(g < 0) continue;
                ++reads;
                seen.insert({g, shell::channel_at(p, step, s)});
            }
        CHECK(reads == total_channels(p));
        CHECK(static_cast<int>(seen.size()) == total_channels(p));
    }
}

TEST_CASE("parallel: a sense pin whose channels are exhausted is fully off") {
    // Its node floats then. Storing it would put noise under a real
    // channel's index (Review Focus 1).
    for(int step = 16; step < 24; ++step)
    {
        const shell::StepPattern sp = shell::step_pattern(kRevaShape, step);
        for(int g = 6; g < 10; ++g) CHECK(((sp.enable_mask >> g) & 1u) == 1u);
        CHECK_FALSE(shell::sense_live(kRevaShape, step, 2));
        CHECK_FALSE(shell::sense_live(kRevaShape, step, 3));
        CHECK(shell::sense_live(kRevaShape, step, 0));
        CHECK(shell::sense_live(kRevaShape, step, 1));
    }
    for(int step = 8; step < 16; ++step)
    {
        CHECK_FALSE(shell::sense_live(shell::kCouponPlayChain, step, 1));
        CHECK(shell::step_pattern(shell::kCouponPlayChain, step).enable_mask == 0x2u);
    }
}

TEST_CASE("parallel: step_of finds each channel where the scan reads it") {
    for(const auto& p : kParallelProfiles)
        for(int g = 0; g < p.groups; ++g)
            for(int ch = 0; ch < p.channels[g]; ++ch)
            {
                const int s = shell::step_of(p, g, ch);
                REQUIRE(s >= 0);
                CHECK(shell::group_at(p, s, p.sense_of_group[g]) == g);
                CHECK(shell::channel_at(p, s, p.sense_of_group[g]) == ch);
            }
}

TEST_CASE("parallel: step counts") {
    CHECK(shell::scan_steps(kRevaShape) == 24);
    CHECK(shell::mux_total(kRevaShape) == 96);
    CHECK(shell::sense_channels(kRevaShape, 0) == 24);
    CHECK(shell::sense_channels(kRevaShape, 3) == 16);
    CHECK(shell::scan_steps(shell::kCouponPlayChain) == 16);
    CHECK(shell::mux_total(shell::kCouponPlayChain) == 32);
}

TEST_CASE("parallel: steps that do not exist enable nothing and read nothing") {
    for(const auto& p : kParallelProfiles)
        for(int step : {-1, shell::scan_steps(p), shell::scan_steps(p) + 5})
        {
            const shell::StepPattern sp = shell::step_pattern(p, step);
            CHECK(sp.enable_mask == static_cast<uint16_t>((1u << p.groups) - 1u));
            for(int s = 0; s < p.sense_pins; ++s)
            {
                CHECK(shell::group_at(p, step, s) == -1);
                CHECK(shell::channel_at(p, step, s) == -1);
                CHECK_FALSE(shell::sense_live(p, step, s));
            }
            CHECK(shell::group_of_step(p, 0) == -1);   // sequential only
        }
}

TEST_CASE("chain word: Rev A's address cannot reach the enable field") {
    // Three address lines sit directly below EN0 (Review Focus 4). With
    // every enable ON (mask 0), an unmasked 4-bit address 0x0F would set
    // bit 3 and switch mux 0 off.
    const uint64_t w = shell::chain_word(kRevaShape, shell::StepPattern{0x0F, 0}, 0u);
    CHECK((w & 0x7u) == 0x7u);
    CHECK(((w >> 3) & 0x3FFu) == 0u);
}

TEST_CASE("chain word: a 40-bit word puts LEDs at 13..31 and nothing above") {
    const uint64_t lit = shell::chain_word(kRevaShape, shell::StepPattern{0, 0}, 0xFFFFFFFFu);
    CHECK(((lit >> 13) & 0x7FFFFu) == 0x7FFFFu);
    CHECK((lit & 0x1FFFu) == 0u);
    CHECK((lit >> 32) == 0u);
    const uint64_t off = shell::chain_word(kRevaShape, shell::step_pattern(kRevaShape, -1), 0u);
    CHECK(off == (uint64_t{0x3FF} << 3));
}

TEST_CASE("coupon chain: words are bit for bit what part 1 clocked") {
    // Every coupon probe was measured on these words. Recomputed with part
    // 1's formula, independent of chain_word(): 4 address bits, 2 enables at
    // 4, 8 LEDs at 6.
    for(int step = 0; step < 24; ++step)
        for(uint32_t leds : {0u, 0xA5u, 0xFFu})
        {
            const shell::StepPattern sp = shell::step_pattern(shell::kCouponChain, step);
            const uint64_t legacy = (uint64_t{sp.address} & 0x0Fu)
                                    | ((uint64_t{sp.enable_mask} & 0x3u) << 4)
                                    | ((uint64_t{leds} & 0xFFu) << 6);
            CAPTURE(step);
            CHECK(shell::chain_word(shell::kCouponChain, sp, leds) == legacy);
        }
    CHECK(shell::step_pattern(shell::kCouponChain, 0).enable_mask == 0x2u);
    CHECK(shell::step_pattern(shell::kCouponChain, 16).enable_mask == 0x1u);
    CHECK(shell::step_pattern(shell::kCouponChain, 23).address == 7);
    CHECK_FALSE(shell::kCouponChain.parallel_sense);
    CHECK(shell::kCouponChain.addr_bits == 4);
}

TEST_CASE("coupon play chain: the coupon's wiring, run in parallel") {
    const shell::ChainProfile& c = shell::kCouponChain;
    const shell::ChainProfile& p = shell::kCouponPlayChain;
    CHECK(p.parallel_sense);
    CHECK(p.sense_pins == c.sense_pins);
    CHECK(p.groups == c.groups);
    CHECK(p.channels[0] == c.channels[0]);
    CHECK(p.channels[1] == c.channels[1]);
    CHECK(p.sense_of_group[0] == c.sense_of_group[0]);
    CHECK(p.sense_of_group[1] == c.sense_of_group[1]);
    CHECK(p.chain_bits == c.chain_bits);
    CHECK(p.addr_shift == c.addr_shift);
    CHECK(p.enable_shift == c.enable_shift);
    CHECK(p.led_shift == c.led_shift);
    CHECK(p.led_bits == c.led_bits);
    CHECK(p.button_bit == c.button_bit);
    CHECK(p.addr_bits == c.addr_bits);
    for(int step = 0; step < 8; ++step)
    {
        CHECK(shell::step_pattern(p, step).enable_mask == 0x0u);   // both on
        CHECK(shell::step_pattern(p, step).address == step);
    }
    for(int step = 8; step < 16; ++step)
        CHECK(shell::step_pattern(p, step).address == step);       // the 4067's upper half
}
```

- [ ] **Step 3: Run the tests to verify they fail**

```bash
HOSTENV cmake --build build
```
Expected: compile errors naming `kCouponPlayChain`, `group_at`, `channel_at`, `sense_channels`, and the excess initializers in `kRevaShape`.

- [ ] **Step 4: Replace `shell/mux_plan.h`**

```cpp
#pragma once

// The write side of the panel scan, with no hardware type in it -- same
// arrangement as controls.h and for the same reason: this is where a wrong
// address pattern is one visible line instead of a knob that misbehaves on a
// board.
//
// Two step models (spec 2026-10-02-rev-a-p6a-panel-scan-design.md section 3.1):
//
//   sequential -- one group (chip) is enabled per step, and the steps walk
//                 group 0's channels, then group 1's. Every coupon probe was
//                 measured this way (kCouponChain), and SHELL_MUX_PROBE priced
//                 its CPU cost on kPanelChain.
//   parallel   -- each sense pin owns the groups wired to it, in group order,
//                 and at step k every sense pin enables the group holding its
//                 k-th channel. The address lines are shared by every chip, so
//                 every enabled group sees the same address. A sense pin whose
//                 channels are exhausted has all of its groups disabled. This
//                 is Rev A (P2 section 3: one mux per sense pin per step).
#include <cstdint>

namespace shell {

// The first of the raw ADC pins, as an index into libDaisy's patch_sm
// channel enum (CV_1..CV_8 = 0..7, then ADC_9 = 8). It is a number here and
// not the enum constant because this header may not include a hardware
// header -- mux_scan.cpp static_asserts the two against each other.
inline constexpr int kSenseAdcBase = 8;

// Rev A has ten muxes, each with its own enable (P2 section 3).
inline constexpr int kMaxGroups = 10;

// One board's chain, as data. This is a value and not a set of #defines so
// that the host test can run the same assertions against every profile -- a
// wrong address pattern is a line here and a knob that misbehaves on a board
// there.
struct ChainProfile
{
    int sense_pins;        // raw ADC pins this board populates
    int sense_adc_base;    // index of the first of them in patch_sm's enum
    int groups;            // enable lines, one per group
    int channels[kMaxGroups];        // channels on that group's chip
    int sense_of_group[kMaxGroups];  // sense pin carrying it, -1 = all of them
                                     // (sequential profiles only)
    int chain_bits;        // bits clocked per step; the bit-bang cost scales
    int addr_shift;
    int enable_shift;
    int led_shift;
    int led_bits;
    int button_bit;        // index into the bits shifted out of the 165, -1 = none
    int  addr_bits      = 4;      // address lines on the chain
    bool parallel_sense = false;  // the step model, see the top of this file
};

// The pre-P2 panel draft: four 74HC595 = 32 bits, 19 LEDs, four address
// lines, two 16-channel groups on all four sense pins. It is no longer the
// panel -- Rev A is kRevaChain in generated_panel_map.h -- but it is the
// profile SHELL_MUX_PROBE's CPU cost was measured against
// (docs/bench/2026-08-23-978cbaf-shell-mux-placement.md), and images with
// neither SHELL_COUPON_PROBE nor SHELL_PANEL_SCAN keep it.
inline constexpr ChainProfile kPanelChain{
    4, kSenseAdcBase, 2, {16, 16}, {-1, -1}, 32, 0, 4, 8, 19, -1};

// The test coupon (hardware/coupon/). Two 74HC595 = 16 bits, eight LEDs, one
// CD74HC4067 on ADC_9 and one CD74HC4051 on ADC_10 -- so the two groups do
// NOT have the same channel count, and each sits on its own sense pin.
// Derivation of the bit order: netlist.py:267 plus MSB-first clocking
// through U_SR1.QH' -> U_SR2.SER. Sequential: every coupon probe was
// measured with exactly these patterns.
inline constexpr ChainProfile kCouponChain{
    2, kSenseAdcBase, 2, {16, 8}, {0, 1}, 16, 0, 4, 6, 8, 7};

// The coupon's wiring, scanned with Rev A's model: the 4067 and the 4051
// enabled together on their separate sense pins for steps 0-7, the 4067
// alone for 8-15. The coupon play image runs it so the coupon rehearses Rev
// A's pattern (spec section 3.2).
inline constexpr ChainProfile kCouponPlayChain{
    2, kSenseAdcBase, 2, {16, 8}, {0, 1}, 16, 0, 4, 6, 8, 7, 4, true};

// One mux input, named by group (chip) and channel.
struct MuxChannel
{
    int group;
    int ch;
};

// The channels of every group wired to `sense`.
constexpr int sense_channels(const ChainProfile& p, int sense)
{
    int n = 0;
    for(int g = 0; g < p.groups; ++g)
        if(p.sense_of_group[g] == sense) n += p.channels[g];
    return n;
}

constexpr int scan_steps(const ChainProfile& p)
{
    int n = 0;
    if(!p.parallel_sense)
    {
        for(int g = 0; g < p.groups; ++g) n += p.channels[g];
        return n;
    }
    for(int s = 0; s < p.sense_pins; ++s)
        if(sense_channels(p, s) > n) n = sense_channels(p, s);
    return n;
}

constexpr int mux_total(const ChainProfile& p)
{
    return scan_steps(p) * p.sense_pins;
}

struct StepPattern
{
    uint8_t  address;      // the shared address lines
    uint16_t enable_mask;  // active low, one bit per group
};

// The chain's address and enables for `step`. A step that does not exist
// parks the scan with every enable off.
StepPattern step_pattern(const ChainProfile& p, int step);

// The group a SEQUENTIAL step belongs to, or -1 for a step that does not
// exist -- and always -1 on a parallel profile, where a step has one group
// per sense pin; ask group_at() there.
int group_of_step(const ChainProfile& p, int step);

// The group the scan reads on sense pin `sense` during `step`, or -1 when no
// live channel reaches that pin (it does not exist, its group is not the
// enabled one, or -- parallel -- its channels are exhausted and the node
// floats). Both models.
int group_at(const ChainProfile& p, int step, int sense);

// The channel group_at()'s group is on during `step`, or -1 where group_at()
// says -1.
int channel_at(const ChainProfile& p, int step, int sense);

// The index g_mux_raw stores (step, sense) under, or -1 for an index that
// does not exist. This is an index bijection over (step, sense) pairs, not a
// claim about the board: callers that store values must ask sense_live()
// first, because a pin with no live channel floats.
int mux_channel(const ChainProfile& p, int step, int sense);

// Whether sense pin `sense` carries a live channel during `step`:
// group_at() >= 0.
bool sense_live(const ChainProfile& p, int step, int sense);

// The step that selects channel `ch` on group `group`, or -1 out of range --
// an out-of-range address would still select SOME channel and hand back a
// foreign knob's voltage. The bound is the group's own: the coupon's two
// groups are 16 and 8 channels.
int step_of(const ChainProfile& p, int group, int ch);

// The chain word for a step, with `leds` in the LED field. The address is
// masked to addr_bits, the enables to the profile's groups, the LEDs to
// led_bits, so no field can reach another.
uint64_t chain_word(const ChainProfile& p, StepPattern s, uint32_t leds);

// Which bit of the 74HC165 return stream carries the board's button, counted
// from the first bit shifted out, or -1 if the board has none.
int button_bit(const ChainProfile& p);

} // namespace shell
```

- [ ] **Step 5: Replace `shell/mux_plan.cpp`**

```cpp
#include "mux_plan.h"

namespace shell {

namespace {

constexpr uint16_t all_off(const ChainProfile& p)
{
    return static_cast<uint16_t>((1u << p.groups) - 1u);
}

// Parallel: the step group g's channel 0 is read on -- the channel count of
// every earlier group on the same sense pin.
int parallel_start(const ChainProfile& p, int g)
{
    int start = 0;
    for(int i = 0; i < g; ++i)
        if(p.sense_of_group[i] == p.sense_of_group[g]) start += p.channels[i];
    return start;
}

} // namespace

int group_of_step(const ChainProfile& p, int step)
{
    if(p.parallel_sense) return -1;
    if(step < 0 || step >= scan_steps(p)) return -1;
    int rest = step;
    for(int g = 0; g < p.groups; ++g)
    {
        if(rest < p.channels[g]) return g;
        rest -= p.channels[g];
    }
    return -1;
}

int group_at(const ChainProfile& p, int step, int sense)
{
    if(step < 0 || step >= scan_steps(p)) return -1;
    if(sense < 0 || sense >= p.sense_pins) return -1;
    if(!p.parallel_sense)
    {
        const int g     = group_of_step(p, step);
        const int wired = p.sense_of_group[g];
        return (wired < 0 || wired == sense) ? g : -1;
    }
    int rest = step;
    for(int g = 0; g < p.groups; ++g)
    {
        if(p.sense_of_group[g] != sense) continue;
        if(rest < p.channels[g]) return g;
        rest -= p.channels[g];
    }
    return -1;
}

int channel_at(const ChainProfile& p, int step, int sense)
{
    const int g = group_at(p, step, sense);
    if(g < 0) return -1;
    if(p.parallel_sense) return step - parallel_start(p, g);
    int ch = step;
    for(int i = 0; i < g; ++i) ch -= p.channels[i];
    return ch;
}

int step_of(const ChainProfile& p, int group, int ch)
{
    if(group < 0 || group >= p.groups) return -1;
    if(ch < 0 || ch >= p.channels[group]) return -1;
    if(p.parallel_sense) return parallel_start(p, group) + ch;
    int step = ch;
    for(int g = 0; g < group; ++g) step += p.channels[g];
    return step;
}

StepPattern step_pattern(const ChainProfile& p, int step)
{
    // A step that does not exist parks the scan with every enable off. An
    // out-of-range ADDRESS would still select some channel and hand back a
    // foreign knob's voltage, which is worse than reading nothing.
    if(step < 0 || step >= scan_steps(p)) return StepPattern{0, all_off(p)};

    if(!p.parallel_sense)
    {
        const int g    = group_of_step(p, step);
        int       addr = step;
        for(int i = 0; i < g; ++i) addr -= p.channels[i];
        return StepPattern{static_cast<uint8_t>(addr),
                           static_cast<uint16_t>(all_off(p) & ~(1u << g))};
    }

    uint16_t mask  = all_off(p);
    int      addr  = 0;
    bool     first = true;
    for(int s = 0; s < p.sense_pins; ++s)
    {
        const int g = group_at(p, step, s);
        if(g < 0) continue;
        mask = static_cast<uint16_t>(mask & ~(1u << g));
        if(first)
        {
            addr  = channel_at(p, step, s);
            first = false;
        }
    }
    return StepPattern{static_cast<uint8_t>(addr), mask};
}

int mux_channel(const ChainProfile& p, int step, int sense)
{
    if(step < 0 || step >= scan_steps(p)) return -1;
    if(sense < 0 || sense >= p.sense_pins) return -1;
    return step * p.sense_pins + sense;
}

bool sense_live(const ChainProfile& p, int step, int sense)
{
    return group_at(p, step, sense) >= 0;
}

uint64_t chain_word(const ChainProfile& p, StepPattern s, uint32_t leds)
{
    const uint64_t addr_mask = (uint64_t{1} << p.addr_bits) - 1u;
    const uint64_t led_mask  = (uint64_t{1} << p.led_bits) - 1u;
    return ((uint64_t{s.address} & addr_mask) << p.addr_shift)
           | (uint64_t{static_cast<uint16_t>(s.enable_mask & all_off(p))}
              << p.enable_shift)
           | ((uint64_t{leds} & led_mask) << p.led_shift);
}

int button_bit(const ChainProfile& p) { return p.button_bit; }

} // namespace shell
```

- [ ] **Step 6: Run the host tests and verify they pass**

```bash
HOSTENV cmake --build build
```
```bash
HOSTENV ctest --test-dir build -R "spky_tests" --output-on-failure
```
Expected: PASS. That includes every pre-existing `mux plan:` and `xtalk plan:` case. Those compare `chain_word()` (now `uint64_t`) against `uint32_t` locals, which is a widening comparison.

- [ ] **Step 7: Prove the address mask can go red**

Temporarily change `addr_mask` in `chain_word()` to `0x0Fu`. Rebuild, run `spky_tests`, and confirm "chain word: Rev A's address cannot reach the enable field" fails. Restore the line with the editor (not `git checkout`), rebuild, run again: PASS. Note both runs in the report.

- [ ] **Step 8: Check the firmware still builds**

```bash
ARMENV make -C shell -j8 images SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1
```
Expected: links. `mux_scan.cpp` still passes a `uint64_t` into `write_chain(uint32_t)`; that narrowing is harmless for 16- and 32-bit profiles and is fixed in Task 4.

- [ ] **Step 9: Commit**

```bash
git add shell/mux_plan.h shell/mux_plan.cpp tests/test_mux_plan.cpp
```
```bash
git commit -m "shell: parallel step model for Rev A, kCouponPlayChain, 64-bit chain word

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: Keys, the panel span, and a refusing apply_control

**Files:**
- Create: `shell/keys.h`, `shell/keys.cpp`, `tests/test_keys.cpp`
- Modify: `shell/controls.h`, `shell/controls.cpp`, `shell/scan_value.h`, `shell/scan_value.cpp`
- Modify: `tests/test_controls_map.cpp`, `tests/test_scan_value.cpp` (append)
- Modify: `CMakeLists.txt` (the `spky_tests` source list)

**Interfaces:**
- Consumes: `shell::button_bit`, `shell::kCouponChain` (Task 1).
- Produces:
  - `struct ControlEntry { int group; int ch; int param; int sense = -1; };` — `param` −1 = scanned, sent nowhere.
  - `bool apply_control(const ControlEntry&, float v, spky::Instrument&);` — false and no effect for a param outside `0..P_COUNT-1`.
  - `Span panel_span(uint16_t zero, uint16_t rail);`
  - `keys.h`:
    - `kMaxKeys` = 4, `kKeyDebounce` = 3;
    - `struct KeyPad { int count; int bit[kMaxKeys]; };`
    - `inline constexpr KeyPad kCouponKeys{1, {7}};`
    - `struct KeyState { uint8_t pressed; uint8_t candidate; uint8_t same[4]; uint16_t presses[4]; };`
    - `void key_update(KeyState&, const KeyPad&, uint32_t ret);`

- [ ] **Step 1: Write the failing tests**

Create `tests/test_keys.cpp`:

```cpp
// The panel's keys, debounced on the host: on the board a key that bounces
// into two presses, or never lets go, is a gesture that does the wrong thing.
// Spec: docs/superpowers/specs/2026-10-02-rev-a-p6a-panel-scan-design.md
// section 3.4.
#include <doctest/doctest.h>
#include "../shell/keys.h"
#include "../shell/mux_plan.h"

namespace {
constexpr uint32_t kUp = 0xFFFFFFFFu;   // every input high: every key released
constexpr uint32_t down(int bit) { return kUp & ~(1u << bit); }
constexpr shell::KeyPad kTwo{2, {7, 6}};

void feed(shell::KeyState& s, const shell::KeyPad& p, uint32_t ret, int n)
{
    for(int i = 0; i < n; ++i) shell::key_update(s, p, ret);
}
}

TEST_CASE("keys: fewer equal reads than the debounce are not a press") {
    shell::KeyState s;
    feed(s, shell::kCouponKeys, down(7), shell::kKeyDebounce - 1);
    CHECK(s.pressed == 0);
    feed(s, shell::kCouponKeys, down(7), 1);
    CHECK(s.pressed == 1);
    CHECK(s.presses[0] == 1);
}

TEST_CASE("keys: a bouncing contact never registers") {
    shell::KeyState s;
    for(int i = 0; i < 20; ++i)
        shell::key_update(s, shell::kCouponKeys, (i & 1) ? kUp : down(7));
    CHECK(s.pressed == 0);
    CHECK(s.presses[0] == 0);
}

TEST_CASE("keys: a held key counts once, and release is debounced too") {
    shell::KeyState s;
    feed(s, shell::kCouponKeys, down(7), 50);
    CHECK(s.presses[0] == 1);
    feed(s, shell::kCouponKeys, kUp, shell::kKeyDebounce - 1);
    CHECK(s.pressed == 1);
    feed(s, shell::kCouponKeys, kUp, 1);
    CHECK(s.pressed == 0);
    CHECK(s.presses[0] == 1);
}

TEST_CASE("keys: held from the first read counts as one press") {
    // A key held while the board powers up (Review Focus 3).
    shell::KeyState s;
    feed(s, shell::kCouponKeys, down(7), 200);
    CHECK(s.pressed == 1);
    CHECK(s.presses[0] == 1);
}

TEST_CASE("keys: five clean presses count five") {
    shell::KeyState s;
    for(int i = 0; i < 5; ++i)
    {
        feed(s, shell::kCouponKeys, down(7), 5);
        feed(s, shell::kCouponKeys, kUp, 5);
    }
    CHECK(s.presses[0] == 5);
    CHECK(s.pressed == 0);
}

TEST_CASE("keys: active low -- a high input is a released key") {
    shell::KeyState s;
    feed(s, shell::kCouponKeys, kUp, 10);
    CHECK(s.pressed == 0);
    feed(s, shell::kCouponKeys, 0u, shell::kKeyDebounce);
    CHECK(s.pressed == 1);
}

TEST_CASE("keys: two keys debounce independently") {
    shell::KeyState s;
    feed(s, kTwo, down(7), 2);
    feed(s, kTwo, down(7) & down(6), 1);   // key 1 starts while key 0 is mid-count
    CHECK(s.pressed == 0x1);
    feed(s, kTwo, down(7) & down(6), 2);
    CHECK(s.pressed == 0x3);
    CHECK(s.presses[0] == 1);
    CHECK(s.presses[1] == 1);
}

TEST_CASE("keys: the coupon key is the bit kCouponChain names") {
    CHECK(shell::kCouponKeys.count == 1);
    CHECK(shell::kCouponKeys.bit[0] == shell::button_bit(shell::kCouponChain));
}
```

Append to `tests/test_controls_map.cpp`:

```cpp
TEST_CASE("controls: an entry without a parameter is refused, not applied") {
    // Rev A's table carries rows that are scanned and reported but sent
    // nowhere (spec section 2.3). apply_param() would index kParams[-1].
    spky::Instrument inst;
    inst.init(48000.0f);
    const float a = inst.rate(spky::PART_A);
    CHECK_FALSE(shell::apply_control(shell::ControlEntry{0, 0, -1}, 0.9f, inst));
    CHECK_FALSE(shell::apply_control(shell::ControlEntry{0, 0, spky::P_COUNT}, 0.9f, inst));
    CHECK(inst.rate(spky::PART_A) == doctest::Approx(a));
    CHECK(shell::apply_control(shell::ControlEntry{0, 2, spky::P_RATE_A}, 0.9f, inst));
    CHECK(inst.rate(spky::PART_A) == doctest::Approx(0.9f));
}

TEST_CASE("controls: an entry records its sense pin, -1 when unrecorded") {
    CHECK(shell::kCouponControls[0].sense == -1);
    // A local, not a braced temporary inside CHECK(): the preprocessor
    // splits macro arguments on the commas inside braces.
    const shell::ControlEntry e{3, 4, spky::P_RATE_B, 1};
    CHECK(e.sense == 1);
}
```

Append to `tests/test_scan_value.cpp`:

```cpp
TEST_CASE("scan value: the panel span is the two calibration channels") {
    const shell::Span s = shell::panel_span(31, 63484);
    CHECK(s.valid);
    CHECK(s.zero == 31);
    CHECK(s.rail == 63484);
}

TEST_CASE("scan value: a collapsed rail or a lifted zero is not a panel span") {
    CHECK_FALSE(shell::panel_span(31, shell::kRailFloor - 1).valid);
    CHECK(shell::panel_span(31, shell::kRailFloor).valid);
    CHECK_FALSE(shell::panel_span(shell::kRailMargin + 1, 63484).valid);
    CHECK(shell::panel_span(shell::kRailMargin, 63484).valid);
}

TEST_CASE("scan value: an invalid span emits nothing, before or after a valid one") {
    // Boot: the play images start with an invalid span and nothing may reach
    // the engine until a sweep has measured one (Review Focus 2).
    shell::PotFilter f;
    float v = -1.0f;
    CHECK_FALSE(shell::pot_filter(f, 30000, shell::Span{0, 0, false}, 16, &v));
    CHECK(v == -1.0f);
    CHECK(shell::pot_filter(f, 30000, shell::panel_span(31, 63484), 16, &v));
}
```

In `CMakeLists.txt`, in the `spky_tests` source list directly after `tests/test_scan_value.cpp`, add:

```cmake
    shell/keys.cpp
    tests/test_keys.cpp
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
HOSTENV cmake --build build
```
Expected: compile errors. `keys.h` is missing, `apply_control` returns `void`, `ControlEntry` has no `sense`, and `panel_span` is undeclared.

- [ ] **Step 3: Create `shell/keys.h`**

```cpp
#pragma once

// The panel's keys, read through the 74HC165 in the same pass as every scan
// step (spec 2026-10-02-rev-a-p6a-panel-scan-design.md section 3.4). No
// hardware type: tests/test_keys.cpp holds the debounce on the host.
#include <cstdint>

namespace shell {

inline constexpr int kMaxKeys = 4;

// A key changes state after this many equal reads in a row. One read per
// 2 ms block, so 6 ms.
inline constexpr int kKeyDebounce = 3;

// Where each key sits in the 165's return stream, counted from the first bit
// shifted out (MuxScan::read_chain). The first bit is D7, so a key on Dn is
// bit 7 - n.
struct KeyPad
{
    int count;
    int bit[kMaxKeys];
};

// The coupon: SW1 on D0 (hardware/coupon/scripts/netlist.py), so bit 7 --
// what kCouponChain.button_bit has always said.
inline constexpr KeyPad kCouponKeys{1, {7}};

struct KeyState
{
    uint8_t  pressed           = 0;   // bit i = key i, debounced
    uint8_t  candidate         = 0;   // bit i = what key i read last
    uint8_t  same[kMaxKeys]    = {};  // equal reads of the candidate in a row
    uint16_t presses[kMaxKeys] = {};  // released -> pressed transitions
};

// One read of the 165's return stream. Keys are active low: a pressed key
// pulls its input to GND against the pull-up.
void key_update(KeyState& s, const KeyPad& pad, uint32_t ret);

} // namespace shell
```

- [ ] **Step 4: Create `shell/keys.cpp`**

```cpp
#include "keys.h"

namespace shell {

void key_update(KeyState& s, const KeyPad& pad, uint32_t ret)
{
    for(int i = 0; i < pad.count && i < kMaxKeys; ++i)
    {
        const uint8_t me   = static_cast<uint8_t>(1u << i);
        const bool    raw  = ((ret >> pad.bit[i]) & 1u) == 0u;
        const bool    cand = (s.candidate & me) != 0u;
        if(raw == cand)
        {
            if(s.same[i] < kKeyDebounce) ++s.same[i];
        }
        else
        {
            s.candidate = static_cast<uint8_t>(raw ? (s.candidate | me)
                                                   : (s.candidate & ~me));
            s.same[i]   = 1;
        }
        const bool now = (s.pressed & me) != 0u;
        if(s.same[i] >= kKeyDebounce && now != raw)
        {
            s.pressed = static_cast<uint8_t>(raw ? (s.pressed | me)
                                                 : (s.pressed & ~me));
            if(raw) ++s.presses[i];
        }
    }
}

} // namespace shell
```

- [ ] **Step 5: Change `shell/controls.h`**

Replace the `ControlEntry` struct and the `apply_control` declaration.

```cpp
struct ControlEntry
{
    int group;       // mux group (chip) as in mux_plan.h; on Rev A the global mux 0..9
    int ch;          // channel on that chip
    int param;       // spky::ParamId, or -1: scanned and reported, sent nowhere
    int sense = -1;  // the sense pin the schematic wires the group to, -1 = not recorded
};
```

```cpp
// Scales v into the entry's parameter range and routes it via apply_param().
// Returns false, and touches nothing, for an entry without a parameter.
bool apply_control(const ControlEntry& e, float v, spky::Instrument& inst);
```

- [ ] **Step 6: Change `shell/controls.cpp`**

```cpp
bool apply_control(const ControlEntry& e, float v, spky::Instrument& inst)
{
    if(e.param < 0 || e.param >= spky::P_COUNT) return false;
    // No clamp here: v arrives clamped from scan_value's span_normalize(),
    // and apply_param() clamps to the table range once more.
    spky::apply_param(inst, e.param, control_value(e.param, v));
    return true;
}
```

- [ ] **Step 7: Add `panel_span` to `shell/scan_value.h` and `shell/scan_value.cpp`**

In `scan_value.h`, below `kPotHysteresis`:

```cpp
// The span Rev A measures for itself once per sweep from its two calibration
// channels, CAL_GND and CAL_3V3 (spec 2026-10-02 section 3.5): coupon_span()'s
// rail floor and zero ceiling. There is no tie spread to check -- one channel
// per rail -- and the two bounds already order the pair.
Span panel_span(uint16_t zero, uint16_t rail);
```

In `scan_value.cpp`, inside `namespace shell`:

```cpp
Span panel_span(uint16_t zero, uint16_t rail)
{
    return Span{zero, rail, rail >= kRailFloor && zero <= kRailMargin};
}
```

- [ ] **Step 8: Run the tests and verify they pass**

```bash
HOSTENV cmake --build build
```
```bash
HOSTENV ctest --test-dir build -R "spky_tests" --output-on-failure
```
Expected: PASS.

- [ ] **Step 9: Prove the debounce and the refusal can go red**

Run two sabotages, one at a time:
1. Set `kKeyDebounce` to 1. Expected red: "fewer equal reads than the debounce" and "a bouncing contact never registers".
2. Make `apply_control` skip its `param` check. Expected red: "an entry without a parameter is refused". It may also crash, since `kParams[-1]` is out of bounds; either outcome is red.

Restore each with the editor, rebuild, and confirm PASS. Note both runs in the report.

- [ ] **Step 10: Commit**

```bash
git add shell/keys.h shell/keys.cpp shell/controls.h shell/controls.cpp shell/scan_value.h shell/scan_value.cpp tests/test_keys.cpp tests/test_controls_map.cpp tests/test_scan_value.cpp CMakeLists.txt
```
```bash
git commit -m "shell: key debounce, panel span from two calibration channels, apply_control refuses rows without a parameter

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The generated Rev A table

**Files:**
- Create: `shell/gen_panel_map.py`, `shell/test_gen_panel_map.py`
- Create (generated, committed): `shell/generated_panel_map.h`
- Modify: `CMakeLists.txt` (one `add_test`)
- Test: `tests/test_mux_plan.cpp`, `tests/test_controls_map.cpp` (append)

**Interfaces:**
- Consumes:
  - `ChainProfile` (with `addr_bits`, `parallel_sense`), `MuxChannel`, `group_at`, `channel_at`, `step_of` (Task 1);
  - `ControlEntry` with `sense`, `ControlTable`, `KeyPad` (Task 2).
- Produces (in `shell/generated_panel_map.h`, namespace `shell`):
  - `inline constexpr ChainProfile kRevaChain`
  - `inline constexpr ControlEntry kRevaControls[]` (70 rows, (mux, channel) order)
  - `inline constexpr ControlTable kRevaTable`
  - `inline constexpr MuxChannel kRevaCalZero`, `kRevaCalRail`
  - `inline constexpr KeyPad kRevaKeys`
- `gen_panel_map.py` API:
  - `GenError`, `load_inputs() -> dict`, `build(panel_map, tables, params, hw_ids, safe, unmapped) -> str`
  - `check_text(committed, generated) -> bool`, `read_committed() -> str`, `main(argv) -> int`

- [ ] **Step 1: Write the failing guard `shell/test_gen_panel_map.py`**

```python
#!/usr/bin/env python3
"""Guard for shell/gen_panel_map.py (spec 2026-10-02-rev-a-p6a-panel-scan-design.md
section 4). Plain script -- pytest is not installed here -- and its exit code
is the verdict. Every input check has a sabotage that must turn it red.
"""
import copy
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import gen_panel_map as g  # noqa: E402

FAILS = []


def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        FAILS.append(name)


BASE = g.load_inputs()
TEXT = g.build(**BASE)
ROWS = [line for line in TEXT.splitlines() if "// row " in line]

# --- live ---------------------------------------------------------------
check("committed header equals the generator's output",
      g.check_text(g.read_committed(), TEXT))
check("70 pot rows", len(ROWS) == 70)
check("35 rows send a parameter", sum("spky::P_" in r for r in ROWS) == 35)
check("every SAFE ParamId appears in the header",
      all("spky::%s," % pid in TEXT for pid, _ in BASE["safe"].values()))
check("a CRLF checkout is not stale",
      g.check_text(TEXT.replace("\n", "\r\n"), TEXT))


# --- sabotages: each must make build() raise with the named reason ---------
def sabotage(name, mutate, needle):
    inp = copy.deepcopy(BASE)
    mutate(inp)
    label = "sabotage %s goes red (%s)" % (name, needle)
    try:
        g.build(**inp)
    except g.GenError as e:
        check(label, needle in str(e))
        if needle not in str(e):
            print("     got: %s" % e)
        return
    check(label, False)


def swap_led_with_spare(inp):
    chips = inp["tables"]["sr_outputs"]
    where = {n: (c, k) for c, chip in enumerate(chips) for k, n in enumerate(chip)}
    (a, b), (c, d) = where["LED4"], where["SR_SPARE0"]
    chips[a][b], chips[c][d] = "SR_SPARE0", "LED4"


def swap_d8_d9(inp):
    pins = inp["tables"]["module_pins"]
    pins["D8"], pins["D9"] = pins["D9"], pins["D8"]


def duplicate_channel(inp):
    pots = inp["panel_map"]["pots"]
    pots[1]["mux"], pots[1]["channel"] = pots[0]["mux"], pots[0]["channel"]


sabotage("pot unclassified", lambda i: i["safe"].pop("RATE_A"),
         "unclassified: RATE_A")
sabotage("pot in both lists", lambda i: i["unmapped"].__setitem__("RATE_A", "x"),
         "both SAFE and UNMAPPED: RATE_A")
sabotage("unknown ParamId",
         lambda i: i["safe"].__setitem__("RATE_A", ("P_RATE_Q", "x")),
         "no ParamId P_RATE_Q")
sabotage("pot missing from the VCV HW panel",
         lambda i: i["hw_ids"].discard("RATE_A"),
         "not in generated_hw_panel.hpp: RATE_A")
sabotage("classified name that is no pot",
         lambda i: i["safe"].__setitem__("NOPE_A", ("P_RATE_A", "x")),
         "not a pot: NOPE_A")
sabotage("two rows on one mux input", duplicate_channel, "twice: mux 0 ch 0")
sabotage("pot on the wrong sense pin",
         lambda i: i["panel_map"]["pots"][0].__setitem__("sense", "SENSE_3"),
         "but mux 0 is on SENSE_0")
sabotage("an unaccounted mux input",
         lambda i: i["panel_map"]["spare"].pop(0),
         "79 of 80 mux inputs")
sabotage("LED field not contiguous", swap_led_with_spare,
         "LED0..LED18 not contiguous")
sabotage("sense pins off the ADC run", swap_d8_d9,
         "SENSE_2 on D8 is ADC index 11, expected 10")
sabotage("mux list out of order",
         lambda i: i["panel_map"]["muxes"].__setitem__("SENSE_0", [1, 0, 2]),
         "not ascending")
sabotage("more keys than the firmware holds",
         lambda i: i["tables"]["keys"].append("EXTRA"),
         "keys: 5")
sabotage("calibration channel renamed",
         lambda i: i["panel_map"]["calibration"][0].__setitem__("id", "CAL_X"),
         "calibration")

# --- stale header ---------------------------------------------------------
check("sabotage stale header goes red",
      not g.check_text(TEXT.replace("spky::P_RATE_A", "spky::P_RATE_B", 1), TEXT))

print("%d failed" % len(FAILS) if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
```

- [ ] **Step 2: Run it to verify it fails**

```bash
python shell/test_gen_panel_map.py
```
Expected: `ModuleNotFoundError: No module named 'gen_panel_map'`.

- [ ] **Step 3: Write `shell/gen_panel_map.py`**

```python
#!/usr/bin/env python3
"""Generates shell/generated_panel_map.h: the Rev A panel as the firmware sees it.

Spec: docs/superpowers/specs/2026-10-02-rev-a-p6a-panel-scan-design.md,
sections 2 and 4.

Inputs, all read, none written:
  hardware/reva/panel-map.json         P3's assignment (assign.py): pots, muxes,
                                       calibration and spare channels
  hardware/reva/blocks.py              P2's tables: SR_OUTPUTS, KEYS, MODULE_PINS
  engine/param_table.h                 the ParamId names
  host/vcv/src/generated_hw_panel.hpp  the FireflowHW controls

SAFE and UNMAPPED below are the only hand-written part. A pot P3 adds or
renames stops the generator until it is put in one of them.

    python shell/gen_panel_map.py           write the header
    python shell/gen_panel_map.py --check   exit 1 if the committed header is stale
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
REVA = os.path.join(ROOT, "hardware", "reva")
PANEL_MAP = os.path.join(REVA, "panel-map.json")
PARAM_TABLE = os.path.join(ROOT, "engine", "param_table.h")
HW_PANEL = os.path.join(ROOT, "host", "vcv", "src", "generated_hw_panel.hpp")
OUT = os.path.join(HERE, "generated_panel_map.h")

CHANNELS_PER_MUX = 8   # P2 section 3: ten 74HC4051
MAX_KEYS = 4           # shell/keys.h kMaxKeys
SENSE_ADC_BASE = 8     # shell/mux_plan.h kSenseAdcBase (ADC_9)
# libDaisy's patch_sm ADC index per module pin: the pin table in
# shell/main.cpp's adc_use_measured_sampling_time(), and P2 section 2.
ADC_OF_PIN = {"A2": 8, "A3": 9, "D9": 10, "D8": 11}
CAL_IDS = ("CAL_GND", "CAL_3V3")

# Spec section 2.2: VCV calls exactly the setter apply_param() calls, passes
# the knob value unchanged, over the same range. Evidence is the line in
# host/vcv/src/Fireflow.cpp as of 2026-10-02; deck B runs through the same
# line inside pushParams()' per-deck loop.
_PER_DECK_SAFE = {
    "RATE":    ("P_RATE",    "set_rate(mvp), Fireflow.cpp:783"),
    "SHAPE":   ("P_SHAPE",   "set_shape(mvp), Fireflow.cpp:784"),
    "SMOOTH":  ("P_SMOOTH",  "set_smooth(mvp), Fireflow.cpp:786"),
    "RANGE":   ("P_RANGE",   "set_range(mvp), Fireflow.cpp:787"),
    "MOD":     ("P_DEPTH",   "set_depth(pp(MOD)), Fireflow.cpp:790; "
                             "apply_param(P_DEPTH) calls set_depth"),
    "TUNE":    ("P_TUNE",    "set_tune(mvp), Fireflow.cpp:791"),
    "DECAY":   ("P_DECAY",   "set_voice_decay(mvp), Fireflow.cpp:794"),
    "FILT":    ("P_FILT",    "set_voice_filt(raw), Fireflow.cpp:798"),
    "COLOR":   ("P_COLOR",   "set_color(mv), Fireflow.cpp:799"),
    "LINK":    ("P_LINK",    "set_link(mv), Fireflow.cpp:832"),
    "PAN":     ("P_PAN",     "set_pan(mv), Fireflow.cpp:898"),
    "REV_MIX": ("P_REVMIX",  "set_reverb_mix(part, raw), Fireflow.cpp:1241-1242"),
}
_GLOBAL_SAFE = {
    "SHUFFLE":   ("P_SHUFFLE",   "set_shuffle(raw), Fireflow.cpp:781"),
    "MORPH":     ("P_MORPH",     "set_morph(mv), Fireflow.cpp:1204"),
    "TIDE":      ("P_TIDE",      "set_tide(mv), Fireflow.cpp:1230"),
    "CHOKE":     ("P_CHOKE",     "set_choke(raw), Fireflow.cpp:1231"),
    "PULL":      ("P_PULL",      "set_pull(raw), Fireflow.cpp:1232"),
    "REV_SIZE":  ("P_REV_SIZE",  "set_reverb_size(mv), Fireflow.cpp:1237"),
    "REV_DECAY": ("P_REV_DECAY", "set_reverb_decay(mv), Fireflow.cpp:1238"),
    "REV_TONE":  ("P_REV_TONE",  "set_reverb_tone(mv), Fireflow.cpp:1239"),
    "REV_DIFF":  ("P_REV_DIFF",  "set_reverb_diffusion(mv), Fireflow.cpp:1240"),
    "SCALE":     ("P_SCALE",     "set_scale(round), Fireflow.cpp:1253; 13 steps both sides"),
    "PACE":      ("P_PACE",      "set_pace(raw), Fireflow.cpp:1262"),
}
_PER_DECK_UNMAPPED = {
    "DENSITY":  "also drives sampler_overlap, Fireflow.cpp:993",
    "ATTACK":   "one pot with STAGES; the BBD re-points it, Fireflow.cpp:1022",
    "SUB":      "LANE_SIZE on the sampler, Fireflow.cpp:1095",
    "RES":      "VCV knob 0..1 (Fireflow.cpp:479), table 0..0.75",
    "DEPTH":    "LANE_MOTION base, Fireflow.cpp:1130",
    "COMP":     "level/compressor split with a curve, Fireflow.cpp:872-883",
    "FLUX":     "also switches the FX block on, Fireflow.cpp:840",
    "GRIT":     "bipolar with a dead zone and a mode, Fireflow.cpp:1143-1148",
    "MELODY":   "SCAN on the sampler, Fireflow.cpp:1082-1083",
    "DETUNE":   "squared; SPREAD on FEED, Fireflow.cpp:812-819, 1097",
    "ENGINE":   "UI remap, Fireflow.cpp:906-914",
    "STEPS":    "0 = STEP off, per-deck set_step, Fireflow.cpp:1149-1150",
    "SONG":     "14-rung ladder, Fireflow.cpp:1163-1175",
    "SOURCE":   "no ParamId; LANE_SOURCE base, Fireflow.cpp:994",
    "FLUXRATE": "no ParamId; detented index, Fireflow.cpp:822-823",
    "FLUXFB":   "no ParamId; FX target base, Fireflow.cpp:824-825",
}
_GLOBAL_UNMAPPED = {
    "COUPLE": "sync zone split, Fireflow.cpp:1211-1216",
    "DRIFT":  "settle zone, Fireflow.cpp:1223-1229",
    "TEMPO":  "VCV maps 40 + 200 v (Fireflow.cpp:1256), table 50..140",
}

SAFE = dict(_GLOBAL_SAFE)
for _base, (_pid, _ev) in _PER_DECK_SAFE.items():
    for _d in "AB":
        SAFE["%s_%s" % (_base, _d)] = ("%s_%s" % (_pid, _d), _ev)
UNMAPPED = dict(_GLOBAL_UNMAPPED)
for _base, _why in _PER_DECK_UNMAPPED.items():
    for _d in "AB":
        UNMAPPED["%s_%s" % (_base, _d)] = _why


class GenError(Exception):
    pass


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def read_committed():
    return read(OUT) if os.path.isfile(OUT) else ""


def param_names(text):
    start = text.index("#define SPKY_PARAMS")
    return set(re.findall(r"X\((P_\w+)", text[start:text.index("enum ParamId", start)]))


def hw_panel_ids(text):
    start = text.index("kParamCtls[]")
    return set(re.findall(r"^\s*\{(\w+), WK_", text[start:text.index("};", start)], re.M))


def load_inputs():
    if REVA not in sys.path:
        sys.path.insert(0, REVA)
    import blocks
    panel_map = json.loads(read(PANEL_MAP))
    tables = {
        "sr_outputs": [list(chip) for chip in blocks.SR_OUTPUTS],
        "keys": list(blocks.KEYS),
        "module_pins": dict(blocks.MODULE_PINS),
        "mux_s": [blocks.sr(n) for n in blocks.MUX_S],
        "mux_en": [blocks.sr(n) for n in blocks.MUX_EN],
        "leds": [blocks.led_net(i) for i in range(len(panel_map["leds"]))],
    }
    return {"panel_map": panel_map, "tables": tables,
            "params": param_names(read(PARAM_TABLE)),
            "hw_ids": hw_panel_ids(read(HW_PANEL)),
            "safe": dict(SAFE), "unmapped": dict(UNMAPPED)}


def _run(flat, nets):
    """The bit the first of `nets` sits on; they must follow one another."""
    missing = [n for n in nets if n not in flat]
    if missing:
        raise GenError("chain: %s not in SR_OUTPUTS" % ", ".join(missing))
    idx = [flat.index(n) for n in nets]
    if idx != list(range(idx[0], idx[0] + len(idx))):
        raise GenError("chain: %s..%s not contiguous: %s" % (nets[0], nets[-1], idx))
    return idx[0]


def build(panel_map, tables, params, hw_ids, safe, unmapped):
    pots, cal = panel_map["pots"], panel_map["calibration"]
    spare, muxes = panel_map.get("spare", []), panel_map["muxes"]

    # muxes and sense pins
    senses = ["SENSE_%d" % i for i in range(len(muxes))]
    if sorted(muxes) != senses:
        raise GenError("sense pins: %s" % sorted(muxes))
    sense_of_mux = {}
    for i, s in enumerate(senses):
        if muxes[s] != sorted(muxes[s]):
            raise GenError("%s: mux list not ascending: %s" % (s, muxes[s]))
        for m in muxes[s]:
            if m in sense_of_mux:
                raise GenError("mux %d on two sense pins" % m)
            sense_of_mux[m] = i
    n_mux = len(sense_of_mux)
    if sorted(sense_of_mux) != list(range(n_mux)):
        raise GenError("muxes are not 0..%d: %s" % (n_mux - 1, sorted(sense_of_mux)))

    # every mux input exactly once: pots, calibration, spare (P2 section 3)
    seen = {}
    for rows in (pots, cal, spare):
        for r in rows:
            m, ch, name = r["mux"], r["channel"], r.get("id", "spare")
            if m not in sense_of_mux or not 0 <= ch < CHANNELS_PER_MUX:
                raise GenError("%s: no such mux input: mux %s ch %s" % (name, m, ch))
            if (m, ch) in seen:
                raise GenError("twice: mux %d ch %d (%s, %s)" % (m, ch, seen[(m, ch)], name))
            seen[(m, ch)] = name
            if r["sense"] != senses[sense_of_mux[m]]:
                raise GenError("%s: sense %s, but mux %d is on %s"
                               % (name, r["sense"], m, senses[sense_of_mux[m]]))
    if len(seen) != n_mux * CHANNELS_PER_MUX:
        raise GenError("%d of %d mux inputs accounted for"
                       % (len(seen), n_mux * CHANNELS_PER_MUX))

    # sense pin -> ADC channel: the profile assumes one contiguous run
    pin_of = {net: pin for pin, net in tables["module_pins"].items()}
    for i, s in enumerate(senses):
        adc = ADC_OF_PIN.get(pin_of.get(s))
        if adc != SENSE_ADC_BASE + i:
            raise GenError("%s on %s is ADC index %s, expected %d"
                           % (s, pin_of.get(s), adc, SENSE_ADC_BASE + i))

    # the 595 chain: bit k of the word lands on output k of SR_OUTPUTS
    flat = [n for chip in tables["sr_outputs"] for n in chip]
    if len(flat) > 64:
        raise GenError("chain: %d bits do not fit a 64-bit word" % len(flat))
    addr_shift = _run(flat, tables["mux_s"])
    enable_shift = _run(flat, tables["mux_en"][:n_mux])
    leds = tables["leds"]
    led_shift = _run(flat, leds)

    # the 165: key i on D i, and the first bit shifted out is D7
    keys = tables["keys"]
    if not 0 < len(keys) <= MAX_KEYS:
        raise GenError("keys: %d, the firmware holds 1..%d" % (len(keys), MAX_KEYS))
    key_bits = [7 - i for i in range(len(keys))]

    cal_by_id = {c["id"]: c for c in cal}
    if sorted(cal_by_id) != sorted(CAL_IDS):
        raise GenError("calibration: %s, expected %s" % (sorted(cal_by_id), list(CAL_IDS)))

    # classification (spec section 2)
    ids = [p["id"] for p in pots]
    if len(set(ids)) != len(ids):
        raise GenError("pot ids not unique")
    for name in sorted(set(safe) | set(unmapped)):
        if name not in ids:
            raise GenError("not a pot: %s" % name)
    both = sorted(set(safe) & set(unmapped))
    if both:
        raise GenError("both SAFE and UNMAPPED: %s" % ", ".join(both))
    missing = [i for i in ids if i not in safe and i not in unmapped]
    if missing:
        raise GenError("unclassified: %s" % ", ".join(missing))
    for name, (pid, _) in sorted(safe.items()):
        if pid not in params:
            raise GenError("%s: no ParamId %s in engine/param_table.h" % (name, pid))
    for p in pots:
        for i in p.get("ids", [p["id"]]):
            if i not in hw_ids:
                raise GenError("not in generated_hw_panel.hpp: %s" % i)

    rows = []
    for p in sorted(pots, key=lambda r: (r["mux"], r["channel"])):
        if p["id"] in safe:
            pid, ev = safe[p["id"]]
            rows.append((p, "spky::" + pid, "safe: " + ev))
        else:
            rows.append((p, "-1", "unmapped: " + unmapped[p["id"]]))

    return _render(rows, len(senses), n_mux, sense_of_mux, len(flat), addr_shift,
                   len(tables["mux_s"]), enable_shift, led_shift, len(leds),
                   cal_by_id, keys, key_bits)


def _render(rows, n_sense, n_mux, sense_of_mux, chain_bits, addr_shift, addr_bits,
            enable_shift, led_shift, led_bits, cal_by_id, keys, key_bits):
    out = []
    w = out.append
    w("// GENERATED by shell/gen_panel_map.py -- do not edit by hand.")
    w("// Sources: hardware/reva/panel-map.json, hardware/reva/blocks.py,")
    w("// engine/param_table.h; the SAFE/UNMAPPED lists live in the generator.")
    w("// Spec: docs/superpowers/specs/2026-10-02-rev-a-p6a-panel-scan-design.md")
    w("#pragma once")
    w('#include "controls.h"')
    w('#include "keys.h"')
    w('#include "mux_plan.h"')
    w("")
    w("namespace shell {")
    w("")
    w("// P2 sections 3 and 4: %d 74HC4051 on %d sense pins, one mux per sense pin"
      % (n_mux, n_sense))
    w("// per step; a %d-bit 595 chain -- address %d-%d, enables %d-%d, LEDs %d-%d."
      % (chain_bits, addr_shift, addr_shift + addr_bits - 1, enable_shift,
         enable_shift + n_mux - 1, led_shift, led_shift + led_bits - 1))
    w("inline constexpr ChainProfile kRevaChain{")
    w("    %d, kSenseAdcBase, %d," % (n_sense, n_mux))
    w("    {%s}," % ", ".join([str(CHANNELS_PER_MUX)] * n_mux))
    w("    {%s}," % ", ".join(str(sense_of_mux[m]) for m in range(n_mux)))
    w("    %d, %d, %d, %d, %d, -1, %d, true};"
      % (chain_bits, addr_shift, enable_shift, led_shift, led_bits, addr_bits))
    w("")
    w("// One row per pot in (mux, channel) order; SHELL_PLAY_V prints the values")
    w("// in this order. param -1: scanned and reported, sent nowhere (spec 2.3).")
    w("inline constexpr ControlEntry kRevaControls[] = {")
    for i, (p, target, note) in enumerate(rows):
        w("    {%d, %d, %s, %d},  // row %d %s -- %s"
          % (p["mux"], p["channel"], target, sense_of_mux[p["mux"]], i, p["id"], note))
    w("};")
    w("inline constexpr ControlTable kRevaTable{")
    w("    kRevaControls,")
    w("    static_cast<int>(sizeof(kRevaControls) / sizeof(kRevaControls[0]))};")
    w("")
    w("// The calibration channels (P2 section 3): the panel reads its own span.")
    z, r = cal_by_id["CAL_GND"], cal_by_id["CAL_3V3"]
    w("inline constexpr MuxChannel kRevaCalZero{%d, %d};  // CAL_GND" % (z["mux"], z["channel"]))
    w("inline constexpr MuxChannel kRevaCalRail{%d, %d};  // CAL_3V3" % (r["mux"], r["channel"]))
    w("")
    w("// %s on the 165's D0..D%d; the first bit shifted out is D7."
      % (", ".join(keys), len(keys) - 1))
    w("inline constexpr KeyPad kRevaKeys{%d, {%s}};"
      % (len(keys), ", ".join(str(b) for b in key_bits)))
    w("")
    w("} // namespace shell")
    return "\n".join(out) + "\n"


def check_text(committed, generated):
    return committed.replace("\r\n", "\n") == generated


def main(argv):
    try:
        text = build(**load_inputs())
    except GenError as e:
        print("gen_panel_map: %s" % e, file=sys.stderr)
        return 2
    if "--check" in argv:
        if check_text(read_committed(), text):
            print("shell/generated_panel_map.h is current")
            return 0
        print("shell/generated_panel_map.h is STALE: run python shell/gen_panel_map.py",
              file=sys.stderr)
        return 1
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote shell/generated_panel_map.h")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
```

- [ ] **Step 4: Generate the header and run the guard**

```bash
python shell/gen_panel_map.py
```
```bash
python shell/test_gen_panel_map.py
```
Expected: `wrote shell/generated_panel_map.h`, then every line `ok`, then `all passed`.
- If the generator stops with `unclassified`, `not a pot` or `not in generated_hw_panel.hpp`: the spec's pot list and `panel-map.json` disagree. That is a STOP. Report the message verbatim; do not invent a classification.

Open `shell/generated_panel_map.h`. The `kRevaChain` initializer must read exactly:

```cpp
inline constexpr ChainProfile kRevaChain{
    4, kSenseAdcBase, 10,
    {8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
    {0, 0, 0, 1, 1, 1, 2, 2, 3, 3},
    40, 0, 3, 13, 19, -1, 3, true};
```

The calibration lines must read `kRevaCalZero{0, 6}` and `kRevaCalRail{1, 6}`, and the key line `kRevaKeys{4, {7, 6, 5, 4}}`. Quote all three in the report.

- [ ] **Step 5: Write the failing doctests**

Append to `tests/test_mux_plan.cpp`, below the Task 1 cases. Add `#include "../shell/generated_panel_map.h"` at the top.

```cpp
TEST_CASE("mux plan: the generated Rev A chain is P2's, field for field") {
    const shell::ChainProfile& p = shell::kRevaChain;
    const shell::ChainProfile& e = kRevaShape;
    CHECK(p.sense_pins == e.sense_pins);
    CHECK(p.sense_adc_base == e.sense_adc_base);
    CHECK(p.groups == e.groups);
    for(int g = 0; g < shell::kMaxGroups; ++g)
    {
        CHECK(p.channels[g] == e.channels[g]);
        CHECK(p.sense_of_group[g] == e.sense_of_group[g]);
    }
    CHECK(p.chain_bits == e.chain_bits);
    CHECK(p.addr_shift == e.addr_shift);
    CHECK(p.enable_shift == e.enable_shift);
    CHECK(p.led_shift == e.led_shift);
    CHECK(p.led_bits == e.led_bits);
    CHECK(p.button_bit == e.button_bit);
    CHECK(p.addr_bits == e.addr_bits);
    CHECK(p.parallel_sense == e.parallel_sense);
    CHECK(shell::scan_steps(p) == 24);
}

TEST_CASE("mux plan: Rev A's keys are D0..D3 of the 165") {
    REQUIRE(shell::kRevaKeys.count == 4);
    CHECK(shell::kRevaKeys.bit[0] == 7);
    CHECK(shell::kRevaKeys.bit[1] == 6);
    CHECK(shell::kRevaKeys.bit[2] == 5);
    CHECK(shell::kRevaKeys.bit[3] == 4);
}
```

Append to `tests/test_controls_map.cpp`. Add `#include <set>`, `#include <utility>`, `#include "../shell/generated_panel_map.h"` and `#include "../shell/mux_plan.h"` at the top.

```cpp
TEST_CASE("controls: the Rev A table has one row per pot") {
    CHECK(shell::kRevaTable.count == 70);
}

TEST_CASE("controls: every Rev A row is a channel the scan reads, on its own sense pin") {
    const shell::ChainProfile& p = shell::kRevaChain;
    for(int i = 0; i < shell::kRevaTable.count; ++i)
    {
        const shell::ControlEntry& e = shell::kRevaTable.entries[i];
        CAPTURE(i);
        REQUIRE(e.group >= 0);
        REQUIRE(e.group < p.groups);
        CHECK(e.sense == p.sense_of_group[e.group]);
        const int s = shell::step_of(p, e.group, e.ch);
        REQUIRE(s >= 0);
        CHECK(shell::group_at(p, s, e.sense) == e.group);
        CHECK(shell::channel_at(p, s, e.sense) == e.ch);
    }
}

TEST_CASE("controls: no two Rev A rows share an input, and none is a calibration channel") {
    // Review Focus 5.
    std::set<std::pair<int, int>> seen;
    for(int i = 0; i < shell::kRevaTable.count; ++i)
        seen.insert({shell::kRevaTable.entries[i].group, shell::kRevaTable.entries[i].ch});
    CHECK(static_cast<int>(seen.size()) == shell::kRevaTable.count);
    CHECK(shell::find_control(shell::kRevaTable, shell::kRevaCalZero.group,
                              shell::kRevaCalZero.ch) == nullptr);
    CHECK(shell::find_control(shell::kRevaTable, shell::kRevaCalRail.group,
                              shell::kRevaCalRail.ch) == nullptr);
}

TEST_CASE("controls: the Rev A table sends exactly the 35 safe parameters (spec 2.3)") {
    using namespace spky;
    const std::set<int> expected = {
        P_RATE_A, P_RATE_B, P_SHAPE_A, P_SHAPE_B, P_SMOOTH_A, P_SMOOTH_B,
        P_RANGE_A, P_RANGE_B, P_TUNE_A, P_TUNE_B, P_DECAY_A, P_DECAY_B,
        P_FILT_A, P_FILT_B, P_COLOR_A, P_COLOR_B, P_LINK_A, P_LINK_B,
        P_PAN_A, P_PAN_B, P_REVMIX_A, P_REVMIX_B, P_DEPTH_A, P_DEPTH_B,
        P_MORPH, P_TIDE, P_CHOKE, P_PULL, P_SHUFFLE, P_REV_SIZE, P_REV_DECAY,
        P_REV_TONE, P_REV_DIFF, P_PACE, P_SCALE};
    REQUIRE(expected.size() == 35);
    std::multiset<int> got;
    for(int i = 0; i < shell::kRevaTable.count; ++i)
        if(shell::kRevaTable.entries[i].param >= 0)
            got.insert(shell::kRevaTable.entries[i].param);
    CHECK(got.size() == 35);
    CHECK(std::set<int>(got.begin(), got.end()) == expected);
}
```

- [ ] **Step 6: Register the guard in `CMakeLists.txt`**

Insert directly after the `read_scan_check_guard` block:

```cmake
# The Rev A panel as the firmware sees it: shell/generated_panel_map.h is
# regenerated and compared, and every input check proves its RED
# (spec 2026-10-02-rev-a-p6a-panel-scan-design.md section 4).
add_test(NAME shell_panel_map_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_gen_panel_map.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)
```

- [ ] **Step 7: Build and run**

```bash
HOSTENV cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```
```bash
HOSTENV cmake --build build
```
```bash
HOSTENV ctest --test-dir build -R "spky_tests|shell_panel_map_guard" --output-on-failure
```
Expected: both PASS.

- [ ] **Step 8: Prove the doctest table check can go red**

In `shell/gen_panel_map.py`, temporarily move `"TUNE"` from `_PER_DECK_SAFE` to `_PER_DECK_UNMAPPED`. Regenerate with `python shell/gen_panel_map.py`, rebuild, and run `spky_tests`. Expected red: "sends exactly the 35 safe parameters".

Undo the edit with the editor, regenerate, rebuild, and confirm PASS. Also run `python shell/gen_panel_map.py --check` and confirm `is current`. Note the runs in the report.

- [ ] **Step 9: Commit**

```bash
git add shell/gen_panel_map.py shell/test_gen_panel_map.py shell/generated_panel_map.h tests/test_mux_plan.cpp tests/test_controls_map.cpp CMakeLists.txt
```
```bash
git commit -m "shell: generated Rev A panel map -- chain, 70 pot rows, 35 safe targets, keys, calibration

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: The play images on the new driver

**Files:**
- Modify: `shell/mux_scan.h`, `shell/mux_scan.cpp`, `shell/panel_scan.cpp` (whole file shown below), `shell/panel_scan.h` (comment)
- Modify: `shell/scan_value.h` (remove `kPanelSpan`), `tests/test_scan_value.cpp` (remove its test)
- Modify: `shell/controls.h` (remove `kPanelTable`), `tests/test_controls_map.cpp` (remove its test)
- Modify: `shell/Makefile` (source list, one dependency edge)

**Interfaces:**
- Consumes:
  - `kCouponPlayChain`, `group_at`, `channel_at`, `step_of`, `chain_word` (`uint64_t`) — Task 1;
  - `key_update`, `KeyState`, `kCouponKeys`, `panel_span`, `apply_control` (bool) — Task 2;
  - `kRevaChain`, `kRevaTable`, `kRevaCalZero`, `kRevaCalRail`, `kRevaKeys` — Task 3.
- Produces, in `MuxScan`:
  - `void set_leds(uint32_t)`, `void set_read_keys(bool)`, `uint32_t last_return() const`;
  - `write_chain` / `write_chain_timed` / `shift_chain_timed` take `uint64_t`;
  - `read_chain(uint64_t)` returns only the 165's 8 bits.

- [ ] **Step 1: Remove the part 1 placeholders and their tests**

- Delete `kPanelSpan` and its comment from `shell/scan_value.h`.
- Delete the `TEST_CASE("scan value: the panel span is the coupon's rail reading")` block from `tests/test_scan_value.cpp`.
- Delete `kPanelTable` from `shell/controls.h`.
- Delete the `TEST_CASE("controls: the panel table is empty until part 2")` block from `tests/test_controls_map.cpp`.
- In `controls.h`'s header comment, replace "The panel's table is empty: … in code." with:

```cpp
// Rev A's table is generated: shell/generated_panel_map.h, from
// hardware/reva/panel-map.json (spec 2026-10-02-rev-a-p6a-panel-scan-design.md).
```

- [ ] **Step 2: Change `shell/mux_scan.h`**

Replace the board-selection block:

```cpp
#include "shell_coupon_probe.h"
#include "mux_plan.h"
```
…
```cpp
#if SHELL_COUPON_PROBE
inline constexpr ChainProfile kActiveChain = kCouponChain;
#else
inline constexpr ChainProfile kActiveChain = kPanelChain;
#endif
```

with:

```cpp
#include "shell_coupon_probe.h"
#include "shell_panel_scan.h"
#include "mux_plan.h"
#if SHELL_PANEL_SCAN && !SHELL_COUPON_PROBE
#include "generated_panel_map.h"
#endif
```
…
```cpp
// Which board and which step model this image runs (spec
// 2026-10-02-rev-a-p6a-panel-scan-design.md section 3.2):
//   coupon probes      -- kCouponChain, sequential, exactly as measured
//   coupon play image  -- kCouponPlayChain: the coupon's wiring, Rev A's model
//   Rev A play image   -- kRevaChain, generated from the pin map
//   anything else      -- kPanelChain, the profile SHELL_MUX_PROBE priced
#if SHELL_COUPON_PROBE && SHELL_PANEL_SCAN
inline constexpr ChainProfile kActiveChain = kCouponPlayChain;
#elif SHELL_COUPON_PROBE
inline constexpr ChainProfile kActiveChain = kCouponChain;
#elif SHELL_PANEL_SCAN
inline constexpr ChainProfile kActiveChain = kRevaChain;
#else
inline constexpr ChainProfile kActiveChain = kPanelChain;
#endif
```

In `class MuxScan`, make these changes:
- Change the four chain signatures to `void write_chain(uint64_t word);`, `uint32_t write_chain_timed(uint64_t word);`, `uint32_t shift_chain_timed(uint64_t word);` and `uint32_t read_chain(uint64_t word);`.
- Above `read_chain`, add one comment line: `// Only the 165's eight stages are returned: with DS on GND everything after reads 0.`
- Add the public members:

```cpp
    // The LED field the next select() latches (spec section 3.3). It never
    // reaches the 595s on its own: LED bits change only in the latch that
    // carries the mux address (P2 section 4).
    void set_leds(uint32_t leds) { leds_ = leds; }

    // The play images read the keys in the same pass as every select()
    // (spec section 3.4); the probes keep the write-only pass they were
    // measured with.
    void     set_read_keys(bool on) { read_keys_ = on; }
    uint32_t last_return() const { return last_return_; }
```

and the private members:

```cpp
    bool        read_keys_   = false;
    uint32_t    last_return_ = 0xFFFFFFFFu;   // every key released
```

- [ ] **Step 3: Change `shell/mux_scan.cpp`**

1. In the three write loops and in `read_chain`, change the parameter type to `uint64_t word`. The loop bodies stay as they are; `((word >> i) & 1u)` is correct on a 64-bit word.
2. In `read_chain`, replace

```cpp
        if(sense_in_.Read())
            in |= 1u << (kActiveChain.chain_bits - 1 - i);
```
with
```cpp
        // Only the 165's eight stages carry anything: with DS on GND every
        // later bit reads 0 (P2 section 4), and on a 40-bit chain a shift
        // past 31 would be undefined.
        const int bit = kActiveChain.chain_bits - 1 - i;
        if(bit < 8 && sense_in_.Read()) in |= 1u << bit;
```
3. At the end of `MuxScan::init()`, add:

```cpp
    // P2 section 3: the enables are undefined until the first latch, so the
    // firmware latches first -- every mux disabled, every LED dark.
    write_chain(chain_word(kActiveChain, step_pattern(kActiveChain, -1), 0u));
```
4. Replace `MuxScan::select` with:

```cpp
void MuxScan::select(int step)
{
    const StepPattern p = step_pattern(kActiveChain, step);
    const uint64_t    w = chain_word(kActiveChain, p, leds_);
    // read_chain() raises the latch once before it shifts, for the 165's
    // parallel load; that edge re-latches the word already latched, so no
    // output moves, and the one latch that changes anything is still the
    // one carrying this step's address (spec section 3.3).
    if(read_keys_)
        last_return_ = read_chain(w);
    else
        write_chain(w);
    live_step_ = step;
}
```

- [ ] **Step 4: Replace `shell/panel_scan.cpp`**

```cpp
#include "panel_scan.h"

#include "shell_panel_scan.h"
#include "shell_coupon_probe.h"

#if SHELL_PANEL_SCAN

#include <atomic>

#include "controls.h"
#include "coupon_expect.h"
#include "keys.h"
#include "mux_scan.h"
#include "scan_value.h"
#if !SHELL_COUPON_PROBE
#include "generated_panel_map.h"
#endif

namespace shell {

namespace {

#if SHELL_COUPON_PROBE
constexpr ControlTable kTable = kCouponTable;
constexpr KeyPad       kKeys  = kCouponKeys;
#else
constexpr ControlTable kTable = kRevaTable;
constexpr KeyPad       kKeys  = kRevaKeys;
#endif

constexpr int kSteps    = scan_steps(kActiveChain);
constexpr int kChannels = mux_total(kActiveChain);

MuxScan   g_scan;
PotFilter g_filter[kChannels];
KeyState  g_keys;

// Last emitted value per channel, -1 = never emitted. Read by the
// foreground for SHELL_PLAY only.
volatile float g_value[kChannels];

// The span the value path uses. It starts invalid -- nothing reaches the
// engine before a sweep has measured one -- and an invalid sweep keeps the
// last valid span (spec 2026-10-02-rev-a-p6a-panel-scan-design.md 3.5).
Span g_span{0, 0, false};
#if SHELL_COUPON_PROBE
// The coupon's ties, in kCouponChain's step order: coupon_span() reads that
// order, and this image scans in kCouponPlayChain's (spec section 3.2).
constexpr int kCouponSteps = scan_steps(kCouponChain);
uint16_t      g_step_raw[kCouponSteps];
#else
uint16_t g_cal_zero = 0;
uint16_t g_cal_rail = 0;
#endif
volatile uint32_t g_sweeps = 0;

} // namespace

void panel_scan_init()
{
    g_scan.init();
    g_scan.set_walk_leds(false);
    g_scan.set_read_keys(true);
    for(int c = 0; c < kChannels; ++c) g_value[c] = -1.0f;
}

void panel_scan_tick(bench::Board& hw, spky::Instrument& inst)
{
    const int step = g_scan.step(hw);
    // The return stream of the latch step() just clocked: the 165 loads on
    // the same edge, so the keys are read once per block.
    key_update(g_keys, kKeys, g_scan.last_return());
#if SHELL_COUPON_PROBE
    // Key held, LED_1 lit: the key path, the LED field and the latch rule in
    // one gesture (spec section 3.7). It reaches the 595s with the NEXT
    // step's latch, never on its own.
    g_scan.set_leds((g_keys.pressed & 1u) != 0u ? 1u : 0u);
#endif
    if(step < 0) return;

    for(int s = 0; s < kActiveChain.sense_pins; ++s)
    {
        // A pin with no live channel this step floats; group_at() says so.
        const int g = group_at(kActiveChain, step, s);
        if(g < 0) continue;
        const int      ch  = channel_at(kActiveChain, step, s);
        const int      idx = mux_channel(kActiveChain, step, s);
        const uint16_t raw = g_mux_raw[idx];
#if SHELL_COUPON_PROBE
        g_step_raw[step_of(kCouponChain, g, ch)] = raw;
#else
        if(g == kRevaCalZero.group && ch == kRevaCalZero.ch) g_cal_zero = raw;
        if(g == kRevaCalRail.group && ch == kRevaCalRail.ch) g_cal_rail = raw;
#endif
        const ControlEntry* e = find_control(kTable, g, ch);
        if(e == nullptr) continue;
        float v;
        if(pot_filter(g_filter[idx], raw, g_span, kPotHysteresis, &v))
        {
            apply_control(*e, v, inst);   // refuses a row without a parameter
            g_value[idx] = v;
        }
    }

    if(step == kSteps - 1)
    {
#if SHELL_COUPON_PROBE
        const Span sp = coupon_span(g_step_raw, kCouponSteps);
#else
        const Span sp = panel_span(g_cal_zero, g_cal_rail);
#endif
        if(sp.valid) g_span = sp;
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
        const int keys  = g_keys.pressed;
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
        // A second line, so the first keeps part 1's format and each stays
        // inside libDaisy's 128-byte log buffer (LOGGER_BUFFER).
        hw.PrintLine("SHELL_PLAY_IO keys=%d presses=%d adc11=%d adc12=%d", keys,
                     static_cast<int>(g_keys.presses[0]),
                     static_cast<int>(hw.adc.Get(daisy::patch_sm::ADC_11)),
                     static_cast<int>(hw.adc.Get(daisy::patch_sm::ADC_12)));
#else
        // Ten values per line, in generated_panel_map.h's row order: seventy
        // in one line would overrun libDaisy's 128-byte log buffer.
        static_assert(kTable.count % 10 == 0, "SHELL_PLAY_V prints ten rows a line");
        for(int r0 = 0; r0 < kTable.count; r0 += 10)
        {
            int v[10];
            for(int i = 0; i < 10; ++i)
            {
                const ControlEntry& e    = kTable.entries[r0 + i];
                const int           step = step_of(kActiveChain, e.group, e.ch);
                const int           idx  = mux_channel(kActiveChain, step, e.sense);
                v[i] = static_cast<int>(g_value[idx] * 1000.0f);
            }
            hw.PrintLine("SHELL_PLAY_V r=%d %d %d %d %d %d %d %d %d %d %d", r0,
                         v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9]);
        }
        hw.PrintLine("SHELL_PLAY zero=%d rail=%d valid=%d sweeps=%d keys=%d "
                     "presses=%d,%d,%d,%d",
                     zero, rail, valid, static_cast<int>(g_sweeps), keys,
                     static_cast<int>(g_keys.presses[0]),
                     static_cast<int>(g_keys.presses[1]),
                     static_cast<int>(g_keys.presses[2]),
                     static_cast<int>(g_keys.presses[3]));
#endif
        hw.Delay(500);
    }
}

} // namespace shell

#endif // SHELL_PANEL_SCAN
```

- [ ] **Step 5: Update `shell/panel_scan.h`'s header comment**

Replace "On the coupon, RV2/RV4/RV6 drive RATE_A/DENSITY_A/FILT_A; on the panel profile the table is empty until part 2 and nothing is applied." with:

```cpp
// On the coupon, RV2/RV4/RV6 drive RATE_A/DENSITY_A/FILT_A over Rev A's step
// model (kCouponPlayChain); on Rev A, the 35 safe pots of the generated table
// drive their parameters (spec 2026-10-02-rev-a-p6a-panel-scan-design.md).
```

- [ ] **Step 6: Change `shell/Makefile`**

In `CPP_SOURCES`, add `keys.cpp \` directly after `controls.cpp \`. Then change the edge line

```make
$(BUILD_DIR)/mux_scan.o $(BUILD_DIR)/coupon_scan.o: $(BUILD_DIR)/shell_coupon_probe.h
```
to
```make
$(BUILD_DIR)/mux_scan.o $(BUILD_DIR)/coupon_scan.o: $(BUILD_DIR)/shell_coupon_probe.h $(BUILD_DIR)/shell_panel_scan.h
```

- [ ] **Step 7: Host tests**

```bash
HOSTENV cmake --build build
```
```bash
HOSTENV ctest --test-dir build -R "spky_tests|shell_panel_map_guard" --output-on-failure
```
Expected: PASS.

- [ ] **Step 8: Build every image the change touches, and record code space**

Run each command; copy the `SRAM_EXEC` row of each verbatim into the report:

```bash
ARMENV make -C shell -j8 images SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1
```
```bash
ARMENV make -C shell -j8 images SHELL_PANEL_SCAN=1
```
```bash
ARMENV make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_SCAN_CHECK=1
```
```bash
ARMENV make -C shell -j8 images SHELL_COUPON_PROBE=1
```
```bash
ARMENV make -C shell -j8 images SHELL_MUX_PROBE=1
```
Expected: all five link. Report a table with these columns: image, `SRAM_EXEC` at baseline (Task 1 Step 1, play images only), `SRAM_EXEC` now.

- [ ] **Step 9: Only if a play image does not link — the one fallback**

Apply this only when the linker reports `SRAM_EXEC` overflowed (spec §6).

In `engine/param_table.h`, change the `SPKY_INFO` line so the firmware drops the name strings:

```cpp
#if defined(SPKY_NO_PARAM_NAMES)
#define SPKY_INFO(id, lo_, hi_, st) { nullptr, lo_, hi_, st },
#else
#define SPKY_INFO(id, lo_, hi_, st) { #id, lo_, hi_, st },
#endif
```

In `shell/Makefile`, next to the other `C_DEFS`/`CPPFLAGS` additions, add `C_DEFS += -DSPKY_NO_PARAM_NAMES`. Before relying on it, confirm with `grep -rn "kParams\[.*\]\.name\|\.name" shell/*.cpp` that nothing in `shell/` reads `.name`. Rebuild Step 8's five images.

If a play image still does not link: **STOP** and report every `SRAM_EXEC` row. Do not shrink anything else.

- [ ] **Step 10: Commit**

```bash
git add shell/mux_scan.h shell/mux_scan.cpp shell/panel_scan.cpp shell/panel_scan.h shell/scan_value.h shell/controls.h shell/Makefile tests/test_scan_value.cpp tests/test_controls_map.cpp
```
If Step 9 was applied, also `git add engine/param_table.h`. Then:
```bash
git commit -m "shell: play images on the parallel scan -- Rev A table, keys through the 165, live calibration span

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: Documentation

**Files:**
- Modify: `shell/README.md` (new section; fix "three keycaps")
- Modify: `docs/roadmap.md` (new entry; fix "three keycaps")

**Interfaces:**
- Consumes: Task 4's `SRAM_EXEC` table, from its report.
- Produces: the board-session instructions Task 6 follows.

- [ ] **Step 1: Fix the key count**

In `shell/README.md` (around line 285) and `docs/roadmap.md` (around line 4100), change "the three keycaps on the 165" to "the four keys on the 165". Confirm with:
```bash
grep -n "three keycaps" shell/README.md docs/roadmap.md
```
Expected: no output.

- [ ] **Step 2: Add the README section**

Insert into `shell/README.md`, directly before `## Where the work stands, and where it goes next`:

```markdown
## Panel scan part 2: Rev A's pin map (P6a)

Spec: `docs/superpowers/specs/2026-10-02-rev-a-p6a-panel-scan-design.md`.

**The step model.** Rev A enables one 4051 per sense pin per step (P2 §3),
so a step reads up to four channels: 24 steps, 48 ms a sweep. `mux_plan.h`
calls this *parallel*. The coupon probes keep the *sequential* model they
were measured with (`kCouponChain`, unchanged). The coupon play image
(`SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1`) runs the coupon's wiring with
Rev A's model (`kCouponPlayChain`): the 4067 and the 4051 live together.

**The table is generated.** `python shell/gen_panel_map.py` writes
`shell/generated_panel_map.h` from `hardware/reva/panel-map.json`,
`hardware/reva/blocks.py` and `engine/param_table.h`. Never edit the header;
`shell_panel_map_guard` regenerates and compares it. 35 of the 70 pots send
a parameter — those whose VCV law is exactly `apply_param()`'s (spec §2). The
other 35 are scanned and printed, and wait for P6b's shared control layer.
A pot added to the panel stops the generator until it is classified.

**Keys and LEDs.** Every step reads the 165 in the same pass as the write;
keys debounce over three reads (6 ms). LED bits only ever travel in the latch
that carries the mux address (P2 §4). Keys have no function yet.

**The span** comes from CAL_GND and CAL_3V3 once per sweep; until a sweep has
measured a valid one, no knob reaches the engine.

**Reading it.** The coupon prints part 1's `SHELL_PLAY` line plus
`SHELL_PLAY_IO keys= presses= adc11= adc12=`. Rev A prints seven
`SHELL_PLAY_V r=<first row> <ten values>` lines (row order and names are in
the generated header's comments; values ×1000, −1000 = never moved) and one
`SHELL_PLAY` summary line with key mask and press counts.

**Coupon session** (spec §7) on the coupon play image:
1. All three pots reach both stops; at rest their printed values do not change.
2. Press SW1 five times: `presses=5`; LED_1 is lit while SW1 is held.
3. Hold SW1, pots untouched: `rv2`/`rv4`/`rv6` do not change.
4. Jumper `TP_ADC12` (D9) to `TP_AGND` and `TP_ADC11` (D8) to `TP_A3V3`:
   `adc11` reads near `zero`, `adc12` near `rail`. Swap the jumpers: they
   swap. (The coupon's test points carry its netlist's old D8/D9 names; P2 §2
   corrected them.)
```

- [ ] **Step 3: Add the roadmap entry**

Insert into `docs/roadmap.md` directly after the paragraph block that starts with `**2026-10-02 — P4-3 fabrication data`:

```markdown
**2026-10-02 — P6a panel scan over Rev A's pin map: built, the coupon session is open.**
Spec `docs/superpowers/specs/2026-10-02-rev-a-p6a-panel-scan-design.md`,
plan `docs/superpowers/plans/2026-10-02-rev-a-p6a-panel-scan.md`.
- `shell/` scans Rev A as P2 and P3 define it: ten 4051s, one per sense pin per step (24 steps), the 40-bit chain, the four keys on the 165, and a span from CAL_GND/CAL_3V3.
- `shell/gen_panel_map.py` generates the table. 35 of the 70 pots send a parameter: those whose VCV law is exactly `apply_param()`'s. The other 35 wait for P6b's shared control layer, which also carries the MOD layer.
- **Finding:** `apply_param()` is not VCV's control law. DEPTH, COMP, FLUX, GRIT and the engine-dependent knobs differ (spec §2.1).
- Code space (`SRAM_EXEC`, from the linker), coupon play and Rev A play images:

  | Image | Before | After |
  |---|---|---|
  | coupon play | <coupon play before> | <coupon play after> |
  | Rev A play | <Rev A play before> | <Rev A play after> |

  <one sentence: whether Step 9's fallback was needed>
- **Open:** the coupon session (spec §7) — parallel scan clean, keys and LED_1, LEDs switching under the scan, D8/D9 as ADC_12/ADC_11.
```

Take the four numbers from Task 4's report, as the linker printed them. Fill the fallback sentence from Task 4 Step 9: either "The `kParams` name fallback was not needed." or "The `kParams` name strings were dropped from the firmware (Task 4 Step 9)." No `<…>` may remain:
```bash
grep -n "<coupon play\|<Rev A play\|<one sentence" docs/roadmap.md
```
Expected: no output.

- [ ] **Step 4: Commit**

```bash
git add shell/README.md docs/roadmap.md
```
```bash
git commit -m "docs: P6a panel scan -- README section, roadmap entry with code space, four keys

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: Coupon session (controller with Bastian)

This task needs Bastian's hands at the coupon. The controller runs it after the final review. It is not dispatched to a subagent.

**Files:**
- Modify: `docs/hardware/scan-measured.md` (append a section)
- Modify: `docs/roadmap.md` (the P6a entry's "Open" line)

- [ ] **Step 1: Build the coupon play image and hand it over**

```bash
ARMENV make -C shell -j8 images SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1
```
Ask Bastian to:
- flash it with `dfu-util -a 0 -s 0x90040000:leave -D shell/build/shell-sram.bin` (the DFU path in `shell/README.md`);
- run the four checks in the README's "Coupon session" list;
- capture the serial log as in part 1's board session.

- [ ] **Step 2: Read the capture against the four checks**

1. `rv2`/`rv4`/`rv6` each reach 0 and 1000; in rest stretches they repeat exactly.
2. `presses` steps 0 → 5 over the five presses; `keys=1` while held.
3. During the hold, `rv2`/`rv4`/`rv6` repeat exactly.
4. With the jumpers as listed, `adc11` is within `kRailMargin` (1311) of `zero` and `adc12` within 1311 of `rail`; swapped, the reverse.

Any check that fails is reported verbatim with its log lines. No re-interpretation, and no change to the criterion.

- [ ] **Step 3: Record and commit**

- Append a section `## P6a coupon session (YYYY-MM-DD)` to `docs/hardware/scan-measured.md`, with the four results and the log lines that show them.
- In the roadmap P6a entry, replace the **Open** line with the result.

```bash
git add docs/hardware/scan-measured.md docs/roadmap.md
```
```bash
git commit -m "docs: P6a coupon session -- parallel scan, keys, LEDs under the scan, D8/D9

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```
