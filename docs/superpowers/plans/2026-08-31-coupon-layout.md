# Coupon Layout Generator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generate `hardware/coupon/coupon.kicad_pcb` — placement, zones and
routing as reviewable data, built and proven by script, ending in Gerbers in
`fab/`.

**Architecture:** Three data/generator files following the schematic side's
pattern (`design.py` → `netlist.py` → `build.py`): `placement.py` holds every
part's position as data, `routing.py` holds every signal track as data,
`build_pcb.py` builds the board through KiCad's `pcbnew` Python API and runs
the proof chain (net comparison, ratsnest 0, DRC, analog-rule measurement,
render, Gerber export). A shared helper module `kipcb.py` wraps the pcbnew
calls.

**Tech Stack:** KiCad 10.0.5 bundled Python
(`C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\python.exe`, module
`pcbnew`), `kicad-cli` for DRC/render/Gerber, existing
`hardware/coupon/scripts/` modules (`design.py`, `netlist.py`, `ksexp.py`).

**Spec:** `docs/superpowers/specs/2026-08-31-coupon-layout-design.md`

## Global Constraints

- Everything written into the repo is English; the conversation is German.
- Never prefix a shell command with `cd`; run scripts with absolute paths.
  No shell writes (`>`/`>>`/`tee`); scripts write files, the shell does not.
- Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- Generator scripts run under KiCad's Python, invoked as
  `"C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\python.exe" <script>`
  (the system `python` has no `pcbnew`). `kicad-cli` is
  `C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\kicad-cli.exe`
  (already resolved by `ksexp.KICAD_CLI`).
- Board data from `design.py` (do not restate numbers elsewhere):
  `BOARD_W_MM = 80.0`, `BOARD_H_MM = 60.0`, `LAYERS = 4`.
- Design-rule floor (spec §4): signal 0.25 mm, supply 0.5 mm, ±12 V 0.8 mm,
  via 0.3 mm drill / 0.6 mm outer, clearance 0.2 mm.
- KiCad's PCB coordinate system: y grows DOWNWARD. All placement/routing data
  is in millimetres from the board's top-left corner at (0, 0); helpers
  convert with `pcbnew.FromMM` / `pcbnew.ToMM`.
- Every proof step exits non-zero on failure; `build_pcb.py` aborts on the
  first red step. There is no separate test runner (spec §5).
- Every checker must be proven able to go red ONCE (repo testing
  discipline). Sabotage for RED proofs happens on a board copy in the
  scratchpad or via an in-memory perturbation flag — NEVER by editing a repo
  file and reverting it with `git checkout` (CRLF trap).
- A task's implementer verifies pcbnew API claims with a probe before
  building on them (the probe rule applies to API behaviour too: a method
  name from memory is a claim, not a measurement).

## File Structure

- Create: `hardware/coupon/scripts/kipcb.py` — pcbnew wrapper: board
  creation, footprint resolution/loading, net table, placement application,
  zone creation and fill, track/via drawing, save. No coupon knowledge.
- Create: `hardware/coupon/scripts/placement.py` — pure data: every
  reference → (x, y, rotation, side) plus the zone-membership table.
- Create: `hardware/coupon/scripts/routing.py` — pure data + tiny helpers:
  explicit tracks and vias for every signal net.
- Create: `hardware/coupon/scripts/check_layout.py` — the five analog rules
  of spec §4, measured from the saved board file.
- Create: `hardware/coupon/scripts/build_pcb.py` — orchestrator + proof
  chain; writes `proof/` and `fab/` outputs.
- Create: `hardware/coupon/README.md` — layer/zone map (fixes `design.py`'s
  dangling "see README" comment).
- Modify: `hardware/coupon/scripts/review.py` — grows a layout section.
- Modify: `docs/roadmap.md` — status entry when done.

---

### Task 1: `kipcb.py` — pcbnew wrapper, proven by probe

**Files:**
- Create: `hardware/coupon/scripts/kipcb.py`
- Create: `hardware/coupon/scripts/probe_kipcb.py` (temporary probe, deleted
  in this same task after it has printed its facts)

