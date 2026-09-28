#!/usr/bin/env python3
"""Guard rails for the scan budget.

No pytest in this environment -- plain asserts, exit code says it all, same
shape as test_settle_budget.py.  Run from anywhere:

    python tools/test_scan_budget.py

Every check below is one of the claims docs/hardware/scan-budget.md makes.
If a constant is revised, the claim it carries has to be re-read, not the
threshold widened.  ctest runs this file (scan_budget_guard).
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scan_budget as s

FAILS = []

G67 = "74HC4067 (16:1)"
G51 = "74HC4051 (8:1)"


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def test_conversion_times():
    # Section 2's two figures, from the measured 6.146 MHz clock.  wait-measured
    # section 9 quotes "63 us of acquisition" for the long rung; with the 8.5
    # cycles of conversion on top a whole conversion is 64.4 us.
    short = s.conversion_s(16.5) * 1e6
    long_ = s.conversion_s(387.5) * 1e6
    check(abs(short - 4.07) < 0.02,
          "a 16.5-cycle conversion is %.2f us, section 2 says 4.07" % short)
    check(abs(long_ - 64.43) < 0.05,
          "a 387.5-cycle conversion is %.2f us, section 2 says 64.4" % long_)
    check(abs(387.5 / s.ADC_CLK_HZ * 1e6 - 63.0) < 0.1,
          "387.5 cycles of acquisition is no longer ~63 us")


def test_the_shipping_pattern_fits_one_step_per_block():
    # The headline.  libDaisy's free-running DMA at the shell's rung, twelve
    # channels, OVS_32: one rotation plus one oversampling group has to land
    # between the address write (plus settle) and the next block's read.
    f = s.free_running(s.SHELL_RUNG, s.LIBDAISY_OVS)
    check(f["fits"], "the shipping pattern no longer fits one step per "
          "block (rotation %.0f us + group %.0f us > window %.0f us)"
          % (f["rotation_s"] * 1e6, f["group_s"] * 1e6, f["window_s"] * 1e6))
    check(abs(f["rotation_s"] * 1e6 - 1562) < 5,
          "the rotation is %.0f us, section 3 says 1562" % (f["rotation_s"] * 1e6))
    check(250e-6 < f["slack_s"] < 350e-6,
          "the slack is %.0f us, section 3 says ~300" % (f["slack_s"] * 1e6))


def test_sixteen_and_a_half_is_the_longest_rung_that_fits_at_ovs_32():
    # An iff, because section 3 says both halves: every rung up to 16.5 fits,
    # every rung above it does not.  Moving the settle ceiling, the block or
    # the clock can move this boundary, and then the sentence is wrong.
    for rung in s.SAMPLING_CYCLES:
        fits = s.free_running(rung, s.LIBDAISY_OVS)["fits"]
        check(fits == (rung <= 16.5),
              "free-running at %.1f cycles, OVS_32: fits=%s; section 3 says "
              "16.5 is the longest rung that fits" % (rung, fits))


def test_the_long_rung_needs_oversampling_off():
    # Section 4, rows F2 and F3.
    at_32 = s.free_running(387.5, 32)
    off = s.free_running(387.5, 1)
    check(not at_32["fits"], "387.5 cycles at OVS_32 now fits one block")
    check(at_32["rotation_s"] > 0.024,
          "387.5 at OVS_32 rotates in %.1f ms, section 4 says ~24.7"
          % (at_32["rotation_s"] * 1e3))
    check(off["fits"], "387.5 cycles with oversampling off no longer fits")


def test_blocking_placements_against_the_reserve():
    # Section 4, rows B1-B3: the reserve is 2.9 points of a 2 ms block.
    reserve = s.reserve_s()
    check(abs(reserve * 1e6 - 58) < 1,
          "the reserve is %.1f us, section 4 says 58" % (reserve * 1e6))
    b1 = s.blocking_cost_s(16.5, 1)
    b2 = s.blocking_cost_s(387.5, 1)
    b3 = s.blocking_cost_s(16.5, 32)
    check(b1 < reserve, "B1 (16.5, single) no longer fits the reserve: "
          "%.1f us" % (b1 * 1e6))
    check(b2 > 4 * reserve, "B2 (387.5, single) is %.0f us, no longer more "
          "than four times the reserve" % (b2 * 1e6))
    check(b3 > 8 * reserve, "B3 (16.5, OVS_32) is %.0f us, no longer more "
          "than eight times the reserve" % (b3 * 1e6))


def test_panel_channel_count_and_topology():
    # The count comes from the hardware panel generator, not from a document.
    # io-budget section 3 still says 65; the plate has had 70 pot positions
    # since 2026-08-30.  If this goes red, the panel moved: re-read
    # scan-budget.md section 5 and io-budget section 3 in the same commit.
    n = s.pot_positions()
    check(n == 70, "the hardware panel has %d pot positions, scan-budget.md "
          "says 70" % n)
    a = s.topology(s.CHIPS[G67], n)
    b = s.topology(s.CHIPS[G51], n)
    check((a["chips"], a["steps"]) == (5, 32),
          "16:1 at %d channels: %d chips, %d steps; section 5 says 5 and 32"
          % (n, a["chips"], a["steps"]))
    check((b["chips"], b["steps"]) == (9, 24),
          "8:1 at %d channels: %d chips, %d steps; section 5 says 9 and 24"
          % (n, b["chips"], b["steps"]))
    # And the old count gives the same steps, which is why settle-budget.md's
    # sweep figures survive the recount.
    for name in (G67, G51):
        check(s.topology(s.CHIPS[name], 65)["steps"]
              == s.topology(s.CHIPS[name], n)["steps"],
              "%s: 65 and %d channels no longer need the same steps" % (name, n))


def test_sweep_rates():
    # Section 5: one step per block.
    a = s.sweep(32, 1)
    b = s.sweep(24, 1)
    check(abs(a["seconds"] - 0.064) < 1e-9 and abs(a["hz"] - 15.625) < 1e-6,
          "32 steps at one per block: %.1f ms / %.2f Hz, section 5 says "
          "64 ms / 15.6 Hz" % (a["seconds"] * 1e3, a["hz"]))
    check(abs(b["seconds"] - 0.048) < 1e-9,
          "24 steps at one per block: %.1f ms, section 5 says 48"
          % (b["seconds"] * 1e3))


def test_two_steps_per_block_need_half_the_oversampling():
    # Section 6: the arithmetic ceiling for a faster scan inside the shipping
    # pattern.  OVS_32 cannot do two, OVS_16 can.
    check(not s.free_running(s.SHELL_RUNG, 32, steps_per_block=2)["fits"],
          "OVS_32 now fits two steps per block")
    check(s.free_running(s.SHELL_RUNG, 16, steps_per_block=2)["fits"],
          "OVS_16 no longer fits two steps per block")


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    if FAILS:
        print("FAIL (%d)" % len(FAILS))
        for f in FAILS:
            print("  - " + f)
        sys.exit(1)
    print("scan budget OK")
