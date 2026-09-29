#!/usr/bin/env python3
"""S-expression reading for KiCad files, and symbol-library access.

Written because regex over .kicad_sym drifts silently: a loose pattern walked
from 74HC165 into a neighbouring symbol and returned a '138 pinout that looked
plausible. Everything here parses properly and raises rather than guessing.

Only what the coupon generator needs: read a file into nested lists, find a
symbol, and report its pins with number, name, electrical type and geometry.
"""
import os

KICAD_ROOT = os.environ.get(
    "KICAD_ROOT", r"C:\Users\bernd\AppData\Local\Programs\KiCad\10.0")
SYMBOL_DIR = os.path.join(KICAD_ROOT, "share", "kicad", "symbols")
KICAD_CLI = os.path.join(KICAD_ROOT, "bin", "kicad-cli.exe")


class Atom(str):
    """A bare token, so quoted strings and symbols stay distinguishable."""


def parse(text, pos=0):
    """Parse one S-expression starting at `pos`; return (node, next_pos)."""
    n = len(text)
    while pos < n and text[pos] in " \t\r\n":
        pos += 1
    if pos >= n:
        raise ValueError("unexpected end of input")
    if text[pos] == "(":
        pos += 1
        out = []
        while True:
            while pos < n and text[pos] in " \t\r\n":
                pos += 1
            if pos >= n:
                raise ValueError("unbalanced '('")
            if text[pos] == ")":
                return out, pos + 1
            node, pos = parse(text, pos)
            out.append(node)
    if text[pos] == '"':
        pos += 1
        buf = []
        while pos < n:
            c = text[pos]
            if c == "\\":
                buf.append(text[pos + 1])
                pos += 2
                continue
            if c == '"':
                return "".join(buf), pos + 1
            buf.append(c)
            pos += 1
        raise ValueError("unterminated string")
    start = pos
    while pos < n and text[pos] not in " \t\r\n()":
        pos += 1
    return Atom(text[start:pos]), pos


def parse_file(path):
    with open(path, encoding="utf-8") as fh:
        node, _ = parse(fh.read())
    return node


def children(node, tag):
    """Direct children of `node` whose head atom is `tag`."""
    return [c for c in node
            if isinstance(c, list) and c and isinstance(c[0], Atom)
            and c[0] == tag]


def child(node, tag):
    got = children(node, tag)
    if len(got) != 1:
        raise KeyError("expected exactly one (%s ...), found %d" % (tag, len(got)))
    return got[0]


class Symbol:
    """One library symbol: its raw node, and its pins resolved.

    A derived symbol carries `(extends "Base")` and no geometry of its own --
    74xx:74HC165 is one, and a parser that ignores this reports zero pins.
    `base` is the symbol the drawing and pins actually come from; `node` stays
    the derived one, because that is what the schematic references by name.
    """

    def __init__(self, lib, name, node, table=None):
        self.lib = lib
        self.name = name
        self.node = node
        self.extends = None
        self.base = self
        ext = children(node, "extends")
        if ext:
            self.extends = str(ext[0][1])
            if table is None or self.extends not in table:
                raise KeyError("%s:%s extends %s, which is not in the library"
                               % (lib, name, self.extends))
            self.base = Symbol(lib, self.extends, table[self.extends], table)
        self.pins = {}          # number -> dict(name, etype, x, y, angle, length)
        self._collect_pins(self.base.node if self.extends else node)
        if not self.pins:
            raise ValueError("%s:%s has no pins" % (lib, name))

    def _collect_pins(self, node):
        """Pins live in the unit sub-symbols, so recurse rather than assume."""
        for c in node:
            if not isinstance(c, list) or not c or not isinstance(c[0], Atom):
                continue
            if c[0] == "pin":
                self._add_pin(c)
            elif c[0] == "symbol":
                self._collect_pins(c)

    def _add_pin(self, node):
        etype = str(node[1])
        at = child(node, "at")
        x, y = float(at[1]), float(at[2])
        angle = int(float(at[3])) if len(at) > 3 else 0
        length = float(child(node, "length")[1])
        name = str(child(node, "name")[1])
        number = str(child(node, "number")[1])
        if number in self.pins:
            raise ValueError("%s:%s duplicate pin %s" % (self.lib, self.name, number))
        self.pins[number] = dict(name=name, etype=etype, x=x, y=y,
                                 angle=angle, length=length)

    def pin(self, number):
        number = str(number)
        if number not in self.pins:
            raise KeyError("%s:%s has no pin %s (has: %s)"
                           % (self.lib, self.name, number,
                              ", ".join(sorted(self.pins))))
        return self.pins[number]

    def by_name(self, pin_name):
        """Pin number carrying `pin_name`. Raises unless exactly one matches."""
        hits = [n for n, p in self.pins.items() if p["name"] == pin_name]
        if len(hits) != 1:
            raise KeyError("%s:%s: %d pins named %r (names: %s)"
                           % (self.lib, self.name, len(hits), pin_name,
                              ", ".join(sorted(p["name"] for p in self.pins.values()))))
        return hits[0]

    def describe(self):
        rows = sorted(self.pins.items(), key=lambda kv: _pin_sort_key(kv[0]))
        return "%s:%s [%d pins] " % (self.lib, self.name, len(self.pins)) + \
               "  ".join("%s=%s" % (n, p["name"] or "-") for n, p in rows)


def _pin_sort_key(number):
    try:
        return (0, int(number), "")
    except ValueError:
        return (1, 0, number)


_LIB_CACHE = {}


def load_symbol(lib_id):
    """`load_symbol("74xx:74HC595")` -> Symbol. Raises if absent."""
    lib, _, name = lib_id.partition(":")
    if not name:
        raise ValueError("lib_id must be 'Library:Symbol', got %r" % lib_id)
    if lib not in _LIB_CACHE:
        path = os.path.join(SYMBOL_DIR, lib + ".kicad_sym")
        if not os.path.exists(path):
            raise FileNotFoundError(path)
        root = parse_file(path)
        table = {}
        for sym in children(root, "symbol"):
            table[str(sym[1])] = sym
        _LIB_CACHE[lib] = table
    table = _LIB_CACHE[lib]
    if name not in table:
        raise KeyError("%s not in %s.kicad_sym" % (name, lib))
    return Symbol(lib, name, table[name], table)


def dump(node, indent=0):
    """Re-serialise a node. Used to embed lifted symbols into a schematic."""
    pad = "\t" * indent
    if isinstance(node, Atom):
        return str(node)
    if isinstance(node, str):
        return '"%s"' % node.replace("\\", "\\\\").replace('"', '\\"')
    head = node[0] if node else Atom("")
    simple = all(not isinstance(c, list) for c in node)
    if simple:
        return "(" + " ".join(dump(c) for c in node) + ")"
    parts = ["(" + dump(head)]
    for c in node[1:]:
        if isinstance(c, list):
            parts.append("\n" + pad + "\t" + dump(c, indent + 1))
        else:
            parts.append(" " + dump(c))
    parts.append("\n" + pad + ")")
    return "".join(parts)


if __name__ == "__main__":
    import sys
    for lib_id in sys.argv[1:] or [
            "74xx:CD74HC4067M", "74xx:74HC4051", "74xx:74HC595",
            "74xx:74HC165", "Device:R_Potentiometer", "Device:LED",
            "Device:R", "Device:C", "Connector_Audio:AudioJack2_Ground",
            "Jumper:Jumper_2_Open", "Connector:TestPoint", "Switch:SW_Push",
            "Connector_Generic:Conn_02x05_Odd_Even"]:
        print(load_symbol(lib_id).describe())
        print()
