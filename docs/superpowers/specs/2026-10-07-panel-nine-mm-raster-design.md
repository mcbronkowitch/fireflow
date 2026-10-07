# Panel correction — the 9 mm raster

**Date:** 2026-10-07
**Phase:** Rev A P1 correction round (P1 spec `2026-09-29-rev-a-p1-panel-parts-design.md`
§5, "each failure gets a fix in the generator"), before the panel freeze
(`panel-freeze-2026-11-06`) and before the Rev A board is ordered.
**Trigger:** the acrylic plate arrived. Checklist items 1 and 2 of
`docs/hardware/grip-test.md` (pinch two neighbours, turn without brushing a
neighbour's cap) failed: the knobs stand too close to play. Item 3 (patching
all jacks) passed — the jack row was fine.
**Decision (Bastian, 2026-10-07):** no two caps closer than **9 mm, edge to
edge**. Spacing beats thematic grouping — form follows function. The Rev A
board is not ordered yet, so the board follows the plate.

This is also **the one panel pass** that P4-1 left its known panel violations
for (decided 2026-10-04): the jack row's height and the LED rework travel
with it, so the scripted chain to the board runs once, not three times.

## 1. Goal and scope

Every knob and every key on the 60 HP plate keeps at least 9.0 mm between cap
edges, measured with the real caps. The plate stays 60 HP, deck B stays
deck A mirrored. The jack row keeps its x positions and rises to
`JACK_Y` = 112.75 (§3.4). The LED inventory changes as in §3.3.

Done means: the generator places every knob from one cell table, a guard
proves the 9 mm rule (and has been seen red once), the hole list, cut file,
panel map, placed and routed Rev A board, generated firmware table and VCV
`FireflowHW` panel all come from the new positions, every gate along that
chain is green, and the board's `KNOWN_PANEL` exemption lists
(`place_check.py`, `route_check.py`) are **empty**.

**Not in scope:** jack x positions and pitch (patching passed the grip test),
the big VCV module (`Fireflow`), the engine, plate colours, key functions.
**How** the new LEDs light (§3.3 states what each one means) belongs to P6b's
LED law on the board and to the VCV host's light code; this pass gives them
LightIds, positions, holes and footprints.

## 2. What was measured

