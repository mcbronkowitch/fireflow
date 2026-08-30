<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.22.0

**Each deck can now be placed in the stereo field.** A new bipolar `PAN` knob
per deck, centre at noon, unity on both channels there — a centred PAN cannot
move a render at all. The law is a plain balance: turn right and the left
channel comes down, the right stays where it was. Nothing gets louder.

**The reverb stays in the middle.** PAN moves the dry signal only. The send
into the room is taken before the balance, so panning a deck hard left leaves
its reverb tail centred and the cloud intact, rather than dragging the whole
space over with it. This was the decision, not an oversight.

**PAN has a MOD ring, and the two decks mirror each other.** Latch MOD and the
PAN knobs take a depth like any other wreathed control. Deck B reads deck A's
lane **negated**, so the decks always move apart — one goes left as the other
goes right — instead of drifting to the same side together.

That mirror is not cosmetic. Both decks reading their own lane was measured at
a correlation of **+1.0000** whenever the two decks share a RATE: 98.7 % of the
time on the same side, 0.0 % opposite. The modulation lane does not depend on
the deck's seed, so two decks were producing one drift, twice, and the ring
panned the whole mix rather than opening it up.

**What the mirror costs, said plainly:** `MOD B` no longer reaches `PAN B`.
Deck B's pan modulation now runs off deck A's master, so `MOD A` down stills
both pans, and you switch deck B's off at its own depth ring instead. PAN has
had **no listening pass yet** — the balance law, the smoothing time, the lane
it rides and the mirror itself are all first-try values.

**Neither panel borrows Rack's parts any more.** New house knobs, jacks and
keys, drawn rather than pulled from the stock library: the knob caps now carry
their deck's accent colour on collar and pointer, so the hand can tell deck A
from deck B without reading a caption. Dragging, snapping, tooltips and the
right-click menu are unchanged — they are still the framework's.

**The lamps light in the colours the plates already printed.** Every LED bed on
both plates has carried a zone tint for a long while, and a saturated yellow
was painting over all of them. Deck A is teal now, deck B orange, the centre
blue-grey. REC stays red, because on a record light that is the point.

**The hardware plate (`FireflowHW`) is one flat surface.** The tinted zones,
the printed ember silhouette, the fade overlays and the dashed drawing frames
are all struck; zone identity moved into the group fields instead, which is
also why the "DECK A" / "DECK B" legends are gone — the tint says it. `SEND`
moved out of `ROOM` and into `LEVEL`, where it belongs, on a frame that now
reaches past the deck edge to hold it. The jack row lost its group legends: the
caption under each socket already names it.

**One deliberate regression on that plate:** the accent rings marking which
knobs accept modulation are no longer printed — they are drawn, and only while
MOD is latched. Silkscreen cannot switch, and a real aluminium panel would have
to carry them permanently or not at all. This release rehearses "not at all",
so on the hardware draft, which knobs carry a depth is learned rather than read.

This is a development alpha. Saved patches may break between releases.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
