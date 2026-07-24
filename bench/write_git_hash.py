#!/usr/bin/env python3
"""Update the generated benchmark git-hash header only when it changed."""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: write_git_hash.py OUTPUT GIT_HASH")
    output = Path(sys.argv[1])
    content = '#define BENCH_GIT_HASH "%s"\n' % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
