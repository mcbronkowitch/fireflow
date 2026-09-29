#!/usr/bin/env python3
"""The P4a proof chain (spec §4.1). Every step runs and prints; the run is
green only if every gated step is. Each gated step has a sabotage that must
turn it red, and a zero-match guard: a step that examined nothing is red,
never green."""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import pcbnew                        # noqa: E402
import stripe as ST                  # noqa: E402
from gen import kipcb                # noqa: E402
from gen import pcb_proof as PP      # noqa: E402

# track_dangling is gated beside the coupon's five: a track drawn onto a
# foreign pad is renamed to that pad's net on save (probed 2026-09-29), so a
# router's short arrives here as a dangling track, not as shorting_items.
GATED = ("shorting_items", "clearance", "hole_clearance", "hole_to_hole",
         "tracks_crossing", "track_dangling")


def _fp(board, ref):
    return board.FindFootprintByReference(ref)


def check_nets(s, pcb_path, prefix):
    want = ST.intent(s)
    if not want:
        return False, "examined 0 nets", []
    got = {n: set(v) for n, v in kipcb.board_nets(s.board).items()}
    bad = ["%s want %s got %s" % (n, sorted(want.get(n, ())), sorted(got.get(n, ())))
           for n in sorted(set(want) | set(got)) if want.get(n) != got.get(n)]
    return not bad, "%d nets, %d differ from the intent node for node" % (len(want), len(bad)), bad


def check_anchors(s, pcb_path, prefix):
    if not s.holes:
        return False, "examined 0 panel parts", []
    bad, worst = [], 0.0
    for ref, (hx, hy) in sorted(s.holes.items()):
        x, y = ST.hole_point(_fp(s.board, ref))
        d = math.hypot(x - hx, y - hy)
        worst = max(worst, d)
        if d > 0.01:
            bad.append("%s sits %.3f mm off its hole" % (ref, d))
    return not bad, "%d panel parts, worst %.4f mm off its hole (limit 0.01)" % (len(s.holes), worst), bad


def check_decoupling(s, pcb_path, prefix):
    if not s.decouplers:
        return False, "examined 0 decouplers", []
    bad, worst = [], 0.0
    for cref, mref in sorted(s.decouplers.items()):
        c = [p for p in _fp(s.board, cref).Pads() if str(p.GetNumber()) == "1"][0]
        m = [p for p in _fp(s.board, mref).Pads()
             if str(p.GetNumber()) == s.vcc_pin[mref]][0]
        d = math.hypot(pcbnew.ToMM(c.GetPosition().x - m.GetPosition().x),
                       pcbnew.ToMM(c.GetPosition().y - m.GetPosition().y))
        worst = max(worst, d)
        if d > ST.DECOUPLE_MAX_MM:
            bad.append("%s is %.3f mm from %s's VCC pad" % (cref, d, mref))
    return not bad, "%d decouplers, worst %.3f mm (limit %.1f)" % (
        len(s.decouplers), worst, ST.DECOUPLE_MAX_MM), bad


def check_copper(s, pcb_path, prefix):
    counts = PP.drc(pcb_path, prefix + "-drc.rpt")
    bad = ["%s: %d" % (k, counts[k]) for k in GATED if counts.get(k)]
    others = ", ".join("%s %d" % kv for kv in sorted(counts.items())
                       if kv[0] not in GATED) or "none"
    gated = ", ".join("%s %d" % (k, counts.get(k, 0)) for k in GATED)
    return not bad, "gated: %s (not gated: %s)" % (gated, others), bad


def render(s, pcb_path, prefix):
    lines = []
    for side, suffix in (("top", "-top.png"), ("bottom", "-bottom.png")):
        rc, out = PP.render(pcb_path, prefix + suffix, side)
        if rc:
            lines.append("render %s rc=%d: %s" % (side, rc, out[-300:]))
    return not lines, "rendered %s-top.png, %s-bottom.png" % (
        os.path.basename(prefix), os.path.basename(prefix)), lines


def _steps(routed):
    steps = [("nets", check_nets), ("anchors", check_anchors),
             ("decoupling", check_decoupling), ("copper", check_copper)]
    return steps + [("render", render)]


def run(s, pcb_path, prefix, routed):
    green = True
    for i, (name, fn) in enumerate(_steps(routed), 1):
        ok, line, details = fn(s, pcb_path, prefix)
        print("%s %d. %-10s %s" % ("   " if ok else "RED", i, name, line))
        for d in details[:25]:
            print("        " + d)
        green = green and ok
    return green


def _sab_nets(s):
    fp = _fp(s.board, sorted(s.holes)[0])
    pad = [p for p in fp.Pads() if p.GetNetname()][0]
    pad.SetNet(kipcb._net(s.board, "SABOTAGE"))


def _sab_nets_missing(s):
    s.parts[:] = []


def _sab_anchors(s):
    fp = _fp(s.board, sorted(s.holes)[0])
    fp.Move(kipcb._pt(0.5, 0.0))


def _sab_anchors_missing(s):
    s.holes.clear()


def _sab_decoupling(s):
    cref = sorted(s.decouplers)[0]
    _fp(s.board, cref).Move(kipcb._pt(3.0, 0.0))


def _sab_decoupling_missing(s):
    s.decouplers.clear()


def _sab_copper(s):
    """A GND track 0.1 mm beside a decoupler's SM_3V3 pad -- a near miss, not
    a touch: copper overlapping a foreign pad is renamed to that pad's net on
    save (probed 2026-09-29), so a touch reaches the DRC as a dangling track."""
    fp = _fp(s.board, sorted(s.decouplers)[0])
    bb = [p for p in fp.Pads() if str(p.GetNumber()) == "1"][0].GetBoundingBox()
    x = pcbnew.ToMM(bb.GetRight()) + 0.1 + ST.W_SIGNAL / 2.0
    kipcb.add_track(s.board, "B.Cu", ST.W_SIGNAL, ST.SUPPLY_NETS[0],
                    [(x, pcbnew.ToMM(bb.GetTop()) - 1.0), (x, pcbnew.ToMM(bb.GetBottom()) + 1.0)])


SABOTAGES = {"nets": _sab_nets, "nets_missing": _sab_nets_missing,
             "anchors": _sab_anchors, "anchors_missing": _sab_anchors_missing,
             "decoupling": _sab_decoupling, "decoupling_missing": _sab_decoupling_missing,
             "copper": _sab_copper}


def sabotage(s, name):
    if name not in SABOTAGES:
        raise SystemExit("unknown sabotage %r; known: %s" % (name, ", ".join(sorted(SABOTAGES))))
    SABOTAGES[name](s)
    print("SABOTAGED: %s" % name)
