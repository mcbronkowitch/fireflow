#!/usr/bin/env python3
"""Plane stitching shared by the generated boards: one via and a short track
per SMD pad on a plane net, placed by a collision search. Moved from the
coupon's build_pcb.py (P4a); the docstrings keep the coupon's probe history.
Runs under KiCad's Python.

Known limits (P4a review)
-------------------------
- The collision search ignores rule areas (keepouts); `via_keepouts` boxes
  are the caller's explicit exception (Rev A P4-2: the pot bodies).
- For the default 1.0-3.0 mm standoffs the `segments` check tests only the
  via point and the track midpoint, ignores layers, and has never been shown
  rejecting a candidate. The longer stubs of the `via_keepouts` search are
  checked along their whole length (`_long_candidate_clear`).
"""
import pcbnew

from gen import kipcb
from gen import pcb_proof as PP


# Keepout arithmetic for the collision search in stitch_plane_pads(): every
# margin here is the Minkowski-sum radius that lets the search treat a via or
# track as a POINT against an obstacle's own bounding box, inflated by
# whatever gap the board's DesignSettings actually requires around it.
CLEARANCE_MM = 0.2        # kipcb.new_board's bds.m_MinClearance
VIA_RADIUS_MM = 0.3       # kipcb.add_via's fixed 0.6 mm via width / 2
TRACK_HALF_MM = 0.25      # the 0.5 mm stitching track width / 2
PAD_KEEPOUT_MM = VIA_RADIUS_MM + CLEARANCE_MM      # 0.5 mm: via edge to pad edge
TRACK_KEEPOUT_MM = TRACK_HALF_MM + CLEARANCE_MM    # 0.45 mm: track edge to pad edge
VIA_VIA_MIN_MM = 2 * VIA_RADIUS_MM + 0.3           # 0.9 mm centre-to-centre,
                                                    # comfortably past the
                                                    # board's own 0.2495 mm
                                                    # hole-to-hole minimum
                                                    # between two 0.6 mm vias
STANDOFFS_MM = (1.0, 1.5, 2.0, 2.5, 3.0)
# The `via_keepouts` escape search (Rev A P4-2, spec §4.2.9): every point of
# a LONG_STEP_MM grid centred on the pad, at least STANDOFFS_MM[0] from it
# (no via in its own pad) and at most LONG_BOUND_MM. The bound is a pad at
# the very centre of a Rev A pot body (9.6 x 11.45 mm, probed 2026-10-01 on
# all 70 pots) escaping along the LONG axis: 11.45 / 2 + VIA_KEEPOUT_MM =
# 6.225 mm, rounded up; the short axis needs 5.3 mm, so a centre pad keeps a
# second way out. Probed on the 2026-10-01 board: eight rays (four axes,
# four diagonals, 0.5 mm steps) left 7 pads unresolved -- the free spots lie
# between the rays, among pot pins, tabs and IC rows; this grid leaves 1, and
# neither a 0.1 mm grid nor a 10 mm bound resolves that one.
LONG_STEP_MM = 0.25
LONG_BOUND_MM = 6.5
_N = int(round(LONG_BOUND_MM / LONG_STEP_MM))
LONG_OFFSETS_MM = tuple((i * LONG_STEP_MM, j * LONG_STEP_MM)
                        for i in range(-_N, _N + 1) for j in range(-_N, _N + 1)
                        if STANDOFFS_MM[0] - 1e-9 <= (i * i + j * j) ** 0.5 * LONG_STEP_MM
                        <= LONG_BOUND_MM + 1e-9)
# A via keepout box grows by the via radius (via POINT outside, via COPPER
# outside) plus the board's clearance. The rule it serves (route_check's
# pot_keepout) counts copper that only touches the box as a violation, so the
# bare radius would put a via exactly on the failing boundary; the 0.2 mm is
# the board's own minimum gap, not a rule of the pot body (it is no copper).
VIA_KEEPOUT_MM = VIA_RADIUS_MM + CLEARANCE_MM      # 0.5 mm


