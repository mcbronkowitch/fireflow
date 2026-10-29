"""Smoke guard for build/sweep.exe's deck split of the Rev A rows (plan
2026-10-29-cpu-hunt-remote Task 4, fix round 1). The corner vectors put one
deck's rows at a stop; the split comes from a hand list of appended param
pairs in host/sweep/main.cpp (deck_of), so a pair added to control/params.hpp
without that list would silently move a row to the globals.

    python tools/test_sweep_rows.py build/sweep.exe

Checks `sweep --list-rows` against the row names in shell/generated_panel_map.h:
a name ending _A is deck A, _B deck B, anything else global, and the split is
29 / 29 / 15. Plain script, exit code is the verdict.
"""
import csv, os, subprocess, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
from pick_workloads import read_row_names

FAILS = []
def check(name, cond, detail=""):
    print(("ok   " if cond else "FAIL ") + name + (("  -- " + detail) if not cond and detail else ""))
    if not cond: FAILS.append(name)


def expected(name):
    return "A" if name.endswith("_A") else "B" if name.endswith("_B") else "G"


def verify(list_rows_text, names):
    rows = list(csv.DictReader(list_rows_text.splitlines()))
    check("one line per panel row", len(rows) == len(names), f"{len(rows)} vs {len(names)}")
    wrong = [(int(r["row"]), names[int(r["row"])], r["deck"]) for r in rows
             if int(r["row"]) < len(names) and r["deck"] != expected(names[int(r["row"])])]
    check("every row's deck matches its name's suffix", not wrong, repr(wrong))
    count = {d: sum(r["deck"] == d for r in rows) for d in "ABG"}
    check("29 / 29 / 15 split", (count["A"], count["B"], count["G"]) == (29, 29, 15), repr(count))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: test_sweep_rows.py SWEEP_EXE")
    r = subprocess.run([sys.argv[1], "--list-rows"], capture_output=True, text=True)
    check("sweep --list-rows exits 0", r.returncode == 0, r.stderr.strip())
    if r.returncode == 0:
        verify(r.stdout, read_row_names())
    if FAILS:
        print(f"\n{len(FAILS)} FAILED"); sys.exit(1)
    print("\nall passed")
