#!/usr/bin/env python3
"""Rev A order package (P4-3 spec §4.4).

    KIPY hardware/reva/fab.py [--board PCB] [--out DIR] [--sabotage NAME]
                              [--write] [--release] [--release-dir DIR]

Reads the committed board (route.py owns it) and writes to hardware/reva/out/fab/
(gitignored): gerbers/ (11 layers + PTH/NPTH drill, dates normalised),
reva-gerbers.zip, cpl-jlc.csv, bom-jlc.csv (P3's, copied), renders. Runs
fab_check. --write copies renders and assembly sheets to docs/hardware/fab/
when green. --release writes the package to hardware/reva/fab/ and refuses
while a known list is open or a rotation is unverified (release_blockers()).
"""
import argparse
import csv
import os
import re
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
for _p in (HW, HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import pcbnew                      # noqa: E402,F401
from gen import assembly as A      # noqa: E402
from gen import kipcb, ksexp       # noqa: E402
from gen import pcb_proof as PP    # noqa: E402
from place import X0, Y0, X1, Y1   # noqa: E402  the outline, P4-1 spec §2.1

COMMITTED = os.path.join(HERE, "kicad", "reva.kicad_pcb")
BOM = os.path.join(HERE, "bom-jlc.csv")
OUT = os.path.join(HERE, "out", "fab")
RELEASE = os.path.join(HERE, "fab")
DOCS = os.path.normpath(os.path.join(HW, "..", "docs", "hardware", "fab"))
LAYERS = ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu", "F.Mask", "B.Mask", "F.Silkscreen",
          "B.Silkscreen", "F.Paste", "B.Paste", "Edge.Cuts")
FIXED_DATE = "2000-01-01T00:00:00+00:00"
FIXED_DATE_SP = "2000-01-01 00:00:00"
ZIP_TIME = (2000, 1, 1, 0, 0, 0)

# JLC's part library does not always share KiCad's zero orientation (spec
# §4.4.3). "deg" is added to KiCad's rotation; "verified" stays None until
# Bastian has looked at JLC's placement preview (a quote upload, no order) and
# then carries that date. No machine can prove a rotation. The six package
# names are the ones kicad-cli's position file printed on 2026-10-02.
ROT_FIX = {
    "C_0603_1608Metric": {"deg": 0, "verified": None},
    "C_0805_2012Metric": {"deg": 0, "verified": None},
    "D_SMA": {"deg": 0, "verified": None},
    "R_0603_1608Metric": {"deg": 0, "verified": None},
    "SOIC-16_3.9x9.9mm_P1.27mm": {"deg": 0, "verified": None},
    "SOT-223-3_TabPin2": {"deg": 0, "verified": None},
}
# Bottom-side parts: rotation = (sign * KiCad rotation + deg) % 360. A sign
# error depends on each part's own rotation, so it is verified on its own.
BOTTOM_SIGN = {"sign": 1, "verified": None}

_DATE_RES = [
    (re.compile(r"(%TF\.CreationDate,)[^*]*(\*%)"), r"\g<1>" + FIXED_DATE + r"\g<2>"),
    (re.compile(r"(G04 #@! TF\.CreationDate,)[^*]*(\*)"), r"\g<1>" + FIXED_DATE + r"\g<2>"),
    (re.compile(r"(G04 Created by KiCad \([^)]*\) date )[^*]*(\*)"), r"\g<1>" + FIXED_DATE_SP + r"\g<2>"),
    (re.compile(r"(; DRILL file .* date )\S+"), r"\g<1>" + FIXED_DATE),
    (re.compile(r"(; #@! TF\.CreationDate,)\S+"), r"\g<1>" + FIXED_DATE),
]


def normalise(text):
    """Every date field kicad-cli 10.0.5 writes (probed 2026-10-02, spec §3)
    replaced by one constant, so two exports are byte-identical."""
    for rx, rep in _DATE_RES:
        text = rx.sub(rep, text)
    return text


class Fab:
    def __init__(self):
        self.board_path = None
        self.board = None
        self.out = None
        self.rot_fix = {k: dict(v) for k, v in ROT_FIX.items()}
        self.bottom_sign = dict(BOTTOM_SIGN)


