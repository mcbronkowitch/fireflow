# 3V3D power budget and U_REG temperature (Rev A)

U_REG (AMS1117-3.3, LCSC C6186, SOT-223, on B.Cu) makes `3V3D` for the shift
registers, the key pull-ups and the 19 panel LEDs from +12 V. It is a linear
regulator, so it burns (Vin − 3.3 V) × I_load. Until 2026-10-01 the tab had
almost no copper around it. This page holds the load inventory, the
dissipation, the thermal resistance before and after the 3V3D copper area at
the tab, and the junction temperature.

Every number below is a line printed by

```
KIPY hardware/reva/power_budget.py [BOARD] [--before BOARD]
```

run on 2026-10-01 against the committed `hardware/reva/kicad/reva.kicad_pcb`.
"Before" is the board at `6203bc7d` (the P4-2 merge), extracted with
`git show 6203bc7d:hardware/reva/kicad/reva.kicad_pcb`. The script is report
only, never a gate. Re-run it after any change to the board or to one of its
assumptions, and copy the new lines here.

Every input is tagged in the script and in this page:

- **[board]**: measured on the board by the script.
- **[datasheet]**: quoted from a named document.
- **[assumed]**: a choice made here, with no source.

## Sources

- **AMS1117:** AMS's own PDF could not be fetched (TLS handshake failure on
  advanced-monolithic.com; LCSC's PDF link returned no text). The text read is
  Slkor's AMS1117 second-source datasheet, the one the P3 schematic spec
  already cites
  (https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/8122/AMS11173.3SOT223.pdf).
  It gives:
  - quiescent current 3 mA typ, 10 mA max at Vin = Vout + 1.25 V;
  - θJA (SOT-223) 150 °C/W in the absolute maximum table, with no copper area
    stated;
  - θJC (SOT-223) 15 °C/W;
  - operating junction range −40…125 °C;
  - thermal shutdown at 150 °C.
- **LCSC C6186 listing:** Iq 5 mA, operating temperature −40…+125 °C, input
  maximum 15 V.
- **Copper area against θJA:** TI LM1117 datasheet (SNOS412Q, January 2023),
  Table 9-2. It covers SOT-223 with the tab soldered to 1 oz top-side copper,
  in still air:

  | top-side copper (in²) | 0.0123 | 0.066 | 0.3 | 0.53 | 0.76 | 1.0 |
  |---|---|---|---|---|---|---|
  | θJA (°C/W) | 136 | 123 | 84 | 75 | 69 | 66 |

  This is a **proxy**. It is TI's die and TI's test board, not the AMS1117 and
  not this board. The two parts are pin compatible, and the copper does most
  of the work. The script interpolates linearly in area between the rows. The
  same TI document gives LM1117I-3.3 Iq 15 mA max over −40…125 °C, which the
  script uses as a sensitivity case.

## Load inventory [board]

The script found these on the routed board:

```
[board] 3V3D loads: 26 pads on 20 parts (U_REG included)
[board] 74HC packages on 3V3D: U_IN1 74HC165, U_SR1 74HC595, U_SR2 74HC595, U_SR3 74HC595, U_SR4 74HC595, U_SR5 74HC595
[board] key pull-ups 3V3D -> key -> GND: R14, R15, R16, R17 (10k)
[board] no DC load: C17, C18, C19, C20, C21, C22, C6, C_LDO_T, TP6
[board] LEDs on 74HC595 outputs: 19 (D1-D19), series resistors 19, values ['1k']
```

Each LED hangs on a 595 output through 1 kΩ to GND (`LEDn → R → LEDn_A → D →
GND`), so its current comes out of 3V3D. A pressed key pulls its 10 kΩ to
GND. The 595 outputs that drive the mux select and enable lines through 1 kΩ
into CMOS inputs carry no steady current while both rails are up [assumed].

## Current per load

| load | typ | worst | tag |
|---|---|---|---|
| LEDs lit | 10 of 19 | 19 of 19 | [assumed]: no firmware LED duty exists yet |
| LED Vf | 2.0 V | 1.8 V | [assumed]: the colours and parts are not chosen; a lower Vf means more current |
| LED current | (3.3 − Vf) / 1 kΩ | same | 1 kΩ [board]; the 595's output drop is ignored, which overstates the current |
| keys pressed | 0 | all 4 | [assumed] |
| 74HC static ICC | 0 | 0.08 mA per package | [assumed]: order of magnitude, no datasheet read; 0.48 mA in total |
| Iq (into GND, from Vin) | 5 mA | 10 mA | [datasheet] 5 mA from LCSC; 10 mA is Slkor's max, specified at Vin = Vout + 1.25 V, so its use at 12 V is [assumed] |
| Vin | 12.0 V | 12.6 V | [assumed]: the bus rail at nominal and at its upper tolerance; D_P12's drop is ignored, which overstates P |

```
typ   LEDs 10 of 19 lit at Vf 2.0 V: 13.00 mA; keys down 0 x 0.33 mA: 0.00 mA; 74HC 6 x 0.00 mA: 0.00 mA; I_load 13.00 mA
worst LEDs 19 of 19 lit at Vf 1.8 V: 28.50 mA; keys down 4 x 0.33 mA: 1.32 mA; 74HC 6 x 0.08 mA: 0.48 mA; I_load 30.30 mA
```

## Dissipation

P = (Vin − Vout) × I_load + Vin × Iq (TI SNOS412Q eq. 3).

