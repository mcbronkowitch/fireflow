#!/usr/bin/env python3
"""Rev A silkscreen pass (P4-3 spec §4.3). A pure function on a board:

    silk.apply(board) -> Report

route.build() calls it last, after the zone fill. Standalone, to iterate in
seconds on a saved board without routing:

    KIPY hardware/reva/silk.py [board.kicad_pcb]   (default out/reva-routed.kicad_pcb)

writes out/reva-silk.kicad_pcb, runs kicad-cli DRC on it and prints the silk
text entries, and renders the back to out/reva-silk-bottom.png.

Front: references hidden (the plate covers the front; the assembly sheet says
what goes where). Back: every reference 1.0 mm, placed by a search around its
courtyard; a reference with no free spot is hidden and must be in NO_ROOM.
Collision boxes are deliberately simple (KiCad's own text bounding box, pad
and silk-graphic bounding boxes grown by a gap); KiCad's DRC is the judge
(route_check silk_clear), so a miss here shows up there, never silently.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
for _p in (HW, HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import pcbnew               # noqa: E402
from gen import kipcb      # noqa: E402

TEXT_MM = 1.0                                   # rule 3's floor, used as the size
STROKE_MM = 0.15
PAD_GAP_MM = 0.15                               # text box to a mask opening
SILK_GAP_MM = 0.15                              # text box to other silk
EDGE_GAP_MM = 0.5                               # text box to the board edge
GAPS_MM = (0.2, 0.5, 0.9, 1.4, 2.0, 2.7, 3.5)   # from the courtyard, nearest first
# a footprint text's first pass, inside its own courtyard: wider rings, since
# U_SM's 10 x 3.6 mm text has no free ring within 3.5 mm inside the module
TEXT_GAPS_MM = GAPS_MM + (5.0, 7.5, 10.0, 12.5, 15.0, 20.0, 25.0)
SLIDES_MM = (0.0, 0.8, -0.8)
SIDES = ("S", "N", "E", "W")
ANGLES = (0, 90)

# Back references (and footprint texts, "<ref>:text") with no free spot,
# measured by the first run (P4-3 Task 4); each entry says what blocked it.
# The 9 mm panel pass (Task 7c, 2026-10-08) freed C2, C3 and C5 (the caps that
# had sat between J_PWR's outline and D_P12/D_N12). After U_SM was pinned and
# the shift registers moved, three other references found no free spot in
# every route run (route_check no_room); the assembly sheet still names them
# (Bastian, 2026-10-08).
_PANEL_PASS = "no free back-side spot after the 9 mm panel pass (Task 7c, 2026-10-08); the assembly sheet names it"
NO_ROOM = {
    "C_SENSE3": _PANEL_PASS,
    "R25": _PANEL_PASS,
    "U_SR5": _PANEL_PASS,
}


class Report:
    def __init__(self):
        self.placed = {}
        self.hidden = []


def _natural(ref):
    m = re.match(r"([A-Za-z_]+?)(\d*)$", ref)
    return (m.group(1), int(m.group(2)) if m.group(2) else -1) if m else (ref, -1)


def _grow(bb, mm):
    g = pcbnew.FromMM(mm)
    return (bb.GetLeft() - g, bb.GetTop() - g, bb.GetRight() + g, bb.GetBottom() + g)


def _hits(a, boxes):
    return any(a[0] < b[2] and a[2] > b[0] and a[1] < b[3] and a[3] > b[1] for b in boxes)


def _inside(a, area):
    return a[0] >= area[0] and a[1] >= area[1] and a[2] <= area[2] and a[3] <= area[3]


def _obstacles(board):
    """Back mask openings and back silk graphics, as grown boxes (nm)."""
    out = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.IsOnLayer(pcbnew.B_Mask):
                out.append(_grow(pad.GetBoundingBox(), PAD_GAP_MM))
        for it in fp.GraphicalItems():
            if it.GetLayer() == pcbnew.B_SilkS and not isinstance(it, pcbnew.PCB_TEXT):
                out.append(_grow(it.GetBoundingBox(), SILK_GAP_MM))
    return out


def _candidates(box_mm, w_mm, h_mm, gaps=GAPS_MM):
    """Text centres around a box (l, t, r, b), nearest ring first."""
    l, t, r, b = box_mm
    cx, cy = (l + r) / 2, (t + b) / 2
    for gap in gaps:
        for side in SIDES:
            for slide in SLIDES_MM:
                if side == "S":
                    yield side, gap, (cx + slide, b + gap + h_mm / 2)
                elif side == "N":
                    yield side, gap, (cx + slide, t - gap - h_mm / 2)
                elif side == "E":
                    yield side, gap, (r + gap + w_mm / 2, cy + slide)
                else:
                    yield side, gap, (l - gap - w_mm / 2, cy + slide)


def _search(text, box_mm, area, blocked, within=None):
    """Move `text` to the first free candidate; returns (side, gap, angle) or
    None. With `within` (nm box), candidates lying inside it are tried first,
    on TEXT_GAPS_MM's rings, then the GAPS_MM ones anywhere."""
    for keep in ((within, None) if within else (None,)):
        for angle in ANGLES:
            text.SetTextAngleDegrees(angle)
            text.SetPosition(kipcb._pt((box_mm[0] + box_mm[2]) / 2, (box_mm[1] + box_mm[3]) / 2))
            bb = text.GetBoundingBox()
            w, h = pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())
            for side, gap, (x, y) in _candidates(box_mm, w, h, TEXT_GAPS_MM if keep else GAPS_MM):
                text.SetPosition(kipcb._pt(x, y))
                tb = _grow(text.GetBoundingBox(), 0.0)
                if (_inside(tb, area) and not _hits(tb, blocked)
                        and (keep is None or _inside(tb, keep))):
                    return side, gap, angle
    return None


