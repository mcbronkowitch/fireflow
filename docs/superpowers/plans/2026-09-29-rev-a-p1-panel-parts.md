# Rev A P1 — cut file, print sheet and hole list — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generate, from the hardware panel generator, everything the acrylic grip-test plate needs — a laser cut file, a 1:1 print sheet and a hole list — behind a guard that runs in ctest, and move the four satellite LEDs that the real hole sizes put too close to their neighbours.

**Architecture:** Hole sizes and the satellite-LED rule live in `host/vcv/res/gen_hw_panel.py` (the panel knows its hardware). A new, separate generator `host/vcv/res/gen_hw_cut.py` imports it and emits three files; it never places anything itself. A new guard `host/vcv/res/test_hw_cut.py`, wired into ctest next to the two panel guards, checks the files against the panel.

**Tech Stack:** Python 3 (system interpreter, no pytest — plain asserts and a `check()` list, exit code is the verdict), CMake/ctest, the VCV plugin build (`host/vcv/build-local.sh`).

**Spec:** `docs/superpowers/specs/2026-09-29-rev-a-p1-panel-parts-design.md` (parent: `docs/superpowers/specs/2026-09-28-rev-a-master-plan-design.md`)

## Global Constraints

- Everything written into the repo is English (code, comments, docs, commit messages).
- **Never prefix a shell command with `cd`.** Run from the repo root with paths; no shell writes (use the Edit/Write tools); never chain `git add`/`git commit` behind `&&` or `;`.
- Commit trailer, on every commit: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- Generated files are never hand-edited; change the generator and re-run it.
- Hole sizes (spec §3.2): pot 7.0 mm, jack 6.0 mm, key 6.2 mm (Thonk low-profile button, "recommended cutout 6.2 mm"), LED 3.1 mm, SD slot 11 × 6 mm, mounting slots 5.2 × 3.2 mm.
- Minimum material between any two hole edges: **2.0 mm** (spec §4).
- Plate width: 304.8 mm nominal minus 0.4 mm, trimmed 0.2 mm off each side — an **assumption** (Doepfer A-100: 20 HP = 101.30 of 101.60 mm); no control moves for it.
- Cut-file convention (Formulor): pure blue `#0000ff` hairlines are cut; units mm.
- A test that cannot go red gets fixed: every new assertion is shown red once.
- Undo a RED-proof sabotage with the Edit tool (reverse the edit) or by re-running the generator — **never** `git checkout <file>`.
- Any task that touches `host/vcv/` and changes `src/generated_hw_panel.hpp` ends with `host/vcv/build-local.sh install`, and the report tells Bastian to restart Rack.

**Two spec amendments, decided with Bastian on 2026-09-29, that this plan implements (Task 5 writes them into the spec):**
1. Spec §3.1 (real cap radii, round keys) is **deferred** to the correction round after the grip test: `BODY_R["S"]` feeds the LEVEL band's x positions and `Y_B2K`'s derivation, so changing it moves controls; `kFfPadR` is also used by the big module's ENGINE latch. The cut file needs only hole sizes, which are independent.
2. The satellite LEDs `MODBTN_L`, `SHIFTBTN_L`, `SYNC_L` and `CEIL_L` leave only 0.85–0.95 mm of material to their key/jack hole (probe, 2026-09-29). They move out to a derived distance so 2.0 mm remains.

---

## File structure

| File | Responsibility |
|---|---|
| `host/vcv/res/gen_hw_panel.py` (modify) | Adds `HOLE_D`, `MIN_WEB`, `SAT_D`; the four satellite lamps derive their x from `SAT_D` |
| `host/vcv/res/test_hw_panel.py` (modify) | Satellite guard checks the new rule, four satellites |
| `host/vcv/res/gen_hw_cut.py` (create) | Hole list, cut SVG, print SVG — reads positions from `gen_hw_panel`, never places |
| `host/vcv/res/test_hw_cut.py` (create) | Guard for the three generated files |
| `host/vcv/res/FireflowHW-holes.json`, `FireflowHW-cut.svg`, `FireflowHW-print.svg` (generated, committed) | The outputs |
| `CMakeLists.txt` (modify) | `hw_cut_guard` ctest entry |
| `docs/hardware/grip-test.md` (create) | Checklist and measurement log skeleton |
| spec, envelope spec, roadmap (modify) | Amendments and status |

---

### Task 1: Satellite LEDs clear the real holes

**Files:**
- Modify: `host/vcv/res/gen_hw_panel.py` (after `CLASS_R` at line 219; `LIGHT_POS` at lines 476-483)
- Modify: `host/vcv/res/test_hw_panel.py:612-633` (`test_satellite_lamps_sit_at_anchor_radius_plus_1_5mm`)
- Regenerated: `host/vcv/res/FireflowHW.svg`, `host/vcv/src/generated_hw_panel.hpp`

**Interfaces:**
- Produces: `hw.HOLE_D: dict[str, float]` keyed by hardware class `"G","S","J","P","L"`; `hw.MIN_WEB = 2.0`; `hw.SAT_D: float` (6.7). Tasks 2-4 import these; they must not redefine them.

