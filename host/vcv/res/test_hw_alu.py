"""Guard for the Rev A P5 aluminium plate files (gen_hw_alu.py): the hole DXF
read back with its own parser against the hole list the board is placed from,
the drilled webs, and the print PDF's bled page size and content.

No pytest in this environment -- plain checks, exit code says it all.
Run from the repo root:  python host/vcv/res/test_hw_alu.py
"""
import json, math, os, re, sys
import gen_hw_panel as hw
import gen_hw_alu as alu

HERE = os.path.dirname(os.path.abspath(__file__))
FAILS = []
PT_PER_MM = 72 / 25.4
# Least aluminium between two drilled hole edges. The tightest pair (a
# satellite lamp beside its jack) has 2.0 mm with the drilled diameters.
MIN_WEB = 1.5


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def _read(name):
    with open(os.path.join(HERE, name), "rb") as f:
        return f.read()


def _dxf_entities():
    """Every LINE/ARC/CIRCLE in the committed DXF as {group code: value}."""
    lines = [l.strip() for l in _read("FireflowHW-alu-holes.dxf").decode("ascii").splitlines()]
    pairs = list(zip(lines[0::2], lines[1::2]))
    start = pairs.index(("2", "ENTITIES"))
    ents, cur = [], None
    for code, val in pairs[start + 1:]:
        if code == "0":
            if cur:
                ents.append(cur)
            cur = {"type": val} if val in ("LINE", "ARC", "CIRCLE") else None
        elif cur is not None:
            cur[code] = val if code == "8" else float(val)
    return ents


def _board_holes():
    """The committed hole list -- the board's placement source."""
    with open(os.path.join(HERE, "FireflowHW-holes.json"), encoding="utf-8") as f:
        doc = json.load(f)
    return doc["plate"], doc["holes"]


def test_committed_files_match_the_generator():
    for name, data in alu.outputs():
        check(_read(name) == data, f"{name} is stale -- run gen_hw_alu.py")


def test_dxf_circles_are_the_board_holes_at_drilled_size():
    plate, holes = _board_holes()
    circles = [e for e in _dxf_entities() if e["type"] == "CIRCLE"]
    round_ = [h for h in holes if "d_mm" in h]
    check(len(circles) == len(round_),
          f"{len(circles)} DXF circles for {len(round_)} round holes")
    for h in round_:
        x, y = h["x_mm"] - plate["x0_mm"], plate["h_mm"] - h["y_mm"]
        r = alu.ALU_D[h["kind"]] / 2
        hit = [c for c in circles if abs(c["10"] - x) < 1e-3 and abs(c["20"] - y) < 1e-3]
        check(len(hit) == 1, f"{h['id']}: {len(hit)} DXF circles at ({x:.3f}, {y:.3f})")
        if hit:
            check(abs(hit[0]["40"] - r) < 1e-4,
                  f"{h['id']}: radius {hit[0]['40']} != {r} ({h['kind']})")
            check(hit[0]["8"] == "HOLES", f"{h['id']}: on layer {hit[0]['8']}")


def test_dxf_slots_and_outline():
    plate, holes = _board_holes()
    ents = _dxf_entities()
    slots = [e for e in ents if e["8"] == "SLOTS"]
    outline = [e for e in ents if e["8"] == "PLATE_OUTLINE"]
    # 4 stadiums (2 lines + 4 arcs each) and the SD rectangle (4 lines).
    check(sum(e["type"] == "ARC" for e in slots) == 16, "mount slots: arc count")
    check(sum(e["type"] == "LINE" for e in slots) == 12, "slots: line count")
    for h in holes:
        if "d_mm" in h:
            continue
        cx, cy = h["x_mm"] - plate["x0_mm"], plate["h_mm"] - h["y_mm"]
        near = [e for e in slots
                if abs(e["10"] - cx) <= h["w_mm"] / 2 + 1e-6
                and abs(e["20"] - cy) <= h["h_mm"] / 2 + 1e-6]
        check(len(near) in (4, 6), f"{h['id']}: {len(near)} DXF edges, expected its own outline")
    xs = [e[k] for e in outline for k in ("10", "11")]
    ys = [e[k] for e in outline for k in ("20", "21")]
    check((min(xs), max(xs), min(ys), max(ys)) == (0.0, plate["w_mm"], 0.0, plate["h_mm"]),
          f"outline {min(xs)}..{max(xs)} x {min(ys)}..{max(ys)}")


def test_every_drilled_web_holds():
    hs = [h for h in alu.holes() if "d_mm" in h]
    worst = min((math.hypot(a["x_mm"] - b["x_mm"], a["y_mm"] - b["y_mm"])
                 - a["d_mm"] / 2 - b["d_mm"] / 2, a["id"], b["id"])
                for i, a in enumerate(hs) for b in hs[i + 1:])
    check(worst[0] >= MIN_WEB, f"web {worst[1]}/{worst[2]} {worst[0]:.2f} mm < {MIN_WEB}")


def _pdf_page(name):
    import fitz
    doc = fitz.open(os.path.join(HERE, name))
    return doc, doc[0]


def test_print_pdf_is_the_bled_plate_in_outlined_vectors():
    name = "FireflowHW-alu-print.pdf"
    doc, page = _pdf_page(name)
    w, h = page.rect.width / PT_PER_MM, page.rect.height / PT_PER_MM
    check(len(doc) == 1, f"{name}: {len(doc)} pages")
    check(abs(w - (alu.PW + 2 * alu.BLEED)) < 0.01 and abs(h - (alu.PH + 2 * alu.BLEED)) < 0.01,
          f"{name}: page {w:.3f} x {h:.3f} mm, want the plate plus {alu.BLEED} mm bleed")
    check(not page.get_fonts(), f"{name}: carries fonts -- text must be outlines")
    check(not page.get_images(), f"{name}: carries raster images")
    plate = alu._hex(alu.PLATE_MID)
    covers = [d for d in page.get_drawings() if d.get("fill") is not None
              and _hexfill(d["fill"]) == plate
              and d["rect"].contains(page.rect + (0.01, 0.01, -0.01, -0.01))]
    check(len(covers) == 1, f"{name}: no plate-colour fill ({plate}) over the whole bled page")


def _hexfill(f):
    return "#" + "".join(f"{round(v * 255):02x}" for v in f)


def test_print_carries_every_caption_and_field_ink():
    """Every word in the plate's SVG is a filled path in the PDF (counted by
    its fill), and each zone's field ink is used."""
    words = re.findall(r"<text [^>]*fill=\"([^\"]+)\"[^>]*>[^<]*</text>", hw.svg())
    name = "FireflowHW-alu-print.pdf"
    _, page = _pdf_page(name)
    fills = {}
    for d in page.get_drawings():
        if d.get("fill") is not None:
            c = _hexfill(d["fill"])
            fills[c] = fills.get(c, 0) + 1
    for side in sorted(hw.ACC):
        check(fills.get(alu.field_ink(side), 0) > 0, f"{name}: no field in {alu.field_ink(side)}")
    for col in set(words):
        check(fills.get(col.lower(), 0) >= words.count(col),
              f"{name}: {fills.get(col.lower(), 0)} paths in {col}, {words.count(col)} words")


def main():
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print(f"FAIL ({len(FAILS)}):")
        for f in FAILS:
            print("  -", f)
        return 1
    print("PASS -- hw alu guards ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
