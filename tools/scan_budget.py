#!/usr/bin/env python3
"""Scan budget for the FireFlow panel -- ARITHMETIC on MEASURED constants.

settle_budget.py asked how long one mux step takes to settle and predicted it
from a model.  The coupon has since measured it (settle-measured.md,
pots-measured.md), measured what a long idle before a conversion does
(wait-measured.md), and measured libDaisy's own free-running ADC pattern at
five rungs (settle-measured.md section 7).  This file puts those numbers
against the audio block and the CPU reserve, for every placement the panel
scan could take.  Nothing here was run on a board; every input was.

The write-up, and which input is measured, documented or derived, is
docs/hardware/scan-budget.md.

Run:    python tools/scan_budget.py
Guard:  python tools/test_scan_budget.py   (ctest: scan_budget_guard)
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import settle_budget as sb

_HW_RES = os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "host", "vcv", "res")

# --- Carried over from settle_budget.py (one source for each) ----------------
ADC_CLK_HZ = sb.ADC_CLK_HZ                # MEASURED 2026-09-17, 6.146 MHz
CONVERSION_CYCLES = sb.CONVERSION_CYCLES  # 8.5, 16-bit
SAMPLING_CYCLES = sb.SAMPLING_CYCLES
BLOCK_S = sb.BLOCK_SECONDS                # 96 @ 48 kHz = 2 ms
N_SENSE_PINS = sb.N_SENSE_PINS            # A2, A3, D8, D9
CHIPS = sb.CHIPS

# --- Settle after an address step: MEASURED on the coupon ---------------------
# Largest true settle seen, pots at mid travel (pots-measured.md section 11).
# Every row below charges the worst of the three, whichever chip it costs.
SETTLE_CEILING_S = {
    ("74HC4067 (16:1)", 10e3): 2.8e-6,
    ("74HC4067 (16:1)", 20e3): 4.8e-6,
    ("74HC4051 (8:1)", 10e3): 2.0e-6,
}
SETTLE_S = max(SETTLE_CEILING_S.values())

# --- libDaisy's ADC as the shell runs it: READ from the source ----------------
# DaisyPatchSM::Init() puts all twelve ADC pins in one regular sequence
# (daisy_patch_sm.cpp:301-322) and AdcHandle::Init() defaults to OVS_32
# (adc.h:118), free-running into a circular DMA buffer (adc.cpp:240-246).
# shell/main.cpp re-initialises the same twelve at SPEED_16CYCLES_5.
# Oversampling runs all 32 conversions of one channel before the next channel:
# DOCUMENTED, stm32h7xx_hal_adc.h:848, "all conversions of oversampling ratio
# are done from 1 trigger" -- not measured on this board, and section 3's
# verdict rests on it.
LIBDAISY_CHANNELS = 12
LIBDAISY_OVS = 32
SHELL_RUNG = 16.5
LONG_RUNG = 387.5

# --- CPU: MEASURED on the bench ----------------------------------------------
# instrument_worst_bbd_dtcm on the Patch Submodule: 97.02-97.16 % pct_max,
# i.e. ~2.9 points of reserve (docs/bench/2026-08-19-...-usb.md).  One mux
# step per block in the callback -- 32 chain bits and four reads of the DMA
# buffer -- costs less than ~0.2 points, below the measurement's resolution
# (docs/bench/2026-08-23-978cbaf-shell-mux-placement.md).
CPU_RESERVE_POINTS = 2.9
STEP_BOOKKEEPING_POINTS = 0.2


def conversion_s(rung):
    """One conversion: sampling window plus 8.5 cycles of conversion."""
    return (rung + CONVERSION_CYCLES) / ADC_CLK_HZ


def group_s(rung, ovs):
    """One channel's oversampling group: `ovs` conversions back to back."""
    return ovs * conversion_s(rung)


def rotation_s(rung, ovs, channels=LIBDAISY_CHANNELS):
    """One pass of the free-running sequence over every channel."""
    return channels * group_s(rung, ovs)


def free_running(rung, ovs, steps_per_block=1, settle_s=SETTLE_S,
                 channels=LIBDAISY_CHANNELS):
    """Does a free-running sequence deliver a clean value per address step?

    The address is written at the start of a step's slot and the DMA buffer
    is read at the start of the next.  The value read is the last group that
    COMPLETED; groups of one channel complete one rotation apart.  So the
    newest completion is at most one rotation old, and it started one group
    before that: it is clean only if rotation + group fits between the
    address settling and the read.
    """
    window = BLOCK_S / steps_per_block - settle_s
    rot = rotation_s(rung, ovs, channels)
    grp = group_s(rung, ovs)
    return dict(rotation_s=rot, group_s=grp, window_s=window,
                slack_s=window - (rot + grp), fits=rot + grp <= window)


def blocking_cost_s(rung, ovs):
    """Callback time for one step converted in place: settle, then the four
    sense pins one after another, waited for."""
    return SETTLE_S + N_SENSE_PINS * group_s(rung, ovs)


def reserve_s():
    return CPU_RESERVE_POINTS / 100.0 * BLOCK_S


