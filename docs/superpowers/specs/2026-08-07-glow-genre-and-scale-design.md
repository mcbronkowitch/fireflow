# Glow GENRE and SCALE: two explicit controls over what NEW may draw

**Date:** 2026-08-07
**Scope:** two new panel controls on Glow, flanking NEW. One constrains which
archetype NEW draws; one overrides the tonality outright. The Fireflow module
is untouched. No engine DSP changes.

## 1. Why

The complaint is that Glow is still chaotic. Two entries in
`docs/superpowers/specs/2026-08-05-flow-listening-notes.md` say where a good
part of it comes from, and both are structural rather than a matter of taste:

- **Item 8 — pressing NEW always changes archetype.** `distance()` adds a flat
  `+0.25` when two archetypes differ while `kDistanceMin` is `0.18`, so the
  bonus alone clears the bar. Across 3 000 `draw_new` calls the result had the
  same archetype as its origin **0 times**. On an instrument weighted 50 %
  drone, *you can never get from one drone to another.*
- **Item 9 — a woken terrain can open with seconds of silence**, up to 18.4 s,
  with roughly half the drone terrains above 4 s.

Neither can be tuned by ear today, because auditioning ten drones in a row is
impossible: every NEW press throws you out of drone, and into a different key
and scale as well. This spec builds the tool that makes the tuning sessions
possible. The tuning itself — starting with drone — is a later spec.

The taste layer is *already* organised per archetype: `BaseRule::per_arch[ARCH_COUNT]`
(`taste.h:897ff`), `kCarrierW`/`kTextureW`/`kModeW` are all per-archetype rows.
Nothing new has to be invented to tune one genre; only a way to stay inside it.

## 2. GENRE — a constraint on the draw, not on the terrain

### 2.1 Control

A snapped 5-position switch at panel mm **(10.48, 78.0)**, left of NEW:

| Position | Meaning |
|---|---|
| ANY (default) | today's weighted draw, `kArchWeight = {0.5, 0.2, 0.15, 0.15}` |
| DRONE / PULSE / ARP / FRAGMENT | NEW may only land in that archetype |

Turning it **changes no sound**. It only says what the *next* NEW press may
draw. Rationale for not re-drawing on turn: sweeping ANY → FRAGMENT would
scrub through PULSE and ARP and hand the player two terrains nobody asked for,
and LOCK refuses NEW, so an instant-redraw knob would need a special rule for
the locked case. This design needs none.

ANY is the default so that a param absent from an older patch restores to
today's behaviour. **The leftmost detent is where the randomness lives** — this
is why neither switch participates in Rack's Randomize (§4.4).

### 2.2 Mechanism: rejection sampling on the master

The archetype is a pure function of the master seed —
`Rng r = make_stream(st.master, kStreamArch, 0); pick_weighted(r, kArchWeight, ARCH_COUNT)`
(`terrain.cpp:247-250`), counter pinned at 0, with no dependence on adventure,
roles, or anything drawn later. `terrain.cpp:509-513` and `:559-563` both
assert this invariant independently as load-bearing.

So a genre lock needs **no `TerrainState` field and no terrain-code format
change**: filter the masters. Codes decode exactly as before, `undo` is
untouched, and a shared code still carries its own genre. The rejected
alternative — an `arch` override field in `TerrainState` — would change the
mapping master → terrain, break code stability, and force format v2 for
nothing.

New API. `ARCH_ANY` goes in `flow_ids.h` beside the enum it extends, not in
`terrain.h`:

```cpp
// flow_ids.h
enum Archetype { ARCH_DRONE = 0, ARCH_PULSE, ARCH_ARP, ARCH_FRAGMENT, ARCH_COUNT };
constexpr int ARCH_ANY = -1;          // "no constraint" for draw_new

// terrain.h
Archetype arch_of(uint32_t master);   // stage 0 alone, no full generate()
TerrainState draw_new(const TerrainState& cur, Rng& seq, int want = ARCH_ANY);
```

`arch_of` exists for cost, and it matters: `new_full` runs on the audio thread
via `controlTick`. At the rarest weight (0.15) roughly every seventh master is
usable, so filtering with the full `generate()` would cost ~54 of them per
press. Filtering with `arch_of` first costs at most 256 cheap stream-seeds plus
**8** `generate()` calls — *fewer* than today's 16-try loop.

`draw_new` branches:

- **`want == ARCH_ANY`** — today's loop verbatim: same RNG draws, same result
  for a given `(cur, seq)`. Do not repeat the default argument in the `.cpp`
  definition.
- **`want == an archetype`** — draw masters off `seq`, skip `master == cur.master`,
  keep those whose `arch_of()` matches, and once **`kGenreCandidates = 8`**
  matches have been collected return the one with the greatest `distance()`
  against `generate(cur)`. **No `kDistanceMin` gate in this branch.** A hard
  cap of **`kGenreDrawCap = 256`** total draws guarantees termination.

### 2.3 Why no threshold, and what happens to the +0.25

Two separate points, and the second corrects an earlier framing of the first.

**The gate would be decorative.** `terrain.cpp:542-549` measured that *no*
same-archetype pair out of 6 603 clears `kDistanceMin` on its base patch. Under
a genre lock every candidate is same-archetype, so the gate would reject all of
them and the 16-try fallback would decide every press — a rule that reads like
a rule and never fires. A genre-local threshold was considered and rejected: it
would have to be derived from a measured percentile, and these tuning sessions
will move `taste.h` constantly. That distribution already shifted once from a
single commit (mean 0.1569 → 0.1229 at the per-domain adventure draw,
`terrain.cpp:551-558`). Best-of-N is immune to that churn and has one honest
number.

**Provenance of that measurement:** it was taken at `89eb461`, *before* the
2026-08-07 weighted scale draw changed `base[P_SCALE]`'s distribution. It has
not been re-measured since. The conclusion very likely survives (one param of
63), but it is cited here as a 2026-08-06 measurement, not as a current fact.

**The archetype term does not vanish — it cancels.** Under a lock all eight
candidates share `cur`'s archetype, so `+0.25` is a constant offset on every
candidate and drops out of the argmax exactly. That is the real reason
best-of-N is sound, and it is a stronger statement than "we dropped the
threshold".

**Therefore the fallback must stay inside the genre.** Keeping the farthest
candidate *regardless* of archetype would be wrong precisely because of the
cancellation: across archetypes the `+0.25` dominates and would guarantee a
lock-breaking result. `best` holds only genre-matching candidates. The
exhausted-cap branch is unreachable in practice — with `min(kArchWeight) = 0.15`,
256 draws yield a mean of 38.4 matches (sd ≈ 5.7); fewer than 8 is beyond 5σ
and zero matches is ≈ 10⁻¹⁸ — and is documented as such in the style of
`terrain.cpp:601-607` rather than tested.

### 2.4 Runtime

`Flow` gains `set_genre(int)` / `genre()`, forwarded to `draw_new` in
`new_full()`. `new_partial` is untouched: partial reroll keeps the master, and
`reroll[]` never reaches `kStreamArch`, so the archetype provably cannot move.

Note explicitly: `Flow::_genre` is state that lives in neither `TerrainState`
nor `wake()`/`init()`. `Flow` stops being a pure function of
`(TerrainState, macros)`. It is harmless because Glow re-pushes it every
control tick, but it is a real change to the class's contract.

`draw_new`'s doc comments at `terrain.h:118-125` and `terrain.cpp:592-607`
state a contract this change makes false. Both are rewritten in the same
commit.

## 3. SCALE and ROOT — a live override

### 3.1 Control

A snapped 14-position switch at **(50.48, 78.0)**, right of NEW: AUTO
(default) plus the 13 scales. Labels come from `SCALE_NAMES`
(`quantizer.h:53-57`), which exists "so the two lists cannot drift apart" —
they are not retyped into the host.

Knob order is **by friction**, so the travel runs from calm to sharp, rather
than `ScaleId` order which is by provenance. A permutation table
`kScaleKnobOrder[13]` maps knob position → `ScaleId`:

| Knob positions | Scales | `kScaleW` |
|---|---|---|
| 1–2 | Minor pent, Major pent | 0.175 |
| 3–6 | Aeolian, Dorian, Mixolydian, Lydian | 0.1125 |
| 7–9 | Hirajoshi, Pygmy, Kumoi | 0.0667 |
| 10–13 | Phrygian, Hijaz, Harmonic minor, Whole tone | 0.025 |

This is the ordering already encoded in `kScaleW` (`taste.h:709-720`), read
descending. It is not re-derived by feel, and §5 pins the two together with a
test so a `kScaleW` retune cannot leave the knob order silently stale.

