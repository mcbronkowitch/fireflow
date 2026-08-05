#!/usr/bin/env python3
"""Single source of truth for the FireFlow Glow VCV panel (spec 6).

12 HP, drawn at true hardware dimensions so the faceplate doubles as the 1:1
draft for the M6 panel: six 16 mm macro knobs in two rows of three, a large
NEW button, and eight jacks in two rows of four along the bottom.

Palette is shared with the big Fireflow panel (gen_panel.py) so the two
modules read as one instrument. Layout is NOT shared: gen_panel.py is built
around a 42 HP two-part faceplate with LED rings, and nothing there applies.

Emits (both committed):
  - res/Glow.svg                     the faceplate
  - src/generated_flow_panel.hpp     enums + control/text tables

Run from host/vcv/:  python3 res/gen_flow_panel.py
The C++ never hardcodes a coordinate, label or colour -- it reads them from
the generated header, so graphics and widget placement can never drift apart.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_panel as base

HP = 12
W  = HP * base.MM_PER_HP          # 60.96 mm
Hh = 128.5                        # standard Eurorack height

# --- geometry (spec 6) --------------------------------------------------------
KNOB_R = 8.0                      # 16 mm macro knobs
BTN_R  = 4.5
JACK_R = 4.2
COL_X  = (10.48, 30.48, 50.48)    # 20 mm pitch, centred on W/2
ROW_Y  = (32.0, 54.0)
KNOB_LBL_DY = 10.5
NEW_XY = (30.48, 78.0)
NEW_LBL_DY = 6.5
JACK_X = (9.48, 23.48, 37.48, 51.48)   # 14 mm pitch, centred on W/2
JACK_Y = (100.0, 117.0)
JACK_LBL_DY = -5.6
LOGO_Y = 10.0

# --- control kinds ------------------------------------------------------------
MACRO = "MACRO"
BTN   = "BTN"
IN    = "IN"
OUT   = "OUT"

RADIUS = {MACRO: KNOB_R, BTN: BTN_R, IN: JACK_R, OUT: JACK_R}
LBL_DY = {MACRO: KNOB_LBL_DY, BTN: NEW_LBL_DY, IN: JACK_LBL_DY, OUT: JACK_LBL_DY}
LBL_SZ = {MACRO: 2.6, BTN: 2.9, IN: 2.2, OUT: 2.2}
WKMAP  = {MACRO: "WK_MACRO", BTN: "WK_BTN", IN: "WK_IN", OUT: "WK_OUT"}


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


def radius_of(c):
    return RADIUS[c.kind]


def label_xy(c):
    """Baseline position of a control's caption."""
    return (c.x, c.y + LBL_DY[c.kind])


