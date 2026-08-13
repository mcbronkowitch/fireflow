#!/usr/bin/env python3
"""Validate and reproducibly create clean-room Glow raster placeholders.

This intentionally uses only Python's standard library.  The placeholders are
made from neutral geometry owned by this repository; they contain no reference
photography, third-party logo, wordmark, or copied decorative artwork.
"""
import argparse
import os
import struct
import sys
import zlib

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


def validate_assets(asset_dir, makefile):
    """Raise AssetError unless every source asset and package entry is valid."""
    package = _package_text(makefile)
    all_assets = dict(PANEL_ASSETS)
    all_assets.update(SWITCH_ASSETS)
    switch_sizes = set()
    for name, (expected_width, expected_height, expected_mode) in all_assets.items():
        path = os.path.join(asset_dir, name)
        if not os.path.isfile(path):
            raise AssetError("%s is missing" % name)
        width, height, mode, raw = _png_info(path)
        if (width, height) != (expected_width, expected_height):
            raise AssetError("%s has size %dx%d, want %dx%d" %
                             (name, width, height, expected_width, expected_height))
        if mode != expected_mode:
            raise AssetError("%s has mode %s, want %s" %
                             (name, mode, expected_mode))
        if name in SWITCH_ASSETS:
            switch_sizes.add((width, height))
            if not _has_visible_alpha(raw, width, height):
                raise AssetError("%s is fully transparent" % name)
        if "res/" + name not in package:
            raise AssetError("%s is absent from DISTRIBUTABLES" % name)
    if len(switch_sizes) != 1:
        raise AssetError("switch assets do not have identical dimensions")


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


def _png(path, width, height, image):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))
    raw = b"".join(b"\0" + image[y * width * 4:(y + 1) * width * 4]
                   for y in range(height))
    with open(path, "wb") as output:
        output.write(PNG_SIGNATURE)
        output.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height,
                                                   8, 6, 0, 0, 0)))
        output.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        output.write(chunk(b"IEND", b""))


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
    parser.add_argument("--asset-dir", default=os.path.dirname(os.path.abspath(__file__)))
    parser.add_argument("--makefile", default=os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Makefile"))
    args = parser.parse_args()
    if args.write_placeholders:
        write_placeholders(args.asset_dir)
    try:
        validate_assets(args.asset_dir, args.makefile)
    except AssetError as error:
        print("assets FAIL: %s" % error)
        return 1
    print("assets OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
