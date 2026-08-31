#!/usr/bin/env python3
"""pcbnew wrapper for the coupon layout generator. No coupon knowledge here.

Runs ONLY under KiCad's own Python (KIPY below); the system python has no
pcbnew module at all -- importing this file under it raises ImportError on
the `import pcbnew` line, which is the correct failure. Coordinates in the
public API are millimetres, y grows downward, origin at the board's top-left
corner; internal units (pcbnew's native nanometres) never cross this module's
boundary.

Two facts below came from a probe (10.0.5), not from the pcbnew docs, because
the docs and the installed API disagree on both:

- `BOARD()` defaults to 2 copper layers; `new_board()` always calls
  `SetCopperLayerCount()` explicitly rather than trusting the constructor.
- `FOOTPRINT.Flip()` segfaults if the footprint has not yet been added to a
  board (a parentless footprint has nowhere to look up the flip layer set).
  `add_part()` therefore calls `board.Add(fp)` BEFORE `fp.Flip(...)`, not
  after as a naive reading of the pcbnew API reference suggests.
"""
import os
import pcbnew

KICAD_ROOT = os.environ.get(
    "KICAD_ROOT", r"C:\Users\bernd\AppData\Local\Programs\KiCad\10.0")
KIPY = os.path.join(KICAD_ROOT, "bin", "python.exe")
FP_SHARE = os.path.join(KICAD_ROOT, "share", "kicad", "footprints")
FP_VENDORED = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib", "DaisyKiCad"))

LAYER = {"F.Cu": pcbnew.F_Cu, "In1.Cu": pcbnew.In1_Cu,
         "In2.Cu": pcbnew.In2_Cu, "B.Cu": pcbnew.B_Cu}


def _pt(x_mm, y_mm):
    return pcbnew.VECTOR2I(pcbnew.FromMM(x_mm), pcbnew.FromMM(y_mm))


def _net(board, name):
    """Existing net by name, or a freshly created and board-owned one.

    `board.FindNet(name)` is pcbnew's own lookup -- used instead of a
    module-level cache so nothing here outlives the BOARD it was found on.
    """
    net = board.FindNet(name)
    if net is None:
        net = pcbnew.NETINFO_ITEM(board, name)
        board.Add(net)
    return net


def new_board(width_mm, height_mm, copper_layers):
    """Fresh board: rectangular Edge.Cuts outline at (0,0)-(w,h), copper
    layer count set, design-rule minimums from the coupon plan's Global
    Constraints (track 0.25 mm, via 0.6 mm outer / 0.3 mm drill, clearance
    0.2 mm)."""
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(copper_layers)
    bds = board.GetDesignSettings()
    bds.m_TrackMinWidth = pcbnew.FromMM(0.25)
    bds.m_ViasMinSize = pcbnew.FromMM(0.6)
    bds.m_MinThroughDrill = pcbnew.FromMM(0.3)
    bds.m_MinClearance = pcbnew.FromMM(0.2)
    corners = [(0, 0), (width_mm, 0), (width_mm, height_mm), (0, height_mm)]
    for a, b in zip(corners, corners[1:] + corners[:1]):
        seg = pcbnew.PCB_SHAPE(board)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        seg.SetStart(_pt(*a))
        seg.SetEnd(_pt(*b))
        seg.SetLayer(pcbnew.Edge_Cuts)
        seg.SetWidth(pcbnew.FromMM(0.1))
        board.Add(seg)
    return board


def footprint(lib_id):
    """Load `"LibName:FootprintName"` from KiCad's share footprints dir, or
    the vendored `hardware/lib/DaisyKiCad/*.pretty` -- the same two-tier
    lookup as `netlist.load()`'s symbol-library search, one directory type
    down."""
    lib, _, name = lib_id.partition(":")
    for base in (FP_SHARE, FP_VENDORED):
        path = os.path.join(base, lib + ".pretty")
        if os.path.isdir(path):
            fp = pcbnew.FootprintLoad(path, name)
            if fp is not None:
                return fp
    raise FileNotFoundError(lib_id)


