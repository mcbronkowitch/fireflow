#!/usr/bin/env python3
"""Guard for the Rev A placement (P4-1 spec §5.1 item 7, §5.4). Plain script;
exit code is the verdict. Re-runs itself under KiCad's Python.

1. Two builds in separate processes are byte-identical.
2. The fresh build is green (known panel violations listed).
3. An unsabotaged reload is green in every step, and every sabotage turns its
   named step red. Every gated step has a sabotage and a `_missing` one.
4. KNOWN_PANEL is empty: the 9 mm panel pass (spec 2026-10-07 §1, Task 7c
   2026-10-08) removed every jack-row, SONG and admitted-pair entry; a
   non-empty entry needs Bastian's decision. The key-name rule
   (check_kit.key_allowed) is still exercised on a fixture.
5. The DRC report parser reads every recorded line shape, and the drc
   sabotage's finding names the sabotaged decoupler (not an empty key).
6. The thresholds (outline, edge clearance, USB distance, shadow size) are
   place_check.py's own constants, and the sabotages module_usb, edge_outline
   and drc_empty (a report with no unconnected_items) are each red for their
   own stated reason.
7. On a panel-only board every LED has a rotation, and no LED's body box
   overlaps another front body (P4.1 §4.2, amended 2026-10-08).
8. U_SM sits exactly on place.SM_PIN; an illegal pinned spot is refused
   (pins in bodies, holes in front courtyards); without the pin the search
   still finds a spot (P4.1 §4.3, amended 2026-10-08, Task 7c).

The committed hardware/reva/kicad/reva.kicad_pcb is the routed board since
P4-2; reva_route_guard (test_route.py) compares it with a fresh route.py run,
so this guard no longer does."""
import os
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

import io                    # noqa: E402
import contextlib            # noqa: E402
import check_kit as CK       # noqa: E402
import place as P           # noqa: E402
import place_check as PC     # noqa: E402

FAILS = []
KIPY = sys.executable
ROOT = None                  # scratch directory of this run, removed at the end

# The pairs the owner admitted until the panel pass (2026-09-29): a key naming
# one of a pair may name nothing but that pair. KNOWN_PANEL is empty since the
# 9 mm panel pass (2026-10-08); the pairs stay as check_key_names's fixture for
# check_kit.key_allowed, which test_route.py still uses.
PAIRS = ({"GATE_A_L", "SOURCE_A"}, {"LVL_B_L", "PAN_B"})
UNGATED_STEPS = ("report", "render")

# One block per line shape kicad-cli 10.0.5 printed (see place_check.py), and
# the refs drc_items must read from it.
SYNTHETIC_REPORT = """\
[clearance]: Freiraum-Verstoss ( Freiraum 0,2000 mm; tatsaechlich 0,1000 mm)
    Local override; error
    @(105.1014 mm, 69.6471 mm): Pad 1 [SM_3V3] of C11 on B.Cu
    @(105.2000 mm, 69.6471 mm): Track [GND] on B.Cu
[shorting_items]: Elemente, die zwei Netze kurzschliessen (Netze GND und M4_CH0)
    Local override; error
    @(258.1300 mm, 21.7080 mm): PTH pad 1 [GND] of D16
    @(256.8000 mm, 22.0000 mm): PTH pad 2 [M4_CH0] of RV56
[courtyards_overlap]: Courtyards overlap
    Rule: x; error
    @(237.3300 mm, 41.2080 mm): Footprint D15
    @(234.4250 mm, 42.7200 mm): Footprint RV54
[copper_edge_clearance]: Platinenkanten-Freiraum-Verstoss (... 0,0000 mm)
    Rule: x; error
    @(302.8000 mm, 119.2500 mm): Segment on Edge.Cuts
    @(214.3000 mm, 118.9200 mm): PTH pad T [MOD2_B] of J13
[courtyards_overlap]: Courtyards overlap
    Rule: x; error
    @(1.0000 mm, 2.0000 mm): Segment on Edge.Cuts
    @(3.0000 mm, 4.0000 mm): Via [GND] on F.Cu - B.Cu
"""
SYNTHETIC_WANT = [("clearance", ["C11"]), ("shorting_items", ["D16", "RV56"]),
                  ("courtyards_overlap", ["D15", "RV54"]),
                  ("copper_edge_clearance", ["J13"]), ("courtyards_overlap", [])]


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


# The check classes a KNOWN_PANEL key may start with: the front check's
# ("body", "pad", "rotation") and the DRC classes place_check gates. Any other
# first token is not a class and stays part of the name.
CLASS_WORDS = {"body", "pad", "rotation"} | set(PC.GATED_DRC)


def scratch(prefix):
    return tempfile.mkdtemp(prefix=prefix, dir=ROOT)


