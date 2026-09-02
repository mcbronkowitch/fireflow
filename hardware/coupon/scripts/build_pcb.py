#!/usr/bin/env python3
"""Build hardware/coupon/coupon.kicad_pcb and prove it. Abort on first red."""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D
import ksexp
import netlist as N
import placement as P
import routing as R
import kipcb
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
PCB = os.path.join(ROOT, "coupon.kicad_pcb")
PROOF = os.path.join(ROOT, "proof")

# The four plane nets, derived from placement.ZONE_RECTS rather than
# hand-duplicated -- GND/AGND on In1.Cu, +3V3/A+3V3 on In2.Cu. +-12V is not a
# plane (Task 4 routes it as a track) and is correctly absent from this set
# because it never appears as a ZONE_RECTS value.
PLANE_NETS = frozenset(net for (_layer, net) in P.ZONE_RECTS)


def fail(msg):
    print("RED:", msg)
    sys.exit(1)


# Keepout arithmetic for the collision search in _stitch_plane_pads(): every
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


def _clear_of_vias(x, y, placed):
    return all(((x - vx) ** 2 + (y - vy) ** 2) ** 0.5 >= VIA_VIA_MIN_MM
               for vx, vy in placed)


def _candidate_clear(px, py, vx, vy, net, obstacles, placed):
    """A candidate via is clear if the via point, and the midpoint of the
    straight pad->via track, both clear every different-net pad by their
    respective keepouts, and the via clears every via already placed this
    pass (any net -- hole-to-hole is a drilling constraint, not a net one).
    One midpoint sample is a coarse stand-in for the whole <=3 mm segment;
    the real arbiter is `check_stitch_hygiene()`'s kicad-cli gate, which
    this search only exists to satisfy on the first try."""
    if not _clear_of_vias(vx, vy, placed):
        return False
    if not _clear_of_pads(vx, vy, net, obstacles, PAD_KEEPOUT_MM):
        return False
    mx, my = (px + vx) / 2.0, (py + vy) / 2.0
    return _clear_of_pads(mx, my, net, obstacles, TRACK_KEEPOUT_MM)


def _find_via_offset(px, py, fx, fy, half_w, half_h, net, obstacles, placed):
    """Search for a clear via position, starting from the courtyard-
    normalized outward direction (see `_stitch_plane_pads()`) and widening
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
            if _candidate_clear(px, py, vx, vy, net, obstacles, placed):
                return vx, vy
    return None


def _stitch_plane_pads(board):
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
    """
    boxes = kipcb.courtyard_boxes(board)
    obstacles = _pad_obstacles(board)
    placed_vias = []
    stitched = {net: 0 for net in PLANE_NETS}
    unresolved = []
    for fp in board.GetFootprints():
        left, top, right, bottom = boxes[fp.GetReference()]
        fx, fy = (left + right) / 2.0, (top + bottom) / 2.0
        half_w = max((right - left) / 2.0, 0.1)
        half_h = max((bottom - top) / 2.0, 0.1)
        for pad in fp.Pads():
            net = pad.GetNetname()
            if net not in PLANE_NETS or pad.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
                continue
            px = pcbnew.ToMM(pad.GetPosition().x)
            py = pcbnew.ToMM(pad.GetPosition().y)
            found = _find_via_offset(px, py, fx, fy, half_w, half_h, net, obstacles, placed_vias)
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
            kipcb.add_track(board, "F.Cu", 0.5, net, [(px, py), (via_x, via_y)])
            stitched[net] += 1
    if unresolved:
        print("   UNRESOLVED via placements (collision search found nothing clear within 3.0 mm):")
        for line in unresolved:
            print("     " + line)
    return stitched


def build():
    parts = N.build()
    board = kipcb.new_board(D.BOARD_W_MM, D.BOARD_H_MM, D.LAYERS)
    placed = 0
    for part in parts:
        if not part.footprint:
            continue
        if part.ref not in P.PLACE:
            fail("no placement for %s" % part.ref)
        x, y, rot = P.PLACE[part.ref]
        kipcb.add_part(board, part, x, y, rot)
        placed += 1
    print("1. placed %d parts" % placed)
    for (layer, net), points in P.ZONE_RECTS.items():
        kipcb.add_zone(board, layer, net, points)
    stitched = _stitch_plane_pads(board)
    print("   stitched %s" % ", ".join(
        "%s:%d" % (net, n) for net, n in sorted(stitched.items())))
    # Signal routing goes down BEFORE the fill: the zone filler has to knit
    # around routing's vias (and around nothing else -- tracks live on F.Cu/B.Cu
    # where no zone is). Filling first and routing after would leave every
    # signal via sitting in solid plane copper of a foreign net.
    n_tracks, n_segments, n_vias = R.apply(board, kipcb)
    print("   routed %d tracks / %d segments, %d signal vias"
          % (n_tracks, n_segments, n_vias))
    kipcb.fill_zones(board)
    return board, parts


