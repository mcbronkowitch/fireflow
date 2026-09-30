#!/usr/bin/env python3
"""Plane stitching shared by the generated boards: one via and a short track
per SMD pad on a plane net, placed by a collision search. Moved from the
coupon's build_pcb.py (P4a); the docstrings keep the coupon's probe history.
Runs under KiCad's Python.

Known limits (P4a review)
-------------------------
- The collision search ignores rule areas (keepouts).
- The `segments` check tests only the via point and the track midpoint,
  ignores layers, and has never been shown rejecting a candidate.
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


NETLESS = "<no net>"   # stands in for "" when netless pads block (netless_blocks=True)


def stitch_plane_pads(board, plane_nets, segments=(), netless_blocks=False,
                      tht_keepoff_mm=None):
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
    """
    boxes = kipcb.courtyard_boxes(board)
    obstacles = _pad_obstacles(board)
    if netless_blocks:
        obstacles = [(onet or NETLESS, box) for onet, box in obstacles]
    keepoffs = () if tht_keepoff_mm is None else _tht_keepoffs(board, plane_nets, tht_keepoff_mm)
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
            found = _find_via_offset(px, py, fx, fy, half_w, half_h, net, obstacles,
                                     placed_vias, segments, keepoffs)
            if found is None:
                dx, dy = px - fx, py - fy
                if abs(dx) / half_w >= abs(dy) / half_h:
                    via_x, via_y = px + (1.0 if dx >= 0 else -1.0), py
                else:
                    via_x, via_y = px, py + (1.0 if dy >= 0 else -1.0)
                unresolved.append("%s.%s [%s] at (%.3f, %.3f) -- kept at the 1.0 mm default"
                                   % (fp.GetReference(), pad.GetNumber(), net, px, py))
            else:
                via_x, via_y = found
            placed_vias.append((via_x, via_y))
            kipcb.add_via(board, (via_x, via_y), net)
            layer = "B.Cu" if pad.IsOnLayer(pcbnew.B_Cu) else "F.Cu"
            kipcb.add_track(board, layer, 0.5, net, [(px, py), (via_x, via_y)])
            stitched[net] += 1
    return stitched, unresolved