**Interfaces:**
- Produces (later tasks rely on these exact names):
  - `kipcb.KIPY` — absolute path to KiCad's python.exe (string constant).
  - `kipcb.new_board(width_mm, height_mm, copper_layers) -> pcbnew.BOARD` —
    fresh board with rectangular Edge.Cuts outline at (0,0)-(w,h), copper
    layer count set, design-rule minimums from the Global Constraints set on
    `board.GetDesignSettings()`.
  - `kipcb.footprint(lib_id) -> pcbnew.FOOTPRINT` — loads
    `"LibName:FootprintName"` from KiCad's share footprints dir or the
    vendored `hardware/lib/DaisyKiCad/*.pretty`, mirroring
    `netlist.load()`'s two-tier lookup.
  - `kipcb.add_part(board, part, x_mm, y_mm, rot_deg, side) -> FOOTPRINT` —
    loads `part.footprint`, sets reference/value from the `netlist.Part`,
    positions it, flips if `side == "B"`, assigns every pad its net from
    `part.nets` (creating `NETINFO_ITEM`s on demand), appends to board.
    Parts with empty footprint (`J_SM1/J_SM2`, `#FLG*`) are skipped and
    returned as `None`.
  - `kipcb.add_zone(board, layer_name, net_name, points_mm) -> ZONE`.
  - `kipcb.fill_zones(board)` — `pcbnew.ZONE_FILLER` over all zones.
  - `kipcb.add_track(board, layer_name, width_mm, net_name, points_mm)`.
  - `kipcb.add_via(board, at_mm, net_name)` — 0.3/0.6 through via.
  - `kipcb.save(board, path)` / `kipcb.load(path)`.
  - `kipcb.board_nets(board) -> {net_name: [(ref, pad_number), ...]}` — the
    read-back used by the net comparison (pads with no net excluded).

- [ ] **Step 1: Write the probe first** — `probe_kipcb.py`, runnable under
  KiCad's Python. It must PRINT, not assume:

```python
#!/usr/bin/env python3
"""Throwaway probe: prints the pcbnew facts kipcb.py is allowed to rely on."""
import pcbnew
print("version:", pcbnew.GetBuildVersion())
b = pcbnew.BOARD()
print("default copper layers:", b.GetCopperLayerCount())
b.SetCopperLayerCount(4)
print("layer name In1:", b.GetLayerName(pcbnew.In1_Cu))
fp = pcbnew.FootprintLoad(
    r"C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\share\kicad\footprints\Resistor_SMD.pretty",
    "R_0805_2012Metric_Pad1.20x1.40mm_HandSolder")
print("loaded R_0805:", fp is not None, "pads:", len(fp.Pads()))
daisy = pcbnew.FootprintLoad(
    r"C:\Users\bernd\Documents\AI\FireFlow\hardware\lib\DaisyKiCad\Daisy-Boards.pretty",
    "DAISY_PATCH_SM")
print("loaded DAISY_PATCH_SM:", daisy is not None, "pads:", len(daisy.Pads()))
names = sorted(p.GetNumber() for p in daisy.Pads())
print("first pads:", names[:6], "...", names[-4:])
xs = [p.GetPosition() for p in daisy.Pads() if p.GetNumber() in ("A1", "A10", "B1", "C1", "D1", "D10")]
for num in ("A1", "A10", "B1", "C1", "D1", "D10"):
    for p in daisy.Pads():
        if p.GetNumber() == num:
            print("pad", num, "at mm", pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y))
            break
```

- [ ] **Step 2: Run the probe, keep its output**

Run: `"C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\python.exe" C:\Users\bernd\Documents\AI\FireFlow\hardware\coupon\scripts\probe_kipcb.py`
Expected: version 10.0.5, both footprints load, DAISY_PATCH_SM pad count and
the mm positions of banks A/B vs C/D print. **The A/B-vs-C/D side answer
decides the SM's orientation in Task 2 — record it in the Task 2 data
comment.** If any API call in the probe fails, the fix happens HERE, not in
kipcb.py later.

- [ ] **Step 3: Write `kipcb.py`** implementing the interface above. Core
  (adapt to what Step 2 printed — this code is the intent, the probe output
  is the authority on names):

