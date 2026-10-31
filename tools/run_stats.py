"""Statistics for the remote CPU hunt (spec 2026-10-29-cpu-hunt-remote-design.md
section 5.4): per-run percentiles of the callback's cycle counts, the A/A noise
floor, and the verdict of a candidate against a baseline.

The board prints, once per 500 ms frame (shell/panel_scan.cpp):

    SHELL_PLAY_LAW cyc_last=.. cyc_max=.. led_last=.. led_max=.. cb_last=.. cb_max=..
    SHELL_PLAY_WIN win=.. gap=.. over=.. n=.. sum=.. (SHELL_CB_WINDOW, SHELL_REMOTE)

and SHELL_REMOTE images, right before the LAW line,

    SHELL_REMOTE run id=.. gen=.. counting=.. void=.. tripped=.. trips=.. gest=.. tog=<8 hex>

(`tripped`: the overrun guard was tripped at some block of that frame's window;
`trips`: how often it has tripped since the run's ack. Older images print no
`trips=`, read as 0.)

plus, once per started run, `SHELL_REMOTE load id=.. gen=.. gest=..` (and for a
gesture `SHELL_REMOTE gest id=.. gen=..` ahead of it), and
`SHELL_REMOTE guard tripped blk=.. by=cb|period` on every frame while the
overrun guard holds the callback silent, `SHELL_REMOTE guard rearm blk=..
by=silence|settle` when it resumes by itself (not read here),
`SHELL_REMOTE gest done id=..` once a
gesture's last step has played, and `SHELL_REMOTE on build=<stamp>` (with
` measure=1 tog=<8 hex>` on the measurement image) every 5 s. The measurement
image adds, 200 blocks after a TOG, `SHELL_REMOTE tog settled word=<8 hex>
gen=.. rst`: a TOG during a run re-settles it under that new gen.

`cb_last` is the last block's callback cycles in that frame, `win` the worst
block since the previous frame. `parse_frames` reads both and ignores every
other line, so a firmware that prints more does not break it.
"""
import json
import math
import re
from typing import Iterable, Optional, Sequence

# Blocks above this count toward `over_930k`. The deadline lies somewhere
# between ~930 k and ~960 k (docs/gotchas.md, cpu-headroom section 3).
OVER_CYCLES = 930_000

KEYS = ("n", "p50", "p99", "p999", "max", "over_930k")

_KV = re.compile(r"(\w+)=(-?\d+)")
# The toggle word is hex, which _KV would cut at its first letter.
_TOG = re.compile(r"\btog=([0-9a-fA-F]{8})\b")
# The image's stamp (7 hex, `+` for a dirty tree) and, on the measurement
# image, measure=1; and a load / gest ack's name.
_ON = re.compile(r"SHELL_REMOTE on build=(\S+)(?:\s+measure=(\d+))?")
_NAME = re.compile(r"\bname=(\S+)")
_GEST_DONE = re.compile(r"SHELL_REMOTE gest done id=(-?\d+)")

# Toggle bits that make a saturated run look clean (engine/util/probe_toggles.h):
# 30 burns the last LATE on every block, 31 stops consulting the overrun guard.
PROBE_BITS = (30, 31)
_PROBE_MASK = sum(1 << b for b in PROBE_BITS)


def _rank(sorted_vals: Sequence[int], q: float) -> int:
    """Nearest-rank percentile: the smallest value with at least q of the
    sample at or below it. No interpolation, so the result is a real block."""
    n = len(sorted_vals)
    return sorted_vals[max(0, math.ceil(q * n) - 1)]


def summarize(cb: Sequence[int]) -> dict:
    if len(cb) == 0:
        raise ValueError("summarize: no samples")
    s = sorted(int(v) for v in cb)
    return {
        "n": len(s),
        "p50": _rank(s, 0.50),
        "p99": _rank(s, 0.99),
        "p999": _rank(s, 0.999),
        "max": s[-1],
        "over_930k": sum(1 for v in s if v > OVER_CYCLES),
    }


def summarize_means(means: Sequence[float]) -> dict:
    """p50 / p99 / mean over the frames' mean block (sum / n of each WIN line)."""
    if len(means) == 0:
        raise ValueError("summarize_means: no samples")
    s = sorted(float(v) for v in means)
    return {"n": len(s), "p50": _rank(s, 0.50), "p99": _rank(s, 0.99),
            "mean": sum(s) / len(s)}


def aa_floor(a: dict, b: dict) -> dict:
    """Absolute difference per key of two runs of the same workload on the
    same image: what a candidate has to beat before it counts."""
    return {k: abs(a[k] - b[k]) for k in KEYS if k in a and k in b}


