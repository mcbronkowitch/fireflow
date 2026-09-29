"""Guard for the Rev A P1 cut outputs (spec 2026-09-29-rev-a-p1-panel-parts
§4): the hole list, the laser cut file and the 1:1 print sheet. Positions
come from gen_hw_panel.py; this checks that every control got exactly one
hole where it sits, that the acrylic keeps its webs, and that the committed
files are what the generator makes.

No pytest in this environment -- plain checks, exit code says it all.
Run from the repo root:  python host/vcv/res/test_hw_cut.py
"""
import json, math, os, re, sys
import gen_hw_panel as hw
import gen_hw_cut as cut

HERE = os.path.dirname(os.path.abspath(__file__))
FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def _committed_holes():
    with open(os.path.join(HERE, "FireflowHW-holes.json"), encoding="utf-8") as f:
        return json.load(f)["holes"]


def _extent(h):
    """(x0, y0, x1, y1) of a hole's bounding box, mm."""
    if "d_mm" in h:
        r = h["d_mm"] / 2
        return h["x_mm"] - r, h["y_mm"] - r, h["x_mm"] + r, h["y_mm"] + r
    return (h["x_mm"] - h["w_mm"] / 2, h["y_mm"] - h["h_mm"] / 2,
            h["x_mm"] + h["w_mm"] / 2, h["y_mm"] + h["h_mm"] / 2)


def web(a, b):
    """Material between two hole edges, mm. Circles exactly; slots as their
    bounding rectangles, which only ever under-reports the web."""
    if "d_mm" in a and "d_mm" in b:
        return (math.hypot(a["x_mm"] - b["x_mm"], a["y_mm"] - b["y_mm"])
                - a["d_mm"] / 2 - b["d_mm"] / 2)
    if "d_mm" in b:
        a, b = b, a
    if "d_mm" in a:
        x0, y0, x1, y1 = _extent(b)
        dx = max(x0 - a["x_mm"], 0.0, a["x_mm"] - x1)
        dy = max(y0 - a["y_mm"], 0.0, a["y_mm"] - y1)
        return math.hypot(dx, dy) - a["d_mm"] / 2
    ax0, ay0, ax1, ay1 = _extent(a)
    bx0, by0, bx1, by1 = _extent(b)
    dx = max(bx0 - ax1, ax0 - bx1, 0.0)
    dy = max(by0 - ay1, ay0 - by1, 0.0)
    return math.hypot(dx, dy)


def test_every_control_has_one_hole_where_it_sits():
    holes = _committed_holes()
    for c in hw.ALL_HW:
        at = [h for h in holes
              if abs(h["x_mm"] - c.x) < 0.01 and abs(h["y_mm"] - c.y) < 0.01]
        check(len(at) == 1, f"{c.enum} has {len(at)} holes at its coordinate")
        if len(at) == 1:
            want = cut.KIND[hw.hw_class(c.enum)]
            check(at[0]["kind"] == want,
                  f"{c.enum}'s hole is a {at[0]['kind']}, not a {want}")


def test_hole_counts():
    """Counted 2026-09-28 from the generator: 70 pot positions (ATTACK and
    STAGES share a knob per deck), 4 keys, 18 jacks, 19 LEDs, the SD slot and
    four mounting slots. When the plate changes, this goes red on purpose --
    update the numbers in the same commit as the plate."""
    kinds = {}
    for h in _committed_holes():
        kinds[h["kind"]] = kinds.get(h["kind"], 0) + 1
    want = {"pot": 70, "key": 4, "jack": 18, "led": 19, "sd": 1, "mount": 4}
    check(kinds == want, f"hole counts {kinds}, expected {want}")


def test_every_web_holds():
    holes = _committed_holes()
    for i, a in enumerate(holes):
        for b in holes[i + 1:]:
            w = web(a, b)
            check(w >= hw.MIN_WEB - 1e-6,
                  f"{a['id']} and {b['id']} leave {w:.2f} mm of material, "
                  f"under {hw.MIN_WEB}")


def test_holes_stay_on_the_plate_and_off_the_rails():
    x_lo = cut.TRIM + hw.MIN_WEB
    x_hi = cut.TRIM + cut.PLATE_W - hw.MIN_WEB
    for h in _committed_holes():
        x0, y0, x1, y1 = _extent(h)
        check(x0 >= x_lo - 1e-6 and x1 <= x_hi + 1e-6,
              f"{h['id']} reaches within {hw.MIN_WEB} mm of a side edge")
        if h["kind"] == "mount":
            continue
        check(y0 >= hw.KEEP_TOP - 1e-6 and y1 <= hw.KEEP_BOT + 1e-6,
              f"{h['id']} enters a rail zone ({y0:.2f}..{y1:.2f})")


def test_committed_files_match_the_generator():
    for name, fn in cut.OUTPUTS:
        path = os.path.join(HERE, name)
        if not os.path.exists(path):
            FAILS.append(f"{name} is missing -- run res/gen_hw_cut.py")
            continue
        with open(path, encoding="utf-8") as f:
            on_disk = f.read()
        check(on_disk == fn(),
              f"{name} differs from the generator's output -- it was "
              "hand-edited, or the generator changed without re-running it")


def test_cut_file_is_exactly_the_holes():
    """Formulor cuts pure-blue hairlines; anything else would be engraved or
    ignored. One outline plus one shape per hole, nothing more."""
    path = os.path.join(HERE, "FireflowHW-cut.svg")
    if not os.path.exists(path):
        FAILS.append("FireflowHW-cut.svg is missing -- run res/gen_hw_cut.py")
        return
    svg = open(path, encoding="utf-8").read()
    check('width="304.800mm"' in svg and 'height="128.500mm"' in svg,
          "cut file is not in mm at the nominal plate size")
    strokes = set(re.findall(r'stroke="([^"]+)"', svg))
    check(strokes == {cut.BLUE}, f"cut file uses strokes {strokes}, not only {cut.BLUE}")
    check(len(re.findall(r'fill="(?!none")', svg)) == 0, "cut file has a filled shape")
    shapes = re.findall(r"<(circle|rect)\b", svg)
    holes = cut.holes()
    check(len(shapes) == len(holes) + 1,
          f"cut file has {len(shapes)} shapes, expected {len(holes)} holes + 1 outline")
    for h in holes:
        check(cut.shape(h, cut.BLUE, "0.01") in svg, f"{h['id']} is not cut")


def main():
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print(f"FAIL ({len(FAILS)}):")
        for f in FAILS:
            print("  -", f)
        return 1
    print("PASS -- hw cut guards ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
