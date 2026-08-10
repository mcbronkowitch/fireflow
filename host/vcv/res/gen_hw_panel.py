#!/usr/bin/env python3
"""Hardware-mode panel: the 60 HP envelope draft (envelope spec 2026-08-08 §4).

Emits res/FireflowHW.svg + src/generated_hw_panel.hpp (namespace spkyhw).
This is the REGROUPING TOOL: iteration 0 is a mechanical translation of
today's grouping onto the hardware grid. Later rounds move controls freely;
test_hw_panel.py pins what may never move — size, keep-outs, footprints,
mirror symmetry, static lettering.

Shares all parameter identity with gen_panel.py (import); defines only
geometry. Run from host/vcv/:  python3 res/gen_hw_panel.py
"""
import os, copy
import gen_panel as gp

HP = 60
W  = HP * gp.MM_PER_HP            # 304.8 mm
Hh = 128.5
CX = W / 2.0
KEEP_TOP, KEEP_BOT = 9.0, 119.5   # rails + M3 screws own the rest

# Hardware size class per parameter BASE name (spec 2026-08-10 §1). This is
# deliberately NOT derived from c.kind: KNOBC means bipolar and KNOBI means
# detented -- statements about the screen widget's behaviour, not about a
# diameter. A centre-detent pot ships in every size.
# Task 1 reproduces today's kind-derived assignment exactly, so the byte
# compare in test_committed_files_match_the_generator proves the refactor is
# inert. Task 2 is what actually changes it.
HW_SIZE = {
    # --- deck: one head per group (spec 2026-08-10 §1/§2) ---
    "MOD": "G", "DENSITY": "G",                       # MOD group
    "RATE": "S", "SHAPE": "S", "SMOOTH": "S", "RANGE": "S", "MELODY": "S",
    "COLOR": "G",                                     # PITCH group
    "TUNE": "S", "DETUNE": "S",
    "FILT": "G", "SOURCE": "G",                       # VOICE group
    "ATTACK": "S", "DECAY": "S", "RES": "S", "SUB": "S", "STAGES": "S",
    "FLUX": "G",                                      # FLUX group
    "FLUXRATE": "S", "FLUXFB": "S", "LINK": "S",
    "REV_MIX": "G",                                   # ROOM send
    "COMP": "G", "GRIT": "S",                         # OUT group
    "STEPS": "S", "SONG": "S",                        # PLAY group
    "ENGINE": "P", "REC": "P",
    # --- centre ---
    "MORPH": "G", "TIDE": "S", "CHOKE": "S",          # BLEND
    "TEMPO": "S", "COUPLE": "S", "SHUFFLE": "S",      # CLOCK
    "SCALE": "S", "DRIFT": "S",                       # TONALITY
    "REV_DECAY": "G", "REV_SIZE": "S", "REV_TONE": "S", "REV_DIFF": "S",
}

CLASS_R = {"G": 8.0, "S": 5.5, "P": 4.0, "J": 4.0, "L": 1.5}

# Baseline offset from the glyph centre to the caption, per class. Jacks read
# ABOVE their glyph (negative), everything else below. Numerically identical
# to the old kind-keyed LBL_DY_HW (10.5/8.0/6.5/-6.0/0.0 for
# BIGKNOB+KNOBC/SMKNOB+KNOBI/SW2+LATCH+SMBTN/IN+OUT/LIGHT) -- Task 1 changes
# only how the offset is looked up, not its value.
CLASS_LBL_DY = {"G": 10.5, "S": 8.0, "P": 6.5, "J": -6.0, "L": 0.0}


def hw_class(enum):
    """Size class for a full enum name. Jacks and LEDs come from the shared
    inventory's kind (they have no hardware choice to make); everything a
    finger turns or presses comes from HW_SIZE."""
    if enum.startswith("CV_"):
        return "J"
    if enum in ("MODBTN", "SHIFTBTN"):
        return "P"
    if enum in _JACK_ENUMS:
        return "J"
    if enum in _LIGHT_ENUMS:
        return "L"
    # Load-bearing order: this endswith("_L") heuristic is what catches the
    # HW_ONLY LEDs (ENG0_A_L, REC_A_L, ...), which have no entry in
    # _LIGHT_ENUMS. It must stay BELOW the _JACK_ENUMS check above, or
    # IN_L/OUT_L (real jacks, not lights) would be misread as LEDs -- this
    # already happened once, in Task 4.
    if enum.endswith("_L"):
        return "L"
    base = enum[:-2] if enum.endswith(("_A", "_B")) else enum
    return HW_SIZE[base]


