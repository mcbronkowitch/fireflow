#!/usr/bin/env python3
"""Single source of truth for the FireFlow Glow VCV panel (Simple Touch 2).

16 HP, laid out on the reviewed control centres and named physical 10+2 pad
geometry in res/touch2_geometry.py: P00-P09 on the lower touch board, P10/P11
on the upper rear PCB, six trim knobs, two faders, two switches and one stereo
out. No CV inputs -- the board has none.

This is a VCV panel. It is NOT the faceplate draft -- see touch2_geometry.py
for why the millimetres here are good enough for Rack and not for a router.

Palette is shared with the big Fireflow panel (gen_panel.py) so the two modules
read as one instrument. Layout is not shared.

Emits (both committed):
  - res/Glow.svg                     the faceplate
  - src/generated_flow_panel.hpp     enums + control/text tables

Run from host/vcv/:  python3 res/gen_flow_panel.py
The C++ never hardcodes a coordinate, label or colour -- it reads them from the
generated header, so graphics and widget placement can never drift apart.
"""
import math
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_panel as base
import touch2_geometry as geo
from touch2_geometry import PADS

HP = 16
W  = HP * base.MM_PER_HP          # 81.28 mm
Hh = geo.PLATE_H                  # 128.5 mm

# --- dark copper surface ------------------------------------------------------
PANEL_TOP       = "#20221d"
PANEL_MID       = "#171814"
PANEL_BOTTOM    = "#10110f"
PANEL_BORDER    = "#5f594d"
PANEL_TEXT      = "#d7d1c5"
PAD_FILL        = "#171812"
PAD_COPPER_DIM  = "#7d4a30"
PAD_TOPO        = "#633c29"
PAD_COPPER      = base.COPPER
PAD_GREEN       = base.GREEN
PAD_REFUSED     = base.BRICK
PAD_GLOW_WIDTH  = 1.60
PAD_STROKE_W    = 0.55
PAD_INNER_SCALE = 0.82
GRAIN = tuple((((i * 37) % 79) + 1.0,
               ((i * 53) % 123) + 2.0,
               0.10 if i % 3 else 0.16) for i in range(36))

# --- printed footprints -------------------------------------------------------
# Widget classes are chosen in Glow.cpp; these are what the PLATE prints, and
# what the collision guard measures. Glow.cpp documents each widget against the
# figure here, the way it already does for the macro knobs.
#
# Only the footprints are ours. Every CENTRE outside the pad field comes from
# touch2_geometry, and a footprint that will not fit between two measured
# centres gets smaller -- those centres never move.
#
# KNOB_R is the one footprint that is NOT ours: the board prints a gold collar
# around each knob and Task 2 measured it, so the plate prints that and not a
# round number. It used to read 4.5 -- a 9 mm cap on a board whose own collar
# is 7.8 mm across -- and Glow.cpp then chose its widget against the 9. Both
# now follow geo.KNOB_COLLAR_R: 3.885 mm radius, and Rack's RoundSmallBlackKnob
# (7.68 mm) is the stock cap nearest to it.
KNOB_R    = geo.KNOB_COLLAR_R     # measured silkscreen collar, 7.77 mm across
FADER_W   = 6.8                   # VCVSlider is 6.72 mm wide
FADER_H   = geo.FADER_TRAVEL
SWITCH_W  = 5.0                   # CKSSThree is 4.56 x 9.60 mm
SWITCH_H  = 9.0
JACK_R    = 4.2

# Printed panel-outline width.
HAIRLINE_W = 0.28

LOGO_Y = 10.0
LOGO_SZ = 4.2
LOGO_GAP = 1.1                    # between the two anchors, see TEXTS
GLYPH_ADV = 0.60                  # Share Tech Mono advance, in em
# --- control kinds ------------------------------------------------------------
MACRO  = "MACRO"
PAD    = "PAD"
FADER  = "FADER"
SWITCH = "SWITCH"
OUT    = "OUT"

