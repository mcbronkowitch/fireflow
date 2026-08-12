# The Fireflow -> flow parameter map

**Authority for the correspondence between a hand-authored `Fireflow` patch and
a `flow` `BaseOverlay`.** The converter (`host/vcv/src/flow_patch_bridge.hpp`,
spec 2026-08-11 §5) implements this table and invents nothing. If a mapping is
not written here, the converter does not perform it.

Written 2026-08-12, against `HEAD` of branch `flow-patch-transfer`.

## Counts

| | |
|---|---|
| **mapped** (`direct` or an explicit formula) | **37** |
| **UNREACHABLE** | **1** |
| **total base-rule parameters** | **38** |

Derived, not remembered. The 38 come from `engine/flow/taste.h`'s `kBaseRules`
table:

```bash
awk '/^inline const BaseRule kBaseRules\[\] = \{/,/^\};/' engine/flow/taste.h \
  | grep -E "^\s*\{" | grep -o "P_[A-Z_]*" | sort
```

`P_COMP_A` is deliberately absent — it occurs only inside a comment in that
table and is story-owned. `P_COMP_B` is present. `is_base_rule()`
(`engine/flow/terrain.cpp:30`) reads the same table, so this list and the
overlay's honoured set cannot drift apart.

The other 25 of `P_COUNT` = 63 are story-owned and are not transferable at all
(spec §3). They are not rows here.

## How to read a row

- **flow param** — the `spky::flow::ParamId` (`engine/flow/flow_params.h:69-110`,
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
   knob printed `MOD`. `P_COMP_B` comes from the knob printed `LVL`.
   `P_FORM_B` comes from the knob printed `SONG`.
2. **The knob's value is not always the engine's value.** For `ENGINE`, `CHOKE`,
   `COUPLE`, `TEMPO` and `COMP` (`LVL`), `Fireflow.cpp` transforms the knob
   before handing it to the engine. Flow's `apply_param()`
   (`flow_params.h:135`) calls the *same* engine setter with the overlay value
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
| `P_ENGINE_A` | `ENGINE_A` | **formula.** Knob `k = round(ENGINE_A)`, `k` in 0..4 (`configSwitch(0.f, 4.f, …, {"Synth","Sampler","Wave","Body","BBD"})`, `Fireflow.cpp:433-434`). Store the `EngineId`, not `k`: `0 -> ENGINE_SYNTH(1)`, `2 -> ENGINE_WAVE(3)`, `3 -> ENGINE_BODY(4)`, `4 -> ENGINE_BBD(5)`, **anything else -> `ENGINE_SAMPLER(2)`** (`Fireflow.cpp:649-656`) | `EngineId` is `engine/parts/engine_iface.h:11-20`. Knob position 1 becomes `ENGINE_TEST_TONE(0)` **only** when the part's separate `smp[p].testTone` flag is set (`Fireflow.cpp:655`); that is not a knob position and does not transfer — such a part converts as `SAMPLER`, and the converter reports it. See also the carrier note below |
| `P_ENGINE_B` | `ENGINE_B` | same formula, `p = 1` | — |
| `P_SCALE` | `SCALE` | **direct** (`Fireflow.cpp:915` — `set_scale((int)round(params[SCALE]))`) | Same id space on both sides: `configParam<ScaleQuantity>(c.id, 0.f, SCALE_LIST_COUNT-1, …)` (`Fireflow.cpp:376`), snapped (`:412`); `SCALE_LIST_COUNT` is 13 (`engine/pitch/quantizer.h:16-33`) and `kParams[P_SCALE]` is `0..12, 13 steps`. `terrain.cpp:350` static-asserts the two agree |
| `P_ROOT` | — | **UNREACHABLE.** Fireflow has no ROOT control. `set_root` appears **zero** times in `Fireflow.cpp` (`grep -c set_root` = 0), and no `ParamId` in `generated_panel.hpp` names a root | The overlay must leave `has[P_ROOT] = false`, so the terrain's own stage-2 root draw (`terrain.cpp:435`) stands. The converter must name this every time — a transferred place is in the terrain's key, not the patch's. (`Flow` does carry a `_root_ovr` override, `flow.cpp:601`, but nothing in Fireflow feeds it; it is not a transfer path.) |
| `P_FORM_B` | `SONG_B` | **formula.** `rung = round(SONG_B)`; `P_FORM_B = song_ladder_at(rung).form` (`Fireflow.cpp:866-867`) | There is **no FORM control** — the 2026-08-09 control reduction deleted it and folded FORM into SONG. The knob is `configSwitch(0.f, kSongLadderCount-1 = 13, …, 14 labels)` and snapped (`Fireflow.cpp:396-398, :412`), so its value **is** the rung index. The ladder is 14 of the 5x7 = 35 `(Principle, SongMode)` pairs (`engine/mod/song_ladder.h:24-38`); every individual `form` 0..4 does occur, so `P_FORM_B`'s own range is fully reachable — only 21 of the 35 *combinations* are not |
| `P_SONG_B` | `SONG_B` | **formula.** Same rung: `P_SONG_B = song_ladder_at(rung).song` (`Fireflow.cpp:866, :868`) | Same one knob as `P_FORM_B`; the two flow params are not independently settable from Fireflow. Every individual `song` value 0..6 occurs on the ladder |
| `P_STEPS_B` | `STEPS_B` | **formula.** `s = round(STEPS_B)`, knob range `0..16` (`configParam(c.id, 0.f, 16.f, init, "Steps")`, `Fireflow.cpp:411`, snapped `:412`); `s >= 2 -> P_STEPS_B = s`. `s == 0` means STEP off for that deck (`set_step(p, steps > 0, steps)`, `Fireflow.cpp:843`) — do **not** set `has[P_STEPS_B]`, and see `P_MODE`. `s == 1` has no flow representation (`kParams[P_STEPS_B]` is `2..16`); clamp to 2 and report | Flow re-clamps to `2..16` anyway in `Flow::push_mode_and_steps` (`flow.cpp:401`). **Fireflow's `STEPS_A` has no destination**: `P_STEPS_A` is *not* a base rule (it is story-owned, DENSITY's "rate" story), so an overlay entry for it would be read by nothing. Report it as dropped |

