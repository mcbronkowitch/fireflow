#!/usr/bin/env python3
"""Analyze the pinned Touch 2 DXF into source-derived mechanics.

This module is the generator's geometry input.  It intentionally derives
centres and envelopes from connected DXF spline unions; it contains no
nominal drill or slot dimensions.  The mechanical verifier has a separate
parser and does not import this module.
"""

from __future__ import annotations

import hashlib
import math
from dataclasses import dataclass
from pathlib import Path


DXF_SHA256 = "55c84aa39e8d4e1484ae05a5145a545dd4749097129adb35a6fdb9da1ff606a0"
MM_PER_INCH = 25.4
ENDPOINT_TOLERANCE_MM = 0.000010
KICAD_MIN_EDGE_SEGMENT_MM = 0.000200

Point = tuple[float, float]
Segment = tuple[Point, Point, Point, Point]


@dataclass(frozen=True)
class SplineRecord:
    index: int
    flags: int
    segments: tuple[Segment, ...]


@dataclass(frozen=True)
class Aperture:
    reference: str
    kind: str
    centre: Point
    width: float
    height: float
    record_indices: tuple[int, ...]
    segments: tuple[Segment, ...]
    source_segment_count: int
    max_kicad_deviation: float


@dataclass(frozen=True)
class SourceGeometry:
    records: tuple[SplineRecord, ...]
    reference_records: tuple[SplineRecord, ...]
    outer: SplineRecord
    apertures: tuple[Aperture, ...]
    open_reference_record_indices: tuple[int, ...]
    dxf_origin: Point
    width: float
    height: float


def _pairs(path: Path) -> list[tuple[int, str]]:
    lines = path.read_text(encoding="cp1252").splitlines()
    if len(lines) % 2:
        raise ValueError("DXF group-code stream has an odd line count")
    return [(int(lines[index].strip()), lines[index + 1].strip())
            for index in range(0, len(lines), 2)]


def _raw_records(path: Path) -> tuple[SplineRecord, ...]:
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != DXF_SHA256:
        raise ValueError(f"DXF hash {digest} does not match provenance")
    pairs = _pairs(path)
    records = []
    for start, pair in enumerate(pairs):
        if pair != (0, "SPLINE"):
            continue
        end = next(index for index in range(start + 1, len(pairs))
                   if pairs[index][0] == 0)
        entity = pairs[start:end]
        degree = next(int(value) for code, value in entity if code == 71)
        flags = next(int(value) for code, value in entity if code == 70)
        xs = [float(value) for code, value in entity if code == 10]
        ys = [float(value) for code, value in entity if code == 20]
        if degree != 3 or len(xs) != len(ys) or (len(xs) - 1) % 3:
            raise ValueError("DXF SPLINE is not a cubic Bezier chain")
        controls = tuple(zip(xs, ys))
        segments = tuple(tuple(controls[index:index + 4])
                         for index in range(0, len(controls) - 1, 3))
        records.append(SplineRecord(len(records), flags, segments))
    if len(records) != 27:
        raise ValueError(f"expected 27 official spline records, found {len(records)}")
    return tuple(records)


def _cubic(p0: float, p1: float, p2: float, p3: float, t: float) -> float:
    u = 1.0 - t
    return (u**3 * p0 + 3.0 * u * u * t * p1
            + 3.0 * u * t * t * p2 + t**3 * p3)


def _extrema(values: tuple[float, float, float, float]) -> tuple[float, ...]:
    p0, p1, p2, p3 = values
    a = -p0 + 3.0 * p1 - 3.0 * p2 + p3
    b = 2.0 * (p0 - 2.0 * p1 + p2)
    c = p1 - p0
    candidates = [0.0, 1.0]
    if abs(a) < 1e-15:
        if abs(b) > 1e-15:
            candidates.append(-c / b)
    else:
        discriminant = b * b - 4.0 * a * c
        if discriminant >= 0.0:
            root = math.sqrt(discriminant)
            candidates.extend(((-b - root) / (2.0 * a),
                               (-b + root) / (2.0 * a)))
    return tuple(_cubic(p0, p1, p2, p3, t)
                 for t in candidates if 0.0 <= t <= 1.0)