FOOTPRINT = {
    MACRO:  (KNOB_R * 2, KNOB_R * 2),
    FADER:  (FADER_W, FADER_H),
    SWITCH: (SWITCH_W, SWITCH_H),
    OUT:    (JACK_R * 2, JACK_R * 2),
}
# Jack captions print BELOW the jack: above it they would land on the masthead
# rule at y = 8.65. Physical pad IDs remain metadata and are not printed.
LBL_DY = {MACRO: 0.0, PAD: 0.0, FADER: 0.0, SWITCH: 0.0, OUT: 5.6}
LBL_SZ = {MACRO: 2.2, PAD: 2.6, FADER: 2.2, SWITCH: 2.2, OUT: 2.2}
WKMAP  = {MACRO: "WK_MACRO", PAD: "WK_PAD", FADER: "WK_FADER",
          SWITCH: "WK_SWITCH", OUT: "WK_OUT"}

# Knob POSITION -> index into PARAMS, i.e. which macro sits on which knob.
# The six macros keep flow_ids.h's enum order in PARAMS (Glow.cpp indexes
# params[MOTION + m] and six static_asserts pin it); re-sorting the panel is a
# change to this table alone.
KNOB_MACRO = [0, 1, 2, 3, 4, 5]   # S31 S32 S33 S34, then S30, S35
assert sorted(KNOB_MACRO) == list(range(6)), \
    "KNOB_MACRO must be a permutation of the six macros; a duplicate would "\
    "leave a hole in PARAMS and every downstream guard would crash instead of "\
    "reporting"


class Ctl(object):
    def __init__(self, enum, kind, x, y, label, tip):
        self.enum, self.kind = enum, kind
        self.x, self.y = x, y
        self.label, self.tip = label, tip


class Txt(object):
    def __init__(self, x, y, size, rgb, anchor, s, weight=None):
        self.x, self.y, self.size = x, y, size
        self.rgb, self.anchor, self.str = rgb, anchor, s
        self.weight = weight


def footprint_of(c):
    if c.kind == PAD:
        index = int(c.enum.split("_")[1]) - 1
        bounds = PAD_SHAPES[index].curve_bounds
        return (bounds.max_x - bounds.min_x, bounds.max_y - bounds.min_y)
    return FOOTPRINT[c.kind]


def label_xy(c):
    """Baseline position of a control's caption."""
    if c.kind == PAD:
        index = int(c.enum.split("_")[1]) - 1
        return PAD_SHAPES[index].label
    return (c.x, c.y + LBL_DY[c.kind])


# --- named physical pad geometry ---------------------------------------------
def _polygon_centroid(points):
    cross = [x0*y1 - x1*y0
             for (x0, y0), (x1, y1) in zip(points, points[1:] + points[:1])]
    scale = 1.0 / (3.0 * sum(cross))
    x = sum((p0[0] + p1[0]) * c
            for p0, p1, c in zip(points, points[1:] + points[:1], cross))
    y = sum((p0[1] + p1[1]) * c
            for p0, p1, c in zip(points, points[1:] + points[:1], cross))
    return (x * scale, y * scale)


class PadShape(object):
    def __init__(self, physical):
        self.pad_id = physical.pad_id
        self.zone = physical.zone
        self.points = physical.points_mm
        self.label = physical.label_anchor_mm
        self.centre = _polygon_centroid(self.points)
        self.verified = physical.verified
        self.source_note = physical.source_note
        self.curve_bounds = None
        self.bounds = None


PAD_SHAPES = tuple(PadShape(pad) for pad in PADS)
assert len({shape.pad_id for shape in PAD_SHAPES}) == 12


def catmull_rom_cubics(points):
    n, out = len(points), []
    for i, p1 in enumerate(points):
        p0, p2, p3 = points[(i-1)%n], points[(i+1)%n], points[(i+2)%n]
        c1 = (p1[0] + (p2[0]-p0[0])/6.0, p1[1] + (p2[1]-p0[1])/6.0)
        c2 = (p2[0] - (p3[0]-p1[0])/6.0, p2[1] - (p3[1]-p1[1])/6.0)
        out.append((c1, c2, p2))
    return out


def _cubic_point(p0, c1, c2, p3, t):
    u = 1.0 - t
    return (u*u*u*p0[0] + 3*u*u*t*c1[0] + 3*u*t*t*c2[0] + t*t*t*p3[0],
            u*u*u*p0[1] + 3*u*u*t*c1[1] + 3*u*t*t*c2[1] + t*t*t*p3[1])


