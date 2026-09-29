"""Task 9 fix: fill islands and area of every zone on a saved board, read
from the stored fill (no refill)."""
import sys
import pcbnew

path = sys.argv[1]
b = pcbnew.LoadBoard(path)
print(path)
outline = b.GetBoardEdgesBoundingBox()
print("board bbox %.1f x %.1f mm = %.0f mm2" % (
    pcbnew.ToMM(outline.GetWidth()), pcbnew.ToMM(outline.GetHeight()),
    pcbnew.ToMM(outline.GetWidth()) * pcbnew.ToMM(outline.GetHeight())))
for z in b.Zones():
    for layer in z.GetLayerSet().Seq():
        polys = z.GetFilledPolysList(layer)
        n = polys.OutlineCount()
        holes = sum(polys.HoleCount(i) for i in range(n))
        area = polys.Area() / 1e12   # nm2 -> mm2
        print("   %-7s %-6s islands %2d  holes %3d  area %7.0f mm2" % (
            z.GetNetname(), b.GetLayerName(layer), n, holes, area))
