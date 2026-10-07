#!/usr/bin/env python3
"""Hardware-mode panel: the 60 HP envelope draft (envelope spec 2026-08-08 §4).

Emits res/FireflowHW.svg + src/generated_hw_panel.hpp (namespace spkyhw).
Geometry is the 9 mm raster of spec docs/superpowers/specs/2026-10-07-panel-
nine-mm-raster-design.md: every knob and key on one cell table, every group
field built from its cells.

Plate graphics are "Option B" (owner decision 2026-08-29, picked from a
rendered design round): the plate is ONE continuous dark surface -- no zone
tints, no seams, no baked fades, no airflow/ember print. Zone identity lives
entirely in the soft tinted group fields (cool on deck A, warm on deck B,
near-neutral in the centre), and the lettering carries the rest. Not a single
control moved for it -- the round is colour only.

Legends lost their two-digit index on 2026-08-30, and each field's top edge
now takes a small rounded bite around its own legend so the word sits on bare
plate instead of half on the edge. Lettering did not move: the name starts
where the index used to.

Shares all parameter identity with gen_panel.py (import); defines only
geometry. Run from host/vcv/:  python3 res/gen_hw_panel.py
"""
import os, copy
import itertools
import gen_panel as gp
import hw_fields

HP = 60
W  = HP * gp.MM_PER_HP            # 304.8 mm
Hh = 128.5
CX = W / 2.0
KEEP_TOP, KEEP_BOT = 9.0, 119.5   # rails + M3 screws own the rest (bodies, not ink)
ZONE_A = 124.20                   # which accent a control gets, not a plate tint

# ---------------------------------------------------------------------------
#  Plate palette — "Option B" (29 Aug). The plate itself is one continuous
#  dark surface: no zone tints, no seams, no fades, no printed silhouette.
#  The only zone colour left on the plate is the soft tinted group field and
#  the accent rings. Nothing here moves a control: the round is colour only.
# ---------------------------------------------------------------------------
PLATE_HI, PLATE_LO = "#0f1418", "#0b0f12"

HW_LABEL   = "#b9cdd7"            # knob/jack captions
HW_LEGEND  = "#7f9aa8"            # group names, brand subline
HW_RING    = "#33454e"            # hairline around a body
HW_RING2   = "#1e2a30"            # secondary hairline (LED bezel)
HW_WELL    = "#080b0d"            # mounting hole under a pot
JACK_METAL = "#7f8f96"
PAD_FILL   = "#151b1f"            # button keycap. Near-white until 2026-08-30,
                                  # when FfPad joined the knob/jack family: a
                                  # dark cap with an accent edge. The edge
                                  # colour is per-pad now, so PAD_STROKE is
                                  # gone -- see pad_accent().
LED_OFF    = "#0d1417"
ACC = {"A": "#3fbf9c", "B": "#e8945a", "C": "#7fb6c9"}

# Group fields. A group is no longer a drawn frame (dashed rect + corner
# brackets + a knockout under its legend) but a soft tinted panel: one white
# wash that lifts it off the plate, one accent wash that says which zone it
# belongs to. Since the plate lost its own zone tints, THESE carry the whole
# cool/warm deck identity -- so the deck fields are twice the accent of the
# centre, which stays near-neutral.
FIELD_BASE_OPACITY = 0.02
FIELD_ACC_OPACITY = {"A": 0.05, "B": 0.05, "C": 0.025}

# The legend straddles its field's top edge, so that edge ran straight through
# the middle of the glyphs. The field is a FILL and not a stroke, so the fix is
# a bite taken out of its outline rather than a patch laid over one: the top
# edge steps down before the legend and back up after it, and the whole word
# sits on bare plate. Cut from the outline, so it cannot go stale against the
# fill the way the old knockout patch could.
FIELD_R = 1.5                     # the field's own corner radius
NOTCH_R = 0.4                     # the bite's own corners -- minimal on purpose
NOTCH_PAD = 1.0                   # air each side of the legend's ink
NOTCH_DEPTH = 1.10                # clears the baseline (LEGEND_DY 0.75) by 0.35
# The number that constrains the three above is how much frame is left to the
# RIGHT of the longest legend in the narrowest box. Measured, not assumed:
# ENG, a 16.50 mm frame, has 5.25 mm to spare before the notch would reach
# its rounded corner; every other frame has more (LEVEL 11.90, then up). So
# LEGEND_INSET + NOTCH_PAD may grow by 5.2 mm between them before the guard
# in test_hw_panel.py starts refusing a plate.

# Real hardware bodies, not the finger-clearance radius the layout is spaced
# on. The fields are drawn against THESE. They are the real parts (spec
# 2026-10-07 §2; the deferred P1 §3.1 lands here): Davies 1900H 12 mm cap,
# Micro Knob 7.7 mm, Thonk low-profile key cap 6 mm -- a key draws as its
# round cap.
BODY_R = {"G": 6.0, "S": 3.85, "P": 3.0, "J": 3.1, "L": 1.5}

# Distance from the printed word to the real body edge -- ONE number, not a
# per-class offset. Read off the small pots, which are 51 of the 69 params,
# so the common case does not move. Setting the offsets per class instead
# is how the plate ended up with four different gaps (4.50 big, 3.60 small,
# 2.50 pad, 4.90 jack): each was plausible on its own and nothing compared
# them.
CAPTION_GAP = 3.60

# What Rack actually puts on top of the plate, in mm. Neither the plate body
# nor the layout clearance circle: the rehearsal widget is a third radius,
# and it is the one that can bury a legend the SVG preview shows fine.
# Measured from Rack2Pro/res/ComponentLibrary at 75 dpi -- RoundBlackKnob
# 28.348 px, Trimpot 17.856 px, VCVButton 18.000 px, PJ301M 23.700 px.
RACK_R = {"G": gp.FF_KNOB_R["G"], "S": gp.FF_KNOB_R["S"],
          "P": 3.05, "J": gp.FF_PORT_R, "L": 1.50}

