#!/usr/bin/env python3
"""The P4-1 checks (spec §5). Every step runs and prints; the run is green
only if every gated step is. A step that examined nothing is red. Known
panel violations (spec §5.3) print as known; an unlisted failure is red, and
so is a listed one that no longer fails."""
import math
import os
import re

import pcbnew

import place as P
from gen import kipcb
from gen import pcb_proof as PP
from gen import place as PL

# Filled from the first full run of Task 3/4/6, restricted to the jack row, the
# SONG clusters and, by the owner's decision (Bastian, 2026-09-29), the
# GATE_A_L/SOURCE_A and LVL_B_L/PAN_B pairs (spec §5.3); test_place.py
# asserts exactly that set, name by name.
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
        # LED legs overlap SOURCE_A's / PAN_B's pins in every LED rotation; no
        # pot rotation fixes it without breaking the cap marker (T18, 20 deg
        # steps) or the jack row after the panel pass; waits for the panel
        # pass (Bastian, 2026-09-29).
        "rotation GATE_A_L",
        "rotation LVL_B_L",
        "rotation SONG_A_L",
        "rotation SONG_B_L",
    },
    "drc": {
        # the jack row: pad T past the 0.5 mm edge clearance, as in "edge"
        "copper_edge_clearance CLOCK", "copper_edge_clearance GATE_A",
        "copper_edge_clearance GATE_B", "copper_edge_clearance IN_L",
        "copper_edge_clearance IN_R", "copper_edge_clearance MOD1_A",
        "copper_edge_clearance MOD1_B", "copper_edge_clearance MOD2_A",
        "copper_edge_clearance MOD2_B", "copper_edge_clearance MOD3_A",
        "copper_edge_clearance MOD3_B", "copper_edge_clearance MOD4_A",
        "copper_edge_clearance MOD4_B", "copper_edge_clearance OUT_L",
        "copper_edge_clearance OUT_R", "copper_edge_clearance PITCH_A",
        "copper_edge_clearance PITCH_B", "copper_edge_clearance RESET",
        # the SONG lamps sit on their pot's pins (same overlap as "front")
        "shorting_items SONG_A/SONG_A_L", "shorting_items SONG_B/SONG_B_L",
        # GATE_A_L/SOURCE_A and LVL_B_L/PAN_B: LED legs on the pot's pins in
        # every LED rotation (see "front" above); the pairs wait for the
        # panel pass by the owner's decision (Bastian, 2026-09-29)
        "clearance GATE_A_L/SOURCE_A", "shorting_items GATE_A_L/SOURCE_A",
        "shorting_items LVL_B_L/PAN_B",
    },
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
    for r in P.back_tht_refs(s):
        fp = _fp(s.board, r)
        for y in refs:
            hits = [n for n, pb in PL.pad_boxes(fp) if PL.overlaps(pb, body[y])]
            if hits:
                found["pad %s/%s" % (r, _key(s, y))] = "pins %s of %s inside %s's body" % (
                    ",".join(hits), r, _key(s, y))
    ok, details, nk = _judge(s, "front", found)
    details += ["reported, not gated: " + r for r in reported]
    return ok, "%d front parts, %d violations (%d known), %d pot pins under LEDs reported" % (
        len(refs), len(found), nk, len(reported)), details


def check_module(s, pcb_path, prefix):
    if s.shadow is None:
        return False, "no module shadow recorded", []
    um = _fp(s.board, "U_SM")
    if um is None:
        return False, "U_SM is not on the board", []
    try:
        shadow = P.module_shadow(um)          # measured from the board, not the placer's record
    except ValueError as e:
        return False, str(e), []
    bad = []
    if max(abs(a - b) for a, b in zip(shadow, s.shadow)) > 1e-3:
        bad.append("U_SM's shadow %s differs from the recorded %s" % (
            tuple(round(v, 2) for v in shadow), tuple(round(v, 2) for v in s.shadow)))
    for fp in s.board.GetFootprints():
        ref = fp.GetReference()
        if ref == "U_SM" or not fp.IsFlipped():
            continue
        c = PL.courtyard_box(fp)
        if c is not None and PL.overlaps(c, shadow):
            bad.append("%s's courtyard enters the module shadow" % ref)
    jp = _fp(s.board, "J_PWR")
    if jp is None:
        bad.append("J_PWR is not on the board")
    else:
        g = PL.gap(PL.courtyard_box(jp), shadow)
        if g < P.USB_CLEAR_MM:
            bad.append("J_PWR is %.1f mm from the module shadow (limit %.0f)" % (g, P.USB_CLEAR_MM))
    return not bad, "shadow %s, J_PWR %.1f mm away (limit %.0f)" % (
        tuple(round(v, 2) for v in shadow),
        PL.gap(PL.courtyard_box(jp), shadow) if jp else -1, P.USB_CLEAR_MM), bad


