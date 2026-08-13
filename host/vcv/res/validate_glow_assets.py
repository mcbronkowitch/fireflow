#!/usr/bin/env python3
"""Validate and reproducibly derive clean-room Glow raster assets.

This intentionally uses only Python's standard library.  The physical
faceplate is uniformly resampled from the committed KiCad front preview; the
other neutral assets contain no reference photography, third-party logo,
wordmark, or copied decorative artwork.
"""
import argparse
import hashlib
import math
import os
import re
import struct
import sys
import zlib


RACK_WIDTH_MM = 81.28
RACK_HEIGHT_MM = 128.5
RACK_WIDTH_PX = 960
RACK_HEIGHT_PX = 1520
RACK_PX_PER_MM = RACK_WIDTH_PX / RACK_WIDTH_MM
FACEPLATE_WIDTH_MM = 80.899997
FACEPLATE_HEIGHT_MM = 67.999993
FACEPLATE_TX_MM = (RACK_WIDTH_MM - FACEPLATE_WIDTH_MM) / 2.0
FACEPLATE_TY_MM = 8.5
FIDUCIAL_TOLERANCE_MM = RACK_WIDTH_MM / RACK_WIDTH_PX
OPAQUE_BOARD_ALPHA = 128

FACEPLATE_MANIFEST = {
    "fireflow-derivation": "kicad-physical-master-v1",
    "fireflow-pixel-sha256":
        "08eb70559d816dea30eac00b362f9fc82e684b84efc4ff16889f3896db3ac77e",
    "fireflow-source-board-sha256":
        "745af5799a38e13bf21eec80266137818832cf3a1e8cfdba858796725d518ba1",
    "fireflow-source-preview-sha256":
        "9d196304083c09d7a2fb9b22e34d1214d775fdeb43cd8640a12a8ca908674e1b",
    "fireflow-transform-mm": "s=1;tx=0.1900015;ty=8.5",
}

# This manifest is a review sidecar for geometry generated from the pinned DXF.
# Coordinates are board-local millimetres, never hand-fitted Rack positions.
FACEPLATE_FIDUCIALS_MM = {
    "mount_left_top": (5.079803181, 32.485598643),
    "mount_right_bottom": (75.774196174, 63.468293432),
    # The source envelope edge itself is antialiased.  Sample exactly one Rack
    # pixel into each opening; that is both transparent and the stated limit.
    "fader_left_top_inset_1px":
        (5.079803181, 34.956499993 + FIDUCIAL_TOLERANCE_MM),
    "fader_right_bottom_inset_1px":
        (75.774203803, 60.997399711 - FIDUCIAL_TOLERANCE_MM),
    "knob_1": (17.163203227, 36.903498480),
    "knob_6": (63.713604201, 54.048496065),
    "diagonal_opening": (70.000000000, 10.000000000),
}

# Only like-for-like centre comparisons belong here.  The VCV centres came
# from rectified photography and remain a separate evidence source; their
# residual is reported and guarded, never applied to move/warp this master.
CONTROL_FIDUCIALS = {
    "knob_1": "knob_s31",
    "knob_6": "knob_s35",
    "fader_left": "fader_s36",
    "fader_right": "fader_s37",
}
MECHANICAL_CONTROL_CENTRES_MM = {
    "knob_1": FACEPLATE_FIDUCIALS_MM["knob_1"],
    "knob_6": FACEPLATE_FIDUCIALS_MM["knob_6"],
    "fader_left": (5.079803181, 47.976949852),
    "fader_right": (75.774203803, 47.976949852),
}
# This is not the mechanical-raster tolerance.  It guards only the older
# rectified-photography source against gross drift while its physical-scale
# comparison remains 0.454-0.840 mm.  Task 8's independently recomputed
# best-fit comparison is 0.218 mm, but that 1.023423583 scale must never move
# or warp the faceplate master.
CONTROL_SOURCE_MAX_ERROR_MM = 1.00

PANEL_ASSETS = {
    "GlowRear.png": (960, 1520, "RGBA"),
    "GlowFaceplate.png": (960, 1520, "RGBA"),
    "GlowTouch.png": (960, 1520, "RGBA"),
}

SWITCH_ASSETS = {
    "GlowSwitchDown.png": (96, 192, "RGBA"),
    "GlowSwitchCenter.png": (96, 192, "RGBA"),
    "GlowSwitchUp.png": (96, 192, "RGBA"),
}

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class AssetError(Exception):
    """An asset violates the source-controlled package contract."""


