# 3V3D power budget and U_REG temperature (Rev A)

U_REG (AMS1117-3.3, LCSC C6186, SOT-223, on B.Cu) makes `3V3D` for the shift
registers, the key pull-ups and the 15 panel LEDs from +12 V. It is a linear
regulator, so it burns (Vin − 3.3 V) × I_load. Until 2026-10-01 the tab had
almost no copper around it. This page holds the load inventory, the
dissipation, the thermal resistance before and after the 3V3D copper area at
the tab, and the junction temperature.

The numbers on this page come from four sources:

- The current, dissipation, copper area, θJA and Tj figures are lines
  printed by `KIPY hardware/reva/power_budget.py [BOARD] [--before BOARD]`.
- The `reg_copper` line is `hardware/reva/route_check.py`'s output, from
  `route.py --write`.
- The area is computed from the placed regulator by `route.reg_copper()` in
  `hardware/reva/route.py` (since 2026-10-08), and its 0.2 mm clearance is
  `RU.CLEARANCE`.
- The board size (300.8 × 110 mm) is P4-1's outline.

The script last ran on 2026-10-08 (Task 7c, the 9 mm panel pass) against
the committed `hardware/reva/kicad/reva.kicad_pcb`. Two older boards serve as
"before":

- the board at `6203bc7d` (the P4-2 merge, no copper area), extracted with
  `git show 6203bc7d:hardware/reva/kicad/reva.kicad_pcb`;
- the 2026-10-01 board with the first, hand-drawn area (`824c9269`).

The script is report only, never a gate. Re-run it after any change to the
board or to one of its assumptions, and copy the new lines here.

The script rounds P and θJA to the printed digits before it computes Tj, so
every Tj below recomputes by hand.

Every input is tagged in the script and on this page:

- **[board]**: measured on the board by the script.
- **[datasheet]**: quoted from a named datasheet.
- **[listing]**: a distributor's product listing, not a datasheet.
- **[assumed]**: a choice made here, with no source.

## Sources

**AMS1117 [datasheet].** AMS's own PDF could not be fetched (TLS handshake
failure on advanced-monolithic.com; LCSC's PDF link returned no text). The
text read is Slkor's AMS1117 second-source datasheet, the one the P3
schematic spec already cites
(https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/8122/AMS11173.3SOT223.pdf).
It gives:

- quiescent current 3 mA typ and 10 mA max at Vin = Vout + 1.25 V. This
  sits in an Electrical Characteristics table headed "Vin ≤ 7 V, Tj = 25 °C
  unless otherwise specified". Nothing in it covers 12 V.
- in the absolute maximum table:
  - θJA (SOT-223) 150 °C/W, with no copper area stated;
  - maximum power dissipation (SOT-223) 600 mW;
- θJC (SOT-223) 15 °C/W;
- operating junction range −40…125 °C;
- thermal shutdown at 150 °C.

**LCSC C6186 [listing].** Iq 5 mA, operating temperature −40…+125 °C, input
maximum 15 V.

**Copper area against θJA [datasheet]: TI LM1117 (SNOS412Q, January 2023).**

- Table 9-2 gives SOT-223 RθJA against copper area. Its footnote says "Tab
  of device attached to topside copper". TI's top side is the component
  side, which for U_REG is B.Cu.
- The 1 oz copper weight and the still air are not in the table. They come
  from Figure 9-11, which plots the same data: its caption reads "RθJA vs
  1-oz Copper Area for SOT-223", and its legend reads "TA = 25°C, Still Air".

| top-side copper (in²) | 0.0123 | 0.066 | 0.3 | 0.53 | 0.76 | 1.0 |
|---|---|---|---|---|---|---|
| θJA (°C/W) | 136 | 123 | 84 | 75 | 69 | 66 |

- This is a **proxy**: TI's die and TI's test board, not the AMS1117 and not
  this board. The two parts are pin compatible, and the copper does most of
  the work.
