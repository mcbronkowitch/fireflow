#!/usr/bin/env python3
"""Guard rails for the generated FireFlow Glow panel (Simple Touch 2).

Runs the generator in-process and asserts what must never drift: the enum
ORDER, the control complement of the board, rectangle-based collisions, and
that the committed SVG/header still match what the generator emits.

No pytest in this environment -- plain asserts, exit code says it all.
Run from host/vcv/:  python res/test_flow_panel.py
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_flow_panel as g
import touch2_geometry as geo

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def approx(a, b, tol=0.01):
    return abs(a - b) <= tol


# --- the contract: enum ORDER defines ids in every saved patch ----------------
# Glow ships as a dev alpha and no patches exist, so this order was rewritten
# once, on purpose, when the panel became the Touch 2. From here on it is the
# contract again: a param's id is its index, and nothing may be inserted into
# the middle of this list.
PARAM_ORDER = (['MOTION', 'DENSITY', 'BRIGHT', 'DIRT', 'WANDER', 'SPACE'] +
               ['PAD_%d' % (i + 1) for i in range(12)] +
               ['FADER_L', 'FADER_R', 'SW_L', 'SW_R'])
OUTPUT_ORDER = ['OUT_L', 'OUT_R']

# Which macro is printed on which KNOB POSITION, in geo.KNOBS order. This is a
# statement about the finished panel, not a copy of g.KNOB_MACRO -- see
# test_which_macro_sits_on_which_knob.
KNOB_LAYOUT = ['MOTION', 'DENSITY', 'BRIGHT', 'DIRT', 'WANDER', 'SPACE']
KNOB_CHAN = ['S31', 'S32', 'S33', 'S34', 'S30', 'S35']

# The one pair the collision guard tolerates, and the reason it exists.
# Both three-position switches are mounted THROUGH the pad field on the real
# board -- touch2_geometry's own test_switches_sit_inside_the_pad_field asserts
# that, and the TouchFX sketch draws it. No pad tile bigger than 12.0 x 4.95 mm
# clears SW_L, and a field of 5 mm dashes would misdescribe the board worse
# than a shared corner does. So the overlap is allowed, but it is PINNED: a new
# one, or the loss of this one, is a failure.
EXPECTED_OVERLAPS = {('PAD_2', 'SW_L')}


def test_enum_order():
    check([c.enum for c in g.PARAMS] == PARAM_ORDER,
          "param enum order drifted: %s" % [c.enum for c in g.PARAMS])
    check(g.INPUTS == [],
          "the board has no inputs; INPUTS must stay empty")
    check([c.enum for c in g.OUTPUTS] == OUTPUT_ORDER,
          "output enum order drifted: %s" % [c.enum for c in g.OUTPUTS])


def test_macro_params_match_flow_macro_order():
    # engine/flow/flow_ids.h: M_MOTION, M_DENSITY, M_BRIGHT, M_DIRT,
    # M_WANDER, M_SPACE. Glow.cpp indexes params[MOTION + m] directly, so the
    # first six params MUST be the six macros in that order -- which is why
    # re-sorting the KNOBS is done with KNOB_MACRO and not by moving enums.
    check([c.enum for c in g.PARAMS][:6] ==
          ['MOTION', 'DENSITY', 'BRIGHT', 'DIRT', 'WANDER', 'SPACE'],
          "the first six params must mirror flow_ids.h's Macro order")


def test_panel_size():
    check(approx(g.W, 81.28), "panel width is %.3f, want 81.28 (16 HP)" % g.W)
    check(approx(g.Hh, 128.5), "panel height is %.3f, want 128.5" % g.Hh)


def test_control_complement_matches_the_board():
    counts = {}
    for c in g.PARAMS + g.OUTPUTS:
        counts[c.kind] = counts.get(c.kind, 0) + 1
    for kind, want, what in ((g.MACRO, 6, "trim knobs"),
                             (g.PAD, 12, "touch pads"),
                             (g.FADER, 2, "faders"),
                             (g.SWITCH, 2, "switches"),
                             (g.OUT, 2, "jacks")):
        check(counts.get(kind, 0) == want,
              "want %d %s, have %d" % (want, what, counts.get(kind, 0)))


def test_geometry_comes_from_the_measured_table():
    """Positions must not be re-typed into the generator by hand."""
    pads = [(c.x, c.y) for c in g.PARAMS if c.kind == g.PAD]
    check(pads == [(x, y) for x, y in geo.PADS],
          "pad centres drifted from touch2_geometry.PADS")
    faders = [(c.x, c.y) for c in g.PARAMS if c.kind == g.FADER]
    check(faders == [(x, y) for x, y in geo.FADERS],
          "fader centres drifted from touch2_geometry.FADERS")
    switches = [(c.x, c.y) for c in g.PARAMS if c.kind == g.SWITCH]
    check(switches == [(x, y) for x, y in geo.SWITCHES],
          "switch centres drifted from touch2_geometry.SWITCHES")
    jacks = [(c.x, c.y) for c in g.OUTPUTS]
    check(jacks == [(x, y) for x, y in geo.JACKS],
          "jack centres drifted from touch2_geometry.JACKS")


def test_pad_order_is_the_board_order_not_reading_order():
    """geo.PADS is left-to-right within three overlapping bands, and index i is
    MPR121 place i. Tidying it into reading order would silently renumber every
    electrode, and a y-sorted list is exactly what a tidy-up produces."""
    ys = [c.y for c in g.PARAMS if c.kind == g.PAD]
    check(ys != sorted(ys),
          "the pad list is sorted top-to-bottom -- it was tidied into reading "
          "order, which renumbers the MPR121 channels")


def test_which_macro_sits_on_which_knob():
    """The six macros keep enum order; which knob each SITS on is a table.

    Stated here independently of g.KNOB_MACRO on purpose. Asserting the panel
    against the same table the panel was built from proves nothing -- any
    permutation satisfies it, including the inverse of the intended one. So the
    layout is written out by knob POSITION (S31 S32 S33 S34, then S30 S35), and
    re-sorting the panel means changing KNOB_MACRO *and* this list together.
    """
    for pos, name in enumerate(KNOB_LAYOUT):
        c = next((c for c in g.PARAMS if c.enum == name), None)
        check(c is not None, "%s is not a param at all" % name)
        if c is None:
            continue
        check((c.x, c.y) == geo.KNOBS[pos],
              "%s must sit on knob position %d (%s), it sits at (%.2f, %.2f)"
              % (name, pos, KNOB_CHAN[pos], c.x, c.y))


def _rect(c):
    w, h = g.footprint_of(c)
    return (c.x - w / 2.0, c.y - h / 2.0, c.x + w / 2.0, c.y + h / 2.0)


def test_no_overlap():
    all_ctls = g.PARAMS + g.OUTPUTS
    hits = set()
    for i, a in enumerate(all_ctls):
        ax0, ay0, ax1, ay1 = _rect(a)
        for b in all_ctls[i + 1:]:
            bx0, by0, bx1, by1 = _rect(b)
            if ax0 < bx1 and bx0 < ax1 and ay0 < by1 and by0 < ay1:
                hits.add((a.enum, b.enum))
    for pair in sorted(hits - EXPECTED_OVERLAPS):
        check(False, "%s and %s overlap" % pair)
    for pair in sorted(EXPECTED_OVERLAPS - hits):
        check(False, "%s and %s no longer overlap -- delete the exception in "
                     "EXPECTED_OVERLAPS instead of carrying a dead one" % pair)


def test_on_panel():
    m = g.EDGE_KEEPOUT
    for c in g.PARAMS + g.OUTPUTS:
        x0, y0, x1, y1 = _rect(c)
        check(x0 >= m and x1 <= g.W - m,
              "%s runs off the plate horizontally (%.2f .. %.2f)"
              % (c.enum, x0, x1))
        check(y0 >= m and y1 <= g.Hh - m,
              "%s runs off the plate vertically (%.2f .. %.2f)"
              % (c.enum, y0, y1))


def test_labels_clear_every_glyph():
    all_ctls = g.PARAMS + g.OUTPUTS
    for c in all_ctls:
        lx, ly = g.label_xy(c)
        for other in all_ctls:
            if other is c:
                continue
            x0, y0, x1, y1 = _rect(other)
            check(not (x0 < lx < x1 and y0 < ly < y1),
                  "%s's label baseline sits inside %s" % (c.enum, other.enum))


def test_only_the_pads_carry_printed_captions():
    """A printed TEMPO beside a fader assigned to `off` would be a lie baked
    into an SVG. Function names are runtime tooltips (spec 3.3)."""
    for c in g.PARAMS:
        if c.kind == g.PAD:
            check(c.label.isdigit(),
                  "pad %s must print its number, prints %r" % (c.enum, c.label))
        else:
            check(c.label == "",
                  "%s must print no caption, prints %r" % (c.enum, c.label))


def test_silkscreen_copy():
    words = [t.str for t in g.TEXTS]
    check('FireFlow' in words, "the logo must read FireFlow")
    check('GLOW' in words, "the logo must read GLOW")
    ys = {t.y for t in g.TEXTS if t.str in ('FireFlow', 'GLOW')}
    check(len(ys) == 1, "FireFlow and GLOW must sit on ONE line")
    joined = " ".join(words + [c.label for c in g.PARAMS + g.OUTPUTS])
    check('ton-k' not in joined and 'ton k' not in joined.lower(),
          "ton-k is the brand and must not appear on the panel")


def test_logo_font_weights():
    fireflow = next((t for t in g.TEXTS if t.str == "FireFlow"), None)
    glow = next((t for t in g.TEXTS if t.str == "GLOW"), None)
    check(fireflow is not None, "FireFlow text entry not found")
    check(glow is not None, "GLOW text entry not found")
    if fireflow is None or glow is None:
        return          # a missing half is already reported; do not crash on it
    check(fireflow.weight is not None and glow.weight is not None,
          "both wordmark halves must carry a weight")
    check(fireflow.weight < glow.weight,
          "FireFlow must be lighter than GLOW")


def test_wordmark_is_visually_centred():
    """The two differently sized words must centre as ONE visible mark.

    They are anchored end / start, so equal anchors put the visible mark ~5 mm
    left of centre. Derive the outer bounds here independently of the
    generator's own offset arithmetic.
    """
    fireflow = next(t for t in g.TEXTS if t.str == "FireFlow")
    glow = next(t for t in g.TEXTS if t.str == "GLOW")
    left = fireflow.x - len(fireflow.str) * fireflow.size * 0.60
    right = glow.x + len(glow.str) * glow.size * 0.60
    centre = (left + right) / 2.0
    check(abs(centre - g.W / 2.0) <= 0.5,
          "wordmark is %.2f mm off panel centre" % (centre - g.W / 2.0))


def test_alpha_pennant_survives():
    panel = g.svg()
    check('id="alphaPennant"' in panel,
          "the early-alpha faceplate needs its pennant")
    check('ALPHA' in [t.str for t in g.TEXTS],
          "the pennant label must reach Rack's runtime text overlay")


def test_the_header_emits_no_zero_length_input_table():
    """`static const PanelCtl kInputCtls[] = {};` is a GCC extension and
    ill-formed standard C++. The enum must still be there."""
    hpp = g.header()
    check("kInputCtls" not in hpp,
          "the board has no inputs, so no kInputCtls table may be emitted")
    check("NUM_INPUTS" in hpp, "InputId/NUM_INPUTS must still be emitted")


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
