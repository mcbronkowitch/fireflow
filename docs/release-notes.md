<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.21.5

The hardware panel draft (`FireflowHW`) said nothing about what the instrument
was doing. Ten LEDs were drawn, six of them could not light at all, and two of
the four that could sat in a row where they meant nothing. This release makes
the plate talk: **19 lamps, and every one of them answers the question that is
asked where it sits.** Nothing in the audio path changed, and the large
`Fireflow` module is untouched.

**The lights show modulation excursion, not knob position.** That distinction is
the whole design. A knob parked at 0,9 sitting perfectly still would outshine a
knob at 0,1 swinging full scale if the lamps read the modulated target — so they
read the *swing* instead. Four lamps per deck, at the knob nearest each lane's
usual destination (`SOURCE`, `FILT`, `COLOR`, `COMP`), track their lane's
excursion with a peak-held envelope: dark when nothing moves, a dim breath when
the modulation is shallow, a bright one when it is deep. The envelope sets the
ceiling and the instantaneous excursion breathes inside it, so depth and motion
are both readable from one lamp.

**TEMPO is the one metronome on the plate.** A short tick on the transport
downbeat, scaled by TEMPO and PACE rather than by wall clock, so it stays a beat
marker instead of eating the bar at high BPM. **SONG** flashes for 150 ms when a
deck switches its A/B snapshot and goes dark again — the earlier double-pulse
became unreadable once the FLOW melody engine started advancing SONG on its own.
**GATE** reports that a note is sounding, straight through with no smoothing.
**CEIL**, outboard of the OUT R jack, shows the master limiter bending — its
audible onset, not its gain reduction. **REC** keeps the three states it always
had.

**Two dead lamps are gone.** `CAP_A`/`CAP_B` indicated the capture sequencer,
which was deleted a month ago; they had been drawn 4 mm from the unrelated REC
button ever since. Three lamps are deliberately dark for now: `SYNC`, and the two
pad lamps at MOD and SHIFT, which need a latch that does not exist yet. They are
written every tick rather than skipped, and a test asserts it.

Under the hood the display law lives in `host/vcv/src/led_law.hpp`, free of Rack
and unit-tested on its own, and the engine gained two const observers —
`lane_excursion()` and the limiter's bend — that read state without touching it.
Brightness floor, gamma and release are by-ear candidates awaiting a look at real
hardware; on screen they are already honest.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
