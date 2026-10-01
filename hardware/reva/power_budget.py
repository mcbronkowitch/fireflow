#!/usr/bin/env python3
"""U_REG's power budget and junction temperature (docs/hardware/power-budget.md).

    KIPY hardware/reva/power_budget.py [BOARD] [--before BOARD]

Report only, never a gate: it reads a routed board (default the committed
kicad/reva.kicad_pcb), counts the 3V3D loads on it, measures the 3V3D B.Cu
copper that touches U_REG's tab, and prints current, dissipation, thermal
resistance and junction temperature. Every number in power-budget.md is a
line this script printed; re-run it after any change to the board or to an
assumption below, and copy the new lines into the doc.

Every input is tagged: [board] measured on the board by this script,
[datasheet] quoted from a named document, [assumed] a choice made here."""
import argparse
import math
import os
import re
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
COMMITTED = os.path.join(HERE, "kicad", "reva.kicad_pcb")

REG_REF, NET = "U_REG", "3V3D"
V_OUT = 3.3                 # [datasheet] AMS1117-3.3 nominal output (Slkor)
V_IN = {"typ": 12.0, "worst": 12.6}     # [assumed] +12 V bus, nominal / upper tolerance; D_P12's drop ignored
# typ 5 mA: [listing] LCSC C6186 (Slkor's AMS1117 text says 3 mA typ);
# worst 10 mA: [datasheet] Slkor AMS1117 max at Vin = Vout + 1.25 V, in a
# table headed "Vin <= 7 V, Tj = 25 C unless otherwise specified", so its use
# at 12 V is [assumed]
IQ_MA = {"typ": 5.0, "worst": 10.0}
IQ_ALT_MA = 15.0            # [datasheet] TI LM1117I-3.3 max over -40..125 C, Vin <= 15 V: the sensitivity case
THETA_DS = 150.0            # [datasheet] Slkor AMS1117 SOT-223 θJA (absolute maximum table), copper area not stated
PD_MAX_W = 0.600            # [datasheet] Slkor AMS1117 maximum power dissipation, SOT-223
THETA_JEDEC = 61.6          # [datasheet] TI LM1117 §7.4 Thermal Information, RθJA DCY (SOT-223): an optimistic bound
LED_VF = {"typ": 2.0, "worst": 1.8}     # [assumed] the panel LEDs' colour and Vf are not chosen yet
LED_ON = {"typ": 0.5, "worst": 1.0}     # [assumed] fraction of LEDs lit (firmware duty is not written yet)
VF_SWEEP = (1.8, 2.0, 3.0)  # [assumed] Vf values to show the colour's effect (red-ish ... blue/white-ish)
KEYS_DOWN = {"typ": 0, "worst": None}   # [assumed] None: every key pressed at once
HC_ICC_MA = {"typ": 0.0, "worst": 0.08}  # [assumed] 74HC static supply current per package, order of magnitude
TA_C = (40.0, 50.0)         # [assumed] air inside the case
TJ_LIMIT_C = 125.0          # [datasheet] Slkor AMS1117 operating junction range -40..125 C ([listing] LCSC agrees)
# [datasheet] TI LM1117 (SNOS412Q, Jan 2023) Table 9-2: SOT-223 RθJA against
# top-side copper area, "tab of device attached to topside copper" (TI's
# top side is the component side: B.Cu for U_REG here). 1 oz and still air
# are from Figure 9-11, which plots the same data ("RθJA vs 1-oz Copper Area
# for SOT-223", legend "TA = 25°C, Still Air"). A proxy: TI's test board,
# not this board, and TI's die, not AMS's.
TI_TABLE = ((0.0123, 136.0), (0.066, 123.0), (0.3, 84.0), (0.53, 75.0), (0.76, 69.0), (1.0, 66.0))
MM2_PER_IN2 = 645.16
NEAR_MM = 10.0              # the "within 10 mm of the tab" figure, reported beside the total


def mm(v):
    return pcbnew.ToMM(v)


def mm2(a):
    return a / 1e12


def theta(area_mm2):
    """RθJA from TI_TABLE, linear in area between its rows, clamped at its
    ends (below 0.0123 in² it answers the first row)."""
    a = area_mm2 / MM2_PER_IN2
    if a <= TI_TABLE[0][0]:
        return TI_TABLE[0][1]
    for (a0, t0), (a1, t1) in zip(TI_TABLE, TI_TABLE[1:]):
        if a <= a1:
            return t0 + (t1 - t0) * (a - a0) / (a1 - a0)
    return TI_TABLE[-1][1]


def tab_pad(board):
    fp = board.FindFootprintByReference(REG_REF)
    pads = [p for p in fp.Pads() if p.GetNetname() == NET and p.IsOnLayer(pcbnew.B_Cu)]
    return max(pads, key=lambda p: p.GetEffectivePolygon(pcbnew.B_Cu, pcbnew.ERROR_INSIDE).Area())