```python
#!/usr/bin/env python3
"""pcbnew wrapper for the coupon layout generator. No coupon knowledge here.

Runs ONLY under KiCad's own Python (KIPY below); the system python has no
pcbnew. Coordinates in the public API are millimetres, y grows downward,
origin at the board's top-left corner.
"""
import os
import pcbnew

KICAD_ROOT = os.environ.get(
    "KICAD_ROOT", r"C:\Users\bernd\AppData\Local\Programs\KiCad\10.0")
KIPY = os.path.join(KICAD_ROOT, "bin", "python.exe")
FP_SHARE = os.path.join(KICAD_ROOT, "share", "kicad", "footprints")
FP_VENDORED = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib", "DaisyKiCad"))

LAYER = {"F.Cu": pcbnew.F_Cu, "In1.Cu": pcbnew.In1_Cu,
         "In2.Cu": pcbnew.In2_Cu, "B.Cu": pcbnew.B_Cu}


def _pt(x_mm, y_mm):
    return pcbnew.VECTOR2I(pcbnew.FromMM(x_mm), pcbnew.FromMM(y_mm))


def new_board(width_mm, height_mm, copper_layers):
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(copper_layers)
    bds = board.GetDesignSettings()
    bds.m_TrackMinWidth = pcbnew.FromMM(0.25)
    bds.m_ViasMinSize = pcbnew.FromMM(0.6)
    bds.m_MinThroughDrill = pcbnew.FromMM(0.3)
    bds.m_MinClearance = pcbnew.FromMM(0.2)
    corners = [(0, 0), (width_mm, 0), (width_mm, height_mm), (0, height_mm)]
    for a, b in zip(corners, corners[1:] + corners[:1]):
        seg = pcbnew.PCB_SHAPE(board)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        seg.SetStart(_pt(*a)); seg.SetEnd(_pt(*b))
        seg.SetLayer(pcbnew.Edge_Cuts)
        seg.SetWidth(pcbnew.FromMM(0.1))
        board.Add(seg)
    return board


def footprint(lib_id):
    lib, _, name = lib_id.partition(":")
    for base in (FP_SHARE, FP_VENDORED):
        path = os.path.join(base, lib + ".pretty")
        if os.path.isdir(path):
            fp = pcbnew.FootprintLoad(path, name)
            if fp is not None:
                return fp
    raise FileNotFoundError(lib_id)


def _net(board, name, cache={}):
    key = (id(board), name)
    if key not in cache:
        info = pcbnew.NETINFO_ITEM(board, name)
        board.Add(info)
        cache[key] = info
    return cache[key]


def add_part(board, part, x_mm, y_mm, rot_deg, side="F"):
    if not part.footprint:
        return None
    fp = footprint(part.footprint)
    fp.SetReference(part.ref)
    fp.SetValue(part.value)
    fp.SetPosition(_pt(x_mm, y_mm))
    if side == "B":
        fp.Flip(_pt(x_mm, y_mm), False)
    fp.SetOrientationDegrees(rot_deg)
    for pad in fp.Pads():
        net = part.nets.get(str(pad.GetNumber()))
        if net is not None:
            pad.SetNet(_net(board, net))
    board.Add(fp)
    return fp
```

  plus `add_zone` (ZONE with `SetLayer`, `SetNet`, outline via
  `zone.Outline().AddOutline` of a `SHAPE_LINE_CHAIN`), `fill_zones`
  (`pcbnew.ZONE_FILLER(board).Fill(board.Zones())`), `add_track`
  (a `pcbnew.PCB_TRACK` per consecutive point pair with layer/width/net),
  `add_via` (`pcbnew.PCB_VIA`, `SetDrill(FromMM(0.3))`,
  `SetWidth(FromMM(0.6))`), `save` (`pcbnew.SaveBoard(path, board)`),
  `load` (`pcbnew.LoadBoard(path)`), and `board_nets` (walk
  `board.GetFootprints()` → pads → `pad.GetNetname()`).