ROOT lives in the right-click menu: AUTO plus 12 note names. On real hardware
there is no context menu, so the M6 panel will either leave root on AUTO or
give it a control — a panel question, not a reason to withhold it in Glow.

### 3.2 Why an override is safe here

`P_SCALE` and `P_ROOT` are pure base parameters. Grepped across the tree, their
only `taste.h` occurrences are the two `kBaseRules` rows at `:915-916`; no
`kStories` entry targets them, no `kVetos` row covers them, and `generate()`
writes them once at `terrain.cpp:395-396`. `apply_param`
(`flow_params.h:120-121`) is the only writer, and it fans the root out to both
parts. So no macro curve can fight the override.

### 3.3 Where the override is applied — and where it must not be

`Flow` gains `set_scale_override(int)` / `set_root_override(int)`, `-1` meaning
AUTO. In `recompute_and_push`, an active override replaces the value for that
param **after `quantize_hyst` has run**, not instead of it.

This ordering is not a detail; the obvious alternative is a bug.
`kHysteresisFrac = 0.5f` (`taste.h:583`) and `quantize_hyst`
(`flow.cpp:242-244`) compares strictly:

```cpp
if (force || x > n + 0.5f + kHysteresisFrac || x < n - 0.5f - kHysteresisFrac)
    _step_now[p] = nearest;
```

For `P_SCALE` (13 steps over 0..12) and `P_ROOT` (12 over 0..11) the step size
is exactly 1 and every candidate is an integer, so an un-forced one-step move
**never** passes that guard — forcing at the switch phase (`flow.cpp:451`) is
the only thing that ever moves the scale. If the override skipped
`quantize_hyst`, `_step_now[P_SCALE]` would freeze. Press NEW while the
override is held onto a terrain whose scale differs by exactly one step
(p ≈ 2/13 per press), then return to AUTO: `|x − n| == 1`, the guard fails, and
the stale step is returned — **Glow sits on the wrong scale indefinitely**,
until some later terrain happens to move it by two or more steps. Running
`quantize_hyst` first and overriding its result keeps `_step_now` and
`_disc_done` tracking the terrain, so AUTO resumes correctly on the next tick.

A value arriving from JSON goes through `clamp_to(kParams[p], v)`;
`flow.cpp:544-553` documents that `param_now()` must never publish an
out-of-range value.

### 3.4 Consequences, all intended

- **Immediate.** Unlike GENRE, SCALE is a setting, not a draw rule. Turning it
  is heard at once. A scale change is discontinuous by nature; there is nothing
  to blend.
- **The terrain is untouched.** Its own scale is still in `base[]` and returns
  the moment the knob goes back to AUTO. Nothing is lost.
- **Chatter stops.** Listening-notes item 7 measured `P_SCALE` changing 258
  times inside one 6 s blend from hysteresis chatter. Under an override the
  pushed value is constant.

### 3.5 Accepted: an override fires without a duck

`switch_phase_for` (`flow.cpp:318-330`) deliberately rides SCALE and ROOT with
the carrier deck so the tonality change lands with the lead voice, and
`begin_blend` (`flow.cpp:160-182`) opens a reverb duck around it so no switch
of either kind happens outside a wash. A hand-turned override has neither: a
14-position knob dragged across its range fires one un-ducked scale change per
detent. **Accepted for now, by ear, deliberately** — it is a conscious hand
movement, not an automatic event. Routing the override through the existing
duck stays available if it grates. Related: listening-notes item 4 already
flags an audible edge on the duck itself.

## 4. Host: Glow.cpp, panel, persistence

### 4.1 Threading

Both switches are ordinary params: `controlTick` runs on the audio thread from
`process()` via `ctrlDiv` (`Glow.cpp:473`, divider `kCtrlInterval`) and already
reads `params[].getValue()` for the macros the same way. `configSwitch` sets
`smoothEnabled = false`, so the snapped integer arrives with no smoothing lag.
No `UiOp` needed.

**The ROOT menu item is different and must not be a plain `int`.** It is
written from `appendContextMenu`'s lambda on the UI thread and read every
control tick on the audio thread — a data race. The existing `UiOp` mechanism
(`Glow.cpp:110-126`, `:364-380`) is the wrong shape: it is a one-shot
`exchange()`, while the root override is a standing value. Use
`std::atomic<int> rootOverride{-1}`, matching `uiOp`'s default seq_cst. The
same applies to `dataFromJson`'s live branch (`Glow.cpp:196-210`), which the
file itself documents as UI-thread code.

