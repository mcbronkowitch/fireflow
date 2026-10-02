#!/usr/bin/env python3
"""Draw the coupon's assembly plan -- the sheet you stuff the board from.

The fabricated board's own silkscreen cannot be read. 0805 parts sit on a 2 mm
pitch and their reference designators need about 5 mm, so the printing overlaps
itself; in the analog corner the names are one grey smear. `proof/` already has
board renders, and they have the same problem for the same reason. You cannot
solder a board you cannot tell apart, so the plan is drawn instead of printed.

Two sheets come out of one pass over the board file:

  proof/coupon-assembly.svg   the working sheet -- whole board, the analog
                              corner enlarged, and a stuffing list with one row
                              per reel. All 78 parts named.
  proof/coupon-overview.svg   the same geometry at a width that survives a
                              narrow column, with eight numbered zones instead
                              of 78 labels. This is the one the journal prints.

**This script reads `coupon.kicad_pcb` and nothing else, so unlike `build.py`,
`build_pcb.py` and `review.py` it runs under the system interpreter** -- no
`pcbnew`, no symbol libraries, no KiCad install. Placement, rotation, courtyard
and pad geometry come from the footprints; the part VALUES come from each
footprint's own `Value` property, which is what the BOM in `proof/review.md`
also reports. Nothing here is transcribed by hand, so the plan cannot drift
away from the board the way a hand-kept table would: re-run it after any
placement change and the drawing follows.

Every label is placed by search. A candidate slot is rejected if it touches a
part body, another label, a leader already drawn, or the board outline; the
nearest surviving slot wins, and a label standing off from its part gets a
leader that may not cross another part unless nothing legal is left. The text
metrics that search runs on (`ADVANCE`, `ASCENDER`, `DESCENDER` in `gen/assembly.py`) were
measured in a browser against the rendered monospace stack rather than guessed,
because a label box that is wrong by a descender puts leaders under glyphs.

Usage:

    python scripts/assembly_plan.py
    python scripts/assembly_plan.py --out-dir DIR --prefix NAME

The second form is how the website's copies are refreshed, since it wants the
files under a different name:

    python scripts/assembly_plan.py \\
        --out-dir ../../../FireFlow_Website/public/media/site \\
        --prefix fireflow-hw-coupon
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..")))
from gen.assembly import (  # noqa: E402  (moved to hardware/gen for Rev A, P4-3 spec §4.4.5)
    ALERT, BOARD, INK, MUTED, PAPER, RULE,
    View, open_svg, read_board, scale_bar)

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.normpath(os.path.join(HERE, "..", "coupon.kicad_pcb"))
DEFAULT_OUT = os.path.normpath(os.path.join(HERE, "..", "proof"))

# fill, stroke. Keyed by the class classify() puts a footprint in, not by the
# raw Value string: "10k" is both a resistor and a pot, and they are not the
# same thing to a hand holding a reel.
STYLE = {
    "0R":      ("#ccd4da", "#5a6a75"),
    "1k":      ("#f2ceae", "#b96532"),
    "10k":     ("#c0ded4", "#1d6f5f"),
    "100n":    ("#cbe3f0", "#0f6e99"),
    "10u":     ("#9dc2da", "#0b5273"),
    "dnp":     (PAPER,     ALERT),
    "led":     ("#bde294", "#4a7c2b"),
    "probe":   ("#ecd07a", "#8a6d18"),
    "link":    ("#e8dcbd", "#8a7430"),
    "pot10k":  ("#ddd2be", "#6b5f44"),
    "pot20k":  ("#cfc3ab", "#6b5f44"),
    "chip":    ("#3a3a34", INK),
    "module":  ("#e4ded1", "#4a463c"),
    "conn":    ("#ded7c9", "#4a463c"),
}

# Order and wording of the stuffing list, and of the overview's colour key.
CLASS_ORDER = ["0R", "1k", "10k", "100n", "10u", "led", "dnp", "probe", "link",
               "pot10k", "pot20k", "chip", "conn", "module"]
CLASS_NAME = {
    "0R": "0 R", "1k": "1 k", "10k": "10 k", "100n": "100 n", "10u": "10 &#181;F 25 V",
    "led": "green LED", "dnp": "DNP &#8212; fit nothing", "probe": "test point",
    "link": "solder jumper", "pot10k": "10 k pot", "pot20k": "20 k pot",
    "chip": "SOIC chip", "conn": "connector / switch", "module": "the module",
}

# Parts with room to carry their own name inside the outline.
BIG = {"U_SM", "U_SR1", "U_SR2", "U_IN1", "U_MUX16", "U_MUX8",
       "J_PWR", "J_AUDIO", "SW1", "RV1", "RV2", "RV3", "RV4", "RV5", "RV6", "RV7"}


def classify(value, footprint):
    """Which reel a footprint comes off, from its Value and its land pattern."""
    family = footprint.split(":")[-1]
    if "Potentiometer" in family:
        return "pot20k" if value.startswith("20") else "pot10k"
    if "SOIC" in family:
        return "chip"
    if "DAISY" in family.upper():
        return "module"
    if "SolderJumper" in family:
        return "link"
    if "TestPoint" in family:
        return "probe"
    if "LED" in family:
        return "led"
    if value == "DNP":
        return "dnp"
    if value.startswith("10u"):
        return "10u"
    if value in ("0R", "1k", "10k", "100n"):
        return value
    return "conn"


def write_assembly(parts, path):
    """The working sheet: whole board, analog corner enlarged, stuffing list."""
    width = 1600
    scale, ox, oy = 13.0, 150, 175
    detail, dscale, dx = (58.0, 45.5, 100.0, 80.0), 25.0, 230
    dy = int(oy + 80.0 * scale + 160)
    list_y = int(dy + (detail[3] - detail[1]) * dscale + 130)
    height = list_y + 400

    full = View(parts, ox, oy, scale, big=BIG, style=STYLE)
    full.place_labels()
    corner = View(parts, dx, dy, dscale, detail, font=11, margin=200, big=BIG, style=STYLE)
    corner.place_labels()

    problems = full.check() + corner.check()

    out = []
    open_svg(out, width, height)
    out.append('<text x="40" y="44" font-size="20" fill="%s" letter-spacing="0.04em">'
               'FIREFLOW TEST COUPON &#183; ASSEMBLY PLAN</text>' % INK)
    out.append('<text x="40" y="66" font-size="12" fill="%s">Top view, component side &#183; '
               '100 &#215; 80 mm &#183; every part sits where the board file puts it</text>' % MUTED)
    out.append('<text x="%d" y="44" font-size="11.5" fill="%s" text-anchor="end">%d parts, all '
               'on the front copper</text>' % (width - 40, MUTED, len(parts)))
    out.append('<text x="%d" y="66" font-size="11.5" fill="%s" text-anchor="end">geometry read '
               'from hardware/coupon/coupon.kicad_pcb</text>' % (width - 40, MUTED))
    out.append('<line x1="40" y1="84" x2="%d" y2="84" stroke="%s"/>' % (width - 40, RULE))
    out.append('<circle cx="46" cy="108" r="2.6" fill="%s"/>' % INK)
    out.append('<text x="58" y="112" font-size="11" fill="%s">= pad 1, at its measured position '
               '&#8212; on the SOIC chips that is the notch end.</text>' % INK)
    out.append('<line x1="660" y1="102" x2="660" y2="114" stroke="#1d3d0f" stroke-width="2.6"/>')
    out.append('<text x="672" y="112" font-size="11" fill="%s">= LED cathode (the _K pad).</text>' % INK)
    out.append('<text x="930" y="112" font-size="11" fill="%s">C_COM16 and C_COM8 are DNP: fit '
               'the footprint, leave it empty.</text>' % ALERT)

    out.append('<text x="%d" y="%d" font-size="13" fill="%s" letter-spacing="0.1em">'
               '1 &#183; THE WHOLE BOARD</text>' % (ox, oy - 18, INK))
    full.draw(out)

    fx, fy = ox + detail[0] * scale, oy + detail[1] * scale
    fw, fh = (detail[2] - detail[0]) * scale, (detail[3] - detail[1]) * scale
    for ax, ay, sx, sy in ((fx, fy, 1, 1), (fx + fw, fy, -1, 1),
                           (fx, fy + fh, 1, -1), (fx + fw, fy + fh, -1, -1)):
        out.append('<path d="M %g %g L %g %g M %g %g L %g %g" fill="none" stroke="%s" '
                   'stroke-width="2.2"/>'
                   % (ax, ay + sy * 30, ax, ay, ax, ay, ax + sx * 30, ay, ALERT))
    out.append('<text x="%g" y="%g" font-size="11" fill="%s" text-anchor="end">detail 2</text>'
               % (fx - 8, fy + 14, ALERT))
    scale_bar(out, ox, oy + 80.0 * scale + 26, scale)

    out.append('<text x="%d" y="%d" font-size="13" fill="%s" letter-spacing="0.1em">2 &#183; THE '
               'ANALOG CORNER, ENLARGED &#8212; x %g..%g mm, y %g..%g mm of the same board</text>'
               % (dx, dy - 18, INK, detail[0], detail[2], detail[1], detail[3]))
    corner.draw(out, grid=5)

    out.append('<line x1="40" y1="%d" x2="%d" y2="%d" stroke="%s"/>'
               % (list_y - 40, width - 40, list_y - 40, RULE))
    out.append('<text x="40" y="%d" font-size="13" fill="%s" letter-spacing="0.1em">3 &#183; WHAT '
               'GOES WHERE &#8212; one row per reel</text>' % (list_y - 14, INK))

    groups = {}
    for p in parts:
        groups.setdefault(p["cls"], []).append(p["ref"])
    columns = [40, 40 + (width - 80) // 2]
    per_column = (len(CLASS_ORDER) + 1) // 2
    for i, cls in enumerate(CLASS_ORDER):
        refs = sorted(groups.get(cls, []))
        if not refs:
            continue
        cx = columns[i // per_column]
        cy = list_y + 10 + (i % per_column) * 34
        fill, stroke = STYLE[cls]
        dash = ' stroke-dasharray="3 2"' if cls == "dnp" else ""
        out.append('<rect x="%d" y="%d" width="16" height="11" rx="1.5" fill="%s" stroke="%s" '
                   'stroke-width="1.1"%s/>' % (cx, cy - 9, fill, stroke, dash))
        out.append('<text x="%d" y="%d" font-size="11" fill="%s">%s</text>'
                   % (cx + 24, cy, INK, CLASS_NAME[cls]))
        out.append('<text x="%d" y="%d" font-size="10" fill="%s">&#215;%d</text>'
                   % (cx + 150, cy, MUTED, len(refs)))
        lines, cur = [], ""
        for ref in refs:
            if len(cur) + len(ref) + 2 > 92:
                lines.append(cur)
                cur = ref
            else:
                cur = (cur + "  " + ref) if cur else ref
        if cur:
            lines.append(cur)
        for n, line in enumerate(lines[:2]):
            out.append('<text x="%d" y="%d" font-size="9.5" fill="%s">%s</text>'
                       % (cx + 188, cy + n * 12, MUTED, line))

    out.append('</svg>')
    write(path, out)
    return problems, full.crossings + corner.crossings


# The overview's numbered zones: number, marker x/y in px, and the point in
# board mm the leader lands on.
ZONES = [
    (1, "left", 20.0, 8.0, 18.0, "the shift registers and the eight LEDs"),
    (2, "top", 40.0, 46.0, 26.0, "the Daisy module &#8212; it sits on four 2&#215;5 sockets"),
    (3, "top", 86.0, 90.0, 5.0, "the button and the ADC probe points"),
    (4, "right", 26.0, 95.5, 28.0, "Eurorack power in, bulk caps beside it"),
    (5, "left", 48.1, 30.0, 48.1, "the 1.0 mm moat &#8212; digital above, analog below"),
    (6, "left", 60.0, 3.0, 56.0, "the 3.5 mm audio jack"),
    (7, "bottom", 36.0, 39.5, 68.0, "seven pots, 10 k and 20 k mixed"),
    (8, "right", 66.0, 88.0, 66.0, "both mux chips, and most of the small parts"),
]


def write_overview(parts, path):
    """The column-width version: same board, eight numbered zones, colour key."""
    width, height = 760, 852
    scale, ox, oy = 6.4, 60, 55
    bw, bh = 100.0 * scale, 80.0 * scale

    out = []
    open_svg(out, width, height)
    out.append('<rect x="%g" y="%g" width="%g" height="%g" rx="5" fill="%s" stroke="%s" '
               'stroke-width="1.6"/>' % (ox, oy, bw, bh, BOARD, INK))
    for mm in range(10, 100, 10):
        out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="#ded7c6" stroke-width="0.6"/>'
                   % (ox + mm * scale, oy, ox + mm * scale, oy + bh))
    for mm in range(10, 80, 10):
        out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="#ded7c6" stroke-width="0.6"/>'
                   % (ox, oy + mm * scale, ox + bw, oy + mm * scale))
    out.append('<rect x="%g" y="%g" width="%g" height="%g" fill="#d3c7ab"/>'
               % (ox, oy + 47.6 * scale, bw, scale))

    for p in sorted(parts, key=lambda q: -(q["x1"] - q["x0"]) * (q["y1"] - q["y0"])):
        fill, stroke = STYLE[p["cls"]]
        dash = ' stroke-dasharray="3 2"' if p["cls"] == "dnp" else ""
        out.append('<rect x="%g" y="%g" width="%g" height="%g" rx="1" fill="%s" stroke="%s" '
                   'stroke-width="0.8"%s/>'
                   % (ox + p["x0"] * scale, oy + p["y0"] * scale,
                      (p["x1"] - p["x0"]) * scale, (p["y1"] - p["y0"]) * scale,
                      fill, stroke, dash))

    markers = []
    for num, side, along, tx, ty, _text in ZONES:
        if side == "left":
            mx, my = 32, oy + along * scale
        elif side == "right":
            mx, my = width - 32, oy + along * scale
        elif side == "top":
            mx, my = ox + along * scale, 30
        else:
            mx, my = ox + along * scale, oy + bh + 18
        markers.append((num, mx, my, ox + tx * scale, oy + ty * scale))

    for _num, mx, my, px, py in markers:
        dx, dy = px - mx, py - my
        dist = max(math.hypot(dx, dy), 1e-6)
        # Start on the circle's edge, so the line never runs under its digit.
        out.append('<line x1="%g" y1="%g" x2="%g" y2="%g" stroke="%s" stroke-width="1"/>'
                   % (mx + dx / dist * 12, my + dy / dist * 12, px, py, ALERT))
    for num, mx, my, _px, _py in markers:
        out.append('<circle cx="%g" cy="%g" r="11" fill="%s" stroke="%s" stroke-width="1.6"/>'
                   % (mx, my, PAPER, ALERT))
        out.append('<text x="%g" y="%g" font-size="12" fill="%s" text-anchor="middle">%d</text>'
                   % (mx, my + 4.2, ALERT, num))

    scale_bar(out, ox, oy + bh + 22, scale)

    for i, zone in enumerate(ZONES):
        cx = 30 if i < 4 else 396
        cy = 638 + (i % 4) * 21
        out.append('<text x="%d" y="%d" font-size="11.5" fill="%s">%d  %s</text>'
                   % (cx, cy, INK, zone[0], zone[5]))

    out.append('<line x1="30" y1="740" x2="730" y2="740" stroke="%s"/>' % RULE)
    out.append('<text x="30" y="762" font-size="10.5" fill="%s" letter-spacing="0.08em">'
               'THE COLOUR IS THE VALUE</text>' % MUTED)
    # Every class the board actually carries gets a swatch: the two pot values
    # are different reels and the drawing already tells them apart by shade, so
    # a key that merged them would be lying about what the picture shows.
    key = ["0R", "1k", "10k", "100n", "10u", "led",
           "probe", "pot10k", "pot20k", "chip", "link", "dnp"]
    short = dict(CLASS_NAME, led="LED", probe="test point", pot10k="10 k pot",
                 pot20k="20 k pot", chip="chip", link="jumper", dnp="fit nothing",
                 **{"10u": "10 &#181;F"})
    for i, cls in enumerate(key):
        cx = 30 + (i % 6) * 118
        cy = 792 + (i // 6) * 26
        fill, stroke = STYLE[cls]
        dash = ' stroke-dasharray="3 2"' if cls == "dnp" else ""
        out.append('<rect x="%d" y="%d" width="15" height="10" rx="1" fill="%s" stroke="%s" '
                   'stroke-width="1"%s/>' % (cx, cy - 8, fill, stroke, dash))
        out.append('<text x="%d" y="%d" font-size="11" fill="%s">%s</text>'
                   % (cx + 22, cy, INK, short[cls]))

    out.append('</svg>')
    write(path, out)


def write(path, out):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(out) + "\n")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out-dir", default=DEFAULT_OUT,
                    help="where to write the two SVGs (default: hardware/coupon/proof)")
    ap.add_argument("--prefix", default="coupon",
                    help="basename prefix, so the website's copies can keep their own names")
    args = ap.parse_args(argv)

    parts, (board_w, board_h) = read_board(PCB, classify)
    print("read %s: %d parts on a %g x %g mm board"
          % (os.path.relpath(PCB), len(parts), board_w, board_h))

    assembly = os.path.join(args.out_dir, "%s-assembly.svg" % args.prefix)
    overview = os.path.join(args.out_dir, "%s-overview.svg" % args.prefix)

    problems, crossings = write_assembly(parts, assembly)
    write_overview(parts, overview)

    print("wrote %s" % os.path.relpath(assembly))
    print("wrote %s" % os.path.relpath(overview))
    if crossings:
        print("%d label(s) had no clean slot; their leader crosses a part" % crossings)
    if problems:
        for line in problems:
            print("  PROBLEM: %s" % line)
        return 1
    print("label check: no overlaps, nothing on a part or the board edge")
    return 0


if __name__ == "__main__":
    sys.exit(main())
