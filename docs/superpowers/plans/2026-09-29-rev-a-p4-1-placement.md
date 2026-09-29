# Rev A P4-1 Placement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generate the placed, unrouted Rev A board
(`hardware/reva/kicad/reva.kicad_pcb`) from P3's netlist and P1's hole list.
Every placement check is green, or listed as a known panel violation.
Renders are produced every run.

**Architecture:**
- `hardware/gen/place.py` holds the board-independent geometry: boxes, the
  hole point, the body box and the spiral first-fit search.
- `hardware/reva/place.py` builds the Rev A board in a fixed order: outline,
  front panel parts, J_SD, module, J_PWR, then the SMD parts by anchor.
- `hardware/reva/place_check.py` runs the checks and the report. It carries
  the P4a-style sabotage modes, each proving that a check can go red.
- A ctest guard rebuilds the board twice, compares it byte for byte with the
  committed file, and runs every sabotage mode.

**Tech Stack:** KiCad 10.0.5 pcbnew Python (KIPY), kicad-cli (DRC, render),
system Python 3.14 for ctest entry points, which re-exec themselves under KIPY.

**Spec:** `docs/superpowers/specs/2026-09-29-rev-a-p4-1-placement-design.md`,
including its two planning amendments (§4.2 SD source, §4.3 USB clearance).

## Global Constraints

- **Runtimes:**
  - KIPY = `/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe`
    (Python 3.11 with pcbnew).
  - System `python` is 3.14 and has no pcbnew.
  - `pytest` is not installed. Guards are plain scripts; the exit code is the
    verdict.
- **Shell:**
  - Never prefix a shell command with `cd`.
  - No shell writes and no writes behind `&&`. Use the Write and Edit tools
    for files.
  - Long compounds go in a script file in the scratchpad.
- **Repository:**
  - Everything written into the repo is English.
  - Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **Units and coordinates:** mm, y down. Board coordinates equal panel
  coordinates: origin at the plate's top-left, as in
  `host/vcv/res/FireflowHW-holes.json`.
- **Outline:** x 2.0–302.8, y 9.25–119.25 (300.8 × 110 mm). 4 copper
  layers: In1.Cu GND, In2.Cu SM_3V3.
- **Clearances:**
  - copper-to-edge 0.5 mm, set as `m_CopperEdgeClearance`
  - pad clearance 0.2 mm
  - SMD courtyard inset from the outline 1.0 mm
- **Rotations:**
  - Pots 270 (pins north), except the top pot row: 90 (pins south).
  - Jacks 0. Keys 0.
  - LEDs: the first of 0, 90, 180, 270 that passes. J_SD 0.
  - Module 0 or 180, the nearer the centre.
- **Distances:**
  - Decoupling: 100 nF pad 1 within 2.0 mm of its IC's VCC pad.
    C_SD1 likewise to J_SD pad 4.
  - J_PWR courtyard ≥ 35 mm from the module shadow.
- **Coupon:** `hardware/coupon` must keep rebuilding byte-identically
  (`coupon_*` ctest). Do not change `kipcb.new_board()` defaults.
- **Known traps (probed):**
  - `PCB_VIA::GetWidth()` without a layer hangs pcbnew; use
    `GetWidth(pcbnew.F_Cu)`.
  - A flipped footprint's pads report F.Cu from `GetLayerName()`; use
    `IsOnLayer`.
  - kicad-cli overwrites an existing report.
  - `KIID.SeedGenerator` (called in `new_board`) makes the board byte-stable.
    Create nothing pcbnew-side before `new_board()`.
  - `SaveBoard` writes a `.kicad_pro` beside the board. Always save into
    `hardware/reva/out/` and copy only the `.kicad_pcb` into
    `hardware/reva/kicad/`, or `reva_build_guard` goes stale.
- **Renders:** every run renders front and back into `hardware/reva/out/`.
  The implementer looks at them before the next iteration; the report to the
  controller names what they show.
- **Verification:** `ctest --test-dir build -R "hw_gen|reva|coupon" --output-on-failure`
  must pass at the end of every task. ctest does not build; these guards are
  scripts and need no build.

## File structure

| File | Responsibility |
|---|---|
| `hardware/gen/place.py` (new) | Box math, `hole_point`, `body_box`, `pad_boxes`, `courtyard_box`, `spiral`, `first_fit` |
| `hardware/gen/test_place.py` (new) | Guard for gen/place.py (KIPY re-exec) |
| `hardware/gen/kipcb.py` (modify) | Footprint search also looks in `hardware/lib/FireFlow` |
| `hardware/reva/sd_footprint.py` (new) | Writes `hardware/lib/FireFlow/FireFlow.pretty/SD_Yamaichi_PJS008U-3000-0.kicad_mod` |
| `hardware/reva/test_sd_footprint.py` (new) | Guard: generated == committed, pads and drills as sourced |
| `hardware/reva/parts.py`, `build.py` (modify) | J_SD gets the new footprint; FireFlow library registered |
| `hardware/reva/place.py` (new) | Builds the board; CLI |
| `hardware/reva/place_check.py` (new) | Checks, known list, report, sabotage |
| `hardware/reva/test_place.py` (new) | Guard: determinism, committed board, green, every sabotage red |
| `hardware/reva/kicad/reva.kicad_pcb` (new, generated) | The placed board |
| `hardware/reva/spike/` (delete) | Announced in the routing report |
| `CMakeLists.txt` (modify) | `hw_gen_place_guard`, `reva_sd_footprint_guard`, `reva_place_guard` |
| `.gitignore` (modify) | Drop `hardware/reva/spike/out/` |

---

### Task 1: Shared placement geometry, spike removed

**Files:**
- Create: `hardware/gen/place.py`
- Create: `hardware/gen/test_place.py`
- Modify: `CMakeLists.txt` (after the `hw_gen_route_guard` entry)
- Modify: `.gitignore` (remove the line `hardware/reva/spike/out/`)
- Delete: `hardware/reva/spike/` (whole directory, via `git rm -r`)

**Interfaces:**
- Produces, used by Tasks 3–6, all in mm with y down:
  - `box(bb) -> (l, t, r, b)`
  - `grow(b, d)`
  - `overlaps(a, b) -> bool`: strict; touching boxes do not overlap.
  - `gap(a, b) -> float`: Euclidean gap between boxes, 0 if they overlap.
  - `inside(b, outer) -> bool`
  - `hole_point(fp) -> (x, y)`
  - `body_box(fp) -> (l, t, r, b)`
  - `pad_boxes(fp) -> [(number:str, box)]`
  - `courtyard_box(fp) -> box | None`
  - `spiral(cx, cy, step, rmax)`: a generator of (x, y).
  - `first_fit(fp, target, blocked, inner, step, rmax, rotations=(0, 90, 180, 270), accept=None) -> (x, y, rot)`
    moves `fp` and appends its courtyard box to `blocked`. It raises
    `ValueError` naming the ref when nothing fits.

- [ ] **Step 1: Write the failing test** `hardware/gen/test_place.py`

```python
#!/usr/bin/env python3
"""Guard for hardware/gen/place.py. Plain script; the exit code is the
verdict. Needs pcbnew, so under the system Python (ctest) it re-runs itself
under KiCad's Python."""
import os
import subprocess
import sys

HW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, HW)
try:
    import pcbnew  # noqa: F401
except ImportError:
    from gen import ksexp
    kipy = os.path.join(ksexp.KICAD_ROOT, "bin", "python.exe")
    sys.exit(subprocess.call([kipy, os.path.abspath(__file__)]))

import pcbnew                 # noqa: E402
from gen import kipcb         # noqa: E402
from gen import place as PL   # noqa: E402

FAILS = []


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def near(a, b, tol=0.01):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


def placed(lib_id, rot=0, side="F"):
    board = pcbnew.BOARD()
    fp = kipcb.footprint(lib_id)
    fp.SetPosition(kipcb._pt(0.0, 0.0))
    board.Add(fp)
    if side == "B":
        fp.Flip(kipcb._pt(0.0, 0.0), False)
    fp.SetOrientationDegrees(rot)
    return board, fp


def main():
    check(PL.overlaps((0, 0, 2, 2), (1, 1, 3, 3)), "overlaps: crossing boxes")
    check(not PL.overlaps((0, 0, 1, 1), (1, 0, 2, 1)), "overlaps: touching is not overlapping")
    check(abs(PL.gap((0, 0, 1, 1), (4, 5, 6, 6)) - 5.0) < 1e-9, "gap: 3-4-5 diagonal")
    check(PL.gap((0, 0, 2, 2), (1, 1, 3, 3)) == 0.0, "gap: overlapping boxes have 0")
    check(PL.inside((1, 1, 2, 2), (0, 0, 3, 3)) and not PL.inside((1, 1, 4, 2), (0, 0, 3, 3)),
          "inside: in and out")

    pts = list(PL.spiral(0.0, 0.0, 1.0, 2.0))
    check(pts[0] == (0.0, 0.0) and len(pts) == 25, "spiral: centre first, 5x5 = 25 points (%d)" % len(pts))
    check(max(abs(x) + abs(y) for x, y in pts[1:9]) <= 2.0 and all(max(abs(x), abs(y)) == 1.0 for x, y in pts[1:9]),
          "spiral: ring 1 comes before ring 2")

    # LED_D3.0mm: pad 1 at the origin, pad 2 at +2.54; the F.Fab body circle at +1.27.
    _b, led = placed("LED_THT:LED_D3.0mm")
    check(near(PL.hole_point(led), (1.27, 0.0)), "hole_point: LED body circle centre %s" % (PL.hole_point(led),))
    # The Thonk key has no F.Fab: courtyard centre, courtyard box +-4.34 (probed 2026-09-29).
    _b, key = placed("Thonk:SW_Push_LP_Button")
    kx, ky = PL.hole_point(key)
    kb = PL.body_box(key)
    check(near((kb[0] - kx, kb[1] - ky, kb[2] - kx, kb[3] - ky), (-4.34, -4.34, 4.34, 4.34)),
          "body_box: key falls back to its courtyard (%s)" % (kb,))
    # Alpha pot at rot 0: F.Fab box -6.55..4.9 x -4.8..4.8 around the shaft (probed 2026-09-29).
    _b, pot = placed("Potentiometer_THT:Potentiometer_Alpha_RD901F-40-00D_Single_Vertical")
    px, py = PL.hole_point(pot)
    pb = PL.body_box(pot)
    check(near((pb[0] - px, pb[1] - py, pb[2] - px, pb[3] - py), (-6.55, -4.8, 4.9, 4.8)),
          "body_box: pot F.Fab box (%s)" % ((pb[0] - px, pb[1] - py, pb[2] - px, pb[3] - py),))
    check(len(PL.pad_boxes(pot)) >= 3, "pad_boxes: pot has its three pins and tabs")

    # first_fit on the back: free target -> the target itself, rotation 0.
    board, r1 = placed("Resistor_SMD:R_0603_1608Metric", side="B")
    blocked = []
    inner = (-50.0, -50.0, 50.0, 50.0)
    x, y, rot = PL.first_fit(r1, (10.0, 10.0), blocked, inner, 0.5, 5.0)
    check((x, y, rot) == (10.0, 10.0, 0) and len(blocked) == 1, "first_fit: a free target is taken as is")
    # Blocked centre: the part moves out, and its courtyard clears the block.
    board, r2 = placed("Resistor_SMD:R_0603_1608Metric", side="B")
    block = (-1.5, -1.5, 1.5, 1.5)
    blocked = [block]
    x, y, rot = PL.first_fit(r2, (0.0, 0.0), blocked, inner, 0.5, 5.0)
    cy = PL.courtyard_box(r2)
    check(not PL.overlaps(cy, block) and (x, y) != (0.0, 0.0), "first_fit: steps out of a blocked centre to (%.1f, %.1f)" % (x, y))
    # accept() refusing everything -> ValueError naming the part.
    board, r3 = placed("Resistor_SMD:R_0603_1608Metric", side="B")
    r3.SetReference("R_TEST")
    try:
        PL.first_fit(r3, (0.0, 0.0), [], inner, 0.5, 1.0, accept=lambda fp: False)
        check(False, "first_fit: raises when nothing is accepted")
    except ValueError as e:
        check("R_TEST" in str(e), "first_fit: raises naming the part (%s)" % e)

    print("FAILED: %d" % len(FAILS) if FAILS else "all gen/place checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run it and see it fail**

Run: `python hardware/gen/test_place.py`
Expected: `ImportError`/`ModuleNotFoundError` for `gen.place` (non-zero exit).

- [ ] **Step 3: Write `hardware/gen/place.py`**

```python
#!/usr/bin/env python3
"""Placement geometry shared by the generated boards (Rev A P4-1).

Runs under KiCad's Python only. Millimetres, y down, board coordinates -- as
kipcb. Boxes are (left, top, right, bottom). Moved here from the P4a spike's
stripe.py (hole_point, the spiral, the first-fit search), which is deleted.
"""
import math