- [ ] **Step 4: Round-trip check** — extend `probe_kipcb.py`: build a board
  with `new_board(80, 60, 4)`, add one `netlist.build()` part
  (`R_BTN`), save to the scratchpad, `load()` it back, assert
  `board_nets()` shows `{"+3V3": [("R_BTN","1")], "BTN_1": [("R_BTN","2")]}`
  and print `OK`.

Run: same interpreter, expect `OK`.
Expected failure mode proven once: mistype the pad number in the assertion,
see it fail, fix back.

- [ ] **Step 5: Delete the probe, commit**

```bash
git add hardware/coupon/scripts/kipcb.py
git rm --cached --ignore-unmatch hardware/coupon/scripts/probe_kipcb.py
git commit -m "hw(coupon): kipcb.py, the pcbnew facts behind it probed first"
```

(Delete `probe_kipcb.py` from the working tree too; record its printed pad
positions for banks A/B vs C/D in the commit message body — Task 2 needs
them.)

---

### Task 2: `placement.py` + board skeleton — every part placed, nets proven

**Files:**
- Create: `hardware/coupon/scripts/placement.py`
- Create: `hardware/coupon/scripts/build_pcb.py` (first half: steps 0–2 of
  the proof chain)

**Interfaces:**
- Consumes: `kipcb` (Task 1), `netlist.build()`, `design.BOARD_W_MM/H_MM/LAYERS`.
- Produces:
  - `placement.PLACE` — `{ref: (x_mm, y_mm, rot_deg)}` for every part with a
    footprint; side is always `"F"` (single-sided assembly, hand soldering).
  - `placement.DOMAIN` — `{ref: "analog" | "digital" | "seam"}` for every
    placed part; `"seam"` is exactly the two jumpers `JP_GND`/`JP_3V3` and
    the SM itself.
  - `placement.ZONE_RECTS` — the four zone outlines as
    `{("In1.Cu","AGND"): [...4 points...], ("In1.Cu","GND"): [...],
    ("In2.Cu","A+3V3"): [...], ("In2.Cu","+3V3"): [...]}` (values from the
    authored geometry; the analog rects sit under the analog domain, gap
    ≥ 1 mm between zone pairs).
  - `build_pcb.build() -> pcbnew.BOARD` — board with outline + parts + nets
    (zones/tracks come in Tasks 3–4).

**Authoring guidance (this is the layout act itself):** SM top-centre with
its bank side chosen from Task 1's probe so the supply pins (A1–A10) face the
board's right edge; IDC header top-right; audio jack on the left edge, pin
row inside; muxes left-of-centre below the SM with their COM pins facing
their sense pins on the SM; pots in two groups around the muxes; 595/165/LED
row bottom-right; every 100n adjacent to its VCC pin (spec §4 rule 5 is a
placement fact before it is a check). Author coordinates, then iterate
against the Step 3 check until green — the check is the arbiter, not the
first guess.

- [ ] **Step 1: Write the failing check first** — `build_pcb.py` with proof
  steps 0–2 only:

```python
#!/usr/bin/env python3
"""Build hardware/coupon/coupon.kicad_pcb and prove it. Abort on first red."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D
import netlist as N
import placement as P
import kipcb

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.normpath(os.path.join(HERE, "..", "coupon.kicad_pcb"))


def fail(msg):
    print("RED:", msg)
    sys.exit(1)


def build():
    parts = N.build()
    board = kipcb.new_board(D.BOARD_W_MM, D.BOARD_H_MM, D.LAYERS)
    placed = 0
    for part in parts:
        if not part.footprint:
            continue
        if part.ref not in P.PLACE:
            fail("no placement for %s" % part.ref)
        x, y, rot = P.PLACE[part.ref]
        kipcb.add_part(board, part, x, y, rot)
        placed += 1
    print("1. placed %d parts" % placed)
    return board, parts


def check_nets(board, parts):
    want = {n: sorted(v) for n, v in
            N.nets_from(parts, include_virtual=False).items()}
    got = {n: sorted(v) for n, v in kipcb.board_nets(board).items()}
    if want != got:
        for n in sorted(set(want) | set(got)):
            if want.get(n) != got.get(n):
                print("  net %-20s want %s got %s" % (n, want.get(n), got.get(n)))
        fail("board nets do not match the intent")
    print("2. %d nets match the intent node for node" % len(want))


if __name__ == "__main__":
    board, parts = build()
    check_nets(board, parts)
    kipcb.save(board, PCB)
    print("wrote", PCB)
```

