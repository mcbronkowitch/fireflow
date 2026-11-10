# Sampler takes on the SD card: a recording survives a restart

Date: 2026-11-06. Status: design, not built; revised the same day after an
independent review (§10). Firmware: `shell/` (Rev A play image). Engine:
`engine/sampler/`. Hardware: Rev A #1 as it is, **C_SD2 not fitted**
(Bastian, 2026-11-06: soldering is deferred until further notice), so fault R5
stands and every card transfer needs the pot hold.

## 1. Intent

Bastian wants the sampler's recordings on the SD card, so that a take
survives a power cycle. Today a take lives only in SDRAM
(`shell/sdram_mem.cpp`, `g_sampler[2][2 016 000]`) and is gone at the next
boot. Panel-state persistence (spec `2026-10-28-panel-state-persistence-design.md`)
already brings back the knobs; this spec brings back the material the knobs
play.

After this change each deck's last take comes back shortly after boot, the
slice map it had included, and an overdub after the boot lands exactly as it
would have before the power cut.

## 2. Goals, non-goals, anti-goals

Goals
- Each deck's take is saved to the card without a button press and is in the
  buffer again after a power cycle.
- The restore is exact: the same frames, the same slice markers and
  strengths, the same overdub behaviour (§3.2 P2, P4). One measured
  exception: a take that was never overdubbed still has the feedback switch's
  4 ms rise ahead of it, a restored one does not (`SampleBuffer::settle_loop`),
  so the first overdub after such a take can differ in its first
  `kRecordFade` frames, under its own fade-in (Task 3's test pins this).
- A power cut at any moment leaves the previous take loadable.
- The pots stay playable while a take saves (paced writes, §4.3).
- A take can be cleared from the panel, and the clear survives a restart too
  (§4.5).
- The files are ordinary WAV files a computer can open.
- A hung or broken card never takes the board down, and the board stays
  reflashable without hands (§4.4, §4.6).

Non-goals
- A sample library: several takes per deck, browsing, naming. The panel has
  no display and no free keys for it; that is a later round.
- Loading arbitrary WAV files from the card (other rates, mono, 16-bit). The
  loader reads only what this firmware wrote (§4.1). VCV keeps its own WAV
  path.
- Card hot-plug. The card is mounted once at boot.
- Streaming (field recordings, idea 2 of `sd-use-cases-measured.md` §18) and
  reverb grains from card files (idea 1).
- Restoring transient sampler state: playhead, grain cloud, SCAN offset. The
  persistence spec excludes transient state for the same reason (§1 there).
- Restoring sampler options. Tape mode, reverse and feedback are constant
  defaults on the hardware (`g_opt`), so a restore loses none of them.

Anti-goals
- No conversion to 16 bit (§3.2 P1 is why).
- No card traffic and no bulk memory work (memset, CRC, copy) in the audio
  callback. Every card call runs in the foreground.
- No saves while a take is being recorded or overdubbed.
- No edits inside `lib/libDaisy` (the submodule stays as it is; §4.1).

## 3. Facts

### 3.1 Measured on the board (`docs/hardware/sd-use-cases-measured.md`)

All without C_SD2, on the 64 GB FAT32 card that is in Rev A #1.

| fact | number | source |
|---|---|---|
| Streaming without a hold puts every pot over its band (R5) | all 73 pots | §3 (A1), `reva-bring-up.md` fault R5 |
| Dropping the pot readings of every block that saw a transfer, tail 0, keeps the kept readings at the idle floor | 4.7–5.3 % of blocks held at 384 KB/s, ~10 % at 768 KB/s, longest hold 3 blocks (32 KB reads) | §3 (A1) |
| 16 KB reads cost the callback ≤ ±9 k cycles per block (≤ 1 %), 32 KB reads up to +22 k, streaming flat out | writes were not priced by chunk size | §4 (A3) |
| A full buffer (16 128 000 B) as float, 16 KB pieces back to back: save 3 077 ms max / 2 995 mean, load 1 977 / 1 972 ms; float loads are bit-exact; the callback does not get dearer | worst `f_write` 31.9 ms, worst `f_read` 4.0 ms; held 1 539 blocks (save), 990 (load), i.e. ~1.6 / ~1.0 held blocks per 16 KB piece | §13 (D2), `kOpChunk` = 16 KB |
| Write latency: p99.9 65–131 ms; one single stall of 1.97 s in 20 min of writing | 32 KB writes at 192–288 KB/s and back to back | §7 (C2) |
| The first write after mount is slow | 1.95 s (D4), 3.84 s (D3); a free-cluster search is the inferred cause, not measured | §14, §15 |
| A 540 B file saves with its `f_sync` in about 20 ms and reads back in 2.5 ms | 100 of 100 compare | §15 (D4) |
| The Daisy bootloader scans the card **root** for `*.bin` on every boot | — | §2 (P1), §17 (D5) |
| The SDMMC bus clock must stop between transfers (`CLKCR.PWRSAV`) | with it running, SENSE_2/3's idle raw spread was ~95 counts, against 14 before the card was fitted | `shell/sd_probe.cpp` `kClockPowerSave` |

### 3.2 Probed for this spec (desktop, 2026-11-06)

Scratch probes, clang -O2, the engine sources at `3c3801b4`, 48 kHz.

**P1 — the buffer leaves ±1 under overdub.** `SampleBuffer` alone, a 2 s take
of full-scale input (sine 220 Hz at 1.0; uniform noise ±1; decaying noise
bursts every 250 ms at 0.3–1.0), then REC again on the locked loop (an
overdub, which is what REC does once a take exists) for 30 passes with fresh
input of the same kind.

| input | FEED knob | take peak | peak after 1 / 3 / 10 / 30 overdubs | samples > 1 after 30 |
|---|---|---|---|---|
| sine | 0.00 | 1.000 | 0.999 / 1.001 / 1.001 / 1.001 | 2.8 % |
| sine | 0.80 | 1.000 | 0.818 / 1.300 / 1.335 / 1.335 | 46.1 % |
| sine | 0.95 (default) | 1.000 | 0.829 / 1.990 / 4.626 / 5.465 | 88.3 % |
| sine | 1.00 | 1.000 | 0.620 / 2.063 / 2.307 / 2.307 | 96.7 % |
| noise | 0.95 | 1.000 | 1.809 / 2.950 / 4.072 / 4.543 | 55.0 % |
| bursts | 0.95 | 0.983 | 0.976 / 1.576 / 2.210 / 2.097 | 0.6 % |

A take alone stays at its input level; any overdub at the default FEED
does not, by up to 5.5× (+14.7 dB). A 16-bit file scaled to ±1 would clip most
of such a buffer; one normalised per file would spend up to ~15 dB of its range
on headroom. The file is 32-bit float (§4.1), which D2 measured as bit-exact.

**P2 — a rescan does not reproduce the live slice map.** `SampleBuffer` plus
`SliceMap` driven as `SamplerEngine::process_in` drives them (write, then
`on_write` with the frame that landed). Bursts every 12 000 frames, 4 s take;
then one overdub pass at the default FEED with bursts offset by 6 000 frames.
The live map is compared with `scan()` over the same float buffer
(`load_sample`'s path) and over an int16-quantised copy.

| case | markers live / scan(float) / scan(int16) | live = scan(float) (frame and strength) | scan(float) = scan(int16) |
|---|---|---|---|
| take only | 16 / 16 / 16 | 15 of 16 (marker 11 906: strength 195 live, 174 scanned) | 15 of 16 |
| take + overdub | 32 / 32 / 32 | 31 of 32 (marker 5 911: 14 against 15) | 32 of 32 |

The positions agree; strengths do not always. Strength drives the sampler
FEEL accents (M5e), so a rescan would change how a restored take plays. The
file carries the map itself (§4.1).

**P3 — a finished take survives an engine switch.** `Instrument` with
SDRAM-sized static buffers (10 s), deck A on SAMPLER, a 2 s take of a 220 Hz
sine at 0.5 with REC released, then SYNTH for 2 s, then SAMPLER again for 2 s:
`rec_size` 96 000 and the slice count (1) unchanged at every step. A finished
take on a deck that is currently on SYNTH is saved and restored like any other.

**P4 — an overdub after a restore lands as on the live buffer.** Two
`SampleBuffer`s: A records a 2 s take; B gets A's frames copied in and
`set_rec_size(A.rec_size())`. Both run 1 s idle, then the same 2 s overdub at
the default FEED: **0 of 96 000 frames differ.** Both have `write_head` 0 after
the take / restore.

**P5 — an engine switch during a take leaves the deck "recording".** As P3,
but SYNTH is selected 1 s into the take, then REC is released (what the law
does once the deck is off SAMPLER, `control_law.h` `wantRec`): after 2 s on
SYNTH `is_recording()` is still 1 (`rec_size` 48 191). Back on SAMPLER it
drops to 0 within 2 s. The fade-out only advances while `process_in` reaches
the sampler, which it does only on a SAMPLER deck (`part.h`, `_engine_wants_in`).
§4.3 copes with this rather than changing the engine.

### 3.3 Read from the source (not runtime claims)

- **The buffer is never zeroed on the board.** `g_sampler` is NOLOAD SDRAM
  (`shell/sdram_mem.cpp`). `SampleBuffer::clear()` memsets only when it held
  content (`_size != 0`), and `init()` ends in that `clear()`, so SDRAM garbage
  survives init. `sampler_engine.h` (`sample_data()` note) requires hosts to
  inject zeroed memory; the shell does not.
- **A fresh take adds onto what is in memory.** With the loop not yet cut,
  `fb` is 1 and `write()` sums the input onto the old frame
  (`sample_buffer.cpp`, `write()`). So whatever lies past `rec_size` matters
  the moment REC starts on an empty deck.
- **`clear()` is a 16 MB byte loop.** `docs/gotchas.md`: `memset` on the board
  is a byte loop at ~3 cycles per byte into SDRAM, ~48 M cycles for a full
  buffer, about 50 blocks of callback. It must not run in the callback.
- There is no clear on the hardware today: `sampler_clear` is called by no
  `shell/` or `control/` code. Since REC on a locked take overdubs (P1's setup
  is that path), the panel can only add to a take, never start a fresh one.
  With persistence that becomes permanent, hence §4.5.
- REC records only on a SAMPLER deck (`control/control_law.h`, `wantRec`).
- SHIFT has one use today: SHIFT + MOD clears the depth twins
  (`shell/controls.cpp`, `apply_key_edges`, called from `panel_scan_tick` in
  the audio callback).
- `SHELL_SD=1` today is a probe image: it requires `SHELL_REMOTE=1` and
  `SHELL_RAW_SPREAD=1` (`shell/Makefile`) and runs its card work after
  `StartAudio()` and after the listener is up, so a hung card still leaves the
  bootloader reachable. The pot hold (`sd_hold_block`, `g_sd_seen`) exists
  only under `#if SHELL_SD` in `panel_scan.cpp`.
- libDaisy's FatFs is revision 68300 with `_USE_EXPAND 0` and
  `_USE_FASTSEEK 1` (`lib/libDaisy/src/sys/ffconf.h`); `f_expand` is not
  available. `sd_diskio.c` cleans the D-cache before a DMA write and
  invalidates after a read; its only timeout is `SD_TIMEOUT` = 30 s per wait.
- Every `f_sync` after a write rewrites the file's directory entry (size,
  time), even when the size does not change (`ff.c`, `f_sync`).
- `g_sampler` is 32-byte aligned only because `.sdram_bss` starts at
  `0xC0000000` and every object before it is a multiple of 32 B in size
  (`shell.map`); nothing asserts it.
- `System::GetNow()` runs about 25 % slow while audio plays (`docs/gotchas.md`).
- Lines printed before `StartLog()` are never seen (`docs/gotchas.md`);
  `SHELL_PERSIST src` is repeated every 5 s for that reason.

## 4. Design

### 4.1 The file

One folder per slot under `/FIREFLOW/`, never in the root (the bootloader's
`*.bin` scan, §3.1): `/FIREFLOW/A0/TAKE.WAV`, `/FIREFLOW/A1/TAKE.WAV`,
`/FIREFLOW/B0/TAKE.WAV`, `/FIREFLOW/B1/TAKE.WAV`. Each slot's directory entry
sits in its own directory sector, so the `f_sync` that rewrites one slot's
entry (§3.3) cannot tear another slot's. Deck A writes its two slots in turn,
deck B likewise.

Each slot is a RIFF/WAVE file:

| offset | chunk | content |
|---|---|---|
| 0 | `RIFF` … `WAVE` | RIFF size covers the whole file |
| 12 | `fmt ` (and `fact`, if a reader needs it for format 3) | format 3 (IEEE float), 2 channels, 48 000 Hz, 32 bit |
| after `fmt ` | `ffsm` | FireFlow sampler meta, sized so that `data` starts at 4 096 |
| 4 088 | `data` | `frames × 8` bytes: `SampleBuffer::Frame` {l, r} interleaved, the buffer's own layout |
| 4 096 + frames × 8 | `JUNK` | pads the file to its preallocated size (below) |

The whole first 4 096 B — RIFF header, `fmt `, `ffsm`, the `data` chunk
header with its size field — is one block, written as one piece (§4.2).

`ffsm` holds: magic, layout version, deck (0/1), `seq` (32-bit, per deck,
starting at 1, incremented per save; 0 means invalid), `frames`, the slice
map (count, then per marker its frame as u32 and its strength as u8; at most
`kMaxSlices` = 512, 2 560 B), and a CRC-32 over the `ffsm` fields before it
and the `data` bytes.

The `data` chunk starts on a sector boundary (4 096), so FatFs can move whole
sectors straight between the SDRAM buffer and the card, without its sector
buffer and without a conversion. That is D2's f32 path.

Every slot is created once at its maximum size (`4 096 + 16 128 000 + 8` B)
and from then on only overwritten in place. Since `f_expand` is compiled out
(§3.3) and the submodule is not edited, the file is grown at creation by
`f_lseek` to its full size and an `f_sync` (FatFs allocates the clusters); the
builder may instead put a shadow `ffconf.h` ahead of libDaisy's on the include
path to enable `_USE_EXPAND`. Contiguity is checked once after creation with
fast seek's cluster map, as C1 did (`frags=`); it is a speed matter, not a
correctness one. A save then never allocates clusters, which is the inferred
cause of the slow first write (§3.1; that this removes it is unmeasured, §6).
The `JUNK` chunk keeps the file valid RIFF at every take length; a computer
plays the `data` chunk and skips the rest. Four slots take 64.5 MB of the card.

### 4.2 Validity and power loss

A slot is valid when magic, version and deck match, `seq ≠ 0`,
`frames ≤ kSamplerFrames`, the markers are strictly ascending, each
`< frames`, at most `kMaxSlices`, and the CRC matches. Per deck the valid slot
with the higher `seq` wins.

A save always goes to the deck's **other** slot, in this order:

1. Write the slot's 4 096 B header block with `seq = 0` (invalid), `f_sync`.
2. Write `data` in paced pieces (§4.3), computing the CRC over each piece as
   it goes out, then the `JUNK` header.
3. Write the final header block (new `seq`, `frames`, `data` size, slice map,
   CRC), `f_sync`.

A power cut in step 1 or 2 leaves the slot invalid and the other slot
untouched; one in step 3 leaves a CRC mismatch. Either way the previous take
loads. The new take is lost only if the power goes before step 3 completes.
What is not covered: a torn write of a FAT sector or of the FAT32 FSInfo
sector. Neither is written after creation (no allocation, no size change), so
the exposure is limited to the first boot that creates the slots.

### 4.3 When and how a take is saved

**Generation.** The engine keeps a content generation per deck (§4.7). It
changes when the buffer's state machine leaves idle (a take or overdub
starts), when it returns to idle (the fade-out finished), on a clear, and on a
restore. Tied to the state machine rather than to `set_recording` calls, it
also covers a take whose fade-out finishes later (P5).

