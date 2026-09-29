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
        self.blocked = []
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


def _tht_blocked(s):
    """Every through-hole pad on the board, grown by PAD_CLEAR: SMD parts on
    the back must keep off the pin tails."""
    out = []
    for fp in s.board.GetFootprints():
        for p in fp.Pads():
            if p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                out.append(PL.grow(PL.box(p.GetBoundingBox()), PAD_CLEAR))
    return out


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

    PL.first_fit(fp, target, blocked, PL.grow(outline_box(), -EDGE_INSET), 0.5, 60.0,
                 rotations=(rot,), accept=ok)
    s.parts["J_PWR"] = part
    s.anchors["J_PWR"] = target


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