def _cubic_extrema(a, b, c, d):
    """Return closed-interval extrema parameters for one Bezier coordinate."""
    qa = 3.0 * (-a + 3.0*b - 3.0*c + d)
    qb = 6.0 * (a - 2.0*b + c)
    qc = 3.0 * (b - a)
    roots = [0.0, 1.0]
    eps = 1e-9
    if abs(qa) < eps:
        if abs(qb) >= eps:
            roots.append(-qc / qb)
    else:
        disc = qb*qb - 4.0*qa*qc
        if disc >= 0.0:
            root = math.sqrt(disc)
            roots.extend(((-qb - root) / (2.0*qa),
                          (-qb + root) / (2.0*qa)))
    return [t for t in roots if 0.0 <= t <= 1.0]


def _pad_curve_bounds(shape):
    points = []
    for i, (c1, c2, p3) in enumerate(catmull_rom_cubics(shape.points)):
        p0 = shape.points[i]
        ts = _cubic_extrema(p0[0], c1[0], c2[0], p3[0])
        ts += _cubic_extrema(p0[1], c1[1], c2[1], p3[1])
        points.extend(_cubic_point(p0, c1, c2, p3, t) for t in ts)
    xs, ys = [p[0] for p in points], [p[1] for p in points]
    return geo.Bounds(min(xs), min(ys), max(xs), max(ys))


def _runtime_pad_bounds(shape):
    """Bounds emitted to Rack: cubic contour plus the outer glow radius.

    The values are rounded away from the contour so the three-decimal header
    cannot shorten a widget enough to clip its NanoVG stroke.
    """
    x0, y0, x1, y1 = _pad_curve_bounds(shape)
    halo = PAD_GLOW_WIDTH / 2.0
    return geo.Bounds(math.floor((x0 - halo) * 1000.0) / 1000.0,
                      math.floor((y0 - halo) * 1000.0) / 1000.0,
                      math.ceil((x1 + halo) * 1000.0) / 1000.0,
                      math.ceil((y1 + halo) * 1000.0) / 1000.0)


for _shape in PAD_SHAPES:
    _shape.curve_bounds = _pad_curve_bounds(_shape)
    _shape.bounds = _runtime_pad_bounds(_shape)
assert all(s.bounds.min_x <= s.centre[0] <= s.bounds.max_x and
           s.bounds.min_y <= s.centre[1] <= s.bounds.max_y for s in PAD_SHAPES)


def sample_closed_pad(shape, samples_per_segment=20):
    points = []
    for i, (c1, c2, p2) in enumerate(catmull_rom_cubics(shape.points)):
        p1 = shape.points[i]
        for sample in range(samples_per_segment):
            t = sample / float(samples_per_segment)
            u = 1.0 - t
            points.append(_cubic_point(p1, c1, c2, p2, t))
    return points


# --- the tables ---------------------------------------------------------------
_MACRO_NAMES = ["MOTION", "DENSITY", "BRIGHT", "PACE", "WANDER", "SPACE"]
_MACRO_TIPS = [
    "MOTION -- how much everything moves",
    "DENSITY -- how much happens",
    "BRIGHT -- spectral centre",
    "PACE -- stretched to rhythmic",
    "WANDER -- predictable to wandering",
    "SPACE -- close to vast",
]
# Board channel per knob POSITION, from the TouchFX sketch's ASCII drawing.
_KNOB_CHAN = ["S31", "S32", "S33", "S34", "S30", "S35"]
_FADER_CHAN = ["S36", "S37"]
# Each three-position switch reads as a PAIR of board channels.
_SWITCH_CHAN = ["S09/S10", "S07/S08"]

PARAMS = [None] * 6
for _pos, _macro in enumerate(KNOB_MACRO):
    _x, _y = geo.KNOBS[_pos]
    PARAMS[_macro] = Ctl(_MACRO_NAMES[_macro], MACRO, _x, _y, "",
                         "%s  [%s]" % (_MACRO_TIPS[_macro], _KNOB_CHAN[_pos]))

