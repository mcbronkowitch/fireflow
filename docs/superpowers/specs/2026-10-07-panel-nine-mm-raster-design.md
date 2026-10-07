# Panel correction — the 9 mm raster

**Date:** 2026-10-07
**Phase:** Rev A P1 correction round (P1 spec `2026-09-29-rev-a-p1-panel-parts-design.md`
§5, "each failure gets a fix in the generator"), before the panel freeze
(`panel-freeze-2026-11-06`) and before the Rev A board is ordered.
**Trigger:** the acrylic plate arrived. Checklist items 1 and 2 of
`docs/hardware/grip-test.md` (pinch two neighbours, turn without brushing a
neighbour's cap) failed: the knobs stand too close to play. Item 3 (patching
all jacks) passed — the jack row was fine.
**Decisions (Bastian, 2026-10-07):** no two caps closer than **9 mm, edge to
edge**; spacing beats thematic grouping — form follows function. The plate
shows **timing, not modulation**. The Rev A board is not ordered yet, so the
board follows the plate.

This is also **the one panel pass** that P4-1 left its known panel violations
for (decided 2026-10-04): the jack row's height and the LED rework travel with
the knob raster, so the scripted chain to the board runs once.

## 1. Goal and scope

Every knob and key on the 60 HP plate keeps at least 9.0 mm between cap
edges, measured with the real caps. Deck B stays deck A mirrored. The plate
stays 60 HP.

Done means: the generator places every control from one cell table; a guard
proves the 9 mm rule and has been seen red once; the hole list, cut file,
print sheet, panel map, placed and routed Rev A board, order package,
generated firmware table and the VCV `FireflowHW` panel all come from the new
positions; every gate along that chain is green; and the board's
`KNOWN_PANEL` exemption sets (`place_check.py`, `route_check.py`) are empty
— except `route_check.py`'s `"audio"` set, which holds exactly the admitted
jack-zone pairs (§5.2).

**In scope:** the knob raster (§3), four reserved knob positions (§4), the
LED inventory (§5), the jack row's height (§6), the group fields (§7), the
mux assignment rule (§8), and the chain to the board and the firmware (§10).

**Not in scope:** jack x positions and pitch (patching passed); the big VCV
module `Fireflow`; the engine; plate colours; key functions. **How** the LEDs
light is P6b's LED law and the VCV host's light code — §5 fixes what each one
*means*, this pass gives it a LightId, a position, a hole and a footprint.
**Making the four reserved knobs real parameters** is a follow-up spec (§4).

## 2. What was measured

Every number here was printed by a throwaway probe against the generator and
`hardware/reva/assign.py` on 2026-10-07; the guards in §9 re-measure them.
Cap sizes are the parts bought (P1 §1): Micro Knob 7.7 mm Ø (r 3.85),
Davies 1900H 12 mm Ø (r 6.0), Thonk low-profile key cap 6 mm Ø (r 3.0; P1 §6,
"only the 6 mm cap passes the 6.2 mm hole").

**Today's plate:** 70 knob positions (14 big). The 13.0 mm pitch leaves
**5.3 mm** between small caps, which is also the median nearest-neighbour
gap. 54 pairs are under 9 mm, involving 66 of the 70 knobs.
`BODY_R["S"]` = 4.4 still describes an 8.8 mm cap, not the 7.7 mm part
(P1 §3.1, deferred to this round).

**The new plate** (§3, §4): 74 knob positions and 2 deck keys, **no pair
under 9.00 mm**. Big beside small lands on exactly 9.00 (24 pairs); small
beside small is 11.15 mm across and 12.43 mm down; median nearest-neighbour
gap 10.27 mm; deck-to-centre seam 11.48 mm. Caps span x 4.70 .. 300.10
(4.5 mm to each edge of the 304.4 mm plate). Lowest cap edge 98.85; lowest
knob ink (caption) 102.45. Jack-row keys to the nearest cap: 11.20 mm
(SHFT / `ROOT_A`). Nearest jack centre to a cap edge: 13.90 mm.

