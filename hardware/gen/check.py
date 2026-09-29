#!/usr/bin/env python3
"""Checks for generated schematics: KiCad's own netlist against the intent.

Moved from hardware/coupon/scripts/build.py (P3 spec §3.1). The comparison is
the load-bearing check: a schematic's connectivity is geometry -- stub
directions, a symbol-space Y flip -- and geometry is exactly what a generator
gets quietly wrong. KiCad's exported netlist is the independent read.
"""
import argparse
import filecmp
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

_HW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
if _HW not in sys.path:
    sys.path.insert(0, _HW)

from gen import ksexp  # noqa: E402
from gen import netlist as N  # noqa: E402
from gen import sch_writer as W  # noqa: E402

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


def write_lib_tables(parts, dest_dir, lib_dirs, extra_sym_libs=()):
    """Emit sym-lib-table / fp-lib-table covering exactly what the parts use.

    Derived from the part list rather than hand-kept, so a new part cannot
    silently leave the tables behind. KiCad's own libraries go through its
    ${KICAD10_*_DIR} variables; vendored ones through `lib_dirs`, {library:
    directory uri holding <lib>.kicad_sym and/or <lib>.pretty} -- the coupon
    passes "${KIPRJMOD}/../lib/DaisyKiCad" for Daisy-Boards, so nothing
    machine-specific reaches the repository. A footprint without a library
    (the open "P4") gets no row.
    """
    sym_libs = sorted({p.lib_id.split(":")[0] for p in parts} | set(extra_sym_libs))
    fp_libs = sorted({p.footprint.split(":")[0] for p in parts if ":" in p.footprint})

    def rows(libs, kind):
        out = []
        for lib in libs:
            ext = "kicad_sym" if kind == "sym" else "pretty"
            if lib in lib_dirs:
                uri = "%s/%s.%s" % (lib_dirs[lib], lib, ext)
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


# --- the three check levels (P3 spec §4) ------------------------------------

VENDORED_DIR = os.path.join(_HW, "lib", "DaisyKiCad").replace("\\", "/")
OPEN_FOOTPRINT = "P4"       # the SD socket, chosen in P4 (spec §6); allowed once
KICAD_FP_DIR = os.path.join(ksexp.KICAD_ROOT, "share", "kicad", "footprints")
OUTPUT_TYPES = {"output", "tri_state", "power_out"}
PANEL_KINDS = {"pot", "jack", "key", "led", "sd"}


def _lib_dirs(project):
    """Vendored library dirs: Daisy-Boards always, plus the project's own."""
    dirs = {"Daisy-Boards": VENDORED_DIR}
    dirs.update({k: v.replace("\\", "/") for k, v in project.lib_dirs.items()})
    return dirs


class Finding:
    def __init__(self, rule, sheet, text):
        self.rule, self.sheet, self.text = rule, sheet, text

    def __str__(self):
        return "FAIL %s [%s]: %s" % (self.sheet, self.rule, self.text)


def _real_nets(project):
    """{net: [(part, pin)]} without power flags and power symbols."""
    nets = {}
    for p in project.parts():
        if N.is_virtual(p.ref):
            continue
        for pin, net in p.nets.items():
            nets.setdefault(net, []).append((p, pin))
    return nets


def rule_single_pin(project):
    sheet = project.sheet_of()
    nets = _real_nets(project)
    found = [Finding("single_pin", sheet[nodes[0][0].ref],
                     "net %s has one pin (%s.%s)" % (net, nodes[0][0].ref, nodes[0][1]))
             for net, nodes in sorted(nets.items()) if len(nodes) == 1]
    return len(nets), found


def rule_driver_conflict(project):
    sheet = project.sheet_of()
    examined, found = 0, []
    for net, nodes in sorted(_real_nets(project).items()):
        if net in project.power:
            continue
        drivers = [(p, pin) for p, pin in nodes
                   if p.etype(pin) in OUTPUT_TYPES]
        if not drivers:
            continue
        examined += 1
        if len(drivers) > 1:
            found.append(Finding("driver_conflict", sheet[drivers[0][0].ref],
                                 "net %s has %d outputs: %s" % (
                                     net, len(drivers),
                                     ", ".join("%s.%s" % (p.ref, pin) for p, pin in drivers))))
    return examined, found