_JACK_ENUMS = {c.enum for c in gp.INPUTS} | {c.enum for c in gp.OUTPUTS}
_LIGHT_ENUMS = {c.enum for c in gp.LIGHTS}

# --- slot map (spec 2026-08-10 §3) ------------------------------------------
# Row rhythm, top to bottom: a status/transport strip, two two-row bands, the
# jack row. The four CV targets sit in the LOWEST knob row so their jacks are
# adjacent, not merely aligned (spec §4).
Y_TOP = 15.0                      # transport + the whole LED field
Y_B1K, Y_B1G = 30.0, 46.0         # band 1: MOD, FLUX, ROOM send
Y_B2K, Y_B2G = 66.0, 86.0         # band 2: VOICE, PITCH, OUT -- the CV targets
JACK_Y = 107.0
SD_X, SD_Y, SD_W, SD_H = 152.4, 110.0, 15.0, 12.0

# The four CV columns. A jack and its target share this x exactly; the guard
# test_cv_sits_under_its_target holds them together.
X_FILT, X_TIMB, X_COLOR, X_LVL = 23.0, 42.5, 76.25, 100.0

DECK_POS = {
    # status + transport strip
    "STEPS":  (14.0, Y_TOP), "SONG":   (26.5, Y_TOP),
    "ENGINE": (38.5, Y_TOP), "REC":    (69.5, Y_TOP),
    # band 1 -- MOD is ONE engine object (_parts[p].mod()); MOD is its depth
    "RATE":   (14.0, Y_B1K), "SHAPE":  (26.5, Y_B1K), "SMOOTH": (39.0, Y_B1K),
    "RANGE":  (51.5, Y_B1K), "MELODY": (64.0, Y_B1K),
    "MOD":    (26.5, Y_B1G), "DENSITY": (51.5, Y_B1G),
    # band 1 -- FLUX, then the deck's way into the shared room
    "FLUXRATE": (76.0, Y_B1K), "FLUXFB": (88.5, Y_B1K), "LINK": (101.0, Y_B1K),
    "FLUX":     (88.5, Y_B1G), "REV_MIX": (110.0, Y_B1G),
    # band 2 -- the CV targets and their satellites
    "ATTACK": (14.0, Y_B2K), "DECAY": (26.5, Y_B2K),
    "RES":    (39.0, Y_B2K), "SUB":   (51.5, Y_B2K),
    "FILT":   (X_FILT, Y_B2G), "SOURCE": (X_TIMB, Y_B2G),
    "TUNE":   (70.0, Y_B2K), "DETUNE": (82.5, Y_B2K),
    "COLOR":  (X_COLOR, Y_B2G),
    "GRIT":   (100.0, Y_B2K), "COMP": (X_LVL, Y_B2G),
    # deliberate dual assignment: BEND rides ATTACK's knob (spec §6)
    "STAGES": (14.0, Y_B2K),
}

CENTER_POS = {
    "TEMPO":  (127.4, Y_B1K), "COUPLE": (139.9, Y_B1K),
    "SHUFFLE": (152.4, Y_B1K), "SCALE": (164.9, Y_B1K),
    "DRIFT":  (177.4, Y_B1K),
    "TIDE":   (140.4, Y_B1G), "CHOKE":  (164.4, Y_B1G),
    "MORPH":  (152.4, 62.0),
    "REV_DECAY": (152.4, 79.0),
    "REV_SIZE": (134.4, 94.0), "REV_TONE": (152.4, 94.0),
    "REV_DIFF": (170.4, 94.0),
}

