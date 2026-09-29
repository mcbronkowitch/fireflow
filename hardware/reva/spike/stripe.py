#!/usr/bin/env python3
"""The SENSE_1 strip of Rev A as a board to route (P4a spec §2).

Runs under KiCad's Python. Positions come from the P1 hole list and from the
footprints themselves; the only typed numbers are the spec's assumptions --
the outline, the port column, the part orientations -- and the coupon's
rules (clearance, widths, the 2.0 mm decoupling distance).

Throwaway harness: P4 builds its own placement. What is meant to outlive the
spike lives in hardware/gen/.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REVA = os.path.normpath(os.path.join(HERE, ".."))
HW = os.path.normpath(os.path.join(REVA, ".."))
for _p in (HW, REVA):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import pcbnew          # noqa: E402
import assign          # noqa: E402
import blocks as BL    # noqa: E402
import build as RB     # noqa: E402
import parts as RP     # noqa: E402
from gen import kipcb  # noqa: E402

SENSE = BL.SENSE[1]
OUT_L, OUT_R = BL.MODULE_PINS["B2"], BL.MODULE_PINS["B1"]
LOCKED_NETS = (SENSE, OUT_L, OUT_R)
SUPPLY_NETS = (BL.GND, BL.SM3V3)
X0, Y0, X1, Y1 = 203.0, 6.0, 300.0, 122.0   # spec §2.1 outline, an assumption
PORT_X = X0 + 1.5          # port pad (1.5 mm round) keeps 0.5 mm from the cut
PORT_PITCH = 2.54
PORT_ACCESS = 8.0          # a port slot needs this much keepout-free run eastward
EDGE_INSET = 1.0           # SMD courtyards stay this far inside the outline
CLEARANCE = 0.2            # the coupon's board minimum
W_SIGNAL, W_SUPPLY = 0.25, 0.4
DECOUPLE_MAX_MM = 2.0      # the coupon's check_layout rule 5
# Spec §2.2: pot pins NORTH (270). Probed while planning: pins south (90)
# puts five strip LEDs on pot pins (26 shorting_items before any routing);
# sideways fails too (0: RES_B crosses the cut; 180: C15 finds no place).
ROT = {"pot": 270, "jack": 0, "led": 0, "key": 0}
COPPER = {2: ("F.Cu", "B.Cu"), 4: ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu")}


class Strip:
    """The board plus the bookkeeping the proof needs."""

    def __init__(self, layers):
        self.board = None
        self.layers = layers
        self.parts = []        # every netlist Part on the board, ports included
        self.holes = {}        # ref -> (x, y) of the panel hole it must sit on
        self.keepouts = []     # (label, (l, t, r, b))
        self.ports = {}        # net -> port ref
        self.decouplers = {}   # cap ref -> mux ref
        self.vcc_pin = {}      # mux ref -> its VCC pad number
        self.led_nets = set()
        self.locked = []       # filled by locked.apply() (Task 3)
        self.unrouted_before_fill = None   # set by a routing method (Tasks 6-7)
        self.fill_nets = []                # nets filled after routing


def _box(bb):
    return (pcbnew.ToMM(bb.GetLeft()), pcbnew.ToMM(bb.GetTop()),
            pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))


def _grow(box, d):
    return (box[0] - d, box[1] - d, box[2] + d, box[3] + d)


def _overlaps(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def _rect(box):
    l, t, r, b = box
    return [(l, t), (r, t), (r, b), (l, b)]


def hole_point(fp):
    """Where the panel hole sits on this footprint: the centre of its one
    F.Fab circle, or its front courtyard centre when it has none (the Thonk
    key). Probed 2026-09-29: pot shaft, jack bore and LED body are each the
    footprint's only F.Fab circle."""
    circles = [g for g in fp.GraphicalItems()
               if hasattr(g, "GetShape") and g.GetShape() == pcbnew.SHAPE_T_CIRCLE
               and g.GetLayer() == pcbnew.F_Fab]
    if len(circles) > 1:
        raise ValueError("%s: %d F.Fab circles, expected one or none"
                         % (fp.GetReference(), len(circles)))
    if circles:
        c = circles[0].GetCenter()
        return pcbnew.ToMM(c.x), pcbnew.ToMM(c.y)
    l, t, r, b = _box(fp.GetCourtyard(pcbnew.F_CrtYd).BBox())
    return (l + r) / 2.0, (t + b) / 2.0


