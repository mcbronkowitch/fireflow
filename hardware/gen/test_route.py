#!/usr/bin/env python3
"""Guard for hardware/gen/route.py. Plain script; the exit code is the
verdict. Every routed result is checked by exact geometry here -- segment to
segment, segment to obstacle -- never by asking the router's own raster."""
import math
import os
import sys

sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")))
from gen import route as R  # noqa: E402

CLR = 0.2
FAILS = []


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def seg_point(p, a, b):
    (px, py), (ax, ay), (bx, by) = p, a, b
    dx, dy = bx - ax, by - ay
    if dx == 0 and dy == 0:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def seg_seg(s1, s2):
    return min(seg_point(s1[0], *s2), seg_point(s1[1], *s2),
               seg_point(s2[0], *s1), seg_point(s2[1], *s1))


def seg_rect(s, rect):
    """Distance from a segment to an axis-aligned rectangle, sampled every
    0.01 mm along the segment (exact enough for a 0.2 mm rule)."""
    (x1, y1), (x2, y2) = s
    l, t, r, b = rect
    n = max(1, int(math.hypot(x2 - x1, y2 - y1) / 0.01))
    best = float("inf")
    for i in range(n + 1):
        x, y = x1 + (x2 - x1) * i / n, y1 + (y2 - y1) * i / n
        best = min(best, math.hypot(max(l - x, 0, x - r), max(t - y, 0, y - b)))
    return best


def length(res, net):
    return sum(math.hypot(b[0] - a[0], b[1] - a[1]) for _l, a, b in res.routes[net]["segments"])


def touches(res, net, x, y):
    return any(math.hypot(a[0] - x, a[1] - y) < 1e-6 or math.hypot(b[0] - x, b[1] - y) < 1e-6
               for _l, a, b in res.routes[net]["segments"])


def test_straight():
    r = R.Router((0, 0, 10, 4), 0.2, 1, CLR, 0.3)
    r.add_net("A", 0.125, [(1.0, 2.0, (0,)), (9.0, 2.0, (0,))])
    res = r.run()
    check(not res.failed and res.conflicts == 0, "straight: routed")
    check(abs(length(res, "A") - 8.0) < 0.3, "straight: length %.2f ~ 8" % length(res, "A"))
    check(touches(res, "A", 1.0, 2.0) and touches(res, "A", 9.0, 2.0), "straight: both pads reached")


def test_detour():
    rect = (4.5, 0.0, 5.5, 3.0)
    r = R.Router((0, 0, 10, 6), 0.2, 1, CLR, 0.3)
    r.add_obstacle("X", (0,), ("rect",) + rect)
    r.add_net("A", 0.125, [(1.0, 2.0, (0,)), (9.0, 2.0, (0,))])
    res = r.run()
    d = min(seg_rect((a, b), rect) for _l, a, b in res.routes["A"]["segments"])
    check(not res.failed, "detour: routed")
    check(d >= 0.125 + CLR - 1e-6, "detour: %.3f mm from the obstacle (need %.3f)" % (d, 0.325))


def test_via():
    r = R.Router((0, 0, 10, 4), 0.2, 2, CLR, 0.3)
    r.add_obstacle(None, (0,), ("rect", 4.5, 0.0, 5.5, 4.0))
    r.add_net("A", 0.125, [(1.0, 2.0, (0,)), (9.0, 2.0, (0,))])
    res = r.run()
    check(not res.failed, "via: routed around a full-height wall")
    check(len(res.routes["A"]["vias"]) >= 2, "via: %d vias" % len(res.routes["A"]["vias"]))


def test_own_net_passable():
    r = R.Router((0, 0, 10, 4), 0.2, 1, CLR, 0.3)
    r.add_obstacle("A", (0,), ("rect", 4.0, 0.0, 6.0, 4.0))
    r.add_net("A", 0.125, [(1.0, 2.0, (0,)), (9.0, 2.0, (0,))])
    res = r.run()
    check(not res.failed and abs(length(res, "A") - 8.0) < 0.3,
          "own copper does not block its own net")


