#!/usr/bin/env python3
"""A three-sheet fixture for the shared writer and the check tool.

Not a circuit anyone builds. Every writer and check feature has something
KiCad must read back here: two rails drawn with one +3V3 power symbol (A3V3
and 3V3D), a sheet-local net, nets crossing sheets, a multi-unit op-amp, a
strict part with explicit no-connects, panel-mounted parts with PanelIds, and
analog/digital rail domains. LCSC values are placeholders ("C-FIXTURE").
"""
import os
import sys

_HW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
if _HW not in sys.path:
    sys.path.insert(0, _HW)

from gen.netlist import Part            # noqa: E402
from gen.project import Project, Sheet  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
POWER = {"GND": "power:GND", "+12V": "power:+12V", "-12V": "power:-12V",
         "A3V3": "power:+3V3", "3V3D": "power:+3V3"}
DOMAINS = {"analog": {"A3V3"}, "digital": {"3V3D"}}
HOLES = [{"id": "DEMO_LED", "kind": "led"}, {"id": "DEMO_POT", "kind": "pot"}]
FX = "C-FIXTURE"
FP_R = "Resistor_SMD:R_0603_1608Metric"
FP_C = "Capacitor_SMD:C_0603_1608Metric"
FP_HDR3 = "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical"
FP_HDR2 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"


def _power_sheet():
    parts = []
    j1 = Part("J1", "Connector_Generic:Conn_01x03", "power in", FP_HDR3, source="fixture")
    j1.by_number(1, "+12V").by_number(2, "GND").by_number(3, "-12V")
    j2 = Part("J2", "Connector_Generic:Conn_01x02", "rails in", FP_HDR2, source="fixture")
    j2.by_number(1, "A3V3").by_number(2, "3V3D")
    j3 = Part("J3", "Connector_Generic:Conn_01x02", "data in", FP_HDR2, source="fixture")
    j3.by_number(1, "DEMO_DATA").by_number(2, "DEMO_CLK")
    j4 = Part("J4", "Connector_Generic:Conn_01x02", "sense out", FP_HDR2, source="fixture")
    j4.by_number(1, "DEMO_SENSE").by_number(2, "GND")
    j5 = Part("J5", "Connector_Generic:Conn_02x05_Odd_Even", "bus",
              "Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical", source="fixture", strict=True)
    for n in (1, 2):
        j5.by_number(n, "-12V")
    for n in (3, 4, 5, 6):
        j5.by_number(n, "GND")
    for n in (9, 10):
        j5.by_number(n, "+12V")
    j5.no_connect(7, 8)
    parts += [j1, j2, j3, j4, j5]
    for ref, rail in (("C1", "3V3D"), ("C2", "A3V3")):
        c = Part(ref, "Device:C", "100n", FP_C, lcsc=FX)
        parts.append(c.by_number(1, rail).by_number(2, "GND"))
    for i, net in enumerate(("+12V", "-12V", "GND", "A3V3", "3V3D")):
        parts.append(Part("#FLG%04d" % (i + 1), "power:PWR_FLAG", "PWR_FLAG", "")
                     .by_number(1, net))
    return Sheet("power", "Power and inputs", parts)


def _logic_sheet():
    u1 = Part("U1", "74xx:74HC595", "74HC595", "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm",
              lcsc=FX, domain="digital", strict=True)
    u1.by_name("VCC", "3V3D").by_name("GND", "GND").by_name("~{SRCLR}", "3V3D")
    u1.by_name("~{OE}", "GND").by_name("SER", "DEMO_DATA")
    u1.by_name("SRCLK", "DEMO_CLK").by_name("RCLK", "DEMO_CLK").by_name("QA", "LED_A")
    u1.by_name("QB", "DEMO_EN")
    u1.no_connect(*[u1.sym.by_name(n) for n in
                    ("QC", "QD", "QE", "QF", "QG", "QH", "QH'")])
    u2 = Part("U2", "Amplifier_Operational:TL072", "TL072",
              "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", lcsc=FX, domain="analog")
    u2.by_number(3, "POT_W").by_number(2, "BUF_OUT").by_number(1, "BUF_OUT")
    u2.by_number(5, "GND").by_number(6, "U2B_FB").by_number(7, "U2B_FB")
    u2.by_number(8, "+12V").by_number(4, "-12V")
    rv1 = Part("RV1", "Device:R_Potentiometer", "10k",
               "Potentiometer_THT:Potentiometer_Alpha_RD901F-40-00D_Single_Vertical",
               source="fixture", panel_id="DEMO_POT", domain="analog")
    rv1.by_number(1, "GND").by_number(2, "POT_W").by_number(3, "A3V3")
    c3 = Part("C3", "Device:C", "100n", FP_C, lcsc=FX, domain="analog")
    c3.by_number(1, "A3V3").by_number(2, "GND")
    u3 = Part("U3", "74xx:74HC4051", "74HC4051", "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm",
              lcsc=FX, domain="analog", strict=True)
    u3.by_name("VCC", "A3V3").by_name("VEE", "GND").by_name("GND", "GND")
    u3.by_name("A", "DEMO_SENSE").by_name("~{E}", "DEMO_EN")
    for i in range(3):
        u3.by_name("S%d" % i, "GND")
    u3.by_name("A0", "POT_W")
    for ch in range(1, 8):
        u3.by_name("A%d" % ch, "GND")
    return Sheet("logic", "Shift register and buffer", [u1, u2, rv1, c3, u3])


def _led_sheet():
    d1 = Part("D1", "Device:LED", "red", "LED_THT:LED_D3.0mm",
              source="fixture", panel_id="DEMO_LED")
    d1.by_name("A", "LED_A").by_name("K", "LED_A_K")
    r1 = Part("R1", "Device:R", "1k", FP_R, lcsc=FX)
    r1.by_number(1, "LED_A_K").by_number(2, "GND")
    return Sheet("leds", "Panel LED", [d1, r1])


def project():
    return Project("demo", "hardware/gen demo fixture",
                   [_power_sheet(), _logic_sheet(), _led_sheet()],
                   power=POWER, domain_rails=DOMAINS, holes=HOLES,
                   waivers=os.path.join(HERE, "demo-erc-waivers.txt"),
                   paper="A3",
                   comments=["GENERATED by hardware/gen/sch_writer.py -- fixture"])