JACK_POS = {"PITCH_A": 54.0, "GATE_A": 65.0,
            "IN_L": 112.0, "IN_R": 126.0, "CLOCK": 140.0,
            "RESET": 164.8, "OUT_L": 178.8, "OUT_R": 192.8,
            "GATE_B": W - 65.0, "PITCH_B": W - 54.0}

# The two LEDs the shared inventory already knows about sit at the right end
# of each deck's status strip; the other 14 arrive with HW_ONLY in task 4.
LIGHT_POS = {"REC_A_L": (77.0, Y_TOP), "GATE_A_L": (81.0, Y_TOP),
             "REC_B_L": (W - 77.0, Y_TOP), "GATE_B_L": (W - 81.0, Y_TOP)}

def place(c):
    """Clone a gen_panel control onto the hardware grid."""
    n = copy.copy(c)
    n.r = CLASS_R[hw_class(c.enum)]
    n.lbl = None                    # no radial orbit labels on the hw grid
    base, x = c.enum, None
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
HW_INPUTS  = [place(c) for c in gp.INPUTS]
HW_OUTPUTS = [place(c) for c in gp.OUTPUTS]
HW_LIGHTS  = [place(c) for c in gp.LIGHTS]

# --- hardware-only inventory (spec 2026-08-10 §4/§5) ------------------------
# Elements that exist on sheet metal and have no VCV id: two reserved pads,
# eight CV inputs, sixteen further LEDs. They must NOT enter HW_PARAMS --
# test_same_runtime_params_same_order holds that list against the shared
# inventory. They DO need a C++ table: Rack does not render the SVG's text
# (NanoSVG), so every caption meant to be readable in the rehearsal goes
# through HwPanelText.
class HwOnly:
    __slots__ = ("enum", "kind", "x", "y", "r", "label", "tip")

    def __init__(self, enum, cls, x, y, label, tip):
        self.enum, self.x, self.y, self.label, self.tip = enum, x, y, label, tip
        self.kind = {"P": gp.LATCH, "J": gp.IN, "L": gp.LIGHT}[cls]
        self.r = CLASS_R[cls]


_CV = (("FILT", X_FILT), ("TIMB", X_TIMB), ("COLOR", X_COLOR), ("LVL", X_LVL))
# The four targets are read off engine/mod/lane_id.h, not chosen: LANE_SIZE,
# LANE_SOURCE, LANE_PITCH and LANE_LEVEL are slots the internal modulator
# already drives, and each has a big knob. LANE_MOTION (SHAPE) gets no jack --
# SHAPE is small and sits in band 1, where a cable would cross the playing
# surface. Deliberate deletion, spec §4.
_ENGINE_LED_X = [45.5, 49.5, 53.5, 57.5, 61.5]
_STATUS_LED_X = [85.0, 89.0]          # FLOW/STEP, CAPTURE (REC and GATE are shared)

HW_ONLY = [
    HwOnly("MODBTN", "P", 140.4, Y_TOP, "MOD", "reserved, no function"),
    HwOnly("SHIFTBTN", "P", 164.4, Y_TOP, "SHIFT", "reserved, no function"),
    HwOnly("TEMPO_L", "L", 149.4, Y_TOP, "", "tempo"),
    HwOnly("SYNC_L", "L", 155.4, Y_TOP, "", "sync"),
]
for _side, _sx in (("A", lambda v: v), ("B", lambda v: W - v)):
    for _name, _x in _CV:
        HW_ONLY.append(HwOnly(f"CV_{_name}_{_side}", "J", _sx(_x), JACK_Y,
                              _name, f"CV in -> {_name}"))
    for _i, _x in enumerate(_ENGINE_LED_X):
        HW_ONLY.append(HwOnly(f"ENG{_i}_{_side}_L", "L", _sx(_x), Y_TOP, "",
                              "engine"))
    for _name, _x in zip(("FLOW", "CAP"), _STATUS_LED_X):
        HW_ONLY.append(HwOnly(f"{_name}_{_side}_L", "L", _sx(_x), Y_TOP, "",
                              _name.lower()))