def _chunks(path):
    with open(path, "rb") as source:
        if source.read(8) != PNG_SIGNATURE:
            raise AssetError("%s is not a PNG" % os.path.basename(path))
        while True:
            length_data = source.read(4)
            if not length_data:
                return
            length = struct.unpack(">I", length_data)[0]
            kind = source.read(4)
            data = source.read(length)
            crc = source.read(4)
            if len(kind) != 4 or len(data) != length or len(crc) != 4:
                raise AssetError("%s is a truncated PNG" % os.path.basename(path))
            yield kind, data
            if kind == b"IEND":
                return


def _png_info(path):
    width = height = bit_depth = color_type = None
    image_data = []
    for kind, data in _chunks(path):
        if kind == b"IHDR":
            if len(data) != 13:
                raise AssetError("%s has an invalid IHDR" % os.path.basename(path))
            width, height, bit_depth, color_type, compression, filtering, interlace = \
                struct.unpack(">IIBBBBB", data)
            if compression != 0 or filtering != 0 or interlace != 0:
                raise AssetError("%s uses unsupported PNG encoding" % os.path.basename(path))
        elif kind == b"IDAT":
            image_data.append(data)
    if width is None or not image_data:
        raise AssetError("%s lacks image data" % os.path.basename(path))
    modes = {2: "RGB", 6: "RGBA"}
    if bit_depth != 8 or color_type not in modes:
        mode = "PNG color type %s at %s-bit" % (color_type, bit_depth)
    else:
        mode = modes[color_type]
    return width, height, mode, zlib.decompress(b"".join(image_data))


def _png_text(path):
    """Return uncompressed Latin-1 PNG text fields."""
    fields = {}
    for kind, data in _chunks(path):
        if kind != b"tEXt" or b"\0" not in data:
            continue
        key, value = data.split(b"\0", 1)
        fields[key.decode("latin-1")] = value.decode("latin-1")
    return fields


def _sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _pixel_sha256(raw, width, height):
    return hashlib.sha256(b"".join(_unfilter_rgba(raw, width, height))).hexdigest()


