# Flow Machine — compact macro module (design)

**Date:** 2026-08-05
**Status:** approved design, pre-plan
**Scope:** a portable macro/terrain layer over the engine plus a new compact
VCV module "Flow". The hardware expander and the M6 panel itself are out of
scope (doors kept open, nothing more).

## 1. Idea

A "flow machine": endless, always-evolving drones and pads from a module with
six knobs and one button. Every knob position sounds pleasantly ambient by
construction. **NEW** rolls a fresh "terrain" — a new generated instrument to
explore — while the knobs keep their fixed meanings. The existing full
Spotymod panel remains untouched as the full-control view; the later hardware
expander is a future product, not part of this design.

Decisions fixed during brainstorming:

- VCV prototype first; the macro layer must be portable so M6 firmware reuses
  it unchanged.
- 6 knobs + NEW. Fixed macro semantics; NEW rerolls the terrain underneath.
- The A/B duo is invisible: macros drive both decks; NEW also rolls the deck
  roles. The player only ever sees one instrument.
- I/O: stereo out, clock + reset in, CV over the four sound macros.
- Panel drawn at true hardware dimensions (see §6) so the VCV faceplate
  doubles as the 1:1 draft for the M6 panel.

## 2. Architecture

New portable component **`engine/flow/`** — pure logic on the normalized
`0..1` setters of `engine/instrument.h`. No hardware or Rack type crosses in.

```
engine/flow/
  terrain.h/.cpp   seed → full patch + macro mapping table (deterministic)
  taste.h          taste rules: allowed ranges & named constraints per param
  flow.h/.cpp      runtime: holds terrain + 6 macro values, computes final
                   params, runs the NEW crossfade, calls the setters
```

Consumers:

- **VCV module "Flow"** — second module in the Spotymod plugin. Embeds the
  same `Instrument`, talks only to `flow.h`.
- **Render host** — drives the flow layer from `scenario.json` (seed + macro
  rides), so terrains are testable and auditionable without Rack.
- **M6 firmware (later)** — reads 6 pots + 1 button, calls the same `flow.h`.

The flow layer encapsulates nothing away: it calls the same public setters as
any host. A full-control expander later just sets parameters directly.

## 3. The six macros

Four sound macros, two frame macros. Each macro has a fixed one-word meaning;
*what* it touches in detail is the terrain's choice — always inside that
meaning, always monotone (more knob = more of it).

| Macro | Meaning | Typical target pool (terrain picks subset + spans) |
|---|---|---|
| **MOTION** | how much everything moves | TIDE, DRIFT, mod-lane depths, REV SMEAR/WOBL, FLUX amount |
| **DENSITY** | how much happens | deck DENSITY, STEPS activity, COLOR (chord density), SONG/FORM movement |
| **BRIGHT** | spectral center | FILT of both decks, REV TONE, SUB balance, engine-specific timbre params |
| **DIRT** | clean ↔ driven | GRIT + COMP per deck, MASTER_DRIVE (threshold-aware, see below) |
| **PACE** | sense of time | TEMPO (free) / clock divider (synced), coupled envelope & RATE feel |
| **SPACE** | close ↔ vast | reverb sends of both decks + SIZE/DECAY on one coupled path |

**Macro math — replace, not add.** For every mapped parameter:
`value = lerp(lo, hi, curve(macro))` with terrain-chosen `lo/hi` inside the
taste limits. Unmapped parameters sit at their terrain base value. Every knob
position is therefore inside the allowed region by construction — the
"always pleasant" guarantee is arithmetic, not hope.

Guard rails inherited from project memory:

- **By-ear values become range centers, never get overwritten.** The tuned
  resonance cap, reverb gains and CHOKE states define the safe zones the
  terrain and macros move inside.
- **DIRT respects that MASTER_DRIVE is a threshold:** the macro primarily
  rides the deck GRITs; MASTER_DRIVE only joins in the top third of the
  macro's travel, because beyond the limiter's knee it stops controlling
  dirt.

