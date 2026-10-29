"""Statistics for the remote CPU hunt (spec 2026-10-29-cpu-hunt-remote-design.md
section 5.4): per-run percentiles of the callback's cycle counts, the A/A noise
floor, and the verdict of a candidate against a baseline.

The board prints, once per 500 ms frame (shell/panel_scan.cpp):

    SHELL_PLAY_LAW cyc_last=.. cyc_max=.. led_last=.. led_max=.. cb_last=.. cb_max=..
    SHELL_PLAY_WIN win=.. gap=.. over=.. n=..        (SHELL_CB_WINDOW, SHELL_REMOTE)

and SHELL_REMOTE images, right before the LAW line,

    SHELL_REMOTE run id=.. counting=.. void=.. gest=..

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


def parse_frames(lines: Iterable[str], load_id: Optional[int] = None,
                 gest_id: Optional[int] = None) -> dict:
    """Pull the per-frame fields out of serial lines. Lines may carry a host
    stamp prefix; anything that is not a LAW, WIN or run line is skipped.

    With load_id or gest_id (shell/panel_scan.cpp, SHELL_REMOTE): the board
    prints `SHELL_REMOTE run id=.. counting=.. void=.. gest=..` right before
    each frame's LAW line, and counting=1 says the whole frame window lay
    after the load's settle. A LAW / WIN line then counts only while the
    latest run line is the asked-for run with counting=1; the others are
    `skipped`. `void` is true once any run line of that run says void=1.
    Without either, every frame counts (captures from before the filter)."""
    out = {"cb_last": [], "cb_max": [], "win": [], "gap": [], "over": [],
           "reconnects": 0, "skipped": 0, "void": False}
    filtered = load_id is not None or gest_id is not None
    take = not filtered
    for line in lines:
        if "#reconnect" in line:
            out["reconnects"] += 1
            continue
        if "SHELL_REMOTE run " in line:
            kv = dict((k, int(v)) for k, v in _KV.findall(line))
            mine = filtered and _run_matches(kv, load_id, gest_id)
            if mine and kv.get("void") == 1:
                out["void"] = True
            if filtered:
                take = mine and kv.get("counting") == 1
            continue
        if ("SHELL_PLAY_LAW" in line or "SHELL_PLAY_WIN" in line) and not take:
            if "SHELL_PLAY_LAW" in line:
                out["skipped"] += 1
            continue
        if "SHELL_PLAY_LAW" in line:
            kv = dict((k, int(v)) for k, v in _KV.findall(line))
            if "cb_last" in kv:
                out["cb_last"].append(kv["cb_last"])
            if "cb_max" in kv:
                out["cb_max"].append(kv["cb_max"])
        elif "SHELL_PLAY_WIN" in line:
            kv = dict((k, int(v)) for k, v in _KV.findall(line))
            if "win" in kv:
                out["win"].append(kv["win"])
            if "gap" in kv:
                out["gap"].append(kv["gap"])
            if "over" in kv:
                out["over"].append(kv["over"])
    return out


def summarize_run(lines: Iterable[str], load_id: Optional[int] = None,
                  gest_id: Optional[int] = None) -> dict:
    """The run's summary.json: `cb_last` (the last block of each frame, zeros
    dropped -- the counter reads 0 before the first block) and `frame_max`
    (the worst block of each frame, SHELL_CB_WINDOW and SHELL_REMOTE images),
    each as `summarize` output; plus the totals the clicks question needs.
    load_id / gest_id: count only the settled frames of that run, see
    parse_frames(); `skipped` frames did not count, `void` = a pot took over."""
    f = parse_frames(lines, load_id, gest_id)
    last = [v for v in f["cb_last"] if v > 0]
    res = {"frames": len(f["cb_last"]), "skipped": f["skipped"], "void": f["void"],
           "reconnects": f["reconnects"]}
    res["cb_last"] = summarize(last) if last else None
    res["frame_max"] = summarize(f["win"]) if f["win"] else None
    res["cb_max_final"] = f["cb_max"][-1] if f["cb_max"] else None
    res["gap_max"] = max(f["gap"]) if f["gap"] else None
    res["over_total"] = sum(f["over"]) if f["over"] else None
    return res


if __name__ == "__main__":
    import sys
    with open(sys.argv[1], encoding="utf-8", errors="replace") as fh:
        print(json.dumps(summarize_run(fh), indent=2))
