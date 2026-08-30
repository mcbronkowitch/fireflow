#!/usr/bin/env python3
"""Guard rails for the settle-time budget.

No pytest in this environment -- plain asserts, exit code says it all, same
shape as test_count_panel_controls.py.  Run from tools/:

    python test_settle_budget.py

Every check below is one of the claims docs/hardware/settle-budget.md makes.
If a constant is revised, the claim it carries has to be re-read, not the
threshold widened.
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import settle_budget as s

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def _all_configs():
    for name, chip in s.CHIPS.items():
        for pot in s.POTS:
            for c_ext, label in s.COM_CAPS:
                yield name, chip, pot, c_ext, label


def test_adc_clock_derivation():
    # PLL3 (M=6, N=295, R=32) on a 16 MHz HSE, then ADC_CLOCK_ASYNC_DIV2.
    # The 24.58 MHz kernel matches libDaisy's own "24.xMhz" comment on PLL3R.
    check(abs(s.ADC_KERNEL_HZ - 24.58e6) < 0.02e6,
          "PLL3R moved: %.3f MHz, expected ~24.58" % (s.ADC_KERNEL_HZ / 1e6))
    check(abs(s.ADC_CLK_HZ - 12.29e6) < 0.01e6,
          "ADC clock moved: %.3f MHz, expected ~12.29" % (s.ADC_CLK_HZ / 1e6))


def test_redistribution_is_the_binding_term():
    # The finding that turned the analysis around: it is never the ST
    # acquisition rule (term B) that sets the sampling window, it is the
    # charge the S&H cap carries over from the previous channel (term C).
    for name, chip, pot, c_ext, label in _all_configs():
        t = s.terms(pot, chip, c_ext)
        check(t["c_redist"] > t["b"],
              "%s pot=%.0fk cap=%s: term B (%.0f ns) beats term C (%.0f ns) -- "
              "the sampling-window argument in the doc no longer holds"
              % (name, pot / 1e3, label, t["b"] * 1e9, t["c_redist"] * 1e9))


def test_libdaisy_default_sampling_window_is_too_short():
    # docs claim: the hand-written scan must raise SamplingTime above the
    # SPEED_8CYCLES_5 default in every configuration considered.
    default_s = s.LIBDAISY_DEFAULT_CYCLES / s.ADC_CLK_HZ
    for name, chip, pot, c_ext, label in _all_configs():
        t = s.terms(pot, chip, c_ext)
        check(max(t["b"], t["c_redist"]) > default_s,
              "%s pot=%.0fk cap=%s: 8.5 cycles (%.0f ns) would already do -- "
              "the SPEED_16CYCLES_5 recommendation is no longer general"
              % (name, pot / 1e3, label, default_s * 1e9))


def test_a_capacitor_at_com_never_helps():
    # "weg, nicht kleiner": every cap fitted at COM costs sweep time, so the
    # envelope spec's "<= 1 nF or none" resolves to none.
    for name, chip in s.CHIPS.items():
        for pot in s.POTS:
            bare = s.sweep(chip, pot, 0.0)
            for c_ext, label in s.COM_CAPS:
                if c_ext == 0.0:
                    continue
                with_cap = s.sweep(chip, pot, c_ext)
                if with_cap["sweep_s"] is None:
                    continue          # unbuildable is worse than slow
                check(with_cap["sweep_s"] > bare["sweep_s"],
                      "%s pot=%.0fk: %s at COM is not worse than nothing "
                      "(%.1f us vs %.1f us) -- the 'fit no cap' rule needs "
                      "re-reading" % (name, pot / 1e3, label,
                                      with_cap["sweep_s"] * 1e6,
                                      bare["sweep_s"] * 1e6))


def test_ten_k_sweeps_inside_one_audio_block():
    # The headline: a full sweep of every channel fits in one block, so the
    # design is not stuck at the one-step-per-block pacing the 2026-08-23
    # capture was measured at.
    for name, chip in s.CHIPS.items():
        r = s.sweep(chip, 10e3, 0.0)
        if r["blocks"] is None:
            FAILS.append("%s at 10k/no cap has no long enough sampling window "
                         "at all" % name)
            continue
        check(r["blocks"] < 0.5,
              "%s at 10k/no cap needs %.2f of a block -- the 'a full sweep "
              "fits in one block' claim is gone" % (name, r["blocks"]))


def test_fifty_k_is_where_the_16_to_1_falls_over():
    # Why the pot value is a real decision and not a detail.
    r = s.sweep(s.CHIPS["74HC4067 (16:1)"], 50e3, 0.0)
    check(r["blocks"] is None or r["blocks"] > 1.0,
          "74HC4067 at 50k/no cap now fits in %.2f of a block -- the pot-value "
          "ceiling in the doc is wrong" % (r["blocks"] or 0.0))


def test_eight_to_one_costs_fewer_steps_than_sixteen_to_one():
    # Counterintuitive, and the reason timing does not decide 8:1 vs 16:1:
    # nine chips distribute over four sense pins better than five do.
    a = s.topology(s.CHIPS["74HC4051 (8:1)"])
    b = s.topology(s.CHIPS["74HC4067 (16:1)"])
    check(a["steps"] < b["steps"],
          "the 8:1 no longer needs fewer sweep steps (%d) than the 16:1 (%d)"
          % (a["steps"], b["steps"]))
    check(a["chips"] == 9, "8:1 chip count moved: %d, expected 9 for %d "
          "channels" % (a["chips"], s.N_CHANNELS))
    check(b["chips"] == 5, "16:1 chip count moved: %d, expected 5 for %d "
          "channels" % (b["chips"], s.N_CHANNELS))


def test_model_reproduces_the_plans_own_ten_nanofarad_figure():
    # The only settle number that existed in the repo before this tool: the
    # Phase-0 plan's step 5 says 10 nF at COM makes "tau ~ 26 us".  The model
    # has to land on that, or it is not modelling the same circuit.
    t = s.terms(10e3, s.CHIPS["74HC4067 (16:1)"], 10e-9)
    tau_us = t["r"] * t["c"] * 1e6
    check(25.0 < tau_us < 28.0,
          "tau at 10k/10nF is %.1f us; the plan says ~26 us, so the model no "
          "longer describes the same circuit" % tau_us)


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print("FAIL (%d)" % len(FAILS))
        for f in FAILS:
            print("  - " + f)
        sys.exit(1)
    print("settle budget OK")
