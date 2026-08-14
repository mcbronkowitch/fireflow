# The Fireflow -> flow parameter map

**Authority for the correspondence between a hand-authored `Fireflow` patch and
a `flow` `BaseOverlay`.** The converter (`host/vcv/src/flow_patch_bridge.hpp`,
spec 2026-08-11 §5) implements this table and invents nothing. If a mapping is
not written here, the converter does not perform it.

**Sibling authority:** [`engine-map.md`](engine-map.md) owns the *other*
question — how a `ModLane` behaves once a value arrives: the lane state space,
which sources sum into an axis besides its knob, and who writes the variables the
behaviour depends on. This file maps **parameters across hosts**; that one maps
**behaviour inside a lane**. A parameter being transferable says nothing about
what it does when it lands.

Written 2026-08-12, against `HEAD` of branch `flow-patch-transfer`. **Revised
2026-08-12** when four parameters moved out of the story tables and into
`kBaseRules` — see "The 2026-08-12 widening" below. **Revised again the same
day** by the PACE work, which deleted the DIRT story outright and added
`P_PACE` — see "The PACE move" below. **Citations re-derived 2026-08-14**: all 39
`Fireflow.cpp` line numbers had drifted by exactly 12, six in `flow.cpp` by
varying amounts, and `apply_param` by 10. Every one of the 69 was relocated from
the quoted expression beside it — which is the reason each row carries one.
The counts and formulas were unaffected.

## Counts

| | |
|---|---|
| **mapped** (`direct` or an explicit formula) | **45** |
| **UNREACHABLE** | **2** |
| **total base-rule parameters** | **47** |

Derived, not remembered. The 47 come from `engine/flow/taste.h`'s `kBaseRules`
table:

```bash
awk '/^inline const BaseRule kBaseRules\[\] = \{/,/^\};/' engine/flow/taste.h \
  | grep -E "^\s*\{" | grep -o "P_[A-Z_]*" | sort
```

`is_base_rule()` (`engine/flow/terrain.cpp:30`) reads the same table, so this
list and the overlay's honoured set cannot drift apart. `tests/test_flow_overlay.cpp`
pins the count and names the five rows the PACE move added.

The other 17 of `P_COUNT` = 64 are story-owned and are not transferable at all
(spec §3). They are not rows here.

## The 2026-08-12 widening

Four parameters left the story tables and became base rules, taking the count
from 38 to 42. This was not a tidy-up: two of them were the loudest things a
transferred patch was losing, and one of the two was also broken on pads that
had never seen a transfer.

- **`P_TIDE`** was owned by `M_MOTION`'s "orbit" story. It scales the four
  texture lanes against the PITCH lane over a **16× span** — ×1/4 to ×4
  (`tide_free` / `kTideRatios`, `engine/mod/divisions.h`). A carried patch
  therefore kept its `RATE` exactly and still ran its modulation at up to four
  times the speed it was built at, because `RATE` is only one of the inputs to
  a lane's Hz. Speed is a property of the patch, so it now travels with it.
  `M_MOTION` keeps DRIFT and the reverb wash and no longer changes how fast
  anything clocks.
- **`P_COLOR_A` / `P_COLOR_B`** were owned by `M_DENSITY`'s "thick" story.
  COLOR **is the chord size** — `ChordBuilder::set_color` counts tones over
  fixed zone edges (`engine/pitch/chord.h`), so below `kEdge2` = 0.125 a deck
  voices exactly one note. `M_DENSITY` had two variants and `generate()` picked
  one uniformly, so on the draws where "thick" lost, `terrain.cpp`'s stage-4
  else-branch handed COLOR its curve's `bp[0]` as a "calm floor, unmapped" —
  drawn from the `{0, .1}` band, entirely below `kEdge2` — with `storied[]`
  false, so no macro, no weather offset and no veto could reach it.
  **Measured: 1050 of 2000 terrains, and 52.5% of all woken pads had both decks
  pinned to a single tone with no control able to change it.** Now 0%.
  `tests/test_flow_chord_reach.cpp` holds both numbers.
- **`P_SUB_A`** came along because deleting the "thick" variant left it as that
  story's only target, and a story that moves nothing but a sub level is not a
  story. It gains a destination it never had.

