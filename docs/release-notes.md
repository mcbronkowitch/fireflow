<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow Glow is now a Synthux Touch 2 replica

This release touches one module. **FireFlow** and **FireFlow HW Draft** are
unchanged.

FireFlow Glow's control surface is replaced, not extended: a true-size,
16 HP panel replicating the Synthux Simple Touch 2 — twelve touch pads, six
trim knobs, two assignable faders, two assignable centre-off switches, one
stereo out. There are no CV inputs and no clock input any more — the board
has none, so neither does the module — and the old NEW button, its five CV
jacks and its two flanking GENRE/SCALE knobs are gone with them.

**Twelve pads, twelve curated places.** Each pad holds one terrain code, an
optional name and an optional note. Tap a pad and it wakes immediately; hold
it for about 0.4 s and it rerolls all six macro domains at once while
keeping the ground — tonality, roles, pace — intact; tap the same pad again
and it returns to the curated state. Under LOCK, pads still change place —
only the reroll is refused, because LOCK guards the generator, not the
recall.

**Faders and switches are assignable**, from the right-click menu, and every
save remembers the assignment. A fader can be Off, Tempo (overriding the
terrain's own tempo across 50–140 BPM, every control tick) or Master
(linear output gain, unity by default). A switch can be Off, Lock or Scale.
Lock is now a pure function of switch position — there is no separate
stored lock any more, so a physical switch and a menu toggle can no longer
disagree about it.

**Everything the board cannot do moved to a Workshop menu**: drawing a new
terrain, rerolling one macro, undo, the Genre/Root/Scale constraints, and
naming, noting, pinning and exporting the twelve places as `pool.tsv`. The
board is the stage; Rack is the workshop, and none of this ships to the
Touch.

The full rationale, the measured geometry and the pad state machine are in
`docs/superpowers/specs/2026-08-11-glow-touch-2-panel-design.md`; the
control surface itself is documented in `host/vcv/README.md`'s "FireFlow
Glow" section.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.

FireFlow Glow's panel still says `ALPHA`: this is a rehearsal rig for a board
that has not arrived yet, not a finished instrument, and its twelve default
places are drawn, not curated. FireFlow HW Draft is a design study and says
`DRAFT` for the same reason.
