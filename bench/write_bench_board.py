#!/usr/bin/env python3
"""Update the generated benchmark board header only when it changed."""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[2] not in {"seed", "patch_sm"}:
        raise SystemExit("usage: write_bench_board.py OUTPUT {seed|patch_sm}")
    output = Path(sys.argv[1])
    content = '#define BENCH_BOARD "%s"\n' % sys.argv[2]
    if sys.argv[2] == "patch_sm":
        content += "#define BENCH_BOARD_PATCH_SM 1\n"
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
