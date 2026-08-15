#!/usr/bin/env python3
"""Hardware-mode panel: the 60 HP envelope draft (envelope spec 2026-08-08 §4).

Emits res/FireflowHW.svg + src/generated_hw_panel.hpp (namespace spkyhw).
Geometry is the 2026-08-10 redistribution as drawn in
docs/hardware/2026-08-10-hw-panel-redistribution.svg (graphics round 15 Aug).

Plate graphics are the 15 Aug design round, variant 2a "Technical Blueprint --
echte Potigroessen": dark anodised plate, three tinted zones, a printed
airflow/ember silhouette, and one fixed raster of drawing frames. Not a
single control moved for it -- the round is colour, texture, frames and
lettering only.

Shares all parameter identity with gen_panel.py (import); defines only
geometry. Run from host/vcv/:  python3 res/gen_hw_panel.py
"""
import os, copy
import gen_panel as gp

HP = 60
W  = HP * gp.MM_PER_HP            # 304.8 mm
Hh = 128.5
CX = W / 2.0
KEEP_TOP, KEEP_BOT = 9.0, 119.5   # rails + M3 screws own the rest (bodies, not ink)
ZONE_A = 124.20                   # deck/centre colour split of the plate itself

# ---------------------------------------------------------------------------
#  Plate palette — design "Technical Blueprint / echte Potigrößen" (15 Aug).
#  Dark anodised plate, three tinted zones, group boxes as drawing frames.
#  Nothing here moves a control: the whole round is colour, texture, frames
#  and lettering.
# ---------------------------------------------------------------------------
PLATE_HI, PLATE_LO = "#0f1418", "#0b0f12"
ZONE_A_HI, ZONE_A_LO = "#12191d", "#0d1417"   # deck A, cool
ZONE_C_HI, ZONE_C_LO = "#080b0d", "#0a0e10"   # centre, neutral and darkest
ZONE_B_HI, ZONE_B_LO = "#1b1714", "#12100e"   # deck B, warm
ZONE_MID = {"A": "#10171a", "B": "#171411"}   # flat stand-in for the fade overlays
SEAM_W = 12.4                                 # blend width across a zone border

HW_LABEL   = "#a7bcc6"            # knob/jack captions
HW_LEGEND  = "#6f8894"            # group names, brand subline
HW_RING    = "#33454e"            # hairline around a body
HW_RING2   = "#1e2a30"            # secondary hairline (LED bezel)
HW_WELL    = "#080b0d"            # mounting hole under a pot
JACK_METAL = "#7f8f96"
PAD_FILL   = "#dbe9ef"            # button keycap
PAD_STROKE = "#3c525c"
LED_OFF    = "#0d1417"
ACC = {"A": "#3fbf9c", "B": "#e8945a", "C": "#7fb6c9"}

BOX_FILL, BOX_FILL_OPACITY = "#0c1215", 0.55
BOX_STROKE = "#2b3d46"
BOX_DASH = "1.6 1.2"
BOX_BRACKET = 2.6                 # corner-angle arm length
KNOCKOUT = "#0b0f12"              # patch the frame under a group legend

# Real hardware bodies, not the finger-clearance radius the layout is spaced
# on. The frames are drawn against THESE, which is what buys the air between
# the boxes (design note 2a).
BODY_R = {"G": 6.0, "S": 4.4, "P": 3.1, "J": 3.1, "L": 1.5}

# What Rack actually puts on top of the plate, in mm. Neither the plate body
# nor the layout clearance circle: the rehearsal widget is a third radius,
# and it is the one that can bury a legend the SVG preview shows fine.
# Measured from Rack2Pro/res/ComponentLibrary at 75 dpi -- RoundBlackKnob
# 28.348 px, Trimpot 17.856 px, VCVButton 18.000 px, PJ301M 23.700 px.
RACK_R = {"G": 4.80, "S": 3.02, "P": 3.05, "J": 4.02, "L": 1.50}

# ShareTechMono, the face HwPanelText loads: 0.5 em advance, ~0.72 em cap
# height. Both numbers below are what the legend knockouts are cut to.
FONT_ADVANCE, FONT_CAP = 0.50, 0.72


def text_run(x, y, size, spacing, anchor, txt):
    """Ink box of a lettering row: (x0, x1, y_top, y_baseline)."""
    w = len(txt) * (size * FONT_ADVANCE + spacing)
    x0 = x if anchor == "start" else (x - w if anchor == "end" else x - w / 2)
    return (x0, x0 + w, y - size * FONT_CAP, y)


def _blend_hex(fg, bg, t):
    """Opaque mix of fg over bg at opacity t, matching a fill-opacity wash."""
    def rgb(h):
        h = h.lstrip("#")
        return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
    a, b = rgb(fg), rgb(bg)
    m = tuple(round(a[i] * t + b[i] * (1.0 - t)) for i in range(3))
    return f"#{m[0]:02x}{m[1]:02x}{m[2]:02x}"


# PanelTxt/PanelCtl carry a colour, not an opacity: anything the design draws
# at partial alpha has to be mixed down here or Rack would print it solid.
IDX_COL = {s: _blend_hex(ACC[s], KNOCKOUT, 0.75) for s in ACC}
LED_ON = {s: _blend_hex(ACC[s], LED_OFF, 0.55) for s in ACC}


def zone_of(x):
    """Which of the three plate zones x falls in."""
    if x < ZONE_A:
        return "A"
    if x > W - ZONE_A:
        return "B"
    return "C"

HW_SIZE = {
    "MOD": "G", "DENSITY": "G",
    "RATE": "S", "SHAPE": "S", "SMOOTH": "S", "RANGE": "S", "MELODY": "S",
    "COLOR": "G",
    "TUNE": "S", "DETUNE": "S",
    "FILT": "G", "SOURCE": "S",                    # TIMB is small (graphics round)
    "ATTACK": "S", "DECAY": "S", "RES": "S", "SUB": "S", "STAGES": "S",
    "FLUX": "G",
    "FLUXRATE": "S", "FLUXFB": "S", "LINK": "S",
    "REV_MIX": "G",
    "COMP": "G", "GRIT": "S",
    "STEPS": "S", "SONG": "S",
    "ENGINE": "S", "REC": "P",                     # ENGINE is a 5-zone detent pot
    "MORPH": "G", "TIDE": "S", "CHOKE": "S", "PACE": "S",
    "TEMPO": "S", "COUPLE": "S", "SHUFFLE": "S",
    "SCALE": "S", "DRIFT": "S",
    "REV_DECAY": "G", "REV_SIZE": "S", "REV_TONE": "S", "REV_DIFF": "S",
}

CLASS_R = {"G": 8.5, "S": 6.0, "P": 4.0, "J": 4.0, "L": 1.5}
CLASS_LBL_DY = {"G": 10.5, "S": 8.0, "P": 6.5, "J": 8.0, "L": 0.0}

# Four-character plate words. Keys are enum bases (SHAPE) or full names (IN_L).
HW_CAPTION = {
    "SHAPE": "SHAP", "RANGE": "RANG", "COLOR": "COLR",
    "COUPLE": "SYNC", "TEMPO": "TEMP", "SHUFFLE": "SHFL",
    "SCALE": "SCAL", "DRIFT": "DRFT", "CHOKE": "CHOK",
    "MORPH": "MRPH", "REV_DECAY": "DECY",
    "STAGES": "",
    "IN_L": "IN L", "IN_R": "IN R", "OUT_L": "OUT L", "OUT_R": "OUT R",
    "SHIFTBTN": "SHFT",
}


