#!/usr/bin/env python3
"""The P4-2 checks (spec §5). Every step measures the saved board, not what
the router reported. Each step owns its thresholds and net sets (none is
imported from rules.py, route.py or gen/route.py). Known panel violations
(spec §4.4) print as known; an unlisted failure is red, and so is a listed
one that no longer fails. A step that examined nothing is red."""
import json
import math
import os
import re
import shutil
import subprocess
import tempfile

import pcbnew

import check_kit as CK
import place as P
from gen import ksexp
from gen import pcb_proof as PP
from gen import place as PL

# Filled from the first converged full run (Task 6, 2026-10-01), restricted by
# the owner's rule (spec §4.4) to the jack row, the SONG clusters, the
# GATE_A_L/SOURCE_A and LVL_B_L/PAN_B pairs and the jack-zone pairs
# CEIL_L/OUT_R and SHIFTBTN_L/IN_L. The jack-zone pairs have no routed or drc
# item; they are found items of the audio step (Task 7, spec §5.4).
# A listed "routed" key is admitted only from a plane cut-off block (below).
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
    "audio": {
        # jack zones (spec §4.2.2): placement already puts aggressor pads
        # within 10 mm of these jacks' victim pads, keyed by the panel ids
        # of the jack and the panel LED involved, sorted like every key
        # (Bastian, 2026-09-30: the pairs CEIL_L/OUT_R and SHIFTBTN_L/IN_L)
        "CEIL_L/OUT_R",         # J18.T OUT_R: R34.1 [LED16], R34.2 and D17.2 [LED16_A]
        "IN_L/SHIFTBTN_L",      # J1.T IN_L: D1.2 [LED0_A]
    },
    "silk": set(),  # silk text entries the panel pass must clear; expected none (P4-3 spec §5.1)
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
HERE_RC = os.path.dirname(os.path.abspath(__file__))
KICAD_DIR = os.path.join(HERE_RC, "kicad")
LIB_DIR = os.path.normpath(os.path.join(HERE_RC, "..", "lib"))
SCH = os.path.join(KICAD_DIR, "reva.kicad_sch")
PATHS_EMPTY = "paths measured nothing: no component read from KiCad's netlist"
PARITY_EMPTY = "parity positive control found nothing"
PARITY_CONTROL = ("R1", "LCSC", "C0")   # this field, changed on a copy, must show up
_NETLIST_PATHS = None                    # {ref: path}, per process: the schematic does not change in a run
_PARITY_CACHE = {}                       # link key -> parity items
_ITEM_PAD_RE = re.compile(r"(?:PTH pad|Pad) (\S+) \[([^\]]+)\] of (\S+)")
_ITEM_ZONE_RE = re.compile(r"Zone \[([^\]]+)\]")
# Copper items that name no part (probed 2026-10-01): a gap inside a routed
# net names tracks, "@(109.7500 mm, 58.2000 mm): Track [SENSE_2] on B.Cu, ...";
# a plane item reads "@(2.0000 mm, 9.2500 mm): Zone [SM_3V3] on In2.Cu, ...".
_ITEM_TRACK_RE = re.compile(r"@\(([-\d.]+) mm, ([-\d.]+) mm\): (Track|Via|Arc|Zone) \[([^\]]+)\]")
_CLASS_RE = re.compile(r"^\[([a-z0-9_]+)\]")    # as check_kit's, so the block lists align


def _block_nets(block):
    """The nets of a block's part-less copper items, sorted, "?" if none."""
    return "/".join(sorted({n for _x, _y, _k, n in _ITEM_TRACK_RE.findall(block)})) or "?"


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
    srcs, track_ends = {}, {}           # key -> [(why, is a plane cut-off)], one per block
    texts = [b for b in re.split(r"(?=^\[)", txt, flags=re.M) if b.startswith("[unconnected_items]")]
    for (_c, refs), block in zip(blocks, texts):
        why = "kicad-cli unconnected_items, not a plane cut-off"
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
            for x, y, kind, net in _ITEM_TRACK_RE.findall(block):
                track_ends.setdefault(net, set()).add("%s (%s, %s)" % (kind.lower(), x, y))
            key = "unrouted net %s" % _block_nets(block)
            why = "kicad-cli unconnected_items between tracks, no pad"
        srcs.setdefault(key, []).append((why, bool(cut)))
    found = {k: " + ".join(w for w, _c in v) for k, v in srcs.items()}
    # A listed key stands for one plane cut-off and nothing else: a key that
    # also (or only) comes from another block, e.g. a signal gap at a SONG
    # pot's pin, is judged as not listed, so it is red.
    not_cut = {k for k, v in srcs.items() if not all(c for _w, c in v)}
    known = s.known.get("routed", set()) - not_cut
    ok, details, _nk = CK.judge(known, found)
    dup = sorted(k for k, v in srcs.items() if len(v) > 1)
    if dup:
        ok = False
        details = ["%s: %d blocks under one key" % (k, len(srcs[k])) for k in dup] + details
    n_known = sum(len(v) for k, v in srcs.items() if k in known)
    by_net = PP.unconnected_by_net(prefix + "-drc.rpt") if blocks else {}
    for n, ends in track_ends.items():
        by_net.setdefault(n, set()).update(ends)
    details += ["unrouted on %s: %s" % (n, ", ".join(sorted(p))) for n, p in sorted(by_net.items())]
    if res is None or res.conflicts:
        ok = False
        details.insert(0, "router left %s nets in conflict" % (None if res is None else res.conflicts))
    if res is not None and res.failed:
        ok = False
        details.insert(0, "router failed nets: %s" % ", ".join(res.failed))
    return ok, ("%d unconnected items (%d known), router %s rounds, %s conflicts, %s failed, %.1f s"
                % (len(blocks), n_known, getattr(res, "iterations", "?"), getattr(res, "conflicts", "?"),
                   len(getattr(res, "failed", [])), s.seconds)), details


def check_drc(s, pcb_path, prefix):
    txt, err = _drc(s, pcb_path, prefix)
    if err:
        return False, err, []
    summ = CK.report_summary(txt)
    blocks = CK.drc_blocks(txt)
    n_viol = sum(1 for c, _r, _f in blocks if c != "unconnected_items")
    n_tracks = sum(1 for t in _saved_board(s, pcb_path).GetTracks() if t.Type() == pcbnew.PCB_TRACE_T)
    if not summ["complete"] or summ["violations"] != n_viol or not n_tracks:
        return False, ("the DRC report was not read: complete %s, report says %s violations, %d parsed, "
                       "%d tracks on the board" % (summ["complete"], summ["violations"], n_viol, n_tracks)), []
    texts = [b for b in re.split(r"(?=^\[)", txt, flags=re.M) if _CLASS_RE.match(b)]
    found, front_rep = {}, []
    front = set(s.front)
    for (cls, refs, first), block in zip(blocks, texts):
        if cls not in GATED_DRC:
            continue
        if cls in FRONT_REPORTED and refs and set(refs) <= front:
            front_rep.append("%s %s" % (cls, "/".join(sorted(_key(s, r) for r in refs))))
            continue
        if refs:
            key = "%s %s" % (cls, "/".join(sorted(_key(s, r) for r in refs)))
        else:
            # copper items only (tracks, vias, zones): keyed by their nets, so
            # N such items do not collapse into one anonymous key
            key = "%s net %s" % (cls, _block_nets(block))
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


# The check's own values (spec §2, §4.2.2, §4.3); not imported from rules.py
# or route.py. Changing one of these is a spec change.
AUDIO_MM = 10.0            # spec §2.1: victim to aggressor copper, same layer
EDGE_MM = 3.0              # spec §5.4: across a zone edge, inside routed copper to the item outside
LR_MM = 2.0                # spec §2.5: L to R copper, same layer
SENSE_FACTOR = 1.3         # spec §2.6: SENSE copper <= 1.3 x MST of its pads
ZONE_PITCH_MM = 2.54       # spec §4.2.2: U_SM pads within one pin pitch form a group
ZONE_MARGIN_MM = 2.54      # spec §4.2.2: a zone is its box grown by one pin pitch
PF_PER_MM = 0.1            # estimate, not measured (spec §3)
MODULE_REF = "U_SM"
VICTIMS = ("OUT_L", "OUT_R", "IN_L", "IN_R")                       # spec §2.4
AGGRESSORS = tuple(["LED%d" % n for n in range(19)] + ["LED%d_A" % n for n in range(19)]
                   + ["SR_CLK", "SR_DATA", "SR_LATCH", "SR_DIN",
                      "SD_CK", "SD_CMD", "SD_D0", "SD_D1", "SD_D2", "SD_D3"])   # spec §2.3
LR = (("OUT_L", "OUT_R"), ("IN_L", "IN_R"))
SENSE = ("SENSE_0", "SENSE_1", "SENSE_2", "SENSE_3")
RULES = {"min_track_width": 0.25, "min_via_diameter": 0.6, "min_through_hole_diameter": 0.3,
         "min_clearance": 0.2, "min_copper_edge_clearance": 0.5}       # spec §3, §4.3
PLANES = (("In1.Cu", pcbnew.In1_Cu, "GND"), ("In2.Cu", pcbnew.In2_Cu, "SM_3V3"))   # spec §1
PLANE_VIA_KEEPOFF_MM = 1.0  # spec §4.2.8; reported only, never gated (controller ruling C11.1)
PIECE_MM = 0.5
LAYERS = {"F.Cu": pcbnew.F_Cu, "B.Cu": pcbnew.B_Cu}
# A step that measured nothing says so under its own prefix, never under
# the prefix of a measured violation (the _missing sabotages match these).
AUDIO_EMPTY = "audio measured nothing:"
LR_EMPTY = "L/R measured nothing:"
SENSE_EMPTY = "SENSE measured nothing:"


def _mm(v):
    return pcbnew.ToMM(v)


def _pad_box(pad):
    return PL.box(pad.GetBoundingBox())


def _box_gap(a, b):
    return math.hypot(max(b[0] - a[2], a[0] - b[2], 0.0), max(b[1] - a[3], a[1] - b[3], 0.0))


