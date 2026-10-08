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
board follows the plate. The distance from the knobs to the jacks stays as it
is: plugged cables are in the way of turning knobs.

This is also **the one panel pass** that P4-1 left its known panel violations
for (decided 2026-10-04): the jack row's height and the LED rework travel with
the knob raster, so the scripted chain to the board runs once.

## 1. Goal and scope

*Amended 2026-10-08 after Task 7a:* the smallest gap is 9.70 mm since the
centre re-pitch (§3); it was 10.27.

Every knob and key on the 60 HP plate keeps at least 9.0 mm between cap
edges, measured with the real caps — and the layout below delivers at least
9.70 mm. Deck B stays deck A mirrored; the centre column is mirror-symmetric
about the plate's centre line. The plate stays 60 HP.

Done means: the generator places every control from one cell table; a guard
proves the 9 mm rule and has been seen red once; the hole list, cut file,
print sheet, panel map, placed and routed Rev A board, order package,
generated firmware table and the VCV `FireflowHW` panel all come from the new
positions; every gate along that chain is green; and the board's
`KNOWN_PANEL` exemption sets (`place_check.py`, `route_check.py`) are empty
— except `route_check.py`'s `"audio"` set, which holds exactly the two
admitted jack-zone pairs (§5.3).

**In scope:** the knob raster (§3), three reserved knob positions (§4), the
LED inventory (§5), the jack row's height (§6), the group fields (§7), the
mux assignment rule (§8), and the chain to the board and the firmware (§10).

**Not in scope:** jack x positions and pitch (patching passed); the big VCV
module `Fireflow`; the engine; plate colours; key functions. **How** the LEDs
light is P6b's LED law and the VCV host's light code — §5 fixes what each one
*means*, this pass gives it a LightId, a position, a hole and a footprint.
**Making the reserved knobs real parameters** is a follow-up spec (§4).

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

**The new plate** (§3, §4): 73 knob positions and 2 deck keys. **Smallest gap
10.27 mm** (a big cap over a small one, vertically; 9.70 since the centre
re-pitch of 2026-10-08, §3). Small beside small:
12.50 mm across, 12.43 mm down; big beside small across: 10.35 mm; median
nearest-neighbour gap 10.35 mm. Caps span x 5.00 .. 299.80 (4.8 mm to each
edge of the 304.4 mm plate). Lowest cap edge 98.85; lowest knob ink (a small
caption) 102.45. Jack-row keys to the nearest cap: 11.15 mm. Nearest jack
centre to a cap edge: 13.95 mm.

