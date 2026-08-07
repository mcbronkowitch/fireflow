#!/usr/bin/env python3
"""Update the generated benchmark transport header only when it changed."""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[2] not in {"semihost", "usb"}:
        raise SystemExit(
            "usage: write_bench_transport.py OUTPUT {semihost|usb}"
        )
    output = Path(sys.argv[1])
    content = '#define BENCH_TRANSPORT "%s"\n' % sys.argv[2]
    if sys.argv[2] == "usb":
        content += "#define BENCH_TRANSPORT_USB 1\n"
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
