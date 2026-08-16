<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.21.4

In STEP a deck used to play every composed note at the same level and the same
length. Pitch and placement differed; nothing else did, which is why a dense
pattern read as a machine running rather than as a phrase being played. This
release gives those notes a contour — **without adding a single control.**

**The accent comes from the groove that is already there.** Every note deck
already ranks the slots of its groove cell, and DENSE decides how far down that
ranking it fires. The first slot to sound is the anchor; the ones DENSE reveals
later are, by construction, further down the rank. That rank is now the accent:
the anchor strikes at full strength, and each note DENSE adds strikes a little
softer and a little shorter than the one before it. Turn DENSE down to a single
note and it is loud again. The animation appears as the pattern fills in, which
is where the ear wants it.

**DEC is the ceiling.** The accent shortens notes only as far as the DEC knob
has opened the envelope: at DEC 0 it cannot touch ring time at all, at DEC 1 the
weakest note of a full pattern decays in about a third of the set time. The knob
still decides how long a note is; the accent only ever subtracts from it. Level
works the same way, down to 30 % at the far end, and it composes with the
existing chord compensation instead of replacing it — chords keep their level.

**FLOW is untouched, on purpose.** A FLOW deck is a drone, and per-note dynamics
there would be animation where the design wants stillness. The accent is
computed only in STEP and reads as zero everywhere else. SYNTH, WAVE and BODY
spend it; SAMPLER and BBD ignore it, since neither has a per-note envelope for
it to scale.

**Fixed: a manual strike is always full strength.** Tapping PLAY or TRIG used to
inherit whatever accent the sequencer had last pushed into the engine, so a
press could land at roughly 40 % velocity for no visible reason. A hand-played
note is an anchor by definition and now strikes accordingly.

Both accent depths are first-pass by-ear values and may still move.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