def test_crossing_two_layers():
    r = R.Router((0, 0, 10, 10), 0.2, 2, CLR, 0.3)
    r.add_net("A", 0.125, [(1.0, 5.0, (0, 1)), (9.0, 5.0, (0, 1))])
    r.add_net("B", 0.125, [(5.0, 1.0, (0, 1)), (5.0, 9.0, (0, 1))])
    res = r.run()
    check(not res.failed and res.conflicts == 0, "crossing: both nets, no conflict left")
    # The router may put A and B on different layers entirely, leaving no
    # same-layer pair, so no distance is asserted here; the clearance proof
    # lives in test_side_by_side and test_negotiation. What this test proves
    # is the two-layer resolution its name promises: something left layer 0.
    uses = [sum(1 for l, _a, _b in res.routes[n]["segments"] if l == 1) + len(res.routes[n]["vias"])
            for n in ("A", "B") if n in res.routes]
    check(any(uses), "crossing: resolved on two layers (layer-1 segments + vias per net: %s)" % uses)


def test_side_by_side():
    """One layer, two nets squeezed past the same obstacle: both must share
    the gap above it, so a same-layer pair always exists."""
    r = R.Router((0, 0, 10, 4), 0.2, 1, CLR, 0.3)
    r.add_obstacle(None, (0,), ("rect", 4.0, 0.0, 6.0, 1.8))
    r.add_net("A", 0.125, [(1.0, 1.0, (0,)), (9.0, 1.0, (0,))])
    r.add_net("B", 0.125, [(1.0, 3.0, (0,)), (9.0, 3.0, (0,))])
    res = r.run()
    pairs = [seg_seg((a1, b1), (a2, b2))
             for _l1, a1, b1 in res.routes.get("A", {"segments": []})["segments"]
             for _l2, a2, b2 in res.routes.get("B", {"segments": []})["segments"]]
    check(not res.failed and res.conflicts == 0 and pairs,
          "side by side: both routed, %d pairs to measure" % len(pairs))
    worst = min(pairs) if pairs else 0.0
    check(worst >= 0.25 + CLR - 1e-6,
          "side by side: A-B %.3f mm (need %.3f)" % (worst, 0.45))


def test_negotiation():
    """One layer, a wall with two gaps, each just wide enough for one track.
    Both nets' shortest paths want the low gap, so the first round conflicts
    and only ripping up plus history/pressure sends B through the high gap."""
    r = R.Router((0, 0, 10, 10), 0.2, 1, CLR, 0.3)
    for rect in ((4.5, 0.0, 5.5, 2.0), (4.5, 3.2, 5.5, 6.6), (4.5, 7.8, 5.5, 10.0)):
        r.add_obstacle(None, (0,), ("rect",) + rect)
    r.add_net("A", 0.125, [(1.0, 2.0, (0,)), (9.0, 2.0, (0,))])
    r.add_net("B", 0.125, [(1.0, 3.4, (0,)), (9.0, 3.4, (0,))])
    res = r.run()
    check(res.iterations >= 2 and res.conflicts == 0 and not res.failed,
          "negotiation: %d iterations, %d conflicts left, failed %s"
          % (res.iterations, res.conflicts, res.failed))
    pairs = [seg_seg((a1, b1), (a2, b2))
             for l1, a1, b1 in res.routes.get("A", {"segments": []})["segments"]
             for l2, a2, b2 in res.routes.get("B", {"segments": []})["segments"] if l1 == l2]
    check(len(pairs) > 0, "negotiation: %d same-layer A-B pairs to measure" % len(pairs))
    worst = min(pairs) if pairs else 0.0
    check(len(pairs) > 0 and worst >= 0.25 + CLR - 1e-6,
          "negotiation: A-B %.3f mm (need %.3f)" % (worst, 0.45))


