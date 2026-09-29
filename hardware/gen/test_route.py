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


if __name__ == "__main__":
    for t in (test_straight, test_detour, test_via, test_own_net_passable,
              test_crossing_two_layers, test_side_by_side, test_negotiation,
              test_failed_net_leaves_no_copper, test_tree, test_deterministic):
        t()
    print("FAILED: %d" % len(FAILS) if FAILS else "all route checks passed")
    sys.exit(1 if FAILS else 0)
