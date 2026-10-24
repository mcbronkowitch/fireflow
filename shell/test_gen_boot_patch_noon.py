#!/usr/bin/env python3
"""Guard for shell/gen_boot_patch_noon.py (fault R4, docs/hardware/reva-bring-up.md
10.1). Plain script -- pytest is not installed here -- and its exit code is the
verdict. Every refusal the generator makes has a sabotage that must turn it red.
"""
import copy
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import gen_boot_patch_noon as g  # noqa: E402

FAILS = []


def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        FAILS.append(name)


BASE = g.load_inputs()
TEXT = g.build(**BASE)
VALUES = [int(x) for line in TEXT.splitlines() if line.startswith("    ") and "// rows" in line
          for x in line.split("//")[0].replace(",", " ").split()]

# --- live ---------------------------------------------------------------
check("committed header exists and equals the generator's output",
      os.path.exists(g.OUT) and g.check_text(g.read_committed(), TEXT))
check("73 row values", len(VALUES) == 73)
check("every value is an emitted 0..1000", all(0 <= v <= 1000 for v in VALUES))
# The record says every pot sat at noon; the loosest reading of "noon" that
# still rules out a frame from the wrong phase of the capture (the 0-stop
# windows 66-68 print zeros, the init patch prints the -1000 of never emitted).
check("every value is near noon (250..750)", all(250 <= v <= 750 for v in VALUES))
check("the header names its capture and window",
      g.CAPTURE_REL in TEXT and "CBP_WIN w=%d" % g.WINDOW in TEXT)
check("the header carries the row count for the static_assert",
      "kBootPatchNoonRowCount = 73;" in TEXT)
check("a CRLF checkout is not stale",
      g.check_text(TEXT.replace("\n", "\r\n"), TEXT))


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


def _frame_indices(lines, window):
    """Line indices of the frame the generator takes: its SHELL_PLAY line and
    its eight V lines, found the same way the generator finds them."""
    stop = next(i for i, l in enumerate(lines) if l.startswith("CBP_WIN w=%d " % window))
    p = max(i for i in range(stop) if lines[i].startswith("SHELL_PLAY ") and "keys=" in lines[i])
    v = {}
    j = p - 1
    while len(v) < 8:
        if lines[j].startswith("SHELL_PLAY_V r="):
            v[int(lines[j].split("r=")[1].split()[0])] = j
        j -= 1
    return p, v


def drop_window(inp):
    inp["lines"] = [l for l in inp["lines"] if not l.startswith("CBP_WIN w=%d " % inp["window"])]


def never_emitted(inp):
    _, v = _frame_indices(inp["lines"], inp["window"])
    parts = inp["lines"][v[70]].split()
    parts[-1] = "-1000"
    inp["lines"][v[70]] = " ".join(parts)


def short_row_line(inp):
    _, v = _frame_indices(inp["lines"], inp["window"])
    inp["lines"][v[20]] = inp["lines"][v[20]].rsplit(" ", 1)[0]


def mod_latched(inp):
    p, _ = _frame_indices(inp["lines"], inp["window"])
    inp["lines"][p] = inp["lines"][p].replace("presses=0,0,0,0", "presses=0,0,1,0")


def key_held(inp):
    p, _ = _frame_indices(inp["lines"], inp["window"])
    inp["lines"][p] = inp["lines"][p].replace("keys=0 ", "keys=4 ")


def out_of_range(inp):
    _, v = _frame_indices(inp["lines"], inp["window"])
    parts = inp["lines"][v[0]].split()
    parts[2] = "1001"
    inp["lines"][v[0]] = " ".join(parts)


def no_frame(inp):
    stop = next(i for i, l in enumerate(inp["lines"]) if l.startswith("CBP_WIN w=%d " % inp["window"]))
    inp["lines"] = [l for i, l in enumerate(inp["lines"]) if i >= stop or not l.startswith("SHELL_PLAY_V")]


sabotage("the window missing from the capture", drop_window, "no 'CBP_WIN w=43' line")
sabotage("a row never emitted", never_emitted, "row 72 never emitted (-1000)")
sabotage("a V line short one value", short_row_line, "r=20 has 9 values, expected 10")
sabotage("MOD latched in the frame", mod_latched, "MOD latched in the frame")
sabotage("a key held in the frame", key_held, "a key is held in the frame")
sabotage("a value outside 0..1000", out_of_range, "row 0 value 1001 outside 0..1000")
sabotage("no frame before the window", no_frame, "no complete SHELL_PLAY frame")

# --- stale header ---------------------------------------------------------
check("sabotage stale header goes red",
      not g.check_text(TEXT.replace(" 531,", " 530,", 1), TEXT))

print("%d failed" % len(FAILS) if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
