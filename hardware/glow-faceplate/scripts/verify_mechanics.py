#!/usr/bin/env python3
"""Verify the editable Glow faceplate master against the official DXF.

This is intentionally independent of KiCad's DRC.  DRC is the electrical and
manufacturing-rule authority; this guard makes the source-mechanical contract
explicit: exact 1:1 outline geometry, one closed Edge.Cuts contour, no
duplicate edge segments, and the complete NPTH/slot inventory.
"""

from __future__ import annotations

import argparse
import hashlib
import math
import re
import sys
from pathlib import Path


DXF_SHA256 = "55c84aa39e8d4e1484ae05a5145a545dd4749097129adb35a6fdb9da1ff606a0"
MM_PER_INCH = 25.4
GEOMETRY_TOLERANCE_MM = 0.006
DIMENSION_TOLERANCE_MM = 0.010

EXPECTED_CIRCULAR_NPTH = {
    "JACK_UPPER": (5.066304, 7.407949, 8.0),
    "JACK_LOWER": (5.066304, 22.641449, 8.0),
    "KNOB_1": (17.163205, 36.903497, 9.0),
    "KNOB_2": (32.678405, 36.903497, 9.0),
    "KNOB_3": (48.196807, 36.903497, 9.0),
    "KNOB_4": (63.711706, 36.903497, 9.0),
    "KNOB_5": (17.163205, 54.048495, 9.0),
    "KNOB_6": (63.713606, 54.048495, 9.0),
    "MOUNT_FADER_L_TOP": (5.079804, 32.485598, 4.5),
    "MOUNT_FADER_R_TOP": (75.774196, 32.485598, 4.5),
    "MOUNT_FADER_L_BOTTOM": (5.079804, 63.468294, 4.5),
    "MOUNT_FADER_R_BOTTOM": (75.774196, 63.468294, 4.5),
    "MOUNT_UPPER_RIGHT": (75.661701, 26.907097, 4.5),
}

EXPECTED_SLOTS = {
    "FADER_LEFT": (5.079804, 47.976946, 4.3010, 26.0409),
    "FADER_RIGHT": (75.774196, 47.976946, 4.3010, 26.0409),
}

VCV_CENTRES = {
    "JACK_UPPER": (4.31, 15.15),
    "JACK_LOWER": (4.33, 30.60),
    "KNOB_1": (16.90, 45.42),
    "KNOB_2": (32.73, 45.48),
    "KNOB_3": (48.63, 45.34),
    "KNOB_4": (64.47, 45.50),
    "KNOB_5": (16.89, 62.82),
    "KNOB_6": (64.46, 62.83),
    "FADER_LEFT": (4.61, 56.75),
    "FADER_RIGHT": (76.74, 56.80),
}

# One uniform least-squares map compares the 1:1 official faceplate-local
# datum to the existing photograph-rectified Rack coordinates.  It is evidence
# only: this scale is never applied to the KiCad manufacturing geometry.
VCV_COMPARISON_SCALE = 1.0234410317105076
VCV_COMPARISON_TX_MM = -0.7363784792541788
VCV_COMPARISON_TY_MM = 7.602795420241295
VCV_MAX_ERROR_MM = 0.25


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
            out = []
            while position < len(tokens) and tokens[position] != ")":
                out.append(parse_one())
            if position >= len(tokens):
                raise ValueError("unclosed KiCad s-expression")
            position += 1
            return out
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


def _float(value) -> float:
    return float(value)


def _xy(node) -> tuple[float, float]:
    return (_float(node[1]), _float(node[2]))


def _layer(node) -> str | None:
    layer = _child(node, "layer")
    return layer[1] if layer and len(layer) > 1 else None


def _curve_points(curve) -> tuple[tuple[float, float], ...]:
    pts = _child(curve, "pts")
    return tuple(_xy(item) for item in _children(pts, "xy")) if pts else ()


def _dxf_pairs(path: Path):
    lines = path.read_text(encoding="cp1252").splitlines()
    if len(lines) % 2:
        raise ValueError("DXF group-code stream has an odd line count")
    out = []
    for offset in range(0, len(lines), 2):
        out.append((int(lines[offset].strip()), lines[offset + 1].strip()))
    return out


