#!/usr/bin/env python3
"""The P4-1 checks (spec §5). Every step runs and prints; the run is green
only if every gated step is. A step that examined nothing is red. Known
panel violations (spec §5.3) print as known; an unlisted failure is red, and
so is a listed one that no longer fails."""
import math
import os

import pcbnew

import place as P
from gen import kipcb
from gen import pcb_proof as PP
from gen import place as PL

# Filled from the first full run of Task 3/4/6, restricted to the jack row and
# the SONG clusters (spec §5.3); test_place.py asserts that restriction.
KNOWN_PANEL = {
    "edge": {
        "CLOCK", "GATE_A", "GATE_B", "IN_L", "IN_R", "MOD1_A", "MOD1_B", "MOD2_A",
        "MOD2_B", "MOD3_A", "MOD3_B", "MOD4_A", "MOD4_B", "OUT_L", "OUT_R",
        "PITCH_A", "PITCH_B", "RESET",
    },
    "front": {
        "body SONG_A/SONG_A_L",
        "body SONG_B/SONG_B_L",
        "pad SONG_A_L/SONG_A",
        "pad SONG_B_L/SONG_B",
        "rotation SONG_A_L",
        "rotation SONG_B_L",
    },
    "drc": set(),
}


def _fp(board, ref):
    return board.FindFootprintByReference(ref)


def _key(s, ref):
    return s.ids.get(ref, ref)


def _judge(s, check, found):
    """found: {key: message}. Returns (ok, details, n_known)."""
    known = s.known.get(check, set())
    details = []
    for k in sorted(found):
        tag = "known, waits for the panel pass" if k in known else "NEW"
        details.append("%s: %s [%s]" % (k, found[k], tag))
    stale = sorted(known - set(found))
    details += ["%s: listed as known but no longer fails -- remove it from KNOWN_PANEL" % k
                for k in stale]
    unknown = [k for k in found if k not in known]
    return not unknown and not stale, details, len(set(found) & known)


def check_anchors(s, pcb_path, prefix):
    if not s.holes:
        return False, "examined 0 panel parts", []
    bad, worst = [], 0.0
    for ref, (hx, hy) in sorted(s.holes.items()):
        x, y = PL.hole_point(_fp(s.board, ref))
        d = math.hypot(x - hx, y - hy)
        worst = max(worst, d)
        if d > 0.01:
            bad.append("%s (%s) sits %.3f mm off its hole" % (ref, _key(s, ref), d))
    return not bad, "%d panel parts, worst %.4f mm off its hole (limit 0.01)" % (
        len(s.holes), worst), bad


def _drawn_outline(board):
    """The centre-line box of the Edge.Cuts drawings: their bounding box
    (which includes half the line width) shrunk by that half width, read from
    the drawings. None when there are none."""
    boxes, half = [], 0.0
    for d in board.GetDrawings():
        if d.GetLayer() != pcbnew.Edge_Cuts:
            continue
        boxes.append(PL.box(d.GetBoundingBox()))
        half = max(half, pcbnew.ToMM(d.GetWidth()) / 2.0)
    if not boxes:
        return None
    u = (min(b[0] for b in boxes), min(b[1] for b in boxes),
         max(b[2] for b in boxes), max(b[3] for b in boxes))
    return PL.grow(u, -half)


def check_edge(s, pcb_path, prefix):
    want = P.outline_box()
    drawn = _drawn_outline(s.board)
    if drawn is None:
        return False, "the board has no outline (expected %s)" % (want,), []
    if max(abs(a - b) for a, b in zip(drawn, want)) > 1e-3:
        return False, "the drawn outline %s is not the spec outline %s" % (
            tuple(round(v, 3) for v in drawn), want), []
    lim = PL.grow(want, -P.EDGE_CLEAR)
    found, n = {}, 0
    for fp in s.board.GetFootprints():
        for num, b in PL.pad_boxes(fp):
            n += 1
            if not PL.inside(b, lim):
                over = max(lim[0] - b[0], lim[1] - b[1], b[2] - lim[2], b[3] - lim[3])
                k = _key(s, fp.GetReference())
                found[k] = "pad %s %.2f mm past the %.1f mm edge clearance" % (num, over, P.EDGE_CLEAR)
    if not n:
        return False, "examined 0 pads", []
    ok, details, nk = _judge(s, "edge", found)
    return ok, "%d pads, %d parts past the edge clearance (%d known)" % (n, len(found), nk), details


