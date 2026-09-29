#!/usr/bin/env python3
"""The shared schematic writer, proven by KiCad's own netlist export.

1. The coupon, written flat to a temp dir, exports exactly its intent.
2. The demo fixture -- three sheets, power symbols, a local net, a multi-unit
   op-amp -- exports exactly its intent.
3. Writing either project twice gives byte-identical files.
Needs kicad-cli (gen.ksexp.KICAD_CLI); without it this fails, never skips.

    python hardware/gen/test_sch_writer.py
"""
import filecmp
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HW)
sys.path.insert(0, os.path.join(HW, "coupon", "scripts"))

from gen import check, ksexp, sch_writer as W   # noqa: E402
from gen.fixtures import demo                   # noqa: E402
import generate_schematic as coupon_sch         # noqa: E402


def check_project(make, failures):
    proj = make()
    with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
        layouts = W.write_project(proj, a)
        W.write_project(make(), b)
        sheets = {s.name: s for s in proj.sheets}
        for name, (placed, height) in sorted(layouts.items()):
            for hit in W.overlaps(sheets[name].parts, placed, proj.power):
                failures.append("%s/%s: overlap %s <-> %s" % (proj.name, name, hit[0], hit[1]))
            if not W.fits(height, proj.paper):
                failures.append("%s/%s: %.0f mm tall, does not fit %s"
                                % (proj.name, name, height, proj.paper))
        names = sorted(os.listdir(a))
        if names != sorted(os.listdir(b)):
            failures.append("%s: file sets differ between runs" % proj.name)
        _, mismatch, errors = filecmp.cmpfiles(a, b, names, shallow=False)
        for n in mismatch + errors:
            failures.append("%s: %s differs between two writes" % (proj.name, n))
        exported = check.export_netlist(proj, a)
        intended = check.intended_nets(proj)
        for bad in check.compare(intended, exported):
            failures.append("%s: %s" % (proj.name, bad))
        victim = sorted(intended)[0]
        planted = dict(intended)
        planted[victim] = set(intended[victim]) | {("X_PLANTED", "1")}
        if not check.compare(planted, exported):
            failures.append("%s: compare() missed a planted node" % proj.name)
        print("  %s: %d files, %d intended nets" % (proj.name, len(names), len(intended)))


def main():
    if not os.path.exists(ksexp.KICAD_CLI):
        print("FAIL kicad-cli not found at %s" % ksexp.KICAD_CLI)
        return 1
    failures = []
    for make in (coupon_sch.project, demo.project):
        check_project(make, failures)
    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("ok: coupon and demo export their intent; writes are byte-stable")
    return 0


if __name__ == "__main__":
    sys.exit(main())