def _unfilter_rgba(raw, width, height):
    stride = width * 4
    if len(raw) != height * (stride + 1):
        raise AssetError("RGBA pixel data has an unexpected length")
    rows = []
    previous = bytearray(stride)
    index = 0
    for _ in range(height):
        filter_type = raw[index]
        index += 1
        scanline = bytearray(raw[index:index + stride])
        index += stride
        for x in range(stride):
            left = scanline[x - 4] if x >= 4 else 0
            above = previous[x]
            upper_left = previous[x - 4] if x >= 4 else 0
            if filter_type == 1:
                scanline[x] = (scanline[x] + left) & 255
            elif filter_type == 2:
                scanline[x] = (scanline[x] + above) & 255
            elif filter_type == 3:
                scanline[x] = (scanline[x] + ((left + above) // 2)) & 255
            elif filter_type == 4:
                prediction = left + above - upper_left
                distances = (abs(prediction - left), abs(prediction - above),
                             abs(prediction - upper_left))
                predictor = (left, above, upper_left)[distances.index(min(distances))]
                scanline[x] = (scanline[x] + predictor) & 255
            elif filter_type != 0:
                raise AssetError("RGBA pixel data has an invalid filter")
        rows.append(scanline)
        previous = scanline
    return rows


def _has_visible_alpha(raw, width, height):
    return any(row[x] for row in _unfilter_rgba(raw, width, height)
               for x in range(3, len(row), 4))


def _package_text(makefile):
    if os.path.isfile(makefile):
        with open(makefile, encoding="utf-8") as source:
            return source.read()
    return makefile


def _distributables(makefile):
    """Return literal paths assigned to DISTRIBUTABLES (comments excluded)."""
    text = _package_text(makefile)
    logical_lines = re.sub(r"\\\r?\n", " ", text).splitlines()
    entries = set()
    for line in logical_lines:
        line = line.split("#", 1)[0]
        assignment = re.match(r"^\s*DISTRIBUTABLES\s*(?:\+?=|:=)\s*(.*)$", line)
        if assignment:
            entries.update(assignment.group(1).split())
    return entries


def validate_assets(asset_dir, makefile):
    """Raise AssetError unless every source asset and package entry is valid."""
    package = _distributables(makefile)
    all_assets = dict(PANEL_ASSETS)
    all_assets.update(SWITCH_ASSETS)
    infos = {}
    for name, (expected_width, expected_height, expected_mode) in all_assets.items():
        path = os.path.join(asset_dir, name)
        if not os.path.isfile(path):
            raise AssetError("%s is missing" % name)
        infos[name] = _png_info(path)

    fallback = os.path.join(asset_dir, "Glow.svg")
    if not os.path.isfile(fallback):
        raise AssetError("Glow.svg is missing")
    if "res/Glow.svg" not in package:
        raise AssetError("Glow.svg is absent from DISTRIBUTABLES")

    switch_sizes = {(infos[name][0], infos[name][1]) for name in SWITCH_ASSETS}
    if len(switch_sizes) != 1:
        raise AssetError("switch assets do not have identical dimensions")

    canonical_asset_dir = os.path.dirname(os.path.abspath(__file__))
    if os.path.abspath(asset_dir) == canonical_asset_dir:
        _validate_faceplate_derivation(
            os.path.join(asset_dir, "GlowFaceplate.png"),
            infos["GlowFaceplate.png"])

    for name, (expected_width, expected_height, expected_mode) in all_assets.items():
        width, height, mode, raw = infos[name]
        if (width, height) != (expected_width, expected_height):
            raise AssetError("%s has size %dx%d, want %dx%d" %
                             (name, width, height, expected_width, expected_height))
        if mode != expected_mode:
            raise AssetError("%s has mode %s, want %s" %
                             (name, mode, expected_mode))
        if name in SWITCH_ASSETS:
            if not _has_visible_alpha(raw, width, height):
                raise AssetError("%s is fully transparent" % name)
        if "res/" + name not in package:
            raise AssetError("%s is absent from DISTRIBUTABLES" % name)


def _blank(width, height):
    return bytearray(width * height * 4)


def _fill(image, width, height, color):
    image[:] = bytes(color) * (width * height)


def _rect(image, width, height, x0, y0, x1, y1, color):
    for y in range(max(0, y0), min(height, y1)):
        start = (y * width + max(0, x0)) * 4
        image[start:start + max(0, min(width, x1) - max(0, x0)) * 4] = \
            bytes(color) * max(0, min(width, x1) - max(0, x0))


def _line(image, width, height, x0, y0, x1, y1, color):
    steps = max(abs(x1 - x0), abs(y1 - y0), 1)
    for step in range(steps + 1):
        x = round(x0 + (x1 - x0) * step / steps)
        y = round(y0 + (y1 - y0) * step / steps)
        _rect(image, width, height, x - 1, y - 1, x + 2, y + 2, color)


def _png(path, width, height, image, text_fields=None):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))
    raw = b"".join(b"\0" + image[y * width * 4:(y + 1) * width * 4]
                   for y in range(height))
    with open(path, "wb") as output:
        output.write(PNG_SIGNATURE)
        output.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height,
                                                   8, 6, 0, 0, 0)))
        for key, value in sorted((text_fields or {}).items()):
            output.write(chunk(b"tEXt", key.encode("latin-1") + b"\0" +
                               value.encode("latin-1")))
        output.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        output.write(chunk(b"IEND", b""))


def _opaque_bounds(rows, width, height):
    bounds = [width, height, -1, -1]
    for y, row in enumerate(rows):
        for x in range(width):
            if row[x * 4 + 3] >= OPAQUE_BOARD_ALPHA:
                bounds[0] = min(bounds[0], x)
                bounds[1] = min(bounds[1], y)
                bounds[2] = max(bounds[2], x)
                bounds[3] = max(bounds[3], y)
    if bounds[2] < bounds[0]:
        raise AssetError("KiCad preview has no opaque board pixels")
    return tuple(bounds)


def _bilinear(rows, width, height, x, y):
    if x < 0.0 or y < 0.0 or x > width - 1 or y > height - 1:
        return (0, 0, 0, 0)
    x0, y0 = int(math.floor(x)), int(math.floor(y))
    x1, y1 = min(width - 1, x0 + 1), min(height - 1, y0 + 1)
    fx, fy = x - x0, y - y0
    weights = ((x0, y0, (1.0 - fx) * (1.0 - fy)),
               (x1, y0, fx * (1.0 - fy)),
               (x0, y1, (1.0 - fx) * fy),
               (x1, y1, fx * fy))
    out = []
    for channel in range(4):
        value = sum(rows[py][px * 4 + channel] * weight
                    for px, py, weight in weights)
        out.append(max(0, min(255, int(round(value)))))
    return tuple(out)


