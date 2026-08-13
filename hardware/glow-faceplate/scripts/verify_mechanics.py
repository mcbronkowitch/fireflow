#!/usr/bin/env python3
"""Independently verify the faceplate against the pinned official DXF."""

from __future__ import annotations

import argparse
import ast
import hashlib
import math
import re
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path


DXF_SHA256 = "55c84aa39e8d4e1484ae05a5145a545dd4749097129adb35a6fdb9da1ff606a0"
MM_PER_INCH = 25.4
DXF_JOIN_TOLERANCE_MM = 0.000010
KICAD_ROUNDING_TOLERANCE_MM = 0.000002
KICAD_MIN_EDGE_SEGMENT_MM = 0.000200
VCV_MAX_ERROR_MM = 0.25
FORBIDDEN = re.compile(
    r"\b(SYNTHUX|TOUCH|MOTION|SHAPE|ENERGY|SPACE|FADER|SWITCH)\b",
    re.IGNORECASE,
)

Point = tuple[float, float]
Segment = tuple[Point, Point, Point, Point]


@dataclass(frozen=True)
class DxfRecord:
    index: int
    flags: int
    segments: tuple[Segment, ...]


@dataclass(frozen=True)
class ExpectedAperture:
    name: str
    kind: str
    centre: Point
    width: float
    height: float
    record_indices: tuple[int, ...]
    segments: tuple[Segment, ...]
    source_segment_count: int
    max_kicad_deviation: float

    @property
    def board_reference(self) -> str:
        return "DXF_" + "_".join(f"{index:02d}" for index in self.record_indices)


@dataclass(frozen=True)
class OfficialGeometry:
    outer: DxfRecord
    reference_records: tuple[DxfRecord, ...]
    apertures: tuple[ExpectedAperture, ...]
    open_record_indices: tuple[int, ...]

    @property
    def reference_segments(self) -> tuple[Segment, ...]:
        return tuple(segment for record in self.reference_records
                     for segment in record.segments)


def _tokenize_sexpr(text: str) -> list[str]:
    return re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+', text)


def _parse_sexpr(text: str):
    tokens = _tokenize_sexpr(text)
    position = 0

    def parse_one():
        nonlocal position
        if position >= len(tokens):
            raise ValueError("unexpected end of KiCad s-expression")
        token = tokens[position]
        position += 1
        if token == "(":
            output = []
            while position < len(tokens) and tokens[position] != ")":
                output.append(parse_one())
            if position >= len(tokens):
                raise ValueError("unclosed KiCad s-expression")
            position += 1
            return output
        if token == ")":
            raise ValueError("unexpected ')' in KiCad s-expression")
        if token.startswith('"'):
            return bytes(token[1:-1], "utf-8").decode("unicode_escape")
        return token

    root = parse_one()
    if position != len(tokens):
        raise ValueError("trailing data after KiCad s-expression")
    return root


def _children(node, tag: str):
    if not isinstance(node, list):
        return []
    return [child for child in node[1:]
            if isinstance(child, list) and child and child[0] == tag]


def _child(node, tag: str):
    matches = _children(node, tag)
    return matches[0] if matches else None


def _walk(node):
    if isinstance(node, list):
        yield node
        for child in node[1:]:
            yield from _walk(child)


def _xy(node) -> Point:
    return float(node[1]), float(node[2])


def _layer(node) -> str | None:
    layer = _child(node, "layer")
    return layer[1] if layer and len(layer) > 1 else None


def _curve_points(curve) -> tuple[Point, ...]:
    points = _child(curve, "pts")
    return tuple(_xy(item) for item in _children(points, "xy")) if points else ()


def _edge_segment(node) -> Segment | None:
    if node[0] == "gr_curve":
        points = _curve_points(node)
        return points if len(points) == 4 else None
    if node[0] == "gr_line":
        start, end = _child(node, "start"), _child(node, "end")
        if not start or not end:
            return None
        a, d = _xy(start), _xy(end)
        return (a, ((2.0 * a[0] + d[0]) / 3.0,
                    (2.0 * a[1] + d[1]) / 3.0),
                   ((a[0] + 2.0 * d[0]) / 3.0,
                    (a[1] + 2.0 * d[1]) / 3.0), d)
    return None


def _uuid(node) -> str | None:
    item = _child(node, "uuid")
    return item[1] if item and len(item) > 1 else None


def _dxf_pairs(path: Path) -> list[tuple[int, str]]:
    lines = path.read_text(encoding="cp1252").splitlines()
    if len(lines) % 2:
        raise ValueError("DXF group-code stream has an odd line count")
    return [(int(lines[index].strip()), lines[index + 1].strip())
            for index in range(0, len(lines), 2)]


