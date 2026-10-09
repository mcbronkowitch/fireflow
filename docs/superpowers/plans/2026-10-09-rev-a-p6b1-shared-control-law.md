# Rev A P6b-1 — Shared Control Law Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** VCV's `Fireflow::pushParams()` becomes a Rack-free `control::ControlLawT` that VCV and the Rev A / coupon firmware both call, so every pot drives what its `FireflowHW` twin drives and the hardware boots with the init patch's mod depths.

**Architecture:** A new top-level `control/` holds a generated parameter header (`params.hpp`, namespace `ffctl`), the four already Rack-free state/MOD headers, and a header-only `ControlLawT<Inst, Hooks>` whose `tick()` is pushParams' body minus the Rack-bound lines. VCV copies `params[]` into a float vector and calls it; the firmware keeps its own vector, writes pots into it and calls it once per block. Room in SRAM_EXEC comes from moving boot-only code to QSPI first.

**Tech Stack:** C++17 (clang+Ninja desktop, ARM GCC Daisy, Rack SDK MinGW), doctest, Python 3 generators and guards.

**Spec:** `docs/superpowers/specs/2026-10-09-rev-a-p6b1-shared-control-law-design.md`

## Global Constraints

- Everything written into the repo is English. Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- Never prefix a shell command with `cd`; long compounds go into a script in the scratchpad.
- Desktop: `source env.sh` then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` — Release is mandatory. **ctest does not build**: always `cmake --build build` before `ctest --test-dir build --output-on-failure`.
- Firmware: never `source env.sh` in that shell. `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"` then `make -C shell -j8 <switches> images`.
- The three images: default; `SHELL_PANEL_SCAN=1` (Rev A); `SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1` (coupon). `cmp`/`md5sum` before flashing anything you intend to measure (1-second mtime trap).
- VCV: only `host/vcv/build-local.sh`; a task touching `host/vcv` ends with `host/vcv/build-local.sh install` and "restart Rack".
- Panel guards run as plain scripts from `host/vcv/`: `python res/test_panel.py`, `python res/test_hw_panel.py` (pytest is not installed).
- A test that cannot go red gets fixed: every new gate is proven RED once (sabotage, run, restore by re-applying the edit — never `git checkout <file>`, see memory `fireflow-scripted-edits-crlf`).
- No bit-exactness gates. Margin targets from the spec: **≥ 8 KB SRAM_EXEC free** on the Rev A image with the law linked; `tick()` **≤ 1 point = 9600 cycles** per call or the §6 rule applies.
- Out of scope (spec §1): depth editing, MOD latch, SHIFT, REC on hardware, LEDs, factory sample on hardware, CV/CLOCK/RESET on hardware, ROOT/WOBL parameters, the render host.

## Review Focus

1. **Order of setter calls inside the deck loop must not change.** `set_engine` → `set_excitation_sources` → factory autoload → sampler options → REC: the autoload sits between them. Task 4 keeps it there through `Hooks::after_engine`; Task 5 pins the order with a recorder test.
2. **A pot at either stop on a snapping parameter** (ENGINE at 5, STEPS at 16, FLUXRATE at 11, SCALE at 12, SONG at 13) must reach the top value, not one below. Task 8 pins `knob_from_pot(…, 1.0f)` for each.
3. **A knob value outside its range** (a modulated `mv()` result, or a firmware pot past the calibrated span) must clamp to the table range exactly as Rack's ParamQuantity did. Task 5 pins `mv()` clamping at both ends.
4. **The first control tick after boot** must not fire a SONG re-roll, a DRIFT settle or a BBD edge. `on_restore()` at firmware boot arms all three; Task 8 pins "first tick after `on_restore()` with init knobs fires nothing".
5. **Deck B through appended ids** (FILT/COLOR/LINK/PAN/STAGES/DEPTH/REC/FLUXRATE/FLUXFB): the moved code must keep the explicit `p ? X_B : X_A` reads. Task 5 drives deck B only and checks each lands on part 1.

---

### Task 0: Branch and baseline

**Files:** none.

- [ ] **Step 1:** `git status` must be clean on `main` (at or after `9a1f0803`). Create the branch: `git switch -c feat/p6b1-control-law`.
- [ ] **Step 2:** Desktop baseline: `source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build --output-on-failure -E "reva_route_guard|reva_fab_guard"`. Expected: all pass. (The two excluded guards take ~45 min and nothing here touches `hardware/`.)
- [ ] **Step 3:** Panel guard baseline, from `host/vcv/`: `python res/test_panel.py` — record the printed pass count; Task 4 must end with the same count plus the checks it adds.
- [ ] **Step 4:** Firmware baseline, in a shell **without** env.sh, run the scratchpad script that builds the three images (the one used on 2026-10-09: loop over `""`, `SHELL_PANEL_SCAN=1`, `SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1`; grep the memory table; `md5sum build/shell.elf build/*.bin`). Expected: Rev A SRAM_EXEC 261340 B.

---

### Task 1: Probe — does code execute from QSPI on this boot path?

The spec's memory plan (§5) rests on this. Board session with Bastian. Nothing from this task is kept except the Makefile change and the write-up line, **if** it passes.

**Files:**
- Modify: `shell/Makefile` (QSPI_SECTION, around line 491)
- Modify: `shell/panel_scan.cpp` (one attribute, probe only)
- Modify: `shell/README.md` (flash section, around lines 50–95)

- [ ] **Step 1: Ship text and bank in one QSPI image.** In `shell/Makefile` replace

```make
QSPI_SECTION = .qspiflash_data
```

with

```make
# Code placed in .qspiflash_text (spec 2026-10-09-rev-a-p6b1 section 5) sits in
# front of the wavetable bank at 0x90100000, so the two travel as one image:
# whenever cold code changes, shell-qspi.bin changes and must be flashed again.
QSPI_SECTIONS = .qspiflash_text .qspiflash_data
```

and change the three uses: `--remove-section=$(QSPI_SECTION)` → `$(foreach s,$(QSPI_SECTIONS),--remove-section=$(s))`, `--only-section=$(QSPI_SECTION)` → `$(foreach s,$(QSPI_SECTIONS),--only-section=$(s))`.

- [ ] **Step 2: Put one cold function there.** In `shell/panel_scan.cpp` change the definition line of `panel_scan_init` to

```cpp
__attribute__((section(".qspiflash_text"), noinline)) void panel_scan_init()
```

- [ ] **Step 3: Build and inspect.** Build the coupon image (`SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1`). Run `arm-none-eabi-nm -C shell/build/shell.elf | grep panel_scan_init` — expected address `0x9010xxxx`. `arm-none-eabi-size -A shell/build/shell.elf | grep qspiflash` — `.qspiflash_text` non-zero, `.qspiflash_data` 65024 B at a higher address than before.
- [ ] **Step 4: Board session (Bastian).** Flash both images to the coupon:

```bash
dfu-util -a 0 -s 0x90100000 -D shell/build/shell-qspi.bin
```

then (bootloader again)

```bash
dfu-util -a 0 -s 0x90040000:leave -D shell/build/shell-sram.bin
```

Expected: the serial log prints `SHELL_PLAY` lines, the three pots move sound, a WAVE deck sounds (proves the shifted bank is read correctly).

- [ ] **Step 5: Decision gate.**
  - **Boots and plays:** keep Step 1 and the README change (Step 6); **revert Step 2** (Task 7 places code by linker script, not by attribute). Continue.
  - **Hard fault / no boot / silent WAVE:** stop. Record what was seen in the roadmap's M6 entry. The memory route switches to the fallback of spec §5 (`-Os` on `control_law`'s TU is impossible for a header-only template; instead the firmware instantiates the law in its own `shell/control_tick.cpp` compiled `-Os`, as `mux_plan.o` is) plus `__attribute__((cold))` on boot-only shell functions. Ask Bastian before continuing.
- [ ] **Step 6:** In `shell/README.md`, the table of the two physical images: `shell-qspi.bin` now reads "cold code **and** the bank; flash to `0x90100000` whenever its md5 changed". Add the two dfu-util commands above.
- [ ] **Step 7: Commit** (Makefile + README only): `build(shell): the QSPI image carries .qspiflash_text in front of the bank`.

---

### Task 2: `control/params.hpp` — ids, ranges, mod layer, init values

**Files:**
- Modify: `host/vcv/res/gen_panel.py` (new `param_range()`, new `control_params_header()`, `__main__`)
- Create (generated): `control/params.hpp`
- Modify: `host/vcv/res/test_panel.py` (freshness check, `test_committed_artifacts_match_generator` at ~line 3770)
- Modify: `host/vcv/src/Fireflow.cpp:433-610` (`configControls()` reads ranges from the table)
- Create: `tests/test_control_params.cpp`
- Modify: `CMakeLists.txt` (add the test file; add `${CMAKE_SOURCE_DIR}` to `spky_tests` includes)

**Interfaces:**
- Produces: `namespace ffctl { enum ParamId {…, NUM_PARAMS}; static constexpr int PART_STRIDE; enum ModKind; struct ModTarget; static const ModTarget kModLayer[]; static constexpr float kInitParamDefaults[NUM_PARAMS]; struct ParamRange { float lo, hi; bool snap; }; static constexpr ParamRange kParamRange[NUM_PARAMS]; }` — ids numerically equal to `spkyvcv::ParamId` (same generator list `PARAMS`).

- [ ] **Step 1: Write the failing test** `tests/test_control_params.cpp`:

```cpp
// control/params.hpp is generated from the same PARAMS list as VCV's
// generated_panel.hpp. These checks hold the two copies together and pin the
// range table against what configControls() used to hard-code.
#include <doctest/doctest.h>
#include <cmath>
#include "control/params.hpp"
#include "vcv/src/generated_panel.hpp"
#include "vcv/src/init_patch.hpp"

TEST_CASE("params: ffctl ids are VCV's ids") {
    CHECK(ffctl::NUM_PARAMS == spkyvcv::NUM_PARAMS);
    CHECK(ffctl::PART_STRIDE == spkyvcv::PART_STRIDE);
    CHECK(ffctl::RATE_B == spkyvcv::RATE_B);
    CHECK(ffctl::PAN_B == spkyvcv::PAN_B);
    CHECK(ffctl::MODBTN == spkyvcv::MODBTN);
}

TEST_CASE("params: mod layer and init values are VCV's") {
    constexpr int n = sizeof(ffctl::kModLayer) / sizeof(ffctl::kModLayer[0]);
    REQUIRE(n == int(sizeof(spkyvcv::kModLayer) / sizeof(spkyvcv::kModLayer[0])));
    for (int i = 0; i < n; ++i) {
        CHECK(ffctl::kModLayer[i].soundId == spkyvcv::kModLayer[i].soundId);
        CHECK(ffctl::kModLayer[i].depthId == spkyvcv::kModLayer[i].depthId);
        CHECK(ffctl::kModLayer[i].kind == spkyvcv::kModLayer[i].kind);
        CHECK(ffctl::kModLayer[i].slot == spkyvcv::kModLayer[i].slot);
        CHECK(ffctl::kModLayer[i].part == spkyvcv::kModLayer[i].part);
    }
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i)
        CHECK(ffctl::kInitParamDefaults[i] == spkyvcv::kInitParamDefaults[i]);
}

TEST_CASE("params: every init value lies in its range") {
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i) {
        INFO("param " << i);
        CHECK(ffctl::kInitParamDefaults[i] >= ffctl::kParamRange[i].lo);
        CHECK(ffctl::kInitParamDefaults[i] <= ffctl::kParamRange[i].hi);
    }
}

TEST_CASE("params: ranges are configControls()'s") {
    using namespace ffctl;
    auto is = [](int id, float lo, float hi, bool snap) {
        INFO("param " << id);
        CHECK(kParamRange[id].lo == lo);
        CHECK(kParamRange[id].hi == hi);
        CHECK(kParamRange[id].snap == snap);
    };
    is(RATE_A, 0.f, 1.f, false);
    is(CHOKE, -1.f, 1.f, false);   is(PULL, -1.f, 1.f, false);
    is(FILT_B, -1.f, 1.f, false);  is(PAN_A, -1.f, 1.f, false);
    is(MELODY_A, -1.f, 1.f, false); is(GRIT_B, -1.f, 1.f, false);
    is(SCALE, 0.f, 12.f, true);    is(SONG_A, 0.f, 13.f, true);
    is(FLUXRATE_B, 0.f, 11.f, true); is(STEPS_A, 0.f, 16.f, true);
    is(ENGINE_B, 0.f, 5.f, true);  is(REC_A, 0.f, 1.f, true);
    is(MODBTN, 0.f, 1.f, true);    is(MODD_RATE_A, -1.f, 1.f, false);
    is(TEMPO, 0.f, 1.f, false);    is(DRIFT, 0.f, 1.f, false);
}
```

Add `tests/test_control_params.cpp` to `spky_tests` in `CMakeLists.txt` (after `tests/test_mod_layer.cpp`), and change `target_include_directories(spky_tests PRIVATE host)` to `target_include_directories(spky_tests PRIVATE host ${CMAKE_SOURCE_DIR})`.

- [ ] **Step 2: Run, expect FAIL** — `cmake --build build` fails: `control/params.hpp` not found.

- [ ] **Step 3: Generator.** In `host/vcv/res/gen_panel.py`, after `init_patch_header()`, add:

```python
# The parameter ranges configControls() passes to configParam/configSwitch,
# as data (spec 2026-10-09-rev-a-p6b1 section 3.1). The firmware needs them to
# turn a 0..1 pot into parameter units; VCV reads the same table.
_BIPOLAR_SMALL = {"CHOKE", "PULL", "FILT_A", "FILT_B", "PAN_A", "PAN_B"}
_KNOBI_TOP = {"SCALE": "float(spky::SCALE_LIST_COUNT - 1)",
              "SONG_A": "float(spky::kSongLadderCount - 1)",
              "SONG_B": "float(spky::kSongLadderCount - 1)",
              "FLUXRATE_A": "float(spky::kFluxRateCount - 1)",
              "FLUXRATE_B": "float(spky::kFluxRateCount - 1)",
              "STEPS_A": "16.f", "STEPS_B": "16.f"}

def param_range(c):
    """(lo, hi, snap) as C++ expressions, mirroring configControls()."""
    if c.enum == "MODBTN":
        return ("0.f", "1.f", True)
    if c.enum.startswith("MODD_"):
        return ("-1.f", "1.f", False)
    if c.kind in (BIGKNOB, SMKNOB):
        return ("-1.f", "1.f", False) if c.enum in _BIPOLAR_SMALL else ("0.f", "1.f", False)
    if c.kind == KNOBC:
        return ("-1.f", "1.f", False)
    if c.kind == KNOBI:
        return ("0.f", _KNOBI_TOP[c.enum], True)
    if c.kind == LATCH and c.enum in ("REC_A", "REC_B"):
        return ("0.f", "1.f", True)
    if c.kind == LATCH and c.enum in ("ENGINE_A", "ENGINE_B"):
        return ("0.f", "5.f", True)
    raise ValueError("no range rule for %s (%s)" % (c.enum, c.kind))

def control_params_header():
    L = ["// GENERATED by host/vcv/res/gen_panel.py -- do not edit by hand.",
         "// The parameter space the shared control law speaks (spec",
         "// docs/superpowers/specs/2026-10-09-rev-a-p6b1-shared-control-law-design.md",
         "// section 3.1). Same PARAMS list as host/vcv/src/generated_panel.hpp,",
         "// so every id is numerically VCV's; tests/test_control_params.cpp holds",
         "// the two together.",
         "#pragma once",
         '#include "pitch/quantizer.h"   // SCALE_LIST_COUNT',
         '#include "mod/song_ladder.h"   // kSongLadderCount',
         '#include "mod/divisions.h"     // kFluxRateCount',
         "", "namespace ffctl {", "", "enum ParamId {"]
    for c in PARAMS:
        L.append(f"    {c.enum},")
    L += ["    NUM_PARAMS", "};", "",
          f"static constexpr int PART_STRIDE = {PART_STRIDE};", "",
          "enum ModKind { MODK_TDEPTH = 0, MODK_FXDEPTH = 1, MODK_HOST = 2 };",
          "struct ModTarget { int soundId; int depthId; unsigned char kind; "
          "unsigned char slot; unsigned char part; };",
          "static const ModTarget kModLayer[] = {"]
    KINDMAP = {"TDEPTH": 0, "FXDEPTH": 1, "HOST": 2}
    for base, kind, slot, _init in MOD_DECK_TARGETS:
        for pi, sfx in enumerate(("_A", "_B")):
            L.append(f"    {{{base}{sfx}, MODD_{base}{sfx}, {KINDMAP[kind]}, {slot}, {pi}}},")
    for base, kind, slot, _init in MOD_CENTER_TARGETS:
        L.append(f"    {{{base}, MODD_{base}, {KINDMAP[kind]}, {slot}, 2}},")
    L += ["};", "", "static constexpr float kInitParamDefaults[] = {"]
    for c in PARAMS:
        L.append(f"    {_float_literal(INIT_DEFAULTS[c.enum])}f, // {c.enum}")
    L += ["};",
          "static_assert(sizeof(kInitParamDefaults) / sizeof(kInitParamDefaults[0]) == NUM_PARAMS,",
          '              "init snapshot must cover every ParamId");', "",
          "struct ParamRange { float lo, hi; bool snap; };",
          "static constexpr ParamRange kParamRange[] = {"]
    for c in PARAMS:
        lo, hi, snap = param_range(c)
        L.append(f"    {{{lo}, {hi}, {'true' if snap else 'false'}}}, // {c.enum}")
    L += ["};",
          "static_assert(sizeof(kParamRange) / sizeof(kParamRange[0]) == NUM_PARAMS,",
          '              "range table must cover every ParamId");', "",
          "} // namespace ffctl"]
    return "\n".join(L) + "\n"
```

In `__main__`, after the init_patch write:

```python
    repo = os.path.dirname(os.path.dirname(root))
    with open(os.path.join(repo, "control", "params.hpp"), "w", newline="\n") as f:
        f.write(control_params_header())
```

and extend the print line with `and control/params.hpp`. Create the folder `control/` first (Write tool, not a shell mkdir).

- [ ] **Step 4: Freshness guard.** In `test_panel.py`'s committed-artifacts test, add to the tuple list:

```python
            (os.path.join(root, "..", "..", "control", "params.hpp"),
             g.control_params_header()),
```

- [ ] **Step 5: VCV reads the table.** In `Fireflow.cpp` add `#include "control/params.hpp"` next to `generated_panel.hpp`, and directly under the existing `static_assert(NUM_PARAMS == MODBTN + 51, …)` add:

```cpp
static_assert((int)ffctl::NUM_PARAMS == (int)NUM_PARAMS, "control/params.hpp is stale");
static_assert((int)ffctl::MODBTN == (int)MODBTN, "control/params.hpp is stale");
```

In `configControls()`, replace **every** literal range pair in a `configParam`/`configSwitch` call for a `kParamCtls` id with `rangeOf(c.id).lo, rangeOf(c.id).hi` where `rangeOf(id)` is a file-local helper

