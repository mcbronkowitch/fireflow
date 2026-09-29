#!/usr/bin/env python3
"""BOM grouping: JLC lines by (value, footprint, LCSC), natural designator
order; DNP parts, flags and parts kept out of the BOM appear nowhere; hand
parts group by (source, footprint, note).

    python hardware/gen/test_bom.py
"""
import os
import sys

sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")))
from gen import bom                  # noqa: E402
from gen.netlist import Part         # noqa: E402

R = "Resistor_SMD:R_0603_1608Metric"


def parts():
    return [Part("R10", "Device:R", "1k", R, lcsc="C21190"),
            Part("R2", "Device:R", "1k", R, lcsc="C21190"),
            Part("R3", "Device:R", "10k", R, lcsc="C25804"),
            Part("C9", "Device:C", "100n", "Capacitor_SMD:C_0603_1608Metric",
                 lcsc="C14663", dnp=True),
            Part("TP1", "Connector:TestPoint", "TP", "TestPoint:TestPoint_Pad_D1.5mm",
                 lcsc="C1", in_bom=False),
            Part("TP2", "Connector:TestPoint", "TP", "TestPoint:TestPoint_Pad_D1.5mm",
                 source="Fixture", in_bom=False),
            Part("#FLG0001", "power:PWR_FLAG", "PWR_FLAG", "", lcsc="C2"),
            Part("D2", "Device:LED", "B", "LED_THT:LED_D3.0mm", source="Thonk", note="3 mm LED"),
            Part("D1", "Device:LED", "A", "LED_THT:LED_D3.0mm", source="Thonk", note="3 mm LED")]


def main():
    failures = []
    jlc = bom.jlc_rows(parts())
    want = [{"Comment": "1k", "Designator": "R2,R10", "Footprint": "R_0603_1608Metric",
             "LCSC": "C21190"},
            {"Comment": "10k", "Designator": "R3", "Footprint": "R_0603_1608Metric",
             "LCSC": "C25804"}]
    if jlc != want:
        failures.append("jlc_rows: got %s" % jlc)
    hand = bom.hand_rows(parts())
    if hand != [{"Source": "Thonk", "Qty": 2, "Part": "3 mm LED",
                 "Footprint": "LED_D3.0mm", "Designators": "D1,D2"}]:
        failures.append("hand_rows: got %s" % hand)
    text = bom.to_csv(jlc, bom.JLC_FIELDS)
    if not text.startswith('"Comment","Designator","Footprint","LCSC"\n'):
        failures.append("jlc csv header: %r" % text.splitlines()[0])
    if failures:
        for f in failures:
            print("FAIL " + f)
        return 1
    print("ok: BOM grouping, exclusions and CSV header")
    return 0


if __name__ == "__main__":
    sys.exit(main())
