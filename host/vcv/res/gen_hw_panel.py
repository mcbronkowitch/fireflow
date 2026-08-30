#!/usr/bin/env python3
"""Hardware-mode panel: the 60 HP envelope draft (envelope spec 2026-08-08 §4).

Emits res/FireflowHW.svg + src/generated_hw_panel.hpp (namespace spkyhw).
Geometry is the 2026-08-10 redistribution as drawn in
docs/hardware/2026-08-10-hw-panel-redistribution.svg (graphics round 15 Aug).

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
import gen_panel as gp

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
PAD_FILL   = "#dbe9ef"            # button keycap
PAD_STROKE = "#3c525c"
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
# on. The frames are drawn against THESE, which is what buys the air between
# the boxes (design note 2a).
# P is 4.0 because that is the keycap this file actually draws (an 8 mm
# square), not the 3.1 the design's cap-radius rule would hand a 4.0 slot.
BODY_R = {"G": 6.0, "S": 4.4, "P": 4.0, "J": 3.1, "L": 1.5}

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
RACK_R = {"G": 4.80, "S": 3.02, "P": 3.05, "J": 4.02, "L": 1.50}

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

# Which pots wear the printed mod wreath (spec 2026-08-22 §5, variant B):
# exactly the faces with a depth param. Derived from gen_panel's tables so
# the plate and the param block cannot drift apart.
MOD_WREATHED = ({f"{b}_A" for b, _, _, _ in gp.MOD_DECK_TARGETS}
                | {f"{b}_B" for b, _, _, _ in gp.MOD_DECK_TARGETS}
                | {b for b, _, _, _ in gp.MOD_CENTER_TARGETS})


def zone_of(x):
    """Which of the three accent zones x falls in. The plate is one flat
    surface now -- this only picks a group field's tint, a lamp's lit colour
    and a mod wreath's ring colour."""
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
    # FILT is big again since 2026-08-23. It went G -> S on 2026-08-19 for
    # RASTER, not for room: at r=8.5 its neighbour spacing is 14.5 mm against
    # 12 for every other pair, so it could not sit on the 13 mm pitch beside
    # TIMB and DPTH. That reason is gone -- FILT no longer sits IN the pitch.
    # It moved to the end of VOICE's lower row (column 4, under SUB) and the
    # column between it and DPTH stays empty, which is what buys the 14.5.
    "FILT": "G",
    "SOURCE": "S",                                 # TIMB is small (graphics round)
    "DEPTH": "S",                                   # FEED, the VOICE knob
    "ATTACK": "S", "DECAY": "S", "RES": "S", "SUB": "S", "STAGES": "S",
    "FLUX": "G",
    "FLUXRATE": "S", "FLUXFB": "S", "LINK": "S",
    # SEND went G -> S on 2026-08-30 when it left ROOM for LEVEL. Not a taste
    # call: at r=8.5 it needs 14.5 mm to its neighbour, and the LEVEL band runs
    # on TIMING's 13.0 pitch. A big SEND cannot stand on that pitch at all --
    # see the guard test_level_band_is_evenly_divided.
    "REV_MIX": "S",
    "COMP": "G", "GRIT": "S",
    "STEPS": "S", "SONG": "S",
    "ENGINE": "S", "REC": "P",                     # ENGINE is a 5-zone detent pot
    "MORPH": "G", "TIDE": "S", "CHOKE": "S", "PACE": "S", "PULL": "S",
    "TEMPO": "S", "COUPLE": "S", "SHUFFLE": "S",
    "SCALE": "S", "DRIFT": "S",
    "REV_DECAY": "G", "REV_SIZE": "S", "REV_TONE": "S", "REV_DIFF": "S",
}

CLASS_R = {"G": 8.5, "S": 6.0, "P": 4.0, "J": 4.0, "L": 1.5}
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

