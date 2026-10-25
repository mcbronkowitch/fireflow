<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.23.2

**CPU work only. Nothing should sound different.** Turning TIDE used to cost
the hardware's control code one expensive block. Every lane rebuilt its
smoothing even where nothing had changed, and every lane whose step count moved
searched the shuffle grid twice. Both now do only the work that changes
something. Tests show the output is bit-identical to 2.23.1. In Rack you will
not notice it. On the hardware, TIDE's block on the stress patch now stays at
26 156 cycles, under the 28 000 at which the control code would have
to be split across two audio blocks.

This is a development alpha. Saved patches may break between releases.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
