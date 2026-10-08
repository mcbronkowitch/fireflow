#!/usr/bin/env python3
"""Guard for the Rev A routing (P4-2 spec §6). Plain script; exit code is the
verdict. Re-runs itself under KiCad's Python.

1. Two route.py runs in separate processes are green and byte-identical.
2. The committed hardware/reva/kicad/reva.kicad_pcb equals a fresh run.
3. Every gated step of route_check has a sabotage and a `_missing` one, and
   every sabotage has its own phrase (WHY).
4. KNOWN_PANEL names only what spec §4.4 admits, and the name rule itself
   refuses what it must refuse. Since the 9 mm panel pass (2026-10-08) the
   SONG clusters, the GATE_A_L/SOURCE_A and LVL_B_L/PAN_B pairs and the SONG
   plane cut-offs are no longer admitted:
   - drc keys: a gated class word, then names of the 18 jacks (hole y 112.75);
   - routed keys: "unrouted", then such names, at least two; "unrouted net
     ..." never;
   - audio keys (bare, no class word): exactly one of the jack-zone pairs
     IN_L/SHIFTBTN_L and MODBTN_L/OUT_R (Bastian, 2026-09-30, renewed
     2026-10-07 for the combined lamps).
5. An in-process build, saved and reloaded, is green in every step. Its
   exemption zones (module and jack) lie pairwise more than AUDIO_MM apart
   (a pair with items in two different zones is exempt and never measured),
   and each listed jack zone has exactly the members pinned in JACK_ZONES.
6. Every sabotage turns its named step red, and its phrase is printed by that
   step (its RED line or one of its detail lines), not just anywhere in the
   run. routed_song also shows its signal gap as a NEW "not a plane cut-off"
   item; drc_cut also turns rules_file red on the saved report.
7. Branch probes: the non-vacuity and count branches no sabotage reaches are
   each driven red once, with their own phrase on their own step (PROBES).
8. U_REG's heat copper rule (route.reg_copper_rects, Task 7c 2026-10-08)
   gives the hand-computed L on a synthetic field, touches no keep-out and
   refuses a keep-out on the tab (check_reg_rule).

Counts on a sabotaged board are never asserted: kicad-cli's counts vary from
run to run where copper crosses (probed 2026-10-01: 9 or 11 clearance items)."""
import os
import shutil
import subprocess
import sys
import tempfile
import time

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
import copy                  # noqa: E402
import json                  # noqa: E402
import re                    # noqa: E402
import assign                # noqa: E402
import check_kit as CK       # noqa: E402
import route as R            # noqa: E402
import route_check as RC     # noqa: E402
from gen import kipcb        # noqa: E402
from gen import place as PL  # noqa: E402

FAILS = []
KIPY = sys.executable
ROOT = None                  # scratch directory of this run, removed at the end
UNGATED_STEPS = ("report", "render")

# Spec §4.4. The P4-1 pairs (Bastian, 2026-09-29), the SONG clusters and
# their plane cut-offs were admitted until the panel pass and are gone since
# 2026-10-08 (Task 7c): nothing of them may be listed again. The jack-zone
# pairs (Bastian, 2026-09-30; renewed 2026-10-07 for the combined lamps) stay,
# for the audio step's jack zones only: a key naming one of a pair names
# nothing else.
PAIRS = ()
ZONE_PAIRS = ({"IN_L", "SHIFTBTN_L"}, {"MODBTN_L", "OUT_R"})
SONG = set()
CUT_OFF_KEYS = set()
CLASS_WORDS = {"routed": {"unrouted"}, "drc": set(RC.GATED_DRC)}

# The jack zones as the audio step finds them (spec §4.2.2, review M2): the
# key, the victim pad and every aggressor pad by ref.pad. A new aggressor pad
# (panel or not) that widens a zone under the same key turns this red. The
# panel pass (2026-10-08, Task 7c) did not empty them: two pairs stay,
# admitted, with the members of the new board's route_check report.
JACK_ZONES = {
    "IN_L/SHIFTBTN_L": ("J1.T", {"D1.2", "R18.1", "R18.2"}),
    "MODBTN_L/OUT_R": ("J18.T", {"D15.2"}),
}