def _official_outer_segments(path: Path):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != DXF_SHA256:
        raise ValueError(f"official DXF hash {digest} does not match provenance")
    pairs = _dxf_pairs(path)
    spline_starts = [index for index, pair in enumerate(pairs)
                     if pair == (0, "SPLINE")]
    if not spline_starts:
        raise ValueError("official DXF has no SPLINE entities")
    start = spline_starts[-1]
    end = next(index for index in range(start + 1, len(pairs))
               if pairs[index][0] == 0)
    entity = pairs[start:end]
    flags = next(int(value) for code, value in entity if code == 70)
    degree = next(int(value) for code, value in entity if code == 71)
    xs = [float(value) for code, value in entity if code == 10]
    ys = [float(value) for code, value in entity if code == 20]
    if degree != 3 or not (flags & 1) or (len(xs) - 1) % 3:
        raise ValueError("official outer SPLINE is not a closed cubic Bezier chain")
    raw = list(zip(xs, ys))
    raw_segments = [tuple(raw[index:index + 4])
                    for index in range(0, len(raw) - 1, 3)]
    bbox = _segments_bbox(raw_segments)
    min_x, _min_y, _max_x, max_y = bbox
    transformed = []
    for segment in raw_segments:
        transformed.append(tuple(
            ((x - min_x) * MM_PER_INCH, (max_y - y) * MM_PER_INCH)
            for x, y in segment
        ))
    return tuple(transformed)


def _cubic_coordinate(p0: float, p1: float, p2: float, p3: float,
                      t: float) -> float:
    u = 1.0 - t
    return (u * u * u * p0 + 3.0 * u * u * t * p1
            + 3.0 * u * t * t * p2 + t * t * t * p3)


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
    return tuple(_cubic_coordinate(p0, p1, p2, p3, t)
                 for t in candidates if 0.0 <= t <= 1.0)


def _segments_bbox(segments):
    xs, ys = [], []
    for segment in segments:
        xs.extend(_coordinate_extrema(tuple(point[0] for point in segment)))
        ys.extend(_coordinate_extrema(tuple(point[1] for point in segment)))
    return min(xs), min(ys), max(xs), max(ys)


def _points_close(a, b, tolerance=GEOMETRY_TOLERANCE_MM) -> bool:
    return math.dist(a, b) <= tolerance


def _normal_curve_key(points):
    forward = tuple((round(x, 6), round(y, 6)) for x, y in points)
    reverse = tuple(reversed(forward))
    return min(forward, reverse)


def _edge_errors(root, expected_segments):
    errors = []
    curves = [node for node in _children(root, "gr_curve")
              if _layer(node) == "Edge.Cuts"]
    parsed = [_curve_points(curve) for curve in curves]
    if not parsed:
        return ["Edge.Cuts has no cubic outline segments",
                "overall dimensions are unavailable without Edge.Cuts"]
    bad = [index for index, points in enumerate(parsed) if len(points) != 4]
    if bad:
        errors.append(f"Edge.Cuts curves without four cubic points: {bad}")
    valid = [points for points in parsed if len(points) == 4]
    keys = [_normal_curve_key(points) for points in valid]
    if len(keys) != len(set(keys)):
        errors.append("Edge.Cuts contains duplicate curve segments")
    if len(valid) != len(expected_segments):
        errors.append(f"Edge.Cuts has {len(valid)} curves, want "
                      f"{len(expected_segments)} from the official DXF")
    else:
        for index, (actual, expected) in enumerate(zip(valid, expected_segments)):
            if any(not _points_close(a, e) for a, e in zip(actual, expected)):
                errors.append(f"Edge.Cuts curve {index} differs from official DXF")
                break
    if valid:
        for index, points in enumerate(valid):
            following = valid[(index + 1) % len(valid)]
            if not _points_close(points[-1], following[0]):
                errors.append(f"Edge.Cuts is open between curves {index} and "
                              f"{(index + 1) % len(valid)}")
                break
        min_x, min_y, max_x, max_y = _segments_bbox(valid)
        width, height = max_x - min_x, max_y - min_y
        if abs(width - 80.900) > DIMENSION_TOLERANCE_MM:
            errors.append(f"overall width is {width:.4f} mm, want 80.900 mm")
        if abs(height - 68.000) > DIMENSION_TOLERANCE_MM:
            errors.append(f"overall height is {height:.4f} mm, want 68.000 mm")
        if math.dist((min_x, min_y), (0.0, 0.0)) > DIMENSION_TOLERANCE_MM:
            errors.append(f"official upper-left datum is ({min_x:.4f}, "
                          f"{min_y:.4f}), want (0, 0)")
    return errors


