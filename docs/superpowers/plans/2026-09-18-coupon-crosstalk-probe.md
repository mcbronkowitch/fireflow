# Coupon Crosstalk Probe Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure how far a settled multiplexer channel on the test coupon moves when the board's own digital side fires one controlled event before the ADC's aperture, and report a verdict against half an LSB of 12 bit.

**Architecture:** The settle probe's ADC primitives move into a shared `probe_adc` translation unit, verbatim, and that move is proven behaviour-identical on the board before anything else is built. A second firmware image (`SHELL_XTALK_PROBE=1`) then reuses those primitives with a different word pair: the victim's mux address and enable are held fixed and only aggressor bits change, so the sample-and-hold's channel-change transient is not in the measurement at all. Everything decidable without hardware — the victim table, the case table, the chain-word construction, the four gates, the scan-settle arithmetic — lives in a hardware-free file the host test suite exercises.

**Tech Stack:** ARM GCC via `make` (libDaisy, STM32H7 HAL), C++17, doctest on the host through CMake/ctest, Python 3 + pyserial 3.5 for the reader.

**Spec:** [`docs/superpowers/specs/2026-09-18-coupon-crosstalk-probe-design.md`](../specs/2026-09-18-coupon-crosstalk-probe-design.md) — read it before Task 1. The plan argues from it throughout and cites section numbers.

**Predecessor:** [`docs/superpowers/plans/2026-09-17-coupon-settle-probe.md`](2026-09-17-coupon-settle-probe.md) built the instrument this one reuses. Its results are [`docs/hardware/settle-measured.md`](../../hardware/settle-measured.md); §1–§3 are the measured facts this probe inherits, and §5 is the pair of open observations it exists to attribute.

**Round two:** [`docs/superpowers/plans/2026-09-18-coupon-codec-tone-probe.md`](2026-09-18-coupon-codec-tone-probe.md) depends on every task here.

## Three decisions this plan makes that the spec did not

Stated up front rather than buried, because each is a place where the plan
and the spec differ and an executor will otherwise think one of them is wrong.

1. **`SHELL_XTALK` is split into two lines, not one.** Spec §8 prints the
   case's identity and its per-point statistics on a single line. That line
   runs about 150 characters, and libDaisy's log buffer is 128 bytes
   (`lib/libDaisy/src/hid/logger.h:29`, `LOGGER_BUFFER`); `Logger::TransmitBuf()`
   truncates past it and stamps the last two bytes `$$`. This is not
   hypothetical — `settle_probe.cpp`'s comment above `SHELL_SETTLE_KNEE`
   records the day it happened and the two fields it cost. So the identity
   fields move to a `SHELL_XTALK_CASE` line printed once per case, and
   `SHELL_XTALK` carries `case`, `d_ns`, `n`, `mean`, `min`, `max`. The field
   names are the spec's.
2. **`kScanSettleNs` is 2 000 000 ns and the whole grid lies before it.**
   Task 5 reads it from `shell/` rather than assuming it, and "How the
   verdict reads" below says what the reader does with a criterion boundary
   no grid point reaches.
3. **G5 builds its own span.** Spec §7 judges each victim against
   `coupon_expect`'s band "judged against the span the two ties give", but
   both 0 Ω victims are tied to `AGND` — there is no rail among them, and
   `coupon_span()` needs a whole scan's raw array, which this probe never
   takes. Task 5 reads four tie channels of its own (two A+3V3, two AGND) and
   builds the `Span` from them, reproducing `coupon_span()`'s semantics with
   four conversions instead of twenty-four.

## Global Constraints

- **Everything written into the repo is English** — code, comments, commit messages, docs. The conversation is German; the files are not. `shell/Makefile` and `shell/main.cpp` carry older German comment blocks. **Leave them alone.** New content is English; do not translate the neighbours and do not file the mismatch as a defect.
- **Never prefix a shell command with `cd`.** The Bash tool already starts in the repo root. Use `make -C shell`, not `cd shell && make`. A `cd`-compound cannot be auto-approved on this machine and prompts Bastian even for a read-only command.
- **The working copy carries uncommitted changes that are not yours** — everything under `hardware/coupon/`. **Never `git add -A`, never `git add .`, never `git commit -a`.** Stage the exact paths each task's commit step names and nothing else. Read `hardware/coupon/scripts/netlist.py` freely; write nothing there.
- **Two toolchains, never mixed in one task.** The firmware build must never `source env.sh`:
  ```bash
  PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_XTALK_PROBE=1
  ```
  `images`, not `all`. The host build needs `env.sh` and `-DCMAKE_BUILD_TYPE=Release`; Release is not optional, a Debug configure makes `spky_tests` and `ctrl_identity` fail with "SYNTH reference moved".
  ```bash
  source env.sh; cmake -S . -B build -DCMAKE_BUILD_TYPE=Release; cmake --build build; ctest --test-dir build --output-on-failure
  ```
- **`ctest` does not build.** A green run can be a stale binary. Always `cmake --build build` first.
- **`cmp` before you flash.** Make has one-second mtime resolution on this machine and has twice produced byte-identical images for two different switch positions. Every task that builds firmware compares the image it is about to flash against the one it replaced and refuses to proceed on "identical".
- **Commit trailer** is `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`, not the Anthropic default.
- **A test that cannot go red gets fixed.** Every task that adds a gate proves the RED once, by hand, and says in its report what it saw. Do not claim a RED you did not watch. Do not write a gate that passes without asserting — a `CHECK` inside a loop that never runs, a fixture that satisfies the assertion by construction, an assertion against a value the code under test just produced.
- **Never `git checkout <file>` to undo** a deliberate sabotage — retype the original. A scripted edit rewrites the whole file's line endings and the diff stays deceptively clean.
- **No runtime claim without a probe.** Every number in this plan that came from a model is labelled derived. Do not upgrade one to "measured" in a report because the code compiled. Numbers quoted from `settle-measured.md` are measured and are cited as such.
- **`ADC_CLOCK_ASYNC_DIV2` stays untouched**, and so does everything else in `adc_init()`. The measured 6.146 MHz of `settle-measured.md` §1 is what every duration in both probes is computed against.

---

## How the verdict reads

Spec §5: for every aggressor case and every victim, `|delta(d)| <= 8` counts
at every grid point at or past `kScanSettleNs`.

`kScanSettleNs` is **read from `shell/`** (Task 5, step 2), and it is the
audio block period, because `MuxScan::step()` reads the sense pins for the
step it wrote *last* time and only then clocks out the next address
(`shell/mux_scan.cpp:127-150`); `shell/mux_scan.h`'s header comment says so in
as many words. At the 96 samples / 48 kHz `src/hw/board.h:69-70` sets, that is
2 000 000 ns, against a grid that ends at 12 800 ns.

So **no grid point lies at or past the criterion boundary**, and a reader that
applied §5 literally would compute a verdict over an empty set and print
"pass". It must not. The reader takes one of two paths and says which:

- **`scan_settle_ns` falls inside the grid** — the verdict is §5 as written,
  over the points at or past it, and the row says `verdict_basis=criterion`.
