"""Build and launch the init-patch audition without persistent device writes."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from bench.audition.prepare_factory import prepare


ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
BUILD = HERE / "build"
TOOLCHAIN = Path(r"C:\Program Files\DaisyToolchain\bin")
OPENOCD = TOOLCHAIN / "openocd.exe"
SCRIPTS = Path(r"C:\Program Files\DaisyToolchain\openocd\scripts")
OBJCOPY = TOOLCHAIN / "arm-none-eabi-objcopy.exe"
OBJDUMP = TOOLCHAIN / "arm-none-eabi-objdump.exe"
NM = TOOLCHAIN / "arm-none-eabi-nm.exe"
GIT_BASH = Path(r"C:\Program Files\Git\bin\bash.exe")

QSPI_ADDRESS = 0x90040000
QSPI_SIZE = 65024


class LayoutError(RuntimeError):
    pass


def parse_objdump_sections(text: str) -> dict[str, tuple[int, int, int]]:
    sections: dict[str, tuple[int, int, int]] = {}
    pattern = re.compile(
        r"^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+"
        r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+",
        re.MULTILINE,
    )
    for match in pattern.finditer(text):
        name, size, vma, lma = match.groups()
        sections[name] = (int(size, 16), int(vma, 16), int(lma, 16))
    return sections


def parse_nm_symbols(text: str) -> dict[str, int]:
    symbols: dict[str, int] = {}
    pattern = re.compile(
        r"^\s*([0-9a-fA-F]+)\s+\S\s+(\S+)\s*$", re.MULTILINE
    )
    for match in pattern.finditer(text):
        address, name = match.groups()
        symbols[name] = int(address, 16)
    return symbols


def validate_layout(
    sections: dict[str, tuple[int, int, int]],
    symbols: dict[str, int],
) -> None:
    qspi = sections.get(".qspiflash_data")
    if qspi != (QSPI_SIZE, QSPI_ADDRESS, QSPI_ADDRESS):
        raise LayoutError(
            "expected the read-only QSPI payload to be exactly "
            "65024 bytes at VMA/LMA 0x90040000"
        )
    state = symbols.get("g_audition_state")
    upload = symbols.get("g_factory_upload")
    if state is None or not (
        0x20000000 <= state < 0x20020000
        or 0x24000000 <= state < 0x24080000
    ):
        raise LayoutError("g_audition_state is absent or outside SRAM")
    if upload is None or not 0xC0000000 <= upload < 0xC4000000:
        raise LayoutError("g_factory_upload is absent or outside SDRAM")


def validate_openocd_config(text: str) -> None:
    persistent_write = re.compile(
        r"^\s*(?:flash\s|program(?:\s|$)|nand\s|stm32h7x\s+mass_erase)",
        re.IGNORECASE | re.MULTILINE,
    )
    if persistent_write.search(text):
        raise LayoutError("OpenOCD config contains a persistent write command")


def _tcl_path(path: Path) -> str:
    return str(path.resolve()).replace("\\", "/")


def build_openocd_command(
    *,
    openocd: Path,
    scripts: Path,
    interface: str,
    config: Path,
    sram_elf: Path,
    qspi_payload: Path,
    factory_bin: Path,
    state_address: int,
    upload_address: int,
) -> list[str]:
    variables = (
        ("SRAM_ELF", sram_elf),
        ("QSPI_PAYLOAD", qspi_payload),
        ("FACTORY_BIN", factory_bin),
    )
    command = [
        str(openocd),
        "-s",
        str(scripts),
        "-f",
        f"interface/{interface}",
        "-f",
        "target/stm32h7x.cfg",
    ]
    for name, path in variables:
        command.extend(["-c", f"set {name} {{{_tcl_path(path)}}}"])
    command.extend(
        [
            "-c",
            f"set STATE_ADDRESS 0x{state_address:08x}",
            "-c",
            f"set UPLOAD_ADDRESS 0x{upload_address:08x}",
            "-f",
            str(config),
        ]
    )
    return command


def _run_text(command: list[str]) -> str:
    completed = subprocess.run(
        command, check=True, capture_output=True, text=True
    )
    return completed.stdout


def build() -> tuple[Path, Path, Path, int, int]:
    if sys.platform == "win32":
        repo = _tcl_path(ROOT)
        shell_command = (
            "export PATH=/c/Progra~1/DaisyToolchain/bin:/usr/bin:/bin; "
            f"cd {repo}/bench/audition && make -j8 build/audition.elf"
        )
        subprocess.run([str(GIT_BASH), "-lc", shell_command], check=True)
    else:
        subprocess.run(
            ["make", "-j8", "build/audition.elf"], cwd=HERE, check=True
        )

    elf = BUILD / "audition.elf"
    sram_elf = BUILD / "audition-sram.elf"
    qspi_payload = BUILD / "audition-qspi.bin"
    factory_bin = BUILD / "factory-planar-f32.bin"
    prepare(
        ROOT / "host" / "vcv" / "res" / "factory.wav",
        factory_bin,
        BUILD / "factory_meta.h",
    )

    sections = parse_objdump_sections(_run_text([str(OBJDUMP), "-h", str(elf)]))
    symbols = parse_nm_symbols(_run_text([str(NM), "-n", str(elf)]))
    validate_layout(sections, symbols)

    subprocess.run(
        [
            str(OBJCOPY),
            "-O",
            "binary",
            "--only-section=.qspiflash_data",
            str(elf),
            str(qspi_payload),
        ],
        check=True,
    )
    subprocess.run(
        [
            str(OBJCOPY),
            "--remove-section=.qspiflash_data",
            str(elf),
            str(sram_elf),
        ],
        check=True,
    )
    if qspi_payload.stat().st_size != QSPI_SIZE:
        raise LayoutError("extracted QSPI payload has the wrong size")
    split_sections = parse_objdump_sections(
        _run_text([str(OBJDUMP), "-h", str(sram_elf)])
    )
    if ".qspiflash_data" in split_sections:
        raise LayoutError("SRAM ELF still contains loadable QSPI data")
    return (
        sram_elf,
        qspi_payload,
        factory_bin,
        symbols["g_audition_state"],
        symbols["g_factory_upload"],
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--build-only",
        action="store_true",
        help="build and validate artifacts without connecting to the Seed",
    )
    parser.add_argument("--interface", default="stlink-dap.cfg")
    args = parser.parse_args()

    sram, qspi, factory, state, upload = build()
    print(
        f"validated: SRAM ELF, read-only QSPI bank, "
        f"sample upload at 0x{upload:08x}"
    )
    if args.build_only:
        return 0

    config = HERE / "audition.cfg"
    validate_openocd_config(config.read_text(encoding="utf-8"))
    command = build_openocd_command(
        openocd=OPENOCD,
        scripts=SCRIPTS,
        interface=args.interface,
        config=config,
        sram_elf=sram,
        qspi_payload=qspi,
        factory_bin=factory,
        state_address=state,
        upload_address=upload,
    )
    subprocess.run(command, check=True)
    print("Seed audition is RUNNING (SRAM only; reset or power off to stop).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
