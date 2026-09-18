# Coupon Codec-Tone Probe Implementation Plan (round two)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure how far a settled multiplexer channel on the test coupon moves when the submodule's own audio output carries a tone through the analog zone past the muxes, and close the "edge inside the sampling window" sequence round one deferred.

**Architecture:** A third firmware image (`SHELL_TONE_PROBE=1`) that calls `StartAudio` — which round one's deliberately never does — and generates a sine in the callback from a phase accumulator, recording the DWT count at block entry so the foreground can compute phase. The abscissa is phase, not delay; the statistic is the peak-to-peak of `delta` across the phase grid, because a sinusoidal disturbance has zero mean over a period. It drives ADC1 through round one's `probe_adc` and reuses round one's victims, word builders and gates unchanged.

**Tech Stack:** ARM GCC via `make` (libDaisy, STM32H7 HAL, the SAI/codec path), C++17, doctest on the host through CMake/ctest, Python 3 + pyserial 3.5 for the reader. One bench instrument that is not a probe: a multimeter, Task 1.

**Spec:** [`docs/superpowers/specs/2026-09-18-coupon-codec-tone-probe-design.md`](../specs/2026-09-18-coupon-codec-tone-probe-design.md) — read it before Task 1.

---

## This plan depends on round one, completely

[`docs/superpowers/plans/2026-09-18-coupon-crosstalk-probe.md`](2026-09-18-coupon-crosstalk-probe.md)
must be **finished and its Task 1 comparison must have held** before the first
step here. Nothing in that plan is repeated here; this one consumes it.

| What round one produced | Which of its tasks | How this plan uses it |
|---|---|---|
| `shell/probe_adc.h` / `.cpp` — `init()`, `warm_up()`, `select()`, `select_time()`, `sample_now()`, `mean_of_repeats()`, `mean_span_of_repeats()`, `measure_clock()`, `offset_ns_for_rung()`, `channel_of_group()`, `sample_time_for_rung()`, the three HAL flags, `timeouts()` | its Task 1 | every conversion this probe takes. The behaviour-preserving proof it ran on the board is the reason this image's numbers are comparable to round one's at all |
| `shell::step_of(profile, group, ch)` in `mux_plan.h` | its Task 1 | parking the victim |
| `shell::kXtalkVictimTable` — the same five victims, spec §4 | its Task 2 | **used directly. This plan defines no victim table.** |
| `xtalk_word()`, `victim_address_mask()`, `victim_enable_mask()`, `other_enable_mask()`, `led_field_mask()`, `address_bits_of_group()` | its Task 2 | the WIN cases' word pairs |
| `kXtalkPlan` — the 58 aggressor cases | its Task 2 | the WIN cases point into it; §6's sequence fires round-one aggressors inside a conversion window |
| `XtalkSummary`, `XtalkGates`, `xtalk_gates()` — G2, G4, G5, G6 | its Task 3 | **G2, G4 and G5 are reused unchanged.** G6 does not apply: there is no control curve here, because a tone has no latch pulse to control for |
| `MuxScan::write_chain_timed()`, `shift_chain_timed()` | its Tasks 1 and 5 | the WIN sequence's event |
| `SHELL_XTALK_PROBE`'s Makefile switch, `write_shell_xtalk_probe.py`, `write_git_hash.py` | its Task 4 | the pattern Task 2 below copies, and the git stamp reused as-is |
| `shell/read_xtalk.py`, and `xtalk.csv.meta.csv`'s `silent` scope | its Task 7 | **G8's input.** `read_tone.py` takes that file on the command line and refuses to run without it |
| The silent block's measured `settled_mean_spread` per victim | its Task 5 | the floor this image's stopped level must reproduce, within 4 counts |

**Round one's Global Constraints apply here verbatim** — read
[its Global Constraints section](2026-09-18-coupon-crosstalk-probe.md) before
starting. They are not restated. The three that bite hardest in this plan:
never `git add -A` (the working copy carries uncommitted `hardware/coupon/`
work that is not yours); never mix the two toolchains in one task; `cmp` the
image against the one it replaces before every flash.

## Four decisions this plan makes that the spec did not

1. **`SHELL_TONE` and `SHELL_TONE_WIN` each split into two lines.** Same
   reason and same evidence as round one's first decision: libDaisy's log
   buffer is 128 bytes and both of spec §8's lines run past it at their widest
   values (about 130 and 143 characters). The identity fields move to
   `SHELL_TONE_CASE` and `SHELL_TONE_WINCASE`, printed once per case.
2. **The firmware prints `g8=-1` and the reader computes G8.** Spec §7 lists
   G8 among the firmware's gates and §8 prints it on `SHELL_TONE_GATES`, but
   §8 also says G8 needs round one's `xtalk.csv.meta.csv`, which the board
   does not have. Baking round one's five numbers into the firmware as
   constants would turn a measurement into a literal that nobody re-measures —
   exactly what the deleted `kConversionNs` was. So `g8=-1` means "not
   evaluable on the board", the firmware's `gates_ok` excludes it, and
   `read_tone.py` computes it and folds it into the exit code.
3. **G6 does not carry over, and neither does a control level.** Round one's
   control is a latch pulse with no bit change. A tone has no latch, so there
   is nothing to control for; the running-silent level is this probe's
   reference and it does the same job. Spec §7 already omits G6 from its
   table; this makes the omission explicit rather than leaving an executor to
   wonder whether it was forgotten.
4. **The 100 Hz row stays in the table whichever way Task 1's bench reading
   goes**, carrying a `below_corner` flag instead of being removed. Removing
   a row makes two tables to keep in step; a flag makes one table and one
   number, and the reader simply does not read a flagged row's level as part
   of the capacitive slope.

---

## File Structure

| File | Responsibility |
|---|---|
| `shell/write_shell_tone_probe.py` | **New (Task 1).** Switch-header generator, the same shape as `write_shell_xtalk_probe.py`, writing `SHELL_TONE_PROBE` and `SHELL_TONE_DC`. |
| `shell/tone_probe.h` / `.cpp` | **New (Tasks 1, 3, 4, 5).** The instrument, including the audio callback. Board-only. |
| `shell/tone_plan.h` / `.cpp` | **New (Task 2).** The frequency × level table, the phase arithmetic, the WIN case list. Pure; no hardware type. Consumes `xtalk_plan.h` for the victims and the words. |
| `tests/test_tone_plan.cpp` | **New (Task 2).** Host gate. |
| `shell/Makefile` | **Modify (Tasks 1 and 2).** The `SHELL_TONE_PROBE` and `SHELL_TONE_DC` switches, the generator, `tone_plan.cpp` and `tone_probe.cpp`, the new stale-object entries. |
| `shell/main.cpp` | **Modify (Task 1).** Dispatch. |
| `shell/read_tone.py` | **New (Task 6).** Reads one block, writes `tone.csv` and `tone.csv.meta.csv`, computes `delta(φ)`, `delta_pp`, G8 and the verdict. |
| `shell/test_read_tone.py` | **New (Task 6).** Host guard. |
| `CMakeLists.txt` | **Modify (Tasks 2 and 6).** `tone_plan.cpp` + its test into `spky_tests`; `test_read_tone.py` as its own `add_test`. |
| `docs/superpowers/specs/2026-09-18-coupon-codec-tone-probe-design.md` | **Modify (Task 1).** §9's bench reading, recorded as measured. |

Six tasks. Task 2 and the host half of Task 6 need no board. Tasks 1, 3, 4, 5
and the last step of 6 need the coupon on USB and a human to press
BOOT+RESET; Task 1 additionally needs a multimeter and two test points.

**Task 1 decides Task 2's table.** Do not start Task 2 before Task 1's
reading is recorded in the spec.

---

### Task 1: The bench reading, and the image that makes it possible

Spec §9: **whether the Patch Submodule's audio output is DC-coupled at
B1/B2.** Nothing in this repo says, and the answer decides whether a static
level — the codec held at a constant, the simplest possible aggressor — is
available at all, or whether a coupling capacitor's corner sets the lowest
usable frequency.

It cannot be answered by reading anything. It needs a meter on `TP_AUDIO_L`
against `TP_AGND` while the callback writes a constant, so this task builds
the smallest image that writes a constant. That image is also this plan's
switch, its dispatch and its callback skeleton, which is why the bench
question and the scaffolding are one task rather than two.

**Files:**
- Create: `shell/write_shell_tone_probe.py`, `shell/tone_probe.h`, `shell/tone_probe.cpp`
- Modify: `shell/Makefile`, `shell/main.cpp`
- Modify: `docs/superpowers/specs/2026-09-18-coupon-codec-tone-probe-design.md` (§9)

**Interfaces:**
- Consumes: `bench::Board`; `SHELL_GIT_HASH` from round one's `write_git_hash.py`.
- Produces: `void shell::run_tone_probe(bench::Board& hw)` — never returns. The `SHELL_TONE_PROBE` and `SHELL_TONE_DC` make switches. The measured answer to spec §9.

- [ ] **Step 1: Copy the switch generator**

Create `shell/write_shell_tone_probe.py` as a copy of round one's
`shell/write_shell_xtalk_probe.py` — which already writes two defines — with
`XTALK_PROBE` becoming `TONE_PROBE` and `XTALK_RV4` becoming `TONE_DC`.
Keep the English docstring and the stale-object deletion verbatim; do not
shorten the timestamp paragraph.

`SHELL_TONE_DC` is the answer this task is about to measure: `1` when the
audio output is DC-coupled at B1/B2 and the static-level row is therefore
available, `0` when it is not. It defaults to `0` — the conservative
position, because a static row measured through a coupling capacitor would
read as silence and look like a clean result.

- [ ] **Step 2: Add the Makefile switches**

In `shell/Makefile`, after round one's `SHELL_XTALK_RV4` block, in English:

```make
# Position 1 replaces the instrument with the codec-tone probe: a tone of
# chosen frequency and level runs on AUDIO_OUT_L/R through the analog domain
# while a mux channel is read. Unlike every other probe switch here it DOES
# call StartAudio -- that is the point, and it is why it cannot share an
# image with the crosstalk probe, whose floor is measured with the codec
# stopped.
# Spec:  ../docs/superpowers/specs/2026-09-18-coupon-codec-tone-probe-design.md
# Plan:  ../docs/superpowers/plans/2026-09-18-coupon-codec-tone-probe.md
SHELL_TONE_PROBE ?= 0

ifneq ($(filter $(SHELL_TONE_PROBE),0 1),$(SHELL_TONE_PROBE))
$(error SHELL_TONE_PROBE must be 0 or 1)
endif

ifeq ($(SHELL_TONE_PROBE),1)
ifneq ($(SHELL_COUPON_PROBE),1)
$(error SHELL_TONE_PROBE=1 needs SHELL_COUPON_PROBE=1: the probe drives the \
coupon chain profile, and the panel profile has none of its channels)
endif
ifeq ($(SHELL_SETTLE_PROBE),1)
$(error SHELL_TONE_PROBE=1 and SHELL_SETTLE_PROBE=1 are mutually exclusive)
endif
ifeq ($(SHELL_XTALK_PROBE),1)
$(error SHELL_TONE_PROBE=1 and SHELL_XTALK_PROBE=1 are mutually exclusive: \
the crosstalk probe measures its floor with the codec stopped, and an image \
that starts audio cannot produce that floor)
endif
endif

# Whether the submodule's audio output is DC-coupled at B1/B2 -- MEASURED on
# the bench with a meter on TP_AUDIO_L against TP_AGND, spec section 9 and
# the codec-tone plan's Task 1. 1 adds the static-level row (the codec held
# at a constant, the simplest possible aggressor); 0 leaves it out, because a
# static row measured through a coupling capacitor reads as silence and looks
# like a clean result.
SHELL_TONE_DC ?= 0

ifneq ($(filter $(SHELL_TONE_DC),0 1),$(SHELL_TONE_DC))
$(error SHELL_TONE_DC must be 0 or 1)
endif
```

In `CPP_SOURCES`, after `xtalk_probe.cpp`:

```make
	tone_plan.cpp \
	tone_probe.cpp \
```

`tone_plan.cpp` does not exist until Task 2. Add **only** `tone_probe.cpp`
here and add `tone_plan.cpp` in Task 2, or this task does not build.

In `SWITCH_OBJECTS`, add `$(BUILD_DIR)/tone_probe.o`. In `SWITCH_HEADERS`,
add the generator line (mind the trailing backslash on the line above):

```make
  $(shell python write_shell_tone_probe.py $(BUILD_DIR)/shell_tone_probe.h $(SHELL_TONE_PROBE) $(SHELL_TONE_DC) $(SWITCH_OBJECTS))
```

Extend round one's git-stamp line so `tone_probe.o` is deleted when the stamp
changes, and add the explicit header edges:

```make
  $(shell python write_git_hash.py $(BUILD_DIR)/shell_git_hash.h $(BUILD_DIR)/xtalk_probe.o $(BUILD_DIR)/tone_probe.o)
```