# Row rhythm (spec 2026-08-10 §8). The middle band runs on three lines and
# nothing sits between them: small knobs at Y_B1K, the four that had drifted
# to 47.61/49.25/50.22 gathered on Y_B1M, and every big cap on Y_B1G. FILT
# came DOWN to that line rather than the other three coming up -- MORPH
# cannot rise past 52.0 without displacing SYNC's caption, and shortening
# this band would leave the jack row without a margin (only 0.75 mm of slack
# is left in the whole row chain).
Y_TOP = 14.5
Y_B1K, Y_B1M, Y_B1G = 34.0, 50.22, 53.0
# Y_B2K was 76.0 until 2026-08-30, when the last big cap left it for Y_B2B.
# It is 74.40 for one reason and it is arithmetic, not taste: a small body is
# 4.40, so 74.40 puts its top on 70.00 -- exactly where the big caps' tops
# used to be. The row's topmost ink therefore does not move, its margin stays
# 2.15, and its bottom edge stays on 107.15. Change this number and the whole
# chain below shifts; test_row3_ceiling_holds_at_seventy is the guard.
Y_B2K, Y_B2G = 74.40, 95.0
# The line REV_DECAY has always sat on, 3 mm under the small caps. FLUX's MIX
# joined it 2026-08-30. Note what a move onto this line costs: while ANY big
# cap is still on Y_B2K its body top (70.00) is the row's topmost ink, so the
# row frame does not move and the move is free. The last one to leave takes
# the topmost ink down to the small caps at 71.60, and row_frames() mirrors
# that onto the bottom edge -- 1.60 mm the chain to the jack row has not got.
Y_B2B = 79.0
# The LEVEL band, on the line REV_TONE already used. When it was chosen this
# was forced: with the small caps still on 76.00, REV_SIZE's caption sat at
# 84.00 and the shoulder/foot chain was 0.70 mm short on Y_B2G. That reason
# EXPIRED on 2026-08-30 when Y_B2K went to 74.40 and the caption rose to
# 82.40 -- Y_B2G would fit again now. The band stays here for the reason it
# is kept, not the one that put it here: FB, PAN's slot, GRIT, SEND and TONE
# read as ONE line across the plate, and REV_TONE anchors it.
Y_B2L = 97.0
JACK_Y = 114.0
SD_X, SD_Y, SD_W, SD_H = 152.4, JACK_Y, 11.0, 6.0

# The small-BIG-small figure, used twice on this plate: TIMING's TIDE/MRPH/
# PACE and, since 2026-08-23, VOICE's lower row. Both are written from these
# two numbers so the two cannot drift apart -- the whole reason VOICE reads
# calm in that shape is that the eye has already learnt it from TIMING.
# CENTRE_PITCH is what a small knob needs beside a big one (14.5 mm) plus a
# margin; VOICE_MID is the centre of deck A's VOICE frame, which GROUP_ROWS
# row 2 puts at 57.75..120.00. test_hw_panel pins VOICE_MID against the real
# frame rather than trusting this comment.
CENTRE_PITCH = 16.0
VOICE_MID = 88.875

# CV jack columns — uniform 11.5 mm raster, not under the knobs (spec §13).
X_COLOR, X_FILT, X_TIMB, X_LVL = 79.0, 90.5, 102.0, 113.5

# ---------------------------------------------------------------------------
#  The LEVEL band (2026-08-30). SEND left ROOM for LEVEL, which is where it
#  belongs -- it is a per-deck send, not a reverb control. LEVEL's frame has no
#  room for it, so the frame grows a FOOT past the deck edge and the three
#  small knobs stand in the band that main box and foot form together.
#
#  Only ONE number here is chosen: LEVEL_H_MARGIN. Everything else follows.
#  The pitch is TIMING's own (TEMP - SYNC - SHFL); the outer slots hug their
#  edges with the margin, which centres the group by construction; and the
#  foot's right edge is wherever SEND's margin ends.
#
#  Why 5.40 and not the 2.15 the ROW uses: 2.15 is the VERTICAL margin. The
#  plate's knob rows hold 3.10..9.00 horizontally (median 5.98), and every mm
#  the band takes costs ROOM's tongue two -- it closes in from both sides. The
#  balance point is 16.25 - 2m = m, i.e. 5.42; 5.40 rounds it and leaves the
#  band 5.40 and REV_TONE 5.45. Pinned by test_level_band_is_evenly_divided.
LEVEL_BAND_L = 93.95              # LEVEL A's own left edge; the guard pins this
LEVEL_PITCH = 13.00               # TEMP -> SYNC -> SHFL, unchanged since
LEVEL_H_MARGIN = 5.40             # the one free number
_lvl0 = LEVEL_BAND_L + BODY_R["S"] + LEVEL_H_MARGIN
# Slot 0 is deliberately EMPTY. It is held for PAN, which has no ParamId yet;
# an empty slot costs the same geometry as a filled one, so it is cheaper to
# pay once now than to re-pitch the band later. Do not "tidy" it away --
# test_level_band_holds_an_empty_slot exists to stop exactly that.
LEVEL_SLOTS = (_lvl0, _lvl0 + LEVEL_PITCH, _lvl0 + 2 * LEVEL_PITCH)

