#!/usr/bin/env python3
"""Checks on the Rev A order package (P4-3 spec §5.2), in P4-1/P4-2's pattern:
every gated step has a sabotage and a `_missing` one with its own phrase, and
a step that examined nothing is red. Thresholds live here, not in fab.py."""
import csv
import os
import re

import pcbnew
import check_kit as CK

POS_TOL_MM = 0.01
EDGE_TOL_MM = 0.01
BOARD_W_MM, BOARD_H_MM = 300.8, 110.0          # P4-1 spec §2.1, the outline
EXPECTED_LAYERS = ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu", "F.Mask", "B.Mask", "F.Silkscreen",
                   "B.Silkscreen", "F.Paste", "B.Paste", "Edge.Cuts")
DRILL_FILES = ("reva-PTH.drl", "reva-NPTH.drl")  # Step 1 measured (kicad-cli 10.0.5)

GERBER_EMPTY = "gerber_set measured nothing: no file in the gerber directory"
DRILL_EMPTY = "drill measured nothing: no drill file read"
CPL_EMPTY = "cpl measured nothing: no CPL row read"
ROT_EMPTY = "rot_table measured nothing: no package examined"
BOM_EMPTY = "bom_lcsc measured nothing: no BOM line read"
ASSEMBLY_EMPTY = "assembly measured nothing: no part on a sheet"

_COORD_RE = re.compile(r"X(-?\d+)Y(-?\d+)D0[123]\*")
_HOLE_RE = re.compile(r"^X-?[\d.]+Y-?[\d.]+", re.M)


def _gerber_name(layer):
    return "reva-%s." % layer.replace(".", "_")


def edge_extent(gerber_dir):
    """(x0, y0, x1, y1) in mm of Edge.Cuts, read from the exported Gerber
    (format 4.6, so coordinates are in 1e-6 mm; Gerber y points up, so it is
    the board's y negated -- the same convention as KiCad's CPL, which is why
    one extent serves both checks)."""
    names = [n for n in os.listdir(gerber_dir) if n.startswith(_gerber_name("Edge.Cuts"))] \
        if os.path.isdir(gerber_dir) else []
    if len(names) != 1:
        return None
    pts = [(int(x) / 1e6, int(y) / 1e6) for x, y in
           _COORD_RE.findall(open(os.path.join(gerber_dir, names[0]), encoding="utf-8").read())]
    if not pts:
        return None
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def check_gerber_set(s, _a=None, _b=None):
    gdir = os.path.join(s.out, "gerbers")
    names = sorted(os.listdir(gdir)) if os.path.isdir(gdir) else []
    if not names:
        return False, GERBER_EMPTY, []
    bad = []
    for layer in EXPECTED_LAYERS:
        hits = [n for n in names if n.startswith(_gerber_name(layer))]
        if len(hits) != 1:
            bad.append("missing layer %s" % layer if not hits else "layer %s twice: %s" % (layer, hits))
        elif os.path.getsize(os.path.join(gdir, hits[0])) == 0:
            bad.append("empty layer file %s" % hits[0])
    for d in DRILL_FILES:
        if d not in names or os.path.getsize(os.path.join(gdir, d)) == 0:
            bad.append("missing or empty drill file %s" % d)
    known = {n for n in names for l in EXPECTED_LAYERS if n.startswith(_gerber_name(l))} | set(DRILL_FILES)
    bad += ["unexpected file %s" % n for n in names if n not in known]
    ext = edge_extent(gdir)
    if ext is None:
        bad.append("Edge.Cuts has no coordinates")
    else:
        w, h = ext[2] - ext[0], ext[3] - ext[1]
        if abs(w - BOARD_W_MM) > EDGE_TOL_MM or abs(h - BOARD_H_MM) > EDGE_TOL_MM:
            bad.append("Edge.Cuts extent %.3f x %.3f mm, outline is %.1f x %.1f" % (w, h, BOARD_W_MM, BOARD_H_MM))
    return not bad, "%d files, edge %s" % (len(names), "%.2f x %.2f mm" % (ext[2] - ext[0], ext[3] - ext[1]) if ext else "?"), bad