def build_in(dirname):
    r = subprocess.run([KIPY, os.path.join(HERE, "place.py"), "--out", dirname],
                       capture_output=True, text=True)
    return r.returncode, os.path.join(dirname, "reva-placed.kicad_pcb"), r.stdout + r.stderr


def run_text(s, pcb, prefix):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        PC.run(s, pcb, prefix)
    return buf.getvalue()


def red_steps(text):
    return {line.split()[2] for line in text.splitlines() if line.startswith("RED ")}


def new_keys(text):
    """The keys of every [NEW] detail line ("<key>: <message> [NEW]")."""
    return [line.strip().split(": ", 1)[0] for line in text.splitlines()
            if line.rstrip().endswith("[NEW]")]


def check_key_names():
    """The name check is itself able to go red (a key it should refuse)."""
    allowed = {"CLOCK", "IN_L", "SONG_A", "SONG_A_L", "GATE_A_L", "SOURCE_A"}
    good = ["CLOCK", "body SONG_A/SONG_A_L", "pad SONG_A_L/SONG_A",
            "copper_edge_clearance CLOCK", "shorting_items GATE_A_L/SOURCE_A"]
    bad = ["NOT_A_PART CLOCK",        # bare edge key with a stray space: only the tail "CLOCK" used to be checked
           "CLOCK stray",
           "CLOCK NOT_A_PART",        # same, and the tail is not a part either
           "widget CLOCK",            # an unknown first token is not a class word
           "body CLOCK/NOT_A_PART",   # a class word, but one name is not admitted
           "body SONG_A/GATE_A_L",    # names from two different pairs' worlds
           "body "]                   # no names at all
    for k in good:
        check(CK.key_allowed(k, allowed, PAIRS, CLASS_WORDS), "key_allowed accepts %r" % k)
    for k in bad:
        check(not CK.key_allowed(k, allowed, PAIRS, CLASS_WORDS), "key_allowed refuses %r" % k)


def check_parser():
    path = os.path.join(scratch("drcparse_"), "synthetic.rpt")
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(SYNTHETIC_REPORT)
    got = PC.drc_items(path)
    check(got == SYNTHETIC_WANT, "drc_items reads SMD pad on a layer, PTH pad, footprint, "
          "track/segment/via lines (got %s)" % got)


def check_sabotage_coverage():
    gated = [n for n, _f in PC.STEPS if n not in UNGATED_STEPS]
    check(bool(gated), "the gated step list was examined (%s)" % ", ".join(gated))
    for n in gated:
        check(PC.TURNS_RED.get(n) == n and n in PC.SABOTAGES,
              "gated step %s has a sabotage that turns it red" % n)
        check(PC.TURNS_RED.get(n + "_missing") == n and (n + "_missing") in PC.SABOTAGES,
              "gated step %s has a %s_missing sabotage" % (n, n))


def check_led_rule():
    """P4.1 §4.2 (amended 2026-10-08): place_panel turns every LED so that its
    pads AND its own body box clear every other front part. Run on a
    panel-only board, so it reports even when the back side cannot be placed.
    Without the body test deck B's REC lamp sits on its key at rotation 0."""
    import build as RB
    from gen import kipcb
    from gen import place as PL
    s = P.Placed()
    s.board = kipcb.new_board(P.X1 - P.X0, P.Y1 - P.Y0, P.LAYERS, origin=(P.X0, P.Y0))
    P.place_panel(s, RB.project())
    fps = {r: s.board.FindFootprintByReference(r) for r in s.front}
    leds = [r for r in s.front if P._hole_index()[s.ids[r]]["kind"] == "led"]
    check(len(leds) == 15, "the LED rule examined the 15 panel LEDs (%d)" % len(leds))
    check(not s.no_rotation, "every LED found a rotation (none: %s)" % sorted(s.no_rotation))
    for r in leds:
        lb = PL.body_box(fps[r])
        hit = [s.ids[q] for q in s.front if q != r and PL.overlaps(lb, PL.body_box(fps[q]))]
        check(not hit, "%s (rot %.0f): its body overlaps no front body (hits: %s)"
              % (s.ids[r], fps[r].GetOrientationDegrees(), hit))


