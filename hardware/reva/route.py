#!/usr/bin/env python3
"""P4-2: the placed Rev A board -> gen.route -> the routed board (spec §4.1).

    KIPY hardware/reva/route.py [--write] [--sabotage NAME] [--out DIR]

Placement is P4-1's `place.build()`, in memory. SMD pads on the plane nets
are stitched first (their vias are obstacles to the router). Every pad is an
obstacle owned by its net; netless pads (pot tabs, unused contacts) block
every net; footprint rule areas block their layers; each pot's body box
blocks F.Cu (spec §2.7). Every net with two or more pads is routed except
the plane nets. Every terminal carries its pad's copper shape, so a pad that
lies partly off the grid (the jack row's tip pads, spec §4.2.6) is still
reached. Victims and aggressors carry pair groups, U_SM's pad groups and the
jack zones (a victim pad with aggressor pads already within 10 mm) are
exemption zones, SENSE and audio route in earlier tiers. The rules are
judged by route_check.py on the saved board, never here."""
import argparse
import copy
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
for _p in (HW, HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)

import pcbnew               # noqa: E402
import place as P           # noqa: E402
import rules as RU          # noqa: E402
from gen import kipcb       # noqa: E402
from gen import place as PL  # noqa: E402
from gen import route as GR  # noqa: E402  (not `route`: this module is route.py)
from gen import stitch      # noqa: E402

OUT = P.OUT
COMMITTED = P.COMMITTED
DOCS = os.path.normpath(os.path.join(HW, "..", "docs", "hardware", "routing"))
LAYER_NAMES = ("F.Cu", "B.Cu")
AGGR = "aggressor"


def _mm(v):
    return pcbnew.ToMM(v)


class Routed:
    def __init__(self):
        self.board = None
        self.ids = {}
        self.front = []
        self.known = {}
        self.result = None
        self.widths = {}
        self.zones = []
        self.jack_zones = []
        self.stitched = {}
        self.unresolved = []
        self.seconds = 0.0
        self.skip_render = False

    def reload(self, path):
        new = Routed()
        for k, v in self.__dict__.items():
            if k not in ("board", "result"):
                setattr(new, k, copy.deepcopy(v))
        new.result = self.result          # read-only after the run
        new.board = kipcb.load(path)
        return new


