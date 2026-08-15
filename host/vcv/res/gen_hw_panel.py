#!/usr/bin/env python3
"""Hardware-mode panel: the 60 HP envelope draft (envelope spec 2026-08-08 §4).

Emits res/FireflowHW.svg + src/generated_hw_panel.hpp (namespace spkyhw).
Geometry is the 2026-08-10 redistribution as drawn in
docs/hardware/2026-08-10-hw-panel-redistribution.svg (graphics round 15 Aug).

Shares all parameter identity with gen_panel.py (import); defines only
geometry. Run from host/vcv/:  python3 res/gen_hw_panel.py
"""
import math
import os, copy
import gen_panel as gp

HP = 60
W  = HP * gp.MM_PER_HP            # 304.8 mm
Hh = 128.5
CX = W / 2.0
KEEP_TOP, KEEP_BOT = 9.0, 119.5   # rails + M3 screws own the rest (bodies, not ink)
ZONE_A = 124.20                   # disc colour split; the wash edge is not this line
ZONE_A_OPACITY, ZONE_B_OPACITY = 0.16, 0.18
SLAB_X = 82.0                     # floor of the wash polygon; the inner edge is smoothed
WASH_PAD = 12.0                   # raw lobe radius beyond the glyph; clamped at the centre
WASH_DY = 1.0                     # sample spacing along the inner edge, mm
WASH_SMOOTH_MM = 8.0              # triangular half-window; kills per-knob cusps
HW_WELL = "#e6ddd0"               # centre well interior
HW_KNOB = "#2b2b28"               # unused on the plate; Rack widgets are the caps
JACK_METAL = "#8a8a86"
WELL_PAD, WELL_RIM = 3.0, 0.55


def _blend_hex(fg, bg, t):
    """Opaque mix of fg over bg at opacity t, matching a fill-opacity wash."""
    def rgb(h):
        h = h.lstrip("#")
        return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
    a, b = rgb(fg), rgb(bg)
    m = tuple(round(a[i] * t + b[i] * (1.0 - t)) for i in range(3))
    return f"#{m[0]:02x}{m[1]:02x}{m[2]:02x}"


KNOB_DISC_A = _blend_hex(gp.GREEN, gp.PAPER, ZONE_A_OPACITY)
KNOB_DISC_B = _blend_hex(gp.COPPER, gp.PAPER, ZONE_B_OPACITY)


def knob_disc_fill(x):
    """Panel-wash colour at x, so a disc reads as a hole through the well."""
    if x < ZONE_A:
        return KNOB_DISC_A
    if x > W - ZONE_A:
        return KNOB_DISC_B
    return gp.PAPER

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
        return (c.x, c.y, "middle", 2.2, gp.INK)
    dy = CLASS_LBL_DY[hw_class(c.enum)]
    if c.y >= JACK_Y - 0.5:
        dy = 8.0
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
#  Organic wells: outer union of padded glyph circles
# =============================================================================
_EPS, _INSIDE = 1e-7, 1e-4


def _intersections(c0, c1):
    x0, y0, r0 = c0
    x1, y1, r1 = c1
    dx, dy = x1 - x0, y1 - y0
    d = math.hypot(dx, dy)
    if d < _EPS or d > r0 + r1 - _EPS or d < abs(r0 - r1) + _EPS:
        return []
    a = (r0 * r0 - r1 * r1 + d * d) / (2.0 * d)
    h2 = max(0.0, r0 * r0 - a * a)
    h = math.sqrt(h2)
    ux, uy = dx / d, dy / d
    px, py = x0 + a * ux, y0 + a * uy
    pts = [(px - uy * h, py + ux * h)]
    if h > 1e-8:
        pts.append((px + uy * h, py - ux * h))
    return pts


def _contained(i, circles):
    x, y, r = circles[i]
    for j, (xj, yj, rj) in enumerate(circles):
        if j != i and math.hypot(x - xj, y - yj) + r <= rj + _INSIDE:
            return True
    return False


def _in_other(px, py, circles, skip):
    for j, (x, y, r) in enumerate(circles):
        if j not in skip and math.hypot(px - x, py - y) < r - _INSIDE:
            return True
    return False