- [ ] **Step 1: Rewrite the satellite guard (RED first)**

Replace the whole function at `test_hw_panel.py:612-633` with:

```python
def test_satellite_lamps_clear_their_anchor_hole():
    """Satellite lamps sit on the jack row beside their key or jack, at ONE
    derived distance SAT_D: the larger anchor hole's radius, the minimum
    material web, and the LED hole's radius (Rev A P1 spec, 2026-09-29).
    Until 2026-09-29 they sat at the anchor's class radius + 1.5 mm = 5.5,
    which the real holes turned into a 0.85-0.95 mm web -- acrylic cracks
    there. SYNC_L joins the rule; it was a literal before."""
    SATELLITES = {
        "MODBTN_L": "MODBTN", "SHIFTBTN_L": "SHIFTBTN",
        "CEIL_L": "OUT_R", "SYNC_L": "CLOCK",
    }
    need = (max(hw.HOLE_D["P"], hw.HOLE_D["J"]) / 2 + hw.MIN_WEB
            + hw.HOLE_D["L"] / 2)
    check(hw.SAT_D >= need - 1e-9,
          f"SAT_D {hw.SAT_D} leaves less than {hw.MIN_WEB} mm "
          f"(needs {need:.2f})")
    by = {c.enum: c for c in hw.ALL_HW}
    checked = 0
    for lamp, anchor in SATELLITES.items():
        if lamp not in by or anchor not in by:
            check(False, f"{lamp} or its anchor {anchor} is missing from the panel")
            continue
        l, a = by[lamp], by[anchor]
        d = ((l.x - a.x) ** 2 + (l.y - a.y) ** 2) ** 0.5
        check(abs(d - hw.SAT_D) < 0.01,
              f"{lamp} is {d:.3f} mm from {anchor}, not SAT_D ({hw.SAT_D:.2f} mm)")
        checked += 1
    check(checked == 4, f"expected 4 satellites, checked {checked}")
```

- [ ] **Step 2: Run the guard, expect RED**

Run: `python host/vcv/res/test_hw_panel.py`
Expected: a traceback or FAIL naming `HOLE_D` / `SAT_D` (they do not exist yet). This is the RED.

- [ ] **Step 3: Add the hole table and the rule to the generator**

In `gen_hw_panel.py`, directly after the `CLASS_R = {...}` line (line 219), insert:

```python
# Panel holes of the real parts (Rev A P1 spec 2026-09-29 §3.2), by class.
# Datasheet values until the parts are measured (spec §6): Alpha 9 mm M7
# bushing, Thonkiconn, Thonk low-profile button ("cutout 6.2 mm"), 3 mm LED.
# gen_hw_cut.py cuts these; nothing here draws them.
HOLE_D = {"G": 7.0, "S": 7.0, "J": 6.0, "P": 6.2, "L": 3.1}
# Least material between two hole edges -- acrylic cracks at thinner webs.
MIN_WEB = 2.0
# Satellite lamp distance from its key or jack: the larger anchor hole, the
# web, the LED hole -- 3.1 + 2.0 + 1.55 = 6.65, rounded up to 6.7.
SAT_D = 6.7
```

Then replace the `LIGHT_POS = {...}` literal (lines 476-483) with:

```python
LIGHT_POS = {"REC_A_L": (108.50, Y_TOP), "REC_B_L": (W - 108.50, Y_TOP),
             # Jack-row satellites, all at SAT_D from their anchor (Rev A P1,
             # 2026-09-29; they were at the class radius + 1.5 mm = 5.5,
             # which left 0.85-0.95 mm of material to the real holes).
             # SYNC_L is inboard of CLOCK, SHFT's lamp inboard of SHFT,
             # MOD's inboard of MOD, the limiter lamp outboard of OUT_R.
             "SYNC_L":     (136.00 - SAT_D, JACK_Y),
             "MODBTN_L":   (290.80 - SAT_D, JACK_Y),
             "SHIFTBTN_L": (14.00 + SAT_D, JACK_Y),
             # Limiter lamp: jack-row satellite of OUT_R, outboard, same y as
             # MODBTN_L. Unsuffixed, so _twin_enum declares no mirror partner.
             "CEIL_L":     (JACK_POS["OUT_R"] + SAT_D, JACK_Y)}
```

Before editing, confirm the three anchors really sit at x = 136.00 (CLOCK), 290.80 (MODBTN) and 14.00 (SHIFTBTN): run
`python -c "import sys; sys.path.insert(0, r'host/vcv/res'); import gen_hw_panel as hw; b={c.enum:c for c in hw.ALL_HW}; print(b['CLOCK'].x, b['MODBTN'].x, b['SHIFTBTN'].x)"`
Expected: `136.0 290.8 14.0`. If any differs, use the anchor's own coordinate expression (e.g. `JACK_POS[...]` or the `HwOnly` literal) instead of the number, and say so in the report.

- [ ] **Step 4: Regenerate and run the panel guards**

