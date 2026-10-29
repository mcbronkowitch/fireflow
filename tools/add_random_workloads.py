"""Append random sweep vectors to the board workloads (plan 2026-10-29-cpu-hunt-remote
Task 9 step 2: a pairing whose desk-vs-board rank correlation is below 0.5 gets
20 more random vectors on the board).

    python tools/add_random_workloads.py PASS1_DIR --pairs BBD_BBD,WAVE_WAVE \
        --n 20 --seed 1 [--workloads shell/workloads.json]

PASS1_DIR holds pass 1 of the sweep (`<A>_<B>.csv`, --random 200 --seconds 10,
see the JSON's sweep.note); its rows carry the material the sweep loaded them
with. Per pairing, `--n` vectors drawn with random.Random(seed) from that
pairing's `rnd*` rows with silent_frac < 0.5 that no workload already uses.
They are appended as kind "rnd", named `<A>_<B>_rnd<k>`, after every existing
entry, so no existing id moves. Their desk_ns are pass 1's (10 s, 3 reps), not
pass 2's. tools/pick_workloads.py carries kind "rnd" over when it regenerates.
Then run shell/gen_workloads.py.
"""
import argparse, json, os, random, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pick_workloads as pw


def add(doc, sweep_rows, pairs, n, seed):
    used = {w.get("source", "").split(":", 1)[-1] + "@" + "_".join(w.get("pair", []))
            for w in doc["workloads"]}
    taken = {w["name"] for w in doc["workloads"]}
    rng = random.Random(seed)
    added = []
    for tag in pairs:
        pool = [r for r in sweep_rows
                if "_".join(r["pair"]) == tag and r["vector_id"].startswith("rnd")
                and r["silent_frac"] < 0.5 and f"{r['vector_id']}@{tag}" not in used]
        pool.sort(key=lambda r: r["vector_id"])
        if len(pool) < n:
            sys.exit(f"{tag}: only {len(pool)} unused loud random rows for --n {n}")
        k = 1
        for r in rng.sample(pool, n):
            while f"{tag}_rnd{k}" in taken:
                k += 1
            w = pw._workload(f"{tag}_rnd{k}", "rnd", r)
            taken.add(w["name"])
            added.append(w)
    doc["workloads"].extend(added)
    return added


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("pass1_dir")
    ap.add_argument("--pairs", required=True, help="comma-separated pairings, e.g. BBD_BBD,WAVE_WAVE")
    ap.add_argument("--n", type=int, default=20)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--workloads", default=os.path.join(pw.ROOT, "shell", "workloads.json"))
    a = ap.parse_args(argv)
    pairs = [p for p in a.pairs.split(",") if p]
    doc = json.load(open(a.workloads, encoding="utf-8"))
    csvs = [os.path.join(a.pass1_dir, f"{p}.csv") for p in pairs]
    rows = pw.read_sweep(csvs, len(pw.read_row_names()))
    added = add(doc, rows, pairs, a.n, a.seed)
    doc.setdefault("random", []).append({"pairs": pairs, "n": a.n, "seed": a.seed,
                                         "from": "sweep pass 1 (--random 200 --seconds 10)"})
    with open(a.workloads, "w", encoding="utf-8", newline="\n") as f:
        f.write(pw.dumps(doc) + "\n")
    print(f"{a.workloads}: +{len(added)} rnd workloads, {len(doc['workloads'])} in all")
    return 0


if __name__ == "__main__":
    sys.exit(main())