```cpp
static const ffctl::ParamRange& rangeOf(int id) { return ffctl::kParamRange[id]; }
```

and the two calls after the loop (`MODBTN`, `t.depthId`) likewise. Keep every Quantity type, label and `snapEnabled` line exactly as they are. VCV needs `-I$(REPO)` for the include: in `host/vcv/Makefile` change `FLAGS += -I$(REPO)/engine -I$(REPO)/third_party -I$(REPO)/host` to append ` -I$(REPO)`.

- [ ] **Step 6: Generate and run.** From `host/vcv/`: `python res/gen_panel.py`, then `python res/test_panel.py` (expected: baseline count + 0 failures; a check that pinned a literal range string in `configControls` gets its needle updated to the new text — never deleted). From the root: `cmake --build build && ctest --test-dir build -R spky_tests --output-on-failure`. Expected: PASS.
- [ ] **Step 7: RED proof.** Change `"STEPS_A": "16.f"` to `"15.f"` in `_KNOBI_TOP`, regenerate, rebuild: `params: ranges are configControls()'s` fails. Restore, regenerate. Then edit one byte of `control/params.hpp` by hand: `python res/test_panel.py` reports it stale. Restore by regenerating.
- [ ] **Step 8: VCV build.** `host/vcv/build-local.sh` — expected: builds.
- [ ] **Step 9: Commit:** `feat(control): generated params.hpp -- ids, ranges, mod layer, init values in one table`.

---

### Task 3: Move the Rack-free headers into `control/`

**Files:**
- Move: `host/vcv/src/{mod_layer,bbd_edge_state,drift_settle_state,song_rung_state}.hpp` → `control/`
- Modify: includes in `host/vcv/src/Fireflow.cpp:17-21`, `tests/test_mod_layer.cpp:2`, `tests/test_bbd_edge_state.cpp:3`, `tests/test_drift_settle_state.cpp:3`, `tests/test_song_rung_state.cpp:3`
- Modify: `shell/Makefile:35` (`C_INCLUDES` gains `-I../`)

- [ ] **Step 1:** `git mv` the four files. Namespaces (`spkyvcv`, `spkymod`) stay — renaming is churn this spec does not need.
- [ ] **Step 2:** Includes become `#include "control/mod_layer.hpp"` (etc.) in the five files above. In `shell/Makefile` line 35 append ` -I../` so `control/…` resolves from `shell/` too.
- [ ] **Step 3:** Run `cmake --build build && ctest --test-dir build -R spky_tests`, `python res/test_panel.py` from `host/vcv/` (the check `"song_rung_state.hpp" in cpp` still holds: the include line keeps the file name), `host/vcv/build-local.sh`. Expected: all pass, same counts.
- [ ] **Step 4: Commit:** `refactor(control): the four Rack-free state and MOD headers move to control/`.

---

### Task 4: Extract `ControlLawT` — a move, plus the VCV adapter

This commit must read as a **move** under `git diff --color-moved=zebra`: the body of pushParams lands in `control/control_law.h` with only the substitutions listed below.

**Files:**
- Create: `control/control_law.h`
- Modify: `host/vcv/src/Fireflow.cpp` (members `:380-389`, `:397-404`; `mv/mvp/pp/ppb` `:683-755`; `kGritDead` `:763`; `pushParams` `:765-1263`; `kCoupleZoneSplit` `:41`; `onReset` `:1314-1336`; restore path `:1512-1520`)
- Modify: `host/vcv/res/test_panel.py` (source loading for the scrape checks)
- Modify: `bench/audition/init_patch.cpp` only if a mirrored-constant check now points at the moved constant (see Step 5)

**Interfaces:**
- Consumes: `ffctl::*` (Task 2), `control/*.hpp` (Task 3).
- Produces:

```cpp
namespace control {
struct DeckOptions { int tape_idx = 1; bool reverse = false; float feedback = 0.95f;
                     bool test_tone = false; bool excite_tape = true,
                     excite_other_deck = false, excite_audio_in = false; };
struct Options { DeckOptions deck[2]; float measured_bpm = 0.f; };
struct Events  { bool bbd_edge[2] = {}; bool rec_started[2] = {}; };
struct NoHooks { template <class Inst> void after_engine(int, int, Inst&) {} };
inline constexpr float kCoupleZoneSplit = 0.5f;
inline constexpr float kGritDead = 0.03f;
template <class Inst, class Hooks = NoHooks> class ControlLawT {
public:
    ControlLawT();
    void   on_reset();    // song rung + drift settle
    void   on_restore();  // + bbd edge
    Events tick(const float* knobs, const Options& opt, Inst& inst, Hooks& hooks);
    Events tick(const float* knobs, const Options& opt, Inst& inst);  // NoHooks
};
}
```

- [ ] **Step 1: Write `control/control_law.h`.** Skeleton (complete except the moved body):

```cpp
#pragma once
// The control law both hosts run (spec docs/superpowers/specs/
// 2026-10-09-rev-a-p6b1-shared-control-law-design.md). This is what was
// Fireflow::pushParams(): knob values in parameter units in, Instrument setter
// calls out. No Rack type, no hardware type, no file I/O. Templated on the
// instrument so tests/test_control_law.cpp can run it on a recorder; both
// hosts instantiate it with spky::Instrument.
#include <algorithm>
#include <cmath>
#include "instrument.h"
#include "mod/song_ladder.h"
#include "control/params.hpp"
#include "control/mod_layer.hpp"
#include "control/bbd_edge_state.hpp"
#include "control/song_rung_state.hpp"
#include "control/drift_settle_state.hpp"

namespace control {

// (moved from Fireflow.cpp:33-41, comment and all)
inline constexpr float kCoupleZoneSplit = 0.5f;
// (moved from Fireflow.cpp:757-763, comment and all)
inline constexpr float kGritDead = 0.03f;

struct DeckOptions { /* as in Interfaces above, one comment per field naming
                        the SamplerPartState field it mirrors */ };
struct Options { DeckOptions deck[2]; float measured_bpm = 0.f; };
struct Events  { bool bbd_edge[2] = {}; bool rec_started[2] = {}; };
struct NoHooks { template <class Inst> void after_engine(int, int, Inst&) {} };

template <class Inst, class Hooks = NoHooks>
class ControlLawT {
public:
    ControlLawT() {
        for (int i = 0; i < ffctl::NUM_PARAMS; ++i) _modIdxBySound[i] = -1;
        for (size_t i = 0; i < sizeof(ffctl::kModLayer) / sizeof(ffctl::kModLayer[0]); ++i)
            _modIdxBySound[ffctl::kModLayer[i].soundId] = (int)i;
    }
    void on_reset()   { for (auto& s : _songRung) s.rearm(); _driftSettled.rearm(); }
    void on_restore() { for (auto& b : _bbdEdge) b.rearm(); on_reset(); }

    Events tick(const float* knobs, const Options& opt, Inst& inst) {
        NoHooks h; return _tick(knobs, opt, inst, h);
    }
    Events tick(const float* knobs, const Options& opt, Inst& inst, Hooks& hooks) {
        return _tick(knobs, opt, inst, hooks);
    }

private:
    const float* _k = nullptr;
    float _laneOut[spky::PART_COUNT][spky::LANE_COUNT] = {};
    float _laneOutStepped[spky::PART_COUNT][spky::LANE_COUNT] = {};
    float _modMaster[spky::PART_COUNT] = {};
    int   _modIdxBySound[ffctl::NUM_PARAMS];
    spkyvcv::BbdEdgeState     _bbdEdge[spky::PART_COUNT];
    spkyvcv::SongRungState    _songRung[spky::PART_COUNT];
    spkyvcv::DriftSettleState _driftSettled;

    float prm(int id) const { return _k[id]; }
    float pp(int baseA, int part) const { return _k[baseA + part * ffctl::PART_STRIDE]; }
    bool  ppb(int baseA, int part) const { return pp(baseA, part) > 0.5f; }
    float mv(int soundId) const;      // moved body, see substitutions
    float mvp(int baseA, int part) const { return mv(baseA + part * ffctl::PART_STRIDE); }
    template <class H> Events _tick(const float* knobs, const Options& opt, Inst& inst, H& hooks);
};

using ControlLaw = ControlLawT<spky::Instrument>;

} // namespace control
```

Then **move** (cut from Fireflow.cpp, paste) `mv()`'s body into `ControlLawT::mv` and pushParams' body (lines 766–1262) into `_tick`, bracketed by `_k = knobs; Events ev;` at the top and `return ev;` at the bottom, with `using namespace ffctl;` as the body's first line. Substitutions, and **only** these:

| Old (Fireflow.cpp) | New (control_law.h) |
|---|---|
| `params[X].getValue()` | `prm(X)` |
| `modMaster`, `laneOut`, `laneOutStepped`, `modIdxBySound`, `bbdEdge`, `songRung`, `driftSettled` | the same names with a leading `_` |
| `paramQuantities[soundId]` / `q->getMinValue()` / `q->getMaxValue()` (mv) | `ffctl::kParamRange[soundId].lo` / `.hi` |
| `smp[p].testTone` | `opt.deck[p].test_tone` |
| `smp[p].exciteTape / exciteOtherDeck / exciteAudioIn` | `opt.deck[p].excite_tape / excite_other_deck / excite_audio_in` |
| `smp[p].tapeIdx / reverse / feedback` | `opt.deck[p].tape_idx / reverse / feedback` |
| the whole factory-autoload `if` (`:941-949`) | `hooks.after_engine(p, eng, inst);` (same position) |
| inside the REC `if (wantRec) { smp[p].path.clear(); smp[p].factoryLoaded = false; }` | `if (wantRec) ev.rec_started[p] = true;` |
| inside the BBD-edge `if`: `params[…FLUX…].setValue(0.f);` and `smp[p].exciteOtherDeck = true;` | `ev.bbd_edge[p] = true;` (keep both comments, reworded "the host does …") |
| tempo `if (inputs[CLOCK].isConnected() && clkSamples > 1.f && curSr > 0.f) { float measured = 60.f * curSr / clkSamples; if (…) bpm = measured; }` | `if (opt.measured_bpm >= 20.f && opt.measured_bpm <= 400.f) bpm = opt.measured_bpm;` |
| `spkymod::` / `spky::` calls | unchanged |
| `kModLayer`, `MODK_*`, `ModTarget`, param ids | resolve to `ffctl::` through the `using` |

