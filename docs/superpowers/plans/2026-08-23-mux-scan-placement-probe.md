# Mux Scan Placement Probe — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Price the panel's 595-chain/mux scan on the Patch Submodule that is already on the desk — in the audio callback against in the foreground, in CPU points and in dB on the block-rate tone — so the control-PCB schematic is drawn with a measured number instead of a guess.

**Architecture:** Three pieces, and only one of them needs a board. A pure scan plan (`shell/mux_plan.*`) that turns a step index into an address pattern, an enable mask and a chain word — no hardware type, tested inside `spky_tests`. A hardware half (`shell/mux_scan.*`) that bit-bangs the four production chain pins and reads the four raw ADC channels, selected by a build switch with three positions: off, in the callback, in the foreground. And a host-side analyser (`tools/blockrate_fft.py`) that turns a recording into "level at the block rate relative to total RMS", the one number the 8 Aug finding is written in.

The scan never busy-waits for the mux to settle. It writes the address at the end of one audio block and samples it at the start of the next, so the block period *is* the settle window. That is the design being priced.

**Tech Stack:** ARM GCC via `make` in `shell/` (never `source env.sh` in that shell), clang + Ninja for `spky_tests`, `dfu-util` over USB for flashing, pyserial for the probe read-back, ffmpeg (dshow) + numpy for the audio side.

**Spec:** `docs/hardware/io-budget.md` §3 and §6 (topology, GPIO balance, the three ways this can jam), plus the 2026-08-16 entry under "M6 — Hardware prototype" in `docs/roadmap.md`. There is no separate design doc; §6 carries the argument.

## What this decides, and what it does not

Three decisions hang on the result, and none of them is "optimise the engine harder":

1. **Where the scan runs.** Callback or foreground. Nobody has asked this before — §6 costs the scan as "ein pro Block gebitbangter Ketten-Write plus Kanalauswertung" landing in the CPU reserve, which silently assumes the callback. If a foreground scan keeps up with one step per block, the audio budget is not the constraint at all and the whole item dissolves.
2. **Whether a co-controller goes on the control PCB.** §6's fallback path. That is a part in the schematic and a second firmware project; it has to be decided before the board is ordered, not after.
3. **Whether a periodic chain burst moves the block-rate tone.** The 8 Aug finding says the tone hangs on how compute activity is distributed inside the block — filling the idle gap with a `nop` loop moved it 7.5 dB. A burst of GPIO edges every block is a new source of exactly that kind, and no amount of CPU saving fixes it if it lands.

Explicitly **not** decided here, and no task may pretend otherwise:

- **The settle time per channel.** That needs a 74HC4051/4067, a pot and jumper wires on the table — Phase-0 Task 6 step 5b, unchanged and still open.
- **8:1 against 16:1.** Availability and assembly price, not a measurement.
- **The per-channel cost of the apply side.** The probe images deliberately do not push values into the engine (see Global Constraints); what is priced is the chain write and the ADC reads.

## Honest limits of this round, to be repeated in the write-up

Nothing is attached to the four pins. The drive current is therefore lower than production, where each edge charges a real chain's input capacitance. A **null result on the block-rate tone is a lower bound, not an acquittal** — the same honesty the `nop`-loop experiment was written up with in `shell/README.md`. A *positive* result, on the other hand, is decisive: if an unloaded burst already moves the tone, a loaded one will not move it less.

The bit-bang clocks as fast as two register writes allow, with no inter-edge delay. If a real chain needs one, the measured cost is **optimistic**. Say so in the table rather than in a footnote.

## Global Constraints

- **`shell/` builds with ARM GCC through `make`, and `source env.sh` must never be run in that shell.** `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"`, target `images`, never `all`.
- **`-O3` is fixed** by `override OPT := -O3` in `shell/Makefile`. Do not touch it: every number in `docs/bench/` is an `-O3` number.
- **Every build switch gets a generated header**, in the shape of `write_shell_selftest.py` / `write_shell_cpu_probe.py`. A bare `-D` is invisible to make's dependency graph and hands out a stale `main.o` — this project has been bitten by exactly that.
- **One build, one question, one flash.** Do not fold two changes into one image; the 8 Aug session lost a day to a diagnostic image whose phases were timed by block count.
- **No claim without a probe.** Nothing in this plan's write-up may state a runtime number the run did not print. Arithmetic (steps per sweep) is arithmetic and may be stated as such.
- **Re-measure the baseline in the same session.** The 62.78 % / 65.30 % / 48.42 % from 2026-08-08 predates FEED and everything after it; it is a historical row, not a comparison partner.
- **The probe images do not apply mux values to the engine.** The operating point must stay bit-for-bit the one the baseline image runs, or the audio comparison compares two different instruments.
- **New code and comments are English** (`CLAUDE.md`), even though `shell/main.cpp` around them is German.
- **Never prefix a shell command with `cd`.** Run the shell build from a script in the scratchpad if a directory change is genuinely needed.

---

### Task 1: The block-rate analyser

**Files:**
- Create: `tools/blockrate_fft.py`
- Test: `tools/test_blockrate_fft.py`

**Interfaces:**
- Consumes: nothing.
- Produces: `blockrate_fft.analyze(path, block_rate_hz=500.0, half_width_hz=5.0) -> dict` with keys `total_rms_dbfs`, `band_rms_dbfs`, `excess_db` (band minus total), `sample_rate`, `seconds`. Task 5 calls it; the CLI prints the same four numbers.