- **Trigger.** A deck whose generation differs from the one last saved, and
  which is not recording, is saved once it has stayed so for
  **`kTakeQuietS` = 5 s** (my call). A take that is overdubbed again within
  those 5 s is saved once, after the last pass. Right after a restore the
  "last saved" generation is seeded to the restored one, so a boot does not
  re-save what it just loaded.
- **P5's case.** A deck switched off SAMPLER in the middle of a take stays
  "recording" (P5) and is not saved until it is back on SAMPLER, where the
  fade-out completes and the generation moves. If the board is switched off
  before that, the take is lost and the previous one loads. Documented, not
  fixed (an engine change that ends the take on the switch would alter how a
  take ends today; §8).
- **Pacing.** The data goes out in **16 KB pieces** (A3) at **one piece per
  42 ms** (≈ 390 KB/s). Every block that sees a piece drops its pot readings
  (A1's hold, tail 0). From D2's ~1.6 held blocks per piece and the 2 ms block,
  that is ~9 % of blocks held during a save — arithmetic, not measured; at
  21 ms per piece it would be ~19 %. A 10 s take (3.84 MB) takes ≈ 9.8 s, a
  full 42 s buffer ≈ 41 s. The pace is a constant the §6.1 measurement sets.
- **Timing source.** Quiet time and pacing are counted in audio blocks (or the
  DWT counter), never with `System::GetNow()` (§3.3).
- **Abort.** The save is a sequence lock: the foreground reads the deck's
  generation and recording flag (through a volatile read or a fence, as the
  foreground's other polls of callback state do), copies the piece or the
  slice map, then reads them again; any change aborts. This runs around every
  piece and around the slice-map copy before step 3. On an abort the slot stays
  invalid (step 1 already marked it), and the trigger starts over. Nothing in
  the audio path waits for the card.
- **Order.** One save at a time; deck A before deck B when both are due. The
  panel persistence save (QSPI) and a take save never interleave: both run in
  the same foreground loop, one after the other.
- **Stalls.** A card stall (C2: p99.9 up to 131 ms, once 1.97 s) only delays
  the save; the source is the SDRAM buffer, not a stream, so there is no ring
  to run dry. But the pots are held for as long as one `f_write` lasts, so a
  stall freezes the pots for its length.
- **D-cache.** The callback writes the buffer through the D-cache; the save's
  DMA reads SDRAM. `sd_diskio.c` cleans the cache over each write's range
  before the transfer (§3.3), so each piece must start and end on a 32-byte
  line: `g_sampler` gets `alignas(32)` and a `static_assert`, and the piece
  size and the 4 096 B data offset are multiples of 32.

### 4.4 Boot

Card work starts **after** `StartAudio()`, after the listener is up and after
the boot take is done (`boot_take_ready`), in `run_panel_scan_report`'s
foreground — where the SD probe image runs its card work, for the same reason:
a card that hangs still leaves the bootloader reachable, so the board can be
reflashed without hands. The board plays at once; each deck's sampler
material arrives when its load is done.

Before audio starts, in `main.cpp` after `inst.init()`: nothing touches the
card. The buffer's garbage (§3.3) is made harmless by the written-frame
watermark (§4.7), not by zeroing 32 MB.

In the foreground, once the boot take is done:

1. Mount the card (bus clock stopped between transfers).
2. Create `/FIREFLOW/`, the four slot directories and any missing slot file
   (§4.1). On later boots this step writes nothing.
3. Per deck, while that deck is **loading** (§4.7: it plays as empty and its
   REC is locked out): read both header blocks, take the newer valid-looking
   one, read its `data` into `g_sampler[p]` while computing the CRC. Reads run
   back to back with the pot hold, which freezes the pots for about 2 s per full
   deck (D2: 990 held blocks for 16 MB) right after boot. On a CRC mismatch, try
   the other slot; if neither is valid, the deck stays empty.
4. Hand frames and slice map to the engine (`sampler_restore`, §4.7), applied
   by the callback at the top of a block; seed the deck's last-saved
   generation (§4.3).

A whole-load deadline (DWT, e.g. 10 s per deck) abandons a load that does not
finish: the deck stays empty, saves stay off for the session, the log says so.
libDaisy's 30 s per-wait timeout (§3.3) cannot be shortened without editing
the submodule, so a hung call can still block the foreground loop that long;
the listener keeps the board reflashable meanwhile.

### 4.5 Clearing a take

**SHIFT + REC A / SHIFT + REC B clears that deck's take** (my call; SHIFT has
no other REC use). It sets the deck's REC latch to 0 and calls
`sampler_clear(p)` from the callback, which is O(1) with the watermark (§4.7):
grains killed, `rec_size` 0, watermark 0, generation bumped. No memset. 5 s
later the deck saves an empty take: steps 1–3 of §4.2 with `frames = 0`, which
writes the 4 096 B header block. After a restart the deck is empty.

### 4.6 No card, a bad card, a failing card

- Mount fails, no card, or not FAT: the board plays as today, both decks
  empty, no saves for this session.
- A card call that returns an error during a save: the save is abandoned, the
  trigger retries after `kTakeQuietS`; after three failures in a row saves stop
  for this session.
- The outcome of mount, of each deck's load (slot, `seq`, frames, or why it
  was rejected) and of each save goes into a latched `SHELL_TAKE` line that
  the report loop repeats every 5 s, as `SHELL_PERSIST src` is, since lines
  produced before the log is open are never seen (§3.3).

