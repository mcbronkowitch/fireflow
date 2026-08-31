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


def _stitch_plane_pads(board):
    """One via + 0.5 mm F.Cu track per SMD pad on a plane net, generated
    mechanically from the board's own pads rather than as hand data (brief
    step 2: `pad.GetNetname()` in the four plane nets and
    `pad.GetAttribute() == pcbnew.PAD_ATTRIB_SMD`). Through-hole pads on the
    same nets need nothing here -- the hole itself reaches every copper
    layer, In1/In2 included.

    Via placement: 1.0 mm from the pad centre, stepped straight out along
    whichever body axis (x or y) the pad sits *proportionally* closer to the
    edge of -- (dx / half-width) vs (dy / half-height) of the footprint's
    own courtyard box, not the raw (dx, dy) magnitudes. Tried first and
    wrong: raw-magnitude dominance picks the axis the pad happens to be
    furthest along in absolute terms, which for an oblong two-row IC is the
    *along-the-row* axis for any pin more than a package half-width from
    centre -- so the via walks parallel to the row, straight at the next
    pin. Confirmed as an actual `shorting_items` violation (GND via landing
    0.78 mm from `U_IN1` pin 16, `+3V3`) with the raw vector, and *still*
    shorting after switching to raw-magnitude axis snapping (GND via 0.27 mm
    from the same pin, because pin 13 sits further from the package's
    vertical centre than the half-width, so the wrong axis won both times).
    Normalizing each axis by the courtyard's own half-extent asks the
    question the geometry actually needs -- "is this pad nearer the
    package's left/right edge or its top/bottom edge" -- so a pin's step
    stays perpendicular to the row it is in, moving it away from every
    other pin on that row at once instead of along it. `JP_GND`/`JP_3V3`
    fall out of the same rule for free: their two pads straddle the moat on
    the y axis with a much taller courtyard than it is wide, so each pad's
    dominant normalized axis is y and the via lands in its own zone
    (digital pad north, analog pad south) -- what actually joins each
    domain pair when the jumper is closed. Both `GetPosition()` calls
    return absolute board coordinates, so no separate un-rotate step is
    needed for either axis choice.

    The 0.6 mm via / 0.3 mm drill come from `kipcb.add_via` (the Global
    Constraints minimums); the 0.5 mm track width is spec Section 4's supply
    width, used everywhere per the brief (0.25 mm AGND sense-side decouplers
    is unnecessary here).
    """
    boxes = kipcb.courtyard_boxes(board)
    stitched = {net: 0 for net in PLANE_NETS}
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
            dx, dy = px - fx, py - fy
            if abs(dx) / half_w >= abs(dy) / half_h:
                via_x, via_y = px + (1.0 if dx >= 0 else -1.0), py
            else:
                via_x, via_y = px, py + (1.0 if dy >= 0 else -1.0)
            kipcb.add_via(board, (via_x, via_y), net)
            kipcb.add_track(board, "F.Cu", 0.5, net, [(px, py), (via_x, via_y)])
            stitched[net] += 1
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
    kipcb.fill_zones(board)
    print("   stitched %s" % ", ".join(
        "%s:%d" % (net, n) for net, n in sorted(stitched.items())))
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


if __name__ == "__main__":
    board, parts = build()
    check_nets(board, parts)
    kipcb.save(board, PCB)
    print("wrote", PCB)
    check_courtyards(PCB)
    check_shadow(board)
    check_plane_connectivity(os.path.join(PROOF, "drc-placement.rpt"))