DECK_POS = {
    "STEPS":  (35.00, Y_TOP), "SONG": (48.00, Y_TOP),
    "RATE":   (61.00, Y_TOP), "MELODY": (74.00, Y_TOP),
    "REC":    (101.00, Y_TOP),
    "SHAPE":  (18.25, Y_B1K), "SMOOTH": (31.25, Y_B1K), "RANGE": (44.25, Y_B1K),
    "MOD":    (21.75, Y_B1G), "DENSITY": (40.75, Y_B1G),
    "ATTACK": (68.25, Y_B1K), "DECAY": (81.25, Y_B1K),
    "RES":    (94.25, Y_B1K), "SUB": (107.25, Y_B1K),
    # VOICE's lower row is TIMING's row, copied: small - BIG - small, the big
    # one centred, on the CENTRE_PITCH raster. TIDE/MRPH/PACE has stood that
    # way since the graphics round and reads calm even though it, too, spans
    # two of the band's lines -- because it is symmetric about its big cap.
    #
    # The lower row left the ATTACK/DECAY/RES/SUB column raster to get there,
    # and that is the point. It followed those columns until 2026-08-23, when
    # FILT went back to a big cap and landed at the end of the row: that left
    # two small knobs crowded at 4.20 mm, a 15.60 mm hole, and a big cap alone
    # on a third line -- three heights in a two-row group, which is what made
    # VOICE read restless next to MOTION. Approved by eye 2026-08-23 against
    # three alternatives; the numbers here are measured, not chosen:
    # body gaps 5.60/5.60, frame air 10.72 either side.
    #
    # (The old column 3 was RES's partner DAMP, the EDGE knob, until
    # 2026-08-20; ENGINE opened this row at 70.25 before it moved to its own
    # status-row frame, which is what freed the slot DPTH holds now.)
    "SOURCE": (VOICE_MID - CENTRE_PITCH, Y_B1M),
    "FILT":   (VOICE_MID, Y_B1G),
    "DEPTH":  (VOICE_MID + CENTRE_PITCH, Y_B1M),
    "ENGINE": (16.25, Y_TOP),
    "TUNE":   (17.00, Y_B2K), "DETUNE": (30.00, Y_B2K),
    "COLOR":  (23.50, Y_B2G),
    "FLUX":   (67.00, Y_B2B),
    # FB came off Y_B2G onto the LEVEL band's line 2026-08-30, so the plate's
    # bottom line reads straight across: FB, PAN's slot, GRIT, SEND, TONE.
    # Free of charge -- its caption lands on 105.00, where REV_TONE's already
    # was, so the row's ink and therefore its frame do not move.
    "FLUXRATE": (54.00, 89.86), "FLUXFB": (67.00, Y_B2L), "LINK": (80.00, 89.86),
    # LVL keeps its place; only the band below it is new. GRIT came off Y_B2G
    # to join it, and SEND came the whole way over from ROOM.
    "COMP":   (106.50, Y_B2B),
    "GRIT":   (LEVEL_SLOTS[1], Y_B2L),
    "REV_MIX": (LEVEL_SLOTS[2], Y_B2L),
    "STAGES": (68.25, Y_B1K),
}

