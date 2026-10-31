# SD card bring-up on Rev A (§11) — design

Date: 2026-10-31. Status: approved scope from the session that started this
work (the CPU-round session); Bastian confirms J_SD's pin 1 before any SD
access runs on the board.

## 1. Why

`J_SD` (Yamaichi PJS008U-3000-0, 4-bit SDMMC on the module's D2-D7) was
deferred on 2026-10-22 and is soldered now, with an empty FAT32 card in it.
The SD card exists for a future generative granular element that reads a
sample from SD into SDRAM, from the main loop, while the audio callback runs
(spec `2026-10-30-cpu-engine-kernels-design.md` §1, on
`feat/cpu-engine-kernels`). This round is bring-up only: does the card mount,
do bytes survive a write and a read, how fast does the main loop read, and
does streaming disturb the pot scan (bring-up row §11.2; the socket hangs on
SM_3V3, A10, which also feeds the pots).

Not in scope: the sample player, WAV parsing, the granular element, any
change to the play image without the switch.

## 2. The image

One new build switch, `SHELL_SD` (0 or 1, default 0), stamped into
`build/shell_sd.h` by `write_shell_sd.py` (the same shape as
`write_shell_remote.py`, including the delete-on-failure rule that
`test_writer_refusals.py` checks).

`SHELL_SD=1` requires `SHELL_REMOTE=1` and `SHELL_RAW_SPREAD=1` (so also
`SHELL_PANEL_SCAN=1` without the coupon) and excludes
`SHELL_REMOTE_MEASURE=1`. The reasons:

- **The listener.** Rev A has no SWD. With `SHELL_REMOTE` a hang or a fault in
  the SD code ends in the bootloader (`BOOT` from the USB interrupt, fault
  vectors), so `tools/remote_run.py` can leave the image without a hand on
  BOOT and RESET.
- **The persisted patch.** The listener image never writes the persistence
  record (`persist_write()` is not linked), so Bastian's patch
  (`seq=581`) cannot move while the SD image runs. The SD code never touches
  QSPI.
- **The raw readout.** §11.2 compares the pots' raw spread with the card
  streaming and idle; `SHELL_PLAY_R` is that readout, and its window counter
  times the stream's phases.
- **Not the measurement image:** its busy loop replaces the very `Delay(500)`
  the stream replaces.

Image flags: `SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1 SHELL_REMOTE=1
SHELL_RAW_SPREAD=1 SHELL_SD=1`, clean build.

USE_FATFS=1 links libDaisy's FatFs (`ff.c`, `diskio.c`, `ff_gen_drv.c`,
`ccsbcs.c`) in this image only. The driver is libDaisy's `SdmmcHandler`
(4-bit, `Speed::FAST` = 50 MHz, the library default) and `FatFSInterface`
(`sd_diskio.c`: DMA with D-cache clean before and invalidate after each
transfer).

## 3. What it does

New file pair `shell/sd_probe.{h,cpp}`, compiled only under `SHELL_SD=1`.

**Memory.** The FatFs objects (`FatFSInterface`, `FIL`) sit in one
`alignas(32)` block in AXI SRAM (`.bss`), the 32 KB transfer buffer in SDRAM
(`.sdram_bss`, where the granular element's sample will live). Both are
IDMA-reachable; the 32-byte alignment keeps the driver's cache invalidate
off any line the audio interrupt writes.

**Bring-up, once** (in `run_panel_scan_report()`, right after the listener
starts, so audio runs and `BOOT` works throughout):

1. Mount `0:` (`f_mount(..., 1)`), report the file system type and the
   card's total and free MiB.
2. If `FFSDTEST.BIN` exists from an earlier boot, verify it against the
   pattern first: data that survived a power cycle (`prev=`).
3. Write `FFSDTEST.BIN`, 8 MiB in 32 KB `f_write` calls, a pattern that is a
   hash of the 32-bit word index; `f_close` (timed: it flushes FAT and
   directory).
4. Open it again and read it back in 32 KB `f_read` calls (they go to the
   card: FatFs keeps no file data beyond its one-sector window), comparing
   every word. No remount: a second card init would test the init, not the
   bytes.

Throughput counts only the time inside `f_write` / `f_read`
(`System::GetUs()`); pattern generation and the compare are outside it. Each
step also reports its worst single call.

**Stream, every report pass** (in place of `Delay(500)`): the raw window
counter `g_raw_win` sets the phase -- windows `w` with `(w / 10) % 2 == 1`
stream, the others idle. In a streaming phase the pass reads the test file
in 32 KB chunks (wrapping at the end) and verifies them, until 500 ms are up;
in an idle phase it waits 500 ms as before. 10 windows per phase give 9
clean windows each (the first window of a phase can hold the previous
phase's last chunk and is not judged).

Every number is measured with the init patch or Bastian's patch playing,
i.e. with the audio callback taking ~80 % of the CPU: that is the condition
the granular element will load under. A figure without audio is not
measured.

## 4. Lines

All within libDaisy's 128-byte log buffer:

```
SHELL_SD mount=<fr> fs=<1..4> total_mib= free_mib= bus=4 mhz=50
SHELL_SD prev=<none|ok|bad> fr= off=
SHELL_SD write kib= us= kib_s= worst_us= fr=
SHELL_SD verify=<ok|bad> kib= us= kib_s= worst_us= fr= off=
SHELL_SD_S w= on= kib= ms= kib_s= worst_us= bad= fr=
```

`fr` is FatFs's `FRESULT` (0 = ok); `off` the first bad byte offset. The
first four lines go out on the first report pass and every 10th after it
(5 s, like `SHELL_REMOTE on`); `SHELL_SD_S` once per pass.

`shell/read_sd.py` reads a capture: it prints the bring-up lines' verdict
and, reusing `read_play.py`'s window parser, the raw spread per sense pin
over the streaming windows against the idle windows, plus the worst stream
call. Its guard `shell/test_read_sd.py` runs on a synthetic capture
(ctest `read_sd_guard`).

## 5. Pass criteria (bring-up §11)

- **Mount and verify:** `mount=0`, `verify=ok` with `fr=0`, and `prev=ok`
  after a power cycle.
- **§11.2:** the pots' raw spread with the card streaming stays within each
  sense pin's band H (64 on SENSE_0, 32 on SENSE_1-3 since fault R3's
  workaround; the row's original H is 16) and is not visibly above the idle
  windows of the same run. If not: fit C_SD2 (10 µF), per the row.
- **Throughput** has no criterion; it is recorded for the granular element.

## 6. Risks

- **A mirrored J_SD costs the card** (§3.1). No SD access runs before Bastian
  has confirmed pin 1.
- **SRAM_EXEC.** FatFs and the HAL SD driver add code to an image that also
  carries the listener; if the link does not fit, FatFs goes to QSPI cold
  code (it is never on the audio path).
- **50 MHz on a hand-soldered socket.** If the bus fails at 50 MHz the
  fallback is `Speed::STANDARD` (25 MHz), a constant in `sd_probe.cpp`.