def tab_copper(board):
    """(area mm², area within NEAR_MM of the tab centre mm²): the outline of
    the union of all NET copper on B.Cu (pads, tracks, zone fills) that
    overlaps U_REG's tab."""
    lid = pcbnew.B_Cu
    err = pcbnew.FromMM(0.005)
    u = pcbnew.SHAPE_POLY_SET()
    for f in board.GetFootprints():
        for p in f.Pads():
            if p.GetNetname() == NET and p.IsOnLayer(lid):
                p.TransformShapeToPolygon(u, lid, 0, err, pcbnew.ERROR_INSIDE)
    for t in board.GetTracks():
        if t.GetNetname() == NET and t.IsOnLayer(lid):
            t.TransformShapeToPolygon(u, lid, 0, err, pcbnew.ERROR_INSIDE)
    for z in board.Zones():
        if not z.GetIsRuleArea() and z.GetNetname() == NET and z.IsOnLayer(lid):
            u.BooleanAdd(z.GetFilledPolysList(lid))
    u.Simplify()
    tab = tab_pad(board)
    tp = tab.GetEffectivePolygon(lid, pcbnew.ERROR_INSIDE)
    c = tab.GetPosition()
    disk = pcbnew.SHAPE_POLY_SET()
    disk.NewOutline()
    for i in range(360):
        a = math.radians(i)
        disk.Append(c.x + int(pcbnew.FromMM(NEAR_MM) * math.cos(a)), c.y + int(pcbnew.FromMM(NEAR_MM) * math.sin(a)))
    total = near = 0.0
    for i in range(u.OutlineCount()):
        one = pcbnew.SHAPE_POLY_SET()
        one.AddOutline(u.COutline(i))
        for j in range(u.HoleCount(i)):
            one.AddHole(u.CHole(i, j))
        x = pcbnew.SHAPE_POLY_SET(one)
        x.BooleanIntersection(tp)
        if x.Area() <= 0:
            continue
        total += mm2(one.Area())
        n = pcbnew.SHAPE_POLY_SET(one)
        n.BooleanIntersection(disk)
        near += mm2(n.Area())
    return total, near, mm2(tp.Area())


def inventory(board):
    """The NET loads by kind, read from the board."""
    parts = {}
    for f in board.GetFootprints():
        if any(p.GetNetname() == NET for p in f.Pads()):
            parts[f.GetReference()] = f
    led_r = {}       # LEDn resistor ref -> ohms
    leds = []
    for f in board.GetFootprints():
        nets = {p.GetNetname() for p in f.Pads()}
        if f.GetReference().startswith("R") and any(re.fullmatch(r"LED\d+", n) for n in nets) \
                and any(re.fullmatch(r"LED\d+_A", n) for n in nets):
            led_r[f.GetReference()] = f.GetValue()
        if f.GetReference().startswith("D") and any(re.fullmatch(r"LED\d+_A", n) for n in nets) and "GND" in nets:
            leds.append(f.GetReference())
    hc = sorted(r for r, f in parts.items() if f.GetValue() in ("74HC595", "74HC165"))
    pullups = sorted(r for r, f in parts.items() if r.startswith("R") and f.GetValue() == "10k")
    rest = sorted(r for r in parts if r not in hc and r not in pullups and r != REG_REF)
    n_pads = sum(1 for f in parts.values() for p in f.Pads() if p.GetNetname() == NET)
    leds.sort(key=lambda r: int(re.sub(r"\D", "", r) or 0))
    return {"parts": parts, "hc": hc, "pullups": pullups, "rest": rest, "leds": leds,
            "led_r": led_r, "n_pads": n_pads}


