# FireFlow Rev A — P4-1: placement

**Date:** 2026-09-29
**Status:** approved in conversation (Bastian), to be reviewed as written
**Parent:** the Rev A master plan
(`docs/superpowers/specs/2026-09-28-rev-a-master-plan-design.md`), sub-project
P4, the layout generator.

## 1. Why P4 is split, and what P4-1 delivers

P4 (placement, routing, DRC, JLC files) is too large for one spec. It is cut
into three, each with its own spec and plan (Bastian, 2026-09-29):

| Part | Output |
|---|---|
| **P4-1 placement** (this spec) | outline, every part placed, placement checks green or listed, renders |
| P4-2 routing | the whole board routed with `hardware/gen/route.py` on 4 layers, including the per-net-pair audio clearance (routing-spike finding 1) and the L/R spacing (finding 5) |
| P4-3 fabrication | silkscreen (master plan rule 3), assembly sheet, Gerbers, JLC BOM and CPL |

P4-1 starts now, before the panel freeze (6 Nov). It places from the current
P1 hole list, `host/vcv/res/FireflowHW-holes.json`. A later hole list is one
re-run. P4-1 also finds what the panel must change for the board to fit, while
the correction round the master plan budgets before the freeze is still open.

**P4-1 delivers** `hardware/reva/kicad/reva.kicad_pcb`: placed, unrouted,
byte-stable. It also delivers the checks of §5 and front and back renders of
every run.

## 2. Decisions taken in brainstorming