Run: `python host/vcv/res/gen_hw_panel.py`
Expected: `wrote res/FireflowHW.svg and src/generated_hw_panel.hpp` and `params=75 inputs=12 outputs=6 lights=19  panel=60HP`.

Run: `python host/vcv/res/test_hw_panel.py`
Expected: `PASS -- hw panel guards ok`.

If any **other** guard goes red (a caption, a frame, the mirror check), do not change the guard to fit: stop and report which guard and which lamp, with the numbers — moving a lamp is a panel decision and Bastian decides it.

- [ ] **Step 5: Show the plate**

Render the plate:
`"/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" --headless --disable-gpu --force-device-scale-factor=2 --screenshot=<scratchpad>/p1-task1-plate.png --window-size=1220,520 --hide-scrollbars --default-background-color=ffffffff "file:///C:/Users/bernd/Documents/AI/FireFlow/host/vcv/res/FireflowHW.svg"`
Open the PNG (Read tool) and look at the jack row: four lamps beside SHFT, CLOCK, OUT R and MOD, none touching a caption. Put the PNG path in the report.

- [ ] **Step 6: VCV build and install**

Run: `host/vcv/build-local.sh install`
Expected: the script prints the installed plugin's size and timestamp. Quote it in the report, and tell Bastian to restart Rack.

- [ ] **Step 7: Commit**

```bash
git add host/vcv/res/gen_hw_panel.py host/vcv/res/test_hw_panel.py host/vcv/res/FireflowHW.svg host/vcv/src/generated_hw_panel.hpp
```
```bash
git commit -m "hw(panel): satellite LEDs keep 2 mm of material to their key or jack hole

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: The hole list and its guard, in ctest

**Files:**
- Create: `host/vcv/res/gen_hw_cut.py`
- Create: `host/vcv/res/test_hw_cut.py`
- Create (generated): `host/vcv/res/FireflowHW-holes.json`
- Modify: `CMakeLists.txt` (after the `hw_panel_guard` block, around line 374)

**Interfaces:**
- Consumes: `hw.ALL_HW`, `hw.hw_class(enum)`, `hw.HOLE_D`, `hw.MIN_WEB`, `hw.W`, `hw.Hh`, `hw.HP`, `hw.SD_X/SD_Y/SD_W/SD_H`, `hw.KEEP_TOP`, `hw.KEEP_BOT`, `hw.mm(v)`, `hw._write_atomic(path, text)`, `gp.MM_PER_HP`.
- Produces: `cut.holes() -> list[dict]` (keys `id, kind, x_mm, y_mm` and either `d_mm` or `w_mm, h_mm`); `cut.holes_json() -> str`; `cut.PLATE_W`, `cut.TRIM`, `cut.KIND`; `cut.write_all(here) -> list[str]` (paths written). Tasks 3-4 extend `write_all`.

- [ ] **Step 1: Write the guard (RED first)**

Create `host/vcv/res/test_hw_cut.py`:

```python
"""Guard for the Rev A P1 cut outputs (spec 2026-09-29-rev-a-p1-panel-parts
§4): the hole list, the laser cut file and the 1:1 print sheet. Positions
come from gen_hw_panel.py; this checks that every control got exactly one
hole where it sits, that the acrylic keeps its webs, and that the committed
files are what the generator makes.

No pytest in this environment -- plain checks, exit code says it all.
Run from the repo root:  python host/vcv/res/test_hw_cut.py
"""
import json, math, os, re, sys
import gen_hw_panel as hw
import gen_hw_cut as cut

HERE = os.path.dirname(os.path.abspath(__file__))
FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def _committed_holes():
    with open(os.path.join(HERE, "FireflowHW-holes.json"), encoding="utf-8") as f:
        return json.load(f)["holes"]


def _extent(h):
    """(x0, y0, x1, y1) of a hole's bounding box, mm."""
    if "d_mm" in h:
        r = h["d_mm"] / 2
        return h["x_mm"] - r, h["y_mm"] - r, h["x_mm"] + r, h["y_mm"] + r
    return (h["x_mm"] - h["w_mm"] / 2, h["y_mm"] - h["h_mm"] / 2,
            h["x_mm"] + h["w_mm"] / 2, h["y_mm"] + h["h_mm"] / 2)


def web(a, b):
    """Material between two hole edges, mm. Circles exactly; slots as their
    bounding rectangles, which only ever under-reports the web."""
    if "d_mm" in a and "d_mm" in b:
        return (math.hypot(a["x_mm"] - b["x_mm"], a["y_mm"] - b["y_mm"])
                - a["d_mm"] / 2 - b["d_mm"] / 2)
    if "d_mm" in b:
        a, b = b, a
    if "d_mm" in a:
        x0, y0, x1, y1 = _extent(b)
        dx = max(x0 - a["x_mm"], 0.0, a["x_mm"] - x1)
        dy = max(y0 - a["y_mm"], 0.0, a["y_mm"] - y1)
        return math.hypot(dx, dy) - a["d_mm"] / 2
    ax0, ay0, ax1, ay1 = _extent(a)
    bx0, by0, bx1, by1 = _extent(b)
    dx = max(bx0 - ax1, ax0 - bx1, 0.0)
    dy = max(by0 - ay1, ay0 - by1, 0.0)
    return math.hypot(dx, dy)