def _pt_on(c, a):
    return (c[0] + c[2] * math.cos(a), c[1] + c[2] * math.sin(a))


def _uniq_angles(angles):
    angles = sorted(a % (2 * math.pi) for a in angles)
    out = []
    for a in angles:
        if not out or min((a - out[-1]) % (2 * math.pi),
                          (out[-1] - a) % (2 * math.pi)) > 1e-6:
            out.append(a)
    if len(out) >= 2 and (out[0] + 2 * math.pi - out[-1]) % (2 * math.pi) < 1e-6:
        out.pop()
    return out


def _boundary_arcs(circles):
    arcs = []
    n = len(circles)
    for i, c in enumerate(circles):
        if _contained(i, circles):
            continue
        angs = []
        for j in range(n):
            if j == i:
                continue
            for p in _intersections(c, circles[j]):
                angs.append(math.atan2(p[1] - c[1], p[0] - c[0]))
        angs = _uniq_angles(angs)
        if not angs:
            for a0 in (0.0, math.pi):
                arcs.append({"s": _pt_on(c, a0), "e": _pt_on(c, a0 + math.pi),
                             "r": c[2], "cx": c[0], "cy": c[1],
                             "a0": a0, "span": math.pi})
            continue
        for k, a0 in enumerate(angs):
            a1 = angs[(k + 1) % len(angs)]
            span = (a1 - a0) % (2 * math.pi)
            if span < 1e-8:
                continue
            mx, my = _pt_on(c, a0 + span / 2.0)
            if _in_other(mx, my, circles, {i}):
                continue
            arcs.append({"s": _pt_on(c, a0), "e": _pt_on(c, a0 + span),
                         "r": c[2], "cx": c[0], "cy": c[1],
                         "a0": a0, "span": span})
    return arcs


def _key(p):
    return (round(p[0], 3), round(p[1], 3))


def _stitch(arcs):
    unused = set(range(len(arcs)))
    loops = []
    while unused:
        i = min(unused)
        unused.remove(i)
        loop = [arcs[i]]
        start, cur = _key(arcs[i]["s"]), _key(arcs[i]["e"])
        for _ in range(len(arcs) + 2):
            if cur == start:
                break
            found = None
            for j in unused:
                if _key(arcs[j]["s"]) == cur:
                    found = j
                    break
            if found is None:
                cx, cy = loop[-1]["e"]
                best, bestd = None, 1e9
                for j in unused:
                    d = (arcs[j]["s"][0] - cx) ** 2 + (arcs[j]["s"][1] - cy) ** 2
                    if d < bestd:
                        bestd, best = d, j
                if best is not None and bestd <= 0.08 ** 2:
                    found = best
            if found is None:
                break
            unused.remove(found)
            loop.append(arcs[found])
            cur = _key(arcs[found]["e"])
        loops.append(loop)
    return loops


def _loop_poly(loop, nseg=8):
    pts = []
    for a in loop:
        for t in range(nseg):
            ang = a["a0"] + a["span"] * (t / nseg)
            pts.append((a["cx"] + a["r"] * math.cos(ang),
                        a["cy"] + a["r"] * math.sin(ang)))
    return pts


def _shoelace(pts):
    acc = 0.0
    n = len(pts)
    for i in range(n):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % n]
        acc += x1 * y2 - x2 * y1
    return 0.5 * acc


def _centroid(pts):
    return (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))


def _pip(px, py, poly):
    inside = False
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        if (y1 > py) != (y2 > py):
            xin = (x2 - x1) * (py - y1) / (y2 - y1) + x1
            if px < xin:
                inside = not inside
    return inside


def _outer_loops(arcs):
    packed = []
    for loop in _stitch(arcs):
        poly = _loop_poly(loop)
        if len(poly) >= 3:
            packed.append((abs(_shoelace(poly)), _centroid(poly), poly, loop))
    keep = []
    for i, (_area, c, _poly, loop) in enumerate(packed):
        hole = False
        for j, (area_j, _cj, poly_j, _loop_j) in enumerate(packed):
            if i != j and abs(_shoelace(packed[i][2])) < area_j and _pip(c[0], c[1], poly_j):
                hole = True
                break
        if not hole:
            keep.append(loop)
    return keep