1. **The board is at most 110 mm tall**, centred between the rails:
   y 9.25–119.25 in panel coordinates (128.5 − 110 = 18.5, split evenly).
   110 mm is the common upper bound; above it, some rails collide
   ([Mod Wiggler, "Max PCB height for Eurorack?"](https://www.modwiggler.com/forum/viewtopic.php?t=203897),
   [elektrophon wiki, "Eurorack PCB Size"](https://github.com/spielhuus/elektrophon/wiki/Eurorack-PCB-Size)).
   Bastian chose the safe value over measuring a case or using Doepfer's
   112 mm.
2. **The panel adapts; the board does not bend.** The panel pass happens
   after the grip test, in the correction round before the freeze. The
   acrylic plate for the grip test is ordered unchanged; 1–3 mm do not matter
   for grip. P4-1 carries the known panel violations in a list (§5.3) until
   the pass lands.
3. **Placement is anchor plus search** (approach 1 of three). Every SMD part
   gets a target point from the netlist. The spike's square-spiral search
   takes the nearest free spot there. A short override table holds manual
   corrections, relative to the anchor. The rejected alternatives were a
   coupon-style hand table (about 90 coordinates to retype after every panel
   change) and an optimiser (more code, placements harder to explain).
4. **The SD socket protrudes through the panel slot.** That is usual and
   fine (Bastian).

## 3. Measured facts

Probed 2026-09-29 under KiCad 10.0.5's Python. The pad and body boxes come
from the footprints P3 assigns (`hardware/reva/parts.py`), placed so that
their hole point sits on the hole. The hole point is the spike's
`hole_point()`: the one F.Fab circle, or the F.CrtYd centre for the Thonk
key. A body is the footprint's F.Fab bounding box. The Thonk key has no
F.Fab, so its body is its F.CrtYd box. That is conservative: the box also
covers F.Fab lines that are not body, such as pin housings. The probe scripts
were scratch. The plan's first task re-derives these numbers through the real
code, and the guard keeps them.

**Pad extent around the hole, in y (mm):**

| Part | rot 0 | rot 90 | rot 180 | rot 270 |
|---|---|---|---|---|
| pot (Alpha RD901F) | −6.16..+6.16 | −1.62..+8.40 | −6.16..+6.16 | −8.40..+1.62 |
| jack (QingPu WQP-PJ398SM) | −7.40..+5.98 | −1.06..+1.06 | −5.98..+7.40 | −1.06..+1.06 |
| LED 3 mm | −0.90..+0.90 | −2.17..+2.17 | −0.90..+0.90 | −2.17..+2.17 |
| key (Thonk LP) | −3.50..+3.50 | −3.30..+3.30 | −3.50..+3.50 | −3.30..+3.30 |

**Pot rows** sit at y 14.5 (top), 34.0, 50.22, 53.0, 74.4, 79.0, 89.86,
95.0 and 97.0. **Jacks** sit at y 114.0, 11.5 mm apart centre to centre, with
gaps of 22.5 and 32.8 at the centre.

What follows from these:

- **Top pot row:**
  - Pins north (rot 270, the spike's choice) puts pads at y 6.10, inside the
    rail band.
  - Pins sideways (rot 0/180) reaches y 8.34.
  - Only pins south (rot 90) fits: pads from y 12.88.
  - The lower rows keep pins north.
- **Jacks:**
  - Rot 90/270 would keep the pads inside, but the rotated body box is
    12.58 mm wide against the 11.5 mm pitch, so neighbours collide.
  - Rot 0 is the only usable rotation, and it puts pads at y 119.98. That
    is 0.73 mm past the edge, and 1.23 mm past it with the 0.5 mm edge
    clearance.
  - The jack row must therefore sit at y ≤ 112.77. Keys, LEDs and the SD
    slot share its y (`JACK_Y`).
- **SONG_A_L / SONG_B_L** sit 7.2 mm under their top-row pots. With the pot
  pins south, pad and pin touch. The pad-edge gap to the SONG pot's pads, per
  downward shift of the lamp:

  | shift (mm) | 0 | 1.0 | 1.4 | 1.8 | 2.2 | 2.6 | 3.0 |
  |---|---|---|---|---|---|---|---|
  | LED rot 0 | 0.00 | 0.00 | 0.00 | 0.00 | 0.11 | 0.51 | 0.91 |
  | LED rot 90 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |

  **A 2.6 mm drop with the LED at rot 0 leaves 0.51 mm.** The lamp is one
  block with its caption (`caption_led_cluster()`), so the word SONG drops
  with it.
- **After both changes** (probed with the jack row at 112.7, and the SONG
  lamps down 1.4 and 1.8 mm):
  - No two bodies overlap.
  - No LED leg sits inside a foreign body.
  - Pads span y 11.00–118.69, which is 0.56 mm inside the bottom edge.
  - Five pot pins remain under an LED's body box: SOURCE_A under GATE_A_L,
    PAN_A under LVL_A_L, PAN_B under LVL_B_L, and both SONG pots under their
    lamps.
  - The SONG pair at a 2.6 mm drop was not probed with this full check.
- **Pot pins under an LED body box are probably no collision.** The LED dome
  sits at the plate and the pot pins at the board, but no height is measured.
  The grip test's panel-to-board gap and the parts in hand decide it.
- **The panel generator has little vertical room.** It has 0.75 mm of slack
  in the row chain (`gen_hw_panel.py`, comment above `Y_TOP`). A trial edit
  (`JACK_Y` 112.7, SONG cluster down 2.6) failed `test_hw_panel.py`:
  - frames cut their own captions
  - the jack row sat below its frame
  - the SONG caption left its frame
  - the pinned `JACK_Y == 114`

  The trial was reverted. The fix is a real re-layout of the vertical chain,
  which is Bastian's by-eye call (master plan rule 9).

**SD socket:** Yamaichi PJS008U-3000-0, the example socket in P3
(`2026-09-29-rev-a-p3-schematic-design.md`, the SD paragraph). Per
[LCSC C3177022](https://www.lcsc.com/product-detail/SD-Card-Connectors_Yamaichi-Electronics-PJS008U-3000-0_C3177022.html):

- through hole
- 14.18 mm above the board
- $2.07
- out of stock on 2026-09-29

A distributor summary gives 8 contacts at 2.2 mm terminal pitch in two
staggered rows. That is not yet read off the drawing. KiCad 10.0.5 ships no
vertical microSD footprint (`Connector_Card.pretty` holds only horizontal
ones), so P4-1 generates one.

## 4. Design

### 4.1 Outline and coordinates

- **Coordinates:** board coordinates are panel coordinates (mm, origin at
  the plate's top-left, y down), as in the spike. `kipcb.new_board(...,
  origin=)` takes the origin.
- **Outline:** x 2.0–302.8, y 9.25–119.25, a 300.8 × 110 mm rectangle. The
  x margin of 1.8 mm to the nominal panel edge is an assumption: it leaves
  neighbours room.
- **Stack-up:** 4 layers, with In1 GND and In2 SM_3V3 as planes, as in the
  spike's 4-layer run.
- **Mounting:** no mounting holes. The board hangs on the pot and jack nuts
  (P1 spec, "no standoffs").
- **Copper-to-edge clearance:** 0.5 mm, set in the board's design settings
  so that KiCad's DRC measures it too. This is an assumption, stricter than
  JLC's minimum.

### 4.2 Panel parts

- **Position:** every panel part sits on its hole. A part is matched by its
  `panel_id` against the hole list's `id` and `ids` aliases; `ATTACK_A`
  carries `STAGES_A`.
- **Rotation, by rule and not by table:**
  - pot: 270 (pins north); 90 (pins south) on the top pot row, the row with
    the smallest pot y.
  - jack: 0.
  - key: 0.
  - LED: the first of 0, 90, 180, 270 whose pads avoid every foreign body box
    and keep the pad clearance. If no rotation passes, the check fails; see
    §5.3 for the known cases.
- **SD socket (J_SD):** on the front, through hole, centred on the `SD`
  slot's centre. Its footprint comes from `sd_footprint.py`, which writes a
  `.kicad_mod` from the drawing's dimensions and cites the drawing's figure
  numbers. The drawing is read during the plan, and every dimension in the
  script names its source.

### 4.3 Back side: the fixed parts

Every SMD part goes on the back: JLC assembles one side (master plan
decision 3). Through-hole parts on the back are the module's sockets and
J_PWR.

- **Module (U_SM, `Daisy-Boards:DAISY_PATCH_SM`):**
  - **Position:** on the back, as close to the board centre as it fits. P2
    assumes the module sits near the middle so the longest mux COM run stays
    around 15 cm.
  - **Pins:** its through-hole pins must not enter a front-side body box, and
    they must keep the pad clearance to front pads. The search walks the
    spiral from the centre and takes the first spot that passes.
  - **Rotation:** the one that points its USB connector at the nearer long
    edge (top or bottom), so Bastian can flash over USB during bring-up.
  - **USB corridor:** a keep-free rectangle from the connector to the board
    edge, 12 mm wide. The width is an assumption for a USB-C plug's
    overmold.
  - **Shadow:** the module's outline is a keep-out for everything else on the
    back. The footprint has no courtyard (`kipcb.courtyard_boxes`
    docstring), so the plan probes which layer carries the module outline,
    or else measures it off the coupon's `SM_SHADOW`.
- **Power header (J_PWR, 2×5 shrouded IDC):**
  - **Position:** on the back, in the board half that does not hold
    OUT_L/OUT_R, at mid height. The first free spot on the spiral from
    there.
  - **Rotation:** the key faces the nearer long edge.
  - **Pin 1 (−12 V):** its side is recorded for P4-3's silkscreen marking.
- **Depth, reported and not gated:** module 15 mm (measured) + board 1.6 mm +
  panel-to-board gap (assumed 10 mm, until the grip test) against the
  Palette's 45.5 mm at the middle and 37.4 mm at the outermost HP.

### 4.4 Back side: the SMD parts

- **Order:** ICs first, most pins first, then decoupling, then every 2-pin
  part.
- **Anchors, all derived from the netlist and `panel-map.json`:**

  | Part | Anchor |
  |---|---|
  | 74HC4051 (U_MUX0–9) | centroid of the wiper pads of its pots (P3: "at the centre of its group") |
  | 74HC595 (U_SR1–5) | centroid of the pads it drives, followed through one 2-pin part (its LED resistors to its LEDs) |
  | 74HC165 (U_IN1) | centroid of its key pads, through one 2-pin part where there is one |
  | 100 nF at an IC | that IC's VCC pad; accepted only within 2.0 mm (the coupon's `check_layout` rule 5) |
  | LED resistor | the LED pad on its net |
  | D_P12, D_N12, U_REG, the regulator's capacitors, C_LDO_T | J_PWR |
  | C_SENSE0–3 | the module pad of its SENSE net |
  | C_SD1, C_SD2 | J_SD's VCC pad, 2.0 mm like decoupling |
  | everything else (test points, pull-ups) | centroid of the placed pads on its non-supply nets |

- **Search:** the spike's square spiral (`stripe._spiral`, `_place_smd`),
  moved to `hardware/gen/`. It walks rings nearest first and tries rotations
  0, 90, 180 and 270. The first position whose back courtyard clears
  everything blocked wins:
  - the outline inset by 1.0 mm (the spike's `EDGE_INSET`)
  - every through-hole pad box grown by 0.2 mm
  - the module shadow
  - J_PWR's courtyard
  - the USB corridor
  - SMD parts placed earlier

  Step and radius are set per class (spike values: ICs 0.5/30,
  decoupling 0.1/3, LED resistors 0.25/10). A part that finds no spot fails
  the build by name.
- **Overrides:** `OVERRIDES = {ref: (dx, dy, rot, reason)}`, empty at the
  start. The offset is relative to the part's anchor, so it survives the
  panel pass. An entry is added only after a render shows why, and its
  reason names that render.

## 5. Checks

### 5.1 Gated (the build fails)

1. **Hole over part** (master plan rule 1): every panel part's hole point
   lies within 0.01 mm of its hole. One part per hole, one hole per panel
   part.
2. **Edge:** every pad at least 0.5 mm inside the outline.
3. **Front bodies:**
   - no two body boxes overlap
   - no pad lies inside a foreign body box
   - exception: a pot pad under an LED body box is reported, per §5.2
4. **Module:** its pads miss every front body box; nothing on the back
   enters its shadow; the USB corridor is empty.
5. **Decoupling:** each 100 nF within 2.0 mm of its IC's VCC pad, pad to
   pad. C_SD1 likewise to J_SD.
6. **KiCad DRC** on the placed, unrouted board, gated classes:
   - `courtyards_overlap`
   - `pth_inside_courtyard`
   - `shorting_items`
   - `clearance`
   - `hole_clearance`
   - `hole_to_hole`
   - `copper_edge_clearance`
   - `items_not_allowed`

   `unconnected_items` is expected and not gated; routing is P4-2.
7. **Determinism:** two runs give byte-identical boards, and the committed
   board equals a fresh run.

### 5.2 Reported (printed every run, never gates)

- Pot pins under an LED body box, by name (five expected after the panel
  pass).
- The SD socket's 14.18 mm height against the assumed gap.
- The depth budget of §4.3.
- Ratsnest length per block (each mux group, the shift-register chain, the
  power block, the module) and in total, as P4-2's baseline.
- Each mux's distance to its anchor, each decoupling distance, and each
  override in force.

### 5.3 Known panel violations

`KNOWN_PANEL = {check: {item, ...}}` lists the violations the panel pass
will remove:

- the jack-row pads past the edge (check 2)
- the SONG lamps without a legal rotation (check 3)

A listed item that fails is printed as "known, waits for the panel pass". An
unlisted failure fails the build. **A listed item that no longer fails also
fails the build**, so the list cannot go stale: the panel pass has to remove
its entries in the same commit that fixes them. An empty `KNOWN_PANEL` is a
condition of the freeze, and it goes on the grip-test log's freeze checklist.

### 5.4 Each check can go red

As in P4a, every gated check has a sabotage mode that makes it fail on
purpose (`place.py --sabotage <check>`). A guard runs each mode once and
asserts it goes red. The mode also has a `*_missing` variant: the check's
input is absent, and the check must fail rather than pass on nothing. This
closes the harness gaps from routing-spike finding 13 on the placement side:

- an empty report that prints nothing
- a guard that was never exercised

## 6. Renders and output

- **Renders:** every run writes front and back PNGs to
  `hardware/reva/out/` (gitignored), through `pcb_proof.render`.
  - The implementer looks at them before the next iteration.
  - Bastian gets them at the end of every task (master plan rule 2).
  - Milestone renders are committed under `docs/hardware/placement/`.
- **Output:** `hardware/reva/kicad/reva.kicad_pcb` is committed. It sits next
  to the schematic project P3 generates, placed and unrouted. UUIDs are
  seeded (`kipcb.new_board`), so it is byte-stable.

## 7. Files and tests

- **`hardware/reva/place.py`:** builds the board. It reads P3's project
  (`build.py`), the hole list and `panel-map.json`. Flags: `--sabotage`,
  `--where`.
- **`hardware/reva/place_check.py`:** the checks of §5, and the report.
- **`hardware/reva/sd_footprint.py`:** writes the J_SD footprint into a
  project-local library, registered in `hardware/reva/kicad/fp-lib-table`.
- **`hardware/gen/place.py`:** the reusable parts, each with a test in
  `hardware/gen/test_place.py`:
  - the spiral search
  - hole-point
  - body boxes
  - the body-collision check
- **`hardware/reva/spike/`** is deleted in the first task, after
  `hole_point`, `_spiral` and `_place_smd` have moved to `hardware/gen/place.py`,
  as the routing report announced. The report keeps the measurements, and git keeps the
  code.
- **ctest:**
  - `reva_place_guard`: rebuilds, compares the board byte for byte, runs
    every gated check and every sabotage mode.
  - `hw_gen_place_guard`: the gen tests.

## 8. Assumptions, to be replaced by measurements

| Assumption | Replaced by |
|---|---|
| Panel-to-board gap 10 mm | the grip test (`docs/hardware/grip-test.md`) |
| Outline x margin 1.8 mm | nothing planned; revisit if a case disagrees |
| Copper-to-edge 0.5 mm | JLC's rule, checked in P4-3 |
| USB corridor 12 mm wide | the module and a cable in hand, at bring-up at the latest |
| Pot pins under an LED dome do not collide | the grip test, parts in hand |

## 9. Out of scope

- Routing, the audio clearance and the L/R spacing: P4-2.
- Silkscreen, reference designators, the assembly sheet, Gerbers, BOM and
  CPL: P4-3.
- The panel pass (`JACK_Y`, the SONG clusters, the vertical chain): a P1
  addendum after the grip test, decided by Bastian by eye. P4-1 hands it the
  numbers of §3.
- Buying the SD socket: hand-soldered, so it is bought with the second set
  of panel parts (master plan timeline, review week).
