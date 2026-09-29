# Rev A P1 — panel parts, cut file and grip test

**Date:** 2026-09-29
**Status:** approved in conversation (Bastian), to be reviewed as written
**Parent:** [`2026-09-28-rev-a-master-plan-design.md`](2026-09-28-rev-a-master-plan-design.md), sub-project P1
**Deadlines:** parts and acrylic ordered **Fri 9 Oct 2026**; panel freeze (H1)
**Fri 6 Nov 2026**

P1 is the mechanical track of Rev A: choose every panel-mounted part, export
a cut file from the panel generator, build a mechanical-only acrylic plate
with the real parts in it, test it by hand, and freeze the panel.

## 1. Parts — decided

Counted from the generator on 2026-09-28 (`gen_hw_panel.py`, positions with
`hw_class`): **14 big and 56 small knob positions = 70 pots** (`ATTACK_A/B`
and `STAGES_A/B` share a knob each), **4 keys** (`REC_A`, `REC_B`, `MODBTN`,
`SHIFTBTN`), **18 jacks**, **19 LEDs**, one SD slot.

| Part | Type | Qty (+spare) | Source |
|---|---|---|---|
| Pots | **Genuine Alpha 9 mm vertical, RD901F-40 family, T18 knurled shaft, B10K linear** — one type for all 70 positions; big and small differ by cap only | 70 (+7) | Thonk |
| Big caps | Davies 1900H clone, T18 (12 mm Ø × 16 mm) | 14 (+2) | Thonk |
| Small caps | Thonk Micro Knobs, T18 (7.7 mm Ø × 13.85 mm) | 56 (+6) | Thonk |
| Jacks | Thonkiconn PJ398SM mono | 18 (+2) | Thonk |
| Keys | Thonk low-profile push buttons, round cap | 4 (+2) | Thonk |
| LEDs | 3 mm flat-top THT | 19 (+6) | Thonk |

Rationale and rejected options, briefly:

- **Genuine Alpha, not a clone.** The coupon's Amazon RV09 clone missed the
  footprint's support-lug slots. The coupon footprint is KiCad's own
  `Potentiometer_Alpha_RD901F-40-00D_Single_Vertical`, i.e. drawn for the
  genuine part; the fit is *expected*, not yet confirmed (§6).
- **One pot type.** One footprint, one hole size, one order line, and the
  coupon's measured pot facts apply everywhere.
- **Rejected: ALPS RK09K with a capless 6 mm knurled plastic shaft** for the
  small knobs (Reichelt, 1.10 €). It would have saved about 90 € per set, but
  it has no threaded bushing — the pot hangs on its solder joints — and a
  different footprint. Bastian's decision: Alphas throughout.
- **Rejected: 8 mm square keycaps** (E-Switch TL1105 + cap). Not stocked by
  Thonk, and the switch needs a riser to reach the panel. The drawing changes
  from an 8 mm square to a round cap (§3).
- **No standoffs.** The 70 pots and 18 jacks, all nutted to the panel across
  the full 305 mm, hold the board — the usual Eurorack construction (Mutable,
  Befaco). The plate therefore gets no M3 holes. The envelope spec's
  2026-09-28 note calling for mid-board standoffs is corrected the same day
  as this spec.
- **LEDs without spacers.** They are soldered with the panel screwed on and
  pushed into their holes, so they sit flush by construction.
- **Centre detents: none for now.** The bipolar knobs (at least `CHOKE` and
  `PULL`; the full list is taken from the engine ranges in
  `engine/param_table.h` during planning) get the plain pot and a firmware
  dead zone at centre. Thonk's centre-detent B10K was out of stock on
  2026-09-28; if it is back by the Rev A order, those positions take it.
- **ENGINE** stays a plain pot that the firmware splits into five zones. No
  9 mm Alpha with five detents exists; the matching Alpha rotary switch
  (`SR1712F`) is hard to get.

**Order: one set now, the second in December.** One set is about £170 of
parts at Thonk's listed prices (pots ~£96 at the 50+ break, caps ~£53, the
rest ~£20), roughly **240 € delivered** once shipping, 19 % import VAT and the
carrier's fee are added — an estimate; the cart is read back before ordering.
The second set, for the second Rev A board, is ordered in the Rev A review
week (11–18 Dec) after the grip test has had its say. Thonk (UK) charges no
VAT at checkout for EU orders, and its FAQ warns that import charges fall due
on delivery.

## 2. The acrylic plate — decided

- **Clear acrylic GS, 3 mm**, laser-cut at formulor.de, with a **1:1 paper
  print of the real plate artwork** underneath (A3, holes cut out). The clear
  plate carries the parts; the print shows the legends, so the grip test also
  tests readability, with no engraving cost.
- **Price:** 20.49 € for 3 mm opaque black and 23.15 € for 3 mm fluorescent
  (Formulor quote 2026-09-28, neutral test file of the same size and hole
  count, shipping extra). Clear is not quoted yet; expected in that range.
- **Thickness:** 3 mm acrylic against the 2 mm aluminium of Rev A. The pot
  and jack threads are expected to take the extra millimetre (to check on the
  parts, §6).
- **One correction cut** (~20 €, a few days) is budgeted.
- **Order timing:** the acrylic is ordered on 9 Oct together with the parts,
  with hole sizes from the datasheets, not after the parts arrive. If a hole
  is wrong, the budgeted correction cut pays for it; waiting would cost two
  weeks of the grip test.

## 3. Generator changes

All in `host/vcv/res/gen_hw_panel.py`; nothing is drawn by hand.

