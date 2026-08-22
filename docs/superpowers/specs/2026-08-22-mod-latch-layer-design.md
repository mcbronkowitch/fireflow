# MOD latch layer — design

**Date:** 2026-08-22
**Status:** implemented 2026-08-22, plan
`docs/superpowers/plans/2026-08-22-mod-latch-layer.md`
**Scope:** `FireflowHW` module (VCV), engine untouched except where stated. The
`shell/` firmware inherits the pattern later; it is out of scope here.

## 1. Concept

The MOD pad on the hardware-panel draft — today print-only, no param, no
widget — becomes a **latching button with its lamp lit while active**. While
latched, every modulatable knob shows and edits its **modulation depth**
instead of its sound value. Releasing the latch returns every knob to its
sound meaning; both value sets persist in the patch.

The MOD knob keeps its existing role in both modes: **per-deck master
modulation intensity**, itself never modulatable. The factorization is the one
the engine already computes:

```
effective modulation = lane signal × depth (per target) × MOD (per deck)
```

with one standing exception: the pitch lane is the anchor and ignores the
master (`part.cpp` `_mod_term`, spec 2026-07-17 mod-tide). That exception
**stays**. Turning pitch modulation off is RANG's job — see §6, probe 1.

## 2. Decisions taken in the brainstorm

- **Fixed lane assignment.** Every modulatable parameter is bound to exactly
  one of the deck's five lanes (§4). No routing UI.
- **Init sounds exactly like today.** Targets that already carry engine depth
  keep their booted values as init; every new (host-computed) depth inits at
  0. TUNE depth 0 at init falls out of this automatically.
- **`FireflowHW` first.** The big `Fireflow` module keeps its current panel;
  new params live on the shared module but only the HW widget gets the layer.
- **Pitch anchor kept**, no listening pass needed.
- **No separate pitch-depth knob.** RANG already scales the pitch lane and
  RANG 0 silences it exactly (probe 1). One amplifier per signal.
- **Excluded from modulation** (their knobs stay live on their normal function
  while MOD is latched, matching how hardware pots will behave):
  - structural/discrete: ENG, STPS, SONG, SCAL, REC, STAGES (BBD deck's
    stage-count control, conditionally visible rather than modulatable)
  - clock: TEMP, SHFL, PACE
  - zone/action knobs: DRFT (settle at left stop), SYNC/COUPLE, GRIT
    (sign picks mode), TIME (discrete flux slices), CHOK (zones)
  - MOD itself (per-deck master)
- **Lane-group knobs are modulatable** (RATE, SHAP, SMTH, DENS, RANG, VARY) —
  self-modulation of the mod engine is allowed, deliberately.
- **Center column is modulated by both decks mixed**: source = mean of deck A
  and deck B contributions, each `lane × that deck's MOD`. Both masters down →
  the center is still.
- **Depth knobs are unipolar 0..1.** The lanes are bipolar; depth scales the
  swing around the sound value.

## 3. Two modulation paths

### 3a. Engine-backed faces (6 per deck)

Where `Part` already owns a depth slot, the depth knob writes that slot via
the existing setters — no host-side summing, no double modulation:

| Face knob | Engine slot | Setter | Init depth |
|---|---|---|---|
| TIMB | `_tdepth[LANE_SOURCE]` | `set_target_depth` | 1.0 (boot value) |
| DPTH | `_tdepth[LANE_MOTION]` | `set_target_depth` | 0.7 (boot value) |
| FILT | `_tdepth[LANE_SIZE]` | `set_target_depth` | 0.55 (boot value) |
| MIX | `_fx_depth[FXT_FX_MIX]` | `set_fx_target_depth` | 0.0 |
| FB | `_fx_depth[FXT_FLUX_FB]` | `set_fx_target_depth` | 0.0 |
| SEND | `_fx_depth[FXT_REV_SEND]` | `set_fx_target_depth` | 0.0 |

FX activation rule: `set_fx_target_active(slot, depth > 0)` — pushed alongside
the depth. All five `_fx_active` boot false, which is exactly the init-0
contract (probe 3: inactive slot's `fx_target_value` is pinned to its base,
p2p exactly 0; active at depth 1 it swings the full clamp range).

`FXT_GRIT_INT` and `FXT_FLUX_TIME` stay unused by the layer (their faces GRIT
and TIME are excluded).

FILT's engine-side additive trim on the LANE_SIZE sum
(`synth_engine.cpp` `_targets[LANE_SIZE] + off`) is untouched: in sound mode
the knob keeps trimming; in MOD mode its depth param edits the axis depth.

### 3b. Host-computed targets (everything else)

The host already pushes every parameter every block and can read the lanes
(`Instrument::lane_output`, probe 2). For each host-computed target:

```
pushed value = clamp01(knob + depth × MOD(deck) × lane_output(deck, lane))
```

computed in knob space (0..1, before the parameter's existing engine
mapping) and pushed through the parameter's existing setter. **No new engine
summing points.** For center-column targets the lane term is
`0.5 × (MOD_A × lane_output(A, lane) + MOD_B × lane_output(B, lane))`.

