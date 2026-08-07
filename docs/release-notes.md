<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## Glow gets three explicit controls: GENRE, SCALE and ROOT

Glow generates a whole instrument from one seed, and until now every part of that
was the dice's business. Three things are now yours to decide. The **FireFlow**
module is unchanged in this release.

**GENRE**, left of NEW, decides which kind of terrain NEW is allowed to draw —
Any, Drone, Pulse, Arp or Fragment. It was effectively impossible to hear two
drones in a row before: the archetype changes so strongly that any new terrain
counted as far enough away, and across 3 000 presses not one landed on the same
archetype twice. Set GENRE and the draw stays inside that genre, picking the
furthest of eight candidates so NEW still means somewhere else. Turning GENRE
changes nothing you can hear until the next press — it is a rule about the draw,
not a sound control.

**SCALE**, right of NEW, fixes the tonality. Auto is what Glow has always done:
whatever the terrain drew. Any other position takes effect immediately, and Auto
gives the terrain's own scale straight back — the terrain keeps tracking
underneath while you hold a scale, so releasing lands you on the current
terrain's scale, not the one it had when you started. The travel runs from calm
to sharp: the two pentatonics first, then the seven-note modes, then hirajoshi,
pygmy and kumoi, then the four that bite.

**ROOT** lives in the right-click menu, same idea, twelve semitones plus Auto. It
is saved with the patch.

## A pasted terrain code no longer carries the whole sound

The code still *is* the terrain, and copying it out and pasting it in still takes
you to that place. But an explicit SCALE or ROOT rides on top of it and travels
in the patch rather than in the code — so a code you paste reproduces the
sharer's terrain, not necessarily their tonality. Set both to Auto before sharing
if you want what you hear to be what they get.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.

FireFlow Glow remains at **0.1** — its panel says `ALPHA`, and it is a first
playable version, not a finished instrument.
