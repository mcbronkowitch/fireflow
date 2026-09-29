#!/usr/bin/env python3
"""The strip board -> Specctra DSN -> Freerouting -> SES -> the strip board
(P4a spec §3.3).

Freerouting and its Java runtime live outside the repository (Task 4):
%LOCALAPPDATA%/fireflow-tools/, or wherever FIREFLOW_JAVA and
FIREFLOW_FREEROUTING point. A missing one stops the run with its name.
Freerouting always runs with -da: its anonymous analytics stay off. The
proof that they were off is the DEBUG line "Analytics are disabled", which
Freerouting writes only to its own log file (never to the console, Task 4);
a run whose log section lacks it stops here.
"""
import math
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import pcbnew          # noqa: E402
import stripe as ST    # noqa: E402
from gen import kipcb  # noqa: E402

TOOLS = os.path.join(os.environ.get("LOCALAPPDATA", ""), "fireflow-tools")
JAVA = os.environ.get("FIREFLOW_JAVA", os.path.join(TOOLS, "jre", "bin", "java.exe"))
JAR = os.environ.get("FIREFLOW_FREEROUTING", os.path.join(TOOLS, "freerouting.jar"))
FR_LOG = os.path.join(os.environ.get("LOCALAPPDATA", ""), "freerouting", "logs",
                      "freerouting.log")
# Verified in Task 4; change only with a probe. No -l (it moves the log file
# into <cwd>\en\), no -inc (accepted, no effect headless).
FR_ARGS = ["--gui.enabled=false", "-da", "-mp", "20", "-mt", "1"]
# Task 4: locked tracks come back from the import unchanged and still locked;
# the import deletes unlocked tracks, so locking before the export (locked.py)
# is what keeps them.
RELOCK_AFTER_IMPORT = False
TIMEOUT_S = 3600
BANNER = "INFO   Freerouting v"
ANALYTICS = "Analytics are disabled"
# Lines of one run's log section worth echoing: the command line, the
# analytics proof, the stage summaries and the pass/stop lines.
ECHO = ("Command line arguments", ANALYTICS, "stage started", "stage completed",
         "pass #", "Stopping", "could not be routed", "unrouted connection", "  -  ")
FIX_WIRE = re.compile(r"\(wire \(path (\S+) (\S+)\s+(-?[\d.]+) (-?[\d.]+)\s+(-?[\d.]+) (-?[\d.]+)\)"
                      r"\(net (\S+?)\)\(type fix\)\)")
SNAP_UM = 1.0


def _num(v):
    return ("%.3f" % v).rstrip("0").rstrip(".")


def fix_locked_wires(txt, board):
    """DSN handling of the locked nets' fixed wires, for two faults found on
    Task 7's first strip run:

    1. The exported fixed wires do not end on the pad centres. Every number
       in the SENSE_1 wire lines has at most six significant digits
       (274768, -42412.5, -84550.7), while place lines carry more. That the
       format is six significant digits is inferred, not read in KiCad's
       source. The three COM-pad ends were off by 0.5, 0.286 and 0.143/0.014
       um. Freerouting wrote stubs of 0.5 and 0.3 um from the pad centre to
       the wire end for the first two (none for the third, unexplained), and
       they came back as unlocked tracks on a locked net. Endpoints within
       SNAP_UM of a same-net pad centre are moved onto it.
    2. Freerouting reported SENSE_1's U_MUX4-3 -> U_MUX5-3 unrouted. The
       U_MUX5 branch ends on the interior of the trunk wire, a T-junction.
       Such a wire is split at that point, so the junction is a shared
       vertex.

    Both came in together (run 2), so the log does not show which of the two
    cleared SENSE_1. The copper is unchanged: the SES carries no fixed wire,
    and the locked tracks on the board are never touched. Returns (text,
    snapped, split)."""
    pads = {}
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() in ST.LOCKED_NETS:
                pads.setdefault(pad.GetNetname(), []).append(
                    (pcbnew.ToMM(pad.GetPosition().x) * 1000.0,
                     -pcbnew.ToMM(pad.GetPosition().y) * 1000.0))
    wires = []
    for m in FIX_WIRE.finditer(txt):
        layer, width, x1, y1, x2, y2, net = m.groups()
        wires.append([m, layer, width, net.strip('"'),
                      [(float(x1), float(y1)), (float(x2), float(y2))]])
    snapped = 0
    for w in wires:
        for i, (x, y) in enumerate(w[4]):
            for px, py in pads.get(w[3], ()):
                if 0 < math.hypot(px - x, py - y) <= SNAP_UM:
                    w[4][i] = (px, py)
                    snapped += 1
                    break
    split = 0
    out, last = [], 0
    for w in wires:
        (ax, ay), (bx, by) = w[4]
        seg = math.hypot(bx - ax, by - ay)
        cuts = []
        for o in wires:
            if o is w or o[3] != w[3] or o[1] != w[1]:
                continue
            for (x, y) in o[4]:
                t = ((x - ax) * (bx - ax) + (y - ay) * (by - ay)) / (seg * seg) if seg else 0
                d = abs((bx - ax) * (ay - y) - (ax - x) * (by - ay)) / seg if seg else 1e9
                if 0 < t < 1 and d <= SNAP_UM and (x, y) not in [p for _t, p in cuts]:
                    cuts.append((t, (x, y)))
        pts = [w[4][0]] + [p for _t, p in sorted(cuts)] + [w[4][1]]
        split += len(cuts)
        m = w[0]
        out.append(txt[last:m.start()])
        out.append("\n    ".join(
            "(wire (path %s %s  %s  %s)(net %s)(type fix))" % (
                w[1], w[2], _num(p[0]) + " " + _num(p[1]), _num(q[0]) + " " + _num(q[1]),
                m.group(7))
            for p, q in zip(pts, pts[1:])))
        last = m.end()
    out.append(txt[last:])
    return "".join(out), snapped, split


