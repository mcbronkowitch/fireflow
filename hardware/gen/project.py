#!/usr/bin/env python3
"""What a generated schematic is made of: sheets of parts, plus the facts the
checks need -- power nets, rail domains, panel holes, the ERC waiver file.
P3 spec §2-§4.
"""
from collections import Counter


class Sheet:
    def __init__(self, name, title, parts):
        if not name or name != name.lower() or not name.replace("_", "").isalnum():
            raise ValueError("sheet name must be lower_snake_case, got %r" % name)
        self.name, self.title, self.parts = name, title, list(parts)


class Project:
    def __init__(self, name, title, sheets, power=None, domain_rails=None,
                 holes=None, waivers=None, paper="A3", flat=False, comments=(),
                 lib_dirs=None, ground="GND"):
        self.name, self.title = name, title
        self.sheets = list(sheets)
        self.power = dict(power or {})            # net -> power symbol lib_id
        self.domain_rails = {k: set(v) for k, v in (domain_rails or {}).items()}
        self.holes = holes                        # [{"id", "kind", ...}] or None
        self.waivers = waivers                    # ERC waiver file path or None
        self.paper = paper
        self.flat = flat                          # one sheet, no overview (the coupon)
        self.comments = list(comments)
        self.lib_dirs = dict(lib_dirs or {})      # lib name -> abs dir with <lib>.pretty / .kicad_sym
        self.ground = ground
        if flat and len(self.sheets) != 1:
            raise ValueError("a flat project has exactly one sheet")
        names = [s.name for s in self.sheets]
        if len(set(names)) != len(names):
            raise ValueError("duplicate sheet names: %s" % names)
        if not flat and name in names:
            raise ValueError("sheet %r would overwrite the overview file" % name)
        refs = Counter(p.ref for s in self.sheets for p in s.parts)
        dup = sorted(r for r, n in refs.items() if n > 1)
        if dup:
            raise ValueError("refs used more than once: %s" % ", ".join(dup))
        slashed = sorted({net for p in self.parts() for net in p.nets.values()
                          if "/" in net})
        if slashed:
            raise ValueError("net names may not contain '/': %s" % slashed)

    def parts(self):
        return [p for s in self.sheets for p in s.parts]

    def sheet_of(self):
        return {p.ref: s.name for s in self.sheets for p in s.parts}
