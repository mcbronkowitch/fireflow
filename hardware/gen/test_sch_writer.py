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


def check_global_labels(failures):
    """Spec P4-3 §4.2: with global_labels every net label is global, and
    KiCad's netlist names the demo's sheet-local net without a sheet prefix."""
    proj = demo.project()
    local_nets = [n for n in {net for p in proj.parts() for net in p.nets.values()}
                  if n not in proj.power
                  and len({s.name for s in proj.sheets for p in s.parts if n in p.nets.values()}) == 1]
    if not local_nets:
        failures.append("global_labels: the demo fixture has no sheet-local net to test")
        return
    proj.global_labels = True
    with tempfile.TemporaryDirectory() as d:
        W.write_project(proj, d)
        text = "".join(open(os.path.join(d, f), encoding="utf-8").read()
                       for f in sorted(os.listdir(d)) if f.endswith(".kicad_sch"))
        if "(label " in text:
            failures.append("global_labels: a local label was written")
        # Raw names on purpose: check.export_netlist() normalizes the
        # "/sheet/" prefix away, which is exactly what this test must see.
        sch = os.path.join(d, proj.name + ".kicad_sch")
        net = os.path.join(d, proj.name + ".net")
        rc, _out = check.run([ksexp.KICAD_CLI, "sch", "export", "netlist", "-o", net, sch],
                             "kicad-cli sch export netlist", quiet=True)
        names = check.parse_exported_netlist(net) if rc == 0 else {}
        for n in local_nets:
            if n not in names:
                failures.append("global_labels: KiCad does not name %r without a prefix (has %s)"
                                % (n, sorted(k for k in names if n in k)))


def check_path_helpers(failures):
    """symbol_path() equals the instance path the writer puts in the file,
    minus the root sheet -- the form KiCad's netlist gives a footprint."""
    proj = demo.project()
    with tempfile.TemporaryDirectory() as d:
        W.write_project(proj, d)
        seen = 0
        for s in proj.sheets:
            text = open(os.path.join(d, s.name + ".kicad_sch"), encoding="utf-8").read()
            for p in s.parts:
                if p.ref.startswith("#"):
                    continue
                # The file carries the instance path "/<root>/<sheet>" and the
                # symbol's own (uuid ...); the footprint path joins sheet and symbol.
                want = W.symbol_path(proj.name, s.name, p.ref)
                sheet, sym = W.sheet_uuid(proj.name, s.name), W.symbol_uuid(proj.name, p.ref)
                inst = '(path "/%s/%s" (reference "%s")' % (W.Uuids(proj.name)("root"), sheet, p.ref)
                if want != "/%s/%s" % (sheet, sym) or inst not in text or '(uuid "%s")' % sym not in text:
                    failures.append("symbol_path(%s) %s does not match %s.kicad_sch" % (p.ref, want, s.name))
                seen += 1
        if not seen:
            failures.append("path helpers: no symbol examined")


def main():
    if not os.path.exists(ksexp.KICAD_CLI):
        print("FAIL kicad-cli not found at %s" % ksexp.KICAD_CLI)
        return 1
    failures = []
    for make in (coupon_sch.project, demo.project):
        check_project(make, failures)
    check_global_labels(failures)
    check_path_helpers(failures)
    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("ok: coupon and demo export their intent; writes are byte-stable")
    return 0


if __name__ == "__main__":
    sys.exit(main())
