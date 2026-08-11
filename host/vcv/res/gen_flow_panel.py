#!/usr/bin/env python3
"""Single source of truth for the FireFlow Glow VCV panel (Simple Touch 2).

16 HP, laid out on the measured control centres of a Synthux Simple Touch 2
(res/touch2_geometry.py): twelve touch pads, six trim knobs, two faders, two
switches and one stereo out. No CV inputs -- the board has none.

Every centre is the measured one except the twelve pad places, which are
computed from the measured field: see "the pad field" below for why, and for
what is still measurement in them.

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
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_panel as base
import touch2_geometry as geo

HP = 16
W  = HP * base.MM_PER_HP          # 81.28 mm
Hh = geo.PLATE_H                  # 128.5 mm

# --- printed footprints -------------------------------------------------------
# Widget classes are chosen in Glow.cpp; these are what the PLATE prints, and
# what the collision guard measures. Glow.cpp documents each widget against the
# figure here, the way it already does for the macro knobs.
#
# Only the footprints are ours. Every CENTRE outside the pad field comes from
# touch2_geometry, and a footprint that will not fit between two measured
# centres gets smaller -- those centres never move.
#
# PAD_W is the one footprint set by a collision rather than by taste. The pad
# grid is five columns wide and SW_L's measured centre (x = 30.34) falls in the
# gap between columns 1 and 2; at 7.6 mm the column-1 tile stops 1.02 mm short
# of the switch, and every millimetre added to the tile eats one and a half off
# that gap (the outer columns are pinned by COL_MARGIN, so the pitch shrinks
# with the tile). PAD_H is capped the same way by SW_R, which the top row
# passes 1.28 mm above and the middle row 0.92 mm below.
#
# KNOB_R is the one footprint that is NOT ours: the board prints a gold collar
# around each knob and Task 2 measured it, so the plate prints that and not a
# round number. It used to read 4.5 -- a 9 mm cap on a board whose own collar
# is 7.8 mm across -- and Glow.cpp then chose its widget against the 9. Both
# now follow geo.KNOB_COLLAR_R: 3.885 mm radius, and Rack's RoundSmallBlackKnob
# (7.68 mm) is the stock cap nearest to it.
KNOB_R    = geo.KNOB_COLLAR_R     # measured silkscreen collar, 7.77 mm across
PAD_W     = 7.6
PAD_H     = 9.0
PAD_R     = 2.0                   # corner radius
FADER_W   = 6.8                   # VCVSlider is 6.72 mm wide
FADER_H   = geo.FADER_TRAVEL
SWITCH_W  = 5.0                   # CKSSThree is 4.56 x 9.60 mm
SWITCH_H  = 9.0
JACK_R    = 4.2

# --- strokes and the runtime pad collar ---------------------------------------
# HAIRLINE_W is the stroke the plate prints around every printed shape. It is a
# constant rather than five typed literals because Glow.cpp's TouchPlate draws
# ITS collar over the printed pad tile and has to match it exactly.
#
# COLLAR_* are the four colours that same widget strokes the live pad with, and
# the heavier width it uses while a pad is live. They are emitted for one
# reason: three of the four ARE gen_panel palette entries, and typed into
# Glow.cpp they were a second copy -- retune the palette and the printed tiles
# move while the live / excursed / refused collars silently keep the old
# colours, with nothing gating it. Spec 3.1 grants exactly one carve-out from
# "no colour is written into Glow.cpp", and it is PadQuantity's runtime label,
# not this.
#
# What stays the widget's is WHICH colour a state gets and when -- that is
# runtime state, and it is why the pads have no LightIds. This file only owns
# what the four colours ARE.
HAIRLINE_W    = 0.28
COLLAR_LIVE_W = 0.55
COLLAR_REFUSED  = base.BRICK    # a hold LOCK turned down, or a code that will
                                # not decode. Copper here would read as the
                                # reroll that just did not happen.
COLLAR_EXCURSED = base.COPPER   # part-B accent: "that worked"
COLLAR_LIVE     = base.GREEN    # part-A accent: this pad's place is playing
COLLAR_IDLE     = base.LINE     # the printed hairline, redrawn

LOGO_Y = 10.0
LOGO_SZ = 4.2
LOGO_GAP = 1.1                    # between the two anchors, see TEXTS
GLYPH_ADV = 0.60                  # Share Tech Mono advance, in em
ALPHA_FLAG_X = W - 6.9
ALPHA_FLAG_Y = 14.2
ALPHA_FLAG_H = 3.8

# --- control kinds ------------------------------------------------------------
MACRO  = "MACRO"
PAD    = "PAD"
FADER  = "FADER"
SWITCH = "SWITCH"
OUT    = "OUT"

FOOTPRINT = {
    MACRO:  (KNOB_R * 2, KNOB_R * 2),
    PAD:    (PAD_W, PAD_H),
    FADER:  (FADER_W, FADER_H),
    SWITCH: (SWITCH_W, SWITCH_H),
    OUT:    (JACK_R * 2, JACK_R * 2),
}
# Pad digits are optically centred in their tile, so their baseline sits a
# little below the centre. Jack captions print BELOW the jack: above it they
# would land on the masthead rule at y = 8.65.
LBL_DY = {MACRO: 0.0, PAD: 0.91, FADER: 0.0, SWITCH: 0.0, OUT: 5.6}
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
    return FOOTPRINT[c.kind]


def label_xy(c):
    """Baseline position of a control's caption."""
    return (c.x, c.y + LBL_DY[c.kind])