STEP_RE = re.compile(r"^(RED|   ) \d+\. (\S+)")
ZONE_LINE_RE = re.compile(r"^\s+(\S+): jack zone (\S+) \[[^\]]+\]: (.*); zone \(")
MEMBER_RE = re.compile(r"(\S+\.\S+) \[")


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def scratch(prefix):
    return tempfile.mkdtemp(prefix=prefix, dir=ROOT)


def build_in(dirname):
    r = subprocess.run([KIPY, os.path.join(HERE, "route.py"), "--out", dirname],
                       capture_output=True, text=True)
    return r.returncode, os.path.join(dirname, "reva-routed.kicad_pcb"), r.stdout + r.stderr


def run_text(s, pcb, prefix):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        RC.run(s, pcb, prefix)
    return buf.getvalue()


def red_steps(text):
    return {line.split()[2] for line in text.splitlines() if line.startswith("RED ")}


def sections(text):
    """{step: [its step line, then its detail lines]} of one check run."""
    out, cur = {}, None
    for line in text.splitlines():
        m = STEP_RE.match(line)
        if m:
            cur = m.group(2)
            out[cur] = [line]
        elif cur and line.startswith("        "):
            out[cur].append(line)
        else:
            cur = None
    return out


def on_red_step(text, step, phrase, suffix=""):
    """The lines of `step`'s own output that carry `phrase` (and end with
    `suffix`), [] unless the step is red."""
    sec = sections(text).get(step, [])
    if not sec or not sec[0].startswith("RED "):
        return []
    return [ln.strip() for ln in sec if phrase in ln and ln.rstrip().endswith(suffix)]


def admissible(chk, key, jacks):
    """Spec §4.4's name rule for one KNOWN_PANEL key of check `chk`."""
    if chk == "audio":
        # bare keys: no class word, the names are the whole key
        names = set(CK.key_names(key, set()))
        return (CK.key_allowed(key, jacks | set().union(*ZONE_PAIRS), ZONE_PAIRS, set())
                and names in [set(p) for p in ZONE_PAIRS])
    words = CLASS_WORDS.get(chk)
    if words is None or key.partition(" ")[0] not in words or key.startswith("unrouted net "):
        return False
    if not CK.key_allowed(key, jacks | SONG | set().union(*PAIRS), PAIRS, words):
        return False
    if chk == "routed" and len(set(CK.key_names(key, words))) < 2:
        return key in CUT_OFF_KEYS
    return True


def check_key_names(jacks):
    """The name rule is itself able to go red (keys it must refuse)."""
    good = [("drc", "copper_edge_clearance CLOCK"), ("drc", "clearance IN_L/IN_R"),
            ("routed", "unrouted CLOCK/RESET"),
            ("audio", "MODBTN_L/OUT_R"), ("audio", "IN_L/SHIFTBTN_L")]
    bad = [("routed", "unrouted OUT_L"),          # a pad-to-track gap at one jack
           ("routed", "unrouted SOURCE_A"),       # one name of a pair no longer admitted
           ("routed", "unrouted SONG_A"),         # a SONG plane cut-off, gone with the panel pass
           ("routed", "unrouted SONG_A_L"),       # one name, not a cut-off
           ("routed", "unrouted net SENSE_2"),    # a gap between tracks
           ("routed", "unrouted net ?"),
           ("routed", "unrouted CLOCK_BOGUS"),    # not a part
           ("routed", "clearance CLOCK"),         # a drc class word under routed
           ("drc", "unrouted CLOCK/RESET"),       # routed's class word under drc
           ("drc", "CLOCK"),                      # no class word
           ("drc", "widget CLOCK"),               # an unknown class word
           ("drc", "clearance MODBTN_L/OUT_R"),   # a jack-zone pair outside the audio step
           ("drc", "shorting_items GATE_A_L/SOURCE_A"),  # a P4-1 pair, gone with the panel pass
           ("drc", "shorting_items SONG_A/SONG_A_L"),    # a SONG cluster, gone with the panel pass
           ("drc", "clearance "),                 # no names at all
           ("audio", "OUT_R"),                    # a jack alone
           ("audio", "IN_L/MODBTN_L"),            # across the two pairs
           ("audio", "MODBTN_L/OUT_R/SHIFTBTN_L"),
           ("audio", "CEIL_L/OUT_R"),             # the 2026-09-30 name, gone with the panel pass
           ("audio", "jack MODBTN_L/OUT_R"),      # a class word on a bare key
           ("audio", "IN_L/SHIFTBTN_L/SONG_A"),
           ("planes", "unrouted CLOCK/RESET")]    # a check that has no known list
    for chk, k in good:
        check(admissible(chk, k, jacks), "name rule accepts %s %r" % (chk, k))
    for chk, k in bad:
        check(not admissible(chk, k, jacks), "name rule refuses %s %r" % (chk, k))