def check_module_pin():
    """P4.1 §4.3 (amended 2026-10-08, Task 7c): with SM_PIN set, U_SM lands
    exactly on it; a pinned spot that fails place_module's own tests is
    refused, both (152.40, 64.25) rot 0 (pins in pot bodies: Task 7a found
    no upright spot on the 9 mm raster) and (152.40, 57.75) rot 270 (legal by
    bodies and pads, but B1 and C5 sit in REV_DIFF's / REV_MOD's courtyards:
    the gated DRC's pth_inside_courtyard). Without the pin the search still
    finds a spot (printed)."""
    import build as RB
    from gen import kipcb
    proj = RB.project()

    def panel_board():
        s = P.Placed()
        s.board = kipcb.new_board(P.X1 - P.X0, P.Y1 - P.Y0, P.LAYERS, origin=(P.X0, P.Y0))
        P.place_panel(s, proj)
        return s

    def spot(s):
        fp = s.board.FindFootprintByReference("U_SM")
        return (round(P.pcbnew.ToMM(fp.GetPosition().x), 4), round(P.pcbnew.ToMM(fp.GetPosition().y), 4),
                int(round(fp.GetOrientationDegrees())) % 360)
    check(P.SM_PIN is not None, "U_SM has a pinned spot (SM_PIN)")
    if P.SM_PIN is None:
        return
    s = panel_board()
    P.place_module(s, proj)
    check(spot(s) == (P.SM_PIN[0], P.SM_PIN[1], P.SM_PIN[2] % 360),
          "U_SM sits on its pin %r (got %r)" % (P.SM_PIN[:3], spot(s)))
    keep = P.SM_PIN
    try:
        for bad in ((152.40, 64.25, 0, "pins in pot bodies"),
                    (152.40, 57.75, 270, "holes in front courtyards")):
            P.SM_PIN = bad
            try:
                P.place_module(panel_board(), proj)
                refused = False
            except ValueError:
                refused = True
            check(refused, "an illegal pinned U_SM spot (%.2f, %.2f) rot %d (%s) is refused" % bad)
        P.SM_PIN = None
        s = panel_board()
        try:
            P.place_module(s, proj)
            found = spot(s)
        except ValueError as e:
            found = str(e)
        check(isinstance(found, tuple), "without the pin the search still finds a spot: %r" % (found,))
    finally:
        P.SM_PIN = keep


def main():
    global ROOT
    ROOT = tempfile.mkdtemp(prefix="reva_place_guard_")
    try:
        return run()
    finally:
        shutil.rmtree(ROOT, ignore_errors=True)


def run():
    t1, t2 = scratch("place1_"), scratch("place2_")
    rc1, pcb1, out1 = build_in(t1)
    rc2, pcb2, _ = build_in(t2)
    check(rc1 == 0, "a fresh build is green (rc %d)" % rc1)
    if rc1:
        print(out1[-3000:])
    same = os.path.exists(pcb1) and os.path.exists(pcb2) and open(pcb1, "rb").read() == open(pcb2, "rb").read()
    check(same, "two builds in separate processes are byte-identical")
    # No committed-board comparison here: the committed board is routed since
    # P4-2 and guarded by reva_route_guard (test_route.py).

    n_keys = sum(len(v) for v in PC.KNOWN_PANEL.values())
    check(n_keys == 0, "KNOWN_PANEL is empty after the panel pass (spec 2026-10-07 §1): %r"
          % {k: sorted(v) for k, v in PC.KNOWN_PANEL.items() if v})

    check_key_names()
    check_parser()
    check_sabotage_coverage()
    check_led_rule()
    check_module_pin()

    base = P.build()
    d0 = scratch("base_")
    pcb0 = os.path.join(d0, "reva-placed.kicad_pcb")
    P.save(base, pcb0)
    red0 = red_steps(run_text(base.reload(pcb0), pcb0, os.path.join(d0, "reva-placed")))
    check(not red0, "an unsabotaged reload is green in every step (red: %s)" % sorted(red0))

    for name in sorted(PC.SABOTAGES):
        d = scratch("sab_")
        pcb = os.path.join(d, "reva-placed.kicad_pcb")
        P.save(base, pcb)
        s = base.reload(pcb)
        with contextlib.redirect_stdout(io.StringIO()):
            PC.sabotage(s, name)
        P.save(s, pcb)
        text = run_text(s, pcb, os.path.join(d, "reva-placed"))
        red = red_steps(text)
        want = PC.TURNS_RED[name]
        check(want in red, "sabotage %s turns %s red (red: %s)" % (name, want, sorted(red)))
        if name in PC.WHY:
            # red for its own reason, not for a bystander: the phrase is on a
            # RED step line or one of its details
            red_lines = [ln for ln in text.splitlines() if PC.WHY[name] in ln]
            check(bool(red_lines), "sabotage %s is red for its own reason (%r): %s" % (
                name, PC.WHY[name], red_lines[0].strip() if red_lines else "no such line"))
        if name == "drc":
            # the finding must be parsed, not an empty "clearance " key
            cref = sorted(base.decouplers)[0]
            hits = [k for k in new_keys(text) if cref in CK.key_names(k, CLASS_WORDS)]
            check(bool(hits), "the drc sabotage's NEW finding names decoupler %s (NEW keys: %s)" % (
                cref, new_keys(text)))

    print("FAILED: %d" % len(FAILS) if FAILS else "all placement checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
