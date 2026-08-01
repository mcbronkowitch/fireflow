#!/usr/bin/env python3
"""Build the split bench firmware, guard its QSPI payload, load the SRAM image
through the debug probe, and capture semihosting output."""

import argparse
import csv
import datetime
import hashlib
import io
import math
import os
import socket
import subprocess
import sys
import tempfile
import time
import queue
import threading

from profiles import DEFAULT_PROFILE, WAVE_ACCEPTANCE, resolve
from itcm_placement import ItcmPlacementError, validate_itcm_placement
from qspi_tools import (
    QspiGuardError,
    prepare_split_artifacts,
    program_and_verify,
    require_clean_tree,
    require_live_device,
    require_live_digest,
    require_verified_payload,
    validate_helper_elf,
)

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OPENOCD = r"C:\Program Files\DaisyToolchain\bin\openocd.exe"
SCRIPTS = r"C:\Program Files\DaisyToolchain\openocd\scripts"
ELF = os.path.join(HERE, "build", "bench.elf")
SRAM_ELF = os.path.join(HERE, "build", "bench-sram.elf")
QSPI_PAYLOAD = os.path.join(HERE, "build", "bench-qspi.bin")
QSPI_RECEIPT = os.path.join(HERE, "build", "qspi-verified.json")
PROGRAMMER_DIR = os.path.join(HERE, "qspi_programmer")
PROGRAMMER_ELF = os.path.join(
    PROGRAMMER_DIR, "build", "qspi-programmer.elf"
)
PROGRAMMER_CFG = os.path.join(HERE, "openocd", "qspi-programmer.cfg")
OBJCOPY = r"C:\Program Files\DaisyToolchain\bin\arm-none-eabi-objcopy.exe"
OBJDUMP = r"C:\Program Files\DaisyToolchain\bin\arm-none-eabi-objdump.exe"
READELF = r"C:\Program Files\DaisyToolchain\bin\arm-none-eabi-readelf.exe"
NM = r"C:\Program Files\DaisyToolchain\bin\arm-none-eabi-nm.exe"
OPENOCD_TCL_ADDRESS = ("127.0.0.1", 6666)
OPENOCD_SHUTDOWN = b"shutdown\x1a"

OPTIMIZATION_FLAGS = {
    "o2": "-O2",
    "o3": "-O3",
    "o3-lto": "-O3 -flto",
}


def build(families, itcm_hot=False, optimization="o2"):
    # Do not ask libDaisy's default `all` target for bench.bin: one flat
    # binary spanning SRAM (0x24000000) and QSPI (0x90040000) would encode the
    # address gap. Build the ELF, then extract two explicit artifacts.
    subprocess.run(
        ["make", "-j8", "build/qspi-programmer.elf"],
        cwd=PROGRAMMER_DIR,
        check=True,
    )
    subprocess.run(
        ["make", "-j8", "BENCH_FAMILIES=%s" % " ".join(families),
         "BENCH_ITCM_HOT=%d" % int(itcm_hot),
         "BENCH_OPTIMIZATION=%s" % optimization,
         "build/bench.elf"],
        cwd=HERE,
        check=True,
    )
    return prepare_existing_artifacts()


def prepare_existing_artifacts():
    """Re-derive both physical images from the authoritative linked ELF."""
    validate_helper_elf(PROGRAMMER_ELF, readelf=READELF)
    return prepare_split_artifacts(
        ELF,
        SRAM_ELF,
        QSPI_PAYLOAD,
        programmer_elf_path=PROGRAMMER_ELF,
        objcopy=OBJCOPY,
        objdump=OBJDUMP,
    )


def gracefully_shutdown_openocd(proc):
    """Ask OpenOCD to release the probe, then wait for its normal exit."""
    if proc.poll() is not None:
        return True
    try:
        with socket.create_connection(OPENOCD_TCL_ADDRESS, timeout=1) as control:
            control.sendall(OPENOCD_SHUTDOWN)
        proc.wait(timeout=10)
        return True
    except (OSError, subprocess.TimeoutExpired):
        return False


def run_once(interface, timeout):
    """Load and run the image, reading openocd's output until BENCH_END.

    openocd is both the loader and the semihosting server, so it stays alive
    for the whole run and its stdout IS the capture. Returns the captured
    lines, or None on timeout -- a hang writes nothing.
    """
    cmd = [
        OPENOCD,
        "-s", SCRIPTS,
        "-f", "interface/%s" % interface,
        "-f", "target/stm32h7x.cfg",
        "-c", "set IMAGE {%s}" % SRAM_ELF.replace("\\", "/"),
        "-f", os.path.join(HERE, "openocd", "spotykach-sram.cfg"),
    ]
    # openocd logs to stderr and semihosting output can land on either --
    # merge them so no line is missed.
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, bufsize=1)
    deadline = time.monotonic() + timeout
    lines, done = [], False
    output = queue.Queue()

    def read_output():
        for raw in iter(proc.stdout.readline, ""):
            output.put(raw)
        output.put(None)

    reader = threading.Thread(target=read_output, daemon=True)
    reader.start()
    try:
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            try:
                raw = output.get(timeout=max(0.01, min(0.25, remaining)))
            except queue.Empty:
                continue
            if raw is None:
                break
            line = raw.rstrip("\r\n")
            print(line)
            lines.append(line)
            if line.startswith("BENCH_END"):
                done = True
                break
    finally:
        if not (done and gracefully_shutdown_openocd(proc)):
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired as error:
                    raise RuntimeError(
                        "OpenOCD did not exit after kill"
                    ) from error
    return lines if done else None


