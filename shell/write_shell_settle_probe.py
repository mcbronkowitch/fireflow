"""Writes the settle-probe switch as a real header.

Same shape and same reason as write_shell_cpu_probe.py: a bare -D is
invisible to make's dependency graph, and an existing build/ would happily
reuse a stale main.o -- in the worst case shipping a measurement of the
wrong board under the right name.

Like write_shell_coupon_probe.py this ALWAYS defines the symbol, including
in position 0, because `#if SHELL_SETTLE_PROBE` has to work in both.

THE TIMESTAMP EDGE IS NOT ENOUGH; that happened on this machine on
2026-08-23, when a header written 0.36 s after main.o landed in the same
wall-clock second and make judged it "not newer". So this script deletes the
dependent objects itself whenever the content changes, which hangs on no
timer resolution. The objects come in as further arguments.
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in {"0", "1"}:
        raise SystemExit(
            "usage: write_shell_settle_probe.py OUTPUT {0|1} [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    # The script runs while the Makefile is parsed, so before any rule has
    # created build/.
    output.parent.mkdir(parents=True, exist_ok=True)
    content = "#define SHELL_SETTLE_PROBE %s\n" % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[3:]:
            Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