def verdict(base: dict, cand: dict, floor: dict) -> str:
    """"saves" when p99 and max are both lower than base by more than the
    floor's p99 / max; "costs" when either is higher by more than its floor
    (checked first, so p99 down but max up is a cost); otherwise "noise"."""
    d99 = cand["p99"] - base["p99"]
    dmax = cand["max"] - base["max"]
    f99, fmax = floor["p99"], floor["max"]
    if d99 > f99 or dmax > fmax:
        return "costs"
    if -d99 > f99 and -dmax > fmax:
        return "saves"
    return "noise"


def _run_matches(kv: dict, load_id: Optional[int], gest_id: Optional[int]) -> bool:
    """A `SHELL_REMOTE run` line belongs to the asked-for run: a LOAD of
    load_id (no gesture), or the gesture gest_id (whatever its base id)."""
    gest = kv.get("gest", -1)
    if gest_id is not None:
        return gest == gest_id
    return kv.get("id") == load_id and gest == -1


def _find_ack(lines: Sequence[str], load_id: Optional[int],
              gest_id: Optional[int]):
    """(index, gen) of the last ack of the asked-for run, or (None, None): the
    gesture's `SHELL_REMOTE gest id=G gen=N` line, or a plain LOAD's
    `SHELL_REMOTE load id=X gen=N gest=-1` line (a gesture's base load line
    carries its gest id, so it is not a LOAD of that id)."""
    found = (None, None)
    for i, line in enumerate(lines):
        if gest_id is not None:
            if "SHELL_REMOTE gest id=" not in line:
                continue
            kv = dict((k, int(v)) for k, v in _KV.findall(line))
            if kv.get("id") == gest_id and "gen" in kv:
                found = (i, kv["gen"])
        elif "SHELL_REMOTE load id=" in line:
            kv = dict((k, int(v)) for k, v in _KV.findall(line))
            if kv.get("id") == load_id and kv.get("gest") == -1 and "gen" in kv:
                found = (i, kv["gen"])
    return found


