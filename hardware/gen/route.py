#!/usr/bin/env python3
"""Grid router for the generated boards (P4a spec §3.2). No pcbnew:
obstacles and terminals in, segments and vias out, millimetres throughout.

The model
---------
The routable area is a grid of cells PITCH apart on L copper layers. A path
is a chain of cell centres joined by 8-neighbour steps (so 0/45/90 degree
segments) and vias (a layer change at one cell).

Clearance is kept by *clearance classes*: one per distinct track half-width,
plus one for vias. A cell is forbidden to a class-c path if its centre is
closer to foreign copper than

    half_width(c) + clearance + slack

`slack` covers the grid: every point of a path lies within s = PITCH*sqrt(2)/2
of one of its cell centres. Against an exact obstacle shape one s suffices;
against another path, which is off-grid by up to s as well, two are needed.
Routed copper is therefore marked with half_width(own) + clearance +
half_width(c) + 2s (s where one side is a via, which sits on a cell centre).
Conservative on purpose: the DRC is the arbiter, and this only has to pass it.

Static obstacles (pads, locked tracks, keepouts) go into one array per class
holding FREE, the owning net's id (a net may cross its own copper), or
BLOCKED (netless, or claimed by two nets). Routed copper goes into a second
array per class that counts how many nets claim each cell.

Negotiated congestion (PathFinder, McMurchie & Ebeling 1995): in each round
every net still in conflict is ripped up and rerouted by A*; a cell other
nets already claim costs (1 + history) * (1 + pressure * claims). Pressure
grows each round and history accumulates where conflicts stay, until no net
overlaps another or the round limit is reached.

Pair clearances (P4-2 spec §4.2): nets and obstacles may carry a group;
`pair_clearance(A, B, mm)` keeps copper of A at least `mm` from copper of B
on the same layer. It is hard: a cell within reach of a paired group's copper
is forbidden like foreign static copper, never priced. Routed copper marks
its straight runs as capsules (radius own half-width + mm + target
half-width + 2s, s for the via class), vias as discs on every layer. Copper
inside a `pair_exempt` rect marks nothing, and no cell inside one is ever
marked.

Across a rect's edge (`pair_edge(A, B, mm)`, added 2026-10-08 for Rev A,
P4-2 spec §5.4): routed copper of one group inside a rect marks the cells
outside every rect within `mm`, and copper outside marks the cells inside
within `mm`, both for the other group. Static copper inside a rect (a pad)
is exempt: it marks nothing, and its own cells are never marked for its
group. Off unless called.

Via-only obstacles (`add_obstacle(..., via_only=True)`): rasterised into the
via class alone, with the via radius + clearance + s reach, so vias keep their
copper edge at least `clearance` off the shape while tracks pass over it.
They take no part in pair marking.

Priority tiers: `add_net(..., tier=t)`. Nets route in ascending tier, then by
span, so a lower tier claims its cells first. In the pressure term a net of
tier t counts claims by nets of tier <= t in full and claims by higher tiers
at a quarter. History is not tier-weighted (see Known limits). With one tier
in use the cost is the plain formula above. `Result.stats` holds length and
via count per routed net.

Deterministic: fixed net order, fixed neighbour order, a counter as the heap
tie-break, no randomness anywhere.

Known limits (P4a review)
-------------------------
- Off-grid terminal stubs: `_geometry` emits a stub from the pad centre to a
  cell up to about 0.57 mm away (the +-2-cell fallback in `_terminal_cells`).
  The stub is never entered into occupancy, so it has no clearance guarantee.
  A terminal given its pad shape (a fourth tuple element) whose window finds
  no usable cell falls back to the nearest usable cell inside the shape. That
  cell lies in the pad's own copper, so the route ending there is already the
  connection and no stub is emitted for it: kicad-cli checks a stub track on
  its own (copper_edge_clearance on the jack pads), and the track end need not
  be at the pad centre to connect.
- Two foreign halos overlapping mark a cell BLOCKED (`_rasterise`), so
  tight-pitch pads can become unreachable. The SOIC-16 at 1.27 mm worked; the
  SD socket and the module are untested.
- The A* heuristic aims at `tcells[j][0]` only. With multi-layer targets it is
  not admissible, so paths may be suboptimal.
- `_astar`'s via step checks only the via class, which is safe only while
  `via_radius >= half_width` (asserted in `run()`).
- When the round limit is hit, `run()` still returns routes with overlapping
  copper, and `Result.conflicts` counts NETS in conflict, not cells. Adapters
  must fail on `conflicts > 0`.
- The tree test checks endpoint contact, not connectivity.
- A static pair shape whose bbox centre lies inside an exemption rect marks
  nothing, even where it extends outside (fine for pads; a long locked seg
  would lose its mark).
- A pair mark is hard, never a conflict: a net that fails because a partner
  routed earlier is blocked goes to `failed` and is not retried, so pair
  failures depend on route order (tiers mitigate).
- History is shared across tiers; with a cheap alternative a lower-tier net
  may still yield, and both nets may detour.
"""
import heapq
import math
from array import array
from bisect import bisect_left, bisect_right