import pcbnew

from gen import kipcb


def box(bb):
    return (pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop()),
            pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))


def grow(b, d):
    return (b[0] - d, b[1] - d, b[2] + d, b[3] + d)


def overlaps(a, b):
    """Strict: boxes that only touch do not overlap."""
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def gap(a, b):
    dx = max(b[0] - a[2], a[0] - b[2], 0.0)
    dy = max(b[1] - a[3], a[1] - b[3], 0.0)
    return math.hypot(dx, dy)


def inside(b, outer):
    return b[0] >= outer[0] and b[1] >= outer[1] and b[2] <= outer[2] and b[3] <= outer[3]


def _fab_layer(fp):
    return pcbnew.B_Fab if fp.IsFlipped() else pcbnew.F_Fab


def _crtyd_layer(fp):
    return pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd


def hole_point(fp):
    """Where the panel hole sits on this footprint: the centre of its one
    Fab circle, or its courtyard centre when it has none (the Thonk key).
    Probed 2026-09-29: pot shaft, jack bore and LED body are each the
    footprint's only F.Fab circle."""
    layer = _fab_layer(fp)
    circles = [g for g in fp.GraphicalItems()
               if hasattr(g, "GetShape") and g.GetShape() == pcbnew.SHAPE_T_CIRCLE
               and g.GetLayer() == layer]
    if len(circles) > 1:
        raise ValueError("%s: %d Fab circles, expected one or none"
                         % (fp.GetReference(), len(circles)))
    if circles:
        c = circles[0].GetCenter()
        return pcbnew.ToMM(c.x), pcbnew.ToMM(c.y)
    l, t, r, b = box(fp.GetCourtyard(_crtyd_layer(fp)).BBox())
    return (l + r) / 2.0, (t + b) / 2.0


def body_box(fp):
    """The part's body as a box: the bounding box of its Fab graphics (text
    excluded), or its courtyard when it has no Fab graphics (the Thonk key).
    Conservative: Fab lines that are not body, such as pin housings, count."""
    layer = _fab_layer(fp)
    boxes = [box(g.GetBoundingBox()) for g in fp.GraphicalItems()
             if g.GetLayer() == layer and g.GetClass() not in ("PCB_TEXT", "PCB_FIELD")]
    if not boxes:
        c = courtyard_box(fp)
        if c is None:
            raise ValueError("%s has neither Fab graphics nor a courtyard" % fp.GetReference())
        return c
    return (min(b[0] for b in boxes), min(b[1] for b in boxes),
            max(b[2] for b in boxes), max(b[3] for b in boxes))


def pad_boxes(fp):
    return [(str(p.GetNumber()), box(p.GetBoundingBox())) for p in fp.Pads()]


def courtyard_box(fp):
    bb = fp.GetCourtyard(_crtyd_layer(fp)).BBox()
    if not bb.GetWidth() and not bb.GetHeight():
        return None
    return box(bb)


def spiral(cx, cy, step, rmax):
    """Candidate centres in square rings around (cx, cy), nearest first
    inside each ring, in a fixed order."""
    yield cx, cy
    n = 1
    while n * step <= rmax + 1e-9:
        ring = ([(i, -n) for i in range(-n, n + 1)]
                + [(n, j) for j in range(-n + 1, n + 1)]
                + [(i, n) for i in range(n - 1, -n - 1, -1)]
                + [(-n, j) for j in range(n - 1, -n, -1)])
        ring.sort(key=lambda ij: (ij[0] ** 2 + ij[1] ** 2, ij))
        for i, j in ring:
            yield cx + i * step, cy + j * step
        n += 1


def first_fit(fp, target, blocked, inner, step, rmax,
              rotations=(0, 90, 180, 270), accept=None):
    """Move `fp` to the first spiral position around `target` (and the first
    rotation there) whose courtyard lies inside `inner`, overlaps nothing in
    `blocked`, and that `accept(fp)` approves. Appends the courtyard box to
    `blocked` and returns (x, y, rot)."""
    rel = {}
    for rot in rotations:
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(kipcb._pt(*target))
        c = courtyard_box(fp)
        if c is None:
            raise ValueError("%s has no courtyard to search with" % fp.GetReference())
        rel[rot] = (c[0] - target[0], c[1] - target[1], c[2] - target[0], c[3] - target[1])
    for x, y in spiral(target[0], target[1], step, rmax):
        for rot in rotations:
            l, t, r, b = rel[rot]
            cand = (x + l, y + t, x + r, y + b)
            if not inside(cand, inner) or any(overlaps(cand, o) for o in blocked):
                continue
            fp.SetOrientationDegrees(rot)
            fp.SetPosition(kipcb._pt(x, y))
            if accept is not None and not accept(fp):
                continue
            blocked.append(cand)
            return x, y, rot
    raise ValueError("no free place for %s within %.1f mm of (%.2f, %.2f)"
                     % (fp.GetReference(), rmax, target[0], target[1]))
