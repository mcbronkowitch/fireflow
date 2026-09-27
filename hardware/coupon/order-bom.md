# Coupon parts order

**Both orders are placed. Boards: JLCPCB, 2026-09-02, $22.02. Parts: Reichelt,
2026-09-03, 14.19 € + 5.95 € shipping.** This file stays as the record of what
was bought and why, and as the list to repeat from if a second board gets
populated. **One line is still open:** the pots, which stopped being a stock
item on 2026-09-16 — see "The pots" below.

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
| 4 | Female header 2×10, 2.54 mm, straight — **cut to 2×5, see below** | `MPE 094-2-020` | 4 | 0.67 € | 2.68 € |
| 2 | Box header 10-pin, straight | `WSL 10G` | 1 | 0.15 € | 0.30 € |
| 5 | Tactile switch 6×6 mm, h 8 mm | `TASTER 9305` | 1 | 0.15 € | 0.75 € |

Order quantities are above the per-board need on purpose: 0805 parts get lost,
and the fab ships five boards while `README.md`'s rule is to populate a second
copy rather than rework the first.

### Two things to check rather than trust

- **The module sits on four 2×5 sockets, not two 2×10.** The landing
  pattern (`hardware/lib/DaisyKiCad/Daisy-Boards.pretty/DAISY_PATCH_SM.kicad_mod`,
  pad coordinates read 2026-09-15) is four separate 2×5 groups: banks A and
  D lie horizontally on the module's left edge with 25.5 mm between the two
  groups, banks B and C vertically on its right edge with 15.8 mm between
  them. No straight 2×10 strip fits either side. The four 2×10 strips above
  are still the right purchase: cut each one down to a 2×5 — the position
  under the cut is lost, so a 2×10 yields one intact 2×5 plus a 2×4 offcut —
  and the four cuts give exactly the four sockets the pattern needs. Buying
  four 2×5 female headers outright is the cleaner alternative. The asymmetry
  is also what makes a rotated module impossible to seat: horizontal groups
  on one edge, vertical on the other.

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

- **The seven pots (RV1–RV7)** — 4 × 10 k and 3 × 20 k linear. Were "from
  stock" until 2026-09-16, when the stock parts turned out to be bodies too
  large to reach the board without flying leads. They are now the **one open
  order line**, and not a Reichelt one: see "The pots" below.
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

## The pots: RV09 vertical, and what to check

The stock parts turned out to be oversized bodies that reach the board only on
flying leads, and that is the one thing this coupon may not be built with:
`docs/hardware/settle-budget.md` §6 puts the confirmation on a fabricated board
precisely because a wired node is an undefined multiple of the assumed 65 pF.
Run through `tools/settle_budget.py` with the wire modelled as extra node
capacitance at `COM` (1.5 pF/cm, the pessimistic placement), 5 cm still lands
inside the same 16.5-cycle sampling window and the same 0.16 block — but at
20 cm the 74HC4067 falls into the next window, and the crosstalk and noise
points stop meaning anything long before that. So the pots go on the board.

**What to buy: an RV09 9 mm vertical, 2.5 mm pitch, in 10 k and 20 k.** Checked
2026-09-16: Amazon `B01HO8EFXI` (10 k) and `B01HO8ELFA` (20 k), 6.99 € per 10
pieces each, which covers all seven positions with spares. Those are listing
claims, not measurements — the checks below are what settles it.

**Reichelt cannot supply the pair.** Its only 9 mm vertical part is the ALPS
`RK09K113-LIN10K` (manufacturer `RK09K1130A0H`, 1.10 €), and that line carries
10 k, 50 k and 100 k — **no 20 k**, so requirement 4 of `proof/review.md`
Section 1 ("10 k and 20 k side by side on one mux") cannot be met from there.
The package is electrically irrelevant either way; the resistance is the device
under test.

Board figures below read from the committed board and the footprint,
2026-09-03 and 2026-09-16.

- **Pin row:** three 1.0 mm holes at **2.5 mm pitch**, spanning 5.0 mm. The
  RV09 pitch matches, but check the legs are round enough for a 1.0 mm hole
  rather than flattened.
- **Support lugs — expect them not to fit.** The board draws the Alpha
  RD901F-40-00D pattern: two oval slots 1.1 × 1.8 mm, **7.5 mm to the side** of
  the pin row and **9.6 mm between centres**. Other 9 mm families use their own
  spacing; the ALPS `RK09K1130A0H` drawing, for one, gives 1.6 × 2.1 mm holes
  at 7.0 mm from the pin row and 10.6 mm across the pair — a miss on every
  count, whether that 10.6 is read outer-to-outer or centre-to-centre. Measure
  the lugs before pressing anything down. If they miss, clip them off; the pot
  then hangs on three solder joints, so add a dab of hot glue under the body
  before anything gets turned.
- **A standing body claims far less board area** — roughly the 9.8 mm square
  between the pin row and the lug slots, instead of a 20 mm body reaching out
  over its neighbours. Whether it still overhangs `JP_GND` or `R_SP11` at RV4
  is **unmeasured**; the case is metal either way, so keep Kapton under it
  until someone has looked.
- **If a right-angle part gets used after all**, the old clearance measurements
  still apply: the pots sit in two rows, RV1–RV4 at y 50.5 mm and RV5–RV7 at
  y 65.0 mm, pin rows running east–west, leaving **14.5 mm between the rows**
  and **15.0 mm from the lower row to the board's south edge** — point the
  lower row's shafts south, over the edge, and the upper row's north. **RV4 is
  the tight one:** `JP_GND` sits 1.5 mm north of it and `R_SP11` 1.3 mm south,
  so its body rests on neighbours whichever way it faces. A pot case is metal
  and would lie flat across the analog section's parts and traces, so Kapton
  under every body.
- **If short of pots:** the channel plan measures only **RV2 (10 k), RV4
  (20 k) and RV6 (10 k)**; the rest are companion channels. Populate those
  three first.
- **If the pots are log rather than linear:** still usable. Peak source
  impedance is R/4 regardless of taper — it just no longer occurs at mid
  travel but where the track splits 50/50, far to one side on a log part. Find
  that position with a meter instead of trusting the knob.