def apply(board):
    rep = Report()
    edge = board.GetBoardEdgesBoundingBox()
    area = _grow(edge, -EDGE_GAP_MM)
    blocked = _obstacles(board)
    size = pcbnew.VECTOR2I(pcbnew.FromMM(TEXT_MM), pcbnew.FromMM(TEXT_MM))
    court = kipcb.courtyard_boxes(board)
    fps = sorted(board.GetFootprints(), key=lambda f: _natural(f.GetReference()))
    for fp in fps:
        if not fp.IsFlipped():
            fp.Reference().SetVisible(False)
    # footprint texts on the back first (U_SM's "INSTALL ON THIS SIDE"), so the
    # references avoid them; searched around their own box, starting in place,
    # inside their footprint's courtyard first (the text belongs to the part:
    # searched freely, U_SM's landed 15 mm outside the module, P4-3 Task 4)
    for fp in fps:
        for it in fp.GraphicalItems():
            if not (isinstance(it, pcbnew.PCB_TEXT) and it.GetLayer() == pcbnew.B_SilkS):
                continue
            key = fp.GetReference() + ":text"
            if it.IsVisible():
                # rule 3 governs every visible silk text (ruling R2/R8): the
                # stroke always, the height only where it is below TEXT_MM
                # (U_SM's is 1.1684 mm with a 0.1016 mm stroke)
                it.SetTextThickness(pcbnew.FromMM(STROKE_MM))
                if pcbnew.ToMM(it.GetTextSize().y) < TEXT_MM:
                    it.SetTextSize(size)
            here = _grow(it.GetBoundingBox(), 0.0)
            if _inside(here, area) and not _hits(here, blocked):
                rep.placed[key] = ("in place", 0.0, it.GetTextAngleDegrees())
            else:
                b = it.GetBoundingBox()
                own = tuple(pcbnew.FromMM(v) for v in court[fp.GetReference()])
                spot = _search(it, (pcbnew.ToMM(b.GetLeft()), pcbnew.ToMM(b.GetTop()),
                                    pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom())), area, blocked,
                               within=own)
                if spot is None:
                    it.SetVisible(False)
                    rep.hidden.append(key)
                    continue
                rep.placed[key] = spot
            blocked.append(_grow(it.GetBoundingBox(), SILK_GAP_MM))
    for fp in fps:
        if not fp.IsFlipped():
            continue
        ref = fp.GetReference()
        t = fp.Reference()
        t.SetVisible(True)
        t.SetTextSize(size)
        t.SetTextThickness(pcbnew.FromMM(STROKE_MM))
        spot = _search(t, court[ref], area, blocked)
        if spot is None:
            t.SetVisible(False)
            rep.hidden.append(ref)
            continue
        rep.placed[ref] = spot
        blocked.append(_grow(t.GetBoundingBox(), SILK_GAP_MM))
    rep.hidden.sort(key=_natural)
    return rep


def main(argv=None):
    from gen import pcb_proof as PP
    argv = sys.argv[1:] if argv is None else argv
    src = argv[0] if argv else os.path.join(HERE, "out", "reva-routed.kicad_pcb")
    board = kipcb.load(src)
    rep = apply(board)
    out = os.path.join(HERE, "out", "reva-silk.kicad_pcb")
    kipcb.save(board, out)
    print("placed %d, hidden %d: %s" % (len(rep.placed), len(rep.hidden), ", ".join(rep.hidden)))
    rpt = os.path.join(HERE, "out", "reva-silk-drc.rpt")
    try:
        PP.drc(out, rpt)
    except RuntimeError as e:
        print("drc:", e)
    txt = open(rpt, encoding="utf-8", errors="replace").read() if os.path.exists(rpt) else ""
    n = 0
    for block in re.split(r"(?=^\[)", txt, flags=re.M):
        if block.startswith(("[silk_over_copper]", "[silk_overlap]")) and \
                re.search(r"\): (?:Reference field|Value field|Footprint text|Text) ", block):
            n += 1
            print("  " + " | ".join(l.strip() for l in block.splitlines()[:4]))
    print("silk classes: %s" % ", ".join("%s %d" % (c, len(re.findall(r"^\[%s\]" % c, txt, re.M)))
                                         for c in ("silk_over_copper", "silk_overlap", "silk_edge_clearance")))
    for key in sorted(k for k in rep.placed if ":" in k):
        print("%s: %s" % (key, rep.placed[key]))
    print("silk text entries:", n)
    PP.render(out, os.path.join(HERE, "out", "reva-silk-bottom.png"), "bottom")
    return 0


if __name__ == "__main__":
    sys.exit(main())