- The script interpolates linearly in area between the rows. Over a convex
  curve, a chord errs high, which is the safe side.
- The same document's §7.4 Thermal Information gives RθJA 61.6 °C/W for the
  DCY (SOT-223) package on its standard test board, which the datasheet does
  not describe. It is shown below as an **optimistic bound** only.
- The same document gives LM1117I-3.3 Iq 15 mA max over −40…125 °C
  (Vin ≤ 15 V). The script uses it as a sensitivity case.

## Load inventory [board]

The script found these on the routed board:

```
[board] 3V3D loads: 26 pads on 20 parts (U_REG included)
[board] 74HC packages on 3V3D: U_IN1 74HC165, U_SR1 74HC595, U_SR2 74HC595, U_SR3 74HC595, U_SR4 74HC595, U_SR5 74HC595
[board] key pull-ups 3V3D -> key -> GND: R14, R15, R16, R17 (10k)
[board] no DC load: C17, C18, C19, C20, C21, C22, C6, C_LDO_T, TP6
[board] LEDs on 74HC595 outputs: 15 (D1-D15), series resistors 15, values ['1k']
```

Each LED hangs on a 595 output through 1 kΩ to GND (`LEDn → R → LEDn_A → D →
GND`), so its current comes out of 3V3D. A pressed key pulls its 10 kΩ to
GND. The 595 outputs that drive the mux select and enable lines through 1 kΩ
into CMOS inputs carry no steady current while both rails are up [assumed].

## Current per load

| load | typ | worst | tag |
|---|---|---|---|
| LEDs lit | 8 of 15 | 15 of 15 | [assumed]: no firmware LED duty exists yet |
| LED Vf | 2.0 V | 1.8 V | [assumed]: the colours and parts are not chosen; a lower Vf means more current |
| LED current | (3.3 − Vf) / 1 kΩ | same | 1 kΩ [board]; the 595's output drop is ignored, which overstates the current |
| keys pressed | 0 | all 4 | [assumed] |
| 74HC static ICC | 0 | 0.08 mA per package | [assumed]: order of magnitude, no datasheet read; 0.48 mA in total |
| Iq (into GND, from Vin) | 5 mA | 10 mA | 5 mA [listing] LCSC; 10 mA [datasheet] Slkor's max at Vin = Vout + 1.25 V, so its use at 12 V is [assumed] |
| Vin | 12.0 V | 12.6 V | [assumed]: the bus rail at nominal and at its upper tolerance; D_P12's drop is ignored, which overstates P |

```
typ   LEDs 8 of 15 lit at Vf 2.0 V: 10.40 mA; keys down 0 x 0.33 mA: 0.00 mA; 74HC 6 x 0.00 mA: 0.00 mA; I_load 10.40 mA
worst LEDs 15 of 15 lit at Vf 1.8 V: 22.50 mA; keys down 4 x 0.33 mA: 1.32 mA; 74HC 6 x 0.08 mA: 0.48 mA; I_load 24.30 mA
```

## Dissipation

P = (Vin − Vout) × I_load + Vin × Iq (TI SNOS412Q eq. 3).

```
typ   Vin 12.0 V, Iq 5.0 mA: P = (Vin - Vout) x I_load + Vin x Iq = 0.150 W
typ   of it Vin x Iq: 0.060 W (40 %)
worst Vin 12.6 V, Iq 10.0 mA: P = (Vin - Vout) x I_load + Vin x Iq = 0.352 W
worst of it Vin x Iq: 0.126 W (36 %)
worst with Iq 15 mA: P = 0.415 W
Slkor SOT-223 maximum power dissipation 0.600 W: worst 0.352 W, with Iq 15 mA 0.415 W
```

Iq alone is more than a third of the dissipation, before a single LED is lit.
Both worst cases stay below Slkor's 600 mW package maximum.

## Thermal resistance

**Before.** The 3V3D B.Cu copper touching the tab was the tab pad and the
0.5 mm track to the nearest via:

```
before 3V3D B.Cu copper touching the tab: 14.1 mm2 (0.022 in2; 14.1 mm2 within 10 mm of the tab centre; the tab pad alone 7.38 mm2) -> RthJA 133.7 C/W (TI Table 9-2, interpolated)
```

Slkor's own figure, 150 °C/W with the copper unstated, sits in the same
range. Both are estimates. Neither is this board.

**The first area (2026-10-01).** `REG_COPPER` was two fixed rectangles, an
L at the regulator's old spot (61.62, 54.98): x 51.9..66.1 × y 39.0..50.8
and x 60.2..66.1 × y 50.8..61.0. On that board:

```
before 3V3D B.Cu copper touching the tab: 238.7 mm2 (0.370 in2; 116.8 mm2 within 10 mm of the tab centre; the tab pad alone 7.38 mm2) -> RthJA 81.3 C/W (TI Table 9-2, interpolated)
```

**After (2026-10-08).** The 9 mm panel pass moved J_PWR to the bottom-left
corner and U_REG with it, to (21.12, 92.98). The area now follows the placed
regulator: `route.reg_copper()` takes the largest rectangle holding the tab,
plus the partner rectangle that overlaps it by at least the tab's short
side and enlarges the union most. Both stay inside a ±15 mm square around
the tab centre (`REG_WIN_MM` in `route.py` is its half-size, not a radius)
and clear of:

- foreign B.Cu pads (+0.2 mm);
- back courtyards and the module shadow;
- the stitching's vias and tracks;
- U_REG's pin column.

On this board it is (17.42, 74.83, 24.82, 94.83) and
(6.22, 76.83, 35.22, 86.23), 351.0 mm² in one outline. The two rectangles
cross: the committed zone outline has 12 vertices, a plus sign. The tab
centre is (21.12, 89.83); the zone reaches 14.9 mm from it in x and 15.0 mm
in y, so it fills the ±15 mm square along both axes while its far corners
lie up to 19.77 mm away.

How it is built:

- The router keeps every other net's B.Cu copper and vias off the area.
- After routing, the area becomes a 3V3D zone on B.Cu.
- The zone connects solidly to the tab (`ZONE_CONNECTION_FULL`, no thermal
  spokes) and keeps 0.2 mm clearance.
- `route_check.py` step `reg_copper` gates the filled copper on the tab at
  ≥ 200 mm²:

```
9. reg_copper U_REG.2 [3V3D] tab 7.38 mm2: 351.0 mm2 of filled 3V3D B.Cu copper on it (min 200.0), 1 zone(s), 1 of 1 outline(s) touch it
```

The θJA lookup counts all 3V3D B.Cu copper on the tab: the 351.0 mm² zone
plus the 3V3D tracks that leave it on B.Cu.

```
after  3V3D B.Cu copper touching the tab: 381.6 mm2 (0.591 in2; 161.0 mm2 within 10 mm of the tab centre; the tab pad alone 7.38 mm2) -> RthJA 73.4 C/W (TI Table 9-2, interpolated)
```

The area is a cross (a 29.0 × 9.4 mm strip across a 7.4 × 20.0 mm column),
not TI's compact square. More than half of it (all but 161.0 of 381.6 mm²) lies more than
10 mm from the tab centre, and that far part spreads heat less well than
TI's pattern does. Two things push the other way:

- the board is 4-layer, with the In1 GND plane under B.Cu;
- the board is 300.8 × 110 mm.

Neither effect is quantified. TI's 61.6 °C/W bound shows how far a
multilayer board could take it.

## Junction temperature

Tj = Ta + θJA × P. Ta is the air in the case [assumed: 40 and 50 °C]. The
limit is 125 °C [datasheet: Slkor's operating junction range; the LCSC
listing agrees].

The loads are those of today's board (15 LEDs) in every row, the "before"
rows included.