def hw_class(enum):
    """Size class for a full enum name. Jacks and LEDs come from the shared
    inventory's kind (they have no hardware choice to make); everything a
    finger turns or presses comes from HW_SIZE."""
    if enum in ("MODBTN", "SHIFTBTN"):
        return "P"
    if enum in _JACK_ENUMS:
        return "J"
    if enum in _LIGHT_ENUMS:
        return "L"
    if enum.endswith("_L"):
        return "L"
    base = enum[:-2] if enum.endswith(("_A", "_B")) else enum
    return HW_SIZE[base]


def _caption_for(enum, fallback):
    if enum in HW_CAPTION:
        return HW_CAPTION[enum]
    base = enum[:-2] if enum.endswith(("_A", "_B")) else enum
    return HW_CAPTION.get(base, fallback)


_JACK_ENUMS = ({c.enum for c in gp.INPUTS} | {c.enum for c in gp.OUTPUTS}
               | {c.enum for c in gp.HW_MOD_INPUTS})
_LIGHT_ENUMS = {c.enum for c in gp.LIGHTS}

# Row rhythm (spec 2026-08-10 §8), VOICE big-row compacted to y=51 with TIMB small.
Y_TOP = 14.5
Y_B1K, Y_B1G = 34.0, 53.0
Y_VOICE = 51.0
Y_B2K, Y_B2G = 76.0, 95.0
JACK_Y = 114.0
SD_X, SD_Y, SD_W, SD_H = 152.4, JACK_Y, 11.0, 6.0

# CV jack columns — uniform 11.5 mm raster, not under the knobs (spec §13).
X_COLOR, X_FILT, X_TIMB, X_LVL = 79.0, 90.5, 102.0, 113.5

DECK_POS = {
    "STEPS":  (35.00, Y_TOP), "SONG": (48.00, Y_TOP),
    "RATE":   (61.00, Y_TOP), "MELODY": (74.00, Y_TOP),
    "REC":    (101.00, Y_TOP),
    "SHAPE":  (18.25, Y_B1K), "SMOOTH": (31.25, Y_B1K), "RANGE": (44.25, Y_B1K),
    "MOD":    (21.75, Y_B1G), "DENSITY": (40.75, Y_B1G),
    "ATTACK": (68.25, Y_B1K), "DECAY": (81.25, Y_B1K),
    "RES":    (94.25, Y_B1K), "SUB": (107.25, Y_B1K),
    "ENGINE": (70.25, 49.25), "FILT": (86.25, Y_VOICE), "SOURCE": (102.25, 47.61),
    "TUNE":   (17.00, Y_B2K), "DETUNE": (30.00, Y_B2K),
    "COLOR":  (23.50, Y_B2G),
    "FLUX":   (67.00, Y_B2K),
    "FLUXRATE": (54.00, 89.86), "FLUXFB": (67.00, Y_B2G), "LINK": (80.00, 89.86),
    "COMP":   (106.50, Y_B2K), "GRIT": (106.50, Y_B2G),
    "REV_MIX": (136.40, Y_B2G),
    "STAGES": (68.25, Y_B1K),
}

CENTER_POS = {
    "SCALE":  (139.40, Y_TOP), "DRIFT": (152.40, Y_TOP), "CHOKE": (165.40, Y_TOP),
    "TEMPO":  (139.40, Y_B1K), "COUPLE": (152.40, Y_B1K), "SHUFFLE": (165.40, Y_B1K),
    "TIDE":   (136.40, 50.22), "MORPH": (152.40, Y_B1G), "PACE": (168.40, 50.22),
    "REV_SIZE": (136.40, Y_B2K), "REV_DECAY": (152.40, 79.00), "REV_DIFF": (168.40, Y_B2K),
    "REV_TONE": (152.40, 97.00),
}

JACK_POS = {"PITCH_A": 56.00, "GATE_A": 67.50,
            "IN_L": 33.00, "IN_R": 44.50, "CLOCK": 136.00,
            "RESET": 168.80, "OUT_L": 260.30, "OUT_R": 271.80,
            "GATE_B": W - 67.50, "PITCH_B": W - 56.00,
            "MOD1_A": X_COLOR, "MOD2_A": X_FILT, "MOD3_A": X_TIMB, "MOD4_A": X_LVL,
            "MOD1_B": W - X_COLOR, "MOD2_B": W - X_FILT,
            "MOD3_B": W - X_TIMB, "MOD4_B": W - X_LVL}

LIGHT_POS = {"REC_A_L": (108.50, Y_TOP), "GATE_A_L": (116.50, Y_TOP),
             "REC_B_L": (W - 108.50, Y_TOP), "GATE_B_L": (W - 116.50, Y_TOP)}


def place(c):
    """Clone a gen_panel control onto the hardware grid."""
    n = copy.copy(c)
    n.r = CLASS_R[hw_class(c.enum)]
    n.lbl = None
    n.label = _caption_for(c.enum, c.label)
    base = c.enum
    if base.endswith("_A") or base.endswith("_B"):
        stem, side = base[:-2], base[-1]
        if stem in DECK_POS:
            ax, ay = DECK_POS[stem]
            n.x, n.y = (ax, ay) if side == "A" else (W - ax, ay)
            return n
    if base in CENTER_POS:
        n.x, n.y = CENTER_POS[base]
        return n
    if base in JACK_POS:
        n.x, n.y = JACK_POS[base], JACK_Y
        return n
    if base in LIGHT_POS:
        n.x, n.y = LIGHT_POS[base]
        return n
    raise KeyError(f"no hw slot for {base}")

HW_PARAMS  = [place(c) for c in gp.RUNTIME_PANEL_PARAMS]
HW_INPUTS  = [place(c) for c in gp.INPUTS] + [place(c) for c in gp.HW_MOD_INPUTS]
HW_OUTPUTS = [place(c) for c in gp.OUTPUTS]
HW_LIGHTS  = [place(c) for c in gp.LIGHTS]


class HwOnly:
    __slots__ = ("enum", "kind", "x", "y", "r", "label", "tip")

    def __init__(self, enum, cls, x, y, label, tip):
        self.enum, self.x, self.y, self.label, self.tip = enum, x, y, label, tip
        self.kind = {"P": gp.LATCH, "J": gp.IN, "L": gp.LIGHT}[cls]
        self.r = CLASS_R[cls]


HW_ONLY = [
    HwOnly("SHIFTBTN", "P", 14.00, JACK_Y, "SHFT", "reserved, no function"),
    HwOnly("MODBTN", "P", W - 14.00, JACK_Y, "MOD", "reserved, no function"),
    HwOnly("TEMPO_L", "L", 130.40, Y_B1K, "", "tempo"),
    HwOnly("SYNC_L", "L", W - 130.40, Y_B1K, "", "sync"),
]
for _side, _sx in (("A", lambda v: v), ("B", lambda v: W - v)):
    HW_ONLY.append(HwOnly(f"FLOW_{_side}_L", "L", _sx(93.50), Y_TOP, "", "flow"))
    HW_ONLY.append(HwOnly(f"CAP_{_side}_L", "L", _sx(112.50), Y_TOP, "", "capture"))

ALL_HW = HW_PARAMS + HW_INPUTS + HW_OUTPUTS + HW_LIGHTS + HW_ONLY

TEXTS = []

LBL_MARGIN = 1.5


def _caption_is_clear(c, lx, ly):
    """True when (lx, ly) sits outside c's own footprint and clears every
    other control's clearance circle by LBL_MARGIN."""
    if ((lx - c.x) ** 2 + (ly - c.y) ** 2) ** 0.5 < c.r - 1e-9:
        return False
    for o in ALL_HW:
        if o is c:
            continue
        if ((lx - o.x) ** 2 + (ly - o.y) ** 2) ** 0.5 < o.r + LBL_MARGIN - 1e-9:
            return False
    return True