def segments_bbox(segments: tuple[Segment, ...]) -> tuple[float, float,
                                                           float, float]:
    xs, ys = [], []
    for segment in segments:
        xs.extend(_extrema(tuple(point[0] for point in segment)))
        ys.extend(_extrema(tuple(point[1] for point in segment)))
    return min(xs), min(ys), max(xs), max(ys)


def _components(records: tuple[SplineRecord, ...]) -> tuple[tuple[int, ...], ...]:
    adjacency = {record.index: set() for record in records}
    for offset, left in enumerate(records):
        left_ends = (left.segments[0][0], left.segments[-1][-1])
        for right in records[offset + 1:]:
            right_ends = (right.segments[0][0], right.segments[-1][-1])
            if min(math.dist(a, b) for a in left_ends for b in right_ends) \
                    <= ENDPOINT_TOLERANCE_MM:
                adjacency[left.index].add(right.index)
                adjacency[right.index].add(left.index)
    found, components = set(), []
    for record in records:
        if record.index in found:
            continue
        pending, component = [record.index], []
        while pending:
            index = pending.pop()
            if index in found:
                continue
            found.add(index)
            component.append(index)
            pending.extend(adjacency[index] - found)
        components.append(tuple(sorted(component)))
    return tuple(components)


def _closed(component: tuple[int, ...], by_index: dict[int, SplineRecord]) -> bool:
    endpoints = []
    for index in component:
        record = by_index[index]
        endpoints.extend((record.segments[0][0], record.segments[-1][-1]))
    return all(any(other_index != index and
                   math.dist(point, other) <= ENDPOINT_TOLERANCE_MM
                   for other_index, other in enumerate(endpoints))
               for index, point in enumerate(endpoints))


def _component_geometry(component: tuple[int, ...],
                        by_index: dict[int, SplineRecord]):
    boxes = [segments_bbox(by_index[index].segments) for index in component]
    bbox = (min(box[0] for box in boxes), min(box[1] for box in boxes),
            max(box[2] for box in boxes), max(box[3] for box in boxes))
    return ((bbox[0] + bbox[2]) / 2.0, (bbox[1] + bbox[3]) / 2.0,
            bbox[2] - bbox[0], bbox[3] - bbox[1])


def _reverse_segments(segments: tuple[Segment, ...]) -> tuple[Segment, ...]:
    return tuple(tuple(reversed(segment)) for segment in reversed(segments))