**The cost, stated plainly:** Glow's macro surface no longer has a chord
thickness control at all, and `M_DENSITY` no longer has a variant coin. Chord
size is now a property of the terrain draw and of a transferred patch, moved by
NEW and by reroll rather than by a knob.

The `P_TIDE` and `P_COLOR_*` base rows are **first-pass values set by
arithmetic, not by ear** — `taste.h` says so at both rows, and the spans are
placed against `ChordBuilder`'s zone edges and `tide_free`'s ratios
respectively. They are the obvious thing to check against the ear first.

## The PACE move

Later the same day the PACE work (spec `2026-08-12-modulation-pace-design.md`)
took the count from 42 to 47. `M_DIRT` became `M_PACE`, and the DIRT "heat"
story was **deleted entirely** — so all four of its targets became base rules,
and `P_PACE` was added as a fifth row.

- **`P_PACE`** is new, and the reason it is a base rule rather than a story is
  the whole design: a story-owned parameter is unreachable from a
  `BaseOverlay` by construction (`generate()` applies the overlay by iterating
  `kBaseRules`), so a macro that *owned* the pace would throw away a
  transferred patch's own speed — the same failure `P_TIDE` was moved to fix,
  one level up. The terrain draws **no** pace at all: its span is a degenerate
  `{0.5, 0.5}` on every archetype, which is exactly ×1. The row exists so the
  overlay has a destination and the coverage test has no hole. `M_PACE` adds a
  live offset on top, in `Flow`'s guard chain (`flow.cpp`), not in the terrain.
- **`P_DRIVE`, `P_GRIT_A`, `P_GRIT_B`, `P_COMP_A`** were the DIRT story's four
  targets. Three of them — `P_DRIVE`, `P_GRIT_A`, `P_GRIT_B` — had a literal,
  degenerate `{0.f, 0.f}` `bp0` span, and stage 4 writes `base[p] = bp[0]`, so
  **all three sat at exactly 0.0 on every terrain ever drawn**. They now draw
  real per-archetype spans. `P_COMP_A` joins `P_COMP_B`'s band.
- They were given real spans rather than inheriting the degenerate `bp0`,
  which would have kept every existing terrain code rendering identically. The
  price of that would have been pinning `P_DRIVE` at 0 forever, and DRIVE has
  had **no** Fireflow control since the 2026-08-09 reduction retired
  `MASTER_DRIVE`/`PUSH` — Glow would have lost master drive outright with
  nothing able to restore it. Terrain codes re-render instead; that was the
  owner's ruling.

**What this buys the transfer, stated plainly:** deck A's compressor and both
GRIT sends now have a destination they never had, and `STEPS_A` / `SONG_A` /
`FORM_A` are the only Fireflow controls still landing nowhere. **What it does
not buy:** `P_DRIVE` gained a destination in flow and still has no *source* in
Fireflow, so it is a new **UNREACHABLE** row rather than a new mapped one.

## How to read a row

- **flow param** — the `spky::flow::ParamId` (`engine/flow/flow_params.h:69-120`,
  the `SPKY_FLOW_PARAMS` X-macro). Rows are in that enum's order.
- **Fireflow** — the `ParamId` from `host/vcv/src/generated_panel.hpp:16-86`.
  `PART_STRIDE` is 20: part A occupies `[0, 20)`, part B the next 20, and
  everything from `MORPH` onward is appended and **not** part-strided.
- **conversion** — exactly one of `direct`, an explicit formula, or
  `UNREACHABLE` with a reason. Every row cites the line in
  `host/vcv/src/Fireflow.cpp` that is the evidence.

Two traps this table exists to survive, both of which have already bitten this
repo (see the `fireflow-control-merge-init-trap` memory):

1. **The knob's name is not the parameter's name.** `P_DEPTH_A/B` comes from the
   knob printed `MOD`. `P_COMP_A/B` come from the knobs printed `LVL`.
   `P_FORM_B` comes from the knob printed `SONG`.
2. **The knob's value is not always the engine's value.** For `ENGINE`, `CHOKE`,
   `COUPLE`, `TEMPO`, `GRIT` and `COMP` (`LVL`), `Fireflow.cpp` transforms the knob
   before handing it to the engine. Flow's `apply_param()`
   (`flow_params.h:145`) calls the *same* engine setter with the overlay value
   raw. So for those rows the converter must store what **`Fireflow` pushed to
   the engine**, not what the knob read. Copying the knob is the exact shape of
   the four conversion changes that silently moved the factory sound.

