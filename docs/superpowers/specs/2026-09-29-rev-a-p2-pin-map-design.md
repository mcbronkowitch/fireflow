# Rev A P2 — pin map and circuit blocks

**Date:** 2026-09-29
**Status:** approved in conversation (Bastian), to be reviewed as written
**Parent:** [`2026-09-28-rev-a-master-plan-design.md`](2026-09-28-rev-a-master-plan-design.md), sub-project P2
**Deadline:** Fri 16 Oct 2026 (unblocks P3 schematic and P6 firmware)

P2 fixes, for the one Rev A board, what every Patch Submodule pin does and
which circuit blocks sit around the module. It decides no positions (P4) and
draws no schematic (P3).

## 1. Decisions

1. **No MIDI on Rev A** (Bastian, 2026-09-29). The ten free-use pins are
   taken (SDMMC 6, chains 4); a MIDI input would need UART1 RX on A2, which
   is a pot sense pin, and would stretch the pot sweep from 48 to 64 ms. Clock
   comes in on the CLOCK jack; a MIDI-to-clock module fits the Palette's 1U
   row later. Rev B may revisit it if playing shows the gap.
2. **Ten 74HC4051 (8:1), not five 74HC4067 (16:1).** Every measured row
   favours the 4051 (§6), it sweeps faster, and it is cheaper at JLC:

   | | 74HC4051 | 74HC4067 |
   |---|---|---|
   | Settle, 10k pot at mid travel (coupon, measured) | ≤ 2.0 µs | ≤ 2.8 µs |
   | Shift after an idle, same impedance (measured) | 1× | 1.3–1.4× |
   | Crosstalk row 7 (measured) | smaller | larger |
   | Full sweep | 24 steps, 48 ms | 32 steps, 64 ms |
   | JLC, 2026-09-29 | C9386, Extended, ~$0.22 × 10 | C496123, Extended, $0.56 × 5 |

   10 × 8 = 80 channels: 70 pots, 2 calibration channels, 8 spare.
3. **Two 3.3 V rails.** Analog 3V3 is the module's A10 and feeds only pots
   and muxes (the ratiometric reference the coupon proved). Digital 3V3 comes
   from a local AMS1117-3.3 (C6186, Basic) on +12 V and feeds the shift
   registers and the LEDs, so LED current no longer flows through the pot
   reference.
4. **One solid ground plane**, no analog/digital split. The coupon's moat was
   bridged at bring-up and the split-versus-solid comparison was never
   measured; on a 305 mm board with jacks across its whole width a split is
   impractical. Analog parts are grouped by placement instead (P4).
5. **Reverse protection:** a series Schottky (SS14, C2480, Basic) on +12 V
   and on −12 V, plus the keyed shrouded header. The coupon had the key only.
6. **No front USB on Rev A.** Firmware goes in over the module's own USB with
   the module out of the case, or from the SD card — the reason the SD bus is
   4-bit.

## 2. Pin map — all 40 module pins

ADC channel numbers from `lib/libDaisy/src/daisy_patch_sm.cpp:10-21`
(`PIN_ADC_CTRL_n`); CV index to pin from the same table.

