# SOURCE Knob and Detune Menu Design

**Date:** 2026-07-25
**Status:** Approved after live-playability review
**Scope:** VCV control surface and shared Synth/Wave/Sampler parameter routing

## Problem

The Synth and Wave engines expose their most important oscillator-color
parameter only through `LANE_SOURCE`. Its base is fixed at `0.5`, so the user
can shape the modulation but cannot directly choose a steady oscillator
timbre or wavetable frame.

The existing per-part `DTUN` knob has comparatively little audible travel and
is adjusted less often. In the Sampler it is already repurposed as
`ORGANIZE`, which is the Sampler's SOURCE base. The physical control is
therefore a natural home for one consistent SOURCE-base concept across all
three engines.

Patch compatibility is explicitly out of scope because the instrument is
still in development. No migration or preservation of the old parameter IDs
is required.

## Decision

The physical `DTUN` knobs become per-part `SOURCE` knobs. Their visible meaning
follows the selected engine:

| Engine | Caption | Meaning |
|---|---|---|
| Synth | `TIMB` | Oscillator morph: sine → triangle → saw → pulse |
| Wave | `FRAME` | Continuous position through all 16 wavetable frames |
| Sampler | `ORG` | Read position in the recorded or loaded material |

The normalized range is `0..1` and the initial value is `0.5` for both parts.
The existing SOURCE lane remains active and modulates around this base through
the normal target equation. The per-part `MOD` control continues to set the
texture-lane modulation depth. No second or engine-specific SOURCE state is
introduced: switching engines immediately reinterprets the current physical
knob position.

## Detune

Detune moves from the panel to two ordinary but widgetless VCV parameters,
one for each part. They are exposed as `Detune A` and `Detune B` sliders in
the module context menu.

- Range: `0..35 ct` total oscillator spread.
- Default: `6 ct`.
- The two oscillators remain symmetrically offset by half of the configured
  spread.
- Detune is independent of SOURCE. Changing or modulating TIMB or FRAME never
  changes oscillator tuning width.

Thus `6 ct` always produces oscillator offsets of approximately `+3 ct` and
`-3 ct`, regardless of the current SOURCE base or lane output. This removes a
hidden second function from the primary live TIMB/FRAME control.

The widgetless parameters are normal Rack parameters rather than custom JSON
fields, so Rack handles patch persistence. They remain normalized internally
and their displayed value is scaled to cents. The context menu also provides
an explicit per-part reset to exactly `6 ct`; the current value is always
shown in cents.

Undo and external automation are not implied merely by using a Rack
parameter. If the selected menu widget cannot prove correct history and
mapping behavior, those capabilities remain out of scope rather than being
advertised.

Both melodic engines receive the setting. The Sampler continues not to
receive Synth/Wave detune, matching the existing engine split.

## Parameter and Routing Changes

The generated panel parameter currently named `DETUNE_A/B` is renamed to
`SOURCE_A/B` without changing its physical location. Two new widgetless
`DETUNE_A/B` parameters are added to the generated parameter model and
configured even though no panel widgets are created for them.

The host pushes:

1. `SOURCE_A/B` to `LANE_SOURCE` base for Synth, Wave and Sampler;
2. widgetless `DETUNE_A/B` to `set_voice_detune()` for Synth and Wave.

The shared melodic-engine implementation stops multiplying detune by the
modulated SOURCE target. Its configured value is the actual total oscillator
spread for both Synth and Wave.

The current engine-dependent SOURCE-base branch is removed. In particular,
Synth and Wave no longer overwrite SOURCE with `0.5` on every control update.
Sampler `ORGANIZE` behavior remains the same apart from reading the renamed
SOURCE parameter.

The current caption path is static, so this change introduces real
engine-aware rendering rather than adding another always-visible alias.
Exactly one caption is drawn at each SOURCE knob:

- `TIMB` when the corresponding ENG parameter selects Synth;
- `FRAME` when it selects Wave;
- `ORG` when it selects Sampler.

The caption follows the selected ENG parameter immediately, including
automation and patch restoration, rather than waiting for the audio engine's
short crossfade to complete. Generated static aliases at that position are
suppressed, so captions cannot stack or go stale. The stable Rack parameter
name and tooltip use `SOURCE A/B` and explain all three meanings, avoiding a
second dynamic string state that could disagree with the panel.

## Initialization

The VCV initialization snapshot is updated so that:

- `SOURCE_A = 0.5`;
- `SOURCE_B = 0.5`;
- widgetless `DETUNE_A = 6 / 35`;
- widgetless `DETUNE_B = 6 / 35`.

The shared engine's standalone boot value may remain its existing raw default,
because each host is responsible for pushing its surface state after
initialization. Tests must distinguish the engine-only boot contract from the
VCV surface default.

## Verification

Automated coverage must establish:

1. `SOURCE_A/B` appear at the old DTUN knob positions and the generated
   parameter ordering remains internally consistent.
2. The two widgetless detune parameters are configured, have no panel widgets,
   display `0..35 ct`, default to `6 ct`, survive patch save/reload, and can
   be reset exactly to `6 ct` from the context menu.
3. In Synth mode SOURCE sets the oscillator morph base and the SOURCE lane
   modulates around it.
4. In Wave mode SOURCE reaches frame positions `0..15` continuously and the
   SOURCE lane modulates around it.
5. In Sampler mode SOURCE retains the existing ORGANIZE behavior.
6. Engine switching reinterprets one current SOURCE value without separate
   memories or soft takeover.
7. Detune is independent of SOURCE base and modulation, reaches both melodic
   engines as the configured total spread, remains symmetric between their
   oscillator pairs, and does not reach the Sampler.
8. The VCV context menu contains independent Detune A and Detune B controls.
9. Rendered Synth, Wave and Sampler panel states show exactly one correct
   SOURCE caption for both parts. ENG automation and patch restoration update
   it immediately, and `FRAME` fits without collision.
10. Panel generation, initialization-snapshot checks, the native test suite,
    and the VCV local build all pass.

## Out of Scope

- Legacy patch or automation migration.
- Detune automation or MIDI-learn discoverability from a widgetless menu
  parameter.
- A hardware gesture for editing detune.
- Separate SOURCE memories per engine.
- Changing the SOURCE lane curve, target depth, or `MOD` behavior.
- Changing the 35-cent detune ceiling.