def _panel_rows(proj):
    """[(part, hole, kind)] for every panel part of a kind in ROT."""
    by_id = {}
    for h in assign.load_holes():
        for i in h.get("ids", [h["id"]]):
            by_id[i] = h
    rows = []
    for part in proj.parts():
        if not part.panel_id:
            continue
        h = by_id.get(part.panel_id)
        if h is None:
            raise ValueError("%s: panel id %s is not in the hole list"
                             % (part.ref, part.panel_id))
        if h["kind"] in ROT:
            rows.append((part, h, h["kind"]))
    return rows


def _survey(rows):
    """{ref: (anchor, [(pad, box)])} with every panel part placed at its hole
    on a scratch board. Must run before kipcb.new_board(): the scratch board
    draws UUIDs, and new_board() reseeds the generator afterwards."""
    scratch = pcbnew.BOARD()
    out = {}
    for part, h, kind in rows:
        fp = kipcb.add_part(scratch, part, 0.0, 0.0, ROT[kind])
        rx, ry = hole_point(fp)
        anchor = (h["x_mm"] - rx, h["y_mm"] - ry)
        fp.SetPosition(kipcb._pt(*anchor))
        out[part.ref] = (anchor, [(str(p.GetNumber()), _box(p.GetBoundingBox()))
                                  for p in fp.Pads()])
    return out


def _classify(survey):
    """own: every pad east of the cut. foreign: some pad reaches past the
    cut, some does not -> {ref: [(pad, box) reaching in]}. Anything else is
    left out."""
    own, foreign = set(), {}
    for ref, (_anchor, pads) in survey.items():
        inside = [pb for pb in pads if pb[1][0] >= X0]
        reaching = [pb for pb in pads if pb[1][2] > X0]
        if len(inside) == len(pads):
            own.add(ref)
        elif reaching:
            foreign[ref] = reaching
    return own, foreign


def _check_own(rows, own, panel_map):
    want = {p["id"] for p in panel_map["pots"] if p["sense"] == SENSE}
    got = {part.panel_id for part, _h, kind in rows
           if kind == "pot" and part.ref in own}
    if want != got:
        raise ValueError("strip pots differ from %s's: missing %s, extra %s"
                         % (SENSE, sorted(want - got), sorted(got - want)))


def _spiral(cx, cy, step, rmax):
    """Candidate centres in square rings around (cx, cy), nearest first
    inside each ring, in a fixed order."""
    yield cx, cy
    n = 1
    while n * step <= rmax:
        ring = ([(i, -n) for i in range(-n, n + 1)]
                + [(n, j) for j in range(-n + 1, n + 1)]
                + [(i, n) for i in range(n - 1, -n - 1, -1)]
                + [(-n, j) for j in range(n - 1, -n, -1)])
        ring.sort(key=lambda ij: (ij[0] ** 2 + ij[1] ** 2, ij))
        for i, j in ring:
            yield cx + i * step, cy + j * step
        n += 1