- [ ] **Step 2: Run it, watch it fail** — `placement.py` does not exist yet.

Run: `"C:\Users\bernd\AppData\Local\Programs\KiCad\10.0\bin\python.exe" C:\Users\bernd\Documents\AI\FireFlow\hardware\coupon\scripts\build_pcb.py`
Expected: ImportError on `placement` (that is the RED).

- [ ] **Step 3: Author `placement.py`** — module docstring naming the probe
  facts it is built on (SM bank sides, from Task 1's commit), then the three
  tables. Structure:

```python
#!/usr/bin/env python3
"""Every coupon part's position, as data. Millimetres, y grows downward,
origin top-left. Authored against the zone plan of the 2026-08-31 layout
spec; the checks in build_pcb.py and check_layout.py are the arbiter.

SM bank orientation per the Task-1 probe: <RECORD PROBE FACT HERE>.
"""

SM = (40.0, 20.0, 0)          # top-centre anchor; banks per docstring

PLACE = {
    "U_SM": SM,
    "J_PWR": (74.0, 6.0, 90),
    "J_AUDIO": (4.0, 44.0, 90),
    "U_MUX16": (24.0, 44.0, 0),
    "U_MUX8": (38.0, 44.0, 0),
    # ... every remaining ref; authored, then iterated until the
    # Task-2 net check and Task-6 DRC are green ...
}

DOMAIN = {
    "U_SM": "seam", "JP_GND": "seam", "JP_3V3": "seam",
    "U_MUX16": "analog", "U_MUX8": "analog",
    # ... every placed ref exactly once ...
}

ZONE_RECTS = {
    ("In1.Cu", "AGND"):  [(1, 32), (46, 32), (46, 59), (1, 59)],
    ("In1.Cu", "GND"):   [(1, 1), (79, 1), (79, 59), (47.5, 59), (47.5, 33.5), (1, 33.5)],
    ("In2.Cu", "A+3V3"): [(1, 32), (46, 32), (46, 59), (1, 59)],
    ("In2.Cu", "+3V3"):  [(1, 1), (79, 1), (79, 59), (47.5, 59), (47.5, 33.5), (1, 33.5)],
}
```

  (The rect values above are the starting shape: analog quadrant bottom-left,
  ≥1 mm gap; final values come out of the iterate-until-green loop. A
  completeness guard belongs at module bottom:
  `assert set(PLACE) == set(DOMAIN)`.)

- [ ] **Step 4: Iterate until green.** Add a placement for every part
  `netlist.build()` emits with a footprint (85 parts minus SM sockets and
  flags). Run `build_pcb.py` after each round.

Run: as Step 2.
Expected: `1. placed ...`, `2. ... nets match the intent node for node`,
`wrote ...coupon.kicad_pcb`.

- [ ] **Step 5: Courtyard-overlap check.** Append to `build_pcb.py` after
  `check_nets` a step 3 that runs DRC restricted to placement sanity via
  `kicad-cli pcb drc` (see Task 6 for the full call) and greps the report
  for `courtyards_overlap`; any hit is red. Iterate placement until zero.

Run: as Step 2. Expected: `3. 0 courtyard overlaps`.

- [ ] **Step 6: Commit**

```bash
git add hardware/coupon/scripts/placement.py hardware/coupon/scripts/build_pcb.py hardware/coupon/coupon.kicad_pcb
git commit -m "hw(coupon): every part has a place, and the board's nets are proven"
```

---

### Task 3: Zones — split planes that actually connect

**Files:**
- Modify: `hardware/coupon/scripts/build_pcb.py`

**Interfaces:**
- Consumes: `placement.ZONE_RECTS`, `kipcb.add_zone`, `kipcb.fill_zones`.
- Produces: board with four filled zones; proof step 4 "plane nets connected".

- [ ] **Step 1: Write the failing check first.** Append to `build_pcb.py`:
  after zone fill, for each of the four plane nets (`GND`, `AGND`, `+3V3`,
  `A+3V3`), every pad on that net must be connected: read
  `board.GetConnectivity()` and count unconnected items per net
  (`GetUnconnectedCount(True)` scoped by walking the ratsnest edges; probe
  the exact API first, record it in a comment). Since no vias exist yet,
  this MUST be red when first run.

Run: `build_pcb.py`. Expected: `RED: plane net GND has N unconnected pads`
(the RED proof for this checker).

- [ ] **Step 2: Add the zones and the stitching vias.** In `build()`: create
  the four zones from `ZONE_RECTS`; then for every pad on a plane net that
  is a through-hole pad, the hole itself reaches In1/In2 — SMD pads on plane
  nets instead get one via from `kipcb.add_via` dropped 1.0 mm from the pad
  centre, connected with a 0.5 mm F.Cu track (0.25 mm for AGND sense-side
  decouplers is unnecessary — supply width everywhere, spec §4). Generate
  these programmatically from the board's pads (`pad.GetNetname()` in the
  four plane nets and `pad.GetAttribute() == pcbnew.PAD_ATTRIB_SMD`), not as
  hand data — via-per-SMD-plane-pad is mechanical, not a design decision.
  Then `kipcb.fill_zones(board)`.