def add_part(board, part, x_mm, y_mm, rot_deg, side="F"):
    """Place one `netlist.Part`, wire every pad it named to a net, append it
    to the board. Parts with no footprint (the SM's two socket strips,
    `J_SM1`/`J_SM2`, and the `#FLG*` power-flag placeholders) carry no
    physical footprint by design -- skipped, `None` returned."""
    if not part.footprint:
        return None
    fp = footprint(part.footprint)
    fp.SetReference(part.ref)
    fp.SetValue(part.value)
    fp.SetPosition(_pt(x_mm, y_mm))
    board.Add(fp)                      # before Flip(): see module docstring
    if side == "B":
        fp.Flip(_pt(x_mm, y_mm), False)
    fp.SetOrientationDegrees(rot_deg)
    for pad in fp.Pads():
        net_name = part.nets.get(str(pad.GetNumber()))
        if net_name is not None:
            pad.SetNet(_net(board, net_name))
    return fp


def add_zone(board, layer_name, net_name, points_mm):
    zone = pcbnew.ZONE(board)
    zone.SetLayer(LAYER[layer_name])
    zone.SetNet(_net(board, net_name))
    chain = pcbnew.SHAPE_LINE_CHAIN()
    for x_mm, y_mm in points_mm:
        chain.Append(_pt(x_mm, y_mm))
    chain.SetClosed(True)
    zone.Outline().AddOutline(chain)
    board.Add(zone)
    return zone


def fill_zones(board):
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())


def add_track(board, layer_name, width_mm, net_name, points_mm):
    """One `PCB_TRACK` segment per consecutive pair in `points_mm`."""
    net = _net(board, net_name)
    layer = LAYER[layer_name]
    width = pcbnew.FromMM(width_mm)
    for (x1, y1), (x2, y2) in zip(points_mm, points_mm[1:]):
        track = pcbnew.PCB_TRACK(board)
        track.SetStart(_pt(x1, y1))
        track.SetEnd(_pt(x2, y2))
        track.SetWidth(width)
        track.SetLayer(layer)
        track.SetNet(net)
        board.Add(track)


def add_via(board, at_mm, net_name):
    """0.3 mm drill / 0.6 mm outer through via, the Global Constraints
    minimums -- this generator places no via larger than the minimum."""
    via = pcbnew.PCB_VIA(board)
    via.SetPosition(_pt(*at_mm))
    via.SetDrill(pcbnew.FromMM(0.3))
    via.SetWidth(pcbnew.FromMM(0.6))
    via.SetNet(_net(board, net_name))
    board.Add(via)


def save(board, path):
    pcbnew.SaveBoard(path, board)


def load(path):
    return pcbnew.LoadBoard(path)


def courtyard_boxes(board):
    """`{ref: (left, top, right, bottom)}` in mm, read back from the board.

    The front courtyard where a footprint has one, its own graphic/pad
    bounding box where it does not. The fallback is not hypothetical:
    `DAISY_PATCH_SM` carries no courtyard geometry at all, so KiCad's DRC
    cannot see it collide with anything and a caller that needs to reason
    about the module's footprint has to ask for its outline instead.
    """
    boxes = {}
    for fp in board.GetFootprints():
        bb = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
        if not bb.GetWidth() and not bb.GetHeight():
            bb = fp.GetBoundingBox(False, False)
        boxes[fp.GetReference()] = (
            pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop()),
            pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))
    return boxes


def board_nets(board):
    """`{net_name: [(ref, pad_number), ...]}` read back from the board --
    the same shape as `netlist.nets_from()`, so the two are directly
    comparable. Pads with no net (empty netname) are excluded."""
    nets = {}
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            name = pad.GetNetname()
            if name:
                nets.setdefault(name, []).append(
                    (fp.GetReference(), pad.GetNumber()))
    return nets
