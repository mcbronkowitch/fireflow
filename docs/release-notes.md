<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow HW Draft is regrouped

This release touches one module. **FireFlow** and **FireFlow Glow** are unchanged,
and no patch you have saved will sound different.

FireFlow HW Draft is not an instrument — it is the 60 HP aluminium panel being
designed in public, drawn at true size so the layout can be argued with before
anything is milled. Until now it was the screen layout moved onto a hardware grid
with no opinion about it. It has one now.

**Knob sizes say something.** A control's size is set by its rank inside its
group, not by what kind of widget the screen happens to draw. Eighteen large
knobs — DENS, MOD, COLOR, FILT, TIMB, MIX, SEND and LVL on each deck, plus MORPH
and reverb DECAY in the centre — and everything else small. Large means you reach
for it while the music runs.

**Groups follow the engine.** RATE, SHAPE, DENS, SMTH, RANGE, VARY and MOD are a
single object in the code, and the old screen sectors cut that modulator into
three pieces; the plate keeps it whole. COLOR turned out to be chord size rather
than tone colour, so it sits with TUNE now.

**Eight CV inputs, printed where they belong.** Each one sits in the jack row
directly under the large knob it drives and carries that knob's name, so the
assignment reads without a legend. They are hardware, not Rack ports — on screen
they are drawn, not patchable, and the same goes for the MOD and SHIFT buttons.

**Captions place themselves.** Below the control, else above, else to the side,
whichever position clears every neighbour first — mirrored correctly on the right
deck. The first look at the finished plate in Rack found three of them pushed up
into the status strip on both decks, which turned out to be 1.5 mm of missing
room rather than a bad rule. One row moved down two millimetres and they came
home. The panel is no taller for it.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.

FireFlow Glow remains at **0.1** — its panel says `ALPHA`, and it is a first
playable version, not a finished instrument. FireFlow HW Draft is a design study
and says `DRAFT` for the same reason.