def test_failed_net_leaves_no_copper():
    """F is unroutable (a netless wall splits its third terminal off) but its
    first leg is routable. That partial leg must not stay in the occupancy:
    B has to cross the strip F's leg would claim, and must do so cleanly."""
    r = R.Router((0, 0, 10, 6), 0.2, 1, CLR, 0.3)
    r.add_obstacle(None, (0,), ("rect", 4.6, 0.0, 5.6, 6.0))
    r.add_net("F", 0.125, [(0.0, 3.0, (0,)), (4.0, 3.0, (0,)), (9.0, 3.0, (0,))])
    r.add_net("B", 0.125, [(2.0, 1.0, (0,)), (2.0, 5.0, (0,))])
    res = r.run()
    check(res.failed == ["F"], "phantom: F failed (%s)" % res.failed)
    check("F" not in res.routes, "phantom: F absent from routes")
    check("B" in res.routes and res.conflicts == 0,
          "phantom: B routed, %d conflicts left" % res.conflicts)


def test_tree():
    r = R.Router((0, 0, 10, 10), 0.2, 1, CLR, 0.3)
    pts = [(1.0, 1.0), (9.0, 1.0), (5.0, 9.0)]
    r.add_net("T", 0.2, [(x, y, (0,)) for x, y in pts])
    res = r.run()
    check(not res.failed and all(touches(res, "T", x, y) for x, y in pts),
          "tree: all three terminals reached")


def test_deterministic():
    def once():
        r = R.Router((0, 0, 10, 10), 0.2, 2, CLR, 0.3)
        r.add_net("A", 0.125, [(1.0, 5.0, (0, 1)), (9.0, 5.0, (0, 1))])
        r.add_net("B", 0.125, [(5.0, 1.0, (0, 1)), (5.0, 9.0, (0, 1))])
        return r.run().routes
    check(once() == once(), "deterministic: two runs identical")


import hashlib


def _digest(routes):
    return hashlib.sha256(repr(sorted(routes.items())).encode()).hexdigest()


def _pin_scenario():
    """A board that exercises negotiation, vias and a three-terminal tree at
    once; its output is pinned so that every later change to the router must
    leave the default path byte-identical."""
    r = R.Router((0, 0, 12, 10), 0.2, 2, CLR, 0.3)
    for rect in ((5.0, 0.0, 6.0, 2.0), (5.0, 3.2, 6.0, 6.6), (5.0, 7.8, 6.0, 10.0)):
        r.add_obstacle(None, (0,), ("rect",) + rect)
    r.add_obstacle("P", (0, 1), ("circle", 8.0, 5.0, 0.4))
    r.add_net("A", 0.125, [(1.0, 2.0, (0,)), (11.0, 2.0, (0,))])
    r.add_net("B", 0.125, [(1.0, 3.4, (0,)), (11.0, 3.4, (0,))])
    r.add_net("C", 0.25, [(6.0, 1.0, (0, 1)), (6.0, 9.0, (0, 1)), (1.0, 9.0, (1,))])
    return r.run()


PINNED = "059a48d04d53b94c0f6fd7d9c0f0869ff1b1581a53dc654999ec266a3e35a64d"


def test_defaults_unchanged():
    res = _pin_scenario()
    d = _digest(res.routes)
    print("    pin digest", d)
    check(d == PINNED, "defaults: routes byte-identical to the pinned router output")


def _min_same_layer(res, n1, n2):
    """Edge-free centre-line distance between two nets' same-layer segments."""
    ds = [seg_seg((a1, b1), (a2, b2))
          for l1, a1, b1 in res.routes.get(n1, {"segments": []})["segments"]
          for l2, a2, b2 in res.routes.get(n2, {"segments": []})["segments"] if l1 == l2]
    return min(ds) if ds else None


def _pair_board(rule=None, exempt=None):
    """A straight at y 6; B from (1, 12) to (29, 12) with a netless block at
    x 10-20, y 11-18. Under the block is shorter for B but passes ~4.3 mm
    from A's centre line; over it is far."""
    r = R.Router((0, 0, 30, 20), 0.2, 1, CLR, 0.3)
    r.add_obstacle(None, (0,), ("rect", 10.0, 11.0, 20.0, 18.0))
    r.add_net("A", 0.125, [(1.0, 6.0, (0,)), (29.0, 6.0, (0,))], group="victim")
    r.add_net("B", 0.125, [(1.0, 12.0, (0,)), (29.0, 12.0, (0,))], group="aggr")
    if rule is not None:
        r.pair_clearance("victim", "aggr", rule)
    if exempt is not None:
        r.pair_exempt(exempt)
    return r.run()


