<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.21.6

**A new factory patch.** Adding a module to the rack — or hitting Initialize —
now boots a different instrument. Nothing in the engine changed; every number
that moved is a knob position, played and saved from a 2.21.5 module.

**FEED against WAVE.** Deck A boots the coupled feedback-FM drone, deck B the
wavetable engine. It is the first factory patch to start a FEED deck at all,
and the first with no SYNTH, BODY or SAMPLER deck at boot.

**Deck B starts stepped**, eight steps, while deck A free-runs — the first
factory patch that boots a deck in step mode on purpose. Both decks sit on
ladder rung 0 (*TwoMotif / Off*), where the old patch spread them to opposite
ends.

**It also starts moving.** TEMPO is off its floor for the first time (79.5 BPM
instead of 40), so everything clocked — both lane rates, the tape division,
deck B's step grid — runs faster out of the box, while PACE sits *below* ×1 to
slow the modulation clock underneath it. DRIFT is parked at zero: the weather
walk stays where you leave it until you ask for it. The scale is Minor
pentatonic, the first factory patch outside the modes group, and both decks
boot at the top of LVL/COMP.

Along the way two mirroring gaps closed in `bench/audition`, the harness the
desktop tests and the Seed audition share with the Rack host: a FEED deck's
`SPREAD` and the `DPTH` knob were never pushed there. Both were invisible while
no factory patch used them, and both are audible now.

Saved patches are unaffected — this changes what a *new* module boots with.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
