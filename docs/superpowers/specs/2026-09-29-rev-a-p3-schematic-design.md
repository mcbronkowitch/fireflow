# Rev A P3 — schematic generator

**Date:** 2026-09-29
**Status:** approved in conversation (Bastian), to be reviewed as written
**Parent:** [`2026-09-28-rev-a-master-plan-design.md`](2026-09-28-rev-a-master-plan-design.md), sub-project P3
**Inputs:** [P2 pin map](2026-09-29-rev-a-p2-pin-map-design.md), [P1 panel parts](2026-09-29-rev-a-p1-panel-parts-design.md) and its hole list `host/vcv/res/FireflowHW-holes.json`
**Deadline:** Fri 13 Nov 2026 — clean ERC, proven netlist, every SMD part from JLC's catalogue and in stock

P3 generates the Rev A schematic from Python, as the coupon's was, and hands
P4 a netlist it can place from. The coupon's lesson drives the design: the
agent writing a generator must be able to check its own work in seconds, and
must see the result as an image, not infer it from report lines.

## 1. Approach — decided

**Extend the coupon generator** (`hardware/coupon/scripts/`) rather than
switch tools or draw by hand. It produced a working schematic with a
node-by-node netlist proof; what Rev A needs on top is listed in §3.

Rejected:

- **Netlist-only tools** (SKiDL, atopile) straight into layout: faster to
  write, but no ERC and no drawing to read — nothing to check a bring-up
  fault against.
- **Drawing it by hand in KiCad:** Bastian's time, and the schematic would
  drift from the panel generator (working rule 1).

## 2. Sheets

One sheet per circuit block, **A3** each, so a printout stays readable; each
sheet is also a zone of the layout.

| # | Sheet | Contents |
|---|---|---|
| 1 | Overview | Sheet symbols for all blocks, revision, notes |
| 2 | Power | 10-pin shrouded header, 2 × SS14, bulk capacitors, AMS1117-3.3 for `3V3D` |
| 3 | Module | Patch SM (40 pins), its sockets, the four unpopulated sense-line pads, test points |
| 4–7 | Mux region 0–3 | The region's 3 or 2 muxes with their pots, calibration inputs, spare inputs tied to GND |
| 8 | Chains | 5 × 74HC595, 1 × 74HC165, the 13 × 1 kΩ address/enable series resistors, 4 keys with pull-ups |
| 9 | LEDs | 19 LEDs with series resistors |
| 10 | Jacks | 18 Thonkiconn with the patch.Init() circuit; split into audio and CV/gate if it does not fit one A3 |
| 11 | SD | Socket, pull-ups, decoupling after Electrosmith's reference |

Drawing rules, on every sheet:

- **No wires between parts.** As on the coupon, every pin gets a short stub
  and a label. Nets inside one sheet get local labels, nets that cross
  sheets get global labels. Net names are P2's: `SENSE_0`, `SR_DATA`, `EN3`,
  `LED12`, …
- **Power nets** (`GND`, `+12V`, `-12V`, `A3V3`, `3V3D`) are power symbols,
  not labels, so the two 3.3 V rails cannot be mistaken for each other.
- **Each chip's 100 nF** sits on the chip's own sheet, next to it.
- **The four mux regions are four separate sheet files**, not four instances
  of one: instances would merge their global labels, and the regions differ
  in size.
- **Text** at KiCad's default 1.27 mm, never smaller.

## 3. Generator structure

### 3.1 `hardware/gen/` — shared, moved out of the coupon

| File | From | Added |
|---|---|---|
| `ksexp.py` | as is | KiCad path overridable by environment variable; default unchanged |
| `netlist.py` | the core only (`Part`, pin-name wiring, `nets_from`); the coupon's `build()` stays in the coupon | fields `LCSC`, `Source`, `PanelId` |
| `sch_writer.py` | `generate_schematic.py` | several sheets under an overview sheet; power symbols; seeded UUIDs (derived from ref and pin, identical on every run); every unit of a multi-unit symbol |
| `check.py` | `build.py` | the three check levels (§4); ERC gated against a waiver list; one PNG per sheet |
| `bom.py` | new | JLC BOM (Comment, Designator, Footprint, LCSC) |

