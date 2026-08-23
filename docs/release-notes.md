<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.21.9

A drawing release for the 60 HP hardware panel (`FireflowHW`). **No engine
change** — nothing sounds different, and the main `Fireflow` module is
untouched.

**FILT is a large cap again, and VOICE now stands in the same figure as
TIMING.** The filter had been shrunk to a small knob in August because a large
one could not sit on the 13 mm pitch it shared with TIMB and DPTH — a large cap
needs 14.5 mm to a small neighbour. Rather than shrink the knob to fit the row,
the row changed: VOICE's lower half is now small–LARGE–small with the filter
centred, which is exactly how TIDE / MRPH / PACE has stood in TIMING since the
graphics round. Both rows are now written from one pitch and one centre line, so
re-tuning the figure re-tunes both instead of letting them drift apart.

**What that fixes:** VOICE was the only two-row group on the plate running on
three heights, with its two small knobs crowded together on the left, a 15.6 mm
hole in the middle and the large filter alone on a third line. It read restless
next to MOTION beside it. The new row is symmetric — 5.6 mm between bodies on
both sides, 10.7 mm of air to the frame on both ends.

**The FIREFLOW wordmark and the "60 HP" legend have come off the plate** while
the panel's branding is redrawn. The header strip is deliberately empty for now
rather than carrying a placeholder.

The hardware panel remains a design study. No hardware is ordered, and the
module is still labelled DRAFT.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