def _place_smd(board, part, target, blocked, step, rmax, accept=None):
    """Put `part` on the back at the first spiral position around `target`
    whose courtyard clears `blocked` and the outline inset, and that
    `accept(fp)` (if given) approves. Appends the courtyard to `blocked`."""
    fp = kipcb.add_part(board, part, target[0], target[1], 0, side="B")
    rel = {}
    for rot in (0, 90):
        fp.SetOrientationDegrees(rot)
        l, t, r, b = _box(fp.GetCourtyard(pcbnew.B_CrtYd).BBox())
        rel[rot] = (l - target[0], t - target[1], r - target[0], b - target[1])
    inner = (X0 + EDGE_INSET, Y0 + EDGE_INSET, X1 - EDGE_INSET, Y1 - EDGE_INSET)
    for x, y in _spiral(target[0], target[1], step, rmax):
        for rot in (0, 90):
            l, t, r, b = rel[rot]
            box = (x + l, y + t, x + r, y + b)
            if (box[0] < inner[0] or box[1] < inner[1]
                    or box[2] > inner[2] or box[3] > inner[3]):
                continue
            if any(_overlaps(box, o) for o in blocked):
                continue
            fp.SetOrientationDegrees(rot)
            fp.SetPosition(kipcb._pt(x, y))
            if accept is not None and not accept(fp):
                continue
            blocked.append(box)
            return fp
    raise ValueError("no free place for %s within %.1f mm of (%.2f, %.2f)"
                     % (part.ref, rmax, target[0], target[1]))


def _pad_xy(fp, number):
    for p in fp.Pads():
        if str(p.GetNumber()) == str(number):
            return pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)
    raise KeyError("%s has no pad %s" % (fp.GetReference(), number))


def _decouplers(proj, muxes):
    """{cap ref: mux ref}: in sheet mux_sense_1 each mux is followed by its
    100 nF (blocks.mux_region), SM_3V3 on pin 1, GND on pin 2."""
    sheet = [s for s in proj.sheets if s.name == "mux_" + SENSE.lower()][0]
    out, cur = {}, None
    for p in sheet.parts:
        if p.ref in muxes:
            cur = p.ref
        elif cur and p.nets == {"1": BL.SM3V3, "2": BL.GND}:
            out[p.ref], cur = cur, None
    if sorted(out.values()) != sorted(muxes):
        raise ValueError("decouplers %s do not cover muxes %s" % (out, muxes))
    return out


def _leaving_nets(proj, placed_refs):
    """Nets carried both by a placed part and by a board part outside the
    strip -- each of them needs a port."""
    inside, outside = set(), set()
    for p in proj.parts():
        if not p.footprint or not p.on_board:
            continue
        (inside if p.ref in placed_refs else outside).update(p.nets.values())
    return sorted(inside & outside)


def _assign_slots(srcs, slots):
    """Order-preserving assignment of ascending sources to ascending slots
    that minimises the total |dy| (dynamic programme, n x m)."""
    n, m = len(srcs), len(slots)
    inf = float("inf")
    cost = [[0.0] * (m + 1)] + [[inf] * (m + 1) for _ in range(n)]
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            cost[i][j] = min(cost[i][j - 1],
                             cost[i - 1][j - 1] + abs(srcs[i - 1] - slots[j - 1]))
    if cost[n][m] == inf:
        raise ValueError("%d ports, only %d free slots" % (n, m))
    out, j = [], m
    for i in range(n, 0, -1):
        while cost[i][j] == cost[i][j - 1]:
            j -= 1
        out.append(slots[j - 1])
        j -= 1
    return out[::-1]


def _port_rows(board, nets, keepouts):
    """[(net, y)] in the port column: ports in the order of their net's mean
    pad y, each on a 2.54 mm slot whose eastward run of PORT_ACCESS mm is
    clear of every keepout."""
    src = {}
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() in nets:
                src.setdefault(pad.GetNetname(), []).append(
                    pcbnew.ToMM(pad.GetPosition().y))
    order = sorted(nets, key=lambda n: (sum(src[n]) / len(src[n]), n))
    slots, y = [], Y0 + EDGE_INSET + 1.0
    while y <= Y1 - EDGE_INSET - 1.0:
        band = (X0, y - 1.0, X0 + PORT_ACCESS, y + 1.0)
        if not any(_overlaps(band, k) for _label, k in keepouts):
            slots.append(y)
        y = round(y + PORT_PITCH, 3)
    ys = _assign_slots([sum(src[n]) / len(src[n]) for n in order], slots)
    return list(zip(order, ys))