def _pad_obstacles(board):
    """`[(netname, (left, top, right, bottom))]` for every pad on the board,
    world-aligned -- `pad.GetBoundingBox()` already accounts for the
    footprint's own rotation, so a pad on an R_LED turned 90 deg reports the
    same shape a rotation-naive reader of its local pad size would miss."""
    obstacles = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            bb = pad.GetBoundingBox()
            obstacles.append((pad.GetNetname(), (
                pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop()),
                pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))))
    return obstacles


def _clear_of_pads(x, y, net, obstacles, margin):
    for onet, (left, top, right, bottom) in obstacles:
        if onet == net or not onet:      # same net may touch; no-net pads don't exist electrically
            continue
        if left - margin <= x <= right + margin and top - margin <= y <= bottom + margin:
            return False
    return True


def _tht_keepoffs(board, plane_nets, keepoff_mm):
    """Boxes for `tht_keepoff_mm`: the bounding box of every through-hole pad
    on a plane net, grown by the via radius plus the keep-off -- the same
    Minkowski-sum trick as PAD_KEEPOUT_MM, so a via POINT outside the grown
    box has its copper at least `keepoff_mm` from the pad's copper."""
    m = VIA_RADIUS_MM + keepoff_mm
    boxes = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() in plane_nets and pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                bb = pad.GetBoundingBox()
                boxes.append((pcbnew.ToMM(bb.GetLeft()) - m, pcbnew.ToMM(bb.GetTop()) - m,
                              pcbnew.ToMM(bb.GetRight()) + m, pcbnew.ToMM(bb.GetBottom()) + m))
    return boxes


def _clear_of_keepoffs(x, y, keepoffs):
    return not any(l <= x <= r and t <= y <= b for l, t, r, b in keepoffs)


def _clear_of_vias(x, y, placed):
    return all(((x - vx) ** 2 + (y - vy) ** 2) ** 0.5 >= VIA_VIA_MIN_MM
               for vx, vy in placed)


def _candidate_clear(px, py, vx, vy, net, obstacles, placed, segments=(), keepoffs=()):
    """A candidate via is clear if the via point, and the midpoint of the
    straight pad->via track, both clear every different-net pad by their
    respective keepouts, and the via clears every via already placed this
    pass (any net -- hole-to-hole is a drilling constraint, not a net one).
    `keepoffs` (see `_tht_keepoffs`) are boxes the via point, never the
    track, must stay out of -- whatever the via's net.
    One midpoint sample is a coarse stand-in for the whole <=3 mm segment;
    the real arbiter is `check_stitch_hygiene()`'s kicad-cli gate, which
    this search only exists to satisfy on the first try."""
    if not _clear_of_vias(vx, vy, placed):
        return False
    if not _clear_of_pads(vx, vy, net, obstacles, PAD_KEEPOUT_MM):
        return False
    if not _clear_of_keepoffs(vx, vy, keepoffs):
        return False
    mx, my = (px + vx) / 2.0, (py + vy) / 2.0
    for snet, a, b, hw in segments:
        if snet == net:
            continue
        if PP.seg_point_dist((vx, vy), a, b) < hw + PAD_KEEPOUT_MM:
            return False
        if PP.seg_point_dist((mx, my), a, b) < hw + TRACK_KEEPOUT_MM:
            return False
    return _clear_of_pads(mx, my, net, obstacles, TRACK_KEEPOUT_MM)


