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
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ksexp
import netlist as N
import generate_schematic as G

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
PROOF = os.path.join(ROOT, "proof")
FAB = os.path.join(ROOT, "fab")


def run(args, what):
    r = subprocess.run(args, capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
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


def write_lib_tables(parts):
    """Emit sym-lib-table / fp-lib-table covering exactly what the parts use.

    Derived from the part list rather than hand-kept, so a new part cannot
    silently leave the tables behind. Paths use KiCad's own ${KICAD10_*_DIR}
    variables and ${KIPRJMOD} for the vendored library, so nothing
    machine-specific reaches the repository.
    """
    sym_libs = sorted({p.lib_id.split(":")[0] for p in parts})
    fp_libs = sorted({p.footprint.split(":")[0] for p in parts if p.footprint})

    def rows(libs, kind):
        out = []
        for lib in libs:
            if lib == "Daisy-Boards":
                uri = ("${KIPRJMOD}/../lib/DaisyKiCad/Daisy-Boards."
                       + ("kicad_sym" if kind == "sym" else "pretty"))
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
        with open(os.path.join(ROOT, fname), "w",
                  encoding="utf-8", newline="\n") as fh:
            fh.write(body)
    return sym_libs, fp_libs


def main():
    os.makedirs(PROOF, exist_ok=True)
    os.makedirs(FAB, exist_ok=True)
    sch = G.SCH_PATH
    cli = ksexp.KICAD_CLI

    print("0. library tables")
    s, f = write_lib_tables(N.build())
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
    rc, _ = run([cli, "sch", "export", "netlist", "-o", net_path, sch],
                "kicad-cli sch export netlist")
    if rc != 0:
        return 1
    exported = parse_exported_netlist(net_path)
    intended = {k: set(v)
                for k, v in N.nets_from(N.build(), include_virtual=False).items()}
    bad = compare(intended, exported)
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
    share = os.path.join(ksexp.KICAD_ROOT, "share", "kicad")
    defines = ["-D", "KICAD10_SYMBOL_DIR=" + os.path.join(share, "symbols"),
               "-D", "KICAD10_FOOTPRINT_DIR=" + os.path.join(share, "footprints")]
    rc, _ = run([cli, "sch", "erc", "--severity-error", "--severity-warning",
                 "--exit-code-violations"] + defines + ["-o", erc_path, sch],
                "kicad-cli sch erc")
    if os.path.exists(erc_path):
        txt = open(erc_path, encoding="utf-8", errors="replace").read()
        print("   " + txt.strip().splitlines()[0] if txt.strip() else "   (empty)")

    print("5. schematic PDF")
    pdf = os.path.join(PROOF, "coupon-schematic.pdf")
    run([cli, "sch", "export", "pdf", "-o", pdf, sch], "kicad-cli sch export pdf")

    return 0


if __name__ == "__main__":
    sys.exit(main())