All numbers below were printed by a probe against today's `gen_hw_panel.py`,
not read off a drawing. Cap sizes are the parts actually bought (P1 §1):
Micro Knob 7.7 mm Ø (r 3.85), Davies 1900H 12 mm Ø (r 6.0), Thonk
low-profile key cap 6 mm Ø (r 3.0; P1 §6, "only the 6 mm cap passes the
6.2 mm hole").

**Today's plate:** 70 knob positions (14 big). The 13.0 mm pitch leaves
**5.3 mm** between small caps; that is also the median nearest-neighbour gap.
45 pairs are under 8 mm and **54 pairs under 9 mm**, involving 66 of the 70
knobs. `BODY_R["S"]` = 4.4 still describes the 8.8 mm cap the layout was
drawn with, not the 7.7 mm part (P1 §3.1, deferred to this round).

**The new raster** (§3), checked the same way: **no pair under 9.00 mm**.
Big beside small lands on exactly 9.00 (20 pairs); small beside small is
11.15 mm across and 12.43 mm down; median nearest-neighbour gap 10.27 mm.
Deck-to-centre seam 11.48 mm. Caps span x 4.70 .. 300.10 (4.5 mm to each
plate edge of the 304.4 mm plate); lowest cap edge y 101.00, 11.75 mm above the
jack row at its new height of 112.75 (§3.4).

Rejected on the numbers: keeping the current groups and only stretching them.
Today's knobs stand on seven to eight distinct heights; at a ≥ 16.7 mm
vertical pitch the plate holds five to six rows, so the groups would have to
be rebuilt anyway, just without a rule. Moving the jacks to widen the plate
(Bastian's fallback) is not needed.

## 3. The raster

**One rule:** every knob and key sits on a cell of one raster, and no two big
caps are orthogonal neighbours (diagonal is fine).

| | Value | Why |
|---|---|---|
| Column pitch | **18.85 mm** | big r 6.0 + small r 3.85 + 9.0 |
| Row pitch | **20.125 mm** | rows 14.5 → 95.0 in four steps |
| Rows | y = 14.500, 34.625, 54.750, 74.875, 95.000 | top row as today; bottom row puts a big cap's edge on 101.0 |
| Deck A columns | x = 10.70, 29.55, 48.40, 67.25, 86.10, 104.95 | column 1 = plate edge 0.2 + 4.5 + big r 6.0 |
| Deck B | x → 304.8 − x | mirror axis 152.4, as today |
| Centre | x = 152.4 + k · 18.85, k ∈ {−1, 0, +1}; row 1 at k ∈ {±0.5, ±1.5} | centred on 152.4, big caps on the centre line |

Two big caps orthogonally adjacent would need 21.0 mm and the raster gives
18.85 / 20.125, so the "no big neighbours" rule is load-bearing, not taste.

### 3.1 Deck A (column 1 = outer edge; deck B mirrored)

| Row | c1 | c2 | c3 | c4 | c5 | c6 |
|---|---|---|---|---|---|---|
| R1 | `ENGINE` | `STEPS` | `SONG` | `RATE` | `MELODY` | `REC` (key) |
| R2 | `SHAPE` | **`MOD`** | `SMOOTH` | `ATTACK`/`STAGES` | `DECAY` | `RES` |
| R3 | **`DENSITY`** | `RANGE` | `SUB` | `SOURCE` | **`FILT`** | `DEPTH` |
| R4 | `TUNE` | `DETUNE` | `FLUXRATE` | **`FLUX`** | `PAN` | `GRIT` |
| R5 | **`COLOR`** | — | `FLUXFB` | `LINK` | **`COMP`** | `REV_MIX` |

Bold = big cap. Groups stay contiguous blocks, no longer all rectangles:
ENG, SEQUENCE and CAPTURE share R1; MOTION is R2 c1–c3 + R3 c1–c2; VOICE is
R2 c4–c6 + R3 c3–c6; PITCH is R4 c1–c2 + R5 c1; FLUX is R4–R5 c3–c4; LEVEL is
R4–R5 c5–c6, which puts SEND (`REV_MIX`) at the inner bottom corner, next to
ROOM. Column 6 carries no big cap, which is what keeps the seam to the centre
at 11.48 mm. Approved by Bastian 2026-10-07 from a 1:1 preview.

### 3.2 Centre

| Row | Knobs (k) | Group |
|---|---|---|
| R1 | `SCALE` (−1.5), `DRIFT` (−0.5), `CHOKE` (+0.5), `PULL` (+1.5) | GLOBAL |
| R2 | `TEMPO` (−1), `COUPLE` (0), `SHUFFLE` (+1) | TIMING |
| R3 | `TIDE` (−1), **`MORPH`** (0), `PACE` (+1) | TIMING |
| R4 | `REV_SIZE` (−1), `REV_TONE` (0), `REV_DIFF` (+1) | ROOM |
| R5 | **`REV_DECAY`** (0) | ROOM |

`REV_DECAY` cannot sit in R4: directly under `MORPH` the two big caps would be
7.75 mm apart.

### 3.3 LEDs (Bastian, 2026-10-07)

19 lamps drawn today, **17** after: six go, four come, two change meaning.
The guiding rule (Bastian): **the plate shows timing, not modulation** —
plus the few states a player must see at a glance (signal present, record,
latch, limiter).

**Removed** — they showed lane excursions, i.e. modulation:
`SRC_A_L`/`SRC_B_L` (TIMB), `FLT_A_L`/`FLT_B_L` (FILT), `CLR_A_L`/`CLR_B_L`
(COLR). Their LightIds go from `gen_panel.py`'s `HW_ONLY_LIGHTS`.

**Added:**

| Lamp | Where | Shows |
|---|---|---|
| `FTIME_A_L` / `FTIME_B_L` | beside `FLUXRATE` (caption TIME, knob lamp) | one flash per FLUX time period of that deck — the repeat tempo TIME sets — **only while that deck's FLUX `MIX` > 0**, so at a quarter-note TIME it does not just double `TEMPO_L`, and it also says FLUX is in the sound |
| `IN_LVL_L` | jack-row satellite of `IN_L`, outboard (left): x = 33.0 − 6.7 = 26.30 | input signal, the peak of IN L and IN R together |
| `RST_L` | jack-row satellite of `RESET`, outboard: x = 168.8 + 6.7 = 175.50 | a flash on every reset received |

`RST_L` mirrors `CLK_L` (129.30, inboard of CLOCK) about the centre line,
so the CLOCK group reads symmetric. `IN_LVL_L` sits between `SHIFTBTN_L`
(20.70) and `IN_L`: 2.50 mm of material to the SHFT lamp's hole and 2.15 mm
to IN L's hole, both above `MIN_WEB` = 2.0. Whether IN L's nut covers it is
the grip-test measurement already listed for `CLK_L`/`CEIL_L`.

**Meanings for lamps that had none** (both are drawn and wired today but
held dark by `led_law.hpp`, "needs host state" / "waits for SHIFT"):

| Lamp | Shows |
|---|---|
| `CLK_L` (renamed from `SYNC_L`) | a flash on every pulse arriving at the CLOCK jack. Not redundant with `TEMPO_L`: with PACE in use the transport's beat and the incoming pulse run at different rates, and this lamp is how the player sees which pulse is actually patched. Renamed because `SYNC` is also the printed caption of the `COUPLE` knob, which has no lamp — the two were confused in review. |
| `SHIFTBTN_L` | one pulse when a **new engine** (either deck) or a **new scale** has actually taken effect — at the switch, not when the knob is turned, so the player sees the moment the next engine runs. Once SHIFT gets functions, steady while SHIFT is held. The pulse must read differently from both that steady light and the MOD latch's double pulse; the pattern is P6b's. |

**Not added: a STEPS lamp.** Asked for first, then dropped by Bastian the
same day: a step-advance flash would differ from the ATK gate lamp
(`GATE_*_L`, `inst.gate()` straight through in `host/vcv/src/led_law.hpp`)
only on rests and on notes held across steps. Its two LEDs went to FLUX
TIME instead. So spec 2026-08-16 (song-phrase-flash S4) stands: STEPS keeps
no lamp, `FLOW_*` stay undrawn, and
`test_steps_has_no_lamp_on_the_hw_plate` stays.

**Not added: TIDE and PACE lamps.** Also asked for first and dropped the
same day under the timing rule: TIDE scales the texture lanes' rate
(`SuperModulator::set_tide`) and PACE the modulator's base rate
(`set_pace`), so both lamps would have shown modulation speed, not musical
time. FLUX TIME is timing: FLUX is a tempo-synced delay and TIME picks its
division (`set_flux_rate(slice_idx)` on the bpm).

**Changed meaning — `LVL_A_L` / `LVL_B_L`** (at COMP, caption LVL): today
the `LANE_LEVEL` excursion (`led_law.hpp`), i.e. modulation. From now on:
**does this deck deliver signal at all** — lit while the deck's own output
into the mix is above a floor, dark when nothing comes out (filter cutoff
all the way down, LVL at zero, an empty sampler). The point is to see at a
glance which deck is silent. Which tap and which floor is a P6b / VCV-host
question and needs a probe; the panel pass only keeps the lamp where it is.

**Unchanged:** the other knob lamps (`SONG_*`, `GATE_*` at ATTACK, `LVL_*` at
COMP with its new meaning, `TEMPO_L`) keep their caption cluster and move with their knob.
`REC_A_L` / `REC_B_L` stop being hand-placed (today `(108.50, Y_TOP)`) and
become satellites of their key at `SAT_D` = 6.7 mm, inboard: x = 111.65 on
deck A, 7.4 mm of material to `SCALE`'s pot hole. `CLK_L`, `MODBTN_L`,
`SHIFTBTN_L`, `CEIL_L` keep their x.

**Placement rule for every lamp:** the plate web (`MIN_WEB`) to every hole
**and** the board's pad clearance to every pot pad, as `place_check.py`
judges it. The second half is what P4-1 found broken: a knob lamp's caption
cluster puts the LED ~7 mm under its pot, and the **top row's pots have
their pins south** (`POT_TOP_ROT` = 90, `place.py`), right where that LED
lands. That hit `SONG_*` — the only top-row knob lamp, since none of the
new ones sits in R1 (`FLUXRATE` is R4); with the real
`BODY_R["S"]` = 3.85 the cluster moves 0.55 mm *closer* still. Lower-row
pots have their pins north, away from their own lamp. P4-1 measured that the
SONG lamp needs a 2.6 mm drop at LED rotation 0 for 0.51 mm pad clearance
(P4-1 placement spec, SONG item and its drop table). In the new raster SONG
(R1 c3) has `SMOOTH` under it, whose cap top is at 30.775, and the dropped
cluster also pushes the SEQUENCE/MOTION frame seam (§4.3). **So the SONG
lamp's position is the first thing the plan probes** — candidates are a
dropped cluster or a lamp beside the knob in the 11.15 mm gap — and the
probe's numbers go into this spec before code follows.

### 3.4 Jack row

`JACK_Y` 114.00 → **112.75**. P4-1 measured that the jacks' tip pads lie
past the board edge at 114.0 and that the row must sit at y ≤ 112.77; keys,
jack-row LEDs and the SD slot share `JACK_Y` and move with it. The 9 mm rule
reaches the jack row through its keys: SHFT sits under `COLOR_A` (and MOD
under `COLOR_B`) at 9.05 mm, measured — at `JACK_Y` = 112.50 it would be
8.81 and fail, so 112.75 is not a rounding of 112.77 but the window between
two limits. Nearest jack centre to a cap edge: 12.29 mm (13.50 at 114.0).

## 4. Generator changes (`host/vcv/res/gen_hw_panel.py`)

1. **Cell table replaces coordinates.** `DECK_POS` and `CENTER_POS` become
   one table of name → (row, column) (centre: (row, k)); the raster constants
   of §3 turn cells into millimetres. The hand-tuned machinery that existed
   only to place knobs between lines goes: `Y_B1K/Y_B1M/Y_B1G`,
   `Y_B2K/Y_B2G/Y_B2B/Y_B2L`, `CENTRE_PITCH`, `VOICE_MID`, the LEVEL band
   (`LEVEL_*`, `_lvl0`, `LEVEL_SLOTS`, the foot/shoulder constants).
   `JACK_POS`, `SD_*` and `MODBTN`'s jack-row slot stay; `JACK_Y` becomes
   112.75 (§3.4).
2. **Real cap radii.** `BODY_R` = {G 6.0, S 3.85, P 3.0, J 3.1, L 1.5}. This
   is P1 §3.1, deferred to exactly this round. Captions stay at
   `CAPTION_GAP` = 3.60 below the body edge, so they rise 0.55 mm with it.
   `kFfPadR` in the header follows `BODY_R["P"]`; it is also used by the big
   module's ENGINE latch, so check that widget renders unchanged.
3. **Group frames from cells.** A frame is the union of its cells' ink
   (bodies, captions, lamp clusters) plus a margin, drawn as a rectangle or as
   the existing two-band L. The fixed `GROUP_ROWS` cut raster goes. Measured
   budget: where a small knob's caption sits above another group's big cap
   (e.g. `STEPS` over `MOD`, `SOURCE` over `FLUX`) there are **6.675 mm**
   from caption ink to cap edge, so margin + `BOX_GAP` + margin must fit in
   that: today's 2.15 + 3.0 + 2.15 = 7.30 does not; 1.80 + 3.0 + 1.80 = 6.60
   does. The margin is a drawing number and may shrink; the knob raster is
   not moved for a frame.
4. **Lamps (§3.3).** `KNOB_LAMPS` loses the SOURCE/FILT/COLOR entries and
   gains `FTIME_*` → `FLUXRATE_*`; `SYNC_L` is renamed `CLK_L` everywhere
   it is named (`gen_panel.py`, `led_law.hpp`, the guards, `panel-map.json`
   via `assign.py`, the shell's generated table);
   `LIGHT_POS` gains `IN_LVL_L` and `RST_L` as jack-row satellites and
   derives `REC_*_L` from its key. The LightIds change in `gen_panel.py`'s
   `HW_ONLY_LIGHTS`, so the header's `LightId` enum regenerates; the VCV host
   compiles against it and every removed id must leave `host/vcv/src` too.
   The VCV host may leave the new lamps dark in this pass — lighting them is
   §1's out-of-scope item — but must not crash or draw them in the wrong
   place.
5. **Outputs regenerate:** `FireflowHW.svg`, `FireflowHW-print.svg`,
   `FireflowHW-holes.json`, the header, and (via `gen_hw_cut.py`, which places
   nothing) `FireflowHW-cut.svg`.

## 5. Guards (`host/vcv/res/test_hw_panel.py`)

- **New — the 9 mm rule.** For every pair of knobs and keys,
  centre distance − r₁ − r₂ ≥ 9.0 (tolerance 1e-6), with the cap radii of §2
  written into the test, not imported from `BODY_R`, so a later edit to the
  generator cannot loosen the guard. Also asserts the pair count is non-zero
  and that 70 knobs and all 4 keys (`REC_A/B`, `SHIFTBTN`, `MODBTN`) were
  seen. **RED proven once** by moving
  one knob 0.1 mm towards a neighbour.
- **New — cell table is the source.** Every knob position equals its cell's
  raster coordinate; no knob is placed outside the table.
- **Removed** (they pin coordinates the raster replaces):
  `test_row3_ceiling_holds_at_seventy`, `test_level_band_is_evenly_divided`,
  `test_level_band_clears_rooms_shoulder`,
  `test_level_band_holds_pan_in_slot_zero`,
  `test_middle_band_runs_on_three_lines`, `test_group_raster_closes`,
  `test_rows_are_centred_on_their_ink`,
  and the coordinate pins inside
  `test_drawing_geometry` (DECY/TONE at 79.0/97.0 — a layout pin, not a
  by-ear decision; `docs/by-ear-decisions.md` has no entry for it).
  Each removal is listed in the commit message with its reason.
- **Rewritten:** `test_led_inventory_after_the_feedback_round` to §3.3's
  list (17 lamps, the six removed absent, the four added present);
  `test_satellite_lamps_clear_their_anchor_hole` extended to `IN_LVL_L`,
  `RST_L`, `REC_*_L`; every jack-row y check reads `JACK_Y`, not 114.0.
- **Kept, unchanged in intent:** 60 HP, rail keep-out, mirror symmetry (also
  of captions), same runtime params in the same order as the big panel,
  footprints, captions clear of neighbours and of their own knob, lamp
  clusters, satellite webs (`MIN_WEB`), SD cutout, legend notches, bodies and
  captions inside their frame, committed files match the generator.

## 6. Downstream chain, in order

The board is the real risk, so it runs before the drawing is polished.

1. **Probe the SONG lamps** (§3.3) against the pot footprint's pads,
   both candidates, and write the chosen position and its measured clearance
   into §3.3 before any generator code.
2. **Generator + guard** (§4.1, §4.2, §4.4, §5) — positions, lamps, jack
   row, holes, cut file.
3. **Board.** `hardware/reva/assign.py` → `panel-map.json` (pot → mux
   assignment changes with the positions) → `place.py` → `route.py` →
   `route_check.py`, then P4-3's export (Gerbers, drill, CPL, BOM, assembly
   sheets) and `reva_fab_guard`. The pots get more room than before (13.0 →
   18.85 / 20.125 mm pitch), but the router has to prove it; if it does not
   close, that is a stop-and-report, not a reason to move knobs back. Gate:
   both `KNOWN_PANEL` lists empty and `test_place.py` / `test_route.py`
   asserting the empty sets. The LED count drops from 19 to 17, so the
   five 595s keep enough bits; `assign.py` reassigns indices.
4. **Firmware table.** `shell/gen_panel_map.py` → `generated_panel_map.h`;
   `cmake --build` and `ctest` in **Release** (`test_controls_map`,
   `test_mux_plan`). Any firmware code naming a removed lamp
   (`SRC_*`, `FLT_*`, `CLR_*`) goes with it.
5. **Drawing** (§4.3) — frames, print, VCV header.
6. **VCV.** `host/vcv/build-local.sh install`, then restart Rack; a headless
   render (`Rack.exe -u <throwaway> -t 2`) confirms the panel draws.
7. **Docs.** `docs/hardware/grip-test.md`: items 1–2 recorded as failed with
   the 5.3 mm measurement and this spec as the fix, item 3 as passed;
   `docs/roadmap.md` M6 entry.

**Bastian's part:** order the correction cut at Formulor (budgeted in P1 §2,
~20 €) from the new `FireflowHW-cut.svg`, then rerun grip-test items 1–2 and
4. The freeze tag waits for that pass.

## 7. Risks

- **Router does not close** on the new pot positions (§6.3). Mitigation: it
  runs right after the generator, before any drawing work, so a failure
  costs one task. The new jack-row height and the new lamp positions near
  back-side parts may need `OVERRIDES` in `place.py`, as P4-1/P4-2 did.
- **The SONG lamps have no measured position yet** (§3.3). The plan's first
  task is the probe; if neither candidate clears the pads, that comes back
  to Bastian before the generator moves.
- **Frame margins** (§4.3) may need to drop below 1.80 somewhere the probe
  did not look. The guard "bodies and captions inside their frame" plus
  "frames do not overlap" decides; a margin change is a drawing change only.
- **Cap sizes are datasheet/part-listing values.** If a caliper on the
  delivered caps says otherwise, the guard's radii change and the raster is
  re-checked before the cut is ordered.