There is no pytest in this environment. The guard is a plain script with asserts, the same shape as `tools/test_count_panel_controls.py`.

- [ ] **Step 1: Write the failing guard**

Create `tools/test_blockrate_fft.py`:

```python
#!/usr/bin/env python3
"""Guard rails for the block-rate analyser.

No pytest here -- plain asserts, exit code says it all, same shape as
tools/test_count_panel_controls.py. Run from tools/:
    python test_blockrate_fft.py

Why a guard at all for forty lines of numpy: the 8 Aug finding is written
in ONE number ("the tone sits 4.9 dB under total RMS"), and a scaling slip
in that number is invisible -- it looks like a measurement either way.
"""
import os, sys, wave, tempfile
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blockrate_fft as b

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def write_wav(path, data, sr=48000):
    """Stereo 16-bit, the format ffmpeg's dshow capture writes."""
    x = np.clip(data, -1.0, 1.0)
    frames = (x * 32767.0).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(np.repeat(frames, 2).tobytes())


def test_pure_tone_reports_its_own_rms():
    # A sine of amplitude 0.1 has RMS 0.1/sqrt(2) = -23.01 dBFS. The band
    # RMS around its frequency must report that and not half of it, not
    # twice it -- window-gain bookkeeping is exactly where this goes wrong.
    sr, n = 48000, 48000 * 2
    t = np.arange(n) / sr
    sig = 0.1 * np.sin(2 * np.pi * 500.0 * t)
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "tone.wav")
        write_wav(p, sig, sr)
        r = b.analyze(p, block_rate_hz=500.0)
    check(abs(r["band_rms_dbfs"] - (-23.01)) < 0.5,
          "pure tone band RMS %.2f dBFS, expected -23.0" % r["band_rms_dbfs"])
    check(abs(r["excess_db"]) < 0.5,
          "a wav that IS the tone must have excess ~0 dB, got %.2f"
          % r["excess_db"])


def test_noise_only_has_no_block_rate_line():
    # The discriminating case. If this passes for noise too, the analyser
    # is measuring the band's width and not a tone in it.
    sr, n = 48000, 48000 * 2
    rng = np.random.default_rng(0)
    sig = 0.05 * rng.standard_normal(n)
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "noise.wav")
        write_wav(p, sig, sr)
        r = b.analyze(p, block_rate_hz=500.0)
    check(r["excess_db"] < -15.0,
          "noise should sit far below total RMS in a 10 Hz band, got %.2f dB"
          % r["excess_db"])


def test_the_8_aug_shape_is_reproduced():
    # Noise floor plus a line 5 dB ABOVE the total RMS is not physical, so
    # the built case is the real one: a tone a few dB under a broadband
    # signal, the shape the board actually showed.
    sr, n = 48000, 48000 * 2
    t = np.arange(n) / sr
    rng = np.random.default_rng(1)
    sig = 0.2 * rng.standard_normal(n) + 0.05 * np.sin(2 * np.pi * 500.0 * t)
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "mix.wav")
        write_wav(p, sig, sr)
        r = b.analyze(p, block_rate_hz=500.0)
    # tone RMS = 0.05/sqrt(2) = -29.0 dBFS; total ~ 0.2 -> -14.0 dBFS.
    check(abs(r["band_rms_dbfs"] - (-29.0)) < 0.7,
          "band RMS %.2f dBFS, expected -29.0" % r["band_rms_dbfs"])
    check(-17.0 < r["excess_db"] < -12.0,
          "excess %.2f dB outside the constructed -15 dB" % r["excess_db"])


for name, fn in sorted(list(globals().items())):
    if name.startswith("test_") and callable(fn):
        fn()

if FAILS:
    print("FAIL")
    for f in FAILS:
        print("  " + f)
    raise SystemExit(1)
print("ok: blockrate_fft guard")
```

- [ ] **Step 2: Run it and watch it fail for the right reason**

Run: `python tools/test_blockrate_fft.py`
Expected: `ModuleNotFoundError: No module named 'blockrate_fft'`. Not an assertion failure — the module does not exist yet.

- [ ] **Step 3: Write the analyser**

Create `tools/blockrate_fft.py`:

```python
#!/usr/bin/env python3
"""Level at the audio block rate, relative to total RMS.

The 8 Aug diagnosis of the block-rate tone (shell/README.md, "Offener
Befund") was an ad-hoc numpy session. This file is the same measurement
with a name and a guard, because the round that prices the mux scan has to
compare against those numbers and a re-derived scaling would compare two
different quantities.

Why "relative to total RMS" and not dBFS alone: the damping between the
board's DAC and the interface input is unknown and irrelevant -- a ratio
is scale-invariant, an absolute level is not. Both are reported anyway,
because a clipped or near-silent capture has to be visible as such.

Capture side (the only DirectShow audio input on this machine):
    ffmpeg -f dshow -i audio="Line (Universal Audio Twin USB)" \
           -t 10 -ac 2 -ar 48000 -y out.wav

Usage:
    python blockrate_fft.py out.wav [--block-rate 500] [--half-width 5]
"""
import argparse
import wave

import numpy as np


def _read_mono(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise SystemExit("expected 16-bit PCM, got %d bytes/sample"
                             % w.getsampwidth())
        sr = w.getframerate()
        ch = w.getnchannels()
        raw = w.readframes(w.getnframes())
    x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    # The channel count comes from the header and is not inferred from the
    # length -- a mono capture with an even sample count would otherwise be
    # folded in half, which looks like a plausible signal.
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, sr


def _dbfs(rms):
    # A silent capture is a real outcome, not a crash. -inf would poison
    # every arithmetic downstream, so it floors at -200.
    return 20.0 * np.log10(rms) if rms > 1e-10 else -200.0


def analyze(path, block_rate_hz=500.0, half_width_hz=5.0):
    """RMS in a narrow band around block_rate_hz, against the total RMS.

    The band level is computed by zeroing every bin outside the band and
    transforming back, NOT by reading a peak bin. A peak bin needs a window
    correction factor to become an RMS, and that factor is the single most
    common place for this measurement to be off by 3 dB.
    """
    x, sr = _read_mono(path)
    if x.size == 0:
        raise SystemExit("empty capture: %s" % path)

    total = float(np.sqrt(np.mean(x * x)))

    spec = np.fft.rfft(x)
    freq = np.fft.rfftfreq(x.size, 1.0 / sr)
    keep = np.abs(freq - block_rate_hz) <= half_width_hz
    band_spec = np.where(keep, spec, 0.0)
    band = np.fft.irfft(band_spec, n=x.size)
    band_rms = float(np.sqrt(np.mean(band * band)))

    return {
        "sample_rate": sr,
        "seconds": x.size / float(sr),
        "total_rms_dbfs": _dbfs(total),
        "band_rms_dbfs": _dbfs(band_rms),
        "excess_db": _dbfs(band_rms) - _dbfs(total),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("wav")
    ap.add_argument("--block-rate", type=float, default=500.0,
                    help="audio block rate in Hz; 48000/96 = 500")
    ap.add_argument("--half-width", type=float, default=5.0)
    a = ap.parse_args()
    r = analyze(a.wav, a.block_rate, a.half_width)
    print("%s  %.1f s @ %d Hz" % (a.wav, r["seconds"], r["sample_rate"]))
    print("  total RMS      %8.2f dBFS" % r["total_rms_dbfs"])
    print("  %.0f Hz band    %8.2f dBFS" % (a.block_rate, r["band_rms_dbfs"]))
    print("  excess         %8.2f dB (band minus total)" % r["excess_db"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Run the guard and watch it pass**

Run: `python tools/test_blockrate_fft.py`
Expected: `ok: blockrate_fft guard`

- [ ] **Step 5: Prove the RED once**

Temporarily change `_dbfs` to `10.0 * np.log10(rms)` (the power-versus-amplitude slip this guard exists for), re-run the guard, confirm `test_pure_tone_reports_its_own_rms` fails, then revert. A guard that has never been seen red is a decoration.

- [ ] **Step 6: Commit**

```bash
git add tools/blockrate_fft.py tools/test_blockrate_fft.py
git commit -m "tools: the block-rate tone gets a name and a guard

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: The scan plan, tested before any board sees it

**Files:**
- Create: `shell/mux_plan.h`, `shell/mux_plan.cpp`
- Create: `tests/test_mux_plan.cpp`
- Modify: `CMakeLists.txt:90-91` (next to `shell/controls.cpp` / `tests/test_controls_map.cpp`)

**Interfaces:**
- Consumes: nothing.
- Produces: `shell::kSensePins`, `kMuxGroups`, `kMuxChannels`, `kScanSteps`, `kMuxTotal`, `kChainBits`, `shell::StepPattern{uint8_t address; uint8_t enable_mask;}`, `shell::step_pattern(int) -> StepPattern`, `shell::mux_channel(int step, int sense) -> int`, `shell::chain_word(StepPattern, uint32_t leds) -> uint32_t`. Task 3 calls all of them.

- [ ] **Step 1: Write the failing test**

Create `tests/test_mux_plan.cpp`:

```cpp
// The write side of the panel scan is data logic and belongs on the host,
// exactly like shell/controls.h. On the board a wrong address pattern shows
// up as "the wrong knob moved", which is expensive to find; here it is a
// line. Nothing in this file may include a hardware header.
#include <doctest/doctest.h>
#include <set>
#include "../shell/mux_plan.h"

TEST_CASE("mux plan: the address walks 0..15 once per group") {
    for(int g = 0; g < shell::kMuxGroups; ++g)
        for(int a = 0; a < shell::kMuxChannels; ++a)
        {
            const shell::StepPattern p
                = shell::step_pattern(g * shell::kMuxChannels + a);
            CHECK(static_cast<int>(p.address) == a);
        }
}

TEST_CASE("mux plan: exactly one group is enabled per step") {
    // Enables are active low. Two groups on at once shorts two mux outputs
    // onto one sense pin -- silently, and the reading looks plausible.
    for(int s = 0; s < shell::kScanSteps; ++s)
    {
        const shell::StepPattern p = shell::step_pattern(s);
        int low = 0;
        for(int g = 0; g < shell::kMuxGroups; ++g)
            if(((p.enable_mask >> g) & 1u) == 0u) ++low;
        CHECK(low == 1);
        CHECK(((p.enable_mask >> (s / shell::kMuxChannels)) & 1u) == 0u);
    }
}

TEST_CASE("mux plan: a step that does not exist enables nothing") {
    // The safe answer, and not an obvious one: an out-of-range ADDRESS
    // would still select some channel and return a foreign knob's voltage.
    for(int s : {-1, shell::kScanSteps, shell::kScanSteps + 7})
    {
        const shell::StepPattern p = shell::step_pattern(s);
        for(int g = 0; g < shell::kMuxGroups; ++g)
            CHECK(((p.enable_mask >> g) & 1u) == 1u);
    }
}

TEST_CASE("mux plan: every channel is reached exactly once per sweep") {
    std::set<int> seen;
    for(int s = 0; s < shell::kScanSteps; ++s)
        for(int i = 0; i < shell::kSensePins; ++i)
        {
            const int ch = shell::mux_channel(s, i);
            CHECK(ch >= 0);
            CHECK(ch < shell::kMuxTotal);
            seen.insert(ch);
        }
    CHECK(static_cast<int>(seen.size()) == shell::kMuxTotal);
}

TEST_CASE("mux plan: an index that does not exist is answered, not assumed") {
    CHECK(shell::mux_channel(-1, 0) == -1);
    CHECK(shell::mux_channel(0, -1) == -1);
    CHECK(shell::mux_channel(shell::kScanSteps, 0) == -1);
    CHECK(shell::mux_channel(0, shell::kSensePins) == -1);
}

TEST_CASE("mux plan: the LED field cannot collide with address or enable") {
    // One chain carries both, which is why LEDs cost no extra CPU. It is
    // also why an overlap would make a lit LED move a knob.
    const shell::StepPattern p = shell::step_pattern(5);
    const uint32_t no_leds  = shell::chain_word(p, 0u);
    const uint32_t all_leds = shell::chain_word(p, 0x7FFFFu);
    CHECK((no_leds & 0x0Fu) == p.address);
    CHECK(((no_leds >> shell::kEnableShift) & 0x03u) == p.enable_mask);
    CHECK((all_leds & 0xFFu) == (no_leds & 0xFFu));
}
```

