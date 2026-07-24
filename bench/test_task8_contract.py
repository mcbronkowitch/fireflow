"""Source contract for the matched Task 8 direct-engine bench rows."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SYSTEM_SOURCE = ROOT / "bench" / "workloads_system.cpp"
MAKEFILE = ROOT / "bench" / "Makefile"


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


if __name__ == "__main__":
    unittest.main()