def check_sabotage_coverage():
    gated = [n for n, _f in RC.STEPS if n not in UNGATED_STEPS]
    check(bool(gated), "the gated step list was examined (%s)" % ", ".join(gated))
    for n in gated:
        check(RC.TURNS_RED.get(n) == n and n in RC.SABOTAGES,
              "gated step %s has a sabotage that turns it red" % n)
        check(RC.TURNS_RED.get(n + "_missing") == n and (n + "_missing") in RC.SABOTAGES,
              "gated step %s has a %s_missing sabotage" % (n, n))
    for name in sorted(RC.SABOTAGES):
        check(bool(RC.WHY.get(name)) and name in RC.TURNS_RED,
              "sabotage %s names its step and its phrase" % name)


def check_reg_rule():
    """route.reg_copper_rects on a hand-computed field (Task 7c, 2026-10-08):
    tab (0, 0, 2, 4), grid 0.5, half-size 5 (window x -4..6, y -3..7), a pin
    column left of the tab (x < -0.5 for y -1..5) and a foreign box top
    right (x >= 3 for y < 1). The largest rectangle holding the tab is
    (-0.5, -3, 3, 7), 35 mm2; the best partner overlapping it by >= 2 mm
    both ways is (-0.5, 1, 6, 7), union 53 mm2. No rectangle may touch a
    keep-out, and a keep-out on the tab is refused."""
    tab = (0.0, 0.0, 2.0, 4.0)
    keep = [(-10.0, -1.0, -0.5, 5.0), (3.0, -3.0, 6.0, 1.0)]
    inner = (-100.0, -100.0, 100.0, 100.0)
    got = R.reg_copper_rects(tab, keep, inner, win=5.0, grid=0.5)
    check(got == [(-0.5, -3.0, 3.0, 7.0), (-0.5, 1.0, 6.0, 7.0)],
          "heat copper rule: the hand-computed L, got %r" % (got,))
    check(all(not PL.overlaps(r, k) for r in got for k in keep),
          "heat copper rule: no rectangle overlaps a keep-out")
    try:
        R.reg_copper_rects(tab, keep + [(1.0, 1.0, 1.2, 1.2)], inner, win=5.0, grid=0.5)
        refused = False
    except ValueError:
        refused = True
    check(refused, "heat copper rule: a keep-out on the tab is refused")


def check_known_names(jacks):
    n_keys = 0
    for chk, keys in sorted(RC.KNOWN_PANEL.items()):
        for k in sorted(keys):
            n_keys += 1
            check(admissible(chk, k, jacks),
                  "KNOWN_PANEL[%s] %r is admissible under spec §4.4's name rule" % (chk, k))
    check(n_keys > 0, "KNOWN_PANEL was examined (%d keys)" % n_keys)


