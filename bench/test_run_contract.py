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

from profiles import Profile, WAVE_ACCEPTANCE, resolve

QSPI_SHA256 = "a" * 64
DEVICE_ID = "00112233445566778899aabb"
# Registry order from bench/families.cpp -- every real capture below carries
# rows from all seven families that existed when it was measured, so the
# header declares those seven. The `body` family arrived later (M5j); a
# capture from before it cannot and should not name it. `taps` is the other
# direction: Task 6 (e004a3d) retired the whole taps bench family going
# forward and dropped it from this list to match -- but that capture was
# measured before Task 6 too, so it genuinely reported `taps` rows on the
# wire. Dropping the name here doesn't un-measure them; it only made this
# fixture disagree with its own header. Kept at seven.
ALL_FAMILIES = "system voice mem mod abl taps sampler"

# The profile those captures ARE: `full` as it stood before the `body` family
# joined it. Today's `full` names eight families, so a pre-body capture is no
# longer a complete run of it -- correctly so. Validating this real evidence
# against the profile of its own era keeps it doing the job it is here for
# (proving the universal gates accept a genuine, complete capture) instead of
# turning it into a stale-manifest test it was never meant to be.
PRE_BODY_FULL = Profile(
    families=tuple(ALL_FAMILIES.split()),
    gates=frozenset({WAVE_ACCEPTANCE}),
)

# The row protocol those captures ARE, mirroring PRE_BODY_FULL immediately
# above: run.py's BENCH_PROTOCOL_ROWS_BY_FAMILY is today's row protocol, not
# 2026-07-25's. Three things moved since this capture was measured -- `system`
# now expects `instrument_worst_bbd` where this capture reported
# `instrument_worst_taps` (M5j/bbd hadn't landed yet); `abl` no longer carries
# `echo_short_sdram`/`echo_short_sram` (they left with the tap bank); and the
# whole `taps` family is gone from today's registry (Task 6, e004a3d) even
# though this capture is exactly the evidence that it once existed. Pointing
# validation at today's table would reject a real, complete, correctly-shaped
# capture for not being a capture of something that did not exist yet -- the
# same failure mode PRE_BODY_FULL exists to prevent, one level down at the row
# instead of the family. Pinned to what the instrument actually reported that
# day; every other family below is unchanged from today's table because
# nothing else about it has moved.
PRE_BODY_ROWS_BY_FAMILY = {
    "system": (
        "empty_callback",
        "mod_plane_2x_center",
        "synth_1_voice",
        "synth_2_voices",
        "synth_4_voices",
        "synth_2x4",
        "wave_2x4",
        "fx_none",
        "fx_grit",
        "fx_flux_sdram",
        "fx_comp",
        "oliverb_solo_sram",
        "instrument_init",
        "instrument_worst",
        "instrument_worst_taps",
    ),
    "voice": (
        "morph_osc_bare",
        "modal_voice",
        "string_voice",
        "resonator",
        "formant_osc",
        "vosim_osc",
        "harmonic_osc",
        "grainlet_osc",
        "z_osc",
        "variable_shape_osc",
    ),
    "mem": (
        "grain_read_sram",
        "grain_read_sdram",
        "oliverb_sdram",
        "echo_walk_sram",
        "echo_walk_sdram",
    ),
    "mod": (
        "lane_flow_shape00",
        "lane_flow_shape03",
        "lane_flow_shape07",
        "lane_flow_shape10",
        "lane_step_shape00",
        "super_mod_5lanes",
        "center_tick",
    ),
    "abl": (
        "micro_sinf",
        "micro_tanhf",
        "micro_powf",
        "micro_fast_sin",
        "part_glue_flow",
        "inst_worst_noflux",
        "inst_worst_noreverb",
        "inst_worst_nogrit",
        "inst_worst_choked",
        "limiter_clean",
        "limiter_driven",
        "grit_drive_solo",
        "grit_reduce_solo",
        "echo_short_sdram",
        "echo_short_sram",
    ),
    "taps": (
        "tap_read_sdram",
        "taps_2_opt",
    ),
    "sampler": (
        "sampler_flow_typ",
        "sampler_flow_worst",
        "sampler_overdub_worst",
        "sampler_scan_ctrl",
        "sampler_win_sram",
        "sampler_win_sdram",
        "inst_sampler_worst",
        "sampler_worst_slowspawn",
        "inst_sampler_nomotion",
        "inst_sampler_slowspawn",
        "inst_sampler_noflux",
        "inst_sampler_noreverb",
        "inst_sampler_onepart",
        "sampler_worst_nomotion",
    ),
}
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