def _find_via_offset(px, py, fx, fy, half_w, half_h, net, obstacles, placed, segments=(),
                     keepoffs=()):
    """Search for a clear via position, starting from the courtyard-
    normalized outward direction (see `stitch_plane_pads()`) and widening
    from there: that direction first, then the perpendicular one, then both
    reversed -- each at growing standoff, 1.0 mm to 3.0 mm -- before giving
    up. Reversed directions matter for parts wedged against a denser
    neighbour on their outward side (the LED/resistor and bulk-cap columns,
    4 mm pitch): the free space there is often on the *other* side of the
    pad, or across the part rather than along its row."""
    dx, dy = px - fx, py - fy
    if abs(dx) / half_w >= abs(dy) / half_h:
        primary = (1.0 if dx >= 0 else -1.0, 0.0)
        secondary = (0.0, 1.0 if dy >= 0 else (-1.0 if dy < 0 else 1.0))
    else:
        primary = (0.0, 1.0 if dy >= 0 else -1.0)
        secondary = (1.0 if dx >= 0 else (-1.0 if dx < 0 else 1.0), 0.0)
    for ux, uy in (primary, secondary, (-primary[0], -primary[1]), (-secondary[0], -secondary[1])):
        for d in STANDOFFS_MM:
            vx, vy = px + ux * d, py + uy * d
            if _candidate_clear(px, py, vx, vy, net, obstacles, placed, segments, keepoffs):
                return vx, vy
    return None


def _grow_boxes(boxes, m):
    return [(l - m, t - m, r + m, b + m) for l, t, r, b in boxes]


def _in_box(x, y, box):
    l, t, r, b = box
    return l <= x <= r and t <= y <= b


def _seg_hits_box(a, b, box):
    """Liang-Barsky: does segment a-b meet the closed box at all?"""
    l, t, r, btm = box
    dx, dy = b[0] - a[0], b[1] - a[1]
    u0, u1 = 0.0, 1.0
    for p, q in ((-dx, a[0] - l), (dx, r - a[0]), (-dy, a[1] - t), (dy, btm - a[1])):
        if p == 0:
            if q < 0:
                return False
            continue
        u = q / p
        if p < 0:
            u0 = max(u0, u)
        else:
            u1 = min(u1, u)
        if u0 > u1:
            return False
    return True


def _seg_box_dist(a, b, box):
    """Exact distance from segment a-b to an axis-aligned box: 0 if they
    meet, else the least of each end to the box and each corner to the
    segment (two disjoint convex shapes are closest at a vertex of one)."""
    if _seg_hits_box(a, b, box):
        return 0.0
    l, t, r, btm = box

    def pt_box(p):
        return ((max(l - p[0], 0.0, p[0] - r)) ** 2 + (max(t - p[1], 0.0, p[1] - btm)) ** 2) ** 0.5
    return min(pt_box(a), pt_box(b),
               *(PP.seg_point_dist(c, a, b) for c in ((l, t), (r, t), (r, btm), (l, btm))))


def _orient(a, b, c):
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])


def _seg_seg_dist(a, b, c, d):
    """Exact closest approach of two segments: `PP.seg_seg_dist` plus the
    crossing case it leaves out (two segments that cross are 0 apart)."""
    if _orient(a, b, c) * _orient(a, b, d) < 0 and _orient(c, d, a) * _orient(c, d, b) < 0:
        return 0.0
    return PP.seg_seg_dist((a, b), (c, d))