def rule_rail_domain(project):
    """A3V3 feeds pots and muxes only, 3V3D the digital side (P2 decision 3)."""
    rail_domain = {r: d for d, rails in project.domain_rails.items() for r in rails}
    sheet = project.sheet_of()
    examined, found = 0, []
    for p in project.parts():
        if not p.domain:
            continue
        touched = sorted({n for n in p.nets.values() if n in rail_domain})
        if not touched:
            continue
        examined += 1
        if p.domain not in project.domain_rails:
            found.append(Finding("rail_domain", sheet[p.ref],
                                 "%s has unknown domain %r" % (p.ref, p.domain)))
            continue
        wrong = [n for n in touched if rail_domain[n] != p.domain]
        if wrong:
            found.append(Finding("rail_domain", sheet[p.ref], "%s is %s but sits on %s"
                                 % (p.ref, p.domain, ", ".join(wrong))))
    return examined, found


def rule_pins_accounted(project):
    sheet = project.sheet_of()
    strict = [p for p in project.parts() if p.strict]
    found = []
    for p in strict:
        loose = [n for n in p.unconnected() if n not in p.nc]
        if loose:
            found.append(Finding("pins_accounted", sheet[p.ref],
                                 "%s pins %s carry no net and are not marked no-connect"
                                 % (p.ref, ", ".join(loose))))
    return len(strict), found


def rule_panel_ids(project):
    holes = [h for h in (project.holes or []) if h["kind"] in PANEL_KINDS]
    sheet = project.sheet_of()
    by_id = {}
    for p in project.parts():
        if p.panel_id:
            by_id.setdefault(p.panel_id, []).append(p.ref)
    hole_ids = {h["id"] for h in holes}
    found = []
    for h in holes:
        refs = by_id.get(h["id"], [])
        if len(refs) != 1:
            found.append(Finding("panel_ids", "-", "hole %s (%s) has %d parts%s"
                                 % (h["id"], h["kind"], len(refs),
                                    (": " + ", ".join(refs)) if refs else "")))
    for pid, refs in sorted(by_id.items()):
        if pid not in hole_ids:
            found.append(Finding("panel_ids", sheet[refs[0]],
                                 "%s carries PanelId %s, which is no panel hole"
                                 % (", ".join(refs), pid)))
    for p in project.parts():
        if p.panel and not p.panel_id:
            found.append(Finding("panel_ids", sheet[p.ref],
                                 "%s is a panel part without a PanelId" % p.ref))
    return len(holes), found


def rule_sourced(project):
    """Every real part has exactly one of an LCSC number (JLC fits it) or a
    Source (bought elsewhere, hand-soldered) -- P3 spec §6."""
    sheet = project.sheet_of()
    parts = [p for p in project.parts() if not N.is_virtual(p.ref) and p.in_bom]
    found = [Finding("sourced", sheet[p.ref],
                     "%s needs exactly one of LCSC or Source (LCSC=%r, Source=%r)"
                     % (p.ref, p.lcsc, p.source))
             for p in parts if bool(p.lcsc) == bool(p.source)]
    return len(parts), found


def _footprint_exists(project, footprint):
    lib, _, name = footprint.partition(":")
    if not lib or not name:
        return False
    base = _lib_dirs(project).get(lib, KICAD_FP_DIR)
    return os.path.exists(os.path.join(base, lib + ".pretty", name + ".kicad_mod"))


def rule_footprints(project):
    """Every part on the board names a footprint that exists; parts off the
    board (sockets bought for the BOM) name none; exactly one open "P4" is
    allowed (spec §6)."""
    sheet = project.sheet_of()
    examined, found, open_refs = 0, [], []
    for p in project.parts():
        if N.is_virtual(p.ref):
            continue
        if not p.on_board:
            if p.footprint:
                found.append(Finding("footprints", sheet[p.ref], "%s is off the board "
                                     "but names footprint %s" % (p.ref, p.footprint)))
            continue
        examined += 1
        if p.footprint == OPEN_FOOTPRINT:
            open_refs.append(p.ref)
        elif not _footprint_exists(project, p.footprint):
            found.append(Finding("footprints", sheet[p.ref], "%s: footprint %r not found"
                                 % (p.ref, p.footprint)))
    if len(open_refs) > 1:
        found.append(Finding("footprints", "-", "only one open %r footprint is allowed: %s"
                             % (OPEN_FOOTPRINT, ", ".join(open_refs))))
    return examined, found


RULES = {"single_pin": rule_single_pin,
         "driver_conflict": rule_driver_conflict,
         "rail_domain": rule_rail_domain,
         "pins_accounted": rule_pins_accounted,
         "panel_ids": rule_panel_ids,
         "sourced": rule_sourced,
         "footprints": rule_footprints}


