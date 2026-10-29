# CPU hunt on Rev A — measured remotely, flashed rarely

**Date:** 2026-10-29
**Status:** approved by Bastian, 2026-10-29, on the condition that the
important review findings are folded in ("Arbeite die wichtigen findings
dann einbinden, schreibe dann gleich den Plan"). Three items in §9 still
need his answer; the plan carries them as its first task.
**Phase:** during bring-up (P7), on Rev A #1.

**Inputs:** the brainstorming of 2026-10-29 with Bastian (his decisions are
marked *Bastian*); a builder check and an independent review of the first
draft (2026-10-29, read-only, against the code at main `39b9a3ed`); the CPU
headroom record `docs/hardware/cpu-headroom-measured.md` and its spec
`2026-10-18-cpu-headroom-design.md`; `docs/hardware/persist-measured.md`;
`docs/hardware/reva-bring-up.md`; the clicks session of 2026-10-28.

## 1. Intent

The audio callback's reserve on Rev A is gone on a real patch. On
2026-10-28 Bastian's own patch (deck A WAVE, deck B SYNTH) ran at a steady
`cb_last` median of ~900 k (p99 ~927 k) and reached a `cb_max` of 966 806,
against a deadline somewhere between ~930 k and ~960 k (`docs/gotchas.md`,
cpu-headroom §3). That session heard clicks, but their shape — 1–2 samples
toward zero, mostly on one channel — is **not** the dropped-block signature
(two discontinuities one block apart, cpu-headroom §3), and `gap` never
exceeded ~960 150. Whether the clicks are CPU at all is open.

This round looks thoroughly for cheap savings that change the sound little
or not at all, across **all engines** (*Bastian*), and does it **without
Bastian at the board**: he has other projects to run and wants to flash as
rarely as possible (*Bastian*: "ab und zu ein neues image aufspielen geht
auch nur nicht so oft").

**Not yet measured, and re-measured first (§5.0):** the Rev A overhead over
the coupon. A figure of 831 k for the init patch on Rev A exists only in a
session memory, not in `docs/`, and with persistence on, the boot take writes
every pot's position, so "init patch" on Rev A is not the init patch. The
memory figures `SRAM_EXEC` 56 972 B free (2026-10-24, before persistence) and
`ITCM_HOT` ~15 kB free (2026-10-18) are stale too.

## 2. What Bastian decided

- **The board stays connected** for days: USB to the PC, the audio output
  on the Apollo Twin (*Bastian*: "kein problem").
- **Claude flashes on its own** once an image with a remote bootloader entry
  is on the board (*Bastian*: "darfst du selber machen"). Bastian's hands are
  needed only when an image cannot be reached any more.
- **Sound may change, within limits.** A saving audible only in a direct A/B
  comparison is acceptable (*Bastian*: "wäre ok", to the example of a reverb
  at half its internal rate).
- **All engines** (*Bastian*: "alle engines").
- **VCV: whichever is simpler** (*Bastian*: "kann in vcv gleich bleiben",
  then "was einfacher ist — wenn es einfacher ist, dass VCV auch die
  Änderungen bekommt, dann ist das auch in Ordnung"). See §4.
- **His patch may come out sounding somewhat different** (*Bastian*: "Der
  Patch kann ruhig etwas anders klingen, das ist kein Problem"). No restore
  guarantee, no final check (§5.6).

## 3. Goals, non-goals, anti-goals

**Goals.**

1. A per-section cost split of each block on Rev A, for Bastian's patch and
   for each engine's worst case: deck A, deck B, mix/FX, control law, panel
   scan and lamps, gate latch, persistence tick — and the time spent outside
   the callback (interrupts, USB, libDaisy's conversion), measured, not
   inferred (§5.4).
2. A ranked list of savings, each priced in board cycles on Rev A, each with
   its class (§4).
3. The accepted savings landed on `main`. **Target (Claude's proposal, not
   contradicted by Bastian): worst block ≤ 880 000 cycles** on Bastian's
   patch and on the worst case of every engine **that the method of §5.1
   finds** — panel-reachable states only: knob vectors plus the panel
   gestures of §5.2, no setter states the panel cannot produce (*Claude's
   call*, put to Bastian and not contradicted). The bench's synthetic rows
   are measured for information, not held to the target. Where an engine
   cannot reach the target with acceptable savings, the record gives its
   number.
4. **A real answer to what the 2026-10-28 clicks are** — a goal in its own
   right, not a by-product, because the target can be met while the clicks
   stay (§5.5).

**Non-goals.** No new features, no panel or control changes, no re-tuning of
anything in `docs/by-ear-decisions.md` (a saving that would move one of those
values goes to Bastian with a recording). P6b-1's Plan B (the control law
across two blocks) is re-opened only if the split shows the law is a top
cost.

**Anti-goals** (failure even if the numbers drop):

- Bastian has to come to the board more than once (the setup visit, §5.3),
  apart from a true recovery case.
- A class-2 saving lands that Bastian never heard (or, for panel-side
  savings, never saw).
- The board is left on a measurement image. The round ends on a play image
  of `main` (`SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1`, memory
  `fireflow-reva-play-image-flags`).
- A saving is reported from desktop numbers. Desktop ranks; DWT on the board
  sizes (memory `fireflow-daisy-control-cost`: the desktop/board ratio swung
  between 85× and 120×, and it differs by code path — SDRAM vs ITCM/SRAM).

## 4. Classes of saving

1. **Same result, cheaper.** Not recomputing control-rate values whose
   inputs did not change, cheaper math with the same output, moving work out
   of the audio callback, splitting it across blocks without changing what is
   heard, seen or felt, the `-O3` exceptions (`-O3` is already the shell's
   global default, `shell/Makefile:508`; the candidates are the units that
   are deliberately not at `-O3` — `control_tick` at `-O2` because `-O3` cost
   14.3 KB, `mux_plan` at `-Os`). Taken on cycle numbers plus a render or
   recording showing no change beyond noise.
2. **Audible only in A/B.** Reduced internal rates, fewer taps or stages,
   coarser control rates for slow modulation — and **`-ffast-math`**:
   `-ffinite-math-only` can fold away the engine's `isfinite`/NaN guards
   (`shell/Makefile:36-42`; guards in `engine/center/transport.h`,
   `engine/fx/bbd.h`, `engine/instrument.cpp`), so it is at least class 2
   and lands only with those guards proven to survive (a test that goes red
   when a guard is folded). Each class-2 saving goes to Bastian as an A/B
   pair; he decides.
3. **Plainly audible.** Listed with its price, not built unless Bastian asks.

**Savings that skip silent voices or stages lower the average, not the worst
block.** They are priced on non-silent worst cases only, and do not count
toward the target unless they lower the worst block.

**Panel-side savings** (scan, lamps, keys) are judged by what is seen and
felt (*Claude's call*). Keeping the lamp refresh (`kLedSteps` = 8 on the
block-rate latch), the pot-to-sound latency and the key debounce timing at
today's values is class 1; changing any of them is class 2 and goes to
Bastian to look at, batched with the listening.

**VCV.** Default: one shared engine path. An accepted saving in `engine/`
changes VCV too; the render hashes in `tests/check_render_hash.cmake` are
updated on purpose in the same commit with the reason (no bit-exactness gates
beyond those — CLAUDE.md, memory `fireflow-bit-exactness-not-required`). A
firmware-only compile-time switch is used only where that is simpler; then
the desktop suite builds and passes with the switch on too. A shared saving
must hold in VCV's conditions, which differ from the firmware's: VCV calls
`process(..., 1)` per sample at Rack's rate, 44.1–192 kHz
(`host/vcv/src/Fireflow.cpp`). A saving that spreads work over 96-sample
blocks or assumes 48 kHz may save nothing there or change behaviour, so:
`test_block_size_invariance` stays green, the saving is checked at another
sample rate, and every VCV-touching change ends with
`host/vcv/build-local.sh install` (memory `fireflow-vcv-finish-with-install`).

## 5. How the round runs

### 5.0 Re-measure the inputs at `main`

Before any design of candidates: a Rev A play image of current `main` with
the existing probes (`SPKY_PROBE_MARKS`, `SHELL_CB_WINDOW`) gives the current
split and the current link map (`SRAM_EXEC`, `ITCM_HOT` free). This is the
first image the setup visit flashes, or folded into the measurement image
(§5.3) — the plan decides; no figure from §1 is used before it is
re-measured.

### 5.1 Desktop: a law-driven sweep, built first

The desktop renderer (`host/render/`) drives setters from a scenario at
`process(..., 1)`; `control::ControlLaw` runs only in `tests/`. The sweep
the round needs does not exist and is built first: a desktop harness that
feeds knob vectors through `ControlLaw` at n = 96 per block, per engine and
pairing, and reports per-block cost. It ranks knob vectors and candidates,
and scans the hot path's disassembly for libm calls and setters that
recompute unchanged values (the FEED denormal round, engine-map §9, is the
pattern for silent-but-running stages).

Because desktop ranking does not reliably transfer (§3), the board measures
the desktop's top-N vectors **plus corner vectors** (every knob of a deck at
its extremes, in the combinations the desktop rates expensive). Goal 3's
"worst case" is the worst of that set.

Pairings (*Claude's call*): each engine on both decks, each engine against
Bastian's other deck, Bastian's patch as booted. Not every candidate is
priced on every workload — only on the workloads where the desktop shows it
matters; the full matrix would be many board-hours.

### 5.2 Workloads: static vectors and gestures

A **workload** is a knob vector fed through `ControlLaw` — the path
`SHELL_BOOT_PATCH` uses (`apply_boot_patch_rows`). Loading one is itself a
transition: a deck entering BBD fires a `bbd_edge`, and `apply_law_events`
sets FLUX to 0. A load therefore re-arms the law (`on_restore()`, as
`control_rearm_after_take()` does), waits until the swap settles, and only
then resets the maxima and starts counting.

A static vector measures the law at its cheapest: its sent-value cache calls
no setter when nothing changed. The largest overruns seen on Rev A were
**gestures**, so the workload set also contains scripted gestures, replayed
by the image at a fixed rate: an ENGINE turn (law spike 20 846), TIDE / PULL
/ MORPH sweeps (30 776, R4), a deck swap onto BBD, a reverb clear
(`CBP_RVCLR`), the MOD clear. They are measured as worst blocks during the
gesture.

**Non-silent material.** A Sampler deck is silent until REC has recorded
something, and BBD needs excitation. Each workload states its material:
neighbour-deck excitation where the engine offers it, otherwise a signal on
the board's audio input (§9, item 2). A worst case measured on silence does
not count.

**Determinism.** A workload load restarts the seed and the transport, so two
runs of one workload are comparable. Stop criterion: for a deck with a
pattern, at least two of its own pattern periods; for a deck without one
(FEED, FLOW-mode decks), a fixed duration chosen from an A/A run (§5.4) so
that the p99.9 is stable within the noise floor.

**Pot drift.** Over days a pot can drift past `kPotTouch` (144) and quietly
take a workload over (Rev A's longest measured spread is 57 counts, over
≤ 70 s; RV2 drifted unexplained on the coupon). Every ownership change during
a run is logged; a run with one is void.

### 5.3 The setup visit and the measurement image

**One visit by Bastian**, in this order:

1. He flashes the measurement image by hand (QSPI first, then SRAM with
   `:leave`, CLAUDE.md).
2. He patches the cables for §9 item 2 (both outputs to the Twin; the
   board's input if needed).
3. **He stays for the first remote flash.** The remote bootloader entry has
   only ever run from the bench's `main()` after a run, with audio stopped
   (`bench/main.cpp:93`); the shell would enter it with SAI and ADC DMA
   running and QSPI memory-mapped, and `board_init()` overwrites
   `boot_info.version` with v6_1 (`src/hw/board.h`), so whether the module's
   bootloader honours `DAISY_INFINITE_TIMEOUT` is unproven on this board.
   Claude flashes the next image remotely while Bastian can still press the
   buttons if it fails.

The **measurement image** is a Rev A play image plus a build switch for this
round:

- **Section timing.** The `SPKY_PROBE_MARKS` split extended by the shell's
  sections, `SHELL_CB_WINDOW`'s per-frame worst block / gap / late count, and
  `cb_profile`'s worst-block breakdown and histogram (`CBP_WORST`, `CBP_REC*`)
  — section maxima come from different blocks, so the worst block's own
  breakdown is the number that adds up.
- **A USB command channel** on the CDC port the shell already logs on.
  Commands: load a workload, start a gesture script, toggle a candidate,
  reset maxima, inject a late block (§5.5), enter the bootloader.
- **A self-limiter.** USB and the audio DMA both sit at NVIC priority 0
  (`lib/libDaisy/src/usbd/usbd_conf.c:100-106`, `src/sys/dma.c`); the DMA
  stream has the lower exception number and wins, so a callback that runs
  back to back starves USB and the foreground — `docs/gotchas.md` records
  over-budget blocks that "starved the USB log". A workload that pushes the
  callback over budget would cut the command channel. After N consecutive
  over-budget blocks the image drops to silence (or the booted patch) and
  says so, as `SHELL_CPU_PROBE` already does (`shell/main.cpp:145-156`).
  "Over budget" has two signatures, because 12–30 k of every block run
  outside the callback's measured window (`docs/gotchas.md`): the callback's
  own cycles above a limit, or the period from one callback's entry to the
  next above the block length for N blocks in a row — the callback runs back
  to back with its own cycles under the limit. Once tripped, nothing the
  image injects (the measurement image's late-block burn) keeps running.
- **The bootloader entry runs in the USB receive callback**, not the
  foreground, so it works while the foreground is starved but the callback
  still leaves gaps between blocks. Under true saturation — the callback,
  with what runs around it, takes a block or longer and runs back to back —
  the USB interrupt is starved by the same priority tie and no command, BOOT
  included, arrives: the self-limiter above is the only way out of that
  state. It must not call `HAL_Delay` there: SysTick is priority 14, so a
  delay inside a priority-0 handler hangs (the bench's `Delay(100)` drain
  cannot be copied).
- **Toggles are read once per block into a const local**, never from a
  volatile global inside a per-sample loop — global stores in that loop once
  pushed every block over budget (`docs/gotchas.md`). Toggling a stateful
  candidate is itself a transition: maxima are reset after it settles.
- **Persistence saves are suppressed** while the switch is on, so a loaded
  workload does not replace Bastian's patch wholesale (*Claude's call*; the
  saved sectors sit above `0x907C0000`, and a DFU flash of a pair leaves them
  alone, `persist-measured.md`, "Reflash").
- **Self-recovery on boot failures**, because "the image does not boot" is
  the likely failure and today it is a dead end: an ITCM stamp mismatch
  blinks at 5 Hz forever (`shell/main.cpp`), a mismatched pair hard-faults
  before USB comes up (`shell/README.md`), libDaisy's `HardFault_Handler`
  executes `BKPT`, which locks up without a debugger, and USB does not reset
  the board (it runs from 12 V alone). Every image of the round:
  - enters the bootloader instead of blinking when `itcm_load()` fails;
  - routes HardFault (and NMI, MemManage, BusFault, UsageFault) to the same
    entry (vector table copied to RAM first thing in `main()`);
  - optionally arms the IWDG and checks the reset flag at the top of
    `main()` — whether an IWDG survives a software reset into the Daisy
    bootloader is unknown and is probed during the setup visit before it is
    relied on.

**Every image of the round carries a minimal listener** — the bootloader
entry, the workload load, the self-recovery — including the comparison
images without the measurement switch. A clean image without it would be a
dead end that costs Bastian's buttons to leave. Only the round's very last
image (§5.6) is a plain play image of `main`. Whether the minimal listener
should stay in `main` for good is a separate decision, not part of this
round.

**Which image judges what.** Engine worst cases can only be loaded on an
image with the listener, and the measurement image prints far more and costs
more outside the callback (USB already costs ~6 370 cycles per block,
`shell/main.cpp`). So: engine worst cases are judged on the listener image;
the offset between it and a plain image is measured on Bastian's patch and
stated with every number.

### 5.4 Measuring

Claude, alone, through a host runner that timestamps every serial line on the
host (`GetNow()` runs ~25 % slow while audio plays, `docs/gotchas.md`),
reopens the COM port after every re-enumeration (a reset kills the open
handle), and records both Twin inputs with ffmpeg and a wall-clock start
(memory `fireflow-audio-measurement-rig`).

- **A/A noise floor first.** The same workload twice on the same image,
  before any toggle. A candidate's effect counts only above that floor.
- **Statistics.** p50 / p99 / p99.9 per run plus the histogram and the
  worst block's breakdown — not `cb_max` alone (at near-equal means, coupon
  images B and C read 866 k and 894 k). Each candidate is compared against an
  interleaved baseline run on the same image.
- **Time outside the callback.** A foreground loop that reads DWT and adds
  up every jump between reads (time spent in interrupts) gives the total
  busy time per block; minus `cb`, that is the outside-window cost. Measured
  on the listener image and on a plain image of Bastian's patch, because the
  click band's position depends on it.

### 5.5 The clicks

The command channel makes a decisive test cheap: inject a late block at a
host-known time (burn cycles in one callback until it overruns) and record
what a real late block looks like on Rev A through the same capture chain;
plus a constant-load reference run with no clicks expected. If the 2026-10-28
clicks do not look like the injected ones, they are not CPU, and the record
says where else to look (the capture chain is an open suspect). Matching
clicks to frames with late blocks is weak evidence on its own: frames with
blocks over 930 k are common, so coincidences happen; alignment uses host
timestamps, and the Twin's clock drift against the board is measured from
the injected blocks.

### 5.6 Landing and ending

Accepted savings land on `main` through the usual path (branch, tests, the
desktop suite green in Release, review, VCV install where touched). The last
image is a plain play image of `main`, flashed remotely (it needs no listener
because nothing follows it), booting Bastian's patch; one run of his patch on
it gives the shipped number. Results go into a record under `docs/hardware/`,
measured facts the next session needs into `docs/engine-map.md`, status into
`docs/roadmap.md`.

### 5.7 What still needs Bastian

- the setup visit (§5.3);
- his hands only if an image cannot be reached any more (§9 item 1 can
  remove even that);
- listening to the class-2 A/B pairs and looking at panel-side class-2
  savings, in one batch;
- the go-ahead for the merge to `main`.

## 6. Constraints

- The listener and measurement code must fit beside the play image without
  pushing hot code out of ITCM (`shell/itcm_placement.py` guards the link);
  the free space is re-measured in §5.0.
- The firmware toolchain never mixes with `env.sh` (CLAUDE.md). make's
  one-second mtime trap applies to images built in quick succession (memory
  `fireflow-bench-stale-object-trap`): clean build per image, `cmp` before
  flashing. DFU readback is useless (memory `fireflow-dfu-readback-useless`),
  so the image's own boot print (its link stamp and build id) is the only
  proof of what runs.
- The bench's `run.py` refuses a dirty tree (memory
  `fireflow-bench-clean-tree-guard`), if the bench is used for a candidate.
- Full ctest runs the minutes-long `reva_*` PCB guards; use `-E "^reva_"`
  when `hardware/` is untouched (memory
  `fireflow-ctest-reva-guards-orphan-kicad`).

## 7. Left to the builder

- The command protocol's syntax; how workloads and gesture scripts are
  encoded.
- Which candidates get a runtime toggle and which a build switch (a compiler
  flag cannot be toggled).
- How many images follow the measurement image, and their order.
- N for the self-limiter; the injected-block size.
- Whether a candidate is also priced on the coupon via the bench.
- The record's layout.

## 8. Risks named, not solved

- The self-recovery paths cover an ITCM mismatch and a HardFault. A hang
  with interrupts masked, or a bad QSPI write that breaks the bootloader
  entry itself, is not covered without the IWDG or §9 item 1.
- **A mismatched or half-flashed pair runs foreign code before the stamp
  check.** `main()` calls `board_init()` → `DaisyPatchSM::Init()` before
  `itcm_load()` compares the ITCM stamp, and with `qspi_cold.ld` that init
  (ADC, DAC, I2C, GPIO) executes from the QSPI image — the other build's, or
  a half-written one. The bootloader entry only follows if that code faults;
  if it hangs or loops, the board is dark without a port. The runner
  therefore retries a failed flash step as a whole pair and never resets or
  sends BOOT while a begun pair is unfinished; whether the IWDG survives a
  reset into the bootloader (and could cover this) is probed at the setup
  visit.
- **Full saturation is covered by the overrun guard alone.** A callback
  that runs back to back starves the USB interrupt (§5.3), so BOOT cannot
  help; the guard trips on N consecutive over-budget callbacks or N
  consecutive late entries. Its period threshold carries a margin above the
  block length for entry jitter (5 000 cycles, chosen, not measured): a
  callback back to back by less than that per block is not caught. With the
  guard switched off on purpose (measurement image) the board needs hands.
- **A fault entry needs a stack the core can still push to.** The fault
  vectors reset the stack pointer before any C runs, but the core pushes its
  exception frame onto the old stack first; a stack pointer already outside
  writable memory faults that push and the core locks up before the entry
  runs. Only an overflow that still lands in writable memory is recovered.
- The desktop sweep can still miss a board-only worst case; the corner
  vectors reduce, not remove, that.

## 9. Open for Bastian (the plan's first task)

1. **Remote power cycling.** A switchable 12 V supply — a smart plug
   controllable from the PC (for example one with a local HTTP API), a
   two-digit euro sum — plus a `dfu-util` wait loop that catches the Daisy
   bootloader's ~3 s window after power-on (visible in `persist-measured.md`)
   would make every failure recoverable without hands. Whether the
   bootloader accepts DFU in that window without BOOT pressed is unverified
   and is tested during the setup visit. Buy one, or rely on the
   self-recovery of §5.3? (If the 12 V comes from a case supply that powers
   other gear, the plug cycles that too.)
   *Bastian, 2026-10-29:* "Nein, ohne" — no plug. The round relies on the
   §5.3 self-recovery and the overrun guard; plan Task 7 Step 7 is skipped. A
   board that wedges without a fault needs BOOT + RESET by hand.
2. **Cabling at the setup visit.** Both board outputs to Twin inputs 1 and 2
   (the clicks were mostly on one channel; stereo A/B pairs need both), and,
   if the board's audio input should carry material for Sampler/BBD
   workloads, a Twin output to the board's input. The Twin must stay at
   48 kHz with input monitoring off for the round's duration — does that
   collide with his other projects?
   *Bastian, 2026-10-29:* both board outputs are on Twin inputs 1 and 2; no
   Twin output goes to the board, because the input material for a deck comes
   from the other deck. "Check the rate once": the device's shared-mode format
   is 48 000 Hz and both channels carry signal (a 4 s capture: −11 dBFS peak on
   each). A DirectShow capture without input options arrives at 44 100 Hz and
   ffmpeg resamples it, so the capture now passes
   `-sample_rate 48000 -sample_size 16 -channels 2` before `-i` (commit
   11275876). Input monitoring was not answered; it does not matter here,
   since no Twin output reaches the board. Note that `workloads.json` still
   records `input: noise`, which is what the desktop sweep fed; on the board
   the audio input jack is unconnected.
3. **Permission prompts.** `dfu-util`, `ffmpeg` and the serial runner would
   each prompt him unless they are allowed for this project (memory
   `subagent-shell-auto-approval`). Allow them for the round?
   *Bastian, 2026-10-29:* "you may use dfu-util, ffmpeg and the measurement
   script" — allowed in the project-local, untracked
   `.claude/settings.local.json`, nothing broader.
