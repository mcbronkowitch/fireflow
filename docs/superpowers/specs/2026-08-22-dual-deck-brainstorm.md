# Dual-deck feature brainstorm — 2026-08-22

Status: **brainstorm, not a spec.** Costs deliberately unassessed. No runtime
claims below have been probed; everything is design intent until measured
(probe rule applies before any of this enters a spec).

Direction chosen by Bastian: mainly **generative interplay between the decks**
(the decks react to each other), with a pinch of **sonic contrast/layering**
(two layered sound worlds that stay legible).

## Grounding — what crosses the decks today

Six mechanisms exist (see `2026-08-21-m6-one-brain-two-shells-design.md`
for why cross-deck behaviour is the product identity):

1. **MORPH** — equal-power A/B crossfade in Center, also fades reverb sends.
2. **CHOKE** — signed event priority; extended 2026-08-22 by the sidechain
   duck (`docs/superpowers/plans/2026-08-22-choke-sidechain-duck-plan.md`).
3. **Control-rate mono excitation bus** — each deck's dry tap feeds the
   sibling's BODY resonator (off by default).
4. **Audio-rate stereo deck bus** — 1-sample-latency neighbour audio,
   consumed by SAMPLER and BBD when routed.
5. **FLUX LINK** — each deck's FLUX reads the *sibling's* PITCH-lane rhythm
   (bipolar DRAG vs. thinning).
6. **COUPLE + DRIFT** — Kuramoto phase pull between the decks; one shared OU
   weather walk tapped with opposite signs.

The gap: **the modulation lanes never cross decks.** Only audio and the
rhythm view travel between them. PULL (chord gravity,
`2026-07-19-pull-chord-gravity-design.md`) is designed but unbuilt (M5l).

## The ideas

### Modulation layer (the unopened seam)

**1. Lane crossover** — a deck's lane output (e.g. LEVEL or MOTION) becomes a
selectable modulation source for a target on the *other* deck. A "breathes",
B's filter answers. First direct mod-to-mod bridge between the decks.
→ **on the roadmap since 2026-08-22.**

**2. Mirror lanes / counter-motion** — B follows a chosen lane of A inverted
or phase-shifted: where A brightens, B darkens. DRIFT anti-correlates only
indirectly via shared weather; this is direct, per-lane, deterministic —
call-and-response instead of coincidence.
→ **on the roadmap since 2026-08-22.**

*(Design note: 1 and 2 sit in the same mod-glue and may turn out to be one
feature with two faces — spec them together.)*

### Event / sequencer layer

**3. Build PULL** — chord gravity: A's chord tones capture B's melody notes.
Spec exists (M5l) and is the ripest candidate; harmonic interplay to
complement the rhythmic (CHOKE) and dynamic (duck) axes.

**4. Inverse CHOKE / gap-filler** — the complement of ducking: instead of
"A silences B", "A's silence invites B". B prefers to trigger in A's rests.
Together with CHOKE this makes a bipolar conversation axis:
displace ↔ evade.

**5. Canon / event echo** — A's trigger events replay on B delayed and
transformed (transposed, thinned). Generative counterpoint from one deck.

**6. Energy budget** — total density across both decks is a shared resource:
when A gets busier, B thins automatically (and vice versa). A seesaw instead
of two independent DENSITY knobs; structurally prevents the layers from
washing each other out (the layering pinch).

### Audio layer (extending the sidechain family)

**7. Sidechain-to-anything** — generalize the envelope follower the CHOKE
duck introduced: A's envelope modulates not just B's level but selectably
B's FILT, REV send, or FLUX. Builds directly on the just-landed duck
infrastructure.

**8. Spectral evasion** — B is dynamically thinned where A currently has
energy (a tracking notch instead of a broadband duck). The ambient promise:
two layers that never mask each other. Likely expensive — evaluate later,
possibly VCV-first.

### Space layer (the layering pinch)

**9. Depth staggering** — one knob pushes the decks apart in the shared room:
one near and dry, the other far and wet (send, damping, possibly width moving
in opposition). Exploits the one-room/per-deck-send architecture.

**10. MORPH weather** — a slow Center-driven automation of the crossfader
(TIDE-like): the decks relieve each other over minutes without anyone
touching the knob. Generative behaviour at the top of the mix.

### Mix-point essentials (gaps noticed during the research pass)

**11. Per-deck tilt EQ / isolator** — the deck FX chain (Grit, Flux, Comp)
has no EQ; FILT and COLOR are voice controls, not mix tools. A one-knob
tilt (dark ↔ bright) per deck at the mix point is the classic two-source
tool for keeping layers legible, and doubles as a sidechain-family target
(A ducks B's lows instead of B's level). Probably the cheapest effective
layering feature available.

**12. Per-deck pan / width** — LVL per deck exists, but no per-deck stereo
placement was found in the research pass (**verify before speccing** — this
is an absence-of-evidence claim, not a probed fact). Left/right or width
staggering is the second simple separation axis next to depth staggering
(idea 9).

## Decisions so far

- All ten original ideas kept; none rejected (Bastian, 2026-08-22).
- Ideas 11 (tilt EQ) and 12 (pan/width) added from the gap pass on Bastian's
  pick; other noticed gaps (deck mute/solo, deck copy/swap, cue output,
  verify whether resampling A→B via the deck bus already works) stay out of
  the idea list for now but are recorded here so they are not lost.
- **Ideas 1 and 2 entered `docs/roadmap.md` on 2026-08-22** (planned, no
  spec, not yet ordered into the milestone sequence).
- Favourites flagged for deeper exploration: 3 (PULL, ripest), 4 (inverse
  CHOKE, completes the conversation axis), 1 (lane crossover, opens the
  unconnected layer).
- Costs/CPU explicitly deferred; no hardware-vs-VCV triage yet.
- Sequencing guidance agreed 2026-08-22: specs may be developed in
  parallel; implementations mostly sequential (shared choke points:
  `param_table.h`, `instrument.h` setters, generated panels, init-rebake
  chain), at most two disjoint features at once, and the by-ear listening
  pass is the true serial resource.
