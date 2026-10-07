"""Group fields of the 60 HP plate (spec docs/superpowers/specs/
2026-10-07-panel-nine-mm-raster-design.md §7).

A field is the union of axis-aligned rects (x0, x1, y0, y1), in mm, y down.
This module owns the geometry only: the union's outline as one closed
rectilinear polygon, point and rect containment, and the gap between two
fields. Lettering, colour and the legend notch stay in gen_hw_panel.py.

Rectilinear and not just "a rectangle or an L": the centre's TIMING field is
an arch with ROOM standing inside it, so the outline is traced from the union
itself rather than assembled from a fixed number of bands."""
import itertools
import math

EPS = 1e-9


def _covered(rects, x, y):
    return any(r[0] - EPS <= x <= r[1] + EPS and r[2] - EPS <= y <= r[3] + EPS
               for r in rects)


def _grid(rects, extra_x=(), extra_y=()):
    xs = sorted({v for r in rects for v in (r[0], r[1])} | set(extra_x))
    ys = sorted({v for r in rects for v in (r[2], r[3])} | set(extra_y))
    return xs, ys


def outline(rects):
    """Vertices of the union's boundary, clockwise on screen (y down),
    starting at the top-left-most corner so the first edge runs right along
    the top. Collinear vertices are dropped. Raises ValueError unless the
    union is one piece without holes -- a pinch at a corner counts as two."""
    xs, ys = _grid(rects)
    nx, ny = len(xs) - 1, len(ys) - 1
    fill = [[_covered(rects, (xs[i] + xs[i + 1]) / 2, (ys[j] + ys[j + 1]) / 2)
             for j in range(ny)] for i in range(nx)]

    def filled(i, j):
        return 0 <= i < nx and 0 <= j < ny and fill[i][j]

    nxt = {}

    def edge(a, b):
        if a in nxt:
            raise ValueError("the union touches itself at grid corner %r" % (a,))
        nxt[a] = b

    for i in range(nx):
        for j in range(ny):
            if not fill[i][j]:
                continue
            if not filled(i, j - 1):
                edge((i, j), (i + 1, j))              # top, left to right
            if not filled(i + 1, j):
                edge((i + 1, j), (i + 1, j + 1))      # right, top to bottom
            if not filled(i, j + 1):
                edge((i + 1, j + 1), (i, j + 1))      # bottom, right to left
            if not filled(i - 1, j):
                edge((i, j + 1), (i, j))              # left, bottom to top
    if not nxt:
        raise ValueError("empty field")
    start = min(nxt, key=lambda p: (p[1], p[0]))
    loop, p = [start], nxt[start]
    while p != start:
        loop.append(p)
        p = nxt[p]
        if len(loop) > len(nxt):
            raise ValueError("the outline does not close")
    if len(loop) != len(nxt):
        raise ValueError("the field is not one piece without holes "
                         "(%d of %d boundary edges on the outer loop)"
                         % (len(loop), len(nxt)))
    pts = [(xs[i], ys[j]) for i, j in loop]
    out = []
    for k, b in enumerate(pts):
        a, c = pts[k - 1], pts[(k + 1) % len(pts)]
        if (abs(a[0] - b[0]) < EPS and abs(b[0] - c[0]) < EPS) or \
           (abs(a[1] - b[1]) < EPS and abs(b[1] - c[1]) < EPS):
            continue
        out.append(b)
    k0 = min(range(len(out)), key=lambda k: (out[k][1], out[k][0]))
    return out[k0:] + out[:k0]


def contains_rect(rects, x0, x1, y0, y1):
    """True when the whole of (x0, x1, y0, y1) lies inside the union. Cut on
    every edge of the union and of the box, so a box straddling an inner
    corner cannot pass on its four corners alone."""
    xs, ys = _grid(rects, (x0, x1), (y0, y1))
    xs = [v for v in xs if x0 - EPS <= v <= x1 + EPS]
    ys = [v for v in ys if y0 - EPS <= v <= y1 + EPS]
    return all(_covered(rects, (xs[i] + xs[i + 1]) / 2, (ys[j] + ys[j + 1]) / 2)
               for i in range(len(xs) - 1) for j in range(len(ys) - 1))


def _rect_gap(a, b):
    dx = max(a[0] - b[1], b[0] - a[1], 0.0)
    dy = max(a[2] - b[3], b[2] - a[3], 0.0)
    return math.hypot(dx, dy)


class Field:
    """One group field. x, y is the outline's first vertex (top-left), w the
    length of its first edge along the top -- the edge the legend straddles --
    and h the drop from y to the lowest rect's bottom."""

    def __init__(self, rects):
        self.rects = [tuple(float(v) for v in r) for r in rects]
        self.outline = outline(self.rects)
        (self.x, self.y), (x1, _y1) = self.outline[0], self.outline[1]
        self.w = x1 - self.x
        self.h = max(r[3] for r in self.rects) - self.y

    def covers(self, x, y):
        return _covered(self.rects, x, y)

    def contains_rect(self, x0, x1, y0, y1):
        return contains_rect(self.rects, x0, x1, y0, y1)

    def gap_to(self, other):
        return min(_rect_gap(a, b) for a, b in itertools.product(self.rects, other.rects))

    def overlaps(self, other):
        return any(min(a[1], b[1]) - max(a[0], b[0]) > EPS and
                   min(a[3], b[3]) - max(a[2], b[2]) > EPS
                   for a, b in itertools.product(self.rects, other.rects))
