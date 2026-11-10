# Sampler takes on SD: board measurements

Rev A #1, branch `feat/sampler-takes-sd`. Spec:
[`2026-11-06-sampler-takes-on-sd-design.md`](../superpowers/specs/2026-11-06-sampler-takes-on-sd-design.md),
plan [`2026-11-06-sampler-takes-on-sd.md`](../superpowers/plans/2026-11-06-sampler-takes-on-sd.md).
The board printed every number below. The captures are not in the repo.

## 1. Setup, board session A (plan Task 9), 2026-11-06

- **Board and card.** Rev A #1 on USB, C_SD2 not fitted, the card from the SD
  rounds (FAT32, 64 GB) with their test files still on it.
- **Hands off.** Nobody touched the panel; nothing was listened to.
- **Images.** Clean builds of committed trees, flashed QSPI first, then SRAM,
  through `remote_run.py boot` / `flash --expect-build`. Common flags
  `SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1 SHELL_RAW_SPREAD=1 SHELL_PERSIST=0`:
  persistence off, so no image of the session wrote QSPI and Bastian's
  persisted patch stayed untouched; the engine ran the init patch.
  - `c16e848` + `SHELL_TAKES=1 SHELL_TAKES_STRESS=1`: pieces every 21 blocks (42 ms).
  - `bc44958` + `SHELL_TAKES=1 SHELL_TAKES_STRESS=1`: pieces every 40 blocks (80 ms).
  - `bc44958` without `SHELL_TAKES`: the baseline for the callback (§6).
- **What the stress image does.** Deck A gets a 10 s pattern (480 000 frames,
  3.84 MB) if it loads empty; a save of deck A is forced every 30 s; each save
  is read back and compared with the buffer (`cmp=`). Deck B stays empty.

## 2. §6.1: the pot hold during a save

One 20 min capture per pace. Per save, `held / blocks` from the `save_t` line
(blocks the hold dropped during the save, over the save's length in blocks).
The pots: per sense pin the worst / median of the kept one-second raw spread
over the whole capture (`read_sd.burst_summary`, the first two windows
skipped), against the pin's band.

| pace | saves | `ok=1` | `cmp=ok` | held per save | save of 10 s | worst `f_write` | S0 (64) | S1 (32) | S2 (32) | S3 (32) | over band | over 144 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 21 blocks (42 ms) | 40 (+2 in the boot capture) | 40 | 42 | 10.8–15.8 % | 10.9–11.6 s | 62.0 ms | 52/40.5 | 17/13.5 | 18/15.0 | 18/16.0 | 0 | 0 |
| 40 blocks (80 ms) | 39 | 39 | 39 | 6.0–6.3 % | 19.7–19.9 s | 60.0 ms | 52/40.0 | 19/13.0 | 16/14.0 | 19/16.0 | 0 | 0 |

- At 42 ms every save held more than the gate's 10 %; the worst held 915 of
  5 797 blocks, 3.9 per 16 KB piece (234 pieces). **Fails** the gate.
- At 80 ms every save held 595–628 blocks, 2.5–2.7 per piece. **Passes**: the
  kept readings sit where A1's idle class sat (A1 idle: 50/37, 14/11, 17/12,
  18/12), no pot over its band, none over `kPotTouch`.
- `kTakePieceEveryBlocks` is 40 since `bc44958`. A full 42 s deck is then
  985 pieces, about 83 s per save (arithmetic: 19.8 s per 234 pieces).
- The worst `f_write` per save is not random: over consecutive saves it falls
  from about 60 ms to 4–10 ms and jumps back, a sawtooth over about 12 saves
  (both paces). Printed, not explained.

## 3. §6.2: the first write after mount

The second boot (`bc44958`, the slots existed from `c16e848`'s boot): the first
save's worst `f_write` was **28.0 ms**, inside C2's tail (≤ 131 ms). **Pass.**
The first boot that created the slots: its first save printed 8.0 ms. `frags=2`
on both boots: the most fragmented of the four slots has two fragments.

## 4. §6.3: load time

The `load` line's `ms`, counted in blocks: the chosen slot's header read once
more, its data and the CRC (the mount and the first look at both headers are
not in it). There was
no boot take on these images (`SHELL_PERSIST=0`).

| boot | deck A | deck B |
|---|---|---|
| `c16e848`, slots created | `src=empty` (no valid slot) | `src=empty` |
| `bc44958` | `slot=1 seq=42 frames=480000 ms=478 crc=ok src=card` | `src=empty`, 0 ms |

478 ms for 3.84 MB read back to back with the CRC. Two full decks need
Bastian's hands (Task 10).

## 5. §6.4: CRC cost

`SHELL_TAKE crc sw_ms=164 kib=3750`: the software table over the 10 s pattern
(3.84 MB in SDRAM), 164 ms, about 43 ms per MiB. The H7's CRC unit is not
built. The load's 478 ms include the CRC over the same bytes.

## 6. The audio callback

`cb_max` (worst callback since the first second, DWT cycles; the block is
960 000):

| image | capture | `cb_max` |
|---|---|---|
| takes, 42 ms | 20 min | 693 392 |
| takes, 80 ms | 20 min | 689 596 |
| no takes | 10 min | 690 044 |

The take images sit within 3 400 cycles of the baseline either way (0.35 % of
a block): saving and the read-back cost the callback nothing measurable on
this patch. One capture per image; the spread between runs of one image is
not measured here.

## 7. What is left for Task 10

Two full decks, a real take on each deck, overdub, SHIFT + REC, power cuts
during a save, bad cards, the file on a PC, persistence together with takes,
and pots turned by hand during a save and the boot load.
