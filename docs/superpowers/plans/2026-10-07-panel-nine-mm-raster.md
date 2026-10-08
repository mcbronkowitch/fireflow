# Panel correction — the 9 mm raster: Implementation Plan

> **Amended 2026-10-08 after Task 7a (read this over the numbers below; the
> plan body is not rewritten):**
> - The centre runs on its own pitch, `CENTRE_PITCH` = **23.0**: centre
>   columns **129.40, 152.40, 175.40** (not 132.20 / 172.60); deck columns
>   unchanged. Smallest cap gap 9.70, not 10.27.
> - `SIDE_LAMPS` is gone: SONG's lamp is a cluster lamp (`KNOB_LAMPS`).
> - Every cluster lamp follows spec §5 / §5.1 as amended 2026-10-08: word
>   centred under its knob, LED on its glyph midline at `knob.x ± LED_DX`
>   (6.9 pots, 6.0 the REC key) toward its own group, deck B mirrored.
> - `place.py`: U_SM may turn 90/270 (P4.1 §4.3) and the LED rotation pick
>   keeps the LED body off every front body (P4.1 §4.2), both amended
>   2026-10-08.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Re-place every control of the 60 HP plate on one 20.2 × 20.125 mm raster that keeps ≥ 9 mm (in fact ≥ 10.27 mm) between cap edges, rework the LEDs, raise the jack row, and carry the new plate through the cut file, the mux assignment, the Rev A schematic and board, the firmware table and the VCV `FireflowHW` panel.

**Architecture:** `host/vcv/res/gen_hw_panel.py` stays the single source of positions; it gains a cell table (rows × columns) instead of hand-written coordinates, and group fields are built from cells by a new pure-geometry module `host/vcv/res/hw_fields.py`. Everything downstream is a re-run of the existing scripted chain (`gen_hw_cut.py` → `hardware/reva/assign.py` → `build.py` → `place.py` → `route.py` → `fab.py`, and `shell/gen_panel_map.py`), with two rule changes: `assign.py` may split one column across mux bands, and `gen_panel_map.py` accepts reserved pots that send nothing.

**Tech Stack:** Python 3 (plain-script guards, no pytest), KiCad 10 Python (`pcbnew`) for the board, C++17 + doctest for the engine/host tests, clang + Ninja (Release) for the desktop build, the VCV Rack SDK via `host/vcv/build-local.sh`.

**Spec:** `docs/superpowers/specs/2026-10-07-panel-nine-mm-raster-design.md` — read it before any task; section numbers below (§n) refer to it.

## Global Constraints

- Everything written into the repo is **English** (code, comments, commit messages, docs).
- Commit trailer, every commit: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>` (CLAUDE.md; not the Anthropic default).
- **Never prefix a shell command with `cd`**, never write files from the shell (use the editor tools), never put a write behind `&&` — the Bash tool starts in the repo root. A command that needs a working directory goes into a script file in the scratchpad.
- Engine/tests/render build: `source env.sh`, then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, `cmake --build build`, `ctest --test-dir build --output-on-failure`. **Release is mandatory** (Debug fails the render-hash gates). **ctest does not build** — always `cmake --build build` first.
- Never `source env.sh` in a shell that builds Daisy firmware; this plan builds no firmware.
- VCV: only `host/vcv/build-local.sh` (and `build-local.sh install` to finish). The panel generators and their guards must run with `host/vcv/` as working directory (they import `gen_panel` from `res/`). Guards run through ctest (`hw_panel_guard`, `panel_guard`, `hw_cut_guard`, `hw_fields_guard`), which sets it; the generators run through the scratchpad script `run_gen.py` written in Task 2 Step 1.
- KiCad Python: `KIPY=/c/Users/bernd/AppData/Local/Programs/KiCad/10.0/bin/python.exe` (`hardware/gen/ksexp.py` `KICAD_ROOT`).
- Raster constants, verbatim from the spec: column pitch **20.20 mm**; rows **14.500, 34.625, 54.750, 74.875, 95.000**; deck A columns **11.00, 31.20, 51.40, 71.60, 91.80, 112.00**; centre columns **132.20, 152.40, 172.60**; deck B mirrored about 152.4 (x → 304.8 − x).
- Cap radii for the guard, verbatim: big **6.0**, small **3.85**, key **3.0**; minimum gap **9.0**.
- `JACK_Y` = **112.75**. `FIELD_MARGIN` = **1.45**. `BOX_GAP` = 3.0 (unchanged). `LEGEND_INSET` = 4.0 (unchanged).
- Final LED count on the plate: **15**. Pots: **73**. Mux inputs used: **75 of 80**.
- **No runtime claim without a probe** (CLAUDE.md probe rule): a number that goes into a doc or a test comment is printed by code first.
- A test that cannot go red gets fixed; prove each new guard RED once.
- **Start from a clean tree.** On 2026-10-07 `git status` listed four `hardware/reva/` files as modified; they were byte-identical to HEAD (a stale index stat) and cleared with `git add`. If `git status` shows anything before a task starts, find out what it is before touching it.

## Review Focus

1. **A caption or lamp that the SVG shows clear but a Rack widget covers** — only legends are checked against `RACK_R`. Task 9's headless render is read for every knob caption and lamp; a covered one is a finding, not a pass.
2. **A reserved pot that sends a value** — `ROOT_A`, `ROOT_B`, `REV_MOD` must reach the firmware table as rows that send nothing. Task 8 adds a check that their rows carry `-1` and a reason starting `reserved:`.
3. **Deck B fields that are not deck A mirrored** — the new field builder computes both decks independently from positions. Task 2's field guard compares deck B's rects with deck A's mirrored rects.
4. **The relaxed mux split taking a split when none is needed** — Task 5 adds a synthetic case that fits without a split and asserts none is taken.
5. **A stale `SYNC_L` / `CEIL_L` / `SRC_*` / `FLT_*` / `CLR_*` name** surviving in code that compiles today only because the old enum still exists — Task 3 ends with a repo-wide grep that must come back empty outside history docs.

---

## File Structure

| File | Responsibility | Tasks |
|---|---|---|
| `host/vcv/res/hw_fields.py` (new) | Pure geometry of a field: rect-union outline, containment, gaps | 1 |
| `host/vcv/res/test_hw_fields.py` (new) | Guard for `hw_fields.py` | 1 |
| `CMakeLists.txt` | ctest entry `hw_fields_guard` | 1 |
| `host/vcv/res/gen_hw_panel.py` | Raster, cell table, reserved knobs, lamps, jack row, fields from cells, round keys | 2, 3 |
| `host/vcv/res/test_hw_panel.py` | 9 mm guard, cell guard, field guard; obsolete pins removed; LED tests | 2, 3 |
| `host/vcv/res/FireflowHW.svg`, `host/vcv/src/generated_hw_panel.hpp` | Regenerated outputs | 2, 3 |
| `host/vcv/res/gen_panel.py`, `host/vcv/src/generated_panel.hpp` | LightId inventory (`HW_ONLY_LIGHTS`) | 3 |
| `host/vcv/src/led_law.hpp`, `tests/test_led_law.cpp` | Lamps that no longer exist, `CLK_L`, MOD + limiter on one lamp | 3 |
| `host/vcv/res/FireflowHW-holes.json`, `-cut.svg`, `-print.svg`, `res/test_hw_cut.py` | Hole list, cut, print; hole counts | 4 |
| `hardware/reva/assign.py`, `test_assign.py`, `panel-map.json` | Relaxed band split | 5 |
| `hardware/reva/blocks.py` (+ `test_build.py` if it pins LEDs) | 15 LED nets on the 595 chain | 6 |
| `hardware/reva/place.py`, `place_check.py`, `test_place.py`, `route_check.py`, `test_route.py`, `kicad/reva.kicad_pcb`, fab outputs, `docs/hardware/placement/` | The board | 7 |
| `shell/gen_panel_map.py`, `shell/test_gen_panel_map.py`, `shell/generated_panel_map.h`, `tests/test_controls_map.cpp`, `tests/test_mux_plan.cpp` | Firmware table with reserved pots | 8 |
| `docs/hardware/grip-test.md`, `docs/roadmap.md`, `docs/hardware/io-budget.md` | Docs | 10 |

---

### Task 1: `hw_fields.py` — rectilinear field geometry

**Files:**
- Create: `host/vcv/res/hw_fields.py`
- Create: `host/vcv/res/test_hw_fields.py`
- Modify: `CMakeLists.txt` (after the `hw_panel_guard` entry, ~line 376)

**Interfaces:**
- Produces (used by Task 2):
  - `hw_fields.outline(rects) -> list[tuple[float, float]]` — clockwise on screen (y down), first vertex is the top-left-most corner, first edge runs right along the top; raises `ValueError` unless the union is one piece without holes.
  - `hw_fields.Field(rects)` with attributes `rects` (list of `(x0, x1, y0, y1)` tuples), `outline`, `x`, `y` (first vertex), `w` (length of the first, top edge), `h` (lowest rect bottom − `y`) and methods `covers(x, y) -> bool`, `contains_rect(x0, x1, y0, y1) -> bool`, `gap_to(other) -> float` (smallest Euclidean distance between the two unions, 0.0 when they touch or overlap), `overlaps(other) -> bool` (positive-area intersection).

- [ ] **Step 1: Write the failing test**

`host/vcv/res/test_hw_fields.py`:

```python
"""Guard for hw_fields.py (spec 2026-10-07 §7): the outline of a union of
axis-aligned rects, containment and gaps. Plain script; exit code is the
verdict. Run from host/vcv/: python res/test_hw_fields.py"""
import sys
import hw_fields as F

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def test_rectangle():
    pts = F.outline([(0, 10, 0, 5)])
    check(pts == [(0, 0), (10, 0), (10, 5), (0, 5)], f"rectangle outline {pts}")


def test_l_shape_from_two_rects_and_a_join():
    # upper band wide, lower band narrow, joined by a filler -- MOTION's shape
    rects = [(0, 30, 0, 10), (0, 15, 13, 20), (0, 15, 10, 13)]
    pts = F.outline(rects)
    check(pts == [(0, 0), (30, 0), (30, 10), (15, 10), (15, 20), (0, 20)],
          f"L outline {pts}")


def test_arch():
    # TIMING's arch: a top band and two legs, nothing between the legs
    rects = [(0, 30, 0, 10), (0, 8, 10, 30), (22, 30, 10, 30)]
    pts = F.outline(rects)
    check(pts == [(0, 0), (30, 0), (30, 30), (22, 30), (22, 10),
                  (8, 10), (8, 30), (0, 30)], f"arch outline {pts}")


def test_two_pieces_are_refused():
    for label, rects in (("apart", [(0, 5, 0, 5), (10, 15, 0, 5)]),
                         ("corner pinch", [(0, 5, 0, 5), (5, 10, 5, 10)]),
                         ("ring with a hole", [(0, 30, 0, 5), (0, 30, 25, 30),
                                               (0, 5, 5, 25), (25, 30, 5, 25)])):
        try:
            F.outline(rects)
            check(False, f"outline accepted {label}")
        except ValueError:
            pass


