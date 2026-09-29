#!/usr/bin/env python3
"""Guard for hardware/gen/pcb_proof.py. Plain script; the exit code is the
verdict. Needs pcbnew, so under the system Python (ctest) it re-runs itself
under KiCad's Python."""
import os
import subprocess
import sys
import tempfile

HW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, HW)
try:
    import pcbnew  # noqa: F401
except ImportError:
    from gen import ksexp
    kipy = os.path.join(ksexp.KICAD_ROOT, "bin", "python.exe")
    sys.exit(subprocess.call([kipy, os.path.abspath(__file__)]))

from gen import kipcb            # noqa: E402
from gen import pcb_proof as PP  # noqa: E402

FAILS = []


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def board_with_two_resistors():
    """R1 at (5, 5), R2 at (15, 5); pad 1 of both on net A, pad 2 on net B.
    Net A is routed around the pads at y 7; net B is left unrouted."""
    b = kipcb.new_board(20, 10, 2)
    for ref, x in (("R1", 5.0), ("R2", 15.0)):
        fp = kipcb.footprint("Resistor_SMD:R_0603_1608Metric")
        fp.SetReference(ref)
        fp.SetPosition(kipcb._pt(x, 5.0))
        b.Add(fp)
        for pad in fp.Pads():
            pad.SetNet(kipcb._net(b, {"1": "A", "2": "B"}[pad.GetNumber()]))
    kipcb.add_track(b, "F.Cu", 0.25, "A",
                    [(4.2, 5.0), (4.2, 7.0), (14.2, 7.0), (14.2, 5.0)])
    return b


def main():
    tmp = tempfile.mkdtemp(prefix="pcb_proof_")
    check(abs(PP.seg_point_dist((0, 1), (0, 0), (2, 0)) - 1.0) < 1e-9,
          "seg_point_dist: point 1 mm above a segment")
    check(abs(PP.seg_seg_dist(((0, 0), (2, 0)), ((0, 3), (2, 3))) - 3.0) < 1e-9,
          "seg_seg_dist: parallel segments 3 mm apart")

    b = board_with_two_resistors()
    clean = os.path.join(tmp, "clean.kicad_pcb")
    kipcb.save(b, clean)
    check(PP.live_unconnected(b) == 1, "live_unconnected: net B's one pair")

    rpt = os.path.join(tmp, "clean.rpt")
    counts = PP.drc(clean, rpt)
    check(not counts.get("shorting_items") and not counts.get("clearance"),
          "drc: the clean board has no short and no clearance violation (%s)" % counts)
    by_net = PP.unconnected_by_net(rpt)
    check(by_net == {"B": {"R1.2", "R2.2"}},
          "unconnected_by_net: only net B, both pads (%s)" % by_net)
    segs = PP.track_segments(b, lambda n: n == "A")
    check(len(segs) == 3 and all(s[1] == "F.Cu" for s in segs),
          "track_segments: three F.Cu segments on net A")

    # A near miss, not a touch: a track that overlaps a foreign pad is renamed
    # to the pad's net on save (probed 2026-09-29), so a "short" drawn onto a
    # pad reaches the DRC as a dangling track, never as shorting_items.
    kipcb.add_track(b, "F.Cu", 0.25, "A", [(6.445, 3.0), (6.445, 6.5)])   # 0.1 mm beside R1.2
    near = os.path.join(tmp, "near.kicad_pcb")
    kipcb.save(b, near)
    counts = PP.drc(near, os.path.join(tmp, "near.rpt"))
    check(counts.get("clearance", 0) > 0,
          "drc: a net-A track 0.1 mm from a net-B pad is reported (%s)" % counts)

    # The stale report matters only when kicad-cli writes nothing: kicad-cli
    # overwrites an existing report whenever it runs (probed 2026-09-29).
    stale = os.path.join(tmp, "missing.rpt")
    with open(stale, "w", encoding="utf-8") as fh:
        fh.write("[stale_marker]: left behind by an earlier run\n")
    try:
        got = PP.drc(os.path.join(tmp, "missing.kicad_pcb"), stale)
        check(False, "drc: a missing board raises, even over a stale report (got %s)" % got)
    except RuntimeError:
        check(True, "drc: a missing board raises, even over a stale report")

    print("FAILED: %d" % len(FAILS) if FAILS else "all pcb_proof checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
