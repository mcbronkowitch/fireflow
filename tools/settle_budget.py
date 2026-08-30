#!/usr/bin/env python3
"""Settle-time budget for the FireFlow control scan -- CALCULATED, not measured.

This is the paper half of Phase-0 Task 6 step 5b.  It does not close 5b; it
turns 5b from a bisection search into a yes/no confirmation with a predicted
number.  Everything here is a single-pole RC model.  What it cannot see is
listed in docs/hardware/settle-budget.md under "What the model cannot see".

Three terms have to fit before a multiplexed channel's value is trustworthy:

  A  node settle     after the address changes, the COM node slews from the old
                     channel's voltage to the new one through the pot's wiper
                     impedance + the mux R_on into C_COM + stray (+ any cap
                     fitted at COM).
  B  acquisition     ST's rule: the ADC's sample-and-hold cap must charge
                     through the source impedance inside the sampling window.
  C  redistribution  the S&H cap still holds the PREVIOUS channel's charge; on
                     connection it dumps that onto the node, which then has to
                     recover through the wiper impedance.  This is why
                     libDaisy's own mux path keeps a "buffer for trash data
                     during mux pin changes" (adc.cpp).

B and C both happen inside the sampling window, so the binding one is max(B,C);
A happens before it and is paid once per address step.

Run from tools/:  python settle_budget.py
Guard:            python test_settle_budget.py
"""
import math

# --- ADC timing, derived from the libDaisy sources in this repo ---------------
# lib/libDaisy/src/sys/system.cpp:494-499 (PLL3) and :515 (ADC clock source),
# lib/libDaisy/src/per/adc.cpp:229 (prescaler), adc.h:45-59 (speeds, default).
HSE_HZ = 16e6                                   # Daisy: 16 MHz crystal
PLL3_M, PLL3_N, PLL3_R = 6, 295, 32
ADC_KERNEL_HZ = HSE_HZ / PLL3_M * PLL3_N / PLL3_R   # RCC_ADCCLKSOURCE_PLL3
ADC_CLK_HZ = ADC_KERNEL_HZ / 2                      # ADC_CLOCK_ASYNC_DIV2
CONVERSION_CYCLES = 8.5              # STM32H7 RM: 16-bit conversion
SAMPLING_CYCLES = [1.5, 2.5, 8.5, 16.5, 32.5, 64.5, 387.5, 810.5]
LIBDAISY_DEFAULT_CYCLES = 8.5        # AdcChannelConfig::SPEED_8CYCLES_5

# --- Multiplexers ------------------------------------------------------------
# CD74HC4067 (TI/Harris SCHS209), read off the datasheet table verbatim:
#   "Common Capacitance, C COM - 50 pF", "Capacitance, C S - 5 pF",
#   R_ON typ 70 ohm @ VCC = 4.5 V, max 160 ohm @ 25 C, 200 @ 85 C.
# CD74HC4051: C_COM = 25 pF -- half the channels hang on COM, half the load.
# NEITHER part is characterised at 3.3 V; the datasheets stop at 4.5 V and 6 V.
CHIPS = {
    "74HC4051 (8:1)": dict(c_com=25e-12, ways=8),
    "74HC4067 (16:1)": dict(c_com=50e-12, ways=16),
}

# R_on at 3.3 V is an extrapolation, and deliberately pessimistic: HC R_on rises
# as VCC falls, and 150 is roughly twice the 4.5 V typical.  It barely matters --
# at a 10k pot the wiper alone contributes 2.5k, so even 500 ohm would move
# term A by 13 %.  R_on is not a risk factor in this design.
R_ON_OHM = 150.0

C_STRAY_F = 15e-12      # ESTIMATE, not measured: MCU pin + PCB trace
C_ADC_F = 4e-12         # ST: STM32H7 internal sample-and-hold capacitor
R_ADC_OHM = 2000.0      # ST community figure for slow channels; NOT verbatim.
                        # Enters term B only, which is never the binding term.

# --- The instrument ----------------------------------------------------------
N_SENSE_PINS = 4        # A2, A3, D8, D9 -- the raw ADC pins, io-budget section 3
N_CHANNELS = 65         # io-budget section 3
BLOCK_SECONDS = 96 / 48000.0
TARGET_BITS = 12        # what the panel actually needs; 16 is the ADC's width


def _time_constants(bits):
    """Time constants needed to settle within half an LSB of `bits`."""
    return math.log(1.0 / (0.5 / (1 << bits)))


