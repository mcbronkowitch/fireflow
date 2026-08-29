<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.21.10

**Every MOD depth knob now has two halves.** Hold MOD and turn a wreathed knob:
to the **right of noon** it does what it always did — the depth of that lane's
continuous, gliding modulation. To the **left of noon** the same lane is
**sampled and held** at its own slot boundaries, so the target steps between
values instead of sliding between them. Noon is a standstill: the knob is off
there, with a small dead zone around it so "off" is reachable on a real pot the
way it already is on GRIT and PULL.

A left-hand setting is **not an inverted right-hand one.** The sign picks which
reading of the lane the target follows; the distance from noon sets how much.
At equal distance in both directions you get the same amount of modulation, one
gliding and one stepping.

**Where you will hear it.** The two halves diverge most in FLOW with SMOOTH up
— that is where the continuous reading is smoothest and the held one is most
obviously a staircase. In STEP at SMOOTH 0 they are *the same signal*, exactly,
because the follower is already a staircase and sampling it changes nothing. If
you go looking for the feature there you will conclude the knob is broken; it
is not.

**One honest caveat:** sampling shaves the peaks a little. The grid rarely
catches a lane's exact extremes, so a held reading swings slightly less than
its continuous twin — measured 0.905 against 0.941 peak-to-peak at SMOOTH 0.7.
That is ordinary sample-and-hold behaviour, not a defect, but it means the left
half is a touch gentler than the right at the same distance from noon.

**The MOD lamp now double-pulses while the latch holds.** Two short flashes,
a gap, then a longer dark tail, twice a second. The latch re-points every
wreathed knob on the plate at once, so forgetting it is engaged is the
expensive mistake, and a steady lamp sits in peripheral vision as furniture.
An even blink was tried alongside and rejected — at this rate it reads as a
loose contact rather than as a signal.

**A new factory patch.** A fresh module no longer boots the 2.21.6 sound. Deck
A still runs FEED against WAVE on deck B, deck B still boots stepped, and the
tempo, scale and drift settings are unchanged — but the voicing moved, MORPH
sits at centre, the modulation clock runs at ×1/16, and the two decks no longer
share one compressor amount.

The part worth knowing about: **the MOD layer now boots with something
dialled.** Five depths sit off noon out of the box — SUB on deck B, DETUNE on
both decks, and MORPH and REV_DIFF in the centre column. Latch MOD on a fresh
patch and you will see them away from centre. That is the patch, not a fault.

The depth knobs' range is now −1..+1 instead of 0..1. Patches from earlier
versions are not converted — this is a development alpha and saved patches may
break between releases.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