def check_nets(board, parts):
    """Board connectivity against netlist.build(), node for node.

    Compared as SETS of (ref, pin), not as lists: a footprint may carry the
    same pad number more than once -- SW_PUSH_6mm has two pads "1" and two
    pads "2", one pair per switch contact -- so the board read-back is a
    multiset where the schematic intent has one node per pin. Duplicated pad
    numbers are the same electrical node; anything else is a real difference
    and still shows up below.
    """
    want = {n: sorted(set(v)) for n, v in
            N.nets_from(parts, include_virtual=False).items()}
    got = {n: sorted(set(v)) for n, v in kipcb.board_nets(board).items()}
    if want != got:
        for n in sorted(set(want) | set(got)):
            if want.get(n) != got.get(n):
                print("  net %-20s want %s got %s" % (n, want.get(n), got.get(n)))
        fail("board nets do not match the intent")
    print("2. %d nets match the intent node for node" % len(want))


def check_courtyards(pcb_path):
    """Placement sanity: no two parts may share board area.

    Runs KiCad's own DRC rather than re-deriving courtyard polygons here --
    the same reason the schematic side exports a netlist instead of trusting
    its own generator. Only `courtyards_overlap` gates this step; the other
    violation classes are unrouted-board noise until Task 6 routes it, so
    they are counted and printed, not enforced.

    THE SM IS INVISIBLE TO THIS CHECK. `DAISY_PATCH_SM.kicad_mod` carries no
    courtyard geometry, so DRC cannot report the module overlapping anything
    -- it does not know the module occupies board area at all. A green step 3
    therefore says nothing about the largest part on the board, which is why
    `check_shadow()` exists beside it.

    The report is deleted before the run and its absence afterwards is the
    failure signal. It is a committed file, so testing `os.path.exists()` on
    a stale one would read the PREVIOUS run's verdict and print green for a
    `kicad-cli` that never produced anything. Return code cannot stand in for
    it: `--exit-code-violations` makes a nonzero rc the normal outcome here,
    since the board is deliberately unrouted until Task 4.
    """
    os.makedirs(PROOF, exist_ok=True)
    rpt = os.path.join(PROOF, "drc-placement.rpt")
    if os.path.exists(rpt):
        os.remove(rpt)
    r = subprocess.run([ksexp.KICAD_CLI, "pcb", "drc",
                        "--exit-code-violations", "--severity-error",
                        "--severity-warning", "-o", rpt, pcb_path],
                       capture_output=True, text=True)
    if not os.path.exists(rpt):
        fail("kicad-cli pcb drc wrote no report (rc=%d)\n%s"
             % (r.returncode, (r.stdout + r.stderr).strip()))
    txt = open(rpt, encoding="utf-8", errors="replace").read()
    kinds = {}
    for kind in re.findall(r"^\[([a-z0-9_]+)\]", txt, re.M):
        kinds[kind] = kinds.get(kind, 0) + 1
    overlaps = [ln.strip() for ln in txt.splitlines()
                if "courtyards_overlap" in ln]
    if overlaps:
        for ln in overlaps[:20]:
            print("  " + ln)
        fail("%d courtyard overlaps" % len(overlaps))
    print("3. 0 courtyard overlaps (other DRC classes, not gated here: %s)"
          % (", ".join("%s %d" % kv for kv in sorted(kinds.items())) or "none"))


def check_shadow(board):
    """Nothing may sit under the module.

    The Patch SM stands ~11 mm off the board on its sockets and its body
    covers `placement.SM_SHADOW`, so every pot shaft, connector, button,
    jumper and probe point has to be outside that rectangle. Step 3 cannot
    see this -- the module has no courtyard for DRC to collide with -- so the
    module's own outline is checked here as data instead. `U_SM` is exempt
    for the obvious reason.

    Courtyards, not centres: a part whose centre clears the rectangle by a
    millimetre while its body reaches 4 mm under the module is exactly the
    drift this guards against.
    """
    x0, y0, x1, y1 = P.SM_SHADOW
    bad = []
    for ref, (left, top, right, bottom) in sorted(
            kipcb.courtyard_boxes(board).items()):
        if ref == "U_SM":
            continue
        if not (right < x0 or left > x1 or bottom < y0 or top > y1):
            bad.append("%-9s occupies (%.2f,%.2f)-(%.2f,%.2f)"
                       % (ref, left, top, right, bottom))
    if bad:
        for line in bad[:20]:
            print("  " + line)
        fail("%d parts in the module's shadow (%.1f,%.1f)-(%.1f,%.1f)"
             % (len(bad), x0, y0, x1, y1))
    print("4. 0 parts in the module's shadow (%.1f,%.1f)-(%.1f,%.1f)"
          % (x0, y0, x1, y1))