def fast(project):
    """Level 1: the intent alone, no KiCad, no file written."""
    findings = []
    for name, rule in RULES.items():
        examined, found = rule(project)
        if examined == 0:
            findings.append(Finding(name, "-", "examined nothing -- a rule with "
                                    "no input cannot pass"))
        findings += found
    return findings


# --- sabotage: every rule shows its RED on demand -----------------------------

class SabotageError(ValueError):
    """A sabotage that cannot be planted, or that the chosen level ignores."""


def _first(items, what):
    if not items:
        raise SabotageError("this sabotage needs %s and the project has none" % what)
    return items[0]


def _sab_add(project, part):
    project.sheets[0].parts.append(part)


def _sab_single_pin(pr):
    _sab_add(pr, N.Part("R_SAB1", "Device:R", "1k", "", lcsc="C-SABOTAGE")
             .by_number(1, "SAB_ALONE").by_number(2, pr.ground))


def _sab_driver_conflict(pr):
    driven = [n for n, nodes in sorted(_real_nets(pr).items())
              if n not in pr.power
              and any(p.etype(pin) in OUTPUT_TYPES for p, pin in nodes)]
    _sab_add(pr, N.Part("U_SAB1", "74xx:74HC595", "74HC595", "", lcsc="C-SABOTAGE")
             .by_name("QA", _first(driven, "a driven net")))


def _sab_rail_domain(pr):
    domains = sorted(pr.domain_rails)
    if len(domains) < 2:
        raise SabotageError("rail_domain sabotage needs two domains")
    rail = sorted(pr.domain_rails[domains[1]])[0]
    _sab_add(pr, N.Part("R_SAB1", "Device:R", "1k", "", lcsc="C-SABOTAGE",
                        domain=domains[0]).by_number(1, rail).by_number(2, pr.ground))


def _sab_pins_accounted(pr):
    p = _first([p for p in pr.parts() if p.strict and p.nets], "a strict part")
    del p.nets[sorted(p.nets, key=ksexp._pin_sort_key)[0]]


def _sab_panel_ids(pr):
    _first([p for p in pr.parts() if p.panel_id], "a panel part").panel_id = ""


def _sab_sourced(pr):
    p = _first([p for p in pr.parts() if not N.is_virtual(p.ref)], "a real part")
    p.lcsc = p.source = ""


def _sab_footprints(pr):
    p = _first([p for p in pr.parts() if not N.is_virtual(p.ref) and p.on_board],
               "a part on the board")
    p.footprint = "Nope:Missing"


def _sab_panel_orphan(pr):
    _sab_add(pr, N.Part("SW_SAB1", "Switch:SW_Push", "orphan", "",
                        source="C-SABOTAGE", panel=True)
             .by_number(1, pr.ground).by_number(2, pr.ground))


SABOTAGE = {"single_pin": _sab_single_pin,
            "driver_conflict": _sab_driver_conflict,
            "rail_domain": _sab_rail_domain,
            "pins_accounted": _sab_pins_accounted,
            "panel_ids": _sab_panel_ids,
            "sourced": _sab_sourced,
            "footprints": _sab_footprints,
            "panel_orphan": _sab_panel_orphan,
            "erc": _sab_single_pin}          # a one-pin label: ERC must see it too
# Handled inside the levels, not by editing the project. "X:empty" starves the
# check's own examined-nothing guard, as RULE:empty does for the fast rules.
LEVEL_SABOTAGE = {"overlap", "overlap:empty", "netlist", "netlist:empty",
                  "sheet_edge", "stability"}
FAST_SABOTAGE = ({r for r in SABOTAGE if r != "erc"} | {r + ":empty" for r in RULES})
SHEET_SABOTAGE = FAST_SABOTAGE | {"overlap", "overlap:empty", "netlist",
                                  "netlist:empty", "sheet_edge"}
FULL_SABOTAGE = SHEET_SABOTAGE | {"erc", "stability"}
HONOURED = {"fast": FAST_SABOTAGE, "sheet": SHEET_SABOTAGE, "full": FULL_SABOTAGE}


def _empty(pr, rule):
    if rule in ("single_pin", "driver_conflict", "sourced"):
        for s in pr.sheets:
            s.parts = []
    elif rule == "rail_domain":
        pr.domain_rails = {}
    elif rule == "pins_accounted":
        for p in pr.parts():
            p.strict = False
    elif rule == "panel_ids":
        pr.holes = []
    elif rule == "footprints":
        for p in pr.parts():
            p.on_board, p.footprint = False, ""