- **`scan_settle_ns` falls past the end of the grid** (today's case) — the
  verdict is the envelope `max|delta(d)|` over the *whole* grid, and the row
  says `verdict_basis=envelope`. This is the more pessimistic of the two, and
  it is what spec §6 licenses: the shipping ADC free-runs a circular DMA, so a
  pot read lands at an arbitrary phase relative to any scan event and the
  envelope is the worst case it can meet. It is **not** a measurement at 2 ms
  and the reader never labels it as one.

Both paths are fixture-tested in Task 7. Neither is chosen by the firmware:
the firmware prints `scan_settle_ns` and the curves, and the reader decides.

---

## File Structure

| File | Responsibility |
|---|---|
| `shell/probe_adc.h` / `.cpp` | **New (Task 1).** The ADC primitives, moved verbatim out of `settle_probe.cpp`: `init()`, `warm_up()`, `select()`/`select_time()`, `sample_now()`, the two mean reducers, the clock-span pass and the arithmetic derived from it, the three HAL status flags, the timeout counter. Board-only; never compiled on the host. |
| `shell/settle_probe.cpp` | **Modify (Task 1).** Its file-local copies of the above are deleted and it calls `probe_adc::` instead. Nothing else changes, and the board proves it. |
| `shell/mux_plan.h` / `.cpp` | **Modify (Task 1).** Gains `step_of(profile, group, channel)`, which both probes need and which `settle_probe.cpp` carries as a file-local today. |
| `tests/test_mux_plan.cpp` | **Modify (Task 1).** Host assertions for `step_of()`. |
| `shell/xtalk_plan.h` / `.cpp` | **New (Tasks 2 and 3).** The victim table, the chain-word construction, the 58-entry case table, the scan-settle arithmetic, the four gates. Pure data and pure functions; no hardware type ever enters, same reason as `mux_plan.h` and `settle_plan.h`. |
| `tests/test_xtalk_plan.cpp` | **New (Tasks 2 and 3).** Host gate for the above. Recomputes rather than copies: every `r_src_ohm` is asserted against what `settle_plan.cpp` already carries for the same channel, so a typo fails instead of matching a typo. |
| `shell/write_shell_xtalk_probe.py` | **New (Task 4).** Switch-header generator, same shape and same reason as `write_shell_settle_probe.py`. |
| `shell/write_git_hash.py` | **New (Task 4).** Writes `SHELL_GIT_HASH` for spec §8's `git=` field, with a bounded-length dirty marker. |
| `shell/xtalk_probe.h` / `.cpp` | **New (Tasks 4, 5, 6).** The instrument. Board-only. |
| `shell/mux_scan.h` / `.cpp` | **Modify (Task 5).** Gains `shift_chain_timed()` — 16 bits clocked, no latch pulse, timestamped after the last `SRCLK` edge. |
| `shell/Makefile` | **Modify (Tasks 1 and 4).** `probe_adc.cpp` into `CPP_SOURCES`; then the `SHELL_XTALK_PROBE` and `SHELL_XTALK_RV4` switches, the two generators, the new stale-object entries. |
| `shell/main.cpp` | **Modify (Task 4).** Dispatch, alongside the existing probes. |
| `shell/read_xtalk.py` | **New (Task 7).** Collects one complete block from USB-CDC, writes `xtalk.csv` and `xtalk.csv.meta.csv`, computes `delta(d)` and the verdict. |
| `shell/test_read_xtalk.py` | **New (Task 7).** Host guard for the parser and the verdict arithmetic; no board and no pyserial needed. |
| `CMakeLists.txt` | **Modify (Tasks 2 and 7).** `xtalk_plan.cpp` + its test into `spky_tests`; `test_read_xtalk.py` as its own `add_test`. |

Seven tasks. Tasks 2, 3 and the host half of 7 need no board. Tasks 1, 4, 5,
6 and the last step of 7 need the coupon on USB and a human to press
BOOT+RESET.

**Task 1 is a gate on the whole plan.** No task after it may begin until its
step 9 has printed the comparison and the comparison held.

---

### Task 1: The ADC primitives move to `probe_adc`, and the board says nothing changed

The settle probe took ten fix rounds to reach the instrument
`settle-measured.md` describes. Moving its ADC primitives into a shared file
is behaviour-preserving by definition — and "by definition" is exactly the
kind of claim this repo does not accept without a probe. This task captures a
baseline from the *unmodified* image first, and the refactor is not done until
the refactored image reproduces it.

**Files:**
- Create: `shell/probe_adc.h`, `shell/probe_adc.cpp`
- Modify: `shell/settle_probe.cpp` (delete the moved code, call `probe_adc::`)
- Modify: `shell/mux_plan.h`, `shell/mux_plan.cpp` (add `step_of()`)
- Modify: `tests/test_mux_plan.cpp` (assert `step_of()`)
- Modify: `shell/Makefile` (`probe_adc.cpp` into `CPP_SOURCES`, `probe_adc.o` into `SWITCH_OBJECTS`)

**Interfaces:**
- Consumes: `bench::Board` from `src/hw/board.h`; `shell/cycles.h`; `shell/settle_plan.h` for `kSamplingLadderTenths` / `kSamplingLadderLen`.
- Produces: the `shell::probe_adc` namespace listed in step 4, and `int shell::step_of(const ChainProfile& p, int group, int ch)`. Tasks 5 and 6 and the whole codec-tone plan build on these exact names.

- [ ] **Step 1: Capture the baseline from the unmodified image**

There is **no stored settle capture in this repo** — `settle-measured.md`
carries the numbers, not the lines. So the comparison's left-hand side has to
be taken now, before a character of `settle_probe.cpp` moves.

Confirm `shell/` is clean (`hardware/coupon/` is expected to be dirty and is
not yours):

```bash
git status --short -- shell tests CMakeLists.txt
```

Expected: no output. If `shell/` is dirty, stop and report — the baseline
would not be a baseline.

Build and flash the settle image as it stands:

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_SETTLE_PROBE=1
```

Keep the image; it is half of step 8's `cmp`:

```bash
cp shell/build/shell-sram.bin /tmp/settle-before.bin
```

Ask the operator for DFU (hold BOOT, tap RESET, release BOOT), then:

```bash
dfu-util -a 0 -s 0x90040000:leave -D shell/build/shell-sram.bin
```

Find the port:

```bash
python -c "from serial.tools import list_ports; [print(p.device) for p in list_ports.comports()]"
```

Capture the raw stream — six blocks' worth, which is the number
`settle-measured.md` §4 and §5 used, and the spread across them is what makes
a one-block difference readable as a difference rather than as scatter:

```bash
python -c "import serial,sys; ser=serial.Serial(sys.argv[1],timeout=1.0); [sys.stdout.write(ser.readline().decode('utf-8','replace')) for _ in range(4000)]" COM4 > /tmp/settle-before.log
```

Keep exactly these line kinds for the comparison, and put them in the task
report verbatim:

```bash
grep -E "SHELL_SETTLE_(CFG|CLK|CAL|OFFSET|GATES) " /tmp/settle-before.log | sort | uniq -c
```

Spec §3 names `_CLK`, `_CAL` and `_GATES`. `_CFG` and `_OFFSET` are added
here because `_CFG` carries `adc_khz` and `_OFFSET` carries `offset_ns` and
`smp_tenths` per pair — and the offset arithmetic is precisely the part of the
move that is derived rather than copied. Leaving it out would drop the one
line that would show the move's only real risk.

Cross-check the capture against `settle-measured.md` §1–§3 before trusting it
as a baseline, and say in the report whether each held:

- `SHELL_SETTLE_CFG`'s `adc_khz` should read **6146** (§1).
- `SHELL_SETTLE_OFFSET` should read **991 ns** at `smp_tenths=15` and
  **1154 ns** at `smp_tenths=25` (§1).
- `SHELL_SETTLE_GATES` should read `g1=1 g2=1 g3=0 g4=1` — **G3 fails, and
  that is the known, documented state of this instrument** (§3, §5). A run in
  which G3 passes is a different board state and the baseline is not
  comparable; report it and stop.
- `SHELL_SETTLE_CAL`'s `lat_max_ns - lat_min_ns` should fall in the
  **42…138 ns** band §2 records across captures.

- [ ] **Step 2: Add `step_of()` to the chain profile**

`settle_probe.cpp` carries this as a file-local, and `xtalk_probe.cpp` would
need an identical copy. Two copies of the arithmetic that maps a
`(group, channel)` pair onto a scan step, for two instruments reading the same
board, is how the two silently disagree. It belongs in `mux_plan.h` beside
`step_pattern()`, where it is pure and the host can assert it.

In `shell/mux_plan.h`, after `mux_channel()`'s declaration:

```cpp
// The scan step that selects channel `ch` on group `group`: group 0's
// channels occupy the start of the step space, group 1's follow all of
// group 0's -- the same layout step_pattern() and group_of_step() walk.
//
// Out of range returns -1, for the same reason mux_channel() does: a
// half-seated chip produces indices nobody planned, and an out-of-range
// address would still select SOME channel and hand back a foreign knob's
// voltage, which is worse than reading nothing. The two groups do NOT have
// the same channel count on the coupon (16 and 8), so the bound has to be
// the group's own.
int step_of(const ChainProfile& p, int group, int ch);
```

In `shell/mux_plan.cpp`, after `group_of_step()`:

```cpp
int step_of(const ChainProfile& p, int group, int ch)
{
    if(group < 0 || group >= p.groups) return -1;
    if(ch < 0 || ch >= p.channels[group]) return -1;
    int step = ch;
    for(int g = 0; g < group; ++g) step += p.channels[g];
    return step;
}
```

- [ ] **Step 3: Assert `step_of()` on the host, and watch it fail first**

Append to `tests/test_mux_plan.cpp`:

```cpp
TEST_CASE("mux plan: step_of is the inverse of group_of_step") {
    // Not a restatement of the implementation: this walks every step of both
    // profiles, asks which group and channel it is, and requires step_of()
    // to hand the same step back. A sign error or an off-by-one in either
    // direction breaks the round trip.
    for(const shell::ChainProfile* p : {&shell::kPanelChain, &shell::kCouponChain}) {
        for(int s = 0; s < shell::scan_steps(*p); ++s) {
            const int g = shell::group_of_step(*p, s);
            REQUIRE(g >= 0);
            int ch = s;
            for(int i = 0; i < g; ++i) ch -= p->channels[i];
            CAPTURE(s);
            CHECK(shell::step_of(*p, g, ch) == s);
        }
    }
}

TEST_CASE("mux plan: step_of answers out of range instead of assuming") {
    CHECK(shell::step_of(shell::kCouponChain, -1, 0) == -1);
    CHECK(shell::step_of(shell::kCouponChain, 2, 0) == -1);
    CHECK(shell::step_of(shell::kCouponChain, 0, -1) == -1);
    // The coupon's two groups are 16 and 8 channels, so channel 8 exists on
    // group 0 and does not exist on group 1. A shared bound would pass on
    // group 0 and hand back step 24 on group 1, past the end of the step
    // space.
    CHECK(shell::step_of(shell::kCouponChain, 0, 8) == 8);
    CHECK(shell::step_of(shell::kCouponChain, 1, 8) == -1);
    CHECK(shell::step_of(shell::kCouponChain, 1, 7) == 23);
}
```

**The RED, if the test is written before step 2:** a compile failure,
`'step_of' is not a member of namespace 'shell'`, because the declaration does
not exist. Run:

```bash
source env.sh; cmake --build build
```

**The RED, if step 2 is already in:** change `step += p.channels[g]` to
`step += p.channels[group]` and run

```bash
source env.sh; cmake --build build; ./build/spky_tests.exe -tc="mux plan: step_of is the inverse*"
```

Expected: FAILURE on the coupon profile at `s = 16` (group 1, channel 0). The
wrong subscript adds group 1's 8 channels instead of group 0's 16, so
`step_of` returns 8 where 16 was asked for. The panel profile cannot catch it
at all — both its groups have 16 channels — which is exactly why the loop runs
over both. **Retype** the original line; do not `git checkout`. Rebuild,
confirm green, report what you saw.

- [ ] **Step 4: Write `probe_adc.h`**

Create `shell/probe_adc.h`:

```cpp
#pragma once

// The ADC primitives both coupon probes drive ADC1 with. Extracted verbatim
// from settle_probe.cpp, which reached this shape over ten hardware fix
// rounds -- every comment that came with a fix came with it, because each one
// records a failure that cost a board session to find.
//
// Why a shared file rather than a second mode in settle_probe.cpp: that file
// is 1028 lines and its knee search, its tail reference and its gates G1 and
// G3 are the settle question's statistics, every one of which is the wrong
// question for a crosstalk measurement. Crosstalk spec section 3.
//
// Board-only. Never compiled on the host: it holds HAL types, which is
// exactly why settle_plan.h and xtalk_plan.h hold none.
#include <cstdint>

#include "hw/board.h"

namespace shell {
namespace probe_adc {

// sample_now() writes this into *span_cycles when the EOC poll hit its bound.
// It is not a duration and must never be averaged into one.
inline constexpr uint32_t kTimeoutSentinel = 0xFFFFFFFFu;

// The ADC1 channel number behind each of the coupon's two sense pins.
// group 0 = the 4067 on ADC_9, group 1 = the 4051 on ADC_10.
uint32_t channel_of_group(int group);

// The HAL ADC_SAMPLETIME_* constant at index `rung` of settle_plan.h's
// kSamplingLadderTenths. Out of range clamps to the longest rung, matching
// sample_time_index_for()'s own out-of-range answer.
uint32_t sample_time_for_rung(int rung);

// Takes ADC1 away from libDaisy and configures it single-shot, 16 bit, no
// oversampling, no DMA. Calibrates ONCE. Must run before anything else here.
void init(bench::Board& hw);

// Runs the first conversion to completion so no later timed sample pays the
// one-time enable cost. Must run AFTER a select(). Returns false on timeout:
// a probe that cannot start must say it cannot start.
bool warm_up();

void select_time(uint32_t channel, uint32_t sampling_time);
void select(uint32_t channel);   // the working rung

// One conversion, interrupt-masked across the timed window, EOC poll bounded.
// Writes the start-to-EOC span in DWT cycles if asked.
uint16_t sample_now(uint32_t* span_cycles);

// Mean value / mean span over `repeats` conversions on whatever channel and
// sampling time is currently selected. Timed-out repeats are excluded from
// the span reducer and counted; -1 comes back when every repeat timed out.
int32_t mean_of_repeats(int repeats);
int32_t mean_span_of_repeats(int repeats);

// Lifetime count of timed-out conversions, and the three HAL statuses this
// file would otherwise throw away. None of them is a gate; they are printed
// so a run whose ADC was never set up does not pass as plausible integers.
uint32_t timeouts();
bool     init_ok();
bool     cal_ok();
bool     cfg_ok();

// This boot's measured ADC clock and everything derived from it. The clock is
// MEASURED every boot and never assumed: settle-budget.md section 1 derived
// 12.29 MHz from PLL3's dividers, the board runs at 6.146 MHz -- exactly half
// -- and that wrong constant cost three fix rounds chasing a latency that was
// never in software.
struct Clock
{
    int32_t span_short_cyc;          // mean span at the working sampling rung
    int32_t span_long_cyc;           // mean span at the 387.5-cycle rung
    int32_t working_conversion_ns;   // 25 ADC cycles at the measured clock
    int32_t measured_adc_khz;
    double  core_cyc_per_adc_cyc;
    double  pre_adstart_overhead_core_cyc;
    bool    ok;                      // false -> every derived field reads -1
};

// Two spans on the SAME parked channel, identical in every way except the
// configured sampling time, so every fixed cost cancels in the difference and
// only the extra 371 ADC cycles remain. Leaves the working sampling time
// selected, which every later pass needs.
Clock measure_clock(int repeats, uint32_t channel);

// pre-ADSTART overhead plus that rung's sampling window, in ns, or -1 when
// the clock pass came back invalid.
//
// The sampling window and NOT the conversion cycles: the sample-and-hold is
// acquired at the END of the window, and the conversion cycles that follow
// only digitise what is already captured, so they do not delay the instant
// that needs the node to have settled.
int32_t offset_ns_for_rung(const Clock& c, int rung);

} // namespace probe_adc
} // namespace shell
```

- [ ] **Step 5: Move the bodies into `probe_adc.cpp`**

Create `shell/probe_adc.cpp`. **Cut and paste**, do not retype and do not
improve. Every block comment moves with its code. From
`shell/settle_probe.cpp`, move:

| From `settle_probe.cpp` | Becomes |
|---|---|
| `kAdcChannelGroup0/1` and their citation comment, `channel_of_group()` | `probe_adc::channel_of_group()` |
| `g_adc`, `g_adc_init_ok/cal_ok/cfg_ok` and their comment, `adc_init()` | file-static in `probe_adc.cpp`; `probe_adc::init()`, `init_ok()`, `cal_ok()`, `cfg_ok()` |
| `kSampleTimeWorking`, `kSampleTimeLong`, `kSampleTimeByRung[]` | file-static; `probe_adc::sample_time_for_rung()` |
| `adc_select_time()`, `adc_select()` | `probe_adc::select_time()`, `probe_adc::select()` |
| `kPollTimeoutCycles`, `kTimeoutSentinel`, `g_adc_timeouts`, `sample_now()` and the whole block comment above it | `probe_adc::sample_now()`, `probe_adc::timeouts()`; `kTimeoutSentinel` becomes the header constant |
| `adc_warm_up()` | `probe_adc::warm_up()` |
| `mean_of_repeats()`, `mean_span_of_repeats()` | the same names under `probe_adc::` |

`sample_time_for_rung()` is the one line that is new rather than moved:

```cpp
uint32_t sample_time_for_rung(int rung)
{
    // Clamped, not asserted, and clamped to the LONGEST rung: that is the
    // answer settle_plan.cpp's sample_time_index_for() already gives for an
    // impedance no rung covers, so the two agree at the edge instead of one
    // of them reading past the end of an array inside a timed path.
    if(rung < 0) rung = 0;
    if(rung >= kSamplingLadderLen) rung = kSamplingLadderLen - 1;
    return kSampleTimeByRung[rung];
}
```

Then `measure_clock()` and `offset_ns_for_rung()`, which are the clock pass
and its arithmetic lifted out of `run_settle_probe()`'s body. Copy the
expressions **character for character** from `settle_probe.cpp`. Step 9 makes
a drift visible, but a drift introduced on purpose by tidying is one you then
have to find on a board:

```cpp
Clock measure_clock(int repeats, uint32_t channel)
{
    Clock c{};

    // Selected explicitly rather than relying on the caller having done it.
    // settle_probe.cpp reached this pass with the working time already
    // selected by its own adc_select(); saying so here makes the pass
    // self-contained without changing what runs.
    select_time(channel, kSampleTimeWorking);
    c.span_short_cyc = mean_span_of_repeats(repeats);

    select_time(channel, kSampleTimeLong);
    c.span_long_cyc = mean_span_of_repeats(repeats);

    select_time(channel, kSampleTimeWorking);   // restore

    c.ok = c.span_short_cyc > 0 && c.span_long_cyc > c.span_short_cyc;
    c.core_cyc_per_adc_cyc =
        c.ok ? static_cast<double>(c.span_long_cyc - c.span_short_cyc) / 371.0
             : 0.0;

    // 16.5 sampling + 8.5 conversion = 25 ADC cycles.
    constexpr double kWorkingTotalAdcCycles = 25.0;
    const double working_conversion_core_cyc =
        kWorkingTotalAdcCycles * c.core_cyc_per_adc_cyc;
    c.working_conversion_ns =
        c.ok ? static_cast<int32_t>(cycles_to_ns(static_cast<uint32_t>(
                   working_conversion_core_cyc + 0.5)))
             : -1;
    c.measured_adc_khz =
        c.ok ? static_cast<int32_t>(480000.0 / c.core_cyc_per_adc_cyc + 0.5) : -1;
    c.pre_adstart_overhead_core_cyc =
        c.ok ? (static_cast<double>(c.span_short_cyc) - working_conversion_core_cyc)
             : 0.0;
    return c;
}

int32_t offset_ns_for_rung(const Clock& c, int rung)
{
    if(!c.ok) return -1;
    if(rung < 0) rung = 0;
    if(rung >= kSamplingLadderLen) rung = kSamplingLadderLen - 1;
    const double sampling_window_core_cyc =
        (static_cast<double>(kSamplingLadderTenths[rung]) / 10.0)
        * c.core_cyc_per_adc_cyc;
    return static_cast<int32_t>(cycles_to_ns(static_cast<uint32_t>(
        c.pre_adstart_overhead_core_cyc + sampling_window_core_cyc + 0.5)));
}
```

`probe_adc.cpp` needs `#include "probe_adc.h"`, `#include "cycles.h"`,
`#include "settle_plan.h"` and `#include <stm32h7xx_hal.h>` — the last one
with the same comment `settle_probe.cpp` carries: raw HAL, not libDaisy's
`AdcHandle`, because `AdcHandle::Start()` recalibrates.

- [ ] **Step 6: Rewire `settle_probe.cpp`**

Delete everything step 5 moved. Add `#include "probe_adc.h"`. Replace the call
sites; nothing else in the file changes:

- `adc_init(hw)` → `probe_adc::init(hw)`
- `adc_select(x)` → `probe_adc::select(x)`
- `adc_select_time(x, t)` → the clock pass only, and that is now `measure_clock()`
- `sample_now(p)` → `probe_adc::sample_now(p)`
- `adc_warm_up()` → `probe_adc::warm_up()`
- `mean_of_repeats(n)` / `mean_span_of_repeats(n)` → `probe_adc::…`
- `channel_of_group(g)` → `probe_adc::channel_of_group(g)`
- `kTimeoutSentinel` → `probe_adc::kTimeoutSentinel`
- `g_adc_timeouts` → `probe_adc::timeouts()`
- `g_adc_init_ok` / `g_adc_cal_ok` / `g_adc_cfg_ok` → `probe_adc::init_ok()` / `cal_ok()` / `cfg_ok()`
- the file-local `step_of(group, ch)` → `step_of(kCouponChain, group, ch)`, and delete the local

The clock pass and the offsets collapse to:

```cpp
    const probe_adc::Clock clk
        = probe_adc::measure_clock(kRepeats, probe_adc::channel_of_group(p0.group));
    const bool    has_clk               = clk.ok;
    const int32_t working_conversion_ns = clk.working_conversion_ns;
    const int32_t measured_adc_khz      = clk.measured_adc_khz;

    int32_t rung_idx[kSettlePairs];
    int32_t offset_ns[kSettlePairs];
    for(int p = 0; p < kSettlePairs; ++p)
    {
        rung_idx[p]  = sample_time_index_for(kSettlePlan[p].r_src_ohm);
        offset_ns[p] = probe_adc::offset_ns_for_rung(clk, rung_idx[p]);
    }
```

and `SHELL_SETTLE_CLK`'s two span fields come from `clk.span_short_cyc` /
`clk.span_long_cyc`. **Every `hw.PrintLine()` format string and every argument
order stays byte-identical.** The comparison in step 9 is only worth anything
if the lines are the same lines.

- [ ] **Step 7: Wire `probe_adc.cpp` into the firmware build**

In `shell/Makefile`, in `CPP_SOURCES`, directly before `settle_probe.cpp`:

```make
	probe_adc.cpp \
```

And in `SWITCH_OBJECTS`, add `$(BUILD_DIR)/probe_adc.o`:

```make
SWITCH_OBJECTS = $(BUILD_DIR)/main.o $(BUILD_DIR)/mux_scan.o $(BUILD_DIR)/coupon_scan.o $(BUILD_DIR)/settle_probe.o $(BUILD_DIR)/probe_adc.o
```

`probe_adc.cpp` reads no switch header today — it includes `hw/board.h` and
the HAL and nothing else. It is listed anyway: the cost of an unnecessary
entry is one recompile, the cost of a missing one is an image built against
the wrong switch that looks exactly like a measurement, and that trap has
fired twice on this machine.

- [ ] **Step 8: Build, and prove the rebuild reached the image**

Host first — `probe_adc.cpp` is not in `spky_tests`, but `mux_plan.cpp` is,
and step 3's assertions have to be green:

```bash
source env.sh; cmake -S . -B build -DCMAKE_BUILD_TYPE=Release; cmake --build build; ctest --test-dir build --output-on-failure
```

Expected: every test passes, including the two new `step_of` cases.

Then the firmware, in a shell that has **not** sourced `env.sh`:

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_SETTLE_PROBE=1
```

```bash
cmp shell/build/shell-sram.bin /tmp/settle-before.bin
```

**Expected: the files differ.** Identical means make reused objects it should
have rebuilt, and the board would then reproduce the baseline for the trivial
reason that it is still running the baseline. Do not proceed past an identical
`cmp`; remove `shell/build/` and rebuild.

- [ ] **Step 9: Flash, capture, and compare — the gate on the whole plan**

Flash as in step 1 and capture the same way:

```bash
python -c "import serial,sys; ser=serial.Serial(sys.argv[1],timeout=1.0); [sys.stdout.write(ser.readline().decode('utf-8','replace')) for _ in range(4000)]" COM4 > /tmp/settle-after.log
```

```bash
grep -E "SHELL_SETTLE_(CFG|CLK|CAL|OFFSET|GATES) " /tmp/settle-after.log | sort | uniq -c
```

Compare against step 1, field by field, and put the table in the report:

| Field | Requirement |
|---|---|
| `SHELL_SETTLE_CLK` `span_short_cyc`, `span_long_cyc` | within **1 %** of the baseline. These are 64-repeat means of a hardware span; they are not bit-reproducible and were never expected to be. |
| `SHELL_SETTLE_CFG` `adc_khz` | **identical**, and 6146. It is an integer derived from the two spans, and 1 % on the spans is well inside one kHz here. |
| `SHELL_SETTLE_OFFSET` `offset_ns`, `smp_tenths` | **identical** for all six pairs — 991 / 1154 ns at rungs 15 / 25. These are integers out of the same arithmetic; anything else means the offset computation drifted in the move, which is the one part of it that is derived rather than copied. |
| `SHELL_SETTLE_CAL` `lat_mean_ns`, `lat_min_ns`, `lat_max_ns` | the same *quantity*, not the same number: `lat_max - lat_min` inside the 42…138 ns band `settle-measured.md` §2 records, `lat_mean_ns` positive and within 10 % of the baseline's. |
| `SHELL_SETTLE_GATES` `g1 g2 g3 g4`, `init_ok cal_ok cfg_ok` | **identical**, and `1 1 0 1` / `1 1 1`. G3 failing is the documented state of this instrument (§3, §5), not a regression this task caused. |

**If any "identical" row is not identical, the refactor is not
behaviour-preserving and this task is not done.** Report the two lines side by
side and stop. Do not adjust a constant to make them match.

- [ ] **Step 10: Commit**

```bash
git add shell/probe_adc.h shell/probe_adc.cpp shell/settle_probe.cpp shell/mux_plan.h shell/mux_plan.cpp tests/test_mux_plan.cpp shell/Makefile
git commit -m "refactor(shell): the ADC primitives move out of the settle probe

Cut and pasted, not rewritten: settle_probe.cpp reached this shape over ten
hardware fix rounds and every comment that came with a fix came with it. Only
the clock pass changed shape, into measure_clock(), and only because the
second probe needs the same two spans measured the same way.

Proven on the board rather than argued. A baseline block was captured from
the unmodified image first, and the refactored image reproduces its
SHELL_SETTLE_CLK, _CFG, _CAL, _OFFSET and _GATES lines -- the offsets and the
gate verdicts to the integer, the spans to within one percent. G3 still
fails, which is the documented state of this instrument and not this commit.

step_of() moves to mux_plan.h because the crosstalk probe needs the same
arithmetic, and two copies of it for two instruments reading one board is how
the two silently disagree.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: The victims, the chain words, and the case table

Pure data, host-tested, no board. Spec §2 (what can be pitted against what)
and §4 (the plan table).

**Files:**
- Create: `shell/xtalk_plan.h`, `shell/xtalk_plan.cpp`
- Create: `tests/test_xtalk_plan.cpp`
- Modify: `CMakeLists.txt` (line 98, directly after `tests/test_settle_plan.cpp`)

**Interfaces:**
- Consumes: `shell/mux_plan.h` (`kCouponChain`, `chain_word()`, `StepPattern`, `step_of()`), `shell/settle_plan.h` (`kSettlePlan`, `kSettleCounts`, `kRepeats`, `kGridStepNs`, `kGridPoints`, `kParkNs`, `Point`).
- Produces: `XtalkKind`, `XtalkVictim`, `kXtalkVictimTable[kXtalkVictims]`, `XtalkCase`, `kXtalkPlan[kXtalkCases]`, `address_bits_of_group()`, `victim_address_mask()`, `victim_enable_mask()`, `other_enable_mask()`, `xtalk_word()`, `scan_settle_ns()`. Task 3 extends the same two files; Tasks 5, 6 and 7 consume these names.

- [ ] **Step 1: Write the failing test**

Create `tests/test_xtalk_plan.cpp`. Note what it does **not** do: it does not
copy the spec's impedance column. Every `r_src_ohm` is asserted against what
`settle_plan.cpp` already carries for the same `(group, channel)`, so a typo
in one table fails instead of matching a typo in the other. All five victims
appear as a `to_ch` in `kSettlePlan`, which is what makes that possible.

```cpp
// The crosstalk probe's victims, aggressors and chain words. Derived from
// the crosstalk spec sections 2 and 4 and from
// hardware/coupon/scripts/netlist.py; if the two disagree, the netlist wins
// and this file is wrong.
#include <doctest/doctest.h>
#include "../shell/xtalk_plan.h"
#include "../shell/settle_plan.h"
#include "../shell/mux_plan.h"

namespace {

// The r_src_ohm settle_plan.cpp carries for a (group, channel), or 0 if that
// channel is not a target there. Not a copy of the impedance column -- a
// lookup into the table that already passed its own host gate.
uint32_t settle_r_src(int group, int ch) {
    for(int p = 0; p < shell::kSettlePairs; ++p) {
        const shell::SettlePair& sp = shell::kSettlePlan[p];
        if(sp.group == group && sp.to_ch == ch) return sp.r_src_ohm;
    }
    return 0u;
}

int victim_index_of(const shell::XtalkCase& c) {
    for(int v = 0; v < shell::kXtalkVictims; ++v) {
        const shell::XtalkVictim& vv = shell::kXtalkVictimTable[v];
        if(vv.group == c.group && vv.channel == c.channel) return v;
    }
    return -1;
}

} // namespace

TEST_CASE("xtalk victims: every victim exists and carries the settle plan's impedance") {
    for(int v = 0; v < shell::kXtalkVictims; ++v) {
        const shell::XtalkVictim& vv = shell::kXtalkVictimTable[v];
        CAPTURE(v);
        REQUIRE((vv.group == 0 || vv.group == 1));
        CHECK(vv.channel >= 0);
        CHECK(vv.channel < shell::kCouponChain.channels[vv.group]);
        // The cross-check that makes the two tables one table.
        const uint32_t r = settle_r_src(vv.group, vv.channel);
        REQUIRE(r != 0u);
        CHECK(vv.r_src_ohm == r);
    }
}

TEST_CASE("xtalk victims: the impedance ladder is the attribution axis") {
    // Spec section 2: two 5150 ohm dividers (one per mux), one 650 ohm
    // divider as the impedance control, and two 0 ohm ties as the
    // instrument's zero -- one per mux. Without one zero per mux a delta on
    // the 4051 has nothing to be judged against.
    int at_5150 = 0, at_650 = 0, ties = 0, ties_g0 = 0, ties_g1 = 0;
    for(int v = 0; v < shell::kXtalkVictims; ++v) {
        const shell::XtalkVictim& vv = shell::kXtalkVictimTable[v];
        if(vv.r_src_ohm == 5150u) ++at_5150;
        else if(vv.r_src_ohm == 650u) ++at_650;
        else if(vv.r_src_ohm == 150u) {
            ++ties;
            if(vv.group == 0) ++ties_g0; else ++ties_g1;
        }
    }
    CHECK(at_5150 == 2);
    CHECK(at_650 == 1);
    CHECK(ties == 2);
    CHECK(ties_g0 == 1);
    CHECK(ties_g1 == 1);
}

TEST_CASE("xtalk words: a word selects its victim and disables the other mux by default") {
    for(int v = 0; v < shell::kXtalkVictims; ++v) {
        const shell::XtalkVictim& vv = shell::kXtalkVictimTable[v];
        CAPTURE(v);
        const uint32_t w = shell::xtalk_word(vv, shell::other_enable_mask(vv.group));
        // The victim's own enable is LOW: enables are active low, so the
        // victim's mux is the one that is on.
        CHECK((w & shell::victim_enable_mask(vv.group)) == 0u);
        // The other mux's enable is HIGH: off.
        CHECK((w & shell::other_enable_mask(vv.group)) != 0u);
        // The address bits the victim's mux actually reads carry its channel.
        const uint32_t addr = (w & shell::victim_address_mask(vv.group))
                              >> shell::kCouponChain.addr_shift;
        CHECK(addr == static_cast<uint32_t>(
                  vv.channel & ((1 << shell::address_bits_of_group(vv.group)) - 1)));
    }
}

TEST_CASE("xtalk words: the 4051 reads three address lines, the 4067 four") {
    // netlist.py:163, "the 8:1 uses three of the four". This is the whole
    // reason MUX_A3 can be an aggressor against a victim on the 4051 and
    // against nothing else, so it is asserted rather than assumed.
    CHECK(shell::address_bits_of_group(0) == 4);
    CHECK(shell::address_bits_of_group(1) == 3);
    CHECK(shell::victim_address_mask(0) == (0x0Fu << shell::kCouponChain.addr_shift));
    CHECK(shell::victim_address_mask(1) == (0x07u << shell::kCouponChain.addr_shift));
    CHECK(shell::victim_enable_mask(0) == (1u << shell::kCouponChain.enable_shift));
    CHECK(shell::victim_enable_mask(1) == (2u << shell::kCouponChain.enable_shift));
    CHECK(shell::other_enable_mask(0) == shell::victim_enable_mask(1));
    CHECK(shell::other_enable_mask(1) == shell::victim_enable_mask(0));
}

TEST_CASE("xtalk plan: every case names a real victim and a real channel") {
    for(int i = 0; i < shell::kXtalkCases; ++i) {
        const shell::XtalkCase& c = shell::kXtalkPlan[i];
        CAPTURE(i);
        REQUIRE((c.group == 0 || c.group == 1));
        CHECK(c.channel >= 0);
        CHECK(c.channel < shell::kCouponChain.channels[c.group]);
        CHECK(victim_index_of(c) >= 0);
        CHECK(c.r_src_ohm == settle_r_src(c.group, c.channel));
        CHECK(c.row >= 1);
        CHECK(c.row <= 10);
    }
}

TEST_CASE("xtalk plan: both words hold the victim still") {
    // The measurement's entire premise (spec section 3): the victim never
    // changes channel, so the sample-and-hold's channel-change transient is
    // not in the reading at all. A case whose two words moved the victim's
    // address or its enable would be measuring settle time again, under a
    // name that says crosstalk.
    for(int i = 0; i < shell::kXtalkCases; ++i) {
        const shell::XtalkCase& c = shell::kXtalkPlan[i];
        CAPTURE(i);
        const uint32_t hold = shell::victim_address_mask(c.group)
                              | shell::victim_enable_mask(c.group);
        CHECK((c.word_a & hold) == (c.word_b & hold));
        CHECK((c.word_a & shell::victim_enable_mask(c.group)) == 0u);
    }
}

TEST_CASE("xtalk plan: a Latch case changes something and a control changes nothing") {
    int controls = 0, latch_aggressors = 0;
    for(int i = 0; i < shell::kXtalkCases; ++i) {
        const shell::XtalkCase& c = shell::kXtalkPlan[i];
        CAPTURE(i);
        if(c.kind == shell::XtalkKind::Latch) {
            if(c.row == 2) { CHECK(c.word_a == c.word_b); ++controls; }
            else           { CHECK(c.word_a != c.word_b); ++latch_aggressors; }
        } else {
            // Silent, ShiftOnly and Static have no second word: nothing is
            // latched, so a differing word_b would be a field nobody reads
            // pretending to be an event.
            CHECK(c.word_a == c.word_b);
        }
    }
    CHECK(controls == shell::kXtalkVictims);
    CHECK(latch_aggressors > 0);
}

TEST_CASE("xtalk plan: every victim has a silent case and a control case") {
    // Without both, that victim's aggressor cases have no reference and
    // every delta computed for it is a difference against nothing.
    for(int v = 0; v < shell::kXtalkVictims; ++v) {
        const shell::XtalkVictim& vv = shell::kXtalkVictimTable[v];
        CAPTURE(v);
        int silent = 0, control = 0, silent_printing = 0;
        for(int i = 0; i < shell::kXtalkCases; ++i) {
            const shell::XtalkCase& c = shell::kXtalkPlan[i];
            if(c.group != vv.group || c.channel != vv.channel) continue;
            if(c.row == 1)  ++silent;
            if(c.row == 2)  ++control;
            if(c.row == 10) ++silent_printing;
        }
        CHECK(silent == 1);
        CHECK(control == 1);
        CHECK(silent_printing == 1);
    }
}

TEST_CASE("xtalk plan: the MUX_A3 cases move bit 3 and nothing else") {
    // Spec section 9's named red. Rows 5 and 6 are the bare address edge
    // across the moat: A3 is the ONLY bit that may differ, because the 4051
    // ignores it and the 4067's response is what row 6 separates from row 5.
    // A case that also moved an LED bit or an enable would be a different
    // experiment wearing this one's label.
    const uint32_t a3 = 8u << shell::kCouponChain.addr_shift;
    int seen = 0;
    for(int i = 0; i < shell::kXtalkCases; ++i) {
        const shell::XtalkCase& c = shell::kXtalkPlan[i];
        if(c.row != 5 && c.row != 6) continue;
        CAPTURE(i);
        CHECK((c.word_a ^ c.word_b) == a3);
        // Row 5 holds the 16:1 disabled, row 6 enables it. That is the whole
        // difference between "a bare edge crossing the moat" and "the 16:1
        // switching channel into ADC_9 while ADC_10 is read".
        const uint32_t en16 = shell::victim_enable_mask(0);
        CHECK(((c.word_a & en16) != 0u) == (c.row == 5));
        CHECK(c.needs_rv4 == (c.row == 6));
        ++seen;
    }
    CHECK(seen == 8);
}

TEST_CASE("xtalk plan: the LED cases move the whole LED field and only it") {
    const uint32_t leds = ((1u << shell::kCouponChain.led_bits) - 1u)
                          << shell::kCouponChain.led_shift;
    int seen = 0;
    for(int i = 0; i < shell::kXtalkCases; ++i) {
        const shell::XtalkCase& c = shell::kXtalkPlan[i];
        if(c.row != 7) continue;
        CAPTURE(i);
        CHECK((c.word_a ^ c.word_b) == leds);
        ++seen;
    }
    CHECK(seen == 2 * shell::kXtalkVictims);
}

TEST_CASE("xtalk plan: every Latch aggressor runs in both directions") {
    // Charge injection has a sign and di/dt has a sign. A disturbance that
    // flips with the edge is coupling; one that does not is the pulse
    // itself, and only both directions can tell them apart. So for every
    // aggressor case there must be a mirror with word_a and word_b swapped.
    for(int i = 0; i < shell::kXtalkCases; ++i) {
        const shell::XtalkCase& c = shell::kXtalkPlan[i];
        if(c.kind != shell::XtalkKind::Latch || c.row == 2) continue;
        CAPTURE(i);
        bool mirrored = false;
        for(int j = 0; j < shell::kXtalkCases; ++j) {
            const shell::XtalkCase& d = shell::kXtalkPlan[j];
            if(j != i && d.row == c.row && d.group == c.group
               && d.channel == c.channel && d.word_a == c.word_b
               && d.word_b == c.word_a)
                mirrored = true;
        }
        CHECK(mirrored);
    }
}

TEST_CASE("xtalk plan: the Static cases come in pairs, dark and lit") {
    for(int v = 0; v < shell::kXtalkVictims; ++v) {
        const shell::XtalkVictim& vv = shell::kXtalkVictimTable[v];
        CAPTURE(v);
        const uint32_t leds = ((1u << shell::kCouponChain.led_bits) - 1u)
                              << shell::kCouponChain.led_shift;
        int dark = 0, lit = 0;
        for(int i = 0; i < shell::kXtalkCases; ++i) {
            const shell::XtalkCase& c = shell::kXtalkPlan[i];
            if(c.kind != shell::XtalkKind::Static) continue;
            if(c.group != vv.group || c.channel != vv.channel) continue;
            if((c.word_a & leds) == 0u)    ++dark;
            if((c.word_a & leds) == leds)  ++lit;
        }
        CHECK(dark == 1);
        CHECK(lit == 1);
    }
}

TEST_CASE("xtalk plan: the table is the size the spec's ten rows add up to") {
    // 5 silent + 5 control + 6 MUX8_EN_N + 4 MUX16_EN_N + 4 A3-high
    // + 4 A3-low + 10 LED + 10 static + 5 shift-only + 5 silent-printing.
    CHECK(shell::kXtalkCases == 58);
    int by_row[11] = {};
    for(int i = 0; i < shell::kXtalkCases; ++i) ++by_row[shell::kXtalkPlan[i].row];
    CHECK(by_row[1] == 5);
    CHECK(by_row[2] == 5);
    CHECK(by_row[3] == 6);
    CHECK(by_row[4] == 4);
    CHECK(by_row[5] == 4);
    CHECK(by_row[6] == 4);
    CHECK(by_row[7] == 10);
    CHECK(by_row[8] == 10);
    CHECK(by_row[9] == 5);
    CHECK(by_row[10] == 5);
}

TEST_CASE("xtalk plan: only row 6 needs the pot that is not fitted") {
    for(int i = 0; i < shell::kXtalkCases; ++i) {
        const shell::XtalkCase& c = shell::kXtalkPlan[i];
        CAPTURE(i);
        CHECK(c.needs_rv4 == (c.row == 6));
    }
}

TEST_CASE("xtalk plan: the scan settle delay is the audio block period") {
    // Read from shell/, not chosen here -- see the comment on
    // scan_settle_ns() and Task 5 step 2. The board's own numbers are
    // handed in at runtime; these are the arithmetic's own properties.
    CHECK(shell::scan_settle_ns(96, 48000) == 2000000u);
    CHECK(shell::scan_settle_ns(48, 48000) == 1000000u);
    CHECK(shell::scan_settle_ns(96, 96000) == 1000000u);
    // A board that answers nonsense must not produce a plausible duration.
    CHECK(shell::scan_settle_ns(96, 0) == 0u);
    CHECK(shell::scan_settle_ns(0, 48000) == 0u);
}

TEST_CASE("xtalk plan: the scan settle arithmetic does not overflow on the way") {
    // block_size * 1e9 leaves 32 bits for any block size above 4, so the
    // intermediate has to be 64 bit. With a 32-bit intermediate a 96-sample
    // block at 48 kHz comes back as 331 350 ns instead of 2 000 000 -- a
    // plausible-looking number, which is the worst kind, and one that would
    // put the criterion boundary INSIDE the grid and silently switch the
    // reader onto its other verdict branch.
    CHECK(shell::scan_settle_ns(96, 48000) == 2000000u);
    CHECK(shell::scan_settle_ns(1024, 48000) == 21333333u);
    CHECK(shell::scan_settle_ns(1, 1) == 1000000000u);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
source env.sh; cmake --build build
```

**Expected RED:** a compile failure — `shell/xtalk_plan.h` does not exist.
That is the correct first red, and it is red because nothing in this task's
interface exists yet.

- [ ] **Step 3: Write the header**

Create `shell/xtalk_plan.h`:

```cpp
#pragma once

// What the crosstalk probe holds still, what it fires at it, and how it
// decides. Data and pure arithmetic with no hardware type in it, for the same
// reason as mux_plan.h and settle_plan.h: this is where a wrong word is a
// visible line instead of a board that "looks quiet".
//
// Every chain word here is DERIVED -- from hardware/coupon/scripts/netlist.py
// for which 595 output carries which control net, and from mux_plan.h's
// kCouponChain for the bit layout. Nothing in this file may be quoted as
// measured.
//
// Spec: ../docs/superpowers/specs/2026-09-18-coupon-crosstalk-probe-design.md
#include <cstdint>

#include "mux_plan.h"
#include "settle_plan.h"

namespace shell {

// How the aggressor reaches the board. Printed as kind=%d, so the numbering
// is part of the output format and may not be reordered.
enum class XtalkKind : uint8_t
{
    Silent    = 0,   // no chain access at all for the whole grid
    Latch     = 1,   // shift word_b and pulse RCLK; t0 is that edge
    ShiftOnly = 2,   // shift word_a, no RCLK pulse; the 595 outputs do not move
    Static    = 3,   // hold one word, 64 conversions, no grid
};

// A channel that is read while something else on the board moves.
struct XtalkVictim
{
    int      group;       // 0 = the 4067 on ADC_9, 1 = the 4051 on ADC_10
    int      channel;
    uint32_t r_src_ohm;   // switch Ron plus what the netlist wires; the
                          // attribution axis (spec section 2)
};

inline constexpr int kXtalkVictims = 5;

// In the HEADER and `inline constexpr`, like kCouponChain and unlike
// kSettlePlan, and that is not a style choice: xtalk_plan.cpp builds every
// chain word in kXtalkPlan from this table at compile time, and a constant
// expression may not read a merely-`const` object defined in another
// translation unit -- or in the same one. Putting the table here is what
// lets the word builder run in the constant evaluator, which is where a
// wrong word is a compile error rather than a quiet run.
//
// Spec section 2. Two 5150 ohm dividers, one on each mux, because a
// disturbance that differs between the 4067 and the 4051 is in the chip and
// not in the board; one 650 ohm divider as the impedance control; and one
// 0 ohm tie per mux as the instrument's zero. A delta that grows with R_src
// is charge or current arriving at the node; one that is flat across 0, 650
// and 5150 ohms is in the ground, the reference or the ADC.
//
// The seven pots being unpopulated does not matter to a victim: a pot at mid
// travel is a 5 kohm static source, which REF_A already is.
inline constexpr XtalkVictim kXtalkVictimTable[kXtalkVictims] = {
    // group ch  R_src
    {0,  8, 5150},   // REF_A,  10k/10k divider on the 4067  (netlist.py REFS)
    {1,  6, 5150},   // REF_C,  the same on the 4051
    {0,  9,  650},   // REF_B,  1k/1k divider: the impedance control
    {0, 10,  150},   // R_SP10, spare tied to AGND through 0 R
    {1,  3,  150},   // R_LO3,  the same on the 4051
};

// How many address lines the victim's mux actually reads. The 4067 uses all
// four; the 4051 uses three (netlist.py:163, "the 8:1 uses three of the
// four"). That asymmetry is not a detail -- it is the entire reason MUX_A3
// can be an aggressor against a victim on the 4051 and against nothing else.
constexpr int address_bits_of_group(int group)
{
    return group == 0 ? 4 : 3;
}

constexpr uint32_t victim_address_mask(int group)
{
    return ((1u << address_bits_of_group(group)) - 1u) << kCouponChain.addr_shift;
}

// Enables are active low and one bit each: bit enable_shift + 0 is EN16
// (netlist.py, U_SR1.QE), bit enable_shift + 1 is EN8 (U_SR1.QF).
constexpr uint32_t victim_enable_mask(int group)
{
    return 1u << (kCouponChain.enable_shift + group);
}

constexpr uint32_t other_enable_mask(int group)
{
    return 1u << (kCouponChain.enable_shift + (1 - group));
}

// The whole LED field, bits led_shift .. led_shift + led_bits - 1.
constexpr uint32_t led_field_mask()
{
    return ((1u << kCouponChain.led_bits) - 1u) << kCouponChain.led_shift;
}

// A chain word that selects `v` and takes every other bit from `rest`.
//
// The victim's address bits and its own enable bit are FORCED; everything
// else -- the other mux's enable, A3 when the victim sits on the 4051, and
// the whole LED field -- comes from `rest` and is what a case varies. That is
// what makes "the victim never changes channel" a property of the word
// builder rather than of every hand-written table entry.
constexpr uint32_t xtalk_word(const XtalkVictim& v, uint32_t rest)
{
    const uint32_t addr_mask = victim_address_mask(v.group);
    const uint32_t en_mask   = victim_enable_mask(v.group);
    const uint32_t addr
        = (static_cast<uint32_t>(v.channel) << kCouponChain.addr_shift) & addr_mask;
    // Clearing en_mask out of `rest` is what enables the victim's mux:
    // active low.
    return (rest & ~(addr_mask | en_mask)) | addr;
}

// One entry of the plan table (spec section 4).
struct XtalkCase
{
    uint8_t   row;            // the spec section 4 table row, 1..10
    int       group;          // the victim's mux
    int       channel;        // the victim's channel, held for the whole case
    uint32_t  r_src_ohm;      // the victim's, from the netlist
    uint32_t  word_a;         // the chain word before the event
    uint32_t  word_b;         // what the event latches (== word_a for a control)
    XtalkKind kind;
    bool      needs_rv4;      // true for row 6, the one variant section 2 flags
    bool      prints_inline;  // row 10: PrintLine after every grid point
};

inline constexpr int kXtalkCases = 58;
extern const XtalkCase kXtalkPlan[kXtalkCases];

// The delay the shipping scan gives a mux address before it reads it.
//
// READ FROM shell/, never chosen here. MuxScan::step() reads the sense pins
// for the step it wrote LAST time and only then clocks out the next address
// (mux_scan.cpp:110-131), and mux_scan.h's header comment says so in as many
// words: "the address is clocked out at the end of one audio block and
// sampled at the start of the next, so the block period IS the settle
// window". There is no named constant in shell/ to read -- the value IS the
// audio block period, so the probe asks the board for the block size and the
// sample rate at runtime and prints both derivation inputs beside the result.
//
// 64-bit intermediate on purpose: block_size * 1e9 leaves 32 bits for any
// block size above 4, and a 96-sample block would come back as 1.1 s instead
// of 2 ms -- a plausible-looking number, which is the worst kind.
constexpr uint32_t scan_settle_ns(int block_size, int sample_rate_hz)
{
    if(block_size <= 0 || sample_rate_hz <= 0) return 0u;
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(block_size) * 1000000000ull)
        / static_cast<uint64_t>(sample_rate_hz));
}

} // namespace shell
```

- [ ] **Step 4: Write the table**

Create `shell/xtalk_plan.cpp`. The victims are spec §2's table; the cases are
spec §4's, expanded to one entry per (row, victim, edge direction).

```cpp
#include "xtalk_plan.h"

namespace shell {

// kXtalkVictimTable lives in the header, and its own comment there says why:
// every word below is built from it in the constant evaluator.

namespace {

// Shorthand for the table below. `rest` carries every bit the case varies;
// the victim's own address and enable are forced by xtalk_word().
constexpr uint32_t w(int v, uint32_t rest)
{
    return xtalk_word(kXtalkVictimTable[v], rest);
}

// Victim indices, so the table reads as the spec's table does.
constexpr int A  = 0;   // REF_A,  group 0, ch 8
constexpr int C  = 1;   // REF_C,  group 1, ch 6
constexpr int B  = 2;   // REF_B,  group 0, ch 9
constexpr int S  = 3;   // R_SP10, group 0, ch 10
constexpr int L  = 4;   // R_LO3,  group 1, ch 3

// The quiet baseline for a victim: the other mux disabled, LEDs dark.
constexpr uint32_t base(int v)
{
    return other_enable_mask(kXtalkVictimTable[v].group);
}

constexpr uint32_t kA3   = 8u << kCouponChain.addr_shift;
constexpr uint32_t kEn16 = victim_enable_mask(0);
constexpr uint32_t kEn8  = victim_enable_mask(1);
constexpr uint32_t kLeds = led_field_mask();

// A Silent, ShiftOnly or Static case: one word, no event.
constexpr XtalkCase one(uint8_t row, int v, uint32_t rest, XtalkKind k,
                        bool prints_inline = false)
{
    return XtalkCase{row, kXtalkVictimTable[v].group, kXtalkVictimTable[v].channel,
                     kXtalkVictimTable[v].r_src_ohm, w(v, rest), w(v, rest), k,
                     false, prints_inline};
}

// A Latch case: two words, one edge.
constexpr XtalkCase edge(uint8_t row, int v, uint32_t rest_a, uint32_t rest_b,
                         bool needs_rv4 = false)
{
    return XtalkCase{row, kXtalkVictimTable[v].group, kXtalkVictimTable[v].channel,
                     kXtalkVictimTable[v].r_src_ohm, w(v, rest_a), w(v, rest_b),
                     XtalkKind::Latch, needs_rv4, false};
}

} // namespace

// Spec section 4's table, in the order it runs. Rows 1 and 2 come first
// because every later row's delta is a difference against row 2's curve, and
// G6 is a difference between rows 2 and 1 -- the firmware holds row 1's five
// curves for the whole block for exactly that reason.
//
// WHICH CHANNEL THE AGGRESSOR MUX LANDS ON IS NOT A FREE PARAMETER. The two
// muxes share A0..A2, so when a case enables the aggressor's mux, its address
// is whatever the victim's low bits happen to be: against REF_A (ch 8) and
// R_SP10 (ch 10) that puts the 4051 on RV5 and RV6, wipers that float until
// the pots are fitted. The floating node sits on MUX8_COM = ADC_10, not on
// the victim's ADC_9, so it does not enter the reading -- but word_a and
// word_b are printed, so which channel it was is recoverable from any
// capture, and it should be in any write-up of rows 3 and 9.
const XtalkCase kXtalkPlan[kXtalkCases] = {
    // Row 1: the floor. No chain access and no print for the whole grid.
    // Does the 8-12 count wander of settle-measured.md section 5 need the
    // scan's own activity?
    one(1, A, base(A), XtalkKind::Silent),
    one(1, C, base(C), XtalkKind::Silent),
    one(1, B, base(B), XtalkKind::Silent),
    one(1, S, base(S), XtalkKind::Silent),
    one(1, L, base(L), XtalkKind::Silent),

    // Row 2: the control. A latch pulse with no bit change, so every row
    // below it is a difference that isolates the bit change from the pulse
    // and the shift that precedes it.
    edge(2, A, base(A), base(A)),
    edge(2, C, base(C), base(C)),
    edge(2, B, base(B), base(B)),
    edge(2, S, base(S), base(S)),
    edge(2, L, base(L), base(L)),

    // Row 3: MUX8_EN_N, both directions, against the three victims on the
    // 4067. The moat-crossing bus and the 4051's own switches, into a read
    // taken on the 16:1.
    edge(3, A, 0u,    kEn8), edge(3, A, kEn8, 0u),
    edge(3, B, 0u,    kEn8), edge(3, B, kEn8, 0u),
    edge(3, S, 0u,    kEn8), edge(3, S, kEn8, 0u),

    // Row 4: the mirror image -- MUX16_EN_N against the two victims on the
    // 4051.
    edge(4, C, 0u,     kEn16), edge(4, C, kEn16, 0u),
    edge(4, L, 0u,     kEn16), edge(4, L, kEn16, 0u),

    // Row 5: a bare address edge across the moat. MUX_A3 toggles with the
    // 16:1 DISABLED, so no mux acts on it and what is left is the trace.
    edge(5, C, kEn16,       kEn16 | kA3), edge(5, C, kEn16 | kA3, kEn16),
    edge(5, L, kEn16,       kEn16 | kA3), edge(5, L, kEn16 | kA3, kEn16),

    // Row 6: the same edge with the 16:1 ENABLED, so it switches channel
    // into ADC_9 while ADC_10 is read. REF_C sits at address 110, so A3
    // moves the 16:1 between channel 6 (RV4's wiper, floating until the pot
    // is in) and channel 14 (AGND) -- which is why these four carry
    // needs_rv4 and are reported as skipped until a build flag says the pot
    // is fitted.
    edge(6, C, 0u,   kA3,  true), edge(6, C, kA3,  0u, true),
    edge(6, L, 0u,   kA3,  true), edge(6, L, kA3,  0u, true),

    // Row 7: eight simultaneous current edges through the single ground
    // join. Both directions, every victim.
    edge(7, A, base(A),         base(A) | kLeds), edge(7, A, base(A) | kLeds, base(A)),
    edge(7, C, base(C),         base(C) | kLeds), edge(7, C, base(C) | kLeds, base(C)),
    edge(7, B, base(B),         base(B) | kLeds), edge(7, B, base(B) | kLeds, base(B)),
    edge(7, S, base(S),         base(S) | kLeds), edge(7, S, base(S) | kLeds, base(S)),
    edge(7, L, base(L),         base(L) | kLeds), edge(7, L, base(L) | kLeds, base(L)),

    // Row 8: the DC shift of the star point under eight LEDs. No grid, no
    // edge -- 64 conversions at each of two held states, and the reader
    // differences them.
    one(8, A, base(A),         XtalkKind::Static), one(8, A, base(A) | kLeds, XtalkKind::Static),
    one(8, C, base(C),         XtalkKind::Static), one(8, C, base(C) | kLeds, XtalkKind::Static),
    one(8, B, base(B),         XtalkKind::Static), one(8, B, base(B) | kLeds, XtalkKind::Static),
    one(8, S, base(S),         XtalkKind::Static), one(8, S, base(S) | kLeds, XtalkKind::Static),
    one(8, L, base(L),         XtalkKind::Static), one(8, L, base(L) | kLeds, XtalkKind::Static),

    // Row 9: digital activity with NO control-line change -- 16 bits shifted,
    // RCLK never pulsed, so the 595 storage register never moves. Read from
    // the 74HC595 datasheet, unmeasured on this board: row 9 against row 2 is
    // the check.
    one(9, A, base(A), XtalkKind::ShiftOnly),
    one(9, C, base(C), XtalkKind::ShiftOnly),
    one(9, B, base(B), XtalkKind::ShiftOnly),
    one(9, S, base(S), XtalkKind::ShiftOnly),
    one(9, L, base(L), XtalkKind::ShiftOnly),

    // Row 10: as row 1, but PrintLine after every grid point, as the settle
    // probe does. USB-CDC and DMA activity, which settle-measured.md section
    // 5 names as an unexamined candidate for the 8-12 count wander.
    one(10, A, base(A), XtalkKind::Silent, true),
    one(10, C, base(C), XtalkKind::Silent, true),
    one(10, B, base(B), XtalkKind::Silent, true),
    one(10, S, base(S), XtalkKind::Silent, true),
    one(10, L, base(L), XtalkKind::Silent, true),
};

} // namespace shell
```

- [ ] **Step 5: Wire it into the host build**

In `CMakeLists.txt`, in the `spky_tests` source list, directly after
`tests/test_settle_plan.cpp` (line 98):

```cmake
    shell/xtalk_plan.cpp
    tests/test_xtalk_plan.cpp
```

- [ ] **Step 6: Run the tests to verify they pass**

```bash
source env.sh; cmake -S . -B build -DCMAKE_BUILD_TYPE=Release; cmake --build build; ctest --test-dir build --output-on-failure
```

Expected: every test passes, `test_settle_plan.cpp`'s and
`test_mux_plan.cpp`'s included — confirm they are still present and still
running.

- [ ] **Step 7: Prove the RED once**

Spec §9 names this one: the `MUX_A3` assertion.

In `shell/xtalk_plan.cpp`, change row 5's first entry from

```cpp
    edge(5, C, kEn16,       kEn16 | kA3),
```

to

```cpp
    edge(5, C, kEn16,       kEn16 | kA3 | (1u << kCouponChain.led_shift)),
```

Rebuild and run only that case:

```bash
source env.sh; cmake --build build; ./build/spky_tests.exe -tc="xtalk plan: the MUX_A3 cases move bit 3*"
```

**Expected: FAILURE** on `CHECK((c.word_a ^ c.word_b) == a3)`. It is red
because the sabotaged entry now toggles an LED bit alongside A3, so the
exclusive-or carries two bits and the case is no longer "a bare address edge
across the moat" — it is an address edge plus an LED edge, which is row 7's
question. That is the failure this assertion exists to catch, and it is
exactly the sort of change a later editor makes without noticing.

**Retype** the original line; do not `git checkout`. Rebuild, run the full
suite, confirm green, report what you saw.

- [ ] **Step 8: Commit**

```bash
git add shell/xtalk_plan.h shell/xtalk_plan.cpp tests/test_xtalk_plan.cpp CMakeLists.txt
git commit -m "feat(shell): the crosstalk probe's victims, chain words and case table

Fifty-eight cases across five victims, as data with no hardware in it. The
victim's own address and enable are forced by the word builder rather than
written out per entry, so 'the victim never changes channel' -- the premise
the whole measurement rests on -- is a property of one function instead of
a promise about a hand-written table.

The test does not copy the spec's impedance column: every r_src_ohm is
asserted against what settle_plan.cpp already carries for the same channel,
which is possible because all five victims are targets there too. A typo in
one table now fails instead of matching a typo in the other.

The MUX_A3 assertion was red-proven by hand: an entry that toggles an LED
bit alongside A3 stops being a bare address edge and becomes row 7's
question wearing row 5's label.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The four gates

Pure, host-tested, no board. Spec §7. G2 and G4 are the settle probe's,
unchanged and reusing its constants; G5 and G6 are new.

**Files:**
- Modify: `shell/xtalk_plan.h`, `shell/xtalk_plan.cpp` — **extend, do not replace.** Task 2's tables stay exactly as they are.
- Modify: `tests/test_xtalk_plan.cpp` — **append test cases.** Do not delete or rewrite Task 2's; this plan's predecessor was bitten by a task that replaced its predecessor's test file.

**Interfaces:**
- Consumes: everything Task 2 produced, plus `kFloorMaxCounts`, `kJitterMaxNs`, `kSettleCounts` from `settle_plan.h`.
- Produces: `XtalkSummary`, `XtalkGates`, `xtalk_gates()`. Task 6 fills the summary and prints `XtalkGates::ok()`; Task 7's reader reads the printed bits.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_xtalk_plan.cpp`:

```cpp
namespace {

// A summary that passes every gate, for tests that break exactly one thing.
shell::XtalkSummary clean_xtalk_summary() {
    shell::XtalkSummary s{};
    s.b0                   = 32;    // settle-measured.md section 5 measured 27..40
    s.lat_min_ns           = 700;
    s.lat_max_ns           = 800;
    s.lat_mean_ns          = 747;
    s.address_ok           = true;
    s.worst_control_delta  = 3;
    return s;
}

} // namespace

TEST_CASE("xtalk gates: a clean run passes all four") {
    const shell::XtalkGates g = shell::xtalk_gates(clean_xtalk_summary());
    CHECK(g.g2_floor);
    CHECK(g.g4_jitter);
    CHECK(g.g5_address);
    CHECK(g.g6_control);
    CHECK(g.ok());
}

TEST_CASE("xtalk gates: G2 is the settle probe's floor, unchanged") {
    shell::XtalkSummary s = clean_xtalk_summary();
    s.b0 = shell::kFloorMaxCounts;
    CHECK(shell::xtalk_gates(s).g2_floor);
    s.b0 = shell::kFloorMaxCounts + 1;
    CHECK_FALSE(shell::xtalk_gates(s).g2_floor);
    // -1 is the firmware's "every repeat timed out" sentinel, not a floor of
    // minus one count.
    s.b0 = -1;
    CHECK_FALSE(shell::xtalk_gates(s).g2_floor);
}

TEST_CASE("xtalk gates: G4 is the settle probe's jitter gate, unchanged") {
    shell::XtalkSummary s = clean_xtalk_summary();
    s.lat_min_ns = 700;
    s.lat_max_ns = 700 + shell::kJitterMaxNs;
    CHECK(shell::xtalk_gates(s).g4_jitter);
    s.lat_max_ns = 700 + shell::kJitterMaxNs + 1;
    CHECK_FALSE(shell::xtalk_gates(s).g4_jitter);
    // A negative mean means the conversion-time subtraction came out larger
    // than the span it was subtracted from, which invalidates every delay in
    // the run.
    s = clean_xtalk_summary();
    s.lat_mean_ns = -1;
    CHECK_FALSE(shell::xtalk_gates(s).g4_jitter);
}

TEST_CASE("xtalk gates: G5 refuses a run whose victims are not where the table says") {
    // A wrong word here makes every delta a measurement of nothing -- the
    // reading would be of some other channel, moving for some other reason,
    // and it would look exactly like a number.
    shell::XtalkSummary s = clean_xtalk_summary();
    s.address_ok = false;
    CHECK_FALSE(shell::xtalk_gates(s).g5_address);
    CHECK_FALSE(shell::xtalk_gates(s).ok());
}

TEST_CASE("xtalk gates: G6 refuses a control that is not a control") {
    // The bound is kSettleCounts, the same half-LSB-of-12-bit criterion the
    // aggressor verdict uses -- because if a latch pulse with NO bit change
    // already moves the reading by that much, the aggressor cases cannot be
    // read as differences at all.
    shell::XtalkSummary s = clean_xtalk_summary();
    s.worst_control_delta = shell::kSettleCounts;
    CHECK(shell::xtalk_gates(s).g6_control);
    s.worst_control_delta = shell::kSettleCounts + 1;
    CHECK_FALSE(shell::xtalk_gates(s).g6_control);
}

TEST_CASE("xtalk gates: G6 takes a magnitude, and a negative one is a fault") {
    // worst_control_delta is filled from max |mean_control(d) -
    // mean_silent(d)|, so it cannot be negative unless the firmware never
    // filled it. A gate that passed an unfilled field would pass a run in
    // which the control curve was never taken.
    shell::XtalkSummary s = clean_xtalk_summary();
    s.worst_control_delta = -1;
    CHECK_FALSE(shell::xtalk_gates(s).g6_control);
}

TEST_CASE("xtalk gates: ok() is the conjunction and nothing else") {
    // Each gate alone must be able to refuse the run. A fold that dropped
    // one would leave that gate printed and toothless, which is worse than
    // not having it.
    for(int which = 0; which < 4; ++which) {
        shell::XtalkSummary s = clean_xtalk_summary();
        if(which == 0) s.b0 = shell::kFloorMaxCounts + 1;
        if(which == 1) s.lat_mean_ns = -1;
        if(which == 2) s.address_ok = false;
        if(which == 3) s.worst_control_delta = shell::kSettleCounts + 1;
        CAPTURE(which);
        CHECK_FALSE(shell::xtalk_gates(s).ok());
    }
}
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
source env.sh; cmake --build build
```

**Expected RED:** a compile failure — `shell::XtalkSummary`,
`shell::XtalkGates` and `shell::xtalk_gates` do not exist. Red because the
declarations are not written yet.

- [ ] **Step 3: Extend the header**

Append inside `namespace shell` in `shell/xtalk_plan.h`, after
`scan_settle_ns()`:

```cpp
// What a completed block reduces to before it is judged. Spec section 7.
//
// G1 and G3 do not carry over from the settle probe: there is no knee here,
// and the settled region's width is a REPORTED QUANTITY in this instrument
// (spec section 5's settled_mean_spread and widest_sample_band), not a gate.
// Gating it would refuse the very run that answers the question.
struct XtalkSummary
{
    int32_t b0;                    // spread of a 0 ohm victim's 64 settled
                                   // conversions; -1 = every repeat timed out
    int32_t lat_min_ns;
    int32_t lat_max_ns;
    int32_t lat_mean_ns;
    bool    address_ok;            // every victim read inside its coupon_expect
                                   // band in its Silent case
    int32_t worst_control_delta;   // max |mean_control(d) - mean_silent(d)|
                                   // over every victim and every grid point
};

struct XtalkGates
{
    bool g2_floor;
    bool g4_jitter;
    bool g5_address;
    bool g6_control;

    bool ok() const { return g2_floor && g4_jitter && g5_address && g6_control; }
};

// A run that fails any gate prints every number and refuses the verdict; the
// reader exits 1. Failing a gate is never a statement that the board is
// defective -- with one honest exception, and the exception is G6.
//
// G6 failing may well be a real result rather than a broken instrument: a
// latch pulse alone moving a 5150 ohm channel by 8 counts would be a finding
// about this board. The gate still refuses the PER-AGGRESSOR verdict in that
// case, because the differences stop being interpretable; what the run then
// reports is the control curve, which is its result.
XtalkGates xtalk_gates(const XtalkSummary& s);
```

- [ ] **Step 4: Write the gates**

Append inside `namespace shell` in `shell/xtalk_plan.cpp`:

```cpp
XtalkGates xtalk_gates(const XtalkSummary& s)
{
    XtalkGates g{};

    // G2, unchanged from the settle probe: the measured noise floor must
    // leave the mean's own uncertainty well inside the decision threshold.
    // 64 counts peak to peak across 64 samples is about 5.1 sigma, which
    // puts the spread of a 64-sample mean near a fifth of the 8-count
    // criterion.
    g.g2_floor = s.b0 >= 0 && s.b0 <= kFloorMaxCounts;

    // G4, unchanged: aperture jitter inside one grid step, and a latency the
    // conversion model does not forbid.
    g.g4_jitter = s.lat_mean_ns >= 0 && s.lat_max_ns >= s.lat_min_ns
                  && (s.lat_max_ns - s.lat_min_ns) <= kJitterMaxNs;

    // G5, new: the mux is where the table says and is enabled. Computed on
    // the board against coupon_expect()'s bands and the span the probe's own
    // tie reads give it; this function only folds the verdict in, because
    // the bands need a hardware read and this file may not hold one.
    g.g5_address = s.address_ok;

    // G6, new: the control must be a control.
    g.g6_control = s.worst_control_delta >= 0
                   && s.worst_control_delta <= kSettleCounts;

    return g;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
source env.sh; cmake --build build; ctest --test-dir build --output-on-failure
```

Expected: every test passes, Task 2's included.

- [ ] **Step 6: Prove the RED on G6, deliberately**

Spec §9 names G6 as the one to red-prove, "because that is the case where
every aggressor `delta` still looks like a number".

In `xtalk_gates()`, replace

```cpp
    g.g6_control = s.worst_control_delta >= 0
                   && s.worst_control_delta <= kSettleCounts;
```

with

```cpp
    g.g6_control = s.worst_control_delta >= 0;
```

Rebuild and run:

```bash
source env.sh; cmake --build build; ./build/spky_tests.exe -tc="xtalk gates: G6 refuses a control that is not*"
```

**Expected: FAILURE** on the `CHECK_FALSE` with
`worst_control_delta = kSettleCounts + 1`. It is red because without the
ceiling a run whose control curve sits 9 counts off its silent curve — a
latch pulse alone already past the criterion — passes, and every aggressor
`delta` computed against that control is still a perfectly plausible integer.
That is the exact failure the gate exists to stop.

**Retype** the original two lines; do not `git checkout`. Rebuild, run the
full suite, confirm green, report what you saw.

- [ ] **Step 7: Commit**

```bash
git add shell/xtalk_plan.h shell/xtalk_plan.cpp tests/test_xtalk_plan.cpp
git commit -m "feat(shell): the crosstalk probe's four gates

G2 and G4 are the settle probe's, reusing its constants rather than
restating them: same floor, same jitter bound, same instrument. G1 and G3 do
not carry over -- there is no knee here, and the settled region's width is
the quantity this probe reports rather than one it gates, so gating it would
refuse the run that answers the question.

G5 refuses a run whose victims are not where the table says: a wrong word
makes every delta a measurement of nothing, and it looks exactly like a
number. G6 refuses a control that is not a control, and it is the one gate
whose failure may be a real finding about the board rather than a broken
instrument -- the run then reports its control curve and refuses only the
per-aggressor verdict.

G6 was red-proven by hand: without its ceiling a control curve 9 counts off
its silent curve passes, and every aggressor delta against it is still a
plausible integer.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: The build switch, the git stamp, and an image that says nothing

This task ships a firmware that prints an empty, well-formed block. That is
deliberate, and it is the shape the settle probe's Task 3 used for the same
reason: "the switch, the link and the USB path work" and "the instrument
works" fail for entirely different reasons and are worth separating.

**Files:**
- Create: `shell/write_shell_xtalk_probe.py`, `shell/write_git_hash.py`
- Create: `shell/xtalk_probe.h`, `shell/xtalk_probe.cpp`
- Modify: `shell/Makefile`, `shell/main.cpp`

**Interfaces:**
- Consumes: `shell/xtalk_plan.h` (Tasks 2 and 3), `bench::Board`.
- Produces: `void shell::run_xtalk_probe(bench::Board& hw)` — never returns. The `SHELL_XTALK_PROBE` and `SHELL_XTALK_RV4` make switches. `SHELL_GIT_HASH`.

- [ ] **Step 1: Copy the switch generator**

Create `shell/write_shell_xtalk_probe.py` as a copy of
`shell/write_shell_settle_probe.py` with every `SETTLE` replaced by `XTALK`,
keeping the English docstring and keeping the stale-object deletion verbatim.

**One difference from the copy, and it is deliberate: this generator writes
two defines, not one.** `SHELL_XTALK_RV4` needs the same treatment as the
probe switch — a bare `-D` is invisible to make's dependency graph, which is
the entire reason these generators exist — and giving it a second generator
and a second header would be two files to keep in step for two values that
are always set together. So the usage line becomes

```
usage: write_shell_xtalk_probe.py OUTPUT {0|1} {0|1} [STALE_OBJECT...]
```

with the second `{0|1}` being `SHELL_XTALK_RV4`, and the content is

```python
    content = ("#define SHELL_XTALK_PROBE %s\n"
               "#define SHELL_XTALK_RV4 %s\n" % (sys.argv[2], sys.argv[3]))
```

The argument validation checks both, and the stale objects start at
`sys.argv[4]`.

The docstring's second paragraph reads:

```
Like write_shell_settle_probe.py this ALWAYS defines both symbols, including
in position 0, because `#if SHELL_XTALK_PROBE` has to work in both -- and
SHELL_XTALK_RV4 rides here rather than in a -D or a second generator because
a bare -D is exactly the stale-object trap this file exists to close, and the
two values are never set apart.
```

Do not shorten the timestamp paragraph. It documents a real failure from
2026-08-23 and it is the reason the script deletes objects itself.

- [ ] **Step 2: Write the git stamp generator**

Spec §8's `SHELL_XTALK_CFG` line ends in `git=%s`, so an image has to know
which commit built it. `bench/write_git_hash.py` does this for the bench, but
from the Makefile's `$(shell git ...)` and through a FORCE rule — and a FORCE
rule is exactly what failed twice on this machine for the switch headers. This
one runs at parse time like the switch generators and deletes its dependent
object itself.

Create `shell/write_git_hash.py`:

```python
"""Writes the shell's git stamp as a real header, at Makefile PARSE time.

Same shape and same reason as write_shell_settle_probe.py: a rule with FORCE
did not hold on this machine, twice, because make has one-second mtime
resolution and a header written 0.36 s after the object landed in the same
wall-clock second counts as "not newer". This script runs while the Makefile
is read, so when the value changes the dependent object is gone before make
builds its graph.

The stamp is deliberately BOUNDED IN LENGTH: seven hex digits plus at most
one '+' for a dirty tree. libDaisy's log buffer is 128 bytes
(lib/libDaisy/src/hid/logger.h:29) and SHELL_XTALK_CFG is already near it, so
`git describe`'s variable-length output -- which grows a tag name and a
commit count when a tag is in reach -- would silently truncate the line and
stamp it "$$".

'+' rather than '-dirty' for the same three bytes of reason. A dirty stamp is
not a failure here: this probe is expected to run from a working tree that
carries uncommitted hardware work. It only has to be VISIBLE, so a capture is
never mistaken for one taken at a clean commit.
"""
import subprocess
import sys
from pathlib import Path


def stamp() -> str:
    try:
        head = subprocess.run(
            ["git", "rev-parse", "--short=7", "HEAD"],
            capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        # No git, or not a repository. Seven characters of honesty: a blank
        # field would read as a clean build at an unknown commit.
        return "nogit00"
    try:
        dirty = subprocess.run(
            ["git", "status", "--porcelain"],
            capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        dirty = "?"
    return head + ("+" if dirty else "")


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit("usage: write_git_hash.py OUTPUT [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)
    content = '#define SHELL_GIT_HASH "%s"\n' % stamp()
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[2:]:
            Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 3: Add the Makefile switches**

In `shell/Makefile`, after the `SHELL_SETTLE_PROBE` validation block — **in
English**, and do not translate the German blocks above it:

```make
# Position 1 replaces the instrument with the crosstalk probe: one mux
# channel is held still and one controlled digital event is fired on the same
# board at a chosen time before the ADC's aperture. It needs the coupon chain
# profile, so it implies SHELL_COUPON_PROBE's board -- but not its scan, and
# not the settle probe's sweep either. The two probe switches are mutually
# exclusive: both dispatch before StartAudio and neither returns, so an image
# with both set would run whichever main.cpp reaches first and print the other
# one's name nowhere.
# Spec:  ../docs/superpowers/specs/2026-09-18-coupon-crosstalk-probe-design.md
# Plan:  ../docs/superpowers/plans/2026-09-18-coupon-crosstalk-probe.md
SHELL_XTALK_PROBE ?= 0

ifneq ($(filter $(SHELL_XTALK_PROBE),0 1),$(SHELL_XTALK_PROBE))
$(error SHELL_XTALK_PROBE must be 0 or 1)
endif

ifeq ($(SHELL_XTALK_PROBE),1)
ifneq ($(SHELL_COUPON_PROBE),1)
$(error SHELL_XTALK_PROBE=1 needs SHELL_COUPON_PROBE=1: the probe drives the \
coupon chain profile, and the panel profile has none of its channels)
endif
ifeq ($(SHELL_SETTLE_PROBE),1)
$(error SHELL_XTALK_PROBE=1 and SHELL_SETTLE_PROBE=1 are mutually exclusive: \
both dispatch before StartAudio and neither returns)
endif
endif

# Whether RV4 is fitted on the coupon. Spec section 4: with the 16:1 enabled,
# toggling MUX_A3 moves it between channel 6 -- RV4's wiper, FLOATING until
# the pot is in -- and channel 14 (AGND). Switching a floating node into
# ADC_9 is a different experiment, so those four cases are reported as
# skipped=1 until this says otherwise. A skipped case is a LINE in the
# output, never a silently shorter table.
SHELL_XTALK_RV4 ?= 0

ifneq ($(filter $(SHELL_XTALK_RV4),0 1),$(SHELL_XTALK_RV4))
$(error SHELL_XTALK_RV4 must be 0 or 1)
endif
```

`SHELL_XTALK_RV4` is **not** a `-D`. It rides in the generated header beside
`SHELL_XTALK_PROBE` (step 1), because a bare `-D` is invisible to make's
dependency graph and an existing `build/` would happily reuse an
`xtalk_probe.o` built with the other value — an image that reports four cases
as skipped when the pot is in, or measures a floating wiper when it is not,
and either way looks exactly like a measurement.

In `CPP_SOURCES`, after `settle_probe.cpp`:

```make
	xtalk_plan.cpp \
	xtalk_probe.cpp \
```

In `SWITCH_OBJECTS`, add the new object:

```make
SWITCH_OBJECTS = $(BUILD_DIR)/main.o $(BUILD_DIR)/mux_scan.o $(BUILD_DIR)/coupon_scan.o $(BUILD_DIR)/settle_probe.o $(BUILD_DIR)/probe_adc.o $(BUILD_DIR)/xtalk_probe.o
```

In `SWITCH_HEADERS`, add two continuation lines (remember the trailing
backslash on the line that precedes them):

```make
  $(shell python write_shell_xtalk_probe.py $(BUILD_DIR)/shell_xtalk_probe.h $(SHELL_XTALK_PROBE) $(SHELL_XTALK_RV4) $(SWITCH_OBJECTS)) \
  $(shell python write_git_hash.py $(BUILD_DIR)/shell_git_hash.h $(BUILD_DIR)/xtalk_probe.o)
```

The git stamp deletes only `xtalk_probe.o`, because that is the only object
that reads it. Listing the whole `SWITCH_OBJECTS` there would force a full
rebuild of six objects on every commit for a string in one format argument.

And the explicit header dependency lines — extend `main.o`'s and add
`xtalk_probe.o`'s:

```make
$(BUILD_DIR)/main.o: $(BUILD_DIR)/shell_selftest.h $(BUILD_DIR)/shell_cpu_probe.h $(BUILD_DIR)/shell_mux_probe.h $(BUILD_DIR)/shell_idle_fill.h $(BUILD_DIR)/shell_coupon_probe.h $(BUILD_DIR)/shell_xtalk_probe.h
$(BUILD_DIR)/xtalk_probe.o: $(BUILD_DIR)/shell_xtalk_probe.h $(BUILD_DIR)/shell_git_hash.h $(BUILD_DIR)/shell_coupon_probe.h
```

The edge is belt and braces beside the parse-time deletion, and it is the
cheap half: without it the very first build of a clean tree can reach
`xtalk_probe.cpp` before either header exists.

- [ ] **Step 4: Write the skeleton probe**

Create `shell/xtalk_probe.h`:

```cpp
#pragma once

// The crosstalk probe. Holds one multiplexer channel still, fires one
// controlled digital event on the same board at a chosen time before the
// ADC's aperture, and reports how far the reading moves.
//
// It is NOT a settle-time measurement -- that instrument exists and its
// results stand (docs/hardware/settle-measured.md). It reuses that probe's
// ADC primitives, its measured clock, its gates G2 and G4 and its grid, and
// it inherits their limits.
//
// Spec: ../docs/superpowers/specs/2026-09-18-coupon-crosstalk-probe-design.md
#include "hw/board.h"

namespace shell {

// Never returns. Prints one block per pass on USB-CDC, forever.
void run_xtalk_probe(bench::Board& hw);

} // namespace shell
```

Create `shell/xtalk_probe.cpp` — for this task the block is empty but
complete:

```cpp
#include "xtalk_probe.h"

#include "cycles.h"
#include "shell_git_hash.h"
#include "xtalk_plan.h"

namespace shell {

void run_xtalk_probe(bench::Board& hw)
{
    cycles_init();
    hw.StartLog(false);

    // Read, not assumed: the CPU probe's own comment in main.cpp records the
    // day a block size was inferred from phase durations (48) and was in
    // fact 96. Both go into scan_settle_ns(), and both are printed so the
    // derivation is checkable from the capture alone.
    const int block_size = static_cast<int>(hw.AudioBlockSize());
    const int sr_hz      = static_cast<int>(hw.AudioSampleRate());

    while(1)
    {
        // The configuration line comes FIRST and the end marker always
        // arrives, even on a pass that measured nothing. A reader that can
        // only recognise a complete block is the point: a truncated one must
        // be discarded rather than half-believed.
        //
        // BYTE BUDGET. libDaisy's log buffer is 128 bytes
        // (lib/libDaisy/src/hid/logger.h:29) and a longer line is truncated
        // and stamped "$$". This one runs 119 bytes at its widest values
        // (adc_khz 4 digits, scan_settle_ns 7, git 8 plus CRLF). Do not add
        // a field to it; SHELL_XTALK_GATES has room.
        hw.PrintLine("SHELL_XTALK_CFG adc_khz=%d repeats=%d grid_ns=%d "
                     "points=%d park_ns=%d scan_settle_ns=%d rv4=%d git=%s",
                     -1, kRepeats, kGridStepNs, kGridPoints,
                     static_cast<int>(kParkNs),
                     static_cast<int>(scan_settle_ns(block_size, sr_hz)),
                     SHELL_XTALK_RV4, SHELL_GIT_HASH);
        // block_size and sr are the inputs to the field above. They ride on
        // their own line rather than in CFG because CFG has no room left,
        // and a derived duration whose inputs are not in the capture cannot
        // be checked by anyone reading it later.
        hw.PrintLine("SHELL_XTALK_RATE block_size=%d sr_hz=%d cases=%d victims=%d",
                     block_size, sr_hz, kXtalkCases, kXtalkVictims);
        hw.PrintLine("SHELL_XTALK_END");
        hw.Delay(1000);
    }
}

} // namespace shell
```

`adc_khz` prints `-1` for now — Task 5 measures it. Leave a comment saying
so, so nobody reads this pass as an ADC that failed to calibrate.

- [ ] **Step 5: Dispatch from main**

In `shell/main.cpp`, beside the existing `#include "shell_settle_probe.h"`
(line 15):

```cpp
#include "shell_xtalk_probe.h"
```

Beside the existing settle include block (line 38):

```cpp
#if SHELL_XTALK_PROBE
#include "xtalk_probe.h"
#endif
```

Extend the USB identity guard (line 47) so the crosstalk image links:

```cpp
#if defined(SHELL_CPU_PROBE) || SHELL_COUPON_PROBE || SHELL_SETTLE_PROBE || SHELL_XTALK_PROBE
```

And in `main()`, **before** the `SHELL_SETTLE_PROBE` block, because the
Makefile already refuses the combination and the more specific image should
come first if that guard is ever relaxed:

```cpp
#if SHELL_XTALK_PROBE
    // The board under test is the coupon, and the question is whether the
    // board's own digital side moves a settled pot reading. No engine, no
    // audio: StartAudio is deliberately never called here, because the codec
    // is round two's aggressor and an image that runs it cannot measure
    // round one's floor.
    shell::run_xtalk_probe(hw);   // never returns
#endif
```

- [ ] **Step 6: Build both switch positions and prove they differ**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_XTALK_PROBE=0
```

```bash
cp shell/build/shell-sram.bin /tmp/xtalk0.bin
```

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_XTALK_PROBE=1
```

```bash
cmp shell/build/shell-sram.bin /tmp/xtalk0.bin
```

Expected: both builds succeed and `cmp` reports the files **differ**.
Identical images mean the switch did not reach the objects — that exact
failure has happened on this machine twice and is why the generator deletes
objects itself. Do not proceed past an identical `cmp`.

Confirm both guards fire:

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell images SHELL_XTALK_PROBE=1 SHELL_COUPON_PROBE=0
```

Expected: the profile `$(error ...)`, no build.

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell images SHELL_XTALK_PROBE=1 SHELL_SETTLE_PROBE=1 SHELL_COUPON_PROBE=1
```

Expected: the mutual-exclusion `$(error ...)`, no build.

- [ ] **Step 7: Flash and confirm the block arrives**

Operator into DFU, then:

```bash
dfu-util -a 0 -s 0x90040000:leave -D shell/build/shell-sram.bin
```

```bash
python -c "import serial,sys; ser=serial.Serial(sys.argv[1],timeout=1.0); [sys.stdout.write(ser.readline().decode('utf-8','replace')) for _ in range(40)]" COM4
```

Expected: `SHELL_XTALK_CFG …`, `SHELL_XTALK_RATE …` and `SHELL_XTALK_END`
once per second, and nothing else.

Two things to read and report, both of them measurements this image makes for
the first time:

1. **`block_size` and `sr_hz`.** Report what the board said. `src/hw/board.h`
   sets 96 and 48 kHz, so `scan_settle_ns` should print **2000000**. If the
   board says something else, that is the number the verdict uses and the
   plan's "How the verdict reads" section has to be re-read against it.
2. **No line ends in `$$`.** That is libDaisy's truncation marker. If
   `SHELL_XTALK_CFG` carries one, the byte budget in step 4's comment is
   wrong for this build's git stamp and the line has to lose a field before
   anything else is built on it.

- [ ] **Step 8: Commit**

```bash
git add shell/write_shell_xtalk_probe.py shell/write_git_hash.py shell/xtalk_probe.h shell/xtalk_probe.cpp shell/Makefile shell/main.cpp
git commit -m "feat(shell): the crosstalk probe's build switch and an empty block

An image that prints a well-formed block with nothing in it, on purpose:
'the switch, the link and the USB path work' and 'the instrument works' fail
for different reasons and are worth separating.

SHELL_XTALK_PROBE=1 without SHELL_COUPON_PROBE=1 is a make error rather than
a silent image built against the panel profile, and together with
SHELL_SETTLE_PROBE=1 it is a make error too -- both dispatch before
StartAudio and neither returns, so an image with both would run one and name
neither. The two switch positions were cmp'd, not assumed.

The git stamp is bounded to eight characters because SHELL_XTALK_CFG is
already within nine bytes of libDaisy's 128-byte log buffer, and `git
describe` grows a tag name without warning. The block period behind
scan_settle_ns is READ from the board and printed beside it, with its two
inputs on their own line, so the derivation is checkable from a capture.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: The primitives on the board — the shift, the span, the floor

The first task that measures anything. It delivers the **silent block**: rows
1 and 10 of the plan table, with G2, G4 and G5, which between them answer the
first of `settle-measured.md` §5's two open questions on their own — does the
8–12 count wander survive a board doing nothing at all?

**Files:**
- Modify: `shell/mux_scan.h`, `shell/mux_scan.cpp` (`shift_chain_timed()`)
- Modify: `shell/xtalk_probe.cpp`

**Interfaces:**
- Consumes: `probe_adc::` (Task 1), `shell::step_of()` (Task 1), `xtalk_plan.h` (Tasks 2 and 3), `shell/coupon_expect.h`.
- Produces: `uint32_t MuxScan::shift_chain_timed(uint32_t word)`; and inside `xtalk_probe.cpp`, file-local `park_word()`, `measure_silent()`, `read_span()`, and the `SHELL_XTALK_CLK`, `_CAL`, `_SPAN`, `_G5`, `_CASE`, `_XTALK` and `_GATES` lines.

- [ ] **Step 1: Add the un-latched shift to `MuxScan`**

In `shell/mux_scan.h`, beside `write_chain_timed()`:

```cpp
    // Like write_chain_timed(), but the RCLK pulse is left out entirely:
    // 16 bits are clocked and nothing is latched. Returns the DWT cycle
    // count taken immediately after the last SRCLK falling edge.
    //
    // WHAT THIS IS FOR. The 595's outputs follow its STORAGE register, which
    // moves on RCLK only -- so a shift with no latch is digital supply and
    // ground activity with no change on any mux control line. That is the
    // crosstalk probe's row 9, and it separates "the chain's traffic" from
    // "the bits the chain carries". Read from the 74HC595 datasheet and
    // UNMEASURED on this board; row 9 against row 2 is the check.
    //
    // The 165 is clocked too -- it shares CP -- which is what the shipping
    // scan does on every step anyway, so this is not a quieter event than
    // production, it is the production event minus the latch.
    uint32_t shift_chain_timed(uint32_t word);
```

In `shell/mux_scan.cpp`, beside `write_chain_timed()`:

```cpp
uint32_t MuxScan::shift_chain_timed(uint32_t word)
{
    // Same shift loop as write_chain() and write_chain_timed(), copied
    // rather than reconstructed -- its bit order is load-bearing and the
    // three must not drift apart.
    for(int i = kActiveChain.chain_bits - 1; i >= 0; --i)
    {
        data_.Write(((word >> i) & 1u) != 0u);
        clock_.Write(true);
        clock_.Write(false);
    }
    // No latch_.Write() at all. The line is left where it was -- LOW, the
    // rest state write_chain() leaves it in -- so this produces no RCLK edge
    // and the 595 outputs do not move.
    return cycles_now();
}
```

Read `write_chain()` in the file first and copy its shift order rather than
reconstructing it from this plan.

- [ ] **Step 2: Establish `kScanSettleNs` from `shell/`, in writing**

Before writing any measurement code, read these three places and record what
they say in the task report, with the file and line:

1. `shell/mux_scan.cpp`, `MuxScan::step()` — the order is *read the sense
   pins for `live_step_`, then clock out `next_step_`*. So a channel's address
   is latched at the end of one block's step and read at the start of the
   next.
2. `shell/mux_scan.h`, the header comment above `class MuxScan` — "IT NEVER
   WAITS FOR THE MUX TO SETTLE. The address is clocked out at the end of one
   audio block and sampled at the start of the next, so the block period IS
   the settle window -- 2 ms at 96 samples / 48 kHz."
3. `src/hw/board.h:69-70` — `hw.SetAudioBlockSize(96)` and
   `SAI_48KHZ`.

**There is no named `kScanSettleNs` constant in `shell/` to read.** The value
*is* the audio block period. If a later change introduces such a constant,
use it and say so in the report; until then `scan_settle_ns(block_size,
sr_hz)` computed from the board's own answers is the reading, and Task 4's
image already prints both inputs.

State in the report, as a derived number and not a measured one: at 96 / 48000
the delay is **2 000 000 ns**, and the probe's grid ends at 12 800 ns, so the
whole grid lies before the criterion boundary. That is the input to the
reader's `verdict_basis=envelope` path in Task 7.

- [ ] **Step 3: The span, and G5's yardstick**

Spec §7's G5 asks each victim to read inside its `coupon_expect` band. That
needs a `Span` — and `coupon_span()` takes a whole scan's raw array, which
this probe never takes, while the two 0 Ω victims are both tied to `AGND` and
give no rail. So the probe reads four tie channels of its own and builds the
`Span` by hand, reproducing `coupon_span()`'s semantics with four conversions.

In `shell/xtalk_probe.cpp`, in an anonymous namespace:

```cpp
// The four 0 R tie channels on the 4067, from netlist.py's NEIGHBOURS and
// the spare-channel loop: R_HI1 and R_HI2 to A+3V3, R_LO1 and R_LO2 to AGND.
// Two of each, because a single tie cannot tell "the rail" from "an open
// tie" -- the pair spread is the check coupon_expect.h's kTieSpread exists
// for, and reading only one would adopt a collapsed supply as the reference
// and then pass every victim against it.
constexpr int kTieHi1 = 1, kTieHi2 = 5, kTieLo1 = 3, kTieLo2 = 7;

// Park on (group, ch) and take kRepeats conversions. The park is kParkNs,
// the same 20 us the settle probe uses, which is far past every prediction
// for these channels.
int32_t read_parked(MuxScan& chain, int group, int ch)
{
    chain.write_chain(chain_word(
        kCouponChain, step_pattern(kCouponChain, step_of(kCouponChain, group, ch)), 0u));
    const uint32_t t0 = cycles_now();
    while(cycles_now() - t0 < ns_to_cycles(kParkNs)) { }
    return probe_adc::mean_of_repeats(kRepeats);
}

// coupon_span()'s semantics from four reads instead of twenty-four, plus the
// two tie spreads the verdict turns on. The spreads ride in the return value
// rather than being recomputed at the call site: a G5 failure has four
// possible causes and a reader must be able to see which one gave way
// without re-deriving it from numbers that are not printed.
struct SpanRead
{
    Span    span;
    int32_t hi_spread;
    int32_t lo_spread;
};

SpanRead measure_span(MuxScan& chain)
{
    const int32_t hi1 = read_parked(chain, 0, kTieHi1);
    const int32_t hi2 = read_parked(chain, 0, kTieHi2);
    const int32_t lo1 = read_parked(chain, 0, kTieLo1);
    const int32_t lo2 = read_parked(chain, 0, kTieLo2);

    SpanRead r{};
    r.hi_spread = (hi1 > hi2) ? (hi1 - hi2) : (hi2 - hi1);
    r.lo_spread = (lo1 > lo2) ? (lo1 - lo2) : (lo2 - lo1);

    const int32_t rail = (hi1 + hi2) / 2;
    const int32_t zero = (lo1 + lo2) / 2;
    r.span.rail  = static_cast<uint16_t>(rail);
    r.span.zero  = static_cast<uint16_t>(zero);
    // The validity conditions are coupon_expect.h's own, and each one has a
    // failure behind it: the two ties of a kind must agree inside
    // kTieSpread (one open tie), the rail must clear kRailFloor (a collapsed
    // supply adopted as the reference, which would then pass every victim
    // judged against it), and the rail must sit above the zero (swapped
    // nets).
    r.span.valid = r.hi_spread <= static_cast<int32_t>(kTieSpread)
                   && r.lo_spread <= static_cast<int32_t>(kTieSpread)
                   && rail >= static_cast<int32_t>(kRailFloor)
                   && rail > zero;
    return r;
}
```

and it prints its own evidence:

```cpp
        hw.PrintLine("SHELL_XTALK_SPAN zero=%d rail=%d hi_spread=%d "
                     "lo_spread=%d valid=%d",
                     static_cast<int>(sr.span.zero),
                     static_cast<int>(sr.span.rail),
                     sr.hi_spread, sr.lo_spread, sr.span.valid ? 1 : 0);
```

And per victim, judged with the library function rather than a reimplementation:

```cpp
        for(int v = 0; v < kXtalkVictims; ++v)
        {
            const XtalkVictim& vv = kXtalkVictimTable[v];
            const Expect e = coupon_expect(step_of(kCouponChain, vv.group, vv.channel));
            const bool   ok = sr.span.valid
                && coupon_verdict(e, static_cast<uint16_t>(silent_mean[v]), sr.span);
            if(!ok) address_ok = false;
            hw.PrintLine("SHELL_XTALK_G5 victim_group=%d victim_ch=%d "
                         "expect=%d mean=%d ok=%d",
                         vv.group, vv.channel, static_cast<int>(e),
                         silent_mean[v], ok ? 1 : 0);
        }
```

`silent_mean[v]` is the mean of that victim's whole silent curve, taken in
step 5.

- [ ] **Step 4: The clock and the latency pass**

Copied in shape from `settle_probe.cpp`'s, now through `probe_adc`:

```cpp
    MuxScan chain;
    chain.init();
    probe_adc::init(hw);

    const XtalkVictim& v0 = kXtalkVictimTable[3];   // R_SP10, a 0 R AGND tie
    probe_adc::select(probe_adc::channel_of_group(v0.group));

    const bool warm_ok = probe_adc::warm_up();
    hw.PrintLine("SHELL_XTALK_WARMUP ok=%d init_ok=%d cal_ok=%d cfg_ok=%d",
                 warm_ok ? 1 : 0, probe_adc::init_ok() ? 1 : 0,
                 probe_adc::cal_ok() ? 1 : 0, probe_adc::cfg_ok() ? 1 : 0);

    (void)read_parked(chain, v0.group, v0.channel);   // park for the clock pass
    const probe_adc::Clock clk
        = probe_adc::measure_clock(kRepeats, probe_adc::channel_of_group(v0.group));
```

The 0 Ω AGND tie and not a divider, for the same reason the settle probe used
P0: the clock pass measures spans, and a channel with nothing to settle is the
one whose spans carry the least of anything else.

`SHELL_XTALK_WARMUP` is printed at boot and **may not be the only place those
three flags appear** — `StartLog(false)` does not wait for a host, and
`settle_probe.cpp`'s own comment records the capture in which that line did
not arrive once in 2730 lines. They ride on `SHELL_XTALK_GATES` inside the
loop as well.

Inside the block, the latency pass on the same parked 0 Ω tie, verbatim in
shape from `settle_probe.cpp` (timed-out repeats excluded, `valid_n` dividing
the sums, `-1` when every repeat timed out):

```cpp
        hw.PrintLine("SHELL_XTALK_CLK span_short_cyc=%d span_long_cyc=%d "
                     "smp_short_tenths=%d smp_long_tenths=%d",
                     clk.span_short_cyc, clk.span_long_cyc, 165, 3875);
        hw.PrintLine("SHELL_XTALK_CAL lat_mean_ns=%d lat_min_ns=%d "
                     "lat_max_ns=%d b0=%d timeouts=%d",
                     lat_mean, lat_min, lat_max, b0,
                     static_cast<int>(probe_adc::timeouts()));
```

`b0` is G2's floor and `lat_*` are G4's. Both lines are printed **inside** the
forever loop, not once at boot, for the reason above.

`SHELL_XTALK_CFG`'s `adc_khz` now carries `clk.measured_adc_khz` instead of
Task 4's `-1`.

- [ ] **Step 5: Rows 1 and 10 — the silent block**

A Silent case takes the whole grid with **no chain access and no print**
inside it. That is the only way to get a reading with no 595 traffic and no
USB traffic in the measurement, and it is the case the whole attribution
turns on.

```cpp
// One grid point of a Silent case. There is no latch, so there is no t0 from
// the chain -- the repeat takes its own, spins the SAME park + d it would
// have spun in a Latch case, and converts. The cadence is identical to a
// Latch repeat minus the chain traffic, which is exactly the comparison
// wanted: the difference between this curve and the control curve is the
// shift and the pulse, and nothing else.
Point measure_silent_point(uint32_t d_ns)
{
    const uint32_t park_cycles = ns_to_cycles(kParkNs);
    const uint32_t d_cycles    = ns_to_cycles(d_ns);

    int64_t sum = 0;
    int32_t lo = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    for(int r = 0; r < kRepeats; ++r)
    {
        // cycles_now() - t0 on uint32_t wraps correctly with no special
        // case: the DWT counter is free-running and unsigned specifically so
        // this subtraction is always valid, even across a wrap. Do not "fix"
        // it.
        const uint32_t t0 = cycles_now();
        while(cycles_now() - t0 < park_cycles + d_cycles) { }
        const int32_t v = probe_adc::sample_now(nullptr);
        sum += v;
        if(v < lo) lo = v;
        if(v > hi) hi = v;
    }
    return Point{static_cast<int32_t>(sum / kRepeats), lo, hi};
}
```

Per Silent case: park once on `word_a` before the grid, select the victim's
channel and its rung, then walk the grid. Row 1 buffers all 65 points and
prints them **after** the grid completes; row 10 prints as it goes. The line
format is identical either way, which is deliberate — the record of which is
which is the `row` field on `SHELL_XTALK_CASE`.

Two statistics per Silent case, both of them `settle-measured.md` §5's own,
printed on their own line:

```cpp
        hw.PrintLine("SHELL_XTALK_STAT case=%d settled_mean_spread=%d "
                     "widest_sample_band=%d at_d_ns=%d",
                     i, mean_spread, widest_band, widest_band_d_ns);
```

- `settled_mean_spread` — peak to peak of the 65 per-point means. §5 measured
  **8–12** counts on the divider pairs and **0–1** on the 0 Ω ties, with the
  board running the settle probe's own sweep. If it comes back at 0–1 on the
  dividers here, the wander needed the instrument's own traffic. If it comes
  back at 8–12 with the board otherwise idle, the wander is the node or the
  reference and none of the later rows can be blamed for it.
- `widest_sample_band` — the raw min–max per grid point and where it sat. §5
  measured single-point bands of **44…197** counts, on the 0 Ω ties as well.
  They either survive silence or they do not.

Both are reported, neither is gated. Spec §7 is explicit that the settled
region's width is a quantity here and not a gate.

The per-case lines, with the byte budget spelled out:

```cpp
        // BYTE BUDGET, and this is why the spec's single SHELL_XTALK line is
        // split in two here. libDaisy's log buffer is 128 bytes
        // (lib/libDaisy/src/hid/logger.h:29); the spec's combined line runs
        // about 150 and would be truncated and stamped "$$", which is how
        // settle_probe.cpp lost two fields before anyone noticed. This line
        // runs 114 bytes at its widest values; the point line below runs 67.
        hw.PrintLine("SHELL_XTALK_CASE case=%d row=%d kind=%d victim_group=%d "
                     "victim_ch=%d r_src=%d word_a=%d word_b=%d skipped=%d",
                     i, static_cast<int>(c.row), static_cast<int>(c.kind),
                     c.group, c.channel, static_cast<int>(c.r_src_ohm),
                     static_cast<int>(c.word_a), static_cast<int>(c.word_b),
                     skipped ? 1 : 0);
        hw.PrintLine("SHELL_XTALK case=%d d_ns=%d n=%d mean=%d min=%d max=%d",
                     i, static_cast<int>(grid_ns(k)), kRepeats,
                     pts[k].mean, pts[k].min, pts[k].max);
```

And the gates line, with the three HAL statuses riding along:

```cpp
        hw.PrintLine("SHELL_XTALK_GATES g2=%d g4=%d g5=%d g6=%d init_ok=%d "
                     "cal_ok=%d cfg_ok=%d gates_ok=%d",
                     gates.g2_floor ? 1 : 0, gates.g4_jitter ? 1 : 0,
                     gates.g5_address ? 1 : 0, gates.g6_control ? 1 : 0,
                     probe_adc::init_ok() ? 1 : 0, probe_adc::cal_ok() ? 1 : 0,
                     probe_adc::cfg_ok() ? 1 : 0, gates.ok() ? 1 : 0);
        hw.PrintLine("SHELL_XTALK_END");
```

For this task `worst_control_delta` is `0` and G6 therefore passes
vacuously — Task 6 computes it. **Say so in a comment at the assignment**, so
nobody reads this block's `g6=1` as a verdict about a control curve that was
never taken.

Each victim is read at the rung `sample_time_index_for(r_src_ohm)` picks for
its impedance, through `probe_adc::sample_time_for_rung()`. `settle-measured.md`
§7 measured that this rung reads about 200 counts low at 5150 Ω in steady
state. No absolute level from this probe may be quoted without that
qualification — and every `delta` is a difference of two readings at the same
rung, so the bias cancels there.

- [ ] **Step 6: Build, cmp, flash**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_XTALK_PROBE=1
```

```bash
cp shell/build/shell-sram.bin /tmp/xtalk-task5.bin; cmp /tmp/xtalk-task5.bin /tmp/xtalk0.bin
```

Expected: differ. Flash as in Task 4 and capture one whole block:

```bash
python -c "import serial,sys; ser=serial.Serial(sys.argv[1],timeout=1.0); [sys.stdout.write(ser.readline().decode('utf-8','replace')) for _ in range(2000)]" COM4 > /tmp/xtalk-silent.log
```

- [ ] **Step 7: Read the result honestly**

Report, in this order:

1. **`gates_ok`, and which gate failed if any.** A failed G2 or G4 indicts the
   instrument and the numbers below may not be quoted. A failed G5 means a
   victim is not where the table says and *nothing* in the block means
   anything — report the `SHELL_XTALK_SPAN` and `SHELL_XTALK_G5` lines and
   stop.
2. **`SHELL_XTALK_CLK` and `SHELL_XTALK_CAL` against Task 1's baseline.**
   `adc_khz` should read 6146 and `lat_max - lat_min` should fall in the
   42…138 ns band. This is the second, independent confirmation that
   `probe_adc` behaves the same in both images.
3. **The silent curves' `settled_mean_spread`, all five victims.** Against
   `settle-measured.md` §5's 8–12 (dividers) and 0–1 (ties). **This is the
   task's headline result** and it is the first measurement that speaks to
   §5's cost-if-wrong paragraph. Say plainly which way it came out; do not
   round it toward either expectation.
4. **The silent curves' `widest_sample_band` and where it sat.** Against §5's
   44…197.
5. **Row 10 against row 1, per victim** — the same statistic with `PrintLine`
   inside the grid instead of after it. That difference *is* the USB-CDC/DMA
   candidate §5 names, and this is the first number anyone has had for it.

Do not adjust a constant to make a gate pass. If a gate fails, that is the
deliverable.

- [ ] **Step 8: Commit**

```bash
git add shell/mux_scan.h shell/mux_scan.cpp shell/xtalk_probe.cpp
git commit -m "feat(shell): the crosstalk probe's silent block, and its own span

A grid taken with no chain access and no print inside it, which is the only
way to read a settled channel with neither the 595's traffic nor USB-CDC's
in the measurement. Row 10 repeats it with PrintLine after every point, so
the difference between the two IS the DMA candidate settle-measured.md
section 5 names and could not test.

G5 builds its own span rather than calling coupon_span(): that function needs
a whole scan's raw array, which this probe never takes, and both 0 ohm
victims are tied to AGND so there is no rail among them. Four tie reads --
two per rail, because a single tie cannot tell 'the rail' from 'an open tie'
-- reproduce its semantics and print their own evidence.

shift_chain_timed() clocks 16 bits and latches nothing, so row 9 can ask
what the chain's traffic alone does. That the 595 outputs stay put is read
from the datasheet and unmeasured on this board; row 9 against row 2 is the
check, and it is Task 6's.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: The whole table — control, aggressors, static, shift-only

**Files:**
- Modify: `shell/xtalk_probe.cpp`

**Interfaces:**
- Consumes: everything from Tasks 1, 2, 3 and 5.
- Produces: the complete `SHELL_XTALK` block of spec §8, including `SHELL_XTALK_STATIC`, and a `worst_control_delta` that makes G6 mean something.

- [ ] **Step 1: The Latch and ShiftOnly measurement**

```cpp
// One grid point of a Latch or ShiftOnly case.
//
// The victim's address and enable are the same in both words (xtalk_plan.h's
// word builder guarantees it and test_xtalk_plan.cpp asserts it), so the
// victim never changes channel and the sample-and-hold's charge
// redistribution -- settle-measured.md section 7's 845-count first arrival --
// is not in this measurement at all.
Point measure_event_point(MuxScan& chain, const XtalkCase& c, uint32_t d_ns)
{
    const uint32_t d_cycles    = ns_to_cycles(d_ns);
    const uint32_t park_cycles = ns_to_cycles(kParkNs);

    int64_t sum = 0;
    int32_t lo = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    for(int r = 0; r < kRepeats; ++r)
    {
        // The park establishes BOTH things the event needs: a settled victim
        // and a defined starting state for the aggressor bits.
        chain.write_chain(c.word_a);
        const uint32_t park_t0 = cycles_now();
        while(cycles_now() - park_t0 < park_cycles) { }

        // t = 0 is the 595's own RCLK edge for a Latch case, and the last
        // SRCLK falling edge for a ShiftOnly case. Neither is the call site:
        // 16 clocked bits in front of the event would fold into every
        // reading as a constant nobody measured.
        const uint32_t t0 = (c.kind == XtalkKind::Latch)
                                ? chain.write_chain_timed(c.word_b)
                                : chain.shift_chain_timed(c.word_a);
        while(cycles_now() - t0 < d_cycles) { }

        const int32_t v = probe_adc::sample_now(nullptr);
        sum += v;
        if(v < lo) lo = v;
        if(v > hi) hi = v;
    }
    return Point{static_cast<int32_t>(sum / kRepeats), lo, hi};
}
```

- [ ] **Step 2: The Static measurement**

```cpp
// A Static case: one word held, kRepeats conversions, no grid and no event.
// Row 8 is the DC shift of the ground join under eight LEDs, and a DC shift
// has no d -- the reader differences the dark case's mean against the lit
// case's for the same victim.
Point measure_static(MuxScan& chain, const XtalkCase& c)
{
    chain.write_chain(c.word_a);
    const uint32_t t0 = cycles_now();
    while(cycles_now() - t0 < ns_to_cycles(kParkNs)) { }

    int64_t sum = 0;
    int32_t lo = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    for(int r = 0; r < kRepeats; ++r)
    {
        const int32_t v = probe_adc::sample_now(nullptr);
        sum += v;
        if(v < lo) lo = v;
        if(v > hi) hi = v;
    }
    return Point{static_cast<int32_t>(sum / kRepeats), lo, hi};
}
```

printed as:

```cpp
        hw.PrintLine("SHELL_XTALK_STATIC case=%d victim_group=%d victim_ch=%d "
                     "r_src=%d word=%d n=%d mean=%d min=%d max=%d",
                     i, c.group, c.channel, static_cast<int>(c.r_src_ohm),
                     static_cast<int>(c.word_a), kRepeats,
                     p.mean, p.min, p.max);
```

A Static case still gets its `SHELL_XTALK_CASE` line, so the reader's
completeness check is one rule for all 58 cases.

- [ ] **Step 3: Skipped cases are lines, not absences**

```cpp
        // Spec section 4: a skipped case is a LINE in the output, never a
        // silently shorter table. A reader that has to infer an absence
        // cannot tell "not run" from "lost in transit", and USB-CDC on this
        // machine does lose whole lines.
        const bool skipped = c.needs_rv4 && (SHELL_XTALK_RV4 == 0);
        if(skipped)
        {
            print_case_line(hw, i, c, true);
            continue;   // no SHELL_XTALK points, no SHELL_XTALK_STATIC
        }
```

- [ ] **Step 4: G6, and the order the block runs in**

Rows 1 and 2 run first for every victim, and row 1's five curves are held for
the whole block:

```cpp
        // Row 1's curves, kept for the block. 5 victims x 65 points x 4 bytes
        // is 1300 bytes -- nothing on this board, and the only way G6 can be
        // a difference between two curves rather than a difference between a
        // curve and a memory.
        int32_t silent_mean_curve[kXtalkVictims][kGridPoints];
```

and G6 folds over both:

```cpp
        // G6: for each victim, max |mean_control(d) - mean_silent(d)| over
        // the whole grid. The control carries the same shift and the same
        // latch pulse as every aggressor case, so if the control alone has
        // already moved the reading past the criterion, the aggressor deltas
        // are no longer interpretable as differences.
        int32_t worst = 0;
        for(int k = 0; k < kGridPoints; ++k)
        {
            const int32_t d = control_curve[k] - silent_mean_curve[v][k];
            const int32_t a = (d < 0) ? -d : d;
            if(a > worst) worst = a;
        }
        if(worst > summary.worst_control_delta) summary.worst_control_delta = worst;
```

Every other row's `delta` is computed **by the reader**, not here. Spec §5 is
explicit: the firmware prints curves and gates; the verdict is the reader's.

- [ ] **Step 5: Build, cmp, flash, and time the block**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_XTALK_PROBE=1
```

```bash
cp shell/build/shell-sram.bin /tmp/xtalk-task6.bin; cmp /tmp/xtalk-task6.bin /tmp/xtalk-task5.bin
```

Expected: differ. Flash, then capture with a generous line budget — the block
is much longer than the settle probe's:

```bash
python -c "import serial,sys,time; ser=serial.Serial(sys.argv[1],timeout=1.0); t=time.monotonic(); [sys.stdout.write(ser.readline().decode('utf-8','replace')) for _ in range(12000)]; print('elapsed %.1f s' % (time.monotonic()-t), file=sys.stderr)" COM4 > /tmp/xtalk-full.log
```

**Report the measured wall-clock time of one whole block.** The plan's
estimate is *derived*: 44 grid cases × 65 points × 64 repeats at roughly
36 µs a repeat is about 6.6 s of measurement plus about 2 900 printed lines,
so a block near 10 s. That is arithmetic and not a measurement, and Task 7's
reader needs the real number for its default timeout.

Also confirm, and report:
- `SHELL_XTALK_CASE` appears exactly 58 times in one block.
- the four `row=6` cases carry `skipped=1` (RV4 is not fitted).
- no line ends in `$$`.

- [ ] **Step 6: Read the result honestly**

Report, in this order:

1. `gates_ok` and each gate. **If G6 failed, stop and report the control
   curves.** That is the spec working: a latch pulse alone moving a 5150 Ω
   channel past the criterion is a finding about the board, and it makes every
   aggressor difference uninterpretable. Do not quote a single aggressor
   delta from such a run.
2. If the gates passed, the raw material for the verdict — but **do not
   compute the verdict by hand**. Task 7's reader computes it and its
   arithmetic is fixture-tested; a hand-computed verdict in a task report is
   a number with no guard behind it. Report instead: the largest per-point
   `mean` excursion you see in each row, per victim, and whether it scales
   with `r_src`.
3. **The R-ladder, which speaks before any curve is read** (spec §9): if the
   two 0 Ω victims show the same excursion as the 5150 Ω ones, the coupling is
   not into the node. Say which way it came out.
4. **Row 9 against row 2** — shift without latch against latch without bit
   change. A row-9 excursion on a 0 Ω victim that matches its control would
   say the shift alone does nothing to the 595 outputs, which is the
   datasheet claim spec §10 marks unmeasured on this board.
5. **Row 8's two means, per victim.** The DC shift of the star point under
   eight LEDs at roughly 1.3 mA each (derived — (3.3 V − V_f) / 1 kΩ at a
   green LED's ~2 V; the spec's "~2 mA" is an upper bound).

- [ ] **Step 7: Commit**

```bash
git add shell/xtalk_probe.cpp
git commit -m "feat(shell): the crosstalk sweep, all fifty-eight cases

Each repeat parks on word_a for 20 us -- which settles the victim AND gives
the aggressor bits a defined starting state -- then latches word_b taking t0
from the 595's own RCLK edge, or shifts word_a taking t0 from the last SRCLK
edge, spins the commanded delay and converts. The victim's address and
enable are identical in both words, so the sample-and-hold's channel-change
transient is not in the measurement at all.

Rows 1 and 2 run first and row 1's five curves are held for the whole block,
because G6 has to be a difference between two curves taken in the same block
rather than a difference between a curve and a memory.

A skipped case is a line with skipped=1, never an absence: USB-CDC on this
machine loses whole lines, and a reader that has to infer an absence cannot
tell 'not run' from 'lost in transit'.

No verdict is computed here. The firmware prints curves and gates; the
reader decides, and its arithmetic has fixtures behind it.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 7: The reader and its guard

**Files:**
- Create: `shell/read_xtalk.py`, `shell/test_read_xtalk.py`
- Modify: `CMakeLists.txt` (after the `read_settle_guard` block, around line 323)

**Interfaces:**
- Consumes: the block format from Tasks 4, 5 and 6.
- Produces: `parse_block(lines)`, `format_csv(block)`, `format_meta_csv(block)`, `deltas(block)`, `verdicts(block)` — pure functions the guard exercises without pyserial and without a board. `xtalk.csv.meta.csv`'s `silent` scope is what the codec-tone plan's G8 reads.

- [ ] **Step 1: Write the failing guard**

Create `shell/test_read_xtalk.py`, following `shell/test_read_settle.py`: a
plain script, exit code is the verdict, no pytest (not installed here), and
the fixture is **generated** rather than pasted, because a 58-case block
pasted as literal strings is unreadable and goes stale the first time a field
moves.

```python
"""Guard for read_xtalk.py's parser, its delta arithmetic and its verdict.

Runs as a plain script; pytest is not installed on this machine, and the
tools/ guards that had no runner stood red for 23 days behind exactly that
gap.

build_block() generates a format-faithful block at the smallest size that
still exercises every rule -- two victims, two grid points -- rather than
pasting 58 cases. A literal fixture of this block would be unreadable and
would go stale the first time a field moved, which is what happened to the
settle plan's own SAMPLE block.
"""
import sys

from read_xtalk import (parse_block, format_csv, format_meta_csv, deltas,
                        verdicts)

FAILURES = []


def check(label, cond):
    if not cond:
        FAILURES.append(label)


def build_block(scan_settle_ns=2000000, grid_points=2, gates_ok=1,
                aggressor_means=(100, 100), control_means=(100, 100),
                silent_means=(100, 100)):
    """Two victims (one 5150 ohm divider, one 0 ohm tie), four cases:
    silent, control, one LED aggressor and one static, per victim."""
    lines = [
        "SHELL_XTALK_CFG adc_khz=6146 repeats=64 grid_ns=200 points=%d "
        "park_ns=20000 scan_settle_ns=%d rv4=0 git=4a7800f+"
        % (grid_points, scan_settle_ns),
        "SHELL_XTALK_RATE block_size=96 sr_hz=48000 cases=10 victims=2",
        "SHELL_XTALK_CLK span_short_cyc=2000 span_long_cyc=30000 "
        "smp_short_tenths=165 smp_long_tenths=3875",
        "SHELL_XTALK_CAL lat_mean_ns=747 lat_min_ns=700 lat_max_ns=800 "
        "b0=32 timeouts=0",
        "SHELL_XTALK_SPAN zero=0 rail=63485 hi_spread=1 lo_spread=1 valid=1",
    ]
    victims = ((0, 8, 5150), (0, 10, 150))
    for g, ch, _r in victims:
        lines.append("SHELL_XTALK_G5 victim_group=%d victim_ch=%d expect=2 "
                     "mean=100 ok=1" % (g, ch))

    case = 0
    for vi, (g, ch, r) in enumerate(victims):
        for row, kind, wa, wb, means in (
                (1,  0, 0x28, 0x28, silent_means),
                (2,  1, 0x28, 0x28, control_means),
                (7,  1, 0x28, 0x3FE8, aggressor_means)):
            lines.append("SHELL_XTALK_CASE case=%d row=%d kind=%d "
                         "victim_group=%d victim_ch=%d r_src=%d word_a=%d "
                         "word_b=%d skipped=0"
                         % (case, row, kind, g, ch, r, wa, wb))
            for k in range(grid_points):
                lines.append("SHELL_XTALK case=%d d_ns=%d n=64 mean=%d "
                             "min=%d max=%d"
                             % (case, k * 200, means[k], means[k] - 2,
                                means[k] + 2))
            if row == 1:
                lines.append("SHELL_XTALK_STAT case=%d settled_mean_spread=%d "
                             "widest_sample_band=44 at_d_ns=0"
                             % (case, 9 + vi))
            case += 1

    for vi, (g, ch, r) in enumerate(victims):
        for word, mean in ((0x28, 100), (0x3FE8, 104)):
            lines.append("SHELL_XTALK_CASE case=%d row=8 kind=3 "
                         "victim_group=%d victim_ch=%d r_src=%d word_a=%d "
                         "word_b=%d skipped=0" % (case, g, ch, r, word, word))
            lines.append("SHELL_XTALK_STATIC case=%d victim_group=%d "
                         "victim_ch=%d r_src=%d word=%d n=64 mean=%d min=%d "
                         "max=%d" % (case, g, ch, r, word, mean, mean - 2,
                                     mean + 2))
            case += 1

    lines.append("SHELL_XTALK_GATES g2=%d g4=%d g5=%d g6=%d init_ok=1 "
                 "cal_ok=1 cfg_ok=1 gates_ok=%d"
                 % (gates_ok, gates_ok, gates_ok, gates_ok, gates_ok))
    lines.append("SHELL_XTALK_END")
    return lines


def drop_first(lines, prefix):
    out = list(lines)
    for i, line in enumerate(out):
        if line.startswith(prefix):
            del out[i]
            return out
    raise AssertionError("no line starts with %r" % prefix)


# --- 1-4: a complete block, and what it carries ---
base = build_block()
block = parse_block(base)
check("a complete block parses", block is not None)
check("every case is kept", block and len(block["cases"]) == 10)
check("every point is kept", block and len(block["points"]) == 12)
check("the verdict bits are carried",
      block and block["gates"]["gates_ok"] == 1)

# --- 5: cut off before the end marker ---
check("a block truncated before SHELL_XTALK_END is refused",
      parse_block(base[:-1]) is None)

# --- 6: a missing point row ---
# A serial read timeout produces exactly this: the end marker arrives, a row
# does not. Only a per-case point count catches it; a total would not, because
# another case still has its points.
check("a block missing one case's point row is refused",
      parse_block(drop_first(base, "SHELL_XTALK case=1 d_ns=200")) is None)

# --- 7: a missing CASE line ---
check("a block missing a SHELL_XTALK_CASE line is refused",
      parse_block(drop_first(base, "SHELL_XTALK_CASE case=2 ")) is None)

# --- 8: a missing STATIC line ---
check("a block whose static case has no measurement is refused",
      parse_block(drop_first(base, "SHELL_XTALK_STATIC case=6 ")) is None)

# --- 9: a failed run still parses and is marked ---
failed = parse_block(build_block(gates_ok=0))
check("a failed run still parses", failed is not None)
check("a failed run is marked", failed and failed["gates"]["gates_ok"] == 0)

# --- 10: the delta arithmetic ---
# delta(d) = mean_aggressor(d) - mean_control(d), per victim. Not against the
# silent curve: the control carries the same shift and the same latch pulse,
# so the difference isolates the bit change.
d = deltas(parse_block(build_block(aggressor_means=(106, 103),
                                   control_means=(100, 100))))
check("delta is aggressor minus control, per grid point",
      d and [row["delta"] for row in d if row["case"] == 2] == [6, 3])
check("delta is computed for every victim's aggressor",
      d and sorted({row["case"] for row in d}) == [2, 5])

# --- 11: the verdict, when the criterion lies past the grid ---
# The shipping scan waits a whole audio block (2 ms) and the grid ends at
# 12.8 us, so NO grid point is at or past the criterion boundary. The verdict
# must NOT be computed over an empty set and reported as a pass; it falls
# back to the whole-grid envelope and says so.
v = verdicts(parse_block(build_block(aggressor_means=(106, 103))))
check("a criterion past the end of the grid reports an envelope",
      v and all(row["verdict_basis"] == "envelope" for row in v))
check("the envelope is the largest magnitude over the whole grid",
      v and max(row["worst_delta"] for row in v) == 6)
check("the envelope verdict still applies the 8-count criterion",
      v and all(row["pass"] for row in v))
fail_v = verdicts(parse_block(build_block(aggressor_means=(109, 100))))
check("an envelope past 8 counts fails",
      fail_v and not all(row["pass"] for row in fail_v))

# --- 12: the verdict, when the criterion lies inside the grid ---
# The other branch, which a shipping firmware that waited 400 ns would take.
# The point at d_ns=0 is BEFORE the boundary and is characterisation, not a
# verdict; only d_ns=200 counts.
inside = verdicts(parse_block(build_block(scan_settle_ns=200,
                                          aggressor_means=(900, 103))))
check("a criterion inside the grid uses the criterion",
      inside and all(row["verdict_basis"] == "criterion" for row in inside))
check("points before the boundary are characterisation, not verdict",
      inside and max(row["worst_delta"] for row in inside) == 3)
check("and the verdict passes on the points that count",
      inside and all(row["pass"] for row in inside))

# --- 13: a failed gate refuses the verdict ---
check("a run whose gates failed yields no per-aggressor verdict",
      verdicts(parse_block(build_block(gates_ok=0))) == [])

# --- 14: the CSV ---
csv = format_csv(block)
check("the CSV has a header and one row per point",
      len(csv.strip().split("\n")) == 13)
check("the CSV carries the case's row so the spec table is recoverable",
      "row" in csv.splitlines()[0])

# --- 15: the metadata CSV ---
meta_rows = [r.split(",") for r in format_meta_csv(block).strip().split("\n")]
check("the metadata CSV has the four-column header",
      meta_rows[0] == ["scope", "case", "key", "value"])
meta = {(r[0], r[1], r[2]): r[3] for r in meta_rows[1:]}
check("the metadata carries the configuration",
      meta.get(("cfg", "", "scan_settle_ns")) == "2000000")
check("the metadata carries the block period's two inputs",
      meta.get(("rate", "", "block_size")) == "96"
      and meta.get(("rate", "", "sr_hz")) == "48000")
check("the metadata carries the gate verdicts",
      all(meta.get(("gates", "", g)) == "1" for g in ("g2", "g4", "g5", "g6")))
check("the metadata carries the span G5 was judged against",
      meta.get(("span", "", "rail")) == "63485")
# The codec-tone probe's G8 reads exactly this, by case, off this file.
check("the metadata carries each silent case's settled mean spread",
      meta.get(("silent", "0", "settled_mean_spread")) == "9"
      and meta.get(("silent", "3", "settled_mean_spread")) == "10")
check("the metadata carries the static cases' means",
      meta.get(("static", "6", "mean")) == "100"
      and meta.get(("static", "7", "mean")) == "104")

# --- 16: libDaisy's "$$" overflow marker ---
truncated = list(base)
truncated[-2] = truncated[-2][:-2] + "$$"
check("a $$-truncated field does not crash the parser, and yields no block",
      parse_block(truncated) is None)
spliced = list(base)
spliced[-1] = "S$$SHELL_XTALK_END"
check("a spliced end marker does not crash the parser, and yields no block",
      parse_block(spliced) is None)

if FAILURES:
    for f in FAILURES:
        print("FAIL: %s" % f, file=sys.stderr)
    raise SystemExit(1)
print("read_xtalk guard: ok")
```

- [ ] **Step 2: Run it to verify it fails**

```bash
python shell/test_read_xtalk.py
```

**Expected RED:** `ModuleNotFoundError: No module named 'read_xtalk'`. It is
red because the module does not exist. Run it with `shell/` as the working
directory — the guard imports its neighbour, which is why the `add_test` below
sets `WORKING_DIRECTORY`.

- [ ] **Step 3: Write the reader**

Create `shell/read_xtalk.py`, following `read_settle.py` exactly in shape:
`import serial` **inside** `main()` so the guard can import the module without
pyserial, and accumulate until the end marker rather than returning on the
first match.

`_is_complete()` has four rules, and each one exists because a different
corruption gets past the other three:

```python
def _is_complete(block):
    if block["cfg"] is None or block["rate"] is None or block["gates"] is None:
        return False
    # 1. Every case the firmware said it would print, printed. The count is
    #    the firmware's own (SHELL_XTALK_RATE's `cases=`), not this reader's
    #    idea of how big the table is -- a reader that carried its own 58
    #    would refuse every capture the day a row is added.
    if len(block["cases"]) != block["rate"]["cases"]:
        return False
    if sorted(c["case"] for c in block["cases"]) != list(range(block["rate"]["cases"])):
        return False
    # 2. Every grid case, and only a grid case, has exactly grid_points
    #    points. PER CASE: a serial timeout loses one case's row while its
    #    neighbours keep theirs, and a total count reports that as complete.
    want = Counter()
    for c in block["cases"]:
        if c["skipped"]:
            continue
        if c["kind"] == KIND_STATIC:
            continue
        want[c["case"]] = block["cfg"]["points"]
    if Counter(p["case"] for p in block["points"]) != want:
        return False
    # 3. Every Static case has its one measurement.
    want_static = {c["case"] for c in block["cases"]
                   if c["kind"] == KIND_STATIC and not c["skipped"]}
    if {s["case"] for s in block["statics"]} != want_static:
        return False
    # 4. Every Silent case has its two reported statistics -- they are what
    #    settle-measured.md section 5 could not attribute, and round two's G8
    #    reads them out of the metadata file.
    want_stat = {c["case"] for c in block["cases"]
                 if c["kind"] == KIND_SILENT and not c["skipped"]}
    if {s["case"] for s in block["stats"]} != want_stat:
        return False
    return True
```

`KIND_SILENT = 0`, `KIND_LATCH = 1`, `KIND_SHIFT_ONLY = 2` and
`KIND_STATIC = 3` are module constants mirroring `XtalkKind` in
`shell/xtalk_plan.h`, with a comment pointing at it — the numbering is part
of the printed format and may not be reordered on either side.

The parts that are this reader's own, and their reasons:

```python
def deltas(block):
    """delta(d) = mean_aggressor(d) - mean_control(d), per aggressor case.

    Against the CONTROL and not the silent curve, which is spec section 5's
    choice and not a detail: the control carries the same 16-bit shift and
    the same RCLK pulse as the aggressor case, so the difference isolates the
    bit change. Against the silent curve it would isolate the bit change plus
    the pulse plus the shift, which is three findings folded into one number.

    The silent curve is reported beside both, and its own two statistics
    (settled_mean_spread, widest_sample_band) are findings in their own
    right -- they are what settle-measured.md section 5 could not attribute.
    """
```

```python
def verdicts(block):
    """The per-aggressor verdict, and which of the two bases it rests on.

    Spec section 5's criterion is |delta(d)| <= 8 at every grid point at or
    past scan_settle_ns -- the delay the shipping scan gives an address
    before it reads it, which the firmware prints and does not assume.

    On this firmware that delay is the audio block period, 2 ms, and the grid
    ends at 12.8 us. So no grid point reaches the boundary, and a reader that
    applied the criterion literally would compute a maximum over an empty set
    and report a pass. It does not. It falls back to the envelope over the
    WHOLE grid and labels the row verdict_basis=envelope.

    The fallback is not a weakening. The shipping ADC free-runs a circular
    DMA (settle-measured.md section 7), so a pot read lands at an arbitrary
    phase relative to any scan event, and the envelope over d is the worst
    case such a read can meet -- which is the argument spec section 6 makes
    in as many words. It is strictly more pessimistic than the criterion
    branch, and it is never labelled as a measurement taken at 2 ms.

    A run whose gates did not pass yields NO verdicts at all: the differences
    stop being interpretable, which is the whole point of G6.
    """
```

The `main()` epilogue prints the three things a reader must not miss, and
returns 1 on a failed gate or a failed verdict:

```python
    if not block["gates"]["gates_ok"]:
        failed = [name for key, name in (
            ("g2", "instrument floor"), ("g4", "instrument jitter"),
            ("g5", "victim addressing"), ("g6", "the control curve"))
            if not block["gates"][key]]
        print("GATES FAILED (%s) -- no crosstalk verdict from this run may be "
              "quoted" % ", ".join(failed), file=sys.stderr)
        if not block["gates"]["g6"]:
            print("G6 in particular may be a REAL RESULT and not a broken "
                  "instrument: a latch pulse with no bit change already moved "
                  "a reading past the criterion. The control curves are in "
                  "the CSV and they are this run's finding.", file=sys.stderr)
        return 1
```

Default timeout: use the block duration Task 6 **measured**, rounded up
generously — not the plan's derived estimate. Put the measured number in the
module docstring with its date.

- [ ] **Step 4: Run the guard to verify it passes**

```bash
python shell/test_read_xtalk.py
```

Run with `shell/` as the working directory. Expected: `read_xtalk guard: ok`.

- [ ] **Step 5: Register it with ctest**

In `CMakeLists.txt`, after the `read_settle_guard` block:

```cmake
add_test(NAME read_xtalk_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_xtalk.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)
```

- [ ] **Step 6: Run the whole suite**

```bash
source env.sh; cmake -S . -B build -DCMAKE_BUILD_TYPE=Release; cmake --build build; ctest --test-dir build --output-on-failure
```

Expected: every test passes, including `read_settle_guard`,
`read_coupon_guard` and the new `read_xtalk_guard`.

- [ ] **Step 7: Prove the RED twice**

**First red — the completeness check.** In `_is_complete()`, change rule 2's
per-case point count from

```python
    if Counter(p["case"] for p in block["points"]) != want:
        return False
```

to

```python
    if len(block["points"]) < 0:
        return False
```

Run the guard. **Expected: FAIL** on "a block missing one case's point row is
refused". It is red because a total count — and this sabotage is not even
that — cannot notice that case 1 lost a row while case 0 kept both; only a
per-case count can, and USB-CDC on this machine loses whole lines. The settle
probe's bring-up plan shipped a version of this check that could not go red;
do not repeat that.

**Second red — the envelope fallback.** In `verdicts()`, change the boundary
selection from

```python
    at_or_past = [row for row in rows if row["d_ns"] >= scan_settle_ns]
    basis = "criterion" if at_or_past else "envelope"
    considered = at_or_past if at_or_past else rows
```

to

```python
    considered = [row for row in rows if row["d_ns"] >= scan_settle_ns]
    basis = "criterion"
```

Run the guard. **Expected: FAIL** on "a criterion past the end of the grid
reports an envelope" and on "an envelope past 8 counts fails". It is red
because with the fallback gone, `considered` is empty for today's 2 ms
boundary, `max()` over it is either an exception or a default, and the run
that was never examined at all reports as a pass under the label
`criterion` — the single most dangerous output this reader could produce.

**Retype** both originals; do not `git checkout`. Re-run, confirm green,
report what you saw for each.

- [ ] **Step 8: Read a real block from the board**

Flash Task 6's image if the board is not still carrying it, then:

```bash
python shell/read_xtalk.py COM4 xtalk.csv
```

Report: the exit code, the stderr summary, the row count in `xtalk.csv`, the
row count in `xtalk.csv.meta.csv`, and the `verdict_basis` the run used. Then
the verdicts themselves — per row of the spec's §4 table, per victim, with
`worst_delta` — and say plainly for each whether the criterion held.

Do not put `xtalk.csv` in the repo. `docs/hardware/` gets a measured document
written from it, which is separate work and not this plan's.

- [ ] **Step 9: Commit**

```bash
git add shell/read_xtalk.py shell/test_read_xtalk.py CMakeLists.txt
git commit -m "feat(shell): the crosstalk reader, and a guard with a runner

Accumulates until the end marker and counts points PER CASE, not in total: a
serial timeout loses one case's row while its neighbours keep theirs, and a
total count reports that block as complete.

delta is computed against the control curve and not the silent one, which is
the spec's choice and not a detail -- the control carries the same shift and
the same RCLK pulse, so the difference isolates the bit change instead of
folding three findings into one number.

The verdict has two bases and says which. The shipping scan waits a whole
audio block, 2 ms, and this grid ends at 12.8 us, so no grid point reaches
the criterion boundary; a reader that applied the criterion literally would
maximise over an empty set and print a pass. It falls back to the envelope
over the whole grid, labels it, and is strictly more pessimistic. Both
branches have fixtures and the fallback was red-proven by deleting it.

The metadata file carries each silent case's settled_mean_spread by case,
which is what round two's G8 reads to check its own floor against this one.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Self-review against the spec

| Spec section | Where it lands |
|---|---|
| §1 what this is | The whole plan; the "depends on the settle probe having run" clause is Task 1 |
| §2 what can be pitted against what | Task 2 (the victim table and the aggressor bits); the `needs_rv4` variant is Task 2's row 6 and Task 6 step 3 |
| §3 the instrument is the settle probe with a different word pair | Task 1 (the refactor and its board proof), Task 5 step 1 (`shift_chain_timed`), Task 5 step 5 (the block that touches the chain zero times), Task 4 (the switch, the `SHELL_SETTLE_PROBE` pattern, the dispatch before `StartAudio`) |
| §4 the plan table | Task 2 (all 58 entries, all ten rows, both edge directions); skipped cases Task 6 step 3 |
| §5 three levels per victim, and the statistic | Task 5 step 5 (`settled_mean_spread`, `widest_sample_band` on the silent curve), Task 6 step 4 (the control), Task 7 (`deltas()`, the verdict) |
| §6 what the instrument cannot see | Task 5 step 5's rung comment (the 200-count steady-state bias, and why it cancels in a difference); the offset floor and the jitter are inherited through `probe_adc` and re-confirmed in Task 5 step 7 item 2. The reverse-order sequence is explicitly round two's |
| §7 gates | Task 3 (G2, G4, G6 and the `address_ok` fold), Task 5 step 3 (G5's span and its per-victim evidence) |
| §8 output | Task 4 (`_CFG`, `_RATE`, `_END`), Task 5 (`_CLK`, `_CAL`, `_SPAN`, `_G5`, `_CASE`, `_XTALK`, `_STAT`, `_GATES`), Task 6 (`_STATIC`), Task 7 (the reader and its guard) |
| §9 how this can go red | Task 2 step 7 (the `MUX_A3` red, the spec's own candidate), Task 3 step 6 (G6, the spec's own candidate), Task 7 step 7 (two reds), Task 1 steps 3 and 9. The R-ladder's own statement is Task 6 step 6 item 3 |
| §10 read / derived / unmeasured | Task 2's header labels the table derived; Task 5 step 2 reads `kScanSettleNs` from `shell/` and names the lines; Task 6 step 6 item 4 is the datasheet claim about `shift_chain_timed()` reaching a measurement |
| §11 out of scope | Nothing built for the codec, the reverse-order sequence, a second board or rework — all correctly deferred |

**Placeholder scan:** none. Every test body, every code block and every
command is the content the executor runs. The two places that say "copy this
file and change these names" (Task 1 step 5's move table, Task 4 step 1's
generator) name the source file, the exact substitution and every deviation
from a straight copy.

**Type consistency:** `XtalkVictim`, `XtalkCase`, `XtalkKind`,
`kXtalkVictimTable`, `kXtalkPlan`, `kXtalkVictims`, `kXtalkCases`,
`xtalk_word()`, `victim_address_mask()`, `victim_enable_mask()`,
`other_enable_mask()`, `led_field_mask()`, `address_bits_of_group()`,
`scan_settle_ns()`, `XtalkSummary`, `XtalkGates`, `xtalk_gates()`,
`probe_adc::` and `step_of()` are spelled the same in every task that touches
them. `Point`, `kRepeats`, `kGridStepNs`, `kGridPoints`, `kParkNs`,
`kSettleCounts`, `kFloorMaxCounts`, `kJitterMaxNs` come from `settle_plan.h`
and are not redefined.

**Three gaps, stated rather than hidden:**

1. **The verdict has no grid point to stand on.** Covered above and in
   Task 7; the envelope fallback is the plan's decision. If Bastian would
   rather the grid were extended to reach 2 ms, that is a different
   instrument — 10 000 grid points at 64 repeats is about 20 minutes a case —
   and it should be raised before Task 5 rather than after.
2. **The aggressor mux's own channel is not a free parameter.** The two muxes
   share A0..A2, so when a case enables the aggressor's mux its address is
   whatever the victim's low bits are; against `REF_A` and `R_SP10` that is an
   unfitted pot wiper. The spec does not address it. The plan records it in a
   comment on `kXtalkPlan` and in Task 6's report, and does not change the
   design: the floating node sits on the aggressor's COM, not the victim's
   ADC pin.
3. **`SHELL_XTALK_STAT` is a line the spec does not name.** Spec §5 asks for
   `settled_mean_spread` and `widest_sample_band` as reported quantities but
   §8's line list has nowhere to put them, and `SHELL_XTALK_CFG` and
   `SHELL_XTALK_CASE` are both within a few bytes of the log buffer. It gets
   its own line.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-09-18-coupon-crosstalk-probe.md`. Two execution options:

**1. Subagent-Driven (recommended)** — a fresh subagent per task, review between tasks, fast iteration. Tasks 1, 4, 5 and 6 and the last step of 7 need the board on USB and a human to press BOOT+RESET, so those halt for the operator. **Task 1 is a hard gate:** do not dispatch Task 2 until its step 9 comparison has been printed and held.

**2. Inline Execution** — tasks run in this session with checkpoints for review.
