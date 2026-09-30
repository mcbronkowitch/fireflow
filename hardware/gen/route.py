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
- Two foreign halos overlapping mark a cell BLOCKED (`_rasterise`), so
  tight-pitch pads can become unreachable. The SOIC-16 at 1.27 mm worked; the
  SD socket and the module are untested.
- The A* heuristic aims at `tcells[j][0]` only. With multi-layer targets it is
  not admissible, so paths may be suboptimal.
- `_via_step` checks only the via class, which is safe only while
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

FREE, BLOCKED = 0, -1
SQRT2 = math.sqrt(2.0)
_DIRS = ((1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
         (1, 1, SQRT2), (1, -1, SQRT2), (-1, 1, SQRT2), (-1, -1, SQRT2))
_INF = float("inf")


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


def _inside_any(x, y, rects):
    return any(l <= x <= r and t <= y <= b for l, t, r, b in rects)


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
        self._obstacles = []
        self._nets = []
        self._discs = {}
        self._groups = {}          # net -> group
        self._ogroups = []         # per obstacle: explicit group or None
        self._pairs = []           # (group_a, group_b, mm)
        self._exempt = []          # (left, top, right, bottom)
        self._cur_pair = None
        self._tiers = {}           # net -> priority tier
        self._cur_tier = 0

    # --- input -------------------------------------------------------------
    def add_obstacle(self, net, layers, shape, group=None):
        self._obstacles.append((net, tuple(layers), shape))
        self._ogroups.append(group)

    def add_net(self, name, half_width, terminals, group=None, tier=0):
        self._nets.append((name, half_width, [(x, y, tuple(ls)) for x, y, ls in terminals]))
        if group is not None:
            self._groups[name] = group
        self._tiers[name] = int(tier)

    def pair_clearance(self, group_a, group_b, mm):
        self._pairs.append((group_a, group_b, float(mm)))

    def pair_exempt(self, rect):
        self._exempt.append(tuple(float(v) for v in rect))

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

    def _capsule_cells(self, out, layers, a, b, radius):
        """Add to `out` every cell index (on each layer) whose centre lies
        within `radius` of segment a-b and outside every exemption rect."""
        shape = ("seg", a[0], a[1], b[0], b[1], 0.0)
        l, t, r, btm = _shape_box(shape)
        ix0 = max(0, int(math.floor((l - radius - self.x0) / self.pitch)))
        ix1 = min(self.nx - 1, int(math.ceil((r + radius - self.x0) / self.pitch)))
        iy0 = max(0, int(math.floor((t - radius - self.y0) / self.pitch)))
        iy1 = min(self.ny - 1, int(math.ceil((btm + radius - self.y0) / self.pitch)))
        for iy in range(iy0, iy1 + 1):
            for ix in range(ix0, ix1 + 1):
                x, y = self._xy(ix, iy)
                if _shape_dist(x, y, shape) >= radius or _inside_any(x, y, self._exempt):
                    continue
                for layer in layers:
                    out.add(self._idx(layer, ix, iy))

    def _shape_cells(self, out, layers, shape, radius):
        """Static obstacles: skipped entirely if their centre lies inside an
        exemption rect; otherwise every cell within `radius` of the shape,
        outside the rects."""
        l, t, r, btm = _shape_box(shape)
        if _inside_any((l + r) / 2.0, (t + btm) / 2.0, self._exempt):
            return
        ix0 = max(0, int(math.floor((l - radius - self.x0) / self.pitch)))
        ix1 = min(self.nx - 1, int(math.ceil((r + radius - self.x0) / self.pitch)))
        iy0 = max(0, int(math.floor((t - radius - self.y0) / self.pitch)))
        iy1 = min(self.ny - 1, int(math.ceil((btm + radius - self.y0) / self.pitch)))
        for iy in range(iy0, iy1 + 1):
            for ix in range(ix0, ix1 + 1):
                x, y = self._xy(ix, iy)
                if _shape_dist(x, y, shape) >= radius or _inside_any(x, y, self._exempt):
                    continue
                for layer in layers:
                    out.add(self._idx(layer, ix, iy))

    def _pair_blocked(self, c, i):
        pb = self._cur_pair
        return pb is not None and (pb[0][c][i] or pb[1][c][i])

    # --- costs -----------------------------------------------------------------
    def _cost(self, c, i, nid, pres):
        st = self._static[c][i]
        if st == BLOCKED or (st != FREE and st != nid):
            return None
        if self._cur_pair is not None and self._pair_blocked(c, i):
            return None
        if self._occ_le is None:
            return (1.0 + self._hist[i]) * (1.0 + pres * self._occ[c][i])
        le = self._occ_le[self._cur_tier][c][i]
        return (1.0 + self._hist[i]) * (1.0 + pres * (le + 0.25 * (self._occ[c][i] - le)))

    def _via_step(self, ix, iy, nid, pres):
        v = self._via_c
        claims = 0
        for layer in range(self.layers):
            i = self._idx(layer, ix, iy)
            st = self._static[v][i]
            if st == BLOCKED or (st != FREE and st != nid):
                return None
            if self._cur_pair is not None and self._pair_blocked(v, i):
                return None
            if self._occ_le is None:
                claims += self._occ[v][i]
            else:
                le = self._occ_le[self._cur_tier][v][i]
                claims += le + 0.25 * (self._occ[v][i] - le)
        return self.via_cost * (1.0 + pres * claims)

    # --- A* ----------------------------------------------------------------------
    def _astar(self, sources, targets, target_xy, nid, c, pres):
        tix, tiy = target_xy
        targets = set(targets)

        def h(i):
            _l, ix, iy = self._split(i)
            dx, dy = abs(ix - tix), abs(iy - tiy)
            return (dx + dy) + (SQRT2 - 2.0) * min(dx, dy)

        g, prev, heap, n = {}, {}, [], 0
        for i in sorted(sources):
            g[i], prev[i] = 0.0, None
            heap.append((h(i), 0.0, n, i))
            n += 1
        heapq.heapify(heap)
        while heap:
            _f, gi, _n, i = heapq.heappop(heap)
            if gi > g[i]:
                continue
            if i in targets:
                path = []
                while i is not None:
                    path.append(i)
                    i = prev[i]
                return path[::-1]
            layer, ix, iy = self._split(i)
            steps = []
            for dx, dy, w in _DIRS:
                jx, jy = ix + dx, iy + dy
                if 0 <= jx < self.nx and 0 <= jy < self.ny:
                    j = self._idx(layer, jx, jy)
                    cost = self._cost(c, j, nid, pres)
                    if cost is not None:
                        steps.append((j, w * cost))
            if self.layers > 1:
                vc = self._via_step(ix, iy, nid, pres)
                if vc is not None:
                    for other in range(self.layers):
                        if other != layer:
                            steps.append((self._idx(other, ix, iy), vc))
            for j, w in steps:
                ng = gi + w
                if ng < g.get(j, _INF):
                    g[j], prev[j] = ng, i
                    heapq.heappush(heap, (ng + h(j), ng, n, j))
                    n += 1
        return None

    # --- one net ---------------------------------------------------------------
    def _terminal_cells(self, x, y, layers, nid, c):
        """The nearest cell with a usable centre, per layer of the pad."""
        cx = int(round((x - self.x0) / self.pitch))
        cy = int(round((y - self.y0) / self.pitch))
        out = []
        for layer in layers:
            best = None
            for dy in range(-2, 3):
                for dx in range(-2, 3):
                    ix, iy = cx + dx, cy + dy
                    if not (0 <= ix < self.nx and 0 <= iy < self.ny):
                        continue
                    i = self._idx(layer, ix, iy)
                    st = self._static[c][i]
                    if st == BLOCKED or (st != FREE and st != nid):
                        continue
                    if self._cur_pair is not None and self._pair_blocked(c, i):
                        continue
                    px, py = self._xy(ix, iy)
                    key = (math.hypot(px - x, py - y), i)
                    if best is None or key < best:
                        best = key
            if best is not None:
                out.append(best[1])
        return out

    def _route_net(self, k, pres):
        name, hw, terms = self._nets[k]
        nid, c = k + 1, self._class_of[k]
        g = self._net_group[k] if self._pairs else None
        self._cur_pair = ((self._pstat[g], self._pocc[g]) if g in getattr(self, "_pstat", {}) else None)
        self._cur_tier = self._tier_of[k]
        tcells = [self._terminal_cells(x, y, ls, nid, c) for x, y, ls in terms]
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
        g = self._net_group[k] if self._pairs else None
        pm = []
        for tgt, mm in self._pair_src.get(g, ()) if g is not None else ():
            for c2, hw2 in enumerate(self._classes):
                extra = self.s if c2 == self._via_c else 2 * self.s
                cells = set()
                for layer, a, b in self._runs(paths):
                    for p, q in _clip_outside(a, b, self._exempt):
                        self._capsule_cells(cells, (layer,), p, q, hw + mm + hw2 + extra)
                for ix, iy in self._vias_of(paths):
                    x, y = self._xy(ix, iy)
                    if not _inside_any(x, y, self._exempt):
                        self._capsule_cells(cells, range(self.layers), (x, y), (x, y),
                                            self.via_radius + mm + hw2 + self.s)
                occ = self._pocc[tgt][c2]
                for i in cells:
                    occ[i] += 1
                pm.append((tgt, c2, cells))
        self._pmarks[k] = pm

    def _rip(self, k):
        if self._marks[k] is None:
            return
        for tgt, c2, cells in self._pmarks[k] or ():
            occ = self._pocc[tgt][c2]
            for i in cells:
                occ[i] -= 1
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
        for (x, y, _ls), cells in zip(terms, self._tcells[k]):
            for i in cells:
                if i in used:
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
            "via_radius must be >= every track half-width (see _via_step)"
        ids ={name: k + 1 for k, (name, _hw, _t) in enumerate(self._nets)}
        for net, _l, _s in self._obstacles:
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
        for net, layers, shape in self._obstacles:
            nid = BLOCKED if net is None else ids[net]
            for c, hw in enumerate(self._classes):
                self._rasterise(self._static[c], nid, layers, shape, hw + self.clearance + self.s)
        self._net_group = [self._groups.get(name) for name, _hw, _t in self._nets]
        self._pair_src = {}            # source group -> [(target group, mm)]
        for ga, gb, mm in self._pairs:
            self._pair_src.setdefault(ga, []).append((gb, mm))
            self._pair_src.setdefault(gb, []).append((ga, mm))
        targets = sorted({g for lst in self._pair_src.values() for g, _mm in lst})
        self._pstat = {g: [array("b", [0]) * cells for _ in self._classes] for g in targets}
        self._pocc = {g: [array("H", [0]) * cells for _ in self._classes] for g in targets}
        for (net, layers, shape), og in zip(self._obstacles, self._ogroups):
            g = og if og is not None else self._groups.get(net)
            for tgt, mm in self._pair_src.get(g, ()):
                for c, hw in enumerate(self._classes):
                    marked = set()
                    self._shape_cells(marked, layers, shape, hw + mm + self.s)
                    arr = self._pstat[tgt][c]
                    for i in marked:
                        arr[i] = 1
        n = len(self._nets)
        self._marks, self._paths, self._tcells = [None] * n, [[] for _ in range(n)], [[] for _ in range(n)]
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
