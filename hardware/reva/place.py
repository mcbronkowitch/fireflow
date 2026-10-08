#!/usr/bin/env python3
"""Rev A placement (P4-1 spec, docs/superpowers/specs/2026-09-29-rev-a-p4-1-placement-design.md).

    KIPY hardware/reva/place.py [--write] [--sabotage NAME] [--out DIR] [--where REF]

Builds the placed, unrouted board from P3's project (build.project()), the P1
hole list and panel-map.json, saves <out>/reva-placed.kicad_pcb (default
hardware/reva/out/), renders it and runs place_check. Exit 0 only when every
gated check is green (known panel violations listed, spec §5.3). --write also,
only when the run is GREEN, copies the renders to docs/hardware/placement/.
The committed hardware/reva/kicad/reva.kicad_pcb is the routed board since
P4-2: route.py owns it (P4-2 spec §4.1), place.py never writes it.
"""
import argparse
import copy
import math
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
import rules as RU     # noqa: E402
from gen import kipcb # noqa: E402
from gen import place as PL  # noqa: E402
from gen import sch_writer as W  # noqa: E402

OUT = os.path.join(HERE, "out")
DOCS = os.path.normpath(os.path.join(HW, "..", "docs", "hardware", "placement"))
X0, Y0, X1, Y1 = 2.0, 9.25, 302.8, 119.25   # spec §2.1 / §4.1
EDGE_CLEAR = RU.EDGE_CLEAR   # copper to edge (assumption, spec §8)
EDGE_INSET = 1.0          # SMD courtyards stay this far inside the outline
PAD_CLEAR = RU.CLEARANCE  # the coupon's board minimum
DECOUPLE_MAX_MM = 2.0     # the coupon's check_layout rule 5
USB_CLEAR_MM = 35.0       # spec §4.3 amendment
LAYERS = 4
PLANES = (("In1.Cu", RU.PLANE_NETS[0]), ("In2.Cu", RU.PLANE_NETS[1]))
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
        self.blocked = []
        self.no_rotation = set()
        self.overrides_used = []
        self.anchor_from = {}
        self.sheet_of = {}
        self.known = {}
        self.unconnected = []

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
    """The LED's pads miss every foreign body and keep PAD_CLEAR to every
    foreign pad, and its own body box overlaps no foreign body (P4.1 §4.2,
    amended 2026-10-08). The body test is what turns a lamp beside its owner
    so its flat side faces the owner: LED_D3.0mm's body box is not centred
    on its hole (x -1.55 .. +1.95 at rotation 0), and the pads-only pick put
    deck B's mirrored REC lamp on its key."""
    if any(PL.overlaps(PL.body_box(fp), o) for o in bodies):
        return False
    for _n, b in PL.pad_boxes(fp):
        if any(PL.overlaps(b, o) for o in bodies):
            return False
        if any(PL.gap(b, o) < PAD_CLEAR for o in pads):
            return False
    return True


def place_panel(s, proj):
    """Every panel part on its hole (spec §4.2); LEDs last, each at the first
    rotation whose pads miss every foreign body and keep PAD_CLEAR and whose
    body overlaps no foreign body (_led_fits)."""
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


SM_SHADOW_MM = (68.17, 40.18)   # spec §4.3: x -34.09..34.08, y -20.09..20.09 (the coupon's SM_SHADOW)
SM_SHADOW_TOL = 0.1
SM_ROTS = (0, 90, 180, 270)     # spec §4.3, amended 2026-10-08 (was 0 / 180)
_TEXT_CLASSES = ("PCB_TEXT", "PCB_TEXTBOX", "PCB_FIELD")


