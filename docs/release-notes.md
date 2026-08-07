<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## Glow stops sounding dissonant

Glow's generated terrains had a habit of landing somewhere sour. Two causes were
found by measurement, and both are fixed. The **FireFlow** module is unchanged in
this release.

**The scale draw was a coin toss over all thirteen scales.** Whole tone, hijaz,
phrygian and harmonic minor took 31 % of terrains between them. The draw is now
weighted, and staggered by how much friction a scale can produce when two
sustained voices land on it at once — minor and major pentatonic contain neither
a minor second nor a tritone and are now the common case, while the exotic four
drop to roughly a tenth of terrains. The weights are tempered by the terrain's
adventure level, exactly like every other weight table in the instrument, so an
adventurous terrain can still reach whole tone; it just rarely does. Measured
over the same 24 terrains as the original diagnosis, the number whose two decks
share no common scale fell from 4 to 2.

**A BBD texture deck could glide most of an octave against the other deck.** In
FLOW mode the BBD does not quantize, and its PITCH lane is not a note — it is the
delay clock, spread geometrically across a window up to five octaves wide. Glow
now bounds that travel to a one-semitone budget. Measured on two terrains that
had the problem, the deck's pitch travel went from 10.6 and 10.5 semitones to
0.34 and 0.39. The bend survives as a bend; it stops being a second key.

One thing this release deliberately does **not** change: the sampler deck still
never quantizes. Its TUNE transposes a recording as a whole, and snapping that to
the instrument's scale would be meaningless.

## Your saved terrain codes will sound different

Every terrain code now draws a different scale than it did in 2.20.0. The root is
untouched — bit for bit the same — but the scale on top of it is not. Codes are
not broken and will still load; they simply lead somewhere else. Nothing else
about the format changed.

## If you downloaded 3.0.0, replace it

**v3.0.0 does not load, on any platform.** Rack reads the first component of a
plugin's version as its ABI version, so `3.0.0` announces "Rack 3" — a Rack that
does not exist, and Rack skips the plugin silently at startup. Delete the old
`Fireflow` folder and `.vcvplugin` from your user plugin directory before
installing, so nothing stale is left behind.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.

FireFlow Glow remains at **0.1** — its panel says `ALPHA`, and it is a first
playable version, not a finished instrument.