def derive_faceplate(preview_path, output_path):
    """Uniformly map one KiCad front render into the 4x Rack canvas."""
    width, height, mode, raw = _png_info(preview_path)
    if mode != "RGBA":
        raise AssetError("KiCad preview must be RGBA")
    rows = _unfilter_rgba(raw, width, height)
    # KiCad high-quality PNGs contain a broad low-alpha post-processing halo.
    # Removing pixels below 50% alpha isolates the renderer's solid board; the
    # subsequent bilinear sample supplies a one-pixel antialiased cut edge.
    for row in rows:
        for x in range(width):
            offset = x * 4
            if row[offset + 3] < OPAQUE_BOARD_ALPHA:
                row[offset:offset + 4] = b"\0\0\0\0"
    x0, y0, x1, y1 = _opaque_bounds(rows, width, height)
    source_ppmm_x = (x1 - x0 + 1) / FACEPLATE_WIDTH_MM
    source_ppmm_y = (y1 - y0 + 1) / FACEPLATE_HEIGHT_MM
    source_ppmm = (source_ppmm_x + source_ppmm_y) / 2.0
    source_cx = (x0 + x1) / 2.0
    source_cy = (y0 + y1) / 2.0

    image = _blank(RACK_WIDTH_PX, RACK_HEIGHT_PX)
    for py in range(RACK_HEIGHT_PX):
        board_y = (py + 0.5) / RACK_PX_PER_MM - FACEPLATE_TY_MM
        if board_y < 0.0 or board_y > FACEPLATE_HEIGHT_MM:
            continue
        sy = source_cy + (board_y - FACEPLATE_HEIGHT_MM / 2.0) * source_ppmm
        for px in range(RACK_WIDTH_PX):
            board_x = (px + 0.5) / RACK_PX_PER_MM - FACEPLATE_TX_MM
            if board_x < 0.0 or board_x > FACEPLATE_WIDTH_MM:
                continue
            sx = source_cx + (board_x - FACEPLATE_WIDTH_MM / 2.0) * source_ppmm
            color = _bilinear(rows, width, height, sx, sy)
            offset = (py * RACK_WIDTH_PX + px) * 4
            image[offset:offset + 4] = bytes(color)

    preview_hash = _sha256(preview_path)
    pixel_hash = hashlib.sha256(image).hexdigest()
    manifest = dict(FACEPLATE_MANIFEST)
    manifest.update({
        "fireflow-source-preview-sha256": preview_hash,
        "fireflow-source-opaque-bounds": "%d,%d,%d,%d" % (x0, y0, x1, y1),
        "fireflow-source-pixels-per-mm": "%.9f" % source_ppmm,
        "fireflow-pixel-sha256": pixel_hash,
    })
    _png(output_path, RACK_WIDTH_PX, RACK_HEIGHT_PX, image, manifest)
    return manifest


def _alpha_at_rack_mm(rows, x_mm, y_mm):
    px = min(RACK_WIDTH_PX - 1, max(0, int(round(x_mm * RACK_PX_PER_MM - 0.5))))
    py = min(RACK_HEIGHT_PX - 1, max(0, int(round(y_mm * RACK_PX_PER_MM - 0.5))))
    return rows[py][px * 4 + 3]


def _control_source_residuals():
    import touch2_geometry as geometry
    residuals = {}
    for physical_name, control_name in CONTROL_FIDUCIALS.items():
        source_x, source_y = MECHANICAL_CONTROL_CENTRES_MM[physical_name]
        rendered = (source_x + FACEPLATE_TX_MM, source_y + FACEPLATE_TY_MM)
        residuals[physical_name] = math.dist(
            rendered, geometry.CONTROL_CENTRES_MM[control_name])
    return residuals