CENTER_POS = {
    # The GLOBAL centre row carries FOUR knobs since 2026-08-23. It used to be
    # three on a 13.0 mm pitch centred on 152.40; PULL made it four, so the
    # group was re-centred and the three existing knobs each moved 6.5 mm left.
    # The pitch is UNCHANGED at 13.0 and so is the 1.0 mm gap between bodies --
    # this row sets no new clearance precedent, it just uses the width the
    # centre cell always had (measured: bodies span 126.90..177.90 inside a
    # cell of 123.00..181.80, so 3.90 mm spare on each side).
    #
    # What it costs, and it is deliberate: with an even count no knob sits on
    # the centre line any more, and GLOBAL no longer flushes with TEMPO/COUPLE/
    # SHUFFLE in the TIMING row below. Re-centring is not optional -- leaving
    # SCALE at 139.40 and hanging PULL off the right end lands it at 178.40,
    # whose body overruns the cell edge by 2.60 mm.
    "SCALE":  (132.90, Y_TOP), "DRIFT": (145.90, Y_TOP), "CHOKE": (158.90, Y_TOP),
    "TEMPO":  (139.40, Y_B1K), "COUPLE": (152.40, Y_B1K), "SHUFFLE": (165.40, Y_B1K),
    # The small-BIG-small figure VOICE's lower row now copies. Same two
    # numbers, so re-tuning one row re-tunes both -- which is what keeps them
    # reading as the same figure instead of two rows that merely resemble it.
    "TIDE":   (W / 2 - CENTRE_PITCH, Y_B1M), "MORPH": (W / 2, Y_B1G),
    "PACE":   (W / 2 + CENTRE_PITCH, Y_B1M),
    "REV_SIZE": (136.40, Y_B2K), "REV_DECAY": (152.40, Y_B2B), "REV_DIFF": (168.40, Y_B2K),
    "REV_TONE": (152.40, 97.00),
    # PULL (spec 2026-07-19 pull-chord-gravity), fourth in the GLOBAL row and
    # deliberately next to CHOKE: those two are the only bipolar controls in
    # the whole centre (-1..+1; every other centre knob is 0..1 or stepped),
    # they share the sign-picks-a-deck convention PULL was designed on, and
    # both read "how do the two decks relate to each other".
    #
    # It shipped loose here first (2026-08-22): a scan for a slot that needed
    # NOTHING to move found none -- best margin -1.201 mm in ROOM, -0.945 in
    # GLOBAL, +0.100 in TIMING, which is fabrication noise. That was the right
    # answer to the wrong question. Re-pitching the row was always the way in;
    # it just was not an implementer's call to make. Bastian made it 2026-08-23.
    "PULL": (171.90, Y_TOP),
    # MODBTN is a real latch param now (spec 2026-08-22 mod-latch-layer §5),
    # placed through the same place() path as every sound knob; the
    # coordinates are unchanged from its old HW_ONLY slot.
    "MODBTN": (W - 14.00, JACK_Y),
}

JACK_POS = {"PITCH_A": 56.00, "GATE_A": 67.50,
            "IN_L": 33.00, "IN_R": 44.50, "CLOCK": 136.00,
            "RESET": 168.80, "OUT_L": 260.30, "OUT_R": 271.80,
            "GATE_B": W - 67.50, "PITCH_B": W - 56.00,
            "MOD1_A": X_COLOR, "MOD2_A": X_FILT, "MOD3_A": X_TIMB, "MOD4_A": X_LVL,
            "MOD1_B": W - X_COLOR, "MOD2_B": W - X_FILT,
            "MOD3_B": W - X_TIMB, "MOD4_B": W - X_LVL}

# Knob-owned lamps: the caption and the LED are one block under the knob,
# word then air then LED, centred on the knob x. Same reading order on both
# decks -- not an optical [LED][word] mirror. Pads, the CLOCK jack lamp and
# the ceiling lamp stay as satellites (LIGHT_POS below).
KNOB_LAMPS = {
    "SRC_A_L": "SOURCE_A", "SRC_B_L": "SOURCE_B",
    "FLT_A_L": "FILT_A",   "FLT_B_L": "FILT_B",
    "CLR_A_L": "COLOR_A",  "CLR_B_L": "COLOR_B",
    "LVL_A_L": "COMP_A",   "LVL_B_L": "COMP_B",
    "SONG_A_L": "SONG_A",  "SONG_B_L": "SONG_B",
    "GATE_A_L": "ATTACK_A", "GATE_B_L": "ATTACK_B",
    "TEMPO_L": "TEMPO",
}
KNOBS_WITH_LAMPS = set(KNOB_LAMPS.values())
LED_CAPTION_GAP = 0.8   # mm of air between the word's ink and the LED body
CAPTION_SIZE = 2.2      # same size hw_label prints


