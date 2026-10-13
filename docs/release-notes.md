<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.23.0

**The hardware plate (`FireflowHW`) is the one going to the board.** Its
layout is frozen: the Rev A boards are ordered, and the acrylic plate is cut
from the same file this panel is drawn from. Every knob now sits on one 9 mm
raster: at least 9 mm of air between any two caps, a three-column centre on its
own wider pitch, and the jack row at the bottom of the plate.

**Three new pots that do nothing yet.** `ROOT` on each deck (a per-deck scale
root) and `WOBL` in `ROOM` (reverb tail wobble) are reserved: they have a hole,
a caption and a pot on the board, but no function and no Rack widget. They show
as dark discs on the plate.

**Fifteen lamps, and they show timing, not modulation.** The TIMB, FILT and
COLR excursion lamps and the limiter lamp are gone. New on the plate: a `TIME`
lamp for the FLUX time period, `CLK` and `RST` beside their jacks, and the SHFT
and MOD lamps between their key and jack. Every knob lamp sits beside its
caption, toward its own group. **In Rack, `TIME`, `CLK`, `RST` and `SHFT` stay
dark for now**: what they will show is designed but not wired yet. The MOD lamp
now also takes over the old limiter lamp's job.

**The plate is drawn evenly.** Every gap between two group fields is now
3.0 mm. It ranged from 3.0 to 5.9 mm before, widest above DIFF and WOBL.
Every field fills its row's full height, so CAPTURE is as tall as its
neighbours, and every group name sits centred on its field's top edge. Deck B
now reads as an exact mirror image of deck A.

**Fixed: loading a patch onto a BBD deck no longer silences FLUX.** Restoring a
patch over a running deck whose ENG is BBD made the plugin see a fresh switch
to BBD. It then set FLUX to 0 and turned excite-other-deck on, over the patch
you had just loaded. A restore now fires no switch; turning ENG yourself still
does.

**Under the hood:** the control law that turns knob positions into engine
settings moved out of the Rack module into a shared, Rack-free layer. The
firmware runs the same code. It now sends a setting only when its value
changed. Nothing should sound different.

This is a development alpha. Saved patches may break between releases.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
