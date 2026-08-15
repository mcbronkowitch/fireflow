<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.21.3

The 42 HP **FireFlow** module is unchanged in layout and sound. This release
redraws the 60 HP **FireFlow HW Draft**, which is still a design study and
still labelled as a draft.

**A new plate.** The light panel with its organic wells is gone. In its place:
a dark anodised plate in three tinted zones — cool on deck A, warm on deck B,
neutral through the centre — with an airflow and ember silhouette printed
underneath, and every function group drawn as a framed field with a numbered
legend. Knob positions did not move for it.

**Frames that sit straight.** Each frame hugs what it prints with the same
margin above and below, and the rows keep 3 mm between them. Row heights are
computed rather than typed in, so a caption that moves takes its frame with it.

**One caption distance.** Every printed word now keeps the same 3.6 mm to its
own component. Previously each size class had its own offset, and the four had
grown apart — 4.5 mm on the big pots against 2.5 mm on the pads — which showed
as labels hanging further from the large knobs than from the small ones.

**Three lines through the middle.** MOTION, VOICE and TIMING ran on five
different knob heights. They now run on three: small caps, the mid row, and
every large cap together, with FILT joining MOD, DENS and MORPH.

FireFlow HW Draft remains a 60 HP workbench, not a finished instrument.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
