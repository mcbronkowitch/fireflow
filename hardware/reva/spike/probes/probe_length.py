"""Task 9 fix: track length and segment count on a saved board, split into
supply (GND, SM_3V3), locked (SENSE_1, OUT_L, OUT_R) and the rest; vias per
group."""
import sys
import pcbnew

SUPPLY = {"GND", "SM_3V3"}
LOCKED = {"SENSE_1", "OUT_L", "OUT_R"}


def group(n):
    return "supply" if n in SUPPLY else "locked" if n in LOCKED else "signal"


b = pcbnew.LoadBoard(sys.argv[1])
print(sys.argv[1])
seg, mm, vias = {}, {}, {}
for t in b.GetTracks():
    g = group(t.GetNetname())
    if t.GetClass() == "PCB_VIA":
        vias[g] = vias.get(g, 0) + 1
        continue
    seg[g] = seg.get(g, 0) + 1
    mm[g] = mm.get(g, 0.0) + pcbnew.ToMM(t.GetLength())
for g in ("signal", "supply", "locked"):
    print("   %-6s segments %4d  length %7.1f mm  vias %d" % (
        g, seg.get(g, 0), mm.get(g, 0.0), vias.get(g, 0)))
