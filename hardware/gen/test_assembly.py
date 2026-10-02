#!/usr/bin/env python3
"""hw_gen_assembly_guard: the assembly-sheet code lives in hardware/gen/
(P4-3 spec §4.4.5); the coupon's two committed sheets still come out
byte-identical. Line endings are normalised: the script writes LF, a Windows
checkout holds CRLF.

    python hardware/gen/test_assembly.py      # exit code is the verdict
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
SCRIPT = os.path.join(HW, "coupon", "scripts", "assembly_plan.py")
PROOF = os.path.join(HW, "coupon", "proof")
SHEETS = ("coupon-assembly.svg", "coupon-overview.svg")


def lf(path):
    with open(path, "rb") as fh:
        return fh.read().replace(b"\r\n", b"\n")


BOARD = """(kicad_pcb (version 20240108)
  (footprint "L:F" (layer "F.Cu") (at 10 10)
    (property "Reference" "R1") (property "Value" "10k") (attr smd)
    (fp_rect (start -1 -1) (end 1 1) (layer "F.CrtYd")))
  (footprint "L:B" (layer "B.Cu") (at 20 10)
    (property "Reference" "R2") (property "Value" "10k") (attr smd dnp)
    (fp_rect (start -1 -1) (end 1 1) (layer "B.CrtYd")))
  (gr_rect (start 0 0) (end 30 20) (layer "Edge.Cuts")))
"""


def read_board_keys(assembly):
    """The two keys Rev A reads: footprint side, and DNP from (attr ... dnp)."""
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "t.kicad_pcb")
        with open(path, "w") as fh:
            fh.write(BOARD)
        parts, size = assembly.read_board(path, lambda value, lib: "res")
    got = {p["ref"]: (p["side"], p["dnp"], p["cls"]) for p in parts}
    want = {"R1": ("F", False, "res"), "R2": ("B", True, "dnp")}
    return [] if got == want else ["read_board side/dnp: got %r, want %r" % (got, want)]


def main():
    fails = []
    with tempfile.TemporaryDirectory() as d:
        rc = subprocess.call([sys.executable, SCRIPT, "--out-dir", d, "--prefix", "coupon"])
        if rc:
            fails.append("assembly_plan.py exited %d" % rc)
        for name in SHEETS:
            got = os.path.join(d, name)
            if not os.path.exists(got):
                fails.append("%s was not written" % name)
            elif lf(got) != lf(os.path.join(PROOF, name)):
                fails.append("%s differs from hardware/coupon/proof/%s" % (name, name))
    try:
        sys.path.insert(0, HW)
        from gen import assembly  # noqa: F401
    except ImportError as e:
        fails.append("gen.assembly does not import: %s" % e)
    else:
        fails += read_board_keys(assembly)
    for f in fails:
        print("FAIL", f)
    print("FAILED: %d" % len(fails) if fails else "assembly sheets unchanged")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
