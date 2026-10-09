#!/usr/bin/env python3
"""Guard for shell/gen_panel_map.py (spec 2026-10-02-rev-a-p6a-panel-scan-design.md
section 4; classification spec 2026-10-09-rev-a-p6b1-shared-control-law-design.md
sections 4.2 and 7.2). Plain script -- pytest is not installed here -- and its exit code
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
check("73 pot rows", len(ROWS) == 73)
check("70 rows send a parameter", sum("ffctl::" in r for r in ROWS) == 70)
# P6b-1 spec 7.2: every row that is not reserved sends the id of the FireflowHW
# control at its position -- the pot's own name, which the VCV HW panel places.
SENDING = [r for r in ROWS if "ffctl::" in r.split("//")[0]]
check("every sending row sends its own pot's id, a FireflowHW control",
      all(r.split("//")[0].split("ffctl::")[1].split(",")[0]
          == r.split("//")[1].split()[2] and
          r.split("//")[1].split()[2] in BASE["hw_ids"] for r in SENDING))
# A reserved pot is scanned like any other and sends nothing (spec 2026-10-07
# section 4): target -1 in the code part of the row, reason "reserved:" after it.
check("3 reserved rows send nothing",
      sum("reserved:" in r.split("//")[1] and ", -1, " in r.split("//")[0]
          for r in ROWS) == 3)
check("the reserved rows are ROOT_A, ROOT_B and REV_MOD",
      sorted(r.split("//")[1].split()[2] for r in ROWS
             if "reserved:" in r.split("//")[1]) == ["REV_MOD", "ROOT_A", "ROOT_B"])
check("no row sends an engine ParamId any more", "spky::" not in TEXT)
# Findings r1 item 1: the ATTACK pots carry STAGES as their BBD alternate, and
# nothing else carries an alternate (a row's code part has five fields then).
ALTS = sorted((r.split("//")[1].split()[2], r.split("//")[0].split("ffctl::")[2].split("}")[0])
              for r in ROWS if r.split("//")[0].count("ffctl::") == 2)
check("exactly the two ATTACK rows carry STAGES as their alternate",
      ALTS == [("ATTACK_A", "STAGES_A"), ("ATTACK_B", "STAGES_B")])
check("the header validates every id at compile time",
      "static_assert(entries_valid(kRevaControls," in TEXT)
check("no stale Fireflow.cpp line citation", "Fireflow.cpp" not in TEXT)
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


sabotage("pot with no ffctl id",
         lambda i: i["params"].discard("RATE_A"), "RATE_A: no ffctl id")
sabotage("reserved pot given an id",
         lambda i: i["reserved"].pop("ROOT_A"), "ROOT_A: no ffctl id")
sabotage("alternate with no ffctl id",
         lambda i: i["params"].discard("STAGES_A"),
         "ATTACK_A: alternate STAGES_A has no ffctl id")


def second_id(name, extra):
    def mutate(inp):
        pot = next(p for p in inp["panel_map"]["pots"] if p["id"] == name)
        pot["ids"] = [name, extra]
    return mutate


sabotage("a second id on a pot the firmware has no rule for",
         second_id("RATE_A", "SHAPE_A"), "RATE_A: shares its position with SHAPE_A")
sabotage("the other deck's STAGES as an alternate",
         second_id("ATTACK_A", "STAGES_B"), "ATTACK_A: shares its position with STAGES_B")
sabotage("reserved pot dropped from the list",
         lambda i: i["reserved"].pop("ROOT_B"), "ROOT_B: no ffctl id")
sabotage("reserved name that is no pot",
         lambda i: i["reserved"].__setitem__("NOPE", "x"), "not a pot: NOPE")
sabotage("pot missing from the VCV HW panel",
         lambda i: i["hw_ids"].discard("RATE_A"),
         "not in generated_hw_panel.hpp: RATE_A")
sabotage("two rows on one mux input", duplicate_channel, "twice: mux 0 ch 0")
sabotage("pot on the wrong sense pin",
         lambda i: i["panel_map"]["pots"][0].__setitem__("sense", "SENSE_3"),
         "but mux 0 is on SENSE_0")
sabotage("an unaccounted mux input",
         lambda i: i["panel_map"]["spare"].pop(0),
         "79 of 80 mux inputs")
sabotage("LED field not contiguous", swap_led_with_spare,
         "LED0..LED14 not contiguous")
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
      not g.check_text(TEXT.replace("ffctl::RATE_A", "ffctl::RATE_B", 1), TEXT))

print("%d failed" % len(FAILS) if FAILS else "all passed")
sys.exit(1 if FAILS else 0)