DECOUPLE_MAX_MM = 2.0     # the coupon's check_layout rule 5 (spec §5.1 item 5)
SD_VCC = ("J_SD", "4")    # C_SD1 decouples J_SD's VDD pad (spec §5.1 item 5)


def _pad(fp, number):
    got = [p for p in fp.Pads() if str(p.GetNumber()) == str(number)]
    return got[0] if got else None


def _expected_decoupled(s):
    """{part: VCC pad}, independent of the placer's pairing: every U_* on the
    board except U_SM and U_REG whose symbol has a VCC pin, and J_SD pad 4."""
    want, bad = {}, []
    for fp in s.board.GetFootprints():
        ref = fp.GetReference()
        if not ref.startswith("U_") or ref in ("U_SM", "U_REG"):
            continue
        part = s.parts.get(ref)
        if part is None:
            bad.append("%s is on the board without a part record" % ref)
            continue
        try:
            want[ref] = str(part.sym.by_name("VCC"))
        except (KeyError, ValueError):
            continue
    if _fp(s.board, SD_VCC[0]) is not None:
        want[SD_VCC[0]] = SD_VCC[1]
    return want, bad


def check_decoupling(s, pcb_path, prefix):
    want, bad = _expected_decoupled(s)
    if not want:
        return False, "examined 0 parts that need a decoupler", bad
    have = {}
    for cref, (ic, num) in sorted(s.decouplers.items()):
        have.setdefault(ic, []).append((cref, num))
    lines, worst, n = [], 0.0, 0
    for ic in sorted(set(have) - set(want)):
        bad.append("%s is recorded as decoupling %s, which needs no decoupler" % (
            ", ".join(c for c, _n in have[ic]), ic))
    for ic, vcc in sorted(want.items()):
        if not have.get(ic):
            bad.append("%s has no decoupler recorded (expected one at pad %s)" % (ic, vcc))
            continue
        m = _pad(_fp(s.board, ic), vcc)
        for cref, num in have[ic]:
            cfp = _fp(s.board, cref)
            c = _pad(cfp, "1") if cfp is not None else None
            if num != vcc or c is None or m is None:
                bad.append("%s: recorded at %s pad %s, expected pad %s%s" % (
                    cref, ic, num, vcc, "" if c is not None else "; the cap is not on the board"))
                continue
            if c.GetNetname() != m.GetNetname() or _pad(cfp, "2").GetNetname() != P.BL.GND:
                bad.append("%s pad 1 is on %s, %s pad %s on %s (pad 2 on %s)" % (
                    cref, c.GetNetname(), ic, vcc, m.GetNetname(), _pad(cfp, "2").GetNetname()))
                continue
            d = math.hypot(pcbnew.ToMM(c.GetPosition().x - m.GetPosition().x),
                           pcbnew.ToMM(c.GetPosition().y - m.GetPosition().y))
            n += 1
            worst = max(worst, d)
            lines.append("%s pad 1 is %.3f mm from %s pad %s" % (cref, d, ic, vcc))
            if d > DECOUPLE_MAX_MM:
                bad.append("%s is %.3f mm from %s pad %s" % (cref, d, ic, vcc))
    return not bad, "%d parts need one, %d decouplers measured, worst %.3f mm (limit %.1f)" % (
        len(want), n, worst, DECOUPLE_MAX_MM), bad + lines


def _mst(pts):
    """Prim's minimum spanning tree length over pad centres."""
    if len(pts) < 2:
        return 0.0
    done, rest, total = [pts[0]], list(pts[1:]), 0.0
    while rest:
        d, j = min((math.hypot(a[0] - b[0], a[1] - b[1]), j)
                   for j, b in enumerate(rest) for a in done)
        total += d
        done.append(rest.pop(j))
    return total


SD_HEIGHT_MM = 14.18      # LCSC C3177022
GAP_MM = 10.0             # assumption until the grip test (spec §8)
MODULE_MM, BOARD_MM = 15.0, 1.6
PALETTE_MID, PALETTE_EDGE = 45.5, 37.4


PLANE_NETS = {net for _layer, net in P.PLANES}