Every overlay value is clamped to `kParams[p]` on the way into `generate()`
(`terrain.cpp:494`), so an out-of-range value cannot enter a terrain — but it
also cannot come back out. Rows where a Fireflow value can exceed flow's range
say so and say what the converter must report.

---

## The table

### Discrete world picks

| flow param | Fireflow | conversion | notes |
|---|---|---|---|
| `P_ENGINE_A` | `ENGINE_A` | **formula.** Knob `k = round(ENGINE_A)`, `k` in 0..4 (`configSwitch(0.f, 4.f, …, {"Synth","Sampler","Wave","Body","BBD"})`, `Fireflow.cpp:483-484`). Store the `EngineId`, not `k`: `0 -> ENGINE_SYNTH(1)`, `2 -> ENGINE_WAVE(3)`, `3 -> ENGINE_BODY(4)`, `4 -> ENGINE_BBD(5)`, **anything else -> `ENGINE_SAMPLER(2)`** (`Fireflow.cpp:700-705`) | `EngineId` is `engine/parts/engine_iface.h:11-20`. Knob position 1 becomes `ENGINE_TEST_TONE(0)` **only** when the part's separate `smp[p].testTone` flag is set (`Fireflow.cpp:705`); that is not a knob position and does not transfer — such a part converts as `SAMPLER`, and the converter reports it. See also the carrier note below |
| `P_ENGINE_B` | `ENGINE_B` | same formula, `p = 1` | — |
| `P_SCALE` | `SCALE` | **direct** (`Fireflow.cpp:965` — `set_scale((int)round(params[SCALE]))`) | Same id space on both sides: `configParam<ScaleQuantity>(c.id, 0.f, SCALE_LIST_COUNT-1, …)` (`Fireflow.cpp:426`), snapped (`:450`); `SCALE_LIST_COUNT` is 13 (`engine/pitch/quantizer.h:16-33`) and `kParams[P_SCALE]` is `0..12, 13 steps`. `terrain.cpp:350` static-asserts the two agree |
| `P_ROOT` | — | **UNREACHABLE.** Fireflow has no ROOT control. `set_root` appears **zero** times in `Fireflow.cpp` (`grep -c set_root` = 0), and no `ParamId` in `generated_panel.hpp` names a root | The overlay must leave `has[P_ROOT] = false`, so the terrain's own stage-2 root draw (`terrain.cpp:435`) stands. The converter must name this every time — a transferred place is in the terrain's key, not the patch's. (`Flow` does carry a `_root_ovr` override, `flow.cpp:638`, but nothing in Fireflow feeds it; it is not a transfer path.) |
| `P_FORM_B` | `SONG_B` | **formula.** `rung = round(SONG_B)`; `P_FORM_B = song_ladder_at(rung).form` (`Fireflow.cpp:916-917`) | There is **no FORM control** — the 2026-08-09 control reduction deleted it and folded FORM into SONG. The knob is `configSwitch(0.f, kSongLadderCount-1 = 13, …, 14 labels)` and snapped (`Fireflow.cpp:446-448, :450`), so its value **is** the rung index. The ladder is 14 of the 5x7 = 35 `(Principle, SongMode)` pairs (`engine/mod/song_ladder.h:24-38`); every individual `form` 0..4 does occur, so `P_FORM_B`'s own range is fully reachable — only 21 of the 35 *combinations* are not |
| `P_SONG_B` | `SONG_B` | **formula.** Same rung: `P_SONG_B = song_ladder_at(rung).song` (`Fireflow.cpp:916, :906`) | Same one knob as `P_FORM_B`; the two flow params are not independently settable from Fireflow. Every individual `song` value 0..6 occurs on the ladder |
| `P_STEPS_B` | `STEPS_B` | **formula.** `s = round(STEPS_B)`, knob range `0..16` (`configParam(c.id, 0.f, 16.f, init, "Steps")`, `Fireflow.cpp:461`, snapped `:450`); `s >= 2 -> P_STEPS_B = s`. `s == 0` means STEP off for that deck (`set_step(p, steps > 0, steps)`, `Fireflow.cpp:893`) — do **not** set `has[P_STEPS_B]`, and see `P_MODE`. `s == 1` has no flow representation (`kParams[P_STEPS_B]` is `2..16`); clamp to 2 and report | Flow re-clamps to `2..16` anyway in `Flow::push_mode_and_steps` (`flow.cpp:422-423`). **Fireflow's `STEPS_A` has no destination**: `P_STEPS_A` is *not* a base rule (it is story-owned, DENSITY's "rate" story), so an overlay entry for it would be read by nothing. Report it as dropped |

