"""Pick the board's workloads from the law-driven sweep (plan 2026-10-29-cpu-hunt-remote
Task 4, spec sections 5.1 and 5.2).

    python tools/pick_workloads.py SWEEP.csv... --top 5 --out shell/workloads.json

Per pairing (eng_a, eng_b) of the sweep CSVs (build/sweep.exe output): the
`--top` vectors by max_ns among those with silent_frac < 0.5, plus every corner
vector (`corner_*`, kept whatever their cost or silence). Then the hand-written
entries: the R4 all-noon frame as a workload, and the gestures of spec 5.2 as
row/value sequences at a fixed rate (GESTURES below). An existing --out file's
workloads with "kind": "hand" that this script does not write itself (e.g.
`bastian`, captured on the board in Task 7) are carried over.

Rows are the Rev A panel table's (shell/generated_panel_map.h), named from its
`// row N NAME` comments; values are x1000 of the normalised pot value, as
SHELL_PLAY_V prints them and apply_boot_patch_rows() reads them.
"""
import argparse, csv, json, os, re, sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PANEL_MAP = os.path.join(ROOT, "shell", "generated_panel_map.h")
NOON_H = os.path.join(ROOT, "shell", "generated_boot_patch_noon.h")

# Gesture rate: a pot turned at 10 x1000 every 4 blocks -- full travel in 400
# blocks (0.8 s at 96 samples / 48 kHz). One rate for every turn, so the
# gestures are comparable with each other.
STEP_X1000 = 10
STEP_BLOCKS = 4
# Key pseudo-rows (no pot row exists for a key). Task 5 applies them through
# apply_key_edges() with the KeyMap's MOD bit, SHIFT held for KEY_SHIFT_MOD.
KEY_ROWS = {-1: "KEY_MOD", -2: "KEY_SHIFT_MOD"}


def read_row_names(path=PANEL_MAP):
    names = {}
    for m in re.finditer(r"//\s*row\s+(\d+)\s+(\S+)", open(path, encoding="utf-8").read()):
        names[int(m.group(1))] = m.group(2)
    n = len(names)
    if sorted(names) != list(range(n)) or n == 0:
        sys.exit(f"{path}: row comments are not 0..N-1")
    return [names[i] for i in range(n)]


def read_noon(path=NOON_H):
    txt = open(path, encoding="utf-8").read()
    body = txt[txt.index("kBootPatchNoonRows"):]
    body = body[body.index("{") + 1: body.index("}")]
    body = re.sub(r"//[^\n]*", "", body)
    return [int(x) for x in re.findall(r"-?\d+", body)]


def read_sweep(paths, n_rows):
    rows = []
    for p in paths:
        with open(p, newline="") as f:
            for r in csv.DictReader(f):
                vec = [int(r[f"row{i}"]) for i in range(n_rows)]
                if f"row{n_rows}" in r:
                    sys.exit(f"{p}: more row columns than the panel table's {n_rows}")
                rows.append({
                    "vector_id": r["vector_id"], "pair": (r["eng_a"], r["eng_b"]),
                    "p50_ns": float(r["p50_ns"]), "p99_ns": float(r["p99_ns"]),
                    "max_ns": float(r["max_ns"]), "silent_frac": float(r["silent_frac"]),
                    "rows_x1000": vec, "csv": os.path.basename(p)})
    return rows


def pick(sweep_rows, top):
    """Per pairing: top `top` non-corner by max_ns (silent_frac < 0.5), then the corners."""
    by_pair = defaultdict(list)
    for r in sweep_rows:
        by_pair[r["pair"]].append(r)
    out = []
    for pair in sorted(by_pair):
        rs = by_pair[pair]
        corners = sorted((r for r in rs if r["vector_id"].startswith("corner_")),
                         key=lambda r: r["vector_id"])
        loud = [r for r in rs if not r["vector_id"].startswith("corner_") and r["silent_frac"] < 0.5]
        loud.sort(key=lambda r: (-r["max_ns"], r["vector_id"]))
        tag = f"{pair[0]}_{pair[1]}"
        for k, r in enumerate(loud[:top], 1):
            out.append(_workload(f"{tag}_top{k}", "top", r, rank=k))
        for r in corners:
            out.append(_workload(f"{tag}_{r['vector_id']}", "corner", r))
    return out