```
typ   Vin 12.0 V, Iq 5.0 mA: P = (Vin - Vout) x I_load + Vin x Iq = 0.173 W
typ   of it Vin x Iq: 0.060 W (35 %)
worst Vin 12.6 V, Iq 10.0 mA: P = (Vin - Vout) x I_load + Vin x Iq = 0.408 W
worst of it Vin x Iq: 0.126 W (31 %)
worst with Iq 15 mA: P = 0.471 W
```

Iq alone accounts for about a third of the dissipation, before a single LED
is lit.

## Thermal resistance

**Before.** The 3V3D B.Cu copper touching the tab was the tab pad and the
0.5 mm track to the nearest via:

```
before 3V3D B.Cu copper touching the tab: 14.1 mm2 (0.022 in2; 14.1 mm2 within 10 mm of the tab centre; the tab pad alone 7.38 mm2) -> RthJA 134 C/W (TI Table 9-2, interpolated)
```

Slkor's own figure, 150 °C/W with the copper unstated, sits in the same
range. Both are estimates. Neither is this board.

**After.** `REG_COPPER` in `hardware/reva/route.py` is an L-shaped area. One
arm runs north of U_REG, from x 51.9 to 66.1 and y 39.0 to 50.8. The other is
the tab's own column, from x 60.2 to 66.1 and y 50.8 to 61.0. Together they
cover 227.7 mm². The router keeps every other net's B.Cu copper and vias off
the area. After routing it becomes a 3V3D zone on B.Cu with a solid
connection to the tab (`ZONE_CONNECTION_FULL`, no thermal spokes) and 0.2 mm
clearance. `route_check.py` step `reg_copper` gates the filled copper on the
tab at ≥ 200 mm²:

```
9. reg_copper U_REG.2 [3V3D] tab 7.38 mm2: 227.7 mm2 of filled 3V3D B.Cu copper on it (min 200.0), 1 zone(s), 1 of 1 outline(s) touch it
```

With the 3V3D tracks that leave the zone on B.Cu:

```
after  3V3D B.Cu copper touching the tab: 238.7 mm2 (0.370 in2; 116.8 mm2 within 10 mm of the tab centre; the tab pad alone 7.38 mm2) -> RthJA 81 C/W (TI Table 9-2, interpolated)
```

The area is an L, not TI's compact square, and about half of it (all but
116.8 of 238.7 mm²) lies more than 10 mm from the tab centre. The far half spreads heat less well than TI's pattern
does. Two things push the other way, and neither is quantified here:

- the board is 4-layer, with the In1 GND plane under B.Cu;
- the board is 300.8 × 110 mm.

## Junction temperature

Tj = Ta + θJA × P. Ta is the air in the case [assumed: 40 and 50 °C]. The limit
is 125 °C [datasheet: Slkor and LCSC, operating junction range].

| | Ta | P typ 0.173 W | P worst 0.408 W | worst with Iq 15 mA, 0.471 W |
|---|---|---|---|---|
| before, 134 °C/W | 40 °C | 63.1 °C | 94.5 °C | 102.9 °C |
| before, 134 °C/W | 50 °C | 73.1 °C | 104.5 °C | 112.9 °C |
| datasheet 150 °C/W | 40 °C | 66.0 °C | 101.2 °C | — |
| datasheet 150 °C/W | 50 °C | 76.0 °C | 111.2 °C | — |
| **after, 81 °C/W** | 40 °C | 54.1 °C | 73.1 °C | 78.3 °C |
| **after, 81 °C/W** | 50 °C | 64.1 °C | **83.1 °C** | 88.3 °C |

**Verdict.** With the copper area, the worst case at Ta 50 °C is 83.1 °C. That
leaves 41.9 K to the 125 °C limit, against 20.5 K before (TI proxy) or 13.8 K
(Slkor's 150 °C/W). Each margin depends on the assumptions marked above.

```
after  Ta 50 C worst P 0.408 W: Tj 83.1 C (limit 125 C, margin 41.9 K)
before Ta 50 C worst P 0.408 W: Tj 104.5 C (limit 125 C, margin 20.5 K)
datasheet θJA 150 C/W (copper unstated), Ta 50 C: Tj typ 76.0 C, worst 111.2 C (margin 13.8 K)
```

## What would change the verdict

- **LED colours (Vf).** The current per LED is (3.3 V − Vf) / 1 kΩ. A
  low-Vf LED draws five times what a 3 V one does:

  ```
  one LED at Vf 1.8 V through 1k: 1.50 mA
  one LED at Vf 2.0 V through 1k: 1.30 mA
  one LED at Vf 3.0 V through 1k: 0.30 mA
  ```

  The worst case assumes every LED is the lowest-Vf kind. A 3 V LED would
  also be dim on a 3.3 V rail.
- **Firmware LED duty.** All 19 lit is the worst case. Multiplexing or PWM
  dimming scales the LED term (28.5 mA) directly.
- **A hotter case.** Tj rises one for one with Ta. The worst case reaches the
  limit at Ta 91.9 °C after, and at Ta 70.5 °C before (the printed lines
  `worst case reaches 125 C at Ta ...`).
- **Iq.** A part at TI's LM1117I limit of 15 mA raises the worst case to
  0.471 W: +5.1 K after, +8.4 K before.
- **Vin.** A rail above 12.6 V raises both terms. D_P12's forward drop lowers
  Vin and is ignored here.
- **The copper area.** It is gated at ≥ 200 mm². If a later placement moves
  U_REG or its neighbours, `reg_copper` goes red before the area shrinks
  unseen.

None of this is measured on hardware. A thermocouple or IR reading of U_REG's
tab at bring-up, all LEDs lit, would replace the θJA estimate with a number.