def gate_ledger_section(md):
    """Isolate the '## Gate ledger' section of a written evidence Markdown
    document, so a test can assert against gate-ledger content specifically
    rather than the whole document -- which also holds a verdict and
    per-run tables that could accidentally contain a matching substring."""
    start = md.index("## Gate ledger")
    end = md.index("\n## ", start + len("## Gate ledger"))
    return md[start:end]


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
                # See PRE_BODY_FULL: these captures are real pre-body evidence,
                # so they are validated against the profile they actually are.
                mock.patch.object(runner, "resolve", return_value=PRE_BODY_FULL),
                # See PRE_BODY_ROWS_BY_FAMILY: validate_captures's row check
                # (protocol_rowset) reads this table directly, not through
                # resolve(), so it needs its own pin alongside the profile's.
                mock.patch.object(
                    runner,
                    "BENCH_PROTOCOL_ROWS_BY_FAMILY",
                    PRE_BODY_ROWS_BY_FAMILY,
                ),
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
        self.assertIn("run,profile,qspi_sha256,device_fingerprint", csv_text)
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


def resolve_profile(name):
    """resolve() validates a profile's manifest against the row protocol
    (bench/profiles.py's resolve() docstring), so it needs run.py's
    BENCH_PROTOCOL_ROWS_BY_FAMILY passed in -- this helper is the one place
    in this file that wires the two together, so every call site below
    doesn't have to repeat it."""
    return resolve(name, runner.BENCH_PROTOCOL_ROWS_BY_FAMILY)


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

        runner.validate_captures([capture, capture], resolve_profile("system"))

    def test_reported_families_must_match_the_requested_profile(self):
        """A stale image built for a different profile is rejected."""
        capture = runner.parse(
            # firmware says more than asked
            capture_lines(self.system_rows(), families="system voice")
        )

        with self.assertRaisesRegex(runner.BenchValidationError, "families"):
            runner.validate_captures([capture, capture], resolve_profile("system"))

    def test_unknown_profile_name_is_rejected(self):
        with self.assertRaises(KeyError):
            resolve_profile("nonsense")

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

    def test_candidate_section_says_why_it_is_empty_under_a_partial_profile(self):
        """The verdict's candidate list is fed by the `voice` family. Under a
        profile that omits it, the heading must not stand over an empty body:
        a reader cannot tell that apart from 'no candidate cost anything worth
        reporting'. The neighbouring SRAM-vs-SDRAM prose already says
        'n/a (row missing)' for the same reason."""
        capture = runner.parse(
            capture_lines(self.system_rows(), families="system")
        )

        with tempfile.TemporaryDirectory() as temp:
            base = runner.write_results(
                temp, [capture, capture], resolve_profile("system"), "system"
            )
            with open(base + ".md", encoding="utf-8") as fh:
                md = fh.read()

        self.assertIn("n/a (family `voice` not in this profile)", md)

    def test_candidate_section_lists_the_rows_when_the_profile_carries_them(self):
        """The empty-case marker must not displace the real listing -- a fix
        that always printed 'n/a' would satisfy the test above."""
        from profiles import Profile

        rows = self.system_rows()
        for i, name in enumerate(runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["voice"]):
            rows.append(family_row("voice", name, 100, 101, "1%07x" % i))
        capture = runner.parse(
            capture_lines(rows, families="system voice")
        )
        both = Profile(
            families=("system", "voice"), gates=frozenset({WAVE_ACCEPTANCE})
        )

        with tempfile.TemporaryDirectory() as temp:
            base = runner.write_results(temp, [capture, capture], both, "sysvoice")
            with open(base + ".md", encoding="utf-8") as fh:
                md = fh.read()

        self.assertNotIn("n/a (family `voice` not in this profile)", md)
        self.assertIn("one real voice", md)

    def test_written_evidence_names_the_profile_and_ledgers_the_gates(self):
        capture = runner.parse(
            capture_lines(self.system_rows(), families="system")
        )

        with tempfile.TemporaryDirectory() as temp:
            base = runner.write_results(
                temp, [capture, capture], resolve_profile("system"), "system"
            )

            self.assertTrue(base.endswith("-system"))
            with open(base + ".md", encoding="utf-8") as fh:
                md = fh.read()
            self.assertIn("system", md)
            self.assertIn("wave_acceptance", md)
            # Every universal gate is recorded as having run.
            self.assertIn("row set", md.lower())
            with open(base + ".csv", encoding="utf-8") as fh:
                csv_text = fh.read()
            self.assertIn("profile", csv_text.splitlines()[0])
            self.assertIn(",system,", csv_text)

    def test_gate_ledger_marks_wave_acceptance_applied_when_the_profile_declares_it(self):
        """The 'system' profile declares wave_acceptance, so the ledger
        must record it under 'Applied and passed' -- not silently drop it,
        and not misfile it under 'Not applicable' either."""
        capture = runner.parse(
            capture_lines(self.system_rows(), families="system")
        )

        with tempfile.TemporaryDirectory() as temp:
            base = runner.write_results(
                temp, [capture, capture], resolve_profile("system"), "system"
            )
            with open(base + ".md", encoding="utf-8") as fh:
                md = fh.read()

        ledger = gate_ledger_section(md)
        applied, not_applicable = ledger.split(
            "Not applicable to this profile:"
        )
        self.assertIn("`wave_acceptance`", applied)
        self.assertNotIn("wave_acceptance", not_applicable)
        self.assertIn("none", not_applicable)

    def test_gate_ledger_marks_wave_acceptance_not_applicable_with_reason_when_the_profile_omits_it(self):
        """A profile that does not declare wave_acceptance must not have
        the ledger claim the gate ran -- that would convert a real gap
        into a false assurance. wave_2x4 is deliberately made SLOWER than
        synth_2x4 here (a capture that would fail the gate outright), so a
        ledger bug that claims 'applied and passed' regardless of the
        profile can't hide behind a capture that happened to pass anyway.

        This profile carries `system`, so its families COULD supply the
        gate's rows -- it simply doesn't declare the gate. The reason must
        say that, not the (false, for this case) "does not contain" the
        rows -- that sentence belongs to a different profile shape, see
        test_gate_ledger_reason_distinguishes_missing_rows_from_undeclared_gate
        below.
        """
        from profiles import Profile

        capture = runner.parse(
            capture_lines(self.system_rows(wave_avg=900), families="system")
        )
        ungated = Profile(families=("system",), gates=frozenset())

        with tempfile.TemporaryDirectory() as temp:
            base = runner.write_results(
                temp, [capture, capture], ungated, "system"
            )
            with open(base + ".md", encoding="utf-8") as fh:
                md = fh.read()

        ledger = gate_ledger_section(md)
        applied, not_applicable = ledger.split(
            "Not applicable to this profile:"
        )
        self.assertNotIn("wave_acceptance", applied)
        self.assertIn("`wave_acceptance`", not_applicable)
        self.assertIn("does not declare", not_applicable)
        self.assertNotIn("does not supply", not_applicable)

    def test_gate_ledger_reason_distinguishes_missing_rows_from_undeclared_gate(self):
        """The two ways wave_acceptance can be 'not applicable' say
        different things, and the ledger must not use the same sentence
        for both: a profile whose families genuinely cannot supply
        synth_2x4/wave_2x4 (voice-only, below) is a different situation
        from a profile that carries `system` but simply never declared the
        gate (the previous test)."""
        from profiles import Profile

        voice_names = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["voice"]
        voice_rows = [
            family_row("voice", name, 100, 101, "%08x" % i)
            for i, name in enumerate(voice_names)
        ]
        voice_capture = runner.parse(
            capture_lines(voice_rows, families="voice")
        )
        voice_only = Profile(families=("voice",), gates=frozenset())

        with tempfile.TemporaryDirectory() as temp:
            base = runner.write_results(
                temp, [voice_capture, voice_capture], voice_only, "voice"
            )
            with open(base + ".md", encoding="utf-8") as fh:
                md = fh.read()

        not_applicable = gate_ledger_section(md).split(
            "Not applicable to this profile:"
        )[1]
        self.assertIn("do not supply", not_applicable)
        self.assertNotIn("does not declare", not_applicable)


