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

# Real clearance radii (mm): what a finger and a nut need, not a pixel.
HW_R = {gp.BIGKNOB: 8.0, gp.KNOBC: 8.0, gp.SMKNOB: 5.5, gp.KNOBI: 5.5,
        gp.SW2: 4.0, gp.LATCH: 4.0, gp.SMBTN: 4.0,
        gp.IN: 4.0, gp.OUT: 4.0, gp.LIGHT: 1.5}
LBL_DY_HW = {gp.BIGKNOB: 10.5, gp.KNOBC: 10.5, gp.SMKNOB: 8.0, gp.KNOBI: 8.0,
             gp.SW2: 6.5, gp.LATCH: 6.5, gp.SMBTN: 6.5,
             gp.IN: -6.0, gp.OUT: -6.0, gp.LIGHT: 0.0}   # jack labels ABOVE

# --- iteration-0 slot map: deck-A coordinates per param BASE name -----------
# Big knobs, 22 mm grid, two rows (5 + 4).
BIG_ROW1_Y, BIG_ROW2_Y = 18.0, 40.0
DECK_POS = {
    "RATE":    (14.0, BIG_ROW1_Y), "SHAPE":  (36.0, BIG_ROW1_Y),
    "DENSITY": (58.0, BIG_ROW1_Y), "SMOOTH": (80.0, BIG_ROW1_Y),
    "RANGE":  (102.0, BIG_ROW1_Y),
    "MELODY":  (25.0, BIG_ROW2_Y), "MOD":    (47.0, BIG_ROW2_Y),
    "TUNE":    (69.0, BIG_ROW2_Y), "COLOR":  (91.0, BIG_ROW2_Y),
    # small rows, 15 mm grid; voice cluster | fx cluster
    "ATTACK":   (12.0, 56.0), "FILT":    (27.0, 56.0), "SUB":     (42.0, 56.0),
    "FLUXRATE": (62.0, 56.0), "FLUX":    (77.0, 56.0), "FLUXFB":  (92.0, 56.0),
    "REV_MIX": (107.0, 56.0),
    "DECAY":    (12.0, 71.0), "RES":     (27.0, 71.0), "SOURCE":  (42.0, 71.0),
    "LINK":     (62.0, 71.0), "FLUXTIME":(77.0, 71.0), "GRIT":    (92.0, 71.0),
    "COMP":    (107.0, 71.0),
    # play row: seq knobs + pads
    "STEPS":    (12.0, 86.0), "FORM":    (27.0, 86.0), "SONG":    (42.0, 86.0),
    "ENGINE":   (57.0, 86.0), "GRITMODE":(72.0, 86.0), "STEP":    (87.0, 86.0),
    "NEWPHRASE":(102.0, 86.0), "REC":    (117.0, 86.0),
    # deliberate dual assignment: BEND rides ATTACK's knob (spec §1)
    "STAGES":   (12.0, 56.0),
}
# --- shared centre strip, 15 mm grid, three columns -------------------------
CL, CC, CR = CX - 16.0, CX, CX + 16.0
CENTER_POS = {
    "SYNC":   (CL, 18.0), "MORPH": (CC, 18.0), "TIDE":  (CR, 18.0),
    "TEMPO":  (CL, 36.0), "COUPLE":(CC, 36.0), "SHUFFLE":(CR, 36.0),
    "SCALE":  (CL, 51.0), "DRIFT": (CC, 51.0), "CHOKE": (CR, 51.0),
    "SPOT":   (CL, 66.0), "MASTER_DRIVE": (CC, 66.0), "SETTLE": (CR, 66.0),
    "REV_SIZE":(CL, 81.0), "REV_TONE": (CC, 81.0), "REV_SMEAR": (CR, 81.0),
    "REV_DECAY":(CL, 96.0), "REV_DIFF": (CC, 96.0), "REV_MOD":  (CR, 96.0),
}
JACK_Y = 113.0
JACK_POS = {"PITCH_A": 14.0, "GATE_A": 29.0, "IN_L": 60.0, "IN_R": 75.0,
            "CLOCK": CX - 7.5, "RESET": CX + 7.5,
            "OUT_L": W - 75.0, "OUT_R": W - 60.0,
            "GATE_B": W - 29.0, "PITCH_B": W - 14.0}
LIGHT_POS = {"GATE_A_L": (7.0, 30.0), "GATE_B_L": (W - 7.0, 30.0),
             "REC_A_L": (124.5, 86.0), "REC_B_L": (W - 124.5, 86.0)}

def place(c):
    """Clone a gen_panel control onto the hardware grid."""
    n = copy.copy(c)
    n.r = HW_R[c.kind]
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
ALL_HW = HW_PARAMS + HW_INPUTS + HW_OUTPUTS + HW_LIGHTS

TEXTS = [(CX, 6.0, 3.2, 0.9, gp.INK, "middle", "FIREFLOW HW DRAFT 60HP")]

# STAGES shares ATTACK's knob (deliberate dual assignment, spec §1). A
# screen widget can gate one caption away by engine state; an aluminium
# plate can't -- it prints both words. A row below ATK's own caption lands
# inside the next row's DECAY knob (15 mm grid leaves no room for a second
# line under the knob) -- verified by test_labels_stay_off_neighbour_footprints
# on that placement, which failed with a 4.4 mm gap against DECAY's 5.5 mm
# clearance radius. Above the knob is clear (nothing else occupies the
# second big-knob row over x=12), so BEND's label sits there instead: the
# plate reads BEND above the shared knob, ATK below it. Slightly smaller so
# the pair reads as a stacked pair, not a collision.
STAGES_LBL_Y_OFFSET = -7.0
STAGES_LBL_SIZE = 1.8

def hw_label(c):
    if c.enum.startswith("STAGES_"):
        return (c.x, c.y + STAGES_LBL_Y_OFFSET, "middle", STAGES_LBL_SIZE, gp.INK)
    return (c.x, c.y + LBL_DY_HW[c.kind], "middle", 2.2, gp.INK)

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
    # one glyph per control, by kind
    for c in ALL_HW:
        if c.kind in (gp.IN, gp.OUT):
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(c.r)}" '
                      f'fill="{gp.GRAPHITE}" stroke="{gp.LINE}" stroke-width="0.35"/>')
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
    L2.append("static constexpr int kHwHP = 60;")
    L2.extend(emit_table("kParamCtls", HW_PARAMS))
    L2.extend(emit_table("kInputCtls", HW_INPUTS))
    L2.extend(emit_table("kOutputCtls", HW_OUTPUTS))
    L2.extend(emit_table("kLightCtls", HW_LIGHTS))
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
    with open(os.path.join(here, "FireflowHW.svg"), "w") as f:
        f.write(svg())
    with open(os.path.join(root, "src", "generated_hw_panel.hpp"), "w") as f:
        f.write(header())
    print("wrote res/FireflowHW.svg and src/generated_hw_panel.hpp")
    print(f"params={len(HW_PARAMS)} inputs={len(HW_INPUTS)} "
          f"outputs={len(HW_OUTPUTS)} lights={len(HW_LIGHTS)}  panel={HP}HP")