# --- the pad field: measured structure, computed places -----------------------
# touch2_geometry.PADS is a measurement and stays one. It is also the weakest
# row in that file: the photograph resolves TEN copper cells, not twelve, and
# two of the twelve centres come from a 2-means split of the two oversized edge
# cells. Printed straight, that field reads as accidental -- ragged rows, two
# lonely tiles at the bottom, and SW_L standing on PAD_2 over a quarter of the
# switch's area.
#
# So the STRUCTURE is taken from the measurement and the PLACES are computed
# from it. What is still measured:
#   * the field runs from geo.PAD_FIELD_TOP to the plate's bottom edge, full
#     width -- the solid part of the segmentation;
#   * the twelve places fall into three bands of five, five and two, split out
#     of the measured y positions at the two large gaps (below);
#   * inside a band, each place keeps its measured left-to-right order and
#     takes the grid column nearest its own measured x.
# What is not: the even column pitch and the even row pitch. Nothing here is a
# new measurement, and no measured number is re-typed -- when the 600 dpi scan
# replaces geo.PADS, this layout follows it.

# The largest y step INSIDE a measured band is 4.13 mm; the smallest step
# BETWEEN two bands is 10.85 mm. 8.0 sits in the middle of that gap, so the
# split is not sensitive to the +/- 2 mm the pad row carries.
BAND_GAP     = 8.0
FIELD_MARGIN = 1.0                # inside the field top and the plate bottom
COL_MARGIN   = 1.6                # inside the plate's side edges; the inner
                                  # border rule sits at 0.65 + 0.28 stroke, and
                                  # a tile flush against it reads as a mistake


def _bands_of(pads, gap):
    """Group measured pad centres into bands, splitting at the large y gaps.

    Returns lists of INDICES into `pads`, top band first, each band sorted by
    measured x. The indices are the MPR121 places and must survive the trip.
    """
    by_y = sorted(range(len(pads)), key=lambda i: pads[i][1])
    bands, cur = [], [by_y[0]]
    for prev, i in zip(by_y, by_y[1:]):
        if pads[i][1] - pads[prev][1] > gap:
            bands.append(cur)
            cur = []
        cur.append(i)
    bands.append(cur)
    return [sorted(b, key=lambda i: pads[i][0]) for b in bands]


def _lay_out(pads, bands):
    """Place one tile per measured pad on an even grid inside the field."""
    cols = max(len(b) for b in bands)
    step_x = (W - 2 * COL_MARGIN - PAD_W) / float(cols - 1)
    grid_x = [COL_MARGIN + PAD_W / 2.0 + k * step_x for k in range(cols)]
    top_y = geo.PAD_FIELD_TOP + FIELD_MARGIN + PAD_H / 2.0
    bot_y = Hh - FIELD_MARGIN - PAD_H / 2.0
    step_y = (bot_y - top_y) / float(len(bands) - 1)
    places = [None] * len(pads)
    for row, band in enumerate(bands):
        y = top_y + row * step_y
        free = list(range(cols))
        for i in band:                      # ascending measured x
            k = min(free, key=lambda k: abs(grid_x[k] - pads[i][0]))
            free.remove(k)
            places[i] = (grid_x[k], y)
    return places


PAD_BANDS = _bands_of(geo.PADS, BAND_GAP)
assert sorted(i for b in PAD_BANDS for i in b) == list(range(len(geo.PADS))), \
    "every measured pad must land in exactly one band"
PAD_PLACES = _lay_out(geo.PADS, PAD_BANDS)
assert None not in PAD_PLACES, "a measured pad got no place on the grid"


