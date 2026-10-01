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
boards are never saved together, so nothing collides).

`via_keepouts` (spec §4.2.9, the pot bodies): a box centred on the GND pad,
4.9 mm each way, so every default candidate (<= 3.0 mm) lands in it and the
first clear via is the escape search's 5.5 mm. Without the option the via
lands inside; with it, outside, on a stub longer than 3.0 mm. Two traps sit
on the 5.5 mm westward stub with the via point and the stub's midpoint both
clear: an other-net pad, and an other-net locked segment. A midpoint-only
check would take that stub; the whole-length check must not."""
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


def inside(v, box):
    """The via POINT inside the box: its copper then overlaps by more than
    a radius, so a negative edge_gap alone would not say it."""
    return box[0] <= v[0] <= box[2] and box[1] <= v[1] <= box[3]


def stub(b):
    """The one stitching track: ((x1, y1), (x2, y2))."""
    t = [t for t in b.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T]
    return ((pcbnew.ToMM(t[0].GetStart().x), pcbnew.ToMM(t[0].GetStart().y)),
            (pcbnew.ToMM(t[0].GetEnd().x), pcbnew.ToMM(t[0].GetEnd().y))) if len(t) == 1 else None


def samples(s, step=0.01):
    (x1, y1), (x2, y2) = s
    n = max(1, int(math.hypot(x2 - x1, y2 - y1) / step))
    return [(x1 + (x2 - x1) * i / n, y1 + (y2 - y1) * i / n) for i in range(n + 1)]


def seg_box_gap(s, box):
    """Segment centre line to a box, sampled every 0.01 mm -- independent of
    stitch.py's exact arithmetic on purpose."""
    l, t, r, btm = box
    return min(math.hypot(max(l - x, 0.0, x - r), max(t - y, 0.0, y - btm)) for x, y in samples(s))


def seg_seg_gap(s, a, b):
    """Centre line to centre line, sampled every 0.01 mm along `s`."""
    def d(p):
        dx, dy = b[0] - a[0], b[1] - a[1]
        u = max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / (dx * dx + dy * dy)))
        return math.hypot(p[0] - a[0] - u * dx, p[1] - a[1] - u * dy)
    return min(d(p) for p in samples(s))


def keepout_checks(v0, half):
    """`via_keepouts`; `v0` is the default via, `half` the pin header pad's
    half width."""
    bp, _s, _u = board()
    pad = next(p for p in next(iter(bp.GetFootprints())).Pads() if p.GetNumber() == "1")
    px, py = pcbnew.ToMM(pad.GetPosition().x), pcbnew.ToMM(pad.GetPosition().y)
    box = (px - 4.9, py - 4.9, px + 4.9, py + 4.9)

    # Off: the option absent and the option empty are the default, exactly;
    # a box nowhere near the pad changes nothing either.
    be, _s, _u = board(via_keepouts=())
    check(vias(be) == v0, "via_keepouts=(): the same via as the default (%s)" % vias(be))
    bf, _s, _u = board(via_keepouts=[(25.0, 15.0, 28.0, 18.0)])
    check(vias(bf) == v0, "a far keepout box: the same via as the default (%s)" % vias(bf))
    check(inside(v0[0], box), "without the option the default via lands inside the box (%s)" % v0)

    # On: the via leaves the box on a stub longer than the default 3.0 mm.
    bk, sk, uk = board(via_keepouts=[box])
    vk, sv = vias(bk), stub(bk)
    gap = edge_gap(vk[0], box) if len(vk) == 1 else -1.0
    check(len(vk) == 1 and sk == {"GND": 1} and not uk,
          "keepout: one via, resolved (%s, unresolved %s)" % (vk, uk))
    check(gap >= ST.CLEARANCE_MM - 1e-6,
          "keepout: via copper %.3f mm outside the box (need %.3f)" % (gap, ST.CLEARANCE_MM))
    length = math.hypot(sv[1][0] - sv[0][0], sv[1][1] - sv[0][1]) if sv else 0.0
    check(length > max(ST.STANDOFFS_MM), "keepout: the stub is %.2f mm, past the default 3.0 mm" % length)

    # Trap 1: an other-net pad across the westward 5.5 mm stub. The via point
    # keeps 0.55 mm from its box (keepout 0.5) and the stub's midpoint 0.5 mm
    # (keepout 0.45): only the whole length can see the crossing.
    west = ((px, py), (px - 5.5, py))
    at = (px - 5.5 + 0.55 + half, py)
    b1, s1, u1 = board([("J2", "SIG2", at)], via_keepouts=[box])
    jbox = pad_box(b1, "J2")
    mid = ((west[0][0] + west[1][0]) / 2.0, py)
    check(edge_gap(west[1], jbox) + ST.VIA_RADIUS_MM >= ST.PAD_KEEPOUT_MM - 1e-6
          and seg_box_gap((mid, mid), jbox) >= ST.TRACK_KEEPOUT_MM - 1e-6
          and seg_box_gap(west, jbox) == 0.0,
          "trap 1 armed: via and midpoint clear of J2, the west stub crosses it")
    v1, t1 = vias(b1), stub(b1)
    g1 = seg_box_gap(t1, jbox) if t1 else -1.0
    check(len(v1) == 1 and not u1 and s1 == {"GND": 1} and inside(v1[0], box) is False,
          "trap 1: one via outside the box, resolved (%s, unresolved %s)" % (v1, u1))
    check(g1 >= ST.TRACK_KEEPOUT_MM - 1e-6,
          "trap 1: the stub keeps %.3f mm off the other-net pad (need %.3f), via %s"
          % (g1, ST.TRACK_KEEPOUT_MM, v1))

    # Trap 2: an other-net locked segment across the same stub, 1.4 mm from
    # the via point and 1.35 mm from the midpoint.
    hw = 0.125
    a, b = (px - 4.1, py - 1.0), (px - 4.1, py + 1.0)
    b2, s2, u2 = board(segments=[("SIG2", a, b, hw)], via_keepouts=[box])
    v2, t2 = vias(b2), stub(b2)
    g2 = seg_seg_gap(t2, a, b) if t2 else -1.0
    check(len(v2) == 1 and not u2 and not inside(v2[0], box),
          "trap 2: one via outside the box, resolved (%s, unresolved %s)" % (v2, u2))
    check(g2 >= hw + ST.TRACK_KEEPOUT_MM - 1e-6,
          "trap 2: the stub keeps %.3f mm off the segment's centre line (need %.3f), via %s"
          % (g2, hw + ST.TRACK_KEEPOUT_MM, v2))

    # Nowhere to go: a box over the whole board. The pad is unresolved as
    # before, and its line says the kept via is inside a keepout.
    bu, su, uu = board(via_keepouts=[(-50.0, -50.0, 80.0, 70.0)])
    check(len(uu) == 1 and uu[0].endswith(", inside a via keepout") and len(vias(bu)) == 1,
          "no escape: unresolved and marked (%s)" % uu)


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
    keepout_checks(v0, half)
    print("FAILED: %d" % len(FAILS) if FAILS else "all stitch checks passed")
    sys.exit(1 if FAILS else 0)


if __name__ == "__main__":
    main()
