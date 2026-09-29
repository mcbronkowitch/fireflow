#!/usr/bin/env python3
"""Bills of materials from a part list (P3 spec §3.1).

jlc_rows: what JLCPCB assembles -- every real, populated part with an LCSC
number, one line per (value, footprint, LCSC). hand_rows: what is bought
elsewhere and hand-soldered -- parts with a Source, one line per (source,
footprint, note). DNP parts, power symbols and flags, and parts kept out of
the BOM (test points) appear in neither.
"""
import csv
import io
import re

from gen import netlist as N

JLC_FIELDS = ("Comment", "Designator", "Footprint", "LCSC")
HAND_FIELDS = ("Source", "Qty", "Part", "Footprint", "Designators")


def _natural(ref):
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", ref)]


def _bought(parts):
    return [p for p in parts if not N.is_virtual(p.ref) and p.in_bom and not p.dnp]


def jlc_rows(parts):
    groups = {}
    for p in _bought(parts):
        if p.lcsc:
            groups.setdefault((p.value, p.footprint, p.lcsc), []).append(p.ref)
    rows = [{"Comment": value, "Designator": ",".join(sorted(refs, key=_natural)),
             "Footprint": fp.split(":", 1)[-1], "LCSC": lcsc}
            for (value, fp, lcsc), refs in groups.items()]
    return sorted(rows, key=lambda r: _natural(r["Designator"].split(",")[0]))


def hand_rows(parts):
    groups = {}
    for p in _bought(parts):
        if p.source and not p.lcsc:
            groups.setdefault((p.source, p.footprint, p.note), []).append(p)
    rows = []
    for (source, fp, note), ps in groups.items():
        rows.append({"Source": source, "Qty": len(ps), "Part": note or ps[0].value,
                     "Footprint": fp.split(":", 1)[-1] if fp else "(no footprint)",
                     "Designators": ",".join(sorted((p.ref for p in ps), key=_natural))})
    return sorted(rows, key=lambda r: (r["Source"], r["Part"]))


def to_csv(rows, fields):
    buf = io.StringIO()
    w = csv.DictWriter(buf, fieldnames=fields, lineterminator="\n",
                       quoting=csv.QUOTE_ALL)
    w.writeheader()
    w.writerows(rows)
    return buf.getvalue()