def module_shadow(fp):
    """The module's shadow: the box of the silkscreen SHAPES on the part's own
    side, text excluded (probed 2026-09-29: the footprint's "INSTALL ON THIS
    SIDE" PCB_TEXT alone reaches 9 mm past the module outline). Raises
    ValueError naming the part when the box is not SM_SHADOW_MM, upright
    (rotation 0/180) or turned (90/270, w and h swapped; spec §4.3 amendment
    2026-10-08)."""
    layer = pcbnew.B_SilkS if fp.IsFlipped() else pcbnew.F_SilkS
    boxes = [PL.box(g.GetBoundingBox()) for g in fp.GraphicalItems()
             if g.GetLayer() == layer and g.GetClass() not in _TEXT_CLASSES]
    if not boxes:
        raise ValueError("%s: no silkscreen shapes for the module shadow" % fp.GetReference())
    b = (min(b[0] for b in boxes), min(b[1] for b in boxes),
         max(b[2] for b in boxes), max(b[3] for b in boxes))
    w, h = b[2] - b[0], b[3] - b[1]
    if not shadow_shape_ok(w, h, SM_SHADOW_MM, SM_SHADOW_TOL):
        raise ValueError("%s: shadow is %.2f x %.2f mm, spec §4.3 says %.2f x %.2f (or turned)"
                         % (fp.GetReference(), w, h, SM_SHADOW_MM[0], SM_SHADOW_MM[1]))
    return b


def shadow_shape_ok(w, h, want, tol):
    """True when a w x h box is the module shadow `want`, upright or turned
    by 90 degrees."""
    return any(abs(w - a) <= tol and abs(h - b) <= tol
               for a, b in (want, (want[1], want[0])))


def _tht_clear_of_front(fp, bodies, pads):
    """A back THT part's pins come through to the front: no pin in a front
    body, PAD_CLEAR to every front pad."""
    for _n, b in PL.pad_boxes(fp):
        if any(PL.overlaps(b, o) for o in bodies):
            return False
        if any(PL.gap(b, o) < PAD_CLEAR for o in pads):
            return False
    return True


def _front_courtyards(board, refs):
    """[(courtyard polygon, its box)] of the front parts in `refs` on the board."""
    out = []
    for ref in refs:
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            continue
        cy = fp.GetCourtyard(pcbnew.F_CrtYd)
        if cy.OutlineCount():
            out.append((cy, PL.box(cy.BBox())))
    return out


def _holes_clear_of_courtyards(fp, courtyards):
    """No through hole of `fp` overlaps a front courtyard (P4.1 §4.3, amended
    2026-10-08): KiCad's gated pth_inside_courtyard. The hole's bounding box
    is intersected with the courtyard polygon. Calibrated on the 9 mm panel
    board: none at U_SM (152.40, 69.75) rot 90, where the DRC found none;
    exactly B1/REV_DIFF and C5/REV_MOD at (152.40, 57.75) rot 270, the two
    items the DRC reported."""
    for p in fp.Pads():
        if p.GetAttribute() not in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
            continue
        hb = p.GetEffectiveHoleShape().BBox()
        hbox = PL.box(hb)
        for cy, cbox in courtyards:
            if not PL.overlaps(hbox, cbox):
                continue
            hole = pcbnew.SHAPE_POLY_SET()
            hole.NewOutline()
            for x, y in ((hb.GetLeft(), hb.GetTop()), (hb.GetRight(), hb.GetTop()),
                         (hb.GetRight(), hb.GetBottom()), (hb.GetLeft(), hb.GetBottom())):
                hole.Append(x, y)
            x2 = pcbnew.SHAPE_POLY_SET(cy)
            x2.BooleanIntersection(hole)
            if x2.Area() > 0:
                return False
    return True


def back_tht_refs(s):
    return [r for r in ("U_SM", "J_PWR") if s.board.FindFootprintByReference(r) is not None]


def _tht_blocked(s):
    """Every through-hole pad on the board, grown by PAD_CLEAR: SMD parts on
    the back must keep off the pin tails."""
    out = []
    for fp in s.board.GetFootprints():
        for p in fp.Pads():
            if p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                out.append(PL.grow(PL.box(p.GetBoundingBox()), PAD_CLEAR))
    return out


