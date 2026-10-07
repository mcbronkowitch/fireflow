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

19 lamps drawn today, **15** after.

| Lamp | Where | Shows | Status |
|---|---|---|---|
| `SONG_A_L` / `_B_L` | **beside** SONG, inboard (§5.1) | flash on a phrase change | unchanged meaning; leaves the caption cluster |
| `GATE_A_L` / `_B_L` | under ATTACK (cluster) | a note is sounding (`inst.gate()`) | unchanged |
| `LVL_A_L` / `_B_L` | under COMP (cluster) | **this deck delivers signal at all** — lit while the deck's own output into the mix is above a floor, dark when nothing comes out (cutoff all the way down, LVL at zero, an empty sampler) | **new meaning** (was the `LANE_LEVEL` excursion, i.e. modulation). Tap and floor: a P6b / VCV-host probe |
| `FTIME_A_L` / `_B_L` | under FLUXRATE, caption TIME (cluster) | one flash per FLUX time period — the repeat tempo TIME sets — **only while that deck's FLUX MIX > 0** | **new**. FLUX is a tempo-synced delay and TIME picks its division (`set_flux_rate(slice_idx)`), so this is timing; gating on MIX keeps it from doubling `TEMPO_L` at a quarter note and says FLUX is in the sound |
| `REC_A_L` / `_B_L` | under the REC key (cluster) | unchanged | **moved** from a hand-placed satellite (108.5, 14.5) into a word-then-LED cluster like the knob lamps: beside the key it reaches 0.25 mm out of its cell and breaks the 3 mm field gap to GLOBAL or SEQUENCE (§7) |
| `TEMPO_L` | under TEMPO (cluster) | the transport beat | unchanged |
| `CLK_L` | jack-row satellite of CLOCK, inboard (129.30) | a flash on every pulse arriving at the CLOCK jack | **renamed from `SYNC_L`** (the COUPLE knob's printed caption is also SYNC and has no lamp; the two were confused in review) and **given a meaning** — today it is held dark. Not redundant with `TEMPO_L`: with PACE in use the beat and the incoming pulse run at different rates |
| `RST_L` | jack-row satellite of RESET, outboard (168.80 + 6.7 = 175.50) | a flash on every reset received | **new**; mirrors `CLK_L` about the centre line |
| `SHIFTBTN_L` | jack row, **centred between the SHFT key and IN L**: x = (14.00 + 33.00) / 2 = 23.50 | **two jobs** (Bastian, 2026-10-07): steady while SHIFT is latched; otherwise the **input signal**, the peak of IN L and IN R together. On top of either, one pulse when a **new engine** (either deck) or a **new scale** has actually taken effect — at the switch, not when the knob turns — which must read clearly against the level display and against MOD's double pulse; patterns are P6b's | **moved** (was a satellite at 20.70) and **given its meanings** (held dark today). Replaces the separate IN lamp first planned here, saving one LED. 9.50 mm to both anchors: 4.85 mm of material to the SHFT key's hole, 4.95 to IN L's |
| `MODBTN_L` | jack row, **centred between OUT R and the MOD key**: x = (271.80 + 290.80) / 2 = 281.30 | **two jobs** (Bastian, 2026-10-07): MOD's double pulse while the MOD latch is engaged; otherwise the **master limiter** bending (what `CEIL_L` showed). While MOD is latched the limiter is not shown — accepted | **moved** (was a satellite at 284.10) and takes over `CEIL_L`, saving one LED; mirrors `SHIFTBTN_L` about the centre line |

**Removed** (lane excursions, i.e. modulation): `SRC_A_L`/`_B_L` (TIMB),
`FLT_A_L`/`_B_L` (FILT), `CLR_A_L`/`_B_L` (COLR). **Folded into `MODBTN_L`:**
`CEIL_L`. **Never added:** the separate IN lamp (`IN_LVL_L`), folded into
`SHIFTBTN_L` the same day.

**Asked for and dropped the same day:** a STEPS lamp (a step-advance flash
differs from the gate lamp only on rests and held notes — so spec 2026-08-16
song-phrase-flash S4 stands, `FLOW_*` stay undrawn and
`test_steps_has_no_lamp_on_the_hw_plate` stays); TIDE and PACE lamps (TIDE
scales the texture lanes' rate, PACE the modulator's base rate — both would
show modulation speed).

### 5.1 The SONG lamp sits beside its knob

A cluster LED sits ~7 mm under its control, and the **top row's pots have
their pins south** (`POT_TOP_ROT` = 90 in `place.py`), right where that LED
lands. P4-1 measured that the SONG lamp needed a 2.6 mm drop at LED rotation
0 for 0.51 mm pad clearance (P4-1 placement spec, SONG item). SONG is the only
top-row knob lamp (REC is a key; FTIME, TEMPO and the others sit in R3/R4,
where their own pot's pins point north, away from them).

**A drop is ruled out by the fields** (§7): the SEQUENCE band's bottom may sit
at most 24.175 (MOTION's band starts at `MOD`'s cap, 28.625 − 1.45, minus
`BOX_GAP`), and today's cluster already puts it at 24.110 — 0.065 mm of room.

**So the SONG lamp stands beside its knob, inboard, on the knob's line:**
x = SONG.x + 18.85 / 2 = 38.975 on deck A (mirrored on B), y = 14.50,
halfway to STEPS; the SONG caption stays centred under the knob. Computed
from KiCad's `Potentiometer_Alpha_RD901F-40-00D_Single_Vertical`: at rotation
90 the support lugs sit 4.8 mm left and right of the shaft, pads 2.72 × 3.24,
so their outer edge is 6.16 mm out; an LED with its pads stacked vertically
at 9.425 mm keeps **2.36 mm** pad to pad to both SONG's and STEPS's lugs.
Plate web to SONG's hole: 4.38 mm. The board task confirms it with
`place_check.py`; if that disagrees with this arithmetic, the board task
stops and reports.

### 5.2 The jack-row lamps sit in jack zones — admitted

`route_check.py`'s audio step flags every LED pad within 10 mm of an audio
jack's tip pad: the 595-driven LED lines are aggressors, the jack a victim.
Bastian admitted two such pairs on 2026-09-30, `CEIL_L/OUT_R` and
`IN_L/SHIFTBTN_L`. A lamp that shows the input beside IN cannot keep 10 mm,
and neither can the combined lamps of §5 (9.50 mm from their jack's centre,
their pads closer still). So after the pass **exactly two pairs stay
admitted** (Bastian, 2026-10-07): `IN_L/SHIFTBTN_L` (unchanged key) and
`MODBTN_L/OUT_R` (replaces `CEIL_L/OUT_R`). Combining the lamps is what keeps
it at two rather than three. Whether they actually couple is measured on the
first Rev A board with the coupon's crosstalk rig, not argued away now. The
`"audio"` set and `test_route.py`'s `JACK_ZONES` hold these two keys, with
whatever aggressor pads the audio step finds for them on the new board.

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
  ink ± `FIELD_MARGIN` vertically, the existing x cuts. The two keys and
  their centred lamps stay loose, as the keys and their lamps are today
  (`SHIFTBTN_L`'s edge at 25.00 keeps 3.00 mm to IN's frame at 28.00).
  Its top (108.20) keeps 4.3 mm to the knob fields above.
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
- **Rewritten:** `test_led_inventory_after_the_feedback_round` to §5 (15
  lamps; the six removed and `CEIL_L` absent; `FTIME_*`, `RST_L` present;
  `CLK_L` not `SYNC_L`); `test_satellite_lamps_clear_their_anchor_hole` to
  `CLK_L` and `RST_L` at `SAT_D`, plus `SHIFTBTN_L` and `MODBTN_L` centred
  between their key and jack with `MIN_WEB` to both holes;
  `test_size_classes_match_the_spec` and `test_hw_only_inventory` to the
  reserved four; every jack-row y check reads `JACK_Y`.
- **Kept, unchanged in intent:** 60 HP, rail keep-out, mirror symmetry (also
  of captions), same runtime params in the same order as the big panel,
  footprints, captions clear of neighbours and of their own control, lamp
  clusters, satellite webs, SD cutout, legend notches, Rack widgets not
  burying legends, committed files match the generator.

Downstream guards change their pinned counts with reasons: 74 pot rows and
35 safe rows plus 4 reserved in `shell/test_gen_panel_map.py` and
`tests/test_controls_map.cpp`; 15 LED bits in the Rev A chain profile
(`tests/test_mux_plan.cpp`); `test_assign.py` to the relaxed split, with a
sabotage that forbids an unnecessary column split.

## 10. Downstream chain, in order

The board is the real risk, so it runs right after the generator and before
the drawing is polished.

1. **Generator, cut, guards** (§3–§7, §9) — positions, reserved knobs,
   lamps, jack row, fields, keys; `gen_hw_cut.py` regenerates the hole list,
   cut file and print sheet.
2. **Assignment** (§8) — `assign.py` relaxed, `panel-map.json` regenerated.
3. **Schematic** — `blocks.py`'s shift-register chain carries 15 LED nets
   (`LED0..LED14`) and four more `SR_SPARE` outputs; `build.py` places 74 pots
   from the panel map; the check and ERC stay green.
4. **Board** — `place.py` →
   `route.py` → `route_check.py` → P4-3's export and `reva_fab_guard`.
   Gate: `place_check.KNOWN_PANEL` empty; `route_check.KNOWN_PANEL` empty but
   for `"audio"`, which holds exactly the two admitted jack-zone pairs
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
- **The SONG lamp's clearance is arithmetic from the footprint file** (§5.1),
  not yet a `place_check` run. If the board disagrees, that comes back to
  Bastian before anything else moves.
- **Field margin 1.45 mm is tight by construction** (§7): it is exactly what
  the worst pair allows. Any later lamp over a big cap breaks it; the field
  guard catches that.
- **Cap sizes are datasheet/part-listing values.** If a caliper on the
  delivered caps says otherwise, the guard's radii change and the raster is
  re-checked before the cut is ordered.
- **Spare caps run to zero** for green and dark blue (§4). A lost cap means a
  reorder; the reserved positions can go capless meanwhile.
