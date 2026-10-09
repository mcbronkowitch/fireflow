# Grip test — the acrylic plate (Rev A P1)

Spec: [`2026-09-29-rev-a-p1-panel-parts-design.md`](../superpowers/specs/2026-09-29-rev-a-p1-panel-parts-design.md) §5–§6.
Plate: clear 3 mm acrylic cut from `host/vcv/res/FireflowHW-cut.svg`, with
`FireflowHW-print.svg` printed at 100 % underneath (check the 100 mm bar with
a ruler before cutting the print).

The rail slots leave 1.4 mm of acrylic to the plate edge (Eurorack standard,
fine in aluminium), so screw the acrylic plate only hand-tight with nylon
washers, or rest it in the rails unscrewed.

## Orders

Both placed by Bastian on 2026-10-02, a week ahead of the spec's 9 Oct
deadline. Delivery dates go in the table when the parcels arrive.

| Supplier | What | Price | Arrived |
|---|---|---|---|
| Thonk | One set of panel parts (below) | **£179.01 paid**: £164.51 parts excl. VAT + £14.50 FedEx International Connect Plus (4–7 days). DDU: import VAT and carrier fee still due on delivery | |
| Formulor | The plate: `FireflowHW-cut.svg` as of commit `60fbc37e`, clear acrylic GS 3 mm, P2. Formulor read it as one part, 304.4 × 128.5 mm | **28.14 € paid** | |

Thonk cart, 207 items, read back before checkout:

| Qty | Part |
|---|---|
| 77 | Alpha 9 mm vertical, T18 shaft, B10K |
| 7 / 2 / 7 | Davies 1900H T18 — green / dark blue / orange |
| 23 / 14 / 24 | Micro Knobs T18 — green / dark blue / orange |
| 20 | Thonkiconn mono (WQP518MA) |
| 1 | Knurled nuts, bag of 50 |
| 6 | DPDT momentary low-profile push buttons |
| 1 | Low-profile button caps, taster pack (3 each red, yellow, green, blue) |
| 25 | Flat-top 3 mm LEDs, L-424SURDTK (red; Thonk's only single-colour 3 mm LED) |

Green Micro Knobs: Thonk had 23 in stock, so part A's 22 small knobs have one
spare instead of the spec's two.

**Correction cut (not ordered yet).** Checklist items 1 and 2 failed, so the
plate is cut again from the 9 mm panel pass's file: `FireflowHW-cut.svg` as of
commit `815dbe3a` (the last commit that changed it; 304.4 × 128.5 mm, 115
holes, as `gen_hw_cut.py` prints). Bastian orders it at Formulor, then repeats
items 1, 2 and 4 on it; item 3 passed on the first plate.

## Measurements (spec §6)

| Date | What | Result |
|---|---|---|
| | Alpha support lugs in the coupon's RD901F slots | |
| | Pot bushing thread and length vs 3 mm acrylic / 2 mm aluminium | |
| | Jack thread vs 3 mm (the keys have no thread or nut; they sit on the board and are not tested on the acrylic) | |
| | Jack nut outer diameter (does it cover the `CLK_L` LED hole at 6.7 mm, or the combined `SHIFTBTN_L` / `MODBTN_L` holes, 9.50 mm from their jack?) | |
| | Panel-to-board height, pot and jack seated on a board | |
| | Alpha anti-rotation tab: present? where? | |
| | Actual plate width of a bought 60 HP blank, if one is at hand (the cut assumes 304.4 mm) | |
| | Kerf: measure one pot hole and one LED hole (expected ~0.1–0.2 mm over size); the narrowest webs in the file (`FireflowHW-holes.json`, 111 holes without the mounting slots, as printed by `web()` of `test_hw_cut.py`): the two satellite lamps `CLK_L` and `RST_L` to their jacks 2.15 mm; the cluster lamps 3.70 mm (`REC_A_L` / `REC_B_L` to their key) and 4.54 mm (the other pot-cluster lamps to their own pot); `SHIFTBTN_L` / `MODBTN_L` 4.85 mm to their key; `LVL_A_L` / `LVL_B_L` 6.14 mm. Measure the two satellite webs and one cluster web on the correction cut: with the kerf above they should come out about 0.1–0.2 mm below those figures | |

## Checklist (spec §5)

| # | Check | Result | Fix (position / size / legend) |
|---|---|---|---|
| 1 | Pinch two neighbouring knobs at once, every group | **Failed** (2026-10-07): the small caps stand 5.3 mm apart edge to edge, measured from the generator (spec 2026-10-07 §2) | The 9 mm panel pass: every control on a 20.2 mm raster, no two caps closer than 9 mm (smallest gap 9.70 mm in the current generator). Spec [`2026-10-07-panel-nine-mm-raster-design.md`](../superpowers/specs/2026-10-07-panel-nine-mm-raster-design.md), plan [`2026-10-07-panel-nine-mm-raster.md`](../superpowers/plans/2026-10-07-panel-nine-mm-raster.md) |
| 2 | Turn each knob without brushing a neighbour's cap | **Failed** (2026-10-07): same cause as 1 | Same pass |
| 3 | All 18 jacks patched with real cables: what gets covered | **Passed** (2026-10-07): "patching was fine" (Bastian) | none |
| 4 | Every legend readable from playing distance, through the print | | |
| 5 | All four keys: reach, accidental presses | moved to the first Rev A board — the keys sit on the board, not in the acrylic | |

Every fix goes into `host/vcv/res/gen_hw_panel.py`, never into a generated
file. The freeze tag `panel-freeze-2026-11-06` goes on the generator commit
that passes this list. The freeze also needs every `KNOWN_PANEL` entry in
`hardware/reva/place_check.py` (P4-1 spec §5.3) and in `hardware/reva/route_check.py`
(P4-2 spec §5) to carry a `SIGNED_OFF` record, the same rule
`fab.release_blockers()` applies (P4-3 spec §4.4.6); an entry without one blocks
the freeze.
