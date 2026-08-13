#!/usr/bin/env python3
"""Tests for the distributable Glow raster-asset contract.

Plain asserts keep this runnable in a Rack checkout without pytest.  Every
negative case uses a real temporary PNG fixture, not a mocked image reader.
"""
import hashlib
import os
import shutil
import struct
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import validate_glow_assets as validator
from validate_glow_assets import (PANEL_ASSETS, SWITCH_ASSETS, AssetError,
                                  validate_assets)


HERE = os.path.dirname(os.path.abspath(__file__))


def write_png(path, size, mode="RGBA", alpha=255):
    width, height = size
    channels = 4 if mode == "RGBA" else 3
    pixel = bytes((40, 50, 60, alpha)) if channels == 4 else bytes((40, 50, 60))
    raw = b"".join(b"\x00" + pixel * width for _ in range(height))

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))

    color_type = 6 if channels == 4 else 2
    with open(path, "wb") as output:
        output.write(b"\x89PNG\r\n\x1a\n")
        output.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height,
                                                   8, color_type, 0, 0, 0)))
        output.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        output.write(chunk(b"IEND", b""))


def write_valid_assets(folder):
    for name, (width, height, mode) in PANEL_ASSETS.items():
        path = os.path.join(folder, name)
        if name == "GlowFaceplate.png":
            shutil.copyfile(os.path.join(HERE, name), path)
        else:
            write_png(path, (width, height), mode)
    for name, (width, height, mode) in SWITCH_ASSETS.items():
        write_png(os.path.join(folder, name), (width, height), mode)
    with open(os.path.join(folder, "Glow.svg"), "w", encoding="utf-8") as output:
        output.write("<svg/>")


def package_list():
    return "DISTRIBUTABLES += res/Glow.svg " + " ".join(
        "res/" + name for name in list(PANEL_ASSETS) + list(SWITCH_ASSETS))


def expect_error(folder, makefile, message):
    try:
        validate_assets(folder, makefile)
    except AssetError as error:
        assert message in str(error), str(error)
    else:
        raise AssertionError("expected AssetError containing %r" % message)


def mutate_faceplate(folder, mutation, metadata=None):
    path = os.path.join(folder, "GlowFaceplate.png")
    width, height, mode, raw = validator._png_info(path)
    assert mode == "RGBA"
    image = bytearray(b"".join(validator._unfilter_rgba(raw, width, height)))
    mutation(image, width, height)
    fields = validator._png_text(path)
    fields.update(metadata or {})
    fields["fireflow-pixel-sha256"] = hashlib.sha256(image).hexdigest()
    validator._png(path, width, height, image, fields)


def pixel_xy(x_mm, y_mm):
    return (round(x_mm * validator.RACK_PX_PER_MM - 0.5),
            round(y_mm * validator.RACK_PX_PER_MM - 0.5))


def alpha_at(image, width, x, y):
    return image[(y * width + x) * 4 + 3]


def transitions(image, width, *, axis, fixed, start, end):
    values = []
    for position in range(start, end + 1):
        x, y = (position, fixed) if axis == "x" else (fixed, position)
        values.append(alpha_at(image, width, x, y) >= 128)
    return [start + index for index in range(1, len(values))
            if values[index] != values[index - 1]]


def extend_opening_at_transition(image, width, height, *, axis, fixed,
                                 start, end, transition_index=0):
    found = transitions(image, width, axis=axis, fixed=fixed,
                        start=start, end=end)
    assert len(found) > transition_index, found
    edge = found[transition_index]
    # The selected transitions are visible -> transparent. Extend the opening
    # three pixels into the visible side, beyond the one-pixel tolerance.
    for position in range(edge - 3, edge):
        for offset in (-1, 0, 1):
            x, y = ((position, fixed + offset) if axis == "x"
                    else (fixed + offset, position))
            assert 0 <= x < width and 0 <= y < height
            image[(y * width + x) * 4:(y * width + x + 1) * 4] = b"\0\0\0\0"


def test_missing_asset():
    with tempfile.TemporaryDirectory() as folder:
        write_valid_assets(folder)
        os.remove(os.path.join(folder, "GlowRear.png"))
        expect_error(folder, package_list(), "GlowRear.png is missing")


def test_panel_requires_exact_width():
    with tempfile.TemporaryDirectory() as folder:
        write_valid_assets(folder)
        write_png(os.path.join(folder, "GlowFaceplate.png"), (959, 1520))
        expect_error(folder, package_list(), "GlowFaceplate.png has size 959x1520")


def test_assets_require_alpha_channel():
    with tempfile.TemporaryDirectory() as folder:
        write_valid_assets(folder)
        write_png(os.path.join(folder, "GlowTouch.png"), (960, 1520), "RGB")
        expect_error(folder, package_list(), "GlowTouch.png has mode RGB")


def test_switches_require_identical_sizes():
    with tempfile.TemporaryDirectory() as folder:
        write_valid_assets(folder)
        write_png(os.path.join(folder, "GlowSwitchUp.png"), (95, 192))
        expect_error(folder, package_list(), "switch assets do not have identical dimensions")


