#!/usr/bin/env python3
"""Guard for shell/gen_panel_map.py (spec 2026-10-02-rev-a-p6a-panel-scan-design.md
section 4). Plain script -- pytest is not installed here -- and its exit code
is the verdict. Every check the spec names (section 4) has a sabotage that
must turn it red; a few purely defensive generator checks have none.
"""
import copy
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import gen_panel_map as g  # noqa: E402

FAILS = []


def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        FAILS.append(name)


BASE = g.load_inputs()
TEXT = g.build(**BASE)
ROWS = [line for line in TEXT.splitlines() if "// row " in line]

# --- live ---------------------------------------------------------------
check("committed header equals the generator's output",
      g.check_text(g.read_committed(), TEXT))
check("70 pot rows", len(ROWS) == 70)
check("35 rows send a parameter", sum("spky::P_" in r for r in ROWS) == 35)
check("every SAFE ParamId appears in the header",
      all("spky::%s," % pid in TEXT for pid, _ in BASE["safe"].values()))
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


def swap_led_with_spare(inp):
    chips = inp["tables"]["sr_outputs"]
    where = {n: (c, k) for c, chip in enumerate(chips) for k, n in enumerate(chip)}
    (a, b), (c, d) = where["LED4"], where["SR_SPARE0"]
    chips[a][b], chips[c][d] = "SR_SPARE0", "LED4"


def swap_d8_d9(inp):
    pins = inp["tables"]["module_pins"]
    pins["D8"], pins["D9"] = pins["D9"], pins["D8"]


def duplicate_channel(inp):
    pots = inp["panel_map"]["pots"]
    pots[1]["mux"], pots[1]["channel"] = pots[0]["mux"], pots[0]["channel"]


sabotage("pot unclassified", lambda i: i["safe"].pop("RATE_A"),
         "unclassified: RATE_A")
sabotage("pot in both lists", lambda i: i["unmapped"].__setitem__("RATE_A", "x"),
         "both SAFE and UNMAPPED: RATE_A")
sabotage("unknown ParamId",
         lambda i: i["safe"].__setitem__("RATE_A", ("P_RATE_Q", "x")),
         "no ParamId P_RATE_Q")
sabotage("pot missing from the VCV HW panel",
         lambda i: i["hw_ids"].discard("RATE_A"),
         "not in generated_hw_panel.hpp: RATE_A")
sabotage("classified name that is no pot",
         lambda i: i["safe"].__setitem__("NOPE_A", ("P_RATE_A", "x")),
         "not a pot: NOPE_A")
sabotage("two rows on one mux input", duplicate_channel, "twice: mux 0 ch 0")
sabotage("pot on the wrong sense pin",
         lambda i: i["panel_map"]["pots"][0].__setitem__("sense", "SENSE_3"),
         "but mux 0 is on SENSE_0")
sabotage("an unaccounted mux input",
         lambda i: i["panel_map"]["spare"].pop(0),
         "79 of 80 mux inputs")
sabotage("LED field not contiguous", swap_led_with_spare,
         "LED0..LED18 not contiguous")
sabotage("sense pins off the ADC run", swap_d8_d9,
         "SENSE_2 on D8 is ADC index 11, expected 10")
sabotage("mux list out of order",
         lambda i: i["panel_map"]["muxes"].__setitem__("SENSE_0", [1, 0, 2]),
         "not ascending")
sabotage("more keys than the firmware holds",
         lambda i: i["tables"]["keys"].append("EXTRA"),
         "keys: 5")
sabotage("calibration channel renamed",
         lambda i: i["panel_map"]["calibration"][0].__setitem__("id", "CAL_X"),
         "calibration")

# --- stale header ---------------------------------------------------------
check("sabotage stale header goes red",
      not g.check_text(TEXT.replace("spky::P_RATE_A", "spky::P_RATE_B", 1), TEXT))

print("%d failed" % len(FAILS) if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
