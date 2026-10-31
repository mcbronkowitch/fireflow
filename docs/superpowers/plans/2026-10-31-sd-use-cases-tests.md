# SD use-case tests Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Probe images, readers and board runs that answer the feasibility
questions P1, A1, A3, C1, C2, B1, B2, D1-D5 and H1 of the spec, each
result written to `docs/hardware/sd-use-cases-measured.md`.

**Architecture:** The SD bring-up image (`SHELL_SD=1`) gains a probe mode
switch `SHELL_SD_PROBE`; each mode replaces the report loop's stream pass
with its own pass and prints its own lines. A separate listener-image
switch `SHELL_RGRAIN` adds a throwaway grain layer into the reverb input
for B1/B2. Every probe schedules its own phases (idle against active,
parameter sweeps) from a counter, so one run interleaves its conditions and
no compile-time constant has to be swept. The schedules, the pot hold and
the grain grant are pure functions with host tests; the readers are plain
Python scripts with host tests on synthetic captures, wired into ctest.

**Tech Stack:** C++17 firmware in `shell/` (libDaisy, FatFs via
`USE_FATFS`), doctest host tests in `tests/`, Python readers in `shell/`
and `tools/`, `tools/remote_run.py` for the board.

**Spec:** [`../specs/2026-10-31-sd-use-cases-tests-design.md`](../specs/2026-10-31-sd-use-cases-tests-design.md)

## Global Constraints

- No write to the QSPI patch sectors (0x907FE000 / 0x907FF000) from any SD
  code; D4 reads the record, never writes it.
- Every board session is announced to the moderating session
  (`local_28355466-a99d-490c-99ae-eb072504d3d3`) before the board is taken,
  and the board is left on the image it names (default: main's listener).
- Flash from a clean build of a committed tree, as a pair, QSPI first:
  `remote_run.py boot --expect-build <stamp>` then
  `remote_run.py flash shell/build/shell-qspi.bin shell/build/shell-sram.bin --expect-build <stamp>`.
  Every capture's boot print shows `SHELL_PERSIST src=flash seq=581` (or
  the current seq).
- Firmware build: `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH" make -C shell clean`
  then `make -C shell -j8 images <flags>`. Desktop: `source env.sh`,
  `cmake --build build`, `ctest --test-dir build -E "^reva_"` (ctest does not
  build).
- The SD probe flags always include `SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1
  SHELL_REMOTE=1 SHELL_RAW_SPREAD=1 SHELL_SD=1`.
- Judge cycle changes by window means on interleaved phases, never by one
  worst block (`docs/gotchas.md`: ±135 k between runs).
- Nobody judges sound in this round; the Twin is not used.
- Saved text is English; commit trailer
  `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- Never `cd`; no shell writes into the repo (Edit/Write only).

## Review Focus

- A capture whose lines carry `remote_run.py`'s host-time prefix
  (`1000.701 SHELL_...`): every reader must strip it, or it silently
  finds no lines. Pinned in Task 2 (`strip_host_time`) and used by every
  reader after it.
- A card error mid-run (`fr != 0`): a probe must stop its transfers,
  keep printing, and the reader must report the error instead of averaging
  over zeros. Pinned in Task 5's and Task 8's reader tests.
- The window sum tearing against `n` (the callback adds a block between
  the foreground's two reads): mean off by a block. Pinned in Task 3
  (read n, sum, n; retry).
- A grant computed from a callback that tripped the guard (silenced
  blocks are short): the cap would grant everything right after a trip.
  Pinned in Task 4's `rgrain_grant` test (a silenced block grants 0).
- `FFSDTEST.BIN` already on the card from the bring-up: the rename must
  delete it, or the bootloader keeps finding it. Pinned in Task 1.

---

### Task 1: P1 — the test file leaves the bootloader's search

**Files:**
- Modify: `shell/sd_probe.cpp` (kName, a root listing after mount)
- Modify: `shell/read_sd.py`, `shell/test_read_sd.py`
- Modify: `shell/README.md` (the `SHELL_SD` row: file name, the root line)

**Interfaces:**
- Produces: line `SHELL_SD root=<n> bin=<k> names=<A,B,...>` once after
  mount (names: the first 8 root entries, 8.3, comma-separated, ≤ 100 B);
  `read_sd.parse_root(lines) -> dict | None` with keys `n`, `bin`, `names`.

- [ ] **Step 1: Write the failing test.** In `test_read_sd.py`:
  `test_root_line_parsed` (a capture with `SHELL_SD root=2 bin=0
  names=FFSDTEST.DAT,SYSTEM~1` → `parse_root` gives `bin == 0`,
  `names == ["FFSDTEST.DAT", "SYSTEM~1"]`) and `test_bin_in_root_fails_card`
  (`bin=1` → `card_verdict` false, the report names the file).
- [ ] **Step 2: Run it, expect FAIL** (`python shell/test_read_sd.py`:
  `parse_root` not defined).
- [ ] **Step 3: Implement.** `kName = "FFSDTEST.DAT"`; after mount and
  before the previous-file check, `f_unlink("FFSDTEST.BIN")` (FR_NO_FILE is
  fine, print nothing extra), then list the root with `f_opendir` /
  `f_readdir` and print the root line; `bin` counts names ending in `.BIN`
  case-insensitively. `parse_root` + the verdict rule in `read_sd.py`.
- [ ] **Step 4: Run the test, expect PASS;** prove RED once by making
  `card_verdict` ignore `bin` (it must go red), then restore.
- [ ] **Step 5: Build** the bring-up flags, clean; `f_unlink` and
  `f_readdir` in `shell/build/shell.nm`.
- [ ] **Step 6: Commit** `feat(shell): SD test file leaves the bootloader's .bin search`.