def _reference_errors(root, expected_segments):
    errors = []
    references = [node for node in _children(root, "gr_curve")
                  if _layer(node) == "Dwgs.User"]
    if len(references) < len(expected_segments):
        errors.append("locked mechanical reference does not contain the "
                      "official outer spline")
        return errors
    first = references[:len(expected_segments)]
    if not all(any(item == "locked" or
                   (isinstance(item, list) and item and item[0] == "locked")
                   for item in curve[1:]) for curve in first):
        errors.append("mechanical reference outer spline is not locked")
    for index, (curve, expected) in enumerate(zip(first, expected_segments)):
        points = _curve_points(curve)
        if len(points) != 4 or any(not _points_close(a, e)
                                   for a, e in zip(points, expected)):
            errors.append(f"mechanical reference curve {index} differs from DXF")
            break
    return errors


def _footprint_reference(footprint) -> str | None:
    for prop in _children(footprint, "property"):
        if len(prop) >= 3 and prop[1] == "Reference":
            return prop[2]
    for text in _children(footprint, "fp_text"):
        if len(text) >= 3 and text[1] == "reference":
            return text[2]
    return None


def _mechanical_inventory(root):
    inventory = {}
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
        if not drill:
            continue
        inventory[ref] = ((_float(at[1]), _float(at[2])), pad[3], drill)
    return inventory


def _inventory_errors(root):
    errors = []
    inventory = _mechanical_inventory(root)
    for footprint in _children(root, "footprint"):
        ref = _footprint_reference(footprint)
        if ref not in EXPECTED_CIRCULAR_NPTH and ref not in EXPECTED_SLOTS:
            continue
        attr = _child(footprint, "attr")
        if not attr or "board_only" not in attr[1:]:
            errors.append(f"{ref} is not marked board_only; KiCad will treat "
                          "the generated mechanic as a missing library footprint")
        if len(footprint) > 1 and ":" in footprint[1]:
            errors.append(f"{ref} uses library-qualified id {footprint[1]!r}; "
                          "embedded mechanics must not trigger a library lookup")
    for ref, (x, y, diameter) in EXPECTED_CIRCULAR_NPTH.items():
        item = inventory.get(ref)
        if item is None:
            errors.append(f"missing circular NPTH {ref}")
            continue
        at, shape, drill = item
        if shape != "circle" or len(drill) != 2:
            errors.append(f"{ref} is not a circular NPTH drill")
            continue
        if not _points_close(at, (x, y)):
            errors.append(f"{ref} centre {at} does not match official "
                          f"({x:.6f}, {y:.6f})")
        if abs(_float(drill[1]) - diameter) > DIMENSION_TOLERANCE_MM:
            errors.append(f"{ref} drill is {drill[1]} mm, want {diameter:.1f} mm")
    for ref, (x, y, width, height) in EXPECTED_SLOTS.items():
        item = inventory.get(ref)
        if item is None:
            errors.append(f"missing routed NPTH slot {ref}")
            continue
        at, shape, drill = item
        if shape != "oval" or len(drill) != 4 or drill[1] != "oval":
            errors.append(f"{ref} is not an explicit oval NPTH slot")
            continue
        if not _points_close(at, (x, y)):
            errors.append(f"{ref} centre {at} does not match official "
                          f"({x:.6f}, {y:.6f})")
        actual = (_float(drill[2]), _float(drill[3]))
        if (abs(actual[0] - width) > DIMENSION_TOLERANCE_MM or
                abs(actual[1] - height) > DIMENSION_TOLERANCE_MM):
            errors.append(f"{ref} slot is {actual[0]:.4f} x {actual[1]:.4f} mm, "
                          f"want {width:.4f} x {height:.4f} mm")
    return errors


def _silk_errors(root):
    errors = []
    for text in _children(root, "gr_text"):
        if _layer(text) != "F.SilkS":
            continue
        effects = _child(text, "effects")
        font = _child(effects, "font") if effects else None
        size = _child(font, "size") if font else None
        if not size or len(size) < 3:
            errors.append("front silkscreen text has no explicit font size")
            continue
        if min(_float(size[1]), _float(size[2])) < 0.8:
            errors.append(f"front silkscreen text height {size[2]} mm is below "
                          "KiCad's vendor-neutral 0.8 mm default")
    return errors