def check_front(s, pcb_path, prefix):
    if not s.front:
        return False, "examined 0 front parts", []
    fps = {r: _fp(s.board, r) for r in s.front}
    body = {r: PL.body_box(fp) for r, fp in fps.items()}
    pads = {r: PL.pad_boxes(fp) for r, fp in fps.items()}
    found, reported = {}, []
    refs = sorted(fps)
    for i, a in enumerate(refs):
        for b in refs[i + 1:]:
            if PL.overlaps(body[a], body[b]):
                found["body %s" % "/".join(sorted((_key(s, a), _key(s, b))))] = "bodies overlap"
    for x in refs:
        for y in refs:
            if x == y:
                continue
            hits = [n for n, pb in pads[x] if PL.overlaps(pb, body[y])]
            if not hits:
                continue
            kx, ky = _key(s, x), _key(s, y)
            if x.startswith("RV") and y.startswith("D"):
                reported.append("pot %s pins %s under LED %s" % (kx, ",".join(hits), ky))
            else:
                found["pad %s/%s" % (kx, ky)] = "pads %s of %s inside %s's body" % (",".join(hits), kx, ky)
    for ref in sorted(s.no_rotation):
        found["rotation %s" % _key(s, ref)] = "no LED rotation keeps its legs clear"
    ok, details, nk = _judge(s, "front", found)
    details += ["reported, not gated: " + r for r in reported]
    return ok, "%d front parts, %d violations (%d known), %d pot pins under LEDs reported" % (
        len(refs), len(found), nk, len(reported)), details


def render(s, pcb_path, prefix):
    lines = []
    for side, suffix in (("top", "-top.png"), ("bottom", "-bottom.png")):
        rc, out = PP.render(pcb_path, prefix + suffix, side)
        if rc:
            lines.append("render %s rc=%d: %s" % (side, rc, out[-300:]))
    return not lines, "rendered %s-top.png, %s-bottom.png" % (
        os.path.basename(prefix), os.path.basename(prefix)), lines


STEPS = [("anchors", check_anchors), ("edge", check_edge), ("front", check_front),
         ("render", render)]


def run(s, pcb_path, prefix):
    s.known = {k: set(v) for k, v in KNOWN_PANEL.items()} if not s.known else s.known
    green = True
    for i, (name, fn) in enumerate(STEPS, 1):
        ok, line, details = fn(s, pcb_path, prefix)
        print("%s %d. %-10s %s" % ("   " if ok else "RED", i, name, line))
        for d in details[:40]:
            print("        " + d)
        green = green and ok
    return green


def _sab_anchors(s):
    _fp(s.board, sorted(s.holes)[0]).Move(kipcb._pt(0.5, 0.0))


def _sab_anchors_missing(s):
    s.holes.clear()


def _sab_edge(s):
    """A test point (back, module sheet) moved to 0.2 mm from the left edge."""
    ref = sorted(r for r in s.parts if r.startswith("TP"))[0] if any(
        r.startswith("TP") for r in s.parts) else sorted(s.holes)[0]
    fp = _fp(s.board, ref)
    b = PL.pad_boxes(fp)[0][1]
    fp.Move(kipcb._pt(P.X0 + 0.2 - b[0], 0.0))


def _sab_edge_missing(s):
    for d in [d for d in s.board.GetDrawings() if d.GetLayer() == pcbnew.Edge_Cuts]:
        s.board.Delete(d)


def _sab_front(s):
    """The first LED moved onto the first pot's shaft: bodies overlap."""
    led = sorted(r for r in s.front if r.startswith("D"))[0]
    pot = sorted(r for r in s.front if r.startswith("RV"))[0]
    lx, ly = PL.hole_point(_fp(s.board, led))
    px, py = s.holes[pot]
    _fp(s.board, led).Move(kipcb._pt(px - lx, py - ly))


def _sab_front_missing(s):
    s.front = []


def _sab_known_stale(s):
    s.known.setdefault("edge", set()).add("NOT_A_PART")


SABOTAGES = {"anchors": _sab_anchors, "anchors_missing": _sab_anchors_missing,
             "edge": _sab_edge, "edge_missing": _sab_edge_missing,
             "front": _sab_front, "front_missing": _sab_front_missing,
             "known_stale": _sab_known_stale}
# which step each sabotage must turn red (test_place.py reads this)
TURNS_RED = {"anchors": "anchors", "anchors_missing": "anchors", "edge": "edge",
             "edge_missing": "edge", "front": "front", "front_missing": "front",
             "known_stale": "edge"}


def sabotage(s, name):
    if name not in SABOTAGES:
        raise SystemExit("unknown sabotage %r; known: %s" % (name, ", ".join(sorted(SABOTAGES))))
    if not s.known:
        s.known = {k: set(v) for k, v in KNOWN_PANEL.items()}
    SABOTAGES[name](s)
    print("SABOTAGED: %s" % name)