- [ ] **Step 3: Run until green.** Zone rect adjustments in
  `placement.ZONE_RECTS` are allowed (that is why they are data).

Run: `build_pcb.py`.
Expected: `4. plane nets fully connected (GND/AGND/+3V3/A+3V3)`.

- [ ] **Step 4: Commit**

```bash
git add hardware/coupon/scripts/build_pcb.py hardware/coupon/scripts/placement.py hardware/coupon/coupon.kicad_pcb
git commit -m "hw(coupon): four split planes, joined only where the jumpers say"
```

---

### Task 4: `routing.py` — every signal net as data, ratsnest to zero

**Files:**
- Create: `hardware/coupon/scripts/routing.py`
- Modify: `hardware/coupon/scripts/build_pcb.py`

**Interfaces:**
- Consumes: `kipcb.add_track`, `kipcb.add_via`; pad positions read from the
  built board (`build_pcb` passes the board to a `routing.apply(board, kipcb)`
  entry point so track endpoints can be expressed as `("pad", ref, number)`
  and resolved to real coordinates — routing data never hard-codes a pad's
  xy).
- Produces:
  - `routing.TRACKS` — list of
    `(net_name, layer, width_mm, [endpoint, ...])` where an endpoint is
    `("pad", ref, pad_number)` or `(x_mm, y_mm)`.
  - `routing.VIAS` — list of `(net_name, (x_mm, y_mm))` for signal-layer
    changes (B.Cu overflow).
  - `routing.apply(board, kipcb_module)` — resolves endpoints, draws all
    tracks/vias.

- [ ] **Step 1: Write the failing check first.** Append proof step 5 to
  `build_pcb.py`: total ratsnest (unconnected pairs, all nets) must be 0 —
  `board.GetConnectivity().GetUnconnectedCount(True)` after a
  `board.BuildConnectivity()` refresh (verify with a one-line probe, keep
  the verified call in a comment). Runs after `routing.apply`.

Run: `build_pcb.py` with an empty `TRACKS`/`VIAS` in a fresh `routing.py`.
Expected: `RED: 40-ish unconnected pairs` — the RED proof, and the honest
count of what routing owes.

- [ ] **Step 2: Author the routing data,** iterating against the red count.
  Order of authoring (each round runs `build_pcb.py` and watches the number
  fall): ±12 V power tracks (0.8 mm, IDC → SM pins → bulk caps); COM
  clusters (0.25 mm, F.Cu, mux COM pin → TP → DNP pad → 0R → SM sense pin —
  the §4-rule-1 order); audio (jack ← SM B1/B2 along the left edge);
  pot wipers and neighbour/divider/spare ties to their mux channel pins;
  address bus + enables (SM/595 side to both muxes, crossing the seam next
  to `JP_GND`); SR chain (`SR_CLK` as one run SM → 595 → 595 → 165, then
  data/latch); LED nets + `BTN_1`. B.Cu with a via pair only where F.Cu is
  blocked; every via is a hand-placed data point in `VIAS`.