- [ ] **Step 2: VCV adapter.** In `Fireflow.cpp` delete the moved members and helpers; add

```cpp
#include "control/control_law.h"
…
    // The host side of control/control_law.h: what needs Rack or a file.
    struct VcvHooks {
        Fireflow* m;
        void after_engine(int p, int eng, spky::Instrument& inst) {
            // moved verbatim from pushParams: the factory autoload (Task 8)
            if (eng == 1 && !m->smp[p].testTone && inst.sampler_empty(p)
                && !m->factoryTried[p]) {
                m->factoryTried[p] = true;
                if (!m->factoryL.empty()) {
                    inst.load_sample(p, m->factoryL.data(), m->factoryR.data(),
                                     m->factoryL.size());
                    m->smp[p].factoryLoaded = true;
                }
            }
        }
    };
    control::ControlLawT<spky::Instrument, VcvHooks> law;
    float knobs[NUM_PARAMS] = {};

    void pushParams() {
        for (int i = 0; i < NUM_PARAMS; ++i) knobs[i] = params[i].getValue();
        control::Options opt;
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            opt.deck[p] = {smp[p].tapeIdx, smp[p].reverse, smp[p].feedback,
                           smp[p].testTone, smp[p].exciteTape,
                           smp[p].exciteOtherDeck, smp[p].exciteAudioIn};
        }
        if (inputs[CLOCK].isConnected() && clkSamples > 1.f && curSr > 0.f)
            opt.measured_bpm = 60.f * curSr / clkSamples;
        VcvHooks hooks{this};
        const control::Events ev = law.tick(knobs, opt, inst, hooks);
        for (int p = 0; p < spky::PART_COUNT; ++p) {
            if (ev.bbd_edge[p]) {            // spec 5.11 / 5.12, see control_law.h
                params[p ? FLUX_B : FLUX_A].setValue(0.f);
                smp[p].exciteOtherDeck = true;
            }
            if (ev.rec_started[p]) {         // the buffer no longer matches a source
                smp[p].path.clear();
                smp[p].factoryLoaded = false;
            }
        }
    }
```

`onReset` (`:1322-1335`): replace `songRung[p].rearm();` (inside the loop) and `driftSettled.rearm();` with a single `law.on_reset();` after the loop. Restore path (`:1515-1520`): replace the loop and `driftSettled.rearm();` with `law.on_restore();`. Tooltip quantities that used `kCoupleZoneSplit` now use `control::kCoupleZoneSplit`.

- [ ] **Step 3: Re-point the panel guards.** In `host/vcv/res/test_panel.py`, add near the top

```python
def host_source():
    """What Rack runs: Fireflow.cpp plus the control law it calls (spec
    2026-10-09-rev-a-p6b1). Source scrapes that used to read pushParams
    read both, so a moved line is still found where it now lives."""
    here = os.path.dirname(os.path.abspath(__file__))
    parts = [os.path.join(here, "..", "src", "Fireflow.cpp"),
             os.path.join(here, "..", "..", "..", "control", "control_law.h")]
    return "\n".join(open(p, encoding="utf-8").read() for p in parts)
```

Replace every `open(os.path.join(here, "..", "src", "Fireflow.cpp")…).read()` and every `with open(…Fireflow.cpp…) as f: cpp = f.read()` with `cpp = host_source()` (or `host_cpp = host_source()`). Replace every `cpp_scope(cpp, "void pushParams()")` with `cpp_scope(cpp, "Events _tick(")`. Then run `python res/test_panel.py`: for each remaining failure, the check pinned a text the substitution table changed (`params[X].getValue()` → `prm(X)`, `smp[p].…` → `opt.deck[p].…`); update its needle to the new text, run its mutation (if it has one) and confirm it still goes red. **No check is deleted.** The dead-zone/detune mirror checks against `bench/audition/init_patch.cpp` (`:1487-1590`) keep comparing values; only the host file they read changes. Expected end state: same pass count as Task 0 Step 3 plus Task 2's check.

- [ ] **Step 4: Build all hosts.** `cmake --build build && ctest --test-dir build -R "spky_tests|panel_guard|hw_panel_guard" --output-on-failure` and `host/vcv/build-local.sh`. Expected: pass, builds.
- [ ] **Step 5: Move check.** `git diff --color-moved=zebra --stat` and `git diff --color-moved=zebra -- host/vcv/src/Fireflow.cpp control/control_law.h | less -R`: everything outside the adapter, the substitution-table lines and the test_panel edits shows as moved. Paste the summary (moved/added/removed line counts) into the commit body.
- [ ] **Step 6: Commit:** `refactor(control): pushParams moves to control/control_law.h; VCV is an adapter over it`.

---

### Task 5: Law gates on a recorder

**Files:**
- Create: `tests/control_recorder.h`
- Create: `tests/test_control_law.cpp`
- Modify: `CMakeLists.txt` (add `tests/test_control_law.cpp`)

**Interfaces:**
- Consumes: `control::ControlLawT`, `control::Options`, `control::Events`, `ffctl::*`.

- [ ] **Step 1: The recorder** `tests/control_recorder.h`:

```cpp
#pragma once
// An Instrument stand-in for control/control_law.h: logs every setter call
// (name, deck, value) and answers the five getters the law reads.
#include <string>
#include <type_traits>
#include <vector>
#include "instrument.h"

struct Rec {
    struct Call { std::string fn; int p; float v; };
    std::vector<Call> calls;
    spky::EngineId eng[2] = {spky::ENGINE_SYNTH, spky::ENGINE_SYNTH};
    bool  recording[2] = {};
    bool  empty[2] = {true, true};
    float lane[2][spky::LANE_COUNT] = {};
    float laneStep[2][spky::LANE_COUNT] = {};

    template <class T> static float f(T v) {
        if constexpr (std::is_enum_v<T>) return float(int(v)); else return float(v);
    }
    void log(std::string fn, int p, float v) { calls.push_back({std::move(fn), p, v}); }
    // last value a setter got for deck p (p = -1: global), NaN if never called
    float last(const std::string& fn, int p = -1) const {
        for (auto it = calls.rbegin(); it != calls.rend(); ++it)
            if (it->fn == fn && it->p == p) return it->v;
        return std::nanf("");
    }
    int count(const std::string& fn) const {
        int n = 0; for (auto& c : calls) n += c.fn == fn; return n;
    }
    int index(const std::string& fn, int p) const {
        for (size_t i = 0; i < calls.size(); ++i)
            if (calls[i].fn == fn && calls[i].p == p) return int(i);
        return -1;
    }

    // getters
    float lane_output(int p, int s) const { return lane[p][s]; }
    float lane_output_stepped(int p, int s) const { return laneStep[p][s]; }
    spky::EngineId engine_id(int p) const { return eng[p]; }
    bool  sampler_is_recording(int p) const { return recording[p]; }
    bool  sampler_empty(int p) const { return empty[p]; }

#define REC_G(name) template <class T> void name(T v) { log(#name, -1, f(v)); }
#define REC_P(name) template <class T> void name(int p, T v) { log(#name, p, f(v)); }
#define REC_PS(name) template <class S, class T> void name(int p, S s, T v) \
        { log(std::string(#name) + "/" + std::to_string(int(s)), p, f(v)); }
    REC_G(set_shuffle) REC_G(set_morph) REC_G(set_sync) REC_G(set_couple)
    REC_G(set_drift) REC_G(set_tide) REC_G(set_choke) REC_G(set_pull)
    REC_G(set_reverb_size) REC_G(set_reverb_decay) REC_G(set_reverb_tone)
    REC_G(set_reverb_diffusion) REC_G(set_master_drive) REC_G(set_reverb_smear)
    REC_G(set_reverb_mod) REC_G(set_scale) REC_G(set_tempo_bpm) REC_G(set_pace)
    REC_P(set_rate) REC_P(set_shape) REC_P(set_density) REC_P(set_smooth)
    REC_P(set_range) REC_P(set_depth) REC_P(set_tune) REC_P(set_voice_attack)
    REC_P(set_voice_decay) REC_P(set_voice_resonance) REC_P(set_voice_filt)
    REC_P(set_color) REC_P(set_voice_sub) REC_P(set_voice_detune)
    REC_P(set_flux_mix) REC_P(set_flux_rate) REC_P(set_link) REC_P(set_part_level)
    REC_P(set_comp) REC_P(set_pan) REC_P(sampler_speed_mode) REC_P(sampler_reverse)
    REC_P(sampler_feedback) REC_P(sampler_overlap) REC_P(set_variation)
    REC_P(sampler_scan) REC_P(set_grit_mode) REC_P(set_grit_mix) REC_P(set_form)
    REC_P(set_song) REC_P(set_reverb_mix)
    REC_PS(set_fx_target_base) REC_PS(set_target_base) REC_PS(set_target_depth)
    REC_PS(set_fx_target_depth) REC_PS(set_fx_target_active)
    REC_PS(set_target_active) REC_PS(set_fx_on)
#undef REC_G
#undef REC_P
#undef REC_PS
    void set_engine(int p, spky::EngineId id) { eng[p] = id; log("set_engine", p, f(id)); }
    void sampler_record(int p, bool on) { recording[p] = on; log("sampler_record", p, on); }
    void set_step(int p, bool on, int steps) { log("set_step", p, on ? float(steps) : -1.f); }
    void set_excitation_sources(int p, bool a, bool b, bool c) {
        log("set_excitation_sources", p, float(a) + 2.f * b + 4.f * c);
    }
    void settle() { log("settle", -1, 1.f); }
    void new_phrase(int p) { log("new_phrase", p, 1.f); }
    void sampler_punch(int p) { log("sampler_punch", p, 1.f); }
};
```

