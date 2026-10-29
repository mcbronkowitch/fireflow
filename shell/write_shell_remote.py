"""Writes the SHELL_REMOTE switch as a real header.

Same shape and same reason as write_shell_persist.py: a bare -D is invisible
to make's dependency graph, and an existing build/ would happily reuse a stale
main.o or panel_scan.o -- an image without the listener under a listening
name, which on Rev A (no SWD) is a board that needs Bastian's hands on BOOT
and RESET to leave, or the reverse.

ALWAYS defines the symbol, including in position 0, because
`#if SHELL_REMOTE` has to work in both.

THE TIMESTAMP EDGE IS NOT ENOUGH (2026-08-23, see write_shell_wait_probe.py):
this script runs while the Makefile is parsed and deletes the dependent
objects itself whenever the content changes. The objects come in as further
arguments.

Spec: ../docs/superpowers/specs/2026-10-29-cpu-hunt-remote-design.md
section 5.3.
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in {"0", "1"}:
        raise SystemExit(
            "usage: write_shell_remote.py OUTPUT {0|1} [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    # The script runs while the Makefile is parsed, so before any rule has
    # created build/.
    output.parent.mkdir(parents=True, exist_ok=True)
    content = "#define SHELL_REMOTE %s\n" % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[3:]:
            Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