### Rate, pitch and shape (per deck)

| flow param | Fireflow | conversion | notes |
|---|---|---|---|
| `P_TUNE_A` / `P_TUNE_B` | `TUNE_A` / `TUNE_B` | **direct** (`Fireflow.cpp:618` — `set_tune(p, pp(TUNE_A, p))`) | Knob is plain `configParam(c.id, 0.f, 1.f, …)` (`Fireflow.cpp:404`), same range as `kParams` |
| `P_RATE_A` / `P_RATE_B` | `RATE_A` / `RATE_B` | **direct** (`Fireflow.cpp:612`) | `configParam<RateQuantity>(c.id, 0.f, 1.f, …)` (`:355`). `RateQuantity` is **display only** (`:44-52`) — it prints a division name in the GRID zone and a Hz figure otherwise; it does not transform the value |
| `P_SHAPE_A` / `P_SHAPE_B` | `SHAPE_A` / `SHAPE_B` | **direct** (`Fireflow.cpp:613`) | Plain `0..1` knob |
| `P_SMOOTH_A` / `P_SMOOTH_B` | `SMOOTH_A` / `SMOOTH_B` | **direct** (`Fireflow.cpp:615`) | Plain `0..1` knob |
| `P_RANGE_A` / `P_RANGE_B` | `RANGE_A` / `RANGE_B` | **direct** (`Fireflow.cpp:616`) | Plain `0..1` knob. Runtime cap under Glow: on a deck currently pushed as `ENGINE_BBD` **in FLOW mode**, `Flow::recompute_and_push` caps RANGE at `kBbdFlowRangeMax` (`flow.cpp:581-585`). The stored value is unchanged; what is heard may be lower. Report it when the deck's converted engine is BBD |
| `P_DEPTH_A` / `P_DEPTH_B` | `MOD_A` / `MOD_B` | **direct** (`Fireflow.cpp:617` — `set_depth(p, pp(MOD_A, p))`) | **The panel prints `MOD`; the parameter is `DEPTH`.** `generated_panel.hpp:117` gives `MOD_A` the caption `"MOD"`. There is also a `set_depth` naming collision in this engine (`spotykach-gotchas`) — this row was verified from the call site, not the name |

### Envelope and voice colour (per deck)

| flow param | Fireflow | conversion | notes |
|---|---|---|---|
| `P_ATTACK_A` / `P_ATTACK_B` | `ATTACK_A` / `ATTACK_B` | **direct** (`Fireflow.cpp:620` — `set_voice_attack`) | Plain `0..1` knob. The caption changes per engine (`HIT` on BODY, `kDynCaptions`, `generated_panel.hpp:203-206`); the engine setter does not |
| `P_DECAY_A` / `P_DECAY_B` | `DECAY_A` / `DECAY_B` | **direct** (`Fireflow.cpp:621` — `set_voice_decay`) | As above (`DAMP`/`TAIL` captions) |
| `P_RES_A` / `P_RES_B` | `RES_A` / `RES_B` | **direct, then clamped.** (`Fireflow.cpp:622` — `set_voice_resonance`.) Fireflow's knob is `0..1` (generic `configParam`, `:392`); `kParams[P_RES_*]` caps at **0.75** — the table entry is `X(P_RES_A, 0.f, 0.75f, 0) X(P_RES_B, 0.f, 0.75f, 0)` (`flow_params.h:86`), and the prose calling it the by-ear resonance cap is `flow_params.h:21` — and `terrain.cpp:494` clamps on the way in. **Report any value above 0.75** | Caption is `CHAR`/`TILT` on BODY/BBD; setter unchanged |
| `P_SUB_A` / `P_SUB_B` | `SUB_A` / `SUB_B` | **direct** (`Fireflow.cpp:625` — `set_voice_sub(p, pp(SUB_A, p))`), strided | `configParam<SubQuantity>(c.id, 0.f, 1.f, …)` (`:375`); `SubQuantity` is display only (`:165-179`). **Half of this knob is lost:** on a Sampler deck Fireflow *additionally* re-points it to `LANE_SIZE` as GENE SIZE (`Fireflow.cpp:867-873`), which flow never writes — reported per deck when that deck converts to SAMPLER. `P_SUB_A` became a base rule on 2026-08-12; before that this knob had no destination on deck A at all |
| `P_COLOR_A` / `P_COLOR_B` | `COLOR_A` / `COLOR_B` | **direct** (`Fireflow.cpp:624` — `set_color(p, params[p ? COLOR_B : COLOR_A])`) | **This is the CHORD SIZE, not a timbre tint.** `ChordBuilder::set_color` (`engine/pitch/chord.h:49-56`) counts tones over fixed edges: under `kEdge2` = 0.125 one note, then 2/3/4 at `kEdge3`/`kEdge4`, with the fifth slot becoming a ninth above `kEdge9` = 0.85. Note the **explicit ternary** at the push site — `COLOR_A/B` are appended params outside `PART_STRIDE`, exactly like `LINK_A/B`, so `pp(COLOR_A, p)` reads the wrong slot. On a BODY deck COLOR is read as chord *quality* rather than as pitches (`engine/body/material.h`); the setter is the same either way |