### 4.7 Engine API (portable, host-tested)

- **Written-frame watermark in `SampleBuffer`.** Frames at or past the
  watermark count as zero: `write()` treats the old frame there as silence
  instead of reading it, and the watermark follows the write head up.
  `clear()` sets it to 0 and skips its memset; `set_rec_size(n)` sets it to
  `n`. The reads (`read_linear`) only ever see `< rec_size ≤ watermark`. On the
  desktop hosts, whose memory is zero anyway, output is unchanged; the RED is a
  test that injects non-zero memory and records a fresh take (§7). This
  replaces "hosts must inject zeroed memory" (`sampler_engine.h`), and it is
  the pattern the BBD line already uses (`engine/fx/bbd.h`, `filled_`: a cell
  not written since the last reset reads as stale).
- `SamplerEngine::content_gen()` (and `Instrument::sampler_content_gen(p)`):
  a 32-bit counter, changed on the state transitions listed in §4.3, on
  `clear()` (the `SamplerEngine` one, not `SampleBuffer::clear()` inside
  `init()`), on `load_sample()` and on `restore()`.
- `SliceMap::export_markers(frame*, strength*, max)` / `import_markers(…)`;
  the import leaves the detector as `scan()` does (`_last_frame = SIZE_MAX`).
- `Instrument::sampler_buffer(p)`: the raw buffer and its capacity, for the
  foreground's reads and the load.