def test_pair_binding():
    """Without the rule B passes under the block, closer than 5 mm edge to
    edge: the test board really tests something."""
    res = _pair_board()
    d = _min_same_layer(res, "A", "B")
    check(not res.failed and d is not None and d - 0.25 < 5.0,
          "pair binding: without a rule A-B edge %.3f mm (< 5.0)" % ((d or 0) - 0.25))


def test_pair_keeps_apart():
    res = _pair_board(rule=5.0)
    d = _min_same_layer(res, "A", "B")
    check(not res.failed and res.conflicts == 0, "pair: both routed (%s)" % res.failed)
    check(d is not None and d - 0.25 >= 5.0 - 1e-6,
          "pair: A-B edge %.3f mm (need 5.0)" % ((d or 0) - 0.25))


def test_pair_exempt():
    """An exemption rect over the passage under the block lifts the rule
    there: B goes under again, and every B point closer than 5 mm to A lies
    inside the rect."""
    rect = (9.0, 5.0, 21.0, 12.0)
    res = _pair_board(rule=5.0, exempt=rect)
    check(not res.failed, "exempt: B routed (%s)" % res.failed)
    close = []
    for _l, p, q in res.routes.get("B", {"segments": []})["segments"]:
        for t in (i / 20.0 for i in range(21)):
            x, y = p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])
            if abs(y - 6.0) - 0.25 < 5.0 - 1e-6:
                close.append((x, y))
    check(close, "exempt: B passes closer than 5 mm somewhere (%d points)" % len(close))
    check(all(rect[0] <= x <= rect[2] and rect[1] <= y <= rect[3] for x, y in close),
          "exempt: every close B point lies inside the exemption rect")


def test_pair_static_pad():
    """A static pad of the victim group, 1 mm off B's straight line, pushes B
    away to at least the rule distance."""
    r = R.Router((0, 0, 30, 20), 0.2, 1, CLR, 0.3)
    r.add_obstacle("V", (0,), ("rect", 14.5, 8.5, 15.5, 9.5), group="victim")
    r.add_net("B", 0.125, [(1.0, 10.5, (0,)), (29.0, 10.5, (0,))], group="aggr")
    r.pair_clearance("victim", "aggr", 3.0)
    res = r.run()
    worst = min((seg_rect((p, q), (14.5, 8.5, 15.5, 9.5))
                 for _l, p, q in res.routes.get("B", {"segments": []})["segments"]), default=None)
    check(not res.failed and worst is not None and worst - 0.125 >= 3.0 - 1e-6,
          "static pad: B edge %.3f mm from the victim pad (need 3.0)" % ((worst or 0) - 0.125))


def test_pair_other_layer():
    """The rule is per layer: B crosses A on the other layer without a via."""
    r = R.Router((0, 0, 20, 20), 0.2, 2, CLR, 0.3)
    r.add_net("A", 0.125, [(1.0, 10.0, (0,)), (19.0, 10.0, (0,))], group="victim")
    r.add_net("B", 0.125, [(10.0, 1.0, (1,)), (10.0, 19.0, (1,))], group="aggr")
    r.pair_clearance("victim", "aggr", 5.0)
    res = r.run()
    b = res.routes.get("B", {"segments": [], "vias": []})
    check(not res.failed and not b["vias"] and all(l == 1 for l, _p, _q in b["segments"]),
          "other layer: B routed on layer 1 only, no via (%s)" % res.failed)


def _via_pair_board(rule):
    """Two layers, a netless wall on layer 0 across B's straight path, so B has
    to change layer left of it. A victim pad on layer 0 sits 3.35 mm above the
    via's cheapest spot, a second one on layer 1 3.35 mm below it: a track at
    y 5 stays legal on both layers (>= 3.265 mm), a via (0.6 mm radius) does
    not."""
    r = R.Router((0, 0, 20, 10), 0.2, 2, CLR, 0.6)
    r.add_obstacle(None, (0,), ("rect", 9.0, 0.0, 11.0, 10.0))
    r.add_obstacle("V1", (0,), ("rect", 7.8, 8.35, 8.2, 8.75), group="victim")
    r.add_obstacle("V2", (1,), ("rect", 7.8, 1.25, 8.2, 1.65), group="victim")
    r.add_net("B", 0.125, [(1.0, 5.0, (0,)), (19.0, 5.0, (0,))], group="aggr")
    if rule is not None:
        r.pair_clearance("victim", "aggr", rule)
    return r.run()


