#!/usr/bin/env python3
"""P4-2: the placed Rev A board -> gen.route -> the routed board (spec §4.1).

    KIPY hardware/reva/route.py [--write] [--sabotage NAME] [--out DIR]

Placement is P4-1's `place.build()`, in memory. SMD pads on the plane nets
are stitched first (their vias are obstacles to the router; spec §4.2.9
keeps them off the pot bodies). Every pad is an
obstacle owned by its net; netless pads (pot tabs, unused contacts) block
every net; footprint rule areas block their layers; each pot's body box
blocks F.Cu (spec §2.7). Every net with two or more pads is routed except
the plane nets. Every terminal carries its pad's copper shape, so a pad that
lies partly off the grid (the jack row's tip pads, spec §4.2.6) is still
reached. Victims and aggressors carry pair groups, U_SM's pad groups and the
jack zones (a victim pad with aggressor pads already within 10 mm) are
exemption zones, SENSE and audio route in earlier tiers. U_REG's heat
copper (reg_copper(), computed from the placed regulator) is a 3V3D-owned
B.Cu obstacle and, after routing, a 3V3D zone. The silkscreen pass (silk.py, P4-3 spec §4.3) runs last, after
the zone fill. The rules are judged by route_check.py on the saved board,
never here."""
import argparse
import copy
import math
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
for _p in (HW, HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import pcbnew               # noqa: E402
import place as P           # noqa: E402
import rules as RU          # noqa: E402
import silk                 # noqa: E402
from gen import kipcb       # noqa: E402
from gen import place as PL  # noqa: E402
from gen import route as GR  # noqa: E402  (not `route`: this module is route.py)
from gen import stitch      # noqa: E402

OUT = P.OUT
COMMITTED = os.path.join(HERE, "kicad", "reva.kicad_pcb")   # route.py owns it (spec §4.1)
DOCS = os.path.normpath(os.path.join(HW, "..", "docs", "hardware", "routing"))
LAYER_NAMES = ("F.Cu", "B.Cu")
AGGR = "aggressor"

# U_REG's heat copper (Bastian, 2026-10-01; docs/hardware/power-budget.md):
# a 3V3D area on B.Cu at the regulator's tab. The router keeps every other
# net's copper (and every via but 3V3D's) off it; after routing it becomes a
# 3V3D zone, solid to the tab.
# Until 2026-10-08 the area was two fixed rectangles, (51.9, 39.0, 66.1,
# 50.8) and (60.2, 50.8, 66.1, 61.0), drawn for U_REG at (61.62, 54.98). The
# 9 mm panel pass moved J_PWR and with it U_REG, and the rectangles were left
# on foreign pads 46 mm from the tab. Since 2026-10-08 (Bastian, Task 7c) the
# area follows the placed regulator: reg_copper() computes it from the board
# with the same keep-outs the fixed area kept by hand, plus courtyards.
REG_NET = "3V3D"
REG_REF = "U_REG"
REG_WIN_MM = 15.0       # the area stays inside the box of this half-size around the tab centre
REG_GRID_MM = 0.2       # the search grid (the router's pitch); a cell overlapping a keep-out is out


def reg_keepouts(board):
    """(tab box, pin column box, keep-out boxes, inner box) for U_REG's heat
    copper, read from the placed board, mm:
    - every pad on B.Cu that is not U_REG's own 3V3D, netless pads included,
      grown by RU.CLEARANCE (the fill's own clearance);
    - every back-side courtyard but U_REG's, and the module shadow;
    - every via and B.Cu track already on the board (the plane stitching,
      which runs first), grown by RU.CLEARANCE;
    - U_REG's pin column: the box of its pins (every pad but the tab) grown
      by RU.CLEARANCE and run out to the board edge on the side away from
      the tab, so +12V and GND still reach pins 3 and 1 from outside (the
      2026-10-01 area kept x <= 59.47 free for the same reason);
    - the outline inset by RU.EDGE_CLEAR (inner box).
    The tab is U_REG's largest REG_NET pad on B.Cu."""
    reg = board.FindFootprintByReference(REG_REF)
    own = [p for p in reg.Pads() if p.GetNetname() == REG_NET and p.IsOnLayer(pcbnew.B_Cu)]
    if not own:
        raise ValueError("%s has no %s pad on B.Cu" % (REG_REF, REG_NET))
    tab_pad = max(own, key=lambda p: p.GetBoundingBox().GetWidth() * p.GetBoundingBox().GetHeight())
    tab = PL.box(tab_pad.GetBoundingBox())
    pins = [PL.box(p.GetBoundingBox()) for p in reg.Pads() if PL.box(p.GetBoundingBox()) != tab]
    g = PL.grow((min(q[0] for q in pins), min(q[1] for q in pins),
                 max(q[2] for q in pins), max(q[3] for q in pins)), RU.CLEARANCE)
    dx = (g[0] + g[2] - tab[0] - tab[2]) / 2.0
    dy = (g[1] + g[3] - tab[1] - tab[3]) / 2.0
    big = 1000.0
    if abs(dx) > abs(dy):
        column = (g[0] - big, g[1], g[2], g[3]) if dx < 0 else (g[0], g[1], g[2] + big, g[3])
    else:
        column = (g[0], g[1] - big, g[2], g[3]) if dy < 0 else (g[0], g[1], g[2], g[3] + big)
    keep = [column]
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        for p in fp.Pads():
            if ref == REG_REF and p.GetNetname() == REG_NET:
                continue
            if p.IsOnLayer(pcbnew.B_Cu):
                keep.append(PL.grow(PL.box(p.GetBoundingBox()), RU.CLEARANCE))
        if fp.IsFlipped() and ref != REG_REF:
            c = PL.courtyard_box(fp)
            if c is not None:
                keep.append(c)
            if ref == "U_SM":
                keep.append(P.module_shadow(fp))
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T or t.IsOnLayer(pcbnew.B_Cu):
            keep.append(PL.grow(PL.box(t.GetBoundingBox()), RU.CLEARANCE))
    inner = (P.X0 + RU.EDGE_CLEAR, P.Y0 + RU.EDGE_CLEAR, P.X1 - RU.EDGE_CLEAR, P.Y1 - RU.EDGE_CLEAR)
    return tab, column, keep, inner


def reg_copper_rects(tab, keep, inner, win=REG_WIN_MM, grid=REG_GRID_MM):
    """The heat copper as one or two rectangles, pure geometry (mm boxes):
    A is the largest rectangle that holds the tab and touches no keep-out;
    B is the rectangle, free as well, whose union with A is largest among
    those that overlap A by at least the tab's short side in both
    directions (a joint no narrower than the tab). Both lie inside `inner`
    and inside the box of half-size `win` around the tab centre, on a grid
    of `grid` aligned to the tab's corner; a cell that overlaps a keep-out
    is out (the keep-outs carry their clearance already). Two overlapping rectangles are one simply connected area.
    Returns [A] or [A, B]; raises ValueError when a tab cell is not free."""
    cx, cy = (tab[0] + tab[2]) / 2.0, (tab[1] + tab[3]) / 2.0
    lo_x, hi_x = max(inner[0], cx - win), min(inner[2], cx + win)
    lo_y, hi_y = max(inner[1], cy - win), min(inner[3], cy + win)
    eps = 1e-6
    i0 = -int(math.floor((tab[0] - lo_x) / grid + eps))
    j0 = -int(math.floor((tab[1] - lo_y) / grid + eps))
    nx = int(math.floor((hi_x - tab[0]) / grid + eps)) - i0
    ny = int(math.floor((hi_y - tab[1]) / grid + eps)) - j0

    def cell(i, j):
        x, y = tab[0] + (i + i0) * grid, tab[1] + (j + j0) * grid
        return (x, y, x + grid, y + grid)
    free = [[True] * nx for _ in range(ny)]
    for q in keep:
        a = max(0, int(math.floor((q[0] - tab[0]) / grid)) - i0)
        bb = min(nx - 1, int(math.ceil((q[2] - tab[0]) / grid)) - i0)
        c = max(0, int(math.floor((q[1] - tab[1]) / grid)) - j0)
        d = min(ny - 1, int(math.ceil((q[3] - tab[1]) / grid)) - j0)
        for j in range(c, d + 1):
            row = free[j]
            for i in range(a, bb + 1):
                if row[i] and PL.overlaps(cell(i, j), q):
                    row[i] = False
    ti0, tj0 = -i0, -j0
    ti1 = ti0 + int(math.ceil((tab[2] - tab[0]) / grid - eps)) - 1
    tj1 = tj0 + int(math.ceil((tab[3] - tab[1]) / grid - eps)) - 1
    if not (0 <= ti0 <= ti1 < nx and 0 <= tj0 <= tj1 < ny):
        raise ValueError("%s's tab is not inside the heat copper window" % REG_REF)
    pre = []
    for i in range(nx):
        s, col = 0, [0]
        for j in range(ny):
            s += 0 if free[j][i] else 1
            col.append(s)
        pre.append(col)

    def clear(i, t, b):
        return pre[i][b + 1] - pre[i][t] == 0
    if not all(clear(i, tj0, tj1) for i in range(ti0, ti1 + 1)):
        raise ValueError("%s's tab touches a heat copper keep-out" % REG_REF)
    best_a = None                      # (cells, (i0, j0, i1, j1))
    t = tj0
    while t >= 0 and all(clear(i, t, tj1) for i in range(ti0, ti1 + 1)):
        b = tj1
        while b < ny and all(clear(i, t, b) for i in range(ti0, ti1 + 1)):
            l, r = ti0, ti1
            while l > 0 and clear(l - 1, t, b):
                l -= 1
            while r < nx - 1 and clear(r + 1, t, b):
                r += 1
            n = (r - l + 1) * (b - t + 1)
            if best_a is None or n > best_a[0]:
                best_a = (n, (l, t, r, b))
            b += 1
        t -= 1
    al, at, ar, ab = best_a[1]
    join = int(math.ceil(min(tab[2] - tab[0], tab[3] - tab[1]) / grid - eps))
    best_b = None
    h = [0] * nx
    for b in range(ny):
        for i in range(nx):
            h[i] = h[i] + 1 if free[b][i] else 0
        for i in range(nx):
            if h[i] == 0:
                continue
            l, r = i, i
            while l > 0 and h[l - 1] >= h[i]:
                l -= 1
            while r < nx - 1 and h[r + 1] >= h[i]:
                r += 1
            t = b - h[i] + 1
            ow = min(r, ar) - max(l, al) + 1
            oh = min(b, ab) - max(t, at) + 1
            if ow < join or oh < join:
                continue
            union = (r - l + 1) * (b - t + 1) + best_a[0] - ow * oh
            if union > best_a[0] and (best_b is None or union > best_b[0]):
                best_b = (union, (l, t, r, b))

    def mm(rc):
        l, t, r, b = rc
        return (round(tab[0] + (l + i0) * grid, 4), round(tab[1] + (t + j0) * grid, 4),
                round(tab[0] + (r + 1 + i0) * grid, 4), round(tab[1] + (b + 1 + j0) * grid, 4))
    return [mm(best_a[1])] + ([mm(best_b[1])] if best_b else [])


def reg_copper(board):
    """U_REG's heat copper on the placed board: reg_copper_rects over
    reg_keepouts."""
    tab, _column, keep, inner = reg_keepouts(board)
    return reg_copper_rects(tab, keep, inner)


def reg_outline(rects):
    """The heat copper's union as one outline, [(x, y)] mm."""
    u = pcbnew.SHAPE_POLY_SET()
    for l, t, r, b in rects:
        one = pcbnew.SHAPE_POLY_SET()
        one.NewOutline()
        for x, y in ((l, t), (r, t), (r, b), (l, b)):
            one.Append(pcbnew.FromMM(x), pcbnew.FromMM(y))
        u.BooleanAdd(one)
    u.Simplify()
    assert u.OutlineCount() == 1 and u.HoleCount(0) == 0, "the heat copper must be one simply connected area"
    ch = u.COutline(0)
    return [(_mm(ch.CPoint(i).x), _mm(ch.CPoint(i).y)) for i in range(ch.PointCount())]


def _mm(v):
    return pcbnew.ToMM(v)


class Routed:
    def __init__(self):
        self.board = None
        self.ids = {}
        self.front = []
        self.known = {}
        self.result = None
        self.widths = {}
        self.zones = []
        self.jack_zones = []
        self.stitched = {}
        self.unresolved = []
        self.seconds = 0.0
        self.skip_render = False
        self.silk = None
        self.reg_copper = []

    def reload(self, path):
        new = Routed()
        for k, v in self.__dict__.items():
            if k not in ("board", "result"):
                setattr(new, k, copy.deepcopy(v))
        new.result = self.result          # read-only after the run
        new.board = kipcb.load(path)
        return new


def module_zones(board, margin):
    """One rect per group of U_SM pads whose centres lie within one pin pitch
    (2.54 mm) of a neighbour: the box of the group's pad centres grown by
    `margin` (spec §4.2.2; four groups on the P4-1 board)."""
    sm = board.FindFootprintByReference("U_SM")
    pts = sorted({(round(_mm(p.GetPosition().x), 4), round(_mm(p.GetPosition().y), 4))
                  for p in sm.Pads()})
    parent = list(range(len(pts)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for i, a in enumerate(pts):
        for j in range(i + 1, len(pts)):
            b = pts[j]
            if (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 <= (2.54 + 1e-3) ** 2:
                parent[find(i)] = find(j)
    groups = {}
    for i, p in enumerate(pts):
        groups.setdefault(find(i), []).append(p)
    return sorted((min(x for x, _ in g) - margin, min(y for _, y in g) - margin,
                   max(x for x, _ in g) + margin, max(y for _, y in g) + margin)
                  for g in groups.values())


def _gap(a, b):
    dx = max(b[0] - a[2], a[0] - b[2], 0.0)
    dy = max(b[1] - a[3], a[1] - b[3], 0.0)
    return (dx * dx + dy * dy) ** 0.5


def jack_zones(board, audio_mm, margin):
    """Spec §4.2.2 "Jack zones": for every victim pad outside U_SM with
    aggressor pads closer than `audio_mm` edge to edge (pad bounding boxes),
    the box of that victim pad and those aggressor pads grown by `margin`.
    Returns [(victim ref, pad, (l, t, r, b))] (two on the 2026-09-30 board:
    J18.T and J1.T)."""
    victims, aggr = [], []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            b = PL.box(pad.GetBoundingBox())
            if pad.GetNetname() in RU.VICTIMS and fp.GetReference() != "U_SM":
                victims.append((fp.GetReference(), str(pad.GetNumber()), b))
            elif pad.GetNetname() in RU.AGGRESSORS:
                aggr.append(b)
    out = []
    for ref, num, vb in sorted(victims):
        near = [ab for ab in aggr if _gap(vb, ab) < audio_mm]
        if near:
            boxes = [vb] + near
            out.append((ref, num, (min(q[0] for q in boxes) - margin, min(q[1] for q in boxes) - margin,
                                   max(q[2] for q in boxes) + margin, max(q[3] for q in boxes) + margin)))
    return out


def pot_refs(placed):
    """The front refs whose panel hole is a pot (probed 2026-09-30: 70)."""
    holes = P._hole_index()
    return sorted(r for r in placed.front if holes.get(placed.ids.get(r), {}).get("kind") == "pot")


def _group(net):
    if net in RU.VICTIMS:
        return net                      # one group per victim: the L/R rule pairs them
    if net in RU.AGGRESSORS:
        return AGGR
    return None


def _pad_rect(pad):
    bb = pad.GetBoundingBox()
    return ("rect", _mm(bb.GetLeft()), _mm(bb.GetTop()), _mm(bb.GetRight()), _mm(bb.GetBottom()))


def _pad_shape(pad, layer):
    """The pad's copper as a router shape (spec §4.2.6): a circle for a round
    pad, else its bounding box. Probed 2026-09-30 (10.0.5): GetShape(layer) /
    GetSize(layer) answer the same with and without the layer on this board
    (every padstack is uniform); a jack tip is a 2.13 mm circle, J_SD's pins
    1.2 mm circles, SMD pads roundrects. The circle's centre is the box
    centre, so a pad offset cannot move it off its copper."""
    if pad.GetShape(layer) == pcbnew.PAD_SHAPE_CIRCLE:
        _k, l, t, r, b = _pad_rect(pad)
        return ("circle", (l + r) / 2.0, (t + b) / 2.0, _mm(pad.GetSize(layer).x) / 2.0)
    return _pad_rect(pad)


def _router_input(s, placed):
    b = s.board
    inset = RU.EDGE_CLEAR + RU.SUPPLY_W / 2.0
    r = GR.Router((P.X0 + inset, P.Y0 + inset, P.X1 - inset, P.Y1 - inset),
                  RU.PITCH, len(LAYER_NAMES), RU.CLEARANCE, RU.VIA_D / 2.0, RU.VIA_COST)
    for v in RU.VICTIMS:
        r.pair_clearance(v, AGGR, RU.AUDIO_MM)
    for a, bb in RU.LR_PAIRS:
        r.pair_clearance(a, bb, RU.LR_MM)
    for z in s.zones:
        r.pair_exempt(z)
    for _ref, _num, z in s.jack_zones:
        r.pair_exempt(z)
    terms = {}
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            ls = tuple(i for i, n in enumerate(LAYER_NAMES) if pad.IsOnLayer(kipcb.LAYER[n]))
            net = pad.GetNetname() or None
            r.add_obstacle(net, ls, _pad_rect(pad), group=_group(net))
            if net and net not in RU.PLANE_NETS:
                shape = _pad_shape(pad, kipcb.LAYER[LAYER_NAMES[ls[0]]])
                terms.setdefault(net, []).append(
                    (_mm(pad.GetPosition().x), _mm(pad.GetPosition().y), ls, shape))
    for t in b.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            r.add_obstacle(t.GetNetname(), range(len(LAYER_NAMES)),
                           ("circle", _mm(t.GetPosition().x), _mm(t.GetPosition().y),
                            _mm(t.GetWidth(pcbnew.F_Cu)) / 2.0), group=_group(t.GetNetname()))
        elif t.Type() == pcbnew.PCB_TRACE_T and t.GetLayerName() in LAYER_NAMES:
            r.add_obstacle(t.GetNetname(), (LAYER_NAMES.index(t.GetLayerName()),),
                           ("seg", _mm(t.GetStart().x), _mm(t.GetStart().y),
                            _mm(t.GetEnd().x), _mm(t.GetEnd().y), _mm(t.GetWidth()) / 2.0),
                           group=_group(t.GetNetname()))
    for fp in b.GetFootprints():
        for z in fp.Zones():
            if z.GetIsRuleArea() and z.GetDoNotAllowTracks():
                ls = tuple(i for i, n in enumerate(LAYER_NAMES) if z.IsOnLayer(kipcb.LAYER[n]))
                bb = z.GetBoundingBox()
                r.add_obstacle(None, ls, ("rect", _mm(bb.GetLeft()), _mm(bb.GetTop()),
                                          _mm(bb.GetRight()), _mm(bb.GetBottom())))
    # Spec §4.2.8: no via within PLANE_THT_VIA_KEEPOFF of a plane-net THT
    # pad, so its thermal spokes keep their landing room. The router keeps a
    # via's copper `clearance` off a via-only shape, hence the shape is the
    # pad grown by the rest.
    grow = RU.PLANE_THT_VIA_KEEPOFF - RU.CLEARANCE
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() in RU.PLANE_NETS and pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                sh = _pad_shape(pad, pcbnew.F_Cu)
                if sh[0] == "circle":
                    sh = ("circle", sh[1], sh[2], sh[3] + grow)
                else:
                    sh = ("rect", sh[1] - grow, sh[2] - grow, sh[3] + grow, sh[4] + grow)
                r.add_obstacle(None, range(len(LAYER_NAMES)), sh, via_only=True)
    # The grid's inset (edge clearance + a supply half-width) is short of a
    # via's radius: a via on the outermost cell sat 0.45 mm from the edge
    # (T1, 2026-09-30). A via-only band along the outline, EDGE_CLEAR -
    # CLEARANCE deep, keeps via copper EDGE_CLEAR off the edge without
    # moving the grid.
    band = RU.EDGE_CLEAR - RU.CLEARANCE
    for rect in ((P.X0 - 1.0, P.Y0 - 1.0, P.X1 + 1.0, P.Y0 + band),
                 (P.X0 - 1.0, P.Y1 - band, P.X1 + 1.0, P.Y1 + 1.0),
                 (P.X0 - 1.0, P.Y0 - 1.0, P.X0 + band, P.Y1 + 1.0),
                 (P.X1 - band, P.Y0 - 1.0, P.X1 + 1.0, P.Y1 + 1.0)):
        r.add_obstacle(None, range(len(LAYER_NAMES)), ("rect",) + rect, via_only=True)
    for ref in pot_refs(placed):
        l, t, rr, btm = PL.body_box(b.FindFootprintByReference(ref))
        r.add_obstacle(None, (LAYER_NAMES.index("F.Cu"),), ("rect", l, t, rr, btm))
    # U_REG's heat copper: owned by REG_NET on B.Cu, so REG_NET may enter and
    # every other net keeps its B.Cu copper and its vias off (a via needs
    # every layer free; probed 2026-10-01 on a 10 x 4 mm grid).
    for rect in s.reg_copper:
        r.add_obstacle(REG_NET, (LAYER_NAMES.index("B.Cu"),), ("rect",) + tuple(rect))
    for net in sorted(terms):
        if len(terms[net]) < 2:
            continue
        w = RU.SUPPLY_W if net in RU.SUPPLY_TRACK_NETS else RU.SIGNAL_W
        s.widths[net] = w
        r.add_net(net, w / 2.0, terms[net], group=_group(net), tier=RU.tier_of(net))
    return r


def build():
    placed = P.build()
    s = Routed()
    s.board = placed.board
    s.ids, s.front = dict(placed.ids), list(placed.front)
    kipcb.set_netclasses(s.board, RU.SIGNAL_W, {"supply": (RU.SUPPLY_W, list(RU.SUPPLY_TRACK_NETS))},
                         clearance_mm=RU.CLEARANCE, via_mm=RU.VIA_D, drill_mm=RU.VIA_DRILL)
    s.zones = module_zones(s.board, RU.EXEMPT_MARGIN_MM)
    s.jack_zones = jack_zones(s.board, RU.AUDIO_MM, RU.EXEMPT_MARGIN_MM)
    # Spec §4.2.9: stitching vias keep their copper off the pot bodies, the
    # same boxes the router's F.Cu pot obstacle uses (`_router_input`).
    pots = [PL.body_box(s.board.FindFootprintByReference(ref)) for ref in pot_refs(placed)]
    s.stitched, s.unresolved = stitch.stitch_plane_pads(s.board, set(RU.PLANE_NETS), netless_blocks=True,
                                                        tht_keepoff_mm=RU.PLANE_THT_VIA_KEEPOFF,
                                                        via_keepouts=pots)
    s.reg_copper = reg_copper(s.board)      # after the stitch: its vias and tracks are keep-outs
    r = _router_input(s, placed)
    t0 = time.time()
    s.result = r.run(max_iters=RU.MAX_ITERS, pres0=RU.PRES0, pres_mult=RU.PRES_MULT,
                     hist_inc=RU.HIST_INC)
    s.seconds = round(time.time() - t0, 1)
    for net, geo in sorted(s.result.routes.items()):
        for layer, a, bb in geo["segments"]:
            kipcb.add_track(s.board, LAYER_NAMES[layer], s.widths[net], net, [a, bb])
        for xy in geo["vias"]:
            kipcb.add_via(s.board, xy, net)
    # The heat copper as a zone: solid to the tab (no thermal spokes), the
    # board's clearance (the planes keep KiCad's default 0.5 mm).
    z = kipcb.add_zone(s.board, "B.Cu", REG_NET, reg_outline(s.reg_copper))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    z.SetLocalClearance(pcbnew.FromMM(RU.CLEARANCE))
    kipcb.fill_zones(s.board)
    # P4-3 spec §4.3: the silkscreen pass last, so the byte-identity guard covers it
    s.silk = silk.apply(s.board)
    return s


def save(s, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    kipcb.save(s.board, path)


def main(argv=None):
    import route_check as RC
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--sabotage", default="")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args(argv)
    s = build()
    res = s.result
    for ref, num, z in s.jack_zones:
        print("jack zone %s.%s (%s): %s" % (ref, num, s.ids.get(ref, ref),
                                             ", ".join("%.2f" % v for v in z)))
    print("router:nets %d failed %d %s conflicts %d iterations %d vias %d length %.1f mm seconds %.1f"
          % (len(s.widths), len(res.failed), res.failed, res.conflicts, res.iterations,
             sum(v["vias"] for v in res.stats.values()),
             sum(v["length_mm"] for v in res.stats.values()), s.seconds))
    print("silk: placed %d, hidden %d" % (len(s.silk.placed), len(s.silk.hidden)))
    if s.unresolved:
        print("stitch unresolved: %s" % "; ".join(s.unresolved))
    prefix = os.path.join(a.out, "reva-routed")
    pcb = prefix + ".kicad_pcb"
    if a.sabotage:
        save(s, pcb)
        s = s.reload(pcb)
        RC.sabotage(s, a.sabotage)
    save(s, pcb)
    print("wrote", os.path.relpath(pcb))
    green = RC.run(s, pcb, prefix)
    if a.write and not a.sabotage and not green:
        print("not copied: the run is RED")
    if a.write and not a.sabotage and green:
        shutil.copyfile(pcb, COMMITTED)
        print("copied to", os.path.relpath(COMMITTED))
        os.makedirs(DOCS, exist_ok=True)
        for side in ("top", "bottom"):
            png = "%s-%s.png" % (prefix, side)
            if os.path.exists(png):
                shutil.copyfile(png, os.path.join(DOCS, os.path.basename(png)))
        print("renders copied to", os.path.relpath(DOCS))
    print("GREEN" if green else "RED")
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
