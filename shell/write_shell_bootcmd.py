"""Writes the SHELL_BOOTCMD switch as a real header.

The value written is the EFFECTIVE one the Makefile resolves (SHELL_BOOTCMD_ON):
1 only for a play image (SHELL_PANEL_SCAN=1) without SHELL_REMOTE, whose full
listener already answers BOOT and PING. So `#if SHELL_BOOTCMD` in main.cpp and
panel_scan.cpp means "the boot-only listener is in this image", and every
image the switch does not reach compiles exactly as before it existed.

Same shape and same reason as write_shell_remote.py: a bare -D is invisible
to make's dependency graph, and an existing build/ would happily reuse a stale
main.o or panel_scan.o -- a play image without the listener under a build
that should have one, which on Rev A (no SWD) is a board that needs Bastian's
hands on BOOT and RESET to leave, or the reverse.

ALWAYS defines the symbol, including in position 0, because
`#if SHELL_BOOTCMD` has to work in both.

THE TIMESTAMP EDGE IS NOT ENOUGH (2026-08-23, see write_shell_wait_probe.py):
this script runs while the Makefile is parsed and deletes the dependent
objects itself whenever the content changes. The objects come in as further
arguments.
"""
import sys
from pathlib import Path


def run() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in {"0", "1"}:
        raise SystemExit(
            "usage: write_shell_bootcmd.py OUTPUT {0|1} [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    # The script runs while the Makefile is parsed, so before any rule has
    # created build/.
    output.parent.mkdir(parents=True, exist_ok=True)
    content = "#define SHELL_BOOTCMD %s\n" % sys.argv[2]
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
