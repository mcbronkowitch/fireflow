"""Fail-closed inspection of the benchmark's requested ITCM placement."""

from pathlib import Path
import re
import subprocess


ITCM_START = 0x00000100
ITCM_END = 0x00010000
DTCM_START = 0x20000000
DTCM_END = 0x20020000

HOT_SYMBOL_FRAGMENTS = (
    "spky::Instrument::process(",
    "spky::Part::_control_tick()",
    "spky::PartFx::process(",
    "spky::Flux::process(",
    "spky::BbdLine::Process(",
    "spky::Grit::process(",
    "spky::AmbientReverb::process(",
    "spky::Comp::process(",
    "spky::VoiceT<spky::MorphOsc>::process(",
    "spky::SynthEngineT<spky::VoiceT<spky::MorphOsc> >::process(",
)
DTCM_STORAGE_FRAGMENT = "g_dtcm_instrument_storage"


class ItcmPlacementError(ValueError):
    pass


def _symbols(nm_text):
    parsed = []
    for line in nm_text.splitlines():
        match = re.match(
            r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+\S\s+(.+)$",
            line,
        )
        if match:
            parsed.append(
                (
                    int(match.group(1), 16),
                    int(match.group(2), 16),
                    match.group(3),
                )
            )
    return parsed


def _matching_symbols(symbols, fragment):
    return [
        (address, size, name)
        for address, size, name in symbols
        if fragment in name
    ]


def validate_itcm_outputs(nm_text, sections_text, program_headers_text):
    """Validate already-captured GNU inspection output.

    This parser is shared by the controller preflight and its regression
    tests. It returns the placement facts needed by callers and raises on
    every missing, empty, or out-of-range requirement.
    """
    symbols = _symbols(nm_text)
    if not symbols:
        raise ItcmPlacementError("nm produced no parseable linked symbols")

    for fragment in HOT_SYMBOL_FRAGMENTS:
        matches = _matching_symbols(symbols, fragment)
        if not matches:
            raise ItcmPlacementError(
                "representative ITCM symbol missing: %s" % fragment
            )
        outside = [
            (address, name)
            for address, _size, name in matches
            if not ITCM_START <= address < ITCM_END
        ]
        if outside:
            address, name = outside[0]
            raise ItcmPlacementError(
                "representative ITCM symbol is outside ITCM: %s at 0x%08x"
                % (name, address)
            )

    dtcm_matches = _matching_symbols(symbols, DTCM_STORAGE_FRAGMENT)
    if len(dtcm_matches) != 1:
        raise ItcmPlacementError(
            "expected exactly one DTCM instrument storage symbol, found %d"
            % len(dtcm_matches)
        )
    dtcm_address, dtcm_size, _dtcm_name = dtcm_matches[0]
    if (
        dtcm_size <= 0
        or dtcm_address < DTCM_START
        or dtcm_address + dtcm_size > DTCM_END
    ):
        raise ItcmPlacementError(
            "DTCM instrument storage is empty or outside DTCM: "
            "address 0x%08x, size 0x%x" % (dtcm_address, dtcm_size)
        )

    hot_section = re.search(
        r"^\s*\d+\s+\.itcm_audio_hot\s+"
        r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)",
        sections_text,
        re.MULTILINE,
    )
    if hot_section is None:
        raise ItcmPlacementError(".itcm_audio_hot section is missing")
    hot_size, hot_vma, hot_lma = (
        int(value, 16) for value in hot_section.groups()
    )
    if hot_size <= 0:
        raise ItcmPlacementError(".itcm_audio_hot section is empty")
    if hot_vma != ITCM_START or hot_lma != ITCM_START:
        raise ItcmPlacementError(
            ".itcm_audio_hot has wrong VMA/LMA: 0x%08x/0x%08x"
            % (hot_vma, hot_lma)
        )
    if hot_vma + hot_size > ITCM_END:
        raise ItcmPlacementError(
            ".itcm_audio_hot exceeds ITCM: end 0x%08x"
            % (hot_vma + hot_size)
        )

    load_pattern = re.compile(
        r"^\s*LOAD\s+(0x[0-9a-fA-F]+)\s+"
        r"(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+"
        r"(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+"
        r"(.+?)\s+(0x[0-9a-fA-F]+)\s*$",
        re.MULTILINE,
    )
    loads = list(load_pattern.finditer(program_headers_text))
    itcm_load_index = None
    for load_index, load in enumerate(loads):
        virtual_address = int(load.group(2), 16)
        physical_address = int(load.group(3), 16)
        file_size = int(load.group(4), 16)
        memory_size = int(load.group(5), 16)
        flags = load.group(6).replace(" ", "")
        if (
            virtual_address == ITCM_START
            and physical_address == ITCM_START
            and "E" in flags
            and file_size == hot_size
            and memory_size == hot_size
        ):
            itcm_load_index = load_index
            break
    if itcm_load_index is None:
        raise ItcmPlacementError(
            "ELF has no executable ITCM LOAD segment at 0x00000100 "
            "covering the complete hot section"
        )
    if not re.search(
        r"^[ \t]*%02d[ \t]+.*\.itcm_audio_hot\b" % itcm_load_index,
        program_headers_text,
        re.MULTILINE,
    ):
        raise ItcmPlacementError(
            ".itcm_audio_hot is not mapped to its ITCM LOAD segment"
        )

    return {
        "hot_size": hot_size,
        "hot_vma": hot_vma,
        "hot_lma": hot_lma,
        "dtcm_storage_address": dtcm_address,
        "dtcm_storage_size": dtcm_size,
    }


def _require_file(path, description):
    resolved = Path(path)
    if not resolved.is_file():
        raise ItcmPlacementError(
            "%s not found: %s" % (description, resolved)
        )
    return resolved


def _inspect(command, description):
    try:
        return subprocess.run(
            [str(part) for part in command],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        raise ItcmPlacementError(
            "%s failed: %s" % (description, error)
        ) from error


def validate_itcm_placement(elf, *, nm, objdump, readelf):
    """Inspect one linked ELF; missing tools or invalid output fail closed."""
    elf_path = _require_file(elf, "linked ELF")
    nm_path = _require_file(nm, "inspection tool nm")
    objdump_path = _require_file(objdump, "inspection tool objdump")
    readelf_path = _require_file(readelf, "inspection tool readelf")

    nm_text = _inspect(
        [nm_path, "--print-size", "--size-sort", "-C", elf_path],
        "nm inspection",
    )
    sections_text = _inspect(
        [objdump_path, "-h", elf_path],
        "objdump section inspection",
    )
    program_headers_text = _inspect(
        [readelf_path, "--wide", "--segments", elf_path],
        "readelf segment inspection",
    )
    return validate_itcm_outputs(
        nm_text,
        sections_text,
        program_headers_text,
    )
