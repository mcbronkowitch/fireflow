#!/usr/bin/env python3
"""Build hardware/coupon/coupon.kicad_pcb and prove it. Abort on first red."""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D
import ksexp
import netlist as N
import placement as P
import kipcb

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
PCB = os.path.join(ROOT, "coupon.kicad_pcb")
PROOF = os.path.join(ROOT, "proof")


def fail(msg):
    print("RED:", msg)
    sys.exit(1)


def build():
    parts = N.build()
    board = kipcb.new_board(D.BOARD_W_MM, D.BOARD_H_MM, D.LAYERS)
    placed = 0
    for part in parts:
        if not part.footprint:
            continue
        if part.ref not in P.PLACE:
            fail("no placement for %s" % part.ref)
        x, y, rot = P.PLACE[part.ref]
        kipcb.add_part(board, part, x, y, rot)
        placed += 1
    print("1. placed %d parts" % placed)
    return board, parts


def check_nets(board, parts):
    """Board connectivity against netlist.build(), node for node.

    Compared as SETS of (ref, pin), not as lists: a footprint may carry the
    same pad number more than once -- SW_PUSH_6mm has two pads "1" and two
    pads "2", one pair per switch contact -- so the board read-back is a
    multiset where the schematic intent has one node per pin. Duplicated pad
    numbers are the same electrical node; anything else is a real difference
    and still shows up below.
    """
    want = {n: sorted(set(v)) for n, v in
            N.nets_from(parts, include_virtual=False).items()}
    got = {n: sorted(set(v)) for n, v in kipcb.board_nets(board).items()}
    if want != got:
        for n in sorted(set(want) | set(got)):
            if want.get(n) != got.get(n):
                print("  net %-20s want %s got %s" % (n, want.get(n), got.get(n)))
        fail("board nets do not match the intent")
    print("2. %d nets match the intent node for node" % len(want))


def check_courtyards(pcb_path):
    """Placement sanity: no two parts may share board area.

    Runs KiCad's own DRC rather than re-deriving courtyard polygons here --
    the same reason the schematic side exports a netlist instead of trusting
    its own generator. Only `courtyards_overlap` gates this step; the other
    violation classes are unrouted-board noise until Task 6 routes it, so
    they are counted and printed, not enforced.
    """
    os.makedirs(PROOF, exist_ok=True)
    rpt = os.path.join(PROOF, "drc-placement.rpt")
    r = subprocess.run([ksexp.KICAD_CLI, "pcb", "drc",
                        "--exit-code-violations", "--severity-error",
                        "--severity-warning", "-o", rpt, pcb_path],
                       capture_output=True, text=True)
    if not os.path.exists(rpt):
        fail("kicad-cli pcb drc wrote no report (rc=%d)\n%s"
             % (r.returncode, (r.stdout + r.stderr).strip()))
    txt = open(rpt, encoding="utf-8", errors="replace").read()
    kinds = {}
    for kind in re.findall(r"^\[([a-z0-9_]+)\]", txt, re.M):
        kinds[kind] = kinds.get(kind, 0) + 1
    overlaps = [ln.strip() for ln in txt.splitlines()
                if "courtyards_overlap" in ln]
    if overlaps:
        for ln in overlaps[:20]:
            print("  " + ln)
        fail("%d courtyard overlaps" % len(overlaps))
    print("3. 0 courtyard overlaps (other DRC classes, not gated here: %s)"
          % (", ".join("%s %d" % kv for kv in sorted(kinds.items())) or "none"))


if __name__ == "__main__":
    board, parts = build()
    check_nets(board, parts)
    kipcb.save(board, PCB)
    print("wrote", PCB)
    check_courtyards(PCB)