def hw_label(c):
    """Caption placement, by rule rather than by named exception."""
    if not c.label:
        return (c.x, c.y, "middle", 2.2, HW_LABEL)
    dy = CLASS_LBL_DY[hw_class(c.enum)]
    if c.y >= JACK_Y - 0.5:
        dy = 8.0
    third = ((c.x + c.r + 1.0, c.y + 1.0, "start") if c.x <= CX else
             (c.x - c.r - 1.0, c.y + 1.0, "end"))
    for lx, ly, anchor in ((c.x, c.y + dy, "middle"),
                           (c.x, c.y - dy, "middle"),
                           third):
        if _caption_is_clear(c, lx, ly):
            return (lx, ly, anchor, 2.2, HW_LABEL)
    raise ValueError(f"no clear caption position for {c.enum} -- the geometry "
                     f"is too tight, move a control (spec 2026-08-10 §3)")


# =============================================================================
#  Group frames: one fixed raster of drawing boxes
# =============================================================================
# Four rows straight across the plate, 3 mm of air everywhere between boxes,
# a shared outer edge at 8 mm, a shared deck edge at 120 mm, and deck B is
# deck A mirrored. Boxes are cut at CUTS, which is the centre of the 3 mm
# gap, so the arithmetic can never leave a sliver or an overlap.
BOX_GAP = 3.0
DECK_EDGE = 120.0                 # right edge of the last deck-A box
CENTRE_L = 123.0                  # left edge of the centre column
PLATE_EDGE = 8.0                  # outer edge of a full-width row

# (y, h, first x, cuts, deck-A names, deck-B names, centre name)
GROUP_ROWS = [
    # The status row starts at KEEP_TOP, not at 8: its legend straddles the
    # box edge, and at y=8 the lettering would print under the rail.
    (9.00, 16.0, 28.00, [85.20],
     ["SEQUENCE", "CAPTURE"], ["SEQUENCE", "CAPTURE"], "GLOBAL"),
    (28.00, 38.2, PLATE_EDGE, [56.25],
     ["MOTION", "VOICE"], ["MOTION", "VOICE"], "TIMING"),
    (69.20, 38.2, PLATE_EDGE, [42.00, 92.45],
     ["PITCH", "FLUX", "LEVEL"], ["PITCH", "FLUX", "LEVEL"], "ROOM"),
    (110.40, 15.0, 28.00, [50.25, 73.25],
     ["IN", "CV A", "MOD A"], ["OUT", "CV B", "MOD B"], "CLOCK"),
]

# Legend numbering, in reading order: decks top to bottom, then the centre
# column, then the jack row.
GROUP_ORDER = ("SEQUENCE", "CAPTURE", "MOTION", "VOICE", "PITCH", "FLUX",
               "LEVEL", "GLOBAL", "TIMING", "ROOM", "IN", "CV", "MOD",
               "CLOCK", "OUT")


JACK_ROW_Y = GROUP_ROWS[-1][0]
LEGEND_SIZE, LEGEND_SPACING = 1.9, 0.5


class Box:
    __slots__ = ("n", "side", "x", "y", "w", "h")

    def __init__(self, n, side, x, y, w, h):
        self.n, self.side, self.x, self.y, self.w, self.h = n, side, x, y, w, h

    @property
    def idx(self):
        stem = self.n[:-2] if self.n.endswith((" A", " B")) else self.n
        return GROUP_ORDER.index(stem) + 1

    @property
    def legend_y(self):
        """Baseline of the group legend.

        Rows 1-3 straddle the frame's top edge, as drawn in the design. The
        jack row cannot: a PJ301M is 8.03 mm across, so its widget reaches
        up to y=109.99 and would bury a legend sitting at 111.15 -- visible
        in Rack, invisible in the SVG, where the jack body is only 6.2 mm.
        So that one row's legend rides just above its frame instead."""
        return self.y - 0.80 if self.y == JACK_ROW_Y else self.y + 0.75

    @property
    def legend_straddles(self):
        return self.y != JACK_ROW_Y


def group_boxes():
    """The 24 drawing frames, left to right within each row."""
    out = []
    for y, h, x0, cuts, names_a, names_b, centre in GROUP_ROWS:
        edges = [x0] + list(cuts) + [DECK_EDGE]
        for i, n in enumerate(names_a):
            lo = edges[i] + (BOX_GAP / 2.0 if i else 0.0)
            hi = edges[i + 1] - (BOX_GAP / 2.0 if i + 1 < len(names_a) else 0.0)
            out.append(Box(n, "A", lo, y, hi - lo, h))
        out.append(Box(centre, "C", CENTRE_L, y, W - 2 * CENTRE_L, h))
        for i, n in enumerate(names_b):
            lo = edges[i] + (BOX_GAP / 2.0 if i else 0.0)
            hi = edges[i + 1] - (BOX_GAP / 2.0 if i + 1 < len(names_a) else 0.0)
            out.append(Box(n, "B", W - hi, y, hi - lo, h))
    return out


BOXES = group_boxes()


def body_r(c):
    """Radius of the real component body, not the layout clearance circle."""
    return BODY_R[hw_class(c.enum)]


def box_of(c):
    """The frame a control's centre falls in, or None (SHFT/MOD sit loose)."""
    for b in BOXES:
        if b.x <= c.x <= b.x + b.w and b.y <= c.y <= b.y + b.h:
            return b
    return None


def _legend_w(name):
    """Knockout width for a two-part legend: index, 1 mm lead, name, trail."""
    per = LEGEND_SIZE * FONT_ADVANCE + LEGEND_SPACING
    return len(name) * per + (8.4 - 3.0) + 1.0


def _groups_svg():
    out = []
    for b in BOXES:
        acc = ACC[b.side]
        out.append(f'<rect x="{mm(b.x)}" y="{mm(b.y)}" width="{mm(b.w)}" '
                   f'height="{mm(b.h)}" fill="{BOX_FILL}" '
                   f'fill-opacity="{BOX_FILL_OPACITY}" stroke="{BOX_STROKE}" '
                   f'stroke-width="0.25" stroke-dasharray="{BOX_DASH}"/>')
        for cx, cy, sx, sy in ((b.x, b.y, 1, 1), (b.x + b.w, b.y, -1, 1),
                               (b.x, b.y + b.h, 1, -1),
                               (b.x + b.w, b.y + b.h, -1, -1)):
            out.append(f'<path fill="none" stroke="{acc}" stroke-width="0.45" '
                       f'stroke-opacity="0.9" d="M{mm(cx + sx * BOX_BRACKET)},'
                       f'{mm(cy)} L{mm(cx)},{mm(cy)} L{mm(cx)},'
                       f'{mm(cy + sy * BOX_BRACKET)}"/>')
        if b.legend_straddles:
            out.append(f'<rect x="{mm(b.x + 3.0)}" y="{mm(b.y - 1.5)}" '
                       f'width="{mm(_legend_w(b.n))}" height="3.000" '
                       f'fill="{KNOCKOUT}"/>')
    return out


def group_texts():
    """Legends as PanelTxt rows. Rack does not render SVG text, so the plate
    lettering and the rehearsal lettering must both come from this table."""
    out = []
    for b in BOXES:
        out.append((b.x + 4.0, b.legend_y, LEGEND_SIZE, LEGEND_SPACING,
                    IDX_COL[b.side], "start", f"{b.idx:02d}"))
        out.append((b.x + 8.4, b.legend_y, LEGEND_SIZE, LEGEND_SPACING,
                    HW_LEGEND, "start", b.n))
    return out