def ohms(v):
    m = re.fullmatch(r"([\d.]+)\s*([kKmM]?)", v.strip())
    return float(m.group(1)) * {"": 1.0, "k": 1e3, "K": 1e3, "m": 1e6, "M": 1e6}[m.group(2)]


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("board", nargs="?", default=COMMITTED)
    ap.add_argument("--before", default="", help="a board to measure the tab copper of, for comparison")
    a = ap.parse_args(argv)
    b = pcbnew.LoadBoard(a.board)
    inv = inventory(b)
    print("board: %s" % os.path.basename(a.board))
    print("[board] %s loads: %d pads on %d parts (U_REG included)" % (NET, inv["n_pads"], len(inv["parts"])))
    print("[board] 74HC packages on %s: %s" % (NET, ", ".join("%s %s" % (r, inv["parts"][r].GetValue())
                                                            for r in inv["hc"])))
    print("[board] key pull-ups %s -> key -> GND: %s (%s)" % (NET, ", ".join(inv["pullups"]),
                                                             ", ".join(sorted({inv["parts"][r].GetValue()
                                                                               for r in inv["pullups"]}))))
    print("[board] no DC load: %s" % ", ".join(inv["rest"]))
    rvals = sorted(set(inv["led_r"].values()))
    print("[board] LEDs on 74HC595 outputs: %d (%s), series resistors %d, values %s"
          % (len(inv["leds"]), "-".join([inv["leds"][0], inv["leds"][-1]]) if inv["leds"] else "none",
             len(inv["led_r"]), rvals))
    r_led = ohms(rvals[0]) if len(rvals) == 1 else None
    n_keys = len(inv["pullups"])
    r_key = ohms(inv["parts"][inv["pullups"][0]].GetValue())
    loads = {}
    for case in ("typ", "worst"):
        n_on = int(math.ceil(len(inv["leds"]) * LED_ON[case]))
        i_led = n_on * (V_OUT - LED_VF[case]) / r_led * 1e3
        keys = n_keys if KEYS_DOWN[case] is None else KEYS_DOWN[case]
        i_key = keys * V_OUT / r_key * 1e3
        i_hc = len(inv["hc"]) * HC_ICC_MA[case]
        i_load = i_led + i_key + i_hc
        # rounded to what is printed, so every Tj below recomputes by hand
        p = round((V_IN[case] - V_OUT) * i_load / 1e3 + V_IN[case] * IQ_MA[case] / 1e3, 3)
        loads[case] = p
        print("%-5s LEDs %d of %d lit at Vf %.1f V: %.2f mA; keys down %d x %.2f mA: %.2f mA; 74HC %d x %.2f mA: "
              "%.2f mA; I_load %.2f mA" % (case, n_on, len(inv["leds"]), LED_VF[case],
                                           i_led, keys, V_OUT / r_key * 1e3, i_key, len(inv["hc"]),
                                           HC_ICC_MA[case], i_hc, i_load))
        print("%-5s Vin %.1f V, Iq %.1f mA: P = (Vin - Vout) x I_load + Vin x Iq = %.3f W"
              % (case, V_IN[case], IQ_MA[case], p))
        print("%-5s of it Vin x Iq: %.3f W (%.0f %%)" % (case, V_IN[case] * IQ_MA[case] / 1e3,
                                                       100.0 * V_IN[case] * IQ_MA[case] / 1e3 / p))
    for vf in VF_SWEEP:
        print("one LED at Vf %.1f V through %s: %.2f mA" % (vf, rvals[0], (V_OUT - vf) / r_led * 1e3))
    p_alt = round(loads["worst"] + V_IN["worst"] * (IQ_ALT_MA - IQ_MA["worst"]) / 1e3, 3)
    print("worst with Iq %.0f mA: P = %.3f W" % (IQ_ALT_MA, p_alt))
    print("Slkor SOT-223 maximum power dissipation %.3f W: worst %.3f W, with Iq %.0f mA %.3f W"
          % (PD_MAX_W, loads["worst"], IQ_ALT_MA, p_alt))
    for th_ds, what in ((THETA_DS, "Slkor θJA, copper unstated"), (THETA_JEDEC, "TI §7.4 RθJA, optimistic bound")):
        for ta in TA_C:
            tw = ta + th_ds * loads["worst"]
            print("datasheet θJA %.1f C/W (%s), Ta %.0f C: Tj typ %.1f C, worst %.1f C (margin %.1f K)"
                  % (th_ds, what, ta, ta + th_ds * loads["typ"], tw, TJ_LIMIT_C - tw))
    boards = [("after", b)]
    if a.before:
        boards.insert(0, ("before", pcbnew.LoadBoard(a.before)))
    for label, bb in boards:
        total, near, tab = tab_copper(bb)
        th = round(theta(total), 1)     # rounded to what is printed
        print("%-6s %s B.Cu copper touching the tab: %.1f mm2 (%.3f in2; %.1f mm2 within %.0f mm of the tab "
              "centre; the tab pad alone %.2f mm2) -> RthJA %.1f C/W (TI Table 9-2, interpolated)"
              % (label, NET, total, total / MM2_PER_IN2, near, NEAR_MM, tab, th))
        for ta in TA_C:
            for case in ("typ", "worst"):
                tj = ta + th * loads[case]
                print("%-6s Ta %.0f C %-5s P %.3f W: Tj %.1f C (limit %.0f C, margin %.1f K)"
                      % (label, ta, case, loads[case], tj, TJ_LIMIT_C, TJ_LIMIT_C - tj))
            tj = ta + th * p_alt
            print("%-6s Ta %.0f C worst, Iq %.0f mA, P %.3f W: Tj %.1f C (+%.1f K)"
                  % (label, ta, IQ_ALT_MA, p_alt, tj, th * (p_alt - loads["worst"])))
        print("%-6s worst case reaches %.0f C at Ta %.1f C" % (label, TJ_LIMIT_C, TJ_LIMIT_C - th * loads["worst"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
