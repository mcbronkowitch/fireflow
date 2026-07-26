"""Host contracts for fail-closed repeated benchmark evidence."""

import csv
import importlib.util
import io
from pathlib import Path
import socket
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

from profiles import WAVE_ACCEPTANCE, resolve

QSPI_SHA256 = "a" * 64
DEVICE_ID = "00112233445566778899aabb"
# Registry order from bench/families.cpp -- every real capture below carries
# rows from all seven families, so the header declares all seven.
ALL_FAMILIES = "system voice mem mod abl taps sampler"
REAL_EVIDENCE_PATH = (
    RUNNER_PATH.parent.parent
    / "docs"
    / "bench"
    / "2026-07-25-d294556.csv"
)


class FakeOpenOcd:
    def __init__(self):
        self.stdout = io.StringIO("BENCH_END\n")
        self.gracefully_stopped = False
        self.terminated = False
        self.killed = False
        self.wait_calls = []

    def poll(self):
        if self.gracefully_stopped or self.terminated or self.killed:
            return 0
        return None

    def wait(self, timeout):
        self.wait_calls.append(timeout)
        if self.poll() is None:
            raise runner.subprocess.TimeoutExpired("openocd", timeout)
        return 0

    def terminate(self):
        self.terminated = True

    def kill(self):
        self.killed = True


class KillRequiredOpenOcd(FakeOpenOcd):
    def poll(self):
        return 0 if self.killed else None

    def terminate(self):
        self.terminated = True


class UnkillableOpenOcd(KillRequiredOpenOcd):
    def poll(self):
        return None


def family_row(family, name, avg_cyc, max_cyc, checksum):
    return (
        "BENCH,%s,%s,%d,%d,%.2f,%.2f,%s"
        % (
            family,
            name,
            avg_cyc,
            max_cyc,
            100.0 * avg_cyc / runner.BUDGET_CYCLES,
            100.0 * max_cyc / runner.BUDGET_CYCLES,
            checksum,
        )
    )


def bench_row(name, avg_cyc, max_cyc, checksum):
    return family_row("system", name, avg_cyc, max_cyc, checksum)


def capture_lines(
    rows, *, anchors=(), device_id=DEVICE_ID, qspi_sha256=QSPI_SHA256,
    families=ALL_FAMILIES
):
    return [
        "BENCH_BEGIN,%s,480000000,96,dcache+icache,%s,%s,%s"
        % (runner.current_head(), qspi_sha256, device_id, families),
        *rows,
        *anchors,
        "BENCH_END",
    ]


def real_evidence_rows(run_index):
    with REAL_EVIDENCE_PATH.open(newline="", encoding="utf-8") as stream:
        rows = [
            row for row in csv.DictReader(stream)
            if int(row["run"]) == run_index
        ]
    return [
        "BENCH,{family},{name},{avg_cyc},{max_cyc},{pct_avg},{pct_max},{checksum}"
        .format(**row)
        for row in rows
    ]


def replace_rows(rows, *replacements):
    replacements_by_name = {
        replacement.split(",")[2]: replacement
        for replacement in replacements
    }
    return [
        replacements_by_name.get(row.split(",")[2], row)
        for row in rows
    ]


class ParseContract(unittest.TestCase):
    def test_parse_reads_the_families_field(self):
        lines = [
            "BENCH_BEGIN,abc1234,480000000,96,dcache+icache,"
            + "0" * 64 + ",dead,system voice\n",
            "BENCH,system,empty_callback,2,12,0.00,0.00,ea306fb5\n",
            "BENCH_END\n",
        ]
        header, rows, _ = runner.parse(lines)
        self.assertEqual(header["families"], ("system", "voice"))

    def test_parse_rejects_a_header_without_families(self):
        """An old firmware image must not validate against the new host."""
        lines = [
            "BENCH_BEGIN,abc1234,480000000,96,dcache+icache,"
            + "0" * 64 + ",dead\n",
            "BENCH,system,empty_callback,2,12,0.00,0.00,ea306fb5\n",
            "BENCH_END\n",
        ]
        self.assertIsNone(runner.parse(lines))


