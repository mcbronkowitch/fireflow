#!/usr/bin/env python3
"""A 264-part, eleven-sheet stand-in for Rev A's size (P3 spec §4).

Used to measure how long the check tool's full level takes before Rev A
exists. Its verdict does not matter -- only the timing line does.
"""
import os
import sys

_HW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
if _HW not in sys.path:
    sys.path.insert(0, _HW)

from gen.netlist import Part            # noqa: E402
from gen.project import Project, Sheet  # noqa: E402

FX = "C-FIXTURE"
SOIC16 = "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm"


def project():
    sheets = []
    for i in range(11):
        parts = []
        for k in range(2):
            u = Part("U%d%02d" % (k + 1, i), "74xx:74HC595", "74HC595", SOIC16, lcsc=FX)
            u.by_name("VCC", "3V3D").by_name("GND", "GND").by_name("~{SRCLR}", "3V3D")
            u.by_name("~{OE}", "GND").by_name("SER", "L%d_IN%d" % (i, k))
            u.by_name("SRCLK", "CLK").by_name("RCLK", "LATCH")
            for q, letter in enumerate("ABCDEFGH"):
                u.by_name("Q" + letter, "L%d_Q%d_%d" % (i, k, q))
            u.by_name("QH'", "L%d_IN1" % i if k == 0 else "L%d_IN0" % (i + 1))
            parts.append(u)
        for r in range(20):
            k, q = divmod(r % 16, 8)
            parts.append(Part("R%d%02d" % (i, r), "Device:R", "1k",
                              "Resistor_SMD:R_0603_1608Metric", lcsc=FX)
                         .by_number(1, "L%d_Q%d_%d" % (i, k, q)).by_number(2, "GND"))
        for c in range(2):
            parts.append(Part("C%d%02d" % (i, c), "Device:C", "100n",
                              "Capacitor_SMD:C_0603_1608Metric", lcsc=FX)
                         .by_number(1, "3V3D").by_number(2, "GND"))
        sheets.append(Sheet("s%02d" % i, "load sheet %d" % i, parts))
    return Project("load", "hardware/gen load fixture", sheets,
                   power={"GND": "power:GND", "3V3D": "power:+3V3"}, paper="A3")
