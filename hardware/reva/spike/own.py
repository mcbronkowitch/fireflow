#!/usr/bin/env python3
"""The strip board -> gen.route -> the strip board (P4a spec §3.2).

Every pad becomes an obstacle owned by its net (netless pads -- pot tabs,
unused jack and key contacts -- block every net), every locked track a
segment obstacle, every keepout a netless rectangle. Every net with two or
more pads is routed except the locked nets, and on four layers the planes'
nets. Pads become terminals at their centre, on the copper layers they are on.
"""
import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import pcbnew          # noqa: E402
import stripe as ST    # noqa: E402
from gen import kipcb  # noqa: E402
from gen import route as GR  # noqa: E402  (not `route`: this module defines route())

PITCH = 0.2
VIA_RADIUS = 0.3        # kipcb.add_via: 0.6 mm
VIA_COST = 8.0
MAX_ITERS = 30
LAYER_NAMES = ("F.Cu", "B.Cu")   # signals on the outer layers; In1/In2 are planes on 4 layers


def _mm(v):
    return pcbnew.ToMM(v)


def route_strip(s):
    board = s.board
    edge = _mm(board.GetDesignSettings().m_CopperEdgeClearance)
    inset = edge + ST.W_SUPPLY / 2.0
    r = GR.Router((ST.X0 + inset, ST.Y0 + inset, ST.X1 - inset, ST.Y1 - inset),
                  PITCH, len(LAYER_NAMES), ST.CLEARANCE, VIA_RADIUS, VIA_COST)
    skip = set(ST.LOCKED_NETS) | (set(ST.SUPPLY_NETS) if s.layers == 4 else set())
    terminals = {}
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            layers = tuple(i for i, n in enumerate(LAYER_NAMES)
                           if pad.IsOnLayer(kipcb.LAYER[n]))
            bb = pad.GetBoundingBox()
            net = pad.GetNetname() or None
            r.add_obstacle(net, layers, ("rect", _mm(bb.GetLeft()), _mm(bb.GetTop()),
                                         _mm(bb.GetRight()), _mm(bb.GetBottom())))
            if net and net not in skip:
                terminals.setdefault(net, []).append(
                    (_mm(pad.GetPosition().x), _mm(pad.GetPosition().y), layers))
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            r.add_obstacle(t.GetNetname(), range(len(LAYER_NAMES)),
                           ("circle", _mm(t.GetPosition().x), _mm(t.GetPosition().y),
                            _mm(t.GetWidth()) / 2.0))
        elif t.Type() == pcbnew.PCB_TRACE_T and t.GetLayerName() in LAYER_NAMES:
            r.add_obstacle(t.GetNetname(), (LAYER_NAMES.index(t.GetLayerName()),),
                           ("seg", _mm(t.GetStart().x), _mm(t.GetStart().y),
                            _mm(t.GetEnd().x), _mm(t.GetEnd().y), _mm(t.GetWidth()) / 2.0))
    for _label, (l, t_, rr, b) in s.keepouts:
        r.add_obstacle(None, range(len(LAYER_NAMES)), ("rect", l, t_, rr, b))
    # Footprint-owned rule areas (e.g. the PJ398SM's own F.Cu keepout): netless
    # obstacles on each copper layer they cover. Without them a track ran
    # through J13's (Task 6: items_not_allowed 1).
    for fp in board.GetFootprints():
        for z in fp.Zones():
            if not (z.GetIsRuleArea() and z.GetDoNotAllowTracks()):
                continue
            layers = tuple(i for i, n in enumerate(LAYER_NAMES)
                           if z.IsOnLayer(kipcb.LAYER[n]))
            bb = z.GetBoundingBox()
            r.add_obstacle(None, layers, ("rect", _mm(bb.GetLeft()), _mm(bb.GetTop()),
                                          _mm(bb.GetRight()), _mm(bb.GetBottom())))
    widths = {}
    for net in sorted(terminals):
        if len(terminals[net]) < 2:
            continue
        w = ST.W_SUPPLY if net in ST.SUPPLY_NETS else ST.W_SIGNAL
        widths[net] = w
        r.add_net(net, w / 2.0, terminals[net])
    return r, widths


def route(s):
    t0 = time.time()
    r, widths = route_strip(s)
    res = r.run(max_iters=MAX_ITERS)
    n_vias, length = 0, 0.0
    for net, geo in sorted(res.routes.items()):
        for layer, a, b in geo["segments"]:
            kipcb.add_track(s.board, LAYER_NAMES[layer], widths[net], net, [a, b])
            length += math.hypot(b[0] - a[0], b[1] - a[1])
        for xy in geo["vias"]:
            kipcb.add_via(s.board, xy, net)
            n_vias += 1
    return {"nets": len(widths), "failed": res.failed, "conflicts": res.conflicts,
            "iterations": res.iterations, "vias": n_vias,
            "length_mm": round(length, 1), "seconds": round(time.time() - t0, 1)}