def test_switches_require_their_exact_size():
    with tempfile.TemporaryDirectory() as folder:
        write_valid_assets(folder)
        for name in SWITCH_ASSETS:
            write_png(os.path.join(folder, name), (95, 192))
        expect_error(folder, package_list(), "GlowSwitchDown.png has size 95x192")


def test_switches_require_visible_alpha():
    with tempfile.TemporaryDirectory() as folder:
        write_valid_assets(folder)
        write_png(os.path.join(folder, "GlowSwitchCenter.png"), (96, 192), alpha=0)
        expect_error(folder, package_list(), "GlowSwitchCenter.png is fully transparent")


def test_every_asset_is_explicitly_packaged():
    with tempfile.TemporaryDirectory() as folder:
        write_valid_assets(folder)
        makefile = package_list().replace("res/GlowSwitchDown.png ", "")
        expect_error(folder, makefile, "GlowSwitchDown.png is absent from DISTRIBUTABLES")


def test_comments_and_other_variables_do_not_package_assets():
    with tempfile.TemporaryDirectory() as folder:
        write_valid_assets(folder)
        makefile = ("DISTRIBUTABLES += res/Glow.svg\n"
                    "# DISTRIBUTABLES += res/GlowRear.png\nOTHER = " +
                    package_list())
        expect_error(folder, makefile, "GlowRear.png is absent from DISTRIBUTABLES")


def test_fallback_svg_must_exist():
    with tempfile.TemporaryDirectory() as folder:
        write_valid_assets(folder)
        os.remove(os.path.join(folder, "Glow.svg"))
        expect_error(folder, package_list(), "Glow.svg is missing")


def test_fallback_svg_must_be_packaged():
    with tempfile.TemporaryDirectory() as folder:
        write_valid_assets(folder)
        expect_error(folder, package_list().replace("res/Glow.svg ", ""),
                     "Glow.svg is absent from DISTRIBUTABLES")


def test_manual_faceplate_pixel_edit_cannot_self_authorize_with_new_hash():
    with tempfile.TemporaryDirectory() as folder:
        write_valid_assets(folder)

        def edit(image, width, height):
            offset = ((height - 1) * width + width - 1) * 4
            image[offset:offset + 4] = bytes((255, 0, 255, 255))

        mutate_faceplate(folder, edit)
        expect_error(folder, package_list(),
                     "pixels differ from regenerated KiCad derivative")


def test_altered_faceplate_derivation_metadata_is_rejected():
    with tempfile.TemporaryDirectory() as folder:
        write_valid_assets(folder)
        mutate_faceplate(folder, lambda image, width, height: None,
                         {"fireflow-transform-mm": "s=1;tx=3;ty=8.5"})
        expect_error(folder, package_list(),
                     "derivation metadata differs: fireflow-transform-mm")


def test_enlarged_mount_contour_is_rejected_by_edge_transition():
    with tempfile.TemporaryDirectory() as folder:
        write_valid_assets(folder)
        cx, cy = validator.FACEPLATE_FIDUCIALS_MM["mount_left_top"]
        px, py = pixel_xy(cx + validator.FACEPLATE_TX_MM,
                          cy + validator.FACEPLATE_TY_MM)
        radius = round(4.0 * validator.RACK_PX_PER_MM)
        mutate_faceplate(folder, lambda image, width, height:
                         extend_opening_at_transition(
                             image, width, height, axis="x", fixed=py,
                             start=px - radius, end=px + radius))
        expect_error(folder, package_list(),
                     "mount_left_top contour transition")


def test_enlarged_fader_endpoint_is_rejected_by_edge_transition():
    with tempfile.TemporaryDirectory() as folder:
        write_valid_assets(folder)
        cx, cy = validator.FACEPLATE_FIDUCIALS_MM["fader_left_top_inset_1px"]
        px, py = pixel_xy(cx + validator.FACEPLATE_TX_MM,
                          cy + validator.FACEPLATE_TY_MM)
        radius = round(3.0 * validator.RACK_PX_PER_MM)
        mutate_faceplate(folder, lambda image, width, height:
                         extend_opening_at_transition(
                             image, width, height, axis="y", fixed=px,
                             start=py - radius, end=py + radius,
                             transition_index=1))
        expect_error(folder, package_list(),
                     "fader_left_top contour transition")


def test_shifted_diagonal_contour_is_rejected_by_edge_transition():
    with tempfile.TemporaryDirectory() as folder:
        write_valid_assets(folder)
        search_start, search_end = validator.DIAGONAL_PROBE_MM["search_x"]
        _px, py = pixel_xy(0.0, validator.DIAGONAL_PROBE_MM["row_y"] +
                           validator.FACEPLATE_TY_MM)
        start, _ = pixel_xy(search_start + validator.FACEPLATE_TX_MM, 0.0)
        end, _ = pixel_xy(search_end + validator.FACEPLATE_TX_MM, 0.0)
        mutate_faceplate(folder, lambda image, width, height:
                         extend_opening_at_transition(
                             image, width, height, axis="x", fixed=py,
                             start=start, end=end))
        expect_error(folder, package_list(),
                     "diagonal contour transition")


if __name__ == "__main__":
    for name, function in sorted(globals().items()):
        if name.startswith("test_") and callable(function):
            function()
    print("assets tests OK")
