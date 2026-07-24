"""Split-image and verified-DFU helpers for the Daisy WAVE bench."""

from pathlib import Path
import hashlib
import json
import re
import subprocess
import tempfile


QSPI_ADDRESS = 0x90040000
QSPI_SIZE = 65024
QSPI_SECTION = ".qspiflash_data"
DFU_DEVICE = ",0483:df11"


class QspiGuardError(RuntimeError):
    pass


def payload_sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_qspi_section(objdump_text):
    match = re.search(
        r"^\s*\d+\s+\.qspiflash_data\s+"
        r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+",
        objdump_text,
        re.MULTILINE,
    )
    if not match:
        raise QspiGuardError("ELF has no loadable .qspiflash_data section")
    size = int(match.group(1), 16)
    vma = int(match.group(2), 16)
    lma = int(match.group(3), 16)
    if (vma, lma, size) != (QSPI_ADDRESS, QSPI_ADDRESS, QSPI_SIZE):
        raise QspiGuardError(
            "unexpected QSPI layout: VMA=0x%08x LMA=0x%08x size=%d; "
            "expected 0x%08x/0x%08x/%d"
            % (vma, lma, size, QSPI_ADDRESS, QSPI_ADDRESS, QSPI_SIZE)
        )
    return vma, size


def _checked_run(run, command, **kwargs):
    try:
        return run(command, check=True, **kwargs)
    except subprocess.CalledProcessError as error:
        raise QspiGuardError(
            "command failed (%s): %s" % (error.returncode, " ".join(command))
        ) from error


def prepare_split_artifacts(
    elf_path,
    sram_elf_path,
    payload_path,
    *,
    objcopy,
    objdump,
    run=subprocess.run,
):
    elf_path = Path(elf_path)
    sram_elf_path = Path(sram_elf_path)
    payload_path = Path(payload_path)
    if not elf_path.is_file():
        raise QspiGuardError("bench ELF is missing: %s" % elf_path)

    table = _checked_run(
        run,
        [objdump, "-h", str(elf_path)],
        capture_output=True,
        text=True,
    )
    parse_qspi_section(table.stdout)

    _checked_run(
        run,
        [
            objcopy,
            "--only-section=%s" % QSPI_SECTION,
            "-O",
            "binary",
            str(elf_path),
            str(payload_path),
        ],
    )
    _checked_run(
        run,
        [
            objcopy,
            "--remove-section=%s" % QSPI_SECTION,
            str(elf_path),
            str(sram_elf_path),
        ],
    )
    if not payload_path.is_file() or payload_path.stat().st_size != QSPI_SIZE:
        size = payload_path.stat().st_size if payload_path.exists() else -1
        raise QspiGuardError(
            "extracted QSPI payload is %d bytes; expected %d" % (size, QSPI_SIZE)
        )
    sram_table = _checked_run(
        run,
        [objdump, "-h", str(sram_elf_path)],
        capture_output=True,
        text=True,
    )
    if QSPI_SECTION in sram_table.stdout:
        raise QspiGuardError("SRAM-only ELF still contains .qspiflash_data")
    return artifact_identity(elf_path, sram_elf_path, payload_path)


def artifact_identity(elf_path, sram_elf_path, payload_path):
    paths = (Path(elf_path), Path(sram_elf_path), Path(payload_path))
    if not all(path.is_file() for path in paths):
        raise QspiGuardError("split-image identity requires all three artifacts")
    return {
        "elf_sha256": payload_sha256(paths[0]),
        "sram_elf_sha256": payload_sha256(paths[1]),
        "qspi_sha256": payload_sha256(paths[2]),
    }