def _ratsnest_lines(s):
    """Spec §5.2, P4-2's baseline: MST length over pad centres per net, for
    every net except the two plane nets (GND, SM_3V3), which are not routed.
    A net belongs to a block (a sheet) when all its pads' parts are on that
    sheet, otherwise to "between blocks"."""
    idx = P.pad_index(s.board)
    per = {b: 0.0 for b in set(s.sheet_of.values())}
    between, signal = 0.0, 0.0
    for net in sorted(idx):
        if net in PLANE_NETS:
            continue
        v = idx[net]
        length = _mst([xy for _r, _p, xy in v])
        if net not in P.SUPPLY:
            signal += length
        sheets = {s.sheet_of.get(r, "?") for r, _p, _xy in v}
        if len(sheets) == 1:
            b = sheets.pop()
            per[b] = per.get(b, 0.0) + length
        else:
            between += length
    total = sum(per.values()) + between
    out = ["ratsnest (MST over pad centres, all nets but the planes %s): %.0f mm total, "
           "%.0f mm of it on signal nets" % ("/".join(sorted(PLANE_NETS)), total, signal)]
    out += ["ratsnest block %-12s %6.0f mm" % (b, per[b]) for b in sorted(per)]
    out.append("ratsnest between blocks    %6.0f mm" % between)
    return out


def report(s, pcb_path, prefix):
    lines = ["SD socket %.2f mm tall against a %.1f mm panel gap: protrudes %.2f mm past "
             "the panel's back face (assumed gap)" % (SD_HEIGHT_MM, GAP_MM, SD_HEIGHT_MM - GAP_MM),
             "depth: module %.1f + board %.1f + gap %.1f = %.1f mm behind the panel, against "
             "the Palette's %.1f (middle) / %.1f (outermost HP)" % (
                 MODULE_MM, BOARD_MM, GAP_MM, MODULE_MM + BOARD_MM + GAP_MM, PALETTE_MID, PALETTE_EDGE)]
    for ref in ("U_SM", "J_PWR"):
        fp = _fp(s.board, ref)
        if fp is not None:
            lines.append("%s at (%.2f, %.2f) rot %.0f" % (
                ref, pcbnew.ToMM(fp.GetPosition().x), pcbnew.ToMM(fp.GetPosition().y),
                fp.GetOrientationDegrees()))
    jp = _fp(s.board, "J_PWR")
    if jp is not None:
        pb = dict(PL.pad_boxes(jp))
        x1, x10 = (pb["1"][0] + pb["1"][2]) / 2.0, (pb["10"][0] + pb["10"][2]) / 2.0
        vx, vy = P.jpwr_key_vector(jp)
        lines.append("J_PWR pin 1 (-12 V) at the %s end (pad 1 x %.2f, pad 10 x %.2f); "
                     "key vector (%.2f, %.2f) faces the %s edge" % (
                         "west" if x1 < x10 else "east", x1, x10, vx, vy,
                         "top" if vy < 0 else "bottom"))
    sd2, sd = _fp(s.board, "C_SD2"), _fp(s.board, SD_VCC[0])
    if sd2 is not None and sd is not None:
        a, b = _pad(sd2, "1").GetPosition(), _pad(sd, SD_VCC[1]).GetPosition()
        lines.append("C_SD2 (DNP, not gated) pad 1 is %.3f mm from %s pad %s" % (
            math.hypot(pcbnew.ToMM(a.x - b.x), pcbnew.ToMM(a.y - b.y)), SD_VCC[0], SD_VCC[1]))
    lines += _ratsnest_lines(s)
    for ref in sorted(r for r in s.anchors if r.startswith("U_")):
        fp = _fp(s.board, ref)
        ax, ay = s.anchors[ref]
        how = s.anchor_from.get(ref)
        lines.append("%s %.1f mm from its anchor%s" % (ref, math.hypot(
            pcbnew.ToMM(fp.GetPosition().x) - ax, pcbnew.ToMM(fp.GetPosition().y) - ay),
            " (%s)" % how if how else ""))
    lines += ["override in force: " + o for o in s.overrides_used] or ["no overrides in force"]
    return True, "reported, never gates", lines


def render(s, pcb_path, prefix):
    lines = []
    for side, suffix in (("top", "-top.png"), ("bottom", "-bottom.png")):
        rc, out = PP.render(pcb_path, prefix + suffix, side)
        if rc:
            lines.append("render %s rc=%d: %s" % (side, rc, out[-300:]))
    return not lines, "rendered %s-top.png, %s-bottom.png" % (
        os.path.basename(prefix), os.path.basename(prefix)), lines


