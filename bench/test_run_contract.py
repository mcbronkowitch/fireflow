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


ANCHOR_NAMES = (
    "oliverb_solo_sram",
    "tap_read_sdram",
    "instrument_worst_bbd_dtcm",
    "instrument_worst_bbd",
    "instrument_worst",
)


def anchor_lines(rows, overrides=None):
    overrides = overrides or {}
    row_names = {row.split(",")[2] for row in rows}
    return tuple(
        "ANCHOR,%s,%s,%s"
        % (
            name,
            overrides.get(name, ("10.00", "11.00"))[0],
            overrides.get(name, ("10.00", "11.00"))[1],
        )
        for name in ANCHOR_NAMES
        if name in row_names
    )


def capture_lines(
    rows, *, anchors=None, device_id=DEVICE_ID, qspi_sha256=QSPI_SHA256,
    families=ALL_FAMILIES, layout="axi", optimization="o2"
):
    if anchors is None:
        anchors = anchor_lines(rows)
    return [
        "BENCH_BEGIN,%s,480000000,96,dcache+icache,%s,%s,%s,%s,%s"
        % (
            runner.current_head(),
            qspi_sha256,
            device_id,
            families,
            layout,
            optimization,
        ),
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
    def test_parse_reads_the_families_and_layout_fields(self):
        lines = [
            "BENCH_BEGIN,abc1234,480000000,96,dcache+icache,"
            + "0" * 64 + ",dead,system voice,itcm-hot,o2\n",
            "BENCH,system,empty_callback,2,12,0.00,0.00,ea306fb5\n",
            "BENCH_END\n",
        ]
        header, rows, _ = runner.parse(lines)
        self.assertEqual(header["families"], ("system", "voice"))
        self.assertEqual(header["layout"], "itcm-hot")

    def test_parse_rejects_a_header_without_layout(self):
        """An old firmware image must not validate against the new host."""
        lines = [
            "BENCH_BEGIN,abc1234,480000000,96,dcache+icache,"
            + "0" * 64 + ",dead,system voice\n",
            "BENCH,system,empty_callback,2,12,0.00,0.00,ea306fb5\n",
            "BENCH_END\n",
        ]
        self.assertIsNone(runner.parse(lines))

    def test_parse_reads_layout_and_optimization_fields(self):
        lines = [
            "BENCH_BEGIN,abc1234,480000000,96,dcache+icache,"
            + "0" * 64
            + ",dead,system voice,itcm-hot,o3\n",
            "BENCH,system,empty_callback,2,12,0.00,0.00,ea306fb5\n",
            "BENCH_END\n",
        ]
        header, _rows, _anchors = runner.parse(lines)
        self.assertEqual(header["layout"], "itcm-hot")
        self.assertEqual(header["optimization"], "o3")

    def test_parse_rejects_a_header_without_optimization(self):
        lines = [
            "BENCH_BEGIN,abc1234,480000000,96,dcache+icache,"
            + "0" * 64
            + ",dead,system voice,itcm-hot\n",
            "BENCH,system,empty_callback,2,12,0.00,0.00,ea306fb5\n",
            "BENCH_END\n",
        ]
        self.assertIsNone(runner.parse(lines))

    def test_parse_rejects_a_structurally_malformed_anchor(self):
        lines = [
            "BENCH_BEGIN,abc1234,480000000,96,dcache+icache,"
            + "0" * 64
            + ",dead,system,itcm-hot,o3\n",
            "BENCH,system,empty_callback,2,12,0.00,0.00,ea306fb5\n",
            "ANCHOR,instrument_worst_bbd_dtcm,95.63\n",
            "BENCH_END\n",
        ]
        try:
            parsed = runner.parse(lines)
        except Exception as error:
            self.fail("malformed capture escaped parse validation: %s" % error)
        self.assertIsNone(parsed)


class ItcmLayoutContract(unittest.TestCase):
    def test_build_requests_the_itcm_make_mode(self):
        with (
            mock.patch.object(runner.subprocess, "run") as run,
            mock.patch.object(
                runner, "prepare_existing_artifacts", return_value={}
            ),
        ):
            runner.build(("system",), itcm_hot=True)

        self.assertIn(
            "BENCH_ITCM_HOT=1",
            run.call_args_list[1].args[0],
        )

    def test_build_requests_the_optimization_make_mode(self):
        with (
            mock.patch.object(runner.subprocess, "run") as run,
            mock.patch.object(
                runner, "prepare_existing_artifacts", return_value={}
            ),
        ):
            runner.build(("system",), itcm_hot=True, optimization="o3")
        self.assertIn(
            "BENCH_OPTIMIZATION=o3",
            run.call_args_list[1].args[0],
        )

    def test_itcm_preflight_precedes_qspi_programming(self):
        argv = [
            "run.py",
            "--profile",
            "system",
            "--no-build",
            "--itcm-hot",
            "--program-qspi",
            "--build-only",
        ]
        preflight_error = getattr(
            runner,
            "ItcmPlacementError",
            runner.QspiGuardError,
        )
        with (
            mock.patch.object(
                runner,
                "prepare_existing_artifacts",
                return_value={},
            ),
            mock.patch.object(runner, "require_clean_tree"),
            mock.patch.object(
                runner,
                "validate_itcm_placement",
                side_effect=preflight_error("invalid ITCM placement"),
                create=True,
            ) as preflight,
            mock.patch.object(runner, "program_and_verify") as program,
            mock.patch.object(sys, "argv", argv),
        ):
            result = runner.main()

        self.assertEqual(result, 2)
        preflight.assert_called_once()
        program.assert_not_called()

    def test_repeat_rejects_mixed_layouts(self):
        profile = resolve("system", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY)
        names = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["system"]
        rows = []
        for index, name in enumerate(names):
            checksum = (
                "aabbccdd"
                if name in {
                    "instrument_worst_bbd",
                    "instrument_worst_bbd_dtcm",
                }
                else "%08x" % index
            )
            rows.append(bench_row(name, 100, 101, checksum))
        axi = runner.parse(
            capture_lines(rows, families="system", layout="axi")
        )
        itcm = runner.parse(
            capture_lines(rows, families="system", layout="itcm-hot")
        )

        with self.assertRaisesRegex(
            runner.BenchValidationError,
            "layout differs",
        ):
            runner.validate_captures([axi, itcm], profile)

    def test_requested_itcm_rejects_an_axi_capture(self):
        profile = resolve("system", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY)
        names = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["system"]
        rows = []
        for index, name in enumerate(names):
            checksum = (
                "aabbccdd"
                if name in {
                    "instrument_worst_bbd",
                    "instrument_worst_bbd_dtcm",
                }
                else "%08x" % index
            )
            rows.append(bench_row(name, 100, 101, checksum))
        capture = runner.parse(
            capture_lines(rows, families="system", layout="axi")
        )

        with self.assertRaisesRegex(
            runner.BenchValidationError,
            "requested layout itcm-hot",
        ):
            runner.validate_captures(
                [capture, capture],
                profile,
                expected_layout="itcm-hot",
            )


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
                    anchors=anchor_lines(
                        first,
                        {"instrument_worst": ("92.40", "97.60")},
                    ),
                ),
                capture_lines(
                    second,
                    anchors=anchor_lines(
                        second,
                        {"instrument_worst": ("92.50", "97.70")},
                    ),
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
        self.assertIn(
            "run,profile,board,layout,optimization,qspi_sha256,device_fingerprint",
            csv_text,
        )
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
            if name in {
                "instrument_worst_bbd",
                "instrument_worst_bbd_dtcm",
            }:
                checksum = "aabbccdd"
            else:
                checksum = "%08x" % i
            rows.append(bench_row(name, avg, avg + 1, checksum))
        return rows

    def gate_capture(
        self,
        *,
        avg_cyc,
        max_cyc,
        pct_avg,
        pct_max,
        callback_avg,
        callback_max,
    ):
        rows = replace_rows(
            self.system_rows(),
            "BENCH,system,instrument_worst_bbd_dtcm,"
            "%d,%d,%s,%s,aabbccdd"
            % (avg_cyc, max_cyc, pct_avg, pct_max),
        )
        anchors = anchor_lines(
            rows,
            {
                "instrument_worst_bbd_dtcm": (
                    callback_avg,
                    callback_max,
                )
            },
        )
        return runner.parse(
            capture_lines(rows, anchors=anchors, families="system")
        )

    def write_system_report(self, captures):
        with tempfile.TemporaryDirectory() as temp:
            base = runner.write_results(
                temp,
                captures,
                resolve_profile("system"),
                "system",
            )
            return Path(base + ".md").read_text(encoding="utf-8")

    def test_system_profile_validates_against_its_filtered_rowset(self):
        """A system-only capture is complete for the system profile."""
        capture = runner.parse(
            capture_lines(self.system_rows(), families="system")
        )

        runner.validate_captures([capture, capture], resolve_profile("system"))

    def regress_rows(self):
        """A complete row set for the regress profile: the system rows a
        two-family image still emits, plus every bbd row. Checksums are
        offset from the system block so no two rows collide by accident."""
        rows = self.system_rows()
        for i, name in enumerate(runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["bbd"]):
            rows.append(family_row("bbd", name, 100, 101, "%08x" % (0xb0 + i)))
        return rows

    def test_bbd_family_ends_with_the_stage_walk_row(self):
        """The crossfade row is APPENDED, not inserted: row order is
        execution state, and inserting ahead of a row changes that row's
        checksum (bench/README.md, 'Row order is state')."""
        bbd_rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["bbd"]

        self.assertEqual("bbd_line_stage_walk", bbd_rows[-1])
        self.assertEqual(
            (
                "bbd_ceiling",
                "bbd_line_only",
                "bbd_line_tap",
                "bbd_line_tap_half",
                "bbd_walk_sdram",
                "bbd_line_stage_walk",
            ),
            bbd_rows,
        )

    def test_regress_rejects_a_capture_without_the_stage_walk_row(self):
        """The row-set gate must be able to go red on a missing row, or it
        proves nothing when it is green."""
        rows = [
            row
            for row in self.regress_rows()
            if row.split(",")[2] != "bbd_line_stage_walk"
        ]
        capture = runner.parse(capture_lines(rows, families="system bbd"))

        with self.assertRaises(runner.BenchValidationError):
            runner.validate_captures(
                [capture, capture], resolve_profile("regress")
            )

    def test_regress_profile_carries_system_and_bbd(self):
        """The profile's whole point is that the gate rows and the BBD
        kernel rows are in ONE image, so gate-versus-kernel is a same-build
        comparison rather than a cross-image subtraction."""
        profile = resolve_profile("regress")

        self.assertEqual(("system", "bbd"), profile.families)
        self.assertIn(WAVE_ACCEPTANCE, profile.gates)

    def test_regress_capture_validates_against_its_filtered_rowset(self):
        """A capture holding both families is complete for the profile."""
        capture = runner.parse(
            capture_lines(self.regress_rows(), families="system bbd")
        )

        runner.validate_captures(
            [capture, capture], resolve_profile("regress")
        )

    def test_regress_rejects_a_capture_missing_the_bbd_family(self):
        """A system-only image must not be accepted under this profile --
        that is exactly the stale-image mix-up the round cannot afford."""
        capture = runner.parse(
            capture_lines(self.system_rows(), families="system")
        )

        with self.assertRaises(runner.BenchValidationError):
            runner.validate_captures(
                [capture, capture], resolve_profile("regress")
            )

    def test_capture_validation_rejects_duplicate_anchors(self):
        rows = self.system_rows()
        anchors = anchor_lines(rows)
        capture = runner.parse(
            capture_lines(
                rows,
                anchors=anchors + (anchors[0],),
                families="system",
            )
        )

        with self.assertRaisesRegex(
            runner.BenchValidationError,
            "duplicate anchor",
        ):
            runner.validate_captures(
                [capture, capture],
                resolve_profile("system"),
            )

    def test_capture_validation_rejects_nonnumeric_anchor_values(self):
        rows = self.system_rows()
        anchors = anchor_lines(
            rows,
            {"instrument_worst_bbd_dtcm": ("not-a-number", "99.54")},
        )
        capture = runner.parse(
            capture_lines(rows, anchors=anchors, families="system")
        )

        with self.assertRaisesRegex(
            runner.BenchValidationError,
            "non-numeric anchor",
        ):
            runner.validate_captures(
                [capture, capture],
                resolve_profile("system"),
            )

    def test_capture_validation_rejects_timeout_dtcm_bbd_gate(self):
        rows = replace_rows(
            self.system_rows(),
            "BENCH,system,instrument_worst_bbd_dtcm,"
            "TIMEOUT,101,,,aabbccdd",
        )
        capture = runner.parse(
            capture_lines(rows, families="system")
        )

        with self.assertRaisesRegex(
            runner.BenchValidationError,
            "non-numeric gate workload",
        ):
            runner.validate_captures(
                [capture, capture],
                resolve_profile("system"),
            )

    def test_bbd_engine_rows_remain_registered_and_anchored(self):
        system_rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["system"]
        for name in (
            "instrument_worst_bbd",
            "instrument_worst_bbd_dtcm",
            "inst_bbd_engine_worst",
        ):
            self.assertIn(name, system_rows)

        anchors = runner.anchor_names(resolve_profile("system"))
        self.assertIn("instrument_worst_bbd", anchors)
        self.assertIn("instrument_worst_bbd_dtcm", anchors)

    def test_o2_verdict_uses_worst_bbd_engine_repeat(self):
        first = self.gate_capture(
            avg_cyc=947087,
            max_cyc=985609,
            pct_avg="98.65",
            pct_max="102.66",
            callback_avg="98.81",
            callback_max="102.74",
        )
        second = self.gate_capture(
            avg_cyc=947092,
            max_cyc=986105,
            pct_avg="98.65",
            pct_max="102.71",
            callback_avg="98.81",
            callback_max="102.75",
        )

        runner.validate_captures(
            [first, second],
            resolve_profile("system"),
        )
        report = self.write_system_report([first, second])

        self.assertIn(
            "Run 1 102.66 % / 102.74 %; Run 2 102.71 % / 102.75 %",
            report,
        )
        self.assertIn(
            "worst maxima are **102.71 % offline** and "
            "**102.75 % in the real callback**",
            report,
        )
        self.assertIn(
            "**Conclusion: the DTCM BBD-engine gate does not fit.**",
            report,
        )
        self.assertIn("both decks on the BBD part engine", report)
        self.assertIn("STEP freeze engaged", report)
        self.assertNotIn("tape-FLUX gate", report)
        self.assertIn("`fx_flux_sdram`", report)
        self.assertIn("stereo tape FLUX", report)

    def test_o3_verdict_uses_worst_bbd_engine_repeat(self):
        first = self.gate_capture(
            avg_cyc=916310,
            max_cyc=954884,
            pct_avg="95.44",
            pct_max="99.46",
            callback_avg="95.63",
            callback_max="99.52",
        )
        second = self.gate_capture(
            avg_cyc=916322,
            max_cyc=955328,
            pct_avg="95.45",
            pct_max="99.51",
            callback_avg="95.63",
            callback_max="99.54",
        )

        report = self.write_system_report([first, second])

        self.assertIn(
            "Run 1 99.46 % / 99.52 %; Run 2 99.51 % / 99.54 %",
            report,
        )
        self.assertIn(
            "worst maxima are **99.51 % offline** and "
            "**99.54 % in the real callback**",
            report,
        )
        self.assertIn(
            "**Conclusion: the DTCM BBD-engine gate fits.**",
            report,
        )

    def test_repeat_rejects_mixed_optimizations(self):
        rows = self.system_rows()
        o2 = runner.parse(capture_lines(rows, families="system", optimization="o2"))
        o3 = runner.parse(capture_lines(rows, families="system", optimization="o3"))
        with self.assertRaisesRegex(
            runner.BenchValidationError, "optimization differs"
        ):
            runner.validate_captures([o2, o3], resolve_profile("system"))

    def test_requested_o3_rejects_an_o2_capture(self):
        rows = self.system_rows()
        capture = runner.parse(
            capture_lines(rows, families="system", optimization="o2")
        )
        with self.assertRaisesRegex(
            runner.BenchValidationError, "requested optimization o3"
        ):
            runner.validate_captures(
                [capture, capture],
                resolve_profile("system"),
                expected_optimization="o3",
            )

    def test_unknown_reported_optimization_is_rejected(self):
        rows = self.system_rows()
        capture = runner.parse(
            capture_lines(rows, families="system", optimization="turbo")
        )
        with self.assertRaisesRegex(
            runner.BenchValidationError, "unknown optimization turbo"
        ):
            runner.validate_captures(
                [capture, capture],
                resolve_profile("system"),
            )

    def test_dtcm_ab_rejects_unequal_checksums(self):
        """Moving the gate object may change its price, never its output."""
        rows = self.system_rows()
        if not any(
            row.split(",")[2] == "instrument_worst_bbd_dtcm"
            for row in rows
        ):
            rows.append(
                bench_row(
                    "instrument_worst_bbd_dtcm",
                    90,
                    91,
                    "22222222",
                )
            )
        rows = replace_rows(
            rows,
            bench_row("instrument_worst_bbd", 100, 101, "11111111"),
            bench_row("instrument_worst_bbd_dtcm", 90, 91, "22222222"),
        )
        capture = runner.parse(capture_lines(rows, families="system"))

        with self.assertRaisesRegex(
            runner.BenchValidationError,
            "DTCM A/B checksum mismatch",
        ):
            runner.validate_captures(
                [capture, capture],
                resolve_profile("system"),
            )

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

            self.assertTrue(base.endswith("-system-axi-o2"))
            with open(base + ".md", encoding="utf-8") as fh:
                md = fh.read()
            self.assertIn("system", md)
            self.assertIn("Execution layout: `axi`", md)
            self.assertIn("wave_acceptance", md)
            # Every universal gate is recorded as having run.
            self.assertIn("row set", md.lower())
            with open(base + ".csv", encoding="utf-8") as fh:
                csv_text = fh.read()
            self.assertIn("profile", csv_text.splitlines()[0])
            self.assertIn(",system,", csv_text)

    def test_evidence_persists_layout_and_optimization_identity(self):
        capture = runner.parse(
            capture_lines(
                self.system_rows(),
                families="system",
                layout="itcm-hot",
                optimization="o3",
            )
        )
        with tempfile.TemporaryDirectory() as temp:
            base = runner.write_results(
                temp, [capture, capture], resolve_profile("system"), "system"
            )
            with open(base + ".csv", encoding="utf-8") as stream:
                csv_text = stream.read()
            with open(base + ".md", encoding="utf-8") as stream:
                md_text = stream.read()
        self.assertTrue(base.endswith("-system-itcm-hot-o3"))
        self.assertIn(
            "run,profile,board,layout,optimization,qspi_sha256,device_fingerprint",
            csv_text,
        )
        self.assertIn("Optimization: `o3` (`-O3`).", md_text)
        self.assertNotIn("`-ffast-math -funroll-loops`", md_text)

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
        self.assertEqual(len(rows), 11, "sweep family must have exactly 11 rows")

    def test_flux_rate_sweep_rows_are_expected(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        self.assertEqual(
            tuple(name for name in rows if name.startswith("sweep_flux_rate_")),
            tuple("sweep_flux_rate_%d" % index for index in (0, 3, 6, 8, 11)),
        )

    def test_retired_stages_sweep_rows_are_rejected(self):
        rows = runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["sweep"]
        self.assertFalse(
            any(name.startswith("sweep_stages_") for name in rows),
            "no FLUX STAGES sweep row may return",
        )

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

    def test_deck_engine_row_is_expected(self):
        self.assertIn("deck_engine_hot", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"])

    def test_flux_hot_row_is_expected(self):
        # The remainder-split round's re-pricing of FLUX at the deck's own
        # operating point (spec 2026-07-30-remainder-split-design section 4).
        # It lives in `instr`, not `system`, so fx_flux_sdram's own setup --
        # and therefore its checksum, and its comparability with two committed
        # rounds of evidence -- stays untouched.
        self.assertIn("fx_flux_hot", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"])

    def test_tone_solo_row_is_expected(self):
        # The shell's own engine, priced through IPartEngine* so the dispatch a
        # Part pays is inside the figure (spec 2026-07-30-remainder-split-design
        # section 4). Task 3's deck_shell is only interpretable as
        # deck_shell - fx_none - tone_solo - deck_mod_hot, so this row is
        # load-bearing for the round's arithmetic (section 4.1) rather than an
        # extra data point. The fourth term is not decoration and was not in
        # the formula as registered: Part::process runs _mod.process() every
        # sample (Part::process, engine/parts/part.h:246), so deck_shell has the
        # modulation plane, and the three-term version this comment carried
        # until the fix round charged that plane twice (section 9.2).
        self.assertIn("tone_solo", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"])

    def test_deck_shell_row_is_expected(self):
        # A whole Part with the cheapest engine and no FX (spec
        # 2026-07-30-remainder-split-design section 4). It is the first term of
        # section 4.1's corrected
        # `Part-level code = deck_shell - fx_none - tone_solo - deck_mod_hot`,
        # so the round's central quantity does not exist without it. All four
        # of those rows must be present together for that arithmetic to be
        # computable in a single run, which is the reason these assertions sit
        # beside the other three rather than standing alone.
        #
        # deck_mod_hot became a term only after the run (section 9.2). The
        # formula as registered omitted it, which charged the modulation plane
        # -- run by Part::process every sample, engine/parts/part.h:246 --
        # once inside Part-level code and once beside it in remainder', and
        # that single defect accounts for all three of the round's prediction
        # misses. Its co-presence here is load-bearing in exactly the way
        # fx_none's and tone_solo's already were.
        self.assertIn("deck_shell", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"])
        self.assertIn("tone_solo", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"])
        self.assertIn("deck_mod_hot", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["instr"])
        self.assertIn("fx_none", runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["system"])


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

    def test_missing_required_anchor_writes_no_evidence(self):
        rows = ProfileContract().system_rows()
        capture = capture_lines(
            rows,
            anchors=(),
            families="system",
        )
        profile = resolve_profile("system")

        result, artifacts = self.run_with_profile(
            [capture, capture],
            profile,
        )

        self.assertEqual(result, 2)
        self.assertEqual(artifacts, {})

    def test_invalid_capture_does_not_persist_program_receipt(self):
        rows = ProfileContract().system_rows()
        capture = capture_lines(
            rows,
            anchors=(),
            families="system",
        )
        profile = resolve_profile("system")
        with tempfile.TemporaryDirectory() as temp:
            out_dir = Path(temp) / "evidence"
            receipt = Path(temp) / "qspi-verified.json"
            argv = [
                "run.py",
                "--no-build",
                "--program-qspi",
                "--repeat",
                "2",
                "--out-dir",
                str(out_dir),
            ]

            def write_receipt(_payload, _helper, receipt_path, **_kwargs):
                Path(receipt_path).write_text("provisional", encoding="utf-8")

            with (
                mock.patch.object(
                    runner,
                    "prepare_existing_artifacts",
                    return_value={},
                ),
                mock.patch.object(runner, "require_clean_tree"),
                mock.patch.object(
                    runner,
                    "program_and_verify",
                    side_effect=write_receipt,
                ),
                mock.patch.object(
                    runner,
                    "require_verified_payload",
                    return_value={"device_id": DEVICE_ID},
                ),
                mock.patch.object(runner, "require_live_digest"),
                mock.patch.object(runner, "require_live_device"),
                mock.patch.object(
                    runner,
                    "run_once",
                    side_effect=[capture, capture],
                ),
                mock.patch.object(runner, "resolve", return_value=profile),
                mock.patch.object(runner, "QSPI_RECEIPT", str(receipt)),
                mock.patch.object(sys, "argv", argv),
            ):
                result = runner.main()

            self.assertEqual(result, 2)
            self.assertFalse(receipt.exists())
            self.assertFalse(out_dir.exists())

    def test_valid_capture_commits_provisional_program_receipt(self):
        rows = ProfileContract().system_rows()
        capture = capture_lines(rows, families="system")
        profile = resolve_profile("system")
        with tempfile.TemporaryDirectory() as temp:
            out_dir = Path(temp) / "evidence"
            receipt = Path(temp) / "qspi-verified.json"
            argv = [
                "run.py",
                "--no-build",
                "--program-qspi",
                "--repeat",
                "2",
                "--out-dir",
                str(out_dir),
            ]

            def write_receipt(_payload, _helper, receipt_path, **_kwargs):
                Path(receipt_path).write_text("verified", encoding="utf-8")

            with (
                mock.patch.object(
                    runner,
                    "prepare_existing_artifacts",
                    return_value={},
                ),
                mock.patch.object(runner, "require_clean_tree"),
                mock.patch.object(
                    runner,
                    "program_and_verify",
                    side_effect=write_receipt,
                ),
                mock.patch.object(
                    runner,
                    "require_verified_payload",
                    return_value={"device_id": DEVICE_ID},
                ),
                mock.patch.object(runner, "require_live_digest"),
                mock.patch.object(runner, "require_live_device"),
                mock.patch.object(
                    runner,
                    "run_once",
                    side_effect=[capture, capture],
                ),
                mock.patch.object(runner, "resolve", return_value=profile),
                mock.patch.object(runner, "QSPI_RECEIPT", str(receipt)),
                mock.patch.object(sys, "argv", argv),
            ):
                result = runner.main()

            self.assertEqual(result, 0)
            self.assertEqual(
                receipt.read_text(encoding="utf-8"),
                "verified",
            )
            self.assertEqual(
                {path.suffix for path in out_dir.iterdir()},
                {".csv", ".md"},
            )

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
            if name in {
                "instrument_worst_bbd",
                "instrument_worst_bbd_dtcm",
            }:
                checksum = "aabbccdd"
            else:
                checksum = "%08x" % i
            rows.append(bench_row(name, avg, avg + 1, checksum))
        capture = capture_lines(rows, families="system")
        profile = Profile(families=("system",), gates=frozenset())

        result, artifacts = self.run_with_profile([capture, capture], profile)

        self.assertEqual(result, 0)
        self.assertNotIn("WAVE performance gate", artifacts[".md"])
        self.assertNotIn("PASS", artifacts[".md"])


class FakeSerial:
    """A serial line that yields a fixed sequence and then goes quiet.

    readline() returning b"" is exactly what pyserial does when its own
    read timeout expires -- so the empty tail here is a silent line, not
    end-of-file, and run_once_usb has to keep waiting on the host deadline
    rather than treating it as the end of the run.
    """

    def __init__(self, lines):
        self._lines = list(lines)
        self.closed = False

    def readline(self):
        if self._lines:
            return (self._lines.pop(0) + "\r\n").encode()
        return b""

    def close(self):
        self.closed = True


class UsbRunContract(unittest.TestCase):
    def test_run_once_usb_stops_at_bench_end(self):
        port = FakeSerial(["BENCH_BEGIN,families=system", "BENCH,a,b,1", "BENCH_END"])
        lines = runner.run_once_usb(port, timeout=5.0)
        self.assertEqual(lines[-1], "BENCH_END")
        self.assertEqual(len(lines), 3)
        self.assertTrue(port.closed)

    def test_run_once_usb_returns_none_on_timeout(self):
        # A line that never says BENCH_END must yield None. A hang may not
        # produce half a capture that reads like a result.
        port = FakeSerial(["BENCH_BEGIN,families=system"])
        self.assertIsNone(runner.run_once_usb(port, timeout=0.2))
        self.assertTrue(port.closed)

    def test_a_rejected_header_ends_the_run_at_the_first_line(self):
        # Without a probe there is no pre-run receipt, so the proof that the
        # bank on the chip is the linked bank is the digest the measuring
        # firmware itself reports -- and it reports it in line one. Acting on
        # it there costs seconds; acting on it after BENCH_END costs twenty
        # minutes for a capture that was void from the start.
        port = FakeSerial(["BENCH_BEGIN,abc,480000000,96,dc,deadbeef,uid",
                           "BENCH,a,b,1", "BENCH_END"])
        seen = []

        def reject(line):
            seen.append(line)
            raise runner.QspiGuardError("bank on the chip is not the linked bank")

        with self.assertRaises(runner.QspiGuardError):
            runner.run_once_usb(port, timeout=5.0, on_header=reject)
        self.assertEqual(len(seen), 1)
        self.assertTrue(port.closed)

    def test_an_accepted_header_does_not_interrupt_the_run(self):
        port = FakeSerial(["BENCH_BEGIN,abc,480000000,96,dc,deadbeef,uid",
                           "BENCH,a,b,1", "BENCH_END"])
        lines = runner.run_once_usb(port, timeout=5.0, on_header=lambda _: None)
        self.assertEqual(lines[-1], "BENCH_END")

    def test_waits_for_the_board_to_come_back_between_repeats(self):
        # After BENCH_END the board jumps into the bootloader and
        # re-enumerates. dfu-util called into that gap exits 74 and the run
        # dies one repeat short of the two it needs to be evidence.
        answers = [False, False, True]
        self.assertTrue(
            runner.wait_for_dfu(timeout=5.0, probe=lambda: answers.pop(0))
        )

    def test_a_board_that_never_returns_is_not_written_to(self):
        self.assertFalse(
            runner.wait_for_dfu(timeout=0.6, probe=lambda: False)
        )

    def test_new_port_is_the_one_that_was_not_there_before(self):
        # COM5 is already on this desk and is not the board. Identifying it
        # as "the only port" or "the lowest port" would grab the wrong one.
        steps = [{"COM5"}, {"COM5"}, {"COM5", "COM9"}]
        self.assertEqual(
            runner.wait_for_new_port({"COM5"}, timeout=5.0,
                                     lister=lambda: steps.pop(0)),
            "COM9",
        )

    def test_no_new_port_within_the_window_is_reported_as_none(self):
        # Silence here is the DTCM/DMA failure mode: the image loaded, ran,
        # and never enumerated. It must not read as a port to open.
        self.assertIsNone(
            runner.wait_for_new_port({"COM5"}, timeout=0.4,
                                     lister=lambda: {"COM5"})
        )

    def test_load_dfu_targets_the_app_address_and_leaves(self):
        # :leave is what replaces openocd's reset-halt-resume: dfu-util
        # hands control to the freshly written image itself.
        calls = []
        with mock.patch.object(runner.subprocess, "run",
                               side_effect=lambda cmd, **kw: calls.append(cmd)):
            runner.load_dfu("bench-sram.bin", 0x90040000)
        self.assertEqual(len(calls), 1)
        self.assertIn("dfu-util", calls[0][0])
        self.assertIn("0x90040000:leave", calls[0])
        self.assertIn("bench-sram.bin", calls[0])


class BoardContract(unittest.TestCase):
    """Every historical number in docs/bench/ was measured on a Daisy Seed.
    The Patch Submodule is the M6 target and a different board, so a capture
    that does not say which one it came from is not evidence -- it is a
    number that can be quoted for the wrong board a month from now, when
    nobody remembers which one was on the desk today.
    """

    def system_rows(self):
        rows = []
        for i, name in enumerate(runner.BENCH_PROTOCOL_ROWS_BY_FAMILY["system"]):
            avg = 400 if name == "synth_2x4" else 100
            checksum = (
                "aabbccdd"
                if name in {"instrument_worst_bbd", "instrument_worst_bbd_dtcm"}
                else "%08x" % i
            )
            rows.append(bench_row(name, avg, avg + 1, checksum))
        return rows

    def capture(self):
        return runner.parse(capture_lines(self.system_rows(), families="system"))

    def write(self, **kwargs):
        capture = self.capture()
        with tempfile.TemporaryDirectory() as temp:
            base = runner.write_results(
                temp,
                [capture, capture],
                resolve_profile("system"),
                "system",
                **kwargs,
            )
            return (
                base,
                Path(base + ".md").read_text(encoding="utf-8"),
                Path(base + ".csv").read_text(encoding="utf-8"),
            )

    def test_build_requests_the_board_make_mode(self):
        with (
            mock.patch.object(runner.subprocess, "run") as run,
            mock.patch.object(
                runner, "prepare_existing_artifacts", return_value={}
            ),
        ):
            runner.build(("system",), board="patch_sm")

        self.assertIn("BENCH_BOARD=patch_sm", run.call_args_list[1].args[0])

    def test_build_defaults_to_the_board_the_history_was_taken_on(self):
        with (
            mock.patch.object(runner.subprocess, "run") as run,
            mock.patch.object(
                runner, "prepare_existing_artifacts", return_value={}
            ),
        ):
            runner.build(("system",))

        self.assertIn("BENCH_BOARD=seed", run.call_args_list[1].args[0])

    def test_a_seed_capture_keeps_the_name_the_history_uses(self):
        """Same reasoning as the transport suffix in write_results: every
        capture in docs/bench/ predating this switch came off a Seed, so
        naming the Seed explicitly would rename history for no gain and
        break every cross-reference into it."""
        base, _md, _csv = self.write(board="seed", transport="usb")

        self.assertTrue(base.endswith("-system-axi-o2-usb"), base)

    def test_a_submodule_capture_carries_the_board_in_its_name(self):
        base, _md, _csv = self.write(board="patch_sm", transport="usb")

        self.assertTrue(base.endswith("-system-axi-o2-patch_sm-usb"), base)

    def test_every_csv_row_carries_the_board(self):
        """A reader who opens the CSV alone, without the filename, must still
        be able to tell the two boards apart -- that is the whole point of a
        machine-readable row."""
        _base, _md, csv_text = self.write(board="patch_sm", transport="usb")

        header = csv_text.splitlines()[0]
        self.assertIn("board", header.split(","))
        for line in csv_text.splitlines()[1:]:
            self.assertIn("patch_sm", line.split(","))

    def test_the_report_names_the_board_it_was_measured_on(self):
        """write_results used to state 'Measured on a Daisy Seed' as a
        constant. Left alone, the very first submodule capture would have
        said, in its own evidence document, that it came off the board it
        exists to be compared against."""
        _base, seed_md, _csv = self.write(board="seed")
        _base, patch_md, _csv = self.write(board="patch_sm")

        self.assertIn("Daisy Seed", seed_md)
        self.assertNotIn("Patch Submodule", seed_md)
        self.assertIn("Daisy Patch Submodule", patch_md)
        self.assertNotIn("Daisy Seed", patch_md)


if __name__ == "__main__":
    unittest.main()
