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
from gen import kipcb, ksexp       # noqa: E402
from gen import pcb_proof as PP    # noqa: E402

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


def export(s, renders=True):
    _export_gerbers(s)
    _export_cpl(s)
    shutil.copyfile(BOM, os.path.join(s.out, "bom-jlc.csv"))
    _zip(s)
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