COLOR keeps its baked `kColorMod` MOTION swing (by-ear stock, untouched); the
COLR depth adds host-side on top. Same for the sampler overlap swing
(`kOverlapMod`) — DENS depth modulates density host-side, the overlap constant
stays as-is.

Block-rate stepping of a setter is glitch-free at the swing sizes involved:
probe 4 pushed `set_voice_filt` on a 5.33 ms grid over a ±0.35 swing and the
worst inter-sample output delta was 0.091 against 0.061 for the static twin,
with zero deltas above 0.25 in 960 000 samples. Sanity check, not a gate — no
bit-exactness gates in this repo.

## 4. Lane assignment (fixed, one table in the host, ear-tunable)

Lane tempo characters: SOURCE ×2 (fast), SIZE ×½ (slow), PITCH ×1,
MOTION ×¾, LEVEL ×1½.

| Lane | Per-deck targets | Center targets (A+B mixed) |
|---|---|---|
| SOURCE | DTUN (shimmer) | — |
| SIZE | RATE, SMTH, RANG, RES | reverb SIZE, DECY, TONE, DIFF; TIDE |
| PITCH | TUNE (vibrato/drift) | — |
| MOTION | SHAP, DENS, ATK, DEC, COLR | MRPH |
| LEVEL | SUB, VARY, LINK, LVL | — |

Plus the six engine-backed faces of §3a (TIMB/DPTH/FILT on their own lanes by
construction, MIX/FB/SEND on lanes 2/4/3 by the FX table). The table lives in
one place in `Fireflow.cpp` beside the existing push loop, so retuning an
assignment is a one-line edit.

Conditional faces follow their face, not their engine wiring: SUB and DTUN are
host-computed on every engine, including the engines where the sound knob
writes a lane base (Sampler SUB, FEED DTUN) — on those engines the base is
already lane-modulated by `_tdepth[LANE_SIZE]`, and the host-computed term
composes on top the same way it does for every other knob.

## 5. VCV mechanics

- **`MODBTN` becomes a real param.** In `gen_hw_panel.py` it moves from
  `HW_ONLY` to a latch (`WK_LATCH`, like REC); SHIFT stays reserved.
  Generator guard `res/test_hw_panel.py` moves with it. One button, global —
  it latches the layer for both decks and the center.
- **Lamp:** `led_law.hpp` currently forces `MODBTN_L` to 0 with a comment
  waiting for exactly this latch. It becomes the latch state. `SHIFTBTN_L`
  stays 0.
- **Depth params:** one new appended `ParamId` per modulatable face
  (21 per deck + 6 center + MODBTN = 49 new params), unipolar 0..1, engine-
  backed ones inited per §3a, all others 0. Param ids are append-only
  (`Fireflow.cpp:295`); patch breakage is a non-issue (dev alpha).
- **Widget swap:** the existing `SlotVisible` mechanism (the STAGES/ATTACK
  precedent) stacks the sound knob and its depth knob on one coordinate;
  the latch picks which is visible. Depth tooltips read "<name> mod depth".
- **Push loop:** engine-backed depths push their setters every block like
  every other param; host-computed targets replace the current
  `pp(...)`-direct push with the §3b formula (which degenerates to the
  current push at depth 0 — pinned by a test, §7).
- Excluded knobs keep their normal widget and function while latched.
- The four unwired `MOD1..4` CV jacks per deck stay unwired; CV over depths
  is a later, separate design.

### Visual affordance (decided 2026-08-22, variant B of three mocked options;
**revised 2026-08-22, same day, after the owner reviewed the rendered
panel**)

- **Printed mod wreath — recolour, not a satellite ring.** The first cut of
  variant B drew a second, dashed hairline ring outside each modulatable
  knob's body ring (radius = body radius + 1.2 mm, stroke 0.35 mm at 0.85
  opacity, dash `1.6 1.2` — the group frames' own dash). Seeing it rendered
  in Rack, the owner asked for it plainer: every knob already has a body
  ring, so a modulatable knob should simply have *that same ring*, at the
  same radius and stroke-width, recoloured to the zone accent (`ACC`: deck A
  teal, deck B orange, center blue-grey) instead of the default hairline
  colour (`HW_RING`). Solid, no dash, no opacity — one stroke doing the job
  two strokes used to. A knob without a depth keeps the plain `HW_RING`
  ring, so the plate still reads as "light and dark rings", just in two
  colours where before it was one. Drawn by `gen_hw_panel.py` as plate
  print; silkscreen-friendly (pure line work, like the rest of the
  Blueprint plate).
- **Absence carries meaning:** a plain `HW_RING` ring (not a zone-accent
  ring) = the knob keeps its sound function even while MOD is latched. The
  MOD knob itself is deliberately left with the plain ring — it is the
  per-deck master in both modes.
