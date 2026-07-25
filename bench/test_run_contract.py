"""Host contracts for fail-closed repeated benchmark evidence."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


RUNNER_PATH = Path(__file__).with_name("run.py")
sys.path.insert(0, str(RUNNER_PATH.parent))
RUNNER_SPEC = importlib.util.spec_from_file_location("bench_runner_contract", RUNNER_PATH)
if RUNNER_SPEC and RUNNER_SPEC.loader:
    runner = importlib.util.module_from_spec(RUNNER_SPEC)
    RUNNER_SPEC.loader.exec_module(runner)
else:
    runner = None

QSPI_SHA256 = "a" * 64
DEVICE_ID = "00112233445566778899aabb"


def bench_row(name, avg_cyc, max_cyc, checksum):
    return (
        "BENCH,system,%s,%d,%d,%.2f,%.2f,%s"
        % (
            name,
            avg_cyc,
            max_cyc,
            100.0 * avg_cyc / runner.BUDGET_CYCLES,
            100.0 * max_cyc / runner.BUDGET_CYCLES,
            checksum,
        )
    )


def capture_lines(
    rows, *, anchors=(), device_id=DEVICE_ID, qspi_sha256=QSPI_SHA256
):
    return [
        "BENCH_BEGIN,%s,480000000,96,dcache+icache,%s,%s"
        % (runner.current_head(), qspi_sha256, device_id),
        *rows,
        *anchors,
        "BENCH_END",
    ]


class RunContract(unittest.TestCase):
    def run_evidence(self, captures):
        with tempfile.TemporaryDirectory() as temp:
            argv = [
                "run.py",
                "--no-build",
                "--repeat",
                str(len(captures)),
                "--out-dir",
                temp,
            ]
            with (
                mock.patch.object(runner, "prepare_existing_artifacts", return_value={}),
                mock.patch.object(runner, "require_clean_tree"),
                mock.patch.object(
                    runner,
                    "require_verified_payload",
                    return_value={"device_id": DEVICE_ID},
                ),
                mock.patch.object(runner, "require_live_digest"),
                mock.patch.object(runner, "require_live_device"),
                mock.patch.object(runner, "run_once", side_effect=captures),
                mock.patch.object(sys, "argv", argv),
            ):
                result = runner.main()
            artifacts = {
                path.suffix: path.read_text(encoding="utf-8")
                for path in Path(temp).iterdir()
            }
            return result, artifacts

    def run_main(self, captures):
        return self.run_evidence(captures)[0]

    def test_repeat_rejects_a_missing_row(self):
        complete = [
            bench_row("synth_2x4", 340000, 350000, "11111111"),
            bench_row("wave_2x4", 300000, 310000, "22222222"),
            bench_row("empty_callback", 2, 12, "33333333"),
        ]
        missing_empty = complete[:2]

        self.assertEqual(
            self.run_main(
                [capture_lines(complete), capture_lines(missing_empty)]
            ),
            2,
        )

    def test_repeat_rejects_duplicate_row_names(self):
        complete = [
            bench_row("synth_2x4", 340000, 350000, "11111111"),
            bench_row("wave_2x4", 300000, 310000, "22222222"),
        ]
        duplicate_wave = complete + [
            bench_row("wave_2x4", 299000, 309000, "22222222")
        ]

        self.assertEqual(
            self.run_main(
                [capture_lines(complete), capture_lines(duplicate_wave)]
            ),
            2,
        )

    def test_repeat_rejects_checksum_drift(self):
        first = [
            bench_row("synth_2x4", 340000, 350000, "11111111"),
            bench_row("wave_2x4", 300000, 310000, "22222222"),
        ]
        drifted = [
            bench_row("synth_2x4", 340100, 350100, "11111111"),
            bench_row("wave_2x4", 300100, 310100, "33333333"),
        ]

        self.assertEqual(
            self.run_main([capture_lines(first), capture_lines(drifted)]),
            2,
        )

    def test_repeat_requires_the_matched_wave_acceptance_rows(self):
        synth_only = [
            bench_row("synth_2x4", 340000, 350000, "11111111")
        ]

        self.assertEqual(
            self.run_main(
                [capture_lines(synth_only), capture_lines(synth_only)]
            ),
            2,
        )

    def test_repeat_rejects_each_wave_acceptance_violation(self):
        cases = {
            "average exceeds synth": [
                bench_row("synth_2x4", 340000, 360000, "11111111"),
                bench_row("wave_2x4", 350000, 350000, "22222222"),
            ],
            "maximum exceeds synth": [
                bench_row("synth_2x4", 340000, 360000, "11111111"),
                bench_row("wave_2x4", 300000, 370000, "22222222"),
            ],
            "maximum reaches block budget": [
                bench_row("synth_2x4", 950000, 970000, "11111111"),
                bench_row("wave_2x4", 900000, runner.BUDGET_CYCLES, "22222222"),
            ],
            "non-numeric WAVE result": [
                bench_row("synth_2x4", 340000, 360000, "11111111"),
                "BENCH,system,wave_2x4,TIMEOUT,TIMEOUT,TIMEOUT,TIMEOUT,22222222",
            ],
        }
        for label, rows in cases.items():
            valid = [
                bench_row("synth_2x4", 340000, 360000, "11111111"),
                bench_row("wave_2x4", 300000, 350000, "22222222"),
            ]
            for failing_run in (1, 2):
                captures = [capture_lines(valid), capture_lines(valid)]
                captures[failing_run - 1] = capture_lines(rows)
                with self.subTest(label=label, failing_run=failing_run):
                    self.assertEqual(self.run_main(captures), 2)

    def test_wave_acceptance_allows_exact_comparison_boundaries(self):
        rows = [
            bench_row(
                "synth_2x4",
                900000,
                runner.BUDGET_CYCLES - 1,
                "11111111",
            ),
            bench_row(
                "wave_2x4",
                900000,
                runner.BUDGET_CYCLES - 1,
                "22222222",
            ),
        ]

        self.assertEqual(
            self.run_main([capture_lines(rows), capture_lines(rows)]),
            0,
        )

    def test_evidence_requires_at_least_two_runs(self):
        rows = [
            bench_row("synth_2x4", 340000, 350000, "11111111"),
            bench_row("wave_2x4", 300000, 310000, "22222222"),
        ]

        self.assertEqual(self.run_main([capture_lines(rows)]), 2)

    def test_repeat_rejects_unstable_device_or_qspi_identity(self):
        rows = [
            bench_row("synth_2x4", 340000, 350000, "11111111"),
            bench_row("wave_2x4", 300000, 310000, "22222222"),
        ]
        changed = {
            "device fingerprint": capture_lines(
                rows, device_id="ffeeddccbbaa998877665544"
            ),
            "QSPI digest": capture_lines(rows, qspi_sha256="b" * 64),
        }
        first = capture_lines(rows)
        for label, second in changed.items():
            with self.subTest(label=label):
                self.assertEqual(self.run_main([first, second]), 2)

    def test_evidence_persists_both_runs_qspi_digest_and_device_fingerprint(self):
        first = [
            bench_row("synth_2x4", 340000, 350000, "11111111"),
            bench_row("wave_2x4", 300000, 310000, "22222222"),
        ]
        second = [
            bench_row("synth_2x4", 340100, 350100, "11111111"),
            bench_row("wave_2x4", 300100, 310100, "22222222"),
        ]
        result, artifacts = self.run_evidence(
            [
                capture_lines(
                    first,
                    anchors=("ANCHOR,instrument_worst,92.40,97.60",),
                ),
                capture_lines(
                    second,
                    anchors=("ANCHOR,instrument_worst,92.50,97.70",),
                ),
            ]
        )

        self.assertEqual(result, 0)
        csv_text = artifacts[".csv"]
        md_text = artifacts[".md"]
        fingerprint = (
            "c6680d202ae781852bea690650975ae4c"
            "753749e56483677c494f96c8bf00abd"
        )
        self.assertEqual(csv_text.count("\nsystem,"), 0)
        self.assertEqual(csv_text.count("\n1,"), 2)
        self.assertEqual(csv_text.count("\n2,"), 2)
        self.assertIn("run,qspi_sha256,device_fingerprint", csv_text)
        self.assertIn(QSPI_SHA256, csv_text)
        self.assertIn(fingerprint, csv_text)
        self.assertNotIn(DEVICE_ID, csv_text)
        self.assertIn(",340000,350000,", csv_text)
        self.assertIn(",340100,350100,", csv_text)
        self.assertIn("## Run 1", md_text)
        self.assertIn("## Run 2", md_text)
        self.assertIn(QSPI_SHA256, md_text)
        self.assertIn(fingerprint, md_text)
        self.assertNotIn(DEVICE_ID, md_text)
        self.assertIn("| `instrument_worst` | 92.40 | 97.60 |", md_text)
        self.assertIn("| `instrument_worst` | 92.50 | 97.70 |", md_text)


if __name__ == "__main__":
    unittest.main()
