<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.21.7

**Every knob becomes its own modulation depth.** The MOD pad on the hardware
panel is a latching button now. Press it and the lamp stays lit; while it does,
each modulatable knob stops showing its sound value and shows how deeply the
modulation engine moves it instead. Press again and the panel comes back
exactly as you left it — both sets of values live in the patch.

**The panel tells you which knobs those are.** Every modulatable pot wears its
zone's accent colour on the ring around it — teal on deck A, orange on deck B,
blue-grey in the centre. A plain dark ring means the knob keeps its normal job
even while the layer is latched: MOD itself, GRIT, TIME, DRFT, SYNC, CHOK and
the whole clock and structure row. The absence of colour is the information.

**48 depths, two routes into the sound.** Six faces per deck already owned a
depth slot in the engine — TIMB, DPTH, FILT and the three FX sends — and their
knobs now write those slots directly. Everything else is computed in the host
and folded into the knob position before it reaches the engine, so no new
summing points appeared anywhere in the signal path.

**Pitch stays anchored.** Nothing in the layer targets the pitch lane's depth;
RANG still owns how far the phrase moves, and RANG at zero still silences it
exactly. Turn MOD down and the texture stops breathing while the phrase keeps
playing — that relationship is unchanged and deliberate.

**Nothing sounds different until you raise a depth.** The three engine-backed
depths boot at the values the engine already used, every host-computed depth
boots at zero, and the latch boots off.

**One exception, and it is audible: deck B's DPTH knob works now.** It never
reached the engine — the read resolved to the wrong parameter — so deck B's
motion base has been stuck since the knob was wired up. Fixing it moves that
base from 0.0 to 0.5 (0.25 on a sampler deck). A saved patch with a deck B will
sound different where it was silently ignoring that knob before. Along the same
line, DENS on a sampler deck now modulates grain overlap as well as the groove
gate, so the two halves of that one knob move together.

The full-size Fireflow module is untouched — this layer belongs to the hardware
panel. SHIFT remains reserved and inert.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
