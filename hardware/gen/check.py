#!/usr/bin/env python3
"""Checks for generated schematics: KiCad's own netlist against the intent.

Moved from hardware/coupon/scripts/build.py (P3 spec §3.1). The comparison is
the load-bearing check: a schematic's connectivity is geometry -- stub
directions, a symbol-space Y flip -- and geometry is exactly what a generator
gets quietly wrong. KiCad's exported netlist is the independent read.
"""
import os
import subprocess
import sys

_HW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
if _HW not in sys.path:
    sys.path.insert(0, _HW)

from gen import ksexp  # noqa: E402
from gen import netlist as N  # noqa: E402

VENDORED_LIBS = {"Daisy-Boards"}


def run(args, what, quiet=False):
    r = subprocess.run(args, capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    if not quiet:
        print("  %-28s rc=%d" % (what, r.returncode))
        if out:
            for line in out.splitlines()[:25]:
                print("      " + line)
    return r.returncode, out


def parse_exported_netlist(path):
    """{net name: {(ref, pin), ...}} from KiCad's own export."""
    root = ksexp.parse_file(path)
    nets = {}
    for net in ksexp.children(ksexp.child(root, "nets"), "net"):
        name = str(ksexp.child(net, "name")[1])
        nodes = set()
        for node in ksexp.children(net, "node"):
            nodes.add((str(ksexp.child(node, "ref")[1]),
                       str(ksexp.child(node, "pin")[1])))
        nets[name] = nodes
    return nets


def compare(intended, exported):
    """Report every difference. Returns a list of complaints."""
    bad = []
    for name, nodes in sorted(intended.items()):
        want = set(nodes)
        if name not in exported:
            bad.append("net %s is missing from the exported netlist "
                       "(intended nodes: %s)"
                       % (name, ", ".join("%s.%s" % n for n in sorted(want))))
            continue
        got = exported[name]
        missing = want - got
        extra = got - want
        if missing:
            bad.append("net %s is missing %s"
                       % (name, ", ".join("%s.%s" % n for n in sorted(missing))))
        if extra:
            bad.append("net %s carries unintended %s"
                       % (name, ", ".join("%s.%s" % n for n in sorted(extra))))
    # KiCad names unconnected single pins "unconnected-(...)"; those are the
    # deliberate no-connects and are not a difference worth reporting.
    for name in sorted(exported):
        if name not in intended and not name.startswith("unconnected-"):
            bad.append("net %s exists in the schematic but not in the intent "
                       "(nodes: %s)"
                       % (name, ", ".join("%s.%s" % n for n in sorted(exported[name]))))
    return bad


def write_lib_tables(parts, dest_dir, vendored_uri, extra_sym_libs=()):
    """Emit sym-lib-table / fp-lib-table covering exactly what the parts use.

    Derived from the part list rather than hand-kept, so a new part cannot
    silently leave the tables behind. KiCad's own libraries go through its
    ${KICAD10_*_DIR} variables; the vendored Daisy library through
    `vendored_uri` (the coupon passes "${KIPRJMOD}/../lib/DaisyKiCad", so
    nothing machine-specific reaches the repository).
    """
    sym_libs = sorted({p.lib_id.split(":")[0] for p in parts} | set(extra_sym_libs))
    fp_libs = sorted({p.footprint.split(":")[0] for p in parts if p.footprint})

    def rows(libs, kind):
        out = []
        for lib in libs:
            if lib in VENDORED_LIBS:
                uri = "%s/%s.%s" % (vendored_uri, lib,
                                    "kicad_sym" if kind == "sym" else "pretty")
            elif kind == "sym":
                uri = "${KICAD10_SYMBOL_DIR}/%s.kicad_sym" % lib
            else:
                uri = "${KICAD10_FOOTPRINT_DIR}/%s.pretty" % lib
            out.append('  (lib (name "%s")(type "KiCad")(uri "%s")'
                       '(options "")(descr ""))' % (lib, uri))
        return "\n".join(out)

    for kind, libs, fname in (("sym", sym_libs, "sym-lib-table"),
                              ("fp", fp_libs, "fp-lib-table")):
        head = "sym_lib_table" if kind == "sym" else "fp_lib_table"
        body = "(%s\n  (version 7)\n%s\n)\n" % (head, rows(libs, kind))
        with open(os.path.join(dest_dir, fname), "w",
                  encoding="utf-8", newline="\n") as fh:
            fh.write(body)
    return sym_libs, fp_libs


def kicad_defines():
    """-D arguments kicad-cli needs to resolve the library tables.

    kicad-cli does not inherit KiCad's own path variables, so the project
    library tables resolve to nothing and every symbol is reported as coming
    from a library "the current configuration does not contain" -- 163 of the
    coupon's first 167 ERC violations were that and nothing else.
    """
    share = os.path.join(ksexp.KICAD_ROOT, "share", "kicad")
    return ["-D", "KICAD10_SYMBOL_DIR=" + os.path.join(share, "symbols"),
            "-D", "KICAD10_FOOTPRINT_DIR=" + os.path.join(share, "footprints")]


def normalize(nets):
    """Sheet-local nets export as /<sheet>/<name>; the intent knows only <name>."""
    out = {}
    for name, nodes in nets.items():
        short = name.rsplit("/", 1)[-1] if name.startswith("/") else name
        if short in out:
            raise ValueError("two exported nets shorten to %r" % short)
        out[short] = nodes
    return out


def export_netlist(project, sch_dir):
    """kicad-cli's netlist of the written project, names normalized."""
    sch = os.path.join(sch_dir, project.name + ".kicad_sch")
    net = os.path.join(sch_dir, project.name + ".net")
    rc, out = run([ksexp.KICAD_CLI, "sch", "export", "netlist", "-o", net, sch],
                  "kicad-cli sch export netlist", quiet=True)
    if rc != 0:
        raise RuntimeError("netlist export failed (rc=%d):\n%s" % (rc, out))
    return normalize(parse_exported_netlist(net))


def intended_nets(project):
    return {k: set(v) for k, v in
            N.nets_from(project.parts(), include_virtual=False).items()}
