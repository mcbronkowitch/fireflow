<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.23.4

**Melodies come back.** On a deck that plays notes (SYNTH, WAVE, BODY), the
PITCH RANGE knob used to do two things at once: it set how wide the melody
could move, and it pushed the melody's centre upward, by a whole octave and a
half at the knob's centre. That put the melody against the top of the pitch
span, where half of every phrase was clipped onto one note. Around noon a deck
mostly repeated the root, and the centre of the knob was its worst position.

RANGE is now only a width around a centre that stays put. The width at every
knob position is the same as before. A note that would leave the span is folded
back into it instead of being clipped, so it still moves. On the noon patch,
phrases stuck on one note drop from 53 % to 5 %. SAMPLER and BBD keep their
old RANGE behaviour, since their PITCH is playback speed and delay time, not a
note.

A second route to the same symptom is fixed too: a deck that was switched to
BBD and back kept BBD's pitch setting and came back at the top of the span. It
now returns to the centre.

This is a development alpha. Saved patches may break between releases.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