Rejected on the numbers: stretching today's groups (their knobs stand on
seven to eight heights; at ≥ 16.7 mm vertical pitch the plate holds five to
six rows) and moving the jacks to widen the plate (Bastian's fallback) — not
needed.

## 3. The raster

| | Value | Why |
|---|---|---|
| Column pitch | **18.85 mm** | big r 6.0 + small r 3.85 + 9.0 |
| Row pitch | **20.125 mm** | rows 14.5 → 95.0 in four steps |
| Rows R1..R5 | y = 14.500, 34.625, 54.750, 74.875, 95.000 | top row as today |
| Deck A columns c1..c6 | x = 10.70, 29.55, 48.40, 67.25, 86.10, 104.95 | c1 = plate edge 0.2 + 4.5 + big r 6.0 |
| Deck B | x → 304.8 − x | mirror axis 152.4, as today |
| Centre | x = 152.4 + k · 18.85; R1 at k ∈ {±0.5, ±1.5}, R2..R5 at k ∈ {−1, 0, +1} | centred on 152.4 |

**Big caps stand in R2 and R4 only, never in column c6, and never next to
another big cap in the same row.** Two orthogonally adjacent big caps would
need 21.0 mm against 18.85 / 20.125, so this is load-bearing. Keeping R1 and
R5 small is what gives the jack row (§6) and the group fields (§7) their
room: the lowest ink is a small caption at 102.45 instead of a big cap's
lamp cluster at 105.3. c6 small keeps the seam to the centre at 11.48 mm.

### 3.1 Deck A (c1 = outer edge; deck B mirrored; ● = big; *italics* = reserved, §4)

| Row | c1 | c2 | c3 | c4 | c5 | c6 |
|---|---|---|---|---|---|---|
| R1 | `ENGINE` | `SONG` | `STEPS` | `RATE` | `MELODY` | `REC` (key) |
| R2 | `MOD` ● | `SHAPE` | `DENSITY` ● | `SOURCE` | `FILT` ● | `DEPTH` |
| R3 | `SMOOTH` | `RANGE` | `SUB` | `ATTACK`/`STAGES` | `DECAY` | `RES` |
| R4 | `COLOR` ● | `TUNE` | `FLUX` ● | `FLUXRATE` | `COMP` ● | `PAN` |
| R5 | *`ROOT_A`* | `DETUNE` | `FLUXFB` | `LINK` | `GRIT` | `REV_MIX` |

Groups, all contiguous: ENG (R1 c1), SEQUENCE (R1 c2–c5), CAPTURE (R1 c6),
MOTION (R2 c1–c3 + R3 c1–c2), VOICE (R2 c4–c6 + R3 c3–c6), PITCH (R4–R5
c1–c2), FLUX (R4–R5 c3–c4), LEVEL (R4–R5 c5–c6). VOICE reads TIMB FILT DPTH
over ATK DEC RES SUB — today's two VOICE rows, swapped. `SONG` and `STEPS`
swapped so no knob lamp stands over a big cap (§7). Approved by Bastian
2026-10-07 from a 1:1 preview.

### 3.2 Centre

| Row | k = −1.5 | −1 | −0.5 | 0 | +0.5 | +1 | +1.5 | Group |
|---|---|---|---|---|---|---|---|---|
| R1 | `SCALE` | | `DRIFT` | | `CHOKE` | | `PULL` | GLOBAL |
| R2 | | `TIDE` | | `MORPH` ● | | `PACE` | | TIMING |
| R3 | | `TEMPO` | | `COUPLE` | | `SHUFFLE` | | TIMING |
| R4 | | `REV_SIZE` | | `REV_DECAY` ● | | `REV_DIFF` | | ROOM |
| R5 | | *`REV_MOD`* | | `REV_TONE` | | *`REV_SMEAR`* | | ROOM |

## 4. Reserved knob positions

Four free cells get a pot now, because a pot added after the board is
ordered costs a board. Bastian chose (2026-10-07) to reserve the hardware in
this pass and make the parameters real in a follow-up spec:

| Id | Cell | Caption | Becomes |
|---|---|---|---|
| `ROOT_A` / `ROOT_B` | R5 c1, each deck | `ROOT` | a per-deck scale root, 12-step detent. Today `P_ROOT` is one global param that sets both decks and **the VCV host never sets it** — both decks always play in C. `quant().set_root()` is already per deck in the engine, so the follow-up splits `P_ROOT` into `ROOT_A`/`ROOT_B`. Not redundant with TUNE, which shifts pitch continuously instead of changing key. |
| `REV_MOD` | centre R5 k=−1, under SIZE | `WOBL` | the reverb's tail-delay LFO depth (`P_REV_MOD`, `set_mod_depth`) |
| `REV_SMEAR` | centre R5 k=+1, under DIFF | `SMER` | the reverb's input-diffuser LFO depth (`P_REV_SMEAR`, `set_diffuser_mod_depth`), beside DIFF because both act on the diffusers |

**`docs/by-ear-decisions.md` says the opposite** ("do not propose giving
SMEAR or WOBL a knob back", confirmed by ear 2026-08-09, host constants 0.30
and 0.15). Bastian reversed that on 2026-10-07. This pass only reserves the
hardware, so the by-ear entry changes in the follow-up, where the knobs boot
at 0.30 / 0.15 so the sound does not move.

In this pass the four are **`HwOnly` entries** in `gen_hw_panel.py` (like
`SHIFTBTN`): a hole, a caption and a pot on the plate and the board, a mux
channel, a row in the firmware table that sends nothing — and no Rack
widget, no ParamId, no change to the big panel. Small caps: 60 needed
against 61 bought (green 23 = part A's 23, dark blue 14 = the centre's 14,
orange 24 for part B's 23); pots: 74 against 77 bought.

## 5. LEDs

19 lamps drawn today, **17** after.

| Lamp | Where | Shows | Status |
|---|---|---|---|
| `SONG_A_L` / `_B_L` | under SONG (cluster) | flash on a phrase change | unchanged meaning; position probed (§5.1) |
| `GATE_A_L` / `_B_L` | under ATTACK (cluster) | a note is sounding (`inst.gate()`) | unchanged |
| `LVL_A_L` / `_B_L` | under COMP (cluster) | **this deck delivers signal at all** — lit while the deck's own output into the mix is above a floor, dark when nothing comes out (cutoff all the way down, LVL at zero, an empty sampler) | **new meaning** (was the `LANE_LEVEL` excursion, i.e. modulation). Tap and floor: a P6b / VCV-host probe |
| `FTIME_A_L` / `_B_L` | under FLUXRATE, caption TIME (cluster) | one flash per FLUX time period — the repeat tempo TIME sets — **only while that deck's FLUX MIX > 0** | **new**. FLUX is a tempo-synced delay and TIME picks its division (`set_flux_rate(slice_idx)`), so this is timing; gating on MIX keeps it from doubling `TEMPO_L` at a quarter note and says FLUX is in the sound |
| `REC_A_L` / `_B_L` | under the REC key (cluster) | unchanged | **moved** from a hand-placed satellite (108.5, 14.5) into a word-then-LED cluster like the knob lamps: beside the key it reaches 0.25 mm out of its cell and breaks the 3 mm field gap to GLOBAL or SEQUENCE (§7) |
| `TEMPO_L` | under TEMPO (cluster) | the transport beat | unchanged |
| `CLK_L` | jack-row satellite of CLOCK, inboard (129.30) | a flash on every pulse arriving at the CLOCK jack | **renamed from `SYNC_L`** (the COUPLE knob's printed caption is also SYNC and has no lamp; the two were confused in review) and **given a meaning** — today it is held dark. Not redundant with `TEMPO_L`: with PACE in use the beat and the incoming pulse run at different rates |
| `RST_L` | jack-row satellite of RESET, outboard (168.80 + 6.7 = 175.50) | a flash on every reset received | **new**; mirrors `CLK_L` about the centre line |
| `IN_LVL_L` | jack-row satellite of IN_L, outboard (33.00 − 6.7 = 26.30) | input signal, the peak of IN L and IN R together | **new**. 2.50 mm of material to the SHFT lamp's hole, 2.15 to IN L's, both over `MIN_WEB`; whether IN L's nut covers it is the grip-test measurement already listed for `CLK_L`/`CEIL_L`. Inside IN L's audio jack zone by construction (§5.2) |
| `SHIFTBTN_L` | unchanged (20.70) | one pulse when a **new engine** (either deck) or a **new scale** has actually taken effect — at the switch, not when the knob turns. Once SHIFT has functions, steady while held. The pulse must read differently from that and from MOD's double pulse; pattern is P6b's | **given a meaning** (held dark today) |
| `MODBTN_L` | unchanged | the MOD latch | unchanged |
| `CEIL_L` | unchanged | the master limiter bending | unchanged |

**Removed** (lane excursions, i.e. modulation): `SRC_A_L`/`_B_L` (TIMB),
`FLT_A_L`/`_B_L` (FILT), `CLR_A_L`/`_B_L` (COLR).

**Asked for and dropped the same day:** a STEPS lamp (a step-advance flash
differs from the gate lamp only on rests and held notes — so spec 2026-08-16
song-phrase-flash S4 stands, `FLOW_*` stay undrawn and
`test_steps_has_no_lamp_on_the_hw_plate` stays); TIDE and PACE lamps (TIDE
scales the texture lanes' rate, PACE the modulator's base rate — both would
show modulation speed).

### 5.1 The SONG lamp needs a probe

A cluster LED sits ~7 mm under its control, and the **top row's pots have
their pins south** (`POT_TOP_ROT` = 90 in `place.py`), right where that LED
lands. P4-1 measured that the SONG lamp needed a 2.6 mm drop at LED rotation
0 for 0.51 mm pad clearance (P4-1 placement spec, SONG item). With the real
`BODY_R["S"]` = 3.85 the cluster sits 0.55 mm *higher* still. SONG is the
only top-row knob lamp (REC is a key, not a pot; FTIME, TEMPO and the others
sit in R3/R4 over pots whose pins point north, away from them). Under SONG now
stands the small `SHAPE` (cap top 30.775), so a drop has room.

**The plan's first board step probes SONG's lamp** against the pot pads, with
two candidates: the cluster dropped by a measured amount, or the lamp beside
the knob in the 11.15 mm gap. The winning position and its measured
clearance go into this section before the generator is changed for it.

### 5.2 The IN lamp sits in a jack zone — admitted

`route_check.py`'s audio step flags every LED pad within 10 mm of an audio
jack's tip pad: the 595-driven LED lines are aggressors, the jack a victim.
Bastian admitted two such pairs on 2026-09-30 (`CEIL_L/OUT_R`,
`IN_L/SHIFTBTN_L`). A lamp "beside IN" cannot keep 10 mm, so **`IN_L/IN_LVL_L`
is admitted as the third pair** (Bastian, 2026-10-07), on the same terms:
whether it actually couples is measured on the first Rev A board with the
coupon's crosstalk rig, not argued away now. The `"audio"` set and
`test_route.py`'s `JACK_ZONES` therefore hold these three keys after the pass,
with whatever aggressor pads the audio step finds for them on the new board.

## 6. Jack row

`JACK_Y` 114.00 → **112.75**. P4-1 measured that the jacks' tip pads lie past
the board edge at 114.0 and that the row must sit at y ≤ 112.77. Keys,
jack-row LEDs and the SD slot share `JACK_Y` and move with it. Jack x
positions do not move. With R5 all small, the 9 mm rule no longer bites here
(keys 11.20 mm from the nearest cap), so 112.75 is simply the old row raised
as little as the board allows.

## 7. Group fields

A group's field is built from its cells, no longer from the fixed
`GROUP_ROWS` cut raster:

- **Cell rect, x:** the union of the control's ink ± `FIELD_MARGIN` and its
  column cell, x ± (18.85 − 3.0) / 2 = ± 7.925. The column cell is what gives
  a one-control group (ENG, CAPTURE) room for its legend.
- **Band, y:** per group and row, from the topmost ink − `FIELD_MARGIN` to the
  bottommost ink + `FIELD_MARGIN` of that group's controls in that row. Ink is
  bodies, captions, and lamp clusters including the LED body that hangs
  0.7 mm under a caption.
- **Field:** a group's bands, joined across the rows it spans. With §3's
  cells every field is a rectangle or a two-band L, which the existing `Box`
  (with `foot`) already draws: where the lower band is narrower the step sits
  at the upper band's bottom, where it is wider at the lower band's top.
- **`FIELD_MARGIN` = 1.45 mm** (today 2.15 vertically). Measured: with 1.45
  every pair of fields of different groups keeps ≥ 3.00 mm (`BOX_GAP`); at
  1.50 they come to 2.97, because a cluster LED hangs 0.7 mm under its
  caption. This is why `SONG`/`STEPS` swapped and no lamp stands over a big
  cap: SONG over DENSITY would have needed a 1.48 mm margin.
- **`LEGEND_INSET` 4.0 → 3.0.** CAPTURE is a one-cell field of 15.85 mm, and
  its legend needs inset + 9.65 + `NOTCH_PAD` + `NOTCH_R` + `FIELD_R` ≤ 15.85;
  at 3.0 that is 15.55. The notch still clears the left corner
  (3.0 − 1.0 ≥ 1.5 + 0.4).
- **Jack row:** keeps its own frames (IN, CV, MOD, CLOCK, OUT; no legends),
  ink ± `FIELD_MARGIN` vertically, the existing x cuts — except the row now
  starts at x = 23.30 instead of 28.00 so `IN_LVL_L` sits inside IN. Its top
  (108.20) keeps 4.3 mm to the knob fields above.
- **Keys drawn round:** the plate draws a key as its real 6 mm round cap
  (r = `BODY_R["P"]` = 3.0), not an 8 mm square; `kFfPadR` follows. It is
  also used by the big module's ENGINE latch, so that widget gets a look.

## 8. Mux assignment

10 muxes × 8 channels = 80. Today 70 pots + 2 calibration = 72. After: 74 + 2
= **76**, 4 spare.

`assign.py` splits the pots into four contiguous x-bands (24 / 16 / 16 / 24
channels, `SENSE_0, SENSE_2, SENSE_3, SENSE_1`) and **never splits a column**.
With `ROOT_A`/`ROOT_B` that has no solution — measured: a deck carries 29
pots and its c1–c5 hold 25 against a 24-channel band. The rule is relaxed:

- Pots are ordered by (x, y, id) and the band cuts may fall anywhere in that
  order. The score is (number of split columns, fullest band's fill ratio,
  cuts), lowest wins — a split is taken only when no split-free answer
  exists.
- Measured on the new plate: **one split**, at x = 218.70 (deck B c5):
  `MELODY_B` (R1) joins `SENSE_3`'s band, the rest of that column stays in
  `SENSE_1`'s. Bands hold 22 (incl. both calibration channels) / 14 / 16 / 24.

That pot's wire runs to the neighbouring band's mux — a longer track, no
schematic change.

## 9. Guards

`host/vcv/res/test_hw_panel.py`:

- **New — the 9 mm rule.** For every pair of knobs (params and reserved) and
  deck keys, centre distance − r₁ − r₂ ≥ 9.0 (tolerance 1e-6), with the cap
  radii of §2 **written into the test**, not imported from `BODY_R`, so a
  later generator edit cannot loosen it. Asserts it saw 74 knob positions,
  14 big, and both deck keys. **RED proven once** by moving one knob 0.1 mm
  towards a neighbour.
- **New — the cell table is the source.** Every knob and deck key equals its
  cell's raster coordinate; big caps only in R2/R4 and never in c6.
- **New — fields keep `BOX_GAP`** between groups (replaces
  `test_group_raster_closes`), and every control's ink sits inside its own
  field.
- **Removed** (they pin coordinates or rules the raster replaces):
  `test_row3_ceiling_holds_at_seventy`, `test_level_band_is_evenly_divided`,
  `test_level_band_clears_rooms_shoulder`,
  `test_level_band_holds_pan_in_slot_zero`,
  `test_middle_band_runs_on_three_lines`, `test_group_raster_closes`,
  `test_rows_are_centred_on_their_ink`, and the coordinate pins in
  `test_drawing_geometry` (ENGINE at 16.25, the VOICE/TIMING figure, TIME/LINK
  at 89.86, DECY/TONE at 79.0/97.0 — layout pins, not by-ear decisions;
  `docs/by-ear-decisions.md` has no entry for them — and the GLOBAL row's
  13.0 mm pitch). Each removal is named in its commit message.
- **Rewritten:** `test_led_inventory_after_the_feedback_round` to §5 (17
  lamps, the six removed absent, the new four present, `CLK_L` not `SYNC_L`);
  `test_satellite_lamps_clear_their_anchor_hole` to `MODBTN_L`,
  `SHIFTBTN_L`, `CEIL_L`, `CLK_L`, `RST_L`, `IN_LVL_L`;
  `test_size_classes_match_the_spec` and `test_hw_only_inventory` to the
  reserved four; every jack-row y check reads `JACK_Y`.
- **Kept, unchanged in intent:** 60 HP, rail keep-out, mirror symmetry (also
  of captions), same runtime params in the same order as the big panel,
  footprints, captions clear of neighbours and of their own control, lamp
  clusters, satellite webs, SD cutout, legend notches, Rack widgets not
  burying legends, committed files match the generator.

Downstream guards change their pinned counts with reasons: 74 pot rows and
35 safe rows plus 4 reserved in `shell/test_gen_panel_map.py` and
`tests/test_controls_map.cpp`; 17 LED bits in the Rev A chain profile
(`tests/test_mux_plan.cpp`); `test_assign.py` to the relaxed split, with a
sabotage that forbids an unnecessary column split.

## 10. Downstream chain, in order

The board is the real risk, so it runs right after the generator and before
the drawing is polished.

1. **Generator, cut, guards** (§3–§7, §9) — positions, reserved knobs,
   lamps, jack row, fields, keys; `gen_hw_cut.py` regenerates the hole list,
   cut file and print sheet.
2. **Assignment** (§8) — `assign.py` relaxed, `panel-map.json` regenerated.
3. **Schematic** — `blocks.py`'s shift-register chain carries 17 LED nets
   (`LED0..LED16`) and two more `SR_SPARE` outputs; `build.py` places 74 pots
   from the panel map; the check and ERC stay green.
4. **Board** — the SONG lamp probe (§5.1) first; then `place.py` →
   `route.py` → `route_check.py` → P4-3's export and `reva_fab_guard`.
   Gate: `place_check.KNOWN_PANEL` empty; `route_check.KNOWN_PANEL` empty but
   for `"audio"`, which holds exactly the three admitted jack-zone pairs
   (§5.2); `test_place.py` / `test_route.py` assert exactly that. New pot positions near back-side parts may need
   `OVERRIDES` in `place.py`, as P4-1/P4-2 did. If the router does not
   close, that is a stop-and-report, not a reason to move knobs back.
5. **Firmware table** — `shell/gen_panel_map.py` gains a `RESERVED` table
   (the four ids, sending nothing, no `generated_hw_panel.hpp` lookup);
   `generated_panel_map.h` regenerates; `cmake --build` and `ctest` in
   **Release**.
6. **VCV** — the LightId changes reach `led_law.hpp` (removed ids gone,
   `SYNC_L` → `CLK_L`); new lamps may stay dark in this pass but must draw in
   the right place. `host/vcv/build-local.sh install`, restart Rack, and a
   headless render (`Rack.exe -u <throwaway> -t 2`) shows the panel.
7. **Docs** — `docs/hardware/grip-test.md`: items 1–2 failed with the 5.3 mm
   measurement and this spec as the fix, item 3 passed; `docs/roadmap.md` M6
   entry; the follow-up spec for §4's parameters listed under "Planned".

**Bastian's part:** order the correction cut at Formulor (budgeted in P1 §2,
~20 €) from the new `FireflowHW-cut.svg`, then rerun grip-test items 1–2 and
4. The freeze tag waits for that pass.

## 11. Risks

- **Router does not close** on the new pot positions (§10.4). It runs right
  after the generator, so a failure costs one task, not a redrawn plate.
- **The SONG lamp has no measured position yet** (§5.1). If neither candidate
  clears the pads, that comes back to Bastian before the generator moves.
- **Field margin 1.45 mm is tight by construction** (§7): it is exactly what
  the worst pair allows. Any later lamp over a big cap breaks it; the field
  guard catches that.
- **Cap sizes are datasheet/part-listing values.** If a caliper on the
  delivered caps says otherwise, the guard's radii change and the raster is
  re-checked before the cut is ordered.
- **Spare caps run to zero** for green and dark blue (§4). A lost cap means a
  reorder; the reserved positions can go capless meanwhile.
