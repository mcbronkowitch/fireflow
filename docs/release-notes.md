<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.

  Written 2026-08-14 for the flow/Glow removal. `plugin.json` still reads
  2.21.1 — the bump belongs to whoever tags this, and MAJOR stays 2.
-->

## FireFlow Glow is removed

This release takes a module away and leaves the other two alone. **FireFlow**
and **FireFlow HW Draft** are unchanged: same panels, same parameters, same
sound.

**FireFlow Glow is gone, and it is not coming back.** It was a six-macro box
built to be the small thing you turn up with, on top of a generative terrain
layer that drew whole patches from a seed. That layer — `engine/flow/`, some
seven thousand lines — went with it. Neither existed for its own sake, and the
reason they existed no longer does.

**If you have a saved patch containing a Glow**, Rack will report the module as
missing when you open it and leave a placeholder in its place. The rest of the
patch loads normally; FireFlow and FireFlow HW Draft instances in the same
patch are untouched. There is no migration, and none is planned — Glow's state
was a terrain seed, and the generator that turned a seed into a sound is not in
the plugin any more.

**Nothing in the engine changed.** The removal was subtraction plus one
rename: the parameter table that Glow drove the instrument through was never
Glow's — it is a reflection table over the engine's own setters — and it now
lives at `engine/param_table.h`, where the hardware panel will use it. The
audio path, the modulation system and both surviving panels are byte-for-byte
what they were.

The reasoning, the specs and the plans that produced Glow are kept in
`docs/attic/`, together with a note recording the values that were tuned by
ear rather than derived, so the next design round does not have to guess at
them a second time.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.

FireFlow HW Draft still says `DRAFT` on its panel: it is a design study for a
60 HP hardware module, not a finished instrument.