def _cli(*args):
    r = subprocess.run([ksexp.KICAD_CLI] + list(args), capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if r.returncode:
        raise RuntimeError("kicad-cli %s rc=%d: %s" % (args[0:3], r.returncode, (r.stdout + r.stderr)[-300:]))


def prepare(board_src, out, sabotage=""):
    import fab_check as FC
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, "board"))
    s = Fab()
    s.out = out
    s.board = kipcb.load(board_src)
    if sabotage in FC.BOARD_SABOTAGES:
        FC.sabotage(s, sabotage)
    s.board_path = os.path.join(out, "board", "reva.kicad_pcb")
    kipcb.save(s.board, s.board_path)
    return s


def _export_gerbers(s):
    gdir = os.path.join(s.out, "gerbers")
    os.makedirs(gdir, exist_ok=True)
    _cli("pcb", "export", "gerbers", "--layers", ",".join(LAYERS), "-o", gdir, s.board_path)
    _cli("pcb", "export", "drill", "--excellon-separate-th", "-o", gdir + os.sep, s.board_path)
    for n in sorted(os.listdir(gdir)):
        p = os.path.join(gdir, n)
        if n.endswith(".gbrjob"):
            os.remove(p)              # the job file is not part of the package
            continue
        with open(p, encoding="utf-8", newline="") as fh:
            txt = fh.read()
        with open(p, "w", encoding="utf-8", newline="") as fh:
            fh.write(normalise(txt))


def _export_cpl(s):
    tmp = os.path.join(s.out, "board", "pos.csv")
    _cli("pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both",
         "--smd-only", "--exclude-dnp", "-o", tmp, s.board_path)
    with open(tmp, encoding="utf-8", newline="") as fh:
        rows = list(csv.DictReader(fh))
    out = []
    for r in rows:
        fix = s.rot_fix.get(r["Package"], {"deg": 0})
        sign = s.bottom_sign["sign"] if r["Side"] == "bottom" else 1
        rot = (sign * float(r["Rot"]) + fix["deg"]) % 360
        out.append({"Designator": r["Ref"], "Mid X": "%.4f" % float(r["PosX"]),
                    "Mid Y": "%.4f" % float(r["PosY"]),
                    "Layer": "Bottom" if r["Side"] == "bottom" else "Top",
                    "Rotation": "%g" % rot})
    with open(os.path.join(s.out, "cpl-jlc.csv"), "w", encoding="utf-8", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["Designator", "Mid X", "Mid Y", "Layer", "Rotation"],
                           lineterminator="\n")
        w.writeheader()
        w.writerows(out)


