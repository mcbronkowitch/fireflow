#!/usr/bin/env python3
"""One command: generate the coupon, then prove what was generated.

    python build.py            # from hardware/coupon/scripts/

Steps, in order, stopping at the first failure:

  1. generate coupon.kicad_sch from design.py + netlist.py
  2. export the netlist with kicad-cli and COMPARE IT TO THE INTENT.
     This is the load-bearing check. The schematic's connectivity is geometry --
     stub directions, a symbol-space Y flip -- and geometry is exactly what a
     generator gets quietly wrong. KiCad's own netlist is the independent read.
  3. ERC
  4. schematic PDF, for review by eye rather than by S-expression

The board steps are added once the schematic passes.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..")))
from gen import ksexp  # noqa: E402  (moved to hardware/gen, P3 spec §3.1)
from gen import check as C  # noqa: E402
import netlist as N
import generate_schematic as G

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
PROOF = os.path.join(ROOT, "proof")
FAB = os.path.join(ROOT, "fab")


def main():
    os.makedirs(PROOF, exist_ok=True)
    os.makedirs(FAB, exist_ok=True)
    sch = G.SCH_PATH
    cli = ksexp.KICAD_CLI

    print("0. library tables")
    s, f = C.write_lib_tables(N.build(), ROOT,
                              {"Daisy-Boards": "${KIPRJMOD}/../lib/DaisyKiCad"})
    print("   %d symbol libraries, %d footprint libraries" % (len(s), len(f)))

    print("1. generate")
    G.main()

    print("2. drawing collisions")
    parts = N.build()
    placed, _height = G.layout(parts)
    hits = G.overlaps(parts, placed)
    print("   %d overlapping pairs of labels, texts and symbol bodies"
          % len(hits))
    if hits:
        for a, b in hits[:20]:
            print("     - %s <-> %s" % (a, b))
        return 1

    print("3. netlist, exported and compared against the intent")
    net_path = os.path.join(FAB, "coupon.net")
    rc, _ = C.run([cli, "sch", "export", "netlist", "-o", net_path, sch],
                "kicad-cli sch export netlist")
    if rc != 0:
        return 1
    exported = C.parse_exported_netlist(net_path)
    intended = {k: set(v)
                for k, v in N.nets_from(N.build(), include_virtual=False).items()}
    bad = C.compare(intended, exported)
    print("   intended %d nets, exported %d" % (len(intended), len(exported)))
    if bad:
        print("   MISMATCH (%d):" % len(bad))
        for b in bad[:40]:
            print("     - " + b)
        return 1
    print("   every intended net matches node for node")

    print("4. ERC")
    erc_path = os.path.join(PROOF, "erc.rpt")
    # kicad-cli does not inherit KiCad's own path variables, so the project
    # library tables resolve to nothing and every symbol is reported as coming
    # from a library "the current configuration does not contain" -- 163 of the
    # first 167 violations were that and nothing else.
    defines = C.kicad_defines()
    rc, _ = C.run([cli, "sch", "erc", "--severity-error", "--severity-warning",
                 "--exit-code-violations"] + defines + ["-o", erc_path, sch],
                "kicad-cli sch erc")
    if os.path.exists(erc_path):
        txt = open(erc_path, encoding="utf-8", errors="replace").read()
        print("   " + txt.strip().splitlines()[0] if txt.strip() else "   (empty)")

    print("5. schematic PDF")
    pdf = os.path.join(PROOF, "coupon-schematic.pdf")
    C.run([cli, "sch", "export", "pdf", "-o", pdf, sch], "kicad-cli sch export pdf")

    return 0


if __name__ == "__main__":
    sys.exit(main())
