# Seed Init-Patch Audition Design

**Date:** 2026-07-25

**Status:** Approved design; implementation pending

## Goal

Run the current spotymod VCV init patch on the connected Daisy Seed as a
continuous real-time listening test. The test must execute the portable
`Instrument` engine on the Seed rather than play a desktop render.

## Scope

The audition is a separate development host, not the M6 production firmware
shell. It has no panel controls, storage browser, settings persistence, or
boot-time installation path. It starts the approved init state automatically
and keeps playing until the Seed is reset or powered off.

The audition must not erase or program the Daisy bootloader, application flash,
or external QSPI. Code is loaded into SRAM through the connected ST-Link. The
existing WAVE bank at `0x90040000` is read in place and must match the current
linked bank before the audition starts.

## Architecture

The host consists of three focused pieces:

1. A desktop preparation tool decodes `host/vcv/res/factory.wav`, resamples
   only if necessary, and emits interleaved 48 kHz stereo IEEE-float data plus
   a small metadata record.
2. A Daisy audition firmware initializes the Seed, the shared engine memory,
   and one `Instrument`. It applies the values from
   `host/vcv/src/init_patch.hpp` through the same semantic setters used by the
   VCV host, then loads the prepared sample into part B's sampler.
3. A launch script uses OpenOCD and ST-Link to load the firmware into SRAM,
   initialize SDRAM, transfer the prepared sample into a reserved SDRAM input
   region, verify the WAVE-bank digest, and release the firmware into its
   continuous audio callback.

The firmware copies the uploaded sample from the reserved input region into
the sampler's owned buffers before starting audio. This keeps the uploaded
source separate from the engine's working memory and makes buffer ownership
explicit.

## Init-Patch Mapping

The audition reproduces the complete audible state of
`spkyvcv::kInitParamDefaults`, including:

- part A using Synth and part B using Sampler;
- both modulation lane sets, STEP modes, FORM/SONG state, scale, tempo, sync,
  coupling, and center controls;
- voice, dynamics, filter, FLUX, grit, room-send, reverb, shuffle, source, and
  detune values;
- the bundled factory sample loaded into sampler part B.

VCV-only widget state and file paths are excluded. No random value may come
from wall-clock time; the engine uses fixed seeds so repeated launches begin
from the same state.

## Audio and Runtime

The Seed runs at 48 kHz with 96-sample blocks, matching the existing hardware
bench. The audio callback processes zero-valued external input through the
real `Instrument` and writes its stereo result directly to the Seed outputs.

Playback begins automatically after initialization and repeats indefinitely.
The engine's sequencer, sampler, effects, and modulation run continuously; the
factory sample is engine material, not a separately looped WAV player.

Monitors must be connected at low volume before launch. The audition applies a
fixed conservative output trim outside the engine to reduce the risk of a
loud first block without changing the engine's internal dynamics.

## Validation and Failure Handling

Desktop tests must verify:

- the factory WAV conversion produces 48 kHz stereo float samples with the
  expected frame count and finite values;
- every VCV init parameter has an explicit audition mapping;
- the upload layout fits in the available 64 MiB SDRAM without overlapping
  engine memory;
- the audition firmware links entirely into SRAM except for the existing
  mapped WAVE bank.

Before audio starts, the firmware validates the uploaded metadata, frame
count, and sample sentinel, and the launcher validates the live QSPI SHA-256
against the current bank payload. Any validation failure leaves both audio
outputs at zero and reports an error through the attached debugger.

The final hardware check confirms that OpenOCD loaded only SRAM and SDRAM,
audio started without a fault, and the process remains alive while the user
listens. Subjective sound quality remains a listening judgment by the user.

## Non-Goals

- Implementing the M6 panel/LED/gesture firmware shell.
- Persisting the audition across power cycles.
- Writing the factory sample to SD card or QSPI.
- Replacing or modifying the installed application firmware.
- Claiming production readiness from this one listening test.