# kicad-cli 10.0.5 report shapes, probed 2026-09-30 on the placed board (item
# lines follow a header line; the messages are localised, the item lines are
# not):
#   [courtyards_overlap]: Courtyards overlap
#       Rule: ...; error
#       @(237.3300 mm, 41.2080 mm): Footprint D15
#       @(234.4250 mm, 42.7200 mm): Footprint RV54
#   [copper_edge_clearance]: ... (... Freiraum 0,5000 mm; tatsaechlich 0,0000 mm)
#       @(302.8000 mm, 119.2500 mm): Segment on Edge.Cuts        <- names no ref
#       @(214.3000 mm, 118.9200 mm): PTH pad T [MOD2_B] of J13
#   [clearance]: Freiraum-Verstoss ( Freiraum 0,2000 mm; tatsaechlich 0,1973 mm)
#       @(72.8750 mm, 42.7200 mm): PTH pad 2 [M1_CH3] of RV10
#       @(71.5700 mm, 41.2080 mm): PTH pad 2 [LED3_A] of D4
#   SMD pads carry the layer (silk classes report the same way):
#       @(105.1014 mm, 69.6471 mm): Pad 1 [SM_3V3] of C11 on B.Cu
#       @(209.8000 mm, 109.5000 mm): Segment of J13 on F.Silkscreen
#   Tracks and vias name no part:
#       @(...): Track [GND] on B.Cu
# (shorting_items and pth_inside_courtyard use the same "... pad N [NET] of REF" /
# "Footprint REF" item lines.) A ref is the word after "of" or "Footprint", with
# an optional " on <layer>" after it, at the end of the line.
GATED_DRC = ("courtyards_overlap", "pth_inside_courtyard", "shorting_items", "clearance",
             "hole_clearance", "hole_to_hole", "copper_edge_clearance", "items_not_allowed")
FRONT_REPORTED = ("courtyards_overlap", "pth_inside_courtyard")
_REF_RE = re.compile(
    r"(?:\bof|Footprint) ([A-Za-z_]+[0-9]*[A-Za-z_0-9]*)(?: on \S+)?\s*$", re.M)
_CLASS_RE = re.compile(r"^\[([a-z0-9_]+)\]")
_ITEM_RE = re.compile(r"^\s*(@\(.*)$", re.M)


def drc_blocks(txt):
    """[(class, [refs], first item line)] per violation block of report text."""
    out = []
    for block in re.split(r"(?=^\[)", txt, flags=re.M):
        m = _CLASS_RE.match(block)
        if m:
            first = _ITEM_RE.search(block)
            out.append((m.group(1), sorted(set(_REF_RE.findall(block))),
                        first.group(1).strip() if first else "no item line"))
    return out


def drc_items(rpt_path):
    """[(class, [refs])] per violation block of a kicad-cli report."""
    txt = open(rpt_path, encoding="utf-8", errors="replace").read()
    return [(c, r) for c, r, _first in drc_blocks(txt)]


def check_drc(s, pcb_path, prefix):
    rpt = prefix + "-drc.rpt"
    try:
        items = PP.drc(pcb_path + (".missing" if getattr(s, "drc_broken", False) else ""), rpt)
    except RuntimeError as e:
        return False, "kicad-cli wrote no report: %s" % str(e)[:200], []
    blocks = drc_blocks(open(rpt, encoding="utf-8", errors="replace").read())
    found, front_crtyd = {}, []
    front = set(s.front)
    for cls, refs, first in blocks:
        if cls not in GATED_DRC:
            continue
        if cls in FRONT_REPORTED and refs and set(refs) <= front:
            # Front-side courtyards overlap by design on this panel (the P4a
            # strip already had 6 LED/pot/jack overlaps unrouted); the physical
            # question is the front check's body test (spec §5.1 item 6
            # amendment).
            front_crtyd.append("%s %s" % (cls, "/".join(sorted(_key(s, r) for r in refs))))
            continue
        key = "%s %s" % (cls, "/".join(sorted(_key(s, r) for r in refs)))
        found.setdefault(key, "kicad-cli, first item %s" % first)
    ok, details, nk = _judge(s, "drc", found)
    details += ["reported, not gated: " + f for f in sorted(set(front_crtyd))]
    others = ", ".join("%s %d" % kv for kv in sorted(items.items()) if kv[0] not in GATED_DRC) or "none"
    return ok, "gated: %d items (%d known), %d front courtyard items reported; not gated: %s" % (
        len(found), nk, len(front_crtyd), others), details


STEPS = [("anchors", check_anchors), ("edge", check_edge), ("front", check_front),
         ("module", check_module), ("decoupling", check_decoupling),
         ("drc", check_drc), ("report", report), ("render", render)]


DETAIL_CAP = 40


