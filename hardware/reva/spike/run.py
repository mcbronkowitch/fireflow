#!/usr/bin/env python3
"""P4a routing spike: build the SENSE_1 strip, route it, prove it.

    KIPY hardware/reva/spike/run.py --method none|own|freerouting --layers 2|4 [--sabotage NAME]

Writes hardware/reva/spike/out/<method>-<layers>L.* (gitignored). Exit 0 only
when every gated proof step is green.
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import pcbnew          # noqa: E402
import freerouting as FR  # noqa: E402
import locked as LK   # noqa: E402
import own as OWN     # noqa: E402
import proof as PF    # noqa: E402
import stripe as ST   # noqa: E402
from gen import kipcb  # noqa: E402
from gen import pcb_proof as PP  # noqa: E402
from gen import stitch as STITCH  # noqa: E402

OUT = os.path.join(HERE, "out")


def finish(s):
    """After any routing method: record what it left unrouted, then the
    2-layer GND fill on both sides (spec §2.5), then fill all zones.

    On 4 layers the supply nets are not routed at all: their pads reach the
    In1/In2 planes (through holes and stitching vias), which are part of the
    design, not a rescue. So the count is taken after the planes are filled
    -- there is no outer fill on 4 layers that could bridge a signal net.
    Task 8 run 1 counted before the plane fill and got 75, every one of them
    a supply connection the planes carry by design; that raw number is still
    kept in `s.unrouted_before_planes` and printed."""
    if s.layers == 4:
        s.unrouted_before_planes = PP.live_unconnected(s.board)
        kipcb.fill_zones(s.board)
    s.unrouted_before_fill = PP.live_unconnected(s.board)
    if s.layers == 2:
        rect = [(ST.X0, ST.Y0), (ST.X1, ST.Y0), (ST.X1, ST.Y1), (ST.X0, ST.Y1)]
        for layer in ("F.Cu", "B.Cu"):
            kipcb.add_zone(s.board, layer, ST.SUPPLY_NETS[0], rect)
        s.fill_nets = [ST.SUPPLY_NETS[0]]
    kipcb.fill_zones(s.board)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--method", choices=("none", "own", "freerouting"), required=True)
    ap.add_argument("--layers", type=int, choices=(2, 4), default=2)
    ap.add_argument("--sabotage", default="")
    ap.add_argument("--where", default="")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    prefix = os.path.join(OUT, "%s-%dL" % (a.method, a.layers))

    t0 = time.time()
    s = ST.build(a.layers)
    print("built the strip: %d parts, %d keepouts, %d ports in %.1f s"
          % (len(s.parts), len(s.keepouts), len(s.ports), time.time() - t0))
    if a.where:
        for fp in s.board.GetFootprints():
            for pad in fp.Pads():
                if pad.GetNetname() == a.where:
                    layers = "/".join(n for n in ("F.Cu", "B.Cu")
                                      if pad.IsOnLayer(kipcb.LAYER[n]))
                    print("   %-8s %-3s (%.3f, %.3f) %s" % (
                        fp.GetReference(), pad.GetNumber(),
                        pcbnew.ToMM(pad.GetPosition().x),
                        pcbnew.ToMM(pad.GetPosition().y), layers))
        print("   port:", s.ports.get(a.where, "none"))
        return 0
    print("locked %d items on %s" % (LK.apply(s), ", ".join(ST.LOCKED_NETS)))
    if a.layers == 4:
        segs = [(g[0], (g[2], g[3]), (g[4], g[5]), g[6] / 2.0) for g in s.locked]
        # netless_blocks: the pots' mounting tabs are netless PTH pads (run 1:
        # two GND vias landed on RV52's and RV60's tab).
        stitched, unresolved = STITCH.stitch_plane_pads(s.board, set(ST.SUPPLY_NETS), segs,
                                                        netless_blocks=True)
        print("stitched %s; unresolved %d" % (stitched, len(unresolved)))
        for line in unresolved:
            print("   " + line)
    for label, box in s.keepouts:
        print("   keepout %-10s x %.2f..%.2f y %.2f..%.2f" % (label, box[0], box[2], box[1], box[3]))
    routed = a.method != "none"
    if a.method == "own":
        print("own router:", OWN.route(s))
    if a.method == "freerouting":
        print("freerouting:", FR.route(s, prefix))
    if routed:
        finish(s)
        print("unrouted before fill: %d" % s.unrouted_before_fill)
        if a.layers == 4:
            print("   (4 layers: counted after the plane fill; %d before it)"
                  % s.unrouted_before_planes)
    if a.sabotage:
        PF.sabotage(s, a.sabotage)
    pcb = prefix + ".kicad_pcb"
    kipcb.save(s.board, pcb)
    print("wrote", os.path.relpath(pcb))
    t1 = time.time()
    green = PF.run(s, pcb, prefix, routed)
    print("proof took %.1f s -- %s" % (time.time() - t1, "GREEN" if green else "RED"))
    return 0 if green else 1


if __name__ == "__main__":
    sys.exit(main())
