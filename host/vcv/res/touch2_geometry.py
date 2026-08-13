#!/usr/bin/env python3
"""Named physical geometry for the Synthux Simple Touch 2.

Coordinates are millimetres from the upper-left of the 16 HP panel.  The
official DXF is authoritative for removable-faceplate mechanics.  Pad paths
come from the two local first-party frontal photographs identified in
hardware/glow-faceplate/sources/provenance.md; they are reference geometry for
the VCV panel, not fabrication data.

The photographs establish the physical 10+2 split and the visible electrode
silhouettes, but the public Faceplate directory contains no labelled P00-P11
artwork.  Consequently every path retains a source note and remains
``verified=False`` until Synthux supplies a labelled drawing or the production
boards can be scanned.  Ambiguity is metadata, never presented as original
production geometry.
"""

from dataclasses import dataclass
from types import MappingProxyType
from typing import Literal, Mapping


PLATE_W = 81.28
PLATE_H = 128.5
PAD_FIELD_TOP = 77.1

# Retained measured dimensions used by the panel generator.
KNOB_COLLAR_R = 3.885
FADER_TRAVEL = 25.29


@dataclass(frozen=True)
class Bounds:
    min_x: float
    min_y: float
    max_x: float
    max_y: float

    def __iter__(self):
        return iter((self.min_x, self.min_y, self.max_x, self.max_y))


@dataclass(frozen=True)
class ClosedPath:
    pad_id: str
    zone: Literal["lower_touch", "upper_rear"]
    points_mm: tuple[tuple[float, float], ...]
    label_anchor_mm: tuple[float, float]
    verified: bool
    source_note: str

    @property
    def bounds(self) -> Bounds:
        xs = tuple(point[0] for point in self.points_mm)
        ys = tuple(point[1] for point in self.points_mm)
        return Bounds(min(xs), min(ys), max(xs), max(ys))


# Least-squares projective transform from pixels in
# Simple-Touch-22-Synthux-Academy.jpg to panel millimetres.  Six mechanical
# fiducials were used: upper-left and lower-right board corners and all four
# mounting-hole centres.  See provenance.md for inputs and residuals.
REFERENCE_TO_MM_HOMOGRAPHY = (
    (0.130959605, 0.000589551, -64.7027732),
    (-0.000747855, 0.130337729, -37.7993325),
    (0.000000148, 0.000005032, 1.0),
)
REFERENCE_RMS_ERROR_MM = 0.161
REFERENCE_MAX_ERROR_MM = 0.282


LOWER_SOURCE_NOTE = (
    "Silhouette traced from Simple-Touch-22-Synthux-Academy.jpg and checked "
    "against Audrey-Touch-Faceplates.jpg; P-number identity awaits a labelled "
    "Synthux drawing, so this path is not production-verified."
)
UPPER_SOURCE_NOTE = (
    "Permitted fallback: the prior Glow SVG profile was resampled to 16 "
    "anchors and transformed into the upper-rear zone seen in both first-party "
    "photographs. The photographs do not isolate a faithful P10/P11 outline or "
    "left/right identity, so this is not original electrode geometry."
)