class RunContract(unittest.TestCase):
    def test_run_once_gracefully_shuts_down_openocd_after_bench_end(self):
        process = FakeOpenOcd()
        sent = []

        class ControlSocket:
            def __enter__(self):
                return self

            def __exit__(self, exc_type, exc_value, traceback):
                return False

            def sendall(self, payload):
                sent.append(payload)
                process.gracefully_stopped = True

        with (
            mock.patch.object(runner.subprocess, "Popen", return_value=process),
            mock.patch.object(
                socket, "create_connection", return_value=ControlSocket()
            ),
        ):
            lines = runner.run_once("stlink-dap.cfg", 1)

        self.assertEqual(lines, ["BENCH_END"])
        self.assertEqual(sent, [b"shutdown\x1a"])
        self.assertEqual(process.wait_calls, [10])
        self.assertFalse(process.terminated)
        self.assertFalse(process.killed)

    def test_run_once_terminates_when_control_shutdown_fails(self):
        process = FakeOpenOcd()

        with (
            mock.patch.object(runner.subprocess, "Popen", return_value=process),
            mock.patch.object(
                socket,
                "create_connection",
                side_effect=OSError("control port unavailable"),
            ),
        ):
            lines = runner.run_once("stlink-dap.cfg", 1)

        self.assertEqual(lines, ["BENCH_END"])
        self.assertTrue(process.terminated)
        self.assertFalse(process.killed)
        self.assertEqual(process.wait_calls, [10])

    def test_run_once_waits_for_openocd_after_kill(self):
        process = KillRequiredOpenOcd()

        with (
            mock.patch.object(runner.subprocess, "Popen", return_value=process),
            mock.patch.object(
                socket,
                "create_connection",
                side_effect=OSError("control port unavailable"),
            ),
        ):
            lines = runner.run_once("stlink-dap.cfg", 1)

        self.assertEqual(lines, ["BENCH_END"])
        self.assertTrue(process.terminated)
        self.assertTrue(process.killed)
        self.assertEqual(process.wait_calls, [10, 10])

    def test_run_once_errors_if_openocd_survives_kill(self):
        process = UnkillableOpenOcd()

        with (
            mock.patch.object(runner.subprocess, "Popen", return_value=process),
            mock.patch.object(
                socket,
                "create_connection",
                side_effect=OSError("control port unavailable"),
            ),
            self.assertRaisesRegex(
                RuntimeError, "OpenOCD did not exit after kill"
            ),
        ):
            runner.run_once("stlink-dap.cfg", 1)

        self.assertTrue(process.terminated)
        self.assertTrue(process.killed)
        self.assertEqual(process.wait_calls, [10, 10])

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
        complete = real_evidence_rows(1)
        missing_empty = [
            row for row in complete if row.split(",")[2] != "empty_callback"
        ]

        self.assertEqual(
            self.run_main(
                [capture_lines(complete), capture_lines(missing_empty)]
            ),
            2,
        )

    def test_repeat_rejects_duplicate_row_names(self):
        complete = real_evidence_rows(1)
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
        first = real_evidence_rows(1)
        drifted = replace_rows(
            real_evidence_rows(2),
            bench_row("synth_2x4", 340100, 350100, "11111111"),
            bench_row("wave_2x4", 300100, 310100, "33333333"),
        )

        self.assertEqual(
            self.run_main([capture_lines(first), capture_lines(drifted)]),
            2,
        )

    def test_repeat_requires_the_matched_wave_acceptance_rows(self):
        missing_wave = [
            row for row in real_evidence_rows(1)
            if row.split(",")[2] != "wave_2x4"
        ]

        self.assertEqual(
            self.run_main(
                [capture_lines(missing_wave), capture_lines(missing_wave)]
            ),
            2,
        )

    def test_repeat_rejects_two_identically_truncated_runs(self):
        truncated = [
            bench_row("synth_2x4", 340000, 350000, "11111111"),
            bench_row("wave_2x4", 300000, 310000, "22222222"),
        ]

        self.assertEqual(
            self.run_main(
                [capture_lines(truncated), capture_lines(truncated)]
            ),
            2,
        )

    def test_repeat_rejects_each_wave_acceptance_violation(self):
        cases = {
            "average exceeds synth": (
                bench_row("synth_2x4", 340000, 360000, "11111111"),
                bench_row("wave_2x4", 350000, 350000, "22222222"),
            ),
            "maximum exceeds synth": (
                bench_row("synth_2x4", 340000, 360000, "11111111"),
                bench_row("wave_2x4", 300000, 370000, "22222222"),
            ),
            "maximum reaches block budget": (
                bench_row("synth_2x4", 950000, 970000, "11111111"),
                bench_row("wave_2x4", 900000, runner.BUDGET_CYCLES, "22222222"),
            ),
            "non-numeric WAVE result": (
                bench_row("synth_2x4", 340000, 360000, "11111111"),
                "BENCH,system,wave_2x4,TIMEOUT,TIMEOUT,TIMEOUT,TIMEOUT,22222222",
            ),
        }
        valid = replace_rows(
            real_evidence_rows(1),
            bench_row("synth_2x4", 340000, 360000, "11111111"),
            bench_row("wave_2x4", 300000, 350000, "22222222"),
        )
        for label, wave_rows in cases.items():
            rows = replace_rows(
                real_evidence_rows(1),
                *wave_rows,
            )
            for failing_run in (1, 2):
                captures = [capture_lines(valid), capture_lines(valid)]
                captures[failing_run - 1] = capture_lines(rows)
                with self.subTest(label=label, failing_run=failing_run):
                    self.assertEqual(self.run_main(captures), 2)

    def test_wave_acceptance_allows_exact_comparison_boundaries(self):
        rows = replace_rows(
            real_evidence_rows(1),
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
        )

        self.assertEqual(
            self.run_main([capture_lines(rows), capture_lines(rows)]),
            0,
        )

    def test_evidence_requires_at_least_two_runs(self):
        rows = real_evidence_rows(1)

        self.assertEqual(self.run_main([capture_lines(rows)]), 2)

    def test_repeat_rejects_unstable_device_or_qspi_identity(self):
        rows = real_evidence_rows(1)
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

    def test_existing_real_68_row_evidence_is_accepted_without_row_order(self):
        first = real_evidence_rows(1)
        second = list(reversed(real_evidence_rows(2)))

        self.assertEqual(len(first), 68)
        self.assertEqual(len(second), 68)
        self.assertEqual(
            self.run_main([capture_lines(first), capture_lines(second)]),
            0,
        )

    def test_evidence_persists_both_runs_qspi_digest_and_device_fingerprint(self):
        first = replace_rows(
            real_evidence_rows(1),
            bench_row("synth_2x4", 340000, 350000, "11111111"),
            bench_row("wave_2x4", 300000, 310000, "22222222"),
        )
        second = replace_rows(
            real_evidence_rows(2),
            bench_row("synth_2x4", 340100, 350100, "11111111"),
            bench_row("wave_2x4", 300100, 310100, "22222222"),
        )
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
        self.assertEqual(csv_text.count("\n1,"), 68)
        self.assertEqual(csv_text.count("\n2,"), 68)
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
        self.assertFalse(md_text.endswith("\n\n"))

    def test_evidence_persists_explicit_wave_gate_pass_verdict(self):
        first = replace_rows(
            real_evidence_rows(1),
            bench_row("synth_2x4", 340000, 350000, "11111111"),
            bench_row("wave_2x4", 300000, 310000, "22222222"),
        )
        second = replace_rows(
            real_evidence_rows(2),
            bench_row("synth_2x4", 340100, 350100, "11111111"),
            bench_row("wave_2x4", 300100, 310100, "22222222"),
        )

        result, artifacts = self.run_evidence(
            [capture_lines(first), capture_lines(second)]
        )

        self.assertEqual(result, 0)
        md_text = artifacts[".md"]
        self.assertIn("## WAVE performance gate — PASS", md_text)
        self.assertIn(
            "**Run 1 — PASS:** `wave_2x4` average 300000 <= "
            "`synth_2x4` average 340000; maximum 310000 <= 350000; "
            "maximum 310000 < 960000.",
            md_text,
        )
        self.assertIn(
            "**Run 2 — PASS:** `wave_2x4` average 300100 <= "
            "`synth_2x4` average 340100; maximum 310100 <= 350100; "
            "maximum 310100 < 960000.",
            md_text,
        )