def _ordered_component_segments(component: tuple[int, ...],
                                by_index: dict[int, SplineRecord]) \
        -> tuple[Segment, ...]:
    """Order and orient a connected record union into one closed chain."""
    first = min(component)
    ordered = list(by_index[first].segments)
    remaining = set(component) - {first}
    while remaining:
        end = ordered[-1][-1]
        match = None
        for index in sorted(remaining):
            record = by_index[index]
            if math.dist(end, record.segments[0][0]) <= ENDPOINT_TOLERANCE_MM:
                match = index, record.segments
                break
            if math.dist(end, record.segments[-1][-1]) <= ENDPOINT_TOLERANCE_MM:
                match = index, _reverse_segments(record.segments)
                break
        if match is None:
            raise ValueError(f"DXF component {component} is not one ordered chain")
        index, segments = match
        ordered.extend(segments)
        remaining.remove(index)
    if math.dist(ordered[0][0], ordered[-1][-1]) > ENDPOINT_TOLERANCE_MM:
        raise ValueError(f"DXF component {component} is not closed")
    # Illustrator emits repeated zero-length cubic no-ops at many knots.
    # They carry no path geometry and are invalid/duplicated Edge.Cuts items.
    meaningful = tuple(segment for segment in ordered
                       if any(math.dist(segment[0], point) > 1e-12
                              for point in segment[1:]))
    if math.dist(meaningful[0][0], meaningful[-1][-1]) > \
            ENDPOINT_TOLERANCE_MM:
        raise ValueError(f"DXF component {component} changed when no-ops were removed")
    # Put the longest source segment at the serialization seam.  The chain is
    # geometrically identical, while avoiding KiCad treating a sub-micron
    # source segment that straddles the list seam as self-intersection.
    seam = max(range(len(meaningful)),
               key=lambda index: math.dist(meaningful[index][0],
                                           meaningful[index][-1]))
    meaningful = meaningful[seam:] + meaningful[:seam]
    # KiCad 10 rejects Edge.Cuts segments below 0.2 micrometres.  Remove the
    # endpoint after each such source segment and bridge its neighbours.  This
    # is the sole geometric limitation in the fabrication contour; the maximum
    # source-vertex deviation is computed/documented independently.
    vertices = [segment[0] for segment in meaningful]
    while True:
        short = next((index for index in range(len(vertices))
                      if math.dist(vertices[index],
                                   vertices[(index + 1) % len(vertices)]) <
                      KICAD_MIN_EDGE_SEGMENT_MM), None)
        if short is None:
            break
        del vertices[(short + 1) % len(vertices)]

    # Every retained Illustrator cubic is exactly linear. Reparameterize each
    # bridge with 1/3 and 2/3 controls on the same line locus to avoid singular
    # endpoint derivatives in KiCad's curve flattener.
    regularized = []
    for index, start in enumerate(vertices):
        end = vertices[(index + 1) % len(vertices)]
        one_third = ((2.0 * start[0] + end[0]) / 3.0,
                     (2.0 * start[1] + end[1]) / 3.0)
        two_thirds = ((start[0] + 2.0 * end[0]) / 3.0,
                      (start[1] + 2.0 * end[1]) / 3.0)
        regularized.append((start, one_third, two_thirds, end))
    return tuple(regularized)


def _point_segment_distance(point: Point, start: Point, end: Point) -> float:
    dx, dy = end[0] - start[0], end[1] - start[1]
    length_squared = dx * dx + dy * dy
    if length_squared == 0.0:
        return math.dist(point, start)
    t = max(0.0, min(1.0, ((point[0] - start[0]) * dx +
                           (point[1] - start[1]) * dy) / length_squared))
    return math.dist(point, (start[0] + t * dx, start[1] + t * dy))


def _source_fidelity(component: tuple[int, ...],
                     by_index: dict[int, SplineRecord],
                     fabrication: tuple[Segment, ...]) -> tuple[int, float]:
    source = tuple(segment for index in component
                   for segment in by_index[index].segments
                   if any(math.dist(segment[0], point) > 1e-12
                          for point in segment[1:]))
    maximum = max(min(_point_segment_distance(point, segment[0], segment[-1])
                      for segment in fabrication)
                  for source_segment in source for point in source_segment)
    return len(source), maximum