### FX sends and glue

| flow param | Fireflow | conversion | notes |
|---|---|---|---|
| `P_FLUXMIX_A` / `P_FLUXMIX_B` | `FLUX_A` / `FLUX_B` | **direct** (`Fireflow.cpp:632` — `set_flux_mix`) | Plain `0..1` knob. **Known asymmetry, not a converter problem:** Fireflow also gates the block with `set_fx_on(p, FxBlock::Flux, mix > 1e-4f)` (`:639`), and **nothing in `engine/flow/` or `Glow.cpp` ever calls `set_fx_on`** (`grep -rn set_fx_on` hits only `Fireflow.cpp` and the render host). `SoftSwitch::_on` defaults `false` (`engine/fx/fx_util.h`), so under Glow the FLUX block is off and this value is currently inaudible. That is a pre-existing gap in the flow layer — the value still transfers, and this row must not be "fixed" by inventing a mapping |
| `P_COMP_B` | `COMP_B` (printed **`LVL`**) | **formula.** The knob is not the comp amount. Store what Fireflow pushes: `lvl = COMP_B`; `P_COMP_B = lvl <= 0.6 ? 0 : 0.7 * pow((lvl - 0.6) / 0.4, 0.6)` (`Fireflow.cpp:683-691`, constants `kLvlCompSplit = 0.6`, `kCompTop = 0.7`, `kCompShape = 0.6`) | **Deck balance does not transfer.** The lower zone of `LVL` is pure output gain via `set_part_level` (`:675`), and flow never calls `set_part_level` at all — both decks sit at 1.0 under Glow. **And the veto has the last word:** `kVetos` forces `P_COMP_B` into **0.40..0.60** (`taste.h:647`, enforced at `flow.cpp:614-619`), so *every* `LVL` position lands in that band whatever this formula produces. That band is a by-ear ruling (`spotykach-by-ear-decisions`); the converter reports the rewrite, it does not work around it. **This contradicts spec §5's prose**, which assumed the knob transfers raw ("a COMP dialled to 0.85 transfers perfectly and is heard at 0.60"); with the formula 0.85 gives 0.528, which is in band and is what the Fireflow patch actually sounded like. **Corrected in §5 on 2026-08-12 (owner-approved), and §5 now points here as the authority** — this row is the source of truth. The formula is right — copying a merged control's knob value is precisely the `fireflow-control-merge-init-trap` failure |
| `P_COMP_A` | `COMP_A` (printed **`LVL`**) | **formula.** The identical formula as `P_COMP_B`, read off deck A's own knob: `lvl = COMP_A`; `P_COMP_A = lvl <= 0.6 ? 0 : 0.7 * pow((lvl - 0.6) / 0.4, 0.6)` (`Fireflow.cpp:683-691`; the push site is `pp(COMP_A, p)` and `COMP_A`/`COMP_B` are 20 apart, so the stride is correct here) | **A base rule since 2026-08-12** — before that it was owned by the DIRT story and had no destination at all. Everything the `P_COMP_B` row says applies unchanged: the lower zone of `LVL` is pure output gain via `set_part_level` and does **not** transfer, and `kVetos` forces the result into **0.40..0.60** (`taste.h`, enforced at `flow.cpp`'s veto loop), so every `LVL` position lands in that band whatever the formula produces. Report the rewrite |
| `P_GRIT_A` / `P_GRIT_B` | `GRIT_A` / `GRIT_B` | **formula, bipolar with a dead zone.** `k = GRIT_A` (or `GRIT_B`), a `configParam(c.id, -1.f, 1.f, …)` (`Fireflow.cpp:416`); `mag = abs(k)`; `P_GRIT_* = mag <= 0.03 ? 0 : (mag - 0.03) / (1 - 0.03)` (`Fireflow.cpp:886-891`, `kGritDead = 0.03f` at `:592`). **The sign does not transfer**: it picks `GritMode::Reduce` vs `Drive` (`set_grit_mode`), and flow has no `P_GRIT_MODE` — report a negative knob as a mode dropped to `Drive` | **Base rules since 2026-08-12**, from the deleted DIRT story. **Known asymmetry, the same one the `P_FLUXMIX_*` row carries and for the same reason:** Fireflow gates the block with `set_fx_on(p, FxBlock::Grit, abs(GRIT) > kGritDead)` (`:644-645`), and **nothing in `engine/flow/` or `Glow.cpp` ever calls `set_fx_on`**, so under Glow the GRIT block is off and this value is currently inaudible. That is a pre-existing gap in the flow layer — the value still transfers, and this row must not be "fixed" by inventing a mapping. `taste.h`'s spans are deliberately modest for the day that gap closes |
| `P_DRIVE` | — | **UNREACHABLE.** Fireflow has no DRIVE control. `set_master_drive` is called with the **hardcoded literal `0.40f`** (`Fireflow.cpp:962`), fixed by ear in the 2026-08-09 control reduction ("PUSH sat at 0.40 in every patch, and once the limiter rides, DRIVE stops controlling dirt anyway"). There is no `ParamId` in `generated_panel.hpp` for it | The overlay must leave `has[P_DRIVE] = false`, so the terrain's own base draw stands. **This became a row on 2026-08-12** — before that `P_DRIVE` was story-owned and outside this table entirely, which is why the count of UNREACHABLE rows went 1 → 2. The two sides moved in opposite directions on the same day: flow gained a destination for DRIVE exactly when Fireflow no longer had a source. Note also that `kVetos` caps `P_DRIVE` at 0.40 — the very value Fireflow hardcodes — so a converter that *did* copy it would be writing the band ceiling |
| `P_LINK_A` / `P_LINK_B` | `LINK_A` / `LINK_B` | **direct** (`Fireflow.cpp:643` — `set_link(p, params[p ? LINK_B : LINK_A])`) | `configParam<LinkQuantity>(c.id, 0.f, 1.f, …)` (`:371`), display quantity only. Note the **explicit ternary**: `LINK_A/B` are appended params outside `PART_STRIDE`, so `pp(LINK_A, p)` would read the wrong slot. A converter that indexes by stride has the same bug available to it |