```make
$(BUILD_DIR)/tone_probe.o: $(BUILD_DIR)/shell_tone_probe.h $(BUILD_DIR)/shell_git_hash.h $(BUILD_DIR)/shell_coupon_probe.h
```

and add `$(BUILD_DIR)/shell_tone_probe.h` to `main.o`'s dependency line.

- [ ] **Step 3: Write the constant-output image**

Create `shell/tone_probe.h`:

```cpp
#pragma once

// The codec-tone probe. Round one (the crosstalk probe) pits digital edges
// against a settled mux channel; this one pits the AUDIO OUTPUT against it --
// a tone on AUDIO_OUT_L/R, which leave the submodule on B2/B1
// (netlist.py:106) and run through the analog domain to the unpopulated
// J_AUDIO and to TP_AUDIO_L, while a divider channel is read.
//
// It is NOT the 2026-08-23 artifact question. That series is the scan
// coupling INTO audio; this is audio coupling into the scan. The two share a
// board and nothing else.
//
// Spec: ../docs/superpowers/specs/2026-09-18-coupon-codec-tone-probe-design.md
#include "hw/board.h"

namespace shell {

// Never returns. Prints one block per pass on USB-CDC, forever.
void run_tone_probe(bench::Board& hw);

} // namespace shell
```

Create `shell/tone_probe.cpp`. For this task the callback writes a constant
and the foreground prints the board's own audio configuration and nothing
else:

```cpp
#include "tone_probe.h"

#include "cycles.h"
#include "shell_git_hash.h"

namespace shell {

namespace {

// Task 1's constant: a DC level the bench meter can read on TP_AUDIO_L.
// -6 dBFS, the middle of the level ladder Task 2 builds, so the reading is
// taken at a level the tone rows will actually use. Positive, so a meter
// that reads it cannot confuse it with zero.
constexpr float kDcConstant = 0.5011872f;   // 10^(-6/20)

// Written by the callback, read by the foreground. volatile because the two
// are different contexts and nothing else synchronises them.
volatile uint32_t g_blocks = 0;

void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                   daisy::AudioHandle::OutputBuffer out,
                   size_t                           size)
{
    (void)in;
    // NOTHING ELSE in this callback: no engine, no inst.process(). The
    // operating point of this image has to be "the codec, and only the
    // codec", or what the ADC reads is a measurement of the engine's supply
    // draw wearing the codec's name.
    for(size_t i = 0; i < size; ++i)
    {
        out[0][i] = kDcConstant;
        out[1][i] = kDcConstant;
    }
    ++g_blocks;
}

} // namespace

void run_tone_probe(bench::Board& hw)
{
    cycles_init();
    hw.StartLog(false);

    // Read, not assumed. main.cpp's CPU probe carries the comment about the
    // day a block size was inferred as 48 and was in fact 96.
    const int block_size = static_cast<int>(hw.AudioBlockSize());
    const int sr_hz      = static_cast<int>(hw.AudioSampleRate());

    hw.StartAudio(AudioCallback);

    while(1)
    {
        // A block that is a configuration line and an end marker, and that
        // is the whole of this task's firmware: the bench reading is taken
        // with a meter, not with this. blocks= is here so the operator can
        // see the callback is running at all -- a meter reading of zero
        // means two different things otherwise.
        hw.PrintLine("SHELL_TONE_CFG adc_khz=%d repeats=%d phase_points=%d "
                     "block_size=%d sr=%d rv4=%d git=%s",
                     -1, 0, 0, block_size, sr_hz, 0, SHELL_GIT_HASH);
        hw.PrintLine("SHELL_TONE_DCCHECK dbfs=-6 dc=%d blocks=%d",
                     SHELL_TONE_DC, static_cast<int>(g_blocks));
        hw.PrintLine("SHELL_TONE_END");
        hw.Delay(1000);
    }
}

} // namespace shell
```

- [ ] **Step 4: Dispatch from main**

In `shell/main.cpp`, beside the other switch includes:

```cpp
#include "shell_tone_probe.h"
```

and beside the other probe include blocks:

```cpp
#if SHELL_TONE_PROBE
#include "tone_probe.h"
#endif
```

Extend the USB identity guard to include `|| SHELL_TONE_PROBE`.

In `main()`, beside the other probe dispatches. **Placement matters and is
not the same as the others:** this probe calls `StartAudio` itself, so it
must dispatch *after* `inst.init()` like its neighbours but it must not fall
through to the shared `hw.StartAudio(AudioCallback)` at the bottom of
`main()` — it never returns, so it does not.

```cpp
#if SHELL_TONE_PROBE
    // The board under test is the coupon, and the aggressor is the codec.
    // Unlike every other probe image here, this one DOES start audio -- with
    // a callback that writes a tone and nothing else. No engine: the
    // operating point has to be "the codec, and only the codec".
    shell::run_tone_probe(hw);   // never returns
#endif
```

- [ ] **Step 5: Build, cmp, flash**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_TONE_PROBE=0
```

```bash
cp shell/build/shell-sram.bin /tmp/tone0.bin
```

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_TONE_PROBE=1
```

```bash
cmp shell/build/shell-sram.bin /tmp/tone0.bin
```

Expected: differ. Also confirm the three mutual-exclusion errors fire:

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell images SHELL_TONE_PROBE=1 SHELL_XTALK_PROBE=1 SHELL_COUPON_PROBE=1
```

Expected: the codec `$(error ...)`, no build. Then flash:

```bash
dfu-util -a 0 -s 0x90040000:leave -D shell/build/shell-sram.bin
```

```bash
python -c "import serial,sys; ser=serial.Serial(sys.argv[1],timeout=1.0); [sys.stdout.write(ser.readline().decode('utf-8','replace')) for _ in range(30)]" COM4
```

Expected: `SHELL_TONE_CFG …`, `SHELL_TONE_DCCHECK …` with `blocks=` climbing
by roughly 500 per second (96 samples at 48 kHz), and `SHELL_TONE_END`.

**If `blocks=` does not climb, stop.** The callback is not running and the
meter reading below would measure a codec that is not being fed, which is
neither answer.

- [ ] **Step 6: The bench reading**

Ask the operator for a multimeter, DC volts, on **`TP_AUDIO_L` against
`TP_AGND`** (`netlist.py:330-337` places both). With the callback writing a
constant −6 dBFS:

- **A steady DC reading that follows the constant** → the output is
  DC-coupled. Record the voltage.
- **A reading that decays toward zero** → there is a coupling capacitor.
  Record the voltage it starts at and roughly how long it takes to fall to a
  third of it; that time constant is the corner, `f_c = 1 / (2 pi tau)`, and
  it is what the 100 Hz row is checked against.

To separate "decays" from "was never there", have the operator watch the
meter while the board is reset — the constant restarts with the callback.

Report the reading, the instrument, and which of the two it is. **This is a
measurement and it is the first this plan makes**; do not describe it as
expected, confirmed or obvious.

- [ ] **Step 7: Record it in the spec**

Spec §9 says "Either answer goes into this section as measured before the
plan table is finalised." Edit
`docs/superpowers/specs/2026-09-18-coupon-codec-tone-probe-design.md` §9:
replace its last sentence with the reading, the date, the instrument and the
consequence — which of Task 2's two branches it selects, and what
`SHELL_TONE_DC` is therefore built as by default. Also move the DC-coupling
row of §11's read/derived/unmeasured table out of **unmeasured** and into
**measured**, citing §9.

Keep it to what was measured. If the decay was too fast to time with a meter,
say that and say the corner is bounded below rather than known — a bounded
corner still selects the branch, and a made-up number would not.

- [ ] **Step 8: Commit**

```bash
git add shell/write_shell_tone_probe.py shell/tone_probe.h shell/tone_probe.cpp shell/Makefile shell/main.cpp docs/superpowers/specs/2026-09-18-coupon-codec-tone-probe-design.md
git commit -m "feat(shell): the codec-tone probe's switch, and the bench reading it needed

The first image in shell/ that calls StartAudio on purpose and runs nothing
else in the callback. Its switch is mutually exclusive with both other probe
switches, and with the crosstalk probe for a reason worth stating: that
probe measures its floor with the codec stopped, and an image that starts
audio cannot produce that floor.

Its first job is not a measurement this firmware takes. Whether the
submodule's audio output is DC-coupled at B1/B2 is not written down
anywhere in this repo, and it decides whether a static-level case exists at
all -- so the image writes a constant and a meter on TP_AUDIO_L answers it.
The spec's section 9 now carries the reading instead of the question.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: The ladder, the phase arithmetic, and the WIN case list

Pure data, host-tested, no board. Spec §3 (the tone), §5 (the phase grid) and
§6 (the deferred sequence).

