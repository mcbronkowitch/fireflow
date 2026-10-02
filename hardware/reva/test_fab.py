#!/usr/bin/env python3
"""Guard for the Rev A order package (P4-3 spec §6). Plain script; exit code
is the verdict. Re-runs itself under KiCad's Python.

1. Two fab.py runs in separate processes are green and byte-identical
   (renders excluded: PNGs carry renderer noise; board/ is not the package).
2. Every gated step of fab_check has a sabotage and a `_missing` one, and
   every sabotage names its step and its phrase.
3. Every sabotage exits 1, turns its step red, and its phrase is printed on
   that step's own lines.
4. normalise() catches every date form, in any offset (Review Focus 5), and
   leaves coordinates alone.
5. release_blockers(): today it names the known items and BOTTOM_SIGN; with
   empty lists and verified rotations it is empty (Review Focus 2); with only
   BOTTOM_SIGN unverified it names exactly that (Review Focus 1).
6. --release refuses today and leaves its target directory absent."""
import filecmp
import os
import re
import shutil
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

import fab as F            # noqa: E402
import fab_check as FC     # noqa: E402

FAILS = []
STEP_RE = re.compile(r"^(RED|   ) \d+\. (\S+)")


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        FAILS.append(what)


def run_fab(*args):
    r = subprocess.run([sys.executable, os.path.join(HERE, "fab.py")] + list(args),
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode, r.stdout + r.stderr


def sections(text):
    out, cur = {}, None
    for ln in text.splitlines():
        m = STEP_RE.match(ln)
        if m:
            cur = m.group(2)
            out[cur] = [ln]
        elif cur and ln.startswith("        "):
            out[cur].append(ln)
        else:
            cur = None
    return out


def red_steps(text):
    return {m.group(2) for m in (STEP_RE.match(l) for l in text.splitlines()) if m and m.group(1) == "RED"}


def same_tree(a, b):
    bad = []
    for root, dirs, files in os.walk(a):
        dirs[:] = [d for d in dirs if d != "board"]
        for n in files:
            if n.endswith(".png"):
                continue
            pa = os.path.join(root, n)
            pb = os.path.join(b, os.path.relpath(pa, a))
            if not os.path.exists(pb) or not filecmp.cmp(pa, pb, shallow=False):
                bad.append(os.path.relpath(pa, a))
    return bad


def check_normalise():
    fx, fs = F.FIXED_DATE, F.FIXED_DATE_SP
    cases = [("%TF.CreationDate,2026-10-02T09:34:20+02:00*%", "%TF.CreationDate," + fx + "*%"),
             ("%TF.CreationDate,2026-12-01T23:59:59Z*%", "%TF.CreationDate," + fx + "*%"),
             ("%TF.CreationDate,2026-12-01T08:00:00-05:00*%", "%TF.CreationDate," + fx + "*%"),
             ("G04 #@! TF.CreationDate,2026-10-02T09:34:21+02:00*", "G04 #@! TF.CreationDate," + fx + "*"),
             ("G04 Created by KiCad (PCBNEW 10.0.5) date 2026-10-02 09:34:20*",
              "G04 Created by KiCad (PCBNEW 10.0.5) date " + fs + "*"),
             ("; DRILL file KiCad 10.0.5 date 2026-10-02T09:34:22", "; DRILL file KiCad 10.0.5 date " + fx),
             ("; #@! TF.CreationDate,2026-10-02T09:34:22+02:00", "; #@! TF.CreationDate," + fx),
             ("X2000000Y-9250000D02*", "X2000000Y-9250000D02*")]
    for raw, want in cases:
        check(F.normalise(raw) == want, "normalise %r -> %r" % (raw, F.normalise(raw)))


def check_release_rules():
    today = F.release_blockers()
    check(any("unrouted SONG_A" in b for b in today), "release_blockers names the known items today")
    check("BOTTOM_SIGN unverified" in today, "release_blockers names BOTTOM_SIGN today")
    ok_rot = {"X": {"deg": 0, "verified": "2026-12-01"}}
    check(F.release_blockers({}, {}, ok_rot, {"sign": 1, "verified": "2026-12-01"}) == [],
          "release_blockers is empty once the lists are empty and rotations verified")
    check(F.release_blockers({}, {}, ok_rot, {"sign": 1, "verified": None}) == ["BOTTOM_SIGN unverified"],
          "release_blockers names exactly an unverified BOTTOM_SIGN")


def check_coverage():
    gated = [n for n, _f in FC.STEPS]
    check(bool(gated), "the gated step list was examined (%s)" % ", ".join(gated))
    for n in gated:
        check(FC.TURNS_RED.get(n) == n and n in FC.SABOTAGES, "step %s has a sabotage" % n)
        check(FC.TURNS_RED.get(n + "_missing") == n, "step %s has a %s_missing sabotage" % (n, n))
    for name in sorted(FC.SABOTAGES):
        check(bool(FC.WHY.get(name)) and name in FC.TURNS_RED, "sabotage %s names its step and phrase" % name)


def main():
    root = tempfile.mkdtemp(prefix="reva_fab_guard_")
    try:
        a, b = os.path.join(root, "a"), os.path.join(root, "b")
        rc1, out1 = run_fab("--out", a)
        rc2, _ = run_fab("--out", b)
        check(rc1 == 0, "a fresh fab.py run is green (rc %d)" % rc1)
        if rc1:
            print(out1[-3000:])
        check(rc2 == 0, "the second fab.py run is green (rc %d)" % rc2)
        diff = same_tree(a, b)
        check(not diff, "two runs are byte-identical (differ: %s)" % diff[:5])
        # an empty tree is trivially identical to another empty one
        n_gerbers = len(os.listdir(os.path.join(a, "gerbers"))) if os.path.isdir(os.path.join(a, "gerbers")) else 0
        check(n_gerbers >= len(F.LAYERS) + 2 and os.path.exists(os.path.join(a, "reva-gerbers.zip")),
              "the compared tree holds the package (%d gerber/drill files and the zip)" % n_gerbers)
        check(set(n for n, _f in FC.STEPS) <= set(sections(out1)), "every step printed its line")
        check_coverage()
        check_normalise()
        check_release_rules()
        for name in sorted(FC.SABOTAGES):
            rc, text = run_fab("--sabotage", name, "--out", os.path.join(root, "sab"))
            step = FC.TURNS_RED[name]
            check(rc == 1 and step in red_steps(text), "sabotage %s turns %s red (red: %s)"
                  % (name, step, sorted(red_steps(text))))
            hits = [l for l in sections(text).get(step, []) if FC.WHY[name] in l]
            check(bool(hits), "sabotage %s prints %r on %s's own lines" % (name, FC.WHY[name], step))
        rel = os.path.join(root, "release")
        rc, text = run_fab("--release", "--out", os.path.join(root, "r"), "--release-dir", rel)
        check(rc == 1 and "not released" in text and not os.path.exists(rel),
              "--release refuses today and writes nothing (rc %d)" % rc)
    finally:
        shutil.rmtree(root, ignore_errors=True)
    print("FAILED: %d" % len(FAILS) if FAILS else "all fab checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
