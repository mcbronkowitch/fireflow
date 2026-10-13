#!/usr/bin/env python3
"""Front Panel Designer files for the Rev A aluminium plate (P5, Schaeffer
AG): a hole DXF and the artwork for the UV print, both in plate coordinates
(304.4 x 128.5 mm, the trimmed plate of gen_hw_cut.py).

Every position comes from gen_hw_cut.holes(); this file places nothing. Only
the round-hole diameters differ from the acrylic: a laser cut runs ~0.1-0.2 mm
over size, a drilled hole does not, so the metal plate gets its own
clearances (ALU_D, Bastian 2026-10-13). The mounting slots are the acrylic's,
which fit the rails.

Front Panel Designer: a 304.4 x 128.5 mm, 2 mm black anodised panel; import
the DXF's HOLES and SLOTS layers (PLATE_OUTLINE is reference only); place
FireflowHW-alu-print.pdf at -1/-1 (it carries 1 mm bleed); switch the white
underprint on.

The print covers the whole plate (Bastian 2026-10-13): the field tints are
a few levels off the plate colour and would not read against bare anodise.

Run from the repo root:  python host/vcv/res/gen_hw_alu.py
Needs fontTools and PyMuPDF (fitz).
"""
import html, os, re
import gen_hw_panel as hw
import gen_hw_cut as cut

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_PATH = os.path.join(HERE, "..", "..", "..", "third_party", "sharetechmono",
                         "ShareTechMono-Regular.ttf")

# Drilled clearances by hole kind, mm. Keys keep the acrylic's 6.2 (the cap
# passes through; nothing threads into the plate there).
ALU_D = {"pot": 7.2, "jack": 6.2, "led": 3.2, "key": hw.HOLE_D["P"]}

TRIM, PW, PH = cut.TRIM, cut.PLATE_W, hw.Hh


def holes():
    """gen_hw_cut's holes with the drilled diameters."""
    out = []
    for h in cut.holes():
        h = dict(h)
        if "d_mm" in h:
            h["d_mm"] = ALU_D[h["kind"]]
        out.append(h)
    return out


# =============================================================================
#  DXF -- R12, mm, origin at the plate's bottom-left corner, y up
# =============================================================================
def _x(x): return x - TRIM
def _y(y): return PH - y


def _ent(kind, layer, *pairs):
    out = ["0", kind, "8", layer]
    for code, val in pairs:
        out += [str(code), f"{val:.4f}"]
    return out


def _line(layer, x1, y1, x2, y2):
    return _ent("LINE", layer, (10, x1), (20, y1), (30, 0.0),
                (11, x2), (21, y2), (31, 0.0))


def _arc(layer, cx, cy, r, a0, a1):
    return _ent("ARC", layer, (10, cx), (20, cy), (30, 0.0), (40, r),
                (50, a0), (51, a1))


def _rect(layer, cx, cy, w, h, r):
    """A rounded rectangle as lines and quarter arcs; r == h/2 makes the
    mounting slots' stadium."""
    x0, x1, y0, y1 = cx - w / 2, cx + w / 2, cy - h / 2, cy + h / 2
    e = []
    if w - 2 * r > 1e-9:
        e += _line(layer, x0 + r, y0, x1 - r, y0) + _line(layer, x0 + r, y1, x1 - r, y1)
    if h - 2 * r > 1e-9:
        e += _line(layer, x0, y0 + r, x0, y1 - r) + _line(layer, x1, y0 + r, x1, y1 - r)
    if r > 0:
        e += (_arc(layer, x1 - r, y1 - r, r, 0.0, 90.0)
              + _arc(layer, x0 + r, y1 - r, r, 90.0, 180.0)
              + _arc(layer, x0 + r, y0 + r, r, 180.0, 270.0)
              + _arc(layer, x1 - r, y0 + r, r, 270.0, 360.0))
    return e


LAYERS = (("HOLES", 5), ("SLOTS", 1), ("PLATE_OUTLINE", 8))


def dxf():
    E = []
    for h in holes():
        x, y = _x(h["x_mm"]), _y(h["y_mm"])
        if "d_mm" in h:
            E += _ent("CIRCLE", "HOLES", (10, x), (20, y), (30, 0.0), (40, h["d_mm"] / 2))
        else:
            r = h["h_mm"] / 2 if h["kind"] == "mount" else 0.0
            E += _rect("SLOTS", x, y, h["w_mm"], h["h_mm"], r)
    E += _rect("PLATE_OUTLINE", PW / 2, PH / 2, PW, PH, 0.0)
    head = ["0", "SECTION", "2", "HEADER", "9", "$ACADVER", "1", "AC1009",
            "9", "$INSUNITS", "70", "4", "9", "$MEASUREMENT", "70", "1",
            "0", "ENDSEC"]
    tables = ["0", "SECTION", "2", "TABLES", "0", "TABLE", "2", "LAYER",
              "70", str(len(LAYERS))]
    for name, colour in LAYERS:
        tables += ["0", "LAYER", "2", name, "70", "0", "62", str(colour),
                   "6", "CONTINUOUS"]
    tables += ["0", "ENDTAB", "0", "ENDSEC"]
    body = ["0", "SECTION", "2", "ENTITIES"] + E + ["0", "ENDSEC", "0", "EOF"]
    return "\r\n".join(head + tables + body) + "\r\n"