FREE, BLOCKED = 0, -1
SQRT2 = math.sqrt(2.0)
_DIRS = ((1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
         (1, 1, SQRT2), (1, -1, SQRT2), (-1, 1, SQRT2), (-1, -1, SQRT2))
_INF = float("inf")
# Pair marking's safety band (mm): a row's cells closer than radius - _BAND
# (analytically) are marked unexamined, cells farther than radius + _BAND are
# skipped, and every cell between gets the exact per-cell test. Any float
# error in the analytic spans is ~1e-12 mm, far inside the band.
_BAND = 1e-6
_PLUS1, _MINUS1 = (1).__add__, (-1).__add__      # pair-occupancy span updates


def _seg_point(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    if dx == 0 and dy == 0:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def _shape_dist(px, py, shape):
    kind = shape[0]
    if kind == "rect":
        _k, l, t, r, b = shape
        return math.hypot(max(l - px, 0.0, px - r), max(t - py, 0.0, py - b))
    if kind == "circle":
        _k, cx, cy, rad = shape
        return max(0.0, math.hypot(px - cx, py - cy) - rad)
    if kind == "seg":
        _k, x1, y1, x2, y2, hw = shape
        return max(0.0, _seg_point(px, py, x1, y1, x2, y2) - hw)
    raise ValueError("unknown shape %r" % (kind,))


def _shape_box(shape):
    kind = shape[0]
    if kind == "rect":
        return shape[1:5]
    if kind == "circle":
        _k, cx, cy, rad = shape
        return (cx - rad, cy - rad, cx + rad, cy + rad)
    _k, x1, y1, x2, y2, hw = shape
    return (min(x1, x2) - hw, min(y1, y2) - hw, max(x1, x2) + hw, max(y1, y2) + hw)


def _clip_outside(a, b, rects):
    """The parts of segment a-b outside every rect, as [(p, q)]."""
    spans = []
    (ax, ay), (bx, by) = a, b
    dx, dy = bx - ax, by - ay
    for l, t, r, btm in rects:
        t0, t1 = 0.0, 1.0
        ok = True
        for p, q in ((-dx, ax - l), (dx, r - ax), (-dy, ay - t), (dy, btm - ay)):
            if p == 0:
                if q < 0:
                    ok = False
                    break
                continue
            u = q / p
            if p < 0:
                t0 = max(t0, u)
            else:
                t1 = min(t1, u)
        if ok and t0 < t1:
            spans.append((t0, t1))
    spans.sort()
    out, cur = [], 0.0
    for s0, s1 in spans:
        if s0 > cur:
            out.append((cur, s0))
        cur = max(cur, s1)
    if cur < 1.0:
        out.append((cur, 1.0))
    return [((ax + u0 * dx, ay + u0 * dy), (ax + u1 * dx, ay + u1 * dy))
            for u0, u1 in out if u1 - u0 > 1e-9]


def _clip_inside(a, b, rects):
    """The parts of segment a-b inside some rect, as [(p, q)]: the
    complement of `_clip_outside` on the segment."""
    spans = []
    (ax, ay), (bx, by) = a, b
    dx, dy = bx - ax, by - ay
    for l, t, r, btm in rects:
        t0, t1 = 0.0, 1.0
        ok = True
        for p, q in ((-dx, ax - l), (dx, r - ax), (-dy, ay - t), (dy, btm - ay)):
            if p == 0:
                if q < 0:
                    ok = False
                    break
                continue
            u = q / p
            if p < 0:
                t0 = max(t0, u)
            else:
                t1 = min(t1, u)
        if ok and t0 < t1:
            spans.append((t0, t1))
    spans.sort()
    merged = []
    for s0, s1 in spans:
        if merged and s0 <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], s1)
        else:
            merged.append([s0, s1])
    return [((ax + u0 * dx, ay + u0 * dy), (ax + u1 * dx, ay + u1 * dy))
            for u0, u1 in merged if u1 - u0 > 1e-9]


def _subtract_spans(spans, mask):
    """Sorted disjoint index spans minus sorted disjoint mask spans."""
    out, j = [], 0
    for s0, s1 in spans:
        cur = s0
        while j < len(mask) and mask[j][1] <= cur:
            j += 1
        k = j
        while k < len(mask) and mask[k][0] < s1:
            m0, m1 = mask[k]
            if m0 > cur:
                out.append((cur, m0))
            cur = max(cur, m1)
            k += 1
        if cur < s1:
            out.append((cur, s1))
    return out


def _inside_any(x, y, rects):
    return any(l <= x <= r and t <= y <= b for l, t, r, b in rects)


def _capsule_span(ax, ay, bx, by):
    """For segment a-b, a function span(y, rr) giving the open
    x-interval of row y within rr of the segment (rr = radius + a small
    band either way), or None where the row misses; None instead of a
    function where a direction component is tiny but not zero (dividing by
    it would amplify rounding), so the caller tests every cell.

    The capsule is the union of the discs at both ends and the strip of
    points projecting onto the segment; it is convex, so its row slice is
    the hull of the pieces' slices."""
    dx, dy = bx - ax, by - ay
    ln = math.hypot(dx, dy)
    ux, uy = (dx / ln, dy / ln) if ln > 0 else (0.0, 0.0)
    if (ux != 0.0 and abs(ux) < 1e-6) or (uy != 0.0 and abs(uy) < 1e-6):
        return None

    def span(y, rr):
        if rr <= 0.0:
            return None
        lo, hi = _INF, -_INF
        for cx, cy in ((ax, ay), (bx, by)):
            d = y - cy
            if -rr < d < rr:
                h = math.sqrt((rr - d) * (rr + d))
                lo, hi = min(lo, cx - h), max(hi, cx + h)
        if ln > 0:
            e = y - ay
            if ux != 0.0:                   # 0 <= (x-ax)ux + e uy <= ln
                p0, p1 = (-e * uy) / ux, (ln - e * uy) / ux
                a0, a1 = (p0, p1) if p0 <= p1 else (p1, p0)
                ok = True
            else:
                a0, a1, ok = -_INF, _INF, 0.0 <= e * uy <= ln
            if ok:
                if uy != 0.0:               # |(x-ax)uy - e ux| < rr
                    q0, q1 = (e * ux - rr) / uy, (e * ux + rr) / uy
                    if q0 > q1:
                        q0, q1 = q1, q0
                    a0, a1 = max(a0, q0), min(a1, q1)
                else:
                    ok = -rr < e * ux < rr
            if ok and a0 < a1:
                lo, hi = min(lo, ax + a0), max(hi, ax + a1)
        return (lo, hi) if lo < hi else None
    return span


