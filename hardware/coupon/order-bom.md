# Coupon parts order

**Both orders are placed. Boards: JLCPCB, 2026-09-02, $22.02. Parts: Reichelt,
2026-09-03, 14.19 € + 5.95 € shipping.** This file stays as the record of what
was bought and why, and as the list to repeat from if a second board gets
populated.

What to buy to populate one coupon. The board itself was ordered from JLCPCB
on 2026-09-02 (5 pieces, bare boards, no assembly), so everything here is
hand-soldered. Derived from `proof/review.md` Section 4, which stays the
authority on what the design contains — this file only adds where to get it
and how much of it.

Prices and stock checked 2026-09-03 at reichelt.de, incl. 19 % VAT. They will
drift; the article numbers are the durable part.

## One supplier: Reichelt

Everything that has to be bought fits in a single domestic order — no customs,
no second shipping fee. **14.19 € + 5.95 € shipping**, in stock, 1–2 working
days. (That total is the cart's own figure, checked 2026-09-03 with all
thirteen lines in it; the per-line sums below add to 14.21 € because the
tiered resistor prices round differently line by line.)

| qty | part | Reichelt no. | needed | each | sum |
|---:|---|---|---:|---:|---:|
| 50 | Resistor 0805, 0 Ω, 1 % | `SMD-0805 0,00` | 16 | 0.015 € | 0.75 € |
| 25 | Resistor 0805, 1.0 kΩ, 1 % | `SMD-0805 1,00K` | 10 | 0.015 € | 0.38 € |
| 25 | Resistor 0805, 10 kΩ, 1 % | `SMD-0805 10,0K` | 5 | 0.015 € | 0.38 € |
| 25 | MLCC 0805, 100 nF, 50 V, X7R | `CL21B104KBCNNNC` | 5 | 0.05 € | 1.25 € |
| 10 | MLCC 0805, 10 µF, **25 V**, X5R | `CL21A106KAYNNNE` | 4 | 0.13 € | 1.30 € |
| 20 | LED 0805 green, 500 mcd | `0805G3C-KHC-B` | 8 | 0.13 € | 2.60 € |
| 4 | 74HC595, SO-16 | `74HC 595D NXP` | 2 | 0.24 € | 0.96 € |
| 2 | 74HC165, SO-16 | `74HC 165D NXP` | 1 | 0.25 € | 0.50 € |
| 2 | 74HC4051, SO-16 | `74HC 4051D NXP` | 1 | 0.35 € | 0.70 € |
| 2 | 74HC4067, SO-24 | `SMD HC 4067` | 1 | 0.83 € | 1.66 € |
| 4 | Female header 2×10, 2.54 mm, straight | `MPE 094-2-020` | 2 | 0.67 € | 2.68 € |
| 2 | Box header 10-pin, straight | `WSL 10G` | 1 | 0.15 € | 0.30 € |
| 5 | Tactile switch 6×6 mm, h 8 mm | `TASTER 9305` | 1 | 0.15 € | 0.75 € |

Order quantities are above the per-board need on purpose: 0805 parts get lost,
and the fab ships five boards while `README.md`'s rule is to populate a second
copy rather than rework the first.

### Two things to check rather than trust

- **The LED must be a bright type, not a cheap one.** `R_LED*` is 1 kΩ off the
  3V3 rail, so the LEDs run at roughly **1.2 mA**, not 20 mA. A 12 mcd@20 mA
  part lands near 0.7 mcd and is barely visible; the 500 mcd part above gives
  about 30 mcd at the same current. Any high-efficiency green 0805 will do —
  the brightness figure is what matters, not the article number.
- **`SMD HC 4067` is the right part — checked 2026-09-03, no longer an open
  question.** The board's footprint is `SOIC-24W` and the narrow SO-24 variant
  would not fit, and Reichelt's search listing states only "SO-24" without a
  width. The article page settles it: manufacturer part number
  **`CD74HC4067M`** (Texas Instruments), which is exactly what `netlist.py`
  names, and the `M` suffix is the wide body.

## Not bought, and why

- **The seven pots (RV1–RV7)** — 4 × 10 k and 3 × 20 k linear, **from stock**.
  They are right-angle parts rather than the vertical Alpha the footprint
  draws, which is electrically irrelevant (the resistance is the device under
  test, the package is not) but needs three mechanical checks. See
  "Right-angle pots" below.
- **`J_AUDIO`, the 3.5 mm jack** — left unpopulated for now. It serves **none
  of the eight measurement points** in `proof/review.md` Section 1, the
  measurement rig is mono anyway (see `netlist.py`'s comment on the part), and
  the left channel already reaches a probe pad at `TP_AUDIO_L`. Reichelt does
  not carry the CUI SJ1 series and has no part with this pad pattern; a
  Thonkiconn does not substitute (measured: its three pins sit in a straight
  line at y 0 / 3.1 / 11.4 mm with 1.2–1.4 mm drills, against this footprint's
  scattered 2.0 mm holes at (0,0), (3.5,4.5) and (8.3,−5.0) — and it is mono,
  with no ring contact). If the jack is wanted later, buy the actual
  **SJ1-3513N** from Digikey or Mouser on the back of an order that already
  clears their free-shipping threshold.
- **`C_COM16` / `C_COM8`** — deliberately unpopulated, per requirement 2.
- **The ten test points and both solder jumpers** — bare pads; the jumpers are
  bridged with solder.

## Right-angle pots: what to check

Measured from the committed board and from the footprint, 2026-09-03.

- **Pin row:** three 1.0 mm holes at **2.5 mm pitch**. Alpha's 9 mm family uses
  that pitch in the right-angle version too, but measure — and check the legs
  are round enough for a 1.0 mm hole rather than flattened.
- **Support lugs:** two oval holes 1.1 × 1.8 mm, **7.5 mm to the side** of the
  pin row. Right-angle parts will not line up with these. Leave them empty; the
  pot hangs on three solder joints, so add a dab of hot glue under the body
  before anything gets turned.
- **Room for a body lying flat:** the pots sit in two rows, RV1–RV4 at
  y 50.5 mm and RV5–RV7 at y 65.0 mm, pin rows running east–west. That leaves
  **14.5 mm between the rows** and **15.0 mm from the lower row to the board's
  south edge** — so point the lower row's shafts south, over the edge, and the
  upper row's north. **RV4 is the tight one:** `JP_GND` sits 1.5 mm north of it
  and `R_SP11` 1.3 mm south, so its body rests on neighbours whichever way it
  faces.
- **Insulate.** A pot case is metal and here it lies flat across the analog
  section's parts and traces. Kapton under every body.
- **If short of pots:** the channel plan measures only **RV2 (10 k), RV4
  (20 k) and RV6 (10 k)**; the rest are companion channels. Populate those
  three first.
- **If the pots are log rather than linear:** still usable. Peak source
  impedance is R/4 regardless of taper — it just no longer occurs at mid
  travel but where the track splits 50/50, far to one side on a log part. Find
  that position with a meter instead of trusting the knob.