# The module's pinned spot (spec §4.3, amended 2026-10-08, Task 7c; Bastian):
# (x, y, rotation, reason), or None for the spiral search. place_module runs
# its own legality tests on the pin and raises when the spot is illegal.
SM_PIN = (152.40, 56.00, 270,
          "U_SM sweep 2026-10-08 (Task 7c report, Resumes 3 and 4): the spiral's spot "
          "(152.40, 69.75) rot 90 left SD_CMD and SENSE_3 (pins D7/D8 boxed in by "
          "RV34's netless tab, 0.63 mm) and SR_CLK/SR_DATA (pins B7/B8 against the "
          "audio pair marks) unroutable. Legal window by the body/pad tests, x 0.1 / "
          "y 0.25 mm grid: rot 90 y 51.50..53.75 and 69.75..72.00, rot 270 y "
          "54.00..58.00 and 74.25..74.75 (x 152.0..152.8); with the courtyard test "
          "rot 270 keeps y 54.00..56.50 and 74.25..74.75. Courtyard-clean spots where "
          "probe routes (every net, pair rules on) take every module net out: only "
          "rot 270 y 54.50..56.50. Here place_check is GREEN and 30 router rounds "
          "left no net unrouted (13 conflicts). (152.40, 57.75) rot 270, approved "
          "first, was refused by the gated DRC (pth_inside_courtyard B1/REV_DIFF, "
          "C5/REV_MOD). Reserve: 0.4 mm in x to the legal window, 0.50 mm in y to "
          "the courtyard limit (y 56.50)")


def _module_legal(fp, inner, bodies, pads, courtyards):
    """place_module's legality tests for one footprint position: the shadow
    inside the inset outline, every pin clear of the front bodies and pads,
    and every hole clear of the front courtyards."""
    return (PL.inside(module_shadow(fp), inner) and _tht_clear_of_front(fp, bodies, pads)
            and _holes_clear_of_courtyards(fp, courtyards))


def place_module(s, proj):
    """U_SM on the back. With SM_PIN set: exactly at that spot, which must
    pass the same tests as the search (ValueError otherwise). Without it:
    any of SM_ROTS, the spiral spot nearest the board centre whose pins miss
    every front body, keep PAD_CLEAR to every front pad and whose holes
    miss every front courtyard, and whose shadow (its silkscreen box) stays
    EDGE_INSET inside the outline (spec §4.3). Rotations 90/270 were
    admitted 2026-10-08 (§4.3 amendment): the 9 mm raster leaves no 0/180
    spot, and the USB clearance rule is rotation-agnostic. The courtyard
    test and the pin came the same day (§4.3, second amendment)."""
    part = {p.ref: p for p in proj.parts()}["U_SM"]
    cx, cy = (X0 + X1) / 2.0, (Y0 + Y1) / 2.0
    fp = kipcb.add_part(s.board, part, cx, cy, 0, side="B")
    bodies, pads = _front_obstacles(s.board, s.front, None)
    courtyards = _front_courtyards(s.board, s.front)
    inner = PL.grow(outline_box(), -EDGE_INSET)
    if SM_PIN is not None:
        x, y, rot, reason = SM_PIN
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(kipcb._pt(x, y))
        if not _module_legal(fp, inner, bodies, pads, courtyards):
            raise ValueError("U_SM's pinned spot (%.2f, %.2f) rot %d is not legal: its shadow leaves the "
                             "inset outline, a pin meets a front body or pad, or a hole a front courtyard"
                             % (x, y, rot))
        s.parts["U_SM"] = part
        s.shadow = module_shadow(fp)
        s.anchors["U_SM"] = (x, y)
        s.overrides_used.append("U_SM pinned at (%.2f, %.2f) rot %d: %s" % (x, y, rot, reason))
        return
    best = None
    for rot in SM_ROTS:
        for x, y in PL.spiral(cx, cy, 0.5, 60.0):
            fp.SetOrientationDegrees(rot)
            fp.SetPosition(kipcb._pt(x, y))
            if _module_legal(fp, inner, bodies, pads, courtyards):
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
    s.shadow = module_shadow(fp)
    s.anchors["U_SM"] = (x, y)


