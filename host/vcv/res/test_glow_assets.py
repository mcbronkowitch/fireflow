#!/usr/bin/env python3
"""Tests for the distributable Glow raster-asset contract.

Plain asserts keep this runnable in a Rack checkout without pytest.  Every
negative case uses a real temporary PNG fixture, not a mocked image reader.
"""
import os
import struct
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from validate_glow_assets import (PANEL_ASSETS, SWITCH_ASSETS, AssetError,
                                  validate_assets)


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
        write_png(os.path.join(folder, name), (width, height), mode)
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


if __name__ == "__main__":
    for name, function in sorted(globals().items()):
        if name.startswith("test_") and callable(function):
            function()
    print("assets tests OK")
