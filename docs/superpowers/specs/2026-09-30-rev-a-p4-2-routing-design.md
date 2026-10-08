# FireFlow Rev A — P4-2: routing

**Date:** 2026-09-30
**Status:** approved in conversation (Bastian), section by section
**Parent:** the Rev A master plan
(`docs/superpowers/specs/2026-09-28-rev-a-master-plan-design.md`), sub-project
P4, the layout generator. Follows P4-1
(`docs/superpowers/specs/2026-09-29-rev-a-p4-1-placement-design.md`, merged
2026-09-30).

## 1. What P4-2 delivers

P4-2 turns P4-1's placed board into a routed, plane-filled, DRC-checked board:
`hardware/reva/kicad/reva.kicad_pcb`, byte-stable. It routes every net with our
own router (`hardware/gen/route.py`, chosen after the P4a spike) on 4 layers:
signals on F.Cu and B.Cu, GND on In1.Cu, SM_3V3 on In2.Cu. It adds the analog
rules the spike found missing (routing-spike findings 1 and 5), gates them with
checks that can each go red, and renders front and back on every run.

Like P4-1, P4-2 runs now, before the panel freeze (6 Nov). The panel pass is a
re-run, not hand work.

## 2. Decisions taken in brainstorming (Bastian, 2026-09-30)

1. **Audio clearance 10 mm**, the coupon's rule, measured outside the module's
   pad field. It is a design rule inherited from the coupon, not a measured
   coupling limit: the coupon's crosstalk probe measured ADC victims only.
   If the router cannot meet it, the run fails loudly and names the net; the
   number is then Bastian's call, not the implementer's.
2. **The rule applies per layer.** Copper on F.Cu and copper on B.Cu are
   separated by the In1.Cu GND plane. Across layers the audio nets must cross
   digital lines to reach the west jacks at all.
3. **Aggressors** (48 nets): the LED nets on both sides of the series resistor
   (`LED<n>`, `LED<n>_A`, n = 0..18), `SR_CLK`, `SR_DATA`, `SR_LATCH`,
   `SR_DIN`, and the SD bus (`SD_CK`, `SD_CMD`, `SD_D0`..`SD_D3`). Not
   aggressors: the mux address and enable lines (the coupon's crosstalk probe
   put them at the control floor) and the key lines (static).
4. **Victims** (4 nets): `OUT_L`, `OUT_R`, `IN_L`, `IN_R`.
5. **L/R spacing 2.0 mm** edge to edge, per layer, between `OUT_L` and `OUT_R`
   and between `IN_L` and `IN_R`. No guard track.
6. **SENSE nets are routed first, and each is at most 1.3 × its minimum
   spanning tree** (straight-line MST over its pads). Vias allowed. The coupon's
   "COM ≤ 15 mm, 0 vias, F.Cu only" does not carry over: on Rev A two or three
   muxes share one SENSE net (§3).
7. **No F.Cu copper under a pot body.** An Alpha 9 mm pot's two mounting tabs
   are the legs of a metal cover that sits flat on the board; mask alone
   (~20 µm) is not trusted against it. Under jack, LED and key bodies (plastic)
   F.Cu stays free to use.
8. **Known panel violations get a known list**, as in P4-1: a connection may
   stay unrouted, or a DRC item may stand, only if it touches the jack row,
   the SONG clusters or the two admitted LED/pot pairs. The list must be empty
   at the freeze.
9. **Approach A:** the rules live in the router; one run routes the whole
   board. Rejected: pre-routing the rule-bearing nets by scripted waypoints
   (they break on the panel pass, and the LED tracks need the pair rule
   anyway); Freerouting (decided against on 2026-09-29).

## 3. Measured facts this spec rests on

Probed on the committed P4-1 board (`hardware/reva/kicad/reva.kicad_pcb`,
2026-09-30):

- **189 nets** have two or more pads.
- **Audio routes are long.** OUT_L runs U_SM.B2 → J17.T, 89.2 mm straight;
  OUT_R U_SM.B1 → J18.T, 98.7 mm; IN_L and IN_R run from U_SM.B4/B3 to J1/J2 in
  the west, 140.3 and 141.9 mm. Along each straight line only 1–3 LED or
  SR_CLK pads lie within 10 mm, so the pads leave room; whether the router can
  keep the LED *tracks* away is what the first run shows.
- **At the module the rule cannot hold:** SR_CLK (U_SM.B8) sits 2.54–5.68 mm
  from the audio pins B1–B4. That is the module's pinout.