1. ~~**Body radii follow the real caps.**~~ **Deferred 2026-09-29 (Bastian)**
   to the correction round after the grip test, before the freeze.
   `BODY_R["S"]` feeds the LEVEL band's x positions and the derivation of
   `Y_B2K`, so shrinking it to the Micro Knob's 3.85 moves controls; and the
   header's `kFfPadR` is also used by the big module's ENGINE latch. The cut
   file needs only hole sizes, which are independent, so the drawing gets one
   round, after the test, instead of two.
2. **Cut export.** A new output, `res/FireflowHW-cut.svg`, in Formulor's
   convention: pure blue (RGB 0,0,255) hairlines are cut, units are mm, one
   closed path per hole. Contents:

   | Feature | Size |
   |---|---|
   | Plate outline | 128.5 mm high; width **304.8 mm nominal — the real Eurorack panel is slightly narrower for fit; the exact value comes from Doepfer's A-100 mechanical spec** (assumption until read) |
   | Pot hole | 7.0 mm (Alpha M7 bushing; to confirm on the part) |
   | Jack hole | 6.0 mm |
   | Key hole | from the button datasheet |
   | LED hole | 3.1 mm |
   | SD slot | 11 × 6 mm, as the generator already places it |
   | Mounting | four slots at Doepfer's rail positions |

3. **Print export.** `res/FireflowHW-print.pdf` (or SVG): the plate artwork at
   exactly 1:1, with each hole's outline drawn so it can be cut out, and a
   100 mm scale bar to check the printer did not scale.
4. **Hole list.** `res/FireflowHW-holes.json`: one entry per hole —
   `{id, kind, x_mm, y_mm, d_mm}` (or `w_mm`/`h_mm` for the slot), origin at
   the plate's top-left, y down. This is the single source P4 places the
   board's parts from; the cut file and the print are drawn from the same
   list, never from separate coordinates.

## 4. The guard

A new guard next to `res/test_hw_panel.py`, wired into ctest the same way
(`CMakeLists.txt`, the block that already runs the two panel guards). It
asserts:

- every pot, jack, key and LED of the generated panel has **exactly one**
  hole in `FireflowHW-holes.json`, at its own coordinate (0.01 mm), and the
  cut file contains exactly those holes;
- no two holes overlap, and **at least 2.0 mm of material** stays between
  any two hole edges (acrylic cracks at thin webs);
- no hole enters the rail zones;
- the print export's scale bar measures 100 mm in the file.

Each assertion is shown red once (a shifted hole, a thin web) before it is
trusted.

**Found by the guard before anything was cut (2026-09-29):** the four jack-row
satellite LEDs (`MODBTN_L`, `SHIFTBTN_L`, `SYNC_L`, `CEIL_L`) sat at their
anchor's class radius + 1.5 mm, leaving 0.85–0.95 mm of material to the real
key and jack holes. They now sit at `SAT_D` = 6.7 mm (key hole 3.1 + web
2.0 + LED hole 1.55, rounded up). Whether the jack and key nuts cover them is
a §6 measurement.

## 5. Grip test and freeze

**Checklist**, results written to `docs/hardware/grip-test.md`:

1. Pinch two neighbouring knobs at once, in every group.
2. Turn each knob without brushing its neighbours' caps.
3. Patch all 18 jacks with real cables; note every knob, key, LED and legend
   a cable or plug covers.
4. Read every legend from playing distance, through the print.
5. Press all four keys; note reach and accidental presses.
6. Note anything that fails as *position*, *size* or *legend*; each gets a
   fix in the generator, not in a file.

**Freeze, Fri 6 Nov (H1):** the generator commit that passes the grip test is
tagged `panel-freeze-2026-11-06`. After that tag the panel geometry changes
only with Bastian's explicit approval.

## 6. What gets measured, not assumed

When the parts arrive, before the grip test (a caliper and the coupon, which
carries the RD901F footprint):

- the genuine Alpha's support lugs in the coupon's slots — confirms the P4
  footprint;
- pot bushing thread and length against 3 mm acrylic and 2 mm aluminium;
- jack and key thread against 3 mm;
- **the panel-to-board height** set by a pot and a jack seated on a board —
  the number the depth budget (15 mm module + 1.6 mm board + this gap, about
  10 mm expected) is still missing;
- whether the Alpha has an anti-rotation tab, and where.

Each result goes into `docs/hardware/grip-test.md` with the date.

## Out of scope

Rev A footprints and placement (P4), the aluminium plate's finish and maker
(P5), anything electrical (P2/P3).

## Sources

Thonk: [Alpha 9 mm](https://www.thonk.co.uk/shop/alpha-9mm-pots/),
[1900H T18](https://www.thonk.co.uk/shop/1900h-t18/),
[Micro Knobs](https://www.thonk.co.uk/shop/micro-knobs/),
[Thonkiconn](https://www.thonk.co.uk/shop/thonkiconn/),
[low-profile buttons](https://www.thonk.co.uk/shop/low-profile-push-buttons/),
[3 mm LEDs](https://www.thonk.co.uk/shop/new-style-flat-top-leds/),
[EU customs FAQ](https://www.thonk.co.uk/ufaqs/as-an-eu-customer-will-i-have-to-pay-customs-charges-and-vat-when-i-receive-my-thonk-parcel/).
Rejected alternative: [Reichelt RK09K113-LIN10K](https://www.reichelt.com/de/en/alps-rotary-potentiometer-linear-6-mm-mono-10-k-vertical-rk09k113-lin10k-p73815.html).
Laser: [formulor.de](https://www.formulor.de/so-funktionierts).