**CV:** the four sound macros each get one CV input, additive onto the knob
value, hard-clamped to `0..1` — the guarantee survives any input.

## 4. Terrain generator

A terrain is a function of a 32-bit seed — fully deterministic; same seed →
same terrain on every host. Generation order:

1. **Roles.** Which deck carries, which textures. Engine choice per role with
   weights (carrier prefers Synth/Body/Wave; texture may also take
   Sampler/BBD). Never two "loud" engine combinations.
2. **Tonality.** Scale, root, TUNE/RANGE — both decks always share one scale.
3. **Base patch.** Every remaining parameter drawn from the taste ranges,
   honoring the named hard constraints (BODY never below FILT −0.5 — the FILT
   cliff; the resonance cap; no double high density on both decks).
4. **Macro mappings.** Per macro one variant from its target pool: which
   params, which `lo/hi` spans, which curve. Rule: every macro must be
   audible — a minimum span per mapping, so no knob is ever dead.

**Taste rules are data, not code.** `taste.h` is a table: parameter →
min/max/distribution, plus named constraints. The prototype phase's
listening loop consists of tightening or widening this table, not of
rebuilding generator code.

## 5. NEW behavior

**Short press — a transition, not a cut:**

- Continuous parameters ramp to their new targets over ~6 s; the flow layer
  rides the normalized setters and the engine declicks itself (the v2.18.2
  declick round carries this).
- Discrete switches (engine, scale, root) switch once, at the start of the
  blend, under a short duck of the affected deck. The decks switch staggered
  (texture first, then carrier) so both never jump at once.
- The six knobs keep their physical positions and act on the new terrain
  immediately. No value jumps, because macros map absolutely (§3).

**Long press — one step back.** Exactly one previous seed is remembered;
long press blends back to it. No deeper history.

**VCV context menu:** show current seed, enter a seed manually (share
terrains), lock the terrain (NEW ignored — protects live use).

## 6. VCV module "Flow" — panel

Own compact module in the Spotymod plugin, **12 HP (61 mm)**, drawn at true
hardware dimensions so the faceplate doubles as the 1:1 M6 panel draft.
Generated like the big panel: an own `gen_flow_panel.py` emits the SVG + a
generated header; neither is ever hand-edited.

- Logo on top.
- Six **16 mm** macro knobs in two rows of three (~20 mm pitch):
  row 1 MOTION · DENSITY · BRIGHT, row 2 DIRT · PACE · SPACE.
- Large **NEW** button below, with an LED that breathes during the blend.
- Jack block at the bottom, two rows of four at ~14 mm pitch:
  `CV MOT · CV DEN · CV BRT · CV DRT` over `CLK · RST · OUT L · OUT R`.
- PACE and SPACE deliberately have no CV: they are set-and-forget framing
  controls, and the jack budget stays at eight.

The contrast to the big panel is intentional: there the "big" knobs render at
8.4 mm because ~56 controls had to fit; here six knobs render at real 16 mm.

## 7. Testing

doctest units in `tests/`, render host for audio smoke. Project convention:
sanity checks, no byte-identity gates. Every test proves RED once before it
counts.

1. **Property tests over many seeds** (~10 000): every generated parameter
   inside the taste limits; all named constraints hold; every macro mapping
   meets the minimum span; determinism (same seed twice → identical terrain).
2. **Monotonicity:** riding a macro 0→1 moves every mapped parameter
   monotonically in its declared direction.
3. **Render smoke:** a handful of fixed seeds through `render.exe` — no NaN,
   RMS inside plausible bounds, a NEW transition without a level jump beyond
   a fixed dB threshold.

## 8. Out of scope

- The full-control hardware expander (mentioned as future option only; §2
  keeps it architecturally trivial).
- The M6 panel itself (though §6 is its 1:1 draft).
- Presets or storage beyond the single undo seed.
- CV over PACE/SPACE.