- **The module's pad field is two groups, not one box.**
  - The east header is 2 × 10 pads at x 177.63 / 180.17, at 2.54 mm pitch in
    two halves: y 46.17–56.33 and y 72.17–82.33, with a 15.84 mm gap. It
    carries B1–B10 and C1–C10. The lower half holds all four audio pins
    (x 180.17, y 74.71–82.33) and SR_CLK (B8, x 177.63, y 79.79).
  - The west pads are two rows of 5 at x 118.82–128.98, at y 48.98 and 79.52
    (the A and D pins).
  - Clustered at one pin pitch (2.54 mm), that is four groups. All 40 pads are
    THT (1.88 mm round).
  - One box around all 40 pads would be 61 × 36 mm and would exempt most of
    the module's footprint.
- **SENSE nets:**

  | Net | Pads | MST | Farthest pad from its module pin |
  |---|---|---|---|
  | SENSE_0 | 6 | 137.8 mm | 83.7 mm |
  | SENSE_1 | 6 | 245.0 mm | 166.4 mm |
  | SENSE_2 | 5 | 40.5 mm | 19.3 mm |
  | SENSE_3 | 5 | 111.9 mm | 58.5 mm |

  Node capacitance per `settle-budget.md`: 25–50 pF per mux, 15 pF stray
  (estimate). Track adds roughly 0.1 pF/mm (estimate, not measured), so
  SENSE_1's track is of the order of 25 pF.
- **J_SD pad pitch is 1.10 mm** (pads 1–8 at x 148.55–156.26). The router has
  never routed a pitch below the SOIC's 1.27 mm (spike known limit).
- **Supply nets** routed as tracks: `+12V` (6 pads), `-12V` (4), `3V3D` (26),
  plus `+12V_IN` and `-12V_IN` before the reverse diodes. `GND` and `SM_3V3`
  are planes.