def parse_frames(lines: Iterable[str], load_id: Optional[int] = None,
                 gest_id: Optional[int] = None) -> dict:
    """Pull the per-frame fields out of serial lines. Lines may carry a host
    stamp prefix; anything that is not a LAW, WIN or run line is skipped.

    With load_id or gest_id (shell/panel_scan.cpp, SHELL_REMOTE): the board
    prints `SHELL_REMOTE run id=.. gen=.. counting=.. void=.. tripped=..
    trips=.. gest=..` right before each frame's LAW line, and counting=1 says
    the whole frame window held settled blocks of that gen only. Everything
    before the last ack of the asked-for run (its load / gest line) is ignored,
    and the ack's `gen` then picks this run's run lines out of any earlier run
    of the same id. A LAW / WIN line counts while the latest run line is that
    gen with counting=1, whether or not the guard was tripped in the frame
    (the guard re-arms itself, Bastian 2026-10-29: a trip silences part of a
    frame, it does not end the run); the others are `skipped`. `void` is true
    once a run line of that gen says void=1. `tripped` is true once one says
    tripped=1 or trips>0, or a `guard tripped` line follows the ack: the guard
    tripped at some time in the run (informational). `trips`: the last trips=
    of a counted frame (0 for a line without one); `tripped_frames`: the
    counted frames with tripped=1; the `*_u` lists hold the counted frames
    with tripped=0 only. No ack in the capture: nothing counts, `gen` is None.

    A `SHELL_REMOTE tog settled ... gen=N` line after the ack: a TOG
    re-settled the run under gen N (the measurement image), so from there gen
    N is followed, and the frames counted so far -- played with the old toggle
    word -- move to `skipped`. `tog`: the run line's toggle word of every
    counted frame (8 hex digits, distinct, sorted); `probe` is true when one of
    them has a PROBE_BITS bit set; `tog_missing` counts the counted frames
    whose run line had no `tog=` (they add nothing to `tog`).

    A run line decides its own frame only: the decision ends with the frame's
    WIN line, and at a `#reconnect` (a run line lost with the port must not
    let the next LAW / WIN count under the previous frame's decision). A
    gesture run stops counting at its `SHELL_REMOTE gest done id=` line after
    the ack (`gest_done`): what follows is its base workload standing still.
    `build` / `measure`: the last `SHELL_REMOTE on` line anywhere in the
    capture (measure 1 on the measurement image, else 0; both None without
    one); `name`: the ack's name= (the gesture's for a gesture run).
    Without either id, every frame counts (captures from before the filter)."""
    out = {"cb_last": [], "cb_max": [], "win": [], "gap": [], "over": [],
           "mean": [], "mean_u": [],
           "reconnects": 0, "skipped": 0, "void": False, "tripped": False,
           "gen": None, "tog": set(), "probe": False, "tog_missing": 0,
           "gest_done": False, "build": None, "measure": None, "name": None,
           "trips": 0, "tripped_frames": 0, "cb_last_u": [], "cb_max_u": [],
           "win_u": [], "gap_u": [], "over_u": []}
    lines = list(lines)
    for line in lines:
        m = _ON.search(line)
        if m:
            out["build"] = m.group(1)
            out["measure"] = int(m.group(2)) if m.group(2) is not None else 0
    filtered = load_id is not None or gest_id is not None
    take = not filtered
    tog = None   # the latest run line's toggle word
    frame_tripped = False   # the latest run line's tripped=1 (its own frame's)
    frame_trips = 0         # ... and its trips= (0 when it has none)
    start = 0
    if filtered:
        ack_at, out["gen"] = _find_ack(lines, load_id, gest_id)
        start = len(lines) if ack_at is None else ack_at + 1
        if ack_at is not None:
            m = _NAME.search(lines[ack_at])
            out["name"] = m.group(1) if m else None
        for line in lines[:start]:
            if "#reconnect" in line:
                out["reconnects"] += 1
            elif "SHELL_PLAY_LAW" in line:
                out["skipped"] += 1
    for line in lines[start:]:
        if "#reconnect" in line:
            out["reconnects"] += 1
            if filtered:
                take = False   # the next frame's run line decides
            frame_tripped, frame_trips = False, 0
            continue
        if filtered and "SHELL_REMOTE guard tripped" in line:
            out["tripped"] = True   # informational: the count goes on
            continue
        if gest_id is not None and "SHELL_REMOTE gest done" in line:
            m = _GEST_DONE.search(line)
            if m and int(m.group(1)) == gest_id:
                out["gest_done"] = True   # the next run line stops the count
            continue
        if filtered and "SHELL_REMOTE tog settled" in line:
            kv = dict((k, int(v)) for k, v in _KV.findall(line))
            if "gen" in kv and kv["gen"] != out["gen"]:
                out["skipped"] += len(out["cb_last"])
                for key in ("cb_last", "cb_max", "win", "gap", "over", "cb_last_u",
                            "cb_max_u", "win_u", "gap_u", "over_u", "mean",
                            "mean_u"):
                    out[key] = []
                out["tripped_frames"] = 0
                out["tog"], out["probe"] = set(), False
                out["gen"] = kv["gen"]
            take = False   # the next run line of that gen decides
            continue
        if "SHELL_REMOTE run " in line:
            m = _TOG.search(line)
            tog = m.group(1).lower() if m else None
            kv = dict((k, int(v)) for k, v in _KV.findall(line))
            mine = (filtered and kv.get("gen") == out["gen"]
                    and _run_matches(kv, load_id, gest_id))
            if mine and kv.get("void") == 1:
                out["void"] = True
            if mine and (kv.get("tripped") == 1 or kv.get("trips", 0) > 0):
                out["tripped"] = True
            # This frame's own flag and the run's trips so far; a line of
            # another run says nothing (unfiltered captures take every line).
            ours = mine or not filtered
            frame_tripped = ours and kv.get("tripped") == 1
            frame_trips = kv.get("trips", 0) if ours else 0
            if filtered:
                take = mine and kv.get("counting") == 1 and not out["gest_done"]
            continue
        if ("SHELL_PLAY_LAW" in line or "SHELL_PLAY_WIN" in line) and not take:
            if "SHELL_PLAY_LAW" in line:
                out["skipped"] += 1
            continue
        if "SHELL_PLAY_LAW" in line:
            if tog is not None:
                out["tog"].add(tog)
                if int(tog, 16) & _PROBE_MASK:
                    out["probe"] = True
            elif filtered:
                out["tog_missing"] += 1
            kv = dict((k, int(v)) for k, v in _KV.findall(line))
            out["trips"] = frame_trips
            if frame_tripped:
                out["tripped_frames"] += 1
            if "cb_last" in kv:
                out["cb_last"].append(kv["cb_last"])
                if not frame_tripped:
                    out["cb_last_u"].append(kv["cb_last"])
            if "cb_max" in kv:
                out["cb_max"].append(kv["cb_max"])
                if not frame_tripped:
                    out["cb_max_u"].append(kv["cb_max"])
        elif "SHELL_PLAY_WIN" in line:
            kv = dict((k, int(v)) for k, v in _KV.findall(line))
            for name in ("win", "gap", "over"):
                if name in kv:
                    out[name].append(kv[name])
                    if not frame_tripped:
                        out[name + "_u"].append(kv[name])
            # The window's mean block, sum / n. An older image prints no sum=,
            # and the board prints sum=0 n=0 when it could not read the pair
            # consistently: neither says anything about the mean.
            if kv.get("sum") is not None and kv.get("n", 0) > 0:
                out["mean"].append(kv["sum"] / kv["n"])
                if not frame_tripped:
                    out["mean_u"].append(kv["sum"] / kv["n"])
            if filtered:
                take = False   # the frame ends here; the next run line decides
    return out