BRAND_TEXTS = [
    (4.50, 9.40, 3.3, 0.55, HW_LABEL, "start", "FIREFLOW"),
    (4.60, 14.60, 2.1, 0.55, ACC["A"], "start", "DECK A"),
    (W - 4.50, 9.40, 2.3, 0.55, HW_LEGEND, "end", "60 HP"),
    (W - 4.60, 14.60, 2.1, 0.55, ACC["B"], "end", "DECK B"),
]

TEXTS[:] = BRAND_TEXTS + group_texts()


# =============================================================================
#  Plate background: three zones, one printed airflow/ember silhouette
# =============================================================================
# Airflow (deck A) and ember (deck B, the same artwork mirrored) as a print
# under the frames, not as an illustration on top of them. Both come from the
# design document "Fireflow Faceplate" (15 Aug), traced at panel scale.
WIND = [
    ("M-14.0,8.7C-13.0,9.2 -10.0,10.8 -8.0,12.1C-6.0,13.3 -4.0,14.9 -2.0,16.4C0.0,17.9 2.0,19.6 4.0,21.1C6.0,22.6 8.0,24.0 10.0,25.2C12.0,26.4 14.0,27.4 16.0,28.1C18.0,28.8 20.0,29.2 22.0,29.2C24.0,29.3 26.0,29.0 28.0,28.3C30.0,27.7 32.0,26.7 34.0,25.5C36.0,24.3 38.0,22.7 40.0,21.1C42.0,19.5 44.0,17.6 46.0,15.7C48.0,13.9 50.0,11.9 52.0,10.2C54.0,8.4 56.0,6.7 58.0,5.2C60.0,3.8 62.0,2.5 64.0,1.5C66.0,0.6 68.0,-0.0 70.0,-0.3C72.0,-0.6 72.5,0.6 76.0,-0.2C79.5,-1.1 87.7,-5.1 90.9,-5.4C94.2,-5.8 94.3,-3.5 95.5,-2.3C96.7,-1.1 97.7,0.3 98.2,1.6C98.7,2.9 98.9,4.3 98.8,5.6C98.6,6.9 98.1,8.2 97.4,9.3C96.8,10.4 95.7,11.4 94.6,12.1C93.5,12.9 92.2,13.5 90.9,13.9C89.6,14.3 88.2,14.5 86.9,14.5C85.6,14.6 84.3,14.4 83.2,14.1C82.1,13.8 81.1,13.3 80.4,12.8C79.6,12.3 79.0,11.6 78.6,11.0C78.2,10.4 78.0,9.7 77.9,9.1C77.9,8.5 78.1,7.9 78.4,7.4C78.6,6.9 79.3,6.4 79.5,6.2", 1.90),
    ("M-14.0,53.4C-13.0,53.7 -10.0,54.8 -8.0,55.1C-6.0,55.4 -4.0,55.4 -2.0,55.2C0.0,55.0 2.0,54.5 4.0,53.9C6.0,53.3 8.0,52.4 10.0,51.4C12.0,50.5 14.0,49.3 16.0,48.2C18.0,47.1 20.0,45.9 22.0,44.8C24.0,43.8 26.0,42.7 28.0,41.9C30.0,41.1 32.0,40.4 34.0,40.0C36.0,39.5 38.0,39.3 40.0,39.4C42.0,39.4 44.0,39.7 46.0,40.3C48.0,40.8 50.0,41.6 52.0,42.6C54.0,43.6 56.0,44.8 58.0,46.1C60.0,47.4 62.0,48.9 64.0,50.3C66.0,51.7 68.0,53.2 70.0,54.6C72.0,55.9 72.7,56.9 76.0,58.3C79.3,59.7 86.6,62.7 89.5,63.0C92.5,63.4 92.6,61.3 93.7,60.2C94.8,59.2 95.7,57.9 96.1,56.7C96.6,55.5 96.8,54.2 96.7,53.0C96.6,51.9 96.1,50.7 95.5,49.7C94.8,48.7 93.9,47.8 92.9,47.1C91.9,46.4 90.7,45.9 89.5,45.5C88.4,45.1 87.1,45.0 85.9,44.9C84.7,44.9 83.6,45.1 82.6,45.4C81.6,45.6 80.7,46.1 79.9,46.5C79.2,47.0 78.7,47.6 78.3,48.2C78.0,48.7 77.8,49.3 77.8,49.9C77.7,50.4 77.9,51.0 78.1,51.4C78.4,51.9 79.0,52.3 79.2,52.5", 1.65),
    ("M-14.0,76.2C-13.0,75.4 -10.0,72.8 -8.0,71.2C-6.0,69.6 -4.0,68.0 -2.0,66.7C0.0,65.4 2.0,64.2 4.0,63.3C6.0,62.5 8.0,61.8 10.0,61.6C12.0,61.3 14.0,61.3 16.0,61.6C18.0,62.0 20.0,62.6 22.0,63.4C24.0,64.3 26.0,65.4 28.0,66.6C30.0,67.8 32.0,69.3 34.0,70.6C36.0,72.0 38.0,73.5 40.0,74.7C42.0,76.0 44.0,77.3 46.0,78.2C48.0,79.2 50.0,80.1 52.0,80.6C54.0,81.1 56.0,81.3 58.0,81.2C60.0,81.1 62.0,80.7 64.0,80.1C66.0,79.4 68.0,78.4 70.0,77.2C72.0,76.0 73.0,74.4 76.0,73.0C79.0,71.6 85.5,69.1 88.2,68.8C90.8,68.5 90.9,70.4 91.9,71.3C92.9,72.3 93.7,73.4 94.1,74.5C94.5,75.6 94.7,76.7 94.6,77.8C94.5,78.8 94.1,79.9 93.5,80.8C92.9,81.7 92.1,82.5 91.2,83.1C90.3,83.7 89.2,84.2 88.2,84.6C87.1,84.9 85.9,85.0 84.9,85.1C83.9,85.1 82.8,84.9 81.9,84.7C81.0,84.4 80.2,84.0 79.5,83.6C78.9,83.2 78.4,82.7 78.1,82.2C77.8,81.7 77.6,81.1 77.6,80.6C77.6,80.1 77.7,79.6 77.9,79.2C78.1,78.8 78.7,78.4 78.9,78.2", 1.40),
    ("M-14.0,93.5C-13.0,93.8 -10.0,94.7 -8.0,95.5C-6.0,96.3 -4.0,97.3 -2.0,98.4C0.0,99.4 2.0,100.6 4.0,101.8C6.0,102.9 8.0,104.2 10.0,105.3C12.0,106.4 14.0,107.5 16.0,108.4C18.0,109.3 20.0,110.1 22.0,110.7C24.0,111.3 26.0,111.7 28.0,111.9C30.0,112.1 32.0,112.1 34.0,111.9C36.0,111.8 38.0,111.3 40.0,110.8C42.0,110.3 44.0,109.5 46.0,108.8C48.0,108.0 50.0,107.1 52.0,106.3C54.0,105.4 56.0,104.5 58.0,103.7C60.0,102.9 62.0,102.1 64.0,101.5C66.0,101.0 68.0,100.5 70.0,100.2C72.0,100.0 73.2,99.5 76.0,100.0C78.8,100.6 84.4,103.6 86.8,103.8C89.1,104.1 89.2,102.4 90.1,101.6C91.0,100.7 91.7,99.7 92.0,98.8C92.4,97.8 92.6,96.8 92.5,95.8C92.4,94.9 92.0,94.0 91.5,93.2C91.0,92.4 90.3,91.7 89.5,91.1C88.7,90.6 87.7,90.1 86.8,89.8C85.9,89.5 84.8,89.4 83.9,89.4C83.0,89.4 82.0,89.5 81.2,89.7C80.4,89.9 79.7,90.3 79.1,90.7C78.6,91.0 78.1,91.5 77.9,91.9C77.6,92.4 77.4,92.9 77.4,93.3C77.4,93.8 77.5,94.2 77.7,94.6C77.9,94.9 78.4,95.3 78.5,95.4", 1.15),
    ("M-6.0,28.4C-5.0,29.0 -2.0,30.8 0.0,32.0C2.0,33.2 4.0,34.5 6.0,35.6C8.0,36.6 10.0,37.7 12.0,38.5C14.0,39.3 16.0,40.0 18.0,40.4C20.0,40.7 22.0,40.9 24.0,40.8C26.0,40.6 28.0,40.2 30.0,39.6C32.0,39.0 34.0,38.1 36.0,37.0C38.0,36.0 40.0,34.7 42.0,33.4C44.0,32.1 46.0,30.6 48.0,29.2C50.0,27.8 52.0,26.3 54.0,25.1C56.0,23.8 59.0,22.1 60.0,21.6", 0.70),
    ("M-6.0,95.5C-5.0,95.0 -2.0,93.8 0.0,92.8C2.0,91.7 4.0,90.5 6.0,89.3C8.0,88.1 10.0,86.8 12.0,85.7C14.0,84.6 16.0,83.4 18.0,82.5C20.0,81.6 22.0,80.9 24.0,80.4C26.0,79.9 28.0,79.6 30.0,79.6C32.0,79.6 34.0,79.8 36.0,80.3C38.0,80.8 40.0,81.6 42.0,82.6C44.0,83.5 46.0,84.7 48.0,86.0C50.0,87.3 52.0,88.7 54.0,90.1C56.0,91.5 59.0,93.6 60.0,94.3", 0.70),
]

