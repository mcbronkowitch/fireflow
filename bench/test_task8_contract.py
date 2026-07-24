"""Source contract for the matched Task 8 direct-engine bench rows."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SYSTEM_SOURCE = ROOT / "bench" / "workloads_system.cpp"
MAKEFILE = ROOT / "bench" / "Makefile"
GENERATOR = ROOT / "tools" / "bake_wavetables.py"
BANK_SOURCE = ROOT / "engine" / "synth" / "wt_bank.cpp"
LINKER = ROOT / "alt_sram.lds"
RUNNER = ROOT / "bench" / "run.py"
OPENOCD_CFG = ROOT / "bench" / "openocd" / "spotykach-sram.cfg"
SAMPLE_SHA = "81a914d0248bc7265703b81e27e4546264993705c11c1e30acd45cae2390e747"


def compact(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", source)
    if not match:
        raise AssertionError(f"missing function {name}")
    start = source.index("{", match.start())
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1:index]
    raise AssertionError(f"unterminated function {name}")


class Task8Contract(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = SYSTEM_SOURCE.read_text(encoding="utf-8")
        cls.makefile = MAKEFILE.read_text(encoding="utf-8")

    def test_common_setup_is_exactly_matched(self) -> None:
        normalized = compact(self.source)
        self.assertIn(
            "constexpr float kEngine2x4Pitches[] = "
            "{ 0.25f, 0.35f, 0.45f, 0.55f };",
            normalized,
        )
        body = compact(function_body(self.source, "setup_engine_2x4"))
        for expected in (
            "a.set_seed(3u);",
            "b.set_seed(4u);",
            "a.init(kSampleRate);",
            "b.init(kSampleRate);",
            "a.set_decay(1.f);",
            "b.set_decay(1.f);",
            "a.set_cycle(2.f);",
            "b.set_cycle(2.f);",
            "a.set_flow(false);",
            "b.set_flow(false);",
            "for (float pitch : kEngine2x4Pitches) "
            "{ a.trigger(pitch); b.trigger(pitch); }",
        ):
            self.assertIn(expected, body)
        self.assertIn(
            "void setup_synth_2x4() "
            "{ setup_engine_2x4(g_synth_2x4_a, g_synth_2x4_b); }",
            normalized,
        )
        self.assertIn(
            "void setup_wave_2x4() "
            "{ setup_engine_2x4(g_wave_2x4_a, g_wave_2x4_b); }",
            normalized,
        )

    def test_process_contract_renders_two_engines_for_one_block(self) -> None:
        normalized = compact(self.source)
        body = compact(function_body(self.source, "proc_engine_2x4"))
        for expected in (
            "for (size_t i = 0; i < kBlock; ++i)",
            "a.process(a_l, a_r);",
            "b.process(b_l, b_r);",
            "acc += a_l + a_r + b_l + b_r;",
            "acc += static_cast<float>(a.active_voices());",
            "acc += static_cast<float>(b.active_voices());",
        ):
            self.assertIn(expected, body)
        self.assertIn(
            "float proc_synth_2x4() "
            "{ return proc_engine_2x4(g_synth_2x4_a, g_synth_2x4_b); }",
            normalized,
        )
        self.assertIn(
            "float proc_wave_2x4() "
            "{ return proc_engine_2x4(g_wave_2x4_a, g_wave_2x4_b); }",
            normalized,
        )

    def test_rows_and_bank_source_are_linked_together(self) -> None:
        rows = compact(self.source)
        self.assertIn(
            '{ "system", "synth_2x4", setup_synth_2x4, proc_synth_2x4 }, '
            '{ "system", "wave_2x4", setup_wave_2x4, proc_wave_2x4 },',
            rows,
        )
        source_entries = [
            line.strip().rstrip("\\").strip()
            for line in self.makefile.splitlines()
            if line.strip().startswith("../engine/synth/")
        ]
        self.assertIn("../engine/synth/wt_bank.cpp", source_entries)
        self.assertEqual(
            source_entries[-3:],
            [
                "../engine/synth/voice.cpp",
                "../engine/synth/synth_engine.cpp",
                "../engine/synth/wt_bank.cpp",
            ],
        )

    def test_generator_owns_arm_only_qspi_placement_without_sample_drift(self) -> None:
        generator = GENERATOR.read_text(encoding="utf-8")
        bank = BANK_SOURCE.read_text(encoding="utf-8")
        expected_macro = compact(
            """
            #if defined(__ARM_EABI__)
            #define WT_BANK_QSPI __attribute__((section(".qspiflash_data")))
            #else
            #define WT_BANK_QSPI
            #endif
            """
        )
        for generated_line in (
            '"#if defined(__ARM_EABI__)",',
            '"#define WT_BANK_QSPI '
            '__attribute__((section(\\".qspiflash_data\\")))",',
            '"#else",',
            '"#define WT_BANK_QSPI",',
            '"#endif",',
            '"const int16_t kBankSamples[kTotalSamples] WT_BANK_QSPI = {",',
        ):
            self.assertIn(generated_line, generator)
        self.assertIn(expected_macro, compact(bank))
        self.assertIn(
            "const int16_t kBankSamples[kTotalSamples] WT_BANK_QSPI = {",
            bank,
        )
        self.assertIn(f"// Sample SHA-256: {SAMPLE_SHA}", bank)

    def test_qspi_region_and_sram_loader_respect_split_image_contract(self) -> None:
        linker = compact(LINKER.read_text(encoding="utf-8"))
        self.assertIn(
            "QSPIFLASH (RX) : ORIGIN = 0x90040000, LENGTH = 7936K",
            linker,
        )
        runner = RUNNER.read_text(encoding="utf-8")
        self.assertIn('"bench-sram.elf"', runner)
        self.assertIn("require_verified_payload", runner)
        self.assertIn("prepare_existing_artifacts", runner)
        self.assertIn("require_live_digest", runner)
        self.assertIn("require_clean_tree", runner)
        cfg = OPENOCD_CFG.read_text(encoding="utf-8")
        self.assertIn("load_image $IMAGE", cfg)


if __name__ == "__main__":
    unittest.main()