# ShareTechMono, the face HwPanelText loads: 0.5 em advance, ~0.72 em cap
# height. Both numbers are what the legend notches are cut to, via text_run.
FONT_ADVANCE, FONT_CAP = 0.50, 0.72


def text_run(x, y, size, spacing, anchor, txt):
    """Ink box of a lettering row: (x0, x1, y_top, y_baseline).

    The gaps are BETWEEN the glyphs, so there are len-1 of them, not len.
    nvgTextLetterSpacing puts one after the last glyph too, but that only
    moves the pen -- no ink follows it. Counting it made every run read
    `spacing` too wide on the right, which is invisible in a collision test
    (it only errs safe) and very visible the moment something is CENTRED on
    the box: the legend notches came out 1.00 mm of air on the left and 1.50
    on the right, and that is what Bastian saw on the plate 2026-08-30."""
    w = len(txt) * size * FONT_ADVANCE + max(len(txt) - 1, 0) * spacing
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
LED_ON = {s: _blend_hex(ACC[s], LED_OFF, 0.55) for s in ACC}

# Two of the fifteen lamps sit at the plate's edges and would be filed under
# a deck by position alone: the MOD and SHFT lamps (the limiter lamp CEIL_L
# folded into MODBTN_L, spec 2026-10-07 §5). Same correction GLOBAL_KEYS makes
# for the keycaps -- see pad_accent().
LED_GLOBALS = {"MODBTN_L", "SHIFTBTN_L"}


def led_accent(c):
    """A lamp's live glow colour."""
    if c.enum in ("REC_A_L", "REC_B_L"):
        return gp.LED_REC
    return ACC["C"] if c.enum in LED_GLOBALS else ACC[zone_of(c.x)]


def led_bed(c):
    """The printed dot under a lamp: its own glow, mixed down into the dark."""
    return _blend_hex(led_accent(c), LED_OFF, 0.55)

# Which pots wear the mod ring (spec 2026-08-22 §5): exactly the faces with a
# depth param. Derived from gen_panel's tables so the ring and the param block
# cannot drift apart. Since revision 3 (2026-08-30) this feeds kModRing in the
# header, not the plate print -- see the ring comment in svg().
MOD_WREATHED = ({f"{b}_A" for b, _, _, _ in gp.MOD_DECK_TARGETS}
                | {f"{b}_B" for b, _, _, _ in gp.MOD_DECK_TARGETS}
                | {b for b, _, _, _ in gp.MOD_CENTER_TARGETS})


# The two keycaps whose position lies. MOD sits at the right end of the jack
# row and SHFT at the left end, so zone_of() would file the global MOD latch
# under deck B and SHFT under deck A. Both are global; both get the neutral
# centre accent. Same reasoning that kept the jacks colourless entirely.
GLOBAL_KEYS = {"MODBTN", "SHIFTBTN"}


def zone_of(x):
    """Which of the three accent zones x falls in. The plate is one flat
    surface now -- this only picks a group field's tint, a lamp's lit colour
    and a mod wreath's ring colour."""
    if x < ZONE_A:
        return "A"
    if x > W - ZONE_A:
        return "B"
    return "C"

def pad_accent(c):
    """A keycap's edge colour: its zone, unless the key is a global one."""
    return ACC["C"] if c.enum in GLOBAL_KEYS else ACC[zone_of(c.x)]


HW_SIZE = {
    "MOD": "G", "DENSITY": "G",
    "RATE": "S", "SHAPE": "S", "SMOOTH": "S", "RANGE": "S", "MELODY": "S",
    "COLOR": "G",
    "TUNE": "S", "DETUNE": "S",
    # Big caps stand where the 9 mm raster allows them: rows 2 and 4, never
    # deck column 6, never side by side (spec 2026-10-07 §3; the cell guard
    # in test_hw_panel.py holds it).
    "FILT": "G",
    "SOURCE": "S",                                 # TIMB is small (graphics round)
    "DEPTH": "S",                                   # FEED, the VOICE knob
    "ATTACK": "S", "DECAY": "S", "RES": "S", "SUB": "S", "STAGES": "S",
    "FLUX": "G",
    "FLUXRATE": "S", "FLUXFB": "S", "LINK": "S",
    # SEND went G -> S on 2026-08-30 when it left ROOM for LEVEL; on the
    # raster it stands in row 5, which stays small (spec 2026-10-07 §3).
    "REV_MIX": "S",
    "PAN": "S",
    "COMP": "G", "GRIT": "S",
    "STEPS": "S", "SONG": "S",
    "ENGINE": "S", "REC": "P",                     # ENGINE is a 5-zone detent pot
    "MORPH": "G", "TIDE": "S", "CHOKE": "S", "PACE": "S", "PULL": "S",
    "TEMPO": "S", "COUPLE": "S", "SHUFFLE": "S",
    "SCALE": "S", "DRIFT": "S",
    "REV_DECAY": "G", "REV_SIZE": "S", "REV_TONE": "S", "REV_DIFF": "S",
    "ROOT": "S", "REV_MOD": "S",                    # reserved pots, spec 2026-10-07 §4
}

