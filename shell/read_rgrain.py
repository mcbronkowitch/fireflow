"""Reads a capture of the reverb grain layer (SHELL_RGRAIN=1 or 2; spec
docs/superpowers/specs/2026-10-31-sd-use-cases-tests-design.md section 5, B1
and B2). Record with tools/remote_run.py (or read_play.py), then:

    python read_rgrain.py capture.txt [--slope CYCLES]

The board prints, per 500 ms report pass, `SHELL_REMOTE run id= ... counting=
void= tripped=`, SHELL_PLAY_LAW, `SHELL_PLAY_WIN win= ... n= sum=` (the
window's worst block, its block count and cycle sum) and, right after it,
`SHELL_RGRAIN mode= n= cap= granted_sum= blocks= swing= rms_x1e6=` for the
same window. A frame is a SHELL_RGRAIN line with the latest run and WIN lines
of its pass; it counts when the run line says counting=1 void=0 and the WIN
line has n > 0 (n=0 is the board's torn-read give-up). Frames are grouped by
workload, the run line's id.

B1 (mode=1, the fixed-N sweep): per workload and n, the mean of the frames'
per-window means (sum / n); the slope per grain is a least-squares fit of
those means against n with one intercept per workload (each workload's own
base, one common slope). Frames whose window the overrun guard touched
(tripped=1) are left out: their means hold silenced blocks. The halves: per
workload and n, the first half of its frames in time order against the rest;
the board interleaves N per round (0-8-2-4, 4-2-8-0, ...), so each half holds
half of the rounds of every n. Pass: both halves fit, and their slopes are
within 10 % of each other (of their mean). Per workload the room in grains,
floor((945 000 - x) / slope), at x = the N=0 mean, the p99 of the N=0 frames'
worst blocks (nearest rank) and their worst block: 945 000 is the click line
(spec section 2).

B2 (mode=2, the load-dependent cap; the cap alternates off and on per 60
frames): per workload and cap, the frames, the worst block (win), the mean of
the per-window means, the guard trips and the distribution of the per-frame
mean grant (granted_sum / blocks, rounded). A trip is a `SHELL_REMOTE guard
tripped blk=` line; it belongs to the frame printed before it, and is counted
once per blk (the board repeats the line every pass while the guard holds).
Pass, on every workload: both caps have frames, worst(cap) <= worst(off) +
the slope, and trips(cap) <= trips(off). The slope is kRgrainCycles from
rgrain.h (one grain's price, B1's result once Task 10 wrote it), or --slope.

Both modes: the layer check. rms_x1e6 > 0 exactly when grains ran -- n > 0
in mode 1, granted_sum > 0 in mode 2. A frame that breaks it fails its mode:
a layer that runs and reaches nothing, or that sounds without running, prices
the wrong thing.

Exit 0 when every mode the capture holds passes, 1 otherwise (and with no
counted SHELL_RGRAIN frame at all).
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

CLICK = 945000         # the click line (spec section 2, "Worst top-20 workload")
HALF_TOLERANCE = 0.10  # B1: the two halves' slopes, relative to their mean

_HOST_TIME = re.compile(r"^\d+\.\d+ ")
_RUN = re.compile(r"^SHELL_REMOTE run (.*)$")
_TRIP = re.compile(r"^SHELL_REMOTE guard tripped blk=(\d+)")
_WIN = re.compile(r"^SHELL_PLAY_WIN (.*)$")
_RGRAIN = re.compile(r"^SHELL_RGRAIN (.*)$")
_KV = re.compile(r"(\w+)=(-?\d+)")
_CYCLES = re.compile(r"\bkRgrainCycles\s*=\s*(\d+)\s*;")


def _kv(text):
    return {k: int(v) for k, v in _KV.findall(text)}


def rgrain_cycles(path=None):
    """kRgrainCycles from rgrain.h: B2's one-grain margin."""
    path = path or os.path.join(HERE, "rgrain.h")
    with open(path, encoding="utf-8") as f:
        m = _CYCLES.search(f.read())
    if not m:
        raise SystemExit("read_rgrain.py: no kRgrainCycles in %s" % path)
    return int(m.group(1))


