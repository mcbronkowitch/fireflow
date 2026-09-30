#!/usr/bin/env python3
"""Guard for the Rev A placement (P4-1 spec §5.1 item 7, §5.4). Plain script;
exit code is the verdict. Re-runs itself under KiCad's Python.

1. Two builds in separate processes are byte-identical.
2. The committed hardware/reva/kicad/reva.kicad_pcb equals a fresh build.
3. The fresh build is green (known panel violations listed).
4. An unsabotaged reload is green in every step, and every sabotage turns its
   named step red.
5. KNOWN_PANEL names only jack-row parts, the SONG clusters and, by the
   owner's decision (Bastian, 2026-09-29), the two LED/pot pairs
   GATE_A_L/SOURCE_A and LVL_B_L/PAN_B, each pair only with its own partner.
   Every name in every key is checked, not just one of them."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
for _p in (HW, HERE):
    if _p not in sys.path:
        sys.path.insert(0, _p)
try:
    import pcbnew  # noqa: F401
except ImportError:
    from gen import ksexp
    kipy = os.path.join(ksexp.KICAD_ROOT, "bin", "python.exe")
    sys.exit(subprocess.call([kipy, os.path.abspath(__file__)]))

import io                    # noqa: E402
import contextlib            # noqa: E402
import assign                # noqa: E402
import place as P            # noqa: E402
import place_check as PC     # noqa: E402

FAILS = []
KIPY = sys.executable

# The pairs the owner admitted until the panel pass (2026-09-29): a key naming
# one of a pair may name nothing but that pair.
PAIRS = ({"GATE_A_L", "SOURCE_A"}, {"LVL_B_L", "PAN_B"})


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def key_names(key):
    """The part names a KNOWN_PANEL key stands for: with a space, the first
    token is the check's class ("body", "pad", "rotation", "clearance", ...)
    and is dropped; the rest splits on "/"."""
    rest = key.split(" ", 1)[1] if " " in key else key
    return [n for n in rest.split("/") if n]


def key_allowed(key, allowed):
    names = set(key_names(key))
    if not names or not names <= allowed:
        return False
    return all(names <= pair for pair in PAIRS if names & pair)


def build_in(dirname):
    r = subprocess.run([KIPY, os.path.join(HERE, "place.py"), "--out", dirname],
                       capture_output=True, text=True)
    return r.returncode, os.path.join(dirname, "reva-placed.kicad_pcb"), r.stdout + r.stderr


def red_steps(s, pcb, prefix):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        PC.run(s, pcb, prefix)
    return {line.split()[2] for line in buf.getvalue().splitlines() if line.startswith("RED ")}


def main():
    t1, t2 = tempfile.mkdtemp(prefix="place1_"), tempfile.mkdtemp(prefix="place2_")
    rc1, pcb1, out1 = build_in(t1)
    rc2, pcb2, _ = build_in(t2)
    check(rc1 == 0, "a fresh build is green (rc %d)" % rc1)
    if rc1:
        print(out1[-3000:])
    same = os.path.exists(pcb1) and os.path.exists(pcb2) and open(pcb1, "rb").read() == open(pcb2, "rb").read()
    check(same, "two builds in separate processes are byte-identical")
    check(os.path.exists(P.COMMITTED) and open(P.COMMITTED, "rb").read() == open(pcb1, "rb").read(),
          "committed %s equals a fresh build (rerun place.py --write)" % os.path.relpath(P.COMMITTED))

    # The jack row is every hole at y 114.0 (spec §5.3); besides the 18 jacks
    # that is SD, two keys and four lamps.
    holes = assign.load_holes()
    jack_ids = {h["id"] for h in holes if abs(h["y_mm"] - 114.0) < 1e-6}
    jacks = {h["id"] for h in holes if h["kind"] == "jack"}
    check(len(jacks) == 18 and jacks <= jack_ids,
          "all 18 jacks are on the jack row (%d jacks, %d of them on it)" % (
              len(jacks), len(jacks & jack_ids)))
    allowed = jack_ids | {"SONG_A", "SONG_B", "SONG_A_L", "SONG_B_L"} | set().union(*PAIRS)
    n_keys = 0
    for chk, keys in sorted(PC.KNOWN_PANEL.items()):
        for k in sorted(keys):
            n_keys += 1
            check(key_allowed(k, allowed),
                  "KNOWN_PANEL[%s] %r names only jack-row parts, SONG parts or an admitted pair" % (chk, k))
    check(n_keys > 0, "KNOWN_PANEL was examined (%d keys)" % n_keys)

    base = P.build()
    d0 = tempfile.mkdtemp(prefix="base_")
    pcb0 = os.path.join(d0, "reva-placed.kicad_pcb")
    P.save(base, pcb0)
    red0 = red_steps(base.reload(pcb0), pcb0, os.path.join(d0, "reva-placed"))
    check(not red0, "an unsabotaged reload is green in every step (red: %s)" % sorted(red0))

    for name in sorted(PC.SABOTAGES):
        d = tempfile.mkdtemp(prefix="sab_")
        pcb = os.path.join(d, "reva-placed.kicad_pcb")
        P.save(base, pcb)
        s = base.reload(pcb)
        with contextlib.redirect_stdout(io.StringIO()):
            PC.sabotage(s, name)
        P.save(s, pcb)
        red = red_steps(s, pcb, os.path.join(d, "reva-placed"))
        want = PC.TURNS_RED[name]
        check(want in red, "sabotage %s turns %s red (red: %s)" % (name, want, sorted(red)))

    print("FAILED: %d" % len(FAILS) if FAILS else "all placement checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
