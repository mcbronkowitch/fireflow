#!/usr/bin/env python3
"""The P4-2 checks (spec §5). Every step measures the saved board, not what
the router reported. Each step owns its thresholds and net sets (none is
imported from rules.py, route.py or gen/route.py). Known panel violations
(spec §4.4) print as known; an unlisted failure is red, and so is a listed
one that no longer fails. A step that examined nothing is red."""
import os
import re

import pcbnew

import check_kit as CK
from gen import pcb_proof as PP

# Filled from the first converged full run (Task 6, 2026-10-01), restricted by
# the owner's rule (spec §4.4) to the jack row, the SONG clusters, the
# GATE_A_L/SOURCE_A and LVL_B_L/PAN_B pairs and the jack-zone pairs
# CEIL_L/OUT_R and SHIFTBTN_L/IN_L (the last two did not show up).
KNOWN_PANEL = {
    "routed": {
        # SONG lamp legs overlap the pot's pin 3 (P4-1 SONG item): D3 on
        # RV3.3, D16 on RV56.3, so SM_3V3 cannot reach the pin (spec §4.4)
        "unrouted SONG_A",
        "unrouted SONG_B",
    },
    "drc": {
        # jack past the edge: the 18 y 114 jacks' tip pad T lies past the
        # 0.5 mm edge clearance (P4-1 jack row, spec §4.4)
        "copper_edge_clearance CLOCK", "copper_edge_clearance GATE_A",
        "copper_edge_clearance GATE_B", "copper_edge_clearance IN_L",
        "copper_edge_clearance IN_R", "copper_edge_clearance MOD1_A",
        "copper_edge_clearance MOD1_B", "copper_edge_clearance MOD2_A",
        "copper_edge_clearance MOD2_B", "copper_edge_clearance MOD3_A",
        "copper_edge_clearance MOD3_B", "copper_edge_clearance MOD4_A",
        "copper_edge_clearance MOD4_B", "copper_edge_clearance OUT_L",
        "copper_edge_clearance OUT_R", "copper_edge_clearance PITCH_A",
        "copper_edge_clearance PITCH_B", "copper_edge_clearance RESET",
        # SONG lamp on its pot: D3's / D16's legs on RV3's / RV56's pins (P4-1 SONG item)
        "shorting_items SONG_A/SONG_A_L", "shorting_items SONG_B/SONG_B_L",
        # the admitted pairs: D4's legs on RV10's pins, D12's on RV48's
        # (P4-1, Bastian 2026-09-29, spec §4.4); pad to pad, no routed copper
        "clearance GATE_A_L/SOURCE_A", "shorting_items GATE_A_L/SOURCE_A",
        "shorting_items LVL_B_L/PAN_B",
    },
}

GATED_DRC = ("courtyards_overlap", "pth_inside_courtyard", "shorting_items", "clearance",
             "hole_clearance", "hole_to_hole", "copper_edge_clearance", "items_not_allowed",
             "tracks_crossing", "track_dangling", "via_dangling", "track_width",
             "annular_width", "drill_out_of_range", "via_diameter", "starved_thermal")
FRONT_REPORTED = ("courtyards_overlap", "pth_inside_courtyard")


def _key(s, ref):
    return s.ids.get(ref, ref)


def _judge(s, check, found):
    return CK.judge(s.known.get(check, set()), found)