def jpwr_key_vector(fp):
    """Direction of the header's key: from pad 2 to pad 1, measured on the
    placed footprint's real pad centres (so the mirror of a back-side part is
    included, not reasoned about)."""
    pb = dict(PL.pad_boxes(fp))
    c1 = ((pb["1"][0] + pb["1"][2]) / 2.0, (pb["1"][1] + pb["1"][3]) / 2.0)
    c2 = ((pb["2"][0] + pb["2"][2]) / 2.0, (pb["2"][1] + pb["2"][3]) / 2.0)
    return c1[0] - c2[0], c1[1] - c2[1]


def _jpwr_rotation(fp, target_y):
    """The rotation whose key points at the nearer long edge of the board:
    -y (top edge) when the target is above mid height, +y (bottom edge) when
    below, and +y on an exact tie (spec §4.3: "Rotation: the key faces the
    nearer long edge"; pin 1, -12 V, sits on the key side and its side is
    recorded for P4-3's silkscreen). Probed 2026-09-29 on
    Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical at rot 0, front: pad 1 at
    (0,0), pad 2 at (2.54,0), the F.Fab key notch on the odd-pin column's
    outer side, so the key direction is the vector from pad 2 to pad 1."""
    mid = (Y0 + Y1) / 2.0
    want = -1.0 if target_y < mid - 1e-9 else 1.0
    found_axis = False
    for rot in (0, 90, 180, 270):
        fp.SetOrientationDegrees(rot)
        vx, vy = jpwr_key_vector(fp)
        n = math.hypot(vx, vy)
        if abs(vx) / n > math.sin(math.radians(1.0)):
            continue                      # not within 1 degree of the y axis
        found_axis = True
        if vy * want > 0:
            return rot
    if not found_axis:
        raise ValueError("J_PWR: no rotation gives a key vector within 1 degree of +-y")
    raise ValueError("J_PWR: no rotation points the key at the %s edge"
                     % ("top" if want < 0 else "bottom"))


def place_power_header(s, proj, blocked):
    """J_PWR on the back in the board half without OUT_L/OUT_R, at mid height;
    key toward the nearer long edge (_jpwr_rotation); pins clear of the front,
    courtyard >= USB_CLEAR_MM from the module shadow (spec §4.3)."""
    part = {p.ref: p for p in proj.parts()}["J_PWR"]
    by_id = _hole_index()
    out_x = (by_id["OUT_L"]["x_mm"] + by_id["OUT_R"]["x_mm"]) / 2.0
    mid_x = (X0 + X1) / 2.0
    tx = (X0 + mid_x) / 2.0 if out_x > mid_x else (mid_x + X1) / 2.0
    target = (tx, (Y0 + Y1) / 2.0)
    fp = kipcb.add_part(s.board, part, target[0], target[1], 0, side="B")
    rot = _jpwr_rotation(fp, target[1])
    bodies, pads = _front_obstacles(s.board, s.front, None)

    def ok(f):
        return (_tht_clear_of_front(f, bodies, pads)
                and PL.gap(PL.courtyard_box(f), s.shadow) >= USB_CLEAR_MM)

    x, y, _rot = PL.first_fit(fp, target, blocked, PL.grow(outline_box(), -EDGE_INSET),
                              0.5, 60.0, rotations=(rot,), accept=ok)
    s.parts["J_PWR"] = part
    s.anchors["J_PWR"] = (x, y)           # the placed position, spec §4.4


