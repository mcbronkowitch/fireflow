<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.21.8

**PULL — the two decks can share a chord now.** A new bipolar knob, centred in
the panel under CHOKE, draws one deck's melody onto the other deck's sounding
harmony. Turn it left and deck A leads: every note deck B's melodic lane fires
has a chance to land on a pitch class deck A is currently sounding, in whatever
octave deck B's own register puts it, instead of on its own scale. Turn it
right and the roles swap — the same left/right convention CHOKE already uses.
Centred, it does nothing; a small dead zone around noon keeps it reliably off
there on a real knob rather than needing a pixel-perfect click-stop.

**The pull is a probability, not a snap.** How far you turn PULL sets how often
a follower note is pulled onto the leader's chord rather than how hard — near
the dead zone only a few notes bind, at full deflection every one does. A note
already sounding keeps listening: if the leader's chord changes underneath a
bound note, that note glides onto the new chord over the same short slide the
instrument already uses whenever a scale or root changes, instead of waiting
for its own next strike.

**Not every deck has a chord to offer.** SAMPLER and BBD decks use their pitch
lane for something other than a note — a read position, a clock bend — so
leading from one of those simply does nothing, in either mode; the follower
side behaves normally as soon as the other deck leads instead.

**On the 60 HP hardware draft, PULL joins the GLOBAL row as its fourth knob,**
right beside CHOKE — the centre's only other bipolar control, and the one whose
sign convention PULL borrows. Making room re-pitched the row: the three knobs
that were there each moved 6.5 mm left so the four re-centre, at the same
spacing they always had.

Both the dead zone and the pull-probability curve are first-try values and have
not been through a listening pass yet.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