def _path_d(loop):
    s = loop[0]["s"]
    parts = [f"M{s[0]:.3f},{s[1]:.3f}"]
    for a in loop:
        large = 1 if a["span"] > math.pi + 1e-9 else 0
        e = a["e"]
        parts.append(f"A{a['r']:.3f},{a['r']:.3f} 0 {large} 1 {e[0]:.3f},{e[1]:.3f}")
    parts.append("Z")
    return "".join(parts)


def _union_d(circles):
    loops = _outer_loops(_boundary_arcs(circles))
    if not loops:
        raise ValueError(f"no outer loop for {circles[:2]}...")
    return "".join(_path_d(loop) for loop in loops)


def _well_path(glyphs, extra, fill):
    circles = [(x, y, r + extra) for x, y, r in glyphs]
    return f'<path fill="{fill}" d="{_union_d(circles)}"/>'


def _glyphs(*enums):
    by = {c.enum: c for c in ALL_HW}
    return [(by[e].x, by[e].y, by[e].r) for e in enums]


DECK_WELLS = [
    ("STEPS_A", "SONG_A", "RATE_A", "MELODY_A"),
    ("FLOW_A_L", "REC_A", "REC_A_L", "CAP_A_L", "GATE_A_L"),
    ("SHAPE_A", "SMOOTH_A", "RANGE_A", "MOD_A", "DENSITY_A"),
    ("ATTACK_A", "DECAY_A", "RES_A", "SUB_A", "ENGINE_A", "FILT_A", "SOURCE_A"),
    ("TUNE_A", "DETUNE_A", "COLOR_A"),
    ("FLUX_A", "FLUXRATE_A", "FLUXFB_A", "LINK_A"),
    ("COMP_A", "GRIT_A"),
]
CENTER_WELLS = [
    ("SCALE", "DRIFT", "CHOKE"),
    ("TEMPO_L", "TEMPO", "COUPLE", "SHUFFLE", "SYNC_L", "TIDE", "MORPH", "PACE"),
    ("REV_SIZE", "REV_DECAY", "REV_DIFF", "REV_MIX_A", "REV_TONE", "REV_MIX_B"),
]

# Jack-row controls on the deck side of the centre gutter. Their wash lobes
# pull the zone colour along the bottom instead of leaving a hard vertical
# next to MOD4.
WASH_EDGE = (
    "SHIFTBTN", "IN_L", "IN_R", "PITCH_A", "GATE_A",
    "MOD1_A", "MOD2_A", "MOD3_A", "MOD4_A",
)


def _well_svg():
    out = []
    for enums in DECK_WELLS:
        gs = _glyphs(*enums)
        mir = [(W - x, y, r) for x, y, r in gs]
        out.append(_well_path(gs, WELL_PAD + WELL_RIM, gp.GREEN))
        out.append(_well_path(mir, WELL_PAD + WELL_RIM, gp.COPPER))
    for enums in CENTER_WELLS:
        gs = _glyphs(*enums)
        out.append(_well_path(gs, WELL_PAD + WELL_RIM, gp.INK))
    for enums in DECK_WELLS:
        gs = _glyphs(*enums)
        mir = [(W - x, y, r) for x, y, r in gs]
        out.append(_well_path(gs, WELL_PAD, gp.PAPER))
        out.append(_well_path(mir, WELL_PAD, gp.PAPER))
    for enums in CENTER_WELLS:
        gs = _glyphs(*enums)
        out.append(_well_path(gs, WELL_PAD, HW_WELL))
    return out


def centre_well_left():
    """Left edge of the centre-well union (rim included). Wash must stay west."""
    by = {c.enum: c for c in ALL_HW}
    return min(by[e].x - by[e].r - WELL_PAD - WELL_RIM
               for enums in CENTER_WELLS for e in enums)


def wash_radius(x, r):
    """Padded lobe radius, clamped so the circle does not eat a centre well."""
    return max(0.0, min(r + WASH_PAD, centre_well_left() - 1.0 - x))