| | Ta | P typ 0.150 W | P worst 0.352 W | worst with Iq 15 mA, 0.415 W |
|---|---|---|---|---|
| before (no area), 133.7 °C/W | 40 °C | 60.1 °C | 87.1 °C | 95.5 °C |
| before (no area), 133.7 °C/W | 50 °C | 70.1 °C | 97.1 °C | 105.5 °C |
| Slkor 150.0 °C/W | 40 °C | 62.5 °C | 92.8 °C | — |
| Slkor 150.0 °C/W | 50 °C | 72.5 °C | 102.8 °C | — |
| first area (2026-10-01), 81.3 °C/W | 40 °C | 52.2 °C | 68.6 °C | 73.7 °C |
| first area (2026-10-01), 81.3 °C/W | 50 °C | 62.2 °C | 78.6 °C | 83.7 °C |
| **after, 73.4 °C/W** | 40 °C | 51.0 °C | 65.8 °C | 70.5 °C |
| **after, 73.4 °C/W** | 50 °C | 61.0 °C | **75.8 °C** | 80.5 °C |
| TI §7.4 61.6 °C/W (optimistic bound) | 40 °C | 49.2 °C | 61.7 °C | — |
| TI §7.4 61.6 °C/W (optimistic bound) | 50 °C | 59.2 °C | 71.7 °C | — |

**Verdict.** With the copper area, the worst case at Ta 50 °C is 75.8 °C,
49.2 K below the 125 °C limit. Without any area, the margin would be 27.9 K
with the TI proxy and 22.2 K with Slkor's 150 °C/W. The first, hand-drawn
area gave 46.4 K. Every margin depends on the assumptions marked above.

```
after  Ta 50 C worst P 0.352 W: Tj 75.8 C (limit 125 C, margin 49.2 K)
before Ta 50 C worst P 0.352 W: Tj 97.1 C (limit 125 C, margin 27.9 K)
datasheet θJA 150.0 C/W (Slkor θJA, copper unstated), Ta 50 C: Tj typ 72.5 C, worst 102.8 C (margin 22.2 K)
datasheet θJA 61.6 C/W (TI §7.4 RθJA, optimistic bound), Ta 50 C: Tj typ 59.2 C, worst 71.7 C (margin 53.3 K)
```

(The first-area rows are the `before` lines of a run with
`--before <824c9269's board>`. The no-area rows are those of a run with
`--before <6203bc7d's board>`.)

## What would change the verdict

**LED colours (Vf).** Each LED draws (3.3 V − Vf) / 1 kΩ, so a 1.8 V LED
draws five times what a 3 V one does:

```
one LED at Vf 1.8 V through 1k: 1.50 mA
one LED at Vf 2.0 V through 1k: 1.30 mA
one LED at Vf 3.0 V through 1k: 0.30 mA
```

The worst case assumes every LED is the lowest-Vf kind. A 3 V LED would also
be dim on a 3.3 V rail.

**Firmware LED duty.** The worst case has all 15 LEDs lit. Multiplexing or
PWM dimming scales the LED term (22.5 mA) directly.

**A hotter case.** Tj rises one for one with Ta. The worst case reaches the
limit at Ta 99.2 °C with today's area, at Ta 96.4 °C with the first area and
at Ta 77.9 °C with none. These come from the printed lines
`worst case reaches 125 C at Ta ...`.

**Iq.** A part at TI's LM1117I limit of 15 mA raises the worst case to
0.415 W, which adds 4.6 K with today's area and 8.4 K with none.

**Vin.** A rail above 12.6 V raises both terms. D_P12's forward drop lowers
Vin and is ignored here.

**The copper area.** It is gated at ≥ 200 mm². Since 2026-10-08 it
follows the placed regulator. A later placement that crowds U_REG shrinks
it, and `reg_copper` goes red before it falls below 200 mm² unseen.

None of this is measured on hardware. A thermocouple or IR reading of U_REG's
tab at bring-up, all LEDs lit, would replace the θJA estimate with a number.
