#!/usr/bin/env python3
"""Parts, pins and nets, shared by the generated boards.

Moved from hardware/coupon/scripts/netlist.py; the coupon's build() stayed
there. Pins are resolved by NAME through the symbol library, never by number
typed in: `sym.by_name("QH'")` raises if the symbol ever changes, where a
hard-coded 11 would quietly wire the wrong leg. Numbers are used only for
symbols whose pins have no names (Device:R, Device:C -- pins "1"/"2").

Added for Rev A (P3 spec §3.1): `lcsc`, `source` and `panel_id` travel into
the schematic as fields; `domain` ("analog"/"digital") and `strict` feed the
fast checks; `no_connect()` marks a pin unconnected on purpose.
"""
import os

from gen import ksexp

EXTRA_SYMBOL_DIRS = [os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "lib", "DaisyKiCad"))]


class Part:
    def __init__(self, ref, lib_id, value, footprint, note="", lcsc="",
                 source="", panel_id="", domain="", strict=False, panel=False,
                 dnp=False, in_bom=True, on_board=True, pin_types=None):
        self.ref, self.lib_id = ref, lib_id
        self.value, self.footprint = value, footprint
        self.note = note
        self.lcsc, self.source, self.panel_id = lcsc, source, panel_id
        self.domain = domain
        self.strict = strict       # every pin must carry a net or a no_connect
        self.panel = panel         # mounted in a panel hole; needs a PanelId
        self.dnp = dnp             # footprint fitted, part not populated
        self.in_bom = in_bom       # False: a pad, not a part (test points)
        self.on_board = on_board   # False: bought, but has no footprint (sockets)
        self.sym = load(lib_id)
        self.pin_types = {str(k): v for k, v in (pin_types or {}).items()}
        for number in self.pin_types:
            self.sym.pin(number)   # raises if the override names no pin
        self.nets = {}             # pin number -> net name
        self.nc = set()            # pins left unconnected on purpose

    def etype(self, number):
        """The pin's electrical type, with this part's override applied.

        Overrides exist for symbols whose pin types name a default role the
        board does not use: the Patch SM declares D10 (SPI_SCK) an output, and
        Rev A reads it as the 165's serial input (P2 §4).
        """
        number = str(number)
        return self.pin_types.get(number, self.sym.pin(number)["etype"])

    def by_number(self, number, net):
        number = str(number)
        self.sym.pin(number)                # raises if absent
        if number in self.nets:
            raise ValueError("%s pin %s assigned twice" % (self.ref, number))
        if number in self.nc:
            raise ValueError("%s pin %s is marked no-connect" % (self.ref, number))
        self.nets[number] = net
        return self

    def by_name(self, pin_name, net):
        return self.by_number(self.sym.by_name(pin_name), net)

    def no_connect(self, *numbers):
        for number in numbers:
            number = str(number)
            self.sym.pin(number)
            if number in self.nets:
                raise ValueError("%s pin %s carries net %s; cannot be no-connect"
                                 % (self.ref, number, self.nets[number]))
            self.nc.add(number)
        return self

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