_VIA_PADS = ((7.8, 8.35, 8.2, 8.75), (7.8, 1.25, 8.2, 1.65))


def _via_edge(via, pad):
    """Edge-to-edge distance from a via disc (radius 0.6) to a pad rect."""
    x, y = via
    l, t, r, b = pad
    return math.hypot(max(l - x, 0, x - r), max(t - y, 0, y - b)) - 0.6


def test_pair_via():
    """A via is a disc on every layer, so it must keep the pair distance from
    victim copper on both layers -- also where the track leading to it would
    be legal."""
    free = _via_pair_board(None)
    fv = free.routes.get("B", {"vias": []})["vias"]
    check(not free.failed and len(fv) >= 2 and min(_via_edge(v, p) for v in fv for p in _VIA_PADS) < 3.0,
          "pair via: without a rule a via sits closer than 3 mm to a pad (%s)" % (fv,))
    res = _via_pair_board(3.0)
    vias = res.routes.get("B", {"vias": []})["vias"]
    check(not res.failed and res.conflicts == 0 and len(vias) >= 1,
          "pair via: B routed with %d vias (%s)" % (len(vias), res.failed))
    worst = min((_via_edge(v, p) for v in vias for p in _VIA_PADS), default=0.0)
    check(worst >= 3.0 - 1e-6, "pair via: every via edge >= 3.0 mm from both pads (%.3f)" % worst)

def test_clip_outside():
    def same(got, want):
        return len(got) == len(want) and all(
            abs(g[i][j] - w[i][j]) < 1e-9 for g, w in zip(got, want) for i in (0, 1) for j in (0, 1))
    a, b = (0.0, 0.0), (10.0, 0.0)
    check(same(R._clip_outside(a, b, [(4.0, -1.0, 6.0, 1.0)]),
               [((0.0, 0.0), (4.0, 0.0)), ((6.0, 0.0), (10.0, 0.0))]),
          "clip: one rect splits the segment in two")
    check(same(R._clip_outside(a, b, [(4.0, -1.0, 6.0, 1.0), (5.0, -1.0, 8.0, 1.0)]),
               [((0.0, 0.0), (4.0, 0.0)), ((8.0, 0.0), (10.0, 0.0))]),
          "clip: overlapping rects merge")
    check(R._clip_outside((5.0, 0.0), (5.5, 0.0), [(4.0, -1.0, 6.0, 1.0)]) == [],
          "clip: a segment fully inside gives nothing")
    check(same(R._clip_outside(a, b, []), [(a, b)]), "clip: no rects gives the whole segment")


def test_clip_routed():
    """Victim A runs (1,6)-(13,6) and ends inside the exemption rect (10..14,
    5..7). Copper inside marks nothing, so B, routed after A (longer span),
    may run straight up x 14.6 -- 1.6 mm from A's end, outside the rect --
    while cells beside A's part outside the rect stay marked."""
    rect = (10.0, 5.0, 14.0, 7.0)
    r = R.Router((0, 0, 30, 20), 0.2, 1, CLR, 0.3)
    r.add_net("A", 0.125, [(1.0, 6.0, (0,)), (13.0, 6.0, (0,))], group="victim")
    r.add_net("B", 0.125, [(14.6, 0.5, (0,)), (14.6, 13.0, (0,))], group="aggr")
    r.pair_clearance("victim", "aggr", 3.0)
    r.pair_exempt(rect)
    res = r.run()
    check(not res.failed and res.conflicts == 0, "clip routed: both routed (%s)" % res.failed)
    check("B" in res.routes and length(res, "B") < 13.1,
          "clip routed: B straight past A's end, length %.2f (< 13.1)"
          % (length(res, "B") if "B" in res.routes else -1))
    occ = r._pocc["aggr"][0]
    check(occ[r._idx(0, int(round(14.6 / 0.2)), int(round(6.0 / 0.2)))] == 0,
          "clip routed: no mark beside the copper inside the rect")
    check(occ[r._idx(0, int(round(5.0 / 0.2)), int(round(9.2 / 0.2)))] > 0,
          "clip routed: cells beside the copper outside the rect are marked")