def _workload(name, kind, r, rank=None):
    w = {"name": name, "kind": kind, "pair": list(r["pair"]), "source": f"{r['csv']}:{r['vector_id']}",
         "desk_ns": {"p50": r["p50_ns"], "p99": r["p99_ns"], "max": r["max_ns"]},
         "silent_frac": r["silent_frac"], "rows_x1000": r["rows_x1000"]}
    if rank is not None:
        w["rank"] = rank
    return w


# ---- gestures (spec 5.2), written by hand ----------------------------------

def _turn(row, frm, to, at):
    """Steps turning `row` from `frm` to `to` at the fixed rate, from block `at`.
    Returns (steps, block after the last step)."""
    steps, v, b = [], frm, at
    d = STEP_X1000 if to >= frm else -STEP_X1000
    while v != to:
        v = min(v + d, to) if d > 0 else max(v + d, to)
        steps.append({"block": b, "row": row, "value_x1000": v})
        b += STEP_BLOCKS
    return steps, b


def _seq(base, *moves):
    """moves: ("turn", row, to) | ("set", row, value) | ("key", row) | ("hold", blocks).
    Turns start from the row's current value (base, then earlier moves)."""
    cur, steps, b = list(base), [], 0
    for m in moves:
        if m[0] == "turn":
            s, b = _turn(m[1], cur[m[1]], m[2], b)
            steps += s
            cur[m[1]] = m[2]
        elif m[0] == "set":
            steps.append({"block": b, "row": m[1], "value_x1000": m[2]})
            cur[m[1]] = m[2]
            b += STEP_BLOCKS
        elif m[0] == "key":
            steps.append({"block": b, "row": m[1], "value_x1000": 0})
            b += STEP_BLOCKS
        elif m[0] == "hold":
            b += m[1]
    return steps, b


def gestures(row_names, base_name, base):
    idx = {n: i for i, n in enumerate(row_names)}
    def r(n):
        if n not in idx:
            sys.exit(f"gesture row {n} is not in the panel table")
        return idx[n]
    defs = [
        ("engine_turn_A", "ENGINE A from its base to the left stop, to the right stop and back: every slot, every engine swap",
         [("turn", r("ENGINE_A"), 0), ("turn", r("ENGINE_A"), 1000), ("turn", r("ENGINE_A"), base[r("ENGINE_A")])]),
        ("engine_turn_B", "ENGINE B the same way",
         [("turn", r("ENGINE_B"), 0), ("turn", r("ENGINE_B"), 1000), ("turn", r("ENGINE_B"), base[r("ENGINE_B")])]),
        ("tide_sweep", "TIDE to the left stop, to the right stop and back",
         [("turn", r("TIDE"), 0), ("turn", r("TIDE"), 1000), ("turn", r("TIDE"), base[r("TIDE")])]),
        ("pull_sweep", "PULL to the left stop, to the right stop and back",
         [("turn", r("PULL"), 0), ("turn", r("PULL"), 1000), ("turn", r("PULL"), base[r("PULL")])]),
        ("morph_sweep", "MORPH to the left stop, to the right stop and back",
         [("turn", r("MORPH"), 0), ("turn", r("MORPH"), 1000), ("turn", r("MORPH"), base[r("MORPH")])]),
        ("bbd_swap_A", "deck A turned onto the BBD slot (800), held 2000 blocks, turned back",
         [("turn", r("ENGINE_A"), 800), ("hold", 2000), ("turn", r("ENGINE_A"), base[r("ENGINE_A")])]),
        ("bbd_swap_B", "deck B turned onto the BBD slot (800), held 2000 blocks, turned back (fault R2's move)",
         [("turn", r("ENGINE_B"), 800), ("hold", 2000), ("turn", r("ENGINE_B"), base[r("ENGINE_B")])]),
        ("reverb_clear", "both REV_MIX sends closed (the room sleeps and clears over 32 ticks), held 1000 blocks, REV_MIX_A reopened in one step",
         [("turn", r("REV_MIX_A"), 0), ("turn", r("REV_MIX_B"), 0), ("hold", 1000),
          ("set", r("REV_MIX_A"), base[r("REV_MIX_A")]), ("hold", 1000)]),
        ("mod_clear", "MOD latched, six pots turned to the right stop (they write depth twins), MOD released, held 1000 blocks, SHIFT + MOD clears the layer",
         [("key", -1), ("turn", r("RATE_A"), 1000), ("turn", r("SHAPE_A"), 1000), ("turn", r("FLUX_A"), 1000),
          ("turn", r("RATE_B"), 1000), ("turn", r("SHAPE_B"), 1000), ("turn", r("REV_MIX_A"), 1000),
          ("key", -1), ("hold", 1000), ("key", -2), ("hold", 1000)]),
    ]
    out = []
    for name, what, moves in defs:
        steps, end = _seq(base, *moves)
        used = sorted({s["row"] for s in steps})
        out.append({
            "name": name, "base": base_name, "what": what,
            "rate": {"x1000_per_step": STEP_X1000, "blocks_per_step": STEP_BLOCKS},
            "blocks": end,
            "rows": [{"row": u, "name": KEY_ROWS.get(u) or row_names[u]} for u in used],
            "steps": steps})
    return out


