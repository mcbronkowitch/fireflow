#!/usr/bin/env python3
"""Zaehlt die Bedienelemente des VCV-Panels.

Der Panelgenerator host/vcv/res/gen_panel.py ist die Autoritaet darueber,
welche Controls das Instrument hat. Dieses Skript liest ihn, statt die
Zahlen von Hand zu pflegen -- eine Handzaehlung veraltet beim naechsten
Panel-Commit, und die Hardware-Reduktion haengt an der Zahl.

Der Import ist gefahrlos: gen_panel.py schreibt SVG und Header nur unter
seinem __main__-Guard, beim blossen Import passiert nichts auf der Platte.

Aufruf:  python tools/count_panel_controls.py
"""
import sys
from pathlib import Path

_RES = Path(__file__).resolve().parent.parent / "host" / "vcv" / "res"


def _panel_module():
    sys.path.insert(0, str(_RES))
    import gen_panel
    return gen_panel


def count_controls():
    g = _panel_module()
    return {
        "panel": len(g.PANEL_PARAMS),
        "appended": len(g.APPENDED_PANEL_PARAMS),
        "hidden": len(g.HIDDEN_PARAMS),
        "runtime": len(g.RUNTIME_PANEL_PARAMS),
        "part_a": len(g.PART_A),
        "part_b": len(g.PART_B),
        "shared": len(g.SHARED),
        "inputs": len(g.INPUTS),
        "outputs": len(g.OUTPUTS),
        "lights": len(g.LIGHTS),
    }


def main():
    for key, value in count_controls().items():
        print(f"{key:12} {value}")


if __name__ == "__main__":
    main()
