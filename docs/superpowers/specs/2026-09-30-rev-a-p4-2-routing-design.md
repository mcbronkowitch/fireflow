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
  `place.py`, `route.py`, `route_check.py` and P3's `build.py` read from it.
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
6. **Reported per net:** length (sum of segments) and via count, in
   `Result.stats[name]`.

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
| Via cost, rounds | 8.0, 30 | spike; the only knobs the implementer may tune without Bastian |
| Audio × aggressor | 10.0 mm, same layer | §2.1 |
| L × R | 2.0 mm, same layer | §2.5 |
| SENSE length | ≤ 1.3 × MST | §2.6 |
| Module exemption | pad-group box + 2.54 mm | §4.2.2 |

### 4.4 Known list

`KNOWN_PANEL` in `route_check.py`, keyed per check like P4-1's.

- A key names the panel ids of the parts involved: for an unrouted connection
  the ids of the parts whose pads it joins, for a DRC item as in P4-1.
- The name rule is P4-1's strict one: every name must be one of the 18 jacks,
  the SONG clusters, or GATE_A_L/SOURCE_A and LVL_B_L/PAN_B, each pair only
  with its own partner.
- An unrouted connection between two non-panel pads is never listable.
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
     `drill_out_of_range`, `via_diameter`.
   - **Non-vacuity:** the report must contain its three `Found N` lines and
     `End of Report`. The number of violation blocks parsed must equal the
     `Found N DRC violations` figure. The board must carry at least one track.
   - P4-1's anchor (`unconnected_items > 0`) is dropped: P4-2 routes them away.
3. **rules_file**
   - kicad-cli runs on the committed pair (`kicad/reva.kicad_pcb` beside
     `kicad/reva.kicad_pro`) and on `out/`. The gated counts must be equal.
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
   - After the fill, In1.Cu GND and In2.Cu SM_3V3 are each exactly one filled
     island.
   - Every SMD pad on GND or SM_3V3 has its stitching via: the number of vias
     on the two plane nets is at least the number of SMD pads on them. The
     DRC's `unconnected_items` covers connectivity; this count keeps a
     missing stitch pass from hiding behind pads that reach the plane some
     other way.
9. **report**, never gated
   - Totals: track length and vias, per class.
   - Per victim, the nearest aggressor and where.
   - Per SENSE net, length and capacitance estimate.
   - Router runtime and rounds.
10. **render**: front and back into `hardware/reva/out/`.

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
  - Above 5 minutes wall time it is a finding for Bastian.
  - Coarsening the grid is not the implementer's call.
- **J_SD at 1.10 mm pitch.** It is probed first (§4.2.5).
- **Unroutable rules.**
  - If 10 mm audio or 1.3 × SENSE cannot be met, the run fails naming the net
    and the rule.
  - That is a finding for Bastian, not a threshold to move.
  - Via cost and round count are the only knobs the implementer may tune.
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