If the build reports a setter the law calls that `Rec` lacks, add it in the matching macro line — that list is pushParams' complete call set as of `450eed36`.

- [ ] **Step 2: Write the gates** `tests/test_control_law.cpp`:

```cpp
#include <doctest/doctest.h>
#include <cmath>
#include "control/control_law.h"
#include "control_recorder.h"

using Law = control::ControlLawT<Rec>;
using namespace ffctl;

namespace {
struct Rig {
    Law law; Rec inst; float k[NUM_PARAMS];
    control::Options opt;
    Rig() { for (int i = 0; i < NUM_PARAMS; ++i) k[i] = kInitParamDefaults[i];
            law.on_restore(); }
    control::Events tick() { inst.calls.clear(); return law.tick(k, opt, inst); }
};
}

TEST_CASE("law: LVL/COMP split -- gain below 0.6, compressor above") {
    Rig r;
    r.k[COMP_A] = 0.3f; r.tick();
    CHECK(r.inst.last("set_part_level", 0) == doctest::Approx(0.5f));
    CHECK(r.inst.last("set_comp", 0) == 0.f);
    r.k[COMP_A] = 1.0f; r.tick();
    CHECK(r.inst.last("set_part_level", 0) == 1.f);
    CHECK(r.inst.last("set_comp", 0) == doctest::Approx(0.7f));
    r.k[COMP_A] = 0.8f; r.tick();
    CHECK(r.inst.last("set_comp", 0) == doctest::Approx(0.7f * std::pow(0.5f, 0.6f)));
}

TEST_CASE("law: GRIT dead zone, sign picks the mode; FLUX on above 1e-4") {
    Rig r;
    r.k[GRIT_B] = 0.02f; r.tick();
    CHECK(r.inst.last("set_grit_mix", 1) == 0.f);
    CHECK(r.inst.last("set_fx_on/" + std::to_string(int(spky::FxBlock::Grit)), 1) == 0.f);
    r.k[GRIT_B] = -0.515f; r.tick();
    CHECK(r.inst.last("set_grit_mix", 1) == doctest::Approx(0.5f));
    CHECK(r.inst.last("set_grit_mode", 1) == float(int(spky::GritMode::Reduce)));
    r.k[FLUX_A] = 5e-5f; r.tick();
    CHECK(r.inst.last("set_fx_on/" + std::to_string(int(spky::FxBlock::Flux)), 0) == 0.f);
    r.k[FLUX_A] = 0.2f; r.tick();
    CHECK(r.inst.last("set_fx_on/" + std::to_string(int(spky::FxBlock::Flux)), 0) == 1.f);
}

TEST_CASE("law: ENGINE remap, test tone only on the sampler slot") {
    Rig r;
    const float want[6] = {float(spky::ENGINE_SYNTH), float(spky::ENGINE_SAMPLER),
                           float(spky::ENGINE_WAVE), float(spky::ENGINE_BODY),
                           float(spky::ENGINE_BBD), float(spky::ENGINE_FEED)};
    for (int e = 0; e < 6; ++e) {
        r.k[ENGINE_A] = float(e); r.tick();
        CHECK(r.inst.last("set_engine", 0) == want[e]);
    }
    r.opt.deck[0].test_tone = true;
    r.k[ENGINE_A] = 1.f; r.tick();
    CHECK(r.inst.last("set_engine", 0) == float(spky::ENGINE_TEST_TONE));
    r.k[ENGINE_A] = 0.f; r.tick();
    CHECK(r.inst.last("set_engine", 0) == float(spky::ENGINE_SYNTH));
}

TEST_CASE("law: DETUNE squared, skipped on FEED; SPREAD raw on FEED") {
    // The init patch boots both decks on FEED (ENGINE = 5). DETUNE's branch
    // reads engine_id() BEFORE this tick's set_engine, so each tick sees the
    // engine the previous tick set.
    Rig r;
    r.k[ENGINE_A] = 0.f; r.k[ENGINE_B] = 0.f; r.tick();   // both decks now SYNTH
    r.k[DETUNE_A] = 0.5f; r.tick();
    CHECK(r.inst.last("set_voice_detune", 0) == doctest::Approx(0.25f));
    r.k[ENGINE_A] = 5.f; r.tick();                        // A becomes FEED
    r.tick();                                             // A reads FEED now
    CHECK(r.inst.count("set_voice_detune") == 1);         // deck B only
    CHECK(r.inst.last("set_target_base/" + std::to_string(int(spky::LANE_SIZE)), 0)
          == doctest::Approx(0.5f));
}

TEST_CASE("law: COUPLE zone split and DRIFT settle edge") {
    Rig r;
    r.k[COUPLE] = 0.25f; r.tick();
    CHECK(r.inst.last("set_sync") == 0.f);
    CHECK(r.inst.last("set_couple") == doctest::Approx(0.5f));
    r.k[COUPLE] = 0.75f; r.tick();
    CHECK(r.inst.last("set_sync") == 1.f);
    CHECK(r.inst.last("set_couple") == doctest::Approx(0.5f));
    r.k[DRIFT] = 0.5f; r.tick();
    CHECK(r.inst.count("settle") == 0);
    r.k[DRIFT] = 0.01f; r.tick();
    CHECK(r.inst.count("settle") == 1);
    CHECK(r.inst.last("set_drift") == 0.f);
    r.tick();
    CHECK(r.inst.count("settle") == 0);              // parked: no re-fire
}

TEST_CASE("law: TEMPO from the knob, CLOCK override only inside 20..400") {
    Rig r;
    r.k[TEMPO] = 0.5f; r.tick();
    CHECK(r.inst.last("set_tempo_bpm") == doctest::Approx(140.f));
    r.opt.measured_bpm = 90.f; r.tick();
    CHECK(r.inst.last("set_tempo_bpm") == doctest::Approx(90.f));
    r.opt.measured_bpm = 500.f; r.tick();
    CHECK(r.inst.last("set_tempo_bpm") == doctest::Approx(140.f));
}

TEST_CASE("law: mv() -- lane, mirror and centre terms, clamped to the range") {
    Rig r;
    for (int i = MODBTN + 1; i < NUM_PARAMS; ++i) r.k[i] = 0.f;   // all depths at noon
    r.k[MOD_A] = 1.f; r.k[MOD_B] = 1.f;
    r.k[RATE_A] = 0.5f; r.k[MODD_RATE_A] = 1.f;
    for (int s = 0; s < spky::LANE_COUNT; ++s) r.inst.lane[0][s] = 1.f;
    r.tick();
    CHECK(r.inst.last("set_rate", 0) > 0.5f);         // lane pushed it up
    CHECK(r.inst.last("set_rate", 0) <= 1.f);         // and the range holds it
    r.k[RATE_A] = 1.f; r.tick();
    CHECK(r.inst.last("set_rate", 0) == 1.f);         // clamp at hi
    // PAN_B mirrors deck A's lane, negated
    r.k[PAN_B] = 0.f; r.k[MODD_PAN_B] = 1.f; r.tick();
    CHECK(r.inst.last("set_pan", 1) < 0.f);
    // a depth at noon returns the knob exactly
    r.k[MODD_RATE_A] = 0.f; r.k[RATE_A] = 0.3f; r.tick();
    CHECK(r.inst.last("set_rate", 0) == 0.3f);
}

TEST_CASE("law: engine-backed depths go to the Part, host ones never do") {
    Rig r;
    r.tick();
    int tdepth = 0, fxdepth = 0;
    for (auto& c : r.inst.calls) {
        tdepth  += c.fn.rfind("set_target_depth/", 0) == 0;
        fxdepth += c.fn.rfind("set_fx_target_depth/", 0) == 0;
    }
    CHECK(tdepth == 6);     // SOURCE, DEPTH, FILT x 2 decks
    CHECK(fxdepth == 6);    // FLUX, FLUXFB, REV_MIX x 2 decks
}

TEST_CASE("law: BBD edge fires once per genuine entry, never on restore") {
    Rig r;
    r.inst.eng[0] = spky::ENGINE_BBD; r.k[ENGINE_A] = 4.f;
    CHECK_FALSE(r.tick().bbd_edge[0]);                // restore seeded it
    r.inst.eng[0] = spky::ENGINE_SYNTH; r.k[ENGINE_A] = 0.f; r.tick();
    r.inst.eng[0] = spky::ENGINE_BBD; r.k[ENGINE_A] = 4.f;
    CHECK(r.tick().bbd_edge[0]);
    CHECK_FALSE(r.tick().bbd_edge[0]);
}

TEST_CASE("law: SONG rung change re-rolls, the first tick after restore does not") {
    Rig r;
    r.tick();
    CHECK(r.inst.count("new_phrase") == 0);
    r.k[SONG_B] = r.k[SONG_B] < 6.f ? 10.f : 2.f; r.tick();
    CHECK(r.inst.last("new_phrase", 1) == 1.f);
}

TEST_CASE("law: on_reset re-arms song and drift but not the BBD edge") {
    Rig r;
    r.inst.eng[0] = spky::ENGINE_SYNTH; r.k[ENGINE_A] = 0.f; r.tick();
    r.law.on_reset();
    r.inst.eng[0] = spky::ENGINE_BBD; r.k[ENGINE_A] = 4.f;
    CHECK(r.tick().bbd_edge[0]);                      // still armed: fires
    r.inst.eng[0] = spky::ENGINE_SYNTH; r.k[ENGINE_A] = 0.f; r.tick();
    r.law.on_restore();
    r.inst.eng[0] = spky::ENGINE_BBD; r.k[ENGINE_A] = 4.f;
    CHECK_FALSE(r.tick().bbd_edge[0]);                // restore: adopts
}

TEST_CASE("law: REC starts only on a sampler deck and reports it once") {
    Rig r;
    r.k[REC_A] = 1.f; r.tick();
    CHECK(r.inst.count("sampler_record") == 0);       // synth deck: inert
    r.inst.eng[0] = spky::ENGINE_SAMPLER; r.k[ENGINE_A] = 1.f;
    CHECK(r.tick().rec_started[0]);
    CHECK_FALSE(r.tick().rec_started[0]);
}

TEST_CASE("law: autoload hook sits between excitation and sampler options") {
    struct H { int at = -1; Rec* rec;
               void after_engine(int p, int, Rec&) { if (p == 0) at = int(rec->calls.size()); } };
    control::ControlLawT<Rec, H> law; Rec inst; float k[NUM_PARAMS];
    for (int i = 0; i < NUM_PARAMS; ++i) k[i] = kInitParamDefaults[i];
    law.on_restore();
    H h{-1, &inst}; control::Options opt;
    law.tick(k, opt, inst, h);
    CHECK(h.at == inst.index("set_excitation_sources", 0) + 1);
    CHECK(h.at == inst.index("sampler_speed_mode", 0));
}

TEST_CASE("law: deck B's appended ids land on part 1") {
    Rig r;
    r.k[FILT_B] = -0.4f; r.k[COLOR_B] = 0.6f; r.k[LINK_B] = 0.7f;
    r.k[FLUXRATE_B] = 3.f; r.k[REV_MIX_B] = 0.45f; r.k[DEPTH_B] = 0.2f;
    r.tick();
    CHECK(r.inst.last("set_voice_filt", 1) == doctest::Approx(-0.4f));
    CHECK(r.inst.last("set_flux_rate", 1) == 3.f);
    CHECK(r.inst.last("set_reverb_mix", 1) == doctest::Approx(0.45f));
    CHECK(r.inst.last("set_target_base/" + std::to_string(int(spky::LANE_MOTION)), 1)
          == doctest::Approx(0.2f));
    CHECK(r.inst.last("set_voice_filt", 0) != doctest::Approx(-0.4f));
}

TEST_CASE("law: DeckOptions{} is a fresh VCV deck") {
    control::DeckOptions d;
    CHECK(d.tape_idx == 1); CHECK_FALSE(d.reverse);
    CHECK(d.feedback == doctest::Approx(0.95f)); CHECK_FALSE(d.test_tone);
    CHECK(d.excite_tape); CHECK_FALSE(d.excite_other_deck); CHECK_FALSE(d.excite_audio_in);
}
```

