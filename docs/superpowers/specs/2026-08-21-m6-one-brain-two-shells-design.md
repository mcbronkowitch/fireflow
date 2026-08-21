# M6 direction: one brain, one module, two shells

**Date:** 2026-08-21
**Status:** decision note — a guardrail for the upcoming M6 bring-up spec, not a
build spec. Nothing here is implemented by this document; the bring-up spec
inherits these constraints.

## What was decided

The question on the table was whether FireFlow should be split into two or
three separate Daisy-based hardware modules (one per deck plus a master/mix
section, each on its own Patch Submodule), or stay one instrument on one chip.
The driver turned out to be product feel — "60 HP monster module vs. several
handy modules" — not CPU or I/O. Three decisions close it:

### 1. Single-brain architecture is non-negotiable

Splitting by deck is rejected. The cross-deck behaviour is the product
identity, and it exists only because both decks share one chip:

- CHOKE and the mutual delay ducking (control-rate coupling),
- the BBD part engine's **audio-rate** neighbour bus (the cross-deck audio
  feed) — impossible across two modules without a patch cable, at which point
  the BBD deck is an ordinary delay module,
- sample-accurate shared transport, SYNC, COUPLE, FORM/SONG as a global
  arrangement, the shared reverb with per-deck SEND,
- **PULL (M5l), the next planned expansion, is more deck coupling** — the
  project's direction of travel is toward tighter coupling, not away from it.

A single FireFlow deck as a standalone module would also enter the densest
field in Eurorack (Beads, Arbhar, Morphagene, Nebulae) stripped of exactly the
thing that distinguishes it. The integrated two-deck system with mutual
choke/duck/gravity has no hardware equivalent we know of; the split would
delete that uniqueness, not multiply it.

**If a split is ever forced** (CPU wall with no cheaper remedy), it goes
**along the layer seam, never through the decks**: a second Submodule as a
master-FX coprocessor (reverb, limiter, PUSH) fed by a one-way audio stream
keeps every coupling intact. Deck↔deck stays on one chip in every scenario.

CPU pressure alone does not justify distribution: the reachable-worst-case
question (Phase 0 Task 6 Schritt 8) is still open, and the measured voice-cut
option (~7.9 points, a listening decision) has not been spent — see
`docs/roadmap.md` for the current numbers.

### 2. M6 stays the 60 HP Eurorack module

No architecture change, no panel rework. Everything shipped for the panel —
the regrouping/redistribution rounds, plate round 2a, `gen_hw_panel.py` and
its guards, `docs/hardware/io-budget.md`, the LED feedback round — carries
over verbatim.

### 3. Desktop is a shell decision, not a device decision

The desktop idea is right about the feel and wrong as a rebuild. The pattern
is Moog Mother-32 / Bastl Softpop II: a Eurorack module that lives in a
powered desktop shell and can move into a rack at any time.

- **Stage 1 — buy, don't build:** a small off-the-shelf case. An Intellijel
  Palette is 62 HP, so the 60 HP module fits almost exactly; its 1U row
  covers the desktop wants (line-out/headphones, MIDI) with zero enclosure
  design.
- **Stage 2 — optional, later, its own project:** a dedicated powered desktop
  shell (Mother-32 style) with a built-in line/phones board. Deferred until
  the instrument earns product character; forces no decision today.

Context that weighed in: the owner tests FireFlow exclusively as a single
standalone module in VCV Rack (the instrument is modulation-first and autark
by design), **and** owns an actively used hardware Eurorack system, so the
module's CV I/O (12 in / 6 out per the generator) has daily value. The
two-shell answer serves both facts; a desktop-only box would serve one.

## Consequences for the bring-up spec

1. **The jack set must survive both homes.** Clock I/O and audio-in must be
   sensibly usable with no rack neighbours (the MIDI-clock question is to be
   answered there, not here).
2. **Headphone and line-out stay off the panel** deliberately — they belong
   to the 1U row (stage 1) or the shell (stage 2), not squeezed into 60 HP.
3. No other constraint is added; the bring-up spec's open items (ADC mux
   scan, preset persistence from scratch, ITCM loader) are unchanged.