# Manual corrections (spec §4.4): ref -> (dx, dy, rot, reason), relative to
# the part's anchor. An entry names the render that justified it.
# (The first Task 5 run needed five, all for a decoupler with no room at its
# IC's VCC pad; the IC search now leaves that room itself, but only against
# the parts placed before it, so a later IC can still take it: U_SR2 below.)
# P4-2 Task 6a (2026-09-30) added U_SR1/2/3/5, U_IN1 and R1/R2/R3 because the
# spiral packed the shift registers against the module's edges, where the
# router never converged. The 9 mm panel pass (2026-10-08, Task 7c) moved the
# pots and the module (152.40, 69.75 rot 90): each of those targets then lay
# on a pot's THT pads or another part's courtyard, and their reasons described
# a board that is gone, so they were removed and the §4.4 spiral places those
# parts again. R12's target stayed free and is kept.
OVERRIDES = {
    "U_SR2": (-6.00, 2.50, 90,
              "place.py 2026-10-08 (Task 7c, 9 mm panel; probe_sr2 in the 7c report): with the "
              "2026-09-30 IC overrides removed, U_SR2's first fit (116.06, 60.76) abutted "
              "U_IN1's VCC side and C22 found no spot within 2.0 mm of U_IN1 pad 16; this is "
              "the first spiral spot from U_SR2's anchor that leaves C22 its decoupler spot"),
    "C6": (8.50, -7.00, 270,
           "route.py 2026-10-08 (Task 7c, 9 mm panel; probe_c6 in the 7c report): at its first "
           "fit (rot 90) C6.2's GND stitch via found no spot outside RV18's (DETUNE_A) pot "
           "keep-out and was kept under the pot body (route_check pot_keepout); the same spot "
           "turned to rot 270 puts pad 2 on the other end, where the stitch resolves"),
    "R12": (35.11, -8.74, 0,
            "reva-routed-bottom.png 2026-09-30 (Task 6a run 1): MUX_EN8 the worst net of the knot "
            "east of U_SR1; moved out of that corridor next to U_MUX8, the mux it enables"),
}

# Search step and radius per class (spec §4.4; spike values for ICs,
# decoupling and LED resistors).
STEP = {"ic": (0.5, 30.0), "power": (0.5, 40.0), "decouple": (0.1, 3.0),
        "led_r": (0.25, 10.0), "other": (0.5, 30.0)}
ROTS = (0, 90, 180, 270)
_ROUND_MM = 1e-5          # pcbnew rounds positions to 1 nm; the pure search keeps this margin


def pad_index(board):
    """{net: [(ref, pad number, (x, y))]} over every pad on the board."""
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


def _net_refs(proj):
    out = {}
    for p in proj.parts():
        for net in p.nets.values():
            out.setdefault(net, set()).add(p.ref)
    return out


def _panel_side_nets(part, net_refs):
    """Spec §4.4: the pads a 74HC595 drives, the 74HC165's key pads -- the
    IC's signal nets that reach neither the module nor another IC (clock,
    latch, data and chain nets all do)."""
    out = set()
    for net in set(part.nets.values()) - SUPPLY:
        if not any(r.startswith("U_") for r in net_refs.get(net, set()) - {part.ref}):
            out.add(net)
    return out


def _through_two_pin(part, nets, idx, unplaced):
    """The placed pads on `nets`, and on the far net of each unplaced 2-pin
    part hanging on one of them (a supply far side is not followed)."""
    pts = []
    for net in sorted(nets):
        pts += [xy for ref, _n, xy in idx.get(net, []) if ref != part.ref]
        for q in unplaced:
            if len(q.nets) == 2 and net in q.nets.values():
                other = [n for n in q.nets.values() if n != net][0]
                if other not in SUPPLY:
                    pts += [xy for ref, _n, xy in idx.get(other, []) if ref != part.ref]
    return pts


def _ic_anchor(s, part, idx, unplaced, net_refs):
    """Spec §4.4. Mux: centroid of its pots' wiper pads (P3). 595 / 165: the
    centroid of the placed pads on its panel-side nets, followed through one
    unplaced 2-pin part. An IC whose panel-side nets reach nothing placed
    (U_SR5: its outputs go to test points only) falls back to all its signal
    nets; s.anchor_from records which rule gave the anchor."""
    if part.ref.startswith("U_MUX"):
        m = int(part.ref[len("U_MUX"):])
        pm = RB.load_panel_map()
        ids = {p["id"] for p in pm["pots"] if p["mux"] == m}
        refs = [r for r, pid in s.ids.items() if pid in ids]
        s.anchor_from[part.ref] = "wipers of its %d pots" % len(refs)
        return _centroid([_pad_xy(s.board, r, "2") for r in refs])
    pts = _through_two_pin(part, _panel_side_nets(part, net_refs), idx, unplaced)
    if pts:
        s.anchor_from[part.ref] = "%d panel-side pads" % len(pts)
        return _centroid(pts)
    pts = _through_two_pin(part, set(part.nets.values()) - SUPPLY, idx, unplaced)
    s.anchor_from[part.ref] = ("%d pads on all its signal nets (its panel-side nets "
                               "reach nothing placed)" % len(pts))
    return _centroid(pts)


