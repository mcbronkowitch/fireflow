# Flow Patch Transfer Design

**Date:** 2026-08-11
**Status:** design approved, not implemented
**Parent:** [`2026-08-10-fireflow-touch-curated-places-design.md`](2026-08-10-fireflow-touch-curated-places-design.md)

## 1. Goal

Build a place by hand in the `Fireflow` module and carry it onto one of `Glow`'s
twelve pads, so that the pad recalls that patch's **skeleton** — engines,
tonality, tuning, envelopes, rhythm — while the terrain keeps supplying the
**story layer** the six macro knobs move.

The owner's constraint, stated verbatim: *"Was wichtig ist, dass zumindest die
Engine-Kombination wie im Patch ist. Wenn ich 2 Synth habe, darf im Terrain
nicht eins Sampler oder BBD sein."* He has also ruled out the alternative
authoring loop: browsing drawn terrains until one is good enough does not work
for him. He must be able to dial a patch and keep it.

## 2. Why the obvious design does not work

The obvious design — "give me a terrain code for this patch" — is not buildable,
and the near-miss version of it is worse than it looks. Both findings are
measured, not argued.

### 2.1 A terrain code is a seed, and search does not invert it

`TerrainState` is a 32-bit master plus six `uint16_t` reroll counters
(`engine/flow/terrain.h`). `generate()` expands it into `base[P_COUNT]`, the
macro maps, the weather and the adventure levels by pure table arithmetic over
`taste.h`. There is no inverse.

A probe searched random masters for the terrain nearest a target patch, with the
engine pair as a hard filter. The metric is `distance()`'s base term — mean
|Δ| per parameter, normalized by each parameter's own range — with the +0.25
archetype term dropped, because a raw patch has no archetype. Medians over 12
targets per family:

| target | best-of-1k | best-of-10k | best-of-100k |
|---|---|---|---|
| a real terrain (a perfect answer provably exists) | 0.052 | 0.041 | 0.037 |
| terrain, continuous knobs 15 % toward random | 0.076 | 0.066 | 0.063 |
| terrain, continuous knobs 35 % toward random | 0.117 | 0.110 | 0.101 |
| uniform random patch | 0.305 | 0.299 | 0.291 |

The control row is decisive: **even where a seed with distance exactly 0 exists,
100 000 draws reach only 0.037.** For calibration, two random terrains sit a
median 0.128 apart, and the closest of 4000 random pairs came to 0.041. The
search therefore returns a neighbouring place, never the target, and more draws
will not change that — 100 000 of 2^32 is 0.002 % coverage of a 63-dimensional
space.

### 2.2 Engine pairs: 21 of 36 exist, and that is structural

Over 200 000 random masters, percentage of draws per pair:

```
        B:    TONE   SYNTH    SMPL    WAVE    BODY     BBD
  A TONE     0.000   0.000   0.000   0.000   0.000   0.000
  A SYNTH    0.000   9.072   3.958   8.136   6.258   3.334
  A SMPL     0.000   3.895   0.000   3.553   2.818   0.000
  A WAVE     0.000   8.024   3.577   7.970   6.099   2.833
  A BODY     0.000   6.253   2.812   5.987   4.753   2.225
  A BBD      0.000   3.259   0.000   2.922   2.261   0.000
```

The zeros are construction, not sampling. `taste.h`'s stage-1 role tables split
the decks into a carrier drawn from `kCarrierEngine` (SYNTH, WAVE, BODY only)
and a texture drawn from `kTextureEngine` (all five real engines). TEST_TONE is
in neither list. So SAMPLER+SAMPLER, SAMPLER+BBD and BBD+BBD cannot be drawn —
the comment at `taste.h:660` calls this the "loud pair" rule and states it is
guaranteed by construction.

### 2.3 `base[]` is not what the instrument plays, for 25 of 63 parameters

`Flow::eval_terrain` (`flow.cpp:291`) seeds `out[p] = t.base[p]` and then, for
every curve of every macro, overwrites `out[c.param]` with the curve value.
Because the `has[c.param]` guard starts false, the first curve to target a
parameter always wins that branch. **For any parameter a story owns, `base[]`
never reaches the instrument.**

