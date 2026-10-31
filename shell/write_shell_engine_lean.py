"""Writes the lean switches (SHELL_LEAN_*) as one stamp file.

usage: write_shell_engine_lean.py OUTPUT NAME=V... -- STALE...
       NAME   a lean switch without its prefix (SVF1 for SHELL_LEAN_SVF1)
       V      0 or 1
       STALE  objects to delete when the stamp changes; a directory stands
              for every *.o in it

The lean switches are the class-2 candidates of CPU round 2 (spec
2026-10-30-cpu-engine-kernels-design.md section 4): firmware-only savings that
change the output. A new switch starts OFF until Bastian has accepted it; of
the round-2 three, GRAIN_WIN and EXP2 are ON and SVF1 is OFF (accepted, but
the board measured no saving -- shell/Makefile). Same shape and
same reason as write_shell_fast_math.py: no C++ file reads this header. Each
switch sets -DSPKY_LEAN_<NAME>=1 (shell/Makefile), which a bare make variable
cannot express to the dependency graph -- an existing build/ would reuse the
objects built with the other setting, and two switch positions would compare
byte-identical. So the values go into one stamp, and this script, which runs
while the Makefile is parsed, deletes the stale objects whenever the stamp's
content changes (the timestamp edge alone is not enough, see
write_shell_wait_probe.py). A first write counts as a change.

The Makefile passes the build directory, not only the engine objects: a lean
switch can change an engine class's member layout (SHELL_LEAN_SVF1 adds
members to SvfLp), and the shell objects that embed engine types (main.o,
panel_scan.o, cb_profile.o, wt_bank_d2.o) would keep the old layout -- the
reason write_shell_reverb_half.py sweeps every object as well.

Any failure (a malformed NAME=V, a name given twice, no '--', a write or
delete error) deletes OUTPUT before it exits: the Makefile ignores this
script's exit status.
"""
import re
import sys
from pathlib import Path

USAGE = __doc__.strip().splitlines()[2]
NAME = re.compile(r"^[A-Z][A-Z0-9_]*$")


def run() -> int:
    args = sys.argv[1:]
    if len(args) < 2 or "--" not in args[1:]:
        raise SystemExit(USAGE)
    output = Path(args[0])
    sep = args.index("--", 1)
    pairs, stale = args[1:sep], args[sep + 1:]
    if not pairs:
        raise SystemExit(USAGE)
    values = {}
    for pair in pairs:
        name, eq, value = pair.partition("=")
        if not eq or not NAME.match(name) or value not in {"0", "1"}:
            raise SystemExit("bad lean switch %r (want NAME=0 or NAME=1)" % pair)
        if name in values:
            raise SystemExit("lean switch %s given twice" % name)
        values[name] = value
    content = "".join("#define SHELL_LEAN_%s %s\n" % (name, values[name])
                      for name in sorted(values))
    # The script runs while the Makefile is parsed, so before any rule has
    # created build/.
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.is_file() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")
        for entry in stale:
            path = Path(entry)
            if path.is_dir():
                for obj in path.glob("*.o"):
                    obj.unlink(missing_ok=True)
            else:
                path.unlink(missing_ok=True)
    return 0


def main() -> int:
    try:
        return run()
    except BaseException:
        # A refused or crashed run must not leave the previous stamp in place
        # (the Makefile ignores this script's exit status).
        if len(sys.argv) > 1:
            Path(sys.argv[1]).unlink(missing_ok=True)
        raise


if __name__ == "__main__":
    raise SystemExit(main())
