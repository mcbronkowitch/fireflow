"""Host tests for the split QSPI/SRAM bench programming guard."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock


MODULE_PATH = Path(__file__).with_name("qspi_tools.py")
MODULE_SPEC = importlib.util.spec_from_file_location("task8_qspi_tools", MODULE_PATH)
if MODULE_SPEC and MODULE_SPEC.loader and MODULE_PATH.exists():
    qspi_tools = importlib.util.module_from_spec(MODULE_SPEC)
    MODULE_SPEC.loader.exec_module(qspi_tools)
else:
    qspi_tools = None

RUNNER_PATH = MODULE_PATH.with_name("run.py")
sys.path.insert(0, str(MODULE_PATH.parent))
RUNNER_SPEC = importlib.util.spec_from_file_location("task8_runner", RUNNER_PATH)
if RUNNER_SPEC and RUNNER_SPEC.loader:
    runner = importlib.util.module_from_spec(RUNNER_SPEC)
    RUNNER_SPEC.loader.exec_module(runner)
else:
    runner = None


class QspiGuardContract(unittest.TestCase):
    def require_module(self):
        self.assertIsNotNone(
            qspi_tools,
            "bench/qspi_tools.py must implement the split-image programming guard",
        )
        return qspi_tools

    def test_linked_section_parser_requires_exact_reserved_app_address(self) -> None:
        tools = self.require_module()
        table = """
Idx Name                Size      VMA       LMA       File off  Algn
  4 .qspiflash_data     0000fe00  90040000  90040000  00040000  2**2
"""
        self.assertEqual(
            tools.parse_qspi_section(table),
            (0x90040000, 0xFE00),
        )
        with self.assertRaises(tools.QspiGuardError):
            tools.parse_qspi_section(table.replace("90040000", "90000000"))

    def test_receipt_accepts_only_byte_verified_current_payload(self) -> None:
        tools = self.require_module()
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload = root / "bench-qspi.bin"
            receipt = root / "qspi-verified.json"
            payload.write_bytes(bytes(range(256)) * 254)

            identity = {
                "elf_sha256": "e" * 64,
                "sram_elf_sha256": "s" * 64,
                "qspi_sha256": tools.payload_sha256(payload),
                "programmer_elf_sha256": "p" * 64,
            }
            tools.write_verified_receipt(
                payload,
                receipt,
                device_id="00112233445566778899aabb",
                artifact_identity=identity,
                observed_sha256=tools.payload_sha256(payload),
            )
            tools.require_verified_payload(payload, receipt, identity)

            payload.write_bytes(payload.read_bytes() + b"\0")
            with self.assertRaises(tools.QspiGuardError):
                tools.require_verified_payload(payload, receipt, identity)

    def test_receipt_is_bound_to_authoritative_elf_and_sram_derivative(self) -> None:
        tools = self.require_module()
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload = root / "bench-qspi.bin"
            receipt = root / "qspi-verified.json"
            payload.write_bytes(bytes(range(256)) * 254)
            identity = {
                "elf_sha256": "a" * 64,
                "sram_elf_sha256": "b" * 64,
                "qspi_sha256": tools.payload_sha256(payload),
                "programmer_elf_sha256": "d" * 64,
            }
            tools.write_verified_receipt(
                payload,
                receipt,
                device_id="00112233445566778899aabb",
                artifact_identity=identity,
                observed_sha256=tools.payload_sha256(payload),
            )
            changed = dict(identity, sram_elf_sha256="c" * 64)
            with self.assertRaises(tools.QspiGuardError):
                tools.require_verified_payload(payload, receipt, changed)

            with self.assertRaises(tools.QspiGuardError):
                tools.write_verified_receipt(
                    payload,
                    receipt,
                    device_id="00112233445566778899aabb",
                    artifact_identity=identity,
                    observed_sha256="0" * 64,
                )
            self.assertFalse(receipt.exists())

    def test_live_bank_digest_must_match_current_extracted_payload(self) -> None:
        tools = self.require_module()
        with tempfile.TemporaryDirectory() as temp:
            payload = Path(temp) / "bench-qspi.bin"
            payload.write_bytes(bytes(range(256)) * 254)
            expected = tools.payload_sha256(payload)
            tools.require_live_digest(expected, payload)
            with self.assertRaises(tools.QspiGuardError):
                tools.require_live_digest("0" * 64, payload)

    def test_hardware_gate_rejects_a_dirty_tree(self) -> None:
        tools = self.require_module()

        def fake_run(command, **kwargs):
            return subprocess.CompletedProcess(
                command, 0, stdout=" M bench/run.py\n", stderr=""
            )

        with self.assertRaises(tools.QspiGuardError):
            tools.require_clean_tree("repo", run=fake_run)

    def test_silent_openocd_respects_the_host_timeout(self) -> None:
        self.assertIsNotNone(runner)
        stopped = threading.Event()

        class SilentPipe:
            def readline(self):
                stopped.wait(5)
                return ""

        class FakeProcess:
            stdout = SilentPipe()

            def terminate(self):
                stopped.set()

            def wait(self, timeout=None):
                return 0

            def kill(self):
                stopped.set()

        started = time.monotonic()
        with mock.patch.object(
            runner.subprocess, "Popen", return_value=FakeProcess()
        ):
            self.assertIsNone(runner.run_once("fake.cfg", 0.05))
        self.assertLess(time.monotonic() - started, 0.5)

    def test_helper_layout_rejects_file_backed_bytes_at_the_staging_address(self) -> None:
        tools = self.require_module()
        safe = """
  LOAD 0x010000 0x24000000 0x24000000 0x011910 0x011910 RWE 0x10000
  LOAD 0x030000 0x20000000 0x24011910 0x0003e0 0x003234 RW  0x10000
