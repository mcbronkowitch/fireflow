#!/usr/bin/env python3
"""Guard for shell/gen_workloads.py (plan 2026-10-29-cpu-hunt-remote Task 5).
Plain script -- pytest is not installed here -- and its exit code is the
verdict. The header is parsed back here with its own regexes, not through the
generator, so a round trip that agrees with itself cannot pass by accident.
Every refusal the generator makes has a sabotage that must turn it red.
"""
import copy
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import gen_workloads as g  # noqa: E402

FAILS = []


def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        FAILS.append(name)


BASE = g.load_inputs()
DOC = BASE["doc"]
TEXT = g.build(**BASE)

_WL = re.compile(r'\{"([^"]+)",\s*\{([-\d,\s]+)\},\s*\{(\d), (\d)\},\s*\{(\d), (\d)\},\s*(\d+),\s*'
                 r'WorkloadInput::(\w+)\},\s*// id (\d+)', re.S)
_GD = re.compile(r'\{"([^"]+)",\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)u\},\s*// gesture (\d+)')
_GS = re.compile(r'^\s*\{(\d+)u,\s*(-?\d+),\s*(-?\d+)\},', re.M)


def parse(text):
    wl = [(m.group(1), [int(x) for x in m.group(2).replace(",", " ").split()],
           [m.group(3) == "1", m.group(4) == "1"], [m.group(5) == "1", m.group(6) == "1"],
           int(m.group(7)), m.group(8).lower(), int(m.group(9))) for m in _WL.finditer(text)]
    gd = [(m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5)),
           int(m.group(6))) for m in _GD.finditer(text)]
    steps_part = text.rsplit("kGestureSteps[kGestureStepTotal] =", 1)[-1].split("};", 1)[0]
    gs = [(int(a), int(b), int(c)) for a, b, c in _GS.findall(steps_part)]
    return wl, gd, gs


WL, GD, GS = parse(TEXT)
names = [w["name"] for w in DOC["workloads"]]

# --- live ---------------------------------------------------------------
check("committed header exists and equals the generator's output",
      os.path.exists(g.OUT) and g.check_text(g.read_committed(), TEXT))
check("a CRLF checkout is not stale", g.check_text(TEXT.replace("\n", "\r\n"), TEXT))
check("every workload is in the header, in JSON order, id = index",
      len(WL) == len(DOC["workloads"]) == 146 and [w[6] for w in WL] == list(range(len(WL))))
check("round trip: names, rows, excite, rec, settle and input equal the JSON",
      all(w[0] == j["name"] and w[1] == j["rows_x1000"] and w[2] == j["excite"]
          and w[3] == j["rec"] and w[4] == j["settle_blocks"] and w[5] == j["input"]
          for w, j in zip(WL, DOC["workloads"])))
check("every vector has the table's 73 rows", all(len(w[1]) == 73 for w in WL))
def const(name):
    m = re.search(r"inline constexpr int %s\s*=\s*(-?\d+);" % name, TEXT)
    return int(m.group(1)) if m else None


check("the header carries the counts",
      const("kWorkloadCount") == 146 and const("kWorkloadRowCount") == 73
      and const("kGestureCount") == len(DOC["gestures"])
      and const("kGestureStepTotal") == len(GS))

flat = [(s["block"], s["row"], s["value_x1000"]) for gj in DOC["gestures"] for s in gj["steps"]]
check("round trip: every gesture step, in order", GS == flat)
check("round trip: gesture names, bases, slices and lengths",
      len(GD) == len(DOC["gestures"])
      and all(d[0] == gj["name"] and d[1] == names.index(gj["base"])
              and GS[d[2]:d[2] + d[3]] == [(s["block"], s["row"], s["value_x1000"])
                                            for s in gj["steps"]]
              and d[4] == gj["blocks"] and d[5] == i
              for i, (d, gj) in enumerate(zip(GD, DOC["gestures"]))))
check("kGestureMaxSteps is the longest gesture",
      const("kGestureMaxSteps") == max(len(gj["steps"]) for gj in DOC["gestures"]))
check("rows are stored as int16_t, the tables in .qspi_rodata on the board",
      "int16_t rows_x1000[kWorkloadRowCount]" in TEXT
      and '__attribute__((section(".qspi_rodata")))' in TEXT)


# --- sabotages: each must make build() raise with the named reason ---------
def sabotage(name, mutate, needle):
    inp = copy.deepcopy(BASE)
    mutate(inp)
    label = "sabotage %s goes red (%s)" % (name, needle)
    try:
        g.build(**inp)
    except g.GenError as e:
        check(label, needle in str(e))
        if needle not in str(e):
            print("     got: %s" % e)
        return
    check(label, False)


def wl(inp, i=0):
    return inp["doc"]["workloads"][i]


def gest(inp, i=0):
    return inp["doc"]["gestures"][i]


sabotage("a vector one row short", lambda i: wl(i, 5)["rows_x1000"].pop(), "has 72 rows")
sabotage("a vector one row long", lambda i: wl(i, 5)["rows_x1000"].append(500), "has 74 rows")
sabotage("the table lost a row", lambda i: i["table_rows"].pop(), "table has 72 rows")
sabotage("row_count disagrees", lambda i: i["doc"].update(row_count=72), "row_count")
sabotage("a row name moved", lambda i: i["doc"]["rows"].__setitem__(3, "RATE_B"), "row 3")
sabotage("a value above 1000", lambda i: wl(i)["rows_x1000"].__setitem__(0, 1001), "outside 0..1000")
sabotage("a negative value", lambda i: wl(i)["rows_x1000"].__setitem__(9, -1), "outside 0..1000")
sabotage("a duplicate name", lambda i: wl(i, 1).update(name=wl(i, 0)["name"]), "duplicate")
sabotage("a name too long", lambda i: wl(i).update(name="x" * 40), "longer than")
sabotage("an unknown input", lambda i: wl(i).update(input="pink"), "input")
sabotage("a zero settle", lambda i: wl(i).update(settle_blocks=0), "settle_blocks")
sabotage("excite not a pair", lambda i: wl(i).update(excite=[True]), "excite")
sabotage("rec not bools", lambda i: wl(i).update(rec=[1, 0]), "rec")
sabotage("a gesture row past the table", lambda i: gest(i)["steps"][0].update(row=73), "row 73")
sabotage("a gesture row below -2", lambda i: gest(i)["steps"][0].update(row=-3), "row -3")
sabotage("gesture steps out of order",
         lambda i: gest(i)["steps"][2].update(block=gest(i)["steps"][1]["block"] - 1), "order")
sabotage("a step past the gesture's length",
         lambda i: gest(i).update(blocks=gest(i)["steps"][-1]["block"]), "blocks")
sabotage("an unknown base", lambda i: gest(i).update(base="nope"), "base")
sabotage("a gesture value out of range",
         lambda i: gest(i)["steps"][0].update(value_x1000=1200), "outside 0..1000")
sabotage("a key step with a value", lambda i: gest(i, 8)["steps"][0].update(value_x1000=5), "key step")

print("\n%d failed" % len(FAILS) if FAILS else "\nall ok")
sys.exit(1 if FAILS else 0)
