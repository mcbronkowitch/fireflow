"""Fail-closed contracts for the production ITCM placement preflight."""

from pathlib import Path
import tempfile
import unittest


try:
    import itcm_placement
except ImportError:
    itcm_placement = None


HOT_SYMBOLS = (
    "spky::Instrument::process(",
    "spky::Part::_control_tick()",
    "spky::PartFx::process(",
    "spky::Flux::process(",
    "spky::BbdLine::Process(",
    "spky::BbdEngine::process(",
    "spky::Grit::process(",
    "spky::AmbientReverb::process(",
    "spky::Comp::process(",
    "spky::VoiceT<spky::MorphOsc>::process(",
    "spky::SynthEngineT<spky::VoiceT<spky::MorphOsc> >::process(",
)


def valid_nm_text():
    lines = [
        "%08x 00000020 T %s" % (0x100 + index * 0x40, symbol)
        for index, symbol in enumerate(HOT_SYMBOLS)
    ]
    lines.append(
        "200005c8 0000c280 b "
        "bench::(anonymous namespace)::g_dtcm_instrument_storage"
    )
    return "\n".join(lines)


VALID_SECTIONS = """
Idx Name            Size      VMA       LMA       File off  Algn
  0 .itcm_audio_hot 0000a400  00000100  00000100  00010100  2**3
                    CONTENTS, ALLOC, LOAD, READONLY, CODE
"""

VALID_SEGMENTS = """
Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
LOAD           0x010100 0x00000100 0x00000100 0x0a400 0x0a400 R E 0x10000

 Section to Segment mapping:
  Segment Sections...
   00     .itcm_audio_hot
"""


class ItcmPlacementContract(unittest.TestCase):
    def require_module(self):
        self.assertIsNotNone(
            itcm_placement,
            "the production ITCM placement preflight module is missing",
        )
        return itcm_placement

    def test_valid_outputs_require_hot_symbols_itcm_load_and_dtcm_storage(self):
        placement = self.require_module().validate_itcm_outputs(
            valid_nm_text(),
            VALID_SECTIONS,
            VALID_SEGMENTS,
        )

        self.assertEqual(placement["hot_size"], 0xA400)
        self.assertEqual(placement["dtcm_storage_size"], 0xC280)

    def test_empty_hot_section_is_rejected(self):
        module = self.require_module()
        empty_sections = VALID_SECTIONS.replace("0000a400", "00000000")

        with self.assertRaisesRegex(module.ItcmPlacementError, "empty"):
            module.validate_itcm_outputs(
                valid_nm_text(),
                empty_sections,
                VALID_SEGMENTS,
            )

    def test_hot_section_must_map_to_the_itcm_load_segment(self):
        module = self.require_module()
        wrong_mapping = VALID_SEGMENTS.replace(
            "00     .itcm_audio_hot",
            "00\n   01     .itcm_audio_hot",
        )

        with self.assertRaisesRegex(
            module.ItcmPlacementError,
            "not mapped to",
        ):
            module.validate_itcm_outputs(
                valid_nm_text(),
                VALID_SECTIONS,
                wrong_mapping,
            )

    def test_missing_inspection_tool_fails_closed(self):
        module = self.require_module()
        with tempfile.TemporaryDirectory() as temp:
            elf = Path(temp) / "bench.elf"
            elf.write_bytes(b"ELF fixture")

            with self.assertRaisesRegex(
                module.ItcmPlacementError,
                "inspection tool .* not found",
            ):
                module.validate_itcm_placement(
                    elf,
                    nm=Path(temp) / "missing-nm",
                    objdump=Path(temp) / "missing-objdump",
                    readelf=Path(temp) / "missing-readelf",
                )


if __name__ == "__main__":
    unittest.main()
