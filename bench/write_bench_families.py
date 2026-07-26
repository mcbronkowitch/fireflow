#!/usr/bin/env python3
"""Update the generated bench-family selection header only when it changed.

Mirrors write_git_hash.py: -DBENCH_FAMILY_*=1 on the compile line is
invisible to Make's dependency graph, so switching BENCH_FAMILIES would
leave a stale build/families.o believing it still holds the old family
set. Writing the selection into a real header -- rewritten only when its
content differs -- gives Make a real, timestamp-tracked prerequisite for
families.o instead.
"""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) < 2:
        raise SystemExit("usage: write_bench_families.py OUTPUT [DEFINE ...]")
    output = Path(sys.argv[1])
    defines = sys.argv[2:]
    content = "".join("#define %s 1\n" % name for name in defines)
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