def _zip(s):
    gdir = os.path.join(s.out, "gerbers")
    with zipfile.ZipFile(os.path.join(s.out, "reva-gerbers.zip"), "w", zipfile.ZIP_DEFLATED) as z:
        for n in sorted(os.listdir(gdir)):
            info = zipfile.ZipInfo(n, date_time=ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            with open(os.path.join(gdir, n), "rb") as fh:
                z.writestr(info, fh.read())


# -- assembly sheets (spec §4.4.5) ------------------------------------------------

# px per mm. 4.0 already passes View.check(), but its leaders tangle: back 8
# leader pairs crossing (C_SENSE2's ran across R1's, R2's and C_SENSE3's beside
# U_SR1) and 9 leaders forced across a part, front 6 and 26. At 6.0: back 0
# and 3, front 3 and 5 (measured 2026-10-02 on the committed board).
SHEET_SCALE = 6.0
SHEET_MARGIN = 135     # px around the board, room for leaders (the coupon's value)
STYLE = {"pot": ("#ddd2be", "#6b5f44"), "jack": ("#ded7c9", "#4a463c"),
         "led": ("#bde294", "#4a7c2b"), "key": ("#e8dcbd", "#8a7430"),
         "res": ("#f2ceae", "#b96532"), "cap": ("#cbe3f0", "#0f6e99"),
         "diode": ("#ccd4da", "#5a6a75"), "chip": ("#3a3a34", A.INK),
         "reg": ("#3a3a34", A.INK), "module": ("#e4ded1", "#4a463c"),
         "conn": ("#ded7c9", "#4a463c"), "probe": ("#ecd07a", "#8a6d18"),
         "dnp": (A.PAPER, A.ALERT)}
_CLASSES = (("Potentiometer", "pot"), ("PJ398", "jack"), ("LED", "led"), ("SW_Push", "key"),
            ("SOIC", "chip"), ("SOT-223", "reg"), ("DAISY", "module"), ("IDC", "conn"),
            ("TestPoint", "probe"), ("D_SMA", "diode"), ("R_0", "res"), ("C_0", "cap"),
            ("CP_", "cap"))
BACK_BIG = frozenset({"U_SM", "J_PWR"})     # carry their name inside their outline
# The back sheet's enlarged window, in the back view's mm (x mirrored). At
# SHEET_SCALE three leaders around J_PWR run under another part -- C5's under
# D_P12, C3's under J_PWR and U_SR3, C1's under C_LDO_T -- so C5's line seems
# to end on D_P12; C2, C3 and C5 have no silk reference (silk.NO_ROOM), the
# sheet is their only name. The window labels them again at DETAIL_SCALE.
# x0 = 214 keeps R23 (ends at 213.5) out: a part cut by the frame gets its
# label on the frame line.
BACK_DETAIL = (214.0, 50.0, 250.0, 78.0)
DETAIL_SCALE = 15.0


def classify(value, footprint):
    """The Rev A part classes of the sheets, from the land pattern's name."""
    fam = footprint.split(":")[-1].upper()
    for key, cls in _CLASSES:
        if key.upper() in fam:
            return cls
    return "conn"


def _mirror(p):
    """A back part as seen from the back: every x mirrored about the board's
    centre. gen.assembly.read_board stores pad positions absolute in board mm
    ("x", "y", already rotated); "lx"/"ly" are footprint-local and unused by
    the drawing, so they stay as read."""
    c = X0 + X1
    q = dict(p)
    q["x"], q["x0"], q["x1"] = c - p["x"], c - p["x1"], c - p["x0"]
    q["pads"] = [dict(pd, x=c - pd["x"]) for pd in p["pads"]]
    return q


def _mark_pin1(out, v, parts):
    """A filled square on pad 1 of every LED (KiCad's LED_D3.0mm: pad 1 is the
    cathode, Device:LED pin 1 "K") and of J_PWR (pin 1, the -12 V stripe).
    View.draw's own cathode bar looks for a net ending in _K, which Rev A's
    LEDs do not have (their pad 1 is on GND), so this square is the mark."""
    for p in parts:
        if p.get("cls") != "led" and p.get("ref") != "J_PWR":
            continue
        for pd in p["pads"]:
            if pd["n"] == "1" and v.mx0 <= pd["x"] <= v.mx1 and v.my0 <= pd["y"] <= v.my1:
                x, y = v.px(pd["x"], pd["y"])
                out.append('<rect x="%.1f" y="%.1f" width="6" height="6" fill="%s"/>'
                           % (x - 3, y - 3, A.ALERT))


# View.draw writes the coupon's one-line U_SM note, sized for 13 px/mm; at
# SHEET_SCALE it runs far out of U_SM's outline across its neighbours. The
# sheet writes the same facts (bom-hand.csv's J_SM1..4 line) wrapped instead.
USM_NOTE = ("Daisy Patch SM on four 2&#215;5 sockets",
            "J_SM1..J_SM4, each cut from a 2&#215;10 strip:",
            "solder the sockets, not the module")


def _write_big(out, v):
    """The names inside the BACK_BIG outlines, in place of View.draw's: U_SM
    with USM_NOTE wrapped, J_PWR with its two lines stacked clear of the
    pin-1 square (draw centres them, and the square lands on "Eurorack")."""
    for q in v.here:
        if q["ref"] not in v.big:
            continue
        cx, cy = (q["px0"] + q["px1"]) / 2, (q["py0"] + q["py1"]) / 2
        if q["ref"] == "U_SM":
            out.append('<text x="%g" y="%g" font-size="19" fill="%s" text-anchor="middle">'
                       'U_SM</text>' % (cx, cy - 16, A.INK))
            for i, line in enumerate(USM_NOTE):
                out.append('<text x="%g" y="%g" font-size="9.5" fill="%s" text-anchor="middle">'
                           '%s</text>' % (cx, cy + 6 + 14 * i, A.MUTED, line))
        elif q["ref"] == "J_PWR":
            pin1 = next(pd for pd in q["pads"] if pd["n"] == "1")
            _x, sy = v.px(pin1["x"], pin1["y"])
            if sy > cy:                 # square in the lower half: both lines above it
                value_y = sy - 3 - 3 - A.DESCENDER * v.font
            else:                       # upper half: both lines below it
                value_y = sy + 3 + 3 + A.ASCENDER * (v.font + 2.5) + 14
            name_y = value_y - 14
            out.append('<text x="%g" y="%g" font-size="%g" fill="%s" text-anchor="middle">'
                       '%s</text>' % (cx, name_y, v.font + 2.5, A.INK, q["ref"]))
            out.append('<text x="%g" y="%g" font-size="%g" fill="%s" text-anchor="middle">'
                       '%s</text>' % (cx, value_y, v.font, A.INK, q["value"]))


def write_sheets(s):
    """reva-assembly-front.svg (panel parts by panel id) and
    reva-assembly-back.svg (seen from the back, by reference, DNP dashed);
    s.sheets = {name: (parts on the sheet, View.check() problems)}."""
    parts, _size = A.read_board(s.board_path, classify)
    front = [dict(p, ref=p["value"]) for p in parts if p["side"] == "F"]   # panel ids
    back = [_mirror(p) for p in parts if p["side"] == "B"]
    s.sheets = {}
    w, h = (X1 - X0) * SHEET_SCALE, (Y1 - Y0) * SHEET_SCALE
    # the note and legend sit below the label margin, where no label can stand
    foot = int(h + 2 * SHEET_MARGIN)
    for name, sheet_parts, big, detail, note, legend in (
            ("front", front, frozenset(), None,
             "FRONT: panel parts, labelled with their panel id; LED cathode marked",
             "red square = LED cathode: pad 1 of LED_D3.0mm, the flat side of the body; "
             "dot = pad 1 of the other parts"),
            ("back", back, BACK_BIG, BACK_DETAIL,
             "BACK, seen from the back: JLC fits the SMD parts; hand-solder U_SM's sockets and J_PWR",
             "red square = J_PWR pin 1, -12 V (the stripe of the ribbon); dot = pad 1, on the SOICs "
             "the notch end; red dashed outline = DNP: leave the footprint empty")):
        out = []
        dv = None
        height = foot + 70
        if detail:
            dy = foot + 100 + SHEET_MARGIN
            height = int(dy + (detail[3] - detail[1]) * DETAIL_SCALE + SHEET_MARGIN)
        A.open_svg(out, int(w + 2 * SHEET_MARGIN), height)
        v = A.View(sheet_parts, SHEET_MARGIN, SHEET_MARGIN, SHEET_SCALE, margin=SHEET_MARGIN,
                   bounds=(X0, Y0, X1, Y1), big=big, style=STYLE)
        v.place_labels()
        if getattr(s, "assembly_overlap", False) and name == "back" and len(v.label_boxes) > 1:
            v.label_boxes[1] = v.label_boxes[0]
        problems = v.check()
        if detail:
            dv = A.View(sheet_parts, SHEET_MARGIN, dy, DETAIL_SCALE, region=detail, font=11,
                        margin=SHEET_MARGIN, big=big, style=STYLE)
            dv.place_labels()
            problems += ["detail: %s" % p for p in dv.check()]
        s.sheets[name] = (len(v.here), problems)
        views = [v] + ([dv] if dv else [])
        for view in views:
            # the big parts stay unlabelled by the search, but draw() must not
            # write their names: _write_big writes them to fit the scale
            view.big = frozenset()
            view.draw(out, moat=False, grid=10 if view is v else 5)
            view.big = big
            _write_big(out, view)
            _mark_pin1(out, view, sheet_parts)
        A.scale_bar(out, SHEET_MARGIN, foot + 8, SHEET_SCALE)
        out.append('<text x="%d" y="%d" font-size="14" fill="%s">%s</text>'
                   % (SHEET_MARGIN, foot + 34, A.INK, note))
        out.append('<text x="%d" y="%d" font-size="11" fill="%s">%s</text>'
                   % (SHEET_MARGIN, foot + 54, A.MUTED, legend))
        if dv:
            fx, fy = v.px(detail[0], detail[1])
            fw, fh = (detail[2] - detail[0]) * SHEET_SCALE, (detail[3] - detail[1]) * SHEET_SCALE
            for ax, ay, sx, sy in ((fx, fy, 1, 1), (fx + fw, fy, -1, 1),
                                   (fx, fy + fh, 1, -1), (fx + fw, fy + fh, -1, -1)):
                out.append('<path d="M %g %g L %g %g M %g %g L %g %g" fill="none" stroke="%s" '
                           'stroke-width="2.2"/>'
                           % (ax, ay + sy * 18, ax, ay, ax, ay, ax + sx * 18, ay, A.ALERT))
            out.append('<text x="%d" y="%d" font-size="14" fill="%s">DETAIL (red corners above), '
                       'x %g..%g mm of this view = board x %g..%g mm, y %g..%g mm: J_PWR\'s '
                       'neighbours, among them C2, C3 and C5, which carry no reference on the '
                       'board silk</text>'
                       % (SHEET_MARGIN, dy - SHEET_MARGIN - 10, A.INK, detail[0], detail[2],
                          X0 + X1 - detail[2], X0 + X1 - detail[0], detail[1], detail[3]))
        out.append("</svg>")
        with open(os.path.join(s.out, "reva-assembly-%s.svg" % name), "w",
                  encoding="utf-8", newline="\n") as fh:
            fh.write("\n".join(out) + "\n")


def export(s, renders=True):
    _export_gerbers(s)
    _export_cpl(s)
    shutil.copyfile(BOM, os.path.join(s.out, "bom-jlc.csv"))
    _zip(s)
    write_sheets(s)
    if renders:
        for side in ("top", "bottom"):
            png = os.path.join(s.out, "reva-%s.png" % side)
            rc, msg = PP.render(s.board_path, png, side)
            if rc or not os.path.exists(png):
                raise RuntimeError("render %s failed rc=%s: %s" % (side, rc, msg[-300:]))


def release_blockers(place_known=None, route_known=None, rot_fix=None, bottom_sign=None):
    import place_check as PC
    import route_check as RC
    pk = PC.KNOWN_PANEL if place_known is None else place_known
    rk = RC.KNOWN_PANEL if route_known is None else route_known
    rf = ROT_FIX if rot_fix is None else rot_fix
    bs = BOTTOM_SIGN if bottom_sign is None else bottom_sign
    out = ["place_check KNOWN_PANEL[%s]: %s" % (k, e) for k, v in sorted(pk.items()) for e in sorted(v)]
    out += ["route_check KNOWN_PANEL[%s]: %s" % (k, e) for k, v in sorted(rk.items()) for e in sorted(v)]
    out += ["ROT_FIX %s unverified" % k for k, v in sorted(rf.items()) if not v.get("verified")]
    if not rf:
        out.append("ROT_FIX is empty")
    if not bs.get("verified"):
        out.append("BOTTOM_SIGN unverified")
    return out


def main(argv=None):
    import fab_check as FC
    ap = argparse.ArgumentParser()
    ap.add_argument("--board", default=COMMITTED)
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--sabotage", default="")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--release", action="store_true")
    ap.add_argument("--release-dir", default=RELEASE)
    a = ap.parse_args(argv)
    s = prepare(a.board, a.out, a.sabotage)
    export(s, renders=not a.sabotage)
    if a.sabotage and a.sabotage not in FC.BOARD_SABOTAGES:
        FC.sabotage(s, a.sabotage)
    green = FC.run(s)
    blockers = release_blockers()
    print("order_ready: %s" % ("yes" if not blockers else "no, %d open items" % len(blockers)))
    for b in blockers:
        print("        " + b)
    # spec §4.4.6: informational only, the assembly sheet covers them; not a
    # blocker and not in the open-item count (release_blockers() stays clean)
    import silk as SK
    for ref, why in sorted(SK.NO_ROOM.items()):
        print("        NO_ROOM %s (informational: the assembly sheet covers it) -- %s" % (ref, why))
    if a.write and green and not a.sabotage:
        os.makedirs(DOCS, exist_ok=True)
        for n in sorted(os.listdir(a.out)):
            if n.endswith((".png", ".svg")):
                shutil.copyfile(os.path.join(a.out, n), os.path.join(DOCS, n))
        print("renders and sheets copied to", os.path.relpath(DOCS))
    if a.release:
        if not green or blockers or a.sabotage:
            print("not released: %s" % ("the run is RED" if not green else "%d open items" % len(blockers)))
            return 1
        if os.path.isdir(a.release_dir):
            shutil.rmtree(a.release_dir)
        shutil.copytree(a.out, a.release_dir, ignore=shutil.ignore_patterns("board"))
        print("released to", os.path.relpath(a.release_dir))
    print("GREEN" if green else "RED")
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