def _board_holes(board):
    pth = sum(1 for t in board.GetTracks() if t.Type() == pcbnew.PCB_VIA_T)
    npth = 0
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.GetDrillSize().x <= 0:
                continue
            if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                npth += 1
            elif p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                pth += 1
    return {"reva-PTH.drl": pth, "reva-NPTH.drl": npth}


def check_drill(s, _a=None, _b=None):
    gdir = os.path.join(s.out, "gerbers")
    files = {} if getattr(s, "drill_missing", False) else \
        {d: open(os.path.join(gdir, d), encoding="utf-8").read()
         for d in DRILL_FILES if os.path.exists(os.path.join(gdir, d))}
    if not files:
        return False, DRILL_EMPTY, []
    want = _board_holes(s.board)
    bad, parts = [], []
    for d in DRILL_FILES:
        n = len(_HOLE_RE.findall(files.get(d, "")))
        parts.append("%s %d" % (d, n))
        if n != want[d]:
            bad.append("%s holes: file %d, board %d" % (d.split("-")[1].split(".")[0], n, want[d]))
    return not bad, ", ".join(parts), bad


def _read_csv(path):
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8", newline="") as fh:
        return list(csv.DictReader(fh))


def _bom_refs(rows):
    return {r.strip() for row in rows for r in row["Designator"].split(",") if r.strip()}


def _placeable(board):
    """Board footprints JLC places: SMD, not DNP, not excluded from position files."""
    return {fp.GetReference(): fp for fp in board.GetFootprints()
            if fp.GetAttributes() & pcbnew.FP_SMD and not fp.IsDNP() and not fp.IsExcludedFromPosFiles()}


def check_cpl(s, _a=None, _b=None):
    rows = [] if getattr(s, "cpl_missing", False) else _read_csv(os.path.join(s.out, "cpl-jlc.csv"))
    if not rows:
        return False, CPL_EMPTY, []
    bom = _bom_refs(_read_csv(os.path.join(s.out, "bom-jlc.csv")))
    board = _placeable(s.board)
    cpl = {r["Designator"]: r for r in rows}
    bad = []
    for ref in sorted(set(board) | bom | set(cpl)):
        where = [n for n, src in (("the board", board), ("the BOM", bom), ("the CPL", cpl)) if ref in src]
        if len(where) != 3:
            bad.append("%s: in %s, not in %s" % (ref, " and ".join(where),
                       " and ".join(n for n in ("the board", "the BOM", "the CPL") if n not in where)))
    ext = edge_extent(os.path.join(s.out, "gerbers"))
    for ref, r in sorted(cpl.items()):
        x, y = float(r["Mid X"]), float(r["Mid Y"])
        if r["Layer"] != "Bottom":
            bad.append("%s: layer %s, every placed part is on the bottom" % (ref, r["Layer"]))
        fp = board.get(ref)
        if fp is not None:
            bx, by = pcbnew.ToMM(fp.GetPosition().x), pcbnew.ToMM(fp.GetPosition().y)
            if abs(x - bx) > POS_TOL_MM or abs(y + by) > POS_TOL_MM:   # KiCad's CPL negates y (spec §3)
                bad.append("%s: CPL position %.3f, %.3f, board %.3f, %.3f" % (ref, x, y, bx, -by))
        if ext and not (ext[0] <= x <= ext[2] and ext[1] <= y <= ext[3]):
            bad.append("%s: CPL point %.3f, %.3f outside the Edge.Cuts extent" % (ref, x, y))
    return not bad, "%d CPL rows, %d BOM designators, %d placeable on the board" % (len(cpl), len(bom), len(board)), bad


def check_rot_table(s, _a=None, _b=None):
    packages = set() if getattr(s, "rot_missing", False) else \
        {str(fp.GetFPID().GetLibItemName()) for fp in _placeable(s.board).values()}
    if not packages:
        return False, ROT_EMPTY, []
    bad = ["no ROT_FIX entry for %s" % p for p in sorted(packages) if p not in s.rot_fix]
    if s.bottom_sign.get("sign") not in (1, -1):
        bad.append("BOTTOM_SIGN is %r, must be 1 or -1" % s.bottom_sign.get("sign"))
    unverified = sorted(p for p in packages if p in s.rot_fix and not s.rot_fix[p]["verified"])
    line = "%d packages, %d unverified%s" % (len(packages), len(unverified),
                                             "" if s.bottom_sign.get("verified") else ", BOTTOM_SIGN unverified")
    return not bad, line, bad