def points(seconds):
    """Callback time as points of one audio block."""
    return seconds / BLOCK_S * 100.0


def pot_positions():
    """Physical pot positions on the hardware plate -- one mux channel each.

    Read from host/vcv/res/gen_hw_panel.py, the authority (importing it
    writes nothing).  Size classes G and S are pots, P are keycaps on the 165
    chain; STAGES shares ATTACK's knob, so positions are counted, not params.
    """
    sys.path.insert(0, _HW_RES)
    import gen_hw_panel as g
    return len({(round(c.x, 3), round(c.y, 3)) for c in g.HW_PARAMS
                if g.hw_class(c.enum) in ("G", "S")})


def topology(chip, n_channels):
    """settle_budget.topology() with the channel count as an argument.

    Address lines are common to every mux, so a full sweep is as long as the
    busiest sense pin's chips times their ways."""
    chips = math.ceil(n_channels / chip["ways"])
    on_busiest_pin = math.ceil(chips / N_SENSE_PINS)
    return dict(chips=chips, on_busiest_pin=on_busiest_pin,
                steps=on_busiest_pin * chip["ways"])


def sweep(steps, steps_per_block):
    blocks = math.ceil(steps / steps_per_block)
    seconds = blocks * BLOCK_S
    return dict(blocks=blocks, seconds=seconds, hz=1.0 / seconds)


# The placements section 4 compares.  `accuracy` is what the coupon measured
# for that pattern at pot impedance (5150 ohm = a 20 k pot at mid travel),
# or says that nothing did.
PLACEMENTS = (
    dict(key="F1", kind="free", rung=SHELL_RUNG, ovs=32,
         accuracy="measured: 31736 vs 31738 at 387.5 (settle-measured s7)"),
    dict(key="F2", kind="free", rung=LONG_RUNG, ovs=32,
         accuracy="measured: 31738 (settle-measured s7)"),
    dict(key="F3", kind="free", rung=LONG_RUNG, ovs=1,
         accuracy="unmeasured in this pattern"),
    dict(key="B1", kind="block", rung=SHELL_RUNG, ovs=1,
         accuracy="unmeasured; at 2.5 cyc it read 440-650 low (wait, pots)"),
    dict(key="B2", kind="block", rung=LONG_RUNG, ovs=1,
         accuracy="measured: flat at every wait (wait s5)"),
    dict(key="B3", kind="block", rung=SHELL_RUNG, ovs=32,
         accuracy="unmeasured"),
)


def main():
    n = pot_positions()
    print("ADC conv clock   %6.3f MHz (measured)   block %4.0f us   "
          "settle ceiling %.1f us (measured)"
          % (ADC_CLK_HZ / 1e6, BLOCK_S * 1e6, SETTLE_S * 1e6))
    print("conversion       16.5 cyc %5.2f us   387.5 cyc %5.2f us"
          % (conversion_s(16.5) * 1e6, conversion_s(387.5) * 1e6))
    print("CPU reserve      %.1f points = %.0f us per block\n"
          % (CPU_RESERVE_POINTS, reserve_s() * 1e6))

    print("=== placements, one step per block ===")
    print("  %-3s %-6s %6s %4s | %9s %9s %9s | %s"
          % ("", "kind", "rung", "ovs", "rotation", "slack", "callback",
             "accuracy at 5150 ohm"))
    for p in PLACEMENTS:
        if p["kind"] == "free":
            f = free_running(p["rung"], p["ovs"])
            rot = "%7.0fus" % (f["rotation_s"] * 1e6)
            slack = ("%7.0fus" % (f["slack_s"] * 1e6)) if f["fits"] else \
                "  no fit"
            cb = "<%.1fpt" % STEP_BOOKKEEPING_POINTS
        else:
            cost = blocking_cost_s(p["rung"], p["ovs"])
            rot, slack = "        -", "        -"
            cb = "%.1fpt%s" % (points(cost), "" if cost <= reserve_s() else "!")
        print("  %-3s %-6s %6.1f %4d | %9s %9s %9s | %s"
              % (p["key"], p["kind"], p["rung"], p["ovs"], rot, slack, cb,
                 p["accuracy"]))

    print("\n=== longest rung the free-running pattern fits, per oversampling "
          "===")
    for ovs in (1, 4, 8, 16, 32):
        for spb in (1, 2):
            fit = [r for r in SAMPLING_CYCLES
                   if free_running(r, ovs, steps_per_block=spb)["fits"]]
            print("  OVS_%-3d %d step(s)/block: %s"
                  % (ovs, spb, ("%.1f cycles" % fit[-1]) if fit else "none"))

    print("\n=== the panel: %d pot positions (gen_hw_panel.py) ===" % n)
    for name, chip in CHIPS.items():
        t = topology(chip, n)
        for spb in (1, 2):
            sw = sweep(t["steps"], spb)
            print("  %-16s %d chips, %d steps | %d/block: sweep %4.0f ms, "
                  "%5.1f Hz per channel"
                  % (name, t["chips"], t["steps"], spb, sw["seconds"] * 1e3,
                     sw["hz"]))


if __name__ == "__main__":
    main()