def _shape_span(shape):
    """`_capsule_span` for an obstacle shape: the cells with
    `_shape_dist < rr` (so none for rr <= 0)."""
    kind = shape[0]
    if kind == "rect":
        _k, l, t, r, b = shape

        def span(y, rr):
            d = t - y if t - y > 0.0 else (y - b if y - b > 0.0 else 0.0)
            if rr <= 0.0 or d >= rr:
                return None
            h = math.sqrt((rr - d) * (rr + d))
            return (l - h, r + h)
        return span
    if kind == "circle":
        _k, cx, cy, rad = shape
        disc = _capsule_span(cx, cy, cx, cy)
        return lambda y, rr: disc(y, rr + rad) if rr > 0.0 else None
    if kind == "seg":
        _k, x1, y1, x2, y2, hw = shape
        cap = _capsule_span(x1, y1, x2, y2)
        if cap is None:
            return None
        return lambda y, rr: cap(y, rr + hw) if rr > 0.0 else None
    raise ValueError("unknown shape %r" % (kind,))


def _merge_rows(rows):
    """{row base index: [[ix_lo, ix_hi], ...]} -> sorted disjoint cell index
    spans [(start, end)]."""
    out = []
    for base in sorted(rows):
        cur = None
        for lo, hi in sorted(rows[base]):
            if cur is not None and lo <= cur[1]:
                if hi > cur[1]:
                    cur[1] = hi
                continue
            if cur is not None:
                out.append((base + cur[0], base + cur[1]))
            cur = [lo, hi]
        if cur is not None:
            out.append((base + cur[0], base + cur[1]))
    return out


class Result:
    def __init__(self):
        self.routes = {}
        self.failed = []
        self.conflicts = 0
        self.iterations = 0
        self.stats = {}