_GAPS = ((0.6, 1.8), (9.4, 10.6), (18.2, 19.4))     # south, middle, north


def _gap_board(tier_a, tier_b):
    """A wall with three gaps, each wide enough for one track. A (y 9) sits
    below B (y 11); both want the middle gap (y 10). Whoever loses it goes
    round through the gap on its own side -- A south, B north -- so either net
    can win without the two having to cross."""
    r = R.Router((0, 0, 10, 20), 0.2, 1, CLR, 0.3)
    edges = [0.0]
    for lo, hi in _GAPS:
        edges += [lo, hi]
    edges.append(20.0)
    for k in range(0, len(edges), 2):
        r.add_obstacle(None, (0,), ("rect", 4.5, edges[k], 5.5, edges[k + 1]))
    r.add_net("A", 0.125, [(1.0, 9.0, (0,)), (9.0, 9.0, (0,))], tier=tier_a)
    r.add_net("B", 0.125, [(1.0, 11.0, (0,)), (9.0, 11.0, (0,))], tier=tier_b)
    return r.run()


def _gaps_used(res, net):
    """Indices into _GAPS of the gaps `net` runs through."""
    segs = res.routes.get(net, {"segments": []})["segments"]
    return [k for k, (lo, hi) in enumerate(_GAPS)
            if any(min(p[0], q[0]) < 5.5 and max(p[0], q[0]) > 4.5
                   and lo <= min(p[1], q[1]) and max(p[1], q[1]) <= hi for _l, p, q in segs)]


def test_tiers():
    """The lower tier keeps the short way: with A at tier 0 it takes the middle
    gap and B goes north, with B at tier 0 the roles swap."""
    a_first = _gap_board(0, 1)
    b_first = _gap_board(1, 0)
    check(not a_first.failed and not b_first.failed and a_first.conflicts == 0 and b_first.conflicts == 0,
          "tiers: both boards routed without conflicts")
    check(_gaps_used(a_first, "A") == [1] and _gaps_used(a_first, "B") == [2],
          "tiers: A (tier 0) takes the middle gap, B the north one (A %s, B %s)"
          % (_gaps_used(a_first, "A"), _gaps_used(a_first, "B")))
    check(_gaps_used(b_first, "B") == [1] and _gaps_used(b_first, "A") == [0],
          "tiers: B (tier 0) takes the middle gap, A the south one (A %s, B %s)"
          % (_gaps_used(b_first, "A"), _gaps_used(b_first, "B")))


def test_stats():
    r = R.Router((0, 0, 10, 10), 0.2, 2, CLR, 0.3)
    r.add_obstacle(None, (0,), ("rect", 4.0, 0.0, 6.0, 10.0))
    r.add_net("V", 0.125, [(1.0, 5.0, (0,)), (9.0, 5.0, (0,))])
    res = r.run()
    st = res.stats.get("V", {})
    segs = res.routes.get("V", {"segments": [], "vias": []})
    want = sum(math.hypot(q[0] - p[0], q[1] - p[1]) for _l, p, q in segs["segments"])
    check(abs(st.get("length_mm", -1) - want) < 1e-5 and st.get("vias") == len(segs["vias"]) == 2,
          "stats: length %.3f (want %.3f), vias %s" % (st.get("length_mm", -1), want, st.get("vias")))


if __name__ == "__main__":
    for t in (test_straight, test_detour, test_via, test_own_net_passable,
              test_crossing_two_layers, test_side_by_side, test_negotiation,
              test_failed_net_leaves_no_copper, test_tree, test_deterministic,
              test_defaults_unchanged, test_pair_binding, test_pair_keeps_apart,
              test_pair_exempt, test_pair_static_pad, test_pair_other_layer,
              test_pair_via, test_clip_outside, test_clip_routed,
              test_tiers, test_stats):
        t()
    print("FAILED: %d" % len(FAILS) if FAILS else "all route checks passed")
    sys.exit(1 if FAILS else 0)