- **Loading state.** `Instrument::sampler_begin_load(p)` (callback side):
  grains killed, deck empty, REC locked out until the restore or an abort.
  `Instrument::sampler_restore(p, frames, markers…)` (callback side, top of a
  block): `set_rec_size(frames)` on what the foreground wrote into the buffer,
  markers imported, generation bumped, REC unlocked. The foreground hands both
  over through a mailbox, never by calling into the engine itself.
- `shell/` gets a pure, host-tested unit for the file layout (header encode /
  decode / validate, slot choice) and one for the save state machine (trigger,
  pacing, abort, seed after restore), as `persist_record` and `persist_policy`
  are for QSPI.

### 4.8 Build switch

`SHELL_TAKES` (0/1), independent of `SHELL_SD` / `SHELL_SD_PROBE`, which stay
the probe images. It links FatFs and the take code into the play image
without the raw readout, and works with or without `SHELL_BOOTCMD`.

- Refused together with `SHELL_SD=1` (both own the card), and with the
  measurement images `SHELL_REMOTE`, `SHELL_REMOTE_MEASURE`, `SHELL_RGRAIN` and
  `SHELL_BOOT_PATCH`, so card traffic never lands in a CPU or workload
  measurement.
- Resolved at parse time with its objects in `SWITCH_OBJECTS`, like every
  other `shell/` switch (`docs/gotchas.md`, stale objects: two switch positions
  must never yield the same image).