It is not merely ignored. Where two macros target the same parameter, the winner
is the candidate farthest from `base[c.param]` (`d = fabs(v - t.base[c.param])`).
Writing a hand-dialled value there does not set the parameter; it re-decides
which knob owns it across the whole sweep.

### 2.4 The reroll counters cannot move the base patch

Every draw in stages 0–3 seeds `make_stream(st.master, …, 0)` with the counter
pinned at literal zero: archetype (`terrain.cpp:243`), roles and engines (`:262`),
`adventure_base` (`:287`), scale and root (`:303`), mode (`:327`), and all base
rules (`:346`). Only stage 4 (macro maps, per-macro adventure) and stage 5
(weather) read `st.reroll[]`.

Two consequences. First, the probe's reroll hill-climb column was measuring an
axis that structurally cannot touch 38 of 63 parameters; its 0.003–0.004 gain is
what that predicts and carries no information about the search space. Second,
and usefully: **a pad's hold gesture (`new_partial`) already preserves the engine
pair, scale, root, mode and every base parameter bit-identically.** The owner's
hard requirement is already met in play. It was only ever at risk in the
transfer.

## 3. The seam

`taste.h` partitions the parameter space horizontally, and its own coverage test
enforces the partition:

- **38 parameters** are set by `kBaseRules` — one value per terrain, drawn from
  the master alone. Counted directly: `kBaseRules` has exactly 38 rows.
- **25 parameters** are story-owned — what the six macro knobs move. 22 are
  storied in every terrain; the remaining 3 belong to whichever DENSITY variant
  the seed picked, so their membership is a per-seed coin flip.

38 + 25 = 63 = `P_COUNT`.

This is the seam, and it is not the discrete/continuous line. Everything on the
base side is a value the owner can dial and keep. Everything on the story side
is a curve, not a number — he could not author it in Fireflow even if the
transport carried it.

**Ruling (owner, 2026-08-11): the patch carries the base side; the terrain keeps
the story side.** A transferred place therefore arrives with its own engines,
tonality, tuning, envelopes and rhythm, and with the terrain's filter, reverb,
dirt and drive character. This is a deliberate loss, not an oversight — see §8.

## 4. The overlay

### 4.1 Type

Added to `engine/flow/terrain.h`:

```cpp
// A hand-authored base patch riding alongside a seed. Indexed by ParamId
// rather than packed to the 38 base-rule slots: the packed form would need a
// second index table that could drift from kBaseRules, and 315 bytes is not
// worth that risk. Trivially copyable on purpose -- Place holds one, and
// Glow.cpp copies the whole Place array to the audio thread.
struct BaseOverlay {
    float v[P_COUNT]   = {};
    bool  has[P_COUNT] = {};
};
```

`static_assert(std::is_trivially_copyable<BaseOverlay>::value, …)`, for the same
reason `Place` already carries one.

Also added, beside the existing `arch_of()`:

```cpp
// True if `param` is set by a kBaseRules row -- i.e. if an overlay entry for it
// is honoured. Derived from the table, never hardcoded, so a taste.h change
// moves this with it.
bool is_base_rule(int param);
```

### 4.2 Where it applies

```cpp
Terrain generate(const TerrainState& st, const BaseOverlay* ov = nullptr);
```

The overlay is applied **after the stage-1 engine write and before stage 4**
(`terrain.cpp`, between the engine assignment around line 399 and the stage-4
macro-map loop that begins at line 401), by iterating `kBaseRules` — never by
iterating `P_COUNT`:

```
for each row br in kBaseRules:
    if ov && ov->has[br.param]:
        t.base[br.param] = clamp_to(kParams[br.param], ov->v[br.param])
```

Three properties follow from that position and that loop, and each is worth
stating because each is load-bearing:

- **Story parameters are unreachable through the overlay.** Their bits are never
  read. Setting one is inert by construction rather than by convention, which is
  what makes §3's ruling testable instead of merely documented.
