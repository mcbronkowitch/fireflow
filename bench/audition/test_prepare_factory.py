import math
import struct
import tempfile
import unittest
import wave
from pathlib import Path

from bench.audition.prepare_factory import prepare


REPO = Path(__file__).resolve().parents[2]
FACTORY_WAV = REPO / "host" / "vcv" / "res" / "factory.wav"


def pcm24(value: int) -> bytes:
    if not -8388608 <= value <= 8388607:
        raise ValueError("24-bit sample out of range")
    return (value & 0xFFFFFF).to_bytes(3, "little")


class PrepareFactoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.output = root / "factory-planar-f32.bin"
        self.header = root / "factory_meta.h"

    def tearDown(self):
        self.temp.cleanup()

    def make_pcm24(self, frames, sample_rate=48000):
        path = Path(self.temp.name) / "fixture.wav"
        with wave.open(str(path), "wb") as target:
            target.setnchannels(2)
            target.setsampwidth(3)
            target.setframerate(sample_rate)
            target.writeframes(
                b"".join(pcm24(left) + pcm24(right)
                         for left, right in frames)
            )
        return path

    def test_converts_stereo_24_bit_pcm_to_planar_float(self):
        source = self.make_pcm24(
            [(0, 8388607), (-8388608, 4194304)]
        )

        metadata = prepare(source, self.output, self.header)

        self.assertEqual(metadata.frames, 2)
        actual = struct.unpack("<4f", self.output.read_bytes())
        expected = (0.0, -1.0, 8388607 / 8388608.0, 0.5)
        for got, want in zip(actual, expected):
            self.assertAlmostEqual(got, want, places=6)

    def test_rejects_non_48khz_source(self):
        source = self.make_pcm24([(0, 0)], sample_rate=44100)

        with self.assertRaisesRegex(ValueError, "48000 Hz"):
            prepare(source, self.output, self.header)

    def test_real_factory_file_has_finite_stereo_output(self):
        metadata = prepare(FACTORY_WAV, self.output, self.header)

        self.assertGreater(metadata.frames, 0)
        self.assertEqual(
            self.output.stat().st_size,
            metadata.frames * 2 * struct.calcsize("<f"),
        )
        self.assertTrue(all(math.isfinite(value)
                            for value in metadata.sentinels))
        header = self.header.read_text(encoding="utf-8")
        self.assertIn(
            f"kFactoryFrames = {metadata.frames}u", header
        )
        self.assertIn(
            f"kFactoryFnv1a = 0x{metadata.fnv1a:08x}u", header
        )


if __name__ == "__main__":
    unittest.main()