**Ordering inside `controlTick`:** push genre and both overrides into `Flow`
**before** the `uiOp.exchange()` block at `Glow.cpp:364`. `SET_TERRAIN` and
`RESTORE` call `flow.wake()`, which force-pushes every parameter through
`recompute_and_push(true)`; pushing afterwards would land one tick on the
terrain's own scale after every patch load.

### 4.2 Persistence

GENRE and SCALE are params, so Rack saves them with no code. ROOT needs JSON:

- `dataToJson` writes the key.
- `dataFromJson` reads it **before** the early return at `Glow.cpp:168-169`
  (`if (!json_is_string(code)) return;`). Otherwise a patch with a root
  override and a missing or corrupt terrain string silently drops the
  override.
- `onReset()` (`Glow.cpp:354-358`) clears it. That override is the *deprecated*
  Rack hook, invoked by the default `Module::onReset(const ResetEvent&)` after
  params are reset — so Initialize returns GENRE and SCALE to ANY/AUTO by
  itself, but knows nothing about a non-param member.

### 4.3 The two widget sites

`Glow.cpp:130-140` and `:539-550` are two-way `if (WK_MACRO) … else …`. A new
kind falls into the `else`, which calls `configButton` (a 0..1 momentary,
`randomizeEnabled = false`) and hard-wires the light id `NEW_L` — three widgets
driving one lamp, compiling cleanly. Both become explicit three-way `switch`es
with no silent default.

### 4.4 Randomize

`configSwitch` (`Module.hpp:152-159`) does not touch `randomizeEnabled`, which
defaults to `true` (`ParamQuantity.hpp:63`); `configButton` sets it false.
Glow does not override `onRandomize`, so today Randomize moves only the six
macros. Both new switches set `randomizeEnabled = false`. The reason is not
timidity: **the leftmost detent already is the random setting.** ANY draws the
archetype at random and AUTO takes whatever the terrain drew, so the player
selects randomness at the control. Letting Randomize also pin the instrument to
Whole tone would remove a choice, not add one.

### 4.5 Panel

`res/gen_flow_panel.py` needs more than a table row:

- A third `WidgetKind` (`WK_SEL`) with entries in `RADIUS`, `LBL_DY`, `LBL_SZ`
  and `WKMAP` (`:64-67`) — a missing key is a `KeyError` inside `radius_of()`,
  which three geometry tests call.
- A third draw function. `knob_svg` would `KeyError` on `MACRO_ACCENTS`
  (`:46-53`); `button_svg` emits `id="newCopperCollar"` (`:156`) and would
  produce three elements sharing one SVG id, which
  `test_mockup_style_is_present_on_the_rendered_glow_panel` cannot catch
  because it tests with `in`, not a count.
- **Radius 5.5 mm, `LBL_DY` 7.3**, matching NEW's visual weight rather than the
  macros'. At macro radius 8 the captions would land at y = 89.4 while the
  patch-field rect starts at y = 89.0 (`:228`) — the captions would print on
  its top border, and no existing test models that
  (`test_patch_field_has_no_second_horizontal_rule` inspects `<line>` elements
  only). At 5.5/7.3 all three controls at y = 78 share one caption baseline at
  85.3. Clearances hold: 20 mm to NEW (needs 10), 24 mm to the macro row
  (needs 13.5), 4.98 mm and 55.98 mm to the panel edges (needs 2.0 … 58.96).
- `PARAM_ORDER` (`test_flow_panel.py:30`) is extended with both entries
  **appended after `NEW_BTN`**, even though they sit left and right of it on
  the panel. Enum order is the frozen contract that pins the first six params
  to the macro enum; panel position is independent of it.
- `res/Glow.svg` and `src/generated_flow_panel.hpp` are regenerated in the same
  commit (`test_committed_files_match_the_generator`).

`static_assert(ARCH_DRONE == 0)` sits next to the knob→archetype mapping in
Glow.cpp: the `want = pos - 1` arithmetic depends on it silently, and the six
macro `static_assert`s at `Glow.cpp:35-46` set that precedent.

### 4.6 Menu: show the archetype

`arch_of(flow.state().master)` is now available and free. The context menu
gains an archetype label beside the existing `"Terrain " + terrainCode()` line
(`Glow.cpp:568`). Without it the player cannot see which genre the current
terrain is, nor whether a lock took effect — and this whole feature exists so
the owner can audition one genre at a time.

