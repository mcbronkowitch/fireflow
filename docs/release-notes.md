<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.23.3

**CPU work only. Nothing should sound different.** TIDE's control block on the
hardware got cheaper again. When TIDE changes how many steps a lane has, the
lane used to search the shuffle grid for the step it was on, although it
usually knew it already. It now checks that step first and only searches if it
is wrong. It also no longer rebuilds its smoothing: in STEP the smoothing time
does not depend on the step count at all. The step positions are bit-identical
to 2.23.2. The smoothing coefficients can differ in the last float digit or
two (at most 2.3e-7 relative), about as much as they already changed from one
step count to the next. In Rack you will not notice it. On the hardware, TIDE
alone on the init patch now costs 17 235 cycles instead of 20 809, and on the
stress patch about 19 400 instead of 26 156, well under the 28 000 at which the
control code would have to be split across two audio blocks.

This is a development alpha. Saved patches may break between releases.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
