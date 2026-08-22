# MOD Latch Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The MOD pad on the FireflowHW panel becomes a latching layer: while lit, every wreathed knob edits its own modulation depth; the engine and init sound stay byte-for-byte what they are today until a depth is raised.

**Architecture:** 49 appended ParamIds (MODBTN + 48 depths) come out of `gen_panel.py`, which also emits a `kModLayer[]` routing table. Six engine-backed faces write the engine's existing `set_target_depth`/`set_fx_target_depth`; every other target is host-computed — the push loop adds `depth × MOD × lane_output` in knob space through the parameter's existing setter. The HW widget stacks a depth twin on each wreathed knob and swaps visibility on the latch; the plate print gains dashed accent wreaths from `gen_hw_panel.py`.

**Tech Stack:** Python panel generators (`host/vcv/res/gen_panel.py`, `gen_hw_panel.py`), VCV Rack C++ (`host/vcv/src/Fireflow.cpp`), Rack-free C++ headers tested by `spky_tests` (doctest, `tests/`).

**Spec:** `docs/superpowers/specs/2026-08-22-mod-latch-layer-design.md` — read it first; every decision below argues from it.

## Global Constraints

- **Two toolchains, never mixed.** Engine tests: `source env.sh`, then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build --output-on-failure`. `-DCMAKE_BUILD_TYPE=Release` is mandatory (Debug breaks render-hash gates). VCV plugin: **always** `host/vcv/build-local.sh`, never a hand-rolled g++ (the system g++ is an ARM cross-compiler).
- **Never prefix a shell command with `cd`.** Run everything from the repo root; the panel generators and guards take their directory as `python host/vcv/res/gen_panel.py` style invocations only if they support it — they don't (they `os.path` from `__file__` but are documented to run from `host/vcv/`), so invoke them as `python res/gen_panel.py` **via a script file in the scratchpad** or use `python host/vcv/res/test_panel.py` if it passes from root; if a guard needs cwd `host/vcv`, wrap the call in a scratchpad `.sh` with the cd inside the script.
- **pytest is NOT installed.** `test_panel.py` / `test_hw_panel.py` run as plain scripts (`python <file>`); they print `FAIL (n)` or `OK`.
- **Generated files are never hand-edited:** `generated_panel.hpp`, `generated_hw_panel.hpp`, `init_patch.hpp`, `Fireflow.svg`, `FireflowHW.svg` come only from the generators.
- **Param ids are append-only** (`Fireflow.cpp:295`). Dev alpha: patch breakage is fine, id reordering is not.
- **Appended params must never be read via `pp()`/`ppb()`** — explicit ids or the `kModLayer` table only (see the REC comment block `Fireflow.cpp:301-311`).
- **Everything written into the repo is English.** Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **No bit-exactness gates on renders.** Unit gates use exact equality only where an early-return makes it honest (noted per task).
- **Every test must be able to fail:** each new gate gets one RED proof (a deliberate mutation, run, revert).
- The lane indices are `spky::LANE_SOURCE=0, LANE_SIZE=1, LANE_PITCH=2, LANE_MOTION=3, LANE_LEVEL=4` (`engine/mod/lane_id.h`); FX targets `FXT_GRIT_INT=0, FXT_FLUX_TIME=1, FXT_FX_MIX=2, FXT_REV_SEND=3, FXT_FLUX_FB=4` (`engine/fx/part_fx.h`).
- **The pitch anchor stays:** no depth param may target `LANE_PITCH` (`_tdepth[2]` stays 1.0, untouched).

---

### Task 1: gen_panel.py — mod-layer tables, 49 appended params, kModLayer emission

**Files:**
- Modify: `host/vcv/res/gen_panel.py` (tables near `APPENDED_PANEL_PARAMS` ~line 612, `PARAMS =` line ~648, after `INIT_DEFAULTS` dict ~line 673-820, emission in `header()` after `emit_table` calls ~line 1074)
- Modify: `host/vcv/res/test_panel.py` (new check function, registered wherever the other `check_*` functions run)
- Regenerate: `host/vcv/src/generated_panel.hpp`, `host/vcv/src/init_patch.hpp`, `host/vcv/res/Fireflow.svg`

**Interfaces:**
- Produces (Python, consumed by Task 2): `MOD_DECK_TARGETS`, `MOD_CENTER_TARGETS` (lists of `(base, kind, slot, init)`), `MODBTN_CTL` (a `Ctl`), `MOD_LAYER_PARAMS` (trailing block of `PARAMS`).
- Produces (C++, consumed by Tasks 3/5/6): enums `MODBTN`, `MODD_<base>_A/_B` (21 pairs), `MODD_MORPH`, `MODD_REV_SIZE`, `MODD_REV_DECAY`, `MODD_REV_TONE`, `MODD_REV_DIFF`, `MODD_TIDE`; `enum ModKind { MODK_TDEPTH=0, MODK_FXDEPTH=1, MODK_HOST=2 }`; `struct ModTarget { int soundId; int depthId; unsigned char kind; unsigned char slot; unsigned char part; const char* name; }`; `static const ModTarget kModLayer[48]`. `part` is 0 (deck A), 1 (deck B), 2 (center).

- [ ] **Step 1: Write the failing guard in `test_panel.py`**

Add next to the existing check functions (match their style — `check(cond, msg)` collecting into `FAILS`):

```python
def check_mod_layer():
    """Spec 2026-08-22 mod-latch-layer: 48 depth targets + MODBTN appended
    LAST, engine-backed inits carried over, everything else 0."""
    deck = gp.MOD_DECK_TARGETS
    cent = gp.MOD_CENTER_TARGETS
    check(len(deck) == 21, f"deck targets: {len(deck)} != 21")
    check(len(cent) == 6, f"center targets: {len(cent)} != 6")
    # the appended block is PARAMS' tail, MODBTN first
    tail = [c.enum for c in gp.PARAMS[-(1 + 2 * len(deck) + len(cent)):]]
    check(tail[0] == "MODBTN", f"tail starts {tail[0]}, not MODBTN")
    for base, _k, _s, _i in deck:
        check(f"MODD_{base}_A" in tail and f"MODD_{base}_B" in tail,
              f"MODD_{base} pair missing from PARAMS tail")
    for base, _k, _s, _i in cent:
        check(f"MODD_{base}" in tail, f"MODD_{base} missing from PARAMS tail")
    # inits: engine-backed carry the booted _tdepth values, all else 0
    want = {"SOURCE": 1.0, "DEPTH": 0.7, "FILT": 0.55}
    for base, kind, _s, init in deck:
        expect = want.get(base, 0.0)
        check(abs(init - expect) < 1e-9, f"{base} init {init} != {expect}")
        for sfx in ("_A", "_B"):
            check(abs(gp.INIT_DEFAULTS[f"MODD_{base}{sfx}"] - expect) < 1e-9,
                  f"INIT_DEFAULTS[MODD_{base}{sfx}] != {expect}")
    for base, _k, _s, init in cent:
        check(init == 0.0 and gp.INIT_DEFAULTS[f"MODD_{base}"] == 0.0,
              f"center {base} init must be 0")
    check(gp.INIT_DEFAULTS["MODBTN"] == 0.0, "MODBTN boots unlatched")
    # the pitch anchor: no target may sit on LANE_PITCH via the engine table
    for base, kind, slot, _i in deck + cent:
        if kind == "TDEPTH":
            check(slot != 2, f"{base}: TDEPTH on LANE_PITCH is forbidden (anchor)")
    # excluded faces never grew a depth
    for base in ("GRIT", "FLUXRATE", "STAGES", "MOD", "TEMPO", "SHUFFLE",
                 "PACE", "DRIFT", "COUPLE", "CHOKE", "SCALE", "STEPS",
                 "SONG", "ENGINE", "REC"):
        check(f"MODD_{base}_A" not in gp.INIT_DEFAULTS
              and f"MODD_{base}" not in gp.INIT_DEFAULTS,
              f"excluded face {base} has a depth param")
    # emission reached the header
    src = open(os.path.join(SRC_DIR, "generated_panel.hpp")).read()
    check("kModLayer" in src, "kModLayer missing from generated_panel.hpp")
    check(src.count("MODK_HOST") >= 1 and "struct ModTarget" in src,
          "ModTarget/ModKind missing from generated_panel.hpp")
    check(len(re.findall(r"\{\s*\w+, MODD_", src)) == 48,
          f"kModLayer row count != 48")
