# Spotymod label micro-adjustments

**Date:** 2026-07-24
**Status:** Approved design, pending written-spec review

## Goal

Remove the visible SCAN/MELO-knob conflict and give the two top RATE captions
slightly more air, without changing any control, parameter ID, knob position,
panel width, or existing surface treatment.

## SCAN placement

MELO keeps its approved radial caption geometry. SCAN moves to the radially
outward side of MELO:

- Deck A reads `SCAN MELO`.
- Deck B is the exact geometric mirror and reads `MELO SCAN`.
- Both words share the current MELO baseline.
- The existing `0.80 mm` inter-label gap remains.
- SCAN remains `1.50 mm`, MUTED, and uses the mirrored outward-growing anchor.

For Deck A, MELO spans from its left glyph edge to its existing end anchor.
SCAN's end anchor is:

```text
MELO left edge - 0.80 mm
```

Deck B derives its SCAN record only by mirroring Deck A:

```text
x_B = panel_width - x_A
anchor_B = flip(anchor_A)
```

This keeps both complete label pairs exact mirrors and moves SCAN away from
the MELO knob rather than between the primary caption and the control.

## RATE placement

The RATE knob positions do not move. Both RATE caption baselines move from
`y = 3.80 mm` to `y = 3.00 mm`, equivalent to roughly 2–3 pixels at Rack's
100% scale.

Deck B retains the same Y coordinate and exact mirrored X/anchor relationship.

## Compatibility

- Panel remains 42 HP.
- All 80 parameter IDs and runtime names remain unchanged.
- Both LED rings and every control keep their current position.
- VOICE, FX, PLAY, center groups, background fields, colors, and opacities
  remain unchanged.
- Regenerated SVG and C++ panel tables remain deterministic.

## Verification

Automated guards must fail before implementation and then prove:

- RATE A/B baselines are exactly `3.00 mm`.
- SCAN is outward of MELO on Deck A with a `0.80 mm` gap.
- The complete MELO/SCAN pair is an exact Deck-A/Deck-B mirror.
- SCAN clears the MELO knob glyph and remains within the panel.
- All existing panel guards, parameter-order checks, and generated-asset
  checks remain green.

The final SVG is visually checked at Rack 100% scale before rebuilding and
installing the plugin.