def _raw_dxf_records(path: Path) -> tuple[DxfRecord, ...]:
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != DXF_SHA256:
        raise ValueError(f"official DXF hash {digest} does not match provenance")
    pairs = _dxf_pairs(path)
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
            raise ValueError("official SPLINE is not a cubic Bezier chain")
        controls = tuple(zip(xs, ys))
        segments = tuple(tuple(controls[index:index + 4])
                         for index in range(0, len(controls) - 1, 3))
        records.append(DxfRecord(len(records), flags, segments))
    if len(records) != 27:
        raise ValueError(f"official DXF has {len(records)} spline records, want 27")
    return tuple(records)


def _cubic(p0: float, p1: float, p2: float, p3: float, t: float) -> float:
    u = 1.0 - t
    return (u**3 * p0 + 3.0 * u * u * t * p1
            + 3.0 * u * t * t * p2 + t**3 * p3)


def _coordinate_extrema(values: tuple[float, float, float, float]):
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


def _segments_bbox(segments) -> tuple[float, float, float, float]:
    xs, ys = [], []
    for segment in segments:
        xs.extend(_coordinate_extrema(tuple(point[0] for point in segment)))
        ys.extend(_coordinate_extrema(tuple(point[1] for point in segment)))
    return min(xs), min(ys), max(xs), max(ys)


def _dxf_components(records: tuple[DxfRecord, ...]) -> tuple[tuple[int, ...], ...]:
    adjacency = {record.index: set() for record in records}
    for offset, left in enumerate(records):
        left_ends = (left.segments[0][0], left.segments[-1][-1])
        for right in records[offset + 1:]:
            right_ends = (right.segments[0][0], right.segments[-1][-1])
            if min(math.dist(a, b) for a in left_ends for b in right_ends) \
                    <= DXF_JOIN_TOLERANCE_MM:
                adjacency[left.index].add(right.index)
                adjacency[right.index].add(left.index)
    seen, result = set(), []
    for record in records:
        if record.index in seen:
            continue
        pending, component = [record.index], []
        while pending:
            index = pending.pop()
            if index in seen:
                continue
            seen.add(index)
            component.append(index)
            pending.extend(adjacency[index] - seen)
        result.append(tuple(sorted(component)))
    return tuple(result)


def _component_closed(component: tuple[int, ...], by_index) -> bool:
    endpoints = []
    for index in component:
        record = by_index[index]
        endpoints.extend((record.segments[0][0], record.segments[-1][-1]))
    return all(any(other_index != index and
                   math.dist(point, other) <= DXF_JOIN_TOLERANCE_MM
                   for other_index, other in enumerate(endpoints))
               for index, point in enumerate(endpoints))


def _reverse_segments(segments: tuple[Segment, ...]) -> tuple[Segment, ...]:
    return tuple(tuple(reversed(segment)) for segment in reversed(segments))


def _ordered_component_segments(component: tuple[int, ...], by_index) \
        -> tuple[Segment, ...]:
    first = min(component)
    ordered = list(by_index[first].segments)
    remaining = set(component) - {first}
    while remaining:
        end = ordered[-1][-1]
        match = None
        for index in sorted(remaining):
            record = by_index[index]
            if math.dist(end, record.segments[0][0]) <= DXF_JOIN_TOLERANCE_MM:
                match = index, record.segments
                break
            if math.dist(end, record.segments[-1][-1]) <= DXF_JOIN_TOLERANCE_MM:
                match = index, _reverse_segments(record.segments)
                break
        if match is None:
            raise ValueError(f"DXF component {component} is not one chain")
        index, segments = match
        ordered.extend(segments)
        remaining.remove(index)
    if math.dist(ordered[0][0], ordered[-1][-1]) > DXF_JOIN_TOLERANCE_MM:
        raise ValueError(f"DXF component {component} is open")
    meaningful = tuple(segment for segment in ordered
                       if any(math.dist(segment[0], point) > 1e-12
                              for point in segment[1:]))
    if math.dist(meaningful[0][0], meaningful[-1][-1]) > \
            DXF_JOIN_TOLERANCE_MM:
        raise ValueError(f"DXF component {component} changed after no-op removal")
    seam = max(range(len(meaningful)),
               key=lambda index: math.dist(meaningful[index][0],
                                           meaningful[index][-1]))
    meaningful = meaningful[seam:] + meaningful[:seam]
    vertices = [segment[0] for segment in meaningful]
    while True:
        short = next((index for index in range(len(vertices))
                      if math.dist(vertices[index],
                                   vertices[(index + 1) % len(vertices)]) <
                      KICAD_MIN_EDGE_SEGMENT_MM), None)
        if short is None:
            break
        del vertices[(short + 1) % len(vertices)]
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


