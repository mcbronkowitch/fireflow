#!/usr/bin/env python3
"""Update the generated SPKY_DECK_BUS header only when it changed.

Same rationale, same shape, as write_bench_optimization.py and
write_bench_layout.py: `-DSPKY_DECK_BUS=0` on the compile line is invisible
to Make's dependency graph, so an un-cleaned rebuild that only flips this
flag can silently relink objects built under the PREVIOUS value -- and
because Part::process is `inline` in a header, mixed-flag objects linked
together are an ODR violation, not merely a stale measurement. Emitting the
selection into a real header, written only when its content differs, and
declaring it a prerequisite of every object (see bench/Makefile's
`$(OBJECTS): $(BUILD_DIR)/bench_deck_bus.h`) closes that hole the same way
the git-hash and family headers already do.
"""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[2] not in {"0", "1"}:
        raise SystemExit("usage: write_bench_deck_bus.py OUTPUT {0|1}")
    output = Path(sys.argv[1])
    content = "#define SPKY_DECK_BUS %s\n" % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