def test_every_control_has_one_hole_where_it_sits():
    holes = _committed_holes()
    for c in hw.ALL_HW:
        at = [h for h in holes
              if abs(h["x_mm"] - c.x) < 0.01 and abs(h["y_mm"] - c.y) < 0.01]
        check(len(at) == 1, f"{c.enum} has {len(at)} holes at its coordinate")
        if len(at) == 1:
            want = cut.KIND[hw.hw_class(c.enum)]
            check(at[0]["kind"] == want,
                  f"{c.enum}'s hole is a {at[0]['kind']}, not a {want}")


def test_hole_counts():
    """Counted 2026-09-28 from the generator: 70 pot positions (ATTACK and
    STAGES share a knob per deck), 4 keys, 18 jacks, 19 LEDs, the SD slot and
    four mounting slots. When the plate changes, this goes red on purpose --
    update the numbers in the same commit as the plate."""
    kinds = {}
    for h in _committed_holes():
        kinds[h["kind"]] = kinds.get(h["kind"], 0) + 1
    want = {"pot": 70, "key": 4, "jack": 18, "led": 19, "sd": 1, "mount": 4}
    check(kinds == want, f"hole counts {kinds}, expected {want}")


def test_every_web_holds():
    holes = _committed_holes()
    for i, a in enumerate(holes):
        for b in holes[i + 1:]:
            w = web(a, b)
            check(w >= hw.MIN_WEB - 1e-6,
                  f"{a['id']} and {b['id']} leave {w:.2f} mm of material, "
                  f"under {hw.MIN_WEB}")


def test_holes_stay_on_the_plate_and_off_the_rails():
    x_lo = cut.TRIM + hw.MIN_WEB
    x_hi = cut.TRIM + cut.PLATE_W - hw.MIN_WEB
    for h in _committed_holes():
        x0, y0, x1, y1 = _extent(h)
        check(x0 >= x_lo - 1e-6 and x1 <= x_hi + 1e-6,
              f"{h['id']} reaches within {hw.MIN_WEB} mm of a side edge")
        if h["kind"] == "mount":
            continue
        check(y0 >= hw.KEEP_TOP - 1e-6 and y1 <= hw.KEEP_BOT + 1e-6,
              f"{h['id']} enters a rail zone ({y0:.2f}..{y1:.2f})")


def test_committed_files_match_the_generator():
    for name, fn in cut.OUTPUTS:
        path = os.path.join(HERE, name)
        if not os.path.exists(path):
            FAILS.append(f"{name} is missing -- run res/gen_hw_cut.py")
            continue
        with open(path, encoding="utf-8") as f:
            on_disk = f.read()
        check(on_disk == fn(),
              f"{name} differs from the generator's output -- it was "
              "hand-edited, or the generator changed without re-running it")


