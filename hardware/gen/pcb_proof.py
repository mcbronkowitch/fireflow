#!/usr/bin/env python3
"""Proof steps shared by the generated boards (coupon, Rev A routing spike).

Runs under KiCad's Python only (it imports pcbnew). Extracted from the
coupon's build_pcb.py and check_layout.py, which import it from here; the
docstrings there keep the probe history of each choice (why kicad-cli's
report is parsed instead of pcbnew's connectivity API, why a stale report
is deleted first, why GetUnconnectedCount(True)).
"""
import math
import os
import re
import subprocess

import pcbnew

from gen import ksexp

_CLASS_RE = re.compile(r"^\[([a-z0-9_]+)\]", re.M)
_PAD_RE = re.compile(r"(?:PTH pad|Pad) (\S+) \[([^\]]+)\] of (\S+)")


def _mm(vec):
    return pcbnew.ToMM(vec.x), pcbnew.ToMM(vec.y)


def count_drc_violations(rpt_path):
    """Parse a `kicad-cli pcb drc` report into {violation class: count}."""
    txt = open(rpt_path, encoding="utf-8", errors="replace").read()
    counts = {}
    for kind in _CLASS_RE.findall(txt):
        counts[kind] = counts.get(kind, 0) + 1
    return counts


def drc(pcb_path, rpt_path):
    """kicad-cli DRC at error and warning severity into `rpt_path`.

    The report is deleted first and its absence afterwards is the failure:
    a stale report from an earlier run must never be read as this run's
    verdict. The return code cannot stand in for it, since
    --exit-code-violations makes a nonzero rc the normal outcome.
    """
    if os.path.exists(rpt_path):
        os.remove(rpt_path)
    r = subprocess.run([ksexp.KICAD_CLI, "pcb", "drc", "--exit-code-violations",
                        "--severity-error", "--severity-warning",
                        "-o", rpt_path, pcb_path],
                       capture_output=True, text=True)
    if not os.path.exists(rpt_path):
        raise RuntimeError("kicad-cli pcb drc wrote no report (rc=%d)\n%s"
                           % (r.returncode, (r.stdout + r.stderr).strip()))
    return count_drc_violations(rpt_path)


def unconnected_by_net(rpt_path):
    """{net: {"REF.PAD", ...}} for every pad named in an [unconnected_items]
    block. The header line names neither net nor pad; the two pads are on
    the lines under it."""
    txt = open(rpt_path, encoding="utf-8", errors="replace").read()
    by_net = {}
    for block in re.split(r"(?=^\[)", txt, flags=re.M):
        if not block.startswith("[unconnected_items]"):
            continue
        for padnum, net, ref in _PAD_RE.findall(block):
            by_net.setdefault(net, set()).add("%s.%s" % (ref, padnum))
    return by_net


def live_unconnected(board):
    """Unconnected pairs on the live board; agrees with kicad-cli's
    [unconnected_items] count (probed on the coupon, 10.0.5)."""
    board.BuildConnectivity()
    return board.GetConnectivity().GetUnconnectedCount(True)


def render(pcb_path, png_path, side):
    """`kicad-cli pcb render` of one side ("top" or "bottom")."""
    r = subprocess.run([ksexp.KICAD_CLI, "pcb", "render", "--side", side,
                        "-o", png_path, pcb_path], capture_output=True, text=True)
    return r.returncode, (r.stdout + r.stderr).strip()


def seg_point_dist(p, a, b):
    ax, ay = a
    bx, by = b
    px, py = p
    dx, dy = bx - ax, by - ay
    if dx == 0 and dy == 0:
        return math.hypot(px - ax, py - ay)
    t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)
    t = max(0.0, min(1.0, t))
    cx, cy = ax + t * dx, ay + t * dy
    return math.hypot(px - cx, py - cy)


def seg_seg_dist(s1, s2):
    """Closest approach between two segments. The min over the four
    endpoint-to-opposite-segment distances is not exact for a crossing pair;
    different-net copper never crosses on a DRC-clean board, so it is exact
    for everything a proof asks."""
    a, b = s1
    c, d = s2
    return min(seg_point_dist(a, c, d), seg_point_dist(b, c, d),
               seg_point_dist(c, a, b), seg_point_dist(d, a, b))


def track_segments(board, net_pred):
    """[(net, layer_name, (x1, y1), (x2, y2))] for every PCB_TRACE_T whose
    net passes `net_pred`. Vias and arcs are excluded."""
    out = []
    for t in board.GetTracks():
        if t.Type() != pcbnew.PCB_TRACE_T:
            continue
        net = t.GetNetname()
        if not net_pred(net):
            continue
        out.append((net, t.GetLayerName(), _mm(t.GetStart()), _mm(t.GetEnd())))
    return out