def caption_led_cluster(knob):
    """(cap_x, cap_y, led_x, led_y) for a knob-owned lamp.

    LED centre sits on the caption's glyph midline. The body then hangs
    ~0.7 mm below the baseline; _row_ink() ignores these lamps so the
    frame chain does not grow -- the caption already defines that floor.
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
LIGHT_POS = {"REC_A_L": (108.50, Y_TOP), "REC_B_L": (W - 108.50, Y_TOP),
             "SYNC_L":     (130.50, 114.00),
             "MODBTN_L":   (285.30, 114.00),  "SHIFTBTN_L": ( 19.50, 114.00),
             # Limiter lamp: jack-row satellite of OUT_R, outboard, same y as
             # MODBTN_L. Unsuffixed, so _twin_enum declares no mirror partner.
             # Just outside the OUT frame (right edge 276.80); inside the
             # frame the CLASS_R circles of jack and lamp overlap.
             "CEIL_L":     (JACK_POS["OUT_R"] + CLASS_R["J"] + 1.5, JACK_Y)}


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
HW_INPUTS  = [place(c) for c in gp.INPUTS] + [place(c) for c in gp.HW_MOD_INPUTS]
HW_OUTPUTS = [place(c) for c in gp.OUTPUTS]
_SKIP_HW_LIGHTS = {"FLOW_A_L", "FLOW_B_L"}
HW_LIGHTS  = [place(c) for c in gp.LIGHTS + gp.HW_ONLY_LIGHTS
              if c.enum not in _SKIP_HW_LIGHTS]


class HwOnly:
    __slots__ = ("enum", "kind", "x", "y", "r", "label", "tip")

    def __init__(self, enum, cls, x, y, label, tip):
        self.enum, self.x, self.y, self.label, self.tip = enum, x, y, label, tip
        self.kind = {"P": gp.LATCH, "J": gp.IN, "L": gp.LIGHT}[cls]
        self.r = CLASS_R[cls]


HW_ONLY = [
    HwOnly("SHIFTBTN", "P", 14.00, JACK_Y, "SHFT", "reserved, no function"),
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
#  Group frames: one fixed raster of drawing boxes
# =============================================================================
# Four rows straight across the plate, 3 mm of air everywhere between boxes,
# a shared outer edge at 8 mm, a shared deck edge at 120 mm, and deck B is
# deck A mirrored. Boxes are cut at CUTS, which is the centre of the 3 mm
# gap, so the arithmetic can never leave a sliver or an overlap.
BOX_GAP = 3.0
DECK_EDGE = 120.0                 # right edge of the last deck-A box
CENTRE_L = 123.0                  # left edge of the centre column

# A box's LOWER BAND may run to a different width than its upper one -- that
# is the whole of the L, expressed once. LEVEL's lower band is WIDER (it
# reaches past the deck edge into the master area); ROOM's is NARROWER by the
# same amount plus the usual gap, which is what makes room for it. Neither
# frame is a special case in the drawing code: both are "a rectangle whose
# bottom part has its own left and right edge".
BAND_INK_MARGIN = 2.15            # the row's own vertical ink margin, reused
                                  # for the horizontal step -- it is a frame
                                  # edge running past ink either way
LEVEL_FOOT_R = LEVEL_SLOTS[2] + BODY_R["S"] + LEVEL_H_MARGIN
FOOT_TOP = Y_B2L - BODY_R["S"] - BAND_INK_MARGIN
SHOULDER_BOT = FOOT_TOP - BOX_GAP          # ROOM's upper band stops here
ROOM_FOOT_L = LEVEL_FOOT_R + BOX_GAP
PLATE_EDGE = 8.0                  # outer edge of a full-width row

# The y/h pair in each row below is a SEED, not the drawn frame: it only
# says which controls belong to the row. The frame that gets drawn is
# derived in row_frames() from what the row actually prints.
# (seed y, seed h, first x, cuts, deck-A names, deck-B names, centre name)
GROUP_ROWS = [
    # ENG is its own frame at the outer edge, left of SEQUENCE on deck A and
    # mirrored on B. The status row used to start at x=28 and leave the plate's
    # outer 20 mm empty; the side keep-out is only 2 mm, so that space was
    # always there. Moving ENG out of VOICE is what pays for DPTH and the
    # free slot beside it (DAMP, the EDGE knob, removed 2026-08-20).
    (9.00, 16.0, PLATE_EDGE, [26.00, 85.20],
     ["ENG", "SEQUENCE", "CAPTURE"], ["ENG", "SEQUENCE", "CAPTURE"], "GLOBAL"),
    (28.00, 38.2, PLATE_EDGE, [56.25],
     ["MOTION", "VOICE"], ["MOTION", "VOICE"], "TIMING"),
    (69.20, 38.2, PLATE_EDGE, [42.00, 92.45],
     ["PITCH", "FLUX", "LEVEL"], ["PITCH", "FLUX", "LEVEL"], "ROOM"),
    (110.40, 15.0, 28.00, [50.25, 73.25],
     ["IN", "CV A", "MOD A"], ["OUT", "CV B", "MOD B"], "CLOCK"),
]

# Every group name the plate carries, in reading order: decks top to bottom,
# then the centre column, then the jack row. This printed as a two-digit index
# in front of each legend until 2026-08-30; it outlives the numbering as the
# roster the guard checks the drawn frames against.
GROUP_ORDER = ("ENG", "SEQUENCE", "CAPTURE", "MOTION", "VOICE", "PITCH",
               "FLUX", "LEVEL", "GLOBAL", "TIMING", "ROOM", "IN", "CV", "MOD",
               "CLOCK", "OUT")


LEGEND_SIZE, LEGEND_SPACING = 1.9, 0.5
LEGEND_DY = 0.75                  # legend baseline below a frame's top edge
LEGEND_LIFT = 0.80                # jack row: baseline ABOVE its top edge
LEGEND_INSET = 4.0                # legend's left edge, in from the frame's.
                                  # It is where the struck index used to start,
                                  # so the lettering did not move on 2026-08-30
                                  # -- only the two digits in front of it went.

# The one free number in the whole vertical chain. Not a taste value: the
# status row sits as high as its own legend is allowed to print, so that
# legend's baseline lands exactly on the rail line.
ROW1_TOP = KEEP_TOP - LEGEND_DY


def body_r(c):
    """Radius of the real component body, not the layout clearance circle."""
    return BODY_R[hw_class(c.enum)]


def _row_cells(row):
    """(name, side, x, w) for every frame in a row, left to right."""
    _y, _h, x0, cuts, names_a, names_b, centre = row
    edges = [x0] + list(cuts) + [DECK_EDGE]

    def span(i):
        lo = edges[i] + (BOX_GAP / 2.0 if i else 0.0)
        hi = edges[i + 1] - (BOX_GAP / 2.0 if i + 1 < len(names_a) else 0.0)
        return lo, hi

    cells = []
    for i, n in enumerate(names_a):
        lo, hi = span(i)
        cells.append((n, "A", lo, hi - lo))
    cells.append((centre, "C", CENTRE_L, W - 2 * CENTRE_L))
    for i, n in enumerate(names_b):
        lo, hi = span(i)
        cells.append((n, "B", W - hi, hi - lo))
    return cells


def _row_ink(row):
    """Top and bottom of everything a row PRINTS: real component bodies and
    caption ink. hw_label() raises when a caption has nowhere clear to go --
    that is by design and must stay loud, so it is not caught here."""
    y0, h = row[0], row[1]
    spans = [(x, x + w) for _n, _s, x, w in _row_cells(row)]
    top = bot = None
    for c in ALL_HW:
        if not (y0 <= c.y <= y0 + h):
            continue
        if not any(x0 <= c.x <= x1 for x0, x1 in spans):
            continue
        if c.enum in KNOB_LAMPS:
            # Cluster lamp: hangs slightly below the caption baseline by
            # design. The caption ink already sets the row floor.
            continue
        r = body_r(c)
        edges = [(c.y - r, c.y + r)]
        if c.label:
            _lx, ly, _anchor, size, _col = hw_label(c)
            edges.append((ly - size * FONT_CAP, ly))
        for a, b in edges:
            top = a if top is None else min(top, a)
            bot = b if bot is None else max(bot, b)
    if top is None:
        raise ValueError(f"group row seeded at y={y0} prints nothing")
    return top, bot


def row_frames():
    """(y, h) per row. Every frame hugs its own ink with the SAME margin
    above and below -- a frame whose contents sit high in it reads as a
    mistake, and with captions below their controls that is what a fixed
    row height produces. The rows are then chained by BOX_GAP, so the air
    between them stays uniform and only ROW1_TOP is chosen."""
    out, prev = [], None
    for row in GROUP_ROWS:
        t, b = _row_ink(row)
        m = t - ROW1_TOP if prev is None else t - (prev + BOX_GAP)
        out.append((t - m, (b + m) - (t - m)))
        prev = b + m
    return out


ROW_FRAMES = row_frames()
JACK_ROW_Y = ROW_FRAMES[-1][0]


class Box:
    """A group frame. Rectangular, EXCEPT that its lower band may have its own
    left and right edge -- `foot` is (x0, x1, y): below y the frame spans
    x0..x1 instead of x..x+w. Wider than the box makes an L (LEVEL), narrower
    makes a bracket (ROOM). None means a plain rectangle, which is 24 of 26."""

    __slots__ = ("n", "side", "x", "y", "w", "h", "foot")

    def __init__(self, n, side, x, y, w, h, foot=None):
        self.n, self.side, self.x, self.y, self.w, self.h = n, side, x, y, w, h
        self.foot = foot

    @property
    def bands(self):
        """((x0, x1, y0, y1), ...) -- one entry for a plain box, two when the
        lower band differs. Everything that asks 'what does this frame cover'
        goes through here, so a foot cannot be honoured in one place and
        forgotten in another."""
        if self.foot is None:
            return ((self.x, self.x + self.w, self.y, self.y + self.h),)
        fx0, fx1, fy = self.foot
        return ((self.x, self.x + self.w, self.y, fy),
                (fx0, fx1, fy, self.y + self.h))

    def covers(self, x, y):
        return any(x0 - 1e-9 <= x <= x1 + 1e-9 and y0 - 1e-9 <= y <= y1 + 1e-9
                   for x0, x1, y0, y1 in self.bands)

    def overlaps(self, other):
        for ax0, ax1, ay0, ay1 in self.bands:
            for bx0, bx1, by0, by1 in other.bands:
                if (ax0 < bx1 - 1e-9 and bx0 < ax1 - 1e-9
                        and ay0 < by1 - 1e-9 and by0 < ay1 - 1e-9):
                    return True
        return False

    @property
    def stem(self):
        """The legend without its deck suffix -- an entry in GROUP_ORDER."""
        return self.n[:-2] if self.n.endswith((" A", " B")) else self.n

    @property
    def legend_y(self):
        """Baseline of the group legend.

        Rows 1-3 straddle the frame's top edge, as drawn in the design. The
        jack row cannot: a PJ301M is 8.03 mm across, so its widget reaches
        up to y=109.99 and would bury a legend sitting at 111.15 -- visible
        in Rack, invisible in the SVG, where the jack body is only 6.2 mm.
        So that one row's legend rides just above its frame instead."""
        return (self.y - LEGEND_LIFT if self.y == JACK_ROW_Y
                else self.y + LEGEND_DY)

    @property
    def legend_straddles(self):
        return self.y != JACK_ROW_Y

    @property
    def notch(self):
        """(x0, x1) of the bite this field's top edge takes around its own
        legend, or None where the legend does not straddle the edge."""
        if not self.legend_straddles:
            return None
        _, ink_r, _, _ = text_run(self.x + LEGEND_INSET, self.legend_y,
                                  LEGEND_SIZE, LEGEND_SPACING, "start", self.n)
        return (self.x + LEGEND_INSET - NOTCH_PAD, ink_r + NOTCH_PAD)


