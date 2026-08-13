#!/usr/bin/env python3
"""Independently verify the faceplate against the pinned official DXF."""

from __future__ import annotations

import argparse
import ast
import hashlib
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path


DXF_SHA256 = "55c84aa39e8d4e1484ae05a5145a545dd4749097129adb35a6fdb9da1ff606a0"
MM_PER_INCH = 25.4
DXF_JOIN_TOLERANCE_MM = 0.000010
KICAD_ROUNDING_TOLERANCE_MM = 0.000002
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
    return tuple(ExpectedAperture(names[item["component"]][0],
                                  names[item["component"]][1],
                                  (item["x"], item["y"]),
                                  item["width"], item["height"],
                                  item["component"])
                 for item in items)


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
    forward = tuple((round(x, 6), round(y, 6)) for x, y in points)
    reverse = tuple(reversed(forward))
    return min(forward, reverse)


def _edge_errors(root, official: OfficialGeometry):
    errors = []
    curves = [node for node in _children(root, "gr_curve")
              if _layer(node) == "Edge.Cuts"]
    parsed = [_curve_points(curve) for curve in curves]
    expected = official.outer.segments
    if not parsed:
        return ["Edge.Cuts has no cubic outline segments",
                "overall dimensions are unavailable without Edge.Cuts"]
    if any(len(points) != 4 for points in parsed):
        errors.append("Edge.Cuts contains a non-cubic curve")
    valid = [points for points in parsed if len(points) == 4]
    if len({_normal_curve_key(points) for points in valid}) != len(valid):
        errors.append("Edge.Cuts contains duplicate curve segments")
    if len(valid) != len(expected):
        errors.append(f"Edge.Cuts has {len(valid)} curves, want {len(expected)} "
                      "from the official DXF")
    else:
        for index, (actual, source) in enumerate(zip(valid, expected)):
            if any(not _close(a, b) for a, b in zip(actual, source)):
                errors.append(f"Edge.Cuts curve {index} differs from official DXF")
                break
    for index, points in enumerate(valid):
        following = valid[(index + 1) % len(valid)]
        if not _close(points[-1], following[0]):
            errors.append(f"Edge.Cuts is open between curves {index} and "
                          f"{(index + 1) % len(valid)}")
            break
    if valid:
        actual_box = _segments_bbox(valid)
        source_box = _segments_bbox(expected)
        actual_size = (actual_box[2] - actual_box[0], actual_box[3] - actual_box[1])
        source_size = (source_box[2] - source_box[0], source_box[3] - source_box[1])
        if not _close(actual_size, source_size):
            errors.append(f"overall dimensions are {actual_size[0]:.6f} x "
                          f"{actual_size[1]:.6f} mm, source is "
                          f"{source_size[0]:.6f} x {source_size[1]:.6f} mm")
        if not _close((actual_box[0], actual_box[1]), (0.0, 0.0)):
            errors.append("official upper-left datum is not board (0, 0)")
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


def _footprint_reference(footprint) -> str | None:
    for text in _children(footprint, "fp_text"):
        if len(text) >= 3 and text[1] == "reference":
            return text[2]
    return None


def _mechanical_inventory(root):
    inventory = {}
    footprints = {}
    for footprint in _children(root, "footprint"):
        ref = _footprint_reference(footprint)
        at = _child(footprint, "at")
        pads = _children(footprint, "pad")
        if not ref or not at or len(at) < 3 or len(pads) != 1:
            continue
        pad = pads[0]
        if len(pad) < 4 or pad[2] != "np_thru_hole":
            continue
        drill = _child(pad, "drill")
        size = _child(pad, "size")
        if drill and size:
            inventory[ref] = ((_xy(at)), pad[3], drill, size)
            footprints[ref] = footprint
    return inventory, footprints