Run: `build_pcb.py` after each round.
Expected: `5. ratsnest 0 -- every net routed`.

- [ ] **Step 3: Commit**

```bash
git add hardware/coupon/scripts/routing.py hardware/coupon/scripts/build_pcb.py hardware/coupon/coupon.kicad_pcb
git commit -m "hw(coupon): the routing is data, and the ratsnest is zero"
```

---

### Task 5: `check_layout.py` — the five analog rules, measured

**Files:**
- Create: `hardware/coupon/scripts/check_layout.py`
- Modify: `hardware/coupon/scripts/build_pcb.py` (proof step 6 calls it)

**Interfaces:**
- Consumes: the saved `coupon.kicad_pcb` (loads it fresh via `kipcb.load` —
  it checks the artifact, not the in-memory board), `placement.DOMAIN`,
  `design` net names.
- Produces: `check_layout.run(pcb_path) -> list[str]` — empty list = green,
  strings are violations; `__main__` prints them and exits 1 if any.
  Also `check_layout.measurements(pcb_path) -> dict` with the numbers for
  `review.py`: `{"com16_mm": float, "com8_mm": float, "audio_clearance_mm":
  float, "worst_decoupler_mm": float}`.

The five rules (spec §4), each a measurement over the loaded board:
1. Per COM net: sum of track lengths ≤ 15.0 mm, via count == 0, every
   segment on F.Cu.
2. Min distance between any `AUDIO_*` track segment and any segment of
   `SR_CLK`/`LED_*` nets ≥ 10.0 mm (segment-to-segment distance over
   endpoints and closest approach).
3. Every part is inside its `DOMAIN` zone rect (analog parts within the
   AGND rect's bounds, digital within GND's; seam parts exempt).
4. `SR_CLK`: its track graph from the SM pad is a single path (every node
   degree ≤ 2 — no stubs).
5. Every 100n decoupler within 2.0 mm of its chip's VCC pad
   (pad-centre distance); bulk caps within 8.0 mm of the SM supply pads /
   IDC pads they serve.

- [ ] **Step 1: Write the checker** with the interface above (pure reading:
  `board.GetTracks()`, filter `PCB_VIA` vs `PCB_TRACK` via
  `track.Type()`, lengths from `track.GetLength()`).

- [ ] **Step 2: RED proof on a perturbed copy, in the scratchpad.** Add a
  `--sabotage <rule>` flag to `check_layout.py` used ONLY here: it copies
  the board to the scratchpad, moves `C_M16` 10 mm away (rule 5) before
  checking. Run it, expect rule 5 to fire; then run without the flag,
  expect green. The flag stays in the file — it documents that the checker
  can go red (repo discipline: prove the RED once), and it never touches
  the repo copy.

Run: `check_layout.py <pcb> --sabotage decoupling` → exit 1, names `C_M16`.
Run: `check_layout.py <pcb>` → exit 0.

- [ ] **Step 3: Wire into `build_pcb.py`** as proof step 6; iterate
  placement/routing if any rule is red on the real board (the checker is
  the arbiter; the data files move until it is green).

Run: `build_pcb.py`. Expected: `6. analog rules hold (com16=..mm com8=..mm audio>=..mm)`.

- [ ] **Step 4: Commit**

```bash
git add hardware/coupon/scripts/check_layout.py hardware/coupon/scripts/build_pcb.py hardware/coupon/scripts/placement.py hardware/coupon/scripts/routing.py hardware/coupon/coupon.kicad_pcb
git commit -m "hw(coupon): the five analog rules are measured, not asserted"
```

---

### Task 6: Full proof chain — DRC, render, Gerbers, review sheet

**Files:**
- Modify: `hardware/coupon/scripts/build_pcb.py`
- Modify: `hardware/coupon/scripts/review.py`
- Create: `hardware/coupon/README.md`
- Modify: `docs/roadmap.md`