def check_zones(s, text):
    """Zones apart, and the jack zones' members as the audio step found them."""
    b = s.board
    rects = RC.zones(b) + [z[4] for z in RC.jack_zones(b)]
    check(len(rects) >= 2, "the exemption zones were examined (%d)" % len(rects))
    gaps = [(RC._box_gap(a, c), a, c) for i, a in enumerate(rects) for c in rects[i + 1:]]
    worst = min(gaps, key=lambda g: g[0]) if gaps else (float("inf"), None, None)
    check(worst[0] > RC.AUDIO_MM,
          "the %d exemption zones lie pairwise more than %.1f mm apart (closest %.2f mm: %s, %s)"
          % (len(rects), RC.AUDIO_MM, worst[0], worst[1], worst[2]))
    got = {}
    for line in sections(text).get("audio", []):
        m = ZONE_LINE_RE.match(line)
        if m:
            got[m.group(1)] = (m.group(2), set(MEMBER_RE.findall(m.group(3))))
    check(set(JACK_ZONES) == set(RC.KNOWN_PANEL.get("audio", set())),
          "JACK_ZONES pins exactly the listed audio keys (%s)" % sorted(RC.KNOWN_PANEL.get("audio", set())))
    check(got == JACK_ZONES, "the audio step's jack zones have exactly the pinned members (got %s)" % got)


# --- branch probes (spec §5: a step that examined nothing is red; review
# items M4, M6, task-6 #5 and the rules_file count half). Each either edits
# the reloaded board before the full check run ("mutate") or prepares the
# step's inputs and runs only the named steps ("prep").

def _first_pot(s):
    return sorted(x for x in s.front if x.startswith("RV"))[0]


def _mut_no_copper(s):
    """Every track and via removed."""
    for t in list(s.board.GetTracks()):
        s.board.Delete(t)


def _mut_pot_via(s):
    """A GND via at the centre of the first pot's body box (the via path)."""
    l, t, r, b = PL.body_box(s.board.FindFootprintByReference(_first_pot(s)))
    kipcb.add_via(s.board, ((l + r) / 2.0, (t + b) / 2.0), "GND")


def _mut_pot_ids(s):
    """The first pot's panel id points at no hole: the hole-kind selection
    loses it, the RV prefix keeps it."""
    s.ids[_first_pot(s)] = "NOT_A_HOLE"


def _mut_no_smd(s):
    """Every SMD pad on a plane net moved to another net."""
    net = kipcb._net(s.board, "PLANE_SMD_MOVED")
    for f in s.board.GetFootprints():
        for p in f.Pads():
            if p.GetNetname() in RC.PLANE_NETS and p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD:
                p.SetNet(net)


def _mut_no_gnd_zone(s):
    """Every GND zone removed."""
    for z in list(s.board.Zones()):
        if not z.GetIsRuleArea() and z.GetNetname() == "GND":
            s.board.Delete(z)


def _prep_router_failed(s, pcb, prefix):
    r = copy.copy(s.result)
    r.conflicts, r.failed = 3, ["SENSE_2"]
    s.result = r


def _drop_header(pred):
    """The cached report text loses one block header line, so its items fold
    into the block before it and the parser reads one block fewer than the
    report's own summary says."""
    def prep(s, pcb, prefix):
        txt, err = RC._drc(s, pcb, prefix)
        if err:
            raise SystemExit("probe: kicad-cli failed: %s" % err)
        lines = txt.split("\n")
        i = [i for i, ln in enumerate(lines) if ln.startswith("[") and pred(ln)][0]
        del lines[i]
        s._drc_cache = (pcb, "\n".join(lines))
    return prep


def _zone_cache(s, pcb, change):
    b = RC._saved_board(s, pcb)
    mz, jz = RC.zones(b), RC.jack_zones(b)
    mz, jz, rects = change(mz, jz)
    s._zone_cache = (b, (mz, jz, rects))


def _prep_zone_all(s, pcb, prefix):
    """One exemption zone over the whole board: no copper lies outside."""
    _zone_cache(s, pcb, lambda mz, jz: (mz, jz, [(-1000.0, -1000.0, 1000.0, 1000.0)]))


def _prep_zone_dup(s, pcb, prefix):
    """The first jack zone found twice (review M1: two zones under one key)."""
    _zone_cache(s, pcb, lambda mz, jz: (mz, jz + jz[:1], mz + [z[4] for z in jz + jz[:1]]))


def _prep_rules_severity(s, pcb, prefix):
    """A project file with the five rule values unchanged and courtyards_overlap
    switched to "ignore", so only the count comparison can see it. It was
    clearance until 2026-10-08 (8 items under the saved pro on the old
    board); the 9 mm plate board has no clearance item, but 11 front
    courtyard overlaps, the only gated class with items there."""
    with open(os.path.join(HERE, "kicad", "reva.kicad_pro"), encoding="utf-8") as fh:
        body = json.load(fh)
    body["board"]["design_settings"]["rule_severities"] = {"courtyards_overlap": "ignore"}
    s.rules_pro_bytes = json.dumps(body).encode("utf-8")