# --- the tables ---------------------------------------------------------------
PARAMS = [
    Ctl("MOTION",  MACRO, COL_X[0], ROW_Y[0], "MOTION",  "MOTION -- how much everything moves"),
    Ctl("DENSITY", MACRO, COL_X[1], ROW_Y[0], "DENSITY", "DENSITY -- how much happens"),
    Ctl("BRIGHT",  MACRO, COL_X[2], ROW_Y[0], "BRIGHT",  "BRIGHT -- spectral centre"),
    Ctl("DIRT",    MACRO, COL_X[0], ROW_Y[1], "DIRT",    "DIRT -- clean to driven"),
    Ctl("WANDER",  MACRO, COL_X[1], ROW_Y[1], "WANDER",  "WANDER -- predictable to wandering"),
    Ctl("SPACE",   MACRO, COL_X[2], ROW_Y[1], "SPACE",   "SPACE -- close to vast"),
    Ctl("NEW_BTN", BTN,   NEW_XY[0], NEW_XY[1], "NEW",
        "NEW -- tap: new terrain. Hold + turn a knob: reroll that macro. "
        "Hold 1.5 s: undo. Hold 5 s: lock."),
]
INPUTS = [
    Ctl("CV_MOT", IN, JACK_X[0], JACK_Y[0], "CV MOT", "CV into MOTION (0..10 V, adds to the knob)"),
    Ctl("CV_DEN", IN, JACK_X[1], JACK_Y[0], "CV DEN", "CV into DENSITY (0..10 V, adds to the knob)"),
    Ctl("CV_BRT", IN, JACK_X[2], JACK_Y[0], "CV BRT", "CV into BRIGHT (0..10 V, adds to the knob)"),
    Ctl("CV_DRT", IN, JACK_X[3], JACK_Y[0], "CV DRT", "CV into DIRT (0..10 V, adds to the knob)"),
    Ctl("CV_SPC", IN, JACK_X[0], JACK_Y[1], "CV SPC", "CV into SPACE (0..10 V, adds to the knob)"),
    Ctl("CLK",    IN, JACK_X[1], JACK_Y[1], "CLK",    "Clock in, one pulse per beat -- overrides the terrain's tempo"),
]
OUTPUTS = [
    Ctl("OUT_L", OUT, JACK_X[2], JACK_Y[1], "OUT L", "Main out, left"),
    Ctl("OUT_R", OUT, JACK_X[3], JACK_Y[1], "OUT R", "Main out, right"),
]
TEXTS = [
    Txt(29.9, LOGO_Y, 5.0, base.MUTED, 2, "FireFlow", 300),
    Txt(31.1, LOGO_Y, 5.0, base.INK,   1, "GLOW", 700),
]


# --- SVG ----------------------------------------------------------------------
def mm(v):
    return "%.3f" % v


ANCHOR_SVG = {0: "middle", 1: "start", 2: "end"}


def knob_svg(c):
    """Graphite cap with a white pointer tick at 12 o'clock."""
    return (
        '  <circle cx="%s" cy="%s" r="%s" fill="%s" stroke="%s" stroke-width="0.30"/>\n'
        '  <line x1="%s" y1="%s" x2="%s" y2="%s" stroke="%s" stroke-width="0.55" '
        'stroke-linecap="round"/>\n'
        % (mm(c.x), mm(c.y), mm(KNOB_R), base.GRAPHITE, base.LINE,
           mm(c.x), mm(c.y - KNOB_R * 0.45), mm(c.x), mm(c.y - KNOB_R * 0.85),
           base.WHITE))


def button_svg(c):
    return (
        '  <circle cx="%s" cy="%s" r="%s" fill="%s" stroke="%s" stroke-width="0.35"/>\n'
        '  <circle cx="%s" cy="%s" r="%s" fill="%s" opacity="0.85"/>\n'
        % (mm(c.x), mm(c.y), mm(BTN_R), base.WELL, base.LINE,
           mm(c.x), mm(c.y), mm(BTN_R * 0.55), base.GREEN))


def jack_svg(c):
    return (
        '  <circle cx="%s" cy="%s" r="%s" fill="%s" stroke="%s" stroke-width="0.30"/>\n'
        '  <circle cx="%s" cy="%s" r="%s" fill="%s"/>\n'
        % (mm(c.x), mm(c.y), mm(JACK_R), base.GRAPHITE, base.LINE,
           mm(c.x), mm(c.y), mm(JACK_R * 0.38), base.WELL))


def text_svg(x, y, size, rgb, anchor, s, weight=None):
    if weight is None:
        return ('  <text x="%s" y="%s" font-family="Inter, Helvetica, sans-serif" '
                'font-size="%s" fill="%s" text-anchor="%s">%s</text>\n'
                % (mm(x), mm(y), mm(size), rgb, ANCHOR_SVG[anchor], s))
    else:
        return ('  <text x="%s" y="%s" font-family="Inter, Helvetica, sans-serif" '
                'font-size="%s" fill="%s" text-anchor="%s" font-weight="%d">%s</text>\n'
                % (mm(x), mm(y), mm(size), rgb, ANCHOR_SVG[anchor], weight, s))