def terms(pot_ohm, chip, c_ext_f=0.0, bits=TARGET_BITS):
    """The three settling terms, in seconds, for one channel step."""
    # A linear pot at mid travel is the worst wiper source impedance: R/4.
    r = pot_ohm / 4.0 + R_ON_OHM
    c = chip["c_com"] + C_STRAY_F + c_ext_f
    n = _time_constants(bits)
    dip = C_ADC_F / (C_ADC_F + c)   # worst case: previous channel at other rail
    return dict(
        r=r,
        c=c,
        a=r * c * n,
        b=(R_ADC_OHM + r) * C_ADC_F * n,
        c_redist=r * (c + C_ADC_F) * math.log(dip * (1 << bits) * 2),
    )


def pick_sampling_cycles(t_needed_s):
    """Smallest libDaisy sampling window that covers `t_needed_s`, or None."""
    for cycles in SAMPLING_CYCLES:
        if cycles / ADC_CLK_HZ >= t_needed_s:
            return cycles
    return None


def topology(chip):
    """How many chips, and how many address steps the busiest sense pin costs.

    Address lines are shared across all muxes (they ride the 595 chain), so a
    full sweep is as long as the most heavily loaded sense pin.
    """
    chips = math.ceil(N_CHANNELS / chip["ways"])
    on_busiest_pin = math.ceil(chips / N_SENSE_PINS)
    return dict(chips=chips,
                on_busiest_pin=on_busiest_pin,
                steps=on_busiest_pin * chip["ways"])


def sweep(chip, pot_ohm, c_ext_f=0.0, bits=TARGET_BITS):
    """Full-sweep cost for one configuration.

    `sampling_cycles` is None when no libDaisy sampling window is long enough;
    the configuration is then simply not buildable as libDaisy is written.
    """
    t = terms(pot_ohm, chip, c_ext_f, bits)
    top = topology(chip)
    cycles = pick_sampling_cycles(max(t["b"], t["c_redist"]))
    if cycles is None:
        return dict(top, sampling_cycles=None, step_s=None, sweep_s=None,
                    blocks=None)
    # One ADC serves all four sense pins, so the conversions are sequential;
    # the address settle is paid once because the addresses are common.
    conv = (cycles + CONVERSION_CYCLES) / ADC_CLK_HZ
    step_s = t["a"] + N_SENSE_PINS * conv
    sweep_s = top["steps"] * step_s
    return dict(top, sampling_cycles=cycles, step_s=step_s, sweep_s=sweep_s,
                blocks=sweep_s / BLOCK_SECONDS)


POTS = (10e3, 20e3, 50e3, 100e3)
COM_CAPS = ((0.0, "none"), (100e-12, "100pF"), (1e-9, "1nF"))


def main():
    print("ADC kernel clock  %7.2f MHz  (PLL3R, system.cpp)"
          % (ADC_KERNEL_HZ / 1e6))
    print("ADC clock         %7.2f MHz  (/2, adc.cpp:229)" % (ADC_CLK_HZ / 1e6))
    print("libDaisy default  %7.0f ns   (%.1f cycles, adc.h)"
          % (LIBDAISY_DEFAULT_CYCLES / ADC_CLK_HZ * 1e9, LIBDAISY_DEFAULT_CYCLES))
    print("audio block       %7.0f us   (96 @ 48k)" % (BLOCK_SECONDS * 1e6))
    print("settling target: half an LSB of %d bit\n" % TARGET_BITS)

    print("=== the three terms, 74HC4067, nothing fitted at COM ===")
    print("  %-7s %9s %8s | %9s %9s %9s" %
          ("pot", "R_src", "C_node", "A settle", "B acq", "C redist"))
    for pot in POTS:
        t = terms(pot, CHIPS["74HC4067 (16:1)"])
        print("  %-7s %7.2fk %6.1fpF | %7.0fns %7.0fns %7.0fns"
              % ("%dk" % (pot / 1e3), t["r"] / 1e3, t["c"] * 1e12,
                 t["a"] * 1e9, t["b"] * 1e9, t["c_redist"] * 1e9))

    for name, chip in CHIPS.items():
        top = topology(chip)
        print("\n=== %s: %d chips, busiest of %d sense pins carries %d "
              "-> %d steps/sweep ==="
              % (name, top["chips"], N_SENSE_PINS, top["on_busiest_pin"],
                 top["steps"]))
        print("  %-7s %8s %9s %10s %11s %10s"
              % ("pot", "COM cap", "sampling", "per step", "full sweep",
                 "of a block"))
        for pot in POTS:
            for c_ext, label in COM_CAPS:
                s = sweep(chip, pot, c_ext)
                if s["sampling_cycles"] is None:
                    print("  %-7s %8s   no libDaisy sampling window is long "
                          "enough" % ("%dk" % (pot / 1e3), label))
                    continue
                print("  %-7s %8s %7.1fcy %8.2fus %9.1fus %9.2f"
                      % ("%dk" % (pot / 1e3), label, s["sampling_cycles"],
                         s["step_s"] * 1e6, s["sweep_s"] * 1e6, s["blocks"]))


if __name__ == "__main__":
    main()
