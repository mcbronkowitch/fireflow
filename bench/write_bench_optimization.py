#!/usr/bin/env python3
"""Update the generated benchmark optimization header only when it changed."""

from pathlib import Path
import sys


VALID = {"o2", "o3", "o3-lto"}


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[2] not in VALID:
        raise SystemExit(
            "usage: write_bench_optimization.py OUTPUT {o2|o3|o3-lto}"
        )
    output = Path(sys.argv[1])
    content = '#define BENCH_OPTIMIZATION "%s"\n' % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