class SweepProfileTest(unittest.TestCase):
    def test_sweep_profile_resolves_and_carries_system(self):
        profile = resolve("sweep", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY)
        self.assertEqual(profile.families, ("system", "sweep"))
        self.assertIn(WAVE_ACCEPTANCE, profile.gates)

    def test_sweep_family_has_row_expectations(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        self.assertNotIn("sweep_probe", rows, "scaffolding row must not reach hardware run")
        self.assertEqual(len(rows), len(set(rows)), "duplicate row names")
        self.assertEqual(len(rows), 15, "sweep family must have exactly 15 rows")

    def test_flux_rate_sweep_rows_are_expected(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        for index in (0, 3, 6, 8, 11):
            self.assertIn("sweep_flux_rate_%d" % index, rows)

    def test_stages_sweep_rows_are_expected(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        for stages in (512, 2048, 8192, 16384):
            self.assertIn("sweep_stages_%d" % stages, rows)

    def test_grit_ablation_rows_are_expected(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        self.assertIn("sweep_grit_bare", rows)
        self.assertIn("sweep_grit_no_bbd_mem", rows)

    def test_flux_wrapper_ablation_row_is_expected(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        self.assertIn("sweep_flux_lines_2ch", rows)

    def test_room_sweep_rows_are_expected(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        for name in ("sweep_room_lo", "sweep_room_mid", "sweep_room_hi"):
            self.assertIn(name, rows)


class AblateProfileTest(unittest.TestCase):
    def test_ablate_profile_resolves_and_carries_system(self):
        profile = resolve("ablate", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY)
        self.assertIn("system", profile.families)
        self.assertIn("instr", profile.families)

    def test_instr_family_has_row_expectations(self):
        self.assertIn("instr", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY)
        self.assertTrue(runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"])

    def test_noverb_row_is_expected(self):
        self.assertIn("instr_noverb", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"])

    def test_part_rows_are_expected(self):
        self.assertIn("instr_part_1", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"])
        self.assertIn("instr_part_2", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"])

    def test_deck_mod_row_is_expected(self):
        self.assertIn("deck_mod_hot", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"])


class ManifestValidationContract(unittest.TestCase):
    """resolve()'s load-time manifest checks (design spec S3): a profile
    naming a family run.py has no row expectations for, or declaring
    wave_acceptance without families that supply its rows, must be caught
    before anything is built or flashed -- not after two hardware repeats.
    """

    def test_resolve_rejects_a_profile_naming_an_unknown_family(self):
        from profiles import Profile
        import profiles as profiles_module

        with mock.patch.dict(
            profiles_module.PROFILES,
            {"bogus": Profile(families=("nope",), gates=frozenset())},
        ):
            with self.assertRaisesRegex(ValueError, "nope"):
                resolve_profile("bogus")

    def test_resolve_rejects_wave_acceptance_without_a_supplying_family(self):
        from profiles import Profile, WAVE_ACCEPTANCE
        import profiles as profiles_module

        with mock.patch.dict(
            profiles_module.PROFILES,
            {
                "bogus": Profile(
                    families=("voice",), gates=frozenset({WAVE_ACCEPTANCE})
                )
            },
        ):
            with self.assertRaisesRegex(ValueError, "wave_acceptance"):
                resolve_profile("bogus")

    def test_resolve_accepts_a_profile_whose_families_supply_wave_acceptance(self):
        from profiles import Profile, WAVE_ACCEPTANCE
        import profiles as profiles_module

        with mock.patch.dict(
            profiles_module.PROFILES,
            {
                "bogus": Profile(
                    families=("system", "voice"),
                    gates=frozenset({WAVE_ACCEPTANCE}),
                )
            },
        ):
            resolve_profile("bogus")  # must not raise

    def test_both_shipped_profiles_pass_manifest_validation(self):
        for name in ("system", "full"):
            with self.subTest(profile=name):
                resolve_profile(name)  # must not raise


class FullProfileLinkContract(unittest.TestCase):
    """`full` is expected to fail to link (SRAM_EXEC/SRAM region overflow --
    design spec S1, S5). A full arm-none-eabi cross-compile inside the unit
    suite would be slow and would make this suite depend on the toolchain
    being on PATH; these tests instead pin the two properties that let a
    link failure be trusted as "the known size problem" rather than a
    manifest bug wearing its symptoms: `full`'s manifest resolves cleanly
    (no unknown family, no ungated wave_acceptance -- see
    ManifestValidationContract above), and its families are exactly the
    ones the Makefile and BENCH_PROTOCOL_ROWS_BY_FAMILY both know about --
    not fewer (which would silently shrink what "full" means and might
    happen to fit) and not more (an extra family is a real, deliberate
    size change, not a stale manifest, and belongs in this set).

    What this does NOT prove: that the link actually fails, or that it
    fails at the *link* step specifically rather than earlier (a
    genuinely broken manifest could still fail at compile or at the
    Makefile's own family guard). That property is established by hand
    per the spec, and by any CI/hardware run that attempts to build
    `full`; it is not re-proven by this host-only suite.
    """

    KNOWN_FAMILIES = frozenset(
        {"system", "voice", "mem", "mod", "abl", "bbd", "body", "sampler"}
    )

    # `sweep` (spec 2026-07-29-fx-cost-curves) has a Makefile entry and a
    # BENCH_PROTOCOL_ROWS_BY_FAMILY entry like every other family, but is
    # deliberately absent from `full` -- bench/Makefile's BENCH_FAMILIES
    # comment says that default line IS `full`'s family list, and `sweep`
    # is intentionally not on it (its own `sweep` profile carries it
    # instead). Naming it here, rather than folding it into KNOWN_FAMILIES,
    # keeps the guard below able to still catch an *accidental* new family
    # that drifts between the Makefile and the row protocol.
    #
    # `instr` (spec 2026-07-29-instrument-ablation) is the same story: a
    # Makefile entry and a BENCH_PROTOCOL_ROWS_BY_FAMILY entry, deliberately
    # left off the `BENCH_FAMILIES ?=` default list, carried instead by its
    # own `ablate` profile.
    NOT_IN_FULL = frozenset({"sweep", "instr"})

    def test_full_profile_manifest_resolves_cleanly(self):
        resolve_profile("full")  # must not raise a manifest error

    def test_full_profile_names_exactly_the_known_families(self):
        profile = resolve_profile("full")

        self.assertEqual(set(profile.families), self.KNOWN_FAMILIES)
        self.assertEqual(
            set(runner.BENCH_PROTOCOL_ROWS_BY_FAMILY),
            self.KNOWN_FAMILIES | self.NOT_IN_FULL,
        )

    def test_full_profile_families_match_the_makefiles_known_families(self):
        """Guards against the Makefile and profiles.py drifting apart --
        e.g. a family renamed in one but not the other, which would leave
        `full` naming a family the Makefile no longer recognises (or vice
        versa), and a link failure could then be a manifest bug in
        disguise rather than the known size problem. `NOT_IN_FULL` families
        are excluded from this comparison deliberately, not silently --
        see the comment on that set."""
        import re

        makefile = (Path(__file__).with_name("Makefile")).read_text(
            encoding="utf-8"
        )
        known_to_makefile = set(
            re.findall(r"^FAMILY_SOURCE_(\w+)\s*=", makefile, re.MULTILINE)
        ) - self.NOT_IN_FULL
        profile = resolve_profile("full")

        self.assertEqual(set(profile.families), known_to_makefile)


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
