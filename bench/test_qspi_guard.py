"""Host tests for the split QSPI/SRAM bench programming guard."""

import importlib.util
from pathlib import Path
import shutil
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
            readback = root / "readback.bin"
            receipt = root / "qspi-verified.json"
            payload.write_bytes(bytes(range(256)) * 254)
            readback.write_bytes(payload.read_bytes())

            identity = {
                "elf_sha256": "e" * 64,
                "sram_elf_sha256": "s" * 64,
                "qspi_sha256": tools.payload_sha256(payload),
            }
            tools.write_verified_receipt(
                payload,
                readback,
                receipt,
                device_id="task8-device",
                artifact_identity=identity,
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
            readback = root / "readback.bin"
            receipt = root / "qspi-verified.json"
            payload.write_bytes(bytes(range(256)) * 254)
            readback.write_bytes(payload.read_bytes())
            identity = {
                "elf_sha256": "a" * 64,
                "sram_elf_sha256": "b" * 64,
                "qspi_sha256": tools.payload_sha256(payload),
            }
            tools.write_verified_receipt(
                payload,
                readback,
                receipt,
                device_id="task8-device",
                artifact_identity=identity,
            )
            changed = dict(identity, sram_elf_sha256="c" * 64)
            with self.assertRaises(tools.QspiGuardError):
                tools.require_verified_payload(payload, receipt, changed)

            bad_readback = root / "bad-readback.bin"
            bad_readback.write_bytes(payload.read_bytes()[:-1] + b"\0")
            with self.assertRaises(tools.QspiGuardError):
                tools.write_verified_receipt(
                    payload,
                    bad_readback,
                    receipt,
                    device_id="task8-device",
                    artifact_identity=identity,
                )

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

    def test_program_path_downloads_then_uploads_and_verifies(self) -> None:
        tools = self.require_module()
        calls = []
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            payload = root / "bench-qspi.bin"
            receipt = root / "qspi-verified.json"
            payload.write_bytes(bytes(range(251)) * 259 + b"x" * 15)

            def fake_run(command, **kwargs):
                calls.append(command)
                if "-l" in command:
                    return subprocess.CompletedProcess(
                        command,
                        0,
                        stdout='Found DFU: [0483:df11] serial="TASK8SERIAL"\n',
                        stderr="",
                    )
                if "-U" in command:
                    upload_path = Path(command[command.index("-U") + 1])
                    shutil.copyfile(payload, upload_path)
                return subprocess.CompletedProcess(
                    command, 0, stdout="", stderr=""
                )

            identity = {
                "elf_sha256": "a" * 64,
                "sram_elf_sha256": "b" * 64,
                "qspi_sha256": tools.payload_sha256(payload),
            }
            tools.program_and_verify(
                payload,
                receipt,
                artifact_identity=identity,
                dfu_util="dfu-util",
                run=fake_run,
            )
            tools.require_verified_payload(payload, receipt, identity)

        self.assertEqual(
            calls[1],
            [
                "dfu-util",
                "-a",
                "0",
                "-s",
                "0x90040000",
                "-D",
                str(payload),
                "-d",
                ",0483:df11",
            ],
        )
        self.assertEqual(calls[2][0:5], [
            "dfu-util", "-a", "0", "-s", "0x90040000:65024",
        ])
        self.assertEqual(calls[2][-2:], ["-d", ",0483:df11"])


if __name__ == "__main__":
    unittest.main()