def zones(board):
    """The module exemption zones (spec §4.2.2), computed here independently
    of route.py: U_SM's pad centres clustered at one pin pitch, each
    cluster's box of centres grown by ZONE_MARGIN_MM."""
    sm = board.FindFootprintByReference(MODULE_REF)
    if sm is None:
        return []
    pts = sorted({(round(_mm(p.GetPosition().x), 4), round(_mm(p.GetPosition().y), 4)) for p in sm.Pads()})
    groups = []
    for p in pts:
        near = [g for g in groups if any(math.hypot(p[0] - q[0], p[1] - q[1]) <= ZONE_PITCH_MM + 1e-3 for q in g)]
        merged = [p] + [q for g in near for q in g]
        groups = [g for g in groups if g not in near] + [merged]
    return sorted((min(x for x, _ in g) - ZONE_MARGIN_MM, min(y for _, y in g) - ZONE_MARGIN_MM,
                   max(x for x, _ in g) + ZONE_MARGIN_MM, max(y for _, y in g) + ZONE_MARGIN_MM)
                  for g in groups)


def jack_zones(board):
    """The jack zones (spec §4.2.2), computed here independently of
    route.py: for every victim pad outside U_SM with aggressor pads closer
    than AUDIO_MM (edge to edge, pad bounding boxes), the box of that victim
    pad and those aggressor pads grown by ZONE_MARGIN_MM.
    [(ref, pad, net, [(ref, pad, net, gap)], rect)]."""
    victims, aggr = [], []
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        for pad in fp.Pads():
            n = pad.GetNetname()
            if n in VICTIMS and ref != MODULE_REF:
                victims.append((ref, str(pad.GetNumber()), n, _pad_box(pad)))
            elif n in AGGRESSORS:
                aggr.append((ref, str(pad.GetNumber()), n, _pad_box(pad)))
    out = []
    for ref, num, net, vb in sorted(victims):
        near = sorted((r, p, n, _box_gap(vb, b), b) for r, p, n, b in aggr if _box_gap(vb, b) < AUDIO_MM)
        if near:
            boxes = [vb] + [q[4] for q in near]
            out.append((ref, num, net, [q[:4] for q in near],
                        (min(q[0] for q in boxes) - ZONE_MARGIN_MM, min(q[1] for q in boxes) - ZONE_MARGIN_MM,
                         max(q[2] for q in boxes) + ZONE_MARGIN_MM, max(q[3] for q in boxes) + ZONE_MARGIN_MM)))
    return out


def _inside(x, y, rects):
    return any(l <= x <= r and t <= y <= b for l, t, r, b in rects)