`MODD_RATE_A` and `MODD_PAN_B` exist because RATE and PAN are host-computed deck targets (`MOD_DECK_TARGETS`); confirm in `control/params.hpp`. The "all depths at noon" loop is valid because the 50 depths are appended after `MODBTN` (`static_assert(NUM_PARAMS == MODBTN + 51)` in Fireflow.cpp).

- [ ] **Step 3: Run** `cmake --build build && ctest --test-dir build -R spky_tests --output-on-failure`. Expected: all pass (the law already exists). Any failure is a wrong expectation **or** a moved-code bug: read the moved line before touching the test.
- [ ] **Step 4: RED proofs**, one at a time, each restored by re-applying the edit: `kLvlCompSplit` 0.6→0.5 (COMP test red); `kGritDead` 0.03→0 (GRIT test red); swap `smp`-derived `test_tone` branch to `false` (ENGINE test red); `on_reset()` also re-arming `_bbdEdge` (on_reset test red); move the `hooks.after_engine` line one statement down (hook-order test red); `measured_bpm` range check removed (TEMPO test red). Record the six in the commit body.
- [ ] **Step 5: Commit:** `test(control): the control law's gates on a recording instrument`.

---

### Task 6: Probe — does the law care how often it ticks?

Spec §4.3. Desktop only. The answer goes into `docs/engine-map.md`.

**Files:**
- Create: scratchpad `probe_tick_rate.cpp` (not committed)
- Modify: `docs/engine-map.md` (a new short section "Control-law tick rate")

- [ ] **Step 1: Write the probe** in the scratchpad (recipe: `docs/engine-map.md` §6):

```cpp
// Run the real Instrument through ControlLaw at two tick divisions over the
// same 20 s knob sweep; print per-second RMS of both and their difference,
// plus the end state of rate()/the setters' observable getters.
#include <cmath>
#include <cstdio>
#include <vector>
#include "instrument.h"
#include "control/control_law.h"
static std::vector<float> run(int div) {
    static spky::Instrument inst; inst.init(48000.f);
    control::ControlLaw law; law.on_restore();
    float k[ffctl::NUM_PARAMS];
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i) k[i] = ffctl::kInitParamDefaults[i];
    control::Options opt;
    std::vector<float> rms; double acc = 0;
    for (int n = 0; n < 48000 * 20; ++n) {
        const float t = n / 48000.f;
        k[ffctl::RATE_A] = 0.5f + 0.4f * std::sin(t * 0.7f);
        k[ffctl::DRIFT]  = t > 10.f && t < 10.5f ? 0.f : 0.6f;   // one settle
        k[ffctl::SONG_A] = t > 15.f ? 9.f : k[ffctl::SONG_A];     // one rung change
        if (n % div == 0) law.tick(k, opt, inst);
        float l = 0, r = 0; inst.process(nullptr, nullptr, &l, &r, 1);
        acc += l * l;
        if (n % 48000 == 47999) { rms.push_back(std::sqrt(acc / 48000)); acc = 0; }
    }
    return rms;
}
int main() {
    auto a = run(16), b = run(96);
    for (size_t i = 0; i < a.size(); ++i)
        std::printf("%2zu s  div16 %.5f  div96 %.5f  diff %.2f dB\n", i, a[i], b[i],
                    20 * std::log10((b[i] + 1e-9) / (a[i] + 1e-9)));
}
```

Compile with the env.sh clang, Release flags, linking the engine sources the way `docs/engine-map.md` §6 shows (0.4 s compile). If `Instrument` is too large for a static, keep it static as written (it is how other probes do it).

- [ ] **Step 2: Read the per-tick state** as well: in `control/*_state.hpp`, does any `tick()` count calls as time? (Expected from the source: they are edge detectors and a hysteresis; no time constant. Confirm by reading, quote the line.) In `engine/`, grep every setter `_tick` calls for per-call smoothing (`grep -n "_smooth\|glide\|slew" engine/instrument.h engine/parts/part.h`) and note any whose step size is per call rather than per sample.
- [ ] **Step 3: Decide.** If every second's diff is within the run-to-run noise of the same division (run `run(16)` twice with a different `init` seed if the engine is seeded; otherwise take ±0.5 dB) and no state counts ticks: write "rate-independent" with the table. If something is rate-dependent: add `float dt` to `tick()` (seconds since the last call) and scale that one thing; add a gate to Task 5's file; re-run the probe until both divisions agree.
- [ ] **Step 4: Write it up** in `docs/engine-map.md` under a new heading `## Control-law tick rate (2026-10-…)`: the table, the verdict, the probe's path in prose ("scratchpad probe, recipe §6"). Commit: `docs(engine-map): the control law at 16 vs 96 samples per tick`.

---

### Task 7: Boot-only code to QSPI, with a placement guard

Needs Task 1 to have passed. Builds room for Task 8.