def apply_sabotage(project, spec):
    rule, _, mode = spec.partition(":")
    if spec in LEVEL_SABOTAGE:
        pass
    elif mode == "empty" and rule in RULES:
        _empty(project, rule)
    elif not mode and rule in SABOTAGE:
        SABOTAGE[rule](project)
    else:
        raise SabotageError("unknown sabotage %r; known: %s"
                            % (spec, ", ".join(sorted(FULL_SABOTAGE))))


def check_honoured(level, spec):
    """A sabotage the chosen level ignores would report PASS -- a RED that is
    no RED. Refuse it instead."""
    if spec in HONOURED[level]:
        return
    if spec in FULL_SABOTAGE:
        raise SabotageError("sabotage %r is not honoured by the %s level; it runs "
                            "at: %s" % (spec, level, ", ".join(
                                l for l in ("fast", "sheet", "full")
                                if spec in HONOURED[l])))
    raise SabotageError("unknown sabotage %r; known: %s"
                        % (spec, ", ".join(sorted(FULL_SABOTAGE))))


# --- levels 2 and 3 -------------------------------------------------------------

def _write_all(project, out_dir):
    sch_dir = os.path.join(out_dir, "sch")
    if os.path.isdir(sch_dir):
        shutil.rmtree(sch_dir)     # a renamed sheet's old file must not linger
    layouts = W.write_project(project, sch_dir)
    write_lib_tables(project.parts(), sch_dir, _lib_dirs(project),
                     extra_sym_libs={"power"} if project.power else ())
    return sch_dir, layouts


def _write_tree(project, dest):
    W.write_project(project, dest)
    write_lib_tables(project.parts(), dest, _lib_dirs(project),
                     extra_sym_libs={"power"} if project.power else ())


def _drawing(project, layouts, names, sabotage):
    sheets = {s.name: s for s in project.sheets}
    found = []
    planted = {"overlap": False, "sheet_edge": False}
    for name in names:
        placed, height = layouts[name]
        if sabotage == "overlap" and not planted["overlap"] and len(placed) > 1:
            keys = list(placed)
            placed = dict(placed)
            placed[keys[1]] = placed[keys[0]]
            planted["overlap"] = True
        if sabotage == "overlap:empty":
            placed = {}
        if sabotage == "sheet_edge" and not planted["sheet_edge"]:
            height = W.PAPER[project.paper][1]      # taller than the title block allows
            planted["sheet_edge"] = True
        if not placed:
            found.append(Finding("overlap", name, "examined nothing -- a sheet with "
                                 "no placed cells cannot pass"))
        else:
            for a, b in W.overlaps(sheets[name].parts, placed, project.power):
                found.append(Finding("overlap", name, "%s <-> %s" % (a, b)))
        if not W.fits(height, project.paper):
            found.append(Finding("sheet_edge", name,
                                 "content is %.0f mm tall; %s leaves %.0f above the "
                                 "title block" % (height, project.paper,
                                                  W.PAPER[project.paper][1] - W.TITLE_BLOCK_H)))
    if sabotage == "overlap" and not planted["overlap"]:
        raise SabotageError("overlap sabotage needs a checked sheet with two "
                            "cells; checked sheet(s) with fewer: %s" % ", ".join(names))
    return found


def _netlist(project, sch_dir, refs=None, sabotage=None):
    exported = {n: v for n, v in export_netlist(project, sch_dir).items()
                if not n.startswith("unconnected-")}
    intended = intended_nets(project)
    if refs is not None:
        def touches(nodes):
            return any(r in refs for r, _ in nodes)
        intended = {n: v for n, v in intended.items() if touches(v)}
        exported = {n: v for n, v in exported.items() if touches(v)}
    if sabotage == "netlist:empty":
        intended, exported = {}, {}
    elif sabotage == "netlist":
        if not intended:
            raise SabotageError("netlist sabotage needs an intended net and the "
                                "checked part has none")
        del intended[sorted(intended)[0]]    # compare() must now see an unintended net
    if not intended and not exported:
        return [Finding("netlist", "-", "examined nothing -- neither the intent "
                        "nor KiCad's export has a net to compare")]
    return [Finding("netlist", "-", bad) for bad in compare(intended, exported)]


