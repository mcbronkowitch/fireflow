# The original Spotykach firmware

This repository grew out of [Synthux-Academy/Spotykach](https://github.com/Synthux-Academy/Spotykach),
the official firmware for the [Spotykach](https://synthux.academy/store/spotykach)
hardware. Until 2026-09-29 that firmware was still in the tree — `main.cpp`,
`app.cpp`, `src/`, the root `Makefile` and `bootloader-spotykach-v2.bin` — and
still built, although it compiled none of `engine/` and nothing built or tested
it any more. It was removed on 2026-09-29.

**Where it is now:** tag `attic/spotykach-firmware-2026-09-29` (commit `6093a957`).
Read a single file, or check the whole tree out next to this one — not into
it, because `src/hw/board.h` is still live here and would be overwritten:

```bash
git show attic/spotykach-firmware-2026-09-29:src/core/buffer.cpp
git worktree add ../spotykach-attic attic/spotykach-firmware-2026-09-29
```

Every `src/core/…`, `src/ui/…`, `src/memory/…`, `app.cpp:<line>` reference in
`engine/` provenance comments and in dated docs points into that tag. The
upstream repository linked above is the living original.

**What stayed**, because `shell/` and `bench/` use it:

- `src/hw/board.h` — the one board-init header both firmware trees share.
- `alt_sram.lds` — the BOOT_SRAM linker script of `shell/`, `bench/` and
  `bench/audition/`.
- `lib/libDaisy` (the bleeptools fork the upstream firmware needed for its
  ws2812 driver) and `lib/DaisySP`.

The root `Makefile`'s `make libs` target is gone with it; build the two
libraries directly — see [`shell/README.md`](../shell/README.md).