def _derive_names(components: tuple[tuple[int, ...], ...], by_index):
    items = []
    for component in components:
        boxes = [_segments_bbox(by_index[index].segments) for index in component]
        bbox = (min(box[0] for box in boxes), min(box[1] for box in boxes),
                max(box[2] for box in boxes), max(box[3] for box in boxes))
        items.append({
            "component": component,
            "x": (bbox[0] + bbox[2]) / 2.0,
            "y": (bbox[1] + bbox[3]) / 2.0,
            "width": bbox[2] - bbox[0],
            "height": bbox[3] - bbox[1],
        })
    slots = [item for item in items
             if max(item["width"], item["height"]) /
             min(item["width"], item["height"]) > 2.0]
    roundish = [item for item in items if item not in slots]
    if len(slots) != 2 or len(roundish) != 13:
        raise ValueError("DXF topology does not yield 13 apertures and 2 slots")
    slots.sort(key=lambda item: item["x"])
    names = {slots[0]["component"]: ("FADER_LEFT", "slot"),
             slots[1]["component"]: ("FADER_RIGHT", "slot")}
    by_width = sorted(roundish, key=lambda item: item["width"])
    mounts, jacks, knobs = by_width[:5], by_width[5:7], by_width[7:]
    for name, item in zip(("JACK_UPPER", "JACK_LOWER"),
                          sorted(jacks, key=lambda item: item["y"])):
        names[item["component"]] = (name, "aperture")
    ordered_y = sorted(knobs, key=lambda item: item["y"])
    gaps = [ordered_y[index + 1]["y"] - ordered_y[index]["y"]
            for index in range(5)]
    split = gaps.index(max(gaps)) + 1
    upper, lower = ordered_y[:split], ordered_y[split:]
    if len(upper) != 4 or len(lower) != 2:
        raise ValueError("DXF knob rows do not resolve to 4 + 2")
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
        for item, vertical in zip(sorted(nearest, key=lambda item: item["y"]),
                                  ("TOP", "BOTTOM")):
            names[item["component"]] = (f"MOUNT_FADER_{side}_{vertical}",
                                         "aperture")
            unused.remove(item["component"])
    if len(unused) != 1:
        raise ValueError("DXF upper-right mount is ambiguous")
    names[unused.pop()] = ("MOUNT_UPPER_RIGHT", "aperture")
    output = []
    for item in items:
        component = item["component"]
        segments = _ordered_component_segments(component, by_index)
        source = tuple(segment for index in component
                       for segment in by_index[index].segments
                       if any(math.dist(segment[0], point) > 1e-12
                              for point in segment[1:]))
        maximum = max(min(_point_segment_distance(point, segment[0], segment[-1])
                          for segment in segments)
                      for source_segment in source for point in source_segment)
        output.append(ExpectedAperture(names[component][0], names[component][1],
                                       (item["x"], item["y"]), item["width"],
                                       item["height"], component, segments,
                                       len(source), maximum))
    return tuple(output)


def _official_geometry(path: Path) -> OfficialGeometry:
    raw = _raw_dxf_records(path)
    outer_raw = raw[-1]
    if not (outer_raw.flags & 1):
        raise ValueError("official outer spline is not closed")
    min_x, _min_y, _max_x, max_y = _segments_bbox(outer_raw.segments)
    transformed = []
    for record in raw:
        segments = tuple(tuple(((x - min_x) * MM_PER_INCH,
                                (max_y - y) * MM_PER_INCH)
                               for x, y in segment)
                         for segment in record.segments)
        transformed.append(DxfRecord(record.index, record.flags, segments))
    records = tuple(transformed)
    outer = records[-1]
    aperture_records = records[:-1]
    by_index = {record.index: record for record in aperture_records}
    components = _dxf_components(aperture_records)
    closed = tuple(component for component in components
                   if _component_closed(component, by_index))
    open_indices = tuple(sorted(index for component in components
                                if not _component_closed(component, by_index)
                                for index in component))
    apertures = _derive_names(closed, by_index)
    return OfficialGeometry(outer, (outer,) + aperture_records,
                            apertures, open_indices)


def _close(a: Point, b: Point) -> bool:
    return math.dist(a, b) <= KICAD_ROUNDING_TOLERANCE_MM


def _normal_curve_key(points):
    # Curves serialize controls at 1 nm; line items serialize endpoints and
    # their verifier-side 1/3 controls are reconstructed in binary float.
    # Five decimal places is only a lookup signature: matched chains are still
    # checked point-by-point at the 2 nm serialization tolerance below.
    forward = tuple((round(x, 5), round(y, 5)) for x, y in points)
    reverse = tuple(reversed(forward))
    return min(forward, reverse)


def _directed_curve_key(points):
    return tuple((round(x, 6), round(y, 6))
                 for x, y in (points[0], points[-1]))


def _endpoint_key(point: Point) -> Point:
    return round(point[0], 6), round(point[1], 6)