def _place_one(s, part, anchor, cls, accept=None, fp=None):
    """First fit around `anchor`, or exactly at anchor + an OVERRIDES offset.
    s.anchors keeps the anchor itself, so the report measures an override.
    `fp`: the part's footprint when it is already on the board."""
    if fp is None:
        fp = kipcb.add_part(s.board, part, anchor[0], anchor[1], 0, side="B")
    inner = PL.grow(outline_box(), -EDGE_INSET)
    step, rmax = STEP[cls]
    rots = ROTS
    target = anchor
    if part.ref in OVERRIDES:
        dx, dy, rot, reason = OVERRIDES[part.ref]
        target = (anchor[0] + dx, anchor[1] + dy)
        step, rmax, rots = 0.1, 0.0, (rot,)
        s.overrides_used.append("%s %+.2f %+.2f rot %d: %s" % (part.ref, dx, dy, rot, reason))
    PL.first_fit(fp, target, s.blocked, inner, step, rmax, rotations=rots, accept=accept)
    s.parts[part.ref] = part
    s.anchors[part.ref] = anchor
    return fp


def _pad1_within(v):
    def ok(fp):
        x, y = [(pcbnew.ToMM(q.GetPosition().x), pcbnew.ToMM(q.GetPosition().y))
                for q in fp.Pads() if str(q.GetNumber()) == "1"][0]
        return math.hypot(x - v[0], y - v[1]) <= DECOUPLE_MAX_MM
    return ok


def cap_shape(fp):
    """{rot: (courtyard box, pad-1 centre)}, both relative to the footprint's
    position, measured on `fp` itself at each of ROTS. Leaves fp at rot 0,
    where kipcb.add_part put it."""
    x0, y0 = pcbnew.ToMM(fp.GetPosition().x), pcbnew.ToMM(fp.GetPosition().y)
    out = {}
    for rot in ROTS:
        fp.SetOrientationDegrees(rot)
        c = PL.courtyard_box(fp)
        if c is None:
            raise ValueError("%s has no courtyard to search with" % fp.GetReference())
        p1 = [q for q in fp.Pads() if str(q.GetNumber()) == "1"][0].GetPosition()
        out[rot] = ((c[0] - x0, c[1] - y0, c[2] - x0, c[3] - y0),
                    (pcbnew.ToMM(p1.x) - x0, pcbnew.ToMM(p1.y) - y0))
    fp.SetOrientationDegrees(ROTS[0])
    return out


def decoupler_spot(shape, v, blocked, inner):
    """The decoupling search (spec §4.4), pure geometry: the first spiral
    centre around the VCC pad `v` (STEP["decouple"]) and the first rotation
    there whose courtyard lies inside `inner`, overlaps nothing in `blocked`
    and puts pad 1 within DECOUPLE_MAX_MM of `v`. Returns (x, y, rot,
    courtyard) or None and changes nothing. The one implementation: the
    decoupler placement uses it, and the IC search dry-runs it."""
    step, rmax = STEP["decouple"]
    for x, y in PL.spiral(v[0], v[1], step, rmax):
        for rot in ROTS:
            (l, t, r, b), (px, py) = shape[rot]
            cand = (x + l, y + t, x + r, y + b)
            if not PL.inside(cand, inner) or any(PL.overlaps(cand, o) for o in blocked):
                continue
            if math.hypot(x + px - v[0], y + py - v[1]) <= DECOUPLE_MAX_MM - _ROUND_MM:
                return x, y, rot, cand
    return None


