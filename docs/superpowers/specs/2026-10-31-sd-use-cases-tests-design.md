# SD card use cases: what to measure before building any of them

Date: 2026-10-31. Status: approved 2026-10-31 by the CPU-hunt session,
which Bastian made moderator of this plan ("du darfst die specs und pläne
freigeben"), with three fixes made (means on interleaved runs for A3/B1,
B2 against a no-grain baseline, the round-2 grain price). Hardware
changes, the Twin and anything audible stay his.

## 1. Why

J_SD works on Rev A #1 (bring-up row 11.0, spec
`2026-10-31-sd-bring-up-design.md`). Bastian has ideas for the card. This
spec does not design any of them. It decides **which questions have to be
answered on the board first**, in which order, and what counts as an
answer. Every number a later feature spec quotes about the card should come
out of one of these tests (the probe rule).

His ideas, in his words condensed:

1. Single grains from short sound files on the card, sent into the reverb
   in addition to the decks.
2. Long field recordings, grains slowly faded in and out, so: does
   streaming from the card work while the instrument plays?
3. More wavetable banks, reloadable from the card.
4. Save and load the sampler's record buffer to and from the card.
5. Anything else that fits. Proposed by the CPU session: a patch library on
   the card, recording the master out, firmware update from the card
   (Daisy bootloader), logging measurements to the card.

## 2. What is already measured

All on Rev A #1, card fitted, 2026-10-31, unless stated.