def module_zones(board, margin):
    """One rect per group of U_SM pads whose centres lie within one pin pitch
    (2.54 mm) of a neighbour: the box of the group's pad centres grown by
    `margin` (spec §4.2.2; four groups on the P4-1 board)."""
    sm = board.FindFootprintByReference("U_SM")
    pts = sorted({(round(_mm(p.GetPosition().x), 4), round(_mm(p.GetPosition().y), 4))
                  for p in sm.Pads()})
    parent = list(range(len(pts)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for i, a in enumerate(pts):
        for j in range(i + 1, len(pts)):
            b = pts[j]
            if (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 <= (2.54 + 1e-3) ** 2:
                parent[find(i)] = find(j)
    groups = {}
    for i, p in enumerate(pts):
        groups.setdefault(find(i), []).append(p)
    return sorted((min(x for x, _ in g) - margin, min(y for _, y in g) - margin,
                   max(x for x, _ in g) + margin, max(y for _, y in g) + margin)
                  for g in groups.values())


def _gap(a, b):
    dx = max(b[0] - a[2], a[0] - b[2], 0.0)
    dy = max(b[1] - a[3], a[1] - b[3], 0.0)
    return (dx * dx + dy * dy) ** 0.5


def jack_zones(board, audio_mm, margin):
    """Spec §4.2.2 "Jack zones": for every victim pad outside U_SM with
    aggressor pads closer than `audio_mm` edge to edge (pad bounding boxes),
    the box of that victim pad and those aggressor pads grown by `margin`.
    Returns [(victim ref, pad, (l, t, r, b))] (two on the 2026-09-30 board:
    J18.T and J1.T)."""
    victims, aggr = [], []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            b = PL.box(pad.GetBoundingBox())
            if pad.GetNetname() in RU.VICTIMS and fp.GetReference() != "U_SM":
                victims.append((fp.GetReference(), str(pad.GetNumber()), b))
            elif pad.GetNetname() in RU.AGGRESSORS:
                aggr.append(b)
    out = []
    for ref, num, vb in sorted(victims):
        near = [ab for ab in aggr if _gap(vb, ab) < audio_mm]
        if near:
            boxes = [vb] + near
            out.append((ref, num, (min(q[0] for q in boxes) - margin, min(q[1] for q in boxes) - margin,
                                   max(q[2] for q in boxes) + margin, max(q[3] for q in boxes) + margin)))
    return out


def pot_refs(placed):
    """The front refs whose panel hole is a pot (probed 2026-09-30: 70)."""
    holes = P._hole_index()
    return sorted(r for r in placed.front if holes.get(placed.ids.get(r), {}).get("kind") == "pot")


def _group(net):
    if net in RU.VICTIMS:
        return net                      # one group per victim: the L/R rule pairs them
    if net in RU.AGGRESSORS:
        return AGGR
    return None


def _pad_rect(pad):
    bb = pad.GetBoundingBox()
    return ("rect", _mm(bb.GetLeft()), _mm(bb.GetTop()), _mm(bb.GetRight()), _mm(bb.GetBottom()))


def _pad_shape(pad, layer):
    """The pad's copper as a router shape (spec §4.2.6): a circle for a round
    pad, else its bounding box. Probed 2026-09-30 (10.0.5): GetShape(layer) /
    GetSize(layer) answer the same with and without the layer on this board
    (every padstack is uniform); a jack tip is a 2.13 mm circle, J_SD's pins
    1.2 mm circles, SMD pads roundrects. The circle's centre is the box
    centre, so a pad offset cannot move it off its copper."""
    if pad.GetShape(layer) == pcbnew.PAD_SHAPE_CIRCLE:
        _k, l, t, r, b = _pad_rect(pad)
        return ("circle", (l + r) / 2.0, (t + b) / 2.0, _mm(pad.GetSize(layer).x) / 2.0)
    return _pad_rect(pad)


def _router_input(s, placed):
    b = s.board
    inset = RU.EDGE_CLEAR + RU.SUPPLY_W / 2.0
    r = GR.Router((P.X0 + inset, P.Y0 + inset, P.X1 - inset, P.Y1 - inset),
                  RU.PITCH, len(LAYER_NAMES), RU.CLEARANCE, RU.VIA_D / 2.0, RU.VIA_COST)
    for v in RU.VICTIMS:
        r.pair_clearance(v, AGGR, RU.AUDIO_MM)
    for a, bb in RU.LR_PAIRS:
        r.pair_clearance(a, bb, RU.LR_MM)
    for z in s.zones:
        r.pair_exempt(z)
    for _ref, _num, z in s.jack_zones:
        r.pair_exempt(z)
    terms = {}
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            ls = tuple(i for i, n in enumerate(LAYER_NAMES) if pad.IsOnLayer(kipcb.LAYER[n]))
            net = pad.GetNetname() or None
            r.add_obstacle(net, ls, _pad_rect(pad), group=_group(net))
            if net and net not in RU.PLANE_NETS:
                shape = _pad_shape(pad, kipcb.LAYER[LAYER_NAMES[ls[0]]])
                terms.setdefault(net, []).append(
                    (_mm(pad.GetPosition().x), _mm(pad.GetPosition().y), ls, shape))
    for t in b.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            r.add_obstacle(t.GetNetname(), range(len(LAYER_NAMES)),
                           ("circle", _mm(t.GetPosition().x), _mm(t.GetPosition().y),
                            _mm(t.GetWidth(pcbnew.F_Cu)) / 2.0), group=_group(t.GetNetname()))
        elif t.Type() == pcbnew.PCB_TRACE_T and t.GetLayerName() in LAYER_NAMES:
            r.add_obstacle(t.GetNetname(), (LAYER_NAMES.index(t.GetLayerName()),),
                           ("seg", _mm(t.GetStart().x), _mm(t.GetStart().y),
                            _mm(t.GetEnd().x), _mm(t.GetEnd().y), _mm(t.GetWidth()) / 2.0),
                           group=_group(t.GetNetname()))
    for fp in b.GetFootprints():
        for z in fp.Zones():
            if z.GetIsRuleArea() and z.GetDoNotAllowTracks():
                ls = tuple(i for i, n in enumerate(LAYER_NAMES) if z.IsOnLayer(kipcb.LAYER[n]))
                bb = z.GetBoundingBox()
                r.add_obstacle(None, ls, ("rect", _mm(bb.GetLeft()), _mm(bb.GetTop()),
                                          _mm(bb.GetRight()), _mm(bb.GetBottom())))
    # Spec §4.2.8: no via within PLANE_THT_VIA_KEEPOFF of a plane-net THT
    # pad, so its thermal spokes keep their landing room. The router keeps a
    # via's copper `clearance` off a via-only shape, hence the shape is the
    # pad grown by the rest.
    grow = RU.PLANE_THT_VIA_KEEPOFF - RU.CLEARANCE
    for fp in b.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() in RU.PLANE_NETS and pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                sh = _pad_shape(pad, pcbnew.F_Cu)
                if sh[0] == "circle":
                    sh = ("circle", sh[1], sh[2], sh[3] + grow)
                else:
                    sh = ("rect", sh[1] - grow, sh[2] - grow, sh[3] + grow, sh[4] + grow)
                r.add_obstacle(None, range(len(LAYER_NAMES)), sh, via_only=True)
    # The grid's inset (edge clearance + a supply half-width) is short of a
    # via's radius: a via on the outermost cell sat 0.45 mm from the edge
    # (T1, 2026-09-30). A via-only band along the outline, EDGE_CLEAR -
    # CLEARANCE deep, keeps via copper EDGE_CLEAR off the edge without
    # moving the grid.
    band = RU.EDGE_CLEAR - RU.CLEARANCE
    for rect in ((P.X0 - 1.0, P.Y0 - 1.0, P.X1 + 1.0, P.Y0 + band),
                 (P.X0 - 1.0, P.Y1 - band, P.X1 + 1.0, P.Y1 + 1.0),
                 (P.X0 - 1.0, P.Y0 - 1.0, P.X0 + band, P.Y1 + 1.0),
                 (P.X1 - band, P.Y0 - 1.0, P.X1 + 1.0, P.Y1 + 1.0)):
        r.add_obstacle(None, range(len(LAYER_NAMES)), ("rect",) + rect, via_only=True)
    for ref in pot_refs(placed):
        l, t, rr, btm = PL.body_box(b.FindFootprintByReference(ref))
        r.add_obstacle(None, (LAYER_NAMES.index("F.Cu"),), ("rect", l, t, rr, btm))
    for net in sorted(terms):
        if len(terms[net]) < 2:
            continue
        w = RU.SUPPLY_W if net in RU.SUPPLY_TRACK_NETS else RU.SIGNAL_W
        s.widths[net] = w
        r.add_net(net, w / 2.0, terms[net], group=_group(net), tier=RU.tier_of(net))
    return r


def build():
    placed = P.build()
    s = Routed()
    s.board = placed.board
    s.ids, s.front = dict(placed.ids), list(placed.front)
    kipcb.set_netclasses(s.board, RU.SIGNAL_W, {"supply": (RU.SUPPLY_W, list(RU.SUPPLY_TRACK_NETS))},
                         clearance_mm=RU.CLEARANCE, via_mm=RU.VIA_D, drill_mm=RU.VIA_DRILL)
    s.zones = module_zones(s.board, RU.EXEMPT_MARGIN_MM)
    s.jack_zones = jack_zones(s.board, RU.AUDIO_MM, RU.EXEMPT_MARGIN_MM)
    s.stitched, s.unresolved = stitch.stitch_plane_pads(s.board, set(RU.PLANE_NETS), netless_blocks=True,
                                                        tht_keepoff_mm=RU.PLANE_THT_VIA_KEEPOFF)
    r = _router_input(s, placed)
    t0 = time.time()
    s.result = r.run(max_iters=RU.MAX_ITERS, pres0=RU.PRES0, pres_mult=RU.PRES_MULT,
                     hist_inc=RU.HIST_INC)
    s.seconds = round(time.time() - t0, 1)
    for net, geo in sorted(s.result.routes.items()):
        for layer, a, bb in geo["segments"]:
            kipcb.add_track(s.board, LAYER_NAMES[layer], s.widths[net], net, [a, bb])
        for xy in geo["vias"]:
            kipcb.add_via(s.board, xy, net)
    kipcb.fill_zones(s.board)
    return s


def save(s, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    kipcb.save(s.board, path)


def main(argv=None):
    import route_check as RC
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--sabotage", default="")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args(argv)
    s = build()
    res = s.result
    for ref, num, z in s.jack_zones:
        print("jack zone %s.%s (%s): %s" % (ref, num, s.ids.get(ref, ref),
                                             ", ".join("%.2f" % v for v in z)))
    print("router:nets %d failed %d %s conflicts %d iterations %d vias %d length %.1f mm seconds %.1f"
          % (len(s.widths), len(res.failed), res.failed, res.conflicts, res.iterations,
             sum(v["vias"] for v in res.stats.values()),
             sum(v["length_mm"] for v in res.stats.values()), s.seconds))
    if s.unresolved:
        print("stitch unresolved: %s" % "; ".join(s.unresolved))
    prefix = os.path.join(a.out, "reva-routed")
    pcb = prefix + ".kicad_pcb"
    if a.sabotage:
        save(s, pcb)
        s = s.reload(pcb)
        RC.sabotage(s, a.sabotage)
    save(s, pcb)
    print("wrote", os.path.relpath(pcb))
    green = RC.run(s, pcb, prefix)
    if a.write and not a.sabotage and not green:
        print("not copied: the run is RED")
    if a.write and not a.sabotage and green:
        shutil.copyfile(pcb, COMMITTED)
        print("copied to", os.path.relpath(COMMITTED))
        os.makedirs(DOCS, exist_ok=True)
        for side in ("top", "bottom"):
            png = "%s-%s.png" % (prefix, side)
            if os.path.exists(png):
                shutil.copyfile(png, os.path.join(DOCS, os.path.basename(png)))
        print("renders copied to", os.path.relpath(DOCS))
    print("GREEN" if green else "RED")
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