def _leaves_room(s, shape, vcc, inner):
    """IC accept (spec §4.4 amendment): the candidate spot leaves its 100 nF a
    spot by decoupler_spot, with the IC's own courtyard added to a copy of
    the blocked list. No footprint moves, nothing is appended."""
    def ok(fp):
        v = [(pcbnew.ToMM(q.GetPosition().x), pcbnew.ToMM(q.GetPosition().y))
             for q in fp.Pads() if str(q.GetNumber()) == vcc][0]
        return decoupler_spot(shape, v, s.blocked + [PL.courtyard_box(fp)], inner) is not None
    return ok


def _place_decoupler(s, part, fp, shape, v):
    """A 100 nF at the VCC pad `v`: decoupler_spot, or its OVERRIDES entry."""
    if part.ref in OVERRIDES:
        return _place_one(s, part, v, "decouple", accept=_pad1_within(v), fp=fp)
    spot = decoupler_spot(shape, v, s.blocked, PL.grow(outline_box(), -EDGE_INSET))
    if spot is None:
        raise ValueError("no free place for %s within %.1f mm of (%.2f, %.2f) with pad 1 "
                         "within %.1f mm" % (part.ref, STEP["decouple"][1], v[0], v[1],
                                            DECOUPLE_MAX_MM))
    x, y, rot, box = spot
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(kipcb._pt(x, y))
    s.blocked.append(box)
    s.parts[part.ref] = part
    s.anchors[part.ref] = v
    return fp


def place_smd(s, proj):
    """Spec §4.4 order: ICs (most pins first; U_REG, 3 pins, last, at J_PWR),
    each accepted only where its 100 nF still fits; the decouplers; J_SD's
    C_SD1/C_SD2 (2.0 mm like decoupling); then every 2-pin part: the power
    block's at J_PWR, LED resistors, C_SENSE, everything else."""
    by_ref = {p.ref: p for p in proj.parts()}
    sheet_of = proj.sheet_of()
    net_refs = _net_refs(proj)
    todo = [p for p in proj.parts() if p.on_board and p.footprint
            and p.ref not in s.parts]
    jp = s.board.FindFootprintByReference("J_PWR")
    jp_c = PL.courtyard_box(jp)
    jp_centre = ((jp_c[0] + jp_c[2]) / 2.0, (jp_c[1] + jp_c[3]) / 2.0)
    inner = PL.grow(outline_box(), -EDGE_INSET)
    s.sheet_of = sheet_of
    s.decouplers = decouplers(proj)
    cap_of = {ic: c for c, (ic, _n) in s.decouplers.items()}
    caps = {}                 # cref -> (footprint, shape), added with its IC

    ics = sorted([p for p in todo if p.ref.startswith("U_")],
                 key=lambda p: (-len(p.sym.pins), p.ref))
    for p in ics:
        if sheet_of[p.ref] == "power":
            s.anchor_from[p.ref] = "J_PWR's courtyard centre"
            _place_one(s, p, jp_centre, "power")
            continue
        unplaced = [q for q in todo if q.ref not in s.parts]
        a = _ic_anchor(s, p, pad_index(s.board), unplaced, net_refs)
        if a is None:
            raise ValueError("%s: no anchor (no placed pad on its signal nets)" % p.ref)
        accept = None
        if p.ref in cap_of:
            cref = cap_of[p.ref]
            cfp = kipcb.add_part(s.board, by_ref[cref], a[0], a[1], 0, side="B")
            caps[cref] = (cfp, cap_shape(cfp))
            accept = _leaves_room(s, caps[cref][1], s.decouplers[cref][1], inner)
        _place_one(s, p, a, "ic", accept=accept)

    for cref, (ic, vcc) in sorted(s.decouplers.items()):
        cfp, shape = caps[cref]
        _place_decoupler(s, by_ref[cref], cfp, shape, _pad_xy(s.board, ic, vcc))

    sd_vcc = _pad_xy(s.board, "J_SD", "4")
    s.decouplers["C_SD1"] = ("J_SD", "4")
    cfp = kipcb.add_part(s.board, by_ref["C_SD1"], sd_vcc[0], sd_vcc[1], 0, side="B")
    _place_decoupler(s, by_ref["C_SD1"], cfp, cap_shape(cfp), sd_vcc)
    # C_SD2 (DNP): plain first fit near J_SD's VCC pad, no 2.0 mm limit -- C_SD1 takes the only spot within it (spec §4.4, §5.1 item 5 gates C_SD1 only).
    _place_one(s, by_ref["C_SD2"], sd_vcc, "other")

    for p in sorted((p for p in todo if p.ref not in s.parts and sheet_of[p.ref] == "power"),
                    key=lambda p: p.ref):
        _place_one(s, p, jp_centre, "power")

    idx = pad_index(s.board)
    for p in sorted((p for p in todo if p.ref not in s.parts and sheet_of[p.ref] == "leds"),
                    key=lambda p: p.ref):
        led_net = p.nets["2"]           # pin 2 is the LED side (blocks.leds)
        led = [xy for ref, _n, xy in idx.get(led_net, []) if ref.startswith("D")]
        _place_one(s, p, led[0], "led_r")

    for p in sorted((p for p in todo if p.ref.startswith("C_SENSE")), key=lambda p: p.ref):
        net = p.nets["1"]               # pin 1 is the SENSE net (blocks.module)
        sm = [xy for ref, _n, xy in pad_index(s.board).get(net, []) if ref == "U_SM"]
        _place_one(s, p, sm[0], "other")

    for p in sorted((p for p in todo if p.ref not in s.parts), key=lambda p: p.ref):
        idx = pad_index(s.board)
        pts = [xy for net in sorted(set(p.nets.values()) - SUPPLY)
               for ref, _n, xy in idx.get(net, []) if ref != p.ref]
        a = _centroid(pts) or s.anchors["U_SM"]
        _place_one(s, p, a, "other")