- [ ] **Step 2: Wire the two files into the test binary**

In `CMakeLists.txt`, directly after `shell/controls.cpp` / `tests/test_controls_map.cpp` (line 90-91):

```cmake
    shell/mux_plan.cpp
    tests/test_mux_plan.cpp
```

- [ ] **Step 3: Run the test and watch it fail**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Expected: the build fails with `shell/mux_plan.h: No such file or directory`. `-DCMAKE_BUILD_TYPE=Release` is not optional — a Debug configure makes `spky_tests` and `ctrl_identity` fail on unrelated render hashes and hides this one.

- [ ] **Step 4: Write the header**

Create `shell/mux_plan.h`:

```cpp
#pragma once

// The write side of the panel scan, with no hardware type in it -- same
// arrangement as controls.h and for the same reason: this is where a wrong
// address pattern is one visible line instead of a knob that misbehaves on a
// board.
//
// Topology (docs/hardware/io-budget.md §3): up to eight CD74HC4067 share the
// four raw ADC pins; their address lines and their enables ride on the same
// 74HC595 chain that carries the LEDs, which is what makes the whole panel
// cost zero GPIOs and is the reason the 4-bit SD slot fits. One STEP of the
// scan is one address plus one enabled group; the four sense pins are then
// read in parallel, so a step yields four channels.
#include <cstdint>

namespace shell {

inline constexpr int kSensePins   = 4;   // raw ADC pins carrying a mux output
inline constexpr int kMuxGroups   = 2;   // 2 groups x 4 chips = 8 chips
inline constexpr int kMuxChannels = 16;  // CD74HC4067
inline constexpr int kScanSteps   = kMuxGroups * kMuxChannels;   // 32
inline constexpr int kMuxTotal    = kScanSteps * kSensePins;     // 128

// Bits clocked out per step, and the number the bit-bang cost scales with --
// so it is a constant with a derivation and not a round figure. 32 = four
// 74HC595: 19 LEDs (what FireflowHW draws today), four address lines, two
// enables, seven spare. Demand today is 67 pot positions (io-budget §3), so
// the 128 channels above are headroom, not a plan.
inline constexpr int kChainBits = 32;

inline constexpr int kAddrShift   = 0;  // four address lines, bits 0..3
inline constexpr int kEnableShift = 4;  // one active-low enable per group
inline constexpr int kLedShift    = 8;  // 19 LED bits from here up

struct StepPattern
{
    uint8_t address;      // 0..kMuxChannels-1
    uint8_t enable_mask;  // active low: exactly one group's bit is 0
};

StepPattern step_pattern(int step);

// The channel a sense pin carries during `step`, or -1 for an index that does
// not exist. Out of range gets an answer instead of an assumption: a
// half-seated chip produces steps nobody planned, and an access past the end
// would be a crash inside the audio callback.
int mux_channel(int step, int sense);

// The chain word for a step, with `leds` in the LED field.
uint32_t chain_word(StepPattern p, uint32_t leds);

} // namespace shell
```

- [ ] **Step 5: Write the implementation**

Create `shell/mux_plan.cpp`:

```cpp
#include "mux_plan.h"

namespace shell {

namespace {
constexpr uint8_t kAllOff = static_cast<uint8_t>((1u << kMuxGroups) - 1u);
}

StepPattern step_pattern(int step)
{
    // A step that does not exist parks the scan with every enable off. An
    // out-of-range address would still select SOME channel and hand back a
    // foreign knob's voltage, which is worse than reading nothing.
    if(step < 0 || step >= kScanSteps) return StepPattern{0, kAllOff};

    const int group = step / kMuxChannels;
    const int addr  = step % kMuxChannels;
    return StepPattern{static_cast<uint8_t>(addr),
                       static_cast<uint8_t>(kAllOff & ~(1u << group))};
}

int mux_channel(int step, int sense)
{
    if(step < 0 || step >= kScanSteps) return -1;
    if(sense < 0 || sense >= kSensePins) return -1;
    return step * kSensePins + sense;
}

uint32_t chain_word(StepPattern p, uint32_t leds)
{
    return (static_cast<uint32_t>(p.address & 0x0Fu) << kAddrShift)
           | (static_cast<uint32_t>(p.enable_mask & kAllOff) << kEnableShift)
           | (leds << kLedShift);
}

} // namespace shell
```