**Files:**
- Create: `shell/qspi_cold.ld` (linker fragment)
- Create: `shell/qspi_placement.py` (checker) and `shell/test_qspi_placement.py` (its host test)
- Modify: `shell/Makefile` (link the fragment; run the checker after link)
- Modify: `CMakeLists.txt` (ctest entry for the checker's host test)

**Interfaces:**
- Produces: `python shell/qspi_placement.py <nm-output-file>` exits 1 and names each forbidden symbol found in QSPI (`0x9010_0000`–`0x907F_FFFF`), 0 otherwise. The forbidden list is in the script as `HOT_PATTERNS` (fnmatch on demangled names).

- [ ] **Step 1: Write the checker's test** `shell/test_qspi_placement.py`:

```python
"""Host test for qspi_placement.py: fed nm lines, it must refuse a hot
symbol in QSPI and accept a cold one. Plain script, exit code is the verdict."""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import qspi_placement as q

FAILS = []
def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond: FAILS.append(name)

cold = "90100010 00000040 T daisy::System::Init(daisy::System::Config const&)\n"
hot_in_qspi = "90100100 00000708 T spky::ModLane::follow(long, float, float)\n"
hot_in_sram = "2400a55c 00000708 T spky::ModLane::follow(long, float, float)\n"
check("cold symbol in QSPI passes", q.violations(cold) == [])
check("hot symbol in SRAM passes", q.violations(hot_in_sram) == [])
check("hot symbol in QSPI is refused",
      q.violations(hot_in_qspi) == ["spky::ModLane::follow(long, float, float)"])
check("engine switch path is hot",
      q.violations("90100200 00000010 T spky::FeedEngine::init(float)\n") != [])
check("the control law is hot",
      q.violations("90100300 00000010 T control::ControlLawT<spky::Instrument, control::NoHooks>::tick(float const*, control::Options const&, spky::Instrument&)\n") != [])
sys.exit(1 if FAILS else 0)
```

Add to `CMakeLists.txt` after `shell_panel_map_guard`:

```cmake
add_test(NAME shell_qspi_placement_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/shell/test_qspi_placement.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shell)
```

- [ ] **Step 2: Run, expect FAIL** (`ModuleNotFoundError: qspi_placement`).
- [ ] **Step 3: The checker** `shell/qspi_placement.py`:

```python
"""Refuses an image whose QSPI-resident code includes anything on the audio
path, the engine-switch path or the control tick (spec 2026-10-09-rev-a-p6b1
section 5). Input: `arm-none-eabi-nm -C -S` output. Code in QSPI runs
execute-in-place through the QSPI bus: fine at boot, not inside a 2 ms block."""
import fnmatch, sys

QSPI_LO, QSPI_HI = 0x90100000, 0x90800000
HOT_PATTERNS = [
    "spky::*::process*", "spky::*::tick*", "spky::*::follow*", "spky::*::_control_tick*",
    "spky::*Engine::*",          # every part engine: switched at runtime
    "spky::Part::*", "spky::Center::*", "spky::ModLane::*", "spky::SuperModulator::*",
    "spky::Instrument::set_*", "spky::Instrument::process*",
    "control::*", "spkymod::*", "spkyvcv::*",
    "shell::panel_scan_tick*", "shell::MuxScan::*", "shell::key_update*", "shell::pot_filter*",
    "*IRQHandler*", "*Callback*", "daisy::AudioHandle::*", "daisy::SaiHandle::*",
    "daisy::DmaHandle*", "HAL_DMA_*", "HAL_SAI_*",
]

def violations(nm_text):
    bad = []
    for line in nm_text.splitlines():
        parts = line.split(None, 3)
        if len(parts) < 4 or parts[2] not in ("T", "t", "W", "w"):
            continue
        addr = int(parts[0], 16)
        name = parts[3]
        if QSPI_LO <= addr < QSPI_HI and any(fnmatch.fnmatchcase(name, p) for p in HOT_PATTERNS):
            bad.append(name)
    return bad

if __name__ == "__main__":
    bad = violations(open(sys.argv[1], encoding="utf-8").read())
    for b in bad:
        print("QSPI placement: hot code in QSPI: " + b)
    sys.exit(1 if bad else 0)
```

- [ ] **Step 4: Run** `ctest --test-dir build -R shell_qspi_placement_guard --output-on-failure` (after `cmake -S . -B build` picks up the new test). Expected: PASS.
- [ ] **Step 5: The linker fragment** `shell/qspi_cold.ld`. Start with the families measured on 2026-10-09 (spec §5): HAL and daisy init, clock config, and `spky::Instrument::init`'s own body. Mangled-name globs on `-ffunction-sections` input sections:

```ld
/* Boot-only code executes in place from QSPI (spec 2026-10-09-rev-a-p6b1
   section 5). Every pattern here must be code that runs before the audio
   callback starts; shell/qspi_placement.py refuses the link otherwise. */
SECTIONS
{
  .qspi_cold :
  {
    . = ALIGN(4);
    *(.text.HAL_*Init) *(.text.HAL_*Init_*) *(.text.HAL_*MspInit)
    *(.text.HAL_RCC*Config) *(.text.HAL_RCCEx_*Config) *(.text.HAL_PWREx_*Config)
    *(.text._ZN5daisy*4InitE*) *(.text._ZN5daisy*4InitEv)
    *(.text._ZN4spky10Instrument4initE*)
    . = ALIGN(4);
  } > QSPIFLASH
}
INSERT BEFORE .text;
```

In `shell/Makefile`, after the libDaisy include, add `LDFLAGS += -Wl,-T,qspi_cold.ld` (if libDaisy's link line passes `-T` for the main script itself, put the fragment after it: `LDSCRIPT_EXTRA`; check `lib/libDaisy/core/Makefile` for how `LDSCRIPT` is passed and append the fragment the same way). Add the section to `QSPI_SECTIONS`: `.qspi_cold .qspiflash_text .qspiflash_data`. Add a post-link guard to the `images` recipe chain:

```make
$(BUILD_DIR)/$(TARGET).nm: $(BUILD_DIR)/$(TARGET).elf FORCE
	arm-none-eabi-nm -C -S $< > $@
	python qspi_placement.py $@
images: $(BUILD_DIR)/$(TARGET).nm
```

- [ ] **Step 6: Build the three images.** Expected: link OK, the checker prints nothing. Read SRAM_EXEC for the Rev A image and `.qspi_cold`'s size; write both down.
- [ ] **Step 7: Grow the list by measurement, not by name.** For each further family the 2026-10-09 heuristic flagged (`init|Init|MspInit|Config|Setup|setup|configure`, 47 296 B; USB 13.4 KB), take it only if `arm-none-eabi-objdump -d shell/build/shell.elf` shows **no** call to it from a function the checker would call hot. Add pattern, rebuild, re-check. Stop when the Rev A image has **≥ 8 KB + the law's measured size** free — the law's size is `nm -S` of `control::ControlLawT*` after Task 8; until then aim for ≥ 16 KB free.
- [ ] **Step 8: RED proof:** add `*(.text._ZN4spky7ModLane6followE*)` to the fragment; rebuild; the link step fails with "hot code in QSPI: spky::ModLane::follow…". Remove it.
- [ ] **Step 9: Board check (Bastian):** flash both images to the coupon; it boots and plays. Then the cost check: build `SHELL_CPU_PROBE=1` once from this commit and once from Task 0's tree (`git worktree add` a scratch checkout; `cmp` the two pairs of images so you know they differ), flash each in turn and read `instrument_worst`. Expected: within ±0.5 points — boot-only code in QSPI must not touch the audio path. More than that means a hot function slipped into the fragment: find it with `nm`, add its pattern to `HOT_PATTERNS` (the checker goes red), drop it from the fragment.
- [ ] **Step 10: Commit:** `build(shell): boot-only code runs from QSPI; the link refuses hot code there`.

---

### Task 8: The firmware runs the law

**Files:**
- Modify: `shell/gen_panel_map.py` (classification → ffctl ids), `shell/test_gen_panel_map.py` (sabotages), regenerate `shell/generated_panel_map.h`
- Modify: `shell/controls.h`, `shell/controls.cpp` (drop `apply_control`, `control_value`; `param` is an `ffctl::ParamId`; add `knob_from_pot`)
- Modify: `shell/panel_scan.cpp` (knob vector, `on_restore()` at init, `tick()` per block, law cycles)
- Modify: `tests/test_controls_map.cpp`
- Modify: `shell/README.md` (known divergences, spec §8)

**Interfaces:**
- Consumes: `control::ControlLaw`, `ffctl::kParamRange`, `ffctl::kInitParamDefaults`.
- Produces: `float shell::knob_from_pot(int param, float v)` — `lo + v·(hi−lo)`, rounded when `snap`; `ControlEntry::param` holds an `ffctl::ParamId` or −1.

- [ ] **Step 1: Failing tests** — in `tests/test_controls_map.cpp`, replace the `spky::P_*` expectations and the two apply/scale cases with:

```cpp
TEST_CASE("controls: the coupon table maps RV2, RV4 and RV6") {
    const shell::ControlTable& t = shell::kCouponTable;
    REQUIRE(t.count == 3);
    CHECK(t.entries[0].param == ffctl::RATE_A);
    CHECK(t.entries[1].param == ffctl::DENSITY_A);
    CHECK(t.entries[2].param == ffctl::FILT_A);
}

TEST_CASE("controls: a pot becomes a knob in parameter units") {
    CHECK(shell::knob_from_pot(ffctl::RATE_A, 0.25f) == doctest::Approx(0.25f));
    CHECK(shell::knob_from_pot(ffctl::FILT_A, 0.0f) == doctest::Approx(-1.0f));
    CHECK(shell::knob_from_pot(ffctl::FILT_A, 1.0f) == doctest::Approx(1.0f));
    CHECK(shell::knob_from_pot(-1, 0.5f) == 0.0f);
}

TEST_CASE("controls: snapping pots reach both stops") {
    CHECK(shell::knob_from_pot(ffctl::ENGINE_A, 1.0f) == 5.f);
    CHECK(shell::knob_from_pot(ffctl::STEPS_B, 1.0f) == 16.f);
    CHECK(shell::knob_from_pot(ffctl::FLUXRATE_A, 1.0f) == 11.f);
    CHECK(shell::knob_from_pot(ffctl::SCALE, 1.0f) == 12.f);
    CHECK(shell::knob_from_pot(ffctl::SONG_B, 1.0f) == 13.f);
    CHECK(shell::knob_from_pot(ffctl::ENGINE_A, 0.0f) == 0.f);
    CHECK(shell::knob_from_pot(ffctl::ENGINE_A, 0.55f) == 3.f);   // 2.75 rounds up
}

TEST_CASE("controls: only the three reserved Rev A pots send nothing") {
    int none = 0;
    for (int i = 0; i < shell::kRevaTable.count; ++i)
        none += shell::kRevaTable.entries[i].param < 0;
    CHECK(shell::kRevaTable.count == 73);
    CHECK(none == 3);
}

TEST_CASE("controls: the first tick after on_restore with init knobs fires nothing") {
    spky::Instrument inst; inst.init(48000.f);
    control::ControlLaw law; law.on_restore();
    float k[ffctl::NUM_PARAMS];
    for (int i = 0; i < ffctl::NUM_PARAMS; ++i) k[i] = ffctl::kInitParamDefaults[i];
    const control::Events ev = law.tick(k, control::Options{}, inst);
    CHECK_FALSE(ev.bbd_edge[0]); CHECK_FALSE(ev.bbd_edge[1]);
    CHECK_FALSE(ev.rec_started[0]); CHECK_FALSE(ev.rec_started[1]);
}
```

Delete the old "sends exactly the 35 safe parameters" case and the "entry without a parameter is refused" case (both test `apply_control`, which goes). Add `#include "control/control_law.h"` at the top. Keep every other case.

- [ ] **Step 2: Run, expect FAIL** (compile errors: `ffctl` ids in `ControlEntry`, no `knob_from_pot`).
- [ ] **Step 3: `controls.h/.cpp`.** Replace `control_value`/`apply_control` with:

```cpp
// lo + v * (hi - lo) of the parameter's range in control/params.hpp, rounded
// for a parameter Rack snaps (ENGINE, STEPS, FLUXRATE, SCALE, SONG ...), or 0
// for no parameter. This is the only thing the firmware does to a pot before
// the shared control law sees it (spec 2026-10-09-rev-a-p6b1 section 4.2).
float knob_from_pot(int param, float v);
```

```cpp
float knob_from_pot(int param, float v)
{
    if(param < 0 || param >= ffctl::NUM_PARAMS) return 0.0f;
    const ffctl::ParamRange& r = ffctl::kParamRange[param];
    const float x = r.lo + v * (r.hi - r.lo);
    return r.snap ? std::round(x) : x;
}
```

`controls.h` includes `control/params.hpp` instead of `param_table.h`; `kCouponControls` uses `ffctl::RATE_A`, `ffctl::DENSITY_A`, `ffctl::FILT_A`; the header comment's "routes it via apply_param()" sentence is replaced by the knob_from_pot one.

- [ ] **Step 4: Generator.** In `shell/gen_panel_map.py`: delete `_PER_DECK_SAFE`, `_GLOBAL_SAFE`, `_PER_DECK_UNMAPPED`, `_GLOBAL_UNMAPPED`, `SAFE`, `UNMAPPED` and their checks; read the id names from `control/params.hpp` (`re.findall(r"^\s+(\w+),$", text[enum_start:enum_end], re.M)` between `enum ParamId {` and `NUM_PARAMS`); classification becomes: reserved → `-1`, otherwise the pot id must be an ffctl id (`GenError("%s: no ffctl id in control/params.hpp")`) and the row gets `ffctl::<id>` with the comment `-- <id>`. Header comment line 3 names `control/params.hpp` instead of `engine/param_table.h`. In `test_gen_panel_map.py`, drop the sabotages that name SAFE/UNMAPPED (`pot unclassified`, `pot in both lists`, `reserved pot also SAFE/UNMAPPED`, `unknown ParamId`, `classified name that is no pot`) and add:

```python
sabotage("pot with no ffctl id",
         lambda i: i["params"].discard("RATE_A"), "RATE_A: no ffctl id")
sabotage("reserved pot given an id",
         lambda i: i["reserved"].pop("ROOT_A"), "ROOT_A: no ffctl id")
```

Regenerate: `python shell/gen_panel_map.py` (writes `generated_panel_map.h`); run `python shell/test_gen_panel_map.py`. Expected: PASS, two new sabotages red-then-restored by construction.

- [ ] **Step 5: `panel_scan.cpp`.** Add `#include "control/control_law.h"` and `#include "cycles.h"`; in the anonymous namespace:

```cpp
// The knob vector the shared control law reads (spec 2026-10-09-rev-a-p6b1
// section 4.2): every parameter in VCV's units, booted from the init patch --
// depths included, which P6b-1 does not let the panel edit yet.
float               g_knobs[ffctl::NUM_PARAMS];
control::ControlLaw g_law;
volatile uint32_t   g_law_cycles_max = 0;
volatile uint32_t   g_law_cycles_last = 0;
```

In `panel_scan_init()`: copy `ffctl::kInitParamDefaults` into `g_knobs`; `g_law.on_restore();`.
In `panel_scan_tick()`: the inner `if(pot_filter(…))` body becomes

```cpp
            g_knobs[e->param] = knob_from_pot(e->param, v);   // e->param >= 0 here
            g_value[idx] = v;
```

with the `find_control` guard extended to skip `e->param < 0` rows **for the knob write only** (keep `g_value` reporting for reserved rows: write `g_value[idx] = v` before the param check). After the sense-pin loop and before `if(step == kSteps - 1)`:

```cpp
    // Once per block, the whole vector (spec section 4.2). Events are not
    // applied: a physical FLUX pot cannot be turned back (spec section 8).
    const uint32_t c0 = cycles_now();
    (void)g_law.tick(g_knobs, control::Options{}, inst);
    const uint32_t dc = cycles_now() - c0;
    g_law_cycles_last = dc;
    if(dc > g_law_cycles_max) g_law_cycles_max = dc;
```

In `run_panel_scan_report`, add one line per report cycle (both images):

```cpp
        hw.PrintLine("SHELL_PLAY_LAW cyc_last=%d cyc_max=%d",
                     static_cast<int>(g_law_cycles_last),
                     static_cast<int>(g_law_cycles_max));
```

Make sure `cycles_init()` runs in this image (main.cpp line 72's condition already lists `SHELL_PANEL_SCAN`; confirm).

- [ ] **Step 6: Desktop run:** `cmake --build build && ctest --test-dir build -R "spky_tests|shell_panel_map_guard" --output-on-failure`. Expected: PASS.
- [ ] **Step 7: Firmware build**, three images. Expected: all link, `qspi_placement.py` silent. Read Rev A SRAM_EXEC: **≥ 8192 B free** (spec §5). If not: Task 7 Step 7 again, then the `-Os` fallback (instantiate the law in `shell/control_tick.cpp` with `$(BUILD_DIR)/control_tick.o: override OPT := -Os`, as `mux_plan.o`).
- [ ] **Step 8: RED proof** for the stops test: make `knob_from_pot` truncate (`std::floor(x)`) — "snapping pots reach both stops" goes red at ENGINE 0.55. Restore.
- [ ] **Step 9: README.** `shell/README.md`: the "Reading it" paragraph gains the `SHELL_PLAY_LAW` line; a new short "Known divergences from VCV (P6b-1)" list copied from spec §8.
- [ ] **Step 10: Commit:** `feat(shell): every pot drives the shared control law; depths boot at the init patch`.

---

### Task 9: Coupon session — boot, three pots, the law's cost

Board session with Bastian; spec §7.4.

**Files:**
- Modify: `docs/roadmap.md` (M6 entry of the day), `shell/README.md` (result line)

- [ ] **Step 1:** Build the coupon image; `md5sum shell/build/shell-qspi.bin` against the last flashed copy — reflash the QSPI image only if it differs. Flash both as in Task 1 Step 4.
- [ ] **Step 2: Checks** (Bastian, serial log open):
  1. Boots, `SHELL_PLAY` lines arrive, sound.
  2. RV2 moves RATE_A, RV4 DENSITY_A, RV6 FILT_A — audibly, each through the law (the init patch's depths now modulate them: expect RATE_A to wander, unlike P6a's session).
  3. `SHELL_PLAY_LAW cyc_max` after a minute of turning all three pots: write down `cyc_last` and `cyc_max`.
- [ ] **Step 3: Apply spec §6's rule.** `cyc_max ≤ 9600`: done. `> 9600`: tick every second block (`static bool odd; if((odd = !odd)) … tick …` around the call in `panel_scan_tick`), rebuild, re-measure; if the average over two blocks still exceeds 9600, split the law's call over blocks by deck (needs a `tick_deck(p)` / `tick_global()` split in `control_law.h` — stop and bring that design back to Bastian first).
- [ ] **Step 4: Record** in `docs/roadmap.md` (new M6 entry `2026-10-…`): what P6b-1 changed, SRAM_EXEC before/after, `.qspi_cold` size, `cyc_max`, the rule's outcome, the three known divergences. Update the M6 table row (P6b-1 done, P6b-2 next). Commit: `docs: P6b-1 coupon session -- the law runs on the board`.

---

### Task 10: VCV install and the Rack hand check

**Files:** none (or fixes found by the check).

- [ ] **Step 1:** `host/vcv/build-local.sh install`, then tell Bastian: "restart Rack".
- [ ] **Step 2: Bastian checks** (spec §7.3), on `FireflowHW` and `Fireflow`:
  1. Initialize: the init patch sounds as before.
  2. Engine switch on deck A through all six engines; on the Sampler the factory drone loads.
  3. Turn ENG to BBD from another engine: FLUX drops to 0 on the panel, "excite other deck" turns on.
  4. Turn SONG one detent: a new phrase.
  5. Save a patch, reload it: no SONG re-roll, no settle glide on load.
- [ ] **Step 3:** Any difference is a moved-code bug: find it with the recorder (write the failing case into `tests/test_control_law.cpp` first), fix, re-run Task 4 Step 4 and this task.
- [ ] **Step 4:** Finish the branch with superpowers:finishing-a-development-branch (whole-branch review first).