def _vcv_errors():
    errors = []
    mechanical = dict(EXPECTED_CIRCULAR_NPTH)
    mechanical.update(EXPECTED_SLOTS)
    for name, expected in VCV_CENTRES.items():
        x, y = mechanical[name][:2]
        mapped = (VCV_COMPARISON_SCALE * x + VCV_COMPARISON_TX_MM,
                  VCV_COMPARISON_SCALE * y + VCV_COMPARISON_TY_MM)
        error = math.dist(mapped, expected)
        if error > VCV_MAX_ERROR_MM:
            errors.append(f"{name} VCV fiducial error is {error:.3f} mm, "
                          f"limit {VCV_MAX_ERROR_MM:.2f} mm")
    return errors


def _svg_errors(path: Path):
    if not path.exists():
        return [f"artwork SVG is missing: {path}"]
    text = path.read_text(encoding="utf-8")
    errors = []
    for group in ("copper_front", "mask_front", "silk_front",
                  "mechanical_reference"):
        if not re.search(rf'<g\b[^>]*\bid="{re.escape(group)}"', text):
            errors.append(f"artwork SVG is missing group {group}")
    if not re.search(r'<svg\b[^>]*\bwidth="80\.900mm"[^>]*\bheight="68\.000mm"',
                     text):
        errors.append("artwork SVG is not declared at 80.900 x 68.000 mm")
    forbidden = re.compile(r"\b(SYNTHUX|TOUCH|MOTION|SHAPE|ENERGY|SPACE|FADER|SWITCH)\b",
                           re.IGNORECASE)
    fabrication = "\n".join(
        match.group(1) for match in re.finditer(
            r'<g\b[^>]*\bid="(?:copper_front|mask_front|silk_front)"[^>]*>'
            r'(.*?)</g>', text, re.DOTALL))
    match = forbidden.search(fabrication)
    if match:
        errors.append(f"forbidden provisional/protected fabrication text: "
                      f"{match.group(0)}")
    return errors


def main(argv=None) -> int:
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parents[3]
    parser.add_argument("--board", type=Path,
                        default=root / "hardware/glow-faceplate/glow-faceplate.kicad_pcb")
    parser.add_argument("--svg", type=Path,
                        default=root / "hardware/glow-faceplate/artwork/glow-faceplate.svg")
    parser.add_argument("--dxf", type=Path,
                        default=root / ".reference/glow-touch2/Simple Touch Faceplate template.dxf")
    args = parser.parse_args(argv)
    errors = []
    try:
        expected_segments = _official_outer_segments(args.dxf)
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
            errors.extend(_edge_errors(board, expected_segments))
            errors.extend(_reference_errors(board, expected_segments))
            errors.extend(_inventory_errors(board))
            errors.extend(_silk_errors(board))
        except (OSError, ValueError) as exc:
            errors.append(f"cannot parse KiCad board: {exc}")
    errors.extend(_svg_errors(args.svg))
    errors.extend(_vcv_errors())
    if errors:
        print(f"FAIL ({len(errors)})")
        for error in errors:
            print(f"  - {error}")
        return 1
    print("PASS -- faceplate mechanics match the official 1:1 DXF")
    print("  outline: 80.900 x 68.000 mm, closed 114-curve Edge.Cuts")
    print("  mechanics: 13 circular NPTHs, 2 routed NPTH slots")
    fit_errors = []
    mechanical = dict(EXPECTED_CIRCULAR_NPTH)
    mechanical.update(EXPECTED_SLOTS)
    for name, expected in VCV_CENTRES.items():
        x, y = mechanical[name][:2]
        mapped = (VCV_COMPARISON_SCALE * x + VCV_COMPARISON_TX_MM,
                  VCV_COMPARISON_SCALE * y + VCV_COMPARISON_TY_MM)
        fit_errors.append(math.dist(mapped, expected))
    print(f"  VCV comparison: uniform-fit maximum {max(fit_errors):.3f} mm "
          f"(limit {VCV_MAX_ERROR_MM:.2f} mm)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