# The point order is clockwise in the panel's screen coordinate system (x
# right, y down).  Sixteen anchors per path preserve corners while keeping the
# generated C++ table uniform for the current renderer.
PADS: tuple[ClosedPath, ...] = (
    ClosedPath("P00", "lower_touch", (
        (15.20, 77.60), (19.00, 77.25), (24.00, 77.25), (28.00, 77.35),
        (31.80, 78.20), (32.00, 79.80), (30.50, 81.00), (27.10, 81.30),
        (27.00, 84.00), (26.10, 86.70), (24.20, 89.20), (20.50, 90.00),
        (17.80, 88.80), (17.30, 86.00), (16.50, 82.50), (15.20, 79.50),
    ), (21.20, 80.20), False, LOWER_SOURCE_NOTE),
    ClosedPath("P01", "lower_touch", (
        (33.80, 77.25), (37.00, 77.15), (41.00, 77.15), (45.00, 77.40),
        (48.40, 78.10), (48.20, 81.00), (46.50, 84.00), (44.50, 86.20),
        (41.70, 88.20), (39.80, 90.00), (38.00, 88.00), (36.20, 85.50),
        (34.80, 83.00), (33.50, 81.00), (33.20, 79.00), (33.40, 78.00),
    ), (40.70, 80.20), False, LOWER_SOURCE_NOTE),
    ClosedPath("P02", "lower_touch", (
        (49.80, 77.50), (53.00, 77.20), (56.50, 77.20), (60.00, 77.60),
        (62.00, 79.00), (62.20, 82.00), (61.30, 85.00), (60.20, 88.00),
        (58.80, 91.00), (55.80, 90.00), (53.20, 88.50), (51.20, 86.00),
        (49.40, 84.00), (48.80, 81.50), (49.00, 79.00), (49.30, 78.00),
    ), (55.80, 80.20), False, LOWER_SOURCE_NOTE),
    ClosedPath("P03", "lower_touch", (
        (0.80, 100.00), (2.00, 99.00), (5.00, 98.50), (9.00, 98.00),
        (12.50, 97.00), (15.00, 96.00), (16.30, 99.00), (16.40, 103.00),
        (16.00, 108.00), (14.00, 113.00), (11.50, 117.00), (8.00, 121.00),
        (4.00, 124.00), (1.00, 126.50), (0.50, 119.00), (0.50, 106.00),
    ), (7.20, 102.00), False, LOWER_SOURCE_NOTE),
    ClosedPath("P04", "lower_touch", (
        (18.20, 93.00), (22.00, 92.00), (26.50, 92.00), (29.50, 93.00),
        (31.50, 95.00), (31.80, 100.00), (31.50, 105.00), (31.00, 110.00),
        (29.00, 114.00), (25.00, 115.00), (21.00, 114.00), (18.50, 112.00),
        (17.00, 108.00), (16.70, 103.00), (16.80, 98.00), (17.20, 94.00),
    ), (24.20, 96.00), False, LOWER_SOURCE_NOTE),
    ClosedPath("P05", "lower_touch", (
        (34.00, 93.00), (37.50, 91.20), (40.00, 91.50), (41.30, 94.00),
        (41.50, 97.00), (42.40, 99.00), (44.00, 101.00), (45.20, 105.00),
        (45.20, 111.00), (44.50, 117.00), (43.50, 122.00), (42.50, 127.00),
        (39.50, 128.00), (37.20, 125.00), (36.00, 120.00), (35.00, 113.00),
    ), (39.20, 102.00), False, LOWER_SOURCE_NOTE),
    ClosedPath("P06", "lower_touch", (
        (48.50, 94.00), (51.00, 92.00), (55.00, 91.00), (60.00, 92.00),
        (64.00, 94.00), (66.00, 97.00), (66.50, 102.00), (66.00, 107.00),
        (64.50, 112.00), (61.00, 115.00), (56.00, 115.00), (52.00, 114.00),
        (49.50, 112.00), (47.80, 108.00), (47.00, 103.00), (48.40, 98.00),
    ), (57.00, 96.00), False, LOWER_SOURCE_NOTE),
    ClosedPath("P07", "lower_touch", (
        (69.00, 100.00), (71.00, 98.50), (74.00, 97.50), (78.00, 97.50),
        (80.50, 98.00), (80.80, 101.00), (80.80, 105.00), (80.80, 113.00),
        (81.00, 120.00), (80.50, 127.00), (77.50, 125.00), (74.00, 122.00),
        (71.00, 119.00), (69.00, 115.00), (68.00, 108.00), (68.00, 102.00),
    ), (75.40, 101.00), False, LOWER_SOURCE_NOTE),
    ClosedPath("P08", "lower_touch", (
        (11.00, 119.00), (13.00, 116.50), (17.00, 114.50), (23.00, 113.50),
        (29.00, 114.00), (34.00, 116.00), (36.20, 119.00), (36.50, 123.00),
        (35.00, 127.30), (30.00, 128.00), (24.00, 128.00), (18.00, 127.80),
        (13.00, 127.00), (10.50, 126.00), (9.80, 124.00), (10.20, 121.00),
    ), (22.80, 120.00), False, LOWER_SOURCE_NOTE),
    ClosedPath("P09", "lower_touch", (
        (47.00, 118.00), (51.00, 115.50), (57.00, 114.50), (63.00, 115.00),
        (68.00, 117.00), (70.00, 120.00), (70.50, 123.00), (70.00, 126.50),
        (68.00, 128.00), (62.00, 128.00), (56.00, 128.00), (51.00, 127.80),
        (47.00, 127.00), (45.50, 125.00), (45.30, 122.00), (46.00, 120.00),
    ), (57.80, 120.00), False, LOWER_SOURCE_NOTE),
    ClosedPath("P10", "upper_rear", (
        (28.91, 2.36), (32.40, 2.00), (36.37, 2.18), (40.15, 2.62),
        (43.00, 3.46), (43.50, 4.88), (42.90, 6.55), (42.26, 8.37),
        (40.73, 9.79), (37.53, 10.00), (33.65, 9.69), (29.48, 9.47),
        (26.18, 8.83), (25.50, 7.24), (26.10, 5.39), (27.03, 3.67),
    ), (34.50, 6.20), False, UPPER_SOURCE_NOTE),
    ClosedPath("P11", "upper_rear", (
        (47.54, 2.00), (51.21, 2.01), (55.24, 2.70), (58.32, 3.31),
        (60.61, 4.22), (61.81, 5.67), (62.00, 7.36), (61.13, 9.26),
        (59.26, 10.84), (56.16, 11.50), (52.66, 11.48), (49.35, 10.78),
        (46.67, 9.54), (45.30, 7.88), (45.00, 5.96), (45.69, 3.71),
    ), (53.50, 6.50), False, UPPER_SOURCE_NOTE),
)