**Interfaces:**
- Consumes: everything above; `ksexp.KICAD_CLI`.
- Produces: `proof/drc.rpt`, `proof/coupon-board-front.png`,
  `proof/coupon-board-back.png`, `fab/gerbers/` + `fab/coupon.drl`,
  review.md §6 "Layout", and the README the `design.py` comment points to.

- [ ] **Step 1: DRC as proof step 7.** Subprocess call mirroring the ERC one
  in `build.py`:

```python
rc, _ = run([cli, "pcb", "drc", "--exit-code-violations",
             "--severity-error", "--severity-warning",
             "-o", os.path.join(PROOF, "drc.rpt"), PCB], "kicad-cli pcb drc")
```

  Expected clean, since zones are filled and saved. Any violation that is
  explained-and-accepted (the schematic side has three such) gets the same
  treatment: documented in review.md with its reason, never silenced.

- [ ] **Step 2: Render + Gerbers as proof step 8.**

```python
run([cli, "pcb", "render", "--side", "top",
     "-o", os.path.join(PROOF, "coupon-board-front.png"), PCB], "render front")
run([cli, "pcb", "render", "--side", "bottom",
     "-o", os.path.join(PROOF, "coupon-board-back.png"), PCB], "render back")
run([cli, "pcb", "export", "gerbers",
     "-o", os.path.join(FAB, "gerbers"), PCB], "gerbers")
run([cli, "pcb", "export", "drill",
     "-o", os.path.join(FAB, "gerbers"), PCB], "drill")
```

- [ ] **Step 3: Extend `review.py`** with a "## 6. Layout" section: the
  placement table (ref, x, y, rot, domain — read from `placement`), and the
  `check_layout.measurements()` numbers with their limits beside them.

Run: `review.py`, read the diff of `proof/review.md`: new section, numbers
present, limits stated.

- [ ] **Step 4: Write `hardware/coupon/README.md`** — one page: what the
  coupon is (one paragraph, linking the envelope spec), the build command
  chain (`build.py` then `build_pcb.py`, both under which interpreter), the
  layer/zone map the `design.py` comment promises, and the order rule
  (populate copies, never rework; the two hands-on blockers before
  ordering).

- [ ] **Step 5: Full chain from zero.** Delete `coupon.kicad_pcb`, run
  `build.py`, then `build_pcb.py`, confirm every proof step prints green and
  the PCB file is byte-stable across two consecutive `build_pcb.py` runs
  (determinism check — if UUIDs churn, seed or strip them; a generated file
  that diffs on every run poisons review).

Run: `git diff --stat hardware/coupon/coupon.kicad_pcb` after the second
run. Expected: no diff.

- [ ] **Step 6: Roadmap entry** — new dated paragraph in `docs/roadmap.md`
  (M6 section) + "Last updated" header rotation, same style as 2026-08-30/31:
  what exists, what was proven, what still blocks the order (bus board,
  land-pattern risk).

- [ ] **Step 7: Commit**

```bash
git add hardware/coupon hardware/coupon/README.md docs/roadmap.md
git commit -m "hw(coupon): the board exists, proven eight ways, gerbers ready"
```

---

## Self-review notes (kept, per plan discipline)

- Spec coverage: §2 stackup → Tasks 2–3; §3 zone plan → Tasks 2–3; §4 rules
  → Task 5 (+ rule floor in Task 1 `new_board`); §5 architecture → Tasks
  1/2/4; §6 proof chain steps 1–6 → Tasks 2 (net compare), 4 (ratsnest),
  6 (DRC), 5 (analog rules), 6 (render/review), 6 (gerbers). §7 out of
  scope: no ordering/firmware tasks exist. Covered.
- The pcbnew API calls in Task 1 are marked as intent with a mandatory probe
  (Step 1–2) before use; where a later task depends on an unverified call
  (connectivity counts), the task says "probe the exact API first".
- Type consistency: `placement.PLACE`/`DOMAIN`/`ZONE_RECTS`,
  `routing.TRACKS`/`VIAS`/`apply`, `check_layout.run`/`measurements`,
  `kipcb.*` names are used identically across tasks.
