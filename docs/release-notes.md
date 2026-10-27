<!--
  Body of the CURRENT release. `.github/workflows/build-plugin.yml` feeds this
  file to the GitHub Release via softprops/action-gh-release's `body_path`, so
  whatever stands here when a `v*` tag is pushed becomes that release's text.

  It describes one release, not a changelog: rewrite it in the same commit that
  bumps `host/vcv/plugin.json`, before tagging. Past texts stay in git history.
  Everything below the comment is public.
-->

## FireFlow 2.23.5

**TUNE goes one octave lower.** On a deck that plays notes (SYNTH, WAVE, BODY,
FEED), turning TUNE fully left now reaches one octave further down than before,
to 55 Hz. Noon and the right half of the knob are unchanged; the left half is
simply wider. A melody near the bottom is folded back at 55 Hz instead of at
110 Hz. SAMPLER and BBD keep their old TUNE range.

The factory patch's deck B sits in TUNE's left half, so its knob value was
adjusted to keep exactly the pitch it had.

**Seven quiet bugs fixed.**

- BODY applied a note's velocity twice, so a soft accent came out far too quiet
  and a weak note after a strong one could sound louder.
- A STEP deck switched back in starts silent instead of resuming an old note.
- RENEW now also renews the last step of an odd-length phrase.
- GRIT's Reduce mode no longer drops the left channel at some settings.
- Texture lanes at STEPS 2 to 4 move with SHAPE sine again instead of standing
  still.
- The hardware firmware stops a deck exciting the other one once it leaves BBD.
- A re-initialised instrument no longer drags a deck's clock from a stale
  offset.

This is a development alpha. Saved patches may break between releases.

## Install

Download the `.vcvplugin` for your platform, unpack it into Rack's user plugin
directory and restart Rack. Built against Rack SDK 2.6.6 for Windows x64, Apple
Silicon (mac-arm64) and Linux x64.
