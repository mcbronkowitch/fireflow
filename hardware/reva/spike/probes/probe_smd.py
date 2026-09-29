"""Task 9 probe: where the SMD placement search put each back-side part,
relative to the point it started from, and the port order for OUT_L/OUT_R."""
import os
import sys

SPIKE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, SPIKE)
import pcbnew  # noqa
import stripe as ST  # noqa

s = ST.build(2)
b = s.board
pm = ST.RB.load_panel_map()


def pos(fp):
    p = fp.GetPosition()
    return pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)


def d(a, c):
    return ((a[0] - c[0]) ** 2 + (a[1] - c[1]) ** 2) ** 0.5


print("SMD part  target           placed           walked  rot")
muxes = sorted(set(s.decouplers.values()))
for mref in muxes:
    m = int(mref[len("U_MUX"):])
    sh = [(p["x_mm"], p["y_mm"]) for p in pm["pots"] if p["mux"] == m]
    t = (sum(x for x, _ in sh) / len(sh), sum(y for _, y in sh) / len(sh))
    fp = b.FindFootprintByReference(mref)
    q = pos(fp)
    print("%-8s (%7.2f,%6.2f) (%7.2f,%6.2f) %6.2f  %d" % (mref, t[0], t[1], q[0], q[1], d(t, q), fp.GetOrientationDegrees()))
for cref, mref in sorted(s.decouplers.items()):
    fm = b.FindFootprintByReference(mref)
    vcc = ST._pad_xy(fm, s.vcc_pin[mref])
    fp = b.FindFootprintByReference(cref)
    q = pos(fp)
    p1 = ST._pad_xy(fp, 1)
    print("%-8s (%7.2f,%6.2f) (%7.2f,%6.2f) %6.2f  %d  pad1-to-VCC %.3f" % (cref, vcc[0], vcc[1], q[0], q[1], d(vcc, q), fp.GetOrientationDegrees(), d(vcc, p1)))
led_nets = {}
for fp in b.GetFootprints():
    ref = fp.GetReference()
    if ref.startswith("R") and not ref.startswith("RV") and fp.GetLayer() == pcbnew.B_Cu:
        # the LED it belongs to: the LED whose hole is nearest the search start
        best = None
        for dref, h in s.holes.items():
            if dref.startswith("D"):
                pads = {p.GetNetname() for p in fp.Pads()}
                dfp = b.FindFootprintByReference(dref)
                if pads & {p.GetNetname() for p in dfp.Pads() if p.GetNetname() != "GND"}:
                    best = (dref, h)
        q = pos(fp)
        print("%-8s (%7.2f,%6.2f) (%7.2f,%6.2f) %6.2f  %d  (LED %s)" % (ref, best[1][0], best[1][1], q[0], q[1], d(best[1], q), fp.GetOrientationDegrees(), best[0]))

print()
print("port order source (mean pad y) for the locked audio nets and neighbours:")
src = {}
for fp in b.GetFootprints():
    if fp.GetReference().startswith("PORT"):
        continue
    for pad in fp.Pads():
        n = pad.GetNetname()
        if n in s.ports:
            src.setdefault(n, []).append(pcbnew.ToMM(pad.GetPosition().y))
rows = []
for n, ref in s.ports.items():
    fp = b.FindFootprintByReference(ref)
    rows.append((pos(fp)[1], ref, n, sum(src[n]) / len(src[n]), len(src[n])))
for y, ref, n, mean, k in sorted(rows):
    print("  %-7s y %7.2f  %-12s mean source y %8.3f over %d pads" % (ref, y, n, mean, k))