# Physical P00-P11 identity is load-bearing; musical parameter identity remains
# PAD_1 through PAD_12 so existing patches retain the same parameter IDs.
for _i, _shape in enumerate(PAD_SHAPES):
    _x, _y = _shape.centre
    PARAMS.append(Ctl("PAD_%d" % (_i + 1), PAD, _x, _y, "",
                      "Touch electrode %s -- Place %d. Tap: go there. Hold: "
                      "reroll all six macro domains, the ground stays. Tap "
                      "again: back." % (_shape.pad_id, _i + 1)))

for _i, (_x, _y) in enumerate(geo.FADERS):
    PARAMS.append(Ctl(["FADER_L", "FADER_R"][_i], FADER, _x, _y, "",
                      "Fader %s -- assignable from the context menu"
                      % _FADER_CHAN[_i]))

for _i, (_x, _y) in enumerate(geo.SWITCHES):
    PARAMS.append(Ctl(["SW_L", "SW_R"][_i], SWITCH, _x, _y, "",
                      "Switch %s -- assignable from the context menu"
                      % _SWITCH_CHAN[_i]))

# The board has no inputs. The generator must emit no kInputCtls table for an
# empty list: `static const PanelCtl kInputCtls[] = {};` is a zero-length
# array, which is ill-formed in standard C++.
INPUTS = []

OUTPUTS = [
    Ctl("OUT_L", OUT, geo.JACKS[0][0], geo.JACKS[0][1], "L", "Main out, left"),
    Ctl("OUT_R", OUT, geo.JACKS[1][0], geo.JACKS[1][1], "R", "Main out, right"),
]

TEXTS = [
    Txt(W*.5-2.2, 10.0, 3.2, PAD_COPPER, 2, "FIREFLOW", 500),
    Txt(W*.5,     10.0, 3.2, PANEL_TEXT, 0, "/", 400),
    Txt(W*.5+2.2, 10.0, 3.2, PAD_COPPER, 1, "GLOW", 700),
]


# --- SVG ----------------------------------------------------------------------
def mm(v):
    return "%.3f" % v


def sw(v):
    """A stroke width, printed as typed (0.28, not 0.280)."""
    return "%g" % v


ANCHOR_SVG = {0: "middle", 1: "start", 2: "end"}


def knob_svg(c):
    return (
        '  <circle cx="%s" cy="%s" r="%s" fill="url(#knobCap)" stroke="%s" '
        'stroke-width="%s"/>\n'
        '  <line x1="%s" y1="%s" x2="%s" y2="%s" stroke="%s" '
        'stroke-width="0.45" stroke-linecap="round"/>\n'
        % (mm(c.x), mm(c.y), mm(KNOB_R), base.GRAPHITE, sw(HAIRLINE_W),
           mm(c.x), mm(c.y - KNOB_R * 0.40), mm(c.x), mm(c.y - KNOB_R * 0.82),
           base.WHITE))


def pad_path_svg(shape, scale=1.0):
    """Return a closed SVG cubic path for an island, optionally scaled in-place."""
    cx, cy = shape.centre
    points = tuple((cx + (x - cx) * scale, cy + (y - cy) * scale)
                   for x, y in shape.points)
    out = ["M %s %s" % (mm(points[0][0]), mm(points[0][1]))]
    for c1, c2, p2 in catmull_rom_cubics(points):
        out.append("C %s %s %s %s %s %s" %
                   (mm(c1[0]), mm(c1[1]), mm(c2[0]), mm(c2[1]),
                    mm(p2[0]), mm(p2[1])))
    return " ".join(out) + " Z"


def pad_svg(c):
    index = int(c.enum.split("_")[1]) - 1
    shape = PAD_SHAPES[index]
    path = pad_path_svg(shape)
    out = ['  <path class="touchIsland" data-pad="%s" d="%s" fill="%s" '
           'stroke="%s" stroke-width="%s"/>\n'
           % (shape.pad_id, path, PAD_FILL, PAD_COPPER_DIM, sw(PAD_STROKE_W))]
    for scale in (0.78, 0.58, 0.38):
        out.append('  <path class="touchTopo" d="%s" fill="none" stroke="%s" '
                   'stroke-width="0.18"/>\n'
                   % (pad_path_svg(shape, scale), PAD_TOPO))
    return "".join(out)