# The silver hatched fields are decoration, not electrodes.  These reviewed
# exclusion polygons follow their inner black boundaries in the frontal view.
SILVER_DECORATION_ZONES_MM: tuple[tuple[tuple[float, float], ...], ...] = (
    ((0.00, 77.10), (13.60, 77.10), (14.30, 82.00), (16.20, 87.00),
     (13.80, 94.50), (7.00, 97.50), (0.00, 96.00)),
    ((65.00, 77.10), (81.28, 77.10), (81.28, 96.50), (74.50, 96.00),
     (68.00, 93.00), (65.00, 87.00), (63.50, 81.50)),
)


SWITCH_CENTRES_MM: tuple[tuple[float, float], tuple[float, float]] = (
    (30.34, 86.37),
    (45.25, 92.88),
)

CONTROL_CENTRES_MM: Mapping[str, tuple[float, float]] = MappingProxyType({
    "knob_s31": (16.90, 45.42),
    "knob_s32": (32.73, 45.48),
    "knob_s33": (48.63, 45.34),
    "knob_s34": (64.47, 45.50),
    "knob_s30": (16.89, 62.82),
    "knob_s35": (64.46, 62.83),
    "fader_s36": (4.61, 56.75),
    "fader_s37": (76.74, 56.80),
    "out_l": (4.31, 15.15),
    "out_r": (4.33, 30.60),
})

# Compatibility views for existing consumers.  New code should use the named
# interfaces above so channel identity is never inferred from an index.
KNOBS = tuple(CONTROL_CENTRES_MM[name] for name in (
    "knob_s31", "knob_s32", "knob_s33", "knob_s34", "knob_s30", "knob_s35"
))
FADERS = tuple(CONTROL_CENTRES_MM[name] for name in ("fader_s36", "fader_s37"))
SWITCHES = SWITCH_CENTRES_MM
JACKS = tuple(CONTROL_CENTRES_MM[name] for name in ("out_l", "out_r"))


def _signed_area(points: tuple[tuple[float, float], ...]) -> float:
    return 0.5 * sum(
        x0 * y1 - x1 * y0
        for (x0, y0), (x1, y1) in zip(points, points[1:] + points[:1])
    )


def _validate() -> None:
    assert len(PADS) == 12, "Touch 2 must expose exactly twelve electrodes"
    ids = tuple(pad.pad_id for pad in PADS)
    assert len(set(ids)) == len(ids), "pad IDs must be unique"
    assert ids == tuple("P%02d" % i for i in range(12)), \
        "pads must be ordered P00 through P11"
    for index, pad in enumerate(PADS):
        assert 8 <= len(pad.points_mm) <= 32, \
            "%s needs 8-32 reviewed anchors" % pad.pad_id
        assert pad.points_mm[0] != pad.points_mm[-1], \
            "%s closure is implicit; do not duplicate the first anchor" % pad.pad_id
        assert _signed_area(pad.points_mm) > 0.0, \
            "%s anchors must be clockwise in panel coordinates" % pad.pad_id
        assert all(0.0 <= x <= PLATE_W and 0.0 <= y <= PLATE_H
                   for x, y in pad.points_mm), \
            "%s leaves the panel" % pad.pad_id
        expected_zone = "lower_touch" if index < 10 else "upper_rear"
        assert pad.zone == expected_zone, \
            "%s belongs to %s" % (pad.pad_id, expected_zone)
        if pad.zone == "lower_touch":
            assert pad.bounds.min_y >= PAD_FIELD_TOP, \
                "%s leaves the lower touch board" % pad.pad_id
        else:
            assert pad.bounds.max_y < PAD_FIELD_TOP, \
                "%s leaves the upper rear PCB" % pad.pad_id
        lx, ly = pad.label_anchor_mm
        assert pad.bounds.min_x <= lx <= pad.bounds.max_x and \
               pad.bounds.min_y <= ly <= pad.bounds.max_y, \
            "%s label anchor leaves its path bounds" % pad.pad_id
        assert pad.verified or pad.source_note.strip(), \
            "%s needs a source note while unverified" % pad.pad_id
    assert len(SWITCH_CENTRES_MM) == 2, "Touch 2 has two switches"
    assert all(0.0 <= x <= PLATE_W and 0.0 <= y <= PLATE_H
               for x, y in SWITCH_CENTRES_MM), "switch centre leaves panel"
    assert all(0.0 <= x <= PLATE_W and 0.0 <= y <= PLATE_H
               for x, y in CONTROL_CENTRES_MM.values()), \
        "control centre leaves panel"


_validate()