FIRE = [
    ("M61.9,124.0C61.1,123.3 58.8,121.1 57.2,119.7C55.7,118.3 54.2,116.8 52.7,115.4C51.2,113.9 49.7,112.5 48.3,111.1C46.9,109.6 45.4,108.2 44.1,106.8C42.7,105.3 41.4,103.9 40.1,102.5C38.9,101.0 37.6,99.6 36.5,98.2C35.3,96.7 34.2,95.3 33.2,93.8C32.1,92.4 31.2,91.0 30.3,89.5C29.4,88.1 28.5,86.7 27.8,85.2C27.1,83.8 26.4,82.4 25.8,80.9C25.2,79.5 24.7,78.1 24.3,76.6C23.9,75.2 23.6,73.7 23.4,72.3C23.2,70.9 23.0,69.4 23.0,68.0C23.0,66.6 23.0,65.1 23.2,63.7C23.3,62.3 23.6,60.8 23.9,59.4C24.2,57.9 24.6,56.5 25.2,55.1C25.7,53.6 26.3,52.2 27.0,50.8C27.7,49.3 28.4,47.9 29.3,46.5C30.1,45.0 31.1,43.6 32.1,42.2C33.1,40.7 34.2,39.3 35.3,37.8C36.5,36.4 37.7,35.0 39.0,33.5C40.3,32.1 41.6,30.7 43.0,29.2C44.4,27.8 45.8,26.4 47.3,24.9C48.7,23.5 50.2,22.1 51.8,20.6C53.3,19.2 54.9,17.7 56.4,16.3C58.0,14.9 58.6,12.7 61.2,12.0C63.8,11.3 69.7,11.3 72.0,12.0C74.3,12.7 73.9,14.9 74.8,16.3C75.7,17.7 76.6,19.2 77.5,20.6C78.4,22.1 79.3,23.5 80.1,24.9C81.0,26.4 81.8,27.8 82.6,29.2C83.4,30.7 84.1,32.1 84.8,33.5C85.6,35.0 86.2,36.4 86.9,37.8C87.5,39.3 88.1,40.7 88.7,42.2C89.2,43.6 89.7,45.0 90.2,46.5C90.7,47.9 91.1,49.3 91.5,50.8C91.8,52.2 92.2,53.6 92.4,55.1C92.7,56.5 93.0,57.9 93.1,59.4C93.3,60.8 93.4,62.3 93.5,63.7C93.6,65.1 93.6,66.6 93.6,68.0C93.6,69.4 93.5,70.9 93.3,72.3C93.2,73.7 93.0,75.2 92.8,76.6C92.5,78.1 92.2,79.5 91.8,80.9C91.4,82.4 91.0,83.8 90.5,85.2C90.0,86.7 89.5,88.1 88.9,89.5C88.3,91.0 87.6,92.4 86.9,93.8C86.1,95.3 85.3,96.7 84.4,98.2C83.6,99.6 82.7,101.0 81.7,102.5C80.7,103.9 79.6,105.3 78.5,106.8C77.4,108.2 76.2,109.6 75.0,111.1C73.7,112.5 72.4,113.9 71.1,115.4C69.7,116.8 68.3,118.3 66.9,119.7C65.4,121.1 63.1,123.3 62.4,124.0Z", 1.90),
    ("M42.3,124.0C41.9,123.5 40.4,122.1 39.5,121.2C38.5,120.2 37.6,119.3 36.8,118.3C35.9,117.4 35.1,116.4 34.3,115.5C33.5,114.5 32.8,113.6 32.1,112.6C31.4,111.7 30.7,110.7 30.1,109.8C29.6,108.8 29.0,107.9 28.5,106.9C28.0,106.0 27.6,105.0 27.2,104.1C26.8,103.1 26.5,102.2 26.3,101.2C26.0,100.3 25.8,99.3 25.6,98.4C25.5,97.4 25.4,96.5 25.4,95.5C25.3,94.6 25.3,93.6 25.4,92.7C25.5,91.7 25.6,90.8 25.8,89.8C26.0,88.9 26.2,87.9 26.5,87.0C26.8,86.1 27.1,85.1 27.5,84.2C27.9,83.2 28.3,82.3 28.8,81.3C29.2,80.4 29.8,79.4 30.3,78.5C30.8,77.5 31.4,76.6 32.0,75.6C32.7,74.7 33.3,73.7 34.0,72.8C34.7,71.8 35.4,70.9 36.1,69.9C36.8,69.0 37.6,68.0 38.3,67.1C39.1,66.1 39.9,65.2 40.7,64.2C41.5,63.3 42.3,62.3 43.2,61.4C44.0,60.4 44.8,59.5 45.7,58.5C46.5,57.6 47.4,56.6 48.2,55.7C49.1,54.7 50.0,53.8 50.8,52.8C51.7,51.9 52.0,50.5 53.4,50.0C54.8,49.5 58.1,49.5 59.2,50.0C60.4,50.5 59.8,51.9 60.1,52.8C60.3,53.8 60.6,54.7 60.8,55.7C61.0,56.6 61.2,57.6 61.5,58.5C61.7,59.5 61.8,60.4 62.0,61.4C62.2,62.3 62.3,63.3 62.5,64.2C62.6,65.2 62.7,66.1 62.8,67.1C62.9,68.0 63.0,69.0 63.1,69.9C63.2,70.9 63.2,71.8 63.3,72.8C63.3,73.7 63.4,74.7 63.4,75.6C63.4,76.6 63.4,77.5 63.4,78.5C63.4,79.4 63.4,80.4 63.4,81.3C63.3,82.3 63.3,83.2 63.2,84.2C63.2,85.1 63.1,86.1 63.0,87.0C63.0,87.9 62.9,88.9 62.7,89.8C62.6,90.8 62.5,91.7 62.3,92.7C62.2,93.6 62.0,94.6 61.8,95.5C61.6,96.5 61.4,97.4 61.2,98.4C60.9,99.3 60.7,100.3 60.4,101.2C60.1,102.2 59.8,103.1 59.4,104.1C59.0,105.0 58.7,106.0 58.2,106.9C57.8,107.9 57.3,108.8 56.9,109.8C56.4,110.7 55.8,111.7 55.2,112.6C54.7,113.6 54.1,114.5 53.4,115.5C52.8,116.4 52.1,117.4 51.3,118.3C50.6,119.3 49.9,120.2 49.1,121.2C48.3,122.1 47.0,123.5 46.6,124.0Z", 1.30),
    ("M83.4,124.0C83.1,123.6 82.2,122.4 81.5,121.6C80.9,120.8 80.3,120.0 79.6,119.2C79.0,118.4 78.3,117.6 77.7,116.8C77.0,116.1 76.4,115.3 75.8,114.5C75.1,113.7 74.5,112.9 73.9,112.1C73.2,111.3 72.6,110.5 72.0,109.7C71.4,108.9 70.9,108.1 70.3,107.3C69.7,106.5 69.2,105.7 68.6,104.9C68.1,104.1 67.6,103.3 67.1,102.5C66.6,101.7 66.2,100.9 65.7,100.2C65.3,99.4 64.9,98.6 64.5,97.8C64.2,97.0 63.8,96.2 63.5,95.4C63.2,94.6 62.9,93.8 62.7,93.0C62.4,92.2 62.2,91.4 62.1,90.6C61.9,89.8 61.8,89.0 61.7,88.2C61.6,87.4 61.5,86.6 61.5,85.8C61.5,85.1 61.5,84.3 61.6,83.5C61.6,82.7 61.7,81.9 61.9,81.1C62.0,80.3 62.2,79.5 62.4,78.7C62.6,77.9 62.8,77.1 63.1,76.3C63.4,75.5 63.7,74.7 64.0,73.9C64.3,73.1 64.7,72.3 65.1,71.5C65.5,70.7 65.9,69.9 66.3,69.2C66.7,68.4 67.2,67.6 67.6,66.8C68.1,66.0 68.6,65.2 69.0,64.4C69.5,63.6 69.1,62.4 70.5,62.0C71.9,61.6 76.1,61.6 77.5,62.0C79.0,62.4 78.7,63.6 79.3,64.4C79.9,65.2 80.5,66.0 81.0,66.8C81.5,67.6 82.1,68.4 82.6,69.2C83.1,69.9 83.5,70.7 84.0,71.5C84.5,72.3 84.9,73.1 85.3,73.9C85.8,74.7 86.2,75.5 86.6,76.3C87.0,77.1 87.4,77.9 87.7,78.7C88.1,79.5 88.5,80.3 88.8,81.1C89.2,81.9 89.5,82.7 89.8,83.5C90.1,84.3 90.4,85.1 90.7,85.8C91.0,86.6 91.3,87.4 91.6,88.2C91.9,89.0 92.1,89.8 92.4,90.6C92.6,91.4 92.8,92.2 93.0,93.0C93.3,93.8 93.4,94.6 93.6,95.4C93.8,96.2 93.9,97.0 94.1,97.8C94.2,98.6 94.3,99.4 94.4,100.2C94.4,100.9 94.5,101.7 94.5,102.5C94.5,103.3 94.5,104.1 94.4,104.9C94.4,105.7 94.3,106.5 94.1,107.3C94.0,108.1 93.8,108.9 93.6,109.7C93.4,110.5 93.1,111.3 92.8,112.1C92.5,112.9 92.1,113.7 91.7,114.5C91.3,115.3 90.8,116.1 90.3,116.8C89.8,117.6 89.3,118.4 88.7,119.2C88.1,120.0 87.4,120.8 86.7,121.6C86.1,122.4 84.9,123.6 84.6,124.0Z", 1.30),
    ("M64.0,124.0C63.7,123.7 62.9,122.8 62.3,122.2C61.7,121.6 61.2,121.1 60.6,120.5C60.0,119.9 59.5,119.3 58.9,118.7C58.4,118.1 57.8,117.5 57.3,116.9C56.7,116.3 56.2,115.7 55.7,115.2C55.2,114.6 54.7,114.0 54.3,113.4C53.8,112.8 53.4,112.2 53.0,111.6C52.6,111.0 52.2,110.4 51.8,109.8C51.5,109.3 51.2,108.7 50.9,108.1C50.6,107.5 50.4,106.9 50.2,106.3C50.0,105.7 49.8,105.1 49.7,104.5C49.5,103.9 49.4,103.4 49.4,102.8C49.3,102.2 49.3,101.6 49.4,101.0C49.4,100.4 49.5,99.8 49.6,99.2C49.7,98.6 49.9,98.1 50.1,97.5C50.3,96.9 50.5,96.3 50.8,95.7C51.1,95.1 51.4,94.5 51.8,93.9C52.1,93.3 52.5,92.7 53.0,92.2C53.4,91.6 53.8,91.0 54.3,90.4C54.8,89.8 55.3,89.2 55.9,88.6C56.4,88.0 57.0,87.4 57.5,86.8C58.1,86.3 58.7,85.7 59.3,85.1C59.9,84.5 60.6,83.9 61.2,83.3C61.8,82.7 62.5,82.1 63.1,81.5C63.7,80.9 64.4,80.4 65.0,79.8C65.7,79.2 66.7,78.3 66.9,78.0C67.2,77.7 66.3,77.7 66.4,78.0C66.6,78.3 67.2,79.2 67.6,79.8C68.0,80.4 68.5,80.9 68.9,81.5C69.3,82.1 69.7,82.7 70.2,83.3C70.6,83.9 71.0,84.5 71.4,85.1C71.8,85.7 72.2,86.3 72.6,86.8C73.0,87.4 73.3,88.0 73.7,88.6C74.0,89.2 74.3,89.8 74.6,90.4C74.9,91.0 75.1,91.6 75.3,92.2C75.6,92.7 75.7,93.3 75.9,93.9C76.0,94.5 76.1,95.1 76.2,95.7C76.2,96.3 76.2,96.9 76.2,97.5C76.2,98.1 76.1,98.6 76.0,99.2C75.9,99.8 75.8,100.4 75.6,101.0C75.4,101.6 75.2,102.2 75.0,102.8C74.7,103.4 74.4,103.9 74.1,104.5C73.8,105.1 73.5,105.7 73.1,106.3C72.7,106.9 72.3,107.5 71.9,108.1C71.5,108.7 71.1,109.3 70.6,109.8C70.2,110.4 69.7,111.0 69.3,111.6C68.8,112.2 68.4,112.8 67.9,113.4C67.4,114.0 66.9,114.6 66.5,115.2C66.0,115.7 65.5,116.3 65.1,116.9C64.6,117.5 64.2,118.1 63.7,118.7C63.3,119.3 62.8,119.9 62.4,120.5C62.0,121.1 61.6,121.6 61.2,122.2C60.8,122.8 60.2,123.7 60.0,124.0Z", 0.90),
    ("M41.0,44.0C40.9,44.3 40.6,45.1 40.1,45.6C39.7,46.0 39.0,46.4 38.4,46.6C37.8,46.8 37.0,46.9 36.3,46.9C35.6,46.9 34.9,46.7 34.4,46.5C33.9,46.2 33.4,45.9 33.1,45.5C32.8,45.2 32.7,44.7 32.7,44.3C32.7,44.0 32.9,43.5 33.1,43.2C33.4,42.9 33.8,42.7 34.2,42.5C34.6,42.4 35.1,42.3 35.5,42.3C35.9,42.3 36.3,42.4 36.6,42.5C36.9,42.6 37.2,42.8 37.4,43.0C37.5,43.2 37.6,43.5 37.6,43.7C37.6,43.9 37.4,44.1 37.4,44.2", 0.70),
    ("M90.0,38.0C89.9,38.2 89.7,38.9 89.3,39.3C89.0,39.6 88.4,39.9 87.9,40.1C87.4,40.3 86.8,40.3 86.2,40.3C85.7,40.3 85.1,40.2 84.7,40.0C84.3,39.8 83.9,39.5 83.7,39.2C83.5,38.9 83.4,38.6 83.4,38.3C83.4,38.0 83.5,37.6 83.7,37.4C83.9,37.2 84.2,37.0 84.5,36.8C84.9,36.7 85.3,36.6 85.6,36.6C85.9,36.6 86.3,36.7 86.5,36.8C86.8,36.9 87.0,37.1 87.1,37.2C87.2,37.4 87.3,37.6 87.3,37.7C87.3,37.9 87.1,38.1 87.1,38.2", 0.70),
    ("M67.2,22.0C67.1,22.2 66.9,22.7 66.7,23.0C66.4,23.3 65.9,23.5 65.5,23.7C65.1,23.8 64.6,23.9 64.2,23.9C63.8,23.8 63.3,23.7 63.0,23.6C62.6,23.4 62.3,23.2 62.1,23.0C62.0,22.7 61.9,22.5 61.9,22.2C61.9,22.0 62.0,21.7 62.2,21.5C62.3,21.3 62.6,21.2 62.8,21.1C63.1,21.0 63.4,20.9 63.7,20.9C63.9,20.9 64.2,21.0 64.4,21.0C64.6,21.1 64.8,21.3 64.9,21.4C65.0,21.5 65.0,21.7 65.0,21.8C65.0,21.9 64.9,22.1 64.9,22.1", 0.70),
]