# The two frames that are not rectangles, keyed by (name, side). Deck B is
# mirrored here rather than written out, so the two can never disagree.
FEET = {
    ("LEVEL", "A"): (LEVEL_BAND_L, LEVEL_FOOT_R, FOOT_TOP),
    ("ROOM", "C"): (ROOM_FOOT_L, W - ROOM_FOOT_L, SHOULDER_BOT),
}


def _foot_for(n, side, x, w):
    key = (n, "A" if side == "B" else side)
    if key not in FEET:
        return None
    fx0, fx1, fy = FEET[key]
    return (W - fx1, W - fx0, fy) if side == "B" else (fx0, fx1, fy)


def group_boxes():
    """The 26 drawing frames, left to right within each row. Two of them (and
    deck B's mirror of one) carry a foot -- see Box and FEET."""
    out = []
    for row, (y, h) in zip(GROUP_ROWS, ROW_FRAMES):
        for n, side, x, w in _row_cells(row):
            out.append(Box(n, side, x, y, w, h, _foot_for(n, side, x, w)))
    return out


BOXES = group_boxes()


def box_of(c):
    """The frame a control's centre falls in, or None (SHFT/MOD sit loose).

    Asks the box, so a foot counts: SEND's centre is past the deck edge and
    still belongs to LEVEL, which is the entire point of the 2026-08-30 round.
    A plain rectangle comparison here would hand it to nobody."""
    for b in BOXES:
        if b.covers(c.x, c.y):
            return b
    return None