def _validate_faceplate_derivation(path, info):
    width, height, _mode, raw = info
    fields = _png_text(path)
    for key, expected in FACEPLATE_MANIFEST.items():
        if fields.get(key) != expected:
            raise AssetError("GlowFaceplate.png is not the KiCad physical preview derivative")
    required = ("fireflow-source-opaque-bounds",
                "fireflow-source-pixels-per-mm")
    if any(not fields.get(key) for key in required):
        raise AssetError("GlowFaceplate.png lacks its KiCad derivation sidecar")
    if _pixel_sha256(raw, width, height) != fields["fireflow-pixel-sha256"]:
        raise AssetError("GlowFaceplate.png pixels differ from its derivation manifest")

    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    preview = os.path.join(root, "hardware", "glow-faceplate", "proof",
                           "glow-faceplate-preview.png")
    board = os.path.join(root, "hardware", "glow-faceplate",
                         "glow-faceplate.kicad_pcb")
    if not os.path.isfile(preview) or _sha256(preview) != \
            fields["fireflow-source-preview-sha256"]:
        raise AssetError("GlowFaceplate.png does not match the committed KiCad preview")
    if not os.path.isfile(board) or _sha256(board) != \
            FACEPLATE_MANIFEST["fireflow-source-board-sha256"]:
        raise AssetError("GlowFaceplate.png physical-master hash is stale")

    rows = _unfilter_rgba(raw, width, height)
    for name, (board_x, board_y) in FACEPLATE_FIDUCIALS_MM.items():
        rack_x = board_x + FACEPLATE_TX_MM
        rack_y = board_y + FACEPLATE_TY_MM
        pixel_x = int(round(rack_x * RACK_PX_PER_MM - 0.5))
        pixel_y = int(round(rack_y * RACK_PX_PER_MM - 0.5))
        sampled_rack = ((pixel_x + 0.5) / RACK_PX_PER_MM,
                        (pixel_y + 0.5) / RACK_PX_PER_MM)
        if math.dist((rack_x, rack_y), sampled_rack) > FIDUCIAL_TOLERANCE_MM:
            raise AssetError("GlowFaceplate.png fiducial %s exceeds one pixel" % name)
        alpha = _alpha_at_rack_mm(rows, rack_x, rack_y)
        if alpha > 16:
            raise AssetError("GlowFaceplate.png fiducial %s is not transparent" % name)

    residuals = _control_source_residuals()
    if max(residuals.values()) > CONTROL_SOURCE_MAX_ERROR_MM:
        raise AssetError("VCV photographic control source exceeds its separate 1.00 mm guard")


def write_placeholders(asset_dir):
    """Write six reproducible neutral RGBA placeholder assets."""
    os.makedirs(asset_dir, exist_ok=True)
    rear = _blank(960, 1520)
    _fill(rear, 960, 1520, (15, 17, 16, 255))
    _rect(rear, 960, 1520, 34, 34, 926, 1486, (20, 23, 21, 255))
    _png(os.path.join(asset_dir, "GlowRear.png"), 960, 1520, rear)

    face = _blank(960, 1520)
    for y in range(1520):
        _line(face, 960, 1520, 70, y, min(890, 70 + y // 2), y,
              (97, 105, 96, 190))
    _line(face, 960, 1520, 90, 118, 870, 438, (160, 167, 153, 220))
    _png(os.path.join(asset_dir, "GlowFaceplate.png"), 960, 1520, face)

    touch = _blank(960, 1520)
    _rect(touch, 960, 1520, 82, 770, 878, 1450, (49, 55, 52, 235))
    for row in range(3):
        for column in range(4):
            x, y = 130 + column * 180, 840 + row * 170
            _rect(touch, 960, 1520, x, y, x + 112, y + 86, (132, 140, 126, 215))
            _rect(touch, 960, 1520, x + 5, y + 5, x + 107, y + 81,
                  (49, 55, 52, 235))
    _png(os.path.join(asset_dir, "GlowTouch.png"), 960, 1520, touch)

    positions = {"GlowSwitchDown.png": 137, "GlowSwitchCenter.png": 95,
                 "GlowSwitchUp.png": 53}
    for name, lever_y in positions.items():
        switch = _blank(96, 192)
        _rect(switch, 96, 192, 30, 20, 66, 172, (92, 97, 95, 255))
        _rect(switch, 96, 192, 34, 24, 62, 168, (185, 190, 184, 255))
        _line(switch, 96, 192, 48, lever_y, 48, lever_y + 28, (225, 228, 220, 255))
        _png(os.path.join(asset_dir, name), 96, 192, switch)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write-placeholders", action="store_true")
    parser.add_argument("--derive-faceplate", action="store_true")
    parser.add_argument("--preview", default=os.path.abspath(os.path.join(
        os.path.dirname(__file__), "..", "..", "..", "hardware",
        "glow-faceplate", "proof", "glow-faceplate-preview.png")))
    parser.add_argument("--asset-dir", default=os.path.dirname(os.path.abspath(__file__)))
    parser.add_argument("--makefile", default=os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Makefile"))
    args = parser.parse_args()
    if args.write_placeholders:
        write_placeholders(args.asset_dir)
    if args.derive_faceplate:
        manifest = derive_faceplate(
            args.preview, os.path.join(args.asset_dir, "GlowFaceplate.png"))
        print("derived GlowFaceplate.png from KiCad preview %s" %
              manifest["fireflow-source-preview-sha256"])
    try:
        validate_assets(args.asset_dir, args.makefile)
    except AssetError as error:
        print("assets FAIL: %s" % error)
        return 1
    print("assets OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
