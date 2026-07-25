import tempfile
import unittest
from pathlib import Path

from bench.audition.launch import (
    LayoutError,
    build_openocd_command,
    parse_nm_symbols,
    parse_objdump_sections,
    validate_openocd_config,
    validate_layout,
)


class LaunchContractTests(unittest.TestCase):
    def test_parses_and_validates_the_split_memory_layout(self):
        sections = parse_objdump_sections(
            """
  0 .isr_vector   00000298  24000000  24000000  00010000  2**2
 18 .qspiflash_data 0000fe00  90040000  90040000  00050000  2**2
"""
        )
        symbols = parse_nm_symbols(
            """
20000004 D g_audition_state
c0a8473c B g_factory_upload
"""
        )
        validate_layout(sections, symbols)

    def test_rejects_any_qspi_payload_outside_the_read_only_wave_bank(self):
        sections = parse_objdump_sections(
            "18 .qspiflash_data 0000fe00 90050000 90050000 00050000 2**2"
        )
        symbols = {
            "g_audition_state": 0x20000004,
            "g_factory_upload": 0xC0A8473C,
        }
        with self.assertRaisesRegex(LayoutError, "0x90040000"):
            validate_layout(sections, symbols)

    def test_openocd_command_passes_only_load_and_verify_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            command = build_openocd_command(
                openocd=root / "openocd.exe",
                scripts=root / "scripts",
                interface="stlink-dap.cfg",
                config=root / "audition.cfg",
                sram_elf=root / "audition-sram.elf",
                qspi_payload=root / "audition-qspi.bin",
                factory_bin=root / "factory.bin",
                state_address=0x20000004,
                upload_address=0xC0A8473C,
            )
        joined = " ".join(command).replace("\\", "/")
        self.assertIn("set STATE_ADDRESS 0x20000004", joined)
        self.assertIn("set UPLOAD_ADDRESS 0xc0a8473c", joined)
        self.assertIn("interface/stlink-dap.cfg", joined)
        self.assertNotIn("program ", joined)
        self.assertNotIn("flash ", joined)

    def test_rejects_persistent_write_commands_in_openocd_config(self):
        with self.assertRaisesRegex(LayoutError, "persistent write"):
            validate_openocd_config("init\nflash write_image erase payload.bin")


if __name__ == "__main__":
    unittest.main()
