#!/usr/bin/env python3
"""Generate the editable KiCad/SVG Glow faceplate masters deterministically."""

from __future__ import annotations

import argparse
import uuid
from pathlib import Path

import dxf_apertures as dxf


GENERATOR_VERSION = "2"
FABRICATION_DATE = "2026-08-13"
REVISION = "A"
UUID_NAMESPACE = uuid.UUID("9ee2beb7-f348-5ea0-90ef-bf1f83bcb138")


def _uid(name: str) -> str:
    return str(uuid.uuid5(UUID_NAMESPACE, name))


def _fmt(value: float) -> str:
    if abs(value) < 0.0000005:
        value = 0.0
    return f"{value:.6f}".rstrip("0").rstrip(".")


def _quote(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _curve_s_expr(points, layer: str, name: str, *, locked=False,
                  width=0.05):
    pts = " ".join(f"(xy {_fmt(x)} {_fmt(y)})" for x, y in points)
    lock = "\n    (locked)" if locked else ""
    return (
        "  (gr_curve\n"
        f"    (pts {pts})\n"
        f"    (stroke (width {_fmt(width)}) (type default))\n"
        f"    (layer {_quote(layer)}){lock}\n"
        f"    (uuid {_quote(_uid(name))})\n"
        "  )"
    )


def _line_s_expr(start, end, layer: str, name: str, width: float):
    return (
        "  (gr_line\n"
        f"    (start {_fmt(start[0])} {_fmt(start[1])})\n"
        f"    (end {_fmt(end[0])} {_fmt(end[1])})\n"
        f"    (stroke (width {_fmt(width)}) (type default))\n"
        f"    (layer {_quote(layer)})\n"
        f"    (uuid {_quote(_uid(name))})\n"
        "  )"
    )


def _art_curve_s_expr(points, layer: str, name: str, width: float):
    return _curve_s_expr(points, layer, name, width=width)


def _text_s_expr(text: str, at, size, thickness, name: str):
    return (
        f"  (gr_text {_quote(text)}\n"
        f"    (at {_fmt(at[0])} {_fmt(at[1])} 0)\n"
        "    (layer \"F.SilkS\")\n"
        f"    (uuid {_quote(_uid(name))})\n"
        "    (effects\n"
        f"      (font (size {_fmt(size)} {_fmt(size)}) "
        f"(thickness {_fmt(thickness)}))\n"
        "      (justify)\n"
        "    )\n"
        "  )"
    )


def _mechanical_id(aperture: dxf.Aperture) -> str:
    return "DXF_" + "_".join(f"{index:02d}" for index in aperture.record_indices)


ART_CURVES = (
    ((25.0, 25.2), (32.0, 18.9), (45.0, 31.1), (56.0, 24.8)),
    ((24.0, 27.2), (32.5, 21.1), (44.0, 32.7), (57.0, 26.4)),
    ((23.5, 29.3), (32.0, 23.7), (45.0, 34.0), (57.5, 28.2)),
)


def _group_s_expr(name: str, member_names):
    members = " ".join(_quote(_uid(member)) for member in member_names)
    return (
        f"  (group {_quote(name)}\n"
        f"    (uuid {_quote(_uid('group-' + name))})\n"
        f"    (members {members})\n"
        "  )"
    )


def make_board(source: dxf.SourceGeometry):
    items = []
    for index, segment in enumerate(source.outer.segments):
        name = f"edge-outer-{index:03d}"
        items.append(_curve_s_expr(segment, "Edge.Cuts", name))

    for aperture in sorted(source.apertures,
                           key=lambda item: item.record_indices):
        contour_id = _mechanical_id(aperture)
        for index, segment in enumerate(aperture.segments):
            name = f"edge-internal-{contour_id}-{index:03d}"
            items.append(_curve_s_expr(segment, "Edge.Cuts", name))

    reference_names = []
    for spline_index, spline in enumerate(source.reference_records):
        for segment_index, segment in enumerate(spline.segments):
            name = f"reference-{spline_index:02d}-{segment_index:03d}"
            reference_names.append(name)
            items.append(_curve_s_expr(segment, "Dwgs.User", name, locked=True,
                                       width=0.04))

    copper_names, mask_names = [], []
    for index, points in enumerate(ART_CURVES):
        copper = f"copper-flow-{index}"
        mask = f"mask-flow-{index}"
        copper_names.append(copper)
        mask_names.append(mask)
        items.append(_art_curve_s_expr(points, "F.Cu", copper, 0.55))
        items.append(_art_curve_s_expr(points, "F.Mask", mask, 0.85))

    silk_names = ["silk-title", "silk-detail"]
    items.append(_text_s_expr("FIREFLOW / GLOW", (40.45, 63.6), 1.25, 0.18,
                              silk_names[0]))
    items.append(_text_s_expr(f"FACEPLATE  REV {REVISION}  {FABRICATION_DATE}",
                              (40.45, 65.65), 0.80, 0.12, silk_names[1]))
    items.extend((
        _group_s_expr("mechanical_reference", reference_names),
        _group_s_expr("copper_front", copper_names),
        _group_s_expr("mask_front", mask_names),
        _group_s_expr("silk_front", silk_names),
    ))

    body = "\n\n".join(items)
    return f'''(kicad_pcb (version 20240108) (generator "fireflow_faceplate_generator")
  (general (thickness 1.6))
  (paper "A4")
  (title_block
    (title "FireFlow Glow faceplate mechanical master")
    (date "{FABRICATION_DATE}")
    (rev "{REVISION}")
    (company "FireFlow")
    (comment 1 "EDITABLE MASTER - FABRICATION EXPORT GATED")
  )
  (layers
    (0 "F.Cu" signal)
    (31 "B.Cu" signal)
    (36 "B.SilkS" user "B.Silkscreen")
    (37 "F.SilkS" user "F.Silkscreen")
    (38 "B.Mask" user)
    (39 "F.Mask" user)
    (40 "Dwgs.User" user "Locked official DXF reference")
    (44 "Edge.Cuts" user)
    (48 "B.Fab" user)
    (49 "F.Fab" user)
  )
  (setup
    (max_error 0.005)
    (pad_to_mask_clearance 0)
    (solder_mask_min_width 0.20)
  )
  (net 0 "")

{body}
)
'''


def _svg_path(spline: dxf.SplineRecord):
    if not spline.segments:
        return ""
    start = spline.segments[0][0]
    pieces = [f"M {_fmt(start[0])},{_fmt(start[1])}"]
    for segment in spline.segments:
        _p0, p1, p2, p3 = segment
        pieces.append("C " + " ".join(
            f"{_fmt(x)},{_fmt(y)}" for x, y in (p1, p2, p3)))
    if spline.flags & 1:
        pieces.append("Z")
    return " ".join(pieces)


def make_svg(source: dxf.SourceGeometry):
    reference_paths = [
        f'    <path data-dxf-record="{spline.index}" d="{_svg_path(spline)}"/>'
        for spline in source.reference_records
    ]
    copper = []
    mask = []
    for index, points in enumerate(ART_CURVES):
        d = (f"M {_fmt(points[0][0])},{_fmt(points[0][1])} C "
             + " ".join(f"{_fmt(x)},{_fmt(y)}" for x, y in points[1:]))
        copper.append(f'    <path id="copper-flow-{index}" d="{d}"/>')
        mask.append(f'    <path id="mask-flow-{index}" d="{d}"/>')
    return f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg"
     width="{_fmt(source.width)}mm" height="{_fmt(source.height)}mm"
     viewBox="0 0 {_fmt(source.width)} {_fmt(source.height)}"
     data-origin="official-dxf-upper-left"
     data-source-sha256="{dxf.DXF_SHA256}">
  <title>FireFlow Glow faceplate editable artwork master</title>
  <desc>Physical millimetres. Fabrication exports remain gated.</desc>
  <g id="copper_front" fill="none" stroke="#c78b36" stroke-width="0.55"
     stroke-linecap="round" stroke-linejoin="round">
{chr(10).join(copper)}
  </g>
  <g id="mask_front" fill="none" stroke="#ffffff" stroke-width="0.85"
     stroke-linecap="round" stroke-linejoin="round" opacity="0.45">
{chr(10).join(mask)}
  </g>
  <g id="silk_front" fill="#f2eee2" font-family="sans-serif"
     text-anchor="middle">
    <text x="40.45" y="63.6" font-size="1.25" font-weight="600">FIREFLOW / GLOW</text>
    <text x="40.45" y="65.65" font-size="0.80">FACEPLATE  REV {REVISION}  {FABRICATION_DATE}</text>
  </g>
  <g id="mechanical_reference" fill="none" stroke="#4aa5a8"
     stroke-width="0.04" vector-effect="non-scaling-stroke"
     data-locked="true" data-units="mm">
{chr(10).join(reference_paths)}
    <circle cx="0" cy="0" r="0.35"/>
  </g>
</svg>
'''


def main(argv=None) -> int:
    root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser()
    parser.add_argument("--dxf", type=Path,
                        default=root / ".reference/glow-touch2/Simple Touch Faceplate template.dxf")
    parser.add_argument("--board", type=Path,
                        default=root / "hardware/glow-faceplate/glow-faceplate.kicad_pcb")
    parser.add_argument("--svg", type=Path,
                        default=root / "hardware/glow-faceplate/artwork/glow-faceplate.svg")
    args = parser.parse_args(argv)
    source = dxf.analyze_dxf(args.dxf)
    args.board.parent.mkdir(parents=True, exist_ok=True)
    args.svg.parent.mkdir(parents=True, exist_ok=True)
    args.board.write_text(make_board(source), encoding="utf-8",
                          newline="\n")
    args.svg.write_text(make_svg(source), encoding="utf-8",
                        newline="\n")
    print(f"wrote {args.board}")
    print(f"wrote {args.svg}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