SILHOUETTE_OPACITY = 0.09


def _silhouette_svg():
    """Airflow on deck A, ember on deck B (same paths, mirrored).

    The design fades these with an SVG mask. NanoSVG -- what Rack parses --
    ignores <mask> entirely and would print the lines at full strength right
    across the centre gutter, so the fade is baked as a gradient of the zone
    colour laid OVER the print instead. Same picture in both renderers."""
    out = []
    for tag, paths, stroke, mirror in (("wind", WIND, "#79d8e0", False),
                                       ("fire", FIRE, "#ff8a3c", True)):
        xf = f' transform="translate({mm(W)},0) scale(-1,1)"' if mirror else ""
        out.append(f'<g fill="none" stroke="{stroke}" stroke-linecap="round" '
                   f'stroke-linejoin="round" stroke-opacity="{SILHOUETTE_OPACITY}"'
                   f'{xf}>')
        for d, w in paths:
            out.append(f'<path d="{d}" stroke-width="{w:.2f}"/>')
        out.append("</g>")
    return out


def _plate_svg():
    """Plate, three zones, printed silhouette, then the baked fades."""
    out = [f'<rect x="0" y="0" width="{mm(W)}" height="{mm(Hh)}" '
           f'fill="url(#hw-plate)"/>',
           f'<rect x="0" y="0" width="{mm(ZONE_A)}" height="{mm(Hh)}" '
           f'fill="url(#hw-zoneA)"/>',
           f'<rect x="{mm(ZONE_A)}" y="0" width="{mm(W - 2 * ZONE_A)}" '
           f'height="{mm(Hh)}" fill="url(#hw-zoneC)"/>',
           f'<rect x="{mm(W - ZONE_A)}" y="0" width="{mm(ZONE_A)}" '
           f'height="{mm(Hh)}" fill="url(#hw-zoneB)"/>']
    out.extend(_silhouette_svg())
    # Baked mask: strong at the outer edge, open across the deck, closing
    # again before the centre gutter.
    out.append(f'<rect x="0" y="0" width="{mm(ZONE_A)}" height="{mm(Hh)}" '
               f'fill="url(#hw-fadeA)"/>')
    out.append(f'<rect x="{mm(W - ZONE_A)}" y="0" width="{mm(ZONE_A)}" '
               f'height="{mm(Hh)}" fill="url(#hw-fadeB)"/>')
    # Zone seams: the deck tint dissolves into the centre rather than ending
    # on a vertical line.
    out.append(f'<rect x="{mm(ZONE_A - SEAM_W / 2)}" y="0" width="{mm(SEAM_W)}" '
               f'height="{mm(Hh)}" fill="url(#hw-seamL)"/>')
    out.append(f'<rect x="{mm(W - ZONE_A - SEAM_W / 2)}" y="0" '
               f'width="{mm(SEAM_W)}" height="{mm(Hh)}" fill="url(#hw-seamR)"/>')
    return out