Rejected on the numbers: stretching today's groups (their knobs stand on
seven to eight heights; at ≥ 16.7 mm vertical pitch the plate holds five to
six rows); moving the jacks to widen the plate (Bastian's fallback) — not
needed; lowering the bottom row toward the jacks (≤ 1.3 mm, worth 0.3 mm of
row pitch, and the cables want that room).

## 3. The raster

*Amended 2026-10-08 after Task 7a (Bastian's decision):* the centre runs on
its own pitch, **23.0 mm**; the outer centre columns move from 132.20 /
172.60 to **129.40 / 175.40**, column 0 stays at 152.40, the deck columns do
not move, the mirror symmetry about 152.4 holds. Why: on the uniform
20.2 mm pitch the Patch Submodule (U_SM, back side, THT sockets that stay
THT, pins reaching the front) had **no legal spot anywhere on the board**
— its headers A and D stand 28.0 mm apart against a 20.125 mm row pitch, and
the windows between pot lugs missed by 3.75 mm (Task 7a's whole-board
scan). Measured with the board placer's own tests at 23.0: U_SM fits at
rotation 90/270 with its centre at x 152.00 .. 152.80, never at 0/180;
`place.py` puts it at (152.40, 69.75) rot 90 (P4.1 §4.3 amendment of the
same day admits 90/270). Cost on the plate, measured: deck column 6 to the
outer centre column is 17.40 mm centre to centre, so the **smallest cap-edge
gap is 9.70 mm**, above the 9.0 rule — the eight small-beside-small pairs
across that seam (R2–R5 on both sides, e.g. COUPLE/PAN_A); every other pair
keeps ≥ 10.0 mm. Median nearest-neighbour gap over the
73 knobs and 2 deck keys 10.28 mm. Unchanged: caps span x 5.00 .. 299.80,
jack-row keys 11.15 mm from the nearest cap, nearest jack centre 13.95 mm
from a cap edge.

| | Value | Why |
|---|---|---|
| Column pitch | **20.20 mm** in the decks, **23.00 mm** in the centre | 15 columns (6 + 3 + 6) centred on 152.4; 7 × 20.2 + 6.0 = 147.4 puts deck A's outer big cap 5.0 mm from the nominal edge; the centre pitch gives U_SM its spot (above) |
| Row pitch | **20.125 mm** | rows 14.5 → 95.0 in four steps |
| Rows R1..R5 | y = 14.500, 34.625, 54.750, 74.875, 95.000 | top row as today |
| Deck A columns c1..c6 | x = 11.00, 31.20, 51.40, 71.60, 91.80, 112.00 | x = 152.4 − (8 − c) · 20.2 |
| Deck B | x → 304.8 − x | mirror axis 152.4, as today |
| Centre columns k = −1, 0, +1 | x = 129.40, 152.40, 175.40 | x = 152.4 + k · 23.0 (was k · 20.2) |

**Big caps stand in R2 and R4 only, never in deck column c6, and never next
to another big cap in the same row.** Two orthogonally adjacent big caps would
need 21.0 mm against 20.2 / 20.125, so this is load-bearing. Keeping R1 and R5
small is what gives the jack row (§6) and the group fields (§7) their room:
the lowest ink is a small caption at 102.45.

The centre had 16 controls for 15 cells until Bastian dropped the `REV_SMEAR`
reservation (§4); three centre columns are what let the whole plate run on
one 20.2 mm pitch instead of 18.85. (Since 2026-10-08 the centre runs wider,
23.0 mm, for the module — see the amendment above.)

### 3.1 Deck A (c1 = outer edge; deck B mirrored; ● = big; *italics* = reserved, §4)

| Row | c1 | c2 | c3 | c4 | c5 | c6 |
|---|---|---|---|---|---|---|
| R1 | `ENGINE` | `STEPS` | `SONG` | `RATE` | `MELODY` | `REC` (key) |
| R2 | `MOD` ● | `SHAPE` | `DENSITY` ● | `SOURCE` | `FILT` ● | `DEPTH` |
| R3 | `SMOOTH` | `RANGE` | `ATTACK`/`STAGES` | `DECAY` | `RES` | `SUB` |
| R4 | `COLOR` ● | `TUNE` | `FLUX` ● | `FLUXRATE` | `COMP` ● | `PAN` |
| R5 | *`ROOT_A`* | `DETUNE` | `FLUXFB` | `LINK` | `GRIT` | `REV_MIX` |

Groups, all contiguous: ENG (R1 c1), SEQUENCE (R1 c2–c5, today's order STPS
SONG RATE VARY), CAPTURE (R1 c6), MOTION (R2 c1–c3 + R3 c1–c2), VOICE (R2
c4–c6 + R3 c3–c6), PITCH (R4–R5 c1–c2), FLUX (R4–R5 c3–c4), LEVEL (R4–R5
c5–c6). VOICE reads TIMB FILT DPTH over ATK DEC RES SUB: RES stands under
FILT, ATK and DEC stay side by side. SEND (`REV_MIX`) sits at the inner bottom
corner, next to ROOM.

### 3.2 Centre (mirror-symmetric)

| Row | k = −1 | 0 | +1 | Groups |
|---|---|---|---|---|
| R1 | `SCALE` | `CHOKE` | `PULL` | GLOBAL |
| R2 | `TIDE` | `MORPH` ● | `PACE` | TIMING |
| R3 | `TEMPO` | `REV_SIZE` | `SHUFFLE` | TIMING · ROOM · TIMING |
| R4 | `COUPLE` | `REV_DECAY` ● | `DRIFT` | TIMING · ROOM · TIMING |
| R5 | `REV_DIFF` | `REV_TONE` | *`REV_MOD`* | ROOM |

TIMING is an arch — R2 whole, then two legs down k = ±1 to R4 — and ROOM an
inverted T inside it (the stem R3–R4 at k = 0, the foot R5). **DRIFT moves from
GLOBAL to TIMING** and stands opposite COUPLE (printed SYNC): SYNC pulls the
decks together, DRIFT pushes them apart, so the two form the mirrored pair.
Both big caps stay on the centre line. Approved by Bastian 2026-10-07 from a
1:1 preview.

## 4. Reserved knob positions

Three free cells get a pot now, because a pot added after the board is
ordered costs a board. Bastian chose (2026-10-07) to reserve the hardware in
this pass and make the parameters real in a follow-up spec:

| Id | Cell | Caption | Becomes |
|---|---|---|---|
| `ROOT_A` / `ROOT_B` | R5 c1, each deck | `ROOT` | a per-deck scale root, 12-step detent. Today `P_ROOT` is one global param that sets both decks and **the VCV host never sets it** — both decks always play in C. `quant().set_root()` is already per deck in the engine, so the follow-up splits `P_ROOT` into `ROOT_A`/`ROOT_B`. Not redundant with TUNE, which shifts pitch continuously instead of changing key. |
| `REV_MOD` | centre R5 k=+1 | `WOBL` | the reverb's tail-delay LFO depth (`P_REV_MOD`, `set_mod_depth`) |

`REV_SMEAR` was reserved too and **dropped the same day** to free a centre
cell (§3): its constant was confirmed by ear on 2026-08-09 ("smear ist ok").
For `REV_MOD`, **`docs/by-ear-decisions.md` says the opposite** ("do not
propose giving SMEAR or WOBL a knob back", host constant 0.15). Bastian
reversed that for WOBL on 2026-10-07; this pass only reserves the hardware, so
the by-ear entry changes in the follow-up, where the knob boots at 0.15 so the
sound does not move.

In this pass the three are **`HwOnly` entries** in `gen_hw_panel.py` (like
`SHIFTBTN`): a hole, a caption and a pot on the plate and the board, a mux
channel, a row in the firmware table that sends nothing — and no Rack widget,
no ParamId, no change to the big panel. Parts: 73 pots against 77 bought;
small caps 59 against 61 bought (part A 23 of 23 green, centre 13 of 14 dark
blue, part B 23 of 24 orange); big caps unchanged.

## 5. LEDs

*Amended 2026-10-08 after Task 7a:* every knob lamp, SONG's included, is
one caption cluster by the rule of §5.1 (word centred, LED beside it on its
glyph midline toward its own group, deck B mirrored). "Cluster" in the table
below means that rule.

19 lamps drawn today, **15** after.

| Lamp | Where | Shows | Status |
|---|---|---|---|
| `SONG_A_L` / `_B_L` | beside the SONG word (cluster, §5.1) | flash on a phrase change | unchanged meaning; a cluster lamp again since 2026-10-08 (beside the knob 2026-10-07) |
| `GATE_A_L` / `_B_L` | under ATTACK (cluster) | a note is sounding (`inst.gate()`) | unchanged |
| `LVL_A_L` / `_B_L` | under COMP (cluster) | **this deck delivers signal at all** — lit while the deck's own output into the mix is above a floor, dark when nothing comes out (cutoff all the way down, LVL at zero, an empty sampler) | **new meaning** (was the `LANE_LEVEL` excursion, i.e. modulation). Tap and floor: a P6b / VCV-host probe |
| `FTIME_A_L` / `_B_L` | under FLUXRATE, caption TIME (cluster) | one flash per FLUX time period — the repeat tempo TIME sets — **only while that deck's FLUX MIX > 0** | **new**. FLUX is a tempo-synced delay and TIME picks its division (`set_flux_rate(slice_idx)`), so this is timing; gating on MIX keeps it from doubling `TEMPO_L` at a quarter note and says FLUX is in the sound |
| `REC_A_L` / `_B_L` | beside the REC word, under the key (cluster, §5.1; `LED_DX_KEY` 6.0) | unchanged | **moved** from a hand-placed satellite (108.5, 14.5) into the caption cluster like the knob lamps, so it stays inside CAPTURE's field (§7) |
| `TEMPO_L` | under TEMPO (cluster) | the transport beat | unchanged |
| `CLK_L` | jack-row satellite of CLOCK, inboard (129.30) | a flash on every pulse arriving at the CLOCK jack | **renamed from `SYNC_L`** (the COUPLE knob's printed caption is also SYNC and has no lamp; the two were confused in review) and **given a meaning** — today it is held dark. Not redundant with `TEMPO_L`: with PACE in use the beat and the incoming pulse run at different rates |
| `RST_L` | jack-row satellite of RESET, outboard (168.80 + 6.7 = 175.50) | a flash on every reset received | **new**; mirrors `CLK_L` about the centre line |
| `SHIFTBTN_L` | jack row, **centred between the SHFT key and IN L**: x = (14.00 + 33.00) / 2 = 23.50 | **two jobs** (Bastian, 2026-10-07): steady while SHIFT is latched; otherwise the **input signal**, the peak of IN L and IN R together. On top of either, one pulse when a **new engine** (either deck) or a **new scale** has actually taken effect — at the switch, not when the knob turns — which must read clearly against the level display and against MOD's double pulse; patterns are P6b's | **moved** (was a satellite at 20.70) and **given its meanings** (held dark today). Replaces a separate IN lamp, saving one LED. 9.50 mm to both anchors: 4.85 mm of material to the SHFT key's hole, 4.95 to IN L's |
| `MODBTN_L` | jack row, **centred between OUT R and the MOD key**: x = (271.80 + 290.80) / 2 = 281.30 | **two jobs** (Bastian, 2026-10-07): MOD's double pulse while the MOD latch is engaged; otherwise the **master limiter** bending (what `CEIL_L` showed). While MOD is latched the limiter is not shown — accepted | **moved** (was a satellite at 284.10) and takes over `CEIL_L`, saving one LED; mirrors `SHIFTBTN_L` about the centre line |

**Removed** (lane excursions, i.e. modulation): `SRC_A_L`/`_B_L` (TIMB),
`FLT_A_L`/`_B_L` (FILT), `CLR_A_L`/`_B_L` (COLR). **Folded into `MODBTN_L`:**
`CEIL_L`. **Never added:** a separate IN lamp, folded into `SHIFTBTN_L` the
same day.

**Asked for and dropped the same day:** a STEPS lamp (a step-advance flash
differs from the gate lamp only on rests and held notes — so spec 2026-08-16
song-phrase-flash S4 stands, `FLOW_*` stay undrawn and
`test_steps_has_no_lamp_on_the_hw_plate` stays); TIDE and PACE lamps (TIDE
scales the texture lanes' rate, PACE the modulator's base rate — both would
show modulation speed).

### 5.1 The caption cluster: the LED beside its centred word

*Amended 2026-10-08 after Task 7a (Bastian's decision, from a 1:1
preview):* this section replaces "The SONG lamp sits beside its knob". The
side lamp is gone; SONG is a cluster lamp like the others.

Why: Task 7a's board run found seven LED bodies overlapping their owner's
body (GATE ×2, FTIME ×2, TEMPO, REC ×2; Task 7a report §3) — the old
word-then-LED block, centred on the knob, put the LED only 2.05–3.24 mm
beside the knob's x. Dropping the LEDs instead was considered and withdrawn;
Bastian rejected that look and the SONG side spot.

**The rule** (`caption_led_cluster` in `gen_hw_panel.py`):

- The word is centred under its knob (`cap_x = knob.x`), at the one
  `CAPTION_GAP` every caption keeps (its y did not change).
- The LED centre sits on the word's glyph midline, at
  `knob.x ± LED_DX`: **6.9 mm** for a pot owner, **6.0 mm**
  (`LED_DX_KEY`) for the REC key.
- The LED stands on the side of its own group, never into a neighbour's:
  deck A puts ATTACK's, COMP's, SONG's and REC's lamp right of the word and
  FLUXRATE's left; TEMPO's (centre, no partner) right. **Deck B mirrors**
  (the signs flip) — this replaces the 2026-10-07 rule "same reading order
  on both decks, not an optical mirror".
- No outward slide any more: every lamp keeps ≥ 3.70 mm of acrylic to its
  owner's hole (REC; the pot lamps 4.54, LVL 6.14), against P1's `MIN_WEB`
  of 2.0.

**Board-probed** (2026-10-08, `place.py`'s footprints and `LED_ROTS`, the LED
on its word's glyph midline, slid sideways): the LED body clears every front
body and its pads clear every foreign body and keep `PAD_CLEAR` to every
foreign pad, at some LED rotation, from |dx| **5.70** (ATTACK; FLUXRATE_A,
left), **5.75** (TEMPO; FLUXRATE_B, right), **5.90** (REC key) and **6.75**
(SONG, on its own pot whose pins point south in the top row; 6.80 for
SONG_B on its right, the side it does not take). COMP's lamp (dy 8.808) was clear at every probed dx — 0.00, the old
cluster's 2.05 and the placed 6.90 — at every LED rotation. The guard pins
these as floors under `LED_DX`, the stricter side where two were measured
(FLUXRATE 5.75, SONG 6.80).

On the placed board (2026-10-08, panel + U_SM + J_PWR) the seven Task 7a
overlaps and every SONG finding are gone. The LED's body box is asymmetric
about its hole (−1.55 .. +1.95 mm at rotation 0), so REC's 5.90 holds only
with the flat side toward the key (rotation 0 right of the key, 180 left of
it; 6.30 at any rotation). The first board run still showed `REC_B/REC_B_L`
(0.295 × 0.487 mm), because `place_panel` took the first rotation whose
*pads* fit — 0 on both decks. *Resolved the same day (Bastian):* the LED
rotation pick also keeps the LED's own body off every front body (P4.1 §4.2
amendment), so REC_B_L takes 180, no other LED changes rotation, and the
front check reports 0 violations. (Raising `LED_DX_KEY` to the
rotation-free 6.30 instead would have cost CAPTURE/GLOBAL their 3 mm field
gap: 2.85.)

The SONG lamp's old arithmetic (3.04 mm pad to lug beside the knob) is
history; on the board the cluster SONG lamp keeps 2.600 mm pad to pad to its
own pot at LED rotation 90.

### 5.2 Lamps over big caps

A cluster lamp hangs its LED body 0.7 mm under its caption. Where such a lamp
stands over a big cap of another group (ATTACK's over FLUX), the field margin
may be at most 1.4825 mm for the two fields to keep 3 mm (§7). That is why
`FIELD_MARGIN` is 1.45.

### 5.3 The jack-row lamps sit in jack zones — admitted

`route_check.py`'s audio step flags every LED pad within 10 mm of an audio
jack's tip pad: the 595-driven LED lines are aggressors, the jack a victim.
Bastian admitted two such pairs on 2026-09-30, `CEIL_L/OUT_R` and
`IN_L/SHIFTBTN_L`. A lamp that shows the input beside IN cannot keep 10 mm,
and neither can the combined lamps of §5 (9.50 mm from their jack's centre,
their pads closer still). So after the pass **exactly two pairs stay
admitted** (Bastian, 2026-10-07): `IN_L/SHIFTBTN_L` (unchanged key) and
`MODBTN_L/OUT_R` (replaces `CEIL_L/OUT_R`). Whether they actually couple is
measured on the first Rev A board with the coupon's crosstalk rig, not argued
away now. The `"audio"` set and `test_route.py`'s `JACK_ZONES` hold these two
keys, with whatever aggressor pads the audio step finds for them on the new
board.

## 6. Jack row

`JACK_Y` 114.00 → **112.75**. P4-1 measured that the jacks' tip pads lie past
the board edge at 114.0 and that the row must sit at y ≤ 112.77. Keys,
jack-row LEDs and the SD slot share `JACK_Y` and move with it. Jack x
positions do not move. With R5 all small the 9 mm rule does not bite here
(keys 11.15 mm from the nearest cap), so 112.75 is simply the old row raised
as little as the board allows.

## 7. Group fields

*Amended 2026-10-08 after Task 7a:* the column cell knows the centre pitch
(§3) — the cell-rect and join rules below; SONG's lamp is a cluster lamp
(§5.1). Measured on the new plate: every pair of fields of different groups
keeps ≥ 3.000 mm, `FIELD_MARGIN` 1.45 still holds (1.4825 is still the
largest that keeps 3.0, 1.485 gives 2.997), deck B's fields are deck A's
mirrored.

A group's field is built from its cells, no longer from the fixed
`GROUP_ROWS` cut raster:

- **Cell rect, x:** the union of the control's ink ± `FIELD_MARGIN` and its
  column cell. A deck column's cell is x ± (20.2 − 3.0) / 2 = ± 8.6, on both
  sides (deck column c6 too). Between the centre columns the cell is
  ± (23.0 − 3.0) / 2 = ± 10.0. The outer centre columns' **outward** side
  (toward the deck) has no column-cell minimum — ink + `FIELD_MARGIN` only:
  deck column c6 stands 17.40 mm away there, and a 10.0 cell would meet its
  field. The column cell is what gives a
  one-control group (ENG, CAPTURE) room for its legend; CAPTURE's 17.2 mm
  holds its legend at today's `LEGEND_INSET` of 4.0 (needs 16.55).
- **Band, y:** per group and row, from the topmost ink − `FIELD_MARGIN` to the
  bottommost ink + `FIELD_MARGIN` of that group's controls in that row. Ink is
  bodies, captions, and lamps (a cluster LED hangs 0.7 mm under its caption
  and counts as its owner's ink).
- **Field:** the union of the group's cell rects, stretched to their row's
  band, plus the joins between cells that are neighbours in the same group
  (same row, adjacent columns — neighbouring columns in the one list of
  fifteen, whatever their pitch; or same column, adjacent rows). The outline is
  any rectilinear polygon — TIMING's arch is not a rectangle or an L — drawn
  with the existing rounded-corner and legend-notch rules. The legend sits on
  the field's top-left edge as today.
- **`FIELD_MARGIN` = 1.45 mm** (today 2.15 vertically). Measured: with 1.45
  every pair of fields of different groups keeps ≥ 3.00 mm (`BOX_GAP`); at
  1.50 VOICE's band comes to 2.97 from FLUX and LEVEL (§5.2).
- **Jack row:** keeps its own frames (IN, CV, MOD, CLOCK, OUT; no legends),
  ink ± `FIELD_MARGIN` vertically, the existing x cuts. The two keys and their
  centred lamps stay loose, as the keys and their lamps are today
  (`SHIFTBTN_L`'s edge at 25.00 keeps 3.00 mm to IN's frame at 28.00). Its top
  (108.20) keeps 4.3 mm to the knob fields above.
- **Keys drawn round:** the plate draws a key as its real 6 mm round cap
  (r = `BODY_R["P"]` = 3.0), not an 8 mm square; `kFfPadR` follows. It is
  also used by the big module's ENGINE latch, so that widget gets a look.

## 8. Mux assignment

10 muxes × 8 channels = 80. Today 70 pots + 2 calibration = 72. After: 73 + 2
= **75**, 5 spare.

`assign.py` splits the pots into four contiguous x-bands (24 / 16 / 16 / 24
channels, `SENSE_0, SENSE_2, SENSE_3, SENSE_1`) and **never splits a column**.
With `ROOT_A`/`ROOT_B` that has no solution — measured: a deck carries 29
pots and its c1–c5 hold 25 against a 24-channel band. The rule is relaxed:

- Pots are ordered by (x, y, id) and the band cuts may fall anywhere in that
  order. The score is (number of split columns, fullest band's fill ratio,
  cuts), lowest wins — a split is taken only when no split-free answer
  exists.
- Measured on the new plate: **one split**, at x = 213.00 (deck B c5):
  `MELODY_B` (R1) joins `SENSE_3`'s band, the rest of that column stays in
  `SENSE_1`'s. Bands hold 22 (incl. both calibration channels) / 14 / 15 / 24.

That pot's wire runs to the neighbouring band's mux — a longer track, no
schematic change.

## 9. Guards

*Amended 2026-10-08 after Task 7a* (`test_hw_panel.py`; each change proven
red once against a generator sabotaged in memory):

- **Rewritten:** `test_knob_lamps_sit_in_the_caption_cluster` to §5.1 — the
  word centred under its knob at the class offset, the LED on the glyph
  midline at `knob.x ± LED_DX` (`LED_DX_KEY` for REC), the side per group
  written into the test (deck B mirrored), the board-probed dx floors of
  §5.1 written into the test, the LED inside its owner's field, 11 lamps.
- **Replaced:** `test_song_lamp_stands_beside_its_knob` by
  `test_song_lamp_is_a_cluster_lamp` (SONG's lamps are `KNOB_LAMPS`
  entries; no `SIDE_LAMPS` table).
- **Widened:** `test_mirror_symmetry` covers the cluster lamps, and
  `test_caption_mirror_symmetry` drops its cluster exception — both mirror
  now.
- **Pinned:** `test_cells_are_the_source` checks the centre columns at
  129.40 / 152.40 / 175.40, written into the test.
- **Refined:** `test_labels_stay_off_neighbour_footprints` holds a caption
  to the own-knob floor (the radius), not radius + `LBL_MARGIN`, against a
  control on its owner's own shaft — STAGES shares ATTACK's knob. With the
  word centred, ATTACK's sits 7.45 mm from that shaft like every small
  knob's word, which the margin rule (7.50) refused although STAGES is no
  neighbour. The alternative, moving every cluster word 0.865 mm away from
  its LED, was measured (fields keep 3.000) and not taken: §5.1 says the
  word is centred. The generator's own caption check (`_caption_is_clear`)
  carries the same rule, and cluster words go through it like every other
  caption.
- **New:** `test_column_cells_follow_the_centre_pitch` pins the column
  cell's half-widths as literals: 0.0 / 10.0 at 129.40, 10.0 / 10.0 at
  152.40, 10.0 / 0.0 at 175.40, 8.6 / 8.6 on the deck columns.

Guards in other places: `place.py`'s `module_shadow` and `place_check.py`'s
module step accept the turned shadow (P4.1 §4.3 amendment), still refusing
any other box. `hardware/reva/test_place.py` checks on a panel-only board
that every LED has a rotation and no LED body overlaps a front body (P4.1
§4.2 amendment).

`host/vcv/res/test_hw_panel.py`:

- **New — the 9 mm rule.** For every pair of knobs (params and reserved) and
  deck keys, centre distance − r₁ − r₂ ≥ 9.0 (tolerance 1e-6), with the cap
  radii of §2 **written into the test**, not imported from `BODY_R`, so a
  later generator edit cannot loosen it. Asserts it saw 73 knob positions,
  14 big, and both deck keys. **RED proven once** by moving one knob towards a
  neighbour until it fails.
- **New — the cell table is the source.** Every knob and deck key equals its
  cell's raster coordinate; big caps only in R2/R4, never in c6, never two
  side by side; the centre table is mirror-symmetric in k.
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
  reserved three; `test_light_accent_table`'s neutral lamps to `MODBTN_L` and
  `SHIFTBTN_L`; every jack-row y check reads `JACK_Y`.
- **Kept, unchanged in intent:** 60 HP, rail keep-out, mirror symmetry (also
  of captions), same runtime params in the same order as the big panel,
  footprints, captions clear of neighbours and of their own control, lamp
  clusters, satellite webs, SD cutout, legend notches, Rack widgets not
  burying legends, committed files match the generator.

Downstream guards change their pinned counts with reasons: 73 pot rows (35
safe, 3 reserved) in `shell/test_gen_panel_map.py` and
`tests/test_controls_map.cpp`; 15 LED bits in the Rev A chain profile
(`tests/test_mux_plan.cpp`); hole counts in `res/test_hw_cut.py`;
`test_assign.py` to the relaxed split, with a check that no column is split
when a split-free answer exists.

## 10. Downstream chain, in order

The board is the real risk, so it runs right after the generator and before
the drawing is polished.

1. **Generator, cut, guards** (§3–§7, §9) — positions, reserved knobs,
   lamps, jack row, fields, keys; `gen_hw_cut.py` regenerates the hole list,
   cut file and print sheet.
2. **Assignment** (§8) — `assign.py` relaxed, `panel-map.json` regenerated.
3. **Schematic** — `blocks.py`'s shift-register chain carries 15 LED nets
   (`LED0..LED14`) and four more `SR_SPARE` outputs; `build.py` places 73 pots
   from the panel map; the check and ERC stay green.
4. **Board** — `place.py` → `route.py` → `route_check.py` → P4-3's export and
   `reva_fab_guard`. Gate: `place_check.KNOWN_PANEL` empty;
   `route_check.KNOWN_PANEL` empty but for `"audio"`, which holds exactly the
   two admitted jack-zone pairs (§5.3); `test_place.py` / `test_route.py`
   assert exactly that. New pot positions near back-side parts may need
   `OVERRIDES` in `place.py`, as P4-1/P4-2 did. If the router does not close,
   that is a stop-and-report, not a reason to move knobs back.
5. **Firmware table** — `shell/gen_panel_map.py` gains a `RESERVED` table
   (the three ids, sending nothing, no `generated_hw_panel.hpp` lookup);
   `generated_panel_map.h` regenerates; `cmake --build` and `ctest` in
   **Release**.
6. **VCV** — the LightId changes reach `led_law.hpp` (removed ids gone,
   `SYNC_L` → `CLK_L`, the limiter folded into `MODBTN_L`); new lamps may stay
   dark in this pass but must draw in the right place.
   `host/vcv/build-local.sh install`, restart Rack, and a headless render
   (`Rack.exe -u <throwaway> -t 2`) shows the panel.
7. **Docs** — `docs/hardware/grip-test.md`: items 1–2 failed with the 5.3 mm
   measurement and this spec as the fix, item 3 passed; `docs/roadmap.md` M6
   entry; the follow-up spec for §4's parameters listed under "Planned".

**Bastian's part:** order the correction cut at Formulor (budgeted in P1 §2,
~20 €) from the new `FireflowHW-cut.svg`, then rerun grip-test items 1–2 and
4. The freeze tag waits for that pass.

## 11. Risks

- **Router does not close** on the new pot positions (§10.4). It runs right
  after the generator, so a failure costs one task, not a redrawn plate.
- ~~**The SONG lamp's clearance is arithmetic from the footprint file**~~
  (§5.1) — *resolved 2026-10-08:* the board disagreed (Task 7a), SONG
  became a cluster lamp, and its clearance is now board-probed (§5.1).
- **Field margin 1.45 mm is tight by construction** (§5.2, §7): it is what
  the worst pair allows. Any later lamp over a big cap of another group breaks
  it; the field guard catches that.
- **Cap sizes are datasheet/part-listing values.** If a caliper on the
  delivered caps says otherwise, the guard's radii change and the raster is
  re-checked before the cut is ordered.
- **Green spare caps run to zero** (§4). A lost one means a reorder; `ROOT_A`
  can go capless meanwhile.
