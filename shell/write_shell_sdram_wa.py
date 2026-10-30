"""Writes the SDRAM write-allocate switch as a real header.

Same shape and same reason as write_shell_keys_nc.py: a bare -D is invisible
to make's dependency graph, and an existing build/ would happily reuse a stale
main.o -- an image with libDaisy's write-through-like SDRAM attributes under a
write-allocate name, or the reverse, and two switch positions that compare
byte-identical.

ALWAYS defines the symbol, including in position 0, because
`#if SHELL_SDRAM_WA` has to work in both.

THE TIMESTAMP EDGE IS NOT ENOUGH (2026-08-23, see write_shell_wait_probe.py):
this script runs while the Makefile is parsed and deletes the dependent
objects itself whenever the content changes. The objects come in as further
arguments.

Why the switch exists: libDaisy's MPU maps SDRAM write-back WITHOUT
write-allocate (TEX 0, C=1, B=1), so every store to a line that is not in the
D-cache bypasses it. Spec 2026-10-29-cpu-hunt-remote-design.md, FLUX probe
rounds.
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in {"0", "1"}:
        raise SystemExit(
            "usage: write_shell_sdram_wa.py OUTPUT {0|1} [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    # The script runs while the Makefile is parsed, so before any rule has
    # created build/.
    output.parent.mkdir(parents=True, exist_ok=True)
    content = "#define SHELL_SDRAM_WA %s\n" % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[3:]:
            Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