**Files:**
- Create: `shell/tone_plan.h`, `shell/tone_plan.cpp`
- Create: `tests/test_tone_plan.cpp`
- Modify: `CMakeLists.txt` (after round one's `tests/test_xtalk_plan.cpp`)
- Modify: `shell/Makefile` (`tone_plan.cpp` into `CPP_SOURCES`)

**Interfaces:**
- Consumes: `shell/xtalk_plan.h` — `kXtalkVictimTable`, `kXtalkVictims`, `kXtalkPlan`, `kXtalkCases`, `XtalkKind`, `victim_address_mask()`, `victim_enable_mask()`; `shell/settle_plan.h` — `kRepeats`, `kSettleCounts`, `kGridStepNs`, `kGridPoints`.
- Produces: `ToneLevel`, `ToneRow`, `kToneRows[kToneRowCount]`, `ToneWinCase`, `kToneWinCases[kToneWinCaseCount]`, `kTonePhasePoints`, `kTonePhaseScale`, `kToneRepeats`, `kToneLevelPoints`, `phase_step_per_sample()`, `phase_target()`, `phase_advance()`, `phase_reached()`, `kToneDbfsFloor`, `kToneSampleRateHz`, `kToneHasStaticRow`, `kToneCornerHz`, `kToneWinRung`, `kToneWinPoints`, `kToneWinStepNs`, `kToneWinWindowNsNominal`, `tone_win_ns()`. Tasks 3, 4, 5 and 6 consume these names.

- [ ] **Step 1: Write the failing test**

Create `tests/test_tone_plan.cpp`:

```cpp
// The codec-tone probe's frequency and level ladder, its phase arithmetic
// and the round-one aggressors it fires inside a conversion window. Derived
// from the codec-tone spec sections 3, 5 and 6.
#include <doctest/doctest.h>
#include "../shell/tone_plan.h"
#include "../shell/xtalk_plan.h"
#include "../shell/settle_plan.h"

TEST_CASE("tone rows: every frequency is below half the sample rate") {
    // Spec section 10's named red. A row above Nyquist does not produce the
    // tone it claims -- it produces an alias at sr - f, at a frequency
    // nothing in the table names, and the capacitive slope read off three
    // such rows would be a slope through a fiction.
    for(int i = 0; i < shell::kToneRowCount; ++i) {
        CAPTURE(i);
        CHECK(shell::kToneRows[i].f_hz > 0);
        CHECK(shell::kToneRows[i].f_hz * 2 < shell::kToneSampleRateHz);
    }
}

TEST_CASE("tone rows: no row asks for more than full scale") {
    for(int i = 0; i < shell::kToneRowCount; ++i) {
        CAPTURE(i);
        CHECK(shell::kToneRows[i].dbfs <= 0);
        CHECK(shell::kToneRows[i].dbfs >= shell::kToneDbfsFloor);
    }
}

TEST_CASE("tone rows: the frequency ladder spans a decade and a half, at three points") {
    // Spec section 3: capacitive coupling grows as C dV/dt, i.e. linearly
    // with f at fixed level; resistive or ground coupling does not. Two
    // points give a line through anything; three give a slope that can be
    // wrong. Fewer than three distinct frequencies would make the whole
    // frequency argument unfalsifiable.
    int distinct = 0;
    for(int i = 0; i < shell::kToneRowCount; ++i) {
        bool seen = false;
        for(int j = 0; j < i; ++j)
            if(shell::kToneRows[j].f_hz == shell::kToneRows[i].f_hz) seen = true;
        if(!seen) ++distinct;
    }
    CHECK(distinct == 3);
}

TEST_CASE("tone rows: the level ladder is a linearity check, at three points") {
    int distinct = 0;
    for(int i = 0; i < shell::kToneRowCount; ++i) {
        bool seen = false;
        for(int j = 0; j < i; ++j)
            if(shell::kToneRows[j].dbfs == shell::kToneRows[i].dbfs) seen = true;
        if(!seen) ++distinct;
    }
    CHECK(distinct == 3);
}

TEST_CASE("tone rows: the table is the full cross product, plus the static row when it exists") {
    // Nine rows of 3 x 3, and one static row iff the output is DC-coupled --
    // which Task 1 measured and SHELL_TONE_DC carries.
    const int expected = 9 + (shell::kToneHasStaticRow ? 1 : 0);
    CHECK(shell::kToneRowCount == expected);
}

TEST_CASE("tone rows: a row below the coupling corner is flagged, not deleted") {
    // Spec section 9's other branch. A row below an AC-coupled output's
    // corner still runs -- its result is a real measurement of what that
    // output does at that frequency -- but the reader must not read its
    // level as part of the capacitive slope, because the attenuation is the
    // coupling network's and not the board's.
    for(int i = 0; i < shell::kToneRowCount; ++i) {
        CAPTURE(i);
        const int f = shell::kToneRows[i].f_hz;
        // f > 0 in the condition, and it is not redundant: the static row is
        // f_hz = 0, and a bare `f < corner` would flag a constant as being
        // below the corner of a network a constant does not pass through at
        // all. A DC-coupled output is the only reason that row exists.
        CHECK(shell::kToneRows[i].below_corner
              == (shell::kToneCornerHz > 0 && f > 0 && f < shell::kToneCornerHz));
    }
}

TEST_CASE("tone phase: sixteen points span exactly one period") {
    CHECK(shell::kTonePhasePoints == 16);
    CHECK(shell::phase_target(0) == 0u);
    // The last point must be one step short of a full turn, not at it: a
    // point at a full turn is the same instant as point 0 and the grid would
    // measure fifteen phases while reporting sixteen.
    CHECK(shell::phase_target(shell::kTonePhasePoints)
          == shell::kTonePhaseScale);
    for(int i = 1; i < shell::kTonePhasePoints; ++i) {
        CAPTURE(i);
        CHECK(shell::phase_target(i) > shell::phase_target(i - 1));
        CHECK(shell::phase_target(i) - shell::phase_target(i - 1)
              == shell::kTonePhaseScale / shell::kTonePhasePoints);
    }
}

TEST_CASE("tone phase: one second of steps comes back to where it started") {
    // The accumulator is what the callback advances per sample. If it does
    // not come back, the phase the foreground computes drifts against the
    // tone the codec is emitting and every delta(phi) is smeared across the
    // grid.
    //
    // ONE SECOND and not one period, and the difference matters: 5 kHz does
    // not divide 48 kHz into a whole number of samples (9.6 of them), so
    // "one period of samples" is not a thing the integer accumulator can be
    // asked about. One second is exactly f_hz periods for any integer f, and
    // exactly sr samples.
    for(int i = 0; i < shell::kToneRowCount; ++i) {
        const shell::ToneRow& r = shell::kToneRows[i];
        if(r.f_hz == 0) continue;                 // the static row has no phase
        CAPTURE(i);
        const uint32_t step = shell::phase_step_per_sample(r.f_hz,
                                                           shell::kToneSampleRateHz);
        const uint32_t after
            = shell::phase_advance(0u, step, shell::kToneSampleRateHz);
        // Not exactly zero: phase_step_per_sample truncates, by less than one
        // phase unit per sample, so a second accumulates less than sr units
        // of error. That bound is derived from the truncation itself and not
        // chosen. Distance to the nearest turn, because "just short of a full
        // turn" and "just past zero" are the same place.
        const uint32_t err = (after < shell::kTonePhaseScale - after)
                                 ? after
                                 : (shell::kTonePhaseScale - after);
        CHECK(err < static_cast<uint32_t>(shell::kToneSampleRateHz));
        // And that error must stay far inside one grid point, or a phase
        // point measured at the start of a case is a different phase by the
        // end of it.
        CHECK(err < shell::kTonePhaseScale / shell::kTonePhasePoints / 4u);
    }
}

TEST_CASE("tone phase: the crossing detector fires once per period, not once per sample") {
    // The failure this catches is a detector written as `now >= target`: it
    // fires on EVERY sample after the first crossing, so the spin returns
    // immediately with whatever phase it happens to be at, and the phase
    // grid measures one phase sixteen times. Over ten periods that detector
    // fires hundreds of times and a correct one fires ten.
    //
    // Nine to eleven rather than exactly ten: phase_step_per_sample
    // truncates, so a period is a fraction of a sample longer than
    // sr / f_hz and ten of them can straddle a boundary either way.
    const uint32_t step = shell::phase_step_per_sample(1000,
                                                       shell::kToneSampleRateHz);
    const int per_period = shell::kToneSampleRateHz / 1000;
    for(int idx = 0; idx < shell::kTonePhasePoints; ++idx) {
        const uint32_t target = shell::phase_target(idx);
        CAPTURE(idx);
        int      hits = 0;
        uint32_t now  = 0u;
        for(int s = 0; s < 10 * per_period; ++s) {
            const uint32_t prev = now;
            now = shell::phase_advance(now, step, 1);
            if(shell::phase_reached(prev, now, target)) ++hits;
        }
        CHECK(hits >= 9);
        CHECK(hits <= 11);
    }
}

TEST_CASE("tone phase: the crossing detector never makes the spin wait two periods") {
    // Spec section 10: "the crossing detector never waits more than two
    // periods". Starting from every phase in the grid, the next crossing of
    // every target must arrive inside one turn of the accumulator.
    //
    // The bound is per_period + 2 and not + 1: a turn takes
    // ceil(kTonePhaseScale / step) samples, and because step is truncated
    // downward that is up to one sample more than sr / f_hz. At 1 kHz it is
    // 49 against a per_period of 48.
    //
    // This is also the test that catches the OTHER wrap failure -- a
    // detector that misses the crossing which happens across the wrap never
    // fires for a target just above it, and this loop then runs out rather
    // than breaking.
    const uint32_t step = shell::phase_step_per_sample(1000,
                                                       shell::kToneSampleRateHz);
    const int per_period = shell::kToneSampleRateHz / 1000;
    for(int from = 0; from < shell::kTonePhasePoints; ++from) {
        for(int idx = 0; idx < shell::kTonePhasePoints; ++idx) {
            CAPTURE(from);
            CAPTURE(idx);
            const uint32_t target = shell::phase_target(idx);
            uint32_t now    = shell::phase_target(from);
            bool     fired  = false;
            int      waited = 0;
            for(; waited <= per_period + 2; ++waited) {
                const uint32_t prev = now;
                now = shell::phase_advance(now, step, 1);
                if(shell::phase_reached(prev, now, target)) { fired = true; break; }
            }
            CHECK(fired);
            CHECK(waited <= per_period + 2);
        }
    }
}

TEST_CASE("tone WIN cases: every one points at a real round-one aggressor") {
    // The index is into kXtalkPlan, so a row added there would silently
    // repoint it. The table carries what it expects to find and this
    // assertion is what makes a shifted index a failure rather than a
    // measurement of a different aggressor under the right label.
    for(int i = 0; i < shell::kToneWinCaseCount; ++i) {
        const shell::ToneWinCase& w = shell::kToneWinCases[i];
        CAPTURE(i);
        REQUIRE(w.xtalk_case >= 0);
        REQUIRE(w.xtalk_case < shell::kXtalkCases);
        const shell::XtalkCase& c = shell::kXtalkPlan[w.xtalk_case];
        CHECK(c.row     == w.want_row);
        CHECK(c.group   == w.want_group);
        CHECK(c.channel == w.want_channel);
        // It must be an EVENT: a Silent or Static case has nothing to fire
        // inside a sampling window.
        CHECK(c.kind == shell::XtalkKind::Latch);
        CHECK(c.word_a != c.word_b);
        // And it must still hold the victim still -- round one's assertion,
        // reused, because this sequence changes when the edge lands and
        // nothing else.
        const uint32_t hold = shell::victim_address_mask(c.group)
                              | shell::victim_enable_mask(c.group);
        CHECK((c.word_a & hold) == (c.word_b & hold));
    }
}

TEST_CASE("tone WIN cases: MUX8_EN_N against REF_A is there in both directions") {
    // Spec section 6: it runs "for MUX8_EN_N against REF_A regardless",
    // because that is the case the moat crossing is most directly in. Both
    // directions, because charge injection has a sign.
    int found = 0;
    for(int i = 0; i < shell::kToneWinCaseCount; ++i) {
        const shell::ToneWinCase& w = shell::kToneWinCases[i];
        if(w.want_row == 3 && w.want_group == 0 && w.want_channel == 8) ++found;
    }
    CHECK(found == 2);
}

TEST_CASE("tone WIN cases: the window grid meets round one's grid end to end") {
    // Spec section 6: d_before_end runs 0..12800 ns in 200 ns steps, the
    // same 65 points, so round one's d AFTER the edge continues where this
    // one's d_before_end stops. A different step or count would make the two
    // curves uncomparable, which is the only reason to run this sequence on
    // the same aggressors at all.
    CHECK(shell::kToneWinPoints == shell::kGridPoints);
    CHECK(shell::kToneWinStepNs == shell::kGridStepNs);
    CHECK(shell::tone_win_ns(0) == 0u);
    CHECK(shell::tone_win_ns(shell::kToneWinPoints - 1) == 12800u);
}

TEST_CASE("tone WIN cases: the window is long enough to hold the whole grid") {
    // The 387.5-cycle rung gives about 63 us of acquisition at the measured
    // 6.146 MHz (settle-measured.md section 7). The grid reaches 12.8 us
    // back from the end of it, so the edge always lands inside the window --
    // an edge fired before the window opened would be round one's
    // measurement again, at a delay nobody commanded.
    CHECK(shell::kToneWinRung == 6);                    // 387.5 cycles
    CHECK(shell::kSamplingLadderTenths[shell::kToneWinRung] == 3875);
    // Derived, not measured, and the firmware checks the real number against
    // it at runtime: 387.5 / 6.146 MHz = 63.05 us.
    CHECK(shell::kToneWinWindowNsNominal > 12800u * 4u);
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
source env.sh; cmake --build build
```

**Expected RED:** a compile failure — `shell/tone_plan.h` does not exist. Red
because nothing in this task's interface is written yet.

- [ ] **Step 3: Write the header**

Create `shell/tone_plan.h`:

```cpp
#pragma once

// The codec-tone probe's ladder, its phase arithmetic and the round-one
// aggressors it fires inside a conversion window. Data and pure arithmetic
// with no hardware type in it, for the same reason as xtalk_plan.h.
//
// It defines NO victim table: the victims are round one's five, used
// directly from xtalk_plan.h, and a second copy of them would be two tables
// to keep in step for one board.
//
// Spec: ../docs/superpowers/specs/2026-09-18-coupon-codec-tone-probe-design.md
#include <cstdint>

#include "settle_plan.h"
#include "shell_tone_probe.h"   // SHELL_TONE_DC, from Task 1's bench reading
#include "xtalk_plan.h"

namespace shell {

// Which of the three levels a printed row was taken at. Printed as level=%d,
// so the numbering is part of the output format and may not be reordered.
enum class ToneLevel : uint8_t
{
    Stopped       = 0,   // StopAudio() before the block; the codec is idle
    RunningSilent = 1,   // audio started, callback writes zeros
    Tone          = 2,   // one row of the frequency x level table
};

// The sample rate the ladder is checked against. Read from src/hw/board.h's
// SetAudioSampleRate(SAI_48KHZ); the firmware asks the board at runtime and
// prints the answer, and a board that disagrees with this constant is a
// finding, not a silent retune.
inline constexpr int kToneSampleRateHz = 48000;

// The quietest row. -20 dBFS is a tenth of full scale in amplitude, which is
// two decades above the ADC's own floor and still far enough below 0 dBFS
// for a linearity check to mean something.
inline constexpr int kToneDbfsFloor = -20;

// Task 1's bench reading, carried through the switch header. 1 = the audio
// output is DC-coupled at B1/B2 and the static row is available.
inline constexpr bool kToneHasStaticRow = (SHELL_TONE_DC != 0);

// The coupling network's corner, in Hz. 0 means there is none -- the output
// is DC-coupled and no row is below any corner.
//
// TASK 1 SETS THIS from its bench reading, and it is the one number in this
// header that does not follow from anything else here. It ships at 0
// because that is both the correct value for a DC-coupled output and the
// safe value for one nobody has measured yet: at 0, below_corner is false
// everywhere and the reader reads every row's level as part of the slope,
// which is only wrong if a corner exists and was not entered. Task 1 is a
// hard prerequisite of this task precisely so that cannot happen. If the
// decay was only bounded rather than timed, this carries the bound and the
// spec's section 9 says it is a bound.
inline constexpr int kToneCornerHz = 0;

struct ToneRow
{
    int  f_hz;           // 0 on the static row: a constant has no frequency
    int  dbfs;           // negative, or 0 for full scale
    bool below_corner;   // the coupling network attenuates this row; its
                         // level may not be read as part of the slope
};

inline constexpr int kToneRowCount = 9 + (kToneHasStaticRow ? 1 : 0);
extern const ToneRow kToneRows[kToneRowCount];

// --- The phase grid (spec section 5) ---
//
// Phase is a fixed-point fraction of a period, 0 .. kTonePhaseScale. A power
// of two so the wrap is free and the arithmetic is integer: this runs beside
// a timed conversion and a float divide there would be measuring the
// measurement, the same argument cycles.h makes for ns_to_cycles().
inline constexpr uint32_t kTonePhaseScale  = 1u << 24;
inline constexpr int      kTonePhasePoints = 16;

// Sixteen points per period, 64 repeats each. Sixteen is enough because the
// quantity is a PEAK-TO-PEAK and a sine's extremes are broad -- a finer grid
// would cost time to resolve a maximum that is already flat where it matters.
inline constexpr int kToneRepeats = kRepeats;

// How many sub-measurements the Stopped and RunningSilent levels take, each
// of kToneRepeats conversions. It is round one's grid point count, and that
// is G8's whole premise: round one's settled_mean_spread is the peak-to-peak
// of 65 means of 64 conversions, so this level's has to be the peak-to-peak
// of 65 means of 64 conversions or the 4-count bound compares two
// differently-shaped spreads and means nothing.
inline constexpr int kToneLevelPoints = kGridPoints;

// How far the accumulator moves per output sample.
constexpr uint32_t phase_step_per_sample(int f_hz, int sr_hz)
{
    if(f_hz <= 0 || sr_hz <= 0) return 0u;
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(f_hz) * kTonePhaseScale)
        / static_cast<uint64_t>(sr_hz));
}

// Grid point `idx`'s phase. Defined for idx == kTonePhasePoints too, which
// is one full turn -- the same instant as point 0, and the host test asserts
// that the last GRID point is one step short of it rather than at it.
constexpr uint32_t phase_target(int idx)
{
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(idx) * kTonePhaseScale)
        / static_cast<uint64_t>(kTonePhasePoints));
}

// `n` samples of advance, wrapping. uint32 arithmetic wraps at 2^32, not at
// kTonePhaseScale, so the mask is explicit.
constexpr uint32_t phase_advance(uint32_t phase, uint32_t step, int n)
{
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(phase) + static_cast<uint64_t>(step) * n)
        & (kTonePhaseScale - 1u));
}

// Whether the advance from `prev` to `now` crossed `target`.
//
// THE WRAP IS THE WHOLE POINT. A magnitude comparison -- now >= target --
// fires on every sample after the first crossing and never fires at all for
// a target the accumulator steps over. The crossing test has to handle the
// step that wraps from near the top of the range to near the bottom, or a
// target just above the wrap is never reached and the foreground spins
// forever.
constexpr bool phase_reached(uint32_t prev, uint32_t now, uint32_t target)
{
    if(now >= prev)                       // no wrap in this step
        return prev < target && target <= now;
    return target > prev || target <= now;   // wrapped
}

// --- The edge inside the sampling window (spec section 6) ---
//
// The rung: 387.5 ADC cycles, index 6 of settle_plan.h's
// kSamplingLadderTenths, which settle-measured.md section 7 measured landing
// on the divider's true value. The sample-and-hold tracks the node through
// the whole window and the aperture closes at its end, so an edge fired at a
// chosen time BEFORE that end shows its remainder at the aperture.
inline constexpr int kToneWinRung = 6;

// 387.5 / 6.146 MHz = 63.05 us. DERIVED from the measured clock, for the
// host assertion that the grid fits inside the window; the firmware computes
// the real window from THIS boot's probe_adc::Clock and prints it, and a
// disagreement is a finding rather than a silent retune.
inline constexpr uint32_t kToneWinWindowNsNominal = 63050;

// The same 65 points at 200 ns as round one's grid, counted backwards from
// the end of the window, so the two instruments' curves line up end to end:
// round one's d after the edge continues where this one's d_before_end
// stops.
inline constexpr int kToneWinPoints = kGridPoints;
inline constexpr int kToneWinStepNs = kGridStepNs;

constexpr uint32_t tone_win_ns(int i)
{
    return static_cast<uint32_t>(i) * static_cast<uint32_t>(kToneWinStepNs);
}

// A round-one aggressor, fired inside a conversion window.
//
// The index is into kXtalkPlan and the three want_* fields are what that
// index must be. A row added to kXtalkPlan would silently repoint the index;
// the host assertion on want_row/want_group/want_channel is what turns that
// into a failure instead of a measurement of a different aggressor under the
// right label.
struct ToneWinCase
{
    int     xtalk_case;
    uint8_t want_row;
    int     want_group;
    int     want_channel;
};

inline constexpr int kToneWinCaseCount = 2;
extern const ToneWinCase kToneWinCases[kToneWinCaseCount];

} // namespace shell
```

- [ ] **Step 4: Write the tables**

Create `shell/tone_plan.cpp`:

```cpp
#include "tone_plan.h"

namespace shell {

namespace {

// Spec section 3's two ladders. Frequency: capacitive coupling grows as
// C dV/dt -- linearly with f at fixed level -- and resistive or ground
// coupling does not, so three points give a slope that can come out wrong.
// 5 kHz is the top because it keeps the phase grid coarse enough for the
// callback's own timing (section 5) and nothing above it is needed to read
// the slope. Level: a linearity check, because coupling that is not linear
// in level is not coupling.
constexpr int kFreqHz[3]  = {100, 1000, 5000};
constexpr int kLevelDb[3] = {-20, -6, 0};

constexpr bool below_corner(int f_hz)
{
    return kToneCornerHz > 0 && f_hz > 0 && f_hz < kToneCornerHz;
}

} // namespace

// The full 3 x 3 cross product, frequency-major so a reader of the raw log
// sees the level ladder run inside each frequency -- which is the order the
// linearity check wants, and the order that makes a drifting board show as a
// tilt within a frequency rather than across the table.
//
// Shape: sine only. A square wave would be a stronger aggressor, but its
// harmonics fold the frequency ladder into one point; it is an optional
// fourth row once the sine slope is known, and it is out of scope here.
const ToneRow kToneRows[kToneRowCount] = {
    {kFreqHz[0], kLevelDb[0], below_corner(kFreqHz[0])},
    {kFreqHz[0], kLevelDb[1], below_corner(kFreqHz[0])},
    {kFreqHz[0], kLevelDb[2], below_corner(kFreqHz[0])},
    {kFreqHz[1], kLevelDb[0], below_corner(kFreqHz[1])},
    {kFreqHz[1], kLevelDb[1], below_corner(kFreqHz[1])},
    {kFreqHz[1], kLevelDb[2], below_corner(kFreqHz[1])},
    {kFreqHz[2], kLevelDb[0], below_corner(kFreqHz[2])},
    {kFreqHz[2], kLevelDb[1], below_corner(kFreqHz[2])},
    {kFreqHz[2], kLevelDb[2], below_corner(kFreqHz[2])},
#if SHELL_TONE_DC
    // The static row, available only because Task 1's meter said the output
    // is DC-coupled. f_hz = 0: a constant has no frequency and no phase, so
    // it is measured as a whole-block mean like the running-silent level and
    // never gets a phase grid.
    {0, -6, false},
#endif
};

// Spec section 6: the deferred sequence runs "for the round-one aggressors
// that showed a delta at all, and for MUX8_EN_N against REF_A regardless,
// because that is the case the moat crossing is most directly in".
//
// WHICH ROUND-ONE AGGRESSORS SHOWED A DELTA IS A RESULT AND NOT A PLAN. Only
// the unconditional pair is here. When round one's reader has printed its
// verdicts, add the cases whose |delta| was non-trivial -- as further entries
// with their own want_row/want_group/want_channel -- and say in the commit
// which capture named them.
//
// The two indices are REF_A's MUX8_EN_N pair in kXtalkPlan, both directions.
// They are the 11th and 12th entries of that table (row 3's first pair); the
// host test asserts what they must be rather than trusting the arithmetic.
const ToneWinCase kToneWinCases[kToneWinCaseCount] = {
    {10, 3, 0, 8},   // MUX8_EN_N low -> high, victim REF_A
    {11, 3, 0, 8},   // MUX8_EN_N high -> low, victim REF_A
};

} // namespace shell
```

**Verify the two indices against `kXtalkPlan` before running the test** —
count the entries above row 3's first pair in round one's
`shell/xtalk_plan.cpp` (five row-1 entries, five row-2 entries, then row 3's
REF_A pair) and correct them if the table has moved. The host assertion will
catch a wrong index, but it is cheaper to read the table than to read a
failure.

- [ ] **Step 5: Wire it into both builds**

In `CMakeLists.txt`, in `spky_tests`, after round one's
`tests/test_xtalk_plan.cpp`:

```cmake
    shell/tone_plan.cpp
    tests/test_tone_plan.cpp
```

`tone_plan.h` includes `shell_tone_probe.h`, which the **firmware** build
generates into `shell/build/`. The host build has no such file, so the host
needs its own. Add to `CMakeLists.txt`, beside the `spky_tests` target:

```cmake
# The switch headers are generated by shell/Makefile into shell/build/, which
# the host build does not have and must not depend on. tone_plan.h reads
# SHELL_TONE_DC -- Task 1's measured bench answer -- so the host gets its own
# one-line header with the same default the Makefile carries. A host test of
# the OTHER branch flips this line; the firmware's value always comes from
# the Makefile switch.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/host_switches/shell_tone_probe.h
     "#define SHELL_TONE_PROBE 0\n#define SHELL_TONE_DC 0\n")
target_include_directories(spky_tests PRIVATE
     ${CMAKE_CURRENT_BINARY_DIR}/host_switches)
```

In `shell/Makefile`, add `tone_plan.cpp` to `CPP_SOURCES` beside
`tone_probe.cpp` (Task 1 deliberately left it out).

- [ ] **Step 6: Run the tests to verify they pass**

```bash
source env.sh; cmake -S . -B build -DCMAKE_BUILD_TYPE=Release; cmake --build build; ctest --test-dir build --output-on-failure
```

Expected: every test passes, round one's included.

If `kToneHasStaticRow` is true (Task 1 measured a DC-coupled output), the
"full cross product" case expects 10 rows; if false, 9. Confirm the host
header written in step 5 carries the **same** value the Makefile defaults to,
or the host suite and the firmware disagree about the table's size while both
pass their own checks.

- [ ] **Step 7: Prove the RED once**

Spec §10 names this one: the Nyquist assertion.

In `shell/tone_plan.cpp`, change

```cpp
constexpr int kFreqHz[3]  = {100, 1000, 5000};
```

to

```cpp
constexpr int kFreqHz[3]  = {100, 1000, 30000};
```

Rebuild and run:

```bash
source env.sh; cmake --build build; ./build/spky_tests.exe -tc="tone rows: every frequency is below half*"
```

**Expected: FAILURE** on `CHECK(shell::kToneRows[i].f_hz * 2 <
shell::kToneSampleRateHz)` for the three 30 kHz rows. It is red because
30 kHz at a 48 kHz sample rate is above Nyquist: the codec does not emit
30 kHz, it emits an 18 kHz alias, at a frequency nothing in the table names —
and a capacitive slope read off 100 Hz, 1 kHz and "30 kHz" would be a slope
through a fiction that looks like three honest points.

**Confirm that this is the only case that fails**, and say so in the report.
It is worth checking rather than assuming: the phase assertions do *not*
catch a 30 kHz row — 30 000 · 2²⁴ / 48 000 is exact, so the accumulator comes
back to zero perfectly and "one second of steps comes back to where it
started" passes. The Nyquist bound is the only thing standing between this
table and an aliased aggressor, which is why the spec named it.

**Retype** the original line; do not `git checkout`. Rebuild, run the full
suite, confirm green, report what you saw.

- [ ] **Step 8: Commit**

```bash
git add shell/tone_plan.h shell/tone_plan.cpp tests/test_tone_plan.cpp CMakeLists.txt shell/Makefile
git commit -m "feat(shell): the codec-tone probe's ladder, phase arithmetic and window cases

Three frequencies and three levels, because capacitive coupling grows
linearly with f at fixed level and ground coupling does not -- two points
give a line through anything, three give a slope that can come out wrong.
No victim table: the victims are round one's five, used directly, because a
second copy of them would be two tables to keep in step for one board.

Phase is a fixed-point fraction of a period with a power-of-two scale, so
the wrap is free and the arithmetic is integer -- this runs beside a timed
conversion and a float divide there would be measuring the measurement. The
crossing detector handles the wrap, which is not a detail: a magnitude
comparison never fires for a target the accumulator steps over, and the
foreground would spin forever.

The window cases carry both an index into kXtalkPlan and what that index
must be, so a row added to round one's table fails a host assertion instead
of quietly measuring a different aggressor under the right label. Only the
unconditional pair is here; which other round-one aggressors earn the
sequence is a result, not a plan.

The Nyquist assertion was red-proven by hand: a 30 kHz row at 48 kHz does
not emit 30 kHz, it emits an 18 kHz alias at a frequency nothing in the
table names, and the slope read off it would look like three honest points.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The tone, the phase clock, and the two silent levels

The first task here that measures anything, and it delivers a result on its
own: the **stopped** and **running-silent** levels. Their difference is the
I2S/SAI-DMA finding — `settle-measured.md` §5 names DMA activity as an
unexamined candidate for the 8–12 count wander, and round one's silent block
measured it with the codec stopped. This is the first time anything measures
it with the codec running.

**Files:**
- Modify: `shell/tone_probe.cpp`

**Interfaces:**
- Consumes: `probe_adc::` and `step_of()` (round one's Task 1), `kXtalkVictimTable` (round one's Task 2), `xtalk_gates()` (round one's Task 3), `tone_plan.h` (Task 2).
- Produces: the audio callback with its phase accumulator and block clock, `SHELL_TONE_CLK`, `_CAL`, `_SPAN`, `_G5`, `_LEVEL` and `_GATES`, and a `missed_blocks` count.

- [ ] **Step 1: The callback**

Replace Task 1's constant with the real generator. Nothing else goes in it:

```cpp
// What the callback publishes to the foreground. volatile, and read in the
// order written: dwt first, then phase, then blocks. The foreground only
// ever needs a CONSISTENT pair (dwt_at_block_start, phase_at_block_start),
// and it gets one by reading blocks, then the pair, then blocks again, and
// retrying if the count moved -- a seqlock, and the cheapest correct thing
// on a single-writer single-reader pair this small.
volatile uint32_t g_blocks             = 0;
volatile uint32_t g_dwt_at_block_start = 0;
volatile uint32_t g_phase_at_block     = 0;

// The row the callback is currently emitting. Written by the foreground
// between cases, never during one.
volatile uint32_t g_phase_step = 0;
volatile float    g_amplitude  = 0.0f;

void AudioCallback(daisy::AudioHandle::InputBuffer  in,
                   daisy::AudioHandle::OutputBuffer out,
                   size_t                           size)
{
    (void)in;
    const uint32_t t0 = cycles_now();

    uint32_t    phase = g_phase_at_block;
    const uint32_t st = g_phase_step;
    const float    a  = g_amplitude;

    for(size_t i = 0; i < size; ++i)
    {
        // sinf() and not a table: this callback has 96 samples of a 2 ms
        // block to fill and nothing else to do, so the cost is irrelevant --
        // and a table would put its own interpolation error into the
        // aggressor, which is the one signal in this measurement that has to
        // be what it says it is.
        const float s = sinf(6.2831853f * (static_cast<float>(phase)
                                           / static_cast<float>(kTonePhaseScale)));
        const float v = a * s;
        out[0][i] = v;
        out[1][i] = v;
        phase = phase_advance(phase, st, 1);
    }

    // Published AFTER the buffer is filled but describing the block's START:
    // t0 was taken at entry, and phase_at_block is the phase the block began
    // with, which is what the foreground's phase(t) formula needs.
    g_dwt_at_block_start = t0;
    g_phase_at_block     = phase;   // the NEXT block's starting phase
    ++g_blocks;
}
```

`tone_probe.cpp` needs `#include <cmath>` for `sinf()` here and `powf()` in
Task 4.

**Note the subtlety and write it into the comment:** `g_phase_at_block` is
published as the *next* block's starting phase, and `g_dwt_at_block_start` as
*this* block's entry time. The foreground's formula therefore reads the pair
after a block boundary and uses `g_phase_at_block` with the *next*
`g_dwt_at_block_start`. Get this wrong by one block and every phase is offset
by a constant 2 ms — which at 100 Hz is 200 whole periods and looks like
noise, and at 5 kHz is 10 periods and looks like nothing at all. The seqlock
read is what makes the pair consistent; the off-by-one-block is what makes it
*correct*, and it is not the same bug.

- [ ] **Step 2: The foreground's phase clock**

```cpp
// The phase at DWT count `t`, from the last block boundary the callback
// published. Spec section 5:
//   phi(t) = phase_at_block_start + f * (t - dwt_at_block_start) / f_core
// expressed in the fixed-point phase units, with the multiply done in 64 bit
// because a 2 ms block at 480 MHz is ~960 000 core cycles and the step is up
// to 2^24 / 9.6.
uint32_t phase_now(uint32_t step_per_sample, int sr_hz)
{
    uint32_t blocks0, dwt, phase, blocks1;
    do
    {
        blocks0 = g_blocks;
        dwt     = g_dwt_at_block_start;
        phase   = g_phase_at_block;
        blocks1 = g_blocks;
    } while(blocks0 != blocks1);

    const uint32_t elapsed = cycles_now() - dwt;   // unsigned, wraps correctly
    // core cycles -> output samples -> phase.
    const uint64_t samples
        = (static_cast<uint64_t>(elapsed) * static_cast<uint64_t>(sr_hz))
          / (static_cast<uint64_t>(kCoreMhz) * 1000000ull);
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(phase)
         + static_cast<uint64_t>(step_per_sample) * samples)
        & (kTonePhaseScale - 1u));
}
```

The spin that waits for a target phase runs **with interrupts enabled**, so
the callback keeps the codec fed; only `probe_adc::sample_now()` masks them,
and only across its own conversion (~2 µs at the working rung, far inside a
2 ms block). Say both in a comment at the spin, because the two are easy to
conflate and getting them the wrong way round starves the callback.

- [ ] **Step 3: G7, the callback health gate**

```cpp
// G7: zero missed blocks across the case.
//
// A starved callback outputs the DMA buffer's stale contents, so the tone is
// not the tone -- and a delta_pp measured against that is unreadable rather
// than merely wrong. The count is the callback's own entries against what
// the elapsed DWT time and the block size say it should have been.
//
// Tolerance of one block, and only one: the two clocks are read at slightly
// different instants and a boundary can fall between them. Two is a real
// miss.
const uint32_t expected_blocks
    = static_cast<uint32_t>((static_cast<uint64_t>(cycles_now() - case_t0)
                             * static_cast<uint64_t>(sr_hz))
                            / (static_cast<uint64_t>(kCoreMhz) * 1000000ull
                               * static_cast<uint64_t>(block_size)));
const uint32_t seen = g_blocks - blocks_at_case_start;
const int32_t  missed = static_cast<int32_t>(expected_blocks)
                        - static_cast<int32_t>(seen);
if(missed > 1) missed_blocks += missed;
```

- [ ] **Step 4: The two silent levels, and G2/G4/G5**

The calibration passes are round one's, called through `probe_adc` and
printed on this probe's line names:

```cpp
        hw.PrintLine("SHELL_TONE_CLK span_short_cyc=%d span_long_cyc=%d "
                     "smp_short_tenths=%d smp_long_tenths=%d",
                     clk.span_short_cyc, clk.span_long_cyc, 165, 3875);
        hw.PrintLine("SHELL_TONE_CAL lat_mean_ns=%d lat_min_ns=%d "
                     "lat_max_ns=%d b0=%d timeouts=%d",
                     lat_mean, lat_min, lat_max, b0,
                     static_cast<int>(probe_adc::timeouts()));
```

G5's span and per-victim verdict are round one's `measure_span()` and
`coupon_verdict()` pass, unchanged in substance, printed as
`SHELL_TONE_SPAN` and `SHELL_TONE_G5`. **Copy it from `xtalk_probe.cpp`
rather than reinventing it**, and say in a comment that the two are the same
pass so a later change to one is visible as a divergence from the other.

Then the two levels, per victim:

```cpp
        // Stopped: the codec idle. This is round one's silent block repeated
        // in this image, and it is what G8 checks -- an image whose floor is
        // not round one's floor has had something moved by the refactor or
        // the linker, and every tone result in it would be plausible and
        // against the wrong baseline.
        hw.StopAudio();
        measure_level(ToneLevel::Stopped, v);

        // Running, silent: audio started, the callback writing zeros. I2S,
        // the SAI DMA and the block interrupt as aggressor, with no signal
        // on the trace. The difference from Stopped is the DMA finding.
        g_amplitude  = 0.0f;
        g_phase_step = 0u;
        hw.StartAudio(AudioCallback);
        measure_level(ToneLevel::RunningSilent, v);
```

`measure_level()` is:

```cpp
// A whole-block measurement: no phase, because there is no phase in silence.
//
// ITS SHAPE IS ROUND ONE'S SILENT CURVE AND NOT AN ARBITRARY RUN, and that
// is G8's whole premise. Round one's settled_mean_spread is the peak-to-peak
// of 65 means of 64 conversions each; this takes exactly the same -- 65
// sub-measurements of kToneRepeats conversions -- so the two are the same
// statistic and the 4-count bound compares like with like. A shorter run
// here would make G8 a comparison between two differently-shaped spreads,
// which is a bound with no meaning behind it.
//
// It costs 65 x 64 conversions at about 2.5 us each, roughly 10 ms per level
// per victim: nothing against a block that takes minutes.
struct LevelResult
{
    Point   p;
    int32_t settled_mean_spread;
    int32_t widest_sample_band;
};

LevelResult measure_level(MuxScan& chain, const XtalkVictim& v);
```

Each level is that mean, min and max — plus the two statistics, so the three
are comparable:

```cpp
        hw.PrintLine("SHELL_TONE_LEVEL case=%d level=%d victim_group=%d "
                     "victim_ch=%d r_src=%d n=%d mean=%d min=%d max=%d",
                     i, static_cast<int>(level), vv.group, vv.channel,
                     static_cast<int>(vv.r_src_ohm), kToneRepeats,
                     p.mean, p.min, p.max);
        hw.PrintLine("SHELL_TONE_STAT case=%d settled_mean_spread=%d "
                     "widest_sample_band=%d",
                     i, mean_spread, widest_band);
```

`SHELL_TONE_LEVEL`'s `mean`, `min` and `max` are over all 65 × 64
conversions; `SHELL_TONE_STAT`'s `settled_mean_spread` is the peak-to-peak of
the 65 sub-means and `widest_sample_band` the widest single sub-measurement's
raw min–max. Those are round one's two statistics, computed the same way over
the same shape — **say so in a comment**, because G8 compares them across two
firmware images and a reader has to be able to see that the comparison is
legitimate.

Use `kToneLevelPoints` from `tone_plan.h` (Task 2) for the sub-measurement
count rather than writing `65` here.

And the gates line, with `g8=-1`:

```cpp
        // g8=-1 means NOT EVALUATED HERE, and it is not a failure. G8
        // compares this image's floor against round one's silent block, and
        // round one's numbers are in xtalk.csv.meta.csv, not in this
        // firmware. Baking them in as constants would turn a measurement
        // into a literal nobody re-measures -- which is exactly what the
        // deleted kConversionNs was. read_tone.py computes G8 from the two
        // files and folds it into its exit code; gates_ok below excludes it.
        hw.PrintLine("SHELL_TONE_GATES g2=%d g4=%d g5=%d g7=%d g8=%d "
                     "missed_blocks=%d gates_ok=%d",
                     gates.g2_floor ? 1 : 0, gates.g4_jitter ? 1 : 0,
                     gates.g5_address ? 1 : 0, missed_blocks == 0 ? 1 : 0,
                     -1, static_cast<int>(missed_blocks),
                     (gates.g2_floor && gates.g4_jitter && gates.g5_address
                      && missed_blocks == 0) ? 1 : 0);
```

`xtalk_gates()` computes G2, G4 and G5 from an `XtalkSummary` with
`worst_control_delta = 0` — and **that zero has to carry a comment**: there
is no control curve in this probe, G6 does not apply, and a `g6` that read 1
here would claim a control was taken. It is not printed at all.

- [ ] **Step 5: Build, cmp, flash, read**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_TONE_PROBE=1
```

```bash
cp shell/build/shell-sram.bin /tmp/tone-task3.bin; cmp /tmp/tone-task3.bin /tmp/tone0.bin
```

Expected: differ. Flash, then:

```bash
python -c "import serial,sys; ser=serial.Serial(sys.argv[1],timeout=1.0); [sys.stdout.write(ser.readline().decode('utf-8','replace')) for _ in range(600)]" COM4 > /tmp/tone-levels.log
```

- [ ] **Step 6: Read the result honestly**

Report, in this order:

1. **`gates_ok`, `missed_blocks`, and each gate.** A non-zero `missed_blocks`
   means the foreground starved the callback and nothing in the block may be
   quoted — report it and stop, because the fix is in the spin, not in the
   numbers.
2. **`SHELL_TONE_CLK` and `SHELL_TONE_CAL` against round one's.** Same
   `adc_khz`, `lat_max - lat_min` in the 42…138 ns band. This is the third
   independent confirmation that `probe_adc` behaves the same across images,
   and the first taken with the SAI running.
3. **The stopped level's `settled_mean_spread`, per victim, against round
   one's silent block** (from `xtalk.csv.meta.csv`, scope `silent`). This is
   G8's input. Say the five pairs of numbers and their differences. Spec §7
   wants them within 4 counts.
4. **Running-silent minus stopped, per victim.** *This is the task's headline
   result.* It is the I2S / SAI-DMA contribution that `settle-measured.md`
   §5 named and could not test, and it is the first number anyone has for it.
   Report the mean difference and the difference in `settled_mean_spread`,
   and say whether it scales with `r_src` across the ladder — 150 Ω, 650 Ω,
   5150 Ω.
5. **Whether any level's mean sits outside its `coupon_expect` band** (G5's
   per-victim lines), which would say the victim is not where the table says.

- [ ] **Step 7: Commit**

```bash
git add shell/tone_probe.cpp
git commit -m "feat(shell): the tone generator, the phase clock, and the two silent levels

The callback writes a sine and nothing else -- no engine, no process() --
because the operating point of this image has to be 'the codec, and only the
codec' or what the ADC reads is the engine's supply draw wearing the codec's
name.

The foreground reads the block boundary through a seqlock and computes phase
from it. Two distinct traps are commented at the publish site: the pair must
be consistent (the seqlock), and the published phase belongs to the NEXT
block while the published DWT count belongs to THIS one. Getting the second
wrong offsets every phase by a constant 2 ms, which at 100 Hz is 200 whole
periods and looks like noise.

The spin that waits for a target phase runs with interrupts enabled so the
callback stays fed; only the conversion masks them, and only for the ~2 us
it takes. A starved callback outputs stale DMA contents, so G7 counts blocks
against elapsed time and a run that missed one reports no tone result at all.

g8 prints -1: it compares this image's floor against round one's, whose
numbers live in xtalk.csv.meta.csv and not in this firmware. Baking them in
would turn a measurement into a literal nobody re-measures, which is what
the deleted kConversionNs was.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: The phase grid

**Files:**
- Modify: `shell/tone_probe.cpp`

**Interfaces:**
- Consumes: everything from Tasks 2 and 3.
- Produces: `SHELL_TONE_CASE` and `SHELL_TONE` — the per-row, per-victim, per-phase-point measurement of spec §8.

- [ ] **Step 1: The measurement**

```cpp
// One phase point of one tone row against one victim.
//
// Every repeat waits for the NEXT crossing of its target phase, so repeats
// are one or more periods apart and never share a block's interrupt jitter
// -- which is the whole reason this is a phase grid and not 64 conversions
// in a row. The wait runs with interrupts ENABLED; only the conversion
// masks them.
Point measure_phase_point(uint32_t step, int sr_hz, int phase_idx, int* n_out)
{
    const uint32_t target = phase_target(phase_idx);

    int64_t sum   = 0;
    int32_t lo    = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    int     valid = 0;
    for(int r = 0; r < kToneRepeats; ++r)
    {
        uint32_t prev = phase_now(step, sr_hz);
        // Bounded, like every other wait in these two probes: a tone that
        // stopped -- a codec that lost its clock, a callback that died --
        // must leave a flag and a shorter n, not a board that is silent with
        // no clue why. Two periods at the LOWEST frequency in the ladder,
        // expressed in core cycles.
        const uint32_t t0 = cycles_now();
        bool reached = false;
        while(cycles_now() - t0 < kPhaseWaitTimeoutCycles)
        {
            const uint32_t now = phase_now(step, sr_hz);
            if(phase_reached(prev, now, target)) { reached = true; break; }
            prev = now;
        }
        if(!reached) { ++g_phase_timeouts; continue; }

        const int32_t v = probe_adc::sample_now(nullptr);
        sum += v;
        if(v < lo) lo = v;
        if(v > hi) hi = v;
        ++valid;
    }
    if(n_out) *n_out = valid;
    return valid > 0 ? Point{static_cast<int32_t>(sum / valid), lo, hi}
                     : Point{-1, -1, -1};
}
```

Two declarations this function needs, beside it in the anonymous namespace:

```cpp
// Two periods of the LOWEST frequency in the ladder, in core cycles: at
// 100 Hz a period is 10 ms, so two are 20 ms, and at 480 MHz that is
// 9 600 000 cycles. Derived from kFreqHz[0] rather than written as a
// literal, so a ladder that gains a lower frequency does not silently
// shorten its own patience.
constexpr uint32_t kPhaseWaitTimeoutCycles = 9600000u;

// Repeats that never saw their target phase. Printed, never folded into an
// average: a run that lost repeats must say so rather than report a quieter
// n that looks like a cleaner measurement.
uint32_t g_phase_timeouts = 0;
```

`n_out` is what the `SHELL_TONE` line's `n=` field carries, and it is the
repeat count that actually contributed — not `kToneRepeats`. A point whose
`n` is short is a point with a timeout behind it, and the reader can see it
without cross-referencing the gates line.

- [ ] **Step 2: Setting a row, and the order the block runs in**

Per row, per victim: park the victim, select its rung, set the callback's
step and amplitude, let the codec settle, then walk the 16 phase points.

```cpp
        // The amplitude from dbfs, computed once per row and handed to the
        // callback between cases, never during one. powf() is fine here:
        // this runs once per row in the foreground, nowhere near a timed
        // path.
        const float amp = powf(10.0f, static_cast<float>(row.dbfs) / 20.0f);
        g_phase_step = phase_step_per_sample(row.f_hz, sr_hz);
        g_amplitude  = amp;

        // Let the codec and the analog path settle into the new row before
        // the first conversion. Ten blocks is 20 ms, which is two periods of
        // the lowest frequency in the ladder -- derived, not measured, and
        // generous on purpose because it costs 20 ms against a row that
        // costs seconds.
        hw.Delay(20);
```

The static row (`f_hz == 0`, present only when Task 1 measured a DC-coupled
output) has no phase and takes the same whole-block measurement the levels
do, printed as a `SHELL_TONE_LEVEL` line with `level=2` and the row's
`f_hz=0`. Say so in a comment: it is a tone row by table position and a level
row by shape, and a reader that assumed every `level=2` row had a phase grid
would refuse the block.

- [ ] **Step 3: The output**

```cpp
        // BYTE BUDGET, and the reason the spec's single SHELL_TONE line is
        // split in two here: libDaisy's log buffer is 128 bytes
        // (lib/libDaisy/src/hid/logger.h:29) and the spec's combined line
        // runs about 130 at its widest values -- truncated, and stamped
        // "$$". Same split and same reason as the crosstalk probe's
        // SHELL_XTALK_CASE. This line runs 108 bytes; the point line below
        // runs 62.
        hw.PrintLine("SHELL_TONE_CASE case=%d level=%d f_hz=%d dbfs=%d "
                     "victim_group=%d victim_ch=%d r_src=%d below_corner=%d",
                     i, static_cast<int>(ToneLevel::Tone), row.f_hz, row.dbfs,
                     vv.group, vv.channel, static_cast<int>(vv.r_src_ohm),
                     row.below_corner ? 1 : 0);
        hw.PrintLine("SHELL_TONE case=%d phase_idx=%d n=%d mean=%d min=%d max=%d",
                     i, k, valid_n, p.mean, p.min, p.max);
```

- [ ] **Step 4: Build, cmp, flash, time the block**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_TONE_PROBE=1
```

```bash
cp shell/build/shell-sram.bin /tmp/tone-task4.bin; cmp /tmp/tone-task4.bin /tmp/tone-task3.bin
```

Expected: differ. Flash, then capture one whole block and time it:

```bash
python -c "import serial,sys,time; ser=serial.Serial(sys.argv[1],timeout=1.0); t=time.monotonic(); [sys.stdout.write(ser.readline().decode('utf-8','replace')) for _ in range(6000)]; print('elapsed %.1f s' % (time.monotonic()-t), file=sys.stderr)" COM4 > /tmp/tone-grid.log
```

**Report the measured block duration.** The plan's estimate is *derived* and
it is dominated by the phase waits, not the conversions: 9 rows × 5 victims ×
16 points × 64 repeats, each repeat waiting up to one period. At 100 Hz a
period is 10 ms, so the three 100 Hz rows alone cost on the order of
5 × 16 × 64 × 5 ms ≈ 26 s each. **That is minutes per block**, and it is the
number Task 6's reader timeout has to be built from. If it comes out
intolerable, the honest lever is `kToneRepeats` or the victim list — say so
and raise it, do not quietly shorten the phase wait, which is what makes the
repeats independent.

- [ ] **Step 5: Read the result honestly**

Report:

1. `gates_ok`, `missed_blocks`, `phase_timeouts`.
2. For each row and each victim, the peak-to-peak of the 16 per-point means.
   **Do not compute `delta_pp` by hand** — Task 6's reader does it against
   the running-silent level and its arithmetic has fixtures. Report the raw
   spread.
3. Whether the spread grows with frequency at fixed level (the capacitive
   signature) and whether it grows with level at fixed frequency (linearity).
   Say what you see; do not name a mechanism. `docs/gotchas.md` and
   `settle-measured.md` both carry entries about mechanisms named without a
   probe behind them.
4. The two 0 Ω victims against the 5150 Ω ones, and `REF_B` at 650 Ω between
   them. Spec §10: a spread that does not scale between 650 and 5150 Ω is not
   in the node.

- [ ] **Step 6: Commit**

```bash
git add shell/tone_probe.cpp
git commit -m "feat(shell): the phase grid

Sixteen points per period, 64 repeats, and every repeat waits for the NEXT
crossing of its target phase -- so repeats are one or more periods apart and
never share a block's interrupt jitter. That wait is the reason this is a
phase grid and not 64 conversions in a row, and it is also what makes a
block take minutes rather than seconds.

The wait is bounded at two periods of the ladder's lowest frequency and
counts its timeouts, printed beside missed_blocks: a codec that lost its
clock must leave a flag and a shorter n, not a quieter-looking average.

The static row, when the output is DC-coupled, is a tone row by table
position and a level row by shape -- it has no phase, so it prints as a
LEVEL line. A reader that assumed every level=2 row carried a phase grid
would refuse the block.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: The edge inside the sampling window

Round one's §6 deferred exactly one thing: seeing a transient that has decayed
before the 991 ns offset floor. The reverse order does it — start a long
conversion, land the aggressor edge *inside* its acquisition window — and it
belongs here because it is the second new sequence built on the same
primitives.

**Files:**
- Modify: `shell/tone_probe.cpp`

**Interfaces:**
- Consumes: `kToneWinCases`, `kToneWinRung`, `tone_win_ns()` (Task 2); `MuxScan::write_chain_timed()` (round one); `probe_adc::select_time()`, `sample_time_for_rung()`, `measure_clock()` (round one's Task 1).
- Produces: `SHELL_TONE_WINCASE` and `SHELL_TONE_WIN`.

- [ ] **Step 1: The sequence**

Spec §6's five lines, with the two things it leaves to the implementation
spelled out — how a conversion is started without `sample_now()`'s own
timing, and where the window's length comes from:

```cpp
// One point of the window sweep.
//
// The order is the reverse of every other measurement in these two probes:
// the conversion starts FIRST and the aggressor edge lands inside its
// acquisition window. The sample-and-hold tracks the node through the whole
// window and the aperture closes at its end, so a transient whose remainder
// at the aperture is still above the criterion shows as a function of how
// long before the end it was fired.
//
// t0 here is the CONVERSION START, not a latch edge. That is the difference
// from round one, and it is why this cannot be a case in round one's table.
// It deliberately does NOT call probe_adc::sample_now(): that function owns
// the whole start-to-read span, and this sequence has to act in the middle
// of it. Do not "unify" the two -- doing so removes the measurement.
Point measure_window_point(MuxScan& chain, const XtalkCase& c,
                           uint32_t window_cycles, uint32_t d_before_end_ns,
                           int* n_out)
{
    const uint32_t d_cycles = ns_to_cycles(d_before_end_ns);
    // The edge is fired when this much of the window has already elapsed.
    // A d_before_end longer than the window would mean firing before the
    // conversion started, which is round one's measurement at an
    // uncommanded delay; the caller has already refused that, and this is
    // the belt.
    if(d_cycles >= window_cycles) return Point{-1, -1, -1};
    const uint32_t fire_at = window_cycles - d_cycles;

    int64_t sum   = 0;
    int32_t lo    = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    int     valid = 0;
    for(int r = 0; r < kToneRepeats; ++r)
    {
        chain.write_chain(c.word_a);
        const uint32_t p0 = cycles_now();
        while(cycles_now() - p0 < ns_to_cycles(kParkNs)) { }

        // Interrupts masked across the whole sequence, not just the
        // conversion: the edge's position INSIDE the window is the
        // measurement, and a block interrupt landing between the start and
        // the latch would move it by microseconds. The mask is saved and
        // restored rather than cleared and set, so this cannot turn
        // interrupts on if it was called with them off -- same discipline as
        // probe_adc::sample_now().
        //
        // It costs the callback one window, ~63 us, well inside a 2 ms
        // block, and G7 is what notices if that ever stops being true.
        const uint32_t primask = __get_PRIMASK();
        __disable_irq();

        const uint32_t t0 = cycles_now();
        ADC1->CR |= ADC_CR_ADSTART;    // the same direct start probe_adc uses

        while(cycles_now() - t0 < fire_at) { }
        (void)chain.write_chain_timed(c.word_b);

        bool timed_out = true;
        while((cycles_now() - t0) < kWinPollTimeoutCycles)
            if((ADC1->ISR & ADC_ISR_EOC) != 0u) { timed_out = false; break; }
        const int32_t v = timed_out ? -1 : static_cast<int32_t>(ADC1->DR);

        __set_PRIMASK(primask);

        if(timed_out) { ++g_win_timeouts; continue; }
        sum += v;
        if(v < lo) lo = v;
        if(v > hi) hi = v;
        ++valid;
    }
    if(n_out) *n_out = valid;
    return valid > 0 ? Point{static_cast<int32_t>(sum / valid), lo, hi}
                     : Point{-1, -1, -1};
}
```

`g_win_timeouts` is a file-scope `uint32_t` beside `g_phase_timeouts`, with
the same comment and printed on the same line. `kWinPollTimeoutCycles` is
dealt with in step 2.

This is the one place in either probe that drives ADC1's registers outside
`probe_adc`, and the function's own comment says why. It also needs
`#include <stm32h7xx_hal.h>` in `tone_probe.cpp` for `ADC1`, `ADC_CR_ADSTART`
and `ADC_ISR_EOC`, with the same note `probe_adc.cpp` carries: raw HAL, not
libDaisy's `AdcHandle`.

- [ ] **Step 2: The window's length, from this boot's clock**

```cpp
    // 387.5 sampling cycles at THIS boot's measured ADC clock. Not
    // kToneWinWindowNsNominal -- that constant exists for the host assertion
    // that the grid fits inside the window, and a firmware that used it
    // would be back to assuming a clock the probe can measure, which is the
    // mistake settle-measured.md section 1 records costing three fix rounds.
    const uint32_t window_cycles = static_cast<uint32_t>(
        (static_cast<double>(kSamplingLadderTenths[kToneWinRung]) / 10.0)
        * clk.core_cyc_per_adc_cyc + 0.5);
    const uint32_t window_ns = cycles_to_ns(window_cycles);
```

Print it, and refuse the sweep if the grid does not fit:

```cpp
        // A window shorter than the grid means the last points would fire
        // before the conversion started. Refuse the sweep and say so rather
        // than print points measured at a delay nobody commanded.
        hw.PrintLine("SHELL_TONE_WINDOW window_ns=%d nominal_ns=%d "
                     "grid_end_ns=%d fits=%d",
                     static_cast<int>(window_ns),
                     static_cast<int>(kToneWinWindowNsNominal),
                     static_cast<int>(tone_win_ns(kToneWinPoints - 1)),
                     (window_ns > tone_win_ns(kToneWinPoints - 1)) ? 1 : 0);
```

`kWinPollTimeoutCycles` must clear the long rung: `kPollTimeoutCycles` in
`probe_adc` is already sized for it (300 µs at 480 MHz, sized to clear
396 ADC cycles even at 3 MHz), so reuse that number and cite it rather than
inventing a second one.

- [ ] **Step 3: The output**

```cpp
        hw.PrintLine("SHELL_TONE_WINCASE case=%d xtalk_case=%d victim_group=%d "
                     "victim_ch=%d r_src=%d word_a=%d word_b=%d",
                     i, w.xtalk_case, c.group, c.channel,
                     static_cast<int>(c.r_src_ohm),
                     static_cast<int>(c.word_a), static_cast<int>(c.word_b));
        hw.PrintLine("SHELL_TONE_WIN case=%d d_before_end_ns=%d n=%d mean=%d "
                     "min=%d max=%d",
                     i, static_cast<int>(tone_win_ns(k)), valid_n,
                     p.mean, p.min, p.max);
```

The window sweep runs with the codec **stopped** — it is a round-one
aggressor and the tone is not part of this question. Say so in a comment and
print it: the `SHELL_TONE_WINCASE` line's case index sits in the same
namespace as the tone cases, and a reader has to know the codec state
differed.

Add `codec=%d` to `SHELL_TONE_WINCASE` (0 stopped, 1 running) rather than
leaving it to the comment. Re-check the byte budget with it: the line runs
about 112 bytes at its widest values.

- [ ] **Step 4: Build, cmp, flash, read**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell -j8 images SHELL_COUPON_PROBE=1 SHELL_TONE_PROBE=1
```

```bash
cp shell/build/shell-sram.bin /tmp/tone-task5.bin; cmp /tmp/tone-task5.bin /tmp/tone-task4.bin
```

Expected: differ. Flash and capture.

- [ ] **Step 5: Read the result honestly**

Report:

1. `SHELL_TONE_WINDOW` — the measured window against the nominal 63 050 ns,
   and `fits`. If `fits=0`, the sweep did not run and that is the finding.
2. The two `MUX8_EN_N` / `REF_A` curves over `d_before_end_ns`, both
   directions. **This is the first time anything in this repo has seen inside
   the offset floor**, and the shape near the end of the window is the part
   round one could not reach.
3. Whether the two directions' curves are mirror images. A disturbance that
   flips sign with the edge is coupling; one that does not is the pulse
   itself. That is round one's argument, applied where round one could not
   look.
4. Where the two instruments' curves meet: this sweep's `d_before_end = 0`
   point and round one's `d = 0` point are the same victim and the same
   aggressor at two different places in the conversion, and the grids were
   built to line up end to end. Say whether they do, and do not smooth a
   discontinuity away — a step between them is a finding about the offset
   arithmetic and it belongs in the report, not in a fitted curve.

- [ ] **Step 6: Commit**

```bash
git add shell/tone_probe.cpp
git commit -m "feat(shell): the edge inside the sampling window

Round one could not see a transient that decayed before its 991 ns offset
floor, and said so. The reverse order sees it: the conversion starts first,
at the 387.5-cycle rung, and the aggressor edge lands inside the acquisition
window at a commanded time before its end.

This is the only place in either probe that touches ADC1's registers outside
probe_adc, and the comment says why: sample_now() owns the whole
start-to-read span and this sequence has to act in the middle of it.
Interrupts are masked across the whole sequence and not just the conversion,
because the edge's position inside the window IS the measurement.

The window's length comes from this boot's measured clock and not from the
nominal constant -- that constant exists for a host assertion, and a
firmware that used it would be assuming a clock the probe can measure, which
is the mistake that cost three fix rounds. If the grid does not fit inside
the measured window the sweep refuses to run and prints why.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: The reader, its guard, and G8

**Files:**
- Create: `shell/read_tone.py`, `shell/test_read_tone.py`
- Modify: `CMakeLists.txt` (after round one's `read_xtalk_guard` block)

**Interfaces:**
- Consumes: the block format from Tasks 3, 4 and 5; round one's `xtalk.csv.meta.csv`.
- Produces: `parse_block(lines)`, `format_csv(block)`, `format_meta_csv(block)`, `deltas(block)`, `delta_pp(block)`, `g8(block, xtalk_meta)`, `verdicts(block, xtalk_meta)`.

- [ ] **Step 1: Write the failing guard**

Create `shell/test_read_tone.py`, following round one's `test_read_xtalk.py`
in shape: a plain script, exit code is the verdict, no pytest, fixture
generated rather than pasted.

```python
"""Guard for read_tone.py's parser, its delta_pp arithmetic and G8.

Runs as a plain script; pytest is not installed on this machine.

G8 needs round one's numbers, so the fixture builds BOTH a tone block and a
minimal xtalk.csv.meta.csv. That is the point of the gate: a run whose floor
is not round one's floor would produce tone results that are all plausible
and all against the wrong baseline, and nothing in the tone block alone can
notice.
"""
import sys

from read_tone import (parse_block, format_csv, format_meta_csv, deltas,
                       delta_pp, g8, verdicts)

FAILURES = []


def check(label, cond):
    if not cond:
        FAILURES.append(label)


def build_xtalk_meta(spreads=(9, 9)):
    """Round one's metadata, cut to what G8 reads: each silent case's
    settled_mean_spread, and enough of its CASE rows to say which victim
    that case was."""
    rows = ["scope,case,key,value"]
    for i, (g, ch) in enumerate(((0, 8), (0, 10))):
        rows.append("case,%d,row,1" % i)
        rows.append("case,%d,victim_group,%d" % (i, g))
        rows.append("case,%d,victim_ch,%d" % (i, ch))
        rows.append("silent,%d,settled_mean_spread,%d" % (i, spreads[i]))
    return "\n".join(rows) + "\n"


def build_block(phase_points=4, gates_ok=1, missed=0,
                stopped_spreads=(9, 9),
                tone_means=(100, 106, 100, 94)):
    """Two victims, two levels and one tone row, at the smallest size that
    still exercises every rule."""
    lines = [
        "SHELL_TONE_CFG adc_khz=6146 repeats=64 phase_points=%d block_size=96 "
        "sr=48000 rv4=0 git=4a7800f+" % phase_points,
        "SHELL_TONE_CLK span_short_cyc=2000 span_long_cyc=30000 "
        "smp_short_tenths=165 smp_long_tenths=3875",
        "SHELL_TONE_CAL lat_mean_ns=747 lat_min_ns=700 lat_max_ns=800 b0=32 "
        "timeouts=0",
        "SHELL_TONE_SPAN zero=0 rail=63485 hi_spread=1 lo_spread=1 valid=1",
    ]
    victims = ((0, 8, 5150), (0, 10, 150))
    for g, ch, _r in victims:
        lines.append("SHELL_TONE_G5 victim_group=%d victim_ch=%d expect=2 "
                     "mean=100 ok=1" % (g, ch))

    case = 0
    for vi, (g, ch, r) in enumerate(victims):
        for level, mean in ((0, 100), (1, 100)):
            lines.append("SHELL_TONE_LEVEL case=%d level=%d victim_group=%d "
                         "victim_ch=%d r_src=%d n=64 mean=%d min=%d max=%d"
                         % (case, level, g, ch, r, mean, mean - 2, mean + 2))
            spread = stopped_spreads[vi] if level == 0 else 9
            lines.append("SHELL_TONE_STAT case=%d settled_mean_spread=%d "
                         "widest_sample_band=44" % (case, spread))
            case += 1
        lines.append("SHELL_TONE_CASE case=%d level=2 f_hz=1000 dbfs=-6 "
                     "victim_group=%d victim_ch=%d r_src=%d below_corner=0"
                     % (case, g, ch, r))
        for k in range(phase_points):
            lines.append("SHELL_TONE case=%d phase_idx=%d n=64 mean=%d min=%d "
                         "max=%d" % (case, k, tone_means[k],
                                     tone_means[k] - 2, tone_means[k] + 2))
        case += 1

    lines.append("SHELL_TONE_WINDOW window_ns=63050 nominal_ns=63050 "
                 "grid_end_ns=12800 fits=1")
    lines.append("SHELL_TONE_GATES g2=%d g4=%d g5=%d g7=%d g8=-1 "
                 "missed_blocks=%d gates_ok=%d"
                 % (gates_ok, gates_ok, gates_ok, 1 if missed == 0 else 0,
                    missed, gates_ok))
    lines.append("SHELL_TONE_END")
    return lines


def drop_first(lines, prefix):
    out = list(lines)
    for i, line in enumerate(out):
        if line.startswith(prefix):
            del out[i]
            return out
    raise AssertionError("no line starts with %r" % prefix)


# --- 1-4: a complete block ---
base = build_block()
block = parse_block(base)
check("a complete block parses", block is not None)
check("both levels are kept per victim", block and len(block["levels"]) == 4)
check("every phase point is kept", block and len(block["points"]) == 8)
check("the window line is kept", block and block["window"]["fits"] == 1)

# --- 5-7: incomplete blocks are refused ---
check("a block truncated before SHELL_TONE_END is refused",
      parse_block(base[:-1]) is None)
check("a block missing one case's phase point is refused",
      parse_block(drop_first(base, "SHELL_TONE case=2 phase_idx=1")) is None)
check("a block missing a LEVEL line is refused",
      parse_block(drop_first(base, "SHELL_TONE_LEVEL case=1 ")) is None)

# --- 8: delta and delta_pp ---
# delta(phi) = mean_tone(phi) - mean_running_silent, and the number that
# carries the result is the PEAK-TO-PEAK across the grid -- a sinusoidal
# disturbance has zero mean over a period, so an average would report every
# tone case as zero.
d = deltas(block)
check("delta is the tone point minus that victim's running-silent mean",
      d and [row["delta"] for row in d if row["case"] == 2] == [0, 6, 0, -6])
pp = delta_pp(block)
check("delta_pp is the peak-to-peak across the phase grid",
      pp and all(row["delta_pp"] == 12 for row in pp))
check("a mean of delta would have been zero, which is why pp is the statistic",
      sum(row["delta"] for row in d if row["case"] == 2) == 0)

# --- 9: the criterion ---
# delta_pp <= 8 on every 5150 ohm victim at every row.
v = verdicts(parse_block(build_block(tone_means=(100, 103, 100, 97))),
             build_xtalk_meta())
check("a delta_pp of 6 passes", v and all(row["pass"] for row in v))
fail_v = verdicts(block, build_xtalk_meta())
check("a delta_pp of 12 fails on the 5150 ohm victim",
      fail_v and not all(row["pass"] for row in fail_v))
check("and the 150 ohm victim is reported beside it, not gated on",
      fail_v and any(row["r_src"] == 150 for row in fail_v))

# --- 10: G8 ---
# The stopped level's settled_mean_spread must be within 4 counts of round
# one's silent block for the same victim.
check("G8 passes when the floors agree",
      g8(parse_block(build_block(stopped_spreads=(9, 10))),
         build_xtalk_meta(spreads=(9, 9)))["pass"])
check("G8 passes at exactly 4 counts",
      g8(parse_block(build_block(stopped_spreads=(13, 9))),
         build_xtalk_meta(spreads=(9, 9)))["pass"])
check("G8 fails at 5 counts",
      not g8(parse_block(build_block(stopped_spreads=(14, 9))),
             build_xtalk_meta(spreads=(9, 9)))["pass"])
check("G8 names the victim whose floor moved",
      g8(parse_block(build_block(stopped_spreads=(14, 9))),
         build_xtalk_meta(spreads=(9, 9)))["worst_victim"] == (0, 8))

# --- 11: a failed gate refuses the verdict ---
check("a run whose firmware gates failed yields no verdict",
      verdicts(parse_block(build_block(gates_ok=0)), build_xtalk_meta()) == [])
check("a run that missed a block yields no verdict",
      verdicts(parse_block(build_block(missed=3)), build_xtalk_meta()) == [])
check("a run whose G8 failed yields no verdict",
      verdicts(parse_block(build_block(stopped_spreads=(14, 9))),
               build_xtalk_meta(spreads=(9, 9))) == [])

# --- 12: the CSV files ---
csv = format_csv(block)
check("the CSV has a header and one row per phase point",
      len(csv.strip().split("\n")) == 9)
meta_rows = [r.split(",") for r in format_meta_csv(block).strip().split("\n")]
check("the metadata CSV has the four-column header",
      meta_rows[0] == ["scope", "case", "key", "value"])
meta = {(r[0], r[1], r[2]): r[3] for r in meta_rows[1:]}
check("the metadata carries the block period's inputs",
      meta.get(("cfg", "", "block_size")) == "96"
      and meta.get(("cfg", "", "sr")) == "48000")
check("the metadata carries the firmware's g8 sentinel, unaltered",
      meta.get(("gates", "", "g8")) == "-1")
check("the metadata carries the measured window",
      meta.get(("window", "", "window_ns")) == "63050")

# --- 13: libDaisy's "$$" overflow marker ---
truncated = list(base)
truncated[-2] = truncated[-2][:-2] + "$$"
check("a $$-truncated field does not crash the parser, and yields no block",
      parse_block(truncated) is None)

if FAILURES:
    for f in FAILURES:
        print("FAIL: %s" % f, file=sys.stderr)
    raise SystemExit(1)
print("read_tone guard: ok")
```

- [ ] **Step 2: Run it to verify it fails**

```bash
python shell/test_read_tone.py
```

**Expected RED:** `ModuleNotFoundError: No module named 'read_tone'`. Red
because the module does not exist. Run it with `shell/` as the working
directory.

- [ ] **Step 3: Write the reader**

Create `shell/read_tone.py`, following `read_xtalk.py` in shape: `import
serial` inside `main()`, accumulate to the end marker, per-case completeness
counts.

The usage differs from every other reader here, and the difference is
load-bearing:

```python
"""...

Call:
    python read_tone.py PORT out.csv XTALK_META.csv [timeout_s]

XTALK_META.csv is round one's `xtalk.csv.meta.csv`, and it is NOT optional.
G8 compares this image's stopped-level floor against round one's silent
block, per victim, and without that file the gate cannot be evaluated at all
-- so this reader refuses to run rather than print a verdict with a gate
missing. A tone result against the wrong baseline is plausible in every
digit, which is exactly the failure G8 exists to catch.

Default timeout: <the number Task 4 measured>. The block is dominated by the
phase waits and not by the conversions -- every repeat waits for the next
crossing of its target phase, and at 100 Hz that is up to 10 ms each.
"""
```

`g8()` returns a dict with `pass`, `worst_delta`, `worst_victim` and the
per-victim pairs, so the failure names which victim's floor moved rather than
just refusing. `verdicts()` returns `[]` when the firmware's gates failed,
when `missed_blocks` is non-zero, or when G8 failed — the three reasons the
tone numbers are not interpretable, each printed distinctly on stderr.

The criterion is spec §4's: `delta_pp <= 8` on every **5150 Ω** victim at
every table row. The 650 Ω and 150 Ω victims are reported beside it and not
gated on, because they are the attribution axis and not the product question —
say that in a comment, because a reader that silently gated them would fail a
run for a result that is doing its job.

- [ ] **Step 4: Run the guard to verify it passes**

```bash
python shell/test_read_tone.py
```

Expected: `read_tone guard: ok`.

- [ ] **Step 5: Register it with ctest**

```cmake
add_test(NAME read_tone_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_read_tone.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)
```

- [ ] **Step 6: Run the whole suite**

```bash
source env.sh; cmake -S . -B build -DCMAKE_BUILD_TYPE=Release; cmake --build build; ctest --test-dir build --output-on-failure
```

Expected: every test passes, including `read_xtalk_guard`,
`read_settle_guard`, `read_coupon_guard` and the new `read_tone_guard`.

- [ ] **Step 7: Prove the RED on G8, deliberately**

Spec §7 names G8 as the one to red-prove: "a stopped-level spread 5 counts off
round one's must fail, because that is a run whose tone results would all be
plausible and all be against the wrong baseline."

In `g8()`, replace the bound

```python
    result["pass"] = worst <= 4
```

with

```python
    result["pass"] = True
```

Run the guard. **Expected: FAIL** on "G8 fails at 5 counts", on "a run whose
G8 failed yields no verdict", and on "G8 names the victim whose floor moved"
only if you also removed `worst_victim` — check which of the three fired and
report it.

It is red because with the bound gone, an image whose floor sits 5 counts off
round one's passes, and every `delta_pp` in it is a perfectly plausible
number measured against a baseline that is not the one it claims. Nothing
else in the block can notice: the tone results do not look wrong, they look
like results.

**Retype** the original line; do not `git checkout`. Re-run, confirm green,
report what you saw.

- [ ] **Step 8: Read a real block from the board**

Flash Task 5's image if the board is not still carrying it, and make sure
round one's `xtalk.csv.meta.csv` is to hand:

```bash
python shell/read_tone.py COM4 tone.csv xtalk.csv.meta.csv
```

Report: the exit code, the stderr summary, the row counts in both CSVs, the
G8 numbers per victim, and then the verdicts — per table row, per victim,
with `delta_pp` — saying plainly for each whether the criterion held.

Then the three things the whole two-plan sequence was for, stated as
measurements and not as conclusions:

1. **Whether `delta_pp` grows linearly with frequency at fixed level.**
2. **Whether it grows linearly with level at fixed frequency.**
3. **Whether it scales between 650 Ω and 5150 Ω.** Spec §10: a `delta_pp`
   that does not scale between them is not in the node.

Do not put `tone.csv` in the repo. `docs/hardware/` gets a measured document
written from it and from round one's capture, which is separate work and not
this plan's.

- [ ] **Step 9: Commit**

```bash
git add shell/read_tone.py shell/test_read_tone.py CMakeLists.txt
git commit -m "feat(shell): the codec-tone reader, and a G8 that needs round one's file

delta_pp and not a mean, because a sinusoidal disturbance has zero mean over
a period and an averaged delta would report every tone case as clean. The
guard asserts that directly: the same fixture whose deltas sum to zero has a
peak-to-peak of twelve.

The reader takes round one's xtalk.csv.meta.csv on the command line and
REFUSES TO RUN without it. G8 compares this image's stopped-level floor
against round one's silent block per victim, and a tone result against the
wrong baseline is plausible in every digit -- there is nothing in the tone
block alone that could notice. G8 was red-proven by removing its bound.

The criterion gates the 5150 ohm victims only. The 650 ohm and 0 ohm ones
are the attribution axis, reported beside it and never gated on: a reader
that gated them would fail a run for a result that is doing its job.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Self-review against the spec

| Spec section | Where it lands |
|---|---|
| §1 what this is | The whole plan; the "not the 2026-08-23 artifact question" boundary is `tone_probe.h`'s header comment (Task 1) |
| §2 why this is round two | Task 1 (audio running, the exclusive switch), Task 2 (phase as the abscissa), Task 3 (the time reference in the callback) |
| §3 the tone | Task 2 (the two ladders, sine only, the square row left out of scope), Task 3 step 1 (the callback, `sinf` and why not a table) |
| §4 three levels per victim | Task 3 step 4 (Stopped, RunningSilent), Task 4 (Tone); `delta` and `delta_pp` are Task 6. The criterion is Task 6's, on the 5150 Ω victims |
| §5 the phase grid | Task 2 (the arithmetic and its host assertions), Task 3 step 2 (`phase_now`), Task 4 step 1 (the wait, and which spin masks interrupts) |
| §6 the edge inside the sampling window | Task 2 (`kToneWinCases`, the rung, the grid that meets round one's end to end), Task 5 (the sequence, the measured window, the refusal when it does not fit) |
| §7 gates | Task 3 step 4 (G2, G4, G5 reused from round one; G7); Task 6 (G8, red-proven). G6's non-applicability is stated in the decisions section |
| §8 output | Task 1 (`_CFG`, `_END`), Task 3 (`_CLK`, `_CAL`, `_SPAN`, `_G5`, `_LEVEL`, `_STAT`, `_GATES`), Task 4 (`_CASE`, `SHELL_TONE`), Task 5 (`_WINDOW`, `_WINCASE`, `_WIN`), Task 6 (the reader and its guard) |
| §9 before it is built | **Task 1**, which is why it is first: the bench reading decides the plan table, and the spec is amended with it in the same task |
| §10 how this can go red | Task 2 step 7 (the Nyquist row, the spec's own candidate, with its second consequence reported too), Task 2's phase assertions, Task 6 step 7 (G8, the spec's own candidate). The 0 Ω/650 Ω statement is Task 4 step 5 item 4 and Task 6 step 8 item 3 |
| §11 read / derived / unmeasured | Task 1 step 7 moves DC coupling from unmeasured to measured in the spec itself; `kToneWinWindowNsNominal` is labelled derived and the firmware measures the real one; the "0 dBFS on an unloaded output is a valid aggressor" row stays reasoned and is not upgraded anywhere |
| §12 out of scope | Nothing built for the square-wave row, a populated jack, the scan-into-audio direction, a second board or a second submodule |

**Placeholder scan:** none. Two values are deliberately left for the executor
to fill from a measurement rather than from this plan, and both say so
explicitly and say where the number comes from: `read_tone.py`'s default
timeout (Task 4's measured block duration) and `kToneCornerHz` (Task 1's
bench reading). Neither is a TBD — each has a named source and a task that
produces it.

**Type consistency:** `ToneLevel`, `ToneRow`, `kToneRows`, `kToneRowCount`,
`ToneWinCase`, `kToneWinCases`, `kToneWinCaseCount`, `kTonePhasePoints`,
`kTonePhaseScale`, `kToneRepeats`, `kToneSampleRateHz`, `kToneDbfsFloor`,
`kToneHasStaticRow`, `kToneCornerHz`, `kToneWinRung`, `kToneWinPoints`,
`kToneWinStepNs`, `kToneWinWindowNsNominal`, `phase_step_per_sample()`,
`phase_target()`, `phase_advance()`, `phase_reached()`, `tone_win_ns()` are
spelled the same in every task that touches them. Everything from round one —
`probe_adc::`, `kXtalkVictimTable`, `kXtalkPlan`, `XtalkKind`,
`xtalk_gates()`, `victim_address_mask()`, `victim_enable_mask()`,
`step_of()`, `MuxScan::write_chain_timed()` — is used under round one's
spelling and redefined nowhere.

**Three gaps, stated rather than hidden:**

1. **A block takes minutes, not seconds.** The phase wait is what makes the
   repeats independent, and at 100 Hz it dominates everything else. The
   derived estimate is in Task 4 step 4; if the measured number is
   intolerable, the levers are `kToneRepeats` and the victim list, and that
   is a decision for Bastian rather than a quiet edit to the wait.
2. **Which round-one aggressors earn the window sequence is a result, not a
   plan.** Only the unconditional `MUX8_EN_N` / `REF_A` pair is in
   `kToneWinCases`. Spec §6's "the aggressors that showed a delta at all"
   cannot be known until round one's reader has printed its verdicts, and
   `tone_plan.cpp` says so at the table with instructions for adding them.
3. **`kToneCornerHz` ships at 0 and Task 1 sets it.** 0 is both the correct
   value for a DC-coupled output and the only safe default for one nobody has
   measured, and the header says so; when the bench reading finds an
   AC-coupled output, the measured corner goes in and the `below_corner`
   assertion in `test_tone_plan.cpp` starts doing work. Task 1 is a hard
   prerequisite of Task 2, so it is never left unmeasured in practice — but a
   reader skipping straight to the header should not mistake the 0 for a
   decision that was made rather than one that is pending.

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-09-18-coupon-codec-tone-probe.md`. **Round one must be finished first** — see "This plan depends on round one, completely" above. Two execution options:

**1. Subagent-Driven (recommended)** — a fresh subagent per task, review between tasks. Tasks 1, 3, 4, 5 and the last step of 6 need the board on USB and a human to press BOOT+RESET; Task 1 additionally needs a multimeter and an operator willing to hold two probes. **Task 1 is a hard gate:** it decides Task 2's table, so do not dispatch Task 2 until its reading is recorded in the spec.

**2. Inline Execution** — tasks run in this session with checkpoints for review.
