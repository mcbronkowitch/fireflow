#!/usr/bin/env python3
"""Guard for hardware/gen/stitch.py's `tht_keepoff_mm`. Plain script; the exit
code is the verdict. Needs pcbnew, so under the system Python (ctest) it
re-runs itself under KiCad's Python.

Scenario: one 0603 resistor, pad 1 on GND. Run with the default search, the
stitching via lands 1.0 mm outward of that pad. A GND through-hole pad is then
placed right where that via went: the default search ignores it (same net), a
search with `tht_keepoff_mm` must keep the via that far off the pad's copper,
edge to edge. A second through-hole pad on a net that is NOT a plane net sits
0.6 mm (box edge) from the default via: the keep-off must not apply to it.
Each scenario is its own board (`kipcb.new_board` reseeds pcbnew's UUIDs; the
boards are never saved together, so nothing collides)."""
import math
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

from gen import kipcb            # noqa: E402
from gen import stitch as ST     # noqa: E402

FAILS = []
PLANES = {"GND"}
R_POS = (10.0, 10.0)
THT = "Connector_PinHeader_2.54mm:PinHeader_1x01_P2.54mm_Vertical"


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def board(tht=(), **kw):
    """`tht` = [(ref, net, (x, y))]; returns (board, stitched, unresolved)."""
    b = kipcb.new_board(30, 20, 4)
    fp = kipcb.footprint("Resistor_SMD:R_0603_1608Metric")
    fp.SetReference("R1")
    fp.SetPosition(kipcb._pt(*R_POS))
    b.Add(fp)
    for pad in fp.Pads():
        pad.SetNet(kipcb._net(b, {"1": "GND", "2": "SIG"}[pad.GetNumber()]))
    for ref, net, at in tht:
        j = kipcb.footprint(THT)
        j.SetReference(ref)
        j.SetPosition(kipcb._pt(*at))
        b.Add(j)
        for pad in j.Pads():
            pad.SetNet(kipcb._net(b, net))
    st, unresolved = ST.stitch_plane_pads(b, PLANES, **kw)
    return b, st, unresolved


def vias(b):
    return [(pcbnew.ToMM(t.GetPosition().x), pcbnew.ToMM(t.GetPosition().y))
            for t in b.GetTracks() if t.Type() == pcbnew.PCB_VIA_T]


def pad_box(b, ref):
    bb = next(f for f in b.GetFootprints() if f.GetReference() == ref).Pads()[0].GetBoundingBox()
    return (pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop()),
            pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))


def edge_gap(v, box):
    """Via copper edge to the pad's bounding box, mm (negative: overlapping)."""
    l, t, r, btm = box
    return math.hypot(max(l - v[0], 0.0, v[0] - r), max(t - v[1], 0.0, v[1] - btm)) - ST.VIA_RADIUS_MM


def main():
    b0, st0, _u = board()
    v0 = vias(b0)
    check(len(v0) == 1 and st0 == {"GND": 1}, "default: one GND via (%s)" % v0)
    vx, vy = v0[0]

    # A GND through-hole pad exactly where the default via went.
    bd, _s, _u = board([("J1", "GND", (vx, vy))])
    same = vias(bd)
    check(same == v0, "default: the same-net through-hole pad does not move the via (%s)" % same)
    box = pad_box(bd, "J1")
    check(edge_gap(same[0], box) < 0.0, "default: that via sits on the pad (gap %.3f)" % edge_gap(same[0], box))

    # tht_keepoff_mm=None is the default, exactly.
    bn, _s, _u = board([("J1", "GND", (vx, vy))], tht_keepoff_mm=None)
    check(vias(bn) == same, "None: the same vias as the default")

    # With the keep-off the via leaves the pad by that margin, edge to edge.
    keep = 0.5
    bk, sk, uk = board([("J1", "GND", (vx, vy))], tht_keepoff_mm=keep)
    vk = vias(bk)
    check(len(vk) == 1 and sk == {"GND": 1} and not uk,
          "keep-off: still one via, resolved (%s, unresolved %s)" % (vk, uk))
    gap = edge_gap(vk[0], box) if vk else -1.0
    check(vk != same and gap >= keep - 1e-6,
          "keep-off: via moved, edge %.3f mm from the pad (need %.3f)" % (gap, keep))

    # A through-hole pad on a net that is not a plane net is not kept off:
    # its box edge is 0.6 mm from the default via (0.3 mm of copper + 0.3 mm of
    # air), past the existing 0.5 mm pad keepout but inside the 1.0 mm a
    # keep-off of 0.5 would add around a plane-net pad.
    half = (box[2] - box[0]) / 2.0                 # the pin header pad's half width
    ux = 1.0 if vx >= R_POS[0] else -1.0           # away from the resistor

    # The margin's arithmetic: a GND pad beyond the default via, its copper
    # 0.4 mm from the via's copper. Past a 0.3 mm keep-off the via stays; a
    # 0.5 mm keep-off moves it (a margin that forgot the via's own radius
    # would leave it in place).
    at = (vx + ux * (half + 0.3 + 0.4), vy)
    b3, _s, _u = board([("J1", "GND", at)], tht_keepoff_mm=0.3)
    b5, _s, _u = board([("J1", "GND", at)], tht_keepoff_mm=0.5)
    g = edge_gap(v0[0], pad_box(b3, "J1"))
    check(abs(g - 0.4) < 0.01 and vias(b3) == v0,
          "margin: pad %.3f mm from the default via; keep-off 0.3 leaves it (%s)" % (g, vias(b3)))
    v5 = vias(b5)
    check(len(v5) == 1 and v5 != v0 and edge_gap(v5[0], pad_box(b5, "J1")) >= 0.5 - 1e-6,
          "margin: keep-off 0.5 moves it to %s (%.3f mm from the pad)"
          % (v5, edge_gap(v5[0], pad_box(b5, "J1")) if v5 else -1.0))

    bo, _s, _u = board([("J2", "SIG", (vx + ux * (half + 0.6), vy))], tht_keepoff_mm=keep)
    gap_o = edge_gap(v0[0], pad_box(bo, "J2"))
    check(vias(bo) == v0 and abs(gap_o - 0.3) < 0.01,
          "non-plane pad: via stays at the default %s, %.3f mm from it (%s)" % (v0, gap_o, vias(bo)))
    print("FAILED: %d" % len(FAILS) if FAILS else "all stitch checks passed")
    sys.exit(1 if FAILS else 0)


if __name__ == "__main__":
    main()