### Task 2: Reader plumbing — host-time prefix and SD lines in runner captures

**Files:**
- Modify: `shell/read_sd.py`, `shell/test_read_sd.py`

**Interfaces:**
- Produces: `read_sd.strip_host_time(lines) -> list[str]` (drops a leading
  `<float> ` from each line, leaves other lines as they are). Every
  `parse_*` in `read_sd.py` calls it first.

- [ ] **Step 1: Failing test** `test_runner_prefix_stripped`: the good
  synthetic capture with `"1000.701 "` prefixed to every line gives the same
  `card_verdict` and stream stats as without.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** with `re.compile(r"^\d+\.\d+ ")`.
- [ ] **Step 4: Run, expect PASS;** RED proven by removing the call.
- [ ] **Step 5: Commit** `test(shell): read_sd reads remote_run captures`.

### Task 3: The window mean on every remote image

**Files:**
- Modify: `shell/panel_scan.cpp` (`g_win_cb_sum`, the `SHELL_PLAY_WIN` line)
- Modify: `tools/run_stats.py`, `tools/test_remote_lib.py`

**Interfaces:**
- Produces: `SHELL_PLAY_WIN win=.. gap=.. over=.. n=.. sum=..` (sum: the
  callback cycles of the window's n blocks, uint32; a 500 ms window is at
  most ~250 × 1.9 M); `run_stats` summary gains `mean` (per frame sum/n,
  then p50/p99/mean over counted frames) next to `win`.

- [ ] **Step 1: Failing test** in `test_remote_lib.py`:
  `test_win_mean_from_sum` — frames with `n=250 sum=225000000` and
  `n=250 sum=230000000` give a summary mean of 910 000; a frame without
  `sum=` (an older image) leaves `mean` absent instead of 0.
- [ ] **Step 2: Run** `python tools/test_remote_lib.py`, expect FAIL.
- [ ] **Step 3: Implement.** Callback: `g_win_cb_sum += dc`, reset with the
  others. Foreground, before printing: read `n`, `sum`, `n` again and retry
  while the two `n` differ (at most 3 tries, then print `sum=0 n=0`).
  Only the `SHELL_PLAY_WIN` line carries `sum`; the stress image's save
  line does not.
- [ ] **Step 4: Run, expect PASS;** RED proven by dropping the `/n`.
- [ ] **Step 5: Build** a plain listener image
  (`SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1 SHELL_REMOTE=1`), clean; the
  images build guard still passes (`python shell/check_images_build.py`).
- [ ] **Step 6: Commit** `feat(shell): SHELL_PLAY_WIN carries the window's cycle sum`.

### Task 4: Schedules, the pot hold and the grain grant (pure logic)

**Files:**
- Create: `shell/sd_schedule.h`, `shell/sd_schedule.cpp`
- Create: `tests/test_sd_schedule.cpp`
- Modify: `CMakeLists.txt` (both into the host test list, beside
  `shell/pot_owner.cpp` / `tests/test_pot_owner.cpp`)

**Interfaces (namespace `shell`):**
- `struct BurstPhase { bool on; uint16_t period_ms; uint8_t tail; };`
  `BurstPhase burst_phase(uint32_t w);` — raw window w → phase
  `(w / 10) % 12`; even phases idle; odd phases 1,3,5 at 83 ms with tail
  0,1,2; 7,9,11 at 41 ms with tail 0,1,2. (83 ms per 32 KB ≈ 395 KB/s,
  41 ms ≈ 799 KB/s.)
