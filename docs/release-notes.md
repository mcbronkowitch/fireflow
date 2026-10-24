<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.23.1

**ROOT and WOBL work.** The three reserved pots from 2.23.0 are now parameters.
`ROOT` on each deck sets that deck's key: 12 zones from C at the left stop to B
at the right, and the tooltip shows the note name. Turning it moves the scale
under the running melody without re-latching the song; a song rebuilt
afterwards starts from the new root. `WOBL` in `ROOM` is the reverb tail
wobble. It boots at 0.15, the old fixed value, so the factory patch sounds the
same until you turn it. On `Fireflow` the three knobs sit in the centre column.

**CLOCK and RESET follow one law, shared with the firmware.** CLOCK takes one
pulse per quarter note. The tempo is averaged over the last eight periods and
changes only on a pulse. Pulses outside 20..400 BPM are ignored. When the
pulses stop, FireFlow keeps the last tempo until you move the TEMPO knob.
RESET now restarts the music on STEP decks. While a clock is running, it waits
for the next CLOCK pulse.

**The lamps from 2.23.0 light up.** `TIME` flashes once per FLUX period, `CLK`
and `RST` flash on their jack's pulses, and `SHFT` shows the input level plus
one long flash when an engine or scale change takes effect. The lamp law is now
the same code the firmware runs. Its timings are first guesses that no one has
watched yet.

**GRIT's off zone around noon is wider: 0.06 instead of 0.03.** On the first
real pot, a GRIT knob set to noon by eye read +0.044, so it never switched off.
In Rack this only means GRIT starts a touch further from noon.

**Under the hood:** CPU work for the hardware's audio callback. The STEP grid
search now runs only when the phase leaves its step. The FX setters run only
when their inputs move. A sleeping reverb clears its room in 32 small steps
instead of all at once. Tests confirm the first two are bit-identical, so
nothing should sound different.

This is a development alpha. Saved patches may break between releases.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