class ProfileContract(unittest.TestCase):
    def system_rows(self, wave_avg=200):
        """A complete row set for the system-only profile. wave_2x4 defaults
        to well under synth_2x4 (400) so the WAVE gate passes; callers that
        want it to fail the gate pass a larger wave_avg."""
        names = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["system"]
        rows = []
        for i, name in enumerate(names):
            if name == "synth_2x4":
                avg = 400
            elif name == "wave_2x4":
                avg = wave_avg
            else:
                avg = 100
            rows.append(bench_row(name, avg, avg + 1, "%08x" % i))
        return rows

    def test_system_profile_validates_against_its_filtered_rowset(self):
        """A system-only capture is complete for the system profile."""
        capture = runner.parse(
            capture_lines(self.system_rows(), families="system")
        )

        runner.validate_captures([capture, capture], resolve("system"))

    def test_reported_families_must_match_the_requested_profile(self):
        """A stale image built for a different profile is rejected."""
        capture = runner.parse(
            # firmware says more than asked
            capture_lines(self.system_rows(), families="system voice")
        )

        with self.assertRaisesRegex(runner.BenchValidationError, "families"):
            runner.validate_captures([capture, capture], resolve("system"))

    def test_unknown_profile_name_is_rejected(self):
        with self.assertRaises(KeyError):
            resolve("nonsense")

    def test_a_profile_without_wave_acceptance_does_not_run_it(self):
        """The gate must be genuinely skipped, not accidentally satisfied.

        Neither shipped profile omits wave_acceptance, so this uses a
        synthetic one. Without it, nothing proves the gate is actually
        conditional -- an `if WAVE_ACCEPTANCE in profile.gates` that was
        never false would pass every test in this file.
        """
        from profiles import Profile

        # wave_2x4 deliberately SLOWER than synth_2x4: this capture would
        # fail wave_acceptance outright. A profile that does not declare the
        # gate must accept it anyway.
        capture = runner.parse(
            capture_lines(self.system_rows(wave_avg=900), families="system")
        )

        ungated = Profile(families=("system",), gates=frozenset())
        runner.validate_captures([capture, capture], ungated)  # accepted

        gated = Profile(
            families=("system",), gates=frozenset({WAVE_ACCEPTANCE})
        )
        with self.assertRaises(runner.BenchValidationError):
            runner.validate_captures([capture, capture], gated)  # rejected