```

Adapt `SRC_DIR`/imports to how the file already locates `generated_panel.hpp` (it reads it for other checks — reuse that path variable) and register `check_mod_layer()` in the same runner as the sibling checks.

- [ ] **Step 2: Run the guard, verify it fails**

Run (scratchpad wrapper if cwd matters): `python host/vcv/res/test_panel.py`
Expected: FAIL mentioning `MOD_DECK_TARGETS` (AttributeError caught into FAILS) or the kModLayer checks.

- [ ] **Step 3: Implement in `gen_panel.py`**

Insert after the `APPENDED_PANEL_PARAMS` list (before `RUNTIME_PANEL_PARAMS =`):

```python
# --- MOD latch layer (spec 2026-08-22-mod-latch-layer-design.md) ----------
# One row per modulatable face. kind: "TDEPTH" writes Part::_tdepth[slot]
# via set_target_depth, "FXDEPTH" writes _fx_depth[slot] via
# set_fx_target_depth (+ active <=> depth > 0), "HOST" is host-computed in
# knob space (Fireflow.cpp pushParams). slot is the spky lane index for
# TDEPTH/HOST (SOURCE 0, SIZE 1, PITCH 2, MOTION 3, LEVEL 4) and the FX
# target index for FXDEPTH. init: engine-backed faces carry the booted
# _tdepth values (part.h) so init sounds exactly like today; every
# host-computed depth starts at 0 for the same reason. The pitch anchor:
# nothing here may put a TDEPTH on slot 2 (LANE_PITCH) — RANG already owns
# pitch-mod amount and RANGE 0 silences the lane exactly (spec §6 probe 1).
MOD_DECK_TARGETS = [
    ("SOURCE",  "TDEPTH", 0, 1.0),
    ("DEPTH",   "TDEPTH", 3, 0.7),
    ("FILT",    "TDEPTH", 1, 0.55),
    ("FLUX",    "FXDEPTH", 2, 0.0),   # FXT_FX_MIX
    ("FLUXFB",  "FXDEPTH", 4, 0.0),   # FXT_FLUX_FB
    ("REV_MIX", "FXDEPTH", 3, 0.0),   # FXT_REV_SEND
    ("RATE",    "HOST", 1, 0.0),
    ("SHAPE",   "HOST", 3, 0.0),
    ("DENSITY", "HOST", 3, 0.0),
    ("SMOOTH",  "HOST", 1, 0.0),
    ("RANGE",   "HOST", 1, 0.0),
    ("MELODY",  "HOST", 4, 0.0),
    ("SUB",     "HOST", 4, 0.0),
    ("DETUNE",  "HOST", 0, 0.0),
    ("ATTACK",  "HOST", 3, 0.0),
    ("DECAY",   "HOST", 3, 0.0),
    ("RES",     "HOST", 1, 0.0),
    ("COLOR",   "HOST", 3, 0.0),
    ("TUNE",    "HOST", 2, 0.0),      # HOST on the pitch lane is fine; TDEPTH is not
    ("LINK",    "HOST", 4, 0.0),
    ("COMP",    "HOST", 4, 0.0),
]
MOD_CENTER_TARGETS = [
    ("MORPH",     "HOST", 3, 0.0),
    ("REV_SIZE",  "HOST", 1, 0.0),
    ("REV_DECAY", "HOST", 1, 0.0),
    ("REV_TONE",  "HOST", 1, 0.0),
    ("REV_DIFF",  "HOST", 1, 0.0),
    ("TIDE",      "HOST", 1, 0.0),
]
# The latch itself. Coordinates are the HW plate's (W-14, jack row); the big
# panel never draws this block (it is not in RUNTIME_PANEL_PARAMS), so the
# numbers only matter to gen_hw_panel.py's place().
MODBTN_CTL = Ctl("MODBTN", LATCH, W - 14.0, 114.0, "MOD", "MOD layer latch")
MOD_LAYER_PARAMS = [MODBTN_CTL]
for _base, _kind, _slot, _init in MOD_DECK_TARGETS:
    for _sfx in ("_A", "_B"):
        MOD_LAYER_PARAMS.append(Ctl(f"MODD_{_base}{_sfx}", SMKNOB, 0.0, 0.0, ""))
for _base, _kind, _slot, _init in MOD_CENTER_TARGETS:
    MOD_LAYER_PARAMS.append(Ctl(f"MODD_{_base}", SMKNOB, 0.0, 0.0, ""))
```

Change the `PARAMS` line to:

```python
PARAMS = PANEL_PARAMS + HIDDEN_PARAMS + APPENDED_PANEL_PARAMS + MOD_LAYER_PARAMS
```

`RUNTIME_PANEL_PARAMS` and `STATIC_PANEL_PARAMS` stay untouched (the big module and its SVG do not change).

After the closing brace of the `INIT_DEFAULTS = { ... }` dict add:

```python
# MOD latch layer defaults (spec §3a): engine-backed depths carry the booted
# values, host-computed depths and the latch itself start at 0 — init is
# byte-identical to today.
INIT_DEFAULTS["MODBTN"] = 0.0
for _base, _kind, _slot, _init in MOD_DECK_TARGETS:
    INIT_DEFAULTS[f"MODD_{_base}_A"] = _init
    INIT_DEFAULTS[f"MODD_{_base}_B"] = _init