def _defs_svg():
    """Every gradient in millimetres, never in bounding-box fractions.

    Measured, not assumed: with the design's objectBoundingBox gradients,
    Rack drew the ember half of the print and dropped the airflow half
    entirely -- the fadeA overlay came out opaque across the whole of deck A
    while its mirror image behaved. The browser showed both. userSpaceOnUse
    takes the question off the table, so the plate cannot mean two different
    things in the preview and on the module."""
    def lin(name, vec, stops):
        x1, y1, x2, y2 = vec
        s = "".join(f'<stop offset="{o}" stop-color="{c}" stop-opacity="{a}"/>'
                    for o, c, a in stops)
        return (f'<linearGradient id="hw-{name}" gradientUnits="userSpaceOnUse" '
                f'x1="{mm(x1)}" y1="{mm(y1)}" x2="{mm(x2)}" y2="{mm(y2)}">'
                f'{s}</linearGradient>')

    def fade(name, vec, col):
        return lin(name, vec, [(0, col, 0.85), (0.35, col, 0),
                               (0.86, col, 0.45), (1, col, 1)])

    tilt = 0.35 * Hh                       # the design's y2="0.35" on a zone
    return ["<defs>",
            lin("plate", (0, 0, 0, Hh), [(0, PLATE_HI, 1), (1, PLATE_LO, 1)]),
            lin("zoneA", (0, 0, ZONE_A, tilt),
                [(0, ZONE_A_HI, 1), (1, ZONE_A_LO, 1)]),
            lin("zoneC", (ZONE_A, 0, ZONE_A, Hh),
                [(0, ZONE_C_HI, 1), (1, ZONE_C_LO, 1)]),
            lin("zoneB", (W, 0, W - ZONE_A, tilt),
                [(0, ZONE_B_HI, 1), (1, ZONE_B_LO, 1)]),
            fade("fadeA", (0, 0, ZONE_A, 0), ZONE_MID["A"]),
            fade("fadeB", (W, 0, W - ZONE_A, 0), ZONE_MID["B"]),
            lin("seamL", (ZONE_A - SEAM_W / 2, 0, ZONE_A + SEAM_W / 2, 0),
                [(0, ZONE_C_HI, 0), (1, ZONE_C_HI, 1)]),
            lin("seamR", (W - ZONE_A + SEAM_W / 2, 0,
                          W - ZONE_A - SEAM_W / 2, 0),
                [(0, ZONE_C_HI, 0), (1, ZONE_C_HI, 1)]),
            "</defs>"]


# =============================================================================
#  SVG
# =============================================================================
def mm(v): return f"{v:.3f}"