# =============================================================================
#  Print artwork -- ink only, none of the Rack furniture (bodies, rings, LEDs)
# =============================================================================
def _rgb(c): return tuple(int(c[i:i + 2], 16) for i in (1, 3, 5))
def _mix(a, b, t): return tuple(x * (1 - t) + y * t for x, y in zip(a, b))
def _hex(c): return "#" + "".join(f"{round(v):02x}" for v in c)


# The print is opaque ink, so each field's two washes are flattened into one
# colour against the plate colour the print lays down: the design's ramp,
# taken at its middle.
PLATE_MID = _mix(_rgb(hw.PLATE_HI), _rgb(hw.PLATE_LO), 0.5)


def field_ink(side):
    c = _mix(PLATE_MID, (255, 255, 255), hw.FIELD_BASE_OPACITY)
    return _hex(_mix(c, _rgb(hw.ACC[side]), hw.FIELD_ACC_OPACITY[side]))


class Outliner:
    """Text as filled glyph outlines in Share Tech Mono, the face Rack draws
    the panel lettering with -- the print shop then needs no font."""

    def __init__(self, path):
        from fontTools.ttLib import TTFont
        self.font = TTFont(path)
        self.glyphs = self.font.getGlyphSet()
        self.cmap = self.font.getBestCmap()
        self.upm = self.font["head"].unitsPerEm
        self.hmtx = self.font["hmtx"]

    def path(self, text, x, y, size, anchor, spacing):
        from fontTools.pens.svgPathPen import SVGPathPen
        from fontTools.pens.transformPen import TransformPen
        s = size / self.upm
        names = [self.cmap[ord(ch)] for ch in text]
        adv = [self.hmtx[n][0] * s for n in names]
        # Letter-spacing between glyphs only, none after the last one, so a
        # spaced legend centres on its own letters.
        width = sum(adv) + spacing * (len(names) - 1)
        x0 = x - {"start": 0.0, "middle": width / 2, "end": width}[anchor]
        out = []
        for n, a in zip(names, adv):
            pen = SVGPathPen(self.glyphs)
            self.glyphs[n].draw(TransformPen(pen, (s, 0, 0, -s, x0, y)))
            out.append(pen.getCommands())
            x0 += a + spacing
        return " ".join(c for c in out if c)


TEXT_RE = re.compile(r"<text ([^>]*)>([^<]*)</text>")


# The plate colour runs to the plate edge, so it bleeds past it on every
# side; the page is that much larger and is placed at -BLEED/-BLEED.
BLEED = 1.0


def print_svg(font):
    """The artwork on the plate colour (solid: the design's ramp is 4 levels
    deep and not printable), on a page the trimmed plate plus BLEED."""
    bl = BLEED
    P = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{PW + 2 * bl:.3f}mm" '
         f'height="{PH + 2 * bl:.3f}mm" '
         f'viewBox="{TRIM - bl:.3f} {-bl:.3f} {PW + 2 * bl:.3f} {PH + 2 * bl:.3f}">',
         f'<rect x="{-bl:.3f}" y="{-bl:.3f}" width="{hw.W + 2 * bl:.3f}" '
         f'height="{PH + 2 * bl:.3f}" fill="{_hex(PLATE_MID)}"/>']
    for b in hw.BOXES:
        P.append(f'<path d="{hw._field_d(b)}" fill="{field_ink(b.side)}"/>')
    for attrs, txt in TEXT_RE.findall(hw.svg()):
        a = dict(re.findall(r'([\w-]+)="([^"]*)"', attrs))
        d = font.path(html.unescape(txt), float(a["x"]), float(a["y"]),
                      float(a["font-size"]), a.get("text-anchor", "start"),
                      float(a.get("letter-spacing", 0)))
        P.append(f'<path d="{d}" fill="{a["fill"]}"/>')
    P.append("</svg>")
    return "\n".join(P) + "\n"


def pdf(svg_text):
    import fitz
    src = fitz.open("svg", svg_text.encode())
    doc = fitz.open("pdf", src.convert_to_pdf())
    doc.set_metadata({})
    return doc.tobytes(garbage=4, deflate=True, no_new_id=True)


def outputs():
    font = Outliner(FONT_PATH)
    return [("FireflowHW-alu-holes.dxf", dxf().encode("ascii")),
            ("FireflowHW-alu-print.pdf", pdf(print_svg(font)))]


def write_all(here):
    written = []
    for name, data in outputs():
        path = os.path.join(here, name)
        with open(path, "wb") as f:
            f.write(data)
        written.append(path)
    return written


if __name__ == "__main__":
    for p in write_all(HERE):
        print("wrote", os.path.relpath(p))
    hs = holes()
    print(f"holes={len(hs)} plate={PW:.1f}x{PH}mm "
          f"inks A={field_ink('A')} B={field_ink('B')} C={field_ink('C')}")