def parse(lines):
    """(frames, trips). A frame: {"i", "mode", "n", "cap", "granted", "blocks",
    "swing", "rms", "id", "counting", "void", "tripped", "win", "mean"}, the run
    and WIN fields from the latest such lines of its pass (None without them).
    A trip: {"frame": the index of the frame printed before it, or None,
    "blk"}."""
    frames, trips = [], []
    run, win = None, None
    for raw in lines:
        line = _HOST_TIME.sub("", raw.strip(), count=1)
        m = _RUN.match(line)
        if m:
            run, win = _kv(m.group(1)), None
            continue
        m = _WIN.match(line)
        if m:
            win = _kv(m.group(1))
            continue
        m = _TRIP.match(line)
        if m:
            trips.append({"frame": len(frames) - 1 if frames else None, "blk": int(m.group(1))})
            continue
        m = _RGRAIN.match(line)
        if not m:
            continue
        kv = _kv(m.group(1))
        if not all(k in kv for k in ("mode", "n", "cap", "granted_sum", "blocks", "rms_x1e6")):
            continue
        run = run or {}
        wn = (win or {}).get("n", 0)
        frames.append({
            "i": len(frames), "mode": kv["mode"], "n": kv["n"], "cap": kv["cap"],
            "granted": kv["granted_sum"], "blocks": kv["blocks"], "swing": kv.get("swing", 0),
            "rms": kv["rms_x1e6"], "id": run.get("id"),
            "counting": run.get("counting") == 1, "void": run.get("void", 0) != 0,
            "tripped": run.get("tripped", 0) != 0,
            "win": (win or {}).get("win"),
            "mean": (win["sum"] / wn) if win and wn > 0 and "sum" in win else None,
        })
        win = None   # one WIN line, one frame
    return frames, trips


def counted(f):
    return f["counting"] and not f["void"] and f["mean"] is not None and f["win"] is not None


def layer_problems(frames):
    """The frames whose rms disagrees with whether grains ran: n in mode 1,
    granted_sum in mode 2. ["frame 12 (w127 n=4 rms_x1e6=0)", ...]."""
    out = []
    for f in frames:
        ran = f["n"] > 0 if f["mode"] == 1 else f["granted"] > 0
        if ran != (f["rms"] > 0):
            out.append("frame %d (w%s n=%d granted_sum=%d rms_x1e6=%d)"
                       % (f["i"], f["id"], f["n"], f["granted"], f["rms"]))
    return out