- [ ] **Step 6: Run the tests and watch them pass**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: green, including the six new `mux plan:` cases. Do not accept a subagent's word for this — run it.

- [ ] **Step 7: Commit**

```bash
git add shell/mux_plan.h shell/mux_plan.cpp tests/test_mux_plan.cpp CMakeLists.txt
git commit -m "shell: the scan's write side, tested before a mux exists

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The hardware half and the three-position switch

**Files:**
- Create: `shell/mux_scan.h`, `shell/mux_scan.cpp`
- Create: `shell/write_shell_mux_probe.py`
- Create: `shell/read_probe.py`
- Modify: `shell/Makefile` (switch validation, generated header, `CPP_SOURCES`)
- Modify: `shell/main.cpp` (include, callback placement, foreground pacing, print line)

**Interfaces:**
- Consumes: everything Task 2 produces.
- Produces: `shell::MuxScan` with `init()`, `step(bench::Board&)`, `steps() const -> uint32_t`; `shell::g_mux_values[kMuxTotal]`; the build switch `SHELL_MUX_PROBE` ∈ {0,1,2}; the probe line gains `steps=<n>`.

This task ends at "three images build". The board work is Task 4.

- [ ] **Step 1: The switch header writer**

Create `shell/write_shell_mux_probe.py`:

```python
"""Writes the mux-probe switch as a real header.

Same shape and same reason as write_shell_cpu_probe.py and
write_shell_selftest.py: a bare -D is invisible to make's dependency graph,
and SHELL_MUX_PROBE is a variable that gets moved between THREE values. An
existing build/ would keep a stale main.o -- and in the worst case hand out
a measurement of the wrong placement under the right name.
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[2] not in {"0", "1", "2"}:
        raise SystemExit("usage: write_shell_mux_probe.py OUTPUT {0|1|2}")
    output = Path(sys.argv[1])
    content = "#define SHELL_MUX_PROBE %s\n" % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

Note the difference to the two existing writers: this one always defines the symbol, because `#if SHELL_MUX_PROBE == 1` needs it to exist in all three positions.

- [ ] **Step 2: The hardware half**

Create `shell/mux_scan.h`:

```cpp
#pragma once

// The hardware half of the panel scan. mux_plan.h holds everything that can
// be checked on the host; this file holds what only means something on a
// board: four bit-banged chain pins and four ADC reads.
//
// IT NEVER WAITS FOR THE MUX TO SETTLE. The address is clocked out at the end
// of one audio block and sampled at the start of the next, so the block period
// IS the settle window -- 2 ms at 96 samples / 48 kHz. A busy-wait settle
// inside the callback would be dead time charged to the audio budget for
// nothing. Whether 2 ms is actually enough is Phase-0 Task 6 step 5b's
// question and needs a mux on the table; this file is what gets priced first.
//
// One step per block means a full sweep of kScanSteps takes 32 blocks = 64 ms,
// i.e. ~15.6 Hz per channel with all eight chips populated, ~31 Hz with four.
// That is arithmetic, not a measurement, and it is the rate the CPU numbers
// belong to.
#include "hw/board.h"
#include "mux_plan.h"

namespace shell {

class MuxScan
{
  public:
    void init();

    // Read the four sense pins for the step written last time, then clock out
    // the next step.
    void step(bench::Board& hw);

    uint32_t steps() const { return steps_; }

  private:
    void write_chain(uint32_t word);

    daisy::GPIO data_, clock_, latch_, sense_in_;
    uint32_t    leds_      = 0;
    int         next_step_ = 0;
    int         live_step_ = -1;
    uint32_t    steps_     = 0;
};

// Where the scan puts what it read. Volatile so a build that does not apply
// the values still performs the reads -- the probe images deliberately do NOT
// push these into the engine, because the operating point has to stay the one
// the baseline image runs or the audio comparison compares two instruments.
extern volatile float g_mux_values[kMuxTotal];

} // namespace shell
```

Create `shell/mux_scan.cpp`:

```cpp
#include "mux_scan.h"

namespace shell {

volatile float g_mux_values[kMuxTotal] = {};

namespace {

// The production pins, not spare ones picked for a test: io-budget §3 spends
// B7, B8, D1 and D10 on the 595/165 chain and every remaining GPIO on the
// 4-bit SD slot. Measuring on other pins would price a different port.
constexpr daisy::Pin kData  = daisy::patch_sm::DaisyPatchSM::B7;
constexpr daisy::Pin kClock = daisy::patch_sm::DaisyPatchSM::B8;
constexpr daisy::Pin kLatch = daisy::patch_sm::DaisyPatchSM::D1;
constexpr daisy::Pin kIn    = daisy::patch_sm::DaisyPatchSM::D10;

// libDaisy's default, and it stays the default. Edge rate is an EMI
// parameter and this round measures whether the burst moves the block-rate
// tone -- so it moves one thing at a time. A faster setting is a separate
// measurement, not a footnote to this one.
constexpr daisy::GPIO::Speed kSpeed = daisy::GPIO::Speed::LOW;

} // namespace

void MuxScan::init()
{
    data_.Init(kData, daisy::GPIO::Mode::OUTPUT, daisy::GPIO::Pull::NOPULL,
               kSpeed);
    clock_.Init(kClock, daisy::GPIO::Mode::OUTPUT, daisy::GPIO::Pull::NOPULL,
                kSpeed);
    latch_.Init(kLatch, daisy::GPIO::Mode::OUTPUT, daisy::GPIO::Pull::NOPULL,
                kSpeed);
    // The 165 button chain's return line. Nothing drives it on a bare
    // submodule, hence the pull-up: a floating input is a current source and
    // a noise source, and this round is about noise.
    sense_in_.Init(kIn, daisy::GPIO::Mode::INPUT, daisy::GPIO::Pull::PULLUP);
}

void MuxScan::write_chain(uint32_t word)
{
    // MSB first, no delay between edges: at 480 MHz two register writes are
    // some nanoseconds apart, which a 74HC595 at 3V3 may or may not accept.
    // If a real chain needs a delay, THE COST MEASURED HERE IS OPTIMISTIC --
    // that belongs in the write-up, not in a comment nobody reads.
    for(int i = kChainBits - 1; i >= 0; --i)
    {
        data_.Write(((word >> i) & 1u) != 0u);
        clock_.Write(true);
        clock_.Write(false);
    }
    latch_.Write(true);
    latch_.Write(false);
}

void MuxScan::step(bench::Board& hw)
{
    if(live_step_ >= 0)
    {
        for(int s = 0; s < kSensePins; ++s)
        {
            const int ch = mux_channel(live_step_, s);
            if(ch >= 0)
                g_mux_values[ch]
                    = hw.GetAdcValue(daisy::patch_sm::CV_1 + s);
        }
    }

    // The LED field changes every step, as it does in production. A constant
    // word would let the compiler hoist the loop's work out of the hot path
    // and price a scan nobody will ship.
    leds_ = (leds_ + 1u) & 0x7FFFFu;

    const StepPattern p = step_pattern(next_step_);
    write_chain(chain_word(p, leds_));

    live_step_ = next_step_;
    next_step_ = (next_step_ + 1) % kScanSteps;
    ++steps_;
}

} // namespace shell
```

- [ ] **Step 3: Wire the switch into the Makefile**

In `shell/Makefile`, after the `SHELL_CPU_PROBE` block:

```make
# --- Mux-Sonde -----------------------------------------------------------
# Drei Stellungen, nicht zwei: 0 aus, 1 der Scan IM Audio-Callback, 2 der
# Scan im Vordergrund, getaktet auf einen Schritt pro Block. Die Frage
# dieser Runde ist genau der Unterschied zwischen 1 und 2 -- deshalb muss
# derselbe Arbeitsumfang an beiden Stellen laufen und nicht nur ungefaehr.
SHELL_MUX_PROBE ?= 0

ifneq ($(filter $(SHELL_MUX_PROBE),0 1 2),$(SHELL_MUX_PROBE))
$(error SHELL_MUX_PROBE must be 0, 1 or 2)
endif
```

Add both sources to `CPP_SOURCES` (right after `sdram_mem.cpp`):

```make
	mux_plan.cpp \
	mux_scan.cpp \
```

And the generated header, next to the two existing rules at the bottom:

```make
$(BUILD_DIR)/shell_mux_probe.h: FORCE | $(BUILD_DIR)
	@python write_shell_mux_probe.py $@ $(SHELL_MUX_PROBE)

$(BUILD_DIR)/main.o: $(BUILD_DIR)/shell_mux_probe.h
```

- [ ] **Step 4: Wire it into main.cpp**

Add next to the other two generated includes at the top:

```cpp
#include "shell_mux_probe.h"
```

After `static spky::Instrument inst;`:

```cpp
#if SHELL_MUX_PROBE
#include "mux_scan.h"
static shell::MuxScan g_mux;
namespace {
// Ticked by the callback, read by the foreground. The foreground variant has
// to do the SAME number of steps per unit time as the callback variant, or
// the two numbers are not comparable -- pacing it off the audio block is the
// only clock both share.
volatile uint32_t g_block_tick = 0;
}
#endif
```

Inside `AudioCallback`, in the `SHELL_CPU_PROBE` branch — the placement is the whole experiment, so it is spelled out:

```cpp
    g_meter.OnBlockStart();
    inst.process(in[0], in[1], out[0], out[1], size);
#if SHELL_MUX_PROBE == 1
    g_mux.step(hw);            // INSIDE the meter: that is the point
#endif
    g_meter.OnBlockEnd();

#if SHELL_MUX_PROBE == 2
    g_block_tick = g_block_tick + 1;   // outside the meter, on purpose
#endif
```

And in the normal (non-CPU-probe) path, after `inst.process(...)`:

```cpp
#if SHELL_MUX_PROBE == 1
    g_mux.step(hw);
#endif
#if SHELL_MUX_PROBE == 2
    g_block_tick = g_block_tick + 1;
#endif
```

In `main()`, after `inst.set_density(...)`:

```cpp
#if SHELL_MUX_PROBE
    g_mux.init();
#endif
```

Replace the CPU probe's wait loop with the paced foreground variant:

```cpp
    hw.StartAudio(AudioCallback);
#if SHELL_MUX_PROBE == 2
    uint32_t last_tick = 0;
    while(!g_probe_done)
    {
        const uint32_t t = g_block_tick;
        if(t != last_tick)
        {
            last_tick = t;
            g_mux.step(hw);
        }
    }
#else
    while(!g_probe_done) { }          // der Callback begrenzt sich selbst
#endif
    hw.StopAudio();
```

Extend the print line so a foreground scan that cannot keep up is visible instead of assumed:

```cpp
        hw.PrintLine("SHELL_CPU sr=%d block=%d blocks=%d avg=%d max=%d min=%d "
                     "mux=%d steps=%d hundredths_pct",
                     static_cast<int>(probe_sr), static_cast<int>(probe_bs),
                     static_cast<int>(g_probe_limit),
                     static_cast<int>(avg), static_cast<int>(mx),
                     static_cast<int>(mn),
                     static_cast<int>(SHELL_MUX_PROBE),
#if SHELL_MUX_PROBE
                     static_cast<int>(g_mux.steps()));
#else
                     0);
#endif
```

Finally, the non-probe `while(1) {}` at the end of `main()` has to pace the foreground scan too, or the audio image in position 2 scans nothing:

```cpp
#if SHELL_MUX_PROBE == 2
    uint32_t idle_tick = 0;
    while(1)
    {
        const uint32_t t = g_block_tick;
        if(t != idle_tick) { idle_tick = t; g_mux.step(hw); }
    }
#else
    while(1) {}
#endif
```

- [ ] **Step 5: The probe reader**

Create `shell/read_probe.py`:

```python
"""Reads one SHELL_CPU line off the board's USB CDC port.

