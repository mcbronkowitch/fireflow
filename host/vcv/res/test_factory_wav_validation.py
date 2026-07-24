#!/usr/bin/env python3
"""Regression cases for the factory WAV format guard."""
import importlib.util
import struct
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
GUARD_PATH = HERE / "test_factory_wav.py"
SPEC = importlib.util.spec_from_file_location("factory_wav_guard", GUARD_PATH)
guard = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(guard)


def with_data_byte(raw):
    """Append a byte to data and keep RIFF/data sizes internally consistent."""
    corrupted = bytearray(raw)
    data = corrupted.index(b"data")
    size = struct.unpack_from("<I", corrupted, data + 4)[0]
    struct.pack_into("<I", corrupted, data + 4, size + 1)
    corrupted.extend(b"\0\0")  # One data byte plus RIFF's required pad byte.
    struct.pack_into("<I", corrupted, 4, len(corrupted) - 8)
    return corrupted


def with_bad_byte_rate(raw):
    corrupted = bytearray(raw)
    fmt = corrupted.index(b"fmt ")
    byte_rate = struct.unpack_from("<I", corrupted, fmt + 16)[0]
    struct.pack_into("<I", corrupted, fmt + 16, byte_rate - 1)
    return corrupted


def with_bad_block_align(raw):
    corrupted = bytearray(raw)
    fmt = corrupted.index(b"fmt ")
    block_align = struct.unpack_from("<H", corrupted, fmt + 20)[0]
    struct.pack_into("<H", corrupted, fmt + 20, block_align - 1)
    return corrupted


def assert_rejected(raw, expected):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "factory.wav"
        path.write_bytes(raw)
        previous_wav = guard.WAV
        guard.WAV = path
        try:
            try:
                guard.main()
            except AssertionError as error:
                assert expected in str(error), str(error)
            else:
                raise AssertionError("corrupted WAV was accepted")
        finally:
            guard.WAV = previous_wav


def main():
    raw = guard.WAV.read_bytes()
    assert_rejected(with_data_byte(raw), "data length")
    assert_rejected(with_bad_block_align(raw), "block align")
    assert_rejected(with_bad_byte_rate(raw), "byte rate")
    guard.main()
    print("PASS -- factory WAV synthetic validation cases and real asset")


if __name__ == "__main__":
    main()