class ProfileAwareEvidenceContract(unittest.TestCase):
    """write_results (and the WAVE verdict it writes) must follow the
    profile's gates, not assume `system` / WAVE_ACCEPTANCE are always
    present. Neither shipped profile currently omits either, so both tests
    use a synthetic profile -- the BODY branch adds a real one that does."""

    def run_with_profile(self, captures, profile):
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
                mock.patch.object(runner, "resolve", return_value=profile),
                mock.patch.object(sys, "argv", argv),
            ):
                result = runner.main()
            artifacts = {
                path.suffix: path.read_text(encoding="utf-8")
                for path in Path(temp).iterdir()
            }
            return result, artifacts

    def test_a_profile_without_system_writes_a_complete_document(self):
        """A family-only profile with no `system` rows must not KeyError
        inside write_results after a complete two-run hardware capture --
        that is the worst possible moment to lose the evidence."""
        from profiles import Profile

        names = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["voice"]
        rows = [
            family_row("voice", name, 100, 101, "%08x" % i)
            for i, name in enumerate(names)
        ]
        capture = capture_lines(rows, families="voice")
        profile = Profile(families=("voice",), gates=frozenset())

        result, artifacts = self.run_with_profile([capture, capture], profile)

        self.assertEqual(result, 0)
        self.assertIn(".md", artifacts)
        self.assertNotIn("WAVE performance gate", artifacts[".md"])

    def test_an_ungated_profile_does_not_claim_the_wave_gate_passed(self):
        """A profile that carries `system` but does not declare
        WAVE_ACCEPTANCE must not print a PASS claim for a capture that
        would fail the gate outright -- the document's only purpose is to
        be evidence, and a false PASS defeats that."""
        from profiles import Profile

        names = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["system"]
        rows = []
        for i, name in enumerate(names):
            if name == "synth_2x4":
                avg = 100
            elif name == "wave_2x4":
                avg = 900  # deliberately slower: would fail the gate outright
            else:
                avg = 100
            rows.append(bench_row(name, avg, avg + 1, "%08x" % i))
        capture = capture_lines(rows, families="system")
        profile = Profile(families=("system",), gates=frozenset())

        result, artifacts = self.run_with_profile([capture, capture], profile)

        self.assertEqual(result, 0)
        self.assertNotIn("WAVE performance gate", artifacts[".md"])
        self.assertNotIn("PASS", artifacts[".md"])


if __name__ == "__main__":
    unittest.main()
