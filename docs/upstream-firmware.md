# The original Spotykach firmware

This repository grew out of [Synthux-Academy/Spotykach](https://github.com/Synthux-Academy/Spotykach),
the official firmware for the [Spotykach](https://synthux.academy/store/spotykach)
hardware, and the original firmware tree is still here: `main.cpp`, `app.cpp`,
`src/`, the root `Makefile`, and the libDaisy / DaisySP submodules.

It is **not** what this project builds any more. spotymod's own hardware target
is a standalone Daisy Patch Submodule prototype (milestone M6); porting the
modulation-first engine onto Spotykach hardware is no longer planned. The tree
is kept because it still compiles, and because it documents the drivers,
clocking and bootloader the project started from.

The instructions below build and flash **that original firmware**, not the
modulation-first engine.

## Setup

Clone recursively, or run `git submodule update --init --recursive` to fetch the
submodules (libDaisy + DaisySP).

Note: the ws2812 driver requires a slight modification to libDaisy, so the
libDaisy submodule points at a specific branch within the bleeptools fork (based
on the Infrasonic Audio fork), which also carries a few MIDI and mpr121 changes.

## Compiling

Build the libraries once (a `Makefile` target is provided):

```bash
make -j8 libs
```

Then build the firmware:

```bash
make -j8
```

On success the binaries land in `build/`: `spotykach.bin` (flashed via DFU) and
`spotykach.elf` (for debugging).

## Flashing

The bootloader enables USB DFU updating from the **external** USB-C port on the
rear of the main PCB (not the one on the Seed).

1. Compile the firmware (above).
2. Connect the main PCB's USB-C port to the computer (a data-capable cable).
3. Hold `Reset` on the back of the unit for ~3 seconds — the bottom-pad LEDs
   start to "breathe" in white.
4. Run `make program-dfu`.

The device then boots the new firmware. A bad flash can temporarily "brick" the
unit and require reinstalling the bootloader, firmware, or both.