def _pdf_and_pngs(project, sch_dir, out_dir, names, overview):
    import fitz   # PyMuPDF; imported here so the fast level needs nothing extra
    pdf = os.path.join(out_dir, project.name + ".pdf")
    rc, text = run([ksexp.KICAD_CLI, "sch", "export", "pdf", "-o", pdf,
                    os.path.join(sch_dir, project.name + ".kicad_sch")],
                   "kicad-cli sch export pdf", quiet=True)
    if rc != 0:
        raise RuntimeError("PDF export failed (rc=%d):\n%s" % (rc, text))
    doc = fitz.open(pdf)
    order = [s.name for s in project.sheets]
    want_pages = 1 if project.flat else len(order) + 1
    if doc.page_count != want_pages:
        raise RuntimeError("%s has %d pages, expected %d" % (pdf, doc.page_count, want_pages))
    pngs = []
    if overview and not project.flat:
        pngs.append(os.path.join(out_dir, project.name + "-overview.png"))
        doc[0].get_pixmap(dpi=110).save(pngs[-1])
    for name in names:
        pngs.append(os.path.join(out_dir, name + ".png"))
        doc[0 if project.flat else order.index(name) + 1].get_pixmap(dpi=110).save(pngs[-1])
    doc.close()
    return pngs


def load_waivers(path):
    """`type | exact item description | count | reason`, one per line."""
    if path is None:
        return []
    out = []
    with open(path, encoding="utf-8") as fh:
        for i, line in enumerate(fh, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = [f.strip() for f in line.split("|")]
            if len(fields) != 4 or not all(fields) or not fields[2].isdigit() \
                    or int(fields[2]) < 1:
                raise ValueError("%s:%d: want 'type | exact item | count | reason', "
                                 "got %r" % (path, i, line))
            out.append((fields[0], fields[1], int(fields[2]), fields[3]))
    return out


def match_waivers(violations, waivers):
    """violations: [(type, [item descriptions])]. A waiver names one exact item
    description and how many violations it covers. Unwaived violations, stale
    waivers and waivers that cover more or fewer than their count are all
    findings, so one line cannot quietly absorb a new violation on the same
    part, and `Pin 9` never matches `Pin 90`."""
    hits, found = [0] * len(waivers), []
    for vtype, items in violations:
        k = next((k for k, (wt, item, _, _) in enumerate(waivers)
                  if wt == vtype and item in items), None)
        if k is None:
            found.append(Finding("erc", "-", "%s: %s" % (vtype, "; ".join(items) or "(no items)")))
        else:
            hits[k] += 1
    for k, (wt, item, count, _) in enumerate(waivers):
        if hits[k] == 0:
            found.append(Finding("erc_waiver", "-", "waiver matches nothing: %s | %s"
                                 % (wt, item)))
        elif hits[k] != count:
            found.append(Finding("erc_waiver", "-", "waiver %s | %s covers %d violation(s), "
                                 "its count says %d" % (wt, item, hits[k], count)))
    return found


def _erc(project, sch_dir, out_dir):
    """ERC through kicad-cli, JSON report. Waivers match on the violation type
    and on the exact text of an item description ("Symbol U1 Pin 9 [...]"),
    never on the top-level description, which KiCad localizes (German on this
    machine)."""
    report = os.path.join(out_dir, "erc.json")
    if os.path.exists(report):
        os.remove(report)
    rc, text = run([ksexp.KICAD_CLI, "sch", "erc", "--format", "json",
                    "--severity-error", "--severity-warning"] + kicad_defines()
                   + ["-o", report, os.path.join(sch_dir, project.name + ".kicad_sch")],
                   "kicad-cli sch erc", quiet=True)
    if not os.path.exists(report):
        raise RuntimeError("ERC wrote no report (rc=%d):\n%s" % (rc, text))
    with open(report, encoding="utf-8") as fh:
        data = json.load(fh)
    violations = [(v["type"], [i.get("description", "") for i in v.get("items", [])])
                  for s in data["sheets"] for v in s["violations"]]
    return match_waivers(violations, load_waivers(project.waivers)), len(violations)


def _stability(factory, sabotage):
    found = []
    with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
        _write_tree(factory(), a)
        W.Uuids.random = sabotage == "stability"
        try:
            _write_tree(factory(), b)
        finally:
            W.Uuids.random = False
        names = sorted(set(os.listdir(a)) | set(os.listdir(b)))
        _, mismatch, errors = filecmp.cmpfiles(a, b, names, shallow=False)
        found += [Finding("stability", "-", "%s differs between two writes" % n)
                  for n in mismatch + errors]
    return found


def sheet_level(project, name, out_dir, sabotage=None):
    """Level 2: all sheets written (KiCad cannot export one sub-sheet alone),
    one sheet checked and rendered."""
    names = [s.name for s in project.sheets]
    if name not in names:
        raise ValueError("no sheet %r; sheets: %s" % (name, ", ".join(names)))
    os.makedirs(out_dir, exist_ok=True)
    findings = fast(project)
    sch_dir, layouts = _write_all(project, out_dir)
    findings += _drawing(project, layouts, [name], sabotage)
    refs = {p.ref for s in project.sheets if s.name == name for p in s.parts}
    findings += _netlist(project, sch_dir, refs, sabotage)
    for png in _pdf_and_pngs(project, sch_dir, out_dir, [name], overview=False):
        print("look at: " + png)
    return findings


def full_level(project, factory, out_dir, sabotage=None):
    """Level 3: everything, at the end of every task."""
    os.makedirs(out_dir, exist_ok=True)
    t = [time.monotonic()]

    def lap():
        t.append(time.monotonic())
        return t[-1] - t[-2]

    findings = fast(project)
    sch_dir, layouts = _write_all(project, out_dir)
    names = [s.name for s in project.sheets]
    findings += _drawing(project, layouts, names, sabotage)
    t_write = lap()
    findings += _netlist(project, sch_dir, None, sabotage)
    t_net = lap()
    erc_found, n = _erc(project, sch_dir, out_dir)
    findings += erc_found
    t_erc = lap()
    pngs = _pdf_and_pngs(project, sch_dir, out_dir, names, overview=True)
    t_pdf = lap()
    findings += _stability(factory, sabotage)
    t_stab = lap()
    print("ERC: %d violation(s) reported, %d unwaived"
          % (n, len([f for f in erc_found if f.rule == "erc"])))
    print("timing: fast+write+drawing %.1f s, netlist %.1f s, ERC %.1f s, "
          "PDF+PNG %.1f s, stability %.1f s" % (t_write, t_net, t_erc, t_pdf, t_stab))
    for png in pngs:
        print("look at: " + png)
    return findings


def load_factory(path):
    spec = importlib.util.spec_from_file_location("gen_project_under_check",
                                                  os.path.abspath(path))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod.project


def main(argv=None):
    ap = argparse.ArgumentParser(description="Check a generated schematic "
                                 "(P3 spec §4).")
    ap.add_argument("--project", required=True,
                    help="a .py file defining project() -> gen.project.Project")
    level = ap.add_mutually_exclusive_group(required=True)
    level.add_argument("--fast", action="store_true", help="intent only, <1 s")
    level.add_argument("--sheet", metavar="NAME", help="check and render one sheet")
    level.add_argument("--full", action="store_true", help="everything, incl. ERC")
    ap.add_argument("--sabotage", metavar="SPEC",
                    help="break one thing on purpose: RULE, RULE:empty, overlap, "
                         "overlap:empty, netlist, netlist:empty, sheet_edge "
                         "(--sheet and --full), erc, stability (--full only); "
                         "a level refuses a sabotage it does not run")
    ap.add_argument("--out", help="output dir (default: <project dir>/out)")
    args = ap.parse_args(argv)

    factory = load_factory(args.project)
    out = args.out or os.path.join(os.path.dirname(os.path.abspath(args.project)), "out")
    level_name = "fast" if args.fast else "sheet" if args.sheet else "full"
    t0 = time.monotonic()
    try:
        project = factory()
        if args.sabotage:
            check_honoured(level_name, args.sabotage)
            apply_sabotage(project, args.sabotage)
        if args.fast:
            label, findings = "fast", fast(project)
        elif args.sheet:
            label = "sheet " + args.sheet
            findings = sheet_level(project, args.sheet, out, args.sabotage)
        else:
            label, findings = "full", full_level(project, factory, out, args.sabotage)
    except SabotageError as e:
        ap.error(str(e))
    except (ValueError, RuntimeError, KeyError) as e:
        print(Finding("error", "-", "%s: %s" % (type(e).__name__, e)))
        print("FAIL %s: the check could not run, %.1f s"
              % (level_name, time.monotonic() - t0))
        return 1
    dt = time.monotonic() - t0
    for f in findings:
        print(f)
    if findings:
        print("FAIL %s: %d finding(s), %.1f s" % (label, len(findings), dt))
        return 1
    print("PASS %s: %d parts, %.1f s" % (label, len(project.parts()), dt))
    return 0


if __name__ == "__main__":
    sys.exit(main())
