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

## 1. Goal and scope

Every knob and every key on the 60 HP plate keeps at least 9.0 mm between cap
edges, measured with the real caps. The plate stays 60 HP, the jack row stays
where it is, deck B stays deck A mirrored.

Done means: the generator places every knob from one cell table, a guard
proves the 9 mm rule (and has been seen red once), the hole list, cut file,
panel map, placed and routed Rev A board, generated firmware table and VCV
`FireflowHW` panel all come from the new positions, and every gate along that
chain is green.

**Not in scope:** the jack row (positions, pitch, satellites), the big VCV
module (`Fireflow`), the engine, plate colours, key functions, the LED law.

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
plate edge of the 304.4 mm plate); lowest cap edge y 101.00, 13 mm above the
jack row at 114.0.

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

### 3.3 LEDs

- **Knob-owned lamps** (SONG, ATTACK, SOURCE, FILT, COLOR, COMP, TEMPO) keep
  their caption cluster and move with their knob; nothing to decide.
- **`REC_A_L` / `REC_B_L`** stop being hand-placed (today `(108.50, Y_TOP)`)
  and become satellites of their key at `SAT_D` = 6.7 mm, inboard:
  x = 111.65 on deck A. Its hole keeps 7.4 mm of material to `SCALE`'s pot
  hole.
- **Jack-row satellites** (`SYNC_L`, `MODBTN_L`, `SHIFTBTN_L`, `CEIL_L`) do
  not move.

## 4. Generator changes (`host/vcv/res/gen_hw_panel.py`)

1. **Cell table replaces coordinates.** `DECK_POS` and `CENTER_POS` become
   one table of name → (row, column) (centre: (row, k)); the raster constants
   of §3 turn cells into millimetres. The hand-tuned machinery that existed
   only to place knobs between lines goes: `Y_B1K/Y_B1M/Y_B1G`,
   `Y_B2K/Y_B2G/Y_B2B/Y_B2L`, `CENTRE_PITCH`, `VOICE_MID`, the LEVEL band
   (`LEVEL_*`, `_lvl0`, `LEVEL_SLOTS`, the foot/shoulder constants). `JACK_Y`,
   `JACK_POS`, `SD_*` and `MODBTN`'s jack-row slot stay.
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
4. **Outputs regenerate:** `FireflowHW.svg`, `FireflowHW-print.svg`,
   `FireflowHW-holes.json`, the header, and (via `gen_hw_cut.py`, which places
   nothing) `FireflowHW-cut.svg`.

## 5. Guards (`host/vcv/res/test_hw_panel.py`)

- **New — the 9 mm rule.** For every pair of knobs and keys,
  centre distance − r₁ − r₂ ≥ 9.0 (tolerance 1e-6), with the cap radii of §2
  written into the test, not imported from `BODY_R`, so a later edit to the
  generator cannot loosen the guard. Also asserts the pair count is non-zero
  and that 70 knobs and 2 deck keys were seen. **RED proven once** by moving
  one knob 0.1 mm towards a neighbour.
- **New — cell table is the source.** Every knob position equals its cell's
  raster coordinate; no knob is placed outside the table.
- **Removed** (they pin coordinates the raster replaces):
  `test_row3_ceiling_holds_at_seventy`, `test_level_band_is_evenly_divided`,
  `test_level_band_clears_rooms_shoulder`,
  `test_level_band_holds_pan_in_slot_zero`,
  `test_middle_band_runs_on_three_lines`, `test_group_raster_closes`,
  `test_rows_are_centred_on_their_ink`, and the coordinate pins inside
  `test_drawing_geometry` (DECY/TONE at 79.0/97.0 — a layout pin, not a
  by-ear decision; `docs/by-ear-decisions.md` has no entry for it).
  Each removal is listed in the commit message with its reason.
- **Kept, unchanged in intent:** 60 HP, rail keep-out, mirror symmetry (also
  of captions), same runtime params in the same order as the big panel,
  footprints, captions clear of neighbours and of their own knob, lamp
  clusters, satellite webs (`MIN_WEB`), SD cutout, legend notches, bodies and
  captions inside their frame, committed files match the generator.

## 6. Downstream chain, in order

The board is the real risk, so it runs before the drawing is polished.

1. **Generator + guard** (§4.1, §4.2, §5) — positions, holes, cut file.
2. **Board.** `hardware/reva/assign.py` → `panel-map.json` (pot → mux
   assignment changes with the positions) → `place.py` → `route.py` →
   `route_check.py`, then P4-3's export (Gerbers, drill, CPL, BOM, assembly
   sheets) and `reva_fab_guard`. The pots get more room than before (13.0 →
   18.85 / 20.125 mm pitch), but the router has to prove it; if it does not
   close, that is a stop-and-report, not a reason to move knobs back.
3. **Firmware table.** `shell/gen_panel_map.py` → `generated_panel_map.h`;
   `cmake --build` and `ctest` in **Release** (`test_controls_map`,
   `test_mux_plan`).
4. **Drawing** (§4.3) — frames, print, VCV header.
5. **VCV.** `host/vcv/build-local.sh install`, then restart Rack; a headless
   render (`Rack.exe -u <throwaway> -t 2`) confirms the panel draws.
6. **Docs.** `docs/hardware/grip-test.md`: items 1–2 recorded as failed with
   the 5.3 mm measurement and this spec as the fix, item 3 as passed;
   `docs/roadmap.md` M6 entry.

**Bastian's part:** order the correction cut at Formulor (budgeted in P1 §2,
~20 €) from the new `FireflowHW-cut.svg`, then rerun grip-test items 1–2 and
4. The freeze tag waits for that pass.

## 7. Risks

- **Router does not close** on the new pot positions (§6.2). Mitigation: it
  runs second, before any drawing work, so a failure costs one task.
- **Frame margins** (§4.3) may need to drop below 1.80 somewhere the probe
  did not look. The guard "bodies and captions inside their frame" plus
  "frames do not overlap" decides; a margin change is a drawing change only.
- **Cap sizes are datasheet/part-listing values.** If a caliper on the
  delivered caps says otherwise, the guard's radii change and the raster is
  re-checked before the cut is ordered.