CLASS_R = {"G": 8.5, "S": 6.0, "P": 4.0, "J": 4.0, "L": 1.5}
# Panel holes of the real parts (Rev A P1 spec 2026-09-29 §3.2), by class.
# Datasheet values until the parts are measured (spec §6): Alpha 9 mm M7
# bushing, Thonkiconn, Thonk low-profile button ("cutout 6.2 mm"), 3 mm LED.
# gen_hw_cut.py cuts these; nothing here draws them.
HOLE_D = {"G": 7.0, "S": 7.0, "J": 6.0, "P": 6.2, "L": 3.1}
# Least material between two hole edges -- acrylic cracks at thinner webs.
MIN_WEB = 2.0
# Satellite lamp distance from its key or jack: the larger anchor hole's
# radius, the web, the LED hole's radius -- 3.1 + 2.0 + 1.55 = 6.65, rounded
# up to 6.7.
SAT_D = 6.7
CLASS_LBL_DY = {cls: (0.0 if cls == "L" else r + CAPTION_GAP)
                for cls, r in BODY_R.items()}
# The jack row is the one place a shared BASELINE beats a shared gap: SHFT
# and MOD are keycaps sitting in a line of jacks, and letting them keep
# their own offset puts two words 0.9 mm below the other seven.
JACK_ROW_LBL_DY = CLASS_LBL_DY["J"]