def check_bom_lcsc(s, _a=None, _b=None):
    rows = [] if getattr(s, "bom_missing", False) else _read_csv(os.path.join(s.out, "bom-jlc.csv"))
    if not rows:
        return False, BOM_EMPTY, []
    bad, n = [], 0
    for row in rows:
        for ref in [r.strip() for r in row["Designator"].split(",") if r.strip()]:
            n += 1
            fp = s.board.FindFootprintByReference(ref)
            have = fp.GetFieldText("LCSC") if fp is not None and fp.HasField("LCSC") else None
            if have != row["LCSC"]:
                bad.append("%s: BOM says %s, the footprint's LCSC field %s" % (ref, row["LCSC"], have))
    return not bad, "%d BOM placements against the board's LCSC fields" % n, bad


def check_assembly(s, _a=None, _b=None):
    """Both sheets drawn, each with as many parts as the board has on that
    side (pcbnew's count against gen.assembly's reading), and View.check()
    clean on each: every part labelled, no label on another or on a part."""
    sheets = {} if getattr(s, "assembly_missing", False) else getattr(s, "sheets", {})
    if not sheets or not any(n for n, _p in sheets.values()):
        return False, ASSEMBLY_EMPTY, []
    want = {"front": sum(1 for fp in s.board.GetFootprints() if not fp.IsFlipped()),
            "back": sum(1 for fp in s.board.GetFootprints() if fp.IsFlipped())}
    bad = []
    for name in sorted(want):
        path = os.path.join(s.out, "reva-assembly-%s.svg" % name)
        if name not in sheets or not os.path.exists(path) or os.path.getsize(path) == 0:
            bad.append("%s sheet missing" % name)
            continue
        n, problems = sheets[name]
        if n != want[name]:
            bad.append("%s sheet: %d parts, the board has %d" % (name, n, want[name]))
        bad += ["%s sheet: %s" % (name, p) for p in problems]
    return not bad, "front %s, back %s parts" % (sheets.get("front", ("?",))[0],
                                                  sheets.get("back", ("?",))[0]), bad


STEPS = [("gerber_set", check_gerber_set), ("drill", check_drill), ("cpl", check_cpl),
         ("rot_table", check_rot_table), ("bom_lcsc", check_bom_lcsc),
         ("assembly", check_assembly)]


def run(s):
    # the step functions take (s, _a, _b) to fit check_kit's runner; there is
    # no board-file path or report prefix to hand them here
    return CK.run_steps(STEPS, s, None, None)


# -- sabotages. BOARD_SABOTAGES change the board or the tables before the
# export; the others change the exported files after it. -----------------------

def _sab_gerber_set(s):
    gdir = os.path.join(s.out, "gerbers")
    os.remove(os.path.join(gdir, [n for n in os.listdir(gdir) if n.startswith(_gerber_name("F.Paste"))][0]))


def _sab_gerber_set_missing(s):
    gdir = os.path.join(s.out, "gerbers")
    for n in os.listdir(gdir):
        os.remove(os.path.join(gdir, n))


def _sab_drill(s):
    p = os.path.join(s.out, "gerbers", "reva-PTH.drl")
    txt = open(p, encoding="utf-8", newline="").read()
    first = _HOLE_RE.search(txt)
    end = txt.index("\n", first.start()) + 1
    open(p, "w", encoding="utf-8", newline="").write(txt[:first.start()] + txt[end:])


def _sab_drill_missing(s):
    s.drill_missing = True


def _rewrite_cpl(s, fn):
    p = os.path.join(s.out, "cpl-jlc.csv")
    rows = _read_csv(p)
    rows = fn(rows)
    with open(p, "w", encoding="utf-8", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["Designator", "Mid X", "Mid Y", "Layer", "Rotation"], lineterminator="\n")
        w.writeheader()
        w.writerows(rows)


def _sab_cpl(s):
    _rewrite_cpl(s, lambda rows: [r for r in rows if r["Designator"] != "C1"])


def _sab_cpl_shift(s):
    def shift(rows):
        for r in rows:
            if r["Designator"] == "R1":
                r["Mid X"] = "%.4f" % (float(r["Mid X"]) + 0.1)
        return rows
    _rewrite_cpl(s, shift)