ALL_HW = HW_PARAMS + HW_INPUTS + HW_OUTPUTS + HW_LIGHTS + HW_ONLY

# Static lettering that isn't a control's own caption. TITLE_TEXT is named
# rather than inlined because test_hw_panel.py's rail-keepout guard exempts
# it by name: the title sits at y=6.0, inside KEEP_TOP=9.0, but the M3
# mounting screws live at the panel's left/right edges (x near 0 and W), not
# near x=CX where the title is centred, so nothing mechanical collides with
# it. Every other entry in TEXTS is expected to clear the rails for real.
TITLE_TEXT = "FIREFLOW HW DRAFT 60HP"

TEXTS = [(CX, 6.0, 3.2, 0.9, gp.INK, "middle", TITLE_TEXT)]

# BEND shares ATTACK's knob (deliberate dual assignment, spec §6). A screen
# widget can gate one caption away by engine state; an aluminium plate can't
# -- it prints both words. The plate reads BEND above the shared knob, ATK
# below it, slightly smaller so the pair reads as a stack, not a collision.
STAGES_LBL_Y_OFFSET = -7.0
STAGES_LBL_SIZE = 1.8

LBL_MARGIN = 1.5   # a caption anchor clears every FOREIGN footprint by this


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
    """Caption placement, by rule rather than by named exception.

    The old code carried FLUXFB_LBL_Y_OFFSET because GRIT went bipolar and
    inherited KNOBC's larger clearance. That cause is gone with the size
    classes, the pattern is not: at 16 mm row spacing a small knob's default
    caption lands 8.1 mm from the 8.0 mm knob below it. Shortening the offset
    cannot fix the general case -- for two big knobs 17 mm apart, any offset
    short enough to clear the neighbour is inside the control's OWN footprint.
    So the caption steps aside instead: below, then above, then to the right.
    """
    if not c.label:
        # Nothing is drawn, so nothing needs a clear position. The header
        # table still carries the fields, so keep the old default rather
        # than sending an invisible caption on a search it cannot win:
        # a LIGHT's dy is 0.0, which collapses "below" and "above" onto the
        # control itself and leaves only "right", inside its neighbour.
        return (c.x, c.y, "middle", 2.2, gp.INK)
    if c.enum.startswith("STAGES_"):
        return (c.x, c.y + STAGES_LBL_Y_OFFSET, "middle", STAGES_LBL_SIZE, gp.INK)
    dy = CLASS_LBL_DY[hw_class(c.enum)]
    # Candidate 3 steps away from the mirror axis, not always toward +x: on
    # the right half a mirrored _A/_B pair must produce mirrored captions,
    # so the step direction AND the anchor flip together (spec 2026-08-10
    # §6 fix-2). c.x == CX (a centre control) keeps the left-half behaviour.
    third = ((c.x + c.r + 1.0, c.y + 1.0, "start") if c.x <= CX else
             (c.x - c.r - 1.0, c.y + 1.0, "end"))
    for lx, ly, anchor in ((c.x, c.y + dy, "middle"),
                           (c.x, c.y - dy, "middle"),
                           third):
        if _caption_is_clear(c, lx, ly):
            return (lx, ly, anchor, 2.2, gp.INK)
    raise ValueError(f"no clear caption position for {c.enum} -- the geometry "
                     f"is too tight, move a control (spec 2026-08-10 §3)")

# =============================================================================
#  SVG
# =============================================================================
def mm(v): return f"{v:.3f}"

