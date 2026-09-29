#!/usr/bin/env python3
"""Cut file, print sheet and hole list for the Rev A P1 acrylic plate
(spec docs/superpowers/specs/2026-09-29-rev-a-p1-panel-parts-design.md §3).

Every coordinate comes from gen_hw_panel.py; this file adds hole sizes only
through hw.HOLE_D and places nothing. P4 (the Rev A board) places its parts
from FireflowHW-holes.json, so plate and board share one source.

Run from the repo root:  python host/vcv/res/gen_hw_cut.py
"""
import json, os
import gen_panel as gp
import gen_hw_panel as hw

KIND = {"G": "pot", "S": "pot", "J": "jack", "P": "key", "L": "led"}

# Real Eurorack panels are cut a little under HP x 5.08 so neighbours fit
# (Doepfer A-100: 20 HP = 101.30 of 101.60 mm). ASSUMPTION for 60 HP: 0.4 mm
# under, trimmed evenly off both sides so no control moves.
PLATE_W = hw.W - 0.4
TRIM = (hw.W - PLATE_W) / 2
# Mounting slots: DIY convention, 7.5 mm in from the plate's left edge and
# 3.0 mm from top and bottom, horizontal pitch a whole number of HP.
# ASSUMPTION until checked against the Palette's rails.
MOUNT_X = (TRIM + 7.5, TRIM + 7.5 + (hw.HP - 3) * gp.MM_PER_HP)
MOUNT_Y = (3.0, hw.Hh - 3.0)
MOUNT_W, MOUNT_H = 5.2, 3.2


def _r3(v):
    return round(v, 3)


def holes():
    """One hole per distinct control position, then the SD slot and the four
    mounting slots. ATTACK_x and STAGES_x share a knob, hence one hole."""
    out, seen = [], set()
    for c in hw.ALL_HW:
        key = (_r3(c.x), _r3(c.y))
        if key in seen:
            continue
        seen.add(key)
        cls = hw.hw_class(c.enum)
        out.append({"id": c.enum, "kind": KIND[cls], "x_mm": _r3(c.x),
                    "y_mm": _r3(c.y), "d_mm": hw.HOLE_D[cls]})
    out.append({"id": "SD", "kind": "sd", "x_mm": _r3(hw.SD_X),
                "y_mm": _r3(hw.SD_Y), "w_mm": hw.SD_W, "h_mm": hw.SD_H})
    n = 0
    for y in MOUNT_Y:
        for x in MOUNT_X:
            n += 1
            out.append({"id": f"MOUNT{n}", "kind": "mount", "x_mm": _r3(x),
                        "y_mm": _r3(y), "w_mm": MOUNT_W, "h_mm": MOUNT_H})
    return out


def holes_json():
    doc = {"generated_by": "host/vcv/res/gen_hw_cut.py -- do not edit by hand",
           "units": "mm, origin top-left of the nominal 60 HP plate, y down",
           "plate": {"x0_mm": _r3(TRIM), "w_mm": _r3(PLATE_W), "h_mm": hw.Hh},
           "holes": holes()}
    return json.dumps(doc, indent=1) + "\n"


BLUE = "#0000ff"   # Formulor: RGB 0,0,255 hairlines are cut


def shape(h, stroke, width):
    """One hole as one SVG element: a circle, or a rectangle whose rounded
    ends make the mounting slots stadium-shaped."""
    if "d_mm" in h:
        return (f'<circle cx="{hw.mm(h["x_mm"])}" cy="{hw.mm(h["y_mm"])}" '
                f'r="{hw.mm(h["d_mm"] / 2)}" fill="none" stroke="{stroke}" '
                f'stroke-width="{width}"/>')
    rx = h["h_mm"] / 2 if h["kind"] == "mount" else 0.0
    return (f'<rect x="{hw.mm(h["x_mm"] - h["w_mm"] / 2)}" '
            f'y="{hw.mm(h["y_mm"] - h["h_mm"] / 2)}" width="{hw.mm(h["w_mm"])}" '
            f'height="{hw.mm(h["h_mm"])}" rx="{hw.mm(rx)}" fill="none" '
            f'stroke="{stroke}" stroke-width="{width}"/>')


def cut_svg():
    P = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{hw.mm(hw.W)}mm" '
         f'height="{hw.mm(hw.Hh)}mm" viewBox="0 0 {hw.mm(hw.W)} {hw.mm(hw.Hh)}">',
         f'<rect x="{hw.mm(TRIM)}" y="0.000" width="{hw.mm(PLATE_W)}" '
         f'height="{hw.mm(hw.Hh)}" fill="none" stroke="{BLUE}" stroke-width="0.01"/>']
    P += [shape(h, BLUE, "0.01") for h in holes()]
    P.append("</svg>")
    return "\n".join(P) + "\n"


OUTPUTS = [("FireflowHW-holes.json", holes_json),
           ("FireflowHW-cut.svg", cut_svg)]


def write_all(here):
    written = []
    for name, fn in OUTPUTS:
        path = os.path.join(here, name)
        hw._write_atomic(path, fn())
        written.append(path)
    return written


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    for p in write_all(here):
        print("wrote", os.path.relpath(p))
    print(f"holes={len(holes())} plate={PLATE_W:.1f}x{hw.Hh}mm")
