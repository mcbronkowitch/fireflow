#!/usr/bin/env python3
"""The committed Rev A files are exactly what build.py writes today.

Byte for byte, except git's own autocrlf (a Windows checkout turns LF into
CRLF). A stale schematic, BOM or review sheet goes red here; the check tool's
full level (ctest reva_check_guard) proves the schematic itself.

    python hardware/reva/test_build.py
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import build  # noqa: E402


def read(path):
    with open(path, encoding="utf-8", newline="") as fh:
        return fh.read().replace("\r\n", "\n")


def main():
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        written = build.write_all(tmp)
        if len(written) < 14:
            failures.append("build.write_all wrote only %d files" % len(written))
        for rel in written:
            committed = os.path.join(HERE, rel)
            if not os.path.exists(committed):
                failures.append("%s is not committed -- run python hardware/reva/build.py" % rel)
            elif read(committed) != read(os.path.join(tmp, rel)):
                failures.append("%s is stale -- run python hardware/reva/build.py" % rel)
        kdir = os.path.join(HERE, build.KICAD)
        extra = sorted(set("%s/%s" % (build.KICAD, n) for n in os.listdir(kdir))
                       - set(written))
        failures += ["%s is committed but no longer generated" % e for e in extra]
    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("ok: %d generated Rev A files match the committed ones" % len(written))
    return 0


if __name__ == "__main__":
    sys.exit(main())