def _unconnected_plane_pads(rpt_path):
    """`{net: {(ref, pad_number), ...}}` for the four plane nets, read out of
    kicad-cli's own DRC report.

    Probed against pcbnew 10.0.5, board loaded from the Task-2 artifact
    (zero zones, zero vias -- the RED case this checker exists for):

    - `board.GetConnectivity()` + `board.BuildConnectivity()` gives
      `conn.GetUnconnectedCount(True)` -> 183, a single board-wide scalar
      that matches the DRC report's `unconnected_items` count exactly, but
      carries no per-net breakdown.
    - `conn.GetNetItems(netcode, [pcbnew.PCB_PAD_T])` raises
      `TypeError: ... argument 3 of type 'std::vector< KICAD_T, ... >'` --
      this build's SWIG bindings never instantiate that vector template for
      Python, so there is no way to pass a type filter in.
    - `conn.RunOnUnconnectedEdges(callback)` raises the same class of error
      for `std::function<bool(CN_EDGE&)>` -- the callback overload is not
      reachable from Python either.
    - `conn.IsConnectedOnLayer(pad, layer)` *does* run (its `aTypes` argument
      has a default and can be omitted), but it answers a different
      question: whether the pad's own copper shape is present on that
      layer, not whether the pad is electrically joined to the plane there.
      An SMD pad correctly stitched into the GND plane by a via one layer
      away still reads `False` on `In1_Cu`, because the pad itself has no
      copper there -- confirmed by adding a real via + 0.5 mm track to
      `JP_GND` pad 1 in a scratchpad probe and rechecking.

    So none of `CONNECTIVITY_DATA`'s three per-item/per-net entry points
    gives "which pads on net X are still unconnected" from Python. kicad-cli
    itself already answers exactly that, one violation per missing
    connection, both endpoints' `[NETNAME]` and `of REF` tagged in the text
    -- `check_courtyards()` above regenerates this same report from the
    freshly saved board, so this function only re-parses it (the same
    "trust kicad-cli, do not re-derive its ratsnest" choice
    `check_courtyards`'s docstring makes for courtyard overlaps). It must
    therefore run after `check_courtyards()` in the same build, against the
    same `rpt_path`.
    """
    txt = open(rpt_path, encoding="utf-8", errors="replace").read()
    blocks = re.split(r"(?=^\[)", txt, flags=re.M)
    pad_re = re.compile(r"(?:PTH pad|Pad) (\S+) \[([^\]]+)\] of (\S+)")
    by_net = {}
    for block in blocks:
        if not block.startswith("[unconnected_items]"):
            continue
        for padnum, net, ref in pad_re.findall(block):
            if net in PLANE_NETS:
                by_net.setdefault(net, set()).add((ref, padnum))
    return by_net


def check_plane_connectivity(rpt_path):
    """Every pad on GND/AGND/+3V3/A+3V3 must reach its plane -- through its
    own hole (through-hole pads) or through a stitching via (SMD pads,
    `build()`'s job). See `_unconnected_plane_pads()` for the probed API
    choice behind how this is measured."""
    by_net = _unconnected_plane_pads(rpt_path)
    bad = {net: by_net.get(net, set()) for net in PLANE_NETS}
    if any(bad.values()):
        for net in sorted(PLANE_NETS):
            if bad[net]:
                print("  plane net %-6s %d unconnected pads: %s"
                      % (net, len(bad[net]),
                         ", ".join("%s.%s" % rp for rp in sorted(bad[net])[:10])))
        worst = max(bad, key=lambda n: len(bad[n]))
        fail("plane net %s has %d unconnected pads" % (worst, len(bad[worst])))
    print("5. plane nets fully connected (GND/AGND/+3V3/A+3V3)")


# Review round 1: the only copper anywhere on this still-unrouted board is
# Task 3's own zone fill plus its stitching vias/tracks -- Task 2's DRC
# baseline (recorded in the Task-2 report) had zero entries in every one of
# these four classes. A count that goes back above zero is this task's own
# regression, not "unrouted-board noise" the way `unconnected_items` and the
# silkscreen classes are -- those exist because Task 4/6 have not run yet;
# these exist only if the stitching itself is wrong.
#
# Task 4 added `tracks_crossing` to the same list, and earned it on its very
# first routing round: the opening +-12 V draft crossed -12 V over the +12 V
# lane at (86.0, 22.0) and DRC saw it, while step 3 was still filing the class
# under "not gated here". Two tracks of different nets sharing a point is a
# short on the finished board, not unrouted-board noise, and there is no
# reading of "the board is not routed yet" that makes one acceptable.
GATED_STITCH_CLASSES = ("shorting_items", "clearance", "hole_clearance",
                        "hole_to_hole", "tracks_crossing")