- The pot hold (`sd_hold_block`, `g_sd_seen`) moves out of `#if SHELL_SD` into
  a shared unit both images use.
- Default 0 until §7 passes, then 1 in the play image (`SHELL_PANEL_SCAN=1`).

## 5. What it costs

| cost | size | status |
|---|---|---|
| Pot readings dropped during a save | ~9 % of blocks at one 16 KB piece per 42 ms | arithmetic from D2; unmeasured (§6.1) |
| Save duration | ≈ 9.8 s for a 10 s take, ≈ 41 s for a full buffer | arithmetic |
| Pots frozen during a card stall | p99.9 up to 131 ms, once 1.97 s (C2) | measured for writes, not for this pattern |
| Pots frozen while loading at boot | ≈ 2 s per full deck (D2: 990 blocks) | measured for a scratch buffer |
| Audio callback | none measurable while D2 moved a whole buffer (busy −5 242 against idle); ≤ ±9 k cycles per block for 16 KB reads flat out (A3) | measured; paced 16 KB writes not priced on their own |
| CRC | over every saved and loaded byte, in the foreground | unmeasured (§6.4) |
| Card space | 64.5 MB | arithmetic |
| A take recorded less than ~5 s + its save time before a power cut | lost; the previous take loads | by design |

## 6. Board measurements owed before the plan is final (probe rule)

