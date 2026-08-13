#!/usr/bin/env python3
"""Guard rails for the named physical Touch 2 geometry.

No pytest is required: exit status is the contract.
Run from host/vcv/:  python res/test_touch2_geometry.py
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import touch2_geometry as geo


FAILS = []


def check(condition, message):
    if not condition:
        FAILS.append(message)


def _signed_area(points):
    return 0.5 * sum(
        x0 * y1 - x1 * y0
        for (x0, y0), (x1, y1) in zip(points, points[1:] + points[:1])
    )


def test_plate_is_16hp():
    check(abs(geo.PLATE_W - 81.28) < 0.001,
          "plate width is %.3f, want 81.28 (16 HP)" % geo.PLATE_W)
    check(abs(geo.PLATE_H - 128.5) < 0.001,
          "plate height is %.3f, want 128.5" % geo.PLATE_H)


def test_touch2_is_named_physical_ten_plus_two():
    check([pad.pad_id for pad in geo.PADS] ==
          ["P%02d" % i for i in range(12)],
          "pads must retain stable P00-P11 order")
    check([pad.zone for pad in geo.PADS[:10]] == ["lower_touch"] * 10,
          "P00-P09 must remain on the lower touch board")
    check([pad.zone for pad in geo.PADS[10:]] == ["upper_rear"] * 2,
          "P10/P11 must remain on the upper rear PCB")
    lower_top = min(pad.bounds.min_y for pad in geo.PADS[:10])
    check(all(pad.bounds.max_y < lower_top for pad in geo.PADS[10:]),
          "upper pads must sit wholly above the lower board")


def test_pad_paths_are_reviewable_closed_shapes():
    for pad in geo.PADS:
        check(12 <= len(pad.points_mm) <= 32,
              "%s needs 12-32 reviewed anchors" % pad.pad_id)
        check(pad.points_mm[0] != pad.points_mm[-1],
              "%s repeats its implicit closure anchor" % pad.pad_id)
        check(_signed_area(pad.points_mm) > 0.0,
              "%s is not clockwise in panel coordinates" % pad.pad_id)
        for x, y in pad.points_mm:
            check(0.0 <= x <= 81.28 and 0.0 <= y <= 128.5,
                  "%s anchor (%.2f, %.2f) leaves the panel"
                  % (pad.pad_id, x, y))


def test_unverified_paths_explain_their_source():
    for pad in geo.PADS:
        check(pad.verified or bool(pad.source_note.strip()),
              "%s is unverified without a source note" % pad.pad_id)


def test_control_complement_and_named_centres():
    check(len(geo.KNOBS) == 6, "Touch 2 needs six knob centres")
    check(len(geo.FADERS) == 2, "Touch 2 needs two fader centres")
    check(len(geo.SWITCH_CENTRES_MM) == 2,
          "Touch 2 needs two switch centres")
    check(len(geo.JACKS) == 2, "Touch 2 needs two jack centres")
    expected = {
        "knob_s31", "knob_s32", "knob_s33", "knob_s34", "knob_s30",
        "knob_s35", "fader_s36", "fader_s37", "out_l", "out_r",
    }
    check(set(geo.CONTROL_CENTRES_MM) == expected,
          "named control centres drifted: %s"
          % sorted(geo.CONTROL_CENTRES_MM))


def test_controls_are_on_the_plate():
    points = list(geo.CONTROL_CENTRES_MM.values()) + \
        list(geo.SWITCH_CENTRES_MM)
    for index, (x, y) in enumerate(points):
        check(0.0 < x < geo.PLATE_W and 0.0 < y < geo.PLATE_H,
              "control centre %d (%.2f, %.2f) leaves the panel"
              % (index, x, y))


def test_knob_field_is_two_rows_in_board_order():
    top = geo.KNOBS[:4]
    xs = [x for x, _ in top]
    ys = [y for _, y in top]
    check(xs == sorted(xs), "S31-S34 must read left to right")
    check(max(ys) - min(ys) <= 0.16 + 1e-9,
          "S31-S34 no longer share the measured upper row")
    check(all(y > max(ys) for _, y in geo.KNOBS[4:]),
          "S30/S35 must remain below S31-S34")
    check(geo.KNOBS[4][0] < geo.KNOBS[5][0],
          "S30 must remain left of S35")


def test_faders_flank_the_knob_field():
    knob_xs = [x for x, _ in geo.KNOBS]
    for index, (x, _) in enumerate(geo.FADERS):
        check(x < min(knob_xs) or x > max(knob_xs),
              "fader %d no longer flanks the knob field" % index)
    check(geo.FADER_TRAVEL > 10.0,
          "fader travel %.1f mm is implausibly short" % geo.FADER_TRAVEL)


def test_switches_sit_on_the_lower_touch_board():
    for index, (_, y) in enumerate(geo.SWITCH_CENTRES_MM):
        check(y >= geo.PAD_FIELD_TOP,
              "switch %d is above the lower touch board" % index)


def test_rectification_error_is_rack_scale_not_fabrication_scale():
    check(0.0 < geo.REFERENCE_RMS_ERROR_MM < 0.5,
          "rectification RMS %.3f mm is outside the reviewed Rack range"
          % geo.REFERENCE_RMS_ERROR_MM)
    check(geo.REFERENCE_RMS_ERROR_MM <= geo.REFERENCE_MAX_ERROR_MM < 0.5,
          "rectification max %.3f mm is outside the reviewed Rack range"
          % geo.REFERENCE_MAX_ERROR_MM)


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print("FAIL (%d)" % len(FAILS))
        for failure in FAILS:
            print("  - " + failure)
        sys.exit(1)
    print("geometry OK")