def run(s, pcb_path, prefix):
    s.known = {k: set(v) for k, v in KNOWN_PANEL.items()} if not s.known else s.known
    green = True
    for i, (name, fn) in enumerate(STEPS, 1):
        ok, line, details = fn(s, pcb_path, prefix)
        print("%s %d. %-10s %s" % ("   " if ok else "RED", i, name, line))
        # A green step prints every detail (they are reports: known items,
        # distances). A red one prints its first 40 and says how many it cut.
        shown = details if ok else details[:DETAIL_CAP]
        for d in shown:
            print("        " + d)
        if len(details) > len(shown):
            print("        ... %d more lines" % (len(details) - len(shown)))
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


def _sab_module(s):
    """A back test point, or failing that J_PWR, moved into the shadow centre."""
    cx, cy = (s.shadow[0] + s.shadow[2]) / 2.0, (s.shadow[1] + s.shadow[3]) / 2.0
    tps = sorted(r for r in s.parts if r.startswith("TP"))
    fp = _fp(s.board, tps[0] if tps else "J_PWR")
    fp.SetPosition(kipcb._pt(cx, cy))


def _sab_module_missing(s):
    s.shadow = None


def _sab_front_back_tht(s):
    """U_SM moved so its first pad (A1) sits on the first pot's hole point: a
    back THT pin inside a front body."""
    pot = sorted(r for r in s.front if r.startswith("RV"))[0]
    um = _fp(s.board, "U_SM")
    pb = PL.pad_boxes(um)[0][1]
    px, py = s.holes[pot]
    um.Move(kipcb._pt(px - (pb[0] + pb[2]) / 2.0, py - (pb[1] + pb[3]) / 2.0))


def _sab_decoupling(s):
    """The first decoupler moved 10 mm further from its IC's VCC pad, along
    the VCC-pad-to-pad-1 direction."""
    cref = sorted(s.decouplers)[0]
    ic, num = s.decouplers[cref]
    c = _pad(_fp(s.board, cref), "1").GetPosition()
    m = _pad(_fp(s.board, ic), num).GetPosition()
    dx, dy = pcbnew.ToMM(c.x - m.x), pcbnew.ToMM(c.y - m.y)
    d = math.hypot(dx, dy)
    ux, uy = (dx / d, dy / d) if d > 0 else (1.0, 0.0)
    _fp(s.board, cref).Move(kipcb._pt(10.0 * ux, 10.0 * uy))


def _sab_decoupling_missing(s):
    s.decouplers.clear()


def _sab_drc(s):
    """A GND track 0.1 mm beside a decoupler's rail pad: a near miss, not a
    touch (a touch is renamed to the pad's net on save, probed 2026-09-29)."""
    fp = _fp(s.board, sorted(s.decouplers)[0])
    bb = [p for p in fp.Pads() if str(p.GetNumber()) == "1"][0].GetBoundingBox()
    x = pcbnew.ToMM(bb.GetRight()) + 0.1 + 0.125
    kipcb.add_track(s.board, "B.Cu", 0.25, "GND",
                    [(x, pcbnew.ToMM(bb.GetTop()) - 1.0), (x, pcbnew.ToMM(bb.GetBottom()) + 1.0)])


def _sab_drc_missing(s):
    s.drc_broken = True


SABOTAGES = {"anchors": _sab_anchors, "anchors_missing": _sab_anchors_missing,
             "edge": _sab_edge, "edge_missing": _sab_edge_missing,
             "front": _sab_front, "front_missing": _sab_front_missing,
             "known_stale": _sab_known_stale,
             "module": _sab_module, "module_missing": _sab_module_missing,
             "front_back_tht": _sab_front_back_tht,
             "decoupling": _sab_decoupling, "decoupling_missing": _sab_decoupling_missing,
             "drc": _sab_drc, "drc_missing": _sab_drc_missing}
# which step each sabotage must turn red (test_place.py reads this)
TURNS_RED = {"anchors": "anchors", "anchors_missing": "anchors", "edge": "edge",
             "edge_missing": "edge", "front": "front", "front_missing": "front",
             "known_stale": "edge", "module": "module", "module_missing": "module",
             "front_back_tht": "front", "decoupling": "decoupling",
             "decoupling_missing": "decoupling", "drc": "drc", "drc_missing": "drc"}


def sabotage(s, name):
    if name not in SABOTAGES:
        raise SystemExit("unknown sabotage %r; known: %s" % (name, ", ".join(sorted(SABOTAGES))))
    if not s.known:
        s.known = {k: set(v) for k, v in KNOWN_PANEL.items()}
    SABOTAGES[name](s)
    print("SABOTAGED: %s" % name)