for _base, _kind, _slot, _init in MOD_CENTER_TARGETS:
    INIT_DEFAULTS[f"MODD_{_base}"] = _init
```

In `header()`, directly after the five `emit_table(...)` calls, add:

```python
    KINDMAP = {"TDEPTH": 0, "FXDEPTH": 1, "HOST": 2}
    L2.append("enum ModKind { MODK_TDEPTH = 0, MODK_FXDEPTH = 1, MODK_HOST = 2 };")
    L2.append("struct ModTarget { int soundId; int depthId; unsigned char kind; "
              "unsigned char slot; unsigned char part; const char* name; };")
    L2.append("static const ModTarget kModLayer[] = {")
    for base, kind, slot, _init in MOD_DECK_TARGETS:
        for pi, sfx in enumerate(("_A", "_B")):
            L2.append(f'    {{{base}{sfx}, MODD_{base}{sfx}, {KINDMAP[kind]}, '
                      f'{slot}, {pi}, "{base} {"AB"[pi]} mod depth"}},')
    for base, kind, slot, _init in MOD_CENTER_TARGETS:
        L2.append(f'    {{{base}, MODD_{base}, {KINDMAP[kind]}, {slot}, 2, '
                  f'"{base} mod depth"}},')
    L2.append("};")
```

- [ ] **Step 4: Regenerate and run both guards**

Run `python host/vcv/res/gen_panel.py` (from `host/vcv/` via a scratchpad wrapper — the generator writes `res/Fireflow.svg` relative to itself so check where the outputs landed; the correct invocation per repo docs is from `host/vcv/`). Then `python host/vcv/res/test_panel.py`.
Expected: `OK`, params count printed by the generator grows by 49. Also run `python host/vcv/res/test_hw_panel.py` — **it will FAIL** (HW_PARAMS mismatch is Task 2's job only if it does; if it passes, note that and move on — the `HW_PARAMS == RUNTIME_PANEL_PARAMS` guard compares runtime params, which did not change, so it should still pass here).

- [ ] **Step 5: RED proof**

Temporarily set `("SOURCE", "TDEPTH", 0, 1.0)` to init `0.9` — guard must fail on the init check. Revert, rerun, `OK`.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/res/gen_panel.py host/vcv/res/test_panel.py host/vcv/src/generated_panel.hpp host/vcv/src/init_patch.hpp host/vcv/res/Fireflow.svg
git commit -m "feat(vcv): mod-layer param block and kModLayer table come out of the generator

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

(Only add `res/Fireflow.svg` if the regeneration actually changed it; it should not, since STATIC_PANEL_PARAMS is untouched — verify with `git status` and report if it differs.)

---

### Task 2: gen_hw_panel.py — MODBTN becomes a real latch, wreaths on the plate

**Files:**
- Modify: `host/vcv/res/gen_hw_panel.py` (HW_ONLY ~line 336, CENTER_POS ~line 229, HW_CAPTION ~line 148, HW_PARAMS ~line 316, pot-drawing loop ~line 747-769, label placement)
- Modify: `host/vcv/res/test_hw_panel.py` (two identity asserts at lines 28 and 349, new wreath check)
- Regenerate: `host/vcv/src/generated_hw_panel.hpp`, `host/vcv/res/FireflowHW.svg`

**Interfaces:**
- Consumes: `gp.MODBTN_CTL`, `gp.MOD_DECK_TARGETS`, `gp.MOD_CENTER_TARGETS` (Task 1).
- Produces: `MOD_WREATHED` (Python set of wreathed enums, for the guard); `MODBTN` as the last row of `spkyhw::kParamCtls` (WK_LATCH — Task 6's widget loop draws it with zero new code); dashed wreath circles in `FireflowHW.svg`.

- [ ] **Step 1: Write the failing guards**

In `test_hw_panel.py`, update both identity asserts (lines 28 and 349):

```python
    assert [c.enum for c in hw.HW_PARAMS] == \
        [c.enum for c in gp.RUNTIME_PANEL_PARAMS] + ["MODBTN"]
```

Add a new check function in the file's style:

```python
def check_mod_wreaths():
    """Spec 2026-08-22 §5: every mod target wears a dashed accent wreath,
    nothing else does, and MODBTN is a real latch param now."""
    want = ({f"{b}_A" for b, _, _, _ in gp.MOD_DECK_TARGETS}
            | {f"{b}_B" for b, _, _, _ in gp.MOD_DECK_TARGETS}
            | {b for b, _, _, _ in gp.MOD_CENTER_TARGETS})
    check(hw.MOD_WREATHED == want,
          f"MOD_WREATHED diverged from gp tables: {hw.MOD_WREATHED ^ want}")
    svg = open(os.path.join(HERE, "FireflowHW.svg")).read()
    rings = re.findall(r'<circle[^>]*stroke-dasharray="1.6 1.2"[^>]*/>', svg)
    # group frames use the same dash but are <rect>, not <circle>, so every
    # dashed circle on the plate is a wreath
    check(len(rings) == len(want), f"{len(rings)} wreath circles, want {len(want)}")
    for ring in rings:
        check(any(acc in ring for acc in hw.ACC.values()),
              f"wreath without a zone accent: {ring}")
    # the master knob is deliberately unwreathed
    for enum in ("MOD_A", "MOD_B"):
        c = next(c for c in hw.HW_PARAMS if c.enum == enum)
        check(f'cx="{c.x:.3f}" cy="{c.y:.3f}" r="{hw.BODY_R["G"] + 1.2:.3f}"'
              not in svg, f"{enum} grew a wreath")
    # MODBTN: real param, out of kHwOnlyCtls, caption on the jack-row baseline
    src = open(os.path.join(SRC, "generated_hw_panel.hpp")).read()
    check(re.search(r"\{\s*MODBTN\s*,\s*WK_LATCH", src), "MODBTN not in kParamCtls")
    hwonly = src.split("kHwOnlyCtls")[1]
    check("MODBTN" not in hwonly.split("};")[0], "MODBTN still in kHwOnlyCtls")
    mod = next(c for c in hw.HW_PARAMS if c.enum == "MODBTN")
    shift = next(c for c in hw.HW_ONLY if c.enum == "SHIFTBTN")
    my, sy = hw.hw_label(mod)[1], hw.hw_label(shift)[1]
    check(abs(my - sy) < 1e-6, f"MOD caption baseline {my} != SHFT {sy}")