def fader_svg(c):
    return (
        '  <rect x="%s" y="%s" width="%s" height="%s" rx="%s" fill="%s" '
        'stroke="%s" stroke-width="%s"/>\n'
        '  <line x1="%s" y1="%s" x2="%s" y2="%s" stroke="%s" '
        'stroke-width="0.6" stroke-linecap="round"/>\n'
        % (mm(c.x - FADER_W / 2.0), mm(c.y - FADER_H / 2.0), mm(FADER_W),
           mm(FADER_H), mm(FADER_W / 2.0), PAD_FILL, PANEL_BORDER,
           sw(HAIRLINE_W),
           mm(c.x), mm(c.y - FADER_H / 2.0 + 1.2),
           mm(c.x), mm(c.y + FADER_H / 2.0 - 1.2), base.WELL))


def switch_svg(c):
    return (
        '  <rect x="%s" y="%s" width="%s" height="%s" rx="1.0" fill="%s" '
        'stroke="%s" stroke-width="%s"/>\n'
        % (mm(c.x - SWITCH_W / 2.0), mm(c.y - SWITCH_H / 2.0), mm(SWITCH_W),
           mm(SWITCH_H), base.GRAPHITE, base.LINE, sw(HAIRLINE_W)))


def jack_svg(c):
    return (
        '  <circle cx="%s" cy="%s" r="%s" fill="%s" stroke="%s" stroke-width="0.30"/>\n'
        '  <circle cx="%s" cy="%s" r="%s" fill="%s"/>\n'
        % (mm(c.x), mm(c.y), mm(JACK_R), base.GRAPHITE, base.LINE,
           mm(c.x), mm(c.y), mm(JACK_R * 0.38), base.WELL))


SVG_FOR = {MACRO: knob_svg, PAD: pad_svg, FADER: fader_svg,
           SWITCH: switch_svg, OUT: jack_svg}


def text_svg(x, y, size, rgb, anchor, s, weight=None):
    if weight is None:
        return ('  <text x="%s" y="%s" font-family="Inter, Helvetica, sans-serif" '
                'font-size="%s" fill="%s" text-anchor="%s">%s</text>\n'
                % (mm(x), mm(y), mm(size), rgb, ANCHOR_SVG[anchor], s))
    return ('  <text x="%s" y="%s" font-family="Inter, Helvetica, sans-serif" '
            'font-size="%s" fill="%s" text-anchor="%s" font-weight="%d">%s</text>\n'
            % (mm(x), mm(y), mm(size), rgb, ANCHOR_SVG[anchor], weight, s))


def svg():
    out = []
    out.append('<?xml version="1.0" encoding="UTF-8"?>\n')
    out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%smm" height="%smm" '
               'viewBox="0 0 %s %s">\n' % (mm(W), mm(Hh), mm(W), mm(Hh)))
    out.append('  <defs>\n')
    out.append('  <radialGradient id="knobCap" cx="38%" cy="32%" r="75%">\n')
    out.append('    <stop offset="0" stop-color="#3a3d35"/>\n')
    out.append('    <stop offset="0.55" stop-color="%s"/>\n' % base.GRAPHITE)
    out.append('    <stop offset="1" stop-color="#15160f"/>\n')
    out.append('  </radialGradient>\n')
    out.append('  <linearGradient id="plate" x1="0" y1="0" x2="0" y2="1">\n')
    out.append('    <stop offset="0" stop-color="%s"/>\n' % PANEL_TOP)
    out.append('    <stop offset="0.48" stop-color="%s"/>\n' % PANEL_MID)
    out.append('    <stop offset="1" stop-color="%s"/>\n' % PANEL_BOTTOM)
    out.append('  </linearGradient>\n')
    out.append('  </defs>\n')
    out.append('  <rect x="0" y="0" width="%s" height="%s" fill="url(#plate)"/>\n'
               % (mm(W), mm(Hh)))
    for x, y, opacity in GRAIN:
        out.append('  <circle class="panelGrain" cx="%s" cy="%s" r="0.12" fill="%s" '
                   'fill-opacity="%s"/>\n'
                   % (mm(x), mm(y), PANEL_TEXT, sw(opacity)))
    out.append('  <rect id="glowPanelInnerBorder" x="0.650" y="0.650" '
               'width="%s" height="%s" rx="1.2" fill="none" stroke="%s" '
               'stroke-width="%s"/>\n'
               % (mm(W - 1.3), mm(Hh - 1.3), PANEL_BORDER, sw(HAIRLINE_W)))
    # The product mockup's masthead: quiet rules, one solder-green dot and one
    # copper dot around the compact FireFlow Glow wordmark.
    out.append('  <circle cx="4.000" cy="8.650" r="0.250" fill="%s"/>\n' % PAD_GREEN)
    out.append('  <line id="glowBrandRuleLeft" x1="5.900" y1="8.650" '
               'x2="8.800" y2="8.650" stroke="%s" stroke-width="0.25"/>\n'
               % PAD_GREEN)
    out.append('  <line id="glowBrandRuleRight" x1="%s" y1="8.650" '
               'x2="%s" y2="8.650" stroke="%s" stroke-width="0.25"/>\n'
               % (mm(W - 9.1), mm(W - 6.2), PAD_COPPER))
    out.append('  <circle cx="%s" cy="8.650" r="0.250" fill="%s"/>\n'
               % (mm(W - 4.3), PAD_COPPER))
    for c in PARAMS + OUTPUTS:
        out.append(SVG_FOR[c.kind](c))
    for t in TEXTS:
        out.append(text_svg(t.x, t.y, t.size, t.rgb, t.anchor, t.str, t.weight))
    for c in PARAMS + OUTPUTS:
        if c.label:
            lx, ly = label_xy(c)
            out.append(text_svg(lx, ly, LBL_SZ[c.kind], label_rgb(c), 0, c.label))
    out.append('</svg>\n')
    return "".join(out)