def dumps(x, ind=0):
    """JSON with one line per flat list / flat object (a vector, a step), so
    the file diffs per workload rather than per number."""
    flat = lambda v: not isinstance(v, (list, dict))
    if isinstance(x, list) and all(flat(v) for v in x):
        return json.dumps(x)
    if isinstance(x, dict) and all(flat(v) for v in x.values()):
        return json.dumps(x)
    pad, pad1 = " " * ind, " " * (ind + 1)
    if isinstance(x, list):
        return "[\n" + ",\n".join(pad1 + dumps(v, ind + 1) for v in x) + "\n" + pad + "]"
    if isinstance(x, dict):
        return "{\n" + ",\n".join(f"{pad1}{json.dumps(k)}: {dumps(v, ind + 1)}"
                                  for k, v in x.items()) + "\n" + pad + "}"
    return json.dumps(x)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("csv", nargs="+")
    ap.add_argument("--top", type=int, default=5)
    ap.add_argument("--out", required=True)
    ap.add_argument("--sweep-note", default="",
                    help="how the CSVs were produced (seconds, reps, input), stored in the JSON")
    a = ap.parse_args(argv)

    names = read_row_names()
    n = len(names)
    sweep_rows = read_sweep(a.csv, n)
    picked = pick(sweep_rows, a.top)

    noon = read_noon()
    if len(noon) != n:
        sys.exit(f"generated_boot_patch_noon.h has {len(noon)} rows, the panel table {n}")
    hand = [{"name": "r4_noon", "kind": "hand",
             "source": "shell/generated_boot_patch_noon.h (fault R4's all-noon frame, captured on Rev A #1)",
             "rows_x1000": noon}]
    mine = {w["name"] for w in hand}
    if os.path.exists(a.out):
        old = json.load(open(a.out, encoding="utf-8"))
        for w in old.get("workloads", []):
            if w.get("kind") == "hand" and w["name"] not in mine:
                hand.append(w)

    doc = {
        "about": ("Board workloads for the remote CPU hunt (spec 2026-10-29-cpu-hunt-remote "
                  "sections 5.1/5.2). GENERATED by tools/pick_workloads.py from the law-driven "
                  "sweep (build/sweep.exe); hand entries are kind=hand and the gestures. "
                  "rows_x1000: one value per kRevaTable row, x1000 of the normalised pot value. "
                  "Gesture steps: at `block` (from the gesture's start) set `row` to "
                  "`value_x1000` through the pot path; row -1 = a MOD key press, row -2 = "
                  "SHIFT held + MOD press (clear_mod_layer). `bastian` is captured in Task 7."),
        "table": "kRevaTable (shell/generated_panel_map.h)",
        "row_count": n,
        "rows": names,
        "block": 96,
        "sample_rate": 48000,
        "sweep": {"csv": sorted({os.path.basename(p) for p in a.csv}), "top": a.top,
                  "note": a.sweep_note,
                  "material": ("BBD decks: excite_other_deck held across the switch; SAMPLER "
                               "decks: REC latched for a 2500-block settle, then released; "
                               "audio in: low-passed noise about -12 dBFS")},
        "workloads": picked + hand,
        "gestures": gestures(names, "r4_noon", noon),
    }
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(dumps(doc) + "\n")
    pairs = sorted({tuple(w["pair"]) for w in picked})
    print(f"{a.out}: {len(picked)} swept workloads over {len(pairs)} pairings, "
          f"{len(hand)} hand, {len(doc['gestures'])} gestures")
    return 0


if __name__ == "__main__":
    sys.exit(main())
