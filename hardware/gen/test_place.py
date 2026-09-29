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
