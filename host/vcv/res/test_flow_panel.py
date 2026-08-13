#!/usr/bin/env python3
"""Guard rails for the generated FireFlow Glow panel (Simple Touch 2).

Runs the generator in-process and asserts what must never drift: enum order,
the physical 10+2 pad split, stable identities, switch and silver-field
exclusions, and byte-for-byte generated SVG/header agreement.

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

# Both switches are mounted through the lower touch board. The traced electrode
# contours must clear their physical footprints by more than a rounding error.
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


def test_touch2_uses_true_ten_plus_two_mapping():
    """A physical Touch 2 is P00-P09 below and P10/P11 above."""
    check([getattr(p, "pad_id", None) for p in g.PAD_SHAPES] ==
          ["P%02d" % i for i in range(12)],
          "pad shapes must retain physical IDs P00 through P11")
    check([getattr(p, "zone", None) for p in g.PAD_SHAPES[:10]] ==
          ["lower_touch"] * 10,
          "P00-P09 must belong to the lower touch board")
    check([getattr(p, "zone", None) for p in g.PAD_SHAPES[10:]] ==
          ["upper_rear"] * 2,
          "P10/P11 must belong to the upper rear PCB")


def test_upper_pads_are_above_lower_touch_board():
    """The 10+2 split is physical, not another twelve-place lower field."""
    if any(not hasattr(p.bounds, "min_y") for p in g.PAD_SHAPES):
        check(False, "pad bounds need named min_y/max_y coordinates")
        return
    lower_top = min(p.bounds.min_y for p in g.PAD_SHAPES[:10])
    check(all(p.bounds.max_y < lower_top for p in g.PAD_SHAPES[10:]),
          "P10/P11 must sit wholly above the lower touch board")


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


def test_geometry_comes_from_the_named_physical_table():
    """Every generator coordinate comes from the named geometry module."""
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
    for source, generated in zip(geo.PADS, g.PAD_SHAPES):
        check(generated.pad_id == source.pad_id and
              generated.zone == source.zone and
              generated.points == source.points_mm and
              generated.verified == source.verified,
              "%s was retyped or lost physical metadata in the generator"
              % source.pad_id)


def test_unverified_paths_keep_source_notes():
    for shape in g.PAD_SHAPES:
        check(shape.verified or bool(shape.source_note.strip()),
              "%s is unverified without a source note" % shape.pad_id)


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


def _point_in_polygon(point, polygon):
    x, y = point
    inside = False
    previous = polygon[-1]
    for current in polygon:
        x0, y0 = previous
        x1, y1 = current
        if (y0 > y) != (y1 > y):
            crossing_x = x0 + (y - y0) * (x1 - x0) / (y1 - y0)
            if x < crossing_x:
                inside = not inside
        previous = current
    return inside


def _polygon_edge_samples(polygon, samples_per_edge=20):
    out = []
    for a, b in zip(polygon, polygon[1:] + polygon[:1]):
        for sample in range(samples_per_edge):
            t = sample / float(samples_per_edge)
            out.append((a[0] + (b[0] - a[0]) * t,
                        a[1] + (b[1] - a[1]) * t))
    return out


def test_pad_contours_clear_switches():
    switch_rects = [_rect(c) for c in g.PARAMS if c.kind == g.SWITCH]
    check(len(switch_rects) == 2, "Touch 2 must expose two switch clearances")
    for shape in g.PAD_SHAPES:
        samples = g.sample_closed_pad(shape, samples_per_segment=100)
        for rect in switch_rects:
            clearance = min(_point_rect_distance(p, rect) for p in samples)
            x0, y0, x1, y1 = rect
            probes = ((x0, y0), (x1, y0), (x1, y1), (x0, y1),
                      ((x0 + x1) / 2.0, (y0 + y1) / 2.0))
            switch_inside_pad = any(_point_in_polygon(p, samples) for p in probes)
            check(clearance >= SWITCH_CLEARANCE and not switch_inside_pad,
                  "%s clears a switch by %.2f mm, want %.2f mm with no "
                  "enclosed switch hardware"
                  % (shape.pad_id, clearance, SWITCH_CLEARANCE))


def test_no_electrode_enters_silver_decoration_zones():
    check(len(geo.SILVER_DECORATION_ZONES_MM) == 2,
          "Touch 2 must define two silver decorative fields")
    for shape in g.PAD_SHAPES:
        electrode = g.sample_closed_pad(shape, samples_per_segment=100)
        for zone_index, silver in enumerate(geo.SILVER_DECORATION_ZONES_MM):
            electrode_enters = any(_point_in_polygon(p, silver)
                                    for p in electrode)
            silver_enters = any(_point_in_polygon(p, electrode)
                                for p in _polygon_edge_samples(silver, 50))
            check(not electrode_enters and not silver_enters,
                  "%s overlaps silver decoration zone %d"
                  % (shape.pad_id, zone_index + 1))


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
    """Rectangles guard conventional controls; pads use exact contour gates."""
    all_ctls = g.PARAMS + g.OUTPUTS
    for i, a in enumerate(all_ctls):
        ax0, ay0, ax1, ay1 = _rect(a)
        for b in all_ctls[i + 1:]:
            if a.kind == g.PAD or b.kind == g.PAD:
                continue
            bx0, by0, bx1, by1 = _rect(b)
            check(not (ax0 < bx1 and bx0 < ax1 and ay0 < by1 and by0 < ay1),
                  "%s and %s overlap" % (a.enum, b.enum))


def test_on_panel():
    m = EDGE_KEEPOUT
    for c in g.PARAMS + g.OUTPUTS:
        if c.kind == g.PAD:
            index = int(c.enum.split("_")[1]) - 1
            x0, y0, x1, y1 = g.PAD_SHAPES[index].curve_bounds
        else:
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


def test_parameters_carry_no_printed_captions():
    """P-numbers and provisional functions belong to runtime metadata."""
    for c in g.PARAMS:
        check(c.label == "",
              "%s must print no caption, prints %r" % (c.enum, c.label))


def test_neutral_svg_prints_no_p_number_labels():
    panel = g.svg()
    for pad_id in ("P%02d" % i for i in range(12)):
        check(">%s<" % pad_id not in panel,
              "neutral SVG visibly prints %s" % pad_id)


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
        check(12 <= len(shape.points) <= 32,
              "%s must retain 12-32 reviewed anchors" % shape.pad_id)
        param = next(c for c in g.PARAMS if c.enum == "PAD_%d" % (i + 1))
        check((param.x, param.y) == shape.centre,
              "%s lost its generated control centre" % shape.pad_id)
        check(shape.pad_id in param.tip,
              "%s is missing from PAD_%d runtime metadata"
              % (shape.pad_id, i + 1))
        fingerprints.append(tuple(shape.points))
    check(len(set(fingerprints)) == 12, "all pad contours must be unique")


def test_generated_header_exports_pad_geometry():
    h = g.header()
    check("enum class PadZone : std::uint8_t { LowerTouch, UpperRear };" in h,
          "header lacks physical pad zones")
    check("struct PadShape" in h and "const XY* points;" in h and
          "std::size_t pointCount;" in h and "const char* id;" in h and
          "PadZone zone;" in h and "bool verified;" in h and
          "kPadShapes[12]" in h,
          "header lacks pad geometry")
    for pad_id in ("P%02d" % i for i in range(12)):
        check(('"%s"' % pad_id) in h,
              "generated header lacks stable ID %s" % pad_id)
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