def _curve_components(curves: list[Segment]) -> tuple[tuple[int, ...], ...]:
    endpoints: dict[Point, list[int]] = {}
    for index, curve in enumerate(curves):
        endpoints.setdefault(_endpoint_key(curve[0]), []).append(index)
        endpoints.setdefault(_endpoint_key(curve[-1]), []).append(index)
    adjacency = {index: set() for index in range(len(curves))}
    for members in endpoints.values():
        for left in members:
            adjacency[left].update(right for right in members if right != left)
    seen, components = set(), []
    for index in range(len(curves)):
        if index in seen:
            continue
        pending, component = [index], []
        while pending:
            current = pending.pop()
            if current in seen:
                continue
            seen.add(current)
            component.append(current)
            pending.extend(adjacency[current] - seen)
        components.append(tuple(sorted(component)))
    return tuple(components)


def _ordered_directed_chain(component: tuple[int, ...], curves: list[Segment]):
    starts: dict[Point, list[int]] = {}
    ends: dict[Point, list[int]] = {}
    for index in component:
        starts.setdefault(_endpoint_key(curves[index][0]), []).append(index)
        ends.setdefault(_endpoint_key(curves[index][-1]), []).append(index)
    endpoint_keys = set(starts) | set(ends)
    closed = all(len(starts.get(key, ())) == 1 and
                 len(ends.get(key, ())) == 1 for key in endpoint_keys)
    if not closed:
        return (), False
    first = min(component)
    ordered_indices, seen = [], set()
    current = first
    while current not in seen:
        seen.add(current)
        ordered_indices.append(current)
        following = starts.get(_endpoint_key(curves[current][-1]), ())
        if len(following) != 1:
            return (), False
        current = following[0]
    if current != first or seen != set(component):
        return (), False
    return tuple(curves[index] for index in ordered_indices), True


def _curve_signature(segments) -> tuple:
    return tuple(sorted(_directed_curve_key(segment) for segment in segments))


def _point_on_cubic(segment: Segment, t: float) -> Point:
    return tuple(_cubic(*(point[axis] for point in segment), t)
                 for axis in (0, 1))


def _polyline(segments, samples_per_curve=4) -> tuple[Point, ...]:
    points = [segments[0][0]]
    for segment in segments:
        points.extend(_point_on_cubic(segment, sample / samples_per_curve)
                      for sample in range(1, samples_per_curve + 1))
    return tuple(points)


def _signed_area(segments) -> float:
    points = _polyline(segments)
    return 0.5 * sum(a[0] * b[1] - b[0] * a[1]
                     for a, b in zip(points, points[1:]))


def _orientation(a: Point, b: Point, c: Point) -> float:
    return (b[0] - a[0]) * (c[1] - a[1]) - \
           (b[1] - a[1]) * (c[0] - a[0])


def _line_segments_intersect(a: Point, b: Point, c: Point, d: Point) -> bool:
    ab_c, ab_d = _orientation(a, b, c), _orientation(a, b, d)
    cd_a, cd_b = _orientation(c, d, a), _orientation(c, d, b)
    return ((ab_c > 1e-10 and ab_d < -1e-10 or
             ab_c < -1e-10 and ab_d > 1e-10) and
            (cd_a > 1e-10 and cd_b < -1e-10 or
             cd_a < -1e-10 and cd_b > 1e-10))


def _self_intersects(polyline: tuple[Point, ...]) -> bool:
    count = len(polyline) - 1
    for left in range(count):
        for right in range(left + 1, count):
            if right in (left, left + 1) or (left == 0 and right == count - 1):
                continue
            if _line_segments_intersect(polyline[left], polyline[left + 1],
                                        polyline[right], polyline[right + 1]):
                return True
    return False


def _polylines_intersect(left: tuple[Point, ...],
                         right: tuple[Point, ...]) -> bool:
    for a, b in zip(left, left[1:]):
        for c, d in zip(right, right[1:]):
            if _line_segments_intersect(a, b, c, d):
                return True
    return False


def _point_inside(point: Point, polygon: tuple[Point, ...]) -> bool:
    x, y = point
    inside = False
    for (x0, y0), (x1, y1) in zip(polygon, polygon[1:]):
        if (y0 > y) != (y1 > y):
            crossing = (x1 - x0) * (y - y0) / (y1 - y0) + x0
            if x < crossing:
                inside = not inside
    return inside


def _topology_errors(contours: dict[str, tuple[Segment, ...]]):
    errors = []
    outer = _polyline(contours["outer"])
    if _self_intersects(outer):
        errors.append("outer Edge.Cuts contour self-intersects")
    internals = {name: _polyline(segments) for name, segments in contours.items()
                 if name != "outer"}
    for name, polyline in internals.items():
        if _self_intersects(polyline):
            errors.append(f"internal contour {name} self-intersects")
        if not _point_inside(polyline[0], outer):
            errors.append(f"internal contour {name} is outside the outer outline")
        if _polylines_intersect(polyline, outer):
            errors.append(f"internal contour {name} intersects the outer outline")
    names = sorted(internals)
    for left_index, left_name in enumerate(names):
        left_box = _segments_bbox(contours[left_name])
        for right_name in names[left_index + 1:]:
            right_box = _segments_bbox(contours[right_name])
            if (left_box[2] < right_box[0] or right_box[2] < left_box[0] or
                    left_box[3] < right_box[1] or right_box[3] < left_box[1]):
                continue
            if _polylines_intersect(internals[left_name], internals[right_name]):
                errors.append(f"internal contours {left_name} and {right_name} "
                              "intersect")
    return errors