- **`apply_constraints()` still gets the last word.** It runs at
  `terrain.cpp:476`, after stage 4, so the BODY FILT floor and the
  no-double-high-density rule see the overlaid engines and values.
- **`kParams` clamping happens on the way in**, so an out-of-range value from a
  host with a wider knob cannot enter the terrain at all.

### 4.3 `a_carries` must be recomputed

`Terrain::a_carries` is drawn from a coin in stage 1 and published because `Flow`
needs it for the staggered discrete switch and the duck schedule. If the overlay
sets `P_ENGINE_A`/`P_ENGINE_B`, the drawn coin can name a deck that now holds a
texture-only engine, and `switch_phase_for()` then rides SCALE and ROOT with the
wrong deck.

After applying the overlay, recompute:

- exactly one of the two decks holds an engine in `kCarrierEngine` → that deck
  carries;
- both do → keep the drawn coin;
- neither does → **the overlay is rejected and `generate()` falls back to the
  drawn engines**, because a terrain with no carrier has no defined role
  structure. Today this case is reachable only for the four pairs §2.2 lists as
  unreachable by drawing; see §10 for the pending decision on whether to make
  them legal.

The converter (§5) reports a rejection rather than letting it pass silently.

### 4.4 `Flow` carries it, including the undo slot

`engine/flow/flow.h` gains `BaseOverlay _overlay; bool _have_overlay = false;`
beside `_state`, and:

```cpp
void wake(const TerrainState& s, const BaseOverlay* ov);   // ov == nullptr keeps today's behaviour
```

There are exactly two `generate()` call sites in `flow.cpp` — `wake()` at line 88
and `begin_blend()` at line 149. Route both through one private
`Terrain gen(const TerrainState&) const` that passes the stored overlay. Reroll,
undo and the blend then inherit it with no further edits.

**The undo slot must carry the overlay too.** `_undo` is a bare `TerrainState`
today (`flow.cpp:93, 147, 209, 221`). Undoing across two pads with different
overlays would otherwise combine one place's base with another's stories —
silently, with nothing in the code able to catch it. `_undo` gains a companion
overlay, and `restore_undo()` takes one, so a patch load restores the pair.

## 5. The converter and its report

New file `host/vcv/src/flow_patch_bridge.hpp` — no Rack types, no jansson, no
widgets, so the desktop doctest suite tests it headlessly. This is the same
split `touch_pads.hpp` and `glow_ui.hpp` already use.

It takes Fireflow's parameter values and produces a `BaseOverlay` **plus a
report of what it could not carry**. The report is the deliverable, not a
nicety: it is what tells the owner which knobs to stop dialling.

```cpp
struct TransferNote { int param; const char* reason; };   // param may be -1 for "no flow param"

// At most one note per parameter, plus room for the handful that name no flow
// parameter at all (GRIT's mode, the deck balance, the no-carrier rejection).
inline constexpr int kMaxNotes = P_COUNT + 8;

struct TransferReport {
    BaseOverlay  overlay;
    TransferNote notes[kMaxNotes];
    int          note_count = 0;
    bool         overlay_rejected = false;   // §4.3's no-carrier case
};
```

The conversions that are not identity, each of which is a place this has
silently gone wrong before (see the `fireflow-control-merge-init-trap` memory —
this repo has been bitten four times by exactly this class of change):

| Fireflow surface | flow parameter | conversion |
|---|---|---|
| ENGINE knob, 0..4 | `P_ENGINE_A/B`, `EngineId` 0..5 | renumber: 0→SYNTH, 2→WAVE, 3→BODY, 4→BBD, everything else→SAMPLER. Position 1 becomes TEST_TONE only when the part's separate `testTone` flag is set, so it is not a knob position and does not transfer |
| CHOKE | `P_CHOKE` | ×0.5 |
| DRIFT | `P_DRIFT` | settle-zone rescale |
| COUPLE | `P_COUPLE` | grid-zone split; also the only source of `set_sync` |
| TEMPO, 40..240 | `P_TEMPO_BPM`, 50..140 | clamp, and report anything outside |
| COMP knob (is LVL) | `P_COMP_A/B` | drives `set_part_level`, which flow never calls |
| GRIT, bipolar | `P_GRIT_A/B`, unipolar | magnitude only; the mode is lost |
| SONG rung | `P_FORM_*`, `P_SONG_*` | 14 curated rungs of 35 combinations |

