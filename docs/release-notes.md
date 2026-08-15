<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.21.2

The 42 HP **FireFlow** module is unchanged in layout and sound. This release
is the 60 HP **FireFlow HW Draft**: a visual pass on the hardware-envelope
rehearsal, still a design study, still labelled as a draft.

**The plate.** Deck A is solder-green, deck B copper, the shared centre paper.
Those three fields no longer meet on a hard vertical: the inner edge follows
the deck silhouette, then a light smooth so it reads as one curve rather than
a chain of circles. Knob discs under the Rack widgets are the same wash colour,
so they sit as holes through the wells rather than black caps.

**Knobs.** ENG and TIMB, TIME and LINK, TIDE and PACE sit slightly off the old
row grid so the organic wells close; DECY drops a little to give MORPH air.
ATK and SUB stay on the envelope row, captions underneath.

**MOD 1–4.** The eight green COLOR / FILT / TIMB / LVL placeholders are real
Rack jacks, mirrored, on the jack row. They are not wired into the engine yet.

Saved patches keep their original four inputs (IN L/R, CLOCK, RESET); the MOD
jacks append after those ids. FireFlow HW Draft remains a 60 HP workbench, not
a finished instrument.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