```

Reuse the file's existing path variables for the SVG/hpp (it already opens both — copy those expressions instead of `HERE`/`SRC` placeholders). Register the check with the others.

- [ ] **Step 2: Run, verify it fails**

`python host/vcv/res/test_hw_panel.py` → FAIL (no `MOD_WREATHED`, identity assert fires).

- [ ] **Step 3: Implement in `gen_hw_panel.py`**

1. `HW_CAPTION` gains `"MODBTN": "MOD",`.
2. `CENTER_POS` gains `"MODBTN": (W - 14.00, JACK_Y),`.
3. `HW_ONLY` loses the MODBTN line (SHIFTBTN stays).
4. After the existing `HW_PARAMS = [place(c) for c in gp.RUNTIME_PANEL_PARAMS]`:

```python
# MODBTN is a real latch param now (spec 2026-08-22 mod-latch-layer §5); it
# lives in gp.MOD_LAYER_PARAMS, outside RUNTIME_PANEL_PARAMS, so the big
# panel never draws it -- placed here explicitly, keycap slot it always had.
HW_PARAMS = HW_PARAMS + [place(gp.MODBTN_CTL)]
```

5. Caption baseline: `MODBTN`'s caption must stay on the jack-row baseline like SHFT (the comment at lines 142-145 explains why). In `hw_label` (the function that returns `(lx, ly, anchor, size, colour)`), add the same branch the HW_ONLY path uses for keycaps — if it keys on `c.kind == gp.LATCH and c.y == JACK_Y` or on enum, prefer the explicit enum check:

```python
    if c.enum in ("MODBTN", "SHIFTBTN"):
        return (c.x, c.y + JACK_ROW_LBL_DY, "middle", CAPTION_SIZE, HW_LABEL)
```

(Read `hw_label` first; if HW_ONLY captions already flow through a shared branch that this placement hits naturally, skip the edit and let the guard prove it.)

6. Wreath set + drawing. Near the palette constants:

```python
# Which pots wear the printed mod wreath (spec 2026-08-22 §5, variant B):
# exactly the faces with a depth param. Derived from gen_panel's tables so
# the plate and the param block cannot drift apart.
MOD_WREATHED = ({f"{b}_A" for b, _, _, _ in gp.MOD_DECK_TARGETS}
                | {f"{b}_B" for b, _, _, _ in gp.MOD_DECK_TARGETS}
                | {b for b, _, _, _ in gp.MOD_CENTER_TARGETS})
```

In the pot-drawing `else:` branch of the `for c in ALL_HW:` loop (the branch that draws the mounting hole, ~line 766), after the well circle:

```python
            if c.enum in MOD_WREATHED:
                # Variant B mod wreath: dashed satellite ring, group-frame
                # dash, zone accent. Absence of the ring means the knob
                # keeps its sound function while MOD is latched.
                P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" '
                          f'r="{mm(body_r(c) + 1.2)}" fill="none" '
                          f'stroke="{ACC[zone_of(c.x)]}" stroke-width="0.35" '
                          f'stroke-opacity="0.85" stroke-dasharray="1.6 1.2"/>')
```

- [ ] **Step 4: Regenerate and run the guard**

Run `python res/gen_hw_panel.py` from `host/vcv/` (scratchpad wrapper), then `python host/vcv/res/test_hw_panel.py` and `python host/vcv/res/test_panel.py`.
Expected: both `OK`. Eyeball `host/vcv/res/FireflowHW.svg` in a browser — 48 dashed wreaths, MOD/GRIT/TIME/DRFT/SYNC/CHOK and the whole clock/structure row bare, MOD keycap caption unchanged.

- [ ] **Step 5: RED proof**

Temporarily add `"GRIT"` wreathing (add `f"GRIT_A"` to `MOD_WREATHED`), regenerate — guard must fail on the set diff AND the ring count. Revert, regenerate, `OK`.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/res/gen_hw_panel.py host/vcv/res/test_hw_panel.py host/vcv/src/generated_hw_panel.hpp host/vcv/res/FireflowHW.svg
git commit -m "feat(vcv): MODBTN is a real latch and the plate prints its mod wreaths

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: mod_layer.hpp — Rack-free layer math + table sanity gates

**Files:**
- Create: `host/vcv/src/mod_layer.hpp`
- Create: `tests/test_mod_layer.cpp`
- Modify: `tests/CMakeLists.txt` (add the new source next to `test_led_law.cpp`'s entry, same include dirs — `test_led_law.cpp` already includes `generated_panel.hpp`, so the include path for `host/vcv/src` exists; copy its wiring)

**Interfaces:**
- Consumes: `spkyvcv::kModLayer`, `MODK_*`, `MODD_*` enums, `initParamDefault()` (Task 1); `spky::LANE_COUNT`, `spky::LANE_PITCH`, `spky::FXT_COUNT`.
- Produces (consumed by Task 5): `namespace spkymod`: `float modded(float knob, float depth, float laneTerm, float lo, float hi)`, `float lane_term(float master, float laneOut)`, `float center_term(float mA, float lA, float mB, float lB)`.

- [ ] **Step 1: Write the failing test**

`tests/test_mod_layer.cpp` (doctest style — copy the header/include pattern from `tests/test_led_law.cpp`):

```cpp
#include "doctest.h"
#include "mod_layer.hpp"
#include "generated_panel.hpp"
#include "mod/lane_id.h"
#include "fx/part_fx.h"
#include "init_patch.hpp"
#include <set>

using namespace spkyvcv;

// Depth 0 must reproduce the plain knob push EXACTLY -- the early return in
// modded() makes bit-equality honest here (no arithmetic touches the value).
// This is the "init sounds like today" gate.
TEST_CASE("mod layer: depth 0 is the identity") {
    for (float knob : {0.f, 0.1337f, 0.5f, 0.99f, 1.f, -0.73f}) {
        CHECK(spkymod::modded(knob, 0.f, 0.83f, -1.f, 1.f) == knob);
        CHECK(spkymod::modded(knob, 0.f, -1.f, -1.f, 1.f) == knob);
    }
}

TEST_CASE("mod layer: offset lands in knob space and clamps to the range") {
    CHECK(spkymod::modded(0.5f, 1.f, 0.25f, 0.f, 1.f) == doctest::Approx(0.75f));
    CHECK(spkymod::modded(0.9f, 1.f, 1.f, 0.f, 1.f) == 1.f);       // top clamp
    CHECK(spkymod::modded(-0.9f, 1.f, -1.f, -1.f, 1.f) == -1.f);   // bipolar floor
    CHECK(spkymod::lane_term(0.5f, -0.8f) == doctest::Approx(-0.4f));
    // both masters down -> the center is still (spec §2)
    CHECK(spkymod::center_term(0.f, 1.f, 0.f, -1.f) == 0.f);
    CHECK(spkymod::center_term(1.f, 0.6f, 1.f, 0.2f) == doctest::Approx(0.4f));
}