The coupon then calls the shared tools. **Proof that the move broke
nothing:** the coupon's exported netlist is identical before and after,
timestamps aside. The coupon's committed files are not regenerated.

### 3.2 `hardware/reva/` — Rev A only

| File | Job |
|---|---|
| `parts.py` | Part catalogue, one row per part type: value, symbol, footprint, LCSC number, Basic/Extended, source |
| `assign.py` | Reads the hole list and computes pot → (sense pin, mux, channel) and LED → index (§5); writes `panel-map.json` |
| `blocks.py` | One function per sheet: `power()`, `module()`, `mux_region(i)`, `chains()`, `leds()`, `jacks()`, `sd()`. P2's tables live here, once |
| `build.py` | Blocks → schematic → checks → PDF, PNGs, BOM |
| `erc-waivers.txt` | Known ERC exceptions, one per line, each with its reason; anything not listed fails the build |

**`PanelId` links the three generators.** Every pot, jack, key and LED
carries the id of its hole from `FireflowHW-holes.json`. The checks assert
one part per hole and one hole per panel part; P4 places by this id; after a
panel change in the grip test, one re-run moves the schematic, the mux
assignment and the firmware table together.

**Committed outputs:** the `.kicad_sch` files, `panel-map.json` and the BOM,
all byte-stable (a test runs the generator twice and compares). PDF and PNGs
go to the ignored `hardware/reva/out/`; a PDF is attached to each review.

## 4. Checking while building

The coupon's slowest phase was a blind loop: an agent whose only feedback was
a text report after a full KiCad run (memory `fireflow-pcb-generator-speedups`).
P3 builds the check tool **before** the first Rev A sheet exists (stage 2b,
§7), in three levels behind one command:

| Level | Cost | Checks | When the implementer runs it |
|---|---|---|---|
| `--fast` | pure Python, no KiCad, no file written | on the intent: nets with a single pin; two outputs on one net; `A3V3` and `3V3D` crossed (a mux or pot on `3V3D`, a 595/165/LED on `A3V3`); a module pin used twice or neither used nor marked unconnected; `PanelId` missing or duplicate; SMD part without LCSC | after every edit |
| `--sheet NAME` | one sheet written, netlist exported | netlist against intent for that sheet; overlap and sheet-edge check; **PNG of the sheet**, which the implementer opens and looks at | after every finished sheet |
| `--full` | all sheets | ERC against `erc-waivers.txt`, whole netlist, BOM, PDF, byte-stability | at the end of every task |

**Measured 2026-09-29** on the coupon schematic (~80 parts, one sheet), with
`kicad-cli` 10.0: netlist export 0.9 s, ERC 6.1 s, PDF 0.4 s. Rev A has about
three times the parts; the full level's time is measured when the multi-sheet
skeleton stands (stage 2b), not assumed.

Rules for the check tool:

1. **Short, located reports.** `FAIL mux_region_2: net EN7 has one pin
   (U2.Q4)`, not a raw ERC dump.
2. **Every rule has a sabotage mode** (`--sabotage <rule>`) that breaks the
   intent on purpose and shows the rule go red, including a mode proving that
   a rule which finds nothing to examine fails rather than passes. Net names
   are imported from `blocks.py`, never retyped in a check.
3. **The controller re-runs `--full` at every task boundary**; an
   implementer's "green" is not evidence.

**Tests in ctest** (plain scripts, as the panel guards):

1. `assign`: every one of the 70 pots exactly once; regions contiguous in x;
   no mux over 8 channels; both calibration channels present.
2. `blocks` (the `--fast` level): all 40 module pins used or marked
   unconnected; every `PanelId` once; every SMD part with an LCSC number.
3. The `--full` level with `kicad-cli`. A missing `kicad-cli` fails the test;
   it is never skipped.

Each is shown red once before it is trusted.

## 5. Pot and LED assignment

P2 §3 left "which pot goes to which mux" to P4's placement. **P3 takes it
over** and computes it from the hole list; P4 then places each mux at the
centre of its group. The firmware table (P6) follows `panel-map.json`.