def _field_d(b):
    """One group field as a path: a rounded rect with a bite taken out of the
    top edge where the legend prints.

    A path and not a <rect rx>, because the bite has to come out of the
    OUTLINE. The field is a fill, so the old trick -- a knockout patch laid
    over the frame in the plate's own colour -- has nothing to hide any more:
    it would have to repaint the plate exactly, and the plate is a gradient.
    Cutting the fill instead cannot drift out of step with what is under it.

    Quadratic curves, not elliptical arcs: at NOTCH_R = 0.4 mm the two are
    indistinguishable, and Q takes the sweep-flag question -- and any question
    about NanoSVG's arc parser -- off the table entirely."""
    x, y, w, h = b.x, b.y, b.w, b.h
    fx0, fx1, fy = b.foot if b.foot else (x, x + w, y + h)

    # Vertices clockwise from the top-left, with the notch inserted into the
    # top edge and the foot's step into the sides. Where the foot matches the
    # box on one side, two vertices coincide and _rounded_poly drops them --
    # so an L, a bracket and a plain rectangle all come out of this one list.
    pts = [(x, y)]
    rad = [FIELD_R]
    if b.notch:
        n0, n1 = b.notch
        pts += [(n0, y), (n0, y + NOTCH_DEPTH),
                (n1, y + NOTCH_DEPTH), (n1, y)]
        rad += [NOTCH_R] * 4
    pts += [(x + w, y), (x + w, fy), (fx1, fy), (fx1, y + h),
            (fx0, y + h), (fx0, fy), (x, fy)]
    rad += [FIELD_R] * 7
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
             HW_LEGEND, "start", b.n) for b in BOXES]


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
                      f'fill="{LED_ON[zone_of(c.x)]}"/>')
        elif hw_class(c.enum) == "P":
            P.append(f'<rect x="{mm(c.x-c.r)}" y="{mm(c.y-c.r)}" width="{mm(2*c.r)}" '
                      f'height="{mm(2*c.r)}" rx="1.2" fill="{PAD_FILL}" '
                      f'stroke="{PAD_STROKE}" stroke-width="0.3"/>')
        else:
            # The mounting hole, drawn at the real pot body -- not a cap. Rack
            # puts its own knob widget on top and a plate has a hole here.
            # Variant B mod wreath (docs/superpowers/specs/2026-08-22-mod-latch-
            # layer-design.md §5): a wreathed knob's own body ring is recoloured
            # to the zone accent instead of HW_RING -- same radius, same
            # stroke-width, solid. A non-wreathed knob keeps the plain dark
            # ring. Absence of the accent colour still carries the meaning:
            # the knob keeps its sound function while MOD is latched.
            ring = ACC[zone_of(c.x)] if c.enum in MOD_WREATHED else HW_RING
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(br)}" '
                      f'fill="{HW_WELL}" stroke="{ring}" stroke-width="0.3"/>')
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
