# The panel scan, part 1 — scan core, value path, and the coupon's proof

**Date:** 2026-09-28
**Status:** approved in conversation (Bastian, 2026-09-28), section by section
**Predecessor:** [`docs/hardware/scan-budget.md`](../../hardware/scan-budget.md)
— the arithmetic this round tests, and its §7, the check it asks for.

## 1. What this round is, and what it is not

The shell gets its first control path from a mux channel to the engine. Two
images come out of it:

- **`SHELL_SCAN_CHECK`**, a measurement image for the test coupon. It answers
  `scan-budget.md` §7: in the shell's own ADC pattern, does a value written
  one block and read the next come out clean, on every coupon channel, with
  the engine running? It also measures the per-channel noise that sets the
  value path's hysteresis.
- **`SHELL_PANEL_SCAN`**, the playing image. On the coupon, RV2, RV4 and RV6
  drive `RATE_A`, `DENSITY_A` and `FILT_A` (Bastian's choice, 2026-09-28). On
  the panel profile it scans and applies nothing, because the panel's table is
  part 2.

**Part 2 is not in this round:** the 70-pot table, the three keycaps on the
165 and the 19 LEDs on the 595. All three wait for the control PCB's pin map,
which does not exist. (Bastian, 2026-09-28: "erst Teil 1".)

## 2. Two defects in the existing scan, fixed here

**`MuxScan::step()` reads nothing.** It stores `hw.GetAdcValue(i)`, which
returns libDaisy's `AnalogControl::Value()` (`daisy_patch_sm.cpp:443`). That
value only moves when `ProcessAnalogControls()` runs, and nothing in `shell/`
calls it, so every stored value stays at its initial 0.0f (`ctrl.cpp:13,27`).
*Read from the source, not measured;* the check image is the measurement, and
arm P reads a rail tie that must come out near 63485. The 2026-08-23 CPU run
was unaffected: it priced the reads, not their values. The fix reads the raw
DMA word, `hw.adc.Get(i)`, as `coupon_scan.cpp` already does. `AnalogControl`'s
slew filter must never sit behind a mux in any case, because it would average
across channel changes.

**`step()` runs after `process()`.** The ~300 µs slack in `scan-budget.md` §3
assumes exactly one block between the address write and the read. After
`process()`, that interval is 2000 µs plus the difference between two
consecutive `process()` durations. Across the engine's load range that
difference can reach ~740 µs, which is more than twice the slack. **Every new
image calls the scan first in the callback, before `process()`.**
`SHELL_MUX_PROBE`'s images keep their placement: they are the 2026-08-23
measurement's instrument, and moving their call would change what they
measured.

## 3. Components

All in `shell/`. The hardware-free parts are compiled into `spky_tests`, the
same arrangement `mux_plan.h` and `controls.h` already use.

| unit | kind | responsibility |
|---|---|---|
| `mux_plan.{h,cpp}` | host | + `sense_live(profile, step, sense)`: is this sense pin's mux enabled during this step |
| `mux_scan.{h,cpp}` | board | reads raw words, live pins only; `select(step)`, `read_step(hw, step)`; `step()` is read-then-select; `g_mux_raw[]` replaces `g_mux_values[]` |
| `scan_value.{h,cpp}` | host | normalise against a `Span`, clamp, hysteresis, endpoint snap, first emission |
| `controls.{h,cpp}` | host | per-board table (group, ch) → `ParamId`; `apply_control()` scales into the param's range and calls `apply_param()` |
| `scan_check_plan.{h,cpp}` | host | the check image's block layout: which arm, step and action each block of a run is |
| `scan_check.{h,cpp}` | board | the check image: a callback tick and a foreground reporter |
| `panel_scan.{h,cpp}` | board | the playing image: a callback tick, span tracking, and the `SHELL_PLAY` line |
| `read_scan_check.py` | host | reader and gates; guard `test_read_scan_check.py` in ctest |

`Span` is `coupon_expect.h`'s existing struct (`zero`, `rail`, `valid`), and
`coupon_span()` is reused unchanged.

## 4. The check image, `SHELL_SCAN_CHECK=1`

Requires `SHELL_COUPON_PROBE=1`. The engine runs at the shell's fixed
operating point (`set_tempo_bpm(96)`, `RATE_A` 0.4, `DENSITY_A` 0.6), audio is
started, and the scan work runs at the start of the callback. Nothing read
reaches the engine, so the operating point stays fixed through the run, as in
every earlier round.

**One run block, repeated for ever** (the coupon has 24 steps: group 0's 16,
then group 1's 8):

| arm | blocks | what each block does | per step |
|---|---:|---|---|
| **S** — the shipping pattern | 1 + 24 × 64 | block 0: select step 0. Then each block: read the live step, select the next. 64 full sweeps | 64 reads |
| **P** — parked | 24 × 69 | per step: block 0 selects it, blocks 5…68 read it | 64 reads |
| **0** — lag zero | 24 × 64 | each block: select the step, then read it in the same callback | 64 reads |

That is 4729 blocks, ~9.46 s at 2 ms. Per arm and step the callback keeps
`n`, `sum`, `min` and `max` of the raw live-pin word. The foreground prints
the finished run block while the callback fills the next one (double buffer,
swapped at the run-block boundary).

**Output**, integers only, transcribed by the reader from the firmware:

    SHELL_SCAN_CFG blk=<k> block=<frames> sr=<hz> steps=24 git=<hash>
    SHELL_SCAN_CH blk=<k> arm=<S|P|0> step=<s> group=<g> ch=<c> n=<n> sum=<sum> min=<min> max=<max>
    SHELL_SCAN_HEALTH blk=<k> ticks=<blocks counted by the callback> expected=4729
    SHELL_SCAN_END blk=<k>

`sum` fits in 32 bits: 64 × 65535 < 2³².

**Gates, in the reader:**

- **G1, span:** the P means of the rail and AGND ties give a valid span,
  using the same rules as `coupon_span()`.
- **G2, completeness:** `ticks == expected`, and every (arm, step) has
  `n == 64`.
- **G3, the control can fail:** a step *qualifies* if its predecessor is in the
  same group and their P means differ by more than 4096 counts. At least three
  steps must qualify, and on every qualifying step arm 0 must miss its own P
  mean by more than 8 counts. If this gate fails, the comparison below could
  not have failed, and a clean S proves nothing.

**The result:** for every step, `d = mean_S − mean_P`, with the criterion
**|d| ≤ 8 counts**, half an LSB of 12 bit, as in every earlier round. The
reader also prints, informationally, arm 0's mean against its predecessor's
P mean. It is not gated: that arm 0 reads the previous channel is the
expected mechanism, and nothing here gates a mechanism.

**What each outcome says:**
- All 24 steps pass and G1–G3 hold: §7 is answered. The oversampling order
  is the documented one, the slack holds with the engine running, and F1 is
  clean on real pots under a stepping mux.
- Steps with a far-off predecessor fail: a group straddles the address change.
  The pattern is not clean at one step per block, and part 1's playing image
  stops at this point for a new decision.

The reader's exit code is 1 on any failed gate or failed criterion.

**The hysteresis rule.** H is the largest `max − min` in arm S over the seven
pot channels (the coupon's `Unchecked` steps, g0 ch 0/2/4/6 and g1 ch 0/2/4),
across all complete run blocks of the committed capture, rounded up to the
next multiple of 16 counts, and never less than 16: a zero-width band would
emit on every count of noise. The reader prints it. The playing image carries it
as `kPotHysteresis` with the capture as its provenance.

## 5. The playing image, `SHELL_PANEL_SCAN=1`

**Per callback:** the scan's `step()` runs first and returns the step it just
read, or −1 on the very first callback. Then the value path runs for every
live channel of that step that the board's table maps, and then
`process()`.

**Span.** On the coupon, the raw values of the latest reads are kept per step.
After each full sweep, `coupon_span()` runs over them; when it comes back
valid, it replaces the current span, and an invalid one leaves the current
span in place. Before the first valid span, nothing is applied. On the panel
profile, the span is the constant `{0, 63485, valid}`, labelled as the
coupon's 2026-09-17 rail reading, until part 2 gives the panel its own tie
channels (§8).

**Value path** (`scan_value`), per mapped channel:
1. `v = (raw − zero) / (rail − zero)`, clamped to 0…1. The clamp belongs here,
   on the reading side, as `controls.cpp` has always said.
2. **Endpoint snap:** `raw ≤ zero + H` gives v = 0, and `raw ≥ rail − H` gives
   v = 1.
3. **Emit** when the channel has never emitted, when `|raw − last emitted raw|
   > H`, or when the snapped value differs from the last emitted value (so
   that a stop is reached even inside the hysteresis band).
4. **Apply:** `apply_control()` sends `lo + v × (hi − lo)` from `kParams` to
   `apply_param()`. `FILT_A` spans −1…1, and the other two span 0…1.

The fixed `RATE_A` and `DENSITY_A` lines stay in `main.cpp` and run in every
image. In the coupon playing image the pots then override them with their
first emission, once the span is valid. The tempo stays fixed at 96 BPM.

**Not in this image:** no slew beyond the hysteresis. Whether `FILT_A` steps
audibly at 48 ms per sweep is Bastian's call at the coupon; nothing is
claimed about it beforehand.

**Foreground:** twice a second,

    SHELL_PLAY rv2=<v×1000> rv4=<v×1000> rv6=<v×1000> zero=<z> rail=<r> valid=<0|1> sweeps=<n>

On the panel profile the image prints `SHELL_PLAY` with the span and sweep
count only.

## 6. Build switches

Both switches are written as generated headers at Makefile parse time, like
every switch since 2026-08-23. Each one's writer deletes the objects that
read it (`write_shell_scan_check.py`, `write_shell_panel_scan.py`).

- `SHELL_SCAN_CHECK=1` needs `SHELL_COUPON_PROBE=1`. It excludes the settle,
  xtalk, tone and wait probes, `SHELL_PANEL_SCAN`, `SHELL_MUX_PROBE` and
  `SHELL_CPU_PROBE`.
- `SHELL_PANEL_SCAN=1` works with or without `SHELL_COUPON_PROBE=1`, and
  excludes the same set.
- In `main()`, both paths run before `run_coupon_bringup()`. That call is
  reached only by the plain `SHELL_COUPON_PROBE=1` image, as today.
- `scan_check.o` and `panel_scan.o` join `SWITCH_OBJECTS` and the git stamp's
  deletion list.
- Before any flash, build the `SHELL_COUPON_PROBE=1` bring-up image and both
  new coupon images (`+SHELL_SCAN_CHECK=1`, `+SHELL_PANEL_SCAN=1`), and `cmp`
  them pairwise: all three must differ. The stale-object trap produces
  byte-identical images for different switch positions, and this check is
  what catches it.

## 7. Testing

- **doctest** (in `spky_tests`):
  - `sense_live` on both profiles;
  - `scan_value`'s normalise, clamp, snap at both ends, hysteresis inside and
    outside H, first emission, and an invalid span;
  - the control table: the three coupon entries reach `rate(PART_A)`,
    `density` and `FILT_A` with the right scaling, unknown (group, ch) pairs
    change nothing, and the panel table is empty; the coupon entries agree
    with `pot_plan.h`'s `kPots`;
  - `scan_check_plan`'s block layout: 4729 blocks, each arm's first and last
    block, and every step read exactly 64 times per arm.
- **`test_read_scan_check.py`**, in ctest as `read_scan_check_guard`:
  generated blocks for a clean run, a contaminated run (S reads that mix
  predecessors), an incomplete block, a G3 that cannot fail (arm 0 equal to
  P), and an invalid span. Each fixture must drive the verdict it names; the
  RED of G3 is proven once. After board session 1, a section that parses the
  committed capture.
- `test_controls_map.cpp` is rewritten for the table. Its edge case (an index
  that does not exist changes nothing) carries over.

## 8. For part 2, recorded here so it is not lost

The panel needs a span, and on the coupon the span came from 0 Ω ties to AGND
and to the analog rail. **Tie one spare mux channel to AGND and one to the
rail on the control PCB.** Then the panel calibrates itself on every sweep
instead of trusting one coupon's 63485, and a collapsed supply shows as an
invalid span rather than as every pot reading 3 % high. The 16:1 has ten spare
channels at 70 pots (`scan-budget.md` §5).

## 9. Order of work

1. Scan core (`mux_plan` + `mux_scan`), and the callback placement for the new
   images.
2. `scan_value` and the control table.
3. The check image, its plan, the reader and its guard.
4. **Board session 1:** Bastian flashes over DFU, and the capture is committed
   to `docs/hardware/captures/`.
5. Write-up `docs/hardware/scan-measured.md`, H fixed from the capture, and
   `scan-budget.md` §7 answered.
6. The playing image.
7. **Board session 2:** Bastian turns the pots and listens, and `SHELL_PLAY`
   is read alongside.
8. Status documents: `shell/README.md`'s switch table and next steps, and the
   roadmap.

If board session 1 fails its criterion, stop after step 5 and bring the result
to Bastian. The playing image is built on the assumption that the pattern is
clean.