And the parameters with no path at all, which the report must name every time:

- **`P_ROOT`** — Fireflow has no ROOT control; `set_root` does not appear in
  `Fireflow.cpp` at all.
- **`P_FORM_A/B`** — no FORM control since the 2026-08-09 control reduction.
- **`P_MODE`** — not independent; it rides COUPLE's zone split.
- **`P_DRIVE`, `P_REV_SMEAR`, `P_REV_MOD`** — hardcoded in `Fireflow.cpp`.
- **the 25 story parameters** — dropped by §3's ruling.

Finally, the report must state the **veto rewrites** before the owner signs a
place off, because they are by-ear rulings the memory index says not to "fix":
`P_COMP_A/B` forced into 0.40–0.60, `P_REVMIX_A/B` floored at 0.08, `P_DRIVE`
capped at 0.40, `P_REV_MOD` capped at 0.25, `P_RES` capped at 0.75 by `kParams`.
`P_COMP_B` is on the base side, so it transfers — but not by copying the knob.
The COMP knob is LVL, and Fireflow runs it through a split/power curve before
`set_comp` (`Fireflow.cpp:633-641`), so an LVL dialled to 0.85 is a comp amount
of ~0.528, which is what transfers and what is heard: in band, and what the
Fireflow patch genuinely sounded like. `docs/flow-fireflow-param-map.md` is the
authority for this and every other conversion; the converter implements that
table and invents nothing.

**`flow_params.h`'s "verified against Fireflow.cpp's configParam" comment is
stale** — it cites a `FORM_A/B configSwitch` that no longer exists. Correcting
it is part of this work, since it is the evidence the discarded design leaned on.

## 6. Storage

- **`Place`** (`host/vcv/src/touch_pads.hpp`) gains a `BaseOverlay`. It stays
  trivially copyable, so `UiOp::SET_PLACES` remains a `memcpy` with no
  allocation; the twelve-place payload grows from roughly 2.1 KB to about 6 KB,
  which is bounded and stays off the heap.
- **The code stays `F1` at 24 characters.** `kTerrainCodeLen` does not move,
  `decode_code` keeps its exact-length check, and `Place::code` keeps its size.
  The code now names the story layer, which is the only thing it was ever able
  to name. A place with an overlay is not fully described by its code, and the
  UI must not imply otherwise.
- **`pool.tsv`** gains one column for the overlay. `export_pool_tsv`'s existing
  `fp` column stays reserved and empty — it has no producer yet, and adding a
  second one here in a second language is exactly the divergence its gate is
  meant to catch.
- **Glow's JSON** gains the overlay per place, alongside `name` and `note`.

## 7. Wish-filtered seed selection

Because archetype, roles, engines, tonality and mode are pure functions of the
master at counter zero (§2.4), a seed can be **ordered** instead of searched.
Three small pure functions beside the existing `arch_of()`, in
`engine/flow/terrain.{h,cpp}`:

```cpp
void roles_of(uint32_t master, int& engine_a, int& engine_b, bool& a_carries);
void tonality_of(uint32_t master, int& scale, int& root);
int  mode_of(uint32_t master);
```

`arch_of()` set this precedent already: `draw_new`'s genre branch uses it to
reject candidates before paying for a full `generate()`. A wish like
"SYNTH + WAVE, Dorian, STEP" is then satisfied **exactly**, at a hit rate near
1 in 300 masters — milliseconds, and no approximation anywhere.

This is not a replacement for the overlay. It is what makes the seed's own
contribution deliberate rather than arbitrary.

## 8. What does not transfer

Stated plainly, because the owner will build twelve places against it:

