#!/usr/bin/env python3
"""Rev A circuit blocks, one function per sheet (P3 spec §2, §3.2).

P2's tables live here, once: the module pin map (P2 §2), the 595 bit table and
the 165 inputs (§4), power (§5). Positions never appear: which pot sits on
which mux and which LED gets which index comes from panel-map.json
(assign.py), and P4 places the parts.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_HW = os.path.normpath(os.path.join(HERE, ".."))
for p in (_HW, HERE):
    if p not in sys.path:
        sys.path.insert(0, p)

from gen.project import Sheet  # noqa: E402
from parts import flag, make   # noqa: E402

# --- nets (P2) ----------------------------------------------------------------
GND, P12, N12 = "GND", "+12V", "-12V"
SM3V3 = "SM_3V3"          # the module's 3V3 on A10: pots, muxes, SD card
D3V3 = "3V3D"             # AMS1117 from +12 V: shift registers, LEDs
P12_IN, N12_IN = "+12V_IN", "-12V_IN"      # bus side of the reverse diodes
POWER = {GND: "power:GND", P12: "power:+12V", N12: "power:-12V",
         SM3V3: "power:+3V3", D3V3: "power:+3V3"}
DOMAINS = {"analog": {SM3V3}, "digital": {D3V3}}

SENSE = ("SENSE_0", "SENSE_1", "SENSE_2", "SENSE_3")
SR_DATA, SR_CLK, SR_LATCH, SR_DIN = "SR_DATA", "SR_CLK", "SR_LATCH", "SR_DIN"
# P2 calls the address lines A0-A2; here MUX_S0-S2, because "A0" is also the
# name of a 4051 channel pin.
MUX_S = ("MUX_S0", "MUX_S1", "MUX_S2")
MUX_EN = tuple("MUX_EN%d" % n for n in range(10))


def sr(net):
    """The shift-register side of a 1 k address/enable series resistor (P2 §3)."""
    return net + "_SR"


def led_net(index):
    return "LED%d" % index


def key_net(key):
    return "KEY_" + key


# --- P2 §2: every module pin ----------------------------------------------------
MODULE_PINS = {
    "A1": N12, "A2": "SENSE_0", "A3": "SENSE_1", "A4": GND, "A5": P12, "A7": GND,
    "A10": SM3V3,
    "B1": "OUT_R", "B2": "OUT_L", "B3": "IN_R", "B4": "IN_L",
    "B5": "GATE_A", "B6": "GATE_B", "B7": SR_DATA, "B8": SR_CLK,
    "B9": "RESET", "B10": "CLOCK",
    "C1": "PITCH_A", "C2": "MOD4_A", "C3": "MOD3_A", "C4": "MOD2_A", "C5": "MOD1_A",
    "C6": "MOD1_B", "C7": "MOD2_B", "C8": "MOD3_B", "C9": "MOD4_B", "C10": "PITCH_B",
    "D1": SR_LATCH, "D2": "SD_D3", "D3": "SD_D2", "D4": "SD_D1", "D5": "SD_D0",
    "D6": "SD_CK", "D7": "SD_CMD", "D8": "SENSE_3", "D9": "SENSE_2", "D10": SR_DIN,
}
MODULE_NC = ("A6", "A8", "A9")      # +5 V out (the module's analog rail), USB
JACKS = ("IN_L", "IN_R", "OUT_L", "OUT_R", "GATE_A", "GATE_B", "RESET", "CLOCK",
         "PITCH_A", "PITCH_B", "MOD1_A", "MOD2_A", "MOD3_A", "MOD4_A",
         "MOD1_B", "MOD2_B", "MOD3_B", "MOD4_B")
JACK_NORMAL = {"IN_R": "IN_L"}      # patch.Init(): unpatched, R takes L

# --- P2 §4: the chains ------------------------------------------------------------
SR_OUTPUTS = (      # chip order from SR_DATA; outputs QA..QH
    tuple(sr(n) for n in MUX_S) + tuple(sr(n) for n in MUX_EN[0:5]),
    tuple(sr(n) for n in MUX_EN[5:10]) + (led_net(0), led_net(1), led_net(2)),
    tuple(led_net(i) for i in range(3, 11)),
    # 15 lamps since the 9 mm panel pass (spec 2026-10-07 §5): LED11..LED14,
    # then four more spare outputs.
    tuple(led_net(i) for i in range(11, 15)) + tuple("SR_SPARE%d" % i for i in range(8, 12)),
    tuple("SR_SPARE%d" % i for i in range(8)),
)
# Spare outputs of the last chip (SR_SPARE0..7) end on a test point. The four
# spares on U_SR4 (SR_SPARE8..11) are marked no-connect instead: four more
# test points would wrap the chains sheet onto another row (check.py
# sheet_edge) and add four more parts to a board that has no use for them.
SR_TEST_POINTS = SR_OUTPUTS[-1]
SR_NO_CONNECT = tuple(n for outs in SR_OUTPUTS[:-1] for n in outs if n.startswith("SR_SPARE"))
KEYS = ("REC_A", "REC_B", "MODBTN", "SHIFTBTN")      # 165 inputs D0..D3


class Refs:
    """Sequential references per prefix, in build order -- deterministic
    because the sheets are always built in the same order."""

    def __init__(self):
        self.count = {}

    def __call__(self, prefix):
        self.count[prefix] = self.count.get(prefix, 0) + 1
        return "%s%d" % (prefix, self.count[prefix])


def _decouple(refs, rail, domain):
    return make("c100n", refs("C"), domain=domain).by_number(1, rail).by_number(2, GND)


def power(refs):
    hdr = make("header", "J_PWR", strict=True)
    for n in (1, 2):
        hdr.by_number(n, N12_IN)
    for n in (3, 4, 5, 6):
        hdr.by_number(n, GND)
    for n in (9, 10):
        hdr.by_number(n, P12_IN)
    hdr.no_connect(7, 8)        # GND on the A-100 bus; open, as on the coupon
    d_p = make("schottky", "D_P12").by_name("A", P12_IN).by_name("K", P12)
    d_n = make("schottky", "D_N12").by_name("A", N12).by_name("K", N12_IN)
    reg = make("ldo", "U_REG", domain="digital", strict=True)
    reg.by_name("VI", P12).by_name("VO", D3V3).by_name("GND", GND)
    parts = [hdr, d_p, d_n, reg]
    for kind, rail in (("c10u", P12), ("c100n", P12), ("c10u", N12), ("c100n", N12),
                       ("c10u", P12), ("c22u", D3V3)):
        parts.append(make(kind, refs("C")).by_number(1, rail).by_number(2, GND))
    # DNP tantalum option at the AMS1117 output (fixed ref: nothing renumbers)
    parts.append(make("tant22u", "C_LDO_T", dnp=True,
                      note="DNP bring-up option: fit if the regulator oscillates with "
                           "the MLCC alone")
                 .by_number(1, D3V3).by_number(2, GND))
    parts += [flag("#FLG0001", P12), flag("#FLG0002", N12)]
    return Sheet("power", "Eurorack power, reverse protection, 3V3D", parts)


def module(refs):
    sm = make("module", "U_SM", strict=True, pin_types={"D10": "input"})
    for pin, net in sorted(MODULE_PINS.items()):
        sm.by_number(pin, net)
    sm.no_connect(*MODULE_NC)
    parts = [sm]
    parts += [make("sm_socket", "J_SM%d" % i, on_board=False) for i in range(1, 5)]
    for i, net in enumerate(SENSE):
        parts.append(make("c100n", "C_SENSE%d" % i, dnp=True,
                          note="COM pad, unpopulated -- the coupon verdict (P2 §3)")
                     .by_number(1, net).by_number(2, GND))
    for net in SENSE + (SM3V3, D3V3, GND):
        parts.append(make("tp", refs("TP"), value=net).by_number(1, net))
    parts.append(flag("#FLG0003", SM3V3))
    return Sheet("module", "Patch Submodule, sense pads, test points", parts)


def chains(refs):
    parts, prev = [], SR_DATA
    for i, outs in enumerate(SR_OUTPUTS, 1):
        u = make("sr_out", "U_SR%d" % i, domain="digital", strict=True)
        u.by_name("VCC", D3V3).by_name("GND", GND).by_name("~{SRCLR}", D3V3)
        u.by_name("~{OE}", GND).by_name("SRCLK", SR_CLK).by_name("RCLK", SR_LATCH)
        u.by_name("SER", prev)
        for letter, net in zip("ABCDEFGH", outs):
            if net in SR_NO_CONNECT:
                u.no_connect(u.sym.by_name("Q" + letter))
            else:
                u.by_name("Q" + letter, net)
        if i < len(SR_OUTPUTS):
            prev = "SR_CHAIN%d" % i
            u.by_name("QH'", prev)
        else:
            u.no_connect(u.sym.by_name("QH'"))
        parts += [u, _decouple(refs, D3V3, "digital")]
    inp = make("sr_in", "U_IN1", domain="digital", strict=True)
    inp.by_name("VCC", D3V3).by_name("GND", GND).by_name("CP", SR_CLK)
    inp.by_name("~{PL}", SR_LATCH).by_name("~{CE}", GND).by_name("DS", GND)
    inp.by_name("Q7", SR_DIN)
    inp.no_connect(inp.sym.by_name("~{Q7}"))
    for i, key in enumerate(KEYS):
        inp.by_name("D%d" % i, key_net(key))
    for i in range(len(KEYS), 8):
        inp.by_name("D%d" % i, GND)
    parts += [inp, _decouple(refs, D3V3, "digital")]
    for net in MUX_S + MUX_EN:
        parts.append(make("r1k", refs("R"), note="series; P2 §3 power-up clamp")
                     .by_number(1, sr(net)).by_number(2, net))
    for key in KEYS:
        parts.append(make("r10k", refs("R"), domain="digital", note="key pull-up")
                     .by_number(1, D3V3).by_number(2, key_net(key)))
        sw = make("key", refs("SW"), value=key, panel_id=key, panel=True, strict=True)
        sw.by_number(1, key_net(key)).by_number(2, GND).no_connect(3, 4, 5, 6)
        parts.append(sw)
    for net in SR_TEST_POINTS:
        parts.append(make("tp", refs("TP"), value=net).by_number(1, net))
    return Sheet("chains", "Shift-register chains and keys", parts)


def leds(refs, led_rows):
    parts = []
    for row in sorted(led_rows, key=lambda r: r["index"]):
        i = row["index"]
        parts.append(make("r1k", refs("R"), note="LED series resistor")
                     .by_number(1, led_net(i)).by_number(2, led_net(i) + "_A"))
        d = make("led", refs("D"), value=row["id"], panel_id=row["id"], panel=True,
                 strict=True)
        parts.append(d.by_name("A", led_net(i) + "_A").by_name("K", GND))
    return Sheet("leds", "Panel LEDs", parts)


def jacks(refs, jack_holes):
    parts = []
    for h in sorted(jack_holes, key=lambda h: (h["x_mm"], h["id"])):
        if h["id"] not in JACKS:
            raise ValueError("panel jack %s is not in P2's pin map" % h["id"])
        j = make("jack", refs("J"), value=h["id"], panel_id=h["id"], panel=True,
                 strict=True)
        j.by_number("T", h["id"]).by_number("S", GND)
        if h["id"] in JACK_NORMAL:
            j.by_number("TN", JACK_NORMAL[h["id"]])
        else:
            j.no_connect("TN")
        parts.append(j)
    return Sheet("jacks", "Jacks, straight to the module (patch.Init())", parts)


def sd(refs):
    # J_SD has no domain on purpose: a digital part on SM_3V3 (spec addendum),
    # so rail_domain does not examine it.
    j = make("sd", "J_SD", panel_id="SD", panel=True, strict=True)
    for number, net in (("1", "SD_D2"), ("2", "SD_D3"), ("3", "SD_CMD"), ("4", SM3V3),
                        ("5", "SD_CK"), ("6", GND), ("7", "SD_D0"), ("8", "SD_D1"),
                        ("SH", GND)):
        j.by_number(number, net)
    c1 = make("c100n", "C_SD1", note="SD socket VDD decoupling, at the socket")
    c2 = make("c10u", "C_SD2", dnp=True,
              note="DNP bring-up option: fit if the pot scan shows SD card noise")
    for c in (c1, c2):
        c.by_number(1, SM3V3).by_number(2, GND)
    return Sheet("sd", "SD card, straight to the module (patch.Init())", [j, c1, c2])


CAL_NETS = {"CAL_GND": GND, "CAL_3V3": SM3V3}     # panel-scan spec §8


def mux_region(refs, panel_map, sense):
    """One sense pin's muxes and pots (P2 §3), assigned by assign.py."""
    muxes = {}
    parts = []
    for mux in panel_map["muxes"][sense]:
        u = make("mux", "U_MUX%d" % mux, domain="analog", strict=True)
        u.by_name("VCC", SM3V3).by_name("VEE", GND).by_name("GND", GND)
        u.by_name("A", sense)                  # the 4051 symbol calls COM "A"
        u.by_name("~{E}", MUX_EN[mux])
        for i, net in enumerate(MUX_S):
            u.by_name("S%d" % i, net)
        muxes[mux] = u
        parts += [u, _decouple(refs, SM3V3, "analog")]
    for c in panel_map["calibration"]:
        if c["sense"] == sense:
            muxes[c["mux"]].by_name("A%d" % c["channel"], CAL_NETS[c["id"]])
    for s in panel_map["spare"]:
        if s["sense"] == sense:
            muxes[s["mux"]].by_name("A%d" % s["channel"], GND)
    for p in sorted((p for p in panel_map["pots"] if p["sense"] == sense),
                    key=lambda p: (p["mux"], p["channel"])):
        wiper = "M%d_CH%d" % (p["mux"], p["channel"])
        muxes[p["mux"]].by_name("A%d" % p["channel"], wiper)
        rv = make("pot", refs("RV"), value=p["id"], panel_id=p["id"], panel=True,
                  domain="analog", strict=True)
        parts.append(rv.by_number(1, GND).by_number(2, wiper).by_number(3, SM3V3))
    # Short on purpose: a longer title runs past the A3 title block with three muxes.
    return Sheet("mux_" + sense.lower(), "Pots on %s: %s" % (
        sense, ", ".join("U_MUX%d" % m for m in panel_map["muxes"][sense])), parts)
