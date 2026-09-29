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

OUT = os.path.join(HERE, "out")


def finish(s):
    """After any routing method: record what it left unrouted, then the
    2-layer GND fill on both sides (spec §2.5), then fill all zones."""
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