- **kicad-cli's report** has the summary lines `** Found N DRC violations **`,
  `** Found N unconnected pads **`, `** Found N Footprint errors **` and
  `** End of Report **` (P4-1's report, 3444 lines).
- **KiCad 10's defaults differ from the board rules:** a fresh `BOARD()` has
  min track 0.2, min via 0.5, min clearance 0.0 mm; `kipcb.new_board` sets
  0.25 / 0.6 / 0.2. Only copper-to-edge (0.5 mm) matches. The committed
  `reva.kicad_pro` (P3's `build.py`) holds only `meta`.
- **Spike timing:** the P4a strip (97.1 × 116.1 mm, 47 nets) routed on 4 layers
  in 6.2 s at pitch 0.2 mm. The full board is about 3× the area with 189 nets.
- **Probed during execution (Task 1, 2026-09-30): full-board runtime.** The
  unmodified router on the whole placed board, alone on an idle machine:
  `nets 187 failed 18 [CLOCK, GATE_A, GATE_B, IN_L, IN_R, MOD1_A..MOD4_A,
  MOD1_B..MOD4_B, OUT_L, OUT_R, PITCH_A, PITCH_B, RESET] conflicts 2
  iterations 30 vias 425 length 8683.1 mm seconds 426.7`.
  - With `max_iters=1`: 20.2 s and 119 conflicts. The negotiation never
    converges, so all 30 rounds run.
  - A first measurement (464.9 s) overlapped other work on the machine and was
    discarded.
  - 187 nets, not 189: the two plane nets (GND, SM_3V3) are not routed.
- **Probed during execution (Task 1, 2026-09-30): J_SD terminals at pitch
  0.2.** Terminal cells per pad: SD_D2 (1) 2, SD_D3 (2) 2, SD_CMD (3) 2,
  SD_CK (5) 2, SD_D0 (7) 2, SD_D1 (8) 2. Minimum 2, none has 0, so §4.2.5's
  finer stub is not needed.
- **Probed during execution (Task 1, 2026-09-30): jack tip pads lie outside
  the grid.** All 18 failed nets are jack nets.
  - Each jack's tip pad T sits at y 118.92 (pad box y 117.86..119.98).
  - The router grid ends at y 118.5 (outline 119.25 − edge 0.5 − supply
    half-width 0.25).
  - `_terminal_cells` searches only ±2 cells (0.4 mm) around the rounded pad
    centre, so no terminal cell exists and all four audio nets are unrouted.
  - Answer: §4.2.6.
- **Probed during execution (Task 1, 2026-09-30): which `.kicad_pro` keys
  kicad-cli honours.** On the P4-1 board plus one 0.2 mm track on 3V3D:
  - A pro with `board.design_settings.rules` (plus `meta`) gives the same
    per-class DRC counts as the pro SaveBoard writes; `track_width` 1 with it,
    0 without.
  - Adding `net_settings` changes no count. The minimal key set is
    `board.design_settings.rules`: `min_track_width` 0.25,
    `min_via_diameter` 0.6, `min_through_hole_diameter` 0.3,
    `min_clearance` 0.2, `min_copper_edge_clearance` 0.5.
  - `clearance` (8) and `copper_edge_clearance` (18) counts were the same
    without the rules; only the counts were compared.
- **Probed during execution (Task 6, 2026-09-30): pair-rule cost.** With pad
  shapes, the A* sped up (500.9 s for 30 rounds without pair rules), pair
  rules, module zones and tiers, one profiled round took 487.0 s: 419 s
  marking pair clearances around routed copper, 17 s path search.
  `failed ['LED16', 'LED16_A', 'OUT_R']` (the jack zones of §4.2.2 answer
  that). Bastian decided on 2026-09-30 to speed the marking up (same radii,
  default path byte for byte) before the first full run.
- **Probed during execution (Task 6, 2026-09-30): the router does not
  converge.** With every rule in place: 12 conflicts after 30 rounds,
  784.5 s. Tuning via cost and the negotiation parameters gave 15–22
  conflicts (four runs); ripping up every net each round gave 17. Switching
  single rules off (conflicts only; the runs shared the CPU): no pair rule 9,
  no via keepouts 15, one tier 7 (IN_L and IN_R then fail). No single rule
  causes it. The conflicts sit in x 104–152, y 29–75, where the P4-1 placement
  put 146 pads on 2208 mm² (0.066 per mm², against 0.028 over the whole
  board): U_SR1–3, U_IN1, U_MUX7, part of U_MUX6, and the module's west pins,
  under ten pots. Bastian decided on 2026-09-30 to spread the placement there
  through P4-1's `OVERRIDES` (P4-1 spec §4.4), with the panel untouched.
- **Measured during execution (Tasks 6a and 6b, 2026-10-01):**
  - Placement was spread through `OVERRIDES` twice. Task 6a thinned the dense
    field (x 104–152, y 29–75) from 146 to 100 pads; the router's conflicts
    after 30 rounds went from 12 to 0. Task 6b, after §4.2.9, moved the
    plane-net pads clear of the pot bodies: U_SR3 to (84, 73) rotated 90°,
    U_IN1 8 mm south. The escape stubs under pot bodies (U_SR3.8 GND
    5.70 mm under RV22, C22.2 GND 3.36 mm under RV40) are gone, and C19.2,
    boxed in before, is stitched.
  - `place.py` reserves a decoupler's room only against the parts placed
    before its IC. With only U_IN1 moved, the run stopped with "no free place
    for C17"; U_SR1 is pinned by an override to work around it.
  - The offsets of U_SR5, R1–R3 and R12 ride on anchors that follow U_SR1 and
    U_IN1. Re-derive them whenever those two move.
  - U_REG moved 22.5 mm in Task 6b and now sits 17.4 mm from J_PWR. No check
    gates that distance.
  - The router leaves T-junctions: a branch can end on the middle of a host
    segment. kicad-cli flagged two of them as `track_dangling` once (Task 6b,
    run 2), and none of the 46 on the final board. `track_dangling` is gated.

## 4. Design

### 4.1 Pipeline and files

**place → route → check**, one process:

- `hardware/reva/place.py` is unchanged in what it places. `route.py` calls
  `place.build()` and routes that board in memory, so placement is always fresh.
- **New `hardware/reva/route.py`:**
  - builds the router's input from the placed board;
  - runs the router and writes the result back as tracks and vias;
  - stitches SMD pads on the plane nets to their planes (`stitch.py`,
    `netless_blocks=True`, routing-spike finding 8);
  - fills the zones and saves to `hardware/reva/out/reva-routed.kicad_pcb`;
  - runs `route_check.run()`, which renders front and back.
  - With `--write`, and only on a GREEN run, it copies the board to
    `hardware/reva/kicad/reva.kicad_pcb` and the renders to
    `docs/hardware/routing/`.
- **New `hardware/reva/route_check.py`:** the checks of §5, in P4-1's pattern.
- **New `hardware/reva/rules.py`:** every board rule in one place. That covers
  track widths per class, via, clearance, copper-to-edge, the pair rules, the
  aggressor and victim sets, the SENSE factor and the exemption margin.
  `place.py`, `route.py` and P3's `build.py` read from it.
  - The checks keep their own thresholds per the P4-1 constraint.
  - `route_check.py` does **not** import its limits from `rules.py` or
    `route.py`: it owns them, as `place_check.py` does.
- **The committed `reva.kicad_pro` carries the board rules.** `build.py`
  writes them, so the committed pair (`reva.kicad_pcb` + `reva.kicad_pro`) is
  judged by kicad-cli under the real rules. Which JSON keys kicad-cli honours
  is probed first (plan). The coupon's project file is untouched:
  `gen/check.write_project_file` keeps its default output.
- **Ownership of `reva.kicad_pcb` moves to `route.py`.**
  - `place.py --write` no longer copies to `kicad/`, and its renders go to
    `docs/hardware/placement/` as before.
  - `reva_place_guard` stops comparing the committed board and keeps every
    other assertion.
  - `test_build.py`'s exemption (`PLACED_BOARD`) names `route.py` as the
    owner.

### 4.2 What `hardware/gen/route.py` learns

Every addition defaults to off. The coupon and `hw_gen_route_guard` stay byte
for byte.

1. **Net groups and pair clearances.**
   - `add_net(name, half_width, terminals, group=None)`;
     `add_obstacle(net, layers, shape, group=None)`.
   - `pair_clearance(group_a, group_b, mm)` declares that copper of a net in
     group A keeps `mm` from copper of a net in group B on the same layer.
   - The rule covers tracks, vias and pads. THT pads are on every layer.
   - Implementation: a third array family, "pair occupancy". For each ordered
     pair (A, B) routed or static copper of A marks, for B's classes, the
     cells within `hw_own + mm + hw_B + 2s` (s for a via or an exact static
     shape). A cell marked for a net's group is forbidden to it.
   - It is **hard**, not negotiated: a pair cell is BLOCKED like foreign static
     copper, never a cost. The negotiation cannot trade it away.
   - Rev A's pairs:
     - victims × aggressors: 10.0 mm;
     - `OUT_L` × `OUT_R` and `IN_L` × `IN_R`: 2.0 mm (one group per net).
2. **Exemption zones.**
   - `pair_exempt(rect)`: inside the rectangle no pair marks are written and
     none are read.
   - Rev A's zones are one rectangle per module pad group: the bounding box of
     that group's pad centres, grown by 2.54 mm on every side (one pin pitch).
     Groups are clusters of U_SM pads whose centres lie within 2.54 mm of a
     neighbour's.
   - This yields four rectangles today: the east header's two halves and the
     two west rows.
   - The 2.54 mm is the controller's choice, approved by Bastian: it lets the
     audio pins and SR_CLK leave the header at all.
   - **Jack zones** (decided by Bastian 2026-09-30 after the Task 6 probe).
     Placement already breaks the 10 mm rule pad to pad at two jacks
     (edge to edge, pad bounding boxes):
     - J18.T (OUT_R): R34.1 (LED16) 5.02 mm, D17.2 (LED16_A, panel id
       CEIL_L) 6.35 mm, R34.2 6.39 mm. The pair marks then cover the whole
       tip pad, so OUT_R, LED16 and LED16_A cannot route at all.
     - J1.T (IN_L): D1.2 (LED0_A, panel id SHIFTBTN_L) 9.53 mm.
     - J17.T, J2.T and J2.TN have no aggressor pad within 10 mm.
   - For every victim pad outside U_SM with an aggressor pad closer than
     10.0 mm (edge to edge), one more exemption rectangle: the bounding box of
     that victim pad and those aggressor pads, grown by 2.54 mm. It is derived
     from the geometry, so it disappears once the panel pass moves the parts.
     Outside it the 10 mm rule is unchanged.
   - Each jack zone is a known panel item (§4.4), keyed by the panel ids of
     the victim's jack and the panel LED involved.
3. **Priority tiers.**
   - `add_net(..., tier=0)`. Nets route in ascending tier, then by the
     existing span order.
   - During negotiation a cell costs a net of tier t
     `(1 + history) × (1 + pres × (claims_up_to_t + 0.25 × claims_after_t))`,
     where `claims_up_to_t` counts claims by nets of tier ≤ t and
     `claims_after_t` claims by nets of a higher tier. An earlier-tier net
     therefore pushes later ones aside cheaply, and later ones pay full price
     to take its cells. With every net in one tier `claims_after_t` is always
     0 and the cost is today's, byte for byte.
   - Rev A: tier 0 = SENSE_0..3, tier 1 = the four audio nets, tier 2 =
     everything else.
4. **Per-layer keepouts** already exist as netless obstacles with a layer
   list. Rev A adds one F.Cu obstacle per pot: its body box from
   `gen/place.body_box`.
5. **A finer terminal stub** only if the J_SD probe (plan Task 1) shows that
   a J_SD pin is unreachable at pitch 0.2. Otherwise the pitch stays 0.2 mm
   everywhere.
6. **A pad-bounded terminal fallback.** Decided by Bastian 2026-09-30 after
   the Task 1 probe (jack tip pads lie outside the grid, §3).
   - A terminal may carry its pad's copper shape.
   - Only when the ±2-cell window around the pad centre finds no usable cell,
     the router takes the usable grid cell nearest the pad centre whose centre
     lies inside that pad shape.
   - The stub then lies inside the pad's own copper, so it adds no edge or
     clearance violation. *Corrected 2026-09-30 after Task 6's run 2:*
     kicad-cli checks the stub track on its own, so the 18 jack-tip stubs
     gave 18 `copper_edge_clearance` items. A terminal cell found by this
     fallback therefore gets no stub: the route ends at the cell, which lies
     inside the pad's copper, and that is the connection.
   - Off by default: a terminal without a shape behaves exactly as today; the
     coupon and `hw_gen_route_guard` stay byte for byte.
   - This makes the 18 jack nets, the audio nets included, routable before the
     panel pass.
7. **Reported per net:** length (sum of segments) and via count, in
   `Result.stats[name]`.
8. **Via-only keepouts** (added 2026-09-30 after Task 6's probes). An
   obstacle may block vias only (the via class), leaving tracks free.
   - Probed on Task 6's board: router vias 0.465 mm (SENSE_1, U_SM.A4) and
     0.523 / 0.677 mm (M6_CH6, MUX_S1, D10.1) edge to edge from GND
     through-hole pads starved their In1.Cu thermals. Three GND stitching vias
     cut an SM_3V3 fragment off at RV41.3 on In2.Cu.
   - Rev A: every through-hole pad of a plane net gets a via-only ring of
     1.0 mm (thermal gap 0.5 + zone clearance 0.5), edge to edge. The stitch
     search keeps the same distance (a `stitch_plane_pads` option, off by
     default for the coupon). Measured: 1.0 mm clears both starved thermals.
   - Off by default: the coupon and `hw_gen_route_guard` stay byte for byte.
9. **Stitch vias keep off the pot bodies** (Bastian, 2026-10-01, after
   Task 7's first run). `pot_keepout` (§5.7) found 15 stitching vias (11 GND,
   4 SM_3V3) with copper 0.05–3.34 mm inside a pot's body box. No router via
   and no F.Cu track was under a body. 11 of those SMD pads sit under a pot
   body themselves.
   - `stitch_plane_pads` takes boxes that via copper must stay out of. For
     a pad under a body, the via goes outside the box and a stitch track on
     the pad's own layer leads to it. The rule is unchanged.
   - A stub longer than the old 3.0 mm standoffs is checked along its whole
     length, not only at its midpoint.
   - Off by default: the coupon and `hw_gen_stitch_guard` stay byte for
     byte.
   - The stitch runs before the router, so its vias and tracks are router
     obstacles. The fix therefore re-routes the board. SENSE_1 (1.3185 × MST
     in Task 7's run, limit 1.3) is measured again after it. If it is still
     over, via cost is probed (§4.3; Bastian, 2026-10-01).
10. **U_REG's heat copper** (Bastian, 2026-10-01; no `gen/route.py` change).
    `hardware/reva/route.py` reserves a 3V3D area on B.Cu at U_REG's tab
    (`REG_COPPER`, a 3V3D-owned `add_obstacle` on B.Cu only) and fills it as
    a 3V3D zone after routing; see `docs/hardware/power-budget.md`.
    *Amended 2026-10-08 (panel 9 mm raster, Task 7c; Bastian):* the area
    was two fixed rectangles drawn for U_REG at (61.62, 54.98). The panel
    pass moved J_PWR and U_REG, which left them 46 mm from the tab and on
    foreign pads. The area now follows the placed regulator:
    `route.reg_copper()` takes the largest rectangle holding the tab, plus
    the partner rectangle that overlaps it by at least the tab's short side
    and enlarges the union most. Both stay within 15 mm of the tab centre
    and keep off every foreign B.Cu pad (+0.2 mm), every back courtyard, the
    module shadow, the stitching's vias and B.Cu tracks, and U_REG's pin
    column, which runs from its pins to the board edge. `route_check`'s
    200 mm² gate is unchanged.

The router measures nothing it gates. Every rule is judged by `route_check.py`
on the saved board.

### 4.3 Parameters (`rules.py`)

| Rule | Value | Source |
|---|---|---|
| Signal track | 0.25 mm | `kipcb.new_board` minimum, spike |
| Supply track (`+12V`, `-12V`, `+12V_IN`, `-12V_IN`, `3V3D`) | 0.5 mm | controller's choice; the spike used 0.4 mm on 2 layers |
| Via | 0.6 / 0.3 mm | `kipcb.add_via`, spike |
| Clearance | 0.2 mm | P4-1 |
| Copper to edge | 0.5 mm | P4-1, JLC |
| Grid pitch | 0.2 mm | spike |
| Via cost, rounds | 8.0, 60 | spike (30 rounds); 60 since 2026-10-08 (Task 7c: with the zone-edge rule the router converged in 44 rounds, 30 left 6 conflicts); the only knobs the implementer may tune without Bastian |
| Negotiation: `pres0`, `pres_mult`, `hist_inc` | 0.5, 1.6, 2.0 | router defaults (`hist_inc` 1.0); tunable too since 2026-09-30 (Bastian, after Task 6's run 2: 25 conflicts after 30 rounds, 898 s), at most 4 measured runs. `hist_inc` 2.0 since 2026-10-08 (Bastian, Task 7c): after the 9 mm panel pass the only setting that converged |
| Audio across a zone edge | 3.0 mm | §5.4; the router keeps it since 2026-10-08 (`gen/route.py` `pair_edge`, `rules.EDGE_MM`) |
| Audio × aggressor | 10.0 mm, same layer | §2.1 |
| L × R | 2.0 mm, same layer | §2.5 |
| SENSE length | ≤ 1.3 × MST | §2.6 |
| Module exemption | pad-group box + 2.54 mm | §4.2.2 |
| Jack zone | victim pad + aggressor pads within 10 mm, box + 2.54 mm | §4.2.2 |

### 4.4 Known list

`KNOWN_PANEL` in `route_check.py`, keyed per check like P4-1's.

- A key names the panel ids of the parts involved: for an unrouted connection
  the ids of the parts whose pads it joins, for a DRC item as in P4-1.
- The name rule is P4-1's strict one: every name must be one of the 18 jacks,
  the SONG clusters, or GATE_A_L/SOURCE_A and LVL_B_L/PAN_B, each pair only
  with its own partner.
- Two more pairs, for the jack zones of §4.2.2 only (Bastian, 2026-09-30):
  CEIL_L/OUT_R and SHIFTBTN_L/IN_L, each only with its own partner. They must
  be gone after the panel pass like every other entry.
  *Amended 2026-10-08 (Task 7c):* the panel pass (spec 2026-10-07 §5.3)
  renewed the two jack-zone pairs for the combined lamps: IN_L/SHIFTBTN_L
  and MODBTN_L/OUT_R stay admitted. Every other entry (the jack row, the
  SONG clusters, GATE_A_L/SOURCE_A, LVL_B_L/PAN_B) is gone and may not be
  listed again.
- An unrouted connection between two non-panel pads is never listable.
- An unconnected item on a plane net (GND, SM_3V3) is keyed by the pad that
  is cut off from its plane, found through the board's own connectivity, not
  by the partner kicad-cli pairs it with (Bastian, 2026-09-30). Task 6 found
  the SONG lamps' legs overlapping their pots' pin 3 (D3 on RV3.3, D16 on
  RV56.3, 0.43–0.63 mm), so SM_3V3 cannot reach them; kicad-cli paired
  RV56.3 with RV59.3 (RANGE_B), its nearest SM_3V3 pad. The keys are
  `unrouted SONG_A` and `unrouted SONG_B`.
- A listed item that no longer fails is red.
- `test_route.py` asserts the name rule.

## 5. Checks (`route_check.py`)

Every step measures the saved board, not what the router reports. Each gated
step has a sabotage and a `<step>_missing` sabotage, and each sabotage names
the step it turns red and a phrase that only that failure prints. Sabotages
edit the saved, routed board and never re-route, which keeps the guard fast.
A step that examined nothing is red.

1. **routed**
   - Router conflicts are 0.
   - Every connection is routed: kicad-cli `unconnected_items`, read per net
     and pad pair.
   - Unrouted items are allowed only if listed (§4.4).
   - The step prints the unrouted items net by net (closes spike finding 13).
2. **drc**
   - Gated classes: P4-1's eight (`courtyards_overlap`,
     `pth_inside_courtyard` — both front-only reported, not gated —
     `shorting_items`, `clearance`, `hole_clearance`, `hole_to_hole`,
     `copper_edge_clearance`, `items_not_allowed`), plus `tracks_crossing`,
     `track_dangling`, `via_dangling`, `track_width`, `annular_width`,
     `drill_out_of_range`, `via_diameter`, and `starved_thermal` (added by
     Bastian 2026-09-30: Task 6's run 2 had starved thermals at U_SM.A4 and
     D10.1 on GND, and the fragments they leave break §5.8's one island).
   - **Non-vacuity:** the report must contain its three `Found N` lines and
     `End of Report`. The number of violation blocks parsed must equal the
     `Found N DRC violations` figure. The board must carry at least one track.
   - P4-1's anchor (`unconnected_items > 0`) is dropped: P4-2 routes them away.
3. **rules_file**
   - kicad-cli runs on the committed pair (`kicad/reva.kicad_pcb` beside
     `kicad/reva.kicad_pro`) and on `out/`. The gated counts must be equal.
   - The routed board is committed, and the two readings are the same
     board. The step runs on `out/`'s board beside the committed
     `.kicad_pro`, and the guard's §6.2 (committed board equals a fresh run)
     makes that the committed pair.
   - The pro's min track, via, clearance and edge values are read and must
     equal the check's own constants.
4. **audio**
   - For each victim, on the same layer, outside the exemption zones: the
     distance from its copper (tracks, vias, pads) to any aggressor copper is
     at least 10.0 mm.
   - The measurement is its own code, not the router's.
   - The victim and aggressor sets are the check's own, written out as in
     §2.3–2.4. The check asserts that all 4 victims and all 48 aggressors
     exist on the board, so a renamed net cannot empty the rule.
   - The step prints the worst distance per victim and what it was measured
     against.
   - The check computes the module zones and the jack zones (§4.2.2) itself.
     Each jack zone it finds is a `found` item judged against `KNOWN_PANEL`,
     so a zone that disappears in the panel pass leaves a stale entry, which
     is red.
   - **Across a zone edge** (Bastian, 2026-10-01, after the Task 7 review).
     Take a victim–aggressor pair on the same layer where exactly one item
     lies inside an exemption zone. If that inside item is routed copper (a
     track or a via), the pair must be at least **3.0 mm** apart. If it is a
     pad, the pair is exempt. Pairs with both items inside a zone stay
     exempt.
     - Why: the check, like the router, used to drop all zone copper. An
       IN_L track then lay 0.706 mm from an SR_DATA via just inside the east
       module zone (Task 7's first board), and audio was green.
     - Why 3.0 mm: inside the zones the module pins themselves sit at a
       2.54 mm pitch.
     - On the committed board (re-routed 2026-10-01 with U_REG's copper
       area, `route.py --write`) the closest pairs are IN_R 5.562 mm from an
       SR_CLK track (the aggressor inside the zone), IN_L 5.904 mm (an IN_L
       track inside the zone at U_SM.B4 against an SR_DATA via outside),
       OUT_R 7.182 mm from an SR_DATA via @(175.75, 76.80) (on the board
       before, 7.528 mm from a LED16 via in the J18 zone), and OUT_L
       8.222 mm from an SR_CLK track.
     - The router is unchanged. This bound is the check's alone.
5. **lr**
   - `OUT_L`↔`OUT_R` and `IN_L`↔`IN_R`, same layer, outside the zones: at
     least 2.0 mm.
6. **sense**
   - Each SENSE net's copper length is at most 1.3 × the MST of its pads, both
     measured by the check.
   - The step prints the length, the factor, the via count and the estimated
     track capacitance (0.1 pF/mm, marked as an estimate).
7. **pot_keepout**
   - No F.Cu track or via copper overlaps a pot's body box.
   - The box comes from `gen/place.body_box`, which is geometry, not a
     threshold.
8. **planes**
   - After the fill, In1.Cu GND and In2.Cu SM_3V3 each have one main island,
     and every other filled outline touches a pad of its own net. A free
     island is red. *Amended by Bastian 2026-09-30:* the first rule, "exactly
     one island", cannot hold on this board. Task 6 found four GND fragments
     of 0.78–2.59 mm²: three between J_PWR's two pin rows (pads 0.84 mm
     apart) and a dead-end spoke at U_SM.A4. All are tied to GND pads, and
     none is caused by routing. The step reports every fragment with its
     place and area. `starved_thermal` (§5.2) catches weak connections.
   - Read the zone's layer through its layer set: `ZONE.GetLayerName()`
     returns "F.Cu" for the In1/In2 plane zones (probed 2026-09-30).
   - Every SMD pad on GND or SM_3V3 has its stitching via: the number of vias
     on the two plane nets is at least the number of SMD pads on them. The
     DRC's `unconnected_items` covers connectivity; this count keeps a
     missing stitch pass from hiding behind pads that reach the plane some
     other way.
9. **reg_copper** (*amendment, Bastian 2026-10-01*; report and render
   moved to 10 and 11)
   - The filled 3V3D copper on B.Cu whose outline overlaps U_REG's tab pad
     (an intersection with area) is at least 200 mm².
   - Sabotages: `reg_copper` (the fill cut to the tab grown by 3 mm) and
     `reg_copper_missing`.
   - On the committed board it reads 227.7 mm². See
     `docs/hardware/power-budget.md`.
10. **report**, never gated
   - Totals: track length and vias, per class.
   - Per victim, the nearest aggressor and where.
   - Per SENSE net, length and capacitance estimate.
   - Router runtime and rounds.
11. **render**: front and back into `hardware/reva/out/`.

**Freeze condition:** `KNOWN_PANEL` in `route_check.py` is empty, in addition
to P4-1's. `docs/hardware/grip-test.md` gets that sentence.

## 6. Guard and outputs

- **`hardware/reva/test_route.py`**, ctest `reva_route_guard`:
  1. Two runs in separate processes are byte-identical.
  2. The committed `reva.kicad_pcb` equals a fresh run.
  3. The fresh run is green.
  4. Every gated step is green on an unsabotaged reload.
  5. Every sabotage turns its step red with its own phrase.
  6. Every gated step has a sabotage and a `_missing` sabotage.
  7. The `KNOWN_PANEL` name rule holds.
- **Router unit tests** in `hardware/gen/test_route.py` (`hw_gen_route_guard`):
  - A pair rule keeps two nets apart on a small synthetic board.
  - An exemption zone lifts it.
  - Tiers change who yields.
  - Defaults reproduce the old output byte for byte.
- **Renders:** every run renders into `out/`; `--write` copies to
  `docs/hardware/routing/`. The PNGs are not byte-stable (P4-1); they are
  copied at milestones only.
- **Roadmap:** one dated entry with the numbers of the first green run.

## 7. Risks, and what the plan does about them

- **Runtime.** The first plan task measures a full-board run.
  - Above 10 minutes wall time it is a finding for Bastian (raised from 5 by
    Bastian on 2026-09-30 after the Task 1 probe, 426.7 s). The limit is
    re-measured after the router additions, and the guard's ctest timeout
    grows with it.
  - Coarsening the grid is not the implementer's call.
- **J_SD at 1.10 mm pitch.** It is probed first (§4.2.5).
- **Unroutable rules.**
  - If 10 mm audio or 1.3 × SENSE cannot be met, the run fails naming the net
    and the rule.
  - That is a finding for Bastian, not a threshold to move.
  - Via cost, round count and the negotiation parameters (§4.3) are the only
    knobs the implementer may tune.
- **The pair rule is hard.** A victim net that cannot reach its pad because
  aggressor copper sits within 10 mm of it outside the zones shows up as a
  routing failure, not as a DRC item. That is intended.
- **Panel pass.** It is a re-run. The known list empties or the build is red.

## 8. Not in P4-2

- Silkscreen, assembly sheet, Gerbers, JLC BOM and CPL: P4-3.
- Schematic `(path …)` links on the footprints: P4-3.
- Any panel change. `host/vcv/res/gen_hw_panel.py` and the hole list are
  untouched.
- A guard track between L and R (not chosen).
- Crosstalk measurement on the routed board: bring-up (master plan rule 7).