def _sab_cpl_outside(s):
    """R1's Mid Y with its sign flipped: the point leaves the Gerber outline
    (Review Focus 3 -- the one-origin rule needs its own RED)."""
    def flip(rows):
        for r in rows:
            if r["Designator"] == "R1":
                r["Mid Y"] = "%.4f" % -float(r["Mid Y"])
        return rows
    _rewrite_cpl(s, flip)


def _sab_cpl_dnp(s):
    """C_LDO_T loses its DNP flag on the board: the CPL now places it."""
    s.board.FindFootprintByReference("C_LDO_T").SetDNP(False)


def _sab_cpl_missing(s):
    s.cpl_missing = True


def _sab_rot_table(s):
    s.rot_fix.pop(next(k for k in sorted(s.rot_fix) if k.startswith("SOIC")))


def _sab_rot_sign(s):
    s.bottom_sign["sign"] = 0


def _sab_rot_table_missing(s):
    s.rot_missing = True


def _sab_bom_lcsc(s):
    s.board.FindFootprintByReference("C2").SetField("LCSC", "C1")


def _sab_bom_lcsc_missing(s):
    s.bom_missing = True


def _sab_assembly(s):
    """Two labels on the back sheet forced onto one spot (fab.write_sheets
    reads the flag while it draws)."""
    s.assembly_overlap = True


def _sab_assembly_missing(s):
    s.assembly_missing = True


SABOTAGES = {"gerber_set": _sab_gerber_set, "gerber_set_missing": _sab_gerber_set_missing,
             "drill": _sab_drill, "drill_missing": _sab_drill_missing,
             "cpl": _sab_cpl, "cpl_shift": _sab_cpl_shift, "cpl_outside": _sab_cpl_outside,
             "cpl_dnp": _sab_cpl_dnp, "cpl_missing": _sab_cpl_missing,
             "rot_table": _sab_rot_table, "rot_sign": _sab_rot_sign,
             "rot_table_missing": _sab_rot_table_missing,
             "bom_lcsc": _sab_bom_lcsc, "bom_lcsc_missing": _sab_bom_lcsc_missing,
             "assembly": _sab_assembly, "assembly_missing": _sab_assembly_missing}
# the assembly flags must be set before export() draws the sheets
BOARD_SABOTAGES = {"cpl_dnp", "rot_table", "rot_sign", "bom_lcsc",
                   "assembly", "assembly_missing"}
TURNS_RED = {"gerber_set": "gerber_set", "gerber_set_missing": "gerber_set",
             "drill": "drill", "drill_missing": "drill",
             "cpl": "cpl", "cpl_shift": "cpl", "cpl_outside": "cpl", "cpl_dnp": "cpl",
             "cpl_missing": "cpl",
             "rot_table": "rot_table", "rot_sign": "rot_table", "rot_table_missing": "rot_table",
             "bom_lcsc": "bom_lcsc", "bom_lcsc_missing": "bom_lcsc",
             "assembly": "assembly", "assembly_missing": "assembly"}
WHY = {"gerber_set": "missing layer F.Paste", "gerber_set_missing": GERBER_EMPTY,
       "drill": "PTH holes: file", "drill_missing": DRILL_EMPTY,
       "cpl": "C1: in the board and the BOM, not in the CPL",
       "cpl_shift": "R1: CPL position",
       "cpl_outside": "outside the Edge.Cuts extent",
       "cpl_dnp": "C_LDO_T: in the board and the CPL, not in the BOM",
       "cpl_missing": CPL_EMPTY,
       "rot_table": "no ROT_FIX entry for SOIC", "rot_sign": "BOTTOM_SIGN is 0",
       "rot_table_missing": ROT_EMPTY,
       "bom_lcsc": "C2: BOM says", "bom_lcsc_missing": BOM_EMPTY,
       # gen.assembly.View.check()'s own words for an overlap, on the back sheet
       "assembly": "back sheet: two labels overlap", "assembly_missing": ASSEMBLY_EMPTY}


def sabotage(s, name):
    if name not in SABOTAGES:
        raise SystemExit("unknown sabotage %r; known: %s" % (name, ", ".join(sorted(SABOTAGES))))
    SABOTAGES[name](s)
    print("SABOTAGED: %s" % name)