"""
        tools.parse_helper_load_segments(safe)
        unsafe = safe.replace("0x0003e0", "0x030000")
        with self.assertRaises(tools.QspiGuardError):
            tools.parse_helper_load_segments(unsafe)

    def test_programmer_result_requires_one_exact_success_record(self) -> None:
        tools = self.require_module()
        digest = "a" * 64
        uid = "0123456789abcdefABCDEF01"
        record = "QSPI_PROGRAM_OK,90040000,65024,%s,%s" % (digest, uid)
        self.assertEqual(
            tools.parse_programmer_result("debug noise\n" + record + "\n"),
            (digest, uid.lower()),
        )
        with self.assertRaises(tools.QspiGuardError):
            tools.parse_programmer_result(record + "\n" + record)
        with self.assertRaises(tools.QspiGuardError):
            tools.parse_programmer_result("QSPI_PROGRAM_ERROR,compare")

    def test_program_path_uses_sram_helper_and_target_digest(self) -> None:
        tools = self.require_module()
        calls = []
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload = root / "bench-qspi.bin"
            helper = root / "qspi-programmer.elf"
            receipt = root / "qspi-verified.json"
            payload.write_bytes(bytes(range(251)) * 259 + b"x" * 15)
            helper.write_bytes(b"ELF")
            digest = tools.payload_sha256(payload)

            def fake_run(command, **kwargs):
                calls.append(command)
                if command[0] == "readelf":
                    return subprocess.CompletedProcess(
                        command,
                        0,
                        stdout=(
                            "  LOAD 0x010000 0x24000000 0x24000000 "
                            "0x011910 0x011910 RWE 0x10000\n"
                        ),
                        stderr="",
                    )
                return subprocess.CompletedProcess(
                    command,
                    0,
                    stdout=(
                        "Info : target halted\n"
                        "QSPI_PROGRAM_OK,90040000,65024,%s,00112233445566778899aabb\n"
                        % digest
                    ),
                    stderr="",
                )

            identity = {
                "elf_sha256": "a" * 64,
                "sram_elf_sha256": "b" * 64,
                "qspi_sha256": digest,
                "programmer_elf_sha256": tools.payload_sha256(helper),
            }
            tools.program_and_verify(
                payload,
                helper,
                receipt,
                artifact_identity=identity,
                openocd="openocd",
                scripts="scripts",
                interface="stlink-dap.cfg",
                config="qspi-programmer.cfg",
                readelf="readelf",
                run=fake_run,
            )
            tools.require_verified_payload(payload, receipt, identity)

        self.assertEqual(calls[0], ["readelf", "-lW", str(helper)])
        command = calls[1]
        self.assertEqual(command[0], "openocd")
        self.assertIn("set HELPER {%s}" % str(helper).replace("\\", "/"), command)
        self.assertIn("set PAYLOAD {%s}" % str(payload).replace("\\", "/"), command)
        self.assertNotIn("dfu-util", command)

    def test_program_timeout_removes_any_stale_receipt(self) -> None:
        tools = self.require_module()
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload = root / "bench-qspi.bin"
            helper = root / "qspi-programmer.elf"
            receipt = root / "qspi-verified.json"
            payload.write_bytes(bytes(range(251)) * 259 + b"x" * 15)
            helper.write_bytes(b"ELF")
            receipt.write_text("stale", encoding="utf-8")

            def fake_run(command, **kwargs):
                if command[0] == "readelf":
                    return subprocess.CompletedProcess(
                        command,
                        0,
                        stdout=(
                            "  LOAD 0x010000 0x24000000 0x24000000 "
                            "0x011910 0x011910 RWE 0x10000\n"
                        ),
                        stderr="",
                    )
                raise subprocess.TimeoutExpired(command, kwargs["timeout"])

            identity = {
                "elf_sha256": "a" * 64,
                "sram_elf_sha256": "b" * 64,
                "qspi_sha256": tools.payload_sha256(payload),
                "programmer_elf_sha256": tools.payload_sha256(helper),
            }
            with self.assertRaises(tools.QspiGuardError):
                tools.program_and_verify(
                    payload,
                    helper,
                    receipt,
                    artifact_identity=identity,
                    openocd="openocd",
                    scripts="scripts",
                    interface="stlink-dap.cfg",
                    config="qspi-programmer.cfg",
                    readelf="readelf",
                    run=fake_run,
                )
            self.assertFalse(receipt.exists())


if __name__ == "__main__":
    unittest.main()