### 4.7 Not fixed: NEW held while turning the new knobs

The knobs are correctly immune to the mark gesture — `Gesture::knob_delta`
rejects `macro < 0 || macro >= MACRO_COUNT` (`gesture.h:75`), and both
`Glow.cpp:382-403` and `KnobTracker` (`glow_ui.hpp:33-56`) are fixed at
`MACRO_COUNT`. But holding NEW still runs the hold timers, so adjusting an
adjacent knob for 1.5 s or 5 s and then releasing fires UNDO or LOCK. Noted,
not addressed.

## 5. Tests

The forms below are chosen so each can actually go red; three obvious
formulations cannot, and are called out.

**GENRE**

- `arch_of(m) == generate({m}).arch` over many masters. This is the load-bearing
  claim of the whole design — if the cheap path ever diverges, the filter
  selects phantoms.
- Genre lock holds: `draw_new(cur, seq, ARCH_DRONE)` returns a drone for every
  one of many origins. Red today by construction (measured 0 of 3 000).
- Best-of-N is the maximum: at a fixed seed, the result has the greatest
  `distance()` of the eight matching candidates.
- The genre branch never returns `cur.master`. The ANY branch's guarantee
  (`terrain.cpp:614`) is asserted separately at
  `test_flow_terrain_code.cpp:54`; the new branch's skip needs its own.
- **GENRE changes no sound**: for a fixed terrain and fixed macros, every
  `param_now(p)` is identical at ANY and at DRONE with no NEW press. This is
  the design's central safety claim and currently has no test.
- ANY is unchanged: **not** `draw_new(cur, seq)` versus
  `draw_new(cur, seq, ARCH_ANY)` — those are the same call through the default
  argument, a tautology that cannot fail. Pin the literal master the current
  code produces for a fixed `(cur, seed)`, in the shape
  `test_flow_terrain_code.cpp:46-55` already uses.
- `kGenreDrawCap` is **not** tested directly: `draw_new` returns a
  `TerrainState` and `Rng` exposes no draw count, so the assertion cannot be
  written. The cap is a documented termination guarantee, and the
  cap-exhausted branch is unreachable (§2.3).

**SCALE / ROOT**

- AUTO pushes identical values to today, for both params.
- The override holds across every macro setting and through a blend.
- **The stale-step case**: hold an override, press NEW onto a terrain whose
  scale differs by exactly one step, release to AUTO, assert `param_now(P_SCALE)`
  equals the new terrain's base. This is the only test that separates a correct
  implementation from §3.3's buggy alternative — the naive "returning to AUTO
  restores the scale" test passes against both, because it never presses NEW
  while the override is held.
- Releasing an override *mid-blend*, and at the `kCarrierStaggerFrac`
  boundary.
- The ROOT override reaches both parts. `apply_param` issues `set_root` to
  PART_A and PART_B; a host-side override affecting only one would be invisible
  to a `param_now` assertion.
- `kScaleKnobOrder` is a permutation of 0..12 **and** `kScaleW` is
  non-increasing along it. The permutation check alone tests the table, not the
  feature; the monotonicity check is what catches a `kScaleW` retune that
  reorders the groups and leaves the knob travel no longer running calm → sharp.
- JSON round-trip of the root override, including the `Glow.cpp:169`
  early-return path and the Initialize clear.

**Panel**

- The first six params are still exactly the macro enum; GENRE and SCALE exist
  and are not among them.

## 6. The terrain code stops being the whole state

`Glow.cpp:568-578` says "the code is the whole state (spec 4), so copying it
out and pasting it in is the entire sharing story", and `glow_ui.hpp:133` calls
`GlowSave` "exactly what a patch stores". A SCALE or ROOT override changes what
the listener hears while leaving the code unchanged, so a pasted code no longer
reproduces the sharer's sound. GENRE is exempt — it is inaudible by
construction and only constrains future draws.

Both comments are corrected in the same commit, and the overrides go into the
patch JSON so a reloaded patch sounds as it did when saved. The code stays the
terrain's identity; it is no longer the instrument's entire state.

## 7. Out of scope

The drone tuning itself. The controls exist so that ten drones can be heard in
a row and the actual defect named — the slow entrance (item 9), the ~15.9 dB
loudness spread (item 2), the discrete churn (item 7) are candidates, not
conclusions. Each later genre gets its own session and its own spec.