- `struct SdHold { uint8_t left = 0; bool step(bool transfer_seen, uint8_t tail); };`
  — called once per block; returns true when this block's pot readings are
  dropped: a block that saw a transfer drops and sets `left = tail`; else
  a block with `left > 0` drops and counts down.
- `uint8_t rgrain_n(uint32_t frame);` — B1's N per 500 ms frame: phases of
  20 frames, the sequence {0, 8, 2, 4, 4, 2, 8, 0} repeating.
- `bool rgrain_cap_on(uint32_t frame);` — B2: phases of 60 frames, off
  first, then alternating.
- `uint8_t rgrain_grant(uint32_t last_cb, uint8_t last_granted, bool last_silenced, uint32_t budget, uint32_t per_grain, uint8_t max);`
  — works from the previous block's cost without the layer, so the grant
  does not feed back on itself (review, 2026-10-31):
  `base = last_cb - last_granted * per_grain` (clamped at 0); `0` if
  `last_silenced` or `base >= budget`, else
  `min(max, (budget - base) / per_grain)`.

- [ ] **Step 1: Failing tests** in `tests/test_sd_schedule.cpp`:
  - `burst_phase`: w 0-9 idle; w 10 → on, 83, tail 0; w 50 → on, 83,
    tail 2; w 70 → on, 41, tail 0; w 120 → idle (wraps).
  - `SdHold`: tail 2: transfer at block 0 → drop at blocks 0, 1, 2, keep
    at 3; tail 0: drop only at the transfer block; a transfer during a
    countdown restarts it.
  - `rgrain_n`: frame 0 → 0, 20 → 8, 60 → 4, 80 → 4, 159 → 0, 160 → 0.
  - `rgrain_cap_on`: 0 → false, 60 → true, 119 → true, 120 → false.
  - `rgrain_grant` (args: last_cb, last_granted, last_silenced, budget,
    per_grain, max): (900 000, 0, false, 920 000, 18 000, 8) → 1;
    (900 000, 4, false, 920 000, 18 000, 8) → 5 (base 828 000);
    (700 000, 0, …) → 8 (capped); (930 000, 0, …) → 0;
    (50 000, 8, false, …) → 8 (base clamps at 0, not wrapped);
    (500 000, 0, true, …) → 0.
- [ ] **Step 2: Run, expect FAIL** (does not compile:
  `shell/sd_schedule.h` missing).
- [ ] **Step 3: Implement** the five in `sd_schedule.{h,cpp}`; no hardware
  types.