# Four-character plate words. Keys are enum bases (SHAPE) or full names (IN_L).
HW_CAPTION = {
    "SHAPE": "SHAP", "RANGE": "RANG", "COLOR": "COLR",
    "COUPLE": "SYNC", "TEMPO": "TEMP", "SHUFFLE": "SHFL",
    "SCALE": "SCAL", "DRIFT": "DRFT", "CHOKE": "CHOK",
    "MORPH": "MRPH", "REV_DECAY": "DECY",
    "STAGES": "",
    "DEPTH": "DPTH",
    "IN_L": "IN L", "IN_R": "IN R", "OUT_L": "OUT L", "OUT_R": "OUT R",
    "SHIFTBTN": "SHFT",
    "MODBTN": "MOD",
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

# P4-1 measured the jacks' tip pads past the board edge at 114.0; the row
# must sit at y <= 112.77 (spec 2026-10-07 §6). Keys, jack-row lamps and the
# SD slot share this line and move with it.
JACK_Y = 112.75
SD_X, SD_Y, SD_W, SD_H = 152.4, JACK_Y, 11.0, 6.0

# CV jack columns — uniform 11.5 mm raster, not under the knobs (spec §13).
X_COLOR, X_FILT, X_TIMB, X_LVL = 79.0, 90.5, 102.0, 113.5

# ---------------------------------------------------------------------------
#  The 9 mm raster (spec docs/superpowers/specs/2026-10-07-panel-nine-mm-
#  raster-design.md §3). One pitch for all fifteen columns -- six per deck,
#  three in the centre -- centred on 152.4; 7 x 20.2 + 6.0 puts deck A's outer
#  big cap 5.0 mm from the nominal edge. Rows run 14.5 .. 95.0. Every gap the
#  grip test asked for (>= 9 mm between caps) is >= 10.27 mm here, measured.
#  Big caps stand in rows 2 and 4 only, never in deck column 6, never side by
#  side: two adjacent big caps would need 21.0 mm.
# ---------------------------------------------------------------------------
COL_PITCH = 20.2
ROW_Y = (14.500, 34.625, 54.750, 74.875, 95.000)
ROW_PITCH = ROW_Y[1] - ROW_Y[0]


def deck_col_x(col):
    """Deck A column 1..6 (1 = outer edge); deck B is W - x."""
    return CX - (8 - col) * COL_PITCH


def centre_col_x(k):
    """Centre column k = -1, 0, +1."""
    return CX + k * COL_PITCH


# Deck A (deck B mirrored): stem -> (row, column). ATTACK and STAGES share a
# knob. ROOT is reserved (§4).
DECK_CELLS = {
    "ENGINE": (1, 1), "STEPS": (1, 2), "SONG": (1, 3), "RATE": (1, 4),
    "MELODY": (1, 5), "REC": (1, 6),
    "MOD": (2, 1), "SHAPE": (2, 2), "DENSITY": (2, 3), "SOURCE": (2, 4),
    "FILT": (2, 5), "DEPTH": (2, 6),
    "SMOOTH": (3, 1), "RANGE": (3, 2), "ATTACK": (3, 3), "STAGES": (3, 3),
    "DECAY": (3, 4), "RES": (3, 5), "SUB": (3, 6),
    "COLOR": (4, 1), "TUNE": (4, 2), "FLUX": (4, 3), "FLUXRATE": (4, 4),
    "COMP": (4, 5), "PAN": (4, 6),
    "ROOT": (5, 1), "DETUNE": (5, 2), "FLUXFB": (5, 3), "LINK": (5, 4),
    "GRIT": (5, 5), "REV_MIX": (5, 6),
}
# Centre: name -> (row, k). Mirror-symmetric: TIMING is an arch (row 2 whole,
# legs at k = -1/+1 down to row 4), ROOM an inverted T inside it. COUPLE
# (printed SYNC) and DRIFT are the arch's mirrored legs: together / apart.
CENTRE_CELLS = {
    "SCALE": (1, -1), "CHOKE": (1, 0), "PULL": (1, 1),
    "TIDE": (2, -1), "MORPH": (2, 0), "PACE": (2, 1),
    "TEMPO": (3, -1), "REV_SIZE": (3, 0), "SHUFFLE": (3, 1),
    "COUPLE": (4, -1), "REV_DECAY": (4, 0), "DRIFT": (4, 1),
    "REV_DIFF": (5, -1), "REV_TONE": (5, 0), "REV_MOD": (5, 1),
}
DECK_GROUPS = {
    "ENG": ("ENGINE",),
    "SEQUENCE": ("STEPS", "SONG", "RATE", "MELODY"),
    "CAPTURE": ("REC",),
    "MOTION": ("MOD", "SHAPE", "DENSITY", "SMOOTH", "RANGE"),
    "VOICE": ("SOURCE", "FILT", "DEPTH", "ATTACK", "STAGES", "DECAY", "RES", "SUB"),
    "PITCH": ("COLOR", "TUNE", "ROOT", "DETUNE"),
    "FLUX": ("FLUX", "FLUXRATE", "FLUXFB", "LINK"),
    "LEVEL": ("COMP", "PAN", "GRIT", "REV_MIX"),
}
CENTRE_GROUPS = {
    "GLOBAL": ("SCALE", "CHOKE", "PULL"),
    "TIMING": ("TIDE", "MORPH", "PACE", "TEMPO", "SHUFFLE", "COUPLE", "DRIFT"),
    "ROOM": ("REV_SIZE", "REV_DECAY", "REV_DIFF", "REV_TONE", "REV_MOD"),
}
# Reserved pots (spec §4): a hole, a caption and a pot on plate and board, a
# mux channel, a firmware row that sends nothing -- no ParamId, no Rack widget.
RESERVED = {"ROOT": ("ROOT", "reserved: per-deck scale root (spec 2026-10-07 §4)"),
            "REV_MOD": ("WOBL", "reserved: reverb tail wobble (spec 2026-10-07 §4)")}


def _deck_xy(stem):
    row, col = DECK_CELLS[stem]
    return deck_col_x(col), ROW_Y[row - 1]


def _centre_xy(name):
    row, k = CENTRE_CELLS[name]
    return centre_col_x(k), ROW_Y[row - 1]


DECK_POS = {s: _deck_xy(s) for s in DECK_CELLS if s not in RESERVED}
CENTER_POS = {n: _centre_xy(n) for n in CENTRE_CELLS if n not in RESERVED}
# MODBTN is a real latch param (spec 2026-08-22 mod-latch-layer §5), placed
# through place() like every sound knob, on the jack row.
SHIFT_X = 14.00     # the SHFT key's x; MODBTN mirrors it about the centre line
CENTER_POS["MODBTN"] = (W - SHIFT_X, JACK_Y)

JACK_POS = {"PITCH_A": 56.00, "GATE_A": 67.50,
            "IN_L": 33.00, "IN_R": 44.50, "CLOCK": 136.00,
            "RESET": 168.80, "OUT_L": 260.30, "OUT_R": 271.80,
            "GATE_B": W - 67.50, "PITCH_B": W - 56.00,
            "MOD1_A": X_COLOR, "MOD2_A": X_FILT, "MOD3_A": X_TIMB, "MOD4_A": X_LVL,
            "MOD1_B": W - X_COLOR, "MOD2_B": W - X_FILT,
            "MOD3_B": W - X_TIMB, "MOD4_B": W - X_LVL}

# Knob-owned lamps: the caption and the LED are one block under the knob,
# word then air then LED, centred on the knob x. Same reading order on both
# decks -- not an optical [LED][word] mirror. The jack-row keys, the CLOCK
# jack lamps stay as satellites or between key and jack (LIGHT_POS below).
KNOB_LAMPS = {
    "LVL_A_L": "COMP_A",   "LVL_B_L": "COMP_B",
    # FTIME flashes once per FLUX time period: timing, not modulation (spec
    # 2026-10-07 §5). It sits in FLUXRATE's caption cluster.
    "FTIME_A_L": "FLUXRATE_A", "FTIME_B_L": "FLUXRATE_B",
    "GATE_A_L": "ATTACK_A", "GATE_B_L": "ATTACK_B",
    "TEMPO_L": "TEMPO",
    # REC's lamp joined the cluster on 2026-10-07: beside the key it reached
    # 0.25 mm out of CAPTURE's cell (spec §5).
    "REC_A_L": "REC_A", "REC_B_L": "REC_B",
}
# SONG's lamp stands BESIDE its knob, inboard, on the knob's line: the top
# row's pots have their pins south, where a cluster LED would land, and the
# row band leaves no room to drop it (spec §5.1).
SIDE_LAMPS = {"SONG_A_L": "SONG_A", "SONG_B_L": "SONG_B"}
LAMP_OWNER = {**KNOB_LAMPS, **SIDE_LAMPS}
KNOBS_WITH_LAMPS = set(KNOB_LAMPS.values())
LED_CAPTION_GAP = 0.8   # mm of air between the word's ink and the LED body
CAPTION_SIZE = 2.2      # same size hw_label prints


def caption_led_cluster(knob):
    """(cap_x, cap_y, led_x, led_y) for a knob-owned lamp.

    LED centre sits on the caption's glyph midline. The body then hangs
    ~0.7 mm below the baseline, and the group field counts it as its
    owner's ink (spec 2026-10-07 §7).
    """
    w = len(knob.label) * (CAPTION_SIZE * FONT_ADVANCE)
    led_r = BODY_R["L"]
    total = w + LED_CAPTION_GAP + 2 * led_r
    x0 = knob.x - total / 2.0
    cap_x = x0 + w / 2.0
    cap_y = knob.y + CLASS_LBL_DY[hw_class(knob.enum)]
    led_x = x0 + w + LED_CAPTION_GAP + led_r
    led_y = cap_y - (CAPTION_SIZE * FONT_CAP) / 2.0
    return cap_x, cap_y, led_x, led_y

# Stay-put lamps only. Knob-owned entries are filled from caption_led_cluster
# after HW_PARAMS exists -- do not hand-edit those back in here.
LIGHT_POS = {
    # Satellites at SAT_D (Rev A P1): CLK_L inboard of CLOCK, RST_L outboard
    # of RESET -- the two mirror each other about the centre line.
    "CLK_L":      (JACK_POS["CLOCK"] - SAT_D, JACK_Y),
    "RST_L":      (JACK_POS["RESET"] + SAT_D, JACK_Y),
    # Spec 2026-10-07 §5: one lamp each, centred between key and jack, two
    # jobs each (SHIFT latched / input level; MOD latched / limiter). Both sit
    # in their jack's audio zone, admitted (spec §5.3).
    "SHIFTBTN_L": ((SHIFT_X + JACK_POS["IN_L"]) / 2.0, JACK_Y),
    "MODBTN_L":   ((JACK_POS["OUT_R"] + (W - SHIFT_X)) / 2.0, JACK_Y),
}


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
# MODBTN is a real latch param now (spec 2026-08-22 mod-latch-layer §5); it
# lives in gp.MOD_LAYER_PARAMS, outside RUNTIME_PANEL_PARAMS, so the big
# panel never draws it -- placed here explicitly, keycap slot it always had.
HW_PARAMS = HW_PARAMS + [place(gp.MODBTN_CTL)]
_by_param = {c.enum: c for c in HW_PARAMS}
for lamp, knob_enum in KNOB_LAMPS.items():
    LIGHT_POS[lamp] = caption_led_cluster(_by_param[knob_enum])[2:]
for lamp, knob_enum in SIDE_LAMPS.items():
    k = _by_param[knob_enum]
    inboard = COL_PITCH / 2.0 if k.x < CX else -COL_PITCH / 2.0
    LIGHT_POS[lamp] = (k.x + inboard, k.y)
HW_INPUTS  = [place(c) for c in gp.INPUTS] + [place(c) for c in gp.HW_MOD_INPUTS]
HW_OUTPUTS = [place(c) for c in gp.OUTPUTS]
_SKIP_HW_LIGHTS = {"FLOW_A_L", "FLOW_B_L"}
HW_LIGHTS  = [place(c) for c in gp.LIGHTS + gp.HW_ONLY_LIGHTS
              if c.enum not in _SKIP_HW_LIGHTS]


class HwOnly:
    __slots__ = ("enum", "kind", "x", "y", "r", "label", "tip")

    def __init__(self, enum, cls, x, y, label, tip):
        self.enum, self.x, self.y, self.label, self.tip = enum, x, y, label, tip
        self.kind = {"P": gp.LATCH, "J": gp.IN, "L": gp.LIGHT, "S": gp.SMKNOB}[cls]
        self.r = CLASS_R[cls]


HW_ONLY = [
    HwOnly("SHIFTBTN", "P", SHIFT_X, JACK_Y, "SHFT", "reserved, no function"),
    HwOnly("ROOT_A", "S", *_deck_xy("ROOT"), *RESERVED["ROOT"]),
    HwOnly("ROOT_B", "S", W - _deck_xy("ROOT")[0], _deck_xy("ROOT")[1], *RESERVED["ROOT"]),
    HwOnly("REV_MOD", "S", *_centre_xy("REV_MOD"), *RESERVED["REV_MOD"]),
]

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
        return (c.x, c.y, "middle", CAPTION_SIZE, HW_LABEL)
    if c.enum in KNOBS_WITH_LAMPS:
        cap_x, cap_y, _, _ = caption_led_cluster(c)
        return (cap_x, cap_y, "middle", CAPTION_SIZE, HW_LABEL)
    dy = CLASS_LBL_DY[hw_class(c.enum)]
    if c.y >= JACK_Y - 0.5:
        dy = JACK_ROW_LBL_DY
    third = ((c.x + c.r + 1.0, c.y + 1.0, "start") if c.x <= CX else
             (c.x - c.r - 1.0, c.y + 1.0, "end"))
    for lx, ly, anchor in ((c.x, c.y + dy, "middle"),
                           (c.x, c.y - dy, "middle"),
                           third):
        if _caption_is_clear(c, lx, ly):
            return (lx, ly, anchor, CAPTION_SIZE, HW_LABEL)
    raise ValueError(f"no clear caption position for {c.enum} -- the geometry "
                     f"is too tight, move a control (spec 2026-08-10 §3)")


# =============================================================================
#  Group fields: rect unions built from the cells (spec 2026-10-07 §7)
# =============================================================================
# Every group's field is the union of its controls' cell rects, so a field
# can be any rectilinear shape -- TIMING is an arch with ROOM standing inside
# it. 3 mm of air between fields of different groups; deck B is deck A
# mirrored because its controls are.
BOX_GAP = 3.0

# Every group name the plate carries, in reading order: decks top to bottom,
# then the centre column, then the jack row. This printed as a two-digit index
# in front of each legend until 2026-08-30; it outlives the numbering as the
# roster the guard checks the drawn frames against.
GROUP_ORDER = ("ENG", "SEQUENCE", "CAPTURE", "MOTION", "VOICE", "PITCH",
               "FLUX", "LEVEL", "GLOBAL", "TIMING", "ROOM", "IN", "CV", "MOD",
               "CLOCK", "OUT")


LEGEND_SIZE, LEGEND_SPACING = 1.9, 0.5
LEGEND_DY = 0.75                  # legend baseline below a frame's top edge
LEGEND_INSET = 4.0                # legend's left edge, in from the frame's.
                                  # It is where the struck index used to start,
                                  # so the lettering did not move on 2026-08-30
                                  # -- only the two digits in front of it went.


def body_r(c):
    """Radius of the real component body, not the layout clearance circle."""
    return BODY_R[hw_class(c.enum)]


FIELD_MARGIN = 1.45                 # spec §7: the worst pair allows 1.4825
CELL_HALF = (COL_PITCH - BOX_GAP) / 2.0

# The jack row keeps its own frames (no legends since 2026-08-30).
JACK_ROW_X0, JACK_ROW_CUTS = 28.00, (50.25, 73.25)
JACK_ROW_A, JACK_ROW_B, JACK_ROW_C = ("IN", "CV A", "MOD A"), ("OUT", "CV B", "MOD B"), "CLOCK"
DECK_EDGE, CENTRE_L = 120.0, 123.0


class Box(hw_fields.Field):
    """A group field: a rect union with a name, a side and a legend."""

    def __init__(self, n, side, rects):
        super().__init__(rects)
        self.n, self.side = n, side

    @property
    def stem(self):
        """The legend without its deck suffix -- an entry in GROUP_ORDER."""
        return self.n[:-2] if self.n.endswith((" A", " B")) else self.n

    @property
    def prints_legend(self):
        """The jack row prints none since 2026-08-30 -- IN / CV A / MOD A /
        CLOCK / MOD B / CV B / OUT are struck. The jacks are the one row a
        legend never helped: they are the only sockets on the plate and the
        captions under them name every one."""
        return abs(self.y - JACK_ROW_Y) > 1e-9

    @property
    def legend_y(self):
        """Baseline of the group legend -- always straddling the top edge."""
        return self.y + LEGEND_DY

    @property
    def notch(self):
        """(x0, x1) of the bite this field's top edge takes around its own
        legend, or None where the field prints no legend to make room for."""
        if not self.prints_legend:
            return None
        _, ink_r, _, _ = text_run(self.x + LEGEND_INSET, self.legend_y,
                                  LEGEND_SIZE, LEGEND_SPACING, "start", self.n)
        return (self.x + LEGEND_INSET - NOTCH_PAD, ink_r + NOTCH_PAD)


def _ink(c):
    """Everything control c prints: body, caption, and any lamp it owns."""
    r = body_r(c)
    x0, x1, y0, y1 = c.x - r, c.x + r, c.y - r, c.y + r
    if c.label:
        lx, ly, anchor, size, _col = hw_label(c)
        tx0, tx1, ty0, ty1 = text_run(lx, ly, size, 0.0, anchor, c.label)
        x0, x1, y0, y1 = min(x0, tx0), max(x1, tx1), min(y0, ty0), max(y1, ty1)
    lr = BODY_R["L"]
    for lamp, owner in LAMP_OWNER.items():
        if owner == c.enum:
            lx, ly = LIGHT_POS[lamp]
            x0, x1 = min(x0, lx - lr), max(x1, lx + lr)
            y0, y1 = min(y0, ly - lr), max(y1, ly + lr)
    return x0, x1, y0, y1


def _knob_field_rects(members):
    """Spec §7: each control's cell rect (its ink plus FIELD_MARGIN, at least
    its column cell), stretched to its row's band within the group, plus the
    joins between neighbouring cells of the same group."""
    rows = {}
    for c in members:
        x0, x1, y0, y1 = _ink(c)
        cell = [min(x0 - FIELD_MARGIN, c.x - CELL_HALF), max(x1 + FIELD_MARGIN, c.x + CELL_HALF),
                y0 - FIELD_MARGIN, y1 + FIELD_MARGIN]
        rows.setdefault(round(c.y, 6), []).append((c, cell))
    cells = []
    for row in rows.values():
        top = min(cell[2] for _c, cell in row)
        bot = max(cell[3] for _c, cell in row)
        for c, cell in row:
            cell[2], cell[3] = top, bot
            cells.append((c, cell))
    rects = [tuple(cell) for _c, cell in cells]
    for (a, ra), (b, rb) in itertools.combinations(cells, 2):
        if abs(a.y - b.y) < 1e-6 and abs(abs(a.x - b.x) - COL_PITCH) < 1e-6:
            lo, hi = (ra, rb) if a.x < b.x else (rb, ra)
            if lo[1] < hi[0]:
                rects.append((lo[1], hi[0], ra[2], ra[3]))
        elif abs(a.x - b.x) < 1e-6 and abs(abs(a.y - b.y) - ROW_PITCH) < 1e-6:
            up, dn = (ra, rb) if a.y < b.y else (rb, ra)
            if up[3] < dn[2]:
                rects.append((max(up[0], dn[0]), min(up[1], dn[1]), up[3], dn[2]))
    # A 2 x 2 block of one group's cells (MOTION, VOICE, PITCH, FLUX, LEVEL)
    # leaves a hole where its four joins meet. Fill it: a field is one piece
    # without holes (hw_fields.outline refuses a ring). It lies inside the
    # group's own rects, so no gap to another field changes.
    at = {}
    for c, cell in cells:
        k = (round(c.x, 6), round(c.y, 6))
        o = at.get(k, cell)
        at[k] = (min(o[0], cell[0]), max(o[1], cell[1]), cell[2], cell[3])
    for (x, y), ul in at.items():
        ur = at.get((round(x + COL_PITCH, 6), y))
        dl = at.get((x, round(y + ROW_PITCH, 6)))
        dr = at.get((round(x + COL_PITCH, 6), round(y + ROW_PITCH, 6)))
        if ur and dl and dr:
            hx0, hx1 = min(ul[1], dl[1]), max(ur[0], dr[0])
            if hx0 < hx1 and ul[3] < dl[2]:
                rects.append((hx0, hx1, ul[3], dl[2]))
    return rects


def _jack_row_cells():
    """(name, side, x, w) for the jack row's frames, left to right."""
    edges = [JACK_ROW_X0] + list(JACK_ROW_CUTS) + [DECK_EDGE]

    def span(i):
        lo = edges[i] + (BOX_GAP / 2.0 if i else 0.0)
        hi = edges[i + 1] - (BOX_GAP / 2.0 if i + 1 < len(JACK_ROW_A) else 0.0)
        return lo, hi

    cells = []
    for i, n in enumerate(JACK_ROW_A):
        lo, hi = span(i)
        cells.append((n, "A", lo, hi - lo))
    cells.append((JACK_ROW_C, "C", CENTRE_L, W - 2 * CENTRE_L))
    for i, n in enumerate(JACK_ROW_B):
        lo, hi = span(i)
        cells.append((n, "B", W - hi, hi - lo))
    return cells


def _jack_row_band():
    spans = [(x, x + w) for _n, _s, x, w in _jack_row_cells()]
    top = bot = None
    for c in ALL_HW:
        if abs(c.y - JACK_Y) > 0.5 or not any(x0 <= c.x <= x1 for x0, x1 in spans):
            continue
        r = body_r(c)
        edges = [(c.y - r, c.y + r)]
        if c.label:
            _lx, ly, _a, size, _col = hw_label(c)
            edges.append((ly - size * FONT_CAP, ly))
        for a, b in edges:
            top = a if top is None else min(top, a)
            bot = b if bot is None else max(bot, b)
    return top - FIELD_MARGIN, bot + FIELD_MARGIN


JACK_ROW_Y = _jack_row_band()[0]


def group_boxes():
    """The 26 group fields: deck A, deck B, the centre, then the jack row."""
    by = {c.enum: c for c in ALL_HW}
    out = []
    for side in "AB":
        for name, stems in DECK_GROUPS.items():
            members = [by[f"{s}_{side}"] for s in stems if f"{s}_{side}" in by]
            out.append(Box(name, side, _knob_field_rects(members)))
    for name, names in CENTRE_GROUPS.items():
        out.append(Box(name, "C", _knob_field_rects([by[n] for n in names])))
    y0, y1 = _jack_row_band()
    for n, side, x, w in _jack_row_cells():
        out.append(Box(n, side, [(x, x + w, y0, y1)]))
    return out


BOXES = group_boxes()


def box_of(c):
    """The field a control's centre falls in, or None (SHFT/MOD sit loose).
    Asks the field's own rect union, so an arch's leg or a T's foot counts."""
    for b in BOXES:
        if b.covers(c.x, c.y):
            return b
    return None


def _field_d(b):
    """One group field as a path: the union's outline, every corner rounded,
    with a bite out of the top edge where the legend prints.

    A path and not a <rect rx>, because the bite has to come out of the
    OUTLINE: the field is a fill over a gradient plate, so a knockout patch
    could never repaint what is under it. The outline's first edge is the
    top edge from the top-left corner (hw_fields.outline), which is the edge
    the legend straddles.

    Quadratic curves, not elliptical arcs: at NOTCH_R = 0.4 mm the two are
    indistinguishable, and Q takes the sweep-flag question -- and any question
    about NanoSVG's arc parser -- off the table entirely."""
    pts = list(b.outline)
    rad = [FIELD_R] * len(pts)
    if b.notch:
        n0, n1 = b.notch
        _x, y = pts[0]
        pts[1:1] = [(n0, y), (n0, y + NOTCH_DEPTH), (n1, y + NOTCH_DEPTH), (n1, y)]
        rad[1:1] = [NOTCH_R] * 4
    return _rounded_poly(pts, rad)


def _rounded_poly(pts, radii):
    """Closed polygon, every corner rounded, quadratics only.

    Consecutive duplicates are dropped first: that is what lets a plain
    rectangle, an L and a bracket share one vertex list. Q and not A because
    at these radii the two are indistinguishable, and Q asks nothing of
    NanoSVG's arc parser or of my sweep-flag arithmetic."""
    keep = [(p, r) for i, (p, r) in enumerate(zip(pts, radii))
            if p != pts[i - 1]]
    pts = [p for p, _ in keep]
    radii = [r for _, r in keep]
    n, out = len(pts), []
    for i, p in enumerate(pts):
        pv, nx, want = pts[i - 1], pts[(i + 1) % n], radii[i]

        def toward(q):
            dx, dy = q[0] - p[0], q[1] - p[1]
            L = (dx * dx + dy * dy) ** 0.5
            return (dx / L, dy / L), L

        (ux1, uy1), L1 = toward(pv)
        (ux2, uy2), L2 = toward(nx)
        r = min(want, L1 / 2, L2 / 2)
        a = (p[0] + ux1 * r, p[1] + uy1 * r)
        c = (p[0] + ux2 * r, p[1] + uy2 * r)
        out.append(("M" if i == 0 else "L") + f"{mm(a[0])},{mm(a[1])}")
        out.append(f"Q{mm(p[0])},{mm(p[1])} {mm(c[0])},{mm(c[1])}")
    return " ".join(out + ["Z"])


def _groups_svg():
    """Every group is two washes on the same outline: a white one that lifts
    the field off the plate, and an accent one that says which zone it belongs
    to. With the plate flat, these fields carry the whole cool/warm deck
    identity -- the centre stays near-neutral on purpose."""
    out = []
    for b in BOXES:
        d = _field_d(b)
        out.append(f'<path d="{d}" fill="#ffffff" '
                   f'fill-opacity="{FIELD_BASE_OPACITY}"/>')
        out.append(f'<path d="{d}" fill="{ACC[b.side]}" '
                   f'fill-opacity="{FIELD_ACC_OPACITY[b.side]}"/>')
    return out


def group_texts():
    """Legends as PanelTxt rows -- ONE per frame since 2026-08-30, when the
    two-digit index in front of every name was struck. Rack does not render
    SVG text, so the plate lettering and the rehearsal lettering must both
    come from this table."""
    return [(b.x + LEGEND_INSET, b.legend_y, LEGEND_SIZE, LEGEND_SPACING,
             HW_LEGEND, "start", b.n) for b in BOXES if b.prints_legend]


BRAND_TEXTS = [
    # Empty on purpose, and the list stays so TEXTS keeps its shape.
    # The "FIREFLOW" wordmark and the "60 HP" legend were pulled 2026-08-23:
    # the plate's branding is being redrawn and nothing placeholder should sit
    # in the header strip meanwhile. That strip (y=9.40) is still free.
    # "DECK A"/"DECK B" were struck 2026-08-29 with the Option B round (owner
    # decision): the tinted group fields carry the deck identity now, so the
    # words were redundant.
]

TEXTS[:] = BRAND_TEXTS + group_texts()


# =============================================================================
#  Plate background: one continuous dark surface
# =============================================================================
def _plate_svg():
    """The whole plate, top to bottom, in one rect.

    Option B (29 Aug): the three tinted zone rects, the seam gradients, the
    baked fade overlays and the printed airflow/ember silhouette are all gone.
    Deck identity is carried by the tinted group fields instead, so nothing
    here may reintroduce a vertical edge across the plate."""
    return [f'<rect x="0" y="0" width="{mm(W)}" height="{mm(Hh)}" '
            f'fill="url(#hw-plate)"/>']


def _defs_svg():
    """The plate gradient, in millimetres and never in bounding-box fractions.

    Measured, not assumed: objectBoundingBox gradients made Rack and the
    browser disagree about this plate -- one of them painted an overlay opaque
    across a whole deck while its mirror image behaved. userSpaceOnUse takes
    the question off the table, so any gradient added here stays in mm."""
    def lin(name, vec, stops):
        x1, y1, x2, y2 = vec
        s = "".join(f'<stop offset="{o}" stop-color="{c}" stop-opacity="{a}"/>'
                    for o, c, a in stops)
        return (f'<linearGradient id="hw-{name}" gradientUnits="userSpaceOnUse" '
                f'x1="{mm(x1)}" y1="{mm(y1)}" x2="{mm(x2)}" y2="{mm(y2)}">'
                f'{s}</linearGradient>')

    return ["<defs>",
            lin("plate", (0, 0, 0, Hh), [(0, PLATE_HI, 1), (1, PLATE_LO, 1)]),
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
                      f'fill="{led_bed(c)}"/>')
        elif hw_class(c.enum) == "P":
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(br)}" '
                      f'fill="{PAD_FILL}" stroke="{pad_accent(c)}" stroke-width="0.3"/>')
        else:
            # The mounting hole, drawn at the real pot body -- not a cap. Rack
            # puts its own knob widget on top and a plate has a hole here.
            # Every body ring is the plain dark hairline again. The accent
            # recolour that marked a modulatable knob was plate print from
            # 2026-08-22 to 2026-08-30; it now lives in kModRing and Rack
            # draws it only while the MOD latch is engaged (spec
            # docs/superpowers/specs/2026-08-22-mod-latch-layer-design.md §5
            # revision 3). Silkscreen cannot switch, so the aluminium plate
            # marks nothing -- the accepted cost of that call.
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
    # The mod ring left the plate on 2026-08-30 (spec 2026-08-22 §5 revision
    # 3): Rack draws it, gated on the MOD latch, so the colour and the radius
    # have to reach the widget as data. They come from here and nowhere else
    # -- a widget carrying its own ACC / ZONE_A / W literals is exactly what
    # sank the first ModDepthRing.
    L2.append("// Latch-gated mod ring, parallel to kParamCtls, same order.")
    L2.append("// rgb 0 = this knob owns no depth and wears no ring. rMm is")
    L2.append("// the pot BODY radius -- the ring the plate used to print.")
    L2.append("struct HwModRing { unsigned rgb; float rMm; };")
    L2.append("static const HwModRing kModRing[] = {")
    for c in HW_PARAMS:
        if c.enum in MOD_WREATHED:
            L2.append(f"    {{{rgb(ACC[zone_of(c.x)])}, {body_r(c):.3f}f}},")
        else:
            L2.append("    {0, 0.000f},")
    L2.append("};")
    L2.append("static_assert(sizeof(kModRing) / sizeof(kModRing[0]) == "
               "sizeof(kParamCtls) / sizeof(kParamCtls[0]), "
               "\"kModRing desynced\");")
    # FfKnob's collar and pointer. FfAccent itself is declared in
    # generated_panel.hpp; both halves are the same zone here -- the two-tone
    # row exists for the big panel's MORPH, which this plate has no twin for.
    L2.append("// Knob and keycap accent, parallel to kParamCtls, same order.")
    L2.append("static const FfAccent kParamAccent[] = {")
    for c in HW_PARAMS:
        a = rgb(pad_accent(c) if hw_class(c.enum) == "P" else ACC[zone_of(c.x)])
        L2.append(f"    {{{a}, {a}}},")
    L2.append("};")
    L2.append("static_assert(sizeof(kParamAccent) / sizeof(kParamAccent[0]) == "
               "sizeof(kParamCtls) / sizeof(kParamCtls[0]), "
               "\"kParamAccent desynced\");")
    # FfPad covers its printed bed exactly, so the widget's half-width is the
    # pad's own body radius, not RACK_R -- the stock button was smaller than
    # the square it sat on, which is what made the two read as two objects.
    L2.append(f"static constexpr float kFfPadR = {BODY_R['P']:.3f}f;  // mm")
    L2.extend(emit_table("kInputCtls", HW_INPUTS))
    L2.extend(emit_table("kOutputCtls", HW_OUTPUTS))
    L2.extend(emit_table("kLightCtls", HW_LIGHTS))
    L2.append("// Light glow, parallel to kLightCtls, same order.")
    L2.append("static const FfAccent kLightAccent[] = {")
    for c in HW_LIGHTS:
        a = rgb(led_accent(c))
        L2.append(f"    {{{a}, {a}}},")
    L2.append("};")
    L2.append("static_assert(sizeof(kLightAccent) / sizeof(kLightAccent[0]) == "
               "sizeof(kLightCtls) / sizeof(kLightCtls[0]), "
               "\"kLightAccent desynced\");")
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