def summarize_run(lines: Iterable[str], load_id: Optional[int] = None,
                  gest_id: Optional[int] = None, expect_tog: Optional[int] = None) -> dict:
    """The run's summary.json: `cb_last` (the last block of each frame, zeros
    dropped -- the counter reads 0 before the first block) and `frame_max`
    (the worst block of each frame, SHELL_CB_WINDOW and SHELL_REMOTE images),
    each as `summarize` output; plus the totals the clicks question needs.
    load_id / gest_id: count only the settled frames of that run, see
    parse_frames(); `skipped` frames did not count, `void` = a pot took over,
    `tripped` = the overrun guard tripped at some time in the run (the count
    goes on: the guard re-arms itself), `trips` = its trips since the ack (the
    last trips= of a counted frame), `tripped_frames` = the counted frames
    with tripped=1, `untripped` = the statistics above (frames, cb_last,
    frame_max, cb_max_final, gap_max, over_total) over the counted frames with
    tripped=0 only -- the frames the guard did not silence a block of; the
    top-level ones cover every counted frame. `gen` = the run's gen from its
    ack (None: no ack seen), or the
    gen a TOG re-settled it under; `tog` = the counted frames' toggle words.
    A counted frame with bit 30 or 31 set makes the run `void` with an
    `error` (ProbeBits): its frames say nothing about a candidate.
    expect_tog: the toggle word the run was meant to play (`expect_tog` in the
    summary, 8 hex digits); a counted frame with another word, or without a
    `tog=` to confirm it, is an `error` (ToggleMismatch) -- a word left over
    from an earlier run would otherwise measure the wrong candidate. ProbeBits
    is reported first. `gest_done`, `build`, `measure` and `name`: see
    parse_frames(). `error` is None otherwise."""
    f = parse_frames(lines, load_id, gest_id)
    last = [v for v in f["cb_last"] if v > 0]
    res = {"frames": len(f["cb_last"]), "skipped": f["skipped"], "void": f["void"],
           "tripped": f["tripped"], "gen": f["gen"], "reconnects": f["reconnects"],
           "tog": sorted(f["tog"]), "gest_done": f["gest_done"], "build": f["build"],
           "measure": f["measure"], "name": f["name"],
           "expect_tog": None if expect_tog is None else "%08x" % expect_tog, "error": None}
    if f["probe"]:
        res["void"] = True
        res["error"] = ("ProbeBits: bit 30 or 31 set in a counted frame (tog=%s)"
                        % ",".join(res["tog"]))
    elif expect_tog is not None:
        other = [t for t in res["tog"] if t != res["expect_tog"]]
        if other or f["tog_missing"]:
            res["error"] = ("ToggleMismatch: counted frames ran with tog=%s%s, expected %s"
                            % (",".join(other) or "-",
                               " (%d without tog=)" % f["tog_missing"] if f["tog_missing"] else "",
                               res["expect_tog"]))
    res["cb_last"] = summarize(last) if last else None
    res["frame_max"] = summarize(f["win"]) if f["win"] else None
    res["cb_max_final"] = f["cb_max"][-1] if f["cb_max"] else None
    res["gap_max"] = max(f["gap"]) if f["gap"] else None
    res["over_total"] = sum(f["over"]) if f["over"] else None
    # The guard: trips so far, the counted frames it was tripped in, and the
    # same statistics over the counted frames it was not tripped in.
    res["trips"] = f["trips"]
    res["tripped_frames"] = f["tripped_frames"]
    last_u = [v for v in f["cb_last_u"] if v > 0]
    res["untripped"] = {
        "frames": len(f["cb_last_u"]),
        "cb_last": summarize(last_u) if last_u else None,
        "frame_max": summarize(f["win_u"]) if f["win_u"] else None,
        "cb_max_final": f["cb_max_u"][-1] if f["cb_max_u"] else None,
        "gap_max": max(f["gap_u"]) if f["gap_u"] else None,
        "over_total": sum(f["over_u"]) if f["over_u"] else None,
    }
    # `mean`: only when some frame carried sum= (images from before it, and
    # frames the board could not read, leave it out rather than say 0).
    if f["mean"]:
        res["mean"] = summarize_means(f["mean"])
    if f["mean_u"]:
        res["untripped"]["mean"] = summarize_means(f["mean_u"])
    return res


if __name__ == "__main__":
    import sys
    with open(sys.argv[1], encoding="utf-8", errors="replace") as fh:
        print(json.dumps(summarize_run(fh), indent=2))