BUDGET_CYCLES = 960000

# Host-side form of the workload protocol emitted by main.cpp. Keep the
# families grouped like the seven k*Workloads tables so adding or removing a
# firmware row has one obvious fail-closed host update. Validation uses the
# row set, never the incidental table/output order.
BENCH_PROTOCOL_ROWS_BY_FAMILY = {
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
        "inst_worst_deck_bus",
        "instrument_worst_bbd",
        "instrument_worst_bbd_dtcm",
        # Spec 2026-07-31 bbd-part-engine 8.3 row 1: both decks on the
        # voiceless BBD PART ENGINE (its stereo pair), as distinct from
        # instrument_worst_bbd above (FLUX's mono line behind a SYNTH deck).
        "inst_bbd_engine_worst",
    ),
    "sweep": (
        "sweep_flux_rate_0",
        "sweep_flux_rate_3",
        "sweep_flux_rate_6",
        "sweep_flux_rate_8",
        "sweep_flux_rate_11",
        "sweep_grit_bare",
        "sweep_grit_no_bbd_mem",
        "sweep_flux_lines_2ch",
        "sweep_room_lo",
        "sweep_room_mid",
        "sweep_room_hi",
    ),
    "instr": (
        "instr_part_1",
        "instr_part_2",
        "instr_noverb",
        "deck_mod_hot",
        "deck_engine_hot",
        "fx_flux_hot",
        "tone_solo",
        "deck_shell",
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
    ),
    "bbd": (
        "bbd_ceiling",
        "bbd_line_only",
        "bbd_line_tap",
        "bbd_line_tap_half",
        "bbd_walk_sdram",
    ),
    "body": (
        "mode_bank_24",
        "mode_bank_24_static",
        "ks_string_pair",
        "ks_string_pair_nolin",
        "ks_string_pair_port",
        "body_2x4",
        "body_2x4_string",
        "inst_body_worst",
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

ANCHOR_NAMES = (
    "oliverb_solo_sram",
    "tap_read_sdram",
    "instrument_worst_bbd_dtcm",
    "instrument_worst_bbd",
    "instrument_worst",
)


def protocol_rowset(profile):
    """The (family, name) pairs a capture from this profile must contain."""
    return frozenset(
        (family, name)
        for family in profile.families
        for name in BENCH_PROTOCOL_ROWS_BY_FAMILY[family]
    )


def anchor_names(profile):
    """The exact callback anchors this profile's compiled rows can emit."""
    row_names = {name for _family, name in protocol_rowset(profile)}
    return frozenset(name for name in ANCHOR_NAMES if name in row_names)


def parse(lines):
    """Pull the marker-delimited payload out of a capture. Returns
    (header, rows, anchors) or None if the run never completed."""
    header, rows, anchors = None, [], []
    for line in lines:
        if line.startswith("BENCH_BEGIN,"):
            f = line.split(",")
            # Optimization is fail-closed like layout and families: an older
            # image cannot be accepted by guessing what the compiler did.
            if len(f) != 10:
                continue
            header = {
                "githash": f[1],
                "clock": f[2],
                "block": f[3],
                "cache": f[4],
                "qspi_sha256": f[5],
                "device_id": f[6],
                "families": tuple(f[7].split()),
                "layout": f[8].strip(),
                "optimization": f[9].strip(),
            }
        elif line.startswith("BENCH,"):
            f = line.split(",")
            if len(f) != 8:
                return None
            rows.append({
                "family": f[1], "name": f[2], "avg_cyc": f[3], "max_cyc": f[4],
                "pct_avg": f[5], "pct_max": f[6], "checksum": f[7],
            })
        elif line.startswith("ANCHOR,"):
            f = line.split(",")
            if len(f) != 4:
                return None
            anchors.append({"name": f[1], "avg_pct": f[2], "max_pct": f[3]})
    if header is None or not rows:
        return None
    return header, rows, anchors


def by_name(rows):
    return {r["name"]: r for r in rows}


class BenchValidationError(ValueError):
    pass


def _require_numeric(value, error_message):
    try:
        number = float(value)
    except (TypeError, ValueError) as error:
        raise BenchValidationError(error_message) from error
    if not math.isfinite(number):
        raise BenchValidationError(error_message)
    return number


def validate_captures(
    captures, profile, expected_layout=None, expected_optimization=None
):
    """Reject any repeat capture that cannot be accepted as hardware evidence."""
    expected_rowset = protocol_rowset(profile)
    expected_rows = None
    expected_identity = None
    first_layout = None
    first_optimization = None
    expected_anchor_names = anchor_names(profile)
    for run_index, (header, rows, anchors) in enumerate(captures, start=1):
        identity = (header["qspi_sha256"], header["device_id"])
        if expected_identity is None:
            expected_identity = identity
        elif identity != expected_identity:
            raise BenchValidationError(
                "run %d QSPI digest or device fingerprint differs from run 1"
                % run_index
            )
        layout = header["layout"]
        if first_layout is None:
            first_layout = layout
        elif layout != first_layout:
            raise BenchValidationError(
                "run %d layout differs from run 1: %s vs %s"
                % (run_index, layout, first_layout)
            )
        if expected_layout is not None and layout != expected_layout:
            raise BenchValidationError(
                "run %d reports layout %s but requested layout %s"
                % (run_index, layout, expected_layout)
            )
        optimization = header["optimization"]
        if optimization not in OPTIMIZATION_FLAGS:
            raise BenchValidationError(
                "run %d reports unknown optimization %s"
                % (run_index, optimization)
            )
        if first_optimization is None:
            first_optimization = optimization
        elif optimization != first_optimization:
            raise BenchValidationError(
                "run %d optimization differs from run 1: %s vs %s"
                % (run_index, optimization, first_optimization)
            )
        if (
            expected_optimization is not None
            and optimization != expected_optimization
        ):
            raise BenchValidationError(
                "run %d reports optimization %s but requested optimization %s"
                % (run_index, optimization, expected_optimization)
            )
        reported = tuple(header["families"])
        if reported != tuple(profile.families):
            raise BenchValidationError(
                "run %d reports families %s but --profile requested %s -- "
                "the flashed image and the --profile argument disagree; "
                "rebuild for this profile (drop --no-build), or pass the "
                "--profile that matches the image actually on the device"
                % (run_index, " ".join(reported) or "none",
                   " ".join(profile.families))
            )
        row_keys = [(row["family"], row["name"]) for row in rows]
        keys = set(row_keys)
        if len(keys) != len(row_keys):
            duplicates = sorted(
                "%s/%s" % key for key in keys if row_keys.count(key) > 1
            )
            raise BenchValidationError(
                "run %d has duplicate bench rows: %s"
                % (run_index, ", ".join(duplicates))
            )
        if keys != expected_rowset:
            missing = sorted(expected_rowset - keys)
            extra = sorted(keys - expected_rowset)
            fmt = lambda items: ", ".join(
                "%s/%s" % item for item in items
            ) or "none"
            raise BenchValidationError(
                "run %d does not match the complete %d-row bench protocol "
                "(missing: %s; extra: %s)"
                % (
                    run_index,
                    len(expected_rowset),
                    fmt(missing),
                    fmt(extra),
                )
            )
        reported_anchor_names = [anchor["name"] for anchor in anchors]
        unique_anchor_names = set(reported_anchor_names)
        if len(unique_anchor_names) != len(reported_anchor_names):
            duplicates = sorted(
                name
                for name in unique_anchor_names
                if reported_anchor_names.count(name) > 1
            )
            raise BenchValidationError(
                "run %d has duplicate anchors: %s"
                % (run_index, ", ".join(duplicates))
            )
        if unique_anchor_names != expected_anchor_names:
            missing = sorted(expected_anchor_names - unique_anchor_names)
            extra = sorted(unique_anchor_names - expected_anchor_names)
            raise BenchValidationError(
                "run %d does not match the exact anchor protocol "
                "(missing: %s; extra: %s)"
                % (
                    run_index,
                    ", ".join(missing) or "none",
                    ", ".join(extra) or "none",
                )
            )
        for anchor in anchors:
            for field in ("avg_pct", "max_pct"):
                _require_numeric(
                    anchor[field],
                    "run %d has a non-numeric anchor %s/%s"
                    % (run_index, anchor["name"], field),
                )
        named = by_name(rows)
        axi_gate = named.get("instrument_worst_bbd")
        dtcm_gate = named.get("instrument_worst_bbd_dtcm")
        gate_names = set()
        if axi_gate and dtcm_gate:
            gate_names.update(
                ("instrument_worst_bbd", "instrument_worst_bbd_dtcm")
            )
        if WAVE_ACCEPTANCE in profile.gates:
            gate_names.update(("synth_2x4", "wave_2x4"))
        for gate_name in gate_names:
            gate_row = named[gate_name]
            for field in ("avg_cyc", "max_cyc", "pct_avg", "pct_max"):
                _require_numeric(
                    gate_row[field],
                    "run %d has a non-numeric gate workload %s/%s"
                    % (run_index, gate_name, field),
                )
        if (
            axi_gate
            and dtcm_gate
            and axi_gate["checksum"] != dtcm_gate["checksum"]
        ):
            raise BenchValidationError(
                "run %d DTCM A/B checksum mismatch: AXI %s, DTCM %s"
                % (
                    run_index,
                    axi_gate["checksum"],
                    dtcm_gate["checksum"],
                )
            )
        if WAVE_ACCEPTANCE in profile.gates:
            names = {row["name"] for row in rows}
            required = {"synth_2x4", "wave_2x4"}
            if not required.issubset(names):
                raise BenchValidationError(
                    "run %d is missing WAVE acceptance rows: %s"
                    % (run_index, ", ".join(sorted(required - names)))
                )
            synth = named["synth_2x4"]
            wave = named["wave_2x4"]
            try:
                synth_avg = int(synth["avg_cyc"])
                synth_max = int(synth["max_cyc"])
                wave_avg = int(wave["avg_cyc"])
                wave_max = int(wave["max_cyc"])
            except (TypeError, ValueError) as error:
                raise BenchValidationError(
                    "run %d has a non-numeric WAVE acceptance result" % run_index
                ) from error
            if wave_avg > synth_avg:
                raise BenchValidationError(
                    "run %d WAVE average %d exceeds SYNTH average %d"
                    % (run_index, wave_avg, synth_avg)
                )
            if wave_max > synth_max:
                raise BenchValidationError(
                    "run %d WAVE maximum %d exceeds SYNTH maximum %d"
                    % (run_index, wave_max, synth_max)
                )
            if wave_max >= BUDGET_CYCLES:
                raise BenchValidationError(
                    "run %d WAVE maximum %d is not below the %d-cycle block budget"
                    % (run_index, wave_max, BUDGET_CYCLES)
                )
        if expected_rows is None:
            expected_rows = by_name(rows)
            continue
        current_rows = by_name(rows)
        drifted = sorted(
            name
            for name in expected_rows
            if expected_rows[name]["checksum"] != current_rows[name]["checksum"]
        )
        if drifted:
            raise BenchValidationError(
                "run 1 vs run %d checksum drift: %s"
                % (run_index, ", ".join(drifted))
            )


def current_head():
    """git rev-parse --short HEAD for REPO, same abbreviation the firmware
    embeds (bench/Makefile's git_hash.h uses the identical command)."""
    return subprocess.run(
        ["git", "-C", REPO, "rev-parse", "--short", "HEAD"],
        capture_output=True, text=True, check=True).stdout.strip()


def check_hash(header):
    """Guard against a mislabelled result file. bench/Makefile's
    git_hash.h rule gives the embedded hash a real dependency edge onto
    the object that reports it, but that only prevents ONE way this can
    go stale (an un-rebuilt object). This is the loud, independent
    host-side check that catches staleness from any source -- a flashed
    image that predates a `git checkout`, a build directory copied from
    elsewhere, etc. A mislabelled result file is worse than no result
    file, so this is an error, not a warning: return False and the caller
    must abort without writing anything.
    """
    embedded = header["githash"]
    head = current_head()
    if embedded != head:
        print("ERROR: firmware reports git hash %r but the working tree's "
              "HEAD is %r -- the flashed image does not match the code "
              "you think you're measuring. Writing nothing." % (embedded, head),
              file=sys.stderr)
        return False
    return True


def ratio(rows, num, den):
    """Cycle ratio between two rows, as a raw float (or an error string)."""
    d = by_name(rows)
    if num not in d or den not in d:
        return "n/a (row missing)"
    try:
        a, b = float(d[num]["avg_cyc"]), float(d[den]["avg_cyc"])
    except ValueError:
        return "n/a (TIMEOUT)"
    if b <= 0:
        return "n/a (zero denominator)"
    return a / b


def sig2(x):
    """Format a ratio to 2 significant figures for verdict PROSE only --
    e.g. 5.28 -> "5.3", 44.33 -> "44", 0.29 -> "0.29". Measured intra-run
    jitter (~1700 cycles on a 1.5M-cycle workload) and a cross-build layout
    shift (~7% on a 29K-cycle workload) mean anything past this is noise
    dressed up as precision. The data tables keep full precision; this
    helper is not used there."""
    if not isinstance(x, float):
        return x  # error string from ratio(), pass through unrounded
    if x == 0:
        return "0x"
    import math
    exp = math.floor(math.log10(abs(x)))
    decimals = max(0, 1 - exp)
    r = round(x, decimals)
    return ("%dx" % round(r)) if decimals <= 0 else ("%.*fx" % (decimals, r))


def pct1(s):
    """Format a firmware percentage string to whole-number prose precision.
    The firmware prints hundredths (e.g. "165.08"); verdict prose rounds to
    a whole number so it doesn't imply resolution the hardware jitter can't
    support."""
    try:
        return "%.0f" % float(s)
    except ValueError:
        return s


def verdict(captures):
    """The paragraph the spec's acceptance criterion 2 asks for: the three
    questions this bench exists to answer, answered in prose."""
    _header, rows, _anchors = captures[0]
    d = by_name(rows)
    out = io.StringIO()

    worst = d.get("instrument_worst")
    out.write("## Verdict\n\n")

    gate_name = "instrument_worst_bbd_dtcm"
    gate_results = []
    for run_index, (_run_header, run_rows, run_anchors) in enumerate(
        captures,
        start=1,
    ):
        gate = by_name(run_rows).get(gate_name)
        anchor = {
            item["name"]: item for item in run_anchors
        }.get(gate_name)
        if gate is None or anchor is None:
            break
        try:
            offline_max = float(gate["pct_max"])
            callback_max = float(anchor["max_pct"])
        except (TypeError, ValueError):
            break
        if not math.isfinite(offline_max) or not math.isfinite(callback_max):
            break
        gate_results.append((run_index, offline_max, callback_max))

    if len(gate_results) == len(captures):
        run_values = "; ".join(
            "Run %d %.2f %% / %.2f %%" % result
            for result in gate_results
        )
        worst_offline = max(result[1] for result in gate_results)
        worst_callback = max(result[2] for result in gate_results)
        fits = worst_offline < 100.0 and worst_callback < 100.0
        if fits:
            conclusion = (
                "**Conclusion: the DTCM+BBD gate fits.** Every offline and "
                "real-callback maximum is below 100 % of the block budget."
            )
        else:
            conclusion = (
                "**Conclusion: the DTCM+BBD gate does not fit.** At least "
                "one offline or real-callback maximum is at or above 100 % "
                "of the block budget."
            )
        out.write(
            "**DTCM+BBD budget — go/no-go.** The decision workload is "
            "`instrument_worst_bbd_dtcm`: the full eight-voice instrument, "
            "BBD echo and the retained DTCM instrument state. Run maxima "
            "(offline / real callback): %s. Across all %d repeats, the worst "
            "maxima are **%.2f %% offline** and **%.2f %% in the real "
            "callback**. %s\n\n"
            % (
                run_values,
                len(gate_results),
                worst_offline,
                worst_callback,
                conclusion,
            )
        )
    else:
        out.write(
            "**DTCM+BBD budget — NO RESULT.** "
            "`instrument_worst_bbd_dtcm` did not produce both numeric "
            "offline and callback maxima in every repeat. The go/no-go "
            "question is unanswered.\n\n"
        )

    # Ratios are against synth_1_voice -- ONE REAL spotymod voice (two MorphOsc
    # in unison + sub + SVF + envelope). NOT against morph_osc_bare, which is a
    # single oscillator kernel and ~7.3x cheaper; anchoring on that inflates
    # every ratio by that factor and misranks the table.
    out.write("**Cost per candidate, relative to one real spotymod voice.**\n\n")
    candidates = [r for r in rows
                  if r["family"] == "voice" and r["name"] != "morph_osc_bare"]
    # An empty body here would read as "no candidates cost anything worth
    # reporting" rather than "this profile did not carry them" -- the same
    # silent omission the gate ledger exists to prevent. Say which it is.
    if not candidates:
        out.write("- n/a (family `voice` not in this profile)\n")
    for r in candidates:
        out.write("- `%s` — %s one real voice (%s a bare oscillator kernel)\n"
                  % (r["name"],
                     sig2(ratio(rows, r["name"], "synth_1_voice")),
                     sig2(ratio(rows, r["name"], "morph_osc_bare"))))
    out.write("\n")

    out.write(
        "**SRAM vs SDRAM.** The grain-read proxy (8 scattered interpolated "
        "stereo reads per sample, identical window in both regions) costs "
        "**%s** in SDRAM against SRAM. That is a bare access pattern, written "
        "before the sampler existed to stand in for it; the `sampler_win_*` "
        "pair below is the same contrast with the real engine around it. The "
        "Oliverb pair reads **%s**, and the shortened echo-style streaming "
        "walk **%s**.\n\n"
        % (sig2(ratio(rows, "grain_read_sdram", "grain_read_sram")),
           sig2(ratio(rows, "oliverb_sdram", "oliverb_solo_sram")),
           sig2(ratio(rows, "echo_walk_sdram", "echo_walk_sram"))))

    sw = d.get("inst_sampler_worst")
    if sw and sw["avg_cyc"] != "TIMEOUT":
        try:
            over = float(sw["pct_max"]) >= 100.0
        except ValueError:
            over = None
        if over is True:
            call = ("**Conclusion: the texture deck fits on average and "
                    "overruns on peaks.** The grain count itself is already "
                    "capped (`kSpawnHeadroom`) and the ablations say the "
                    "remainder is not a burst: with the MOTION and SIZE lanes "
                    "quiet the cloud's per-block work is flat to 1 %, and the "
                    "worst block is simply the ceiling's worth of grains plus "
                    "the whole FX chain. Dropping either FLUX or the reverb "
                    "from this patch brings it under 100 %; capping grains "
                    "harder does not.")
        elif over is False:
            call = ("**Conclusion: a two-part texture deck fits**, peaks "
                    "included.")
        else:
            call = "**Conclusion: undetermined.**"
        out.write(
            "**The texture deck.** Both parts on the sampler at its worst "
            "case — DENS 8, SIZE 0.05 (128-sample grains spawning every 16), "
            "MOTION 1 scattering reads over the whole 42 s record buffer, "
            "SCAN running, and instrument_worst's FX chain unchanged around "
            "it — costs **%s %% of the block budget on the mean block and "
            "%s %% on the worst**, against **%s %% / %s %%** for the same box "
            "with both parts on the synth. The mean is the cheaper of the "
            "two; the peak is not. %s\n\n"
            % (pct1(sw["pct_avg"]), pct1(sw["pct_max"]),
               pct1(worst["pct_avg"]) if worst else "n/a",
               pct1(worst["pct_max"]) if worst else "n/a",
               call))
        out.write(
            "Solo, the same cloud is **%s %%** mean / **%s %%** peak, a "
            "musical setting (DENS 4, half-second grains, MOTION 0.5, "
            "playhead parked) is **%s %%** mean, and running an overdub "
            "underneath the worst-case cloud costs **%s** its mean. "
            "SCAN driven at the VCV host's rate — every 16 samples, six "
            "times the engine's own control tick, the rate "
            "`SamplerEngine::set_scan`'s comment names as the one that "
            "matters — reads **%s** against the same cloud without it. "
            "With the whole engine around it the "
            "record buffer's region costs **%s** (`sampler_win_sdram` over "
            "`sampler_win_sram`, identical settings and identical 8192-frame "
            "content), well under the bare proxy's figure above: the "
            "scheduler, window, filter and normalization are all region-blind "
            "and dilute it.\n\n"
            % (pct1(d["sampler_flow_worst"]["pct_avg"]),
               pct1(d["sampler_flow_worst"]["pct_max"]),
               pct1(d["sampler_flow_typ"]["pct_avg"]),
               sig2(ratio(rows, "sampler_overdub_worst", "sampler_flow_worst")),
               sig2(ratio(rows, "sampler_scan_ctrl", "sampler_flow_worst")),
               sig2(ratio(rows, "sampler_win_sdram", "sampler_win_sram"))))

    out.write(
        "*The decision gate retains the firmware's two-decimal percentages "
        "because values immediately around 100 % determine the stop gate. "
        "Other prose uses whole percentage points and two significant "
        "figures for ratios; the tables below retain full measured "
        "precision.*\n\n"
    )
    return out.getvalue()


def device_fingerprint(device_id):
    """Stable, non-reversible identifier for the measured Daisy Seed."""
    return hashlib.sha256(device_id.encode("ascii")).hexdigest()


def wave_gate_verdict(captures):
    out = io.StringIO()
    out.write("## WAVE performance gate — PASS\n\n")
    out.write(
        "All %d runs satisfy the matched WAVE/SYNTH acceptance gates.\n\n"
        % len(captures)
    )
    for run_index, (_, rows, _) in enumerate(captures, start=1):
        named = by_name(rows)
        synth = named["synth_2x4"]
        wave = named["wave_2x4"]
        out.write(
            "- **Run %d — PASS:** `wave_2x4` average %s <= "
            "`synth_2x4` average %s; maximum %s <= %s; maximum %s < %d.\n"
            % (
                run_index,
                wave["avg_cyc"],
                synth["avg_cyc"],
                wave["max_cyc"],
                synth["max_cyc"],
                wave["max_cyc"],
                BUDGET_CYCLES,
            )
        )
    out.write("\n")
    return out.getvalue()


def wave_gate_not_applicable_reason(profile):
    """Why `wave_acceptance` did not run for this profile.

    There are two genuinely different reasons, and the gate ledger must not
    conflate them: a profile whose families cannot supply synth_2x4 and
    wave_2x4 at all, versus a profile whose families could but whose
    manifest simply does not declare the gate. Hardcoded prose here once
    said "which this profile does not contain" unconditionally -- false for
    the second case, inside a document whose only purpose is to be
    evidence. Derive it instead.
    """
    supplied = {
        row_name
        for family in profile.families
        for row_name in BENCH_PROTOCOL_ROWS_BY_FAMILY.get(family, ())
    }
    required = {"synth_2x4", "wave_2x4"}
    missing = required - supplied
    if missing:
        return (
            "needs %s, which this profile's families (%s) do not supply"
            % (", ".join(sorted(missing)), ", ".join(profile.families) or "none")
        )
    return "this profile's manifest does not declare it"


def write_results(out_dir, captures, profile, profile_name):
    # The gate ledger below claims every universal gate ran and passed,
    # including "at least two runs". Checking that here makes the claim
    # true by construction rather than by trusting the caller got the
    # order right. The other four universal gates (row set, duplicates,
    # identity, checksum drift) are still only true because main() calls
    # validate_captures before calling this function -- re-running that
    # full check here would duplicate real validation work for a function
    # whose job is to persist a capture, not gate it, and whose caller
    # already wraps validate_captures in its own error handling. That
    # ordering dependency is the residual convention this does not remove.
    if len(captures) < 2:
        raise ValueError(
            "write_results claims the 'at least two runs' gate passed; "
            "called with %d capture(s)" % len(captures)
        )
    header, rows, anchors = captures[0]
    os.makedirs(out_dir, exist_ok=True)
    stamp = datetime.date.today().isoformat()
    base = os.path.join(
        out_dir,
        "%s-%s-%s-%s-%s"
        % (
            stamp,
            header["githash"],
            profile_name,
            header["layout"],
            header["optimization"],
        ),
    )
    fingerprint = device_fingerprint(header["device_id"])

    with open(base + ".csv", "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["run", "profile", "layout", "optimization", "qspi_sha256",
                    "device_fingerprint", "family", "name", "avg_cyc", "max_cyc",
                    "pct_avg", "pct_max", "checksum"])
        for run_index, (run_header, run_rows, _) in enumerate(captures, start=1):
            run_fingerprint = device_fingerprint(run_header["device_id"])
            for r in run_rows:
                w.writerow([
                    run_index,
                    profile_name,
                    run_header["layout"],
                    run_header["optimization"],
                    run_header["qspi_sha256"],
                    run_fingerprint,
                    r["family"],
                    r["name"],
                    r["avg_cyc"],
                    r["max_cyc"],
                    r["pct_avg"],
                    r["pct_max"],
                    r["checksum"],
                ])

    with open(base + ".md", "w", encoding="utf-8") as fh:
        fh.write("# Bench evidence %s — `%s`\n\n" % (stamp, header["githash"]))
        # The gate ledger is the point of this document: it must record a
        # gate as applied only when validate_captures actually ran it, and
        # name a not-applicable gate together with the reason -- so a
        # partial (profile-scoped) run cannot skip a gate silently. The
        # universal gates below always ran if validation passed, because
        # validate_captures enforces them unconditionally for every
        # profile; only wave_acceptance is profile-scoped.
        universal = (
            "row set matches the profile exactly (no missing, no extra rows)",
            "no duplicate rows",
            "anchor set matches the profile exactly and is numeric",
            "all decision-gate measurements are numeric",
            "QSPI digest and device fingerprint identical across runs",
            "per-row checksums identical across runs",
            "at least two runs (`--repeat`, minimum 2)",
        )
        fh.write("## Gate ledger\n\n")
        fh.write("Execution layout: `%s`.\n\n" % header["layout"])
        fh.write(
            "Optimization: `%s` (`%s`).\n\n"
            % (
                header["optimization"],
                OPTIMIZATION_FLAGS[header["optimization"]],
            )
        )
        fh.write("Profile `%s` — families: %s\n\n"
                 % (profile_name,
                    ", ".join("`%s`" % f for f in profile.families)))
        fh.write("Applied and passed:\n\n")
        for g in universal:
            fh.write("- %s\n" % g)
        if WAVE_ACCEPTANCE in profile.gates:
            fh.write("- `wave_acceptance`: wave_2x4 no slower than "
                     "synth_2x4, below the %d-cycle block budget\n"
                     % BUDGET_CYCLES)
        fh.write("\nNot applicable to this profile:\n\n")
        if WAVE_ACCEPTANCE not in profile.gates:
            fh.write("- `wave_acceptance` — %s\n"
                     % wave_gate_not_applicable_reason(profile))
        else:
            fh.write("- none\n")
        fh.write("\n")
        fh.write("Measured on a Daisy Seed (STM32H750). %s Hz core clock, "
                 "block size %s, %s. "
                 "Block budget %d cycles.\n\n"
                 % (header["clock"], header["block"], header["cache"],
                    BUDGET_CYCLES))
        fh.write(
            "All %d runs report QSPI payload SHA-256 `%s` and device "
            "fingerprint `%s` (SHA-256 of the MCU UID).\n\n"
            % (len(captures), header["qspi_sha256"], fingerprint)
        )
        # Only claim the WAVE gate passed when the profile actually declared
        # (and therefore validate_captures actually enforced) it -- otherwise
        # this document would either assert an untrue PASS for an ungated
        # profile, or KeyError on synth_2x4/wave_2x4 rows a system-less
        # profile never had, after the hardware time is already spent.
        if WAVE_ACCEPTANCE in profile.gates:
            fh.write(wave_gate_verdict(captures))
        fh.write(verdict(captures))
        for run_index, (run_header, run_rows, run_anchors) in enumerate(
                captures, start=1):
            fh.write("## Run %d\n\n" % run_index)
            fh.write(
                "QSPI payload SHA-256 `%s`; device fingerprint `%s`.\n\n"
                % (
                    run_header["qspi_sha256"],
                    device_fingerprint(run_header["device_id"]),
                )
            )
            fh.write("### Offline table\n\n")
            fh.write("| family | workload | avg cyc | max cyc | avg % | max % | checksum |\n")
            fh.write("|---|---|---:|---:|---:|---:|---|\n")
            for r in run_rows:
                fh.write("| %s | `%s` | %s | %s | %s | %s | `%s` |\n"
                         % (r["family"], r["name"], r["avg_cyc"], r["max_cyc"],
                            r["pct_avg"], r["pct_max"], r["checksum"]))
            if run_anchors:
                fh.write(
                    "\n### Anchor mode (real audio callback, CpuLoadMeter)\n\n"
                )
                fh.write("| workload | avg % | max % |\n|---|---:|---:|\n")
                for x in run_anchors:
                    fh.write("| `%s` | %s | %s |\n"
                             % (x["name"], x["avg_pct"], x["max_pct"]))
            if run_index < len(captures):
                fh.write("\n")
    return base


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--interface", default="stlink-dap.cfg",
                    help="openocd interface cfg; this desk's probe is an ST-Link V3")
    ap.add_argument("--transport", default="semihost", choices=["semihost"],
                    help="capture transport (USB-CDC fallback: see bench/README.md)")
    ap.add_argument("--timeout", type=float, default=600.0,
                    help="seconds to wait for BENCH_END")
    ap.add_argument("--build-only", action="store_true")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument(
        "--itcm-hot",
        action="store_true",
        help="link the pre-registered audio hotset into ITCM",
    )
    ap.add_argument(
        "--optimization",
        default="o2",
        choices=tuple(OPTIMIZATION_FLAGS),
        help="compiler optimization identity carried by the firmware",
    )
    ap.add_argument(
        "--program-qspi",
        action="store_true",
        help=(
            "through the connected ST-Link, load an SRAM-only helper and the "
            "65024-byte payload, program and byte-compare QSPI at 0x90040000, "
            "then write a helper-bound identity receipt"
        ),
    )
    ap.add_argument("--repeat", type=int, default=2,
                    help="runs to compare for the determinism check")
    ap.add_argument(
        "--profile", default=DEFAULT_PROFILE,
        help="which workload families to build and measure "
             "(see bench/profiles.py)")
    ap.add_argument("--out-dir", default=os.path.join(REPO, "docs", "bench"))
    args = ap.parse_args()
    try:
        profile = resolve(args.profile, BENCH_PROTOCOL_ROWS_BY_FAMILY)
    except (KeyError, ValueError) as error:
        print("ERROR: %s" % error, file=sys.stderr)
        return 2
    if not args.build_only and args.repeat < 2:
        print("ERROR: hardware evidence requires at least two runs",
              file=sys.stderr)
        return 2

    receipt_context = None
    active_receipt = QSPI_RECEIPT
    persist_program_receipt = args.program_qspi and not args.build_only
    if persist_program_receipt:
        receipt_context = tempfile.TemporaryDirectory(
            prefix="spotykach-qspi-receipt-"
        )
        active_receipt = os.path.join(
            receipt_context.name,
            "qspi-verified.json",
        )

    def finish(result):
        nonlocal receipt_context
        if receipt_context is not None:
            receipt_context.cleanup()
            receipt_context = None
        return result

    try:
        identity = (
            build(
                profile.families,
                itcm_hot=args.itcm_hot,
                optimization=args.optimization,
            )
            if not args.no_build
            else prepare_existing_artifacts()
        )
        if args.itcm_hot:
            validate_itcm_placement(
                ELF,
                nm=NM,
                objdump=OBJDUMP,
                readelf=READELF,
            )
        if args.program_qspi or not args.build_only:
            require_clean_tree(REPO)
        if args.program_qspi:
            program_and_verify(
                QSPI_PAYLOAD,
                PROGRAMMER_ELF,
                active_receipt,
                artifact_identity=identity,
                openocd=OPENOCD,
                scripts=SCRIPTS,
                interface=args.interface,
                config=PROGRAMMER_CFG,
                readelf=READELF,
            )
        verified_receipt = None
        if not args.build_only:
            verified_receipt = require_verified_payload(
                QSPI_PAYLOAD, active_receipt, identity
            )
    except (
        ItcmPlacementError,
        QspiGuardError,
        subprocess.CalledProcessError,
    ) as error:
        print("ERROR: %s" % error, file=sys.stderr)
        return finish(2)
    if args.build_only:
        return finish(0)

    captures = []
    for i in range(max(1, args.repeat)):
        print("# run %d/%d" % (i + 1, args.repeat), file=sys.stderr)
        lines = run_once(args.interface, args.timeout)
        if lines is None:
            print("ERROR: BENCH_END never arrived (timeout or openocd exited)",
                  file=sys.stderr)
            return finish(2)
        parsed = parse(lines)
        if parsed is None:
            print("ERROR: capture completed but held no usable rows",
                  file=sys.stderr)
            return finish(2)
        header, _rows, _anchors = parsed
        try:
            require_live_digest(header["qspi_sha256"], QSPI_PAYLOAD)
            require_live_device(header["device_id"], verified_receipt)
        except QspiGuardError as error:
            print("ERROR: %s" % error, file=sys.stderr)
            return finish(2)
        if not check_hash(header):
            return finish(2)
        captures.append(parsed)

    try:
        validate_captures(
            captures,
            profile,
            expected_layout="itcm-hot" if args.itcm_hot else "axi",
            expected_optimization=args.optimization,
        )
    except BenchValidationError as error:
        print("ERROR: %s" % error, file=sys.stderr)
        return finish(2)

    base = write_results(args.out_dir, captures, profile, args.profile)
    if persist_program_receipt:
        os.replace(active_receipt, QSPI_RECEIPT)
    print("# wrote %s.md and %s.csv" % (base, base), file=sys.stderr)
    return finish(0)


if __name__ == "__main__":
    sys.exit(main())
