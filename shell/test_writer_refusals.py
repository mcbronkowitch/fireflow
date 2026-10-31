"""Host test: a shell build-switch writer that refuses or crashes leaves NO output.

The Makefile runs these writers while it is parsed and ignores their exit
status, so a refusal that left the previous build's header, stamp or linker
script in place would let the build carry on with a stale value (and the
guards behind it read the stale file and pass). Each writer therefore deletes
its output on ANY failure. This runs the five writers that carry that rule
(reverb_half, fast_math, remote, remote_measure, engine_lean) in a temp
directory with an old output already in place and checks, per writer:

  - a refusal (bad value / bad marker count)       -> non-zero exit, output gone
  - an OSError after the output was written        -> non-zero exit, output gone
  - a good run (control, so the test is not vacuous) -> exit 0, output written

Usage: test_writer_refusals.py [WRITER_DIR]   (default: this directory; point it
at a copy of older writers to see the test go red). Plain script, the exit code
is the verdict.
"""
import os, subprocess, sys, tempfile
from pathlib import Path

here = Path(os.path.dirname(os.path.abspath(__file__)))
wdir = Path(sys.argv[1]) if len(sys.argv) > 1 else here

FAILS = []
def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        FAILS.append(name)

def run(script, *args):
    r = subprocess.run([sys.executable, str(wdir / script)] + [str(a) for a in args],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return r.returncode

def trap_dir(parent, name):
    """A directory where the writer expects a deletable file: unlinking it raises OSError."""
    d = parent / name
    d.mkdir()
    (d / "inner").write_text("x")
    return d

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)

    # --- fast_math / remote: OUTPUT {values} [STALE_OBJECT...] -------------
    for script, good, bad in (("write_shell_fast_math.py", "1", "3"),
                              ("write_shell_remote.py", "1", "2")):
        d = tmp / script[:-3]
        d.mkdir()
        out = d / "sw.h"
        out.write_text("#define OLD 0\n")
        check("%s: bad value refused" % script, run(script, out, bad) != 0)
        check("%s: bad value leaves no output" % script, not out.exists())

        out.write_text("#define OLD 0\n")
        trap = trap_dir(d, "stale.o")
        rc = run(script, out, good, trap)       # content changes -> deletes the stale object -> OSError
        check("%s: OSError after the write is non-zero" % script, rc != 0)
        check("%s: OSError after the write leaves no output" % script, not out.exists())

        rc = run(script, out, good)
        check("%s: good run writes the header" % script, rc == 0 and out.is_file())

    # --- remote_measure: OUTPUT {0|1} BUILD_DIR ----------------------------
    script = "write_shell_remote_measure.py"
    d = tmp / "rm"
    d.mkdir()
    out = d / "sw.h"
    out.write_text("#define OLD 0\n")
    check("%s: bad value refused" % script, run(script, out, "2", d) != 0)
    check("%s: bad value leaves no output" % script, not out.exists())

    out.write_text("#define OLD 0\n")
    trap_dir(d, "x.o")                           # the glob("*.o") sweep cannot unlink it
    rc = run(script, out, "1", d)
    check("%s: OSError during the sweep is non-zero" % script, rc != 0)
    check("%s: OSError during the sweep leaves no output" % script, not out.exists())

    d2 = tmp / "rm2"
    d2.mkdir()
    out2 = d2 / "sw.h"
    check("%s: good run writes the header" % script,
          run(script, out2, "1", d2) == 0 and out2.is_file())

    # --- reverb_half: SOURCE_LD OUTPUT_LD STATE REVERB_HALF MEASURE BUILD_DIR
    script = "write_shell_reverb_half.py"
    d = tmp / "rh"
    d.mkdir()
    good_src = d / "good.ld"
    good_src.write_text("A\n    @REVERB_TANK@\nB\n")
    two_markers = d / "two.ld"
    two_markers.write_text("@REVERB_TANK@\n@REVERB_TANK@\n")
    no_marker = d / "none.ld"
    no_marker.write_text("A\nB\n")
    ld, st = d / "out.ld", d / "out.state"

    def old_outputs():
        ld.write_text("old script\n")
        st.write_text("old state\n")

    for label, src, half in (("no marker", no_marker, "1"),
                             ("two markers", two_markers, "1"),
                             ("unreadable source", d / "missing.ld", "1"),
                             ("bad REVERB_HALF", good_src, "2")):
        old_outputs()
        rc = run(script, src, ld, st, half, "0", d)
        check("%s: %s refused" % (script, label), rc != 0)
        check("%s: %s leaves no linker script and no state" % (script, label),
              not ld.exists() and not st.exists())

    old_outputs()
    trap_dir(d, "y.o")                           # state changes -> object sweep -> OSError
    rc = run(script, good_src, ld, st, "1", "0", d)
    check("%s: OSError after the script was written is non-zero" % script, rc != 0)
    check("%s: OSError after the script was written leaves no output" % script,
          not ld.exists() and not st.exists())

    d3 = tmp / "rh3"
    d3.mkdir()
    ld3, st3 = d3 / "out.ld", d3 / "out.state"
    check("%s: good run writes script and state" % script,
          run(script, good_src, ld3, st3, "1", "0", d3) == 0 and ld3.is_file() and st3.is_file())

    # --- engine_lean: OUTPUT NAME=V... -- STALE... (a STALE directory: every *.o)
    script = "write_shell_engine_lean.py"
    d = tmp / "el"
    d.mkdir()
    out = d / "lean.h"
    for label, args in (("bad value", ["SVF1=2", "--"]),
                        ("lower-case name", ["svf1=1", "--"]),
                        ("no '='", ["SVF1", "--"]),
                        ("a name twice", ["SVF1=0", "SVF1=1", "--"]),
                        ("no '--'", ["SVF1=1"]),
                        ("no switch", ["--"])):
        out.write_text("#define OLD 0\n")
        check("%s: %s refused" % (script, label), run(script, out, *args) != 0)
        check("%s: %s leaves no output" % (script, label), not out.exists())

    out.write_text("#define OLD 0\n")
    sweep = d / "objs"
    sweep.mkdir()
    trap_dir(sweep, "x.o")                       # the *.o sweep cannot unlink it
    rc = run(script, out, "SVF1=1", "--", sweep)
    check("%s: OSError during the sweep is non-zero" % script, rc != 0)
    check("%s: OSError during the sweep leaves no output" % script, not out.exists())

    # Control, so the refusals above are not vacuous: a good run writes the
    # stamp, a changed value sweeps the directory and deletes a named object,
    # an unchanged one keeps both.
    d4 = tmp / "el4"
    d4.mkdir()
    out4, objs, named = d4 / "lean.h", d4 / "objs", d4 / "named.o"
    objs.mkdir()
    check("%s: good run writes the stamp" % script,
          run(script, out4, "SVF1=0", "--", objs, named) == 0
          and out4.read_text() == "#define SHELL_LEAN_SVF1 0\n")
    (objs / "a.o").write_text("x")
    named.write_text("x")
    check("%s: unchanged value keeps the objects" % script,
          run(script, out4, "SVF1=0", "--", objs, named) == 0
          and (objs / "a.o").exists() and named.exists())
    check("%s: changed value deletes the objects" % script,
          run(script, out4, "SVF1=1", "--", objs, named) == 0
          and not (objs / "a.o").exists() and not named.exists()
          and out4.read_text() == "#define SHELL_LEAN_SVF1 1\n")

sys.exit(1 if FAILS else 0)