### Rate, pitch and shape (per deck)

| flow param | Fireflow | conversion | notes |
|---|---|---|---|
| `P_TUNE_A` / `P_TUNE_B` | `TUNE_A` / `TUNE_B` | **direct** (`Fireflow.cpp:568` — `set_tune(p, pp(TUNE_A, p))`) | Knob is plain `configParam(c.id, 0.f, 1.f, …)` (`Fireflow.cpp:354`), same range as `kParams` |
| `P_RATE_A` / `P_RATE_B` | `RATE_A` / `RATE_B` | **direct** (`Fireflow.cpp:562`) | `configParam<RateQuantity>(c.id, 0.f, 1.f, …)` (`:319`). `RateQuantity` is **display only** (`:38-44`) — it prints a division name in the GRID zone and a Hz figure otherwise; it does not transform the value |
| `P_SHAPE_A` / `P_SHAPE_B` | `SHAPE_A` / `SHAPE_B` | **direct** (`Fireflow.cpp:563`) | Plain `0..1` knob |
| `P_SMOOTH_A` / `P_SMOOTH_B` | `SMOOTH_A` / `SMOOTH_B` | **direct** (`Fireflow.cpp:565`) | Plain `0..1` knob |
| `P_RANGE_A` / `P_RANGE_B` | `RANGE_A` / `RANGE_B` | **direct** (`Fireflow.cpp:566`) | Plain `0..1` knob. Runtime cap under Glow: on a deck currently pushed as `ENGINE_BBD` **in FLOW mode**, `Flow::recompute_and_push` caps RANGE at `kBbdFlowRangeMax` (`flow.cpp:555-559`). The stored value is unchanged; what is heard may be lower. Report it when the deck's converted engine is BBD |
| `P_DEPTH_A` / `P_DEPTH_B` | `MOD_A` / `MOD_B` | **direct** (`Fireflow.cpp:567` — `set_depth(p, pp(MOD_A, p))`) | **The panel prints `MOD`; the parameter is `DEPTH`.** `generated_panel.hpp:117` gives `MOD_A` the caption `"MOD"`. There is also a `set_depth` naming collision in this engine (`spotykach-gotchas`) — this row was verified from the call site, not the name |

### Envelope and voice colour (per deck)