### Global

| flow param | Fireflow | conversion | notes |
|---|---|---|---|
| `P_MORPH` | `MORPH` | **direct** (`Fireflow.cpp:921`) | Plain `0..1` knob |
| `P_COUPLE` | `COUPLE` (printed **`FREE\|GRID`**) | **formula, zone split.** `k = COUPLE`, `grid = k >= 0.5` (`kCoupleZoneSplit`, `Fireflow.cpp:35`); `P_COUPLE = grid ? (k - 0.5) / 0.5 : k / 0.5` (`Fireflow.cpp:929-933`) | Store the rescaled half-zone value, **not the knob**: flow's `apply_param` hands `P_COUPLE` straight to `set_couple` (`flow_params.h:186`), which is what Fireflow's rescaled value feeds. This is also the sole source of `set_sync` in Fireflow (`:918`) — see `P_MODE` |
| `P_CHOKE` | `CHOKE` | **formula.** `P_CHOKE = CHOKE * 0.5` (`Fireflow.cpp:948`) | The knob is `configSwitch(c.id, -2.f, 2.f, …)` with 5 snapped states (`:357-359`); x0.5 lands them on flow's `-1..1` — `X(P_CHOKE, -1.f, 1.f, 0)` (`flow_params.h:96`). The five states are by-ear rulings (`spotykach-by-ear-decisions`) — the halving is the whole conversion, nothing rounds |
| `P_TIDE` | `TIDE` | **direct** (`Fireflow.cpp:947` — `set_tide(params[TIDE])`) | Plain `0..1` knob, but the SCALE it drives is geometric: `tide_free` = `2^(4·(v−0.5))`, so 0.5 is exactly ×1 and the ends are ×1/4 and ×4. Synced, it snaps to `kTideRatios`'s 9 rungs instead. It scales the four **texture** lanes only (`SOURCE ×2`, `SIZE ×0.5`, `MOTION ×0.75`, `LEVEL ×1.5` — `kLaneRatio`); the PITCH lane is unaffected (`_pitch_scale` is a fixed 1.0). A base rule since 2026-08-12 — see the widening note above |
| `P_SHUFFLE` | `SHUFFLE` | **direct** (`Fireflow.cpp:610`) | Plain `0..1` knob |
| `P_REV_DIFF` | `REV_DIFF` | **direct** (`Fireflow.cpp:952` — `set_reverb_diffusion`) | Plain `0..1` knob. `REV_SIZE`/`REV_DECAY`/`REV_TONE` are story-owned, and `set_master_drive`/`set_reverb_smear`/`set_reverb_mod` are hardcoded constants in Fireflow (`:950-952`) — none of those are rows here |
| `P_TEMPO_BPM` | `TEMPO` | **formula, then clamp.** `bpm = 40 + TEMPO * 200` (`Fireflow.cpp:968`), i.e. **40..240 BPM**. `kParams[P_TEMPO_BPM]` is **50..140** — `X(P_TEMPO_BPM, 50.f, 140.f, 0)` (`flow_params.h:101`). Clamp, and **report anything outside** | An external clock at `CLOCK` overrides the knob at runtime (`:957-960`); that is live state, not patch state, and does not transfer |
| `P_PACE` | `PACE` | **direct.** The knob is normalized `0..1` and `Instrument::set_pace` takes exactly that normalized position, not the multiplier — `pace_mult()` (`engine/mod/divisions.h`) is the curve, and it lives in the engine so a host tooltip cannot drift from what runs. `kParams[P_PACE]` is `0..1` continuous, the same range, so nothing is clamped and nothing is reported | **0.5 is exactly ×1**, and it is the centre of the knob, not an end: the curve is `32^(2n-1)` below centre and `4^(2n-1)` above, i.e. ×1/32 … ×1 … ×4, asymmetric on purpose (the fast end is already reachable through `RATE`). **The terrain draws no pace** — the `kBaseRules` span is a degenerate `{0.5, 0.5}` — so an unset `has[P_PACE]` leaves the place at ×1, and a *set* one is the patch's own speed carried intact. Glow's `M_PACE` macro then adds a live **offset** on top (`flow.cpp`'s guard chain), so a carried patch plays at its own speed with the knob centred. **Availability:** the `PACE` knob enters `generated_panel.hpp` with the Fireflow panel commit (plan Task 11); a converter built before it lands must treat this row as absent, not guess a source |
| `P_MODE` | `COUPLE` | **formula, derived.** `P_MODE = (COUPLE >= 0.5) ? 1 : 0` — the COUPLE zone split is the *only* thing that drives `set_sync` in Fireflow (`Fireflow.cpp:929-930`), and `P_MODE` is the only thing that drives `set_sync` in flow (`Flow::push_mode_and_steps`, `flow.cpp:419, :405`) | **Not an independent control, and lossy in one specific way.** In Fireflow the per-deck step flag comes from each deck's own `STEPS` knob (`set_step(p, steps > 0, steps)`, `:881`) while `set_sync` is global; in flow, the single `P_MODE` drives `set_sync` **and both decks' step flags** (`flow.cpp:427-429`). So a patch with deck A free and deck B stepped — an ordinary ambient patch — has no representation. The converter must report whenever `(COUPLE >= 0.5) != (STEPS_A > 0)` or `!= (STEPS_B > 0)`. Spec §5 lists `P_MODE` under "no path at all"; that is about independence, not availability — the value *is* determined by the patch, and leaving it unset would hand a hand-authored stepped patch to the terrain's mode coin, which is worse |

---

## Overlay-level effects that are not per-row

Two things the converter must handle that no single row owns:

1. **The no-carrier rejection (spec §4.3).** `generate()` classifies both decks'
   engines before applying anything: if **neither** is in `kCarrierEngine` =
   `{SYNTH, WAVE, BODY}` (`taste.h:663-664`, `is_carrier_engine`), the whole
   overlay is dropped (`terrain.cpp:486-497`). A Fireflow patch with
   SAMPLER + BBD, SAMPLER + SAMPLER or BBD + BBD is therefore **not
   transferable at all** — not "partly transferred", dropped. The converter
   must detect this before writing anything and set `overlay_rejected`.
   Widening `kCarrierEngine` is spec §10's open decision, out of scope here.
2. **Nothing outside the 47 has a destination.** Not "is transferred with
   loss" — has no slot. That includes every story-owned parameter — the 17 of
   them left after the PACE move: `FILT_A/B`, `VARIATION_A/B`, `DENSITY_A/B`,
   `STEPS_A`, `REVMIX_A/B`, `DRIFT`, `FORM_A`, `SONG_A`, and the reverb shape
   (`REV_SIZE`, `REV_DECAY`, `REV_TONE`, `REV_SMEAR`, `REV_MOD`). The flow
   params with no base rule but a live Fireflow control are therefore
   `STEPS_A`, `SONG_A`/`FORM_A` — `COMP_A` and `GRIT_A/B` left this list on
   2026-08-12 and have rows above. And it includes everything outside
   `P_COUNT` entirely: sample content, `SOURCE_A/B`, `FLUXRATE`, `FLUXFB`,
   `DETUNE`, `STAGES`, `REC`, the excitation bus.

## Values a Fireflow patch can hold that flow cannot

The five places where a legal Fireflow value has no legal flow value. Each is a
clamp on the way in (`terrain.cpp:494`) and each must appear in the report:

| where | Fireflow can be | flow accepts | on conversion |
|---|---|---|---|
| `RES_A/B` | `0..1` | `0..0.75` | clamp to 0.75, report |
| `TEMPO` | 40..240 BPM | 50..140 BPM | clamp, report |
| `STEPS_B` | 0..16 | 2..16 (`s == 0` means STEP off) | `0` -> leave unset + `P_MODE = 0` question; `1` -> clamp to 2, report |
| `COMP_A`/`COMP_B` (`LVL`) | full travel | veto band 0.40..0.60 | value stored, band applied at runtime, report the rewrite. `COMP_A` joined this row on 2026-08-12 |
| `GRIT_A/B` | bipolar `-1..1`, sign = mode | `0..1` magnitude only | the `Reduce`/`Drive` mode has no flow representation; report a negative knob |

And one that is not a range problem at all, because it happens after the value
has already been applied correctly: **Glow's TEMPO fader overwrites the carried
tempo on every control tick** (`Glow.cpp:1039-1042`). The terrain owns tempo and
re-pushes it on every terrain change, so a host-side fader has to re-assert
itself or one wake would undo it. The left fader is assigned to TEMPO by default
and boots at mid travel, which is 95 BPM. Set that fader's target to `off` to
hear the patch's own tempo. In FLOW mode this costs nothing — the mod lanes run
on absolute Hz there (`free_hz`) and ignore BPM entirely — but in STEP mode it
moves the whole grid. Reported unconditionally, like `P_ROOT`.

## Where this file's claims come from

Everything above was read at `flow-patch-transfer` HEAD from:

- `engine/flow/taste.h` — `kBaseRules` (47 rows since the PACE move), `kVetos`, `kCarrierEngine`
- `engine/flow/flow_params.h` — `SPKY_FLOW_PARAMS` ranges, `apply_param()`
- `engine/flow/terrain.{h,cpp}` — `BaseOverlay`, `is_base_rule()`, the overlay
  block in `generate()`
- `engine/flow/flow.cpp` — `recompute_and_push()`, `push_mode_and_steps()`
- `host/vcv/src/generated_panel.hpp` — `enum ParamId`, `kParamCtls`,
  `kDynCaptions`, `PART_STRIDE`
- `host/vcv/src/Fireflow.cpp` — `configControls()` and the `pushParams()`
  control-push block

Line numbers are given for a reader who wants to check a row in under a minute;
the quoted expression beside each is what actually identifies the site if the
file moves.
