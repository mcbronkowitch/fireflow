"""Writes the SHELL_SD_PROBE switch (the SD use-case tests' probe mode) as a
real header.

Same shape and same reason as write_shell_sd.py: a bare -D is invisible to
make's dependency graph, and an existing build/ would happily reuse a stale
sd_probe.o or panel_scan.o -- an image that streams under the burst name, or
the other way round. A mode that changes code in two objects and is not
resolved here at parse time yields byte-identical images for two switch
positions (the stale-object trap, memory: fireflow-bench-stale-object-trap).

The header carries the mode twice: `SHELL_SD_PROBE_<NAME> 1` for `#if`s that
name a mode, and `SHELL_SD_PROBE <index>` (the position in MODES, `stream`
first) for the one place that has to print or compare it. The Makefile
allows any value but `stream` only with SHELL_SD=1.

This script runs while the Makefile is parsed and deletes the dependent
objects itself whenever the content changes; they come in as further
arguments.

Spec: ../docs/superpowers/specs/2026-10-31-sd-use-cases-tests-design.md section 5.
"""
import sys
from pathlib import Path

MODES = ("stream", "burst", "rtail", "wtail", "bank", "buffer", "duplex", "patch")


def run() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in MODES:
        raise SystemExit(
            "usage: write_shell_sd_probe.py OUTPUT {%s} [STALE_OBJECT...]"
            % "|".join(MODES))
    output = Path(sys.argv[1])
    # The script runs while the Makefile is parsed, so before any rule has
    # created build/.
    output.parent.mkdir(parents=True, exist_ok=True)
    name = sys.argv[2]
    content = "#define SHELL_SD_PROBE_%s 1\n#define SHELL_SD_PROBE %d\n" % (
        name.upper(), MODES.index(name))
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[3:]:
            Path(stale).unlink(missing_ok=True)
    return 0


def main() -> int:
    try:
        return run()
    except BaseException:
        # A refused or crashed run must not leave the previous build's header
        # in place: the Makefile ignores this script's exit status, and the
        # compile would read the stale value. With the file gone it fails on
        # the missing include instead.
        if len(sys.argv) > 1:
            Path(sys.argv[1]).unlink(missing_ok=True)
        raise


if __name__ == "__main__":
    raise SystemExit(main())