def _drc(s, pcb_path, prefix):
    """(report text, error line or None). Cached per run on `s`."""
    cache = getattr(s, "_drc_cache", None)
    if cache and cache[0] == pcb_path:
        return cache[1], None
    rpt = prefix + "-drc.rpt"
    try:
        PP.drc(pcb_path + (".missing" if getattr(s, "drc_broken", False) else ""), rpt)
    except RuntimeError as e:
        return None, "kicad-cli wrote no report: %s" % str(e)[:200]
    if getattr(s, "drc_cut", False):
        # sabotage "drc_cut": the report loses its tail, as a crashed writer would
        txt = open(rpt, encoding="utf-8", errors="replace").read()
        with open(rpt, "w", encoding="utf-8") as fh:
            fh.write(txt[: len(txt) // 2])
    txt = open(rpt, encoding="utf-8", errors="replace").read()
    s._drc_cache = (pcb_path, txt)
    return txt, None


PLANE_NETS = ("GND", "SM_3V3")      # In1.Cu, In2.Cu (spec §1)
_ITEM_PAD_RE = re.compile(r"(?:PTH pad|Pad) (\S+) \[([^\]]+)\] of (\S+)")
_ITEM_ZONE_RE = re.compile(r"Zone \[([^\]]+)\]")
# A gap inside a routed net names tracks, no pad (probed 2026-10-01 with the
# "routed" sabotage): "@(109.7500 mm, 58.2000 mm): Track [SENSE_2] on B.Cu, ..."
_ITEM_TRACK_RE = re.compile(r"@\(([-\d.]+) mm, ([-\d.]+) mm\): (Track|Via|Arc) \[([^\]]+)\]")


def _saved_board(s, pcb_path):
    """The saved board, loaded once per run, its connectivity built: plane
    connectivity is read from the file kicad-cli judged, not from memory."""
    cache = getattr(s, "_saved_cache", None)
    if cache and cache[0] == pcb_path:
        return cache[1]
    b = pcbnew.LoadBoard(pcb_path)
    b.BuildConnectivity()
    s._saved_cache = (pcb_path, b)
    return b


def _plane_cut_off(s, pcb_path, block):
    """For an unconnected_items block whose items are all on one plane net:
    [(ref, pad, net)] of its pads that the saved board's connectivity joins
    to no zone (probed 2026-09-30: GetConnectedItems(pad) of RV3.3 holds the
    pad alone, RV59.3's holds its ZONE). [] for any other block."""
    pads = _ITEM_PAD_RE.findall(block)
    nets = {net for _n, net, _r in pads} | set(_ITEM_ZONE_RE.findall(block))
    if len(nets) != 1 or not nets <= set(PLANE_NETS):
        return []
    b = _saved_board(s, pcb_path)
    conn = b.GetConnectivity()
    out = []
    for num, net, ref in pads:
        fp = b.FindFootprintByReference(ref)
        pad = [p for p in fp.Pads() if str(p.GetNumber()) == num][0] if fp else None
        if pad is None:
            continue
        if not any(i.GetClass() == "ZONE" for i in conn.GetConnectedItems(pad)):
            out.append((ref, num, net))
    return sorted(set(out))


def check_routed(s, pcb_path, prefix):
    txt, err = _drc(s, pcb_path, prefix)
    if err:
        return False, err, []
    res = s.result
    if getattr(s, "routed_missing", False):
        txt = ""
    blocks = [(c, refs) for c, refs, _f in CK.drc_blocks(txt) if c == "unconnected_items"]
    summ = CK.report_summary(txt)
    if not summ["complete"]:
        return False, "the DRC report is incomplete, so unconnected items cannot be counted", []
    if summ["unconnected"] != len(blocks):
        return False, ("the report says %s unconnected pads but %d blocks were read"
                       % (summ["unconnected"], len(blocks))), []
    found, track_ends = {}, {}
    texts = [b for b in re.split(r"(?=^\[)", txt, flags=re.M) if b.startswith("[unconnected_items]")]
    for (_c, refs), block in zip(blocks, texts):
        why = "kicad-cli unconnected_items"
        cut = _plane_cut_off(s, pcb_path, block)
        if cut:
            # spec §4.4: a plane-net item is keyed by the pad cut off from its
            # plane, not by the partner kicad-cli pairs it with
            refs = sorted({r for r, _n, _net in cut})
            why = "cut off from its plane: %s" % ", ".join(
                "%s.%s [%s] (%s)" % (r, n, net, _key(s, r)) for r, n, net in cut)
        if refs:
            key = "unrouted %s" % "/".join(sorted(_key(s, r) for r in refs))
        else:
            # no pad in the block: a gap between tracks, keyed by its net
            # (PP.unconnected_by_net reads pads only, so its ends go in here)
            items = _ITEM_TRACK_RE.findall(block)
            for x, y, kind, net in items:
                track_ends.setdefault(net, set()).add("%s (%s, %s)" % (kind.lower(), x, y))
            nets = sorted({n for _x, _y, _k, n in items})
            key = "unrouted net %s" % ("/".join(nets) or "?")
            why = "kicad-cli unconnected_items between tracks, no pad"
        found[key] = why
    ok, details, nk = _judge(s, "routed", found)
    by_net = PP.unconnected_by_net(prefix + "-drc.rpt") if blocks else {}
    for n, ends in track_ends.items():
        by_net.setdefault(n, set()).update(ends)
    details += ["unrouted on %s: %s" % (n, ", ".join(sorted(p))) for n, p in sorted(by_net.items())]
    if res is None or res.conflicts:
        ok = False
        details.insert(0, "router left %s nets in conflict" % (None if res is None else res.conflicts))
    if res is not None and res.failed:
        details.insert(0, "router failed nets: %s" % ", ".join(res.failed))
    return ok, ("%d unconnected items (%d known), router %s rounds, %s conflicts, %s failed, %.1f s"
                % (len(found), nk, getattr(res, "iterations", "?"), getattr(res, "conflicts", "?"),
                   len(getattr(res, "failed", [])), s.seconds)), details


def check_drc(s, pcb_path, prefix):
    txt, err = _drc(s, pcb_path, prefix)
    if err:
        return False, err, []
    summ = CK.report_summary(txt)
    blocks = CK.drc_blocks(txt)
    n_viol = sum(1 for c, _r, _f in blocks if c != "unconnected_items")
    n_tracks = sum(1 for t in s.board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T)
    if not summ["complete"] or summ["violations"] != n_viol or not n_tracks:
        return False, ("the DRC report was not read: complete %s, report says %s violations, %d parsed, "
                       "%d tracks on the board" % (summ["complete"], summ["violations"], n_viol, n_tracks)), []
    found, front_rep = {}, []
    front = set(s.front)
    for cls, refs, first in blocks:
        if cls not in GATED_DRC:
            continue
        if cls in FRONT_REPORTED and refs and set(refs) <= front:
            front_rep.append("%s %s" % (cls, "/".join(sorted(_key(s, r) for r in refs))))
            continue
        key = "%s %s" % (cls, "/".join(sorted(_key(s, r) for r in refs)))
        found.setdefault(key, "kicad-cli, first item %s" % first)
    ok, details, nk = _judge(s, "drc", found)
    details += ["reported, not gated: " + f for f in sorted(set(front_rep))]
    counts = {}
    for cls, _r, _f in blocks:
        if cls not in GATED_DRC and cls != "unconnected_items":
            counts[cls] = counts.get(cls, 0) + 1
    others = ", ".join("%s %d" % kv for kv in sorted(counts.items())) or "none"
    return ok, ("gated: %d items (%d known), %d front courtyard items reported, %d violations in the "
                "report, %d tracks; not gated: %s" % (len(found), nk, len(front_rep), n_viol, n_tracks,
                                                       others)), details


def render(s, pcb_path, prefix):
    """Front and back; a stale PNG from an earlier run is deleted first, so
    an existing file is this run's render."""
    if getattr(s, "skip_render", False):
        return True, "skipped (guard sabotage run)", []
    made, bad = [], []
    for side in ("top", "bottom"):
        png = "%s-%s.png" % (prefix, side)
        if os.path.exists(png):
            os.remove(png)
        rc, out = PP.render(pcb_path, png, side)
        if rc or not os.path.exists(png):
            bad.append("render %s rc=%d: %s" % (side, rc, out[-300:]))
        else:
            made.append(os.path.basename(png))
    return not bad, "rendered %s" % (", ".join(made) or "nothing"), bad


STEPS = [("routed", check_routed), ("drc", check_drc), ("render", render)]


def run(s, pcb_path, prefix):
    s.known = {k: set(v) for k, v in KNOWN_PANEL.items()} if not s.known else s.known
    s._drc_cache = None
    s._saved_cache = None
    return CK.run_steps(STEPS, s, pcb_path, prefix)


def _fp(board, ref):
    return board.FindFootprintByReference(ref)


def _sab_routed(s):
    """The longest routed track of SENSE_2 removed: a connection opens (the
    net is 40 mm, has no plane, and a routed tree has no second path; a
    terminal stub could sit inside its own pad, the longest track cannot)."""
    t = max((t for t in s.board.GetTracks()
             if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetname() == "SENSE_2"),
            key=lambda t: (t.GetLength(), -t.GetStart().x, -t.GetStart().y))
    s.board.Delete(t)


def _sab_routed_missing(s):
    s.routed_missing = True


def _decoupler(board):
    """The first 100 nF by reference (C10 on the 2026-09-30 board, U_MUX6's;
    C1 is the 10 uF bulk cap)."""
    return sorted((f for f in board.GetFootprints()
                   if f.GetReference().startswith("C") and f.GetValue() == "100n"),
                  key=lambda f: f.GetReference())[0]


def _sab_drc(s):
    """A GND track 0.1 mm beside a decoupler's rail pad (P4-1's near miss)."""
    from gen import kipcb
    fp = _decoupler(s.board)
    bb = [p for p in fp.Pads() if str(p.GetNumber()) == "1"][0].GetBoundingBox()
    x = pcbnew.ToMM(bb.GetRight()) + 0.1 + 0.125
    kipcb.add_track(s.board, "B.Cu", 0.25, "GND",
                    [(x, pcbnew.ToMM(bb.GetTop()) - 1.0), (x, pcbnew.ToMM(bb.GetBottom()) + 1.0)])


def _sab_drc_missing(s):
    s.drc_broken = True


def _sab_drc_cut(s):
    s.drc_cut = True


SABOTAGES = {"routed": _sab_routed, "routed_missing": _sab_routed_missing,
             "drc": _sab_drc, "drc_missing": _sab_drc_missing, "drc_cut": _sab_drc_cut}
TURNS_RED = {"routed": "routed", "routed_missing": "routed", "drc": "drc",
             "drc_missing": "drc", "drc_cut": "drc"}
WHY = {"routed": "unrouted on SENSE_2", "routed_missing": "incomplete",
       "drc": "[NEW]", "drc_missing": "wrote no report", "drc_cut": "was not read"}


def sabotage(s, name):
    if name not in SABOTAGES:
        raise SystemExit("unknown sabotage %r; known: %s" % (name, ", ".join(sorted(SABOTAGES))))
    if not s.known:
        s.known = {k: set(v) for k, v in KNOWN_PANEL.items()}
    SABOTAGES[name](s)
    print("SABOTAGED: %s" % name)
