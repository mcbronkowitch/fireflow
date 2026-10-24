"""Writes the raw-spread readout switch as a real header.

Same shape and same reason as write_shell_panel_scan.py: a bare -D is
invisible to make's dependency graph, and an existing build/ would happily
reuse a stale panel_scan.o -- an image without the readout under a name that
promises one, or the reverse.

ALWAYS defines the symbol, including in position 0, because
`#if SHELL_RAW_SPREAD` has to work in both.

THE TIMESTAMP EDGE IS NOT ENOUGH (2026-08-23, see write_shell_wait_probe.py):
this script deletes the dependent objects itself whenever the content
changes. The objects come in as further arguments.

Why the switch exists: docs/hardware/reva-bring-up.md rows 5.3, 5.5 and 5.7
and fault R3 (pots at mid travel cross the hysteresis; the play image printed
no raw counts, so the size of the excursion was unmeasured).
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in {"0", "1"}:
        raise SystemExit(
            "usage: write_shell_raw_spread.py OUTPUT {0|1} [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    # The script runs while the Makefile is parsed, so before any rule has
    # created build/.
    output.parent.mkdir(parents=True, exist_ok=True)
    content = "#define SHELL_RAW_SPREAD %s\n" % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[3:]:
            Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