# name: (mutate or None, prep or None, steps run (None: all), [(step, phrase)], [(step, absent phrase)])
PROBES = {
    "no_copper": (_mut_no_copper, None, None,
                  [("drc", "0 tracks on the board"),
                   ("pot_keepout", "no F.Cu track or via on the board, nothing measured"),
                   ("sense", RC.SENSE_EMPTY + " SENSE_0")], []),
    "pot_via": (_mut_pot_via, None, None, [("pot_keepout", "F.Cu copper under a pot body: GND via")], []),
    "pot_ids": (_mut_pot_ids, None, None, [("pot_keepout", "pot selection disagrees")], []),
    "planes_no_smd": (_mut_no_smd, None, None,
                      [("planes", "no SMD pad on the plane nets, the stitching count measured nothing")], []),
    "planes_no_zone": (_mut_no_gnd_zone, None, None, [("planes", "no filled GND zone on In1.Cu")], []),
    "routed_router": (None, _prep_router_failed, ["routed"],
                      [("routed", "router left 3 nets in conflict"), ("routed", "router failed nets: SENSE_2")], []),
    # the routed board has no unconnected item since the 9 mm panel pass
    # (2026-10-08): the routed sabotage's open connection gives the block
    # whose header the probe drops
    "routed_count": (RC.SABOTAGES["routed"], _drop_header(lambda ln: ln.startswith("[unconnected_items]")),
                     ["routed"], [("routed", "unconnected pads but")], []),
    "drc_count": (None, _drop_header(lambda ln: not ln.startswith("[unconnected_items]")), ["drc"],
                  [("drc", "the DRC report was not read: complete True")], []),
    "rules_count": (None, _prep_rules_severity, ["rules_file"],
                    [("rules_file", "class courtyards_overlap: 0 under the committed pro")],
                    [("rules_file", "in reva.kicad_pro, not")]),
    "zones_all": (None, _prep_zone_all, ["audio", "lr"],
                  [("audio", "OUT_L: no copper outside the exemption zones"),
                   ("audio", "no aggressor copper outside the exemption zones"),
                   ("lr", "OUT_L/OUT_R: no copper outside the zones")], []),
    "zones_dup": (None, _prep_zone_dup, ["audio"], [("audio", "2 zones under one key")], []),
}

# Sabotages that must show more than their WHY: (step, phrase, line suffix).
EXTRA = {
    # the only proof of check_routed's not_cut exclusion (task-6 re-review)
    "routed_song": [("routed", "not a plane cut-off", "[NEW]")],
    # rules_file's "saved report incomplete" branch
    "drc_cut": [("rules_file", "the DRC report under the saved project file is incomplete", "")],
}


def fresh(base, prefix_name):
    d = scratch(prefix_name)
    pcb = os.path.join(d, "reva-routed.kicad_pcb")
    R.save(base, pcb)
    s = base.reload(pcb)
    s.skip_render = True
    return s, pcb, os.path.join(d, "reva-routed")


def run_probe(base, name):
    mutate, prep, steps, want, absent = PROBES[name]
    s, pcb, prefix = fresh(base, "probe_")
    if mutate:
        mutate(s)
        R.save(s, pcb)
    if steps is None:
        text = run_text(s, pcb, prefix)
    else:
        s.known = {k: set(v) for k, v in RC.KNOWN_PANEL.items()}
        s._drc_cache = s._saved_cache = s._zone_cache = s._audio_cache = None
        prep(s, pcb, prefix)
        fns = dict(RC.STEPS)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            CK.run_steps([(n, fns[n]) for n in steps], s, pcb, prefix)
        text = buf.getvalue()
    for step, phrase in want:
        hits = on_red_step(text, step, phrase)
        check(bool(hits), "probe %s turns %s red with %r: %s" % (
            name, step, phrase, hits[0] if hits else "no such line on a RED %s" % step))
    for step, phrase in absent:
        hits = [ln.strip() for ln in sections(text).get(step, []) if phrase in ln]
        check(not hits, "probe %s: %s prints no %r (%s)" % (name, step, phrase, hits[:1]))


