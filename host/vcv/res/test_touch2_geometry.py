#!/usr/bin/env python3
"""Guard rails for the measured Touch 2 control centres.

These assertions catch the classes of measuring error that matter -- a
miscounted control, a transposed pair, a decimal point in the wrong place --
without pretending to know the board's true dimensions. They deliberately do
NOT assert tolerances on the copper positions themselves: a gate whose red has
no remedy is not a gate.

No pytest in this environment -- plain asserts, exit code says it all.
Run from host/vcv/:  python res/test_touch2_geometry.py
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import touch2_geometry as geo

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def test_plate_is_16hp():
    check(abs(geo.PLATE_W - 81.28) < 0.001,
          "plate width is %.3f, want 81.28 (16 HP)" % geo.PLATE_W)
    check(abs(geo.PLATE_H - 128.5) < 0.001,
          "plate height is %.3f, want 128.5" % geo.PLATE_H)


def test_px_per_mm_is_plausible():
    """PX_PER_MM is the calibration every other number here was pushed through,
    and it is the one export where a slipped decimal point would go unnoticed:
    the mm tables below are already in mm, so nothing else in this file would
    change. The band is deliberately wide -- it is a decimal-point gate, not a
    re-measurement. The source frame is 417 px across an 81.28 mm plate, so any
    honest value sits near 5; 0.5036 or 50.36 do not.
    """
    check(4.0 < geo.PX_PER_MM < 7.0,
          "PX_PER_MM is %.4f -- a photograph that resolves an %.2f mm plate in "
          "a few hundred pixels lands between 4 and 7 px/mm; check the decimal "
          "point" % (geo.PX_PER_MM, geo.PLATE_W))


# The radius band the collar detector actually searched, in PIXELS (see the
# per-class detection table in touch2_geometry). Stated HERE and not read from
# that file on purpose: a guard that takes its threshold from the file it
# polices can be disarmed by editing that file -- the same rule EDGE_KEEPOUT
# follows in test_flow_panel.py.
COLLAR_SEARCH_PX = (15.0, 24.0)


def test_knob_collar_radius_survives_the_trip_back_to_pixels():
    """KNOB_COLLAR_R is a RADIUS in millimetres, and gen_flow_panel prints the
    knob cap at it -- so a wrong one is a wrong plate, and nothing about the
    number itself would show it. Pushing it back through PX_PER_MM must land
    inside the band the ring score searched. That catches the two errors this
    export invites and the value cannot reveal: a diameter written where a
    radius belongs (7.77 mm -> 39 px, off the top of the band) and a slipped
    decimal (0.3885 mm -> 2 px, off the bottom).
    """
    lo, hi = COLLAR_SEARCH_PX
    px = geo.KNOB_COLLAR_R * geo.PX_PER_MM
    check(lo <= px <= hi,
          "KNOB_COLLAR_R %.4f mm is %.1f px at %.3f px/mm, outside the "
          "[%.0f, %.0f] px band the collar detector searched -- a "
          "radius/diameter mix-up or a slipped decimal point"
          % (geo.KNOB_COLLAR_R, px, geo.PX_PER_MM, lo, hi))


# The knob row's own numbers, stated HERE and not read from touch2_geometry --
# same rule COLLAR_SEARCH_PX follows. touch2_geometry's header says it twice:
# "the four upper knobs sit on one row to within 0.16 mm at a column pitch of
# 15.83 / 15.90 / 15.84 mm", and the TouchFX ASCII drawing puts S31..S34 across
# the top with S30 and S35 below them, left and right. The tiny slack on the
# spread is binary floating point, not measurement slack: the four y values
# differ by exactly 0.16 and 45.50 - 45.34 evaluates to 0.16000000000000014.
KNOB_ROW_SPREAD = 0.16
KNOB_ROW_N = 4


def test_the_knob_field_is_two_rows_in_board_order():
    """Four across, two below -- a claim about the BOARD, checked here.

    This is the gate the panel side cannot be: res/test_flow_panel.py asserts
    the drawn knobs against geo.KNOBS, and geo.KNOBS is the table the generator
    read to place them, so a transposition INSIDE this list moves subject and
    expectation together and passes. It was proved: swapping KNOBS[0] and
    KNOBS[1], regenerating, and re-running both panel guards printed OK while
    the emitted header had MOTION drawn at the second knob's coordinates with a
    tooltip still claiming board channel S31.

    So the structure gets asserted against the record instead. KNOBS[0..3] are
    S31 S32 S33 S34 left to right on one row; KNOBS[4] is S30 and KNOBS[5] is
    S35, below that row, S30 to the left. Any two-element swap breaks one of
    those three statements. This survives the 600 dpi re-measure spec 11
    schedules -- it says nothing about where the row IS.
    """
    if len(geo.KNOBS) != 6:
        return          # test_counts already reports it; do not index into it
    top = geo.KNOBS[:KNOB_ROW_N]
    xs = [x for x, _ in top]
    for i in range(len(xs) - 1):
        check(xs[i] < xs[i + 1],
              "KNOBS[%d] x=%.2f is not left of KNOBS[%d] x=%.2f -- the upper "
              "row must read S31 S32 S33 S34 left to right"
              % (i, xs[i], i + 1, xs[i + 1]))
    ys = [y for _, y in top]
    spread = max(ys) - min(ys)
    check(spread <= KNOB_ROW_SPREAD + 1e-9,
          "the four upper knobs span %.3f mm in y, want one row to within "
          "%.2f mm -- a knob from the lower row was transposed into the upper "
          "one" % (spread, KNOB_ROW_SPREAD))

    lo_l, lo_r = geo.KNOBS[4], geo.KNOBS[5]
    for i, (_, y) in ((4, lo_l), (5, lo_r)):
        check(y > max(ys),
              "KNOBS[%d] y=%.2f is not below the upper row (%.2f) -- S30 and "
              "S35 sit under S31..S34" % (i, y, max(ys)))
    check(lo_l[0] < lo_r[0],
          "KNOBS[4] x=%.2f is not left of KNOBS[5] x=%.2f -- S30 is the left "
          "of the two lower knobs, S35 the right" % (lo_l[0], lo_r[0]))


def test_counts():
    for name, want in (("PADS", 12), ("KNOBS", 6), ("FADERS", 2),
                       ("SWITCHES", 2), ("JACKS", 2)):
        have = len(getattr(geo, name))
        check(have == want, "%s has %d entries, want %d" % (name, have, want))


def test_everything_is_on_the_plate():
    for name in ("PADS", "KNOBS", "FADERS", "SWITCHES", "JACKS"):
        for i, (x, y) in enumerate(getattr(geo, name)):
            check(0.0 < x < geo.PLATE_W,
                  "%s[%d] x=%.2f is off the plate" % (name, i, x))
            check(0.0 < y < geo.PLATE_H,
                  "%s[%d] y=%.2f is off the plate" % (name, i, y))


def test_no_two_centres_coincide():
    """A copy-paste slip while measuring shows up here and nowhere else."""
    pts = []
    for name in ("PADS", "KNOBS", "FADERS", "SWITCHES", "JACKS"):
        for i, p in enumerate(getattr(geo, name)):
            pts.append(("%s[%d]" % (name, i), p))
    for i, (na, a) in enumerate(pts):
        for nb, b in pts[i + 1:]:
            d = ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5
            check(d > 1.0, "%s and %s are %.2f mm apart -- measuring slip?"
                  % (na, nb, d))


def test_zones_are_in_the_right_order_top_to_bottom():
    """The board reads jacks, then knobs, then pads. A transposed axis or a
    flipped image would break this and nothing else."""
    jack_y = max(y for _, y in geo.JACKS)
    knob_y = min(y for _, y in geo.KNOBS)
    pad_y = min(y for _, y in geo.PADS)
    check(jack_y < knob_y,
          "jacks (%.1f) must sit above the knobs (%.1f)" % (jack_y, knob_y))
    check(knob_y < pad_y,
          "knobs (%.1f) must sit above the pad field (%.1f)" % (knob_y, pad_y))


def test_faders_flank_the_knob_field():
    xs = [x for x, _ in geo.KNOBS]
    for i, (x, _) in enumerate(geo.FADERS):
        check(x < min(xs) or x > max(xs),
              "FADERS[%d] x=%.2f sits inside the knob field" % (i, x))
    check(geo.FADER_TRAVEL > 10.0,
          "fader travel %.1f mm is implausibly short" % geo.FADER_TRAVEL)


def test_switches_sit_inside_the_pad_field():
    pad_top = min(y for _, y in geo.PADS)
    for i, (_, y) in enumerate(geo.SWITCHES):
        check(y >= pad_top,
              "SWITCHES[%d] y=%.2f is above the pad field (%.2f) -- the "
              "TouchFX sketch puts both switches among the pads"
              % (i, y, pad_top))


def test_the_source_is_named():
    check(bool(geo.SRC_IMAGE),
          "SRC_IMAGE must name where these numbers came from")


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print("FAIL (%d)" % len(FAILS))
        for f in FAILS:
            print("  - " + f)
        sys.exit(1)
    print("geometry OK")