| Pin | Module function | Rev A net | Notes |
|---|---|---|---|
| A1 | −12 V in | −12 V after the reverse diode | |
| A2 | ADC_9 (UART1 RX) | `SENSE_0` | 3 muxes |
| A3 | ADC_10 (UART1 TX) | `SENSE_1` | 3 muxes |
| A4 | GND | GND | |
| A5 | +12 V in | +12 V after the reverse diode | |
| A6 | +5 V out | not connected | nothing on the board needs 5 V |
| A7 | GND | GND | |
| A8 | USB D− | not connected | module's own USB socket only |
| A9 | USB D+ | not connected | |
| A10 | +3V3 out | `A3V3` (analog 3V3) | pots and muxes only |
| B1 | Audio out R | `OUT_R` jack | |
| B2 | Audio out L | `OUT_L` jack | |
| B3 | Audio in R | `IN_R` jack | |
| B4 | Audio in L | `IN_L` jack | |
| B5 | Gate out 1 | `GATE_A` jack | |
| B6 | Gate out 2 | `GATE_B` jack | |
| B7 | GPIO (I2C1 SCL) | `SR_DATA` | 595 chain serial in |
| B8 | GPIO (I2C1 SDA) | `SR_CLK` | 595 and 165 clock |
| B9 | Gate in 2 | `RESET` jack | |
| B10 | Gate in 1 | `CLOCK` jack | |
| C1 | CV out 1 | `PITCH_A` jack | 0–5 V |
| C2 | CV_4 | `MOD4_A` jack | bipolar input stage on the module |
| C3 | CV_3 | `MOD3_A` jack | |
| C4 | CV_2 | `MOD2_A` jack | |
| C5 | CV_1 | `MOD1_A` jack | |
| C6 | CV_5 | `MOD1_B` jack | |
| C7 | CV_6 | `MOD2_B` jack | |
| C8 | CV_7 | `MOD3_B` jack | |
| C9 | CV_8 | `MOD4_B` jack | |
| C10 | CV out 2 | `PITCH_B` jack | 0–5 V |
| D1 | GPIO (SPI2 NSS) | `SR_LATCH` | 595 latch and 165 load |
| D2–D7 | SDMMC | SD card, 4-bit | exact data-line order from Electrosmith's reference |
| D8 | ADC_12 (SPI2 MISO) | `SENSE_3` | 2 muxes |
| D9 | ADC_11 (SPI2 MOSI) | `SENSE_2` | 2 muxes |
| D10 | GPIO (SPI2 SCK) | `SR_DIN` | 165 serial out |

The MOD-jack-to-CV assignment follows CV index (deck A on CV_1–4, deck B on
CV_5–8); P3 may swap jacks within a deck for routing, and the firmware table
follows the schematic, not the other way round.

**Pin balance:** every pin is used or deliberately unconnected (A6, A8, A9).
There is no spare pin; reserve lives in the chains (§4).

**One correction to the coupon's paperwork:** `hardware/coupon/scripts/netlist.py:100-101`
comments D8 as ADC_11 and D9 as ADC_12; libDaisy has it the other way round
(D9 = ADC_11, D8 = ADC_12). It never mattered on the coupon — both are test
points — but Rev A reads pots on both, so libDaisy is the authority here.

## 3. The pot scan

- **Muxes:** ten 74HC4051, powered from `A3V3`/GND. S0–S2 shared by all ten,
  driven from the 595 chain; each mux has its own active-low enable from the
  chain. Each mux's COM goes to one sense pin.
- **Distribution:** `SENSE_0` and `SENSE_1` carry three muxes each,
  `SENSE_2` and `SENSE_3` two each. At every step exactly one mux per sense
  pin is enabled, so a sweep is 3 muxes × 8 channels = 24 steps, one step per
  2 ms block = 48 ms (as measured for 8:1 on the coupon).
- **Regions:** each sense pin serves one contiguous region of the panel, and
  its muxes sit next to their pots. Which pots go to which mux is P4's
  placement result; the firmware table is keyed on (sense pin, mux, channel)
  (shell/README.md: part 2 must key on the sense pin).
- **Module position:** the module sits near the middle of the board so the
  longest mux-COM run stays around 15 cm. COM trace capacitance adds to the
  settle node (~1.5 pF/cm, `docs/hardware/settle-budget.md`); this is an
  assumption to verify at bring-up, not a measurement.
- **Pots:** 10k linear (Alpha RD901F-40), ends on `A3V3` and GND, wiper to a
  mux input.
- **Calibration:** one mux input tied to GND, one to `A3V3`, so the panel
  reads its own span (panel-scan spec §8).
- **Unused mux inputs** (8) tied to GND, never floating.
- **COM capacitor:** none fitted — the coupon verdict. It rests on arithmetic
  and was never falsified on hardware, so each sense line gets one
  unpopulated 0603 pad to GND near the module.
- **Series resistors:** 1 kΩ in each of the 13 address and enable lines, so a
  digital rail that rises before `A3V3` cannot push current through an
  unpowered mux's input clamps.
- **Power-up:** the enables are undefined until the first latch; at worst two
  pots on one sense pin meet through the mux (limited by the 10 kΩ pots,
  harmless). The firmware latches first.

## 4. The chains

Same four lines as the coupon: `SR_DATA` (B7), `SR_CLK` (B8), `SR_LATCH`
(D1), `SR_DIN` (D10). The 165 shares clock and latch, as on the coupon.