def run_sabotage(base, name):
    s, pcb, prefix = fresh(base, "sab_")
    with contextlib.redirect_stdout(io.StringIO()):
        RC.sabotage(s, name)
    R.save(s, pcb)
    text = run_text(s, pcb, prefix)
    want = RC.TURNS_RED[name]
    check(want in red_steps(text), "sabotage %s turns %s red (red: %s)" % (name, want, sorted(red_steps(text))))
    phrase = RC.why(s, name)
    hits = on_red_step(text, want, phrase)
    check(bool(hits), "sabotage %s is red for its own reason on %s's own lines (%r): %s" % (
        name, want, phrase, hits[0] if hits else "no such line"))
    for step, phrase, suffix in EXTRA.get(name, []):
        hits = on_red_step(text, step, phrase, suffix)
        check(bool(hits), "sabotage %s also shows %r%s on a RED %s: %s" % (
            name, phrase, " ... " + suffix if suffix else "", step, hits[0] if hits else "no such line"))


def main():
    global ROOT
    ROOT = tempfile.mkdtemp(prefix="reva_route_guard_")
    try:
        return run()
    finally:
        shutil.rmtree(ROOT, ignore_errors=True)


def run():
    t_all = t = time.time()
    times = []

    def lap(what):
        nonlocal t
        times.append((what, time.time() - t))
        print("-- %s: %.1f s" % (what, times[-1][1]))
        t = time.time()

    t1, t2 = scratch("route1_"), scratch("route2_")
    rc1, pcb1, out1 = build_in(t1)
    rc2, pcb2, _ = build_in(t2)
    lap("two route.py runs")
    check(rc1 == 0, "a fresh route.py run is green (rc %d)" % rc1)
    if rc1:
        print(out1[-3000:])
    check(rc2 == 0, "the second route.py run is green (rc %d)" % rc2)
    same = os.path.exists(pcb1) and os.path.exists(pcb2) and open(pcb1, "rb").read() == open(pcb2, "rb").read()
    check(same, "two runs in separate processes are byte-identical")
    check(os.path.exists(R.COMMITTED) and os.path.exists(pcb1)
          and open(R.COMMITTED, "rb").read() == open(pcb1, "rb").read(),
          "committed %s equals a fresh run (rerun route.py --write)" % os.path.relpath(R.COMMITTED))

    # The jack row: the 18 jacks, hole y 112.75 (JACK_Y since the 9 mm panel pass).
    jacks = {h["id"] for h in assign.load_holes()
             if h["kind"] == "jack" and abs(h["y_mm"] - 112.75) < 1e-6}
    check(len(jacks) == 18, "the jack row is the 18 jacks at y 112.75 (%d)" % len(jacks))
    check_key_names(jacks)
    check_known_names(jacks)
    check_sabotage_coverage()
    check_reg_rule()
    lap("static checks")

    base = R.build()
    lap("in-process build")
    s0, pcb0, prefix0 = fresh(base, "base_")
    text0 = run_text(s0, pcb0, prefix0)
    red0 = red_steps(text0)
    check(not red0, "an unsabotaged reload is green in every step (red: %s)" % sorted(red0))
    if red0:
        print(text0[-3000:])
    gated = [n for n, _f in RC.STEPS if n not in UNGATED_STEPS]
    check(set(gated) <= set(sections(text0)), "every gated step printed its line on the reload")
    check_zones(s0, text0)
    lap("baseline check run")

    for name in sorted(RC.SABOTAGES):
        run_sabotage(base, name)
    lap("%d sabotages" % len(RC.SABOTAGES))
    for name in sorted(PROBES):
        run_probe(base, name)
    lap("%d branch probes" % len(PROBES))

    print("-- wall time %.1f s (%s)" % (time.time() - t_all, ", ".join("%s %.0f s" % w for w in times)))
    print("FAILED: %d" % len(FAILS) if FAILS else "all routing checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
