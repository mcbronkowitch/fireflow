<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## New: FireFlow Glow 0.1

The plugin now ships **two modules over the same engine core**.

**FireFlow Glow** is the flow-machine view of the instrument: 12 HP, six macro
knobs and one NEW button over a seeded generative terrain. Where the FireFlow
module gives every engine setter its own control, Glow drives `engine/flow/` —
MOTION, DENSITY, BRIGHT, DIRT, WANDER and SPACE move the whole terrain at once,
and NEW rolls a fresh one. Terrains are codes: they survive copy, paste,
save/reload and Initialize, and can be typed in or passed around. Five CV inputs
and a clock input put the macros and the tempo under patch control.

Glow is at **0.1** — its panel says `ALPHA`, and it is a first playable version,
not a finished instrument. The house seed it wakes on is still a measured
placeholder rather than a by-ear choice.

The **FireFlow** module is unchanged in this release.

## If you downloaded 3.0.0, replace it

**v3.0.0 does not load, on any platform.** Rack reads the first component of a
plugin's version as its ABI version, so `3.0.0` announces "Rack 3" — a Rack that
does not exist. Rack skips the plugin silently at startup; the only trace is one
line in `log.txt`:

```
Could not load plugin .../Fireflow: Plugin version 3.0.0 does not match Rack ABI version 2
```

Nothing was wrong with the build itself. 3.0.0 had been chosen to mark the plugin
becoming two instruments rather than one, not to claim Rack 3 — but that first
number is not ours to spend. This release is the same code under a version Rack
accepts. Delete the old `Fireflow` folder and `.vcvplugin` from your user plugin
directory before installing, so nothing stale is left behind.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
