#!/usr/bin/env python3
"""Writes the J_SD footprint: Yamaichi PJS008U-3000-0, vertical microSD,
through hole (P4-1 spec §4.2).

Source of every number: the EasyEDA footprint CONN-TH_PJS008U-3000-0 that
LCSC publishes for C3177022, read 2026-09-29 from
https://easyeda.com/api/products/C3177022/components -- a secondary source;
the manufacturer drawing could not be read while planning. A socket in hand
checks it before the order. EasyEDA units are 10 mil; origin (4001.57,
3009.594), y down. The footprint origin here is the body-box centre.

    python hardware/reva/sd_footprint.py      # writes the committed file
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = "SD_Yamaichi_PJS008U-3000-0"
PATH = os.path.normpath(os.path.join(HERE, "..", "lib", "FireFlow", "FireFlow.pretty",
                                     NAME + ".kicad_mod"))
U = 0.254                            # one EasyEDA unit in mm
ORIGIN = (4001.57, 3009.594)
# (number, x, y, diameter, hole radius) in EasyEDA units, verbatim.
PADS = [("1", 3986.41, 3005.52, 4.724, 1.378), ("2", 3990.74, 3001.1879, 4.724, 1.378),
        ("3", 3995.07, 3005.52, 4.724, 1.378), ("4", 3999.40, 3001.1879, 4.724, 1.378),
        ("5", 4003.74, 3005.52, 4.724, 1.378), ("6", 4008.07, 3001.1879, 4.724, 1.378),
        ("7", 4012.40, 3005.52, 4.724, 1.378), ("8", 4016.73, 3001.1879, 4.724, 1.378),
        ("SH", 3984.64, 3018.00, 5.512, 1.7717), ("SH", 4018.50, 3018.00, 5.512, 1.7717)]
BBOX = (3976.3, 2998.6, 50.5, 22.3)  # x, y, width, height
COURTYARD_MARGIN = 0.25              # KiCad library convention for connectors
HEIGHT_MM = 14.18                    # LCSC C3177022 "Height Above Board"


def _mm(v):
    return round(v * U, 3)


def geometry():
    """(pads [(number, x, y, dia, drill)], body box) in mm, centred on the body."""
    l, t = (BBOX[0] - ORIGIN[0]) * U, (BBOX[1] - ORIGIN[1]) * U
    r, b = l + BBOX[2] * U, t + BBOX[3] * U
    cx, cy = (l + r) / 2.0, (t + b) / 2.0
    pads = [(n, round(_mm(x - ORIGIN[0]) - cx, 3), round(_mm(y - ORIGIN[1]) - cy, 3),
             round(d * U, 2), round(2 * hr * U, 2)) for n, x, y, d, hr in PADS]
    hw, hh = round((r - l) / 2.0, 3), round((b - t) / 2.0, 3)
    return pads, (-hw, -hh, hw, hh)


def text():
    pads, (l, t, r, b) = geometry()
    m = COURTYARD_MARGIN
    lines = [
        '(footprint "%s"' % NAME,
        '\t(version 20241229)',
        '\t(generator "fireflow_sd_footprint")',
        '\t(layer "F.Cu")',
        '\t(descr "Yamaichi PJS008U-3000-0 vertical microSD, THT; from the LCSC/EasyEDA '
        'footprint of C3177022 (secondary source); height %.2f mm")' % HEIGHT_MM,
        '\t(attr through_hole)',
        '\t(fp_text reference "REF**" (at 0 %.2f 0) (layer "F.SilkS") '
        '(effects (font (size 1 1) (thickness 0.15))))' % (t - 1.2),
        '\t(fp_text value "%s" (at 0 %.2f 0) (layer "F.Fab") '
        '(effects (font (size 1 1) (thickness 0.15))))' % (NAME, b + 1.2),
        '\t(fp_rect (start %.3f %.3f) (end %.3f %.3f) (stroke (width 0.1) (type solid)) '
        '(fill none) (layer "F.Fab"))' % (l, t, r, b),
        '\t(fp_rect (start %.3f %.3f) (end %.3f %.3f) (stroke (width 0.05) (type solid)) '
        '(fill none) (layer "F.CrtYd"))' % (l - m, t - m, r + m, b + m),
    ]
    for n, x, y, d, drill in pads:
        lines.append('\t(pad "%s" thru_hole circle (at %.3f %.3f) (size %.2f %.2f) '
                     '(drill %.2f) (layers "*.Cu" "*.Mask"))' % (n, x, y, d, d, drill))
    lines.append(')')
    return "\n".join(lines) + "\n"


def write(path=PATH):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text())


if __name__ == "__main__":
    write()
    print("wrote", os.path.relpath(PATH))