def _long_candidate_clear(px, py, vx, vy, net, layer, obstacles, placed, segments, keepoffs,
                          stubs):
    """`_candidate_clear` for the `via_keepouts` escape search, with the
    track checked along its WHOLE length instead of at its midpoint (spec
    §4.2.9: these stubs run to 6.5 mm). The via point keeps the default
    search's checks. The track keeps TRACK_KEEPOUT_MM, edge to edge, off
    every different-net pad box (exact segment-to-box distance, any layer:
    the pad list carries no layer, which only ever over-rejects) and off
    every `segments` entry. `stubs` = `[(net, layer, pad point, via point)]`,
    every stitch already placed this pass: the track keeps clear of a
    different-net stub on its own layer and of that stub's via, and the via
    keeps clear of every different-net stub (it spans all layers)."""
    if not _clear_of_vias(vx, vy, placed):
        return False
    if not _clear_of_pads(vx, vy, net, obstacles, PAD_KEEPOUT_MM):
        return False
    if not _clear_of_keepoffs(vx, vy, keepoffs):
        return False
    a, b = (px, py), (vx, vy)
    for onet, box in obstacles:
        if onet == net or not onet:
            continue
        if _seg_box_dist(a, b, box) < TRACK_KEEPOUT_MM:
            return False
    for snet, c, d, hw in segments:
        if snet == net:
            continue
        if PP.seg_point_dist(b, c, d) < hw + PAD_KEEPOUT_MM:
            return False
        if _seg_seg_dist(a, b, c, d) < hw + TRACK_KEEPOUT_MM:
            return False
    for snet, slayer, c, d in stubs:
        if snet == net:
            continue
        if PP.seg_point_dist(b, c, d) < TRACK_HALF_MM + PAD_KEEPOUT_MM:
            return False
        if PP.seg_point_dist(d, a, b) < VIA_RADIUS_MM + TRACK_KEEPOUT_MM:
            return False
        if slayer == layer and _seg_seg_dist(a, b, c, d) < TRACK_HALF_MM + TRACK_KEEPOUT_MM:
            return False
    return True


def _default_in_box(px, py, boxes):
    """True if any candidate of the default search (four axis directions,
    STANDOFFS_MM) falls inside one of `boxes` -- i.e. a via keepout took
    part in that search's failure."""
    return any(_in_box(px + ux * d, py + uy * d, box)
               for ux, uy in ((1.0, 0.0), (-1.0, 0.0), (0.0, 1.0), (0.0, -1.0))
               for d in STANDOFFS_MM for box in boxes)


def _find_long_via_offset(px, py, fx, fy, half_w, half_h, net, layer, obstacles, placed,
                          segments, keepoffs, stubs):
    """The `via_keepouts` escape search, run only when the default search
    failed with a via keepout among its blockers: LONG_OFFSETS_MM, shortest
    stub first -- the least copper the router has to go around. Equal
    lengths go to the default search's outward (courtyard-normalized)
    direction first, then its perpendicular, then by angle, so the order is
    fixed. Pads further than the bound plus the via's pad keepout cannot
    block, so they are dropped up front (the grid is ~2000 points)."""
    dx, dy = px - fx, py - fy
    if abs(dx) / half_w >= abs(dy) / half_h:
        primary = (1.0 if dx >= 0 else -1.0, 0.0)
        secondary = (0.0, 1.0 if dy >= 0 else -1.0)
    else:
        primary = (0.0, 1.0 if dy >= 0 else -1.0)
        secondary = (1.0 if dx >= 0 else -1.0, 0.0)
    reach = LONG_BOUND_MM + PAD_KEEPOUT_MM
    near = [(onet, (l, t, r, b)) for onet, (l, t, r, b) in obstacles
            if l - reach <= px <= r + reach and t - reach <= py <= b + reach]

    def key(o):
        d = (o[0] * o[0] + o[1] * o[1]) ** 0.5
        return (round(d, 6), -round((o[0] * primary[0] + o[1] * primary[1]) / d, 6),
                -round((o[0] * secondary[0] + o[1] * secondary[1]) / d, 6), o)
    for ox, oy in sorted(LONG_OFFSETS_MM, key=key):
        vx, vy = px + ox, py + oy
        if _long_candidate_clear(px, py, vx, vy, net, layer, near, placed, segments,
                                 keepoffs, stubs):
            return vx, vy
    return None


NETLESS = "<no net>"   # stands in for "" when netless pads block (netless_blocks=True)