def _wash_circles():
    glyphs = []
    for enums in DECK_WELLS:
        glyphs.extend(_glyphs(*enums))
    glyphs.extend(_glyphs(*WASH_EDGE))
    out = []
    for x, y, r in glyphs:
        wr = wash_radius(x, r)
        if wr > 0.05:
            out.append((x, y, wr))
    return out


def wash_edge_xs():
    """Smoothed inner edge of the left wash: (y, x) samples, y from 0 to Hh.

    Raw envelope is the rightmost point of the deck lobes at each y; a
    triangular moving average then takes the per-knob cusps off so the
    seam reads as one curve, not a chain of circles. Floor SLAB_X, ceiling
    just west of the centre wells."""
    circles = _wash_circles()
    ceiling = centre_well_left() - 1.0
    ys = []
    y = 0.0
    while y <= Hh + 1e-9:
        ys.append(y)
        y += WASH_DY
    raw = []
    for yy in ys:
        x = SLAB_X
        for cx, cy, r in circles:
            dy = yy - cy
            if abs(dy) <= r:
                x = max(x, cx + math.sqrt(max(0.0, r * r - dy * dy)))
        raw.append(min(x, ceiling))
    half = max(1, int(round(WASH_SMOOTH_MM / WASH_DY)))
    n = len(raw)
    sm = []
    for i in range(n):
        lo, hi = max(0, i - half), min(n, i + half + 1)
        acc = wsum = 0.0
        for j in range(lo, hi):
            w = float(half + 1 - abs(j - i))
            acc += w * raw[j]
            wsum += w
        sm.append(min(max(acc / wsum, SLAB_X), ceiling))
    return list(zip(ys, sm))


def _zone_wash_svg():
    """Full-bleed A/B wash as two polygons whose inner edges are the smoothed
    deck silhouette. NanoSVG draws polygons; it drops large arc-unions."""
    edge = wash_edge_xs()

    def poly(fill, mirror):
        if not mirror:
            pts = ["0.000,0.000"]
            pts.extend(f"{mm(x)},{mm(y)}" for y, x in edge)
            pts.append(f"0.000,{mm(Hh)}")
        else:
            pts = [f"{mm(W)},0.000"]
            pts.extend(f"{mm(W - x)},{mm(y)}" for y, x in edge)
            pts.append(f"{mm(W)},{mm(Hh)}")
        return f'<polygon fill="{fill}" points="{" ".join(pts)}"/>'

    return [poly(KNOB_DISC_A, False), poly(KNOB_DISC_B, True)]


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
    P.extend(_zone_wash_svg())
    P.extend(_well_svg())
    P.append(f'<rect x="{mm(SD_X - SD_W / 2)}" y="{mm(SD_Y - SD_H / 2)}" '
              f'width="{mm(SD_W)}" height="{mm(SD_H)}" rx="1" fill="none" '
              f'stroke="{gp.INK}" stroke-width="0.5" stroke-dasharray="1.5,1"/>')
    for c in ALL_HW:
        if c.kind in (gp.IN, gp.OUT):
            fill = JACK_METAL
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(c.r)}" '
                      f'fill="{fill}" stroke="{gp.LINE}" stroke-width="0.35"/>')
        elif c.kind == gp.LIGHT:
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(c.r)}" '
                      f'fill="#1a1206" stroke="#3a2c12" stroke-width="0.25"/>')
        elif hw_class(c.enum) == "P":
            P.append(f'<rect x="{mm(c.x-c.r)}" y="{mm(c.y-c.r)}" width="{mm(2*c.r)}" '
                      f'height="{mm(2*c.r)}" rx="1.0" fill="{gp.WHITE}" '
                      f'stroke="{gp.LINE}" stroke-width="0.3"/>')
        else:
            # Zone-coloured disc: a hole through the well, not a black cap.
            # Rack's widget sits on top; the well pad shows as the inner ring.
            P.append(f'<circle cx="{mm(c.x)}" cy="{mm(c.y)}" r="{mm(c.r)}" '
                      f'fill="{knob_disc_fill(c.x)}"/>')
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