1. **The character half.** Filter, reverb amount and shape, dirt, drive,
   variation, tide, drift — all story-owned. A transferred place has its own
   skeleton and the terrain's character.
2. **ROOT, FORM and the GRIT mode.** Not authorable in Fireflow at all.
3. **MODE per deck.** Flow has one global `P_MODE` driving `set_sync` and both
   decks' step flags. A Fireflow patch with deck A free and deck B stepped — a
   free pad under a stepped arp, an ordinary ambient patch — has no
   representation in the flow layer.
4. **Deck balance.** Fireflow's LVL drives `set_part_level`; flow never calls
   it, so both decks sit at 1.0 under Glow.
5. **Six veto bands and three caps**, listed in §5.
6. **Anything outside the 63.** The excitation bus, sample content, `SOURCE_A/B`,
   `FLUXRATE`/`FLUXFB`, `DETUNE`, `STAGES`. A patch built around sampler content
   is not transferable by any format.

## 9. Testing

Against the `fireflow-vacuous-test-gates` memory: each gate below must be shown
red once, for the reason it names.

- **The overlay is honoured on the base side.** Generate with and without an
  overlay setting every `kBaseRules` parameter; assert each lands. RED by
  skipping the apply loop.
- **The overlay is inert on the story side.** Set every story parameter's bit;
  assert the generated terrain is unchanged. RED by looping `P_COUNT` instead of
  `kBaseRules` — which is precisely the mistake this guards.
- **`is_base_rule()` agrees with the table.** Assert the base-rule set and the
  story-target set are disjoint and cover `P_COUNT`, reading both from `taste.h`
  rather than from a transcribed list.
- **`a_carries` follows the overlaid engines.** Overlay an engine pair that
  contradicts the drawn coin; assert the carrier deck moved. Separately assert
  the no-carrier case is rejected and falls back.
- **Undo carries the overlay.** Wake pad 1 with overlay X, wake pad 2 with
  overlay Y, undo; assert the terrain matches pad 1's overlay and not pad 2's.
  RED by leaving `_undo` a bare `TerrainState`.
- **The reroll preserves the overlay.** Hold-reroll a transferred place; assert
  all 38 base values are bit-identical afterwards.
- **The converter reports its losses.** For a patch exercising ROOT, FORM, MODE,
  GRIT mode and an out-of-range TEMPO, assert the note count and each note's
  parameter. RED by returning an empty report.
- **The round trip through `pool.tsv` and JSON is lossless** for the 38 values.

The desktop suite compiles `host/vcv/src/*.hpp` already
(`target_include_directories(spky_tests PRIVATE host)`), so the bridge and the
overlay are testable without Rack.

## 10. Open decisions

**Widening `kCarrierEngine` is a separate spec and a decision with a deadline.**
Making SAMPLER+SAMPLER and SAMPLER+BBD legal means widening the carrier table —
which re-resolves every existing terrain code, including `kHouseCode` and Glow's
twelve default draws. It is cheapest now, because curating has not started; once
twelve places exist it is expensive. It is deliberately not part of this spec:
its risk profile is global where this one's is local. Until it is decided, §4.3
rejects a no-carrier overlay and the converter says so.

**The measurement nobody has run.** Every target patch in §2.1 was synthetic —
perturbed terrains or uniform draws. A patch the owner actually builds is
neither. Two or three saved Fireflow patches, pushed through the converter and
rendered, would say how much of a real patch survives §8 before twelve places
get built against it. This validates the design; it does not block it.

## 11. Scope

In: the overlay in `engine/flow/`, §7's wish filters (`roles_of()`,
`tonality_of()`, `mode_of()` beside the existing `arch_of()`), the converter
header, the two menu items, `Place`, `pool.tsv`, Glow's JSON, the stale comment
in `flow_params.h`, and `docs/flow-fireflow-param-map.md` — the authoritative
parameter map the converter implements.

Out: the `kCarrierEngine` widening (§10), the firmware — `shell/` has no flow
code at all yet, so the format reaches it whenever the flow layer does, and not
before.
