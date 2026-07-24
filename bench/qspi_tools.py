"""Split-image and SRAM-helper QSPI guards for the Daisy WAVE bench."""

from pathlib import Path
import hashlib
import json
import re
import subprocess


QSPI_ADDRESS = 0x90040000
QSPI_SIZE = 65024
QSPI_SECTION = ".qspiflash_data"
SRAM_BASE = 0x24000000
STAGING_ADDRESS = 0x24040000
RECEIPT_MODE = "swd-sram-target-byte-identity"


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
    except subprocess.TimeoutExpired as error:
        raise QspiGuardError(
            "command timed out: %s" % " ".join(command)
        ) from error


def prepare_split_artifacts(
    elf_path,
    sram_elf_path,
    payload_path,
    *,
    programmer_elf_path,
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
    return artifact_identity(
        elf_path, sram_elf_path, payload_path, programmer_elf_path
    )


def artifact_identity(
    elf_path, sram_elf_path, payload_path, programmer_elf_path
):
    paths = (
        Path(elf_path),
        Path(sram_elf_path),
        Path(payload_path),
        Path(programmer_elf_path),
    )
    if not all(path.is_file() for path in paths):
        raise QspiGuardError(
            "split-image identity requires the bench, SRAM, payload, "
            "and programmer artifacts"
        )
    return {
        "elf_sha256": payload_sha256(paths[0]),
        "sram_elf_sha256": payload_sha256(paths[1]),
        "qspi_sha256": payload_sha256(paths[2]),
        "programmer_elf_sha256": payload_sha256(paths[3]),
    }


def write_verified_receipt(
    payload_path,
    receipt_path,
    device_id,
    artifact_identity,
    observed_sha256,
):
    payload_path = Path(payload_path)
    receipt_path = Path(receipt_path)
    if receipt_path.exists():
        receipt_path.unlink()
    if not payload_path.is_file() or payload_path.stat().st_size != QSPI_SIZE:
        raise QspiGuardError("QSPI payload must be exactly %d bytes" % QSPI_SIZE)
    expected_sha256 = payload_sha256(payload_path)
    if observed_sha256 != expected_sha256:
        raise QspiGuardError(
            "target QSPI byte-compare digest does not match the payload"
        )
    if not re.fullmatch(r"[0-9a-f]{24}", device_id or ""):
        raise QspiGuardError("target MCU UID is not 24 lowercase hex digits")
    receipt = {
        "address": QSPI_ADDRESS,
        "size": QSPI_SIZE,
        "sha256": expected_sha256,
        "device_id": device_id,
        "verification": RECEIPT_MODE,
        "artifacts": artifact_identity,
    }
    temporary = receipt_path.with_name(receipt_path.name + ".tmp")
    temporary.write_text(
        json.dumps(receipt, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(receipt_path)


def require_verified_payload(payload_path, receipt_path, current_identity):
    payload_path = Path(payload_path)
    receipt_path = Path(receipt_path)
    if not payload_path.is_file():
        raise QspiGuardError("QSPI payload is missing; run a bench build first")
    if payload_path.stat().st_size != QSPI_SIZE:
        raise QspiGuardError("QSPI payload size does not match the linked bank")
    if not receipt_path.is_file():
        raise QspiGuardError(
            "QSPI is not verified for this payload; connect the ST-Link and "
            "run run.py --program-qspi --build-only"
        )
    try:
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise QspiGuardError("QSPI verification receipt is unreadable") from error
    expected = {
        "address": QSPI_ADDRESS,
        "size": QSPI_SIZE,
        "sha256": payload_sha256(payload_path),
        "verification": RECEIPT_MODE,
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


def parse_helper_load_segments(readelf_text):
    segments = []
    for line in readelf_text.splitlines():
        match = re.match(
            r"^\s*LOAD\s+"
            r"(0x[0-9a-fA-F]+)\s+"
            r"(0x[0-9a-fA-F]+)\s+"
            r"(0x[0-9a-fA-F]+)\s+"
            r"(0x[0-9a-fA-F]+)\s+"
            r"(0x[0-9a-fA-F]+)\s+",
            line,
        )
        if not match:
            continue
        physical = int(match.group(3), 16)
        file_size = int(match.group(4), 16)
        if file_size == 0:
            continue
        end = physical + file_size
        if physical < SRAM_BASE or end > STAGING_ADDRESS:
            raise QspiGuardError(
                "programmer LOAD bytes 0x%08x..0x%08x overlap or escape "
                "the SRAM helper area below 0x%08x"
                % (physical, end, STAGING_ADDRESS)
            )
        segments.append((physical, file_size))
    if not segments:
        raise QspiGuardError("programmer ELF has no file-backed LOAD segments")
    return segments


def validate_helper_elf(helper_path, *, readelf, run=subprocess.run):
    helper_path = Path(helper_path)
    if not helper_path.is_file():
        raise QspiGuardError("QSPI SRAM programmer ELF is missing: %s" % helper_path)
    table = _checked_run(
        run,
        [readelf, "-lW", str(helper_path)],
        capture_output=True,
        text=True,
    )
    return parse_helper_load_segments(table.stdout)


def parse_programmer_result(output):
    records = [
        line.strip()
        for line in output.splitlines()
        if line.strip().startswith("QSPI_PROGRAM_")
    ]
    if len(records) != 1:
        raise QspiGuardError(
            "expected exactly one QSPI programmer record, found %d"
            % len(records)
        )
    error = re.fullmatch(r"QSPI_PROGRAM_ERROR,([a-z]+)", records[0])
    if error:
        raise QspiGuardError(
            "QSPI SRAM programmer failed at stage: %s" % error.group(1)
        )
    success = re.fullmatch(
        r"QSPI_PROGRAM_OK,90040000,65024,"
        r"([0-9a-f]{64}),([0-9a-fA-F]{24})",
        records[0],
    )
    if not success:
        raise QspiGuardError(
            "QSPI programmer record has the wrong address, size, digest, or UID"
        )
    digest, device_id = success.groups()
    return digest, device_id.lower()


def program_and_verify(
    payload_path,
    helper_path,
    receipt_path,
    *,
    artifact_identity,
    openocd,
    scripts,
    interface,
    config,
    readelf,
    run=subprocess.run,
):
    payload_path = Path(payload_path)
    helper_path = Path(helper_path)
    receipt_path = Path(receipt_path)
    if receipt_path.exists():
        receipt_path.unlink()
    if not payload_path.is_file() or payload_path.stat().st_size != QSPI_SIZE:
        raise QspiGuardError(
            "refusing QSPI programming: payload is absent or wrong-sized"
        )
    validate_helper_elf(helper_path, readelf=readelf, run=run)
    if artifact_identity.get("qspi_sha256") != payload_sha256(payload_path):
        raise QspiGuardError("artifact identity does not match the QSPI payload")
    if artifact_identity.get("programmer_elf_sha256") != payload_sha256(helper_path):
        raise QspiGuardError("artifact identity does not match the programmer ELF")

    completed = _checked_run(
        run,
        [
            openocd,
            "-s",
            scripts,
            "-f",
            "interface/%s" % interface,
            "-f",
            "target/stm32h7x.cfg",
            "-c",
            "set HELPER {%s}" % str(helper_path).replace("\\", "/"),
            "-c",
            "set PAYLOAD {%s}" % str(payload_path).replace("\\", "/"),
            "-f",
            str(config),
        ],
        capture_output=True,
        text=True,
        timeout=130,
    )
    digest, device_id = parse_programmer_result(
        (completed.stdout or "") + (completed.stderr or "")
    )
    receipt_path.parent.mkdir(parents=True, exist_ok=True)
    write_verified_receipt(
        payload_path,
        receipt_path,
        device_id,
        artifact_identity,
        digest,
    )
