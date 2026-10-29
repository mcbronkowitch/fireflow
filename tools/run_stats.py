"""Statistics for the remote CPU hunt (spec 2026-10-29-cpu-hunt-remote-design.md
section 5.4): per-run percentiles of the callback's cycle counts, the A/A noise
floor, and the verdict of a candidate against a baseline.

The board prints, once per 500 ms frame (shell/panel_scan.cpp):

    SHELL_PLAY_LAW cyc_last=.. cyc_max=.. led_last=.. led_max=.. cb_last=.. cb_max=..
    SHELL_PLAY_WIN win=.. gap=.. over=.. n=..        (SHELL_CB_WINDOW, SHELL_REMOTE)

and SHELL_REMOTE images, right before the LAW line,

    SHELL_REMOTE run id=.. gen=.. counting=.. void=.. tripped=.. gest=..

plus, once per started run, `SHELL_REMOTE load id=.. gen=.. gest=..` (and for a
gesture `SHELL_REMOTE gest id=.. gen=..` ahead of it), and
`SHELL_REMOTE guard tripped blk=..` on every frame while the overrun guard
holds the callback silent.

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
    gest=..` right before each frame's LAW line, and counting=1 says the whole
    frame window held settled blocks of that gen only. Everything before the
    last ack of the asked-for run (its load / gest line) is ignored, and the
    ack's `gen` then picks this run's run lines out of any earlier run of the
    same id. A LAW / WIN line counts only while the latest run line is that
    gen with counting=1 and tripped=0; the others are `skipped`. `void` is
    true once a run line of that gen says void=1. `tripped` is true once one
    says tripped=1 or a `guard tripped` line follows the ack: the callback
    plays silence from the trip, so no frame from there on counts. No ack in
    the capture: nothing counts, `gen` is None.
    Without either id, every frame counts (captures from before the filter)."""
    out = {"cb_last": [], "cb_max": [], "win": [], "gap": [], "over": [],
           "reconnects": 0, "skipped": 0, "void": False, "tripped": False,
           "gen": None}
    lines = list(lines)
    filtered = load_id is not None or gest_id is not None
    take = not filtered
    start = 0
    if filtered:
        ack_at, out["gen"] = _find_ack(lines, load_id, gest_id)
        start = len(lines) if ack_at is None else ack_at + 1
        for line in lines[:start]:
            if "#reconnect" in line:
                out["reconnects"] += 1
            elif "SHELL_PLAY_LAW" in line:
                out["skipped"] += 1
    for line in lines[start:]:
        if "#reconnect" in line:
            out["reconnects"] += 1
            continue
        if filtered and "SHELL_REMOTE guard tripped" in line:
            out["tripped"] = True   # the next run line stops the count
            continue
        if "SHELL_REMOTE run " in line:
            kv = dict((k, int(v)) for k, v in _KV.findall(line))
            mine = (filtered and kv.get("gen") == out["gen"]
                    and _run_matches(kv, load_id, gest_id))
            if mine and kv.get("void") == 1:
                out["void"] = True
            if mine and kv.get("tripped") == 1:
                out["tripped"] = True
            if filtered:
                take = mine and kv.get("counting") == 1 and not out["tripped"]
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
    parse_frames(); `skipped` frames did not count, `void` = a pot took over,
    `tripped` = the overrun guard silenced the run (nothing counts from the
    trip on), `gen` = the run's gen from its ack (None: no ack seen)."""
    f = parse_frames(lines, load_id, gest_id)
    last = [v for v in f["cb_last"] if v > 0]
    res = {"frames": len(f["cb_last"]), "skipped": f["skipped"], "void": f["void"],
           "tripped": f["tripped"], "gen": f["gen"], "reconnects": f["reconnects"]}
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