class Router:
    def __init__(self, bounds, pitch, layers, clearance, via_radius, via_cost=8.0):
        self.x0, self.y0, x1, y1 = bounds
        self.pitch, self.layers, self.clearance = pitch, layers, clearance
        self.via_radius, self.via_cost = via_radius, via_cost
        self.nx = int(math.floor((x1 - self.x0) / pitch + 1e-9)) + 1
        self.ny = int(math.floor((y1 - self.y0) / pitch + 1e-9)) + 1
        self.plane = self.nx * self.ny
        self.s = pitch * SQRT2 / 2.0
        # the cell-centre coordinates `_xy` gives, per column and per row;
        # non-decreasing, so bisect finds the cells between two x values
        self._xs = [round(self.x0 + ix * self.pitch, 6) for ix in range(self.nx)]
        self._ys = [round(self.y0 + iy * self.pitch, 6) for iy in range(self.ny)]
        self._obstacles = []
        self._vobstacles = []      # via-only obstacles: (net, layers, shape)
        self._nets = []
        self._tshapes = []         # per net: per terminal, the pad's shape or None
        self._discs = {}
        self._groups = {}          # net -> group
        self._ogroups = []         # per obstacle: explicit group or None
        self._pairs = []           # (group_a, group_b, mm)
        self._exempt = []          # (left, top, right, bottom)
        self._edges = []           # (group_a, group_b, mm): across an exemption rect's edge
        self._cur_pair = None
        self._tiers = {}           # net -> priority tier
        self._cur_tier = 0

    # --- input -------------------------------------------------------------
    def add_obstacle(self, net, layers, shape, group=None, via_only=False):
        """`via_only` obstacles keep vias off (a via's copper edge at least
        `clearance` from the shape) but not tracks: they are rasterised into
        the via class only, and take no part in pair marking (so `group` is
        ignored for them). `net` None blocks every net's vias."""
        if via_only:
            self._vobstacles.append((net, tuple(layers), shape))
            return
        self._obstacles.append((net, tuple(layers), shape))
        self._ogroups.append(group)

    def add_net(self, name, half_width, terminals, group=None, tier=0):
        self._nets.append((name, half_width, [(t[0], t[1], tuple(t[2])) for t in terminals]))
        self._tshapes.append([t[3] if len(t) > 3 else None for t in terminals])
        if group is not None:
            self._groups[name] = group
        self._tiers[name] = int(tier)

    def pair_clearance(self, group_a, group_b, mm):
        self._pairs.append((group_a, group_b, float(mm)))

    def pair_exempt(self, rect):
        self._exempt.append(tuple(float(v) for v in rect))

    def pair_edge(self, group_a, group_b, mm):
        """Across an exemption rect's edge (Rev A P4-2 spec §5.4, added
        2026-10-08): routed copper of one group inside a rect keeps `mm` from
        the other group's copper outside every rect, and copper outside keeps
        `mm` from the other group's routed copper inside. Static copper inside
        a rect (a pad) stays exempt: it marks nothing, and its own cells are
        never marked for its group. Hard like a pair clearance. Off unless
        called."""
        self._edges.append((group_a, group_b, float(mm)))

    # --- grid ----------------------------------------------------------------
    def _idx(self, layer, ix, iy):
        return layer * self.plane + iy * self.nx + ix

    def _split(self, idx):
        layer, rem = divmod(idx, self.plane)
        iy, ix = divmod(rem, self.nx)
        return layer, ix, iy

    def _xy(self, ix, iy):
        return (round(self.x0 + ix * self.pitch, 6), round(self.y0 + iy * self.pitch, 6))

    def _disc(self, radius):
        """Cell offsets (dx, dy) whose centre lies closer than `radius`."""
        key = round(radius, 9)
        if key not in self._discs:
            n = int(math.ceil(radius / self.pitch))
            self._discs[key] = [(dx, dy) for dy in range(-n, n + 1) for dx in range(-n, n + 1)
                                if math.hypot(dx, dy) * self.pitch < radius]
        return self._discs[key]

    def _rasterise(self, arr, nid, layers, shape, radius):
        l, t, r, b = _shape_box(shape)
        ix0 = max(0, int(math.floor((l - radius - self.x0) / self.pitch)))
        ix1 = min(self.nx - 1, int(math.ceil((r + radius - self.x0) / self.pitch)))
        iy0 = max(0, int(math.floor((t - radius - self.y0) / self.pitch)))
        iy1 = min(self.ny - 1, int(math.ceil((b + radius - self.y0) / self.pitch)))
        for iy in range(iy0, iy1 + 1):
            for ix in range(ix0, ix1 + 1):
                x, y = self._xy(ix, iy)
                if _shape_dist(x, y, shape) >= radius:
                    continue
                for layer in layers:
                    i = self._idx(layer, ix, iy)
                    cur = arr[i]
                    if nid == BLOCKED or (cur != FREE and cur != nid):
                        arr[i] = BLOCKED
                    elif cur == FREE:
                        arr[i] = nid

    def _mark_rows(self, rows, layers, shape, radius, span, where="outside"):
        """Add to `rows` ({row base index: [[ix_lo, ix_hi], ...]}, one row per
        layer) every cell of the shape's bbox grown by `radius` whose centre
        has `_shape_dist < radius` and lies outside every exemption rect
        (`where` "outside", the default), inside one ("inside", pair_edge),
        or anywhere (None).

        That is the per-cell test over the bbox, enumerated row by row: the
        analytic `span` bounds each row's run, the cells within _BAND of its
        ends get the exact test, the rest are marked or skipped unexamined;
        the rects are cut out per row as exact index ranges (the same `<=`
        comparisons as `_inside_any`, found by bisect in the sorted `_xs`).
        With `span` None every bbox cell gets the exact test."""
        l, t, r, btm = _shape_box(shape)
        ix0 = max(0, int(math.floor((l - radius - self.x0) / self.pitch)))
        ix1 = min(self.nx - 1, int(math.ceil((r + radius - self.x0) / self.pitch)))
        iy0 = max(0, int(math.floor((t - radius - self.y0) / self.pitch)))
        iy1 = min(self.ny - 1, int(math.ceil((btm + radius - self.y0) / self.pitch)))
        xs, ys, nx, plane, exempt = self._xs, self._ys, self.nx, self.plane, self._exempt
        end = ix1 + 1
        for iy in range(iy0, iy1 + 1):
            y = ys[iy]
            if span is None:
                lo, hi, a, b = ix0, end, ix0, ix0
            else:
                o = span(y, radius + _BAND)
                if o is None:
                    continue
                lo = bisect_left(xs, o[0], ix0, end)
                hi = bisect_right(xs, o[1], lo, end)
                if lo >= hi:
                    continue
                inner = span(y, radius - _BAND)
                if inner is None:
                    a = b = lo
                else:
                    a = bisect_right(xs, inner[0], lo, hi)
                    b = bisect_left(xs, inner[1], a, hi)
            run = []
            for ix in range(lo, a):
                if _shape_dist(xs[ix], y, shape) < radius:
                    if run and run[-1][1] == ix:
                        run[-1][1] = ix + 1
                    else:
                        run.append([ix, ix + 1])
            if a < b:
                if run and run[-1][1] == a:
                    run[-1][1] = b
                else:
                    run.append([a, b])
            for ix in range(b, hi):             # b >= a: bisected from a
                if _shape_dist(xs[ix], y, shape) < radius:
                    if run and run[-1][1] == ix:
                        run[-1][1] = ix + 1
                    else:
                        run.append([ix, ix + 1])
            if where == "outside":
                for el, et, er, eb in exempt:
                    if run and et <= y <= eb:
                        p, q = bisect_left(xs, el), bisect_right(xs, er)
                        if p < q:
                            cut = []
                            for s0, s1 in run:
                                if s1 <= p or s0 >= q:
                                    cut.append([s0, s1])
                                    continue
                                if s0 < p:
                                    cut.append([s0, p])
                                if s1 > q:
                                    cut.append([q, s1])
                            run = cut
            elif where == "inside":
                keep = []
                for el, et, er, eb in exempt:
                    if run and et <= y <= eb:
                        p, q = bisect_left(xs, el), bisect_right(xs, er)
                        for s0, s1 in run:
                            a0, a1 = max(s0, p), min(s1, q)
                            if a0 < a1:
                                keep.append([a0, a1])
                run = keep
            if run:
                for layer in layers:
                    rows.setdefault(layer * plane + iy * nx, []).extend(run)

    def _capsule_cells(self, rows, layers, a, b, radius):
        """Add to `rows` (see `_mark_rows`) every cell (on each layer) whose
        centre lies within `radius` of segment a-b and outside every
        exemption rect."""
        self._mark_rows(rows, layers, ("seg", a[0], a[1], b[0], b[1], 0.0), radius,
                        _capsule_span(a[0], a[1], b[0], b[1]))

    def _shape_cells(self, layers, shape, radius):
        """Static obstacles: skipped entirely if their centre lies inside an
        exemption rect; otherwise every cell within `radius` of the shape,
        outside the rects. Returns the cells as index spans [(start, end)]."""
        l, t, r, btm = _shape_box(shape)
        if _inside_any((l + r) / 2.0, (t + btm) / 2.0, self._exempt):
            return []
        rows = {}
        self._mark_rows(rows, layers, shape, radius, _shape_span(shape))
        return _merge_rows(rows)

    def _pair_blocked(self, c, i):
        pb = self._cur_pair
        return pb is not None and (pb[0][c][i] or pb[1][c][i])

    # --- A* ----------------------------------------------------------------------
    def _astar(self, sources, targets, target_xy, nid, c, pres):
        # Speed: this loop is nearly the router's whole run time, so the cell
        # cost, the via step and the octile heuristic are written inline, and
        # `_idx` and `_split` are unrolled. This loop is the only
        # implementation, and its order is load-bearing: the same blocking
        # tests, the same floats in the same order of operations, the same
        # neighbour order (_DIRS, then the via steps in ascending layer) and
        # the same heap entries (f, g, n, i) with the same counter keep every
        # route byte-identical (hw_gen_route_guard's PINNED hash gates it).
        # The static test `st and st != nid` covers BLOCKED and foreign cells,
        # since FREE == 0 and nid >= 1.
        #
        # One exact prune: with pres >= 0 and hist >= 0 every factor of the
        # cell cost is >= 1.0 (occ >= le >= 0 always), so w * cost >= w and
        # gi + w * cost >= gi + w, because float rounding is monotone; likewise
        # a via step costs >= via_cost when via_cost >= 0. Where that lower
        # bound already fails `ng < g[j]`, the full step would fail it too, so
        # the neighbour is skipped before its cost is computed. It never pushes,
        # pops or skips anything the unpruned search would not.
        tix, tiy = target_xy
        targets = set(targets)
        nx, ny, plane, nlayers = self.nx, self.ny, self.plane, self.layers
        hk = SQRT2 - 2.0                          # the heuristic's diagonal term
        hist = self._hist
        v = self._via_c
        st_c, st_v = self._static[c], self._static[v]
        occ_c, occ_v = self._occ[c], self._occ[v]
        tiered = self._occ_le is not None
        if tiered:
            le_c = self._occ_le[self._cur_tier][c]
            le_v = self._occ_le[self._cur_tier][v]
        paired = self._cur_pair is not None
        if paired:
            pa_c, pb_c = self._cur_pair[0][c], self._cur_pair[1][c]
            pa_v, pb_v = self._cur_pair[0][v], self._cur_pair[1][v]
        via_cost, vias = self.via_cost, nlayers > 1
        prune = self._hist_nonneg and pres >= 0.0
        vprune = prune and via_cost >= 0.0
        layer_range = range(nlayers)
        dirs = [(dx, dy, w, dx + dy * nx) for dx, dy, w in _DIRS]   # d: j - i in the plane
        heappush, heappop = heapq.heappush, heapq.heappop

        # g lives in self._g, a per-cell array that is +inf outside a search
        # (the old dict's `g.get(j, inf)`); every cell given a g is in `prev`,
        # so the finally clause below puts exactly those back to +inf.
        g, prev, heap, n = self._g, {}, [], 0
        try:
            for i in sorted(sources):
                g[i], prev[i] = 0.0, None
                iy, ix = divmod(i % plane, nx)
                ex, ey = abs(ix - tix), abs(iy - tiy)
                heap.append(((ex + ey) + hk * min(ex, ey), 0.0, n, i))
                n += 1
            heapq.heapify(heap)
            while heap:
                _f, gi, _n, i = heappop(heap)
                if gi > g[i]:
                    continue
                if i in targets:
                    path = []
                    while i is not None:
                        path.append(i)
                        i = prev[i]
                    return path[::-1]
                layer, rem = divmod(i, plane)
                iy, ix = divmod(rem, nx)
                for dx, dy, w, d in dirs:
                    jx, jy = ix + dx, iy + dy
                    if 0 <= jx < nx and 0 <= jy < ny:
                        j = i + d
                        gj = g[j]
                        if prune and gi + w >= gj:
                            continue              # gi + w * cost >= gj: no relaxation
                        st = st_c[j]
                        if st and st != nid:      # BLOCKED or foreign: _cost's test
                            continue
                        if paired and (pa_c[j] or pb_c[j]):
                            continue
                        if tiered:
                            le = le_c[j]
                            cost = (1.0 + hist[j]) * (1.0 + pres * (le + 0.25 * (occ_c[j] - le)))
                        else:
                            cost = (1.0 + hist[j]) * (1.0 + pres * occ_c[j])
                        ng = gi + w * cost
                        if ng < gj:
                            g[j], prev[j] = ng, i
                            ex = jx - tix if jx >= tix else tix - jx
                            ey = jy - tiy if jy >= tiy else tiy - jy
                            heappush(heap, (ng + ((ex + ey) + hk * (ex if ex <= ey else ey)), ng, n, j))
                            n += 1
                if vias and vprune:
                    lb = gi + via_cost            # gi + vc >= lb
                    need = False
                    for other in layer_range:
                        if other != layer and lb < g[other * plane + rem]:
                            need = True
                            break
                else:
                    need = vias
                if need:
                    claims = 0
                    for l2 in layer_range:
                        k = l2 * plane + rem
                        st = st_v[k]
                        if st and st != nid:
                            break
                        if paired and (pa_v[k] or pb_v[k]):
                            break
                        if tiered:
                            le = le_v[k]
                            claims += le + 0.25 * (occ_v[k] - le)
                        else:
                            claims += occ_v[k]
                    else:
                        ng = gi + via_cost * (1.0 + pres * claims)
                        ex = ix - tix if ix >= tix else tix - ix
                        ey = iy - tiy if iy >= tiy else tiy - iy
                        hj = (ex + ey) + hk * (ex if ex <= ey else ey)
                        for other in layer_range:
                            if other != layer:
                                j = other * plane + rem
                                if ng < g[j]:
                                    g[j], prev[j] = ng, i
                                    heappush(heap, (ng + hj, ng, n, j))
                                    n += 1
            return None
        finally:
            for i in prev:
                g[i] = _INF

    # --- one net ---------------------------------------------------------------
    def _usable(self, c, i, nid):
        st = self._static[c][i]
        if st == BLOCKED or (st != FREE and st != nid):
            return False
        return not (self._cur_pair is not None and self._pair_blocked(c, i))

    def _terminal_cells(self, x, y, layers, nid, c, shape=None):
        """The nearest cell with a usable centre, per layer of the pad. Where
        the +-2-cell window finds none and the pad's `shape` is given, the
        cells whose centre lies inside the shape are searched instead (a pad
        that sits partly or wholly off the grid). Returns (cells, fallback):
        `fallback` is the set of those cells that came from the shape search,
        which `_geometry` joins to the pad by no stub."""
        cx = int(round((x - self.x0) / self.pitch))
        cy = int(round((y - self.y0) / self.pitch))
        out, fallback = [], set()
        for layer in layers:
            best = None
            for dy in range(-2, 3):
                for dx in range(-2, 3):
                    ix, iy = cx + dx, cy + dy
                    if not (0 <= ix < self.nx and 0 <= iy < self.ny):
                        continue
                    i = self._idx(layer, ix, iy)
                    if not self._usable(c, i, nid):
                        continue
                    px, py = self._xy(ix, iy)
                    key = (math.hypot(px - x, py - y), i)
                    if best is None or key < best:
                        best = key
            from_shape = best is None and shape is not None
            if from_shape:
                l, t, r, b = _shape_box(shape)
                ix0 = max(0, int(math.floor((l - self.x0) / self.pitch)))
                ix1 = min(self.nx - 1, int(math.ceil((r - self.x0) / self.pitch)))
                iy0 = max(0, int(math.floor((t - self.y0) / self.pitch)))
                iy1 = min(self.ny - 1, int(math.ceil((b - self.y0) / self.pitch)))
                for iy in range(iy0, iy1 + 1):
                    for ix in range(ix0, ix1 + 1):
                        px, py = self._xy(ix, iy)
                        if _shape_dist(px, py, shape) != 0.0:
                            continue
                        i = self._idx(layer, ix, iy)
                        if not self._usable(c, i, nid):
                            continue
                        key = (math.hypot(px - x, py - y), i)
                        if best is None or key < best:
                            best = key
            if best is not None:
                out.append(best[1])
                if from_shape:
                    fallback.add(best[1])
        return out, fallback

    def _route_net(self, k, pres):
        name, hw, terms = self._nets[k]
        nid, c = k + 1, self._class_of[k]
        g = self._net_group[k] if (self._pairs or self._edges) else None
        self._cur_pair = ((self._pstat[g], self._pocc[g]) if g in getattr(self, "_pstat", {}) else None)
        self._cur_tier = self._tier_of[k]
        found = [self._terminal_cells(x, y, ls, nid, c, sh)
                 for (x, y, ls), sh in zip(terms, self._tshapes[k])]
        tcells = [cells for cells, _fb in found]
        if any(not tc for tc in tcells):
            return False, []
        tree, done, paths = set(tcells[0]), [0], []
        todo = list(range(1, len(terms)))
        while todo:
            j = min(todo, key=lambda t: (min(math.hypot(terms[t][0] - terms[d][0],
                                                        terms[t][1] - terms[d][1])
                                             for d in done), t))
            _l, tix, tiy = self._split(tcells[j][0])
            path = self._astar(tree, tcells[j], (tix, tiy), nid, c, pres)
            if path is None:
                return False, paths
            paths.append(path)
            tree.update(path)
            tree.update(tcells[j])
            done.append(j)
            todo.remove(j)
        self._tcells[k] = tcells
        self._tfallback[k] = [fb for _cells, fb in found]
        return True, paths

    def _vias_of(self, paths):
        out = []
        for path in paths:
            for a, b in zip(path, path[1:]):
                la, ax, ay = self._split(a)
                lb, bx, by = self._split(b)
                if la != lb:
                    out.append((ax, ay))
        return out

    def _commit(self, k, paths):
        _name, hw, _t = self._nets[k]
        marks = [set() for _ in self._classes]
        for path in paths:
            for i in path:
                layer, ix, iy = self._split(i)
                for c2, hw2 in enumerate(self._classes):
                    extra = self.s if c2 == self._via_c else 2 * self.s
                    for dx, dy in self._disc(hw + self.clearance + hw2 + extra):
                        jx, jy = ix + dx, iy + dy
                        if 0 <= jx < self.nx and 0 <= jy < self.ny:
                            marks[c2].add(self._idx(layer, jx, jy))
        for ix, iy in self._vias_of(paths):
            for c2, hw2 in enumerate(self._classes):
                for dx, dy in self._disc(self.via_radius + self.clearance + hw2 + self.s):
                    jx, jy = ix + dx, iy + dy
                    if 0 <= jx < self.nx and 0 <= jy < self.ny:
                        for layer in range(self.layers):
                            marks[c2].add(self._idx(layer, jx, jy))
        for c2, m in enumerate(marks):
            occ = self._occ[c2]
            for i in m:
                occ[i] += 1
        if self._occ_le is not None:
            for t in range(self._tier_of[k], self._ntiers):
                for c2, m in enumerate(marks):
                    occ = self._occ_le[t][c2]
                    for i in m:
                        occ[i] += 1
        self._marks[k], self._paths[k] = marks, paths
        g = self._net_group[k] if (self._pairs or self._edges) else None
        pm = []
        srcs = self._pair_src.get(g, ()) if g is not None else ()
        if srcs:
            # A class's cell set depends on the distance only, never on the
            # target: built once per (mm, class), shared by every target at
            # that distance (and by _rip through _pmarks).
            clipped = [(layer, p, q) for layer, a, b in self._runs(paths)
                       for p, q in _clip_outside(a, b, self._exempt)]
            vias = [xy for xy in (self._xy(ix, iy) for ix, iy in self._vias_of(paths))
                    if not _inside_any(xy[0], xy[1], self._exempt)]
            built = {}
            for tgt, mm in srcs:
                for c2, hw2 in enumerate(self._classes):
                    spans = built.get((mm, c2))
                    if spans is None:
                        extra = self.s if c2 == self._via_c else 2 * self.s
                        rows = {}
                        for layer, p, q in clipped:
                            self._capsule_cells(rows, (layer,), p, q, hw + mm + hw2 + extra)
                        for x, y in vias:
                            self._capsule_cells(rows, range(self.layers), (x, y), (x, y),
                                                self.via_radius + mm + hw2 + self.s)
                        spans = built[(mm, c2)] = _merge_rows(rows)
                    occ = self._pocc[tgt][c2]
                    for s0, s1 in spans:
                        occ[s0:s1] = array("H", map(_PLUS1, occ[s0:s1]))
                    pm.append((tgt, c2, spans))
        esrcs = self._edge_src.get(g, ()) if g is not None else ()
        if esrcs:
            # pair_edge: copper outside every rect marks the cells inside the
            # rects within reach, copper inside a rect marks the cells outside
            runs = self._runs(paths)
            outside = [(layer, p, q) for layer, a, b in runs for p, q in _clip_outside(a, b, self._exempt)]
            inside = [(layer, p, q) for layer, a, b in runs for p, q in _clip_inside(a, b, self._exempt)]
            vxy = [self._xy(ix, iy) for ix, iy in self._vias_of(paths)]
            v_out = [xy for xy in vxy if not _inside_any(xy[0], xy[1], self._exempt)]
            v_in = [xy for xy in vxy if _inside_any(xy[0], xy[1], self._exempt)]
            for tgt, mm in esrcs:
                for c2, hw2 in enumerate(self._classes):
                    extra = self.s if c2 == self._via_c else 2 * self.s
                    rows = {}
                    for items, vias, where in ((outside, v_out, "inside"), (inside, v_in, "outside")):
                        for layer, p, q in items:
                            self._mark_rows(rows, (layer,), ("seg", p[0], p[1], q[0], q[1], 0.0),
                                            hw + mm + hw2 + extra, _capsule_span(p[0], p[1], q[0], q[1]),
                                            where=where)
                        for x, y in vias:
                            self._mark_rows(rows, range(self.layers), ("seg", x, y, x, y, 0.0),
                                            self.via_radius + mm + hw2 + self.s,
                                            _capsule_span(x, y, x, y), where=where)
                    spans = _subtract_spans(_merge_rows(rows), self._padmask.get(tgt, ()))
                    occ = self._pocc[tgt][c2]
                    for s0, s1 in spans:
                        occ[s0:s1] = array("H", map(_PLUS1, occ[s0:s1]))
                    pm.append((tgt, c2, spans))
        self._pmarks[k] = pm

    def _rip(self, k):
        if self._marks[k] is None:
            return
        for tgt, c2, spans in self._pmarks[k] or ():
            occ = self._pocc[tgt][c2]
            for s0, s1 in spans:
                occ[s0:s1] = array("H", map(_MINUS1, occ[s0:s1]))
        self._pmarks[k] = None
        for c2, m in enumerate(self._marks[k]):
            occ = self._occ[c2]
            for i in m:
                occ[i] -= 1
        if self._occ_le is not None:
            for t in range(self._tier_of[k], self._ntiers):
                for c2, m in enumerate(self._marks[k]):
                    occ = self._occ_le[t][c2]
                    for i in m:
                        occ[i] -= 1
        self._marks[k], self._paths[k] = None, []

    def _conflict_cells(self, k):
        """Cells where net k's copper sits on another net's claim."""
        if self._marks[k] is None:
            return []
        c, marks, out = self._class_of[k], self._marks[k], []
        for path in self._paths[k]:
            for i in path:
                if self._occ[c][i] - (1 if i in marks[c] else 0) > 0:
                    out.append(i)
        v = self._via_c
        for ix, iy in self._vias_of(self._paths[k]):
            for layer in range(self.layers):
                i = self._idx(layer, ix, iy)
                if self._occ[v][i] - (1 if i in marks[v] else 0) > 0:
                    out.append(i)
        return out

    # --- output ------------------------------------------------------------------
    def _runs(self, paths):
        """The straight runs of `paths` as [(layer, (x, y), (x, y))]."""
        segments = []
        for path in paths:
            run = [path[0]]
            for a, b in zip(path, path[1:]):
                la, ax, ay = self._split(a)
                lb, bx, by = self._split(b)
                if la != lb:
                    segments += self._run_segments(run)
                    run = [b]
                    continue
                if len(run) >= 2:
                    _l0, px, py = self._split(run[-2])
                    _l1, qx, qy = self._split(run[-1])
                    if (qx - px, qy - py) != (bx - ax, by - ay):
                        segments += self._run_segments(run)
                        run = [run[-1]]
                run.append(b)
            segments += self._run_segments(run)
        return segments

    def _geometry(self, k):
        _name, _hw, terms = self._nets[k]
        segments, used = self._runs(self._paths[k]), set()
        for path in self._paths[k]:
            used.update(path)
        for (x, y, _ls), cells, fb in zip(terms, self._tcells[k], self._tfallback[k]):
            for i in cells:
                if i in used and i not in fb:     # a fallback cell lies in the pad: no stub
                    layer, ix, iy = self._split(i)
                    cx, cy = self._xy(ix, iy)
                    if math.hypot(cx - x, cy - y) > 1e-6:
                        segments.append((layer, (x, y), (cx, cy)))
        vias = sorted(set(self._xy(ix, iy) for ix, iy in self._vias_of(self._paths[k])))
        return {"segments": segments, "vias": vias}

    def _run_segments(self, run):
        if len(run) < 2:
            return []
        la, ax, ay = self._split(run[0])
        _lb, bx, by = self._split(run[-1])
        return [(la, self._xy(ax, ay), self._xy(bx, by))]

    # --- the loop ----------------------------------------------------------------
    def run(self, max_iters=30, pres0=0.5, pres_mult=1.6, hist_inc=1.0):
        """Route every net. Returns a `Result`.

        If `max_iters` is reached with nets still overlapping, the routes
        returned still contain overlapping copper, and `Result.conflicts`
        counts NETS in conflict, not cells. Adapters must treat
        `conflicts > 0` (and a non-empty `failed`) as failure."""
        assert all(hw <= self.via_radius for _n, hw, _t in self._nets), \
            "via_radius must be >= every track half-width (see _astar's via step)"
        ids ={name: k + 1 for k, (name, _hw, _t) in enumerate(self._nets)}
        for net, _l, _s in self._obstacles + self._vobstacles:
            if net is not None and net not in ids:
                ids[net] = len(ids) + 1
        hws = sorted({hw for _n, hw, _t in self._nets})
        self._classes = hws + [self.via_radius]
        self._via_c = len(self._classes) - 1
        self._class_of = [hws.index(hw) for _n, hw, _t in self._nets]
        cells = self.layers * self.plane
        self._static = [array("i", [FREE]) * cells for _ in self._classes]
        self._occ = [array("i", [0]) * cells for _ in self._classes]
        tier_ids = sorted({self._tiers.get(name, 0) for name, _hw, _t in self._nets}) or [0]
        self._tier_of = [tier_ids.index(self._tiers.get(name, 0)) for name, _hw, _t in self._nets]
        self._ntiers = len(tier_ids)
        # _occ_le[t][c][i]: claims on cell i by nets of tier <= t (one tier: unused)
        self._occ_le = ([[array("i", [0]) * cells for _ in self._classes] for _ in range(self._ntiers)]
                        if self._ntiers > 1 else None)
        self._hist = array("d", [0.0]) * cells
        self._hist_nonneg = hist_inc >= 0.0    # licenses _astar's exact prune
        self._g = array("d", [_INF]) * cells   # _astar's g, +inf between searches
        for net, layers, shape in self._obstacles:
            nid = BLOCKED if net is None else ids[net]
            for c, hw in enumerate(self._classes):
                self._rasterise(self._static[c], nid, layers, shape, hw + self.clearance + self.s)
        for net, layers, shape in self._vobstacles:      # the via class only
            nid = BLOCKED if net is None else ids[net]
            self._rasterise(self._static[self._via_c], nid, layers, shape,
                            self.via_radius + self.clearance + self.s)
        self._net_group = [self._groups.get(name) for name, _hw, _t in self._nets]
        self._pair_src = {}            # source group -> [(target group, mm)]
        for ga, gb, mm in self._pairs:
            self._pair_src.setdefault(ga, []).append((gb, mm))
            self._pair_src.setdefault(gb, []).append((ga, mm))
        self._edge_src = {}            # pair_edge: source group -> [(target group, mm)]
        for ga, gb, mm in self._edges:
            self._edge_src.setdefault(ga, []).append((gb, mm))
            self._edge_src.setdefault(gb, []).append((ga, mm))
        targets = sorted({g for lst in list(self._pair_src.values()) + list(self._edge_src.values())
                          for g, _mm in lst})
        self._pstat = {g: [array("b", [0]) * cells for _ in self._classes] for g in targets}
        self._pocc = {g: [array("H", [0]) * cells for _ in self._classes] for g in targets}
        # pair_edge: the cells of a group's static copper inside a rect (its
        # pads there) are exempt and never marked for that group
        self._padmask = {}
        if self._edges:
            masks = {}
            for (net, layers, shape), og in zip(self._obstacles, self._ogroups):
                g = og if og is not None else self._groups.get(net)
                if g not in self._edge_src:
                    continue
                l, t, r, btm = _shape_box(shape)
                if _inside_any((l + r) / 2.0, (t + btm) / 2.0, self._exempt):
                    self._mark_rows(masks.setdefault(g, {}), layers, shape, self.s, _shape_span(shape), where=None)
            self._padmask = {g: _merge_rows(rows) for g, rows in masks.items()}
        for (net, layers, shape), og in zip(self._obstacles, self._ogroups):
            g = og if og is not None else self._groups.get(net)
            built = {}                 # per (mm, class), as in _commit
            for tgt, mm in self._pair_src.get(g, ()):
                for c, hw in enumerate(self._classes):
                    spans = built.get((mm, c))
                    if spans is None:
                        spans = built[(mm, c)] = self._shape_cells(layers, shape, hw + mm + self.s)
                    arr = self._pstat[tgt][c]
                    for s0, s1 in spans:
                        arr[s0:s1] = array("b", [1]) * (s1 - s0)
            # pair_edge: static copper outside every rect marks the cells
            # inside the rects within reach (static copper inside is exempt)
            esrc = self._edge_src.get(g, ())
            if esrc:
                l, t, r, btm = _shape_box(shape)
                if not _inside_any((l + r) / 2.0, (t + btm) / 2.0, self._exempt):
                    for tgt, mm in esrc:
                        for c, hw in enumerate(self._classes):
                            rows = {}
                            self._mark_rows(rows, layers, shape, hw + mm + self.s, _shape_span(shape),
                                            where="inside")
                            spans = _subtract_spans(_merge_rows(rows), self._padmask.get(tgt, ()))
                            arr = self._pstat[tgt][c]
                            for s0, s1 in spans:
                                arr[s0:s1] = array("b", [1]) * (s1 - s0)
        n = len(self._nets)
        self._marks, self._paths, self._tcells = [None] * n, [[] for _ in range(n)], [[] for _ in range(n)]
        self._tfallback = [[] for _ in range(n)]
        self._pmarks = [None] * n

        def span(k):
            ts = self._nets[k][2]
            xs, ys = [t[0] for t in ts], [t[1] for t in ts]
            return (max(xs) - min(xs) + max(ys) - min(ys), self._nets[k][0])

        order = sorted(range(n), key=lambda k: (self._tier_of[k], span(k)))
        res, failed, pres, todo = Result(), set(), pres0, list(order)
        for it in range(1, max_iters + 1):
            res.iterations = it
            for k in todo:
                self._rip(k)
                ok, paths = self._route_net(k, pres)
                if not ok:
                    # A failed net commits nothing: its partial paths never
                    # reach `routes`, so their claims would only charge the
                    # healthy nets for copper that does not exist.
                    failed.add(k)
                    continue
                self._commit(k, paths)
            bad = [(k, self._conflict_cells(k)) for k in order if k not in failed]
            bad = [(k, cc) for k, cc in bad if cc]
            res.conflicts = len(bad)
            if not bad:
                break
            for _k, cc in bad:
                for i in cc:
                    self._hist[i] += hist_inc
            pres *= pres_mult
            todo = [k for k, _cc in bad]
        res.failed = sorted(self._nets[k][0] for k in failed)
        for k in order:
            if k not in failed:
                name = self._nets[k][0]
                geo = self._geometry(k)
                res.routes[name] = geo
                res.stats[name] = {
                    "length_mm": round(sum(math.hypot(q[0] - p[0], q[1] - p[1])
                                           for _l, p, q in geo["segments"]), 6),
                    "vias": len(geo["vias"])}
        return res
