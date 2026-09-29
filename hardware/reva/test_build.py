#!/usr/bin/env python3
"""The committed Rev A files are exactly what build.py writes today.

Byte for byte, except git's own autocrlf (a Windows checkout turns LF into
CRLF). A stale schematic, BOM or review sheet goes red here; the check tool's
full level (ctest reva_check_guard) proves the schematic itself.

    python hardware/reva/test_build.py
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import build  # noqa: E402


def read(path):
    with open(path, encoding="utf-8", newline="") as fh:
        return fh.read().replace("\r\n", "\n")


TOP_LEVEL = ("bom-jlc.csv", "bom-hand.csv", "review.md")


def lib_table_failures(kdir):
    """Every ${KIPRJMOD} uri in the committed lib tables must resolve to a
    vendored .kicad_sym file or .pretty directory; KiCad's own rows are not
    ours to check. A guard that finds no ${KIPRJMOD} row examined nothing."""
    failures = []
    found = 0
    for table in ("sym-lib-table", "fp-lib-table"):
        path = os.path.join(kdir, table)
        if not os.path.exists(path):
            continue
        for uri in re.findall(r'\(uri "([^"]*)"\)', read(path)):
            if not uri.startswith("${KIPRJMOD}"):
                continue
            found += 1
            target = os.path.normpath(kdir + uri[len("${KIPRJMOD}"):])
            ok = (os.path.isfile(target) and target.endswith(".kicad_sym")) \
                or (os.path.isdir(target) and target.endswith(".pretty"))
            if not ok:
                failures.append("%s: %s does not resolve to a .kicad_sym or .pretty (%s)"
                                % (table, uri, target))
    if not found:
        failures.append("no ${KIPRJMOD} row in the committed lib tables -- "
                        "the lib-path guard examined nothing")
    return failures


def main():
    failures = []
    # What git tracks: the kicad/ tree and the three top-level outputs.
    # Untracked, ignored KiCad by-products (.kicad_prl, backups, caches) are
    # never orphans, and a generated file that exists but was never `git add`ed
    # is "not committed" -- it would be missing from a fresh clone.
    try:
        out = subprocess.run(
            ["git", "ls-files", "--", "hardware/reva/" + build.KICAD]
            + ["hardware/reva/" + t for t in TOP_LEVEL],
            cwd=REPO, capture_output=True, text=True, check=True).stdout
        tracked = {line.strip()[len("hardware/reva/"):]
                   for line in out.splitlines() if line.strip()}
    except (OSError, subprocess.CalledProcessError) as exc:
        failures.append("git ls-files failed: %s" % exc)
        tracked = set()
    with tempfile.TemporaryDirectory() as tmp:
        written = build.write_all(tmp)
        if len(written) < 17:
            failures.append("build.write_all wrote only %d files" % len(written))
        for rel in written:
            committed = os.path.join(HERE, rel)
            if rel not in tracked:
                failures.append("%s is not committed -- run python hardware/reva/build.py "
                                "and git add it" % rel)
            elif not os.path.exists(committed):
                failures.append("%s is tracked but missing from the work tree" % rel)
            elif read(committed) != read(os.path.join(tmp, rel)):
                failures.append("%s is stale -- run python hardware/reva/build.py" % rel)
        # Orphans: tracked under kicad/ but no longer written by build.py.
        failures += ["%s is committed but no longer generated" % e
                     for e in sorted(tracked - set(written))]
    failures += lib_table_failures(os.path.join(HERE, build.KICAD))
    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("ok: %d generated Rev A files match the committed ones" % len(written))
    return 0


if __name__ == "__main__":
    sys.exit(main())
