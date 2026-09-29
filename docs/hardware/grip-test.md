# Grip test — the acrylic plate (Rev A P1)

Spec: [`2026-09-29-rev-a-p1-panel-parts-design.md`](../superpowers/specs/2026-09-29-rev-a-p1-panel-parts-design.md) §5–§6.
Plate: clear 3 mm acrylic cut from `host/vcv/res/FireflowHW-cut.svg`, with
`FireflowHW-print.svg` printed at 100 % underneath (check the 100 mm bar with
a ruler before cutting the print).

## Measurements (spec §6)

| Date | What | Result |
|---|---|---|
| | Alpha support lugs in the coupon's RD901F slots | |
| | Pot bushing thread and length vs 3 mm acrylic / 2 mm aluminium | |
| | Jack and key thread vs 3 mm | |
| | Jack nut and key nut outer diameter (do they cover the satellite LED holes at 6.7 mm?) | |
| | Panel-to-board height, pot and jack seated on a board | |
| | Alpha anti-rotation tab: present? where? | |
| | Actual plate width of a bought 60 HP blank, if one is at hand (the cut assumes 304.4 mm) | |

## Checklist (spec §5)

| # | Check | Result | Fix (position / size / legend) |
|---|---|---|---|
| 1 | Pinch two neighbouring knobs at once, every group | | |
| 2 | Turn each knob without brushing a neighbour's cap | | |
| 3 | All 18 jacks patched with real cables: what gets covered | | |
| 4 | Every legend readable from playing distance, through the print | | |
| 5 | All four keys: reach, accidental presses | | |

Every fix goes into `host/vcv/res/gen_hw_panel.py`, never into a generated
file. The freeze tag `panel-freeze-2026-11-06` goes on the generator commit
that passes this list.
