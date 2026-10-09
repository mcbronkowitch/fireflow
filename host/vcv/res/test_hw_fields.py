"""Guard for hw_fields.py (spec 2026-10-07 §7): the outline of a union of
axis-aligned rects, containment and gaps. Plain script; exit code is the
verdict. Run from host/vcv/: python res/test_hw_fields.py"""
import sys
import hw_fields as F

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def test_rectangle():
    pts = F.outline([(0, 10, 0, 5)])
    check(pts == [(0, 0), (10, 0), (10, 5), (0, 5)], f"rectangle outline {pts}")


def test_l_shape_from_two_rects_and_a_join():
    # upper band wide, lower band narrow, joined by a filler -- MOTION's shape
    rects = [(0, 30, 0, 10), (0, 15, 13, 20), (0, 15, 10, 13)]
    pts = F.outline(rects)
    check(pts == [(0, 0), (30, 0), (30, 10), (15, 10), (15, 20), (0, 20)],
          f"L outline {pts}")


def test_arch():
    # TIMING's arch: a top band and two legs, nothing between the legs
    rects = [(0, 30, 0, 10), (0, 8, 10, 30), (22, 30, 10, 30)]
    pts = F.outline(rects)
    check(pts == [(0, 0), (30, 0), (30, 30), (22, 30), (22, 10),
                  (8, 10), (8, 30), (0, 30)], f"arch outline {pts}")


def test_two_pieces_are_refused():
    for label, rects in (("apart", [(0, 5, 0, 5), (10, 15, 0, 5)]),
                         ("corner pinch", [(0, 5, 0, 5), (5, 10, 5, 10)]),
                         ("ring with a hole", [(0, 30, 0, 5), (0, 30, 25, 30),
                                               (0, 5, 5, 25), (25, 30, 5, 25)])):
        try:
            F.outline(rects)
            check(False, f"outline accepted {label}")
        except ValueError:
            pass


def test_field_attributes():
    f = F.Field([(0, 30, 0, 10), (0, 8, 10, 30), (22, 30, 10, 30)])
    check((f.x, f.y, f.w, f.h) == (0, 0, 30, 30), f"x/y/w/h {(f.x, f.y, f.w, f.h)}")
    check(f.covers(4, 20) and not f.covers(15, 20), "covers() ignores the arch's gap")
    check(f.contains_rect(1, 7, 2, 28), "a body in the left leg is not inside")
    check(not f.contains_rect(1, 12, 2, 28), "a body poking into the gap is inside")
    check(f.contains_rect(2, 28, 1, 9), "a body across the top band is not inside")


def test_gaps():
    a = F.Field([(0, 10, 0, 10)])
    b = F.Field([(13, 20, 0, 10)])
    c = F.Field([(13, 20, 14, 20)])
    d = F.Field([(5, 15, 5, 15)])
    check(abs(a.gap_to(b) - 3.0) < 1e-9, f"side gap {a.gap_to(b)}")
    check(abs(a.gap_to(c) - 5.0) < 1e-9, f"diagonal gap {a.gap_to(c)}")
    check(a.gap_to(d) == 0.0 and a.overlaps(d), "overlap not seen")
    check(not a.overlaps(b), "a 3 mm gap counted as overlap")


def main():
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print(f"FAIL ({len(FAILS)}):")
        for f in FAILS:
            print("  -", f)
        return 1
    print("PASS -- hw_fields ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
