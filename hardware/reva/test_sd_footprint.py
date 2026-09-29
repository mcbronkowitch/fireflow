#!/usr/bin/env python3
"""Guard for the generated J_SD footprint: the committed file equals a fresh
generation, and KiCad loads it with the sourced pads. Re-runs itself under
KiCad's Python for the load half."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
for p in (HW, HERE):
    if p not in sys.path:
        sys.path.insert(0, p)

import sd_footprint as SD   # noqa: E402

FAILS = []


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def text_half():
    tmp = os.path.join(tempfile.mkdtemp(prefix="sdfp_"), "x.kicad_mod")
    SD.write(tmp)
    fresh = open(tmp, "rb").read()
    committed = open(SD.PATH, "rb").read() if os.path.exists(SD.PATH) else b""
    check(fresh == committed, "committed %s equals a fresh generation" % os.path.relpath(SD.PATH))


def load_half():
    import pcbnew
    from gen import kipcb
    fp = kipcb.footprint("FireFlow:" + SD.NAME)
    pads = {}
    for p in fp.Pads():
        pads.setdefault(str(p.GetNumber()), []).append(
            (round(pcbnew.ToMM(p.GetPosition().x), 3), round(pcbnew.ToMM(p.GetPosition().y), 3),
             round(pcbnew.ToMM(p.GetDrillSize().x), 2)))
    check(sorted(pads) == ["1", "2", "3", "4", "5", "6", "7", "8", "SH"],
          "pads 1-8 and SH (%s)" % sorted(pads))
    check(len(pads.get("SH", [])) == 2, "two SH pegs")
    check(pads.get("1") == [(-3.846, -1.075, 0.7)], "pad 1 at (-3.846, -1.075), drill 0.70 (%s)" % pads.get("1"))
    check(pads.get("8") == [(3.856, -2.175, 0.7)], "pad 8 at (3.856, -2.175), drill 0.70 (%s)" % pads.get("8"))
    check(sorted(pads.get("SH", [])) == [(-4.295, 2.095, 0.9), (4.305, 2.095, 0.9)],
          "SH pegs at (-4.295 / 4.305, 2.095), drill 0.90 (%s)" % pads.get("SH"))


def main():
    text_half()
    try:
        import pcbnew  # noqa: F401
        load_half()
    except ImportError:
        from gen import ksexp
        kipy = os.path.join(ksexp.KICAD_ROOT, "bin", "python.exe")
        rc = subprocess.call([kipy, os.path.abspath(__file__), "--load-only"])
        check(rc == 0, "the KiCad-side load half passed")
    print("FAILED: %d" % len(FAILS) if FAILS else "all sd footprint checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    if "--load-only" in sys.argv:
        load_half()
        sys.exit(1 if FAILS else 0)
    sys.exit(main())
