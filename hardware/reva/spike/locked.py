#!/usr/bin/env python3
"""The strip's rule-bearing nets, hand-routed and locked (P4a spec §2.4).

SENSE_1 runs from the three mux COM pins to its port; OUT_L and OUT_R run
from their jack's tip to their port. All on B.Cu (the SMD side), no vias,
W_SIGNAL wide. Data, not a router: every corner was placed by hand against
the pad coordinates `run.py --where <net>` prints, and the proof's `locked`
step checks the routers leave every segment exactly where it is.

Endpoints are named, never numeric: ("pad", ref, number) resolves from the
built board, ("port", net) resolves to that net's port pad. Raw (x, y) is
used only for corners.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import pcbnew          # noqa: E402
import stripe as ST    # noqa: E402
from gen import kipcb  # noqa: E402

SENSE, OUT_L, OUT_R = ST.SENSE, ST.OUT_L, ST.OUT_R
W = ST.W_SIGNAL

# The COM pads' centres as pcbnew.ToMM returns them from the built board;
# `run.py --where SENSE_1` prints them rounded: U_MUX3.3 (224.491, 42.435),
# U_MUX4.3 (274.767, 42.413), U_MUX5.3 (255.968, 84.551). The corners that
# line up with a pad use the unrounded value, so the last segment into
# the pad is exactly 90 degrees.
COM3_X = 224.490714
COM4_X = 274.7675
COM5_Y = 84.550714
SENSE_Y = 56.26            # PORT10's row (`--where SENSE_1`)
TRUNK_Y = 64.0             # the free band between the RV54/RV62 tabs and RV64/RV66

# (net, layer, width_mm, [point, ...])
TRACKS = [
    # SENSE_1 trunk: east from the port along its own row, 45 degrees down
    # past D14 into the free band at y 64, east to under U_MUX4 and straight
    # up into its COM pad through the 6.7 mm gap between the RV62 and RV63
    # tabs.
    (SENSE, "B.Cu", W, [("port", SENSE), (COM3_X, SENSE_Y),
                        (COM3_X + (TRUNK_Y - SENSE_Y), TRUNK_Y), (COM4_X, TRUNK_Y),
                        ("pad", "U_MUX4", "3")]),
    # Branch to U_MUX3: straight up from the trunk's first corner, between
    # the RV55 and RV54 tabs, into the COM pad from outside its row.
    (SENSE, "B.Cu", W, [(COM3_X, SENSE_Y), ("pad", "U_MUX3", "3")]),
    # Branch to U_MUX5: south from the trunk at x 246 (between the RV66 tab
    # and RV68.3), then east along the COM pad's row into the mux's west
    # column from outside.
    (SENSE, "B.Cu", W, [(246.0, TRUNK_Y), (246.0, COM5_Y), ("pad", "U_MUX5", "3")]),
    # OUT_L: from J17's tip 45 degrees north-east into the J17-J18 gap, up
    # it at x 263.5 (2 mm clear of J17.TN and J17.S), then west along
    # PORT21's row (y 101.98) through the free band between the RV70 tabs
    # and the jack sleeves. Its vertical runs EAST of OUT_R's so the two
    # never cross: the port column puts OUT_L above OUT_R, the jacks put
    # J17 west of J18, so one of the pair has to pass round a tip pad.
    (OUT_L, "B.Cu", W, [("pad", "J17", "T"), (263.5, 115.72), (263.5, 101.98),
                        ("port", OUT_L)]),
    # OUT_R is the one that passes round a tip: south out of J18's tip,
    # west along y 121.0 under J17's tip (0.89 mm from the pad, 0.875 mm
    # from the outline; the board's edge rule is 0.5), up the J16-J17 gap
    # at x 254.5 (4.5 mm clear of either jack's pads), then west along
    # PORT22's row (y 104.52). Leaving J18 southward keeps it away from the
    # D17/R34 LED group east of the jack.
    (OUT_R, "B.Cu", W, [("pad", "J18", "T"), (271.8, 121.0), (254.5, 121.0),
                        (254.5, 104.52), ("port", OUT_R)]),
]


def _pad_xy(board, ref, number):
    fp = board.FindFootprintByReference(ref)
    if fp is None:
        raise KeyError("no footprint %s" % ref)
    for pad in fp.Pads():
        if str(pad.GetNumber()) == str(number):
            return pcbnew.ToMM(pad.GetPosition().x), pcbnew.ToMM(pad.GetPosition().y)
    raise KeyError("%s has no pad %s" % (ref, number))


def _resolve(strip, point):
    if point[0] == "pad":
        return _pad_xy(strip.board, point[1], point[2])
    if point[0] == "port":
        return _pad_xy(strip.board, strip.ports[point[1]], "1")
    return point


def geometry(board):
    out = []
    for t in board.GetTracks():
        if t.GetNetname() not in ST.LOCKED_NETS or t.Type() != pcbnew.PCB_TRACE_T:
            continue
        out.append((t.GetNetname(), t.GetLayerName(),
                    round(pcbnew.ToMM(t.GetStart().x), 3), round(pcbnew.ToMM(t.GetStart().y), 3),
                    round(pcbnew.ToMM(t.GetEnd().x), 3), round(pcbnew.ToMM(t.GetEnd().y), 3),
                    round(pcbnew.ToMM(t.GetWidth()), 3)))
    return sorted(out)


def apply(strip):
    for net, layer, width, points in TRACKS:
        kipcb.add_track(strip.board, layer, width, net,
                        [_resolve(strip, p) for p in points])
    n = kipcb.lock_tracks(strip.board, set(ST.LOCKED_NETS))
    strip.locked = geometry(strip.board)
    return n