TEST_CASE("mod layer: kModLayer is exactly the spec's table") {
    const int n = sizeof(kModLayer) / sizeof(kModLayer[0]);
    CHECK(n == 48);
    std::set<int> depthIds, soundIds;
    int centers = 0, tdepth = 0, fxdepth = 0;
    for (int i = 0; i < n; ++i) {
        const auto& t = kModLayer[i];
        CHECK(t.soundId != t.depthId);
        CHECK(t.depthId > REC_B);            // appended block only
        CHECK(t.depthId < NUM_PARAMS);
        CHECK(depthIds.insert(t.depthId).second);
        CHECK(soundIds.insert(t.soundId).second);
        CHECK(t.part <= 2);
        if (t.part == 2) ++centers;
        if (t.kind == MODK_TDEPTH) {
            ++tdepth;
            CHECK(t.slot < spky::LANE_COUNT);
            CHECK(t.slot != spky::LANE_PITCH);   // the anchor stays
        } else if (t.kind == MODK_FXDEPTH) {
            ++fxdepth;
            CHECK(t.slot < spky::FXT_COUNT);
        } else {
            CHECK(t.kind == MODK_HOST);
            CHECK(t.slot < spky::LANE_COUNT);
        }
    }
    CHECK(centers == 6);
    CHECK(tdepth == 6);      // TIMB/DPTH/FILT x two decks
    CHECK(fxdepth == 6);     // MIX/FB/SEND x two decks
    // excluded faces never appear as a sound target
    for (int excluded : {(int)GRIT_A, (int)GRIT_B, (int)FLUXRATE_A, (int)FLUXRATE_B,
                         (int)STAGES_A, (int)STAGES_B, (int)MOD_A, (int)MOD_B,
                         (int)TEMPO, (int)SHUFFLE, (int)PACE, (int)DRIFT,
                         (int)COUPLE, (int)CHOKE, (int)SCALE, (int)STEPS_A,
                         (int)STEPS_B, (int)SONG_A, (int)SONG_B, (int)ENGINE_A,
                         (int)ENGINE_B, (int)REC_A, (int)REC_B, (int)MODBTN})
        CHECK(soundIds.count(excluded) == 0);
}