def check_stitch_hygiene(rpt_path):
    """Every class in `GATED_STITCH_CLASSES` -- `shorting_items`,
    `clearance`, `hole_clearance`, `hole_to_hole` and `tracks_crossing` --
    must be exactly zero in the same DRC report `check_courtyards()` and
    `check_plane_connectivity()` already read.

    This is the gate `_stitch_plane_pads()`'s own docstring argues for: two
    different via-direction heuristics each produced a real `shorting_items`
    violation before the courtyard-normalized-axis fix, and the collision
    search added afterward (`_find_via_offset()`) is a heuristic too --
    printing a count only helps if someone remembers to read it on every
    run. A hard gate is honest here specifically because the board is
    unrouted: nothing but this task's own copper exists yet to blame a
    violation on.
    """
    txt = open(rpt_path, encoding="utf-8", errors="replace").read()
    counts = {}
    for kind in re.findall(r"^\[([a-z0-9_]+)\]", txt, re.M):
        if kind in GATED_STITCH_CLASSES:
            counts[kind] = counts.get(kind, 0) + 1
    bad = {k: counts[k] for k in GATED_STITCH_CLASSES if counts.get(k)}
    if bad:
        for k in sorted(bad):
            print("  %s: %d" % (k, bad[k]))
        fail("stitching introduced %d DRC violations (%s)"
             % (sum(bad.values()), ", ".join(sorted(bad))))
    print("6. copper is clean: %s" % ", ".join("0 " + k for k in GATED_STITCH_CLASSES))


def check_ratsnest(board, rpt_path):
    """Nothing is left unrouted. Every pad of every net -- signal nets by
    `routing.TRACKS`, plane nets by their stitching vias -- has to be joined
    to the rest of its net.

    Measured, not read from a docstring: probed under pcbnew 10.0.5 on the
    board this generator builds,

        board.BuildConnectivity()
        board.GetConnectivity().GetUnconnectedCount(True)   -> 84

    against kicad-cli's own `[unconnected_items]` count of 84 in the same
    report -- the two agree exactly, on the pre-routing board and after every
    routing round since. `GetUnconnectedCount(False)` returns the same 84 here
    (the flag selects whether items with no net are visited at all, and this
    board has none), so the `True` form is kept as the one the DRC number was
    matched against.

    Both numbers are therefore available and both are printed; the pcbnew one
    gates, because it is read off the live board object rather than re-parsed
    out of a text file that a stale run could have left behind. The DRC number
    is printed beside it as the second opinion -- a silent divergence between
    them would mean the connectivity object and the saved file disagree, which
    is worth seeing rather than hiding behind one of the two.
    """
    board.BuildConnectivity()
    live = board.GetConnectivity().GetUnconnectedCount(True)
    txt = open(rpt_path, encoding="utf-8", errors="replace").read()
    from_drc = len(re.findall(r"^\[unconnected_items\]", txt, re.M))
    if live or from_drc:
        # The `[unconnected_items]` header line names neither net nor pad --
        # the two pads are on the two lines under it. Same block split as
        # `_unconnected_plane_pads()`, reported per net so a routing round can
        # see which nets it still owes rather than a wall of identical lines.
        by_net = {}
        pad_re = re.compile(r"(?:PTH pad|Pad) (\S+) \[([^\]]+)\] of (\S+)")
        for block in re.split(r"(?=^\[)", txt, flags=re.M):
            if not block.startswith("[unconnected_items]"):
                continue
            for padnum, net, ref in pad_re.findall(block):
                by_net.setdefault(net, set()).add("%s.%s" % (ref, padnum))
        for net in sorted(by_net):
            print("  %-20s %s" % (net, ", ".join(sorted(by_net[net]))))
        fail("%d unconnected pairs (pcbnew), %d (kicad-cli) -- the ratsnest "
             "is not empty" % (live, from_drc))
    print("7. ratsnest 0 -- every net routed (pcbnew and kicad-cli agree)")


if __name__ == "__main__":
    board, parts = build()
    check_nets(board, parts)
    kipcb.save(board, PCB)
    print("wrote", PCB)
    check_courtyards(PCB)
    check_shadow(board)
    check_plane_connectivity(os.path.join(PROOF, "drc-placement.rpt"))
    check_stitch_hygiene(os.path.join(PROOF, "drc-placement.rpt"))
    check_ratsnest(board, os.path.join(PROOF, "drc-placement.rpt"))
