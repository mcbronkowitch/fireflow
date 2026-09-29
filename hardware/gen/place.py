#!/usr/bin/env python3
"""Placement geometry shared by the generated boards (Rev A P4-1).

Runs under KiCad's Python only. Millimetres, y down, board coordinates -- as
kipcb. Boxes are (left, top, right, bottom). Moved here from the P4a spike's
stripe.py (hole_point, the spiral, the first-fit search), which is deleted.
"""
import math

import pcbnew

from gen import kipcb


def box(bb):
    return (pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop()),
            pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))


def grow(b, d):
    return (b[0] - d, b[1] - d, b[2] + d, b[3] + d)


def overlaps(a, b):
    """Strict: boxes that only touch do not overlap."""
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def gap(a, b):
    dx = max(b[0] - a[2], a[0] - b[2], 0.0)
    dy = max(b[1] - a[3], a[1] - b[3], 0.0)
    return math.hypot(dx, dy)


def inside(b, outer):
    return b[0] >= outer[0] and b[1] >= outer[1] and b[2] <= outer[2] and b[3] <= outer[3]


def _fab_layer(fp):
    return pcbnew.B_Fab if fp.IsFlipped() else pcbnew.F_Fab


def _crtyd_layer(fp):
    return pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd


def hole_point(fp):
    """Where the panel hole sits on this footprint: the centre of its one
    Fab circle, or its courtyard centre when it has none (the Thonk key).
    Probed 2026-09-29: pot shaft, jack bore and LED body are each the
    footprint's only F.Fab circle."""
    layer = _fab_layer(fp)
    circles = [g for g in fp.GraphicalItems()
               if hasattr(g, "GetShape") and g.GetShape() == pcbnew.SHAPE_T_CIRCLE
               and g.GetLayer() == layer]
    if len(circles) > 1:
        raise ValueError("%s: %d Fab circles, expected one or none"
                         % (fp.GetReference(), len(circles)))
    if circles:
        c = circles[0].GetCenter()
        return pcbnew.ToMM(c.x), pcbnew.ToMM(c.y)
    l, t, r, b = box(fp.GetCourtyard(_crtyd_layer(fp)).BBox())
    return (l + r) / 2.0, (t + b) / 2.0


def body_box(fp):
    """The part's body as a box: the bounding box of its Fab graphics (text
    excluded), or its courtyard when it has no Fab graphics (the Thonk key).
    Conservative: Fab lines that are not body, such as pin housings, count."""
    layer = _fab_layer(fp)
    boxes = [box(g.GetBoundingBox()) for g in fp.GraphicalItems()
             if g.GetLayer() == layer and g.GetClass() not in ("PCB_TEXT", "PCB_FIELD")]
    if not boxes:
        c = courtyard_box(fp)
        if c is None:
            raise ValueError("%s has neither Fab graphics nor a courtyard" % fp.GetReference())
        return c
    return (min(b[0] for b in boxes), min(b[1] for b in boxes),
            max(b[2] for b in boxes), max(b[3] for b in boxes))


def pad_boxes(fp):
    return [(str(p.GetNumber()), box(p.GetBoundingBox())) for p in fp.Pads()]


def courtyard_box(fp):
    bb = fp.GetCourtyard(_crtyd_layer(fp)).BBox()
    if not bb.GetWidth() and not bb.GetHeight():
        return None
    return box(bb)


def spiral(cx, cy, step, rmax):
    """Candidate centres in square rings around (cx, cy), nearest first
    inside each ring, in a fixed order."""
    yield cx, cy
    n = 1
    while n * step <= rmax + 1e-9:
        ring = ([(i, -n) for i in range(-n, n + 1)]
                + [(n, j) for j in range(-n + 1, n + 1)]
                + [(i, n) for i in range(n - 1, -n - 1, -1)]
                + [(-n, j) for j in range(n - 1, -n, -1)])
        ring.sort(key=lambda ij: (ij[0] ** 2 + ij[1] ** 2, ij))
        for i, j in ring:
            yield cx + i * step, cy + j * step
        n += 1


def first_fit(fp, target, blocked, inner, step, rmax,
              rotations=(0, 90, 180, 270), accept=None):
    """Move `fp` to the first spiral position around `target` (and the first
    rotation there) whose courtyard lies inside `inner`, overlaps nothing in
    `blocked`, and that `accept(fp)` approves. Appends the courtyard box to
    `blocked` and returns (x, y, rot)."""
    rel = {}
    for rot in rotations:
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(kipcb._pt(*target))
        c = courtyard_box(fp)
        if c is None:
            raise ValueError("%s has no courtyard to search with" % fp.GetReference())
        rel[rot] = (c[0] - target[0], c[1] - target[1], c[2] - target[0], c[3] - target[1])
    for x, y in spiral(target[0], target[1], step, rmax):
        for rot in rotations:
            l, t, r, b = rel[rot]
            cand = (x + l, y + t, x + r, y + b)
            if not inside(cand, inner) or any(overlaps(cand, o) for o in blocked):
                continue
            fp.SetOrientationDegrees(rot)
            fp.SetPosition(kipcb._pt(x, y))
            if accept is not None and not accept(fp):
                continue
            blocked.append(cand)
            return x, y, rot
    raise ValueError("no free place for %s within %.1f mm of (%.2f, %.2f)"
                     % (fp.GetReference(), rmax, target[0], target[1]))
