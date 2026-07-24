# Sampler Preset as the VCV Init Patch

## Goal

Make a fresh Spotymod VCV module and Rack's **Initialize** action reproduce the
panel state stored in `sampler.vcvm`. The init patch must use the plugin's
existing bundled `host/vcv/res/factory.wav`; it must not retain or access the
preset's machine-specific absolute WAV path.

## Init State

All 80 parameter values from the preset are the new defaults, indexed by the
existing stable `ParamId` values. This includes both engines, STEP switches,
SYNC, scale, FX, per-deck room mix, and SHUFFLE. Momentary controls and REC
remain off as recorded in the preset.

The non-parameter principle state defaults to `[2, 0]`. A fresh instance and
Rack **Initialize** both restore that state and push it into the engine.

Part A remains in Synth mode. Part B starts in Sampler mode. Because Part B is
empty and has not consumed its factory-load attempt after initialization, the
existing factory autoload path loads `res/factory.wav` into Part B. The
external file recorded for Part A in `sampler.vcvm` is intentionally ignored.
The bundled WAV itself is not replaced or modified.

## Implementation Shape

Keep the defaults code-native in the VCV host rather than loading a bundled
`.vcvm` file at runtime. Centralize the complete parameter snapshot so every
`configParam`/`configSwitch` call reads its default from one indexed source.
This removes the current split between `defaultFor()` and hard-coded defaults
inside `configControls()` and makes the saved snapshot auditable against stable
parameter IDs.

Reset the non-parameter principle state explicitly in the module reset path.
Do not change patch/preset deserialization: saved patches must continue to
override init defaults, including their own parameter values, principles, and
sampler persistence data.

## Verification

Add a source-level VCV host test before changing production defaults. It must
fail against the old init patch and then verify:

- the indexed init snapshot contains exactly `NUM_PARAMS` entries;
- representative and structurally important values match the preset,
  including both engine switches, scale, filters, room mixes, and SHUFFLE;
- the default principle state is `[2, 0]`;
- the factory asset remains part of the VCV distribution.

Then run the complete panel test, the repository C++ test suite, and the local
VCV build/install script. Verify that the installed unpacked plugin DLL is the
freshly built DLL and that the installed plugin contains the unchanged bundled
`factory.wav`.

## Non-goals

- Do not replace `factory.wav`.
- Do not bundle `sampler.vcvm`.
- Do not copy or reference the absolute `D:\...` sample path.
- Do not change saved-patch compatibility or existing `ParamId` values.
- Do not push commits or tags as part of this task.