**Output chain: five 74HC595** (C5947, Basic), on digital 3V3, `~OE` to GND,
`~SRCLR` to digital 3V3. 40 outputs:

| Chip (from `SR_DATA`) | Outputs |
|---|---|
| U1 | A0, A1, A2, EN0–EN4 |
| U2 | EN5–EN9, LED0–LED2 |
| U3 | LED3–LED10 |
| U4 | LED11–LED18 |
| U5 | 8 spare, brought to a test-point row |

Which physical LED gets which LED index is assigned in P3/P4 by proximity;
the firmware table follows the schematic.

**Input chain: one 74HC165** (C5613, Extended), on digital 3V3. D0–D3 =
`REC_A`, `REC_B`, `MODBTN`, `SHIFTBTN`, each with a 10 kΩ pull-up to digital
3V3 and the key pulling to GND (Thonk low-profile momentary button, one pole
used). D4–D7 to GND (spare). `DS` and `~CE` to GND.

**LEDs:** 19 × 3 mm THT, each from a 595 output through a series resistor to
GND. 1 kΩ as on the coupon (~1.3 mA); the brightness is a by-eye decision at
bring-up.

**The LED crosstalk rule (coupon row 7):** an LED edge disturbed a conversion
by 13–15 counts for about 13 µs after the edge (`crosstalk-measured.md`).
Rev A needs no hardware for it: LED bits change **only** in the same latch as
the mux address, and the scan reads one block (~2 ms) after that latch. The
firmware (P6) must keep that rule, including for any LED dimming. The coupon
ran its scan images with the LEDs dark, so a scan with LEDs switching is a
bring-up check (§7).

## 5. Power and jacks

- **Header:** 10-pin shrouded, keyed Eurorack (±12 V, GND). No +5 V from the
  bus is needed.
- **Protection and bulk:** SS14 in series on +12 V and −12 V; 10 µF and
  100 nF on each rail after the diode; 100 nF at every chip.
- **Digital 3V3:** AMS1117-3.3 from +12 V; ~30 mA (19 LEDs, 5 × 595, 165)
  is ~0.26 W in the SOT-223; 10 µF in and out (datasheet values in P3).
- **Analog 3V3:** A10 only; ~23 mA of pots plus the muxes. The module's A10
  current rating is not written anywhere we have read (io-budget §6); P3
  reads the Patch SM datasheet for it.
- **No switching converter** anywhere on the board (the block-rate tone rule,
  io-budget §6).
- **Jacks:** 18 Thonkiconn, each 1:1 to its module pin (§2). Each jack's
  circuit (series resistors, protection, normalling) is taken from
  Electrosmith's published patch.Init() schematic, not designed afresh; where
  that schematic wires a pin straight, so does Rev A.
- **SD:** 4-bit on D2–D7, pull-ups and decoupling as Electrosmith's reference;
  the socket that reaches the panel is chosen in P4.

## 6. Coupon findings carried over

Measured on the coupon and binding for Rev A: 10k pots (`io-budget.md` §6,
`pots-measured.md`); no COM capacitor (coupon README); pots and muxes on the
module's 3V3 from A10; `SPEED_16CYCLES_5` with OVS 32, one mux step per 2 ms
block, write then read one block later (`scan-measured.md`); hysteresis
H = 16 counts (`scan-measured.md`); the 4051's settle, idle shift and row-7
advantage over the 4067 (`settle-measured.md`, `pots-measured.md`,
`crosstalk-measured.md`); the scan runs in the audio callback, never in the
foreground (+7.3 dB block-rate tone in the foreground, `io-budget.md`); the
chain pins B7/B8/D1/D10.

## 7. Checked at bring-up (P7), not assumed

- The scan with LEDs switching every step meets H = 16.
- Settle on the longest COM run (~15 cm) stays inside the sampling window.
- D8 and D9 read as ADC_12 and ADC_11 — never exercised on the coupon. P6
  can check it earlier on the coupon by feeding a known voltage into its D8/D9
  test points.
- A10's load (pots + muxes) against the module's rating.
- AMS1117 temperature at full LED load.
- LED brightness at 1 kΩ (by eye).

## Out of scope

Positions and the SD socket (P4); the schematic itself (P3); the firmware
tables (P6).