def svg():
    P = []
    P.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{mm(W)}mm" '
              f'height="{mm(Hh)}mm" viewBox="0 0 {mm(W)} {mm(Hh)}">')
    P.extend(_defs_svg())
    P.extend(_plate_svg())
    # Plate edge highlight. Hex + stroke-opacity, never rgba(): NanoSVG's
    # colour parser is not the browser's.
    P.append(f'<rect x="0.4" y="0.4" width="{mm(W-0.8)}" height="{mm(Hh-0.8)}" '
              f'rx="1.2" fill="none" stroke="#ffffff" stroke-opacity="0.10" '
              f'stroke-width="0.3"/>')
    P.extend(_groups_svg())
    # SD slot: a body on the jack row, drawn like one.
    P.append(f'<rect x="{mm(SD_X - SD_W / 2)}" y="{mm(SD_Y - SD_H / 2)}" '
              f'width="{mm(SD_W)}" height="{mm(SD_H)}" rx="0.8" '
              f'fill="{HW_WELL}" stroke="{HW_RING}" stroke-width="0.3"/>')
    P.append(f'<rect x="{mm(SD_X - SD_W / 2 + 1.0)}" y="{mm(SD_Y - SD_H / 2 + 1.2)}" '
              f'width="{mm(SD_W - 2.0)}" height="{mm(SD_H - 2.4)}" rx="0.4" '
              f'fill="none" stroke="{JACK_METAL}" stroke-width="0.25" '
              f'stroke-opacity="0.6"/>')
    for c in ALL_HW:
        br = body_r(c)
        if c.kind in (gp.IN, gp.OUT):
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(br)}" '
                      f'fill="{HW_WELL}" stroke="{HW_RING}" stroke-width="0.3"/>')
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(br - 1.1)}" '
                      f'fill="none" stroke="{JACK_METAL}" stroke-width="0.7"/>')
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(br * 0.38)}" '
                      f'fill="#050607"/>')
        elif c.kind == gp.LIGHT:
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(br)}" '
                      f'fill="{LED_OFF}" stroke="{HW_RING2}" stroke-width="0.25"/>')
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="0.750" '
                      f'fill="{LED_ON[zone_of(c.x)]}"/>')
        elif hw_class(c.enum) == "P":
            P.append(f'<rect x="{mm(c.x-c.r)}" y="{mm(c.y-c.r)}" width="{mm(2*c.r)}" '
                      f'height="{mm(2*c.r)}" rx="1.2" fill="{PAD_FILL}" '
                      f'stroke="{PAD_STROKE}" stroke-width="0.3"/>')
        else:
            # The mounting hole, drawn at the real pot body -- not a cap. Rack
            # puts its own knob widget on top and a plate has a hole here.
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(br)}" '
                      f'fill="{HW_WELL}" stroke="{HW_RING}" stroke-width="0.3"/>')
        if c.label:
            lx, ly, anchor, size, colour = hw_label(c)
            P.append(f'<text x="{mm(lx)}" y="{mm(ly)}" fill="{colour}" '
                      f'text-anchor="{anchor}" font-family="monospace" '
                      f'font-size="{size}">{c.label}</text>')
    for (x, y, size, spacing, col, anchor, txt) in TEXTS:
        P.append(f'<text x="{mm(x)}" y="{mm(y)}" fill="{col}" text-anchor="{anchor}" '
                  f'font-family="monospace" font-size="{size}" '
                  f'letter-spacing="{spacing}">{txt}</text>')
    P.append('</svg>')
    return "\n".join(P)

# =============================================================================
#  C++ header
# =============================================================================
def rgb(hexcol): return "0x" + hexcol.lstrip("#").upper()

ANCHOR_ID = {"middle": 0, "start": 1, "end": 2}

def emit_table(name, items):
    L2 = [f"static const PanelCtl {name}[] = {{"]
    for c in items:
        lx, ly, anchor, size, colour = hw_label(c)
        L2.append(f'    {{{c.enum}, {gp.WKMAP[c.kind]}, {{{c.x:.3f}f, {c.y:.3f}f}}, '
                  f'"{c.label}", {{{lx:.3f}f, {ly:.3f}f}}, {ANCHOR_ID[anchor]}, '
                  f'{size:.2f}f, {rgb(colour)}, "{c.tip}"}},')
    L2.append("};")
    return L2

def header():
    L2 = []
    L2.append("// GENERATED by res/gen_hw_panel.py -- do not edit by hand.")
    L2.append("#pragma once")
    L2.append('#include "generated_panel.hpp"')
    L2.append("namespace spkyhw {")
    L2.append("using namespace spkyvcv;")
    L2.append("struct HwOnlyCtl { WidgetKind kind; XY mm; const char* label;")
    L2.append("                   XY lbl; unsigned char anchor; float lblSize;")
    L2.append("                   unsigned lblRgb; };")
    L2.append("static constexpr int kHwHP = 60;")
    L2.extend(emit_table("kParamCtls", HW_PARAMS))
    L2.append("// 1 = big cap, 0 = small. Parallel to kParamCtls, same order.")
    L2.append("// The rehearsal widget reads THIS, not c.kind -- kind says")
    L2.append("// bipolar/detented, which is not a diameter.")
    L2.append("static const unsigned char kParamSize[] = {")
    L2.append("    " + ", ".join("1" if hw_class(c.enum) == "G" else "0"
                                 for c in HW_PARAMS) + ",")
    L2.append("};")
    L2.append("static_assert(sizeof(kParamSize) == sizeof(kParamCtls) / "
               "sizeof(kParamCtls[0]), \"kParamSize desynced\");")
    L2.extend(emit_table("kInputCtls", HW_INPUTS))
    L2.extend(emit_table("kOutputCtls", HW_OUTPUTS))
    L2.extend(emit_table("kLightCtls", HW_LIGHTS))
    L2.append("// Hardware-only: no VCV id. Rack does not render SVG text,")
    L2.append("// so these captions must come from here (spec 2026-08-10 §5).")
    L2.append("static const HwOnlyCtl kHwOnlyCtls[] = {")
    for c in HW_ONLY:
        lx, ly, anchor, size, colour = hw_label(c) if c.label else (
            c.x, c.y, "middle", 2.2, HW_LABEL)
        L2.append(f'    {{{gp.WKMAP[c.kind]}, {{{c.x:.3f}f, {c.y:.3f}f}}, '
                  f'"{c.label}", {{{lx:.3f}f, {ly:.3f}f}}, {ANCHOR_ID[anchor]}, '
                  f'{size:.2f}f, {rgb(colour)}}},')
    L2.append("};")
    L2.append("static const PanelTxt kPanelTexts[] = {")
    for (x, y, size, spacing, col, anchor, txt) in TEXTS:
        L2.append(f'    {{{{{x:.3f}f, {y:.3f}f}}, {size:.2f}f, {spacing:.2f}f, '
                  f'{rgb(col)}, {ANCHOR_ID[anchor]}, "{txt}"}},')
    L2.append("};")
    L2.append("} // namespace spkyhw")
    return "\n".join(L2) + "\n"

def _write_atomic(path, text):
    """Write beside the target and os.replace() onto it, so a crash after
    the target is already open for writing can never leave a truncated
    file. hw_label() raises ValueError by design whenever geometry gets too
    tight (a control that cannot find a clear caption spot) -- that must not
    be allowed to zero out res/FireflowHW.svg or, worse,
    src/generated_hw_panel.hpp, which the VCV build then #includes."""
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(tmp, path)


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    svg_text = svg()
    header_text = header()
    _write_atomic(os.path.join(here, "FireflowHW.svg"), svg_text)
    _write_atomic(os.path.join(root, "src", "generated_hw_panel.hpp"), header_text)
    print("wrote res/FireflowHW.svg and src/generated_hw_panel.hpp")
    print(f"params={len(HW_PARAMS)} inputs={len(HW_INPUTS)} "
          f"outputs={len(HW_OUTPUTS)} lights={len(HW_LIGHTS)}  panel={HP}HP")
