#!/usr/bin/env python3
"""Guard rails for the generated FireFlow Glow panel (Simple Touch 2).

Runs the generator in-process and asserts what must never drift: the enum
ORDER, the control complement of the board, rectangle-based collisions, and
that the committed SVG/header still match what the generator emits.

No pytest in this environment -- plain asserts, exit code says it all.
Run from host/vcv/:  python res/test_flow_panel.py

Proving one of these red by hand: delete res/__pycache__ first. Python's .pyc
invalidation keys on mtime AND size, and several natural perturbations here are
byte-length neutral (swapping two names, 12.0 -> 17.0, [0, 1, ...] -> [1, 0,
...]), so a size-neutral edit inside one mtime tick re-runs the OLD module and
prints a clean pass. Four false greens were collected that way once.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_flow_panel as g
import gen_panel as base
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
PARAM_ORDER = (['MOTION', 'DENSITY', 'BRIGHT', 'PACE', 'WANDER', 'SPACE'] +
               ['PAD_%d' % (i + 1) for i in range(12)] +
               ['FADER_L', 'FADER_R', 'SW_L', 'SW_R'])
OUTPUT_ORDER = ['OUT_L', 'OUT_R']

# Which macro is printed on which KNOB POSITION, in geo.KNOBS order. This is a
# statement about the finished panel, not a copy of g.KNOB_MACRO -- see
# test_which_macro_sits_on_which_knob.
KNOB_LAYOUT = ['MOTION', 'DENSITY', 'BRIGHT', 'PACE', 'WANDER', 'SPACE']
KNOB_CHAN = ['S31', 'S32', 'S33', 'S34', 'S30', 'S35']

# Everything printed must lie ON the plate. Not 2 mm inside it: the board's own
# left column -- both jacks at x = 4.3 and the left fader at x = 4.6 -- sits
# closer to the edge than that, and the plate, not our taste, decides. With a
# 2 mm keep-out and real PJ301M / VCVSlider footprints, OUT_L spans 0.11 mm from
# the edge, OUT_R 0.13, FADER_L 1.21 and FADER_R 1.14, and there is no remedy
# short of moving measured centres. A gate whose red has no remedy is not a
# gate. This lives HERE and not in the generator on purpose: a guard that takes
# its threshold from the file it polices can be disarmed by editing that file.
EDGE_KEEPOUT = 0.0

# Both switches are mounted THROUGH the pad field on the real board, so the
# twelve places are laid out around them (gen_flow_panel's "the pad field").
# This is the margin that layout must keep -- not zero, because a tile that
# merely fails to intersect a switch by 0.05 mm is a collision waiting for the
# next re-measure. The tightest pair today is the middle row against SW_R.
SWITCH_CLEARANCE = 0.5


def test_enum_order():
    check([c.enum for c in g.PARAMS] == PARAM_ORDER,
          "param enum order drifted: %s" % [c.enum for c in g.PARAMS])
    check(g.INPUTS == [],
          "the board has no inputs; INPUTS must stay empty")
    check([c.enum for c in g.OUTPUTS] == OUTPUT_ORDER,
          "output enum order drifted: %s" % [c.enum for c in g.OUTPUTS])


def test_macro_params_match_flow_macro_order():
    # engine/flow/flow_ids.h: M_MOTION, M_DENSITY, M_BRIGHT, M_PACE,
    # M_WANDER, M_SPACE. Glow.cpp indexes params[MOTION + m] directly, so the
    # first six params MUST be the six macros in that order -- which is why
    # re-sorting the KNOBS is done with KNOB_MACRO and not by moving enums.
    check([c.enum for c in g.PARAMS][:6] ==
          ['MOTION', 'DENSITY', 'BRIGHT', 'PACE', 'WANDER', 'SPACE'],
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
    """Positions must not be re-typed into the generator by hand.

    The pads are the exception and have their own tests below: their centres
    are computed from the measured field, not printed from the measured
    centres. Everything else is the measurement, verbatim.
    """
    faders = [(c.x, c.y) for c in g.PARAMS if c.kind == g.FADER]
    check(faders == [(x, y) for x, y in geo.FADERS],
          "fader centres drifted from touch2_geometry.FADERS")
    switches = [(c.x, c.y) for c in g.PARAMS if c.kind == g.SWITCH]
    check(switches == [(x, y) for x, y in geo.SWITCHES],
          "switch centres drifted from touch2_geometry.SWITCHES")
    jacks = [(c.x, c.y) for c in g.OUTPUTS]
    check(jacks == [(x, y) for x, y in geo.JACKS],
          "jack centres drifted from touch2_geometry.JACKS")
    # Sizes are ours, with one exception: the knob cap is printed at the board's
    # measured silkscreen collar radius, so it is a measurement like the centres
    # and must not be re-typed either. It used to read a round 4.5.
    check(g.KNOB_R == geo.KNOB_COLLAR_R,
          "the printed knob cap radius is %.3f mm, not the measured collar "
          "radius %.3f -- a round number was typed into the generator"
          % (g.KNOB_R, geo.KNOB_COLLAR_R))


def test_the_measured_pad_list_is_not_reading_order():
    """geo.PADS is left-to-right within three overlapping bands, and index i is
    MPR121 place i. Tidying it into reading order would silently renumber every
    electrode, and a y-sorted list is exactly what a tidy-up produces. The
    laid-out places ARE in reading order now, so this has to be asserted on the
    measured table -- which is the record, and stays one."""
    ys = [y for _, y in geo.PADS]
    check(ys != sorted(ys),
          "the measured pad list is sorted top-to-bottom -- it was tidied into "
          "reading order, which renumbers the MPR121 channels")


def test_the_measured_field_top_is_a_field_top():
    """PAD_FIELD_TOP is the one solid number in the pad segmentation and the
    layout hangs off it. It must sit below the knob row and above every
    measured pad centre, or it is not the top of the field."""
    check(max(y for _, y in geo.KNOBS) < geo.PAD_FIELD_TOP,
          "PAD_FIELD_TOP %.2f is not below the knob row (%.2f)"
          % (geo.PAD_FIELD_TOP, max(y for _, y in geo.KNOBS)))
    check(geo.PAD_FIELD_TOP <= min(y for _, y in geo.PADS),
          "PAD_FIELD_TOP %.2f sits below the topmost measured pad (%.2f)"
          % (geo.PAD_FIELD_TOP, min(y for _, y in geo.PADS)))
    check(geo.PAD_FIELD_TOP < geo.PLATE_H,
          "PAD_FIELD_TOP %.2f is off the plate" % geo.PAD_FIELD_TOP)


def test_the_bands_come_out_of_the_measurement():
    """Five, five and two, split out of the measured y positions. The sizes are
    stated here, not read from the generator: they are a fact about the board.
    """
    check([len(b) for b in g.PAD_BANDS] == [5, 5, 2],
          "the pad bands are %s, want [5, 5, 2]"
          % [len(b) for b in g.PAD_BANDS])
    check([i for b in g.PAD_BANDS for i in b] == list(range(12)),
          "the bands no longer read as places 1..12 in order: %s"
          % [i for b in g.PAD_BANDS for i in b])


def test_each_place_stays_where_it_was_measured():
    """The layout is tidy, not a reshuffle: of the twelve laid-out places, the
    one nearest measured centre i must BE place i. Stated globally on purpose,
    so it is not the generator's band/column arithmetic restated -- a swapped
    pair or a mixed-up band moves a place past its neighbour and fails here."""
    places = [(c.x, c.y) for c in g.PARAMS if c.kind == g.PAD]
    check(len(places) == len(geo.PADS), "place count differs from measured")
    if len(places) != len(geo.PADS):
        return
    for i, (mx, my) in enumerate(geo.PADS):
        d = [((px - mx) ** 2 + (py - my) ** 2) ** 0.5 for px, py in places]
        nearest = min(range(len(d)), key=lambda k: d[k])
        check(nearest == i,
              "measured place %d (%.2f, %.2f) is nearest laid-out place %d, "
              "not its own" % (i + 1, mx, my, nearest + 1))


def test_the_places_stay_inside_the_measured_field():
    """The field is measured; the places inside it are not. A tile that leaves
    the field is drawing over the knob row or off the plate."""
    for c in g.PARAMS:
        if c.kind != g.PAD:
            continue
        x0, y0, x1, y1 = _rect(c)
        check(y0 >= geo.PAD_FIELD_TOP,
              "%s starts at y = %.2f, above the measured field top (%.2f)"
              % (c.enum, y0, geo.PAD_FIELD_TOP))
        check(y1 <= geo.PLATE_H,
              "%s ends at y = %.2f, below the plate (%.2f)"
              % (c.enum, y1, geo.PLATE_H))
        check(x0 >= 0.0 and x1 <= geo.PLATE_W,
              "%s spans %.2f .. %.2f, outside the full-width field"
              % (c.enum, x0, x1))


def test_which_macro_sits_on_which_knob():
    """The six macros keep enum order; which knob each SITS on is a table.

    Stated here independently of g.KNOB_MACRO on purpose. Asserting the panel
    against the same table the panel was built from proves nothing -- any
    permutation satisfies it, including the inverse of the intended one. So the
    layout is written out by knob POSITION (S31 S32 S33 S34, then S30 S35), and
    re-sorting the panel means changing KNOB_MACRO *and* this list together.

    It decouples from KNOB_MACRO and NOT from geo.KNOBS, which is the table the
    generator read to produce c.x and c.y: a transposition inside geo.KNOBS
    moves subject and expectation together and passes here. That is the
    geometry record's own business, and test_the_knob_field_is_two_rows_in_
    board_order in res/test_touch2_geometry.py is the gate for it.

    The channel column is asserted too, not just quoted in a failure message.
    It reaches the player as the knob's runtime tooltip, and it is a separate
    claim from the position: permuting the generator's _KNOB_CHAN would move
    every printed S-number without moving a single knob.
    """
    for pos, name in enumerate(KNOB_LAYOUT):
        c = next((c for c in g.PARAMS if c.enum == name), None)
        check(c is not None, "%s is not a param at all" % name)
        if c is None:
            continue
        check((c.x, c.y) == geo.KNOBS[pos],
              "%s must sit on knob position %d (%s), it sits at (%.2f, %.2f)"
              % (name, pos, KNOB_CHAN[pos], c.x, c.y))
        check(c.tip.endswith("[%s]" % KNOB_CHAN[pos]),
              "%s sits on knob position %d, whose board channel is %s, but its "
              "tooltip reads %r" % (name, pos, KNOB_CHAN[pos], c.tip))


def _rect(c):
    if c.kind == g.PAD:
        index = int(c.enum.split("_")[1]) - 1
        return g.PAD_SHAPES[index].curve_bounds
    w, h = g.footprint_of(c)
    return (c.x - w / 2.0, c.y - h / 2.0, c.x + w / 2.0, c.y + h / 2.0)


def _point_rect_distance(p, rect):
    x, y = p
    x0, y0, x1, y1 = rect
    dx = max(x0 - x, 0.0, x - x1)
    dy = max(y0 - y, 0.0, y - y1)
    return (dx * dx + dy * dy) ** 0.5


def test_pad_contours_clear_switches():
    switch_rects = [_rect(c) for c in g.PARAMS if c.kind == g.SWITCH]
    for i, shape in enumerate(g.PAD_SHAPES):
        samples = g.sample_closed_pad(shape, samples_per_segment=20)
        for rect in switch_rects:
            clearance = min(_point_rect_distance(p, rect) for p in samples)
            check(clearance >= SWITCH_CLEARANCE,
                  "PAD_%d clears a switch by %.2f mm, want %.2f"
                  % (i + 1, clearance, SWITCH_CLEARANCE))


def test_pad_runtime_bounds_cover_the_spline_and_halo():
    """The header emits shape.bounds as a widget's min/max, so it must contain
    the whole cubic contour plus the outer half of TouchPlate's glow stroke.

    Knot extrema are not enough: Catmull-Rom becomes cubic Beziers and their
    extrema can sit between anchors. Sampling independently from the emitted
    bounds catches both that escape and halo clipping.
    """
    halo = g.PAD_GLOW_WIDTH / 2.0
    for i, shape in enumerate(g.PAD_SHAPES):
        x0, y0, x1, y1 = shape.bounds
        samples = g.sample_closed_pad(shape, samples_per_segment=200)
        sx0, sx1 = min(x for x, _ in samples), max(x for x, _ in samples)
        sy0, sy1 = min(y for _, y in samples), max(y for _, y in samples)
        check(x0 + halo <= sx0 and sx1 <= x1 - halo and
              y0 + halo <= sy0 and sy1 <= y1 - halo,
              "PAD_%d runtime bounds clip its spline or %.2f mm halo"
              % (i + 1, halo))


def test_no_overlap():
    """Rectangles guard every pair except organic pad/switch pairs, whose
    clearance is tested against the rendered spline in test_pad_contours_clear_switches."""
    all_ctls = g.PARAMS + g.OUTPUTS
    for i, a in enumerate(all_ctls):
        ax0, ay0, ax1, ay1 = _rect(a)
        for b in all_ctls[i + 1:]:
            if {a.kind, b.kind} == {g.PAD, g.SWITCH}:
                continue
            bx0, by0, bx1, by1 = _rect(b)
            check(not (ax0 < bx1 and bx0 < ax1 and ay0 < by1 and by0 < ay1),
                  "%s and %s overlap" % (a.enum, b.enum))


def test_on_panel():
    m = EDGE_KEEPOUT
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
        if not c.label:
            continue        # svg() prints nothing for these; there is no glyph
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
    check(words == ["FIREFLOW", "/", "GLOW"],
          "header must read FIREFLOW / GLOW only: %r" % words)
    check(len({t.y for t in g.TEXTS}) == 1,
          "FIREFLOW / GLOW must share one baseline")


def test_logo_font_weights():
    fireflow = next((t for t in g.TEXTS if t.str == "FIREFLOW"), None)
    glow = next((t for t in g.TEXTS if t.str == "GLOW"), None)
    check(fireflow is not None, "FireFlow text entry not found")
    check(glow is not None, "GLOW text entry not found")
    if fireflow is None or glow is None:
        return          # a missing half is already reported; do not crash on it
    check(fireflow.weight is not None and glow.weight is not None,
          "both wordmark halves must carry a weight")
    check(fireflow.weight < glow.weight,
          "FireFlow must be lighter than GLOW")


def test_wordmark_separator_is_panel_centred():
    separator = next((t for t in g.TEXTS if t.str == "/"), None)
    check(separator is not None, "wordmark separator is missing")
    if separator is not None:
        check(approx(separator.x, g.W * 0.5),
              "wordmark separator is not on the panel centre")
        check(separator.anchor == 0,
              "wordmark separator must be middle-anchored")


def test_dark_copper_panel_contract():
    panel = g.svg()
    check(g.PANEL_TOP == "#20221d" and g.PANEL_BOTTOM == "#10110f",
          "Glow must use the approved warm graphite gradient")
    check('id="alphaPennant"' not in panel, "alpha pennant must be removed")
    check('ALPHA' not in [t.str for t in g.TEXTS], "ALPHA text must be removed")
    check('<rect class="touchPlate"' not in panel,
          "touch pads must no longer be rounded rectangles")


def test_twelve_unique_pad_splines():
    check(len(g.PAD_SHAPES) == 12, "want twelve pad shapes")
    fingerprints = []
    for i, shape in enumerate(g.PAD_SHAPES):
        check(len(shape.points) == g.PAD_POINT_COUNT == 8,
              "PAD_%d must have eight anchors" % (i + 1))
        check(shape.centre == g.PAD_PLACES[i],
              "PAD_%d lost its control centre" % (i + 1))
        fingerprints.append(tuple(shape.points))
    check(len(set(fingerprints)) == 12, "all pad contours must be unique")


def test_pad_numbers_are_two_digit_edge_engravings():
    pads = [c for c in g.PARAMS if c.kind == g.PAD]
    check([c.label for c in pads] == ["%02d" % i for i in range(1, 13)],
          "pad labels must read 01 through 12")
    for i, c in enumerate(pads):
        x0, y0, x1, y1 = g.PAD_SHAPES[i].curve_bounds
        lx, ly = g.label_xy(c)
        check(x0 <= lx <= x1 and y0 <= ly <= y1,
              "%s label is outside its island" % c.enum)
        check(abs(lx - c.x) > 0.15 * (x1 - x0),
              "%s label is still centred" % c.enum)


def test_generated_header_exports_pad_geometry():
    h = g.header()
    check("static constexpr int kPadPointCount = 8;" in h,
          "header lacks point count")
    check("struct PadShape" in h and "XY min; XY max;" in h and
          "kPadShapes[12]" in h,
          "header lacks pad geometry")
    for alias in ("static constexpr float kPadW",
                  "static constexpr float kPadH",
                  "static constexpr float kPadR",
                  "kCollar"):
        check(alias not in h,
              "header retains obsolete rectangular-pad alias %s" % alias)
    check(not hasattr(g, "PAD_R"),
          "generator retains obsolete rectangular-pad radius PAD_R")


def test_the_masthead_rules_survive():
    """The two brand rules flanking the wordmark, by id.

    They are what is left of the four mockup signatures the old gate checked.
    Two of those four -- the macro accent and the NEW collar -- went out with
    the surface they belonged to and are correctly gone. These two did not:
    the generator still emits them, and until this they were the only drawn
    elements on the plate with an id and no gate.
    """
    panel = g.svg()
    for rule in ("glowBrandRuleLeft", "glowBrandRuleRight"):
        check('id="%s"' % rule in panel,
              "the masthead lost %s -- the wordmark reads unflanked" % rule)


def test_the_header_emits_no_zero_length_input_table():
    """`static const PanelCtl kInputCtls[] = {};` is a GCC extension and
    ill-formed standard C++. The enum must still be there."""
    hpp = g.header()
    check("kInputCtls" not in hpp,
          "the board has no inputs, so no kInputCtls table may be emitted")
    check("NUM_INPUTS" in hpp, "InputId/NUM_INPUTS must still be emitted")


def test_fader_wells_use_the_dark_copper_palette():
    """The two stock slider wells must sit in the graphite/copper panel."""
    legacy_fill, legacy_stroke = base.PAPER_DEEP, base.LINE
    for c in (c for c in g.PARAMS if c.kind == g.FADER):
        well = g.fader_svg(c).splitlines()[0]
        check('fill="%s"' % g.PAD_FILL in well,
              "%s well fill is not PAD_FILL: %s" % (c.enum, well))
        check('stroke="%s"' % g.PANEL_BORDER in well,
              "%s well stroke is not PANEL_BORDER: %s" % (c.enum, well))
        check('fill="%s"' % legacy_fill not in well,
              "%s well still uses legacy PAPER_DEEP: %s" % (c.enum, well))
        check('stroke="%s"' % legacy_stroke not in well,
              "%s well still uses legacy LINE: %s" % (c.enum, well))


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
