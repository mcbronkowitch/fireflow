# SD card bring-up on Rev A — plan

Spec: [`../specs/2026-10-31-sd-bring-up-design.md`](../specs/2026-10-31-sd-bring-up-design.md).

**Gate:** no SD access on the board before Bastian has confirmed `J_SD`'s
pin 1 (bring-up §3.1), and no boot, flash or serial port while another
session owns the board.

## Task 1 — the switch

- `shell/write_shell_sd.py`, the `write_shell_remote.py` shape (always
  defines the symbol, deletes its output on any failure, deletes the stale
  objects on a flip).
- `shell/Makefile`: `SHELL_SD ?= 0`, value check, requires `SHELL_REMOTE=1`
  and `SHELL_RAW_SPREAD=1`, excludes `SHELL_REMOTE_MEASURE=1`; under
  `SHELL_SD=1`: `USE_FATFS = 1` (before the core Makefile include) and
  `sd_probe.cpp` in `CPP_SOURCES`; the writer in `SWITCH_HEADERS` with
  `panel_scan.o` as its stale object; the header edge on `panel_scan.o`.
- `shell/test_writer_refusals.py`: add `write_shell_sd.py` to the
  OUTPUT {0|1} loop.

## Task 2 — `shell/sd_probe.{h,cpp}`

- `sd_bringup_run()`: mount, previous-file check, write, read back and
  verify; results in a static struct.
- `sd_report(hw, pass)`: the four `SHELL_SD` lines on pass 0 and every 10th.
- `sd_stream_pass(hw, w)`: the 500 ms pass, streaming when `(w / 10) % 2`,
  and its `SHELL_SD_S` line.
- `panel_scan.cpp`: under `#if SHELL_SD`, call `sd_bringup_run()` after
  `remote_listen_start()`, `sd_report()` once per pass, and
  `sd_stream_pass()` in place of `hw.Delay(500)`.

## Task 3 — reader

- `shell/read_sd.py` (bring-up verdict; stream vs. idle raw spread per sense
  pin via `read_play.py`'s parser) and `shell/test_read_sd.py` on a
  synthetic capture, RED proven once; ctest `read_sd_guard`.

## Task 4 — build and commit

- Clean build of the SD flags; `sd_probe` and `f_mount` symbols in
  `build/shell.nm`; SRAM_EXEC free bytes recorded; a clean build of the play
  flags still links and carries no `f_mount`.
- `shell/README.md`: the switch row and the lines.
- Commit.

## Task 5 — on the board (when free and pin 1 confirmed)

- `remote_run.py boot`, `flash QSPI SRAM --expect-build <stamp>`.
- Capture ≥ 60 s with `read_play.py`'s recorder (hands off), then a power
  cycle by Bastian and a second capture for `prev=ok`.
- `read_sd.py` on the captures; results into `reva-bring-up.md` §0.1.1,
  §0.2.4, §3.1, §11 (11.2's result; 11.1 is Bastian's).
- Back to the play image: clean build `SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1`,
  flash QSPI first; the boot print must show `SHELL_PERSIST src=flash
  seq=581`.
