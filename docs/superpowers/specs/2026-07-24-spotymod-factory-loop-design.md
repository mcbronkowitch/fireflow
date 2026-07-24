# Spotymod Factory Loop Replacement

## Goal

Replace Spotymod's embedded sampler initialization sound with a short excerpt
from the user-owned source file:

`D:\Audio Projekte\Samples_and_Presets\Bass\Loops\110_16_Schröggelbass_Warped_01.wav`

The replacement must remain self-contained in the VCV plugin and load through
the existing factory-sample path without changing sampler behavior.

## Selected Audio

- Use the first four 4/4 bars of the source.
- Tempo: 110 BPM.
- Target duration: 8.7272727 seconds.
- Target frame count at 48 kHz: 418,909 frames, allowing a one-frame rounding
  tolerance in automated validation.
- Preserve stereo, 48 kHz, and 24-bit integer PCM.
- Do not normalize, fade, crossfade, compress, or otherwise process the audio.

The source is exactly sixteen bars long. Selecting its first quarter keeps a
complete musical phrase while retaining approximately the duration and decoded
memory footprint of the current factory sample.

## Integration

The rendered excerpt replaces `host/vcv/res/factory.wav`. The runtime path and
loading logic remain unchanged. The existing VCV packaging process continues to
include the file through `DISTRIBUTABLES`.

Update the VCV README where it describes the factory sample so that its
ownership and four-bar, 110 BPM content are accurate. No DSP, persistence, UI,
parameter, or panel-layout code changes are in scope.

## Verification

Add an asset-level regression test that initially fails against the old
factory sample and then validates the replacement:

- RIFF/WAVE file
- 24-bit PCM
- stereo
- 48 kHz
- four bars at 110 BPM within one frame

Then run the existing VCV tests, build and install the plugin, and verify:

- the packaged and installed `factory.wav` hashes match the generated excerpt;
- the installed DLL and faceplate remain synchronized with the build;
- the plugin archive exists at the expected versioned path.

Rack must be closed while the unpacked plugin is synchronized.
