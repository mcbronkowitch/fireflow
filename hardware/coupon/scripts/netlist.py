#!/usr/bin/env python3
"""Build the coupon's part list and net assignment from design.py.

Pins are resolved by NAME through the symbol library, never by number typed in
here: `sym.by_name("QH'")` raises if the symbol ever changes, where a hard-coded
11 would quietly wire the wrong leg. The only numbers written out are for
symbols whose pins have no names (Device:R, Device:C -- pins "1"/"2").
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D
import ksexp

EXTRA_SYMBOL_DIRS = [os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib", "DaisyKiCad"))]

FP_R = "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder"
FP_C = "Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder"
FP_LED = "LED_SMD:LED_0805_2012Metric_Pad1.15x1.40mm_HandSolder"
FP_TP = "TestPoint:TestPoint_Pad_D1.5mm"
FP_JP = "Jumper:SolderJumper-2_P1.3mm_Open_Pad1.0x1.5mm"
FP_POT = "Potentiometer_THT:Potentiometer_Alps_RK09K_Single_Vertical"
FP_SOIC16 = "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm"
FP_SOIC24 = "Package_SO:SOIC-24W_7.5x15.4mm_P1.27mm"


class Part:
    def __init__(self, ref, lib_id, value, footprint, note=""):
        self.ref, self.lib_id = ref, lib_id
        self.value, self.footprint = value, footprint
        self.note = note
        self.sym = load(lib_id)
        self.nets = {}                     # pin number -> net name

    def by_number(self, number, net):
        number = str(number)
        self.sym.pin(number)               # raises if absent
        if number in self.nets:
            raise ValueError("%s pin %s assigned twice" % (self.ref, number))
        self.nets[number] = net
        return self

    def by_name(self, pin_name, net):
        return self.by_number(self.sym.by_name(pin_name), net)

    def unconnected(self):
        return sorted(set(self.sym.pins) - set(self.nets), key=ksexp._pin_sort_key)


_cache = {}


def load(lib_id):
    """Symbol lookup that also searches the vendored library directories."""
    if lib_id in _cache:
        return _cache[lib_id]
    try:
        sym = ksexp.load_symbol(lib_id)
    except FileNotFoundError:
        lib, _, name = lib_id.partition(":")
        for extra in EXTRA_SYMBOL_DIRS:
            path = os.path.join(extra, lib + ".kicad_sym")
            if os.path.exists(path):
                root = ksexp.parse_file(path)
                table = {str(s[1]): s for s in ksexp.children(root, "symbol")}
                if name not in table:
                    raise KeyError("%s not in %s" % (name, path))
                sym = ksexp.Symbol(lib, name, table[name], table)
                break
        else:
            raise
    _cache[lib_id] = sym
    return sym


def build():
    parts = []

    def add(*a, **kw):
        p = Part(*a, **kw)
        parts.append(p)
        return p

    # ---- the submodule -----------------------------------------------------
    sm = add("U_SM", "Daisy-Boards:Daisy_Patch_SM", "Daisy Patch SM",
             "Daisy-Boards:DAISY_PATCH_SM",
             "through-hole landing pattern so the module stays removable")
    sm.by_number("A1", D.N12)
    sm.by_number("A4", D.GND)
    sm.by_number("A5", D.P12)
    # A6 (+5 V) is deliberately NOT connected: the coupon has no 5 V load, and
    # the symbol declares the pin a power INPUT, so wiring it to a net with no
    # driver earns an error plus an isolated-label warning for nothing.
    sm.by_number("A7", D.GND)
    sm.by_number("A10", D.P3V3)
    sm.by_number("A2", D.SENSE_16)          # ADC_9,  symbol calls it UART_RX
    sm.by_number("A3", D.SENSE_8)           # ADC_10, symbol calls it UART_TX
    sm.by_number("D8", D.SENSE_SPARE_1)     # ADC_11, symbol calls it SPI_MISO
    sm.by_number("D9", D.SENSE_SPARE_2)     # ADC_12, symbol calls it SPI_MOSI
    sm.by_number("B7", D.SR_DATA)           # symbol calls it I2C1_SCL
    sm.by_number("B8", D.SR_CLK)            # symbol calls it I2C1_SDA
    sm.by_number("D1", D.SR_LATCH)          # symbol calls it SPI_NSS
    sm.by_number("D10", D.SR_DIN)           # symbol calls it SPI_SCK
    sm.by_number("B1", D.AUDIO_R)
    sm.by_number("B2", D.AUDIO_L)

    # ---- the SM's sockets --------------------------------------------------
    # Purchase items, nothing more: two 2x10 female strips that solder into
    # U_SM's own landing pattern (40 holes, banks A/B and C/D). They carry no
    # footprint and no nets of their own -- the holes belong to the
    # DAISY_PATCH_SM footprint -- but without them on the BOM the "stays
    # removable" note above is a soldered-in module.
    for ref in ("J_SM1", "J_SM2"):
        add(ref, "Connector_Generic:Conn_02x10_Odd_Even", "2x10 socket 2.54mm",
            "", "female socket strip for the Patch SM landing pattern; "
            "BOM only, no own footprint")

    # ---- Eurorack power ----------------------------------------------------
    # NOTE: 10-pin bus pinout taken as -12 on 1/2, GND on 3..6, +12 on 9/10.
    # The A-100 standard has GND on all of 3..8 (+5 V exists only on the 16-pin
    # connector, pins 11/12); 7/8 stay open anyway as cheap insurance against
    # nonstandard bus boards -- two ground pins is all it costs. TO CONFIRM
    # against the actual bus board before the order goes out; it is the one net
    # here not read out of a datasheet. Shrouded box header, not a bare pin
    # header: the notch is what makes a rotated or row-shifted IDC plug
    # impossible, and a reversed plug is +/-12 V swapped.
    pwr = add("J_PWR", "Connector_Generic:Conn_02x05_Odd_Even", "Eurorack power",
              "Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical",
              "shrouded; pin 1 = -12 V, red stripe; 7/8 left open on purpose")
    for n in (1, 2):
        pwr.by_number(n, D.N12)
    for n in (3, 4, 5, 6):
        pwr.by_number(n, D.GND)
    for n in (9, 10):
        pwr.by_number(n, D.P12)

    # ---- the two multiplexers ---------------------------------------------
    m16 = add("U_MUX16", "74xx:CD74HC4067M", "CD74HC4067M", FP_SOIC24,
              "16:1, C_COM = 50 pF per datasheet")
    m16.by_name("COM", D.COM16)
    m16.by_name("VCC", D.A3V3)
    m16.by_name("GND", D.AGND)
    m16.by_name("~{E}", D.EN16)
    for i, addr in enumerate(D.ADDR):
        m16.by_name("S%d" % i, addr)

    m8 = add("U_MUX8", "74xx:74HC4051", "74HC4051", FP_SOIC16,
             "8:1, C_COM = 25 pF; VEE tied to AGND")
    m8.by_name("A", D.COM8)                 # the 4051 symbol calls COM "A"
    m8.by_name("VCC", D.A3V3)
    m8.by_name("GND", D.AGND)
    m8.by_name("VEE", D.AGND)
    m8.by_name("~{E}", D.EN8)
    for i in range(3):
        m8.by_name("S%d" % i, D.ADDR[i])    # the 8:1 uses three of the four

    # ---- channel nets ------------------------------------------------------
    def chan_net(prefix, idx):
        return "%s_CH%d" % (prefix, idx)

    for idx, _desc in D.MUX16_CHANNELS:
        m16.by_name("I%d" % idx, chan_net("MUX16", idx))
    for idx, _desc in D.MUX8_CHANNELS:
        m8.by_name("A%d" % idx, chan_net("MUX8", idx))

    # ---- pots --------------------------------------------------------------
    # Pin 1 to the low rail, pin 3 to the high rail, pin 2 is the wiper.
    POT_TO_CHANNEL = {"RV1": ("MUX16", 0), "RV2": ("MUX16", 2),
                      "RV3": ("MUX16", 4), "RV4": ("MUX16", 6),
                      "RV5": ("MUX8", 0), "RV6": ("MUX8", 2),
                      "RV7": ("MUX8", 4)}
    for ref, (prefix, ch) in sorted(POT_TO_CHANNEL.items()):
        p = add(ref, "Device:R_Potentiometer", D.POT_VALUES[ref], FP_POT,
                "Alpha 9 mm linear, the part Electrosmith's own example names")
        p.by_number(1, D.AGND)
        p.by_number(2, chan_net(prefix, ch))
        p.by_number(3, D.A3V3)

    # ---- neighbours pulled to opposite rails -------------------------------
    # 0 R rather than a bare trace, so a channel can be freed without a knife.
    NEIGHBOURS = [("R_HI1", D.A3V3, "MUX16", 1), ("R_LO1", D.AGND, "MUX16", 3),
                  ("R_HI2", D.A3V3, "MUX16", 5), ("R_LO2", D.AGND, "MUX16", 7),
                  ("R_HI3", D.A3V3, "MUX8", 1), ("R_LO3", D.AGND, "MUX8", 3),
                  ("R_HI4", D.A3V3, "MUX8", 5), ("R_LO4", D.AGND, "MUX8", 7)]
    for ref, rail, prefix, ch in NEIGHBOURS:
        r = add(ref, "Device:R", "0R", FP_R, "channel %s tie" % ch)
        r.by_number(1, rail)
        r.by_number(2, chan_net(prefix, ch))

    # ---- reference dividers ------------------------------------------------
    # REF_A has the source impedance of a 10k pot at mid travel; REF_B is a tenth
    # of it. If B is quiet and A is not, the noise arrived through the source
    # impedance rather than through the scan.
    REFS = [("R_REFA1", "R_REFA2", "10k", "MUX16", 8),
            ("R_REFB1", "R_REFB2", "1k", "MUX16", 9),
            ("R_REFC1", "R_REFC2", "10k", "MUX8", 6)]
    for hi_ref, lo_ref, value, prefix, ch in REFS:
        net = chan_net(prefix, ch)
        hi = add(hi_ref, "Device:R", value, FP_R, "reference divider, top")
        hi.by_number(1, D.A3V3)
        hi.by_number(2, net)
        lo = add(lo_ref, "Device:R", value, FP_R, "reference divider, bottom")
        lo.by_number(1, net)
        lo.by_number(2, D.AGND)

    # ---- spare channels tied off ------------------------------------------
    for idx, desc in D.MUX16_CHANNELS:
        if desc.startswith("spare"):
            r = add("R_SP%d" % idx, "Device:R", "0R", FP_R, "spare channel tie")
            r.by_number(1, D.AGND)
            r.by_number(2, chan_net("MUX16", idx))

    # ---- COM: the DNP capacitor and the probe point ------------------------
    for ref, net in (("C_COM16", D.COM16), ("C_COM8", D.COM8)):
        c = add(ref, "Device:C", "DNP", FP_C,
                "footprint fitted, part NOT populated -- settle-budget finding 1")
        c.by_number(1, net)
        c.by_number(2, D.AGND)
    for ref, net in (("TP_COM16", D.COM16), ("TP_COM8", D.COM8)):
        add(ref, "Connector:TestPoint", "COM probe", FP_TP,
            "scope point, so settling is seen at the node and not only through "
            "the ADC").by_number(1, net)

    # COM reaches the sense pin directly; the link is the net itself.
    for ref, a, b in (("R_S16", D.COM16, D.SENSE_16), ("R_S8", D.COM8, D.SENSE_8)):
        r = add(ref, "Device:R", "0R", FP_R,
                "COM to sense pin, 0 R so the path can be opened for a probe")
        r.by_number(1, a)
        r.by_number(2, b)

    # ---- the two supply-topology links ------------------------------------
    for ref, a, b, note in (
            ("JP_GND", D.GND, D.AGND, "analog/digital ground link"),
            ("JP_3V3", D.P3V3, D.A3V3, "analog/digital 3V3 link")):
        j = add(ref, "Jumper:Jumper_2_Open", "0R link", FP_JP, note)
        j.by_name("A", a)
        j.by_name("B", b)

    # ---- the shift-register chain -----------------------------------------
    # Four GPIOs total: data out, clock, latch/load, data in. The 595s' RCLK and
    # the 165's ~PL share the latch line, which is what makes four enough.
    sr1 = add("U_SR1", "74xx:74HC595", "74HC595", FP_SOIC16, "chain, first")
    sr2 = add("U_SR2", "74xx:74HC595", "74HC595", FP_SOIC16, "chain, second")
    for sr in (sr1, sr2):
        sr.by_name("VCC", D.P3V3)
        sr.by_name("GND", D.GND)
        sr.by_name("SRCLK", D.SR_CLK)
        sr.by_name("RCLK", D.SR_LATCH)
        sr.by_name("~{SRCLR}", D.P3V3)
        sr.by_name("~{OE}", D.GND)
    sr1.by_name("SER", D.SR_DATA)
    sr1.by_name("QH'", "SR1_TO_SR2")
    sr2.by_name("SER", "SR1_TO_SR2")
    # sr2's QH' ends the chain: left open on purpose, marked no-connect.

    sr1_out = D.ADDR + [D.EN16, D.EN8, "LED_1", "LED_2"]
    # The last two outputs of the chain stay open; a named net on an otherwise
    # unconnected pin only earns an "isolated pin label" from ERC.
    sr2_out = ["LED_%d" % n for n in range(3, 9)] + [None, None]
    for sr, outs in ((sr1, sr1_out), (sr2, sr2_out)):
        for letter, net in zip("ABCDEFGH", outs):
            if net is not None:
                sr.by_name("Q%s" % letter, net)

    inp = add("U_IN1", "74xx:74HC165", "74HC165", FP_SOIC16, "button chain")
    inp.by_name("VCC", D.P3V3)
    inp.by_name("GND", D.GND)
    inp.by_name("CP", D.SR_CLK)
    inp.by_name("~{PL}", D.SR_LATCH)
    inp.by_name("~{CE}", D.GND)
    inp.by_name("DS", D.GND)
    inp.by_name("Q7", D.SR_DIN)
    # ~Q7 unused, marked no-connect.
    inp.by_name("D0", "BTN_1")
    for n in range(1, 8):
        inp.by_name("D%d" % n, D.GND)

    btn = add("SW1", "Switch:SW_Push", "tactile", "Button_Switch_THT:SW_PUSH_6mm",
              "reads through the 165, like the panel's pads will")
    btn.by_number(1, "BTN_1")
    btn.by_number(2, D.GND)
    rpu = add("R_BTN", "Device:R", "10k", FP_R, "button pull-up")
    rpu.by_number(1, D.P3V3)
    rpu.by_number(2, "BTN_1")

    # ---- LEDs, the digital load that makes the noise question realistic ----
    for n in range(1, D.N_LEDS + 1):
        led = add("D%d" % n, "Device:LED", "green", FP_LED, "chain output %d" % n)
        led.by_name("A", "LED_%d" % n)
        led.by_name("K", "LED_%d_K" % n)
        r = add("R_LED%d" % n, "Device:R", "1k", FP_R, "LED series resistor")
        r.by_number(1, "LED_%d_K" % n)
        r.by_number(2, D.GND)

    # ---- decoupling --------------------------------------------------------
    DECOUPLE = [("C_M16", D.A3V3, D.AGND), ("C_M8", D.A3V3, D.AGND),
                ("C_SR1", D.P3V3, D.GND), ("C_SR2", D.P3V3, D.GND),
                ("C_IN1", D.P3V3, D.GND)]
    for ref, hi, lo in DECOUPLE:
        c = add(ref, "Device:C", "100n", FP_C, "decoupling")
        c.by_number(1, hi)
        c.by_number(2, lo)
    BULK = [("C_B3V3", D.P3V3, D.GND, "10u"), ("C_BA3V3", D.A3V3, D.AGND, "10u"),
            ("C_BP12", D.P12, D.GND, "10u"), ("C_BN12", D.N12, D.GND, "10u")]
    for ref, hi, lo, value in BULK:
        c = add(ref, "Device:C", value, FP_C, "bulk")
        c.by_number(1, hi)
        c.by_number(2, lo)

    # ---- audio out and the remaining probe points -------------------------
    # Pin numbers here ARE the footprint pads: T = tip, R = ring, S = sleeve.
    # The sleeve is the cable shield, so it carries AGND and nothing else --
    # the previous AudioJack2_Ground symbol put AUDIO_R on the sleeve and its
    # ground pin G on no pad at all, which no netlist-level check can see.
    jack = add("J_AUDIO", "Connector_Audio:AudioJack3", "3.5 mm out",
               "Connector_Audio:Jack_3.5mm_CUI_SJ1-3513N_Horizontal",
               "the measurement rig is mono; both channels are brought out")
    jack.by_number("T", D.AUDIO_L)
    jack.by_number("R", D.AUDIO_R)
    jack.by_number("S", D.AGND)

    PROBES = [("TP_AUDIO_L", D.AUDIO_L), ("TP_A3V3", D.A3V3), ("TP_3V3", D.P3V3),
              ("TP_AGND", D.AGND), ("TP_GND", D.GND), ("TP_CLK", D.SR_CLK),
              ("TP_ADC11", D.SENSE_SPARE_1), ("TP_ADC12", D.SENSE_SPARE_2)]
    for ref, net in PROBES:
        add(ref, "Connector:TestPoint", "probe", FP_TP, "").by_number(1, net)

    # ---- ERC power flags ---------------------------------------------------
    # Every rail here is fed by a connector pin, which no symbol declares as an
    # output, so ERC would report each one as undriven without these.
    # KiCad's own reference style for these is #FLGnnnn; anything shorter is
    # reported as an annotation error. They contribute no netlist node -- see
    # is_virtual() -- so build.py excludes them from the comparison.
    # NOT on +3V3 or +5V: the submodule's own pins already declare themselves
    # power outputs there, and a flag on top of one is a power-output clash.
    # Not on GND: the submodule's own GND pins are declared power OUTPUTS, so a
    # flag there is a power-output-to-power-output clash. +3V3 does need one --
    # A10 is declared a power input in the symbol, not an output.
    for n, net in enumerate([D.P12, D.N12, D.AGND, D.A3V3, D.P3V3]):
        add("#FLG%04d" % (n + 1), "power:PWR_FLAG", "PWR_FLAG", "").by_number(1, net)

    return parts


def is_virtual(ref):
    """Power symbols and flags: real in the schematic, absent from the netlist.

    KiCad does not emit a node for a PWR_FLAG pin, so comparing an exported
    netlist against the intent has to leave them out or every rail reports a
    missing node.
    """
    return ref.startswith("#")


def nets_from(parts, include_virtual=True):
    nets = {}
    for p in parts:
        if not include_virtual and is_virtual(p.ref):
            continue
        for pin, net in p.nets.items():
            nets.setdefault(net, []).append((p.ref, pin))
    return nets


if __name__ == "__main__":
    parts = build()
    nets = nets_from(parts)
    print("%d parts, %d nets" % (len(parts), len(nets)))
    singles = sorted(n for n, v in nets.items() if len(v) < 2)
    print("single-node nets (%d): %s" % (len(singles), ", ".join(singles)))
    for p in parts:
        u = p.unconnected()
        if u:
            print("  %-10s unconnected pins: %s" % (p.ref, ", ".join(u)))
