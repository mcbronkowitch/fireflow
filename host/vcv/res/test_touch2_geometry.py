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