def _log_size():
    return os.path.getsize(FR_LOG) if os.path.exists(FR_LOG) else 0


def _section(start):
    """This run's part of Freerouting's shared log: from the last startup
    banner after byte `start` to the end of the file."""
    if not os.path.exists(FR_LOG):
        return ""
    with open(FR_LOG, "rb") as fh:
        fh.seek(start if start <= _log_size() else 0)
        text = fh.read().decode("utf-8", errors="replace")
    i = text.rfind(BANNER)
    return text[text.rfind("\n", 0, i) + 1:] if i >= 0 else ""


def route(s, prefix):
    for path, var in ((JAVA, "FIREFLOW_JAVA"), (JAR, "FIREFLOW_FREEROUTING")):
        if not os.path.exists(path):
            raise SystemExit("freerouting: %s not found (set %s)" % (path, var))
    dsn, ses, log = prefix + ".dsn", prefix + ".ses", prefix + "-freerouting.log"
    for p in (dsn, ses):
        if os.path.exists(p):
            os.remove(p)
    if not pcbnew.ExportSpecctraDSN(s.board, dsn):
        raise SystemExit("freerouting: DSN export failed")
    with open(dsn, encoding="utf-8") as fh:
        txt, snapped, split = fix_locked_wires(fh.read(), s.board)
    with open(dsn, "w", encoding="utf-8") as fh:
        fh.write(txt)
    print("   DSN: %d fixed-wire ends snapped onto pad centres, %d fixed wires split "
          "at a junction" % (snapped, split))
    args = list(FR_ARGS)
    if s.layers == 4:
        args += ["-inc", "Supply"]     # GND and SM_3V3 are planes on 4 layers
    cmd = [JAVA, "-jar", JAR, "-de", dsn, "-do", ses] + args
    start = _log_size()
    t0 = time.time()
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=TIMEOUT_S)
    seconds = round(time.time() - t0, 1)
    section = _section(start)
    with open(log, "w", encoding="utf-8") as fh:
        fh.write("RUN %s\n\n== console ==\n%s%s\n== %s section ==\n%s" % (
            " ".join(os.path.basename(c) if c in (JAVA, JAR) else c for c in cmd),
            r.stdout, r.stderr, FR_LOG, section))
    lines = section.splitlines()
    analytics = [ln for ln in lines if ANALYTICS in ln]
    for ln in lines:
        if any(k in ln for k in ECHO):
            print("   | " + ln[:220])
    if not analytics:
        raise SystemExit("freerouting: BLOCKED -- no '%s' line in this run's log "
                         "section (%s), see %s" % (ANALYTICS, FR_LOG, log))
    if not os.path.exists(ses):
        raise SystemExit("freerouting: no SES written (rc=%d), see %s" % (r.returncode, log))
    if not pcbnew.ImportSpecctraSES(s.board, ses):
        raise SystemExit("freerouting: SES import failed")
    if RELOCK_AFTER_IMPORT:
        kipcb.lock_tracks(s.board, set(ST.LOCKED_NETS))
    vias, length = 0, 0.0
    for t in s.board.GetTracks():
        if t.GetNetname() in ST.LOCKED_NETS:
            continue
        if t.Type() == pcbnew.PCB_VIA_T:
            vias += 1
        elif t.Type() == pcbnew.PCB_TRACE_T:
            length += math.hypot(pcbnew.ToMM(t.GetEnd().x - t.GetStart().x),
                                 pcbnew.ToMM(t.GetEnd().y - t.GetStart().y))
    return {"seconds": seconds, "rc": r.returncode, "vias": vias,
            "length_mm": round(length, 1), "log": os.path.relpath(log),
            "analytics": analytics[0].strip()}