def p99(values):
    s = sorted(values)
    return s[max(0, -(-99 * len(s) // 100) - 1)]


def fit(cells):
    """The common slope of {(workload, n): mean} against n, one intercept per
    workload; a workload with fewer than two n adds nothing. None without a
    spread in n."""
    by = {}
    for (w, n), y in cells.items():
        by.setdefault(w, []).append((n, y))
    sxy = sxx = 0.0
    for pts in by.values():
        if len({n for n, _ in pts}) < 2:
            continue
        nb = sum(n for n, _ in pts) / len(pts)
        yb = sum(y for _, y in pts) / len(pts)
        sxy += sum((n - nb) * (y - yb) for n, y in pts)
        sxx += sum((n - nb) ** 2 for n, _ in pts)
    return sxy / sxx if sxx > 0 else None


def _mean(xs):
    return sum(xs) / len(xs)


def b1(frames):
    """{"slope", "slope_a", "slope_b", "workloads": {id: {"n": {n: {"frames",
    "mean", "p99", "worst"}}, "room": {"mean", "p99", "worst"} or None}},
    "layer": [...], "ok", "why"}."""
    use = [f for f in frames if f["mode"] == 1 and counted(f) and not f["tripped"]]
    groups = {}
    for f in use:
        groups.setdefault((f["id"], f["n"]), []).append(f)
    cells, cells_a, cells_b = {}, {}, {}
    for key, fs in groups.items():
        cells[key] = _mean([f["mean"] for f in fs])
        h = len(fs) // 2
        if h:
            cells_a[key] = _mean([f["mean"] for f in fs[:h]])
        cells_b[key] = _mean([f["mean"] for f in fs[h:]])
    slope, slope_a, slope_b = fit(cells), fit(cells_a), fit(cells_b)

    workloads = {}
    for (w, n), fs in sorted(groups.items()):
        wins = [f["win"] for f in fs]
        workloads.setdefault(w, {"n": {}, "room": None})["n"][n] = {
            "frames": len(fs), "mean": cells[(w, n)], "p99": p99(wins), "worst": max(wins)}
    for w, d in workloads.items():
        z = d["n"].get(0)
        if z and slope and slope > 0:
            d["room"] = {k: int((CLICK - z[k]) // slope) for k in ("mean", "p99", "worst")}

    layer = layer_problems(use)
    why = []
    if not use:
        why.append("no counted mode-1 frames")
    elif slope is None:
        why.append("no workload has two values of n")
    elif slope_a is None or slope_b is None:
        why.append("a half of the rounds has no fit")
    elif abs(slope_a - slope_b) > HALF_TOLERANCE * abs(slope_a + slope_b) / 2:
        why.append("halves %.0f and %.0f differ by %.1f %% (limit %.0f %%)"
                   % (slope_a, slope_b, 200.0 * abs(slope_a - slope_b) / abs(slope_a + slope_b),
                      100 * HALF_TOLERANCE))
    if layer:
        why.append("%d frames whose rms disagrees with n, first %s" % (len(layer), layer[0]))
    return {"slope": slope, "slope_a": slope_a, "slope_b": slope_b, "workloads": workloads,
            "layer": layer, "ok": not why, "why": why}


def b2(frames, trips, slope):
    """{"workloads": {id: {cap: {"frames", "worst", "mean", "trips",
    "granted": {grains: frames}}}}, "layer": [...], "ok", "why"}; cap is 0
    (off) or 1 (on), slope the one-grain margin."""
    use = [f for f in frames if f["mode"] == 2 and counted(f)]
    by = {}
    for f in use:
        c = by.setdefault(f["id"], {}).setdefault(f["cap"], {
            "frames": 0, "worst": 0, "means": [], "blks": set(), "granted": {}})
        c["frames"] += 1
        c["worst"] = max(c["worst"], f["win"])
        c["means"].append(f["mean"])
        g = int(round(f["granted"] / f["blocks"])) if f["blocks"] > 0 else 0
        c["granted"][g] = c["granted"].get(g, 0) + 1
    in_use = {f["i"] for f in use}
    for t in trips:
        if t["frame"] in in_use:
            f = frames[t["frame"]]
            by[f["id"]][f["cap"]]["blks"].add(t["blk"])
    workloads = {}
    for w, caps in by.items():
        workloads[w] = {cap: {"frames": c["frames"], "worst": c["worst"],
                              "mean": _mean(c["means"]), "trips": len(c["blks"]),
                              "granted": dict(sorted(c["granted"].items()))}
                        for cap, c in caps.items()}

    layer = layer_problems(use)
    why = []
    if not use:
        why.append("no counted mode-2 frames")
    for w, caps in sorted(workloads.items(), key=lambda kv: str(kv[0])):
        if 0 not in caps or 1 not in caps:
            why.append("w%s: no frames with the cap %s" % (w, "off" if 0 not in caps else "on"))
            continue
        off, on = caps[0], caps[1]
        if on["worst"] > off["worst"] + slope:
            why.append("w%s: worst %d with the cap, over %d + %d"
                       % (w, on["worst"], off["worst"], slope))
        if on["trips"] > off["trips"]:
            why.append("w%s: %d trips with the cap, %d without" % (w, on["trips"], off["trips"]))
    if layer:
        why.append("%d frames whose rms disagrees with granted_sum, first %s"
                   % (len(layer), layer[0]))
    return {"workloads": workloads, "layer": layer, "ok": not why, "why": why}


def report(lines, out=None, slope=None):
    out = out or sys.stdout
    frames, trips = parse(lines)
    modes = sorted({f["mode"] for f in frames if counted(f)})
    if not modes:
        print("no counted SHELL_RGRAIN frames", file=out)
        return 1
    ok = True
    if 1 in modes:
        r = b1(frames)
        fmt = lambda v: "-" if v is None else "%.0f" % v   # noqa: E731
        print("B1: slope %s cycles per grain (first half %s, second half %s)"
              % (fmt(r["slope"]), fmt(r["slope_a"]), fmt(r["slope_b"])), file=out)
        for w, d in sorted(r["workloads"].items(), key=lambda kv: str(kv[0])):
            for n, s in sorted(d["n"].items()):
                print("  w%s n=%d: %d frames, mean %.0f, p99 win %d, worst %d"
                      % (w, n, s["frames"], s["mean"], s["p99"], s["worst"]), file=out)
            rm = d["room"]
            if rm:
                print("  w%s room (grains to %d): %d at the mean, %d at p99, %d at the worst"
                      % (w, CLICK, rm["mean"], rm["p99"], rm["worst"]), file=out)
        print("B1 verdict: %s" % ("PASS" if r["ok"] else "FAIL (" + "; ".join(r["why"]) + ")"),
              file=out)
        ok = ok and r["ok"]
    if 2 in modes:
        s = slope if slope is not None else rgrain_cycles()
        r = b2(frames, trips, s)
        for w, caps in sorted(r["workloads"].items(), key=lambda kv: str(kv[0])):
            for cap, c in sorted(caps.items()):
                print("  w%s cap=%d: %d frames, worst %d, mean %.0f, trips %d, granted %s"
                      % (w, cap, c["frames"], c["worst"], c["mean"], c["trips"],
                         " ".join("%d:%d" % kv for kv in c["granted"].items())), file=out)
        print("B2 verdict (one grain = %d cycles): %s"
              % (s, "PASS" if r["ok"] else "FAIL (" + "; ".join(r["why"]) + ")"), file=out)
        ok = ok and r["ok"]
    return 0 if ok else 1


def main(argv):
    args = argv[1:]
    slope = None
    if len(args) == 3 and args[1] == "--slope":
        slope = int(args[2])
        args = args[:1]
    if len(args) != 1:
        raise SystemExit("usage: read_rgrain.py capture.txt [--slope CYCLES]")
    with open(args[0], encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    return report(lines, slope=slope)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