def unconnected_net(part, number):
    """KiCad's name for the net of a pin the schematic leaves unconnected
    (forms probed 2026-10-02: unconnected-(J_PWR-Pin_7-Pad7),
    unconnected-(U_IN1-~{Q7}-Pad7)); schematic parity checks it. A pin
    without a name drops the name part: the jacks' TN pins are
    unconnected-(J1-PadTN) (parity probe on the committed board, 2026-10-02)."""
    name = part.sym.pin(number)["name"]
    return "unconnected-(%s-%sPad%s)" % (part.ref, name + "-" if name else "", number)


def link_footprints(s, proj):
    """P4-3 spec §4.2: every footprint linked to its symbol, every unconnected
    pin on KiCad's unconnected-(...) net. Returns the net names created."""
    sheet_of = proj.sheet_of()
    made = []
    for p in proj.parts():
        fp = s.board.FindFootprintByReference(p.ref)
        if fp is None:
            continue
        sheet = sheet_of[p.ref]
        kipcb.link_part(fp, p, W.symbol_path(proj.name, sheet, p.ref), sheet, sheet + ".kicad_sch")
        for number in p.unconnected():
            name = unconnected_net(p, number)
            if kipcb.set_pad_net(s.board, fp, number, name):
                made.append(name)
    return sorted(made)


def build():
    proj = RB.project()
    s = Placed()
    s.board = kipcb.new_board(X1 - X0, Y1 - Y0, LAYERS, origin=(X0, Y0))
    s.board.GetDesignSettings().m_CopperEdgeClearance = pcbnew.FromMM(EDGE_CLEAR)
    rect = [(X0, Y0), (X1, Y0), (X1, Y1), (X0, Y1)]
    for layer, net in PLANES:
        kipcb.add_zone(s.board, layer, net, rect)
    place_panel(s, proj)
    place_module(s, proj)
    s.blocked = _tht_blocked(s) + [s.shadow]
    place_power_header(s, proj, s.blocked)
    place_smd(s, proj)
    s.unconnected = link_footprints(s, proj)
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
    if a.write and not a.sabotage and not green:
        print("not copied: the run is RED")
    if a.write and not a.sabotage and green:
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
