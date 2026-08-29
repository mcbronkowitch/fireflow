<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.22.0

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

**Init is unchanged.** The six engine-backed depths still boot at the values
they always had, so a fresh patch sounds exactly like the last release. Their
stored knob positions moved (the dead zone rescales the axis), but what reaches
the engine did not.

The depth knobs' range is now −1..+1 instead of 0..1. Patches from earlier
versions are not converted — this is a development alpha and saved patches may
break between releases.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