1. **Hold share for paced 16 KB writes** at one piece per 42 ms (and 21 ms for
   comparison): blocks held and pot spread, against A1's numbers. Gate: kept
   readings at the idle floor, held ≤ 10 % of blocks during a save. The pace
   constant is set from this row.
2. **The first write after mount, with preallocated slots.** Boot twice: on the
   second boot (slots exist) the first save's worst `f_write` must sit within
   C2's tail (≤ 131 ms), not at D3/D4's 2–4 s.
3. **Load time after boot** with no card, with empty slots, with a 10 s take
   and with two full decks: from the boot take's end to each deck's restore.
4. **CRC cost** over 16 MB in the foreground, software table against the H7's
   CRC unit, both at load (back to back) and at save (per piece).
5. **Placement and memory.** Where FatFs and the take code land (ITCM, SRAM,
   `QSPI_COLD`), and the AXI SRAM left over. The stream probe image had
   30 088 B AXI free (`shell/sd_probe.cpp`, the `g_buf_axi` note); the play image's
   figure is unmeasured.

## 7. Verification

Host (ctest), each test shown RED once:
- File layout: encode → decode round trip; each corruption (magic, version,
  deck, `seq = 0`, CRC, oversize `frames`, unsorted or out-of-range markers)
  rejected; slot choice by `seq`, including neither slot valid.