def build(layers=2):
    proj = RB.project()
    pm = RB.load_panel_map()
    rows = _panel_rows(proj)
    survey = _survey(rows)                       # before new_board()
    own, foreign = _classify(survey)
    _check_own(rows, own, pm)

    s = Strip(layers)
    board = s.board = kipcb.new_board(X1 - X0, Y1 - Y0, layers, origin=(X0, Y0))
    if layers == 4:
        rect = _rect((X0, Y0, X1, Y1))
        kipcb.add_zone(board, "In1.Cu", SUPPLY_NETS[0], rect)   # GND
        kipcb.add_zone(board, "In2.Cu", SUPPLY_NETS[1], rect)   # SM_3V3
    by_ref = {p.ref: p for p in proj.parts()}

    tht = []
    leds = []
    for part, h, kind in rows:
        if part.ref not in own:
            continue
        anchor, pads = survey[part.ref]
        kipcb.add_part(board, part, anchor[0], anchor[1], ROT[kind])
        s.parts.append(part)
        s.holes[part.ref] = (h["x_mm"], h["y_mm"])
        tht += [_grow(box, CLEARANCE) for _n, box in pads]
        if kind == "led":
            leds.append(part)

    for ref in sorted(foreign):
        for number, box in foreign[ref]:
            k = _grow(box, CLEARANCE)
            k = (max(k[0], X0), k[1], k[2], k[3])
            kipcb.add_keepout(board, _rect(k), COPPER[layers])
            s.keepouts.append(("%s.%s" % (ref, number or "tab"), k))

    blocked = tht + [k for _label, k in s.keepouts]
    muxes = ["U_MUX%d" % m for m in pm["muxes"][SENSE]]
    for mref in muxes:
        m = int(mref[len("U_MUX"):])
        shafts = [(p["x_mm"], p["y_mm"]) for p in pm["pots"] if p["mux"] == m]
        target = (sum(x for x, _ in shafts) / len(shafts),
                  sum(y for _, y in shafts) / len(shafts))
        _place_smd(board, by_ref[mref], target, blocked, step=0.5, rmax=30.0)
        s.parts.append(by_ref[mref])

    s.decouplers = _decouplers(proj, muxes)
    for cref, mref in sorted(s.decouplers.items()):
        s.vcc_pin[mref] = str(by_ref[mref].sym.by_name("VCC"))
        vcc = _pad_xy(board.FindFootprintByReference(mref), s.vcc_pin[mref])

        def near_vcc(fp, vcc=vcc):
            x, y = _pad_xy(fp, 1)
            return ((x - vcc[0]) ** 2 + (y - vcc[1]) ** 2) ** 0.5 <= DECOUPLE_MAX_MM

        _place_smd(board, by_ref[cref], vcc, blocked, step=0.1, rmax=3.0,
                   accept=near_vcc)
        s.parts.append(by_ref[cref])

    anode = {d.nets[str(d.sym.by_name("A"))]: d for d in leds}
    for p in sorted(proj.parts(), key=lambda p: p.ref):
        d = anode.get(p.nets.get("2")) if p.ref.startswith("R") else None
        if d is None:
            continue
        target = s.holes[d.ref]
        _place_smd(board, p, target, blocked, step=0.25, rmax=10.0)
        s.parts.append(p)
        s.led_nets.update(p.nets.values())

    for k, (net, y) in enumerate(_port_rows(board, _leaving_nets(
            proj, {p.ref for p in s.parts}), s.keepouts), 1):
        port = RP.make("tp", "PORT%d" % k).by_number(1, net)
        kipcb.add_part(board, port, PORT_X, y, 0, side="B")
        s.parts.append(port)
        s.ports[net] = port.ref

    kipcb.set_netclasses(board, W_SIGNAL, {"Supply": (W_SUPPLY, list(SUPPLY_NETS))})
    return s


def intent(strip):
    """{net: {(ref, pad), ...}} the board must carry: every placed part's
    pins, ports included. Pin numbers equal pad numbers for every footprint
    in the strip (probed 2026-09-29)."""
    out = {}
    for part in strip.parts:
        for pin, net in part.nets.items():
            out.setdefault(net, set()).add((part.ref, pin))
    return out