def write_verified_receipt(
    payload_path,
    readback_path,
    receipt_path,
    device_id,
    artifact_identity,
):
    payload_path = Path(payload_path)
    readback_path = Path(readback_path)
    receipt_path = Path(receipt_path)
    if not payload_path.is_file() or payload_path.stat().st_size != QSPI_SIZE:
        raise QspiGuardError("QSPI payload must be exactly %d bytes" % QSPI_SIZE)
    if not readback_path.is_file() or readback_path.read_bytes() != payload_path.read_bytes():
        raise QspiGuardError("DFU QSPI readback does not match the programmed payload")
    receipt = {
        "address": QSPI_ADDRESS,
        "size": QSPI_SIZE,
        "sha256": payload_sha256(payload_path),
        "device_id": device_id,
        "verification": "dfu-upload-byte-identity",
        "artifacts": artifact_identity,
    }
    receipt_path.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def require_verified_payload(payload_path, receipt_path, current_identity):
    payload_path = Path(payload_path)
    receipt_path = Path(receipt_path)
    if not payload_path.is_file():
        raise QspiGuardError("QSPI payload is missing; run a bench build first")
    if payload_path.stat().st_size != QSPI_SIZE:
        raise QspiGuardError("QSPI payload size does not match the linked bank")
    if not receipt_path.is_file():
        raise QspiGuardError(
            "QSPI is not verified for this payload; put the Seed in Daisy "
            "bootloader DFU mode and run run.py --program-qspi --build-only"
        )
    try:
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise QspiGuardError("QSPI verification receipt is unreadable") from error
    expected = {
        "address": QSPI_ADDRESS,
        "size": QSPI_SIZE,
        "sha256": payload_sha256(payload_path),
        "verification": "dfu-upload-byte-identity",
        "artifacts": current_identity,
    }
    for key, value in expected.items():
        if receipt.get(key) != value:
            raise QspiGuardError(
                "QSPI verification receipt does not match current payload (%s)" % key
            )
    return receipt


def require_live_digest(observed_sha256, payload_path):
    expected = payload_sha256(payload_path)
    if not re.fullmatch(r"[0-9a-f]{64}", observed_sha256 or ""):
        raise QspiGuardError("firmware did not report a valid live QSPI SHA-256")
    if observed_sha256 != expected:
        raise QspiGuardError(
            "live Seed QSPI SHA-256 does not match the extracted bank payload"
        )


def require_clean_tree(repo_path, run=subprocess.run):
    status = _checked_run(
        run,
        [
            "git",
            "-C",
            str(repo_path),
            "status",
            "--porcelain",
            "--untracked-files=all",
        ],
        capture_output=True,
        text=True,
    )
    if status.stdout.strip():
        raise QspiGuardError(
            "refusing hardware evidence from a dirty working tree; commit or "
            "remove all tracked and untracked source changes first"
        )


def _dfu_device_id(listing):
    serials = set(
        re.findall(
            r'Found DFU: \[0483:df11\].*?serial="([^"]+)"',
            listing,
            re.IGNORECASE,
        )
    )
    if len(serials) != 1:
        raise QspiGuardError(
            "expected exactly one Daisy bootloader DFU device, found %d" % len(serials)
        )
    return next(iter(serials))


def program_and_verify(
    payload_path,
    receipt_path,
    *,
    artifact_identity,
    dfu_util="dfu-util",
    run=subprocess.run,
):
    payload_path = Path(payload_path)
    receipt_path = Path(receipt_path)
    if not payload_path.is_file() or payload_path.stat().st_size != QSPI_SIZE:
        raise QspiGuardError("refusing DFU: QSPI payload is absent or wrong-sized")
    if receipt_path.exists():
        receipt_path.unlink()

    listing = _checked_run(
        run,
        [dfu_util, "-l", "-d", DFU_DEVICE],
        capture_output=True,
        text=True,
    )
    device_id = _dfu_device_id(listing.stdout + listing.stderr)
    _checked_run(
        run,
        [
            dfu_util,
            "-a",
            "0",
            "-s",
            "0x%08x" % QSPI_ADDRESS,
            "-D",
            str(payload_path),
            "-d",
            DFU_DEVICE,
        ],
    )

    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        prefix="qspi-readback-", suffix=".bin", dir=receipt_path.parent, delete=False
    ) as temporary:
        readback_path = Path(temporary.name)
    try:
        _checked_run(
            run,
            [
                dfu_util,
                "-a",
                "0",
                "-s",
                "0x%08x:%d" % (QSPI_ADDRESS, QSPI_SIZE),
                "-U",
                str(readback_path),
                "-d",
                DFU_DEVICE,
            ],
        )
        write_verified_receipt(
            payload_path,
            readback_path,
            receipt_path,
            device_id,
            artifact_identity,
        )
    finally:
        if readback_path.exists():
            readback_path.unlink()
