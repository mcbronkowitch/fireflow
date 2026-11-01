"""Writes the SHELL_RGRAIN switch as a real header.

The write_shell_remote_measure.py shape, for the same reason: a non-zero
position also turns on -DSPKY_PROBE_REVERB_INJECT (shell/Makefile), and that
reaches the engine -- Instrument's layout and Instrument::process change with
it, and every shell object that embeds an Instrument with them. A list of
dependent objects would be one forgotten name away from linking a hooked
Instrument against an unhooked caller. So on a change this script deletes
EVERY object file in the build directory, and the next build compiles all of
them under the new value.

ALWAYS defines the symbol, including in position 0, because `#if SHELL_RGRAIN`
has to work in all three.

THE TIMESTAMP EDGE IS NOT ENOUGH (2026-08-23, memory
fireflow-bench-stale-object-trap): this runs while the Makefile is parsed,
before make has read the directory, so the objects are gone before the
dependency graph exists. A first write counts as a change.

    write_shell_rgrain.py OUTPUT {0|1|2} BUILD_DIR

Spec: ../docs/superpowers/specs/2026-10-31-sd-use-cases-tests-design.md
section 5, B1 and B2. Held by test_writer_refusals.py.
"""
import sys
from pathlib import Path


def run() -> int:
    if len(sys.argv) != 4 or sys.argv[2] not in {"0", "1", "2"}:
        raise SystemExit(
            "usage: write_shell_rgrain.py OUTPUT {0|1|2} BUILD_DIR")
    output = Path(sys.argv[1])
    build_dir = Path(sys.argv[3])
    # Before any rule has created build/.
    output.parent.mkdir(parents=True, exist_ok=True)
    content = "#define SHELL_RGRAIN %s\n" % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        for stale in build_dir.glob("*.o"):
            stale.unlink(missing_ok=True)
        output.write_text(content, encoding="utf-8")
    return 0


def main() -> int:
    try:
        return run()
    except BaseException:
        # A refused or crashed run must not leave the previous build's header
        # in place: the Makefile ignores this script's exit status, and the
        # compile would read the stale value. With the file gone it fails on
        # the missing include instead.
        if len(sys.argv) > 1:
            Path(sys.argv[1]).unlink(missing_ok=True)
        raise


if __name__ == "__main__":
    raise SystemExit(main())