```

- [ ] **Step 4: Run the test and see it pass**

Run: `python hardware/gen/test_place.py`
Expected: every line `ok`, last line `all gen/place checks passed`, exit 0.
If the pot or key box assertion fails, print the measured box and stop. Those
numbers are the spec's §3 probe values, and a mismatch means the probe or the
footprint changed. Report it; do not edit the expected value.

- [ ] **Step 5: Prove the RED once**

Temporarily change `overlaps` to `<=` comparisons and re-run. Expected: the
"touching is not overlapping" line fails. Restore the file with the Edit tool,
not `git checkout` (memory: scripted edits eat CRLF). Re-run: green.

- [ ] **Step 6: Wire ctest, remove the spike**

Add to `CMakeLists.txt`, directly after the `hw_gen_route_guard` block, in
its style:

```cmake
add_test(NAME hw_gen_place_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/gen/test_place.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```

Then:
- run `git rm -r hardware/reva/spike`
- remove the `.gitignore` line `hardware/reva/spike/out/`
- `grep -rn "reva/spike\|reva\.spike" hardware CMakeLists.txt` must print
  nothing. The routing report in `docs/` keeps its references on purpose, as
  history.

Reconfigure and run:
`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` (after `source env.sh` in the same command script), then
`ctest --test-dir build -R "hw_gen|reva|coupon" --output-on-failure`.
Expected: all pass, `hw_gen_place_guard` among them.

- [ ] **Step 7: Commit**

```bash
git add hardware/gen/place.py hardware/gen/test_place.py CMakeLists.txt .gitignore
git commit -m "hw(gen): placement geometry shared; the P4a spike harness removed

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

(`git rm` already staged the spike deletion.)

---

### Task 2: The J_SD footprint

**Files:**
- Create: `hardware/reva/sd_footprint.py`
- Create: `hardware/lib/FireFlow/FireFlow.pretty/SD_Yamaichi_PJS008U-3000-0.kicad_mod` (generated)
- Create: `hardware/reva/test_sd_footprint.py`
- Modify: `hardware/gen/kipcb.py`: `FP_FIREFLOW` and the search tuple in `footprint()`.
- Modify: `hardware/reva/parts.py:73-74`: the `"sd"` PartType.
- Modify: `hardware/reva/build.py`: `lib_dirs` and `LIB_URIS` gain `FireFlow`.
- Regenerate: `hardware/reva/kicad/*` via `python hardware/reva/build.py`.
- Modify: `CMakeLists.txt`: add `reva_sd_footprint_guard`.

**Interfaces:**
- Produces: the footprint `FireFlow:SD_Yamaichi_PJS008U-3000-0`.
  - Pads `1`–`8` and two pads `SH`.
  - The origin is the body centre, which the spec places on the SD slot
    centre.
  - F.Fab box ±6.413 × ±2.832 mm; F.CrtYd box that plus 0.25 mm.
  - `kipcb.footprint("FireFlow:SD_Yamaichi_PJS008U-3000-0")` loads it.

**Source of every number** (spec §4.2 amendment): the EasyEDA footprint
`CONN-TH_PJS008U-3000-0` that LCSC publishes for C3177022. It was read
2026-09-29 from `https://easyeda.com/api/products/C3177022/components`.
EasyEDA units are 10 mil (0.254 mm), with the footprint origin at
(4001.57, 3009.594), y down. The raw pads (x, y, diameter, hole radius, number):

| EasyEDA x | y | dia | hole r | number | → mm (x, y), dia, drill |
|---|---|---|---|---|---|
| 3986.41 | 3005.52 | 4.724 | 1.378 | 1 | (−3.851, −1.035), 1.20, 0.70 |
| 3990.74 | 3001.1879 | 4.724 | 1.378 | 2 | (−2.751, −2.135), 1.20, 0.70 |
| 3995.07 | 3005.52 | 4.724 | 1.378 | 3 | (−1.651, −1.035), 1.20, 0.70 |
| 3999.40 | 3001.1879 | 4.724 | 1.378 | 4 | (−0.551, −2.135), 1.20, 0.70 |
| 4003.74 | 3005.52 | 4.724 | 1.378 | 5 | (0.551, −1.035), 1.20, 0.70 |
| 4008.07 | 3001.1879 | 4.724 | 1.378 | 6 | (1.651, −2.135), 1.20, 0.70 |
| 4012.40 | 3005.52 | 4.724 | 1.378 | 7 | (2.751, −1.035), 1.20, 0.70 |
| 4016.73 | 3001.1879 | 4.724 | 1.378 | 8 | (3.851, −2.135), 1.20, 0.70 |
| 3984.64 | 3018.00 | 5.512 | 1.7717 | NC → `SH` | (−4.300, 2.135), 1.40, 0.90 |
| 4018.50 | 3018.00 | 5.512 | 1.7717 | NC → `SH` | (4.300, 2.135), 1.40, 0.90 |

EasyEDA BBox: x 3976.3 + 50.5, y 2998.6 + 22.3, which gives
x −6.419..+6.409 and y −2.793..+2.872 mm, a centre of (−0.005, +0.040). The
script centres the body box on the origin: ±6.413 × ±2.832. It shifts every
pad by that same centre, so the footprint origin is the body centre.
Computed by the script's own `geometry()` while planning, the final pads
(number, x, y, dia, drill) are:

```
1 (-3.846, -1.075)  2 (-2.746, -2.175)  3 (-1.646, -1.075)  4 (-0.546, -2.175)
5 ( 0.556, -1.075)  6 ( 1.656, -2.175)  7 ( 2.756, -1.075)  8 ( 3.856, -2.175)
   all 1.20 round, drill 0.70
SH (-4.295, 2.095)  SH (4.305, 2.095)   1.40 round, drill 0.90
```

Pin numbering matches the KiCad `Connector:Micro_SD_Card` symbol that
`blocks.sd()` wires:
1 SD_D2, 2 SD_D3, 3 SD_CMD, 4 VDD, 5 SD_CK, 6 GND, 7 SD_D0, 8 SD_D1, SH GND.
That is the standard microSD contact order, which EasyEDA's numbers follow.

- [ ] **Step 1: Write the failing guard** `hardware/reva/test_sd_footprint.py`

```python
#!/usr/bin/env python3
"""Guard for the generated J_SD footprint: the committed file equals a fresh
generation, and KiCad loads it with the sourced pads. Re-runs itself under
KiCad's Python for the load half."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
for p in (HW, HERE):
    if p not in sys.path:
        sys.path.insert(0, p)

import sd_footprint as SD   # noqa: E402

FAILS = []


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def text_half():
    tmp = os.path.join(tempfile.mkdtemp(prefix="sdfp_"), "x.kicad_mod")
    SD.write(tmp)
    fresh = open(tmp, "rb").read()
    committed = open(SD.PATH, "rb").read() if os.path.exists(SD.PATH) else b""
    check(fresh == committed, "committed %s equals a fresh generation" % os.path.relpath(SD.PATH))


def load_half():
    import pcbnew
    from gen import kipcb
    fp = kipcb.footprint("FireFlow:" + SD.NAME)
    pads = {}
    for p in fp.Pads():
        pads.setdefault(str(p.GetNumber()), []).append(
            (round(pcbnew.ToMM(p.GetPosition().x), 3), round(pcbnew.ToMM(p.GetPosition().y), 3),
             round(pcbnew.ToMM(p.GetDrillSize().x), 2)))
    check(sorted(pads) == ["1", "2", "3", "4", "5", "6", "7", "8", "SH"],
          "pads 1-8 and SH (%s)" % sorted(pads))
    check(len(pads.get("SH", [])) == 2, "two SH pegs")
    check(pads.get("1") == [(-3.846, -1.075, 0.7)], "pad 1 at (-3.846, -1.075), drill 0.70 (%s)" % pads.get("1"))
    check(pads.get("8") == [(3.856, -2.175, 0.7)], "pad 8 at (3.856, -2.175), drill 0.70 (%s)" % pads.get("8"))
    check(sorted(pads.get("SH", [])) == [(-4.295, 2.095, 0.9), (4.305, 2.095, 0.9)],
          "SH pegs at (-4.295 / 4.305, 2.095), drill 0.90 (%s)" % pads.get("SH"))


def main():
    text_half()
    try:
        import pcbnew  # noqa: F401
        load_half()
    except ImportError:
        from gen import ksexp
        kipy = os.path.join(ksexp.KICAD_ROOT, "bin", "python.exe")
        rc = subprocess.call([kipy, os.path.abspath(__file__), "--load-only"])
        check(rc == 0, "the KiCad-side load half passed")
    print("FAILED: %d" % len(FAILS) if FAILS else "all sd footprint checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    if "--load-only" in sys.argv:
        load_half()
        sys.exit(1 if FAILS else 0)
    sys.exit(main())
```

The expected values are the planning-time output of `geometry()` listed
above. They include the centring shift of (+0.005, −0.040) mm.

- [ ] **Step 2: Run it and see it fail**

Run: `python hardware/reva/test_sd_footprint.py`
Expected: `ModuleNotFoundError: sd_footprint`.

- [ ] **Step 3: Write `hardware/reva/sd_footprint.py`** (system Python, plain text writer)

```python
#!/usr/bin/env python3
"""Writes the J_SD footprint: Yamaichi PJS008U-3000-0, vertical microSD,
through hole (P4-1 spec §4.2).

Source of every number: the EasyEDA footprint CONN-TH_PJS008U-3000-0 that
LCSC publishes for C3177022, read 2026-09-29 from
https://easyeda.com/api/products/C3177022/components -- a secondary source;
the manufacturer drawing could not be read while planning. A socket in hand
checks it before the order. EasyEDA units are 10 mil; origin (4001.57,
3009.594), y down. The footprint origin here is the body-box centre.

    python hardware/reva/sd_footprint.py      # writes the committed file
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = "SD_Yamaichi_PJS008U-3000-0"
PATH = os.path.normpath(os.path.join(HERE, "..", "lib", "FireFlow", "FireFlow.pretty",
                                     NAME + ".kicad_mod"))
U = 0.254                            # one EasyEDA unit in mm
ORIGIN = (4001.57, 3009.594)
# (number, x, y, diameter, hole radius) in EasyEDA units, verbatim.
PADS = [("1", 3986.41, 3005.52, 4.724, 1.378), ("2", 3990.74, 3001.1879, 4.724, 1.378),
        ("3", 3995.07, 3005.52, 4.724, 1.378), ("4", 3999.40, 3001.1879, 4.724, 1.378),
        ("5", 4003.74, 3005.52, 4.724, 1.378), ("6", 4008.07, 3001.1879, 4.724, 1.378),
        ("7", 4012.40, 3005.52, 4.724, 1.378), ("8", 4016.73, 3001.1879, 4.724, 1.378),
        ("SH", 3984.64, 3018.00, 5.512, 1.7717), ("SH", 4018.50, 3018.00, 5.512, 1.7717)]
BBOX = (3976.3, 2998.6, 50.5, 22.3)  # x, y, width, height
COURTYARD_MARGIN = 0.25              # KiCad library convention for connectors
HEIGHT_MM = 14.18                    # LCSC C3177022 "Height Above Board"


def _mm(v):
    return round(v * U, 3)


def geometry():
    """(pads [(number, x, y, dia, drill)], body box) in mm, centred on the body."""
    l, t = (BBOX[0] - ORIGIN[0]) * U, (BBOX[1] - ORIGIN[1]) * U
    r, b = l + BBOX[2] * U, t + BBOX[3] * U
    cx, cy = (l + r) / 2.0, (t + b) / 2.0
    pads = [(n, round(_mm(x - ORIGIN[0]) - cx, 3), round(_mm(y - ORIGIN[1]) - cy, 3),
             round(d * U, 2), round(2 * hr * U, 2)) for n, x, y, d, hr in PADS]
    hw, hh = round((r - l) / 2.0, 3), round((b - t) / 2.0, 3)
    return pads, (-hw, -hh, hw, hh)


def text():
    pads, (l, t, r, b) = geometry()
    m = COURTYARD_MARGIN
    lines = [
        '(footprint "%s"' % NAME,
        '\t(version 20241229)',
        '\t(generator "fireflow_sd_footprint")',
        '\t(layer "F.Cu")',
        '\t(descr "Yamaichi PJS008U-3000-0 vertical microSD, THT; from the LCSC/EasyEDA '
        'footprint of C3177022 (secondary source); height %.2f mm")' % HEIGHT_MM,
        '\t(attr through_hole)',
        '\t(fp_text reference "REF**" (at 0 %.2f 0) (layer "F.SilkS") '
        '(effects (font (size 1 1) (thickness 0.15))))' % (t - 1.2),
        '\t(fp_text value "%s" (at 0 %.2f 0) (layer "F.Fab") '
        '(effects (font (size 1 1) (thickness 0.15))))' % (NAME, b + 1.2),
        '\t(fp_rect (start %.3f %.3f) (end %.3f %.3f) (stroke (width 0.1) (type solid)) '
        '(fill none) (layer "F.Fab"))' % (l, t, r, b),
        '\t(fp_rect (start %.3f %.3f) (end %.3f %.3f) (stroke (width 0.05) (type solid)) '
        '(fill none) (layer "F.CrtYd"))' % (l - m, t - m, r + m, b + m),
    ]
    for n, x, y, d, drill in pads:
        lines.append('\t(pad "%s" thru_hole circle (at %.3f %.3f) (size %.2f %.2f) '
                     '(drill %.2f) (layers "*.Cu" "*.Mask"))' % (n, x, y, d, d, drill))
    lines.append(')')
    return "\n".join(lines) + "\n"


def write(path=PATH):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text())


if __name__ == "__main__":
    write()
    print("wrote", os.path.relpath(PATH))
```

- [ ] **Step 4: Teach kipcb the library**

In `hardware/gen/kipcb.py`, after `FP_THONK = ...` add
`FP_FIREFLOW = os.path.join(FP_LIB, "FireFlow")`. In `footprint()` change the
tuple to `(FP_SHARE, FP_VENDORED, FP_THONK, FP_FIREFLOW)`, and add
"`hardware/lib/FireFlow` (generated footprints)" to the docstring's list.
The coupon never names a `FireFlow:` footprint, so its build stays
byte-identical.

- [ ] **Step 5: Generate and run the guard**

Run: `python hardware/reva/sd_footprint.py`, then `python hardware/reva/test_sd_footprint.py`.
Expected: all `ok`.

If KiCad refuses the file (the load half raises), the s-expression header is
the suspect. Open one of KiCad 10's own `Connector_Card.pretty/*.kicad_mod`
files and match its `(version ...)` and attribute syntax. Then regenerate.

- [ ] **Step 6: Prove the RED once**

Change pad 1's EasyEDA x in `PADS` to 3986.51, regenerate into a temp path
only (`python -c "import sys; sys.path.insert(0,'hardware/reva'); import sd_footprint as S; S.write('C:/Users/bernd/AppData/Local/Temp/sdred.kicad_mod')"`),
and run the guard. Expected: the "committed equals fresh" line fails.
Restore the value with the Edit tool and re-run: green.

- [ ] **Step 7: Wire the part to the footprint**

- `hardware/reva/parts.py`, the `"sd"` entry becomes:
  `PartType("Connector:Micro_SD_Card", "microSD", "FireFlow:SD_Yamaichi_PJS008U-3000-0", "", HAND, "", "Yamaichi PJS008U-3000-0 vertical microSD, THT (P4-1 spec §4.2)")`.
  Update the module docstring's line 8, which mentions the open "P4"
  footprint: it is chosen now.
- `hardware/reva/build.py`:
  - add `FIREFLOW_DIR = os.path.join(HW, "lib", "FireFlow")`
  - `lib_dirs={"Thonk": THONK_DIR, "FireFlow": FIREFLOW_DIR}`
  - `LIB_URIS` gains `"FireFlow": "${KIPRJMOD}/../../lib/FireFlow"`
- Run `python hardware/reva/build.py`. It regenerates `kicad/sd.kicad_sch`
  (the footprint field), `fp-lib-table` (a FireFlow row) and `review.md` /
  `bom-hand.csv` if they name the source.
- Run `python hardware/gen/check.py --project hardware/reva/build.py --full --out build/reva-check`.
  Expected: clean. `rule_footprints` now finds no open `P4`, which it allows
  (it rejects more than one, not zero).

- [ ] **Step 8: ctest entry and full run**

```cmake
add_test(NAME reva_sd_footprint_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/test_sd_footprint.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```

Reconfigure (as in Task 1) and run `ctest --test-dir build -R "hw_gen|reva|coupon" --output-on-failure`.
Expected: all pass.

- [ ] **Step 9: Commit**

```bash
git add hardware/reva/sd_footprint.py hardware/reva/test_sd_footprint.py hardware/lib/FireFlow hardware/gen/kipcb.py hardware/reva/parts.py hardware/reva/build.py hardware/reva/kicad hardware/reva/review.md hardware/reva/bom-hand.csv CMakeLists.txt
git commit -m "hw(reva): J_SD is the Yamaichi PJS008U, footprint generated from LCSC's data

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The board, the panel parts and the first checks

**Files:**
- Create: `hardware/reva/place.py`
- Create: `hardware/reva/place_check.py`

**Interfaces:**
- Consumes (Task 1): `gen.place` functions. (Task 2): the J_SD footprint.
- Produces, used by Tasks 4–6:
  - `place.X0, Y0, X1, Y1, EDGE_CLEAR, EDGE_INSET, PAD_CLEAR, DECOUPLE_MAX_MM, USB_CLEAR_MM, SUPPLY`
  - `place.Placed`, with fields:
    - `board`
    - `parts {ref: Part}`
    - `holes {ref: (x, y)}`: panel parts and J_SD
    - `front [ref]`
    - `ids {ref: panel_id}`
    - `anchors {ref: (x, y)}`
    - `decouplers {cap: (ic_ref, pad_number)}`
    - `shadow` (box or None)
    - `no_rotation set(ref)`
    - `overrides_used [str]`
    - `known {check: set(key)}`

    Method `Placed.reload(path) -> Placed`: the board loaded from `path`,
    every other field deep-copied.
  - `place.build() -> Placed`
  - `place.save(s, path)`
  - `place.main()`: the CLI.
  - `place_check.run(s, pcb_path, prefix) -> bool`
  - `place_check.STEPS`: an ordered list of (name, fn), each
    `fn(s, pcb_path, prefix) -> (ok, line, details)`.
  - `place_check.SABOTAGES {name: fn(s)}`
  - `place_check.sabotage(s, name)`
  - `place_check.KNOWN_PANEL`

The checks in this task are `anchors`, `edge` and `front`. Tasks 4–6 add
`module`, `decoupling` and `drc`, plus the report.

- [ ] **Step 1: Write `hardware/reva/place.py` (panel half)**

```python
#!/usr/bin/env python3
"""Rev A placement (P4-1 spec, docs/superpowers/specs/2026-09-29-rev-a-p4-1-placement-design.md).

    KIPY hardware/reva/place.py [--write] [--sabotage NAME] [--out DIR] [--where REF]

Builds the placed, unrouted board from P3's project (build.project()), the P1
hole list and panel-map.json, saves <out>/reva-placed.kicad_pcb (default
hardware/reva/out/), renders it and runs place_check. Exit 0 only when every
gated check is green (known panel violations listed, spec §5.3). --write also
copies the .kicad_pcb -- never the .kicad_pro SaveBoard writes beside it --
to hardware/reva/kicad/reva.kicad_pcb.
"""
import argparse
import copy
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
for _p in (HW, HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import pcbnew          # noqa: E402
import assign          # noqa: E402
import blocks as BL    # noqa: E402
import build as RB     # noqa: E402
from gen import kipcb  # noqa: E402
from gen import place as PL  # noqa: E402

OUT = os.path.join(HERE, "out")
COMMITTED = os.path.join(HERE, "kicad", "reva.kicad_pcb")
DOCS = os.path.normpath(os.path.join(HW, "..", "docs", "hardware", "placement"))
X0, Y0, X1, Y1 = 2.0, 9.25, 302.8, 119.25   # spec §2.1 / §4.1
EDGE_CLEAR = 0.5          # copper to edge (assumption, spec §8)
EDGE_INSET = 1.0          # SMD courtyards stay this far inside the outline
PAD_CLEAR = 0.2           # the coupon's board minimum
DECOUPLE_MAX_MM = 2.0     # the coupon's check_layout rule 5
USB_CLEAR_MM = 35.0       # spec §4.3 amendment
LAYERS = 4
PLANES = (("In1.Cu", BL.GND), ("In2.Cu", BL.SM3V3))
SUPPLY = {BL.GND, BL.SM3V3, BL.D3V3, BL.P12, BL.N12, BL.P12_IN, BL.N12_IN}
POT_ROT, POT_TOP_ROT = 270, 90
FIXED_ROT = {"jack": 0, "key": 0, "sd": 0}
LED_ROTS = (0, 90, 180, 270)


class Placed:
    def __init__(self):
        self.board = None
        self.parts = {}
        self.holes = {}
        self.front = []
        self.ids = {}
        self.anchors = {}
        self.decouplers = {}
        self.shadow = None
        self.no_rotation = set()
        self.overrides_used = []
        self.known = {}

    def reload(self, path):
        new = Placed()
        for k, v in self.__dict__.items():
            if k not in ("board", "parts"):
                setattr(new, k, copy.deepcopy(v))
        new.parts = dict(self.parts)          # Part objects are read-only here
        new.board = kipcb.load(path)
        return new


def outline_box():
    return (X0, Y0, X1, Y1)


def _hole_index():
    by_id = {}
    for h in assign.load_holes():
        for i in h.get("ids", [h["id"]]):
            by_id[i] = h
    return by_id


def _place_on_hole(board, part, hole, rot):
    """Add `part` at `rot` with its hole point on the hole centre."""
    fp = kipcb.add_part(board, part, 0.0, 0.0, rot)
    hx, hy = PL.hole_point(fp)
    fp.SetPosition(kipcb._pt(hole["x_mm"] - hx, hole["y_mm"] - hy))
    return fp


def _front_obstacles(board, refs, skip):
    """Body boxes and pad boxes of the front parts in `refs` that are already
    on the board, except `skip` (LEDs later in the list are not placed yet)."""
    bodies, pads = [], []
    for ref in refs:
        if ref == skip:
            continue
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            continue
        bodies.append(PL.body_box(fp))
        pads += [b for _n, b in PL.pad_boxes(fp)]
    return bodies, pads


def _led_fits(fp, bodies, pads):
    for _n, b in PL.pad_boxes(fp):
        if any(PL.overlaps(b, o) for o in bodies):
            return False
        if any(PL.gap(b, o) < PAD_CLEAR for o in pads):
            return False
    return True


def place_panel(s, proj):
    """Every panel part on its hole (spec §4.2); LEDs last, each at the first
    rotation whose pads miss every foreign body and keep PAD_CLEAR."""
    by_id = _hole_index()
    rows = []
    for part in proj.parts():
        if not part.panel_id:
            continue
        h = by_id.get(part.panel_id)
        if h is None:
            raise ValueError("%s: panel id %s is not in the hole list" % (part.ref, part.panel_id))
        rows.append((part, h))
    top = min(h["y_mm"] for _p, h in rows if h["kind"] == "pot")
    leds = []
    for part, h in rows:
        kind = h["kind"]
        s.parts[part.ref] = part
        s.holes[part.ref] = (h["x_mm"], h["y_mm"])
        s.ids[part.ref] = part.panel_id
        s.front.append(part.ref)
        if kind == "led":
            leds.append((part, h))
            continue
        rot = (POT_TOP_ROT if h["y_mm"] == top else POT_ROT) if kind == "pot" else FIXED_ROT[kind]
        _place_on_hole(s.board, part, h, rot)
    for part, h in leds:
        fp = None
        for rot in LED_ROTS:
            if fp is None:
                fp = _place_on_hole(s.board, part, h, rot)
            else:
                fp.SetOrientationDegrees(rot)
                fp.SetPosition(kipcb._pt(0.0, 0.0))
                hx, hy = PL.hole_point(fp)
                fp.SetPosition(kipcb._pt(h["x_mm"] - hx, h["y_mm"] - hy))
            bodies, pads = _front_obstacles(s.board, s.front, part.ref)
            if _led_fits(fp, bodies, pads):
                break
        else:
            s.no_rotation.add(part.ref)
            fp.SetOrientationDegrees(0)
            fp.SetPosition(kipcb._pt(0.0, 0.0))
            hx, hy = PL.hole_point(fp)
            fp.SetPosition(kipcb._pt(h["x_mm"] - hx, h["y_mm"] - hy))


def build():
    proj = RB.project()
    s = Placed()
    s.board = kipcb.new_board(X1 - X0, Y1 - Y0, LAYERS, origin=(X0, Y0))
    s.board.GetDesignSettings().m_CopperEdgeClearance = pcbnew.FromMM(EDGE_CLEAR)
    rect = [(X0, Y0), (X1, Y0), (X1, Y1), (X0, Y1)]
    for layer, net in PLANES:
        kipcb.add_zone(s.board, layer, net, rect)
    place_panel(s, proj)
    return s


def save(s, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    kipcb.save(s.board, path)


def main(argv=None):
    import place_check as PC
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--sabotage", default="")
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--where", default="")
    a = ap.parse_args(argv)
    s = build()
    if a.where:
        fp = s.board.FindFootprintByReference(a.where)
        if fp is None:
            print("no footprint %s" % a.where)
            return 1
        for n, b in PL.pad_boxes(fp):
            print("   %-4s x %.3f..%.3f y %.3f..%.3f" % (n, b[0], b[2], b[1], b[3]))
        print("   body", PL.body_box(fp), "rot", fp.GetOrientationDegrees(),
              "side", "B" if fp.IsFlipped() else "F")
        return 0
    prefix = os.path.join(a.out, "reva-placed")
    pcb = prefix + ".kicad_pcb"
    if a.sabotage:
        save(s, pcb)
        s = s.reload(pcb)
        PC.sabotage(s, a.sabotage)
    save(s, pcb)
    print("wrote", os.path.relpath(pcb))
    green = PC.run(s, pcb, prefix)
    if a.write and not a.sabotage:
        shutil.copyfile(pcb, COMMITTED)
        print("copied to", os.path.relpath(COMMITTED))
        os.makedirs(DOCS, exist_ok=True)
        for side in ("top", "bottom"):
            png = "%s-%s.png" % (prefix, side)
            if os.path.exists(png):
                shutil.copyfile(png, os.path.join(DOCS, os.path.basename(png)))
        print("renders copied to", os.path.relpath(DOCS))
    print("GREEN" if green else "RED")
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Write `hardware/reva/place_check.py` (first three checks)**

```python
#!/usr/bin/env python3
"""The P4-1 checks (spec §5). Every step runs and prints; the run is green
only if every gated step is. A step that examined nothing is red. Known
panel violations (spec §5.3) print as known; an unlisted failure is red, and
so is a listed one that no longer fails."""
import math
import os

import pcbnew

import place as P
from gen import kipcb
from gen import pcb_proof as PP
from gen import place as PL

# Filled from the first full run of Task 3/4/6, restricted to the jack row and
# the SONG clusters (spec §5.3); test_place.py asserts that restriction.
KNOWN_PANEL = {"edge": set(), "front": set(), "drc": set()}


def _fp(board, ref):
    return board.FindFootprintByReference(ref)


def _key(s, ref):
    return s.ids.get(ref, ref)


def _judge(s, check, found):
    """found: {key: message}. Returns (ok, details, n_known)."""
    known = s.known.get(check, set())
    details = []
    for k in sorted(found):
        tag = "known, waits for the panel pass" if k in known else "NEW"
        details.append("%s: %s [%s]" % (k, found[k], tag))
    stale = sorted(known - set(found))
    details += ["%s: listed as known but no longer fails -- remove it from KNOWN_PANEL" % k
                for k in stale]
    unknown = [k for k in found if k not in known]
    return not unknown and not stale, details, len(set(found) & known)


def check_anchors(s, pcb_path, prefix):
    if not s.holes:
        return False, "examined 0 panel parts", []
    bad, worst = [], 0.0
    for ref, (hx, hy) in sorted(s.holes.items()):
        x, y = PL.hole_point(_fp(s.board, ref))
        d = math.hypot(x - hx, y - hy)
        worst = max(worst, d)
        if d > 0.01:
            bad.append("%s (%s) sits %.3f mm off its hole" % (ref, _key(s, ref), d))
    return not bad, "%d panel parts, worst %.4f mm off its hole (limit 0.01)" % (
        len(s.holes), worst), bad


def check_edge(s, pcb_path, prefix):
    ob = PL.box(s.board.GetBoardEdgesBoundingBox())
    if ob[2] - ob[0] <= 0 or ob[3] - ob[1] <= 0:
        return False, "the board has no outline", []
    lim = PL.grow(ob, -P.EDGE_CLEAR)
    found, n = {}, 0
    for fp in s.board.GetFootprints():
        for num, b in PL.pad_boxes(fp):
            n += 1
            if not PL.inside(b, lim):
                over = max(lim[0] - b[0], lim[1] - b[1], b[2] - lim[2], b[3] - lim[3])
                k = _key(s, fp.GetReference())
                found[k] = "pad %s %.2f mm past the %.1f mm edge clearance" % (num, over, P.EDGE_CLEAR)
    if not n:
        return False, "examined 0 pads", []
    ok, details, nk = _judge(s, "edge", found)
    return ok, "%d pads, %d parts past the edge clearance (%d known)" % (n, len(found), nk), details


def check_front(s, pcb_path, prefix):
    if not s.front:
        return False, "examined 0 front parts", []
    fps = {r: _fp(s.board, r) for r in s.front}
    body = {r: PL.body_box(fp) for r, fp in fps.items()}
    pads = {r: PL.pad_boxes(fp) for r, fp in fps.items()}
    found, reported = {}, []
    refs = sorted(fps)
    for i, a in enumerate(refs):
        for b in refs[i + 1:]:
            if PL.overlaps(body[a], body[b]):
                found["body %s" % "/".join(sorted((_key(s, a), _key(s, b))))] = "bodies overlap"
    for x in refs:
        for y in refs:
            if x == y:
                continue
            hits = [n for n, pb in pads[x] if PL.overlaps(pb, body[y])]
            if not hits:
                continue
            kx, ky = _key(s, x), _key(s, y)
            if x.startswith("RV") and y.startswith("D"):
                reported.append("pot %s pins %s under LED %s" % (kx, ",".join(hits), ky))
            else:
                found["pad %s/%s" % (kx, ky)] = "pads %s of %s inside %s's body" % (",".join(hits), kx, ky)
    for ref in sorted(s.no_rotation):
        found["rotation %s" % _key(s, ref)] = "no LED rotation keeps its legs clear"
    ok, details, nk = _judge(s, "front", found)
    details += ["reported, not gated: " + r for r in reported]
    return ok, "%d front parts, %d violations (%d known), %d pot pins under LEDs reported" % (
        len(refs), len(found), nk, len(reported)), details


def render(s, pcb_path, prefix):
    lines = []
    for side, suffix in (("top", "-top.png"), ("bottom", "-bottom.png")):
        rc, out = PP.render(pcb_path, prefix + suffix, side)
        if rc:
            lines.append("render %s rc=%d: %s" % (side, rc, out[-300:]))
    return not lines, "rendered %s-top.png, %s-bottom.png" % (
        os.path.basename(prefix), os.path.basename(prefix)), lines


STEPS = [("anchors", check_anchors), ("edge", check_edge), ("front", check_front),
         ("render", render)]


def run(s, pcb_path, prefix):
    s.known = {k: set(v) for k, v in KNOWN_PANEL.items()} if not s.known else s.known
    green = True
    for i, (name, fn) in enumerate(STEPS, 1):
        ok, line, details = fn(s, pcb_path, prefix)
        print("%s %d. %-10s %s" % ("   " if ok else "RED", i, name, line))
        for d in details[:40]:
            print("        " + d)
        green = green and ok
    return green


def _sab_anchors(s):
    _fp(s.board, sorted(s.holes)[0]).Move(kipcb._pt(0.5, 0.0))


def _sab_anchors_missing(s):
    s.holes.clear()


def _sab_edge(s):
    """A test point (back, module sheet) moved to 0.2 mm from the left edge."""
    ref = sorted(r for r in s.parts if r.startswith("TP"))[0] if any(
        r.startswith("TP") for r in s.parts) else sorted(s.holes)[0]
    fp = _fp(s.board, ref)
    b = PL.pad_boxes(fp)[0][1]
    fp.Move(kipcb._pt(P.X0 + 0.2 - b[0], 0.0))


def _sab_edge_missing(s):
    for d in [d for d in s.board.GetDrawings() if d.GetLayer() == pcbnew.Edge_Cuts]:
        s.board.Remove(d)


def _sab_front(s):
    """The first LED moved onto the first pot's shaft: bodies overlap."""
    led = sorted(r for r in s.front if r.startswith("D"))[0]
    pot = sorted(r for r in s.front if r.startswith("RV"))[0]
    lx, ly = PL.hole_point(_fp(s.board, led))
    px, py = s.holes[pot]
    _fp(s.board, led).Move(kipcb._pt(px - lx, py - ly))


def _sab_front_missing(s):
    s.front = []


def _sab_known_stale(s):
    s.known.setdefault("edge", set()).add("NOT_A_PART")


SABOTAGES = {"anchors": _sab_anchors, "anchors_missing": _sab_anchors_missing,
             "edge": _sab_edge, "edge_missing": _sab_edge_missing,
             "front": _sab_front, "front_missing": _sab_front_missing,
             "known_stale": _sab_known_stale}
# which step each sabotage must turn red (test_place.py reads this)
TURNS_RED = {"anchors": "anchors", "anchors_missing": "anchors", "edge": "edge",
             "edge_missing": "edge", "front": "front", "front_missing": "front",
             "known_stale": "edge"}


def sabotage(s, name):
    if name not in SABOTAGES:
        raise SystemExit("unknown sabotage %r; known: %s" % (name, ", ".join(sorted(SABOTAGES))))
    if not s.known:
        s.known = {k: set(v) for k, v in KNOWN_PANEL.items()}
    SABOTAGES[name](s)
    print("SABOTAGED: %s" % name)
```

Note on `_sab_edge`: in this task there are no test points on the board yet
(SMD comes in Task 5). The fallback moves a panel part instead, which also
turns `anchors` red. That is fine; the guard only asserts that the named step
goes red. Task 5 makes the TP branch live.

- [ ] **Step 3: First run, read the output, look at the renders**

Run: `KIPY hardware/reva/place.py`, i.e.
`/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe hardware/reva/place.py`.

Expected, from the spec §3 probe:
- `anchors` green.
- `edge` RED, listing the 18 jack ids (pads about 1.23 mm past the 0.5 mm
  clearance) as NEW.
- `front` RED with `rotation SONG_A_L` and `rotation SONG_B_L`, plus the pad
  and body items those two LEDs cause at rot 0, as NEW. Three pot-under-LED
  lines are reported: SOURCE_A/GATE_A_L, PAN_A/LVL_A_L, PAN_B/LVL_B_L.

Open `hardware/reva/out/reva-placed-top.png` and `-bottom.png`. They should
show every panel part on the front and nothing on the back yet.

**If the output differs from the expectation above**, stop and report what
came out; do not "fix" by widening the known list. The probe numbers are the
spec's.

- [ ] **Step 4: Fill `KNOWN_PANEL` from that run**

- Copy the NEW keys into `KNOWN_PANEL["edge"]` and `KNOWN_PANEL["front"]`,
  one per line, sorted.
- Only keys naming a jack-row part (any jack id, or a part on
  `JACK_Y` = 114.0) or `SONG_A`/`SONG_B`/`SONG_A_L`/`SONG_B_L` may go in.
- Anything else is a real finding: report it to the controller.

Re-run: `anchors`, `edge` and `front` green, with the known items printed as
known.

- [ ] **Step 5: Every sabotage goes red**

Run `place.py --sabotage NAME` for every name in `SABOTAGES`. Each must end
`RED`, with its `TURNS_RED` step red. Put the result lines in the report.

- [ ] **Step 6: Commit**

```bash
git add hardware/reva/place.py hardware/reva/place_check.py
git commit -m "hw(reva): P4-1 board and panel parts placed; anchors, edge and front checks

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 4: Module, power header, module check, depth report

**Files:**
- Modify: `hardware/reva/place.py`: add `place_module(s, proj)` and
  `place_power_header(s, proj)`, called from `build()` after `place_panel`.
- Modify: `hardware/reva/place_check.py`: `check_module`, `check_front`
  extended to back THT pads, report lines, sabotages.

**Interfaces:**
- Consumes: Task 3's `Placed`, `_front_obstacles`, constants.
- Produces:
  - `s.shadow`: the module's B.Silkscreen box.
  - `s.anchors["U_SM"]` and `s.anchors["J_PWR"]`
  - `place.back_tht_refs(s) -> [ref]`, which returns `["U_SM", "J_PWR"]` once
    placed.

- [ ] **Step 1: Module placement** (append to `place.py`, call from `build()`)

```python
def _silk_box(fp):
    layer = pcbnew.B_SilkS if fp.IsFlipped() else pcbnew.F_SilkS
    boxes = [PL.box(g.GetBoundingBox()) for g in fp.GraphicalItems() if g.GetLayer() == layer]
    return (min(b[0] for b in boxes), min(b[1] for b in boxes),
            max(b[2] for b in boxes), max(b[3] for b in boxes))


def _tht_clear_of_front(fp, bodies, pads):
    """A back THT part's pins come through to the front: no pin in a front
    body, PAD_CLEAR to every front pad."""
    for _n, b in PL.pad_boxes(fp):
        if any(PL.overlaps(b, o) for o in bodies):
            return False
        if any(PL.gap(b, o) < PAD_CLEAR for o in pads):
            return False
    return True


def back_tht_refs(s):
    return [r for r in ("U_SM", "J_PWR") if r in s.anchors]


def place_module(s, proj):
    """U_SM on the back, rotation 0 or 180, the spiral spot nearest the board
    centre whose pins miss every front body and whose shadow (its silkscreen
    box) stays EDGE_INSET inside the outline (spec §4.3; probed free spots
    at (151.0, 64.2) rot 0 and (154.0, 64.2) rot 180)."""
    part = {p.ref: p for p in proj.parts()}["U_SM"]
    cx, cy = (X0 + X1) / 2.0, (Y0 + Y1) / 2.0
    fp = kipcb.add_part(s.board, part, cx, cy, 0, side="B")
    bodies, pads = _front_obstacles(s.board, s.front, None)
    inner = PL.grow(outline_box(), -EDGE_INSET)
    best = None
    for rot in (0, 180):
        for x, y in PL.spiral(cx, cy, 0.5, 60.0):
            fp.SetOrientationDegrees(rot)
            fp.SetPosition(kipcb._pt(x, y))
            if PL.inside(_silk_box(fp), inner) and _tht_clear_of_front(fp, bodies, pads):
                d = math.hypot(x - cx, y - cy)
                if best is None or d < best[0]:
                    best = (d, x, y, rot)
                break
    if best is None:
        raise ValueError("no spot for U_SM within 60 mm of the centre")
    _d, x, y, rot = best
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(kipcb._pt(x, y))
    s.parts["U_SM"] = part
    s.shadow = _silk_box(fp)
    s.anchors["U_SM"] = (x, y)
```

Add `import math` at the top of `place.py`.

- [ ] **Step 2: Power header placement**

```python
def _jpwr_rotation(fp):
    """Long axis vertical, pin 1 (-12 V) nearest the bottom edge: the first
    rotation where pad 1 has the largest y and the pad field is taller than
    wide (spec §4.3, Eurorack "red stripe down")."""
    for rot in (0, 90, 180, 270):
        fp.SetOrientationDegrees(rot)
        pb = PL.pad_boxes(fp)
        xs = [b[0] for _n, b in pb] + [b[2] for _n, b in pb]
        ys = [b[1] for _n, b in pb] + [b[3] for _n, b in pb]
        p1 = [b for n, b in pb if n == "1"][0]
        if max(ys) - min(ys) > max(xs) - min(xs) and p1[3] >= max(b[3] for _n, b in pb) - 1e-6:
            return rot
    raise ValueError("J_PWR: no rotation puts pin 1 at the bottom with the long axis vertical")


def place_power_header(s, proj, blocked):
    """J_PWR on the back in the board half without OUT_L/OUT_R, at mid height;
    pins clear of the front, courtyard >= USB_CLEAR_MM from the module
    shadow (spec §4.3)."""
    part = {p.ref: p for p in proj.parts()}["J_PWR"]
    by_id = _hole_index()
    out_x = (by_id["OUT_L"]["x_mm"] + by_id["OUT_R"]["x_mm"]) / 2.0
    mid_x = (X0 + X1) / 2.0
    tx = (X0 + mid_x) / 2.0 if out_x > mid_x else (mid_x + X1) / 2.0
    target = (tx, (Y0 + Y1) / 2.0)
    fp = kipcb.add_part(s.board, part, target[0], target[1], 0, side="B")
    rot = _jpwr_rotation(fp)
    bodies, pads = _front_obstacles(s.board, s.front, None)

    def ok(f):
        return (_tht_clear_of_front(f, bodies, pads)
                and PL.gap(PL.courtyard_box(f), s.shadow) >= USB_CLEAR_MM)

    PL.first_fit(fp, target, blocked, PL.grow(outline_box(), -EDGE_INSET), 0.5, 60.0,
                 rotations=(rot,), accept=ok)
    s.parts["J_PWR"] = part
    s.anchors["J_PWR"] = target
```

In `build()`, after `place_panel(s, proj)`:

```python
    place_module(s, proj)
    s.blocked = _tht_blocked(s) + [s.shadow]
    place_power_header(s, proj, s.blocked)
```

With the helper:

```python
def _tht_blocked(s):
    """Every through-hole pad on the board, grown by PAD_CLEAR: SMD parts on
    the back must keep off the pin tails."""
    out = []
    for fp in s.board.GetFootprints():
        for p in fp.Pads():
            if p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                out.append(PL.grow(PL.box(p.GetBoundingBox()), PAD_CLEAR))
    return out
```

Add `self.blocked = []` to `Placed.__init__`. `reload()` deep-copies it like
every other field.

- [ ] **Step 3: Checks**

In `place_check.py`, add the new check and extend `check_front` so that back
THT pins count:

```python
def check_module(s, pcb_path, prefix):
    if s.shadow is None:
        return False, "no module shadow recorded", []
    bad = []
    for fp in s.board.GetFootprints():
        ref = fp.GetReference()
        if ref == "U_SM" or not fp.IsFlipped():
            continue
        c = PL.courtyard_box(fp)
        if c is not None and PL.overlaps(c, s.shadow):
            bad.append("%s's courtyard enters the module shadow" % ref)
    jp = _fp(s.board, "J_PWR")
    if jp is None:
        bad.append("J_PWR is not on the board")
    else:
        g = PL.gap(PL.courtyard_box(jp), s.shadow)
        if g < P.USB_CLEAR_MM:
            bad.append("J_PWR is %.1f mm from the module shadow (limit %.0f)" % (g, P.USB_CLEAR_MM))
    return not bad, "shadow %s, J_PWR %.1f mm away (limit %.0f)" % (
        tuple(round(v, 2) for v in s.shadow),
        PL.gap(PL.courtyard_box(jp), s.shadow) if jp else -1, P.USB_CLEAR_MM), bad
```

In `check_front`, after the front-front loops, add:

```python
    for r in P.back_tht_refs(s):
        fp = _fp(s.board, r)
        for y in refs:
            hits = [n for n, pb in PL.pad_boxes(fp) if PL.overlaps(pb, body[y])]
            if hits:
                found["pad %s/%s" % (r, _key(s, y))] = "pins %s of %s inside %s's body" % (
                    ",".join(hits), r, _key(s, y))
```

Insert `("module", check_module)` into `STEPS` after `front`. Add a report
step that never gates:

```python
SD_HEIGHT_MM = 14.18      # LCSC C3177022
GAP_MM = 10.0             # assumption until the grip test (spec §8)
MODULE_MM, BOARD_MM = 15.0, 1.6
PALETTE_MID, PALETTE_EDGE = 45.5, 37.4


def report(s, pcb_path, prefix):
    lines = ["SD socket %.2f mm tall against a %.1f mm panel gap: protrudes %.2f mm past "
             "the panel's back face (assumed gap)" % (SD_HEIGHT_MM, GAP_MM, SD_HEIGHT_MM - GAP_MM),
             "depth: module %.1f + board %.1f + gap %.1f = %.1f mm behind the panel, against "
             "the Palette's %.1f (middle) / %.1f (outermost HP)" % (
                 MODULE_MM, BOARD_MM, GAP_MM, MODULE_MM + BOARD_MM + GAP_MM, PALETTE_MID, PALETTE_EDGE)]
    for ref in ("U_SM", "J_PWR"):
        fp = _fp(s.board, ref)
        if fp is not None:
            lines.append("%s at (%.2f, %.2f) rot %.0f" % (
                ref, pcbnew.ToMM(fp.GetPosition().x), pcbnew.ToMM(fp.GetPosition().y),
                fp.GetOrientationDegrees()))
    return True, "reported, never gates", lines
```

Put `("report", report)` just before `("render", render)`. Sabotages:

```python
def _sab_module(s):
    """A back test point, or failing that J_PWR, moved into the shadow centre."""
    cx, cy = (s.shadow[0] + s.shadow[2]) / 2.0, (s.shadow[1] + s.shadow[3]) / 2.0
    tps = sorted(r for r in s.parts if r.startswith("TP"))
    fp = _fp(s.board, tps[0] if tps else "J_PWR")
    fp.SetPosition(kipcb._pt(cx, cy))


def _sab_module_missing(s):
    s.shadow = None
```

Add both to `SABOTAGES`, with `TURNS_RED` `"module"` for both.

- [ ] **Step 4: Run, look, fix only what the renders justify**

Run `KIPY hardware/reva/place.py`. Expected:
- `module` is green.
- The report puts U_SM near (151, 64) rot 0, or (154, 64) rot 180 (the
  spec's probe), and J_PWR in the left or right half at y about 64.
- `front` must stay at the known set. A new `pad U_SM/...` or
  `pad J_PWR/...` is a real finding: report it.

Look at both renders. The back must show the module outline and the header.

- [ ] **Step 5: Sabotages** `module` and `module_missing` end RED with `module` red.

- [ ] **Step 6: Commit**

```bash
git add hardware/reva/place.py hardware/reva/place_check.py
git commit -m "hw(reva): P4-1 module and power header placed; module check, depth report

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: SMD parts by anchor

**Files:**
- Modify: `hardware/reva/place.py`: `decouplers(proj)`, `place_smd(s, proj)`,
  `OVERRIDES`; called from `build()` after `place_power_header`.
- Modify: `hardware/reva/place_check.py`: `check_decoupling`, the ratsnest and
  anchor report lines, sabotages.

**Interfaces:**
- Consumes: `s.blocked`, `s.shadow`, placed panel parts, U_SM, J_PWR.
- Produces:
  - `s.decouplers {cap: (ic_ref, pad_number)}`
  - `s.anchors[ref]` for every SMD part
  - `place.OVERRIDES`
  - `place.pad_index(board) -> {net: [(ref, pad, (x, y))]}`

- [ ] **Step 1: Write the anchor and search code** (append to `place.py`)

```python
# Manual corrections (spec §4.4): ref -> (dx, dy, rot, reason), relative to
# the part's anchor. Empty at the start; an entry names the render that
# justified it.
OVERRIDES = {}

STEP = {"ic": (0.5, 30.0), "power": (0.5, 40.0), "decouple": (0.1, 3.0),
        "led_r": (0.25, 10.0), "other": (0.5, 30.0)}


def pad_index(board):
    out = {}
    for fp in board.GetFootprints():
        for p in fp.Pads():
            n = p.GetNetname()
            if n:
                out.setdefault(n, []).append(
                    (fp.GetReference(), str(p.GetNumber()),
                     (pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y))))
    return out


def _pad_xy(board, ref, number):
    for p in board.FindFootprintByReference(ref).Pads():
        if str(p.GetNumber()) == str(number):
            return pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)
    raise KeyError("%s has no pad %s" % (ref, number))


def _centroid(pts):
    if not pts:
        return None
    return (sum(x for x, _ in pts) / len(pts), sum(y for _, y in pts) / len(pts))


def decouplers(proj):
    """{cap: (ic, VCC pad)}: in every sheet a 100n whose pin 1 is the rail
    and pin 2 GND, directly after an IC whose VCC carries that rail
    (blocks._decouple, called right after each mux and shift register)."""
    out = {}
    for sheet in proj.sheets:
        prev = None
        for p in sheet.parts:
            if p.ref.startswith("U_") and p.ref not in ("U_SM", "U_REG"):
                prev = p
                continue
            if (prev is not None and p.value == "100n" and p.nets.get("2") == BL.GND):
                vcc = str(prev.sym.by_name("VCC"))
                if prev.nets.get(vcc) == p.nets.get("1"):
                    out[p.ref] = (prev.ref, vcc)
            prev = None
    return out


def _ic_anchor(s, part, idx, unplaced):
    """Mux: centroid of its pots' wiper pads (P3). Other ICs: centroid of the
    placed pads on its signal nets, followed through one unplaced 2-pin part."""
    if part.ref.startswith("U_MUX"):
        m = int(part.ref[len("U_MUX"):])
        pm = RB.load_panel_map()
        ids = {p["id"] for p in pm["pots"] if p["mux"] == m}
        refs = [r for r, pid in s.ids.items() if pid in ids]
        return _centroid([_pad_xy(s.board, r, "2") for r in refs])
    pts = []
    for net in set(part.nets.values()) - SUPPLY:
        pts += [xy for ref, _n, xy in idx.get(net, []) if ref != part.ref]
        for q in unplaced:
            if len(q.nets) == 2 and net in q.nets.values():
                other = [n for n in q.nets.values() if n != net][0]
                if other not in SUPPLY:
                    pts += [xy for ref, _n, xy in idx.get(other, []) if ref != part.ref]
    return _centroid(pts)


def _place_one(s, part, target, cls, accept=None):
    fp = kipcb.add_part(s.board, part, target[0], target[1], 0, side="B")
    inner = PL.grow(outline_box(), -EDGE_INSET)
    step, rmax = STEP[cls]
    rots = (0, 90, 180, 270)
    if part.ref in OVERRIDES:
        dx, dy, rot, reason = OVERRIDES[part.ref]
        target = (target[0] + dx, target[1] + dy)
        step, rmax, rots = 0.1, 0.0, (rot,)
        s.overrides_used.append("%s %+.2f %+.2f rot %d: %s" % (part.ref, dx, dy, rot, reason))
    PL.first_fit(fp, target, s.blocked, inner, step, rmax, rotations=rots, accept=accept)
    s.parts[part.ref] = part
    s.anchors[part.ref] = target
    return fp


def place_smd(s, proj):
    """Spec §4.4 order: power block, ICs (most pins first), decouplers, J_SD's
    caps, LED resistors, C_SENSE, everything else."""
    by_ref = {p.ref: p for p in proj.parts()}
    sheet_of = proj.sheet_of()
    todo = [p for p in proj.parts() if p.on_board and p.footprint
            and p.ref not in s.parts]
    jp = s.board.FindFootprintByReference("J_PWR")
    jp_c = PL.courtyard_box(jp)
    jp_centre = ((jp_c[0] + jp_c[2]) / 2.0, (jp_c[1] + jp_c[3]) / 2.0)
    s.decouplers = decouplers(proj)

    power = [p for p in todo if sheet_of[p.ref] == "power"]
    power.sort(key=lambda p: (p.ref != "U_REG", p.ref))
    for p in power:
        _place_one(s, p, jp_centre, "power")

    left = [p for p in todo if p.ref not in s.parts]
    ics = sorted([p for p in left if p.ref.startswith("U_")],
                 key=lambda p: (-len(p.sym.pins()) if hasattr(p.sym, "pins") else 0, p.ref))
    for p in ics:
        unplaced = [q for q in todo if q.ref not in s.parts]
        a = _ic_anchor(s, p, pad_index(s.board), unplaced)
        if a is None:
            raise ValueError("%s: no anchor (no placed pad on its signal nets)" % p.ref)
        _place_one(s, p, a, "ic")

    for cref, (ic, vcc) in sorted(s.decouplers.items()):
        v = _pad_xy(s.board, ic, vcc)

        def near(fp, v=v):
            x, y = [(pcbnew.ToMM(q.GetPosition().x), pcbnew.ToMM(q.GetPosition().y))
                    for q in fp.Pads() if str(q.GetNumber()) == "1"][0]
            return math.hypot(x - v[0], y - v[1]) <= DECOUPLE_MAX_MM

        _place_one(s, by_ref[cref], v, "decouple", accept=near)

    sd_vcc = _pad_xy(s.board, "J_SD", "4")
    for cref in ("C_SD1", "C_SD2"):
        acc = None
        if cref == "C_SD1":
            def acc(fp, v=sd_vcc):
                x, y = [(pcbnew.ToMM(q.GetPosition().x), pcbnew.ToMM(q.GetPosition().y))
                        for q in fp.Pads() if str(q.GetNumber()) == "1"][0]
                return math.hypot(x - v[0], y - v[1]) <= DECOUPLE_MAX_MM
            s.decouplers[cref] = ("J_SD", "4")
        _place_one(s, by_ref[cref], sd_vcc, "decouple" if cref == "C_SD1" else "other", accept=acc)

    idx = pad_index(s.board)
    for p in sorted((p for p in todo if p.ref not in s.parts and sheet_of[p.ref] == "leds"),
                    key=lambda p: p.ref):
        led_net = p.nets["2"]
        led = [xy for ref, _n, xy in idx.get(led_net, []) if ref.startswith("D")]
        _place_one(s, p, led[0], "led_r")

    for p in sorted((p for p in todo if p.ref.startswith("C_SENSE")), key=lambda p: p.ref):
        net = p.nets["1"]
        sm = [xy for ref, _n, xy in pad_index(s.board).get(net, []) if ref == "U_SM"]
        _place_one(s, p, sm[0], "other")

    for p in sorted((p for p in todo if p.ref not in s.parts), key=lambda p: p.ref):
        idx = pad_index(s.board)
        pts = [xy for net in set(p.nets.values()) - SUPPLY
               for ref, _n, xy in idx.get(net, []) if ref != p.ref]
        a = _centroid(pts) or s.anchors["U_SM"]
        _place_one(s, p, a, "other")
```

Pin count: before writing the `ics` sort key, probe what `p.sym` offers:

```
KIPY -c "import sys; sys.path[:0]=['hardware','hardware/reva']; import build as RB; p=[p for p in RB.project().parts() if p.ref=='U_MUX0'][0]; print([a for a in dir(p.sym) if not a.startswith('_')])"
```

Use its pin list, e.g. `len(p.sym.pins)` or `len(p.sym.pins())` whichever
exists, and replace the `hasattr` expression with it. With all 16-pin ICs
tied, the order is by ref: U_IN1, U_MUX0–9, U_SR1–5.

In `build()`, after `place_power_header(...)`, call `place_smd(s, proj)`.

- [ ] **Step 2: Checks and report**

```python
def check_decoupling(s, pcb_path, prefix):
    if not s.decouplers:
        return False, "examined 0 decouplers", []
    bad, worst = [], 0.0
    for cref, (ic, num) in sorted(s.decouplers.items()):
        c = [p for p in _fp(s.board, cref).Pads() if str(p.GetNumber()) == "1"][0]
        m = [p for p in _fp(s.board, ic).Pads() if str(p.GetNumber()) == num][0]
        d = math.hypot(pcbnew.ToMM(c.GetPosition().x - m.GetPosition().x),
                       pcbnew.ToMM(c.GetPosition().y - m.GetPosition().y))
        worst = max(worst, d)
        if d > P.DECOUPLE_MAX_MM:
            bad.append("%s is %.3f mm from %s pad %s" % (cref, d, ic, num))
    return not bad, "%d decouplers, worst %.3f mm (limit %.1f)" % (
        len(s.decouplers), worst, P.DECOUPLE_MAX_MM), bad


def _mst(pts):
    """Prim's minimum spanning tree length over pad centres."""
    if len(pts) < 2:
        return 0.0
    done, rest, total = [pts[0]], list(pts[1:]), 0.0
    while rest:
        d, j = min((math.hypot(a[0] - b[0], a[1] - b[1]), j)
                   for j, b in enumerate(rest) for a in done)
        total += d
        done.append(rest.pop(j))
    return total
```

Append to `report()`'s lines:

```python
    idx = P.pad_index(s.board)
    nets = {n: [xy for _r, _p, xy in v] for n, v in idx.items() if n not in P.SUPPLY}
    total = sum(_mst(v) for v in nets.values())
    lines.append("ratsnest (signal nets, MST over pad centres): %.0f mm total" % total)
    for ref in sorted(r for r in s.anchors if r.startswith("U_")):
        fp = _fp(s.board, ref)
        ax, ay = s.anchors[ref]
        lines.append("%s %.1f mm from its anchor" % (ref, math.hypot(
            pcbnew.ToMM(fp.GetPosition().x) - ax, pcbnew.ToMM(fp.GetPosition().y) - ay)))
    lines += ["override in force: " + o for o in s.overrides_used] or ["no overrides in force"]
```

Put `("decoupling", check_decoupling)` after `module` in `STEPS`. Sabotages:

```python
def _sab_decoupling(s):
    _fp(s.board, sorted(s.decouplers)[0]).Move(kipcb._pt(3.0, 0.0))


def _sab_decoupling_missing(s):
    s.decouplers.clear()
```

Add both to `SABOTAGES`, with `TURNS_RED` `"decoupling"`. `_sab_edge` and
`_sab_module` now hit their test-point branch.

- [ ] **Step 3: Run, look at the back render, iterate**

Run `KIPY hardware/reva/place.py`. Expected: every step green, the known
items unchanged. Any `no free place for <ref>` is a real finding. Look at
the bottom render around that ref before changing anything. The permitted
levers are:
- that class's `STEP` radius
- an `OVERRIDES` entry whose reason names the render file and what it
  shows

Look at both renders. Muxes should sit among their pot groups and resistors
beside their LEDs. The power block sits at J_PWR, and nothing is in the
module outline. Name what you see in the report.

- [ ] **Step 4: Sabotages** `decoupling`, `decoupling_missing`, and again
`edge` and `module` (now through a TP): each RED with its named step red.

- [ ] **Step 5: Commit**

```bash
git add hardware/reva/place.py hardware/reva/place_check.py
git commit -m "hw(reva): P4-1 SMD parts placed by anchor; decoupling check, ratsnest report

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: DRC, the committed board, the guard

**Files:**
- Modify: `hardware/reva/place_check.py`: `check_drc`, `drc_items`,
  sabotages.
- Create: `hardware/reva/test_place.py`
- Create (generated): `hardware/reva/kicad/reva.kicad_pcb` via `place.py --write`
- Create: `docs/hardware/placement/reva-placed-top.png`, `-bottom.png`
  (copied by `place.py --write`)
- Modify: `CMakeLists.txt` (`reva_place_guard`), `docs/roadmap.md` (one dated
  entry), `docs/hardware/grip-test.md` (the freeze checklist line).

**Interfaces:**
- Consumes: everything above.
- Produces: `place_check.GATED_DRC` and `place_check.drc_items(rpt) -> [(class, [ref, ...])]`.

- [ ] **Step 1: Probe the report format first**

Run `KIPY hardware/reva/place.py`. Then run kicad-cli DRC on
`hardware/reva/out/reva-placed.kicad_pcb` through a scratch script calling
`pcb_proof.drc(pcb, "…/probe.rpt")`, and print the first 60 lines of the
report. Record in the report:
- the exact line shapes of a `[courtyards_overlap]` item
- the shape of a `[copper_edge_clearance]` item
- the shape of a `[clearance]` item

`drc_items` below assumes item lines of the form
`    @(x mm, y mm): <description> of <REF>` or `... Footprint <REF>`.
Adjust its regex to what the probe printed, and keep the probe lines in a
comment above it.

- [ ] **Step 2: The DRC check**

```python
import re

GATED_DRC = ("courtyards_overlap", "pth_inside_courtyard", "shorting_items", "clearance",
             "hole_clearance", "hole_to_hole", "copper_edge_clearance", "items_not_allowed")
FRONT_REPORTED = ("courtyards_overlap", "pth_inside_courtyard")
_REF_RE = re.compile(r"(?:\bof|Footprint) ([A-Za-z_]+[0-9]*[A-Za-z_0-9]*)\s*$", re.M)


def drc_items(rpt_path):
    """[(class, [refs])] per violation block of a kicad-cli report."""
    txt = open(rpt_path, encoding="utf-8", errors="replace").read()
    out = []
    for block in re.split(r"(?=^\[)", txt, flags=re.M):
        m = re.match(r"^\[([a-z0-9_]+)\]", block)
        if m:
            out.append((m.group(1), sorted(set(_REF_RE.findall(block)))))
    return out


def check_drc(s, pcb_path, prefix):
    rpt = prefix + "-drc.rpt"
    try:
        items = PP.drc(pcb_path + (".missing" if getattr(s, "drc_broken", False) else ""), rpt)
    except RuntimeError as e:
        return False, "kicad-cli wrote no report: %s" % str(e)[:200], []
    blocks = drc_items(rpt)
    found, front_crtyd = {}, []
    front = set(s.front)
    for cls, refs in blocks:
        if cls not in GATED_DRC:
            continue
        if cls in FRONT_REPORTED and refs and set(refs) <= front:
            # Front-side courtyards overlap by design on this panel (the P4a
            # strip already had 6 LED/pot/jack overlaps unrouted); the physical
            # question is the front check's body test (spec §5.1 item 6
            # amendment).
            front_crtyd.append("%s %s" % (cls, "/".join(sorted(_key(s, r) for r in refs))))
            continue
        key = "%s %s" % (cls, "/".join(sorted(_key(s, r) for r in refs)))
        found[key] = "kicad-cli"
    ok, details, nk = _judge(s, "drc", found)
    details += ["reported, not gated: " + f for f in front_crtyd]
    others = ", ".join("%s %d" % kv for kv in sorted(items.items()) if kv[0] not in GATED_DRC) or "none"
    return ok, "gated: %d items (%d known), %d front courtyard items reported; not gated: %s" % (
        len(found), nk, len(front_crtyd), others), details
```

Insert `("drc", check_drc)` after `decoupling` in `STEPS`. Sabotages:

```python
def _sab_drc(s):
    """A GND track 0.1 mm beside a decoupler's rail pad: a near miss, not a
    touch (a touch is renamed to the pad's net on save, probed 2026-09-29)."""
    fp = _fp(s.board, sorted(s.decouplers)[0])
    bb = [p for p in fp.Pads() if str(p.GetNumber()) == "1"][0].GetBoundingBox()
    x = pcbnew.ToMM(bb.GetRight()) + 0.1 + 0.125
    kipcb.add_track(s.board, "B.Cu", 0.25, "GND",
                    [(x, pcbnew.ToMM(bb.GetTop()) - 1.0), (x, pcbnew.ToMM(bb.GetBottom()) + 1.0)])


def _sab_drc_missing(s):
    s.drc_broken = True
```

Add both, with `TURNS_RED` `"drc"`.

**Where the near miss can land on an existing via or pad:** check the render
of the sabotage run. If the DRC class that fires is not `clearance`, that is
fine: the check keys on any gated class.

- [ ] **Step 3: Fill `KNOWN_PANEL["drc"]` from the run**

Run `KIPY hardware/reva/place.py`. Every NEW `drc` key must involve a
jack-row part or a SONG pot or lamp (for example
`copper_edge_clearance IN_L`). Copy those into `KNOWN_PANEL["drc"]`. Any
other key is a real finding: stop and report it.

- [ ] **Step 4: Write the guard** `hardware/reva/test_place.py`

```python
#!/usr/bin/env python3
"""Guard for the Rev A placement (P4-1 spec §5.1 item 7, §5.4). Plain script;
exit code is the verdict. Re-runs itself under KiCad's Python.

1. Two builds in separate processes are byte-identical.
2. The committed hardware/reva/kicad/reva.kicad_pcb equals a fresh build.
3. The fresh build is green (known panel violations listed).
4. Every sabotage turns its named step red.
5. KNOWN_PANEL names only jack-row parts and the SONG clusters."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
for _p in (HW, HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)
try:
    import pcbnew  # noqa: F401
except ImportError:
    from gen import ksexp
    kipy = os.path.join(ksexp.KICAD_ROOT, "bin", "python.exe")
    sys.exit(subprocess.call([kipy, os.path.abspath(__file__)]))

import io                    # noqa: E402
import contextlib            # noqa: E402
import assign                # noqa: E402
import place as P            # noqa: E402
import place_check as PC     # noqa: E402

FAILS = []
KIPY = sys.executable


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def build_in(dirname):
    r = subprocess.run([KIPY, os.path.join(HERE, "place.py"), "--out", dirname],
                       capture_output=True, text=True)
    return r.returncode, os.path.join(dirname, "reva-placed.kicad_pcb"), r.stdout + r.stderr


def red_steps(s, pcb, prefix):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        PC.run(s, pcb, prefix)
    return {line.split()[2] for line in buf.getvalue().splitlines() if line.startswith("RED ")}


def main():
    t1, t2 = tempfile.mkdtemp(prefix="place1_"), tempfile.mkdtemp(prefix="place2_")
    rc1, pcb1, out1 = build_in(t1)
    rc2, pcb2, _ = build_in(t2)
    check(rc1 == 0, "a fresh build is green (rc %d)" % rc1)
    if rc1:
        print(out1[-3000:])
    same = os.path.exists(pcb1) and os.path.exists(pcb2) and open(pcb1, "rb").read() == open(pcb2, "rb").read()
    check(same, "two builds in separate processes are byte-identical")
    check(os.path.exists(P.COMMITTED) and open(P.COMMITTED, "rb").read() == open(pcb1, "rb").read(),
          "committed %s equals a fresh build (rerun place.py --write)" % os.path.relpath(P.COMMITTED))

    jack_ids = {h["id"] for h in assign.load_holes() if abs(h["y_mm"] - 114.0) < 1e-6}
    allowed = jack_ids | {"SONG_A", "SONG_B", "SONG_A_L", "SONG_B_L"}
    for chk, keys in sorted(PC.KNOWN_PANEL.items()):
        for k in sorted(keys):
            names = set(k.replace("/", " ").split())
            check(names & allowed, "KNOWN_PANEL[%s] %r names a jack-row part or a SONG cluster" % (chk, k))

    base = P.build()
    for name in sorted(PC.SABOTAGES):
        d = tempfile.mkdtemp(prefix="sab_")
        pcb = os.path.join(d, "reva-placed.kicad_pcb")
        P.save(base, pcb)
        s = base.reload(pcb)
        with contextlib.redirect_stdout(io.StringIO()):
            PC.sabotage(s, name)
        P.save(s, pcb)
        red = red_steps(s, pcb, os.path.join(d, "reva-placed"))
        want = PC.TURNS_RED[name]
        check(want in red, "sabotage %s turns %s red (red: %s)" % (name, want, sorted(red)))

    print("FAILED: %d" % len(FAILS) if FAILS else "all placement checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
```

`red_steps` parses the step name from lines like `RED 2. edge  ...`:
`split()[2]` is the name. `render` runs on every sabotage too; that is
acceptable, though slow. If the guard takes more than 5 minutes, add
`s.skip_render = True` to the sabotage loop and have `render()` return
`True, "skipped", []` when the flag is set. Measure before you add it.

- [ ] **Step 5: Commit the board, wire ctest, run the guard**

- Run `KIPY hardware/reva/place.py --write`. It must end GREEN, and
  `hardware/reva/kicad/reva.kicad_pcb` must now exist.
- Check `git status --short hardware/reva/kicad`: only `reva.kicad_pcb` is
  new, and `reva.kicad_pro` is untouched. If `reva.kicad_pro` changed, the
  copy step wrote more than the board; fix that before going on.

```cmake
add_test(NAME reva_place_guard
         COMMAND ${Python3_EXECUTABLE}
                 ${CMAKE_CURRENT_SOURCE_DIR}/hardware/reva/test_place.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
```

Reconfigure and run `ctest --test-dir build -R "hw_gen|reva|coupon" --output-on-failure`.
Expected: all pass, `reva_place_guard` included. Put its wall time in the
report.

Prove the guard's own RED once: temporarily add
`KNOWN_PANEL["edge"].add("PITCH_A_BOGUS")` (a jack-row-looking name that
never fails). The guard must go red on `known_stale`-style staleness in the
fresh build ("listed as known but no longer fails"). Remove it with the Edit
tool.

- [ ] **Step 6: Docs**

- **Renders:** `place.py --write` (Step 5) has already copied
  `reva-placed-top.png` and `-bottom.png` into `docs/hardware/placement/`.
  Check that both files exist.
- **`docs/roadmap.md`:** add above the 2026-09-29 P4a entry:

  ```
  **2026-09-29 — P4-1 placement: Rev A is placed.**
  `KIPY hardware/reva/place.py` places all <N> parts on a 300.8 × 110 mm,
  4-layer board from P3's netlist and P1's hole list, and
  `reva_place_guard` rebuilds it byte for byte, runs every check and proves
  each can go red. Known panel violations wait for the panel pass after the
  grip test: the jack row (`JACK_Y` must drop to ≤ 112.77) and the SONG
  lamps (2.6 mm lower). Spec
  `docs/superpowers/specs/2026-09-29-rev-a-p4-1-placement-design.md`;
  renders `docs/hardware/placement/`.
  ```

  Fill `<N>` from the run.
- **`docs/hardware/grip-test.md`:** find the freeze paragraph that mentions
  `panel-freeze-2026-11-06`, and add one sentence: "The freeze also needs
  `KNOWN_PANEL` in `hardware/reva/place_check.py` to be empty
  (P4-1 spec §5.3)."

- [ ] **Step 7: Commit**

```bash
git add hardware/reva/place_check.py hardware/reva/test_place.py hardware/reva/kicad/reva.kicad_pcb docs/hardware/placement CMakeLists.txt docs/roadmap.md docs/hardware/grip-test.md
git commit -m "hw(reva): P4-1 DRC check, the placed board committed, reva_place_guard

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

## Self-review notes (for the controller)

- **Spec coverage:**

  | Spec section | Task |
  |---|---|
  | §4.1 outline, layers, edge clearance | 3 |
  | §4.2 panel parts, rotations, LED search | 3 |
  | §4.2 SD footprint | 2 |
  | §4.3 module, shadow, USB clearance, J_PWR, depth report | 4 |
  | §4.4 anchors, search, overrides | 5 |
  | §5.1 items 1–3 | 3 |
  | §5.1 item 4 | 4 |
  | §5.1 item 5 | 5 |
  | §5.1 item 6 | 6 |
  | §5.1 item 7 | 6 |
  | §5.2 reports | 3–5 |
  | §5.3 known list and staleness | 3, 6 |
  | §5.4 sabotage with `*_missing` | 3–6 |
  | §6 renders and committed board | 3–6 |
  | §7 files and ctest | 1, 2, 6 |
  | spike deletion | 1 |

- **"One part per hole, one hole per panel part"** (spec §5.1 item 1) is
  already asserted by P3's `rule_panel_ids` in `reva_check_guard`. The
  anchors check adds the 0.01 mm position half.
- **Spec amendments made while planning** (all three are in the spec):
  - §4.2: the SD source is LCSC/EasyEDA.
  - §4.3: 35 mm USB clearance instead of a corridor, module rot 0/180, the
    shadow from the silkscreen.
  - §5.1 item 6: front-only courtyard classes are reported, not gated.
- **Deliberately probed during execution, not while planning:**
  - The DRC item-line format (Task 6 step 1).
  - `sym`'s pin-list attribute (Task 5 step 1).
  - The exact known-violation keys (Tasks 3 and 6). The spec §3 predicts them;
    the run lists them.
- **Known limit handed to P4-2/P4-3:** the committed `reva.kicad_pro` comes
  from `build.py` and carries no board design settings (edge clearance,
  later the net classes). P4-2/P4-3 must merge the board settings into
  `C.write_project_file`, or the KiCad GUI shows default rules.
