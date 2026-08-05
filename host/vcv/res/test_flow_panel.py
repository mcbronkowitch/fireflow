#!/usr/bin/env python3
"""Guard rails for the generated FireFlow Glow panel.

Runs the generator in-process and asserts what must never drift: the enum
ORDER (patch compatibility), the geometry of spec 6, the silkscreen copy,
and that the committed SVG/header still match what the generator emits.

No pytest in this environment -- plain asserts, exit code says it all.
Run from host/vcv/:  python res/test_flow_panel.py
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_flow_panel as g

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def approx(a, b, tol=0.01):
    return abs(a - b) <= tol


# --- the frozen contract: enum ORDER defines ids in every saved patch --------
PARAM_ORDER = ['MOTION', 'DENSITY', 'BRIGHT', 'DIRT', 'WANDER', 'SPACE',
               'NEW_BTN']
INPUT_ORDER = ['CV_MOT', 'CV_DEN', 'CV_BRT', 'CV_DRT', 'CV_SPC', 'CLK']
OUTPUT_ORDER = ['OUT_L', 'OUT_R']


def test_enum_order():
    check([c.enum for c in g.PARAMS] == PARAM_ORDER,
          "param enum order drifted: %s" % [c.enum for c in g.PARAMS])
    check([c.enum for c in g.INPUTS] == INPUT_ORDER,
          "input enum order drifted: %s" % [c.enum for c in g.INPUTS])
    check([c.enum for c in g.OUTPUTS] == OUTPUT_ORDER,
          "output enum order drifted: %s" % [c.enum for c in g.OUTPUTS])


def test_macro_params_match_flow_macro_order():
    # engine/flow/flow_ids.h: M_MOTION, M_DENSITY, M_BRIGHT, M_DIRT,
    # M_WANDER, M_SPACE. Glow.cpp indexes params[MOTION + m] directly, so
    # the first six params MUST be the six macros in that order.
    check([c.enum for c in g.PARAMS][:6] ==
          ['MOTION', 'DENSITY', 'BRIGHT', 'DIRT', 'WANDER', 'SPACE'],
          "the first six params must mirror flow_ids.h's Macro order")


def test_panel_size():
    check(approx(g.W, 60.96), "panel width is %.3f, want 60.96 (12 HP)" % g.W)
    check(approx(g.Hh, 128.5), "panel height is %.3f, want 128.5" % g.Hh)


def test_knob_geometry():
    knobs = [c for c in g.PARAMS if c.kind == g.MACRO]
    check(len(knobs) == 6, "want 6 macro knobs, have %d" % len(knobs))
    check(approx(g.KNOB_R, 8.0), "knobs must be 16 mm (r=8), have r=%.2f" % g.KNOB_R)
    xs = sorted({c.x for c in knobs})
    ys = sorted({c.y for c in knobs})
    check(len(xs) == 3 and len(ys) == 2, "want a 3x2 grid, have %dx%d" % (len(xs), len(ys)))
    check(approx(xs[1] - xs[0], 20.0) and approx(xs[2] - xs[1], 20.0),
          "column pitch must be 20 mm, have %s" % [round(b - a, 2)
                                                   for a, b in zip(xs, xs[1:])])
    check(approx((xs[0] + xs[2]) / 2.0, g.W / 2.0),
          "knob grid is not centred on the plate")
    row1 = [c.enum for c in knobs if approx(c.y, ys[0])]
    row2 = [c.enum for c in knobs if approx(c.y, ys[1])]
    check(row1 == ['MOTION', 'DENSITY', 'BRIGHT'], "row 1 is %s" % row1)
    check(row2 == ['DIRT', 'WANDER', 'SPACE'], "row 2 is %s" % row2)


def test_jack_geometry():
    jacks = g.INPUTS + g.OUTPUTS
    check(len(jacks) == 8, "want 8 jacks, have %d" % len(jacks))
    xs = sorted({c.x for c in jacks})
    ys = sorted({c.y for c in jacks})
    check(len(xs) == 4 and len(ys) == 2, "want 4x2 jacks, have %dx%d" % (len(xs), len(ys)))
    for a, b in zip(xs, xs[1:]):
        check(approx(b - a, 14.0), "jack pitch must be 14 mm, have %.2f" % (b - a))
    check(approx((xs[0] + xs[3]) / 2.0, g.W / 2.0),
          "jack block is not centred on the plate")
    row1 = [c.enum for c in jacks if approx(c.y, ys[0])]
    row2 = [c.enum for c in jacks if approx(c.y, ys[1])]
    check(row1 == ['CV_MOT', 'CV_DEN', 'CV_BRT', 'CV_DRT'], "jack row 1 is %s" % row1)
    check(row2 == ['CV_SPC', 'CLK', 'OUT_L', 'OUT_R'], "jack row 2 is %s" % row2)


def test_wander_has_no_cv_and_there_is_no_rst():
    names = [c.enum for c in g.INPUTS]
    check('CV_WAN' not in names, "WANDER must have no CV jack (spec 6)")
    check('RST' not in names, "there is deliberately no RST jack (spec 6)")


def test_no_overlap():
    all_ctls = g.PARAMS + g.INPUTS + g.OUTPUTS
    for i, a in enumerate(all_ctls):
        for b in all_ctls[i + 1:]:
            d = ((a.x - b.x) ** 2 + (a.y - b.y) ** 2) ** 0.5
            need = g.radius_of(a) + g.radius_of(b)
            check(d >= need,
                  "%s and %s overlap (%.2f mm apart, need %.2f)"
                  % (a.enum, b.enum, d, need))


def test_on_panel():
    for c in g.PARAMS + g.INPUTS + g.OUTPUTS:
        r = g.radius_of(c)
        check(c.x - r >= 2.0 and c.x + r <= g.W - 2.0,
              "%s runs off the plate horizontally" % c.enum)
        check(c.y - r >= 2.0 and c.y + r <= g.Hh - 2.0,
              "%s runs off the plate vertically" % c.enum)


def test_labels_clear_every_glyph():
    all_ctls = g.PARAMS + g.INPUTS + g.OUTPUTS
    for c in all_ctls:
        lx, ly = g.label_xy(c)
        for other in all_ctls:
            d = ((lx - other.x) ** 2 + (ly - other.y) ** 2) ** 0.5
            check(d >= g.radius_of(other),
                  "%s's label baseline sits inside %s" % (c.enum, other.enum))


def test_silkscreen_copy():
    words = [t.str for t in g.TEXTS]
    check('FireFlow' in words, "the logo must read FireFlow")
    check('GLOW' in words, "the logo must read GLOW")
    ys = {t.y for t in g.TEXTS if t.str in ('FireFlow', 'GLOW')}
    check(len(ys) == 1, "FireFlow and GLOW must sit on ONE line (spec 6)")
    joined = " ".join(words + [c.label for c in
                               g.PARAMS + g.INPUTS + g.OUTPUTS])
    check('ton-k' not in joined and 'ton k' not in joined.lower(),
          "ton-k is the brand and must not appear on the panel (spec 6)")


def test_committed_files_match_the_generator():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    for path, produced in ((os.path.join(here, "Glow.svg"), g.svg()),
                           (os.path.join(root, "src",
                                         "generated_flow_panel.hpp"), g.header())):
        if not os.path.exists(path):
            FAILS.append("%s is missing -- run res/gen_flow_panel.py" % path)
            continue
        with open(path) as f:
            on_disk = f.read()
        check(on_disk == produced,
              "%s differs from the generator's output -- it was hand-edited, "
              "or the generator was changed without re-running it" % path)


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print("FAIL (%d)" % len(FAILS))
        for f in FAILS:
            print("  - " + f)
        sys.exit(1)
    print("panel OK")