- **VCV affordance is the printed ring plus the lamp, same as hardware —
  no extra dynamic ring** (revised 2026-08-22, later the same day, after the
  owner reviewed the rendered panel with `9a3a779` applied). The first cut of
  this section additionally drew a dynamic accent ring in Rack
  (`ModDepthRing` in `host/vcv/src/Fireflow.cpp`) at the pre-recolour
  satellite radius (`rMm = big ? 7.2f : 5.6f`), visible only while latched.
  Once the printed ring itself moved onto the body ring and picked up the
  zone accent colour (`9a3a779`), that dynamic ring became a second,
  concentric accent ring drawn on top of the first — a duplicate signal, not
  a distinct one. The owner's call: delete it. His reasoning — the printed
  accent ring already marks *which* knobs are modulatable, the `MODBTN_L`
  lamp already shows *whether* the layer is engaged, and the knob's own
  pointer shows the depth value; a third indicator of the same state is
  noise. `ModDepthRing`, its instantiation in the `FireflowHWWidget`
  constructor, and the `acc` colour computation that existed only to feed it
  (which duplicated `res/gen_hw_panel.py`'s `ACC`/`ZONE_A`/`W` constants as
  hardcoded literals) were removed 2026-08-22. The pleasing consequence:
  VCV and hardware now share the *same* affordance — printed ring marks
  modulatability, lamp reports the latch, knob shows the depth — rather than
  VCV carrying one indicator hardware physically cannot.
- Validated on the real generated plate (`res/FireflowHW.svg`, regenerated
  2026-08-22 with the recolour): the ring sits at the same radius as every
  other knob's body ring, so it needs no new caption-clearance check — moving
  it inward from `body_r + 1.2` to `body_r` only *increases* the clearance to
  the printed captions that the original variant-B pass already measured
  (`CAPTION_GAP` 3.60 vs. the old ring's outer edge at offset 1.2 + 0.175
  stroke). It also stays outside the Rack widget radii for the same reason
  the satellite ring did — `body_r` itself is already outside `RACK_R` (G
  6.0 vs. `RACK_R` G 4.80; S 4.4 vs. `RACK_R` S 3.02) — so the recoloured
  ring remains visible around the Rack knob widget exactly as the plain
  `HW_RING` ring always has.

## 6. Measured facts this design stands on

All probes 2026-08-22, scratchpad probe round (probe recipe
`docs/engine-map.md` §6). Setup stated per rule.

1. **RANGE 0 silences a lane exactly.** `apply_range` (`engine/mod/range.h`)
   returns 0 at r ≤ 0. Measured on `ModLane` directly — `set_melodic()` before
   `init()`, seed 12345, 48 kHz, rate 0.5 Hz, SHAPE 0, SMOOTH 0, VARY 0,
   20 s — note deck STEP and FLOW and texture FLOW all read
   **p2p 0.000000000, max|v| 0.000000000** at RANGE 0. This is what stands in
   for a pitch-depth knob.
   Caveat recorded while measuring: RANGE's lower half is **unipolar with a
   DC offset by design** (`range.h`: r ≤ 0.5 maps to unipolar 0..amp) — at
   RANGE 0.5 the note lane's max|v| (0.546) exceeds its RANGE 1.0 max|v|
   (0.154). RANG is a character control, not a pure amplitude, and the layer
   does not change that.
2. **`lane_output` is live host API.** Render host, 16 s scenario: `a_motion`
   column (deck A MOTION `lane_output`) moved p2p 1.9998 at rate 0.5 Hz.
3. **FX depth/active behaves as §3a needs.** Same scenario: slot 3
   (REV_SEND, base 0.25) inactive for 8 s → `fx_target_value` p2p exactly 0;
   `set_fx_target_active(true)` + depth 1 at t=8 → p2p 1.0 (full clamp range).
4. **Block-rate setter pushes don't zipper** at layer-typical swings —
   figures in §3b.

## 7. Testing

- **Panel guards:** `res/test_hw_panel.py` follows the MODBTN move; the
  generated header is regenerated, never hand-edited. A new guard asserts
  the wreathed set equals the set of params that own a depth (and that the
  excluded list owns none) — one source of truth for "modulatable".
- **LED law:** unit test that `MODBTN_L` equals the latch state (the law is
  Rack-free and already under test).
- **Mapping table:** a host-side test that every modulatable param has exactly
  one lane and one init depth, and that every excluded param has none.
- **Depth-0 identity:** with all host-computed depths at 0, the pushed value
  equals the plain knob push for every target (absolute epsilon, not bit
  identity — `docs/engine-map.md` §5). This test must be proven RED once
  (house rule) by forcing a nonzero depth.
- Renders remain sanity checks; no checksum gates.

## 8. Out of scope

- `shell/` firmware port of the layer (pattern transfers; the pot-pickup
  problem on real hardware is an M6 design question, not a VCV one).
- SHIFT button (still reserved, still inert).
- Zone-clamped modulation for the excluded zone knobs (recorded as the
  alternative if they should ever join the layer).
- CV control of depths via the MOD1..4 jacks.
- Any change to `kColorMod` / `kOverlapMod` (by-ear stock).