# --- the tables ---------------------------------------------------------------
_MACRO_NAMES = ["MOTION", "DENSITY", "BRIGHT", "DIRT", "WANDER", "SPACE"]
_MACRO_TIPS = [
    "MOTION -- how much everything moves",
    "DENSITY -- how much happens",
    "BRIGHT -- spectral centre",
    "DIRT -- clean to driven",
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

# PAD_PLACES is indexed exactly like geo.PADS: index i is MPR121 place i, so
# the order is load-bearing and must not be tidied into reading order.
for _i, (_x, _y) in enumerate(PAD_PLACES):
    PARAMS.append(Ctl("PAD_%d" % (_i + 1), PAD, _x, _y, str(_i + 1),
                      "Place %d -- tap: go there. Hold: reroll all six macro "
                      "domains, the ground stays. Tap again: back." % (_i + 1)))

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

# The two wordmark halves are anchored end / start around a 1.1 mm gap. Their
# anchors are NOT symmetric about the centre line: FireFlow has eight glyphs
# and GLOW four, so equal anchors would push the combined visible mark about
# 5 mm to the left. Derive the offset instead of typing it, so the mark stays
# centred if the size or the gap ever changes.
_LOGO_DX = (len("FireFlow") * LOGO_SZ * GLYPH_ADV - LOGO_GAP
            - len("GLOW") * LOGO_SZ * GLYPH_ADV) / 2.0
TEXTS = [
    Txt(W * 0.5 + _LOGO_DX, LOGO_Y, LOGO_SZ, base.MUTED, 2, "FireFlow", 300),
    Txt(W * 0.5 + _LOGO_DX + LOGO_GAP, LOGO_Y, LOGO_SZ, base.INK, 1, "GLOW", 700),
    Txt(W - 3.0, 16.75, 1.15, base.WHITE, 0, "ALPHA", 700),
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


def pad_svg(c):
    """A plate the widget draws over. The SVG tile is the printed footprint;
    TouchPlate paints the live/excursed state on top of it at runtime."""
    return (
        '  <rect class="touchPlate" x="%s" y="%s" width="%s" height="%s" '
        'rx="%s" fill="%s" fill-opacity="0.55" stroke="%s" '
        'stroke-width="%s"/>\n'
        % (mm(c.x - PAD_W / 2.0), mm(c.y - PAD_H / 2.0), mm(PAD_W), mm(PAD_H),
           mm(PAD_R), base.PAPER_DEEP, base.LINE, sw(HAIRLINE_W)))


def fader_svg(c):
    return (
        '  <rect x="%s" y="%s" width="%s" height="%s" rx="%s" fill="%s" '
        'stroke="%s" stroke-width="%s"/>\n'
        '  <line x1="%s" y1="%s" x2="%s" y2="%s" stroke="%s" '
        'stroke-width="0.6" stroke-linecap="round"/>\n'
        % (mm(c.x - FADER_W / 2.0), mm(c.y - FADER_H / 2.0), mm(FADER_W),
           mm(FADER_H), mm(FADER_W / 2.0), base.PAPER_DEEP, base.LINE,
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
    out.append('    <stop offset="0" stop-color="%s"/>\n' % base.PAPER_HI)
    out.append('    <stop offset="0.48" stop-color="%s"/>\n' % base.PAPER)
    out.append('    <stop offset="1" stop-color="%s"/>\n' % base.PAPER_LO)
    out.append('  </linearGradient>\n')
    out.append('  </defs>\n')
    out.append('  <rect x="0" y="0" width="%s" height="%s" fill="url(#plate)"/>\n'
               % (mm(W), mm(Hh)))
    out.append('  <rect id="glowPanelInnerBorder" x="0.650" y="0.650" '
               'width="%s" height="%s" rx="1.2" fill="none" stroke="%s" '
               'stroke-width="%s"/>\n'
               % (mm(W - 1.3), mm(Hh - 1.3), base.LINE, sw(HAIRLINE_W)))
    # The product mockup's masthead: quiet rules, one solder-green dot and one
    # copper dot around the compact FireFlow Glow wordmark.
    out.append('  <circle cx="4.000" cy="8.650" r="0.650" fill="%s"/>\n' % base.GREEN)
    out.append('  <line id="glowBrandRuleLeft" x1="5.900" y1="8.650" '
               'x2="8.800" y2="8.650" stroke="%s" stroke-width="0.35"/>\n'
               % base.GREEN)
    out.append('  <line id="glowBrandRuleRight" x1="%s" y1="8.650" '
               'x2="%s" y2="8.650" stroke="%s" stroke-width="0.35"/>\n'
               % (mm(W - 9.1), mm(W - 6.2), base.COPPER))
    out.append('  <circle cx="%s" cy="8.650" r="0.650" fill="%s"/>\n'
               % (mm(W - 4.3), base.COPPER))
    # Early-alpha badge: a small edge-mounted pennant.
    out.append('  <polygon id="alphaPennant" points="%s,%s %s,%s %s,%s %s,%s %s,%s" '
               'fill="%s"/>\n'
               % (mm(W), mm(ALPHA_FLAG_Y), mm(ALPHA_FLAG_X), mm(ALPHA_FLAG_Y),
                  mm(ALPHA_FLAG_X + 1.7), mm(ALPHA_FLAG_Y + ALPHA_FLAG_H / 2.0),
                  mm(ALPHA_FLAG_X), mm(ALPHA_FLAG_Y + ALPHA_FLAG_H),
                  mm(W), mm(ALPHA_FLAG_Y + ALPHA_FLAG_H), base.COPPER))
    for c in PARAMS + OUTPUTS:
        out.append(SVG_FOR[c.kind](c))
    for t in TEXTS:
        out.append(text_svg(t.x, t.y, t.size, t.rgb, t.anchor, t.str, t.weight))
    for c in PARAMS + OUTPUTS:
        if c.label:
            lx, ly = label_xy(c)
            out.append(text_svg(lx, ly, LBL_SZ[c.kind], base.INK, 0, c.label))
    out.append('</svg>\n')
    return "".join(out)


# --- header -------------------------------------------------------------------
def rgb(hexcol):
    return "0x" + hexcol.lstrip("#").upper()


def ctl_row(c):
    lx, ly = label_xy(c)
    return ('    { %s, %s, {%sf, %sf}, "%s", {%sf, %sf}, 0, %sf, %s, "%s" },\n'
            % (c.enum, WKMAP[c.kind], mm(c.x), mm(c.y), c.label,
               mm(lx), mm(ly), mm(LBL_SZ[c.kind]), rgb(base.INK), c.tip))


def enum_block(name, ctls, last):
    body = "".join("    %s,\n" % c.enum for c in ctls)
    return "enum %s {\n%s    %s\n};\n" % (name, body, last)


def header():
    out = []
    out.append("// GENERATED by res/gen_flow_panel.py -- do not edit by hand.\n")
    out.append("// Geometry comes from res/touch2_geometry.py, which was measured\n")
    out.append("// off %s\n" % geo.SRC_IMAGE)
    out.append("// (a sibling repo). Those numbers are photo-derived and\n")
    out.append("// provisional; they get corrected against the board when it\n")
    out.append("// arrives. The twelve pad places are laid out on the measured\n")
    out.append("// field rather than printed from the measured centres -- see\n")
    out.append("// \"the pad field\" in the generator. This panel is a VCV panel,\n")
    out.append("// NOT a faceplate draft.\n")
    out.append("#pragma once\n")
    out.append("namespace spkyvcv { namespace glow {\n")
    out.append("struct XY { float x, y; };\n")
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
    out.append("static constexpr float kPadW    = %sf;\n" % mm(PAD_W))
    out.append("static constexpr float kPadH    = %sf;\n" % mm(PAD_H))
    out.append("static constexpr float kPadR    = %sf;\n" % mm(PAD_R))
    out.append("static constexpr float kFaderW  = %sf;\n" % mm(FADER_W))
    out.append("static constexpr float kFaderH  = %sf;\n" % mm(FADER_H))
    out.append("static constexpr float kSwitchW = %sf;\n" % mm(SWITCH_W))
    out.append("static constexpr float kSwitchH = %sf;\n" % mm(SWITCH_H))
    out.append("static constexpr float kJackR   = %sf;\n" % mm(JACK_R))
    # The pad collar Glow.cpp's TouchPlate strokes at runtime. The WIDGET picks
    # which of these a state gets -- that is runtime state, not panel state, and
    # it is why the pads have no LightIds. What the colours ARE is the panel's,
    # and three of the four are gen_panel palette entries: typed into Glow.cpp
    # they were a second copy that a palette retune would have left behind.
    out.append("static constexpr unsigned kCollarRefused  = %su;\n"
               % rgb(COLLAR_REFUSED))
    out.append("static constexpr unsigned kCollarExcursed = %su;\n"
               % rgb(COLLAR_EXCURSED))
    out.append("static constexpr unsigned kCollarLive     = %su;\n"
               % rgb(COLLAR_LIVE))
    out.append("static constexpr unsigned kCollarIdle     = %su;\n"
               % rgb(COLLAR_IDLE))
    out.append("static constexpr float kCollarWLive = %sf;\n"
               % mm(COLLAR_LIVE_W))
    out.append("static constexpr float kCollarWIdle = %sf;\n"
               % mm(HAIRLINE_W))
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