The shell repeats its result every 500 ms forever and there is no handshake,
so this just listens until a line arrives. Find the port first:

    python -c "from serial.tools import list_ports; \
print([p.device for p in list_ports.comports()])"

Usage:
    python read_probe.py COM7 [timeout_seconds]
"""
import sys
import time

import serial


def main() -> int:
    if len(sys.argv) not in (2, 3):
        raise SystemExit("usage: read_probe.py PORT [timeout_seconds]")
    port = sys.argv[1]
    limit = float(sys.argv[2]) if len(sys.argv) == 3 else 30.0

    with serial.Serial(port, timeout=1.0) as ser:
        deadline = time.monotonic() + limit
        while time.monotonic() < deadline:
            line = ser.readline().decode("utf-8", "replace").strip()
            if line.startswith("SHELL_CPU"):
                print(line)
                return 0
    print("no SHELL_CPU line within %.0f s" % limit, file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 6: Build all three positions and confirm they differ**

Write the build loop to a scratchpad script rather than a `cd`-compound (`CLAUDE.md`, shell conventions), then run it:

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"
cd shell
for p in 0 1 2; do
  make -j8 SHELL_CPU_PROBE=1 SHELL_MUX_PROBE=$p images || exit 1
  cp build/shell-sram.bin build/shell-cpu-mux$p.bin
done
```

Expected: three images, all linking. Then confirm the switch actually reached the object file — the trap this whole generated-header construction exists for:

```bash
cmp build/shell-cpu-mux0.bin build/shell-cpu-mux1.bin && echo "IDENTICAL — the switch did not arrive"
```

Expected: `cmp` reports a difference. If the two images are byte-identical, stop and fix the dependency edge; every number after that point would be a measurement of the same firmware under three names.

Also check `build/shell.map` for the region usage — SRAM_EXEC was at 69.8 % and SRAM at 76.5 % on 2026-08-08, and FEED has landed since. If a region overflows, read the map and name the item; do not shrink buffers.

- [ ] **Step 7: Commit**

```bash
git add shell/mux_scan.h shell/mux_scan.cpp shell/write_shell_mux_probe.py \
        shell/read_probe.py shell/Makefile shell/main.cpp
git commit -m "shell: the scan, and the switch that says where it runs

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: What the scan costs, measured

**Files:**
- Create: `docs/bench/2026-08-2X-<githash>-shell-mux-placement.md` (started here, finished in Task 6)

Board work. Needs the Patch Submodule on USB, DFU mode via RESET then BOOT inside the two-second window.

- [ ] **Step 1: Baseline, on today's engine**

Flash `SHELL_CPU_PROBE=1 SHELL_MUX_PROBE=0` and read one line:

```bash
dfu-util -a 0 -s 0x90040000:leave -D shell/build/shell-cpu-mux0.bin
python shell/read_probe.py COM7
```

Record `sr`, `block`, `blocks`, `avg`, `max`, `min`, `mux=0`, `steps=0`, plus the git hash and the board serial. **Do not reuse the 2026-08-08 numbers as the baseline** — FEED and everything after it landed since; that row is history, not a comparison partner.

- [ ] **Step 2: Repeat it**

Same image, power-cycle, read again. Two runs is the standing bar for a bench capture and the reason drift is visible at all. If `max` moves more than a point between runs, note it — the difference this round is looking for may be that size.

- [ ] **Step 3: The callback variant**

Flash `shell-cpu-mux1.bin`, read twice. Expected shape: `steps` equals `blocks` exactly (one step per block by construction). A shortfall here means the scan is not running where it is supposed to.

- [ ] **Step 4: The foreground variant**

Flash `shell-cpu-mux2.bin`, read twice. Two numbers matter and they are different questions:

- `avg`/`max` should sit at the baseline, because the meter never sees the scan.
- `steps` must equal `blocks`. **If it does not, the foreground cannot keep up and the variant is dead** — that is the finding, and it is worth more than the percentage.

- [ ] **Step 5: Write the table**

Start `docs/bench/2026-08-2X-<githash>-shell-mux-placement.md` with the identity block a bench capture carries — board (`patch_sm`, serial), git hash, `-O3`, transport `usb`, two runs each — and this table:

```
| image | mux | avg % | max % | min % | steps | blocks |
```

Then the derived line, and only if the numbers support it: surcharge in points = `max(mux=1) - max(mux=0)`, against the 2.9 points of reserve on `instrument_worst_bbd_dtcm` (97.02–97.16 % pct_max, `docs/bench/2026-08-19-3def5d5-feed-axi-o2-patch_sm-usb.md`). Note the caveat in the same breath: that reserve row is an `-O2` bench image and the shell is `-O3`. The primary result is the shell-against-shell delta at identical optimization; the reserve only sizes the verdict.

- [ ] **Step 6: Commit the evidence**

```bash
git add docs/bench/
git commit -m "bench: what the chain scan costs, and where

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: Whether the burst moves the block-rate tone

**Files:**
- Modify: `docs/bench/2026-08-2X-<githash>-shell-mux-placement.md`

This needs the submodule with the 3.5 mm jacks (`385138563330` on 2026-08-08, `shell/README.md`) and the UA interface. **The images here are built with `SHELL_CPU_PROBE=0`** — the CPU probe silences the output after five seconds and starts USB CDC, and both would be in the recording.

- [ ] **Step 1: Build the three audio images**

```bash
PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"
cd shell
for p in 0 1 2; do
  make -j8 SHELL_CPU_PROBE=0 SHELL_MUX_PROBE=$p images || exit 1
  cp build/shell-sram.bin build/shell-audio-mux$p.bin
done
```

- [ ] **Step 2: Record the baseline**

Flash `shell-audio-mux0.bin`, wait for the operating point to settle, then:

```bash
ffmpeg -f dshow -i audio="Line (Universal Audio Twin USB)" -t 10 -ac 2 -ar 48000 -y mux0.wav
python tools/blockrate_fft.py mux0.wav --block-rate 500
```

Do not touch the interface's input gain again for the rest of this task. The ratio is scale-invariant, the absolute level is not, and a mid-session gain change makes the two halves of the table incomparable.

- [ ] **Step 3: Record the callback variant**

Flash `shell-audio-mux1.bin`, same recording, same command with `mux1.wav`.

- [ ] **Step 4: Record the foreground variant**

Flash `shell-audio-mux2.bin`, same again with `mux2.wav`.

- [ ] **Step 5: Add the audio table**

```
| image | total RMS dBFS | 500 Hz dBFS | excess dB |
```

The 2026-08-08 reference row for the shape of the answer: total RMS −54.6 dBFS, 500 Hz −59.5 dBFS, excess −4.9 dB. It is a *reference*, not a baseline — different git hash, different engine, possibly different gain. The baseline is Step 2's own row.

Then write the verdict in one sentence, and write the limit next to it in the same paragraph: nothing is attached to the four pins, so an unchanged tone is a lower bound and not an acquittal, while a worsened one is decisive.

- [ ] **Step 6: Commit**

```bash
git add docs/bench/
git commit -m "bench: does the chain burst move the block-rate tone

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: The verdict, into the two documents that carry it

**Files:**
- Modify: `docs/hardware/io-budget.md` §6
- Modify: `docs/roadmap.md` (the 2026-08-16 entry under "M6 — Hardware prototype")
- Modify: `shell/README.md` ("Wo die naechste Arbeit hingeht")

- [ ] **Step 1: Replace the placeholder in io-budget §6**

The second bullet ("Der Mux-Scan selbst") and the "Drei Stellen, an denen es trotzdem klemmen kann" list both currently say the cost is unmeasured. Replace *only* what the run measured. Keep the settle time and the 8:1/16:1 choice open — this round did not touch either, and a document that quietly closes them would be worse than one that never opened them. `io-budget.md` is German; match it.

- [ ] **Step 2: Answer the placement question explicitly**

Whatever the numbers say, §6 gains a named answer to "callback or foreground", because the absence of that question is what made the item look bigger than it was. If foreground keeps up: say so, and demote the CPU-reserve worry from "die reale Gefahr" to what it actually is. If it does not: say why, with the `steps` shortfall as the evidence.

- [ ] **Step 3: Pull the verdict into the roadmap**

The 2026-08-16 paragraph ends "und seine CPU-Kosten sind ungemessen" / "its CPU cost is unmeasured". Replace that clause with the measured delta and a link to the bench capture. Do not restate the table; the roadmap points, the capture carries.

- [ ] **Step 4: Point shell/README.md at what happened**

Its last section still says `shell/controls.{h,cpp}` is where the next work goes and quotes **2,17 Punkte** reserve. Both are stale: the reserve is 2.9 points as of 2026-08-19, and the next work is this. Fix both in the same edit.

- [ ] **Step 5: Verify the docs against the capture**

Re-read the three edited passages next to the bench file and check every number appears in the capture. This project has shipped a status doc that kept calling a deleted feature done; a number that exists only in prose is the same failure one size smaller.

- [ ] **Step 6: Commit**

```bash
git add docs/hardware/io-budget.md docs/roadmap.md shell/README.md
git commit -m "docs: the scan has a price and a place

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## What happens after this plan

If the foreground variant keeps up and the tone does not move, the remaining M6 scan work is Phase-0 Task 6 step 5 and 5b — mux on the breadboard, settle time — and it can wait for the parts. If either fails, the next document to write is a spec for the co-controller on the control PCB, before the schematic is drawn and not after.