def main():
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print(f"FAIL ({len(FAILS)}):")
        for f in FAILS:
            print("  -", f)
        return 1
    print("PASS -- hw cut guards ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run it, expect RED**

Run: `python host/vcv/res/test_hw_cut.py`
Expected: `ModuleNotFoundError: No module named 'gen_hw_cut'`.

- [ ] **Step 3: Write the generator**

Create `host/vcv/res/gen_hw_cut.py`:

```python
#!/usr/bin/env python3
"""Cut file, print sheet and hole list for the Rev A P1 acrylic plate
(spec docs/superpowers/specs/2026-09-29-rev-a-p1-panel-parts-design.md §3).

Every coordinate comes from gen_hw_panel.py; this file adds hole sizes only
through hw.HOLE_D and places nothing. P4 (the Rev A board) places its parts
from FireflowHW-holes.json, so plate and board share one source.

Run from the repo root:  python host/vcv/res/gen_hw_cut.py
"""
import json, os
import gen_panel as gp
import gen_hw_panel as hw

KIND = {"G": "pot", "S": "pot", "J": "jack", "P": "key", "L": "led"}

# Real Eurorack panels are cut a little under HP x 5.08 so neighbours fit
# (Doepfer A-100: 20 HP = 101.30 of 101.60 mm). ASSUMPTION for 60 HP: 0.4 mm
# under, trimmed evenly off both sides so no control moves.
PLATE_W = hw.W - 0.4
TRIM = (hw.W - PLATE_W) / 2
# Mounting slots: DIY convention, 7.5 mm in from the plate's left edge and
# 3.0 mm from top and bottom, horizontal pitch a whole number of HP.
# ASSUMPTION until checked against the Palette's rails.
MOUNT_X = (TRIM + 7.5, TRIM + 7.5 + (hw.HP - 3) * gp.MM_PER_HP)
MOUNT_Y = (3.0, hw.Hh - 3.0)
MOUNT_W, MOUNT_H = 5.2, 3.2


def _r3(v):
    return round(v, 3)


def holes():
    """One hole per distinct control position, then the SD slot and the four
    mounting slots. ATTACK_x and STAGES_x share a knob, hence one hole."""
    out, seen = [], set()
    for c in hw.ALL_HW:
        key = (_r3(c.x), _r3(c.y))
        if key in seen:
            continue
        seen.add(key)
        cls = hw.hw_class(c.enum)
        out.append({"id": c.enum, "kind": KIND[cls], "x_mm": _r3(c.x),
                    "y_mm": _r3(c.y), "d_mm": hw.HOLE_D[cls]})
    out.append({"id": "SD", "kind": "sd", "x_mm": _r3(hw.SD_X),
                "y_mm": _r3(hw.SD_Y), "w_mm": hw.SD_W, "h_mm": hw.SD_H})
    n = 0
    for y in MOUNT_Y:
        for x in MOUNT_X:
            n += 1
            out.append({"id": f"MOUNT{n}", "kind": "mount", "x_mm": _r3(x),
                        "y_mm": _r3(y), "w_mm": MOUNT_W, "h_mm": MOUNT_H})
    return out


def holes_json():
    doc = {"generated_by": "host/vcv/res/gen_hw_cut.py -- do not edit by hand",
           "units": "mm, origin top-left of the nominal 60 HP plate, y down",
           "plate": {"x0_mm": _r3(TRIM), "w_mm": _r3(PLATE_W), "h_mm": hw.Hh},
           "holes": holes()}
    return json.dumps(doc, indent=1) + "\n"


OUTPUTS = [("FireflowHW-holes.json", holes_json)]


def write_all(here):
    written = []
    for name, fn in OUTPUTS:
        path = os.path.join(here, name)
        hw._write_atomic(path, fn())
        written.append(path)
    return written


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    for p in write_all(here):
        print("wrote", os.path.relpath(p))
    print(f"holes={len(holes())} plate={PLATE_W:.1f}x{hw.Hh}mm")
```

- [ ] **Step 4: Generate and run the guard**

Run: `python host/vcv/res/gen_hw_cut.py`
Expected: `wrote ...FireflowHW-holes.json` and `holes=116 plate=304.4x128.5mm`.

Run: `python host/vcv/res/test_hw_cut.py`
Expected: `PASS -- hw cut guards ok`. (If `test_every_web_holds` fails, Task 1 is not in; stop.)

- [ ] **Step 5: Show each assertion red once**

With the Edit tool, change `"x_mm": 14.0` of the `SHIFTBTN` entry in `FireflowHW-holes.json` to `"x_mm": 14.5`. Run the guard.
Expected: FAIL listing `SHIFTBTN has 0 holes at its coordinate` and `FireflowHW-holes.json differs from the generator's output`. Reverse the edit with the Edit tool.

In `gen_hw_panel.py` change `HOLE_D`'s `"L": 3.1` to `"L": 6.0`, re-run `gen_hw_cut.py`, run the guard.
Expected: FAIL naming at least one LED pair under 2.0 mm. Reverse the edit, re-run `gen_hw_cut.py`, run the guard: PASS.

In `test_hole_counts`, change `"pot": 70` to `"pot": 71`, run: FAIL on the counts. Reverse.

For the rail check: in `gen_hw_cut.py` temporarily append `out.append({"id": "X", "kind": "led", "x_mm": 150.0, "y_mm": 8.0, "d_mm": 3.1})` just before `return out`, regenerate, run: FAIL `X enters a rail zone`. Remove the line, regenerate, run: PASS.

Paste each FAIL line into the report.

- [ ] **Step 6: Wire it into ctest**

In `CMakeLists.txt`, directly after the `add_test( NAME hw_panel_guard ... )` block, add:

```cmake
# Rev A P1: the acrylic cut file, the print sheet and the hole list the board
# will be placed from (spec docs/superpowers/specs/2026-09-29-rev-a-p1-
# panel-parts-design.md §4). Same runner shape as the two guards above.
add_test(
    NAME hw_cut_guard
    COMMAND ${Python3_EXECUTABLE} res/test_hw_cut.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/host/vcv
)
```

Run: `source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
Run: `ctest --test-dir build -R "hw_panel_guard|hw_cut_guard" --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 2`.

- [ ] **Step 7: Commit**

```bash
git add host/vcv/res/gen_hw_cut.py host/vcv/res/test_hw_cut.py host/vcv/res/FireflowHW-holes.json CMakeLists.txt
```
```bash
git commit -m "hw(cut): the plate's hole list, one source for the acrylic and the board

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The laser cut file

**Files:**
- Modify: `host/vcv/res/gen_hw_cut.py`
- Modify: `host/vcv/res/test_hw_cut.py`
- Create (generated): `host/vcv/res/FireflowHW-cut.svg`

**Interfaces:**
- Consumes: `cut.holes()`, `cut.PLATE_W`, `cut.TRIM`, `hw.mm`, `hw.W`, `hw.Hh`.
- Produces: `cut.shape(h, stroke, width) -> str` (one SVG element per hole); `cut.cut_svg() -> str`; `cut.BLUE = "#0000ff"`. Task 4 reuses `shape`.

- [ ] **Step 1: Add the guard (RED first)**

Append to `test_hw_cut.py`, above `def main():`:

```python
def test_cut_file_is_exactly_the_holes():
    """Formulor cuts pure-blue hairlines; anything else would be engraved or
    ignored. One outline plus one shape per hole, nothing more."""
    path = os.path.join(HERE, "FireflowHW-cut.svg")
    if not os.path.exists(path):
        FAILS.append("FireflowHW-cut.svg is missing -- run res/gen_hw_cut.py")
        return
    svg = open(path, encoding="utf-8").read()
    check('width="304.800mm"' in svg and 'height="128.500mm"' in svg,
          "cut file is not in mm at the nominal plate size")
    strokes = set(re.findall(r'stroke="([^"]+)"', svg))
    check(strokes == {cut.BLUE}, f"cut file uses strokes {strokes}, not only {cut.BLUE}")
    check(len(re.findall(r'fill="(?!none")', svg)) == 0, "cut file has a filled shape")
    shapes = re.findall(r"<(circle|rect)\b", svg)
    holes = cut.holes()
    check(len(shapes) == len(holes) + 1,
          f"cut file has {len(shapes)} shapes, expected {len(holes)} holes + 1 outline")
    for h in holes:
        check(cut.shape(h, cut.BLUE, "0.01") in svg, f"{h['id']} is not cut")
```

Run: `python host/vcv/res/test_hw_cut.py`
Expected: FAIL `FireflowHW-cut.svg is missing`.

- [ ] **Step 2: Add the cut file to the generator**

In `gen_hw_cut.py`, after `holes_json()`, add:

```python
BLUE = "#0000ff"   # Formulor: RGB 0,0,255 hairlines are cut


def shape(h, stroke, width):
    """One hole as one SVG element: a circle, or a rectangle whose rounded
    ends make the mounting slots stadium-shaped."""
    if "d_mm" in h:
        return (f'<circle cx="{hw.mm(h["x_mm"])}" cy="{hw.mm(h["y_mm"])}" '
                f'r="{hw.mm(h["d_mm"] / 2)}" fill="none" stroke="{stroke}" '
                f'stroke-width="{width}"/>')
    rx = h["h_mm"] / 2 if h["kind"] == "mount" else 0.0
    return (f'<rect x="{hw.mm(h["x_mm"] - h["w_mm"] / 2)}" '
            f'y="{hw.mm(h["y_mm"] - h["h_mm"] / 2)}" width="{hw.mm(h["w_mm"])}" '
            f'height="{hw.mm(h["h_mm"])}" rx="{hw.mm(rx)}" fill="none" '
            f'stroke="{stroke}" stroke-width="{width}"/>')


def cut_svg():
    P = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{hw.mm(hw.W)}mm" '
         f'height="{hw.mm(hw.Hh)}mm" viewBox="0 0 {hw.mm(hw.W)} {hw.mm(hw.Hh)}">',
         f'<rect x="{hw.mm(TRIM)}" y="0.000" width="{hw.mm(PLATE_W)}" '
         f'height="{hw.mm(hw.Hh)}" fill="none" stroke="{BLUE}" stroke-width="0.01"/>']
    P += [shape(h, BLUE, "0.01") for h in holes()]
    P.append("</svg>")
    return "\n".join(P) + "\n"
```

And extend the outputs list:

```python
OUTPUTS = [("FireflowHW-holes.json", holes_json),
           ("FireflowHW-cut.svg", cut_svg)]
```

- [ ] **Step 3: Generate, run, show red once**

Run: `python host/vcv/res/gen_hw_cut.py` then `python host/vcv/res/test_hw_cut.py`
Expected: PASS.

RED proof: in `cut_svg()` temporarily change the outline's `stroke="{BLUE}"` to `stroke="#ff0000"`, regenerate, run: FAIL `cut file uses strokes`. Reverse, regenerate, PASS. Paste the FAIL line into the report.

- [ ] **Step 4: Commit**

```bash
git add host/vcv/res/gen_hw_cut.py host/vcv/res/test_hw_cut.py host/vcv/res/FireflowHW-cut.svg
```
```bash
git commit -m "hw(cut): the acrylic plate's laser cut file

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: The 1:1 print sheet

**Files:**
- Modify: `host/vcv/res/gen_hw_cut.py`
- Modify: `host/vcv/res/test_hw_cut.py`
- Create (generated): `host/vcv/res/FireflowHW-print.svg`

**Interfaces:**
- Consumes: `hw.svg() -> str` (the full plate artwork, root `width="304.800mm"`, ends with `</svg>`), `cut.shape`, `cut.holes`, `cut.TRIM`, `cut.PLATE_W`.
- Produces: `cut.print_svg() -> str`; `cut.SCALE_X0`, `cut.SCALE_Y`.

- [ ] **Step 1: Add the guard (RED first)**

Append to `test_hw_cut.py`, above `def main():`:

```python
def test_print_sheet_is_true_to_scale():
    """The print goes under the clear acrylic. It must be the real plate
    artwork, with every hole outlined for cutting out, and a 100 mm bar the
    printer's scaling can be checked against with a ruler."""
    path = os.path.join(HERE, "FireflowHW-print.svg")
    if not os.path.exists(path):
        FAILS.append("FireflowHW-print.svg is missing -- run res/gen_hw_cut.py")
        return
    svg = open(path, encoding="utf-8").read()
    check(svg.startswith(hw.svg().rstrip()[:-len("</svg>")]),
          "print sheet does not start with the plate artwork")
    m = re.search(r'<line id="scale100" x1="([\d.]+)" y1="[\d.]+" '
                  r'x2="([\d.]+)"', svg)
    check(m is not None, "print sheet has no 100 mm scale bar")
    if m:
        check(abs(float(m.group(2)) - float(m.group(1)) - 100.0) < 1e-6,
              "scale bar is not 100 mm long")
    for h in cut.holes():
        check(cut.shape(h, cut.PRINT_INK, "0.25") in svg,
              f"{h['id']} is not outlined on the print sheet")
```

Run: `python host/vcv/res/test_hw_cut.py`
Expected: FAIL `FireflowHW-print.svg is missing`.

- [ ] **Step 2: Add the print sheet to the generator**

In `gen_hw_cut.py`, after `cut_svg()`, add:

```python
PRINT_INK = "#ffffff"   # hole outlines and scale bar, light on the dark plate
# 100 mm scale bar in the bottom rail zone, clear of the mounting slots.
SCALE_X0, SCALE_Y = 102.4, 123.0


def print_svg():
    """The plate artwork at 1:1, every hole outlined to be cut out with a
    knife, the trimmed plate edge, and a 100 mm bar to check the printer."""
    base = hw.svg().rstrip()
    body = base[:-len("</svg>")]
    P = [body, '<g id="print-overlay">',
         f'<rect x="{hw.mm(TRIM)}" y="0.000" width="{hw.mm(PLATE_W)}" '
         f'height="{hw.mm(hw.Hh)}" fill="none" stroke="{PRINT_INK}" '
         f'stroke-width="0.25"/>']
    P += [shape(h, PRINT_INK, "0.25") for h in holes()]
    P.append(f'<line id="scale100" x1="{hw.mm(SCALE_X0)}" y1="{hw.mm(SCALE_Y)}" '
             f'x2="{hw.mm(SCALE_X0 + 100.0)}" y2="{hw.mm(SCALE_Y)}" '
             f'stroke="{PRINT_INK}" stroke-width="0.3"/>')
    P.append(f'<text x="{hw.mm(SCALE_X0 + 50.0)}" y="{hw.mm(SCALE_Y + 3.0)}" '
             f'fill="{PRINT_INK}" text-anchor="middle" font-family="monospace" '
             f'font-size="2.2">100 mm -- print at 100 %, check with a ruler</text>')
    P.append("</g>")
    P.append("</svg>")
    return "\n".join(P) + "\n"
```

And extend the outputs list:

```python
OUTPUTS = [("FireflowHW-holes.json", holes_json),
           ("FireflowHW-cut.svg", cut_svg),
           ("FireflowHW-print.svg", print_svg)]
```

- [ ] **Step 3: Generate, run, show red once**

Run: `python host/vcv/res/gen_hw_cut.py` then `python host/vcv/res/test_hw_cut.py`
Expected: PASS.

RED proof: change `SCALE_X0 + 100.0` in the `x2=` expression to `SCALE_X0 + 99.0`, regenerate, run: FAIL `scale bar is not 100 mm long`. Reverse, regenerate, PASS. Paste the FAIL line into the report.

- [ ] **Step 4: Look at both sheets**

Render each file:
`"/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" --headless --disable-gpu --force-device-scale-factor=2 --screenshot=<scratchpad>/p1-print.png --window-size=1220,520 --hide-scrollbars --default-background-color=ffffffff "file:///C:/Users/bernd/Documents/AI/FireFlow/host/vcv/res/FireflowHW-print.svg"`
and the same for `FireflowHW-cut.svg` into `p1-cut.png`.
Open both PNGs (Read tool). Check: every knob, jack, key and LED has a white outline on the print; the scale bar sits in the bottom strip and crosses no slot; the cut render shows only blue hairlines. Put both PNG paths in the report.

- [ ] **Step 5: Commit**

```bash
git add host/vcv/res/gen_hw_cut.py host/vcv/res/test_hw_cut.py host/vcv/res/FireflowHW-print.svg
```
```bash
git commit -m "hw(cut): the 1:1 print sheet that goes under the clear acrylic

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: Grip-test log, spec amendments, status

**Files:**
- Create: `docs/hardware/grip-test.md`
- Modify: `docs/superpowers/specs/2026-09-29-rev-a-p1-panel-parts-design.md` (§3 item 1, and §1's "Rejected: 8 mm square keycaps" bullet stays)
- Modify: `docs/roadmap.md` (the M6 section's newest entry)

- [ ] **Step 1: Write the grip-test log skeleton**

Create `docs/hardware/grip-test.md`:

```markdown
# Grip test — the acrylic plate (Rev A P1)

Spec: [`2026-09-29-rev-a-p1-panel-parts-design.md`](../superpowers/specs/2026-09-29-rev-a-p1-panel-parts-design.md) §5–§6.
Plate: clear 3 mm acrylic cut from `host/vcv/res/FireflowHW-cut.svg`, with
`FireflowHW-print.svg` printed at 100 % underneath (check the 100 mm bar with
a ruler before cutting the print).

## Measurements (spec §6)

| Date | What | Result |
|---|---|---|
| | Alpha support lugs in the coupon's RD901F slots | |
| | Pot bushing thread and length vs 3 mm acrylic / 2 mm aluminium | |
| | Jack and key thread vs 3 mm | |
| | Jack nut and key nut outer diameter (do they cover the satellite LED holes at 6.7 mm?) | |
| | Panel-to-board height, pot and jack seated on a board | |
| | Alpha anti-rotation tab: present? where? | |
| | Actual plate width of a bought 60 HP blank, if one is at hand (the cut assumes 304.4 mm) | |

## Checklist (spec §5)

| # | Check | Result | Fix (position / size / legend) |
|---|---|---|---|
| 1 | Pinch two neighbouring knobs at once, every group | | |
| 2 | Turn each knob without brushing a neighbour's cap | | |
| 3 | All 18 jacks patched with real cables: what gets covered | | |
| 4 | Every legend readable from playing distance, through the print | | |
| 5 | All four keys: reach, accidental presses | | |

Every fix goes into `host/vcv/res/gen_hw_panel.py`, never into a generated
file. The freeze tag `panel-freeze-2026-11-06` goes on the generator commit
that passes this list.
```

- [ ] **Step 2: Amend the spec**

In `docs/superpowers/specs/2026-09-29-rev-a-p1-panel-parts-design.md` §3, replace item 1 ("**Body radii follow the real caps.** ...") with:

```markdown
1. ~~**Body radii follow the real caps.**~~ **Deferred 2026-09-29 (Bastian)**
   to the correction round after the grip test, before the freeze.
   `BODY_R["S"]` feeds the LEVEL band's x positions and the derivation of
   `Y_B2K`, so shrinking it to the Micro Knob's 3.85 moves controls; and the
   header's `kFfPadR` is also used by the big module's ENGINE latch. The cut
   file needs only hole sizes, which are independent, so the drawing gets one
   round, after the test, instead of two.
```

And after §4's bullet list, add:

```markdown
**Found by the guard before anything was cut (2026-09-29):** the four jack-row
satellite LEDs (`MODBTN_L`, `SHIFTBTN_L`, `SYNC_L`, `CEIL_L`) sat at their
anchor's class radius + 1.5 mm, leaving 0.85–0.95 mm of material to the real
key and jack holes. They now sit at `SAT_D` = 6.7 mm (key hole 3.1 + web
2.0 + LED hole 1.55, rounded up). Whether the jack and key nuts cover them is
a §6 measurement.
```

- [ ] **Step 3: Roadmap entry**

In `docs/roadmap.md`, in the M6 section, directly above the entry that starts `**2026-09-28, last — Rev A is one board`, add:

```markdown
**2026-09-29 — P1's generator side is in: the acrylic plate can be ordered.**
`host/vcv/res/gen_hw_cut.py` writes the laser cut file, a 1:1 print sheet for
under the clear plate, and `FireflowHW-holes.json`, the hole list the Rev A
board will be placed from; `hw_cut_guard` in ctest checks one hole per
control, 2 mm of material everywhere, and the files against the generator.
The guard's first run moved four LEDs: the jack-row satellites left under
1 mm of acrylic to their key or jack. Parts (all Thonk: genuine Alpha 9 mm
T18 B10K, 1900H and Micro Knob caps, Thonkiconn, low-profile buttons, 3 mm
LEDs; no standoffs) and the plate are ordered by 9 Oct; the grip test log is
[`docs/hardware/grip-test.md`](hardware/grip-test.md). Spec
`docs/superpowers/specs/2026-09-29-rev-a-p1-panel-parts-design.md`.
```

- [ ] **Step 4: Commit**

```bash
git add docs/hardware/grip-test.md docs/superpowers/specs/2026-09-29-rev-a-p1-panel-parts-design.md docs/roadmap.md
```
```bash
git commit -m "docs: Rev A P1 grip-test log, the deferred cap redraw, roadmap entry

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## After the plan (not tasks — Bastian's actions)

1. Upload `host/vcv/res/FireflowHW-cut.svg` to formulor.de, clear acrylic GS 3 mm, read the price back.
2. Fill the Thonk cart per spec §1, read the total back, order.
3. Print `FireflowHW-print.svg` at 100 % on A3, check the 100 mm bar with a ruler.
