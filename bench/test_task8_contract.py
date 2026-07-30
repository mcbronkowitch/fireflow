"""Source contract for the matched Task 8 direct-engine bench rows."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SYSTEM_SOURCE = ROOT / "bench" / "workloads_system.cpp"
BODY_SOURCE = ROOT / "bench" / "workloads_body.cpp"
# setup_engine_2x4 / proc_engine_2x4 and kEngine2x4Pitches were hoisted out of
# workloads_system.cpp into their own header (Task 13, body-resonator-engine)
# so workloads_body.cpp could reuse them for BodyEngine without copying them
# -- a pure move, so the source contract below moves with it rather than
# weakening to "the symbol exists somewhere".
ENGINE_2X4_SOURCE = ROOT / "bench" / "engine_2x4.h"
MAKEFILE = ROOT / "bench" / "Makefile"
GENERATOR = ROOT / "tools" / "bake_wavetables.py"
BANK_SOURCE = ROOT / "engine" / "synth" / "wt_bank.cpp"
LINKER = ROOT / "alt_sram.lds"
RUNNER = ROOT / "bench" / "run.py"
OPENOCD_CFG = ROOT / "bench" / "openocd" / "spotykach-sram.cfg"
QSPI_PROGRAMMER_CFG = ROOT / "bench" / "openocd" / "qspi-programmer.cfg"
QSPI_PROGRAMMER_MAIN = ROOT / "bench" / "qspi_programmer" / "main.cpp"
QSPI_PROGRAMMER_MAKEFILE = ROOT / "bench" / "qspi_programmer" / "Makefile"
QSPI_PROGRAMMER_CORE = ROOT / "bench" / "qspi_programmer" / "program_core.h"
REPORT_SOURCE = ROOT / "bench" / "report.cpp"
RAND_SHIM = ROOT / "bench" / "rand_shim.cpp"
BENCH_MAIN = ROOT / "bench" / "main.cpp"
SAMPLE_SHA = "1dd351d78cd1d087308862838ac4960da1113e1061ea48003af62a2c56a83193"


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
        cls.body_source = BODY_SOURCE.read_text(encoding="utf-8")
        cls.engine_2x4 = ENGINE_2X4_SOURCE.read_text(encoding="utf-8")
        cls.makefile = MAKEFILE.read_text(encoding="utf-8")

    def test_shipping_recipe_uses_the_accepted_o3_mode(self) -> None:
        root_makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertRegex(root_makefile, r"(?m)^OPT\s*=\s*-O3$")
        self.assertNotRegex(root_makefile, r"(?m)^LDFLAGS\s*\+=\s*-flto$")

    def test_engine_2x4_is_hoisted_and_included_not_copied(self) -> None:
        # The pure-move contract itself: both workload files reach the shared
        # header instead of each carrying their own copy.
        self.assertIn('#include "engine_2x4.h"', self.source)
        self.assertIn('#include "engine_2x4.h"', self.body_source)
        # workloads_system.cpp and workloads_body.cpp only ever CALL these
        # names (setup_engine_2x4(pair.a, pair.b), proc_engine_2x4(pair.a,
        # pair.b)); neither file defines them again -- function_body() would
        # find the header's own definition first if a stray copy existed
        # anywhere before it in the file, so the real guard is that grepping
        # each workload file for the definition signature (name followed by
        # a template parameter list, not a two-argument call) comes up empty.
        for source in (self.source, self.body_source):
            self.assertNotRegex(
                compact(source),
                r"\btemplate\s*<\s*class\s+EngineT\s*>\s*(void|float)\s+"
                r"(setup|proc)_engine_2x4",
            )

    def test_common_setup_is_exactly_matched(self) -> None:
        normalized = compact(self.engine_2x4)
        self.assertIn(
            "constexpr float kEngine2x4Pitches[] = "
            "{ 0.25f, 0.35f, 0.45f, 0.55f };",
            normalized,
        )
        body = compact(function_body(self.engine_2x4, "setup_engine_2x4"))
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
        # Call sites live in workloads_system.cpp, not the header.
        system_normalized = compact(self.source)
        self.assertIn(
            "auto& pair = g_system_arena.emplace<SynthPairGroup>(); "
            "setup_engine_2x4(pair.a, pair.b);",
            system_normalized,
        )
        self.assertIn(
            "auto& pair = g_system_arena.emplace<WavePairGroup>(); "
            "setup_engine_2x4(pair.a, pair.b);",
            system_normalized,
        )

    def test_process_contract_renders_two_engines_for_one_block(self) -> None:
        body = compact(function_body(self.engine_2x4, "proc_engine_2x4"))
        for expected in (
            "for (size_t i = 0; i < kBlock; ++i)",
            "a.process(a_l, a_r);",
            "b.process(b_l, b_r);",
            "acc += a_l + a_r + b_l + b_r;",
            "acc += static_cast<float>(a.active_voices());",
            "acc += static_cast<float>(b.active_voices());",
        ):
            self.assertIn(expected, body)
        # Call sites live in workloads_system.cpp, not the header.
        system_normalized = compact(self.source)
        self.assertIn(
            "auto& pair = g_system_arena.get<SynthPairGroup>(); "
            "return proc_engine_2x4(pair.a, pair.b);",
            system_normalized,
        )
        self.assertIn(
            "auto& pair = g_system_arena.get<WavePairGroup>(); "
            "return proc_engine_2x4(pair.a, pair.b);",
            system_normalized,
        )

    def test_serial_system_groups_do_not_coexist_as_globals(self) -> None:
        normalized = compact(self.source)
        for group in (
            "ModGroup",
            "SynthGroup",
            "SynthPairGroup",
            "WavePairGroup",
            "FxGroup",
            "InstrumentGroup",
        ):
            self.assertIn(f"struct {group}", normalized)
        self.assertIn("SerialArena<", self.source)
        self.assertIn("g_system_arena", self.source)
        for obsolete in (
            "SuperModulator g_mod_a",
            "Part g_hook_a",
            "SynthEngine g_synth;",
            "SynthEngine g_synth_2x4_a",
            "WaveEngine g_wave_2x4_a",
            "PartFx g_fx;",
            "Instrument g_inst;",
        ):
            self.assertNotIn(obsolete, self.source)

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
            "SRAM_EXEC (RWX) : ORIGIN = 0x24000000, LENGTH = 0x402E0",
            linker,
        )
        self.assertIn(
            "SRAM (RWX) : ORIGIN = 0x240402E0, LENGTH = 0x3FD20",
            linker,
        )
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

    def test_report_format_scratch_is_preserved_in_dtcm_not_axi_bss(self) -> None:
        report = REPORT_SOURCE.read_text(encoding="utf-8")
        self.assertIn(
            '#define DTCM_REPORT_BSS __attribute__((section(".dtcmram_bss")))',
            report,
        )
        self.assertIn("char DTCM_REPORT_BSS g_buf[256];", report)
        body = compact(function_body(report, "logf"))
        self.assertIn("vsnprintf(g_buf, sizeof(g_buf), fmt, ap);", body)

    def test_qspi_programmer_is_an_sram_only_debug_helper(self) -> None:
        makefile = QSPI_PROGRAMMER_MAKEFILE.read_text(encoding="utf-8")
        source = compact(QSPI_PROGRAMMER_MAIN.read_text(encoding="utf-8"))
        core = compact(QSPI_PROGRAMMER_CORE.read_text(encoding="utf-8"))
        cfg = QSPI_PROGRAMMER_CFG.read_text(encoding="utf-8")
        self.assertIn("APP_TYPE = BOOT_SRAM", makefile)
        self.assertIn("program_core.h", source)
        self.assertIn("EraseBlock(offset, false)", source)
        self.assertIn("kQspiPayloadOffset = 0x00040000u", core)
        self.assertIn("0x24040000u", source)
        self.assertIn("0x90040000u", source)
        self.assertIn("QSPI_PROGRAM_OK,90040000,65024,", source)
        self.assertIn("load_image $HELPER", cfg)
        self.assertIn("load_image $PAYLOAD 0x24040000 bin", cfg)
        self.assertNotIn("reset run", cfg)

    def test_bench_rand_is_deterministic_dtcm_and_heap_free(self) -> None:
        source = RAND_SHIM.read_text(encoding="utf-8")
        self.assertIn("rand_shim.cpp", self.makefile)
        self.assertIn('extern "C" int rand(void)', source)
        self.assertIn('extern "C" void srand(unsigned int seed)', source)
        self.assertIn('section(".dtcmram_bss")', source)
        for allocator in ("malloc(", "calloc(", "realloc(", "free("):
            self.assertNotIn(allocator, source)

    def test_bench_seeds_retained_dtcm_rand_state_before_workloads(self) -> None:
        main = compact(BENCH_MAIN.read_text(encoding="utf-8"))
        self.assertIn("::srand(1u);", main)
        hardware_init = main.index("hw.Init(true);")
        seed = main.index("::srand(1u);")
        first_workload = main.index("bench::run_workload(w)")
        self.assertLess(hardware_init, seed)
        self.assertLess(seed, first_workload)


if __name__ == "__main__":
    unittest.main()
