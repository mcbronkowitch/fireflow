#!/usr/bin/env python3
"""The check machinery place_check.py (P4-1) and route_check.py (P4-2) share:
the kicad-cli DRC report parser, the known-list judge, the step runner, the
key-name rule for KNOWN_PANEL-style keys, and the minimum spanning tree. No
pcbnew here: this module is testable under plain Python (test_check_kit.py)."""
import math
import re


def judge(known, found):
    """known: set of keys; found: {key: message}. Returns (ok, details, n_known)."""
    details = []
    for k in sorted(found):
        tag = "known, waits for the panel pass" if k in known else "NEW"
        details.append("%s: %s [%s]" % (k, found[k], tag))
    stale = sorted(known - set(found))
    details += ["%s: listed as known but no longer fails -- remove it from KNOWN_PANEL" % k
                for k in stale]
    unknown = [k for k in found if k not in known]
    return not unknown and not stale, details, len(set(found) & known)


DETAIL_CAP = 40


def run_steps(steps, s, pcb_path, prefix):
    green = True
    for i, (name, fn) in enumerate(steps, 1):
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


def mst(pts):
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


_FOUND_RE = {"violations": re.compile(r"^\*\* Found (\d+) DRC violations \*\*", re.M),
             "unconnected": re.compile(r"^\*\* Found (\d+) unconnected pads \*\*", re.M),
             "footprint_errors": re.compile(r"^\*\* Found (\d+) Footprint errors \*\*", re.M)}


def report_summary(txt):
    """The report's own counts (probed on P4-1's report, 10.0.5) and whether
    it ran to its end. A reader that loses blocks is caught by comparing
    these counts with the blocks it parsed."""
    out = {}
    for k, rx in _FOUND_RE.items():
        m = rx.search(txt)
        out[k] = int(m.group(1)) if m else None
    out["complete"] = "** End of Report **" in txt and None not in out.values()
    return out


def key_names(key, class_words):
    """The part names a KNOWN_PANEL key stands for: when the first token is a
    known check class word it is dropped; the rest splits on "/". A bare key
    (the "edge" check's) is names only, so a stray space leaves the whole
    string as one name, which no part has."""
    head, _sp, tail = key.partition(" ")
    rest = tail if head in class_words else key
    return [n for n in rest.split("/") if n]


def key_allowed(key, allowed, pairs, class_words):
    names = set(key_names(key, class_words))
    if not names or not names <= allowed:
        return False
    return all(names <= pair for pair in pairs if names & pair)
