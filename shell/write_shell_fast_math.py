"""Writes the SHELL_FAST_MATH switch as a stamp file.

Same shape and same reason as write_shell_keys_nc.py, with one difference: the
switch is not read by any C++ file. It changes the compiler flags the Makefile
gives the ENGINE objects (-ffast-math, with or without -fno-finite-math-only),
which a bare make variable cannot express to the dependency graph -- an
existing build/ would reuse the objects built with the other setting, and two
switch positions would compare byte-identical. So the value goes into a stamp
file the engine objects depend on, and this script, which runs while the
Makefile is parsed, deletes those objects whenever the stamp's content changes
(the timestamp edge alone is not enough, see write_shell_wait_probe.py).
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in {"0", "1", "2"}:
        raise SystemExit(
            "usage: write_shell_fast_math.py OUTPUT {0|1|2} [STALE_OBJECT...]")
    output = Path(sys.argv[1])
    # The script runs while the Makefile is parsed, so before any rule has
    # created build/.
    output.parent.mkdir(parents=True, exist_ok=True)
    content = "#define SHELL_FAST_MATH %s\n" % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for stale in sys.argv[3:]:
            Path(stale).unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