def _edge_errors(root, official: OfficialGeometry):
    errors = []
    nodes = [node for node in root[1:]
             if isinstance(node, list) and node and
             node[0] in ("gr_curve", "gr_line") and
             _layer(node) == "Edge.Cuts"]
    parsed = [_edge_segment(node) for node in nodes]
    if not nodes:
        return ["Edge.Cuts has no cubic contours"]
    if any(segment is None for segment in parsed):
        errors.append("Edge.Cuts contains an invalid curve/line")
    curves = [segment for segment in parsed if segment is not None]
    identifiers = [_uuid(node) for node, segment in zip(nodes, parsed)
                   if segment is not None]
    if any(identifier is None for identifier in identifiers) or \
            len(identifiers) != len(set(identifiers)):
        errors.append("Edge.Cuts curve IDs are missing or unstable/duplicated")

    keys = [_normal_curve_key(points) for points in curves]
    if len(keys) != len(set(keys)):
        errors.append("duplicate internal contour/curve geometry on Edge.Cuts")

    expected = [("outer", official.outer.segments)] + [
        (item.name, item.segments)
        for item in sorted(official.apertures,
                           key=lambda item: item.record_indices)
    ]
    expected_signatures = {name: _curve_signature(segments)
                           for name, segments in expected}
    components = _curve_components(curves)
    actual = []
    for component in components:
        segments = tuple(curves[index] for index in component)
        ordered, closed = _ordered_directed_chain(component, curves)
        actual.append({"component": component, "segments": segments,
                       "ordered": ordered, "closed": closed,
                       "signature": _curve_signature(segments)})

    matched: dict[str, tuple[Segment, ...]] = {}
    remaining = set(range(len(actual)))
    for name, segments in expected:
        signature = expected_signatures[name]
        exact = next((index for index in remaining
                      if actual[index]["signature"] == signature), None)
        if exact is not None:
            item = actual[exact]
            remaining.remove(exact)
        else:
            wanted_keys = set(signature)
            ranked = sorted(remaining,
                            key=lambda index: len(wanted_keys &
                                                  set(actual[index]["signature"])),
                            reverse=True)
            best = ranked[0] if ranked else None
            overlap = (len(wanted_keys & set(actual[best]["signature"]))
                       if best is not None else 0)
            if best is None or overlap < max(1, len(signature) // 2):
                label = "outer outline" if name == "outer" else \
                    f"internal contour {name}"
                errors.append(f"missing {label}")
                continue
            item = actual[best]
            remaining.remove(best)
            label = "outer contour" if name == "outer" else \
                f"internal contour {name}"
            errors.append(f"{label} differs from the official DXF")

        if not item["closed"]:
            label = "outer contour" if name == "outer" else \
                f"open internal contour {name}"
            errors.append(f"{label} is not one closed directed chain")
            continue
        ordered = item["ordered"]
        actual_by_key = {_directed_curve_key(segment): segment
                         for segment in ordered}
        for source_segment in segments:
            actual_segment = actual_by_key.get(_directed_curve_key(source_segment))
            if actual_segment is None or any(not _close(a, b)
                                             for a, b in zip(actual_segment,
                                                             source_segment)):
                label = "outer contour" if name == "outer" else \
                    f"internal contour {name}"
                errors.append(f"{label} differs from the official DXF")
                break
        wanted_sign = _signed_area(segments)
        actual_sign = _signed_area(ordered)
        if wanted_sign * actual_sign <= 0.0:
            label = "outer contour" if name == "outer" else \
                f"internal contour {name}"
            errors.append(f"{label} orientation differs from the DXF")
        matched[name] = ordered

    if remaining:
        errors.append(f"Edge.Cuts has {len(remaining)} unexpected contour(s)")
    if len(components) != 16:
        errors.append(f"Edge.Cuts has {len(components)} connected contours, want "
                      "1 outer plus 15 internal")

    if "outer" in matched:
        actual_box = _segments_bbox(matched["outer"])
        source_box = _segments_bbox(official.outer.segments)
        actual_size = (actual_box[2] - actual_box[0], actual_box[3] - actual_box[1])
        source_size = (source_box[2] - source_box[0], source_box[3] - source_box[1])
        if not _close(actual_size, source_size):
            errors.append("outer Edge.Cuts dimensions differ from the DXF")
        if not _close((actual_box[0], actual_box[1]), (0.0, 0.0)):
            errors.append("official upper-left datum is not board (0, 0)")
    if len(matched) == 16:
        errors.extend(_topology_errors(matched))
    return errors


def _is_locked(curve) -> bool:
    return any(item == "locked" or
               (isinstance(item, list) and item and item[0] == "locked")
               for item in curve[1:])


def _reference_errors(root, official: OfficialGeometry):
    errors = []
    curves = [node for node in _children(root, "gr_curve")
              if _layer(node) == "Dwgs.User"]
    expected = official.reference_segments
    if len(curves) != len(expected):
        errors.append(f"mechanical reference has {len(curves)} curves, want "
                      f"all {len(expected)} curves from 27 DXF records")
    unlocked = [index for index, curve in enumerate(curves) if not _is_locked(curve)]
    if unlocked:
        errors.append(f"mechanical reference curves are not locked: {unlocked[:8]}")
    for index, (curve, source) in enumerate(zip(curves, expected)):
        points = _curve_points(curve)
        if len(points) != 4 or any(not _close(a, b)
                                   for a, b in zip(points, source)):
            errors.append(f"mechanical reference curve {index} differs from DXF")
            break

    reference_ids = [_uuid(curve) for curve in curves]
    if any(identifier is None for identifier in reference_ids):
        errors.append("mechanical reference curve is missing a UUID")
    groups = [node for node in _children(root, "group")
              if len(node) > 1 and node[1] == "mechanical_reference"]
    if len(groups) != 1:
        errors.append("mechanical_reference group is missing or duplicated")
    else:
        members = _child(groups[0], "members")
        member_ids = members[1:] if members else []
        if (len(member_ids) != len(set(member_ids)) or
                set(member_ids) != set(reference_ids)):
            errors.append("mechanical_reference group membership is incomplete")
    return errors


def _npth_errors(root):
    count = 0
    for footprint in _children(root, "footprint"):
        count += sum(len(pad) >= 3 and pad[2] == "np_thru_hole"
                     for pad in _children(footprint, "pad"))
    if count:
        return [f"found {count} approximate NPTH mechanic(s); exact internal "
                "Edge.Cuts are the sole fabrication authority"]
    return []


def _silk_errors(root):
    errors = []
    for text in _children(root, "gr_text"):
        if _layer(text) != "F.SilkS":
            continue
        effects = _child(text, "effects")
        font = _child(effects, "font") if effects else None
        size = _child(font, "size") if font else None
        thickness = _child(font, "thickness") if font else None
        if not size or len(size) < 3 or min(float(size[1]), float(size[2])) < 0.8:
            errors.append("front silkscreen text height is below 0.8 mm")
        if not thickness or float(thickness[1]) < 0.10:
            errors.append("front silkscreen text thickness is below 0.10 mm")
    return errors


def _fabrication_text_errors(root):
    errors = []
    for node in _walk(root):
        if not node or node[0] not in ("gr_text", "gr_text_box", "fp_text",
                                       "fp_text_box", "property"):
            continue
        if _layer(node) not in ("F.Cu", "F.Mask", "F.SilkS"):
            continue
        content = " ".join(str(item) for item in node[1:]
                           if not isinstance(item, list))
        match = FORBIDDEN.search(content)
        if match:
            errors.append("forbidden protected/provisional fabrication text "
                          f"on {_layer(node)}: {match.group(0)}")
    return errors


def _setup_errors(root):
    setup = _child(root, "setup")
    max_error = _child(setup, "max_error") if setup else None
    mask_sliver = _child(setup, "solder_mask_min_width") if setup else None
    errors = []
    if (not max_error or len(max_error) < 2 or
            abs(float(max_error[1]) - 0.005) > KICAD_ROUNDING_TOLERANCE_MM):
        errors.append("board source does not set the 0.005 mm conservative "
                      "curve-rendering approximation error")
    if (not mask_sliver or len(mask_sliver) < 2 or
            abs(float(mask_sliver[1]) - 0.20) > KICAD_ROUNDING_TOLERANCE_MM):
        errors.append("board source does not set the 0.20 mm conservative mask sliver")
    return errors


VCV_NAME_MAP = {
    "JACK_UPPER": "out_l",
    "JACK_LOWER": "out_r",
    "KNOB_1": "knob_s31",
    "KNOB_2": "knob_s32",
    "KNOB_3": "knob_s33",
    "KNOB_4": "knob_s34",
    "KNOB_5": "knob_s30",
    "KNOB_6": "knob_s35",
    "FADER_LEFT": "fader_s36",
    "FADER_RIGHT": "fader_s37",
}


def _vcv_centres(path: Path) -> dict[str, Point]:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    for node in tree.body:
        target = None
        value = None
        if isinstance(node, ast.Assign) and len(node.targets) == 1:
            target, value = node.targets[0], node.value
        elif isinstance(node, ast.AnnAssign):
            target, value = node.target, node.value
        if not isinstance(target, ast.Name) or target.id != "CONTROL_CENTRES_MM":
            continue
        if (isinstance(value, ast.Call) and value.args and
                isinstance(value.func, ast.Name) and
                value.func.id == "MappingProxyType"):
            value = value.args[0]
        result = ast.literal_eval(value)
        if not isinstance(result, dict):
            break
        return {str(name): (float(point[0]), float(point[1]))
                for name, point in result.items()}
    raise ValueError("CONTROL_CENTRES_MM was not found in VCV geometry source")


def _vcv_fit(official: OfficialGeometry, path: Path):
    vcv = _vcv_centres(path)
    mechanics = {item.name: item.centre for item in official.apertures}
    source = [mechanics[name] for name in VCV_NAME_MAP]
    target = [vcv[VCV_NAME_MAP[name]] for name in VCV_NAME_MAP]
    source_mean = (sum(point[0] for point in source) / len(source),
                   sum(point[1] for point in source) / len(source))
    target_mean = (sum(point[0] for point in target) / len(target),
                   sum(point[1] for point in target) / len(target))
    numerator = sum((x - source_mean[0]) * (u - target_mean[0]) +
                    (y - source_mean[1]) * (v - target_mean[1])
                    for (x, y), (u, v) in zip(source, target))
    denominator = sum((x - source_mean[0])**2 + (y - source_mean[1])**2
                      for x, y in source)
    scale = numerator / denominator
    tx = target_mean[0] - scale * source_mean[0]
    ty = target_mean[1] - scale * source_mean[1]
    residuals = {
        name: math.dist((scale * mechanics[name][0] + tx,
                         scale * mechanics[name][1] + ty),
                        vcv[VCV_NAME_MAP[name]])
        for name in VCV_NAME_MAP
    }
    return scale, tx, ty, residuals


def _svg_number(value: float) -> str:
    if abs(value) < 0.0000005:
        value = 0.0
    return f"{value:.6f}".rstrip("0").rstrip(".")


def _canonical_svg_path(record: DxfRecord) -> str:
    start = record.segments[0][0]
    pieces = [f"M {_svg_number(start[0])},{_svg_number(start[1])}"]
    for segment in record.segments:
        pieces.append("C " + " ".join(
            f"{_svg_number(x)},{_svg_number(y)}"
            for x, y in (segment[1], segment[2], segment[3])))
    if record.flags & 1:
        pieces.append("Z")
    return " ".join(pieces)


def _svg_errors(path: Path, official: OfficialGeometry):
    if not path.exists():
        return [f"artwork SVG is missing: {path}"]
    text = path.read_text(encoding="utf-8")
    errors = []
    try:
        root = ET.fromstring(text)
    except ET.ParseError as exc:
        return [f"artwork SVG is not valid XML: {exc}"]
    groups = {node.attrib.get("id"): node for node in root.iter()
              if node.tag.rsplit("}", 1)[-1] == "g" and node.attrib.get("id")}
    for group_name in ("copper_front", "mask_front", "silk_front",
                       "mechanical_reference"):
        if group_name not in groups:
            errors.append(f"artwork SVG is missing group {group_name}")
    source_box = _segments_bbox(official.outer.segments)
    wanted = (source_box[2] - source_box[0], source_box[3] - source_box[1])
    try:
        size = (float(root.attrib["width"].removesuffix("mm")),
                float(root.attrib["height"].removesuffix("mm")))
        view_box = tuple(float(value) for value in root.attrib["viewBox"].split())
    except (KeyError, ValueError):
        size, view_box = (-1.0, -1.0), ()
    if not _close(size, wanted) or len(view_box) != 4 or \
            not _close((view_box[0], view_box[1]), (0.0, 0.0)) or \
            not _close((view_box[2], view_box[3]), wanted):
        errors.append("artwork SVG physical size/viewBox does not match the DXF")
    if root.attrib.get("data-source-sha256") != DXF_SHA256:
        errors.append("artwork SVG does not identify the pinned DXF hash")

    reference = groups.get("mechanical_reference")
    if reference is not None:
        if (reference.attrib.get("data-locked") != "true" or
                reference.attrib.get("data-units") != "mm"):
            errors.append("SVG mechanical_reference lock/unit metadata is invalid")
        paths = [node for node in reference
                 if node.tag.rsplit("}", 1)[-1] == "path"]
        if any("data-dxf-record" not in node.attrib for node in paths):
            errors.append("SVG mechanical_reference contains an unkeyed path")
        records: dict[int, list[ET.Element]] = {}
        for node in paths:
            if "data-dxf-record" not in node.attrib:
                continue
            try:
                index = int(node.attrib["data-dxf-record"])
            except ValueError:
                errors.append("SVG mechanical_reference has an invalid record id")
                continue
            records.setdefault(index, []).append(node)
        expected_records = {record.index: record
                            for record in official.reference_records}
        if set(records) != set(expected_records) or \
                any(len(nodes) != 1 for nodes in records.values()):
            errors.append("SVG mechanical_reference record membership is incomplete")
        for index, record in expected_records.items():
            if index not in records or len(records[index]) != 1:
                continue
            if records[index][0].attrib.get("d") != _canonical_svg_path(record):
                errors.append(f"SVG mechanical_reference record {index} differs "
                              "from the official DXF")
    fabrication = "\n".join(ET.tostring(groups[name], encoding="unicode")
                              for name in ("copper_front", "mask_front",
                                           "silk_front") if name in groups)
    match = FORBIDDEN.search(fabrication)
    if match:
        errors.append("forbidden protected/provisional SVG fabrication text: "
                      f"{match.group(0)}")
    return errors


def _rule_errors(path: Path):
    if not path.exists():
        return [f"vendor-neutral rule source is missing: {path}"]
    text = path.read_text(encoding="utf-8")
    required = ("edge_clearance", "hole_to_hole", "silk_clearance",
                "track_width", "annular_width")
    missing = [token for token in required if token not in text]
    return (["vendor-neutral rule source is incomplete: " + ", ".join(missing)]
            if missing else [])


def main(argv=None) -> int:
    root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser()
    parser.add_argument("--board", type=Path,
                        default=root / "hardware/glow-faceplate/glow-faceplate.kicad_pcb")
    parser.add_argument("--svg", type=Path,
                        default=root / "hardware/glow-faceplate/artwork/glow-faceplate.svg")
    parser.add_argument("--dxf", type=Path,
                        default=root / ".reference/glow-touch2/Simple Touch Faceplate template.dxf")
    parser.add_argument("--vcv-geometry", type=Path,
                        default=root / "host/vcv/res/touch2_geometry.py")
    parser.add_argument("--rules", type=Path,
                        default=root / "hardware/glow-faceplate/glow-faceplate.kicad_dru")
    args = parser.parse_args(argv)
    errors = []
    try:
        official = _official_geometry(args.dxf)
    except (OSError, ValueError) as exc:
        print(f"FAIL -- cannot load authoritative DXF: {exc}")
        return 1
    source_segment_count = sum(item.source_segment_count
                               for item in official.apertures)
    fabrication_segment_count = sum(len(item.segments)
                                    for item in official.apertures)
    maximum_kicad_deviation = max(item.max_kicad_deviation
                                  for item in official.apertures)
    if maximum_kicad_deviation > 0.000105:
        errors.append("KiCad minimum-segment adaptation exceeds 0.000105 mm")
    if not args.board.exists():
        errors.append(f"KiCad board is missing: {args.board}")
    else:
        try:
            board = _parse_sexpr(args.board.read_text(encoding="utf-8"))
            if not board or board[0] != "kicad_pcb":
                raise ValueError("root is not kicad_pcb")
            errors.extend(_edge_errors(board, official))
            errors.extend(_reference_errors(board, official))
            errors.extend(_npth_errors(board))
            errors.extend(_silk_errors(board))
            errors.extend(_fabrication_text_errors(board))
            errors.extend(_setup_errors(board))
        except (OSError, ValueError) as exc:
            errors.append(f"cannot parse KiCad board: {exc}")
    errors.extend(_svg_errors(args.svg, official))
    errors.extend(_rule_errors(args.rules))
    try:
        scale, tx, ty, residuals = _vcv_fit(official, args.vcv_geometry)
        for name, residual in residuals.items():
            if residual > VCV_MAX_ERROR_MM:
                errors.append(f"{name} VCV fiducial error is {residual:.3f} mm, "
                              f"limit {VCV_MAX_ERROR_MM:.2f} mm")
    except (KeyError, OSError, SyntaxError, TypeError, ValueError) as exc:
        errors.append(f"cannot read VCV fiducials: {exc}")
        residuals, scale, tx, ty = {}, 0.0, 0.0, 0.0
    if errors:
        print(f"FAIL ({len(errors)})")
        for error in errors:
            print(f"  - {error}")
        return 1
    source_box = _segments_bbox(official.outer.segments)
    print("PASS -- faceplate mechanics match the official 1:1 DXF")
    print(f"  outline: {source_box[2] - source_box[0]:.6f} x "
          f"{source_box[3] - source_box[1]:.6f} mm, "
          f"closed {len(official.outer.segments)}-curve Edge.Cuts")
    print(f"  reference: 27 records / {len(official.reference_segments)} "
          "locked curves / complete group")
    print(f"  mechanics: 15 source-faithful internal Edge.Cuts contours / "
          f"{fabrication_segment_count} curves from {source_segment_count} "
          "non-degenerate source curves / no approximate NPTHs")
    print(f"  KiCad minimum-segment adaptation: "
          f"{maximum_kicad_deviation * 1000:.6f} micrometres maximum")
    print(f"  VCV comparison: recomputed uniform fit s={scale:.9f}, "
          f"tx={tx:.6f}, ty={ty:.6f}, max={max(residuals.values()):.3f} mm "
          f"(limit {VCV_MAX_ERROR_MM:.2f} mm)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