def svg():
    out = []
    out.append('<?xml version="1.0" encoding="UTF-8"?>\n')
    out.append('<svg xmlns="http://www.w3.org/2000/svg" width="%smm" height="%smm" '
               'viewBox="0 0 %s %s">\n' % (mm(W), mm(Hh), mm(W), mm(Hh)))
    out.append('  <defs><linearGradient id="plate" x1="0" y1="0" x2="0" y2="1">\n')
    out.append('    <stop offset="0" stop-color="%s"/>\n' % base.PAPER_HI)
    out.append('    <stop offset="1" stop-color="%s"/>\n' % base.PAPER_LO)
    out.append('  </linearGradient></defs>\n')
    out.append('  <rect x="0" y="0" width="%s" height="%s" fill="url(#plate)"/>\n'
               % (mm(W), mm(Hh)))
    # hairline above the jack block
    out.append('  <line x1="4.000" y1="%s" x2="%s" y2="%s" stroke="%s" '
               'stroke-width="0.25"/>\n'
               % (mm(JACK_Y[0] + JACK_LBL_DY - 4.0), mm(W - 4.0),
                  mm(JACK_Y[0] + JACK_LBL_DY - 4.0), base.LINE))
    for c in PARAMS:
        out.append(knob_svg(c) if c.kind == MACRO else button_svg(c))
    for c in INPUTS + OUTPUTS:
        out.append(jack_svg(c))
    for t in TEXTS:
        out.append(text_svg(t.x, t.y, t.size, t.rgb, t.anchor, t.str, t.weight))
    for c in PARAMS + INPUTS + OUTPUTS:
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
    out.append("#pragma once\n")
    out.append("namespace spkyvcv { namespace glow {\n")
    out.append("struct XY { float x, y; };\n")
    out.append("enum WidgetKind { WK_MACRO, WK_BTN, WK_IN, WK_OUT };\n")
    out.append("struct PanelCtl { int id; WidgetKind kind; XY mm; const char* label;"
               " XY lbl; unsigned char anchor; float lblSize; unsigned lblRgb;"
               " const char* tip; };\n")
    out.append("// anchor: 0 = middle, 1 = start (left-aligned), 2 = end (right-aligned)\n")
    out.append("struct PanelTxt { XY mm; float size; unsigned rgb;"
               " unsigned char anchor; int weight; const char* str; };\n")
    out.append(enum_block("ParamId", PARAMS, "NUM_PARAMS"))
    out.append(enum_block("InputId", INPUTS, "NUM_INPUTS"))
    out.append(enum_block("OutputId", OUTPUTS, "NUM_OUTPUTS"))
    out.append("enum LightId {\n    NEW_L,\n    NUM_LIGHTS\n};\n")
    out.append("static constexpr float kPanelW = %sf;\n" % mm(W))
    out.append("static constexpr float kPanelH = %sf;\n" % mm(Hh))
    out.append("static constexpr float kKnobR = %sf;\n" % mm(KNOB_R))
    out.append("static constexpr float kBtnR  = %sf;\n" % mm(BTN_R))
    out.append("static constexpr float kJackR = %sf;\n" % mm(JACK_R))
    for name, ctls in (("kParamCtls", PARAMS), ("kInputCtls", INPUTS),
                       ("kOutputCtls", OUTPUTS)):
        out.append("static const PanelCtl %s[] = {\n" % name)
        for c in ctls:
            out.append(ctl_row(c))
        out.append("};\n")
    out.append("static const PanelTxt kTexts[] = {\n")
    for t in TEXTS:
        out.append('    { {%sf, %sf}, %sf, %s, %d, %d, "%s" },\n'
                   % (mm(t.x), mm(t.y), mm(t.size), rgb(t.rgb), t.anchor, t.weight, t.str))
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
