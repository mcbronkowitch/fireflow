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

Deterministic: fixed net order, fixed neighbour order, a counter as the heap
tie-break, no randomness anywhere.
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


class Result:
    def __init__(self):
        self.routes = {}
        self.failed = []
        self.conflicts = 0
        self.iterations = 0


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

    # --- input -------------------------------------------------------------
    def add_obstacle(self, net, layers, shape):
        self._obstacles.append((net, tuple(layers), shape))

    def add_net(self, name, half_width, terminals):
        self._nets.append((name, half_width, [(x, y, tuple(ls)) for x, y, ls in terminals]))

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

    # --- costs -----------------------------------------------------------------
    def _cost(self, c, i, nid, pres):
        st = self._static[c][i]
        if st == BLOCKED or (st != FREE and st != nid):
            return None
        return (1.0 + self._hist[i]) * (1.0 + pres * self._occ[c][i])

    def _via_step(self, ix, iy, nid, pres):
        v = self._via_c
        claims = 0
        for layer in range(self.layers):
            i = self._idx(layer, ix, iy)
            st = self._static[v][i]
            if st == BLOCKED or (st != FREE and st != nid):
                return None
            claims += self._occ[v][i]
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
        self._marks[k], self._paths[k] = marks, paths

    def _rip(self, k):
        if self._marks[k] is None:
            return
        for c2, m in enumerate(self._marks[k]):
            occ = self._occ[c2]
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
    def _geometry(self, k):
        _name, _hw, terms = self._nets[k]
        segments, used = [], set()
        for path in self._paths[k]:
            used.update(path)
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
        ids = {name: k + 1 for k, (name, _hw, _t) in enumerate(self._nets)}
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
        self._hist = array("d", [0.0]) * cells
        for net, layers, shape in self._obstacles:
            nid = BLOCKED if net is None else ids[net]
            for c, hw in enumerate(self._classes):
                self._rasterise(self._static[c], nid, layers, shape, hw + self.clearance + self.s)
        n = len(self._nets)
        self._marks, self._paths, self._tcells = [None] * n, [[] for _ in range(n)], [[] for _ in range(n)]

        def span(k):
            ts = self._nets[k][2]
            xs, ys = [t[0] for t in ts], [t[1] for t in ts]
            return (max(xs) - min(xs) + max(ys) - min(ys), self._nets[k][0])

        order = sorted(range(n), key=span)
        res, failed, pres, todo = Result(), set(), pres0, list(order)
        for it in range(1, max_iters + 1):
            res.iterations = it
            for k in todo:
                self._rip(k)
                ok, paths = self._route_net(k, pres)
                if not ok:
                    failed.add(k)
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
                res.routes[self._nets[k][0]] = self._geometry(k)
        return res