- Restore: take → export frames and markers → `sampler_restore` into a fresh
  engine → identical `rec_size`, identical markers and strengths, and an
  identical overdub pass (P4 as a test, with the engine, not the buffer alone).
  RED: restore through `load_sample()` instead, which must fail on the
  strengths (P2).
- Watermark: an engine on memory filled with a non-zero pattern records a
  fresh take; the buffer must equal the same take on zeroed memory. RED: the
  same test with the watermark bypassed. Likewise after `clear()` without a
  memset, and after a rejected load.
- Save state machine: trigger after 5 s quiet; no save right after a restore;
  abort on REC, on a clear and on a generation change between the two reads of
  the sequence lock; clear saves `frames = 0`; three failures stop saving.
- Generation: every transition listed in §4.3 changes it, nothing else does,
  and P5's late fade-out end changes it.

Board (Rev A #1, card as is):
- Record a take on each deck, wait for the save, power-cycle: both takes play,
  the `SHELL_TAKE` line shows the slot and `seq` loaded.
- Overdub, power-cycle: the overdub is there.
- SHIFT + REC A, wait, power-cycle: deck A is empty, deck B is not. The clear
  itself does not overrun the callback (`cb_max`).
- Cut power **during** a save (Bastian, by hand): the previous take loads.
- Boot with no card, and with a card whose slot files are deliberately
  corrupt: the board plays, the bad slot is rejected, the log says so, and
  `remote_run.py boot` still reflashes it.
- Pots during a save and during the boot load: the §6.1 numbers, with Bastian
  turning pots.
- The files open in a computer audio editor at 48 kHz float stereo.
- Panel persistence still restores (`SHELL_PERSIST src=flash`), and a QSPI save
  and a take save that fall due together both complete — on the persist stress
  image (`write_shell_persist_stress.py`), since the play image's 5-minute
  floor makes that collision rare.

## 8. Calls I made that Bastian may overturn

- 32-bit float files rather than 16 bit (P1).
- Automatic save after 5 s of quiet, no save gesture.
- SHIFT + REC as the clear gesture.
- Paced saves (pots live, a 42 s take needs ~41 s) rather than a burst (≈ 3 s
  with the pots frozen, D2).
- Load after audio starts: the board plays at once, the takes arrive ≈ 2 s per
  full deck later, the pots frozen meanwhile.
- One take per deck, no library.
- P5 documented, not fixed: a take interrupted by an engine switch is saved
  only once the deck is back on SAMPLER.
- No lamp for "a save is running". The REC lamp's states were set by eye
  (`led_law.hpp`), so showing a pending save there (for example a slow blink)
  needs Bastian; until then the only way to know it is safe to switch off is to
  wait ~5 s plus the save time.

## 9. Left to the builder

- Preallocation by `f_lseek` or a shadow `ffconf.h` with `_USE_EXPAND` (§4.1).
- The exact `ffsm` field layout and the CRC polynomial (the H7 unit's default,
  if §6.4 picks it).
- How the foreground loop interleaves take saves with the 500 ms report pass
  and the QSPI save.
- The `SHELL_TAKE` line's fields.
- The whole-load deadline value.

## 10. Review, 2026-11-06

An independent review of the first draft found sixteen points; the ones that
changed this spec:
- the clear ran a 16 MB memset in the callback, and the buffer is never zeroed
  on the board → the written-frame watermark (§3.3, §4.5, §4.7);
- loading before `StartAudio()` would leave a hung card unrecoverable without
  hands → the load runs after the listener and the boot take (§4.4);
- `f_expand` is compiled out (§3.3, §4.1);
- one directory sector for all slots was a shared point of failure → one
  directory per slot (§4.1);
- D2's own numbers predicted a ~19 % hold at the first pace → 42 ms per piece
  and a 10 % gate (§4.3, §6.1);
- a boot would have re-saved every restored take → seed after restore (§4.3);
- an engine switch mid-take leaves the deck recording, measured as P5 (§3.2,
  §4.3);
- the CRC also runs at save; the abort is a sequence lock; boot lines are
  latched; timing uses blocks or DWT; validity and build-switch details; the
  persistence collision test needs the stress image.

The persistence spec's reason for booting REC off ("the buffer is empty after
power-up", its §4.3) is stale once this is built; the rule itself still holds,
because REC is a performance latch.
