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
5. release_blockers(): it names every live known item, unverified rotation and
   an unverified BOTTOM_SIGN (computed from the live tables); with
   empty lists and verified rotations it is empty (Review Focus 2); with only
   BOTTOM_SIGN unverified it names exactly that (Review Focus 1).
6. --release, in-process with release_blockers patched: one open item
   (BOTTOM_SIGN alone) refuses and writes nothing; no open item writes the
   package without board/ and leaves hardware/reva/fab/ alone. The live
   command line refuses iff release_blockers() is not empty. No assertion here
   depends on today's KNOWN_PANEL contents (the refusal is not a ctest gate)."""
import contextlib
import filecmp
import io
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
import place_check as PC   # noqa: E402
import route_check as RC   # noqa: E402

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
    # the live tables, whatever they hold: the panel pass empties the known
    # lists and a JLC quote upload verifies the rotations, and this check must
    # stay green through both
    live = F.release_blockers()
    for tag, mod in (("place_check", PC), ("route_check", RC)):
        want = [(k, e) for k, v in sorted(mod.KNOWN_PANEL.items()) for e in sorted(v)]
        got = [b for b in live if b.startswith(tag + " KNOWN_PANEL[")]
        check(len(got) == len(want) and all(any(b == "%s KNOWN_PANEL[%s]: %s" % (tag, k, e) for b in got)
                                            for k, e in want),
              "release_blockers has one line per %s.KNOWN_PANEL entry (%d)" % (tag, len(want)))
    unver = sorted(k for k, v in F.ROT_FIX.items() if not v.get("verified"))
    got = sorted(b for b in live if b.startswith("ROT_FIX ") and b.endswith(" unverified"))
    check(got == ["ROT_FIX %s unverified" % k for k in unver],
          "release_blockers names every unverified ROT_FIX package (%d)" % len(unver))
    check(("BOTTOM_SIGN unverified" in live) == (not F.BOTTOM_SIGN.get("verified")),
          "release_blockers names BOTTOM_SIGN iff it is unverified")
    ok_rot = {"X": {"deg": 0, "verified": "2026-12-01"}}
    check(F.release_blockers({}, {}, ok_rot, {"sign": 1, "verified": "2026-12-01"}) == [],
          "release_blockers is empty once the lists are empty and rotations verified")
    check(F.release_blockers({}, {}, ok_rot, {"sign": 1, "verified": None}) == ["BOTTOM_SIGN unverified"],
          "release_blockers names exactly an unverified BOTTOM_SIGN")


def run_release(blockers, out, rel):
    """fab.main(--release) in this process with release_blockers() patched
    (main looks it up in fab's module globals at call time). Returns
    (rc, stdout)."""
    orig = F.release_blockers
    F.release_blockers = lambda *a, **k: list(blockers)
    buf = io.StringIO()
    try:
        with contextlib.redirect_stdout(buf):
            rc = F.main(["--release", "--out", out, "--release-dir", rel])
    finally:
        F.release_blockers = orig
    return rc, buf.getvalue()


def check_release_paths(root):
    real_before = os.path.exists(F.RELEASE)
    # (a) BOTTOM_SIGN alone must refuse (Review Focus 1)
    d1 = os.path.join(root, "rel-refused")
    rc, text = run_release(["BOTTOM_SIGN unverified"], os.path.join(root, "ra"), d1)
    check(rc == 1 and "not released: 1 open items" in text and not os.path.exists(d1),
          "--release refuses with BOTTOM_SIGN as the only open item and writes nothing (rc %d)" % rc)
    # (b) nothing open: the package is written, without board/
    d2 = os.path.join(root, "rel-ok")
    rc, text = run_release([], os.path.join(root, "rb"), d2)
    check(rc == 0 and "released to" in text and os.path.isdir(d2),
          "--release with no open item writes the package (rc %d)" % rc)
    check(not os.path.exists(os.path.join(d2, "board")), "the released package has no board/")
    need = ["reva-gerbers.zip", "cpl-jlc.csv", "bom-jlc.csv", "reva-assembly-front.svg",
            "reva-assembly-back.svg", os.path.join("gerbers", "reva-F_Cu.gtl"),
            os.path.join("gerbers", "reva-PTH.drl")]
    missing = [n for n in need if not os.path.exists(os.path.join(d2, n))]
    check(not missing, "the released package holds its files (missing: %s)" % missing)
    check(os.path.exists(F.RELEASE) == real_before, "hardware/reva/fab/ was not touched")
    # the live command line: it refuses iff something is open
    d3 = os.path.join(root, "rel-live")
    rc, text = run_fab("--release", "--out", os.path.join(root, "rc"), "--release-dir", d3)
    if F.release_blockers():
        check(rc == 1 and "not released" in text and not os.path.exists(d3),
              "the live --release refuses while release_blockers() is not empty (rc %d)" % rc)
    else:
        check(rc == 0 and os.path.isdir(d3), "the live --release writes once release_blockers() is empty")


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
        check_release_paths(root)
    finally:
        shutil.rmtree(root, ignore_errors=True)
    print("FAILED: %d" % len(FAILS) if FAILS else "all fab checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