| flow param | Fireflow | conversion | notes |
|---|---|---|---|
| `P_ATTACK_A` / `P_ATTACK_B` | `ATTACK_A` / `ATTACK_B` | **direct** (`Fireflow.cpp:570` — `set_voice_attack`) | Plain `0..1` knob. The caption changes per engine (`HIT` on BODY, `kDynCaptions`, `generated_panel.hpp:203-206`); the engine setter does not |
| `P_DECAY_A` / `P_DECAY_B` | `DECAY_A` / `DECAY_B` | **direct** (`Fireflow.cpp:571` — `set_voice_decay`) | As above (`DAMP`/`TAIL` captions) |
| `P_RES_A` / `P_RES_B` | `RES_A` / `RES_B` | **direct, then clamped.** (`Fireflow.cpp:572` — `set_voice_resonance`.) Fireflow's knob is `0..1` (generic `configParam`, `:354`); `kParams[P_RES_*]` caps at **0.75** — the table entry is `X(P_RES_A, 0.f, 0.75f, 0) X(P_RES_B, 0.f, 0.75f, 0)` (`flow_params.h:86`), and the prose calling it the by-ear resonance cap is `flow_params.h:21` — and `terrain.cpp:494` clamps on the way in. **Report any value above 0.75** | Caption is `CHAR`/`TILT` on BODY/BBD; setter unchanged |
| `P_SUB_B` | `SUB_B` | **direct** (`Fireflow.cpp:575` — `set_voice_sub(p, pp(SUB_A, p))`) | `configParam<SubQuantity>(c.id, 0.f, 1.f, …)` (`:339`); `SubQuantity` is display only (`:129-140`). **Half of this knob is lost:** on a Sampler deck Fireflow *additionally* re-points it to `LANE_SIZE` as GENE SIZE (`Fireflow.cpp:820-824`), which flow never writes. `P_SUB_A` is not a base rule, so Fireflow's `SUB_A` has no destination — report it as dropped |

### FX sends and glue

| flow param | Fireflow | conversion | notes |
|---|---|---|---|
| `P_FLUXMIX_A` / `P_FLUXMIX_B` | `FLUX_A` / `FLUX_B` | **direct** (`Fireflow.cpp:582` — `set_flux_mix`) | Plain `0..1` knob. **Known asymmetry, not a converter problem:** Fireflow also gates the block with `set_fx_on(p, FxBlock::Flux, mix > 1e-4f)` (`:601`), and **nothing in `engine/flow/` or `Glow.cpp` ever calls `set_fx_on`** (`grep -rn set_fx_on` hits only `Fireflow.cpp` and the render host). `SoftSwitch::_on` defaults `false` (`engine/fx/fx_util.h`), so under Glow the FLUX block is off and this value is currently inaudible. That is a pre-existing gap in the flow layer — the value still transfers, and this row must not be "fixed" by inventing a mapping |
| `P_COMP_B` | `COMP_B` (printed **`LVL`**) | **formula.** The knob is not the comp amount. Store what Fireflow pushes: `lvl = COMP_B`; `P_COMP_B = lvl <= 0.6 ? 0 : 0.7 * pow((lvl - 0.6) / 0.4, 0.6)` (`Fireflow.cpp:633-641`, constants `kLvlCompSplit = 0.6`, `kCompTop = 0.7`, `kCompShape = 0.6`) | **Deck balance does not transfer.** The lower zone of `LVL` is pure output gain via `set_part_level` (`:637`), and flow never calls `set_part_level` at all — both decks sit at 1.0 under Glow. **And the veto has the last word:** `kVetos` forces `P_COMP_B` into **0.40..0.60** (`taste.h:644`, enforced at `flow.cpp:577-582`), so *every* `LVL` position lands in that band whatever this formula produces. That band is a by-ear ruling (`spotykach-by-ear-decisions`); the converter reports the rewrite, it does not work around it. **This contradicts spec §5's prose**, which assumed the knob transfers raw ("a COMP dialled to 0.85 transfers perfectly and is heard at 0.60"); with the formula 0.85 gives 0.528, which is in band and is what the Fireflow patch actually sounded like. **Corrected in §5 on 2026-08-12 (owner-approved), and §5 now points here as the authority** — this row is the source of truth. The formula is right — copying a merged control's knob value is precisely the `fireflow-control-merge-init-trap` failure |
| `P_LINK_A` / `P_LINK_B` | `LINK_A` / `LINK_B` | **direct** (`Fireflow.cpp:593` — `set_link(p, params[p ? LINK_B : LINK_A])`) | `configParam<LinkQuantity>(c.id, 0.f, 1.f, …)` (`:334-335`), display quantity only. Note the **explicit ternary**: `LINK_A/B` are appended params outside `PART_STRIDE`, so `pp(LINK_A, p)` would read the wrong slot. A converter that indexes by stride has the same bug available to it |

### Global

