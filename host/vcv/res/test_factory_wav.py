#!/usr/bin/env python3
import struct
from pathlib import Path

WAV = Path(__file__).with_name("factory.wav")
RATE = 48_000
CHANNELS = 2
BITS = 24
EXPECTED_FRAMES = round(RATE * 4 * 4 * 60 / 110)


def riff_chunks(raw):
    assert raw[:4] == b"RIFF" and raw[8:12] == b"WAVE", "not RIFF/WAVE"
    pos = 12
    while pos + 8 <= len(raw):
        chunk_id = raw[pos:pos + 4]
        size = struct.unpack_from("<I", raw, pos + 4)[0]
        start = pos + 8
        end = start + size
        assert end <= len(raw), f"{chunk_id!r} runs past EOF"
        yield chunk_id, raw[start:end]
        pos = end + (size & 1)


def main():
    raw = WAV.read_bytes()
    chunks = dict(riff_chunks(raw))
    assert b"fmt " in chunks and b"data" in chunks, "missing fmt/data chunk"
    fmt = chunks[b"fmt "]
    tag, channels, rate, _byte_rate, block_align, bits = struct.unpack_from(
        "<HHIIHH", fmt
    )
    if tag == 0xFFFE:
        assert len(fmt) >= 40, "short extensible fmt chunk"
        tag = struct.unpack_from("<H", fmt, 24)[0]
    assert tag == 1, f"format tag {tag}, want integer PCM"
    assert channels == CHANNELS, f"{channels} channels, want {CHANNELS}"
    assert rate == RATE, f"{rate} Hz, want {RATE}"
    assert bits == BITS, f"{bits}-bit, want {BITS}-bit"
    frames = len(chunks[b"data"]) // block_align
    assert abs(frames - EXPECTED_FRAMES) <= 1, (
        f"{frames} frames, want {EXPECTED_FRAMES} +/- 1"
    )
    print(
        f"PASS -- factory.wav: {frames} frames, "
        f"{channels}ch, {rate} Hz, {bits}-bit PCM"
    )


if __name__ == "__main__":
    main()
