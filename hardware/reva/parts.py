#!/usr/bin/env python3
"""Rev A part catalogue (P3 spec §3.2): one row per part type.

LCSC numbers and Basic/Extended types are the spec addendum's, checked on
jlcpcb.com 2026-09-29 (C45783 the same day); stock is checked at the freeze.
Panel parts carry Source="Thonk" and are hand-soldered with the panel on;
THT connectors are hand-soldered too. The SD socket's footprint, once the open
"P4", is chosen: the generated FireFlow:SD_Yamaichi_PJS008U-3000-0 (P4-1 spec §4.2).
"""
import os
import sys
from collections import namedtuple

_HW = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
if _HW not in sys.path:
    sys.path.insert(0, _HW)

from gen.netlist import Part  # noqa: E402

PartType = namedtuple("PartType", "lib_id value footprint lcsc source jlc_type note")

FP_R = "Resistor_SMD:R_0603_1608Metric"
FP_C0603 = "Capacitor_SMD:C_0603_1608Metric"
FP_C0805 = "Capacitor_SMD:C_0805_2012Metric"
FP_SOIC16 = "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm"
THONK = "Thonk"
HAND = "hand-soldered"

CATALOGUE = {
    "mux": PartType("74xx:74HC4051", "74HC4051", FP_SOIC16, "C9386", "", "Extended",
                    "Nexperia 74HC4051D,653"),
    "sr_out": PartType("74xx:74HC595", "74HC595", FP_SOIC16, "C5947", "", "Basic",
                       "Nexperia 74HC595D,118"),
    "sr_in": PartType("74xx:74HC165", "74HC165", FP_SOIC16, "C5613", "", "Extended",
                      "Nexperia 74HC165D,653"),
    "ldo": PartType("Regulator_Linear:AMS1117-3.3", "AMS1117-3.3",
                    "Package_TO_SOT_SMD:SOT-223-3_TabPin2", "C6186", "", "Basic",
                    "3V3D from +12 V"),
    "schottky": PartType("Diode:SS14", "SS14", "Diode_SMD:D_SMA", "C2480", "", "Basic",
                         "series reverse protection"),
    "r1k": PartType("Device:R", "1k", FP_R, "C21190", "", "Basic", "1 %"),
    "r10k": PartType("Device:R", "10k", FP_R, "C25804", "", "Basic", "1 %"),
    "c100n": PartType("Device:C", "100n", FP_C0603, "C14663", "", "Basic", "50 V X7R"),
    "c10u": PartType("Device:C", "10u 25V", FP_C0805, "C15850", "", "Basic", "X5R"),
    "c22u": PartType("Device:C", "22u 25V", FP_C0805, "C45783", "", "Basic",
                     "X5R; AMS1117 output"),
    # C1967941 checked on jlcpcb.com 2026-09-29: Extended, 0 in stock -- irrelevant
    # while it is DNP. Low-ESR because the AMS1117 datasheet caps the output
    # capacitor's ESR at 0.5 Ohm. Pin 1 is +, pin 2 is - (probed).
    "tant22u": PartType("Device:C_Polarized", "22u 16V tant",
                        "Capacitor_Tantalum_SMD:CP_EIA-6032-28_Kemet-C", "C1967941", "",
                        "Extended", "AVX TPSC226K016R0300 low-ESR tantalum, 300 mOhm; "
                        "DNP option for AMS1117 stability"),
    "pot": PartType("Device:R_Potentiometer", "10k",
                    "Potentiometer_THT:Potentiometer_Alpha_RD901F-40-00D_Single_Vertical",
                    "", THONK, "", "Alpha RD901F-40 9 mm, T18 shaft, B10K"),
    "jack": PartType("Connector_Audio:AudioJack2_SwitchT", "Thonkiconn",
                     "Connector_Audio:Jack_3.5mm_QingPu_WQP-PJ398SM_Vertical_CircularHoles",
                     "", THONK, "", "Thonkiconn PJ398SM mono"),
    "key": PartType("Switch:SW_Push_DPDT", "LP button", "Thonk:SW_Push_LP_Button",
                    "", THONK, "", "Thonk low-profile button, momentary; pin 1 closes to 2"),
    "led": PartType("Device:LED", "LED 3mm", "LED_THT:LED_D3.0mm", "", THONK, "",
                    "3 mm flat-top LED"),
    "header": PartType("Connector_Generic:Conn_02x05_Odd_Even", "Eurorack power",
                       "Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical", "", HAND, "",
                       "shrouded keyed 2x5 box header; pin 1 = -12 V"),
    "sm_socket": PartType("Connector_Generic:Conn_02x05_Odd_Even", "2x5 socket", "",
                          "", HAND, "", "2x5 female socket for the Patch SM, cut from "
                          "a 2x10 strip; its holes belong to U_SM's footprint"),
    "module": PartType("Daisy-Boards:Daisy_Patch_SM", "Daisy Patch SM",
                       "Daisy-Boards:DAISY_PATCH_SM", "", "Electrosmith", "",
                       "Patch Submodule, socketed"),
    "sd": PartType("Connector:Micro_SD_Card", "microSD", "FireFlow:SD_Yamaichi_PJS008U-3000-0",
                   "", HAND, "", "Yamaichi PJS008U-3000-0 vertical microSD, THT (P4-1 spec §4.2)"),
    "tp": PartType("Connector:TestPoint", "TP", "TestPoint:TestPoint_Pad_D1.5mm", "", "",
                   "", "probe pad"),
}


def make(kind, ref, value=None, **kw):
    """A Part of catalogue type `kind`; keyword arguments go to Part."""
    t = CATALOGUE[kind]
    kw.setdefault("note", t.note)
    if kind == "tp":
        kw.setdefault("in_bom", False)          # a pad, nothing to buy
    return Part(ref, t.lib_id, t.value if value is None else value, t.footprint,
                lcsc=t.lcsc, source=t.source, **kw)


def flag(ref, net):
    """A PWR_FLAG: the rail is fed by a pin no symbol declares an output."""
    return Part(ref, "power:PWR_FLAG", "PWR_FLAG", "").by_number(1, net)
