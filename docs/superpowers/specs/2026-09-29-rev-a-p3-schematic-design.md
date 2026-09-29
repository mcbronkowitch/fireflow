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

## Addendum 2026-09-29 — stage 1 research (§6)

Sources read on 2026-09-29: Electrosmith's patch.Init() schematic
(`ES_Daisy_Patch_SM_Init_Rev1`, 9/10/2021,
https://daisy.nyc3.cdn.digitaloceanspaces.com/products/patch-init/patch_init_schematic.pdf),
the Patch SM datasheet v1.0.5
(https://daisy.nyc3.cdn.digitaloceanspaces.com/products/patch-sm/ES_Patch_SM_datasheet_v1.0.5.pdf),
JLCPCB part pages, the KiCad 10 footprint libraries, Thonk's low-profile
button page.

**Jacks: no circuit on the carrier.** Every patch.Init() jack goes straight
to its module pin: tip to the pin, sleeve to GND, the switched (NORM) contact
open — with one exception: `J_RIN`'s NORM contact is tied to `SIG_LIN`, so
the right input takes the left signal when unpatched. The schematic's notes:
CV inputs -5 V to 5 V, CV/gate outputs 0-5 V, gate inputs on BJT circuits,
and the CV and gate outputs carry output resistors on the module. The
datasheet's application figures agree (gate input 100 kΩ, audio/CV/gate
outputs 100 Ω) and name the Thonkiconn (WQP518MA) as the example jack. Rev A
therefore wires all 18 jacks 1:1, `IN_R` normalled to `IN_L` as on
patch.Init() — **Bastian to confirm the normalling** (a behaviour, not a
wiring detail). The Jacks sheet shrinks to 18 connectors and fits one A3.

**SD: no parts on the carrier.** The datasheet: no pull-up resistors
necessary; the module carries 47 kΩ pull-ups on the SDMMC lines. patch.Init()
wires the socket straight: D2 → SD_D3 (CS pin), D3 → SD_D2, D4 → SD_D1,
D5 → SD_D0, D6 → SD_CK, D7 → SD_CMD, socket VCC to **+3V3 (A10)**. Example
socket: vertical microSD PJS008U-3000-0 (datasheet figure 1.7).
**Open for plan 2 (Bastian):** patch.Init() feeds the card from A10, which on
Rev A is the analog pot reference (P2 decision 3). Card current (tens of mA,
write peaks higher) on A10 would ride on the pot reference; on `3V3D` it
loads the AMS1117, whose dissipation from +12 V is already a P2 §7 bring-up
item (~0.26 W at 30 mA; another 50 mA of card makes it ~0.95 W). One way
out: feed the AMS1117 from the module's +5 V output (A6, rated 800 mA)
instead of +12 V — a 1.7 V drop instead of 8.7 V.

**A10 load: settled.** Datasheet Table 1: 3V3 output 500 mA maximum
("firmware dependent"). 70 pots and 10 muxes (~23 mA, P2 §5) use under 5 %.

**D8/D9: settled.** Datasheet Table 2: D8 = ADC_12 (PC2), D9 = ADC_11 (PC3)
— libDaisy is right and the coupon's comment wrong (P2 §2).

**Power:** patch.Init() notes that reverse protection is on the module but
recommends your own for other parts on the rails; the datasheet needs no
bypass caps at the module. P2's SS14 pair stays (the AMS1117 and the 595s
sit on the rails).

**JLC parts, checked on jlcpcb.com 2026-09-29** (type only; stock is checked
at the freeze):

| LCSC | Part | Type |
|---|---|---|
| C9386 | 74HC4051D,653 (Nexperia), SOIC-16 | Extended |
| C5947 | 74HC595D,118 (Nexperia), SOIC-16 | Basic |
| C5613 | 74HC165D,653 (Nexperia), SOIC-16 | Extended |
| C6186 | AMS1117-3.3, SOT-223 | Basic |
| C2480 | SS14, SMA | Basic |
| C21190 | 1 kΩ 1 % 0603 | Basic |
| C25804 | 10 kΩ 1 % 0603 | Basic |
| C14663 | 100 nF 50 V X7R 0603 | Basic |
| C15850 | 10 µF 25 V X5R 0805 | Basic |

**Footprints (KiCad 10 libraries):** pot
`Potentiometer_THT:Potentiometer_Alpha_RD901F-40-00D_Single_Vertical` (the
coupon's); jack
`Connector_Audio:Jack_3.5mm_QingPu_WQP-PJ398SM_Vertical_CircularHoles`; LED
`LED_THT:LED_D3.0mm`; power header
`Connector_IDC:IDC-Header_2x05_P2.54mm_Vertical` (the coupon's); module
sockets `Connector_PinSocket_2.54mm:PinSocket_2x05_P2.54mm_Vertical` (four).
**Thonk low-profile button:** DPDT, momentary OFF-(ON), 6.2 mm cutout; Thonk
offers a KiCad footprint ZIP, downloaded with Bastian's go-ahead. It stands
**16.5 mm above the board unlatched** (14.85 latched): the keys, not only
pots and jacks, set the panel-to-board gap (P1 §6 measures it).

**THT headers and sockets** (power header, four module sockets) are
hand-soldered with the panel parts and carry `Source`, as on the coupon;
JLC's THT assembly is not used.

**Decided 2026-09-29 (Bastian), after reading the Patch SM's own schematic
(`ES_Daisy_Patch_SM_Rev3-REDUCED`,
https://daisy.nyc3.cdn.digitaloceanspaces.com/products/patch-sm/ES_Daisy_Patch_SM_Schematic.pdf):**

- The module makes its **3V3 with a TPS621x buck** (`+3V3_D`, the MCU's
  rail, on pin A10) and feeds its ADC reference (`+3V3_A`) from the same rail
  through a ferrite bead. Its **+5 V on A6 is the module's analog rail**
  (LD1117-5V linear from +12 V, feeding the CV op-amps). So P2's "analog
  3V3 = A10" is the module's digital rail; the coupon's pot measurements on
  it stand. Plan 2 renames the net `A3V3` to `SM_3V3` so the name stops
  promising "analog". A6 stays unconnected: loading the module's analog 5 V
  with our digital parts is out.
- **SD card VCC on A10**, as in patch.Init(). The AMS1117 keeps its P2 load
  (~30 mA from +12 V). Whether card access disturbs the pot scan is a P7
  bring-up check: scan noise while the card streams.
- **`IN_R` is normalled to `IN_L`**, as in patch.Init().
- **Thonk low-profile button**, footprint `SW_Push_LP_Button` from Thonk's
  ZIP (https://www.thonk.co.uk/wp-content/uploads/2024/08/THONK-SW-Push-LP-Button.zip,
  downloaded 2026-09-29; to be vendored under `hardware/lib/` in plan 2):
  six pads in two rows of three at 2.5 × 5.4 mm, 0.7 mm drills. The
  datasheet's circuit: free, 2–3 and 5–6 are closed; pushed, 1–2 and 4–5.
  Rev A uses one pole — pin 2 common, pin 1 closes to it when pressed — and
  leaves 3–6 unconnected.

## Out of scope

Placement, routing, the SD socket and the board outline (P4, and the routing
spike before it — master plan); the firmware tables (P6, from
`panel-map.json`); the aluminium plate (P5).