| flow param | Fireflow | conversion | notes |
|---|---|---|---|
| `P_MORPH` | `MORPH` | **direct** (`Fireflow.cpp:871`) | Plain `0..1` knob |
| `P_COUPLE` | `COUPLE` (printed **`FREE\|GRID`**) | **formula, zone split.** `k = COUPLE`, `grid = k >= 0.5` (`kCoupleZoneSplit`, `Fireflow.cpp:34`); `P_COUPLE = grid ? (k - 0.5) / 0.5 : k / 0.5` (`Fireflow.cpp:878-883`) | Store the rescaled half-zone value, **not the knob**: flow's `apply_param` hands `P_COUPLE` straight to `set_couple` (`flow_params.h:186`), which is what Fireflow's rescaled value feeds. This is also the sole source of `set_sync` in Fireflow (`:880`) — see `P_MODE` |
| `P_CHOKE` | `CHOKE` | **formula.** `P_CHOKE = CHOKE * 0.5` (`Fireflow.cpp:898`) | The knob is `configSwitch(c.id, -2.f, 2.f, …)` with 5 snapped states (`:320-324`); x0.5 lands them on flow's `-1..1` — `X(P_CHOKE, -1.f, 1.f, 0)` (`flow_params.h:96`). The five states are by-ear rulings (`spotykach-by-ear-decisions`) — the halving is the whole conversion, nothing rounds |
| `P_SHUFFLE` | `SHUFFLE` | **direct** (`Fireflow.cpp:560`) | Plain `0..1` knob |
| `P_REV_DIFF` | `REV_DIFF` | **direct** (`Fireflow.cpp:902` — `set_reverb_diffusion`) | Plain `0..1` knob. `REV_SIZE`/`REV_DECAY`/`REV_TONE` are story-owned, and `set_master_drive`/`set_reverb_smear`/`set_reverb_mod` are hardcoded constants in Fireflow (`:912-914`) — none of those are rows here |
| `P_TEMPO_BPM` | `TEMPO` | **formula, then clamp.** `bpm = 40 + TEMPO * 200` (`Fireflow.cpp:918`), i.e. **40..240 BPM**. `kParams[P_TEMPO_BPM]` is **50..140** — `X(P_TEMPO_BPM, 50.f, 140.f, 0)` (`flow_params.h:101`). Clamp, and **report anything outside** | An external clock at `CLOCK` overrides the knob at runtime (`:919-922`); that is live state, not patch state, and does not transfer |
| `P_MODE` | `COUPLE` | **formula, derived.** `P_MODE = (COUPLE >= 0.5) ? 1 : 0` — the COUPLE zone split is the *only* thing that drives `set_sync` in Fireflow (`Fireflow.cpp:879-880`), and `P_MODE` is the only thing that drives `set_sync` in flow (`Flow::push_mode_and_steps`, `flow.cpp:399, :405`) | **Not an independent control, and lossy in one specific way.** In Fireflow the per-deck step flag comes from each deck's own `STEPS` knob (`set_step(p, steps > 0, steps)`, `:843`) while `set_sync` is global; in flow, the single `P_MODE` drives `set_sync` **and both decks' step flags** (`flow.cpp:405-407`). So a patch with deck A free and deck B stepped — an ordinary ambient patch — has no representation. The converter must report whenever `(COUPLE >= 0.5) != (STEPS_A > 0)` or `!= (STEPS_B > 0)`. Spec §5 lists `P_MODE` under "no path at all"; that is about independence, not availability — the value *is* determined by the patch, and leaving it unset would hand a hand-authored stepped patch to the terrain's mode coin, which is worse |

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
2. **Nothing outside the 38 has a destination.** Not "is transferred with
   loss" — has no slot. That includes every story-owned parameter (FILT,
   COLOR, VARIATION, DENSITY, GRIT, REVMIX, DRIFT, TIDE, DRIVE, the reverb
   shape) as well as the flow params with no base rule but a Fireflow control:
   `STEPS_A`, `SUB_A`, `SONG_A`/`FORM_A`, `COMP_A`. And it includes everything
   outside `P_COUNT` entirely: sample content, `SOURCE_A/B`, `FLUXRATE`,
   `FLUXFB`, `DETUNE`, `STAGES`, `REC`, the excitation bus.

## Values a Fireflow patch can hold that flow cannot

The four places where a legal Fireflow value has no legal flow value. Each is a
clamp on the way in (`terrain.cpp:494`) and each must appear in the report:

| where | Fireflow can be | flow accepts | on conversion |
|---|---|---|---|
| `RES_A/B` | `0..1` | `0..0.75` | clamp to 0.75, report |
| `TEMPO` | 40..240 BPM | 50..140 BPM | clamp, report |
| `STEPS_B` | 0..16 | 2..16 (`s == 0` means STEP off) | `0` -> leave unset + `P_MODE = 0` question; `1` -> clamp to 2, report |
| `COMP_B` (`LVL`) | full travel | veto band 0.40..0.60 | value stored, band applied at runtime, report the rewrite |

## Where this file's claims come from

Everything above was read at `flow-patch-transfer` HEAD from:

- `engine/flow/taste.h` — `kBaseRules` (the 38), `kVetos`, `kCarrierEngine`
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