- **Regions:** four contiguous bands in x, one per sense pin, sized to their
  capacity (`SENSE_0`/`SENSE_1` 3 muxes = 24 channels, `SENSE_2`/`SENSE_3`
  2 muxes = 16). The x-order of the four sense pins is one constant in
  `assign.py`; P4 may change it.
- **Muxes:** within a region, groups of at most 8 pots by proximity; the
  two calibration channels (GND, `A3V3`) and the spares fill the remaining
  inputs.
- **LEDs:** indices by x from left to right, so each 595 drives one band of
  the panel (P2 §4's bit table).
- **Deterministic:** the same hole list gives the same assignment, byte for
  byte. `assign.py` prints the longest pot-to-group-centre distance per
  region for review.
- **Jacks:** P2's default. P2 allows swapping MOD jacks within a deck for
  routing; that is P4's call and a re-run.

## 6. Research — stage 1, before any code

Each answer goes into this spec as an addendum, with its source and date.
Any download is announced to Bastian first.

| Question | Source | For |
|---|---|---|
| The jack and SD circuits | Electrosmith's published patch.Init() schematic | Jacks and SD sheets. The module may condition audio and CV itself, leaving little to add; unknown until read |
| A10's current rating; pin voltage ranges | Patch SM datasheet | whether 70 pots plus 10 muxes (~23 mA) may hang on A10 |
| LCSC number, Basic/Extended, stock | JLC parts search, read only | every SMD part: 4051, 595, 165, AMS1117, SS14, R/C, header, module sockets |
| Symbols and footprints | KiCad 10 libraries, the vendored `hardware/lib/DaisyKiCad`, Thonk datasheets | pot RD901F (the coupon's), Thonkiconn, 3 mm LED, **the Thonk low-profile button** (footprint still unknown) |

Three rules set now:

- **Panel-mounted THT parts** (pots, jacks, keys, LEDs) carry
  `Source=Thonk` instead of an LCSC number; they are hand-soldered with the
  panel on.
- **The module sockets** go into the BOM; they were missing from the
  coupon's.
- **The SD socket** is chosen in P4 (P2 §5). The schematic places the symbol
  with the right pins and marks the footprint `P4`; the checks allow exactly
  this one open footprint.

**Basic vs. Extended** (the decision P3 owns): Basic wherever an equivalent
exists. The Extended parts known so far are the 74HC4051 (C9386) and the
74HC165 (C5613). The BOM lists each Extended type with JLC's per-type loading
fee as shown on the JLC page on the day of the check.

**Stock** is checked in the browser at the schematic freeze (13 Nov) and
again before the order (18 Dec); results are dated in
`hardware/reva/stock-check.md`.

## 7. Stages and dates

Dates are ceilings; the work is expected to run faster.

| By | Stage | Done when |
|---|---|---|
| Fri 2 Oct | 1 Research | §6 answered, with sources, in an addendum |
| Fri 9 Oct | 2 Shared tools in `hardware/gen/`; **2b check tool with all three levels and sabotage modes**, on a skeleton of empty sheets | coupon netlist unchanged; every rule shown red; full-level time measured |
| Fri 16 Oct | 3 `parts.py`, `assign.py`, `blocks.py` | ctest 1 and 2 green; `panel-map.json` exists |
| Fri 23 Oct | 4 Multi-sheet writer fills all sheets | ctest 3 green, ERC clean, every sheet's PNG looked at |
| Fri 30 Oct | 5 BOM, review sheet | Bastian has read the PDF |
| Fri 6 Nov | Panel freeze (P1) | re-run on the frozen panel |
| Mon 9 Nov | Netlist to P4 | layout starts |
| Fri 13 Nov | Sign-off | feedback from the first layout days folded in; stock checked |

The grip test runs alongside once the parts arrive; each panel change it
brings is a re-run, not a redesign.

## Out of scope

Placement, routing, the SD socket and the board outline (P4, and the routing
spike before it — master plan); the firmware tables (P6, from
`panel-map.json`); the aluminium plate (P5).