def _inventory_errors(root, official: OfficialGeometry):
    errors = []
    inventory, footprints = _mechanical_inventory(root)
    expected = {item.board_reference: item for item in official.apertures}
    missing = sorted(set(expected) - set(inventory))
    extra = sorted(set(inventory) - set(expected))
    if missing:
        errors.append(f"missing source-derived NPTH mechanics: {missing}")
    if extra:
        errors.append(f"unexpected NPTH mechanics: {extra}")
    for ref, source in expected.items():
        if ref not in inventory:
            continue
        footprint = footprints[ref]
        attr = _child(footprint, "attr")
        if not attr or "board_only" not in attr[1:]:
            errors.append(f"{ref} is not marked board_only")
        if len(footprint) > 1 and ":" in footprint[1]:
            errors.append(f"{ref} uses a library-qualified footprint id")
        at, shape, drill, size = inventory[ref]
        if shape != "oval" or len(drill) != 4 or drill[1] != "oval":
            errors.append(f"{ref} is not an explicit source-envelope oval NPTH")
            continue
        if not _close(at, source.centre):
            errors.append(f"{ref} centre {at} does not match DXF source "
                          f"{source.centre}")
        actual = (float(drill[2]), float(drill[3]))
        pad_size = (float(size[1]), float(size[2]))
        wanted = (source.width, source.height)
        if not _close(actual, wanted) or not _close(pad_size, wanted):
            errors.append(f"{ref} source envelope is {actual[0]:.6f} x "
                          f"{actual[1]:.6f} mm, want {wanted[0]:.6f} x "
                          f"{wanted[1]:.6f} mm")
    return errors


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
    mask_sliver = _child(setup, "solder_mask_min_width") if setup else None
    if (not mask_sliver or len(mask_sliver) < 2 or
            abs(float(mask_sliver[1]) - 0.20) > KICAD_ROUNDING_TOLERANCE_MM):
        return ["board source does not set the 0.20 mm conservative mask sliver"]
    return []


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


def _svg_errors(path: Path, official: OfficialGeometry):
    if not path.exists():
        return [f"artwork SVG is missing: {path}"]
    text = path.read_text(encoding="utf-8")
    errors = []
    for group in ("copper_front", "mask_front", "silk_front",
                  "mechanical_reference"):
        if not re.search(rf'<g\b[^>]*\bid="{re.escape(group)}"', text):
            errors.append(f"artwork SVG is missing group {group}")
    svg = re.search(r'<svg\b([^>]*)>', text, re.DOTALL)
    source_box = _segments_bbox(official.outer.segments)
    wanted = (source_box[2] - source_box[0], source_box[3] - source_box[1])
    if not svg:
        errors.append("artwork SVG has no root element")
    else:
        width = re.search(r'\bwidth="([0-9.]+)mm"', svg.group(1))
        height = re.search(r'\bheight="([0-9.]+)mm"', svg.group(1))
        if (not width or not height or
                not _close((float(width.group(1)), float(height.group(1))), wanted)):
            errors.append("artwork SVG physical size does not match the DXF")
    reference = re.search(r'<g\b[^>]*\bid="mechanical_reference"[^>]*>'
                          r'(.*?)</g>', text, re.DOTALL)
    if not reference or len(re.findall(r'<path\b', reference.group(1))) != 28:
        # 27 source records plus the explicit 2 mm origin cross path.
        errors.append("SVG mechanical_reference does not contain all 27 DXF records")
    fabrication = "\n".join(match.group(1) for match in re.finditer(
        r'<g\b[^>]*\bid="(?:copper_front|mask_front|silk_front)"[^>]*>'
        r'(.*?)</g>', text, re.DOTALL))
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
    if not args.board.exists():
        errors.append(f"KiCad board is missing: {args.board}")
    else:
        try:
            board = _parse_sexpr(args.board.read_text(encoding="utf-8"))
            if not board or board[0] != "kicad_pcb":
                raise ValueError("root is not kicad_pcb")
            errors.extend(_edge_errors(board, official))
            errors.extend(_reference_errors(board, official))
            errors.extend(_inventory_errors(board, official))
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
    print("  mechanics: 13 source-envelope NPTHs, 2 routed NPTH slots")
    print(f"  VCV comparison: recomputed uniform fit s={scale:.9f}, "
          f"tx={tx:.6f}, ty={ty:.6f}, max={max(residuals.values()):.3f} mm "
          f"(limit {VCV_MAX_ERROR_MM:.2f} mm)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