def test_field_attributes():
    f = F.Field([(0, 30, 0, 10), (0, 8, 10, 30), (22, 30, 10, 30)])
    check((f.x, f.y, f.w, f.h) == (0, 0, 30, 30), f"x/y/w/h {(f.x, f.y, f.w, f.h)}")
    check(f.covers(4, 20) and not f.covers(15, 20), "covers() ignores the arch's gap")
    check(f.contains_rect(1, 7, 2, 28), "a body in the left leg is not inside")
    check(not f.contains_rect(1, 12, 2, 28), "a body poking into the gap is inside")
    check(f.contains_rect(2, 28, 1, 9), "a body across the top band is not inside")


def test_gaps():
    a = F.Field([(0, 10, 0, 10)])
    b = F.Field([(13, 20, 0, 10)])
    c = F.Field([(13, 20, 14, 20)])
    d = F.Field([(5, 15, 5, 15)])
    check(abs(a.gap_to(b) - 3.0) < 1e-9, f"side gap {a.gap_to(b)}")
    check(abs(a.gap_to(c) - 5.0) < 1e-9, f"diagonal gap {a.gap_to(c)}")
    check(a.gap_to(d) == 0.0 and a.overlaps(d), "overlap not seen")
    check(not a.overlaps(b), "a 3 mm gap counted as overlap")