# --- header -------------------------------------------------------------------
def rgb(hexcol):
    return "0x" + hexcol.lstrip("#").upper()


def label_rgb(c):
    if c.kind == PAD:
        return PAD_COPPER
    if c.kind == OUT:
        return PANEL_TEXT
    return base.INK


def ctl_row(c):
    lx, ly = label_xy(c)
    return ('    { %s, %s, {%sf, %sf}, "%s", {%sf, %sf}, 0, %sf, %s, "%s" },\n'
            % (c.enum, WKMAP[c.kind], mm(c.x), mm(c.y), c.label,
               mm(lx), mm(ly), mm(LBL_SZ[c.kind]), rgb(label_rgb(c)), c.tip))


def enum_block(name, ctls, last):
    body = "".join("    %s,\n" % c.enum for c in ctls)
    return "enum %s {\n%s    %s\n};\n" % (name, body, last)


def pad_points_name(shape):
    return "kPad%sPoints" % shape.pad_id


def pad_points_array(shape):
    points = ", ".join("{ %sf, %sf }" % (mm(x), mm(y))
                       for x, y in shape.points)
    return "static const XY %s[] = { %s };\n" % (pad_points_name(shape), points)


def pad_shape_row(shape):
    lx, ly = shape.label
    cx, cy = shape.centre
    x0, y0, x1, y1 = shape.bounds
    zone = "PadZone::LowerTouch" if shape.zone == "lower_touch" else \
        "PadZone::UpperRear"
    verified = "true" if shape.verified else "false"
    return ("    { %s, %d, { %sf, %sf }, { %sf, %sf }, { %sf, %sf }, "
            "{ %sf, %sf }, \"%s\", %s, %s },\n" %
            (pad_points_name(shape), len(shape.points), mm(lx), mm(ly),
             mm(cx), mm(cy), mm(x0), mm(y0), mm(x1), mm(y1),
             shape.pad_id, zone, verified))