def _name_apertures(closed_components: tuple[tuple[int, ...], ...],
                    by_index: dict[int, SplineRecord]) -> tuple[Aperture, ...]:
    derived = []
    for component in closed_components:
        x, y, width, height = _component_geometry(component, by_index)
        derived.append({"component": component, "x": x, "y": y,
                        "width": width, "height": height})

    slots = [item for item in derived
             if max(item["width"], item["height"]) /
             min(item["width"], item["height"]) > 2.0]
    roundish = [item for item in derived if item not in slots]
    if len(slots) != 2 or len(roundish) != 13:
        raise ValueError("official DXF does not resolve to 13 apertures and 2 slots")

    slots.sort(key=lambda item: item["x"])
    names: dict[tuple[int, ...], tuple[str, str]] = {
        slots[0]["component"]: ("FADER_LEFT", "slot"),
        slots[1]["component"]: ("FADER_RIGHT", "slot"),
    }

    by_width = sorted(roundish, key=lambda item: item["width"])
    mounts, jacks, knobs = by_width[:5], by_width[5:7], by_width[7:]
    if not (len(mounts) == 5 and len(jacks) == 2 and len(knobs) == 6):
        raise ValueError("official aperture size ranks are incomplete")

    for name, item in zip(("JACK_UPPER", "JACK_LOWER"),
                          sorted(jacks, key=lambda item: item["y"])):
        names[item["component"]] = (name, "aperture")

    ordered_y = sorted(knobs, key=lambda item: item["y"])
    gaps = [ordered_y[index + 1]["y"] - ordered_y[index]["y"]
            for index in range(len(ordered_y) - 1)]
    split = gaps.index(max(gaps)) + 1
    upper, lower = ordered_y[:split], ordered_y[split:]
    if len(upper) != 4 or len(lower) != 2:
        raise ValueError("official knob aperture rows are not 4 + 2")
    for name, item in zip(("KNOB_1", "KNOB_2", "KNOB_3", "KNOB_4"),
                          sorted(upper, key=lambda item: item["x"])):
        names[item["component"]] = (name, "aperture")
    for name, item in zip(("KNOB_5", "KNOB_6"),
                          sorted(lower, key=lambda item: item["x"])):
        names[item["component"]] = (name, "aperture")

    unused = set(item["component"] for item in mounts)
    for slot, side in zip(slots, ("L", "R")):
        nearest = sorted((item for item in mounts if item["component"] in unused),
                         key=lambda item: math.dist((item["x"], item["y"]),
                                                    (slot["x"], slot["y"])))[:2]
        if len(nearest) != 2:
            raise ValueError("official fader mount pairing is incomplete")
        for item, vertical in zip(sorted(nearest, key=lambda item: item["y"]),
                                  ("TOP", "BOTTOM")):
            names[item["component"]] = (f"MOUNT_FADER_{side}_{vertical}",
                                         "aperture")
            unused.remove(item["component"])
    if len(unused) != 1:
        raise ValueError("official upper-right mount is ambiguous")
    names[unused.pop()] = ("MOUNT_UPPER_RIGHT", "aperture")

    inventory = []
    for item in derived:
        reference, kind = names[item["component"]]
        segments = _ordered_component_segments(item["component"], by_index)
        source_count, maximum = _source_fidelity(item["component"], by_index,
                                                  segments)
        inventory.append(Aperture(reference, kind, (item["x"], item["y"]),
                                  item["width"], item["height"],
                                  item["component"], segments, source_count,
                                  maximum))
    return tuple(sorted(inventory, key=lambda item: item.reference))


def analyze_dxf(path: Path) -> SourceGeometry:
    raw = _raw_records(path)
    outer_raw = raw[-1]
    if not (outer_raw.flags & 1):
        raise ValueError("official outer spline is not closed")
    min_x, _min_y, max_x, max_y = segments_bbox(outer_raw.segments)

    transformed = []
    for record in raw:
        segments = tuple(tuple(((x - min_x) * MM_PER_INCH,
                                (max_y - y) * MM_PER_INCH)
                               for x, y in segment)
                         for segment in record.segments)
        transformed.append(SplineRecord(record.index, record.flags, segments))
    records = tuple(transformed)
    outer = records[-1]
    reference_records = (outer,) + records[:-1]
    aperture_records = records[:-1]
    by_index = {record.index: record for record in aperture_records}
    components = _components(aperture_records)
    closed_components = tuple(component for component in components
                              if _closed(component, by_index))
    open_indices = tuple(sorted(index for component in components
                                if not _closed(component, by_index)
                                for index in component))
    apertures = _name_apertures(closed_components, by_index)
    outer_box = segments_bbox(outer.segments)
    return SourceGeometry(records, reference_records, outer, apertures,
                          open_indices, (min_x, max_y),
                          outer_box[2] - outer_box[0],
                          outer_box[3] - outer_box[1])
