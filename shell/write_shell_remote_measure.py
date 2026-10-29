"""Writes the SHELL_REMOTE_MEASURE switch as a real header.

Same shape and reason as write_shell_remote.py, with one difference that
matters: this switch also turns on -DSPKY_PROBE_MARKS=1 and
-DSPKY_PROBE_TOGGLES=1 (shell/Makefile), and those reach every engine object
-- Instrument::process, Part::process, the lanes, PartFx, Grit all change with
them. A list of dependent objects would be one forgotten name away from
linking a stamped Part against an unstamped Instrument. So on a change this
script deletes EVERY object file in the build directory, and the next build
compiles all of them under the new value.

ALWAYS defines the symbol, including in position 0, because
`#if SHELL_REMOTE_MEASURE` has to work in both.

THE TIMESTAMP EDGE IS NOT ENOUGH (2026-08-23, memory
fireflow-bench-stale-object-trap): this runs while the Makefile is parsed,
before make has read the directory, so the objects are gone before the
dependency graph exists. A first write counts as a change.

    write_shell_remote_measure.py OUTPUT {0|1} BUILD_DIR

Spec: ../docs/superpowers/specs/2026-10-29-cpu-hunt-remote-design.md
section 5.3; plan Task 6. Held by test_remote_wiring.py, which runs it.
"""
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 4 or sys.argv[2] not in {"0", "1"}:
        raise SystemExit(
            "usage: write_shell_remote_measure.py OUTPUT {0|1} BUILD_DIR")
    output = Path(sys.argv[1])
    build_dir = Path(sys.argv[3])
    # Before any rule has created build/.
    output.parent.mkdir(parents=True, exist_ok=True)
    content = "#define SHELL_REMOTE_MEASURE %s\n" % sys.argv[2]
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        for stale in build_dir.glob("*.o"):
            stale.unlink(missing_ok=True)
        output.write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