def main():
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print(f"FAIL ({len(FAILS)}):")
        for f in FAILS:
            print("  -", f)
        return 1
    print("PASS -- hw_fields ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run it to make sure it fails**

Add the ctest entry to `CMakeLists.txt` right after `hw_panel_guard`:

```cmake
# The group fields of the 60 HP plate are rectilinear unions of cell rects
# (spec docs/superpowers/specs/2026-10-07-panel-nine-mm-raster-design.md §7).
add_test(
    NAME hw_fields_guard
    COMMAND ${Python3_EXECUTABLE} res/test_hw_fields.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/host/vcv
)
```

Run: `source env.sh && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && ctest --test-dir build -R hw_fields_guard --output-on-failure`
Expected: FAIL with `ModuleNotFoundError: No module named 'hw_fields'`.

- [ ] **Step 3: Write the implementation**

`host/vcv/res/hw_fields.py`:

```python
"""Group fields of the 60 HP plate (spec docs/superpowers/specs/
2026-10-07-panel-nine-mm-raster-design.md §7).

A field is the union of axis-aligned rects (x0, x1, y0, y1), in mm, y down.
This module owns the geometry only: the union's outline as one closed
rectilinear polygon, point and rect containment, and the gap between two
fields. Lettering, colour and the legend notch stay in gen_hw_panel.py.

Rectilinear and not just "a rectangle or an L": the centre's TIMING field is
an arch with ROOM standing inside it, so the outline is traced from the union
itself rather than assembled from a fixed number of bands."""
import itertools
import math

EPS = 1e-9


def _covered(rects, x, y):
    return any(r[0] - EPS <= x <= r[1] + EPS and r[2] - EPS <= y <= r[3] + EPS
               for r in rects)


def _grid(rects, extra_x=(), extra_y=()):
    xs = sorted({v for r in rects for v in (r[0], r[1])} | set(extra_x))
    ys = sorted({v for r in rects for v in (r[2], r[3])} | set(extra_y))
    return xs, ys


def outline(rects):
    """Vertices of the union's boundary, clockwise on screen (y down),
    starting at the top-left-most corner so the first edge runs right along
    the top. Collinear vertices are dropped. Raises ValueError unless the
    union is one piece without holes -- a pinch at a corner counts as two."""
    xs, ys = _grid(rects)
    nx, ny = len(xs) - 1, len(ys) - 1
    fill = [[_covered(rects, (xs[i] + xs[i + 1]) / 2, (ys[j] + ys[j + 1]) / 2)
             for j in range(ny)] for i in range(nx)]

    def filled(i, j):
        return 0 <= i < nx and 0 <= j < ny and fill[i][j]

    nxt = {}

    def edge(a, b):
        if a in nxt:
            raise ValueError("the union touches itself at grid corner %r" % (a,))
        nxt[a] = b

    for i in range(nx):
        for j in range(ny):
            if not fill[i][j]:
                continue
            if not filled(i, j - 1):
                edge((i, j), (i + 1, j))              # top, left to right
            if not filled(i + 1, j):
                edge((i + 1, j), (i + 1, j + 1))      # right, top to bottom
            if not filled(i, j + 1):
                edge((i + 1, j + 1), (i, j + 1))      # bottom, right to left
            if not filled(i - 1, j):
                edge((i, j + 1), (i, j))              # left, bottom to top
    if not nxt:
        raise ValueError("empty field")
    start = min(nxt, key=lambda p: (p[1], p[0]))
    loop, p = [start], nxt[start]
    while p != start:
        loop.append(p)
        p = nxt[p]
        if len(loop) > len(nxt):
            raise ValueError("the outline does not close")
    if len(loop) != len(nxt):
        raise ValueError("the field is not one piece without holes "
                         "(%d of %d boundary edges on the outer loop)"
                         % (len(loop), len(nxt)))
    pts = [(xs[i], ys[j]) for i, j in loop]
    out = []
    for k, b in enumerate(pts):
        a, c = pts[k - 1], pts[(k + 1) % len(pts)]
        if (abs(a[0] - b[0]) < EPS and abs(b[0] - c[0]) < EPS) or \
           (abs(a[1] - b[1]) < EPS and abs(b[1] - c[1]) < EPS):
            continue
        out.append(b)
    k0 = min(range(len(out)), key=lambda k: (out[k][1], out[k][0]))
    return out[k0:] + out[:k0]


def contains_rect(rects, x0, x1, y0, y1):
    """True when the whole of (x0, x1, y0, y1) lies inside the union. Cut on
    every edge of the union and of the box, so a box straddling an inner
    corner cannot pass on its four corners alone."""
    xs, ys = _grid(rects, (x0, x1), (y0, y1))
    xs = [v for v in xs if x0 - EPS <= v <= x1 + EPS]
    ys = [v for v in ys if y0 - EPS <= v <= y1 + EPS]
    return all(_covered(rects, (xs[i] + xs[i + 1]) / 2, (ys[j] + ys[j + 1]) / 2)
               for i in range(len(xs) - 1) for j in range(len(ys) - 1))


def _rect_gap(a, b):
    dx = max(a[0] - b[1], b[0] - a[1], 0.0)
    dy = max(a[2] - b[3], b[2] - a[3], 0.0)
    return math.hypot(dx, dy)


class Field:
    """One group field. x, y is the outline's first vertex (top-left), w the
    length of its first edge along the top -- the edge the legend straddles --
    and h the drop from y to the lowest rect's bottom."""

    def __init__(self, rects):
        self.rects = [tuple(float(v) for v in r) for r in rects]
        self.outline = outline(self.rects)
        (self.x, self.y), (x1, _y1) = self.outline[0], self.outline[1]
        self.w = x1 - self.x
        self.h = max(r[3] for r in self.rects) - self.y

    def covers(self, x, y):
        return _covered(self.rects, x, y)

    def contains_rect(self, x0, x1, y0, y1):
        return contains_rect(self.rects, x0, x1, y0, y1)

    def gap_to(self, other):
        return min(_rect_gap(a, b) for a, b in itertools.product(self.rects, other.rects))

    def overlaps(self, other):
        return any(min(a[1], b[1]) - max(a[0], b[0]) > EPS and
                   min(a[3], b[3]) - max(a[2], b[2]) > EPS
                   for a, b in itertools.product(self.rects, other.rects))
```

- [ ] **Step 4: Run it to make sure it passes**

Run: `ctest --test-dir build -R hw_fields_guard --output-on-failure`
Expected: PASS, `PASS -- hw_fields ok`.

- [ ] **Step 5: Prove a check can go red**

Temporarily change the expected arch outline in `test_arch` (swap `(22, 10)` and `(8, 10)`), run the guard, see `FAIL (1)`, restore with the editor (not `git checkout`), re-run green.

- [ ] **Step 6: Commit**

```bash
git add host/vcv/res/hw_fields.py host/vcv/res/test_hw_fields.py CMakeLists.txt
git commit -m "feat(hw-panel): hw_fields -- rectilinear group fields from cell rects

Outline of a rect union as one polygon, containment and gaps, so the 9 mm
raster's TIMING arch can be drawn (spec 2026-10-07 §7). Guarded by
hw_fields_guard.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 2: The raster in `gen_hw_panel.py`

The big one. Knobs move onto the cell table, the reserved pots appear, the jack row rises, fields come from cells, keys draw round. The LED **inventory** stays as it is in this task (Task 3 changes it) — but the REC lamp joins the caption cluster and the SONG lamp moves beside its knob, because both are layout.

**Files:**
- Modify: `host/vcv/res/gen_hw_panel.py`
- Modify: `host/vcv/res/test_hw_panel.py`
- Regenerate: `host/vcv/res/FireflowHW.svg`, `host/vcv/src/generated_hw_panel.hpp`

**Interfaces:**
- Consumes: `hw_fields.Field` (Task 1).
- Produces (used by Tasks 3, 4, 9 and the guards): `COL_PITCH = 20.2`, `ROW_Y`, `deck_col_x(col)`, `centre_col_x(k)`, `DECK_CELLS`, `CENTRE_CELLS`, `DECK_GROUPS`, `CENTRE_GROUPS`, `RESERVED` (enum → caption), `KNOB_LAMPS`, `SIDE_LAMPS`, `LAMP_OWNER`, `FIELD_MARGIN = 1.45`, `JACK_Y = 112.75`, `Box(hw_fields.Field)` with `n`, `side`, `stem`, `prints_legend`, `legend_y`, `notch`; `BOXES`; `box_of(c)`.

- [ ] **Step 1: A generator runner in the scratchpad**

The generators must run with `host/vcv/` as working directory. Create `<scratchpad>/run_gen.py` (scratchpad path from the session; not in the repo):

```python
"""Run the VCV panel generators and guards from host/vcv/ without a cd."""
import os, subprocess, sys
VCV = r"C:\Users\bernd\Documents\AI\FireFlow\host\vcv"
os.chdir(VCV)
scripts = sys.argv[1:] or ["res/gen_panel.py", "res/gen_hw_panel.py", "res/gen_hw_cut.py"]
rc = 0
for s in scripts:
    print("==>", s, flush=True)
    rc |= subprocess.call([sys.executable, s])
sys.exit(rc)
```

Use it as `python <scratchpad>/run_gen.py res/gen_hw_panel.py`.

- [ ] **Step 2: Write the failing guards**

In `host/vcv/res/test_hw_panel.py`, add after `test_no_overlap_with_hw_radii`:

```python
# Real caps, written HERE and not read from hw.BODY_R: the 9 mm rule is a
# promise about the bought parts (spec 2026-10-07 §2), and a guard that
# imports its radii from the generator would loosen with it.
CAP_R = {"G": 6.0, "S": 3.85, "P": 3.0}
MIN_CAP_GAP = 9.0


def _cap_items():
    """Every knob position (params and reserved, ATTACK/STAGES once) and
    every key, as (enum, class, x, y)."""
    seen, out = set(), []
    for c in hw.HW_PARAMS + hw.HW_ONLY:
        cls = hw.hw_class(c.enum)
        if cls not in CAP_R:
            continue
        key = (round(c.x, 6), round(c.y, 6))
        if key in seen:
            continue
        seen.add(key)
        out.append((c.enum, cls, c.x, c.y))
    return out


def _cap_gap_failures(items):
    bad = []
    for i, (ea, ca, xa, ya) in enumerate(items):
        for eb, cb, xb, yb in items[i + 1:]:
            gap = ((xa - xb) ** 2 + (ya - yb) ** 2) ** 0.5 - CAP_R[ca] - CAP_R[cb]
            if gap < MIN_CAP_GAP - 1e-6:
                bad.append(f"{ea}/{eb} caps {gap:.2f} mm apart")
    return bad


def test_nine_mm_between_caps():
    """Spec 2026-10-07 §1: no two caps closer than 9 mm, edge to edge."""
    items = _cap_items()
    knobs = [i for i in items if i[1] in ("G", "S")]
    check(len(knobs) == 73, f"{len(knobs)} knob positions, expected 73")
    check(sum(i[1] == "G" for i in knobs) == 14, "expected 14 big caps")
    check(sum(i[1] == "P" for i in items) == 4, "expected 4 keys")
    for f in _cap_gap_failures(items):
        check(False, f)
    # The guard proves it can fail on every run: SHAPE_A moved 2.0 mm toward
    # MOD_A must break the rule (the real gap is 10.35).
    moved = [(e, c, x - 2.0 if e == "SHAPE_A" else x, y) for e, c, x, y in items]
    check(_cap_gap_failures(moved), "the 9 mm guard did not see SHAPE_A moved onto MOD_A")


def test_cells_are_the_source():
    """Every knob and key stands on its cell (spec 2026-10-07 §3)."""
    by = {c.enum: c for c in hw.HW_PARAMS + hw.HW_ONLY}
    seen = 0
    for stem, (row, col) in hw.DECK_CELLS.items():
        for side in "AB":
            c = by.get(f"{stem}_{side}")
            if c is None:
                check(False, f"{stem}_{side} has a cell but no control")
                continue
            x = hw.deck_col_x(col) if side == "A" else hw.W - hw.deck_col_x(col)
            check(abs(c.x - x) < 1e-9 and abs(c.y - hw.ROW_Y[row - 1]) < 1e-9,
                  f"{c.enum} at ({c.x:.3f},{c.y:.3f}), cell says ({x:.3f},{hw.ROW_Y[row - 1]:.3f})")
            seen += 1
    for name, (row, k) in hw.CENTRE_CELLS.items():
        c = by.get(name)
        if c is None:
            check(False, f"{name} has a cell but no control")
            continue
        check(abs(c.x - hw.centre_col_x(k)) < 1e-9 and abs(c.y - hw.ROW_Y[row - 1]) < 1e-9,
              f"{name} is not on its centre cell")
        seen += 1
    check(seen == 2 * len(hw.DECK_CELLS) + len(hw.CENTRE_CELLS), "cells not all checked")
    # Big caps: rows 2 and 4 only, never deck column 6, never side by side.
    for stem, (row, col) in hw.DECK_CELLS.items():
        if hw.HW_SIZE.get(stem) == "G":
            check(row in (2, 4) and col != 6, f"big cap {stem} at R{row} c{col}")
            for other, (r2, c2) in hw.DECK_CELLS.items():
                if other != stem and hw.HW_SIZE.get(other) == "G" and r2 == row:
                    check(abs(c2 - col) > 1, f"big caps {stem} and {other} side by side")
    occupied = {(r, k) for r, k in hw.CENTRE_CELLS.values()}
    check(all((r, -k) in occupied for r, k in occupied), "the centre is not mirror-symmetric")
    for name, (row, k) in hw.CENTRE_CELLS.items():
        if hw.HW_SIZE.get(name) == "G":
            check(row in (2, 4) and k == 0, f"big cap {name} off the centre line or row")
    check(abs(hw.deck_col_x(1) - 11.0) < 1e-9 and abs(hw.deck_col_x(6) - 112.0) < 1e-9,
          "deck columns drifted from 11.00 .. 112.00")


def test_fields_keep_box_gap():
    """Fields of different groups keep BOX_GAP; deck B mirrors deck A
    (spec 2026-10-07 §7)."""
    boxes = hw.BOXES
    check(len(boxes) == 26, f"expected 26 group fields, got {len(boxes)}")
    for i, a in enumerate(boxes):
        for b in boxes[i + 1:]:
            g = a.gap_to(b)
            check(g >= hw.BOX_GAP - 1e-6, f"{a.n}/{a.side} and {b.n}/{b.side} are {g:.2f} mm apart")
    by = {(b.n, b.side): b for b in boxes}
    for name in hw.DECK_GROUPS:
        a, b = by.get((name, "A")), by.get((name, "B"))
        if a is None or b is None:
            check(False, f"{name} lacks a deck field")
            continue
        mir = sorted((round(hw.W - r[1], 6), round(hw.W - r[0], 6), round(r[2], 6), round(r[3], 6))
                     for r in a.rects)
        own = sorted(tuple(round(v, 6) for v in r) for r in b.rects)
        check(mir == own, f"{name}: deck B field is not deck A mirrored")
    check({b.stem for b in boxes} == set(hw.GROUP_ORDER),
          f"fields and GROUP_ORDER disagree: { {b.stem for b in boxes} ^ set(hw.GROUP_ORDER)}")
```

Run: `ctest --test-dir build -R hw_panel_guard --output-on-failure`
Expected: FAIL — `AttributeError: module 'gen_hw_panel' has no attribute 'DECK_CELLS'` (or the new tests' checks).

- [ ] **Step 3: Raster, cells, reserved knobs, jack row**

In `gen_hw_panel.py`:

1. `BODY_R = {"G": 6.0, "S": 3.85, "P": 3.0, "J": 3.1, "L": 1.5}` and update its comment: real parts (spec 2026-10-07 §2; P1 §3.1 lands here). The P comment about the "8 mm square" goes — keys draw as their round 6 mm cap.
2. `HW_SIZE`: add `"ROOT": "S", "REV_MOD": "S"` with a comment `# reserved pots, spec 2026-10-07 §4`.
3. `JACK_Y = 112.75` with the comment: P4-1 measured the jacks' tip pads past the board edge at 114.0; the row must sit at y ≤ 112.77 (spec 2026-10-07 §6).
4. Delete `Y_TOP`, `Y_B1K/Y_B1M/Y_B1G`, `Y_B2K/Y_B2G/Y_B2B/Y_B2L`, `CENTRE_PITCH`, `VOICE_MID`, the whole LEVEL band block (`LEVEL_BAND_L`, `LEVEL_PITCH`, `LEVEL_H_MARGIN`, `_lvl0`, `LEVEL_SLOTS`) and the hand-written `DECK_POS` / `CENTER_POS` dicts with their comments. **Keep** `X_COLOR, X_FILT, X_TIMB, X_LVL` (jack columns) and `JACK_POS`.
5. Insert in their place:

```python
# ---------------------------------------------------------------------------
#  The 9 mm raster (spec docs/superpowers/specs/2026-10-07-panel-nine-mm-
#  raster-design.md §3). One pitch for all fifteen columns -- six per deck,
#  three in the centre -- centred on 152.4; 7 x 20.2 + 6.0 puts deck A's outer
#  big cap 5.0 mm from the nominal edge. Rows run 14.5 .. 95.0. Every gap the
#  grip test asked for (>= 9 mm between caps) is >= 10.27 mm here, measured.
#  Big caps stand in rows 2 and 4 only, never in deck column 6, never side by
#  side: two adjacent big caps would need 21.0 mm.
# ---------------------------------------------------------------------------
COL_PITCH = 20.2
ROW_Y = (14.500, 34.625, 54.750, 74.875, 95.000)
ROW_PITCH = ROW_Y[1] - ROW_Y[0]


def deck_col_x(col):
    """Deck A column 1..6 (1 = outer edge); deck B is W - x."""
    return CX - (8 - col) * COL_PITCH


def centre_col_x(k):
    """Centre column k = -1, 0, +1."""
    return CX + k * COL_PITCH


# Deck A (deck B mirrored): stem -> (row, column). ATTACK and STAGES share a
# knob. ROOT is reserved (§4).
DECK_CELLS = {
    "ENGINE": (1, 1), "STEPS": (1, 2), "SONG": (1, 3), "RATE": (1, 4),
    "MELODY": (1, 5), "REC": (1, 6),
    "MOD": (2, 1), "SHAPE": (2, 2), "DENSITY": (2, 3), "SOURCE": (2, 4),
    "FILT": (2, 5), "DEPTH": (2, 6),
    "SMOOTH": (3, 1), "RANGE": (3, 2), "ATTACK": (3, 3), "STAGES": (3, 3),
    "DECAY": (3, 4), "RES": (3, 5), "SUB": (3, 6),
    "COLOR": (4, 1), "TUNE": (4, 2), "FLUX": (4, 3), "FLUXRATE": (4, 4),
    "COMP": (4, 5), "PAN": (4, 6),
    "ROOT": (5, 1), "DETUNE": (5, 2), "FLUXFB": (5, 3), "LINK": (5, 4),
    "GRIT": (5, 5), "REV_MIX": (5, 6),
}
# Centre: name -> (row, k). Mirror-symmetric: TIMING is an arch (row 2 whole,
# legs at k = -1/+1 down to row 4), ROOM an inverted T inside it. COUPLE
# (printed SYNC) and DRIFT are the arch's mirrored legs: together / apart.
CENTRE_CELLS = {
    "SCALE": (1, -1), "CHOKE": (1, 0), "PULL": (1, 1),
    "TIDE": (2, -1), "MORPH": (2, 0), "PACE": (2, 1),
    "TEMPO": (3, -1), "REV_SIZE": (3, 0), "SHUFFLE": (3, 1),
    "COUPLE": (4, -1), "REV_DECAY": (4, 0), "DRIFT": (4, 1),
    "REV_DIFF": (5, -1), "REV_TONE": (5, 0), "REV_MOD": (5, 1),
}
DECK_GROUPS = {
    "ENG": ("ENGINE",),
    "SEQUENCE": ("STEPS", "SONG", "RATE", "MELODY"),
    "CAPTURE": ("REC",),
    "MOTION": ("MOD", "SHAPE", "DENSITY", "SMOOTH", "RANGE"),
    "VOICE": ("SOURCE", "FILT", "DEPTH", "ATTACK", "STAGES", "DECAY", "RES", "SUB"),
    "PITCH": ("COLOR", "TUNE", "ROOT", "DETUNE"),
    "FLUX": ("FLUX", "FLUXRATE", "FLUXFB", "LINK"),
    "LEVEL": ("COMP", "PAN", "GRIT", "REV_MIX"),
}
CENTRE_GROUPS = {
    "GLOBAL": ("SCALE", "CHOKE", "PULL"),
    "TIMING": ("TIDE", "MORPH", "PACE", "TEMPO", "SHUFFLE", "COUPLE", "DRIFT"),
    "ROOM": ("REV_SIZE", "REV_DECAY", "REV_DIFF", "REV_TONE", "REV_MOD"),
}
# Reserved pots (spec §4): a hole, a caption and a pot on plate and board, a
# mux channel, a firmware row that sends nothing -- no ParamId, no Rack widget.
RESERVED = {"ROOT": ("ROOT", "reserved: per-deck scale root (spec 2026-10-07 §4)"),
            "REV_MOD": ("WOBL", "reserved: reverb tail wobble (spec 2026-10-07 §4)")}


def _deck_xy(stem):
    row, col = DECK_CELLS[stem]
    return deck_col_x(col), ROW_Y[row - 1]


def _centre_xy(name):
    row, k = CENTRE_CELLS[name]
    return centre_col_x(k), ROW_Y[row - 1]


DECK_POS = {s: _deck_xy(s) for s in DECK_CELLS if s not in RESERVED}
CENTER_POS = {n: _centre_xy(n) for n in CENTRE_CELLS if n not in RESERVED}
# MODBTN is a real latch param (spec 2026-08-22 mod-latch-layer §5), placed
# through place() like every sound knob, on the jack row.
CENTER_POS["MODBTN"] = (W - 14.00, JACK_Y)
```

`JACK_Y` must be defined above this block (move its line up if needed).

6. `HwOnly`: kind map gains `"S": gp.SMKNOB`. Replace `HW_ONLY = [...]` with:

```python
HW_ONLY = [
    HwOnly("SHIFTBTN", "P", 14.00, JACK_Y, "SHFT", "reserved, no function"),
    HwOnly("ROOT_A", "S", *_deck_xy("ROOT"), *RESERVED["ROOT"]),
    HwOnly("ROOT_B", "S", W - _deck_xy("ROOT")[0], _deck_xy("ROOT")[1], *RESERVED["ROOT"]),
    HwOnly("REV_MOD", "S", *_centre_xy("REV_MOD"), *RESERVED["REV_MOD"]),
]
```

- [ ] **Step 4: REC lamp into the cluster, SONG lamp beside its knob**

```python
KNOB_LAMPS = {
    "SRC_A_L": "SOURCE_A", "SRC_B_L": "SOURCE_B",
    "FLT_A_L": "FILT_A",   "FLT_B_L": "FILT_B",
    "CLR_A_L": "COLOR_A",  "CLR_B_L": "COLOR_B",
    "LVL_A_L": "COMP_A",   "LVL_B_L": "COMP_B",
    "GATE_A_L": "ATTACK_A", "GATE_B_L": "ATTACK_B",
    "TEMPO_L": "TEMPO",
    # REC's lamp joined the cluster on 2026-10-07: beside the key it reached
    # 0.25 mm out of CAPTURE's cell (spec §5).
    "REC_A_L": "REC_A", "REC_B_L": "REC_B",
}
# SONG's lamp stands BESIDE its knob, inboard, on the knob's line: the top
# row's pots have their pins south, where a cluster LED would land, and the
# row band leaves no room to drop it (spec §5.1).
SIDE_LAMPS = {"SONG_A_L": "SONG_A", "SONG_B_L": "SONG_B"}
LAMP_OWNER = {**KNOB_LAMPS, **SIDE_LAMPS}
```

`LIGHT_POS`: delete the `REC_A_L` / `REC_B_L` entries; the jack-row satellites now read `JACK_Y` (they already do). After the `for lamp, knob_enum in KNOB_LAMPS.items()` loop add:

```python
for lamp, knob_enum in SIDE_LAMPS.items():
    k = _by_param[knob_enum]
    inboard = COL_PITCH / 2.0 if k.x < CX else -COL_PITCH / 2.0
    LIGHT_POS[lamp] = (k.x + inboard, k.y)
```

- [ ] **Step 5: Fields from cells**

Delete `_row_cells`, `_row_ink`, `row_frames`, `ROW_FRAMES`, `ROW1_TOP`, `BAND_INK_MARGIN`, `LEVEL_FOOT_R`, `FOOT_TOP`, `SHOULDER_BOT`, `ROOM_FOOT_L`, `PLATE_EDGE`, the three knob rows of `GROUP_ROWS` (keep the jack row as below), the old `Box` class, `FEET`, `_foot_for`. Keep `BOX_GAP`, `GROUP_ORDER`, the legend constants, `_rounded_poly`, `box_of`. Add `import itertools` and `import hw_fields` at the top. Then:

```python
FIELD_MARGIN = 1.45                 # spec §7: the worst pair allows 1.4825
CELL_HALF = (COL_PITCH - BOX_GAP) / 2.0

# The jack row keeps its own frames (no legends since 2026-08-30).
JACK_ROW_X0, JACK_ROW_CUTS = 28.00, (50.25, 73.25)
JACK_ROW_A, JACK_ROW_B, JACK_ROW_C = ("IN", "CV A", "MOD A"), ("OUT", "CV B", "MOD B"), "CLOCK"
DECK_EDGE, CENTRE_L = 120.0, 123.0


class Box(hw_fields.Field):
    """A group field: a rect union with a name, a side and a legend."""

    def __init__(self, n, side, rects):
        super().__init__(rects)
        self.n, self.side = n, side

    @property
    def stem(self):
        return self.n[:-2] if self.n.endswith((" A", " B")) else self.n

    @property
    def prints_legend(self):
        """The jack row prints none since 2026-08-30."""
        return abs(self.y - JACK_ROW_Y) > 1e-9

    @property
    def legend_y(self):
        return self.y + LEGEND_DY

    @property
    def notch(self):
        if not self.prints_legend:
            return None
        _, ink_r, _, _ = text_run(self.x + LEGEND_INSET, self.legend_y,
                                  LEGEND_SIZE, LEGEND_SPACING, "start", self.n)
        return (self.x + LEGEND_INSET - NOTCH_PAD, ink_r + NOTCH_PAD)


def _ink(c):
    """Everything control c prints: body, caption, and any lamp it owns."""
    r = body_r(c)
    x0, x1, y0, y1 = c.x - r, c.x + r, c.y - r, c.y + r
    if c.label:
        lx, ly, anchor, size, _col = hw_label(c)
        tx0, tx1, ty0, ty1 = text_run(lx, ly, size, 0.0, anchor, c.label)
        x0, x1, y0, y1 = min(x0, tx0), max(x1, tx1), min(y0, ty0), max(y1, ty1)
    lr = BODY_R["L"]
    for lamp, owner in LAMP_OWNER.items():
        if owner == c.enum:
            lx, ly = LIGHT_POS[lamp]
            x0, x1 = min(x0, lx - lr), max(x1, lx + lr)
            y0, y1 = min(y0, ly - lr), max(y1, ly + lr)
    return x0, x1, y0, y1


def _knob_field_rects(members):
    """Spec §7: each control's cell rect (its ink plus FIELD_MARGIN, at least
    its column cell), stretched to its row's band within the group, plus the
    joins between neighbouring cells of the same group."""
    rows = {}
    for c in members:
        x0, x1, y0, y1 = _ink(c)
        cell = [min(x0 - FIELD_MARGIN, c.x - CELL_HALF), max(x1 + FIELD_MARGIN, c.x + CELL_HALF),
                y0 - FIELD_MARGIN, y1 + FIELD_MARGIN]
        rows.setdefault(round(c.y, 6), []).append((c, cell))
    cells = []
    for row in rows.values():
        top = min(cell[2] for _c, cell in row)
        bot = max(cell[3] for _c, cell in row)
        for c, cell in row:
            cell[2], cell[3] = top, bot
            cells.append((c, cell))
    rects = [tuple(cell) for _c, cell in cells]
    for (a, ra), (b, rb) in itertools.combinations(cells, 2):
        if abs(a.y - b.y) < 1e-6 and abs(abs(a.x - b.x) - COL_PITCH) < 1e-6:
            lo, hi = (ra, rb) if a.x < b.x else (rb, ra)
            if lo[1] < hi[0]:
                rects.append((lo[1], hi[0], ra[2], ra[3]))
        elif abs(a.x - b.x) < 1e-6 and abs(abs(a.y - b.y) - ROW_PITCH) < 1e-6:
            up, dn = (ra, rb) if a.y < b.y else (rb, ra)
            if up[3] < dn[2]:
                rects.append((max(up[0], dn[0]), min(up[1], dn[1]), up[3], dn[2]))
    return rects


def _jack_row_cells():
    """(name, side, x, w) for the jack row's frames, left to right."""
    edges = [JACK_ROW_X0] + list(JACK_ROW_CUTS) + [DECK_EDGE]

    def span(i):
        lo = edges[i] + (BOX_GAP / 2.0 if i else 0.0)
        hi = edges[i + 1] - (BOX_GAP / 2.0 if i + 1 < len(JACK_ROW_A) else 0.0)
        return lo, hi

    cells = []
    for i, n in enumerate(JACK_ROW_A):
        lo, hi = span(i)
        cells.append((n, "A", lo, hi - lo))
    cells.append((JACK_ROW_C, "C", CENTRE_L, W - 2 * CENTRE_L))
    for i, n in enumerate(JACK_ROW_B):
        lo, hi = span(i)
        cells.append((n, "B", W - hi, hi - lo))
    return cells


def _jack_row_band():
    spans = [(x, x + w) for _n, _s, x, w in _jack_row_cells()]
    top = bot = None
    for c in ALL_HW:
        if abs(c.y - JACK_Y) > 0.5 or not any(x0 <= c.x <= x1 for x0, x1 in spans):
            continue
        r = body_r(c)
        edges = [(c.y - r, c.y + r)]
        if c.label:
            _lx, ly, _a, size, _col = hw_label(c)
            edges.append((ly - size * FONT_CAP, ly))
        for a, b in edges:
            top = a if top is None else min(top, a)
            bot = b if bot is None else max(bot, b)
    return top - FIELD_MARGIN, bot + FIELD_MARGIN


JACK_ROW_Y = _jack_row_band()[0]


def group_boxes():
    by = {c.enum: c for c in ALL_HW}
    out = []
    for side in "AB":
        for name, stems in DECK_GROUPS.items():
            members = [by[f"{s}_{side}"] for s in stems if f"{s}_{side}" in by]
            out.append(Box(name, side, _knob_field_rects(members)))
    for name, names in CENTRE_GROUPS.items():
        out.append(Box(name, "C", _knob_field_rects([by[n] for n in names])))
    y0, y1 = _jack_row_band()
    for n, side, x, w in _jack_row_cells():
        out.append(Box(n, side, [(x, x + w, y0, y1)]))
    return out


BOXES = group_boxes()
```

`box_of(c)` keeps its body (`b.covers(c.x, c.y)`); update its docstring (no foot any more). Replace `_field_d`:

```python
def _field_d(b):
    """One group field as a path: the union's outline, every corner rounded,
    with a bite out of the top edge where the legend prints."""
    pts = list(b.outline)
    rad = [FIELD_R] * len(pts)
    if b.notch:
        n0, n1 = b.notch
        _x, y = pts[0]
        pts[1:1] = [(n0, y), (n0, y + NOTCH_DEPTH), (n1, y + NOTCH_DEPTH), (n1, y)]
        rad[1:1] = [NOTCH_R] * 4
    return _rounded_poly(pts, rad)
```

- [ ] **Step 6: Keys draw round**

In `svg()`, replace the `hw_class(c.enum) == "P"` `<rect …>` branch with:

```python
        elif hw_class(c.enum) == "P":
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(br)}" '
                      f'fill="{PAD_FILL}" stroke="{pad_accent(c)}" stroke-width="0.3"/>')
```

(`br = body_r(c)` = 3.0. `kFfPadR` in `header()` already reads `BODY_R['P']`.)

- [ ] **Step 7: Retire the tests the raster replaces, rewrite the ones that read old geometry**

In `test_hw_panel.py`:

1. **Delete** `test_row3_ceiling_holds_at_seventy`, `test_level_band_is_evenly_divided`, `test_level_band_clears_rooms_shoulder`, `test_level_band_holds_pan_in_slot_zero`, `test_group_raster_closes`, `test_middle_band_runs_on_three_lines`, `test_rows_are_centred_on_their_ink`, and the helper `_bbox_inside`.
2. `test_bodies_and_captions_sit_inside_their_frame`: replace `_bbox_inside(b, …)` with `b.contains_rect(c.x - r, c.x + r, c.y - r, c.y + r)`; the loose list stays `["CEIL_L", "MODBTN", "MODBTN_L", "SHIFTBTN", "SHIFTBTN_L"]` in this task. Update its docstring: fields are drawn against the real cap radii of spec 2026-10-07 §2.
3. `test_drawing_geometry`: delete everything from `check(abs(hw.JACK_Y - 114.0) …` through the GLOBAL-row loop, and put at the top:

```python
    check(abs(hw.JACK_Y - 112.75) < 1e-9, f"JACK_Y is {hw.JACK_Y}, not 112.75 (spec 2026-10-07 §6)")
    check((hw.SD_W, hw.SD_H) == (11.0, 6.0), f"SD size is {hw.SD_W}x{hw.SD_H}")
    check(abs(hw.SD_Y - hw.JACK_Y) < 1e-9, f"SD_Y is {hw.SD_Y}, not on the jack row")
```

   Keep the SHIFT/MOD-on-jack-row checks, the TEXTS count, the jack-row legend absences, the SEQUENCE/ROOM presence, IN_L caption under the jack, no rail dashes, plate from the top edge, knobs drawn as mounting holes, captions ≤ 4 chars. Update its docstring (no row-rhythm pins any more; the cell guard owns positions).
4. `test_hw_only_inventory`: `kinds.get("P") == 1`, new `kinds.get("S") == 3` ("ROOT_A, ROOT_B, REV_MOD reserved, spec 2026-10-07 §4"), `J` and `L` 0; LED total stays 19 in this task. Update the docstring.
5. `test_led_inventory_after_the_feedback_round`: `abs(by["SYNC_L"].y - 114.0)` → `abs(by["SYNC_L"].y - hw.JACK_Y)`.
6. `test_pad_keycaps_are_dark_and_accented`: the expected pattern becomes the circle of Step 6 (`r="{hw.mm(hw.body_r(c))}"`). Docstring: printed at the real 6 mm cap.
7. `test_caption_gap_is_one_number`: replace the `width="{2 * body_r}"` check with `f'cx="{hw.mm(c.x)}" cy="{hw.mm(c.y)}" r="{hw.mm(hw.body_r(c))}" fill="{hw.PAD_FILL}"' in svg` and its comment (round 6 mm cap).
8. `test_mod_wreaths`: the "no accent body ring" regex now also matches the keycaps' accent edge. Exclude key centres:

```python
    keys = {(hw.mm(c.x), hw.mm(c.y)) for c in hw.ALL_HW if hw.hw_class(c.enum) == "P"}
    for col in set(hw.ACC.values()):
        for m in re.finditer(r'<circle cx="([0-9.]+)" cy="([0-9.]+)"[^>]*stroke="%s" '
                             r'stroke-width="0\.3"' % col, svg):
            check((m.group(1), m.group(2)) in keys,
                  f"an accent body ring in {col} is still printed at ({m.group(1)},{m.group(2)})")
```

   and adjust the comment that says keycaps are rects.
9. `test_knob_lamps_sit_in_the_caption_cluster` docstring: SONG is a side lamp now (`SIDE_LAMPS`); add a small test:

```python
def test_song_lamp_stands_beside_its_knob():
    """Spec 2026-10-07 §5.1: inboard, on the knob's line, half a pitch out."""
    by = {c.enum: c for c in hw.ALL_HW}
    for lamp, knob in hw.SIDE_LAMPS.items():
        l, k = by[lamp], by[knob]
        want = k.x + (hw.COL_PITCH / 2 if k.x < hw.CX else -hw.COL_PITCH / 2)
        check(abs(l.x - want) < 1e-9 and abs(l.y - k.y) < 1e-9,
              f"{lamp} at ({l.x:.2f},{l.y:.2f}), want ({want:.2f},{k.y:.2f})")
    check("SONG_A" not in hw.KNOBS_WITH_LAMPS, "SONG is still a cluster lamp")
```

10. `grep -n "hw\.\(Y_TOP\|Y_B\|LEVEL_\|CENTRE_PITCH\|VOICE_MID\|GROUP_ROWS\|ROW_FRAMES\|FEET\|_row_\|ROW1_TOP\|PLATE_EDGE\)" host/vcv/res/*.py` must come back empty.

- [ ] **Step 8: Regenerate and run**

Run: `python <scratchpad>/run_gen.py res/gen_hw_panel.py` — it prints `params=… inputs=12 outputs=6 lights=19 panel=60HP`.
Run: `cmake --build build && ctest --test-dir build -R "hw_panel_guard|hw_fields_guard|panel_guard" --output-on-failure`
Expected: all PASS. If `test_fields_keep_box_gap` reports a pair under 3.00 mm, stop: the spec's measurement disagrees with the generator — report the pair and its gap, do not change `FIELD_MARGIN` or a cell on your own.

- [ ] **Step 9: Prove the 9 mm guard red by hand once**

In `DECK_CELLS`, temporarily set `"SHAPE": (2, 1)` → the guard must fail (two knobs on one cell; also the cell test). Restore with the editor, regenerate, green again. Record the RED in the commit message.

- [ ] **Step 10: Look at it**

Open `host/vcv/res/FireflowHW.svg` in the browser pane (or render it headless as in `memory: fireflow-vcv-panel-layout`) and compare against the spec's tables §3.1 / §3.2: every knob in its cell, TIMING an arch, ROOM an inverted T, SONG's lamp beside SONG, REC's lamp under REC, round keys, the jack row's frames below the knob fields.

- [ ] **Step 11: Commit**

```bash
git add host/vcv/res/gen_hw_panel.py host/vcv/res/test_hw_panel.py host/vcv/res/FireflowHW.svg host/vcv/src/generated_hw_panel.hpp
git commit -m "feat(hw-panel): the 9 mm raster -- cells, reserved pots, fields from cells, jack row at 112.75

Every knob sits on one 20.2 x 20.125 mm raster (spec 2026-10-07 §3);
smallest cap gap 10.27 mm. ROOT_A/B and REV_MOD are reserved HwOnly pots.
Fields are rect unions (TIMING arch, ROOM T), margin 1.45. REC's lamp
joins the cluster, SONG's stands beside its knob. Keys draw as 6 mm caps.

Removed guards, each pinning a coordinate the raster replaces:
test_row3_ceiling_holds_at_seventy, test_level_band_is_evenly_divided,
test_level_band_clears_rooms_shoulder, test_level_band_holds_pan_in_slot_zero,
test_group_raster_closes, test_middle_band_runs_on_three_lines,
test_rows_are_centred_on_their_ink, and the row/figure pins in
test_drawing_geometry. New: test_nine_mm_between_caps (RED proven by
putting SHAPE on MOD's cell), test_cells_are_the_source,
test_fields_keep_box_gap, test_song_lamp_stands_beside_its_knob.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 3: The LED inventory — 15 lamps

**Files:**
- Modify: `host/vcv/res/gen_panel.py` (`HW_ONLY_LIGHTS`, ~line 1009)
- Modify: `host/vcv/res/gen_hw_panel.py` (`KNOB_LAMPS`, `LIGHT_POS`, `LED_GLOBALS`, `test`-visible docs)
- Modify: `host/vcv/src/led_law.hpp`
- Modify: `tests/test_led_law.cpp`
- Modify: `host/vcv/res/test_hw_panel.py`
- Regenerate: `host/vcv/src/generated_panel.hpp`, `host/vcv/src/generated_hw_panel.hpp`, `host/vcv/res/Fireflow.svg` (if the big generator rewrites it), `host/vcv/res/FireflowHW.svg`

**Interfaces:**
- Consumes: Task 2's `KNOB_LAMPS`, `SIDE_LAMPS`, `LIGHT_POS`, `JACK_POS`, `SAT_D`.
- Produces: LightIds `FTIME_A_L`, `FTIME_B_L`, `CLK_L`, `RST_L`; removed `SRC_*_L`, `FLT_*_L`, `CLR_*_L`, `SYNC_L`, `CEIL_L`. `HW_LIGHTS` has 15 entries.

- [ ] **Step 1: Write the failing guards**

Replace `test_led_inventory_after_the_feedback_round` with:

```python
def test_led_inventory_after_the_panel_pass():
    """15 lamps drawn (spec 2026-10-07 §5): the lane-excursion lamps and CEIL_L
    gone, FTIME and RST new, SYNC_L renamed CLK_L, SHIFT and MOD lamps centred
    between their key and jack."""
    names = {c.enum for c in hw.HW_LIGHTS}
    check(len(hw.HW_LIGHTS) == 15, f"{len(hw.HW_LIGHTS)} lights, expected 15")
    for dead in ("SRC_A_L", "SRC_B_L", "FLT_A_L", "FLT_B_L", "CLR_A_L", "CLR_B_L",
                 "CEIL_L", "SYNC_L", "CAP_A_L", "CAP_B_L"):
        check(dead not in names, f"{dead} is still drawn")
    for want in ("FTIME_A_L", "FTIME_B_L", "RST_L", "CLK_L", "LVL_A_L", "SONG_A_L",
                 "GATE_A_L", "REC_A_L", "TEMPO_L", "MODBTN_L", "SHIFTBTN_L"):
        check(want in names, f"{want} missing")
    lights = {c.enum for c in gp.LIGHTS + gp.HW_ONLY_LIGHTS}
    for dead in ("SRC_A_L", "FLT_A_L", "CLR_A_L", "CEIL_L", "SYNC_L"):
        check(dead not in lights, f"{dead} is still a LightId")
    check(hw.KNOB_LAMPS.get("FTIME_A_L") == "FLUXRATE_A", "FTIME_A_L is not FLUXRATE_A's cluster lamp")
```

Replace `test_satellite_lamps_clear_their_anchor_hole` with:

```python
def test_jack_row_lamps():
    """CLK_L and RST_L are satellites at SAT_D; SHIFTBTN_L and MODBTN_L stand
    centred between their key and jack (spec 2026-10-07 §5), every hole web
    at least MIN_WEB."""
    by = {c.enum: c for c in hw.ALL_HW}
    for lamp, anchor in (("CLK_L", "CLOCK"), ("RST_L", "RESET")):
        l, a = by[lamp], by[anchor]
        d = ((l.x - a.x) ** 2 + (l.y - a.y) ** 2) ** 0.5
        check(abs(d - hw.SAT_D) < 0.01, f"{lamp} is {d:.3f} mm from {anchor}, not SAT_D")
    check(by["CLK_L"].x < by["CLOCK"].x and by["RST_L"].x > by["RESET"].x,
          "CLK_L is not inboard of CLOCK or RST_L not outboard of RESET")
    for lamp, key, jack in (("SHIFTBTN_L", "SHIFTBTN", "IN_L"), ("MODBTN_L", "MODBTN", "OUT_R")):
        l, k, j = by[lamp], by[key], by[jack]
        check(abs(l.x - (k.x + j.x) / 2) < 1e-9 and abs(l.y - hw.JACK_Y) < 1e-9,
              f"{lamp} is not centred between {key} and {jack}")
        for anchor in (k, j):
            web = (abs(l.x - anchor.x) - hw.HOLE_D[hw.hw_class(anchor.enum)] / 2
                   - hw.HOLE_D["L"] / 2)
            check(web >= hw.MIN_WEB - 1e-9, f"{lamp} leaves {web:.2f} mm to {anchor.enum}")
    need = max(hw.HOLE_D["P"], hw.HOLE_D["J"]) / 2 + hw.MIN_WEB + hw.HOLE_D["L"] / 2
    check(hw.SAT_D >= need - 1e-9, f"SAT_D {hw.SAT_D} leaves less than {hw.MIN_WEB} mm")
```

In `test_light_accent_table`: `LED_GLOBALS == {"MODBTN_L", "SHIFTBTN_L"}` and the neutral loop over those two; docstring: MOD and SHFT lamps sit at the plate's edges.
In `test_hw_only_inventory`: LED total 15.
In `test_bodies_and_captions_sit_inside_their_frame`: loose list `["MODBTN", "MODBTN_L", "SHIFTBTN", "SHIFTBTN_L"]`.

Run: `ctest --test-dir build -R hw_panel_guard --output-on-failure` → FAIL (19 lights, SYNC_L present, …).

- [ ] **Step 2: The LightIds**

`gen_panel.py` `HW_ONLY_LIGHTS`:

```python
HW_ONLY_LIGHTS = [
    Ctl("LVL_A_L",     LIGHT, 0, 0, ""),   # deck delivers signal (spec 2026-10-07 §5);
    Ctl("LVL_B_L",     LIGHT, 0, 0, ""),   # the VCV law still shows LANE_LEVEL until P6b
    Ctl("SONG_A_L",    LIGHT, 0, 0, ""),   # which phrase snapshot is sounding
    Ctl("SONG_B_L",    LIGHT, 0, 0, ""),
    Ctl("FTIME_A_L",   LIGHT, 0, 0, ""),   # one flash per FLUX time period while MIX > 0
    Ctl("FTIME_B_L",   LIGHT, 0, 0, ""),
    Ctl("FLOW_A_L",    LIGHT, 0, 0, ""),   # drawn since the regroup, never lit
    Ctl("FLOW_B_L",    LIGHT, 0, 0, ""),
    Ctl("TEMPO_L",     LIGHT, 0, 0, ""),
    Ctl("CLK_L",       LIGHT, 0, 0, ""),   # a pulse at the CLOCK jack (was SYNC_L)
    Ctl("RST_L",       LIGHT, 0, 0, ""),   # a reset received
    Ctl("MODBTN_L",    LIGHT, 0, 0, ""),   # MOD latched, otherwise the limiter
    Ctl("SHIFTBTN_L",  LIGHT, 0, 0, ""),   # SHIFT latched, otherwise input level
]
```

- [ ] **Step 3: The plate's lamps**

`gen_hw_panel.py`: from `KNOB_LAMPS` delete the `SRC_*`, `FLT_*`, `CLR_*` entries and add `"FTIME_A_L": "FLUXRATE_A", "FTIME_B_L": "FLUXRATE_B"` (comment: spec §5, timing not modulation). `LED_GLOBALS = {"MODBTN_L", "SHIFTBTN_L"}` with its comment updated (CEIL_L folded into MODBTN_L). `LIGHT_POS` jack-row part becomes:

```python
LIGHT_POS = {
    # Satellites at SAT_D (Rev A P1): CLK_L inboard of CLOCK, RST_L outboard
    # of RESET -- the two mirror each other about the centre line.
    "CLK_L":      (JACK_POS["CLOCK"] - SAT_D, JACK_Y),
    "RST_L":      (JACK_POS["RESET"] + SAT_D, JACK_Y),
    # Spec 2026-10-07 §5: one lamp each, centred between key and jack, two
    # jobs each (SHIFT latched / input level; MOD latched / limiter). Both sit
    # in their jack's audio zone, admitted (spec §5.3).
    "SHIFTBTN_L": ((14.00 + JACK_POS["IN_L"]) / 2.0, JACK_Y),
    "MODBTN_L":   ((JACK_POS["OUT_R"] + (W - 14.00)) / 2.0, JACK_Y),
}
```

Update the `KNOB_LAMPS` / satellite comments and the `test_knob_lamps_sit_in_the_caption_cluster` docstring (`SYNC_L` → `CLK_L`, no `CEIL_L`). Check that `"SHIFTBTN_L"` is still the same `14.00` the `HW_ONLY` entry uses (write the 14.00 once as a constant `SHIFT_X = 14.00` and use it in both places, and `W - SHIFT_X` for MODBTN).

- [ ] **Step 4: The VCV LED law**

`host/vcv/src/led_law.hpp`:
- line ~148: `for (int id : {FLOW_A_L, FLOW_B_L, CLK_L, RST_L, FTIME_A_L, FTIME_B_L, SHIFTBTN_L})` and its comment: these lamps are placed and wired; what they show is P6b (spec 2026-10-07 §5).
- line ~165: `duty_out[MODBTN_L] = mod_latched ? (mod_pulse_on(p.blink) ? steps - 1 : 0) : duty(inst.limiter_squash(), steps);` with a comment: one lamp, two jobs — the latch while engaged, otherwise the limiter that `CEIL_L` showed (spec 2026-10-07 §5).
- `kExc`: keep only `{LVL_A_L, spky::LANE_LEVEL}, {LVL_B_L, spky::LANE_LEVEL}`, size 2, loop `i < 2`; comment that LVL's meaning moves to "deck delivers signal" in P6b.
- delete `duty_out[CEIL_L] = …`.

`tests/test_led_law.cpp`:
- line ~200: `CHECK(duty[spkyvcv::CEIL_L] == 0);` → `CHECK(duty[spkyvcv::MODBTN_L] == 0);` (unlatched and idle: the limiter is not squashing).
- lines ~202–220: `SRC_A_L` → `LVL_A_L` and the comments "SOURCE excursion" → "LEVEL excursion". **Run it before assuming it holds:** if `CHECK(hi > lo)` fails because the test's setup does not move `LANE_LEVEL`, read how that setup drives `LANE_SOURCE` and drive `LANE_LEVEL` the same way. Do not delete the check — a lamp test that cannot go red gets fixed.
- Add one case: MOD latched shows the double pulse even while the limiter would read high is out of reach without driving the limiter — instead assert the code path: with `mod_latched = true` the MODBTN_L duty over one second takes both `0` and `steps - 1` (the existing latched-pulse test at ~line 351 may already do this; extend it rather than duplicate).

- [ ] **Step 5: Regenerate, build, run**

Run: `python <scratchpad>/run_gen.py res/gen_panel.py res/gen_hw_panel.py`
Run: `cmake --build build && ctest --test-dir build -R "hw_panel_guard|panel_guard|spky_tests" --output-on-failure`
Expected: PASS. `spky_tests` compiles `led_law.hpp` against the regenerated `generated_panel.hpp`.

- [ ] **Step 6: No stale names**

Run: `git grep -n -E "\b(SYNC_L|CEIL_L|SRC_[AB]_L|FLT_[AB]_L|CLR_[AB]_L)\b" -- . ':!docs/superpowers' ':!docs/attic' ':!docs/milestone-history.md' ':!docs/roadmap.md'`
Expected: hits only in `hardware/reva/place_check.py`, `route_check.py`, `test_route.py` comments/keys (Task 7 owns them), `docs/hardware/grip-test.md` and `docs/hardware/io-budget.md` (Task 10). Anything else gets fixed now.

- [ ] **Step 7: Commit**

```bash
git add host/vcv/res/gen_panel.py host/vcv/res/gen_hw_panel.py host/vcv/res/test_hw_panel.py host/vcv/src/led_law.hpp tests/test_led_law.cpp host/vcv/src/generated_panel.hpp host/vcv/src/generated_hw_panel.hpp host/vcv/res/FireflowHW.svg
git commit -m "feat(hw-panel): 15 lamps -- timing, not modulation

TIMB/FILT/COLR excursion lamps and CEIL_L go; FTIME (FLUX time, gated on
MIX) and RST are new; SYNC_L becomes CLK_L; SHIFTBTN_L and MODBTN_L sit
centred between key and jack with two jobs each (spec 2026-10-07 §5).
The VCV law folds the limiter into MODBTN_L; the new lamps stay dark
until P6b.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

(Add `host/vcv/res/Fireflow.svg` if `gen_panel.py` rewrote it.)

---

### Task 4: Hole list, cut file, print sheet

**Files:**
- Regenerate: `host/vcv/res/FireflowHW-holes.json`, `FireflowHW-cut.svg`, `FireflowHW-print.svg`
- Modify: `host/vcv/res/test_hw_cut.py` (`test_hole_counts`)

- [ ] **Step 1: The failing count**

`test_hole_counts`: `want = {"pot": 73, "key": 4, "jack": 18, "led": 15, "sd": 1, "mount": 4}`; docstring: counted 2026-10-07 from the generator (spec 2026-10-07): 73 pot positions (ATTACK and STAGES share a knob; three reserved), 4 keys, 18 jacks, 15 LEDs.
Run: `ctest --test-dir build -R hw_cut_guard --output-on-failure` → FAIL (committed holes are the old plate).

- [ ] **Step 2: Regenerate and run**

Run: `python <scratchpad>/run_gen.py res/gen_hw_cut.py` → prints `holes=115 plate=304.4x128.5mm` (73 + 4 + 18 + 15 + 1 + 4).
Run: `ctest --test-dir build -R "hw_cut_guard|hw_panel_guard" --output-on-failure` → PASS.

- [ ] **Step 3: Commit**

```bash
git add host/vcv/res/test_hw_cut.py host/vcv/res/FireflowHW-holes.json host/vcv/res/FireflowHW-cut.svg host/vcv/res/FireflowHW-print.svg
git commit -m "feat(hw-panel): cut file, print sheet and hole list for the 9 mm raster

73 pots, 4 keys, 18 jacks, 15 LEDs (spec 2026-10-07). The correction cut
Bastian orders from Formulor is this FireflowHW-cut.svg.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 5: The relaxed mux split

**Files:**
- Modify: `hardware/reva/assign.py` (`_split_regions`, module docstring)
- Modify: `hardware/reva/test_assign.py`
- Regenerate: `hardware/reva/panel-map.json`

**Interfaces:**
- Produces: `assign._split_regions(pots) -> (regions: dict[sense, list[pot]], cal_sense: str)` — unchanged signature; bands are contiguous in `(x_mm, y_mm, id)` order.

- [ ] **Step 1: Write the failing tests**

In `test_assign.py`:

1. Replace the x-overlap check in `invariants()` with an order check:

```python
    order = sorted(m["pots"], key=lambda p: (p["x_mm"], p["y_mm"], p["id"]))
    rank = {p["id"]: i for i, p in enumerate(order)}
    bands = [[rank[p["id"]] for p in m["pots"] if p["sense"] == s] for s in A.SENSE_ORDER]
    for left, right in zip(bands, bands[1:]):
        if left and right and max(left) >= min(right):
            bad.append("bands overlap in (x, y, id) order")
```

2. Add a split check and synthetic cases:

```python
def split_columns(m):
    """Columns whose pots sit in more than one band."""
    cols = {}
    for p in m["pots"]:
        cols.setdefault(p["x_mm"], set()).add(p["sense"])
    return sorted(x for x, s in cols.items() if len(s) > 1)


def synthetic(columns):
    """Pots in columns of the given sizes, 20.2 mm apart, rows 20.125 apart."""
    return [{"id": "P%d_%d" % (c, r), "x_mm": 10.0 + 20.2 * c, "y_mm": 14.5 + 20.125 * r}
            for c, n in enumerate(columns) for r in range(n)]


def split_cases():
    bad = []
    # 72 pots in 18 columns of 4 fit 24/16/16/24 (+2 cal) without a split.
    regions, _ = A._split_regions(synthetic([4] * 18))
    xs = [{p["x_mm"] for p in regions[s]} for s in A.SENSE_ORDER]
    if any(a & b for i, a in enumerate(xs) for b in xs[i + 1:]):
        bad.append("a split was taken where a split-free answer exists")
    # The plate's own column pattern (deck 5 5 5 5 5 4, centre 5 5 5, deck
    # 4 5 5 5 5 5): whole columns cannot fill the bands, one split can.
    regions, _ = A._split_regions(synthetic([5, 5, 5, 5, 5, 4, 5, 5, 5, 4, 5, 5, 5, 5, 5]))
    xs = [{p["x_mm"] for p in regions[s]} for s in A.SENSE_ORDER]
    shared = sum(len(a & b) for i, a in enumerate(xs) for b in xs[i + 1:])
    if shared != 1:
        bad.append("expected exactly one split column, got %d" % shared)
    return bad
```

   Measured while writing this plan: today's `_split_regions` raises `ValueError` on the plate pattern (the RED) and fits `[4] * 18`; the relaxed rule takes exactly one split on the first and none on the second.

3. In `main()`: `failures += split_cases()`; after `m = A.assign(holes)`: `if len(split_columns(m)) > 1: failures.append("more than one column split: %s" % split_columns(m))`; rename the `"81 pots"` sabotage label to `"84 pots"` (73 + 11).
4. Update the module docstring's "x-bands" wording.

Run: `python hardware/reva/test_assign.py` → FAIL: `ValueError: 73 pots do not fit 80 channels in four contiguous bands` (the new hole list) or the synthetic case.

- [ ] **Step 2: Implement**

Replace `_split_regions` in `assign.py`:

```python
def _split_regions(pots):
    """Four contiguous bands, one per sense pin in SENSE_ORDER, in
    (x, y, id) order.

    A cut may fall inside a column (spec 2026-10-07 §8): a deck carries 29
    pots and its first five columns hold 25 against a 24-channel band, so
    whole columns no longer fit. A split is taken only when no split-free
    answer exists: every split whose bands fit their capacity -- both
    calibration channels going to the band with the most room -- is scored
    (columns split, fullest band's fill ratio, cuts); the lowest wins.
    Exhaustive: 73 pots give C(72, 3) = 59 640 splits, well under a second.
    """
    order = sorted(pots, key=lambda p: (p["x_mm"], p["y_mm"], p["id"]))
    caps = [CHANNELS * len(MUXES[s]) for s in SENSE_ORDER]
    best = None
    for cuts in itertools.combinations(range(1, len(order)), len(caps) - 1):
        bounds = (0,) + cuts + (len(order),)
        sizes = [bounds[i + 1] - bounds[i] for i in range(len(caps))]
        free = [c - s for c, s in zip(caps, sizes)]
        cal = max(range(len(caps)), key=lambda i: (free[i], -i))
        used = [s + (len(CALIBRATION) if i == cal else 0) for i, s in enumerate(sizes)]
        if any(u > c for u, c in zip(used, caps)):
            continue
        splits = sum(order[c - 1]["x_mm"] == order[c]["x_mm"] for c in cuts)
        score = (splits, max(u / c for u, c in zip(used, caps)), cuts)
        if best is None or score < best[0]:
            best = (score, bounds, cal)
    if best is None:
        raise ValueError("%d pots do not fit %d channels in four contiguous bands"
                         % (len(pots), sum(caps)))
    _, bounds, cal = best
    regions = {s: order[bounds[i]:bounds[i + 1]] for i, s in enumerate(SENSE_ORDER)}
    return regions, SENSE_ORDER[cal]
```

- [ ] **Step 3: Regenerate and run**

Run: `python hardware/reva/assign.py` then `python hardware/reva/test_assign.py`
Expected: `ok: 73 pots on 10 muxes, 2 calibration, 5 spare, 15 LEDs`. Print `split_columns()` of the new map: it must be `[213.0]` (deck B column 5, spec §8). If it is anything else, stop and report.

- [ ] **Step 4: Commit**

```bash
git add hardware/reva/assign.py hardware/reva/test_assign.py hardware/reva/panel-map.json
git commit -m "feat(reva): mux bands may split one column -- 73 pots + 2 cal on 80 channels

Whole columns no longer fit: a deck's c1..c5 hold 25 pots against a
24-channel band once ROOT is reserved (spec 2026-10-07 §8). Cuts may now
fall inside a column, scored by columns split first; the new plate needs
one, at x 213.0 (MELODY_B joins SENSE_3).

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 6: The schematic — 15 LED nets

**Files:**
- Modify: `hardware/reva/blocks.py` (`SR_OUTPUTS`, ~line 70)
- Modify (only if it pins 19 LEDs or 70 pots): `hardware/reva/test_build.py`, `hardware/reva/erc-waivers.txt`

- [ ] **Step 1: Run the guards to see them red**

Run: `cmake --build build && ctest --test-dir build -R "reva_build_guard|reva_check_guard" --output-on-failure`
Expected: FAIL — the panel map now carries 15 LEDs and 73 pots; the chain still names `LED15..LED18`.

- [ ] **Step 2: Implement**

```python
SR_OUTPUTS = (      # chip order from SR_DATA; outputs QA..QH
    tuple(sr(n) for n in MUX_S) + tuple(sr(n) for n in MUX_EN[0:5]),
    tuple(sr(n) for n in MUX_EN[5:10]) + (led_net(0), led_net(1), led_net(2)),
    tuple(led_net(i) for i in range(3, 11)),
    # 15 lamps since the 9 mm panel pass (spec 2026-10-07 §5): LED11..LED14,
    # then four more spare outputs.
    tuple(led_net(i) for i in range(11, 15)) + tuple("SR_SPARE%d" % i for i in range(8, 12)),
    tuple("SR_SPARE%d" % i for i in range(8)),
)
```

Find how `SR_SPARE0..7` are terminated in the build (search `SR_SPARE` in `hardware/reva/*.py` and `hardware/gen/*.py`); the four new ones must be handled the same way (no-connect or test point). If ERC waivers list spare nets by name, add `SR_SPARE8..11` with the same justification.

- [ ] **Step 3: Run**

Run: `ctest --test-dir build -R "reva_assign_guard|reva_build_guard|reva_check_guard|hw_gen_bom_guard" --output-on-failure`
Expected: PASS. If `test_build.py` pins counts, update them with a comment citing the spec.

- [ ] **Step 4: Commit**

```bash
git add hardware/reva/blocks.py
git commit -m "feat(reva): 15 LED nets on the 595 chain, four more spare outputs

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

(Add `test_build.py` / `erc-waivers.txt` / regenerated schematic files the build writes, if any — check `git status`.)

---

### Task 7: The board — place, route, export

This task is procedural: the board is generated, and its guards decide. Budget: `route.py` takes up to an hour (`reva_route_guard` timeout 3600). Read `docs/superpowers/specs/2026-09-29-rev-a-p4-1-placement-design.md` §5, `2026-09-30-rev-a-p4-2-routing-design.md` §4 and `2026-10-02-rev-a-p4-3-fabrication-design.md` before starting, and each script's docstring for its flags.

**Files:**
- Modify: `hardware/reva/place_check.py` (`KNOWN_PANEL`), `hardware/reva/test_place.py`
- Modify: `hardware/reva/route_check.py` (`KNOWN_PANEL`), `hardware/reva/test_route.py` (`PAIRS`, `ZONE_PAIRS`, `SONG`, `CUT_OFF_KEYS`, `JACK_ZONES`)
- Modify only if a check demands it: `hardware/reva/place.py` (`OVERRIDES` — back-side parts only, never a pot, jack, key or LED)
- Regenerate: `hardware/reva/kicad/reva.kicad_pcb`, `docs/hardware/placement/*`, the P4-3 export outputs (follow `fab.py --help` and the P4-3 spec for the release paths)

- [ ] **Step 1: Placement report**

Run: `"$KIPY" hardware/reva/place.py` (no `--write`), `KIPY` as in Global Constraints.
Read the report. Expected, against today's `KNOWN_PANEL`: the `"edge"` set (jack row) **resolved** because `JACK_Y` = 112.75; the SONG body/pad/rotation items **resolved** (SONG's lamp beside the knob, spec §5.1 predicts 3.04 mm pad to pad); `rotation GATE_A_L` / `rotation LVL_B_L` **resolved** (SOURCE_A and PAN_B no longer neighbour those lamps). Any finding that is not in today's `KNOWN_PANEL` is new: if it is a back-side part colliding with the moved pots, fix it with an `OVERRIDES` entry in `place.py` in the style of the existing ones (reason string, date); if it names a panel part (pot, jack, key, LED) — **stop and report**, do not move panel parts.

If the SONG lamp shows a pad clearance different from 3.04 mm, or any finding at all, stop and report (spec §11).

- [ ] **Step 2: Empty `place_check.KNOWN_PANEL` and its guard**

`place_check.py`: `KNOWN_PANEL = {"edge": set(), "front": set(), "drc": set()}` (same keys as today, all empty) with the comment: emptied by the 9 mm panel pass (spec 2026-10-07 §1); a non-empty entry needs Bastian's decision.
`test_place.py`: replace the allowed-names block with:

```python
    n_keys = sum(len(v) for v in PC.KNOWN_PANEL.values())
    check(n_keys == 0, "KNOWN_PANEL is empty after the panel pass (spec 2026-10-07 §1): %r"
          % {k: sorted(v) for k, v in PC.KNOWN_PANEL.items() if v})
```

and update its docstring item 4. Remove `PAIRS`/SONG helpers that become unused (search the file).

Run: `"$KIPY" hardware/reva/place.py --write` then `cmake --build build && ctest --test-dir build -R "reva_place_guard|hw_gen_place_guard" --output-on-failure` → PASS.

- [ ] **Step 3: Route**

Run: `"$KIPY" hardware/reva/route.py` (no `--write` first; it can take tens of minutes — run it in the background and wait for the notification).
Read `route_check`'s report. Expected: the `"routed"` SONG cut-offs, the jack-row `copper_edge_clearance` entries, the SONG `shorting_items` and the GATE_A_L/SOURCE_A, LVL_B_L/PAN_B pairs **gone**; the audio step reporting jack zones only for `IN_L/SHIFTBTN_L` and `MODBTN_L/OUT_R` (spec §5.3). If the router does not converge, or any other finding appears: **stop and report** with the report's lines (spec §10.4).

- [ ] **Step 4: `route_check.KNOWN_PANEL` and its guard**

`route_check.py`: `"routed": set()`, `"drc": set()`, `"silk": set()`, and

```python
    "audio": {
        # jack zones admitted by Bastian (2026-09-30, renewed 2026-10-07 for the
        # combined lamps, spec 2026-10-07 §5.3): the lamps centred between key
        # and jack sit within 10 mm of the jack's tip pad by construction.
        "IN_L/SHIFTBTN_L",
        "MODBTN_L/OUT_R",
    },
```

with the aggressor-pad comments rewritten from the new report. `test_route.py`: `PAIRS = ()` (or remove its uses), `ZONE_PAIRS = ({"IN_L", "SHIFTBTN_L"}, {"MODBTN_L", "OUT_R"})`, `SONG = set()`, `CUT_OFF_KEYS = set()`, and `JACK_ZONES` with exactly the victim pad and aggressor pads the new report lists for those two keys. Update the comment "Empties with KNOWN_PANEL["audio"] at the panel pass" to say what happened instead (two pairs stay, admitted).

Run: `"$KIPY" hardware/reva/route.py --write`, then `ctest --test-dir build -R "reva_route_guard|reva_check_kit_guard|hw_gen_route_guard|hw_gen_stitch_guard" --output-on-failure` → PASS.

- [ ] **Step 5: Export**

Run the P4-3 export as its spec and `fab.py`'s docstring say (Gerbers, drill, CPL with the rotation table, BOM, assembly sheets). The CPL rotations were verified in JLC's preview on 2026-10-03 (`e1c22439`) for the old board: the rotation table is per part type, so it carries over, but **note in the commit message** that the preview check must be repeated before ordering. Then `ctest --test-dir build -R reva_fab_guard --output-on-failure` → PASS.

- [ ] **Step 6: Look at it**

Open the renders under `docs/hardware/placement/` (and the P4-3 render paths) and compare with the plate: the SONG lamps beside SONG, the LVL/GATE/FTIME/TEMPO/REC lamps under their controls, the three reserved pots present, the jack row on its new line.

- [ ] **Step 7: Commit**

```bash
git add hardware/reva docs/hardware/placement
git status   # nothing of Bastian's pre-existing changes may be staged
git commit -m "feat(reva): the board follows the 9 mm plate -- KNOWN_PANEL empty but two admitted audio zones

Placed, routed and exported from the new hole list: 73 pots, 15 LEDs,
jack row at 112.75 (spec 2026-10-07). Every P4-1 panel violation is gone;
route_check keeps only the admitted jack zones IN_L/SHIFTBTN_L and
MODBTN_L/OUT_R (spec §5.3). Repeat the JLC placement-preview rotation
check before ordering.

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 8: The firmware table — reserved pots send nothing

**Files:**
- Modify: `shell/gen_panel_map.py`, `shell/test_gen_panel_map.py`
- Regenerate: `shell/generated_panel_map.h`
- Modify: `tests/test_controls_map.cpp`, `tests/test_mux_plan.cpp`

**Interfaces:**
- Consumes: `hardware/reva/panel-map.json` (Task 5), `blocks.SR_OUTPUTS` (Task 6), `generated_hw_panel.hpp` (Tasks 2–3).
- Produces: `generated_panel_map.h` with 73 pot rows; reserved rows carry `-1` and a reason starting `reserved:`.

- [ ] **Step 1: Write the failing tests**

`shell/test_gen_panel_map.py`:
- `check("73 pot rows", len(ROWS) == 73)`; keep `35 rows send a parameter`.
- add `check("3 reserved rows send nothing", sum("reserved:" in r and "-1" in r for r in ROWS) == 3)` — adapt to how `ROWS` represents a row (read the file's top: `ROWS` is parsed from the generated header; match the reason text the generator writes).
- the `"LED0..LED18 not contiguous"` sabotage expectation → `"LED0..LED14 not contiguous"`.
- add a sabotage: a reserved id that is not a pot — `lambda i: i["reserved"].__setitem__("NOPE", "x")` → `"not a pot: NOPE"` (after Step 2 the inputs dict carries `"reserved"`).

`tests/test_controls_map.cpp` line ~86: `CHECK(shell::kRevaTable.count == 73);`.
`tests/test_mux_plan.cpp` `kRevaShape`: the LED count `19` → `15` and its comment "nineteen LEDs" → "fifteen LEDs". Read `kPanelChain`'s test (`led_bits == 19`, ~line 137) first: if `kPanelChain` is generated from the Rev A map, change it to 15; if it is the coupon's hand-written profile, leave it.

Run: `python shell/test_gen_panel_map.py` → FAIL.

- [ ] **Step 2: Implement**

In `gen_panel_map.py`, after `UNMAPPED`:

```python
# Spec 2026-10-07 §4: pots on the board with no parameter yet. They are read
# like every pot and send nothing until the follow-up spec gives them one.
RESERVED = {
    "ROOT_A": "per-deck scale root, follow-up to spec 2026-10-07 §4",
    "ROOT_B": "per-deck scale root, follow-up to spec 2026-10-07 §4",
    "REV_MOD": "reverb tail wobble (WOBL), follow-up to spec 2026-10-07 §4",
}
```

Thread it through `load_inputs()` (key `"reserved"`) and `build(...)` (new parameter `reserved`): the classification checks treat `safe`, `unmapped`, `reserved` as three disjoint sets that together cover every pot; the `generated_hw_panel.hpp` lookup skips reserved ids (they are `HwOnly`, not in `kParamCtls`); the row is `(p, "-1", "reserved: " + reserved[p["id"]])`. Update the docstring ("SAFE, UNMAPPED and RESERVED below are the only hand-written part").

- [ ] **Step 3: Regenerate and run everything**

Run: `python shell/gen_panel_map.py`, then `python shell/test_gen_panel_map.py`, then
`source env.sh && cmake --build build && ctest --test-dir build --output-on-failure`
Expected: the **whole** ctest suite PASSES in Release. Copy the summary line (`100% tests passed, 0 tests failed out of N`) into the task report — the controller re-runs it; a claimed green is not evidence.

- [ ] **Step 4: Commit**

```bash
git add shell/gen_panel_map.py shell/test_gen_panel_map.py shell/generated_panel_map.h tests/test_controls_map.cpp tests/test_mux_plan.cpp
git commit -m "feat(shell): Rev A table for the 9 mm plate -- 73 pots, three reserved rows send nothing

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```

---

### Task 9: VCV — build, install, look

**Files:** none changed unless the look finds something.

- [ ] **Step 1: Build and install**

Run: `host/vcv/build-local.sh install` (never a hand-rolled build). Expected: build succeeds, plugin installed. Tell Bastian to restart Rack.

- [ ] **Step 2: Headless render**

Render `FireflowHW` headless as in the memory note `fireflow-rack-screenshots-headless` (`Rack.exe -u <throwaway-dir> -t 2`, a throwaway user dir with a patch that holds one `FireflowHW`). Read the PNG: every knob widget on its cell; no caption, lamp or legend under a Rack widget (Review Focus 1); the reserved pots show as plate holes with their caption and no widget; the round keys sit on their printed caps; the big module's ENGINE latch still looks right (`kFfPadR` changed to 3.0 — render one `Fireflow` too).

- [ ] **Step 3: Report**

If anything is covered or misplaced, fix it in the generator (Task 2's files), regenerate, re-run `hw_panel_guard`, re-install, re-render, and commit with the finding in the message. Otherwise report "rendered, nothing covered" with the PNG path.

---

### Task 10: Docs

**Files:**
- Modify: `docs/hardware/grip-test.md`, `docs/roadmap.md`, `docs/hardware/io-budget.md`

- [ ] **Step 1: Grip test record**

`grip-test.md` checklist: item 1 and 2 — failed, small caps 5.3 mm apart edge to edge (measured from the generator, spec 2026-10-07 §2), fix: spec `2026-10-07-panel-nine-mm-raster-design.md`, plan `2026-10-07-panel-nine-mm-raster.md`; item 3 — passed ("patching was fine", Bastian 2026-10-07). Measurements table: `SYNC_L` → `CLK_L`, `CEIL_L` → the combined `SHIFTBTN_L` / `MODBTN_L` lamps (9.50 mm from their jack). Add a line: the correction cut is `FireflowHW-cut.svg` as of the Task 4 commit (give its hash).

- [ ] **Step 2: Roadmap**

M6 entry: the grip test failed on spacing; the 9 mm panel pass (spec + plan, commits) re-placed every control on a 20.2 × 20.125 mm raster (smallest cap gap 10.27 mm), reworked the lamps to 15 (timing, not modulation), reserved ROOT_A/B and WOBL, raised the jack row, and the board, its export and the firmware table followed; next: Bastian orders the correction cut, reruns grip-test 1–2 and 4, then the freeze tag. Under "Planned", add: **Reserved knobs become parameters** — split `P_ROOT` into `ROOT_A`/`ROOT_B` (the VCV host never sets ROOT today), give WOBL (`P_REV_MOD`) a knob booting at 0.15 and update `docs/by-ear-decisions.md`'s reverb entry; needs its own spec (both panels, factory init).

- [ ] **Step 3: io-budget**

`docs/hardware/io-budget.md` (~line 132) says `SYNC_L` and the two key lamps are held dark. The file is written in German: correct the facts in the file's own language (15 lamps; `CLK_L` instead of `SYNC_L`; `SHIFTBTN_L` and `MODBTN_L` carry two jobs each, spec 2026-10-07 §5) and leave the rest of the file as it is.

- [ ] **Step 4: Commit**

```bash
git add docs/hardware/grip-test.md docs/roadmap.md docs/hardware/io-budget.md
git commit -m "docs: grip test result and the 9 mm panel pass in the roadmap

Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>"
```