- [ ] **Step 4: Build and run** `cmake --build build` then
  `ctest --test-dir build -E "^reva_"`, expect PASS; RED proven once
  (flip SdHold's countdown to `left - 2`).
- [ ] **Step 5: Commit** `feat(shell): SD probe schedules, pot hold and grain grant (host tested)`.

### Task 5: The probe mode switch and A1's burst mode

**Files:**
- Create: `shell/write_shell_sd_probe.py`
- Modify: `shell/Makefile`, `shell/test_writer_refusals.py`,
  `shell/check_images_build.py`, `CMakeLists.txt` (guard comment count)
- Modify: `shell/sd_probe.h`, `shell/sd_probe.cpp`, `shell/panel_scan.cpp`
- Modify: `shell/read_sd.py`, `shell/test_read_sd.py`, `shell/README.md`

**Interfaces:**
- `SHELL_SD_PROBE ?= stream`, values `stream burst rtail wtail bank buffer
  duplex patch`; any value but `stream` needs `SHELL_SD=1` (Makefile
  error otherwise). The writer `write_shell_sd_probe.py OUTPUT NAME
  [STALE...]` writes `#define SHELL_SD_PROBE_<NAME> 1` and
  `#define SHELL_SD_PROBE <index in the list>`, the write_shell_sd.py shape
  (deletes its output on any failure, stale objects on a change; stale:
  `sd_probe.o`, `panel_scan.o`).
- `shell/sd_probe.h`: `extern volatile bool g_sd_seen;` (set by the
  foreground before every `f_read`/`f_write`, cleared by the callback);
  `uint8_t sd_hold_tail();` (the current phase's tail; 0 outside burst).
- `panel_scan.cpp` under `#if SHELL_SD`: once per block, `drop =
  g_sd_hold.step(seen, sd_hold_tail())` with `seen = g_sd_seen;
  g_sd_seen = false;`; while `drop`, the block's pot readings go neither to
  `pot_row_step()` nor into `g_raw_lo/hi`; counters `g_sd_held_blocks`,
  `g_sd_hold_max` (longest run of dropped blocks).
- Line, every pass in burst mode:
  `SHELL_SD_S w= on= mode=burst per= tail= kib= ms= kib_s= worst_us= bad= fr= held= hmax=`.
- `read_sd.py`: classes keyed by `(on, per, tail)`; `report` prints per
  class and sense pin worst/median kept spread, pots over band, pots over
  `kPotTouch` (144), held share.

- [ ] **Step 1: Failing tests.** `test_writer_refusals.py`: the writer
  refuses `nope` and leaves no output, writes on `burst`.
  `test_read_sd.py`: `test_burst_classes` (synthetic capture with idle,
  (83,0) and (83,1) windows: three classes, the spread of each class from
  its own windows only); `test_burst_error_reported` (`fr=1` on a burst
  line: the class is reported as `error`, not averaged).
- [ ] **Step 2: Run both, expect FAIL.**
- [ ] **Step 3: Implement** the switch (Makefile value check, the
  dependency on `SHELL_SD=1`, header edge on `sd_probe.o` and
  `panel_scan.o`), then the burst pass in `sd_probe.cpp`: in a burst phase,
  one 32 KB verified `f_read` of `FFSDTEST.DAT` every `per` ms (wrapping),
  waiting between reads with the bus clock stopped; idle phases read
  nothing; a sticky `fr` stops reads. `stream` mode keeps today's
  behaviour plus `mode=stream` in its line. Then the hold wiring and the
  reader.
- [ ] **Step 4: Run the tests, expect PASS;** RED proven for
  `test_burst_classes` by keying classes on `on` only.
- [ ] **Step 5: Images guard.** Add `reva_sd_burst` (the SD flags +
  `SHELL_SD_PROBE=burst`) to `IMAGES` with SD_PATH linked and SAVE_PATH
  absent, and `reva_sd_probe_no_sd_refused` (`SHELL_SD_PROBE=burst`
  without `SHELL_SD`) to `EXPECT_REFUSED`; run
  `python shell/check_images_build.py`, expect PASS.
- [ ] **Step 6: Commit** `feat(shell): SHELL_SD_PROBE and the burst mode with a pot hold (spec A1)`.

### Task 6: A3's buffer alternation and the callback by phase

**Files:**
- Modify: `shell/sd_probe.cpp` (a second 32 KB transfer buffer in AXI SRAM)
- Modify: `shell/read_sd.py`, `shell/test_read_sd.py`

**Interfaces:**
- In `stream` mode, on-phases alternate the buffer: phase index
  `(w / 10)`; on-phases with `(w / 20) % 2 == 0` use the SDRAM buffer,
  the others the AXI one (`alignas(32)` in `.bss`, so the driver's
  invalidate stays on its own lines). The line gains `buf=sdram|axi`.
- `read_sd.cb_by_phase(lines) -> dict` keyed `idle`, `sdram`, `axi`:
  each a list of per-frame means (`sum / n` from `SHELL_PLAY_WIN`, the
  frame assigned to the latest `SHELL_SD_S` before it) plus `win` maxima.
  The report prints mean of means, p99 of `win`, max `win`, and the
  verdict: |mean(stream) − mean(idle)| ≤ 9 600 cycles, and no `win` over
  945 000.

- [ ] **Step 1: Failing test** `test_cb_by_phase`: frames after an
  `on=0` line go to `idle`, after `on=1 buf=axi` to `axi`; a 10 000-cycle
  mean difference fails the verdict, 5 000 passes; a frame with `sum=0 n=0`
  is skipped, not counted as zero.
- [ ] **Step 2: Run, expect FAIL. Step 3: Implement. Step 4: Run, PASS;**
  RED proven by counting `n=0` frames.
- [ ] **Step 5: Build** the stream flags clean; commit
  `feat(shell): SD stream alternates its buffer; read_sd prices the callback by phase (spec A3)`.

### Task 7: Board session 1 — P1, A1, A3

Announce to the moderator first; ~2 h board time.

- [ ] **Step 1:** Create `docs/hardware/sd-use-cases-measured.md` with
  §1 Setup (board, card, builds, seq) and one section per test, empty.
- [ ] **Step 2: P1.** Flash the stream image (Task 6 commit). Capture
  60 s with `python shell/read_play.py` recording; `read_sd.py`: root
  line `bin=0`. Ask the moderator whether Bastian can watch the LED at one
  boot; else record "SOS not observed".
- [ ] **Step 3: A1.** Flash the burst image. Two captures of 5 min each,
  Bastian's patch, hands off (`read_play.py`). `read_sd.py` on each.
  Record per class the table of the spec's pass; name the smallest
  passing tail, or "no tail passes" (then C/B/D go on with back-to-back
  transfers, per the spec).
- [ ] **Step 4: A3.** Flash the stream image. For each workload 145, 90,
  99, 127, 108: `remote_run.py run --load <id> --seconds 300 --out
  <dir>`; `read_sd.py <dir>/serial.txt`. Record mean idle / sdram / axi,
  p99, max, verdict per workload.
- [ ] **Step 5:** Leave the board on the image the moderator names. Commit
  the results (`docs(hardware): SD use cases -- P1, A1, A3 on Rev A`).
  Append A1's result to row 11.2 / fault R5 in `reva-bring-up.md`.

### Task 8: C1/C2 — latency tails (`rtail`, `wtail`)

**Files:**
- Modify: `shell/sd_probe.cpp`, `shell/read_sd.py`, `shell/test_read_sd.py`,
  `shell/README.md`

**Interfaces:**
- `rtail`: once, if `FFSDBIG.DAT` is missing or not 512 MiB, create it
  with `f_expand` (contiguous) and fill it with the pattern; then phases
  of 10 min: `seq` (32 KB sequential), `rand` (32 KB at random 32 KB-aligned
  offsets, xorshift32 seeded by the DWT cycle count), `randfs` (`rand` with
  a fast-seek CLMT, `_USE_FASTSEEK` is 1 in libDaisy's ffconf.h). The
  transfer pattern is A1's (bursts at 41 ms with the passing tail) when A1
  passed, else back to back; a build constant `kTailBurst` says which.
- `wtail`: phases of 10 min writing `FFSDW.DAT` (truncated at the start)
  at 192 KB/s (16-bit stereo), 288 KB/s (24-bit), then full speed.
- Line every 10 s: `SHELL_SD_H kind=<seq|rand|randfs|w16|w24|wmax> n= worst_us= seek_worst_us= b=<16 counts>`,
  `b[i]` counting calls with latency in [2^i, 2^(i+1)) × 64 µs, the last
  bucket open.
- `read_sd.tails(lines) -> dict` per kind: merged histogram, worst, p99.9,
  and the ring size `2 × worst × rate` (rate 384 KB/s for reads, 288 KB/s
  for writes), rounded up to 32 KB.

- [ ] **Step 1: Failing tests** `test_tails_merge` (two `SHELL_SD_H` lines
  of one kind merge their buckets; worst is the max) and
  `test_tail_ring_size` (worst 20 000 µs at 384 KB/s → 16 KB → rounded
  to 32 KB).
- [ ] **Step 2-4:** FAIL, implement, PASS; RED proven by summing worsts.
- [ ] **Step 5: Images guard:** no new image entry (the mode switch is
  already guarded); build both modes clean.
- [ ] **Step 6: Commit** `feat(shell): SD latency-tail probes (spec C1, C2)`.

### Task 9: The reverb grain layer (`SHELL_RGRAIN`)

**Files:**
- Create: `shell/write_shell_rgrain.py`, `shell/rgrain.h`, `shell/rgrain.cpp`
- Modify: `shell/Makefile`, `shell/test_writer_refusals.py`,
  `shell/check_images_build.py`, `shell/panel_scan.cpp`, `shell/README.md`
- Modify: `engine/instrument.h`, `engine/instrument.cpp` (the inject hook)
- Modify: `shell/test_remote_wiring.py` (the hook's per-block read)
- Create: `shell/read_rgrain.py`, `shell/test_read_rgrain.py`; ctest
  `read_rgrain_guard` beside `read_sd_guard`

**Interfaces:**
- `SHELL_RGRAIN ?= 0`, values `0 1 2` (1: B1's fixed-N sweep, 2: B2's
  cap A/B); needs `SHELL_REMOTE=1`, excludes `SHELL_SD=1` and
  `SHELL_REMOTE_MEASURE=1`. Non-zero adds `-DSPKY_PROBE_REVERB_INJECT` to
  `C_DEFS`. The writer is the `write_shell_remote_measure.py` shape
  (OUTPUT VALUE BUILD_DIR, sweeps every `*.o` on a change), because the
  define reaches engine objects.
- Engine, only under `SPKY_PROBE_REVERB_INJECT`:
  `void Instrument::set_reverb_inject(const float* l, const float* r);`
  — `process()` reads both pointers once at the top of the block into
  locals and adds `l[i]`, `r[i]` to the reverb's two inputs when non-null.
  Without the define the hook does not exist.
- `shell/rgrain.h` (namespace `shell`): `void rgrain_init();` (fills a
  2 s stereo sample in SDRAM, a sine sweep 100 Hz-4 kHz plus noise at
  −12 dBFS, behind a `spky::SampleBuffer`); `void rgrain_block(uint8_t n,
  float* l, float* r, uint32_t frames);` — runs the first n of 8
  `spky::Grain`s (respawned when inactive: 100 ms, attack and decay a
  quarter each, random start, ratio 1) into `l`/`r`; grains past n are not
  processed.
- `panel_scan.cpp` under `#if SHELL_RGRAIN`: before `Instrument::process`,
  n = `rgrain_n(frame)` (1) or `cap_on ? rgrain_grant(last_dc,
  last_granted, last_silenced, 920000, kRgrainCycles, 8) : 0` (2); the
  frame counter is the `SHELL_PLAY_WIN` frame count. Line per frame, right
  after `SHELL_PLAY_WIN`: `SHELL_RGRAIN mode=<1|2> n=<fixed n or -1> cap=<0|1> granted_sum= blocks= swing= rms_x1e6=`
  (swing: the largest |grant − previous block's grant| in the frame, so
  B2 shows whether the cap oscillates; rms of the layer's output over the
  frame).
- `kRgrainCycles`: a constant in `rgrain.h`, 17 900 until Task 10 writes
  B1's measured slope into it.
- `read_rgrain.py`: B1 — frames grouped by n, mean of `sum/n` per n, a
  least-squares slope against n, the slope of the first and second half of
  the rounds, the 10 % agreement verdict, rooms per workload; B2 — per
  `cap`: worst `win`, mean, guard trips (`SHELL_REMOTE guard tripped` lines
  in the phase), granted distribution, the verdict
  `worst(cap) ≤ worst(off) + kRgrainCycles` and `trips(cap) ≤ trips(off)`.
  Both check `rms_x1e6 > 0` exactly when `n > 0` (or `granted_sum > 0`).

- [ ] **Step 1: Failing tests.** `test_writer_refusals.py`: the rgrain
  writer refuses `3`. `test_read_rgrain.py`: `test_slope_fit` (means
  800 000 + 18 000·n for n in {0,2,4,8} → slope 18 000 ± 1, halves agree);
  `test_slope_halves_disagree` (second half's slope 21 000 → verdict
  fails); `test_cap_verdict` (off worst 963 000, cap worst 975 000 with
  slope 18 000 passes; 990 000 fails; one extra trip fails);
  `test_silent_layer_fails` (n=4 frames with `rms_x1e6=0` fail).
  `test_remote_wiring.py`: `set_reverb_inject`'s pointers are read in
  `instrument.cpp` only before the per-sample loop.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement** switch, engine hook, layer, wiring, reader.
- [ ] **Step 4: Run tests, expect PASS;** RED proven for
  `test_silent_layer_fails` by dropping the rms check. Desktop build and
  `ctest -E "^reva_"` green (the hook compiles away there).
- [ ] **Step 5: Images guard:** `reva_rgrain` (`SHELL_PANEL_SCAN=1
  SHELL_KEYS_NC=1 SHELL_REMOTE=1 SHELL_RGRAIN=1`) linked, with a symbol
  rule for `shell::rgrain_block(`; `reva_rgrain_with_sd_refused` in
  `EXPECT_REFUSED`; a plain listener image carries no `rgrain_block`.
- [ ] **Step 6: Commit** `feat(shell): throwaway reverb grain layer for pricing (spec B1, B2)`.

### Task 10: Board session 2 — C1, C2, B1, B2

Announce first; ~5 h board time, may be split.

- [ ] **Step 1: C1, C2.** Flash `rtail` (Task 8), capture 30 min plus the
  file creation; then `wtail`, 30 min; twice each on different days if
  the moderator agrees, else two captures back to back. `read_sd.py`;
  record histograms, worsts, ring sizes; pass = the two runs' worsts
  within 2×.
- [ ] **Step 2: B1.** Flash `SHELL_RGRAIN=1`. For 145, 90, 99, 127, 108:
  `remote_run.py run --load <id> --seconds 240 --out <dir>` (three
  rounds of the eight-phase sequence). `read_rgrain.py`; record slope per
  workload, the halves, rooms at mean / p99 / worst.
- [ ] **Step 3:** Write B1's slope (the median over workloads, rounded up
  to 100) into `kRgrainCycles`; commit
  `feat(shell): kRgrainCycles from B1's board slope`.
- [ ] **Step 4: B2.** Flash `SHELL_RGRAIN=2` from that commit. For 90,
  99, 127, 117, 145: `run --load <id> --seconds 1200`. `read_rgrain.py`;
  record the verdict per workload, the granted distribution and the
  swing (a swing near 8 every frame is oscillation, not headroom); name it
  if the previous-block grant missed a block's own spike (a `win` over the
  off-run's worst + slope in a cap phase).
- [ ] **Step 5:** Board back to the moderator's image; commit results
  (`docs(hardware): SD use cases -- C1, C2, B1, B2`).

### Task 11: D1, D2, D4 probe modes (`bank`, `buffer`, `patch`)

**Files:**
- Modify: `shell/sd_probe.cpp`, `shell/wt_bank_d2.cpp`, `shell/panel_scan.cpp`,
  `shell/read_sd.py`, `shell/test_read_sd.py`, `shell/README.md`
- Modify: `engine/synth/wt_bank.h` (the `set_bank` comment)
- Create: `tests/test_wt_bank_swap.cpp` (into the host test list)

**Interfaces:**
- `bank`: a second slot `g_bank_d2b[spky::wt::kTotalSamples]` in
  `.d2_bss` (`shell::wt_bank_slot(int) -> int16_t*`); once, the factory
  bank is written raw (int16 LE, `kBankSamples` layout) to `FFBANK.WTB`;
  then phases of 10 s alternate idle and loading: every 2 s the file is
  read into the slot that is neither the active one nor a pending one,
  compared with `kBankSamples`, and `g_bank_pending` set; the callback
  calls `spky::wt::set_bank(pending)` at the top of the next block and
  clears it. A load does not start while a swap is pending. Line per
  load: `SHELL_SD_K load_us= worst_us= cmp=<ok|bad> held= fr= slot=`.
- **The swap rule (review, 2026-10-31).** Read at planning time:
  `WtOsc::_read_frame` (`engine/synth/wt_osc.h:181`) calls `wt::table()`
  on every sample read, and nothing else in `engine/`, `shell/` or `host/`
  takes a bank pointer (`wt_bank.cpp`, `wt_bank.h`, `wt_osc.h`,
  `shell/wt_bank_d2.cpp` are the only users). So no pointer into a slot
  survives a block, and a swap at block top is safe for the next block.
  The builder re-checks this before Step 3 and stops if a cache has
  appeared. `set_bank`'s comment becomes: "Call before anything plays,
  or from the audio thread at the top of a block. The bank is read through
  `bank()` per sample and never cached across blocks
  (test_wt_bank_swap.cpp holds that). The SD probe (`shell/sd_probe.cpp`,
  `SHELL_SD_PROBE=bank`) is the only caller that swaps while playing."
- `buffer`: a scratch `spky::SampleBuffer::Frame` array of `kSamplerFrames`
  in `.sdram_bss` (16.1 MB; 29 MiB are free). Cycle: fill with the pattern
  (word index hash, as float bits), save as `FFBUF.F32` (raw frames), clear,
  load, compare all; then the same as `FFBUF.S16` (int16 per channel,
  `lrintf(x * 32767)` of a pattern mapped to [−1, 1)), compare within
  1 LSB. Idle 10 s between operations. Line per operation:
  `SHELL_SD_B fmt=<f32|s16> op=<save|load> ms= worst_us= held= fr= cmp=<ok|bad|-> off=`.
- `patch`: `persist_latest()`'s record bytes to `PATCH.FFP` with `f_sync`,
  read back and compare, 100 times, 1 s apart; line
  `SHELL_SD_P i= write_us= sync_us= read_us= cmp= seq=`. Reads QSPI only.
- The callback by phase (Task 6's `cb_by_phase`) applies: busy phases are
  those with an operation line in them.
- `read_sd.py`: `bank_stats`, `buffer_stats`, `patch_stats`, each with an
  error case.

- [ ] **Step 1: Failing tests:** `tests/test_wt_bank_swap.cpp`,
  `bank swap takes effect on the next read`: a `WtOsc` renders 64 samples
  on the factory bank; `set_bank()` to a copy with every sample negated;
  the next sample equals the negation of what a second, identical `WtOsc`
  left on the factory bank produces for its 65th sample (± 1e-6); then
  `set_bank(nullptr)` restores. Goes red if any pointer into the old bank
  is held across the swap (prove it once by caching
  `wt::table(frame, mip)` in a member for a moment). Then
  `test_bank_cmp_bad_fails`,
  `test_buffer_times_and_cmp` (f32 save 2 000 ms, load 1 300 ms, cmp ok →
  pass; an `s16` cmp=bad → fail with its offset),
  `test_patch_100_of_100` (99 ok + 1 bad → fail).
- [ ] **Step 2-4:** FAIL, implement, PASS; RED proven once per stats
  function.
- [ ] **Step 5:** Build each mode clean; check `.d2_bss` ≤ 0x40000 and
  `.sdram_bss` ≤ 64 MiB in `shell/build/shell.map` for `bank` and `buffer`.
- [ ] **Step 6: Commit** `feat(shell): SD probes for bank reload, buffer save/load, patch file (spec D1, D2, D4)`.

### Task 12: D3 — continuous stream and record (`duplex`)

**Files:**
- Modify: `shell/sd_probe.cpp`, `shell/panel_scan.cpp`, `shell/read_sd.py`,
  `shell/test_read_sd.py`, `shell/README.md`

**Interfaces:**
- Two rings in SDRAM, sizes `kReadRing` / `kWriteRing` from C1/C2's
  result rows (written in as constants with the row cited). The callback
  takes 96 stereo float frames a block from the read ring (summed into a
  volatile checksum, not played) and pushes the master output as 24-bit
  packed stereo into the write ring. The foreground refills the read ring
  from `FFSDBIG.DAT` (left on the card by Task 10's `rtail` run; the mode
  refuses with `fr=` FR_NO_FILE if it is missing) in 32 KB reads when it
  is below half, and drains the
  write ring to `FFREC.DAT` in 32 KB writes, with A1's hold (or back to
  back if A1 failed).
- Line per pass: `SHELL_SD_D w= under= over= rfill= wfill= worst_r_us= worst_w_us= held= fr=`.
- `read_sd.duplex_stats`: totals of under/over, the minimum read-ring and
  maximum write-ring fill, the pot classes as Task 5, the callback as
  Task 6.

- [ ] **Step 1: Failing test** `test_duplex_underrun_fails` (one
  `under=1` → fail) and `test_duplex_clean_passes`.
- [ ] **Step 2-4:** FAIL, implement, PASS; RED proven.
- [ ] **Step 5: Commit** `feat(shell): SD duplex probe, stream and record (spec D3)`.

### Task 13: Board session 3 — D1, D2, D4, D3

Announce first; ~2 h.

- [ ] **Step 1: D1** on workload 127 (`remote_run.py run --load 127
  --seconds 600`): `read_sd.py`; pass: every load cmp=ok, callback within
  the 1 % bound; record load time.
- [ ] **Step 2: D2** on 145, 600 s: record save/load times per format,
  cmp, callback.
- [ ] **Step 3: D4**, 120 s: 100 of 100; leave `PATCH.FFP` on the card.
- [ ] **Step 4: D3** on Bastian's patch, 10 min, hands off
  (`read_play.py` capture): 0 under/over, pots as A1, callback as A3.
- [ ] **Step 5:** Board back to the moderator's image; commit results.

### Task 14: D5 — firmware update from the card (Bastian present)

No code. Needs the moderator to get Bastian's OK and his card reader.

- [ ] **Step 1:** Confirm `PATCH.FFP` from Task 13 is on the card (the
  `patch` image prints it, or Bastian's PC lists it).
- [ ] **Step 2:** Bastian copies the board's current `shell-sram.bin`
  (from the build the board runs, stamp checked) to the card root as
  `FW.BIN`; the card holds no other `.bin` (P1).
- [ ] **Step 3:** Boot; Bastian watches the LED (normal grace blink, SOS,
  or a flash); capture the boot print: stamp and `SHELL_PERSIST seq`.
- [ ] **Step 4:** Bastian removes `FW.BIN`. Record the behaviour in
  `sd-use-cases-measured.md` (D5) and whether a packer is the next step.
  If the board does not come back: DFU recovery with the pair, then
  check seq.

### Task 15: H1 — C_SD2 before and after (when Bastian solders)

- [ ] **Step 1:** Pick one committed build each of `stream` and `burst`;
  note their stamps in the results doc.
- [ ] **Step 2: Before**, on the soldering day: 5 min each, Bastian's
  patch, hands off; `read_sd.py`.
- [ ] **Step 3:** Bastian fits C_SD2 (10 µF).
- [ ] **Step 4: After:** the same two builds, the same captures and reader.
- [ ] **Step 5:** Record before/after per sense pin against row 11.2's
  band H; update row 11.2 and fault R5; if `stream` passes after, say that
  A1's hold may not be needed. Commit.

### Task 16: Close the round

- [ ] **Step 1:** `docs/hardware/sd-use-cases-measured.md` gets a summary
  table: per idea, which tests passed, the numbers a feature spec would
  quote, and what stays open.
- [ ] **Step 2:** `shell/README.md` rows for every probe mode and
  `SHELL_RGRAIN`; `ctest --test-dir build -E "^reva_"` green including
  `shell_images_build_guard`, `read_sd_guard`, `read_rgrain_guard`.
- [ ] **Step 3:** Commit; report to the moderator. No merge or push
  without the moderator's or Bastian's OK.