def copper(board, nets, rects, inside=None):
    """{layer: [(net, kind, geom, half)]} for every copper item of `nets`
    outside `rects`: tracks as pieces <= PIECE_MM (a piece is kept if its
    midpoint is outside), vias as points on both outer layers (a through via
    spans them all; never judged by its GetLayerName), pads as their bounding
    boxes on each outer layer they are on (dropped if the centre is inside).
    geom is ((x1, y1), (x2, y2)) for pieces and vias (x2 = x1), (l, t, r, b)
    for pads. With `inside` (a dict of the same shape) the routed items the
    same test puts inside a rect -- track pieces and vias, never pads -- are
    collected there (spec §5.4, across a zone edge)."""
    out = {ln: [] for ln in LAYERS}
    want = set(nets)
    for t in board.GetTracks():
        n = t.GetNetname()
        if n not in want:
            continue
        if t.Type() == pcbnew.PCB_VIA_T:
            x, y = _mm(t.GetPosition().x), _mm(t.GetPosition().y)
            dest = out if not _inside(x, y, rects) else inside
            if dest is not None:
                for ln in LAYERS:
                    dest.setdefault(ln, []).append(
                        (n, "via", ((x, y), (x, y)), _mm(t.GetWidth(pcbnew.F_Cu)) / 2.0))
            continue
        ln = t.GetLayerName()
        if ln not in out:
            continue
        (x1, y1), (x2, y2) = (_mm(t.GetStart().x), _mm(t.GetStart().y)), (_mm(t.GetEnd().x), _mm(t.GetEnd().y))
        k = max(1, int(math.ceil(math.hypot(x2 - x1, y2 - y1) / PIECE_MM)))
        for i in range(k):
            a = (x1 + (x2 - x1) * i / k, y1 + (y2 - y1) * i / k)
            b = (x1 + (x2 - x1) * (i + 1) / k, y1 + (y2 - y1) * (i + 1) / k)
            dest = out if not _inside((a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0, rects) else inside
            if dest is not None:
                dest.setdefault(ln, []).append((n, "track", (a, b), _mm(t.GetWidth()) / 2.0))
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            n = pad.GetNetname()
            if n not in want:
                continue
            c = pad.GetPosition()
            if _inside(_mm(c.x), _mm(c.y), rects):
                continue
            for ln, lid in LAYERS.items():
                if pad.IsOnLayer(lid):
                    out[ln].append((n, "pad", _pad_box(pad), 0.0))
    return out


def _seg_pt(p, a, b):
    (px, py), (ax, ay), (bx, by) = p, a, b
    dx, dy = bx - ax, by - ay
    if dx == 0 and dy == 0:
        return math.hypot(px - ax, py - ay)
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def _crosses(s1, s2):
    """True if the two segments cross at a point inside both."""
    def orient(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    (a, b), (c, d) = s1, s2
    return orient(a, b, c) * orient(a, b, d) < 0 and orient(c, d, a) * orient(c, d, b) < 0


def _seg_seg(s1, s2):
    """Exact centre-line distance: 0 for a crossing pair, else the closest
    endpoint-to-segment approach (exact for two segments that do not cross)."""
    if _crosses(s1, s2):
        return 0.0
    return min(_seg_pt(s1[0], *s2), _seg_pt(s1[1], *s2), _seg_pt(s2[0], *s1), _seg_pt(s2[1], *s1))


def _seg_box(seg, box):
    """Exact distance from a segment to a filled box: 0 if an end lies in it
    or the segment crosses an edge, else the closest edge approach."""
    l, t, r, b = box
    if any(l <= x <= r and t <= y <= b for x, y in seg):
        return 0.0
    edges = (((l, t), (r, t)), ((r, t), (r, b)), ((r, b), (l, b)), ((l, b), (l, t)))
    return min(_seg_seg(seg, e) for e in edges)


def _dist(a, b):
    """Edge-to-edge distance between two items of copper()."""
    (_n1, k1, g1, h1), (_n2, k2, g2, h2) = a, b
    if k1 == "pad" and k2 == "pad":
        return _box_gap(g1, g2)
    if k1 == "pad":
        (k1, g1, h1), (k2, g2, h2) = (k2, g2, h2), (k1, g1, h1)
    if k2 == "pad":
        return max(0.0, _seg_box(g1, g2) - h1)
    return max(0.0, _seg_seg(g1, g2) - h1 - h2)


def min_distance(items_a, items_b, reach):
    """(distance, item_a, item_b) of the closest pair, found through
    'reach'-sized buckets; (inf, None, None) if none lies within reach."""
    def cells(item):
        _n, k, g, _h = item
        xs = [g[0], g[2]] if k == "pad" else [g[0][0], g[1][0]]
        ys = [g[1], g[3]] if k == "pad" else [g[0][1], g[1][1]]
        for cx in range(int(min(xs) // reach) - 1, int(max(xs) // reach) + 2):
            for cy in range(int(min(ys) // reach) - 1, int(max(ys) // reach) + 2):
                yield cx, cy
    grid = {}
    for it in items_b:
        for c in cells(it):
            grid.setdefault(c, []).append(it)
    best = (float("inf"), None, None)
    for a in items_a:
        seen = set()
        for c in cells(a):
            for b in grid.get(c, ()):
                if id(b) in seen:
                    continue
                seen.add(id(b))
                d = _dist(a, b)
                if d < best[0]:
                    best = (d, a, b)
    if best[0] > reach:
        return (float("inf"), None, None)
    return best


def _where(item):
    _n, k, g, _h = item
    x, y = ((g[0] + g[2]) / 2.0, (g[1] + g[3]) / 2.0) if k == "pad" else g[0]
    return "%s %s @(%.2f, %.2f)" % (item[0], k, x, y)


def _board_nets(board):
    return ({t.GetNetname() for t in board.GetTracks()}
            | {p.GetNetname() for f in board.GetFootprints() for p in f.Pads()})


def _all_zones(s, board):
    """(module zones, jack zones, every exemption rect), cached per board."""
    cache = getattr(s, "_zone_cache", None)
    if cache and cache[0] is board:
        return cache[1]
    mz, jz = zones(board), jack_zones(board)
    got = (mz, jz, mz + [z[4] for z in jz])
    s._zone_cache = (board, got)
    return got


def _audio(s, pcb_path):
    """(ok, line, details, per-victim lines), cached per run: report reuses it."""
    cache = getattr(s, "_audio_cache", None)
    if cache and cache[0] == pcb_path:
        return cache[1]
    got = _measure_audio(s, pcb_path)
    s._audio_cache = (pcb_path, got)
    return got


def _worst(pairs, reach):
    """The closest of min_distance over [(items_a, items_b)], per layer."""
    worst = (float("inf"), None, None)
    for a, b in pairs:
        d = min_distance(a, b, reach)
        if d[0] < worst[0]:
            worst = d
    return worst


def _measure_audio(s, pcb_path):
    board = _saved_board(s, pcb_path)
    nets = _board_nets(board)
    victims = list(VICTIMS)
    missing = [n for n in victims + list(AGGRESSORS) if n not in nets]
    if missing:
        return (False, "%s victims or aggressors absent from the board: %s" % (AUDIO_EMPTY, missing), [], [])
    mz, jz, rects = _all_zones(s, board)
    # Spec §5.4: each jack zone is a found item, judged against KNOWN_PANEL;
    # two zones under one key would hide the second one, so that is red.
    found, per_key = {}, {}
    for ref, num, net, near, z in jz:
        names = {_key(s, ref)} | {s.ids[r] for r, _p, _n, _g in near if r in s.ids}
        key = "/".join(sorted(names))
        msg = "jack zone %s.%s [%s]: %s; zone (%s)" % (
            ref, num, net, ", ".join("%s.%s [%s]%s %.2f mm" % (r, p, n, " (%s)" % s.ids[r] if r in s.ids else "", g)
                                     for r, p, n, g in near),
            ", ".join("%.2f" % v for v in z))
        found[key] = found[key] + " + " + msg if key in found else msg
        per_key[key] = per_key.get(key, 0) + 1
    ok, zdetails, nk = _judge(s, "audio", found)
    zdetails = ["%s: %d zones under one key" % (k, n) for k, n in sorted(per_key.items()) if n > 1] + zdetails
    ok = ok and all(n == 1 for n in per_key.values())
    bad, edge_bad, empty, lines = [], [], [], []
    a_in = {}
    ac = copper(board, AGGRESSORS, rects, inside=a_in)
    n_aggr = sum(len(v) for v in ac.values())
    for v in victims:
        v_in = {}
        vc = copper(board, [v], rects, inside=v_in)
        n_vic = sum(len(x) for x in vc.values())
        if not n_vic:
            empty.append("%s: no copper outside the exemption zones" % v)
            continue
        worst = _worst([(vc[ln], ac[ln]) for ln in LAYERS], AUDIO_MM + 1.0)
        if worst[1] is None:
            line = "%s: %d items, no aggressor copper within %.1f mm on its layers" % (v, n_vic, AUDIO_MM + 1.0)
        else:
            line = "%s: %d items, nearest aggressor %.3f mm: %s against %s" % (
                v, n_vic, worst[0], _where(worst[2]), _where(worst[1]))
            if worst[0] < AUDIO_MM - 1e-6:
                bad.append(line)
        lines.append(line)
        # Spec §5.4, across a zone edge: exactly one item of the pair inside
        # a zone (by the test above: a pad's centre, a via's centre, a track
        # piece's midpoint). An inside pad exempts the pair; inside routed
        # copper keeps EDGE_MM from the item outside.
        edge = _worst([(vc[ln], a_in.get(ln, [])) for ln in LAYERS]
                      + [(v_in.get(ln, []), ac[ln]) for ln in LAYERS], AUDIO_MM + 1.0)
        if edge[1] is None:
            eline = "%s across a zone edge: no pair within %.1f mm" % (v, AUDIO_MM + 1.0)
        else:
            eline = "%s across a zone edge: nearest %.3f mm: %s against %s" % (
                v, edge[0], _where(edge[2]), _where(edge[1]))
            if edge[0] < EDGE_MM - 1e-6:
                edge_bad.append(eline)
        lines.append(eline)
    if not n_aggr:
        empty.append("no aggressor copper outside the exemption zones")
    ok = ok and not bad and not edge_bad and not empty
    return (ok, ("%d victims, %d aggressors (%d items), %d module zones, %d jack zones (%d keys, %d known), "
                 "%d below %.1f mm, %d across a zone edge below %.1f mm"
                 % (len(victims), len(AGGRESSORS), n_aggr, len(mz), len(jz), len(found), nk, len(bad), AUDIO_MM,
                    len(edge_bad), EDGE_MM)),
            ["%s %s" % (AUDIO_EMPTY, e) for e in empty]
            + ["audio clearance below %.1f mm: %s" % (AUDIO_MM, b) for b in bad]
            + ["audio clearance across a zone edge below %.1f mm: %s" % (EDGE_MM, b) for b in edge_bad]
            + zdetails + lines, lines)


def check_audio(s, pcb_path, prefix):
    ok, line, details, _lines = _audio(s, pcb_path)
    return ok, line, details


def check_lr(s, pcb_path, prefix):
    pairs = [] if getattr(s, "lr_missing", False) else list(LR)
    if not pairs:
        return False, "%s no L/R pair examined" % LR_EMPTY, []
    board = _saved_board(s, pcb_path)
    _mz, _jz, rects = _all_zones(s, board)
    bad, empty, details = [], [], []
    for a, b in pairs:
        ca, cb = copper(board, [a], rects), copper(board, [b], rects)
        if not any(ca.values()) or not any(cb.values()):
            empty.append("%s %s/%s: no copper outside the zones" % (LR_EMPTY, a, b))
            continue
        worst = min((min_distance(ca[ln], cb[ln], LR_MM + 1.0) for ln in ca), key=lambda d: d[0])
        line = "%s/%s: %s" % (a, b, "farther than %.1f mm everywhere" % (LR_MM + 1.0) if worst[1] is None
                              else "%.3f mm, %s against %s" % (worst[0], _where(worst[1]), _where(worst[2])))
        details.append(line)
        if worst[1] is not None and worst[0] < LR_MM - 1e-6:
            bad.append(line)
    return not bad and not empty, "%d pairs, %d below %.1f mm" % (len(pairs), len(bad), LR_MM), \
        empty + ["L/R spacing below %.1f mm: %s" % (LR_MM, b) for b in bad] + details


def _sense_lines(s, pcb_path):
    """(bad, empty, lines) per SENSE net, measured on the saved board."""
    board = _saved_board(s, pcb_path)
    bad, empty, lines = [], [], []
    for n in SENSE:
        pts = [(_mm(p.GetPosition().x), _mm(p.GetPosition().y))
               for f in board.GetFootprints() for p in f.Pads() if p.GetNetname() == n]
        tracks = [t for t in board.GetTracks() if t.GetNetname() == n]
        length = sum(_mm(t.GetLength()) for t in tracks if t.Type() != pcbnew.PCB_VIA_T)
        vias = sum(1 for t in tracks if t.Type() == pcbnew.PCB_VIA_T)
        tree = CK.mst(pts)
        if len(pts) < 2 or tree <= 0 or length <= 0:
            empty.append("%s %s: %d pads, MST %.1f mm, copper %.1f mm" % (SENSE_EMPTY, n, len(pts), tree, length))
            continue
        f = length / tree
        line = ("%s: %.1f mm copper, MST %.1f mm over %d pads, factor %.2f, %d vias, ~%.0f pF track (estimate)"
                % (n, length, tree, len(pts), f, vias, length * PF_PER_MM))
        lines.append(line)
        if f > SENSE_FACTOR + 1e-9:
            bad.append(line)
    return bad, empty, lines


def check_sense(s, pcb_path, prefix):
    if getattr(s, "sense_missing", False):
        return False, "%s no SENSE net examined" % SENSE_EMPTY, []
    bad, empty, lines = _sense_lines(s, pcb_path)
    return not bad and not empty, "%d SENSE nets, %d over %.1f x MST" % (len(SENSE), len(bad), SENSE_FACTOR), \
        empty + ["SENSE length over %.1f x MST: %s" % (SENSE_FACTOR, b) for b in bad] + lines


def _pots(s):
    """The front refs whose panel hole is a pot (the hole kind, not the ref
    prefix), and the RV-prefixed front refs, for a cross-check."""
    holes = P._hole_index()
    by_hole = sorted(r for r in s.front if holes.get(s.ids.get(r), {}).get("kind") == "pot")
    by_prefix = sorted(r for r in s.front if r.startswith("RV"))
    return by_hole, by_prefix


def check_pot_keepout(s, pcb_path, prefix):
    pots, by_prefix = ([], []) if getattr(s, "pot_missing", False) else _pots(s)
    if not pots:
        return False, "no pot examined", []
    board = _saved_board(s, pcb_path)
    boxes = {r: PL.body_box(board.FindFootprintByReference(r)) for r in pots}
    bad = []
    if pots != by_prefix:
        bad.append("pot selection disagrees: %d by hole kind, %d by RV prefix" % (len(pots), len(by_prefix)))
    n = 0
    for t in board.GetTracks():
        is_via = t.Type() == pcbnew.PCB_VIA_T
        if not is_via and t.GetLayerName() != "F.Cu":
            continue
        n += 1
        a = (_mm(t.GetStart().x), _mm(t.GetStart().y))
        b = (_mm(t.GetEnd().x), _mm(t.GetEnd().y))
        half = _mm(t.GetWidth(pcbnew.F_Cu) if is_via else t.GetWidth()) / 2.0
        for r, box in sorted(boxes.items()):
            if _seg_box((a, b), box) - half <= 0.0:
                bad.append("F.Cu copper under a pot body: %s %s (%.2f, %.2f)-(%.2f, %.2f) in %s (%s)"
                           % (t.GetNetname(), "via" if is_via else "track", a[0], a[1], b[0], b[1], r, _key(s, r)))
    if not n:
        bad.append("no F.Cu track or via on the board, nothing measured")
    return not bad, "%d pots (%d by RV prefix), %d F.Cu tracks and vias, %d under a body" % (
        len(pots), len(by_prefix), n, sum(1 for x in bad if x.startswith("F.Cu copper"))), bad


def _chain_pts(chain):
    return [(_mm(chain.CPoint(i).x), _mm(chain.CPoint(i).y)) for i in range(chain.PointCount())]


def _pad_touches(pad, lid, outline):
    """True if the pad's copper on `lid` and the filled outline (a
    SHAPE_POLY_SET of one fractured outline) overlap: their intersection
    has area. Probed on the 2026-10-01 board: see the planes step."""
    pp = pad.GetEffectivePolygon(lid, pcbnew.ERROR_INSIDE)
    x = pcbnew.SHAPE_POLY_SET(outline)
    x.BooleanIntersection(pp)
    return x.Area() > 0


def check_planes(s, pcb_path, prefix):
    planes = () if getattr(s, "planes_missing", False) else PLANES
    if not planes:
        return False, "no plane examined", []
    board = _saved_board(s, pcb_path)
    bad, details, n_frag = [], [], 0
    for lname, lid, net in planes:
        zs = [z for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetname() == net and z.IsOnLayer(lid)]
        outs = []
        for z in zs:
            fill = z.GetFilledPolysList(lid)
            for i in range(fill.OutlineCount()):
                one = pcbnew.SHAPE_POLY_SET()
                one.AddOutline(fill.COutline(i))
                pts = _chain_pts(fill.COutline(i))
                outs.append((_mm(_mm(one.Area())), one, pts))
        if not outs:
            bad.append("no filled %s zone on %s" % (net, lname))
            continue
        outs.sort(key=lambda o: -o[0])
        pads = [(f.GetReference(), str(p.GetNumber()), p) for f in board.GetFootprints() for p in f.Pads()
                if p.GetNetname() == net and p.IsOnLayer(lid)]
        details.append("%s %s: main island %.1f mm2, %d other outline(s)" % (lname, net, outs[0][0], len(outs) - 1))
        for area, one, pts in outs[1:]:
            n_frag += 1
            xs, ys = [x for x, _ in pts], [y for _, y in pts]
            bb = PL.grow((min(xs), min(ys), max(xs), max(ys)), 0.1)
            touch = ["%s.%s" % (r, num) for r, num, p in pads
                     if PL.overlaps(_pad_box(p), bb) and _pad_touches(p, lid, one)]
            where = "%s %s fragment %.2f mm2 at (%.2f, %.2f)-(%.2f, %.2f)" % (
                lname, net, area, min(xs), min(ys), max(xs), max(ys))
            if touch:
                details.append("%s, touches %s" % (where, ", ".join(touch)))
            else:
                bad.append("free island: %s touches no %s pad" % (where, net))
    nets = {net for _l, _i, net in planes}
    smd = sum(1 for f in board.GetFootprints() for p in f.Pads()
              if p.GetNetname() in nets and p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD)
    vias = [t for t in board.GetTracks() if t.Type() == pcbnew.PCB_VIA_T and t.GetNetname() in nets]
    details.append("%d SMD pads on the plane nets, %d plane-net vias" % (smd, len(vias)))
    if not smd:
        bad.append("no SMD pad on the plane nets, the stitching count measured nothing")
    if len(vias) < smd:
        bad.append("stitching: %d plane-net vias for %d SMD plane-net pads" % (len(vias), smd))
    details += _plane_via_report(board, nets, vias)
    return not bad, "%d planes, %d fragments, %d SMD plane-net pads, %d plane-net vias" % (
        len(planes), n_frag, smd, len(vias)), bad + details


def _plane_via_report(board, nets, vias):
    """Report only (controller ruling C11.1, never gated): every plane-net
    via whose copper comes within PLANE_VIA_KEEPOFF_MM of a plane-net
    through-hole pad's copper (a round pad as its circle, others as their
    box)."""
    tht = []
    for f in board.GetFootprints():
        for p in f.Pads():
            if p.GetNetname() in nets and p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                tht.append((f.GetReference(), str(p.GetNumber()), p.GetNetname(), _pad_box(p),
                            p.GetShape(pcbnew.F_Cu) == pcbnew.PAD_SHAPE_CIRCLE))
    out = []
    for v in vias:
        x, y = _mm(v.GetPosition().x), _mm(v.GetPosition().y)
        rv = _mm(v.GetWidth(pcbnew.F_Cu)) / 2.0
        for ref, num, net, (l, t, r, b), round_ in tht:
            if round_:
                d = math.hypot(x - (l + r) / 2.0, y - (t + b) / 2.0) - (r - l) / 2.0 - rv
            else:
                d = _box_gap((x, y, x, y), (l, t, r, b)) - rv
            if d < PLANE_VIA_KEEPOFF_MM:
                out.append("reported, not gated: %s via @(%.3f, %.3f) %.3f mm from %s.%s [%s] "
                           "(keep-off %.1f mm)" % (v.GetNetname(), x, y, max(d, 0.0), ref, num, net,
                                                   PLANE_VIA_KEEPOFF_MM))
    return out


# U_REG's heat copper (Bastian, 2026-10-01; docs/hardware/power-budget.md).
# The check's own values; not imported from route.py.
REG_REF = "U_REG"           # the AMS1117-3.3, +12V -> 3V3D, on B.Cu
REG_NET = "3V3D"
REG_MIN_MM2 = 200.0         # filled 3V3D B.Cu copper connected to the tab
REG_EMPTY = "reg_copper measured nothing:"
REG_SAB_MM = 3.0            # the reg_copper sabotage keeps the fill within this of the tab


def _reg_tab(board, ref):
    """U_REG's tab: its largest REG_NET pad on B.Cu (the SOT-223 tab is
    pad 2 at 2.0 x 3.8 mm beside pin 2 at 2.0 x 1.5 mm). None if absent."""
    fp = board.FindFootprintByReference(ref) if ref else None
    pads = [p for p in fp.Pads() if p.GetNetname() == REG_NET and p.IsOnLayer(pcbnew.B_Cu)] if fp else []
    if not pads:
        return None
    return max(pads, key=lambda p: p.GetEffectivePolygon(pcbnew.B_Cu, pcbnew.ERROR_INSIDE).Area())


def _outlines(fill):
    """Each outline of a filled SHAPE_POLY_SET, with its holes, on its own."""
    out = []
    for i in range(fill.OutlineCount()):
        one = pcbnew.SHAPE_POLY_SET()
        one.AddOutline(fill.COutline(i))
        for j in range(fill.HoleCount(i)):
            one.AddHole(fill.CHole(i, j))
        out.append(one)
    return out


def check_reg_copper(s, pcb_path, prefix):
    """The filled REG_NET copper on B.Cu whose outline overlaps U_REG's tab
    pad (intersection with area) is at least REG_MIN_MM2. The zone's layer
    is read through IsOnLayer and the fill through GetFilledPolysList(B_Cu):
    ZONE.GetLayerName() answers "F.Cu" for a B.Cu zone (probed 2026-10-01)."""
    ref = "" if getattr(s, "reg_missing", False) else REG_REF
    board = _saved_board(s, pcb_path)
    tab = _reg_tab(board, ref)
    if tab is None:
        return False, "%s no %s pad of %s on B.Cu" % (REG_EMPTY, REG_NET, REG_REF), []
    lid = pcbnew.B_Cu
    zs = [z for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetname() == REG_NET and z.IsOnLayer(lid)]
    area, n_out, n_touch, details = 0.0, 0, 0, []
    for z in zs:
        conn = "solid" if z.GetPadConnection() == pcbnew.ZONE_CONNECTION_FULL else "thermal spokes"
        details.append("%s zone on B.Cu: pad connection %s, clearance %.2f mm"
                       % (REG_NET, conn, _mm(z.GetLocalClearance())))
        for one in _outlines(z.GetFilledPolysList(lid)):
            n_out += 1
            a = _mm(_mm(one.Area()))
            if _pad_touches(tab, lid, one):
                n_touch += 1
                area += a
            else:
                details.append("%s B.Cu outline of %.2f mm2 not touching the tab" % (REG_NET, a))
    tab_mm2 = _mm(_mm(tab.GetEffectivePolygon(lid, pcbnew.ERROR_INSIDE).Area()))
    line = ("%s.%s [%s] tab %.2f mm2: %.1f mm2 of filled %s B.Cu copper on it (min %.1f), %d zone(s), "
            "%d of %d outline(s) touch it" % (REG_REF, tab.GetNumber(), REG_NET, tab_mm2, area, REG_NET,
                                              REG_MIN_MM2, len(zs), n_touch, n_out))
    bad = []
    if area < REG_MIN_MM2:
        bad.append("U_REG copper below %.1f mm2: %.1f mm2 (%d %s zone(s) on B.Cu)"
                   % (REG_MIN_MM2, area, len(zs), REG_NET))
    return not bad, line, bad + details


def check_rules_file(s, pcb_path, prefix):
    """The committed project file carries the rules (read and compared with
    RULES), and kicad-cli judges the routed board the same under it as under
    the project SaveBoard wrote beside `pcb_path`."""
    here = os.path.dirname(os.path.abspath(__file__))
    pro = os.path.join(here, "kicad", "reva.kicad_pro")
    if getattr(s, "rules_missing", False):
        pro = pro + ".missing"
    try:
        # sabotage "rules_file" hands in the project file's bytes instead
        raw = getattr(s, "rules_pro_bytes", None)
        if raw is None:
            with open(pro, "rb") as fh:
                raw = fh.read()
        rules = json.loads(raw.decode("utf-8"))["board"]["design_settings"]["rules"]
    except (OSError, ValueError, KeyError, TypeError) as e:
        return False, "the committed project file carries no rules: %s" % e, []
    bad = ["%s is %r in %s, not %r" % (k, rules.get(k), os.path.basename(pro), v)
           for k, v in sorted(RULES.items()) if rules.get(k) != v]
    txt, err = _drc(s, pcb_path, prefix)
    if err:
        return False, err, []
    d = tempfile.mkdtemp(prefix="rulesfile_")
    try:
        shutil.copyfile(pcb_path, os.path.join(d, "reva.kicad_pcb"))
        with open(os.path.join(d, "reva.kicad_pro"), "wb") as fh:
            fh.write(raw)
        try:
            PP.drc(os.path.join(d, "reva.kicad_pcb"), os.path.join(d, "r.rpt"))
        except RuntimeError as e:
            return False, "kicad-cli wrote no report under the committed project file: %s" % str(e)[:200], []
        mine_txt = open(os.path.join(d, "r.rpt"), encoding="utf-8", errors="replace").read()
    finally:
        shutil.rmtree(d, ignore_errors=True)
    for name, t in (("committed", mine_txt), ("saved", txt)):
        summ = CK.report_summary(t)
        if not summ["complete"]:
            return False, "the DRC report under the %s project file is incomplete" % name, bad
    # Spec §5.3: the gated counts (drc's classes and routed's
    # unconnected_items). kicad-cli's counts are not deterministic on a board
    # with crossing copper (probed 2026-10-01 on the lr-sabotaged board: 9 or
    # 11 clearance, 0-2 tracks_crossing over five runs under either project
    # file); on the clean board they were stable.
    gated = set(GATED_DRC) | {"unconnected_items"}
    mine, theirs = {}, {}
    for c, _r, _f in CK.drc_blocks(mine_txt):
        if c in gated:
            mine[c] = mine.get(c, 0) + 1
    for c, _r, _f in CK.drc_blocks(txt):
        if c in gated:
            theirs[c] = theirs.get(c, 0) + 1
    diff = sorted(c for c in set(mine) | set(theirs) if mine.get(c, 0) != theirs.get(c, 0))
    bad += ["class %s: %d under the committed pro, %d under the saved one"
            % (c, mine.get(c, 0), theirs.get(c, 0)) for c in diff]
    return not bad, "%d rule values checked, %d gated DRC classes compared (%d items)" % (
        len(RULES), len(set(mine) | set(theirs)), sum(mine.values())), bad


def report(s, pcb_path, prefix):
    """Never gated (spec §5.10)."""
    board = _saved_board(s, pcb_path)
    by_w, width_of, vias = {}, {}, {}
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            continue
        w = round(_mm(t.GetWidth()), 3)
        n, L = by_w.get(w, (0, 0.0))
        by_w[w] = (n + 1, L + _mm(t.GetLength()))
        width_of[t.GetNetname()] = max(width_of.get(t.GetNetname(), 0.0), w)
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            n = t.GetNetname()
            cls = "plane-net stitching" if n in PLANE_NETS else "%.2f mm nets" % width_of.get(n, 0.0)
            vias[cls] = vias.get(cls, 0) + 1
    lines = ["tracks %.2f mm: %d segments, %.1f mm" % (w, n, L) for w, (n, L) in sorted(by_w.items())]
    lines += ["vias on %s: %d" % kv for kv in sorted(vias.items())]
    res = s.result
    lines.append("router: %s rounds, %s conflicts, %s failed, %.1f s" % (
        getattr(res, "iterations", "?"), getattr(res, "conflicts", "?"), len(getattr(res, "failed", []) or []),
        s.seconds))
    _ok, _line, _det, alines = _audio(s, pcb_path)
    lines += ["audio " + a for a in alines]
    _bad, _empty, slines = _sense_lines(s, pcb_path)
    lines += ["sense " + x for x in slines]
    stats = getattr(res, "stats", None) or {}
    for n in VICTIMS + SENSE:
        st = stats.get(n)
        lines.append("router stats %s: %s" % (n, "none" if st is None else ", ".join(
            "%s %s" % (k, ("%.1f" % v) if isinstance(v, float) else v) for k, v in sorted(st.items()))))
    return True, "reported, never gates", lines


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


def _netlist_paths():
    """{ref: footprint path} from KiCad's own netlist export of the committed
    schematic: the component's sheetpath tstamps joined with its tstamps
    (P4-3 spec §3). Components without a footprint (J_SM1..4) are left out.
    This is the independent reader: it never calls sch_writer's uuid5."""
    global _NETLIST_PATHS
    if _NETLIST_PATHS is None:
        tmp = tempfile.mkdtemp(prefix="reva_net_")
        try:
            net = os.path.join(tmp, "reva.net")
            subprocess.run([ksexp.KICAD_CLI, "sch", "export", "netlist", "--format", "kicadsexpr",
                            "-o", net, SCH], capture_output=True)
            out = {}
            if os.path.exists(net):
                root = ksexp.parse_file(net)
                for c in ksexp.children(ksexp.child(root, "components"), "comp"):
                    if not ksexp.children(c, "footprint"):
                        continue
                    sheet = str(ksexp.child(ksexp.child(c, "sheetpath"), "tstamps")[1])
                    out[str(ksexp.child(c, "ref")[1])] = sheet + str(ksexp.child(c, "tstamps")[1])
            _NETLIST_PATHS = out
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    return dict(_NETLIST_PATHS)


def check_paths(s, pcb_path, prefix):
    want = {} if getattr(s, "paths_missing", False) else _netlist_paths()
    if not want:
        return False, PATHS_EMPTY, []
    have = {fp.GetReference(): fp.GetPath().AsString() for fp in s.board.GetFootprints()}
    bad = []
    for ref in sorted(set(want) | set(have)):
        if ref not in have:
            bad.append("%s: in KiCad's netlist, no footprint on the board" % ref)
        elif ref not in want:
            bad.append("%s: footprint with no component in KiCad's netlist" % ref)
        elif have[ref] != want[ref]:
            bad.append("%s: path %s, KiCad's netlist says %s" % (ref, have[ref] or "(none)", want[ref]))
    return not bad, "%d footprints against KiCad's netlist, %d wrong" % (len(have), len(bad)), bad


def _link_key(board):
    """What schematic parity depends on: footprint ids, fields, DNP, pad nets."""
    key = []
    for fp in sorted(board.GetFootprints(), key=lambda f: f.GetReference()):
        fields = tuple(sorted((f.GetName(), f.GetText()) for f in fp.GetFields()))
        nets = tuple(sorted((str(p.GetNumber()), p.GetNetname()) for p in fp.Pads()))
        key.append((fp.GetReference(), fp.GetFPIDAsString(), fp.IsDNP(), fields, nets))
    return tuple(key)


def _parity_items(pcb_path, with_schematic=True):
    """kicad-cli's schematic-parity items for a copy of `pcb_path` laid out as
    the committed project (schematic, project file, lib tables, hardware/lib),
    or None when kicad-cli wrote no report."""
    tmp = tempfile.mkdtemp(prefix="reva_parity_")
    try:
        kd = os.path.join(tmp, "hardware", "reva", "kicad")
        os.makedirs(kd)
        for name in sorted(os.listdir(KICAD_DIR)):
            sch = name.endswith(".kicad_sch")
            if (sch and with_schematic) or name.endswith(".kicad_pro") or name.endswith("-lib-table"):
                shutil.copyfile(os.path.join(KICAD_DIR, name), os.path.join(kd, name))
        shutil.copytree(LIB_DIR, os.path.join(tmp, "hardware", "lib"))
        board = os.path.join(kd, "reva.kicad_pcb")
        shutil.copyfile(pcb_path, board)
        out = os.path.join(tmp, "parity.json")
        subprocess.run([ksexp.KICAD_CLI, "pcb", "drc", "--schematic-parity", "--format", "json",
                        "-o", out, board], capture_output=True)
        if not os.path.exists(out):
            return None
        with open(out, encoding="utf-8") as fh:
            return json.load(fh).get("schematic_parity", [])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# The JSON report's item lines are localised (probed 2026-10-02, German
# locale: "Durchsteckpad 7 [<no net>] von J_PWR"), unlike the text report's.
_PARITY_REF_RE = re.compile(r"(?:Footprint|of|von) (\S+)")


def _parity_lines(items):
    out = []
    for it in items:
        refs = sorted({m for x in it.get("items", []) for m in _PARITY_REF_RE.findall(x.get("description", ""))})
        out.append("parity item %s on %s" % (it.get("type"), "/".join(refs) or "?"))
    return sorted(out)


def check_parity(s, pcb_path, prefix):
    with_sch = not getattr(s, "parity_missing", False)
    key = (with_sch, _link_key(s.board))
    if key not in _PARITY_CACHE:
        items = _parity_items(pcb_path, with_sch)
        # positive control: the same board with one field changed must show it
        ctrl_pcb = prefix + "-parity-control.kicad_pcb"
        b = pcbnew.LoadBoard(pcb_path)
        ref, field, val = PARITY_CONTROL
        b.FindFootprintByReference(ref).SetField(field, val)
        pcbnew.SaveBoard(ctrl_pcb, b)
        control = _parity_items(ctrl_pcb, with_sch)
        _PARITY_CACHE[key] = (items, control)
    items, control = _PARITY_CACHE[key]
    if items is None or not control:
        return False, "%s (control %s)" % (PARITY_EMPTY, "no report" if control is None else "0 items"), []
    lines = _parity_lines(items)
    return not lines, "schematic parity: %d items (positive control %d)" % (len(lines), len(control)), lines


# --- silkscreen (P4-3 spec §4.3, §5.1) ---
SILK_MIN_MM = 1.0       # master plan working rule 3 (the check's own copy, not silk.py's)
SILK_STROKE_MIN_MM = 0.15   # rule 3's stroke (ruling R8; the check's own copy, not silk.py's)
SILK_TEXT_CLASSES = ("silk_over_copper", "silk_overlap")
# every silk class: a report with none of them was not read for silk (ruling R7)
SILK_CLASSES = SILK_TEXT_CLASSES + ("silk_edge_clearance",)
# kicad-cli 10.0.5 item lines for silk text (probed 2026-10-02 on the routed
# board; the rule line is localised, the item lines are not):
#   @(281.9400 mm, 73.6100 mm): Reference field of RV64
#   @(117.8800 mm, 70.6000 mm): Footprint text of U_SM (INSTALL ON
#   THIS SIDE)                      <- the text's own line break, not an item line
# Silk graphics read "@(...): Segment of U_SM on B.Silkscreen" (also Arc,
# Polygon), which CK._REF_RE keys; it misses the footprint text's owner (the
# text follows it), so _TEXT_OWNER_RE adds the owner of every text item.
_TEXT_ITEM_RE = re.compile(r"^\s*@\([^)]*\): (?:Reference field|Value field|Footprint text|Text) ", re.M)
_TEXT_OWNER_RE = re.compile(r"^\s*@\([^)]*\): (?:Reference field|Value field|Footprint text) of "
                            r"([A-Za-z_]+[A-Za-z_0-9]*)", re.M)
SILK_EMPTY = "silk_height measured nothing: no visible silkscreen text"
FRONT_EMPTY = "silk_front measured nothing: no front footprint"
CLEAR_EMPTY = "silk_clear measured nothing: no silk block in the DRC report"
ROOM_EMPTY = "no_room measured nothing: no back footprint"


def _silk_texts(board):
    """(ref, kind, height mm, side, stroke mm) of every visible text on F/B.Silkscreen:
    every field (reference, value and the schematic fields link_part adds,
    which are hidden; one made visible is measured here) and every
    footprint text."""
    out = []
    for fp in board.GetFootprints():
        items = [(f.GetName().lower(), f) for f in fp.GetFields()]
        items += [("text", it) for it in fp.GraphicalItems() if isinstance(it, pcbnew.PCB_TEXT)]
        for kind, t in items:
            if t.IsVisible() and t.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                out.append((fp.GetReference(), kind, pcbnew.ToMM(t.GetTextSize().y),
                            "B" if t.GetLayer() == pcbnew.B_SilkS else "F",
                            pcbnew.ToMM(t.GetTextThickness())))
    return out


def check_silk_height(s, pcb_path, prefix):
    """Rule 3 on every visible silk text: height and stroke."""
    texts = [] if getattr(s, "silk_height_missing", False) else _silk_texts(s.board)
    if not texts:
        return False, SILK_EMPTY, []
    low = sorted(t for t in texts if t[2] < SILK_MIN_MM - 1e-6)
    thin = sorted(t for t in texts if t[4] < SILK_STROKE_MIN_MM - 1e-6)
    details = ["silk text below %.1f mm: %s %s %.2f mm" % (SILK_MIN_MM, r, k, h) for r, k, h, _s, _w in low]
    details += ["silk stroke below %.2f mm: %s %s %.2f mm" % (SILK_STROKE_MIN_MM, r, k, w)
                for r, k, _h, _s, w in thin]
    return (not details, "%d silk texts, %d below %.1f mm, %d with a stroke below %.2f mm"
            % (len(texts), len(low), SILK_MIN_MM, len(thin), SILK_STROKE_MIN_MM), details)


def check_silk_front(s, pcb_path, prefix):
    front = [] if getattr(s, "silk_front_missing", False) else \
        [fp for fp in s.board.GetFootprints() if not fp.IsFlipped()]
    if not front:
        return False, FRONT_EMPTY, []
    shown = sorted(fp.GetReference() for fp in front if fp.Reference().IsVisible())
    return (not shown, "%d front footprints, %d with a visible reference" % (len(front), len(shown)),
            ["visible front reference: %s" % r for r in shown])


def check_silk_clear(s, pcb_path, prefix):
    """KiCad's DRC judges silk text (spec §5.1). Examined nothing (ruling R7):
    the report holds no silk block of any class, text or graphic, e.g. the
    silk classes set to ignore, or the blocks not read."""
    texts = _silk_texts(s.board)
    txt, err = _drc(s, pcb_path, prefix)
    if err:
        return False, err, []
    blocks = re.split(r"(?=^\[)", txt, flags=re.M)
    if getattr(s, "silk_clear_missing", False):
        # sabotage: the report's silk blocks are not read
        blocks = [b for b in blocks if not b.startswith(tuple("[%s]" % c for c in SILK_CLASSES))]
    n_silk = sum(1 for b in blocks if b.startswith(tuple("[%s]" % c for c in SILK_CLASSES)))
    if not n_silk:
        return False, "%s (%d visible silk texts on the board)" % (CLEAR_EMPTY, len(texts)), []
    found, graphic = {}, 0
    for block in blocks:
        m = re.match(r"^\[([a-z0-9_]+)\]", block)
        if not m or m.group(1) not in SILK_TEXT_CLASSES:
            continue
        if not _TEXT_ITEM_RE.search(block):
            graphic += 1            # footprint graphics only: counted, not gated (spec §4.3)
            continue
        refs = sorted(set(CK._REF_RE.findall(block)) | set(_TEXT_OWNER_RE.findall(block)))
        found["%s %s" % (m.group(1), "/".join(refs))] = "silk text over a pad or other silk"
    ok, details, n_known = _judge(s, "silk", found)
    return ok, ("%d visible silk texts, %d silk blocks in the report, %d text entries (%d known), "
                "%d graphic-only entries not gated" % (len(texts), n_silk, len(found), n_known, graphic)), details


def check_no_room(s, pcb_path, prefix):
    import silk as SK
    back = [] if getattr(s, "no_room_missing", False) else \
        [fp for fp in s.board.GetFootprints() if fp.IsFlipped()]
    if not back:
        return False, ROOM_EMPTY, []
    found = {fp.GetReference(): "reference hidden, no free spot"
             for fp in back if not fp.Reference().IsVisible()}
    for fp in back:
        for it in fp.GraphicalItems():
            if isinstance(it, pcbnew.PCB_TEXT) and it.GetLayer() == pcbnew.B_SilkS and not it.IsVisible():
                found[fp.GetReference() + ":text"] = "footprint text hidden, no free spot"
    known = set(SK.NO_ROOM) | set(getattr(s, "extra_no_room", ()))
    ok, details, n_known = CK.judge(known, found)
    details = [d.replace("[NEW]", "[NEW] hidden back reference not in NO_ROOM") for d in details]
    return ok, "%d back footprints, %d hidden (%d listed)" % (len(back), len(found), n_known), details


STEPS =[("routed", check_routed), ("drc", check_drc), ("rules_file", check_rules_file),
         ("audio", check_audio), ("lr", check_lr), ("sense", check_sense),
         ("pot_keepout", check_pot_keepout), ("planes", check_planes), ("reg_copper", check_reg_copper),
         ("paths", check_paths), ("parity", check_parity),
         ("silk_height", check_silk_height), ("silk_front", check_silk_front),
         ("silk_clear", check_silk_clear), ("no_room", check_no_room),
         ("report", report),
         ("render", render)]


def run(s, pcb_path, prefix):
    s.known = {k: set(v) for k, v in KNOWN_PANEL.items()} if not s.known else s.known
    s._drc_cache = None
    s._saved_cache = None
    s._zone_cache = None
    s._audio_cache = None
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


def _sab_routed_song(s):
    """A signal gap at a listed SONG pot: every M0_CH2 track with an end on
    RV3.2 (SONG_A's wiper) removed, so the pad stands alone. kicad-cli pairs
    the pad with the net's nearest track end, and the block keys as
    "unrouted SONG_A", the listed plane cut-off's key (the Task 6 review's
    gate hole: before the fix that block merged into the listed key, green)."""
    fp = _fp(s.board, "RV3")
    pad = [p for p in fp.Pads() if str(p.GetNumber()) == "2"][0]
    gone = [t for t in s.board.GetTracks()
            if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetname() == pad.GetNetname()
            and (pad.HitTest(t.GetStart()) or pad.HitTest(t.GetEnd()))]
    for t in gone:
        s.board.Delete(t)


def _decouplers(board):
    """The 100 nF caps by reference (C10 first on the 2026-09-30 board,
    U_MUX6's; C1 is the 10 uF bulk cap)."""
    return sorted((f for f in board.GetFootprints()
                   if f.GetReference().startswith("C") and f.GetValue() == "100n"),
                  key=lambda f: f.GetReference())


SAB_DRC_KEEP_MM = 0.25     # the drc sabotage's loop keeps clearance + 0.05 mm from foreign copper


def _foreign(board, layer, net, skip):
    """copper()-shaped items of every net but `net` on `layer` (tracks on
    it, every via, pads on it), minus the pads in `skip` ((ref, pad number)
    pairs: pcbnew hands out a new proxy per call, so identity cannot tell)."""
    lid = LAYERS[layer]
    out = []
    for t in board.GetTracks():
        if t.GetNetname() == net:
            continue
        a, b = (_mm(t.GetStart().x), _mm(t.GetStart().y)), (_mm(t.GetEnd().x), _mm(t.GetEnd().y))
        if t.Type() == pcbnew.PCB_VIA_T:
            out.append((t.GetNetname(), "via", (a, a), _mm(t.GetWidth(pcbnew.F_Cu)) / 2.0))
        elif t.GetLayerName() == layer:
            out.append((t.GetNetname(), "track", (a, b), _mm(t.GetWidth()) / 2.0))
    for f in board.GetFootprints():
        for p in f.Pads():
            if p.GetNetname() != net and p.IsOnLayer(lid) and (f.GetReference(), str(p.GetNumber())) not in skip:
                out.append((p.GetNetname(), "pad", _pad_box(p), 0.0))
    return out


def _sab_drc(s):
    """A GND track 0.1 mm beside a decoupler's rail pad (P4-1's near miss).
    It leaves the decoupler's own GND pad and returns into it over a small
    loop, so neither end dangles and it is no island. The loop goes right of
    the rail pad, else left, at the first 100 nF whose loop keeps
    SAB_DRC_KEEP_MM from all foreign copper but that rail pad. On the
    2026-10-01 board that is C12: C10's loops ran into an M6_CH1 via (right)
    and an M6_CH5 track (left), and kicad-cli's report on crossing copper
    varies from run to run (Task 8: once the clearance item was not keyed
    by C10 at all). The clearance item is the only new one. The chosen cap's
    ref goes on `s.sab_ref`; `why()` puts it into WHY["drc"]."""
    from gen import kipcb
    for fp in _decouplers(s.board):
        side = fp.GetLayerName()
        rail = [p for p in fp.Pads() if str(p.GetNumber()) == "1"][0]
        gnd = [p for p in fp.Pads() if p.GetNetname() == "GND"][0]
        bb = rail.GetBoundingBox()
        gx, gy = pcbnew.ToMM(gnd.GetPosition().x), pcbnew.ToMM(gnd.GetPosition().y)
        if abs(gy - pcbnew.ToMM(rail.GetPosition().y)) < 0.5:
            continue            # the loop runs along y: the GND pad must sit above or below the rail pad
        far = (pcbnew.ToMM(bb.GetBottom()) + 0.3 if gy < pcbnew.ToMM(rail.GetPosition().y)
               else pcbnew.ToMM(bb.GetTop()) - 0.3)
        others = _foreign(s.board, side, "GND", {(fp.GetReference(), "1")})
        for sgn in (1, -1):
            x = (pcbnew.ToMM(bb.GetRight()) if sgn > 0 else pcbnew.ToMM(bb.GetLeft())) + sgn * (0.1 + 0.125)
            pts = [(gx, gy), (x, gy), (x, far), (x + sgn * 0.5, far), (x + sgn * 0.5, gy), (x, gy)]
            segs = [("GND", "track", (a, b), 0.125) for a, b in zip(pts, pts[1:])]
            if all(_dist(sg, it) >= SAB_DRC_KEEP_MM for sg in segs for it in others):
                kipcb.add_track(s.board, side, 0.25, "GND", pts)
                s.sab_ref = fp.GetReference()
                return
    raise SystemExit("drc sabotage: no 100 nF with a clear loop beside its rail pad")


def _sab_drc_missing(s):
    s.drc_broken = True


def _sab_drc_cut(s):
    s.drc_cut = True


def _longest(board, net):
    t = max((t for t in board.GetTracks() if t.Type() == pcbnew.PCB_TRACE_T and t.GetNetname() == net),
            key=lambda t: (t.GetLength(), -t.GetStart().x, -t.GetStart().y))
    return t, (_mm(t.GetStart().x), _mm(t.GetStart().y)), (_mm(t.GetEnd().x), _mm(t.GetEnd().y))


def _beside(s, net, along, offsets):
    """A `net` track laid beside `along`'s longest track, on its layer, at
    the first of `offsets` (mm, signed) where it keeps SAB_DRC_KEEP_MM from
    all copper of other nets on the layer. A track laid onto foreign copper
    takes that net on save (2026-10-01: after U_REG's copper area moved the
    routing, LED0 at +3 mm beside OUT_L landed on a MOD4_A track and was
    saved as MOD4_A, so audio stayed green)."""
    from gen import kipcb
    t, (x1, y1), (x2, y2) = _longest(s.board, along)
    layer = t.GetLayerName()
    others = _foreign(s.board, layer, net, set())
    L = math.hypot(x2 - x1, y2 - y1)
    for off in offsets:
        nx, ny = -(y2 - y1) / L * off, (x2 - x1) / L * off
        pts = [(x1 + nx, y1 + ny), (x2 + nx, y2 + ny)]
        seg = (net, "track", tuple(pts), 0.125)
        if all(_dist(seg, it) >= SAB_DRC_KEEP_MM for it in others):
            kipcb.add_track(s.board, layer, 0.25, net, pts)
            s.beside = (net, along, off)
            return
    raise SystemExit("sabotage: no clear place for %s beside %s at %s mm" % (net, along, offsets))


def _sab_audio(s):
    """An LED0 track laid 2-5 mm beside OUT_L's longest track, on its layer
    (the first clear offset, `_beside`)."""
    _beside(s, "LED0", "OUT_L", (3.0, -3.0, 4.0, -4.0, 2.0, -2.0, 5.0, -5.0))


def _sab_audio_missing(s):
    """Every SR_LATCH pad, track and via moved to a renamed net: an
    aggressor the check names is no longer on the board (spec §5.4: a
    renamed net cannot empty the rule)."""
    from gen import kipcb
    net = kipcb._net(s.board, "SR_LATCH_RENAMED")
    for t in s.board.GetTracks():
        if t.GetNetname() == "SR_LATCH":
            t.SetNet(net)
    for f in s.board.GetFootprints():
        for p in f.Pads():
            if p.GetNetname() == "SR_LATCH":
                p.SetNet(net)


def _sab_audio_zone(s):
    """A short SR_CLK track just inside an exemption zone, about 1 mm from
    victim copper outside it (spec §5.4, across a zone edge). Candidates
    start from the victim track pieces outside every zone whose midpoints
    lie closest to a zone; the stub (0.4 mm, across the line to the zone)
    sits 1.2-1.8 mm inside on that line or beside it, at the first place
    whose copper keeps 0.2 mm from all other copper on the layer (a stub
    touching other copper takes that net on save). Every check before the
    fix dropped zone copper, so this stub was invisible to it."""
    from gen import kipcb
    rects = zones(s.board) + [z[4] for z in jack_zones(s.board)]
    vic = {ln: [] for ln in LAYERS}
    cands = []
    for v in VICTIMS:
        for ln, items in copper(s.board, [v], rects).items():
            vic[ln] += items
            for _n, kind, g, _h in items:
                if kind != "track":
                    continue
                mx, my = (g[0][0] + g[1][0]) / 2.0, (g[0][1] + g[1][1]) / 2.0
                for l, t, r, b in rects:
                    qx, qy = min(max(mx, l), r), min(max(my, t), b)
                    d = math.hypot(qx - mx, qy - my)
                    if d > 1e-3:
                        cands.append((round(d, 6), ln, v, mx, my, qx, qy))
    others = {ln: [] for ln in LAYERS}
    for t in s.board.GetTracks():
        a, b = (_mm(t.GetStart().x), _mm(t.GetStart().y)), (_mm(t.GetEnd().x), _mm(t.GetEnd().y))
        if t.Type() == pcbnew.PCB_VIA_T:
            for ln in LAYERS:
                others[ln].append(("", "via", (a, a), _mm(t.GetWidth(pcbnew.F_Cu)) / 2.0))
        elif t.GetLayerName() in LAYERS:
            others[t.GetLayerName()].append(("", "track", (a, b), _mm(t.GetWidth()) / 2.0))
    for f in s.board.GetFootprints():
        for p in f.Pads():
            for ln, lid in LAYERS.items():
                if p.IsOnLayer(lid):
                    others[ln].append(("", "pad", _pad_box(p), 0.0))
    grid = {}
    for ln, items in others.items():
        for it in items:
            _n, k, g, _h = it
            xs = (g[0], g[2]) if k == "pad" else (g[0][0], g[1][0])
            ys = (g[1], g[3]) if k == "pad" else (g[0][1], g[1][1])
            for gx in range(int(min(xs) // 1.0) - 1, int(max(xs) // 1.0) + 2):
                for gy in range(int(min(ys) // 1.0) - 1, int(max(ys) // 1.0) + 2):
                    grid.setdefault((ln, gx, gy), []).append(it)

    def crowded(ln, stub):
        cx, cy = stub[2][0]
        near = grid.get((ln, int(cx // 1.0), int(cy // 1.0)), [])
        return any(_dist(stub, it) < 0.2 for it in near)
    for d, ln, _v, mx, my, qx, qy in sorted(cands)[:400]:
        nx, ny = (qx - mx) / d, (qy - my) / d
        for depth in (1.4, 1.2, 1.6, 1.8):
            for off in (0.0, 0.8, -0.8, 1.6, -1.6):
                cx, cy = qx + nx * depth - ny * off, qy + ny * depth + nx * off
                if not _inside(cx, cy, rects):
                    continue
                stub = ("SR_CLK", "track", ((cx - ny * 0.2, cy + nx * 0.2), (cx + ny * 0.2, cy - nx * 0.2)), 0.125)
                if crowded(ln, stub):
                    continue
                gap = min_distance([stub], vic[ln], 3.0)[0]
                if gap < 2.0:
                    kipcb.add_track(s.board, ln, 0.25, "SR_CLK", list(stub[2]))
                    s.audio_zone = (ln, round(cx, 3), round(cy, 3), round(gap, 3))
                    return
    raise SystemExit("audio_zone sabotage: no clear place inside a zone near victim copper")


def _sab_lr(s):
    """An OUT_R track laid 1-1.5 mm beside OUT_L's longest track, on its
    layer (the first clear offset, `_beside`)."""
    _beside(s, "OUT_R", "OUT_L", (1.0, -1.0, 1.5, -1.5))


def _sab_lr_missing(s):
    s.lr_missing = True


def _sab_sense(s):
    """A 100 mm detour added to SENSE_2 at its first pad: out 50 mm and back
    on B.Cu, so its copper is far above 1.3 x its MST."""
    from gen import kipcb
    pad = [p for f in s.board.GetFootprints() for p in f.Pads() if p.GetNetname() == "SENSE_2"][0]
    x, y = _mm(pad.GetPosition().x), _mm(pad.GetPosition().y)
    kipcb.add_track(s.board, "B.Cu", 0.25, "SENSE_2", [(x, y), (x + 50.0, y), (x, y)])


def _sab_sense_missing(s):
    s.sense_missing = True


def _sab_pot_keepout(s):
    """An F.Cu track of the first pot's wiper net laid across its body box."""
    from gen import kipcb
    r = sorted(x for x in s.front if x.startswith("RV"))[0]
    fp = s.board.FindFootprintByReference(r)
    l, t, rr, b = PL.body_box(fp)
    net = [p.GetNetname() for p in fp.Pads() if str(p.GetNumber()) == "2"][0]
    kipcb.add_track(s.board, "F.Cu", 0.25, net, [(l + 0.5, (t + b) / 2.0), (rr - 0.5, (t + b) / 2.0)])


def _sab_pot_keepout_missing(s):
    s.pot_missing = True


MOAT_MM = (8.0, 0.6)     # the planes sabotage's square moat: outer side, width


def _sab_planes(s):
    """A free island on In1.Cu GND (addendum A.2): a square moat cut out of
    the GND fill where no GND pad, track or via comes within 1 mm of the
    square and the square's rim lies in the fill. The fill inside the moat
    touches no GND pad. The fill is edited, not refilled: the zone's island
    removal (mode 0) would drop an island no GND item holds."""
    lid = pcbnew.In1_Cu
    z = [z for z in s.board.Zones()
         if not z.GetIsRuleArea() and z.GetNetname() == "GND" and z.IsOnLayer(lid)][0]
    fill = pcbnew.SHAPE_POLY_SET(z.GetFilledPolysList(lid))
    gnd = [_pad_box(p) for f in s.board.GetFootprints() for p in f.Pads() if p.GetNetname() == "GND"]
    gnd += [PL.box(t.GetBoundingBox()) for t in s.board.GetTracks() if t.GetNetname() == "GND"]
    side, width = MOAT_MM
    h = side / 2.0

    def pt(x, y):
        return pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))

    def square(cx, cy, hh):
        out = pcbnew.SHAPE_POLY_SET()
        out.NewOutline()
        for x, y in ((cx - hh, cy - hh), (cx + hh, cy - hh), (cx + hh, cy + hh), (cx - hh, cy + hh)):
            out.Append(pt(x, y))
        return out
    for cy in range(20, 106, 2):
        for cx in range(20, 290, 2):
            sq = (cx - h, cy - h, cx + h, cy + h)
            if any(PL.overlaps(PL.grow(sq, 1.0), g) for g in gnd):
                continue
            rim = [(cx - h + side * i / 8.0, cy + dy) for i in range(9) for dy in (-h, h)] + \
                  [(cx + dx, cy - h + side * i / 8.0) for i in range(9) for dx in (-h, h)]
            if not all(fill.Contains(pt(x, y)) for x, y in rim):
                continue
            ring = square(cx, cy, h)
            ring.BooleanSubtract(square(cx, cy, h - width))
            fill.BooleanSubtract(ring)
            fill.Fracture()
            z.SetFilledPolysList(lid, fill)
            s.moat = (cx, cy)
            return
    raise SystemExit("planes sabotage: no place for the moat")


def _sab_planes_missing(s):
    s.planes_missing = True


def _sab_planes_stitch(s):
    """Plane-net vias removed, lowest x first, until there are fewer than
    SMD plane-net pads: on the 2026-10-01 board one via, U_MUX1.8's GND
    stitch at (38.91, 42.51). Every pad stays connected to its plane through
    other copper (the Task 7 review probed it; routed stays green), so only
    the planes step's count gate fires."""
    v = sorted((t for t in s.board.GetTracks()
                if t.Type() == pcbnew.PCB_VIA_T and t.GetNetname() in PLANE_NETS),
               key=lambda t: (t.GetPosition().x, t.GetPosition().y))
    smd = sum(1 for f in s.board.GetFootprints() for p in f.Pads()
              if p.GetNetname() in PLANE_NETS and p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD)
    for t in v[: max(0, len(v) - smd) + 1]:
        s.board.Delete(t)


def _sab_reg_copper(s):
    """Every 3V3D B.Cu fill cut down to U_REG's tab box grown by REG_SAB_MM:
    still on the tab, far below REG_MIN_MM2. The fill is edited, not
    refilled (as the planes sabotage)."""
    lid = pcbnew.B_Cu
    tab = _reg_tab(s.board, REG_REF)
    zs = [z for z in s.board.Zones() if not z.GetIsRuleArea() and z.GetNetname() == REG_NET and z.IsOnLayer(lid)]
    if tab is None or not zs:
        raise SystemExit("reg_copper sabotage: no %s tab or no %s zone on B.Cu" % (REG_REF, REG_NET))
    l, t, r, b = PL.grow(_pad_box(tab), REG_SAB_MM)
    box = pcbnew.SHAPE_POLY_SET()
    box.NewOutline()
    for x, y in ((l, t), (r, t), (r, b), (l, b)):
        box.Append(pcbnew.FromMM(x), pcbnew.FromMM(y))
    for z in zs:
        fill = pcbnew.SHAPE_POLY_SET(z.GetFilledPolysList(lid))
        fill.BooleanIntersection(box)
        fill.Fracture()
        z.SetFilledPolysList(lid, fill)


def _sab_reg_copper_missing(s):
    s.reg_missing = True


def _sab_rules_file(s):
    """rules_file reads a project file whose clearance says 0.15, handed in
    as bytes (no temporary file is left behind)."""
    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, "kicad", "reva.kicad_pro"), encoding="utf-8") as fh:
        body = json.load(fh)
    body["board"]["design_settings"]["rules"]["min_clearance"] = 0.15
    s.rules_pro_bytes = json.dumps(body).encode("utf-8")


def _sab_rules_file_missing(s):
    s.rules_missing = True


def _sab_paths(s):
    """R1's and R2's schematic links swapped: parity cannot see it (it matches
    by reference, spec §3), only the netlist comparison can."""
    a, b = s.board.FindFootprintByReference("R1"), s.board.FindFootprintByReference("R2")
    pa, pb = a.GetPath(), b.GetPath()
    a.SetPath(pb)
    b.SetPath(pa)


def _sab_paths_missing(s):
    s.paths_missing = True


def _sab_parity(s):
    """C2's LCSC field changed: one footprint_symbol_field_mismatch."""
    s.board.FindFootprintByReference("C2").SetField("LCSC", "C1")


def _sab_parity_missing(s):
    """The schematic left out of the parity copy: the positive control finds nothing."""
    s.parity_missing = True


def _sab_silk_height(s):
    """R1's reference at 0.8 mm."""
    s.board.FindFootprintByReference("R1").Reference().SetTextSize(
        pcbnew.VECTOR2I(pcbnew.FromMM(0.8), pcbnew.FromMM(0.8)))


def _sab_silk_height_stroke(s):
    """R1's reference at a 0.10 mm stroke, its height unchanged."""
    s.board.FindFootprintByReference("R1").Reference().SetTextThickness(pcbnew.FromMM(0.10))


def _sab_silk_height_missing(s):
    s.silk_height_missing = True


def _sab_silk_front(s):
    s.board.FindFootprintByReference("RV1").Reference().SetVisible(True)


def _sab_silk_front_missing(s):
    s.silk_front_missing = True


def _sab_silk_clear(s):
    """R1's reference moved onto its own pad 1 and shown."""
    fp = s.board.FindFootprintByReference("R1")
    pad = [p for p in fp.Pads() if str(p.GetNumber()) == "1"][0]
    fp.Reference().SetVisible(True)
    fp.Reference().SetPosition(pad.GetPosition())


def _sab_silk_clear_missing(s):
    """The DRC report's silk blocks are not read (every silk class dropped
    from the text silk_clear parses); the board's texts stay as they are."""
    s.silk_clear_missing = True


def _sab_no_room(s):
    """R1's reference hidden although it had room: a NEW hidden reference."""
    s.board.FindFootprintByReference("R1").Reference().SetVisible(False)


def _sab_no_room_stale(s):
    """R2 listed although its reference is shown: a stale entry."""
    s.extra_no_room = ("R2",)


def _sab_no_room_missing(s):
    s.no_room_missing = True


SABOTAGES ={"routed": _sab_routed, "routed_missing": _sab_routed_missing,
             "routed_song": _sab_routed_song,
             "drc": _sab_drc, "drc_missing": _sab_drc_missing, "drc_cut": _sab_drc_cut,
             "rules_file": _sab_rules_file, "rules_file_missing": _sab_rules_file_missing,
             "audio": _sab_audio, "audio_missing": _sab_audio_missing, "audio_zone": _sab_audio_zone,
             "lr": _sab_lr, "lr_missing": _sab_lr_missing,
             "sense": _sab_sense, "sense_missing": _sab_sense_missing,
             "pot_keepout": _sab_pot_keepout, "pot_keepout_missing": _sab_pot_keepout_missing,
             "planes": _sab_planes, "planes_missing": _sab_planes_missing,
             "planes_stitch": _sab_planes_stitch,
             "reg_copper": _sab_reg_copper, "reg_copper_missing": _sab_reg_copper_missing,
             "paths": _sab_paths, "paths_missing": _sab_paths_missing,
             "parity": _sab_parity, "parity_missing": _sab_parity_missing,
             "silk_height": _sab_silk_height, "silk_height_missing": _sab_silk_height_missing,
             "silk_height_stroke": _sab_silk_height_stroke,
             "silk_front": _sab_silk_front, "silk_front_missing": _sab_silk_front_missing,
             "silk_clear": _sab_silk_clear, "silk_clear_missing": _sab_silk_clear_missing,
             "no_room": _sab_no_room, "no_room_stale": _sab_no_room_stale,
             "no_room_missing": _sab_no_room_missing}
TURNS_RED = {"routed": "routed", "routed_missing": "routed", "routed_song": "routed",
             "drc": "drc", "drc_missing": "drc", "drc_cut": "drc",
             "rules_file": "rules_file", "rules_file_missing": "rules_file",
             "audio": "audio", "audio_missing": "audio", "audio_zone": "audio",
             "lr": "lr", "lr_missing": "lr",
             "sense": "sense", "sense_missing": "sense",
             "pot_keepout": "pot_keepout", "pot_keepout_missing": "pot_keepout",
             "planes": "planes", "planes_missing": "planes", "planes_stitch": "planes",
             "reg_copper": "reg_copper", "reg_copper_missing": "reg_copper",
             "paths": "paths", "paths_missing": "paths",
             "parity": "parity", "parity_missing": "parity",
             "silk_height": "silk_height", "silk_height_missing": "silk_height",
             "silk_height_stroke": "silk_height",
             "silk_front": "silk_front", "silk_front_missing": "silk_front",
             "silk_clear": "silk_clear", "silk_clear_missing": "silk_clear",
             "no_room": "no_room", "no_room_stale": "no_room", "no_room_missing": "no_room"}
WHY = {"routed": "unrouted on SENSE_2", "routed_missing": "incomplete",
       "routed_song": "SONG_A: 2 blocks under one key",
       "drc": "clearance {ref}: kicad-cli", "drc_missing": "wrote no report",
       "drc_cut": "was not read",
       "rules_file": "min_clearance is 0.15", "rules_file_missing": "carries no rules",
       "audio": "audio clearance below 10.0 mm",
       "audio_missing": "audio measured nothing: victims or aggressors absent from the board",
       "audio_zone": "audio clearance across a zone edge below 3.0 mm",
       "lr": "L/R spacing below 2.0 mm", "lr_missing": "L/R measured nothing: no L/R pair examined",
       "sense": "SENSE length over 1.3 x MST", "sense_missing": "SENSE measured nothing: no SENSE net examined",
       "pot_keepout": "F.Cu copper under a pot body", "pot_keepout_missing": "no pot examined",
       "planes": "free island", "planes_missing": "no plane examined",
       "planes_stitch": "stitching:",
       "reg_copper": "U_REG copper below 200.0 mm2", "reg_copper_missing": REG_EMPTY,
       "paths": "KiCad's netlist says", "paths_missing": PATHS_EMPTY,
       "parity": "parity item footprint_symbol_field_mismatch on C2",
       "parity_missing": PARITY_EMPTY,
       "silk_height": "silk text below 1.0 mm: R1", "silk_height_missing": SILK_EMPTY,
       "silk_height_stroke": "silk stroke below 0.15 mm: R1",
       "silk_front": "visible front reference: RV1", "silk_front_missing": FRONT_EMPTY,
       "silk_clear": "silk_over_copper R1", "silk_clear_missing": CLEAR_EMPTY,
       "no_room": "hidden back reference not in NO_ROOM",
       "no_room_stale": "listed as known but no longer fails", "no_room_missing": ROOM_EMPTY}


def why(s, name):
    """WHY[name] for the sabotaged state `s`: `{ref}` is the part the drc
    sabotage chose (`s.sab_ref`). A sabotage that never set it leaves `{ref}`
    in the phrase, which no report line carries, so the guard goes red."""
    return WHY[name].replace("{ref}", getattr(s, "sab_ref", "{ref}"))


def sabotage(s, name):
    if name not in SABOTAGES:
        raise SystemExit("unknown sabotage %r; known: %s" % (name, ", ".join(sorted(SABOTAGES))))
    if not s.known:
        s.known = {k: set(v) for k, v in KNOWN_PANEL.items()}
    SABOTAGES[name](s)
    print("SABOTAGED: %s" % name)