def header():
    out = []
    out.append("// GENERATED by res/gen_flow_panel.py -- do not edit by hand.\n")
    out.append("// Physical 10+2 geometry comes from res/touch2_geometry.py.\n")
    out.append("// It is photo-rectified VCV reference data, not fabrication data;\n")
    out.append("// per-path verification state is carried in PadShape::verified.\n")
    out.append("#pragma once\n")
    out.append("#include <cstddef>\n")
    out.append("#include <cstdint>\n")
    out.append('#include "pad_geometry.hpp"\n')
    out.append("namespace spkyvcv { namespace glow {\n")
    out.append("using XY = spkyvcv::pad_geometry::Point;\n")
    out.append("enum class PadZone : std::uint8_t { LowerTouch, UpperRear };\n")
    out.append("struct PadShape { const XY* points; std::size_t pointCount; "
               "XY label; XY centre; XY min; XY max; const char* id; "
               "PadZone zone; bool verified; };\n")
    out.append("enum WidgetKind { WK_MACRO, WK_PAD, WK_FADER, WK_SWITCH, WK_OUT };\n")
    out.append("struct PanelCtl { int id; WidgetKind kind; XY mm; const char* label;"
               " XY lbl; unsigned char anchor; float lblSize; unsigned lblRgb;"
               " const char* tip; };\n")
    out.append("// anchor: 0 = middle, 1 = start (left-aligned), 2 = end (right-aligned)\n")
    out.append("struct PanelTxt { XY mm; float size; unsigned rgb;"
               " unsigned char anchor; int weight; const char* str; };\n")
    out.append(enum_block("ParamId", PARAMS, "NUM_PARAMS"))
    # The board has no inputs. An empty enum still yields NUM_INPUTS == 0, but
    # an empty ARRAY would be `PanelCtl kInputCtls[] = {}` -- a zero-length
    # array, a GCC extension and ill-formed in standard C++. So the enum is
    # emitted and the table is not; Glow.cpp has no configInput loop.
    out.append(enum_block("InputId", INPUTS, "NUM_INPUTS"))
    out.append(enum_block("OutputId", OUTPUTS, "NUM_OUTPUTS"))
    out.append("enum LightId {\n    NUM_LIGHTS\n};\n")
    out.append("static constexpr float kPanelW = %sf;\n" % mm(W))
    out.append("static constexpr float kPanelH = %sf;\n" % mm(Hh))
    out.append("static constexpr float kKnobR   = %sf;\n" % mm(KNOB_R))
    out.append("static constexpr unsigned kPadCopper    = %su;\n" % rgb(PAD_COPPER))
    out.append("static constexpr unsigned kPadCopperDim = %su;\n" % rgb(PAD_COPPER_DIM))
    out.append("static constexpr unsigned kPadGreen     = %su;\n" % rgb(PAD_GREEN))
    out.append("static constexpr unsigned kPadRefused   = %su;\n" % rgb(PAD_REFUSED))
    out.append("static constexpr float kPadGlowWidth   = %sf;\n" % mm(PAD_GLOW_WIDTH))
    out.append("static constexpr float kPadStrokeWidth = %sf;\n" % mm(PAD_STROKE_W))
    out.append("static constexpr float kPadInnerScale  = %sf;\n" % mm(PAD_INNER_SCALE))
    for shape in PAD_SHAPES:
        out.append(pad_points_array(shape))
    out.append("static const PadShape kPadShapes[12] = {\n")
    for shape in PAD_SHAPES:
        out.append(pad_shape_row(shape))
    out.append("};\n")
    out.append("static constexpr float kFaderW  = %sf;\n" % mm(FADER_W))
    out.append("static constexpr float kFaderH  = %sf;\n" % mm(FADER_H))
    out.append("static constexpr float kSwitchW = %sf;\n" % mm(SWITCH_W))
    out.append("static constexpr float kSwitchH = %sf;\n" % mm(SWITCH_H))
    out.append("static constexpr float kJackR   = %sf;\n" % mm(JACK_R))
    for name, ctls in (("kParamCtls", PARAMS), ("kOutputCtls", OUTPUTS)):
        out.append("static const PanelCtl %s[] = {\n" % name)
        for c in ctls:
            out.append(ctl_row(c))
        out.append("};\n")
    out.append("static const PanelTxt kTexts[] = {\n")
    for t in TEXTS:
        out.append('    { {%sf, %sf}, %sf, %s, %d, %d, "%s" },\n'
                   % (mm(t.x), mm(t.y), mm(t.size), rgb(t.rgb), t.anchor,
                      t.weight, t.str))
    out.append("};\n")
    out.append("} } // namespace spkyvcv::glow\n")
    return "".join(out)


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    with open(os.path.join(here, "Glow.svg"), "w") as f:
        f.write(svg())
    with open(os.path.join(root, "src", "generated_flow_panel.hpp"), "w") as f:
        f.write(header())
    print("wrote res/Glow.svg and src/generated_flow_panel.hpp")