TEST_CASE("mod layer: init defaults keep today's sound") {
    CHECK(initParamDefault(MODBTN) == 0.f);
    CHECK(initParamDefault(MODD_SOURCE_A) == doctest::Approx(1.0f));
    CHECK(initParamDefault(MODD_DEPTH_A) == doctest::Approx(0.7f));
    CHECK(initParamDefault(MODD_FILT_A) == doctest::Approx(0.55f));
    for (const auto& t : kModLayer)
        if (t.kind != MODK_TDEPTH)
            CHECK(initParamDefault(t.depthId) == 0.f);
}
```

- [ ] **Step 2: Run to verify it fails**

```bash
source env.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```
Expected: compile FAILURE — `mod_layer.hpp` does not exist. (Register the file in `tests/CMakeLists.txt` first, mirroring `test_led_law.cpp`.)

- [ ] **Step 3: Write `host/vcv/src/mod_layer.hpp`**

```cpp
#pragma once
// Rack-free math of the MOD latch layer's host-computed path. Fireflow.cpp
// keeps only the wiring -- the same arrangement as led_law.hpp, and for the
// same reason: spky_tests can drive this, Rack cannot be linked there.
// Spec: docs/superpowers/specs/2026-08-22-mod-latch-layer-design.md §3b.
namespace spkymod {

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// The deck term: master MOD times the assigned lane's output.
inline float lane_term(float master, float laneOut) {
    return master * laneOut;
}

// The center term: mean of both decks' terms, so both masters down means
// the center is still (spec §2).
inline float center_term(float masterA, float laneA,
                         float masterB, float laneB) {
    return 0.5f * (masterA * laneA + masterB * laneB);
}

// pushed value = clamp(knob + depth * term) in KNOB space, before the
// parameter's own engine mapping. Depth 0 returns the knob untouched --
// bit-exact by early return, which is what the identity gate leans on.
inline float modded(float knob, float depth, float laneTerm,
                    float lo, float hi) {
    if (depth <= 0.f) return knob;
    return clampf(knob + depth * laneTerm, lo, hi);
}

} // namespace spkymod
```

- [ ] **Step 4: Build and run**

`cmake --build build && ctest --test-dir build --output-on-failure`
Expected: all PASS (including every pre-existing test).

- [ ] **Step 5: RED proof**

In `modded()`, change the early return to `if (depth < 0.f)` — the identity CHECKs must fail (arithmetic now runs at depth 0... it returns knob+0*term which floats back to knob; if the identity still passes, mutate harder: `return knob + 0.001f;` under the depth-0 branch). Confirm at least one CHECK goes red, revert, rerun green. Record which mutation was used.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/src/mod_layer.hpp tests/test_mod_layer.cpp tests/CMakeLists.txt
git commit -m "feat(vcv): rack-free mod-layer math, table sanity and identity gates

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: led_law.hpp — the latch reaches its lamp

**Files:**
- Modify: `host/vcv/src/led_law.hpp` (signature line 118, zero-list lines 122-129)
- Modify: `tests/test_led_law.cpp` (all `fill(...)` call sites + one new case)
- Modify: `host/vcv/src/Fireflow.cpp` (the single `spkyled::fill(...)` call site — grep for it)

**Interfaces:**
- Produces: `spkyled::fill(const spky::Instrument&, Panel&, float dt, int steps, bool mod_latched, int* duty_out)` — note the new `bool mod_latched` parameter before `duty_out`.

- [ ] **Step 1: Write the failing test**

In `tests/test_led_law.cpp`, add (copying the file's existing fixture pattern for constructing an `Instrument` and duty buffer):

```cpp
TEST_CASE("led law: MODBTN lamp is the latch state") {
    // build inst/panel/duty exactly as the neighbouring cases do
    int duty[spkyvcv::NUM_LIGHTS] = {0};
    spkyled::Panel p;
    spkyled::fill(inst, p, 0.01f, 16, /*mod_latched=*/true, duty);
    CHECK(duty[spkyvcv::MODBTN_L] == 15);
    CHECK(duty[spkyvcv::SHIFTBTN_L] == 0);   // SHIFT stays reserved and dark
    spkyled::fill(inst, p, 0.01f, 16, /*mod_latched=*/false, duty);
    CHECK(duty[spkyvcv::MODBTN_L] == 0);
}
```

Update every existing `fill(` call in the test file to pass `false` for the new parameter.

- [ ] **Step 2: Run to verify it fails**

`cmake --build build` → compile FAILURE (fill has no such parameter).

- [ ] **Step 3: Implement**

In `led_law.hpp` change the signature:

```cpp
inline void fill(const spky::Instrument& inst, Panel& p, float dt,
                 int steps, bool mod_latched, int* duty_out) {
```

Shrink the zero-list comment and loop — MODBTN_L leaves it, SHIFTBTN stays with FLOW/SYNC:

```cpp
    // Written, not skipped. FLOW and SYNC need host state this round does
    // not wire, and SHIFTBTN still waits for the round that builds SHIFT.
    // MODBTN left this list on 2026-08-22: its latch exists now.
    for (int id : {FLOW_A_L, FLOW_B_L, SYNC_L, SHIFTBTN_L})
        duty_out[id] = 0;

    // The MOD layer's lamp: lit while the latch holds, hard on/off -- this
    // reports a mode, not a level (spec 2026-08-22 mod-latch-layer §5).
    duty_out[MODBTN_L] = mod_latched ? steps - 1 : 0;
```

In `Fireflow.cpp`, at the one `spkyled::fill(` call, pass `params[MODBTN].getValue() > 0.5f` as the new argument.

- [ ] **Step 4: Run tests and the plugin build**

`cmake --build build && ctest --test-dir build --output-on-failure` → PASS.
`bash host/vcv/build-local.sh` → builds clean (this is the caller-compiles check).

- [ ] **Step 5: RED proof**

Flip the implementation to `steps - 2` — lamp case fails on `== 15`. Revert, green.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/src/led_law.hpp tests/test_led_law.cpp host/vcv/src/Fireflow.cpp
git commit -m "feat(vcv): MODBTN lamp shows the latch, ending its spec-3.4 wait

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: Fireflow.cpp — config, asserts, and the two push paths

**Files:**
- Modify: `host/vcv/src/Fireflow.cpp` (asserts ~line 308-311, `configControls()` ~line 409, module members ~line 397, `pushParams()` ~line 654 onward)

**Interfaces:**
- Consumes: `kModLayer`, `MODK_*`, `spkymod::modded/lane_term/center_term`, `initParamDefault`, `Instrument::lane_output/set_target_depth/set_fx_target_depth/set_fx_target_active`.
- Produces (consumed by Task 6): `Fireflow::modIdxBySound[NUM_PARAMS]` (−1 = no depth; else index into `kModLayer`) — public member.

**Before touching anything:** read `docs/gotchas.md` end to end — the control-merge init trap and the CHOKE notes live there and this file is where they bite.

- [ ] **Step 1: Update the trailing-id assert**

Replace the second REC assert (line 310-311) with:

```cpp
// REC used to close the param list; the MOD layer's appended block trails it
// now. The hazard the old assert guarded is UPGRADED, not gone: a pp()/ppb()
// read of any appended id no longer indexes out of bounds -- it silently
// aliases a mod-layer param. Appended params are read explicitly or through
// kModLayer, never via pp().
static_assert(MODBTN > REC_B, "mod-layer params must stay appended after REC");
static_assert(NUM_PARAMS == MODBTN + 49, "mod layer is 49 params: MODBTN + 48 depths");
```

- [ ] **Step 2: Configure the new params**

At the end of `configControls()`:

```cpp
        // MOD latch layer (spec 2026-08-22). The depth params are not in
        // kParamCtls (the big panel never shows them); the HW widget stacks
        // them on their sound twins. Names come from the generator.
        configSwitch(MODBTN, 0.f, 1.f, 0.f, "MOD layer", {"Off", "On"});
        for (const auto& t : kModLayer)
            configParam(t.depthId, 0.f, 1.f, initParamDefault(t.depthId), t.name);
```

- [ ] **Step 3: Members + constructor index**

Add members next to `ctrlDiv`:

```cpp
    // MOD latch layer state for one control tick: lane outputs and the two
    // masters, sampled once at the top of pushParams so every mv() read in
    // the same tick sees the same modulation frame.
    float laneOut[spky::PART_COUNT][spky::LANE_COUNT] = {};
    float modMaster[spky::PART_COUNT] = {};
    int modIdxBySound[NUM_PARAMS];
```

In the constructor, after `configControls()`:

```cpp
        for (int i = 0; i < NUM_PARAMS; ++i) modIdxBySound[i] = -1;
        for (size_t i = 0; i < sizeof(kModLayer) / sizeof(kModLayer[0]); ++i)
            modIdxBySound[kModLayer[i].soundId] = (int)i;
```

- [ ] **Step 4: The mv() helper + frame sampling**

Next to `pp()` (include `"mod_layer.hpp"` at the top of the file, beside the `led_law.hpp` include):

```cpp
    // MOD-layer read of a host-computed sound param: knob + depth * MOD *
    // lane, in knob space, clamped to the param's own range (spec §3b).
    // Non-targets and engine-backed faces fall straight through to the knob.
    inline float mv(int soundId, int part) {
        const float v = params[soundId].getValue();
        const int mi = modIdxBySound[soundId];
        if (mi < 0) return v;
        const auto& t = kModLayer[mi];
        if (t.kind != MODK_HOST) return v;
        const float term = (t.part == 2)
            ? spkymod::center_term(modMaster[0], laneOut[0][t.slot],
                                   modMaster[1], laneOut[1][t.slot])
            : spkymod::lane_term(modMaster[part], laneOut[part][t.slot]);
        ParamQuantity* q = paramQuantities[soundId];
        return spkymod::modded(v, params[t.depthId].getValue(), term,
                               q->getMinValue(), q->getMaxValue());
    }
    // Strided twin of pp() for the part blocks.
    inline float mvp(int baseA, int part) {
        return mv(baseA + part * PART_STRIDE, part);
    }
```

At the very top of `pushParams()` (before `set_shuffle`):

```cpp
        // Sample the modulation frame once per control tick (spec §3b).
        for (int p = 0; p < 2; ++p) {
            modMaster[p] = pp(MOD_A, p);
            for (int s = 0; s < spky::LANE_COUNT; ++s)
                laneOut[p][s] = inst.lane_output(p, s);
        }
```

- [ ] **Step 5: Route the host-computed reads through mv()**

In `pushParams()`, replace ONLY the reads of faces whose kModLayer kind is HOST — the full list, with their current shapes:

| Push site (current expression) | Becomes |
|---|---|
| `inst.set_rate(p, pp(RATE_A, p))` | `inst.set_rate(p, mvp(RATE_A, p))` |
| `inst.set_shape(p, pp(SHAPE_A, p))` | `mvp(SHAPE_A, p)` |
| `inst.set_density(p, pp(DENSITY_A, p))` | `mvp(DENSITY_A, p)` |
| `inst.set_smooth(p, pp(SMOOTH_A, p))` | `mvp(SMOOTH_A, p)` |
| `inst.set_range(p, pp(RANGE_A, p))` | `mvp(RANGE_A, p)` |
| `inst.set_tune(p, pp(TUNE_A, p))` | `mvp(TUNE_A, p)` |
| `inst.set_voice_attack(p, pp(ATTACK_A, p))` | `mvp(ATTACK_A, p)` |
| `inst.set_voice_decay(p, pp(DECAY_A, p))` | `mvp(DECAY_A, p)` |
| `inst.set_voice_resonance(p, pp(RES_A, p))` | `mvp(RES_A, p)` |
| `inst.set_voice_sub(p, pp(SUB_A, p))` | `mvp(SUB_A, p)` |
| `const float detKnob = pp(DETUNE_A, p);` | `const float detKnob = mvp(DETUNE_A, p);` (mod lands in knob space BEFORE the square — spec §3b) |
| `inst.set_color(p, params[p ? COLOR_B : COLOR_A].getValue())` | `mv(p ? COLOR_B : COLOR_A, p)` |
| `inst.set_link(p, params[p ? LINK_B : LINK_A].getValue())` | `mv(p ? LINK_B : LINK_A, p)` |
| `set_comp(...)` read (grep `set_comp` in pushParams) | wrap its COMP_A/COMP_B read in `mv(...)` the same way |
| MELODY reads at the `set_variation` / `sampler_scan` push (~line 922) | wrap the `MELODY_A/MELODY_B` read in `mv(...)` |
| SUB/DETUNE **re-point** reads on Sampler/FEED (~lines 928-934, `set_target_base(LANE_SIZE, ...)`) | wrap the same knob reads in `mv(...)` — conditional faces follow their face on every engine (spec §4 last paragraph) |
| `inst.set_morph(params[MORPH].getValue())` (~line 992) | `inst.set_morph(mv(MORPH, 0))` |
| `inst.set_tide(params[TIDE].getValue())` (~line 1019) | `mv(TIDE, 0)` |
| `set_reverb_size/tone/decay/diffusion` reads (~lines 1021-1024) | `mv(REV_SIZE, 0)` / `mv(REV_TONE, 0)` / `mv(REV_DECAY, 0)` / `mv(REV_DIFF, 0)` |

Do NOT touch: MOD (master), CHOKE, DRIFT, COUPLE, TEMPO, SHUFFLE, PACE, SCALE, STEPS, SONG, ENGINE, GRIT, FLUXRATE, STAGES, REC — excluded by spec §2. Also do NOT rewrite the engine-backed faces' sound reads (FILT, SOURCE/TIMB, DEPTH/DPTH, FLUX/MIX, FLUXFB/FB, REV_MIX/SEND): their kind is TDEPTH/FXDEPTH, their modulation arrives inside the engine via Step 6, and `mv()` would fall through to the raw knob anyway — leave those reads exactly as they are. The FLUX `> 1e-4f` fx-on gate keeps reading the raw knob (`pp(FLUX_A, p)`); modulation must not toggle the block.

- [ ] **Step 6: Engine-backed depth pushes**

At the end of the per-deck section of `pushParams()` (after the loop over `p`, before the center pushes), add:

```cpp
        // Engine-backed mod depths (spec §3a): TIMB/DPTH/FILT write the
        // Part's own _tdepth slots, MIX/FB/SEND the FX row -- active iff the
        // depth is up. The engine multiplies its own master MOD in for the
        // texture lanes, so no modMaster factor appears here.
        for (const auto& t : kModLayer) {
            if (t.kind == MODK_TDEPTH) {
                inst.set_target_depth(t.part, t.slot,
                                      params[t.depthId].getValue());
            } else if (t.kind == MODK_FXDEPTH) {
                const float d = params[t.depthId].getValue();
                inst.set_fx_target_depth(t.part, t.slot, d);
                inst.set_fx_target_active(t.part, t.slot, d > 0.f);
            }
        }
```

- [ ] **Step 7: Build everything, run everything**

`cmake --build build && ctest --test-dir build --output-on-failure` → all PASS.
`bash host/vcv/build-local.sh` → plugin builds.
Sanity argument to verify and note in the commit: at init all HOST depths are 0, so every `mv()` is `params[...].getValue()` by the early return — the push stream is byte-identical to before this task (that is what Task 3's identity gate pins).

- [ ] **Step 8: Commit**

```bash
git add host/vcv/src/Fireflow.cpp
git commit -m "feat(vcv): pushParams grows the two mod-layer paths behind the latch

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: FireflowHWWidget — depth twins, visibility swap, accent rings

**Files:**
- Modify: `host/vcv/src/Fireflow.cpp` (widget templates near `SlotVisible` ~line 1626, `FireflowHWWidget` ctor ~line 2009-2064)

**Interfaces:**
- Consumes: `Fireflow::modIdxBySound`, `kModLayer`, `ctlVisible(Fireflow*, int)`, `spkyhw::kParamCtls`, `spkyhw::kParamSize`.

- [ ] **Step 1: Add the layer widgets** (next to `SlotVisible`):

```cpp
// MOD latch layer (spec 2026-08-22 §5). Module absent -- the browser
// preview -- counts as unlatched, so the browser shows the sound panel.
static bool modLatched(Fireflow* m) {
    return m && m->params[MODBTN].getValue() > 0.5f;
}

// The sound half of a wreathed knob: hidden while the layer holds. Combines
// with ctlVisible so ATTACK keeps its BBD hiding under the layer too.
template <typename W>
struct ModSound : W {
    Fireflow* fireflow = nullptr;
    int ctlId = 0;
    void step() override {
        this->setVisible(ctlVisible(fireflow, ctlId) && !modLatched(fireflow));
        W::step();
    }
};

// The depth half: visible only while latched, and only where its sound twin
// would be (an ATTACK depth has no business on a BBD deck).
template <typename W>
struct ModDepth : W {
    Fireflow* fireflow = nullptr;
    int soundId = 0;
    void step() override {
        this->setVisible(ctlVisible(fireflow, soundId) && modLatched(fireflow));
        W::step();
    }
};

// The dynamic half of the printed wreath: a solid accent ring behind the
// depth knob while the layer holds, so the whole panel visibly changes
// state (spec §5, "visual affordance").
struct ModDepthRing : TransparentWidget {
    Fireflow* fireflow = nullptr;
    int soundId = 0;
    NVGcolor col = nvgRGB(0x7f, 0xb6, 0xc9);
    float rMm = 5.6f;
    void step() override {
        setVisible(ctlVisible(fireflow, soundId) && modLatched(fireflow));
        TransparentWidget::step();
    }
    void draw(const DrawArgs& args) override {
        nvgBeginPath(args.vg);
        nvgCircle(args.vg, box.size.x * 0.5f, box.size.y * 0.5f, mm2px(rMm));
        nvgStrokeColor(args.vg, col);
        nvgStrokeWidth(args.vg, mm2px(0.35f));
        nvgStroke(args.vg);
    }
};
```

- [ ] **Step 2: Wire the widget loop**

In `FireflowHWWidget`'s `kParamCtls` loop, the knob cases (`WK_BIGKNOB/WK_KNOBC/WK_SMKNOB/WK_KNOBI`) change to: look up `const int mi = module ? module->modIdxBySound[c.id] : -1;` — but the browser has no module, so build the lookup from `kModLayer` directly instead (module-independent):

```cpp
            // depth twin lookup, module-independent so the browser preview
            // agrees with a live module
            int depthId = -1;
            for (const auto& t : spkyvcv::kModLayer)
                if (t.soundId == c.id) { depthId = t.depthId; break; }
```

Then, replacing the existing knob-adding code for those kinds:

```cpp
                case WK_BIGKNOB: case WK_KNOBC: case WK_SMKNOB: case WK_KNOBI: {
                    if (depthId < 0) {
                        // not a mod target: exactly the code this replaces
                        if (big) {
                            addParam(createParamCentered<RoundBlackKnob>(pos, module, c.id));
                        } else if (c.id == STAGES_A || c.id == STAGES_B) {
                            auto* knob = createParamCentered<SlotVisible<Trimpot>>(pos, module, c.id);
                            knob->fireflow = module;
                            knob->ctlId = c.id;
                            addParam(knob);
                        } else {
                            addParam(createParamCentered<Trimpot>(pos, module, c.id));
                        }
                        break;
                    }
                    // wreathed: sound half hides under the latch...
                    if (big) {
                        auto* k = createParamCentered<ModSound<RoundBlackKnob>>(pos, module, c.id);
                        k->fireflow = module; k->ctlId = c.id;
                        addParam(k);
                    } else {
                        auto* k = createParamCentered<ModSound<Trimpot>>(pos, module, c.id);
                        k->fireflow = module; k->ctlId = c.id;
                        addParam(k);
                    }
                    // ...the accent ring and the depth twin surface with it
                    NVGcolor acc = c.mm.x < 124.2f ? nvgRGB(0x3f, 0xbf, 0x9c)
                                 : c.mm.x > 304.8f - 124.2f ? nvgRGB(0xe8, 0x94, 0x5a)
                                 : nvgRGB(0x7f, 0xb6, 0xc9);
                    auto* ring = new ModDepthRing;
                    ring->fireflow = module; ring->soundId = c.id;
                    ring->col = acc; ring->rMm = big ? 7.2f : 5.6f;
                    ring->box.size = mm2px(Vec(2.f * ring->rMm + 1.f, 2.f * ring->rMm + 1.f));
                    ring->box.pos = pos.minus(ring->box.size.div(2.f));
                    addChild(ring);
                    if (big) {
                        auto* d = createParamCentered<ModDepth<RoundBlackKnob>>(pos, module, depthId);
                        d->fireflow = module; d->soundId = c.id;
                        addParam(d);
                    } else {
                        auto* d = createParamCentered<ModDepth<Trimpot>>(pos, module, depthId);
                        d->fireflow = module; d->soundId = c.id;
                        addParam(d);
                    }
                    break;
                }
```

Notes: the ATTACK/STAGES special case dissolves — ATTACK is a mod target (`ModSound` carries its `ctlVisible` BBD-hiding), STAGES is not (falls to the `SlotVisible` branch). MODBTN itself arrives through the existing `WK_LATCH` else-branch (`VCVLatch`) with zero new code — verify it appears, don't add anything.

- [ ] **Step 3: Build + screenshot**

`bash host/vcv/build-local.sh`, then a real-Rack screenshot (memory `fireflow-rack-screenshots-headless`): `Rack.exe -u <throwaway-dir> -t <zoom>` renders the module browser shots headlessly. Confirm: the HW panel shows the sound layer (browser = unlatched), wreaths printed, MOD keycap present at the jack row.
Expected: builds clean, screenshot shows no missing/misplaced widgets.

- [ ] **Step 4: Manual checkpoint (Bastian)**

Interactive Rack: latch MOD — every wreathed knob swaps to its depth twin with an accent ring, TIMB/DPTH/FILT depths sit at their booted positions (1.0 / 0.7 / 0.55), everything else at 0; excluded knobs (MOD, GRIT, TIME, DRFT, SYNC, CHOK, clock row) stay live; unlatch — panel returns, values kept. Raise FILT depth on deck A with MOD up: SIZE-axis modulation deepens. MOD to 0: texture modulation stops, pitch phrase keeps playing (anchor).

- [ ] **Step 5: Commit**

```bash
git add host/vcv/src/Fireflow.cpp
git commit -m "feat(vcv): the HW panel latches into its depth layer

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 7: Close out — docs, full suite, status

**Files:**
- Modify: `docs/roadmap.md` (the living-status entry for the HW panel / VCV module)
- Modify: `docs/superpowers/specs/2026-08-22-mod-latch-layer-design.md` (Status line: `approved design, pre-plan` → `implemented 2026-08-<day>, plan docs/superpowers/plans/2026-08-22-mod-latch-layer.md`)

- [ ] **Step 1: Full verification pass**

```bash
source env.sh
cmake --build build && ctest --test-dir build --output-on-failure
python host/vcv/res/test_panel.py
python host/vcv/res/test_hw_panel.py
bash host/vcv/build-local.sh
```
All green, no skips. Paste the ctest tail and both guard `OK` lines into the report.

- [ ] **Step 2: Roadmap + spec status**

Add to `docs/roadmap.md` under the VCV/HW-panel section (match the file's voice): one entry stating the MOD latch layer shipped — MODBTN latch + lamp, 48 depth params over two paths, printed wreaths — and that SHIFT stays reserved. Flip the spec's Status line.

- [ ] **Step 3: Commit**

```bash
git add docs/roadmap.md docs/superpowers/specs/2026-08-22-mod-latch-layer-design.md
git commit -m "docs: mod latch layer lands in the roadmap

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```
