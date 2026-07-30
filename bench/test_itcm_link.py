"""Linked-address contract for the optional ITCM audio hotset."""

from pathlib import Path
import re
import subprocess
import unittest


HERE = Path(__file__).resolve().parent
NM = Path(r"C:\Program Files\DaisyToolchain\bin\arm-none-eabi-nm.exe")
OBJDUMP = Path(
    r"C:\Program Files\DaisyToolchain\bin\arm-none-eabi-objdump.exe"
)
READELF = Path(
    r"C:\Program Files\DaisyToolchain\bin\arm-none-eabi-readelf.exe"
)
ELF = HERE / "build" / "bench.elf"


def symbol_address(nm_text, fragment):
    for line in nm_text.splitlines():
        if fragment in line:
            match = re.match(r"^([0-9a-fA-F]+)\s", line)
            if match:
                return int(match.group(1), 16)
    raise AssertionError("linked symbol missing: %s" % fragment)


@unittest.skipUnless(NM.is_file(), "Daisy Arm toolchain is not installed")
class ItcmLinkContract(unittest.TestCase):
    def test_audio_hotset_links_into_itcm_and_data_stays_in_dtcm(self):
        subprocess.run(
            [
                "make",
                "-j8",
                "BENCH_FAMILIES=system",
                "BENCH_ITCM_HOT=1",
                "build/bench.elf",
            ],
            cwd=HERE,
            check=True,
            capture_output=True,
            text=True,
        )
        linked = subprocess.run(
            [str(NM), "--print-size", "--size-sort", "-C", str(ELF)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout

        hot_symbols = (
            "spky::Instrument::process(",
            "spky::Part::_control_tick()",
            "spky::PartFx::process(",
            "spky::Flux::process(",
            "spky::BbdLine::Process(",
            "spky::Grit::process(",
            "spky::AmbientReverb::process(",
            "spky::Comp::process(",
            "spky::VoiceT<spky::MorphOsc>::process(",
            "spky::SynthEngineT<spky::VoiceT<spky::MorphOsc> >::process(",
        )
        for symbol in hot_symbols:
            with self.subTest(symbol=symbol):
                address = symbol_address(linked, symbol)
                self.assertGreaterEqual(address, 0x00000100)
                self.assertLess(address, 0x00010000)

        dtcm_address = symbol_address(linked, "g_dtcm_instrument_storage")
        self.assertGreaterEqual(dtcm_address, 0x20000000)
        self.assertLess(dtcm_address, 0x20020000)

        sections = subprocess.run(
            [str(OBJDUMP), "-h", str(ELF)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        hot_section = re.search(
            r"^\s*\d+\s+\.itcm_audio_hot\s+"
            r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)",
            sections,
            re.MULTILINE,
        )
        self.assertIsNotNone(hot_section)
        size, vma, lma = (
            int(value, 16) for value in hot_section.groups()
        )
        self.assertGreater(size, 0)
        self.assertLessEqual(size, 0x10000)
        self.assertEqual(vma, 0x00000100)
        self.assertEqual(lma, 0x00000100)

        program_headers = subprocess.run(
            [str(READELF), "--wide", "--segments", str(ELF)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        self.assertRegex(
            program_headers,
            r"LOAD\s+\S+\s+0x00000100\s+0x00000100\s+"
            r"\S+\s+\S+\s+R E\s+",
        )
        self.assertRegex(program_headers, r"\.itcm_audio_hot")