| Fact | Value | Source |
|---|---|---|
| Card | FAT32, 61 039 MiB free | row 11.0 |
| Sequential read, 32 KB `f_read`, audio running | 12.1-13.2 MB/s, worst call 5.3 ms | row 11.0 |
| Sequential write, 32 KB `f_write`, 8 MiB | ~8.0 MB/s, worst call 11.9 ms | row 11.0 |
| Pot noise while streaming back to back | every pot over its band; SENSE_2/3 worst 514 / 415 counts, ~30× idle | row 11.2, fault R5 |
| Pot noise idle with the SDMMC clock stopped | the no-SD floor | row 11.2 |
| The pot-takeover threshold `kPotTouch` | 144 counts | `shell/pot_owner.h` |
| One grain of the sampler, per 96-sample block | 21.3 k cycles at 221 per grain-sample before round 2; about 17.9 k at about 186 after G1, G2 and G3 (their separate A/B deltas added, not measured together). B1 replaces both | `cpu-hunt-measured.md` §7, "the three numbers" |
| Noise of the worst block | one 60 s `frame_max` moved by ±135 k between runs of the same image and workload; a change is judged by window means on interleaved runs | `docs/gotchas.md` |
| Worst blocks with no grains at all | w127 ~963 k on main and on the round-2 branch; w90 929 k on main | `cpu-hunt-measured.md` §7 |
| Callback, Bastian's patch on main's play image, 10 min | p50 510 k, p99 727 k, max 739 k | `cpu-hunt-measured.md` §8 |
| Worst top-20 workload / click line | 938 701 / ~945 000 cycles | §7, §3 |
| The 192-sample block | +1-3 grains, not taken | §9 |
| SDRAM used / size | 34.9 MiB (`.sdram_bss` 0x22e3000, the SD image's link map) / 64 MiB | `shell/build/shell.map` |
| Sampler record buffer | 16.1 MB per part (42 s stereo float), 32.3 MB for both | `shell/sdram_mem.cpp` |
| Wavetable bank | 65 024 B (16 frames × 2032 int16), copied to D2 SRAM at boot | `engine/synth/wt_bank.h`, `shell/wt_bank_d2.cpp` |
| D2 SRAM (cacheable part) used / size | 63.5 KiB (`.d2_bss` 0xfe00) / 256 KiB | link map |
| Patch record location | the chip's last two 4 KB sectors, 0x907FE000 / 0x907FF000 | `shell/persist_flash.h` |

Two things read off data already on disk, not yet tests of their own:

- The SD captures of 2026-10-31 carry `SHELL_PLAY_LAW` lines. `cb_last`
  (one block every 500 ms, Bastian's patch) is p50 498 k idle / 500 k
  streaming and max 605 k / 610 k in one capture; in the other, p50 502 k /
  498 k. So no large callback cost from SD traffic is visible, but one block
  per print cannot see a short one. Test A3 measures it properly.
- The Daisy bootloader's documentation
  (`lib/libDaisy/doc/md/_a7_Getting-Started-Daisy-Bootloader.md`) says that
  at every boot it scans the card's root for the first file ending in
  `.bin`, verifies it and flashes it if it differs from QSPI, and that an
  invalid one gives one SOS blink pattern and a normal boot. The bring-up
  image writes `FFSDTEST.BIN` to the root. So Rev A #1 presumably blinks
  SOS at every boot now, and that file would shadow a real firmware file.
  Not observed; prerequisite P1 removes the cause either way.

Arithmetic, not measurement: a 48 kHz stereo stream needs 192 KB/s at
16 bit, 384 KB/s as float, so 1.5-3 % of the measured read rate. A 32 KB
chunk takes ~2.5 ms at the measured rate.

## 3. Scope

**In:** board tests that answer feasibility questions for the five ideas:
noise, callback cost, card latency, memory, timing, grain price, and
whether the bootloader path exists. Each test is a probe image built from
the SD bring-up image's pattern (`SHELL_SD=1` plus a probe mode), its reader
script with a host test that has been seen red once, and a result row in a
new measurement document.

**Out:** the features themselves. No grain layer in the engine, no file
browser, no UI for loading, no file formats beyond what a test needs, no
change to the play image. A probe that has to stand in for a feature (the
grain layer in B1/B2) is throwaway code behind a probe switch and is labelled
so.

**Anti-goals:** a test that cannot fail; a number in a later spec that no
probe printed; any write to the QSPI patch sectors from SD code; an untouched
pot taking its parameter during a test on Bastian's patch.

## 4. The order, and why

What unblocks most comes first. R5 gates every idea that touches the card
while the instrument plays, which is all of them except a load at power-up.
Whether a firmware hold fixes it decides whether the ideas need C_SD2 at
all, and that needs no hardware change, so it goes first. Nothing in
phases P to D depends on C_SD2: Bastian fits it later (not today, perhaps
tomorrow), and its before/after comparison H1 is a step of its own.

| Phase | Test | Answers | Ideas it unblocks |
|---|---|---|---|
| P | P1 rename the test file | the bootloader shadowing | 5 (firmware update) |
| A | A1 bursts with a pot hold | can firmware live with R5 | all |
| A | A3 SD traffic against the callback | does IDMA into SDRAM slow the audio | all |
| C | C1 read latency tail and seeks | ring-buffer size for streaming | 1, 2 |
| C | C2 write latency tail | ring-buffer size for recording | 4, 5 |
| B | B1 the price of a reverb grain | how many grains fit | 1, 2 |
| B | B2 a load-dependent grain cap | can grains run on the worst patch | 1, 2 |
| D | D1 wavetable bank reload | load time, memory, swap | 3 |
| D | D2 sampler buffer save / load | time, integrity, cost while playing | 4 |
| D | D3 continuous stream and record | idea 2 and master recording end to end | 2, 5 |
| D | D4 patch file on the card | write time, read-back, a patch backup | 5, and D5's backup |
| D | D5 firmware update from the card | does the bootloader path work at all | 5 |
| H | H1 C_SD2 fitted, before / after (Bastian solders) | is R5 a supply problem | all; may make A1's hold unnecessary |

C runs after A because its numbers should come from the transfer pattern A
settles on (bursts or back to back). B can run in parallel with C on the
desk side; on the board it comes after A3. D5 comes last because it is the
only test that can leave the board unbootable, and it needs D4's backup of
the patch record first.

## 5. The tests

Every test runs on Rev A #1 through `tools/remote_run.py` from a clean
build of a committed tree, QSPI first, and every capture's boot print must
show `SHELL_PERSIST src=flash seq=581` (or the current seq, if Bastian has
saved since). Board times include flashing and booting. Each board session
is announced to the moderating session before the board is taken. The
tests play Bastian's patch or fixed workloads and change no stored value;
B1 and B2 add grains to what the board plays, but nobody judges the sound
in this round.

**Test data comes from the firmware.** Nothing uploads a file to the card
over USB, so every file a test reads is written by a probe image first: the
bank file is the factory bank from flash, the sample is a generated one,
the large files are the bring-up pattern. Only D5 needs a file from the PC,
through Bastian's card reader.

**If A1 fails.** The tests after A still run: C, B and D do not need clean
pots to give their numbers. They then use back-to-back transfers, record
the pot spread without judging it, and their pot criteria (D3) wait for
H1. A1 failing is a result for R5, not a stop.

### P1. The test file leaves the bootloader's search

- **Question:** does the bring-up file make the bootloader blink SOS, and is
  that gone after the rename?
- **Change:** `sd_probe.cpp` writes `FFSDTEST.DAT`, deletes a leftover
  `FFSDTEST.BIN` once.
- **Measured:** the bootloader's LED at two boots, before and after; this
  needs eyes on the board (Bastian), or is noted as unobserved.
- **Pass:** after the change the root holds no `.bin` file (the image prints
  the root listing).
- **Board time:** 10 min.

### A1. Bursts with a pot hold

- **Question:** if the card is read in bursts and every pot reading taken
  while a transfer is running (plus a tail of N blocks) is dropped, do the
  readings that remain stay at the idle floor?
- **Image:** `SHELL_SD=1` with a burst mode: one chunk of 32 KB every T ms,
  T chosen for 384 KB/s (float stereo) and 768 KB/s (two streams); the hold
  is a flag set around each `f_read` that `panel_scan` reads per block.
  Tail N swept 0, 1, 2.
- **Measured:** per sense pin, the worst and median raw spread of the kept
  readings, burst phase against idle phase (`read_sd.py`, extended); the
  share of held scans and the longest hold in blocks.
- **Pass:** at the smallest tail that passes, 0 of 73 pots over their band
  and every pot's worst spread under `kPotTouch` in burst phases, over two
  runs of 5 min; the held share is printed.
- **Board time:** 45 min.

### A3. SD traffic against the callback

- **Question:** does SDMMC IDMA into SDRAM, which the sampler, echo and BBD
  read from, raise the callback's cycle count?
- **Image:** `SHELL_SD=1` on fixed workloads through the listener (145
  `bastian`, 90, 99, 127, 108), stream phases interleaved with idle phases
  as in 11.2; the transfer buffer once in SDRAM, once in AXI SRAM.
- **Measured:** the per-window callback mean and the per-window worst
  (`g_win_cb_max`), stream against idle, per workload and buffer location;
  p99 and the worst block recorded.
- **Pass:** the mean over stream windows within 1 % of the mean over idle
  windows (9.6 k cycles, about half a grain), on interleaved phases; and
  no window over the click line. The worst block alone is not the basis
  (it moves ±135 k between runs). A miss is a result, not a stop: it
  prices the traffic.
- **Board time:** 60 min.

### C1. Read latency tail and seeks

- **Question:** what is the worst `f_read` over minutes, sequential and at
  random offsets in a large file, so the stream ring buffer can be sized?
- **Image:** `SHELL_SD=1`, a mode that writes one 512 MiB file once, then
  reads 32 KB at sequential and at random offsets for 10 min each, with
  FatFs fast seek on and off.
- **Measured:** latency histogram per access pattern, the worst call,
  `f_lseek` time, read rate; in A1's transfer pattern if A1 passed.
- **Pass:** two runs agree within 2× on the worst call; the result row
  states the ring size it implies (2 × worst latency × 384 KB/s).
- **Board time:** 45 min (the file write alone is ~70 s).

### C2. Write latency tail

- **Question:** the same for writes, which have card-internal garbage
  collection pauses the 8 MiB bring-up write may not have hit.
- **Image:** `SHELL_SD=1`, a mode that writes 32 KB chunks for 10 min at
  the rate of 16- and 24-bit stereo recording and then at full speed.
- **Measured:** latency histogram, the worst call, rate.
- **Pass:** as C1.
- **Board time:** 30 min.

### B1. The price of a reverb grain

- **Question:** what does one grain playing a sample from SDRAM into the
  reverb send cost per block on the board, and how much room is there on
  real workloads?
- **Image:** the listener image (`SHELL_REMOTE=1`, no SD code: the price
  of a grain does not depend on where its sample came from) with a
  throwaway grain layer behind a probe switch: N fixed grains (0, 2, 4, 8)
  reading a generated sample from SDRAM with the sampler's kernel (Hann
  window, linear read), mono, summed into the reverb input.
- **Measured:** the per-window callback mean on the workloads of A3 at each
  N, with N interleaved per round (0-8-2-4, then 4-2-8-0, …) over at least
  three rounds; the slope per grain fitted to the window means against N.
  frame_max (the per-window worst) and p99 are recorded, not fitted. Not
  the §7 CBP section split: the slope of the whole block is what a cap
  needs.
- **Pass:** the slopes of two halves of the rounds agree within 10 %. The
  result row states the slope, and the room per workload in grains at the
  mean, at p99 and at the worst block.
- **Board time:** 90 min (5 workloads × 4 N × 3 rounds × 60 s, plus
  flashing).

### B2. A load-dependent grain cap

- **Question:** with grains granted per block from the previous blocks'
  cycle count, does the worst callback stay within one grain of what the
  same workload reaches with no grains, on the worst workloads?
- **Image:** B1's layer plus a cap: grains granted =
  floor((budget − last callback) / slope), budget a constant.
- **Known risk:** a grant from the previous block cannot see the block's
  own spike (the control tick, sample 0; `cpu-hunt-measured.md` §4). B2
  measures whether that matters; it does not assume it doesn't.
- **Measured:** per workload, runs with grains off and with the cap,
  interleaved: callback worst and window means, overrun-guard trips, the
  distribution of granted grains; on 90, 99, 127, 117 and 145, 10 min per
  run.
- **Pass:** with the cap, the worst callback ≤ the no-grain run's worst +
  one grain's slope (B1), and overrun trips no more than the no-grain
  run's, on each workload. A fixed ceiling cannot be the criterion: w127
  reaches ~963 k with no grains. How the grains coming and going sounds is
  Bastian's, by ear, later and only if he asks; it is not part of this
  pass.
- **Board time:** 2 hours (5 workloads × 2 runs × 10 min, plus flashing).

### D1. Wavetable bank reload

- **Question:** how long does loading a 65 KB bank from the card into a
  second D2 slot take, and does swapping the engine's bank pointer between
  blocks change the callback?
- **Image:** `SHELL_SD=1`, a mode that loads a bank file (the factory bank
  exported raw) every 2 s into the other D2 slot and swaps at a block
  boundary, on a WAVE workload.
- **Measured:** load time, verify against the factory bank, held blocks,
  callback per window against a run without swaps.
- **Pass:** verify ok every load; callback within A3's 1 %; load time
  printed. Whether a swap between different banks clicks is a feature
  question (crossfade or not) and is not tested here.
- **Board time:** 20 min.

### D2. Sampler buffer save and load

- **Question:** how long does saving and loading one part's full buffer
  (16.1 MB float, or 8 MB as 16-bit) take while the instrument plays, and
  does the round trip keep the content?
- **Image:** `SHELL_SD=1`, a mode that fills one part's buffer with a known
  pattern, saves it, loads it into a free SDRAM region (not the live
  buffer), compares, in both formats.
- **Measured:** save and load times, the worst call, held blocks, callback
  per window, compare result.
- **Pass:** float round trip identical, 16-bit within 1 LSB of the
  quantised source; callback as A3.
- **Board time:** 30 min.

### D3. Continuous stream and record

- **Question:** idea 2 and master recording end to end: can a 384 KB/s
  read stream and a 288 KB/s write stream (24-bit stereo) run for 10 min
  with ring buffers sized by C1/C2, A1's hold, on Bastian's patch?
- **Measured:** ring underruns and overruns, pot spread, callback.
- **Pass:** 0 underruns and overruns, pots as A1, callback as A3.
- **Board time:** 40 min.

### D4. Patch file on the card

- **Question:** can the persisted patch record be written to and read from
  the card, and how long does a write with `f_sync` take?
- **Image:** `SHELL_SD=1`, a mode that copies the live record to
  `PATCH.FFP` on the card, reads it back, compares, 100 times. It reads the
  QSPI record; it never writes it.
- **Pass:** 100 of 100 identical; the copy stays on the card as the backup
  D5 needs.
- **Board time:** 15 min.

### D5. Firmware update from the card

- **Question:** does the bootloader flash a firmware file from the card on
  Rev A, and do the patch sectors and the SRAM image survive?
- **Known obstacle:** FireFlow ships two images (QSPI and SRAM) carrying
  one build stamp, and `itcm_load()` refuses a mismatched pair; the
  bootloader takes the first `.bin` it finds. So a new build cannot arrive
  as one of the two files. A packer that joins them into one file is out
  of scope for this round.
- **Steps:** the SRAM image already on the board as `FW.BIN` (the
  bootloader should find it, compare and skip). The QSPI image is not put
  on the card: if the bootloader took it as a program, it would write it
  over the SRAM image's place at 0x90040000.
- **Measured:** the boot print's build stamp and `SHELL_PERSIST` seq; the
  bootloader's LED.
- **Pass:** this test answers a question, so its result is the behaviour:
  either "the bootloader skips identical files, a packer is the next
  step", or what went wrong. The board must boot with the same stamp and
  seq afterwards. If it does not, recovery is the DFU path, and D4's file
  is the patch backup.
- **Needs:** Bastian's OK at the time, since it can leave the board
  waiting in the bootloader, and his card reader, since nothing uploads a
  file to the card over USB.
- **Board time:** 30 min.

### H1. C_SD2 fitted, before and after

- **Question:** does the prepared 10 µF on the SD supply remove R5?
- **Needs:** Bastian solders it, not today, perhaps tomorrow (2026-10-31).
  Nothing else in this plan waits for it.
- **Image:** one build, unchanged between the two runs: the 11.2 image
  (back-to-back stream) and A1's burst image. The "before" runs happen on
  the day of the soldering, right before it, so the A/B differs only by the
  part.
- **Measured:** row 11.2's measurement, the same reader, before and after.
- **Pass:** row 11.2's own criterion, the band H, on the "after" run.
- **Board time:** 2 × 30 min plus soldering.

## 6. Results

One new document, `docs/hardware/sd-use-cases-measured.md`, one section
per test, the same shape as `cpu-hunt-measured.md`. Row 11.2 and fault R5
in `reva-bring-up.md` get A1's and H1's results.

## 7. Calls made in this spec (for the reviewer)

- Feasibility probes only, no features, so a "yes" here means "build the
  feature spec next", not "it works".
- The probe modes extend the SD bring-up image rather than the play image,
  because the listener is what gets the board out of a hung card.
- Test data is written by the probe images themselves; only D5 needs
  Bastian's card reader. No USB upload command this round.
- A1 failing does not stop the rest; pot criteria wait for H1 then.
- B1 runs on the listener image without SD code, measuring the whole
  block's worst against N, not the §7 section split.
- D5 only probes the bootloader with the files we already ship; a packer
  for one combined file is a follow-up.
- A3's 1 % bound on window means (9.6 k cycles, about half a grain) and
  B2's "no-grain worst + one grain" margin are mine (B2's after review).
- A1 drops readings rather than holding the scan, so a held pot keeps its
  last value. Holding the whole scan would also be possible.
- Bank swaps and grain dropout are not judged by ear here.
- D5 is last and needs Bastian's OK at the time.
- Board time, all tests: about 9.5 hours, not counting H1's soldering.