def svg():
    P = []
    P.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{mm(W)}mm" '
              f'height="{mm(Hh)}mm" viewBox="0 0 {mm(W)} {mm(Hh)}">')
    P.append(f'<rect x="0" y="0" width="{mm(W)}" height="{mm(Hh)}" fill="{gp.PAPER}"/>')
    P.append(f'<rect x="0.3" y="0.3" width="{mm(W-0.6)}" height="{mm(Hh-0.6)}" '
              f'fill="none" stroke="{gp.LINE}" stroke-width="0.4"/>')
    # keep-out rails, drawn as dashed lines top and bottom
    for y in (KEEP_TOP, KEEP_BOT):
        P.append(f'<line x1="0" y1="{mm(y)}" x2="{mm(W)}" y2="{mm(y)}" '
                  f'stroke="{gp.LINE}" stroke-width="0.3" stroke-dasharray="2,1.5"/>')
    # SD slot: a cutout, not a control. Drawn as an outline because it is a
    # hole in the plate, and 3 mm below the jack centres because TONE's
    # caption needs the room (spec 2026-08-10 §3).
    P.append(f'<rect x="{mm(SD_X - SD_W / 2)}" y="{mm(SD_Y - SD_H / 2)}" '
              f'width="{mm(SD_W)}" height="{mm(SD_H)}" rx="1" fill="none" '
              f'stroke="{gp.INK}" stroke-width="0.5" stroke-dasharray="1.5,1"/>')
    # No legend: the dashed outline reads as a cutout on its own, and a word
    # centred over a milled hole is a word the hole cuts away. (An earlier
    # draft put "SD" here via the TEXTS table; it sat inside the cutout on a
    # real plate and was removed rather than relocated -- see the whole-
    # branch review, fix wave 2026-08-10.)
    # one glyph per control, by kind
    for c in ALL_HW:
        if c.kind in (gp.IN, gp.OUT):
            fill = "#1f4d44" if c.enum.startswith("CV_") else gp.GRAPHITE
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(c.r)}" '
                      f'fill="{fill}" stroke="{gp.LINE}" stroke-width="0.35"/>')
        elif c.kind == gp.LIGHT:
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(c.r)}" '
                      f'fill="#1a1206" stroke="#3a2c12" stroke-width="0.25"/>')
        elif c.kind in (gp.SW2, gp.LATCH, gp.SMBTN):
            P.append(f'<rect x="{mm(c.x-c.r)}" y="{mm(c.y-c.r)}" width="{mm(2*c.r)}" '
                      f'height="{mm(2*c.r)}" rx="1.0" fill="{gp.WHITE}" '
                      f'stroke="{gp.LINE}" stroke-width="0.3"/>')
        else:
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(c.r)}" '
                      f'fill="{gp.GRAPHITE}" stroke="{gp.LINE}" stroke-width="0.3"/>')
        if c.label:
            lx, ly, anchor, size, colour = hw_label(c)
            P.append(f'<text x="{mm(lx)}" y="{mm(ly)}" fill="{colour}" '
                      f'text-anchor="{anchor}" font-family="monospace" '
                      f'font-size="{size}">{c.label}</text>')
    for (x, y, size, spacing, col, anchor, txt) in TEXTS:
        P.append(f'<text x="{mm(x)}" y="{mm(y)}" fill="{col}" text-anchor="{anchor}" '
                  f'font-family="monospace" font-size="{size}" '
                  f'letter-spacing="{spacing}" font-weight="bold">{txt}</text>')
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
    L2.extend(emit_table("kInputCtls", HW_INPUTS))
    L2.extend(emit_table("kOutputCtls", HW_OUTPUTS))
    L2.extend(emit_table("kLightCtls", HW_LIGHTS))
    L2.append("// Hardware-only: no VCV id. Rack does not render SVG text,")
    L2.append("// so these captions must come from here (spec 2026-08-10 §5).")
    L2.append("static const HwOnlyCtl kHwOnlyCtls[] = {")
    for c in HW_ONLY:
        lx, ly, anchor, size, colour = hw_label(c) if c.label else (
            c.x, c.y, "middle", 2.2, gp.INK)
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

if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    with open(os.path.join(here, "FireflowHW.svg"), "w", encoding="utf-8") as f:
        f.write(svg())
    with open(os.path.join(root, "src", "generated_hw_panel.hpp"), "w", encoding="utf-8") as f:
        f.write(header())
    print("wrote res/FireflowHW.svg and src/generated_hw_panel.hpp")
    print(f"params={len(HW_PARAMS)} inputs={len(HW_INPUTS)} "
          f"outputs={len(HW_OUTPUTS)} lights={len(HW_LIGHTS)}  panel={HP}HP")