def stitch_plane_pads(board, plane_nets, segments=(), netless_blocks=False,
                      tht_keepoff_mm=None, via_keepouts=()):
    """One via + 0.5 mm F.Cu track per SMD pad on a plane net, generated
    mechanically from the board's own pads rather than as hand data (brief
    step 2: `pad.GetNetname()` in the four plane nets and
    `pad.GetAttribute() == pcbnew.PAD_ATTRIB_SMD`). Through-hole pads on the
    same nets need nothing here -- the hole itself reaches every copper
    layer, In1/In2 included.

    Via placement starts 1.0 mm from the pad centre, stepped along whichever
    body axis (x or y) the pad sits *proportionally* closer to the edge of --
    (dx / half-width) vs (dy / half-height) of the footprint's own courtyard
    box, not the raw (dx, dy) magnitudes. Tried first and wrong: raw-
    magnitude dominance picks the axis the pad happens to be furthest along
    in absolute terms, which for an oblong two-row IC is the *along-the-row*
    axis for any pin more than a package half-width from centre -- so the
    via walks parallel to the row, straight at the next pin. Confirmed as an
    actual `shorting_items` violation (GND via landing 0.78 mm from `U_IN1`
    pin 16, `+3V3`) with the raw vector, and *still* shorting after switching
    to raw-magnitude axis snapping (GND via 0.27 mm from the same pin,
    because pin 13 sits further from the package's vertical centre than the
    half-width, so the wrong axis won both times). Normalizing each axis by
    the courtyard's own half-extent asks the question the geometry actually
    needs -- "is this pad nearer the package's left/right edge or its
    top/bottom edge" -- so a pin's step stays perpendicular to the row it is
    in, moving it away from every other pin on that row at once instead of
    along it. `JP_GND`/`JP_3V3` fall out of the same rule for free: their two
    pads straddle the moat on the y axis with a much taller courtyard than it
    is wide, so each pad's dominant normalized axis is y and the via lands in
    its own zone (digital pad north, analog pad south) -- what actually joins
    each domain pair when the jumper is closed.

    That axis rule alone still left real clearance/hole_clearance violations
    where a component sits in a tightly-pitched column (the 4 mm-pitch
    LED/resistor and bulk-cap columns): the geometrically correct outward
    direction has no 1.0 mm of clear room there, because the next part in
    the column is already only ~0.2 mm past it. `_find_via_offset()` widens
    the same rule into a real collision search -- same starting direction,
    growing standoff, then the perpendicular and both reversed directions --
    checked against every other-net pad's actual bounding box and every via
    already placed this pass, so it stops at the first position that is
    genuinely clear rather than trusting the first guess. Review round 1
    measured two severe results of the untested first guess: a GND via at
    0.0375 mm actual clearance to a +-12V bulk-cap pad, and several
    ~0.10-0.15 mm LED-column squeezes, all below the board's 0.2 mm floor.

    Both `GetPosition()` calls return absolute board coordinates, so no
    separate un-rotate step is needed for any axis or direction choice.

    The 0.6 mm via / 0.3 mm drill come from `kipcb.add_via` (the Global
    Constraints minimums); the 0.5 mm track width is spec Section 4's supply
    width, used everywhere per the brief (0.25 mm AGND sense-side decouplers
    is unnecessary here).

    P4a: the track goes on the pad's own copper layer (a flipped footprint's
    SMD pads are on B.Cu; `pad.IsOnLayer`, not `GetLayerName`, tells), and
    `segments = [(net, (x1, y1), (x2, y2), half_width)]` are extra obstacles
    -- locked tracks -- that the via and the track midpoint keep clear of.
    Returns `({net: vias placed}, [unresolved lines])`; it prints nothing.

    `netless_blocks=True` makes netless pads obstacles as well. The coupon's
    search skips them (`_clear_of_pads`: "no-net pads don't exist
    electrically") and keeps doing so -- its board must not change. A netless
    PTH pad is still copper with a hole, though: on the P4a strip a pot's
    mounting tab (pad "", no net) took two GND stitching vias, which DRC
    reported as shorting_items and hole_clearance (Task 8, run 1).

    `tht_keepoff_mm` (default None: off, the coupon's behaviour exactly)
    makes every through-hole pad on a plane net an obstacle for the VIA, of
    any net including the via's own, with the via's copper kept that far
    (edge to edge) from the pad's copper. A via beside such a pad cuts into
    the pad's thermal spokes on the inner planes (Rev A P4-2: starved
    thermals); the pad's hole already joins every layer, so nothing is lost.
    Only the via is kept off; the F.Cu track may still pass.

    `via_keepouts` (default empty: off, the coupon's behaviour exactly) are
    boxes `(left, top, right, bottom)` in mm that via COPPER stays out of,
    whatever the via's net; tracks may cross them. Rev A passes its pot
    body boxes (spec §4.2.9: Task 7 found 15 stitching vias inside them, 11
    of their pads under a body themselves -- back-side parts under front
    pots). Each box grows by VIA_KEEPOUT_MM and joins the default search's
    keep-offs. A pad whose default search then fails with a candidate
    inside such a box gets the longer escape search
    (`_find_long_via_offset`: a 0.25 mm grid out to 6.5 mm, the stub
    checked along its whole length) on the pad's own layer; a B.Cu stub
    under a front pot body is no F.Cu copper. Each escape stub is then an
    extra `segments` obstacle for the pads stitched after it. A pad that
    still finds nothing is unresolved as before, its line marked when the
    1.0 mm default via lands inside a keepout.
    """
    boxes = kipcb.courtyard_boxes(board)
    obstacles = _pad_obstacles(board)
    if netless_blocks:
        obstacles = [(onet or NETLESS, box) for onet, box in obstacles]
    keepoffs = () if tht_keepoff_mm is None else _tht_keepoffs(board, plane_nets, tht_keepoff_mm)
    via_boxes = _grow_boxes(via_keepouts, VIA_KEEPOUT_MM)
    if via_boxes:
        keepoffs = list(keepoffs) + via_boxes
    segments = list(segments)        # escape stubs join it; the caller's list is not touched
    stubs = []                       # (net, layer, pad point, via point) of every stitch so far
    placed_vias = []
    stitched = {net: 0 for net in plane_nets}
    unresolved = []
    for fp in board.GetFootprints():
        left, top, right, bottom = boxes[fp.GetReference()]
        fx, fy = (left + right) / 2.0, (top + bottom) / 2.0
        half_w = max((right - left) / 2.0, 0.1)
        half_h = max((bottom - top) / 2.0, 0.1)
        for pad in fp.Pads():
            net = pad.GetNetname()
            if net not in plane_nets or pad.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
                continue
            px = pcbnew.ToMM(pad.GetPosition().x)
            py = pcbnew.ToMM(pad.GetPosition().y)
            layer = "B.Cu" if pad.IsOnLayer(pcbnew.B_Cu) else "F.Cu"
            found = _find_via_offset(px, py, fx, fy, half_w, half_h, net, obstacles,
                                     placed_vias, segments, keepoffs)
            if found is None and via_boxes and _default_in_box(px, py, via_boxes):
                found = _find_long_via_offset(px, py, fx, fy, half_w, half_h, net, layer, obstacles,
                                              placed_vias, segments, keepoffs, stubs)
                if found is not None:
                    segments.append((net, (px, py), found, TRACK_HALF_MM))
            if found is None:
                dx, dy = px - fx, py - fy
                if abs(dx) / half_w >= abs(dy) / half_h:
                    via_x, via_y = px + (1.0 if dx >= 0 else -1.0), py
                else:
                    via_x, via_y = px, py + (1.0 if dy >= 0 else -1.0)
                inside = any(_in_box(via_x, via_y, box) for box in via_boxes)
                unresolved.append("%s.%s [%s] at (%.3f, %.3f) -- kept at the 1.0 mm default%s"
                                   % (fp.GetReference(), pad.GetNumber(), net, px, py,
                                      ", inside a via keepout" if inside else ""))
            else:
                via_x, via_y = found
            placed_vias.append((via_x, via_y))
            stubs.append((net, layer, (px, py), (via_x, via_y)))
            kipcb.add_via(board, (via_x, via_y), net)
            kipcb.add_track(board, layer, 0.5, net, [(px, py), (via_x, via_y)])
            stitched[net] += 1
    return stitched, unresolved
