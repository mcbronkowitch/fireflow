"""Writes the ITCM switch (SHELL_ITCM) as a real header.

Same shape and same reason as write_shell_pot_round.py: a bare -D is
invisible to make's dependency graph, and an existing build/ would happily
reuse a stale main.o or itcm_load.o -- an image that never copies its ITCM
code, or one that copies a section the link does not have.

ALWAYS defines the symbol, including in position 0, because
`#if SHELL_ITCM` has to work in both.

THE TIMESTAMP EDGE IS NOT ENOUGH (2026-08-23, see write_shell_wait_probe.py):
this script deletes the dependent objects itself whenever the content
changes. The objects come in as further arguments.

Spec: ../docs/superpowers/specs/2026-10-18-cpu-headroom-design.md section 4.2
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in {"0", "1"}:
        raise SystemExit(
            "usage: write_shell_itcm.py OUTPUT {0|1} [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    # The script runs while the Makefile is parsed, so before any rule has
    # created build/.
    output.parent.mkdir(parents=True, exist_ok=True)
    content = "#define SHELL_ITCM %s\n" % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[3:]:
            Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
